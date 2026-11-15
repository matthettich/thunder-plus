// Does Chrome's AudioBufferSourceNode (the offline render's note player) low-pass a note that starts between two samples?  The core's sampler (live) starts on a whole sample.
// node tests/sweep/ri-subsample.mjs : white noise through a BufferSource started at whole / half / random sample positions; prints the 8-16 kHz level against the whole-sample start.
import { open } from './seq-lib.mjs';
const A = await open();
const r = await A.page.evaluate(async () => {
  const sr = 44100, N = sr * 2, noise = new Float32Array(sr), seed = (() => { let s = 1; return () => (s = (s * 1664525 + 1013904223) >>> 0) / 4294967296 - 0.5; })(); for (let i = 0; i < noise.length; i++) noise[i] = seed();
  const out = {};
  for (const [name, frac] of [['whole', 0], ['half', 0.5], ['quarter', 0.25], ['0.1', 0.1]]) {
    const oc = new OfflineAudioContext(1, N, sr), b = oc.createBuffer(1, noise.length, sr); b.copyToChannel(noise, 0);
    const s = oc.createBufferSource(); s.buffer = b; s.connect(oc.destination); s.start((1000 + frac) / sr); const d = (await oc.startRendering()).getChannelData(0);
    // energy above 8 kHz via a crude difference of 2-pole high-passed signal: use FFT-free approach: sum of squares of (x[i]-2x[i-1]+x[i-2]) weighting ~ HF
    let hf = 0, tot = 0; for (let i = 3000; i < 40000; i++) { const h = d[i] - 2 * d[i - 1] + d[i - 2]; hf += h * h; tot += d[i] * d[i]; } out[name] = {hf, tot};
  }
  const w = out.whole; for (const k in out) out[k] = {hf_db_vs_whole: +(10 * Math.log10(out[k].hf / w.hf)).toFixed(2), total_db_vs_whole: +(10 * Math.log10(out[k].tot / w.tot)).toFixed(2)};
  return out; });
console.log(JSON.stringify(r)); await A.close();
