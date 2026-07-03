#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Tadashi Kadowaki
# Part of the CH559 USB mouse host project (distributed under GPLv3).
"""Minimal UART monitor for CH559 debug output.

Captures bytes from the FT232 adapter, prints printable lines, and hex-dumps
non-text frames (the reference firmware emits 0xFE-prefixed binary HID frames).

Run via: uv run --with pyserial tools/serial_monitor.py [port] [baud]
Both arguments are optional and order-independent: a numeric arg is the baud
rate, anything else is the port. With no port given, the port is auto-detected.
"""
import sys
import time

import serial
from serial.tools import list_ports

DEFAULT_BAUD = 115200

# Lower-cased substrings marking a likely USB-serial adapter / disqualifying one.
PREFER = ("usbserial", "usbmodem", "ftdi", "ft232", "cp210", "ch340", "wch", "slab")
EXCLUDE = ("bluetooth", "debug-console", "wlan")


def autodetect_port():
    """Return the best-guess serial device path, or None if nothing suitable."""
    candidates = []
    for p in list_ports.comports():
        dev = p.device
        low = (dev + " " + (p.description or "") + " " + (p.manufacturer or "")).lower()
        if any(x in low for x in EXCLUDE):
            continue
        score = 0
        if any(x in low for x in PREFER):
            score += 10
        if "/cu." in dev:  # on macOS prefer the call-out device over tty.
            score += 2
        candidates.append((score, dev))
    if not candidates:
        return None
    candidates.sort(reverse=True)
    best_score, best_dev = candidates[0]
    # Only auto-pick when it actually looks like a USB-serial adapter.
    return best_dev if best_score >= 10 else None


def parse_args(argv):
    port, baud = None, DEFAULT_BAUD
    for a in argv:
        if a.isdigit():
            baud = int(a)
        else:
            port = a
    return port, baud


def main():
    port, baud = parse_args(sys.argv[1:])
    if port is None:
        port = autodetect_port()
        if port is None:
            sys.stderr.write("[monitor] no USB-serial port found. Available ports:\n")
            for p in list_ports.comports():
                sys.stderr.write(f"    {p.device}  {p.description}\n")
            sys.stderr.write("Pass the port explicitly, e.g. "
                             "tools/serial_monitor.py /dev/cu.usbserial-XXXX\n")
            sys.exit(1)
        sys.stderr.write(f"[monitor] auto-detected port {port}\n")

    ser = serial.Serial(port, baud, timeout=0.2)
    sys.stderr.write(f"[monitor] {port} @ {baud} baud\n")
    sys.stderr.flush()
    line = bytearray()
    while True:
        chunk = ser.read(256)
        if not chunk:
            continue
        for b in chunk:
            if b == 0x0A:  # newline -> flush line
                emit(line)
                line.clear()
            else:
                line.append(b)
        # flush long binary frames that never see a newline promptly
        if len(line) > 200:
            emit(line)
            line.clear()


def emit(buf):
    ts = time.strftime("%H:%M:%S")
    text = bytes(buf)
    if all(32 <= c < 127 or c in (9, 13) for c in text):
        print(f"{ts}  {text.decode('ascii', 'replace').rstrip()}", flush=True)
    else:
        hexs = " ".join(f"{c:02X}" for c in text)
        print(f"{ts}  [HEX] {hexs}", flush=True)


if __name__ == "__main__":
    main()
