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
      try { x.tfx_process(st.h, ctx.block); }
      catch (e){ try { x.tfx_free(st.h); } catch (e2){} st.h = -1; io.out.out.set(L0); io.out.outR.set(R0); return; }   // crashed: bypass it
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

/* ===SHARED MODULES START===
   This block is the same in chains/chains-worklet.js and waves/waves-worklet.js. Edit it here (in …chains) and run
   tools/sync-shared.py to copy it into …waves. It needs: clamp, sinT, soft, rng, nextSeed, linked, u and effect(). */

/* Mutable Instruments Beads (MIT): time, size, pitch, shape, density, feedback, blend, reverb + media. */
// Beads divides by zero with Freeze on and Size at exactly 1, which traps the whole wasm module. Cap it just under.
const BEADS_SIZE_MAX = .995;
const Beads = effect('beads', 10, { media:[3,0,3,1], freeze:[0,0,1,1], time:[.2,0,1,.01], size:[.5,0,1,.01], pitch:[.5,0,1,.01], shape:[.5,0,1,.01],
    density:[.85,0,1,.01], fb:[.2,0,1,.01], blend:[1,0,1,.01], verb:[.25,0,1,.01] },
  p => [p.media | 0, 0, p.freeze >= .5 ? 1 : 0, u(p.time), Math.min(u(p.size), BEADS_SIZE_MAX), u(p.pitch), u(p.shape), u(p.density), u(p.fb), u(p.blend), u(p.verb), 0], 5);
/* Flanger, based on DaisySP Flanger (MIT). */
const FlangerFx = effect('flanger', 11, { depth:[0.5,0,1,.01], rate:[0.3,0.05,5,.01], delay:[0.5,0,1,.01], fb:[0.5,0,0.95,.01], mix:[0.5,0,1,.01] },
  p => [p.depth, p.rate, p.delay, p.fb, p.mix], 0.3);
/* Tremolo, based on DaisySP Tremolo (MIT). */
const TremoloFx = effect('tremolo', 13, { rate:[5,0.5,20,.01], depth:[0.7,0,1,.01], shape:[0,0,4,1] },
  p => [p.rate, p.depth, p.shape | 0], 0.2);
/* Overdrive, based on DaisySP Overdrive (MIT). */
const OdriveFx = effect('odrive', 14, { drive:[0.5,0,1,.01], tone:[0.6,0,1,.01], out:[0.2,0,1,.01], mix:[1,0,1,.01] },
  p => [p.drive, p.tone, p.out, p.mix], 0.3);
/* Wavefolder, based on DaisySP Wavefolder (MIT). */
const FolderFx = effect('folder', 15, { gain:[4,1,20,.01], offset:[0,-1,1,.01], out:[0.6,0,1,.01], mix:[1,0,1,.01] },
  p => [p.gain, p.offset, p.out, p.mix], 0.2);
/* Auto-wah, based on DaisySP Autowah (MIT). */
const AutowahFx = effect('autowah', 16, { wah:[0.6,0,1,.01], level:[0.8,0,1,.01], mix:[1,0,1,.01] },
  p => [p.wah, p.level, p.mix], 0.3);
/* Pitch shifter, based on DaisySP PitchShifter (MIT). */
const PshiftFx = effect('pshift', 17, { semis:[7,-24,24,1], win:[80,10,300,1], spray:[0,0,1,.01], mix:[1,0,1,.01] },
  p => [p.semis | 0, p.win, p.spray, p.mix], 0.4);
/* Ladder filter, based on DaisySP LadderFilter (MIT). */
const LadderFx = effect('ladder', 18, { cut:[1200,30,16000,1], res:[0.6,0,1.8,.01], drive:[1,0,4,.01], mode:[0,0,5,1], mix:[1,0,1,.01] },
  p => [p.cut, p.res, p.drive, p.mode | 0, p.mix], 0.6);
/* Cross-mod, based on Mutable Instruments Warps (MIT). */
const XmodFx = effect('xmod', 19, { alg:[0.3,0,1,.01], timbre:[0.5,0,1,.01], carrier:[3,0,3,1], note:[48,24,96,1], clvl:[0.8,0,1,.01], mlvl:[0.8,0,1,.01], mix:[1,0,1,.01] },
  p => [p.alg, p.timbre, p.clvl, p.mlvl, p.carrier | 0, p.note | 0, p.mix], 1.2);
/* Space reverb, based on Cloud Seed (Ghost Note Audio) (MIT). */
const SpaceFx = effect('space', 20, { size:[0.5,0,1,.01], decay:[0.55,0,1,.01], diff:[0.7,0,1,.01], mod:[0.4,0,1,.01], tone:[0.6,0,1,.01], lowcut:[0.2,0,1,.01], pre:[0.1,0,1,.01], mix:[0.35,0,1,.01] },
  p => [p.size, p.decay, p.diff, p.mod, p.tone, p.lowcut, p.pre, p.mix], 2);
/* Lo-fi, based on Airwindows DeRez2 (MIT). */
const LofiFx = effect('lofi', 21, { rate:[0.6,0,1,.01], rez:[0.6,0,1,.01], hard:[0.5,0,1,.01], mix:[1,0,1,.01] },
  p => [p.rate, p.rez, p.hard, p.mix], 0.3);
/* Drive, based on Airwindows PurestDrive (MIT). */
const DriveFx = effect('drive', 22, { drive:[0.5,0,1,.01] },
  p => [p.drive], 0.2);
/* Air, based on Airwindows Air3 (MIT). */
const AirFx = effect('air', 23, { air:[0.5,0,1,.01], gnd:[0.5,0,1,.01] },
  p => [p.air, p.gnd], 0.3);
/* Room reverb, based on Airwindows Verbity2 (MIT). */
const RoomFx = effect('room', 24, { size:[0.5,0,1,.01], sustain:[0.5,0,1,.01], mulch:[0.5,0,1,.01], wet:[0.35,0,1,.01] },
  p => [p.size, p.sustain, p.mulch, p.wet], 1);

/* Comb resonator, original code. */
const CombFx = effect('comb', 25, { note:[48,24,96,1], fb:[0.85,-0.99,0.99,.01], damp:[0.3,0,0.99,.01], spread:[8,0,50,1], mix:[0.6,0,1,.01] },
  (p, ctx) => [(440 * Math.pow(2, ((p.note | 0) - 69) / 12)), p.fb, p.damp, p.spread, p.mix], 0.2);
/* Transient shaper, original code. */
const TransientFx = effect('transient', 26, { att:[0.5,-1,1,.01], sus:[0,-1,1,.01], out:[1,0,2,.01], mix:[1,0,1,.01] },
  (p, ctx) => [p.att, p.sus, p.out, p.mix], 0.1);
/* Trance gate, original code. */
const TgateFx = effect('tgate', 27, { div:[2,0,5,1], pat:[4,0,8,1], len:[0.6,0.05,1,.01], depth:[1,0,1,.01], smooth:[3,0.5,30,.01] },
  (p, ctx) => [(ctx.transport.tempo / 60 / [0.125,0.16666667,0.25,0.33333333,0.5,1][(p.div | 0)]), p.len, p.depth, p.smooth, [65535,21845,43690,18761,22235,54749,4879,4353,5193][(p.pat | 0)], 16, (ctx.transport.startN || 0), 0], 0.1);
/* Multiband squash, original code. */
const OttFx = effect('ott', 28, { depth:[0.5,0,1,.01], time:[0.4,0,1,.01], lo:[0,-12,12,.01], mid:[0,-12,12,.01], hi:[0,-12,12,.01], out:[0,-12,12,.01] },
  (p, ctx) => [p.depth, p.time, p.lo, p.mid, p.hi, p.out], 1.2);
/* Frequency shifter, original code. */
const FshiftFx = effect('fshift', 29, { shift:[0.15,-1,1,.01], fb:[0,0,0.9,.01], mix:[1,0,1,.01] },
  (p, ctx) => [(Math.sign(p.shift) * p.shift * p.shift * 2000), p.fb, p.mix], 0.2);
/* Vinyl, based on Airwindows ToVinyl4 (MIT). */
const VinylFx = effect('vinyl', 30, { mlow:[0.2,0,1,.01], slow:[0.34,0,1,.01], hlim:[0.32,0,1,.01], wear:[0.3,0,1,.01] },
  (p, ctx) => [p.mlow, p.slow, p.hlim, p.wear], 0.4);
/* Dub tape, based on Airwindows Dubly (MIT). */
const DubFx = effect('dub', 31, { amt:[0.5,0,1,.01], mix:[1,0,1,.01] },
  (p, ctx) => [p.amt, p.mix], 0.3);
/* Speaker cab, based on Airwindows Cabs (MIT). */
const CabFx = effect('cab', 32, { model:[0,0,5,1], int:[0.66,0,1,.01], room:[0.33,0,1,.01], size:[0.66,0,1,.01], axis:[0.33,0,1,.01], out:[0.8,0,1,.01] },
  (p, ctx) => [(((p.model | 0) + 0.5) / 6), p.int, p.room, p.size, p.axis, p.out], 0.6);
/* Tube, based on Airwindows Tube2 (MIT). */
const TubeFx = effect('tube', 33, { input:[0.5,0,1,.01], tube:[0.5,0,1,.01] },
  (p, ctx) => [p.input, p.tube], 0.3);

/* ── audio → CV and CV utilities ──────────────────────────────────────────────
   Pitch CV convention: 0.5 = A4 (440 Hz), one octave = 0.125. */

/* Envelope follow: audio in, CV out (peak or squared), with a gate above Gate level. */
const EnvFol = {
  type:'envfol', scope:'mono', cost:.05,
  inputs:{ in:'audio' }, outputs:{ out:'cv', gate:'gate' },
  params:{ atk:[.01,0,1,.001], rel:[.15,0,2,.001], gain:[2,0,16,.01], thr:[.3,0,1,.001], mode:[0,0,1,1] },
  create:() => ({ env:0 }),
  process(st, io, ctx){
    const dt = 1 / ctx.sr, ka = 1 - Math.exp(-dt / Math.max(io.p.atk, 1e-4)), kr = 1 - Math.exp(-dt / Math.max(io.p.rel, 1e-4));
    const gain = io.p.gain, thr = io.p.thr, sq = io.p.mode >= .5;
    for (let i = 0; i < ctx.block; i++){
      const x = io.in.in[i] * gain, y = sq ? x * x : Math.abs(x);
      st.env += (y - st.env) * (y > st.env ? ka : kr);
      io.out.out[i] = st.env; io.out.gate[i] = st.env > thr ? 1 : 0;
    }
  }
};

/* Gate extract: audio in → gate while it stays above Threshold (with hysteresis), plus a smoothed env. */
const GateEx = {
  type:'gateex', scope:'mono', cost:.03,
  inputs:{ in:'audio' }, outputs:{ out:'gate', env:'cv' },
  params:{ thr:[.1,0,1,.001], hyst:[.25,0,1,.01], rel:[.05,0,1,.001] },
  create:() => ({ on:0, env:0 }),
  process(st, io, ctx){
    const thr = io.p.thr, hy = thr * (1 - io.p.hyst), kr = 1 - Math.exp(-1 / (ctx.sr * Math.max(io.p.rel, 1e-4)));
    for (let i = 0; i < ctx.block; i++){
      const a = Math.abs(io.in.in[i]);
      if (st.on){ if (a < hy) st.on = 0; } else if (a > thr) st.on = 1;
      st.env += ((st.on ? 1 : 0) - st.env) * kr;
      io.out.out[i] = st.on; io.out.env[i] = st.env;
    }
  }
};

