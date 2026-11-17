// U25: THE REFERENCE OF THE PLAITS / ELEMENTS LAYER TYPES ("plaits" = Macro osc, "elements" = Resonator). The real ENGINE block of index.html (plaitsPre, elementsPre, makeOsc, renderLayer, renderVoice)
// run in Node's vm, with PL = the web's own thunder-dsp.wasm (waves/dsp/thunder-dsp.wasm, built by dsp/build.sh with -O3 -ffast-math), exactly as the page runs it: renderVoice(v, 48000, null, hold).
// A FRESH INSTANCE of the wasm per rendered voice (the page keeps one whose statics go on from render to render; the native core starts every voice zeroed: docs/port/U25-NOTES.md).
//   node native/tools/engine-u25-dump.mjs [outDir] [--wasm FILE]
//        writes <outDir>/cases.json (every case: name, kind, sample rate, hold, bpm, the slim voice) and <outDir>/ref.json (sha256 + length of the reference render of each) and <outDir>/<name>.f32 (its samples):
//        the gate of test_engine_u25 is a tolerance against these samples (-100 dB of the reference's peak), the cases that are bit-exact stay bit-exact, and the native strict render is pinned by committed hashes.
// The wasm: --wasm FILE, else $U25_WASM, else <repo>/waves/dsp/thunder-dsp.wasm (the file the app ships). CI also runs the gate against the wasm the CMake build makes from the pins ($PIN_WASM_DIR/thunder-dsp.wasm).
// Cases of kind 'refused' (knobs outside the sliders' ranges: the library reads outside its tables there) are not rendered; the C++ test expects kEngUnsupported.
// NO FACTORY VOICE of tests/golden/engines.json (1244 rows) or of sounds/ELECTRO.thunder has a plaits or elements layer (tools/engine-golden.js leaves both types out: NEEDS_DATA_TYPES), so there is no beta hash to
// reuse. The "factory" cases here are what the app's own layer picker makes of the two types: a layer switched to Macro osc / Resonator (index.html's type switch: AHD, atk 0.5, hold 0, dec >= 3000 / 4000,
// no pitch envelope, Tune +12), once for each of the 24 Plaits models and each of the 3 Elements models, struck and (Plaits) as a drone.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '../..');
const SR = 48000;
const sha = a => crypto.createHash('sha256').update(Buffer.from(a.buffer, a.byteOffset, a.byteLength)).digest('hex');
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

