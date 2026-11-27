// P2-1 (code review 27 Nov): consequential paths (saves, stores, audio engine setup, worker messaging, timers) must log a failure
// through tpLog instead of swallowing it. Code is cut out of the real index.html, as in app-logic.test.js.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const html = () => H.read('index.html');
function cut(re, what) { const m = html().match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
function sandbox(extra) { const c = Object.assign({ console: { warn() {}, log() {} }, JSON, Math, Date, Set, Map, Error, String, Number, Array, Object, Promise }, extra); vm.createContext(c); return c; }

test('line-tempo / prerender timer: a throw is logged to tpLog, not swallowed', () => {
  const blk = cut(/setInterval\(\(\) => \{ try \{ if \(!kit\.voices\.some\(voiceLines\)\) return;[^\n]*\}, 100\);/, 'the lnTempo/prerender 100 ms timer');
  const logs = []; let cb; const c = sandbox({ setInterval: f => { cb = f; }, kit: { voices: [{}] }, voiceLines: () => true, lnT: { gen: 0 }, lnTempo() { this.lnT.gen++; }, prerender() { throw new Error('render boom'); }, tpLog: (w, e) => logs.push({ w, e }) });
  c.lnTempo = () => { c.lnT.gen++; };
  vm.runInContext(blk, c); assert.equal(typeof cb, 'function'); cb();
  assert.equal(logs.length, 1, 'one tpLogs entry'); assert.match(logs[0].w, /prerender/); assert.equal(logs[0].e.message, 'render boom');
});

test('persistence: asetSave (audio settings) logs a failed localStorage write', () => {
  const blk = cut(/function asetSave\(\)\{[^\n]*\n/, 'asetSave');
  const logs = []; const c = sandbox({ AUK: 'k', aset: {}, localStorage: { setItem() { throw new Error('quota'); } }, tpLog: (w, e) => logs.push({ w, e }) });
  vm.runInContext(blk + '\nthis.asetSave = asetSave;', c); c.asetSave();
  assert.equal(logs.length, 1); assert.match(logs[0].w, /settings save/); assert.equal(logs[0].e.message, 'quota');
});

test('persistence: the pending-song flush (flushKit) logs a failed save', () => {
  const blk = cut(/try \{ if \(sTimer\)\{ clearTimeout\(sTimer\); sTimer = 0; save\(\); \} \} catch\(e\)\{[^\n]*\}/, 'the pending save in flushKit');
  const logs = []; const c = sandbox({ sTimer: 5, clearTimeout() {}, save() { throw new Error('save boom'); }, tpLog: (w, e) => logs.push({ w, e }) });
  vm.runInContext(blk, c); assert.equal(logs.length, 1); assert.match(logs[0].w, /song save/);
});

/* The guard: empty catches on lines that touch saving, storage, audio or the sequencer. New ones fail this test; a justified
   one means raising the ceiling here on purpose. After the 27 Nov pass (30 sites logged) 186 remain: UI-preference writes
   (localStorage.setItem in a private window), reads, disconnect()/stop() of nodes that may be gone, and per-frame / teardown code. */
const SILENT_CEILING = 186;
test('silent catches on save / store / audio / worker / sequencer lines stay at or below the ceiling', () => {
  const re = /catch *\( *[A-Za-z_]* *\)[ ]*\{[ ]*\}/, kw = /save|store|backup|localStorage|indexedDB|kvg|smp|worker|prerender|audio|ctx|engine|seq/i;
  const n = html().split('\n').filter(l => re.test(l) && kw.test(l)).length;
  assert.ok(n <= SILENT_CEILING, `${n} empty catches on those lines (ceiling ${SILENT_CEILING}): log with tpLog('what failed', e), or leave it empty only if the failure is expected, then raise SILENT_CEILING with a reason`);
});
