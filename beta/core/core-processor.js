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
     {op:'play', gen, mode, o, t0, rs}  start: o as makeSeq takes it; rs {ref, row, step}: RESUME (the page played that step)
     {op:'stop', at}  ·  {op:'loop', l}  ·  {op:'bpm', v}  ·  {op:'perf', xf, mac, fill}  ·  {op:'lxp', st}  ·  {op:'lead', s}
     {op:'test', id, mode, max, opt}  run the sequencer at once (sim, seeded random): tools/seq-golden.mjs --core
   Messages out:
     {op:'ready', v}  ·  {op:'rows', rows, time, bpm, tks, alive}  ·  {op:'end'}  ·  {op:'test', id, rows}  ·
     {op:'error', where, message} */

const CORE_V = 3;   // the message set's version: index.html's CORE_WANT must match (3: patch sync)
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
  prune(t){ if (this.ev.length < 48) return; const v = this.at(t), fut = this.ev.filter(e => e.t > t || (e.k === 3 && e.t + e.d > t)); this.v0 = v; this.ev = [{k:0, t, v}, ...fut]; }
  at(t){
    let v = this.v0, pt = 0, st = null;   // st: a target in progress {from, t, to, tc}
    const val = x => st ? st.to + (st.from - st.to) * Math.exp(-(x - st.t) / st.tc) : v;
    for (const e of this.ev){
      if (e.k === 1){ if (t < e.t){ const a = val(pt); return e.t > pt ? a + (e.v - a) * (t - pt) / (e.t - pt) : e.v; } v = e.v; st = null; pt = e.t; continue; }
      if (t < e.t) return val(t);
      if (e.k === 0){ v = e.v; st = null; pt = e.t; }
      else if (e.k === 2){ st = {from:val(e.t), t:e.t, to:e.v, tc:e.tc}; pt = e.t; }
      else if (e.k === 3){ if (t < e.t + e.d) return e.fn(t); v = e.fn(e.t + e.d); st = null; pt = e.t + e.d; }
    }
    return val(t);
  }
}

/* LIVE ENGINES (phase 2c): the bass, voice, acid / machine and …pump synth nodes are processors in this same audio-thread
   scope; each registers itself by the page's key (globalThis.__tcNodes, from its processorOptions.cid). The core drives one
   by giving it an override (proc.__ov): Auto timelines for its AudioParams (rate, gate, trig, leg, acc, syl, same, wob, and
   r/g/t/l/v + lane on the …pump synth), read in its process() through __tcOv, from the time the core first set each one;
   and the note's level and pan (the page's GainNode and StereoPanner after the node, which the core can't reach), applied
   to its output by __tcPost (the page sets those nodes to 1 and centre when the core takes the node over). */
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
  const ov = proc.__ov; if (!ov || !o || !o[0]) return; const L = o[0], R = o[1] || null, n = L.length, dt = 1 / sampleRate;
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
// One driven parameter: a timeline that takes over from the node's own AudioParam at its first event
function tcParam(ov, k, t){ let A = ov.p.get(k); if (!A){ A = new Auto(0); A.from = t; ov.p.set(k, A); } return A; }

