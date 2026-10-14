#!/usr/bin/env python3
"""Hardware-in-the-loop smoke test (docs/ARCHITECTURE.md §9.5).

Drives the costume over its USB serial console and checks the screen through the webcam:
  reset -> READY -> start Fare Paid -> tap (green) -> tap again (amber) -> unknown card (red)
  -> heap check -> reboot -> scores and cooldowns restored.

Run: make hil   (or tools/hil/.venv/bin/python tools/hil/smoke.py)
Frames are saved in tools/hil/artifacts/.
"""
import colorsys
import pathlib
import random
import subprocess
import sys
import threading
import time

from PIL import Image

from orca_serial import OrcaSerial

ART = pathlib.Path(__file__).parent / "artifacts"
CAMERA = "USB CAMERA"
MIN_INTERNAL_HEAP_KB = 20


def snap(name, delay=0.0):
    """Captures a webcam frame after `delay` seconds; returns the path."""
    time.sleep(delay)
    path = ART / f"smoke-{name}.jpg"
    subprocess.run(["imagesnap", "-q", "-d", CAMERA, "-w", "0.4", str(path)], check=True, capture_output=True)
    return path


def snap_async(name, delay=0.0):
    result = {}
    t = threading.Thread(target=lambda: result.update(path=snap(name, delay)))
    t.start()
    return t, result


def screen_color(path):
    """Classifies the result icon (check / clock / X) above the middle of the screen by hue.

    Screens are black like the real reader, so only strongly coloured pixels are averaged."""
    img = Image.open(path).convert("RGB")
    w, h = img.size
    crop = img.crop((int(w * 0.30), int(h * 0.18), int(w * 0.60), int(h * 0.45))).resize((60, 54))
    raw = crop.tobytes()
    vivid = []
    for i in range(0, len(raw), 3):
        hh, ss, vv = colorsys.rgb_to_hsv(raw[i] / 255, raw[i + 1] / 255, raw[i + 2] / 255)
        if ss > 0.45 and vv > 0.30:
            vivid.append((raw[i], raw[i + 1], raw[i + 2]))
    if len(vivid) < 15:
        return "dark", (0, 0, 0), 0
    r = sum(p[0] for p in vivid) / len(vivid) / 255
    g = sum(p[1] for p in vivid) / len(vivid) / 255
    b = sum(p[2] for p in vivid) / len(vivid) / 255
    hue, sat, val = colorsys.rgb_to_hsv(r, g, b)
    hue *= 360
    if hue < 10 or hue >= 330:  # the webcam shows amber as ~15-35 degrees under warm light
        label = "red"
    elif hue < 70:
        label = "yellow"  # amber (cooldown) or gold (bonus)
    elif hue < 170:
        label = "green"
    else:
        label = "blue"
    return label, (round(r * 255), round(g * 255), round(b * 255)), round(hue)


class Smoke:
    def __init__(self, dev):
        self.dev = dev
        self.failures = []

    def check(self, cond, what):
        print(("  ok   " if cond else "  FAIL ") + what)
        if not cond:
            self.failures.append(what)

    def tap_and_look(self, uid, name, expect_outcome, expect_color):
        t, res = snap_async(name, delay=0.15)
        status, body = self.dev.tap(uid)
        t.join()
        color, rgb, hue = screen_color(res["path"])
        self.check(status == 200 and body.get("outcome") == expect_outcome,
                   f"{name}: outcome {body.get('outcome')} (want {expect_outcome})")
        self.check(color == expect_color, f"{name}: screen {color} rgb{rgb} hue {hue} (want {expect_color})")
        return body


def main():
    ART.mkdir(exist_ok=True)
    with OrcaSerial() as dev:
        s = Smoke(dev)
        print("boot")
        boot = dev.reset_and_wait_ready(30)
        s.check(any("http: HTTP server on port 80" in l for l in boot), "web server started")

        print("start a game")
        status, st = dev.api("GET", "/api/status")
        s.check(status == 200, "status endpoint")
        if st["state"] in ("running", "paused"):
            dev.api("POST", "/api/game/stop")
        dev.api("POST", "/api/enrollment", {"open": True})
        status, _ = dev.api("POST", "/api/game/select", {"id": "fare_paid", "params": {"cooldown": 60000}})
        s.check(status == 200, "select fare_paid")
        status, st = dev.api("POST", "/api/game/start")
        s.check(status == 200 and st["state"] == "running", "game running")
        time.sleep(5.0)  # stopping the previous game shows a 4 s banner
        color, rgb, hue = screen_color(snap("idle"))
        s.check(color == "blue", f"idle screen {color} rgb{rgb} (want blue: the orca swoosh)")

        print("taps")
        uid = "04%012X" % random.getrandbits(48)
        first = s.tap_and_look(uid, "accepted", "accepted", "green")
        time.sleep(3)  # let the result screen time out
        s.tap_and_look(uid, "cooldown", "cooldown", "yellow")
        time.sleep(3)
        dev.api("POST", "/api/enrollment", {"open": False})
        s.tap_and_look("04%012X" % random.getrandbits(48), "unknown", "unknown", "red")
        dev.api("POST", "/api/enrollment", {"open": True})

        heap = dev.command("heap", "HEAP ")
        internal = int(heap.split("internal ")[1].split(" KB")[0])
        s.check(internal >= MIN_INTERNAL_HEAP_KB, f"internal heap {internal} KB free (want >= {MIN_INTERNAL_HEAP_KB})")

        print("reboot and restore")
        time.sleep(6)  # snapshots are debounced to every 5 s
        dev.reset_and_wait_ready(30)
        status, st = dev.api("GET", "/api/status")
        s.check(st["state"] == "running" and st["game"]["id"] == "fare_paid", "game still running after reboot")
        status, body = dev.tap(uid)
        s.check(body.get("outcome") == "cooldown" and body.get("balance") == first.get("balance"),
                f"after reboot: {body.get('outcome')} balance {body.get('balance')} (want cooldown, {first.get('balance')})")

        errors = dev.errors_in_log()
        s.check(not errors, "no errors in the serial log" + ("" if not errors else ": " + " | ".join(errors[:3])))

    print("\nSMOKE " + ("PASSED" if not s.failures else f"FAILED ({len(s.failures)})"))
    return 0 if not s.failures else 1


if __name__ == "__main__":
    sys.exit(main())
