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
  // Fundamental = the LOWEST STRONG PARTIAL, checked for a missing fundamental. Spectrum of the loudest frame (plucks and pings are gone 50 ms later).
  // Strong: a local maximum within 24 dB of the biggest peak, above max(20 Hz, f0exp / 8). A harmonic of the note can never pass for the note: the peak
  // that decides is the lowest one, so a lone tone at 1.5 x f0exp reads 702 cents sharp and fails. Missing fundamental: when the lowest peak p has
  // strong peaks at 1.5p, 2.5p and 3.5p (the odd multiples of p / 2, which a harmonic series on p / 2 has and a root + fifth stack does not), the
  // fundamental is p / 2. Whether the answer is acceptable is pitchErr's job: against the note, with only the octaves the preset itself declares.
  const mp = spectrum(d, Math.max(0, Math.min(Math.max(0, n - N), imax * fr)), N);
  let mxp = 0; for (let i = 2; i < mp.length; i++) if (mp[i] > mxp) mxp = mp[i];
  const thr = mxp * Math.pow(10, -24 / 20), loI = Math.max(3, Math.floor(Math.max(20, f0exp / 8) / bw));
  // a lowest peak only counts when it has a harmonic family (2 of its 2nd..5th multiples are strong too: 3rd and 5th suffice for a tube / odd-harmonic sound): a body thump or a rumble does not
  const strongAt0 = ff => { const c0 = Math.round(ff / bw); let e = 0; for (let i = Math.max(2, c0 - 2); i <= Math.min(mp.length - 1, c0 + 2); i++) e = Math.max(e, mp[i]); return e >= thr; };
  let bestI = -1, strI = -1;
  for (let i = loI; i < mp.length - 2; i++) if (mp[i] >= thr && mp[i] >= mp[i - 1] && mp[i] > mp[i + 1]) {
    if (mp[i] === mxp) strI = i;
    const ff = i * bw; if ([2, 3, 4, 5].filter(h => ff * h < SR / 2.2 && strongAt0(ff * h)).length >= 2) { bestI = i; break; } }
  if (bestI < 0) { bestI = strI > 0 ? strI : Math.max(loI, 2); for (let i = loI; i < mp.length - 2; i++) if (mp[i] > mp[bestI]) bestI = i; }
  const a = Math.log(mp[bestI - 1] + 1e-12), b = Math.log(mp[bestI] + 1e-12), c = Math.log(mp[bestI + 1] + 1e-12), off = (a - c) / (2 * (a - 2 * b + c) || 1);
  let f0 = (bestI + (isFinite(off) ? Math.max(-1, Math.min(1, off)) : 0)) * bw;
  const strongAt = ff => { const c0 = Math.round(ff / bw); let e = 0; for (let i = Math.max(2, c0 - 2); i <= Math.min(mp.length - 1, c0 + 2); i++) e = Math.max(e, mp[i]); return e >= thr; };
  if (f0 * 0.5 >= 20 && f0 * 3.5 < SR / 2.2 && strongAt(f0 * 1.5) && strongAt(f0 * 2.5) && strongAt(f0 * 3.5)) f0 *= 0.5;
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
  return { n, bad, pk, rmsDb: db(rms), loudDb: db(loud), attMs: att, shape, cen, cenRel: Math.log2(Math.max(cen, 20) / f0exp), cenDrift: Math.log2(Math.max(cen2, 20) / Math.max(cen, 20)), f0, harm, bdb, evod, am: sd };
}
/* Expected fundamental: Tone / Blip have a pitch in Hz; Vox counts octaves from C4 (VoxCore: 261.626 * 2^(oct + tune / 12)); every other engine from C2 (65.406). */
const expF0 = v => (v.engine === 'tone' || v.engine === 'blip') ? Math.max(20, v[v.engine].pitch) * Math.pow(2, v.tune / 12)
  : (v.engine === 'vox' ? 261.626 : 65.406) * Math.pow(2, ((v[v.engine] && v[v.engine].oct) || 0) + v.tune / 12);
