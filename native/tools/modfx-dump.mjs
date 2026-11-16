// U19: THE REFERENCE OF THE MODFX MODULES (exact, Node only): the real chains/chains-worklet.js (the shared module block, as it ships) run in Node.
//   node native/tools/modfx-dump.mjs [outDir]    writes <outDir>/defs.json (the module tables of the JS), and per direct case <name>.json (the case), <name>.in.f32 (every input port,
//                                                every block, 128 floats each), <name>.f32 (every output port, every block): the module driven directly with create() / process()
//   node native/tools/modfx-dump.mjs --golden    writes native/tests/golden/modfx_golden.inc: FNV-1a 64 of every direct case's output, and the 32-bit hashes of chains-modules.json for these modules
//                                                with the nextSeed() values each render took (see below)
//   node native/tools/modfx-dump.mjs --check     exits 1 if the committed modfx_golden.inc differs from what the JS gives now, or the engine run no longer gives tests/golden/chains-modules.json
// TWO REFERENCES. (1) The engine run: exactly tests/chains.test.js's renderModule (trackIn -> module -> chainOut through the real ChainsEngine, 400 blocks, the three parameter sets
// default / stressed / switches-on, modules in sorted order in one engine) for every module; the hashes must equal tests/golden/chains-modules.json (this tool fails if they do not), and the
// C++ driver reproduces those committed hashes, which is the gate of the unit. The only change to the source that runs is the text of nextSeed(), which also records every value it hands out
// (an engine run's modules take their seeds from one counter that earlier modules advanced: the C++ driver is given the values this run recorded, per render). (2) The direct run: each module's
// create() and process() called with an io made here (random parameters changing mid-run, signals with bursts, gates and slow CV, linked and unlinked inputs, block sizes below 128, tempo and
// start changes, other sample rates, tails and silences), recording every output port: the C++ is compared sample by sample and with the committed hash.
// WHERE THE REFERENCE COMES FROM: the modules call Math.sin / cos / tan / exp / pow / tanh; Node's Math.* is not the same on every platform, so the committed golden is made on x86-64 Linux,
// Node 22 (as U15 / U16), and the C++ is gated on it everywhere; the comparison with the local run must hold on x86-64 Linux and is reported elsewhere.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { MODS_B, casesB, irTable } from './modfx-b-cases.mjs';   // U20: the cases of Spectral, Extreme stretch and Convolution reverb (own file)

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
const H = require(path.join(ROOT, 'tests/helpers.js'));
const GOLDEN_INC = path.join(ROOT, 'native/tests/golden/modfx_golden.inc');

// the modules of U19 (the C++ registry holds exactly these; test_modfx fails if either list has one the other has not)
export const MODS = ['meta', 'phaser', 'width', 'pingpong', 'meter', 'limiter', 'utility', 'ringmod', 'ensemble', 'spring', 'plate', 'lverb',
  'envfol', 'gateex', 'slew', 'snh', 'quant', 'math', 'logic', 'steps', 'clock', 'clkmul', 'chance', 'counter', 'stepsw', 'burst', 'gatedly', 'flipflop', 'rect', 'scale', 'compare', 'switch', 'xfade', 'const', 'divider', 'euclid',
  'blades', 'onepole', 'allpass', 'dcblock', 'midside', 'dline', 'dimension', 'bbd',
  'harm', 'blipdrum', 'life', 'reson', 'degrade', 'flex', 'sgate', 'retrig', 'pitchfol', 'pitchtrk'];

// ---- the chains worklet, with nextSeed() recording --------------------------------------------------------------------------------------------------------------------------------
const NEEDLE = 'const nextSeed = () => (seedN = (seedN * 1103515245 + 12345) >>> 0);';
const PATCH = 'const nextSeed = () => { const q = globalThis.__seedq; if (q) return q.shift(); const v = (seedN = (seedN * 1103515245 + 12345) >>> 0); (globalThis.__seedlog || (globalThis.__seedlog = [])).push(v); return v; };';
function loadChains() {
  const src = fs.readFileSync(path.join(ROOT, 'chains/chains-worklet.js'), 'utf8');
  if (!src.includes(NEEDLE)) throw new Error('modfx-dump: chains-worklet.js no longer has the nextSeed() text this tool patches');
  const ctx = H.freezeClock(H.workletScope());
  vm.runInContext(src.replace(NEEDLE, PATCH), ctx, { filename: 'chains/chains-worklet.js' });
  return ctx;
}

