'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
// SMP EDIT (5 Nov): the recorder's take edits, cut out of index.html (between ===SMP EDIT START=== and END) as they ship.
const H = require('./helpers'), html = H.read('index.html');
const A = html.indexOf('/* ===SMP EDIT START==='), B = html.indexOf('/* ===SMP EDIT END=== */');
if (A < 0 || B < A) throw new Error('index.html: the SMP EDIT block is missing');
const code = html.slice(A, B), names = [...code.matchAll(/^function (se\w+)/gm)].map(m => m[1]);
const E = new Function(code + '; return {' + names.join(', ') + '};')();

const F = (a) => new Float32Array(a);
const mono = (a, extra) => Object.assign({ data: F(a), data2: null, sr: 1000, name: 'm' }, extra);
const st = (a, b) => ({ data: F(a), data2: F(b), sr: 1000, name: 's' });
const arr = (x) => Array.from(x);
const L = [0.5, -0.25, 0.75, -1, 0.125, 0.25, -0.5, 0.0625];
const R = [-0.5, 0.25, 0.5, 0.5, -0.125, 0.75, 0.25, -0.0625];

// Every sample-returning function with sample args.
const CALLS = {
  seCrop: (s) => E.seCrop(s, 2, 6),
  seCut: (s) => E.seCut(s, 2, 6),
  seSilence: (s) => E.seSilence(s, 2, 6),
  seGain: (s) => E.seGain(s, -6, 1, 7),
  seNormalize: (s) => E.seNormalize(s, -3),
  seFadeIn: (s) => E.seFadeIn(s, 4, 'linear'),
  seFadeOut: (s) => E.seFadeOut(s, 4, 'exp'),
  seReverse: (s) => E.seReverse(s),
  seRemoveDC: (s) => E.seRemoveDC(s),
  seInvert: (s) => E.seInvert(s),
  seToMono: (s) => E.seToMono(s),
  seToStereo: (s) => E.seToStereo(s),
  seAutoTrim: (s) => E.seAutoTrim(s, -60, 1, 1),
  seShift: (s) => E.seShift(s, 2),
  seConcat: (s) => E.seConcat(s, s),
};

for (const name of Object.keys(CALLS)) {
  for (const kind of ['mono', 'stereo']) {
    test(`${name} (${kind}): pure, new arrays, keeps props, channel layout`, () => {
      const s = kind === 'mono' ? mono(L, { tag: 7 }) : st(L, R);
      s.tag = 7;
      const before = { d: arr(s.data), d2: s.data2 && arr(s.data2), sr: s.sr };
      const o = CALLS[name](s);
      assert.deepEqual(arr(s.data), before.d);
      if (s.data2) assert.deepEqual(arr(s.data2), before.d2);
      assert.equal(s.sr, before.sr);
      assert.notEqual(o, s);
      assert.notEqual(o.data, s.data);
      assert.ok(o.data instanceof Float32Array);
      assert.equal(o.sr, s.sr);
      assert.equal(o.tag, 7);
      assert.equal(o.name, s.name);
      if (name === 'seToMono') assert.equal(o.data2, null);
      else if (name === 'seToStereo') assert.ok(o.data2 instanceof Float32Array && o.data2 !== o.data);
      else if (kind === 'mono') assert.equal(o.data2, null);
      else {
        assert.ok(o.data2 instanceof Float32Array);
        assert.notEqual(o.data2, s.data2);
        assert.equal(o.data2.length, o.data.length);
      }
    });
  }
}

test('seRange: clamp, swap, empty, defaults', () => {
  assert.deepEqual(E.seRange(10), [0, 10]);
  assert.deepEqual(E.seRange(10, -5, 99), [0, 10]);
  assert.deepEqual(E.seRange(10, 7, 3), [3, 7]);
  assert.deepEqual(E.seRange(10, 4, 4), [4, 4]);
  assert.deepEqual(E.seRange(10, 2.9, 5.9), [2, 5]);
  assert.deepEqual(E.seRange(10, NaN, null), [0, 10]);
  assert.deepEqual(E.seRange(10, 50, 60), [10, 10]);
});

