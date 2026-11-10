/* THUNDER CORE: the sequencer on the audio thread (native plan, docs/NATIVE-DESKTOP-PLAN.md, phase 2).
   This file is appended to the declarations tools/make-core.mjs copies out of index.html (makeSeq and what it reads), and
   the whole is core/seq-core.js, loaded with audioWorklet.addModule. Edit this file, then run node tools/make-core.mjs.

   Phase 2b: the core steps the song itself (live, as the page's pump did) and plays every note of a rendered pad on its
   own sampler, at the exact sample, so nothing the page's thread does can make those notes late. Everything else (live
   engines, SoundFonts, MUT / GEN variations, effect and mixer commands, MIDI) goes up to the page with each row, booked
   LEAD ahead as before, and the page's player plays it. A note the core plays is sent up too, marked core:true: the page
   lets its own voices on that track go (mono takeover, chokes) and does the rest of its bookkeeping, without sounding it.

   Messages in (from the page, node.port):
     {op:'load', S, vars}        the song (S) and what the sequencer reads from the page (vtrk, NSLOT, TFX: coreTfx())
     {op:'song', S}              the song changed (an edit): copied into the core's song in place (no longer sent)
     {op:'patch', ops}           what changed since the last sync (the page's coreDiff), applied in place
     {op:'vars', vars}           the page settings again (each PLAY)
     {op:'kit', kv}              per pad: {choke, syn, rel, live, sf} (what decides whether the core can play it)
     {op:'buf', pad, data, sr}   a pad's render (mono), or data:null when it has none
     {op:'play', gen, mode, o, t0, rs, watch}  start: o as makeSeq takes it; rs {ref, row, step}: RESUME (the page played that step); watch: the WATCHDOG (phones)
     {op:'ack'}                  the page read a batch of rows (WATCHDOG)
     {op:'stop', at}  ·  {op:'loop', l}  ·  {op:'bpm', v}  ·  {op:'perf', xf, mac, fill}  ·  {op:'lxp', st}  ·  {op:'lead', s}
     {op:'test', id, mode, max, opt}  run the sequencer at once (sim, seeded random): tools/seq-golden.mjs --core
   Messages out:
     {op:'ready', v}  ·  {op:'rows', rows, time, bpm, tks, alive}  ·  {op:'end'}  ·  {op:'test', id, rows}  ·
     {op:'self', gen, why, t}  the WATCHDOG let go of the song at audio time t (why: 'deaf' or 'clock <ratio>')  ·
     {op:'error', where, message} */

/* RESUME_SPREAD: a RESUME's silent replay up to the step the song stopped on (up to 4096 steps, 260-650 ms at song row 60+) is
   no longer run inside the PLAY message. The page sends {op:'prep'} before its own replay; the core builds the sequencer then
   and replays a millisecond's worth of steps in each block, so the work is done by the time PLAY arrives (what is left is run
   there, as before). The result is the same steps in the same order, so the same sequencer. false: PLAY replays it all, as before. */
const RESUME_SPREAD = true, CHASE_MS = 1;
const CORE_V = 7;   // the message set's version: index.html's CORE_WANT must match (3: patch sync; 4: the mix outputs NT+1..NT+3; 5: FXV, BYP, tempo; 6: SoundFonts; 7: hold / held, the exact STOP)
const coreNum = v => Math.round(v * 1e6) / 1e6;
const corePlain = e => { const o = {}; for (const k of Object.keys(e).sort()){ const v = e[k];
  if (typeof v === 'number') o[k] = coreNum(v); else if (typeof v === 'string' || typeof v === 'boolean' || v === null) o[k] = v;
  else if (Array.isArray(v) && v.every(x => typeof x === 'number')) o[k] = v.map(coreNum); } return o; };
function coreSeeded(f){
  const R = Math.random; let s = 0x2545F491;
  Math.random = () => { let x = s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; };
  try { return f(); } finally { Math.random = R; }
}

// The core's own pad renders: what ensure / ensurePlay give the sequencer (chops) and the sampler.
const coreBufs = new Map();   // pad → {data, sr}
ensurePlay = pad => coreBufs.get(pad) || null;
ensure = pad => coreBufs.get(pad) || null;

/* AUTOMATION: the part of AudioParam the page's player uses (setValueAtTime, linearRampToValueAtTime, setTargetAtTime,
   cancelScheduledValues), evaluated per sample. A curve (vibrato) is a function of time over [t, t + d]. */
class Auto {
  constructor(v){ this.v0 = v; this.ev = []; }
  ins(e){ const A = this.ev; let i = A.length; while (i > 0 && A[i - 1].t > e.t) i--; A.splice(i, 0, e); }
  set(v, t){ this.ins({k:0, t, v}); }
  lin(v, t){ this.ins({k:1, t, v}); }
  target(v, t, tc){ this.ins({k:2, t, v, tc:Math.max(1e-5, tc)}); }
  curve(fn, t, d){ this.ins({k:3, t, d, fn}); }
  cancel(t){ this.ev = this.ev.filter(e => e.t < t); }
  // The value from time t on, when nothing moves it any more (a constant), else null: the sampler's fast path
  constFrom(t){ const A = this.ev; if (!A.length) return this.v0; const e = A[A.length - 1]; return e.t <= t && (e.k === 0 || e.k === 1) ? e.v : null; }
  hold(t){ const v = this.at(t); this.cancel(t); this.set(v, t); }
  // Drop what is over by time t (the beat clock gets an event a step for as long as the song plays)
  // Every block (owner, Fold: it sped up and glitched after playing a while: the lists grew to ~50 events each, and each
  // was walked for every sample): what is over is folded into one value, keeping a fade in progress and a curve running.
  prune(t){
    const A = this.ev; if (!A.length || A[0].t > t) return;
    // No closure and no per-event object here (this runs on the audio thread, per block per timeline): a target in progress
    // lives in scalars: hs (is there one), sf (from), sT (start), so (to), sc (time constant).
    let v = this.v0, hs = false, sf = 0, sT = 0, so = 0, sc = 1, cur = null, i = 0, pt = 0;
    for (; i < A.length; i++){ const e = A[i];
      if (e.k === 1){ if (e.t > t) break; v = e.v; hs = false; pt = e.t; continue; }
      if (e.t > t) break;
      if (e.k === 0){ v = e.v; hs = false; cur = null; pt = e.t; }
      else if (e.k === 2){ sf = hs ? so + (sf - so) * Math.exp(-(e.t - sT) / sc) : v; hs = true; sT = e.t; so = e.v; sc = e.tc; cur = null; }
      else if (e.k === 3){ if (e.t + e.d > t){ cur = e; } else { v = e.fn(e.t + e.d); hs = false; cur = null; pt = e.t + e.d; } } }
    if (i === 0) return;
    // FLX CHECK 2 (5 Nov, owner: FLX stutters / "feels like it lags"): a linear ramp still running (the next event a k:1
    // later than t) starts from the last point's TIME, not t. Folding it to {t, v} restarted the ramp from v at every block,
    // so the beat clock (one 'beat' ramp a row) sat at the row's value at each block start and sawed inside it: Flex,
    // Step gate, Retrigger and every tempo-locked effect on the Audio thread sequencer heard a staircase, and ramps on live
    // engines (level fades, glides) crawled. Keep the point where it was.
    if (!hs && !cur && i < A.length && A[i].k === 1){ this.v0 = v; this.ev = [{k:0, t:pt, v}].concat(A.slice(i)); return; }
    const now = cur ? cur.fn(t) : hs ? so + (sf - so) * Math.exp(-(t - sT) / sc) : v, head = [{k:0, t, v:now}];
    if (hs) head.push({k:2, t, v:so, tc:sc});
    if (cur) head.push(cur);
    this.v0 = now; this.ev = head.concat(A.slice(i));
  }
  at(t){
    // No closure and no per-call object (this runs per sample on the audio thread): a target in progress lives in scalars:
    // hs (is there one), sf (from), sT (start), so (to), sc (time constant). Same arithmetic as before, in the same order.
    let v = this.v0, pt = 0, hs = false, sf = 0, sT = 0, so = 0, sc = 1;
    const A = this.ev;
    for (let q = 0; q < A.length; q++){ const e = A[q];
      if (e.k === 1){ if (t < e.t){ const a = hs ? so + (sf - so) * Math.exp(-(pt - sT) / sc) : v; return e.t > pt ? a + (e.v - a) * (t - pt) / (e.t - pt) : e.v; } v = e.v; hs = false; pt = e.t; continue; }
      if (t < e.t) return hs ? so + (sf - so) * Math.exp(-(t - sT) / sc) : v;
      if (e.k === 0){ v = e.v; hs = false; pt = e.t; }
      else if (e.k === 2){ sf = hs ? so + (sf - so) * Math.exp(-(e.t - sT) / sc) : v; hs = true; sT = e.t; so = e.v; sc = e.tc; pt = e.t; }
      else if (e.k === 3){ if (t < e.t + e.d) return e.fn(t); v = e.fn(e.t + e.d); hs = false; pt = e.t + e.d; }
    }
    return hs ? so + (sf - so) * Math.exp(-(t - sT) / sc) : v;
  }
}

/* LIVE ENGINES (phase 2c): the bass, voice, acid / machine and …pump synth nodes are processors in this same audio-thread
   scope; each registers itself by the page's key (globalThis.__tcNodes, from its processorOptions.cid). The core drives one
   by giving it an override (proc.__ov): Auto timelines for its AudioParams (rate, gate, trig, leg, acc, syl, same, wob, and
   r/g/t/l/v + lane on the …pump synth), read in its process() through __tcOv, from the time the core first set each one;
   and the note's level and pan (the page's GainNode and StereoPanner after the node, which the core can't reach), applied
   to its output by __tcPost (the page sets those nodes to 1 and centre when the core takes the node over). */
