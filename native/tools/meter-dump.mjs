// U13: THE METERS' REFERENCE (Node only): what the page computes from a signal, with the page's own loops (index.html ~33690 for a strip: an AnalyserNode with
// fftSize 512 on the pan output, its getFloatTimeDomainData buffer = the last 512 samples of the MONO downmix (L + R) / 2 in float32, peak = max |x|; the master's
// two analysers, fftSize 1024, one per channel, peak the same way, and the average pbLvRead's: the mean of x * x). The windows start as zeros. For each signal the
// expected [track peak, track rms, master L peak, rms, master R peak, rms] after EVERY 128-frame block.
//   node native/tools/meter-dump.mjs [outDir]    writes <outDir>/<case>.json and <case>.in.f32 (L then R); default /tmp/u11/meter-ref
import fs from 'node:fs'; import path from 'node:path'; import { fileURLToPath } from 'node:url';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..'), SR = 48000, Q = 128, N = 128 * 220;
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296 * 2 - 1; }; };
const SIG = {
  sine: () => { const L = new Float32Array(N), R = new Float32Array(N); for (let i = 0; i < N; i++) { L[i] = Math.fround(0.5 * Math.sin(2 * Math.PI * 1000 * i / SR)); R[i] = Math.fround(0.25 * Math.sin(2 * Math.PI * 1500 * i / SR)); } return [L, R]; },
  noise: () => { const r = lcg(5), L = new Float32Array(N), R = new Float32Array(N); for (let i = 0; i < N; i++) { L[i] = Math.fround(0.6 * r()); R[i] = Math.fround(0.3 * r()); } return [L, R]; },
  burst: () => { const L = new Float32Array(N), R = new Float32Array(N); for (let i = 0; i < N; i++) { const t = i % 7000; L[i] = Math.fround(t < 300 ? 0.9 * Math.exp(-t / 100) * Math.sin(t * 0.3) : 0); R[i] = Math.fround(t < 300 ? -0.4 * Math.exp(-t / 150) : 0); } return [L, R]; },
  silence_after: () => { const [L, R] = SIG.noise(); L.fill(0, 9000); R.fill(0, 9000); return [L, R]; },
  square: () => { const L = new Float32Array(N), R = new Float32Array(N); for (let i = 0; i < N; i++) { L[i] = Math.floor(i / 50) % 2 ? -1 : 1; R[i] = Math.floor(i / 70) % 2 ? -1 : 1; } return [L, R]; },
  dc_opposite: () => { const L = new Float32Array(N).fill(0.5), R = new Float32Array(N).fill(-0.5); return [L, R]; },   // the mono downmix is 0
  silence: () => [new Float32Array(N), new Float32Array(N)],
};
// the page's maths on a buffer b
const peak = b => { let p = 0; for (let i = 0; i < b.length; i++) { const a = b[i] < 0 ? -b[i] : b[i]; if (a > p) p = a; } return p; };
const rms = b => { let s = 0; for (let i = 0; i < b.length; i++) s += b[i] * b[i]; return Math.sqrt(s / b.length); };
export function expected(L, R) {
  const out = [], mono = new Float32Array(N); for (let i = 0; i < N; i++) mono[i] = Math.fround(L[i] + R[i]) * 0.5;
  for (let b = 0; b < N / Q; b++) { const e = (b + 1) * Q, win = (x, W) => { const w = new Float32Array(W); for (let i = 0; i < W; i++) { const j = e - W + i; w[i] = j >= 0 ? x[j] : 0; } return w; };
    const m = win(mono, 512), l = win(L, 1024), r = win(R, 1024); out.push([peak(m), rms(m), peak(l), rms(l), peak(r), rms(r)]); }
  return out;
}
const f32 = a => new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {   // (not a string compare with 'file://': a Windows path has a drive letter and backslashes)
  const dir = process.argv[2] || '/tmp/u11/meter-ref'; fs.mkdirSync(dir, { recursive: true });
  for (const [name, mk] of Object.entries(SIG)) { const [L, R] = mk(); fs.writeFileSync(path.join(dir, name + '.json'), JSON.stringify({ name, n: N, expected: expected(L, R) })); fs.writeFileSync(path.join(dir, name + '.in.f32'), Buffer.concat([f32(L), f32(R)])); }
  console.log(`wrote ${Object.keys(SIG).length} cases to ${dir}`);
}