test('seCrop mono/stereo + clamping', () => {
  const m = E.seCrop(mono(L), 2, 5);
  assert.deepEqual(arr(m.data), L.slice(2, 5));
  const s = E.seCrop(st(L, R), 2, 5);
  assert.deepEqual(arr(s.data2), R.slice(2, 5));
  assert.deepEqual(arr(E.seCrop(mono(L), -3, 100).data), L);
  assert.deepEqual(arr(E.seCrop(mono(L), 5, 2).data), L.slice(2, 5));
  assert.equal(E.seCrop(mono(L), 4, 4).data.length, 0);
  assert.equal(E.seCrop(mono(L), 20, 30).data.length, 0);
});

test('seCut mono/stereo + clamping', () => {
  const s = E.seCut(st(L, R), 2, 5);
  assert.deepEqual(arr(s.data), [...L.slice(0, 2), ...L.slice(5)]);
  assert.deepEqual(arr(s.data2), [...R.slice(0, 2), ...R.slice(5)]);
  assert.deepEqual(arr(E.seCut(mono(L), 5, 2).data), [...L.slice(0, 2), ...L.slice(5)]);
  assert.deepEqual(arr(E.seCut(mono(L), 3, 3).data), L);
  assert.equal(E.seCut(mono(L), -9, 99).data.length, 0);
  assert.deepEqual(arr(E.seCut(mono(L), 6, 99).data), L.slice(0, 6));
});

test('seSilence', () => {
  const s = E.seSilence(st(L, R), 2, 5);
  assert.deepEqual(arr(s.data), [L[0], L[1], 0, 0, 0, L[5], L[6], L[7]]);
  assert.deepEqual(arr(s.data2), [R[0], R[1], 0, 0, 0, R[5], R[6], R[7]]);
  assert.equal(E.seSilence(mono(L)).data.every((v) => v === 0), true);
  assert.deepEqual(arr(E.seSilence(mono(L), 6, 2).data), [L[0], L[1], 0, 0, 0, 0, L[6], L[7]]);
  assert.deepEqual(arr(E.seSilence(mono(L), 3, 3).data), L);
  assert.deepEqual(arr(E.seSilence(mono(L), -4, 2).data), [0, 0, ...L.slice(2)]);
});

test('seGain', () => {
  const s = E.seGain(st([1, 1, 1, 1], [0.5, 0.5, 0.5, 0.5]), -6.0206, 1, 3);
  const g = Math.pow(10, -6.0206 / 20);
  assert.equal(s.data[0], 1);
  assert.ok(Math.abs(s.data[1] - g) < 1e-6 && Math.abs(s.data[2] - g) < 1e-6);
  assert.equal(s.data[3], 1);
  assert.ok(Math.abs(s.data2[1] - 0.5 * g) < 1e-6);
  const all = E.seGain(mono([0.5, -0.5]), 6.0205999);
  assert.ok(Math.abs(all.data[0] - 1) < 1e-6 && Math.abs(all.data[1] + 1) < 1e-6);
  assert.deepEqual(arr(E.seGain(mono([1, 1, 1]), -20, 5, 9).data), [1, 1, 1]);
  assert.ok(Math.abs(E.seGain(mono([1, 1, 1]), -20, 3, -5).data[0] - 0.1) < 1e-7); // swapped/clamped
});

test('seNormalize hits target on both channels with common gain; range; silence', () => {
  for (const db of [-0.3, -3, -12, 0]) {
    const s = E.seNormalize(st([0.1, -0.2, 0.05], [0.4, -0.1, 0.3]), db);
    const p = E.sePeak(s);
    assert.ok(Math.abs(p.peak - Math.pow(10, db / 20)) < 1e-6, 'stereo ' + db);
    // same gain both channels: ratio preserved
    assert.ok(Math.abs(s.data[1] / s.data2[0] - -0.5) < 1e-6);
    const m = E.seNormalize(mono([0.1, -0.2, 0.05]), db);
    assert.ok(Math.abs(E.sePeak(m).peak - Math.pow(10, db / 20)) < 1e-6, 'mono ' + db);
  }
  assert.ok(Math.abs(E.sePeak(E.seNormalize(mono([0.1, 0.2])) ).peakDb - -0.3) < 1e-5); // default
  // peak found in the right channel only
  const r = E.seNormalize(st([0.1, 0.1], [0.9, 0.1]), -6);
  assert.ok(Math.abs(r.data2[0] - Math.pow(10, -6 / 20)) < 1e-6);
  // range: loud outside the range is ignored for peak and left untouched
  const rg = E.seNormalize(mono([0.1, 0.5, 2, 0.1]), 0, 1, 2);
  assert.ok(Math.abs(rg.data[1] - 1) < 1e-6);
  assert.equal(rg.data[2], 2);
  assert.equal(rg.data[0], F([0.1])[0]);
  // silence untouched
  const sil = E.seNormalize(st([0, 0, 0], [0, 0, 0]), -1);
  assert.deepEqual(arr(sil.data), [0, 0, 0]);
  assert.deepEqual(arr(sil.data2), [0, 0, 0]);
  assert.notEqual(sil.data, undefined);
  const e = E.seNormalize(mono([]), -1);
  assert.equal(e.data.length, 0);
});