// STEREO SAMPLES (5 Nov): the StereoPannerNode's law for a stereo input (Web Audio spec), as [L→L, R→L, L→R, R→R]:
// panned left the right channel folds into the left, panned right the left into the right; centred, each stays put.
// stBal runs per SAMPLE on a stereo pad whose pan or gain is moving: it fills one shared array (no array per call). Callers
// read the four numbers at once ([ll, rl, lr, rr] = stBal(p)) and never keep it. ZERO4: the "no balance" row, never written.
const SBAL = [0, 0, 0, 0], ZERO4 = [0, 0, 0, 0];
function stBal(pan){ const o = SBAL; if (pan <= 0){ const a = (pan + 1) * Math.PI / 2; o[0] = 1; o[1] = Math.cos(a); o[2] = 0; o[3] = Math.sin(a); return o; } const a = pan * Math.PI / 2; o[0] = Math.cos(a); o[1] = 0; o[2] = Math.sin(a); o[3] = 1; return o; }
const tcNodes = () => globalThis.__tcNodes || (globalThis.__tcNodes = new Map());
function tcOver(proc){ return proc.__ov || (proc.__ov = {p:new Map(), buf:new Map(), g:new Auto(1), pn:new Auto(0), until:Infinity}); }
globalThis.__tcOv = (proc, P, n) => {
  const ov = proc.__ov; if (!ov) return P;
  if (currentTime > ov.until){ proc.__ov = null; return P; }   // released (the core stopped a while ago): the page's params again
  const Q = {}, dt = 1 / sampleRate;
  for (const k in P){ const A = ov.p.get(k); if (!A){ Q[k] = P[k]; continue; }
    A.prune(currentTime);
    const src = P[k], b = ov.buf.get(k) || (ov.buf.set(k, new Float32Array(n)), ov.buf.get(k));
    const c = A.from > currentTime + n * dt ? null : A.constFrom(currentTime);
    if (A.from <= currentTime && c != null){ Q[k] = [c]; continue; }
    for (let i = 0; i < n; i++){ const t = currentTime + i * dt; b[i] = t < A.from ? (src.length > 1 ? src[i] : src[0]) : A.at(t); }
    Q[k] = b; }
  return Q;
};
globalThis.__tcPost = (proc, o) => {
  const ov = proc.__ov; if (!ov || !ov.post || !o || !o[0]) return; const L = o[0], R = o[1] || null, n = L.length, dt = 1 / sampleRate;
  ov.g.prune(currentTime); ov.pn.prune(currentTime);
  const gc = ov.g.constFrom(currentTime), pc = ov.pn.constFrom(currentTime);
  for (let i = 0; i < n; i++){
    const t = currentTime + i * dt, g = gc != null ? gc : ov.g.at(t), p = pc != null ? pc : ov.pn.at(t);
    const l = L[i] * g, r = (R ? R[i] : L[i]) * g;
    if (!R){ L[i] = l; continue; }
    // StereoPannerNode, stereo in
    if (p <= 0){ const x = (p + 1) * Math.PI / 2; L[i] = l + r * Math.cos(x); R[i] = r * Math.sin(x); }
    else { const x = p * Math.PI / 2; L[i] = l * Math.cos(x); R[i] = r + l * Math.sin(x); }
  }
};
/* MUT / GEN and CUT / RES / DEC (3 Nov): a note's variation is rendered on the page (or its Worker) as before; once it
   exists the page sends it here as 'vbuf', keyed by instrument, variation and pad (ii:vk:pad). Any change to the
   instruments or a new render of the pad drops the core's copies (the page sends them again as notes ask). CUT / RES /
   DEC shape the sound here with the page's own shapeData. A note whose variation the core doesn't have yet: the page
   plays it, as before. */
const varBufs = new Map(), varShaped = new Map(); let varBytes = 0;   // (varShaped: no longer filled, 7 Nov; Rd shapes as it plays)
/* SOUNDFONTS (3 Nov): a SoundFont pad's preset, as the page has it once its font is loaded and the preset decoded: the
   pad's controls (P), the zones, the sample headers they read, and the decoded samples ({d, sr, ls, le}). The voice is
   the page's sfNote, played here: sfPick / sfVoice (copied from index.html by make-core) choose the zones and their
   numbers, and each zone runs its sample (looped where the font says), its volume envelope, the font's low-pass with its
   mod envelope and its own pan. Until a pad's preset arrives, the page plays its notes. */
/* Rd: a voice's sample read in playback order, without copying it on the audio thread. REVERSE reads it back to front; SHAPED
   (CUT / RES / DEC) runs shapeData's filter and decay sample by sample as the note reaches them (into a ring), from where
   the note starts (with a short warm-up before an offset start), and ends it where shapeData would have cut it (N). */
const RD_RING = 16384, RD_M = RD_RING - 1;
class Rd {
  constructor(src, src2, sr, tw, rev, start){
    this.src = src; this.src2 = src2; this.rev = rev; this.len = src.length; this.N = this.len; this.tw = tw;
    if (!tw) return;
    const tau = tw.dec != null ? decSec(tw.dec) : 0; this.tau = tau; this.N = tau ? Math.min(this.len, Math.ceil(tau * 9.3 * sr)) : this.len;
    this.f = tw.cut != null;
    if (this.f){ const fc = Math.min(cutHz(tw.cut), sr * 0.45), g = Math.tan(Math.PI * fc / sr), k = 2 - 2 * Math.min(clamp(tw.res | 0, 0, 255) / 255 * 0.97, 0.97);
      this.a1 = 1 / (1 + g * (g + k)); this.a2 = g * this.a1; this.a3 = g * this.a2; }
    this.l1 = 0; this.l2 = 0; this.ring = new Float32Array(RD_RING);
    const s0 = Math.max(0, Math.floor(start) - 4096); this.hi = s0; this.lo = s0;   // (the filter settles in the warm-up)
    this.kd = tau ? Math.exp(-1 / (tau * sr)) : 1; this.e = tau ? Math.pow(this.kd, s0) : 1;
  }
  raw(c, i){ const D = c ? this.src2 : this.src; if (!D || i < 0 || i >= this.len) return 0; return D[this.rev ? this.len - 1 - i : i]; }
  at(c, i){
    if (!this.tw) return this.raw(c, i);
    if (c || i < 0 || i >= this.N) return 0;
    while (this.hi <= i){ const n = this.hi; let x = n < this.len ? this.src[n] : 0;
      if (this.f){ const v3 = x - this.l2, v1 = this.a1 * this.l1 + this.a2 * v3, v2 = this.l2 + this.a2 * this.l1 + this.a3 * v3; this.l1 = 2 * v1 - this.l1; this.l2 = 2 * v2 - this.l2; x = v2; }
      if (this.tau){ x *= this.e; this.e *= this.kd; const m = this.N - 1 - n; if (m < 64) x *= m / Math.min(this.N, 64); }
      this.ring[n & RD_M] = x; this.hi = n + 1; }
    return i < this.hi - RD_RING ? 0 : this.ring[i & RD_M];
  }
}
const sfPads = new Map();
function varDrop(pad){ for (const [k, r] of varBufs) if (pad == null || r.pad === pad){ varBufs.delete(k); varBytes -= r.data.length * 4; }
  for (const k of [...varShaped.keys()]) if (pad == null || k.split(':')[2] === String(pad)) varShaped.delete(k); }
function varIn(d){ const old = varBufs.get(d.k); if (old) varBytes -= old.data.length * 4;
  varBufs.set(d.k, {data:d.data, sr:d.sr, pad:d.pad}); varBytes += d.data.length * 4;
  while (varBytes > 32e6 && varBufs.size > 1){ const [k0, r0] = varBufs.entries().next().value; varBufs.delete(k0); varBytes -= r0.data.length * 4; } }
