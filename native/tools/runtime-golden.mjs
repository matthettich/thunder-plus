// U05 golden: hashes of what the JS `Auto` (core/seq-core.js) and the JS seeded random stream give for a fixed set of timelines,
// for native/tests/test_runtime.cpp to compare the C++ against, bit for bit.
//   node native/tools/runtime-golden.mjs            writes native/tests/golden/runtime_golden.inc
//   node native/tools/runtime-golden.mjs --check    exits 1 if the committed file differs (the JS changed: port the change)
// The program below (which random draws, in which order) is mirrored statement for statement in test_runtime.cpp: change both.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const src = fs.readFileSync(path.join(root, 'core/seq-core.js'), 'utf8');
function classText(name) {
  const a = src.indexOf('class ' + name + ' {'); if (a < 0) throw new Error('class ' + name + ' not found');
  let d = 0, i = a, started = false;
  for (; i < src.length; i++) { const c = src[i]; if (c === '{') { d++; started = true; } else if (c === '}') { d--; if (started && d === 0) break; } }
  return src.slice(a, i + 1);
}
const Auto = vm.runInNewContext('(' + classText('Auto') + ')', { Math });

// FNV-1a, 64 bits, over the 8 little-endian bytes of each double
const dv = new DataView(new ArrayBuffer(8)), M = (1n << 64n) - 1n;
function mk() { let h = 0xcbf29ce484222325n; return { add(x) { dv.setFloat64(0, x, true); for (let i = 0; i < 8; i++) { h ^= BigInt(dv.getUint8(i)); h = (h * 0x100000001b3n) & M; } }, get: () => h }; }

const NONE = 1e300;   // where the JS returns null
const fns = [x => x * x, x => 1 - Math.exp(-x * 3), x => x * 0.5 + 0.25];
let seed = 12345; const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
const R = 600, hashes = [];
for (let r = 0; r < R; r++) {
  const H = mk();
  const v0 = rnd() * 2 - 1, A = new Auto(v0), n = Math.floor(rnd() * 10); let tt = 0;
  for (let k = 0; k < n; k++) {
    const z = rnd(); if (!(z < 0.2)) tt += rnd() * 0.2;
    const kind = Math.floor(rnd() * 4), v = rnd() * 2 - 1, tc = rnd() * 0.05, d = rnd() * 0.1;
    if (kind === 0) A.set(v, tt); else if (kind === 1) A.lin(v, tt); else if (kind === 2) A.target(v, tt, tc); else A.curve(fns[k % 3], tt, d);
  }
  for (let q = 0; q < 40; q++) { const t = rnd() * (tt + 0.3); H.add(A.at(t)); }
  { const c = rnd() * (tt + 0.3); const a = A.constBlk(c, c + 0.01), b = A.constFrom(c); H.add(a == null ? NONE : a); H.add(b == null ? NONE : b); }
  { const t0 = rnd() * (tt + 0.3), C = new Float64Array(8); A.fill(C, t0, 1 / 48000, 8); for (let i = 0; i < 8; i++) H.add(C[i]); }
  for (let q = 0; q < 3; q++) {
    const t = rnd() * (tt + 0.2); A.prune(t); H.add(A.v0); H.add(A.ev.length);
    for (let s = 0; s < 10; s++) { const u = t + rnd() * 0.3; H.add(A.at(u)); }
  }
  { const t = rnd() * (tt + 0.3); A.hold(t); H.add(A.ev.length); for (let s = 0; s < 5; s++) { const u = rnd() * (tt + 0.4); H.add(A.at(u)); } }
  { const t = rnd() * (tt + 0.3); A.cancel(t); H.add(A.ev.length); H.add(A.at(t + 0.05)); }
  hashes.push(H.get());
}

// The seeded random stream: the line is taken from the source so a change to it shows up here.
const m = src.match(/const rand = \(\) => \{ if \(!seeded\) return Math\.random\(\); (let x = seeded\.s \+= 0x6D2B79F5;[^}]*?return [^;]*;) \};/);
if (!m) throw new Error('the seeded rand line was not found in core/seq-core.js');
const rand = new Function('seeded', m[1]);
const rng = [];
for (const nn of [1, 2, 0x7f, 0xff]) { const st = { s: nn * 2654435761 >>> 0 }, H = mk(); for (let i = 0; i < 1000; i++) H.add(rand(st)); rng.push(H.get()); }
// exp itself, across its ranges
const EH = mk(); { let s2 = 777; const r2 = () => { s2 = (s2 * 16807) % 2147483647; return s2 / 2147483647; };
  for (let i = 0; i < 20000; i++) { const x = (r2() * 2 - 1) * (i % 4 === 0 ? 700 : i % 4 === 1 ? 30 : i % 4 === 2 ? 1.2 : 1e-3); EH.add(Math.exp(x)); } }