const POLY = 4;
class ThunderCore extends AudioWorkletProcessor {
  constructor(){
    super();
    this.seq = null; this.LEAD = 0.25; this.kv = []; this.vs = []; this.alive = new Set(); this.ended = false; this.lnodes = new Map(); this.voxG = new Map();
    this.clk = new Auto(0); this.clkOn = false; this.clkBase = 0;   // the beat clock (output NT): beat + 1 while playing, 0 stopped
    this.port.onmessage = e => { try { this.msg(e.data || {}); } catch(err){ this.port.postMessage({op:'error', where:(e.data && e.data.op) || '?', message:String(err && err.stack || err)}); } };
    this.port.postMessage({op:'ready', v:CORE_V});
  }
  msg(d){
    switch (d.op){
      case 'load': S = d.S; if (d.vars){ if ('vtrk' in d.vars) vtrk = d.vars.vtrk; if ('NSLOT' in d.vars) NSLOT = d.vars.NSLOT; if (d.vars.TFX) TFX = d.vars.TFX; } busPlanH = null; busPlanVal = null; return;
      case 'song': { if (!S){ S = d.S; return; } for (const k of Object.keys(S)) if (!(k in d.S)) delete S[k]; Object.assign(S, d.S); busPlanH = null; busPlanVal = null; return; }   // in place: the running sequencer holds this object
      case 'patch': {   // only what changed (coreDiff on the page): [key, null, value] · [key, index, item] · [key, 'len', n] · [key, 'del']
        if (!S) return;
        for (const [k, i, v] of d.ops){ if (i === null) S[k] = v; else if (i === 'del') delete S[k]; else if (i === 'len'){ if (Array.isArray(S[k])) S[k].length = v; }
          else { if (!Array.isArray(S[k])) S[k] = []; S[k][i] = v; } }
        busPlanH = null; busPlanVal = null; return; }
      case 'vars': if (d.vars){ if ('vtrk' in d.vars) vtrk = d.vars.vtrk; if ('NSLOT' in d.vars) NSLOT = d.vars.NSLOT; if (d.vars.TFX) TFX = d.vars.TFX; } return;
      case 'kit': this.kv = d.kv || []; kit = {voices:this.kv}; return;
      case 'buf': if (d.data) coreBufs.set(d.pad, {data:d.data, sr:d.sr}); else coreBufs.delete(d.pad); return;
      case 'test': {
        const o = d.opt || {}, rows = coreSeeded(() => { const out = [];
          const sq = makeSeq(S, d.mode || 'song', {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:true});
          for (let n = 0; sq.alive() && n < (d.max || 20000); n++){ const r = sq.step(); out.push({t:coreNum(r.t), evs:r.evs.map(corePlain)}); }
          return out; });
        this.port.postMessage({op:'test', id:d.id, rows}); return; }
      case 'play': return this.play(d);
      case 'stop': { this.seq = null; const at = d.at == null ? currentTime : d.at; this.stopAll(at); this.clkOn = false; this.clk.cancel(at); this.clk.set(0, at); return; }
      case 'loop': if (this.seq) this.seq.setLoop(d.l || null); return;
      case 'bpm': if (this.seq) this.seq.bpm = d.v; return;
      case 'perf': if (!perfLive.init) perfLiveInit(); if (d.xf != null) perfLive.xf = d.xf; if (d.mac) perfLive.mac = d.mac.slice(); if (d.fill != null) perfLive.fill = !!d.fill; return;
      case 'lxp': lxp.st = d.st | 0; return;
      case 'stats': this.port.postMessage({op:'stats', id:d.id, n:this.nN || 0, late:this.nLate || 0, worst:this.worst || 0, voices:this.alive.size, lead:this.leadMin == null ? null : this.leadMin}); this.nN = this.nLate = this.worst = 0; this.leadMin = null; return;
      case 'sig': { const j = JSON.stringify(S); let h = 0; for (let i = 0; i < j.length; i++) h = (h * 31 + j.charCodeAt(i)) | 0; this.port.postMessage({op:'sig', len:j.length, h}); return; }
      case 'dbg': this.noSampler = !!d.noSampler; return;   // measuring: the page plays every note (the core only sequences)
      case 'die': this.dead = true; this.seq = null; this.alive.clear(); return;   // the page let this core go (Reset audio engine)
      case 'lead': this.LEAD = Math.max(0.05, Math.min(2, +d.s || 0.25)); return;
    }
  }
  play(d){
    this.stopAll(currentTime); this.clkOn = false; this.clk.cancel(currentTime); this.clk.set(0, currentTime);
    const seq = makeSeq(S, d.mode || 'song', Object.assign({}, d.o || {}, {live:true}));
    // RESUME: run silently up to the step the song stopped on; the page played that step, the core carries on after it
    if (d.rs){ let g = 0, first = null;
      while (seq.alive() && g++ < 4096){ const r = seq.step(), p = r.pos && r.pos[d.rs.ref]; if (p && (p.row > d.rs.row || (p.row === d.rs.row && p.step >= d.rs.step))){ first = r; break; } }
      if (first){ const dt = d.t0 - first.t; seq.base += dt; seq.time += dt; if (seq.btT != null) seq.btT += dt; }
      else seq.time = d.t0; }
    else seq.time = d.t0;
    this.seq = seq; this.ended = false; this.gen = d.gen | 0;
  }

