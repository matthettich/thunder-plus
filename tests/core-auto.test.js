// The core's Auto timelines (core/seq-core.js, from core/core-processor.js): at() runs per sample and prune() per block on the
// audio thread. They once allocated a closure (and an object per set-target event) on every call, about 100-180 bytes a call and
// tens of KB a block on a busy song, which fed the scavenger. They now keep a set-target in scalars. This pins both things:
// the source has no closure or per-event object, and the values are exactly (Object.is) what the closure version gave.
const { test } = require('node:test');
const assert = require('node:assert');
const vm = require('node:vm');
const H = require('./helpers');

const src = H.read('core/seq-core.js');
function classText(name) {
  const a = src.indexOf('class ' + name + ' {'); assert.ok(a >= 0, 'class ' + name + ' not found');
  let d = 0, i = a, started = false;
  for (; i < src.length; i++) { const c = src[i]; if (c === '{') { d++; started = true; } else if (c === '}') { d--; if (started && d === 0) break; } }
  return src.slice(a, i + 1);
}
const AutoSrc = classText('Auto');
const Auto = vm.runInNewContext('(' + AutoSrc + ')', { Math });

/* The previous implementation, verbatim (closure version), as the reference. */
class RefAuto extends Auto {
  prune(t) {
    const A = this.ev; if (!A.length || A[0].t > t) return;
    let v = this.v0, st = null, cur = null, i = 0, pt = 0;
    const val = x => st ? st.to + (st.from - st.to) * Math.exp(-(x - st.t) / st.tc) : v;
    for (; i < A.length; i++) {
      const e = A[i];
      if (e.k === 1) { if (e.t > t) break; v = e.v; st = null; pt = e.t; continue; }
      if (e.t > t) break;
      if (e.k === 0) { v = e.v; st = null; cur = null; pt = e.t; }
      else if (e.k === 2) { st = { from: val(e.t), t: e.t, to: e.v, tc: e.tc }; cur = null; }
      else if (e.k === 3) { if (e.t + e.d > t) { cur = e; } else { v = e.fn(e.t + e.d); st = null; cur = null; pt = e.t + e.d; } }
    }
    if (i === 0) return;
    if (!st && !cur && i < A.length && A[i].k === 1) { this.v0 = v; this.ev = [{ k: 0, t: pt, v }].concat(A.slice(i)); return; }
    const now = cur ? cur.fn(t) : val(t), head = [{ k: 0, t, v: now }];
    if (st) head.push({ k: 2, t, v: st.to, tc: st.tc });
    if (cur) head.push(cur);
    this.v0 = now; this.ev = head.concat(A.slice(i));
  }
  at(t) {
    let v = this.v0, pt = 0, st = null;
    const val = x => st ? st.to + (st.from - st.to) * Math.exp(-(x - st.t) / st.tc) : v;
    for (const e of this.ev) {
      if (e.k === 1) { if (t < e.t) { const a = val(pt); return e.t > pt ? a + (e.v - a) * (t - pt) / (e.t - pt) : e.v; } v = e.v; st = null; pt = e.t; continue; }
      if (t < e.t) return val(t);
      if (e.k === 0) { v = e.v; st = null; pt = e.t; }
      else if (e.k === 2) { st = { from: val(e.t), t: e.t, to: e.v, tc: e.tc }; pt = e.t; }
      else if (e.k === 3) { if (t < e.t + e.d) return e.fn(t); v = e.fn(e.t + e.d); st = null; pt = e.t + e.d; }
    }
    return val(t);
  }
}

function methodText(name) {
  const a = AutoSrc.indexOf('\n  ' + name + '(t){'); assert.ok(a >= 0, 'Auto.' + name + ' not found');
  const b = AutoSrc.indexOf('\n  }\n', a); return AutoSrc.slice(a, b);
}
test('Auto.at() and prune() allocate nothing per call: no closure, no per-event object', () => {
  for (const m of ['at', 'prune']) {
    const body = methodText(m).replace(/\/\/.*$/gm, '');
    assert.ok(!/=>/.test(body), 'Auto.' + m + ' makes an arrow function (a closure per call)');
    assert.ok(!/\bst\s*=\s*\{/.test(body), 'Auto.' + m + ' makes a target object per event');
  }
});

test('Auto.at() and prune() give exactly the closure version\'s values on random timelines (every event kind)', () => {
  let seed = 12345; const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; };
  const fns = [x => Math.sin(x * 7), x => x * x, x => 1 - Math.exp(-x * 3)];
  let N = 0, bad = 0, pruneBad = 0;
  for (let r = 0; r < 3000; r++) {
    const v0 = rnd() * 2 - 1, A = new Auto(v0), B = new RefAuto(v0), n = Math.floor(rnd() * 10); let tt = 0;
    for (let k = 0; k < n; k++) {
      // equal times and out-of-order inserts too: ins() orders them, and both versions read the same list
      tt += rnd() < .2 ? 0 : rnd() * .2; const kind = Math.floor(rnd() * 4), v = rnd() * 2 - 1, tc = rnd() * .05, d = rnd() * .1, fn = fns[k % 3];
      for (const X of [A, B]) { if (kind === 0) X.set(v, tt); else if (kind === 1) X.lin(v, tt); else if (kind === 2) X.target(v, tt, tc); else X.curve(fn, tt, d); }
    }
    for (let q = 0; q < 100; q++) { const t = rnd() * (tt + .3); N++; if (!Object.is(A.at(t), B.at(t))) bad++; }
    // prune at a few times, sampling after each: the folded list must read the same too
    for (let q = 0; q < 4; q++) {
      const t = rnd() * (tt + .2); A.prune(t); B.prune(t);
      if (!Object.is(A.v0, B.v0) || A.ev.length !== B.ev.length) pruneBad++;
      for (let s = 0; s < 25; s++) { const u = t + rnd() * .3; N++; if (!Object.is(A.at(u), B.at(u))) bad++; }
    }
  }
  assert.ok(N >= 600000, 'compared ' + N);
  assert.strictEqual(bad, 0, bad + ' at() values differ');
  assert.strictEqual(pruneBad, 0, pruneBad + ' prune() results differ');
});
