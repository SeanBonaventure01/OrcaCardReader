#!/usr/bin/env python3
"""Reset the board, follow its serial log, and take a webcam frame for each `SHOW <name>` line.

Usage: capture_screens.py [--port /dev/cu.usbserial-XXXX] [--out DIR] [--count N] [--timeout S]

Firmware prints `SHOW <name>` right after it puts a test screen up. Frames are written to
DIR/show_<name>.jpg. Requires pyserial (ESP-IDF's Python env has it) and `imagesnap`.
"""
import argparse
import glob
import pathlib
import re
import subprocess
import sys
import time

import serial

CAMERA = "USB CAMERA"
SETTLE_S = 1.0  # let the panel and camera exposure settle before capturing


def default_port():
    ports = glob.glob("/dev/cu.usbserial-*")
    return ports[0] if ports else None


def reset_board(ser):
    # Auto-download circuit: RTS drives EN, DTR drives IO0. Keep IO0 high for a normal boot.
    ser.dtr = False
    ser.rts = True
    time.sleep(0.1)
    ser.rts = False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=default_port())
    ap.add_argument("--out", default="tools/hil/artifacts")
    ap.add_argument("--count", type=int, default=6, help="stop after this many distinct screens")
    ap.add_argument("--timeout", type=float, default=30)
    args = ap.parse_args()
    if not args.port:
        sys.exit("no serial port found")

    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    ser = serial.Serial(args.port, 115200, timeout=0.1)
    reset_board(ser)

    seen, buf, deadline = [], b"", time.time() + args.timeout
    while time.time() < deadline and len(seen) < args.count:
        buf += ser.read(4096)
        while b"\n" in buf:
            raw, buf = buf.split(b"\n", 1)
            line = raw.decode(errors="replace").rstrip()
            if re.search(r"^[EW] \(|Guru|abort|panic", line):
                print("LOG", line)
            m = re.search(r"SHOW (\w+)", line)
            if m and m.group(1) not in seen:
                name = m.group(1)
                seen.append(name)
                time.sleep(SETTLE_S)
                path = out / f"show_{name}.jpg"
                subprocess.run(["imagesnap", "-q", "-d", CAMERA, "-w", "0.5", str(path)],
                               check=True, capture_output=True)
                print("CAPTURED", name, path)
                ser.reset_input_buffer()
                buf = b""
            elif "READY" in line or "PSRAM free" in line:
                print("LOG", line)
    ser.close()
    if len(seen) < args.count:
        sys.exit(f"only saw {len(seen)} of {args.count} screens: {seen}")


if __name__ == "__main__":
    main()
