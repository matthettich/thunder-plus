// index.html's inline worklets lost some per-block garbage (BassProc's rate closure, VoxProc's `at` closure, PitchFx's all-pass
// iterator; ThunderFx's subarray views are the same data, covered by the chains / flex goldens). This renders the bass and voice
// processors and PitchFxCore (reverb: all shimmer modes; delay: every mode) from index.html as it was at build 552 (commit
// 90ab6b8, read with `git show`) and as it is now, and asserts every output sample is bit-identical.
// SECOND PASS (build 563, per-block garbage in the chains / waves hosts and modules, rvMix, __tcOv, the pump synth's `at`, Flex's
// closures): the same, against build 562 (commit b0cb7cc), at the bottom of this file: chains modules and host, waves engine, the pump
// synth with MOD targets, __tcOv's result, FlexCore / StepGateCore (every pattern), plus counts: a steady block makes no typed array.
'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const H = require('./helpers');

const BASE = '90ab6b8';
let old = null;
try { old = execFileSync('git', ['show', BASE + ':index.html'], { cwd: path.resolve(__dirname, '..'), encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { old = null; }

function classText(html, name) {
  const a = html.indexOf('\nclass ' + name + ' {') + 1; assert.ok(a > 0, name);
  let d = 0, i = a, started = false;
  for (; i < html.length; i++) { const c = html[i]; if (c === '{') { d++; started = true; } else if (c === '}') { d--; if (started && d === 0) break; } }
  return html.slice(a, i + 1);
}
function lcg(seed) { let s = seed >>> 0; return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296) * 2 - 1; }
const SR = 48000;

function load(html) {
  const reg = {};
  class AWP { constructor() { this.port = { postMessage() {}, onmessage: null }; } }
  const ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Object, Array, Map, Set, JSON, Date, Number, String, RegExp, isFinite, parseFloat, parseInt,
    AudioWorkletProcessor: AWP, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: SR, currentTime: 0 };
  ctx.globalThis = ctx; vm.createContext(ctx);
  const rv = html.indexOf('function rvMix('); vm.runInContext(html.slice(rv, html.indexOf('\n', rv)), ctx);
  for (const n of ['BassCore', 'VoxCore', 'PitchFxCore']) vm.runInContext(classText(html, n), ctx);
  vm.runInContext('this.PitchFxCore = PitchFxCore; this.BassCore = BassCore; this.VoxCore = VoxCore;', ctx);
  for (const [name, end] of [['BASS_PROC', "registerProcessor('thunder-bass', BassProc);`;"], ['VOX_PROC', "registerProcessor('thunder-vox', VoxProc);`;"]]) {
    const a = html.indexOf('const ' + name + ' = `'), b = html.indexOf(end, a) + end.length;
    assert.ok(a > 0 && b > a, name);
    vm.runInContext(html.slice(a, b) + '\n;(0, eval)', ctx);
    vm.runInContext('(new Function("AudioWorkletProcessor", "registerProcessor", "BassCore", "VoxCore", "sampleRate", ' + name + '))', ctx)(AWP, ctx.registerProcessor, ctx.BassCore, ctx.VoxCore, SR);
  }
  return { reg, ctx };
}

function runProc(Cls, names, seed) {
  const p = new Cls({ processorOptions: {} }), r = lcg(seed), out = [];
  for (let b = 0; b < 400; b++) {
    const P = {};
    for (const n of names) P[n] = new Float32Array(1);
    P.rate = b % 7 < 3 ? new Float32Array(128).map((_, i) => 0.8 + (i + b) * 0.004) : new Float32Array([0.7 + (b % 5) * 0.2]);
    const k = b % 20;
    P.gate = k >= 2 && k < 14 ? (k === 8 ? new Float32Array(128).map((_, i) => i < 60 ? 1 : 0) : new Float32Array([1])) : new Float32Array([0]);
    P.trig = new Float32Array(128).fill(k < 8 ? 1 : 2); if (k === 8) P.trig.fill(3, 50);
    if (P.leg) P.leg = new Float32Array([b % 3 === 0 ? 1 : 0]);
    if (P.syl) P.syl = new Float32Array([Math.floor(b / 20)]);
    if (P.same) P.same = new Float32Array([b % 5 === 0 ? 1 : 0]);
    if (P.bpm) P.bpm = new Float32Array([120 + (b % 3)]);
    if (P.wob) P.wob = new Float32Array([b % 4 === 0 ? 1 : 0]);
    const o = [[new Float32Array(128), new Float32Array(128)]];
    p.process([], o, P); out.push(Array.from(o[0][0]), Array.from(o[0][1]));
  }
  return out;
}
const same = (A, B, what) => {
  assert.equal(A.length, B.length); let nz = 0, bad = 0;
  for (let i = 0; i < A.length; i++) for (let k = 0; k < A[i].length; k++) { if (A[i][k] !== 0) nz++; if (!Object.is(A[i][k], B[i][k])) bad++; }
  assert.ok(nz > 1000, what + ' must sound (' + nz + ')'); assert.equal(bad, 0, what + ': ' + bad + ' samples differ');
};

const skip = !old && 'git history for ' + BASE + ' not available';
test('BassProc and VoxProc render bit-identically to build 552', { skip }, () => {
  const A = load(old), B = load(H.read('index.html'));
  same(runProc(A.reg['thunder-bass'], ['bpm', 'wob', 'trig', 'leg'], 1), runProc(B.reg['thunder-bass'], ['bpm', 'wob', 'trig', 'leg'], 1), 'bass');
  same(runProc(A.reg['thunder-vox'], ['trig', 'leg', 'syl', 'same'], 2), runProc(B.reg['thunder-vox'], ['trig', 'leg', 'syl', 'same'], 2), 'vox');
});

test('PitchFxCore (reverb shimmer modes, every delay mode) renders bit-identically to build 552', { skip }, () => {
  const A = load(old).ctx.PitchFxCore, B = load(H.read('index.html')).ctx.PitchFxCore;
  const cfgs = [];
  for (const kind of [1, 0]) for (const mode of [0, 1, 2, 3, 4]) cfgs.push({ kind, mode, mix: 0.6, pitch: 7, pitch2: -5, tone: 0.5, size: 0.6, fb: 0.5, time: 120, wow: 0.4 });
  const run = (Cls, cfg) => {
    const f = new Cls(SR), r = lcg(5), out = [];
    for (let b = 0; b < 500; b++) {
      const L = new Float32Array(128).map((_, i) => b < 100 ? r() * 0.5 * Math.sin((b * 128 + i) * 0.03) : 0), R = new Float32Array(128).map((_, i) => b < 100 ? r() * 0.4 : 0);
      f.process(L, R, 128, cfg); out.push(Array.from(L), Array.from(R));
    }
    return out;
  };
  for (const cfg of cfgs) same(run(A, cfg), run(B, cfg), 'pitchfx kind ' + cfg.kind + ' mode ' + cfg.mode);
});

/* ================= second pass: against build 562 (commit b0cb7cc) ================= */
const BASE2 = 'b0cb7cc', fs = require('node:fs');
const show2 = f => { try { return execFileSync('git', ['show', BASE2 + ':' + f], { cwd: path.resolve(__dirname, '..'), encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { return null; } };
const OLD = { chains: show2('chains/chains-worklet.js'), waves: show2('waves/waves-worklet.js'), psyn: show2('pump/synth/pump-synth-worklet.js'), core: show2('core/seq-core.js'), html: show2('index.html') };
const skip2 = !OLD.chains && !OLD.waves && !OLD.psyn && !OLD.core && !OLD.html && 'git history for ' + BASE2 + ' not available';
const TFXWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'chains/dsp/tfx.wasm')));
const PUMPWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'pump/pump.wasm')));
const DSPWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm')));
const SIG = H.testSignal(128 * 700);
const bitsEqual = (a, b, what) => { assert.equal(a.length, b.length, what + ' length'); let bad = 0; for (let i = 0; i < a.length; i++) if (!Object.is(a[i], b[i])) bad++; assert.equal(bad, 0, what + ': ' + bad + ' samples differ'); };
const energy = a => { let e = 0; for (const v of a) e += v * v; return e; };

