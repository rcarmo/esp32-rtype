SHELL := /bin/bash
PIO ?= pio
ROM_ZIP ?= roms/rtype.zip
ROM_EXTRACTED ?= roms/extracted/rtype
S3_ENV := esp32-s3-8048s043c-rtype
TAB5_ENV := esp32-p4-tab5-rtype
PIO_ENV ?= $(S3_ENV)
SERIAL_PORT ?= /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
HOST_CXX ?= c++
HOST_BUILD_DIR ?= build/host
HOST_RTYPE_HARNESS ?= $(HOST_BUILD_DIR)/rtype_host_harness
HOST_RTYPE_PPM ?= artifacts/host-rtype-frame.ppm
HOST_RTYPE_PNG ?= artifacts/host-rtype-frame.png
HOST_RTYPE_INSTRUCTIONS ?= 20000000

.PHONY: help inspect-rom extract-rom pack-rom gfx-atlas host-harness host-run check build build-s3 build-tab5 flash monitor clean

help:
	@echo "R-Type display-first targets"
	@echo "  make inspect-rom              - validate attached rtype.zip layout"
	@echo "  make extract-rom              - extract ignored ROM files under roms/extracted/rtype"
	@echo "  make pack-rom                 - create ignored packed main/gfx ROM artifacts"
	@echo "  make gfx-atlas                - render ignored static graphics probe PNGs"
	@echo "  make host-harness             - build native host R-Type harness"
	@echo "  make host-run                 - run native harness and render a PPM/PNG frame"
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

host-harness:
	mkdir -p $(HOST_BUILD_DIR)
	$(HOST_CXX) -std=c++17 -O2 -Wall -Wextra -Wno-strict-aliasing host/rtype_host_harness.cpp -o $(HOST_RTYPE_HARNESS)

host-run: pack-rom host-harness
	mkdir -p artifacts
	$(HOST_RTYPE_HARNESS) --packed artifacts/packed-rtype --rom-dir $(ROM_EXTRACTED) --out $(HOST_RTYPE_PPM) --instructions $(HOST_RTYPE_INSTRUCTIONS)
	convert $(HOST_RTYPE_PPM) $(HOST_RTYPE_PNG)

check: inspect-rom pack-rom gfx-atlas host-harness

build build-s3:
	$(PIO) run -e $(S3_ENV)

build-tab5:
	$(PIO) run -e $(TAB5_ENV)

flash:
	$(PIO) run -e $(PIO_ENV) -t upload --upload-port $(SERIAL_PORT)

monitor:
	$(PIO) device monitor -e $(PIO_ENV) --port $(SERIAL_PORT)

clean:
	rm -rf .pio build sdkconfig sdkconfig.old roms/extracted artifacts/packed-rtype artifacts/gfx-atlas artifacts/host-rtype-frame.ppm artifacts/host-rtype-frame.png