// ---- (1) the engine run: tests/chains.test.js's driver ------------------------------------------------------------------------------------------------------------------------------
const BLOCKS = 400;
function engineRun() {
  const ctx = loadChains(), TFX = new Uint8Array(fs.readFileSync(path.join(ROOT, 'chains/dsp/tfx.wasm'))), PUMP = new Uint8Array(fs.readFileSync(path.join(ROOT, 'pump/pump.wasm')));
  ctx.ChainsDSP.wasmInit({ tfxBytes: TFX }); ctx.__pumpBytes = PUMP;
  const E = new ctx.ChainsDSP.ChainsEngine(48000, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }), M = ctx.ChainsDSP.MODULES;
  function renderModule(type, over) {   // copied from tests/chains.test.js (renderModule, hashOf)
    const m = M[type], params = {}; for (const [k, d] of Object.entries(m.params || {})) params[k] = d[0]; Object.assign(params, over);
    const aIn = Object.keys(m.inputs || {}).filter(n => m.inputs[n] === 'audio'), outs = Object.keys(m.outputs || {}), cab = [];
    if (aIn.includes('in')) cab.push({ from: ['a', 'out'], to: ['b', 'in'] }); if (aIn.includes('inR')) cab.push({ from: ['a', 'outR'], to: ['b', 'inR'] });
    const sink = ['in', 'inR']; outs.slice(0, 2).forEach((p, i) => cab.push({ from: ['b', p], to: ['c', sink[i]] }));
    const probe = m.probe === true; if (probe) { cab.push({ from: ['a', 'out'], to: ['b', 'a'] }, { from: ['a', 'outR'], to: ['b', 'b'] }); }
    E.loadPatch({ modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type, scope: 'mono', params }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }], cables: cab });
    if (probe) E.handleMessage({ op: 'telemetry', value: true });
    const ports = probe ? ['show'] : Object.keys(m.outputs || {}), rec = {}; for (const p of ports) rec[p] = new Float32Array(probe ? 3 + 512 : 128 * BLOCKS);
    const sig = H.testSignal(128 * BLOCKS), L = new Float32Array(128 * BLOCKS), R = new Float32Array(128 * BLOCKS);
    for (let b = 0; b < BLOCKS; b++) {
      const s = sig.subarray(b * 128, (b + 1) * 128); E.setTrackInputs(s, s); E.process(128);
      L.set(E.bus(1), b * 128); R.set(E.busR(1), b * 128);
      const io = E._ios.get('b'); if (io && !probe) for (const p of ports) rec[p].set(io.out[p], b * 128);
    }
    if (probe) { const sh = E.telemetry().shows.find(x => x[0] === 'b'); if (sh) rec.show.set(sh[1]); E.handleMessage({ op: 'telemetry', value: false }); }
    if (E.quality !== 'full') throw new Error('engine shed to draft quality while rendering ' + type);
    return H.hashFloats([L, R, ...ports.map(p => rec[p])]);
  }
  const stress = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (mx - mn === 1 && st === 1) { o[k] = mn; continue; } let v = mn + (mx - mn) * .7; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
  const SILENCERS = new Set(['mute']), KEEP_DEFAULT = new Set(['flip']);
  const switchesOn = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (KEEP_DEFAULT.has(k)) { o[k] = d[0]; continue; } if (mx - mn === 1 && st === 1) { o[k] = SILENCERS.has(k) ? mn : mx; continue; } let v = mn + (mx - mn) * .3; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
  const SKIP = new Set(['trackIn', 'chainOut']), out = {}, seeds = {};
  for (const type of Object.keys(M).sort()) {
    const m = M[type]; if (!m || !m.process || SKIP.has(type)) continue;
    for (const [set, over] of [['default', undefined], ['stressed', stress(m)], ['switches-on', switchesOn(m)]]) {
      ctx.__seedlog = []; out[type + ' @' + set] = renderModule(type, over); seeds[type + ' @' + set] = ctx.__seedlog;
    }
  }
  return { hashes: out, seeds, M: Object.fromEntries(MODS.map(t => [t, M[t]])) };
}

