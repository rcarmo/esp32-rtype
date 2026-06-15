#!/usr/bin/env bun
import { $ } from "bun";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";

const zip = process.argv[2] ?? "roms/rtype.zip";
const outDir = process.argv[3] ?? "roms/extracted/rtype";
const expected: Record<string, { size: number; region: string }> = {
  "cpu-30.bin": { size: 65536, region: "main-v30-program/high-or-bank" },
  "cpu-20.bin": { size: 65536, region: "main-v30-program/high-or-bank" },
  "cpu-10.bin": { size: 65536, region: "main-v30-program/high-or-bank" },
  "cpu-00.bin": { size: 65536, region: "main-v30-program/high-or-bank" },
  "cpu-b3.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "cpu-b2.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "cpu-b1.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "cpu-b0.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "cpu-a3.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "cpu-a2.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "cpu-a1.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "cpu-a0.bin": { size: 32768, region: "graphics/tiles-or-sprites" },
  "rt_r-l1-.bin": { size: 65536, region: "graphics/tiles-or-sprites" },
  "rt_r-l0-.bin": { size: 65536, region: "graphics/tiles-or-sprites" },
  "rt_r-h1-.bin": { size: 65536, region: "graphics/tiles-or-sprites" },
  "rt_r-h0-.bin": { size: 65536, region: "graphics/tiles-or-sprites" },
  "cpu-01.bin": { size: 65536, region: "main-v30-program/alternate-revision" },
  "cpu-11.bin": { size: 65536, region: "main-v30-program/alternate-revision" },
  "cpu-21.bin": { size: 65536, region: "main-v30-program/alternate-revision" },
  "cpu-31.bin": { size: 65536, region: "main-v30-program/alternate-revision" },
};

if (!existsSync(zip)) {
  console.error(`Missing ${zip}`);
  process.exit(1);
}
rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });
await $`unzip -q ${zip} -d ${outDir}`;

let total = 0;
let ok = true;
const files = Object.keys(expected).sort();
const rows = files.map((name) => {
  const path = join(outDir, name);
  if (!existsSync(path)) {
    ok = false;
    return { name, present: false, size: 0, sha1: "", region: expected[name].region };
  }
  const data = readFileSync(path);
  total += data.length;
  if (data.length !== expected[name].size) ok = false;
  return {
    name,
    present: true,
    size: data.length,
    sha1: createHash("sha1").update(data).digest("hex"),
    region: expected[name].region,
  };
});

console.log(JSON.stringify({ zip, outDir, ok, fileCount: rows.filter((r) => r.present).length, total, files: rows }, null, 2));
process.exit(ok ? 0 : 2);
