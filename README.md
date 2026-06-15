# rtype-tab5

Display-first arcade emulator experiment for **Irem R-Type / M72**. The primary test target is now the **ESP32-S3 480×800 ESP32-8048S043C** board; **M5Stack Tab5 / ESP32-P4** is kept as a secondary profile.

Initial scope is deliberately narrow:

- load the user-provided `rtype.zip` ROM set from SD/flash/project `roms/`
- no audio
- no real input
- fixed idle input ports only when CPU emulation is wired
- initialize the ESP32-S3 RGB panel using the Cydintosh-proven 8048S043C path
- render a visible arcade-style framebuffer first, then replace it with decoded R-Type tile/sprite output

This is **not Neo Geo**. The attached ROM set is MAME-style Irem R-Type, not an AES/MVS cartridge.
