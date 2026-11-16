// U22: THE REFERENCE OF THE ENGINE FOUNDATION (exact, Node only): the real ENGINE block of index.html (renderVoice, renderLayer, makeOsc, voiceFilter, voiceAmp, voiceMod, mkVoice,
// padFx / applyFx's JS effects) run in Node's vm on the factory voices that are built only from the layer types sine, tri, saw, square, pulse and the noise family
// (white, pink, brown, blue, metal, lfsr, dust, grit), exactly as tests/engine-golden.test.js renders them (renderVoice(v, 48000, null, hold)), fingerprinted the same way
// (SHA-256 of the float32 bytes). The gate is tests/golden/engines.json of `beta`: the same hash, bit for bit.
//   node native/tools/engine-dump.mjs --fixture <betaRoot>   (by hand, needs a checkout of `beta`: <betaRoot>/tools/engine-golden.js and tests/golden/engines.json)
//        writes native/tests/golden/engine_voices.json (the voices: only what differs from mkVoice / freshLayer defaults, the layers' seeds, the pad effects) and
//        native/tests/golden/engine_golden.inc (the golden's sha256 and length for each of them, copied from beta's engines.json, never made here).
//   node native/tools/engine-dump.mjs [outDir]               the reference of the local Node: <outDir>/ref.json (sha256 + length of every voice rendered by THIS repo's ENGINE block),
//        <outDir>/tables.json (the tables, the defaults of mkVoice / freshLayer, the layer seed counter, sinT / rng / blep / hashRnd samples) and, with --samples, <outDir>/<n>.f32
//        (the render of voice n, to find the first differing sample when the gate fails).
//   node native/tools/engine-dump.mjs --golden               writes native/tests/golden/engine_cases_golden.inc (the direct cases' hashes: sha256 of the real JS's render, Node 22 x86-64 Linux).
//   node native/tools/engine-dump.mjs --check                exits 1 if the local Node's render of any fixture voice does not give the committed golden hash (this repo's ENGINE block
//        against beta's golden: the proof that the fixture and the golden still describe the engine); Linux only gate, as the other goldens.
// WHERE THE REFERENCE COMES FROM: the engine calls Math.sin / cos / exp / pow / tan / tanh in the audio loop. Node's Math.* is not the same on every platform, so the committed
// golden is x86-64 Linux, Node 22 (as beta's engines.json says) and the C++ is compared with it on every platform.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
export const ROOT = path.resolve(HERE, '../..');
const SR = 48000;
const OK_TYPES = new Set(['sine', 'tri', 'saw', 'square', 'pulse', 'white', 'pink', 'brown', 'blue', 'metal', 'lfsr', 'dust', 'grit']);

