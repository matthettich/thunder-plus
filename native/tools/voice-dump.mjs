// U08: runs the core's sampler voice player (core/seq-core.js: ThunderCore's startNote / kill / pitchMods / modVoice / process1) in a
// Node `vm`, with small synthetic pads and hand-written event lists, one feature a case, and writes what the voices sum into the
// tracks' outputs (before the page's mixer) for native/tests/test_voice.cpp to be compared against.
//   node native/tools/voice-dump.mjs [outDir]    writes <outDir>/<case>.f32 (planar float32: per track in the case, L then R, all blocks)
//                                                and <outDir>/<case>.json (the case: block size, kit, pads, events); default /tmp/u08
//   node native/tools/voice-dump.mjs --golden    writes native/tests/golden/voice_golden.inc (FNV-1a 64 over each case's float32 bytes)
//   node native/tools/voice-dump.mjs --check     exits 1 if the committed voice_golden.inc differs (Node 22 on Linux is the reference)
// The pads are made with + and * only (a recurrence, no Math.sin / Math.exp), so JS and C++ build the very same float32 samples:
// test_voice.cpp has the same generators (pad()). Change both together.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { seqCoreText } from './webref.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SR = 48000, NT = 32;
const src = seqCoreText();   // the pinned beta copy (native/tests/web-ref, see web-pin.mjs); THUNDER_WEB_REF=root reads the repository root's older core/seq-core.js

// ---- the synthetic pads (mirrored in test_voice.cpp) ----------------------------------------------------------------------
// sine : mono, 44100 Hz, 22050 samples: a 440 Hz oscillator (y[n] = c y[n-1] - y[n-2]) decaying by 0.99997 a sample
// noise: mono, 48000 Hz, 9600 samples: LCG noise (s = s * 1664525 + 1013904223 mod 2^32, from 12345) under a linear fade-out
// saw  : mono, 48000 Hz, 480 samples: a 100 Hz sawtooth (a 480-sample period, so a short "loop" if the case retriggers it)
// st   : stereo pair, 48000 Hz, 24000 samples: L a 330 Hz decaying oscillator, R a falling ramp under a decay
const PADS = {
  sine: () => { const n = 22050, d = new Float32Array(n), c = 1.9898130397128468 /* 2 cos(2 pi 440 / 44100) */; let y1 = 0.0627, y2 = 0, g = 1;
    for (let i = 0; i < n; i++) { const y = i === 0 ? y1 : c * y1 - y2; if (i > 0) { y2 = y1; y1 = y; } d[i] = Math.fround(y * g); g *= 0.99997; } return { data: d, data2: null, sr: 44100 }; },
  noise: () => { const n = 9600, d = new Float32Array(n); let s = 12345;
    for (let i = 0; i < n; i++) { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; d[i] = Math.fround(((s / 4294967296) * 2 - 1) * (1 - i / n)); } return { data: d, data2: null, sr: 48000 }; },
  saw: () => { const n = 480, d = new Float32Array(n); for (let i = 0; i < n; i++) d[i] = Math.fround(i / n * 2 - 1); return { data: d, data2: null, sr: 48000 }; },
  st: () => { const n = 24000, L = new Float32Array(n), R = new Float32Array(n), c = 1.9904 /* about 2 cos(2 pi 330 / 48000) */; let y1 = 0.0431, y2 = 0, g = 1;
    for (let i = 0; i < n; i++) { const y = i === 0 ? y1 : c * y1 - y2; if (i > 0) { y2 = y1; y1 = y; } L[i] = Math.fround(y * g); R[i] = Math.fround((1 - i / n) * 0.5 * g); g *= 0.99985; } return { data: L, data2: R, sr: 48000 }; },
};
const PADN = ['sine', 'noise', 'saw', 'st'];   // pad 0..3