// ---- chains: a module between a Track In and a Chain Out, through the engine, with parameter, tempo and start changes on the way
function chainsEngine(src) {
  const ctx = H.freezeClock(H.workletScope()); vm.runInContext(src, ctx, { filename: 'chains-worklet.js' });
  ctx.ChainsDSP.wasmInit({ tfxBytes: TFXWASM }); ctx.__pumpBytes = PUMPWASM;
  return { ctx, D: ctx.ChainsDSP, E: new ctx.ChainsDSP.ChainsEngine(48000, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }) };
}
function renderChainsModule(src, type, blocks = 360) {
  const { D, E } = chainsEngine(src), m = D.MODULES[type], params = {};
  for (const [k, d] of Object.entries(m.params || {})) { const [dv, mn, mx, st] = d; params[k] = (mx - mn === 1 && st === 1) ? mn : mn + (mx - mn) * .7; }
  const aIn = Object.keys(m.inputs || {}).filter(n => m.inputs[n] === 'audio'), outs = Object.keys(m.outputs || {}), cab = [];
  if (aIn.includes('in')) cab.push({ from: ['a', 'out'], to: ['b', 'in'] }); if (aIn.includes('inR')) cab.push({ from: ['a', 'outR'], to: ['b', 'inR'] });
  ['in', 'inR'].forEach((sink, i) => { if (outs[i]) cab.push({ from: ['b', outs[i]], to: ['c', sink] }); });
  E.loadPatch({ modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type, scope: 'mono', params }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }], cables: cab });
  const rec = [new Float32Array(128 * blocks), new Float32Array(128 * blocks)], op = {}; for (const k of outs) op[k] = new Float32Array(128 * blocks);
  let seed = 7; const rnd = () => ((seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 4294967296);
  for (let b = 0; b < blocks; b++) {
    if (b > 0 && b % 40 === 0) for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; let v = mn + (mx - mn) * rnd(); if (st >= 1) v = Math.round((v - mn) / st) * st + mn; E.setParam('b', k, v); }   // (an effect's map() must see every change)
    if (b === 150) E.setTransport({ tempo: 97 }); if (b === 200) { E.setTransport({ playing: false }); } if (b === 210) E.setTransport({ playing: true });
    const sl = SIG.subarray(b * 128, (b + 1) * 128); E.setTrackInputs(sl, sl); E.process(128);
    rec[0].set(E.bus(1), b * 128); rec[1].set(E.busR(1), b * 128); const io = E._ios.get('b'); if (io) for (const k of outs) op[k].set(io.out[k], b * 128);
  }
  if (E.quality !== 'full') throw new Error('engine shed to draft quality (machine too slow)');
  return { rec, op };
}
// every module this pass touched: the effect() wrapper (its map() is kept while nothing changed), Blades, Meta, Ring mod, Ensemble, Dimension,
// Harmonizer, Euclid, Life, Spring, Plate, Marbles, Tides, Resample, PitchFx (reverb + delay), the generated Flex / Step gate / Retrigger
const TOUCHED = ['eq', 'comp', 'reverb', 'delay', 'saturation', 'tape', 'chorus', 'crush', 'rings', 'clouds', 'beads', 'flanger', 'tremolo', 'odrive', 'folder', 'autowah', 'pshift', 'ladder', 'xmod', 'space', 'lofi', 'drive', 'air', 'room',
  'comb', 'transient', 'tgate', 'ott', 'fshift', 'vinyl', 'dub', 'cab', 'tube', 'blades', 'meta', 'ringmod', 'ensemble', 'dimension', 'harm', 'euclid', 'life', 'spring', 'plate', 'marbles', 'tides', 'resample', 'flex', 'sgate', 'retrig', 'pdelay', 'preverb'];
