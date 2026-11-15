// node tests/sweep/ri-spec.mjs <A.f32> <B.f32> [sr=44100] [win=4]: phase-blind comparison. Aligns by the amplitude envelope (so a free-running oscillator phase does not matter),
// then per window prints the octave-band level of A and of B in dB and the difference (B - A), plus the envelope correlation.
import { load, bands, biquad } from './ri-lib.mjs';
const [fa, fb, sr0 = 44100, win0 = 4] = process.argv.slice(2), sr = +sr0, win = +win0; const a = load(fa), b = load(fb);
const mono = d => Float32Array.from({ length: d.length / 2 }, (_, i) => (d[2*i] + d[2*i+1]) / 2), ma = mono(a), mb = mono(b);
const D = 64, env = x => { const n = Math.floor(x.length / D), e = new Float32Array(n); for (let i = 0; i < n; i++) { let s = 0; for (let k = 0; k < D; k++) s += Math.abs(x[i * D + k]); e[i] = s / D; } return e; };
const ea = env(ma), eb = env(mb); let best = -2, bk = 0; const C = Math.round(0.6 * sr / D), L = Math.round(0.6 * sr / D);
for (let k = C - L; k <= C + L; k++) { let s = 0, x = 0, y = 0; const n = Math.min(ea.length - 100, 300 * 0 + Math.floor(16 * sr / D)); for (let i = Math.floor(3 * sr / D); i < n; i++) { const j = i + k; if (j < 0 || j >= eb.length) continue; s += ea[i] * eb[j]; x += ea[i] ** 2; y += eb[j] ** 2; } const r = s / Math.sqrt(x * y + 1e-30); if (r > best) { best = r; bk = k; } }
const off = bk * D; console.log(`envelope alignment: offset ${off} samples (${(off / sr * 1000).toFixed(1)} ms), envelope r ${best.toFixed(3)}`);
const edges = [60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000];
function bandE(x, i0, n) { const o = []; for (let q = 0; q < edges.length - 1; q++) { let y = biquad(biquad(x.subarray(i0, i0 + n), 'hp', edges[q], sr), 'lp', edges[q + 1], sr); let s = 0; for (let i = 0; i < y.length; i++) s += y[i] * y[i]; o.push(10 * Math.log10(s / n + 1e-20)); } return o; }
const W = Math.floor(win * sr); console.log('window   ' + edges.slice(0, -1).map((e, i) => (e + '-' + edges[i + 1]).padStart(11)).join('') + '   (dB: A | B-A)');
for (let s = Math.floor(3 * sr); s + W + off < mb.length && s + W < ma.length; s += W) { const A = bandE(ma, s, W), B = bandE(mb, s + off, W);
  console.log(String((s / sr).toFixed(0)).padStart(4) + 's A  ' + A.map(x => x.toFixed(1).padStart(11)).join('') + '\n      B-A' + A.map((x, i) => (B[i] - x).toFixed(1).padStart(11)).join('')); }