// The ENGINE block as a standalone script (the way tools/drums/render.mjs runs it), plus the names this tool wants out of it.
export function engineOf(root) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
  const A = html.indexOf('/* ===ENGINE START=== */'), B = html.indexOf('/* ===ENGINE END=== */');
  if (A < 0 || B < 0) throw new Error('engine markers not found');
  const ctx = vm.createContext({ console: { log() {}, warn() {}, error() {} }, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object, JSON, Map, Set, WeakMap, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
  ctx.globalThis = ctx; ctx.self = ctx;
  vm.runInContext(html.slice(A, B) + '\n;globalThis.__E = {renderVoice, mkVoice, freshLayer, voiceMod, voiceLen, layerLen, sinT, rng, blep, frac, hashRnd, SIN, TS, BASE, RATIOS, CHORDS, MEM, BELL, BELLA, BAR, HAT, SYNC_BEATS, FX, fxReady, freshFx, setBpm: b => { globalThis.S = { bpm: b }; }, setSeed: n => { _seed = n >>> 0; }, getSeed: () => _seed};', ctx, { filename: 'index.html(engine)' });
  return ctx.__E;
}

const sha = a => crypto.createHash('sha256').update(Buffer.from(a.buffer, a.byteOffset, a.byteLength)).digest('hex');
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

// a voice as the fixture keeps it: what differs from the defaults, the layers that render (on and level > 0, in order), their seeds always
function slim(v, E, D) {
  const out = { v: {}, L: [], fx: (v.fx || []).map(f => ({ ...f })) };
  for (const k of Object.keys(v)) if (k !== 'layers' && k !== 'fx' && k !== 'name' && !same(v[k], D.voice[k])) out.v[k] = v[k];
  for (const l of v.layers) if (l.on && l.level > 0) { const o = {}; for (const k of Object.keys(l)) if (k === 'seed' || k === 'on' || !same(l[k], D.layer[k])) o[k] = l[k]; out.L.push(o); }
  return out;
}
function defaultsOf(E) { const v = E.mkVoice('x', [], {}), l = E.freshLayer(); delete v.layers; return { voice: JSON.parse(JSON.stringify(v)), layer: JSON.parse(JSON.stringify(l)) }; }
// the voice the reference renders: the real mkVoice over the slim description (the seeds are given, so the counter does not matter)
function build(E, s, name) { const layers = s.L.map(l => ({ ...l })); const v = E.mkVoice(name, layers, JSON.parse(JSON.stringify(s.v))); v.fx = s.fx.map(f => ({ ...f })); return v; }

const holdOf = (v) => ((v.engine && v.engine !== 'layers' && !['tone', 'fmdrum', 'skin', 'breath', 'blip'].includes(v.engine)) || (v.layers || []).some(l => l.on && l.mode === 'ADSR')) ? 1 : 0;

function fixture(betaRoot) {
  const G = createRequire(import.meta.url)(path.join(betaRoot, 'tools/engine-golden.js'));
  const gold = JSON.parse(fs.readFileSync(path.join(betaRoot, 'tests/golden/engines.json'), 'utf8'));
  const E = engineOf(betaRoot), D = defaultsOf(E), voices = [], rows = [];
  let n = 0;
  for (const it of G.listVoices()) {
    const w = gold.voices[it.key]; if (!w || w.error) continue;
    G.engine().setSeed(G.seedFor(it.key));   // the layers' seeds come from a counter that every voice built bumps: pinned per voice, as the golden does
    const b = it.build(), v = b.v;
    if (v.engine && v.engine !== 'layers') continue;
    const on = v.layers.filter(l => l.on && l.level > 0);
    if (on.some(l => !OK_TYPES.has(l.type))) continue;
    const s = slim(v, E, D), hold = b.hold;
    // the check at generation time: the slim voice, rebuilt through mkVoice and rendered by beta's engine, gives beta's golden hash
    const r = E.renderVoice(build(E, s, 'x'), SR, null, hold);
    if (sha(r) !== w.sha256 || r.length !== w.len) throw new Error('fixture: the slim voice does not reproduce the golden: ' + it.key);
    voices.push({ k: it.key, hold, ...s }); rows.push(`  {${JSON.stringify(it.key)}, ${JSON.stringify(w.sha256)}, ${w.len}},`); n++;
  }
  const outDir = path.join(ROOT, 'native/tests/golden');
  fs.writeFileSync(path.join(outDir, 'engine_voices.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-dump.mjs --fixture from beta (' + gold.node + '): the factory voices built only from sine, tri, saw, square, pulse and the noise layer types. Only what differs from mkVoice / freshLayer defaults.', sampleRate: SR, defaults: D, voices }) + '\n');
  const inc = ['// GENERATED by native/tools/engine-dump.mjs --fixture: tests/golden/engines.json of `beta` (made with Node ' + gold.node + ' on x86-64 Linux), the rows of the voices in engine_voices.json, copied unchanged.',
    '// sha256 of the float32 bytes of renderVoice(v, 48000, null, hold) (a stereo render: left then right), and its length in samples. The gate of test_engine on every platform.',
    'struct EngineGolden { const char* key; const char* sha256; long len; };', 'static const EngineGolden kEngineGolden[] = {', ...rows, '};', 'static const int kEngineGoldenN = ' + rows.length + ';', ''].join('\n');
  fs.writeFileSync(path.join(outDir, 'engine_golden.inc'), inc);
  console.log('wrote ' + n + ' voices');
}


