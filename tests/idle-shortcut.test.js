// SILENCE SHORTCUTS (10 Nov, battery finding 3): the master worklet and a ThunderFx node clocked only by Flex / Step gate stop doing
// per-sample work while the input is exactly silent and every tail is dead, and wake in the very block sound arrives.
// Each is compared with the processor as it was before (git show HEAD~1 of the branch base: the text without the shortcut).
// Skipped without git history. Offline: vm only.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path'), cp = require('child_process');
const H = require('./helpers');
const BASE = '27c4f51';   // the commit before the shortcuts
const show = f => { try { return cp.execFileSync('git', ['show', BASE + ':' + f], { cwd: H.ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { return null; } };
const SR = 48000, B = 128;

/* ---------- master ---------- */
function mk(src, opts) {
  const ctx = H.freezeClock(H.workletScope({ sampleRate: SR })); vm.runInContext(src, ctx, { filename: 'master-worklet.js' });
  const posts = [], P = new ctx.__registered['thunder-master']({ processorOptions: opts });
  P.port.postMessage = m => posts.push(m); return { P, posts };
}
let seed = 7; const rnd = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff - .5; };
function plan(loud1, quiet, loud2) { const s = new Float32Array((loud1 + quiet + loud2) * B); seed = 7;
  for (let i = 0; i < loud1 * B; i++) s[i] = Math.fround(rnd() * 1.6); for (let i = (loud1 + quiet) * B; i < s.length; i++) s[i] = Math.fround(rnd() * 1.6); return s; }
function runM(src, opts, sig) {
  const { P, posts } = mk(src, opts), out = new Float32Array(sig.length), blocksOut = [];
  for (let b = 0; b < sig.length / B; b++) { const a = sig.subarray(b * B, (b + 1) * B), o = [new Float32Array(B), new Float32Array(B)]; P.process([[a, a]], [o]); out.set(o[0], b * B); }
  return { out, P, posts };
}
const NEW = fs.readFileSync(path.join(H.ROOT, 'master-worklet.js'), 'utf8'), OLD = show('master-worklet.js');
for (const [name, set] of [['comp + limiter + meters', { comp: { on: true }, lim: { on: true } }], ['limiter only', { lim: { on: true } }], ['all off', {}]]) {
  test('master shortcut (' + name + '): silent blocks are skipped once the tails are dead; sound resumes in the same block; output matches the old code', { skip: !OLD && 'needs git history' }, () => {
    const LOUD1 = 300, QUIET = 5000, LOUD2 = 60, sig = plan(LOUD1, QUIET, LOUD2), opts = { meters: true, set };
    const n = runM(NEW, opts, sig), o = runM(OLD, opts, sig);
    assert.ok(n.P.sk > 1000, 'the shortcut was taken for most of the silence: ' + n.P.sk);
    assert.equal(o.P.sk, undefined);
    // the silence itself is silent, exactly
    for (let i = (LOUD1 + 1) * B + 4096; i < (LOUD1 + QUIET) * B; i++) assert.equal(n.out[i], 0);
    // before the first shortcut and through the silence: bit-identical; after waking: the first block with sound, and the rest, within a float32 step
    let worst = 0, pk = 0; for (let i = 0; i < sig.length; i++) { worst = Math.max(worst, Math.abs(n.out[i] - o.out[i])); pk = Math.max(pk, Math.abs(o.out[i])); }
    assert.ok(worst <= 2e-7 * pk, 'max difference ' + worst + ' (peak ' + pk + ')');
    // the new sound is not dropped: the limiter's own 3 ms lookahead delays it past the first block (as before), and it matches from there
    const f = (LOUD1 + QUIET) * B; let sum = 0; for (let i = f; i < f + 2 * B; i++) sum += Math.abs(n.out[i]); assert.ok(sum > 1, 'sound in the wake blocks');
    // the meters keep reporting at the same cadence with the same peaks
    assert.equal(n.posts.length, o.posts.length);
    for (let k = 0; k < n.posts.length; k++) { const a = n.posts[k].meter, b = o.posts[k].meter; assert.equal(a.inPk, b.inPk); assert.ok(Math.abs(a.outPk - b.outPk) <= 2e-7); }
  });
}
test('master shortcut: never while the suppressor is on, or the limiter has not recovered, or input is not exactly zero', { skip: !OLD && 'needs git history' }, () => {
  const sig = plan(100, 3000, 0), sup = runM(NEW, { meters: false, set: { sup: { on: true }, lim: { on: true } } }, sig);
  assert.equal(sup.P.sk, 0);
  const { P } = mk(NEW, { meters: false, set: { lim: { on: true } } }); const z = new Float32Array(B), o = () => [new Float32Array(B), new Float32Array(B)];
  for (let b = 0; b < 200; b++) P.process([[z, z]], [o()]);
  const a = P.sk; assert.ok(a > 100); const tiny = new Float32Array(B); tiny[127] = 1e-9; P.process([[tiny, tiny]], [o()]); assert.equal(P.sk, a, 'a 1e-9 sample is not silence');
  P.process([[z, z]], [o()]); assert.equal(P.sk, a, 'and the zero run starts again');
});

/* ---------- ThunderFx: Flex / Step gate only, clock stopped ---------- */
const HTML = H.read('index.html'), HTML0 = show('index.html');
const ACORN = ['/opt/npm-tools/node_modules', path.join(H.ROOT, 'node_modules'), '/usr/lib/node_modules', '/usr/local/lib/node_modules'].find(d => fs.existsSync(path.join(d, 'acorn')));
const WK = ['rvMix', 'DegradeCore', 'VSampCore', 'ResonCore', 'PitchFxCore', 'clkSpb', 'FlexCore', 'StepGateCore', 'RetrigCore', 'beatLock', 'bkHash', 'GrainsCore', 'ScrambleCore', 'GlitchCore'];
function tfxSrc(h) {
  const acorn = require(path.join(ACORN, 'acorn')), i0 = h.indexOf('<script>\n/* ===ENGINE START=== */'), s0 = i0 + 9, src = h.slice(s0, h.indexOf('\n</script>', s0));
  const ast = acorn.parse(src, { ecmaVersion: 'latest', sourceType: 'script', allowReturnOutsideFunction: true }), body = [...ast.body];
  for (const st of ast.body) { const c = st.type === 'ExpressionStatement' && st.expression.type === 'CallExpression' && st.expression.callee; if (c && c.body && c.body.body && c.end - c.start > 200000) body.push(...c.body.body); }
  const by = new Map(); for (const st of body) if ((st.type === 'FunctionDeclaration' || st.type === 'ClassDeclaration') && !by.has(st.id.name)) by.set(st.id.name, src.slice(st.start, st.end));
  const a = h.indexOf('const TFX_SRC = `') + 'const TFX_SRC = `'.length, b = h.indexOf('`;\nlet tfxBytes', a);
  return WK.map(n => by.get(n)).join('\n') + '\n' + new Function('return `' + h.slice(a, b) + '`')();
}
const TFXWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'chains/dsp/tfx.wasm')));
function fxNode(h, extra) {
  const reg = {}; let ts = 0;
  new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', 'currentTime', tfxSrc(h))(class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, (n, c) => { reg[n] = c; }, SR, 0);
  const chain = [{ key: 'f', slot: 0, t: 95, on: true, p: [3, 1, 0.5, 0] }].concat(extra || []);
  const node = new reg['thunder-tfx']({ processorOptions: { bytes: TFXWASM, chain } }); return node;
}
function fxP(len, clockFrom) { const P = { bp: new Float32Array(len).fill(-1), flx: [-1], gat: [-1], rtg: [-1] }; for (let k = 0; k < 8; k++) P['sw' + k] = [-1];
  if (clockFrom != null) for (let i = 0; i < len; i++) P.bp[i] = clockFrom + i / 24000; for (const k in P) if (!(P[k] instanceof Float32Array)) P[k] = Float32Array.from(P[k]); return P; }