for (const curve of ['linear', 'equal', 'exp']) {
  test(`fades (${curve}): endpoints exact, monotone, mono+stereo`, () => {
    const n = 100, len = 300;
    const ones = () => ({ data: F(new Array(len).fill(1)), data2: F(new Array(len).fill(1)), sr: 1000 });
    const fi = E.seFadeIn(ones(), n, curve);
    for (const d of [fi.data, fi.data2]) {
      assert.equal(d[0], 0);
      assert.equal(d[n - 1], 1);
      assert.equal(d[n], 1);
      assert.equal(d[len - 1], 1);
      for (let i = 1; i < n; i++) assert.ok(d[i] >= d[i - 1]);
    }
    const fo = E.seFadeOut(ones(), n, curve);
    for (const d of [fo.data, fo.data2]) {
      assert.equal(d[len - 1], 0);
      assert.equal(d[len - n], 1);
      assert.equal(d[len - n - 1], 1);
      assert.equal(d[0], 1);
      for (let i = len - n + 1; i < len; i++) assert.ok(d[i] <= d[i - 1]);
    }
    const m = E.seFadeIn({ data: F([1, 1, 1, 1]), data2: null, sr: 1 }, 4, curve);
    assert.equal(m.data[0], 0);
    assert.equal(m.data[3], 1);
    assert.equal(m.data2, null);
    const mo = E.seFadeOut({ data: F([1, 1, 1, 1]), sr: 1 }, 4, curve);
    assert.equal(mo.data[3], 0);
    assert.equal(mo.data[0], 1);
    assert.equal(mo.data2, null);
  });
}

test('fade curve shapes, defaults, clamping, n edge cases', () => {
  const ones = (n) => ({ data: F(new Array(n).fill(1)), data2: null, sr: 1 });
  const lin = E.seFadeIn(ones(5), 5, 'linear').data;
  assert.deepEqual(arr(lin), [0, 0.25, 0.5, 0.75, 1]);
  const exp = E.seFadeIn(ones(5), 5, 'exp').data;
  assert.deepEqual(arr(exp), [0, 0.0625, 0.25, 0.5625, 1]);
  const eq = E.seFadeIn(ones(3), 3).data; // default equal-power
  assert.ok(Math.abs(eq[1] - Math.sin(Math.PI / 4)) < 1e-6);
  const fo = E.seFadeOut(ones(3), 3).data;
  assert.ok(Math.abs(fo[1] - Math.sin(Math.PI / 4)) < 1e-6);
  // equal power: in^2 + out^2 = 1 for mirrored positions
  const a = E.seFadeIn(ones(11), 11).data, b = E.seFadeOut(ones(11), 11).data;
  for (let i = 0; i < 11; i++) assert.ok(Math.abs(a[i] ** 2 + b[i] ** 2 - 1) < 1e-6);
  // clamping
  assert.equal(E.seFadeIn(ones(4), 99, 'linear').data[3], 1);
  assert.equal(E.seFadeIn(ones(4), 99, 'linear').data[0], 0);
  assert.deepEqual(arr(E.seFadeIn(ones(4), -5).data), [1, 1, 1, 1]);
  assert.deepEqual(arr(E.seFadeOut(ones(4), 0).data), [1, 1, 1, 1]);
  assert.deepEqual(arr(E.seFadeIn(ones(4), 1).data), [0, 1, 1, 1]);
  assert.deepEqual(arr(E.seFadeOut(ones(4), 1).data), [1, 1, 1, 0]);
  assert.equal(E.seFadeOut(ones(4), 99, 'linear').data[0], 1);
  assert.throws(() => E.seFadeIn(ones(4), 2, 'bogus'));
  assert.equal(E.seFadeIn(mono([]), 5).data.length, 0);
});

