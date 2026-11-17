// U27: THE REFERENCE OF THE LIVE ENGINES acid and mach (rendered through renderLiveCore) AND THE BLIP DRUM (renderBlip) (exact, Node only): the real ENGINE block of index.html run in Node's vm, on (1) every factory voice of beta's
// tests/golden/engines.json whose engine is acid, mach or blip, fingerprinted as beta's engines.json fingerprints them (SHA-256 of the float32 bytes of renderVoice(v, 48000, null, hold)), (2) direct cases: random parameters
// of every engine (the ranges of the engine cards, XUI / BLU), every machine / wave / filter / LFO destination / grain value, the voice's Level / Tune / Drive, other sample rates (22050 to 96000, one fractional), holds that clamp, the
// pad effects on Blip, through the real renderVoice, and (3) CORE SCRIPTS: AcidCore / MachCore driven directly (note on with legato and accent, slides, several notes, a new rate per block, set() between notes, blocks that are not
// multiples of 16, fractional block bounds, renderLiveCore with accent), which the pad render never reaches (it plays one plain note) but a live path will.
//   node native/tools/engine-xeng3-dump.mjs --fixture <betaRoot>   (by hand, needs a checkout of `beta`) writes native/tests/golden/engine_xeng3_voices.json (the voices: only what differs from mkVoice's defaults, the engine's
//        parameter object and the pad effects), engine_xeng3_voices_golden.inc (beta's sha256 and length of each, copied, never made here) and xeng3_ranges.json (min / max / curve / default of every parameter: XUI, BLU).
//   node native/tools/engine-xeng3-dump.mjs [outDir]               the local Node's reference: <outDir>/ref.json (the voices' hashes by this repo's ENGINE block), <outDir>/cases.json (the direct cases and scripts), with --samples
//        <outDir>/u<n>.f32 and <outDir>/c<n>.f32 (the renders, to find the first differing sample).
//   node native/tools/engine-xeng3-dump.mjs --golden               writes native/tests/golden/engine_xeng3_golden.inc (the direct cases' hashes: sha256 of the real JS's render, Node 22 x86-64 Linux).
//   node native/tools/engine-xeng3-dump.mjs --check                exits 1 if the local Node's renders do not give the committed hashes (beta's for the voices, engine_xeng3_golden.inc for the cases). Linux only gate.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../..');
const SR = 48000;
const KINDS = ['acid', 'mach', 'blip'];
const KSET = new Set(KINDS);
const GOLD = path.join(ROOT, 'native/tests/golden');
const sha = a => crypto.createHash('sha256').update(Buffer.from(a.buffer, a.byteOffset, a.byteLength)).digest('hex');
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// The ENGINE block as a standalone script (as engine-xeng2-dump.mjs's engineOf, with the live cores exported too).
function engineOf(root) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
  const A = html.indexOf('/* ===ENGINE START=== */'), B = html.indexOf('/* ===ENGINE END=== */');
  if (A < 0 || B < 0) throw new Error('engine markers not found');
  const ctx = vm.createContext({ console: { log() {}, warn() {}, error() {} }, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object, JSON, Map, Set, WeakMap, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
  ctx.globalThis = ctx; ctx.self = ctx;
  vm.runInContext(html.slice(A, B) + '\n;globalThis.__E = {renderVoice, mkVoice, freshFx, FX, XENG, AcidCore, MachCore, BlipVoice, renderLiveCore, setBpm: b => { globalThis.S = { bpm: b }; }};', ctx, { filename: 'index.html(engine)' });
  return ctx.__E;
}
const defaultsOf = E => { const v = E.mkVoice('x', [], {}); delete v.layers; return JSON.parse(JSON.stringify(v)); };
// a voice as the fixture keeps it: what differs from mkVoice's defaults (the layers are not read by an engine voice), and the pad effects
function slim(v, D) {
  const out = { v: {}, fx: (v.fx || []).map(f => ({ ...f })) };
  for (const k of Object.keys(v)) if (k !== 'layers' && k !== 'fx' && k !== 'name' && !same(v[k], D[k])) out.v[k] = JSON.parse(JSON.stringify(v[k]));
  return out;
}
const build = (E, s, name) => { const v = E.mkVoice(name, [], JSON.parse(JSON.stringify(s.v))); v.fx = s.fx.map(f => ({ ...f })); return v; };

// BLU (the Blip card's rows) is outside the ENGINE block: cut out of index.html and evaluated with stubs for its formatters
function bluOf(root) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
  const a = html.indexOf('const BLU = {'), b = html.indexOf('\n};', a); if (a < 0 || b < 0) throw new Error('BLU not found');
  return vm.runInNewContext('const hz = v => v, ms = v => v, pct = v => v; ' + html.slice(a, b + 3) + '; BLU', { Math });
}

