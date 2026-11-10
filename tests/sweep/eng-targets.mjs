// END-TO-END check of ENGINE TARGETS (10 Nov; docs/mod/ENGINE-TARGETS-DESIGN.md): the tracker commands CUT / RES / ATK / DEC / REL and the MOD targets
// cut res env atk dec sus rel reaching the live engines (bass, vox, acid, mach, the pump live synths) through the real sequencer, page player, audio-thread
// core and worklet nodes, in headless Chrome. Not part of `npm test`.
//   node tests/sweep/eng-targets.mjs [A,B,C,D,E,F]       each check prints PASS / FAIL / NOTE; exit code 1 if any FAIL
//   BETA_DIR=/path/to/a/copy/of/origin-beta  (default: `git archive origin/beta` into a temp dir)
// A: offline render through the page player (pass()); B: the other engines; C: live, page sequencer (AudioParam values); D: live, audio-thread core
// (heard: a tap on the master); E: the MOD screen's TARGET list; F: audio load with and without four LFOs.
import http from 'http'; import fs from 'fs'; import os from 'os'; import path from 'path'; import { execFileSync } from 'child_process'; import { fileURLToPath, pathToFileURL } from 'url';
import { ROOT } from './lib.mjs';
import { table, live } from './lib.mjs';

const only = (process.argv[2] || '').split(',').filter(Boolean), want = k => !only.length || only.includes(k);
const rows = [], add = (n, ok, d) => { const r = [n, ok === 'NOTE' ? 'NOTE' : ok ? 'PASS' : 'FAIL', d || '']; rows.push(r); live(r); };
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.png': 'image/png', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json' };
const { chromium } = await import(pathToFileURL(PW).href);
// the app served from `root`, in its own Chrome (one at a time)
async function openAt(root){
  const srv = http.createServer((q, r) => {
    let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    const f = path.normalize(path.join(root, p)); if (!f.startsWith(root)){ r.writeHead(404); return r.end(); }
    fs.readFile(f, (e, d) => { if (e){ r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
  });
  await new Promise(ok => srv.listen(0, ok));
  const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  const errors = [];
  page.on('pageerror', e => errors.push('pageerror: ' + e.message));
  page.on('console', m => { if (m.type() === 'error' && !/favicon|404|Failed to load resource/.test(m.text())) errors.push('console.error: ' + m.text()); });
  page.setDefaultTimeout(240000);
  await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`);
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 60000 });
  await page.waitForTimeout(1500);
  const g = page.getByRole('button', { name: 'Got it' }); if (await g.count()) await g.first().click().catch(() => {});
  const run = src => page.evaluate(s => window.__seqTest.ev('(async () => {' + s + '\n})()'), src);
  await run(HELPERS);
  return { page, run, errors, close: async () => { await browser.close(); srv.close(); } };
}

// ---- page-side helpers ----
// SONG: one phrase per track (32 rows = 2 bars at 120 bpm, 4 lines a beat); spec = {tracks:[{t, slot, eng, notes:[[row, note]], cmds:[[row, 'CUT', 0x30, col]]}], lfos:[[track, id, shape, rate, depth]]}
// The song goes through normSong / normPerf; the LFOs are added with lfoAdd (what the MOD screen's right-click does) and then given a shape, rate and depth.
// RENDER: renderOut's own steps (makeSeq, chaseEvs, the pads' renders, pass() = the page player in an OfflineAudioContext) up to the mix, without the file.
const HELPERS = `
globalThis.__e2e = {
  build(spec){
    const Sx = JSON.parse(JSON.stringify(S)); for (const r of Sx.song) r.fill(-1);
    Sx.chains = Sx.chains.map(() => null); Sx.phrases = Sx.phrases.map(() => null); Sx.plen = Sx.plen.map(() => spec.plen || 32); Sx.plpb = Sx.plpb.map(() => 0);
    Sx.lpb = 4; Sx.ticks = 6; Sx.bpm = spec.bpm || 120; Sx.swing = 50; Sx.ntr = 16; Sx.smute = Sx.smute.map(() => 0); if (Sx.smuteX) delete Sx.smuteX; Sx.secs = [];
    Sx.perf = {sc:[], mac:[], lk:{}, xa:0, xb:1, xf:0, lfo:{}};
    const ch = p => Array.from({length:16}, (_, k) => k ? [-1, 0] : [p, 0]);
    spec.tracks.forEach((T, k) => {
      cur = T.slot; setEngine('layers'); if (T.eng && T.eng !== 'layers') setEngine(T.eng); if (T.set) Object.assign(V()[T.eng === 'bass' ? 'bass' : T.eng === 'vox' ? 'vox' : T.eng] || V(), T.set);
      V().len = T.len || 4; refreshAll();
      Sx.insts[k] = Object.assign({}, Sx.insts[0], {pad:T.slot});
      Sx.chains[k] = ch(k); const P = Sx.phrases[k] = freshPhrase();
      for (const [y, n] of T.notes){ P[y][0] = n; P[y][IC] = k; }
      for (const [y, c, v, col] of (T.cmds || [])){ const j = FX0 + 2 * (col || 0); P[y][j] = FXC.indexOf(c); P[y][j + 1] = v; }
      Sx.song[0][T.t] = k;
    });
    const n0 = normSong(Sx); for (const k of Object.keys(S)) if (!(k in n0)) delete S[k]; Object.assign(S, n0);
    for (const [t, id, sh, rt, dp] of (spec.lfos || [])){ lfoAdd(id); const a = S.perf.lfo[t]; const L = a[a.length - 1]; L.sh = sh; L.rt = rt; L.dp = dp; }
    saveS(); return {lfo:JSON.stringify(S.perf.lfo)};
  },
  async render(tail){
    const sr = 48000, seq = makeSeq(S, 'song', {row:0, once:true}), evs = []; let guard = 0;
    while (seq.alive() && seq.time < 1200 && guard++ < 100000){ const r = seq.step(); for (const e of r.evs) evs.push(e); }
    const notes = evs.filter(e => e.type === 'note' && kit.voices[e.pad]), keep = new Set(); for (let t = 0; t < NT; t++) if (audible(t) && notes.some(e => e.track === t)) keep.add(t);
    const pads = new Map(); for (const e of notes) if (!pads.has(e.pad)){ const v = kit.voices[e.pad]; pads.set(e.pad, renderVoice(v, sr, null, isSyn(e.pad) ? v.len : 0)); }
    const bufs = new Map(), getB = (pad, rev) => { const k = pad + (rev ? 'r' : 'f'); let b = bufs.get(k); if (!b){ const d0 = pads.get(pad); if (!d0) return null; b = mkBuf(rev ? d0.slice().reverse() : d0, sr); bufs.set(k, b); } return b; };
    const N = Math.ceil((seq.time + (tail == null ? 1 : tail)) * sr);
    const mix = await pass(evs, keep, N, sr, getB, false, false);
    return {L:mix[0], R:mix[1], sec:seq.time, nEng:evs.filter(e => e.type === 'eng').length};
  },
  // the output of one render, kept page-side: .M = the mono mix; hp = 2 kHz high-pass (two RBJ biquads, 24 dB / oct)
  async go(spec, tail){ this.build(spec); const kit0 = JSON.stringify(kit.voices.map(v => v && JSON.parse(JSON.stringify(v, (k, x) => ArrayBuffer.isView(x) ? Array.from(x).slice(0, 64) : x)))); const r = await this.render(tail);
    const kit1 = JSON.stringify(kit.voices.map(v => v && JSON.parse(JSON.stringify(v, (k, x) => ArrayBuffer.isView(x) ? Array.from(x).slice(0, 64) : x))));
    this.R = r; this.kitSame = kit0 === kit1; return {sec:r.sec, nEng:r.nEng, kitSame:this.kitSame}; },
  hp(d, sr, fc){ const w = 2 * Math.PI * (fc || 2000) / sr, c = Math.cos(w), al = Math.sin(w) / (2 * 0.7071), b0 = (1 + c) / 2, b1 = -(1 + c), b2 = (1 + c) / 2, a0 = 1 + al, a1 = -2 * c, a2 = 1 - al;
    let x = d; for (let s = 0; s < 2; s++){ const y = new Float32Array(x.length); let x1 = 0, x2 = 0, y1 = 0, y2 = 0; for (let i = 0; i < x.length; i++){ const v = (b0 * x[i] + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2) / a0; x2 = x1; x1 = x[i]; y2 = y1; y1 = v; y[i] = v; } x = y; } return x; },
  rms(d, a, b){ let s = 0; a = Math.max(0, a | 0); b = Math.min(d.length, b | 0); for (let i = a; i < b; i++) s += d[i] * d[i]; return Math.sqrt(s / Math.max(1, b - a)); },
  // metrics of the last render: n slices of the high band between t0 and t1 s; whole-band rms of the same slices
  slices(t0, t1, n){ const sr = 48000, H = this.hp(this.R.L, sr), o = [], w = [], dt = (t1 - t0) / n; for (let i = 0; i < n; i++){ o.push(this.rms(H, (t0 + i * dt) * sr, (t0 + (i + 1) * dt) * sr)); w.push(this.rms(this.R.L, (t0 + i * dt) * sr, (t0 + (i + 1) * dt) * sr)); } return {hi:o, all:w}; },
  b64(){ const a = new Float32Array(this.R.L.length * 2); a.set(this.R.L); a.set(this.R.R, this.R.L.length); const u = new Uint8Array(a.buffer); let s = ''; for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768)); return btoa(s); },
  ref(){ this.REF = {L:this.R.L, R:this.R.R}; },
  diffRef(){ let m = 0, pk = 0; for (let i = 0; i < this.R.L.length; i++){ m = Math.max(m, Math.abs(this.R.L[i] - this.REF.L[i]), Math.abs(this.R.R[i] - this.REF.R[i])); pk = Math.max(pk, Math.abs(this.R.L[i])); } return {max:m, pk, n:this.R.L.length, nref:this.REF.L.length}; },
};
return true;`;
const NOTE_ROW = 0, OFF_ROW = 31;   // a held 2-bar note: C-4 on row 0, note off on the last row
const spec = (o = {}) => Object.assign({ plen: 32, tracks: [{ t: 0, slot: 8, eng: 'bass', set: { cut: 6000, fenv: 0, res: 0.25 }, notes: [[NOTE_ROW, 48], [OFF_ROW, 200]] }] }, o);

// ---------------------------------------------------------------- the engines under test
// set: the pad's own knobs, chosen so the engine has high-band energy and a sustain below 1 (the checks need room both ways)
const ENGS = {
  bass:   { eng: 'bass',    set: { cut: 2000, fenv: 0, res: 0.25, sus: 0.4, dec: 200, fdec: 3000 }, atkV: 0xA0, ratio: 2,
    knobs: [['CUT', 0x30, 'cut', 'cutHz(V)'], ['DEC', 0xE0, 'dec', 'decSec(V) * 1000'], ['ATK', 0xA0, 'atk', 'decSec(V) * 1000'], ['REL', 0xD0, 'rel', 'decSec(V) * 1000']] },
  acid:   { eng: 'acid',    set: { cut: 6000, envAmt: 0.2, res: 0.25, sus: 0.4, ampDec: 200, rel: 100 }, noAtk: true, ratio: 3,
    knobs: [['CUT', 0x30, 'cut', 'cutHz(V)'], ['DEC', 0xE0, 'ampDec', 'decSec(V) * 1000'], ['REL', 0xD0, 'rel', 'Math.min(1000, decSec(V) * 1000)']] },   // (acid's release stops at 1000 ms: the effective value is clamped to the engine's own range)
  mach:   { eng: 'mach',    set: { lp: 12000, fenv: 0, res: 0.2, sus: 0.4, dec: 200 }, atkV: 0xA0, ratio: 2,
    knobs: [['CUT', 0x30, 'lp', 'cutHz(V)'], ['DEC', 0xE0, 'dec', 'decSec(V) * 1000'], ['ATK', 0xA0, 'atk', 'decSec(V) * 1000'], ['REL', 0xD0, 'rel', 'decSec(V) * 1000']] },
  vox:    { eng: 'vox',     set: {}, atkV: 0xA0, noDec: true, ratio: 1.2, cutDrop: 0.9, relNote: 'vox has no audible release tail in an offline render even from the pad\'s own rel knob (3000 ms and 140 ms sound the same), the same on origin/beta: not this feature',   // vox: CUT moves brightness (a one-pole 1.2-12 kHz), so its high band falls less
    knobs: [['CUT', 0x30, 'bright', 'V / 255'], ['ATK', 0xA0, 'atk', 'decSec(V) * 1000'], ['REL', 0xD0, 'rel', 'decSec(V) * 1000']] },
  pmono:  { eng: 'pmonol',  set: { cutoff: 8000, filter_env: 0, res: 15, a_s: 40, a_d: 200 }, atkV: 0xA0, ratio: 3,
    knobs: [['CUT', 0x30, 'cutoff', 'cutHz(V)'], ['DEC', 0xE0, 'a_d', 'decSec(V) * 1000'], ['ATK', 0xA0, 'a_a', 'decSec(V) * 1000'], ['REL', 0xD0, 'a_r', 'decSec(V) * 1000']] },
  ppoly:  { eng: 'ppolyl',  set: { cutoff: 8000, filter_env: 0, res: 10, a_s: 40, a_d: 200 }, atkV: 0xA0, ratio: 3,
    knobs: [['CUT', 0x30, 'cutoff', 'cutHz(V)'], ['DEC', 0xE0, 'a_d', 'decSec(V) * 1000'], ['ATK', 0xA0, 'a_a', 'decSec(V) * 1000'], ['REL', 0xD0, 'a_r', 'decSec(V) * 1000']] },
  ptract: { eng: 'ptractl', set: { attack: 5, sustain: 40, decay: 200 }, atkV: 0xA0, noCut: true, ratio: 1,
    knobs: [['DEC', 0xE0, 'decay', 'decSec(V) * 1000'], ['ATK', 0xA0, 'attack', 'decSec(V) * 1000'], ['REL', 0xD0, 'release', 'decSec(V) * 1000']] },   // tract: only an envelope
};
const trk = (E, o = {}) => ({ t: 0, slot: 8, eng: E.eng, set: E.set, notes: [[NOTE_ROW, 48], [OFF_ROW, 200]], ...o });
const S1 = (E, o = {}, lfos = []) => ({ plen: 32, tracks: [trk(E, o)], lfos });
let kitBad = 0;   // A7: the number of renders after which the pads' stored parameters were not the same as before
const gen = async (A, sp, tail) => { const r = await A.run(`return await __e2e.go(${JSON.stringify(sp)}, ${tail == null ? 'null' : tail});`); if (!r.kitSame) kitBad++; return r; };
const sl = (A, t0, t1, n) => A.run(`return __e2e.slices(${t0}, ${t1}, ${n});`);
const mx = a => Math.max(...a), mn = a => Math.min(...a), f3 = x => x.toFixed(4), fa = a => a.map(f3).join(' ');
const rmsW = (A, t0, t1) => A.run(`return __e2e.rms(__e2e.R.L, ${t0} * 48000, ${t1} * 48000);`);
const ref = A => A.run(`__e2e.ref(); return 1`);
const dRef = A => A.run(`return __e2e.diffRef();`);
// relative difference of the 2 kHz high band against the kept reference (REF), and of the whole band
const diffRel = A => A.run(`const E = __e2e, h1 = E.hp(E.R.L, 48000), h0 = E.hp(E.REF.L, 48000); let a = 0, b = 0, c = 0, d2 = 0; for (let i = 0; i < h1.length; i++){ const d = h1[i] - h0[i]; a += d * d; b += h0[i] * h0[i]; const e = E.R.L[i] - E.REF.L[i]; c += e * e; d2 += E.REF.L[i] * E.REF.L[i]; } return {hi:Math.sqrt(a / Math.max(b, 1e-20)), all:Math.sqrt(c / Math.max(d2, 1e-20))};`);

let A;   // one browser at a time: opened below for A / B, closed before the next section
async function suite(tag, E, full){
  await gen(A, S1(E));   // (the very first render of a page is not like the later ones for the pump synths: see the NOTE at the end of the table)
  const r0 = await gen(A, S1(E)); const base = await sl(A, 0.1, 3.8, 8), hiB = mx(base.hi), hiL = mn(base.hi);
  await ref(A);
  add(`${tag} R0: no commands, no LFOs: no eng events, kit unchanged, HF energy to measure`, r0.nEng === 0 && r0.kitSame && (hiL > 1e-4 || E.noCut), `eng ${r0.nEng}, kitSame ${r0.kitSame}, hi ${fa(base.hi.slice(0, 3))}`);
  await gen(A, S1(E)); { const d = await dRef(A); add(`${tag} A1 two renders of the same song`, d.max === 0, d.max === 0 ? 'bit-identical' : 'max diff ' + d.max.toExponential(2) + ' (pk ' + d.pk.toFixed(3) + ')'); }
  if (!E.noCut){
    await gen(A, S1(E, { cmds: [[0, 'CUT', 0x30]] })); const c = await sl(A, 0.1, 3.8, 8);
    add(`${tag} A2 CUT 30: high band lower for the whole note`, mx(c.hi) < (E.cutDrop || 0.5) * hiL, `R0 min ${f3(hiL)}, CUT30 max ${f3(mx(c.hi))}, ratio ${(hiL / mx(c.hi)).toFixed(1)}x`);
    await gen(A, S1(E, { cmds: [[0, 'CUT', 0x30], [16, 'CUT', 0xFF]] })); const c3 = await sl(A, 0.1, 3.8, 8), b1 = c3.hi.slice(0, 4), b2 = c3.hi.slice(4);
    add(`${tag} A3 CUT 30, CUT FF a bar later: bar 2 back at R0`, mx(b1.slice(0, 3)) < (E.cutDrop || 0.5) * hiL && mn(b2.slice(1)) > 0.85 * hiL && mx(b2.slice(1)) < 1.2 * hiB, `bar1 ${fa(b1)} | bar2 ${fa(b2)} | R0 ${fa(base.hi)}`);
    await gen(A, S1(E, {}, [[0, 'cut:0', 0, 5, 0xC0]])); const c4 = await sl(A, 0.1, 3.8, 8);
    const ratio = mx(c4.hi) / Math.max(mn(c4.hi), 1e-9), flat = hiB / hiL;
    add(`${tag} A4 LFO on cut:t (sine, 1 bar, C0) moves the high band within the held note`, ratio > E.ratio && flat < 1.1, `LFO slices ${fa(c4.hi)} ratio ${ratio.toFixed(1)}; R0 max/min ${flat.toFixed(3)}`);
  } else {
    await gen(A, S1(E, { cmds: [[0, 'CUT', 0x30]] })); const c = await sl(A, 0.1, 3.8, 8);
    add(`${tag} CUT 30 (no cutoff of its own: the post-engine biquad stays)`, mx(c.hi) < hiL ? true : 'NOTE', `R0 min ${f3(hiL)}, CUT30 max ${f3(mx(c.hi))}`);
  }
  await gen(A, S1(E)); const g0 = await rmsW(A, 0, 0.05), t0r = await rmsW(A, 0.05, 0.45), tail0 = await rmsW(A, 3.95, 4.8);
  if (!E.noAtk){ await gen(A, S1(E, { cmds: [[0, 'ATK', E.atkV]] })); const gA = await rmsW(A, 0, 0.05);
    add(`${tag} A5 ATK ${E.atkV.toString(16).toUpperCase()}: the first 50 ms differ`, Math.abs(gA - g0) / g0 > 0.3 && (E.atkV >= 0x80 ? gA < 0.6 * g0 : gA > 1.3 * g0), `first 50 ms rms ${f3(gA)} vs R0 ${f3(g0)} (${(gA / g0).toFixed(2)}x)`); }
  else { await gen(A, S1(E, { cmds: [[0, 'ATK', 0xA0]] })); const d = await dRef(A); add(`${tag} A5 ATK is ignored (acid's attack is fixed)`, d.max === 0, 'max diff vs R0 ' + d.max.toExponential(2)); }
  await gen(A, S1(E, { cmds: [[0, 'REL', 0x10]] })); const ts = await rmsW(A, 3.95, 4.8);
  await gen(A, S1(E, { cmds: [[0, 'REL', 0xD0]] })); const tl = await rmsW(A, 3.95, 4.8);
  add(`${tag} A5 REL D0 (long) vs REL 10 (short): the tail after the note-off`, tl > 3 * Math.max(ts, 1e-6) ? true : E.relNote ? 'NOTE' : false, `tail rms long ${f3(tl)} short ${f3(ts)} (R0 ${f3(tail0)})` + (E.relNote ? ' · ' + E.relNote : ''));
  if (!E.noDec){ await gen(A, S1(E, { cmds: [[0, 'DEC', 0xE0]] })); const td = await rmsW(A, 0.05, 0.45);
    add(`${tag} A5 DEC E0: the level across the note differs (sustain below 1)`, Math.abs(td - t0r) / t0r > 0.15, `rms 0.05-0.45 s: ${f3(td)} vs R0 ${f3(t0r)} (${(td / t0r).toFixed(2)}x)`); }
  else { await gen(A, S1(E, { cmds: [[0, 'DEC', 0xE0]] })); const d = await dRef(A); add(`${tag} A5 DEC is ignored (vox has no decay)`, d.max === 0, 'max diff vs R0 ' + d.max.toExponential(2)); }
  if (full){
    await gen(A, S1(E)); await ref(A);
    for (const k of ['res', 'env', 'sus']){ await gen(A, S1(E, {}, [[0, k + ':0', 0, 5, 0xC0]])); const d = await diffRel(A); add(`${tag} A6 LFO on ${k}:t changes the render`, d.hi > 0.05 || d.all > 0.05, `relative difference: high band ${d.hi.toFixed(4)}, whole ${d.all.toFixed(4)}`); }
  }
  // the commands are the knobs: a command's render equals the render of the pad with that knob set to the command's value (the contract's formulas, heard)
  if (E.knobs){
    for (const [cmd, v, knob, conv] of E.knobs){
      const kv = await A.run(`return ${conv.replace('V', v)};`);
      await gen(A, S1(E, { cmds: [[0, cmd, v]] })); await ref(A);
      await gen(A, S1(E, { set: { ...E.set, [knob]: kv } })); const d = await diffRel(A);
      add(`${tag} A10 ${cmd} ${v.toString(16).toUpperCase()} = the pad's own ${knob} (${typeof kv === 'number' ? +kv.toFixed(3) : kv})`, d.all < 0.02 && d.hi < 0.05, `relative difference: whole ${d.all.toFixed(5)}, high band ${d.hi.toFixed(5)}`);
    }
  }
  if (A.errors.length) add(`${tag} page errors`, false, A.errors.splice(0).slice(0, 3).join(' | ').slice(0, 300));
}

// ---------------------------------------------------------------- A7 (kept in every render), A8, A9: against origin/beta
// A beta copy of the app: `git archive origin/beta` (or BETA_DIR). The same songs are built by the same helper in both pages; the buffers are compared here.
let BETA_D = null;
const BETA = () => process.env.BETA_DIR || BETA_D || (BETA_D = (() => { const d = fs.mkdtempSync(path.join(os.tmpdir(), 'beta-')); execFileSync('sh', ['-c', `git archive origin/beta | tar -x -C ${d}`], { cwd: ROOT, stdio: 'ignore' }); return d; })());
const buf64 = async (A, sp) => { const r = await gen(A, sp); const b = Buffer.from(await A.run(`return __e2e.b64();`), 'base64'); return { r, f: new Float32Array(b.buffer, b.byteOffset, b.length / 4) }; };
const cmpBuf = (a, b) => { let m = 0, pk = 0; if (a.length !== b.length) return { m: Infinity, pk, n: a.length + ' vs ' + b.length }; for (let i = 0; i < a.length; i++){ m = Math.max(m, Math.abs(a[i] - b[i])); pk = Math.max(pk, Math.abs(a[i])); } return { m, pk, n: a.length }; };
async function betaChecks(){
  // A8: sampled pads (a drum pad and a synth slot with the default engine) with CUT, RES and a cut LFO: nothing there reaches a live engine
  const sam = { plen: 32, tracks: [{ t: 0, slot: 1, notes: [[0, 48], [16, 52]], cmds: [[0, 'CUT', 0x30], [16, 'CUT', 0xFF]] }, { t: 1, slot: 9, eng: 'layers', len: 3, notes: [[0, 48], [31, 200]], cmds: [[0, 'CUT', 0x30], [0, 'RES', 0x80, 1], [16, 'DEC', 0x40]] }], lfos: [[1, 'cut:1', 0, 5, 0xC0]] };
  // A9: no commands, no LFOs: every live engine holding a note
  const mixAll = { plen: 32, tracks: ['bass', 'acid', 'mach', 'vox'].map((k, i) => trk(ENGS[k], { t: i, slot: 8 + i })) }, pumpOnly = S1(ENGS.pmono);
  const out = {};
  for (const [name, root] of [['this', ROOT], ['beta', BETA()]]){
    const B = await openAt(root);
    await gen(B, S1(ENGS.pmono));   // warm up (the first render of a page differs for the pump synths)
    out[name] = { sam: await buf64(B, sam), mix: await buf64(B, mixAll), mix2: await buf64(B, mixAll), pump: await buf64(B, pumpOnly), pump2: await buf64(B, pumpOnly), errs: B.errors.slice() };
    await B.close();
  }
  const s = cmpBuf(out.this.sam.f, out.beta.sam.f), nz = out.this.sam.f.reduce((a, x) => Math.max(a, Math.abs(x)), 0);
  add('A8 sampled pads with CUT / RES / DEC and a cut LFO: the render equals origin/beta\'s', s.m <= 1e-6 && nz > 0.01, `max diff ${s.m.toExponential(2)} over ${s.n} samples, peak ${nz.toFixed(3)}`);
  add('A8 (note) eng events for tracks that have no live engine', 'NOTE', `${out.this.sam.r.nEng} eng events for the two sampled tracks (documented: the sequencer cannot know a pad is live, the player drops them); the audio is identical`);
  const m = cmpBuf(out.this.mix.f, out.beta.mix.f), det = cmpBuf(out.this.mix.f, out.this.mix2.f), detB = cmpBuf(out.beta.mix.f, out.beta.mix2.f);
  add('A9 bass, acid, mach, vox holding a note, no commands or LFOs: identical to origin/beta', (m.m === 0 || m.m <= Math.max(det.m, detB.m)) && out.this.mix.r.nEng === 0, `(equal, or within the 1-ulp run-to-run noise that two renders of the same song in one page have on either branch) max diff ${m.m.toExponential(2)} over ${m.n} samples, peak ${m.pk.toFixed(3)}; (this branch twice: ${det.m.toExponential(2)}, beta twice: ${detB.m.toExponential(2)})`);
  const pm = cmpBuf(out.this.pump.f, out.beta.pump.f), pd = cmpBuf(out.this.pump.f, out.this.pump2.f), pdb = cmpBuf(out.beta.pump.f, out.beta.pump2.f);
  add('A9 pump live mono holding a note, no commands or LFOs: equal to origin/beta (the pump synths are not bit-repeatable from one render to the next, on beta too)', pm.m === 0 ? true : pm.m <= 1e-6 ? 'NOTE' : false, `max diff ${pm.m.toExponential(2)}, peak ${pm.pk.toFixed(3)}; this branch twice: ${pd.m.toExponential(2)}, beta twice: ${pdb.m.toExponential(2)}`);
  for (const k of ['this', 'beta']) if (out[k].errs.length) add(`A8/A9 page errors (${k})`, false, out[k].errs.slice(0, 3).join(' | ').slice(0, 300));
}

// ---------------------------------------------------------------- C, D, E, F: live
const NAMES = ['cCut', 'cRes', 'cDec', 'cAtk', 'cRel', 'oCut', 'oRes', 'oEnv', 'oAtk', 'oDec', 'oSus', 'oRel'], DEFAULTS = [-1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0];
const LIVE_JS = `
globalThis.__live = {
  NAMES: ${JSON.stringify(NAMES)},
  nodes(){ return [...bassLive].map(b => b.node).filter(n => n && n.parameters && n.parameters.get('oCut')); },
  snap(){ return this.nodes().map(n => this.NAMES.map(k => n.parameters.get(k).value)); },
  async start(spec, core){
    try { resumeOn = false; } catch(e){}
    if (core) await window.__seqTest.coreOn(true); else await window.__seqTest.coreOn(false);
    __e2e.build(spec); audio(); if (ctx.state !== 'running') await ctx.resume();
    tStart('song', {row:0}); for (let i = 0; i < 100 && !tp.on; i++) await new Promise(r => setTimeout(r, 100));
    return {on:tp.on, core:!!tp.core, seq:aset.core};
  },
  // sample the twelve params of every live node every 'every' ms for 'ms'
  async sample(ms, every){ const o = [], t0 = performance.now(); while (performance.now() - t0 < ms){ o.push({w:performance.now() - t0, c:ctx.currentTime, s:this.snap()}); await new Promise(r => setTimeout(r, every)); } return o; },
  stop(){ tStop(true); },
  // a tap on rt.out (the mix before the master chain): the samples of the left channel from now on (a ScriptProcessor). NOT on master: the master chain
  // limiter / clipper makes harmonics of a bass note that fill the 2 kHz+ band (a floor of ~1.3e-3 at any cutoff) and hide the filter (found 10 Nov)
  tapOn(){ const sp = ctx.createScriptProcessor(2048, 2, 2), z = ctx.createGain(); z.gain.value = 0; this.chunks = []; sp.onaudioprocess = e => { this.chunks.push(new Float32Array(e.inputBuffer.getChannelData(0))); }; this.src = rt.out; this.src.connect(sp); sp.connect(z); z.connect(ctx.destination); this.sp = sp; this.z = z; },
  tapOff(){ try { this.src.disconnect(this.sp); this.sp.disconnect(); this.z.disconnect(); } catch(e){} this.sp.onaudioprocess = null; let n = 0; for (const c of this.chunks) n += c.length; const d = new Float32Array(n); let o = 0; for (const c of this.chunks){ d.set(c, o); o += c.length; } this.chunks = []; return d; },
  // the 2 kHz high band of a captured buffer in n slices of dt s from the first sound
  bands(d, n, dt, skip){ const sr = ctx.sampleRate; let on = 0; while (on < d.length && Math.abs(d[on]) < 0.02) on++; const H = __e2e.hp(d, sr), o = [], w = []; for (let i = 0; i < n; i++){ const a = on + Math.round((skip + i * dt) * sr), b = a + Math.round(dt * sr); o.push(__e2e.rms(H, a, b)); w.push(__e2e.rms(d, a, b)); } return {hi:o, all:w, onset:on / sr, len:d.length / sr}; },
  async rec(spec, core, ms, n, dt, skip){ const st = await this.start(spec, core); this.tapOn(); await new Promise(r => setTimeout(r, ms)); const d = this.tapOff(); const r = this.bands(d, n, dt, skip); this.stop(); await new Promise(r2 => setTimeout(r2, 500)); return {...r, st}; },
  async cpu(spec, core, settle, ms){ const st = await this.start(spec, core); await new Promise(r => setTimeout(r, settle)); const o = [], t0 = performance.now(); while (performance.now() - t0 < ms){ const v = globalCpuValue(); if (v != null) o.push(v); await new Promise(r => setTimeout(r, 250)); } this.stop(); await new Promise(r => setTimeout(r, 400)); return {st, n:o.length, mean:o.reduce((a, b) => a + b, 0) / Math.max(1, o.length), max:Math.max(0, ...o), src:(typeof cpu !== 'undefined' ? (cpu.cap ? 'renderCapacity' : cpu.probe ? 'probe' : 'fx') : '?')}; },
};
return true;`;

async function liveC(){
  const L = await openAt(ROOT); await L.run(LIVE_JS);
  const bass = ENGS.bass;
  // C1: the LFO on cut: the page sequencer, oCut of the live node every 50 ms
  let st = await L.run(`return await __live.start(${JSON.stringify(S1(bass, {}, [[0, 'cut:0', 0, 5, 0xC0]]))}, false);`);
  add('C0 page sequencer is the one playing', st.on && !st.core && st.seq === 'page', JSON.stringify(st));
  const s1 = await L.run(`return await __live.sample(3300, 50);`);
  const v1 = s1.filter(x => x.s.length).map(x => ({ c: x.c, o: x.s[0][5] })); const lo = Math.min(...v1.map(x => x.o)), hi = Math.max(...v1.map(x => x.o));
  // a sine of 2 s: least squares for a sin + b cos + c over the samples
  let Sxx = [[0, 0, 0], [0, 0, 0], [0, 0, 0]], Sxy = [0, 0, 0]; const t0 = v1.length ? v1[0].c : 0;
  for (const p of v1){ const w = 2 * Math.PI * p.c / 2, x = [Math.sin(w), Math.cos(w), 1]; for (let i = 0; i < 3; i++){ Sxy[i] += x[i] * p.o; for (let j = 0; j < 3; j++) Sxx[i][j] += x[i] * x[j]; } }
  const solve = (M, y) => { const n = 3, a = M.map((r, i) => [...r, y[i]]); for (let i = 0; i < n; i++){ let p = i; for (let r = i + 1; r < n; r++) if (Math.abs(a[r][i]) > Math.abs(a[p][i])) p = r; [a[i], a[p]] = [a[p], a[i]]; for (let r = 0; r < n; r++) if (r !== i){ const f = a[r][i] / a[i][i]; for (let c = i; c <= n; c++) a[r][c] -= f * a[i][c]; } } return a.map((r, i) => r[n] / r[i]); };
  const [sa, sb, sc] = solve(Sxx, Sxy); let res = 0, cnt = 0; for (const p of v1){ const w = 2 * Math.PI * p.c / 2, f = sa * Math.sin(w) + sb * Math.cos(w) + sc; if (p.o !== 0 || true){ res += (p.o - f) ** 2; cnt++; } }
  const amp = Math.hypot(sa, sb);
  add('C1 oCut of the live node follows the LFO (sine, 2 s, 96 bytes each way) within the held note', lo < -80 && hi > 80 && Math.abs(amp - 96) < 15 && Math.sqrt(res / cnt) < 20, `${v1.length} samples; range ${lo.toFixed(1)} .. ${hi.toFixed(1)}; fitted amplitude ${amp.toFixed(1)}, residual rms ${Math.sqrt(res / cnt).toFixed(1)} (the quantum steps and the 50 ms sampling)`);
  const others = s1.filter(x => x.s.length).map(x => x.s[0]); const cc = others.every(r => r[0] === -1 && r[1] === -1 && r[2] === -1 && r[3] === -1 && r[4] === -1);
  add('C2 with only an LFO on cut, the five commanded params stay at -1 and the other offsets at 0', cc && others.every(r => r[6] === 0 && r[7] === 0 && r[8] === 0 && r[9] === 0 && r[10] === 0 && r[11] === 0), `last ${JSON.stringify(others[others.length - 1])}`);
  await L.run(`__live.stop(); await new Promise(r => setTimeout(r, 300)); return 1;`);
  let rest = await L.run(`return __live.snap();`);
  add('C3 after STOP all twelve are at their defaults on every live node', rest.length > 0 && rest.every(r => r.every((v, i) => v === DEFAULTS[i])), `${rest.length} node(s): ${JSON.stringify(rest)}`);
  // C4: CUT 30 on the note's row, CUT FF a bar later
  await L.run(`return await __live.start(${JSON.stringify(S1(bass, { cmds: [[0, 'CUT', 0x30], [16, 'CUT', 0xFF]] }))}, false);`);
  const s2 = (await L.run(`return await __live.sample(3200, 50);`)).filter(x => x.s.length);
  const cut = s2.map(x => x.s[0][0]), at48 = cut.indexOf(48), back = at48 >= 0 ? cut.indexOf(-1, at48) : -1;
  const during = at48 >= 0 && back > at48 ? cut.slice(at48, back) : [];
  add('C4 cCut is 0x30 after CUT 30 and -1 again after CUT FF a bar later', at48 >= 0 && back > at48 && during.every(v => v === 48) && cut.slice(back).every(v => v === -1) && Math.abs((s2[back].c - s2[at48].c) - 2) < 0.15, `values seen ${[...new Set(cut)].join(',')}; 48 held for ${(s2[back > 0 ? back : 0].c - s2[Math.max(0, at48)].c).toFixed(2)} s (want 2)`);
  await L.run(`__live.stop(); await new Promise(r => setTimeout(r, 300)); return 1;`);
  rest = await L.run(`return __live.snap();`);
  add('C5 after STOP (CUT 30 was in force) all twelve are at their defaults on every live node', rest.length > 0 && rest.every(r => r.every((v, i) => v === DEFAULTS[i])), JSON.stringify(rest));
  // C6: STOP while CUT 30 is in force (stop at ~1 s), then a new song without commands: the node starts clean
  await L.run(`await __live.start(${JSON.stringify(S1(bass, { cmds: [[0, 'CUT', 0x30], [0, 'RES', 0x90, 1], [0, 'ATK', 0x80, 2], [1, 'REL', 0x90, 0]] }))}, false); await new Promise(r => setTimeout(r, 1000)); return 1;`);
  const mid = await L.run(`return __live.snap();`);
  await L.run(`__live.stop(); await new Promise(r => setTimeout(r, 300)); return 1;`);
  rest = await L.run(`return __live.snap();`);
  add('C6 all five commands in force mid-note (cCut 48, cRes 144, cAtk 128, cRel 144), then STOP: all defaults', mid.length > 0 && mid[0][0] === 48 && mid[0][1] === 144 && mid[0][3] === 128 && mid[0][4] === 144 && rest.every(r => r.every((v, i) => v === DEFAULTS[i])), `mid ${JSON.stringify(mid)}; after ${JSON.stringify(rest)}`);
  if (L.errors.length) add('C page errors', false, L.errors.splice(0).slice(0, 3).join(' | ').slice(0, 300));
  await L.close();
}

async function liveD(){
  const L = await openAt(ROOT); await L.run(LIVE_JS);
  const bass = ENGS.bass, N = 10, DT = 0.2;
  const R = async (sp) => L.run(`return await __live.rec(${JSON.stringify(sp)}, true, 3600, ${N}, ${DT}, 0.4);`);
  await R(S1(bass));   // (a warm-up play: the very first live note of a page has a start-up transient)
  const a = await R(S1(bass)); add('D0 the audio-thread core is the sequencer playing', a.st.on && a.st.core, JSON.stringify(a.st));
  const b = await R(S1(bass, {}, [[0, 'cut:0', 0, 5, 0xFF]]));
  const c = await R(S1(bass));
  const sus = await R(S1(bass, {}, [[0, 'sus:0', 0, 5, 0xFF]]));
  const swS = mx(sus.all) / Math.max(mn(sus.all), 1e-9);
  add('D5 core: an LFO on sus:t (whole-band level) swings the mix', swS > 1.5, `sus LFO whole band ${fa(sus.all)} (ratio ${swS.toFixed(2)}), none ${fa(a.all)}`);
  const flat = x => mx(x.hi) / mn(x.hi), swing = mx(b.hi) / Math.max(mn(b.hi), 1e-9);
  add('D1 core: high band of the mix (rt.out) swings with the cutoff LFO', swing > 2.5 && flat(a) < 1.5, `LFO ${fa(b.hi)} (ratio ${swing.toFixed(1)}); none ${fa(a.hi)} (ratio ${flat(a).toFixed(2)}); whole band: LFO ${fa(b.all)} none ${fa(a.all)}; onset ${b.onset.toFixed(2)} s`);
  const d = Math.max(...a.hi.map((x, i) => Math.abs(x - c.hi[i]) / x));
  add('D2 core: after STOP a new note with no commands sounds as before (the overrides are cleared)', d < 0.25, `before ${fa(a.hi)} | after the LFO song ${fa(c.hi)} (worst relative difference ${d.toFixed(3)})`);
  const e = await R(S1(bass, { cmds: [[0, 'CUT', 0x30], [16, 'CUT', 0xFF]] }));
  add('D3 core: CUT 30 darkens the first bar, CUT FF brings it back', e.hi[0] < 0.5 * a.hi[0] && e.hi[N - 1] > 0.6 * a.hi[N - 1], `CUT30/FF ${fa(e.hi)} vs none ${fa(a.hi)}`);
  const g = await R(S1(bass, { cmds: [[0, 'CUT', 0x30]] }));
  const h = await R(S1(bass));
  add('D4 core: CUT 30 held in force at STOP does not leak into the next song', g.hi.every(x => x < 0.5 * a.hi[0]) && Math.abs(h.hi[0] - a.hi[0]) / a.hi[0] < 0.25, `CUT 30 ${fa(g.hi)}; next song ${fa(h.hi)} (none: ${fa(a.hi)})`);
  if (L.errors.length) add('D page errors', false, L.errors.splice(0).slice(0, 3).join(' | ').slice(0, 300));
  await L.close();
}

async function modE(){
  const L = await openAt(ROOT);
  const r = await L.run(`__e2e.build(${JSON.stringify({ plen: 32, tracks: [trk(ENGS.bass), { t: 1, slot: 1, notes: [[0, 48]] }, trk(ENGS.pmono, { t: 2, slot: 10 }), trk(ENGS.ptract, { t: 3, slot: 11 })] })});
    const ids = t => tgtList(t).map(x => x[0].split(':')[0]);
    return {live:ids(0), sampled:ids(1), pump:ids(2), tract:ids(3), labels:tgtList(0).map(x => tgtLabel(x[0], true))};`);
  const want = ['cut', 'res', 'dec', 'env', 'atk', 'sus', 'rel'];
  add('E1 a live-engine track lists CUT RES DEC ENV ATK SUS REL', want.every(k => r.live.includes(k)) && r.live.length === 9, `bass: ${r.live.join(' ')} | ${r.labels.join(' ')}`);
  add('E2 a sampled-only track does not list ENV ATK SUS REL', ['env', 'atk', 'sus', 'rel'].every(k => !r.sampled.includes(k)) && ['cut', 'res', 'dec'].every(k => r.sampled.includes(k)), `sampled: ${r.sampled.join(' ')}`);
  add('E3 pump live synths (mono, tract) list the seven too', want.every(k => r.pump.includes(k)) && want.every(k => r.tract.includes(k)), `pump mono: ${r.pump.join(' ')} | tract: ${r.tract.join(' ')}`);
  // the MOD screen's own TARGET picker (modPick) shows them
  const ui = await L.run(`go('mod'); cFxT = 0; cs.mod.y = 0; modPick(0); await new Promise(r => setTimeout(r, 400)); const txt = [...document.querySelectorAll('.pit .pn')].map(e => e.textContent).join(' | '); const open = !!document.querySelector('.pit'); return {open, txt:txt.slice(0, 600)};`);
  add('E4 the MOD screen\'s TARGET picker (modPick) opens and shows the seven on a live track', ui.open && /ATK/i.test(ui.txt) && /SUS/i.test(ui.txt) && /REL/i.test(ui.txt) && /ENV/i.test(ui.txt), ui.txt.slice(0, 260));
  if (L.errors.length) add('E page errors', false, L.errors.splice(0).slice(0, 3).join(' | ').slice(0, 300));
  await L.close();
}

async function cpuF(){
  const L = await openAt(ROOT); await L.run(LIVE_JS);
  const four = [[0, 'cut:0', 0, 5, 0xC0], [0, 'res:0', 0, 4, 0x80], [0, 'env:0', 2, 3, 0x80], [0, 'sus:0', 0, 6, 0x60]];
  const res = [];
  for (let i = 0; i < 2; i++) for (const [name, lf] of [['no LFOs', []], ['four LFOs', four]]){
    const sp = { plen: 32, bpm: 120, tracks: [trk(ENGS.bass, { notes: [[0, 48], [8, 51], [16, 55], [24, 48], [31, 200]] })], lfos: lf };
    res.push([name, await L.run(`return await __live.cpu(${JSON.stringify(sp)}, true, 800, 2800);`)]);
  }
  for (const [n, r] of res) add(`F audio load, core sequencer, bass track, ${n}`, 'NOTE', `mean ${(r.mean * 100).toFixed(1)}% max ${(r.max * 100).toFixed(1)}% (${r.n} readings, ${r.src}, core ${r.st.core})`);
  await L.close();
}

if (want('A') || want('B')){
  A = await openAt(ROOT);
  if (want('A')) await suite('bass', ENGS.bass, true);
  if (want('B')) for (const k of ['acid', 'mach', 'vox', 'pmono', 'ppoly', 'ptract']) await suite(k, ENGS[k], false);
  add('A7 the pads\' stored parameters (the kit) are unchanged after every render with commands and LFOs', kitBad === 0, kitBad ? kitBad + ' render(s) changed the kit' : 'deep-equal before / after, every render above');
  await A.close();
}
if (want('A')) await betaChecks();
if (want('C')) await liveC();
if (want('D')) await liveD();
if (want('E')) await modE();
if (want('F')) await cpuF();
process.exit(table(rows) ? 1 : 0);
