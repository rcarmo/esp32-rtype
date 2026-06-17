#!/usr/bin/env python3
"""Capture an ESP32-S3 playfield frame and render a matched host comparison.

This wraps tools/capture_s3_playfield.py plus the host harness. It parses the
S3 capture line, renders the host at the same frame/root state, and creates a
side-by-side image for fidelity review.
"""

from __future__ import annotations

import argparse
import pathlib
import re
import subprocess
import sys


def parse_int(value: str) -> int:
    return int(value, 0)


def run(cmd: list[str], *, cwd: pathlib.Path, capture: bool = False) -> subprocess.CompletedProcess[str]:
    print("+", " ".join(cmd), flush=True)
    return subprocess.run(
        cmd,
        cwd=str(cwd),
        check=False,
        text=True,
        stdout=subprocess.PIPE if capture else None,
        stderr=subprocess.STDOUT if capture else None,
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--project", default=".", help="project directory")
    ap.add_argument("--port", default="/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0")
    ap.add_argument("--camera", default="auto")
    ap.add_argument("--root", type=parse_int, default=0x0AA6)
    ap.add_argument("--min-frame", type=parse_int, default=0x0700)
    ap.add_argument("--seconds", type=float, default=120.0)
    ap.add_argument("--out-dir", default="artifacts/compare")
    ap.add_argument("--host-instructions", type=parse_int, default=400_000_000)
    ap.add_argument("--python", default=sys.executable)
    ap.add_argument("--host-cxx", default="c++")
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    project = pathlib.Path(args.project).resolve()
    out_dir = (project / args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    capture_cmd = [
        args.python,
        "tools/capture_s3_playfield.py",
        "--port",
        args.port,
        "--camera",
        args.camera,
        "--out-dir",
        str(out_dir),
        "--root",
        f"0x{args.root:04x}",
        "--min-frame",
        f"0x{args.min_frame:04x}",
        "--max-shots",
        "1",
        "--seconds",
        str(args.seconds),
    ]
    if args.no_reset:
        capture_cmd.append("--no-reset")
    cap = run(capture_cmd, cwd=project, capture=True)
    if cap.stdout:
        print(cap.stdout, end="")
    if cap.returncode != 0:
        print(f"capture failed rc={cap.returncode}", file=sys.stderr)
        return cap.returncode

    cap_re = re.compile(r"CAPTURE\s+(\S+)\s+frame=0x([0-9a-fA-F]+)\s+root=0x([0-9a-fA-F]+)\s+scroll=\((\d+),0\)/\((\d+),0\)")
    match = None
    for line in cap.stdout.splitlines() if cap.stdout else []:
        m = cap_re.search(line)
        if m:
            match = m
    if match is None:
        print("could not parse CAPTURE line", file=sys.stderr)
        return 2

    s3_image = pathlib.Path(match.group(1))
    frame = int(match.group(2), 16)
    root = int(match.group(3), 16)
    scroll0 = int(match.group(4))
    scroll1 = int(match.group(5))

    host_build = run(["make", "host-harness", f"HOST_CXX={args.host_cxx}"], cwd=project)
    if host_build.returncode != 0:
        return host_build.returncode

    host_ppm = out_dir / f"host-f{frame:04x}-r{root:04x}.ppm"
    host_png = out_dir / f"host-f{frame:04x}-r{root:04x}.png"
    host_cmd = [
        "build/host/rtype_host_harness",
        "--packed",
        "artifacts/packed-rtype",
        "--rom-dir",
        "roms/extracted/rtype",
        "--target-frame",
        f"0x{frame:04x}",
        "--target-root",
        f"0x{root:04x}",
        "--instructions",
        str(args.host_instructions),
        "--out",
        str(host_ppm),
    ]
    host = run(host_cmd, cwd=project, capture=True)
    if host.stdout:
        print(host.stdout, end="")
    if host.returncode != 0:
        return host.returncode

    conv = run(["convert", str(host_ppm), str(host_png)], cwd=project)
    if conv.returncode != 0:
        return conv.returncode

    host_768 = out_dir / f"host-f{frame:04x}-768.png"
    s3_768 = out_dir / f"s3-f{frame:04x}-768.jpg"
    compare = out_dir / f"host-vs-s3-f{frame:04x}-r{root:04x}.jpg"
    title = f"Exact target: frame=0x{frame:04x} root=0x{root:04x} scroll={scroll0}/{scroll1} | host vs S3"
    cmds = [
        ["convert", str(host_png), "-resize", "768x512", "-gravity", "center", "-background", "black", "-extent", "768x512", str(host_768)],
        ["convert", str(s3_image), "-resize", "768x512", "-gravity", "center", "-background", "black", "-extent", "768x512", str(s3_768)],
        ["montage", str(host_768), str(s3_768), "-tile", "2x1", "-geometry", "+12+12", "-background", "#111111", "-title", title, str(compare)],
    ]
    for cmd in cmds:
        r = run(cmd, cwd=project)
        if r.returncode != 0:
            return r.returncode

    print(f"COMPARE {compare}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
