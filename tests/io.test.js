// NATIVE I/O HELPER (desktop app): the utility process's logic (electron/io-helper.js) against fake addons, and the
// restart-once rule main.js uses. No Electron needed; the real addon and the page side come with the io/ phases.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), path = require('path');
const H = require('./helpers');
const { createIoHelper, createRestartGate } = require(path.join(H.ROOT, 'electron/io-helper.js'));

const DEVS = [
  { id: 'a', name: 'Interface', inputs: 8, outputs: 8, rates: [44100, 48000], isDefaultIn: true, isDefaultOut: true },
  { id: 'b', name: 'Built-in', inputs: 2, outputs: 2, rates: [48000], isDefaultIn: false, isDefaultOut: false },
];
const make = addon => { const out = []; const h = createIoHelper({ loadAddon: () => { if (addon instanceof Error) throw addon; return addon; }, post: m => out.push(m) }); return { h, out }; };

test('io helper: addon loads → hello available with its version, then devices arrive with the request id', () => {
  const { h, out } = make({ version: () => '0.1.0', devices: () => DEVS });
  assert.deepEqual(h.hello, { op: 'hello', available: true, version: '0.1.0', midi: false });
  h.handle({ op: 'devices', id: 7 });
  assert.deepEqual(out, [{ op: 'devices', id: 7, devices: DEVS }]);
  assert.equal(out[0].devices.length, 2);
});

test('io helper: addon missing → hello {available:false, why}; requests answer with an error', () => {
  const { h, out } = make(new Error('Cannot find module thunder_io.node'));
  assert.deepEqual(h.hello, { op: 'hello', available: false, why: 'Cannot find module thunder_io.node' });
  h.handle({ op: 'devices', id: 1 });
  assert.equal(out[0].op, 'error'); assert.equal(out[0].id, 1);
});

test('io helper: devices() throwing → an error message with the id (and the helper keeps working)', () => {
  let fail = true;
  const { h, out } = make({ version: () => '1', devices: () => { if (fail) throw new Error('enumerate failed'); return DEVS; } });
  h.handle({ op: 'devices', id: 'x' });
  assert.deepEqual(out[0], { op: 'error', id: 'x', why: 'enumerate failed' });
  fail = false; h.handle({ op: 'devices', id: 'y' });
  assert.equal(out[1].op, 'devices'); assert.equal(out[1].id, 'y');
});

test('io helper: an unknown op (or junk) → error "unknown op" with the id', () => {
  const { h, out } = make({ version: () => '1', devices: () => [] });
  h.handle({ op: 'nope', id: 3 }); h.handle(null);
  assert.deepEqual(out[0], { op: 'error', id: 3, why: 'unknown op' });
  assert.equal(out[1].why, 'unknown op');
});

test('io helper: require() in plain Node (no parentPort) starts nothing', () => {
  assert.equal(process.parentPort, undefined);
  assert.doesNotThrow(() => require(path.join(H.ROOT, 'electron/io-helper.js')));
  assert.equal(typeof createIoHelper, 'function');
});

test('io helper: the restart rule: one restart after a death, then given up', () => {
  const g = createRestartGate();
  assert.equal(g.canStart(), true, 'first start');
  g.died(); assert.equal(g.canStart(), true, 'restarted once after the first death');
  g.died(); assert.equal(g.canStart(), false, 'second death: "helper stopped"');
});

// ---- input: open / poll / close (A1.2) ----
function makeIn(over = {}) {
  const out = [], calls = [], queue = [];
  const addon = {
    version: () => '1', devices: () => DEVS,
    openInput: (dev, o) => { calls.push(['open', dev, o]); return over.open || { ok: true, rate: o.rate, channels: 8, block: o.block, latencyFrames: 64 }; },
    read: () => { calls.push(['read']); if (over.readThrows) throw new Error(over.readThrows); return queue.length ? queue.shift() : null; },
    closeInput: () => calls.push(['close']),
    inputLost: () => !!over.lost && over.lost(),
  };
  let fn = null, cleared = 0;
  const timers = { setInterval: (f, ms) => { fn = f; timers.ms = ms; return 42; }, clearInterval: h => { if (h === 42) { fn = null; cleared++; } } };
  const h = createIoHelper({ loadAddon: () => addon, post: (m, t) => out.push(t ? { ...m, _t: t } : m), timers });
  const chunk = n => ({ frame0: n * 128, hostNs: 1e9 + n, channels: 8, frames: 128, data: new Float32Array(128 * 8), overruns: 0 });
  return { h, out, calls, queue, chunk, timers, tick: () => fn && fn(), running: () => fn !== null, cleared: () => cleared };
}
const count = (calls, k) => calls.filter(c => c[0] === k).length;

