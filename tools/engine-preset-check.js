'use strict';
// Measures every factory preset of the pitched engines in plain Node (no browser), with the app's own renderer
// (tools/engine-golden.js), and prints a per-engine table: peak, RMS, loud-window RMS, pitch error, spectral centroid,
// attack, decay shape, harmonicity, and the distance to the nearest OTHER preset of the same engine.
//   node tools/engine-preset-check.js [engine ...]        (default: every pitched engine; -v prints every row, -n only new ones)
// Used by tests/engine-presets.test.js (it imports measure(), engines(), BASELINE). Test-only: nothing in the app changes.
const G = require('./engine-golden.js');
const SR = G.SR;
const E = G.engine();

/* Preset counts before the 14 Nov 2026 "many more presets" pass: the presets with an index at or above these are the new ones. */
const BASELINE = { acid: 28, mach: 17, modal: 23, swarm: 17, drift: 19, piano: 15, ep: 13, organ: 15, table: 18, breath: 13, strings: 17, wind: 17,
  phase: 17, partials: 17, chip: 18, vector: 18, scanned: 17, resynth: 12, bytebeat: 21, bass: 18, vox: 33, tone: 11, blip: 13 };
/* The least each engine must have now (existing + the new minimum). Breath and Machine are partly drum engines: pitched / texture presets only. */
const NEW_MIN = { acid: 20, mach: 10, modal: 12, swarm: 12, drift: 12, piano: 8, ep: 8, organ: 8, table: 14, breath: 8, strings: 10, wind: 10,
  phase: 14, partials: 14, chip: 12, vector: 14, scanned: 14, resynth: 10, bytebeat: 14, bass: 20, vox: 8, tone: 8, blip: 8 };
/* Presets whose pitch is not a clean harmonic note (noise, metal, clusters): the pitch check is skipped for them (listed by name). */
const NO_PITCH = new Set(); // filled by the data below via the 'np' marker file: see PITCH_SKIP
const PITCH_SKIP = require('./engine-preset-skip.json');
const PITCHES = [0, 12, 24];   // pad Tune in semitones: C2, C3, C4 on the pad's own scale (f0 = 65.406 * 2^(oct + tune / 12))

const engines = () => Object.keys(BASELINE);
const presetList = k => k === 'bass' ? E.BASS_PRE : k === 'vox' ? E.VOX_PRE : k === 'blip' ? E.BLIP_PRE : E.XUI[k].pre;