// ---- the direct cases: random voices through the real renderVoice (every branch the factory voices do not reach: LFO shapes and syncs, envelope and sequence routes, the voice filter and amp, drive,
// burst, delay, the four pad effects, other sample rates, hold) and voiceMod probes. Made from a fixed seed: the same cases every time; the C++ reads them from <outDir>/cases.json.
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };
function directCases(E) {
  const r = lcg(2211), D = defaultsOf(E), cases = [], probes = [];
  const pick = a => a[Math.floor(r() * a.length)], ur = (lo, hi) => lo + r() * (hi - lo), ui = (lo, hi) => Math.floor(ur(lo, hi + 1));
  const types = [...OK_TYPES], fxTypes = ['degrade', 'reson', 'pdelay', 'preverb'];
  const mkFx = type => { const f = E.freshFx(type); for (const k in E.FX[type].rows) { const w = E.FX[type].rows[k], t = r(); f[k] = w.c === 'logf' || w.c === 'log' ? w.min * Math.pow(w.max / w.min, t) : w.min + t * (w.max - w.min); if (['bits', 'note', 'chord', 'down', 'time'].includes(k) && r() < 0.5) f[k] = Math.round(f[k]); } if ('mode' in f) f.mode = ui(0, type === 'preverb' ? 2 : type === 'pdelay' ? 4 : 1); return f; };
  const layer = adsrOk => { const l = { on: true, type: pick(types), a: r(), b: r(), level: ur(0.2, 1), tune: ui(-24, 36), fine: ui(-60, 60), pAmt: r() < 0.5 ? 0 : ui(-30, 40), pDec: ur(5, 300), mode: adsrOk && r() < 0.5 ? 'ADSR' : 'AHD',
      atk: ur(0.1, 40), hold: r() < 0.3 ? ur(0, 80) : 0, dec: ur(20, 500), sus: r(), rel: ur(5, 300), lpCut: r() < 0.5 ? 20000 : ur(200, 12000), lpRes: r() < 0.4 ? r() : 0, lpEnv: r() < 0.3 ? ur(-1, 1) : 0,
      hpCut: r() < 0.5 ? 20 : ur(30, 4000), hpRes: r() < 0.3 ? r() : 0, hpEnv: r() < 0.3 ? ur(-1, 1) : 0, fAtk: ur(0.1, 30), fDec: ur(10, 400), delay: r() < 0.3 ? ur(0, 40) : 0, burst: r() < 0.3 ? ui(1, 4) : 0, spacing: ur(4, 20), seed: ui(1, 4294967295) };
    return l; };
  for (let i = 0; i < 160; i++) {
    const adsr = r() < 0.4, sr = i % 8 === 7 ? 44100 : i % 11 === 10 ? 96000 : i % 13 === 12 ? 32000 : 48000;
    const layers = Array.from({ length: ui(1, 4) }, () => layer(adsr)); if (i < 13) layers.forEach(l => { l.type = types[i % types.length]; });   // every type alone, with the others around it
    const o = { level: ur(-12, 6), tune: r() < 0.5 ? 0 : ui(-12, 24), drive: r() < 0.4 ? r() : 0, gate: ur(50, 500), len: ur(0.1, 0.6), tail: ur(0, 1.2) };
    if (r() < 0.4) Object.assign(o, { gLpCut: ur(300, 15000), gLpRes: r() < 0.5 ? r() : 0, gLpEnv: r() < 0.5 ? ur(-1, 1) : 0, gFAtk: ur(0.1, 30), gFDec: ur(10, 400) });
    if (r() < 0.4) Object.assign(o, { gAmpOn: true, gMode: r() < 0.5 ? 'ADSR' : 'AHD', gAtk: ur(0.1, 30), gHold: r() < 0.3 ? ur(0, 80) : 0, gDec: ur(20, 400), gSus: r(), gRel: r() < 0.2 ? 0 : ur(5, 300), gAmpAmt: r() < 0.3 ? ur(0, 1) : 1 });
    if (r() < 0.3) Object.assign(o, { gPAmt: ui(-24, 24), gPDec: r() < 0.2 ? 0 : ur(5, 300) });
    // modulation: LFOs of every shape (some tempo-synced), envelopes, the mod sequence, routed to pitch / level / lp / hp
    if (r() < 0.6) {
      for (let k = 1; k <= 3; k++) if (r() < 0.6) Object.assign(o, { ['lfo' + k + 'Rate']: ur(0.2, 20), ['lfo' + k + 'Shape']: ui(0, 5), ['lfo' + k + 'Depth']: r(), ['lfo' + k + 'Sync']: r() < 0.3 ? ui(1, 8) : 0, ['r' + k + 'dst']: ui(0, 4), ['r' + k + 'amt']: ur(-12, 12) });
      for (let k = 1; k <= 3; k++) if (r() < 0.5) Object.assign(o, { ['env' + k + 'A']: ur(1, 80), ['env' + k + 'D']: ur(10, 400), ['env' + k + 'S']: r(), ['env' + k + 'R']: ur(10, 300), ['r' + (3 + k) + 'dst']: ui(0, 4), ['r' + (3 + k) + 'amt']: ur(-12, 12) });
      if (r() < 0.5) Object.assign(o, { seq: Array.from({ length: 32 }, () => r() < 0.3 ? 0 : ur(-1, 1)), seqRate: ur(1, 30), seqSync: r() < 0.3 ? ui(1, 8) : 0, seqDepth: r(), r7dst: ui(0, 4), r7amt: ur(-12, 12) });
    }
    const v = E.mkVoice('case', layers, o);
    v.fx = r() < 0.5 ? Array.from({ length: ui(1, 2) }, () => mkFx(pick(fxTypes))) : [];
    if (v.fx.length && r() < 0.2) v.fx.push({ type: 'room', on: true });   // an effect that is not ready here (the golden's Node has no tfx wasm): passes the sound through
    const bpm = r() < 0.3 ? ur(60, 180) : 120, hold = adsr || r() < 0.2 ? ur(0.05, 0.5) : 0;
    cases.push({ name: 'case' + String(i).padStart(3, '0'), sr, hold, bpm, ...slim(v, E, D) });
  }
  // voiceMod probes: the modulation matrix evaluated at many times, against the real voiceMod (NaN is written null)
  for (let i = 0; i < 60; i++) {
    const o = { gate: ur(50, 500) }; for (let k = 1; k <= 3; k++) Object.assign(o, { ['lfo' + k + 'Rate']: ur(0, 20), ['lfo' + k + 'Shape']: ui(0, 6), ['lfo' + k + 'Depth']: r(), ['lfo' + k + 'Sync']: r() < 0.3 ? ui(0, 9) : 0, ['r' + k + 'dst']: ui(0, 5), ['r' + k + 'amt']: ur(-12, 12) });
    for (let k = 1; k <= 3; k++) Object.assign(o, { ['env' + k + 'A']: r() < 0.2 ? 0 : ur(1, 80), ['env' + k + 'D']: ur(10, 400), ['env' + k + 'S']: r(), ['env' + k + 'R']: r() < 0.2 ? 0 : ur(10, 300), ['r' + (3 + k) + 'dst']: ui(0, 5), ['r' + (3 + k) + 'amt']: ur(-12, 12) });
    Object.assign(o, { seq: Array.from({ length: 32 }, () => r() < 0.3 ? 0 : ur(-1, 1)), seqRate: r() < 0.2 ? 0 : ur(1, 30), seqSync: r() < 0.3 ? ui(0, 9) : 0, seqDepth: r(), r7dst: ui(0, 5), r7amt: ur(-12, 12) });
    const v = E.mkVoice('probe', [], o), bpm = i % 3 ? 120 : ur(40, 200); E.setBpm(bpm);
    const ts = [0, 0.0001, 0.001, 0.0123, 0.05, 0.1, 0.2, 0.333, 0.5, 0.75, 1, 1.5, 2.2, 3.7, 10.1, 31.4159, 100.5, 1000.25].concat(Array.from({ length: 12 }, () => ur(0, 4)));
    probes.push({ v: slim(v, E, D).v, bpm, t: ts, m: ts.map(t => { const m = E.voiceMod(v, t); return [m.pitch, m.level, m.lpcut, m.hpcut]; }) });
  }
  E.setBpm(120);
  return { cases, probes };
}
function renderCase(E, c) { const v = build(E, c, c.name); E.setBpm(c.bpm); try { return E.renderVoice(v, c.sr, null, c.hold); } finally { E.setBpm(120); } }