/* Pitch follow (monophonic): zero-crossing estimate → CV (0.5 = A4) + a voiced gate. Best on simple tones.
   Estimates over a rolling ~85 ms window (one 128-sample block is far too short for a stable reading). */
const PitchFol = {
  type:'pitchfol', scope:'mono', cost:.2,
  inputs:{ in:'audio' }, outputs:{ out:'cv', gate:'gate' },
  params:{ thr:[.02,0,1,.001], glide:[.05,0,.5,.001], depth:[1,0,1,.01], min:[60,40,500,1], max:[1400,100,4000,1] },
  create:() => ({ buf:new Float32Array(4096), w:0, filled:0, ph:.5, voiced:0, f:0 }),
  process(st, io, ctx){
    const N = st.buf.length; let peak = 0;
    for (let i = 0; i < ctx.block; i++){ const x = io.in.in[i]; st.buf[st.w] = x; st.w = (st.w + 1) % N; if (st.filled < N) st.filled++; const a = Math.abs(x); if (a > peak) peak = a; }
    const M = st.filled;
    let cross = 0, prev = st.buf[(st.w - M + N * 2) % N];
    for (let i = 1; i < M; i++){ const s = st.buf[(st.w - M + i + N * 2) % N]; if ((s >= 0) !== (prev >= 0)) cross++; prev = s; }
    const f = M > 1 ? cross * 0.5 * ctx.sr / M : 0;
    st.voiced = peak > io.p.thr && f >= io.p.min && f <= io.p.max ? 1 : 0;
    if (st.voiced) st.f = f;
    const g = 1 - Math.exp(-ctx.block / (ctx.sr * Math.max(io.p.glide, 1e-4)));
    const target = st.voiced ? clamp(0.5 + Math.log2(Math.max(st.f, 1) / 440) / 8, 0, 1) : st.ph;
    st.ph = clamp(st.ph + (target - st.ph) * g, 0, 1);
    io.out.out.fill(st.ph * io.p.depth); io.out.gate.fill(st.voiced);
  }
};

/* Slew / lag: smooths a CV with separate rise and fall times. */
const Slew = {
  type:'slew', scope:'mono', cost:.03,
  inputs:{ in:'cv' }, outputs:{ out:'cv' },
  params:{ up:[.05,0,1,.001], dn:[.05,0,1,.001] },
  create:() => ({ y:0 }),
  process(st, io, ctx){
    const ku = 1 - Math.exp(-1 / (ctx.sr * Math.max(io.p.up, 1e-4))), kd = 1 - Math.exp(-1 / (ctx.sr * Math.max(io.p.dn, 1e-4)));
    for (let i = 0; i < ctx.block; i++){ const x = io.in.in[i]; st.y += (x - st.y) * (x > st.y ? ku : kd); io.out.out[i] = st.y; }
  }
};

/* Sample & hold: samples CV on a Trigger rising edge (or at Rate) and holds it, with a stepped noise out. */
const SampleHold = {
  type:'snh', scope:'mono', cost:.08,
  inputs:{ in:'cv', trig:'gate' }, outputs:{ out:'cv', noise:'cv' },
  params:{ rate:[4,.05,40,.01], smooth:[0,0,1,.01], depth:[1,0,1,.01] },
  create:() => ({ R:rng(nextSeed()), ph:0, t:0, held:0, noise:0, y:0, ny:0 }),
  process(st, io, ctx){
    const trig = linked(io, 'trig'), k = 1 - Math.pow(io.p.smooth, .15) * .9995, dep = io.p.depth;
    for (let i = 0; i < ctx.block; i++){
      let fire = false;
      if (trig){ const g = io.in.trig[i] > .5; fire = g && !st.t; st.t = g; }
      else { st.ph += io.p.rate / ctx.sr; if (st.ph >= 1){ st.ph -= 1; fire = true; } }
      if (fire){ st.held = io.in.in[i]; st.noise = st.R() * 2 - 1; }
      st.y += (st.held - st.y) * k; st.ny += (st.noise - st.ny) * k;
      io.out.out[i] = st.y * dep; io.out.noise[i] = st.ny * dep;
    }
  }
};

/* Quantizer: snaps a pitch CV to the nearest note of a scale (pitch CV: 0.5 = A4, 0.125 per octave). */
const SCALES = [[0,2,4,5,7,9,11],[0,2,3,5,7,8,10],[0,2,3,5,7,8,11],[0,3,5,7,10],[0,2,4,6,8,10],[0,1,2,3,4,5,6,7,8,9,10,11],[0,5,7]];
const Quant = {
  type:'quant', scope:'mono', cost:.03,
  inputs:{ in:'cv' }, outputs:{ out:'cv' },
  params:{ scale:[0,0,6,1], transpose:[0,-24,24,1], depth:[1,0,1,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const sc = SCALES[clamp(io.p.scale | 0, 0, SCALES.length - 1)], tr = io.p.transpose, dep = io.p.depth;
    for (let i = 0; i < ctx.block; i++){
      const semi = Math.round((io.in.in[i] - .5) * 96) + tr, oct = Math.floor(semi / 12), pc = ((semi % 12) + 12) % 12;
      let best = sc[0], bd = 99;
      for (const s of sc){ const d = Math.abs(s - pc); if (d < bd){ bd = d; best = s; } }
      io.out.out[i] = ((oct * 12 + best) / 96 + .5) * dep;
    }
  }
};




/* ── Phase 2: filters, shapers and stereo effects (all original DSP) ────────── */

/* Dual (2/4-pole) multimode filter with drive. Cutoff follows the VCF's mapping so the % reads the same. */
const Blades = {
  type:'blades', scope:'mono', cost:1.2,
  inputs:{ in:'audio', cutoff:'cv', res:'cv' }, outputs:{ out:'audio' },
  params:{ cutoff:[.5,0,1,.001], res:[.3,0,1,.001], drive:[0,0,1,.01], mode:[0,0,3,1], slope:[2,2,4,2] },
  create:() => ({ ic1:0, ic2:0, jc1:0, jc2:0 }),
  process(st, io, ctx){
    const mode = io.p.mode | 0, four = (io.p.slope | 0) >= 4, dr = 1 + io.p.drive * 6, norm = Math.tanh(dr) || 1;
    for (let i = 0; i < ctx.block; i++){
      const fc = Math.min(.49, (.01 + Math.pow(10, (io.p.cutoff + io.in.cutoff[i]) * 3) * .004) * 44100 / ctx.sr);
      const g = Math.tan(Math.PI * fc), k = 2 - 2 * Math.min(clamp(io.p.res + io.in.res[i], 0, 1), .98);
      const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
      const x = Math.tanh(io.in.in[i] * dr) / norm;
      // Stage 1 (TPT state-variable)
      const v3 = x - st.ic2, v1 = a1 * st.ic1 + a2 * v3, v2 = st.ic2 + a2 * st.ic1 + a3 * v3;
      st.ic1 = 2 * v1 - st.ic1; st.ic2 = 2 * v2 - st.ic2;
      let y = mode === 0 ? v2 : mode === 1 ? v1 : mode === 2 ? (x - k * v1 - v2) : (x - k * v1);
      if (four){
        const x2 = mode === 1 ? v1 : v2;
        const u3 = x2 - st.jc2, u1 = a1 * st.jc1 + a2 * u3, u2 = st.jc2 + a2 * st.jc1 + a3 * u3;
        st.jc1 = 2 * u1 - st.jc1; st.jc2 = 2 * u2 - st.jc2;
        y = mode === 0 ? u2 : mode === 1 ? u1 : mode === 2 ? (x2 - k * u1 - u2) : (x2 - k * u1);
      }
      io.out.out[i] = y;
    }
  }
};

/* Meta-modulator (Warps-style): crossfade, ring mod, wave fold or comparator between In and Mod. */
const Meta = {
  type:'meta', scope:'mono', cost:.3,
  inputs:{ in:'audio', mod:'audio' }, outputs:{ out:'audio' },
  params:{ mode:[0,0,3,1], amount:[.5,0,1,.01], mix:[1,0,1,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const mode = io.p.mode | 0, a = io.p.amount, mx = io.p.mix;
    for (let i = 0; i < ctx.block; i++){
      const x = io.in.in[i], m = io.in.mod[i]; let y;
      if (mode === 0) y = x * (1 - a) + m * a;                       // crossfade
      else if (mode === 1) y = x * ((1 - a) + a * m * 1.4142);       // ring mod
      else if (mode === 2) y = Math.sin(x * (1 + a * 7) * Math.PI);  // wave fold (sine shaper)
      else y = x * (m >= 0 ? 1 : -1);                                // comparator (mod flips sign)
      io.out.out[i] = x + (y - x) * mx;
    }
  }
};

/* Phaser: swept all-pass stages through an LFO or the CV input. */
const Phaser = {
  type:'phaser', scope:'mono', cost:.4,
  inputs:{ in:'audio', cv:'cv' }, outputs:{ out:'audio' },
  params:{ rate:[.3,0,1,.01], depth:[.5,0,1,.01], feedback:[.4,0,1,.01], stages:[4,2,8,2], mix:[.5,0,1,.01], sync:[0,0,1,1] },
  create:() => ({ ph:0, z:new Float32Array(8), fb:0 }),
  process(st, io, ctx){
    const hz = io.p.sync >= .5 ? (ctx.transport.tempo / 60) * io.p.rate : .05 * Math.pow(160, io.p.rate);
    const n = clamp(io.p.stages | 0, 2, 8), dep = io.p.depth, fbk = io.p.feedback * .9, mx = io.p.mix, cv = linked(io, 'cv');
    for (let i = 0; i < ctx.block; i++){
      st.ph += hz / ctx.sr; st.ph -= Math.floor(st.ph);
      const f = clamp(.5 + (sinT(st.ph) * dep + (cv ? io.in.cv[i] : 0)) * .5, 0, .999);
      const a = (1 - f) / (1 + f);
      let x = io.in.in[i] + st.fb * fbk;
      for (let s = 0; s < n; s++){ const y = a * x + st.z[s]; st.z[s] = x - a * y; x = y; }
      st.fb = x; io.out.out[i] = io.in.in[i] * (1 - mx) + x * mx;
    }
  }
};

/* ── tempo-synced control and beat repeat (added Oct 2026, original code) ──────────────────────────────────
   Note values in beats, shared by Steps, Clock and Repeat. They restart from the downbeat when the song starts
   (the engine counts transport starts in transport.startN). */
const DIV_BEATS = [.0625, .125, 1 / 6, .25, 1 / 3, .5, 1, 2, 4, 8];
const divBeats = v => DIV_BEATS[clamp(v | 0, 0, DIV_BEATS.length - 1)];
const restarted = (st, ctx) => { const n = ctx.transport.startN || 0; if (st.sn !== n){ st.sn = n; return true; } return false; };

/* Stereo width (mid/side) with a bass-mono control. In R is optional (mono up to stereo). */
const Width = {
  type:'width', scope:'mono', cost:.1,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ width:[1,0,2,.01], bass:[0,0,1,.01], level:[1,0,2,.01] },
  create:() => ({ lp:0 }),
  process(st, io, ctx){
    const R = linked(io, 'inR') ? io.in.inR : io.in.in, w = io.p.width, b = io.p.bass, g = io.p.level;
    const k = 1 - Math.exp(-2 * Math.PI * 200 / ctx.sr);
    for (let i = 0; i < ctx.block; i++){
      const l = io.in.in[i], r = R[i], mid = (l + r) * .5, side = (l - r) * .5;
      st.lp += (side - st.lp) * k;
      const sOut = (side - st.lp * b) * w;
      io.out.out[i] = (mid + sOut) * g; io.out.outR[i] = (mid - sOut) * g;
    }
  }
};

/* Tempo-synced ping-pong delay (or free time). In R is optional. */
const PP_DIVS = [['1/16', .25], ['1/8T', 1 / 3], ['1/8', .5], ['1/4T', 2 / 3], ['1/4', 1], ['1/2', 2], ['1 bar', 4]];
const PingPong = {
  type:'pingpong', scope:'mono', cost:.7,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ div:[4,0,PP_DIVS.length - 1,1], sync:[1,0,1,1], time:[.3,.02,1.5,.001], fb:[.4,0,1,.01], tone:[.5,0,1,.01], mix:[.3,0,1,.01] },
  create(){
    const N = 1 << 17;
    return { bL:new Float32Array(N), bR:new Float32Array(N), mask:N - 1, w:0, lp:0, lpR:0 };
  },
  process(st, io, ctx){
    const R = linked(io, 'inR') ? io.in.inR : io.in.in;
    const beat = 60 / Math.max(ctx.transport.tempo, 20);
    const tSec = io.p.sync >= .5 ? beat * PP_DIVS[clamp(io.p.div | 0, 0, PP_DIVS.length - 1)][1] : io.p.time;
    const d = clamp(Math.round(tSec * ctx.sr), 1, 65536), fb = io.p.fb * .95, mx = io.p.mix, k = .05 + io.p.tone * .9;
    const mask = st.mask;
    for (let i = 0; i < ctx.block; i++){
      const rd = (st.w - d) & mask;
      let dl = st.bL[rd], dr = st.bR[rd];
      st.lp += (dl - st.lp) * k; st.lpR += (dr - st.lpR) * k;
      const wl = st.lp, wr = st.lpR;
      st.bL[st.w] = io.in.in[i] + wr * fb;   // left takes the input and the right's tail
      st.bR[st.w] = wl * fb;                 // right echoes the left (ping-pong)
      st.w = (st.w + 1) & mask;
      io.out.out[i] = io.in.in[i] * (1 - mx) + dl * mx;
      io.out.outR[i] = R[i] * (1 - mx) + dr * mx;
    }
  }
};

