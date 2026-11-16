// U15: the reference of core/src/rt/fdlibm.cpp: Node's own Math.cos / sin / tan / log on 400000 arguments (the ranges Flex, its filter and Retrigger use
// plus wider ones), written as float64 files for ctest `libm`.   node native/tools/libm-dump.mjs <outDir>
import fs from 'node:fs';
import path from 'node:path';
const out = process.argv[2]; if (!out) { console.error('usage: libm-dump.mjs <outDir>'); process.exit(1); }
fs.mkdirSync(out, { recursive: true });
const N = 400000, x = new Float64Array(N), o = new Float64Array(N * 4);
let s = 12345; const r = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; };
for (let i = 0; i < N; i++) {
  const m = i % 8;
  x[i] = m === 0 ? 2 * Math.PI * (r() * 4) * (1 + Math.floor(r() * 24)) : m === 1 ? 2 * Math.PI * r() : m === 2 ? -r() * 30 : m === 3 ? r() * 1.6 : m === 4 ? Math.PI * Math.min(21600, 20 * Math.pow(2, r() * 9) * 8) / 48000 : m === 5 ? Math.exp((r() - 0.5) * 30) : m === 6 ? r() * 1e5 : 1 + r() * 20 - 10 * r();
  o[i * 4] = Math.cos(x[i]); o[i * 4 + 1] = Math.sin(x[i]); o[i * 4 + 2] = Math.tan(x[i]); o[i * 4 + 3] = Math.log(x[i]);
}
fs.writeFileSync(path.join(out, 'x.f64'), Buffer.from(x.buffer)); fs.writeFileSync(path.join(out, 'o.f64'), Buffer.from(o.buffer));
console.log(`wrote ${N} arguments to ${out}`);
