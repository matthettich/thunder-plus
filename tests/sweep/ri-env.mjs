// node tests/sweep/ri-env.mjs <A.f32> <B.f32> <offset> [t0=3] [t1=6] [hop_ms=20] [sr=44100]: aligned envelopes (dB) of A and B side by side, with the best-lag cross-correlation per hop (+-3 ms)
import { load } from './ri-lib.mjs';
const [fa, fb, off, t0 = 3, t1 = 6, hop = 20, sr0 = 44100] = process.argv.slice(2), sr = +sr0, k = +off; const a = load(fa), b = load(fb);
const H = Math.round(+hop / 1000 * sr);
for (let s = Math.round(+t0 * sr); s + H < Math.round(+t1 * sr); s += H) { let ea = 0, eb = 0, sab = 0; for (let i = s; i < s + H; i++) { const x = (a[2*i] + a[2*i+1]) / 2, y = (b[2*(i + k)] + b[2*(i + k) + 1]) / 2; ea += x * x; eb += y * y; sab += x * y; }
  const dA = 10 * Math.log10(ea / H + 1e-20), dB = 10 * Math.log10(eb / H + 1e-20); console.log((s / sr).toFixed(3).padStart(7) + ' A ' + dA.toFixed(1).padStart(6) + ' B ' + dB.toFixed(1).padStart(6) + ' d ' + (dB - dA).toFixed(1).padStart(5) + ' r ' + (sab / Math.sqrt(ea * eb + 1e-30)).toFixed(2)); }
