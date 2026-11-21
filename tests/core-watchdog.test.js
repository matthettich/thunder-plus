// (11 Nov, build 567) The core's WATCHDOG, "deaf": it lets go when rows it SENT have waited 3 s of audio time for the page's
// acknowledgement, not when 3 s pass since the last acknowledgement (a song with rows more than 3 s apart sends nothing to
// acknowledge in between: review of PR #97).
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
function mk() {
  let time = 0; const posted = [], reg = {}, ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Int16Array, Uint16Array, Int8Array, ArrayBuffer, Map, Set, WeakMap, JSON, Date, Number, String, Object, Array, isFinite, parseInt, parseFloat,
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage(m) { posted.push(m); }, onmessage: null }; } }, registerProcessor: (nm, c) => { reg[nm] = c; }, sampleRate: 48000 };
  Object.defineProperty(ctx, 'currentTime', { get: () => time }); ctx.globalThis = ctx; vm.createContext(ctx); vm.runInContext(H.read('core/seq-core.js'), ctx, { filename: 'seq-core.js' });
  const proc = new reg['thunder-core']({});
  // a watched, playing core, as 'play' with watch:true leaves it (the clock check is kept quiet: its window never closes here)
  const arm = t => { time = t; proc.seq = {}; proc.watch = true; proc.ackT = t; proc.ackW = -1; proc.ckW = Date.now() + 1e9; proc.ckA = t; proc.ckBad = 0; posted.length = 0; };
  return { proc, posted, arm, at: t => { time = t; } };
}
test('watchdog: a long gap with nothing sent does not stop the song', () => {
  const C = mk(); C.arm(10);
  C.at(14); assert.equal(C.proc.watchdog(), false, '4 s since the last acknowledgement, but no rows are waiting');
  C.at(60); assert.equal(C.proc.watchdog(), false); assert.ok(C.proc.seq, 'still playing');
});
test('watchdog: rows sent and not acknowledged for over 3 s: it lets go and says so', () => {
  const C = mk(); C.arm(10);
  C.proc.ackW = 20;   // a batch went out at 20 s
  C.at(22.9); assert.equal(C.proc.watchdog(), false, 'under 3 s: waits');
  C.at(23.1); assert.equal(C.proc.watchdog(), true); assert.equal(C.proc.seq, null);
  const m = C.posted.find(x => x.op === 'self'); assert.ok(m && m.why === 'deaf');
});
test('watchdog: an acknowledgement clears the wait; the next batch starts a new one', () => {
  const C = mk(); C.arm(10); C.proc.ackW = 20; C.at(22);
  C.proc.port.onmessage({ data: { op: 'ack' } }); assert.equal(C.proc.ackW, -1);
  C.at(40); assert.equal(C.proc.watchdog(), false);
});
test('watchdog: the source marks the oldest waiting batch where rows are posted, and play starts with none', () => {
  const src = H.read('core/core-processor.js');
  assert.match(src, /if \(rows && !\(this\.ackW >= 0\)\) this\.ackW = currentTime;/);
  assert.match(src, /this\.watch = !!d\.watch; this\.ackT = currentTime; this\.ackW = -1;/);
  assert.match(src, /if \(this\.ackW >= 0 && currentTime - this\.ackW > 3\) why = 'deaf';/);
});
