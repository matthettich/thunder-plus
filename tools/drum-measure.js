'use strict';
// Measures drum presets (the new ones, and the factory ones for reference) by rendering them with the app's own renderer
// in Node (tools/engine-golden.js), and checks each against a window for its drum type. Used by tests/drum-presets.test.js;
// `node tools/drum-measure.js` prints the table (add `all` to include the older presets, `json` for JSON).
// Measures: peak and RMS (dBFS), length (to -50 dB under the peak), decay (peak to -30 dB), spectral centroid, the share of
// the energy below 150 Hz and above 4 kHz, the strongest spectral peak after the first 15 ms ("pitch") and spectral flatness.
const fs = require('fs'), path = require('path');
const G = require('./engine-golden.js');
const SR = G.SR;

/* ---- FFT (radix 2, in place) ---- */
function fft(re, im) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) { let b = n >> 1; for (; j & b; b >>= 1) j ^= b; j ^= b; if (i < j) { let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let len = 2; len <= n; len <<= 1) {
    const ang = -2 * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len) { let cr = 1, ci = 0;
      for (let k = 0; k < len / 2; k++) { const a = i + k, b = a + len / 2, xr = re[b] * cr - im[b] * ci, xi = re[b] * ci + im[b] * cr;
        re[b] = re[a] - xr; im[b] = im[a] - xi; re[a] += xr; im[a] += xi; const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t; } }
  }
}
function powerSpectrum(x, from, to) {
  let n = 1; while (n < Math.max(4096, to - from)) n <<= 1; n = Math.min(n, 1 << 17);
  const re = new Float64Array(n), im = new Float64Array(n), m = Math.min(to - from, n);
  for (let i = 0; i < m; i++) re[i] = x[from + i] * (0.5 - 0.5 * Math.cos(2 * Math.PI * (i + 0.5) / m));   // Hann
  fft(re, im);
  const p = new Float64Array(n / 2); for (let k = 0; k < n / 2; k++) p[k] = re[k] * re[k] + im[k] * im[k];
  return { p, hz: SR / n };
}
const db = x => x > 1e-9 ? 20 * Math.log10(x) : -180;

function measure(data) {
  const n = data.length; let pk = 0, ss = 0, bad = 0;
  for (let i = 0; i < n; i++) { const v = data[i]; if (!Number.isFinite(v)) { bad++; continue; } const a = Math.abs(v); if (a > pk) pk = a; ss += v * v; }
  const out = { n, secs: n / SR, bad, peak: db(pk), rms: db(Math.sqrt(ss / Math.max(1, n))) };
  // 5 ms RMS envelope
  const w = Math.round(SR * 0.005), nb = Math.max(1, Math.ceil(n / w)), env = new Float64Array(nb);
  for (let b = 0; b < nb; b++) { let s = 0, c = 0; for (let i = b * w; i < Math.min(n, (b + 1) * w); i++) { s += data[i] * data[i]; c++; } env[b] = Math.sqrt(s / Math.max(1, c)); }
  let eMax = 0, iMax = 0; for (let b = 0; b < nb; b++) if (env[b] > eMax) { eMax = env[b]; iMax = b; }
  let t30 = 0, t50 = 0; for (let b = iMax; b < nb; b++) { if (db(env[b]) > db(eMax) - 30) t30 = (b + 1 - iMax) * 0.005; if (db(env[b]) > db(eMax) - 50) t50 = (b + 1 - iMax) * 0.005; }
  out.t30 = Math.max(0.005, t30); out.t50 = Math.max(0.005, t50);
  // the spectrum of the whole hit
  const S = powerSpectrum(data, 0, n), P = S.p; let tot = 0, cen = 0, lo = 0, hi = 0, lg = 0, cnt = 0;
  for (let k = 1; k < P.length; k++) { const f = k * S.hz; tot += P[k]; cen += P[k] * f; if (f < 150) lo += P[k]; if (f > 4000) hi += P[k]; if (f > 60 && f < 16000) { lg += Math.log(P[k] + 1e-20); cnt++; } }
  out.centroid = tot > 0 ? cen / tot : 0; out.lowFrac = tot > 0 ? lo / tot : 0; out.highFrac = tot > 0 ? hi / tot : 0;
  let am = 0; for (let k = 1; k < P.length; k++) if (k * S.hz >= 60 && k * S.hz <= 16000) am += P[k];
  out.flat = cnt && am > 0 ? Math.exp(lg / cnt) / (am / cnt) : 0;   // 1: noise, 0: a tone
  // "pitch": the strongest bin after the first 15 ms (the strongest of the whole hit when it is shorter)
  const from = Math.min(Math.round(SR * 0.015), Math.max(0, n - 2048)), S2 = powerSpectrum(data, from, Math.min(n, from + Math.round(SR * 0.4)));
  let bm = 0, bk = 1; for (let k = 2; k < S2.p.length; k++) { const f = k * S2.hz; if (f < 25 || f > 12000) continue; if (S2.p[k] > bm) { bm = S2.p[k]; bk = k; } }
  out.pitch = bk * S2.hz;
  // the head: the spectral centroid of the first 40 ms (the click and the pitch sweep show here)
  const S3 = powerSpectrum(data, 0, Math.min(n, Math.round(SR * 0.04))); let t3 = 0, c3 = 0; for (let k = 1; k < S3.p.length; k++) { t3 += S3.p[k]; c3 += S3.p[k] * k * S3.hz; }
  out.head = t3 > 0 ? c3 / t3 : 0;
  return out;
}