/* ── utilities, reverbs, chorus and ring mod (added Oct 2026, original code) ─────────────────────────────────
   Shared by …chains and …waves. Modules with show(st) report values the editor draws (a meter, gain reduction);
   the engine's telemetry() collects them. */
const dbLin = d => Math.pow(10, d / 20);
const linDb = v => v > 1e-9 ? 20 * Math.log10(v) : -180;
const inR = io => linked(io, 'inR') ? io.in.inR : io.in.in;

/* Meter: passes stereo audio straight through and reports VU (300 ms average) and peak levels, so a meter can sit
   anywhere between two modules. Trim sets the level on the way through. */
const Meter = {
  type:'meter', scope:'mono', cost:.05,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ trim:[0,-24,24,.1] },
  create:() => ({ vl:0, vr:0, pl:0, pr:0, g:1 }),
  process(st, io, ctx){
    const L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, k = 1 - Math.exp(-1 / (ctx.sr * .3)), gt = dbLin(io.p.trim);
    let vl = st.vl, vr = st.vr, pl = st.pl, pr = st.pr, g = st.g;
    for (let i = 0; i < ctx.block; i++){
      g += (gt - g) * .002;
      const l = L[i] * g, r = R[i] * g; oL[i] = l; oR[i] = r;
      vl += (l * l - vl) * k; vr += (r * r - vr) * k;
      const al = l < 0 ? -l : l, ar = r < 0 ? -r : r; if (al > pl) pl = al; if (ar > pr) pr = ar;
    }
    st.vl = vl; st.vr = vr; st.pl = pl; st.pr = pr; st.g = g;
  },
  show(st){ const v = [linDb(Math.sqrt(st.vl * 2)), linDb(Math.sqrt(st.vr * 2)), linDb(st.pl), linDb(st.pr)]; st.pl = 0; st.pr = 0; return v; }
};

/* Limiter: a stereo peak limiter with 2 ms lookahead, so nothing gets past Ceiling. Input pushes into it;
   Release sets how fast it lets go. Reports its gain reduction. */
const Limiter = {
  type:'limiter', scope:'mono', cost:.1,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ input:[0,0,24,.1], ceil:[-.3,-24,0,.1], rel:[.15,.01,1,.001] },
  create(sr){ const n = Math.max(8, Math.round(sr * .002)); return { n, bl:new Float32Array(n), br:new Float32Array(n), pk:new Float32Array(n), w:0, peak:0, pi:0, env:1, gr:1 }; },
  process(st, io, ctx){
    const L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, n = st.n, pk = st.pk;
    const gin = dbLin(io.p.input), ceil = dbLin(io.p.ceil), kr = 1 - Math.exp(-1 / (ctx.sr * io.p.rel)), ka = 1 - Math.exp(-5 / n);
    let env = st.env, w = st.w, gr = st.gr, peak = st.peak, pi = st.pi;
    for (let i = 0; i < ctx.block; i++){
      const yl0 = st.bl[w], yr0 = st.br[w];   // the sample leaving the lookahead
      const l = L[i] * gin, r = R[i] * gin, a = Math.max(l < 0 ? -l : l, r < 0 ? -r : r);
      st.bl[w] = l; st.br[w] = r; pk[w] = a;
      // the loudest sample still to come out: a running maximum, rescanned only when the old maximum leaves
      if (a >= peak){ peak = a; pi = w; } else if (pi === w){ peak = 0; for (let j = 0; j < n; j++) if (pk[j] > peak){ peak = pk[j]; pi = j; } }
      w = w + 1 === n ? 0 : w + 1;
      const target = peak > ceil ? ceil / peak : 1;
      env += (target - env) * (target < env ? ka : kr);
      let yl = yl0 * env, yr = yr0 * env;
      if (yl > ceil) yl = ceil; else if (yl < -ceil) yl = -ceil;   // the last word: a hard ceiling
      if (yr > ceil) yr = ceil; else if (yr < -ceil) yr = -ceil;
      oL[i] = yl; oR[i] = yr; if (env < gr) gr = env;
    }
    st.env = env; st.w = w; st.gr = gr; st.peak = peak; st.pi = pi;
  },
  show(st){ const v = [linDb(st.gr)]; st.gr = 1; return v; }
};

/* Utility: gain (smoothed, so moving it never clicks), pan, mono, polarity flip and mute. */
const Utility = {
  type:'utility', scope:'mono', cost:.03,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ gain:[0,-60,24,.1], pan:[0,-1,1,.01], mono:[0,0,1,1], flip:[0,0,3,1], mute:[0,0,1,1] },
  create:() => ({ gl:1, gr:1 }),
  process(st, io, ctx){
    const L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, p = io.p;
    const g = p.mute >= .5 ? 0 : (p.gain <= -59.9 ? 0 : dbLin(p.gain)), a = (clamp(p.pan, -1, 1) + 1) * Math.PI / 4;
    const tl = g * Math.cos(a) * Math.SQRT2, tr = g * Math.sin(a) * Math.SQRT2, f = p.flip | 0, sl = f & 1 ? -1 : 1, sr = f & 2 ? -1 : 1, mono = p.mono >= .5;
    let gl = st.gl, gr = st.gr;
    for (let i = 0; i < ctx.block; i++){
      gl += (tl - gl) * .003; gr += (tr - gr) * .003;
      let l = L[i] * sl, r = R[i] * sr; if (mono){ const m = (l + r) * .5; l = m; r = m; }
      oL[i] = l * gl; oR[i] = r * gr;
    }
    st.gl = gl; st.gr = gr;
  }
};

/* Ring mod: multiplies the sound by a carrier, its own oscillator (Freq, 5 Hz–5 kHz; the Freq input adds ±4
   octaves) or whatever is patched into Carrier. Spread offsets the right side's phase; Shape squares the carrier up. */
const RingMod = {
  type:'ringmod', scope:'mono', cost:.1,
  inputs:{ in:'audio', inR:'audio', car:'audio', freq:'cv' }, outputs:{ out:'audio', outR:'audio' },
  params:{ freq:[.45,0,1,.001], fine:[0,-1,1,.01], wave:[0,0,3,1], spread:[0,0,1,.01], shape:[0,0,1,.01], mix:[1,0,1,.01] },
  create:() => ({ ph:0, n1:0, n2:0, nr:rng(nextSeed()) }),
  process(st, io, ctx){
    const L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, ext = linked(io, 'car'), fcv = linked(io, 'freq'), p = io.p;
    const base = 5 * Math.pow(1000, p.freq) * Math.pow(2, p.fine / 12), w = p.wave | 0, sp = p.spread * .25, mx = p.mix;
    const dr = 1 + p.shape * 12, nd = Math.tanh(dr);
    const osc = ph => w === 0 ? sinT(ph) : w === 1 ? 1 - 4 * Math.abs(ph - .5) : w === 2 ? (ph < .5 ? 1 : -1) : 0;
    for (let i = 0; i < ctx.block; i++){
      let c1, c2;
      if (ext){ c1 = c2 = io.in.car[i]; }
      else {
        const f = base * (fcv ? Math.pow(2, clamp(io.in.freq[i], -1, 1) * 4) : 1);
        const ph0 = st.ph; st.ph += f / ctx.sr; if (st.ph >= 1){ st.ph -= Math.floor(st.ph); if (w === 3){ st.n1 = st.nr() * 2 - 1; } }
        if (w === 3){ if ((ph0 + sp) % 1 > (st.ph + sp) % 1) st.n2 = st.nr() * 2 - 1; c1 = st.n1; c2 = sp ? st.n2 : st.n1; }
        else { c1 = osc(st.ph); const q = st.ph + sp; c2 = osc(q - Math.floor(q)); }
        if (dr > 1){ c1 = Math.tanh(c1 * dr) / nd; c2 = Math.tanh(c2 * dr) / nd; }
      }
      oL[i] = L[i] * (1 - mx + c1 * mx); oR[i] = R[i] * (1 - mx + c2 * mx);
    }
  }
};

/* Spring reverb: two springs (the right one 13% longer). Each is a feedback loop through a delay and a chain of
   stretched all-pass filters, which smear each sound into the falling "chirp" a real tank makes. Chirp sets how
   strong that is, Length the tank size, Drip the splashy high end going in, Tone the darkness, Decay the length. */
