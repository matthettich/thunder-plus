// varBuf's AudioBuffer cache (index.html) is capped by bytes as well as count, least recently used first, never the newest entry.
// The cache only holds buffers made from VAR.map's data: a miss makes the same buffer again, so evicting never changes sound.
'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const H = require('./helpers');

const html = H.read('index.html');
const a = html.indexOf('const VARBUF_MAX = 256'), b = html.indexOf('function varBuf(ev, ac){');
assert.ok(a > 0 && b > a, 'varBuf cache block not found');
const mk = phone => { const c = { seqHandHere: () => phone }; vm.createContext(c); vm.runInContext(html.slice(a, b) + '\nthis.trim = varBufTrim; this.cap = VARBUF_MAX_BYTES;', c); return c; };
const entry = n => ({ n, d: null, b: null });

test('byte cap: 64 MB on a phone / tablet, 256 MB on a computer', () => {
  assert.equal(mk(true).cap(), 64e6); assert.equal(mk(false).cap(), 256e6);
});
test('evicts least recently used until under the byte cap, keeps the newest', () => {
  const c = mk(true), M = new Map();
  for (let i = 0; i < 5; i++) M.set('k' + i, entry(20e6));   // 100 MB: over 64 MB
  c.trim(M);
  assert.deepEqual([...M.keys()], ['k2', 'k3', 'k4']);   // 60 MB left, oldest gone first
  const big = new Map([['a', entry(10e6)], ['b', entry(300e6)]]); c.trim(big);   // one entry over the cap on its own: it stays
  assert.deepEqual([...big.keys()], ['b']);
});
test('the count cap (256) still holds, and a computer keeps many small buffers', () => {
  const c = mk(false), M = new Map();
  for (let i = 0; i < 300; i++) M.set('k' + i, entry(1000));
  c.trim(M);
  assert.equal(M.size, 256); assert.equal(M.keys().next().value, 'k44');
});
