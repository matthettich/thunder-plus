// io/test/midi.js: the addon's MIDI (phase B) on its loopback port "test:loop" (no MIDI hardware needed; CI and this workspace).
//   THUNDER_IO_TEST=1 node io/test/midi.js        exits 1 on a failure
// Checks: the test port is listed; messages booked ahead go out at their OS time (each within 2 ms, most within 0.5 ms);
// messages booked for the same moment keep their order; a past or zero time sends now; midiClear drops what is still
// queued (and only for that port); closing drops a port's queue; a bad id or an unknown port is refused, not a crash.
'use strict';
process.env.THUNDER_IO_TEST = process.env.THUNDER_IO_TEST || '1';
const path = require('path');
const I = require(path.join(__dirname, '..', '..', 'electron', 'io', 'thunder_io.node'));
const fail = m => { console.error('FAIL ' + m); process.exit(1); };
const sleep = ms => new Promise(r => setTimeout(r, ms));
const drain = () => { const a = []; for (let r; (r = I.midiRead()); ) a.push(...r); return a; };
(async () => {
  const P = I.midiPorts();
  if (!P || !Array.isArray(P.outs) || !Array.isArray(P.ins)) fail('midiPorts shape: ' + JSON.stringify(P));
  if (!P.outs.some(p => p.id === 'test:loop') || !P.ins.some(p => p.id === 'test:loop')) fail('test:loop not listed');
  console.log('ports', JSON.stringify(P));
  const o = I.midiOpenOut('test:loop'); if (!o.ok) fail('open out: ' + o.why);
  const i = I.midiOpenIn('test:loop'); if (!i.ok) fail('open in: ' + i.why);
  drain();

  // TIMING: 40 notes, 10 ms apart, booked 80 ms ahead
  const t0 = I.now() + 80e6, want = [];
  for (let k = 0; k < 40; k++) { const at = t0 + k * 10e6; want.push(at); if (!I.midiSend(o.port, [0x90, k, 100], at)) fail('send refused'); }
  await sleep(600);
  const got = drain().filter(m => m.port === i.port);
  if (got.length !== 40) fail(`timing: ${got.length} of 40 came back`);
  const err = got.map((m, k) => { if (m.bytes[1] !== k) fail('order: ' + m.bytes.join(',')); return (m.ns - want[k]) / 1e6; });
  const early = Math.min(...err), worst = Math.max(...err), med = err.slice().sort((a, b) => a - b)[20];
  console.log(`timing: early-most ${early.toFixed(3)} ms, median ${med.toFixed(3)} ms, latest ${worst.toFixed(3)} ms`);
  if (early < -0.05) fail('sent early: ' + early.toFixed(3) + ' ms');
  if (worst > 2 || med > 0.5) fail('sent late: median ' + med.toFixed(3) + ', latest ' + worst.toFixed(3) + ' ms');

  // SAME MOMENT: kept in the order booked (a note off then a note on, as a retrigger does)
  const t1 = I.now() + 30e6; for (let k = 0; k < 16; k++) I.midiSend(o.port, Uint8Array.of(0x80, k, 0), t1);
  await sleep(120); const same = drain().map(m => m.bytes[1]).join(',');
  if (same !== [...Array(16).keys()].join(',')) fail('same-moment order: ' + same);

  // NOW: 0 or a past time sends at once
  I.midiSend(o.port, [0xF8], 0); I.midiSend(o.port, [0xFA], I.now() - 50e6); await sleep(30);
  const now = drain().map(m => m.bytes[0]); if (now.join() !== [0xF8, 0xFA].join()) fail('now / past: ' + now);

  // CLEAR: what's still queued for the port is dropped; another port's queue stays
  const o2 = I.midiOpenOut('test:loop'); if (!o2.ok) fail('second out');
  const t2 = I.now() + 150e6; for (let k = 0; k < 10; k++) I.midiSend(o.port, [0x90, 60, 100], t2 + k * 1e6);
  I.midiSend(o2.port, [0xB0, 7, 99], t2);
  const st = I.midiStats(); if (st.queued !== 11) fail('queued before the clear: ' + JSON.stringify(st));
  const n = I.midiClear(o.port); if (n !== 10) fail('clear dropped ' + n + ', not 10');
  await sleep(250); const after = drain();
  if (after.length !== 1 || after[0].bytes[0] !== 0xB0) fail('after clear: ' + JSON.stringify(after));

  // CLOSE: a closed port's queue goes; sending to it is refused
  I.midiSend(o2.port, [0x90, 1, 1], I.now() + 100e6); I.midiClose(o2.port);
  if (I.midiSend(o2.port, [0x90, 1, 1], 0)) fail('send to a closed port accepted');
  await sleep(160); if (drain().length) fail('a closed port still sent');

  // BAD INPUT: refused, not a crash
  const bad = I.midiOpenOut('no such port'); if (bad.ok || !/no MIDI output/.test(bad.why)) fail('bad id: ' + JSON.stringify(bad));
  if (I.midiSend(999, [0x90, 1, 1], 0)) fail('unknown port accepted');
  if (I.midiSend(o.port, [], 0)) fail('empty bytes accepted');
  I.midiClose(o.port); I.midiClose(i.port);
  console.log('stats', JSON.stringify(I.midiStats()));
  console.log('midi ok');
  process.exit(0);
})();