export function engineOf(root) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
  const A = html.indexOf('/* ===ENGINE START=== */'), B = html.indexOf('/* ===ENGINE END=== */');
  if (A < 0 || B < 0) throw new Error('engine markers not found');
  const ctx = vm.createContext({ console: { log() {}, warn() {}, error() {} }, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object, JSON, Map, Set, WeakMap, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
  ctx.globalThis = ctx; ctx.self = ctx;
  vm.runInContext(html.slice(A, B) + '\n;let __hh = null; { const p0 = plaitsPre, e0 = elementsPre; plaitsPre = (...a) => { const x = p0(...a); if (__hh) __hh(x); return x; }; elementsPre = (...a) => { const x = e0(...a); if (__hh) __hh(x); return x; }; }\nglobalThis.__E = {renderVoice, mkVoice, freshLayer, PL_NAMES, setPL: x => { PL = x; }, hookPre: h => { __hh = h; }, setBpm: b => { globalThis.S = { bpm: b }; }, setSeed: n => { _seed = n >>> 0; }};', ctx, { filename: 'index.html(engine)' });
  return ctx.__E;
}
function loadWasm(file) {
  const mod = new WebAssembly.Module(fs.readFileSync(file)), z = () => 0, imp = { wasi_snapshot_preview1: { fd_close: z, fd_seek: z, fd_write: z, proc_exit: z } };
  return () => { const i = new WebAssembly.Instance(mod, imp); if (i.exports._initialize) i.exports._initialize(); return { exports: i.exports }; };
}

function slim(v, D) {
  const out = { v: {}, L: [], fx: [] };
  for (const k of Object.keys(v)) if (k !== 'layers' && k !== 'fx' && k !== 'name' && !same(v[k], D.voice[k])) out.v[k] = v[k];
  for (const l of v.layers) if (l.on && l.level > 0) { const o = {}; for (const k of Object.keys(l)) if (k === 'seed' || k === 'on' || !same(l[k], D.layer[k])) o[k] = l[k]; out.L.push(o); }
  return out;
}
function defaultsOf(E) { const v = E.mkVoice('x', [], {}), l = E.freshLayer(); delete v.layers; return { voice: JSON.parse(JSON.stringify(v)), layer: JSON.parse(JSON.stringify(l)) }; }
function build(E, s, name) { const layers = s.L.map(l => ({ ...l })); const v = E.mkVoice(name, layers, JSON.parse(JSON.stringify(s.v))); v.fx = []; return v; }

const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };
export function allCases(E) {
  const D = defaultsOf(E), cases = [];
  const add = (name, kind, layers, o = {}, sr = SR, hold = 0) => { const v = E.mkVoice(name, layers, o); v.fx = []; cases.push({ name, kind, sr, hold, bpm: 120, ...slim(v, D) }); };
  const base = (type, over) => ({ type, on: true, seed: 987654321, ...over });
  // 1. the layer picker's own layers: type switch defaults, one per model; Elements: dec >= 4000 (index.html 9489), Plaits: dec >= 3000
  for (let k = 0; k < 24; k++) {
    const nm = E.PL_NAMES[k].replace(/[^A-Za-z0-9]+/g, '_');
    add('fv_plaits_' + String(k).padStart(2, '0') + '_' + nm, 'voice', [base('plaits', { pEng: k, mode: 'AHD', atk: 0.5, hold: 0, dec: 3000, pAmt: 0, tune: 12 })], { len: 2 });
    add('fvd_plaits_' + String(k).padStart(2, '0') + '_' + nm, 'voice', [base('plaits', { pEng: k, pStrike: false, mode: 'ADSR', atk: 5, sus: 0.8, rel: 300, tune: 12 })], { len: 1.5 }, SR, 0.6);
  }
  for (let m = 0; m < 3; m++) {
    add('fv_elements_' + m, 'voice', [base('elements', { eModel: m, mode: 'AHD', atk: 0.5, hold: 0, dec: 4000, pAmt: 0, tune: 12 })], { len: 2 });
    add('fvb_elements_' + m, 'voice', [base('elements', { eModel: m, eBow: 0.7, eBlow: 0.4, eStrike: 0.3, eStrength: 0.6, eContour: 0.5, mode: 'AHD', dec: 4000, tune: 12 })], { len: 2 }, SR, 0);
  }
  add('fv_layertype_plaits', 'voice', [base('plaits', {})], { len: 2 });          // the golden's skipped "layertype|plaits" at its defaults (PD('type plaits', [{type}]))
  add('fv_layertype_elements', 'voice', [base('elements', {})], { len: 2 });
  // 2. random voices: every parameter range, sample rates, filters, pitch envelopes, bursts, delays, the matrix, several Plaits / Elements layers in one voice (they share the one wasm instance)
  const r = lcg(2525), pick = a => a[Math.floor(r() * a.length)], ur = (lo, hi) => lo + r() * (hi - lo), ui = (lo, hi) => Math.floor(ur(lo, hi + 1));
  const kn = () => r() < 0.15 ? pick([0, 1, 0.5]) : r();
  const layer = (type, i) => {
    const adsr = r() < 0.4;
    const o = { type, on: true, level: ur(0.2, 1), tune: ui(-12, 36), fine: ui(-60, 60), pAmt: r() < 0.5 ? 0 : ui(-24, 30), pDec: ur(5, 300), mode: adsr ? 'ADSR' : 'AHD', atk: ur(0.1, 40), hold: r() < 0.3 ? ur(0, 80) : 0,
      dec: ur(100, 2500), sus: r(), rel: ur(5, 400), lpCut: r() < 0.6 ? 20000 : ur(200, 12000), lpRes: r() < 0.3 ? r() : 0, lpEnv: r() < 0.3 ? ur(-1, 1) : 0, hpCut: r() < 0.6 ? 20 : ur(30, 4000), hpRes: r() < 0.2 ? r() : 0,
      hpEnv: r() < 0.2 ? ur(-1, 1) : 0, fAtk: ur(0.1, 30), fDec: ur(10, 400), delay: r() < 0.2 ? ur(0, 40) : 0, burst: r() < 0.2 ? ui(1, 3) : 0, spacing: ur(4, 20), seed: ui(1, 4294967295) };
    if (type === 'plaits') Object.assign(o, { pEng: i < 48 ? (i >> 1) % 24 : ui(0, 23), pHarm: kn(), pTim: kn(), pMor: kn(), pDecay: kn(), pLpg: kn(), pFm: r() < 0.5 ? 0 : ur(-1, 1), pTimEnv: r() < 0.5 ? 0 : ur(-1, 1), pMorEnv: r() < 0.5 ? 0 : ur(-1, 1), pStrike: r() < 0.65, pAux: r() < 0.25 });
    else Object.assign(o, { eModel: ui(0, 2), eStrength: kn(), eContour: kn(), eBow: kn(), eBlow: kn(), eStrike: kn(), eBowT: kn(), eFlow: kn(), eBlowT: kn(), eMallet: kn(), eStrikeT: kn(), eGeo: kn(), eBright: kn(), eDamp: kn(), ePos: kn(), eSpace: kn() });
    return o;
  };
  for (let i = 0; i < 150; i++) {
    const sr = i % 8 === 7 ? 44100 : i % 11 === 10 ? 96000 : i % 13 === 12 ? 32000 : 48000, n = i < 96 ? 1 : ui(1, 3);
    const layers = Array.from({ length: n }, (_, k) => layer(i < 48 ? 'plaits' : i < 66 ? 'elements' : pick(['plaits', 'plaits', 'elements']), i + k));
    const o = { level: ur(-12, 6), tune: r() < 0.5 ? 0 : ui(-12, 24), drive: r() < 0.3 ? r() : 0, gate: ur(50, 500), len: ur(0.2, 1.4), tail: ur(0, 0.5) };
    if (r() < 0.3) Object.assign(o, { gLpCut: ur(300, 15000), gLpRes: r() < 0.5 ? r() : 0, gLpEnv: r() < 0.5 ? ur(-1, 1) : 0 });
    if (r() < 0.3) Object.assign(o, { gAmpOn: true, gMode: r() < 0.5 ? 'ADSR' : 'AHD', gAtk: ur(0.1, 30), gDec: ur(20, 400), gSus: r(), gRel: ur(5, 300) });
    if (r() < 0.3) Object.assign(o, { gPAmt: ui(-24, 24), gPDec: ur(5, 300) });
    if (r() < 0.5) for (let k = 1; k <= 3; k++) if (r() < 0.6) Object.assign(o, { ['lfo' + k + 'Rate']: ur(0.2, 20), ['lfo' + k + 'Shape']: ui(0, 5), ['lfo' + k + 'Depth']: r(), ['r' + k + 'dst']: ui(0, 4), ['r' + k + 'amt']: ur(-12, 12) });
    add('u25case' + String(i).padStart(3, '0'), 'case', layers, o, sr, adsr(layers) || r() < 0.2 ? ur(0.05, 0.5) : 0);
  }
  function adsr(layers) { return layers.some(l => l.mode === 'ADSR'); }
  // 3. written-out edge cases
  const L = (type, over = {}) => base(type, { atk: 1, dec: 400, ...over });
  for (let k = 0; k < 24; k++) for (const [h, t, m] of [[0, 0, 0], [1, 1, 1], [0.5, 0.5, 0.5]]) add('edge_pl' + k + '_' + h * 10 + t * 10 + m * 10, 'case', [L('plaits', { pEng: k, pHarm: h, pTim: t, pMor: m })], { len: 0.35 });
  for (let m = 0; m < 3; m++) for (const [a, b] of [[0, 0], [1, 1], [0.5, 0.5], [1, 0]]) add('edge_el' + m + '_' + a * 10 + b * 10, 'case', [L('elements', { eModel: m, eStrength: a, eContour: b, eBow: a, eBlow: b, eStrike: 1 - a, eGeo: a, eBright: b, eDamp: a, ePos: b, eSpace: a })], { len: 0.35 });
  add('edge_pl_hi', 'case', [L('plaits', { pEng: 8, tune: 70 })], { len: 0.3 }); add('edge_pl_lo', 'case', [L('plaits', { pEng: 8, tune: -60, pStrike: false })], { len: 0.5 });
  add('edge_el_hi', 'case', [L('elements', { eModel: 0, tune: 70 })], { len: 0.3 }); add('edge_el_lo', 'case', [L('elements', { eModel: 1, tune: -60 })], { len: 0.5 });
  add('edge_pl_22k', 'case', [L('plaits', { pEng: 14, tune: 12 })], { len: 0.3 }, 22050); add('edge_pl_96k', 'case', [L('plaits', { pEng: 14, tune: 12 })], { len: 0.3 }, 96000);
  add('edge_el_22k', 'case', [L('elements', { eModel: 0, tune: 12 })], { len: 0.3 }, 22050); add('edge_el_96k', 'case', [L('elements', { eModel: 2, tune: 12 })], { len: 0.3 }, 96000);
  // a long layer (the pre-render is capped at 8 s: the layer is silent after it) and a very short one
  add('edge_pl_long', 'case', [L('plaits', { pEng: 8, pStrike: false, mode: 'ADSR', rel: 200 })], { len: 9.5, gate: 9000 }, SR, 9);
  add('edge_el_long', 'case', [L('elements', { eModel: 0, mode: 'ADSR', rel: 200 })], { len: 9.5, gate: 9000 }, SR, 9);
  add('edge_pl_short', 'case', [L('plaits', { pEng: 8, atk: 0.1, dec: 1 })], { len: 0.02 }); add('edge_el_short', 'case', [L('elements', { eModel: 0, atk: 0.1, dec: 1 })], { len: 0.02 });
  // knobs outside 0..1 and the other numbers a song file can hold
  add('edge_pl_over', 'refused', [L('plaits', { pEng: 8, pHarm: 1.6, pTim: 1.4, pMor: 1.5, pDecay: 1.3, pLpg: 1.5, pFm: 1.5, pTimEnv: 1.5, pMorEnv: 1.5 })], { len: 0.3 });
  add('edge_pl_under', 'refused', [L('plaits', { pEng: 8, pHarm: -0.6, pTim: -0.4, pMor: -0.5, pDecay: -0.3, pLpg: -0.5, pFm: -1.5, pTimEnv: -1.5, pMorEnv: -1.5 })], { len: 0.3 });
  add('edge_pl_engine_over', 'case', [L('plaits', { pEng: 40 })], { len: 0.3 }); add('edge_pl_engine_under', 'case', [L('plaits', { pEng: -3 })], { len: 0.3 }); add('edge_pl_engine_frac', 'case', [L('plaits', { pEng: 12.9 })], { len: 0.3 });
  add('edge_el_over', 'refused', [L('elements', { eModel: 1, eStrength: 1.6, eContour: 1.4, eBow: 1.5, eBlow: 1.3, eStrike: 1.5, eBowT: 1.5, eFlow: 1.5, eBlowT: 1.4, eMallet: 1.5, eStrikeT: 1.5, eGeo: 1.5, eBright: 1.5, eDamp: 1.5, ePos: 1.5, eSpace: 1.5 })], { len: 0.3 });
  add('edge_el_under', 'refused', [L('elements', { eModel: 1, eStrength: -0.6, eContour: -0.4, eBow: -0.5, eBlow: -0.3, eStrike: -0.5, eBowT: -0.5, eFlow: -0.5, eBlowT: -0.4, eMallet: -0.5, eStrikeT: -0.5, eGeo: -0.5, eBright: -0.5, eDamp: -0.5, ePos: -0.5, eSpace: -0.5 })], { len: 0.3 });
  add('edge_el_model_frac', 'case', [L('elements', { eModel: 1.9 })], { len: 0.3 });
  // a pitch envelope and a layer filter through the pre-render; bursts and a delay; several layers of both types in one voice (one shared instance: their random state and Plaits' RAM go on from layer to layer)
  add('edge_pl_penv', 'case', [L('plaits', { pEng: 20, pAmt: 24, pDec: 80, lpCut: 3000, lpEnv: 0.5 })], { len: 0.5 }); add('edge_el_penv', 'case', [L('elements', { eModel: 2, pAmt: -12, pDec: 120, hpCut: 400 })], { len: 0.5 });
  add('edge_pl_burst', 'case', [L('plaits', { pEng: 21, burst: 3, spacing: 12, delay: 15 })], { len: 0.5 });
  add('edge_stack_two_pl', 'case', [L('plaits', { pEng: 18, tune: 12 }), L('plaits', { pEng: 16, tune: 7, delay: 10 })], { len: 0.6 });
  add('edge_stack_noise', 'case', [L('plaits', { pEng: 17, pStrike: false }), L('plaits', { pEng: 18 }), L('plaits', { pEng: 23 }), L('elements', { eModel: 0, eBlow: 0.8 })], { len: 0.8 });
  add('edge_stack_mixed', 'case', [L('elements', { eModel: 1, eBow: 0.6 }), L('plaits', { pEng: 19 }), L('elements', { eModel: 0, tune: 7 }), L('plaits', { pEng: 20 }), L('sine', { a: 0.1, level: 0.3 })], { len: 0.6 });
  add('edge_with_sine', 'case', [L('plaits', { pEng: 5 }), L('saw', { a: 0.2, level: 0.5 })], { len: 0.4, drive: 0.5 });
  return cases;
}
export function renderCase(E, make, c) {
  const v = build(E, c, c.name); E.setBpm(c.bpm); E.setPL(make());
  try { return E.renderVoice(v, c.sr, null, c.hold); } finally { E.setBpm(120); }
}
function main(argv) {
  const wi = argv.indexOf('--wasm'); const wasm = wi >= 0 ? argv[wi + 1] : (process.env.U25_WASM || path.join(ROOT, 'waves/dsp/thunder-dsp.wasm'));
  const rest = argv.filter((a, i) => wi < 0 || (i !== wi && i !== wi + 1)); const outDir = rest[0] || '/tmp/u25/engine-ref';
  fs.mkdirSync(outDir, { recursive: true });
  const E = engineOf(ROOT), make = loadWasm(wasm), cases = allCases(E), rows = [];
  for (const c of cases) {
    if (c.kind === 'refused') continue;   // the native core refuses these (a knob outside the sliders' range): no reference
    const pre = []; E.hookPre(x => { if (x) pre.push(x.data); return x; });   // every pre-render the voice's layers asked for, in order
    const r = renderCase(E, make, c);
    fs.writeFileSync(path.join(outDir, c.name + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength));
    pre.forEach((d, j) => fs.writeFileSync(path.join(outDir, c.name + '.pre' + j + '.f32'), Buffer.from(d.buffer, d.byteOffset, d.byteLength)));
    rows.push({ name: c.name, sha256: sha(r), len: r.length, pre: pre.map(d => d.length) });
  }
  fs.writeFileSync(path.join(outDir, 'ref.json'), JSON.stringify({ wasm: path.basename(wasm), cases: rows }));
  fs.writeFileSync(path.join(outDir, 'cases.json'), JSON.stringify({ cases }));
  console.log('wrote ' + rows.length + ' cases (' + cases.filter(c => c.kind === 'voice').length + ' factory-style voices) to ' + outDir + ' from ' + wasm);
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
