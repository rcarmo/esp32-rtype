# R-Type ROM layout notes

Source archive: `roms/rtype.zip` (ignored, user-supplied).

## Current classification

The attached archive is an Irem R-Type / M72 ROM set, not Neo Geo.

### Main CPU program

The `rt_r-l*` / `rt_r-h*` files are the V30/8086-family main program byte lanes.

Packed order used by `tools/pack_rtype.ts`:

```text
maincpu-v30.bin = interleave(L0,H0) || interleave(L1,H1)
```

The upper pair with `L,H` interleave has a plausible x86 reset tail:

```text
EA 00 08 00 3F  =>  jmp 3f00:0800
```

This is the strongest local evidence for the byte-lane order.

### Graphics planes

The `cpu-*` files are graphics/bitplane ROMs. A simple 4-plane 8x8 candidate decoder already produces recognizable static R-Type graphics and text:

```text
cpu-00/10/20/30  => graphics plane group candidate
cpu-01/11/21/31  => graphics plane group candidate
cpu-a0/a1/a2/a3  => graphics plane group candidate
cpu-b0/b1/b2/b3  => graphics plane group candidate
```

The generated atlas is a probe, not final M72 video emulation. It confirms the ROMs contain usable planar graphics and gives the next display-only milestone a concrete host-side artifact.

Generate locally with:

```bash
make gfx-atlas
```

Combined preview:

```text
artifacts/gfx-atlas/rtype-gfx-probe-combined.png
```

Artifacts are ignored because they are derived from the user-supplied ROM archive.