function runFx(h, extra, silent, tail) {   // sound, `silent` blocks of silence with the clock stopped, then sound again with the clock running (a Flex pattern)
  const node = fxNode(h, extra), out = []; let c = 0;
  const sig = H.testSignal(B * 40), step = (a, bp) => { const s = a || new Float32Array(B), o = [new Float32Array(B), new Float32Array(B)]; const P = fxP(B, bp); P.flx[0] = bp != null ? 0x24 : -1; node.process([[s, s], [], []], [o], P); out.push(o[0]); };
  for (let b = 0; b < 40; b++) step(sig.subarray(b * B, (b + 1) * B), null);
  for (let b = 0; b < silent; b++) step(null, null);
  for (let b = 0; b < tail; b++) step(sig.subarray(b * B, (b + 1) * B), 8 + b * B / 24000);
  return { node, out };
}
const skipFx = { skip: (!ACORN && 'needs acorn') || (!HTML0 && 'needs git history') };
test('ThunderFx (Flex only, clock stopped): sleeps after 14 s of silence; wakes in the block the sound and the clock arrive, same output as before', skipFx, () => {
  const silent = Math.ceil(16 * SR / B), n = runFx(HTML, null, silent, 40), o = runFx(HTML0, null, silent, 40);
  assert.equal(n.out.length, o.out.length);
  let worst = 0; for (let b = 0; b < n.out.length; b++) for (let i = 0; i < B; i++) worst = Math.max(worst, Math.abs(n.out[b][i] - o.out[b][i]));
  assert.ok(worst <= 1e-6, 'max difference ' + worst);
  assert.equal(n.node.chard, false); assert.equal(n.node.clocked, true);
  let sound = 0; for (const v of n.out[n.out.length - 1]) sound += Math.abs(v); assert.ok(sound > 0.1, 'the wake blocks sound');
});
test('ThunderFx (Flex only): the shortcut really skips the slots (silent, stopped, 14 s) and does not before; a running clock keeps it awake', skipFx, () => {
  const node = fxNode(HTML); let calls = 0; const run = node.slotRun; node.slotRun = function (...a) { calls++; return run.apply(this, a); };
  const o = () => [new Float32Array(B), new Float32Array(B)], z = new Float32Array(B);
  for (let b = 0; b < Math.ceil(13 * SR / B); b++) node.process([[z, z], [], []], [o()], fxP(B, null));
  assert.ok(calls > 4000, 'awake for the first 14 s (a 13 s Flex ring may still hold old sound): ' + calls);
  for (let b = 0; b < Math.ceil(2 * SR / B); b++) node.process([[z, z], [], []], [o()], fxP(B, null));
  const c0 = calls; for (let b = 0; b < 100; b++) node.process([[z, z], [], []], [o()], fxP(B, null)); assert.equal(calls, c0, 'asleep');
  node.process([[z, z], [], []], [o()], fxP(B, 8)); assert.ok(calls > c0, 'a running clock wakes it in the same block');
});
test('ThunderFx: a Retrigger / Grains slot (or a clocked module) keeps a clocked node awake even when stopped', skipFx, () => {
  const node = fxNode(HTML, [{ key: 'r', slot: 1, t: 93, on: true, p: [0, 2, 0.5, 0, 0.5, 1, 0] }]); assert.equal(node.chard, true);
});
