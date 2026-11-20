// Shared helpers for the stretch experiments (NOT shipped; see docs/STRETCH-DESIGN.md).
import fs from 'fs'; import path from 'path'; import { fileURLToPath } from 'url';
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

// The page's own stretchData / GRAIN_BEATS, cut out of index.html by name (brace matching), so the experiments
// compare against the REAL shipped text, not a copy that can drift.
export function pageStretch(){
  const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
  const cut = (head) => { const i = html.indexOf(head); if (i < 0) throw new Error('not found: ' + head);
    let d = 0, j = html.indexOf('{', i); const s = i; for (; j < html.length; j++){ if (html[j] === '{') d++; else if (html[j] === '}' && --d === 0) break; } return html.slice(s, j + 1); };
  const gb = html.slice(html.indexOf('const GRAIN_BEATS'), html.indexOf('\n', html.indexOf('const GRAIN_BEATS')));
  const src = gb + '\n' + cut('function stretchData(') + '\nreturn stretchData;';
  return new Function(src)();
}

// Deterministic PRNG so the break is the same every run.
export function rng(seed){ let s = seed >>> 0; return () => { let x = s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; }; }

// A synthetic two-bar break at `bpm` (kick, snare, ghost snares, hats): stands in for a sampled break (the repo ships none).
export function makeBreak(sr = 44100, bpm = 170, bars = 2){
  const beat = 60 / bpm, N = Math.round(bars * 4 * beat * sr), o = new Float32Array(N), R = rng(7);
  const hit = (t, f) => { const s = Math.round(t * sr); f(s); };
  const kick = s => { let ph = 0; for (let i = 0; i < 0.18 * sr && s + i < N; i++){ const t = i / sr, fr = 45 + 90 * Math.exp(-t * 40); ph += 2 * Math.PI * fr / sr; o[s + i] += 0.9 * Math.sin(ph) * Math.exp(-t * 14); } };
  const snare = (s, a = 0.7) => { let lp = 0; for (let i = 0; i < 0.16 * sr && s + i < N; i++){ const t = i / sr, n = R() * 2 - 1; lp += 0.6 * (n - lp); o[s + i] += a * ((n - lp) * 0.8 * Math.exp(-t * 22) + 0.5 * Math.sin(2 * Math.PI * 190 * t) * Math.exp(-t * 30)); } };
  const hat = (s, a = 0.25) => { let p = 0; for (let i = 0; i < 0.05 * sr && s + i < N; i++){ const t = i / sr, n = R() * 2 - 1; o[s + i] += a * (n - p) * Math.exp(-t * 90); p = n; } };
  for (let b = 0; b < bars; b++){ const t0 = b * 4 * beat;
    for (const q of [0, 2.5]) hit(t0 + q * beat, kick); if (b % 2) hit(t0 + 3.5 * beat, kick);
    for (const q of [1, 3]) hit(t0 + q * beat, s => snare(s)); hit(t0 + 3.75 * beat, s => snare(s, 0.25)); hit(t0 + 1.75 * beat, s => snare(s, 0.2));
    for (let h = 0; h < 8; h++) hit(t0 + h * beat / 2, s => hat(s, h % 2 ? 0.15 : 0.28)); }
  let pk = 0; for (const v of o) pk = Math.max(pk, Math.abs(v)); for (let i = 0; i < N; i++) o[i] *= 0.9 / pk;
  return { data: o, sr, bpm, beats: bars * 4 };
}

export function writeWav(file, d, sr){
  const n = d.length, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22);
  b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++) b.writeInt16LE(Math.max(-32768, Math.min(32767, Math.round(d[i] * 32767))), 44 + i * 2);
  fs.writeFileSync(file, b);
}
