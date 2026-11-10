// ANY BLOCK LENGTH (10 Nov): the worklets were written for the 128-frame render quantum (ThunderFx threw at 256). Each processor
// is driven here with 64-, 128-, 256- and 384-frame blocks (and 4097, which fails soft):
//   - at 128 the output is bit-identical to the processor as it was at aaf7554 (read with `git show`; skipped without history);
//   - at the other sizes nothing throws and every sample is finite;
//   - a 256 block equals two 128 blocks wherever the DSP does not depend on the block size (every chunked processor here).
// The input is a function of the absolute sample position, so a block size only changes where the blocks are cut.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path'), cp = require('child_process');
const H = require('./helpers');

const BASE = 'aaf7554', T = 3072, SIZES = [64, 128, 256, 384];
const show = f => { try { return cp.execFileSync('git', ['show', BASE + ':' + f], { cwd: H.ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { return null; } };
const SIG = H.testSignal(T + 4096);
const sameBits = (a, b) => { if (a.length !== b.length) return false; for (let i = 0; i < a.length; i++) if (!Object.is(a[i], b[i])) return false; return true; };
const finite = a => { for (const v of a) if (!Number.isFinite(v)) return false; return true; };
const peak = a => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return m; };

// run `node` over T samples in blocks of B; mk(a, B) → {ins, P} for the block at absolute sample a; returns [L, R]
function drive(node, B, mk) {
  const L = new Float32Array(T), R = new Float32Array(T);
  for (let a = 0; a < T; a += B) {
    const outs = [[new Float32Array(B), new Float32Array(B)]], { ins, P } = mk(a, B);
    assert.notEqual(node.process(ins, outs, P), false);
    L.set(outs[0][0], a); R.set(outs[0][1], a);
  }
  return [L, R];
}
// the whole matrix for one processor: build(src) → a fresh node; oldSrc / newSrc are the file texts
function matrix(name, build, mk, { oldSrc, newSrc, silentOk = false, equalSplit = true, only256 = false }) {
  const N128 = drive(build(newSrc), 128, mk);
  if (!silentOk) assert.ok(peak(N128[0]) > 1e-4, name + ': the test signal must sound');
  if (oldSrc) { const O = drive(build(oldSrc), 128, mk); assert.ok(sameBits(O[0], N128[0]) && sameBits(O[1], N128[1]), name + ': 128 is bit-identical to ' + BASE); }
  const by = {}; for (const B of SIZES) { by[B] = B === 128 ? N128 : drive(build(newSrc), B, mk); assert.ok(finite(by[B][0]) && finite(by[B][1]), name + ' @' + B + ' finite'); if (!silentOk) assert.ok(peak(by[B][0]) > 1e-4, name + ' @' + B + ' sounds'); }
  if (equalSplit) assert.ok(sameBits(by[256][0], N128[0]) && sameBits(by[256][1], N128[1]), name + ': one 256 block = two 128 blocks');
  if (equalSplit && !only256) assert.ok(sameBits(by[384][0], N128[0]) && sameBits(by[64][0], N128[0]), name + ': 384 and 64 also equal 128');
}

/* ---------- ThunderFx (index.html, TFX_SRC) ---------- */
const HTML = H.read('index.html'), HTML0 = show('index.html');
// the worklet module is built on the page as these declarations' toString() + TFX_SRC (tfxReady): the same declarations are cut out with acorn
const ACORN = ['/opt/npm-tools/node_modules', path.join(H.ROOT, 'node_modules'), '/usr/lib/node_modules', '/usr/local/lib/node_modules'].find(d => fs.existsSync(path.join(d, 'acorn')));
const WK = ['rvMix', 'DegradeCore', 'VSampCore', 'ResonCore', 'PitchFxCore', 'clkSpb', 'FlexCore', 'StepGateCore', 'RetrigCore', 'beatLock', 'bkHash', 'GrainsCore', 'ScrambleCore', 'GlitchCore'];
function tfxSrc(h) {
  const acorn = require(path.join(ACORN, 'acorn')), i0 = h.indexOf('<script>\n/* ===ENGINE START=== */'), s0 = i0 + 9, src = h.slice(s0, h.indexOf('\n</script>', s0));
  const ast = acorn.parse(src, { ecmaVersion: 'latest', sourceType: 'script', allowReturnOutsideFunction: true }), body = [...ast.body];
  for (const st of ast.body) { const c = st.type === 'ExpressionStatement' && st.expression.type === 'CallExpression' && st.expression.callee; if (c && c.body && c.body.body && c.end - c.start > 200000) body.push(...c.body.body); }
  const by = new Map(); for (const st of body) if ((st.type === 'FunctionDeclaration' || st.type === 'ClassDeclaration') && !by.has(st.id.name)) by.set(st.id.name, src.slice(st.start, st.end));
  const a = h.indexOf('const TFX_SRC = `') + 'const TFX_SRC = `'.length, b = h.indexOf('`;\nlet tfxBytes', a); assert.ok(a > 20 && b > a);
  return WK.map(n => { assert.ok(by.has(n), n); return by.get(n); }).join('\n') + '\n' + new Function('return `' + h.slice(a, b) + '`')();
}
const TFXWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'chains/dsp/tfx.wasm')));
function fxBuild(h) {
  const reg = {}, posts = [];
  new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', 'currentTime', tfxSrc(h))(class { constructor() { this.port = { postMessage: m => posts.push(m), onmessage: null }; } }, (n, c) => { reg[n] = c; }, 48000, 0);
  let t = 0, node = null;
  for (; t < 40; t++) { node = new reg['thunder-tfx']({ processorOptions: { bytes: TFXWASM, chain: [{ key: 'a', slot: 0, t, on: true, p: new Array(12).fill(0.5) }, { key: 'f', slot: 1, t: 95, on: true, p: [3, 1, 0.5, 0] }] } }); if (node.slots[0] && node.slots[0].h >= 0) break; }
  assert.ok(node.slots[0].h >= 0, 'a wasm effect slot exists'); node.posts = posts; return node;
}
const fxMk = (a, B) => {
  const s = SIG.subarray(a, a + B), P = { bp: Float32Array.from({ length: B }, (_, i) => (a + i) / 24000), flx: [-1], gat: [-1], rtg: [-1] };
  for (let k = 0; k < 8; k++) P['sw' + k] = [-1]; for (const k in P) P[k] = Float32Array.from(P[k]);
  return { ins: [[s, s], [], []], P };
};
test('ThunderFx (thunder-tfx): 64 / 128 / 256 / 384 frames; 128 identical to aaf7554; a 256 block = two 128 blocks', { skip: !ACORN && 'needs acorn' }, () => {
  matrix('tfx', fxBuild, fxMk, { oldSrc: HTML0, newSrc: HTML });
});
test('ThunderFx: a block longer than 4096 fails soft (silence, one failed message, keeps running)', { skip: !ACORN && 'needs acorn' }, () => {
  const node = fxBuild(HTML), B = 4097, outs = [[new Float32Array(B).fill(1), new Float32Array(B).fill(1)]], { ins, P } = (() => { const s = new Float32Array(B), P = fxMk(0, 1).P; P.bp = new Float32Array(B); return { ins: [[s, s], [], []], P }; })();
  for (let i = 0; i < 3; i++) assert.equal(node.process(ins, outs, P), true);
  assert.equal(peak(outs[0][0]), 0); assert.deepEqual(node.posts.filter(m => m.failed), [{ failed: 'block size 4097' }]);
  assert.doesNotThrow(() => drive(node, 128, fxMk)); // and it goes on working
});

