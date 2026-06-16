# R-Type Tab5 display-first bring-up plan

## Scope

The attached ROM set is Irem R-Type, not Neo Geo. First run targets only a visible display path on ESP32-S3 480×800 first, with M5Stack Tab5 / ESP32-P4 as secondary.

Explicitly deferred:

- audio CPU and sound chips
- physical input
- menus
- save states
- general MAME compatibility

## Milestones

1. **M0 display path**: initialize the ESP32-S3 RGB panel and present a centered 384x256 arcade framebuffer inside the 800x480 panel.
2. **M1 ROM ingestion**: validate/extract `rtype.zip`, classify program/graphics ROMs, and produce packed ROM regions. **Done for local set:** `maincpu-v30.bin` and raw graphics planes are generated under ignored `artifacts/packed-rtype/`.
3. **M2 static graphics proof**: decode enough graphics planes to draw recognizable R-Type tiles/sprites without CPU emulation. **Initial host proof done:** `make gfx-atlas` renders recognizable planar graphics probes.
4. **M3 CPU execution**: wire a V30/8086-family core for the main program; stub inputs and sound.
5. **M4 video registers**: render live tile/sprite state from emulated video RAM.

## Reused from Cydintosh

- PlatformIO ESP-IDF configuration
- ESP32-8048S043C RGB panel timings/pins
- optional M5Stack Tab5 BSP component and ST7123/ILI9881C panel detection approach
- ESP32-P4 rev-v1.x sdkconfig safeguards
- PSRAM-first framebuffer allocation pattern
- strip-based RGB565 panel flushing pattern

## Notes

- ROM layout notes: [rom-layout.md](rom-layout.md)

## Current host harness frontier

- Native harness: `host/rtype_host_harness.cpp`.
- `make host-run` loads the MAME-faithful 1MB V30 map plus sprite/tile ROM regions, executes the R-Type reset path and frame callbacks, and writes `artifacts/host-rtype-frame.png`.
- Current verified run: 100M instructions cleanly, 833 frame callbacks, live sprite RAM (`spr_nz=364`) and foreground VRAM (`vram0_nz≈226`) with no CPU halt. The key fix was restoring the pre-frame stack pointer when the R-Type frame vector tail-returns to the idle loop without the normal IRET epilogue, avoiding collision with the game temp buffer at `0x2fd0`.
- Host benchmark observed roughly 90M interpreted instructions/sec on the Orange Pi in one 100M run; treat values as relative because host load varies.
- Rendering uses real decoded M72 graphics regions and a deterministic visibility fallback for nonzero pens whose emulated palette entry is still black at the current CPU frontier.

## ESP32-S3 graphics renderer status

- Firmware now includes `src/rtype_m72_video.c`, a graphics-only M72 tile/sprite renderer with the same region concepts as the host harness: `sprites`, `tiles0`, `tiles1`, `vram0`, `vram1`, sprite RAM, scroll registers, and 512-entry RGB565 palette.
- The S3 app allocates M72 VRAM/sprite RAM in PSRAM-capable memory and renders through this engine every frame. Until external ROM regions are deployed, deterministic fallback tile/sprite pixels keep the display path active without committing ROM data.
- Current S3 build remains green for `esp32-s3-8048s043c-rtype`; flash usage is about 225 KB.

## External ROM graphics loader

- Firmware exposes `rtype_rom_load_m72_graphics("/spiflash/rtype", &m72)` to load unpacked user-supplied ROM files into PSRAM-backed M72 `sprites`, `tiles0`, and `tiles1` regions.
- The loader mirrors the MAME R-Type sprite-region layout, including first-32KB copies from the 64KB `cpu-01/11/21/31` files.
- ROM files remain external and ignored; if the path is unavailable the S3 renderer continues with deterministic fallback pixels.

## Shared M72 core state

- Firmware now has `src/rtype_m72_core.c`, an ESP-friendly M72 core state layer with a 1MB CPU address map, work/video/sprite/palette memory mapping, idle input ports, scroll/video-off ports, and frame rendering through `rtype_m72_video`.
- The S3 app initializes this core and renders from its mapped VRAM/sprite RAM, aligning firmware structure with the host harness and preparing for a shared CPU backend.

## Firmware CPU backend status

