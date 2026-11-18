// FREEZE's one-time spectra build (pump 24a24e1, in psyn_new: about 0.9 s, once per wasm instance) runs on the audio thread when a live
// node is made, and stops every node in the context. THE RULE (index.html, comment above psWarm): the build happens only while the
// transport is stopped AND the output is quiet; never at PLAY, never during play, never while a tail rings; until it has happened FREEZE
// pads play their render (psNode returns null).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const html = H.read('index.html');
const a = html.indexOf('const psWarm = new WeakSet()');
const hEnd = html.indexOf("\n}\n", html.indexOf('function psWarmUp(ac')) + 3;
const helperSrc = html.slice(a, hEnd);

// a world with a fake clock and timers: advance(ms) runs the timers due, in order
function world(over) {
  const made = [], posted = [], timers = []; let now = 1e6;
  class Node { constructor(ac, name, o) { this.name = name; this.o = o; this.port = { postMessage: m => posted.push(m), onmessage: null }; made.push(this); } connect() {} disconnect() {} }
  // two analysers (left, right) behind a splitter; level.L / level.R are steady levels, burst(v) a 60 ms echo that is over by the next read (a window covering the whole tick sees it)
  const level = { L: 0, R: 0, pend: [0, 0], set v(x) { level.L = level.R = x; }, burst(v, ch) { if (ch === undefined || ch === 0) level.pend[0] = v; if (ch === undefined || ch === 1) level.pend[1] = v; } };
  const ans = [], doc = { hidden: false };
  const mkAn = () => { const i = ans.length, an = { fftSize: 0, smoothingTimeConstant: 1, context: null, getFloatTimeDomainData(b) { b.fill(0); b[b.length - 1] = Math.max(i ? level.R : level.L, level.pend[i]); level.pend[i] = 0; } }; ans.push(an); return an; };
  const ls = [];
  const ac = { state: 'running', sampleRate: 48000, addEventListener: (n, f) => ls.push([n, f]), createAnalyser: () => { const an = mkAn(); an.context = ac; return an; }, createChannelSplitter: () => ({ connect() {} }), createGain: () => ({ gain: {}, connect() {}, disconnect() {} }), destination: {} };
  const c = Object.assign({ AudioWorkletNode: Node, ctx: ac, tp: { on: false }, psOK: new WeakSet([ac]), psSent: new WeakSet(), psLive: { off: false }, aset: { psLive: 'auto' },
    rt: {}, master: { connect() {} }, RCD: { on: false }, kit: { voices: [{ engine: 'pfreezel' }] }, isPsV: v => !!v, WASM_PRECOMPILE: false, tpLog() {}, soon: 0, liveWarmSoon() { c.soon++; },
    performance: { now: () => now }, document: doc, Float32Array, Math,
    setTimeout: (f, ms) => { const t = { f, at: now + (ms || 0), id: timers.length + 1 }; timers.push(t); return t.id; }, clearTimeout: id => { const t = timers.find(x => x.id === id); if (t) t.dead = true; },
    PsynCore: { liveSpec: () => ({ spec: 1 }), bytes: () => new Uint8Array(4), module: () => null, xeng: () => ({ pfreezel: { d: () => ({ a: 1 }) } }) } }, over || {});
  vm.createContext(c); vm.runInContext(helperSrc + '\nthis.__x = {psWarm, psWarming, psIsFreeze, psFreezeDefer, psWarmUp, psWarmSoon, psWarmNow, psEnsureAn, psBusy, psNeeds, get psFails(){return psFails;}, setGo(t){psGoT = t;}, psHit, psTickMs};', c);
  const advance = ms => { const end = now + ms; for (;;) { const t = timers.filter(x => !x.dead && !x.ran && x.at <= end).sort((p, q) => p.at - q.at)[0]; if (!t) break; t.ran = true; now = Math.max(now, t.at); t.f(); } now = end; };
  const skip = ms => { now += ms; };   // time passes with no timer run (a throttled tab)
  return { c, ac, made, posted, X: c.__x, level, advance, skip, doc, ls, now: () => now };
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

test('FREEZE warm-up: only when stopped, quiet for 3 s (the whole interval, not a sample of it) and 3 s after the last trigger; never at PLAY or during play, not while recording', () => {
  let w = world(); w.level.v = 0.05; w.X.psWarmSoon(1000, true);
  w.advance(10000); assert.equal(w.made.length, 0, 'a tail is ringing: it waits');
  w.level.v = 0; w.advance(2000); assert.equal(w.made.length, 0, 'quiet for less than 3 s: not yet');
  w.advance(3000); assert.equal(w.made.length, 1, 'quiet for 3 s: the throwaway is made once');
  // a sparse echo train: 60 ms bursts at -26 dBFS every 700 ms, each over before a 100 ms sample would land on it: the tick sees the peak since the last tick
  w = world(); w.X.psWarmSoon(500, true);
  for (let i = 0; i < 20; i++) { w.level.burst(0.05); w.advance(700); } assert.equal(w.made.length, 0, 'an echo train does not pass');
  w.advance(1000); assert.equal(w.made.length, 0, 'a burst restarts the 3 s'); w.advance(4000); assert.equal(w.made.length, 1, 'and when it is really over, it builds');
  // hits every 3 s: never between hits; it builds only after 3 s with no hit
  w = world(); w.X.psWarmSoon(500, true); w.advance(200);
  for (let i = 0; i < 8; i++) { w.X.psHit(); w.advance(2900); } assert.equal(w.made.length, 0, 'no build between hits 2.9 s apart');
  w.advance(1000); assert.equal(w.made.length, 1, 'after 3 s without a hit');
  // a trigger at t restarts the wait
  w = world(); w.X.psWarmSoon(500, true); w.advance(2800); w.X.psHit(); w.advance(2900); assert.equal(w.made.length, 0, 'the hit restarted it'); w.advance(1500); assert.equal(w.made.length, 1);
  // a resume (statechange, or the context found not running) is a trigger
  w = world(); w.X.psWarmSoon(500, true); w.ac.state = 'suspended'; w.advance(20000); assert.equal(w.made.length, 0, 'suspended: nothing');
  w.ac.state = 'running'; w.advance(2800); assert.equal(w.made.length, 0, 'resumed less than 3 s ago'); w.advance(1500); assert.equal(w.made.length, 1);
  w = world(); w.X.psWarmSoon(500, true); w.advance(2800); w.ls.filter(l => l[0] === 'statechange').forEach(l => l[1]());   // a state change at 2.8 s: the 3 s start again
  w.advance(2900); assert.equal(w.made.length, 0, 'not built before 3 s after the state change'); w.advance(1500); assert.equal(w.made.length, 1);
  // never while playing, starting, or recording
  w = world(); w.c.tp.on = true; w.X.psWarmSoon(500, true); w.advance(30000); assert.equal(w.made.length, 0, 'playing: never');
  w.c.tp.on = false; w.X.setGo(w.now()); w.advance(2000); assert.equal(w.made.length, 0, 'PLAY under way: never');
  w = world(); w.c.RCD.on = true; w.X.psWarmSoon(500, true); w.advance(30000); assert.equal(w.made.length, 0, 'recording: never');
  w.c.RCD.on = false; w.advance(5000); assert.equal(w.made.length, 1);
  w = world({ kit: { voices: [{ engine: 'pmonol' }, null] } }); w.X.psWarmSoon(500, true); w.advance(30000); assert.equal(w.made.length, 0, 'no FREEZE pad: no cost');
  w = world({ aset: { psLive: 'render' } }); w.X.psWarmSoon(500, true); w.advance(30000); assert.equal(w.made.length, 0, 'Settings: live engines off');
});

test('FREEZE quiet check is per channel: one loud channel (or an echo on one) is not quiet, on the tick path and at once', () => {
  for (const ch of ['L', 'R']) {
    let w = world(); w.level[ch] = 0.002; w.X.psWarmSoon(500, true);   // -54 dBFS in one channel only (a mono down-mix would read -60 or less)
    w.advance(20000); assert.equal(w.made.length, 0, ch + ' only: not quiet');
    w = world(); w.X.psEnsureAn(); w.advance(1000); w.level[ch] = 0.002; w.X.psWarmNow('load'); assert.equal(w.made.length, 0, 'at once: ' + ch + ' only is not quiet');
  }
  let w = world(); w.X.psWarmSoon(500, true); for (let i = 0; i < 12; i++) { w.level.burst(0.05, 1); w.advance(700); } assert.equal(w.made.length, 0, 'a right-only echo train does not pass');
});

test('FREEZE at once: a load / kit / engine switch builds NOW when stopped and both channels are quiet (the tap that caused it does not block); otherwise the 3 s rules', () => {
  let w = world(); w.X.psEnsureAn(); w.advance(1000); w.X.psHit();
  assert.equal(w.X.psWarmNow('load'), true); assert.equal(w.made.length, 1, 'built at once, with a trigger stamped a moment ago');
  w = world(); w.X.psEnsureAn(); w.advance(1000); w.level.v = 0.05; w.X.psWarmNow('load'); assert.equal(w.made.length, 0, 'something ringing: not at once');
  w.advance(4000); assert.equal(w.made.length, 0); w.level.v = 0; w.advance(5000); assert.equal(w.made.length, 1, 'then the 3 s rule');
  w = world(); w.X.psEnsureAn(); w.advance(1000); w.c.tp.on = true; w.X.psWarmNow('load'); w.advance(20000); assert.equal(w.made.length, 0, 'playing: never');
  w = world(); w.X.psEnsureAn(); w.advance(1000); w.ac.state = 'suspended'; assert.equal(w.X.psWarmNow('ready'), false); w.advance(5000); assert.equal(w.made.length, 0, 'not running');
  w = world(); w.X.psEnsureAn(); w.advance(1000); w.doc.hidden = true; w.X.psWarmNow('load'); assert.equal(w.made.length, 0, 'hidden tab');
  w = world({ psOK: new WeakSet() }); w.X.psEnsureAn(); w.advance(1000); assert.equal(w.X.psWarmNow('load'), false); assert.equal(w.made.length, 0, 'worklet not ready');
  w = world({ kit: { voices: [{ engine: 'pmonol' }] } }); w.X.psEnsureAn(); w.advance(1000); assert.equal(w.X.psWarmNow('load'), false); assert.equal(w.made.length, 0, 'no FREEZE pad: nothing');
  w = world(); w.X.psEnsureAn(); w.advance(1000); w.X.psWarmNow('load'); w.made[0].port.onmessage({ data: { opened: 1 } }); assert.equal(w.X.psWarmNow('kit'), false); assert.equal(w.made.length, 1, 'warm: nothing more');
  // the analysers are brand new: it looks again when they cover a window, then builds at once
  w = world(); w.X.psWarmNow('ready'); assert.equal(w.made.length, 0); w.advance(1000); assert.equal(w.made.length, 1, 'built as soon as the analysers cover a window');
});

test('FREEZE tick: a late tick (a throttled tab) or a hidden tab restarts the 3 s', () => {
  let w = world(); w.X.psWarmSoon(500, true); w.advance(2000); w.skip(1500); w.advance(0);   // the tick that was due ran 1.5 s late: the window had moved on
  w.advance(2500); assert.equal(w.made.length, 0, 'restarted at the late tick'); w.advance(1500); assert.equal(w.made.length, 1);
  w = world(); w.doc.hidden = true; w.X.psWarmSoon(500, true); w.advance(20000); assert.equal(w.made.length, 0, 'hidden: nothing');
  w.doc.hidden = false; w.advance(2000); assert.equal(w.made.length, 0); w.advance(2500); assert.equal(w.made.length, 1);
});

test('FREEZE warm-up: opened marks the scope warm and disposes the throwaway; failed does NOT (retries 3 s, then 6 s later, then stops: three attempts); no answer in 10 s is a failure', () => {
  let w = world(); w.X.psWarmSoon(500, true); w.advance(5000); assert.equal(w.made.length, 1);
  assert.equal(w.X.psWarm.has(w.ac), false, 'not warm until the node reports');
  w.made[0].port.onmessage({ data: { opened: 1 } });
  assert.equal(w.X.psWarm.has(w.ac), true); assert.ok(w.posted.some(m => m && m.dispose)); assert.equal(w.X.psNeeds(), false);
  // failed
  w = world(); w.X.psWarmSoon(500, true); w.advance(5000); w.made[0].port.onmessage({ data: { failed: 'trap' } });
  assert.equal(w.X.psWarm.has(w.ac), false, 'failed: NOT warm (the real node would fail too and be silent)'); assert.equal(w.X.psWarming.has(w.ac), false, 'warming cleared');
  assert.equal(w.X.psFails, 1); assert.equal(w.X.psFreezeDefer('pfreezel', w.X.psWarm.has(w.ac)), true, 'the render keeps playing');
  w.advance(5000); assert.equal(w.made.length, 1, 'not before 3 s (+ 3 s of quiet)'); w.advance(2500); assert.equal(w.made.length, 2, 'retry');
  w.made[1].port.onmessage({ data: { failed: 'trap' } }); w.advance(8000); assert.equal(w.made.length, 2); w.advance(2500); assert.equal(w.made.length, 3, 'then 6 s later (+ quiet)');
  w.made[2].port.onmessage({ data: { failed: 'trap' } }); w.advance(60000); assert.equal(w.made.length, 3, 'three attempts, then it stops');
  // silence for 10 s
  w = world(); w.X.psWarmSoon(500, true); w.advance(5000); assert.equal(w.made.length, 1); assert.equal(w.X.psWarming.has(w.ac), true);
  w.advance(10500); assert.equal(w.X.psWarming.has(w.ac), false, 'timeout clears psWarming'); assert.equal(w.X.psWarm.has(w.ac), false);
  w.advance(9000); assert.equal(w.made.length, 2, 'and it retries');
});

test('FREEZE rule: the pieces are wired (gate before the node, PLAY stamp, restart keeps it, no warm-up in liveWarm, kit and edit triggers, STOP)', () => {
  const ps = html.slice(html.indexOf('function psNode(t, pad, V, kind, key){'), html.indexOf('/* …WAVES LIVE: as psNode'));
  assert.ok(ps.indexOf('psFreezeDefer(kind, psWarm.has(ac))') > 0 && ps.indexOf('psFreezeDefer(') < ps.indexOf('new AudioWorkletNode'), 'gate precedes the node');
  assert.match(ps, /d\.opened\)\{ if \(psIsFreeze\(kind\)\) psWarm\.add\(ac\)/);
  assert.ok(!/function liveWarm\(max\)\{[^\n]*psWarmUp/.test(html), 'liveWarm (also run at PLAY) never builds');
  assert.match(html, /function tStart\(mode, o\)\{\n  psGoT = performance\.now\(\)/, 'tStart stamps PLAY first');
  assert.match(html, /psKeep = true; if \(lk\) lk\.restart = true; try \{ tStop\(true\); \} finally \{ psKeep = false/, 'the restart\'s own tStop keeps the stamp');
  assert.match(html, /if \(!psKeep\) psGoT = 0; psWarmSoon\(300\)/, 'STOP clears it and asks for the idle warm-up');
  assert.match(html, /function setKit\(k, noUndo\)\{[\s\S]*?psWarmNow\('kit'\); \}\n\/\/ Before replacing the kit/, 'a kit load builds at once when quiet');
  assert.match(html, /function changed\(i = cur, light\)\{\n  psWarmSoon\(1500, true\)/, 'a sound edit (an engine switch) asks');
  assert.match(html, /function liveWarmSoon\(ms\)\{ psWarmSoon\(/, 'a song change asks');
  for (const f of ['function holdNote(i, semis, vel = 1){', 'function play(i, vel = 1){', 'function padPress(i, held){', 'function onMidi(e, src){']) assert.ok(html.includes(f + '\n  psHit();'), f + ' stamps a trigger');
  assert.ok(html.includes('function preview(ii, note, vol, poly, when, hold, trk){') && /function preview\([^\n]*\n  psHit\(\);/.test(html), 'preview stamps');
  assert.match(html, /function play\(ev\)\{\n    if \(MIDI_ONLY_EV\.has\(ev\.type\)\) return;\n    psHit\(\);/, "the player's note function stamps");
  assert.match(html, /for \(const ev of \['pointerdown', 'touchstart', 'keydown'\]\) addEventListener\(ev, psHit, true\)/);
  assert.match(html, /createChannelSplitter\(2\); psAn = \[ctx\.createAnalyser\(\), ctx\.createAnalyser\(\)\]/, 'one analyser per channel');
  assert.match(html, /changed\(cur\); refreshAll\(\); try \{ if \(psIsFreeze\(x\)\) psWarmNow\('engine'\)/, 'an engine switch to FREEZE builds at once');
  assert.match(html, /setSong\(ns, 'Loaded ' \+ \(ns\.name \|\| name\)\);\n    psWarmNow\('load'\)/, 'a song load');
  assert.match(html, /slotsUI\(\); \} catch\(e\)\{\} psWarmNow\('kit'\); \}/, 'a kit load');
  assert.match(html, /psOK\.add\(ac\); setTimeout\(\(\) => psWarmNow\('ready'\), 0\)/, 'the pump worklet becoming ready');
  assert.match(html, /rt\.man\.forEach\(\(a, i\) => \{ a\.fftSize = 1024; sp\.connect\(a, i\); \}\); try \{ psEnsureAn\(\)/, 'analysers made at rtInit');
  const wk = H.read('pump/synth/pump-synth-worklet.js');
  assert.match(wk, /this\.h = X\.psyn_new\(this\.type, sampleRate\);[^\n]*\n[^\n]*postMessage\(\{opened:1\}\)/);
  assert.ok(html.includes('pump-synth-worklet.js?v=13') && H.read('sw.js').includes('pump-synth-worklet.js?v=13'));
});