const SPR_N = 18;
const Spring = {
  type:'spring', scope:'mono', cost:.35,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ decay:[.6,0,1,.01], tone:[.5,0,1,.01], chirp:[.6,0,1,.01], length:[.5,0,1,.01], drip:[.4,0,1,.01], mix:[.35,0,1,.01] },
  create(sr){
    const one = () => ({ d:new Float32Array(Math.ceil(sr * .2) + 4), w:0, x:new Float32Array(SPR_N * 2), y:new Float32Array(SPR_N * 2), lp:0, hp:0, pre:0, fb:0, wob:0 });
    return { s:[one(), one()], t:0, r:rng(nextSeed()) };
  },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR;
    const g = .55 + p.decay * .43, a = .3 + p.chirp * .45, kl = 1 - Math.exp(-2 * Math.PI * (1500 * Math.pow(6, p.tone)) / sr);
    const kh = 1 - Math.exp(-2 * Math.PI * 140 / sr), kp = 1 - Math.exp(-2 * Math.PI * 900 / sr), drip = p.drip * 3, mx = p.mix;
    const base = (.028 + p.length * .05) * sr;
    for (let c = 0; c < 2; c++){
      const s = st.s[c], X = c ? R : L, O = c ? oR : oL, D = s.d, N = D.length, len = base * (c ? 1.13 : 1);
      let t = st.t;
      for (let i = 0; i < ctx.block; i++){
        const x = X[i];
        s.pre += (x - s.pre) * kp; const xin = x + (x - s.pre) * drip;            // Drip: the high end goes in harder
        // a slow random wobble of the tank length, as a real spring never sits still
        if ((t++ & 255) === 0) s.wob = st.r() * 2 - 1;
        const rd = s.w - len * (1 + s.wob * .0015); const ri = Math.floor(rd), fr = rd - ri;
        const d0 = D[((ri % N) + N) % N], d1 = D[((ri + 1) % N + N) % N], dl = d0 + (d1 - d0) * fr;
        s.lp += (dl - s.lp) * kl; s.hp += (s.lp - s.hp) * kh;
        let v = xin * .5 + (s.lp - s.hp) * g;
        // dispersion: stretched all-passes (z^-2), y = a·x + x[n-2] − a·y[n-2]
        const k = t & 1, X2 = s.x, Y2 = s.y;
        for (let j = 0; j < SPR_N; j++){
          const o = j * 2 + k, y = a * v + X2[o] - a * Y2[o];
          X2[o] = v; Y2[o] = y; v = y;
        }
        if (!(v > -8 && v < 8)){ v = 0; X2.fill(0); Y2.fill(0); s.lp = s.hp = 0; }
        D[s.w] = v; s.w = s.w + 1 === N ? 0 : s.w + 1;
        O[i] = x * (1 - mx) + v * mx * 1.4;
      }
      if (c) st.t = t;
    }
  }
};

/* Plate reverb: the classic figure-eight plate tank (Dattorro's topology, written here from the 1997 paper).
   Size scales the tank, Damping darkens the tail, Pre-delay holds it back, Motion moves the tank's delays. */
const PL = { sr:29761, ind:[142, 107, 379, 277], ap1:[672, 908], dl1:[4453, 4217], ap2:[1800, 2656], dl2:[3720, 3163],
  // taps: [sign, tank node, offset]; nodes 0–2 are the right half (delay 4217, all-pass 2656, delay 3163), 3–5 the left
  tapsL:[[1, 0, 266], [1, 0, 2974], [-1, 1, 1913], [1, 2, 1996], [-1, 3, 1990], [-1, 4, 187], [-1, 5, 1066]],
  tapsR:[[1, 3, 353], [1, 3, 3627], [-1, 4, 1228], [1, 5, 2673], [-1, 0, 2111], [-1, 1, 335], [-1, 2, 121]] };
const plBuf = n => ({ b:new Float32Array(n), n, w:0 });
// d.b[d.w] is the next slot to write; the sample written k samples ago is at d.w − k.
const plAt = (d, k) => { let i = d.w - k; if (i < 0) i += d.n; return d.b[i]; };
const plPush = (d, v) => { d.b[d.w] = v; if (++d.w === d.n) d.w = 0; };
const Plate = {
  type:'plate', scope:'mono', cost:.25,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ size:[.7,.2,1,.01], decay:[.6,0,.97,.01], damp:[.4,0,1,.01], pre:[.01,0,.25,.001], mod:[.3,0,1,.01], mix:[.3,0,1,.01] },
  create(sr){
    const s = sr / PL.sr, mk = n => plBuf(Math.ceil(n * s) + 40);
    const st = { s, pre:plBuf(Math.ceil(sr * .26) + 2), bw:0, ind:PL.ind.map(mk), indK:PL.ind.map(n => Math.round(n * s)),
      ap1:PL.ap1.map(n => mk(n + 32)), dl1:PL.dl1.map(mk), ap2:PL.ap2.map(mk), dl2:PL.dl2.map(mk), lp0:0, lp1:0, fb0:0, fb1:0, ph:0,
      tl:new Int32Array(7), tr:new Int32Array(7) };
    st.node = [st.dl1[1], st.ap2[1], st.dl2[1], st.dl1[0], st.ap2[0], st.dl2[0]];
    st.nl = PL.tapsL.map(t => st.node[t[1]]); st.nr = PL.tapsR.map(t => st.node[t[1]]);
    return st;
  },
  process(st, io, ctx){
    const p = io.p, s = st.s, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, mx = p.mix;
    const sz = p.size, dec = p.decay, dd2 = clamp(dec + .15, .25, .5), kd = 1 - p.damp * .9, sc = s * sz;
    const pre = Math.min(st.pre.n - 1, Math.round(p.pre * ctx.sr) + 1), exc = p.mod * 16 * s;
    const ind = st.ind, ik = st.indK, A1 = st.ap1, D1 = st.dl1, A2 = st.ap2, D2 = st.dl2;
    const a1k0 = PL.ap1[0] * sc, a1k1 = PL.ap1[1] * sc, d1k0 = Math.round(PL.dl1[0] * sc), d1k1 = Math.round(PL.dl1[1] * sc);
    const a2k0 = Math.round(PL.ap2[0] * sc), a2k1 = Math.round(PL.ap2[1] * sc), d2k0 = Math.round(PL.dl2[0] * sc), d2k1 = Math.round(PL.dl2[1] * sc);
    const tl = st.tl, tr = st.tr, nl = st.nl, nr = st.nr;
    for (let j = 0; j < 7; j++){ tl[j] = Math.max(1, Math.round(PL.tapsL[j][2] * sc)); tr[j] = Math.max(1, Math.round(PL.tapsR[j][2] * sc)); }
    const dph = .9 / ctx.sr;
    let lp0 = st.lp0, lp1 = st.lp1, fb0 = st.fb0, fb1 = st.fb1, bw = st.bw, ph = st.ph;
    for (let i = 0; i < ctx.block; i++){
      plPush(st.pre, (L[i] + R[i]) * .5);
      bw += (plAt(st.pre, pre) - bw) * .9995;
      let x = bw;
      // input diffusion: four all-passes (y = −g·x + d; d ← x + g·y)
      for (let j = 0; j < 4; j++){ const g = j < 2 ? .75 : .625, y = -g * x + plAt(ind[j], ik[j]); plPush(ind[j], x + g * y); x = y; }
      ph += dph; if (ph >= 1) ph -= 1;
      // left half of the tank, fed by the right half's output
      let v = x + fb1 * dec;
      { const k = a1k0 + exc * (1 + sinT(ph)), ki = k | 0, fr = k - ki, z = plAt(A1[0], ki) * (1 - fr) + plAt(A1[0], ki + 1) * fr, y = .7 * v + z; plPush(A1[0], v - .7 * y); v = y; }
      plPush(D1[0], v); v = plAt(D1[0], d1k0);
      lp0 += (v - lp0) * kd; v = lp0 * dec;
      { const y = -dd2 * v + plAt(A2[0], a2k0); plPush(A2[0], v + dd2 * y); v = y; }
      plPush(D2[0], v); v = plAt(D2[0], d2k0); if (!(v > -8 && v < 8)) v = 0;
      const nf0 = v;
      // right half, fed by the left
      v = x + fb0 * dec;
      { const k = a1k1 + exc * (1 + sinT(ph + .25)), ki = k | 0, fr = k - ki, z = plAt(A1[1], ki) * (1 - fr) + plAt(A1[1], ki + 1) * fr, y = .7 * v + z; plPush(A1[1], v - .7 * y); v = y; }
      plPush(D1[1], v); v = plAt(D1[1], d1k1);
      lp1 += (v - lp1) * kd; v = lp1 * dec;
      { const y = -dd2 * v + plAt(A2[1], a2k1); plPush(A2[1], v + dd2 * y); v = y; }
      plPush(D2[1], v); v = plAt(D2[1], d2k1); if (!(v > -8 && v < 8)) v = 0;
      fb0 = nf0; fb1 = v;
      // output taps across both halves
      const yl = (plAt(nl[0], tl[0]) + plAt(nl[1], tl[1]) - plAt(nl[2], tl[2]) + plAt(nl[3], tl[3]) - plAt(nl[4], tl[4]) - plAt(nl[5], tl[5]) - plAt(nl[6], tl[6])) * .6;
      const yr = (plAt(nr[0], tr[0]) + plAt(nr[1], tr[1]) - plAt(nr[2], tr[2]) + plAt(nr[3], tr[3]) - plAt(nr[4], tr[4]) - plAt(nr[5], tr[5]) - plAt(nr[6], tr[6])) * .6;
      oL[i] = L[i] * (1 - mx) + yl * mx; oR[i] = R[i] * (1 - mx) + yr * mx;
    }
    st.lp0 = lp0; st.lp1 = lp1; st.fb0 = fb0; st.fb1 = fb1; st.bw = bw; st.ph = ph;
  }
};

/* Ensemble: a lush chorus. Ensemble runs three voices around the stereo field with a little fast shimmer;
   Classic I, II and I+II are the two-speed bucket-brigade chorus of 80s polysynths. Rate and Depth scale it. */
const ENS = [[.6, .5, 3], [.513, .35, 2], [.863, .35, 2], [9.75, .12, 2]];
const Ensemble = {
  type:'ensemble', scope:'mono', cost:.2,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[0,0,3,1], rate:[.5,0,1,.01], depth:[.5,0,1,.01], tone:[.7,0,1,.01], mix:[.5,0,1,.01] },
  create(sr){ return { b:new Float32Array(Math.ceil(sr * .05) + 4), w:0, ph:0, vib:0, lp:[0, 0] }; },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, B = st.b, N = B.length;
    const [hz0, dep0, voices] = ENS[clamp(p.mode | 0, 0, 3)], hz = hz0 * Math.pow(2, (p.rate - .5) * 4), dep = dep0 * p.depth * 2;
    const ctr = (voices === 3 ? .012 : .0035) * sr, sw = (voices === 3 ? .004 : .0019) * sr * dep / .5;
    const kt = 1 - Math.exp(-2 * Math.PI * (2000 * Math.pow(8, p.tone)) / sr), mx = p.mix;
    const rd = d => { const r = st.w - d, ri = Math.floor(r), fr = r - ri, a = B[((ri % N) + N) % N], b = B[(((ri + 1) % N) + N) % N]; return a + (b - a) * fr; };
    for (let i = 0; i < ctx.block; i++){
      const m = (L[i] + R[i]) * .5; B[st.w] = m;
      st.ph += hz / sr; if (st.ph >= 1) st.ph -= 1;
      let wl, wr;
      if (voices === 3){
        st.vib += 6.3 / sr; if (st.vib >= 1) st.vib -= 1;
        const v = sinT(st.vib) * .08;
        const a = rd(ctr + sw * (sinT(st.ph) + v)), b = rd(ctr + sw * (sinT(st.ph + 1 / 3) + v)), c = rd(ctr + sw * (sinT(st.ph + 2 / 3) + v));
        wl = a * .7 + b * .45 + c * .15; wr = c * .7 + b * .45 + a * .15;
      } else {
        const tri = 1 - 4 * Math.abs(st.ph - .5);   // triangle, as the originals
        wl = rd(ctr + sw * tri); wr = rd(ctr - sw * tri);
      }
      st.lp[0] += (wl - st.lp[0]) * kt; st.lp[1] += (wr - st.lp[1]) * kt;
      oL[i] = L[i] * (1 - mx * .5) + st.lp[0] * mx; oR[i] = R[i] * (1 - mx * .5) + st.lp[1] * mx;
      st.w = st.w + 1 === N ? 0 : st.w + 1;
    }
  }
};

