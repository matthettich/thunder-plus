// DIAG-1 (26 Nov): the native client says why it is off (pbLog + diag()), asks the hello again when the shell's `bridge` event arrives, and logs every PLAY decision.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const wait = ms => new Promise(r => setTimeout(r, ms));
const OKREPLY = id => ({ t: 'reply', id, v: 1, core: 'n2-live', caps: { nativeAudio: true }, sr: 48000, host: 'android', maxPadBytes: 0, maxSongBytes: 1 << 24 });

function rig(replies) {   // replies: array of functions/values, one per hello ask: 'timeout' | 'err' | 'ok'
  const handlers = {}, sent = [], pb = []; let asks = 0;
  const tc = { host: 'android-n0', send: m => sent.push(m),
    ask: m => { if (m.t !== 'hello') return Promise.resolve({ ok: true }); const k = replies[Math.min(asks++, replies.length - 1)];
      return k === 'timeout' ? Promise.reject(new Error('ask timeout')) : k === 'err' ? Promise.resolve({ t: 'reply', id: 1, err: 'live engine did not start' }) : Promise.resolve(OKREPLY(1)); },
    on: (t, fn) => { (handlers[t] = handlers[t] || []).push(fn); }, off: (t, fn) => { handlers[t] = (handlers[t] || []).filter(f => f !== fn); } };
  const emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m)));
  const sb = { Promise, Error, JSON, Object, Array, Map, Math, isFinite, setTimeout, clearTimeout, setInterval, clearInterval, Float32Array, Date, thunderCore: tc };
  sb.window = sb; vm.createContext(sb); vm.runInContext(H.read('thunder-core-client.js'), sb, { filename: 'thunder-core-client.js' });
  const t0 = Date.now(), changed = [];
  const host = { tpLog: () => {}, pbLog: s => pb.push(s), nativeChanged: () => changed.push(1), build: () => 687, prerender: () => {}, now: () => Date.now() - t0 + 1000, S: () => ({}), songVer: () => 1, aset: { core: 'native' }, toast: () => {}, vars: () => ({}), title: () => 't', lxp: () => 0, perf: () => null, blocked: () => '' };
  return { NC: sb.__thunderClient, host, pb, emit, get asks() { return asks; }, changed };
}

test('a hello that timed out is asked again when the shim says the bridge is up; the failure and the success are in the report', async () => {
  const r = rig(['timeout', 'ok']);
  assert.equal(await r.NC.attach(r.host), false);
  assert.equal(r.NC.on, false); assert.match(r.NC.reason, /ask timeout/);
  assert.match(r.NC.diag(), /^native: thunderCore yes host=android-n0 · client loaded yes · on no · reason hello failed: ask timeout/);
  assert.match(r.NC.diag(), /reply no reply/); assert.match(r.NC.why(), /ask timeout/);
  assert.ok(r.pb.some(l => /native engine: off \(hello failed: ask timeout\)/.test(l)));
  r.emit('bridge', { up: true }); await wait(20);
  assert.equal(r.NC.on, true); assert.equal(r.asks, 2);
  assert.ok(r.pb.some(l => /bridge up .* asking hello again/.test(l))); assert.ok(r.pb.some(l => /native engine: on \(android, 48000 Hz/.test(l)));
  assert.match(r.NC.diag(), /on yes .* hostName android · sr 48000 · core n2-live · hello sent .*\(2x\), reply at/);
  r.emit('bridge', { up: true }); await wait(10); assert.equal(r.asks, 2, 'already on: no new hello');
  assert.ok(r.changed.length >= 2, 'Settings is told');
});

test('one timed retry after a failed hello (3 s), at most three tries', async () => {
  const r = rig(['timeout', 'timeout', 'ok']);
  await r.NC.attach(r.host); assert.equal(r.asks, 1);
  await wait(3100); assert.equal(r.asks, 2); assert.equal(r.NC.on, false);
  await wait(3100); assert.equal(r.asks, 3); assert.equal(r.NC.on, true);
});

test('a shell that refuses the hello (engine did not start) says so', async () => {
  const r = rig(['err']); await r.NC.attach(r.host);
  assert.match(r.NC.reason, /the shell refused the hello: live engine did not start/); assert.match(r.NC.diag(), /reason the shell refused the hello/);
});

test('every PLAY decision is in the report: native, and the exact refusal when the page plays', async () => {
  const r = rig(['ok']); await r.NC.attach(r.host);
  assert.equal(r.NC.can('song', {}), false);   // no clock heartbeat yet
  assert.ok(r.pb.some(l => /old engine plays this song: the native engine is not running/.test(l)));
  assert.match(r.NC.diag(), /last refusal the native engine is not running · last PLAY page \(the native engine is not running\)/);
  r.emit('clock', { sr: 48000 }); r.host.blocked = () => 'Ableton Link needs the old engine';
  assert.equal(r.NC.can('song', {}), false); assert.ok(r.pb.some(l => /Ableton Link needs the old engine/.test(l)));
  r.host.blocked = () => ''; r.NC.detach(); // (a detached client is off; a fresh rig below covers the native verdict)
  const q = rig(['ok']); await q.NC.attach(q.host); q.emit('clock', {}); q.emit('song', {});
  q.NC.edit(); await wait(10);
  assert.equal(q.NC.can('song', {}), true, 'no verdict yet is not a refusal; start() asks'); assert.ok(q.pb.some(l => /PLAY song: the native engine takes it/.test(l))); assert.match(q.NC.diag(), /last PLAY native/);
});
