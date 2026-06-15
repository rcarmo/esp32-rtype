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
  - 300M instructions: full visible bbox `0,0-383,255`, active scroll `230/460`, live tile+sprite rendering, no halt before that sample.
  - 600M instructions: no halt, `irq_count=4999`, `visible_nonblack=147418`, `visible_sprite_px=38450`, 87 output colors, active scroll `175/343`.
  - 300M vs 600M frame delta: `AE=92993`, proving visible frame advancement.
- This satisfies the host done condition: the harness loads the supplied R-Type ROM set and runs the game far enough to render advancing full-screen game graphics.
