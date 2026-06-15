# rtype-tab5

Project goal: display-only R-Type/Irem M72 bring-up on M5Stack Tab5 ESP32-P4.

Constraints:
- Do not commit ROM contents. `roms/*.zip` and extracted binaries are ignored.
- First milestone is display output only: no audio and no input.
- Prefer ESP32-P4/Tab5 only; do not generalize until display boot works.
- Reuse stable Cydintosh Tab5 display/BSP configuration where possible.
- Validate host tools with `make check` and firmware with `make build` when ESP-IDF/PlatformIO is available.