- Added `src/rtype_i86_cpu.c`, an incremental firmware-side 8086/V30-family CPU backend interface over `rtype_m72_core_t` memory/port callbacks.
- Current firmware CPU module includes reset/vector/run plumbing and the migrated host-harness opcode coverage used by the R-Type reset/frame path. The host harness remains the runtime oracle until the S3 app is wired to run CPU frames from external ROMs.
- Next extraction step is to wire S3 frame execution behind a build/runtime flag and feed the CPU backend from external main-program ROMs.

## Palette/visible-frame audit

- Palette RAM is populated in host runs (`palram0_nz≈1517`, `palram1_nz≈1432`; palette entries nonzero in both banks).
- The renderer mostly resolves nonzero pens through real palette entries (`render_palette_px≈35938`) with a smaller fallback contribution (`render_fallback_px≈3606`).
- M72 visible X is `64..447` on a 512-wide raw screen; host and firmware renderers now subtract the visible-area X offset when writing the 384-wide framebuffer.
- Current frame is still not done-condition quality: only about 640 nonblack visible pixels in a small `48x24` bbox, and 20M/100M/300M host frames are identical. Remaining gap is sparse/stuck live video state, not absent palette RAM.

## Done-condition host evidence

- After fixing ModRM displacement/immediate ordering for `C6/C7`, then adding `XCHG`, `TEST r/m,reg`, `TEST AL,imm`, `OR r/m,imm`, `POP r/m16`, `SUB r/m,reg`, `XOR AX,imm16`, and `ADD r/m8,r8`, the host harness now runs into full-screen R-Type graphics.
- Verified long host runs:
  - `make host-run` now defaults to 300M instructions and renders a full-screen game frame: visible bbox `0,0-383,255`, active scroll `230/460`, live tile+sprite rendering, no halt, 70 output colors.
  - 600M instructions: no halt, `irq_count=4999`, `visible_nonblack=147418`, `visible_sprite_px=38450`, 87 output colors, active scroll `175/343`.
  - 300M vs 600M frame delta: `AE=92993`, proving visible frame advancement.
- This satisfies the host done condition: the harness loads the supplied R-Type ROM set and runs the game far enough to render advancing full-screen game graphics.

## Real-palette color correction

- Removed the bring-up/debug color fallback that replaced nonzero pens hitting black palette entries with synthetic RGB colors.
- Host and firmware renderers now return the palette entry exactly; black palette entries remain black.
- Post-fix host evidence: 300M default `make host-run` uses real palette only, full visible bbox `0,0-383,253`, active scroll `230/460`, 49 output colors.
- 300M vs 600M real-palette frames still differ substantially (`AE=69648`), so visible advancement remains proven without fake colors.

## MAME plane-bit ordering fix

- Corrected tile/sprite graphics plane decode to match MAME `gfx_element::decode`: `planeoffset[0]` contributes the high pen bit (`1 << (planes-1)`), then shifts downward.
- Previous decode used `1 << plane`, reversing M72 pen bits and producing wrong palette indices/colors despite correct palette RAM decode.
- Host and firmware now decode M72 planes as: RGN_FRAC(3/4)->bit3, 2/4->bit2, 1/4->bit1, 0/4->bit0.
- Corrected real-palette sequence artifact: `artifacts/rtype-planefix-sequence.png` (not tracked).

## CYD blitter plan

- Smallest CYD target added as `esp32-cyd-rtype` / `make build-cyd`.
- Display path targets known physical format: 240x320 ILI9341 SPI RGB565.
- CYD display now rotates the game into a logical 320x240 landscape view, using a 320x213 aspect-correct viewport mapped onto physical portrait columns.
- `src/rtype_blit.c` includes the rotated 384x256 -> 320x213 column blitter plus the lower-bandwidth 384x256 -> 240x160 5/8 fallback.
- Details: [cyd-blitter-plan.md](cyd-blitter-plan.md)

## Async CYD display worker

- CYD display updates now run through a FreeRTOS worker pinned to core 0.
- The worker performs the 5/8 RGB565 strip downsample and ILI9341 SPI flush; present calls queue the newest frame and drop stale queued frames if display bandwidth falls behind.
- The producer uses double source framebuffers when allocation permits.

## ESP32-S3 live-emulation status (current)

The primary target is `esp32-s3-8048s043c-rtype` (`make build-s3`). It now runs the live R-Type/M72 V30 path on the ESP32-S3 replacement board and renders the active playfield on the 800x480 RGB panel.

Current S3 architecture:

- Board/profile: `RTYPE_BOARD_ESP32_8048S043C` / Sunton ESP32-8048S043C.
- Display: 800x480 RGB panel, proportional output only.
- Game framebuffer: 384x256 RGB565.
- Panel mapping: aspect-preserving `720x480+40,0`; black side bars.
- ROM filesystem: `/spiflash/rtype` FAT storage partition for graphics ROM files.
- Main CPU ROM map: dedicated `maincpu` data partition at `0x410000`, size `1MB`.
- FAT storage partition: `storage` at `0x510000`, size `9MB`.
- Main CPU opcode fetches are flash-mapped from the `maincpu` partition via `rtype_m72_core_map_maincpu_partition(&core, "maincpu")`.
- S3 sparse RAM regions live in PSRAM, including work RAM, sprite RAM, palette RAM, VRAM0/1, and the M72 sound/shared RAM window.

Important S3 fidelity fixes already applied:

- `5a19aee` maps M72 sound/shared RAM (`0xe0000..0xeffff`) on S3 and avoids treating ordinary data reads there as the reset ROM mirror.
- `b481f30` mirrors palette RAM address bit A9 and returns disconnected palette read bits high, matching MAME (`paletteram[offset] | 0xffe0`).
- `ec7e51f` switches the S3 tile compositor to MAME-style M72 tile group/priority masks instead of the old single opaque-BG + transparent-FG pass.
- `aac1a76` and `79d7672` align the host reference harness with the same tile priority masks and palette RAM semantics.

Current S3 performance/fidelity baseline:

- Render pacing default: `render_irq=5`.
- Typical active-playfield logs show ~40-59 IRQ/s depending on scene cost.
- The S3 source framebuffer contains the same dominant green terrain RGB565 values as the host reference during active `root=0x0aa6` playfield states.
- RGB panel color wiring has been verified separately with temporary red/green/blue bars; green displays correctly, so remaining visual differences in photos are not due to panel RGB order.

## Exact host-vs-S3 comparison workflow

Use the reproducible comparison target instead of arbitrary camera photos:

```bash
make compare-s3-host
```

This runs `tools/compare_s3_host.py`, which:

1. runs `tools/capture_s3_playfield.py`,
2. waits for S3 serial `S3 PERF` lines with `root=0x0aa6`,
3. captures a camera frame once the frame counter passes the configured threshold,
4. runs the host harness to the exact same `frame/root` state using `--target-frame` and `--target-root`,
5. generates a side-by-side image under `artifacts/compare/`.

The most recent exact matched comparison used:

- S3: `frame=0x07c4`, `root=0x0aa6`, `scroll=(486,0)/(461,0)`.
- Host: `frame_counter=0x07c4`, `root_state=0x0aa6`, `scroll=(486,0)/(461,0)`.

Generated artifact example:

```text
artifacts/compare/host-vs-s3-f07c4-r0aa6.jpg
```

For just device-side captures at active playfield states:

```bash
make capture-s3-playfield
```

The capture script writes images like:

```text
artifacts/camera/s3-root0aa6-0-f07c4-s486-461.jpg
```

Use exact matched comparisons for fidelity decisions. Arbitrary photos often land on title/ranking/dark playfield moments and can falsely suggest missing backgrounds.

## S3 flashing notes

Normal firmware flash:

```bash
make flash PIO_ENV=esp32-s3-8048s043c-rtype SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

Data partitions must also be present after partition-layout changes or a full erase. Preferred target:

```bash
make flash-s3-data SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

This target runs the ROM packer, rebuilds the ignored 9MB wear-levelled FAT image, and flashes:

- `0x410000 artifacts/packed-rtype/maincpu-map.bin`
- `0x510000 artifacts/s3-rtype-fatfs-wl-9m.bin`

For image generation only:

```bash
make s3-fatfs-image
```

`artifacts/s3-rtype-fatfs-wl-9m.bin` is generated from `artifacts/s3-fatfs-root/rtype/*.bin` and is intentionally ignored.

## Current caveats

- The S3 path is live and renders the active playfield, but it is still a constrained ESP32-S3 port, not a full MAME implementation.
- Audio, real input, and general M72 compatibility remain out of scope.
- Do not reintroduce static/prebaked frames, coarse renderers, direct sprite-overlay shortcuts, or synthetic palette fallbacks.
- Use MAME references for renderer changes; keep host harness and firmware renderer semantics aligned.
