// U28: THE REFERENCE OF THE RENDERED-ONLY ENGINES tone, modal, swarm, drift, piano, ep, organ, breath, strings, fmdrum, wind, phase, partials, skin, chip (exact, Node only): the real ENGINE block of index.html
// run in Node's vm, on (1) every factory voice of beta's tests/golden/engines.json whose engine is one of those (the preset sheet, the layer-preset-free engine presets, the shipped kits, ELECTRO), fingerprinted as beta's
// engines.json fingerprints them (SHA-256 of the float32 bytes of renderVoice(v, 48000, null, hold)), and (2) direct cases: random parameters of every engine (the ranges of the engine cards, XUI), the clamps,
// other sample rates, hold, the pad effects, through the real renderVoice.
//   node native/tools/engine-xeng-dump.mjs --fixture <betaRoot>   (by hand, needs a checkout of `beta`) writes native/tests/golden/engine_xeng_voices.json (the voices: only what differs from mkVoice's defaults,
//        and the engine's parameter object), engine_xeng_voices_golden.inc (beta's sha256 and length of each, copied, never made here) and xeng_ranges.json (min / max / curve / default of every engine parameter: XUI).
//   node native/tools/engine-xeng-dump.mjs [outDir]               the local Node's reference: <outDir>/ref.json (the voices' hashes by this repo's ENGINE block), <outDir>/cases.json (the direct cases), with --samples
//        <outDir>/u<n>.f32 and <outDir>/c<n>.f32 (the renders, to find the first differing sample).
//   node native/tools/engine-xeng-dump.mjs --golden               writes native/tests/golden/engine_xeng_golden.inc (the direct cases' hashes: sha256 of the real JS's render, Node 22 x86-64 Linux).
//   node native/tools/engine-xeng-dump.mjs --check                exits 1 if the local Node's renders do not give the committed hashes (beta's for the voices, engine_xeng_golden.inc for the cases). Linux only gate.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../..');
const SR = 48000;
const KINDS = ['tone', 'modal', 'swarm', 'drift', 'piano', 'ep', 'organ', 'breath', 'strings', 'fmdrum', 'wind', 'phase', 'partials', 'skin', 'chip'];
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
  vm.runInContext(html.slice(A, B) + '\n;globalThis.__E = {renderVoice, mkVoice, freshFx, FX, XENG, setBpm: b => { globalThis.S = { bpm: b }; }};', ctx, { filename: 'index.html(engine)' });
  return ctx.__E;
}
const defaultsOf = E => { const v = E.mkVoice('x', [], {}); delete v.layers; return JSON.parse(JSON.stringify(v)); };
// a voice as the fixture keeps it: what differs from mkVoice's defaults (the layers are not read by an engine voice), the pad effects
function slim(v, D) {
  const out = { v: {}, fx: (v.fx || []).map(f => ({ ...f })) };
  for (const k of Object.keys(v)) if (k !== 'layers' && k !== 'fx' && k !== 'name' && !same(v[k], D[k])) out.v[k] = JSON.parse(JSON.stringify(v[k]));
  return out;
}
const build = (E, s, name) => { const v = E.mkVoice(name, [], JSON.parse(JSON.stringify(s.v))); v.fx = s.fx.map(f => ({ ...f })); return v; };

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
    const r = E.renderVoice(build(E, s, 'x'), SR, null, b.hold);
    if (sha(r) !== w.sha256 || r.length !== w.len) throw new Error('fixture: the slim voice does not reproduce the golden: ' + it.key);
    voices.push({ k: it.key, hold: b.hold, ...s }); rows.push(`  {${JSON.stringify(it.key)}, ${JSON.stringify(w.sha256)}, ${w.len}},`); per[v.engine] = (per[v.engine] || 0) + 1;
  }
  fs.writeFileSync(path.join(GOLD, 'engine_xeng_voices.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-xeng-dump.mjs --fixture from beta (' + gold.node + '): the factory voices whose engine is tone, modal, swarm, drift, piano, ep, organ, breath, strings, fmdrum, wind, phase, partials, skin or chip. Only what differs from mkVoice defaults (and the engine\'s parameter object).', sampleRate: SR, defaults: D, voices }) + '\n');
  fs.writeFileSync(path.join(GOLD, 'engine_xeng_voices_golden.inc'), ['// GENERATED by native/tools/engine-xeng-dump.mjs --fixture: tests/golden/engines.json of `beta` (made with Node ' + gold.node + ' on x86-64 Linux), the rows of the voices in engine_xeng_voices.json, copied unchanged.',
    '// sha256 of the float32 bytes of renderVoice(v, 48000, null, hold), and its length in samples. The gate of test_engine_xeng on every platform.',
    'struct XengGolden { const char* key; const char* sha256; long len; };', 'static const XengGolden kXengGolden[] = {', ...rows, '};', 'static const int kXengGoldenN = ' + rows.length + ';', ''].join('\n'));
  // the engine cards' parameter ranges (XUI), for the direct cases
  const X = G.engine().XUI, ranges = {};
  for (const k of KINDS) { const d = E.XENG[k].d(); ranges[k] = {}; for (const p of Object.keys(d)) { if (p === 'tune' || p === 'drive' || p === 'level') continue; const r = X[k] && X[k].rows && X[k].rows[p]; ranges[k][p] = r ? { min: r.min, max: r.max, c: r.c, d: d[p] } : { d: d[p] }; } }
  fs.writeFileSync(path.join(GOLD, 'xeng_ranges.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-xeng-dump.mjs --fixture from beta: every engine parameter with the engine card\'s min / max / curve (XUI) and the default (XENG[k].d()). The direct cases draw from it.', ranges }, null, 1) + '\n');
  console.log('wrote ' + rows.length + ' voices ' + JSON.stringify(per));
}

