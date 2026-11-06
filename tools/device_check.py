#!/usr/bin/env python3
"""Exercises a connected Coffee Pal over USB serial. Leaves the counts at zero.

Usage: device_check.py <serial port> <folder for screenshots>
Needs pyserial, which PlatformIO's Python already has.
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


s = open_port()
time.sleep(2.5)  # Opening the port may reboot the board.
start = status(s)
print("initial", start, "touch controller:", "found" if start[4] else "NOT FOUND")

# Clear whatever is there: a full tray takes one tap, otherwise two.
if start[0] or start[1]:
    send(s, "r" if start[3] else "rr")
time.sleep(0.5)
expect("cleared", status(s)[:4], (0, 0, 12, 0))
screenshot(s, "device_1_empty")

send(s, "cccc" + "ff")
expect("4 coffees, 2 froths", status(s)[:4], (4, 2, 6, 0))
time.sleep(1.5)
screenshot(s, "device_2_normal")

send(s, "r")
expect("one reset tap keeps counts", status(s)[:2], (4, 2))
screenshot(s, "device_3_confirm")
time.sleep(3.3)
send(s, "r")
expect("expired confirm still keeps counts", status(s)[:2], (4, 2))
time.sleep(3.3)

send(s, "cc" + "ff")
expect("6 + 4 warns, not full", status(s)[:4], (6, 4, 2, 0))
send(s, "f")
expect("6 + 5 is not full", status(s)[3], 0)
send(s, "f")
expect("6 + 6 is full", status(s)[:4], (6, 6, 0, 1))
time.sleep(1.5)
screenshot(s, "device_4_full")

# Saved counts must survive a hard reset.
s.setRTS(True)
time.sleep(0.1)
s.setRTS(False)
s.close()
time.sleep(4)
s = open_port()
time.sleep(2.5)
expect("counts survive reboot", status(s)[:4], (6, 6, 0, 1))

send(s, "r")
expect("full tray resets on one tap", status(s)[:4], (0, 0, 12, 0))
send(s, "c" * 12)
expect("12 coffees alone is full", status(s)[:4], (12, 0, 0, 1))
send(s, "r")
expect("back to zero", status(s)[:4], (0, 0, 12, 0))

# Timing, in the idle state and on a busy animated one.
time.sleep(6)
s.reset_input_buffer()
s.write(b"i")
print("idle timing:", s.readline().decode().split("touch=")[-1].strip())
send(s, "c" * 12)
time.sleep(6)
s.reset_input_buffer()
s.write(b"i")
print("alert timing:", s.readline().decode().split("touch=")[-1].strip())
send(s, "r")
expect("left at zero", status(s)[:4], (0, 0, 12, 0))
s.close()
print("all device checks passed")
