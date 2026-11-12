#!/usr/bin/env python3
"""Exercises a connected Coffee Pal over USB serial. Leaves the counts at zero.

Usage: device_check.py <serial port> <folder for screenshots>
Needs pyserial, which PlatformIO's Python already has. The expected counts come from
src/config.h, so flash the board from the same checkout before running this.
"""
import re
import sys
import time
from pathlib import Path

import serial

port, out = sys.argv[1], Path(sys.argv[2])
out.mkdir(parents=True, exist_ok=True)


def open_port():
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 2
    s.dtr = s.rts = False  # Don't let opening the port reset the board.
    s.open()
    return s


def status(s):
    s.reset_input_buffer()
    s.write(b"i")
    end = time.time() + 3
    while time.time() < end:
        m = re.search(rb"coffees=(\d+) froths=(\d+) left=(\d+) full=(\d+) touch=(\d+)", s.readline())
        if m:
            return tuple(map(int, m.groups()))
    raise SystemExit("FAIL: no status reply")


def send(s, keys):
    for k in keys:
        s.write(k.encode())
        time.sleep(0.2)


def screenshot(s, name):
    s.reset_input_buffer()
    s.write(b"s")
    end = time.time() + 5
    while time.time() < end:
        m = re.match(rb"FRAME (\d+) (\d+)", s.readline())
        if m:
            w, h = int(m[1]), int(m[2])
            data = s.read(w * h * 2)
            if len(data) != w * h * 2:
                raise SystemExit(f"FAIL: short frame {len(data)}")
            ppm = bytearray(f"P6\n{w} {h}\n255\n".encode())
            for i in range(0, len(data), 2):
                v = data[i] | data[i + 1] << 8
                ppm += bytes(((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
            (out / f"{name}.ppm").write_bytes(ppm)
            return
    raise SystemExit("FAIL: no frame")


def expect(label, got, want):
    print(f"{'ok  ' if got == want else 'FAIL'} {label}: {got}")
    if got != want:
        raise SystemExit(1)


# The tray rules the board was flashed with, read from src/config.h.
CONFIG = dict(
    (m[1], int(m[2]))
    for m in re.finditer(r"#define\s+(\w+)\s+(\d+)\b", (Path(__file__).resolve().parent.parent / "src/config.h").read_text())
)
COFFEE, FROTH, CAPACITY = CONFIG["COFFEE_UNITS"], CONFIG["FROTH_UNITS"], CONFIG["TRAY_CAPACITY_UNITS"]
if CAPACITY <= COFFEE + FROTH:
    raise SystemExit("FAIL: these checks need a tray that holds more than one coffee and one froth")
COFFEES_TO_FULL = -(-CAPACITY // COFFEE)


def tray(coffees, froths):
    """The status the board should report for these counts."""
    left = max(CAPACITY - coffees * COFFEE - froths * FROTH, 0)
    return (coffees, froths, left, int(left == 0))


s = open_port()
time.sleep(2.5)  # Opening the port may reboot the board.
start = status(s)
print("initial", start, "touch controller:", "found" if start[4] else "NOT FOUND")
print(f"tray rules: coffee={COFFEE} froth={FROTH} capacity={CAPACITY}")

# Clear whatever is there: a full tray takes one tap, otherwise two.
if start[0] or start[1]:
    send(s, "r" if start[3] else "rr")
time.sleep(0.5)
expect("cleared", status(s)[:4], tray(0, 0))
screenshot(s, "device_1_empty")

send(s, "cf")
expect("1 coffee, 1 froth", status(s)[:4], tray(1, 1))
time.sleep(1.5)
screenshot(s, "device_2_normal")

send(s, "r")
expect("one reset tap keeps counts", status(s)[:2], (1, 1))
screenshot(s, "device_3_confirm")
time.sleep(3.3)
send(s, "r")
expect("expired confirm still keeps counts", status(s)[:2], (1, 1))
time.sleep(3.3)

# Add froths one at a time, checking every step until the tray is full.
froths = 1
while not tray(1, froths)[3]:
    send(s, "f")
    froths += 1
    expect(f"1 coffee + {froths} froths", status(s)[:4], tray(1, froths))
time.sleep(1.5)
screenshot(s, "device_4_full")
full = tray(1, froths)

# Saved counts must survive a hard reset.
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
s.close()
time.sleep(4)
s = open_port()
time.sleep(2.5)
expect("counts survive reboot", status(s)[:4], full)

send(s, "r")
expect("full tray resets on one tap", status(s)[:4], tray(0, 0))
send(s, "c" * (COFFEES_TO_FULL - 1))
expect(f"{COFFEES_TO_FULL - 1} coffees alone is not full", status(s)[:4], tray(COFFEES_TO_FULL - 1, 0))
send(s, "c")
expect(f"{COFFEES_TO_FULL} coffees alone is full", status(s)[:4], tray(COFFEES_TO_FULL, 0))
send(s, "r")
expect("back to zero", status(s)[:4], tray(0, 0))

# Timing, in the idle state and on a busy animated one.
time.sleep(6)
s.reset_input_buffer()
s.write(b"i")
print("idle timing:", s.readline().decode().split("touch=")[-1].strip())
send(s, "c" * COFFEES_TO_FULL)
time.sleep(6)
s.reset_input_buffer()
s.write(b"i")
print("alert timing:", s.readline().decode().split("touch=")[-1].strip())
send(s, "r")
expect("left at zero", status(s)[:4], tray(0, 0))
s.close()
print("all device checks passed")