test('chains: the modules whose per-block garbage was removed render bit-identically to build 562 (parameters, tempo and start changing)', { skip: skip2 || !OLD.chains && 'no base' }, () => {
  const D = chainsEngine(H.read('chains/chains-worklet.js')).D, seen = [];
  for (const type of TOUCHED) {
    if (!D.MODULES[type]) continue; seen.push(type);
    const o = renderChainsModule(OLD.chains, type), n = renderChainsModule(H.read('chains/chains-worklet.js'), type);
    bitsEqual(o.rec[0], n.rec[0], type + ' bus L'); bitsEqual(o.rec[1], n.rec[1], type + ' bus R'); for (const k in o.op) bitsEqual(o.op[k], n.op[k], type + ' out ' + k);
  }
  assert.ok(seen.length >= 45, 'the list names real modules (' + seen.length + ' found)');
});

// ---- the chains host (ThunderChains): bus feeds, sends, sidechain, taps, BYP params, a Source keyed to a send
function chainsHost(src) {
  const ctx = H.freezeClock(H.workletScope()); vm.runInContext(src, ctx, { filename: 'chains-worklet.js' }); ctx.ChainsDSP.wasmInit({ tfxBytes: TFXWASM }); ctx.__pumpBytes = PUMPWASM;
  const patch = { modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'e', type: 'reverb', scope: 'mono', params: { mix: .4, _byp: 0 } }, { id: 'h', type: 'harm', scope: 'mono', params: {} },
      { id: 'd', type: 'delay', scope: 'mono', params: { mix: .3 } }, { id: 'so', type: 'scout', scope: 'mono', params: { snd: 1, level: 1 } }, { id: 'sc', type: 'scin', scope: 'mono', params: { sck: 1 } },
      { id: 'sp', type: 'spring', scope: 'mono', params: { mix: .3 } }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }, { id: 't2', type: 'trackIn', scope: 'mono', params: { track: 2 } }, { id: 'c2', type: 'chainOut', scope: 'mono', params: { track: 1, level: .5 } }],
    cables: [{ from: ['a', 'out'], to: ['e', 'in'] }, { from: ['a', 'outR'], to: ['e', 'inR'] }, { from: ['e', 'out'], to: ['h', 'in'] }, { from: ['e', 'outR'], to: ['h', 'inR'] }, { from: ['h', 'out'], to: ['d', 'in'] }, { from: ['h', 'outR'], to: ['d', 'inR'] },
      { from: ['d', 'out'], to: ['sp', 'in'] }, { from: ['d', 'outR'], to: ['sp', 'inR'] }, { from: ['sp', 'out'], to: ['so', 'in'] }, { from: ['sp', 'out'], to: ['c', 'in'] }, { from: ['sp', 'outR'], to: ['c', 'inR'] },
      { from: ['sc', 'env'], to: ['d', 'p:mix'] }, { from: ['t2', 'out'], to: ['c2', 'in'] }, { from: ['t2', 'outR'], to: ['c2', 'inR'] }] };
  const p = new ctx.__registered['thunder-chains']({ processorOptions: { quality: 'full', track: 0, patch, busMap: [1], tapMap: [0] } }); p.port.postMessage = () => {};
  return p;
}
const hostBlock = b => {
  const s = SIG.subarray(b * 128, (b + 1) * 128), t = SIG.subarray((b + 7) * 128, (b + 8) * 128), z = new Float32Array(128), snd = [t, z, z, z, z, z, z, z];
  const bus = Array.from({ length: 4 }, (_, k) => k < 2 ? t : z); const ins = [[s, s], [t, t], [s], snd, bus, []];
  return { ins, outs: [[new Float32Array(128), new Float32Array(128)], Array.from({ length: 8 }, () => new Float32Array(128))] };
};
function runHost(src, blocks = 300) {
  const p = chainsHost(src), L = new Float32Array(128 * blocks), R = new Float32Array(128 * blocks), S = new Float32Array(128 * blocks);
  for (let b = 0; b < blocks; b++) { const { ins, outs } = hostBlock(b); assert.notEqual(p.process(ins, outs, { b1: [-1], b2: [b > 100 ? 1 : -1] }), false); L.set(outs[0][0], b * 128); R.set(outs[0][1], b * 128); S.set(outs[1][0], b * 128); }
  return { L, R, S };
}
test('chains: the ThunderChains host (bus feeds, sends, sidechain, taps, BYP) renders bit-identically to build 562', { skip: skip2 || !OLD.chains && 'no base' }, () => {
  const o = runHost(OLD.chains), n = runHost(H.read('chains/chains-worklet.js'));
  assert.ok(energy(n.L) > 1, 'the patch must sound'); bitsEqual(o.L, n.L, 'out L'); bitsEqual(o.R, n.R, 'out R'); bitsEqual(o.S, n.S, 'send 1');
});

