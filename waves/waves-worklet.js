/* ═══ …waves audio engine + worklet ═══
   Self-contained, no imports. Standalone, this file is both engine and worklet.
   In Thunder, the WavesEngine class is instantiated inside Thunder's processor
   instead and this wrapper is dropped — that's the whole integration seam.

   Two AudioWorklets cannot share buffers without SharedArrayBuffer (needs
   COOP/COEP, which GitHub Pages can't send), so engine-as-a-class is mandatory.

   Signals: pitch is in semitones from middle C (0 = C4), gates are 0/1,
   modulation is roughly −1…1, audio is −1…1. Buses are stereo.

   The WebAssembly modules come from ...Thunder (dsp/thunder-dsp.wasm: Plaits and
   Elements; dsp/tfx.wasm: the effects). The page fetches them and hands the
   bytes over in processorOptions, since a worklet can't fetch. */

(() => {   // everything lives in this scope, so a page that loads it (…Thunder Plus) keeps its own names
const BLOCK = 128, BUS_COUNT = 8, PROTOCOL = 2, TARGET = 'waves';

/* ── shared helpers ───────────────────────────────────────────────────────── */
const TS = 4096, SIN = new Float32Array(TS + 1);
for (let i = 0; i <= TS; i++) SIN[i] = Math.sin(i / TS * 2 * Math.PI);
const sinT = p => { p -= Math.floor(p); const x = p * TS, i = x | 0; return SIN[i] + (SIN[i + 1] - SIN[i]) * (x - i); };
const frac = x => x - Math.floor(x);
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
const blep = (t, dt) => { if (t < dt){ t /= dt; return t + t - t * t - 1; } if (t > 1 - dt){ t = (t - 1) / dt; return t * t + t + t + 1; } return 0; };
const hzOf = pitch => 261.6256 * Math.pow(2, pitch / 12);
function rng(seed){ let s = (seed >>> 0) || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; }
let seedN = 12345;
const nextSeed = () => (seedN = (seedN * 1103515245 + 12345) >>> 0);
/* Gentle limiter on the speakers: untouched below 0.8, rounds off into ±1 above. */
const soft = x => { const a = Math.abs(x); return a < .8 ? x : Math.sign(x) * (.8 + .2 * Math.tanh((a - .8) / .2)); };
/* Did this input get a cable? (an unpatched input reads 0) */
const linked = (io, name) => io.linked.has(name);

/* ── WebAssembly from ...Thunder ──────────────────────────────────────────── */
const WASI = { wasi_snapshot_preview1: { fd_close: () => 0, fd_seek: () => 0, fd_write: () => 0, proc_exit: () => 0 } };
const WASM = { dsp: null, tfx: null, tfxX: null };
/* o: {dspBytes, tfxBytes} (bytes, compiled here) or {dspModule, tfxModule} (already compiled, as on a page). */
function wasmInit(o){
  try { if (o.dspModule) WASM.dsp = o.dspModule; else if (o.dspBytes) WASM.dsp = new WebAssembly.Module(o.dspBytes); } catch (e) { WASM.dsp = null; }
  try {
    if (o.tfxModule || o.tfxBytes){
      WASM.tfx = o.tfxModule || new WebAssembly.Module(o.tfxBytes);
      const x = new WebAssembly.Instance(WASM.tfx, WASI).exports;
      if (x._initialize) x._initialize();
      WASM.tfxX = x;
    }
  } catch (e) { WASM.tfx = WASM.tfxX = null; }
}
/* Plaits and Elements are single static voices in the dsp module, so each voice gets its own instance. */
function dspInstance(){
  if (!WASM.dsp) return null;
  const x = new WebAssembly.Instance(WASM.dsp, WASI).exports;
  if (x._initialize) x._initialize();
  return x;
}
/* Feeds fixed-rate wasm blocks (Plaits 48 kHz, Elements 32 kHz) out at the context's rate. */
class Resampler {
  constructor(rateIn, sr, chunk, render){
    this.step = rateIn / sr; this.chunk = chunk; this.render = render;
    this.a = new Float32Array(chunk + 1); this.b = new Float32Array(chunk + 1); this.p = chunk;
  }
  next(out){ // out: [a, b] for this sample
    while (this.p >= this.chunk){
      this.a[0] = this.a[this.chunk]; this.b[0] = this.b[this.chunk];
      this.render(this.a, this.b); this.p -= this.chunk;
    }
    const i = this.p | 0, f = this.p - i;
    out[0] = this.a[i] + (this.a[i + 1] - this.a[i]) * f;
    out[1] = this.b[i] + (this.b[i + 1] - this.b[i]) * f;
    this.p += this.step;
  }
}

/* ── module definitions ─────────────────────────────────────────────────────
   create(sr) → state.  process(state, io, ctx) renders one BLOCK.
   io.in[name]/io.out[name] are Float32Array(BLOCK); io.p = live params (with
   modulation applied); io.linked = the inputs that have cables.
   scope 'poly' → one state per voice when anything polyphonic feeds it, else one ·
   'mono' → always one state · 'root' → always one per voice (Host In).          */

const HostIn = {
  type:'hostIn', scope:'root', cost:.02,
  inputs:{}, outputs:{ pitch:'cv', gate:'gate', vel:'cv' },
  params:{ transpose:[0,-48,48,1] },
  create:() => ({}),
  process(st, io, ctx){
    const n = ctx.notes[ctx.voice], tr = io.p.transpose, on = n && n.gate > 0;
    io.out.pitch.fill(n ? n.pitch + tr : 0); io.out.gate.fill(on ? 1 : 0); io.out.vel.fill(on ? n.vel : 0);
  }
};

const VCO = {
  type:'vco', scope:'poly', cost:1,
  inputs:{ pitch:'cv', fm:'cv' }, outputs:{ out:'audio' },
  params:{ wave:[1,0,3,1], tune:[0,-24,24,1], fine:[0,-100,100,1] },
  create:() => ({ phase:Math.random() }),
  process(st, io, ctx){
    const v = Math.pow(2, (io.p.tune + io.p.fine / 100) / 12), w = io.p.wave | 0;
    for (let i = 0; i < ctx.block; i++){
      const dt = Math.min(hzOf(io.in.pitch[i] + io.in.fm[i]) * v / ctx.sr, .5);
      st.phase += dt; st.phase -= Math.floor(st.phase);
      const t = st.phase;
      io.out.out[i] = w === 0 ? (2 * t - 1 - blep(t, dt)) * .8
        : w === 1 ? ((t < .5 ? 1 : -1) + blep(t, dt) - blep(frac(t + .5), dt)) * .8
        : w === 2 ? 4 * Math.abs(t - .5) - 1 : sinT(t);
    }
  }
};

/* FM from ...Thunder: 2-op, feedback, 808-style metal and cross-modulation. */
const RATIOS = [0.25,0.5,0.75,1,1.25,1.5,1.75,2,2.5,3,3.5,4,4.5,5,6,7,8,9,10,11,12,13,14,16];
const FM = {
  type:'fm', scope:'poly', cost:1.2,
  inputs:{ pitch:'cv', fm:'cv', index:'cv' }, outputs:{ out:'audio' },
  params:{ model:[0,0,3,1], tune:[0,-24,24,1], fine:[0,-100,100,1], ratio:[.13,0,1,.01], index:[.4,0,1,.01] },
  create:() => ({ p:0, p1:0, p2:0, pa:0, pb:0 }),
  process(st, io, ctx){
    const md = io.p.model | 0, v = Math.pow(2, (io.p.tune + io.p.fine / 100) / 12), A = io.p.ratio;
    const ratio = RATIOS[Math.round(A * (RATIOS.length - 1))];
    const r1 = 1.4142 + A * 2.4, r2 = 1.6818 + A * 1.3, r3 = 2.613 + A * 3.1;
    for (let i = 0; i < ctx.block; i++){
      const inc = Math.min(hzOf(io.in.pitch[i] + io.in.fm[i]) * v / ctx.sr, .5), B = clamp(io.p.index + io.in.index[i], 0, 1);
      let y;
      if (md === 0){ const I = B * B * 12 * .159155; y = sinT(st.p + I * sinT(st.p * ratio)); }
      else if (md === 1){ const fb = B * 1.8 * .159155; y = sinT(st.p + fb * (st.p1 + st.p2) * .5); st.p2 = st.p1; st.p1 = y; }
      else if (md === 2){ const I = B * B * 9 * .159155; y = .5 * (sinT(st.p + I * sinT(st.p * r1)) + sinT(st.p * r2 + I * sinT(st.p * r3))); }
      else { const I = B * 1.8 * .159155, a = sinT(st.p + I * st.pb), b = sinT(st.p * ratio + I * st.pa); st.pa = a; st.pb = b; y = (a + b) * .5; }
      st.p += inc; if (st.p > 1e6) st.p -= Math.floor(st.p);
      io.out.out[i] = y;
    }
  }
};

/* Additive from ...Thunder: harmonic, odd, membrane, bell, bar and chord. A gate restarts the partials' decay. */
const MEM = [1,1.594,2.136,2.296,2.653,2.918,3.156,3.501,3.6,3.652,4.06,4.154];
const BELL = [0.5,1,1.183,1.506,2,2.514,2.662,3.011,4.166,5.433,6.796,8.215];
const BELLA = [0.9,1,0.7,0.6,0.55,0.45,0.4,0.35,0.28,0.22,0.16,0.12];
const BAR = [1,2.756,5.404,8.933,13.344,18.638];
const CHORDS = [[0,12],[0,7],[0,7,12],[0,4,7],[0,3,7],[0,2,7],[0,5,7],[0,4,7,10],[0,3,7,10],[0,4,7,11],[0,3,7,10,14],[0,3,6],[0,4,8],[0,5,10,15]];
function partials(md, A, B){
  const rs = [], am = [], dm = [];
  if (md <= 1){ const n = 1 + Math.round(A * 31); for (let k = 0; k < n; k++){ const r = md === 1 ? 2 * k + 1 : k + 1; rs.push(r); am.push(Math.pow(r, -(0.4 + B * 1.6))); dm.push(k * 3); } }
  else if (md === 2){ const n = 2 + Math.round(A * 10); for (let k = 0; k < n; k++){ rs.push(MEM[k]); am.push(1 / (1 + k * .7)); dm.push(B * B * 50 * k); } }
  else if (md === 3){ const n = 2 + Math.round(A * 10); for (let k = 0; k < n; k++){ rs.push(BELL[k]); am.push(BELLA[k]); dm.push((0.3 + B * B * 14) * BELL[k]); } }
  else if (md === 4){ const s = 0.12 + A * 0.85; BAR.forEach((r, k) => { rs.push(r); am.push(Math.pow(s, k)); dm.push((0.5 + B * B * 12) * r); }); }
  else { for (const sem of CHORDS[Math.round(A * (CHORDS.length - 1))]){ const r = Math.pow(2, sem / 12); rs.push(r, r * 2, r * 3); am.push(1, B * .5, B * .33); dm.push(0, 2, 4); } }
  let sum = 0, mx = 0; for (const a of am){ sum += a; if (a > mx) mx = a; }
  const norm = 1 / Math.max(.5 * sum + .5 * mx, 1e-6);
  return { rs, am: am.map(a => a * norm), dm };
}
const Additive = {
  type:'additive', scope:'poly', cost:2,
  inputs:{ pitch:'cv', gate:'gate' }, outputs:{ out:'audio' },
  params:{ model:[0,0,5,1], tune:[0,-24,24,1], shape:[.3,0,1,.01], color:[.4,0,1,.01] },
  create:() => ({ ph:0, t:0, g:0, key:'', P:null, gains:new Float64Array(40) }),
  process(st, io, ctx){
    const key = (io.p.model | 0) + ':' + io.p.shape.toFixed(2) + ':' + io.p.color.toFixed(2);
    if (key !== st.key){ st.key = key; st.P = partials(io.p.model | 0, io.p.shape, io.p.color); }
    const P = st.P, n = P.rs.length, gate = linked(io, 'gate'), v = Math.pow(2, io.p.tune / 12);
    const inc0 = hzOf(io.in.pitch[0]) * v / ctx.sr;
    if (gate){ const g = io.in.gate[0] > .5; if (g && !st.g) st.t = 0; st.g = g; }
    let live = 0;
    for (let k = 0; k < n; k++){
      if (P.rs[k] * inc0 > .45){ st.gains[k] = 0; continue; }
      st.gains[k] = P.am[k] * (gate ? Math.exp(-st.t * P.dm[k]) : 1); live = k + 1;
    }
    for (let i = 0; i < ctx.block; i++){
      const inc = hzOf(io.in.pitch[i]) * v / ctx.sr;
      let y = 0; for (let k = 0; k < live; k++){ const g = st.gains[k]; if (g > 1e-5) y += g * sinT(st.ph * P.rs[k]); }
      st.ph += inc; if (st.ph > 1e6) st.ph -= Math.floor(st.ph);
      io.out.out[i] = y;
    }
    st.t += ctx.block / ctx.sr;
  }
};

/* Noise from ...Thunder. Pitch moves the 808 metal, digital and S&H noises. */
const HAT = [205.3,304.4,369.6,522.7,540,800];
const Noise = {
  type:'noise', scope:'poly', cost:.4,
  inputs:{ pitch:'cv' }, outputs:{ out:'audio' },
  params:{ model:[0,0,7,1], shape:[0,0,1,.01], color:[0,0,1,.01] },
  create:() => ({ R:rng(nextSeed()), c:0, held:0, b:new Float64Array(7), br:0, pw:0, ps:HAT.map((f, i) => i * .137), reg:0x7fff, acc:1, out:1, y:0 }),
  process(st, io, ctx){
    const md = io.p.model | 0, A = io.p.shape, B = io.p.color, R = st.R, b = st.b, sr = ctx.sr;
    for (let i = 0; i < ctx.block; i++){
      const inc = hzOf(io.in.pitch[i] - 24) / sr; let y = 0;
      if (md <= 3){
        if (st.c <= 0){
          st.c = 1 + Math.round(A * A * 63 * sr / 48000); const q = B > .01 ? Math.pow(2, 16 - B * 14) / 2 : 0, w = R() * 2 - 1;
          if (md === 0) y = w;
          else if (md === 1){ b[0]=.99886*b[0]+w*.0555179; b[1]=.99332*b[1]+w*.0750759; b[2]=.969*b[2]+w*.153852; b[3]=.8665*b[3]+w*.3104856;
            b[4]=.55*b[4]+w*.5329522; b[5]=-.7616*b[5]-w*.016898; y=(b[0]+b[1]+b[2]+b[3]+b[4]+b[5]+b[6]+w*.5362)*.11; b[6]=w*.115926; }
          else if (md === 2){ st.br = (st.br + .02 * w) / 1.02; y = st.br * 3.5; }
          else { y = (w - st.pw) * .5; st.pw = w; }
          if (q) y = Math.round(y * q) / q; st.held = y;
        }
        st.c--; y = st.held;
      } else if (md === 4){
        const ex = .6 + A * .8, w = .5 - B * .4;
        for (let k = 0; k < 6; k++){ let p = st.ps[k] + inc * 205.3 * Math.pow(HAT[k] / 205.3, ex) / 65.40639; p -= Math.floor(p); st.ps[k] = p; y += p < w ? 1 : -1; }
        y = (y / 6 - (2 * w - 1)) * .9;
      } else if (md === 5){
        st.acc = Math.min(64, st.acc + inc * Math.pow(2, A * 8 - 1) * 16);
        while (st.acc >= 1){ st.acc -= 1; const fb = (st.reg ^ (st.reg >> 1)) & 1; st.reg = (st.reg >> 1) | (fb << 14); if (B >= .5) st.reg = (st.reg & ~64) | (fb << 6); st.out = (st.reg & 1) ? 1 : -1; }
        y = st.out * .7;
      } else if (md === 6){
        const p = A * A * .25 * 48000 / sr, cf = .03 + B * .97, imp = R() < p ? R() * 2 - 1 : 0;
        st.y += (imp - st.y) * cf; y = clamp(st.y * 1.5 / (.2 + cf), -1, 1);
      } else {
        st.acc += inc * Math.pow(2, B * 6) * 4; if (st.acc >= 1){ st.acc -= Math.floor(st.acc); st.held = R() * 2 - 1; }
        st.y += (st.held - st.y) * (1 - A * .995); y = st.y;
      }
      io.out.out[i] = y;
    }
  }
};

/* Mutable Instruments Plaits (from ...Thunder's dsp module), at 48 kHz, resampled. */
const Plaits = {
  type:'plaits', scope:'poly', cost:4, heavy:true,
  inputs:{ pitch:'cv', gate:'gate', timbre:'cv', morph:'cv', harmonics:'cv' },
  outputs:{ out:'audio', aux:'audio' },
  params:{ engine:[8,0,23,1], harmonics:[.5,0,1,.01], timbre:[.5,0,1,.01], morph:[.5,0,1,.01], decay:[.5,0,1,.01], lpg:[.5,0,1,.01] },
  create(sr){
    const x = dspInstance(); if (!x) return { x:null };
    x.pl_init();
    const st = { x, io:null, i:0, fr:[0, 0] };
    st.rs = new Resampler(48000, sr, 12, (a, b) => {
      const io = st.io, i = st.i, p = io.p, g = linked(io, 'gate');
      x.pl_set(p.engine | 0, 60 + io.in.pitch[i], clamp(p.harmonics + io.in.harmonics[i], 0, 1), clamp(p.timbre + io.in.timbre[i], 0, 1),
        clamp(p.morph + io.in.morph[i], 0, 1), p.decay, p.lpg, 0, 0, 0, g ? 1 : 0);
      x.pl_trigger(g && io.in.gate[i] > .5 ? 1 : 0);
      x.pl_render(12);
      a.set(new Float32Array(x.memory.buffer, x.pl_out(), 12), 1); b.set(new Float32Array(x.memory.buffer, x.pl_aux(), 12), 1);
    });
    return st;
  },
  process(st, io, ctx){
    if (!st.x || ctx.quality === 'draft'){ io.out.out.fill(0); io.out.aux.fill(0); return; }
    st.io = io;
    for (let i = 0; i < ctx.block; i++){ st.i = i; st.rs.next(st.fr); io.out.out[i] = st.fr[0]; io.out.aux[i] = st.fr[1]; }
  }
};

/* Mutable Instruments Elements (from ...Thunder's dsp module), at 32 kHz, resampled. */
const Elements = {
  type:'elements', scope:'poly', cost:4, heavy:true,
  inputs:{ pitch:'cv', gate:'gate', strength:'cv' }, outputs:{ out:'audio', aux:'audio' },
  params:{ model:[0,0,2,1], contour:[0,0,1,.01], bow:[0,0,1,.01], blow:[0,0,1,.01], strike:[.8,0,1,.01], mallet:[.5,0,1,.01], timbre:[.5,0,1,.01],
    flow:[.5,0,1,.01], geometry:[.3,0,1,.01], brightness:[.5,0,1,.01], damping:[.6,0,1,.01], position:[.3,0,1,.01], space:[.3,0,1,.01] },
  create(sr){
    const x = dspInstance(); if (!x) return { x:null };
    x.el_init();
    const st = { x, io:null, i:0, fr:[0, 0] };
    st.rs = new Resampler(32000, sr, 16, (a, b) => {
      const io = st.io, i = st.i, p = io.p, g = linked(io, 'gate') ? io.in.gate[i] > .5 : true;
      x.el_set(p.model | 0, 60 + io.in.pitch[i], clamp(.8 + io.in.strength[i], 0, 1), p.contour, p.bow, p.blow, p.strike,
        p.timbre, p.flow, p.timbre, p.mallet, p.timbre, p.geometry, p.brightness, p.damping, p.position, p.space);
      x.el_gate(g ? 1 : 0);
      x.el_process(16);
      a.set(new Float32Array(x.memory.buffer, x.el_main(), 16), 1); b.set(new Float32Array(x.memory.buffer, x.el_aux(), 16), 1);
    });
    return st;
  },
  process(st, io, ctx){
    if (!st.x || ctx.quality === 'draft'){ io.out.out.fill(0); io.out.aux.fill(0); return; }
    st.io = io;
    for (let i = 0; i < ctx.block; i++){ st.i = i; st.rs.next(st.fr); io.out.out[i] = st.fr[0]; io.out.aux[i] = st.fr[1]; }
  }
};

const VCF = {
  type:'vcf', scope:'poly', cost:1,
  inputs:{ in:'audio', cutoff:'cv' }, outputs:{ out:'audio' },
  params:{ cutoff:[.5,0,1,.001], res:[.3,0,1,.001], mode:[0,0,2,1] },
  create:() => ({ ic1:0, ic2:0 }),
  process(st, io, ctx){
    const mode = io.p.mode | 0, q = 1 - io.p.res * .95;
    for (let i = 0; i < ctx.block; i++){
      const fc = Math.min(.45, (.01 + Math.pow(10, io.p.cutoff * 3) * .004) * Math.pow(2, io.in.cutoff[i] * 3) * 44100 / ctx.sr);
      const f = 2 * Math.sin(Math.PI * fc);
      const hp = io.in.in[i] - st.ic2 - q * st.ic1;
      const bp = f * hp + st.ic1; st.ic1 = f * hp + bp;
      const lp = f * bp + st.ic2; st.ic2 = f * bp + lp;
      if (!(Math.abs(st.ic1) < 1e4)) st.ic1 = st.ic2 = 0;
      io.out.out[i] = mode === 0 ? lp : mode === 1 ? bp : hp;
    }
  }
};

/* CV unpatched = fully open, so a VCA can also be a plain volume control. */
const VCA = {
  type:'vca', scope:'poly', cost:.2,
  inputs:{ in:'audio', cv:'cv' }, outputs:{ out:'audio' },
  params:{ gain:[1,0,4,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const g = io.p.gain, cv = linked(io, 'cv');
    for (let i = 0; i < ctx.block; i++) io.out.out[i] = io.in.in[i] * (cv ? io.in.cv[i] : 1) * g;
  }
};

const Mixer = {
  type:'mixer', scope:'poly', cost:.1,
  inputs:{ in1:'audio', in2:'audio', in3:'audio', in4:'audio' }, outputs:{ out:'audio' },
  params:{ l1:[.8,0,1.5,.01], l2:[.8,0,1.5,.01], l3:[.8,0,1.5,.01], l4:[.8,0,1.5,.01], level:[1,0,2,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const p = io.p, a = io.in.in1, b = io.in.in2, c = io.in.in3, d = io.in.in4, o = io.out.out;
    for (let i = 0; i < ctx.block; i++) o[i] = (a[i] * p.l1 + b[i] * p.l2 + c[i] * p.l3 + d[i] * p.l4) * p.level;
  }
};

/* ── effects: ...Thunder's track effects (Airwindows, DaisySP, Mutable Instruments) ──
   Stereo: Right in is optional (the left/mono input feeds both sides when it's empty). */
const u = v => clamp(v, 0, 1);
function effect(type, id, params, map, cost){
  return {
    type, scope:'mono', cost: cost || 1, effect:true,
    inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' }, params,
    create(sr){ const x = WASM.tfxX; if (!x) return { h:-1 }; const h = x.tfx_new(id, sr); return { h, x }; },
    destroy(st){ if (st.x && st.h >= 0) st.x.tfx_free(st.h); },
    process(st, io, ctx){
      const L0 = io.in.in, R0 = linked(io, 'inR') ? io.in.inR : io.in.in;
      if (!st.x || st.h < 0){ io.out.out.set(L0); io.out.outR.set(R0); return; }
      const x = st.x, buf = x.memory.buffer;
      const P = new Float32Array(buf, x.tfx_params(st.h), 12); P.fill(0); P.set(map(io.p, ctx));
      const L = new Float32Array(buf, x.tfx_l(), 128), R = new Float32Array(buf, x.tfx_r(), 128);
      L.set(L0); R.set(R0);
      x.tfx_process(st.h, ctx.block);
      const L2 = new Float32Array(x.memory.buffer, x.tfx_l(), 128), R2 = new Float32Array(x.memory.buffer, x.tfx_r(), 128);
      io.out.out.set(L2.subarray(0, ctx.block)); io.out.outR.set(R2.subarray(0, ctx.block));
    }
  };
}
const EQ = effect('eq', 6, { treble:[.5,0,1,.01], mid:[.5,0,1,.01], bass:[.5,0,1,.01], tfreq:[.4,0,1,.01], bfreq:[.4,0,1,.01], lowpass:[1,0,1,.01], hipass:[0,0,1,.01], out:[.5,0,1,.01] },
  p => [u(p.treble), u(p.mid), u(p.bass), u(p.lowpass), u(p.tfreq), u(p.bfreq), u(p.hipass), u(p.out)], .3);
const Comp = effect('comp', 7, { press:[.4,0,1,.01], speed:[.2,0,1,.01], mew:[1,0,1,.01], out:[1,0,1,.01], mix:[1,0,1,.01] },
  p => [u(p.press), u(p.speed), u(p.mew), u(p.out), u(p.mix)], .4);
const Reverb = effect('reverb', 8, { replace:[.5,0,1,.01], bright:[.5,0,1,.01], detune:[.5,0,1,.01], big:[1,0,1,.01], mix:[.25,0,1,.01] },
  p => [u(p.replace), u(p.bright), u(p.detune), u(p.big), u(p.mix)], 1.5);
/* TapeDelay2's tape runs through 88200 samples at "speed" 1–26 per sample, so speed sets the time. */
const Delay = effect('delay', 9, { time:[.375,.07,1.8,.001], regen:[.38,0,1,.01], tone:[.5,0,1,.01], reso:[0,0,1,.01], flutter:[0,0,1,.01], mix:[.25,0,1,.01] },
  (p, ctx) => { const sp = clamp(88200 / (clamp(p.time, .07, 1.8) * ctx.sr), 1, 26); return [Math.pow((sp - 1) / 25, .25), u(p.regen), u(p.tone), u(p.reso), u(p.flutter), u(p.mix)]; }, .8);
const Saturation = effect('saturation', 2, { drive:[.4,0,1,.01], hipass:[0,0,1,.01], out:[.8,0,1,.01], mix:[1,0,1,.01] },
  p => [.2 + u(p.drive) * .8, u(p.hipass), u(p.out), u(p.mix)], .4);
const Tape = effect('tape', 3, { input:[.5,0,1,.01], soften:[.5,0,1,.01], bump:[.5,0,1,.01], flutter:[.5,0,1,.01], out:[.5,0,1,.01], mix:[1,0,1,.01] },
  p => [u(p.input), u(p.soften), u(p.bump), u(p.flutter), u(p.out), u(p.mix)], .6);
const Chorus = effect('chorus', 4, { depth:[.5,0,1,.01], rate:[.41,0,1,.01], delay:[.24,0,1,.01], fb:[.21,0,1,.01], mix:[.5,0,1,.01] },
  p => [u(p.depth), .05 * Math.pow(160, u(p.rate)), 1 + u(p.delay) * 29, u(p.fb) * .95, u(p.mix)], .4);
const Crush = effect('crush', 5, { down:[.4,0,1,.01], bits:[8,1,16,1], smooth:[0,0,1,1], mix:[1,0,1,.01] },
  p => [u(p.down), Math.round(clamp(p.bits, 1, 16)), p.smooth >= .5 ? 1 : 0, u(p.mix)], .2);
const Rings = effect('rings', 0, { model:[0,0,5,1], poly:[0,0,2,1], note:[0,-24,36,1], structure:[.4,0,1,.01], bright:[.5,0,1,.01], damp:[.6,0,1,.01], pos:[.3,0,1,.01], input:[.5,0,1,.01], mix:[.8,0,1,.01] },
  p => [p.model | 0, [1, 2, 4][p.poly | 0] || 1, 60 + p.note, u(p.structure), u(p.bright), u(p.damp), u(p.pos), u(p.input) * 2, u(p.mix)], 3);
Rings.inputs = { in:'audio', inR:'audio', pitch:'cv' };
{ const map = Rings.process; Rings.process = function(st, io, ctx){ const n = io.p.note; io.p.note = n + io.in.pitch[0]; try { map.call(this, st, io, ctx); } finally { io.p.note = n; } }; }
const Clouds = effect('clouds', 1, { mode:[0,0,3,1], lofi:[0,0,1,1], freeze:[0,0,1,1], pos:[.1,0,1,.01], size:[.5,0,1,.01], pitch:[0,-24,24,1], density:[.6,0,1,.01],
    texture:[.5,0,1,.01], blend:[.5,0,1,.01], fb:[.2,0,1,.01], verb:[.5,0,1,.01], spread:[.5,0,1,.01] },
  p => [p.mode | 0, p.lofi >= .5 ? 1 : 0, p.freeze >= .5 ? 1 : 0, u(p.pos), u(p.size), p.pitch, u(p.density), u(p.texture), u(p.blend), u(p.fb), u(p.verb), u(p.spread)], 4);

/* ── control ──────────────────────────────────────────────────────────────── */
const LFO = {
  type:'lfo', scope:'mono', cost:.3,
  inputs:{ reset:'gate' }, outputs:{ out:'cv' },
  params:{ rate:[1,.02,40,.01], sync:[0,0,1,1], shape:[0,0,4,1], depth:[.5,0,1,.01] },
  create:() => ({ phase:0, r:0, held:0, R:rng(nextSeed()) }),
  process(st, io, ctx){
    const hz = io.p.sync >= .5 ? (ctx.transport.tempo / 60) * io.p.rate * .25 : io.p.rate, sh = io.p.shape | 0, d = io.p.depth;
    for (let i = 0; i < ctx.block; i++){
      const g = io.in.reset[i] > .5; if (g && !st.r) st.phase = 0; st.r = g;
      const before = st.phase;
      st.phase += hz / ctx.sr; st.phase -= Math.floor(st.phase);
      if (st.phase < before) st.held = st.R() * 2 - 1;
      const t = st.phase;
      io.out.out[i] = d * (sh === 1 ? (t < .5 ? 1 : -1) : sh === 2 ? 2 * t - 1 : sh === 3 ? 1 - 4 * Math.abs(t - .5) : sh === 4 ? st.held : sinT(t));
    }
  }
};

const ADSR = {
  type:'adsr', scope:'poly', cost:.2,
  inputs:{ gate:'gate' }, outputs:{ out:'cv' },
  params:{ a:[.01,0,4,.001], d:[.2,0,4,.001], s:[.7,0,1,.001], r:[.3,0,8,.001], depth:[1,0,1,.01] },
  create:() => ({ stage:0, level:0 }),
  process(st, io, ctx){
    const { a, d, s, r } = io.p, dt = 1 / ctx.sr, dep = io.p.depth;
    for (let i = 0; i < ctx.block; i++){
      const g = io.in.gate[i] > .5;
      if (g && (st.stage === 0 || st.stage === 4)) st.stage = 1;
      if (!g && st.stage !== 0 && st.stage !== 4) st.stage = 4;
      if (st.stage === 1){ st.level += dt / Math.max(a, 1e-4); if (st.level >= 1){ st.level = 1; st.stage = 2; } }
      else if (st.stage === 2) st.level += (s - st.level) * dt / Math.max(d, 1e-4) * 6;
      else if (st.stage === 4){ st.level -= st.level * dt / Math.max(r, 1e-4) * 6 + dt * 1e-3; if (st.level <= 0){ st.level = 0; st.stage = 0; } }
      io.out.out[i] = st.level * dep;
    }
  }
};

/* New random value on each clock (or at Rate when nothing is patched to Clock), with optional smoothing. */
const Random = {
  type:'random', scope:'mono', cost:.1,
  inputs:{ clock:'gate' }, outputs:{ out:'cv' },
  params:{ rate:[4,.05,40,.01], smooth:[0,0,1,.01], depth:[.5,0,1,.01] },
  create:() => ({ R:rng(nextSeed()), ph:0, c:0, held:0, y:0 }),
  process(st, io, ctx){
    const clk = linked(io, 'clock'), k = 1 - Math.pow(io.p.smooth, .15) * .9995;
    for (let i = 0; i < ctx.block; i++){
      let fire = false;
      if (clk){ const g = io.in.clock[i] > .5; fire = g && !st.c; st.c = g; }
      else { st.ph += io.p.rate / ctx.sr; if (st.ph >= 1){ st.ph -= 1; fire = true; } }
      if (fire) st.held = st.R() * 2 - 1;
      st.y += (st.held - st.y) * k;
      io.out.out[i] = st.y * io.p.depth;
    }
  }
};

const Attenuverter = {
  type:'atten', scope:'poly', cost:.05,
  inputs:{ in:'cv' }, outputs:{ out:'cv' },
  params:{ amount:[1,-1,1,.01], offset:[0,-1,1,.01] },
  create:() => ({}),
  process(st, io, ctx){ const a = io.p.amount, o = io.p.offset; for (let i = 0; i < ctx.block; i++) io.out.out[i] = io.in.in[i] * a + o; }
};

/* Step sequencer: plays its notes through Host In, like a keyboard, and sends clock, pitch and gate out. */
const RATES = [1, 2, 3, 4, 6, 8];
const Sequencer = {
  type:'seq', scope:'mono', cost:.05,
  inputs:{ clock:'gate', reset:'gate' }, outputs:{ pitch:'cv', gate:'gate', clock:'gate' },
  params:{ run:[1,0,1,1], rate:[3,0,5,1], steps:[8,1,8,1], length:[.5,.05,1,.01],
    s1:[0,-25,24,1], s2:[3,-25,24,1], s3:[7,-25,24,1], s4:[12,-25,24,1], s5:[10,-25,24,1], s6:[7,-25,24,1], s7:[3,-25,24,1], s8:[-25,-25,24,1] },
  create:() => ({ pos:-1, acc:1, c:0, rs:0, note:null, off:0, gateT:0, clkT:0, pitch:0 }),
  destroy(st, eng){ if (st.note != null && eng) eng.noteOff(st.note); },
  process(st, io, ctx){
    const p = io.p, steps = clamp(p.steps | 0, 1, 8), ext = linked(io, 'clock'), eng = ctx.engine;
    const stepLen = 60 / Math.max(20, ctx.transport.tempo) / RATES[clamp(p.rate | 0, 0, 5)] * ctx.sr;
    for (let i = 0; i < ctx.block; i++){
      const r = io.in.reset[i] > .5; if (r && !st.rs) st.pos = -1; st.rs = r;
      let fire = false;
      if (ext){ const g = io.in.clock[i] > .5; fire = g && !st.c; st.c = g; }
      else if (p.run >= .5){ st.acc += 1 / stepLen; if (st.acc >= 1){ st.acc -= 1; fire = true; } }
      if (st.note != null && --st.off <= 0){ eng.noteOff(st.note); st.note = null; }
      if (fire){
        st.pos = (st.pos + 1) % steps;
        const v = p['s' + (st.pos + 1)];
        if (st.note != null){ eng.noteOff(st.note); st.note = null; }
        st.clkT = Math.round(ctx.sr * .005);
        if (v > -25){
          st.pitch = v; st.note = v; eng.noteOn(v, 1);
          st.off = st.gateT = Math.max(16, Math.round((ext ? ctx.sr * .25 : stepLen) * p.length));
        }
      }
      io.out.pitch[i] = st.pitch;
      io.out.gate[i] = st.gateT > 0 ? 1 : 0; if (st.gateT > 0) st.gateT--;
      io.out.clock[i] = st.clkT > 0 ? 1 : 0; if (st.clkT > 0) st.clkT--;
    }
    if (p.run < .5 && !ext && st.note != null){ eng.noteOff(st.note); st.note = null; }
  }
};

/* Out: stereo. With nothing in Right, In plays on both sides. */
const Out = {
  type:'out', scope:'mono', cost:.05,
  inputs:{ in:'audio', inR:'audio' }, outputs:{},
  params:{ slot:[1,1,BUS_COUNT,1], level:[1,0,2,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const k = clamp((io.p.slot | 0) - 1, 0, BUS_COUNT - 1), g = io.p.level, L = ctx.busL(k), R = ctx.busR(k);
    const inR = linked(io, 'inR') ? io.in.inR : io.in.in;
    for (let i = 0; i < ctx.block; i++){ L[i] += io.in.in[i] * g; R[i] += inR[i] * g; }
  }
};

const MODULES = { hostIn:HostIn, vco:VCO, fm:FM, additive:Additive, noise:Noise, plaits:Plaits, elements:Elements,
  vcf:VCF, vca:VCA, mixer:Mixer,
  eq:EQ, comp:Comp, saturation:Saturation, tape:Tape, chorus:Chorus, crush:Crush, delay:Delay, reverb:Reverb, rings:Rings, clouds:Clouds,
  lfo:LFO, adsr:ADSR, random:Random, atten:Attenuverter, seq:Sequencer, out:Out };

/* ── voices ───────────────────────────────────────────────────────────────── */
class VoicePool {
  constructor(max){ this.max = max; this.clock = 0; this.now = 0; this.rows = []; for (let i = 0; i < max; i++) this.rows.push({ pitch:0, gate:0, vel:0, age:0, offAt:-1e12 }); }
  on(pitch, vel){
    let i = this.rows.findIndex(r => r.gate === 0 && r.pitch === pitch);
    if (i < 0) i = this.rows.reduce((best, r, j) => (r.gate === 0 && (best < 0 || r.age < this.rows[best].age)) ? j : best, -1);
    if (i < 0) i = this.rows.reduce((a, r, j) => r.age < this.rows[a].age ? j : a, 0);
    const r = this.rows[i]; r.pitch = pitch; r.gate = 1; r.vel = vel; r.age = ++this.clock; return i;
  }
  off(pitch){ for (const r of this.rows) if (r.gate && r.pitch === pitch){ r.gate = 0; r.offAt = this.now; } }
  allOff(){ for (const r of this.rows) if (r.gate){ r.gate = 0; r.offAt = this.now; } }
  /* A voice keeps running for a while after its note ends, so releases and tails can finish. */
  awake(i, hold){ const r = this.rows[i]; return r.gate > 0 || this.now - r.offAt < hold; }
}

/* A cable into "p:name" modulates that parameter instead of feeding an input. */
const isParamPort = p => typeof p === 'string' && p.startsWith('p:');
class Graph {
  constructor(){ this.mods = new Map(); this.cables = []; this.order = []; this.inc = new Map(); }
  add(id, def, inst){ this.mods.set(id, { id, def, inst, poly:false }); this.compile(); }
  remove(id){ this.mods.delete(id); this.cables = this.cables.filter(c => c.a[0] !== id && c.b[0] !== id); this.compile(); }
  connect(a, b){
    if (!this.mods.has(a[0]) || !this.mods.has(b[0])) return;
    if (this.cables.some(c => c.a[0] === a[0] && c.a[1] === a[1] && c.b[0] === b[0] && c.b[1] === b[1])) return;
    this.cables.push({ a, b }); this.compile();
  }
  disconnect(a, b){ this.cables = this.cables.filter(c => !(c.a[0] === a[0] && c.a[1] === a[1] && c.b[0] === b[0] && c.b[1] === b[1])); this.compile(); }
  compile(){
    const indeg = new Map([...this.mods.keys()].map(k => [k, 0])), adj = new Map([...this.mods.keys()].map(k => [k, []]));
    this.inc = new Map([...this.mods.keys()].map(k => [k, []]));
    for (const c of this.cables){
      if (!adj.has(c.a[0]) || !adj.has(c.b[0])) continue;
      adj.get(c.a[0]).push(c.b[0]); indeg.set(c.b[0], indeg.get(c.b[0]) + 1); this.inc.get(c.b[0]).push(c);
    }
    const q = [...indeg].filter(([, d]) => d === 0).map(([k]) => k), order = [];
    while (q.length){ const k = q.shift(); order.push(k); for (const n of adj.get(k)){ indeg.set(n, indeg.get(n) - 1); if (indeg.get(n) === 0) q.push(n); } }
    for (const k of this.mods.keys()) if (!order.includes(k)) order.push(k);
    this.order = order;
    // A module runs once per voice when something polyphonic (Host In, or a module fed by it) feeds it.
    for (const id of order){
      const m = this.mods.get(id), s = m.def.scope;
      m.poly = s === 'root' ? true : s === 'mono' ? false : this.inc.get(id).some(c => { const src = this.mods.get(c.a[0]); return src && src.poly; });
    }
  }
}

/* ── the engine ───────────────────────────────────────────────────────────── */
class WavesEngine {
  constructor(sampleRate, opts = {}){
    this.sr = sampleRate; this.hosted = !!opts.hosted;
    this.transport = Object.assign({ tempo:120, lpb:4, ticks:6, ppq:0, playing:false }, opts.transport || {});
    this.quality = opts.quality || 'full';
    this.costCeiling = opts.costCeiling ?? .9;
    this.voices = new VoicePool(opts.maxVoices ?? 8);
    this.graph = new Graph();
    this.busesL = new Float32Array(BUS_COUNT * BLOCK); this.busesR = new Float32Array(BUS_COUNT * BLOCK);
    this.load = 0; this.telemetryOn = false;
    this._ios = new Map(); this._states = new Map();
    this._busy = 0; this._frames = 0;
    this._ctx = { sr:sampleRate, block:BLOCK, quality:this.quality, transport:this.transport, notes:this.voices.rows, voice:0, engine:this,
      busL:k => this.busesL.subarray(k * BLOCK, (k + 1) * BLOCK), busR:k => this.busesR.subarray(k * BLOCK, (k + 1) * BLOCK) };
  }
  addModule(id, type, o = {}){
    const def = MODULES[type]; if (!def) return;
    if (this.graph.mods.has(id)) this.removeModule(id);
    const params = {};
    for (const [k, v] of Object.entries(def.params)) params[k] = (o.params && typeof o.params[k] === 'number') ? o.params[k] : v[0];
    this.graph.add(id, def, { scope:def.scope, pos:o.pos || [0, 0], params });
    this._sync();
  }
  removeModule(id){ this._drop(id); this.graph.remove(id); this._sync(); }
  connect(a, b){ this.graph.connect(a, b); this._sync(); }
  disconnect(a, b){ this.graph.disconnect(a, b); this._sync(); }
  setParam(id, name, v){ const m = this.graph.mods.get(id); if (m && typeof v === 'number') m.inst.params[name] = v; }
  setParams(list){ for (const [id, name, v] of list) this.setParam(id, name, v); }
  setTransport(t){ Object.assign(this.transport, t); }
  /* Keep every module's state (oscillator phases, reverb tails) when the patch changes; only drop what no longer fits. */
  _sync(){
    for (const [key, io] of this._ios){ const id = key.split('#')[0], m = this.graph.mods.get(id); if (!m || m.poly !== io.poly) this._ios.delete(key); }
    for (const [key, s] of this._states){
      const id = key.split('#')[0], m = this.graph.mods.get(id);
      if (!m || m.poly !== s.poly){ this._destroy(s); this._states.delete(key); }
    }
  }
  _drop(id){ for (const [key, s] of this._states) if (key.split('#')[0] === id){ this._destroy(s); this._states.delete(key); } for (const key of [...this._ios.keys()]) if (key.split('#')[0] === id) this._ios.delete(key); }
  _destroy(s){ try { if (s.def.destroy) s.def.destroy(s.st, this); } catch (e) {} }
  _reset(){ for (const s of this._states.values()) this._destroy(s); this._states.clear(); this._ios.clear(); }
  /* Frees the effects' WebAssembly slots. Call when an engine is thrown away (offline renders). */
  dispose(){ this._reset(); this.graph = new Graph(); }
  loadPatch(doc){
    this._reset(); this.graph = new Graph();
    for (const m of doc.modules || []) this.addModule(m.id, m.type, m);
    for (const c of doc.cables || []) this.connect(c.from, c.to);
    if (doc.transport) this.setTransport(doc.transport);
    return { modules:(doc.modules || []).length, cables:(doc.cables || []).length };
  }
  noteOn(p, vel = 1){ this.voices.on(p, vel); }
  noteOff(p){ this.voices.off(p); }
  allNotesOff(){ this.voices.allOff(); }
  process(frames = BLOCK){
    const t0 = Date.now();
    this.busesL.fill(0); this.busesR.fill(0); this._render(frames);
    this._tickLoad(Date.now() - t0, frames);
  }
  processHost(frames = BLOCK){ this.process(frames); }
  bus(slot){ return this.busesL.subarray((slot - 1) * BLOCK, slot * BLOCK); }
  busR(slot){ return this.busesR.subarray((slot - 1) * BLOCK, slot * BLOCK); }

  _bufs(m, v){
    const key = m.id + (v >= 0 ? '#' + v : '');
    let io = this._ios.get(key);
    if (!io){
      io = { in:{}, out:{}, base:m.inst.params, p:m.inst.params, pm:null, linked:new Set(), poly:m.poly, mods:null };
      for (const n in m.def.inputs) io.in[n] = new Float32Array(BLOCK);
      for (const n in m.def.outputs) io.out[n] = new Float32Array(BLOCK);
      this._ios.set(key, io);
    }
    return io;
  }
  _state(m, v){
    const key = m.id + (v >= 0 ? '#' + v : '');
    let s = this._states.get(key);
    if (!s){ s = { st:m.def.create(this.sr), def:m.def, poly:m.poly }; this._states.set(key, s); }
    return s.st;
  }
  /* Sum every cable into this module's inputs, and work out its modulated parameters. */
  _gather(m, io, v){
    for (const k in io.in) io.in[k].fill(0);
    io.linked.clear();
    let mods = null;
    const N = this.voices.max;
    for (const c of this.graph.inc.get(m.id) || []){
      const src = this.graph.mods.get(c.a[0]); if (!src) continue;
      const port = c.b[1], param = isParamPort(port);
      let dst;
      if (param){
        const k = port.slice(2); if (!(k in m.def.params)) continue;
        mods = mods || {}; dst = mods[k] || (mods[k] = new Float32Array(BLOCK));
      } else { dst = io.in[port]; if (!dst) continue; io.linked.add(port); }
      const kind = src.def.outputs[c.a[1]];
      if (v >= 0 && src.poly){ const b = this._bufs(src, v).out[c.a[1]]; if (b) for (let i = 0; i < BLOCK; i++) dst[i] += b[i]; }
      else if (v >= 0){ // mono into a voice: control is shared, audio is split between the voices so the sum stays the same
        const b = this._bufs(src, -1).out[c.a[1]]; const g = kind === 'audio' ? 1 / N : 1;
        if (b) for (let i = 0; i < BLOCK; i++) dst[i] += b[i] * g;
      } else if (src.poly){ for (let vv = 0; vv < N; vv++){ const b = this._bufs(src, vv).out[c.a[1]]; if (b) for (let i = 0; i < BLOCK; i++) dst[i] += b[i]; } }
      else { const b = this._bufs(src, -1).out[c.a[1]]; if (b) for (let i = 0; i < BLOCK; i++) dst[i] += b[i]; }
    }
    // Modulated parameters: the slider's value plus the cable's value across the slider's whole range (as in ...Seeds).
    if (mods){
      if (!io.pm) io.pm = {};
      Object.assign(io.pm, io.base);
      for (const k in mods){
        const [, mn, mx, st] = m.def.params[k];
        let r = io.base[k] + mods[k][0] * (mx - mn);
        r = clamp(r, mn, mx); if (st >= 1) r = Math.round((r - mn) / st) * st + mn;
        io.pm[k] = r;
      }
      io.p = io.pm; io.mods = mods;
    } else { io.p = io.base; io.mods = null; }
  }
  _render(frames){
    const ctx = this._ctx;
    ctx.block = frames; ctx.quality = this.quality; ctx.notes = this.voices.rows; ctx.transport = this.transport;
    this.voices.now += frames;
    const hold = this.sr * 12;
    for (let v = 0; v < this.voices.max; v++){
      ctx.voice = v;
      const awake = this.voices.awake(v, hold);
      for (const id of this.graph.order){
        const m = this.graph.mods.get(id); if (!m.poly) continue;
        const io = this._bufs(m, v);
        // Silent voices are skipped (their outputs go quiet) to save CPU.
        if (!awake){ if (!io.asleep){ for (const k in io.out) io.out[k].fill(0); io.asleep = true; } continue; }
        io.asleep = false;
        this._gather(m, io, v);
        m.def.process(this._state(m, v), io, ctx);
      }
    }
    ctx.voice = 0;
    for (const id of this.graph.order){
      const m = this.graph.mods.get(id); if (m.poly) continue;
      const io = this._bufs(m, -1); this._gather(m, io, -1);
      m.def.process(this._state(m, -1), io, ctx);
    }
  }
  /* Date.now() is coarse, but its average over many blocks is the real cost. */
  _tickLoad(ms, frames){
    this._busy += ms; this._frames += frames;
    if (this._frames < this.sr / 4) return;
    const l = this._busy / (this._frames / this.sr * 1000); this._busy = 0; this._frames = 0;
    this.load = this.load * .5 + l * .5;
    if (this.load > this.costCeiling) this._shed();
  }
  _shed(){
    let step = 0;
    if (this.voices.max > 4){ this.voices.allOff(); this.voices = new VoicePool(this.voices.max - 1); this._reset(); step = 1; }
    else if (this.quality === 'full'){ this.quality = 'draft'; step = 2; }
    else { this.allNotesOff(); step = 3; }
    this.load = 0;
    if (this.onShed) this.onShed({ step, voices:this.voices.max, quality:this.quality });
  }
  telemetry(){
    let active = 0; for (const r of this.voices.rows) if (r.gate) active++;
    const buses = [];
    for (let k = 0; k < BUS_COUNT; k++){
      let peak = 0;
      for (let i = 0; i < BLOCK; i++){ const a = Math.abs(this.busesL[k * BLOCK + i]), b = Math.abs(this.busesR[k * BLOCK + i]); if (a > peak) peak = a; if (b > peak) peak = b; }
      buses.push(peak);
    }
    // The live value of every modulated parameter (first voice), so the page can show it.
    const mods = [];
    for (const m of this.graph.mods.values()){
      const io = this._ios.get(m.id + (m.poly ? '#0' : '')); if (!io || !io.mods) continue;
      for (const k in io.mods) mods.push([m.id, k, io.pm[k]]);
    }
    return { load:this.load, active, quality:this.quality, buses, mods, wasm:{ dsp:!!WASM.dsp, tfx:!!WASM.tfxX } };
  }
  handleMessage(m){
    switch (m.op){
      case 'patch': return this.loadPatch(m.value);
      case 'add': return this.addModule(m.id, m.type, m);
      case 'remove': return this.removeModule(m.id);
      case 'connect': return this.connect(m.a, m.b);
      case 'disconnect': return this.disconnect(m.a, m.b);
      case 'param': return this.setParam(m.id, m.name, m.value);
      case 'params': return this.setParams(m.list);
      case 'quality': this.quality = m.value; return;
      case 'transport': return this.setTransport(m.value);
      case 'telemetry': this.telemetryOn = !!m.value; return;
      case 'noteOn': return this.noteOn(m.pitch, m.vel);
      case 'noteOff': return this.noteOff(m.pitch);
      case 'panic': return this.allNotesOff();
    }
  }
}

/* ── protocol ─────────────────────────────────────────────────────────────── */
const OP = { patch:'patch', add:'add', remove:'remove', connect:'connect', disconnect:'disconnect',
  params:'params', quality:'quality', transport:'transport', telemetry:'telemetry',
  noteOn:'noteOn', noteOff:'noteOff', panic:'panic',
  ready:'ready', meter:'meter', shed:'shed', error:'error' };

/* ── the worklet wrapper ──────────────────────────────────────────────────────
   Only inside an AudioWorklet. Loaded as a plain <script> on a page (as ...Thunder Plus does, to
   render waves patches into its pads), the engine above is all there is: WavesEngine, wasmInit, MODULES. */
if (typeof AudioWorkletProcessor !== 'undefined'){
class WavesProcessor extends AudioWorkletProcessor {
  constructor(options){
    super();
    const o = options.processorOptions || {};
    wasmInit(o);
    this.engine = new WavesEngine(sampleRate, {
      hosted:false, maxVoices:o.maxVoices ?? 8, quality:o.quality ?? 'full',
      transport:Object.assign({ tempo:120, lpb:4, ticks:6, playing:false }, o.transport || {})
    });
    this.engine.onShed = info => this.port.postMessage({ target:TARGET, op:OP.shed, value:info });
    this._blocks = 0;
    this.port.onmessage = e => this._recv(e.data);
    this.port.postMessage({ target:TARGET, op:OP.ready, value:{ protocol:PROTOCOL, sr:sampleRate, block:BLOCK, wasm:{ dsp:!!WASM.dsp, tfx:!!WASM.tfxX } } });
  }
  _recv(m){
    if (!m || (m.target && m.target !== TARGET)) return;
    try { this.engine.handleMessage(m); }
    catch (err){ this.port.postMessage({ target:TARGET, op:OP.error, value:{ op:m.op, message:String(err?.message || err) } }); }
  }
  /* Standalone, every bus plays through the speakers; in Thunder each bus is its own input. */
  process(_ins, outs){
    const e = this.engine;
    try { e.process(BLOCK); }
    catch (err){ e.busesL.fill(0); e.busesR.fill(0); if ((this._blocks & 255) === 0) this.port.postMessage({ target:TARGET, op:OP.error, value:{ op:'process', message:String(err?.message || err) } }); }
    const out = outs[0], L = out[0], R = out[1] || out[0];
    L.fill(0); if (R !== L) R.fill(0);
    for (let k = 0; k < BUS_COUNT; k++){
      const o = k * BLOCK;
      for (let i = 0; i < BLOCK; i++){ L[i] += e.busesL[o + i]; if (R !== L) R[i] += e.busesR[o + i]; }
    }
    for (let i = 0; i < BLOCK; i++){ L[i] = soft(L[i]); if (R !== L) R[i] = soft(R[i]); }
    if (e.telemetryOn && (++this._blocks % 24) === 0) this.port.postMessage({ target:TARGET, op:OP.meter, value:e.telemetry() });
    else if (!e.telemetryOn) this._blocks++;
    return true;
  }
}
registerProcessor('waves', WavesProcessor);
} else if (typeof globalThis !== 'undefined'){
  globalThis.WavesDSP = { WavesEngine, wasmInit, MODULES, WASM, BUS_COUNT };
}
})();