// The sound a note plays, when it has MUT / GEN / CUT / RES / DEC: {data, sr}, or null (the page plays it)
function varRec(ev){
  const I = S && S.insts && ev.ii != null ? S.insts[ev.ii] : null;
  if (I && ((I.slc | 0) > 1 || I.sync)) return coreBufs.get(ev.pad) || null;   // sliced / synced: the pad as it is (the page's varBuf)
  let base = null, key;
  if (ev.vk){ key = ev.ii + ':' + ev.vk + ':' + ev.pad; base = varBufs.get(key); if (!base) return null;
    varBufs.delete(key); varBufs.set(key, base); }   // most recent last
  else { base = coreBufs.get(ev.pad); key = 'p:' + ev.pad + ':' + ev.pad; if (!base) return null; }
  if (!ev.tw) return base;
  // NO WHOLE-SAMPLE WORK HERE (owner, 7 Nov: stutters, then faster, then deaf to STOP, on a three-track sample song). CUT / RES
  // / DEC shaped the WHOLE sample (shapeData) inside one audio block, a fresh copy per value, up to 192 kept: a long sample
  // stalled the thread. Now the voice shapes as it plays (Rd, below), a block at a time. Reversed AND shaped: the page plays it
  // (it shapes, then reverses, which can't be streamed from the start).
  if (ev.rev) return null;
  return {data:base.data, data2:null, sr:base.sr, tw:ev.tw};   // (mono, as the page's shaped notes)
}
// A message for another processor in this scope, handled at audio time `at` (its process() takes the due ones at the
// start of each block: proc.__q, sorted by time). FXV's settings and …chains' tempo go this way instead of a timer on the
// page, which a busy main thread made late.
function tcAt(proc, at, msg){ const q = proc.__q || (proc.__q = []); let i = q.length; while (i > 0 && q[i - 1][0] > at) i--; q.splice(i, 0, [at, msg]); }
// FXV: an effect slot's settings with some controls moved (ov: control key → value), as the page's fxpMsg makes them
// The effect's map(): its own (TFX_MAP), or for the shared-module effects (MODFX) their controls' conversions, which the
// page sends as data (TFX[k].cv: [slot key, 0 as is / 1 linear / 2 log, min, max] per module param).
function fxvMap(D, type){
  if (TFX_MAP[type]) return TFX_MAP[type];
  const C = D && D.cv; if (!C) return null;
  const u8 = v => v === 128 ? 0.5 : Math.max(0, Math.min(255, v)) / 255;
  return f => { const o = {}; for (const p in C){ const [q, c, a, b] = C[p], v = f[q]; o[p] = c === 0 ? v : c === 2 ? a * Math.pow(b / a, u8(v)) : a + (b - a) * u8(v); } return o; };
}
function fxvMsg(f, slot, ov, bpm){
  const D = TFX[f.type], mp = fxvMap(D, f.type); if (!D || !mp) return null;
  const vals = mp(Object.assign({}, f, ov), sampleRate, bpm);
  if (D.mod) return {pset:{slot, mp:vals}};
  const p = new Array(12).fill(0); vals.forEach((v, i) => p[i] = v); return {pset:{slot, p}};
}
// One driven parameter: a timeline that takes over from the node's own AudioParam at its first event
function tcParam(ov, k, t){ let A = ov.p.get(k); if (!A){ A = new Auto(0); A.from = t; ov.p.set(k, A); } return A; }

const POLY = 4, FXP_CORE = ['sw0', 'sw1', 'sw2', 'sw3', 'sw4', 'sw5', 'sw6', 'sw7', 'flx', 'gat', 'rtg'];
class ThunderCore extends AudioWorkletProcessor {
  constructor(){
    super();
    this.seq = null; this.LEAD = 0.25; this.kv = []; this.vs = []; this.alive = new Set(); this.ended = false; this.lnodes = new Map(); this.voxG = new Map(); this.fxTouched = new Set(); this.fxOv = new Map();   // fxOv: FXV's moved controls, 'track:slot' → {key: value}
    this.clk = new Auto(0); this.clkOn = false; this.clkBase = 0;   // the beat clock (output NT): beat + 1 while playing, 0 stopped
    this.mixReset();
    this.port.onmessage = e => { try { this.msg(e.data || {}); } catch(err){ this.port.postMessage({op:'error', where:(e.data && e.data.op) || '?', message:String(err && err.stack || err)}); } };
    this.port.postMessage({op:'ready', v:CORE_V});
  }
  /* A prepared replay (prep) stays good only while nothing the sequencer reads has changed: any message but these drops it, and
     PLAY then replays on its own. (vars, lxp and kit arrive with every PLAY: they drop it only when they carry a different value.) */
  chaseKeep(d){
    const o = d.op;
    if (o === 'prep' || o === 'play' || o === 'lead' || o === 'ack' || o === 'stats' || o === 'sig' || o === 'dbg') return true;
    if (o === 'vars') return !d.vars || (!('vtrk' in d.vars) || d.vars.vtrk === vtrk) && (!('NSLOT' in d.vars) || d.vars.NSLOT === NSLOT) && !d.vars.TFX;
    if (o === 'lxp') return (d.st | 0) === lxp.st;
    if (o === 'kit') return JSON.stringify(d.kv || []) === JSON.stringify(this.kv);
    return false;
  }
  chaseKey(d){ return JSON.stringify([d.mode || 'song', d.o || {}, d.rs.ref, d.rs.row, d.rs.step]); }
  // The silent run up to the RESUME step, in steps of the same loop PLAY always ran: ms = a time budget, null = to the end
  chaseRun(c, ms){
    const t0 = Date.now();
    while (c.seq.alive() && c.g++ < 4096){
      const r = c.seq.step(), p = r.pos && r.pos[c.rs.ref];
      if (p && (p.row > c.rs.row || (p.row === c.rs.row && p.step >= c.rs.step))){ c.first = r; c.done = true; return; }
      if (ms != null && Date.now() - t0 >= ms) return;
    }
    c.done = true;
  }
  prep(d){
    this.chase = null;
    if (!RESUME_SPREAD || !d.rs || !S || this.seq) return;   // (a song still playing here would have its performance state cleared by the new sequencer)
    this.chase = {key:this.chaseKey(d), rs:d.rs, g:0, first:null, done:false, seq:makeSeq(S, d.mode || 'song', Object.assign({}, d.o || {}, {live:true}))};
  }
  msg(d){
    if (this.chase && !this.chaseKeep(d)) this.chase = null;
    switch (d.op){
      case 'load': S = d.S; if (d.vars){ if ('vtrk' in d.vars) vtrk = d.vars.vtrk; if ('NSLOT' in d.vars) NSLOT = d.vars.NSLOT; if (d.vars.TFX) TFX = d.vars.TFX; } busPlanH = null; busPlanVal = null; return;
      case 'song': { if (!S){ S = d.S; return; } for (const k of Object.keys(S)) if (!(k in d.S)) delete S[k]; Object.assign(S, d.S); busPlanH = null; busPlanVal = null; return; }   // in place: the running sequencer holds this object
      case 'patch': {   // only what changed (coreDiff on the page): [key, null, value] · [key, index, item] · [key, 'len', n] · [key, 'del']
        if (!S) return;
        for (const [k, i, v] of d.ops){ if (i === null) S[k] = v; else if (i === 'del') delete S[k]; else if (i === 'len'){ if (Array.isArray(S[k])) S[k].length = v; }
          else { if (!Array.isArray(S[k])) S[k] = []; S[k][i] = v; } }
        if (d.ops.some(o => o[0] === 'insts')) varDrop(null);   // an instrument's RANGE / GEN TYPE… changed: its variations are other sounds now
        busPlanH = null; busPlanVal = null; return; }
      case 'vars': if (d.vars){ if ('vtrk' in d.vars) vtrk = d.vars.vtrk; if ('NSLOT' in d.vars) NSLOT = d.vars.NSLOT; if (d.vars.TFX) TFX = d.vars.TFX; } return;
      case 'kit': this.kv = d.kv || []; kit = {voices:this.kv}; return;
      case 'buf': if (d.data) coreBufs.set(d.pad, {data:d.data, data2:d.data2 || null, sr:d.sr}); else coreBufs.delete(d.pad); varDrop(d.pad); return;   // data2: a stereo pad's right channel
      case 'vbuf': varIn(d); return;   // MUT / GEN: a variation the page rendered
      case 'sf': if (d.data) sfPads.set(d.pad, d.data); else sfPads.delete(d.pad); return;   // SOUNDFONTS: a pad's preset
      case 'vdrop': varDrop(d.pad); return;
      case 'test': {
        const o = d.opt || {}, rows = coreSeeded(() => { const out = [];
          const sq = makeSeq(S, d.mode || 'song', {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:true});
          for (let n = 0; sq.alive() && n < (d.max || 20000); n++){ const r = sq.step(); out.push({t:coreNum(r.t), evs:r.evs.map(corePlain)}); }
          return out; });
        this.port.postMessage({op:'test', id:d.id, rows}); return; }
      case 'prep': return this.prep(d);
      case 'play': return this.play(d);
      case 'hold': this.hold = true; return;   // EXACT STOP (6 Nov): stop at the next pattern edge, tell the page {op:'held', t, pos}
      case 'stop': { this.seq = null; const at = d.at == null ? currentTime : d.at; this.stopAll(at); this.clkOn = false; this.clk.cancel(at); this.clk.set(0, at);
        if (d.rel) this.release(at);   // HAND-OVER (7 Nov): the page plays on from `at`, so the live engines are its own from then
        return; }
      case 'loop': if (this.seq) this.seq.setLoop(d.l || null); return;
      case 'bpm': if (this.seq) this.seq.bpm = d.v; return;
      case 'perf': if (!perfLive.init) perfLiveInit(); if (d.xf != null) perfLive.xf = d.xf; if (d.mac) perfLive.mac = d.mac.slice(); if (d.fill != null) perfLive.fill = !!d.fill; return;
      case 'lxp': lxp.st = d.st | 0; return;
      case 'stats': this.port.postMessage({op:'stats', id:d.id, nodes:[...tcNodes().keys()], n:this.nN || 0, late:this.nLate || 0, worst:this.worst || 0, voices:this.alive.size, lead:this.leadMin == null ? null : this.leadMin, skips:this.skips || 0, lnodes:this.lnodes.size, vs:this.vs.reduce((a, T) => a + (T ? T.length : 0), 0), ovEv:[...tcNodes().values()].reduce((a, p) => a + (p.__ov ? [...p.__ov.p.values()].reduce((b, A) => b + A.ev.length, 0) + p.__ov.g.ev.length + p.__ov.pn.ev.length : 0), 0), clk:this.clk.ev.length, ms:this.pms || 0, blocks:this.pbl || 0}); this.pms = 0; this.pbl = 0; this.nN = this.nLate = this.worst = 0; this.leadMin = null; return;
      case 'sig': { const j = JSON.stringify(S); let h = 0; for (let i = 0; i < j.length; i++) h = (h * 31 + j.charCodeAt(i)) | 0; this.port.postMessage({op:'sig', len:j.length, h}); return; }
      case 'dbg': this.noSampler = !!d.noSampler; return;   // measuring: the page plays every note (the core only sequences)
      case 'die': this.release(currentTime); this.dead = true; this.seq = null; this.alive.clear(); return;   // the page let this core go (Reset audio engine)
      case 'ack': this.ackT = currentTime; return;   // WATCHDOG: the page read our rows
      case 'lead': this.LEAD = Math.max(0.05, Math.min(2, +d.s || 0.25)); return;
    }
  }
  /* MIX ON THE AUDIO THREAD (plan 2c, 2 Nov night): TVL / TVA (a track's level), TPN (its pan) and the master column's VOL.
     Three control outputs: NT + 1, one channel per track, a level multiplier (1: the fader as it is) into a gain after the
     track's VOL / mute / solo; NT + 2, one per track, a pan offset (target − the mixer's pan) added to its StereoPanner's
     pan; NT + 3, the master's level offset (target − its knob) added to the master gain. The page plugs a channel in when
     the first such command for it arrives and stops touching that param itself; STOP unplugs them all. */
  mixReset(){ this.mg = new Array(NT).fill(null); this.mp = new Array(NT).fill(null); this.mm = null; }
  mixEv(ev){
    const at = Math.max(ev.t, currentTime);
    if (ev.type === 'mvol'){ const A = this.mm || (this.mm = new Auto(0)); A.hold(at); A.lin(curve(ev.v) - curve(S && S.master != null ? S.master : 255), at + 0.004); return true; }
    const t = ev.track; if (t == null || t < 0 || t >= NT) return false;
    const m = S && S.mix && S.mix[t];
    if (ev.type === 'tpn'){ const A = this.mp[t] || (this.mp[t] = new Auto(0)), P = v => Math.max(-1, Math.min(1, (v - 128) / 127));
      A.target(P(ev.v) - P(m && m.pan != null ? m.pan : 128), at, 0.004); return true; }
    const A = this.mg[t] || (this.mg[t] = new Auto(1));
    if (ev.type === 'tvl') A.target(ev.v / 255, at, 0.004);
    else { const fg = trackGain(m && m.vol != null ? m.vol : 224), g = trackGain(ev.v); A.target(fg > 1e-6 ? g / fg : 0, at, 0.004); }
    return true;
  }
  mixOut(outputs, N, dt){
    const G = outputs[NT + 1], P = outputs[NT + 2], M = outputs[NT + 3];
    if (G) for (let t = 0; t < G.length; t++) this.mixFill(G[t], this.mg[t], 1, N, dt);
    if (P) for (let t = 0; t < P.length; t++) this.mixFill(P[t], this.mp[t], 0, N, dt);
    if (M) this.mixFill(M[0], this.mm, 0, N, dt);
  }
  mixFill(C, A, d, N, dt){ if (!C) return; if (!A){ C.fill(d); return; } A.prune(currentTime); const c = A.constFrom(currentTime); if (c != null){ C.fill(c); return; } for (let i = 0; i < N; i++) C[i] = A.at(currentTime + i * dt); }   // (a method: no closure per block)
  play(d){
    const ms0 = Date.now(), at0 = currentTime;   // PLAYBACK REPORT: how late this PLAY reached the audio thread, and how long it took
    this.stopAll(currentTime); this.mixReset(); this.clkOn = false; this.clk.cancel(currentTime); this.clk.set(0, currentTime);
    // RESUME: run silently up to the step the song stopped on; the page played that step, the core carries on after it
    // (prepared by 'prep', block by block, when the page sent it in time: what is left runs here)
    let seq;
    if (d.rs){
      let c = this.chase; this.chase = null;
      if (!(c && c.key === this.chaseKey(d))) c = {key:null, rs:d.rs, g:0, first:null, done:false, seq:makeSeq(S, d.mode || 'song', Object.assign({}, d.o || {}, {live:true}))};
      if (!c.done) this.chaseRun(c, null);
      seq = c.seq; const first = c.first;
      if (first){ const dt = d.t0 - first.t; seq.base += dt; seq.time += dt; if (seq.btT != null) seq.btT += dt; }
      else seq.time = d.t0; }
    else { this.chase = null; seq = makeSeq(S, d.mode || 'song', Object.assign({}, d.o || {}, {live:true})); seq.time = d.t0; }
    this.seq = seq; this.ended = false; this.gen = d.gen | 0; this.hold = false; this.lr = null; this.ls = null;
    // WATCHDOG (phones; owner, 7 Nov, Fold, screen on: stutter, faster, slower, then it played on after STOP)
    this.watch = !!d.watch; this.ackT = currentTime; this.ckW = Date.now(); this.ckA = currentTime; this.ckBad = 0;
    this.port.postMessage({op:'started', gen:this.gen, late:at0 - d.t0, ms:Date.now() - ms0});
  }

