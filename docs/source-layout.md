# Source tree layout

This repository intentionally separates source, vendored components, local ROM input, and generated artifacts.

## Tracked source

```text
src/                  Firmware app, M72 core, renderer, display glue
include/              Board-profile headers and shared project includes
lib/lcd_cyd/          Local CYD/S3 display support component
host/                 Native host reference harness
components_tab5/      Local Tab5 BSP component snapshot
tools/                Host-side helper scripts
managed_components/   Whitelisted ESP-IDF managed components used by CYD builds
docs/                 Project, board, ROM, and bring-up documentation
roms/.gitkeep         Placeholder only
roms/README.md        Local ROM drop instructions only
```

## Ignored/generated content

Do not commit these:

```text
.pio/
build/
sdkconfig
sdkconfig.old
sdkconfig.*-rtype
dependencies.lock
artifacts/
roms/* except roms/.gitkeep and roms/README.md
roms/extracted/
tools/__pycache__/
```

`make guard-roms` fails if ROMs or generated build/artifact files are tracked.

## ROM workflow

Local ROM input goes in:

```text
roms/rtype.zip
```

That archive is ignored. Use Makefile targets to inspect/extract/pack it:

```bash
make inspect-rom
make extract-rom
make pack-rom
```

Packed output is generated under `artifacts/packed-rtype/` and ignored.

## Firmware targets

Supported build-all set:

```bash
make build-all
```

which currently builds:

```text
esp32-s3-8048s043c-rtype
esp32-cyd-rtype
```

The Tab5 profile is retained as a secondary board profile and should be built explicitly when working on it:

```bash
make build-tab5
```

## Renderer source boundaries

- `src/rtype_m72_core.*`: M72 memory/port/video state bridge.
- `src/rtype_i86_cpu.*`: incremental 8086/V30-family interpreter used by the R-Type runtime path.
- `src/rtype_m72_video.*`: M72 tile/sprite renderer and board-specific sampling helpers.
- `src/rtype_display_s3.c`: S3 RGB-panel output, direct handoff, scaling to `720x480+40,0`.
- `src/rtype_display_cyd.c`: CYD SPI display worker/strip path.
- `src/rtype_display_tab5.c`: Tab5 display profile glue.

Do not add static/prebaked gameplay frames to `src/`. Boot/error patterns are allowed only for no-ROM/no-core diagnostics.

## Validation shortcuts

```bash
make bootstrap
make guard-roms
make build-s3
make build-cyd
make smoke-s3 SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

When a camera is attached:

```bash
make compare-s3-host SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```
