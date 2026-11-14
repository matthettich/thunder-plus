// RELOAD OFFER (11 Nov): Reset audio engine detects the two states it cannot cure (a context that will not run; an output delay far above
// what the context reports) and offers a reload that saves first. The block is cut out of index.html and run in a vm with fakes: no browser.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const blk = cut(/const AUD_RELOAD = true[\s\S]*?\/\* end RELOAD OFFER \*\//, 'the reload offer');

function rig(o = {}) {
  const calls = [], toasts = [], errs = [], logs = [], asks = [], store = {}, clock = { now: 50000 };
  const ctx = o.ctx === null ? null : Object.assign({ state: 'running', baseLatency: 0.021, outputLatency: 0.022, currentTime: 100, getOutputTimestamp() { return { contextTime: 0, performanceTime: 0 }; } }, o.ctx || {});
  const sdb = o.sdb !== false;
  const kit = { voices: o.voices || [{ layers: [{ sample: { id: 'a', data: new Float32Array(4) } }] }] };
  const stored = new Set(o.stored || []);
  const sb = vm.createContext({
    ctx, aLive: { lat: o.lat || 'interactive', rate: 0 }, performance: { now: () => clock.now }, setTimeout: (f, ms) => { setImmediate(f); }, Promise, console,
    RCD: Object.assign({ on: false, sync: null, take: null }, o.rcd), rendering: !!o.rendering, kit, S: { name: 'song' }, KEY: 'kit', TK: 'song',
    sdbOk: sdb, smpStored: sdb ? stored : null, smpPut: s => { calls.push('put:' + s.id); if (o.putFails) return Promise.reject(new Error('x')); if (o.putHangs) return new Promise(() => {}); stored.add(s.id); return Promise.resolve(); },
    kitToJSON: n => ({ n }), songOut: S => S, storeWrite: (k, j) => { calls.push('write:' + k); if (o.writeFails === k) return false; store[k] = j; return true; },
    flushKit: () => calls.push('flushKit'), saveCur: () => calls.push('saveCur'), asetSave: () => calls.push('asetSave'), tStop: q => calls.push('tStop'),
    sessionStorage: { setItem: (k, v) => { calls.push('flag'); store[k] = v; } }, applyUpdate: () => calls.push('RELOAD'),
    pbLog: s => logs.push(s), pbState: () => 'state', toast: m => toasts.push(m), toastErr: m => errs.push(m),
    askYes: async (m, ok) => { asks.push({ m, ok }); return o.answer !== false; } });
  vm.runInContext(blk + '\nthis.api = {audStuckVerdict, audMeasure, audStuckCheck, audReloadSafe};', sb);
  return { sb, ctx, calls, toasts, errs, logs, asks, store, stored, kit, clock, api: sb.api };
}

test('verdict: a healthy engine, and one that is not running', () => {
  const R = rig();
  assert.equal(R.api.audStuckVerdict({ state: 'running', base: 0.021, out: 0.022, heard: 0.043, lat: 'interactive' }), null);
  assert.equal(R.api.audStuckVerdict({ state: 'none' }), null);
  for (const st of ['suspended', 'interrupted', 'closed']) { const v = R.api.audStuckVerdict({ state: st, base: 0, out: 0, heard: 0, lat: 'interactive' }); assert.equal(v.why, 'stuck'); assert.match(v.msg, new RegExp(st)); }
});

test('verdict: the 390 ms output delay of the Fold report, reported or only measured', () => {
  const R = rig(), v = R.api.audStuckVerdict;
  assert.equal(v({ state: 'running', base: 0.021, out: 0.391, heard: 0, lat: 'interactive' }).why, 'delay');   // the figure itself is high
  assert.equal(v({ state: 'running', base: 0.021, out: 0.022, heard: 0.39, lat: 'interactive' }).why, 'delay');   // only the timestamp shows it
  assert.equal(v({ state: 'running', base: 0.1, out: 0.1, heard: 0.45, lat: 'playback' }).why, 'delay');          // a playback hint: the gap counts
  assert.equal(v({ state: 'running', base: 0.1, out: 0.25, heard: 0.36, lat: 'playback' }), null, 'a high but honest figure on the playback hint is normal');
  assert.equal(v({ state: 'running', base: 0.021, out: 0.25, heard: 0.27, lat: 'interactive' }), null, 'under 300 ms and no gap');
  assert.match(v({ state: 'running', base: 0.021, out: 0.391, heard: 0, lat: 'interactive' }).msg, /Bluetooth/);
});

test('measure: reads the context and the output timestamp', () => {
  // the stamp says context time 100 was at the speaker 1000 ms (wall) ago, so the speaker is at 101.0 now; the context is at 101.6: 0.6 s late
  const R = rig({ ctx: { currentTime: 101.6, getOutputTimestamp: () => ({ contextTime: 100, performanceTime: 49000 }) } });
  const m = R.api.audMeasure(); assert.equal(m.state, 'running'); assert.ok(Math.abs(m.heard - 0.6) < 1e-9, String(m.heard)); assert.equal(m.lat, 'interactive');
  const Z = rig({ ctx: { currentTime: 100.5, getOutputTimestamp: () => ({ contextTime: 100, performanceTime: 49000 }) } }); assert.equal(Z.api.audMeasure().heard, 0, 'never negative');
  const S = rig({ ctx: { state: 'suspended', currentTime: 101.6, getOutputTimestamp: () => ({ contextTime: 100, performanceTime: 49000 }) } }); assert.equal(S.api.audMeasure().heard, 0, 'not read while suspended');
  assert.equal(rig({ ctx: null }).api.audMeasure().state, 'none');
});