  /* ---- the sampler: the page player's rendered-pad path (makePlayer: startNote, kill, pitchMods, modVoice, play) ---- */
  T(t){ return this.vs[t] || (this.vs[t] = []); }
  // SOUNDFONTS: makePlayer's sfNote. (One difference: a bend, arpeggio or vibrato moves every zone of a layered preset;
  // the page's moved only the first.)
  sfNote(ev){
    const D = sfPads.get(ev.pad), kv = this.kv[ev.pad]; if (!D || !kv) return;
    const P = D.P, cl = (v, a, b) => Math.max(a, Math.min(b, v)), lr = Math.log2(Math.max(1e-4, ev.rate));
    const key = cl(Math.round(60 + 12 * lr + P.trans), 0, 127), vel = cl(Math.round(127 * ((1 - P.velo) + P.velo * Math.sqrt(cl(ev.gain, 0, 1)))), 1, 127);
    const picks = sfPick(D.font, D.pr, key, vel); if (!picks.length) return;
    { const ld = ev.t - currentTime; this.nN = (this.nN || 0) + 1; if (ld < 0){ this.nLate = (this.nLate || 0) + 1; this.worst = Math.max(this.worst || 0, -ld); } if (this.leadMin == null || ld < this.leadMin) this.leadMin = ld; }
    const T = this.T(ev.track);
    for (let k = T.length - 1; k >= 0; k--) if (!this.live1(T[k], ev.t)) T.splice(k, 1);
    if (!ev.poly){ for (const v of T) this.kill(v, ev.t, v.syn ? Math.min(this.relTc(v), 0.05) : 0.004); T.length = 0; }
    else while (T.length >= POLY) this.kill(T.shift(), ev.t, 0.006);
    const ck = kv.choke || 0;
    if (ck) for (let k = 0; k < NT; k++) if (k !== ev.track) for (const v of this.T(k)) if (v.choke === ck) this.kill(v, ev.t);
    const st0 = Math.max(ev.t, currentTime), sr = sampleRate, hz = c => cl(8.176 * Math.pow(2, c / 1200), 20, sr * 0.45);
    const expTo = (A, from, to, t, d) => { if (!(from > 0) || !(to > 0)){ A.lin(to, t + d); return; } A.curve(x => from * Math.pow(to / from, Math.min(1, Math.max(0, (x - t) / d))), t, d); };
    const zones = []; let relMax = 0.01, looped = false, base = ev.rate, st = false;
    for (const {z, stereo} of picks){
      const vo = sfVoice(D.font, z, key, vel), a = D.smp[z.s]; if (!a || !a.d) continue;
      let L = a.d, R = null;
      if (stereo){ const s0 = D.font.samples[z.s], b = s0 && D.smp[s0.link]; if (b && b.d){ const left = s0.type & 4; L = left ? a.d : b.d; R = left ? b.d : a.d; } }
      const n = R ? Math.min(L.length, R.length) : L.length;
      const F = Math.pow(2, ((60 + P.trans - vo.root) * vo.scale / 100 + vo.tune) / 12), rate = F * Math.pow(ev.rate, vo.scale / 100);
      const zr = new Auto(rate);
      if (ev.slide && ev.slide.dur > 0 && vo.scale === 100){ zr.set(ev.slide.from * F, ev.t); zr.lin(rate, ev.t + ev.slide.dur); }
      if (!zones.length) base = rate;
      const lp = (vo.modes === 1 || vo.modes === 3) && a.le > a.ls + 8 && a.le <= n; if (lp) looped = true;
      const E = vo.v, lg = Math.pow(10, -vo.atten / 200), sus = Math.max(1e-5, lg * Math.pow(10, -E.sus / 200)), atk = E.atk * P.atk;
      const t1 = st0 + E.delay, t2 = t1 + Math.max(0.0015, atk), t3 = t2 + E.hold, env = new Auto(0);
      env.set(0, st0); env.set(0, t1); env.lin(lg, t2); env.set(lg, t3);
      if (E.sus > 0) expTo(env, lg, sus, t3, Math.max(0.005, E.dec * Math.min(1, E.sus / 1000)));
      let flt = null;
      if (vo.fc < 13490 || Math.abs(vo.modEnvToFc) > 1){
        const M = vo.m, f0 = hz(vo.fc), fp = hz(vo.fc + vo.modEnvToFc), fs = hz(vo.fc + vo.modEnvToFc * (1 - M.sus));
        const m1 = st0 + M.delay, m2 = m1 + Math.max(0.001, M.atk), m3 = m2 + M.hold, fr = new Auto(f0);
        fr.set(f0, st0); fr.set(f0, m1); expTo(fr, f0, fp, m1, m2 - m1); fr.set(fp, m3); expTo(fr, fp, fs, m3, Math.max(0.005, M.dec * Math.max(0.001, M.sus)));
        flt = {fr, q:vo.q / 10, k:0, b0:1, b1:0, b2:0, a1:0, a2:0, x1:0, x2:0, y1:0, y2:0, u1:0, u2:0, w1:0, w2:0};
      }
      const zp = !R && vo.pan ? vo.pan / 500 : null; if (R || zp != null) st = true;
      zones.push({L, R, n, sr:a.sr, ls:a.ls, le:a.le, lp, r:zr, rate, env, flt, zp, pos:0, done:false});
      relMax = Math.max(relMax, Math.min(8, E.rel * P.rel));
    }
    if (!zones.length) return;
    const g0 = ev.gain * Math.pow(10, ((P.level || 0) + 6) / 20);
    const v = {sf:zones, st, data:null, r:new Auto(base), g:new Auto(g0), f:new Auto(1), pn:new Auto(cl((ev.pan - 128) / 127, -1, 1)), base, rate:base,
      choke:ck, col:ev.col, ended:false, stopAt:null, syn:true, rel:relMax * 0.4, track:ev.track, end:Infinity, t0:st0, trm:null, apn:null};
    if (!ev.slide) this.pitchMods(v, ev.t, base, ev);
    this.alive.add(v);
    if (ev.cut != null) this.kill(v, ev.cut);
    if (ev.rel != null) this.kill(v, ev.rel, this.relTc(v));
    else if (!kv.syn && looped) this.kill(v, st0 + Math.max(0.1, P.len || 2), this.relTc(v));   // a drum pad: a looping zone lets go after the pad's Max len
    T.push(v);
  }
  // One block of a SoundFont voice into its track's output (as the page's nodes: zones → [zone pan] → gain → fade → pan)
  sfRun(v, out, N){
    const L = out[0], R = out[1] || out[0], dt = 1 / sampleRate, t0 = currentTime;
    v.g.prune(t0); v.f.prune(t0); v.pn.prune(t0); v.r.prune(t0);
    const sL = v.sL || (v.sL = new Float32Array(128)), sR = v.sR || (v.sR = new Float32Array(128)); sL.fill(0); sR.fill(0);
    const rc = v.r.constFrom(t0); let any = false;
    for (const z of v.sf){ if (z.done) continue;
      z.env.prune(t0); z.r.prune(t0); if (z.flt) z.flt.fr.prune(t0);
      const ec = z.env.constFrom(t0), zc = z.r.constFrom(t0), D = z.L, E = z.R, n = z.n, k0 = z.sr / sampleRate, F = z.flt;
      const za = z.zp != null ? (z.zp + 1) * Math.PI / 4 : 0, zl = z.zp != null ? Math.cos(za) : 1, zrr = z.zp != null ? Math.sin(za) : 1;
      for (let i = 0; i < N; i++){
        const t = t0 + i * dt; if (t < v.t0) continue;
        if (z.lp){ while (z.pos >= z.le) z.pos -= z.le - z.ls; }
        else if (z.pos >= n - 1){ z.done = true; break; }
        const p = z.pos, j = p | 0, fr = p - j, j1 = z.lp && j + 1 >= z.le ? z.ls : j + 1;
        const e = ec != null ? ec : z.env.at(t);
        let x = (D[j] + (D[j1] - D[j]) * fr) * e, y = E ? (E[j] + (E[j1] - E[j]) * fr) * e : x;
        if (F){   // Web Audio's low-pass (Q in dB), its cutoff read every 16 samples
          if ((F.k++ & 15) === 0){ const f = F.fr.at(t), w = 2 * Math.PI * Math.min(f, sampleRate * 0.49) / sampleRate, c = Math.cos(w), al = Math.sin(w) / (2 * Math.pow(10, F.q / 20)), a0 = 1 + al;
            F.b0 = (1 - c) / 2 / a0; F.b1 = (1 - c) / a0; F.b2 = F.b0; F.a1 = -2 * c / a0; F.a2 = (1 - al) / a0; }
          const ox = F.b0 * x + F.b1 * F.x1 + F.b2 * F.x2 - F.a1 * F.y1 - F.a2 * F.y2; F.x2 = F.x1; F.x1 = x; F.y2 = F.y1; F.y1 = ox; x = ox;
          if (E){ const oy = F.b0 * y + F.b1 * F.u1 + F.b2 * F.u2 - F.a1 * F.w1 - F.a2 * F.w2; F.u2 = F.u1; F.u1 = y; F.w2 = F.w1; F.w1 = oy; y = oy; } else y = x;
        }
        if (z.zp != null){ sL[i] += x * zl; sR[i] += x * zrr; } else { sL[i] += x; sR[i] += y; }
        const ratio = (rc != null ? rc : v.r.at(t)) / v.base;
        z.pos += (zc != null ? zc : z.r.at(t)) * ratio * k0;
      }
      if (!z.done) any = true;
    }
    const gc = v.g.constFrom(t0), fc = v.f.constFrom(t0), pc = v.pn.constFrom(t0);
    for (let i = 0; i < N; i++){
      const t = t0 + i * dt; if (t < v.t0) continue;
      if (t >= v.end){ v.ended = true; break; }
      const G = (gc != null ? gc : v.g.at(t)) * (fc != null ? fc : v.f.at(t)), p = pc != null ? pc : v.pn.at(t);
      if (!v.st){ const m = sL[i] * G, a = (p + 1) * Math.PI / 4; L[i] += m * Math.cos(a); R[i] += m * Math.sin(a); continue; }   // StereoPannerNode, mono in
      const l = sL[i] * G, r = sR[i] * G;   // stereo in
      if (p <= 0){ const x = (p + 1) * Math.PI / 2; L[i] += l + r * Math.cos(x); R[i] += r * Math.sin(x); }
      else { const x = p * Math.PI / 2; L[i] += l * Math.cos(x); R[i] += r + l * Math.sin(x); }
    }
    if (!any) v.ended = true;
  }
  relTc(v){ return v.syn ? v.rel / 4.6 : 0.004; }
  live1(v, at){ return v && !v.ended && !(v.stopAt != null && v.stopAt <= at); }
  kill(v, at, tc = 0.004, stop = false){
    // STOP (stopAll): a voice booked to start at or after the stop never sounds (CodeRabbit, 7 Nov: a fade began at full level, so the
    // next pattern's downbeat blipped); one already sounding fades as before
    if (stop && v && !v.ended && !v.live && v.t0 >= at - 1e-6){ v.stopAt = at; v.end = Math.min(v.end, v.t0); return; }   // (before the check below: a note's own OFF booked at its start doesn't let it through)
    if (!v || v.ended || (v.stopAt != null && v.stopAt <= at)) return;
    if (v.live){   // a live engine's note lets go (its release is the engine's own); stopping before a booked note takes it back
      const B = v.live, L = v.L, ov = tcOver(L.proc), t = Math.max(at, currentTime), G = tcParam(ov, this.pn(L, v.lane, 'gate'), t); v.stopAt = at;
      if (t < B.lastOn - 1e-6){ G.cancel(t); tcParam(ov, this.pn(L, v.lane, 'trig'), t).cancel(t); }
      G.set(0, t); return; }
    v.stopAt = at; at = Math.max(at, currentTime);
    v.f.cancel(at); v.f.set(1, at); v.f.target(0, at, tc); v.end = Math.min(v.end, at + tc * 12);
  }
  pitchMods(v, t, base, ev){
    const pr = v.r;
    if (ev.arp){ for (const [tt, mul] of ev.arp) pr.set(base * mul, Math.max(tt, t)); if (ev.arp.length) pr.set(base, ev.arp[ev.arp.length - 1][0] + (ev.arp.length > 1 ? ev.arp[1][0] - ev.arp[0][0] : 0.02)); }
    else if (ev.bend){ pr.set(base, t); pr.lin(base * ev.bend.mul, ev.bend.t1); v.rate = base * ev.bend.mul; }
    else if (ev.vib && ev.vib.dep){
      const d = Math.max(0.02, ev.vib.t1 - t), hz = 1 + ev.vib.spd * 1.2, st = ev.vib.dep / 15 * 1.5, t0 = t + 0.0005, dd = d - 0.001;
      pr.set(base, t); pr.curve(x => base * Math.pow(2, st * Math.sin(2 * Math.PI * hz * d * Math.min(1, Math.max(0, (x - t0) / dd))) / 12), t0, dd);
    }
  }
  modVoice(v, ev){
    if (ev.gain != null) v.g.set(ev.gain, ev.t);
    if (ev.pan != null) v.pn.set(Math.max(-1, Math.min(1, (ev.pan - 128) / 127)), ev.t);
    if (ev.mul != null){ v.rate = v.base * ev.mul; v.r.set(v.rate, ev.t); }
    this.pitchMods(v, ev.t, v.rate, ev);
    if (ev.cut != null) this.kill(v, ev.cut);
    if (ev.rel != null) this.kill(v, ev.rel, v.syn ? this.relTc(v) : 0.05);
  }
  // Can the core play this note itself? A rendered pad (not a live engine, a SoundFont or a MUT / GEN variation) whose
  // render the core holds.
  mine(ev){
    if (this.noSampler) return false;
    const k = this.kv[ev.pad]; if (!k || k.live) return false;
    if (k.sf) return sfPads.has(ev.pad);   // SOUNDFONTS: once the page has sent the preset (startNote → sfNote)
    if (ev.st) return false;   // stretched (SYNC) notes: the page's stretch cache, for now
    if (ev.vk || ev.tw){ const r = varRec(ev); if (!r) return false; ev.__buf = r; return true; }   // MUT / GEN / CUT / RES / DEC
    return coreBufs.has(ev.pad);
  }
  // A note the page plays: the core's voices on the track still give way to it (mono takeover, chokes), as they would
  // to any note in one player.
  take(ev){
    const T = this.T(ev.track), kv = this.kv[ev.pad];
    for (let k = T.length - 1; k >= 0; k--) if (!this.live1(T[k], ev.t)) T.splice(k, 1);
    if (!ev.poly){ for (const v of T) this.kill(v, ev.t, v.syn ? Math.min(this.relTc(v), 0.05) : 0.004); T.length = 0; }
    const ck = kv ? kv.choke : 0;
    if (ck) for (let k = 0; k < NT; k++) if (k !== ev.track) for (const v of this.T(k)) if (v.choke === ck) this.kill(v, ev.t);
  }
  startNote(ev){
    if (this.kv[ev.pad] && this.kv[ev.pad].sf){ delete ev.__buf; return this.sfNote(ev); }
    const c = ev.__buf || coreBufs.get(ev.pad), kv = this.kv[ev.pad]; delete ev.__buf; if (!c || !kv) return;
    { const ld = ev.t - currentTime; this.nN = (this.nN || 0) + 1; if (ld < 0){ this.nLate = (this.nLate || 0) + 1; this.worst = Math.max(this.worst || 0, -ld); } if (this.leadMin == null || ld < this.leadMin) this.leadMin = ld; }   // timing stats ({op:'stats'})
    const T = this.T(ev.track);
    for (let k = T.length - 1; k >= 0; k--) if (!this.live1(T[k], ev.t)) T.splice(k, 1);
    if (!ev.poly){ for (const v of T) this.kill(v, ev.t, v.syn ? Math.min(this.relTc(v), 0.05) : 0.004); T.length = 0; }
    else while (T.length >= POLY) this.kill(T.shift(), ev.t, 0.006);
    const ck = kv.choke || 0;
    if (ck) for (let k = 0; k < NT; k++) if (k !== ev.track) for (const v of this.T(k)) if (v.choke === ck) this.kill(v, ev.t);
    // REVERSE (7 Nov): read back to front by the voice's reader (Rd) instead of a reversed copy of the whole sample made here
    const data = c.data, dur = data.length / c.sr, data2 = c.data2 || null;   // STEREO SAMPLES (5 Nov)
    const r = new Auto(ev.rate), g = new Auto(ev.gain), f = new Auto(1), pn = new Auto(Math.max(-1, Math.min(1, (ev.pan - 128) / 127)));
    if (ev.slide && ev.slide.dur > 0){ r.set(ev.slide.from, ev.t); r.lin(ev.rate, ev.t + ev.slide.dur); }
    const t0n = Math.max(ev.t, currentTime), hz = x => ((x >> 4) || 1) * (this.seq ? this.seq.bpm : S.bpm) / 60, dep = x => (x & 15) / 15;
    let trm = null, apn = null;
    if (ev.fdi > 0){ g.set(0, t0n); g.lin(ev.gain, t0n + ev.fdi); }
    if (ev.trm && dep(ev.trm) > 0){ const k = dep(ev.trm) / 2; trm = {hz:hz(ev.trm), d:ev.gain * k, t:t0n}; if (!(ev.fdi > 0)) g.v0 = ev.gain * (1 - k); }
    if (ev.apn && dep(ev.apn) > 0) apn = {hz:hz(ev.apn), d:dep(ev.apn), t:t0n};
    const syn = !!kv.syn, v = {data, data2, sr:c.sr, r, g, f, pn, trm, apn, base:ev.rate, rate:ev.rate, choke:ck, col:ev.col, ended:false, stopAt:null, syn, rel:syn ? (ev.rlo != null ? ev.rlo : kv.rel || 0) : 0, track:ev.track, end:Infinity, pos:0, t0:0};
    if (!ev.slide) this.pitchMods(v, ev.t, ev.rate, ev);
    const off = ev.ofsSec != null ? (ev.rev ? Math.max(0, dur - ev.ofsSec - ev.durSec) : ev.ofsSec) : Math.max(0, Math.min(255, ev.ofs || 0)) / 256 * dur;
    if (off > 0.0005){ f.set(0, t0n); f.lin(1, t0n + 0.0015); }
    v.t0 = t0n; v.pos = Math.min(off, Math.max(0, dur - 0.001)) * c.sr;
    if (ev.rev || c.tw) v.rd = new Rd(c.data, c.data2 || null, c.sr, c.tw || null, !!ev.rev, v.pos);   // (see Rd)
    this.alive.add(v);
    if (ev.cut != null) this.kill(v, ev.cut);
    if (ev.rel != null) this.kill(v, ev.rel, syn ? this.relTc(v) : 0.05);
    T.push(v);
  }
  // One event, as the page's player.play() does for the ones that touch its voices. Returns true when the core sounded it.
  ev(ev){
    // FXO / FLX / GAT / RTG on a track's (or the master's) effect node: makePlayer's 'fxc' and 'fxreset', on the node's own
    // params from here. A node the page hasn't made yet: the page does it (it holds the value until the node exists).
    if (ev.type === 'fxc' || ev.type === 'fxreset'){
      const proc = tcNodes().get('fx:' + ev.track); if (!proc) return false;
      const ov = tcOver(proc), at = Math.max(ev.t, currentTime); ov.until = Infinity;
      for (const k of ev.type === 'fxc' ? [ev.p] : FXP_CORE){ const A = tcParam(ov, k, at); if (ev.type === 'fxreset') A.cancel(at); A.set(ev.type === 'fxc' ? ev.v : -1, at); }
      this.fxTouched.add('fx:' + ev.track); return true;
    }
    // FXV (3 Nov): the selected control of an effect slot, makePlayer's fxpTo: the slot's settings are worked out here
    // (TFX_MAP: the effect's own map()) and handed to its node at the row's time. Effects whose map isn't in the core
    // (TFX_MAP) and …pump slots stay on the page.
    if (ev.type === 'fxp'){
      const proc = tcNodes().get('fx:' + ev.track), A = S && (ev.track >= NT ? S.mfx : S.tfx && S.tfx[ev.track]), f = A && A[ev.slot];
      if (!proc || !f || !TFX[f.type] || TFX[f.type].pump || !fxvMap(TFX[f.type], f.type)) return false;
      const q = TFX[f.type].ps[ev.pi]; if (!q) return false;
      const key = ev.track + ':' + ev.slot, ov = Object.assign({}, this.fxOv.get(key)); ov[q.k] = q.min + Math.round((q.max - q.min) * ev.v / 255);
      const m = fxvMsg(f, ev.slot, ov, this.seq ? this.seq.bpm : S.bpm); if (!m) return false;
      this.fxOv.set(key, ov); tcAt(proc, Math.max(ev.t, currentTime), m); return true;
    }
    // BYP (3 Nov): a …chains module's switch (b1–b15 on the track's thunder-chains node), as fxc above
    if (ev.type === 'chc'){
      const proc = tcNodes().get('chn:' + ev.track); if (!proc) return false;
      const ov = tcOver(proc), at = Math.max(ev.t, currentTime); ov.until = Infinity;
      tcParam(ov, ev.p, at).set(ev.v, at); this.fxTouched.add('chn:' + ev.track); return true;
    }
    // A tempo change: …chains' transport, at its time (the page still sets its own tempo and the live engines' bpm)
    if (ev.type === 'tempo'){
      let n = 0; for (const [k, proc] of tcNodes()) if (k.startsWith('chn:')){ tcAt(proc, Math.max(ev.t, currentTime), {target:'chains-host', op:'transport', value:{tempo:ev.bpm, ticks:ev.tks}}); n++; }
      ev.chnCore = n > 0; return false;
    }
    // the tempo-locked effects' beat clock, as makePlayer's play('beat') ramps its ConstantSource (here + 1: see clkBus)
    if (ev.type === 'beat'){ const at = Math.max(ev.t, currentTime), A = this.clk;
      if (!this.clkOn){ this.clkOn = true; this.clkBase = Math.floor(ev.b / 16) * 16; A.set(ev.b - this.clkBase + 1, at); return true; }
      const v = ev.b - this.clkBase; A.lin(v + 1, at); if (v >= 16){ this.clkBase += 16; A.set(v - 16 + 1, at); } return true; }
    if (ev.type === 'stp'){ for (let k = 0; k <= NT; k++) if (ev.all || k === ev.track){ for (const v of this.T(k)) this.kill(v, ev.t, 0.004); this.T(k).length = 0; } return false; }
    if (ev.type === 'tvl' || ev.type === 'tva' || ev.type === 'tpn' || ev.type === 'mvol') return this.mixEv(ev);
    if (ev.type === 'off' && ev.track != null){ const T = this.T(ev.track);
      if (ev.cols){ for (let k = T.length - 1; k >= 0; k--) if (ev.cols.includes(T[k].col)){ this.kill(T[k], ev.t, this.relTc(T[k])); T.splice(k, 1); } return false; }
      for (const v of T) this.kill(v, ev.t, this.relTc(v)); T.length = 0; return false; }
    if (ev.type === 'mod' && ev.track != null){ for (const v of this.T(ev.track)) if (this.live1(v, ev.t)) this.modVoice(v, ev); return false; }
    if (ev.type !== 'note' || ev.pad == null || ev.track == null) return false;
    { const I = S && S.insts && ev.ii != null ? S.insts[ev.ii] : null; if (I && (I.mo | 0) === 1) return false; }   // MIDI gear only: the page sends it, nothing sounds here
    if (this.mine(ev)){ this.startNote(ev); return true; }
    if (this.liveNote(ev)) return true;
    this.take(ev); return false;
  }
  // the live-engine and effect overrides end at `at` instead of after the release (stopAll's at + 4 / + 0.5): the page's own notes
  // on those nodes from then aren't held shut by the core's gates
  release(at){ for (const proc of tcNodes().values()) if (proc && proc.__ov) proc.__ov.until = Math.min(proc.__ov.until, at); }
  stopAll(at){ for (const v of this.alive) this.kill(v, at, 0.004, true); for (const T of this.vs) if (T) T.length = 0;
    for (const L of this.lnodes.values()){ if (L.cur) this.kill(L.cur, at); if (L.lanes) for (const q of L.lanes) if (q.cur) this.kill(q.cur, at); L.sylN = 0;
      const ov = L.proc.__ov; if (!ov) continue;   // and whatever is still booked on it: gates shut now, no note after this
      for (const [k, A] of ov.p){ if (/^(gate|g\d)$/.test(k)){ A.cancel(at); A.set(0, at); } else if (/^(trig|t\d)$/.test(k)) A.cancel(at); }
      ov.until = at + 4; }   // a live note lets go; its release rings, then the page has the node back
    this.voxG.clear();
    // the effect switches go back to the slots' own (−1), as the page's resetSeq does, then the page has the params again
    for (const key of this.fxTouched){ const proc = tcNodes().get(key); if (!proc || !proc.__ov) continue; const ov = proc.__ov;
      for (const A of ov.p.values()){ A.cancel(at); A.set(-1, at); } ov.until = at + 0.5; }   // FXO… (fx:) and BYP b1–b15 (chn:)
    this.fxTouched.clear();
    // FXV: the slots' own settings again (makePlayer's resetSeq does the same for the ones the page moved)
    for (const [key, ov] of this.fxOv){ const [t, k] = key.split(':').map(Number), proc = tcNodes().get('fx:' + t), A = S && (t >= NT ? S.mfx : S.tfx && S.tfx[t]), f = A && A[k];
      const m = proc && f && fxvMsg(f, k, {}, S.bpm); if (m){ if (proc.__q) proc.__q = proc.__q.filter(x => !(x[1].pset && x[1].pset.slot === k)); tcAt(proc, at, m); } }
    this.fxOv.clear(); }

