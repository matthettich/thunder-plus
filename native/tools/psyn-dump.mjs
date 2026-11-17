// U33: THE PUMP SYNTH REFERENCE (Node only): the web app's own render path, pump/synth/pump-synth-meta.js + pump-synth-engine.js (PsynCore.render, loaded in a vm exactly as tests/pump-synth.test.js
// loads them) with a pump-synth.wasm, dumping SAMPLES (not hashes: the wasm is built with -ffast-math, no native build reproduces its float sums bit for bit; the gate is native/tests/test_pumpsynth.cpp).
//   node native/tools/psyn-dump.mjs [--wasm PUMP-SYNTH.wasm] [outDir]   writes <outDir>/cases.json and <outDir>/<case>.f32 (the render, mono float32)
//   node native/tools/psyn-dump.mjs --check                              the case list (names) against native/tests/golden/psyn_cases.inc
//   node native/tools/psyn-dump.mjs --write-cases                        writes that list (native/tests/golden/psyn_cases.inc; committed)
// WHICH wasm: --wasm / PSYN_WASM, else the repo's shipped pump/synth/pump-synth.wasm (byte-identical to the one native/tools/build-wasm.sh builds from the pin: third_party/PINS.md).
// CASES: the 13 rows of tests/golden/pump-synth.json (every synth at its defaults, 48 kHz, hold 0.5; the reference's hash is compared with the golden's and a difference is reported in cases.json),
// every factory preset of every synth, every synth with all its parameters at their minimum / maximum and with three seeded random sets, the pad's own macros (Tune, Hit, Level, Drive, hold, Drum of a kit,
// the song tempo for WOB, FREEZE's File), sample rates 8000 to 96000. `latency` holds PsynCore.latency of every synth at several rates; `engines` the page's KINDS (type, len, trim, kit, drums); `meta` the page's parameter tables.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const H = require(path.join(ROOT, 'tests/helpers.js'));
const args = process.argv.slice(2); const opt = {}; const pos = [];
for (let i = 0; i < args.length; i++) { if (args[i] === '--wasm') opt.wasm = args[++i]; else if (args[i].startsWith('--')) opt[args[i].slice(2)] = true; else pos.push(args[i]); }
const WASM = opt.wasm || process.env.PSYN_WASM || path.join(ROOT, 'pump/synth/pump-synth.wasm');

function load() {   // as tests/pump-synth.test.js does
  const c = { console: { warn() {}, log() {} }, Math, Float32Array, Uint8Array, WebAssembly, Object, Array, Set, Map, JSON, String, Number, isFinite };
  c.globalThis = c; c.self = c; vm.createContext(c);
  vm.runInContext(H.read('pump/synth/pump-synth-meta.js'), c, { filename: 'pump-synth-meta.js' });
  vm.runInContext(H.read('pump/synth/pump-synth-engine.js'), c, { filename: 'pump-synth-engine.js' });
  c.PsynCore.attachModule(new WebAssembly.Module(fs.readFileSync(WASM)));
  return c;
}
const C = load(), P = C.PsynCore, xe = P.xeng(), ui = P.xui(), META = C.PUMP_SYNTH_META, KEYS = P.rendered;   // KEYS: pmono ... pwob (the rendered engines; a live twin renders as its twin)
const AL = { drive: 'o_drive', tune: 'o_tune', level: 'o_level' }, al = s => AL[s] || s;
const mb32 = seed => { let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; };
const hashOf = s => { let h = 0x811c9dc5 >>> 0; for (const ch of Buffer.from(s)) { h ^= ch; h = Math.imul(h, 0x01000193) >>> 0; } return h; };
const metaOf = k => META.synth[P.engines[k].k];   // the plugin's meta of an engine key (KINDS[key].k: 'mono' ...; the page's key order is not the type order: plife is last)
const base = k => Object.assign(xe[k].d(), { tune: 0, level: 0, drive: 0 });
const settable = p => !((p.flags | 0) & (8 | 32));   // the page's rows skip output / hidden-flag parameters
function fileSample(n, sr, f1) { const d = new Float32Array(n); for (let i = 0; i < n; i++) d[i] = 0.4 * Math.sin(2 * Math.PI * f1 * i / sr) + 0.2 * Math.sin(2 * Math.PI * 3 * f1 * i / sr); return d; }   // (as the web test's FREEZE file)