function build(k, i, tune) {
  const name = presetList(k)[i][0];
  const v = E.PD(k + ' ' + name, [{ type: 'sine' }], {}); v.engine = k; v.tune = tune; v.level = E.ENGINE_LEVEL;
  if (k === 'bass') { v.bass = E.normBass(null); E.setCur(v); E.bassPreset(i, true); }
  else if (k === 'blip') { v.blip = E.normBlip(null); E.setCur(v); E.blipPreset(i, true); }
  else if (k === 'vox') { v.vox = E.normVox(null); E.setCur(v); E.voxPreset(i, true); }
  else { v[k] = E.normX(k, null); E.setCur(v); E.xPreset(i, true); }
  return v;
}
function render(k, i, tune) {
  E.setSeed(G.seedFor('chk|' + k + '|' + i));
  const v = build(k, i, tune);
  const d = E.renderVoice(v, SR, null, 1);
  return { v, d };
}
/* ---- analysis ------------------------------------------------------------------------------------------------ */
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0;
      for (let j = 0; j < len / 2; j++) { const a = i + j, b = a + len / 2, xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } }
  }
}
function spectrum(d, start, N) {
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) { const j = start + i; const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / (N - 1)); re[i] = j < d.length ? d[j] * w : 0; }
  fft(re, im); const m = new Float64Array(N / 2); for (let i = 0; i < N / 2; i++) m[i] = Math.hypot(re[i], im[i]); return m;
}
const BIN = N => SR / N;
function analyse(d, f0exp) {
  const n = d.length; let pk = 0, ss = 0, bad = 0;
  for (let i = 0; i < n; i++) { const x = d[i]; if (!Number.isFinite(x)) { bad++; continue; } const a = Math.abs(x); if (a > pk) pk = a; ss += x * x; }
  const rms = Math.sqrt(ss / n);
  // envelope in 10 ms RMS frames
  const fr = Math.round(0.01 * SR), nf = Math.floor(n / fr), env = new Float64Array(nf);
  for (let f = 0; f < nf; f++) { let s = 0; for (let i = f * fr; i < (f + 1) * fr; i++) s += d[i] * d[i]; env[f] = Math.sqrt(s / fr); }
  let emax = 0, imax = 0; for (let f = 0; f < nf; f++) if (env[f] > emax) { emax = env[f]; imax = f; }
  // loud-window RMS: the loudest 300 ms
  const w = Math.min(nf, 30); let best = 0, run = 0; for (let f = 0; f < nf; f++) { run += env[f] * env[f]; if (f >= w) run -= env[f - w] * env[f - w]; if (f >= w - 1 && run > best) best = run; }
  const loud = Math.sqrt(best / Math.min(nf, w) || 0);
  let att = 0; for (let f = 0; f < nf; f++) if (env[f] >= 0.7 * emax) { att = f * 10; break; }   // ms to 70 % of the max envelope
  const dbAt = t => { const f = Math.min(nf - 1, Math.round(t * 100) + imax); return Math.max(-60, 20 * Math.log10(Math.max(env[f], 1e-9) / Math.max(emax, 1e-9))); };
  const shape = [0.1, 0.3, 0.7, 1.5].map(dbAt);
  // spectrum of the loudest second-ish: 0.12 s after the peak frame (or at the peak when it is late)
  const N = 16384, st = Math.max(0, Math.min(Math.max(0, n - N), (imax * fr) + Math.round(0.05 * SR)));
  const m = spectrum(d, st, N), bw = BIN(N);
  let sw = 0, swf = 0, tot = 0; for (let i = 2; i < m.length; i++) { const p = m[i] * m[i]; tot += p; swf += p * i * bw; sw += p; }
  const cen = swf / Math.max(sw, 1e-30);
  // harmonic product spectrum for f0
  let mx0 = 0; for (let i = 2; i < m.length; i++) if (m[i] > mx0) mx0 = m[i]; const eps = mx0 * 1e-4 + 1e-12;
  let bestI = 0, bestV = 0; const lo = Math.max(2, Math.floor(25 / bw)), hi = Math.floor(2500 / bw);
  for (let i = lo; i < hi; i++) { let p = Math.log(m[i] + eps); for (let h = 2; h <= 5; h++) p += Math.log(m[i * h] + eps); if (p > bestV || !bestV) { bestV = p; bestI = i; } }
  const a = Math.log(m[bestI - 1] + eps), b = Math.log(m[bestI] + eps), c = Math.log(m[bestI + 1] + eps), off = (a - c) / (2 * (a - 2 * b + c) || 1);
  let f0 = (bestI + (isFinite(off) ? Math.max(-1, Math.min(1, off)) : 0)) * bw;
  // strongest spectral peak and how far it is from a multiple of f0exp / 2 (a note and its sub-octave sound)
  let pI = 2, pV = 0; for (let i = 2; i < m.length; i++) if (m[i] > pV) { pV = m[i]; pI = i; }
  const pa = Math.log(m[pI - 1] + 1e-12), pb = Math.log(m[pI] + 1e-12), pc = Math.log(m[Math.min(m.length - 1, pI + 1)] + 1e-12), po = (pa - pc) / (2 * (pa - 2 * pb + pc) || 1), fpk = (pI + (isFinite(po) ? po : 0)) * bw;
  const hh = f0exp / 2, nearest = Math.max(1, Math.round(fpk / hh)) * hh, peakDev = 1200 * Math.log2(fpk / nearest);
  // harmonicity: share of spectral energy near integer multiples of the measured f0, above what noise would give (24 %)
  let he = 0; for (const ff of [f0, f0exp, f0exp / 2]) { let e1 = 0; for (let h = 1; h * ff < SR / 2 && h < 200; h++) { const c0 = h * ff / bw, w2 = Math.max(2, 0.12 * ff / bw); for (let i = Math.max(2, Math.floor(c0 - w2)); i <= Math.min(m.length - 1, Math.ceil(c0 + w2)); i++) e1 += m[i] * m[i]; } he = Math.max(he, e1); }
  const harm = Math.max(0, Math.min(1, (he / Math.max(tot, 1e-30) - 0.24) / 0.76));
  // octave-band balance relative to the note (dB re total, 6 bands: below f0, f0..2, 2..4, 4..8, 8..16, 16..32 f0) and even / odd harmonic balance
  const bands = [0, 0, 0, 0, 0, 0]; for (let i = 2; i < m.length; i++) { const r = i * bw / f0exp, b = r < 1 ? 0 : Math.min(5, 1 + Math.floor(Math.log2(r))); bands[b] += m[i] * m[i]; }
  const bdb = bands.map(x => Math.max(-40, 10 * Math.log10(Math.max(x, 1e-30) / Math.max(tot, 1e-30))));
  let ev = 0, od = 0; for (let h = 1; h <= 24; h++) { const c0 = Math.round(h * f0exp / bw); let e = 0; for (let i = Math.max(2, c0 - 2); i <= Math.min(m.length - 1, c0 + 2); i++) e += m[i] * m[i]; if (h % 2) od += e; else ev += e; }
  const evod = Math.max(-20, Math.min(20, 10 * Math.log10((ev + 1e-12 * tot) / (od + 1e-12 * tot))));
  // brightness change: centroid in the early and late window (0.05 s after the peak, 0.6 s later)
  const st2 = Math.max(0, Math.min(Math.max(0, n - N), st + Math.round(0.6 * SR))), m2 = spectrum(d, st2, N);
  let s2 = 0, s2f = 0; for (let i = 2; i < m2.length; i++) { const p = m2[i] * m2[i]; s2 += p; s2f += p * i * bw; }
  const cen2 = s2 > tot * 1e-6 ? s2f / s2 : cen;
  // amplitude movement: std of the envelope in dB over the loud part (wobbles / tremolo / evolving)
  let sd = 0, cnt = 0, mu = 0; const arr = []; for (let f = imax; f < Math.min(nf, imax + 100); f++) if (env[f] > emax * 0.05) arr.push(20 * Math.log10(env[f] / emax));
  for (const x of arr) mu += x; mu /= Math.max(1, arr.length); for (const x of arr) sd += (x - mu) * (x - mu); sd = Math.sqrt(sd / Math.max(1, arr.length));
  const db = x => 20 * Math.log10(Math.max(x, 1e-9));
  return { n, bad, pk, rmsDb: db(rms), loudDb: db(loud), attMs: att, shape, cen, cenRel: Math.log2(Math.max(cen, 20) / f0exp), cenDrift: Math.log2(Math.max(cen2, 20) / Math.max(cen, 20)), f0, peakDev, harm, bdb, evod, am: sd };
}
const expF0 = v => (v.engine === 'tone' || v.engine === 'blip') ? Math.max(20, v[v.engine].pitch) * Math.pow(2, v.tune / 12) : 65.406 * Math.pow(2, ((v[v.engine] && v[v.engine].oct) || 0) + v.tune / 12);
function centsOff(f, fe) { let c = 1200 * Math.log2(f / fe); c -= 1200 * Math.round(c / 1200); return c; }

