// RENDER vs LIVE harness (14 Nov 2026, investigation "renders should sound like playback").
// Captures (a) the app's own offline render (renderOut) and (b) live playback through the core sequencer (a worklet tap on the
// master gain), of the same song from the same row, and compares them (alignment, per band, per window). App code is never changed:
// the song is modified as JSON before loading (solo, drop effect slots, drop LFOs), the render's file writer is replaced in the page.
import fs from 'fs';
import { open } from './seq-lib.mjs';

const window_t0 = st => st.t0;
export const sleep = ms => new Promise(r => setTimeout(r, ms));

// ---- song variants (pure JSON edits) ----
// opts: solo:[tracks], dropFx:['t:slot',...] (slot removed, others shift not: the slot is set to null), dropAllFx:[tracks], noLfo, mfxOff, from, rows
export function variant(proj, o = {}) {
  const j = JSON.parse(proj), s = j.song;
  if (o.solo && o.solo.length) s.mix.forEach((m, i) => { m.solo = o.solo.includes(i); });
  for (const k of (o.dropFx || [])) { const [t, sl] = k.split(':').map(Number); s.tfx[t][sl] = null; }
  for (const t of (o.dropAllFx || [])) s.tfx[t] = s.tfx[t].map(() => null);
  if (o.dropMaster) s.mfx = s.mfx.map(() => null);
  if (o.noLfo) s.perf.lfo = {};
  if (o.lfoOnly) { const keep = {}; for (const k of o.lfoOnly) if (s.perf.lfo[k]) keep[k] = s.perf.lfo[k]; s.perf.lfo = keep; }
  if (o.noMst) s.mst.on = false;
  if (o.noLimiter) s.limiter = false;
  if (o.mastGain != null) s.master = o.mastGain;
  if (o.rows != null) for (let y = (o.from || 0) + o.rows; y < 256; y++) s.song[y] = s.song[y].map(() => -1);
  s.renderFrom = o.from || 0;
  return JSON.stringify(j);
}

// ---- one session: load a variant, then render and / or capture live ----
export async function session(projText, opts = {}) {
  const A = await open(); const { page } = A;
  const run = src => page.evaluate(s => window.__seqTest.ev(s), `(async()=>{${src}\n})()`);
  await page.evaluate(t => window.__seqTest.load(t, 'acid.thunder'), projText);
  await page.waitForTimeout(1500);
  await run(`aset.vol = 100; if (typeof master !== 'undefined' && master) master.gain.value = 1; ex.norm = false; ex.bits = 24; ex.sr = ctx ? ctx.sampleRate : 48000; return 1;`);
  const S = { A, run };
  S.info = () => run(`return {sr: ctx && ctx.sampleRate, exsr: ex.sr, from: S.renderFrom, perf: JSON.stringify(S.perf.lfo)}`);
  S.render = async () => {
    const b64 = await run(`
      const keep = wavN; window.__W = null; wavN = function(chs, sr, bits){ window.__W = {chs: chs.map(c => Float32Array.from(c)), sr}; return new Uint8Array(1); };
      pickOut = async () => ({dl:true}); writeOut = async () => {};
      try { await renderOut(false); } finally { wavN = keep; }
      if (!window.__W) return null;
      const f = window.__W; const n = f.chs[0].length, o = new Float32Array(n * 2); for (let i = 0; i < n; i++){ o[2*i] = f.chs[0][i]; o[2*i+1] = f.chs[1][i]; }
      let s = '', u = new Uint8Array(o.buffer); for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768));
      return {sr: f.sr, n, b: btoa(s)};`);
    if (!b64) throw new Error('render gave nothing');
    return { sr: b64.sr, n: b64.n, data: new Float32Array(Buffer.from(b64.b, 'base64').buffer.slice(0)) };   // interleaved L R
  };
  S.live = async (secs, row = 0) => {
    const ok = await run(`
      const ok = await __seqTest.coreOn(true); if (!ok) return 'core did not load';
      rtInit();
      const src = 'class R extends AudioWorkletProcessor{constructor(){super();this.b=[];this.f0=-1;this.on=true;this.port.onmessage=e=>{if(e.data==="get"){this.port.postMessage({f0:this.f0,b:this.b});this.b=[];}};}process(i){const x=i[0];if(x&&x.length&&this.on){if(this.f0<0)this.f0=currentFrame;const l=x[0],r=x[1]||x[0];const o=new Float32Array(l.length*2);for(let k=0;k<l.length;k++){o[2*k]=l[k];o[2*k+1]=r[k];}this.b.push(o);}return true;}}registerProcessor("ri-rec",R);';
      const url = URL.createObjectURL(new Blob([src], {type:'text/javascript'})); await ctx.audioWorklet.addModule(url);
      const n = new AudioWorkletNode(ctx, 'ri-rec', {numberOfInputs:1, numberOfOutputs:1, outputChannelCount:[2]}); const z = ctx.createGain(); z.gain.value = 0; master.connect(n); n.connect(z); z.connect(ctx.destination);
      tStart('song', {row:${row}}); await new Promise(r => setTimeout(r, 2500)); tStop(); await new Promise(r => setTimeout(r, 2500));   // a first PLAY to warm everything up (the first one stalls the page: the core then skips ahead)
      PB.skips = 0; PB.late = 0; PB.drop = 0;
      window.__rec = n; window.__recBlocks = []; n.port.onmessage = e => { window.__recGot = e.data; };
      await new Promise(r => setTimeout(r, 400));
      window.__w0 = Date.now(); window.__c0 = ctx.currentTime;
      tStart('song', {row:${row}});
      for (let i = 0; i < 600 && !(tp.on && tp.core); i++) await new Promise(r => setTimeout(r, 5));
      window.__t0 = tp.t0; window.__ct = ctx.currentTime;
      return 'ok';`);
    if (ok !== 'ok') throw new Error(ok);
    await page.waitForTimeout(secs * 1000 + 800);
    const st = await run(`return {on: tp.on, core: tp.core, t0: window.__t0, ct0: window.__ct, t0now: tp.t0, sr: ctx.sampleRate, wall: (Date.now()-window.__w0)/1000, clk: ctx.currentTime-window.__c0, late: PB.late, drop: PB.drop, skips: PB.skips, ps: ctx.playbackStats ? JSON.stringify(ctx.playbackStats.toJSON ? ctx.playbackStats.toJSON() : {}) : ''}`);
    await run(`tStop(); return 1;`);
    // fetch the recording in pieces
    const got = await run(`window.__recGot = null; __rec.port.postMessage('get'); for (let i = 0; i < 200 && !window.__recGot; i++) await new Promise(r => setTimeout(r, 50));
      const g = window.__recGot; if (!g) return null; let tot = 0; for (const b of g.b) tot += b.length; const o = new Float32Array(tot); let p = 0; for (const b of g.b){ o.set(b, p); p += b.length; }
      let s = '', u = new Uint8Array(o.buffer); for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768));
      return {f0: g.f0, n: tot/2, b: btoa(s)};`);
    if (!got) throw new Error('no recording');
    return { st, f0: got.f0, n: got.n, k0: Math.round(window_t0(st) * st.sr) - got.f0, data: new Float32Array(Buffer.from(got.b, 'base64').buffer.slice(0)) };
  };
  S.close = () => A.close();
  return S;
}

