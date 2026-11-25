// PFX: THE REFERENCE OF THE LIBRARY PAD EFFECTS (Node only): the real ENGINE block of index.html (applyFx, txRun, modFxRun, padFx, renderVoice, FX, rvMix) run in Node's vm with the page's
// own libraries: tfx.wasm (TX), thunder-dsp.wasm (PL) and the shared MODFX modules of chains/chains-worklet.js (WavesDSP.MODULES, the text the waves worklet shares). One case per hosted type
// (default parameters and a seeded random set), a few pad cases through `padFx` / `renderVoice` (the order, the tail, the trim and fade), and the REFERENCE ITSELF run twice (a type whose
// output differs between two runs is listed: its golden is not usable).
//   node native/tools/padfx-dump.mjs [outDir]   writes <outDir>/<case>.json (the case: the effect object, sr, bpm, kind), <case>.in.f32 (the sound), <case>.f32 (the page's result)
//   node native/tools/padfx-dump.mjs --golden   writes native/tests/golden/padfx_golden.inc (FNV-1a 64 of every case's output)
//   node native/tools/padfx-dump.mjs --check    exits 1 if the committed padfx_golden.inc differs
// Reference platform: x86-64 Linux, Node 22 (Math.* rounding), as every other golden.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url)), ROOT = path.resolve(HERE, '../..');
const require = createRequire(import.meta.url);
const SR = 48000;
const rd = f => fs.readFileSync(path.join(ROOT, f));

const SKIP = new Set(['degrade', 'reson', 'pdelay', 'preverb']);   // the four JS cores have their own unit (U17)