/* Math: A and B (each input plus its knob) → A+B, A−B, A×B, the smaller and the larger. */
const MathM = {
  type:'math', scope:'mono', cost:.02,
  inputs:{ a:'cv', b:'cv' }, outputs:{ sum:'cv', diff:'cv', mul:'cv', min:'cv', max:'cv' },
  params:{ ka:[0,-1,1,.01], kb:[0,-1,1,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const A = io.in.a, B = io.in.b, o = io.out, ka = io.p.ka, kb = io.p.kb;
    for (let i = 0; i < ctx.block; i++){ const a = A[i] + ka, b = B[i] + kb; o.sum[i] = a + b; o.diff[i] = a - b; o.mul[i] = a * b; o.min[i] = a < b ? a : b; o.max[i] = a > b ? a : b; }
  }
};

/* Logic: two gates in → AND, OR, XOR, and NOT A. */
const Logic = {
  type:'logic', scope:'mono', cost:.02,
  inputs:{ a:'gate', b:'gate' }, outputs:{ and:'gate', or:'gate', xor:'gate', not:'gate' },
  params:{ thr:[.5,.05,.95,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const A = io.in.a, B = io.in.b, o = io.out, t = io.p.thr;
    for (let i = 0; i < ctx.block; i++){ const a = A[i] >= t, b = B[i] >= t; o.and[i] = a && b ? 1 : 0; o.or[i] = a || b ? 1 : 0; o.xor[i] = a !== b ? 1 : 0; o.not[i] = a ? 0 : 1; }
  }
};

/* Divider: counts Clock in and gates every 2nd, 4th, 8th and Nth pulse (each output stays high for the length of
   the pulse that fired it). Reset starts counting again. */
const Divider = {
  type:'divider', scope:'mono', cost:.02,
  inputs:{ clock:'gate', reset:'gate' }, outputs:{ d2:'gate', d4:'gate', d8:'gate', n:'gate' },
  params:{ n:[3,1,32,1] },
  create:() => ({ c:-1, lc:0, lr:0 }),
  process(st, io, ctx){
    const C = io.in.clock, Rs = io.in.reset, o = io.out, N = Math.max(1, io.p.n | 0);
    for (let i = 0; i < ctx.block; i++){
      const r = Rs[i] >= .5; if (r && !st.lr) st.c = -1; st.lr = r;
      const c = C[i] >= .5; if (c && !st.lc) st.c++; st.lc = c;
      const k = st.c;
      o.d2[i] = c && k % 2 === 0 ? 1 : 0; o.d4[i] = c && k % 4 === 0 ? 1 : 0; o.d8[i] = c && k % 8 === 0 ? 1 : 0; o.n[i] = c && k % N === 0 ? 1 : 0;
    }
  }
};

/* Euclidean: spreads Hits as evenly as possible over Steps (rotated by Rotate), one step per note value at the
   song tempo, or per pulse on Clock in. Gate fires on the hits; Accent on the first hit of each cycle.
   Restarts from step 1 when the song starts. */
const Euclid = {
  type:'euclid', scope:'mono', cost:.02,
  inputs:{ clock:'gate', reset:'gate' }, outputs:{ gate:'gate', accent:'gate' },
  params:{ steps:[16,1,32,1], hits:[5,0,32,1], rot:[0,0,31,1], div:[3,0,9,1], len:[.5,.05,.95,.01] },
  create:() => ({ step:-1, ph:1, lc:0, lr:0, sn:undefined }),
  process(st, io, ctx){
    const p = io.p, S = Math.max(1, p.steps | 0), H = clamp(p.hits | 0, 0, S), rot = p.rot | 0, ck = linked(io, 'clock');
    const hit = k => { const j = (((k - rot) % S) + S) % S; return H > 0 && ((j * H) % S) < H; };
    const first = k => { for (let j = 0; j < S; j++) if (hit(j)) return j === ((k % S) + S) % S; return false; };
    if (restarted(st, ctx)){ st.step = -1; st.ph = 1; }
    const inc = (ctx.transport.tempo || 120) / 60 / divBeats(p.div) / ctx.sr;
    for (let i = 0; i < ctx.block; i++){
      const r = io.in.reset[i] >= .5; if (r && !st.lr){ st.step = -1; st.ph = 1; } st.lr = r;
      let on;
      if (ck){ const c = io.in.clock[i] >= .5; if (c && !st.lc) st.step = (st.step + 1) % S; st.lc = c; on = c; }
      else { st.ph += inc; if (st.ph >= 1){ st.ph -= Math.floor(st.ph); st.step = (st.step + 1) % S; } on = st.ph < p.len; }
      const h = st.step >= 0 && on && hit(st.step);
      io.out.gate[i] = h ? 1 : 0; io.out.accent[i] = h && first(st.step) ? 1 : 0;
    }
  }
};

/* Dimension: a stereo-widening bucket-brigade chorus in the spirit of the classic 4-button "dimension" units. Two
   short delay lines swept in opposite directions by one slow LFO, each added to the dry sound on its side and
   subtracted, a little, from the other: wide, but the middle stays put and it sums back to mono cleanly.
   Mode 1 is barely there, 4 the widest; Width scales the wet, Tone darkens it like a bucket-brigade chip. */
const DIM_MODES = [[.25, .45], [.25, .8], [.25, 1.25], [.45, 1.6]];   // [LFO Hz, sweep ms]
const Dimension = {
  type:'dimension', scope:'mono', cost:.12,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[1,0,3,1], width:[.7,0,1,.01], tone:[.6,0,1,.01] },
  create(sr){ return { b:new Float32Array(Math.ceil(sr * .02) + 4), w:0, ph:0, l1:0, l2:0, m1:0, m2:0 }; },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, B = st.b, N = B.length;
    const [hz, ms] = DIM_MODES[clamp(p.mode | 0, 0, 3)], ctr = .0042 * sr, sw = ms * .001 * sr * .5, wd = p.width;
    const kt = 1 - Math.exp(-2 * Math.PI * (2500 * Math.pow(4, p.tone)) / sr);
    const rd = d => { const r = st.w - d, ri = Math.floor(r), fr = r - ri, a = B[((ri % N) + N) % N], b = B[(((ri + 1) % N) + N) % N]; return a + (b - a) * fr; };
    for (let i = 0; i < ctx.block; i++){
      const m = (L[i] + R[i]) * .5; B[st.w] = m;
      st.ph += hz / sr; if (st.ph >= 1) st.ph -= 1;
      const tri = 1 - 4 * Math.abs(st.ph - .5);
      // two poles of filtering each, as the chips' clock filters round off the top
      st.l1 += (rd(ctr + sw * tri) - st.l1) * kt; st.m1 += (st.l1 - st.m1) * kt;
      st.l2 += (rd(ctr - sw * tri) - st.l2) * kt; st.m2 += (st.l2 - st.m2) * kt;
      oL[i] = L[i] + (st.m1 - st.m2 * .4) * wd * .7; oR[i] = R[i] + (st.m2 - st.m1 * .4) * wd * .7;
      st.w = st.w + 1 === N ? 0 : st.w + 1;
    }
  }
};

/* BBD delay: a bucket-brigade analog echo. The repeats get darker the longer the time (a BBD's bandwidth falls with
   its clock), saturate as they build (Repeats past 100% runs away into self-oscillation, held by the saturation),
   and wobble with Wow. Moving Time bends the pitch, as turning the knob on the pedal does. Age adds hiss and
   murk. Sync locks the time to a note value at the song tempo. */
const BBD = {
  type:'bbd', scope:'mono', cost:.15,
  inputs:{ in:'audio', inR:'audio', time:'cv' }, outputs:{ out:'audio', outR:'audio' },
  params:{ time:[.3,.02,.9,.001], sync:[0,0,1,1], div:[2,0,6,1], fb:[.45,0,1.1,.01], tone:[.5,0,1,.01], wow:[.25,0,1,.01], age:[.2,0,1,.01], mix:[.35,0,1,.01] },
  create(sr){ return { b:new Float32Array(Math.ceil(sr * 1.3) + 4), w:0, d:-1, lp1:0, lp2:0, hp:0, ph:0, ph2:0, fb:0, r:rng(nextSeed()), nz:0 }; },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, B = st.b, N = B.length;
    let tt = p.sync >= .5 ? 60 / (ctx.transport.tempo || 120) * PP_DIVS[clamp(p.div | 0, 0, PP_DIVS.length - 1)][1] : p.time;
    if (linked(io, 'time')) tt *= Math.pow(2, clamp(io.in.time[0], -1, 1));
    tt = clamp(tt, .02, 1.2);
    const target = tt * sr; if (st.d < 0) st.d = target;
    const kd = 1 - Math.exp(-1 / (sr * .12));                                  // the time glides: pitch bends
    // bandwidth of a 4096-stage chip at this time, then Tone and Age on top
    const fc = clamp(4096 / (4 * tt) * .85 * Math.pow(2, (p.tone - .5) * 2) * (1 - p.age * .5), 300, 14000);
    const k = 1 - Math.exp(-2 * Math.PI * fc / sr), kh = 1 - Math.exp(-2 * Math.PI * 60 / sr);
    const fbk = p.fb, wow = p.wow, hiss = p.age * p.age * .004, mx = p.mix;
    for (let i = 0; i < ctx.block; i++){
      st.d += (target - st.d) * kd;
      st.ph += .55 / sr; if (st.ph >= 1) st.ph -= 1; st.ph2 += 3.1 / sr; if (st.ph2 >= 1) st.ph2 -= 1;
      const mod = 1 + wow * (sinT(st.ph) * .006 + sinT(st.ph2) * .0012);       // slow wow plus a little flutter
      const r = st.w - st.d * mod, ri = Math.floor(r), fr = r - ri;
      const a = B[((ri % N) + N) % N], c = B[(((ri + 1) % N) + N) % N];
      let y = a + (c - a) * fr;
      st.lp1 += (y - st.lp1) * k; st.lp2 += (st.lp1 - st.lp2) * k; y = st.lp2;  // two poles: the clock filters
      st.hp += (y - st.hp) * kh; y -= st.hp;
      if ((i & 7) === 0) st.nz = (st.r() * 2 - 1) * hiss;
      y += st.nz;
      const x = (L[i] + R[i]) * .5, v = Math.tanh((x + y * fbk) * 1.2) / 1.2;     // the feedback saturates, so it can't run away
      B[st.w] = v; st.w = st.w + 1 === N ? 0 : st.w + 1;
      oL[i] = L[i] * (1 - mx * .5) + y * mx; oR[i] = R[i] * (1 - mx * .5) + y * mx;
    }
  }
};