/* ---------- the pump synth (pump-synth-worklet.js) ---------- */
const PSROOT = 'pump/synth/';
function psMeta() {
  const c = { console: { warn() {}, log() {} }, Math, Float32Array, Uint8Array, WebAssembly, Object, Array, Set, Map, JSON, String, Number, isFinite };
  c.globalThis = c; c.self = c; vm.createContext(c);
  vm.runInContext(H.read(PSROOT + 'pump-synth-meta.js'), c); vm.runInContext(H.read(PSROOT + 'pump-synth-engine.js'), c);
  c.PsynCore.attachModule(new WebAssembly.Module(fs.readFileSync(path.join(H.ROOT, PSROOT + 'pump-synth.wasm')))); return c.PsynCore;
}
const PSC = psMeta(), PSB = new Uint8Array(fs.readFileSync(path.join(H.ROOT, PSROOT + 'pump-synth.wasm')));
const psBuild = kind => src => {
  const c = H.workletScope({ Date }); vm.runInContext(src, c, { filename: 'pump-synth-worklet.js' });
  return new c.__registered['thunder-psyn']({ processorOptions: Object.assign({ p: Object.assign(PSC.xeng()[kind].d(), { tune: 0, level: 0, drive: 0 }), bytes: PSB }, PSC.liveSpec(kind)) });
};
const psMk = (a, B) => { const P = { bpm: [120] }; for (let j = 0; j < 4; j++) { const on = j === 0 && a >= 768; Object.assign(P, { ['r' + j]: [1], ['g' + j]: [on ? 1 : 0], ['t' + j]: [on ? 1 : 0], ['l' + j]: [0], ['v' + j]: [100] }); }
  for (const k of ['cCut', 'cRes', 'cDec', 'cAtk', 'cRel']) P[k] = [-1]; for (const k of ['oCut', 'oRes', 'oEnv', 'oAtk', 'oDec', 'oSus', 'oRel']) P[k] = [0];
  for (const k in P) P[k] = Float32Array.from(P[k]); return { ins: [], P }; };
