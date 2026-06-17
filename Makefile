SHELL := /bin/bash
PIO ?= pio
ROM_ZIP ?= roms/rtype.zip
ROM_EXTRACTED ?= roms/extracted/rtype
S3_ENV := esp32-s3-8048s043c-rtype
TAB5_ENV := esp32-p4-tab5-rtype
CYD_ENV := esp32-cyd-rtype
PIO_ENV ?= $(S3_ENV)
SERIAL_PORT ?= /dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
HOST_CXX ?= c++
HOST_BUILD_DIR ?= build/host
HOST_RTYPE_HARNESS ?= $(HOST_BUILD_DIR)/rtype_host_harness
HOST_RTYPE_PPM ?= artifacts/host-rtype-frame.ppm
HOST_RTYPE_PNG ?= artifacts/host-rtype-frame.png
HOST_RTYPE_INSTRUCTIONS ?= 300000000
PYTHON ?= $(shell if [ -x /workspace/.venvs/pio/bin/python ]; then echo /workspace/.venvs/pio/bin/python; else command -v python3; fi)
S3_MAINCPU_OFFSET := 0x410000
S3_STORAGE_OFFSET := 0x510000
S3_STORAGE_SIZE := 9437184
S3_FATFS_ROOT := artifacts/s3-fatfs-root
S3_FATFS_IMAGE := artifacts/s3-rtype-fatfs-wl-9m.bin
IDF_FATFS_GEN ?= /home/agent/.platformio/packages/framework-espidf/components/fatfs/wl_fatfsgen.py
ESPTOOL ?= $(PYTHON) -m esptool

.PHONY: help bootstrap check-tool guard-roms format-check inspect-rom extract-rom pack-rom gfx-atlas host-harness host-run check build build-all build-s3 build-cyd build-tab5 flash flash-s3 deploy-s3 s3-storage-root s3-fatfs-image flash-s3-data monitor smoke-s3 capture-s3-playfield compare-s3-host clean distclean

help:
	@echo "R-Type display-first targets"
	@echo "  make bootstrap                - check local tools needed for host/firmware workflows"
	@echo "  make inspect-rom              - validate attached rtype.zip layout"
	@echo "  make extract-rom              - extract ignored ROM files under roms/extracted/rtype"
	@echo "  make pack-rom                 - create ignored packed main/gfx ROM artifacts"
	@echo "  make gfx-atlas                - render ignored static graphics probe PNGs"
	@echo "  make host-harness             - build native host R-Type harness"
	@echo "  make host-run                 - run native harness and render a PPM/PNG frame"
	@echo "  make check                    - run host-side ROM/packer/gfx checks"
	@echo "  make guard-roms               - fail if ROM/generated files are tracked"
	@echo "  make format-check             - check C/C++ formatting when clang-format is installed"
	@echo "  make build / build-s3         - build ESP32-S3 480x800 firmware (primary target)"
	@echo "  make build-cyd                - build ESP32 CYD 240x320 SPI firmware (small target)"
	@echo "  make build-tab5               - build ESP32-P4 Tab5 firmware (secondary target; currently BSP-integration limited)"
	@echo "  make build-all                - build currently supported firmware targets (S3 + CYD)"
	@echo "  make flash                    - flash selected PIO_ENV=$(PIO_ENV)"
	@echo "  make flash-s3                 - flash ESP32-S3 firmware"
	@echo "  make deploy-s3                - build, flash firmware/data, then smoke-test S3"
	@echo "  make s3-fatfs-image           - build ignored 9MB S3 FAT ROM storage image"
	@echo "  make flash-s3-data            - flash S3 maincpu + FAT ROM data partitions"
	@echo "  make monitor                  - serial monitor"
	@echo "  make smoke-s3                 - reset S3 and verify live ROM/display/playfield logs"
	@echo "  make capture-s3-playfield     - capture camera frames when S3 reaches active playfield state"
	@echo "  make compare-s3-host          - capture S3 and render exact host side-by-side comparison"
	@echo "  make clean                    - remove generated build/pack/cache artifacts"
	@echo "  make distclean                - clean plus remove all ignored artifacts/ outputs"

bootstrap:
	@echo "Checking required tools..."
	@$(MAKE) --no-print-directory check-tool TOOL=bun
	@$(MAKE) --no-print-directory check-tool TOOL=$(PIO)
	@$(MAKE) --no-print-directory check-tool TOOL=$(HOST_CXX)
	@$(MAKE) --no-print-directory check-tool TOOL=convert
	@$(MAKE) --no-print-directory check-tool TOOL=ffmpeg OPTIONAL=1
	@$(MAKE) --no-print-directory check-tool TOOL=v4l2-ctl OPTIONAL=1
	@test -n "$(PYTHON)" || (echo "missing python3 or /workspace/.venvs/pio/bin/python" >&2; exit 1)
	@$(PYTHON) -c "import importlib.util, sys; missing=[m for m in ('serial',) if importlib.util.find_spec(m) is None]; print('Python modules OK' if not missing else 'missing Python modules for $(PYTHON): '+', '.join(missing), file=sys.stderr); raise SystemExit(1 if missing else 0)"
	@echo "Bootstrap checks passed."