// ---- no typed array and no subarray view is made by a steady block (the hosts, the effects wrapper, the busses, the modules' views)
function counting() {
  const c = { made: 0, sub: 0 }, sub = Float32Array.prototype.subarray;
  c.on = () => { Float32Array.prototype.subarray = function (...a) { c.sub++; return sub.apply(this, a); }; }; c.off = () => { Float32Array.prototype.subarray = sub; };
  c.F32 = new Proxy(Float32Array, { construct(t, a, nt) { c.made++; return Reflect.construct(t, a, nt); } });
  return c;
}
test('chains host and modules: after warm-up a block makes no subarray view and no typed array', () => {
  const c = counting(), src = H.read('chains/chains-worklet.js');
  const ctx = H.freezeClock(H.workletScope({ Float32Array: c.F32 })); vm.runInContext(src, ctx, { filename: 'chains-worklet.js' }); ctx.ChainsDSP.wasmInit({ tfxBytes: TFXWASM }); ctx.__pumpBytes = PUMPWASM;
  const mods = [['trackIn', { track: 1 }], ['reverb', { mix: .4 }], ['delay', { mix: .3 }], ['harm', {}], ['ensemble', {}], ['dimension', {}], ['spring', { mix: .3 }], ['plate', { mix: .3 }], ['ringmod', {}], ['blades', {}], ['meta', {}], ['life', {}], ['euclid', {}],
    ['tides', {}], ['marbles', {}], ['resample', {}], ['pdelay', {}], ['preverb', {}], ['scout', { snd: 1 }], ['chainOut', { track: 1, level: 1 }]], doc = { modules: [], cables: [] };
  mods.forEach(([type, params], i) => { doc.modules.push({ id: 'm' + i, type, scope: 'mono', params }); if (i && i < mods.length - 1) { doc.cables.push({ from: ['m' + (i - 1), 'out'], to: ['m' + i, 'in'] }); } });
  doc.cables.push({ from: ['m17', 'out'], to: ['m19', 'in'] }); doc.cables.push({ from: ['m18', 'out'], to: ['m19', 'inR'] }); doc.cables.push({ from: ['m12', 'gate'], to: ['m10', 'p:amount'] });
  const p = new ctx.__registered['thunder-chains']({ processorOptions: { quality: 'full', track: 0, patch: doc } }); p.port.postMessage = () => {};
  const blocks = Array.from({ length: 80 }, (_, b) => hostBlock(b)); let tries = 0;
  for (let b = 0; b < 40; b++) p.process(blocks[b].ins, blocks[b].outs, { b1: [-1] });
  c.on(); try { c.sub = 0; c.made = 0; for (let b = 40; b < 80; b++) p.process(blocks[b].ins, blocks[b].outs, { b1: [-1] }); } finally { c.off(); }
  assert.equal(c.sub, 0, 'subarray views made in 40 steady blocks');
  assert.equal(c.made, 0, 'typed arrays made in 40 steady blocks');
});
test('waves live and pump synth: after warm-up a block makes no typed array', () => {
  const c = counting();
  const wctx = H.freezeClock(H.workletScope({ Float32Array: c.F32 })); vm.runInContext(H.read('waves/waves-worklet.js'), wctx, { filename: 'waves-worklet.js' }); wctx.WavesDSP && wctx.WavesDSP.wasmInit({ dspBytes: DSPWASM, tfxBytes: TFXWASM });
  const w = new wctx.__registered['thunder-wlive']({ processorOptions: { doc: wdoc2, p: { bus: 1 } } });
  const pctx = H.workletScope({ Date, Float32Array: c.F32 }); vm.runInContext(H.read('pump/synth/pump-synth-worklet.js'), pctx, { filename: 'pump-synth-worklet.js' });
  const ps = new pctx.__registered['thunder-psyn']({ processorOptions: Object.assign({ p: Object.assign(PSC.xeng().pmonol.d(), { tune: 0, level: 0, drive: 0 }), bytes: PSBYTES }, PSC.liveSpec('pmonol')) });
  const P = b => { const o = { bpm: [120] }; for (let j = 0; j < 4; j++) Object.assign(o, { ['r' + j]: [1], ['g' + j]: [j === 0 && b > 5 ? 1 : 0], ['t' + j]: [j === 0 && b > 5 ? 1 : 0], ['l' + j]: [0], ['v' + j]: [100] });
    for (const k of ['cCut', 'cRes', 'cDec', 'cAtk', 'cRel']) o[k] = [b > 20 ? 70 + (b & 7) : -1]; for (const k of ['oCut', 'oRes', 'oEnv', 'oAtk', 'oDec', 'oSus', 'oRel']) o[k] = [0]; for (const k in o) o[k] = Float32Array.from(o[k]); return o; };
  const run = (n, b0, b1) => { for (let b = b0; b < b1; b++) { const pp = P(b), o = [[new Float32Array(128), new Float32Array(128)]]; n.process([], o, pp); } };
  run(w, 0, 40); run(ps, 0, 40);
  const sub = Float32Array.prototype.subarray; let subs = 0; Float32Array.prototype.subarray = function (...a) { subs++; return sub.apply(this, a); };
  const blocksP = Array.from({ length: 40 }, (_, b) => P(40 + b)), outs = Array.from({ length: 40 }, () => [[new Float32Array(128), new Float32Array(128)]]);   // (made before counting)
  try { c.made = 0; subs = 0; for (let b = 0; b < 40; b++) w.process([], outs[b], blocksP[b]); assert.equal(c.made, 0, 'waves live: typed arrays made'); assert.equal(subs, 0, 'waves live: subarray views');
    for (let b = 0; b < 40; b++) ps.process([], outs[b], blocksP[b]); assert.equal(c.made, 0, 'pump synth: typed arrays made'); assert.equal(subs, 0, 'pump synth: subarray views'); }
  finally { Float32Array.prototype.subarray = sub; }
});