for (const kind of ['pmonol', 'ppolyl']) test('pump synth ' + kind + ': any block length; 128 identical to aaf7554; 256 = two 128', () => {
  matrix('psyn ' + kind, psBuild(kind), psMk, { oldSrc: show(PSROOT + 'pump-synth-worklet.js'), newSrc: H.read(PSROOT + 'pump-synth-worklet.js') });
});

/* ---------- chains (chains-worklet.js): ThunderChains and the standalone ChainsProcessor ---------- */
const hot = { modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type: 'utility', scope: 'mono', params: { gain: 6, pan: 0, mono: 0, flip: 0, mute: 0 } }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }],
  cables: [{ from: ['a', 'out'], to: ['b', 'in'] }, { from: ['a', 'outR'], to: ['b', 'inR'] }, { from: ['b', 'out'], to: ['c', 'in'] }, { from: ['b', 'outR'], to: ['c', 'inR'] }] };
const chBuild = name => src => {
  const ctx = H.freezeClock(H.workletScope()); vm.runInContext(src, ctx, { filename: 'chains-worklet.js' }); ctx.ChainsDSP.wasmInit({ tfxBytes: TFXWASM });
  const p = new ctx.__registered[name]({ processorOptions: { quality: 'full', track: 0, patch: name === 'thunder-chains' ? hot : undefined } });
  if (name === 'chains') { p.engine.loadPatch(hot); p.engine.setTrackInputs(SIG.subarray(0, 128), SIG.subarray(0, 128)); }
  return p;
};
const chMk = (a, B) => { const s = SIG.subarray(a, a + B); return { ins: [[s, s], []], P: {} }; };
for (const name of ['thunder-chains', 'chains']) test('chains ' + name + ': any block length; 128 identical to aaf7554; 256 = two 128', () => {
  matrix(name, chBuild(name), chMk, { oldSrc: show('chains/chains-worklet.js'), newSrc: H.read('chains/chains-worklet.js'), only256: name === 'chains' });   // (standalone: its track input is the fixed 128 frames the test hands the engine, so only a 256 block = two 128 blocks compares)
});

/* ---------- waves (waves-worklet.js): the standalone processor and waves live ---------- */
const wdoc = { format: 'waves.patch', version: 1, modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'a', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } },
  { id: 'ea', type: 'adsr', scope: 'poly', params: { a: .005, d: .2, s: .7, r: .05 } }, { id: 'ga', type: 'vca', scope: 'poly', params: { gain: 1 } }, { id: 'o1', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
  cables: [{ from: ['h', 'pitch'], to: ['a', 'pitch'] }, { from: ['h', 'gate'], to: ['ea', 'gate'] }, { from: ['a', 'out'], to: ['ga', 'in'] }, { from: ['ea', 'out'], to: ['ga', 'cv'] }, { from: ['ga', 'out'], to: ['o1', 'in'] }] };
const DSP = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm')));
const wvBuild = name => src => {
  const ctx = H.freezeClock(H.workletScope()); vm.runInContext(src, ctx, { filename: 'waves-worklet.js' }); ctx.WavesDSP && ctx.WavesDSP.wasmInit({ dspBytes: DSP, tfxBytes: TFXWASM });
  Math.random = () => .25;   // oscillators start at a random phase
  const p = new ctx.__registered[name]({ processorOptions: { doc: wdoc, p: { bus: 1 } } });
  if (name === 'waves') { p.engine.loadPatch(wdoc); const k = p.engine.voices.on(-12, 1); assert.ok(k >= 0); }
  return p;
};
const wvMk = (a, B) => { const P = { bpm: [120] }; for (let j = 0; j < 4; j++) { const on = j === 0 && a >= 768; Object.assign(P, { ['r' + j]: [1], ['g' + j]: [on ? 1 : 0], ['t' + j]: [on ? 1 : 0], ['l' + j]: [0], ['v' + j]: [100] }); } for (const k in P) P[k] = Float32Array.from(P[k]); return { ins: [], P }; };
const mathRandom = Math.random;
for (const name of ['waves', 'thunder-wlive']) test('waves ' + name + ': any block length; 128 identical to aaf7554; 256 = two 128', () => {
  try { matrix(name, wvBuild(name), wvMk, { oldSrc: show('waves/waves-worklet.js'), newSrc: H.read('waves/waves-worklet.js') }); } finally { Math.random = mathRandom; }
});

