// node tests/sweep/ri-pitch.mjs <A.f32> <B.f32> <offset> [t0=2.5] [t1=6] [hop_ms=10] [sr=44100]: fundamental frequency tracks (zero-crossing period of a 600 Hz low-passed mono signal) of A and B side by side
import { load, biquad } from './ri-lib.mjs';
const [fa, fb, off, t0 = 2.5, t1 = 6, hop = 10, sr0 = 44100] = process.argv.slice(2), sr = +sr0, k = +off; const a = load(fa), b = load(fb);
const m = d => Float32Array.from({ length: d.length / 2 }, (_, i) => (d[2*i] + d[2*i+1]) / 2), lp = x => biquad(biquad(x, 'lp', 600, sr), 'lp', 600, sr), A = lp(m(a)), B = lp(m(b));
function f0(x, s, n) { let c = 0, first = -1, last = -1; for (let i = s + 1; i < s + n; i++) if (x[i - 1] < 0 && x[i] >= 0) { const fr = -x[i - 1] / (x[i] - x[i - 1]); const t = i - 1 + fr; if (first < 0) first = t; last = t; c++; } return c > 1 ? (c - 1) * sr / (last - first) : 0; }
const H = Math.round(+hop / 1000 * sr); let big = 0, n = 0;
for (let s = Math.round(+t0 * sr); s + H < Math.round(+t1 * sr); s += H) { const fA = f0(A, s, H), fB = f0(B, s + k, H); let ea = 0; for (let i = s; i < s + H; i++) ea += A[i] ** 2; if (ea / H < 1e-6) continue; n++; const dc = fA > 0 && fB > 0 ? 1200 * Math.log2(fB / fA) : NaN; if (Math.abs(dc) > 30) big++; console.log((s / sr).toFixed(3) + ' A ' + fA.toFixed(1).padStart(7) + ' B ' + fB.toFixed(1).padStart(7) + ' cents ' + (isNaN(dc) ? '  n/a' : dc.toFixed(0).padStart(5))); }
console.log('hops with |pitch diff| > 30 cents:', big, 'of', n);