function fixture(betaRoot) {
  const G = createRequire(import.meta.url)(path.join(betaRoot, 'tools/engine-golden.js'));
  const gold = JSON.parse(fs.readFileSync(path.join(betaRoot, 'tests/golden/engines.json'), 'utf8'));
  const E = engineOf(betaRoot), D = defaultsOf(E), voices = [], rows = [], per = {}, seen = new Set(), all = {};
  for (const it of G.listVoices()) {
    const w = gold.voices[it.key]; if (!w || w.error) continue;
    G.engine().setSeed(G.seedFor(it.key));
    const b = it.build(), v = b.v;
    if (!KSET.has(v.engine)) continue;
    if (seen.has(it.key)) throw new Error('fixture: a voice key twice: ' + it.key); seen.add(it.key);
    const s = slim(v, D);
    const r = E.renderVoice(build(E, s, 'x'), SR, null, b.hold);
    if (sha(r) !== w.sha256 || r.length !== w.len) throw new Error('fixture: the slim voice does not reproduce the golden: ' + it.key);
    voices.push({ k: it.key, hold: b.hold, ...s }); rows.push(`  {${JSON.stringify(it.key)}, ${JSON.stringify(w.sha256)}, ${w.len}},`); per[v.engine] = (per[v.engine] || 0) + 1;
  }
  // completeness: every key of beta's own golden that is a voice of these engines must be in the fixture (the listing tool is the source of the set; this re-reads the golden's own keys the other way round)
  for (const it of G.listVoices()) { const w = gold.voices[it.key]; if (!w || w.error) continue; G.engine().setSeed(G.seedFor(it.key)); const e = it.build().v.engine; if (KSET.has(e)) all[e] = (all[e] || 0) + 1; }
  if (!same(all, per)) throw new Error('fixture: incomplete ' + JSON.stringify(all) + ' vs ' + JSON.stringify(per));
  fs.writeFileSync(path.join(GOLD, 'engine_xeng3_voices.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-xeng3-dump.mjs --fixture from beta (' + gold.node + '): the factory voices whose engine is acid, mach or blip. Only what differs from mkVoice defaults (and the engine\'s parameter object).', sampleRate: SR, defaults: D, voices }) + '\n');
  fs.writeFileSync(path.join(GOLD, 'engine_xeng3_voices_golden.inc'), ['// GENERATED by native/tools/engine-xeng3-dump.mjs --fixture: tests/golden/engines.json of `beta` (made with Node ' + gold.node + ' on x86-64 Linux), the rows of the voices in engine_xeng3_voices.json, copied unchanged.',
    '// sha256 of the float32 bytes of renderVoice(v, 48000, null, hold), and its length in samples. The gate of test_engine_xeng3 on every platform.',
    'struct Xeng3Golden { const char* key; const char* sha256; long len; };', 'static const Xeng3Golden kXeng3Golden[] = {', ...rows, '};', 'static const int kXeng3GoldenN = ' + rows.length + ';', ''].join('\n'));
  // the engine cards' parameter ranges (XUI for acid / mach, BLU for blip), for the direct cases
  const X = G.engine().XUI, BLU = bluOf(betaRoot), ranges = {};
  for (const k of KINDS) {
    const d = k === 'blip' ? E.BlipVoice.defaults() : E.XENG[k].d(); ranges[k] = {};
    for (const p of Object.keys(d)) { if (p === 'vel' || (k !== 'blip' && (p === 'tune' || p === 'drive' || p === 'level'))) continue; const r = k === 'blip' ? BLU[p] : X[k] && X[k].rows && X[k].rows[p]; ranges[k][p] = r ? { min: r.min, max: r.max, c: r.c, d: d[p] } : { d: d[p] }; }
  }
  fs.writeFileSync(path.join(GOLD, 'xeng3_ranges.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-xeng3-dump.mjs --fixture from beta: every engine parameter with the engine card\'s min / max / curve (XUI, BLU) and the default. The direct cases draw from it.', ranges }, null, 1) + '\n');
  console.log('wrote ' + rows.length + ' voices ' + JSON.stringify(per));
}

