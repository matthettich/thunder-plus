// U09: compares a thunder-render WAV (float32 stereo) with a reference render (ref.f32: the left channel's samples, then the right's).
// Prints the largest difference in dB below the reference's peak, where it is, and the stretches of the song above -80 dB.
//   node native/tools/render-diff.mjs <out.wav> <ref.f32> [label]        exit 0: at or below -80 dB; 1: above
import fs from 'node:fs';
const w = fs.readFileSync(process.argv[2]), r = fs.readFileSync(process.argv[3]), label = process.argv[4] || '';
let o = 12, data = -1, n = 0, sr = 48000;
while (o + 8 <= w.length) { const id = w.toString('latin1', o, o + 4), sz = w.readUInt32LE(o + 4); if (id === 'fmt ') sr = w.readUInt32LE(o + 12); if (id === 'data') { data = o + 8; n = sz; break; } o += 8 + sz + (sz & 1); }
if (data < 0) { console.error('render-diff: not a WAV'); process.exit(2); }
const W = new Float32Array(w.buffer.slice(w.byteOffset + data, w.byteOffset + data + n)), R = new Float32Array(r.buffer.slice(r.byteOffset, r.byteOffset + r.length)), N = R.length / 2;
if (W.length / 2 !== N) { console.log(`${label}FAIL: lengths differ (the render ${W.length / 2} frames, the reference ${N})`); process.exit(1); }
let pk = 0; for (let i = 0; i < R.length; i++) { const a = Math.abs(R[i]); if (a > pk) pk = a; }
const thr = pk * 1e-4; let md = 0, at = -1, ch = 0, first = -1, start = -1, last = -1, mx = 0; const reg = [];
for (let i = 0; i < N; i++) { const dl = Math.abs(R[i] - W[i * 2]), dr = Math.abs(R[N + i] - W[i * 2 + 1]), d = Math.max(dl, dr);
  if (d > md) { md = d; at = i; ch = dr > dl ? 1 : 0; }
  if (d > thr) { if (first < 0) first = i; if (start < 0) { start = i; mx = 0; } last = i; if (d > mx) mx = d; } else if (start >= 0 && i - last > sr / 10) { reg.push([start, last, mx]); start = -1; } }
if (start >= 0) reg.push([start, last, mx]);
const dB = v => v === 0 ? '-inf' : (20 * Math.log10(v / pk)).toFixed(1);
console.log(`${label}difference ${dB(md)} dB below the reference's peak (${pk.toFixed(4)}); largest at frame ${at} (${(at / sr).toFixed(4)} s, ${ch ? 'right' : 'left'}: reference ${R[ch * N + at]}, render ${W[at * 2 + ch]})`);
if (first >= 0) { const c = Math.abs(R[first] - W[first * 2]) > thr ? 0 : 1;
  console.log(`  first sample above -80 dB: frame ${first} (${(first / sr).toFixed(5)} s, ${c ? 'right' : 'left'}): reference ${R[c * N + first]}, render ${W[first * 2 + c]}`);
  console.log(`  ${reg.length} stretch(es) above -80 dB:` + reg.slice(0, 16).map(([a, b, m]) => ` ${(a / sr).toFixed(3)}-${(b / sr).toFixed(3)} s (${dB(m)})`).join(',') + (reg.length > 16 ? ', ...' : '')); }
process.exit(md <= thr ? 0 : 1);
