// index.html's inline worklets lost some per-block garbage (BassProc's rate closure, VoxProc's `at` closure, PitchFx's all-pass
// iterator; ThunderFx's subarray views are the same data, covered by the chains / flex goldens). This renders the bass and voice
// processors and PitchFxCore (reverb: all shimmer modes; delay: every mode) from index.html as it was at build 552 (commit
// 90ab6b8, read with `git show`) and as it is now, and asserts every output sample is bit-identical.
'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const path = require('node:path');
const { execFileSync } = require('node:child_process');
const H = require('./helpers');

const BASE = '90ab6b8';
let old = null;
try { old = execFileSync('git', ['show', BASE + ':index.html'], { cwd: path.resolve(__dirname, '..'), encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { old = null; }

function classText(html, name) {
  const a = html.indexOf('\nclass ' + name + ' {') + 1; assert.ok(a > 0, name);
  let d = 0, i = a, started = false;
  for (; i < html.length; i++) { const c = html[i]; if (c === '{') { d++; started = true; } else if (c === '}') { d--; if (started && d === 0) break; } }
  return html.slice(a, i + 1);
}
function lcg(seed) { let s = seed >>> 0; return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296) * 2 - 1; }
const SR = 48000;

function load(html) {
  const reg = {};
  class AWP { constructor() { this.port = { postMessage() {}, onmessage: null }; } }
  const ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Object, Array, Map, Set, JSON, Date, Number, String, RegExp, isFinite, parseFloat, parseInt,
    AudioWorkletProcessor: AWP, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: SR, currentTime: 0 };
  ctx.globalThis = ctx; vm.createContext(ctx);
  const rv = html.indexOf('function rvMix(m){'); vm.runInContext(html.slice(rv, html.indexOf('\n', rv)), ctx);
  for (const n of ['BassCore', 'VoxCore', 'PitchFxCore']) vm.runInContext(classText(html, n), ctx);
  vm.runInContext('this.PitchFxCore = PitchFxCore; this.BassCore = BassCore; this.VoxCore = VoxCore;', ctx);
  for (const [name, end] of [['BASS_PROC', "registerProcessor('thunder-bass', BassProc);`;"], ['VOX_PROC', "registerProcessor('thunder-vox', VoxProc);`;"]]) {
    const a = html.indexOf('const ' + name + ' = `'), b = html.indexOf(end, a) + end.length;
    assert.ok(a > 0 && b > a, name);
    vm.runInContext(html.slice(a, b) + '\n;(0, eval)', ctx);
    vm.runInContext('(new Function("AudioWorkletProcessor", "registerProcessor", "BassCore", "VoxCore", "sampleRate", ' + name + '))', ctx)(AWP, ctx.registerProcessor, ctx.BassCore, ctx.VoxCore, SR);
  }
  return { reg, ctx };
}

function runProc(Cls, names, seed) {
  const p = new Cls({ processorOptions: {} }), r = lcg(seed), out = [];
  for (let b = 0; b < 400; b++) {
    const P = {};
    for (const n of names) P[n] = new Float32Array(1);
    P.rate = b % 7 < 3 ? new Float32Array(128).map((_, i) => 0.8 + (i + b) * 0.004) : new Float32Array([0.7 + (b % 5) * 0.2]);
    const k = b % 20;
    P.gate = k >= 2 && k < 14 ? (k === 8 ? new Float32Array(128).map((_, i) => i < 60 ? 1 : 0) : new Float32Array([1])) : new Float32Array([0]);
    P.trig = new Float32Array(128).fill(k < 8 ? 1 : 2); if (k === 8) P.trig.fill(3, 50);
    if (P.leg) P.leg = new Float32Array([b % 3 === 0 ? 1 : 0]);
    if (P.syl) P.syl = new Float32Array([Math.floor(b / 20)]);
    if (P.same) P.same = new Float32Array([b % 5 === 0 ? 1 : 0]);
    if (P.bpm) P.bpm = new Float32Array([120 + (b % 3)]);
    if (P.wob) P.wob = new Float32Array([b % 4 === 0 ? 1 : 0]);
    const o = [[new Float32Array(128), new Float32Array(128)]];
    p.process([], o, P); out.push(Array.from(o[0][0]), Array.from(o[0][1]));
  }
  return out;
}
const same = (A, B, what) => {
  assert.equal(A.length, B.length); let nz = 0, bad = 0;
  for (let i = 0; i < A.length; i++) for (let k = 0; k < A[i].length; k++) { if (A[i][k] !== 0) nz++; if (!Object.is(A[i][k], B[i][k])) bad++; }
  assert.ok(nz > 1000, what + ' must sound (' + nz + ')'); assert.equal(bad, 0, what + ': ' + bad + ' samples differ');
};

const skip = !old && 'git history for ' + BASE + ' not available';
test('BassProc and VoxProc render bit-identically to build 552', { skip }, () => {
  const A = load(old), B = load(H.read('index.html'));
  same(runProc(A.reg['thunder-bass'], ['bpm', 'wob', 'trig', 'leg'], 1), runProc(B.reg['thunder-bass'], ['bpm', 'wob', 'trig', 'leg'], 1), 'bass');
  same(runProc(A.reg['thunder-vox'], ['trig', 'leg', 'syl', 'same'], 2), runProc(B.reg['thunder-vox'], ['trig', 'leg', 'syl', 'same'], 2), 'vox');
});

test('PitchFxCore (reverb shimmer modes, every delay mode) renders bit-identically to build 552', { skip }, () => {
  const A = load(old).ctx.PitchFxCore, B = load(H.read('index.html')).ctx.PitchFxCore;
  const cfgs = [];
  for (const kind of [1, 0]) for (const mode of [0, 1, 2, 3, 4]) cfgs.push({ kind, mode, mix: 0.6, pitch: 7, pitch2: -5, tone: 0.5, size: 0.6, fb: 0.5, time: 120, wow: 0.4 });
  const run = (Cls, cfg) => {
    const f = new Cls(SR), r = lcg(5), out = [];
    for (let b = 0; b < 500; b++) {
      const L = new Float32Array(128).map((_, i) => b < 100 ? r() * 0.5 * Math.sin((b * 128 + i) * 0.03) : 0), R = new Float32Array(128).map((_, i) => b < 100 ? r() * 0.4 : 0);
      f.process(L, R, 128, cfg); out.push(Array.from(L), Array.from(R));
    }
    return out;
  };
  for (const cfg of cfgs) same(run(A, cfg), run(B, cfg), 'pitchfx kind ' + cfg.kind + ' mode ' + cfg.mode);
});
