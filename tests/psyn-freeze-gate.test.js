// FREEZE's one-time spectra build (pump 24a24e1, in psyn_new: about 0.9 s, once per wasm instance) runs on the audio thread when a live
// node is made, and stops every node in the context. Build 643's rule (index.html: psWarm, psFreezeDefer, psWarmUp, psNode, tStop):
// warm while stopped; while playing, a FREEZE pad in an unwarmed context gets no live node (its render plays) until STOP.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const html = H.read('index.html');
const a = html.indexOf('const psWarm = new WeakSet()'), b = html.indexOf('/* …WAVES LIVE: as psNode');   // helpers .. (psNode is between: cut the helpers only)
const hEnd = html.indexOf("\n}\n", html.indexOf('function psWarmUp(ac)')) + 3;
const helperSrc = html.slice(a, hEnd);

function world(over) {
  const made = [], posted = [], disc = [];
  class Node { constructor(ac, name, o) { this.name = name; this.o = o; this.port = { postMessage: m => posted.push(m), onmessage: null }; made.push(this); } connect() {} disconnect() { disc.push(this); } }
  const ac = { createGain: () => ({ gain: {}, connect() {}, disconnect() {} }), destination: {} };
  const c = Object.assign({ AudioWorkletNode: Node, ctx: ac, tp: { on: false }, psOK: new WeakSet([ac]), psSent: new WeakSet(), psLive: { off: false }, aset: { psLive: 'auto' },
    kit: { voices: [{ engine: 'pfreezel' }] }, isPsV: v => !!v, WASM_PRECOMPILE: false, tpLog() {}, soon: 0, liveWarmSoon() { c.soon++; },
    PsynCore: { liveSpec: () => ({ spec: 1 }), bytes: () => new Uint8Array(4), module: () => null, xeng: () => ({ pfreezel: { d: () => ({ a: 1 }) } }) } }, over || {});
  vm.createContext(c); vm.runInContext(helperSrc + '\nthis.__x = {psWarm, psWarming, psIsFreeze, psFreezeDefer, psWarmUp};', c);
  return { c, ac, made, posted, disc, X: c.__x };
}

test('FREEZE gate: not warmed + playing => no live node (the render plays); warmed, or stopped, or another engine => a live node', () => {
  const { X } = world();
  assert.equal(X.psFreezeDefer('pfreezel', false, true), true);
  assert.equal(X.psFreezeDefer('pfreezel', true, true), false, 'warmed: live again');
  assert.equal(X.psFreezeDefer('pfreezel', false, false), false, 'stopped: the node may be made (it does the work)');
  for (const k of ['pmonol', 'ppolyl', 'pcoill', 'pglossl', 'pwobl']) assert.equal(X.psFreezeDefer(k, false, true), false, k + ' is not gated');
});

test('FREEZE warm-up: one throwaway node while stopped with a FREEZE pad; none playing, none without FREEZE; done when the node says opened', () => {
  let w = world({ tp: { on: true } });
  assert.equal(w.X.psWarmUp(w.ac), false); assert.equal(w.made.length, 0, 'playing: nothing is made (it would stall the audio)');
  w = world({ kit: { voices: [{ engine: 'pmonol' }, null] } });
  assert.equal(w.X.psWarmUp(w.ac), false); assert.equal(w.made.length, 0, 'no FREEZE pad: no cost');
  w = world({ aset: { psLive: 'render' } });
  assert.equal(w.X.psWarmUp(w.ac), false, 'Settings: live engines off');
  w = world();
  assert.equal(w.X.psWarmUp(w.ac), true); assert.equal(w.made.length, 1); assert.equal(w.made[0].name, 'thunder-psyn');
  assert.equal(w.X.psWarmUp(w.ac), false, 'already warming'); assert.equal(w.made.length, 1);
  assert.equal(w.X.psWarm.has(w.ac), false, 'not warmed until the node reports');
  w.made[0].port.onmessage({ data: { opened: 1 } });
  assert.equal(w.X.psWarm.has(w.ac), true); assert.ok(w.posted.some(m => m && m.dispose), 'the throwaway is disposed'); assert.ok(w.c.soon >= 1);
  assert.equal(w.X.psWarmUp(w.ac), false, 'warm: nothing more to do'); assert.equal(w.made.length, 1);
});

test('FREEZE gate: the pieces are wired (psNode gate before the node is made, real nodes mark the scope warm, liveWarm and STOP start the warm-up, the worklet says opened)', () => {
  const ps = html.slice(html.indexOf('function psNode(t, pad, V, kind, key){'), html.indexOf('/* …WAVES LIVE: as psNode'));
  assert.ok(ps.indexOf('psFreezeDefer(kind, psWarm.has(ac), tp.on)') > 0 && ps.indexOf('psFreezeDefer(') < ps.indexOf('new AudioWorkletNode'), 'gate precedes the node');
  assert.match(ps, /d\.opened\)\{ if \(psIsFreeze\(kind\)\) psWarm\.add\(ac\)/);
  assert.match(html, /function liveWarm\(max\)\{[^\n]*psWarmUp\(ctx\)/);
  const stop = html.slice(html.indexOf('function tStop(quiet){'), html.indexOf('function tStop(quiet){') + 3000);
  assert.match(stop, /psWarm\.has\(ctx\)[^\n]*liveWarmSoon\(300\)/);
  const wk = H.read('pump/synth/pump-synth-worklet.js');
  assert.match(wk, /this\.h = X\.psyn_new\(this\.type, sampleRate\);[^\n]*\n[^\n]*postMessage\(\{opened:1\}\)/);
  assert.ok(html.includes("pump-synth-worklet.js?v=13") && H.read('sw.js').includes('pump-synth-worklet.js?v=13'));
});
