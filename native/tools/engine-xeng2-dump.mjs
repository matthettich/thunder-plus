// U29: THE REFERENCE OF THE SECOND SET OF RENDERED-ONLY ENGINES table (with a loaded wavetable V.wtData), vector, scanned, resynth (with the analysis V.rsData) and bytebeat (the formula V.bbExpr) (exact, Node only): the real ENGINE block of
// index.html run in Node's vm, on (1) every factory voice of beta's tests/golden/engines.json whose engine is one of those, fingerprinted as beta's engines.json fingerprints them (SHA-256 of the float32 bytes of
// renderVoice(v, 48000, null, hold)), and (2) direct cases: random parameters of every engine (the ranges of the engine cards, XUI), loaded wavetables made by TableCore.fromCycles, resynth analyses made by ResynthCore.analyse of
// test sounds, formulas good and bad, the clamps, other sample rates, tempos, hold, the pad effects, through the real renderVoice.
//   node native/tools/engine-xeng2-dump.mjs --fixture <betaRoot>   (by hand, needs a checkout of `beta`) writes native/tests/golden/engine_xeng2_voices.json (the voices: only what differs from mkVoice's defaults, and the engine's parameter
//        object; the analysis of a Resynth voice is kept once in `blobs` and named by {"$blob": n}), engine_xeng2_voices_golden.inc (beta's sha256 and length of each, copied, never made here) and xeng2_ranges.json (min / max / curve / default of every parameter: XUI).
//   node native/tools/engine-xeng2-dump.mjs [outDir]               the local Node's reference: <outDir>/ref.json (the voices' hashes by this repo's ENGINE block), <outDir>/cases.json (the direct cases), with --samples
//        <outDir>/u<n>.f32 and <outDir>/c<n>.f32 (the renders, to find the first differing sample).
//   node native/tools/engine-xeng2-dump.mjs --golden               writes native/tests/golden/engine_xeng2_golden.inc (the direct cases' hashes: sha256 of the real JS's render, Node 22 x86-64 Linux).
//   node native/tools/engine-xeng2-dump.mjs --check                exits 1 if the local Node's renders do not give the committed hashes (beta's for the voices, engine_xeng2_golden.inc for the cases). Linux only gate.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../..');
const SR = 48000;
const KINDS = ['table', 'vector', 'scanned', 'resynth', 'bytebeat'];
const KSET = new Set(KINDS);
const GOLD = path.join(ROOT, 'native/tests/golden');
const sha = a => crypto.createHash('sha256').update(Buffer.from(a.buffer, a.byteOffset, a.byteLength)).digest('hex');
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// The ENGINE block as a standalone script (as engine-dump.mjs's engineOf, with the XENG table and the engines' own classes exported too).
function engineOf(root) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
  const A = html.indexOf('/* ===ENGINE START=== */'), B = html.indexOf('/* ===ENGINE END=== */');
  if (A < 0 || B < 0) throw new Error('engine markers not found');
  const ctx = vm.createContext({ console: { log() {}, warn() {}, error() {} }, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object, JSON, Map, Set, WeakMap, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
  ctx.globalThis = ctx; ctx.self = ctx;
  vm.runInContext(html.slice(A, B) + '\n;globalThis.__E = {renderVoice, mkVoice, freshFx, FX, XENG, TableCore, ResynthCore, BytebeatCore, SpecUtil, KeysUtil, setBpm: b => { globalThis.S = { bpm: b }; }};', ctx, { filename: 'index.html(engine)' });
  return ctx.__E;
}
const defaultsOf = E => { const v = E.mkVoice('x', [], {}); delete v.layers; return JSON.parse(JSON.stringify(v)); };
// a voice as the fixture keeps it: what differs from mkVoice's defaults (the layers are not read by an engine voice), the pad effects. The analysis of a Resynth voice (tens of KB, the same for most) is kept once in BLOBS and named by {"$blob": n}.
const BLOBS = [];
function slim(v, D) {
  const out = { v: {}, fx: (v.fx || []).map(f => ({ ...f })) };
  for (const k of Object.keys(v)) if (k !== 'layers' && k !== 'fx' && k !== 'name' && !same(v[k], D[k])) out.v[k] = JSON.parse(JSON.stringify(v[k]));
  if (out.v.rsData) { const j = JSON.stringify(out.v.rsData); let i = BLOBS.indexOf(j); if (i < 0) { i = BLOBS.length; BLOBS.push(j); } out.v.rsData = { $blob: i }; }
  return out;
}
const expand = (v, blobs) => { const o = JSON.parse(JSON.stringify(v)); if (o.rsData && o.rsData.$blob !== undefined) o.rsData = JSON.parse(JSON.stringify(blobs[o.rsData.$blob])); return o; };
const build = (E, s, name, blobs) => { const v = E.mkVoice(name, [], expand(s.v, blobs || [])); v.fx = s.fx.map(f => ({ ...f })); return v; };

function fixture(betaRoot) {
  const G = createRequire(import.meta.url)(path.join(betaRoot, 'tools/engine-golden.js'));
  const gold = JSON.parse(fs.readFileSync(path.join(betaRoot, 'tests/golden/engines.json'), 'utf8'));
  const E = engineOf(betaRoot), D = defaultsOf(E), voices = [], rows = [], per = {};
  for (const it of G.listVoices()) {
    const w = gold.voices[it.key]; if (!w || w.error) continue;
    G.engine().setSeed(G.seedFor(it.key));
    const b = it.build(), v = b.v;
    if (!KSET.has(v.engine)) continue;
    const s = slim(v, D);
    const r = E.renderVoice(build(E, s, 'x', BLOBS.map(j => JSON.parse(j))), SR, null, b.hold);
    if (sha(r) !== w.sha256 || r.length !== w.len) throw new Error('fixture: the slim voice does not reproduce the golden: ' + it.key);
    voices.push({ k: it.key, hold: b.hold, ...s }); rows.push(`  {${JSON.stringify(it.key)}, ${JSON.stringify(w.sha256)}, ${w.len}},`); per[v.engine] = (per[v.engine] || 0) + 1;
  }
  fs.writeFileSync(path.join(GOLD, 'engine_xeng2_voices.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-xeng2-dump.mjs --fixture from beta (' + gold.node + '): the factory voices whose engine is table, vector, scanned, resynth or bytebeat (a Resynth voice analysis is in blobs). Only what differs from mkVoice defaults (and the engine\'s parameter object).', sampleRate: SR, defaults: D, blobs: BLOBS.map(j => JSON.parse(j)), voices }) + '\n');
  fs.writeFileSync(path.join(GOLD, 'engine_xeng2_voices_golden.inc'), ['// GENERATED by native/tools/engine-xeng2-dump.mjs --fixture: tests/golden/engines.json of `beta` (made with Node ' + gold.node + ' on x86-64 Linux), the rows of the voices in engine_xeng2_voices.json, copied unchanged.',
    '// sha256 of the float32 bytes of renderVoice(v, 48000, null, hold), and its length in samples. The gate of test_engine_xeng on every platform.',
    'struct Xeng2Golden { const char* key; const char* sha256; long len; };', 'static const Xeng2Golden kXeng2Golden[] = {', ...rows, '};', 'static const int kXeng2GoldenN = ' + rows.length + ';', ''].join('\n'));
  // the engine cards' parameter ranges (XUI), for the direct cases
  const X = G.engine().XUI, ranges = {};
  for (const k of KINDS) { const d = E.XENG[k].d(); ranges[k] = {}; for (const p of Object.keys(d)) { if (p === 'tune' || p === 'drive' || p === 'level') continue; const r = X[k] && X[k].rows && X[k].rows[p]; ranges[k][p] = r ? { min: r.min, max: r.max, c: r.c, d: d[p] } : { d: d[p] }; } }
  fs.writeFileSync(path.join(GOLD, 'xeng2_ranges.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-xeng2-dump.mjs --fixture from beta: every engine parameter with the engine card\'s min / max / curve (XUI) and the default (XENG[k].d()). The direct cases draw from it.', ranges }, null, 1) + '\n');
  console.log('wrote ' + rows.length + ' voices ' + JSON.stringify(per));
}