test('check: a healthy engine is logged and nothing is asked', async () => {
  const R = rig(); const v = await R.api.audStuckCheck();
  assert.equal(v, null); assert.equal(R.asks.length, 0); assert.ok(R.logs.some(l => /reset audio engine check: fine/.test(l))); assert.ok(!R.calls.includes('RELOAD'));
});

test('check: a context that never runs offers the reload, and "Reload the app" saves then reloads', async () => {
  const R = rig({ ctx: { state: 'suspended' } }); const v = await R.api.audStuckCheck();
  assert.equal(v.why, 'stuck'); assert.equal(R.asks.length, 1); assert.equal(R.asks[0].ok, 'Reload the app'); assert.match(R.asks[0].m.body, /saved first/);
  const i = n => R.calls.indexOf(n);
  assert.ok(i('RELOAD') > 0 && i('tStop') < i('write:kit') && i('write:kit') < i('RELOAD') && i('write:song') < i('RELOAD') && i('saveCur') < i('RELOAD') && i('flag') < i('RELOAD'), R.calls.join());
});

test('check: declining leaves everything as it was (no stop, no write, no reload)', async () => {
  const R = rig({ ctx: { state: 'suspended' }, answer: false }); await R.api.audStuckCheck();
  assert.equal(R.asks.length, 1); assert.deepEqual(R.calls, []);
});

test('check: the delay case asks too; a context that starts running during the wait is fine', async () => {
  const R = rig({ ctx: { outputLatency: 0.391 }, answer: false }); assert.equal((await R.api.audStuckCheck()).why, 'delay');
  const S = rig({ ctx: { state: 'suspended' } }); let n = 0; Object.defineProperty(S.ctx, 'state', { get: () => (++n > 3 ? 'running' : 'suspended') });
  assert.equal(await S.api.audStuckCheck(), null); assert.equal(S.asks.length, 0);
});

test('check: no context (audio never started) does nothing', async () => { const R = rig({ ctx: null }); assert.equal(await R.api.audStuckCheck(), null); assert.equal(R.asks.length, 0); });

test('reload: samples not yet in IndexedDB are written first, then the song, then the reload', async () => {
  const R = rig(); assert.equal(await R.api.audReloadSafe(), true);
  assert.deepEqual(R.calls.filter(c => /^(put|write|RELOAD)/.test(c)), ['put:a', 'write:kit', 'write:song', 'RELOAD']);
  assert.ok(R.stored.has('a'));
});

test('reload: a sample that cannot be stored stops it unless the owner says reload anyway', async () => {
  const R = rig({ putFails: true, answer: false }); assert.equal(await R.api.audReloadSafe(), false);
  assert.ok(!R.calls.includes('RELOAD') && !R.calls.includes('write:kit')); assert.match(R.asks[0].m.title, /Samples not stored/);
  const Y = rig({ putFails: true, answer: true }); assert.equal(await Y.api.audReloadSafe(), true); assert.ok(Y.calls.includes('RELOAD'));
});

test('reload: a failed storage write never reloads, and says why', async () => {
  for (const k of ['kit', 'song']) { const R = rig({ writeFails: k }); assert.equal(await R.api.audReloadSafe(), false); assert.ok(!R.calls.includes('RELOAD'), k); assert.equal(R.errs.length, 1); assert.match(R.errs[0], /storage is full/); }
});

test('reload: not while recording or rendering; an unplaced take asks first', async () => {
  const A = rig({ rcd: { on: true } }); assert.equal(await A.api.audReloadSafe(), false); assert.deepEqual(A.calls, []); assert.match(A.errs[0], /recording/);
  const B = rig({ rcd: { sync: {} } }); assert.equal(await B.api.audReloadSafe(), false);
  const C = rig({ rendering: true }); assert.equal(await C.api.audReloadSafe(), false); assert.deepEqual(C.calls, []);
  const D = rig({ rcd: { take: {} }, answer: false }); assert.equal(await D.api.audReloadSafe(), false); assert.deepEqual(D.calls, []);
  const E = rig({ rcd: { take: {} }, answer: true }); assert.equal(await E.api.audReloadSafe(), true);
});

test('reload: without IndexedDB the audio goes inline in the kit; nothing to put', async () => {
  const R = rig({ sdb: false }); assert.equal(await R.api.audReloadSafe(), true);
  assert.ok(!R.calls.some(c => /^put:/.test(c))); assert.equal(R.store.kit, JSON.stringify({ n: false }));
});

test('reload: a second press while one runs is ignored', async () => {
  const R = rig({ ctx: null }); const a = R.api.audReloadSafe(), b = R.api.audReloadSafe(); const [x, y] = await Promise.all([a, b]);
  assert.equal(x, true); assert.equal(y, false); assert.equal(R.calls.filter(c => c === 'RELOAD').length, 1);
});

test('wiring: the button runs the check after the reset, quiet callers do not, and the app says the song is back', () => {
  assert.match(src, /\$\('audReset'\)\.addEventListener\('click', \(\) => \{ resetAudio\(\); audStuckCheck\(\)/);
  assert.ok(!/function resetAudio\(quiet\)\{[\s\S]{0,2500}audStuckCheck/.test(src.replace(blk, '')), 'resetAudio itself (Measure calls it quietly) does not ask');
  assert.match(src, /sessionStorage\.getItem\('thunderplus-audreload-v1'\)/);
  assert.match(blk, /applyUpdate\(\);/, 'the reload is applyUpdate (the desktop shell reloads its own window)');
});
