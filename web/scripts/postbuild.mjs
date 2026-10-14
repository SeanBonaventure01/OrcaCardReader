// After `vite build`: gzip every file (the firmware embeds the .gz copies) and enforce the
// size budget from docs/ARCHITECTURE.md §6.8.
import { readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';
import { gzipSync } from 'node:zlib';

const BUDGET_BYTES = 150 * 1024;
const dist = new URL('../dist/', import.meta.url).pathname;

function walk(dir) {
  return readdirSync(dir).flatMap((f) => {
    const p = join(dir, f);
    return statSync(p).isDirectory() ? walk(p) : [p];
  });
}

let total = 0;
for (const file of walk(dist)) {
  if (file.endsWith('.gz')) continue;
  const gz = gzipSync(readFileSync(file), { level: 9 });
  writeFileSync(`${file}.gz`, gz);
  total += gz.length;
  console.log(`${file.slice(dist.length).padEnd(20)} ${String(gz.length).padStart(7)} B gz`);
}
console.log(`total ${(total / 1024).toFixed(1)} KB gzipped (budget ${BUDGET_BYTES / 1024} KB)`);
if (total > BUDGET_BYTES) {
  console.error('web UI is over its size budget');
  process.exit(1);
}
