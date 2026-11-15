// U10: reference for the beat clock (core/src/mix/beat.cpp). Runs the core's own processor (core/seq-core.js: ThunderCore.ev 'beat', the
// 'stop' message and process1's fill of output NT) in a Node `vm`, feeds it hand-made sequences of the sequencer's 'beat' events the way the
// sequencer books them (a row a time, LEAD = 0.25 s ahead of the block), and writes the clock output: beat + 1 while a song plays, 0 stopped.
//   node native/tools/beat-dump.mjs [outDir]   writes <outDir>/<case>.f32 (the output NT, float32, all blocks) and <case>.json (block size, events)
//   node native/tools/beat-dump.mjs --golden   writes native/tests/golden/beat_golden.inc (FNV-1a 64 over each case's float32 bytes)
//   node native/tools/beat-dump.mjs --check    exits 1 if the committed beat_golden.inc differs
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SR = 48000, NT = 32, LEAD = 0.25;
const src = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8').replace(/\r\n/g, '\n');

// rows: [{t, b}] from a tempo schedule: segments [{rows, bpm, lpb}] from beat b0 at time t0; a row lasts 60 / bpm / lpb s and is 1 / lpb of a beat
function rows(t0, b0, segs) { const out = []; let t = t0, b = b0; for (const s of segs) for (let i = 0; i < s.rows; i++) { out.push({ t, b }); t += 60 / s.bpm / (s.lpb || 4); b += 1 / (s.lpb || 4); } return { rows: out, t, b }; }
const S4 = (n, bpm = 120, lpb = 4) => ({ rows: n, bpm, lpb });
// a case: {N, secs, ev: [rows...], stops: [{at (s) when sent, to (the at argument or null)}], late (blocks a row reaches the processor after its lead time)}
const CASES = {
  steady: { secs: 4, seqs: [rows(0, 0, [S4(32 * 4)])] },
  tempo: { secs: 8, seqs: [rows(0, 0, [S4(40, 120), S4(50, 200), S4(100, 70)])] },
  tempo_lpb: { secs: 6, seqs: [rows(0, 0, [S4(30, 90, 4), S4(60, 90, 8), S4(40, 133.3, 3), S4(40, 133.3, 16)])] },
  stop_restart: { secs: 5, seqs: [rows(0, 0, [S4(8 * 4 + 8)]), rows(1.5, 6.25, [S4(60, 150)]), rows(4, 0, [S4(20)])], stops: [{ send: 1.0, at: 1.0 }, { send: 3.0, at: null }, { send: 3.9, at: 3.95 }] },
  wrap16: { secs: 14, seqs: [rows(0, 12.5, [S4(28 * 4)])] },
  start33: { secs: 3, seqs: [rows(0, 33.25, [S4(50)])] },
  loop: { secs: 12, seqs: [rows(0, 0, [S4(32)]), rows(4, 0, [S4(32)]), rows(8, 0, [S4(32)])] },
  loop_16: { secs: 14, seqs: [rows(0, 12, [S4(16)]), rows(2, 14, [S4(16)]), rows(4, 12, [S4(40)])] },
  midblock: { N: 100, secs: 3, seqs: [rows(0.0123, 0, [S4(60)])] },
  late: { secs: 4, late: 5, seqs: [rows(0.5, 3, [S4(80, 140)])] },
  stop_future: { secs: 3, seqs: [rows(0, 0, [S4(40)]), rows(1.8, 2, [S4(20)])], stops: [{ send: 0.5, at: 0.62 }, { send: 1.5, at: 1.6 }] },
  fast: { secs: 3, seqs: [rows(0, 0, [S4(2000, 300, 16)])] },
  long: { secs: 180, seqs: [rows(0, 0, [S4(4 * 4 * 100, 140, 4), S4(4 * 4 * 100, 140, 4)])] },
  tiny_blocks: { N: 7, secs: 1.5, seqs: [rows(0.0007, 0, [S4(20, 130, 4)]), rows(0.5, 5, [S4(20, 99, 4)])], stops: [{ send: 0.45, at: 0.45 }] },
  one_frame: { N: 1, secs: 0.12, seqs: [rows(0.002, 0, [S4(4, 120, 4)])] },
};
export const CASE_NAMES = Object.keys(CASES);

