// (11 Nov, build 566) Auto.constBlk: the block-aware "is this timeline still?" test behind the core's fast paths.
// The rule it must keep: whenever it answers a value for a block, every sample of that block, read the slow way (at()),
// is that very value. Checked on thousands of random timelines (sets, ramps, targets, curves, with prune as the core runs it).
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'core', 'core-processor.js'), 'utf8');
const i0 = src.indexOf('class Auto {'); let d = 0, i1 = -1;
for (let i = src.indexOf('{', i0); i < src.length; i++){ const c = src[i]; if (c === '{') d++; else if (c === '}'){ if (--d === 0){ i1 = i + 1; break; } } }
const Auto = new Function(src.slice(i0, i1) + '; return Auto;')();
const rnd = (s => () => { let x = s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; })(12345);

test('constBlk exists and is used by the fast paths', () => {
  assert.equal(typeof Auto.prototype.constBlk, 'function');
  for (const re of [/A\.constBlk\(currentTime, currentTime \+ n \* dt\)/, /ov\.g\.constBlk\(currentTime/, /A\.constBlk\(currentTime, currentTime \+ N \* dt\)/, /v\.g\.constBlk\(tb0, tb1\)/]) assert.match(src, re);
});

test('constBlk: a value for a block means every sample of the block reads that value (random timelines, pruned per block)', () => {
  const sr = 48000, dt = 1 / sr, N = 128; let hits = 0, blocks = 0, hitsWithFuture = 0;
  for (let run = 0; run < 400; run++){
    const A = new Auto(rnd()); let t = 1 + rnd();
    // events booked ahead of time, as the sequencer does (0 to 0.3 s ahead), of every kind
    const book = now => { const n = 1 + (rnd() * 3 | 0); for (let j = 0; j < n; j++){ const at = now + rnd() * 0.3, k = rnd();
      if (k < 0.45) A.set(rnd() * 2 - 1, at); else if (k < 0.7) A.lin(rnd() * 2 - 1, at); else if (k < 0.9) A.target(rnd(), at, 0.001 + rnd() * 0.05);
      else { const f = 2 + rnd() * 8, a = rnd(); A.curve(x => a * Math.sin(x * f), at, 0.01 + rnd() * 0.1); } } };
    for (let b = 0; b < 160; b++){
      if (rnd() < 0.12) book(t);
      if (rnd() < 0.02) A.cancel(t + rnd() * 0.1);
      A.prune(t);
      const c = A.constBlk(t, t + N * dt); blocks++;
      if (c != null){ hits++; if (A.ev.length && A.ev[A.ev.length - 1].t > t) hitsWithFuture++;
        for (let i = 0; i < N; i++){ const v = A.at(t + i * dt); if (!Object.is(v, c)) assert.fail('run ' + run + ' block ' + b + ' sample ' + i + ': at() = ' + v + ', constBlk = ' + c + ', events ' + JSON.stringify(A.ev)); } }
      // and it never says less than the old test did: constFrom's "constant" is still constant
      const cf = A.constFrom(t); if (cf != null) assert.ok(Object.is(c, cf), 'constFrom says ' + cf + ' but constBlk says ' + c);
      t += N * dt;
    }
  }
  assert.ok(hits > blocks * 0.2, 'the fast answer is given often (' + hits + ' of ' + blocks + ')');
  assert.ok(hitsWithFuture > 100, 'and with events still booked ahead, which constFrom could not do (' + hitsWithFuture + ')');
});

test('constBlk: the edges', () => {
  const A = new Auto(0.5);
  assert.equal(A.constBlk(1, 1.01), 0.5, 'no events: the start value');
  A.set(0.8, 2); assert.equal(A.constBlk(1, 1.5), 0.5, 'a set after the block: still');
  assert.equal(A.constBlk(1.99, 2.0), null, 'a set at the block end: not still');
  assert.equal(A.constBlk(2, 2.1), 0.8, 'the set at the block start counts');
  A.lin(0.1, 3); assert.equal(A.constBlk(2, 2.1), null, 'a ramp running to a later point: not still');
  assert.equal(A.constBlk(3, 3.1), 0.1, 'after the ramp end: its value');
  A.target(0, 3.05, 0.01); assert.equal(A.constBlk(3, 3.01), 0.1, 'a target booked after the block: still');
  assert.equal(A.constBlk(3.06, 3.07), null, 'a target before the block: not still');
});
