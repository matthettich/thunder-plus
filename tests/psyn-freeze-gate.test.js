// FREEZE's one-time spectra build (pump 24a24e1, in psyn_new: about 0.9 s, once per wasm instance) runs on the audio thread when a live
// node is made, and stops every node in the context. THE RULE (index.html, comment above psWarm): the build happens only while the
// transport is stopped AND the output is quiet; never at PLAY, never during play, never while a tail rings; until it has happened FREEZE
// pads play their render (psNode returns null).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const html = H.read('index.html');
const a = html.indexOf('const psWarm = new WeakSet()');
const hEnd = html.indexOf("\n}\n", html.indexOf('function psWarmUp(ac)')) + 3;
const helperSrc = html.slice(a, hEnd);

// a world with a fake clock and timers: advance(ms) runs the timers due, in order
function world(over) {
  const made = [], posted = [], timers = []; let now = 1e6;
  class Node { constructor(ac, name, o) { this.name = name; this.o = o; this.port = { postMessage: m => posted.push(m), onmessage: null }; made.push(this); } connect() {} disconnect() {} }
  const level = { v: 0 };
  const an = { fftSize: 8, getFloatTimeDomainData(b) { b.fill(level.v); } };
  const ac = { state: 'running', createGain: () => ({ gain: {}, connect() {}, disconnect() {} }), destination: {} };
  const c = Object.assign({ AudioWorkletNode: Node, ctx: ac, tp: { on: false }, psOK: new WeakSet([ac]), psSent: new WeakSet(), psLive: { off: false }, aset: { psLive: 'auto' },
    rt: { man: [an, an] }, RCD: { on: false }, kit: { voices: [{ engine: 'pfreezel' }] }, isPsV: v => !!v, WASM_PRECOMPILE: false, tpLog() {}, soon: 0, liveWarmSoon() { c.soon++; },
    performance: { now: () => now }, Float32Array, Math,
    setTimeout: (f, ms) => { const t = { f, at: now + (ms || 0), id: timers.length + 1 }; timers.push(t); return t.id; }, clearTimeout: id => { const t = timers.find(x => x.id === id); if (t) t.dead = true; },
    PsynCore: { liveSpec: () => ({ spec: 1 }), bytes: () => new Uint8Array(4), module: () => null, xeng: () => ({ pfreezel: { d: () => ({ a: 1 }) } }) } }, over || {});
  vm.createContext(c); vm.runInContext(helperSrc + '\nthis.__x = {psWarm, psWarming, psIsFreeze, psFreezeDefer, psWarmUp, psWarmSoon, psBusy, psNeeds, get psFails(){return psFails;}, setGo(t){psGoT = t;}};', c);
  const advance = ms => { const end = now + ms; for (;;) { const t = timers.filter(x => !x.dead && !x.ran && x.at <= end).sort((p, q) => p.at - q.at)[0]; if (!t) break; t.ran = true; now = Math.max(now, t.at); t.f(); } now = end; };
  return { c, ac, made, posted, X: c.__x, level, advance, now: () => now };
}

test('FREEZE gate: a FREEZE live node is not made until the scope is warm, whatever the transport does; other engines are not gated', () => {
  const { X } = world();
  assert.equal(X.psFreezeDefer('pfreezel', false), true);
  assert.equal(X.psFreezeDefer('pfreezel', true), false, 'warm: live');
  for (const k of ['pmonol', 'ppolyl', 'pcoill', 'pglossl', 'pwobl']) assert.equal(X.psFreezeDefer(k, false), false, k + ' is not gated');
});

test('FREEZE rule: PLAY, a restart or a jump counts as playing (also with tp.on cleared by the restart); STOP ends it', () => {
  const w = world(); const { X, c } = w;
  assert.equal(X.psBusy(), false, 'idle');
  c.tp.on = true; assert.equal(X.psBusy(), true, 'playing');
  c.tp.on = false; X.setGo(w.now()); assert.equal(X.psBusy(), true, 'tStart stamped it: PLAY is starting, or a restart cleared tp.on before liveWarm');
  w.advance(3500); assert.equal(X.psBusy(), false, 'a PLAY that never started does not block for ever');
  // the build itself refuses while busy
  X.setGo(w.now()); assert.equal(X.psWarmUp(w.ac), false); assert.equal(w.made.length, 0);
});

test('FREEZE warm-up: only when stopped and quiet (-70 dBFS for 0.5 s), never at PLAY or during play, not while recording', () => {
  let w = world(); w.level.v = 0.05; w.X.psWarmSoon(1000, true);
  w.advance(5000); assert.equal(w.made.length, 0, 'a tail is ringing: it waits');
  w.level.v = 0; w.advance(300); assert.equal(w.made.length, 0, 'quiet for less than 0.5 s: not yet');
  w.level.v = 0.05; w.advance(500); w.level.v = 0; w.advance(1500); assert.equal(w.made.length, 1, 'quiet for 0.5 s: the throwaway is made once');
  w = world(); w.c.tp.on = true; w.X.psWarmSoon(500, true); w.advance(20000); assert.equal(w.made.length, 0, 'playing: never');
  w.c.tp.on = false; w.X.setGo(w.now()); w.advance(2000); assert.equal(w.made.length, 0, 'PLAY under way: never');
  w = world(); w.c.RCD.on = true; w.X.psWarmSoon(500, true); w.advance(20000); assert.equal(w.made.length, 0, 'recording: never');
  w.c.RCD.on = false; w.advance(2000); assert.equal(w.made.length, 1);
  w = world({ kit: { voices: [{ engine: 'pmonol' }, null] } }); w.X.psWarmSoon(500, true); w.advance(20000); assert.equal(w.made.length, 0, 'no FREEZE pad: no cost');
  w = world({ aset: { psLive: 'render' } }); w.X.psWarmSoon(500, true); w.advance(20000); assert.equal(w.made.length, 0, 'Settings: live engines off');
  w = world({ rt: { man: null } }); w.X.psWarmSoon(500, true); w.advance(3000); assert.equal(w.made.length, 0, 'no level source: waits a fixed long time');
  w.advance(3000); assert.equal(w.made.length, 1);
});