// ---- waves engine: voices, effects, the Out bus
function wavesEngine(src) {
  const ctx = H.freezeClock(H.pageScope()); vm.runInContext(src, ctx, { filename: 'waves-worklet.js' }); ctx.WavesDSP.wasmInit({ dspBytes: DSPWASM, tfxBytes: TFXWASM });
  return { ctx, D: ctx.WavesDSP };
}
const wdoc2 = { format: 'waves.patch', version: 1, modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'a', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } },
  { id: 'ea', type: 'adsr', scope: 'poly', params: { a: .005, d: .2, s: .7, r: .05 } }, { id: 'ga', type: 'vca', scope: 'poly', params: { gain: 1 } }, { id: 'l', type: 'lfo', scope: 'mono', params: { rate: 3, depth: .4 } },
  { id: 'ch', type: 'ensemble', scope: 'mono', params: {} }, { id: 'rv', type: 'reverb', scope: 'mono', params: { mix: .3 } }, { id: 'hm', type: 'harm', scope: 'mono', params: {} }, { id: 'o1', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
  cables: [{ from: ['h', 'pitch'], to: ['a', 'pitch'] }, { from: ['h', 'gate'], to: ['ea', 'gate'] }, { from: ['a', 'out'], to: ['ga', 'in'] }, { from: ['ea', 'out'], to: ['ga', 'cv'] }, { from: ['ga', 'out'], to: ['ch', 'in'] },
    { from: ['ch', 'out'], to: ['hm', 'in'] }, { from: ['ch', 'outR'], to: ['hm', 'inR'] }, { from: ['hm', 'out'], to: ['rv', 'in'] }, { from: ['hm', 'outR'], to: ['rv', 'inR'] }, { from: ['rv', 'out'], to: ['o1', 'in'] }, { from: ['rv', 'outR'], to: ['o1', 'inR'] }, { from: ['l', 'out'], to: ['rv', 'p:mix'] }] };
