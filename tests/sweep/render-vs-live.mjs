// RENDER vs LIVE regression check ("renders should sound like playback", 14 Nov 2026).
//   node tests/sweep/render-vs-live.mjs [--song=/path/song.thunder] [--from=72] [--rows=4] [--tracks=0,7,9,13 | auto] [--full] [--minutes...]
// For each track (soloed) it plays the song live through the core sequencer (headless Chrome, a worklet tap on the master gain, after a
// warm-up PLAY) and renders the same rows with the app's own renderOut, then compares them PHASE-BLIND (a free-running oscillator or LFO
// phase does not matter, a different level, spectrum or timing does):
//   level     live minus render, dB over the window (PASS within 0.5 dB)
//   spectrum  the same per octave band 60 Hz-16 kHz (PASS within 1.5 dB; 8-16 kHz within 3 dB)
//   timing    the lag of the amplitude envelopes, live against render, in ms, against the expected start of the live take
//             (PASS within 10 ms of it and within 5 ms of the median over the tracks: no track is moved against the others)
//   envelope  correlation of the amplitude envelopes (PASS 0.8 or more)
// Waveform correlation is printed for information (it is low for free-running synth engines and meaningless for a real comparison).
// Needs playwright + Chrome (see README.md). One browser at a time; the machine should be quiet (a loaded CPU gives audio underruns).
import fs from 'fs'; import path from 'path';
import { session, variant, envLag, octaves, bands, align } from './ri-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x === '--' + k || x.startsWith('--' + k + '=')); return a == null ? d : a.includes('=') ? a.slice(k.length + 3) : true; };
const SONG = arg('song', '/home/claude/acid/acid.thunder'), FROM = +arg('from', 72), ROWS = +arg('rows', 4), TR = String(arg('tracks', 'auto')), FULL = !!arg('full', false);
const proj0 = fs.readFileSync(SONG, 'utf8');
let tracks;
if (TR === 'auto') {
  const S = await session(variant(proj0, { from: FROM, rows: ROWS }));
  tracks = await S.run(`const sq = makeSeq(S, 'song', {row:${FROM}, once:true}); const c = {}; let g = 0; while (sq.alive() && g++ < 100000){ const r = sq.step(); for (const e of r.evs) if (e.type === 'note' && e.track < NT) c[e.track] = (c[e.track] || 0) + 1; } return Object.keys(c).filter(t => c[t] >= 3 && audible(+t)).map(Number);`);
  await S.close();
} else tracks = TR.split(',').map(Number);
console.log(`song ${SONG}, rows ${FROM}..${FROM + ROWS - 1}, tracks ${tracks.join(',')}${FULL ? ' + full mix' : ''}`);
const mono = d => Float32Array.from({ length: d.length / 2 }, (_, i) => (d[2*i] + d[2*i+1]) / 2);
const rows = [], lags = [];
async function one(name, opts, tries = 0) {
  const proj = variant(proj0, Object.assign({ from: FROM, rows: ROWS }, opts));
  let S = await session(proj); const R = await S.render(); const dur = await S.run(`const sq = makeSeq(S, 'song', {row:${FROM}, once:true}); let g = 0; while (sq.alive() && g++ < 100000) sq.step(); return sq.time - (sq.base || 0);`); await S.close();
  S = await session(proj); const Lv = await S.live(dur + 1, FROM); await S.close();
  if (Lv.st.skips > 1 && tries < 2) { console.log(`  (${name}: the core skipped ahead ${Lv.st.skips} times: the machine is too busy, playing again)`); return one(name, opts, tries + 1); }
  const sr = R.sr, e = envLag(R.data, Lv.data, sr, Lv.k0, 0.04, 2, dur - 2.5), k = e.k, A = mono(R.data), B = mono(Lv.data), i0 = Math.floor(2 * sr), n = Math.floor((dur - 2.5) * sr);
  const oa = octaves(A, sr, i0, n), ob = octaves(B, sr, i0 + k, n), tot = o => 10 * Math.log10(o.reduce((s, v) => s + 10 ** (v / 10), 0));
  const lvl = tot(ob) - tot(oa), oct = oa.map((v, i) => ob[i] - v), rl = align(A, B, sr, 0.003, 12, 2, k / sr), bd = bands(A, sr), bb = bands(B, sr);
  const cr = nm => { let s = 0, x = 0, y = 0; for (let i = i0; i < i0 + n; i++) { const p = bd[nm][i], q = bb[nm][i + rl.k]; s += p * q; x += p * p; y += q * q; } return s / Math.sqrt(x * y + 1e-30); };
  const lagMs = (k - Lv.k0) / sr * 1000; lags.push(lagMs);
  rows.push({ name, lvl, oct, lagMs, absMs: k / sr * 1000, envR: e.r, wf: [cr('low'), cr('mid'), cr('high')], skips: Lv.st.skips });
}
for (const t of tracks) await one('track ' + t, { solo: [t] });
if (FULL) await one('full mix', {});
const med = lags.slice().sort((a, b) => a - b)[Math.floor(lags.length / 2)];
let bad = 0; console.log('\nname        level dB  worst oct  lag vs expected  lag vs median  env r   waveform r (low/mid/high)   result');
for (const r of rows) {
  const why = []; if (Math.abs(r.lvl) > 0.5) why.push('level'); if (r.oct.some((v, i) => Math.abs(v) > (i === 7 ? 3 : 1.5))) why.push('spectrum'); if (Math.abs(r.lagMs) > 10 || Math.abs(r.lagMs - med) > 5) why.push('timing'); if (r.envR < 0.8) why.push('envelope');
  if (r.skips > 1) why.push('(core skipped: rerun)'); if (why.length) bad++;
  console.log(r.name.padEnd(11) + r.lvl.toFixed(2).padStart(8) + Math.max(...r.oct.map(Math.abs)).toFixed(2).padStart(10) + r.lagMs.toFixed(1).padStart(14) + ' ms' + (r.lagMs - med).toFixed(1).padStart(10) + ' ms' + r.envR.toFixed(3).padStart(8) + '   ' + r.wf.map(v => v.toFixed(2)).join(' / ').padEnd(24) + (why.length ? 'FAIL ' + why.join(', ') : 'PASS'));
}
console.log(`\n${rows.length - bad} PASS, ${bad} FAIL`); process.exit(bad ? 1 : 0);