// ---- the case list ---------------------------------------------------------------------------------------------------------------------------------------------------
const cases = [];
const add = (name, k, pad, sr, hold, o = {}) => cases.push({ name, k, pad, sr, hold, vlen: o.vlen, bpm: o.bpm, file: o.file, golden: o.golden });
const G = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests/golden/pump-synth.json'), 'utf8'));
for (const k of KEYS) add('g_' + k, k, base(k), 48000, 0.5, { golden: G[k] });            // the golden's 13 rows: tests/pump-synth.test.js 'a pad renders the same every time, and the output is golden'
for (const k of KEYS) ui[k].pre.forEach(([n, o], i) => { if (i > 0) add('pre_' + k + '_' + i, k, Object.assign(xe[k].d(), o, { tune: 0, level: 0, drive: 0 }), 48000, 0.3); });   // every factory preset (not Init: g_)
for (const k of KEYS) {
  const m = metaOf(k);
  for (const [tag, pick] of [['lo', p => p.min], ['hi', p => p.max]]) {   // every parameter the page shows at its minimum / maximum together
    const pad = base(k); for (const p of m.params) if (settable(p)) pad[al(p.s)] = pick(p);
    add(tag + '_' + k, k, pad, 48000, 0.3);
  }
  for (let j = 0; j < 3; j++) {   // three seeded random sets, each parameter uniform over its range (integers and switches rounded)
    const r = mb32(hashOf(k) + 7919 * j), pad = base(k);
    for (const p of m.params) if (settable(p)) { let v = p.min + r() * (p.max - p.min); if ((p.flags | 0) & 6) v = Math.round(v); pad[al(p.s)] = Math.min(p.max, Math.max(p.min, v)); }
    add('rnd' + j + '_' + k, k, pad, 48000, 0.3);
  }
}
for (const k of KEYS) {   // the pad's macros on the defaults
  for (const t of [-24, -7.4, 12, 24]) add('tune_' + k + '_' + t, k, Object.assign(base(k), { tune: t }), 48000, 0.3);
  for (const v of [1, 127]) add('vel_' + k + '_' + v, k, Object.assign(base(k), { vel: v }), 48000, 0.3);
  add('lvdr_' + k, k, Object.assign(base(k), { level: 6, drive: 1 }), 48000, 0.3);
  add('lvdr2_' + k, k, Object.assign(base(k), { level: -30, drive: 0.5 }), 48000, 0.3);
  add('hold_short_' + k, k, base(k), 48000, 0.02);
  add('hold_long_' + k, k, base(k), 48000, 3);
  add('hold_len_' + k, k, base(k), 48000, 0, { vlen: 0.7 });   // hold 0: V.len
  for (const sr of [8000, 44100, 96000]) add('sr' + sr + '_' + k, k, base(k), sr, 0.3);
}
for (const k of KEYS) if (P.engines[k].kit) for (let d = 0; d < P.engines[k].drums; d++) add('kd_' + k + '_' + d, k, Object.assign(base(k), { kd: d }), 48000, 0.3);   // every drum of a kit
for (const b of [60, 200]) add('bpm_pwob_' + b, 'pwob', Object.assign(base('pwob'), { wob_sync: 1 }), 48000, 1, { bpm: b });
{ const fr = base('pfreeze'); fr.source = 0;   // FREEZE's File source: the pad's own sound
  add('file_pfreeze_44100', 'pfreeze', fr, 48000, 1, { file: { n: 88200, sr: 44100, f: 220 } });
  add('file_pfreeze_22050', 'pfreeze', Object.assign({}, fr, { root: 48 }), 44100, 0.5, { file: { n: 30000, sr: 22050, f: 110 } });
  add('file_pfreeze_none', 'pfreeze', fr, 48000, 0.5); }   // File with no sound: silent