function seedSeq(E, s0, k) { E.setSeed(s0); const o = []; for (let i = 0; i < k; i++) o.push(E.freshLayer().seed); return o; }
function samples(E) {
  const out = { sinT: [], rng: [], blep: [], hashRnd: [] };
  for (const p of [0, 0.1, 0.25, 0.4999999, 1 - 1e-12, 1.0000001, -0.3, -1e-20, 7.77, -123.456, 0.123456789]) out.sinT.push([p, E.sinT(p)]);
  for (const sd of [0, 1, 7, 1234567, 4294967295, 3735928559, 4294967296 + 5]) { const r = E.rng(sd), a = []; for (let i = 0; i < 16; i++) a.push(r()); out.rng.push([sd, a]); }
  for (const [t, dt] of [[0, 0.01], [0.005, 0.01], [0.5, 0.01], [0.995, 0.01], [0.9999, 0.01], [0.3, 0.5], [0.99, 0.2], [0, 0.5]]) out.blep.push([t, dt, E.blep(t, dt)]);
  for (const [n, k] of [[0, 1], [1, 1], [5, 2], [123456, 3], [-3, 1], [987654321, 2], [1e9, 3]]) out.hashRnd.push([n, k, E.hashRnd(n, k)]);
  return out;
}
function tables(E) {
  const D = defaultsOf(E);
  return { sin: Array.from(E.SIN), TS: E.TS, BASE: E.BASE, RATIOS: E.RATIOS, CHORDS: E.CHORDS, MEM: E.MEM, BELL: E.BELL, BELLA: E.BELLA, BAR: E.BAR, HAT: E.HAT, SYNC_BEATS: E.SYNC_BEATS, defaults: D,
    seeds: [0, 1, 1000, 1103515245, 4294967295, 3735928559].map(s => [s, seedSeq(E, s, 24)]), samples: samples(E) };
}

