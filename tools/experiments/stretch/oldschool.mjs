// OLDSCHOOL (cyclic ramp stretch) prototype and measurements. NOT shipped. Run: node tools/experiments/stretch/oldschool.mjs [outdir]
// Algorithm: the output is exactly T = round(beats*60/bpm*sr) samples. It is cut into grains of g samples. Grain k plays the source
// from its START POSITION p_k = L * ramp(k*g / T) (sampled and held at the grain boundary, as a sampler re-triggering its loop does),
// as a hard loop of `g` OUTPUT samples read at ratio r (1 = original pitch). x = crossfade, a fraction of g (0 = authentic hard cut).
import fs from 'fs'; import path from 'path';
import { makeBreak, writeWav, pageStretch } from './lib.mjs';

export const RAMPS = {
  fwd: u => u, rev: u => 1 - u,
  pingpong: u => u < 0.5 ? 2 * u : 2 - 2 * u,
  ease: u => u * u * (3 - 2 * u),            // slow at the ends
  exp: u => u * u,                           // slow start, fast end
};
// o: {g (samples), r (pitch ratio), x (0..0.5 of g), ramp (name | fn), wrap (read past the end: wrap or clamp the start)}
export function oldschool(d, sr, L, T, o){
  const g = Math.max(8, Math.round(o.g)), r = o.r == null ? 1 : o.r, x = Math.min(0.5, Math.max(0, o.x || 0)), xf = Math.round(x * g);
  const ramp = typeof o.ramp === 'function' ? o.ramp : RAMPS[o.ramp || 'fwd'];
  const out = new Float32Array(T), span = g * r;
  const at = (pos) => { const i = Math.floor(pos), f = pos - i; const a = i < L && i >= 0 ? d[i] : 0, b = i + 1 < L && i + 1 >= 0 ? d[i + 1] : 0; return a + (b - a) * f; };   // linear
  for (let s0 = 0; s0 < T; s0 += g){
    let p = L * ramp(Math.min(1, s0 / T));
    if (!o.wrap) p = Math.min(p, Math.max(0, L - span - 1));          // the last grains keep repeating the last g of the sample
    p = Math.round(p);
    for (let i = -xf; i < g + xf; i++){
      const n = s0 + i; if (n < 0 || n >= T) continue;
      const w = xf ? (i < xf ? (i + xf) / (2 * xf) : i >= g - xf ? (g + xf - i) / (2 * xf) : 1) : 1;
      // xmode 'wrap' (the page's LOOP today): the overlap reads the loop's own end / start, so the loop's wrap point sits in the middle of every
      // crossfade (it halves the click, does not remove it). 'straight' (proposed): the overlap reads the source on past the grain's end and
      // before its start, so two straight reads of the source are blended and there is no wrap point in it.
      const ph = o.xmode === 'straight' ? i : ((i % g) + g) % g;
      const pos = p + ph * r;
      out[n] += (r === 1 && pos === (pos | 0) ? (pos >= 0 && pos < L ? d[pos] : 0) : at(o.wrap ? pos % L : pos)) * Math.min(1, Math.max(0, w));
    }
  }
  return out;
}