function makeCore() {
  let Cls = null;
  const ctx = vm.createContext({ console, registerProcessor(n, c) { Cls = c; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
    sampleRate: SR, currentTime: 0, currentFrame: 0 });
  vm.runInContext(src + '\n;globalThis.__u10 = { setS(s){ S = s; } };', ctx, { filename: 'seq-core.js' });
  return { ctx, proc: new Cls(), U: ctx.__u10 };
}

function runCase(name) {
  const C = CASES[name], N = C.N || 128, blocks = Math.ceil(C.secs * SR / N), late = C.late || 0;
  const { ctx, proc, U } = makeCore(); U.setS({ bpm: 120, insts: [] }); proc.kv = [];
  // the schedule: block -> actions, in order. A row is handed over in the first block whose start + LEAD reaches it (then `late` blocks later).
  const acts = new Map(), put = (b, a) => { if (!acts.has(b)) acts.set(b, []); acts.get(b).push(a); };
  for (const sq of C.seqs) for (const r of sq.rows) { const bk = Math.max(0, Math.ceil((r.t - LEAD) * SR / N - 1e-9)) + late; if (bk < blocks) put(bk, { type: 'beat', t: r.t, b: r.b }); }
  for (const s of C.stops || []) put(Math.ceil(s.send * SR / N), { op: 'stop', at: s.at });
  // order inside a block: stops after the rows already due (the sender's order is by time): keep insertion order, but a stop sent in block k comes after row events of k whose t <= send
  const out = new Float32Array(blocks * N), outputs = []; outputs.length = NT + 6;
  outputs[0] = [new Float32Array(N), new Float32Array(N)]; outputs[NT] = [new Float32Array(N)];
  const evlog = [];
  for (let b = 0; b < blocks; b++) {
    ctx.currentTime = b * N / SR; ctx.currentFrame = b * N;
    for (const a of acts.get(b) || []) { evlog.push({ b, a }); if (a.op === 'stop') proc.msg(a.at == null ? { op: 'stop' } : { op: 'stop', at: a.at }); else proc.ev(JSON.parse(JSON.stringify(a))); }
    outputs[0][0].fill(0); outputs[0][1].fill(0); outputs[NT][0].fill(0.5);   // (a sentinel: the processor must write every sample)
    proc.process([], outputs);
    out.set(outputs[NT][0], b * N);
  }
  return { out, meta: { id: name, N, blocks, sr: SR, events: evlog } };
}

const M64 = (1n << 64n) - 1n;
function fnv(f32) { const u = new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength); let h = 0xcbf29ce484222325n; for (let i = 0; i < u.length; i++) { h ^= BigInt(u[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';
const a = process.argv.slice(2);
const results = CASE_NAMES.map(n => ({ n, r: runCase(n) }));
const inc = ['// GENERATED by native/tools/beat-dump.mjs (--golden) from core/seq-core.js. Do not edit: a mismatch means the port is wrong.',
  '// FNV-1a 64 over each case\'s float32 clock output (the core processor\'s output NT).',
  'struct BeatGolden { const char* name; unsigned long long hash; int floats; };',
  'static const BeatGolden kBeatGolden[] = {', ...results.map(({ n, r }) => `  {"${n}", ${hex(fnv(r.out))}, ${r.out.length}},`), '};', `static const int kBeatGoldenN = ${results.length};`, ''].join('\n');
const incPath = path.join(ROOT, 'native/tests/golden/beat_golden.inc');
if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
else if (a[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('beat_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('beat_golden.inc is current'); }
else {
  const dir = a[0] || '/tmp/u10/beat-ref'; fs.mkdirSync(dir, { recursive: true });
  for (const { n, r } of results) { fs.writeFileSync(path.join(dir, n + '.f32'), Buffer.from(r.out.buffer)); fs.writeFileSync(path.join(dir, n + '.json'), JSON.stringify(r.meta)); }
  console.log('wrote ' + results.length + ' beat cases to ' + dir);
}
