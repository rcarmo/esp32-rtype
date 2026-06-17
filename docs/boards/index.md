# Board targets

This project keeps multiple ESP32-class board profiles in one tree. The primary target is the ESP32-S3 8048S043C replacement board; CYD and Tab5 remain build/portability targets.

## Board pages

- [ESP32-S3 8048S043C](esp32-s3-8048s043c.md) — primary live R-Type target.
- [ESP32 CYD 2432S028](esp32-cyd-2432s028.md) — small SPI-display compatibility target.
- [M5Stack Tab5 ESP32-P4](m5stack-tab5-esp32p4.md) — secondary ESP32-P4 profile.

## Common validation

Current green build set:

```bash
make bootstrap
make build-all
```

Equivalent explicit target builds:

```bash
make build-s3
make build-cyd
```

Tab5 is retained as a secondary profile with local BSP integration caveats. Build it explicitly only when working on that board:

```bash
make build-tab5
```

Primary hardware validation:

```bash
make deploy-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
make smoke-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

When a V4L2 camera is attached:

```bash
make compare-s3-host SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

The comparison tools auto-detect `/dev/video*`; pass `--camera /dev/videoN` to the Python scripts if needed.

## Shared rendering principles

- Do not use prebaked/static frames as final output.
- Use live emulated M72 VRAM, sprite RAM, palette RAM, and scroll registers.
- Keep firmware and host renderer behavior aligned.
- Prefer MAME behavior when deciding tile/sprite priority, palette, and visible-area semantics.
- Preserve S3 complete-frame queue drain; it is required for background artwork.