// ---- the cases -------------------------------------------------------------------------------------------------------------
const F = x => x / SR;                                   // a frame as seconds
const FB = (b, o = 0, N = 128) => F(b * N + o);          // block b, o frames in
const note = (o) => Object.assign({ type: 'note', t: 0, track: 0, poly: false, col: 0, pad: 0, rate: 1, gain: 0.8, pan: 128, ofs: 0 }, o);
const KIT0 = [{ choke: 0, syn: false, rel: 0 }, { choke: 0, syn: false, rel: 0 }, { choke: 0, syn: false, rel: 0 }, { choke: 0, syn: false, rel: 0 }];
const kit = (...o) => KIT0.map((k, i) => Object.assign({}, k, o[i] || {}));
// a case: {N (block size), blocks, tracks, bpm, kit, evs: [[block, event | {op:'stopAll', at}], ...]}
const CASES = {
  plain: { tracks: 1, evs: [[0, note({ t: FB(1, 17) })]] },
  pan_rate: { tracks: 2, evs: [[0, note({ t: FB(0, 0), pan: 90, rate: 1.5, gain: 0.5 })], [2, note({ t: FB(3, 40), track: 1, pad: 1, pan: 200, gain: 0.9, rate: 0.75 })]] },
  offrel: { tracks: 2, kit: kit({ syn: true, rel: 0.05 }, {}, { syn: true, rel: 0.2 }), evs: [
    [0, note({ t: FB(0, 5) })], [40, { type: 'off', t: FB(41, 50), track: 0 }],
    [0, note({ t: FB(0, 9), track: 1, pad: 2, col: 1, rate: 0.5 })], [10, note({ t: FB(11), track: 1, pad: 1, col: 2, poly: true })],
    [30, { type: 'off', t: FB(33, 3), track: 1, cols: [1] }], [60, { type: 'off', t: FB(60), track: 1 }]] },
  cutrel: { tracks: 2, kit: kit({ syn: true, rel: 0.04 }, { syn: true, rel: 0 }), evs: [
    [0, note({ t: FB(0), cut: FB(20, 11) })], [0, note({ t: FB(2), track: 1, pad: 1, rel: FB(30), rlo: 0.1 })], [0, note({ t: FB(4), track: 1, pad: 0, poly: true, rel: FB(50, 30) })]] },
  reverse: { tracks: 3, evs: [[0, note({ t: FB(1), rev: true })], [0, note({ t: FB(2), track: 1, pad: 3, rev: true, rate: 1.25 })], [0, note({ t: FB(5), track: 2, pad: 1, rev: true, ofs: 64 })]] },
  shape: { tracks: 4, evs: [
    [0, note({ t: FB(1), pad: 1, tw: { cut: 120, res: 200, dec: 150 } })], [0, note({ t: FB(2), track: 1, pad: 0, tw: { cut: 60, res: 0 } })],
    [0, note({ t: FB(3), track: 2, pad: 1, tw: { dec: 40 } })], [0, note({ t: FB(4), track: 3, pad: 1, ofs: 100, tw: { cut: 200, res: 255, dec: 220 } })],
    [10, note({ t: FB(12), track: 0, pad: 3, tw: { cut: 90, res: 128 }, rate: 0.9 })]] },
  shape_rev: { tracks: 1, evs: [[0, note({ t: FB(0) })], [10, note({ t: FB(12), pad: 1, rev: true, tw: { cut: 100, res: 50 } })], [20, note({ t: FB(22), pad: 3, rev: true })]] },   // reversed + shaped: varRec gives null, the page plays it; the voices still give way (take)
  loop: { tracks: 1, evs: [[0, note({ t: FB(1), pad: 2, rate: 0.25 })], [20, note({ t: FB(21), pad: 2, rate: 1, poly: true })]] },
  slice: { tracks: 3, evs: [[0, note({ t: FB(1), ofs: 64 })], [0, note({ t: FB(1, 40), track: 1, pad: 1, ofs: 128, rate: 1.1 })], [0, note({ t: FB(2), track: 2, pad: 3, ofs: 200 })]] },
  choke: { tracks: 3, kit: kit({ choke: 1 }, { choke: 1 }, { choke: 0 }, { choke: 2 }), evs: [
    [0, note({ t: FB(0) })], [0, note({ t: FB(1), track: 2, pad: 2, rate: 0.3 })], [0, note({ t: FB(1, 7), track: 1, pad: 3, gain: 0.3 })],
    [30, note({ t: FB(31, 60), track: 1, pad: 1 })]] },
  mono: { tracks: 2, kit: kit({ syn: true, rel: 0.1 }), evs: [
    [0, note({ t: FB(0) })], [20, note({ t: FB(21, 30), pad: 1, gain: 0.6 })],
    ...[0, 1, 2, 3, 4, 5].map(k => [k * 10, note({ t: FB(k * 10 + 1, k * 3), track: 1, pad: k % 3, poly: true, col: k, rate: 0.7 + k * 0.1 })])] },
  glide: { tracks: 2, evs: [
    [0, note({ t: FB(0), slide: { from: 0.5, dur: 0.1 } })], [0, note({ t: FB(1), track: 1, pad: 1, rate: 2, slide: { from: 1, dur: 0 } })],
    [0, note({ t: FB(2), track: 1, pad: 0, poly: true, rate: 1, slide: { from: 0.25, dur: 0.05 } })],
    [30, { type: 'mod', t: FB(31), track: 0, mul: 2 }]] },
  pitchmods: { tracks: 4, evs: [
    [0, note({ t: FB(0), arp: [[FB(0), 1], [FB(8), 1.5], [FB(16), 2], [FB(24), 1.25]] })],
    [0, note({ t: FB(1), track: 1, pad: 1, bend: { mul: 2, t1: FB(40) } })],
    [0, note({ t: FB(2), track: 2, pad: 0, vib: { spd: 5, dep: 10, t1: FB(60) } })],
    [0, note({ t: FB(3), track: 3, pad: 3, arp: [] })],
    [20, { type: 'mod', t: FB(21), track: 1, bend: { mul: 0.5, t1: FB(60) } }], [20, { type: 'mod', t: FB(21, 5), track: 3, vib: { spd: 2, dep: 15, t1: FB(80) } }],
    [30, { type: 'mod', t: FB(31), track: 0, arp: [[FB(31), 2], [FB(36), 1]] }]] },
  modvoice: { tracks: 2, kit: kit({ syn: true, rel: 0.05 }), evs: [
    [0, note({ t: FB(0) })], [0, note({ t: FB(0, 3), track: 1, pad: 3 })],
    [20, { type: 'mod', t: FB(22, 10), track: 0, gain: 0.2, pan: 0 }], [30, { type: 'mod', t: FB(31), track: 1, pan: 255, mul: 1.5 }],
    [50, { type: 'mod', t: FB(52), track: 0, cut: FB(53) }], [60, { type: 'mod', t: FB(61), track: 1, rel: FB(62) }], [70, { type: 'mod', t: FB(71), track: 1, gain: 1 }]] },
  pan: { tracks: 4, evs: [
    [0, note({ t: FB(0), pan: 0 })], [0, note({ t: FB(0), track: 1, pad: 1, pan: 255 })], [0, note({ t: FB(0), track: 2, pad: 3, pan: 20 })], [0, note({ t: FB(0), track: 3, pad: 3, pan: 230, rate: 0.8 })]] },
  stereo: { tracks: 2, evs: [
    [0, note({ t: FB(0), pad: 3 })], [0, note({ t: FB(1), track: 1, pad: 3, pan: 60, rate: 1.1 })], [40, { type: 'mod', t: FB(41), track: 1, pan: 200 }], [60, { type: 'mod', t: FB(62), track: 1, pan: 90, gain: 0.3 }]] },
  gainramp: { tracks: 3, evs: [
    [0, note({ t: FB(0), fdi: 0.05 })], [0, note({ t: FB(1), track: 1, pad: 1, fdi: 0.01, gain: 0.7, ofs: 30 })],
    [0, note({ t: FB(2), track: 2, pad: 3 })], [20, { type: 'mod', t: FB(21, 40), track: 2, gain: 0.1 }], [40, { type: 'mod', t: FB(41), track: 2, gain: 0.9 }]] },
  trem_apan: { tracks: 3, bpm: 140, evs: [
    [0, note({ t: FB(0), trm: 0x4f })], [0, note({ t: FB(0), track: 1, pad: 1, apn: 0x38 })], [0, note({ t: FB(0), track: 2, pad: 3, trm: 0x28, apn: 0x1f, fdi: 0.02 })]] },
  midblock: { N: 100, tracks: 1, evs: [[0, note({ t: F(5 * 100 + 37), pad: 1 })], [3, note({ t: F(12 * 100 + 99), pad: 0, track: 0, poly: true, rate: 1.3 })], [4, note({ t: F(7 * 100 + 1), pad: 2, track: 0, poly: true, rate: 2 })]], blocks: 260 },
  overlap: { tracks: 2, evs: [
    [0, note({ t: FB(0), poly: true, col: 0 })], [0, note({ t: FB(8), poly: true, col: 1, pad: 1, rate: 1.3 })], [0, note({ t: FB(16), poly: true, col: 2, pad: 3, pan: 80 })],
    [0, note({ t: FB(3), track: 1, pad: 0, rate: 0.9, pan: 180 })], [10, note({ t: FB(12), track: 1, pad: 1, poly: true, pan: 60 })]] },
  stop: { tracks: 2, kit: kit({ syn: true, rel: 0.05 }), evs: [
    [0, note({ t: FB(0) })], [0, note({ t: FB(30), track: 1, pad: 1 })], [0, { type: 'stp', t: FB(12), track: 0 }], [0, note({ t: FB(40), track: 0, pad: 3 })],
    [20, { op: 'stopAll', at: FB(25) }], [21, note({ t: FB(26), track: 1, pad: 1 })], [22, note({ t: FB(22, 20), track: 0, pad: 2 })], [50, { type: 'stp', t: FB(50), all: true }]] },
  tiny_blocks: { N: 7, blocks: 2400, tracks: 2, evs: [[0, note({ t: F(20), pad: 3, pan: 70 })], [30, note({ t: F(33 * 7 + 3), track: 1, pad: 1, ofs: 50, tw: { cut: 100, res: 100, dec: 100 } })], [400, { type: 'mod', t: F(400 * 7), track: 0, gain: 0.2 }]] },
};
export const CASE_NAMES = Object.keys(CASES);

