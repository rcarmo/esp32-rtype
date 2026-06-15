#!/usr/bin/env bun
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { rtypeManifest } from "./rtype_manifest";

const inDir = process.argv[2] ?? "roms/extracted/rtype";
const outDir = process.argv[3] ?? "artifacts/packed-rtype";

function read(name: string): Buffer {
  const path = join(inDir, name);
  if (!existsSync(path)) throw new Error(`missing ${path}; run make extract-rom first`);
  return readFileSync(path);
}

function sha1(data: Buffer | Uint8Array): string {
  return createHash("sha1").update(data).digest("hex");
}

function interleave(evenName: string, oddName: string): Buffer {
  const even = read(evenName);
  const odd = read(oddName);
  if (even.length !== odd.length) throw new Error(`${evenName}/${oddName} sizes differ`);
  const out = Buffer.alloc(even.length * 2);
  for (let i = 0; i < even.length; i++) {
    out[i * 2] = even[i];
    out[i * 2 + 1] = odd[i];
  }
  return out;
}

mkdirSync(outDir, { recursive: true });

const mainLow = interleave("rt_r-l0-.bin", "rt_r-h0-.bin");
const mainHigh = interleave("rt_r-l1-.bin", "rt_r-h1-.bin");
const mainCpu = Buffer.concat([mainLow, mainHigh]);
writeFileSync(join(outDir, "maincpu-v30.bin"), mainCpu);

// MAME-faithful 20-bit V30 address map for R-Type/M72:
//   0x00000..0x1ffff = L0/H0
//   0x20000..0x3ffff = L1/H1
//   0xe0000..0xfffff = L1/H1 mirror for reset vector fetch
const mainMap = Buffer.alloc(0x100000, 0xff);
mainLow.copy(mainMap, 0x00000);
mainHigh.copy(mainMap, 0x20000);
mainHigh.copy(mainMap, 0xe0000);
writeFileSync(join(outDir, "maincpu-map.bin"), mainMap);

const gfxGroups = [
  ["cpu-00.bin", "cpu-10.bin", "cpu-20.bin", "cpu-30.bin"],
  ["cpu-01.bin", "cpu-11.bin", "cpu-21.bin", "cpu-31.bin"],
  ["cpu-a0.bin", "cpu-a1.bin", "cpu-a2.bin", "cpu-a3.bin", "cpu-b0.bin", "cpu-b1.bin", "cpu-b2.bin", "cpu-b3.bin"],
];
const gfxPacked = Buffer.concat(gfxGroups.flat().map(read));
writeFileSync(join(outDir, "gfx-raw-planes.bin"), gfxPacked);

const resetTail = mainHigh.subarray(mainHigh.length - 16);
const resetJumpOffset = mainHigh.length - 16 + resetTail.indexOf(Buffer.from([0xea, 0x00, 0x08, 0x00, 0x3f]));

const report = {
  inDir,
  outDir,
  maincpu: {
    file: "maincpu-v30.bin",
    size: mainCpu.length,
    sha1: sha1(mainCpu),
    resetTail: resetTail.toString("hex"),
    resetJump: resetJumpOffset >= 0 ? { offsetInHighPair: resetJumpOffset, linearAddress: 0xe0000 + resetJumpOffset, bytes: "ea0008003f", asm: "jmp 3f00:0800" } : null,
  },
  maincpuMap: {
    file: "maincpu-map.bin",
    size: mainMap.length,
    sha1: sha1(mainMap),
    resetVectorBytes: mainMap.subarray(0xffff0, 0xffff5).toString("hex"),
  },
  gfxRawPlanes: {
    file: "gfx-raw-planes.bin",
    size: gfxPacked.length,
    sha1: sha1(gfxPacked),
  },
  sourceFiles: rtypeManifest.map((entry) => {
    const data = read(entry.name);
    return { ...entry, sha1: sha1(data) };
  }),
};
writeFileSync(join(outDir, "manifest.json"), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));