// ---- the direct cases: made from a fixed seed (the same cases every time) -------------------------------------------------------------------------------------------------------------------
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };
// test sounds for Resynth's analyser and cycles for the wavetable loader (all from fixed seeds)
function testSounds(E) {
  const out = {}, r = lcg(77), RS = E.ResynthCore, mk = (sr, sec, fn) => { const n = Math.round(sr * sec), a = new Float32Array(n); for (let i = 0; i < n; i++) a[i] = fn(i / sr, i); return a; };
  out.pluck44 = RS.analyse(RS.demo(44100), 44100, { name: 'pluck 44k' });
  out.harm = RS.analyse(mk(48000, 1.2, (t) => { let y = 0; const vib = 1 + 0.0115 * Math.sin(2 * Math.PI * 5.5 * t); for (let k = 1; k <= 12; k++) y += Math.sin(2 * Math.PI * 220 * k * vib * t + k) / k * Math.exp(-t * (0.8 + 0.3 * k)); return y * 0.4; }), 48000, { name: 'harmonic vib' });
  out.bell = RS.analyse(mk(44100, 1.6, (t) => { let y = 0; [[1, 1.5, 1], [2.76, 1.1, 0.7], [5.4, 0.8, 0.55], [8.93, 0.5, 0.4], [13.34, 0.35, 0.3]].forEach(([q, d, a], k) => { y += a * Math.sin(2 * Math.PI * 330 * q * t + k) * Math.exp(-t / d); }); return y * 0.5; }), 44100, { name: 'bell' });
  out.noise = RS.analyse(mk(48000, 0.45, (t, i) => (r() * 2 - 1) * Math.exp(-t * 5) * 0.8 + 0.3 * Math.sin(2 * Math.PI * 1800 * t) * Math.exp(-t * 12)), 48000, { name: 'noise burst' });
  out.short = RS.analyse(mk(48000, 0.15, (t) => Math.sin(2 * Math.PI * 440 * t) * Math.exp(-t * 25) + (t < 0.002 ? 0.8 : 0)), 48000, { name: 'short ping' });
  out.glide = RS.analyse(mk(48000, 0.8, (t) => { const f = 200 + 125 * t; let ph = 200 * t + 62.5 * t * t, y = 0; for (let k = 1; k <= 6; k++) y += Math.sin(2 * Math.PI * k * ph) / k; return y * 0.3 * Math.min(1, t * 40) * Math.exp(-t * 1.2) + 0 * f; }), 48000, { name: 'glide saw' });
  out.small = RS.analyse(RS.demo(48000), 48000, { name: 'few partials', parts: 12, bytes: 9000 });
  out.forced = RS.analyse(mk(48000, 0.6, (t) => 0.5 * Math.sin(2 * Math.PI * 523 * t) * Math.exp(-t * 4) + 0.2 * (r() * 2 - 1) * Math.exp(-t * 9)), 48000, { name: 'forced unpitched', pitched: 0 });
  return out;
}
function wavetables(E) {
  const r = lcg(515), TC = E.TableCore, out = {}, fin = (wt, id) => { wt.id = id; return wt; };
  out.A = fin(TC.fromCycles(Array.from({ length: 8 }, (_, f) => { const c = new Float32Array(256); for (let i = 0; i < 256; i++) { let y = 0; for (let k = 1; k <= 3 + 2 * f; k++) y += Math.sin(2 * Math.PI * k * i / 256 + f) / Math.pow(k, 1 - f / 12); c[i] = y; } return c; }), 'frames A'), 'wtA');
  out.B = fin(TC.fromCycles([Float32Array.from({ length: 600 }, (_, i) => 2 * i / 600 - 1)], 'one saw'), 'wtB');
  out.C = fin(TC.fromCycles(Array.from({ length: 70 }, (_, f) => { const c = new Float32Array(128); let y = 0; for (let i = 0; i < 128; i++) { y = y * 0.7 + (r() * 2 - 1) * 0.3; c[i] = y + Math.sin(2 * Math.PI * (1 + (f % 5)) * i / 128); } return c; }), 'noisy 70'), 'wtC');
  out.D = fin(TC.fromCycles(Array.from({ length: 5 }, (_, f) => { const c = new Float32Array(2048); for (let i = 0; i < 2048; i++) c[i] = Math.sin(2 * Math.PI * i / 2048) + 0.5 * Math.sin(2 * Math.PI * (3 + f) * i / 2048) + 0.25 * Math.sin(2 * Math.PI * (7 * f + 1) * i / 2048); return c; }), 'long 5'), 'wtD');
  return out;
}
const FORMULAS = ['t*(t>>5|t>>8)', 't*((t>>12|t>>8)&63&t>>4)', '(t*5&t>>7)|(t*3&t>>10)', 'sin(t*3)*100+128', 'a*t&b', 't%c', 'rnd(t)>>2', '(t&t>>8)*c/128', 'tri(t>>2)*2', 'sqrt(t)*a', 'min(t&255,b)', 'max(t%a,c)', '-t', '~t&255',
  '!(t&64)*t', 't<<3>>5', 't>>>3', '(t==0)*255', 't&&t>>8', '0xFF&t', '0b1010101&t', 't>>10&1?t*2:t*3', 't*(42&t>>10)', 'abs(t*7-t*9)', 'floor(t/3)*3', 'int(t>>3)^t', 'cos(t)+sin(t>>2)', '(t*(t>>9|t>>13))&16|(t>>6)', 'random(t)&t', 't*3&t>>5|t>>4', 'a>b?t*a:t*b'];