check-tool:
	@if ! command -v $(TOOL) >/dev/null 2>&1; then \
		if [ "$(OPTIONAL)" = "1" ]; then \
			echo "optional tool not found: $(TOOL)"; \
		else \
			echo "missing required tool: $(TOOL)" >&2; exit 1; \
		fi; \
	else \
		echo "found $(TOOL): $$(command -v $(TOOL))"; \
	fi

guard-roms:
	@tracked="$$(git ls-files 'roms/*' 'artifacts/*' 'sdkconfig.*-rtype' 'dependencies.lock' 2>/dev/null | grep -Ev '^(roms/\.gitkeep|roms/README\.md)$$' || true)"; \
	if [ -n "$$tracked" ]; then \
		echo "Refusing to proceed: generated/ROM/build files are tracked:" >&2; \
		echo "$$tracked" >&2; \
		exit 1; \
	fi
	@echo "Generated/ROM tracking guard passed."

format-check:
	@if ! command -v clang-format >/dev/null 2>&1; then \
		echo "clang-format not installed; skipping format-check"; \
	else \
		files="$$(git ls-files '*.c' '*.h' '*.cpp' '*.hpp')"; \
		if [ -n "$$files" ]; then \
			clang-format --dry-run --Werror $$files; \
		fi; \
	fi

inspect-rom:
	bun tools/inspect_rtype.ts $(ROM_ZIP) $(ROM_EXTRACTED)

extract-rom: inspect-rom

pack-rom: extract-rom
	bun tools/pack_rtype.ts $(ROM_EXTRACTED) artifacts/packed-rtype >/tmp/rtype-pack-report.json
	bun tools/pack_cyd_storage.ts $(ROM_EXTRACTED) artifacts/packed-rtype/cyd-storage.bin >/tmp/rtype-cyd-pack-report.json

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

check: guard-roms inspect-rom pack-rom gfx-atlas host-harness

build build-s3:
	$(PIO) run -e $(S3_ENV)

build-all: build-s3 build-cyd

build-cyd:
	$(PIO) run -e $(CYD_ENV)

build-tab5:
	$(PIO) run -e $(TAB5_ENV)

flash:
	$(PIO) run -e $(PIO_ENV) -t upload --upload-port $(SERIAL_PORT)

flash-s3:
	$(MAKE) flash PIO_ENV=$(S3_ENV) SERIAL_PORT=$(SERIAL_PORT)

deploy-s3: build-s3 flash-s3 flash-s3-data smoke-s3

s3-storage-root: extract-rom
	rm -rf $(S3_FATFS_ROOT)
	mkdir -p $(S3_FATFS_ROOT)/rtype
	cp $(ROM_EXTRACTED)/*.bin $(S3_FATFS_ROOT)/rtype/

s3-fatfs-image: s3-storage-root
	$(PYTHON) $(IDF_FATFS_GEN) --partition_size $(S3_STORAGE_SIZE) --output_file $(S3_FATFS_IMAGE) $(S3_FATFS_ROOT)

flash-s3-data: pack-rom s3-fatfs-image
	$(ESPTOOL) --chip esp32s3 --port $(SERIAL_PORT) --baud 460800 write-flash \
		$(S3_MAINCPU_OFFSET) artifacts/packed-rtype/maincpu-map.bin \
		$(S3_STORAGE_OFFSET) $(S3_FATFS_IMAGE)

monitor:
	$(PIO) device monitor -e $(PIO_ENV) --port $(SERIAL_PORT)

smoke-s3:
	$(PYTHON) tools/smoke_s3.py --port $(SERIAL_PORT)

capture-s3-playfield:
	$(PYTHON) tools/capture_s3_playfield.py --port $(SERIAL_PORT)

compare-s3-host:
	$(PYTHON) tools/compare_s3_host.py --port $(SERIAL_PORT)

clean:
	rm -rf .pio build sdkconfig sdkconfig.old sdkconfig.*-rtype dependencies.lock roms/extracted \
		tools/__pycache__ artifacts/packed-rtype artifacts/gfx-atlas \
		artifacts/host-rtype-frame.ppm artifacts/host-rtype-frame.png \
		$(S3_FATFS_ROOT) $(S3_FATFS_IMAGE)

distclean: clean
	rm -rf artifacts