function measure(k, i) {
  const out = { k, i, name: presetList(k)[i][0], at: [] };
  for (const tune of PITCHES) {
    const { v, d } = render(k, i, tune), fe = expF0(v), a = analyse(d, fe);
    a.tune = tune; a.fe = fe; a.cents = centsOff(a.f0, fe); a.octs = Math.round(Math.log2(a.f0 / fe)); a.pitchErr = Math.min(Math.abs(a.cents), Math.abs(a.peakDev)); out.at.push(a);
  }
  return out;
}
/* feature vector for distinctness (middle pitch): spectral centroid relative to f0 (octaves), attack (log ms), the decay shape (dB at four times),
   harmonicity, brightness drift, amplitude movement. Each is scaled so that "clearly different" is about 1. */
function feat(m) {
  const a = m.at[1];
  return [a.cenRel / 1.2, Math.log2(Math.max(a.attMs, 2) / 2) / 2.2, a.shape[0] / 12, a.shape[1] / 18, a.shape[2] / 25, a.shape[3] / 30, (a.harm - 0.5) / 0.35, a.cenDrift / 0.7, a.am / 4, ...a.bdb.map(x => x / 12), a.evod / 10];
}
const dist = (x, y) => Math.sqrt(x.reduce((s, v, j) => s + (v - y[j]) * (v - y[j]), 0));
function table(k, only) {
  const L = presetList(k), ms = []; for (let i = 1; i < L.length; i++) ms.push(measure(k, i));   // 0 is Init
  const F = ms.map(feat), nn = ms.map((m, a) => { let b = -1, bd = 1e9; ms.forEach((_, c) => { if (c !== a) { const dd = dist(F[a], F[c]); if (dd < bd) { bd = dd; b = c; } } }); return { b, bd }; });
  ms.forEach((m, a) => { m.nn = nn[a].bd; m.nnName = ms[nn[a].b].name; m.isNew = m.i >= BASELINE[k]; });
  return ms;
}
module.exports = { BASELINE, NEW_MIN, PITCH_SKIP, PITCHES, engines, presetList, build, render, measure, table, feat, dist, analyse, E };

