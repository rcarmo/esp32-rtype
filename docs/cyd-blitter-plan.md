# CYD RGB565 blitter/downsampler plan

Target: smallest ESP32 CYD / ESP32-2432S028 with ILI9341 SPI LCD.

## Known formats

- Emulator source framebuffer: `384x256`, RGB565, `uint16_t` pixels.
- CYD physical panel: `240x320` portrait ILI9341 over SPI, RGB565 via `esp_lcd_panel_draw_bitmap`.
- First CYD display mode: exact-aspect `240x160` centered vertically in the `240x320` panel.

## Bandwidth target

Active game area per full frame:

```text
240 * 160 * 2 = 76,800 bytes/frame
```

This is much lower than pushing the original `384x256` frame or a rotated 320-wide landscape buffer.

## Current blitter

Implemented in `src/rtype_blit.c`:

- `rtype_blit_cyd_rotate_scale_columns_320x213()` for rotated fill mode
- maps `384x256 -> 320x213`, centered in logical `320x240`, then rotated onto physical `240x320`
- flushes only active physical columns `x=13..225` over the full 320-pixel height
- active transfer is about `213*320*2 = 136KB/frame`
- strip/column-oriented output for SPI flushing
- packed 32-bit stores remain available in the non-rotated 5/8 path
- `rtype_blit_cyd_scale_strip_240x160()` remains available as a lower-bandwidth exact 5/8 fallback

The lower-bandwidth fallback repeating source-index pattern is:

```text
8 source pixels -> 5 destination pixels
src indices: 0, 1, 3, 4, 6
```

The rotated fill path uses fixed integer source mapping for `384/320` horizontally and `256/213` vertically, isolated behind the blitter API for later LUT/SIMD replacement.

## SIMD preparation

Classic ESP32/CYD does not expose a desktop-style SIMD path for this use case, so the baseline fast path is packed scalar:

- 32-bit RGB565 pair stores
- fixed-pattern decimation
- strip buffers in DMA/internal memory

The function boundaries are intentionally SIMD-ready:

- pixel format is stable (`uint16_t` RGB565)
- source/destination strides are fixed
- hot loop is isolated in `scale_5_from_8()`
- architecture-specific implementations can replace `scale_5_from_8()` / row loops later

For ESP32-S3/LX7 or other targets, the next step is adding an `RTYPE_BLIT_USE_XTENSA_SIMD` implementation guarded by compile-time CPU feature macros.

## Build target

```bash
make build-cyd
```

Current status: builds successfully for `esp32-cyd-rtype`.

## Async display core

- `src/rtype_display_cyd.c` now starts a FreeRTOS display worker pinned to core 0.
- `rtype_display_present_rgb565()` queues the latest source frame and returns immediately; stale queued frames are dropped if the SPI worker falls behind.
- The app attempts to allocate a second 384x256 RGB565 source framebuffer so the producer can render the next frame while core 0 scales/flushes the previous frame.
- If the second framebuffer is unavailable, the producer throttles after present to reduce the chance of overwriting the only source buffer while the display task reads it.
- This is the intended CYD split: emulator/game producer on the app core, SPI/downsample/display on core 0.

## Rotated fill preview

Generated preview artifact: `artifacts/cyd-preview/rtype-cyd-rotated-fill-240x320.png` (not tracked).