function runWaves(src) {
  const { D } = wavesEngine(src), E = new D.WavesEngine(48000, { maxVoices: 4, quality: 'full', seed: 7, transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch(wdoc2); const B = 300, L = new Float32Array(128 * B), R = new Float32Array(128 * B);
  for (let b = 0; b < B; b++) { if (b === 0) { E.noteOn(0, 1); E.noteOn(7, .8); } if (b === 90) E.noteOff(0); if (b === 120) E.noteOn(-5, 1); E.process(128); L.set(E.bus(1), b * 128); R.set(E.busR(1), b * 128); }
  if (E.quality !== 'full') throw new Error('engine shed to draft quality (machine too slow)');
  return { L, R };
}
test('waves: voices, per-voice keys, an effect chain and the Out bus render bit-identically to build 562', { skip: skip2 || !OLD.waves && 'no base' }, () => {
  const mr = Math.random; try { const o = runWaves(OLD.waves), n = runWaves(H.read('waves/waves-worklet.js')); assert.ok(energy(n.L) > .1, 'the patch must sound'); bitsEqual(o.L, n.L, 'bus L'); bitsEqual(o.R, n.R, 'bus R'); } finally { Math.random = mr; }
});
test('waves live: lanes (a-rate rate, legato, gates) render bit-identically to build 562', { skip: skip2 || !OLD.waves && 'no base' }, () => {
  const run = src => {
    const ctx = H.freezeClock(H.workletScope()); vm.runInContext(src, ctx, { filename: 'waves-worklet.js' }); ctx.WavesDSP && ctx.WavesDSP.wasmInit({ dspBytes: DSPWASM, tfxBytes: TFXWASM });
    const p = new ctx.__registered['thunder-wlive']({ processorOptions: { doc: wdoc2, p: { bus: 1 } } }), out = new Float32Array(128 * 260);
    for (let b = 0; b < 260; b++) {
      const P = { bpm: [120] }; for (let j = 0; j < 4; j++) { const on = j < 2 && b >= 10 + j * 5 && b < 150, t = on ? 1 + (b >= 60 ? 1 : 0) : 0;
        Object.assign(P, { ['r' + j]: j === 0 && b > 30 ? Float32Array.from({ length: 128 }, (_, i) => 1 + (i + b) * .001) : [1 + j * .25], ['g' + j]: [on ? 1 : 0], ['t' + j]: [t], ['l' + j]: [b >= 60 ? 1 : 0], ['v' + j]: [100 - j * 10] }); }
      for (const k in P) P[k] = Float32Array.from(P[k]); const o = [[new Float32Array(128), new Float32Array(128)]]; p.process([], o, P); out.set(o[0][0], b * 128);
    }
    return out;
  };
  const o = run(OLD.waves), n = run(H.read('waves/waves-worklet.js')); assert.ok(energy(n) > .1, 'it must sound'); bitsEqual(o, n, 'wlive');
});

// ---- the pump synth: lanes, an a-rate rate (arp / vibrato), legato, and MOD's engine targets (modRead) moving
const PSC = (() => { const c = { console: { warn() {}, log() {} }, Math, Float32Array, Uint8Array, WebAssembly, Object, Array, Set, Map, JSON, String, Number, isFinite }; c.globalThis = c; c.self = c; vm.createContext(c);
  vm.runInContext(H.read('pump/synth/pump-synth-meta.js'), c); vm.runInContext(H.read('pump/synth/pump-synth-engine.js'), c); c.PsynCore.attachModule(new WebAssembly.Module(fs.readFileSync(path.join(H.ROOT, 'pump/synth/pump-synth.wasm')))); return c.PsynCore; })();
const PSBYTES = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'pump/synth/pump-synth.wasm')));
function runPsyn(src, kind) {
  const c = H.workletScope({ Date }); vm.runInContext(src, c, { filename: 'pump-synth-worklet.js' });
  const node = new c.__registered['thunder-psyn']({ processorOptions: Object.assign({ p: Object.assign(PSC.xeng()[kind].d(), { tune: 0, level: 0, drive: 0 }), bytes: PSBYTES }, PSC.liveSpec(kind)) });
  node.port.postMessage = () => {}; const out = new Float32Array(128 * 240);
  for (let b = 0; b < 240; b++) {
    const P = { bpm: [120 + (b >> 5)] }, up = b >= 20 && b < 120;
    for (let j = 0; j < 4; j++) { const on = j < 2 && b >= 10 + j * 8 && b < 170, t = on ? 1 + (b >= 70 ? 1 : 0) : 0;
      Object.assign(P, { ['r' + j]: j === 0 && b >= 40 && b < 100 ? Float32Array.from({ length: 128 }, (_, i) => 1 + .02 * Math.sin((b * 128 + i) * .01)) : [1 + (j === 1 ? .5 : 0)], ['g' + j]: [on ? 1 : 0], ['t' + j]: [t], ['l' + j]: [b >= 70 ? 1 : 0], ['v' + j]: [100 - j * 10] }); }
    for (const k of ['cCut', 'cRes', 'cDec', 'cAtk', 'cRel']) P[k] = [up && k !== 'cRel' ? 60 + (b % 50) : -1]; for (const k of ['oCut', 'oRes', 'oEnv', 'oAtk', 'oDec', 'oSus', 'oRel']) P[k] = [up ? ((b * 7) % 100) - 50 : 0];
    for (const k in P) P[k] = Float32Array.from(P[k]); const o = [[new Float32Array(128), new Float32Array(128)]]; node.process([], o, P); out.set(o[0][0], b * 128);
  }
  return { out, tm: !!node.tm };
}
for (const kind of ['pmonol', 'ppolyl', 'pwobl']) test('pump synth ' + kind + ': lanes, a-rate rate and engine targets render bit-identically to build 562', { skip: skip2 || !OLD.psyn && 'no base' }, () => {
  const o = runPsyn(OLD.psyn, kind), n = runPsyn(H.read('pump/synth/pump-synth-worklet.js'), kind);
  assert.ok(energy(n.out) > 1e-3, kind + ' must sound'); bitsEqual(o.out, n.out, kind);
  if (kind === 'pmonol') assert.ok(n.tm, 'the engine-target path (modRead) was exercised');
});

