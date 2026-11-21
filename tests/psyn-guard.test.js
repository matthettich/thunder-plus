// The pump synth worklet (PsynProc) guards its wasm (10 Nov): a throw out of process() would stop Chrome calling the node for good, and a
// NaN / Infinity in the engine's output would go into the mix. A fake wasm exports object (the real one with psyn_process replaced)
// throws, or writes NaN / Infinity into the output: process() must not throw, the block must be silent, the page must be told once
// ({failed:'pump-synth: ...'}; {nan:n} for a reset), and after a failure the wasm must not be called again. The real wasm is covered
// by tests/pump-synth.test.js and tests/eng-targets-pump.test.js (unchanged) and tests/block-size.test.js (bit-identical to before).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path');
const H = require('./helpers');

const ROOT = 'pump/synth/';
function core() {
  const c = { console: { warn() {}, log() {} }, Math, Float32Array, Uint8Array, WebAssembly, Object, Array, Set, Map, JSON, String, Number, isFinite };
  c.globalThis = c; c.self = c; vm.createContext(c);
  vm.runInContext(H.read(ROOT + 'pump-synth-meta.js'), c); vm.runInContext(H.read(ROOT + 'pump-synth-engine.js'), c);
  c.PsynCore.attachModule(new WebAssembly.Module(fs.readFileSync(path.join(H.ROOT, ROOT + 'pump-synth.wasm')))); return c.PsynCore;
}
const CORE = core(), BYTES = new Uint8Array(fs.readFileSync(path.join(H.ROOT, ROOT + 'pump-synth.wasm')));
// a node whose wasm is the real one with psyn_process (called with the call number) replaced by `hook(real, h, n, call)`
function rig(hook, kind = 'pmonol') {
  const c = H.workletScope({ Date }); vm.runInContext(H.read(ROOT + 'pump-synth-worklet.js'), c, { filename: 'pump-synth-worklet.js' });
  const node = new c.__registered['thunder-psyn']({ processorOptions: Object.assign({ p: Object.assign(CORE.xeng()[kind].d(), { tune: 0, level: 0, drive: 0 }), bytes: BYTES }, CORE.liveSpec(kind)) });
  const posts = []; node.port.postMessage = m => posts.push(m);
  const P = () => { const o = { bpm: [120] }; for (let j = 0; j < 4; j++) Object.assign(o, { ['r' + j]: [1], ['g' + j]: [j === 0 ? 1 : 0], ['t' + j]: [j === 0 ? 1 : 0], ['l' + j]: [0], ['v' + j]: [100] });
    for (const k of ['cCut', 'cRes', 'cDec', 'cAtk', 'cRel']) o[k] = [-1]; for (const k of ['oCut', 'oRes', 'oEnv', 'oAtk', 'oDec', 'oSus', 'oRel']) o[k] = [0];
    for (const k in o) o[k] = Float32Array.from(o[k]); return o; };
  const blk = n => [[new Float32Array(n), new Float32Array(n)]];
  node.process([], blk(128), P());   // the first block instantiates the real wasm
  const real = c.__psynX; assert.ok(real && real.psyn_process, 'the wasm is there');
  const st = { calls: 0, after: 0, failedAt: -1, news: 0 };
  const fake = Object.assign({}, real, { psyn_process(h, n) { st.calls++; if (node.failed) st.after++; return hook(real, h, n, st.calls); },
    psyn_new(...a) { st.news++; return real.psyn_new(...a); } });
  for (const k of Object.keys(real)) if (typeof real[k] === 'function' && !(k in fake)) fake[k] = real[k];
  c.__psynX = fake;
  const run = n => { const o = blk(n); let r; assert.doesNotThrow(() => { r = node.process([], o, P()); }); return { o, r }; };
  return { node, posts, run, st, real };
}
const peak = a => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return m; };
const poke = (real, n, v, at = 5) => { new Float32Array(real.memory.buffer, real.psyn_out(0), n)[at] = v; };

test('psyn guard: a wasm trap -> that block is silent, one {failed}, and the wasm is never called again', () => {
  const R = rig((real, h, n, call) => { if (call === 3) throw new Error('unreachable executed'); return real.psyn_process(h, n); });
  const a = R.run(128), b = R.run(128); assert.ok(peak(a.o[0][0]) + peak(b.o[0][0]) > 1e-4, 'it sounds before the trap');
  const t = R.run(128); assert.equal(t.r, true); assert.equal(peak(t.o[0][0]), 0); assert.equal(peak(t.o[0][1]), 0);
  assert.equal(JSON.stringify(R.posts.filter(m => m.failed)), JSON.stringify([{ failed: 'pump-synth: unreachable executed' }]));
  const calls = R.st.calls; for (let i = 0; i < 20; i++) { const x = R.run(128); assert.equal(peak(x.o[0][0]), 0); }
  assert.equal(R.st.calls, calls, 'no wasm call after the failure'); assert.equal(R.posts.filter(m => m.failed).length, 1, 'told once');
});
test('psyn guard: a trap in a 256-frame block silences the whole block (nothing half written)', () => {
  const R = rig((real, h, n, call) => { if (call === 2) throw new Error('boom'); return real.psyn_process(h, n); });
  R.run(256); const t = R.run(256); assert.equal(peak(t.o[0][0]), 0); assert.equal(peak(t.o[0][1]), 0); assert.equal(R.posts.filter(m => m.failed).length, 1);
});
for (const bad of [NaN, Infinity, -Infinity]) test('psyn guard: ' + bad + ' in the output -> block zeroed, engine made afresh, {nan} counted, sound comes back', () => {
  const R = rig((real, h, n, call) => { const r = real.psyn_process(h, n); if (call === 4) poke(real, n, bad); return r; });
  let last = null; const news0 = R.st.news; const out = [];
  for (let i = 0; i < 12; i++) { const x = R.run(128); out.push(x.o[0][0]); for (const c of x.o[0]) assert.ok(c.every(Number.isFinite), 'block ' + i + ' finite'); }
  assert.equal(peak(out[3]), 0, 'the bad block is silent'); assert.equal(JSON.stringify(R.posts.filter(m => m.nan)), JSON.stringify([{ nan: 1 }])); assert.equal(R.posts.filter(m => m.failed).length, 0);
  assert.equal(R.st.news, news0 + 1, 'a new engine'); assert.ok(peak(out[11]) > 1e-4, 'it sounds again (the held gate starts its note again)');
});
test('psyn guard: an engine that is always NaN is given up on after 8 resets (silence, one {failed})', () => {
  const R = rig((real, h, n) => { const r = real.psyn_process(h, n); poke(real, n, NaN); return r; });
  for (let i = 0; i < 40; i++) { const x = R.run(128); assert.equal(peak(x.o[0][0]), 0); for (const c of x.o[0]) assert.ok(c.every(Number.isFinite)); }
  assert.equal(R.posts.filter(m => m.nan).length, 8); assert.equal(R.posts.filter(m => m.failed).length, 1); assert.match(R.posts.find(m => m.failed).failed, /^pump-synth: /);
  const calls = R.st.calls; R.run(128); assert.equal(R.st.calls, calls);
});
test('psyn guard: a NaN in a 384-frame block (chunked) is found too', () => {
  const R = rig((real, h, n, call) => { const r = real.psyn_process(h, n); if (call === 5) poke(real, n, NaN, n - 1); return r; });
  for (let i = 0; i < 6; i++) { const x = R.run(384); for (const c of x.o[0]) assert.ok(c.every(Number.isFinite)); }
  assert.equal(JSON.stringify(R.posts.filter(m => m.nan)), JSON.stringify([{ nan: 1 }]));
});