// ---- analysis ----
const mono = d => { const n = d.length / 2, o = new Float32Array(n); for (let i = 0; i < n; i++) o[i] = (d[2*i] + d[2*i+1]) * 0.5; return o; };
export function biquad(x, type, f, sr, q = 0.7071) {
  const w = 2 * Math.PI * f / sr, c = Math.cos(w), a = Math.sin(w) / (2 * q); let b0, b1, b2;
  if (type === 'lp') { b0 = (1 - c) / 2; b1 = 1 - c; b2 = b0; } else { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = b0; }
  const a0 = 1 + a, a1 = -2 * c / a0, a2 = (1 - a) / a0; b0 /= a0; b1 /= a0; b2 /= a0;
  const y = new Float32Array(x.length); let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
  for (let i = 0; i < x.length; i++) { const v = b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = v; } return y;
}
const lp4 = (x, f, sr) => biquad(biquad(x, 'lp', f, sr), 'lp', f, sr), hp4 = (x, f, sr) => biquad(biquad(x, 'hp', f, sr), 'hp', f, sr);
export const bands = (x, sr) => ({ low: lp4(x, 200, sr), mid: hp4(lp4(x, 2000, sr), 200, sr), high: hp4(x, 2000, sr), full: x });
function corr(a, b, i0, i1) { let sa = 0, sb = 0, sab = 0, ea = 0, eb = 0; for (let i = i0; i < i1; i++) { const x = a[i], y = b[i]; sab += x * y; ea += x * x; eb += y * y; } const d = Math.sqrt(ea * eb); return { r: d > 1e-20 ? sab / d : NaN, ea, eb, g: ea > 0 ? sab / ea : 1 }; }
// offset k such that b[i + k] ~ a[i]. Wide-band (a 3 kHz low-pass, decimated by 8, then refined at full rate), because a lead
// above 150 Hz has nothing in a low-band alignment (that failed for the acid lead).
export function align(a, b, sr, span = 0.35, secs = 16, skip = 3, center = 0.6, fc = 3000) {   // center: where to look (s); the song repeats every row (1.83 s here), so a blind search is ambiguous
  const D = 8, la = lp4(a, fc, sr), lb = lp4(b, fc, sr), i0s = Math.floor(skip * sr / D), m = Math.min(Math.floor(secs * sr / D), Math.floor(la.length / D) - i0s);
  const da = new Float32Array(m), db = new Float32Array(Math.floor(lb.length / D)); for (let i = 0; i < m; i++) da[i] = la[(i + i0s) * D]; for (let i = 0; i < db.length; i++) db[i] = lb[i * D];
  const L = Math.floor(span * sr / D), C = Math.round(center * sr / D); let best = -2, bk = 0;
  for (let k = C - L; k <= C + L; k++) { let s = 0, ea = 0, eb = 0; for (let i = 0; i < m; i++) { const j = i + i0s + k; if (j < 0 || j >= db.length) continue; s += da[i] * db[j]; ea += da[i] * da[i]; eb += db[j] * db[j]; } const r = s / Math.sqrt(ea * eb + 1e-30); if (r > best) { best = r; bk = k; } }
  const c = bk * D; let bb = -2, bf = c; const s0 = skip * sr | 0, n2 = Math.min(a.length - s0, 8 * sr | 0);
  for (let k = c - 2 * D; k <= c + 2 * D; k++) { let s = 0, ea = 0, eb = 0; for (let i = 0; i < n2; i++) { const j = i + s0 + k; if (j < 0 || j >= b.length) continue; s += a[i + s0] * b[j]; ea += a[i + s0] ** 2; eb += b[j] ** 2; } const r = s / Math.sqrt(ea * eb + 1e-30); if (r > bb) { bb = r; bf = k; } }
  return { k: bf, r: bb, coarse: best };
}
// compare two interleaved stereo takes (b is shifted by k relative to a); per window per band: correlation and residual (dB, after the best gain)
export function compare(A, B, sr, k, win = 5) {
  const a = mono(A), b = mono(B), n = Math.min(a.length, b.length - k) - 1; const ba = bands(a, sr), bb = bands(b, sr), W = Math.floor(win * sr), rows = [];
  for (let s = 0; s + W <= n; s += W) { const row = { t: s / sr }; for (const name of ['low', 'mid', 'high', 'full']) { const c = corr(ba[name], bb[name].subarray(k), s, s + W); // b shifted
        const bs = bb[name]; let sab = 0, ea = 0, eb = 0; for (let i = s; i < s + W; i++) { const x = ba[name][i], y = bs[i + k]; sab += x * y; ea += x * x; eb += y * y; } const g = ea > 0 ? sab / ea : 1;
        let er = 0; for (let i = s; i < s + W; i++) { const d = bs[i + k] - g * ba[name][i]; er += d * d; }
        row[name] = { r: +(sab / Math.sqrt(ea * eb + 1e-30)).toFixed(3), res: +(10 * Math.log10((er + 1e-30) / (ea + 1e-30))).toFixed(1), rmsA: +(Math.sqrt(ea / W)).toExponential(2), rmsB: +(Math.sqrt(eb / W)).toExponential(2) }; } rows.push(row); }
  return rows;
}
export const fmt = rows => rows.map(r => `${String(r.t).padStart(5)}s  ` + ['low', 'mid', 'high', 'full'].map(n => `${n} r=${r[n].r.toFixed(3)} res=${String(r[n].res).padStart(6)}dB`).join('  |  ')).join('\n');
export const save = (p, d) => fs.writeFileSync(p, Buffer.from(d.buffer, d.byteOffset, d.byteLength));
export const load = p => { const b = fs.readFileSync(p); return new Float32Array(b.buffer, b.byteOffset, b.byteLength / 4); };