// ---- __tcOv (core): the override's result, block by block
function tcOvRun(src, n) {
  let time = 0; const reg = {}, ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Map, Set, JSON, Date, Number, String, Object, Array, isFinite, parseInt, parseFloat,
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, registerProcessor: (nm, c) => { reg[nm] = c; }, sampleRate: 48000 };
  Object.defineProperty(ctx, 'currentTime', { get: () => time }); ctx.globalThis = ctx; vm.createContext(ctx); vm.runInContext(src, ctx, { filename: 'seq-core.js' });
  const proc = new reg['thunder-core']({});
  const go = vm.runInContext('(function(proc){ const ov = tcOver(proc); ov.post = false; const a = tcParam(ov, "x", 0), c = tcParam(ov, "k", 0), z = tcParam(ov, "never", 0); a.set(0, 0); a.lin(1, 0.01); a.target(0.25, 0.02, 0.003); c.set(5, 0); c.set(7, 0.012); z.set(1, 99); return (P, n) => globalThis.__tcOv(proc, P, n); })', ctx)(proc);
  const seen = [], Qs = new Set(), ones = new Set();
  for (let b = 0; b < 200; b++) {
    time = b * n / 48000; const P = { x: new Float32Array(n).fill(.5), k: new Float32Array(1), w: new Float32Array(n).map((_, i) => i), q: [3] };
    const Q = go(P, n); Qs.add(Q); ones.add(Q.k); seen.push(Array.from(Q.x.length ? Q.x : [Q.x]), [].concat(Array.from(Q.k)), Array.from(Q.w), Array.from(Q.q), Object.keys(Q).join());
  }
  return { seen, Qs, ones };
}
test('__tcOv: the override result is the same numbers every block as build 562, and its result object / one-number arrays are kept', { skip: skip2 || !OLD.core && 'no base' }, () => {
  const o = tcOvRun(OLD.core, 128), n = tcOvRun(H.read('core/seq-core.js'), 128);
  assert.deepEqual(n.seen, o.seen);
  assert.ok(o.Qs.size > 100 && n.Qs.size === 1, 'one result object (was ' + o.Qs.size + ' of them): ' + n.Qs.size);
  assert.ok(n.ones.size <= 3, 'the constant is the same array while it is constant');
  const n2 = tcOvRun(H.read('core/seq-core.js'), 256); assert.equal(n2.seen[0].length, 256);
});