test('seReverse', () => {
  const s = E.seReverse(st(L, R));
  assert.deepEqual(arr(s.data), L.slice().reverse());
  assert.deepEqual(arr(s.data2), R.slice().reverse());
  assert.deepEqual(arr(E.seReverse(mono([1, 2, 3, 4, 5]), 1, 4).data), [1, 4, 3, 2, 5]);
  assert.deepEqual(arr(E.seReverse(mono([1, 2, 3, 4, 5]), 4, 1).data), [1, 4, 3, 2, 5]);
  assert.deepEqual(arr(E.seReverse(mono([1, 2, 3]), 1, 2).data), [1, 2, 3]);
  assert.deepEqual(arr(E.seReverse(mono([1, 2, 3]), -9, 99).data), [3, 2, 1]);
});

test('seRemoveDC', () => {
  const s = E.seRemoveDC(st([1.5, 2.5, 3.5], [-1, 0, -2]));
  assert.deepEqual(arr(s.data), [-1, 0, 1]);
  assert.ok(Math.abs(s.data2.reduce((a, b) => a + b, 0)) < 1e-6);
  assert.deepEqual(arr(E.seRemoveDC(mono([0.25, 0.75])).data), [-0.25, 0.25]);
  assert.equal(E.seRemoveDC(mono([])).data.length, 0);
});

test('seInvert', () => {
  const s = E.seInvert(st([1, -2, 0], [-1, 3, 0]));
  assert.deepEqual(arr(s.data), [-1, 2, 0]);
  assert.deepEqual(arr(s.data2), [1, -3, 0]);
  assert.ok(Object.is(s.data[2], 0));
  assert.deepEqual(arr(E.seInvert(mono([0.5, -0.5])).data), [-0.5, 0.5]);
});

test('seToMono / seToStereo', () => {
  const m = E.seToMono(st([1, 0, -1], [0, 1, 1]));
  assert.deepEqual(arr(m.data), [0.5, 0.5, 0]);
  assert.equal(m.data2, null);
  const mm = E.seToMono(mono([1, 2]));
  assert.deepEqual(arr(mm.data), [1, 2]);
  assert.equal(mm.data2, null);
  const s = E.seToStereo(mono([1, 2, 3]));
  assert.deepEqual(arr(s.data2), [1, 2, 3]);
  assert.notEqual(s.data, s.data2);
  const ss = E.seToStereo(st([1, 2], [3, 4]));
  assert.deepEqual(arr(ss.data2), [3, 4]);
});

test('sePeak', () => {
  assert.deepEqual(E.sePeak(mono([0.1, -0.5, 0.2])), { peak: 0.5, peakDb: 20 * Math.log10(0.5) });
  const p = E.sePeak(st([0.1, 0.2], [0.1, -0.8]));
  assert.ok(Math.abs(p.peak - 0.8) < 1e-6);
  assert.equal(E.sePeak(mono([0, 0])).peakDb, -Infinity);
  assert.equal(E.sePeak(mono([0, 0])).peak, 0);
  assert.equal(E.sePeak(mono([])).peakDb, -Infinity);
  assert.ok(Math.abs(E.sePeak(mono([1])).peakDb) < 1e-12);
});

