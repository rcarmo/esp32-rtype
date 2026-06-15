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
    resetJump: resetJumpOffset >= 0 ? { offsetInHighPair: resetJumpOffset, bytes: "ea0008003f", asm: "jmp 3f00:0800" } : null,
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