const BAD = ['t*', 'x*t', 't.5', 'a(', 'q(1)', 't=1', '1/0', '5%0', 'sin()', '99999999999', '(((((((((((((((((((((((((((((((((((((((((t)))))))))))))))))))))))))))))))))))))))))', 't+'.repeat(300) + 't', '  ', 'té', '   ', ''];
function directCases(E) {
  const R = JSON.parse(fs.readFileSync(path.join(GOLD, 'xeng2_ranges.json'), 'utf8')).ranges, r = lcg(2911), cases = [], snd = testSounds(E), wts = wavetables(E);
  const pick = a => a[Math.floor(r() * a.length)], ur = (lo, hi) => lo + r() * (hi - lo), ui = (lo, hi) => Math.floor(ur(lo, hi + 1));
  const draw = q => { if (q.min === undefined) return q.d; const t = r(); if (q.c === 'log') return q.min * Math.pow(q.max / q.min, t); if (q.c === 'int') return Math.round(q.min + t * (q.max - q.min)); return q.min + t * (q.max - q.min); };
  const fxTypes = ['degrade', 'reson', 'pdelay', 'preverb'];
  const mkFx = type => { const f = E.freshFx(type); for (const k in E.FX[type].rows) { const w = E.FX[type].rows[k], t = r(); f[k] = w.c === 'logf' || w.c === 'log' ? w.min * Math.pow(w.max / w.min, t) : w.min + t * (w.max - w.min); if (['bits', 'note', 'chord', 'down', 'time'].includes(k) && r() < 0.5) f[k] = Math.round(f[k]); } if ('mode' in f) f.mode = ui(0, type === 'preverb' ? 2 : type === 'pdelay' ? 4 : 1); return f; };
  const add = (name, kind, params, o = {}, sr = SR, hold = 0, extra = {}, bpm = 120, fx = []) => cases.push({ name, kind, sr, hold, bpm, v: { engine: kind, [kind]: params, ...o, ...extra }, fx });
  const sndNames = Object.keys(snd), wtNames = Object.keys(wts);
  const PER = { table: 18, vector: 22, scanned: 16, resynth: 20, bytebeat: 18 };
  for (const k of KINDS) for (let i = 0; i < PER[k]; i++) {
    const params = {}; for (const p in R[k]) if (r() < 0.75) params[p] = draw(R[k][p]);
    const o = {}; if (r() < 0.5) o.level = ur(-12, 6); if (r() < 0.4) o.tune = ui(-12, 24); if (r() < 0.35) o.drive = r(); if (r() < 0.5) o.len = ur(0.15, 2.5); if (r() < 0.2) o.gate = ur(100, 900);
    const extra = {};
    if (k === 'table') { if (i % 2 === 0) params.table = 4; if (r() < 0.7) extra.wtData = JSON.parse(JSON.stringify(wts[wtNames[i % wtNames.length]])); }
    if (k === 'resynth') { if (i % 10 !== 9) extra.rsData = JSON.parse(JSON.stringify(snd[sndNames[i % sndNames.length]])); }
    if (k === 'bytebeat') extra.bbExpr = FORMULAS[(i * 7) % FORMULAS.length];
    const sr = i % 7 === 6 ? 44100 : i % 9 === 8 ? 32000 : SR, hold = i % 3 === 0 ? 0 : ur(0.1, 1.2), bpm = k === 'vector' ? pick([60, 90, 120, 128, 174, 240]) : 120;
    add(k + String(i).padStart(2, '0'), k, params, o, sr, hold, extra, bpm, i % 7 === 3 ? [mkFx(pick(fxTypes))] : []);
  }
  // edge cases, written out
  const e = (name, k, params, o, sr, hold, extra, bpm, fx) => add('edge_' + name, k, params, o, sr, hold, extra, bpm, fx);
  for (const k of KINDS) e(k + '_defaults', k, {}, {}, SR, 0);
  // table: the four built-in tables at the ends and middle of Position, warp, unison, a loaded table (and one that is not valid: Basic)
  for (let t = 0; t < 4; t++) for (const pos of [0, 0.5, 1]) e('table_t' + t + '_p' + pos, 'table', { table: t, pos, warp: pos * 0.8, unison: 1 + t * 2, det: 0.6, sub: 0.3 }, {}, SR, 0.5);
  e('table_move', 'table', { table: 1, pos: 0.1, pmod: 0.8, pdec: 200, pdepth: 0.6, prate: 3, noise: 0.2, unison: 7 }, {}, SR, 0.8); e('table_clamp', 'table', { table: 9.4, unison: 12, pos: 1, warp: 3, oct: 6 }, {}, SR, 0.4);
  for (const n of wtNames) e('table_wt' + n, 'table', { table: 4, pos: 0.37, pmod: 0.3, unison: 3, warp: 0.2 }, {}, SR, 0.6, { wtData: JSON.parse(JSON.stringify(wts[n])) });
  e('table_wt_oct0', 'table', { table: 4, oct: 0, pos: 0.9 }, {}, SR, 0.4, { wtData: JSON.parse(JSON.stringify(wts.A)) }); e('table_wt_hi', 'table', { table: 4, oct: 6, pos: 0.5 }, {}, 96000, 0.3, { wtData: JSON.parse(JSON.stringify(wts.D)) });
  e('table_loaded_none', 'table', { table: 4 }, {}, SR, 0.4); e('table_wt_invalid', 'table', { table: 4 }, {}, SR, 0.4, { wtData: { v: 2, nf: 4, h: 8, pk: 1, a: 'AAAA' } }); e('table_wt_short', 'table', { table: 4, pos: 0.5 }, {}, SR, 0.4, { wtData: { v: 1, id: 'short', nf: 6, h: 8, pk: 0.5, a: 'AAEAAgADAAQABQAG' } });
  e('table_wt_nopk', 'table', { table: 4, pos: 0.2 }, {}, SR, 0.4, { wtData: { v: 1, id: 'nopk', nf: 2, h: 8, a: SU(E, [3000, 0, 0, 2000, 0, 0, 1000, 0, 0, 500, 0, 0, 300, 0, 0, 200, 0, 0, 100, 0, 0, 50, 0, 0, 3000, 0, 0, 2000, 0, 0, 1000, 0, 0, 500, 0, 0, 300, 0, 0, 200, 0, 0, 100, 0, 0, 50]) } });
  // vector: every wave in the first corner, the four corners, the law, the envelope in each loop mode, the LFO, unison, the wave sequence in every division, the loop point and the gate / filter pulses
  for (let w = 0; w < 18; w++) e('vector_wave' + w, 'vector', { wa: w, wb: w, wc: w, wd: w, oct: 1 + (w % 4) }, {}, SR, 0.4);
  e('vector_corners', 'vector', { wa: 2, wb: 9, wc: 12, wd: 17, jx: 0.3, jy: 0.7, law: 1, unison: 3, spread: 0.6 }, {}, SR, 0.6);
  for (let vl = 0; vl < 3; vl++) e('vector_env' + vl, 'vector', { venv: 1, vloop: vl, t1: 100, t2: 150, t3: 90, t4: 200, x1: 0, y1: 0, x2: 1, y2: 0, x3: 1, y3: 1, x4: 0, y4: 1, lx: 0.4, ly: 0.3, lrate: 4 }, {}, SR, 1.4);
  e('vector_env_half', 'vector', { venv: 0.5, vloop: 2, t1: 50, t2: 50, t3: 50, t4: 50, lx: 1, lrate: 9, unison: 5, spread: 1, det: 0.8 }, {}, SR, 0.9);
  for (let d = 0; d <= 11; d++) e('vector_seq_div' + d, 'vector', { mode: 1, sdiv: d, stime: 120, steps: 3 + (d % 6), xfade: d / 11, sloop: d % 3 ? 1 : 0, lstart: 1 + (d % 4), sgate: d % 2 ? 0.7 : 0, sfenv: d % 3 ? 0.8 : 0, unison: 1 + d % 4, spread: 0.5 }, {}, SR, 1.1, {}, d % 2 ? 100 : 133);
  e('vector_seq_16', 'vector', { mode: 1, steps: 16, sdiv: 9, xfade: 0.5, sloop: 1, lstart: 5, s1: 17, s2: 17, unison: 7, spread: 1 }, {}, SR, 1.0, {}, 170); e('vector_seq_noise', 'vector', { mode: 1, steps: 4, s1: 17, s2: 17, s3: 17, s4: 0, sdiv: 7 }, {}, SR, 0.7);
  e('vector_clamp', 'vector', { mode: 1.4, steps: 40, sdiv: 99, unison: 30, lstart: 40, xfade: 7, wa: -3, wb: 99, jx: 3, jy: -2, oct: 6 }, {}, SR, 0.5, {}, 1000); e('vector_bpm_low', 'vector', { mode: 1, sdiv: 2 }, {}, SR, 1.0, {}, 5);
  e('vector_hi', 'vector', { wa: 14, wb: 15, wc: 16, wd: 8, oct: 5, unison: 4, det: 1 }, {}, 96000, 0.3); e('vector_44', 'vector', { wa: 9, wb: 10, wc: 11, wd: 12, oct: 0 }, {}, 44100, 0.5);
  // scanned: every excitation, scan path and ends, the hardening and re-strike
  for (let x = 0; x < 5; x++) for (let en = 0; en < 2; en++) e('scanned_exc' + x + '_ends' + en, 'scanned', { exc: x, ends: en, path: x % 3, masses: 32 + 8 * x, rexc: x === 2 ? 3 : 0, hard: x / 5, taper: en * 0.7, pos: 0.1 + 0.2 * x }, {}, SR, 0.5);
  e('scanned_max', 'scanned', { masses: 128, urate: 4000, stiff: 1, center: 1, damp: 0, smooth: 1, hard: 1, taper: 1, rest: 1, keep: 1, rexc: 20, exc: 2, path: 2, width: 1 }, {}, SR, 0.4);
  e('scanned_min', 'scanned', { masses: 3, urate: 0.2, stiff: 0, center: 0, damp: 1, smooth: 0, rest: 0, keep: 0, width: 0, pos: 1, path: 1 }, {}, SR, 0.4); e('scanned_odd', 'scanned', { masses: 63, ends: 0, exc: 1, path: 2 }, {}, SR, 0.4);
  e('scanned_hi', 'scanned', { oct: 6, masses: 64, urate: 700 }, {}, 96000, 0.3); e('scanned_lo', 'scanned', { oct: 0, masses: 96, urate: 30 }, {}, 44100, 0.6);
  // resynth: each analysis played plain, with the speed / freeze / formant / noise / harmonic / stretch settings; no analysis at all; an analysis that is not valid
  for (const n of sndNames) { e('resynth_' + n, 'resynth', { parts: 96 }, {}, SR, 0, { rsData: JSON.parse(JSON.stringify(snd[n])) }); e('resynth_' + n + '_fx', 'resynth', { speed: 0.5, fkeep: 1, harm: 1, stretch: -0.5, tilt: 0.5, pmot: 0.5, noise: 1, tune: 0 }, { tune: 7 }, SR, 0.5, { rsData: JSON.parse(JSON.stringify(snd[n])) }); }
  e('resynth_freeze', 'resynth', { freeze: 1, pos: 0.3, speed: 2 }, {}, SR, 0.8, { rsData: JSON.parse(JSON.stringify(snd.harm)) }); e('resynth_orig', 'resynth', { orig: 1, parts: 7, noise: 0 }, { tune: -5 }, SR, 0.6, { rsData: JSON.parse(JSON.stringify(snd.bell)) });
  e('resynth_late', 'resynth', { pos: 0.95, speed: 8 }, {}, 44100, 0, { rsData: JSON.parse(JSON.stringify(snd.harm)) }); e('resynth_clamp', 'resynth', { parts: 900, speed: 99, harm: 4, stretch: 3, tilt: -9, fkeep: 5, pmot: 9, pos: 5 }, {}, SR, 0.5, { rsData: JSON.parse(JSON.stringify(snd.pluck44)) });
  e('resynth_96k', 'resynth', { noise: 1 }, {}, 96000, 0.4, { rsData: JSON.parse(JSON.stringify(snd.noise)) }); e('resynth_none', 'resynth', {}, {}, SR, 0.5); e('resynth_none_fx', 'resynth', { oct: 3, atk: 40, sus: 0.4, rel: 300 }, { level: -2, drive: 0.4 }, 44100, 0.7);
  e('resynth_np0', 'resynth', {}, {}, SR, 0.5, { rsData: { v: 1, np: 0, nf: 4, a: 'AAAA' } }); e('resynth_empty_a', 'resynth', {}, {}, SR, 0.5, { rsData: { v: 1, np: 3, nf: 4, a: '' } });
  e('resynth_short_a', 'resynth', { noise: 0 }, {}, SR, 0.5, { rsData: { v: 1, np: 2, nf: 4, f0: 200, hop: 0.02, dur: 0.1, rat: [1, 2], hn: [1, 2], pk: 0.5, a: 'AQIDBAUG' } });
  // bytebeat: formulas, the knobs, the modes, the bit depth, smoothing, a loop, a late start; bad formulas play one sample of silence; a missing formula plays the default
  FORMULAS.forEach((f, i) => e('bb_f' + String(i).padStart(2, '0'), 'bytebeat', { a: 20 + i * 7, b: 255 - i * 5, c: 3 + i * 9, mode: i % 3, bits: 8 - (i % 5), smooth: i % 4 === 3 ? 0.5 : 0, start: i % 6 === 5 ? 5000 : 0, loop: i % 7 === 6 ? 2 : 0, rate: 4000 + 1500 * (i % 6), track: i % 2 }, i % 5 === 4 ? { tune: 5 } : {}, i % 8 === 7 ? 44100 : SR, 0.5, { bbExpr: f }));
  BAD.forEach((f, i) => e('bb_bad' + String(i).padStart(2, '0'), 'bytebeat', {}, {}, SR, 0.4, { bbExpr: f }));
  e('bb_missing', 'bytebeat', {}, {}, SR, 0.5); e('bb_notstring', 'bytebeat', {}, {}, SR, 0.5, { bbExpr: 5 });
  e('bb_clamp', 'bytebeat', { rate: 1e9, a: -9, b: 999, c: 300.4, mode: 7, bits: 99, smooth: 5, start: 1e12, loop: 99, track: 9, oct: 9, sus: 3 }, {}, SR, 0.5, { bbExpr: 't*(t>>5|t>>8)' });
  e('bb_slow', 'bytebeat', { rate: 1, track: 0, loop: 1, smooth: 1, bits: 1, mode: 2 }, { tune: -24 }, 22050, 0.8, { bbExpr: 't*3&t>>5' }); e('bb_fx', 'bytebeat', {}, { tail: 1 }, SR, 0, { bbExpr: '(t*5&t>>7)|(t*3&t>>10)' }, 120, [mkFx('degrade'), { type: 'room', on: true }]);
  // added after the first review of U29 (appended: no earlier case changes). Bytebeat: a loop that really wraps; Loop 13 is 2^20 steps (the formulas carry a t>>16 / t>>15 term: one that repeats every 2^16 steps could not show where a loop wraps), so the clock is the highest (96000 steps/s) and the output rate low (4000) to reach 1.15 million steps in 12 s; Loop 99 clamps to 13 and Loop 12 / 11 wrap at 2^19 / 2^18. Scanned: the Scatter path with fewer than 32 masses (groups of 4, not masses/8) and group ends that are not full.
  for (const lp of [13, 99, 12, 11]) e('bb_wrap_loop' + lp, 'bytebeat', { rate: 48000, track: 1, oct: 3, loop: lp, bits: 8 }, {}, 4000, 12, { bbExpr: '(t*(t>>5|t>>8))^(t>>16)' });
  e('bb_wrap_start', 'bytebeat', { rate: 48000, track: 1, oct: 3, loop: 13, start: 1000000, smooth: 0.3 }, {}, 4000, 12, { bbExpr: '((t*5&t>>7)|(t*3&t>>10))^(t>>15)' });
  for (const m of [3, 4, 5, 8, 9, 13, 17, 25, 31, 32, 33, 40, 64]) e('scanned_scatter_m' + m, 'scanned', { masses: m, path: 2, exc: m % 5, ends: m % 2, pos: 0.3, rexc: 1 }, {}, SR, 0.4);
  for (const m of [5, 9, 17, 31]) e('scanned_shuffle_m' + m, 'scanned', { masses: m, path: 1, exc: m % 5, ends: m % 2 }, {}, SR, 0.4);
  // the pad effects on engines
  for (const [i, k] of ['table', 'vector', 'scanned', 'resynth', 'bytebeat'].entries()) e('fx_' + k, k, {}, { tail: 1 + i }, SR, 0, k === 'resynth' ? { rsData: JSON.parse(JSON.stringify(snd.pluck44)) } : k === 'bytebeat' ? { bbExpr: 't*(t>>9|t>>13)' } : {}, 120, [mkFx(fxTypes[i % 4]), { type: 'room', on: true }]);
  return cases;
}
// the Int16 pairs of a wavetable frame as the base64 TableCore.fromCycles writes them
function SU(E, vals) { return E.SpecUtil.b64(new Uint8Array(Int16Array.from(vals).buffer)); }
function renderCase(E, c) { const v = E.mkVoice(c.name, [], JSON.parse(JSON.stringify(c.v))); v.fx = c.fx.map(f => ({ ...f })); E.setBpm(c.bpm); try { return E.renderVoice(v, c.sr, null, c.hold); } finally { E.setBpm(120); } }

