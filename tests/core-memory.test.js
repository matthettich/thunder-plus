// The audio-thread core's per-block / per-sample allocations (stBal's array per sample, the 4-timeline array per voice per
// block, mixOut's closure, and the Rd ring pool) were removed. This renders the SAME voices through the core as it was
// (core/seq-core.js at build 552, commit 90ab6b8, read with `git show`) and as it is now, and asserts every output sample is
// bit-identical. Covered: a mono pad, a stereo pad (fast path and the ramping per-sample path), a tremolo / auto-pan voice,
// a reversed pad (mono, stereo), shaped pads (CUT / RES / DEC) with many notes in a row so the ring pool is reused, a shaped
// note that starts at an offset, a note killed early, and the track gain / pan / master outputs mixOut fills.
'use strict';
const { test } = require('node:test');
const assert = require('node:assert/strict');
const vm = require('node:vm');
const { execFileSync } = require('node:child_process');
const path = require('node:path');
const H = require('./helpers');

const BASE = '90ab6b8';
let old = null;
try { old = execFileSync('git', ['show', BASE + ':core/seq-core.js'], { cwd: path.resolve(__dirname, '..'), encoding: 'utf8', maxBuffer: 1 << 26, stdio: ['ignore', 'pipe', 'ignore'] }); } catch (e) { old = null; }

const SR = 48000, NT = 32;
function lcg(seed) { let s = seed >>> 0; return () => ((s = (Math.imul(s, 1664525) + 1013904223) >>> 0) / 4294967296) * 2 - 1; }

// A scene runs inside the core's own scope (so it builds the core's Auto timelines and Rd readers), and returns every output sample.
const SCENE = `(function(proc, SR, NT, mkData, setTime){
  const T = (tr) => proc.T(tr);
  let seq = 0;
  function voice(o){
    const g = new Auto(o.g == null ? 1 : o.g), f = new Auto(1), pn = new Auto(o.pan || 0), r = new Auto(o.rate == null ? 1 : o.rate);
    if (o.gRamp) g.lin(o.gRamp[0], o.gRamp[1]);
    if (o.panRamp) pn.lin(o.panRamp[0], o.panRamp[1]);
    if (o.rRamp) r.lin(o.rRamp[0], o.rRamp[1]);
    const data = mkData(o.len, o.seed), data2 = o.stereo ? mkData(o.len, o.seed + 1) : null;
    const v = {data, data2, sr:o.sr || 44100, r, g, f, pn, trm:o.trm || null, apn:o.apn || null, base:1, rate:1, choke:null, col:0, ended:false, stopAt:null,
      syn:false, rel:0, track:o.track || 0, end:o.end == null ? Infinity : o.end, pos:o.pos || 0, t0:o.t0 || 0};
    if (o.rev || o.tw) v.rd = new Rd(data, data2, v.sr, o.tw || null, !!o.rev, v.pos);
    proc.alive.add(v); T(v.track).push(v); return v;
  }
  const out = [], mk = () => { const o = []; for (let i = 0; i < NT + 4; i++) o.push(i < NT ? [new Float32Array(128), new Float32Array(128)] : i === NT ? [new Float32Array(128)] : [new Float32Array(128), new Float32Array(128)]); return o; };
  proc.mg[0] = new Auto(1); proc.mg[0].lin(0.25, 0.02); proc.mp[1] = new Auto(0); proc.mp[1].lin(1, 0.03); proc.mm = new Auto(0);
  const spec = [];
  spec.push({len:30000, seed:1});                                                   // mono, fast path
  spec.push({len:30000, seed:2, stereo:true, pan:-0.4, track:1});                   // stereo, fast path
  spec.push({len:30000, seed:3, stereo:true, pan:-0.8, panRamp:[0.7, 0.03], track:2});   // stereo, pan ramp: per-sample balance
  spec.push({len:30000, seed:4, stereo:true, pan:0.5, gRamp:[0.2, 0.02], track:3});      // stereo, gain ramp
  spec.push({len:30000, seed:5, pan:0.1, trm:{hz:6, d:0.2, t:0}, apn:{hz:3, d:0.5, t:0}, track:4});   // tremolo + auto-pan
  spec.push({len:20000, seed:6, rev:true, track:5});                                // reversed mono
  spec.push({len:20000, seed:7, rev:true, stereo:true, pan:0.3, track:6});          // reversed stereo
  spec.push({len:30000, seed:8, rate:1.5, rRamp:[0.7, 0.03], track:7});             // pitch ramp
  spec.push({len:30000, seed:9, end:0.02, track:8});                                // ends early (v.end)
  spec.push({len:30000, seed:19, t0:0.01, track:9});                                // starts a little late
  spec.push({len:30000, seed:20, rev:true, stereo:true, panRamp:[-0.6, 0.02], track:12});   // reversed stereo, per-sample balance
  const live = spec.map(voice);
  // shaped (CUT / RES / DEC) notes, one after another on a few tracks: the ring pool is taken from and given back
  const tws = [{dec:90, cut:120, res:90}, {dec:60}, {cut:200, res:30}, {dec:150, cut:60, res:200}, {dec:40, cut:255, res:0}];
  let n = 0; const queue = [];
  for (let k = 0; k < 44; k++) queue.push({len:1500 + (k * 733) % 6000, seed:30 + k, tw:tws[k % tws.length], track:10 + (k % 2), pos:(k % 3) * 700, pan:((k % 5) - 2) / 3});
  const BLOCKS = 600; let qi = 0;
  for (let b = 0; b < BLOCKS; b++){
    setTime(b * 128 / SR);
    if (b % 6 === 0 && qi < queue.length) live.push(voice(queue[qi++]));   // a new shaped note every 6 blocks, most ended by the time the next ones come
    const o = mk(); proc.process1([], o);
    for (const ch of o) for (const c of ch) out.push(Array.from(c));
    n += proc.alive.size;
  }
  return {out, alive:n, pool:typeof RD_POOL === 'undefined' ? -1 : RD_POOL.length};
})`;

