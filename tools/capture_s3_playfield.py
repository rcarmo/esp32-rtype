#!/usr/bin/env python3
"""Capture ESP32-S3 R-Type frames at matched live playfield states.

Watches serial PERF logs for a target root state (default 0x0aa6) and captures
one or more camera frames when the emulated frame counter reaches a threshold.
This is intended for reproducible host-vs-device fidelity checks.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys
import time
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover - operator-facing failure
    raise SystemExit("pyserial is required; use /workspace/.venvs/pio/bin/python") from exc


def parse_int(value: str) -> int:
    return int(value, 0)


def video_devices() -> Iterable[pathlib.Path]:
    return sorted(pathlib.Path('/dev').glob('video[0-9]*'))


def ffmpeg_capture(ffmpeg: str, camera: str, path: pathlib.Path) -> int:
    cmd = [
        ffmpeg,
        '-hide_banner',
        '-y',
        '-f',
        'v4l2',
        '-input_format',
        'mjpeg',
        '-video_size',
        '1280x720',
        '-i',
        camera,
        '-frames:v',
        '1',
        '-update',
        '1',
        str(path),
    ]
    return subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode


def resolve_camera(camera: str, ffmpeg: str, out_dir: pathlib.Path) -> str | None:
    if camera and camera != 'auto':
        return camera
    probe_dir = out_dir / '.camera-probe'
    probe_dir.mkdir(parents=True, exist_ok=True)
    for dev in video_devices():
        probe = probe_dir / f'{dev.name}.jpg'
        if ffmpeg_capture(ffmpeg, str(dev), probe) == 0:
            return str(dev)
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--port", default="/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--camera", default="auto", help="V4L2 camera device, or 'auto' to probe /dev/video*")
    ap.add_argument("--out-dir", default="artifacts/camera")
    ap.add_argument("--root", type=parse_int, default=0x0AA6, help="target root state, e.g. 0x0aa6")
    ap.add_argument("--min-frame", type=parse_int, default=0x0700, help="minimum frame counter to capture")
    ap.add_argument("--max-shots", type=int, default=4)
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--min-gap", type=float, default=7.0, help="minimum seconds between captures")
    ap.add_argument("--no-reset", action="store_true", help="do not reset the board before watching serial")
    ap.add_argument("--ffmpeg", default="ffmpeg")
    args = ap.parse_args()

    out_dir = pathlib.Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    camera = resolve_camera(args.camera, args.ffmpeg, out_dir)
    if camera is None:
        print("ERROR no usable V4L2 camera found; pass --camera /dev/videoN or connect a capture device", file=sys.stderr, flush=True)
        return 1
    print(f"Using camera {camera}", flush=True)

    ser = serial.Serial(args.port, args.baud, timeout=0.2)
    if not args.no_reset:
        ser.dtr = False
        ser.rts = True
        time.sleep(0.1)
        ser.rts = False

    perf_re = re.compile(
        rb"frame=0x([0-9a-fA-F]+).*root=0x([0-9a-fA-F]+).*scroll=\((\d+),0\)/\((\d+),0\)"
    )
    shots = 0
    last_shot = 0.0
    start = time.time()
    try:
        while time.time() - start < args.seconds and shots < args.max_shots:
            line = ser.readline()
            if not line:
                continue
            if b"S3 PERF" in line:
                print(line.decode("latin1", "ignore").rstrip(), flush=True)
            match = perf_re.search(line)
            if not match:
                continue
            frame = int(match.group(1), 16)
            root = int(match.group(2), 16)
            scroll0 = int(match.group(3))
            scroll1 = int(match.group(4))
            now = time.time()
            if root != args.root or frame < args.min_frame or now - last_shot < args.min_gap:
                continue
            path = out_dir / f"s3-root{root:04x}-{shots}-f{frame:04x}-s{scroll0}-{scroll1}.jpg"
            rc = ffmpeg_capture(args.ffmpeg, camera, path)
            if rc == 0:
                print(f"CAPTURE {path} frame=0x{frame:04x} root=0x{root:04x} scroll=({scroll0},0)/({scroll1},0)", flush=True)
                shots += 1
                last_shot = now
            else:
                print(f"WARN ffmpeg failed rc={rc} for {path}", file=sys.stderr, flush=True)
    finally:
        ser.close()

    return 0 if shots else 1


if __name__ == "__main__":
    raise SystemExit(main())