function loadEngine() {
  const html = rd('index.html').toString('utf8').replace(/\r\n/g, '\n');
  const A = html.indexOf('/* ===ENGINE START=== */'), B = html.indexOf('/* ===ENGINE END=== */');
  const ctx = vm.createContext({ console: { log() {}, warn() {}, error() {} }, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object, JSON, Map, Set, WeakMap, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
  ctx.globalThis = ctx; ctx.self = ctx;
  // the shared MODFX modules, as the page's pad path reads them (WavesDSP.MODULES[k]): a fresh seed counter per engine
  const H = require(path.join(ROOT, 'tests/helpers.js'));
  const cctx = H.freezeClock(H.workletScope());
  vm.runInContext(rd('chains/chains-worklet.js').toString('utf8'), cctx, { filename: 'chains/chains-worklet.js' });
  ctx.WavesDSP = { MODULES: cctx.ChainsDSP.MODULES };
  vm.runInContext(html.slice(A, B) + '\n;globalThis.__E = {renderVoice, padFx, applyFx, mkVoice, freshLayer, freshFx, FX, setBpm: b => { globalThis.S = { bpm: b }; }};', ctx, { filename: 'index.html(engine)' });
  const z = () => 0, imp = { wasi_snapshot_preview1: { fd_close: z, fd_seek: z, fd_write: z, proc_exit: z } };
  const tx = new WebAssembly.Instance(new WebAssembly.Module(rd('chains/dsp/tfx.wasm')), imp); if (tx.exports._initialize) tx.exports._initialize();
  const pl = new WebAssembly.Instance(new WebAssembly.Module(rd('waves/dsp/thunder-dsp.wasm')), imp); if (pl.exports._initialize) pl.exports._initialize();
  ctx.__tx = tx; ctx.__pl = pl;
  vm.runInContext('TX = globalThis.__tx; PL = globalThis.__pl;', ctx);
  ctx.__E.ctx = ctx; return ctx.__E;
}

const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };
// the sound: a decaying noise-and-tone hit, then silence (the tail the effects ring into)
function sound(n, hit, seed) {
  const r = lcg(seed), a = new Float32Array(n);
  for (let i = 0; i < hit; i++) { const e = Math.exp(-i / (hit / 4)); a[i] = Math.fround(((r() * 2 - 1) * 0.35 + Math.sin(2 * Math.PI * 220 * i / SR) * 0.5) * e); }
  return a;
}
function freshWith(E, type, alt, seed) {
  const f = E.freshFx(type), d = E.FX[type];
  if (alt) { const r = lcg(seed * 7919 + type.length);
    for (const k in d.rows) { const o = d.rows[k]; let v = o.min + r() * (o.max - o.min); if (o.c === 'int') v = Math.round(v); f[k] = v; }
    for (const [k, , opts] of d.segs) { const o = opts[Math.floor(r() * opts.length)]; f[k] = o[0]; } }
  return f;
}
const u8 = a => new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
export const fnv = a => { let h = 0xcbf29ce484222325n; for (const b of u8(a)) { h ^= BigInt(b); h = (h * 0x100000001b3n) & 0xffffffffffffffffn; } return h; };

function cases(E) {
  const out = [], bpm = 120, N = 30000, HIT = 12000;
  const types = Object.keys(E.FX).filter(k => !SKIP.has(k));
  let idx = 0;
  for (const t of types) for (const alt of [false, true]) {
    const f = freshWith(E, t, alt, ++idx);
    out.push({ name: `fx_${t}_${alt ? 'alt' : 'def'}`, kind: 'fx', fx: f, sr: SR, bpm, n: N, hit: HIT, seed: 100 + idx });
  }
  // the whole pad path: padFx (order, trim, fade) over a chain of library effects
  const chain = (name, list) => out.push({ name, kind: 'pad', fxs: list.map(x => freshWith(E, x[0], x[1], ++idx)), sr: SR, bpm, n: N, hit: HIT, tail: 0.5, seed: 7 });
  chain('pad_crush', [['crush', true]]);
  chain('pad_crush_room', [['crush', false], ['room', false]]);
  chain('pad_spring_eq', [['spring', false], ['eq', true]]);
  chain('pad_three', [['odrive', true], ['plate', false], ['tape', true]]);
  // whole voices through renderVoice: layers + pad effects (the order, the tail, the trim and the fade of the real render)
  const voice = (name, layers, fxs, over) => out.push({ name, kind: 'voice', v: { layers, fxs: fxs.map(x => freshWith(E, x[0], x[1], ++idx)), over: over || {} }, sr: SR, bpm });
  const L1 = { on: true, type: 'saw', level: 0.7, a: 0, b: 0, tune: 0, dec: 250, seed: 11, lpCut: 6000 }, L2 = { on: true, type: 'sine', level: 0.8, tune: -12, dec: 400, seed: 12 };
  voice('voice_crush', [L1, L2], [['crush', true]], { len: 0.5, tail: 0.3 });
  voice('voice_spring', [L1], [['spring', false]], { len: 0.3, tail: 1.0 });
  voice('voice_tape_room', [L1, L2], [['tape', false], ['room', true]], { len: 0.4, tail: 0.6 });
  voice('voice_off', [L1], [['crush', false]], { len: 0.3, tail: 0.2 });
  // ELECTRO's own pads that carry pad effects (the saved voice object as the song holds it)
  try { const song = JSON.parse(rd('sounds/ELECTRO.thunder').toString('utf8')), vs = (song.kit && song.kit.voices) || [];
    vs.forEach((v, i) => { if (v && v.fx && v.fx.length) out.push({ name: `electro_v${i}_${v.engine || 'layers'}`, kind: 'electro', saved: v, sr: SR, bpm: (song.song && song.song.bpm) || 120 }); }); } catch (e) {}
  return out;
}

function run(E, c) {
  E.setBpm(c.bpm);
  if (c.kind === 'electro') {
    const V = JSON.parse(JSON.stringify(c.saved)); c.voice = c.saved;
    return { inp: new Float32Array(0), out: E.renderVoice(V, c.sr, null, 0) };
  }
  if (c.kind === 'voice') {
    const layers = c.v.layers.map(l => E.freshLayer ? E.freshLayer(l) : l), V = E.mkVoice('x', layers, JSON.parse(JSON.stringify(c.v.over)));
    V.fx = c.v.fxs.map(f => JSON.parse(JSON.stringify(f)));
    if (c.name === 'voice_off') V.fx[0].on = false;
    c.voice = JSON.parse(JSON.stringify(V));
    return { inp: new Float32Array(0), out: E.renderVoice(V, c.sr, null, 0) };
  }
  const a = sound(c.n, c.hit, c.seed);
  if (c.kind === 'fx') return { inp: a, out: E.applyFx(JSON.parse(JSON.stringify(c.fx)), Float32Array.from(a), c.sr) };
  const V = { fx: c.fxs.map(f => JSON.parse(JSON.stringify(f))), tail: c.tail };
  return { inp: a, out: E.padFx(V, Float32Array.from(a), c.sr, null) };
}

const arg = process.argv[2];
const E0 = loadEngine(), cs = cases(E0), rows = [], unstable = [];
// every case on a FRESH engine (a new wasm instance and seed counter): the page's effects keep state in the instance (statics, the module seed counter), so a case after others would depend on them
for (const c of cs) {
  const r = run(loadEngine(), c);
  { const r2 = run(loadEngine(), c); if (fnv(r.out) !== fnv(r2.out)) unstable.push(c.name); }
  rows.push({ c, r });
}
// the rows' ranges of the tx / density / tape / chorus / crush effects (FX[type].rows min / max, a choice's lowest / highest option): padfx_ranges.inc, the hardening clamp of engine/padfx.cpp
const RANGES = path.join(ROOT, 'native/core/src/engine/padfx_ranges.inc');
const rangesInc = () => { const L = ['// GENERATED by native/tools/padfx-dump.mjs (--ranges) from the FX table of index.html. Do not edit.', 'struct PadRange { const char* type; const char* key; double mn, mx; };', 'static const PadRange kPadRanges[] = {'];
  for (const t of Object.keys(E0.FX)) { const d = E0.FX[t]; if (SKIP.has(t) || d.mod) continue;
    for (const k in d.rows) L.push(`  {"${t}", "${k}", ${d.rows[k].min}, ${d.rows[k].max}},`);
    for (const [k, , opts] of d.segs) { const v = opts.map(o => +o[0]); L.push(`  {"${t}", "${k}", ${Math.min(...v)}, ${Math.max(...v)}},`); } }
  L.push('};', ''); return L.join('\n'); };
const GOLD = path.join(ROOT, 'native/tests/golden/padfx_golden.inc');
const inc = () => '// GENERATED by native/tools/padfx-dump.mjs (--golden): FNV-1a 64 of the page\'s own result for each case (applyFx / padFx run in Node 22, x86-64 Linux, tfx.wasm, thunder-dsp.wasm, the shared MODFX modules). Do not edit.\n'
  + 'struct PadFxGolden { const char* name; unsigned long long hash; int floats; };\nstatic const PadFxGolden kPadFxGolden[] = {\n'
  + rows.map(({ c, r }) => `  {"${c.name}", 0x${fnv(r.out).toString(16).padStart(16, '0')}ull, ${r.out.length}},`).join('\n') + '\n};\nstatic const int kPadFxGoldenN = ' + rows.length + ';\n';
if (unstable.length) console.error('padfx-dump: the page gives different output on two runs for: ' + unstable.join(', '));
if (arg === '--ranges') { fs.writeFileSync(RANGES, rangesInc()); console.log('wrote ' + RANGES); }
else if (arg === '--golden') { fs.writeFileSync(GOLD, inc()); console.log('wrote ' + GOLD + ' (' + rows.length + ' cases)'); }
else if (arg === '--check') { const cur = fs.existsSync(GOLD) ? fs.readFileSync(GOLD, 'utf8') : ''; if (cur !== inc() || !fs.existsSync(RANGES) || fs.readFileSync(RANGES, 'utf8') !== rangesInc()) { console.error('padfx_golden.inc differs from what the page gives now'); process.exit(1); } console.log('padfx golden is current (' + rows.length + ' cases)'); }
else {
  const dir = arg || '/tmp/padfx-ref'; fs.mkdirSync(dir, { recursive: true });
  for (const { c, r } of rows) { fs.writeFileSync(path.join(dir, c.name + '.json'), JSON.stringify(c)); fs.writeFileSync(path.join(dir, c.name + '.in.f32'), u8(r.inp)); fs.writeFileSync(path.join(dir, c.name + '.f32'), u8(r.out)); }
  console.log('wrote ' + rows.length + ' cases to ' + dir);
}
