SHELL := /bin/bash
PIO ?= pio
ROM_ZIP ?= roms/rtype.zip
ROM_EXTRACTED ?= roms/extracted/rtype
S3_ENV := esp32-s3-8048s043c-rtype
TAB5_ENV := esp32-p4-tab5-rtype
PIO_ENV ?= $(S3_ENV)
SERIAL_PORT ?= /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0

.PHONY: help inspect-rom extract-rom check build build-s3 build-tab5 flash monitor clean

help:
	@echo "R-Type display-first targets"
	@echo "  make inspect-rom              - validate attached rtype.zip layout"
	@echo "  make extract-rom              - extract ignored ROM files under roms/extracted/rtype"
	@echo "  make check                    - run host-side ROM checks"
	@echo "  make build / build-s3         - build ESP32-S3 480x800 firmware (primary target)"
	@echo "  make build-tab5               - build ESP32-P4 Tab5 firmware (secondary target)"
	@echo "  make flash                    - flash selected PIO_ENV=$(PIO_ENV)"
	@echo "  make monitor                  - serial monitor"

inspect-rom:
	bun tools/inspect_rtype.ts $(ROM_ZIP) $(ROM_EXTRACTED)

extract-rom: inspect-rom

check: inspect-rom

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