// ---- Flex (every pattern, with its filters, crush and the per-sample tables) and Step gate, from index.html
function fxCores(html) {
  const cut = re => { const m = html.match(re); assert.ok(m, 'cut ' + re); return m[0]; };
  return new Function(cut(/function clkSpb\(o, bp, n\)\{[\s\S]*?\n\}\n/) + cut(/class FlexCore \{[\s\S]*?\n\}\n/) + cut(/class StepGateCore \{[\s\S]*?\n\}\n/) + 'return {FlexCore, StepGateCore};')();
}
function runFlex(C, kind, k, blocks = 22) {
  const sr = 48000, f = kind === 'flex' ? new C.FlexCore(sr) : new C.StepGateCore(sr), out = [];
  const dp = kind === 'flex' ? { pat: k, mix: 1, xf: 5, wait: false } : { pat: k, m1: 0xA5, m2: 0x5A, div: k % 5, len: .6, depth: 1, smooth: 3, wait: false };
  for (let b = 0; b < blocks; b++) {
    const L = new Float32Array(128), R = new Float32Array(128); for (let i = 0; i < 128; i++) { L[i] = SIG[b * 128 + i]; R[i] = SIG[(b + 3) * 128 + i]; }
    const bp = Float32Array.from({ length: 128 }, (_, i) => ((b * 128 + i) * 0.009) % 16);
    f.process(L, R, 128, dp, { bp, flx: -1, gat: -1, sw: -1 }); out.push(L, R);
  }
  return out;
}
test('FlexCore (all 511 patterns) and StepGateCore (all patterns) render bit-identically to build 562', { skip: skip2 || !OLD.html && 'no base' }, () => {
  const O = fxCores(OLD.html), N = fxCores(H.read('index.html')); let nz = 0;
  for (let k = 1; k < 512; k++) { const a = runFlex(O, 'flex', k), b = runFlex(N, 'flex', k); for (let i = 0; i < a.length; i++) { bitsEqual(a[i], b[i], 'flex ' + k); } for (const x of b) nz += energy(x) > 0 ? 1 : 0; }
  for (let k = 0; k < 16; k++) { const a = runFlex(O, 'gate', k), b = runFlex(N, 'gate', k); for (let i = 0; i < a.length; i++) bitsEqual(a[i], b[i], 'step gate ' + k); }
  assert.ok(nz > 1000, 'the patterns must sound');
});
test('rvMix: with a scratch array the same pair and no new array; without it a fresh pair as before', () => {
  const B = load(H.read('index.html')).ctx, A = OLD.html && load(OLD.html).ctx, o = [0, 0];
  for (const m of [-.5, 0, .1, .25, .5, .77, 1, 1.5]) { const f = [...B.rvMix(m)], g = B.rvMix(m, o); assert.equal(g, o); assert.deepEqual([...g], f); if (A) assert.deepEqual(f, [...A.rvMix(m)]); }
  assert.notEqual(B.rvMix(.3), B.rvMix(.3));
});