/* Harmonizer: two pitch-shifted voices at set intervals, each with its own level and pan, plus Detune (for a
   thickening double) and Delay. Each voice reads the input through a delay that drifts at the new speed; when
   it runs out of room it jumps, choosing the jump whose waveform lines up best with what is playing (so the
   pitch stays true, not just the speed) and crossfading into it. Window trades smoothness (long) for tightness. */
const HARM_INT = [-24, -19, -12, -7, -5, -4, -3, 0, 3, 4, 5, 7, 9, 12, 19, 24];
const Harmonizer = {
  type:'harm', scope:'mono', cost:.2,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ v1:[9,0,15,1], v2:[11,0,15,1], lv1:[.7,0,1,.01], lv2:[0,0,1,.01], pan1:[-.4,-1,1,.01], pan2:[.4,-1,1,.01], detune:[0,0,30,.1], delay:[0,0,.15,.001], win:[.5,0,1,.01], dry:[1,0,1,.01] },
  create(sr){ return { b:new Float32Array(Math.ceil(sr * .5) + 4), w:0, v:[0, 1].map(() => ({ d:-1, o:0, f:1 })) }; },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, B = st.b, N = B.length;
    const W = (.02 + p.win * .08) * sr, pre = p.delay * sr + 2, dt = p.detune / 1200, F = Math.max(64, Math.min(W / 3, .012 * sr)), PER = Math.round(.02 * sr);
    const rd = d => { const r = st.w - d, ri = Math.floor(r), fr = r - ri, a = B[((ri % N) + N) % N], b = B[(((ri + 1) % N) + N) % N]; return a + (b - a) * fr; };
    const at = d => { let x = st.w - ((d + .5) | 0); if (x < 0) x += N; return B[x]; };
    const vs = [[HARM_INT[clamp(p.v1 | 0, 0, 15)] / 12 + dt, p.lv1, p.pan1], [HARM_INT[clamp(p.v2 | 0, 0, 15)] / 12 - dt, p.lv2, p.pan2]].map(([oct, lv, pan]) => {
      const a = (clamp(pan, -1, 1) + 1) * Math.PI / 4; return [Math.pow(2, oct), lv * Math.cos(a) * Math.SQRT2, lv * Math.sin(a) * Math.SQRT2]; });
    // the jump (by about half a window, refined over one period's worth of lags) that best matches the recent waveform
    const score = (d, dn, step) => { let c = 0, e = 1e-9; for (let j = 0; j < 384; j += step){ const y = at(dn + j); c += at(d + j) * y; e += y * y; } return c / Math.sqrt(e); };
    const best = (d, dir) => {
      const L0 = Math.round(W * .5); let bl = L0, bv = -Infinity;
      for (let L = L0; L < L0 + PER; L += 6){ const v = score(d, d + dir * L, 12); if (v > bv){ bv = v; bl = L; } }   // coarse
      const c0 = bl; bv = -Infinity;
      for (let L = c0 - 5; L <= c0 + 5; L++){ const v = score(d, d + dir * L, 3); if (v > bv){ bv = v; bl = L; } }   // fine
      return d + dir * bl;
    };
    for (let i = 0; i < ctx.block; i++){
      B[st.w] = (L[i] + R[i]) * .5;
      let yl = L[i] * p.dry, yr = R[i] * p.dry;
      for (let k = 0; k < 2; k++){
        const [ratio, gl, gr] = vs[k], v = st.v[k];
        if (gl === 0 && gr === 0){ v.d = -1; continue; }
        const lo = pre + 2 + F * Math.max(0, ratio - 1), hi = pre + W + PER;
        if (v.d < 0){ v.d = (lo + hi) * .5; v.f = 1; }
        v.d += 1 - ratio; if (v.f < 1) v.o += 1 - ratio;
        if (v.f >= 1){
          if (ratio > 1 && v.d < lo){ v.o = v.d; v.d = best(v.d, 1); v.f = 0; }
          else if (ratio < 1 && v.d > hi){ v.o = v.d; v.d = best(v.d, -1); v.f = 0; }
        }
        let s = rd(v.d);
        if (v.f < 1){ const q = v.f * Math.PI / 2; s = s * Math.sin(q) + rd(v.o) * Math.cos(q); v.f += 1 / F; }
        yl += s * gl; yr += s * gr;
      }
      oL[i] = yl; oR[i] = yr;
      st.w = st.w + 1 === N ? 0 : st.w + 1;
    }
  }
};

/* ---------- Blip voice (original code) ----------
   A minimal electronic drum voice in the spirit of the classic generative "blip" grooveboxes: a parabolic tone with
   a kick (a fast pitch drop), FM, ring mod, a little noise and click, an exponential decay, drive, a bipolar filter
   (left of centre low-pass, right high-pass) and a grain stutter (the hit repeated, each quieter). Self-contained:
   it is copied as text into the pads' engine and into the …chains/…waves worklets. Mono. */
class BlipVoice {
  constructor(sr){ this.sr = sr; this.p = BlipVoice.defaults(); this.reset(); }
  static defaults(){
    return {pitch:180, kick:0.4, kickT:35, fm:0, fmR:1.5, ring:0, ringF:300, noise:0, click:0.2, atk:0.5, decay:220, drive:0, filt:0, res:0.2, grain:0, grainT:60, vel:1};
  }
  reset(){ this.on = false; this.t = 0; this.ph = 0; this.mph = 0; this.rph = 0; this.lp = 0; this.bp = 0; this.rnd = 7777; this.g = 0; this.rep = 0; this.rg = 1; }
  set(o){ this.p = Object.assign(BlipVoice.defaults(), o || {}); }
  trig(vel){ this.on = true; this.t = 0; this.ph = 0; this.mph = 0; this.rep = 0; this.rg = vel == null ? 1 : vel; this.v0 = this.rg; }
  // Adds (does not write) samples [a, b) of the voice into out, times gain g.
  render(out, a, b, g){
    if (!this.on) return;
    const p = this.p, sr = this.sr, inv = 1 / sr, kd = Math.exp(-inv * 1000 / Math.max(1, p.kickT)), ad = Math.exp(-inv * 1000 * 6.9 / Math.max(2, p.decay));
    const atk = Math.max(1, p.atk / 1000 * sr), fmI = p.fm * 8, dr = p.drive * 8, dn = dr ? 1 / Math.tanh(dr) : 1;
    const filt = Math.max(-1, Math.min(1, p.filt)), fc = filt < 0 ? 20000 * Math.pow(0.004, -filt) : 20 * Math.pow(400, filt);
    const f = 2 * Math.sin(Math.PI * Math.min(0.45, fc / sr)), q = 2 - 1.95 * Math.max(0, Math.min(1, p.res)), gT = Math.max(5, p.grainT) / 1000 * sr, rc = Math.sqrt(q / 2);   // rc: resonance doesn't jump the level
    for (let i = a; i < b; i++){
      const t = this.t;
      // grain: the hit starts again every Grain time, each repeat quieter
      if (p.grain >= 1 && this.rep < (p.grain | 0) && t >= gT * (this.rep + 1)){ this.rep++; this.rg = this.v0 * Math.pow(0.72, this.rep); this.ke = 1; this.env = 0; this.ph = 0; }
      if (t === 0){ this.ke = 1; this.env = 0; }
      const tl = t - this.rep * gT;
      // envelope: linear attack, then exponential decay
      this.env = tl < atk ? tl / atk : this.env * ad;
      this.ke *= kd;
      const kk = p.kick * 5 * this.ke, hz = kk > 1e-4 ? p.pitch * Math.exp(kk * 0.6931471805599453) : p.pitch;
      // FM: a sine at fmR × pitch modulates the phase
      this.ph += hz * inv; if (this.ph >= 1) this.ph -= Math.floor(this.ph);
      let pm = this.ph;
      if (fmI > 0){ this.mph += hz * p.fmR * inv; if (this.mph >= 1) this.mph -= Math.floor(this.mph); pm += fmI * Math.sin(2 * Math.PI * this.mph) * this.env * 0.159; }
      const x0 = pm - Math.floor(pm), u = x0 < 0.5 ? 2 * x0 : 2 * x0 - 1;
      let y = (x0 < 0.5 ? 4 : -4) * u * (1 - u);   // parabolic arcs: a sine with a little more edge
      // ring mod: depth from tremolo to metal
      if (p.ring > 0){ this.rph += p.ringF * inv; if (this.rph >= 1) this.rph -= Math.floor(this.rph); y *= 1 - p.ring + p.ring * Math.sin(2 * Math.PI * this.rph); }
      // noise and click
      this.rnd = (this.rnd * 1664525 + 1013904223) >>> 0; const nz = this.rnd / 2147483648 - 1;
      y = y * (1 - p.noise * 0.7) + nz * p.noise;
      if (tl < sr * 0.0015) y += p.click * (1 - tl / (sr * 0.0015)) * (nz * 0.5 + 0.8);
      y *= this.env * this.rg;
      if (dr) y = Math.tanh(y * dr) * dn;
      // bipolar state-variable filter
      if (filt !== 0){ const hp = y - this.lp - q * this.bp; this.bp += f * hp; this.lp += f * this.bp; if (!(Math.abs(this.lp) < 1e3)){ this.lp = this.bp = 0; } y = (filt < 0 ? this.lp : hp) * rc; }
      out[i] += y * g * 0.8;
      this.t++;
      if (this.env < 1e-5 && tl > atk && (p.grain < 1 || this.rep >= (p.grain | 0))){ this.on = false; break; }
    }
  }
}

/* ── Blip drum and Life (added Oct 2026, original code) ──────────────────────────────────────────────────────
   Blip drum: eight BlipVoices, one per trigger input, edited as bar charts (one bar per voice) in the editor.
   Life: Conway's Game of Life (and four relatives) on a 16 × 8 grid that wraps at its edges. A playhead sweeps the
   columns one step per note value; cells alive in its column fire the gate of their row (eight rows, eight gates).
   After each sweep the grid lives one generation. Loop brings back the drawn grid every N generations. */
const BD_ROWS = ['p', 'k', 'f', 'm', 'n', 'd', 'a', 'x'];   // pitch, kick, FM, ring, noise, decay, level, filter
const BD_DEF = { p:[[50, 120, 220, 400, 900, 1800, 3500, 6000], 20, 8000], k:[[0.8, 0.5, 0.3, 0.2, 0.1, 0, 0, 0], 0, 1], f:[[0, 0, 0.2, 0, 0.3, 0, 0.1, 0], 0, 1],
  m:[[0, 0, 0, 0.3, 0, 0.5, 0, 0.6], 0, 1], n:[[0, 0.1, 0.3, 0, 0, 0.5, 0.8, 0.9], 0, 1], d:[[600, 250, 180, 300, 120, 90, 60, 40], 5, 3000], a:[[0.9, 0.8, 0.7, 0.6, 0.6, 0.55, 0.5, 0.45], 0, 1],
  x:[[0, 0, 0, 0, 0, 0.3, 0.5, 0.6], -1, 1] };
