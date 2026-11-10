// Engine targets (docs/mod/ENGINE-TARGETS-DESIGN.md sections 2-4): the live bass / vox / acid / mach processors take twelve k-rate
// params (cCut cRes cDec cAtk cRel, oCut oRes oEnv oAtk oDec oSus oRel). With all at their defaults the output must equal the
// processor of build 554 (commit 7307828, read with `git show`) bit for bit; modulated, the engine's own filter / envelope moves and
// the stored parameter object never changes.
'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const H = require('./helpers');

const BASE = '7307828';
let old = null;
try { old = execFileSync('git', ['show', BASE + ':index.html'], { cwd: path.resolve(__dirname, '..'), encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { old = null; }
const SR = 48000;

function classText(html, name) {
  const a = html.indexOf('\nclass ' + name + ' {') + 1; assert.ok(a > 0, name);
  let d = 0, i = a, started = false;
  for (; i < html.length; i++) { const c = html[i]; if (c === '{') { d++; started = true; } else if (c === '}') { d--; if (started && d === 0) break; } }
  return html.slice(a, i + 1);
}
const CORES = ['BassCore', 'VoxCore', 'AcidCore', 'MachCore'];
function load(html) {
  const reg = {};
  class AWP { constructor() { this.port = { postMessage() {}, onmessage: null }; } }
  const ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Object, Array, Map, Set, JSON, Date, Number, String, RegExp, isFinite, parseFloat, parseInt,
    AudioWorkletProcessor: AWP, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: SR, currentTime: 0 };
  ctx.globalThis = ctx; vm.createContext(ctx);
  for (const n of CORES) vm.runInContext(classText(html, n), ctx);
  vm.runInContext('this.cores = {BassCore, VoxCore, AcidCore, MachCore};', ctx);
  const x0 = html.indexOf('const XPROC = '), x1 = html.indexOf('\nlet bassURL', x0);
  vm.runInContext(html.slice(x0, x1) + '\nthis.XPROC = XPROC;', ctx);
  const srcs = {};
  for (const [name, end] of [['BASS_PROC', "registerProcessor('thunder-bass', BassProc);`;"], ['VOX_PROC', "registerProcessor('thunder-vox', VoxProc);`;"]]) {
    const a = html.indexOf('const ' + name + ' = `'), b = html.indexOf(end, a) + end.length; assert.ok(a > 0 && b > a, name);
    vm.runInContext(html.slice(a, b) + '\nthis.' + name + ' = ' + name + ';', ctx);
    srcs[name] = ctx[name];
  }
  const mk = (src) => vm.runInContext('(function(AudioWorkletProcessor, registerProcessor, BassCore, VoxCore, AcidCore, MachCore, sampleRate){' + src + '})', ctx)(AWP, ctx.registerProcessor, ctx.cores.BassCore, ctx.cores.VoxCore, ctx.cores.AcidCore, ctx.cores.MachCore, SR);
  mk(srcs.BASS_PROC); mk(srcs.VOX_PROC); mk(ctx.XPROC('acid', 'AcidCore')); mk(ctx.XPROC('mach', 'MachCore'));
  return { reg, ctx };
}
const ENG = { bass: 'thunder-bass', vox: 'thunder-vox', acid: 'thunder-acid', mach: 'thunder-mach' };

// One held note: `on` quanta gate high, then `off` quanta of release; `mod(b)` returns {name: value} for quantum b.
function run(rig, eng, { params = {}, on = 200, off = 0, mod = () => ({}), keep } = {}) {
  const Cls = rig.reg[ENG[eng]], p = new Cls({ processorOptions: { p: params } }), out = [], total = on + off;
  for (let b = 0; b < total; b++) {
    const P = { rate: new Float32Array([1]), gate: new Float32Array([b < on ? 1 : 0]), trig: new Float32Array([1]), leg: new Float32Array([0]) };
    if (eng === 'bass') { P.bpm = new Float32Array([120]); P.wob = new Float32Array([0]); }
    if (eng === 'vox') { P.syl = new Float32Array([0]); P.same = new Float32Array([0]); }
    if (eng === 'acid' || eng === 'mach') P.acc = new Float32Array([0]);
    const m = mod(b);
    for (const k in m) P[k] = new Float32Array([m[k]]);
    const o = [[new Float32Array(128), new Float32Array(128)]];
    p.process([], o, P); out.push(o[0][0]);
  }
  if (keep) keep.proc = p;
  const all = new Float32Array(total * 128); out.forEach((q, i) => all.set(q, i * 128));
  return all;
}
const rms = (a, s, e) => { let x = 0; for (let i = s; i < e; i++) x += a[i] * a[i]; return Math.sqrt(x / Math.max(1, e - s)); };
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0; for (let k = 0; k < len / 2; k++) {
      const ur = re[i + k], ui = im[i + k], vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
      re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } }
  }
}
// energy between f0 and f1 Hz of the 8192 samples starting at s (Hann windowed)
function band(a, s, f0, f1) {
  const N = 8192, re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) re[i] = (a[s + i] || 0) * (0.5 - 0.5 * Math.cos(2 * Math.PI * i / N));
  fft(re, im); let e = 0;
  for (let k = Math.ceil(f0 * N / SR); k <= Math.floor(f1 * N / SR) && k < N / 2; k++) e += re[k] * re[k] + im[k] * im[k];
  return e;
}
const hf = (a, s = 128 * 100) => band(a, s, 2000, 20000);
const allFinite = a => { for (let i = 0; i < a.length; i++) if (!Number.isFinite(a[i])) return false; return true; };