// ---- the direct cases: made from a fixed seed (the same cases every time) -------------------------------------------------------------------------------------------------------------------
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };
function directCases(E) {
  const R = JSON.parse(fs.readFileSync(path.join(GOLD, 'xeng_ranges.json'), 'utf8')).ranges, r = lcg(2811), cases = [];
  const pick = a => a[Math.floor(r() * a.length)], ur = (lo, hi) => lo + r() * (hi - lo), ui = (lo, hi) => Math.floor(ur(lo, hi + 1));
  const draw = q => { if (q.min === undefined) return q.d; const t = r(); if (q.c === 'log') return q.min * Math.pow(q.max / q.min, t); if (q.c === 'int') return Math.round(q.min + t * (q.max - q.min)); return q.min + t * (q.max - q.min); };
  const fxTypes = ['degrade', 'reson', 'pdelay', 'preverb'];
  const mkFx = type => { const f = E.freshFx(type); for (const k in E.FX[type].rows) { const w = E.FX[type].rows[k], t = r(); f[k] = w.c === 'logf' || w.c === 'log' ? w.min * Math.pow(w.max / w.min, t) : w.min + t * (w.max - w.min); if (['bits', 'note', 'chord', 'down', 'time'].includes(k) && r() < 0.5) f[k] = Math.round(f[k]); } if ('mode' in f) f.mode = ui(0, type === 'preverb' ? 2 : type === 'pdelay' ? 4 : 1); return f; };
  const add = (name, kind, params, o = {}, sr = SR, hold = 0, fx = []) => cases.push({ name, kind, sr, hold, bpm: 120, v: { engine: kind, [kind]: params, ...o }, fx });
  const PER = 14;
  for (const k of KINDS) for (let i = 0; i < PER; i++) {
    const params = {}; for (const p in R[k]) if (r() < 0.75) params[p] = draw(R[k][p]);
    const o = {}; if (r() < 0.5) o.level = ur(-12, 6); if (r() < 0.4) o.tune = ui(-12, 24); if (r() < 0.35) o.drive = r(); if (r() < 0.5) o.len = ur(0.15, 2.5); if (r() < 0.2) o.gate = ur(100, 900);
    const sr = i % 7 === 6 ? 44100 : i % 9 === 8 ? 32000 : SR, hold = i % 3 === 0 ? 0 : ur(0.1, 1.2);
    add(k + String(i).padStart(2, '0'), k, params, o, sr, hold, i % 7 === 3 ? [mkFx(pick(fxTypes))] : []);
  }
  // edge cases, written out: the clamps and rounds of each engine, defaults only, the extremes of the cards
  const e = (name, k, params, o, sr, hold, fx) => add('edge_' + name, k, params, o, sr, hold, fx);
  for (const k of KINDS) e(k + '_defaults', k, {}, {}, SR, 0);
  e('tone_waves', 'tone', { wave: 4, spectra: 0.8, nType: 3, nRes: 0.9, dist: 0.7, nSweep: 1 }, { drive: 0.4 }); for (let w = 0; w < 4; w++) e('tone_wave' + w, 'tone', { wave: w, spectra: 0.5, nType: w, nSweep: 0.6, dist: w === 2 ? 1 : 0 }, { level: -3 });
  for (let m = 0; m < 12; m++) e('modal_mat' + m, 'modal', { mat: m, exc: m % 5, modes: 24, inharm: m / 12, noise: 0.3, hard: 0.2 + 0.07 * m }, { level: -2 }, SR, m % 2 ? 0.8 : 0);
  e('modal_clamp', 'modal', { mat: 14.7, modes: 40, exc: 7, oct: 7 }, {}, SR, 0.3); e('modal_lowmodes', 'modal', { mat: -2, modes: 0, exc: 3 }, {});
  for (let w = 0; w <= 3; w += 0.75) e('swarm_wave' + w, 'swarm', { wave: w, voices: 48, scatter: 0.6, ivl: Math.floor(w * 2), bloom: 400, shimmer: 0.5, sub: 0.5, spread: 0.8, cohes: w / 3 }, { len: 1.2 }, SR, 0.8);
  e('swarm_one', 'swarm', { voices: 1, wave: 9 }, {}); e('swarm_clamp', 'swarm', { voices: 400, ivl: 12, wave: -3, sus: 3 }, {}, SR, 0.5);
  e('drift_full', 'drift', { voices: 4, aPulse: 0.7, bSaw: 0.5, ring: 0.5, noise: 0.3, sub: 0.4, vib: 0.5, age: 1, chorus: 1, sat: 1, hiss: 1, wow: 1, flutter: 1 }, {}, SR, 3.5); e('drift_clean', 'drift', { age: 0, chorus: 0, sat: 0, hiss: 0, wow: 0, flutter: 0, voices: 1 }, {}, SR, 0.4);
  e('drift_96k', 'drift', { voices: 3, age: 0.8 }, { len: 1 }, 96000, 0.5);
  for (let i = 0; i < 4; i++) e('piano' + i, 'piano', { oct: i + 1, strings: i, pedal: i % 2, hard: i / 3, bright: 1 - i / 3, inharm: i / 3, thump: 0.8 }, { drive: i === 3 ? 0.8 : 0 }, SR, 0.6);
  e('piano_high', 'piano', { oct: 6, strings: 3 }, {}, SR, 0.5); e('ep_trem', 'ep', { trem: 0.8, hard: 1, bark: 1, oct: 4 }, {}, SR, 0.5); e('ep_low', 'ep', { oct: 0, tone: 0 }, {}, SR, 0.5);
  for (let i = 0; i < 3; i++) e('organ_rot' + i, 'organ', { rotary: i, d1: 8, d2: 8, d3: 8, d4: 6, d5: 5, d6: 4, d7: 3, d8: 2, d9: 1, perc: i, pluck: 0.6 * i, click: 0.6 }, {}, SR, 0.5);
  e('organ_high', 'organ', { oct: 6, d1: 0, d2: 0, d3: 8, d9: 8 }, {}, SR, 0.4); e('organ_none', 'organ', { d1: 0, d2: 0, d3: 0, perc: 2 }, {}, SR, 0.3);
  e('breath_grain', 'breath', { grain: 0.8, grate: 120, color: 0.1, wind: 0.8, sweep: 0.6, bands: 8, metal: 1, q: 0.9, sus: 0.5, fenv: 0.5 }, {}, SR, 0.6); e('breath_blue', 'breath', { color: 1, bands: 1, body: 1 }, {}, SR, 0.3);
  for (let c = 0; c < 9; c++) e('strings_chord' + c, 'strings', { chord: c, voices: 1 + c % 6, down: c % 2, strum: 5 + 7 * c, vib: 0.4, pick: 0.3, pos: 0.5, damp: 0.2, size: c / 9 }, {}, SR, 0.5);
  e('strings_low', 'strings', { oct: 0, voices: 6, decay: 4 }, { tune: -12 }, SR, 1);
  for (let a = 0; a < 3; a++) e('fmdrum_alg' + a, 'fmdrum', { alg: a, fb: 0.6, noise: 0.5, click: 0.5, curve: a / 2, i1: 0.9, i2: 0.8 }, { drive: 0.3 });
  for (let m = 0; m < 3; m++) for (const [j, hold] of [[0, 0], [1, 0.6]]) e('wind_m' + m + '_' + j, 'wind', { model: m, breath: 0.4 + 0.2 * m, vib: 0.5, growl: 0.4, noise: 0.3, body: 0.8, stiff: 0.8, pos: 0.7, oct: 2 + m }, {}, SR, hold || 0.3);
  e('wind_low', 'wind', { oct: 0, model: 0 }, {}, SR, 0.3); e('wind_high', 'wind', { oct: 6, model: 1 }, {}, SR, 0.3); e('wind_44k', 'wind', { model: 2, oct: 3 }, {}, 44100, 0.3);
  for (let w = 0; w < 8; w++) e('phase_w' + w, 'phase', { wave: w, wave2: 7 - w, mix: 0.4, det: 0.7, ring: 0.5, dcw: 1, noise: 0.2, key: 0.8 }, {}, SR, 0.4);
  e('partials_max', 'partials', { count: 300, famt: 1, comb: 0.8, stretch: 1, morph: 1, drift: 0.9, tilt: 0.2 }, {}, SR, 0.6); e('partials_combneg', 'partials', { comb: -0.7, count: 24, famt: 0.5, fmt: 300 }, {}, SR, 0.5); e('partials_high', 'partials', { oct: 6, count: 64 }, {}, SR, 0.4);
  e('skin_full', 'skin', { size: 1, tension: 1, hard: 1, damp: 0, muffle: 0, shell: 1, noise: 0.8, pos: 0.02 }, {}); e('skin_small', 'skin', { size: 0, pos: 1, hard: 0, muffle: 1 }, {}); e('skin_mid', 'skin', { size: 0.6, pos: 0.5 }, { level: -4, drive: 0.5 });
  for (let w = 0; w < 3; w++) e('chip_w' + w, 'chip', { wave: w, duty: w + 1, sweep: 2, chord: 3 + w, arp: 2, slide: 0.5, vib: 0.7, quant: w % 2, nshort: w % 2, nfreq: 0.3 + 0.3 * w, sus: 0.8, atk: 3, dec: 6 }, {}, SR, 0.5);
  e('chip_noshort', 'chip', { wave: 2, nshort: 1, nfreq: 1 }, {}, SR, 0.4); e('chip_chords', 'chip', { chord: 8, arp: 1, quant: 1 }, {}, 22050, 0.4);
  // the pad effects on engines
  for (const [i, k] of ['tone', 'modal', 'chip', 'fmdrum'].entries()) e('fx_' + k, k, {}, { tail: 1 + i }, SR, 0, [mkFx(fxTypes[i]), { type: 'room', on: true }]);
  return cases;
}
function renderCase(E, c) { const v = E.mkVoice(c.name, [], JSON.parse(JSON.stringify(c.v))); v.fx = c.fx.map(f => ({ ...f })); E.setBpm(c.bpm); try { return E.renderVoice(v, c.sr, null, c.hold); } finally { E.setBpm(120); } }

