// Shared by tests/sampler-pitch-sync.test.js: loads the sample-loading and sample-fitting code out of an index.html text (the
// working copy, or an older build to record a baseline from) into a vm. Not a test (no .test.js), so `npm test` does not run it.
'use strict';
const vm = require('vm');
const cutRe = (h, re, what) => { const m = h.match(re); if (!m) throw new Error('anchor not found: ' + what); return m[0]; };
function loadSampler(h, extra) {
  const A = h.indexOf('/* ===ENGINE START=== */'), B = h.indexOf('/* ===ENGINE END=== */');
  const eng = h.slice(A, B);
  const C = h.indexOf('const SYNC_MODES = '), D = h.indexOf('// An AudioBuffer from one channel (mono)');
  if (A < 0 || B < 0 || C < 0 || D < 0) throw new Error('index.html: an anchor moved');
  const chopSrc = h.slice(C, D);
  const own = /const smpOwnTune = [^\n]*\n/.test(h) ? cutRe(h, /const smpOwnTune = [^\n]*\n/, 'smpOwnTune') : '';
  const assign = cutRe(h, /function assignSample\([\s\S]*?\n\}\n/, 'assignSample');
  const lines = extra || '';
  const ctx = vm.createContext({ console, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object, JSON, Map, Set, WeakMap,
    Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
  ctx.globalThis = ctx; ctx.self = ctx;
  const stubs = 'const ROOT = 48; const clamp = (v, a, b) => Math.max(a, Math.min(b, v)); let cur = 0, kit = {voices:[]}, S = {bpm:120, lpb:8, insts:[]};\n'
    + 'const markSmpNew = () => {}, smbAdd = () => {}, changed = () => {}, refreshAll = () => {}, ensure = pad => globalThis.__cache[pad];\n'
    + 'globalThis.__cache = [];\n';
  const tail = '\n;globalThis.__E = {freshLayer, mkVoice, renderVoice, assignSample, applyChop, loopSecs, SYNC_MODES, setKit:(k, s) => { kit = k; S = s; }, setCur:i => { cur = i; }, getS:() => S,\n'
    + '  ' + (lines ? lines : '') + 'slParse, slName, slPlan, slSecs, slChoices, slSet, instBeats, setBeats, slBar, SL_MAX, smpOwnTune: typeof smpOwnTune === "function" ? smpOwnTune : null};';
  vm.runInContext(eng + '\n' + stubs + own + '\n' + assign + '\n' + chopSrc + tail, ctx, { filename: 'index.html(sampler)' });
  ctx.__E.ctx = ctx; return ctx.__E;
}
const sine = (hz, sr, secs) => Float32Array.from({ length: Math.round(sr * secs) }, (_, i) => 0.5 * Math.sin(2 * Math.PI * hz * i / sr));
// the frequency of a rendered sine: rising zero crossings over the first `secs` seconds
function freqOf(d, sr, secs) {
  const n = Math.min(d.length, Math.round(sr * secs)); let first = -1, last = -1, c = 0;
  for (let i = 1; i < n; i++) if (d[i - 1] <= 0 && d[i] > 0) { const f = i - 1 + (0 - d[i - 1]) / (d[i] - d[i - 1]); if (first < 0) first = f; last = f; c++; }
  return c < 2 ? 0 : (c - 1) * sr / (last - first);
}
function hash(a) { let h = 0x811c9dc5 >>> 0; const u = new Uint8Array(a.buffer, a.byteOffset, a.byteLength); for (let i = 0; i < u.length; i++) { h ^= u[i]; h = Math.imul(h, 0x01000193) >>> 0; } return h.toString(16).padStart(8, '0'); }

/* The fixture song: pad 0 is a synth slot (voice tune +24) holding a sample saved the OLD way (layer tune 0, notes written low to
   compensate); pad 1 is a drum pad. Instruments set up the way old songs have them (BEATS only, no `lines`). Returns what applyChop
   gives for a spread of notes, tempos, LPBs and modes: this must be the same before and after any change to loading or fitting. */
function chopFixture(E, fsr) {
  fsr = fsr || 44100;
  const old = (tune) => { const v = E.mkVoice('S', [], { tune, len: 1.2, gate: 400, tail: 0 });
    v.layers[0] = E.freshLayer({ on: true, type: 'sample', level: 0.9, mode: 'AHD', atk: 0.1, hold: 1000, dec: 40, tune: 0, sample: { data: sine(220, fsr, 1.1), sr: fsr, name: 't' } }); return v; };
  const kit = { voices: [old(24), old(0)] };
  const S = { bpm: 131, lpb: 8, insts: [] };
  E.setKit(kit, S);
  kit.voices.forEach((v, i) => { const d = E.renderVoice(v, 48000, null, 0); E.ctx.__cache[i] = { data: d, sr: 48000 }; });
  const out = [];
  for (const pad of [0, 1]) for (const bpm of [90, 131, 174]) for (const lpb of [4, 8]) for (const [slc, sync, beats] of [[0, 1, 4], [0, 2, 2], [0, 3, 8], [16, 1, 4], [8, 0, 4], [0, 1, 1]]) for (const n of [24, 48, 60]) {
    S.lpb = lpb; S.bpm = bpm;
    const I = { pad, tsp: 0, slc, smode: 0, sync, beats, grain: 1, ofs: 0 }; S.insts[0] = I;
    const ev = { type: 'note', t: 1, rate: Math.pow(2, (n - 48) / 12), ofs: 0, cut: null, slc: null };
    const c = E.ctx.__cache[pad]; E.applyChop(ev, I, pad, n, 0, bpm, c.data, c.sr);
    out.push([pad, bpm, lpb, slc, sync, beats, n, ev.rate, ev.ofsSec == null ? null : ev.ofsSec, ev.durSec == null ? null : ev.durSec, ev.cut, ev.st ? [ev.st.mode, ev.st.bpm, ev.st.beats, ev.st.grain] : null]);
  }
  return out;
}
module.exports = { loadSampler, sine, freqOf, hash, chopFixture };