// ---- the direct cases: made from a fixed seed (the same cases every time) -------------------------------------------------------------------------------------------------------------------
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };
function directCases(E) {
  const R = JSON.parse(fs.readFileSync(path.join(GOLD, 'xeng3_ranges.json'), 'utf8')).ranges, r = lcg(2717), cases = [];
  const pick = a => a[Math.floor(r() * a.length)], ur = (lo, hi) => lo + r() * (hi - lo), ui = (lo, hi) => Math.floor(ur(lo, hi + 1));
  const draw = q => { if (q.min === undefined) return q.d; const t = r(); if (q.c === 'log') return q.min * Math.pow(q.max / q.min, t); if (q.c === 'int') return Math.round(q.min + t * (q.max - q.min)); return q.min + t * (q.max - q.min); };
  const fxTypes = ['degrade', 'reson', 'pdelay', 'preverb'];
  const mkFx = type => { const f = E.freshFx(type); for (const k in E.FX[type].rows) { const w = E.FX[type].rows[k], t = r(); f[k] = w.c === 'logf' || w.c === 'log' ? w.min * Math.pow(w.max / w.min, t) : w.min + t * (w.max - w.min); if (['bits', 'note', 'chord', 'down', 'time'].includes(k) && r() < 0.5) f[k] = Math.round(f[k]); } if ('mode' in f) f.mode = ui(0, type === 'preverb' ? 2 : type === 'pdelay' ? 4 : 1); return f; };
  const add = (name, kind, params, o = {}, sr = SR, hold = 0, fx = []) => cases.push({ name, kind, sr, hold, bpm: 120, v: { engine: kind, [kind]: params, ...o }, fx });
  const SRS = [48000, 44100, 32000, 96000, 88200, 22050, 48000];
  const PER = { acid: 36, mach: 42, blip: 34 };
  for (const k of KINDS) for (let i = 0; i < PER[k]; i++) {
    const params = {}; for (const p in R[k]) if (r() < 0.75) params[p] = draw(R[k][p]);
    const o = {}; if (r() < 0.5) o.level = ur(-12, 6); if (r() < 0.4) o.tune = ui(-12, 24); if (r() < 0.35) o.drive = r(); if (r() < 0.5) o.len = ur(0.15, 2.5); if (r() < 0.2) o.gate = ur(100, 900);
    const sr = SRS[i % SRS.length], hold = k === 'blip' ? 0 : (i % 3 === 0 ? 0 : ur(0.1, 1.5));
    add(k + String(i).padStart(2, '0'), k, params, o, sr, hold, k === 'blip' && i % 5 === 3 ? [mkFx(pick(fxTypes))] : []);
  }
  const e = (name, k, params, o, sr, hold, fx) => add('edge_' + name, k, params, o, sr, hold, fx);
  for (const k of KINDS) e(k + '_defaults', k, {}, {}, SR, 0);
  // ---- acid: the waves, pulse width, sub, noise, the ladder (resonance up to self-oscillation), the filter envelope, the drive / level, the amp stages
  for (const w of [0, 0.25, 0.5, 0.75, 1]) for (const pw of [0, 0.5, 1]) e('acid_w' + w + '_pw' + pw, 'acid', { wave: w, pw, oct: 2, sub: w * 0.5, noise: pw * 0.2 }, {}, SR, 0.6);
  for (const res of [0, 0.4, 0.72, 0.95, 1, 1.3]) e('acid_res' + res, 'acid', { res, cut: 220, envAmt: 1, dec: 120, oct: 1 }, {}, SR, 0.7);
  for (const cut of [20, 60, 380, 2000, 9000, 20000]) e('acid_cut' + cut, 'acid', { cut, envAmt: 0.2 }, {}, SR, 0.5);
  e('acid_sat', 'acid', { sat: 1, res: 0.9, drive: 0.5 }, { drive: 0.6, level: 4 }, SR, 0.5); e('acid_nosat', 'acid', { sat: 0, sus: 0, ampDec: 60 }, { level: -6 }, SR, 0.5); e('acid_key', 'acid', { key: 1, oct: 3, envAmt: 0 }, { tune: 12 }, SR, 0.5);
  e('acid_slide', 'acid', { slide: 400, accent: 1 }, {}, SR, 0.5); e('acid_ampdec', 'acid', { ampDec: 4000, sus: 0.2, rel: 800 }, {}, SR, 0.5); e('acid_clamp', 'acid', { ampDec: 0, rel: 0, dec: 0, slide: 0, sus: 3, cut: 1e5, res: 5, oct: 9, key: 5, sat: 9 }, {}, SR, 0.4);
  e('acid_neg', 'acid', { oct: -6, cut: 5, rel: 3000 }, { tune: -24 }, 22050, 0.5); e('acid_tune', 'acid', { oct: 0 }, { tune: 36 }, SR, 0.4);
  for (const sr of [8000, 22050, 32000, 44100, 88200, 96000, 192000]) e('acid_sr' + sr, 'acid', { res: 0.8, noise: 0.1, sub: 0.3 }, { level: -2 }, sr, 0.4);
  e('acid_len', 'acid', {}, { len: 0.2 }, SR, 0); e('acid_len_big', 'acid', {}, { len: 9 }, SR, 0); e('acid_hold_big', 'acid', { rel: 900 }, {}, 4000, 40); e('acid_hold_tiny', 'acid', {}, {}, SR, 0.001);
  e('acid_frac_sr', 'acid', { rel: 50 }, {}, 4000.3, 40); e('acid_frac_sr2', 'acid', {}, {}, 11025.5, 0.5); e('acid_sr1', 'acid', {}, {}, 1, 3); e('acid_sr100', 'acid', { oct: -3 }, {}, 100, 2);
  e('acid_fx_ignored', 'acid', {}, {}, SR, 0.4, [mkFx('degrade')]);   // pad effects do not run on a live engine
  // ---- mach: every machine at the ends and middle of A, B, C, the filters, the LFO on each destination, glide, envelope
  for (let m = 0; m < 7; m++) for (const [a, b, c] of [[0, 0, 0], [1, 1, 1], [0.5, 0.5, 0.5], [0.2, 0.9, 0.1], [0.85, 0.15, 0.7]]) e('mach_m' + m + '_' + [a, b, c].join('_'), 'mach', { machine: m, a, b, c, det: m / 7, oct: 1 + (m % 3) }, {}, SR, 0.4);
  for (const m of [-1, 7, 8, 99, 2.6, 3.9, -0.5]) e('mach_machine_odd' + m, 'mach', { machine: m, a: 0.4, b: 0.6, c: 0.3 }, {}, SR, 0.4);
  for (let d = -1; d <= 4; d++) for (const m of [0, 2, 4]) e('mach_lfo' + d + '_m' + m, 'mach', { machine: m, lfoDest: d, lfoDepth: 0.8, lfoRate: 6 + m, a: 0.3 + 0.1 * m, b: 0.5, c: 0.5 }, {}, SR, 0.5);
  e('mach_lfo_fast', 'mach', { lfoDest: 2, lfoDepth: 1, lfoRate: 400, machine: 4, a: 0.5 }, {}, SR, 0.4); e('mach_lfo_dest_frac', 'mach', { lfoDest: 1.9, lfoDepth: 1, lfoRate: 3 }, {}, SR, 0.4); e('mach_lfo_neg', 'mach', { lfoDest: 3, lfoDepth: -1, lfoRate: 7 }, {}, SR, 0.4);
  for (const g of [0, 0.4, 0.6, 50, 2000]) e('mach_glide' + g, 'mach', { glide: g, machine: 1, b: 0.2 }, {}, SR, 0.4);
  for (const [hp, lp, res] of [[30, 3500, 0.25], [2000, 8000, 0.9], [10, 100, 1], [20000, 20000, 0], [0, 30000, 0.5], [500, 600, 0.99]]) e('mach_filt_' + hp + '_' + lp, 'mach', { hp, lp, res, fenv: 0.8, fdec: 50, machine: 0, a: 0.5, b: 0.7 }, {}, SR, 0.4);
  e('mach_env_slow', 'mach', { atk: 400, dec: 2000, sus: 0.3, rel: 1500, fdec: 1500 }, {}, SR, 0.8); e('mach_env_fast', 'mach', { atk: 0, dec: 0, sus: 3, rel: 0, fdec: 0, fenv: 1 }, {}, SR, 0.4); e('mach_drive', 'mach', { machine: 2, b: 1, c: 1 }, { drive: 0.8, level: 5 }, SR, 0.4);
  e('mach_swarm_c', 'mach', { machine: 3, a: 1, b: 1, c: 1 }, { tune: 7 }, SR, 0.4); e('mach_additive_hi', 'mach', { machine: 6, a: 1, b: 0, c: 1, oct: 5 }, {}, 22050, 0.4); e('mach_noise_c', 'mach', { machine: 5, a: 0.1, b: 0.95, c: 0.9 }, {}, SR, 0.5);
  e('mach_neg', 'mach', { oct: -6, lp: 50 }, { tune: -24 }, 32000, 0.5);
  for (const sr of [8000, 22050, 32000, 44100, 88200, 96000, 192000]) e('mach_sr' + sr, 'mach', { machine: sr % 7, a: 0.6, b: 0.6, c: 0.6 }, { level: -2 }, sr, 0.4);
  e('mach_len', 'mach', {}, { len: 0.2 }, SR, 0); e('mach_len_big', 'mach', {}, { len: 9 }, SR, 0); e('mach_hold_big', 'mach', { rel: 900 }, {}, 4000, 40); e('mach_hold_tiny', 'mach', {}, {}, SR, 0.001);
  e('mach_frac_sr', 'mach', { rel: 50, machine: 4 }, {}, 4000.3, 40); e('mach_frac_sr2', 'mach', { machine: 6 }, {}, 11025.5, 0.5); e('mach_sr1', 'mach', {}, {}, 1, 3); e('mach_sr100', 'mach', { oct: -3, machine: 3 }, {}, 100, 2);
  e('mach_fx_ignored', 'mach', {}, {}, SR, 0.4, [mkFx('reson')]);
  // ---- blip: every drum of the factory sheet is a voice; here every switch of the voice: kick, FM, ring, noise, click, the bipolar filter both ways, drive, the grain repeats, tune / level / drive of the voice, sample rates, the long clamp
  for (const filt of [-1, -0.6, -0.2, -0.019, 0, 0.019, 0.3, 0.7, 1]) for (const res of [0, 0.5, 1]) e('blip_filt' + filt + '_res' + res, 'blip', { filt, res, noise: 0.4, pitch: 700, decay: 180 }, {}, SR, 0);
  for (const grain of [0, 0.9, 1, 2, 5, 8]) e('blip_grain' + grain, 'blip', { grain, grainT: 20 + grain * 7, decay: 90, fm: 0.2 }, {}, SR, 0);
  for (const fm of [0, 0.05, 0.5, 1]) for (const fmR of [0.25, 1.5, 12]) e('blip_fm' + fm + '_r' + fmR, 'blip', { fm, fmR, kick: 0.5, decay: 300 }, {}, SR, 0);
  for (const ring of [0, 0.3, 1]) e('blip_ring' + ring, 'blip', { ring, ringF: ring * 2000 + 0.5, decay: 400 }, {}, SR, 0);
  for (const kick of [0, 0.1, 0.5, 1]) for (const kickT of [2, 35, 400]) e('blip_kick' + kick + '_t' + kickT, 'blip', { kick, kickT, pitch: 60 + kick * 100, decay: 500 }, {}, SR, 0);
  for (const noise of [0, 0.5, 1]) for (const click of [0, 0.4, 1]) e('blip_nz' + noise + '_cl' + click, 'blip', { noise, click, decay: 60, atk: 5 }, {}, SR, 0);
  for (const d of [0, 0.3, 1]) for (const vd of [0, 0.5, 2]) e('blip_drive' + d + '_v' + vd, 'blip', { drive: d, pitch: 90, decay: 400 }, { drive: vd }, SR, 0);
  for (const tune of [-24, -7, 0, 5, 36]) e('blip_tune' + tune, 'blip', { pitch: 200, kick: 0.6 }, { tune }, SR, 0); for (const level of [-30, -6, 0, 6, 12]) e('blip_level' + level, 'blip', { pitch: 100, drive: 0.4 }, { level }, SR, 0);
  e('blip_clamp', 'blip', { pitch: 1e5, kick: 9, kickT: 0, fm: 5, fmR: 99, ring: 9, ringF: 1e6, noise: 9, click: 9, atk: 0, decay: 0, drive: 9, filt: 9, res: 9, grain: 99, grainT: 0 }, {}, SR, 0);
  e('blip_neg', 'blip', { pitch: -300, kick: -1, kickT: -5, fm: -1, ring: -1, noise: -1, click: -1, atk: -1, decay: 50, drive: -1, filt: -9, res: -9, grain: -1, grainT: -5 }, { tune: -9, level: -3 }, SR, 0);
  e('blip_long_clamp', 'blip', { decay: 9000, grain: 8, grainT: 500 }, {}, 8000, 0); e('blip_frac_sr', 'blip', { decay: 9000 }, {}, 4000.3, 0); e('blip_frac_sr2', 'blip', { decay: 100 }, {}, 11025.5, 0);
  for (const sr of [8000, 22050, 32000, 44100, 88200, 96000, 192000]) e('blip_sr' + sr, 'blip', { filt: -0.3, res: 0.5, grain: 2, fm: 0.1, noise: 0.2 }, { level: -2 }, sr, 0);
  e('blip_sr1', 'blip', {}, {}, 1, 0); e('blip_sr100', 'blip', { decay: 2000, grain: 3 }, {}, 100, 0); e('blip_hold_ignored', 'blip', { decay: 100 }, { len: 9, gate: 900 }, SR, 5);
  for (const [i, t] of fxTypes.entries()) e('blip_fx_' + t, 'blip', { decay: 300 + 100 * i, pitch: 150 + 50 * i, filt: i % 2 ? 0.3 : -0.3 }, { tail: 1 + i }, SR, 0, [mkFx(t), { type: 'room', on: true }]);
  e('blip_fx_two', 'blip', { decay: 250 }, { tail: 2 }, SR, 0, [mkFx('degrade'), mkFx('preverb')]);
  return cases;
}