function main(argv) {
  if (argv[0] === '--fixture') return fixture(path.resolve(argv[1] || '/home/claude/wt/beta-ref'));
  const check = argv[0] === '--check', golden = argv[0] === '--golden', withSamples = argv.includes('--samples'), outDir = (check || golden) ? null : argv.filter(a => !a.startsWith('--'))[0] || '/tmp/u29/engine-ref';
  const E = engineOf(ROOT), fx = JSON.parse(fs.readFileSync(path.join(GOLD, 'engine_xeng2_voices.json'), 'utf8'));
  const inc = fs.readFileSync(path.join(GOLD, 'engine_xeng2_voices_golden.inc'), 'utf8'), casesPath = path.join(GOLD, 'engine_xeng2_golden.inc');
  const gold = new Map([...inc.matchAll(/\{("(?:[^"\\]|\\.)*"), "([0-9a-f]{64})", (\d+)\}/g)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = []; let bad = 0;
  fx.voices.forEach((s, i) => {
    const r = E.renderVoice(build(E, s, s.k, fx.blobs), SR, null, s.hold), h = sha(r), g = gold.get(s.k);
    if (check && (!g || g.sha !== h || g.len !== r.length)) { bad++; console.error('DIFFERS from the golden: ' + s.k); }
    rows.push({ k: s.k, sha256: h, len: r.length });
    if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'u' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength));
  });
  const cases = directCases(E), crow = cases.map((c, i) => { const r = renderCase(E, c); if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'c' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength)); return { name: c.name, sha256: sha(r), len: r.length }; });
  if (golden) {
    fs.writeFileSync(casesPath, ['// GENERATED by native/tools/engine-xeng2-dump.mjs --golden from the real ENGINE block (the direct cases of directCases): sha256 of renderVoice(v, sr, null, hold) and its length, Node 22 on x86-64 Linux.',
      '// Do not edit: a mismatch means the port is wrong (or the JS changed on purpose: regenerate with --golden and say so).',
      'struct Xeng2CaseGolden { const char* name; const char* sha256; long len; };', 'static const Xeng2CaseGolden kXeng2CaseGolden[] = {', ...crow.map(r => `  {${JSON.stringify(r.name)}, ${JSON.stringify(r.sha256)}, ${r.len}},`), '};',
      'static const int kXeng2CaseGoldenN = ' + crow.length + ';', ''].join('\n'));
    console.log('wrote ' + crow.length + ' direct cases'); return;
  }
  if (check) {
    const cg = new Map([...fs.readFileSync(casesPath, 'utf8').matchAll(/\{("(?:[^"\\]|\\.)*"), "([0-9a-f]{64})", (\d+)\}/g)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
    if (cg.size !== crow.length) { console.error('the committed case list has ' + cg.size + ' cases, the generator ' + crow.length); bad++; }
    for (const r of crow) { const g = cg.get(r.name); if (!g || g.sha !== r.sha256 || g.len !== r.len) { bad++; console.error('DIFFERS from the committed hash: case ' + r.name); } }
    if (bad) { console.error(bad + ' differ'); process.exit(1); }
    console.log('engine_xeng: ' + rows.length + ' voices and ' + crow.length + ' cases match the committed hashes'); return;
  }
  fs.writeFileSync(path.join(outDir, 'ref.json'), JSON.stringify({ voices: rows, cases: crow }));
  fs.writeFileSync(path.join(outDir, 'cases.json'), JSON.stringify({ cases }));
  console.log('reference written to ' + outDir + ': ' + rows.length + ' voices, ' + cases.length + ' cases');
}
main(process.argv.slice(2));
