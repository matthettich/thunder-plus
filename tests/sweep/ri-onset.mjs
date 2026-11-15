// node tests/sweep/ri-onset.mjs <render.f32> <live.f32> [thr=1e-4] [N=3000]: first sound in each take (sample index), then the sample-by-sample difference from there (where do they first part?)
import { load } from './ri-lib.mjs';
const [fa, fb, thr = 1e-4, N = 3000] = process.argv.slice(2); const a = load(fa), b = load(fb);
const on = x => { for (let i = 0; i < x.length / 2; i++) if (Math.abs(x[2*i]) > +thr) return i; return -1; };
const ia = on(a), ib = on(b); console.log('first sample above', thr, ': render', ia, ' live', ib, ' (live - render =', ib - ia, 'samples; live capture started ~0.4 s earlier)');
let first = -1, mx = 0; const rows = []; for (let k = 0; k < +N; k++) { const x = a[2*(ia + k)], y = b[2*(ib + k)], d = Math.abs(x - y); if (d > 1e-6 && first < 0) first = k; mx = Math.max(mx, d); if (k % 250 === 0) rows.push(`k=${k} render ${x.toExponential(3)} live ${y.toExponential(3)} diff ${(y - x).toExponential(2)}`); }
console.log('first |diff| > 1e-6 at k =', first, ' max diff in', N, 'samples', mx.toExponential(2)); console.log(rows.join('\n'));
