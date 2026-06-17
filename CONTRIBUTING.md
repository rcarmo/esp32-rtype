# Contributing

This repository is a display-first ESP32 port of Irem R-Type / M72. The primary hardware target is `esp32-s3-8048s043c-rtype`.

## Required local checks

Before opening or pushing changes that affect source, build metadata, or renderer behavior, run at least:

```bash
make bootstrap
make guard-roms
make build-s3
make build-cyd
```

For S3 firmware changes, also run:

```bash
make smoke-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

When a usable V4L2 camera is attached, run:

```bash
make compare-s3-host SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

## ROM and artifact policy

Do not commit ROMs, extracted ROM files, packed ROM artifacts, screenshots, camera captures, or PlatformIO build output.

Allowed ROM-directory files are only:

```text
roms/.gitkeep
roms/README.md
```

The guard target checks this:

```bash
make guard-roms
```

## Renderer/fidelity rules

- Keep S3 complete-frame queue drain intact; it is required for background artwork.
- Preserve `S3 PERF ... qdrain=<nonzero>/0` for active playfield smoke tests.
- Keep host and firmware renderer semantics aligned.
- Use MAME behavior as the reference for tile/sprite priority, palette, visible area, and sprite traversal.
- Do not replace live emulator output with static/prebaked frames or synthetic palette fallbacks.

See [`AGENTS.md`](AGENTS.md) for more detailed local agent/developer rules.
