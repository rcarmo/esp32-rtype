export type RTypeRomRole = "maincpu_even" | "maincpu_odd" | "gfx_bitplane";

export type RTypeRomEntry = {
  name: string;
  size: number;
  sha1?: string;
  role: RTypeRomRole;
  group: "main_low" | "main_high" | "gfx_cpu_0" | "gfx_cpu_1" | "gfx_tiles";
  note: string;
};

// User-provided rtype.zip is an Irem R-Type/M72 set. The rt_r-l*/rt_r-h*
// pairs are the V30/8086-family main program ROMs. Interleaving L,H for the
// high pair yields the reset-vector tail `EA 00 08 00 3F` (jmp 3f00:0800).
// The cpu-* files are graphics/bitplane ROMs for the display proof path.
export const rtypeManifest: RTypeRomEntry[] = [
  { name: "rt_r-l0-.bin", size: 65536, role: "maincpu_even", group: "main_low", note: "main program low half, even byte lane" },
  { name: "rt_r-h0-.bin", size: 65536, role: "maincpu_odd", group: "main_low", note: "main program low half, odd byte lane" },
  { name: "rt_r-l1-.bin", size: 65536, role: "maincpu_even", group: "main_high", note: "main program high/reset half, even byte lane" },
  { name: "rt_r-h1-.bin", size: 65536, role: "maincpu_odd", group: "main_high", note: "main program high/reset half, odd byte lane" },

  { name: "cpu-00.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_0", note: "graphics bitplane ROM" },
  { name: "cpu-10.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_0", note: "graphics bitplane ROM" },
  { name: "cpu-20.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_0", note: "graphics bitplane ROM" },
  { name: "cpu-30.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_0", note: "graphics bitplane ROM" },
  { name: "cpu-01.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_1", note: "graphics bitplane ROM" },
  { name: "cpu-11.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_1", note: "graphics bitplane ROM" },
  { name: "cpu-21.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_1", note: "graphics bitplane ROM" },
  { name: "cpu-31.bin", size: 65536, role: "gfx_bitplane", group: "gfx_cpu_1", note: "graphics bitplane ROM" },
  { name: "cpu-a0.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
  { name: "cpu-a1.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
  { name: "cpu-a2.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
  { name: "cpu-a3.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
  { name: "cpu-b0.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
  { name: "cpu-b1.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
  { name: "cpu-b2.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
  { name: "cpu-b3.bin", size: 32768, role: "gfx_bitplane", group: "gfx_tiles", note: "graphics bitplane ROM" },
];

export function manifestByName(): Map<string, RTypeRomEntry> {
  return new Map(rtypeManifest.map((entry) => [entry.name, entry]));
}