// ---- (2) the direct run -------------------------------------------------------------------------------------------------------------------------------------------------------------
const lcg = seed => { let s = (seed >>> 0) || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; };
const fnv64 = u8 => { let h = 0xcbf29ce484222325n; const P = 0x100000001b3n, M = 0xffffffffffffffffn; for (let i = 0; i < u8.length; i++) { h ^= BigInt(u8[i]); h = (h * P) & M; } return h; };
const SIGS = ['mix', 'loud', 'quiet', 'impulse', 'burst', 'longsleep'];
// one input signal of n samples: audio, cv or gate, seeded
function signal(kind, sigKind, n, seed, hint) {
  const r = lcg(seed), o = new Float32Array(n);
  if (kind === 'gate') {
    let i = 0, hi = false; const lo = r() < .2 ? .4 : 0, up = r() < .2 ? .6 : 1;
    while (i < n) { const len = Math.floor(200 + r() * 6000) | 0; const w = hi ? Math.max(1, Math.floor(len * (.1 + r() * .5))) : len; for (let j = 0; j < w && i < n; j++, i++) o[i] = hi ? up : lo; hi = !hi; }
    return o;
  }
  if (kind === 'cv') {
    let i = 0, v = r() * 1.2 - .1; while (i < n) { const len = Math.floor(100 + r() * 2000), nv = hint === 'pitch' ? r() * 1.2 - .1 : r() * 2.4 - 1.2, slew = r() < .5; for (let j = 0; j < len && i < n; j++, i++) { o[i] = Math.fround(slew ? v + (nv - v) * (j / len) : v); } v = nv; }
    return o;
  }
  const amp = sigKind === 'loud' ? 3.2 : sigKind === 'quiet' ? .004 : 1, f1 = 80 + r() * 900, f2 = 1000 + r() * 4000;
  let ns = (seed * 2654435761) >>> 0 || 1;
  for (let i = 0; i < n; i++) {
    ns ^= ns << 13; ns >>>= 0; ns ^= ns >>> 17; ns ^= ns << 5; ns >>>= 0; const noise = ns / 4294967296 - .5;
    let v;
    if (sigKind === 'impulse') v = i % 9973 === 0 ? 1 : 0;
    else if (sigKind === 'burst') v = i < 128 * 24 ? .5 * noise + .3 * Math.sin(2 * Math.PI * f1 * i / 48000) : 0;
    else if (sigKind === 'longsleep') v = (i < 128 * 24 || (i >= 128 * 220 && i < 128 * 240)) ? .5 * noise + .3 * Math.sin(2 * Math.PI * f1 * i / 48000) : 0;
    else { const env = (Math.floor(i / 4000) % 5 === 3) ? 0 : 1; v = env * (.4 * Math.sin(2 * Math.PI * f1 * i / 48000) + .2 * Math.sin(2 * Math.PI * f2 * i / 48000) + (i < n / 3 ? noise * .2 : 0)); }
    o[i] = Math.fround(v * amp);
  }
  return o;
}
function randParams(def, rr, kind) {   // kind: 'rand' | 'min' | 'max'
  const p = {};
  for (const [k, d] of Object.entries(def.params || {})) {
    const [, mn, mx, st] = d; let v;
    if (kind === 'min') v = mn; else if (kind === 'max') v = mx;
    else { v = mn + (mx - mn) * rr(); if (st >= 1) v = Math.round((v - mn) / st) * st + mn; else if (rr() < .5 && st > 0) v = Math.round((v - mn) / st) * st + mn; v = Math.min(mx, Math.max(mn, v)); }
    p[k] = v;
  }
  return p;
}
const defaults = def => { const p = {}; for (const [k, d] of Object.entries(def.params || {})) p[k] = d[0]; return p; };
const stressP = def => { const o = {}; for (const [k, d] of Object.entries(def.params || {})) { const [, mn, mx, st] = d; if (mx - mn === 1 && st === 1) { o[k] = mn; continue; } let v = mn + (mx - mn) * .7; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
const switchesP = def => { const o = {}; for (const [k, d] of Object.entries(def.params || {})) { const [, mn, mx, st] = d; if (k === 'flip') { o[k] = d[0]; continue; } if (mx - mn === 1 && st === 1) { o[k] = k === 'mute' ? mn : mx; continue; } let v = mn + (mx - mn) * .3; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };

MODS.push(...MODS_B);   // U20
const LONG = new Set(['spring', 'plate', 'lverb', 'bbd', 'dline', 'pingpong', 'ensemble', 'dimension', 'allpass', 'gatedly']);
function casesFor(type, def) {
  const ins = Object.keys(def.inputs || {}), cs = []; let n = 0;
  const base = { type, sr: 48000, nblocks: 56, sigKind: 'mix', linkMode: 'all', changeEvery: 0, sizes: [128], transport: [], seed: 0 };
  const add = (name, o) => { cs.push(Object.assign({}, base, o, { name: `${type}_${name}`, seed: 1000 * (MODS.indexOf(type) + 1) + (++n) })); };
  if (casesB(type, def, add, { defaults, stressP, switchesP, randParams, lcg, signal })) return cs;   // U20: its own list of cases
  add('def', { params: [defaults(def)] });
  add('stress', { params: [stressP(def)], sigKind: 'loud' });
  add('sw', { params: [switchesP(def)], sigKind: 'impulse' });
  add('min', { params: [randParams(def, null, 'min')], sigKind: 'mix' });
  add('max', { params: [randParams(def, null, 'max')], sigKind: 'quiet' });
  for (let i = 0; i < 4; i++) { const rr = lcg(77 + 13 * i + MODS.indexOf(type) * 101); add(`rand${i}`, { params: [randParams(def, rr)], sigKind: SIGS[(i * 2 + MODS.indexOf(type)) % 5], linkMode: ['all', 'first', 'all', 'half'][i], rr: i }); }
  for (let i = 0; i < 2; i++) add(`mod${i}`, { params: [], changeEvery: i ? 7 : 25, sigKind: 'mix', nblocks: 96, rr: 10 + i });
  add('first', { params: [defaults(def)], linkMode: 'first' });
  add('none', { params: [stressP(def)], linkMode: 'none', nblocks: 40 });
  add('odd', { params: [switchesP(def)], sizes: [1, 37, 128, 64, 3, 127, 100, 5], transport: [{ b: 20, tempo: 73.5 }, { b: 45, tempo: 200, startN: 1 }, { b: 70, playing: false }, { b: 90, playing: true, startN: 2, tempo: 120 }], nblocks: 120, sigKind: 'mix' });
  add('sr44', { params: [stressP(def)], sr: 44100, sigKind: 'mix' });
  add('sr96', { params: [switchesP(def)], sr: 96000, sigKind: 'loud' });
  if (LONG.has(type)) { add('long', { params: [defaults(def)], sigKind: 'impulse', nblocks: 300 }); add('sleep', { params: [defaults(def)], sigKind: 'longsleep', nblocks: 260 });
    add('sleep2', { params: [stressP(def)], sigKind: 'longsleep', nblocks: 260, sr: 96000 }); add('burst', { params: [switchesP(def)], sigKind: 'burst', nblocks: 180 }); }
  return cs;
}

function runDirect(ctx, def, c) {
  const M = def, ins = Object.keys(M.inputs || {}), outs = Object.keys(M.outputs || {});
  const rr = lcg(c.seed * 31 + 5 + (c.rr || 0) * 977), allSeeds = [lcg(c.seed + 3)(), lcg(c.seed + 4)(), lcg(c.seed + 5)()].map(x => (Math.floor(x * 4294967295) >>> 0) || 1);
  // parameter schedule: [{b, p}]
  const sched = []; if (c.changeEvery) { for (let b = 0; b < c.nblocks; b += c.changeEvery) sched.push({ b, p: randParams(M, rr) }); } else c.params.forEach((p, i) => sched.push({ b: c.pb ? c.pb[i] : 0, p }));
  // linked inputs
  const linked = ins.map((n, k) => c.linkMode === 'all' ? true : c.linkMode === 'first' ? k === 0 : c.linkMode === 'none' ? false : (k % 2 === 0));
  const total = c.nblocks * 128, sig = c.mkSig ? ins.map((n, k) => linked[k] ? c.mkSig(k, total, M.inputs[n]) : new Float32Array(total)) : ins.map((n, k) => linked[k] ? signal(M.inputs[n], c.sigKind, total, c.seed * 17 + k * 131 + 1, M.type === 'quant' ? 'pitch' : '') : new Float32Array(total));
  ctx.__seedq = allSeeds.slice(); ctx.__thunderIR = undefined; let irv = 0; const st = M.create(c.sr);
  const io = { in: {}, out: {}, base: sched[0].p, p: sched[0].p, linked: new Set(), mods: null }, tr = { tempo: 120, lpb: 4, ticks: 6, playing: true };
  ins.forEach((n, k) => { io.in[n] = new Float32Array(128); if (linked[k]) io.linked.add(n); }); outs.forEach(n => { io.out[n] = new Float32Array(128); });
  const cx = { sr: c.sr, block: 128, quality: 'full', transport: tr, engine: {} };
  const inF = new Float32Array(c.nblocks * ins.length * 128), outF = new Float32Array(c.nblocks * outs.length * 128), sizes = [];
  let si = 0, ev = 0, tv = 0;
  for (let b = 0; b < c.nblocks; b++) {
    while (ev < sched.length && sched[ev].b <= b) { io.p = io.base = sched[ev].p; ev++; }
    while (tv < c.transport.length && c.transport[tv].b <= b) { const t = c.transport[tv++]; for (const k of ['tempo', 'playing', 'startN']) if (k in t) tr[k] = t[k]; }
    while (c.irs && irv < c.irs.length && c.irs[irv].b <= b) ctx.__thunderIR = irTable(c.irs[irv++].set);   // U20: the song's IRs (a new object each time)
    const n = Math.min(128, c.sizes[si++ % c.sizes.length]); sizes.push(n); cx.block = n;
    ins.forEach((nm, k) => { io.in[nm].set(sig[k].subarray(b * 128, (b + 1) * 128)); inF.set(io.in[nm], (b * ins.length + k) * 128); });
    M.process(st, io, cx);
    outs.forEach((nm, k) => outF.set(io.out[nm], (b * outs.length + k) * 128));
  }
  ctx.__seedq = null; ctx.__thunderIR = undefined;
  return { meta: { type: M.type || c.type, sr: c.sr, nblocks: c.nblocks, sizes, seeds: allSeeds, linked, params: sched.map(e => ({ b: e.b, p: Object.values(e.p) })), transport: c.transport, irs: c.irs || [] }, inF, outF };
}

function tables(M) {
  const o = {};
  for (const [t, m] of Object.entries(M)) o[t] = { in: Object.entries(m.inputs || {}), out: Object.entries(m.outputs || {}), params: Object.entries(m.params || {}).map(([k, d]) => [k, ...d]) };
  return o;
}
const hex8 = v => '0x' + v.toString(16).padStart(8, '0');
function incText(direct, engine) {
  const L = ['// GENERATED by native/tools/modfx-dump.mjs (--golden) from chains/chains-worklet.js (the shared MODFX block). Do not edit: a mismatch means the port is wrong.',
    '// (1) kModfxEngine: the 32-bit FNV-1a hashes of tests/golden/chains-modules.json for the modules of U19 (the ChainsEngine run of tests/chains.test.js), with the nextSeed() values that render took.',
    '// (2) kModfxCases: FNV-1a 64 of each direct case\'s output (every output port, every block), the real module run in Node 22 on x86-64 Linux (Math.sin / cos / tan / exp / pow / tanh in the audio loop).',
    'struct ModfxEngineGolden { const char* type; int set; unsigned hash; int nseeds; unsigned seeds[4]; };',
    'static const ModfxEngineGolden kModfxEngine[] = {'];
  for (const t of MODS) ['default', 'stressed', 'switches-on'].forEach((s, i) => { const k = `${t} @${s}`, sd = engine.seeds[k]; if (sd.length > 4) throw new Error('more than 4 seeds in ' + k);
    L.push(`  {"${t}", ${i}, 0x${engine.hashes[k]}u, ${sd.length}, {${[...sd, 0, 0, 0, 0].slice(0, 4).map(x => x + 'u').join(', ')}}},`); });
  L.push('};', 'static const int kModfxEngineN = sizeof(kModfxEngine) / sizeof(kModfxEngine[0]);',
    'struct ModfxCaseGolden { const char* name; unsigned long long hash; int floats; };', 'static const ModfxCaseGolden kModfxCases[] = {');
  for (const d of direct) L.push(`  {"${d.name}", 0x${d.hash.toString(16).padStart(16, '0')}ull, ${d.floats}},`);
  L.push('};', 'static const int kModfxCasesN = sizeof(kModfxCases) / sizeof(kModfxCases[0]);', '');
  return L.join('\n');
}

const arg = process.argv[2];
// the waves worklet carries the same shared block (its golden needs the U30 engine): the two texts between the markers must be identical
{ const blk = file => { const s = fs.readFileSync(path.join(ROOT, file), 'utf8'), a = s.indexOf('/* ===SHARED MODULES START==='), b = s.indexOf('/* ===SHARED MODULES END===');
    if (a < 0 || b < a) { console.error('modfx-dump: no shared block in ' + file); process.exit(1); } return s.slice(a, b); };
  if (blk('chains/chains-worklet.js') !== blk('waves/waves-worklet.js')) { console.error('modfx-dump: the shared module block of chains-worklet.js and waves-worklet.js differ'); process.exit(1); } }
const t0 = Date.now();
const engine = engineRun();
// the engine run must give what the repo's committed golden says (else this reference is not the one the unit is gated on)
const committed = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests/golden/chains-modules.json'), 'utf8')); let bad = 0;
for (const t of MODS) for (const s of ['default', 'stressed', 'switches-on']) { const k = `${t} @${s}`; if (committed[k] !== engine.hashes[k]) { console.error(`modfx-dump: ${k}: the engine run gives ${engine.hashes[k]}, tests/golden/chains-modules.json has ${committed[k]}`); bad++; } }
if (bad) process.exit(1);
const ctx = loadChains(), M = ctx.ChainsDSP.MODULES, outDir = arg && !arg.startsWith('--') ? arg : null;
for (const t of MODS) if (!M[t] || !M[t].process) { console.error('modfx-dump: no such module in the JS: ' + t); process.exit(1); }
if (outDir) { fs.mkdirSync(outDir, { recursive: true }); fs.writeFileSync(path.join(outDir, 'defs.json'), JSON.stringify(tables(engine.M))); }
const direct = [];
for (const t of MODS) for (const c of casesFor(t, M[t])) {
  const r = runDirect(ctx, M[t], c), outBytes = new Uint8Array(r.outF.buffer);
  direct.push({ name: c.name, hash: fnv64(outBytes), floats: r.outF.length });
  if (outDir) { fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify(r.meta)); fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), new Uint8Array(r.inF.buffer)); fs.writeFileSync(path.join(outDir, c.name + '.f32'), outBytes); }
}
const text = incText(direct, engine);
if (arg === '--golden') { fs.writeFileSync(GOLDEN_INC, text); console.log(`modfx-dump: wrote ${path.relative(ROOT, GOLDEN_INC)} (${direct.length} direct cases, ${MODS.length * 3} engine hashes)`); }
else if (arg === '--check') {
  const now = fs.existsSync(GOLDEN_INC) ? fs.readFileSync(GOLDEN_INC, 'utf8') : '';
  if (now !== text) { console.error('modfx-dump: native/tests/golden/modfx_golden.inc differs from what the JS gives now (run: node native/tools/modfx-dump.mjs --golden)'); process.exit(1); }
  console.log(`modfx-dump: modfx_golden.inc is current (${direct.length} direct cases)`);
} else console.log(`modfx-dump: ${direct.length} direct cases written to ${outDir || '(nowhere: no directory given)'} in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
