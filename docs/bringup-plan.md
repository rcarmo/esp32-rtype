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
2. **M1 ROM ingestion**: validate/extract `rtype.zip`, classify program/graphics ROMs, and produce packed ROM regions.
3. **M2 static graphics proof**: decode enough graphics planes to draw recognizable R-Type tiles/sprites without CPU emulation.
4. **M3 CPU execution**: wire a V30/8086-family core for the main program; stub inputs and sound.
5. **M4 video registers**: render live tile/sprite state from emulated video RAM.

## Reused from Cydintosh

- PlatformIO ESP-IDF configuration
- ESP32-8048S043C RGB panel timings/pins
- optional M5Stack Tab5 BSP component and ST7123/ILI9881C panel detection approach
- ESP32-P4 rev-v1.x sdkconfig safeguards
- PSRAM-first framebuffer allocation pattern
- strip-based RGB565 panel flushing pattern
