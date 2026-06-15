SHELL := /bin/bash
PIO ?= pio
ROM_ZIP ?= roms/rtype.zip
ROM_EXTRACTED ?= roms/extracted/rtype
S3_ENV := esp32-s3-8048s043c-rtype
TAB5_ENV := esp32-p4-tab5-rtype
PIO_ENV ?= $(S3_ENV)
SERIAL_PORT ?= /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0

.PHONY: help inspect-rom extract-rom pack-rom gfx-atlas check build build-s3 build-tab5 flash monitor clean

help:
	@echo "R-Type display-first targets"
	@echo "  make inspect-rom              - validate attached rtype.zip layout"
	@echo "  make extract-rom              - extract ignored ROM files under roms/extracted/rtype"
	@echo "  make pack-rom                 - create ignored packed main/gfx ROM artifacts"
	@echo "  make gfx-atlas                - render ignored static graphics probe PNGs"
	@echo "  make check                    - run host-side ROM/packer/gfx checks"
	@echo "  make build / build-s3         - build ESP32-S3 480x800 firmware (primary target)"
	@echo "  make build-tab5               - build ESP32-P4 Tab5 firmware (secondary target)"
	@echo "  make flash                    - flash selected PIO_ENV=$(PIO_ENV)"
	@echo "  make monitor                  - serial monitor"

inspect-rom:
	bun tools/inspect_rtype.ts $(ROM_ZIP) $(ROM_EXTRACTED)

extract-rom: inspect-rom

pack-rom: extract-rom
	bun tools/pack_rtype.ts $(ROM_EXTRACTED) artifacts/packed-rtype >/tmp/rtype-pack-report.json

gfx-atlas: extract-rom
	bun tools/render_gfx_atlas.ts $(ROM_EXTRACTED) artifacts/gfx-atlas 1024
	convert artifacts/gfx-atlas/cpu-00102030-8x8.png artifacts/gfx-atlas/cpu-01112131-8x8.png artifacts/gfx-atlas/cpu-a0123-8x8.png artifacts/gfx-atlas/cpu-b0123-8x8.png -append artifacts/gfx-atlas/rtype-gfx-probe-combined.png

check: inspect-rom pack-rom gfx-atlas

build build-s3:
	$(PIO) run -e $(S3_ENV)

build-tab5:
	$(PIO) run -e $(TAB5_ENV)

flash:
	$(PIO) run -e $(PIO_ENV) -t upload --upload-port $(SERIAL_PORT)

monitor:
	$(PIO) device monitor -e $(PIO_ENV) --port $(SERIAL_PORT)

clean:
	rm -rf .pio build sdkconfig sdkconfig.old roms/extracted
