// Shared test helpers: load the app's browser-style scripts into a Node vm, with no browser and no audio device.
// Nothing here changes the app; the files under test are read from disk exactly as they ship.
'use strict';
const fs = require('fs'), path = require('path'), vm = require('vm');
const ROOT = path.resolve(__dirname, '..');
const read = rel => fs.readFileSync(path.join(ROOT, rel), 'utf8');

/* An AudioWorkletGlobalScope-like sandbox. `registerProcessor` records what a worklet registers instead of failing. */
function workletScope(extra = {}) {
  const registered = {};
  const ctx = Object.assign({
    console: { warn() {}, log() {}, error() {} }, Math, Float32Array, Float64Array, Int8Array, Int16Array, Int32Array, Uint8Array,
    Uint16Array, Uint32Array, ArrayBuffer, DataView, Map, Set, WeakMap, Promise, JSON, Date, Number, String, Object, Array, Error,
    WebAssembly, Symbol, parseInt, parseFloat, isFinite, isNaN, setTimeout, clearTimeout, queueMicrotask,
    sampleRate: 48000, currentFrame: 0, currentTime: 0,
    registerProcessor: (n, c) => { registered[n] = c; },
    AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
  }, extra);
  ctx.globalThis = ctx; ctx.self = ctx;
  vm.createContext(ctx); ctx.__registered = registered;
  return ctx;
}
/* The same, minus the worklet pieces: this is how a page (or the render Worker) loads waves-worklet.js, and the only way
   it publishes WavesDSP (it registers a processor instead when AudioWorkletProcessor exists). */
function pageScope(extra = {}) {
  const ctx = workletScope(extra); delete ctx.AudioWorkletProcessor; delete ctx.registerProcessor; delete ctx.sampleRate;
  return ctx;
}
/* Run a script in the scope. The app files wrap everything in one closure on purpose (a page that loads them keeps its own
   names), so internals such as `soft` or `rng` are not reachable from outside. `expose` lists names to publish: they are
   copied out by a statement inserted INSIDE that closure, just before its final `})();`. This is done to the text that is
   run, in memory; the file on disk is never touched, and a file with no such closing line is run unchanged. */
function load(ctx, rel, expose = []) {
  let src = read(rel);
  if (expose.length) {
    const end = src.lastIndexOf('})();');
    if (end < 0) throw new Error(rel + ': no closing })(); to publish ' + expose.join(', ') + ' through');
    src = src.slice(0, end) + '\n;' + expose.map(n => `try{globalThis.__x_${n}=${n};}catch(_){}`).join('') + '\n' + src.slice(end);
  }
  vm.runInContext(src, ctx, { filename: rel });
  for (const n of expose) if (('__x_' + n) in ctx) ctx[n] = ctx['__x_' + n];
  return ctx;
}
/* Deterministic stand-ins for time and randomness, so a golden checksum cannot depend on the clock or on test order. */
function freezeClock(ctx) {
  let t = 1e12; ctx.Date = class extends Date { static now() { return t += 1; } };
  // The waves oscillator starts every voice at phase Math.random(), which the engine's `seed` option does not control (it only
  // seeds rng(nextSeed())). That is a musical choice and the app is left alone: the test scope gets its own Math with a
  // deterministic random() instead, so the same patch renders the same audio every time. Everything else on Math is real.
  const M = Object.create(Math); let s = 0x9e3779b9; M.random = () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; };
  ctx.Math = M;
  return ctx;
}

/* FNV-1a over the exact float32 bit patterns: any change to the audio, however small, changes the hash. Reads the raw
   bytes of the array, so it also tells -0 from 0 and one NaN payload from another (which is what a regression looks like). */
function hashFloats(arrs) {
  let h = 0x811c9dc5 >>> 0;
  for (const a of [].concat(arrs)) { const u = new Uint8Array(a.buffer, a.byteOffset, a.byteLength); for (let i = 0; i < u.length; i++) { h ^= u[i]; h = Math.imul(h, 0x01000193) >>> 0; } }
  return h.toString(16).padStart(8, '0');
}
/* A fixed, non-trivial test signal: two sines plus a deterministic noise burst (xorshift, not Math.random). */
function testSignal(n, sr = 48000, seed = 12345) {
  const out = new Float32Array(n); let s = seed >>> 0 || 1;
  for (let i = 0; i < n; i++) { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; const noise = (s / 4294967296 - .5) * .1;
    out[i] = Math.fround(.4 * Math.sin(2 * Math.PI * 220 * i / sr) + .2 * Math.sin(2 * Math.PI * 1337 * i / sr) + (i < n >> 2 ? noise : 0)); }
  return out;
}

/* GOLDEN FILES: expected hashes live in tests/golden/<name>.json. A refactor that changes the audio fails the test; an
   intended change is accepted by running `UPDATE_GOLDEN=1 npm test`, which rewrites the file as a reviewable diff. */
const GOLDEN = path.join(__dirname, 'golden');
function golden(name, actual) {
  const file = path.join(GOLDEN, name + '.json');
  if (process.env.UPDATE_GOLDEN) { fs.mkdirSync(GOLDEN, { recursive: true }); fs.writeFileSync(file, JSON.stringify(actual, null, 1) + '\n'); return { updated: true }; }
  if (!fs.existsSync(file)) throw new Error('No golden file ' + path.relative(ROOT, file) + '. Create it with: UPDATE_GOLDEN=1 npm test');
  const want = JSON.parse(fs.readFileSync(file, 'utf8')), diff = [];
  for (const k of new Set([...Object.keys(want), ...Object.keys(actual)])) if (want[k] !== actual[k]) diff.push(k + ': expected ' + want[k] + ', got ' + actual[k]);
  return { diff };
}
module.exports = { golden, ROOT, read, workletScope, pageScope, load, freezeClock, hashFloats, testSignal };