if (opt['write-cases'] || opt.check) {
  const inc = path.join(ROOT, 'native/tests/golden/psyn_cases.inc');
  const text = '// GENERATED by native/tools/psyn-dump.mjs --write-cases: the names of the reference cases (a case whose reference file is missing FAILS). Do not edit.\nstatic const char* const kPsynCaseNames[] = {\n' + cases.map(c => '  "' + c.name + '",').join('\n') + '\n};\nstatic const int kPsynCaseN = ' + cases.length + ';\n';
  if (opt['write-cases']) { fs.writeFileSync(inc, text); console.log('wrote ' + inc + ' (' + cases.length + ' cases)'); }
  else { if (!fs.existsSync(inc) || fs.readFileSync(inc, 'utf8') !== text) { console.error('psyn_cases.inc differs from the case list: run node native/tools/psyn-dump.mjs --write-cases'); process.exit(1); } console.log('psyn cases current (' + cases.length + ')'); }
  process.exit(0);
}

// ---- the references --------------------------------------------------------------------------------------------------------------------------------------------------
const out = pos[0] || 'psyn-ref'; fs.mkdirSync(out, { recursive: true });
const meta = {}, engines = {};
KEYS.forEach(k => { const m = metaOf(k); meta[k] = { name: P.engines[k].k, params: m.params.map(p => [p.s, p.min, p.max, p.def, p.flags | 0]) };
  const E = P.engines[k]; engines[k] = { type: E.t, len: E.len, trim: E.trim || 1, kit: !!E.kit, drums: E.drums || 0, bpm: !!E.bpm, key: E.k }; });
const rows = []; let goldenBad = 0, bad = 0;
for (const c of cases) {
  const V = c.file ? { frzL: 0, layers: [{ sample: { data: fileSample(c.file.n, c.file.sr, c.file.f), sr: c.file.sr } }] } : (c.vlen ? { len: c.vlen } : undefined);
  if (c.file) fs.writeFileSync(path.join(out, c.name + '.file.f32'), Buffer.from(V.layers[0].sample.data.buffer));
  const d = P.render(c.k, c.pad, c.sr, c.hold, V, c.bpm);
  fs.writeFileSync(path.join(out, c.name + '.f32'), Buffer.from(d.buffer, d.byteOffset, d.byteLength));
  const finite = d.every(Number.isFinite); if (!finite) bad++;
  const hash = H.hashFloats(d); let gold = null; if (c.golden) { gold = c.golden === hash; if (!gold) goldenBad++; }
  rows.push({ name: c.name, k: c.k, type: P.engines[c.k].t, pad: c.pad, sr: c.sr, hold: c.hold, vlen: c.vlen || 0, bpm: c.bpm || 0, file: c.file || null, n: d.length, hash, goldenOk: gold, finite });
}
const latency = []; for (const k of KEYS) for (const sr of [8000, 22050, 44100, 48000, 96000]) latency.push({ k, sr, lat: P.latency(k, sr) });
fs.writeFileSync(path.join(out, 'cases.json'), JSON.stringify({ wasm: path.basename(WASM), cases: rows, latency, engines, meta, goldenRows: Object.keys(G).length, goldenBad }));
console.log('psyn-dump: ' + rows.length + ' cases, ' + rows.reduce((a, r) => a + r.n, 0) + ' samples, ' + bad + ' not finite, golden rows ' + (Object.keys(G).length - goldenBad) + ' of ' + Object.keys(G).length + ' equal the web golden hash -> ' + out);