const bdParams = () => {
  const P = { edit:[0, 0, 7, 1], fmR:[1.5, .25, 12, .01], ringR:[2.3, .01, 16, .01], kickT:[35, 2, 400, 1], dec:[1, .1, 4, .01], drive:[.15, 0, 1, .01],
    lfo:[.5, .05, 20, .01], lfoD:[0, 0, 1, .01], width:[.5, 0, 1, .01], dly:[0, 0, 1, .01], dfb:[.45, 0, .95, .01], ddiv:[4, 0, 9, 1], level:[.8, 0, 1.5, .01] };
  for (const r of BD_ROWS) for (let i = 0; i < 8; i++){ const [d, mn, mx] = BD_DEF[r]; P[r + (i + 1)] = [d[i], mn, mx, r === 'p' || r === 'd' ? 1 : .01]; }
  return P;
};
const BlipDrum = {
  type:'blipdrum', scope:'mono', cost:.25,
  inputs:{ t1:'gate', t2:'gate', t3:'gate', t4:'gate', t5:'gate', t6:'gate', t7:'gate', t8:'gate' }, outputs:{ out:'audio', outR:'audio' },
  params:bdParams(),
  create(sr){ return { v:Array.from({length:8}, () => new BlipVoice(sr)), lg:new Uint8Array(8), buf:new Float32Array(128), d:new Float32Array(Math.ceil(sr * 2.2) + 4), dw:0, lp:[0, 0], lph:0, hit:new Float32Array(8) }; },
  process(st, io, ctx){
    const p = io.p, n = ctx.block, sr = ctx.sr, oL = io.out.out, oR = io.out.outR, B = st.buf;
    oL.fill(0); oR.fill(0);
    st.lph += p.lfo * n / sr; st.lph -= Math.floor(st.lph);
    for (let k = 0; k < 8; k++){
      const g = io.in['t' + (k + 1)], v = st.v[k];
      let at = -1; for (let i = 0; i < n; i++){ const on = g[i] >= .5; if (on && !st.lg[k]) at = i; st.lg[k] = on ? 1 : 0; if (at >= 0) break; }
      if (at >= 0){
        const j = k + 1, pitch = p['p' + j] * Math.pow(2, p.lfoD * Math.sin(2 * Math.PI * (st.lph + k * .125)));
        v.set({ pitch, kick:p['k' + j], kickT:p.kickT, fm:p['f' + j], fmR:p.fmR, ring:p['m' + j], ringF:pitch * p.ringR, noise:p['n' + j], click:.15 + p['k' + j] * .3,
          decay:p['d' + j] * p.dec, drive:0, filt:p['x' + j], res:.25 });
        v.trig(1); st.hit[k] = 1;
      }
      if (!v.on) continue;
      B.fill(0); v.render(B, 0, n, p['a' + (k + 1)]);
      const pan = (k / 7 - .5) * p.width, gl = Math.cos((pan + .5) * Math.PI / 2), gr = Math.sin((pan + .5) * Math.PI / 2);
      for (let i = 0; i < n; i++){ oL[i] += B[i] * gl; oR[i] += B[i] * gr; }
    }
    // a feedback delay at a note value, for washes (cross-fed, darkened, saturated)
    if (p.dly > 0 || st.dOn){
      st.dOn = p.dly > 0;
      const D = st.d, N = D.length, dt = Math.min(N / 2 - 2, Math.round(60 / (ctx.transport.tempo || 120) * divBeats(p.ddiv) * sr)), fb = p.dfb, k = .35;
      for (let i = 0; i < n; i++){
        const r = st.dw - dt * 2, rl = ((r % N) + N) % N, rr = ((r + 1) % N + N) % N;
        st.lp[0] += (D[rl] - st.lp[0]) * k; st.lp[1] += (D[rr] - st.lp[1]) * k;
        const l = oL[i], rgt = oR[i];
        D[st.dw] = Math.tanh(l + st.lp[1] * fb); D[(st.dw + 1) % N] = Math.tanh(rgt + st.lp[0] * fb);
        oL[i] = l + st.lp[0] * p.dly; oR[i] = rgt + st.lp[1] * p.dly;
        st.dw = (st.dw + 2) % N;
      }
    }
    // drive on the mix (one saturator, not eight), then level
    const lv = p.level, dr = p.drive * 6, dn = dr ? 1 / Math.tanh(dr) : 1;
    if (dr) for (let i = 0; i < n; i++){ oL[i] = Math.tanh(oL[i] * dr) * dn * lv; oR[i] = Math.tanh(oR[i] * dr) * dn * lv; }
    else for (let i = 0; i < n; i++){ oL[i] *= lv; oR[i] *= lv; }
    for (let k = 0; k < 8; k++) st.hit[k] *= .9;
  },
  show(st){ return Array.from(st.v, (v, k) => Math.max(st.hit[k], v.on ? Math.min(1, v.env || 0) : 0)); }
};

const LIFE_RULES = [[[3], [2, 3]], [[3, 6], [2, 3]], [[2], []], [[3], [1, 2, 3, 4, 5]], [[3, 6, 7, 8], [3, 4, 6, 7, 8]]];   // Life, HighLife, Seeds, Maze, Day & Night
const lifeParams = () => {
  const P = { w:[16, 4, 16, 1], div:[3, 0, 9, 1], gens:[8, 0, 32, 1], trig:[0, 0, 1, 1], rule:[0, 0, 4, 1], rot:[0, 0, 7, 1], len:[.5, .05, .95, .01] };
  const glider = [0, 0, 0b10, 0b100, 0b111, 0, 0, 0];   // a glider, top left
  for (let y = 0; y < 8; y++) P['b' + y] = [glider[y], 0, 65535, 1];
  return P;
};
const Life = {
  type:'life', scope:'mono', cost:.02,
  inputs:{ clock:'gate', reset:'gate' }, outputs:{ g1:'gate', g2:'gate', g3:'gate', g4:'gate', g5:'gate', g6:'gate', g7:'gate', g8:'gate', any:'gate', count:'cv' },
  params:lifeParams(),
  create:() => ({ cur:new Uint16Array(8), prev:new Uint16Array(8), x:-1, ph:1, gen:0, lc:0, lr:0, sn:undefined, sig:'', fire:0, cnt:0 }),
  load(st, p){ for (let y = 0; y < 8; y++){ st.cur[y] = p['b' + y] & 0xffff; st.prev[y] = 0; } st.gen = 0; },
  next(st, p){
    const W = clamp(p.w | 0, 4, 16), m = (1 << W) - 1, [born, keep] = LIFE_RULES[clamp(p.rule | 0, 0, 4)], nx = new Uint16Array(8);
    const at = (x, y) => (st.cur[(y + 8) % 8] >> ((x + W) % W)) & 1;
    for (let y = 0; y < 8; y++) for (let x = 0; x < W; x++){
      let c = 0; for (let dy = -1; dy <= 1; dy++) for (let dx = -1; dx <= 1; dx++) if (dx || dy) c += at(x + dx, y + dy);
      const a = at(x, y); if (a ? keep.includes(c) : born.includes(c)) nx[y] |= 1 << x;
    }
    st.prev.set(st.cur); for (let y = 0; y < 8; y++) st.cur[y] = nx[y] & m;
    st.gen++;
  },
  process(st, io, ctx){
    const p = io.p, W = clamp(p.w | 0, 4, 16), ck = linked(io, 'clock');
    const SG = st.sg || (st.sg = new Int32Array(8).fill(-1)); let ch = false;
    for (let y = 0; y < 8; y++){ const v = p['b' + y] | 0; if (v !== SG[y]){ SG[y] = v; ch = true; } }
    if (ch) Life.load(st, p);   // the drawn grid changed: start from it
    if (restarted(st, ctx)){ Life.load(st, p); st.x = -1; st.ph = 1; }
    const inc = (ctx.transport.tempo || 120) / 60 / divBeats(p.div) / ctx.sr, o = io.out, G = st.G || (st.G = []);
    for (let k = 0; k < 8; k++) G[k] = o['g' + (k + 1)];
    const ANY = o.any, CNT = o.count, RS = io.in.reset, CK = io.in.clock;
    const step = () => {
      st.x++;
      if (st.x >= W){ st.x = 0; if ((p.gens | 0) > 0 && st.gen + 1 >= (p.gens | 0)) Life.load(st, p); else Life.next(st, p); }
      let f = 0, c = 0;
      for (let y = 0; y < 8; y++){ const a = (st.cur[y] >> st.x) & 1, b = (st.prev[y] >> st.x) & 1; c += a; if (p.trig >= .5 ? a && !b : a) f |= 1 << ((y + (p.rot | 0)) % 8); }
      st.fire = f; st.cnt = c;
    };
    for (let i = 0; i < ctx.block; i++){
      const r = RS[i] >= .5; if (r && !st.lr){ Life.load(st, p); st.x = -1; st.ph = 1; } st.lr = r;
      let on;
      if (ck){ const c = CK[i] >= .5; if (c && !st.lc) step(); st.lc = c; on = c; }
      else { st.ph += inc; if (st.ph >= 1){ st.ph -= Math.floor(st.ph); step(); } on = st.ph < p.len; }
      const f = on ? st.fire : 0;
      for (let k = 0; k < 8; k++) G[k][i] = (f >> k) & 1;
      ANY[i] = f ? 1 : 0; CNT[i] = st.cnt / 8;
    }
  },
  show(st){ return [st.x, st.gen, ...st.cur]; }
};

/* ---------- Degrade (original code) ----------
   A lo-fi / degrade processor after the classic tracker "lofi" plugins: bit crush (Bits, continuous), sample-rate
   reduction (Rate, sample and hold), and a noise floor (Noise, Colour from white to dark, Follow: how much the hiss
   tracks the sound's level, so silence stays clean). Dry and Wet are separate. Self-contained: one copy each in the
   pads' engine, the track-FX worklet and the …chains/…waves worklets. In place, stereo. */
class DegradeCore {
  constructor(sr){ this.sr = sr; this.ph = 1; this.hl = 0; this.hr = 0; this.nl = 0; this.nr = 0; this.env = 0; this.s = 0x2545f491; }
  static defaults(){ return {bits:16, rate:48000, noise:0.15, color:0.3, follow:0.6, dry:0, wet:1}; }
  // the noise level (RMS) for a Noise setting: squared, so the bottom of the range is a whisper (−∞ … −12 dBFS)
  static level(n){ return n * n * 0.25; }
  process(L, R, n, p){
    const sr = this.sr, rate = Math.min(p.rate, sr), hold = rate < sr * 0.98, inc = rate / sr;
    const bits = Math.max(1, Math.min(24, p.bits)), crush = bits < 23.5, q = Math.pow(2, bits - 1);
    const lv = DegradeCore.level(Math.max(0, Math.min(1, p.noise))), col = Math.max(0, Math.min(1, p.color)), k = 1 - col * 0.96, comp = Math.sqrt((2 - k) / k) / 0.577;   // the same loudness (RMS) whatever the colour
    const fol = Math.max(0, Math.min(1, p.follow)), ka = 1 - Math.exp(-1 / (sr * 0.004)), kr = 1 - Math.exp(-1 / (sr * 0.15)), dry = p.dry, wet = p.wet;
    for (let i = 0; i < n; i++){
      const xl = L[i], xr = R[i];
      // envelope of the input, for Follow
      const a = Math.max(Math.abs(xl), Math.abs(xr)); this.env += (a - this.env) * (a > this.env ? ka : kr);
      // sample-rate reduction: hold a sample until the next tick of the slower clock
      if (hold){ this.ph += inc; if (this.ph >= 1){ this.ph -= Math.floor(this.ph); this.hl = xl; this.hr = xr; } } else { this.hl = xl; this.hr = xr; }
      let yl = this.hl, yr = this.hr;
      if (crush){ yl = Math.round(yl * q) / q; yr = Math.round(yr * q) / q; }
      // the noise floor: two white noises (stereo hiss), darkened by Colour, scaled by Follow
      if (lv > 0){
        let s = this.s; s ^= s << 13; s ^= s >>> 17; s ^= s << 5; const w1 = (s >>> 0) / 2147483648 - 1; s ^= s << 13; s ^= s >>> 17; s ^= s << 5; const w2 = (s >>> 0) / 2147483648 - 1; this.s = s;
        this.nl += (w1 - this.nl) * k; this.nr += (w2 - this.nr) * k;
        const g = lv * comp * ((1 - fol) + fol * Math.min(1, this.env * 6));
        yl += this.nl * g; yr += this.nr * g;
      }
      L[i] = xl * dry + yl * wet; R[i] = xr * dry + yr * wet;
    }
  }
}

