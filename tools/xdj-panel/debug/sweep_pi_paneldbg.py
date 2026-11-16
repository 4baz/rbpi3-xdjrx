#!/usr/bin/env python3
"""Sweep xdj-panel controls against the Pi runtime bridge.

Runs inj.py pulses/mainkey presses and reports the src=paneldbg key events that
appear in /tmp/rbp-keys.log for each probe.
"""
import os
import re
import subprocess
import time

ROOT = os.path.dirname(os.path.abspath(__file__))
INJ = os.path.join(ROOT, "inj.py")
KEYLOG = "/tmp/rbp-keys.log"

PULSES = [
    ("browse-push", "b 5d 10"),
    ("load1", "b 5d 40"),
    ("load2", "b 5d 80"),
    ("back", "b 5e 01"),
    ("menu", "b 5f 01"),
    ("play1", "b 13 40"),
    ("play2", "b 23 40"),
]

MAINKEYS = [
    ("usb1", 160),
    ("rekordbox", 159),
    ("usb2", 158),
    ("midi", 161),
]


def read_new_lines(start):
    try:
        with open(KEYLOG, "r", encoding="utf-8", errors="replace") as f:
            f.seek(start)
            return f.read().splitlines()
    except FileNotFoundError:
        return []


def summarize(lines):
    out = []
    seen = set()
    for ln in lines:
        if "src=paneldbg" not in ln:
            continue
        m = re.search(r"op=(\d+)\s+key=(0x[0-9a-fA-F]+)\s+ch=(\d+)", ln)
        if not m:
            continue
        triple = (int(m.group(1)), m.group(2).lower(), int(m.group(3)))
        if triple in seen:
            continue
        seen.add(triple)
        out.append(triple)
    return out


def run():
    subprocess.run([INJ, "base"], check=True)
    time.sleep(0.2)
    print("== paneldbg sweep ==")

    for name, spec in PULSES:
        start = os.path.getsize(KEYLOG) if os.path.exists(KEYLOG) else 0
        subprocess.run([INJ, "pulse", spec, "0.35"], check=True)
        time.sleep(0.2)
        rows = summarize(read_new_lines(start))
        print(f"{name:12s} pulse {spec:>8s} -> {rows if rows else 'no paneldbg events'}")

    for name, gpio in MAINKEYS:
        start = os.path.getsize(KEYLOG) if os.path.exists(KEYLOG) else 0
        subprocess.run([INJ, "mainkey", str(gpio), "0.35"], check=True)
        time.sleep(0.2)
        rows = summarize(read_new_lines(start))
        print(f"{name:12s} gpio {gpio:>3d} -> {rows if rows else 'no paneldbg events'}")


if __name__ == "__main__":
    run()
