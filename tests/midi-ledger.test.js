// HIDDEN-PAGE LEDGER (10 Nov, audio review): msend / MOB / moledNote / moledCut are cut out of index.html and run against a fake MIDI port
// that records (bytes, timestamp). Hidden, notes are handed to the port ahead of time and STOP cannot recall them, so each note-on still
// ahead gets its note-off 1 ms after its own start; the visible (outbox) path is unchanged.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const blk = cut(/function msend\(bytes, t, early, p\)\{[\s\S]*?\n\}\n/, 'msend') + '\n' +
  cut(/const MOB_AHEAD = 60[\s\S]*?function moledCut\(\)\{[\s\S]*?\n  MOLED\.length = 0;\n\}\n/, 'the outbox and the ledger');

function rig(hidden) {
  const sent = [], clock = { now: 1000 };
  const port = { native: false, send(b, ts) { sent.push({ b: Array.from(b), ts: ts == null ? clock.now : ts }); } };
  const ctx = vm.createContext({ Math, Set, Map, Array, console, setInterval: () => 1, clearInterval() {}, performance: { now: () => clock.now },
    document: { hidden, addEventListener() {} }, ctx: {}, midiOut: () => port, a2p: t => t * 1000, moFxLat: () => 0, mLatOf: () => 0 });
  vm.runInContext(blk + '\nthis.api = {msend, MOB, MOLED, moledCut, mobFlush};', ctx);
  return { api: ctx.api, sent, port, clock, ctx };
}
const on = (n, ch = 0) => [0x90 | ch, n, 100], off = (n, ch = 0) => [0x80 | ch, n, 0];

test('ledger: hidden, notes booked ahead; STOP leaves every note-on with a note-off within 1 ms, and nothing sounding', () => {
  const R = rig(true), { api, sent, clock } = R;
  // the sequencer books notes 0.2 .. 1.2 s ahead, each with its own long note-off, a CC and a clock byte that are not ledgered
  const ev = [];
  for (let i = 0; i < 6; i++) { const at = 1.2 + i * 0.2; ev.push([on(60 + i, i % 2), at]); ev.push([off(60 + i, i % 2), at + 0.5]); }
  ev.push([[0xB0, 74, 10], 1.5], [[0xF8], 1.3]);
  for (const [b, t] of ev) api.msend(b, t, false, 0);
  assert.equal(sent.length, ev.length, 'hidden: everything went straight to the port');
  assert.equal(api.MOB.q.length, 0, 'hidden: the outbox is bypassed');
  assert.equal(api.MOLED.length, 6, 'only the six note-ons are ledgered');
  clock.now = 1100; sent.length = 0;
  api.moledCut();   // (what moRelease calls on STOP)
  const ons = ev.filter(e => e[0][0] >= 0x90 && e[0][0] < 0xA0).map(e => ({ n: e[0][1], ch: e[0][0] & 15, ts: e[1] * 1000 }));
  for (const o of ons) {
    const m = sent.find(x => x.b[0] === (0x80 | o.ch) && x.b[1] === o.n);
    assert.ok(m, 'a note-off for note ' + o.n); assert.ok(m.ts - o.ts > 0 && m.ts - o.ts <= 1.0001, 'within 1 ms of its note-on: ' + (m.ts - o.ts));
  }
  const lastOn = Math.max(...ons.map(o => o.ts)), anos = sent.filter(x => x.b[0] >= 0xB0 && x.b[0] < 0xC0 && x.b[1] === 123);
  assert.ok(anos.some(x => x.ts === 1100), 'All Notes Off now'); assert.ok(anos.some(x => x.ts > lastOn + 1), 'and after the last booked note');
  assert.equal(api.MOLED.length, 0, 'ledger emptied');
  // replay the port's timeline: no note is left sounding after the last timestamp
  const all = [...ev.map(e => ({ b: e[0], ts: e[1] * 1000 })), ...sent].filter(x => (x.b[0] >= 0x80 && x.b[0] < 0xA0) || x.b[1] === 123).sort((a, b) => a.ts - b.ts);
  const down = new Set();
  for (const x of all) { const k = (x.b[0] & 15) + ':' + x.b[1]; if (x.b[0] >= 0xB0) { for (const d of [...down]) if (d.startsWith((x.b[0] & 15) + ':')) down.delete(d); } else if (x.b[0] >= 0x90) down.add(k); else down.delete(k); }
  assert.equal(down.size, 0, 'nothing left sounding: ' + [...down]);
  // a note whose time has passed is not touched again
  const R2 = rig(true); R2.api.msend(on(60), 1.05, false, 0); R2.clock.now = 1100; R2.sent.length = 0; R2.api.moledCut(); assert.equal(R2.sent.length, 0, 'past notes are pruned: nothing sent');
});

test('ledger: a port with clear() has it called first; the visible path goes through the outbox and is not ledgered; the cap holds', () => {
  const R = rig(true); const calls = []; R.port.clear = () => calls.push('clear' + R.sent.length);
  R.api.msend(on(60), 1.5, false, 0); R.api.moledCut();
  assert.equal(calls.length, 1); assert.equal(calls[0], 'clear1', 'clear() ran before the note-offs');
  assert.ok(R.sent.some(x => x.b[0] === 0x80 && x.b[1] === 60), 'and the note-off still goes');
  const V = rig(false); V.api.msend(on(60), 1.5, false, 0);   // 500 ms ahead, visible: the outbox
  assert.equal(V.sent.length, 0, 'visible: held back in the outbox'); assert.equal(V.api.MOB.q.length, 1); assert.equal(V.api.MOLED.length, 0, 'not ledgered');
  V.api.msend(on(61), 1.03, false, 0); assert.equal(V.sent.length, 1, 'visible and within 60 ms: sent as before'); assert.equal(V.sent[0].ts, 1030); assert.equal(V.api.MOLED.length, 0);
  V.api.moledCut(); assert.equal(V.sent.length, 1, 'STOP with an empty ledger sends nothing extra');
  const C = rig(true); for (let i = 0; i < 400; i++) C.api.msend(on(i % 128), 1.5 + i / 1000, false, 0); assert.equal(C.api.MOLED.length, 400, 'a busy song is far under the cap: nothing dropped');
  C.sent.length = 0; C.api.moledCut(); for (let i = 0; i < 400; i++) assert.ok(C.sent.some(x => x.b[0] === 0x80 && x.b[1] === i % 128 && Math.abs(x.ts - (1500 + i + 1)) < 0.001), 'note ' + i + ' released 1 ms after its start');
  const X = rig(true); for (let i = 0; i < 5000; i++) X.api.msend(on(i % 128), 1.5 + i / 10000, false, 0); assert.ok(X.api.MOLED.length <= 4096, 'capped');
  const N = rig(true); N.port.native = true; N.api.msend(on(60), 1.5, false, 0); assert.equal(N.api.MOLED.length, 0, 'the native helper (clear()) is not ledgered');
});