test('seFindTrim: silence, head/tail, stereo, padding, clamping', () => {
  const sr = 1000;
  assert.deepEqual(E.seFindTrim({ data: F(new Array(100).fill(0)), data2: null, sr }), { a: 0, b: 0 });
  assert.deepEqual(E.seFindTrim({ data: F(new Array(100).fill(0)), data2: F(new Array(100).fill(0)), sr }), { a: 0, b: 0 });
  assert.deepEqual(E.seFindTrim(mono([])), { a: 0, b: 0 });
  const d = new Array(100).fill(0); d[30] = 0.5; d[60] = -0.5;
  const s = { data: F(d), data2: null, sr };
  assert.deepEqual(E.seFindTrim(s, -50, 0), { a: 30, b: 61 });
  assert.deepEqual(E.seFindTrim(s, -50, 5), { a: 25, b: 66 }); // 5 ms @1k = 5 frames
  assert.deepEqual(E.seFindTrim(s), { a: 25, b: 66 });
  assert.deepEqual(E.seFindTrim(s, -50, 1000), { a: 0, b: 100 }); // clamped
  // below-threshold content ignored; above counts
  const q = new Array(100).fill(0); q[10] = 0.001; q[50] = 0.1; // 0.001 = -60 dB
  assert.deepEqual(E.seFindTrim({ data: F(q), data2: null, sr }, -50, 0), { a: 50, b: 51 });
  assert.deepEqual(E.seFindTrim({ data: F(q), data2: null, sr }, -70, 0), { a: 10, b: 51 });
  // either channel counts (right channel only has the tail)
  const r = new Array(100).fill(0); r[80] = 0.9;
  assert.deepEqual(E.seFindTrim({ data: F(d), data2: F(r), sr }, -50, 0), { a: 30, b: 81 });
  // single loud frame at the edges
  const e = new Array(10).fill(0); e[0] = 1; e[9] = 1;
  assert.deepEqual(E.seFindTrim({ data: F(e), data2: null, sr }, -50, 0), { a: 0, b: 10 });
});

test('seAutoTrim: crops, linear fades, short result, silence', () => {
  const sr = 1000, d = new Array(200).fill(0);
  for (let i = 50; i < 150; i++) d[i] = 1;
  for (const s of [{ data: F(d), data2: null, sr }, { data: F(d), data2: F(d), sr }]) {
    const o = E.seAutoTrim(s, -50, 0, 3);
    assert.equal(o.data.length, 100);
    assert.equal(o.data[0], 0);
    assert.ok(Math.abs(o.data[1] - 0.5) < 1e-6);
    assert.equal(o.data[2], 1);
    assert.equal(o.data[99], 0);
    assert.equal(o.data[50], 1);
    if (s.data2) assert.deepEqual(arr(o.data2), arr(o.data)); else assert.equal(o.data2, null);
  }
  // result shorter than 2*fade: no fades
  const t = new Array(50).fill(0); t[20] = 1; t[21] = 1;
  const o = E.seAutoTrim({ data: F(t), data2: null, sr }, -50, 0, 3);
  assert.deepEqual(arr(o.data), [1, 1]);
  // padding included
  assert.equal(E.seAutoTrim({ data: F(d), data2: null, sr }, -50, 10, 0).data.length, 120);
  // silence -> empty
  const z = E.seAutoTrim({ data: F(new Array(10).fill(0)), data2: null, sr });
  assert.equal(z.data.length, 0);
  assert.equal(z.data2, null);
});

test('seZeroCross: directions, not found, edges', () => {
  const s = mono([1, 1, 1, -1, -1, -1, -1, 1, 1, 1]); // crossings at k=3 and k=7
  assert.equal(E.seZeroCross(s, 5, -1), 3);
  assert.equal(E.seZeroCross(s, 5, 1), 7);
  assert.equal(E.seZeroCross(s, 5, 0), 3); // tie (2 vs 2) -> earlier
  assert.equal(E.seZeroCross(s, 6, 0), 7);
  assert.equal(E.seZeroCross(s, 3, 0), 3);
  assert.equal(E.seZeroCross(s, 8, 1, 100), 8); // none ahead
  assert.equal(E.seZeroCross(s, 8, -1), 7);
  assert.equal(E.seZeroCross(s, 2, -1), 2); // none behind -> i
  assert.equal(E.seZeroCross(s, 5, 1, 1), 5); // maxFrames limit
  assert.equal(E.seZeroCross(s, 5, 0, 1), 5);
  assert.equal(E.seZeroCross(s, 5, 0, 2), 3);
  // exact zero counts
  assert.equal(E.seZeroCross(mono([1, 2, 0, 3, 4]), 4, -1), 2);
  // never found
  assert.equal(E.seZeroCross(mono([1, 1, 1, 1]), 2), 2);
  assert.equal(E.seZeroCross(mono([]), 3), 3);
  // out-of-range i is clamped for the search
  assert.equal(E.seZeroCross(s, 100, -1), 7);
  // uses the LEFT channel only
  const stereo = st([1, 1, 1, 1], [1, -1, 1, -1]);
  assert.equal(E.seZeroCross(stereo, 2), 2);
  // stereo sample works; ignores data2 even if zero-crossing
  assert.equal(E.seZeroCross(st([1, -1, 1], [1, 1, 1]), 2, 0), 2);
});