// ---- core scripts: AcidCore / MachCore driven directly -----------------------------------------------------------------------------------------------------------------------------------------
// ops: {o:'on', leg, acc} {o:'off'} {o:'set', p} {o:'r', a, b, rate}; `len` is the Float32Array's length; the hash is of L. Also renderLiveCore with accent.
function coreScripts(E) {
  const R = JSON.parse(fs.readFileSync(path.join(GOLD, 'xeng3_ranges.json'), 'utf8')).ranges, r = lcg(4099), out = [];
  const ur = (lo, hi) => lo + r() * (hi - lo), ui = (lo, hi) => Math.floor(ur(lo, hi + 1)), pick = a => a[Math.floor(r() * a.length)];
  const draw = q => { if (q.min === undefined) return q.d; const t = r(); if (q.c === 'log') return q.min * Math.pow(q.max / q.min, t); if (q.c === 'int') return Math.round(q.min + t * (q.max - q.min)); return q.min + t * (q.max - q.min); };
  const params = (k, share) => { const p = {}; for (const n in R[k]) if (r() < share) p[n] = draw(R[k][n]); if (r() < 0.5) p.level = ur(-12, 6); if (r() < 0.3) p.tune = ui(-12, 12); if (r() < 0.3) p.drive = r(); return p; };
  const RATES = [1, 1.5, 2, 0.5, 1.2599, 0.25, 3, 1.0595, 0.7071];
  const add = (name, kind, sr, len, p, ops) => out.push({ name, script: true, kind, sr, len, p, ops });
  for (const kind of ['acid', 'mach']) for (let i = 0; i < 24; i++) {
    const sr = pick([48000, 44100, 32000, 96000, 22050]), ops = []; let pos = 0; const notes = ui(2, 5);
    for (let n = 0; n < notes; n++) {
      ops.push({ o: 'on', leg: r() < 0.55, acc: r() < 0.5 });
      if (r() < 0.25) ops.push({ o: 'set', p: params(kind, 0.3) });
      const blocks = ui(1, 4);
      for (let b = 0; b < blocks; b++) { const len = r() < 0.4 ? 16 * ui(1, 40) : ui(1, 700); ops.push({ o: 'r', a: pos, b: pos + len, rate: pick(RATES) }); pos += len; }
      if (r() < 0.8) { ops.push({ o: 'off' }); const len = ui(20, 1500); ops.push({ o: 'r', a: pos, b: pos + len, rate: pick(RATES) }); pos += len; }
    }
    add(kind + '_script' + String(i).padStart(2, '0'), kind, sr, pos + ui(0, 50), params(kind, 0.8), ops);
  }
  // every machine and LFO destination played as a legato line (glide on, a new rate every block), an accented acid line with slides
  for (let m = 0; m < 7; m++) add('mach_line_m' + m, 'mach', SR, 9000, { machine: m, glide: 120, lfoDepth: 0.6, lfoDest: m % 4, lfoRate: 5, a: 0.3 + 0.08 * m, b: 0.4, c: 0.6 }, [{ o: 'on', leg: false }, { o: 'r', a: 0, b: 2000, rate: 1 }, { o: 'on', leg: true }, { o: 'r', a: 2000, b: 4000, rate: 1.5 }, { o: 'on', leg: true }, { o: 'r', a: 4000, b: 6000, rate: 0.75 }, { o: 'off' }, { o: 'r', a: 6000, b: 9000, rate: 0.75 }]);
  for (const slide of [0, 0.4, 0.6, 65, 500]) for (const acc of [false, true]) add('acid_line_s' + slide + (acc ? '_acc' : ''), 'acid', SR, 12000, { slide, accent: 0.9, envAmt: 0.8, dec: 400, cut: 300, res: 0.8, key: 0.5, wave: 0.3, level: 0 }, [{ o: 'on', leg: false, acc }, { o: 'r', a: 0, b: 3000, rate: 1 }, { o: 'on', leg: true, acc: !acc }, { o: 'r', a: 3000, b: 6000, rate: 2 }, { o: 'on', leg: false, acc: true }, { o: 'r', a: 6000, b: 8000, rate: 1.5 }, { o: 'on', leg: true, acc: true }, { o: 'r', a: 8000, b: 9500, rate: 1.2 }, { o: 'off' }, { o: 'r', a: 9500, b: 12000, rate: 1.2 }]);
  // renderLiveCore with accent (the rendered path passes false for Acid): the whole note
  for (const [i, kind] of ['acid', 'mach'].entries()) add(kind + '_live_acc', kind, SR, 0, { cut: 500, res: 0.85, accent: 1, dec: 500 }, [{ o: 'live', acc: true, hold: 0.5 + 0.2 * i }]);
  // fractional block bounds: the writes at a fractional index are ignored, the block length is fractional
  add('acid_frac_bounds', 'acid', SR, 300, {}, [{ o: 'on', leg: false, acc: false }, { o: 'r', a: 0, b: 70.5, rate: 1 }, { o: 'r', a: 70.5, b: 140, rate: 1 }, { o: 'r', a: 20.25, b: 90, rate: 1.5 }, { o: 'off' }, { o: 'r', a: 140, b: 299.5, rate: 1 }]);
  add('mach_frac_bounds', 'mach', SR, 300, { machine: 4 }, [{ o: 'on', leg: false }, { o: 'r', a: 0, b: 70.5, rate: 1 }, { o: 'r', a: 70.5, b: 140, rate: 1 }, { o: 'off' }, { o: 'r', a: 140, b: 299.5, rate: 1 }]);
  // silence before a note (stage 0 and env 0 fills zeros), a note on while the last is still sounding, off twice, a bound beyond the array (writes ignored)
  for (const kind of ['acid', 'mach']) add(kind + '_edges', kind, SR, 500, {}, [{ o: 'r', a: 0, b: 64, rate: 1 }, { o: 'on', leg: true, acc: true }, { o: 'r', a: 64, b: 200, rate: 1 }, { o: 'off' }, { o: 'off' }, { o: 'r', a: 200, b: 260, rate: 1 }, { o: 'on', leg: true, acc: false }, { o: 'r', a: 260, b: 600, rate: 2 }]);
  return out;
}
function runScript(E, c) {
  const C = c.kind === 'acid' ? E.AcidCore : E.MachCore, L = new Float32Array(c.len), R = new Float32Array(c.len), core = new C(c.sr); core.set(c.p);
  for (const op of c.ops) {
    if (op.o === 'on') c.kind === 'acid' ? core.on(op.leg, op.acc) : core.on(op.leg); else if (op.o === 'off') core.off(); else if (op.o === 'set') core.set(op.p);
    else if (op.o === 'r') core.render(L, R, op.a, op.b, op.rate);
    else if (op.o === 'live') return E.renderLiveCore(C, c.p, c.sr, op.hold, op.acc);
  }
  for (let i = 0; i < c.len; i++) if (!Object.is(L[i], R[i]) && !(L[i] !== L[i] && R[i] !== R[i])) throw new Error('script: R differs from L in ' + c.name);
  return L;
}
function renderCase(E, c) {
  if (c.script) return runScript(E, c);
  const v = E.mkVoice(c.name, [], JSON.parse(JSON.stringify(c.v))); v.fx = c.fx.map(f => ({ ...f })); E.setBpm(c.bpm); try { return E.renderVoice(v, c.sr, null, c.hold); } finally { E.setBpm(120); }
}
// a golden hash must not depend on a NaN's sign bit: no reference render may hold a NaN or an infinity
function sane(a, what) { for (let i = 0; i < a.length; i++) if (!Number.isFinite(a[i])) throw new Error('a render holds a non-finite sample (' + what + ' at ' + i + '): not allowed in a golden'); }