test('io helper input: open → opened with the channel count; chunks posted in order with the buffer transferred', () => {
  const t = makeIn();
  t.h.handle({ op: 'open', id: 1, dev: 'a', rate: 48000, block: 128 });
  assert.deepEqual(t.out[0], { op: 'opened', id: 1, ok: true, rate: 48000, channels: 8, block: 128, latencyFrames: 64 });
  assert.deepEqual(t.calls[0], ['open', 'a', { rate: 48000, block: 128 }]);
  assert.equal(t.timers.ms, 5); assert.ok(t.running());
  const c0 = t.chunk(0), c1 = t.chunk(1); t.queue.push(c0, c1);
  t.tick();
  const ins = t.out.filter(m => m.op === 'in');
  assert.equal(ins.length, 2);
  assert.equal(ins[0].frame0, 0); assert.equal(ins[1].frame0, 128);
  assert.equal(ins[1].hostNs, 1e9 + 1); assert.equal(ins[0].channels, 8); assert.equal(ins[0].frames, 128);
  assert.equal(ins[0].data, c0.data); assert.equal(ins[0]._t[0], c0.data.buffer); assert.equal(ins[1]._t[0], c1.data.buffer);
});

test('io helper input: at most 8 reads that return data per tick; the rest on the next tick', () => {
  const t = makeIn();
  t.h.handle({ op: 'open', id: 1, dev: 'a', rate: 48000, block: 128 });
  for (let i = 0; i < 11; i++) t.queue.push(t.chunk(i));
  const n = () => t.out.filter(m => m.op === 'in').length;
  t.tick(); assert.equal(n(), 8);
  t.tick(); assert.equal(n(), 11);
});

test('io helper input: close → closed, poll cleared, closeInput once, no more reads', () => {
  const t = makeIn();
  t.h.handle({ op: 'open', id: 1, dev: 'a', rate: 48000, block: 128 });
  t.h.handle({ op: 'close', id: 2 });
  assert.deepEqual(t.out[t.out.length - 1], { op: 'closed', id: 2 });
  assert.equal(t.running(), false); assert.equal(t.cleared(), 1);
  assert.equal(count(t.calls, 'close'), 1);
  const reads = count(t.calls, 'read'); t.tick();
  assert.equal(count(t.calls, 'read'), reads);
});

test('io helper input: open while open closes the first before opening the second', () => {
  const t = makeIn();
  t.h.handle({ op: 'open', id: 1, dev: 'a', rate: 48000, block: 128 });
  t.h.handle({ op: 'open', id: 2, dev: 'b', rate: 48000, block: 128 });
  assert.deepEqual(t.calls.filter(c => c[0] !== 'read').map(c => c[0] + (c[1] || '')), ['opena', 'close', 'openb']);
  assert.equal(t.cleared(), 1); assert.ok(t.running());
});

test('io helper input: openInput ok:false → opened with ok:false and why, no poll', () => {
  const t = makeIn({ open: { ok: false, why: 'device busy' } });
  t.h.handle({ op: 'open', id: 5, dev: 'a', rate: 48000, block: 128 });
  assert.deepEqual(t.out[0], { op: 'opened', id: 5, ok: false, why: 'device busy' });
  assert.equal(t.running(), false);
});

test('io helper input: read() throwing → error then closed, poll stopped, input closed', () => {
  const t = makeIn({ readThrows: 'device lost' });
  t.h.handle({ op: 'open', id: 1, dev: 'a', rate: 48000, block: 128 });
  t.tick();
  assert.deepEqual(t.out.slice(1), [{ op: 'error', why: 'device lost' }, { op: 'closed', why: 'device lost' }]);
  assert.equal(t.running(), false); assert.equal(count(t.calls, 'close'), 1);
});

test('io helper input: shutdown() with an input open closes it and stops the poll (and is safe when idle)', () => {
  const t = makeIn();
  t.h.handle({ op: 'open', id: 1, dev: 'a', rate: 48000, block: 128 });
  t.h.shutdown();
  assert.equal(count(t.calls, 'close'), 1); assert.equal(t.running(), false);
  t.h.shutdown(); assert.equal(count(t.calls, 'close'), 1);
});

test('io helper input: the device going away (inputLost) drains what arrived, then closes and tells the page why (A2)', () => {
  let gone = false; const t = makeIn({ lost: () => gone });
  t.h.handle({ op: 'open', id: 1, dev: 'a', rate: 48000, block: 128 });
  t.queue.push(t.chunk(0), t.chunk(1)); gone = true; t.tick();
  const ops = t.out.slice(1).map(m => m.op);
  assert.deepEqual(ops, ['in', 'in', 'closed'], 'the last chunks first, then closed');
  assert.match(t.out[t.out.length - 1].why, /went away/);
  assert.equal(t.running(), false); assert.equal(count(t.calls, 'close'), 1);
});

