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

- `rtype_blit_cyd_scale_strip_240x160()`
- exact 5/8 nearest-neighbor downscale: `384x256 -> 240x160`
- strip-oriented output for SPI flushing
- no divides in the hot pixel loop
- unrolled 8-source-pixel to 5-destination-pixel mapping
- packed 32-bit stores for two RGB565 pixels at a time

The repeating source-index pattern is:

```text
8 source pixels -> 5 destination pixels
src indices: 0, 1, 3, 4, 6
```

The vertical row pattern is the same:

```text
8 source rows -> 5 destination rows
src rows: 0, 1, 3, 4, 6
```

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