  /* ---- live engines: makePlayer's bassNode / bassNote / polyVox / polyPs, driving the registered nodes ---- */
  lnode(t, pad, j){
    const k = this.kv[pad], lk = k && k.lk; if (!lk) return null;
    const key = t + ':' + pad + ':' + lk.kind + (j ? ':' + j : ''), proc = tcNodes().get(key); if (!proc) return null;
    let L = this.lnodes.get(key); if (!L || L.proc !== proc){ L = {key, proc, pad, kind:lk.kind, vox:!!lk.vox, ps:!!lk.ps, n:1e6 + Math.floor(Math.random() * 1e5) * 10, lastOn:-1, cur:null, sylN:0, took:false};
      if (L.ps) L.lanes = [0, 1, 2, 3].map(q => ({L, lane:q, n:L.n + q * 1e5, lastOn:-1, cur:null}));
      this.lnodes.set(key, L); }
    return L;
  }
  // the AudioParam names on this node (a …pump synth's lane j: r/g/t/l/v + j)
  pn(L, lane, k){ if (!L.ps) return k; return ({rate:'r', gate:'g', trig:'t', leg:'l', vel:'v'})[k] + lane; }
  liveNote(ev){
    const k = this.kv[ev.pad], lk = k && k.lk; if (!lk) return false;
    const T = this.T(ev.track);
    if (ev.poly && lk.vox){   // polyVox
      const nodes = []; for (let j = 0; j < POLY; j++){ const L = this.lnode(ev.track, ev.pad, j + 1); if (!L) return false; nodes.push(L); }
      const key = ev.track + ':' + ev.pad; let G = this.voxG.get(key); if (!G){ G = {sylN:0, lastT:-9, lastK:0}; this.voxG.set(key, G); }
      const leg = ev.slide && ev.slide.dur > 0;
      let kk; if (Math.abs(ev.t - G.lastT) < 0.004) kk = G.lastK; else { kk = leg ? Math.max(0, G.sylN - 1) : G.sylN++; G.lastT = ev.t; G.lastK = kk; }
      const free = nodes.filter(b => !b.cur || b.cur.stopAt != null || b.cur.ended), pick = this.sameCol(nodes, ev) || (free.length ? free : nodes).reduce((x, y) => (x.lastOn <= y.lastOn ? x : y));
      if (pick.lastOn >= ev.t - 1e-6 && Math.abs(pick.lastOn - ev.t) < 0.004 && free.length === 0) return true;
      for (let q = T.length - 1; q >= 0; q--) if (T[q].live === pick) T.splice(q, 1);
      this.liveStart(ev, pick, null, T, k.choke || 0, kk); ev.coreLive = [pick.key]; return true;
    }
    if (ev.poly && lk.ps){   // polyPs: the node's four lanes
      const L = this.lnode(ev.track, ev.pad, 0); if (!L) return false;
      const Ls = L.lanes, free = Ls.filter(x => !x.cur || x.cur.stopAt != null || x.cur.ended), pick = this.sameCol(Ls, ev) || (free.length ? free : Ls).reduce((x, y) => (x.lastOn <= y.lastOn ? x : y));
      for (let q = T.length - 1; q >= 0; q--) if (T[q].live === pick) T.splice(q, 1);
      this.liveStart(ev, L, pick, T, k.choke || 0); ev.coreLive = [L.key]; return true;
    }
    const L = this.lnode(ev.track, ev.pad, 0); if (!L) return false;   // mono: it takes over the track
    for (const v of T) if (v.live !== (L.ps ? L.lanes[0] : L)) this.kill(v, ev.t, v.syn ? Math.min(this.relTc(v), 0.05) : 0.004); T.length = 0;
    const ck = k.choke || 0; if (ck) for (let q = 0; q < NT; q++) if (q !== ev.track) for (const v of this.T(q)) if (v.choke === ck) this.kill(v, ev.t);
    this.liveStart(ev, L, L.ps ? L.lanes[0] : null, T, ck); ev.coreLive = [L.key]; return true;
  }
  sameCol(nodes, ev){ return ev.col == null ? null : nodes.find(b => b.cur && !b.cur.ended && (b.cur.stopAt == null || b.cur.stopAt > ev.t - 1e-6) && b.cur.col === ev.col && b.lastOn < ev.t - 0.004) || null; }
  // bassNote: one note on a node (or a …pump synth lane)
  liveStart(ev, L, lane, T, ck, fk){
    const ov = tcOver(L.proc); ov.until = Infinity; ov.post = true;
    const B = lane || L, j = lane ? lane.lane : 0, P = k => tcParam(ov, this.pn(L, j, k), Math.max(ev.t, currentTime));
    const t = Math.max(ev.t, currentTime), leg = ev.slide && ev.slide.dur > 0 ? 1 : 0;
    for (const k of ['gate', 'trig', 'leg']) P(k).cancel(t);
    const R = P('rate'); R.hold(t);
    if (leg){ R.set(ev.slide.from, t); R.lin(ev.rate, t + ev.slide.dur); } else R.set(ev.rate, t);
    if (L.vox){ const S1 = P('syl'), S2 = P('same'); S1.cancel(t); S2.cancel(t); const kk = fk != null ? fk : leg ? Math.max(0, L.sylN - 1) : L.sylN++; S1.set(kk, t); S2.set(leg, t); }
    if (this.kv[L.pad] && this.kv[L.pad].lk && !L.vox && !L.ps && L.kind !== 'bass'){ const A = P('acc'); A.cancel(t); A.set(ev.acc ? 1 : 0, t); }
    let gain = ev.gain;
    if (L.ps){ const vc = ev.vc != null && ev.vc >= 0 ? ev.vc : -1, hit = (this.kv[L.pad].lk.vel) || 100, V = P('vel'); V.cancel(t); V.set(Math.max(1, Math.min(127, Math.round((vc >= 0 ? 127 : hit) * (ev.vel != null ? ev.vel : 1)))), t);
      if (vc >= 0){ const c = curve(vc); gain = c > 1e-4 ? ev.gain / c : 0; } }
    P('leg').set(leg, t); P('gate').set(1, t); B.n += 1; P('trig').set(B.n, t);
    ov.g.set(gain, t); ov.pn.set(Math.max(-1, Math.min(1, (ev.pan - 128) / 127)), t);
    if (ev.wob != null && L.kind === 'bass'){ const W = P('wob'); W.set(ev.wob, t); }
    B.lastOn = t; if (B.cur) this.alive.delete(B.cur);
    const v = {live:B, L, lane:j, r:R, g:ov.g, pn:ov.pn, base:ev.rate, rate:ev.rate, choke:ck, col:ev.col, ended:false, stopAt:null, syn:true, rel:0, track:ev.track, end:Infinity, t0:t};
    B.cur = v;
    if (!leg) this.pitchMods(v, t, ev.rate, ev);
    if (ev.cut != null) this.kill(v, ev.cut);
    if (ev.rel != null) this.kill(v, ev.rel);
    T.push(v);
  }