const skip = !old && 'git history for ' + BASE + ' not available';
const NEW = load(H.read('index.html')), OLD = old ? load(old) : null;
const ENGS = ['bass', 'vox', 'acid', 'mach'];
const have = (eng, ...ks) => ks.every(k => ({ bass: 'cut res env atk dec sus rel', vox: 'cut atk rel', acid: 'cut res env dec sus rel', mach: 'cut res env atk dec sus rel' })[eng].split(' ').includes(k));
const cap = k => 'o' + k[0].toUpperCase() + k.slice(1), cap2 = k => 'c' + k[0].toUpperCase() + k.slice(1);

for (const eng of ENGS) {
  test(eng + ': defaults take today\'s path (bit-identical to build 554)', { skip }, () => {
    const mod = b => ({ cCut: -1, cRes: -1, cDec: -1, cAtk: -1, cRel: -1, oCut: 0, oRes: 0, oEnv: 0, oAtk: 0, oDec: 0, oSus: 0, oRel: 0 });
    const A = run(OLD, eng, { on: 150, off: 50 }), B = run(NEW, eng, { on: 150, off: 50 }), C = run(NEW, eng, { on: 150, off: 50, mod });
    assert.ok(rms(A, 0, A.length) > 0.001, 'sounds');
    for (let i = 0; i < A.length; i++) { assert.ok(Object.is(A[i], B[i]), 'omitted params, sample ' + i); assert.ok(Object.is(A[i], C[i]), 'explicit defaults, sample ' + i); }
  });

  test(eng + ': CUT command lowers, MOD offset raises the cutoff', () => {
    const none = hf(run(NEW, eng)), low = hf(run(NEW, eng, { mod: () => ({ cCut: 0x40 }) }));
    assert.ok(low < none * 0.8, `cCut 0x40 should lower HF (${low} vs ${none})`);
    const dn = hf(run(NEW, eng, { mod: () => ({ oCut: -100 }) })), up = hf(run(NEW, eng, { mod: () => ({ oCut: 100 }) }));
    assert.ok(up > dn * 1.3, `oCut +100 should raise HF over -100 (${up} vs ${dn})`);
  });

  test(eng + ': stored parameter object unchanged after modulated quanta', () => {
    const keep = {}, mod = b => ({ cCut: b % 3 ? 90 : -1, cRes: 200, cDec: 40, cAtk: 30, cRel: 60, oCut: b % 5 * 40 - 80, oRes: 50, oEnv: -90, oAtk: 20, oDec: -30, oSus: -60, oRel: 77 });
    const r0 = run(NEW, eng, { on: 1 }); void r0;
    const Cls = NEW.reg[ENG[eng]], p0 = new Cls({ processorOptions: { p: {} } }), before = JSON.parse(JSON.stringify(p0.c.p));
    keep.proc = p0;
    for (let b = 0; b < 200; b++) {
      const P = { rate: new Float32Array([1]), gate: new Float32Array([b < 150 ? 1 : 0]), trig: new Float32Array([1]), leg: new Float32Array([0]), bpm: new Float32Array([120]), wob: new Float32Array([0]), syl: new Float32Array([0]), same: new Float32Array([0]), acc: new Float32Array([0]) };
      const m = mod(b); for (const k in m) P[k] = new Float32Array([m[k]]);
      p0.process([], [[new Float32Array(128), new Float32Array(128)]], P);
      assert.deepEqual(JSON.parse(JSON.stringify(p0.c.p)), before, 'quantum ' + b);
    }
  });

  test(eng + ': extremes are finite', () => {
    const cs = ['cCut', 'cRes', 'cDec', 'cAtk', 'cRel'], os = ['oCut', 'oRes', 'oEnv', 'oAtk', 'oDec', 'oSus', 'oRel'];
    for (const v of [0, 255]) { const m = {}; cs.forEach(k => m[k] = v); assert.ok(allFinite(run(NEW, eng, { on: 100, off: 60, mod: () => m })), eng + ' c=' + v); }
    for (const v of [-255, 255]) { const m = {}; os.forEach(k => m[k] = v); assert.ok(allFinite(run(NEW, eng, { on: 100, off: 60, mod: () => m })), eng + ' o=' + v); }
    const m = {}; cs.forEach(k => m[k] = 255); os.forEach(k => m[k] = 255); assert.ok(allFinite(run(NEW, eng, { on: 100, off: 60, mod: () => m })), eng + ' all max');
  });

  if (have(eng, 'atk')) test(eng + ': attack command / offset changes a new note\'s first 50 ms', () => {
    const fast = run(NEW, eng, { on: 30, mod: () => ({ cAtk: 0 }) }), slow = run(NEW, eng, { on: 30, mod: () => ({ cAtk: 254 }) });
    assert.ok(rms(fast, 0, 2400) > rms(slow, 0, 2400) * 3, 'cAtk');
    const f2 = run(NEW, eng, { on: 30, mod: () => ({ oAtk: -255 }) }), s2 = run(NEW, eng, { on: 30, mod: () => ({ oAtk: 255 }) });
    assert.ok(rms(f2, 0, 2400) > rms(s2, 0, 2400) * 1.5, 'oAtk');
  });

  test(eng + ': release command / offset changes the tail', () => {
    const tail = a => rms(a, 128 * 20, 128 * 35);   // 5 to 20 quanta after the gate falls (the vox's syllable ends by itself soon after)
    const s = run(NEW, eng, { on: 15, off: 100, mod: () => ({ cRel: 0 }) }), l = run(NEW, eng, { on: 15, off: 100, mod: () => ({ cRel: 200 }) });
    assert.ok(tail(l) > tail(s) * 3 && tail(l) > 1e-5, 'cRel ' + tail(l) + ' vs ' + tail(s));
    const s2 = run(NEW, eng, { on: 15, off: 100, mod: () => ({ oRel: -255 }) }), l2 = run(NEW, eng, { on: 15, off: 100, mod: () => ({ oRel: 255 }) });
    assert.ok(tail(l2) > tail(s2) * 3 && tail(l2) > 1e-5, 'oRel');
  });

  if (have(eng, 'sus')) test(eng + ': sustain offset changes the sustain level', () => {
    const lv = a => rms(a, 128 * 600, 128 * 640);
    const lo = run(NEW, eng, { on: 650, mod: () => ({ oSus: -200 }) }), hi = run(NEW, eng, { on: 650, mod: () => ({ oSus: 0 }) });
    assert.ok(lv(hi) > lv(lo) * 1.3, 'sus ' + lv(hi) + ' vs ' + lv(lo));
  });

  if (have(eng, 'env')) test(eng + ': filter-envelope offset changes the sweep depth', () => {
    const sw = a => hf(a, 0);
    const params = eng === 'mach' ? { lp: 800 } : {};   // (mach's own 3.5 kHz low-pass hides the 2 kHz band)
    const lo = run(NEW, eng, { params, on: 100, mod: () => ({ oEnv: -255 }) }), hi = run(NEW, eng, { params, on: 100, mod: () => ({ oEnv: 255 }) });
    assert.ok(sw(hi) > sw(lo) * 1.3, 'env ' + sw(hi) + ' vs ' + sw(lo));
  });

  if (have(eng, 'res')) test(eng + ': resonance command / offset changes the peak at the cutoff', () => {
    const fixed = { bass: { cut: 1000, fenv: 0, key: 0 }, acid: { cut: 1000, envAmt: 0, key: 0 }, mach: { lp: 1000, fenv: 0 } }[eng];
    const pk = a => band(a, 128 * 100, 800, 1250);
    const lo = run(NEW, eng, { params: fixed, mod: () => ({ cRes: 0 }) }), hi = run(NEW, eng, { params: fixed, mod: () => ({ cRes: 255 }) });
    assert.ok(pk(hi) > pk(lo) * 1.2, 'cRes ' + pk(hi) + ' vs ' + pk(lo));
    const lo2 = run(NEW, eng, { params: fixed, mod: () => ({ oRes: -255 }) }), hi2 = run(NEW, eng, { params: fixed, mod: () => ({ oRes: 255 }) });
    assert.ok(pk(hi2) > pk(lo2) * 1.2, 'oRes');
  });

  if (have(eng, 'dec')) test(eng + ': decay command / offset changes the amp decay', () => {
    const lv = a => rms(a, 128 * 80, 128 * 120), p = { sus: 0 };
    const s = run(NEW, eng, { params: eng === 'acid' ? { sus: 0 } : p, on: 130, mod: () => ({ cDec: 0 }) }), l = run(NEW, eng, { params: p, on: 130, mod: () => ({ cDec: 200 }) });
    assert.ok(lv(l) > lv(s) * 2, 'cDec ' + lv(l) + ' vs ' + lv(s));
    const s2 = run(NEW, eng, { params: p, on: 130, mod: () => ({ oDec: -255 }) }), l2 = run(NEW, eng, { params: p, on: 130, mod: () => ({ oDec: 255 }) });
    assert.ok(lv(l2) > lv(s2) * 2, 'oDec');
  });
}