// ---- the processor, in a vm ------------------------------------------------------------------------------------------------
function makeCore() {
  let Cls = null;
  const ctx = vm.createContext({ console, registerProcessor(n, c) { Cls = c; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
    sampleRate: SR, currentTime: 0, currentFrame: 0 });
  vm.runInContext(src + '\n;globalThis.__u08 = { bufs: coreBufs, setS(s){ S = s; } };', ctx, { filename: 'seq-core.js' });
  return { ctx, proc: new Cls(), U: ctx.__u08 };
}

function runCase(name) {
  const C = CASES[name], N = C.N || 128, blocks = C.blocks || 200, bpm = C.bpm || 120, K = C.tracks;
  const { ctx, proc, U } = makeCore();
  const used = new Set(); for (const [, e] of C.evs) if (e.pad != null) used.add(e.pad);
  PADN.forEach((n, i) => { const p = PADS[n](); U.bufs.set(i, p); });
  U.setS({ bpm, insts: [] });
  proc.kv = (C.kit || KIT0).map(k => Object.assign({ live: false }, k));
  const out = []; for (let t = 0; t < K; t++) out.push([new Float32Array(blocks * N), new Float32Array(blocks * N)]);
  const outputs = []; outputs.length = NT + 6;
  for (let t = 0; t < K; t++) outputs[t] = [new Float32Array(N), new Float32Array(N)];
  for (let b = 0; b < blocks; b++) {
    ctx.currentTime = b * N / SR; ctx.currentFrame = b * N;
    for (const [bb, e] of C.evs) if (bb === b) { if (e.op === 'stopAll') proc.stopAll(e.at); else proc.ev(JSON.parse(JSON.stringify(e))); }
    for (let t = 0; t < K; t++) { outputs[t][0].fill(0); outputs[t][1].fill(0); }
    proc.process([], outputs);
    for (let t = 0; t < K; t++) { out[t][0].set(outputs[t][0], b * N); out[t][1].set(outputs[t][1], b * N); }
  }
  const total = new Float32Array(K * 2 * blocks * N); let o = 0; for (let t = 0; t < K; t++) for (let c = 0; c < 2; c++) { total.set(out[t][c], o); o += blocks * N; }
  const meta = { id: name, N, blocks, tracks: K, bpm, sr: SR, pads: PADN, kit: (C.kit || KIT0), events: C.evs.map(([b, e]) => ({ b, e })) };
  return { total, meta, alive: proc.alive.size };
}

// FNV-1a, 64 bits, over the float32 bytes
const M64 = (1n << 64n) - 1n;
function fnv(f32) { const u = new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength); let h = 0xcbf29ce484222325n; for (let i = 0; i < u.length; i++) { h ^= BigInt(u[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';

const a = process.argv.slice(2);
const results = CASE_NAMES.map(n => ({ n, r: runCase(n) }));
const inc = ['// GENERATED by native/tools/voice-dump.mjs (--golden) from core/seq-core.js. Do not edit: a mismatch means the port is wrong.',
  '// FNV-1a 64 over each case\'s float32 output (planar per track, L then R). Node 22 on Linux is the reference engine.',
  'struct VoiceGolden { const char* name; unsigned long long hash; int floats; };',
  'static const VoiceGolden kVoiceGolden[] = {',
  ...results.map(({ n, r }) => `  {"${n}", ${hex(fnv(r.total))}, ${r.total.length}},`), '};', `static const int kVoiceGoldenN = ${results.length};`, ''].join('\n');
const incPath = path.join(ROOT, 'native/tests/golden/voice_golden.inc');
if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
else if (a[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('voice_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('voice_golden.inc is current'); }
else {
  const dir = a[0] || '/tmp/u08'; fs.mkdirSync(dir, { recursive: true });
  for (const { n, r } of results) { fs.writeFileSync(path.join(dir, n + '.f32'), Buffer.from(r.total.buffer)); fs.writeFileSync(path.join(dir, n + '.json'), JSON.stringify(r.meta)); }
  console.log('wrote ' + results.length + ' cases to ' + dir);
}
