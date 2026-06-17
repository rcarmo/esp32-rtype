#!/usr/bin/env python3
"""ESP32-S3 R-Type serial smoke test.

Resets the S3, watches serial logs, and verifies the live R-Type port reaches
its required bring-up milestones without panic/halt.
"""

from __future__ import annotations

import argparse
import re
import sys
import time

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit("pyserial is required; use /workspace/.venvs/pio/bin/python") from exc


FAIL_PATTERNS = [
    re.compile(r"Guru Meditation", re.I),
    re.compile(r"panic", re.I),
    re.compile(r"abort\(\)", re.I),
    re.compile(r"S3 CPU halted", re.I),
    re.compile(r"alloc frame buffers failed", re.I),
    re.compile(r"external graphics ROMs not loaded", re.I),
    re.compile(r"maincpu partition unavailable", re.I),
]

CHECKS = {
    "maincpu_mapped": re.compile(r"mapped main CPU ROM partition 'maincpu'"),
    "fat_mounted": re.compile(r"mounted FAT storage partition at /spiflash"),
    "graphics_loaded": re.compile(r"M72 graphics ROM regions loaded from /spiflash/rtype"),
    "aspect_display": re.compile(r"aspect-blit view=720x480\+40,0"),
    "perf": re.compile(r"S3 PERF"),
    "playfield": re.compile(r"S3 PERF.*root=0x0aa6"),
    "qdrain": re.compile(r"S3 PERF.*qdrain=[1-9]\d*/0.*root=0x0aa6"),
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--seconds", type=float, default=90.0)
    ap.add_argument("--no-reset", action="store_true")
    ap.add_argument("--no-playfield-required", action="store_true", help="do not require root=0x0aa6 before timeout")
    args = ap.parse_args()

    seen = {k: False for k in CHECKS}
    failures: list[str] = []

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    if not args.no_reset:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False

    start = time.time()
    try:
        while time.time() - start < args.seconds:
            line_b = ser.readline()
            if not line_b:
                continue
            line = line_b.decode("latin1", "ignore").rstrip()
            if line:
                print(line, flush=True)
            for pat in FAIL_PATTERNS:
                if pat.search(line):
                    failures.append(line)
            for name, pat in CHECKS.items():
                if pat.search(line):
                    seen[name] = True
            required = ["maincpu_mapped", "fat_mounted", "graphics_loaded", "aspect_display", "perf"]
            if not args.no_playfield_required:
                required.extend(["playfield", "qdrain"]) 
            if all(seen[k] for k in required) and not failures:
                break
    finally:
        ser.close()

    required = ["maincpu_mapped", "fat_mounted", "graphics_loaded", "aspect_display", "perf"]
    if not args.no_playfield_required:
        required.extend(["playfield", "qdrain"]) 
    missing = [k for k in required if not seen[k]]

    print("S3_SMOKE_SUMMARY", " ".join(f"{k}={int(v)}" for k, v in seen.items()), flush=True)
    if failures:
        print("S3_SMOKE_FAIL failure_patterns:", file=sys.stderr)
        for line in failures[:20]:
            print(line, file=sys.stderr)
    if missing:
        print("S3_SMOKE_FAIL missing=" + ",".join(missing), file=sys.stderr)
    if failures or missing:
        return 1
    print("S3_SMOKE_PASS", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