if (import.meta.url === 'file://' + process.argv[1]){
  const outdir = process.argv[2] || '/tmp/stretch-exp'; fs.mkdirSync(outdir, { recursive: true });
  const B = makeBreak(44100, 170, 2), sr = B.sr, L = B.data.length, bpm = 140, beats = B.beats, T = Math.round(beats * 60 / bpm * sr);
  console.log(`source: ${L} samples = ${(L / sr).toFixed(4)} s (${beats} beats at ${B.bpm}); target ${T} samples = ${(T / sr).toFixed(4)} s (${bpm} BPM), ratio ${(T / L).toFixed(4)}`);
  // 1. the page's existing LOOP mode is the 'classic' case of this algorithm: same output?
  const stretchData = pageStretch(), beatS = 60 / bpm;
  for (const grain of [0, 1, 2, 3, 4]){
    const old = stretchData(B.data, sr, L / sr, { mode: 3, bpm, beats, grain });
    const W = Math.max(64, Math.round([1 / 64, 1 / 32, 1 / 16, 1 / 8, 1 / 4][grain] * beatS * sr));
    const mine = oldschool(B.data, sr, L, T, { g: W, r: 1, x: Math.min(Math.round(sr * 0.0015), W >> 2) / W, ramp: 'fwd', wrap: true });
    let md = 0, bad = 0; for (let i = 0; i < T; i++){ const e = Math.abs(old[i] - mine[i]); if (e > md) md = e; if (e > 1e-6) bad++; }
    console.log(`classic LOOP grain ${grain} (W=${W}): page length ${old.length}, mine ${mine.length}, max |diff| ${md.toExponential(2)}, samples differing ${bad}`);
  }
  // 2. measurable facts
  const stepAt = (o) => { let m = 0, cnt = 0; const g = o.g; for (let n = 1; n < o.out.length; n++){ const dd = Math.abs(o.out[n] - o.out[n - 1]); if (dd > m) m = dd; } return m; };
  const sine = new Float32Array(L); for (let i = 0; i < L; i++) sine[i] = 0.5 * Math.sin(2 * Math.PI * 220 * i / sr);   // a smooth source: any big step in the output is a grain boundary
  const srcStep = 0.5 * 2 * Math.PI * 220 / sr;
  const g = Math.round(0.012 * sr);
  console.log(`\nfacts (sine source 220 Hz, source max step ${srcStep.toFixed(4)}; grain g = ${g} samples = 12 ms):`);
  for (const [x, xmode] of [[0, 'wrap'], [0.05, 'wrap'], [0.5, 'wrap'], [0.05, 'straight'], [0.25, 'straight'], [0.5, 'straight']]){
    const out = oldschool(sine, sr, L, T, { g, r: 1, x, xmode, ramp: 'fwd' });
    let jumps = [], mx = 0; for (let n = 1; n < T; n++){ const dd = Math.abs(out[n] - out[n - 1]); if (dd > mx) mx = dd; if (dd > 4 * srcStep) jumps.push(n); }
    const onGrid = jumps.filter(n => n % g === 0).length;
    console.log(`  x=${x} ${xmode}: length ${out.length} (target ${T}); max step ${mx.toFixed(4)} (${(mx / srcStep).toFixed(1)}x source); steps > 4x source: ${jumps.length}, of them exactly on a grain boundary: ${onGrid}`);
  }
  // 3. period: a boundary every g samples (hard cut, x=0)
  { const out = oldschool(sine, sr, L, T, { g, r: 1, x: 0, ramp: 'fwd' }); const ps = []; for (let n = 1; n < T; n++) if (Math.abs(out[n] - out[n - 1]) > 4 * srcStep) ps.push(n);
    const gaps = new Set(); for (let i = 1; i < ps.length; i++) gaps.add(ps[i] - ps[i - 1]); console.log(`  hard-cut boundaries ${ps.length}, distinct gaps between them: ${[...gaps].join(', ')} (g = ${g}); buzz at ${(sr / g).toFixed(1)} Hz`); }
  // 4. pitch: r = 2 doubles the pitch of a sine and keeps the length
  { const out = oldschool(sine, sr, L, T, { g: Math.round(0.5 * sr), r: 2, x: 0, ramp: 'fwd' }); let z = 0; const a = Math.round(0.05 * sr), b = Math.round(0.4 * sr); for (let n = a + 1; n < b; n++) if (out[n - 1] <= 0 && out[n] > 0) z++;
    console.log(`  r=2: rising zero crossings in 0.35 s = ${z} (${(z / 0.35).toFixed(0)} Hz; a 220 Hz source read at 2x gives 440)`); }
  // 5. wav files
  const variants = {
    'classic-loop-1_32': { g: Math.round(beatS / 32 * sr), x: 0.0015 * sr / Math.round(beatS / 32 * sr), r: 1, ramp: 'fwd' },
    'authentic-12ms-x0': { g: Math.round(0.012 * sr), x: 0, r: 1, ramp: 'fwd' },
    'authentic-30ms-x0-rev': { g: Math.round(0.03 * sr), x: 0, r: 1, ramp: 'rev' },
    'smoothed-30ms-x25': { g: Math.round(0.03 * sr), x: 0.25, xmode: 'straight', r: 1, ramp: 'fwd' },
    'pingpong-1_16': { g: Math.round(beatS / 16 * sr), x: 0, r: 1, ramp: 'pingpong' },
    'pitched-up-5st-12ms': { g: Math.round(0.012 * sr), x: 0, r: Math.pow(2, 5 / 12), ramp: 'fwd' },
  };
  writeWav(path.join(outdir, 'source-break.wav'), B.data, sr);
  for (const [n, o] of Object.entries(variants)){ const out = oldschool(B.data, sr, L, T, o); let pk = 0; for (const v of out) pk = Math.max(pk, Math.abs(v)); writeWav(path.join(outdir, n + '.wav'), out, sr); console.log(`wrote ${n}.wav: ${out.length} samples, peak ${pk.toFixed(3)}`); }
  // 6. cost of the page's stretch today (main thread, synchronous)
  const sec10 = new Float32Array(10 * sr).map((_, i) => B.data[i % L]);
  for (const mode of [2, 3]){ const t = performance.now(); const o = stretchData(sec10, sr, 10, { mode, bpm: 120, beats: 16, grain: 1 }); console.log(`page stretchData mode ${mode}, 10 s mono in -> ${(o.length / sr).toFixed(1)} s out: ${(performance.now() - t).toFixed(0)} ms (node, desktop CPU)`); }
  const t = performance.now(); oldschool(sec10, sr, sec10.length, 8 * sr, { g: Math.round(0.012 * sr), r: Math.pow(2, 5 / 12), x: 0.25, xmode: 'straight', ramp: 'fwd' }); console.log(`oldschool (pitched, linear interp, x=25%) 10 s -> 8 s: ${(performance.now() - t).toFixed(0)} ms`);
}