/* ---------- Resonator (original code) ----------
   Tuned resonators that ring at a note and a chord: up to five feedback delay lines (Karplus–Strong style), each
   tuned to a chord tone above Note, with a damping filter in the loop (Bright) and a decay time (Decay). String rings
   with every harmonic; Tube (negative feedback) only the odd ones, hollow like a pipe. Spread places the voices
   across the stereo field. The input excites them all; Mix blends with the dry sound. Self-contained: one copy each
   in the pads' engine, the track-FX worklet and the …chains/…waves worklets. In place, stereo. */
class ResonCore {
  constructor(sr){ this.sr = sr; this.v = []; for (let i = 0; i < 5; i++) this.v.push({ b:new Float32Array(Math.ceil(sr / 20) + 4), w:0, lp:0, dc:0, dx:0 }); this.sig = ''; }
  static defaults(){ return {note:48, chord:3, decay:0.5, bright:0.6, mode:0, spread:0.5, mix:0.5}; }
  static get CHORDS(){ return [[0], [0, 12, 24], [0, 7, 12, 19], [0, 4, 7, 12, 16], [0, 3, 7, 12, 15], [0, 5, 7, 12, 17], [0, 4, 7, 11, 14], [0, 3, 7, 10, 14], [0, 4, 7, 10, 12], [0, 5, 10, 15, 20], [0, 12, 19, 24, 28], [0, 1, 2, 3, 4]]; }
  static get CHORD_NAMES(){ return ['Unison', 'Octaves', 'Fifths', 'Major', 'Minor', 'Sus4', 'Major 7', 'Minor 7', 'Dominant 7', 'Fourths', 'Harmonics', 'Cluster']; }
  process(L, R, n, p){
    const sr = this.sr, C = ResonCore.CHORDS[Math.max(0, Math.min(11, p.chord | 0))], nv = C.length, tube = p.mode >= 0.5;
    const t60 = 0.05 * Math.pow(160, Math.max(0, Math.min(1, p.decay))), a = 0.08 + 0.9 * Math.max(0, Math.min(1, p.bright)), mix = p.mix, sp = p.spread;
    const ing = 0.5 / Math.sqrt(nv);
    for (let k = 0; k < nv; k++){
      const V = this.v[k], f = 440 * Math.pow(2, (p.note + C[k] - 69) / 12);
      // loop length: one period (half for Tube, whose sign flip doubles it), less the damping filter's own delay
      const per = sr / f * (tube ? 0.5 : 1);
      V.d = Math.max(2, Math.min(V.b.length - 3, per - (1 - a) / a));
      V.g = Math.pow(10, -3 * (per / sr) / t60) * (tube ? -1 : 1);
      const pan = nv > 1 ? (k / (nv - 1) - 0.5) * sp : 0; V.gl = Math.cos((pan + 0.5) * Math.PI / 2); V.gr = Math.sin((pan + 0.5) * Math.PI / 2);
    }
    for (let i = 0; i < n; i++){
      const x = (L[i] + R[i]) * 0.5 * ing; let yl = 0, yr = 0;
      for (let k = 0; k < nv; k++){
        const V = this.v[k], B = V.b, N = B.length, r = V.w - V.d, ri = Math.floor(r), fr = r - ri;
        const i0 = ri < 0 ? ri + N : ri, i1 = i0 + 1 >= N ? 0 : i0 + 1, d = B[i0] + (B[i1] - B[i0]) * fr;
        V.lp += (d - V.lp) * a;
        let y = x + V.lp * V.g;
        y = y > 1.5 ? 1.5 : y < -1.5 ? -1.5 : y;
        B[V.w] = y; V.w = V.w + 1 === N ? 0 : V.w + 1;
        const o = V.lp - V.dx + 0.995 * V.dc; V.dx = V.lp; V.dc = o;   // DC block on the way out
        yl += o * V.gl; yr += o * V.gr;
      }
      L[i] = L[i] * (1 - mix) + Math.tanh(yl) * mix; R[i] = R[i] * (1 - mix) + Math.tanh(yr) * mix;
    }
  }
}

/* Resonator: chord-tuned resonators (see ResonCore). */
const Reson = {
  type:'reson', scope:'mono', cost:.06,
  inputs:{ in:'audio', inR:'audio', note:'cv' }, outputs:{ out:'audio', outR:'audio' },
  params:{ note:[48, 24, 96, 1], chord:[3, 0, 11, 1], mode:[0, 0, 1, 1], decay:[.5, 0, 1, .01], bright:[.6, 0, 1, .01], spread:[.5, 0, 1, .01], mix:[.5, 0, 1, .01] },
  create:sr => ({ r:new ResonCore(sr), pp:{} }),
  process(st, io, ctx){
    const L = io.out.out, R = io.out.outR, p = io.p, q = st.pp; L.set(io.in.in); R.set(inR(io));
    // Note CV (0.5 = A4, 0.125 per octave) sets the pitch; then the knob transposes it from C3
    q.note = linked(io, 'note') ? 69 + (io.in.note[0] - .5) * 96 + (p.note - 48) : p.note; q.chord = p.chord; q.mode = p.mode; q.decay = p.decay; q.bright = p.bright; q.spread = p.spread; q.mix = p.mix;
    st.r.process(L, R, ctx.block, q);
  }
};

/* Degrade: lo-fi crush and a noise floor (see DegradeCore). Rate is 0–1 here: 300 Hz × 160^rate, the top is off. */
const Degrade = {
  type:'degrade', scope:'mono', cost:.06,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ bits:[24, 1, 24, .1], rate:[1, 0, 1, .001], noise:[.15, 0, 1, .01], color:[.3, 0, 1, .01], follow:[.6, 0, 1, .01], dry:[0, 0, 1, .01], wet:[1, 0, 1, .01] },
  create:sr => ({ d:new DegradeCore(sr), pp:{} }),
  process(st, io, ctx){
    const L = io.out.out, R = io.out.outR, p = io.p, q = st.pp; L.set(io.in.in); R.set(inR(io));
    q.bits = p.bits; q.rate = p.rate >= .999 ? 48000 : 300 * Math.pow(160, p.rate); q.noise = p.noise; q.color = p.color; q.follow = p.follow; q.dry = p.dry; q.wet = p.wet;
    st.d.process(L, R, ctx.block, q);
  }
};

const SHARED_MODULES = { beads:Beads,
  flanger:FlangerFx, tremolo:TremoloFx, odrive:OdriveFx, folder:FolderFx, autowah:AutowahFx, pshift:PshiftFx, ladder:LadderFx, xmod:XmodFx, space:SpaceFx, lofi:LofiFx, drive:DriveFx, air:AirFx, room:RoomFx,
  comb:CombFx, transient:TransientFx, tgate:TgateFx, ott:OttFx, fshift:FshiftFx, vinyl:VinylFx, dub:DubFx, cab:CabFx, tube:TubeFx,
  envfol:EnvFol, gateex:GateEx, pitchfol:PitchFol, slew:Slew, snh:SampleHold, quant:Quant, blades:Blades, meta:Meta, phaser:Phaser, width:Width, pingpong:PingPong,
  meter:Meter, limiter:Limiter, utility:Utility, ringmod:RingMod, spring:Spring, plate:Plate, ensemble:Ensemble, math:MathM, logic:Logic, divider:Divider, euclid:Euclid,
  dimension:Dimension, bbd:BBD, harm:Harmonizer, blipdrum:BlipDrum, life:Life, degrade:Degrade, reson:Reson };
/* ===SHARED MODULES END=== */

const MODULES = { ...SHARED_MODULES, hostIn:HostIn, vco:VCO, fm:FM, additive:Additive, noise:Noise, plaits:Plaits, elements:Elements,
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
    // A seed makes every random thing in this engine (noise, Random, oscillator start phases' seeds) repeat exactly:
    // ...Thunder uses it for renders, so they come out the same every time.
    if (opts.seed != null) seedN = opts.seed >>> 0;
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
    // The live value of every modulated parameter, so the page can show it (as in ...Seeds). Poly modules show the
    // newest playing voice; with no voice awake, mono sources (a free-running LFO) still move the slider's mark.
    const mods = [];
    const rows = this.voices.rows, hold = this.sr * 12;
    let v0 = -1;
    for (let i = 0; i < rows.length; i++) if (rows[i].gate && (v0 < 0 || rows[i].age > rows[v0].age)) v0 = i;
    if (v0 < 0) for (let i = 0; i < rows.length; i++) if (this.voices.awake(i, hold) && (v0 < 0 || rows[i].age > rows[v0].age)) v0 = i;
    for (const m of this.graph.mods.values()){
      let sums = null;
      for (const c of this.graph.inc.get(m.id) || []){
        if (!isParamPort(c.b[1])) continue;
        const k = c.b[1].slice(2); if (!(k in m.def.params)) continue;
        const src = this.graph.mods.get(c.a[0]); if (!src) continue;
        sums = sums || {}; if (!(k in sums)) sums[k] = 0;
        const io = src.poly ? (v0 >= 0 ? this._ios.get(src.id + '#' + v0) : null) : this._ios.get(src.id);
        const b = io && !io.asleep && io.out[c.a[1]]; if (b) sums[k] += b[0];
      }
      if (!sums) continue;
      for (const k in sums){
        const [, mn, mx, st] = m.def.params[k];
        let r = clamp(m.inst.params[k] + sums[k] * (mx - mn), mn, mx);
        if (st >= 1) r = Math.round((r - mn) / st) * st + mn;
        mods.push([m.id, k, r]);
      }
    }
    // Modules that show something (a meter's levels, a limiter's gain reduction): their latest values.
    const shows = [];
    for (const m of this.graph.mods.values()){
      if (!m.def.show) continue;
      const s = this._states.get(m.id) || this._states.get(m.id + '#0');
      if (s) try { shows.push([m.id, m.def.show(s.st)]); } catch (e) {}
    }
    return { load:this.load, active, quality:this.quality, buses, mods, shows, wasm:{ dsp:!!WASM.dsp, tfx:!!WASM.tfxX } };
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
    if (e.telemetryOn && (++this._blocks % 12) === 0) this.port.postMessage({ target:TARGET, op:OP.meter, value:e.telemetry() });
    else if (!e.telemetryOn) this._blocks++;
    return true;
  }
}
registerProcessor('waves', WavesProcessor);
} else if (typeof globalThis !== 'undefined'){
  globalThis.WavesDSP = { WavesEngine, wasmInit, MODULES, WASM, BUS_COUNT };
}
})();