function casesInc(rows) {
  return ['// GENERATED by native/tools/engine-dump.mjs --golden from the real ENGINE block (the direct cases of directCases): sha256 of renderVoice(v, sr, null, hold) and its length, Node 22 on x86-64 Linux.',
    '// Do not edit: a mismatch means the port is wrong (or the JS changed on purpose: regenerate with --golden and say so).',
    'struct EngineCaseGolden { const char* name; const char* sha256; long len; };', 'static const EngineCaseGolden kEngineCaseGolden[] = {', ...rows.map(r => `  {${JSON.stringify(r.name)}, ${JSON.stringify(r.sha256)}, ${r.len}},`), '};',
    'static const int kEngineCaseGoldenN = ' + rows.length + ';', ''].join('\n');
}

function main(argv) {
  if (argv[0] === '--fixture') return fixture(path.resolve(argv[1] || '/home/claude/wt/beta-ref'));
  const check = argv[0] === '--check', golden = argv[0] === '--golden', withSamples = argv.includes('--samples'), outDir = (check || golden) ? null : argv.filter(a => !a.startsWith('--'))[0] || '/tmp/u22/engine-ref';
  const E = engineOf(ROOT), fx = JSON.parse(fs.readFileSync(path.join(ROOT, 'native/tests/golden/engine_voices.json'), 'utf8'));
  const incPath = path.join(ROOT, 'native/tests/golden/engine_golden.inc'), casesPath = path.join(ROOT, 'native/tests/golden/engine_cases_golden.inc');
  const inc = fs.readFileSync(incPath, 'utf8');
  const gold = new Map([...inc.matchAll(/\{("(?:[^"\\]|\\.)*"), "([0-9a-f]{64})", (\d+)\}/g)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = []; let bad = 0;
  fx.voices.forEach((s, i) => {
    const v = build(E, s, s.k), r = E.renderVoice(v, SR, null, s.hold), h = sha(r), g = gold.get(s.k);
    if (check && (!g || g.sha !== h || g.len !== r.length)) { bad++; console.error('DIFFERS from the golden: ' + s.k); }
    rows.push({ k: s.k, sha256: h, len: r.length });
    if (outDir && withSamples) fs.writeFileSync(path.join(outDir, i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength));
  });
  const D = directCases(E), crow = D.cases.map((c, i) => { const r = renderCase(E, c); if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'case' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength)); return { name: c.name, sha256: sha(r), len: r.length }; });
  if (golden) { fs.writeFileSync(casesPath, casesInc(crow)); console.log('wrote ' + casesPath + ' (' + crow.length + ' cases)'); return; }
  if (check) {
    if (fs.readFileSync(casesPath, 'utf8') !== casesInc(crow)) { bad++; console.error('engine_cases_golden.inc differs from what the JS gives now'); }
    if (bad) { console.error(bad + ' difference(s)'); process.exit(1); }
    console.log('engine golden is current (' + fx.voices.length + ' voices, ' + crow.length + ' direct cases)'); return; }
  fs.writeFileSync(path.join(outDir, 'ref.json'), JSON.stringify({ voices: rows, cases: crow }));
  fs.writeFileSync(path.join(outDir, 'cases.json'), JSON.stringify(D));
  fs.writeFileSync(path.join(outDir, 'tables.json'), JSON.stringify(tables(E)));
  console.log('wrote ' + rows.length + ' voices and ' + crow.length + ' direct cases to ' + outDir);
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
