// How good is the page's existing STRETCH (Hann grains, 50% overlap, no phase alignment) on a steady tone? NOT shipped.
// A steady 220 Hz sine stretched by 1.5x should stay a steady 220 Hz sine. Measure the output's level ripple (RMS per 5 ms) and its frequency.
import { pageStretch } from './lib.mjs';
const stretchData = pageStretch(), sr = 44100, L = 2 * sr;
const sine = new Float32Array(L).map((_, i) => 0.5 * Math.sin(2 * Math.PI * 220 * i / sr));
console.log('ratio 1.5x (2 s of 220 Hz -> 3 s at 40 BPM-equivalent), STRETCH mode, each GRAIN setting (1/64 .. 1/4 beat at 120 BPM):');
for (let grain = 0; grain < 5; grain++){
  const o = stretchData(sine, sr, L / sr, { mode: 2, bpm: 120, beats: 6, grain });   // 6 beats at 120 = 3 s
  const win = Math.round(0.005 * sr), r = []; for (let a = sr / 2 | 0; a + win < o.length - sr / 2; a += win){ let e = 0; for (let i = a; i < a + win; i++) e += o[i] * o[i]; r.push(Math.sqrt(e / win)); }
  const mn = Math.min(...r), mx = Math.max(...r);
  console.log(`  grain ${grain}: length ${(o.length / sr).toFixed(3)} s, level min/max over 5 ms windows ${mn.toFixed(3)} / ${mx.toFixed(3)} = ${(20 * Math.log10(mn / mx)).toFixed(1)} dB ripple (ideal 0.0)`);
}