/* ---------- the master (master-worklet.js): length-agnostic already; the meter rate follows the frames ---------- */
test('master: any block length; 128 identical to aaf7554; the meter is sent at the same rate in time', () => {
  const build = src => { const posts = [], reg = {}; const ctx = H.workletScope(); vm.runInContext(src, ctx, { filename: 'master-worklet.js' });
    const p = new ctx.__registered['thunder-master']({ processorOptions: { set: { comp: { on: true }, lim: { on: true } } } }); p.port.postMessage = m => posts.push(m); p.posts = posts; return p; };
  const mk = (a, B) => { const s = SIG.subarray(a, a + B); return { ins: [[s, s]], P: undefined }; };
  const src = H.read('master-worklet.js');
  matrix('master', build, mk, { oldSrc: show('master-worklet.js'), newSrc: src });
  const rate = B => { const p = build(src); for (let i = 0; i < 20; i++) drive(p, B, mk); return p.posts.filter(m => m.meter).length; };
  const r128 = rate(128); assert.ok(r128 > 10); for (const B of [64, 256, 384]) assert.ok(Math.abs(rate(B) - r128) <= 4, 'meters at ' + B + ': ' + rate(B) + ' vs ' + r128);
});

/* ---------- the core (core-processor.js, bundled into core/seq-core.js) ---------- */
function coreScope(src) {
  let time = 0; const reg = {};
  const ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Map, Set, JSON, Date, Number, String, Object, Array, isFinite, parseInt, parseFloat,
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: 48000 };
  Object.defineProperty(ctx, 'currentTime', { get: () => time }); ctx.globalThis = ctx; vm.createContext(ctx); vm.runInContext(src, ctx, { filename: 'seq-core.js' });
  return { ctx, proc: new reg['thunder-core']({}), setTime: t => { time = t; } };
}
// a SoundFont voice (sfRun) over a looped sample, rendered in blocks of B (the time moves on with the block)
function coreSf(src, B) {
  const { ctx, proc, setTime } = coreScope(src), n = 48000, d = new Float32Array(n).map((_, i) => 0.5 * Math.sin(i * 0.05));
  const v = vm.runInContext('(function(d, n){ const A = x => new Auto(x), z = {env:A(1), r:A(1), flt:null, L:d, R:null, n, sr:48000, lp:true, ls:100, le:n - 10, pos:0, done:false, zp:null};' +
    ' return {sf:[z], st:false, g:A(1), f:A(1), pn:A(0), r:A(1), base:1, t0:0, end:Infinity, track:0, ended:false}; })', ctx)(d, n);
  const L = new Float32Array(T), R = new Float32Array(T);
  for (let a = 0; a < T; a += B) { setTime(a / 48000); const out = [new Float32Array(B), new Float32Array(B)]; proc.sfRun(v, out, B); L.set(out[0], a); R.set(out[1], a); }
  return [L, R];
}
test('core: a SoundFont voice at 64 / 128 / 256 / 384 frames (sfRun used a 128-long scratch: NaN past 128); 128 identical to aaf7554', () => {
  const now = H.read('core/seq-core.js'), old = show('core/seq-core.js'), A = coreSf(now, 128);
  assert.ok(peak(A[0]) > 0.1);
  if (old) { const O = coreSf(old, 128); assert.ok(sameBits(O[0], A[0]) && sameBits(O[1], A[1]), '128 identical'); }
  for (const B of [64, 256, 384]) { const X = coreSf(now, B); assert.ok(finite(X[0]) && finite(X[1]), '@' + B + ' finite'); assert.ok(sameBits(X[0], A[0]) && sameBits(X[1], A[1]), '@' + B + ' equals @128'); }
});
test('core: the parameter override buffer follows the block length', () => {
  const { ctx, proc, setTime } = coreScope(H.read('core/seq-core.js'));
  const run = vm.runInContext('(function(proc){ const ov = tcOver(proc), A = tcParam(ov, "x", 0); A.set(0, 0); A.lin(1, 1); return n => globalThis.__tcOv(proc, {x: new Float32Array(n)}, n).x; })', ctx)(proc);
  for (const n of [128, 256, 64, 384, 128]) { setTime(0); const x = run(n); assert.equal(x.length, n, 'a buffer of ' + n); assert.ok(finite(x) && x[n - 1] > x[0]); }
});