test('FREEZE warm-up: opened marks the scope warm and disposes the throwaway; failed does NOT (retries 3 s, 6 s, 12 s, then stops); no answer in 10 s is a failure', () => {
  let w = world(); w.X.psWarmSoon(500, true); w.advance(2000); assert.equal(w.made.length, 1);
  assert.equal(w.X.psWarm.has(w.ac), false, 'not warm until the node reports');
  w.made[0].port.onmessage({ data: { opened: 1 } });
  assert.equal(w.X.psWarm.has(w.ac), true); assert.ok(w.posted.some(m => m && m.dispose)); assert.equal(w.X.psNeeds(), false);
  // failed
  w = world(); w.X.psWarmSoon(500, true); w.advance(2000); w.made[0].port.onmessage({ data: { failed: 'trap' } });
  assert.equal(w.X.psWarm.has(w.ac), false, 'failed: NOT warm (the real node would fail too and be silent)'); assert.equal(w.X.psWarming.has(w.ac), false, 'warming cleared');
  assert.equal(w.X.psFails, 1); assert.equal(w.X.psFreezeDefer('pfreezel', w.X.psWarm.has(w.ac)), true, 'the render keeps playing');
  w.advance(2000); assert.equal(w.made.length, 1, 'not before 3 s'); w.advance(2500); assert.equal(w.made.length, 2, 'retry after 3 s');
  w.made[1].port.onmessage({ data: { failed: 'trap' } }); w.advance(6500); assert.equal(w.made.length, 3, 'then after 6 s');
  w.made[2].port.onmessage({ data: { failed: 'trap' } }); w.advance(60000); assert.equal(w.made.length, 3, 'three attempts, then it stops');
  // silence for 10 s
  w = world(); w.X.psWarmSoon(500, true); w.advance(2000); assert.equal(w.made.length, 1); assert.equal(w.X.psWarming.has(w.ac), true);
  w.advance(10500); assert.equal(w.X.psWarming.has(w.ac), false, 'timeout clears psWarming'); assert.equal(w.X.psWarm.has(w.ac), false);
  w.advance(4000); assert.equal(w.made.length, 2, 'and it retries');
});

test('FREEZE rule: the pieces are wired (gate before the node, PLAY stamp, restart keeps it, no warm-up in liveWarm, kit and edit triggers, STOP)', () => {
  const ps = html.slice(html.indexOf('function psNode(t, pad, V, kind, key){'), html.indexOf('/* …WAVES LIVE: as psNode'));
  assert.ok(ps.indexOf('psFreezeDefer(kind, psWarm.has(ac))') > 0 && ps.indexOf('psFreezeDefer(') < ps.indexOf('new AudioWorkletNode'), 'gate precedes the node');
  assert.match(ps, /d\.opened\)\{ if \(psIsFreeze\(kind\)\) psWarm\.add\(ac\)/);
  assert.ok(!/function liveWarm\(max\)\{[^\n]*psWarmUp/.test(html), 'liveWarm (also run at PLAY) never builds');
  assert.match(html, /function tStart\(mode, o\)\{\n  psGoT = performance\.now\(\)/, 'tStart stamps PLAY first');
  assert.match(html, /psKeep = true; if \(lk\) lk\.restart = true; try \{ tStop\(true\); \} finally \{ psKeep = false/, 'the restart\'s own tStop keeps the stamp');
  assert.match(html, /if \(!psKeep\) psGoT = 0; psWarmSoon\(300\)/, 'STOP clears it and asks for the idle warm-up');
  assert.match(html, /function setKit\(k, noUndo\)\{[\s\S]*?psWarmSoon\(1500, true\); \}\n\/\/ Before replacing the kit/, 'a kit load asks');
  assert.match(html, /function changed\(i = cur, light\)\{\n  psWarmSoon\(1500, true\)/, 'a sound edit (an engine switch) asks');
  assert.match(html, /function liveWarmSoon\(ms\)\{ psWarmSoon\(/, 'a song change asks');
  const wk = H.read('pump/synth/pump-synth-worklet.js');
  assert.match(wk, /this\.h = X\.psyn_new\(this\.type, sampleRate\);[^\n]*\n[^\n]*postMessage\(\{opened:1\}\)/);
  assert.ok(html.includes('pump-synth-worklet.js?v=13') && H.read('sw.js').includes('pump-synth-worklet.js?v=13'));
});
