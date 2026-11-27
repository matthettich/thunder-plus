// P2-4: the 100 ms line-tempo/prerender timer and the 50 ms tempo-settle stamp must not run their bodies while the page is
// hidden and nothing is playing, but must keep running when visible, or hidden while playing (background play).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');

function timer(re, what) {
  const m = src.match(re); assert.ok(m, 'could not find ' + what + '; the code moved, update this test');
  return m[0];
}
/* Run the one `setInterval(...)` statement in a sandbox whose setInterval captures the callback. */
function load(stmt, env) {
  let cb = null, ms = 0;
  const c = Object.assign({ console, setInterval: (f, n) => { cb = f; ms = n; }, tpLog() {}, performance: { now: () => 1000 } }, env);
  vm.createContext(c); vm.runInContext(stmt, c);
  assert.equal(typeof cb, 'function'); return { c, cb, ms };
}
const states = [['visible, stopped', false, false, true], ['visible, playing', false, true, true], ['hidden, playing', true, true, true], ['hidden, stopped', true, false, false]];

test('line tempo / prerender timer (100 ms): skipped when hidden and stopped, runs when visible or playing hidden', () => {
  const stmt = timer(/setInterval\(\(\) => \{ try \{ if \([^\n]*voiceLines\)\)[^\n]*\}, 100\);/, 'the 100 ms lnTempo/prerender timer');
  for (const [name, hidden, on, runs] of states) {
    let ran = 0;
    const { cb, ms } = load(stmt, { document: { hidden }, tp: { on }, kit: { voices: [1] }, voiceLines: () => true, lnT: { gen: 0 }, lnTempo() { ran++; }, prerender() {} });
    assert.equal(ms, 100); cb(); assert.equal(ran, runs ? 1 : 0, name);
  }
});

test('tempo-settle stamp timer (50 ms): skipped when hidden and stopped, runs when visible or playing hidden', () => {
  const stmt = timer(/setInterval\(\(\) => \{ try \{ if \([^\n]*stq\.bpm && S === stq\.S\)[^\n]*\}, 50\);/, 'the 50 ms stqSee timer');
  const S = {};
  for (const [name, hidden, on, runs] of states) {
    let ran = 0;
    const { cb, ms } = load(stmt, { document: { hidden }, tp: { on }, stq: { bpm: 120, S }, S, stqSee() { ran++; } });
    assert.equal(ms, 50); cb(); assert.equal(ran, runs ? 1 : 0, name);
  }
});
