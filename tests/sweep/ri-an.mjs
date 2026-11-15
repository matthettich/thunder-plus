// node tests/sweep/ri-an.mjs <fileA.f32> <fileB.f32> [sr=44100] [win=5]: align B to A and print per band per window correlation and residual
import { load, align, compare, fmt } from './ri-lib.mjs';
const [fa, fb, sr = 44100, win = 5, c = 0.6, span = 0.35, fc = 3000] = process.argv.slice(2); const a = load(fa), b = load(fb);
const mono = d => Float32Array.from({ length: d.length / 2 }, (_, i) => (d[2*i] + d[2*i+1]) / 2);
let a2 = a, b2 = b, al = align(mono(a), mono(b), +sr, +span, 16, 3, +c, +fc);
if (al.k < 0) { a2 = b; b2 = a; al = align(mono(b), mono(a), +sr, +span, 16, 3, -al.k / +sr, +fc); console.log('(swapped: B was earlier)'); } console.log(`offset ${al.k} samples (${(al.k / sr * 1000).toFixed(2)} ms) r ${al.r.toFixed(4)} coarse ${al.coarse.toFixed(4)}`);
if (al.k < 0) { console.log('(B is earlier: swap)'); }
console.log(fmt(compare(a2, b2, +sr, al.k, +win)));