test('io helper wiring: chunks go to the Electron port WITHOUT a transfer list (MessagePortMain only transfers ports)', () => {
  // CodeRabbit, PR #14: an ArrayBuffer in the list threw on the first chunk and killed the helper in the real app
  const src = require('fs').readFileSync(require('path').join(__dirname, '..', 'electron', 'io-helper.js'), 'utf8');
  const wiring = src.slice(src.indexOf('if (process.parentPort)'));
  assert.match(wiring, /post: m => \{ if \(port\) port\.postMessage\(m\); \}/);
  assert.doesNotMatch(wiring, /port\.postMessage\(m, t/);
});

// ---- MIDI (phase B) ----
function makeMidi() {
  const out = [], calls = [], inQ = []; let ivs = [], next = 1;
  const timers = { setInterval: (f, ms) => { const h = { f, ms }; ivs.push(h); return h; }, clearInterval: h => { ivs = ivs.filter(x => x !== h); } };
  const addon = { version: () => '4', devices: () => [],
    midiPorts: () => ({ outs: [{ id: 'Gear', name: 'Gear' }], ins: [{ id: 'Clock', name: 'Clock' }] }),
    midiOpenOut: n => n === 'Gear' ? { ok: true, port: next++ } : { ok: false, why: 'no MIDI output called ' + n },
    midiOpenIn: n => n === 'Clock' ? { ok: true, port: next++ } : { ok: false, why: 'no MIDI input called ' + n },
    midiSend: (p, b, at) => { calls.push(['send', p, Array.from(b), at]); return true; },
    midiClear: p => { calls.push(['clear', p]); return 0; }, midiClose: p => calls.push(['close', p]),
    midiRead: () => inQ.length ? inQ.splice(0) : null };
  const h = createIoHelper({ loadAddon: () => addon, post: m => out.push(m), timers });
  return { h, out, calls, inQ, tick: () => ivs.forEach(i => i.f()), ivs: () => ivs };
}

test('io helper MIDI: hello says midi; ports; open out / in with ids; a bad name is {ok:false, why}', () => {
  const { h, out } = makeMidi();
  assert.equal(h.hello.midi, true);
  h.handle({ op: 'midiPorts', id: 1 }); assert.deepEqual(out[0], { op: 'midiPorts', id: 1, outs: [{ id: 'Gear', name: 'Gear' }], ins: [{ id: 'Clock', name: 'Clock' }] });
  h.handle({ op: 'midiOpenOut', id: 2, name: 'Gear' }); assert.deepEqual(out[1], { op: 'midiOpened', id: 2, ok: true, port: 1 });
  h.handle({ op: 'midiOpenOut', id: 3, name: 'Nope' }); assert.deepEqual(out[2], { op: 'midiOpened', id: 3, ok: false, why: 'no MIDI output called Nope' });
});

test('io helper MIDI: sends (one and many) and clear reach the addon in order, with no answer', () => {
  const { h, out, calls } = makeMidi();
  h.handle({ op: 'midiOpenOut', id: 1, name: 'Gear' }); out.length = 0;
  h.handle({ op: 'midiSend', port: 1, bytes: [0x90, 60, 100], at: 5e9 });
  h.handle({ op: 'midiSendMany', list: [[1, [0x80, 60, 0], 6e9], [1, [0xF8], 0]] });
  h.handle({ op: 'midiClear', port: 1 });
  assert.deepEqual(calls, [['send', 1, [0x90, 60, 100], 5e9], ['send', 1, [0x80, 60, 0], 6e9], ['send', 1, [0xF8], 0], ['clear', 1]]);
  assert.equal(out.length, 0, 'streams get no answer');
});

test('io helper MIDI: an open input polls; what came is posted as one midiIn batch; shutdown clears and closes every port', () => {
  const { h, out, calls, inQ, tick, ivs } = makeMidi();
  h.handle({ op: 'midiOpenOut', id: 1, name: 'Gear' }); h.handle({ op: 'midiOpenIn', id: 2, name: 'Clock' }); out.length = 0;
  assert.equal(ivs().length, 1, 'one MIDI-in poll');
  tick(); assert.equal(out.length, 0, 'nothing came: nothing posted');
  inQ.push({ port: 2, bytes: [0xF8], ns: 1e9 }, { port: 2, bytes: [0xF8], ns: 1.02e9 }); tick();
  assert.deepEqual(out, [{ op: 'midiIn', list: [{ port: 2, bytes: [0xF8], ns: 1e9 }, { port: 2, bytes: [0xF8], ns: 1.02e9 }] }]);
  calls.length = 0; h.shutdown();
  assert.deepEqual(calls, [['clear', 0], ['close', 1], ['close', 2]]); assert.equal(ivs().length, 0, 'poll stopped');
});

test('io helper MIDI: an addon without MIDI (older build) refuses the requests, ignores the streams', () => {
  const { h, out } = make({ version: () => '3', devices: () => [] });
  assert.equal(h.hello.midi, false);
  h.handle({ op: 'midiSend', port: 1, bytes: [0xF8] }); h.handle({ op: 'midiClear', port: 0 }); assert.equal(out.length, 0);
  h.handle({ op: 'midiPorts', id: 4 }); assert.equal(out[0].op, 'error'); assert.equal(out[0].id, 4);
});
