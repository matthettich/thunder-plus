// node tests/sweep/ri-track.mjs --solo=9 [--from=72 --rows=6 --drop=.. --nolfo ...]: one render and one live capture (same row), then the PHASE-BLIND comparison:
//   - envelope lag of live against the render near the expected offset (t0*sr - f0): the extra delay of live over render in ms (all tracks should give the same number)
//   - overall level difference and the octave-band level differences (dB) over the aligned stretch (a free-running oscillator or LFO phase does not matter here)
//   - the waveform correlation too (low / mid / high), to show what phase does
import fs from 'fs'; import path from 'path';
import { session, variant, envLag, octaves, bands, align, save } from './ri-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x === '--' + k || x.startsWith('--' + k + '=')); return a == null ? d : a.includes('=') ? a.slice(k.length + 3) : true; };
const list = s => s ? String(s).split(',').filter(Boolean) : [];
const TAG = arg('tag', 'tr' + (arg('solo', 'all'))), OUT = arg('out', '/home/claude/scratch/ri'), FROM = +arg('from', 72), ROWS = +arg('rows', 6), NL = +arg('live', 1);
fs.mkdirSync(OUT, { recursive: true });
const opts = { from: FROM, rows: ROWS, solo: list(arg('solo', '')).map(Number), dropFx: list(arg('drop', '')), dropAllFx: list(arg('dropAll', '')).map(Number), noLfo: !!arg('nolfo', false), dropMaster: !!arg('dropmaster', false), noLimiter: !!arg('nolimiter', false), noMst: !!arg('nomst', false) };
if (arg('lfoOnly', null)) opts.lfoOnly = list(arg('lfoOnly', ''));
const proj = variant(fs.readFileSync(arg('song', '/home/claude/acid/acid.thunder'), 'utf8'), opts);
let S = await session(proj, { roundStart: !!arg('roundstart', false), roundParams: !!arg('roundparams', false) }); const R = await S.render(); await S.close(); save(path.join(OUT, TAG + '-render.f32'), R.data);
const mono = d => Float32Array.from({ length: d.length / 2 }, (_, i) => (d[2*i] + d[2*i+1]) / 2);
const out = [];
for (let li = 0; li < NL; li++) {
  S = await session(proj); const Lv = await S.live(ROWS * 1.832061 + 1, FROM); await S.close(); save(path.join(OUT, `${TAG}-live${li}.f32`), Lv.data); fs.writeFileSync(path.join(OUT, `${TAG}-live${li}.json`), JSON.stringify({ k0: Lv.k0, f0: Lv.f0, st: Lv.st }));
  const sr = R.sr, e = envLag(R.data, Lv.data, sr, Lv.k0, 0.04, 2, ROWS * 1.832061 - 2.5), k = e.k, secs = Math.min(R.n, Lv.n - k) / sr - 3.5;
  const A = mono(R.data), B = mono(Lv.data), n = Math.floor((ROWS * 1.832061 - 2.5) * sr), i0 = Math.floor(2.5 * sr);
  const oa = octaves(A, sr, i0, n), ob = octaves(B, sr, i0 + k, n);
  const rl = align(A, B, sr, 0.003, 12, 2.5, k / sr), bd = bands(A, sr), bb = bands(B, sr);
  const cr = nm => { let s = 0, x = 0, y = 0; for (let i = i0; i < i0 + n; i++) { const p = bd[nm][i], q = bb[nm][i + rl.k]; s += p * q; x += p * p; y += q * q; } return s / Math.sqrt(x * y + 1e-30); };
  const lvl = 10 * Math.log10(ob.reduce((s, v) => s + 10 ** (v / 10), 0) / oa.reduce((s, v) => s + 10 ** (v / 10), 0));
  const line = `${TAG} live${li}: expected k0 ${Lv.k0} (${(Lv.k0 / sr * 1000).toFixed(1)} ms) ; envelope lag ${k} (${(k / sr * 1000).toFixed(1)} ms), extra delay of live over render ${((k - Lv.k0) / sr * 1000).toFixed(2)} ms, env r ${e.r.toFixed(3)} ; level B-A ${lvl.toFixed(2)} dB ; octave B-A [${oa.map((v, i) => (ob[i] - v).toFixed(1)).join(' ')}] ; waveform r low ${cr('low').toFixed(2)} mid ${cr('mid').toFixed(2)} high ${cr('high').toFixed(2)} full ${cr('full').toFixed(2)} ; playbackStats underrun ${(/underrunEvents":(\d+)/.exec(Lv.st.ps) || [])[1]}`;
  console.log(line); out.push(line);
}
fs.writeFileSync(path.join(OUT, TAG + '.txt'), out.join('\n') + '\n');
