# M5Stack Tab5 ESP32-P4 target

## Role

Secondary ESP32-P4 build profile retained from the original Tab5 direction. The S3 8048S043C board is now the primary live R-Type target, but Tab5 remains useful as a portability/build target.

PlatformIO environment:

```text
esp32-p4-tab5-rtype
```

Board define:

```c
RTYPE_BOARD_M5STACK_TAB5_ESP32P4
```

Build command:

```bash
make build-tab5
```

Current status: this profile is retained but not part of `make build-all`; local BSP/component integration still needs follow-up before Tab5 is treated as a green target.

## Hardware characteristics

- MCU: ESP32-P4 class target.
- Display stack uses the local M5Stack Tab5 BSP/profile components in `components_tab5/m5stack_tab5`.
- The board differs substantially from the ESP32-S3 8048S043C RGB panel and the ESP32 CYD SPI panel.
- Tab5 display initialization depends on the local BSP component plus managed Espressif LCD/touch dependencies.

## Rendering strategy

The Tab5 strategy is currently a portability strategy rather than the primary live-emulation strategy.

### Source renderer

- Tab5 should use the shared M72 source renderer when brought up.
- Board-specific display code should consume a 384x256 source framebuffer or an explicitly documented proportional viewport.
- MAME fidelity decisions should be made in the shared renderer, not in Tab5-specific display code.

### Display path expectations

- Keep Tab5 display initialization isolated in `rtype_display_tab5.c` and the local `m5stack_tab5` component.
- Avoid leaking S3 RGB-panel assumptions into Tab5.
- If Tab5 later gets a full-framebuffer path, document the panel mapping and buffering model here before claiming visual parity.
- If Tab5 uses a strip/tile path instead, document the scaling and rotation math like the CYD page does.

### Smooth rendering techniques to consider

Tab5 has more headroom than the classic CYD, but it still needs board-specific validation. Candidate techniques should be proven before becoming claims:

- PSRAM-backed source framebuffers.
- Async display handoff if the panel/BSP supports it safely.
- Shared decoded M72 tile/sprite row caches.
- Proportional scaling from 384x256, not arbitrary stretch.
- Complete-frame queue drain if running the live S3-style V30 path.

## Limitations

- Current primary validation evidence is for the S3 replacement board, not Tab5 hardware.
- Tab5 hardware behavior may require separate display timing and panel validation before live-emulation claims.
- Local BSP/component resolution is currently the main follow-up item for `make build-tab5`.
- Audio/input/general MAME compatibility remain out of scope.

## Validation

```bash
make build-tab5
```

At the time of this documentation update, S3 and CYD are the build-all targets. Treat Tab5 build failures as a board-profile issue unless the change under test directly touches shared code.

If Tab5 hardware is attached, pass the correct serial/JTAG device explicitly:

```bash
make flash PIO_ENV=esp32-p4-tab5-rtype SERIAL_PORT=/dev/serial/by-id/...
```

Any future Tab5-specific rendering technique should be documented here and should not weaken the S3 fidelity requirements.