/* Octaves (relative to the played note) a preset declares itself to sit at: 0 always, and for Bass the sub oscillator's octave when Sub is up. */
function declaredOctaves(v) {
  const o = [0], b = v.engine === 'bass' ? v.bass : null;
  if (b && b.sub > 0 && (b.subOct | 0) > 0) o.push(-(b.subOct | 0));
  // Sub oscillator (Acid, Swarm, Drift, Table have a Sub level): it sounds one octave under the note, so with Sub up the fundamental may read an octave down.
  const E0 = v[v.engine] || {};
  if (['acid', 'swarm', 'drift', 'table'].includes(v.engine) && E0.sub >= 0.2) o.push(-1);
  // Drift oscillator B transposed by whole octaves (bSemi = -12 ...): its pitch is declared by the preset.
  if (v.engine === 'drift' && E0.bSemi && E0.bSemi % 12 === 0) o.push(E0.bSemi / 12);
  // Modal bell (material 4): its first mode, the hum, is at 0.5 x the note (ModalCore ratios).
  if (v.engine === 'modal' && Math.round(E0.mat) === 4) o.push(-1);
  // Swarm scatter: with Scatter up some voices sit at the intervals of the chosen "Scatter to" set; the octave ones (+-12, 24) are declared octaves.
  if (v.engine === 'swarm' && E0.scatter > 0) [[12, -12, 24], [7, 12, -5, 19], [4, 7, 12, 16], [3, 7, 12, 15], [5, 7, 12, 17], [1, 2, -1, -2, 3], [12, 19, 24, 28, 31]][Math.max(0, Math.min(6, E0.ivl | 0))].forEach(x => { if (x % 12 === 0 && !o.includes(x / 12)) o.push(x / 12); });
  // Vox Rough: every other glottal cycle is changed (period doubling, vocal fry), so with Rough up the fundamental reads an octave down.
  if (v.engine === 'vox' && E0.rough >= 0.5) o.push(-1);
  // Blip / Tone FM: the carrier is at the pitch and the modulator at fmR x pitch; with fmR = n / 2 or n / 4 (0.5, 1.5, 3.5, ...) the spectrum repeats
  // at pitch / 2 or pitch / 4, i.e. the fundamental sits one or two octaves down. That is declared by the preset's own FM ratio.
  if ((v.engine === 'blip' || v.engine === 'tone') && E0.fm > 0.05 && E0.fmR) for (const q of [2, 4]) if (Math.abs(E0.fmR * q - Math.round(E0.fmR * q)) < 0.02 && Math.round(E0.fmR * q) % 2 === 1 && !o.includes(-Math.log2(q))) o.push(-Math.log2(q));
  // Phase 'Double sine' (wave 3) is a cosine read twice per cycle: with it in the mix the fundamental reads an octave UP, declared by the wave choice.
  if (v.engine === 'phase' && ((Math.round(E0.wave) === 3 && (E0.mix == null ? 0 : E0.mix) < 0.9) || (Math.round(E0.wave2 == null ? 1 : E0.wave2) === 3 && E0.mix > 0.1))) o.push(1);
  // Tine EP: the tone bar always sounds one octave under the tine (TineEPCore: f0 * 0.5), so its fundamental reads an octave down.
  if (v.engine === 'ep') o.push(-1);
  // Organ: each drawbar is a footage (RATIOS 0.5 1.5 1 2 3 4 5 6 8): the lowest octave-related drawbar that is up sets where the fundamental sits.
  if (v.engine === 'organ') { const r = [0.5, 1.5, 1, 2, 3, 4, 5, 6, 8]; r.forEach((x, i) => { if (Number.isInteger(Math.log2(x)) && Math.round(v.organ['d' + (i + 1)] || 0) > 0 && !o.includes(Math.log2(x))) o.push(Math.log2(x)); }); }
  return o;
}
/* cents between the measured fundamental and the note, against the nearest DECLARED octave only: 1.5 x is 702 cents, never 0 */
function centsOff(f, fe, octs) { const raw = 1200 * Math.log2(f / fe); return octs.map(o => raw - 1200 * o).reduce((a, c) => Math.abs(c) < Math.abs(a) ? c : a); }

function measure(k, i) {
  const out = { k, i, name: presetList(k)[i][0], at: [] };
  for (const tune of PITCHES) {
    const { v, d } = render(k, i, tune), fe = expF0(v), a = analyse(d, fe);
    a.tune = tune; a.fe = fe; a.cents = centsOff(a.f0, fe, declaredOctaves(v)); a.octs = Math.round(Math.log2(a.f0 / fe)); a.pitchErr = Math.abs(a.cents); out.at.push(a);
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
/* A preset is off pitch when at least two of the three played pitches miss the note by more than 60 cents: a single miss is a slip of the
   estimator (a body thump, a partial that has decayed), while a wrong note or a wrong octave is wrong at every pitch. */
const pitchBad = m => m.at.filter(a => a.pitchErr > 60).length >= 2;
module.exports = { pitchBad, BASELINE, NEW_MIN, PITCH_SKIP, PITCHES, engines, presetList, build, render, measure, table, feat, dist, analyse, E };

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
