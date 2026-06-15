#!/usr/bin/env bun
import { $ } from "bun";
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { join } from "node:path";

type PlaneGroup = {
  name: string;
  files: string[];
  tileCount: number;
  note: string;
};

const inDir = process.argv[2] ?? "roms/extracted/rtype";
const outDir = process.argv[3] ?? "artifacts/gfx-atlas";
const atlasTiles = Number(process.argv[4] ?? 512);

const groups: PlaneGroup[] = [
  { name: "cpu-00102030-8x8", files: ["cpu-00.bin", "cpu-10.bin", "cpu-20.bin", "cpu-30.bin"], tileCount: 8192, note: "4-plane 8x8 candidate from cpu-00/10/20/30" },
  { name: "cpu-01112131-8x8", files: ["cpu-01.bin", "cpu-11.bin", "cpu-21.bin", "cpu-31.bin"], tileCount: 8192, note: "4-plane 8x8 candidate from cpu-01/11/21/31" },
  { name: "cpu-a0123-8x8", files: ["cpu-a0.bin", "cpu-a1.bin", "cpu-a2.bin", "cpu-a3.bin"], tileCount: 4096, note: "4-plane 8x8 candidate from cpu-a0/a1/a2/a3" },
  { name: "cpu-b0123-8x8", files: ["cpu-b0.bin", "cpu-b1.bin", "cpu-b2.bin", "cpu-b3.bin"], tileCount: 4096, note: "4-plane 8x8 candidate from cpu-b0/b1/b2/b3" },
];

const palette = [
  [0, 0, 0], [38, 50, 92], [52, 101, 164], [69, 180, 210],
  [52, 130, 74], [101, 170, 72], [180, 190, 72], [238, 214, 92],
  [125, 68, 54], [190, 92, 62], [230, 132, 82], [250, 180, 116],
  [105, 74, 140], [160, 110, 190], [210, 170, 230], [255, 255, 255],
];

function readPlane(name: string): Buffer {
  const path = join(inDir, name);
  if (!existsSync(path)) throw new Error(`missing ${path}; run make extract-rom first`);
  return readFileSync(path);
}

function setPixel(rgb: Uint8Array, width: number, x: number, y: number, colorIndex: number) {
  const [r, g, b] = palette[colorIndex & 15];
  const off = (y * width + x) * 3;
  rgb[off] = r; rgb[off + 1] = g; rgb[off + 2] = b;
}

function decode8x8Tile(planes: Buffer[], tileIndex: number, px: number, py: number): number {
  // Simple planar candidate: each plane contributes one bit; each tile is 8 bytes
  // per plane, one byte per row, MSB first. This is intentionally a probe
  // decoder until the M72 GFX decode table is wired from a verified source.
  const base = tileIndex * 8;
  let value = 0;
  for (let p = 0; p < 4; p++) {
    const byte = planes[p][base + py] ?? 0;
    value |= ((byte >> (7 - px)) & 1) << p;
  }
  return value;
}

function writePpm(path: string, width: number, height: number, rgb: Uint8Array) {
  const header = Buffer.from(`P6\n${width} ${height}\n255\n`, "ascii");
  writeFileSync(path, Buffer.concat([header, Buffer.from(rgb)]));
}

mkdirSync(outDir, { recursive: true });
const report: any = { inDir, outDir, atlasTiles, groups: [] };

for (const group of groups) {
  const planes = group.files.map(readPlane);
  const tiles = Math.min(atlasTiles, group.tileCount, Math.min(...planes.map((p) => Math.floor(p.length / 8))));
  const cols = 32;
  const rows = Math.ceil(tiles / cols);
  const tileW = 8;
  const tileH = 8;
  const gap = 1;
  const width = cols * (tileW + gap) + gap;
  const height = rows * (tileH + gap) + gap;
  const rgb = new Uint8Array(width * height * 3);
  rgb.fill(18);

  for (let t = 0; t < tiles; t++) {
    const tx = gap + (t % cols) * (tileW + gap);
    const ty = gap + Math.floor(t / cols) * (tileH + gap);
    for (let y = 0; y < tileH; y++) {
      for (let x = 0; x < tileW; x++) {
        setPixel(rgb, width, tx + x, ty + y, decode8x8Tile(planes, t, x, y));
      }
    }
  }

  const ppm = join(outDir, `${group.name}.ppm`);
  const png = join(outDir, `${group.name}.png`);
  writePpm(ppm, width, height, rgb);
  await $`convert ${ppm} ${png}`.quiet();
  report.groups.push({ ...group, renderedTiles: tiles, width, height, ppm, png });
  console.log(`rendered ${png} (${tiles} tiles)`);
}

writeFileSync(join(outDir, "atlas-report.json"), JSON.stringify(report, null, 2));