// U07: pow (fdlibm's e_pow in V8), Math.round, the 32-bit conversions and Math.imul, and Number -> text (JSON.stringify's form)
const PH = mk(); { let s2 = 4242; const r2 = () => { s2 = (s2 * 16807) % 2147483647; return s2 / 2147483647; };
  for (let i = 0; i < 20000; i++) { const a = r2(), b = r2(), m = i % 5;
    PH.add(m === 0 ? Math.pow(2, (Math.floor(a * 241) - 120 + (b < 0.3 ? Math.floor(b * 100) / 100 : 0)) / 12) : m === 1 ? Math.pow(a * 1.2, 2) : m === 2 ? Math.pow(a * 100, b * 8 - 4)
      : m === 3 ? Math.pow(2, (b * 2400 - 1200) / 1200) : Math.pow(10, (a * 2 - 1) * 300 * b)); }
  for (const [x, y] of [[0, 0], [-0, 3], [-0, -3], [-8, 1 / 3], [-8, 3], [-8, 2], [1, Infinity], [-1, -Infinity], [2, Infinity], [0.5, Infinity], [2, -Infinity], [NaN, 0], [1, NaN], [Infinity, -1], [-Infinity, 3], [4, 0.5], [-4, 0.5],
    [5e-324, 0.5], [2, -1074], [2, -1080], [2, 1023.5], [2, 1024], [10, 308], [10, -320], [1.0000001, 1e10], [0.9999999, 1e20], [3, -1], [7, 1]]) { const v = Math.pow(x, y); PH.add(v !== v ? NaN : v); } }   // (a NaN result as the one canonical NaN: its payload is the machine's)
const RH = mk(); { let s2 = 99; const r2 = () => { s2 = (s2 * 16807) % 2147483647; return s2 / 2147483647; };
  for (const x of [0, -0, 0.5, -0.5, 1.5, -1.5, 2.5, -2.5, 0.49999999999999994, -0.49999999999999994, 4503599627370497, -4503599627370497, 9007199254740991, NaN, Infinity, -Infinity, 1e300, -1e300, 2147483647.5, 4294967295.7, 4294967296, -4294967297.2, 6e18, -6e18])
    { RH.add(Math.round(x)); RH.add(x | 0); RH.add(x >>> 0); }
  for (let i = 0; i < 20000; i++) { const a = r2(), b = r2(), x = (a * 2 - 1) * (i % 3 === 0 ? 1000 : i % 3 === 1 ? 5e9 : 3e19), h = Math.floor(b * 2000) / 2 - 500;
    RH.add(Math.round(x)); RH.add(Math.round(h)); RH.add(x | 0); RH.add(x >>> 0); RH.add(x >> 3); RH.add(Math.imul(x, b * 4294967296)); } }
let NS = 0xcbf29ce484222325n; { let s2 = 31337; const r2 = () => { s2 = (s2 * 16807) % 2147483647; return s2 / 2147483647; };
  const addS = x => { const t = String(x); for (let i = 0; i < t.length; i++) { NS ^= BigInt(t.charCodeAt(i)); NS = (NS * 0x100000001b3n) & M; } NS ^= 10n; NS = (NS * 0x100000001b3n) & M; };
  for (const x of [0, -0, 1, -1, 0.1, 1e21, 1e-7, 0.000001, 123456789012345680000, 1.7976931348623157e308, 5e-324, 0.5, 1 / 3, 2 / 3, 100, 1e5, 123.456, NaN, Infinity, -Infinity, 0.009542, 1.145038]) addS(x);
  for (let i = 0; i < 6000; i++) { const a = r2(), b = r2(), m = i % 6;
    addS(m === 0 ? Math.round(a * 4e8) / 1e6 : m === 1 ? Math.round((a - 0.5) * 2e3) / 1e6 : m === 2 ? Math.floor(a * 1e6) : m === 3 ? a * Math.pow(10, Math.floor(b * 60) - 30) : m === 4 ? -a * b : Math.round(a * 1e9) * Math.pow(10, Math.floor(b * 30))); } }

const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';
const out = '// GENERATED by native/tools/runtime-golden.mjs from core/seq-core.js. Do not edit: a mismatch means the port is wrong.\n'
  + `static const int kAutoGoldenN = ${R};\nstatic const unsigned long long kAutoGolden[${R}] = {\n` + hashes.map(hex).reduce((a, h, i) => a + (i % 4 === 0 ? '  ' : '') + h + ',' + (i % 4 === 3 ? '\n' : ' '), '') + '\n};\n'
  + `static const unsigned long long kRngGolden[4] = { ${rng.map(hex).join(', ')} };\n`
  + `static const unsigned long long kExpGolden = ${hex(EH.get())};\n`
  + `static const unsigned long long kPowGolden = ${hex(PH.get())};\nstatic const unsigned long long kRoundGolden = ${hex(RH.get())};\nstatic const unsigned long long kNumStrGolden = ${hex(NS)};\n`;
const file = path.join(root, 'native/tests/golden/runtime_golden.inc');
if (process.argv.includes('--check')) { const have = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : ''; if (have !== out) { console.error('runtime golden differs from the committed file'); process.exit(1); } console.log('runtime golden: unchanged'); }
else { fs.writeFileSync(file, out); console.log('wrote ' + path.relative(root, file)); }
