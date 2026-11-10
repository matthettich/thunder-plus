// BGPLAY GUARD (10 Nov): the BACKGROUND PLAY block is cut out of index.html and run against fakes. Android only: hidden while playing with
// Background play OFF logs one line each time; on return one toast per page load offers "Turn on" (sets it on, starts the element when playing).
// Turning it off by hand toasts; turning it off by code (a reset) does not.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/var bgOn = [\s\S]*?\/\* ===== END BACKGROUND PLAY/); assert.ok(m, 'could not find the BACKGROUND PLAY block; this test needs updating');
assert.match(m[0], /function bgGuardHide\(/, 'guard present');

function rig({ bgOn = false, android = true, playing = true } = {}) {
  const log = [], toasts = [], plain = [], L = {}, els = [], store = { v: bgOn ? '1' : '0' }, box = { checked: false };
  class FakeAudio { constructor() { this.paused = true; this.plays = 0; els.push(this); } addEventListener() {} play() { this.plays++; this.paused = false; return Promise.resolve(); } pause() { this.paused = true; } }
  const tp = { on: playing };
  const doc = { hidden: false, addEventListener: (t, f) => { (L[t] = L[t] || []).push(f); } };
  const ctx = vm.createContext({ Math, Promise, console, performance: { now: () => 5000 }, Audio: FakeAudio, URL: { createObjectURL: () => 'blob:x' }, Blob: function () {}, ArrayBuffer, DataView,
    localStorage: { getItem: () => store.v, setItem: (k, v) => { store.v = v; } }, navigator: { userAgent: android ? 'Android' : 'Linux', mediaSession: { setActionHandler() {} } },
    MediaMetadata: function () {}, S: { name: 'x' }, tp, tStop() {}, playCtx() {}, pbLog: s => log.push(s), pbState: () => 'state',
    toast: (t) => plain.push(t), toastAct: (t, label, fn) => toasts.push({ t, label, fn }), $: () => box, setTimeout() {}, document: doc });
  vm.runInContext(m[0].replace(/\/\* ===== END BACKGROUND PLAY$/, ''), ctx);
  const vis = h => { doc.hidden = h; (L.visibilitychange || []).forEach(f => f()); };
  return { ctx, tp, log, toasts, plain, els, box, store, vis };
}
const hid = R => R.log.filter(s => /^hidden while playing with Background play OFF: Android will throttle the audio · state$/.test(s));

test('hidden while playing with it off: a line each time, one toast per page load on return', () => {
  const R = rig();
  R.vis(true); assert.equal(hid(R).length, 1); assert.equal(R.toasts.length, 0);
  R.vis(false); assert.equal(R.toasts.length, 1); assert.equal(R.toasts[0].label, 'Turn on');
  assert.match(R.toasts[0].t, /^Background play is off, so playback breaks up when the screen is off or the app is in the background\.$/);
  R.vis(true); R.vis(false); assert.equal(hid(R).length, 2); assert.equal(R.toasts.length, 1);
});

test('"Turn on": sets it on, starts the element while playing, ticks the checkbox', () => {
  const R = rig(); R.vis(true); R.vis(false); R.toasts[0].fn();
  assert.equal(R.ctx.bgOn, true); assert.equal(R.store.v, '1'); assert.equal(R.box.checked, true);
  assert.equal(R.els.length, 1); assert.equal(R.els[0].plays, 1); assert.equal(R.els[0].paused, false);
});

test('nothing with it on, off Android, or not playing', () => {
  for (const o of [{ bgOn: true }, { android: false }, { playing: false }]) {
    const R = rig(o); R.vis(true); R.vis(false);
    assert.equal(hid(R).length, 0, JSON.stringify(o)); assert.equal(R.toasts.length, 0, JSON.stringify(o)); assert.equal(R.plain.length, 0);
  }
});

test('switching off by hand toasts (Android only); by code does not', () => {
  let R = rig({ bgOn: true }); R.ctx.setBgOn(false, true);
  assert.deepEqual(R.plain, ['Background play off: with the screen off, playback will break up. For testing only.']);
  R = rig({ bgOn: true }); R.ctx.setBgOn(false); assert.equal(R.plain.length, 0);
  R = rig({ bgOn: true, android: false }); R.ctx.setBgOn(false, true); assert.equal(R.plain.length, 0);
  R = rig({ bgOn: false }); R.ctx.setBgOn(false, true); assert.equal(R.plain.length, 0);   // already off: nothing new
});