  process(inputs, outputs){
    if (this.dead) return false;
    const __t0 = Date.now(); try { return this.process1(inputs, outputs); } finally { this.pms = (this.pms || 0) + Date.now() - __t0; this.pbl = (this.pbl || 0) + 1; }
  }
  /* WATCHDOG (phones only: the page sends watch:true where it hands over, seqHandHere). The page's own checks needed this
     thread to read their messages, and an overloaded audio thread doesn't: its clock lurches (0.8×, then 1.5×) and STOP,
     re-sent or not, waits behind the rendering, while the core keeps driving the live engines (the poly tracks), which
     unplugging the core doesn't silence. So the core watches itself and lets go on its own:
     - CLOCK: audio time ÷ wall time outside 0.8–1.25 over two 1-second windows running;
     - DEAF: the page hasn't acknowledged a row for 3 s of audio time (it can't hear us, or we can't hear it: a STOP may be
       stuck behind us, or the page dropped this core).
     Letting go is a hand-over stop: every voice and booked note ends now, and the live engines go back to the page's own
     params at once (silent when stopped; the page sequencer's notes when it carries on). The page is told ('self'). */
  watchdog(){
    const s = this.seq; if (!s || !this.watch) return false;
    let why = null;
    if (currentTime - (this.ackT || 0) > 3) why = 'deaf';
    else { const w = Date.now(); if (w - this.ckW >= 1000){ const r = (currentTime - this.ckA) / ((w - this.ckW) / 1000); this.ckW = w; this.ckA = currentTime;
      if (r < 0.8 || r > 1.25){ if (++this.ckBad >= 2) why = 'clock ' + r.toFixed(2); } else this.ckBad = 0; } }
    if (!why) return false;
    const at = currentTime; this.seq = null; this.hold = false; this.stopAll(at); this.release(at); this.clkOn = false; this.clk.cancel(at); this.clk.set(0, at);
    this.port.postMessage({op:'self', gen:this.gen, why, t:at});
    return true;
  }
  process1(inputs, outputs){
    this.watchdog();
    if (this.chase && !this.chase.done) this.chaseRun(this.chase, CHASE_MS);   // a prepared RESUME replay, a millisecond a block
    // 1. step the song up to LEAD ahead: the core books its own notes, the rows go up for the page's part
    const s = this.seq;
    if (s){
      // The audio thread itself was held up (a screenshot, the system busy): rather than racing through the rows it missed
      // (owner, Fold: "the speed increased"), the song moves on from now, as the page's pump does.
      if (s.alive() && s.base != null && s.time < currentTime - 0.03){ this.skips = (this.skips || 0) + 1; const d = currentTime + 0.02 - s.time; s.base += d; s.time += d; if (s.btT != null) s.btT += d; }
      const rows = []; let n = 0, held = null;
      while (s.alive() && s.time < currentTime + this.LEAD && n++ < 3){   // at most 3 steps a block: a longer LEAD (screen off) fills over a few blocks, not in one burst that overruns it
        const r = s.step();
        // EXACT STOP (6 Nov): the pattern edge (a new song row, or the same one again) is where a held STOP ends the song:
        // that step isn't played, every voice lets go at its time, and the page is told
        if (r.pos){ let row = -1, st = -1; for (const p of r.pos) if (p){ if (p.row > row) row = p.row; if (p.step > st) st = p.step; }
          if (row >= 0){ const edge = this.lr != null && (row !== this.lr || st < this.ls); this.lr = row; this.ls = st;
            if (edge && this.hold){ held = {t:r.t, pos:r.pos}; this.hold = false; this.seq = null; this.stopAll(r.t); this.clkOn = false; this.clk.cancel(r.t); this.clk.set(0, r.t); break; } } }
        for (const e of r.evs) if (this.ev(e)) e.core = true;
        rows.push({t:r.t, evs:r.evs, pos:r.pos, tks:r.tks, line:r.line, bpm:s.bpm});
      }
      if (rows.length) this.port.postMessage({op:'rows', gen:this.gen, rows, time:s.time, bpm:s.bpm, tks:s.tks, base:s.base, btT:s.btT, alive:s.alive(), skips:this.skips || 0});
      if (held) this.port.postMessage({op:'held', gen:this.gen, t:held.t, pos:held.pos});
      if (!held && !s.alive() && !this.ended){ this.ended = true; this.port.postMessage({op:'end', gen:this.gen, time:s.time}); }
    }
    // 2. sound the voices into their tracks' outputs
    const N = (outputs[0] && outputs[0][0] && outputs[0][0].length) || 128, sr = sampleRate, dt = 1 / sr;
    { const C = outputs[NT] && outputs[NT][0]; if (C){ const A = this.clk; A.prune(currentTime); if (!A.ev.length && A.v0 === 0) C.fill(0); else for (let i = 0; i < N; i++) C[i] = A.at(currentTime + i * dt); } }
    this.mixOut(outputs, N, dt);
    const tb0 = currentTime, tb1 = currentTime + N * dt;
    for (const v of this.alive){
      const out = outputs[v.track]; if (!out || !out[0]){ continue; }
      if (v.sf){ this.sfRun(v, out, N); if (v.ended){ this.alive.delete(v); const T = this.vs[v.track]; if (T){ const q = T.indexOf(v); if (q >= 0) T.splice(q, 1); } } continue; }   // SOUNDFONTS
      const L = out[0], R = out[1] || out[0], D = v.data, RD = v.rd, len = RD ? RD.N : D.length, inc0 = v.sr / sr;
      // FAST PATH (the usual case: a note that is just playing): gain, fade, pan and pitch don't move in this block
      const gc = v.trm ? null : v.g.constFrom(tb0), fc = v.f.constFrom(tb0), pc = v.apn ? null : v.pn.constFrom(tb0), rc = v.r.constFrom(tb0);
      if (gc != null && fc != null && pc != null && rc != null && v.t0 <= tb0 && v.end >= tb1){
        const k = gc * fc, a = (pc + 1) * Math.PI / 4, kl = k * Math.cos(a), kr = k * Math.sin(a), inc = rc * inc0; let p = v.pos;
        if (RD){ const st = !RD.tw && v.data2, [ll, rl, lr, rr] = st ? stBal(pc) : ZERO4;   // REVERSED / SHAPED (Rd): the same, read through the reader
          for (let i = 0; i < N; i++){ if (p >= len - 1){ v.ended = true; break; } const j = p | 0, f = p - j, d0 = RD.at(0, j), x = d0 + (RD.at(0, j + 1) - d0) * f;
            if (st){ const e0 = RD.at(1, j), y = e0 + (RD.at(1, j + 1) - e0) * f; L[i] += (x * ll + y * rl) * k; R[i] += (x * lr + y * rr) * k; }
            else { L[i] += x * kl; R[i] += x * kr; }
            p += inc; } }
        else if (v.data2){ const D2 = v.data2, [ll, rl, lr, rr] = stBal(pc);   // a stereo pad: the StereoPannerNode's balance law, as the page plays it
          for (let i = 0; i < N; i++){ if (p >= len - 1){ v.ended = true; break; } const j = p | 0, f = p - j, x = D[j] + (D[j + 1] - D[j]) * f, y = D2[j] + (D2[j + 1] - D2[j]) * f;
            L[i] += (x * ll + y * rl) * k; R[i] += (x * lr + y * rr) * k; p += inc; } }
        else for (let i = 0; i < N; i++){ if (p >= len - 1){ v.ended = true; break; } const j = p | 0, x = D[j] + (D[j + 1] - D[j]) * (p - j); L[i] += x * kl; R[i] += x * kr; p += inc; }
        v.pos = p;
        if (v.ended){ this.alive.delete(v); const T = this.vs[v.track]; if (T){ const q = T.indexOf(v); if (q >= 0) T.splice(q, 1); } }
        continue;
      }
      v.g.prune(tb0); v.f.prune(tb0); v.pn.prune(tb0); v.r.prune(tb0);   // (no array per voice per block)
      for (let i = 0; i < N; i++){
        const t = currentTime + i * dt;
        if (t < v.t0) continue;
        if (t >= v.end || v.pos >= len - 1){ v.ended = true; break; }
        const p = v.pos, j = p | 0, d0 = RD ? RD.at(0, j) : D[j], d1 = RD ? RD.at(0, j + 1) : D[j + 1], x = d0 + (d1 - d0) * (p - j), D2 = RD && RD.tw ? null : v.data2;
        let g = v.g.at(t); if (v.trm) g += v.trm.d * Math.sin(2 * Math.PI * v.trm.hz * (t - v.trm.t));
        let pan = v.pn.at(t); if (v.apn) pan = Math.max(-1, Math.min(1, pan + v.apn.d * Math.sin(2 * Math.PI * v.apn.hz * (t - v.apn.t))));
        const gf = g * v.f.at(t);
        if (D2){ const e0 = RD ? RD.at(1, j) : D2[j], e1 = RD ? RD.at(1, j + 1) : D2[j + 1], y2 = e0 + (e1 - e0) * (p - j), [ll, rl, lr, rr] = stBal(pan); L[i] += (x * ll + y2 * rl) * gf; R[i] += (x * lr + y2 * rr) * gf; }
        else { const y = x * gf, a = (pan + 1) * Math.PI / 4;   // StereoPannerNode, mono in: equal power
          L[i] += y * Math.cos(a); R[i] += y * Math.sin(a); }
        v.pos += v.r.at(t) * inc0;
      }
      if (v.ended){ this.alive.delete(v); const T = this.vs[v.track]; if (T){ const k = T.indexOf(v); if (k >= 0) T.splice(k, 1); } }
    }
    return true;
  }
}
registerProcessor('thunder-core', ThunderCore);
