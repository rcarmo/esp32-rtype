# esp32-rtype Agent Guide

Project goal: live, display-first Irem R-Type / M72 bring-up on ESP32-class boards, with the ESP32-S3 8048S043C replacement board as the primary target.

## Non-negotiable constraints

- Do **not** commit ROM contents or generated ROM artifacts.
  - `roms/*.zip`, `roms/extracted/`, and `artifacts/` are ignored/generated.
- Do **not** replace live emulator output with static/prebaked frames.
- Do **not** use coarse/fake renderers, synthetic palette fallbacks, or direct sprite-overlay shortcuts as final fixes.
- Keep firmware and host-renderer semantics aligned with MAME where possible.
- Preserve S3 proportional output: source `384x256` into panel view `720x480+40,0` with black side bars.
- Preserve complete-frame rendering on S3: the renderer must snapshot only after the game update queue has had a bounded chance to drain.
  - This is required for complete background artwork.
  - Serial `S3 PERF` lines must keep `qdrain=<nonzero>/0` in active playfield smoke tests.

## Primary environment

- Host: Orange Pi / Debian.
- Primary firmware environment: `esp32-s3-8048s043c-rtype`.
- Secondary build targets:
  - `esp32-cyd-rtype`
  - `esp32-p4-tab5-rtype`
- S3 serial port convention:
  - `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`

## Standard setup/build commands

Start with:

```bash
make help
make bootstrap
```

The Makefile uses `PYTHON` for helper scripts. It defaults to `/workspace/.venvs/pio/bin/python` when present, otherwise `python3`; override it when needed:

```bash
make smoke-s3 PYTHON=/path/to/python
```

Build host/reference tools and supported firmware targets:

```bash
make host-harness
make build-s3
make build-cyd
```

Build the current green firmware set:

```bash
make build-all
```

Tab5 is a secondary profile with local BSP integration caveats; build it explicitly when working on that board:

```bash
make build-tab5
```

Host-side ROM/render checks:

```bash
make check
make host-run
```

Primary S3 deploy/validation:

```bash
make deploy-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
make smoke-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

Exact host-vs-S3 workflow when a usable camera is attached:

```bash
make compare-s3-host SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

The camera capture tools auto-probe `/dev/video*`. If auto-detection fails, pass an explicit camera device to the Python tool.

## Required validation before committing renderer/S3 changes

At minimum:

```bash
make ci
make smoke-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

If camera capture is available:

```bash
make compare-s3-host SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

For MAME renderer changes, also run:

```bash
make host-harness
```

## S3 fidelity/performance rules

- Keep `render_irq` changes behind smoke/compare evidence.
- Do not reduce fidelity to raise IRQ rate.
- Current S3 rendering relies on:
  - MAME tile priority masks and draw order.
  - MAME sprite-list traversal.
  - palette RAM A9 mirroring and disconnected high read bits.
  - queue-drain-before-render for complete background frames.
  - direct display handoff with triple source framebuffers.
  - decoded tile/sprite row caches and cached row masks.
- If backgrounds disappear, first inspect:
  - `S3 PERF ... qdrain=ok/miss`
  - VRAM background attribute lanes
  - whether the game queue was sampled mid-update.

## Documentation expectations

When changing board behavior, update the relevant file under `docs/boards/` and, if applicable, `docs/bringup-plan.md`.

Board docs live under:

```text
docs/boards/
```

Keep generated artifacts out of commits unless explicitly requested.
