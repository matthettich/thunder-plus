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
  assert.deepEqual(h.hello, { op: 'hello', available: true, version: '0.1.0' });
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