if (require.main === module) {
  const args = process.argv.slice(2), V = args.includes('-v'), NEWONLY = args.includes('-n'), ks = args.filter(a => a[0] !== '-'), list = ks.length ? ks : engines();
  for (const k of list) {
    const ms = table(k), base = ms.filter(m => !m.isNew);
    const rng = f => { const xs = base.map(f); return [Math.min(...xs), Math.max(...xs)]; };
    const med = f => { const xs = base.map(f).sort((a, b) => a - b); return xs[Math.floor(xs.length / 2)]; };
    console.log('\n== ' + k + ': ' + base.length + ' existing, ' + ms.filter(m => m.isNew).length + ' new');
    for (let p = 0; p < PITCHES.length; p++) { const r = rng(m => m.at[p].rmsDb), l = rng(m => m.at[p].loudDb), q = rng(m => m.at[p].pk);
      console.log('  tune ' + PITCHES[p] + ': existing RMS ' + r[0].toFixed(1) + '..' + r[1].toFixed(1) + ' dB  loud ' + l[0].toFixed(1) + '..' + l[1].toFixed(1) + '  peak max ' + q[1].toFixed(2)); }
    const nnb = base.map(m => m.nn).sort((x, y) => x - y); console.log('  loud(C3) median ' + med(m => m.at[1].loudDb).toFixed(1) + '; existing nearest-neighbour distance: min ' + nnb[0].toFixed(2) + ' p10 ' + nnb[Math.floor(nnb.length * 0.1)].toFixed(2) + ' median ' + nnb[Math.floor(nnb.length / 2)].toFixed(2));
    console.log('  idx name'.padEnd(34) + ' new  pk   rms(C2/C3/C4)       loud(C3) pitchErr cents att  cenRel harm  nn  (nearest)');
    for (const m of ms) { if (NEWONLY && !m.isNew) continue; if (!V && !NEWONLY && !m.isNew) continue;
      const a = m.at; console.log(('  ' + String(m.i).padStart(2) + ' ' + m.name).padEnd(34) + (m.isNew ? ' N ' : '   ') + Math.max(...a.map(x => x.pk)).toFixed(2) + ' ' + a.map(x => x.rmsDb.toFixed(1).padStart(6)).join(' ') + '  ' + a[1].loudDb.toFixed(1).padStart(6) + '  ' +
        a.map(x => x.pitchErr.toFixed(0)).join(' ').padEnd(20) + String(a[1].attMs).padStart(4) + ' ' + a[1].cenRel.toFixed(1).padStart(5) + ' ' + a[1].harm.toFixed(2) + ' ' + m.nn.toFixed(2) + ' ' + m.nnName.slice(0, 18)); }
  }
}
