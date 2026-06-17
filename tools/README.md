# Tooling

Helper scripts for ROM inspection/packing, host/S3 comparisons, and serial smoke tests.

## ROM tools

```bash
bun tools/inspect_rtype.ts roms/rtype.zip roms/extracted/rtype
bun tools/pack_rtype.ts roms/extracted/rtype artifacts/packed-rtype
bun tools/pack_cyd_storage.ts roms/extracted/rtype artifacts/packed-rtype/cyd-storage.bin
```

Prefer the Makefile wrappers:

```bash
make inspect-rom
make extract-rom
make pack-rom
```

ROMs and generated packed artifacts are local-only and ignored by git.

## S3 validation tools

```bash
make smoke-s3
make capture-s3-playfield
make compare-s3-host
```

`smoke_s3.py` is the required serial-only hardware gate. It checks boot/runtime logs and requires an active `S3 PERF` line with `qdrain=<nonzero>/0`, guarding against the background-incomplete mid-update render regression.

`capture_s3_playfield.py` and `compare_s3_host.py` use V4L2 camera capture when a usable device is present. Camera devices are auto-detected from `/dev/video*`; pass `--camera /dev/videoN` explicitly when needed.

## Host reference

The host harness is C++ and built by the Makefile:

```bash
make host-harness
make host-run
```

Use it as a renderer/CPU-state reference when changing firmware renderer semantics.