/* ---- windows per drum type: [what, min, max] on a measure; the same table the test uses ---- */
// Levels: the factory drums sit between about -11 and -0.3 dBFS peak (engine drums are normalised to -1.9; layer voices were
// set by hand at -1 for kicks and about -7 for hats and cymbals), RMS per type below.
const WIN = {
  'Kicks':                   { secs: [0.04, 8], t30: [0.02, 5], pitch: [28, 260], lowFrac: [0.3, 1], rms: [-30, -4], peak: [-9, -0.5] },
  'Snares':                  { secs: [0.04, 1.5], t30: [0.01, 0.6], rms: [-32, -4.5], peak: [-11, -0.5], centroid: [200, 14000] },
  'Claps & snaps':           { secs: [0.03, 2.5], t30: [0.01, 1.2], rms: [-38, -4.5], peak: [-11, -0.5], centroid: [200, 14000] },
  'Rims & sticks':           { secs: [0.005, 0.8], t30: [0.005, 0.3], rms: [-32, -4.5], peak: [-11, -0.5], centroid: [300, 9000] },
  'Closed hats':             { secs: [0.01, 0.5], t30: [0.005, 0.25], highFrac: [0.4, 1], rms: [-40, -4.5], peak: [-12, -0.5], centroid: [3000, 17000] },
  'Open hats':               { secs: [0.12, 2.5], t30: [0.08, 1.4], highFrac: [0.35, 1], rms: [-40, -4.5], peak: [-12, -0.5], centroid: [3000, 17000] },
  'Cymbals':                 { secs: [0.2, 9], t30: [0.03, 5.5], highFrac: [0.15, 1], rms: [-46, -4.5], peak: [-12, -0.5], centroid: [1500, 16000] },
  'Toms':                    { secs: [0.08, 4], t30: [0.05, 2], pitch: [40, 500], rms: [-28, -10], peak: [-9, -0.5] },
  'Hand drums':              { secs: [0.05, 3], t30: [0.02, 1.2], pitch: [60, 900], rms: [-30, -9], peak: [-9, -0.5] },
  'Shakers & tambourines':   { secs: [0.03, 2.5], t30: [0.01, 1.2], highFrac: [0.3, 1], rms: [-42, -4.5], peak: [-12, -0.5], centroid: [2500, 17000] },
  'Cowbells, blocks & metal':{ secs: [0.02, 5], t30: [0.01, 2.5], rms: [-40, -4], peak: [-12, -0.5], centroid: [300, 14000] },
  'IDM & glitch':            { secs: [0.005, 3], t30: [0.005, 2], rms: [-46, -4], peak: [-14, -0.3] },
};
function violations(type, m, eng) {
  const w = Object.assign({}, WIN[type]), v = [];
  if (eng === 'chip') for (const k of ['centroid', 'highFrac', 'lowFrac', 'pitch']) delete w[k];   // the 8-bit noise channel has its own spectrum
  if (type === 'Cymbals' && eng === 'layers') delete w.t30;
  if (m.bad) v.push('non-finite');
  if (!(m.peak < -0.2)) v.push('clips');
  if (m.peak < -40) v.push('silent');
  for (const k of Object.keys(w)) { const [a, b] = w[k], x = m[k]; if (!(x >= a && x <= b)) v.push(k + ' ' + (typeof x === 'number' ? x.toFixed(2) : x) + ' not in ' + a + '..' + b); }
  return v;
}

