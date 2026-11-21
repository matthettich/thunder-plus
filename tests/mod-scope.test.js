// MOD scopes (9 Nov): the pure parts (window, sum, cache key) cut out of the real index.html and run in a vm, plus source guards
// that the scopes stay out of the audio path and cost nothing off the MOD screen.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what){ const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved, so this test needs updating`); return m[0]; }
function ctxOf(){
  const lfo = cut(/function lfoAt\(sh, ph, cyc\)\{[\s\S]*?\n\}\n/, 'lfoAt');
  const rates = cut(/var PLFO_MAX = 4, PLFO_RATES = [^\n]*\n/, 'PLFO_RATES');
  const pure = cut(/var MS_CYC = 2[^\n]*\n[^\n]*function modWin[^\n]*\nfunction modMix[^\n]*\nfunction modScKey[^\n]*\n/, 'the scope maths');
  const c = vm.createContext({ Math, clamp: (v, a, b) => Math.min(b, Math.max(a, v)) });
  vm.runInContext(rates + 'var PLFO_SHAPES = ["Sine","Triangle","Saw up","Saw down","Square","Random"];\n' + lfo + pure, c); return c;
}
test('mod scopes: the window is whole cycles of the slowest LFO (more for Random)', () => {
  const c = ctxOf();
  assert.equal(c.modWin([{ sh: 0, rt: 3, dp: 9 }]), 2, 'a 1/4 sine: 2 cycles of one beat');
  assert.equal(c.modWin([{ sh: 5, rt: 3, dp: 9 }]), 4, 'Random: 4 cycles so its steps show');
  assert.equal(c.modWin([{ sh: 0, rt: 0, dp: 9 }, { sh: 1, rt: 5, dp: 9 }]), 8, 'a sum: 2 cycles of the slowest (1 bar = 4 beats)');
});
test('mod scopes: the sum adds the LFOs around the middle and clamps like the sequencer', () => {
  const c = ctxOf(), A = { sh: 2, rt: 3, dp: 100 }, B = { sh: 4, rt: 3, dp: 60 };
  assert.equal(c.modMix([], 0.3), 128);
  assert.ok(Math.abs(c.modMix([A], 0.75) - (128 + 0.5 * 50)) < 1e-9, 'saw up at 3/4 of a cycle: +0.5 of depth/2');
  assert.ok(Math.abs(c.modMix([A, B], 0.25) - (128 + (-0.5) * 50 + 1 * 30)) < 1e-9, 'two LFOs add');
  assert.equal(c.modMix([{ sh: 4, rt: 3, dp: 255 }, { sh: 4, rt: 3, dp: 255 }], 0.1), 255, 'clamps at FF');
  assert.equal(c.modMix([{ sh: 4, rt: 3, dp: 255 }, { sh: 4, rt: 3, dp: 255 }], 0.6), 0, 'and at 00');
  assert.equal(c.modMix([{ sh: 5, rt: 3, dp: 80 }], 7.1), c.modMix([{ sh: 5, rt: 3, dp: 80 }], 7.9));
});
test('mod scopes: the cache key changes with shape, rate, depth, size, color and (Random only) the window', () => {
  const c = ctxOf(), a = [{ sh: 0, rt: 3, dp: 100 }], k = (it, w = 100, h = 30, d = 1, col = '#f00', bt = 0) => c.modScKey(it, w, h, d, col, bt);
  const base = k(a); assert.equal(base, k(a), 'same in, same key');
  assert.notEqual(base, k([{ sh: 1, rt: 3, dp: 100 }])); assert.notEqual(base, k([{ sh: 0, rt: 4, dp: 100 }])); assert.notEqual(base, k([{ sh: 0, rt: 3, dp: 101 }]));
  assert.notEqual(base, k(a, 101)); assert.notEqual(base, k(a, 100, 31)); assert.notEqual(base, k(a, 100, 30, 2)); assert.notEqual(base, k(a, 100, 30, 1, '#0f0'));
  assert.equal(base, k(a, 100, 30, 1, '#f00', 500), 'a sine is the same in every window');
  const r = [{ sh: 5, rt: 3, dp: 100 }]; assert.equal(k(r, 100, 30, 1, '#f00', 0), k(r, 100, 30, 1, '#f00', 3.9)); assert.notEqual(k(r, 100, 30, 1, '#f00', 0), k(r, 100, 30, 1, '#f00', 4.1), 'Random: the next window is a new drawing');
});
test('mod scopes: source guards (off MOD nothing runs, no free loop, out of the audio path)', () => {
  assert.match(src, /function modScTick\(\)\{\s*if \(tv !== 'mod' \|\| !modSc\.on \|\| !tp\.on \|\| lpOn \|\| document\.hidden/, 'the tick returns at once off MOD, stopped, LOW POWER or hidden');
  assert.match(src, /function modScSync\(rebuilt\)\{\s*if \(tv !== 'mod'/, 'sync does nothing off MOD');
  const blk = cut(/var MS_CYC = 2[\s\S]*?\n(?=function modPick)/, 'the scope block'); assert.ok(!/requestAnimationFrame|setInterval|setTimeout/.test(blk), 'it adds no loop of its own');
  const mk = cut(/function makeSeq\(S, mode, o\)\{[\s\S]*?\n\}\n/, 'makeSeq'); assert.ok(!/modSc|modMix/.test(mk), 'makeSeq does not know the scopes');
});
