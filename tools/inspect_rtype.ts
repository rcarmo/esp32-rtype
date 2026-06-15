#!/usr/bin/env bun
import { $ } from "bun";
import { createHash } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, rmSync } from "node:fs";
import { join } from "node:path";
import { rtypeManifest } from "./rtype_manifest";

const zip = process.argv[2] ?? "roms/rtype.zip";
const outDir = process.argv[3] ?? "roms/extracted/rtype";
const expected = new Map(rtypeManifest.map((entry) => [entry.name, entry]));

if (!existsSync(zip)) {
  console.error(`Missing ${zip}`);
  process.exit(1);
}
rmSync(outDir, { recursive: true, force: true });
mkdirSync(outDir, { recursive: true });
await $`unzip -q ${zip} -d ${outDir}`;

let total = 0;
let ok = true;
const files = [...expected.keys()].sort();
const rows = files.map((name) => {
  const path = join(outDir, name);
  const manifest = expected.get(name)!;
  if (!existsSync(path)) {
    ok = false;
    return { name, present: false, size: 0, sha1: "", role: manifest.role, group: manifest.group, note: manifest.note };
  }
  const data = readFileSync(path);
  total += data.length;
  if (data.length !== manifest.size) ok = false;
  return {
    name,
    present: true,
    size: data.length,
    expectedSize: manifest.size,
    sha1: createHash("sha1").update(data).digest("hex"),
    role: manifest.role,
    group: manifest.group,
    note: manifest.note,
  };
});

console.log(JSON.stringify({ zip, outDir, ok, fileCount: rows.filter((r) => r.present).length, total, files: rows }, null, 2));
process.exit(ok ? 0 : 2);