// Phase-blind comparison: the lag (samples) of the amplitude envelopes' best match near `center`, and the octave-band level difference B-A (dB) over a stretch.
export function envLag(a, b, sr, center, span = 0.3, skip = 2, secs = 8) {
  const D = 32, m = x => Float32Array.from({ length: x.length / 2 }, (_, i) => (x[2*i] + x[2*i+1]) / 2), ma = m(a), mb = m(b);
  const env = x => { const n = Math.floor(x.length / D), e = new Float32Array(n); for (let i = 0; i < n; i++) { let s = 0; for (let k = 0; k < D; k++) s += Math.abs(x[i * D + k]); e[i] = s / D; } return e; };
  const ea = env(ma), eb = env(mb), C = Math.round(center / D), L = Math.round(span * sr / D); let best = -2, bk = 0;
  const i0 = Math.floor(skip * sr / D), i1 = Math.min(ea.length - 50, i0 + Math.floor(secs * sr / D));
  for (let k = C - L; k <= C + L; k++) { let s = 0, x = 0, y = 0; for (let i = i0; i < i1; i++) { const j = i + k; if (j < 0 || j >= eb.length) continue; s += ea[i] * eb[j]; x += ea[i] ** 2; y += eb[j] ** 2; } const r = s / Math.sqrt(x * y + 1e-30); if (r > best) { best = r; bk = k; } }
  return { k: bk * D, r: best };
}
export function octaves(x, sr, i0, n) { const edges = [60, 120, 250, 500, 1000, 2000, 4000, 8000, 16000], o = []; for (let q = 0; q < edges.length - 1; q++) { const y = biquad(biquad(x.subarray(i0, i0 + n), 'hp', edges[q], sr), 'lp', edges[q + 1], sr); let s = 0; for (let i = 0; i < y.length; i++) s += y[i] * y[i]; o.push(10 * Math.log10(s / n + 1e-20)); } return o; }