function main(argv) {
  if (argv[0] === '--fixture') return fixture(path.resolve(argv[1] || '/home/claude/wt/beta-ref'));
  const check = argv[0] === '--check', golden = argv[0] === '--golden', withSamples = argv.includes('--samples'), outDir = (check || golden) ? null : argv.filter(a => !a.startsWith('--'))[0] || '/tmp/u28/engine-ref';
  const E = engineOf(ROOT), fx = JSON.parse(fs.readFileSync(path.join(GOLD, 'engine_xeng_voices.json'), 'utf8'));
  const inc = fs.readFileSync(path.join(GOLD, 'engine_xeng_voices_golden.inc'), 'utf8'), casesPath = path.join(GOLD, 'engine_xeng_golden.inc');
  const gold = new Map([...inc.matchAll(/\{("(?:[^"\\]|\\.)*"), "([0-9a-f]{64})", (\d+)\}/g)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = []; let bad = 0;
  fx.voices.forEach((s, i) => {
    const r = E.renderVoice(build(E, s, s.k), SR, null, s.hold), h = sha(r), g = gold.get(s.k);
    if (check && (!g || g.sha !== h || g.len !== r.length)) { bad++; console.error('DIFFERS from the golden: ' + s.k); }
    rows.push({ k: s.k, sha256: h, len: r.length });
    if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'u' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength));
  });
  const cases = directCases(E), crow = cases.map((c, i) => { const r = renderCase(E, c); if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'c' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength)); return { name: c.name, sha256: sha(r), len: r.length }; });
  if (golden) {
    fs.writeFileSync(casesPath, ['// GENERATED by native/tools/engine-xeng-dump.mjs --golden from the real ENGINE block (the direct cases of directCases): sha256 of renderVoice(v, sr, null, hold) and its length, Node 22 on x86-64 Linux.',
      '// Do not edit: a mismatch means the port is wrong (or the JS changed on purpose: regenerate with --golden and say so).',
      'struct XengCaseGolden { const char* name; const char* sha256; long len; };', 'static const XengCaseGolden kXengCaseGolden[] = {', ...crow.map(r => `  {${JSON.stringify(r.name)}, ${JSON.stringify(r.sha256)}, ${r.len}},`), '};',
      'static const int kXengCaseGoldenN = ' + crow.length + ';', ''].join('\n'));
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
