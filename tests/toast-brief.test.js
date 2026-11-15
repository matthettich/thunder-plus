// BRIEF NOTICES (14 Nov, owner: the "block x is also used on another track, so this track got its own copy" pop-up stays too long).
// The code under test is cut out of index.html as in usability.test.js; source pins keep the callers on toastBrief.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function harness(toastsOn = true) {
  const blk = src.match(/const toastQ = \[\];[\s\S]*?\nfunction toastBrief\(key, msg\)\{[\s\S]*?toastKey = key; \}\n/);
  assert.ok(blk, 'toast block moved: update this test');
  const el = { hidden: true, textContent: '', addEventListener() {} };
  let timers = [];
  const c = { Math, JSON, String, Number, Array, Object, clamp, console, $: () => el, localStorage: { getItem: () => (toastsOn ? null : '0') },
    setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; }, clearTimeout() {}, document: { body: { classList: { contains: () => false } } } };
  vm.createContext(c); vm.runInContext('var tt = 0, busNote = "";\n' + blk[0] + '\nthis.api = {toast, toastBrief, toastNext, st: () => ({q: toastQ.length, long: toastLong, quick: toastQuick})};', c);
  return { c, el, last: () => timers[timers.length - 1], timers: () => timers };
}
const M1 = 'Block 0B is also used on another track, so this track got its own copy, 0C (Settings → UNIQUE PER TRACK).';
test('toastBrief: 2.5 to 3.9 s by length, never a long toast', () => {
  const h = harness();
  h.c.api.toastBrief('uniq', 'Short note.'); assert.equal(h.last().ms, 2500);
  assert.equal(h.el.hidden, false); assert.equal(h.c.api.st().long, false);
  h.c.api.toastBrief('share', 'x'.repeat(400)); assert.equal(h.last().ms, 3900); assert.equal(h.c.api.st().long, false, '3.9 s is under the 4 s that makes a toast long');
  h.c.api.toastBrief('uniq', M1); const ms = h.last().ms; assert.ok(ms >= 2500 && ms <= 3900, 'the real message: ' + ms);
});
test('toastBrief: a run of them shows one note (the latest), restarts the time, and queues nothing', () => {
  const h = harness(); h.c.api.toastBrief('uniq', M1);
  for (let i = 0; i < 6; i++) { h.c.api.toastBrief('uniq', M1.replace('0C', '0' + i)); assert.equal(h.el.textContent.slice(-40).includes('0' + i), true); }
  const st = h.c.api.st(); assert.equal(st.q, 0); assert.equal(st.long, false); assert.equal(st.quick, null, 'nothing waits behind it');
});
test('toastBrief: does not hold back an ordinary toast, and the POP-UP MESSAGES setting turns it off', () => {
  const h = harness(); h.c.api.toastBrief('uniq', M1); h.c.api.toast('Saved'); assert.equal(h.el.textContent, 'Saved');
  const off = harness(false); off.c.api.toastBrief('uniq', M1); assert.equal(off.el.hidden, true);
});
test('source pins: the block-copy and shared-phrase notices use toastBrief, none asks for 5 to 7 s', () => {
  assert.equal((src.match(/toastBrief\('uniq'/g) || []).length, 1); assert.equal((src.match(/toastBrief\('share'/g) || []).length, 2);
  const i = src.indexOf('function mstShareWarn'), j = src.indexOf('function setCell');
  assert.doesNotMatch(src.slice(i, j), /, (5|7)000\)/); assert.doesNotMatch(src.slice(j, j + 2500), /, 5000\)/);
});