  /* ---- the sampler: the page player's rendered-pad path (makePlayer: startNote, kill, pitchMods, modVoice, play) ---- */
  T(t){ return this.vs[t] || (this.vs[t] = []); }
  relTc(v){ return v.syn ? v.rel / 4.6 : 0.004; }
  live1(v, at){ return v && !v.ended && !(v.stopAt != null && v.stopAt <= at); }
  kill(v, at, tc = 0.004){
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
    const k = this.kv[ev.pad]; if (!k || k.live || k.sf) return false;
    if ((ev.vk || ev.tw) && !ev.st) return false;
    if (ev.st) return false;   // stretched (SYNC) notes: the page's stretch cache, for now
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
    const c = coreBufs.get(ev.pad), kv = this.kv[ev.pad]; if (!c || !kv) return;
    { const ld = ev.t - currentTime; this.nN = (this.nN || 0) + 1; if (ld < 0){ this.nLate = (this.nLate || 0) + 1; this.worst = Math.max(this.worst || 0, -ld); } if (this.leadMin == null || ld < this.leadMin) this.leadMin = ld; }   // timing stats ({op:'stats'})
    const T = this.T(ev.track);
    for (let k = T.length - 1; k >= 0; k--) if (!this.live1(T[k], ev.t)) T.splice(k, 1);
    if (!ev.poly){ for (const v of T) this.kill(v, ev.t, v.syn ? Math.min(this.relTc(v), 0.05) : 0.004); T.length = 0; }
    else while (T.length >= POLY) this.kill(T.shift(), ev.t, 0.006);
    const ck = kv.choke || 0;
    if (ck) for (let k = 0; k < NT; k++) if (k !== ev.track) for (const v of this.T(k)) if (v.choke === ck) this.kill(v, ev.t);
    const data = ev.rev ? (c.rev || (c.rev = c.data.slice().reverse())) : c.data, dur = data.length / c.sr;
    const r = new Auto(ev.rate), g = new Auto(ev.gain), f = new Auto(1), pn = new Auto(Math.max(-1, Math.min(1, (ev.pan - 128) / 127)));
    if (ev.slide && ev.slide.dur > 0){ r.set(ev.slide.from, ev.t); r.lin(ev.rate, ev.t + ev.slide.dur); }
    const t0n = Math.max(ev.t, currentTime), hz = x => ((x >> 4) || 1) * (this.seq ? this.seq.bpm : S.bpm) / 60, dep = x => (x & 15) / 15;
    let trm = null, apn = null;
    if (ev.fdi > 0){ g.set(0, t0n); g.lin(ev.gain, t0n + ev.fdi); }
    if (ev.trm && dep(ev.trm) > 0){ const k = dep(ev.trm) / 2; trm = {hz:hz(ev.trm), d:ev.gain * k, t:t0n}; if (!(ev.fdi > 0)) g.v0 = ev.gain * (1 - k); }
    if (ev.apn && dep(ev.apn) > 0) apn = {hz:hz(ev.apn), d:dep(ev.apn), t:t0n};
    const syn = !!kv.syn, v = {data, sr:c.sr, r, g, f, pn, trm, apn, base:ev.rate, rate:ev.rate, choke:ck, col:ev.col, ended:false, stopAt:null, syn, rel:syn ? kv.rel || 0 : 0, track:ev.track, end:Infinity, pos:0, t0:0};
    if (!ev.slide) this.pitchMods(v, ev.t, ev.rate, ev);
    const off = ev.ofsSec != null ? (ev.rev ? Math.max(0, dur - ev.ofsSec - ev.durSec) : ev.ofsSec) : Math.max(0, Math.min(255, ev.ofs || 0)) / 256 * dur;
    if (off > 0.0005){ f.set(0, t0n); f.lin(1, t0n + 0.0015); }
    v.t0 = t0n; v.pos = Math.min(off, Math.max(0, dur - 0.001)) * c.sr;
    this.alive.add(v);
    if (ev.cut != null) this.kill(v, ev.cut);
    if (ev.rel != null) this.kill(v, ev.rel, syn ? this.relTc(v) : 0.05);
    T.push(v);
  }
  // One event, as the page's player.play() does for the ones that touch its voices. Returns true when the core sounded it.
  ev(ev){
    // the tempo-locked effects' beat clock, as makePlayer's play('beat') ramps its ConstantSource (here + 1: see clkBus)
    if (ev.type === 'beat'){ const at = Math.max(ev.t, currentTime), A = this.clk;
      if (!this.clkOn){ this.clkOn = true; this.clkBase = Math.floor(ev.b / 16) * 16; A.set(ev.b - this.clkBase + 1, at); return true; }
      const v = ev.b - this.clkBase; A.lin(v + 1, at); if (v >= 16){ this.clkBase += 16; A.set(v - 16 + 1, at); } return true; }
    if (ev.type === 'stp'){ for (let k = 0; k <= NT; k++) if (ev.all || k === ev.track){ for (const v of this.T(k)) this.kill(v, ev.t, 0.004); this.T(k).length = 0; } return false; }
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
  stopAll(at){ for (const v of this.alive) this.kill(v, at); for (const T of this.vs) if (T) T.length = 0;
    for (const L of this.lnodes.values()){ if (L.cur) this.kill(L.cur, at); L.sylN = 0; if (L.proc.__ov) L.proc.__ov.until = at + 4; }   // a live note lets go; its release rings, then the page has the node back
    this.voxG.clear(); }

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
    const ov = tcOver(L.proc); ov.until = Infinity;
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
    // 1. step the song up to LEAD ahead: the core books its own notes, the rows go up for the page's part
    const s = this.seq;
    if (s){
      // The audio thread itself was held up (a screenshot, the system busy): rather than racing through the rows it missed
      // (owner, Fold: "the speed increased"), the song moves on from now, as the page's pump does.
      if (s.alive() && s.base != null && s.time < currentTime - 0.03){ const d = currentTime + 0.02 - s.time; s.base += d; s.time += d; if (s.btT != null) s.btT += d; }
      const rows = []; let n = 0;
      while (s.alive() && s.time < currentTime + this.LEAD && n++ < 3){   // at most 3 steps a block: a longer LEAD (screen off) fills over a few blocks, not in one burst that overruns it
        const r = s.step();
        for (const e of r.evs) if (this.ev(e)) e.core = true;
        rows.push({t:r.t, evs:r.evs, pos:r.pos, tks:r.tks, line:r.line, bpm:s.bpm});
      }
      if (rows.length) this.port.postMessage({op:'rows', gen:this.gen, rows, time:s.time, bpm:s.bpm, tks:s.tks, base:s.base, btT:s.btT, alive:s.alive()});
      if (!s.alive() && !this.ended){ this.ended = true; this.port.postMessage({op:'end', gen:this.gen, time:s.time}); }
    }
    // 2. sound the voices into their tracks' outputs
    const N = (outputs[0] && outputs[0][0] && outputs[0][0].length) || 128, sr = sampleRate, dt = 1 / sr;
    { const C = outputs[NT] && outputs[NT][0]; if (C){ const A = this.clk; A.prune(currentTime); if (!A.ev.length && A.v0 === 0) C.fill(0); else for (let i = 0; i < N; i++) C[i] = A.at(currentTime + i * dt); } }
    const tb0 = currentTime, tb1 = currentTime + N * dt;
    for (const v of this.alive){
      const out = outputs[v.track]; if (!out || !out[0]){ continue; }
      const L = out[0], R = out[1] || out[0], D = v.data, len = D.length, inc0 = v.sr / sr;
      // FAST PATH (the usual case: a note that is just playing): gain, fade, pan and pitch don't move in this block
      const gc = v.trm ? null : v.g.constFrom(tb0), fc = v.f.constFrom(tb0), pc = v.apn ? null : v.pn.constFrom(tb0), rc = v.r.constFrom(tb0);
      if (gc != null && fc != null && pc != null && rc != null && v.t0 <= tb0 && v.end >= tb1){
        const k = gc * fc, a = (pc + 1) * Math.PI / 4, kl = k * Math.cos(a), kr = k * Math.sin(a), inc = rc * inc0; let p = v.pos;
        for (let i = 0; i < N; i++){ if (p >= len - 1){ v.ended = true; break; } const j = p | 0, x = D[j] + (D[j + 1] - D[j]) * (p - j); L[i] += x * kl; R[i] += x * kr; p += inc; }
        v.pos = p;
        if (v.ended){ this.alive.delete(v); const T = this.vs[v.track]; if (T){ const q = T.indexOf(v); if (q >= 0) T.splice(q, 1); } }
        continue;
      }
      for (const A of [v.g, v.f, v.pn, v.r]) A.prune(tb0);
      for (let i = 0; i < N; i++){
        const t = currentTime + i * dt;
        if (t < v.t0) continue;
        if (t >= v.end || v.pos >= len - 1){ v.ended = true; break; }
        const p = v.pos, j = p | 0, x = D[j] + (D[j + 1] - D[j]) * (p - j);
        let g = v.g.at(t); if (v.trm) g += v.trm.d * Math.sin(2 * Math.PI * v.trm.hz * (t - v.trm.t));
        let pan = v.pn.at(t); if (v.apn) pan = Math.max(-1, Math.min(1, pan + v.apn.d * Math.sin(2 * Math.PI * v.apn.hz * (t - v.apn.t))));
        const y = x * g * v.f.at(t), a = (pan + 1) * Math.PI / 4;   // StereoPannerNode, mono in: equal power
        L[i] += y * Math.cos(a); R[i] += y * Math.sin(a);
        v.pos += v.r.at(t) * inc0;
      }
      if (v.ended){ this.alive.delete(v); const T = this.vs[v.track]; if (T){ const k = T.indexOf(v); if (k >= 0) T.splice(k, 1); } }
    }
    return true;
  }
}
registerProcessor('thunder-core', ThunderCore);
