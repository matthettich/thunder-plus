// BG_RESTART (11 Nov): the BACKGROUND PLAY block is cut out of index.html and run against a fake <audio> element, Media Session and clock.
// Android pauses the element on focus loss: while the song plays and BACKGROUND PLAY is on, the app starts it again (at most 3 times per PLAY),
// but never when the pause was its own or a Media Session pause / stop (a real request: that stops the song).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/var bgOn = [\s\S]*?\/\* ===== END BACKGROUND PLAY/); assert.ok(m, 'could not find the BACKGROUND PLAY block; this test needs updating');
assert.match(m[0], /const BG_RESTART = true,/, 'BG_RESTART kill switch present and on');

function rig({ bgOn = true, android = true, refuse = false } = {}) {
  const clock = { now: 5000 }, log = [], timers = [], handlers = {}, docL = {}, els = [];
  class FakeAudio {
    constructor() { this.paused = true; this.L = {}; this.plays = 0; els.push(this); }
    addEventListener(t, f) { (this.L[t] = this.L[t] || []).push(f); }
    play() { this.plays++; if (refuse) return Promise.reject({ name: 'NotAllowedError' }); this.paused = false; return Promise.resolve(); }
    pause() { this.paused = true; }
    fire(t) { this.paused = true; (this.L[t] || []).forEach(f => f()); }   // the system paused it
  }
  const tp = { on: true }, st = { stops: 0 };
  const ctx = vm.createContext({ Math, Promise, console, performance: { now: () => clock.now }, Audio: FakeAudio, URL: { createObjectURL: () => 'blob:x' }, Blob: function () {}, ArrayBuffer, DataView,
    localStorage: { getItem: () => (bgOn ? '1' : '0'), setItem() {} }, navigator: { userAgent: android ? 'Android' : 'Linux', mediaSession: { setActionHandler: (k, f) => { handlers[k] = f; } } },
    MediaMetadata: function () {}, S: { name: 'x' }, tp, tStop: () => { st.stops++; tp.on = false; ctx.bgPlay(false); }, playCtx() {}, pbLog: s => { if (!/^media session: /.test(s)) log.push(s); }, pbState: () => 'state',
    setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; },
    document: { hidden: false, addEventListener: (t, f) => { docL[t] = f; } } });
  vm.runInContext(m[0].replace(/\/\* ===== END BACKGROUND PLAY$/, ''), ctx);
  const flush = () => new Promise(r => setImmediate(r));
  return { ctx, tp, log, timers, handlers, docL, els, clock, st, flush, el: () => els[0] };
}
const restarts = R => R.log.filter(s => /^background play restart \d/.test(s)), sysLogs = R => R.log.filter(s => /paused by the system/.test(s));

test('system pause while playing: one restart, logged', async () => {
  const R = rig(); R.ctx.bgPlay(true); assert.equal(R.el().plays, 1);
  R.el().fire('pause'); await R.flush();
  assert.equal(R.el().plays, 2); assert.equal(R.el().paused, false);
  assert.equal(sysLogs(R).length, 1); assert.match(sysLogs(R)[0], /^background play paused by the system · state$/);
  assert.equal(restarts(R).length, 1); assert.match(restarts(R)[0], /restart 1 \(at once\) ok/);
});

test('the app\'s own pause (STOP, or the setting turned off): no restart', async () => {
  const R = rig(); R.ctx.bgPlay(true); R.tp.on = false; R.ctx.bgPlay(false); R.el().fire('pause'); await R.flush();
  assert.equal(R.el().plays, 1); assert.equal(R.log.length, 0);
  // still playing but the setting is switched off: the app's pause is not undone
  const Q = rig(); Q.ctx.bgPlay(true); Q.ctx.setBgOn(false); Q.el().fire('pause'); await Q.flush();
  assert.equal(Q.el().plays, 1); assert.equal(Q.log.length, 0);
  // a pause the app made itself while the song plays (inside bgOwnPause's window)
  const O = rig(); O.ctx.bgPlay(true); O.ctx.bgOwnPause(); O.el().fire('pause'); await O.flush();
  assert.equal(O.el().plays, 1); assert.equal(O.log.length, 0);
});

test('Media Session pause / stop is a real request: the song stops and nothing restarts', async () => {
  for (const k of ['pause', 'stop']) {
    const Q = rig(); Q.ctx.bgPlay(true); Q.handlers[k](); Q.el().fire('pause'); await Q.flush();   // handler first: STOP, then the element's event
    assert.equal(Q.st.stops, 1, k); assert.equal(Q.tp.on, false); assert.equal(Q.el().plays, 1, 'no restart after ' + k); assert.equal(restarts(Q).length, 0);
    // the element's event first (a restart may be tried), then the handler: the handler's STOP pauses it again and the song is stopped
    const R = rig(); R.ctx.bgPlay(true); R.el().fire('pause'); R.handlers[k](); await R.flush();
    assert.equal(R.st.stops, 1); assert.equal(R.tp.on, false); assert.equal(R.el().paused, true, 'stopped: the element is paused again');
  }
});

test('never loops: at most 3 restarts per PLAY, then it stops trying; a new PLAY starts the count again', async () => {
  const R = rig(); R.ctx.bgPlay(true);
  for (let i = 0; i < 12; i++) { R.clock.now += 2500; R.el().fire('pause'); await R.flush(); if (R.docL.visibilitychange) R.docL.visibilitychange(); }
  assert.equal(restarts(R).length, 3); assert.equal(R.el().plays, 1 + 3);
  R.tp.on = false; R.ctx.bgPlay(false); R.tp.on = true; R.ctx.bgPlay(true); R.clock.now += 5000;
  R.el().fire('pause'); await R.flush(); assert.equal(restarts(R).length, 4, 'a new PLAY restarts again');
});

test('refused, or paused again within 2 s: one 1 s retry, then the next return to the app or touch', async () => {
  const R = rig({ refuse: true }); R.ctx.bgPlay(true); R.el().fire('pause'); await R.flush();
  assert.equal(restarts(R).length, 1); assert.match(restarts(R)[0], /refused: NotAllowedError/);
  assert.equal(R.timers.length, 1); assert.equal(R.timers[0].ms, 1000);
  R.timers[0].f(); await R.flush(); assert.equal(restarts(R).length, 2, 'the 1 s retry');
  R.docL.pointerdown(); await R.flush(); assert.equal(restarts(R).length, 3, 'the touch');
  R.docL.visibilitychange(); R.docL.pointerdown(); await R.flush(); assert.equal(restarts(R).length, 3, 'the cap holds');
  // paused again within 2 s of a successful restart: no immediate second try; the next visible / touch does it
  const Q = rig(); Q.ctx.bgPlay(true); Q.el().fire('pause'); await Q.flush(); Q.clock.now += 500; Q.el().fire('pause'); await Q.flush();
  assert.equal(restarts(Q).length, 1); Q.docL.pointerdown(); await Q.flush(); assert.equal(restarts(Q).length, 2);
});

test('Background play off, or not Android: nothing happens', async () => {
  const R = rig(); R.ctx.bgPlay(true); R.ctx.bgOn = false; R.el().fire('pause'); await R.flush();
  assert.equal(R.el().plays, 1); assert.equal(R.log.length, 0);
  const D = rig({ android: false }); D.ctx.bgPlay(true); assert.equal(D.els.length, 0, 'desktop: no element at all');
  const O = rig({ bgOn: false }); O.ctx.bgPlay(true); assert.equal(O.els.length, 0);
});