test('acid ignores attack (fixed 2 ms): cAtk / oAtk leave the output identical', () => {
  const A = run(NEW, 'acid', { on: 60 }), B = run(NEW, 'acid', { on: 60, mod: () => ({ cAtk: 255, oAtk: 255 }) });
  for (let i = 0; i < A.length; i++) assert.ok(Object.is(A[i], B[i]), 'sample ' + i);
});

test('vox ignores res / env / dec / sus', () => {
  const A = run(NEW, 'vox', { on: 60, off: 20 }), B = run(NEW, 'vox', { on: 60, off: 20, mod: () => ({ cRes: 255, cDec: 255, oRes: 255, oEnv: 255, oDec: -255, oSus: -255 }) });
  for (let i = 0; i < A.length; i++) assert.ok(Object.is(A[i], B[i]), 'sample ' + i);
});

test('a knob turned while a modulation runs is picked up at once', () => {
  const Cls = NEW.reg['thunder-bass'], p = new Cls({ processorOptions: { p: {} } }), out = [];
  for (let b = 0; b < 60; b++) {
    if (b === 30) p.c.set({ cut: 6000 });
    const P = { rate: new Float32Array([1]), gate: new Float32Array([1]), trig: new Float32Array([1]), leg: new Float32Array([0]), bpm: new Float32Array([120]), wob: new Float32Array([0]), oCut: new Float32Array([-60]) };
    const o = [[new Float32Array(128), new Float32Array(128)]]; p.process([], o, P); out.push(...o[0][0]);
  }
  assert.ok(out.some(x => x !== 0), 'sounds');
  assert.ok(p.c.p.cut === 6000, 'stored cut is the knob value');
});
