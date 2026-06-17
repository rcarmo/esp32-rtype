# Documentation index

- [Bring-up plan](bringup-plan.md) — current architecture, S3 workflow, smoke/comparison gates, and caveats.
- [Source layout](source-layout.md) — tracked source vs generated/local files.
- [ROM layout](rom-layout.md) — local R-Type ROM set structure and packing notes.
- [CYD blitter plan](cyd-blitter-plan.md) — CYD display and strip/column rendering notes.
- [Board targets](boards/index.md) — board-specific characteristics, limitations, and rendering strategies.

## Current primary workflow

```bash
make bootstrap
make build-s3
make build-cyd
make smoke-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

`make smoke-s3` requires a queue-drained active playfield frame via `qdrain=<nonzero>/0`, which is the serial-only guard for complete background rendering.

When a camera is attached:

```bash
make compare-s3-host SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

The comparison scripts auto-detect `/dev/video*`; pass `--camera /dev/videoN` to the Python tools when needed.