function main(argv) {
  if (argv[0] === '--fixture') return fixture(path.resolve(argv[1] || '/home/claude/wt/beta-ref'));
  const check = argv[0] === '--check', golden = argv[0] === '--golden', withSamples = argv.includes('--samples'), outDir = (check || golden) ? null : argv.filter(a => !a.startsWith('--'))[0] || '/tmp/u27/engine-ref';
  const E = engineOf(ROOT), fx = JSON.parse(fs.readFileSync(path.join(GOLD, 'engine_xeng3_voices.json'), 'utf8'));
  const inc = fs.readFileSync(path.join(GOLD, 'engine_xeng3_voices_golden.inc'), 'utf8'), casesPath = path.join(GOLD, 'engine_xeng3_golden.inc');
  const gold = new Map([...inc.matchAll(/\{("(?:[^"\\]|\\.)*"), "([0-9a-f]{64})", (\d+)\}/g)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = []; let bad = 0;
  fx.voices.forEach((s, i) => {
    const r = E.renderVoice(build(E, s, s.k), SR, null, s.hold), h = sha(r), g = gold.get(s.k);
    if (check && (!g || g.sha !== h || g.len !== r.length)) { bad++; console.error('DIFFERS from the golden: ' + s.k); }
    rows.push({ k: s.k, sha256: h, len: r.length });
    if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'u' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength));
  });
  const cases = [...directCases(E), ...coreScripts(E)], names = new Set();
  const crow = cases.map((c, i) => { if (names.has(c.name)) throw new Error('case name twice: ' + c.name); names.add(c.name); const r = renderCase(E, c); sane(r, c.name); if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'c' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength)); return { name: c.name, sha256: sha(r), len: r.length }; });
  if (golden) {
    fs.writeFileSync(casesPath, ['// GENERATED by native/tools/engine-xeng3-dump.mjs --golden from the real ENGINE block (the direct cases of directCases and the core scripts): sha256 of renderVoice(v, sr, null, hold) (or of L for a script) and its length, Node 22 on x86-64 Linux.',
      '// Do not edit: a mismatch means the port is wrong (or the JS changed on purpose: regenerate with --golden and say so).',
      'struct Xeng3CaseGolden { const char* name; const char* sha256; long len; };', 'static const Xeng3CaseGolden kXeng3CaseGolden[] = {', ...crow.map(r => `  {${JSON.stringify(r.name)}, ${JSON.stringify(r.sha256)}, ${r.len}},`), '};',
      'static const int kXeng3CaseGoldenN = ' + crow.length + ';', ''].join('\n'));
    console.log('wrote ' + crow.length + ' direct cases'); return;
  }
  if (check) {
    const cg = new Map([...fs.readFileSync(casesPath, 'utf8').matchAll(/\{("(?:[^"\\]|\\.)*"), "([0-9a-f]{64})", (\d+)\}/g)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
    if (cg.size !== crow.length) { console.error('the committed case list has ' + cg.size + ' cases, the generator ' + crow.length); bad++; }
    for (const r of crow) { const g = cg.get(r.name); if (!g || g.sha !== r.sha256 || g.len !== r.len) { bad++; console.error('DIFFERS from the committed hash: case ' + r.name); } }
    if (bad) { console.error(bad + ' differ'); process.exit(1); }
    console.log('engine_xeng3: ' + rows.length + ' voices and ' + crow.length + ' cases match the committed hashes'); return;
  }
  fs.writeFileSync(path.join(outDir, 'ref.json'), JSON.stringify({ voices: rows, cases: crow }));
  fs.writeFileSync(path.join(outDir, 'cases.json'), JSON.stringify({ cases }));
  console.log('reference written to ' + outDir + ': ' + rows.length + ' voices, ' + cases.length + ' cases');
}
main(process.argv.slice(2));