test('seShift: both signs keep length, stereo/mono, clamping', () => {
  const s = mono([1, 2, 3, 4, 5]);
  assert.deepEqual(arr(E.seShift(s, 2).data), [3, 4, 5, 0, 0]);
  assert.deepEqual(arr(E.seShift(s, -2).data), [0, 0, 1, 2, 3]);
  assert.deepEqual(arr(E.seShift(s, 0).data), [1, 2, 3, 4, 5]);
  assert.deepEqual(arr(E.seShift(s, 5).data), [0, 0, 0, 0, 0]);
  assert.deepEqual(arr(E.seShift(s, 99).data), [0, 0, 0, 0, 0]);
  assert.deepEqual(arr(E.seShift(s, -99).data), [0, 0, 0, 0, 0]);
  assert.deepEqual(arr(E.seShift(s, 4).data), [5, 0, 0, 0, 0]);
  assert.deepEqual(arr(E.seShift(s, -4).data), [0, 0, 0, 0, 1]);
  const t = E.seShift(st([1, 2, 3], [4, 5, 6]), 1);
  assert.deepEqual(arr(t.data), [2, 3, 0]);
  assert.deepEqual(arr(t.data2), [5, 6, 0]);
  const u = E.seShift(st([1, 2, 3], [4, 5, 6]), -1);
  assert.deepEqual(arr(u.data2), [0, 4, 5]);
  assert.equal(E.seShift(s, 2).data.length, 5);
  assert.equal(E.seShift(s, -2).data2, null);
  assert.equal(E.seShift(mono([]), 3).data.length, 0);
});

test('seConcat: mono, stereo, promotion, sr mismatch', () => {
  const m = E.seConcat(mono([1, 2]), mono([3]));
  assert.deepEqual(arr(m.data), [1, 2, 3]);
  assert.equal(m.data2, null);
  const s = E.seConcat(st([1, 2], [5, 6]), st([3], [7]));
  assert.deepEqual(arr(s.data), [1, 2, 3]);
  assert.deepEqual(arr(s.data2), [5, 6, 7]);
  const p1 = E.seConcat(mono([1, 2]), st([3], [7]));
  assert.deepEqual(arr(p1.data), [1, 2, 3]);
  assert.deepEqual(arr(p1.data2), [1, 2, 7]);
  const p2 = E.seConcat(st([3], [7]), mono([1, 2]));
  assert.deepEqual(arr(p2.data2), [7, 1, 2]);
  assert.throws(() => E.seConcat(mono([1]), Object.assign(mono([1]), { sr: 44100 })), Error);
  assert.equal(E.seConcat(mono([]), mono([])).data.length, 0);
});

test('data2 stays null for mono through a chain of every function', () => {
  let s = mono(L);
  for (const f of Object.values(CALLS)) {
    if (f === CALLS.seToStereo) continue;
    s = f(s);
    assert.equal(s.data2, null);
  }
  // undefined data2 input also yields null
  const u = { data: F([1, 2, 3]), sr: 10 };
  assert.equal(E.seGain(u, 0).data2, null);
  assert.equal(E.seCut(u, 0, 1).data2, null);
  assert.equal(E.seConcat(u, u).data2, null);
});

test('performance: 2-min stereo 48k normalize + fadeIn + autoTrim < 2 s', () => {
  const N = 48000 * 120;
  const l = new Float32Array(N), r = new Float32Array(N);
  for (let i = 48000; i < N - 48000; i++) { l[i] = 0.4 * Math.sin(i * 0.05); r[i] = 0.3 * Math.sin(i * 0.031); }
  const s = { data: l, data2: r, sr: 48000 };
  const t0 = process.hrtime.bigint();
  const n = E.seNormalize(s, -1);
  const f = E.seFadeIn(n, 48000);
  const a = E.seAutoTrim(f, -50, 5, 3);
  const ms = Number(process.hrtime.bigint() - t0) / 1e6;
  assert.ok(ms < 2000, 'took ' + ms + ' ms');
  assert.ok(a.data.length < N && a.data.length > N - 2 * 48000 - 1);
  assert.ok(Math.abs(E.sePeak(n).peakDb - -1) < 1e-4);
});
