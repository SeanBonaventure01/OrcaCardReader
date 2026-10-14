"""Talk to the costume over its USB serial console (see firmware/main/console.cpp).

    from orca_serial import OrcaSerial
    with OrcaSerial() as dev:
        boot_log = dev.reset_and_wait_ready()
        status, body = dev.api("GET", "/api/status")
"""
import glob
import json
import re
import time

import serial

ANSI = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


def default_port():
    ports = glob.glob("/dev/cu.usbserial-*") + glob.glob("/dev/ttyUSB*")
    if not ports:
        raise RuntimeError("no serial port found")
    return ports[0]


class OrcaSerial:
    def __init__(self, port=None, baud=115200):
        self.ser = serial.Serial(port or default_port(), baud, timeout=0.05)
        self.log = []  # every line seen, ANSI codes stripped

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.ser.close()

    def _lines(self, timeout):
        """Yields complete lines until `timeout` seconds pass."""
        buf = b""
        deadline = time.time() + timeout
        while time.time() < deadline:
            buf += self.ser.read(4096)
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = ANSI.sub("", raw.decode(errors="replace")).rstrip("\r")
                self.log.append(line)
                yield line

    def reset_and_wait_ready(self, timeout=20):
        """Resets the board and returns the boot log up to READY. Raises on panic or timeout."""
        self.ser.dtr = False
        self.ser.rts = True
        time.sleep(0.1)
        self.ser.rts = False
        boot = []
        for line in self._lines(timeout):
            boot.append(line)
            if "Guru Meditation" in line or "abort()" in line or "Backtrace:" in line:
                more = list(self._lines(1.0))
                raise RuntimeError("device crashed during boot:\n" + "\n".join(boot + more))
            if " READY " in line:
                time.sleep(0.5)  # let the console prompt come up
                self.ser.reset_input_buffer()
                return boot
        raise TimeoutError("no READY within %ss:\n%s" % (timeout, "\n".join(boot[-40:])))

    def command(self, text, expect_prefix, timeout=5):
        """Sends a console command and returns the first line starting with expect_prefix."""
        self.ser.write((text + "\n").encode())
        for line in self._lines(timeout):
            idx = line.find(expect_prefix)
            if idx >= 0:
                return line[idx:]
            if "Guru Meditation" in line or "abort()" in line:
                raise RuntimeError("device crashed running %r: %s" % (text, line))
        raise TimeoutError("no %r reply to %r" % (expect_prefix, text))

    def api(self, method, path, body=None, timeout=5):
        line = self.command(
            "api %s %s%s" % (method, path, (" " + json.dumps(body, separators=(",", ":"))) if body is not None else ""),
            "API ", timeout)
        m = re.match(r"API (\d+) (.*)$", line)
        if not m:
            raise RuntimeError("bad API reply: " + line)
        return int(m.group(1)), json.loads(m.group(2))

    def tap(self, uid):
        return self.api("POST", "/api/sim/tap", {"uid": uid})

    def errors_in_log(self):
        return [l for l in self.log if re.match(r"^E \(\d+\)", l) or "Guru" in l or "abort()" in l]
