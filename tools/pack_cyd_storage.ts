#!/usr/bin/env bun
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

const inDir = process.argv[2] ?? "roms/extracted/rtype";
const outPath = process.argv[3] ?? "artifacts/packed-rtype/cyd-storage.bin";

function read(name: string): Buffer {
  const path = join(inDir, name);
  if (!existsSync(path)) throw new Error(`missing ${path}; run make extract-rom first`);
  return readFileSync(path);
}
function interleave(evenName: string, oddName: string): Buffer {
  const even = read(evenName), odd = read(oddName);
  const out = Buffer.alloc(even.length * 2);
  for (let i=0;i<even.length;i++) { out[i*2]=even[i]; out[i*2+1]=odd[i]; }
  return out;
}
function copy(dst: Buffer, off: number, data: Buffer, len = data.length) { data.copy(dst, off, 0, len); }
mkdirSync(join(outPath, ".."), {recursive:true});
const out = Buffer.alloc(0x100000, 0xff);
// 0x00000..0x3ffff compact maincpu, with high pair at 0x20000.
copy(out, 0x00000, Buffer.concat([interleave("rt_r-l0-.bin", "rt_r-h0-.bin"), interleave("rt_r-l1-.bin", "rt_r-h1-.bin")]));
// 0x40000 sprites, MAME layout.
copy(out, 0x40000, read("cpu-00.bin"), 0x10000);
copy(out, 0x50000, read("cpu-01.bin"), 0x08000); copy(out, 0x58000, read("cpu-01.bin"), 0x08000);
copy(out, 0x60000, read("cpu-10.bin"), 0x10000);
copy(out, 0x70000, read("cpu-11.bin"), 0x08000); copy(out, 0x78000, read("cpu-11.bin"), 0x08000);
copy(out, 0x80000, read("cpu-20.bin"), 0x10000);
copy(out, 0x90000, read("cpu-21.bin"), 0x08000); copy(out, 0x98000, read("cpu-21.bin"), 0x08000);
copy(out, 0xa0000, read("cpu-30.bin"), 0x10000);
copy(out, 0xb0000, read("cpu-31.bin"), 0x08000); copy(out, 0xb8000, read("cpu-31.bin"), 0x08000);
// tiles0/tiles1
for (const [base, names] of [[0xc0000, ["cpu-a0.bin","cpu-a1.bin","cpu-a2.bin","cpu-a3.bin"]], [0xe0000, ["cpu-b0.bin","cpu-b1.bin","cpu-b2.bin","cpu-b3.bin"]]] as const) {
  names.forEach((n, i) => copy(out, base + i*0x8000, read(n), 0x8000));
}
writeFileSync(outPath, out);
console.log(JSON.stringify({outPath, size: out.length, layout:{maincpu:"0x00000", sprites:"0x40000", tiles0:"0xc0000", tiles1:"0xe0000"}}, null, 2));