function render(src) {
  let time = 0;
  const posted = [];
  class AWP { constructor() { this.port = { postMessage(m) { posted.push(m); }, onmessage: null }; } }
  const reg = {};
  const ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Map, Set, JSON, Date, Number, String, Object, Array, isFinite, parseInt, parseFloat,
    AudioWorkletProcessor: AWP, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: SR };
  Object.defineProperty(ctx, 'currentTime', { get: () => time });
  ctx.globalThis = ctx;
  vm.createContext(ctx);
  vm.runInContext(src, ctx, { filename: 'seq-core.js' });
  const proc = new reg['thunder-core']({});
  const mkData = (len, seed) => { const r = lcg(seed), d = new Float32Array(len); for (let i = 0; i < len; i++) d[i] = r() * 0.6 + Math.sin(i * 0.01 * (1 + seed % 7)) * 0.3; return d; };
  const scene = vm.runInContext(SCENE, ctx);
  return scene(proc, SR, NT, mkData, t => { time = t; });
}

test('core voices render bit-identically to build 552 (no per-sample / per-block garbage)', { skip: !old && 'git history for ' + BASE + ' not available' }, () => {
  const A = render(old), B = render(H.read('core/seq-core.js'));
  assert.equal(A.out.length, B.out.length);
  assert.equal(A.alive, B.alive, 'the same voices live in every block');
  let nonzero = 0, bad = 0;
  for (let i = 0; i < A.out.length; i++) {
    const a = A.out[i], b = B.out[i];
    for (let k = 0; k < a.length; k++) { if (a[k] !== 0) nonzero++; if (!Object.is(a[k], b[k])) bad++; }
  }
  assert.ok(nonzero > 100000, 'the scene must actually sound (' + nonzero + ' non-zero samples)');
  assert.equal(bad, 0, bad + ' samples differ');
});

test('the Rd ring pool is bounded and gives rings back', () => {
  const B = render(H.read('core/seq-core.js'));
  if (B.pool < 0) return;   // (before the pool existed)
  assert.ok(B.pool >= 1 && B.pool <= 32, 'pool size ' + B.pool);
});
