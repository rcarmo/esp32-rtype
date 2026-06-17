# Local ROM drop directory

Place your legally obtained local R-Type/M72 ROM archive here as:

```text
roms/rtype.zip
```

The ROM archive and extracted binaries are intentionally ignored by git and must not be committed or redistributed.

Ignored examples:

```text
roms/*.zip
roms/*.bin
roms/*.rom
roms/extracted/
```

Use the Makefile workflows to validate/extract/pack local ROM data into ignored artifacts:

```bash
make inspect-rom
make extract-rom
make pack-rom
make flash-s3-data SERIAL_PORT=/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0
```

Only this placeholder documentation is tracked.
