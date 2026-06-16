# rtype-tab5

Display-first arcade emulator experiment for **Irem R-Type / M72**. The primary test target is now the **ESP32-S3 480×800 ESP32-8048S043C** board; **M5Stack Tab5 / ESP32-P4** is kept as a secondary profile.

Current primary target:

- board/env: `esp32-s3-8048s043c-rtype`
- firmware build: `make build-s3`
- firmware flash: `make flash-s3`
- ROM/data flash: `make flash-s3-data`
- full deploy check: `make deploy-s3`
- smoke test: `make smoke-s3`
- exact host/S3 comparison: `make compare-s3-host`

Current S3 status:

- live V30/M72 R-Type path runs on the ESP32-S3 replacement board
- main CPU ROM fetches are flash-mapped from a dedicated `maincpu` partition
- graphics ROMs load from `/spiflash/rtype`
- M72 work/video/sprite/palette/sound RAM is mapped in sparse PSRAM-backed regions
- display is proportional on the 800×480 RGB panel: `720×480+40,0`
- host and firmware renderers use MAME-style M72 tile priority masks and palette RAM semantics
- active playfield backgrounds are visible and can be verified with the exact comparison workflow

Explicitly still out of scope:

- audio output
- real physical input
- menus/save states
- general MAME compatibility

This is **not Neo Geo**. The attached ROM set is MAME-style Irem R-Type, not an AES/MVS cartridge.

See also:

- [`docs/bringup-plan.md`](docs/bringup-plan.md) — current S3 architecture, flashing, smoke/comparison workflow
- [`docs/rom-layout.md`](docs/rom-layout.md) — ROM packing/layout notes