/* ---- distance in (centroid, decay, pitch, flatness) ---- */
const lg2 = x => Math.log2(Math.max(1e-6, x));
function dist(a, b) {
  const dc = lg2(a.centroid) - lg2(b.centroid), dd = lg2(a.t30) - lg2(b.t30), dp = lg2(a.pitch) - lg2(b.pitch), df = (a.flat - b.flat) * 2, dh = lg2(a.head) - lg2(b.head);
  return Math.sqrt(dc * dc + (dd * 0.7) ** 2 + (dp * 0.6) ** 2 + df * df + (dh * 0.5) ** 2);
}

/* ---- the lists ---- */
function sheetGroups() { return G.engine().fvp(); }   // [[group, [[name, make]]]]: the drum types come first
function entries() {
  const E = G.engine(), tn = E.newDrumPresets ? E.newDrumPresets() : [], isNew = new Set(tn.map(e => e[0] + '|' + e[1]));
  const out = [];
  for (const [g, list] of sheetGroups()) { if (!E.DRUM_TYPES.includes(g)) continue; for (const [name, make] of list) out.push({ type: g, name, make, isNew: tn.some(e => e[0] === g && (e[1] === name || e[1] + '' === name)) }); }
  return { out, tn };
}
function renderEntry(e) {
  const E = G.engine(); E.setSeed(G.seedFor('voice|' + e.type + '|' + e.name));
  const v = e.make(); return { v, data: E.renderVoice(v, SR, null, 0) };
}
module.exports = { measure, violations, dist, WIN, entries, renderEntry, fft, db };

if (require.main === module) {
  const all = process.argv.includes('all'), { out } = entries(), E = G.engine();
  const rows = []; for (const e of out) { if (!all && !e.isNew) continue; const r = renderEntry(e), m = measure(r.data); rows.push({ e, m, viol: violations(e.type, m, r.v.engine || 'layers'), eng: r.v.engine || 'layers' }); }
  if (process.argv.includes('json')) { console.log(JSON.stringify(rows.map(r => ({ type: r.e.type, name: r.e.name, eng: r.eng, ...r.m, viol: r.viol })))); process.exit(0); }
  const f = (x, d) => (typeof x === 'number' ? x.toFixed(d) : String(x)).padStart(7);
  let last = '';
  for (const r of rows) { if (r.e.type !== last) { last = r.e.type; console.log('\n== ' + last + '\n' + 'name'.padEnd(30) + 'eng'.padEnd(8) + ' peak    rms    secs    t30  centroid lowF  highF  pitch  flat'); }
    console.log(r.e.name.slice(0, 29).padEnd(30) + r.eng.padEnd(8) + f(r.m.peak, 1) + f(r.m.rms, 1) + f(r.m.secs, 2) + f(r.m.t30, 3) + f(r.m.centroid, 0) + f(r.m.lowFrac, 2) + f(r.m.highFrac, 2) + f(r.m.pitch, 0) + f(r.m.flat, 3) + (r.viol.length ? '  ! ' + r.viol.join('; ') : '')); }
  // near-duplicates inside a type (new against everything in the type)
  const TH = Number(process.env.DUP || 0.22); let nd = 0;
  const by = new Map(); for (const r of (all ? rows : [])) { }
  const allRows = new Map(); for (const e of out) { const r = rows.find(x => x.e === e); const m = r ? r.m : measure(renderEntry(e).data); (allRows.get(e.type) || allRows.set(e.type, []).get(e.type)).push({ e, m }); }
  for (const [t, list] of allRows) for (let i = 0; i < list.length; i++) for (let j = 0; j < i; j++) { if (!list[i].e.isNew && !list[j].e.isNew) continue; const d = dist(list[i].m, list[j].m); if (d < TH) { nd++; console.log('NEAR', t, d.toFixed(3), list[i].e.name, '~', list[j].e.name); } }
  console.log('\n' + rows.length + ' presets, ' + rows.filter(r => r.viol.length).length + ' outside a window, ' + nd + ' near pairs');
}
