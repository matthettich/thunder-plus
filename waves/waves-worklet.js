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
      if (st.buf !== x.memory.buffer){ st.buf = x.memory.buffer; st.vo = new Float32Array(st.buf, x.pl_out(), 12); st.va = new Float32Array(st.buf, x.pl_aux(), 12); }   // views kept per voice
      a.set(st.vo, 1); b.set(st.va, 1);
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
      if (st.buf !== x.memory.buffer){ st.buf = x.memory.buffer; st.vo = new Float32Array(st.buf, x.el_main(), 16); st.va = new Float32Array(st.buf, x.el_aux(), 16); }
      a.set(st.vo, 1); b.set(st.va, 1);
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
      // TPT state-variable (as Blades): stable at every cutoff and resonance. The old Chamberlin/TPT mix doubled the
      // integrator step and ran away above cutoff ~0.45 at the default resonance.
      const g = Math.tan(Math.PI * fc), a1 = 1 / (1 + g * (g + q)), a2 = g * a1, a3 = g * a2;
      const x = io.in.in[i], v3 = x - st.ic2, bp = a1 * st.ic1 + a2 * v3, lp = st.ic2 + a2 * st.ic1 + a3 * v3;
      st.ic1 = 2 * bp - st.ic1; st.ic2 = 2 * lp - st.ic2;
      if (!(Math.abs(st.ic1) < 1e4 && Math.abs(st.ic2) < 1e4)) st.ic1 = st.ic2 = 0;
      io.out.out[i] = mode === 0 ? lp : mode === 1 ? bp : x - q * bp - lp;
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

/* Mixer: Inputs (chans) picks how many channels are in use, 1 to 16 (old patches load with 4). Channels above it are
   skipped, and only patched inputs cost anything. Names are made once here, so nothing is built per block. */
const MIXN = 16;
const MXI = Array.from({ length:MIXN }, (_, i) => 'in' + (i + 1)), MXL = MXI.map((_, i) => 'l' + (i + 1));
const Mixer = (() => {
  const inputs = {}, params = { chans:[4,1,MIXN,1] };
  for (let i = 0; i < MIXN; i++){ inputs[MXI[i]] = 'audio'; params[MXL[i]] = [.8,0,1.5,.01]; }
  params.level = [1,0,2,.01];
  return {
    type:'mixer', scope:'poly', cost:.1, inputs, outputs:{ out:'audio' }, params,
    create:() => ({}),
    process(st, io, ctx){
      const p = io.p, o = io.out.out, n = ctx.block, N = clamp(Math.round(p.chans) || 4, 1, MIXN);
      o.fill(0);
      for (let k = 0; k < N; k++){
        if (!linked(io, MXI[k])) continue;
        const x = io.in[MXI[k]], g = p[MXL[k]] * p.level;
        for (let i = 0; i < n; i++) o[i] += x[i] * g;
      }
    }
  };
})();

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
      // Views into the wasm memory are kept on the state (remade only when the memory grew or the handle changed):
      // five new typed arrays per effect per block were steady garbage on the audio thread.
      if (st.buf !== buf || st.vh !== st.h){ st.buf = buf; st.vh = st.h; st.P = new Float32Array(buf, x.tfx_params(st.h), 12); st.L = new Float32Array(buf, x.tfx_l(), 128); st.R = new Float32Array(buf, x.tfx_r(), 128); }
      const P = st.P, L = st.L, R = st.R; P.fill(0); P.set(map(io.p, ctx));
      // The wasm only checks its output. A NaN or infinity on the way IN hangs the tape delay for good and leaves
      // twenty other effects silent, so bad or absurd samples are zeroed here first.
      // The ±1e-18 (−360 dB, alternating so it has no DC) keeps the effects' filters and delay lines out of denormal
      // numbers on silence, where x86 (the Windows PC) runs some of them up to 30× slower.
      for (let i = 0; i < ctx.block; i++){ const a = L0[i], b = R0[i], dn = i & 1 ? 1e-18 : -1e-18; L[i] = (a > -8 && a < 8 ? a : 0) + dn; R[i] = (b > -8 && b < 8 ? b : 0) + dn; }
      try { x.tfx_process(st.h, ctx.block); }
      catch (e){ try { x.tfx_free(st.h); } catch (e2){} st.h = -1; io.out.out.set(L0); io.out.outR.set(R0); return; }   // crashed: bypass it
      if (st.buf !== x.memory.buffer){ st.buf = x.memory.buffer; st.P = new Float32Array(st.buf, x.tfx_params(st.h), 12); st.L = new Float32Array(st.buf, x.tfx_l(), 128); st.R = new Float32Array(st.buf, x.tfx_r(), 128); }   // the memory grew inside process
      const L2 = st.L, R2 = st.R;
      let bad = false; for (let i = 0; i < ctx.block; i++){ const a = L2[i], b = R2[i]; if (!(a > -1e4 && a < 1e4 && b > -1e4 && b < 1e4)){ bad = true; break; } }
      if (bad){ try { x.tfx_free(st.h); st.h = x.tfx_new(id, ctx.sr); } catch (e){ st.h = -1; } io.out.out.fill(0); io.out.outR.fill(0); return; }   // its state went bad: start it afresh
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
// Size exactly 1 (the line at the end of its shrunk buffer) never decays: it sits at 5× the input for good.
const SPACE_SIZE_MAX = 0.99;
const SpaceFx = effect('space', 20, { size:[0.5,0,1,.01], decay:[0.55,0,1,.01], diff:[0.7,0,1,.01], mod:[0.4,0,1,.01], tone:[0.6,0,1,.01], lowcut:[0.2,0,1,.01], pre:[0.1,0,1,.01], mix:[0.35,0,1,.01] },
  p => [Math.min(p.size, SPACE_SIZE_MAX), p.decay, p.diff, p.mod, p.tone, p.lowcut, p.pre, p.mix], 2);
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

/* Pitch Track (monophonic, original code): a YIN pitch detector on a 4× decimated copy of the input, run about 90
   times a second. Outputs the pitch as CV (0.5 = A4, one octave = 0.125, like Pitch Follow), the same rounded to the
   nearest semitone (Note), a voiced Gate, a Trig on every new note, and the input's level (Env). Transpose shifts
   both pitch outputs; Sensitivity is how sure it has to be (higher hears more, and more wrong notes). */
const PitchTrk = {
  type:'pitchtrk', scope:'mono', cost:.3,
  inputs:{ in:'audio' }, outputs:{ out:'cv', note:'cv', gate:'gate', trig:'gate', env:'cv' },
  params:{ sens:[.15,.03,.5,.01], thr:[.02,0,1,.001], glide:[.03,0,.5,.001], trans:[0,-24,24,1], min:[60,30,500,1], max:[1200,100,3000,1], depth:[1,0,1,.01] },
  create:() => ({ buf:new Float32Array(1024), w:0, n:0, dec:0, acc:0, lp1:0, lp2:0, k:0, f:0, ph:.5, ver:0, note:-1, cand:-1, candN:0, trig:0, env:0, d:new Float32Array(400) }),
  process(st, io, ctx){
    const x = io.in.in, sr = ctx.sr, B = ctx.block, P = io.p, N = st.buf.length; let pk = 0;
    // 4× decimation behind a two-pole low-pass at ~1/5 of the new rate
    const c = 1 - Math.exp(-2 * Math.PI * (sr / 20) / sr);
    for (let i = 0; i < B; i++){ const v = x[i]; st.lp1 += (v - st.lp1) * c; st.lp2 += (st.lp1 - st.lp2) * c;
      if (++st.dec >= 4){ st.dec = 0; st.buf[st.w] = st.lp2; st.w = (st.w + 1) % N; if (st.n < N) st.n++; }
      const a = v < 0 ? -v : v; if (a > pk) pk = a; }
    st.env += (pk - st.env) * (pk > st.env ? .5 : .05);
    if (++st.k >= 4 && st.n >= N){ st.k = 0;
      const rs = sr / 4, W = 320, tMin = Math.max(2, Math.floor(rs / Math.max(P.max, P.min + 1))), tMax = Math.min(st.d.length - 2, Math.ceil(rs / Math.max(20, P.min)));
      const o = (st.w - W - tMax - 1 + N * 2) % N, b = st.buf, d = st.d; let run = 0, best = -1;
      d[0] = 1;
      for (let t = 1; t <= tMax; t++){
        let s = 0; for (let j = 0; j < W; j++){ const q = b[(o + j) % N] - b[(o + j + t) % N]; s += q * q; }
        run += s; d[t] = run > 0 ? s * t / run : 1;
        if (best < 0 && t > tMin && d[t - 1] < P.sens && d[t] >= d[t - 1]){ best = t - 1; break; }
      }
      let f = 0;
      if (best > tMin){ const a = d[best - 1], m = d[best], z = d[best + 1] != null ? d[best + 1] : m, den = a - 2 * m + z;
        const tt = best + (den > 1e-9 ? (a - z) / (2 * den) : 0); f = rs / tt; }
      const voiced = st.env > P.thr && f >= P.min && f <= P.max;
      if (voiced){ st.f = f;
        const nn = Math.round(69 + 12 * Math.log2(f / 440));
        if (nn === st.cand) st.candN++; else { st.cand = nn; st.candN = 1; }
        if (st.candN >= 2 && nn !== st.note){ st.note = nn; st.trig = Math.ceil(.005 * sr / B) + 1; } }
      else if (st.ver){ st.note = -1; st.cand = -1; }
      st.ver = voiced ? 1 : 0;
    }
    const g = 1 - Math.exp(-B / (sr * Math.max(P.glide, 1e-4))), tr = P.trans / 96;
    const target = st.ver && st.f > 0 ? Math.max(0, Math.min(1, 0.5 + Math.log2(st.f / 440) / 8 + tr)) : st.ph;
    st.ph = Math.max(0, Math.min(1, st.ph + (target - st.ph) * g));
    const nv = st.note >= 0 ? Math.max(0, Math.min(1, 0.5 + (st.note - 69) / 96 + tr)) : st.ph;
    io.out.out.fill(st.ph * P.depth); io.out.note.fill(nv * P.depth); io.out.gate.fill(st.ver);
    io.out.trig.fill(st.trig > 0 ? 1 : 0); if (st.trig > 0) st.trig--; io.out.env.fill(Math.min(1, st.env * 1.5));
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
  inputs:{ in:'audio', inR:'audio', cutoff:'cv', res:'cv' }, outputs:{ out:'audio', outR:'audio' },   // stereo (owner, 19 Oct): In R optional
  params:{ cutoff:[.5,0,1,.001], res:[.3,0,1,.001], drive:[0,0,1,.01], mode:[0,0,3,1], slope:[2,2,4,2] },
  create:() => ({ ic1:0, ic2:0, jc1:0, jc2:0, R:{ ic1:0, ic2:0, jc1:0, jc2:0 } }),
  process(st, io, ctx){
    const mode = io.p.mode | 0, four = (io.p.slope | 0) >= 4, dr = 1 + io.p.drive * 6, norm = Math.tanh(dr) || 1, XR = inR(io), oR = io.out.outR;
    // one side through one pair of stages (state z); the cutoff and resonance are worked out once a sample for both
    const side = (z, xin, a1, a2, a3, k) => {
      const x = Math.tanh(xin * dr) / norm;
      const v3 = x - z.ic2, v1 = a1 * z.ic1 + a2 * v3, v2 = z.ic2 + a2 * z.ic1 + a3 * v3;
      z.ic1 = 2 * v1 - z.ic1; z.ic2 = 2 * v2 - z.ic2;
      let y = mode === 0 ? v2 : mode === 1 ? v1 : mode === 2 ? (x - k * v1 - v2) : (x - k * v1);
      if (four){
        const x2 = mode === 1 ? v1 : v2;
        const u3 = x2 - z.jc2, u1 = a1 * z.jc1 + a2 * u3, u2 = z.jc2 + a2 * z.jc1 + a3 * u3;
        z.jc1 = 2 * u1 - z.jc1; z.jc2 = 2 * u2 - z.jc2;
        y = mode === 0 ? u2 : mode === 1 ? u1 : mode === 2 ? (x2 - k * u1 - u2) : (x2 - k * u1);
      }
      return y;
    };
    for (let i = 0; i < ctx.block; i++){
      const fc = Math.min(.49, (.01 + Math.pow(10, (io.p.cutoff + io.in.cutoff[i]) * 3) * .004) * 44100 / ctx.sr);
      const g = Math.tan(Math.PI * fc), k = 2 - 2 * Math.min(clamp(io.p.res + io.in.res[i], 0, 1), .98);
      const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
      io.out.out[i] = side(st, io.in.in[i], a1, a2, a3, k);   // TPT state-variable stages
      oR[i] = side(st.R, XR[i], a1, a2, a3, k);
    }
    for (const z of [st, st.R]) if (!(Math.abs(z.ic1) < 1e4 && Math.abs(z.ic2) < 1e4 && Math.abs(z.jc1) < 1e4 && Math.abs(z.jc2) < 1e4)){ z.ic1 = z.ic2 = z.jc1 = z.jc2 = 0; }
  }
};

/* Meta-modulator (Warps-style): crossfade, ring mod, wave fold or comparator between In and Mod. */
const Meta = {
  type:'meta', scope:'mono', cost:.3,
  inputs:{ in:'audio', inR:'audio', mod:'audio' }, outputs:{ out:'audio', outR:'audio' },   // stereo (owner, 19 Oct): In R optional, one Mod
  params:{ mode:[0,0,3,1], amount:[.5,0,1,.01], mix:[1,0,1,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const mode = io.p.mode | 0, a = io.p.amount, mx = io.p.mix, XR = inR(io), oR = io.out.outR;
    const f = (x, m) => mode === 0 ? x * (1 - a) + m * a              // crossfade
      : mode === 1 ? x * ((1 - a) + a * m * 1.4142)                    // ring mod
      : mode === 2 ? Math.sin(x * (1 + a * 7) * Math.PI)               // wave fold (sine shaper)
      : x * (m >= 0 ? 1 : -1);                                         // comparator (mod flips sign)
    for (let i = 0; i < ctx.block; i++){
      const m = io.in.mod[i], x = io.in.in[i], xr = XR[i];
      io.out.out[i] = x + (f(x, m) - x) * mx; oR[i] = xr + (f(xr, m) - xr) * mx;
    }
  }
};

/* Phaser: swept all-pass stages through an LFO or the CV input. */
const Phaser = {
  type:'phaser', scope:'mono', cost:.4,
  inputs:{ in:'audio', inR:'audio', cv:'cv' }, outputs:{ out:'audio', outR:'audio' },   // stereo (owner, 19 Oct): In R optional, one sweep
  params:{ rate:[.3,0,1,.01], depth:[.5,0,1,.01], feedback:[.4,0,1,.01], stages:[4,2,8,2], mix:[.5,0,1,.01], sync:[0,0,1,1] },
  create:() => ({ ph:0, z:new Float32Array(8), fb:0, zR:new Float32Array(8), fbR:0 }),
  process(st, io, ctx){
    const hz = io.p.sync >= .5 ? (ctx.transport.tempo / 60) * io.p.rate : .05 * Math.pow(160, io.p.rate);
    const n = clamp(io.p.stages | 0, 2, 8), dep = io.p.depth, fbk = io.p.feedback * .9, mx = io.p.mix, cv = linked(io, 'cv'), XR = inR(io), oR = io.out.outR;
    for (let i = 0; i < ctx.block; i++){
      st.ph += hz / ctx.sr; st.ph -= Math.floor(st.ph);
      const f = clamp(.5 + (sinT(st.ph) * dep + (cv ? io.in.cv[i] : 0)) * .5, 0, .999);
      const a = (1 - f) / (1 + f);
      let x = io.in.in[i] + st.fb * fbk;
      for (let s = 0; s < n; s++){ const y = a * x + st.z[s]; st.z[s] = x - a * y; x = y; }
      st.fb = x; io.out.out[i] = io.in.in[i] * (1 - mx) + x * mx;
      const xr0 = XR[i]; let xr = xr0 + st.fbR * fbk;
      for (let s = 0; s < n; s++){ const y = a * xr + st.zR[s]; st.zR[s] = xr - a * y; xr = y; }
      st.fbR = xr; oR[i] = xr0 * (1 - mx) + xr * mx;
    }
    if (!(Math.abs(st.fb) < 1e4 && Math.abs(st.fbR) < 1e4)){ st.fb = st.fbR = 0; st.z.fill(0); st.zR.fill(0); }
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

/* ---------- copied from index.html (keep identical): Pitch delay / Pitch reverb, and the tempo-locked Flex, Step gate and
   Retrigger of the track effects, so …chains, …waves and the pads get them as modules too ---------- */
/* Pitch delay and Pitch reverb (original code), in the spirit of the pitch-shifting delay and reverb pedals.
   A two-head granular pitch shifter (each head a crossfaded window reading a short buffer), inside or after a delay
   or a reverb. Delay modes: Climb (every repeat shifts again, so repeats climb or fall), Shifted (every repeat at the
   one interval), Arp (repeats step through a chord), Tape (a wobbling tape echo whose pitch bends), Reverse (reversed
   repeats). Reverb modes: Shimmer (the tail climbs), Pitched (the reverb of the shifted sound), Dual (two intervals
   in the tail). Stereo, processed in place like DegradeCore. */
class PitchFxCore {
  constructor(sr){
    this.sr = sr; const D = Math.ceil(sr * 2.2);
    this.dl = [new Float32Array(D), new Float32Array(D)]; this.dw = 0; this.D = D;
    this.sh = [0, 1, 2, 3].map(() => ({b:new Float32Array(8192), w:0, ph:0}));
    const base = [1116, 1188, 1277, 1356], ap = [556, 441];
    this.cb = [0, 1].map(c => base.map(n => ({b:new Float32Array(Math.ceil(n * 2.2 * sr / 44100) + 8), i:0, f:0}))); this.cbn = base; this.spread = 23;
    this.ap = [0, 1].map(() => ap.map(n => ({b:new Float32Array(Math.ceil(n * sr / 44100) + 8), i:0})));
    this.lp = [0, 0]; this.fbS = [0, 0]; this.step = 0; this.cnt = 0; this.wph = 0; this.tsm = 0; this.rev = 0;
  }
  static get ARP(){ return [0, 4, 7, 12, 7, 4]; }
  static defaults(){ return {kind:0, mode:0, time:380, fb:0.45, pitch:12, pitch2:-12, tone:0.6, size:0.6, wow:0.2, mix:0.4}; }
  shift(s, x, ratio, win){   // one sample through pitch shifter s
    const b = s.b, N = 8192; b[s.w] = x;
    const W = Math.max(64, Math.min(N - 4, win)); s.ph += (1 - ratio) / W; if (s.ph >= 1) s.ph -= 1; if (s.ph < 0) s.ph += 1;
    let y = 0;
    for (let h = 0; h < 2; h++){
      const ph = (s.ph + h * 0.5) % 1, d = ph * W, rp = s.w - d - 1, i = Math.floor(rp), f = rp - i;
      const a = b[(i + N) & (N - 1)], c = b[(i + 1 + N) & (N - 1)]; y += (a + (c - a) * f) * Math.sin(Math.PI * ph);
    }
    s.w = (s.w + 1) & (N - 1); return y * 0.75;   // the two windows overlap to ~1.4 at their peak
  }
  verb(ch, x, fb, damp, size){
    let y = 0; const C = this.cb[ch];
    for (let k = 0; k < 4; k++){ const c = C[k], L = Math.max(16, Math.min(c.b.length, Math.floor((this.cbn[k] + ch * this.spread) * (0.45 + size * 1.7) * this.sr / 44100)));
      if (c.i >= L) c.i = 0; const o = c.b[c.i]; c.f = o * (1 - damp) + c.f * damp; c.b[c.i] = x + c.f * fb; c.i++; y += o; }
    for (const a of this.ap[ch]){ const L = a.b.length - 8; if (a.i >= L) a.i = 0; const o = a.b[a.i]; a.b[a.i] = y + o * 0.5; a.i++; y = o - y * 0.5; }
    return y * 0.25;
  }
  process(Lb, Rb, n, p){
    const sr = this.sr, mix = p.mix, dry = 1 - mix * 0.5, r1 = Math.pow(2, p.pitch / 12), r2 = Math.pow(2, p.pitch2 / 12), win = sr * 0.06;
    const tc = 1 - Math.exp(-2 * Math.PI * (500 + p.tone * p.tone * 12000) / sr), mode = p.mode | 0, D = this.D;
    if (p.kind | 0){   // reverb
      // Size tops out at a ~7 s tail (it was 0.97, ~14 s), and the shimmer loop is scaled to stay under unity (measured: it rose at 0.45 of the old value, decays at 0.4): with large
      // Size and Shimmer turned well up it used to sit in a permanent self-oscillation that the tanh only bounded.
      const fb = 0.7 + p.size * 0.24, damp = 0.15 + (1 - p.tone) * 0.6, sfb = Math.min(0.9, p.fb) * 0.38;
      for (let i = 0; i < n; i++){
        for (let ch = 0; ch < 2; ch++){
          const x0 = ch ? Rb[i] : Lb[i]; let x = x0;
          if (mode === 1) x = this.shift(this.sh[ch], x, r1, win);
          const w = this.verb(ch, x + this.fbS[ch], fb, damp, p.size) * (1 - fb) * 1.2;   // about unity gain
          let f = 0;
          if (mode === 0) f = this.shift(this.sh[ch], w, r1, win) * sfb;
          else if (mode === 2) f = (this.shift(this.sh[ch], w, r1, win) + this.shift(this.sh[ch + 2], w, r2, win)) * 0.5 * sfb;
          this.lp[ch] += (f - this.lp[ch]) * tc;
          // A slow limiter on the loop: if the shimmer builds past 0.35 it is turned down, so it always dies away.
          const a = Math.abs(this.lp[ch]); this.fenv = this.fenv || [0, 0]; this.fenv[ch] += (a - this.fenv[ch]) * (a > this.fenv[ch] ? 0.002 : 0.00005);
          this.fbS[ch] = Math.tanh(this.lp[ch] * (this.fenv[ch] > 0.35 ? 0.35 / this.fenv[ch] : 1));
          if (ch) Rb[i] = x0 * dry + w * mix * 1.8; else Lb[i] = x0 * dry + w * mix * 1.8;
        }
      }
      return;
    }
    // delay
    const tBase = Math.max(10, Math.min(2000, p.time)) / 1000 * sr, fb = Math.min(0.95, p.fb);
    for (let i = 0; i < n; i++){
      // time: Tape mode wobbles it (and glides when Time is changed); the rest follow Time smoothly
      this.wph += (0.3 + p.wow * 2.2) / sr; if (this.wph >= 1) this.wph -= 1;
      const tgt = mode === 3 ? tBase * (1 + p.wow * 0.012 * Math.sin(2 * Math.PI * this.wph) + p.wow * 0.006 * Math.sin(2 * Math.PI * this.wph * 2.7)) : tBase;
      this.tsm += (tgt - this.tsm) * (mode === 3 ? 0.00006 + p.wow * 0.0002 : 0.002); if (this.tsm < 1) this.tsm = tgt;
      // Arp: the interval steps once per repeat
      this.cnt++; if (this.cnt >= tBase){ this.cnt = 0; this.step = (this.step + 1) % PitchFxCore.ARP.length; }
      const ra = mode === 2 ? Math.pow(2, (PitchFxCore.ARP[this.step] + p.pitch - 12) / 12) : r1;
      for (let ch = 0; ch < 2; ch++){
        const buf = this.dl[ch], x = ch ? Rb[i] : Lb[i];
        let rp = this.dw - (mode === 4 ? (this.rev * 2 % tBase) : this.tsm) - (ch ? sr * 0.0007 : 0);
        while (rp < 0) rp += D; const j = Math.floor(rp), f = rp - j, a = buf[j % D], b = buf[(j + 1) % D];
        let y = a + (b - a) * f;
        if (mode === 4 && p.pitch !== 0) y = this.shift(this.sh[ch], y, r1, win);
        let loop = y;
        if (mode === 0 || mode === 2) loop = this.shift(this.sh[ch], y, ra, win);
        this.lp[ch] += (loop - this.lp[ch]) * tc;
        buf[this.dw] = x + this.lp[ch] * fb;
        const wet = mode === 1 ? this.shift(this.sh[ch], y, r1, win) : mode === 4 ? y * (0.5 - 0.5 * Math.cos(2 * Math.PI * ((this.rev % tBase) / tBase))) : y;
        if (ch) Rb[i] = x * dry + wet * mix * 1.4; else Lb[i] = x * dry + wet * mix * 1.4;
      }
      this.dw = (this.dw + 1) % D; this.rev++;
    }
  }
}
/* Three tempo-locked effects (original code) for track and master FX. They follow the song through the player's beat
   clock (`tr.bp`: the song position in beats, wrapping every 16; below 0 or still = stopped), and the tracker commands
   FLX / GAT / RTG reach them as `tr.flx`, `tr.gat`, `tr.rtg` (−1 = use the effect's own setting). Self-contained: the
   track-FX worklet Blob is built from their toString(), like DegradeCore. */
// The clock's slope gives the samples per beat. The clock is a float32 AudioParam (it moves in ~1e-6-beat steps), so one
// block's slope is off by up to ~0.3%, enough to shove a replayed beat ~100 samples every block (Flex clicked and buzzed).
// So the slope is measured over a run of blocks (used from 4096 samples, restarted every ~32768); a real tempo change
// (> 2%, the same in two blocks running, so a half-stalled block can't fake one) snaps, and a stopped, stalled or
// jumping clock keeps the last good value.
function clkSpb(o, bp, n){
  if (!bp || bp.length < 2 || n < 2) return o.spb;
  const a = bp[0], b = bp[n - 1];
  if (a < 0 || b < 0){ o.cn = 0; return o.spb; }
  if (!(o.cn > 0)){ o.cu = 0; o.cl = a; }
  if (a < o.cl - 8) o.cu += 16;                                          // the clock wrapped (every 16 beats) since the last block…
  const A = a + o.cu; if (b < a - 8) o.cu += 16;                         // …or inside this one
  const B = b + o.cu, s = (B - A) / (n - 1); o.cl = b;
  if (!(s > 1e-7 && s < 0.01)){ o.cn = 0; o.ps = 0; return o.spb; }
  if (!o.sm){ o.spb = 1 / s; o.sm = true; o.cn = 0; }
  else if (Math.abs(s * o.spb - 1) > 0.02){
    if (o.ps && Math.abs(s / o.ps - 1) < 0.005){ o.spb = 1 / s; o.cn = 0; } else { o.ps = s; o.cn = 0; return o.spb; }
  }
  o.ps = 0;
  if (!(o.cn > 0) || o.cn > 32768){ o.cn = 0; o.ca = A; }
  const len = o.cn + n - 1; if (len >= 4096) o.spb = len / (B - o.ca);
  o.cn += n; return o.spb;
}
/* Flex: re-sequences the last bar or two in time with the song (a time-remap per pattern: repeats, reverses, half
   speed, tape stops, scratches, gates, shuffles). Each pattern maps the bar position p (beats 0–4) to the position r it
   plays from (r ≤ p, so it only reads the past). Jumps crossfade from the old read head to the new one. Always fed,
   even while switched off, so switching it on mid-bar already has the bar to play from. It keeps its own song position
   (`ph`, float64, a sample at a time, locked to the clock each block) and a glided samples-per-beat (`sp`), so the read
   head moves smoothly: reading the float32 clock directly made the moving patterns gritty. */
class FlexCore {
  constructor(sr){ this.sr = sr; this.N = Math.ceil(sr * 13); this.bL = null; this.bR = null; this.w = 0; this.D = 0; this.xo = 0; this.xc = 0; this.xn = 1; this.g = 1; this.pg = 1; this.spb = sr / 2; this.sp = sr / 2; this.ph = -1; }
  static get NAMES(){ return ['OFF', 'REPEAT 1/4', 'REPEAT 1/8', 'FILL 1/16', 'STUTTER BUILD', 'HALF SPEED', 'TAPE STOP', 'REVERSE BEATS', 'REVERSE 1/8', 'BACKSPIN', 'SCRATCH',
    'GATE 1/16', 'TRANCE CHOP', 'SHUFFLE', 'BEAT JUGGLE', 'OCTAVE UP', 'RANDOM SLICES']; }
  pat(k, p, bar){
    let r = p, g = 1, u;
    switch (k){
      case 1: r = p % 1; break;
      case 2: r = Math.floor(p) + p % 0.5; break;
      case 3: if (p >= 3) r = 3 + (p - 3) % 0.25; break;
      case 4: if (p >= 3.5) r = 3.5 + (p - 3.5) % 0.125; else if (p >= 3) r = 3 + (p - 3) % 0.25; else if (p >= 2) r = 2 + (p - 2) % 0.5; break;
      case 5: r = p / 2; break;
      case 6: r = p - p * p / 8; if (p > 3.5) g = (4 - p) * 2; break;
      case 7: r = Math.floor(p) - p % 1; break;
      case 8: r = Math.floor(p * 2) / 2 - p % 0.5; break;
      case 9: if (p >= 3){ u = p - 3; r = 3 - 2.5 * u * u; g = 1 - u * u; } break;
      case 10: { const b = Math.floor(p * 2) / 2; u = (p - b) * 2; r = b + 0.125 * (1 - Math.cos(2 * Math.PI * u)); } break;
      case 11: g = p % 0.25 < 0.125 ? 1 : 0; break;
      case 12: g = (0xB6B6 >> (15 - Math.floor(p * 4))) & 1; break;
      case 13: { const s = Math.floor(p * 4); r = [0, 1, 2, 3, 4, 5, 2, 7, 8, 9, 10, 11, 8, 13, 6, 15][s] * 0.25 + p % 0.25; } break;
      case 14: if (p >= 2) r = p - 2; break;
      case 15: r = Math.floor(p) - 1 + 2 * (p % 1); break;
      case 16: { const s = Math.floor(p * 4); let h = Math.imul((bar & 3) * 31 + s * 17 + 7, 2654435761) >>> 0; h ^= h >>> 13; h = h % 11; r = (s - (h < 4 ? 0 : h - 3)) * 0.25 + p % 0.25; } break;
    }
    this.pg = g; return r;
  }
  // 4-point Hermite read (half speed, tape stops and scratches stay clean on cymbals); linear within 2 samples of now,
  // where the Hermite's far point would still be unwritten.
  rd(b, w, D){
    const N = this.N; let x = w - D; if (x < 0) x += N; const i = Math.floor(x), f = x - i, j = i + 1 >= N ? 0 : i + 1;
    if (D < 2) return b[i] + (b[j] - b[i]) * f;
    const y0 = b[i ? i - 1 : N - 1], y1 = b[i], y2 = b[j], y3 = b[j + 1 >= N ? 0 : j + 1];
    return ((((y3 - y0) * 0.5 + (y1 - y2) * 1.5) * f + (y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3)) * f + (y2 - y0) * 0.5) * f + y1;
  }
  feed(L, R, n, tr){ this.run(L, R, n, null, tr); }
  process(L, R, n, dp, tr){ this.run(L, R, n, dp, tr); }
  run(L, R, n, dp, tr){
    if (!this.bL){ this.bL = new Float32Array(this.N); this.bR = new Float32Array(this.N); }
    const N = this.N, bL = this.bL, bR = this.bR, bp = tr && tr.bp, spb = clkSpb(this, bp, n), live = !!(bp && bp.length > 1);
    const k = !dp ? 0 : tr && tr.flx >= 0 ? Math.min(16, Math.round(tr.flx)) : dp.pat | 0, mix = dp ? dp.mix : 0;
    const XF = Math.max(16, Math.round(this.sr * (dp ? dp.xf : 5) / 1000)), gk = 1 - Math.exp(-1 / (this.sr * 0.002));
    // Lock our position to the clock: gently while close (the clock's own float32 steps stay out), at once when it jumps.
    const q0 = live ? bp[0] : -1;
    if (q0 < 0) this.ph = -1;
    else if (this.ph < 0) this.ph = q0;
    else { let e = q0 - this.ph; e -= 16 * Math.round(e / 16); if (Math.abs(e) > 0.004) this.ph = q0; else this.ph += e * 0.1; }
    if (Math.abs(spb / this.sp - 1) > 0.02) this.sp = spb;               // a real tempo change: follow at once (that jump crossfades)
    const sk = 1 - Math.exp(-1 / (this.sr * 0.1));
    for (let i = 0; i < n; i++){
      const w = this.w; bL[w] = L[i]; bR[w] = R[i];
      this.sp += (spb - this.sp) * sk;
      const q = this.ph; if (q >= 0){ this.ph += 1 / this.sp; if (this.ph >= 16) this.ph -= 16; }
      if (!dp){ this.D = 0; this.w = w + 1 >= N ? 0 : w + 1; continue; }
      let D = 0, g = 1;
      if (k > 0 && q >= 0){ const p = q % 4, r = this.pat(k, p, Math.floor(q / 4)); g = this.pg; D = Math.max(0, Math.min(N - 4, (p - r) * this.sp)); }
      if (Math.abs(D - this.D) > 8){ this.xo = this.D; this.xc = XF; this.xn = XF; }
      this.D = D; this.g += (g - this.g) * gk;
      let yl = this.rd(bL, w, D), yr = this.rd(bR, w, D);
      if (this.xc > 0){ const f = this.xc / this.xn; yl += (this.rd(bL, w, this.xo) - yl) * f; yr += (this.rd(bR, w, this.xo) - yr) * f; this.xc--; }
      L[i] += (yl * this.g - L[i]) * mix; R[i] += (yr * this.g - R[i]) * mix;
      this.w = w + 1 >= N ? 0 : w + 1;
    }
  }
}
/* Step gate: a 16-step gate in time with the song. Pattern 0 is the custom one (two hex bytes, steps 1–8 and 9–16,
   step 1 = the top bit); 1–15 are built in. */
class StepGateCore {
  constructor(sr){ this.sr = sr; this.g = 1; }
  static get PATS(){ return ['', 'xxxxxxxxxxxxxxxx', 'x.x.x.x.x.x.x.x.', '.x.x.x.x.x.x.x.x', 'x..x..x.x..x..x.', 'xx.xx.x.xx.xx.x.', 'x.xxx.x.x.xxx.xx', 'xxx.xxx.xxx.x.x.',
    'x...x...x...x...', '..x...x...x...x.', 'x.x..x.x.x..x.x.', 'xxxx....xxxx....', 'xxxxxxxx........', 'x.xx.x.xx.xx.x.x', 'xx..xx..xx..x.x.', 'x..x.x..x..x.xx.']; }
  static get NAMES(){ return ['CUSTOM', 'EVERY STEP', '8THS', 'OFFBEATS', '3-3-2', 'GALLOP', 'BROKEN', 'TRIPLE', 'QUARTERS', 'OFF 8THS', 'SYNCOPATE', 'HALF ON', 'FIRST HALF', 'DENSE', 'PAIRS', 'TRESILLO']; }
  static mask(s){ let m = 0; for (let j = 0; j < 16; j++) if (s[j] === 'x') m |= 1 << (15 - j); return m; }
  process(L, R, n, dp, tr){
    const bp = tr && tr.bp, live = !!(bp && bp.length > 1), pi = tr && tr.gat >= 0 ? Math.min(15, Math.round(tr.gat)) : dp.pat | 0;
    const mask = pi === 0 ? ((dp.m1 & 255) << 8) | (dp.m2 & 255) : StepGateCore.mask(StepGateCore.PATS[pi]);
    const sb = [0.5, 0.25, 0.125, 1 / 6, 1 / 3][dp.div | 0] || 0.25, len = dp.len, lo = 1 - dp.depth, tc = 1 - Math.exp(-1 / (this.sr * Math.max(0.0003, dp.smooth / 1000)));
    for (let i = 0; i < n; i++){
      const q = bp ? (live ? bp[i] : bp[0]) : -1; let tg = 1;
      if (live && q >= 0){ const sp = q / sb, st = Math.floor(sp + 1e-6), fr = sp - st, idx = ((st % 16) + 16) % 16; tg = ((mask >> (15 - idx)) & 1) && fr < len ? 1 : lo; }
      this.g += (tg - this.g) * tc; L[i] *= this.g; R[i] *= this.g;
    }
  }
}
/* Retrigger: catches a slice (1/4 … 1/32T at the song tempo) from the moment it is engaged and repeats it, each repeat
   quieter (Decay) and/or pitched (Pitch, semitones a repeat), cut to Gate of the slice. Engaged by RTG, or always while
   the slot is on in mode ALWAYS (so FXO on/off makes it momentary). The captured slice is frozen while it repeats. */
class RetrigCore {
  constructor(sr){ this.sr = sr; this.N = Math.ceil(sr * 3); this.bL = new Float32Array(this.N); this.bR = new Float32Array(this.N); this.w = 0; this.on = false; this.div = -1; this.st = 0; this.el = 0; this.Ls = 1; this.g = 0; this.spb = sr / 2; }
  static get DIVS(){ return [1, 0.5, 0.25, 0.125, 0.0625, 1 / 3, 1 / 6, 1 / 12]; }
  static get NAMES(){ return ['1/4', '1/8', '1/16', '1/32', '1/64', '1/8T', '1/16T', '1/32T']; }
  process(L, R, n, dp, tr){
    const N = this.N, bL = this.bL, bR = this.bR, spb = clkSpb(this, tr && tr.bp, n), o = tr ? tr.rtg : -1;
    const eng = o > 0.5 ? true : o > -0.5 ? false : (dp.mode | 0) === 1, di = o > 0.5 ? Math.min(7, Math.round(o) - 1) : Math.min(7, dp.div | 0);
    if (eng && (!this.on || di !== this.div)){ this.on = true; this.div = di; this.st = this.w; this.el = 0; this.Ls = Math.max(64, Math.min(N - 64, Math.round(RetrigCore.DIVS[di] * spb))); }
    if (!eng) this.on = false;
    const tgt = this.on ? 1 : 0, gk = 1 - Math.exp(-1 / (this.sr * 0.003)), fd = Math.max(8, Math.round(this.sr * 0.0015)), rs = Math.pow(2, dp.pitch / 12), dec = 1 - dp.decay;
    const Ls = this.Ls, gl = Math.max(0.05, dp.gate) * Ls, mix = dp.mix;
    for (let i = 0; i < n; i++){
      const frozen = this.g > 1e-4 && this.el >= Ls;
      if (!frozen){ bL[this.w] = L[i]; bR[this.w] = R[i]; this.w = this.w + 1 >= N ? 0 : this.w + 1; }
      this.g += (tgt - this.g) * gk;
      if (this.g < 1e-4 && !this.on){ this.g = 0; continue; }
      const rep = Math.floor(this.el / Ls); let yl = L[i], yr = R[i];
      if (rep > 0){
        const pos = this.el - rep * Ls, rate = Math.max(0.25, Math.min(4, Math.pow(rs, rep))), rp = (pos * rate) % Ls;
        let x = this.st + rp; if (x >= N) x -= N; const a = Math.floor(x), f = x - a, b = a + 1 >= N ? 0 : a + 1;
        const env = Math.pow(dec, rep) * Math.max(0, Math.min(1, pos / fd, (gl - pos) / fd));
        yl = (bL[a] + (bL[b] - bL[a]) * f) * env; yr = (bR[a] + (bR[b] - bR[a]) * f) * env;
      }
      this.el++;
      const m = this.g * mix; L[i] += (yl - L[i]) * m; R[i] += (yr - R[i]) * m;
    }
  }
}


/* The track effects' Flex, Step gate and Retrigger as modules. On a tracker track they hear the song position from the
   player's beat clock; a module keeps its own instead: it counts beats at the song tempo from the moment the song starts
   (restarted), wrapping every 16 like the player's clock, and while the song is stopped it reports "stopped" (Flex and
   Step gate then pass the sound; Retrigger still works, from its own tempo). On a pad the "song" starts with the hit. */
const fxBeats = (st, ctx) => {
  if (!st.bp){ st.bp = new Float32Array(ctx.block); st.stop = new Float32Array(1).fill(-1); st.beat = 0; }
  const t = ctx.transport || {};
  if (restarted(st, ctx)) st.beat = 0;
  if (t.playing === false) return st.stop;
  const inc = (t.tempo || 120) / 60 / ctx.sr, bp = st.bp;
  for (let i = 0; i < ctx.block; i++){ bp[i] = st.beat; st.beat += inc; if (st.beat >= 16) st.beat -= 16; }
  return bp;
};
const fxPass = io => { io.out.out.set(io.in.in); io.out.outR.set(inR(io)); };
/* Flex: re-sequences the last bar in time with the song (repeats, reverses, half speed, tape stops, scratches, gates,
   shuffles). Pattern picks the move; the Pattern input (0–1 across the 17 patterns) can switch it from a sequencer. */
const FlexM = {
  type:'flex', scope:'mono', cost:.15,
  inputs:{ in:'audio', inR:'audio', pat:'cv' }, outputs:{ out:'audio', outR:'audio' },
  params:{ pat:[1,0,16,1], mix:[1,0,1,.01], xf:[5,.5,20,.1] },
  create(sr){ return { c:new FlexCore(sr), tr:{ bp:null, flx:-1, gat:-1, rtg:-1 }, dp:{ pat:1, mix:1, xf:5 } }; },
  process(st, io, ctx){
    fxPass(io); st.tr.bp = fxBeats(st, ctx);
    st.tr.flx = linked(io, 'pat') ? clamp(Math.round(io.in.pat[0] * 16), 0, 16) : -1;
    st.dp.pat = clamp(io.p.pat | 0, 0, 16); st.dp.mix = u(io.p.mix); st.dp.xf = clamp(io.p.xf, .5, 20);
    st.c.process(io.out.out, io.out.outR, ctx.block, st.dp, st.tr);
  }
};
/* Step gate: a 16-step gate in time with the song. Pattern 0 is your own: Steps 1–8 and 9–16 are the two bytes (step 1
   the top bit). The Pattern input (0–1 across the 16 patterns) can switch it. */
const StepGateM = {
  type:'sgate', scope:'mono', cost:.05,
  inputs:{ in:'audio', inR:'audio', pat:'cv' }, outputs:{ out:'audio', outR:'audio' },
  params:{ pat:[2,0,15,1], m1:[170,0,255,1], m2:[170,0,255,1], div:[1,0,4,1], len:[.75,.03,1,.01], depth:[1,0,1,.01], smooth:[2,.3,20,.1] },
  create(sr){ return { c:new StepGateCore(sr), tr:{ bp:null, flx:-1, gat:-1, rtg:-1 }, dp:{} }; },
  process(st, io, ctx){
    fxPass(io); st.tr.bp = fxBeats(st, ctx);
    st.tr.gat = linked(io, 'pat') ? clamp(Math.round(io.in.pat[0] * 15), 0, 15) : -1;
    const p = io.p, dp = st.dp; dp.pat = clamp(p.pat | 0, 0, 15); dp.m1 = clamp(p.m1 | 0, 0, 255); dp.m2 = clamp(p.m2 | 0, 0, 255);
    dp.div = clamp(p.div | 0, 0, 4); dp.len = clamp(p.len, .03, 1); dp.depth = u(p.depth); dp.smooth = clamp(p.smooth, .3, 20);
    st.c.process(io.out.out, io.out.outR, ctx.block, dp, st.tr);
  }
};
/* Retrigger: catches a slice (1/4 … 1/32T at the song tempo) and repeats it, each repeat quieter (Decay) and/or pitched
   (Pitch, semitones a repeat), cut to Gate of the slice. Mode Gate repeats while the Trig input is high (or a tracker
   RTG on a track); Always repeats all the time, so a hit stutters. */
const RetrigM = {
  type:'retrig', scope:'mono', cost:.08,
  inputs:{ in:'audio', inR:'audio', trig:'gate' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[1,0,1,1], div:[2,0,7,1], decay:[.15,0,1,.01], pitch:[0,-24,24,1], gate:[1,.05,1,.01], mix:[1,0,1,.01] },
  create(sr){ return { c:new RetrigCore(sr), tr:{ bp:null, flx:-1, gat:-1, rtg:-1 }, dp:{} }; },
  process(st, io, ctx){
    fxPass(io); st.tr.bp = fxBeats(st, ctx);
    const p = io.p, dp = st.dp; dp.mode = clamp(p.mode | 0, 0, 1); dp.div = clamp(p.div | 0, 0, 7); dp.decay = u(p.decay);
    dp.pitch = clamp(p.pitch, -24, 24); dp.gate = clamp(p.gate, .05, 1); dp.mix = u(p.mix);
    st.tr.rtg = linked(io, 'trig') ? (io.in.trig[0] > .5 ? dp.div + 1 : 0) : -1;
    st.c.spb = ctx.sr * 60 / ((ctx.transport && ctx.transport.tempo) || 120);   // the tempo; while the song plays, the beat clock refines it
    st.c.process(io.out.out, io.out.outR, ctx.block, dp, st.tr);
  }
};
/* Pitch delay and Pitch reverb (the pads' and track effects' PitchFxCore) as modules. */
const PDelayM = {
  type:'pdelay', scope:'mono', cost:.3,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[0,0,4,1], time:[380,20,2000,1], fb:[.45,0,.95,.01], pitch:[12,-24,24,1], tone:[.6,0,1,.01], wow:[.2,0,1,.01], mix:[.4,0,1,.01] },
  create(sr){ return { c:new PitchFxCore(sr), q:PitchFxCore.defaults() }; },
  process(st, io, ctx){
    fxPass(io); const p = io.p, q = st.q;
    q.kind = 0; q.mode = clamp(p.mode | 0, 0, 4); q.time = clamp(p.time, 20, 2000); q.fb = clamp(p.fb, 0, .95); q.pitch = clamp(Math.round(p.pitch), -24, 24);
    q.tone = u(p.tone); q.wow = u(p.wow); q.mix = u(p.mix);
    st.c.process(io.out.out, io.out.outR, ctx.block, q);
  }
};
const PReverbM = {
  type:'preverb', scope:'mono', cost:.35,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[0,0,2,1], size:[.6,0,1,.01], fb:[.45,0,.9,.01], pitch:[12,-24,24,1], pitch2:[-12,-24,24,1], tone:[.6,0,1,.01], mix:[.35,0,1,.01] },
  create(sr){ return { c:new PitchFxCore(sr), q:PitchFxCore.defaults() }; },
  process(st, io, ctx){
    fxPass(io); const p = io.p, q = st.q;
    q.kind = 1; q.mode = clamp(p.mode | 0, 0, 2); q.size = u(p.size); q.fb = clamp(p.fb, 0, .9); q.pitch = clamp(Math.round(p.pitch), -24, 24);
    q.pitch2 = clamp(Math.round(p.pitch2), -24, 24); q.tone = u(p.tone); q.mix = u(p.mix);
    st.c.process(io.out.out, io.out.outR, ctx.block, q);
  }
};


/* ── Spectral, Extreme stretch and Convolution reverb (Oct 2026, original code) ── */
/* ── Spectral effects: Spectral, Extreme stretch, Convolution reverb (added Oct 2026, original code) ──────────────
   FFTR: the real FFT these three share. Sizes are powers of two (256 … 32768 are used); each size's tables are made
   once and shared (FFTR.get(n)). It packs the n real samples into an n/2-point complex FFT (radix 2, decimation in
   time) and splits the result: the textbook real-FFT trick (e.g. E. O. Brigham, "The Fast Fourier Transform", 1974).
   Nothing is allocated per call. A spectrum is n/2 + 1 bins in separate re and im arrays (the im of bin 0 and of bin
   n/2 is 0), at an offset if you like. Forward is unnormalised; inverse divides by n, so inverse(forward(x)) is x.
     FFTR.get(n)                                  the shared transform for size n
     f.forward(x, re, im, o = 0, oi = o)          x[0..n) → re[o..o+n/2], im[oi..oi+n/2]
     f.inverse(re, im, x, o = 0, oi = o, g = 1)   back again, times g
   A big transform can be spread over several audio blocks. FFTR.job(maxN), made in create(), holds the work of one
   transform in flight (any size up to maxN):
     f.fwdStart(J, x) · f.pass(J) until it returns 0 (f.passes of them) · f.fwdEnd(J, re, im, o, oi)
     f.invStart(J, re, im, o, oi) · f.pass(J) … · f.invEnd(J, x, g)
   forward() and inverse() use the size's own job, so they finish before returning and never meet a job in flight. */
const FFTR_CACHE = {};
class FFTR {
  static get(n){ return FFTR_CACHE[n] || (FFTR_CACHE[n] = new FFTR(n)); }
  static job(maxN){ const m = Math.max(2, maxN >> 1); return { zr:new Float64Array(m), zi:new Float64Array(m), p:0, inv:false }; }
  constructor(n){
    const m = n >> 1; let bits = 0; while ((1 << bits) < m) bits++;
    this.n = n; this.m = m; this.passes = bits;
    this.rev = new Int32Array(m);
    for (let i = 0; i < m; i++){ let r = 0; for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b); this.rev[i] = r; }
    // the complex FFT's twiddles, e^(−2πij/size) for each pass, laid end to end (pass with half-size h at offset h),
    // so every pass reads them in order
    this.c = new Float64Array(Math.max(2, m)); this.s = new Float64Array(Math.max(2, m));
    for (let h = 1; h < m; h <<= 1) for (let j = 0; j < h; j++){ this.c[h + j] = Math.cos(Math.PI * j / h); this.s[h + j] = Math.sin(Math.PI * j / h); }
    this.sc = new Float64Array(m + 1); this.ss = new Float64Array(m + 1);   // the split's, e^(−iπk/m)
    for (let k = 0; k <= m; k++){ this.sc[k] = Math.cos(Math.PI * k / m); this.ss[k] = Math.sin(Math.PI * k / m); }
    this.J = FFTR.job(n);
  }
  fwdStart(J, x){
    const m = this.m, rev = this.rev, zr = J.zr, zi = J.zi;
    for (let i = 0, j = 0; i < m; i++, j += 2){ const r = rev[i]; zr[r] = x[j]; zi[r] = x[j + 1]; }
    J.p = 0; J.inv = false;
  }
  /* One radix-2 pass of the complex FFT; returns how many are left. */
  pass(J){
    const m = this.m, zr = J.zr, zi = J.zi, p = J.p;
    if (p >= this.passes) return 0;
    const half = 1 << p, size = half << 1, sg = J.inv ? 1 : -1;
    if (half === 1){
      for (let a = 0; a < m; a += 2){ const b = a + 1, ar = zr[a], ai = zi[a], br = zr[b], bi = zi[b]; zr[a] = ar + br; zi[a] = ai + bi; zr[b] = ar - br; zi[b] = ai - bi; }
    } else if (half === 2){
      for (let i = 0; i < m; i += 4){
        let a = i, b = i + 2, tr = zr[b], ti = zi[b];
        zr[b] = zr[a] - tr; zi[b] = zi[a] - ti; zr[a] += tr; zi[a] += ti;
        a = i + 1; b = i + 3; tr = -sg * zi[b]; ti = sg * zr[b];   // twiddle ∓i
        zr[b] = zr[a] - tr; zi[b] = zi[a] - ti; zr[a] += tr; zi[a] += ti;
      }
    } else {
      const C = this.c, S = this.s;
      for (let i = 0; i < m; i += size){
        for (let j = 0, a = i, b = i + half; j < half; j++, a++, b++){
          const wr = C[half + j], wi = sg * S[half + j], xr = zr[b], xi = zi[b], tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
          zr[b] = zr[a] - tr; zi[b] = zi[a] - ti; zr[a] += tr; zi[a] += ti;
        }
      }
    }
    J.p = p + 1; return this.passes - J.p;
  }
  fwdEnd(J, re, im, o = 0, oi = o){
    const m = this.m, zr = J.zr, zi = J.zi, SC = this.sc, SS = this.ss;
    re[o] = zr[0] + zi[0]; im[oi] = 0; re[o + m] = zr[0] - zi[0]; im[oi + m] = 0;
    for (let k = 1, q = m - 1; k <= q; k++, q--){
      // bins k and m − k together, from Z[k] and Z[m − k]
      const a = zr[k], b = zi[k], c = zr[q], d = zi[q];
      const er = (a + c) * .5, ei = (b - d) * .5, fr = (b + d) * .5, fi = (c - a) * .5;
      const wr = SC[k], wi = -SS[k];
      re[o + k] = er + wr * fr - wi * fi; im[oi + k] = ei + wr * fi + wi * fr;
      if (q !== k){
        // for m − k: Fe and Fo are the conjugates, and the twiddle is −conj(W^k)
        const vr = -wr, vi = wi;
        re[o + q] = er + vr * fr + vi * fi; im[oi + q] = -ei - vr * fi + vi * fr;
      }
    }
  }
  invStart(J, re, im, o = 0, oi = o){
    const m = this.m, rev = this.rev, zr = J.zr, zi = J.zi, SC = this.sc, SS = this.ss;
    for (let k = 0; k < m; k++){
      const xr = re[o + k], xi = im[oi + k], yr = re[o + m - k], yi = im[oi + m - k];
      const er = (xr + yr) * .5, ei = (xi - yi) * .5, dr = (xr - yr) * .5, di = (xi + yi) * .5;
      const c = SC[k], s = SS[k], fr = c * dr - s * di, fi = c * di + s * dr;
      const r = rev[k]; zr[r] = er - fi; zi[r] = ei + fr;
    }
    J.p = 0; J.inv = true;
  }
  invEnd(J, x, g = 1){
    const m = this.m, zr = J.zr, zi = J.zi, s = g / m;
    for (let i = 0, j = 0; i < m; i++, j += 2){ x[j] = zr[i] * s; x[j + 1] = zi[i] * s; }
  }
  forward(x, re, im, o = 0, oi = o){ const J = this.J; this.fwdStart(J, x); while (this.pass(J)); this.fwdEnd(J, re, im, o, oi); }
  inverse(re, im, x, o = 0, oi = o, g = 1){ const J = this.J; this.invStart(J, re, im, o, oi); while (this.pass(J)); this.invEnd(J, x, g); }
}
/* The last word on a wet signal: untouched below 2, rounds off towards 4 above (never more). */
const fxSafe = y => { const a = y < 0 ? -y : y; if (a < 2) return y; const e = a - 2; return (y < 0 ? -1 : 1) * (2 + e / (1 + e * .5)); };

/* Shared by the three: radians in, the sine table out; wrap an angle to −π…π; the bins' tilt gains for Tone. */
const TWO_PI = 2 * Math.PI, HALF_PI = Math.PI / 2;
const sinR = a => sinT(a * .15915494309189535), cosR = a => sinT(a * .15915494309189535 + .25);
const wrapPi = a => a - TWO_PI * Math.round(a * .15915494309189535);
// Tone −1…1 tilts the spectrum about 1 kHz, up to 4.5 dB an octave (capped at +12 / −24 dB). bin k of an n-bin FFT.
const tiltGain = (k, n, sr, tone) => { const f = Math.max(30, k * sr / n); return clamp(Math.pow(f * .001, tone * .75), .063, 4); };
const hannWin = n => { const w = new Float32Array(n); for (let i = 0; i < n; i++) w[i] = .5 - .5 * Math.cos(TWO_PI * i / n); return w; };

/* Spectral (original code): a short-time Fourier effect. Each side is cut into 2048-sample frames, 512 apart (75%
   overlap), Hann-windowed going in and coming out (weighted overlap-add, as in R. E. Crochiere, "A weighted overlap-add
   method of short-time Fourier analysis/synthesis", IEEE Trans. ASSP 28(1), 1980; each bin's frequency from its phase
   advance, after J. L. Flanagan & R. M. Golden's phase vocoder, 1966), changed in the frequency domain and put back.
   Mode: Blur smears the sound over time, a soft spectral wash (Amount: from 0.15 s to 12 s of smear). Freeze lets
   the sound through until Freeze is on, then holds it (Amount: how much the held sound moves). Gate keeps only what
   is within Amount of the loudest frequency in each moment (a little: de-noise; a lot: glassy, whistly). Robot
   flattens every phase: a monotone robot buzzing at ~94 Hz (Amount: how much). Whisper scrambles the phases
   (Amount: how much). Shift moves every frequency up or down by Shift (±2000 Hz, finer near the middle): bells and
   metal, as nothing stays in tune; Amount feeds it back, so it spirals. Tone tilts the result dark or bright.
   Freeze works in every mode: it fades the held sound in (80 ms) and out (300 ms), each frequency running on at its
   own speed with a little random drift, so it breathes instead of buzzing. The wet sound is 2048 samples late
   (43 ms at 48 kHz, 46 ms at 44.1); the dry sound is not delayed. */
const SPX_N = 2048, SPX_H = 512, SPX_K = SPX_N / 2 + 1, SPX_W = hannWin(SPX_N);
const Spectral = {
  type:'spectral', scope:'mono', cost:.3, latency:SPX_N,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[0,0,5,1], amt:[.5,0,1,.01], shift:[.15,-1,1,.01], tone:[0,-1,1,.01], freeze:[0,0,1,1], mix:[.5,0,1,.01] },
  create(sr){
    const K = SPX_K, f = () => new Float32Array(K);
    const ch = seed => ({ fin:new Float32Array(SPX_N), acc:new Float32Array(SPX_N), oq:new Float32Array(SPX_H), x:new Float32Array(SPX_N),
      re:f(), im:f(), prev:f(), B:f(), as:f(), q:f(), A:f(), F:f(), fq:f(), fa:f(), fg:f(), gg:f(), sm:f(), sp:f(), sa:f(),
      yp:f(), yn:f(), ao:f(), an:f(), qs:f(), r:seed, cap:false, pkA:0, fpk:0, fbOn:false });
    // the right channel's frames fall half a hop after the left's, so no block does two big transforms
    return { c:[ch(0x9e3779b9), ch(0x2545f491)], cl:0, cr:SPX_H >> 1, fz:0, mix:-1, tone:new Float32Array(K), toneV:NaN, fft:FFTR.get(SPX_N) };
  },
  process(st, io, ctx){
    const p = io.p, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, sr = ctx.sr, H = SPX_H, N = SPX_N;
    const mode = clamp(p.mode | 0, 0, 5), amt = u(p.amt), mx = u(p.mix), sv = clamp(p.shift, -1, 1), frz = p.freeze >= .5;
    const tone = clamp(p.tone, -1, 1);
    if (tone !== st.toneV){ st.toneV = tone; for (let k = 0; k < SPX_K; k++) st.tone[k] = tiltGain(k, N, sr, tone); }
    const c0 = st.c[0], c1 = st.c[1], f0 = c0.fin, f1 = c1.fin, q0 = c0.oq, q1 = c1.oq;
    let cl = st.cl, cr = st.cr, m = st.mix < 0 ? mx : st.mix;
    for (let i = 0; i < ctx.block; i++){
      // a NaN, an infinity or an absurd sample (|x| ≥ 8) in is a 0, on the dry path too
      const l0 = L[i], r0 = R[i], l = l0 > -8 && l0 < 8 ? l0 : 0, r = r0 > -8 && r0 < 8 ? r0 : 0;
      f0[N - H + cl] = l; f1[N - H + cr] = r;
      m += (mx - m) * .002;   // Mix glides, so it never clicks
      oL[i] = l + (fxSafe(q0[cl]) - l) * m; oR[i] = r + (fxSafe(q1[cr]) - r) * m;
      if (++cl === H){
        cl = 0;
        st.fz = frz ? Math.min(1, st.fz + H / (.08 * sr)) : Math.max(0, st.fz - H / (.3 * sr));
        spxFrame(st, c0, mode, amt, sv, frz, sr);
      }
      if (++cr === H){ cr = 0; spxFrame(st, c1, mode, amt, sv, frz, sr); }
    }
    st.cl = cl; st.cr = cr; st.mix = m;
  }
};
function spxReset(c){
  for (const k of ['fin', 'acc', 'oq', 'prev', 'B', 'as', 'q', 'A', 'F', 'fq', 'fa', 'gg', 'yp', 'yn', 'ao', 'an', 'qs']) c[k].fill(0);
  c.fg.fill(1); c.cap = false; c.pkA = 0; c.fpk = 0;
}
/* One frame of one channel: analyse, change, resynthesise, overlap-add, hand the next 512 samples to the output. */
function spxFrame(st, c, mode, amt, sv, frz, sr){
  const N = SPX_N, H = SPX_H, K = SPX_K, W = SPX_W, x = c.x, re = c.re, im = c.im, fin = c.fin, T = st.tone;
  const sm = c.sm, sp = c.sp, sa = c.sa, prev = c.prev, Bm = c.B, AS = c.as, Q = c.q, A = c.A;
  let inPk = 0;
  for (let i = 0; i < N; i++){ const v = fin[i] * W[i]; x[i] = v; const a = v < 0 ? -v : v; if (a > inPk) inPk = a; }
  st.fft.forward(x, re, im);
  c.pkA += (inPk - c.pkA) * .4;
  const blur = mode === 0 && amt > .001;
  // Blur: each bin's power follows the input through a one-pole smoother, so a sound dies away over 0.15 s × 80^Amount
  // (to −60 dB: 0.15 s … 12 s). Smoothing the power (not the magnitude) keeps the energy, so the smear is as loud.
  const cb = blur ? 1 - Math.exp(-H / (sr * .15 * Math.pow(80, amt) / 13.8)) : 1, jb = .7 * amt;
  let r = c.r;
  for (let k = 0; k < K; k++){
    const a = re[k], b = im[k], M = Math.sqrt(a * a + b * b), phi = Math.atan2(b, a);
    // the bin's phase advance since the last frame: its expected kπ/2 (hop N/4) plus the measured deviation
    const e = (k & 3) * HALF_PI; let d = phi - prev[k] - e; d -= TWO_PI * Math.round(d * .15915494309189535);
    const adv = e + d; prev[k] = phi;
    if (blur){
      let Pv = Bm[k] + (M * M - Bm[k]) * cb; if (Pv < 1e-30) Pv = 0; Bm[k] = Pv;
      const Bv = Math.sqrt(Pv);
      let dd = adv - AS[k]; dd -= TWO_PI * Math.round(dd * .15915494309189535);
      AS[k] = wrapPi(AS[k] + dd * .5 * (M >= Bv ? 1 : M / (Bv + 1e-20)));
      // a bin the input still feeds keeps the input's phase; a fading one runs on at its own frequency, drifting a little
      if (M >= .7 * Bv) Q[k] = phi;
      else { r ^= r << 13; r ^= r >>> 17; r ^= r << 5; Q[k] = wrapPi(Q[k] + AS[k] + ((r >>> 0) * 2.3283064365386963e-10 - .5) * jb); }
      sm[k] = Bv; sp[k] = Q[k]; sa[k] = AS[k];
    } else { Bm[k] = M * M; AS[k] = adv; Q[k] = phi; sm[k] = M; sp[k] = phi; sa[k] = adv; }
    const av = A[k] + (sm[k] - A[k]) * .4; A[k] = av < 1e-15 ? 0 : av;
  }
  // Freeze: catch the (lightly averaged) spectrum, then keep each bin turning at its own frequency, plus drift
  const fz = st.fz;
  if (frz && !c.cap){ c.cap = true; c.F.set(A); c.fq.set(sp); c.fa.set(sa); c.fg.fill(1); c.fpk = c.pkA; }
  if (fz > 0 && c.cap){
    const F = c.F, FQ = c.fq, FA = c.fa, FG = c.fg, jit = mode === 1 ? .08 + amt * 1.1 : .35, mot = mode === 1 ? amt * .05 : 0;
    for (let k = 0; k < K; k++){
      r ^= r << 13; r ^= r >>> 17; r ^= r << 5; const u1 = (r >>> 0) * 2.3283064365386963e-10 - .5;
      FQ[k] = wrapPi(FQ[k] + FA[k] + u1 * jit);
      let g = 1;
      if (mot > 0){ r ^= r << 13; r ^= r >>> 17; r ^= r << 5; g = FG[k] + (1 - FG[k]) * .03 + ((r >>> 0) * 2.3283064365386963e-10 - .5) * mot; g = g < .3 ? .3 : g > 1.7 ? 1.7 : g; FG[k] = g; }
      const fm = F[k] * g;
      if (fz >= 1){ sm[k] = fm; sp[k] = FQ[k]; sa[k] = FA[k]; }
      else {
        const w0 = 1 - fz, yr = w0 * sm[k] * cosR(sp[k]) + fz * fm * cosR(FQ[k]), yi = w0 * sm[k] * sinR(sp[k]) + fz * fm * sinR(FQ[k]);
        sm[k] = Math.sqrt(yr * yr + yi * yi); sp[k] = Math.atan2(yi, yr); if (fz > .5) sa[k] = FA[k];
      }
    }
  }
  if (!frz && fz === 0) c.cap = false;
  // the mode
  if (mode === 5){
    const hz = Math.sign(sv) * sv * sv * 2000, dB = hz * N / sr, dphi = TWO_PI * hz * H / sr, fb = amt * .8 * Math.min(1, Math.abs(hz) / 50);
    const yp = c.yp, yn = c.yn, ao = c.ao, an = c.an, QS = c.qs;
    if (!c.fbOn){ yp.fill(0); ao.fill(0); c.fbOn = true; }
    re[0] = im[0] = re[K - 1] = im[K - 1] = 0; yn[0] = yn[K - 1] = 0;
    for (let j = 1; j < K - 1; j++){
      const s = j - dB;
      if (s < 1 || s > K - 2){ yn[j] = 0; an[j] = 0; re[j] = im[j] = 0; continue; }
      const i0 = s | 0, fr = s - i0, i1 = i0 + 1, m0 = sm[i0] + (sm[i1] - sm[i0]) * fr, m1 = fb * (yp[i0] + (yp[i1] - yp[i0]) * fr), ks = fr < .5 ? i0 : i1;
      // the fed-back spiral adds as power (a different partial each time round), so it can't pile up
      const av = (m0 >= m1 ? sa[ks] : ao[ks]) + dphi, m = Math.sqrt(m0 * m0 + m1 * m1);
      yn[j] = m > 1e-15 ? m : 0; an[j] = wrapPi(av);
      const ph = QS[j] = wrapPi(QS[j] + av), g = m * T[j];
      re[j] = g * cosR(ph); im[j] = g * sinR(ph);
    }
    c.yp = yn; c.yn = yp; c.ao = an; c.an = ao;
  } else {
    c.fbOn = false;
    let thr = 0;
    if (mode === 2){ let mxm = 0; for (let k = 0; k < K; k++) if (sm[k] > mxm) mxm = sm[k]; thr = Math.max(mxm * Math.pow(10, (-70 + amt * 64) / 20), 1e-9); }
    const GG = c.gg;
    for (let k = 0; k < K; k++){
      let m = sm[k], ph = sp[k];
      if (mode === 2){ let g = m >= thr ? 1 : (m / thr) * (m / thr) * (m / thr); const h = GG[k] * .55; if (g < h) g = h; if (g < 1e-9) g = 0; GG[k] = g; m *= g; }
      else if (mode === 3) ph += amt * wrapPi(Math.PI * (k & 1) - ph);   // phase πk: each frame a pulse at its centre
      else if (mode === 4){ r ^= r << 13; r ^= r >>> 17; r ^= r << 5; ph += amt * TWO_PI * ((r >>> 0) * 2.3283064365386963e-10 - .5); }
      m *= T[k]; re[k] = m * cosR(ph); im[k] = m * sinR(ph);
    }
    im[0] = im[K - 1] = 0;
  }
  c.r = r;
  // resynthesis: the inverse, the window again, ÷1.5 (the overlapped squared Hann windows sum to 1.5)
  st.fft.inverse(re, im, x, 0, 0, 1 / 1.5);
  // Frames whose phases no longer line up add as power, not amplitude: 3.1 dB quieter with this window and overlap
  // (Σw⁴ against (Σw²)²). Whisper, Robot and a drifting Freeze get that back.
  const inc = mode === 4 ? amt : 0, fzi = c.cap ? fz * (mode === 1 ? Math.min(1, .3 + amt) : .6) : 0;
  let g = 1 + .434 * Math.max(inc, fzi);
  if (mode === 3 && amt > 0){
    // Robot puts each frame's energy into one pulse: give the frame the energy that went in (×2.06, as the pulses
    // add as power), but keep the pulse no more than 2.5 times as peaky
    let e = 0, pk = 0, ei = 0;
    for (let i = 0; i < N; i++){ const v = x[i] * W[i], a = v < 0 ? -v : v; e += v * v; if (a > pk) pk = a; const w = fin[i] * W[i] * W[i]; ei += w * w; }
    ei = (ei / 2.25 * (1 - fz) + fz * e) * 2.06;
    let ge = e > 1e-20 ? Math.sqrt(ei / e) : 1; ge = 1 + (Math.min(4, ge) - 1) * amt;
    const ref = Math.max(inPk, fz * c.fpk) * 2.5;
    g *= ge; if (pk * g > ref && pk > 0) g = ref / pk;
  }
  const acc = c.acc; let bad = false;
  for (let i = 0; i < N; i++){ const v = acc[i] + x[i] * W[i] * g; acc[i] = v; }
  for (let i = 0; i < H; i++){ const v = acc[i]; if (!(v > -64 && v < 64)){ bad = true; break; } }
  if (bad){ spxReset(c); return; }   // something went wrong: start this channel afresh
  c.oq.set(acc.subarray(0, H)); acc.copyWithin(0, H); acc.fill(0, N - H);
  fin.copyWithin(0, H);
}

/* Extreme stretch (original code): smears a moment into a long ambient wash. It records the last 12 s and reads
   through them Amount times slower (×1.5 … ×50). Each hop (a quarter of a window) it takes a window of the recording
   at the slow read head, keeps the loudness of every frequency, throws the phases away (random ones instead), turns
   it back into sound and overlaps it with its neighbours. Written from the published idea of phase-randomising
   time-stretch (N. Nasca's description of "extreme sound stretching"), not from any code.
   Window (2048 … 32768 samples: 43 ms … 0.68 s at 48 kHz): longer is smoother and blurrier, shorter keeps more of
   the rhythm and grain (the wash starts about half a window after the sound, so the longest is the laziest). Freeze holds
   the read head: an endless drone of that moment, gently moving. Tone tilts it dark or bright. When the read head
   falls too far behind (about 10 s), or the song starts again, it jumps to the newest sound with a 1 s crossfade;
   while it reads silence it waits at the newest sound, and a sound that starts after silence is read at normal
   speed until it reaches the middle of the window, so its wash starts half a window later, not seconds later.
   Each frame's transforms are spread over the blocks of its hop, so no block does much more work than another.
   The same input renders the same output (its random numbers start from a fixed seed). */
const XST_SIZES = [2048, 4096, 8192, 16384, 32768], XST_HANN = {};
const xstHann = n => XST_HANN[n] || (XST_HANN[n] = hannWin(n));
const XStretch = {
  type:'stretch', scope:'mono', cost:.3,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ amt:[.5,0,1,.01], win:[2,0,4,1], freeze:[0,0,1,1], tone:[0,-1,1,.01], mix:[.5,0,1,.01] },
  create(sr){
    const NM = XST_SIZES[XST_SIZES.length - 1], K = NM / 2 + 1, cap = Math.round(sr * 12), J = NM / 16;
    for (const n of XST_SIZES){ xstHann(n); FFTR.get(n); }   // made now, not on the audio thread later
    const ch = () => ({ ring:new Float32Array(cap), hold:new Float32Array(NM + 2 * J), acc:new Float32Array(NM + NM / 4 + 8) });
    const ln = new Float32Array(K); for (let k = 0; k < K; k++) ln[k] = Math.log(Math.max(k, 1));
    const H0 = XST_SIZES[2] / 4;
    return { sr, c:[ch(), ch()], cap, NM, J, wpos:0, H:H0, cnt:0, cen:0, oldCen:0, hc:0, srcA:0, srcB:0, xf:0, xfn:0, frozen:false, jumpReq:false,
      x:new Float32Array(NM), re:new Float32Array(K), im:new Float32Array(K), pw:new Float32Array(K), tilt:new Float32Array(K), ln, tiltN:0, tiltV:NaN,
      job:FFTR.job(NM), u:0, nu:0, fe:new Float64Array(2), quiet:true, catchTo:-Infinity, fr:{ N:XST_SIZES[2], P:0, ns:1, tu:0, two:false, wa:0, sa:0, a:0, sb:0, b:0, tone:0 }, r:0x6d2b79f5, sn:undefined, mix:-1 };
  },
  process(st, io, ctx){
    const p = io.p, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, B = ctx.block, cap = st.cap;
    const c0 = st.c[0], c1 = st.c[1], r0 = c0.ring, r1 = c1.ring, a0 = c0.acc, a1 = c1.acc, mx = u(p.mix);
    if (restarted(st, ctx) && st.wpos > 0) st.jumpReq = true;   // the song started again: on to the newest sound
    if (st.mix < 0) st.mix = mx;
    let done = 0;
    while (done < B){
      // this hop's share of the frame's work: it must be finished when the hop ends
      const left = st.H - st.cnt, n = Math.min(B - done, left), want = Math.ceil((st.nu - st.u) / Math.max(1, Math.ceil(left / B)));
      for (let k = 0; k < want && st.u < st.nu; k++) xstUnit(st);
      let w = st.wpos % cap, m = st.mix;
      for (let i = done, j = st.cnt; i < done + n; i++, j++){
        // a NaN, an infinity or an absurd sample (|x| ≥ 8) in is a 0, on the dry path too
        const l0 = L[i], q0 = R[i], l = l0 > -8 && l0 < 8 ? l0 : 0, r = q0 > -8 && q0 < 8 ? q0 : 0;
        r0[w] = l; r1[w] = r; if (++w === cap) w = 0;
        m += (mx - m) * .002;   // Mix glides, so it never clicks
        oL[i] = l + (fxSafe(a0[j]) - l) * m; oR[i] = r + (fxSafe(a1[j]) - r) * m;
      }
      st.mix = m; st.wpos += n; st.cnt += n; done += n;
      if (st.cnt >= st.H){ while (st.u < st.nu) xstUnit(st); xstHop(st, p); }
    }
  }
};
/* End of a hop: the frame made during it is in the accumulators; move them on, move the read head, plan the next
   frame. Read sources: 0 the recording at the read head, 1 the frozen copy, 2 the recording at the old head (while
   a jump crossfades away from it). A crossfade blends two sources' powers: 0.4 s into and out of Freeze, 1 s for
   a jump. */
function xstHop(st, p){
  const sr = st.sr, H = st.H;
  for (const c of st.c){ const a = c.acc; a.copyWithin(0, H); a.fill(0, a.length - H); }
  st.cnt = 0;
  const N = XST_SIZES[clamp(p.win | 0, 0, XST_SIZES.length - 1)], Hn = N / 4, cap = st.cap, NM = st.NM;
  const stretch = 1.5 * Math.pow(50 / 1.5, u(p.amt)), frz = p.freeze >= .5, adv = H / stretch;
  const xfF = clamp(Math.round(.4 * sr / Hn), 2, 40), xfJ = clamp(Math.round(sr / Hn), 2, 100);
  // how old the head may get: the room a 1 s crossfade and the largest window need, inside the 12 s
  const oldest = st.wpos - cap + Math.round(1.2 * sr) + NM + 2 * st.J, newest = st.wpos - N / 2;
  // Reading silence (the last frame's window held less than −90 dB): wait at the newest sound. When sound comes,
  // read at normal speed until the head reaches where it came in (the end of that window), then slow down.
  const f0 = st.fr, free = !st.frozen && st.srcB === 0 && st.xf >= st.xfn;
  if (free){
    if (st.fe[0] + st.fe[1] < 7.5e-10 * f0.N) st.quiet = true;
    else if (st.quiet){ st.quiet = false; st.catchTo = f0.b + f0.N; }
  }
  if (!st.frozen) st.cen = free && st.quiet ? newest : Math.min(st.cen + (st.cen < st.catchTo ? H : adv), newest);   // frozen, it stays put
  if (st.srcA === 2 && st.xf < st.xfn) st.oldCen += adv;
  if (frz && !st.frozen){
    // catch the moment: copy the largest window (and room to jitter) around the read head
    st.frozen = true; st.hc = st.cen;
    const h0 = Math.round(st.hc - NM / 2 - st.J);
    for (const c of st.c){ const hd = c.hold, rg = c.ring; for (let i = 0; i < hd.length; i++){ const q = h0 + i; hd[i] = q >= 0 && q < st.wpos && q >= st.wpos - cap ? rg[q % cap] : 0; } }
    st.srcA = st.srcB === 2 ? 0 : st.srcB; st.srcB = 1; st.xf = 0; st.xfn = xfF;
  } else if (!frz && st.frozen){
    st.frozen = false;
    if (st.jumpReq || st.cen - N / 2 < oldest){ st.cen = newest; st.jumpReq = false; st.catchTo = -Infinity; }
    st.srcA = 1; st.srcB = 0; st.xf = 0; st.xfn = xfF;
  } else if (!frz && st.xf >= st.xfn && (st.jumpReq || st.cen - N / 2 < oldest)){
    // a jump to the newest sound: the old head keeps reading (fading out) while the new one fades in
    st.oldCen = st.cen; st.cen = newest; st.jumpReq = false; st.catchTo = -Infinity; st.srcA = 2; st.srcB = 0; st.xf = 0; st.xfn = xfJ;
  }
  const f = st.fr, two = st.xf < st.xfn && st.srcA !== st.srcB;
  f.N = N; f.P = FFTR.get(N).passes; f.two = two; f.ns = two ? 2 : 1;
  f.wa = two ? 1 - (st.xf + 1) / (st.xfn + 1) : 0; if (st.xf < st.xfn) st.xf++;
  f.tone = clamp(p.tone, -1, 1); f.tu = !(Math.abs(f.tone - st.tiltV) <= .004) || N !== st.tiltN ? 1 : 0;
  f.sa = st.srcA; f.a = two ? Math.round(xstPos(st, st.srcA, N) - N / 2) : 0;
  f.sb = st.srcB; f.b = Math.round(xstPos(st, st.srcB, N) - N / 2);
  st.H = Hn; st.u = 0; st.nu = f.tu + 2 * ((f.ns + 1) * (f.P + 2));
}
/* Where a source reads; the frozen read point wanders ±N/16, so the drone moves. */
function xstPos(st, s, N){
  if (s !== 1) return s === 2 ? st.oldCen : st.cen;
  let r = st.r; r ^= r << 13; r ^= r >>> 17; r ^= r << 5; st.r = r;
  return st.hc + ((r >>> 0) * 2.3283064365386963e-10 * 2 - 1) * N / 16;
}
/* One window of a source into st.x, Hann-windowed (zeros where nothing was recorded); returns its energy. */
function xstGrab(st, c, s, at, N){
  const x = st.x, w = xstHann(N), cap = st.cap; let e = 0;
  if (s === 1){
    const h = c.hold, o = at - Math.round(st.hc - st.NM / 2 - st.J), hl = h.length;
    for (let i = 0; i < N; i++){ const q = o + i; x[i] = q >= 0 && q < hl ? h[q] * w[i] : 0; }
  } else {
    const rg = c.ring, lo = Math.max(0, st.wpos - cap), hi = st.wpos;
    let q = at, k = ((at % cap) + cap) % cap;
    for (let i = 0; i < N; i++, q++){ const v = q >= lo && q < hi ? rg[k] * w[i] : 0; x[i] = v; e += v * v; if (++k === cap) k = 0; }
  }
  return e;
}
/* One unit of the frame's work. In order: [the Tone table], then for each channel: each source (grab and start,
   the passes, the magnitudes), then the resynthesis (random phases and start, the passes, window and add). */
function xstUnit(st){
  const f = st.fr, N = f.N, K = N / 2 + 1, fft = FFTR.get(N), P = f.P, J = st.job, re = st.re, im = st.im, pw = st.pw;
  let v = st.u++;
  if (f.tu){
    if (v === 0){
      // Tone: ±4.5 dB an octave about 1 kHz (+12 / −24 dB at most), from a table of ln k
      const T = st.tilt, ln = st.ln, a = f.tone * .75, cb = Math.log(st.sr / N / 1000), l30 = Math.log(.03);
      for (let k = 0; k < K; k++){ let e = ln[k] + cb; if (e < l30) e = l30; e *= a; T[k] = e > 1.386 ? 4 : e < -2.765 ? .063 : Math.exp(e); }
      st.tiltV = f.tone; st.tiltN = N; return;
    }
    v--;
  }
  const per = (f.ns + 1) * (P + 2), ci = v >= per ? 1 : 0, ch = st.c[ci];
  v -= ci * per;
  if (v < f.ns * (P + 2)){
    const si = v >= P + 2 ? 1 : 0, w = v - si * (P + 2), first = f.two && si === 0;
    if (w === 0){ const e = xstGrab(st, ch, first ? f.sa : f.sb, first ? f.a : f.b, N); st.fe[ci] = first ? 1 : e; fft.fwdStart(J, st.x); }
    else if (w <= P) fft.pass(J);
    else {
      fft.fwdEnd(J, re, im);
      if (!f.two) for (let k = 0; k < K; k++) pw[k] = re[k] * re[k] + im[k] * im[k];
      else if (first){ const g = f.wa; for (let k = 0; k < K; k++) pw[k] = (re[k] * re[k] + im[k] * im[k]) * g; }
      else { const g = 1 - f.wa; for (let k = 0; k < K; k++) pw[k] += (re[k] * re[k] + im[k] * im[k]) * g; }
    }
    return;
  }
  const w = v - f.ns * (P + 2);
  if (w === 0){
    // keep each bin's loudness, give it a random phase
    const T = st.tilt; let r = st.r;
    for (let k = 1; k < K - 1; k++){
      let m = Math.sqrt(pw[k]) * T[k]; if (m < 1e-20) m = 0;
      r ^= r << 13; r ^= r >>> 17; r ^= r << 5; const ph = (r >>> 0) * 2.3283064365386963e-10;
      re[k] = m * sinT(ph + .25); im[k] = m * sinT(ph);
    }
    r ^= r << 13; r ^= r >>> 17; r ^= r << 5; st.r = r;
    re[0] = 0; im[0] = 0; re[K - 1] = 0; im[K - 1] = 0;   // no DC, no Nyquist
    fft.invStart(J, re, im);
  } else if (w <= P) fft.pass(J);
  else {
    // random phases scatter each frame's energy, so overlapped frames add as power: ×4/3 keeps the level
    // (Hann in and out, hop N/4: 1/√(Σw²/N · Σ overlapped w²) = 1/√(3/8 · 3/2))
    fft.invEnd(J, st.x, 4 / 3);
    const x = st.x, hw = xstHann(N), a = ch.acc, o = st.H;
    for (let i = 0; i < N; i++) a[o + i] += x[i] * hw[i];
  }
}

/* Convolution reverb (original code): the sound convolved with an impulse response (IR) made from the controls, or
   with one of the song's own IRs. Uniformly partitioned FFT convolution (overlap-save with a frequency-domain delay
   line per stage, after W. G. Gardner, "Efficient convolution without input-output delay", JAES 43(3), 1995, and
   F. Wefers, "Partitioned convolution algorithms for real-time auralization", 2015) in three stages: 128-sample
   partitions for the first 43 ms, done in the block itself, then 1024-sample ones to 0.34 s and 8192 for the rest,
   the bigger stages' work spread evenly over the blocks before it is due. Neither the dry nor the wet sound is
   delayed (with blocks of 128; any other block size puts the wet sound 128 samples late).
   The IRs are made, not stored, and the same settings always make the same IR, so renders repeat. Rooms get early
   reflections from a shoebox room's mirror images (J. B. Allen & D. A. Berkley, "Image method for efficiently
   simulating small-room acoustics", JASA 65(4), 1979), each bounce a little duller, heard by two microphones for
   stereo, then a noise tail that starts sparse and grows dense (velvet noise, after M. Karjalainen & H. Järveläinen,
   "Reverberation modeling using velvet noise", AES 30th Conf., 2007), through a damping filter that closes as it
   decays (so the highs die first), with a longer-ringing bass in the big rooms. Room, Hall, Chamber, Cathedral: rooms
   from small and live to vast and dark. Plate: dense at once, no reflections, bright. Spring: a dispersive spring
   loop, chirping echoes (two springs). Gated: the 80s drum room, cut off dead. Reverse: swells up into the sound.
   Speaker cab and Telephone: short filter responses (RBJ cookbook biquads) with a little room around the mic.
   Song IR 1–4 play the song's own IRs (globalThis.__thunderIR[0–3] = { L, R or null, sr, id }), resampled to the
   engine's rate; with none there, the dry sound passes untouched. Every IR is scaled to the same energy, so
   changing the type keeps the level.
   Controls: Size scales the space (Spring: its length; Cab: the speaker; Telephone: the bandwidth; Song IR: stretches
   it, ×0.5 … ×2, which also shifts its pitch). Decay is the RT60, 0.1 … 10 s (Gated: the gate time; Reverse: the
   swell; Cab and Telephone: the room around the mic, a tenth of it; Song IR: where it is cut off). Pre-delay holds
   the reverb back. Tone: dull (heavy high damping) to bright (Cab: the mic from the edge to the centre of the cone;
   Telephone: the honk; Song IR: darker or brighter). Early: the early reflections, none to twice their natural level
   (Plate: how sharp the onset is; Spring: the drip; Reverse: how sharply it stops; Cab and Telephone: how much room;
   Song IR: the level of its first 50 ms). Width: the reverb's stereo spread (0 mono, 150% extra wide). Mix.
   A change to any control but Width and Mix makes a new IR once the controls have been still for 150 ms (or every
   second while they keep moving), a little at a time across the blocks, then crossfades to it over 170 ms; it is
   heard 0.5–1 s after the change. The first IR is made as it is needed, each part just before it is used, so it is
   there from the first sample. Up to 6 s of IR (longer Song IRs are cut). */
const CNV_NAMES = ['Room', 'Hall', 'Chamber', 'Plate', 'Cathedral', 'Spring', 'Gated', 'Reverse', 'Speaker cab', 'Telephone', 'Song IR 1', 'Song IR 2', 'Song IR 3', 'Song IR 4'];
const CNV_KIND = [0, 0, 0, 1, 0, 2, 3, 4, 5, 6, 7, 7, 7, 7];   // room, plate, spring, gated, reverse, cab, telephone, song IR
// per type: room x, y, z (m), wall reflection, mirror-image order, energy share of the reflections, density build
// time (s), first density (/s), bass RT ×, highs RT ×, low cut (Hz). Cab and Telephone use the last two (small rooms).
const CNV_ROOM = [[6.2, 4.4, 2.9, .8, 4, .4, .035, 1500, 1.1, .9, 50], [30, 21, 13, .86, 3, .25, .1, 400, 1.3, .85, 35],
  [8, 5.5, 4, .93, 4, .3, .02, 3000, 1, 1.15, 60], [0, 0, 0, 0, 0, 0, .006, 15000, .7, 1.25, 140],
  [60, 30, 32, .9, 3, .2, .25, 150, 1.5, .7, 28], [0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 60],
  [10, 7, 4.5, .85, 3, .3, .012, 3000, 1, 1.1, 70], [20, 14, 9, .86, 3, .2, .05, 2000, 1.1, .9, 50],
  [3.5, 3, 2.5, .6, 3, .6, .01, 3000, 1, .8, 0], [2.4, 2, 2.2, .7, 3, .6, .008, 3000, 1, .8, 0]];
const CNV_S = [128, 1024, 8192], CNV_D = [0, 2048, 16384], CNV_BUDGET = 6, CNV_FADE = 8192, CNV_SPR = 36;
const Convolve = {
  type:'convolve', scope:'mono', cost:.35, latency:0,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ type:[1,0,13,1], size:[.5,0,1,.01], decay:[2,.1,10,.01], pre:[.01,0,.25,.001], tone:[.5,0,1,.01], early:[.5,0,1,.01], width:[1,0,1.5,.01], mix:[.3,0,1,.01] },
  create(sr){
    const S = CNV_S, cap = Math.ceil(sr * 6.25), P = [16, 14, Math.max(1, Math.ceil((cap - CNV_D[2]) / S[2]))], K = S.map(n => n + 1);
    for (const n of S) FFTR.get(2 * n);   // the transforms' tables, made now rather than on the audio thread
    const spec = s => ({ re:new Float32Array(P[s] * K[s]), im:new Float32Array(P[s] * K[s]) }), two = f => [f(), f()];
    const slot = () => ({ H:[0, 1, 2].map(s => two(() => spec(s))), nz:[0, 1, 2].map(s => new Uint8Array(P[s])), g:1, gt:1,
      ring:[null, two(() => new Float32Array(3 * S[1])), two(() => new Float32Array(3 * S[2]))], o0:two(() => new Float32Array(128)) });
    const job = s => ({ on:false, S:0, j:0, need:0, u:0, nu:0, left:0, ph:0, c:0, sub:0, pair:0, pairs:new Int8Array(4), np:0, fj:FFTR.job(2 * S[s]) });
    const syn = () => ({ r:1, l1:0, l2:0, l3:0, e:new Float64Array(4), d:new Float64Array(4), lp:0, hp:0, hx:0, bq:new Float64Array(24),
      dl:new Float32Array(Math.ceil(sr * .09) + 4), w:0, D:1, ap:new Float64Array(CNV_SPR * 2), sl:0 });
    const G = { on:false, init:false, slot:0, ph:0, n:0, type:-1, size:0, decay:0, pre:0, tone:0, early:0, obj:null, id:undefined,
      kind:0, len:0, pre0:0, tr:1, tb:1, rho0:0, gate:0, cut:0, kr:0, fc0:0, th:1, ab:0, qd:0, qb:0, tail0:0, kt:1, kh:0, kp:0, ls:1, drip:0,
      C:new Float64Array(30), E:new Float64Array(2), g:0, ready:0, s:0, p:0, c:0, sub:0,
      step:1, ka:0, hg:1, kx:0, ge:1, geo:new Float64Array(12), ord:0, er:false, erEnd:0, erR:1, ex:0, clr:0, eest:1, et:0, rm:0, ef:0,
      srcL:null, srcR:null, srcN:0, sr, sy:two(syn), ir:two(() => new Float32Array(cap)), x:new Float64Array(2 * S[2]), job:FFTR.job(2 * S[2]) };
    return { sr, cap, P, K, n:0, fpos:0, sl:[slot(), slot()], act:0, fd:false, fa:0, fb:1, fs:0, fe:0, free:0, last:0,
      X:[0, 1, 2].map(s => two(() => spec(s))), head:[0, 0, 0], ticks:[0, 0, 0],
      hist:[0, 1, 2].map(s => two(() => new Float32Array(2 * S[s]))), hp:[0, 0, 0], snap:two(() => new Float32Array(2 * S[2])),
      Y:[0, 1, 2].map(s => ({ re:new Float64Array(K[s]), im:new Float64Array(K[s]) })), x:[0, 1, 2].map(s => new Float64Array(2 * S[s])),
      job:[null, job(1), job(2)], fin:two(() => new Float32Array(128)), wq:two(() => new Float32Array(128)),
      G, want:new Float64Array(6), wobj:null, wid:undefined, chg:0, started:false, mix:-1 };
  },
  process(st, io, ctx){
    const p = io.p, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, B = ctx.block, G = st.G;
    cnvWatch(st, p);
    if (G.on){ let w = 0; while (G.on && w < CNV_BUDGET) w += cnvGen(st); }
    // with no IR at all (a Song IR that isn't there), the dry sound passes at full level
    const mx = st.sl[st.act].empty && !st.fd ? 0 : u(p.mix), wd = clamp(p.width, 0, 1.5), fL = st.fin[0], fR = st.fin[1], qL = st.wq[0], qR = st.wq[1];
    if (st.mix < 0) st.mix = mx;
    let m = st.mix;
    if (B === 128 && st.fpos === 0){
      // the usual case: one block is one frame, so the wet sound is not late at all. A NaN, an infinity or an absurd
      // sample (|x| ≥ 8) in is a 0, on the dry path too.
      for (let i = 0; i < 128; i++){ const l = L[i], r = R[i]; fL[i] = l > -8 && l < 8 ? l : 0; fR[i] = r > -8 && r < 8 ? r : 0; }
      cnvFrame(st);
      for (let i = 0; i < 128; i++){
        const l = fL[i], r = fR[i], wl = qL[i], wr = qR[i], mid = (wl + wr) * .5, sd = (wl - wr) * .5 * wd;
        m += (mx - m) * .002;   // Mix glides, so it never clicks
        oL[i] = l * (1 - m) + fxSafe(mid + sd) * m; oR[i] = r * (1 - m) + fxSafe(mid - sd) * m;
      }
    } else {
      // any other block size: 128-sample frames, the wet path 128 samples late
      for (let i = 0; i < B; i++){
        const l0 = L[i], r0 = R[i], l = l0 > -8 && l0 < 8 ? l0 : 0, r = r0 > -8 && r0 < 8 ? r0 : 0, j = st.fpos, wl = qL[j], wr = qR[j], mid = (wl + wr) * .5, sd = (wl - wr) * .5 * wd;
        m += (mx - m) * .002;
        oL[i] = l * (1 - m) + fxSafe(mid + sd) * m; oR[i] = r * (1 - m) + fxSafe(mid - sd) * m;
        fL[j] = l; fR[j] = r;
        if (++st.fpos === 128){ st.fpos = 0; cnvFrame(st); }
      }
    }
    st.mix = m;
  }
};
/* Has the shape changed? Then a new IR, once it has been still for 150 ms (or a second has passed). */
function cnvWatch(st, p){
  const w = st.want, G = st.G, sr = st.sr, type = clamp(Math.round(p.type), 0, CNV_NAMES.length - 1);
  const v1 = u(p.size), v2 = clamp(p.decay, .1, 10), v3 = clamp(p.pre, 0, .25), v4 = u(p.tone), v5 = u(p.early);
  let obj = null, id;
  if (CNV_KIND[type] === 7){ const T = globalThis.__thunderIR; obj = (T && T[type - 10]) || null; id = obj ? obj.id : undefined; }
  if (w[0] !== type || w[1] !== v1 || w[2] !== v2 || w[3] !== v3 || w[4] !== v4 || w[5] !== v5 || st.wobj !== obj || st.wid !== id){
    if (st.n - st.chg >= .15 * sr) st.chg0 = st.n;   // a new run of changes starts
    w[0] = type; w[1] = v1; w[2] = v2; w[3] = v3; w[4] = v4; w[5] = v5; st.wobj = obj; st.wid = id; st.chg = st.n;
  }
  if (!st.started){ st.started = true; cnvStart(st, true); cnvGen(st); return; }   // the first IR: set up now, made as it is needed
  if (G.on || st.fd || st.n < st.free) return;
  if (G.type === type && G.size === v1 && G.decay === v2 && G.pre === v3 && G.tone === v4 && G.early === v5 && G.obj === obj && G.id === id) return;
  if (st.n - st.chg >= .15 * sr || st.n - Math.max(st.last, st.chg0 || 0) >= sr) cnvStart(st, false);
}
function cnvStart(st, init){
  const G = st.G, w = st.want;
  G.on = true; G.init = init; G.slot = init ? st.act : 1 - st.act; G.ph = 0; G.ready = 0; st.last = st.n;
  G.type = w[0]; G.size = w[1]; G.decay = w[2]; G.pre = w[3]; G.tone = w[4]; G.early = w[5]; G.obj = st.wobj; G.id = st.wid;
  const Z = st.sl[G.slot];
  for (let s = 0; s < 3; s++) Z.nz[s].fill(0);
  if (!init) for (let s = 1; s < 3; s++){ Z.ring[s][0].fill(0); Z.ring[s][1].fill(0); }
}
/* The IR slots a stretch of output [t, t + n) needs: the playing one, and while crossfading the new one. */
function cnvNeed(st, t, n){
  if (!st.fd) return 1 << st.act;
  return (t < st.fe ? 1 << st.fa : 0) | (t + n > st.fs ? 1 << st.fb : 0);
}
/* While the first IR is still being turned into spectra: finish partition p of stage s before it is used. */
function cnvReady(st, slot, s, p){
  const G = st.G;
  if (!G.on || !G.init || G.slot !== slot) return;
  const e = CNV_D[s] + (p + 1) * CNV_S[s];
  while (G.on && G.ready < e) cnvGen(st);
}
/* Y += Σ X(newest − p) · H(p) for partitions p0 … p1 − 1 (those with anything in them). */
function cnvMac(st, slot, s, c, p0, p1){
  const P = st.P[s], K = st.K[s], X = st.X[s][c], H = st.sl[slot].H[s][c], nz = st.sl[slot].nz[s], Y = st.Y[s], Yr = Y.re, Yi = Y.im, Xr = X.re, Xi = X.im, Hr = H.re, Hi = H.im;
  for (let p = p0; p < p1; p++){
    cnvReady(st, slot, s, p);
    if (!nz[p]) continue;
    const xo = ((st.head[s] - p + P) % P) * K, ho = p * K;
    for (let k = 0; k < K; k++){ const a = Xr[xo + k], b = Xi[xo + k], c2 = Hr[ho + k], d = Hi[ho + k]; Yr[k] += a * c2 - b * d; Yi[k] += a * d + b * c2; }
  }
}
/* One 128-sample frame: stage 0 in full, the bigger stages' input and this frame's share of their work, the mix. */
function cnvFrame(st){
  const S = CNV_S, fin = st.fin, t0 = st.n, f0 = FFTR.get(256), h0 = st.hist[0];
  // what the IRs hear: each side with a quarter of the other, so a sound on one side still fills the room
  for (let i = 0; i < 128; i++){ const l = fin[0][i], r = fin[1][i]; h0[0][128 + i] = l * .75 + r * .25; h0[1][128 + i] = r * .75 + l * .25; }
  // stage 0: [last frame, this frame] → the newest spectrum, and this frame's output at once
  st.head[0] = (st.head[0] + 1) % st.P[0]; st.ticks[0]++;
  for (let c = 0; c < 2; c++) f0.forward(h0[c], st.X[0][c].re, st.X[0][c].im, st.head[0] * st.K[0]);
  const need0 = cnvNeed(st, t0, 128), pm0 = Math.min(st.ticks[0], st.P[0]);
  for (let k = 0; k < 2; k++){
    if (!(need0 >> k & 1)) continue;
    for (let c = 0; c < 2; c++){
      const Y = st.Y[0]; Y.re.fill(0); Y.im.fill(0);
      cnvMac(st, k, 0, c, 0, pm0);
      const x = st.x[0]; f0.inverse(Y.re, Y.im, x);
      const o = st.sl[k].o0[c]; for (let i = 0; i < 128; i++) o[i] = x[128 + i];
    }
  }
  // stages 1 and 2: gather the input; when a partition's worth is in, start the job that makes its output, which
  // is due one partition later (overlap-save: the output for [kN + 2N, kN + 3N) once input block k is complete)
  for (let s = 1; s < 3; s++){
    const N = S[s], H0 = st.hist[s];
    for (let c = 0; c < 2; c++) H0[c].set(h0[c].subarray(128, 256), N + st.hp[s]);
    st.hp[s] += 128;
    if (st.hp[s] < N) continue;
    const J = st.job[s];
    while (J.on) cnvJob(st, s);   // never needed (a job ends within its partition), but safe
    st.hp[s] = 0; st.head[s] = (st.head[s] + 1) % st.P[s]; st.ticks[s]++;
    if (s === 1){ const f1 = FFTR.get(2 * N); for (let c = 0; c < 2; c++) f1.forward(H0[c], st.X[1][c].re, st.X[1][c].im, st.head[1] * st.K[1]); }
    else for (let c = 0; c < 2; c++) st.snap[c].set(H0[c]);
    for (let c = 0; c < 2; c++) H0[c].copyWithin(0, N);
    J.S = t0 + 128 + N; J.j = st.ticks[s] - 1; J.need = cnvNeed(st, J.S, N); J.np = 0;
    for (let k = 0; k < 2; k++) if (J.need >> k & 1){ J.pairs[J.np++] = k * 2; J.pairs[J.np++] = k * 2 + 1; }
    const passes = FFTR.get(2 * N).passes, per = s === 2 ? st.P[2] + passes + 2 : Math.ceil(st.P[1] / 7) + 1;
    J.nu = (s === 2 ? 2 * (passes + 2) : 0) + J.np * per; J.u = 0; J.left = N / 128; J.ph = s === 2 ? 0 : 1; J.c = 0; J.sub = 0; J.pair = 0;
    J.on = J.nu > 0;
  }
  for (let c = 0; c < 2; c++) h0[c].copyWithin(0, 128, 256);
  // this frame's share of the jobs (each ends a frame or two before its output is due)
  for (let s = 1; s < 3; s++){
    const J = st.job[s]; if (!J.on) continue;
    const n = Math.ceil((J.nu - J.u) / Math.max(1, J.left)); J.left--;
    for (let k = 0; k < n && J.on; k++) cnvJob(st, s);
  }
  // sum the stages, crossfading between two IRs when there are two; each read sample is cleared for its next turn
  const qL = st.wq[0], qR = st.wq[1], N1 = 3 * S[1], N2 = 3 * S[2], use = st.fd ? 3 : 1 << st.act;
  let i1 = t0 % N1, i2 = t0 % N2;
  for (let i = 0; i < 128; i++){
    let wl = 0, wr = 0, ga = 1, gb = 1;
    if (st.fd){ const th = clamp((t0 + i - st.fs) / (st.fe - st.fs), 0, 1); ga = sinT(th * .25 + .25); gb = sinT(th * .25); }
    for (let k = 0; k < 2; k++){
      if (!(use >> k & 1)) continue;
      const Z = st.sl[k], r1 = Z.ring[1], r2 = Z.ring[2];
      Z.g += (Z.gt - Z.g) * .0005;   // the level (each IR is scaled to the same energy), glided
      const g = Z.g * (!st.fd ? 1 : k === st.fa ? ga : gb);
      wl += (Z.o0[0][i] + r1[0][i1] + r2[0][i2]) * g; wr += (Z.o0[1][i] + r1[1][i1] + r2[1][i2]) * g;
      r1[0][i1] = 0; r1[1][i1] = 0; r2[0][i2] = 0; r2[1][i2] = 0;
    }
    if (!(wl > -64 && wl < 64 && wr > -64 && wr < 64)) wl = wr = 0;
    qL[i] = wl; qR[i] = wr;
    if (++i1 === N1) i1 = 0; if (++i2 === N2) i2 = 0;
  }
  st.n = t0 + 128;
  if (st.fd && st.n >= st.fe){ st.act = st.fb; st.fd = false; st.free = st.n + 128; }
}
/* One unit of a stage-1/2 job: [stage 2: the two forward transforms, a pass at a time], then for each IR slot and
   side: the partitions (stage 2: one a unit; stage 1: seven) and the inverse transform, whose second half is the
   output for the partition starting at J.S. */
function cnvJob(st, s){
  const J = st.job[s], N = CNV_S[s], f = FFTR.get(2 * N), P = st.P[s], K = st.K[s];
  J.u++;
  if (J.ph === 0){
    if (J.sub === 0) f.fwdStart(J.fj, st.snap[J.c]);
    else if (J.sub <= f.passes) f.pass(J.fj);
    else { f.fwdEnd(J.fj, st.X[2][J.c].re, st.X[2][J.c].im, st.head[2] * K); J.sub = -1; if (++J.c === 2) J.ph = 1; }
    J.sub++;
    if (J.ph === 1 && J.np === 0) J.on = false;
    return;
  }
  const pr = J.pairs[J.pair], slot = pr >> 1, c = pr & 1, Y = st.Y[s], nM = s === 2 ? P : Math.ceil(P / 7), pm = Math.min(J.j + 1, P);
  if (J.sub < nM){
    if (J.sub === 0){ Y.re.fill(0); Y.im.fill(0); }
    const a = s === 2 ? J.sub : J.sub * 7, b = Math.min(pm, s === 2 ? a + 1 : a + 7);
    if (a < b) cnvMac(st, slot, s, c, a, b);
    J.sub++; return;
  }
  const x = st.x[s], v = J.sub - nM;
  let done = false;
  if (s === 1){ f.inverse(Y.re, Y.im, x); done = true; }
  else if (v === 0) f.invStart(J.fj, Y.re, Y.im);
  else if (v <= f.passes) f.pass(J.fj);
  else { f.invEnd(J.fj, x); done = true; }
  J.sub++;
  if (done){
    const ring = st.sl[slot].ring[s][c], o = J.S % (3 * N);
    for (let i = 0; i < N; i++) ring[o + i] = x[N + i];
    J.sub = 0; if (++J.pair >= J.np) J.on = false;
  }
}
/* The IR maker. cnvGen does one unit of work and returns its rough cost (1 ≈ 40 µs on a desktop; measured): first the setup,
   then, in order of time, it makes a stretch of the IR and turns each partition into spectra once its samples are
   there. Its level: every IR is scaled to the same energy (CNV_E per side). The first IR plays while it is being
   made, at the energy worked out beforehand, corrected (smoothly) when it is done; a new one is measured before it
   is heard. */
const CNV_E = .09;   // about −6 dB from .35: its tail now sits with the Plate and Spring reverbs at the same Mix
function cnvGen(st){
  const G = st.G;
  if (G.ph === 0){ cnvSetup(st); G.ph = 1; G.clr = G.init ? st.cap : 0; return 4; }   // the first IR's buffers are new (zero)
  if (G.ph === 1){
    // clear the new IR, a stretch at a time
    const e = Math.min(st.cap, G.len + 1), b = Math.min(e, G.clr + 32768);
    G.ir[0].fill(0, G.clr, b); G.ir[1].fill(0, G.clr, b); G.clr = b;
    if (b >= e){ G.ph = G.er ? 2 : 4; G.ex = -G.ord; }
    return .5;
  }
  if (G.ph === 2){ cnvEarly(st, G.ex); if (++G.ex > G.ord) G.ph = 3; return .3; }
  if (G.ph === 3){ cnvEarlyScale(st); G.ph = 4; return 2.5; }
  const S = CNV_S, s = G.s, N = S[s], o = CNV_D[s] + G.p * N, Z = st.sl[G.slot];
  if (G.p >= st.P[s] || o >= G.len){
    if (s < 2){ G.s++; G.p = 0; G.c = 0; G.sub = 0; return .05; }
    return cnvDone(st);
  }
  const e = Math.min(o + N, G.len);
  if (G.n < e) return cnvSynth(st);
  const ir = G.ir, K = st.K[s], ho = G.p * K;
  if (s === 0){
    // (its own buffers: this can run in the middle of the stage-0 convolution, while the first IR is being made)
    const f = FFTR.get(256), x = G.x;
    for (let c = 0; c < 2; c++){
      for (let i = 0; i < 128; i++){ x[i] = o + i < e ? ir[c][o + i] : 0; x[128 + i] = 0; }
      const H = Z.H[0][c]; f.fwdStart(G.job, x); while (f.pass(G.job)); f.fwdEnd(G.job, H.re, H.im, ho);
    }
    cnvPart(st, Z, 0, o, N); return .2;
  }
  const x = G.x, J = G.job, f = FFTR.get(2 * N), c = G.c, H = Z.H[s][c];
  if (G.sub === 0){ const a = ir[c]; for (let i = 0; i < N; i++){ x[i] = o + i < e ? a[o + i] : 0; x[N + i] = 0; } f.fwdStart(J, x); G.sub = 1; return s === 1 ? .2 : 1.6; }
  if (G.sub <= f.passes){ const k = s === 1 ? f.passes : 1; for (let j = 0; j < k; j++) f.pass(J); G.sub += k; return s === 1 ? .6 : .5; }
  f.fwdEnd(J, H.re, H.im, ho); G.sub = 0;
  if (++G.c === 2){ G.c = 0; cnvPart(st, Z, s, o, N); }
  return s === 1 ? .15 : .8;
}
/* A partition is ready: is anything in it (else the convolution skips it), and move on. */
function cnvPart(st, Z, s, o, N){
  const G = st.G, a = G.ir[0], b = G.ir[1], e = Math.min(o + N, G.len); let nz = 0;
  for (let i = o; i < e; i++) if (a[i] !== 0 || b[i] !== 0){ nz = 1; break; }
  Z.nz[s][G.p] = nz; G.p++; G.ready = o + N;
}
function cnvDone(st){
  const G = st.G, Z = st.sl[G.slot], E = G.E[0] + G.E[1];
  G.on = false; G.ph = 5; G.ready = Infinity;
  Z.empty = !(E > 1e-20); Z.gt = Z.empty ? 0 : Math.sqrt(2 * CNV_E / E);
  if (G.init) return .1;
  // a new IR: crossfade to it once both bigger stages have started making its output
  Z.g = Z.gt;
  const S = CNV_S;
  st.fd = true; st.fa = st.act; st.fb = G.slot;
  st.fs = st.n + (S[2] - st.hp[2]) + S[2]; st.fe = st.fs + CNV_FADE;
  return .1;
}
/* RBJ cookbook biquad (R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad filter coefficients") into C[o…o+4]:
   t 0 low-pass, 1 high-pass, 2 peak (g dB). */
function cnvBq(C, o, t, f, q, g, sr){
  const w = 2 * Math.PI * clamp(f, 10, sr * .45) / sr, cs = Math.cos(w), al = Math.sin(w) / (2 * q), A = Math.pow(10, g / 40);
  let b0, b1, b2, a0, a1, a2;
  if (t === 0){ b1 = 1 - cs; b0 = b2 = b1 / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; }
  else if (t === 1){ b1 = -(1 + cs); b0 = b2 = (1 + cs) / 2; a0 = 1 + al; a1 = -2 * cs; a2 = 1 - al; }
  else { b0 = 1 + al * A; b1 = -2 * cs; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * cs; a2 = 1 - al / A; }
  C[o] = b0 / a0; C[o + 1] = b1 / a0; C[o + 2] = b2 / a0; C[o + 3] = a1 / a0; C[o + 4] = a2 / a0;
}
// power gain of two one-pole low-passes in a row (pole a) on white noise: (1−a)⁴(1+a²)/(1−a²)³
const cnvPg2 = a => { const b = 1 - a * a; return Math.pow(1 - a, 4) * (1 + a * a) / (b * b * b); };
/* Works out the IR: its length, the early reflections (written now), and everything the tail needs. */
function cnvSetup(st){
  const G = st.G, sr = st.sr, cap = st.cap, kind = CNV_KIND[G.type], Z = st.sl[G.slot], R = CNV_ROOM[Math.min(G.type, 9)];
  const sc = Math.pow(2, 2 * G.size - 1), tone = G.tone, early = G.early, RT = G.decay, pre = Math.round(G.pre * sr);
  G.kind = kind; G.n = 0; G.E[0] = G.E[1] = 0; G.ready = 0; G.s = 0; G.p = 0; G.c = 0; G.sub = 0; G.pre0 = pre;
  for (let c = 0; c < 2; c++){ const y = G.sy[c]; y.r = ((G.type + 1) * 7919 + c * 104729 + 12345) >>> 0 || 1; y.l1 = y.l2 = y.l3 = y.lp = y.hp = y.hx = 0; y.d.fill(0); y.e.fill(0); y.bq.fill(0); y.dl.fill(0); y.w = 0; y.ap.fill(0); y.sl = 0; }
  let body = 0, Eest = 1;
  const er = kind === 0 || kind === 3 || ((kind === 5 || kind === 6) && early > 0);
  // ── the tail's shape (rooms, plate, gated, reverse; and the little room around a cab or phone)
  if (kind !== 2 && kind !== 7){
    // (Cab and Telephone: the room around the mic rings a tenth of Decay, 30 … 600 ms)
    const small = kind === 5 || kind === 6, trt = small ? clamp(RT * .1, .03, .6) : kind === 3 ? Math.max(1, 6 * clamp(RT, .1, 1.5)) : kind === 4 ? clamp(RT, .1, 6) : RT;
    body = kind === 3 ? clamp(RT, .1, 1.5) : kind === 4 ? clamp(RT, .1, 6) : small ? (early > 0 ? Math.max(trt, .08) : .08) : RT * Math.max(1, R[8]);
    G.tr = trt; G.gate = kind === 3 ? Math.round(clamp(RT, .1, 1.5) * sr) : 0; G.cut = Math.round((kind === 3 ? clamp(.05 * body, .01, .03) : Math.max(.012, .08 * body)) * sr);   // Gated: a 10–30 ms close
    if (kind === 3) body += G.cut / sr;
    G.rho0 = small ? 3000 : R[7] / sc; G.tb = Math.max(.002, (small ? .01 : R[6]) * sc);
    G.kr = 1 - Math.exp(-1 / (sr * (kind === 1 ? .0005 + (1 - early) * .03 : kind === 4 ? .001 : G.tb * .7)));
    // Tone: the tail starts at a cutoff of 800 Hz (dull) … 20 kHz (bright), which halves every th seconds
    G.fc0 = small ? 6000 : 800 * Math.pow(10, tone * 1.4); G.th = trt * (small ? 1 : R[9]) * (.15 + .5 * tone);
    G.ab = R[8] > 1 && !small && kind !== 4 ? Math.min(.9, 1.2 * (R[8] - 1)) : 0;   // the longer bass (not in Reverse)
    G.qd = Math.exp(-6.91 / (trt * sr)); G.qb = Math.exp(-6.91 / (trt * Math.max(1, R[8]) * sr));
    G.kh = R[10] > 0 ? 1 - Math.exp(-2 * Math.PI * R[10] / Math.sqrt(sc) / sr) : 0;
    G.tail0 = kind === 0 || kind === 3 ? Math.round(Math.min(R[0], R[1], R[2]) * sc * .5 / 343 * sr) : 0;   // it starts near the first reflection
    // its energy (unit-variance noise, through the damping filter, times the envelope), summed every 128th sample
    const n = Math.min(Math.round(6 * sr), Math.round(body * sr)), q = 128, ma = Math.pow(G.qd, 2 * q), mb = Math.pow(G.qb, 2 * q), mr = Math.pow(1 - G.kr, q);
    let Et = 0, ea = kind === 4 ? Math.pow(G.qd, 2 * n) : 1, eb = 1, rr = 1;
    for (let i = 0; i < n; i += q){
      const ts = i / sr, fc = clamp(kind === 4 ? G.fc0 * Math.pow(2, -(n / sr - ts) / G.th) : G.fc0 * Math.pow(2, -ts / G.th), 150, sr * .45);
      const rp = 1 - rr, fd = cnvFade(G, i, n), a = (kind === 4 ? ea : rp * rp * ea) * cnvPg2(Math.exp(-2 * Math.PI * fc / sr)), b = G.ab * G.ab * rp * rp * eb;
      Et += (a + b) * fd * fd * q;
      ea = kind === 4 ? ea / ma : ea * ma; eb *= mb; rr *= mr;
    }
    G.et = Et;
  }
  if (kind === 2){
    // Spring: a loop of Length with 18 stretched all-passes (the chirp), damping (Tone) and drip (Early)
    body = Math.min(6, RT);
    G.ls = (.028 + G.size * .05) * sr; G.g = Math.pow(10, -3 * G.ls / (RT * sr));
    G.kt = 1 - Math.exp(-2 * Math.PI * (1500 * Math.pow(6, tone)) / sr); G.kh = 1 - Math.exp(-2 * Math.PI * 140 / sr); G.kp = 1 - Math.exp(-2 * Math.PI * 900 / sr);
    G.drip = early * 3;
    // its energy: the click (with drip) through the loop, averaged over the loop's comb, 1/(1 − g²|loop|²) at each
    // frequency (the all-passes don't change the level), over 256 frequencies
    { const ap = 1 - G.kp, at = 1 - G.kt, ah = 1 - G.kh, g2 = G.g * G.g; let e = 0;
      for (let j = 0; j < 256; j++){
        const w = Math.PI * (j + .5) / 256, cw = Math.cos(w), sw = Math.sin(w), hp = 2 * (1 - cw);
        // drip: X = 1 + drip · ap(1 − z⁻¹)/(1 − ap·z⁻¹)
        const dp = 1 - 2 * ap * cw + ap * ap, xr = 1 + G.drip * ap * ((1 - cw) * (1 - ap * cw) + ap * sw * sw) / dp, xi = G.drip * ap * sw * (1 - ap) / dp;
        const lp = G.kt * G.kt / (1 - 2 * at * cw + at * at), hh = ah * ah * hp / (1 - 2 * ah * cw + ah * ah);
        e += .25 * (xr * xr + xi * xi) / Math.max(.001, 1 - g2 * lp * hh);
      }
      Eest = e / 256; }
  }
  if (kind === 7){
    // Song IR: resampled (cubic), stretched by Size, cut to Decay, darker or brighter with Tone, Early the first 50 ms
    const o = G.obj, L = o && o.L, ok = L && L.length > 1 && o.sr > 1000 && o.sr < 400000;
    G.srcL = ok ? L : null; G.srcR = ok ? (o.R && o.R.length > 1 ? o.R : L) : null;
    G.step = ok ? o.sr / sr / sc : 1; G.srcN = ok ? Math.min(L.length, G.srcR.length) : 0;
    body = ok ? Math.min((G.srcN - 1) / G.step / sr, RT, 6) : 0;
    G.ka = G.step > 1.02 ? 1 : 0;
    if (G.ka) cnvBq(G.C, 0, 0, .42 * o.sr / G.step, .7, 0, o.sr);
    { const k0 = 1 - Math.exp(-2 * Math.PI * 1500 / sr), q = Math.min(1, 2 * tone); G.kt = k0 + (1 - k0) * q * q; }   // dark: down to 1.5 kHz
    G.hg = tone > .5 ? (tone - .5) * 2 : 0; G.kx = 1 - Math.exp(-2 * Math.PI * 3000 / sr);
    G.ge = early < .5 ? dbLin(-24 * (1 - 2 * early)) : 1 + (early - .5) * 2;
    for (const y of G.sy){ y.sl = 0; y.D = 0; y.d.fill(0); y.e.fill(0); }
    // its energy, from at most 8192 of the samples it will use (the level is measured properly once it is made)
    let e = 0; const nU = ok ? Math.min(G.srcN, Math.ceil(body * sr * G.step) + 1) : 0, q = Math.max(1, Math.ceil(nU / 8192));
    for (let i = 0; i < nU; i += q){ const a = L[i], b = G.srcR[i]; if (a === a && b === b) e += (a * a + b * b) * q; }
    Eest = e / Math.max(1e-9, G.step) / 2 || 1;
  }
  if (kind === 5 || kind === 6){
    // the filter: a speaker cab (low thump, presence, cone break-up, steep top) or a telephone line (band-pass, honk)
    const C = G.C;
    if (kind === 5){
      const r = Math.sqrt(sc);
      cnvBq(C, 0, 1, 70 / r, .8, 0, sr); cnvBq(C, 5, 2, 110 / sc, 1.3, 4, sr); cnvBq(C, 10, 2, (1800 + 1400 * tone) / r, 1.2, 3 + 3 * tone, sr);
      cnvBq(C, 15, 2, 4200 / r, 2, -6, sr); cnvBq(C, 20, 0, (2600 + 4400 * tone) / Math.pow(sc, .3), .9, 0, sr); cnvBq(C, 25, 0, (2600 + 4400 * tone) / Math.pow(sc, .3), .6, 0, sr);
    } else {
      const lo = 450 / sc, hi = 3400 * Math.pow(sc, .6);
      cnvBq(C, 0, 1, lo, .7, 0, sr); cnvBq(C, 5, 1, lo, 1, 0, sr); cnvBq(C, 10, 2, 1100, 1.5, 3 + 6 * tone, sr);
      cnvBq(C, 15, 2, 2500, 1.2, 2 * tone, sr); cnvBq(C, 20, 0, hi, .7, 0, sr); cnvBq(C, 25, 0, hi, 1, 0, sr);
    }
    // the filter's own energy (its response to a click), and its gain on the room sound, which is darker than
    // white noise (measured on 4096 samples of noise through two one-pole low-passes at 3 kHz)
    const z = G.sy[0].bq; let ef = 0, ei = 0, eo = 0, l1 = 0, l2 = 0, r = 0x1234567;
    for (let i = 0; i < 4096; i++){ const y = cnvCascade(C, z, i === 0 ? 1 : 0); ef += y * y; }
    z.fill(0);
    const kl = 1 - Math.exp(-2 * Math.PI * 3000 / sr);
    for (let i = 0; i < 4096; i++){
      r ^= r << 13; r ^= r >>> 17; r ^= r << 5; l1 += (((r >>> 0) * 2.3283064365386963e-10 - .5) - l1) * kl; l2 += (l1 - l2) * kl;
      const y = cnvCascade(C, z, l2); if (i >= 1024){ ei += l2 * l2; eo += y * y; }
    }
    z.fill(0); G.ef = ef; G.rm = .4 * early;
    // the click, plus the room (its tail rm²·et and its reflections, 1.5 times that) through the filter
    Eest = ef + (eo / Math.max(1e-20, ei)) * G.rm * G.rm * G.et * 2.5;
  }
  G.len = Math.min(cap, pre + Math.max(0, Math.min(Math.round(6 * sr), Math.round(body * sr))));
  // the early reflections (written in cnvEarly, one slice of mirror images at a time): a shoebox room's mirror
  // images (Allen & Berkley), two microphones a little apart
  G.er = er && G.len > pre;
  if (G.er){
    const Lx = R[0] * sc, Ly = R[1] * sc, Lz = R[2] * sc, g = G.geo;
    g[0] = Lx; g[1] = Ly; g[2] = Lz; g[3] = .27 * Lx; g[4] = .41 * Ly; g[5] = .36 * Lz; g[6] = .68 * Lx; g[7] = .57 * Ly; g[8] = .45 * Lz; g[9] = .1 + .08 * sc;
    g[10] = Math.hypot(g[6] - g[3], g[7] - g[4], g[8] - g[5]); g[11] = R[3];
    G.ord = R[4]; G.erEnd = pre; G.erR = (G.type * 7919 + 77) >>> 0 || 1;
  }
  G.eest = Eest;
  if (!G.er) cnvLevel(st, kind !== 2 && kind !== 5 && kind !== 6 && kind !== 7 ? G.et : Eest);
}
/* The first IR plays while it is made: start it at the level its energy will have. */
function cnvLevel(st, E){
  const G = st.G, Z = st.sl[G.slot];
  if (!(E > 1e-20)) E = 1;
  if (G.init){ Z.g = Z.gt = Math.sqrt(CNV_E / E); Z.empty = G.len <= G.pre0; }
}
/* One slice (mirror images with x index nx) of the early reflections, each bounce a little duller. */
function cnvEarly(st, nx){
  const G = st.G, sr = st.sr, g = G.geo, ord = G.ord, pre = G.pre0, tone = G.tone;
  const Lx = g[0], Ly = g[1], Lz = g[2], sx = g[3], sy = g[4], sz = g[5], mx = g[6], my = g[7], mz = g[8], dm = g[9], d0 = g[10], beta = g[11];
  let r = G.erR, end = G.erEnd;
  for (let ny = -ord; ny <= ord; ny++) for (let nz = -ord; nz <= ord; nz++){
    const k = Math.abs(nx) + Math.abs(ny) + Math.abs(nz); if (!k || k > ord) continue;
    const ix = nx * Lx + (nx & 1 ? Lx - sx : sx), iy = ny * Ly + (ny & 1 ? Ly - sy : sy), iz = nz * Lz + (nz & 1 ? Lz - sz : sz);
    const fc = Math.min(sr * .45, (1200 + 10000 * tone) * Math.pow(.75, k - 1)), a = Math.exp(-2 * Math.PI * fc / sr);
    for (let c = 0; c < 2; c++){
      r ^= r << 13; r ^= r >>> 17; r ^= r << 5; const jit = ((r >>> 0) * 2.3283064365386963e-10 - .5) * .0006 * k;
      const d = Math.hypot(ix - mx, iy - (my + (c ? dm : -dm)), iz - mz), t = (d - d0) / 343 + jit;
      if (t < 0) continue;
      const pos = pre + t * sr, i0 = Math.floor(pos), fr = pos - i0, gn = Math.pow(beta, k) * d0 / d * (1 - a), A = G.ir[c];
      let v = gn;
      for (let j = 0; j < 160 && i0 + j + 1 < G.len; j++){ A[i0 + j] += v * (1 - fr); A[i0 + j + 1] += v * fr; v *= a; if (v < gn * 1e-3) break; }
      if (i0 + 162 > end) end = Math.min(G.len, i0 + 162);
    }
  }
  G.erR = r; G.erEnd = end;
}
/* Scale the reflections to their share of the energy (Early: up to twice it; Cab and Telephone: the room's level). */
function cnvEarlyScale(st){
  const G = st.G, pre = G.pre0, end = G.erEnd, kind = G.kind, R = CNV_ROOM[Math.min(G.type, 9)], early = G.early;
  let e = 0; for (let c = 0; c < 2; c++){ const A = G.ir[c]; for (let i = pre; i < end; i++) e += A[i] * A[i]; }
  e *= .5;
  const small = kind === 5 || kind === 6, share = R[5], want = small ? G.rm * G.rm * G.et * share / (1 - share) : G.et * share / (1 - share) * (2 * early) * (2 * early);
  const g = e > 1e-20 ? Math.sqrt(want / e) : 0;
  for (let c = 0; c < 2; c++){ const A = G.ir[c]; for (let i = pre; i < end; i++) A[i] *= g; }
  cnvLevel(st, small ? G.eest : G.et + want);
}
/* the fade at the end of the IR (and the gate's cut) */
function cnvFade(G, i, n){
  if (G.kind === 3){ const j = i - G.gate; return j <= 0 ? 1 : j >= G.cut ? 0 : .5 + .5 * Math.cos(Math.PI * j / G.cut); }
  if (G.kind === 4){ const j = n - i, f = Math.round((.002 + .04 * (1 - G.early)) * G.sr); return j >= f ? 1 : j <= 0 ? 0 : j / f; }   // Early: how sharply the swell stops
  const f = Math.max(64, Math.round(n * .08)), j = i - (n - f);
  return j <= 0 ? 1 : j >= f ? 0 : .5 + .5 * Math.cos(Math.PI * j / f);
}
/* a sample through the six biquads (transposed direct form II; z holds two states each) */
function cnvCascade(C, z, x){
  for (let b = 0, o = 0, q = 0; b < 6; b++, o += 5, q += 2){
    const y = C[o] * x + z[q]; z[q] = C[o + 1] * x - C[o + 3] * y + z[q + 1]; z[q + 1] = C[o + 2] * x - C[o + 4] * y; x = y;
  }
  return x;
}
/* Makes the next stretch of the IR (both sides), adding the tail to the reflections already written. */
function cnvSynth(st){
  const G = st.G, sr = st.sr, kind = G.kind, pre = G.pre0, n0 = G.n;
  const chunk = kind === 2 ? 256 : kind === 5 || kind === 6 ? 512 : 1024, n1 = Math.min(G.len, n0 + chunk), body = G.len - pre;
  for (let c = 0; c < 2; c++){
    const y = G.sy[c], A = G.ir[c]; let r = y.r, e = 0;
    if (kind === 2){
      // the spring: an impulse (with drip) into the loop
      // (two springs: the right one 13% longer and a little more dispersive, so the sides differ)
      const D = y.dl, N = D.length, ls = G.ls * (c ? 1.13 : 1), g = G.g, kt = G.kt, kh = G.kh, kp = G.kp, X2 = y.ap, a = c ? .6 : .55;
      for (let i = n0; i < n1; i++){
        let v = 0;
        if (i >= pre){
          const x = i === pre ? 1 : 0; y.l1 += (x - y.l1) * kp; const xin = x + (x - y.l1) * G.drip;
          const rd = y.w - ls, ri = Math.floor(rd), fr = rd - ri, d0 = D[((ri % N) + N) % N], d1 = D[(((ri + 1) % N) + N) % N], dl = d0 + (d1 - d0) * fr;
          y.lp += (dl - y.lp) * kt; y.hp += (y.lp - y.hp) * kh;
          v = xin * .5 + (y.lp - y.hp) * g;
          const k = y.sl & 1; y.sl++;
          for (let j = 0; j < 18; j++){ const o = j * 2 + k, w = a * v + X2[o] - a * X2[36 + o]; X2[o] = v; X2[36 + o] = w; v = w; }
          if (!(v > -8 && v < 8)){ v = 0; X2.fill(0); D.fill(0); y.lp = y.hp = 0; }
          D[y.w] = v; y.w = y.w + 1 === N ? 0 : y.w + 1;
          const fd = cnvFade(G, i - pre, body); v *= fd;
        }
        A[i] = v; e += v * v;
      }
    } else if (kind === 7){
      const src = c ? G.srcR : G.srcL, M = G.srcN, step = G.step, h = y.d, C = G.C, z = y.bq, nE = Math.round(.05 * sr), nX = Math.round(.01 * sr);
      for (let i = n0; i < n1; i++){
        let v = 0;
        if (i >= pre && src){
          const pos = (i - pre) * step, j = Math.floor(pos), fr = pos - j;
          // filter the source up to j + 2 (a low-pass first when it is being squeezed), keep the last eight
          while (y.D <= j + 2){ let x = y.D < M ? src[y.D] : 0; x = x === x ? clamp(x, -64, 64) : 0; if (G.ka) x = cnvCascade2(C, z, x); h[y.D & 3] = x; y.D++; }
          const p0 = j > 0 ? h[(j - 1) & 3] : 0, p1 = h[j & 3], p2 = h[(j + 1) & 3], p3 = h[(j + 2) & 3];
          // cubic Hermite
          v = p1 + .5 * fr * (p2 - p0 + fr * (2 * p0 - 5 * p1 + 4 * p2 - p3 + fr * (3 * (p1 - p2) + p3 - p0)));
          // Tone: darker (one pole) or a high shelf up to +6 dB
          y.lp += (v - y.lp) * G.kt; v = y.lp;
          if (G.hg > 0){ y.hx += (v - y.hx) * G.kx; v += (v - y.hx) * G.hg; }
          const t = i - pre; if (t < nE + nX) v *= t < nE ? G.ge : G.ge + (1 - G.ge) * (t - nE) / nX;
          v *= cnvFade(G, t, body);
        }
        A[i] = v; e += v * v;
      }
    } else {
      // rooms, plate, gated, reverse; and the room around a cab or phone: velvet noise (sparse at first, then dense)
      // through two one-pole low-passes whose cutoff falls as it decays (the highs die first)
      const small = kind === 5 || kind === 6, C = G.C, z = y.bq, t0 = pre + G.tail0, rho0 = G.rho0, tb = G.tb * sr, kr = G.kr, kh = G.kh;
      const qd = G.qd, qb = G.qb, ab = G.ab, ab2 = 1 - Math.exp(-2 * Math.PI * 220 / sr), nb2 = 1 / Math.sqrt(cnvPg2(1 - ab2)), rm = small ? G.rm : 1;
      let l1 = y.l1, l2 = y.l2, b1 = y.e[0], b2 = y.e[1], rp = y.e[2], dd = y.e[3], db = y.d[0], hx = y.hx, cs = y.d[1], ce = y.d[2], vp = y.d[3], amp = y.l3, a = y.lp, nm = y.hp;
      if (n0 === 0){ dd = kind === 4 ? Math.exp(-6.91) : 1; db = 1; rp = 0; cs = 0; ce = 0; vp = -1; }
      for (let i = n0; i < n1; i++){
        let v = 0;
        const t = i - t0;
        if (t >= 0 && t < body){
          if ((t & 31) === 0){
            // the cutoff for the next 32 samples
            const ts = t / sr, fc = clamp(kind === 4 ? G.fc0 * Math.pow(2, -(body / sr - ts) / G.th) : G.fc0 * Math.pow(2, -ts / G.th), 150, sr * .45);
            a = Math.exp(-2 * Math.PI * fc / sr); nm = 1;   // not renormalised: what the filter takes away is gone
          }
          let x = 0;
          if (t >= ce){
            const ts = t / tb, rho = Math.min(6000, rho0 * (1 + ts) * (1 + ts)), Td = Math.max(2, sr / rho);
            cs = ce; ce = cs + Td; r ^= r << 13; r ^= r >>> 17; r ^= r << 5; vp = Math.floor(cs + (r >>> 0) * 2.3283064365386963e-10 * Td);
            amp = Math.sqrt(Td) * (r & 1 ? 1 : -1);
          }
          if (t === vp) x = amp;
          l1 += (x - l1) * (1 - a); l2 += (l1 - l2) * (1 - a);
          rp += (1 - rp) * kr;
          let w = kind === 4 ? l2 * nm * dd : l2 * nm * rp * dd;
          if (ab > 0){ r ^= r << 13; r ^= r >>> 17; r ^= r << 5; const wn = ((r >>> 0) * 2.3283064365386963e-10 * 2 - 1) * 1.7320508; b1 += (wn - b1) * ab2; b2 += (b1 - b2) * ab2; w += b2 * nb2 * ab * rp * db; }
          dd *= kind === 4 ? 1 / qd : qd; db *= qb;
          if (dd < 1e-30) dd = 0; if (db < 1e-30) db = 0;
          v = w * cnvFade(G, i - pre, body) * rm;
        }
        v += A[i];
        if (kh > 0){ hx += (v - hx) * kh; v -= hx; }
        if (small) v = cnvCascade(C, z, v + (i === pre ? 1 : 0));
        A[i] = v; e += v * v;
      }
      y.l1 = l1; y.l2 = l2; y.e[0] = b1; y.e[1] = b2; y.e[2] = rp; y.e[3] = dd; y.d[0] = db; y.hx = hx; y.d[1] = cs; y.d[2] = ce; y.d[3] = vp; y.l3 = amp; y.lp = a; y.hp = nm;
    }
    y.r = r; G.E[c] += e;
  }
  G.n = n1;
  return kind === 2 ? 3 : kind === 7 ? 1.5 : kind === 5 || kind === 6 ? 1.2 : 2;
}
/* two biquads (the Song IR's anti-alias low-pass, the same section twice) */
function cnvCascade2(C, z, x){
  for (let q = 0; q < 4; q += 2){ const y = C[0] * x + z[q]; z[q] = C[1] * x - C[3] * y + z[q + 1]; z[q + 1] = C[2] * x - C[4] * y; x = y; }
  return x;
}


/* ── Pitch correct, Vowel filter, Rotary speaker, Granular delay, Reverse echo, Envelope filter (Oct 2026, original code) ── */
/* ── Pitch correct, vowel filter, rotary speaker, granular delay, reverse echo and envelope filter
   (added Oct 2026, all original code) ──────────────────────────────────────────────────────────────────────────── */

/* 4-point Hermite (Catmull-Rom) interpolation between b and c at fraction f: cleaner than linear on swept delays. */
const fxHerm4 = (a, b, c, d, f) => { const c1 = .5 * (c - a), c2 = a - 2.5 * b + 2 * c - .5 * d, c3 = .5 * (d - a) + 1.5 * (b - c); return ((c3 * f + c2) * f + c1) * f + b; };
/* Reads buffer X (power-of-two length, mask m) at the fractional index pos. */
const fxHermAt = (X, m, pos) => { const i = Math.floor(pos), f = pos - i; return fxHerm4(X[(i - 1) & m], X[i & m], X[(i + 1) & m], X[(i + 2) & m], f); };
/* Output guard: untouched below ±2, rounds off into ±4 above, and a NaN becomes silence. */
const fxGuard = y => { const a = y < 0 ? -y : y; return a < 2 ? y : a < 1e9 ? (y < 0 ? -1 : 1) * (2 + 2 * Math.tanh((a - 2) * .5)) : 0; };
/* Feedback saturation: untouched below ±1.6, never past ±2 (soft() scaled up). */
const fxSat = v => 2 * soft(v * .5);
/* A sample that is about to be stored: NaN or absurd → 0, and tiny → 0 (no denormals on silence). */
const fxClean = v => (v > 1e-20 || v < -1e-20) && v > -64 && v < 64 ? v : 0;

/* Pitch correct: finds the pitch of a monophonic voice or instrument and pulls it to the nearest note of Key and Scale
   (or, with a cable in Note, to the pitch CV there, so a sequencer can "play" the voice). Speed is the retune time: 0 ms
   snaps hard (the robot effect), a few hundred ms only nudges. Humanize slows the retune once a note has been held, so
   vibrato and natural drift survive while the note still lands in tune. Transpose shifts the corrected voice by
   semitones. Unvoiced and quiet sound (breaths, consonants) is left uncorrected (only Transpose applies).
   Detection: YIN (de Cheveigné & Kawahara, JASA 2002) on a 4× decimated copy every block, refined at the full rate
   (squared difference around the found lag, parabolic interpolation). Shifting: pitch-synchronous splicing. The voice
   is read through a short delay that drifts at the new speed; when the read point runs out of room it jumps by a whole
   number of detected periods (so the waveform lines up, as in TD-PSOLA) and crossfades over about one period.
   Latency is a few ms plus up to one period (≤ ~20 ms at 65 Hz). Outputs the detected pitch (CV, 0.5 = A4) and a
   voiced gate. */
const PC_SCALES = [[0,1,2,3,4,5,6,7,8,9,10,11], [0,2,4,5,7,9,11], [0,2,3,5,7,8,10], [0,2,3,5,7,8,11], [0,2,4,7,9], [0,3,5,7,10], [0,3,5,6,7,10], [0,2,3,5,7,9,10], [0,2,4,5,7,9,10]];
const PC_MASK = PC_SCALES.map(s => s.reduce((m, k) => m | (1 << k), 0));
const PC_N = 8192, PC_W = 200, PC_WF = 384, PC_FMIN = 65, PC_FMAX = 1100, PC_THR = .15, PC_GATE = .006;
const pcIn = (mask, n, key) => (mask >> ((((n - key) % 12) + 12) % 12)) & 1;
function pcNearest(m, key, mask){
  const c = Math.round(m); let best = c, bd = 1e9;
  for (let n = c - 6; n <= c + 6; n++){ if (!pcIn(mask, n, key)) continue; const d = Math.abs(n - m); if (d < bd){ bd = d; best = n; } }
  return best;
}
/* The period of the latest audio in full-rate samples (fractional), or 0 when it isn't clearly pitched. */
function pcDetect(st, sr, now){
  const D = st.D, rs = sr / D, W = PC_W, lin = st.lin, y = st.yd, db = st.db;
  const tMin = Math.max(2, Math.floor(rs / PC_FMAX)), tMax = Math.min(lin.length - W - 2, y.length - 2, Math.ceil(rs / PC_FMIN));
  const n = W + tMax + 2; if (st.dn < n) return 0;
  for (let j = 0, o = st.dw - n; j < n; j++) lin[j] = db[(o + j) & 1023];
  // YIN: the cumulative-mean-normalised difference; the first dip under the threshold, else the deepest dip if it's good enough
  let run = 0, best = -1, gmin = 2, gt = -1; y[0] = 1;
  for (let t = 1; t <= tMax; t++){
    let s = 0; for (let j = 0; j < W; j++){ const q = lin[j] - lin[j + t]; s += q * q; }
    run += s; const v = run > 1e-20 ? s * t / run : 1; y[t] = v;
    if (t > tMin + 1){
      const pv = y[t - 1];
      if (v >= pv && pv <= y[t - 2]){ if (pv < PC_THR){ best = t - 1; break; } if (pv < gmin){ gmin = pv; gt = t - 1; } }
    }
  }
  if (best < 0){ if (gmin < .35) best = gt; else return 0; }
  const a = y[best - 1], m = y[best], z = y[best + 1], den = a - 2 * m + z;
  const tau = (best + (den > 1e-12 ? .5 * (a - z) / den : 0)) * D;
  // refine at the full rate: the squared difference at ±4 samples around the coarse lag
  const t0 = Math.round(tau), X = st.m, M = PC_N - 1, Wf = PC_WF, rf = st.rf, s0 = now - Wf - t0 - 6;
  let bq = -1, bv = Infinity;
  for (let q = 0; q < 9; q++){
    const t = t0 - 4 + q; let s = 0;
    for (let j = 0; j < Wf; j++){ const e = X[(s0 + j) & M] - X[(s0 + j + t) & M]; s += e * e; }
    rf[q] = s; if (s < bv){ bv = s; bq = q; }
  }
  if (bq <= 0 || bq >= 8) return tau;
  const A = rf[bq - 1], C = rf[bq + 1], d2 = A - 2 * bv + C;
  return t0 - 4 + bq + (d2 > 1e-20 ? .5 * (A - C) / d2 : 0);
}
const PitchCorrect = {
  type:'autotune', scope:'mono', cost:.5,
  inputs:{ in:'audio', inR:'audio', note:'cv' }, outputs:{ out:'audio', outR:'audio', pitch:'cv', gate:'gate' },
  params:{ key:[0,0,11,1], scale:[0,0,8,1], speed:[.025,0,.4,.001], humanize:[0,0,1,.01], trans:[0,-12,12,1], mix:[1,0,1,.01] },
  create(sr){
    const D = Math.max(1, Math.round(sr / 12000));
    return { D, c:1 - Math.exp(-2 * Math.PI / (5 * D)), bL:new Float32Array(PC_N), bR:new Float32Array(PC_N), m:new Float32Array(PC_N), w:0,
      db:new Float32Array(1024), dw:0, dn:0, dec:0, lp1:0, lp2:0, lin:new Float32Array(512), yd:new Float32Array(320), rf:new Float64Array(9),
      env:0, k:0, voiced:0, miss:9, T:0, midi:69, note:-1, tgt:-99, held:0, corr:0, tr:0, cv:.5, d:-1, od:0, xf:1, xs:.01, pw:0 };
  },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR;
    const M = PC_N - 1, bL = st.bL, bR = st.bR, X = st.m, w = st.w, c = st.c;
    // 1. store the block (left, right, mono) and the low-passed, decimated mono the detector uses
    let pk = 0;
    for (let i = 0; i < B; i++){
      let l = L[i], r = R[i]; if (!(l > -8 && l < 8)) l = 0; if (!(r > -8 && r < 8)) r = 0;
      const x = (l + r) * .5, j = (w + i) & M; bL[j] = l; bR[j] = r; X[j] = x; oL[i] = l; oR[i] = r;   // the dry sound, cleaned
      st.lp1 += (x - st.lp1) * c; st.lp2 += (st.lp1 - st.lp2) * c;
      if (++st.dec >= st.D){ st.dec = 0; st.db[st.dw] = st.lp2; st.dw = (st.dw + 1) & 1023; if (st.dn < 1024) st.dn++; }
      const a = x < 0 ? -x : x; if (a > pk) pk = a;
    }
    if (st.lp1 < 1e-15 && st.lp1 > -1e-15) st.lp1 = 0; if (st.lp2 < 1e-15 && st.lp2 > -1e-15) st.lp2 = 0;
    st.env += (pk - st.env) * (pk > st.env ? .6 : .08);
    // 2. detect (every block; every other block when the engine is shedding load)
    const ran = ctx.quality !== 'draft' || (st.k ^= 1);
    if (ran){
      const T = st.env > PC_GATE ? pcDetect(st, sr, w + B) : 0;
      if (T > 0){ st.voiced = 1; st.miss = 0; st.T = T; st.midi = 69 + 12 * Math.log2(sr / T / 440); }
      else if (++st.miss > 2) st.voiced = 0;   // a couple of missed reads don't drop the note
    }
    // 3. the correction (semitones), smoothed by Speed (and Humanize on held notes)
    const kB = B / sr;
    if (st.voiced){
      let tgt;
      if (linked(io, 'note')) tgt = 69 + (io.in.note[B - 1] - .5) * 96;
      else {
        const key = clamp(Math.round(p.key), 0, 11), mask = PC_MASK[clamp(Math.round(p.scale), 0, PC_MASK.length - 1)];
        const n = pcNearest(st.midi, key, mask), cur = st.note;
        // a little hysteresis: stay on the current note until another is clearly nearer
        if (cur < 0 || !pcIn(mask, cur, key) || (n !== cur && Math.abs(st.midi - cur) > Math.abs(st.midi - n) + .2)) st.note = n;
        tgt = st.note;
      }
      if (Math.abs(tgt - st.tgt) > .5) st.held = 0; else st.held += kB;
      st.tgt = tgt;
      const tau = Math.max(0, p.speed) + clamp((st.held - .08) / .2, 0, 1) * u(p.humanize) * .3;
      st.corr += (clamp(tgt - st.midi, -24, 24) - st.corr) * (tau < 5e-4 ? 1 : 1 - Math.exp(-kB / tau));
    } else { st.note = -1; st.tgt = -99; st.held = 0; st.corr -= st.corr * (1 - Math.exp(-kB / .03)); }
    st.tr += (clamp(p.trans, -12, 12) - st.tr) * (1 - Math.exp(-kB / .02));
    if (st.corr < 1e-9 && st.corr > -1e-9) st.corr = 0;
    const r = Math.pow(2, clamp(st.corr + st.tr, -24, 24) / 12), dr = 1 - r, ar = dr < 0 ? -dr : dr;
    // 4. the splicing delay: jump by whole periods (Tj) when the read point leaves [lo, hi], crossfading over F samples
    const voiced = st.voiced && st.T > 0, Tj = voiced ? st.T : Math.round(.012 * sr);
    let F = clamp(Tj, .0015 * sr, .01 * sr);
    if (ar > 1e-6) F = Math.min(F, .9 * Tj / ar);
    F = Math.max(8, Math.floor(F));
    const lo = 4 + r + F * Math.max(0, r - 1), hi = lo + Tj, still = !voiced && ar < 1e-4, mx = u(p.mix);
    if (st.d < 0) st.d = lo + Tj * .5;
    for (let i = 0; i < B; i++){
      const now = w + i;
      if (st.xf >= 1){
        let nd = st.d;
        if (still){ if (nd < lo || nd > .03 * sr) nd = lo + Tj * .5; }
        else if (nd < lo) nd += Math.ceil((lo - nd) / Tj) * Tj;
        else if (nd > hi) nd -= Math.ceil((nd - hi) / Tj) * Tj;
        if (nd !== st.d){ st.od = st.d; st.d = nd; st.xf = 0; st.xs = 1 / F; st.pw = voiced ? 0 : 1; }
      }
      let yl = fxHermAt(bL, M, now - st.d), yr = fxHermAt(bR, M, now - st.d);
      if (st.xf < 1){
        const q = st.xf, ol = fxHermAt(bL, M, now - st.od), or = fxHermAt(bR, M, now - st.od);
        // lined-up waveforms crossfade at constant amplitude; unpitched sound at constant power
        let ga, gb;
        if (st.pw){ const e = q * q * (3 - 2 * q); ga = sinT(e * .25); gb = sinT(e * .25 + .25); } else { ga = .5 - .5 * sinT(q * .5 + .25); gb = 1 - ga; }
        yl = yl * ga + ol * gb; yr = yr * ga + or * gb;
        st.xf += st.xs; st.od += dr;
      }
      st.d += dr;
      if (!(st.d > 2 && st.d < PC_N - 64)){ st.d = lo + Tj * .5; st.xf = 1; }   // safety net, never expected
      const l = oL[i], rr = oR[i];
      oL[i] = fxGuard(l + (yl - l) * mx); oR[i] = fxGuard(rr + (yr - rr) * mx);
    }
    st.w = (w + B) & M;
    if (st.voiced) st.cv = clamp(.5 + (st.midi - 69) / 96, 0, 1);
    if (io.out.pitch) io.out.pitch.fill(st.cv);
    if (io.out.gate) io.out.gate.fill(st.voiced);
  }
};

/* Vowel filter: three resonant band-passes in parallel, set to the first three formants of a vowel, so anything put
   through it "says" A, E, I, O or U. Vowel morphs smoothly from one to the next (the Vowel input adds to it; ±1 sweeps
   the whole range). Voice moves the formants from a man's (0) through a woman's (1) to a child's (2); Shift moves them
   all up or down an octave beyond that. Resonance narrows the peaks. The LFO (Rate, Depth) moves the vowel by
   itself; Env moves it with the input's loudness (negative moves it the other way). Drive saturates the sound going
   in, which gives the formants more harmonics to work with. Positions past either end fold back, so sweeps keep moving.
   Formants: Peterson & Barney (1952), "Control methods used in a study of the vowels", JASA 24(2): average F1–F3 for
   men, women and children and the average formant levels; A = /ɑ/ (hod), E = /ɛ/ (head), I = /i/ (heed),
   O = /ɔ/ (hawed), U = /u/ (who'd). The levels are halved in dB, as the sound going in brings its own tilt.
   Bandwidths are typical values (≈80/100/140 Hz, widening with frequency). Filters: TPT state-variable. */
const VF_F = [
  [[730, 1090, 2440], [530, 1840, 2480], [270, 2290, 3010], [570, 840, 2410], [300, 870, 2240]],
  [[850, 1220, 2810], [610, 2330, 2990], [310, 2790, 3310], [590, 920, 2710], [370, 950, 2670]],
  [[1030, 1370, 3170], [690, 2610, 3570], [370, 3200, 3730], [680, 1060, 3180], [430, 1170, 3260]]];
const VF_DB = [[-1, -5, -28], [-2, -17, -24], [-4, -24, -28], [0, -7, -34], [-3, -19, -43]];
const VF_LF = VF_F.map(v => v.map(f => f.map(Math.log)));
const VF_BW = [80, 100, 140], VF_BREF = [500, 1500, 2500], VF_GAIN = 3.2;
const VowelFilter = {
  type:'vowel', scope:'mono', cost:.4,
  inputs:{ in:'audio', inR:'audio', vowel:'cv' }, outputs:{ out:'audio', outR:'audio' },
  params:{ vowel:[0,0,4,.01], voice:[0,0,2,.01], shift:[0,-1,1,.01], res:[.5,0,1,.01], rate:[.5,.05,10,.01], depth:[0,0,1,.01], env:[0,-1,1,.01], drive:[0,0,1,.01], mix:[1,0,1,.01] },
  create:() => ({ s:new Float64Array(12), a:new Float64Array(12), ph:0, env:0 }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, s = st.s, A = st.a;
    const cv = linked(io, 'vowel'), dr = .25 + u(p.drive) * 10, dn = 1 / Math.tanh(dr), mx = u(p.mix);
    const voice = clamp(p.voice, 0, 2), vi = Math.min(1, Math.floor(voice)), vf = voice - vi, sh = Math.pow(2, clamp(p.shift, -1, 1));
    const bw = Math.pow(2.5, 1 - 2 * u(p.res)), ea = 1 - Math.exp(-1 / (sr * .005)), er = 1 - Math.exp(-1 / (sr * .12));
    const lfoD = u(p.depth) * 2, envD = clamp(p.env, -1, 1) * 4, base = clamp(p.vowel, 0, 4), dph = clamp(p.rate, 0, 40) * 8 / sr;
    const F0 = VF_LF[vi], F1 = VF_LF[vi + 1];
    for (let i0 = 0; i0 < B; i0 += 8){
      // the vowel position, and from it the three filters, every 8 samples
      let q = base + lfoD * sinT(st.ph) + envD * Math.min(1, st.env * 2.5) + (cv ? io.in.vowel[i0] * 4 : 0);
      q %= 8; if (q < 0) q += 8; if (q > 4) q = 8 - q; if (!(q >= 0 && q <= 4)) q = 0;
      const k0 = Math.min(3, Math.floor(q)), kf = q - k0;
      for (let f = 0; f < 3; f++){
        const lf = (F0[k0][f] * (1 - kf) + F0[k0 + 1][f] * kf) * (1 - vf) + (F1[k0][f] * (1 - kf) + F1[k0 + 1][f] * kf) * vf;
        const fc = Math.min(sr * .45, Math.exp(lf) * sh), g = Math.tan(Math.PI * fc / sr);
        const k = clamp(VF_BW[f] * Math.sqrt(fc / VF_BREF[f]) * bw / fc, .02, 2), a1 = 1 / (1 + g * (g + k));
        A[f * 4] = a1; A[f * 4 + 1] = g * a1; A[f * 4 + 2] = g * g * a1;
        A[f * 4 + 3] = k * dbLin((VF_DB[k0][f] * (1 - kf) + VF_DB[k0 + 1][f] * kf) * .5) * VF_GAIN;   // k × v1 peaks at unity
      }
      st.ph += dph; if (st.ph >= 1) st.ph -= 1;
      const i1 = Math.min(B, i0 + 8);
      for (let i = i0; i < i1; i++){
        let l0 = L[i], r0 = R[i]; if (!(l0 > -8 && l0 < 8)) l0 = 0; if (!(r0 > -8 && r0 < 8)) r0 = 0;
        const m = (l0 + r0) * .5, a = m < 0 ? -m : m;
        st.env += (a - st.env) * (a > st.env ? ea : er);
        const xl = Math.tanh(l0 * dr) * dn, xr = Math.tanh(r0 * dr) * dn;
        let yl = 0, yr = 0;
        for (let f = 0, j = 0; f < 3; f++, j += 4){
          const a1 = A[j], a2 = A[j + 1], a3 = A[j + 2];
          let v3 = xl - s[j + 1], v1 = a1 * s[j] + a2 * v3, v2 = s[j + 1] + a2 * s[j] + a3 * v3;
          s[j] = 2 * v1 - s[j]; s[j + 1] = 2 * v2 - s[j + 1]; yl += v1 * A[j + 3];
          v3 = xr - s[j + 3]; v1 = a1 * s[j + 2] + a2 * v3; v2 = s[j + 3] + a2 * s[j + 2] + a3 * v3;
          s[j + 2] = 2 * v1 - s[j + 2]; s[j + 3] = 2 * v2 - s[j + 3]; yr += v1 * A[j + 3];
        }
        oL[i] = fxGuard(l0 + (yl - l0) * mx); oR[i] = fxGuard(r0 + (yr - r0) * mx);
      }
    }
    for (let j = 0; j < 12; j++){ const v = s[j]; if (!(v > -1e4 && v < 1e4) || (v < 1e-20 && v > -1e-20)) s[j] = 0; }
    if (!(st.env > 1e-20 && st.env < 1e4)) st.env = 0;
  }
};

/* Rotary speaker: the classic organ cabinet with a spinning horn and drum. The sound is split at 800 Hz; the horn
   (highs) and the drum (lows) each spin at their own speed: Slow ≈ 0.8 / 0.7 Hz, Fast ≈ 6.8 / 5.9 Hz, Stop brakes
   them. Each rotor gives three things at once: louder/softer as it faces a mic and turns away (amplitude), a pitch
   wobble as it moves towards and away (Doppler, from a short swinging delay: the horn's mouth travels ±15 cm), and a
   darker tone facing away. A duller reflection off the back of the cabinet swings the other way. Two virtual mics
   pick it up; Spread sets the angle between them (0 = mono, 1 = opposite sides). Accel is the rotors' inertia: the
   horn changes speed in about a second, the heavier drum takes four times as long. Drive is the tube preamp in front
   (asymmetric, so it growls), Balance trades horn against drum. A gate in Fast switches to fast while it's high.
   Modelled from the published descriptions of the cabinet's physics (e.g. Smith, Serafin, Abel & Berners, "Doppler
   simulation and the Leslie", DAFx 2002); the code is original. */
const RS_HORN = [0, .8, 6.8], RS_DRUM = [0, .7, 5.9], RS_HG = 1.15, RS_DG = 1.25;
const RotarySpk = {
  type:'rotary', scope:'mono', cost:.4,
  inputs:{ in:'audio', inR:'audio', fast:'gate' }, outputs:{ out:'audio', outR:'audio' },
  params:{ speed:[1,0,2,1], accel:[.5,0,1,.01], drive:[.2,0,1,.01], balance:[0,-1,1,.01], spread:[.8,0,1,.01], mix:[1,0,1,.01] },
  create(sr){
    const n = 1 << Math.ceil(Math.log2(sr * .006 + 16));
    return { h:new Float32Array(n), d:new Float32Array(n), m:n - 1, w:0, ph:0, pd:.37, vh:0, vd:0, go:0, c1:0, c2:0, dc:0, t:new Float64Array(4) };
  },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, H = st.h, Dm = st.d, m = st.m, T = st.t;
    let sp = clamp(Math.round(p.speed), 0, 2);
    if (linked(io, 'fast') && io.in.fast[B - 1] > .5) sp = 2;
    // inertia: the rotors ease towards their new speed, slowing down a little more lazily than they speed up
    const th = .08 + (1 - u(p.accel)) * .9, hT = RS_HORN[sp], dT = RS_DRUM[sp];
    if (!st.go){ st.go = 1; st.vh = hT; st.vd = dT; }   // the first block starts at the chosen speed
    st.vh += (hT - st.vh) * (1 - Math.exp(-B / (sr * th * (hT < st.vh ? 1.4 : 1))));
    st.vd += (dT - st.vd) * (1 - Math.exp(-B / (sr * th * 4 * (dT < st.vd ? 1.4 : 1))));
    if (st.vh < 1e-3) st.vh = 0; if (st.vd < 1e-3) st.vd = 0;
    // the preamp: a biased tanh (asymmetric, so even harmonics too), its gain set so the level stays about the same
    const dv = u(p.drive), dr = .3 + dv * 8, bias = dv * .3, tb = Math.tanh(bias), nr = 1 / ((Math.tanh(.5 * dr + bias) - Math.tanh(bias - .5 * dr)) * (1 + dv * 1.2));
    const g = Math.tan(Math.PI * Math.min(800, sr * .45) / sr), a1 = 1 / (1 + g * (g + 2)), a2 = g * a1, a3 = g * a2;
    const dA = .00044 * sr, d0 = dA + 3, dRef = .0011 * sr, eA = .00022 * sr, e0 = eA + 3;
    const kt = 1 - Math.exp(-2 * Math.PI * 2200 / sr), kdc = 1 - Math.exp(-2 * Math.PI * 20 / sr);
    const phi = u(p.spread) * .25, bal = clamp(p.balance, -1, 1), gh = Math.min(1, 1 + bal) * RS_HG, gd = Math.min(1, 1 - bal) * RS_DG, mx = u(p.mix);
    const ih = st.vh / sr, id = st.vd / sr;
    let w = st.w;
    for (let i = 0; i < B; i++){
      const l0 = L[i], r0 = R[i]; let x0 = (l0 + r0) * .5; if (!(x0 > -8 && x0 < 8)) x0 = 0;
      let x = (Math.tanh(x0 * dr + bias) - tb) * nr;
      st.dc += (x - st.dc) * kdc; x -= st.dc;   // the asymmetric preamp leaves an offset: a 20 Hz high-pass
      // crossover: a state-variable low-pass (Q 0.5) feeds the drum, the rest goes to the horn, so they always add back up
      const v3 = x - st.c2, v1 = a1 * st.c1 + a2 * v3, v2 = st.c2 + a2 * st.c1 + a3 * v3;
      st.c1 = 2 * v1 - st.c1; st.c2 = 2 * v2 - st.c2;
      H[w] = x - v2; Dm[w] = v2;
      st.ph += ih; if (st.ph >= 1) st.ph -= 1; st.pd += id; if (st.pd >= 1) st.pd -= 1;
      let yl = 0, yr = 0;
      for (let s = 0; s < 2; s++){
        const off = s ? phi : -phi;
        // horn: c = 1 facing this mic, −1 facing away; nearer is earlier, so the delay swings with it
        const c = sinT(st.ph - off + .25), b = .5 + .5 * c;
        let hn = fxHermAt(H, m, w - (d0 - dA * c));
        T[s] += (hn - T[s]) * kt; hn = (T[s] + (hn - T[s]) * (.25 + .75 * b)) * (.3 + .7 * b);
        // the reflection off the back panel: later, duller, and loudest when the horn faces away
        const rf = fxHermAt(H, m, w - (d0 + dRef + dA * c));
        T[s + 2] += (rf - T[s + 2]) * kt;
        const hr = T[s + 2] * (.3 + .7 * (1 - b)) * .35;
        // drum: a rotating baffle under the woofer, so a gentler swing and a smaller Doppler
        const cd = sinT(st.pd - off + .25), dm = fxHermAt(Dm, m, w - (e0 - eA * cd)) * (1 - .4 * (.5 - .5 * cd));
        const y = (hn + hr) * gh + dm * gd;
        if (s) yr = y; else yl = y;
      }
      w = (w + 1) & m;
      oL[i] = fxGuard(l0 + (yl - l0) * mx); oR[i] = fxGuard(r0 + (yr - r0) * mx);
    }
    st.w = w;
    st.c1 = fxClean(st.c1); st.c2 = fxClean(st.c2); st.dc = fxClean(st.dc); for (let j = 0; j < 4; j++) T[j] = fxClean(T[j]);
  }
};

/* Granular delay: a delay line read by many short grains (up to 32 at once). Density grains a second start Time
   back (plus a random Spray of up to half a second; Sync locks Time to a note value at the song tempo), each Size
   long with a smooth fade in and out, played at Pitch (±24 semitones) plus a random Jitter, reversed with the
   probability Reverse, and panned at random across Spread. Feedback sends the grains back into the delay line
   (up to 90% a pass, saturating, so it can't run away; with Pitch it climbs or falls with every pass).
   Freeze stops recording, so the grains keep picking over what's there. Grains are kept from crossing the record head,
   so nothing clicks. */
const GD_MAX = 32, GD_WN = 1024, GD_WIN = new Float32Array(GD_WN + 2);
for (let i = 0; i <= GD_WN + 1; i++){ const x = Math.min(1, i / GD_WN), e = Math.min(x, 1 - x) / .3; GD_WIN[i] = e >= 1 ? 1 : .5 - .5 * Math.cos(Math.PI * e); }   // Tukey: 30% raised-cosine fades
function gdSpawn(st, T, G, pitch, jit, spray, rev, spr, frz, N, sr){
  let k = -1; for (let g = 0; g < GD_MAX; g++) if (!st.on[g]){ k = g; break; }
  if (k < 0) return;
  const Rn = st.r, r = Math.pow(2, (pitch + (Rn() * 2 - 1) * jit * jit * 12) / 12), back = Rn() < rev, adv = frz ? 0 : 1;
  G = Math.min(G, N * .45 / (r + 1));   // very long, very high grains are shortened to fit the line
  // how far back it starts, kept so that the grain never runs into the record head (or off the far end)
  const lo = 4 + (back ? 0 : Math.max(0, G * (r - adv))), hi = N - 8 - (back ? G * (r + adv) : Math.max(0, G * (adv - r)));
  let D = T + Rn() * spray * .5 * sr; D = D < lo ? lo : D > hi ? hi : D;
  const a = ((Rn() * 2 - 1) * spr + 1) * Math.PI / 4;
  st.on[k] = 1; st.n++; st.gp[k] = st.w - D; st.gi[k] = back ? -r : r; st.ge[k] = 0; st.gd[k] = 1 / G;
  st.gl[k] = Math.cos(a) * Math.SQRT2; st.gr[k] = Math.sin(a) * Math.SQRT2;
}
const GrainDelay = {
  type:'gdelay', scope:'mono', cost:.45,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ time:[.3,.02,2,.001], sync:[0,0,1,1], div:[4,0,PP_DIVS.length - 1,1], size:[.12,.01,.5,.001], density:[10,1,60,.1], pitch:[0,-24,24,1], jitter:[0,0,1,.01],
    spray:[.15,0,1,.01], reverse:[0,0,1,.01], spread:[.5,0,1,.01], fb:[.3,0,1,.01], freeze:[0,0,1,1], mix:[.4,0,1,.01] },
  create(sr){
    const N = 1 << Math.ceil(Math.log2(sr * 4.5)), f = () => new Float64Array(GD_MAX);
    return { L:new Float32Array(N), R:new Float32Array(N), M:N - 1, w:0, sp:1, n:0, r:rng(nextSeed()),
      on:new Uint8Array(GD_MAX), gp:f(), gi:f(), ge:f(), gd:f(), gl:f(), gr:f() };
  },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, BL = st.L, BR = st.R, M = st.M, N = M + 1;
    const frz = p.freeze >= .5, cap = ctx.quality === 'draft' ? 12 : GD_MAX;
    let Ts = p.sync >= .5 ? 60 / Math.max((ctx.transport && ctx.transport.tempo) || 120, 20) * PP_DIVS[clamp(Math.round(p.div), 0, PP_DIVS.length - 1)][1] : p.time;
    const T = clamp(Ts, .005, 2.5) * sr, G = clamp(p.size, .005, .5) * sr, dens = clamp(p.density, .1, 100) / sr;
    const spray = u(p.spray), jit = u(p.jitter), rev = u(p.reverse), spr = u(p.spread), fb = u(p.fb) * .9, mx = u(p.mix), pitch = clamp(p.pitch, -24, 24);
    const on = st.on, gp = st.gp, gi = st.gi, ge = st.ge, gd = st.gd, gl = st.gl, gr = st.gr;
    for (let i = 0; i < B; i++){
      st.sp += dens;
      if (st.sp >= 1){ st.sp -= 1 + (st.r() - .5) * spray * .8; if (st.n < cap) gdSpawn(st, T, G, pitch, jit, spray, rev, spr, frz, N, sr); }
      let aL = 0, aR = 0, fL = 0, fR = 0, ws = 0;
      if (st.n > 0) for (let g = 0; g < GD_MAX; g++){
        if (!on[g]) continue;
        const e0 = ge[g] * GD_WN, ei = e0 | 0, e = GD_WIN[ei] + (GD_WIN[ei + 1] - GD_WIN[ei]) * (e0 - ei);
        const pos = gp[g], ip = Math.floor(pos), f = pos - ip, a = ip & M, b = (ip + 1) & M;
        const sl = (BL[a] + (BL[b] - BL[a]) * f) * e, sr2 = (BR[a] + (BR[b] - BR[a]) * f) * e;
        fL += sl; fR += sr2; aL += sl * gl[g]; aR += sr2 * gr[g]; ws += e;
        gp[g] = pos + gi[g]; ge[g] += gd[g];
        if (ge[g] >= 1){ on[g] = 0; st.n--; }
      }
      // the echoes are scaled by the overlap^0.7 (between "in step" and "random" grains); the feedback by the full
      // overlap, so its loop gain stays under Feedback whatever the grains do. The overlap is a smooth max(1, ws),
      // (1 + ws^6)^(1/6), so the gain has no corner where grains start to overlap
      const w2 = ws * ws, q6 = 1 + w2 * w2 * w2, nf = Math.pow(q6, -1 / 6), no = Math.pow(q6, -.7 / 6);
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      if (!frz){ BL[st.w] = fxClean(fxSat(xl + fL * nf * fb)); BR[st.w] = fxClean(fxSat(xr + fR * nf * fb)); st.w = (st.w + 1) & M; }
      oL[i] = xl * (1 - mx) + fxGuard(aL * no) * mx; oR[i] = xr * (1 - mx) + fxGuard(aR * no) * mx;
    }
  }
};

/* Reverse echo. Reverse delay (mode 0): the sound is recorded in chunks Time long (Sync: a note value at the song tempo,
   starting on the downbeat) and each chunk plays back backwards while the next records, so every note comes back
   reversed, swelling in. The joins crossfade (12 ms, an equal-power fade with no corners) so they don't click.
   Feedback sends the echoes round again (at most 90% a pass), and each pass is reversed again; Tone darkens them.
   Swell (mode 1): the input goes through a small dense reverb first and it's the reverb that's captured and reversed,
   so each sound is followed by a rising, sucked-in swell: Size sets the reverb (bigger and longer), Time the chunk;
   Feedback repeats the swells. The reverb is a 4-line feedback delay network with a Hadamard matrix and
   damping, after three Schroeder all-pass diffusers (Jot & Chaigne 1991; Schroeder 1962), written here from those
   descriptions. */
const RE_AP = [.0047, .0036, .00127], RE_FDN = [.0297, .0371, .0411, .0437], RE_SG = [1, -1, 1, -1], RE_VG = .45;
function reTank(st, x){
  for (let j = 0; j < 3; j++){ const b = st.ap[j], wi = st.aw[j], v = b[wi], y = v - .62 * x; b[wi] = fxClean(x + .62 * y); st.aw[j] = wi + 1 === b.length ? 0 : wi + 1; x = y; }
  const s = st.fs, dm = st.dm, kd = st.kd;
  for (let j = 0; j < 4; j++){
    const b = st.fd[j], n = b.length; let rp = st.fw[j] - st.fl[j]; if (rp < 0) rp += n;
    const i0 = rp | 0, f = rp - i0, i1 = i0 + 1 === n ? 0 : i0 + 1;
    dm[j] += (b[i0] + (b[i1] - b[i0]) * f - dm[j]) * kd; s[j] = dm[j];
  }
  const h0 = (s[0] + s[1] + s[2] + s[3]) * .5, h1 = (s[0] - s[1] + s[2] - s[3]) * .5, h2 = (s[0] + s[1] - s[2] - s[3]) * .5, h3 = (s[0] - s[1] - s[2] + s[3]) * .5;
  const fg = st.fg, h = st.h4; h[0] = h0; h[1] = h1; h[2] = h2; h[3] = h3;
  for (let j = 0; j < 4; j++){ const b = st.fd[j]; b[st.fw[j]] = fxClean(x * RE_SG[j] + fg[j] * h[j]); st.fw[j] = st.fw[j] + 1 === b.length ? 0 : st.fw[j] + 1; }
  st.ol = (s[0] + s[2] * .6) * RE_VG; st.or = (s[1] + s[3] * .6) * RE_VG;
}
const ReverseEcho = {
  type:'reverse', scope:'mono', cost:.3,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[0,0,1,1], time:[.4,.05,2,.001], sync:[0,0,1,1], div:[4,0,PP_DIVS.length - 1,1], fb:[.3,0,1,.01], size:[.5,0,1,.01], tone:[.6,0,1,.01], mix:[.5,0,1,.01] },
  create(sr){
    const N = 1 << Math.ceil(Math.log2(sr * 4.3));
    return { L:new Float32Array(N), R:new Float32Array(N), M:N - 1, w:0, k:0, T:0, F:1, a:0, b:0, aOn:0, bOn:0, x:0, lp:0, lpR:0, hp:0, hpR:0, sn:undefined,
      ap:RE_AP.map(s => new Float32Array(Math.max(2, Math.round(s * sr)))), aw:new Int32Array(3),
      fd:RE_FDN.map(s => new Float32Array(Math.ceil(s * 2 * sr) + 8)), fw:new Int32Array(4), fl:new Float64Array(4), fg:new Float64Array(4),
      fs:new Float64Array(4), dm:new Float64Array(4), h4:new Float64Array(4), kd:1, sz:-1, ol:0, or:0 };
  },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, BL = st.L, BR = st.R, M = st.M;
    const mode = p.mode >= .5 ? 1 : 0, sync = p.sync >= .5;
    if (ctx.transport && restarted(st, ctx) && sync) st.k = 0;   // synced chunks start on the downbeat
    const Ts = sync ? 60 / Math.max((ctx.transport && ctx.transport.tempo) || 120, 20) * PP_DIVS[clamp(Math.round(p.div), 0, PP_DIVS.length - 1)][1] : p.time;
    const Tn = Math.round(clamp(Ts, .02, (M + 1) / sr * .5 - .05) * sr);
    const fb = u(p.fb) * .9, mx = u(p.mix);
    const kl = 1 - Math.exp(-2 * Math.PI * Math.min(400 * Math.pow(40, u(p.tone)), sr * .45) / sr), kh = 1 - Math.exp(-2 * Math.PI * 40 / sr);
    if (mode){   // the reverb's size and decay glide, so turning Size doesn't click
      const z = u(p.size); st.sz = st.sz < 0 ? z : st.sz + (z - st.sz) * .1;
      const sc = .5 + 1.5 * st.sz, rt = .4 + st.sz * 4.6;
      for (let j = 0; j < 4; j++){ const len = RE_FDN[j] * sc * sr; st.fl[j] = Math.min(len, st.fd[j].length - 4); st.fg[j] = Math.pow(10, -3 * st.fl[j] / (rt * sr)); }
      st.kd = 1 - Math.exp(-2 * Math.PI * 5500 / sr);
    }
    for (let i = 0; i < B; i++){
      if (st.k <= 0){   // a new chunk: the head that was playing fades out, a new one starts from the newest sample backwards
        st.T = Tn; st.F = Math.max(16, Math.min(Math.round(.012 * sr), Tn >> 2));
        st.b = st.a; st.bOn = st.aOn; st.a = st.w - 1; st.aOn = 1; st.k = st.T; st.x = 0;
      }
      const ia = st.a & M; let yl = BL[ia], yr = BR[ia];
      if (st.x < st.F){
        const q = st.x / st.F, e = q * q * (3 - 2 * q), ga = sinT(e * .25), gb = st.bOn ? sinT(e * .25 + .25) : 0, ib = st.b & M;
        yl = yl * ga + BL[ib] * gb; yr = yr * ga + BR[ib] * gb; st.b--; st.x++;
      }
      st.a--; st.k--;
      // Tone (a low-pass) and a 40 Hz high-pass on the echoes and their feedback
      st.lp += (yl - st.lp) * kl; st.lpR += (yr - st.lpR) * kl; st.hp += (st.lp - st.hp) * kh; st.hpR += (st.lpR - st.hpR) * kh;
      yl = st.lp - st.hp; yr = st.lpR - st.hpR;
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      let wl, wr;
      // Swell: the reverb hears only the input; the repeats go round outside it (through a reverb they could build up
      // on its resonances), so the loop gain is never more than Feedback × Tone
      if (mode){ reTank(st, (xl + xr) * .5); wl = st.ol + yl * fb; wr = st.or + yr * fb; }
      else { wl = xl + yl * fb; wr = xr + yr * fb; }
      BL[st.w] = fxClean(fxSat(wl)); BR[st.w] = fxClean(fxSat(wr)); st.w = (st.w + 1) & M;
      oL[i] = xl * (1 - mx) + yl * mx; oR[i] = xr * (1 - mx) + yr * mx;
    }
    st.lp = fxClean(st.lp); st.lpR = fxClean(st.lpR); st.hp = fxClean(st.hp); st.hpR = fxClean(st.hpR);
    for (let j = 0; j < 4; j++) st.dm[j] = fxClean(st.dm[j]);
  }
};

/* Envelope filter (auto-wah, "touch" filter): a resonant state-variable filter (low-pass, band-pass or high-pass)
   whose cutoff follows the loudness: cutoff = Freq × 2^(env × Depth × Range). Positive Depth opens it as you play
   harder, negative sweeps it down (set Freq high for that). Sens is the gain into the follower, Attack and Release its
   speed. Source: Self listens to the input itself, Sidechain to the mixer's sidechain bus (falling back to the input
   when nothing is sent); a cable in SC always wins, so a kick can open the filter on a bass. Drive saturates the
   input; Resonance is gain-compensated so the peak doesn't jump out. The envelope (0…1) comes out at Env.
   Filter: TPT state-variable (Zavalishin, "The Art of VA Filter Design"). */
const EnvFilter = {
  type:'envfilt', scope:'mono', cost:.3,
  inputs:{ in:'audio', inR:'audio', sc:'audio' }, outputs:{ out:'audio', outR:'audio', env:'cv' },
  params:{ mode:[1,0,2,1], source:[0,0,1,1], freq:[.35,0,1,.001], depth:[.7,-1,1,.01], range:[4,1,8,.1], res:[.6,0,1,.01],
    attack:[.005,.0005,.2,.0001], release:[.15,.01,2,.001], sens:[.5,0,1,.01], drive:[0,0,1,.01], mix:[1,0,1,.01] },
  create:() => ({ e:0, l1:0, l2:0, r1:0, r2:0 }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, oE = io.out.env;
    const scP = linked(io, 'sc'), bus = !scP && p.source >= .5 && !!ctx.scL && typeof ctx.scOn === 'function' && !!ctx.scOn();
    const SL = bus ? ctx.scL : L, SR = bus ? (ctx.scR || ctx.scL) : R, SC = io.in.sc;
    const mode = clamp(Math.round(p.mode), 0, 2), base = 40 * Math.pow(400, u(p.freq)), oct = clamp(p.depth, -1, 1) * clamp(p.range, 0, 10) * Math.LN2;
    const k = 2 - 1.92 * u(p.res), comp = mode === 1 ? Math.pow(k, .6) * 1.32 : Math.pow(k / 2, .3);
    const ka = 1 - Math.exp(-1 / (sr * Math.max(p.attack, 1e-4))), kr = 1 - Math.exp(-1 / (sr * Math.max(p.release, 1e-3)));
    const sg = .25 * Math.pow(64, u(p.sens)), dr = .25 + u(p.drive) * 8, dn = 1 / Math.tanh(dr), mx = u(p.mix), fMax = sr * .45, pf = Math.PI / sr;
    let e = st.e, l1 = st.l1, l2 = st.l2, r1 = st.r1, r2 = st.r2;
    for (let i = 0; i < B; i++){
      let l0 = L[i], r0 = R[i]; if (!(l0 > -8 && l0 < 8)) l0 = 0; if (!(r0 > -8 && r0 < 8)) r0 = 0;
      const det = scP ? SC[i] : (SL[i] + SR[i]) * .5, a = (det < 0 ? -det : det) * sg;
      if (a < 64) e += (a - e) * (a > e ? ka : kr);
      const en = 1 - Math.exp(-2 * e), fc = clamp(base * Math.exp(en * oct), 10, fMax), g = Math.tan(pf * fc);
      const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2;
      const xl = Math.tanh(l0 * dr) * dn, xr = Math.tanh(r0 * dr) * dn;
      let v3 = xl - l2, v1 = a1 * l1 + a2 * v3, v2 = l2 + a2 * l1 + a3 * v3;
      l1 = 2 * v1 - l1; l2 = 2 * v2 - l2;
      let yl = mode === 0 ? v2 : mode === 1 ? v1 : xl - k * v1 - v2;
      v3 = xr - r2; v1 = a1 * r1 + a2 * v3; v2 = r2 + a2 * r1 + a3 * v3;
      r1 = 2 * v1 - r1; r2 = 2 * v2 - r2;
      let yr = mode === 0 ? v2 : mode === 1 ? v1 : xr - k * v1 - v2;
      oL[i] = l0 + (fxGuard(yl * comp) - l0) * mx; oR[i] = r0 + (fxGuard(yr * comp) - r0) * mx;
      if (oE) oE[i] = en;
    }
    if (!(l1 > -1e4 && l1 < 1e4 && l2 > -1e4 && l2 < 1e4 && r1 > -1e4 && r1 < 1e4 && r2 > -1e4 && r2 < 1e4)){ l1 = l2 = r1 = r2 = 0; }
    st.e = fxClean(e); st.l1 = fxClean(l1); st.l2 = fxClean(l2); st.r1 = fxClean(r1); st.r2 = fxClean(r2);
  }
};


/* ── Bytebeat and Chaos: the formula compiler and the chaotic systems (the same classes are in index.html's ENGINE block; keep them identical) ── */
/* ---------- Bytebeat formulas (original code) ----------
   A safe little compiler for one-line bytebeat formulas: the C-like expressions of viznut's 2011 "algorithmic
   symphonies from one line of code", where a sample counter t is run through integer maths and the low 8 bits are
   played as sound. Formulas arrive in song files other people share, so the text is NEVER run as code: it is split into
   tokens, parsed, folded and compiled here to a short bytecode that one small loop runs on a 32-bit integer stack. Only
   numbers, t, a, b, c, the operators below and a few functions exist; there are no loops, so every sample costs at most
   a few hundred steps. Integer maths like the C original: 32-bit wrap-around, / and % round toward zero and give 0 when
   dividing by 0, shift counts use their low 5 bits, comparisons, && || and ! give 1 or 0 (both sides are worked out:
   nothing has side effects).
   Operators, C precedence, lowest first:  ?:  ||  &&  |  ^  &  == !=  < <= > >=  << >> >>>  + -  * / %  then unary - + ~ !
   Numbers: 123, 0x7f, 0b101 (whole numbers only). === and !== read as == and !=.
   Functions: sin(x) cos(x) tri(x) (256 = one turn, −127…127), abs(x), min(x,y), max(x,y), sqrt(x) (of |x|, rounded
   down), rnd(x) (0…255, a hash of x; rnd() = rnd(t); random() is the same), floor(x) and int(x) (whole already: they do
   nothing, so formulas written for the JavaScript players read).
   Limits: 512 characters, 400 parts, 40 levels of brackets.
   BBExpr.compile(src) → {src, nodes, run(t, a, b, c) → the 32-bit value, s8(t, a, b, c, mode) → −128…127 (mode 0: the
   low byte as 0…255, less 128; 1: the low byte signed; 2: the value clipped)}. s8 never allocates. A bad formula throws
   an Error with .pos (0-based) and a plain message ("Formula: at character 9, expected ')' to close the '(' at
   character 3 but the formula ended"). BBExpr.check(src) → null, or that message. */
class BBExpr {
  static get MAXLEN(){ return 512; }
  static get MAXNODES(){ return 400; }
  static get MAXDEPTH(){ return 40; }
  static get FUNCS(){ return {sin:1, cos:1, tri:1, abs:1, min:2, max:2, sqrt:1, rnd:-1, random:-1, floor:1, int:1}; }
  // opcodes: 0 const, 1–4 t a b c, 10–28 binary, 30–32 unary, 40–47 functions, 50 jump-if-zero, 51 jump; 60–78 binary with a constant
  static get BIN(){ return {'+':10, '-':11, '*':12, '/':13, '%':14, '<<':15, '>>':16, '>>>':17, '<':18, '<=':19, '>':20, '>=':21, '==':22, '!=':23, '&':24, '^':25, '|':26, '&&':27, '||':28}; }
  static fail(pos, msg){ const e = new Error('Formula: at character ' + (pos + 1) + ', ' + msg); e.pos = pos; e.bb = true; throw e; }
  static check(src){ try { BBExpr.compile(src); return null; } catch (e){ return e && e.bb ? e.message : 'Formula: ' + String(e && e.message || e); } }
  static sinTab(){
    if (!BBExpr._sin){ const s = new Int32Array(256); for (let i = 0; i < 256; i++) s[i] = Math.round(127 * Math.sin(i / 256 * 2 * Math.PI)); BBExpr._sin = s; }
    return BBExpr._sin;
  }
  static hash(x){ let h = Math.imul(x ^ (x >>> 16), 0x45d9f3b); h = Math.imul(h ^ (h >>> 16), 0x45d9f3b); return (h ^ (h >>> 16)) & 255; }
  // one operation on whole numbers, exactly as the bytecode does it (the Int32Array store wraps to 32 bits; x/0 → 0)
  static op(o, x, y){
    const R = BBExpr._r || (BBExpr._r = new Int32Array(1)), S = BBExpr.sinTab();
    switch (o){
      case 10: R[0] = x + y; break; case 11: R[0] = x - y; break; case 12: R[0] = Math.imul(x, y); break;
      case 13: R[0] = x / y; break; case 14: R[0] = x % y; break; case 15: R[0] = x << y; break; case 16: R[0] = x >> y; break; case 17: R[0] = x >>> y; break;
      case 18: R[0] = x < y ? 1 : 0; break; case 19: R[0] = x <= y ? 1 : 0; break; case 20: R[0] = x > y ? 1 : 0; break; case 21: R[0] = x >= y ? 1 : 0; break;
      case 22: R[0] = x === y ? 1 : 0; break; case 23: R[0] = x !== y ? 1 : 0; break; case 24: R[0] = x & y; break; case 25: R[0] = x ^ y; break; case 26: R[0] = x | y; break;
      case 27: R[0] = x !== 0 && y !== 0 ? 1 : 0; break; case 28: R[0] = x !== 0 || y !== 0 ? 1 : 0; break;
      case 30: R[0] = -x; break; case 31: R[0] = ~x; break; case 32: R[0] = x ? 0 : 1; break;
      case 40: R[0] = S[x & 255]; break; case 41: R[0] = S[(x + 64) & 255]; break; case 42: R[0] = (127 * (64 - Math.abs(((x + 64) & 255) - 128))) >> 6; break;
      case 43: R[0] = x < 0 ? -x : x; break; case 44: R[0] = Math.sqrt(Math.abs(x)); break; case 45: R[0] = BBExpr.hash(x); break;
      case 46: R[0] = x < y ? x : y; break; case 47: R[0] = x > y ? x : y; break;
    }
    return R[0];
  }
  static tokens(src){
    const T = [], n = src.length, dig = c => c >= '0' && c <= '9', hex = c => dig(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    const alpha = c => (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c === '_';
    let i = 0;
    while (i < n){
      const ch = src[i];
      if (ch === ' ' || ch === '\t' || ch === '\n' || ch === '\r'){ i++; continue; }
      if (dig(ch)){
        const p = i; let v;
        if (ch === '0' && (src[i + 1] === 'x' || src[i + 1] === 'X')){ i += 2; const q = i; while (i < n && hex(src[i])) i++;
          if (i === q) BBExpr.fail(p, 'a hex number needs digits after 0x'); if (i - q > 8) BBExpr.fail(p, 'that number is too long (32 bits at most)'); v = parseInt(src.slice(q, i), 16) | 0; }
        else if (ch === '0' && (src[i + 1] === 'b' || src[i + 1] === 'B')){ i += 2; const q = i; while (i < n && (src[i] === '0' || src[i] === '1')) i++;
          if (i === q) BBExpr.fail(p, 'a binary number needs digits after 0b'); if (i - q > 32) BBExpr.fail(p, 'that number is too long (32 bits at most)'); v = parseInt(src.slice(q, i), 2) | 0; }
        else { while (i < n && dig(src[i])) i++; if (i - p > 10 || Number(src.slice(p, i)) > 4294967295) BBExpr.fail(p, 'that number is too big (32 bits at most)'); v = Number(src.slice(p, i)) | 0; }
        if (src[i] === '.') BBExpr.fail(i, 'only whole numbers work (the maths is 32-bit whole numbers, like the C original)');
        if (i < n && (alpha(src[i]) || dig(src[i]))) BBExpr.fail(i, 'a number runs into a name (put an operator between them)');
        T.push({k:'num', v, p}); continue;
      }
      if (alpha(ch)){ const p = i; while (i < n && (alpha(src[i]) || dig(src[i]))) i++; T.push({k:'id', v:src.slice(p, i), p}); continue; }
      const three = src.substr(i, 3), two = src.substr(i, 2);
      if (three === '>>>' || three === '===' || three === '!=='){ T.push({k:'op', v:three === '===' ? '==' : three === '!==' ? '!=' : three, p:i}); i += 3; continue; }
      if (two === '<<' || two === '>>' || two === '<=' || two === '>=' || two === '==' || two === '!=' || two === '&&' || two === '||'){ T.push({k:'op', v:two, p:i}); i += 2; continue; }
      if ('+-*/%&|^~!<>?:(),'.includes(ch)){ T.push({k:'op', v:ch, p:i}); i++; continue; }
      if (ch === '=') BBExpr.fail(i, "'=' isn't allowed (a formula can't set anything; == compares)");
      if (ch === '"' || ch === "'" || ch === '`' || ch === '[' || ch === ']' || ch === '{' || ch === '}') BBExpr.fail(i, "text, [ ] and { } aren't part of a formula");
      if (ch === ';') BBExpr.fail(i, "';' isn't allowed: a formula is one expression");
      if (ch === '.') BBExpr.fail(i, "'.' isn't allowed (whole numbers only, and no methods)");
      BBExpr.fail(i, "'" + (ch.codePointAt(0) > 126 || ch.codePointAt(0) < 32 ? 'that character' : ch) + "' isn't allowed here");
    }
    T.push({k:'end', v:'', p:n});
    return T;
  }
  static compile(src){
    if (typeof src !== 'string') BBExpr.fail(0, 'the formula is not text');
    if (src.length > BBExpr.MAXLEN) BBExpr.fail(BBExpr.MAXLEN, 'the formula is too long (' + BBExpr.MAXLEN + ' characters at most)');
    const T = BBExpr.tokens(src), F = BBExpr.FUNCS, BIN = BBExpr.BIN, op = BBExpr.op;
    let k = 0, nodes = 0, depth = 0;
    const peek = () => T[k], next = () => T[k++];
    const what = t => t.k === 'end' ? 'the formula ended' : "found '" + t.v + "'";
    const count = t => { if (++nodes > BBExpr.MAXNODES) BBExpr.fail(t.p, 'the formula is too complicated (' + BBExpr.MAXNODES + ' parts at most)'); };
    const enter = t => { if (++depth > BBExpr.MAXDEPTH + 1) BBExpr.fail(t.p, 'it is nested too deep (' + BBExpr.MAXDEPTH + ' levels at most)'); };
    // tree nodes: [0, value] a constant · [1..4] t a b c · [o, x] unary or a function of one · [o, x, y] binary · [50, c, x, y] ?:
    // anything whose parts are all constants is worked out now
    const mk = (o, x, y) => x[0] === 0 && (y === undefined || y[0] === 0) ? [0, op(o, x[1], y === undefined ? 0 : y[1])] : y === undefined ? [o, x] : [o, x, y];
    const LEVELS = [['||'], ['&&'], ['|'], ['^'], ['&'], ['==', '!='], ['<', '<=', '>', '>='], ['<<', '>>', '>>>'], ['+', '-'], ['*', '/', '%']];
    const expr = () => {   // ?: (right to left)
      const t0 = peek(); enter(t0);
      const c = level(0);
      if (peek().k === 'op' && peek().v === '?'){
        next(); const a = expr(), t = next();
        if (t.k !== 'op' || t.v !== ':') BBExpr.fail(t.p, "expected ':' for the '?' but " + what(t));
        const b = expr(); count(t); depth--;
        return c[0] === 0 ? (c[1] ? a : b) : [50, c, a, b];
      }
      depth--; return c;
    };
    const level = i => {
      if (i >= LEVELS.length) return unary();
      let L = level(i + 1);
      for (;;){
        const t = peek(); if (t.k !== 'op' || !LEVELS[i].includes(t.v)) return L;
        next(); const R = level(i + 1); count(t); L = mk(BIN[t.v], L, R);
      }
    };
    const unary = () => {
      const t = peek();
      if (t.k === 'op' && (t.v === '-' || t.v === '+' || t.v === '~' || t.v === '!')){
        next(); enter(t); const a = unary(); depth--; count(t);
        return t.v === '+' ? a : mk(t.v === '-' ? 30 : t.v === '~' ? 31 : 32, a);
      }
      return primary();
    };
    const primary = () => {
      const t = next(); count(t);
      if (t.k === 'num') return [0, t.v];
      if (t.k === 'op' && t.v === '('){
        const e = expr(), c = next();
        if (c.k !== 'op' || c.v !== ')') BBExpr.fail(c.p, "expected ')' to close the '(' at character " + (t.p + 1) + ' but ' + what(c));
        return e;
      }
      if (t.k === 'id'){
        const nm = t.v, vi = ['t', 'a', 'b', 'c'].indexOf(nm);
        if (vi >= 0) return [1 + vi];
        if (!Object.prototype.hasOwnProperty.call(F, nm)) BBExpr.fail(t.p, "'" + nm + "' isn't known (use t, a, b, c, numbers, or sin cos tri abs min max sqrt rnd)");
        const o = next(); if (o.k !== 'op' || o.v !== '(') BBExpr.fail(o.p, "expected '(' after " + nm + ' but ' + what(o));
        const args = [];
        if (!(peek().k === 'op' && peek().v === ')')){
          for (;;){ args.push(expr()); const s = peek(); if (s.k === 'op' && s.v === ','){ next(); continue; } break; }
        }
        const c = next();
        if (c.k !== 'op' || c.v !== ')') BBExpr.fail(c.p, "expected ')' or ',' in " + nm + '( ) but ' + what(c));
        const want = F[nm];
        if (want >= 0 && args.length !== want) BBExpr.fail(t.p, nm + ' takes ' + want + (want === 1 ? ' value' : ' values') + ', not ' + args.length);
        if (want < 0 && args.length > 1) BBExpr.fail(t.p, nm + ' takes one value or none');
        if (nm === 'floor' || nm === 'int') return args[0];
        const fo = {sin:40, cos:41, tri:42, abs:43, sqrt:44, rnd:45, random:45, min:46, max:47}[nm];
        return fo >= 46 ? mk(fo, args[0], args[1]) : mk(fo, args[0] || [1]);
      }
      if (t.k === 'end') BBExpr.fail(t.p, src.trim() ? 'the formula ended where a value was expected' : 'the formula is empty');
      BBExpr.fail(t.p, "expected a number, t, a, b, c, a function or '(' but " + what(t));
    };
    const root = expr(), e = peek();
    if (e.k !== 'end') BBExpr.fail(e.p, e.k === 'op' && e.v === ')' ? "there's a ')' with no '(' before it" : e.k === 'op' && (e.v === ',' || e.v === ':') ? "'" + e.v + "' isn't expected here" : 'expected an operator but ' + what(e));
    // bytecode, and the deepest the stack gets
    const code = []; let sp = 0, top = 1;
    const push = () => { if (++sp > top) top = sp; };
    const emit = n => {
      const o = n[0];
      if (o === 0){ code.push(0, n[1]); push(); return; }
      if (o <= 4){ code.push(o); push(); return; }
      if (o === 50){   // c ? a : b
        emit(n[1]); code.push(50, 0); const j1 = code.length - 1; sp--;
        emit(n[2]); code.push(51, 0); const j2 = code.length - 1; sp--;
        code[j1] = code.length; emit(n[3]); code[j2] = code.length; return;
      }
      if (n.length === 2){ emit(n[1]); code.push(o); return; }
      if (n[2][0] === 0 && o <= 28){ emit(n[1]); code.push(o + 50, n[2][1]); return; }   // x op constant: one step
      emit(n[1]); emit(n[2]); code.push(o); sp--;
    };
    emit(root);
    const C = Int32Array.from(code), N = C.length, St = new Int32Array(top + 2), S = BBExpr.sinTab(), H = BBExpr.hash;
    const exec = (t, a, b, c) => {
      let s = -1, i = 0, y = 0;
      while (i < N){
        switch (C[i++]){
          case 0: St[++s] = C[i++]; break;
          case 1: St[++s] = t; break; case 2: St[++s] = a; break; case 3: St[++s] = b; break; case 4: St[++s] = c; break;
          case 10: y = St[s--]; St[s] = St[s] + y; break;
          case 11: y = St[s--]; St[s] = St[s] - y; break;
          case 12: y = St[s--]; St[s] = Math.imul(St[s], y); break;
          case 13: y = St[s--]; St[s] = St[s] / y; break;          // the store truncates toward zero; x/0 (±Infinity, NaN) stores 0
          case 14: y = St[s--]; St[s] = St[s] % y; break;
          case 15: y = St[s--]; St[s] = St[s] << y; break;
          case 16: y = St[s--]; St[s] = St[s] >> y; break;
          case 17: y = St[s--]; St[s] = St[s] >>> y; break;
          case 18: y = St[s--]; St[s] = St[s] < y ? 1 : 0; break;
          case 19: y = St[s--]; St[s] = St[s] <= y ? 1 : 0; break;
          case 20: y = St[s--]; St[s] = St[s] > y ? 1 : 0; break;
          case 21: y = St[s--]; St[s] = St[s] >= y ? 1 : 0; break;
          case 22: y = St[s--]; St[s] = St[s] === y ? 1 : 0; break;
          case 23: y = St[s--]; St[s] = St[s] !== y ? 1 : 0; break;
          case 24: y = St[s--]; St[s] = St[s] & y; break;
          case 25: y = St[s--]; St[s] = St[s] ^ y; break;
          case 26: y = St[s--]; St[s] = St[s] | y; break;
          case 27: y = St[s--]; St[s] = St[s] !== 0 && y !== 0 ? 1 : 0; break;
          case 28: y = St[s--]; St[s] = St[s] !== 0 || y !== 0 ? 1 : 0; break;
          case 30: St[s] = -St[s]; break;
          case 31: St[s] = ~St[s]; break;
          case 32: St[s] = St[s] ? 0 : 1; break;
          case 40: St[s] = S[St[s] & 255]; break;
          case 41: St[s] = S[(St[s] + 64) & 255]; break;
          case 42: St[s] = (127 * (64 - Math.abs(((St[s] + 64) & 255) - 128))) >> 6; break;
          case 43: y = St[s]; St[s] = y < 0 ? -y : y; break;
          case 44: St[s] = Math.sqrt(Math.abs(St[s])); break;
          case 45: St[s] = H(St[s]); break;
          case 46: y = St[s--]; if (y < St[s]) St[s] = y; break;
          case 47: y = St[s--]; if (y > St[s]) St[s] = y; break;
          case 50: if (St[s--] === 0) i = C[i]; else i++; break;
          case 51: i = C[i]; break;
          case 60: St[s] = St[s] + C[i++]; break;
          case 61: St[s] = St[s] - C[i++]; break;
          case 62: St[s] = Math.imul(St[s], C[i++]); break;
          case 63: St[s] = St[s] / C[i++]; break;
          case 64: St[s] = St[s] % C[i++]; break;
          case 65: St[s] = St[s] << C[i++]; break;
          case 66: St[s] = St[s] >> C[i++]; break;
          case 67: St[s] = St[s] >>> C[i++]; break;
          case 68: St[s] = St[s] < C[i++] ? 1 : 0; break;
          case 69: St[s] = St[s] <= C[i++] ? 1 : 0; break;
          case 70: St[s] = St[s] > C[i++] ? 1 : 0; break;
          case 71: St[s] = St[s] >= C[i++] ? 1 : 0; break;
          case 72: St[s] = St[s] === C[i++] ? 1 : 0; break;
          case 73: St[s] = St[s] !== C[i++] ? 1 : 0; break;
          case 74: St[s] = St[s] & C[i++]; break;
          case 75: St[s] = St[s] ^ C[i++]; break;
          case 76: St[s] = St[s] | C[i++]; break;
          case 77: St[s] = St[s] !== 0 && C[i++] !== 0 ? 1 : 0; break;
          case 78: St[s] = St[s] !== 0 || C[i++] !== 0 ? 1 : 0; break;
          default: i = N;
        }
      }
    };
    return {src, nodes, size:N,
      run(t, a, b, c){ exec(t | 0, a | 0, b | 0, c | 0); return St[0]; },
      s8(t, a, b, c, mode){ exec(t, a, b, c); const v = St[0]; return mode === 1 ? (v << 24) >> 24 : mode === 2 ? (v < -128 ? -128 : v > 127 ? 127 : v) : (v & 255) - 128; }};
  }
}

/* Bytebeat: a pad engine around BBExpr. The formula runs at its own sample clock (Clock, 8 kHz like the originals);
   with Pitch tracking at 100% that clock follows the pad's octave and tune, so it plays in tune-ish (Octave 2 = the
   clock as set). Each step of the clock the formula's value becomes a sample: 8-bit (the classic: its low 8 bits,
   0…255, around the middle), Signed 8-bit (the low byte as −128…127) or Linear (the value itself clipped to ±128, for
   sin/tri formulas that don't wrap). a, b and c are three knobs the formula can read (0…255). Bits crushes the byte
   further; Smoothing low-passes the stair steps; Start jumps into the formula (t starts there); Loop repeats a
   power-of-two stretch of t (2^(n+7) steps). An amp envelope, drive, level. Above the output rate the clock's steps
   in a sample are averaged. The formula text is not a number, so it lives on the pad (V.bbExpr) and arrives as the
   fourth argument; a bad formula renders silence (the editor shows BBExpr.check's message). */
class BytebeatCore {
  static defaults(){ return {oct:2, rate:8000, track:1, a:128, b:128, c:128, mode:0, bits:8, smooth:0, start:0, loop:0, atk:1, dec:600, sus:1, rel:40, tune:0, drive:0, level:0}; }
  static get MODES(){ return ['8-bit', 'Signed 8-bit', 'Linear']; }
  static get DEFAULT_EXPR(){ return 't*(t>>5|t>>8)'; }
  static smoothHz(s){ return 20000 * Math.pow(2, -7 * Math.max(0, Math.min(1, s))); }
  static loopLen(v){ const k = Math.round(v); return k < 1 ? 0 : Math.pow(2, Math.min(13, k) + 7); }
  // compiled formulas are kept (a few), so dragging a slider doesn't recompile the same text on every render
  static prog(src){
    const C = BytebeatCore._cache || (BytebeatCore._cache = new Map());
    let p = C.get(src); if (p) return p;
    p = BBExpr.compile(src); if (C.size >= 16) C.clear(); C.set(src, p); return p;
  }
  static render(o, sr, hold, extra){
    const p = Object.assign(BytebeatCore.defaults(), o || {});
    const src = typeof extra === 'string' && extra.trim() ? extra : BytebeatCore.DEFAULT_EXPR;
    let P; try { P = BytebeatCore.prog(src); } catch (e){ return new Float32Array(1); }
    const semis = 12 * (Math.round(p.oct) - 2) + (p.tune || 0), tr = Math.max(0, Math.min(1, p.track));
    const clock = Math.max(250, Math.min(96000, Math.max(1000, Math.min(48000, p.rate)) * Math.pow(2, tr * semis / 12)));
    const h = Math.max(0.02, hold || 1), atk = Math.max(0.0005, p.atk / 1000), dec = Math.max(0.001, p.dec / 1000), sus = Math.max(0, Math.min(1, p.sus)), rel = Math.max(0.002, p.rel / 1000);
    const len = Math.min(12, h + rel * 5 + 0.01), N = Math.max(1, Math.ceil(len * sr)), out = new Float32Array(N);
    const A = Math.round(Math.max(0, Math.min(255, p.a))), B = Math.round(Math.max(0, Math.min(255, p.b))), Cc = Math.round(Math.max(0, Math.min(255, p.c)));
    const mode = Math.max(0, Math.min(2, Math.round(p.mode))), bits = Math.max(1, Math.min(8, Math.round(p.bits))), keep = ~((1 << (8 - bits)) - 1);
    const t0 = Math.max(0, Math.min(1 << 30, Math.round(p.start))) | 0, L = BytebeatCore.loopLen(p.loop);
    const step = clock / sr, sm = p.smooth > 0.005, g = sm ? 1 - Math.exp(-2 * Math.PI * Math.min(sr * 0.45, BytebeatCore.smoothHz(p.smooth)) / sr) : 1;
    const dcR = 1 - 2 * Math.PI * 6 / sr, gLv = sus + (1 - sus) * Math.exp(-6.9 * Math.max(0, h - atk) / dec), s8 = P.s8;
    let ph = 0, n = 0, held = (s8(t0, A, B, Cc, mode) & keep) / 128, s1 = 0, s2 = 0, dx = 0, dy = 0;
    for (let i = 0; i < N; i++){
      ph += step;
      if (ph >= 1){
        let acc = 0, k = 0;
        while (ph >= 1){ ph -= 1; n++; if (L && n >= L) n -= L; acc += s8((t0 + n) | 0, A, B, Cc, mode) & keep; k++; }
        held = acc / (k * 128);
      }
      let y = held;
      if (sm){ s1 += (y - s1) * g; s2 += (s1 - s2) * g; y = s2; }
      const o = y - dx + dcR * dy; dx = y; dy = o;   // DC blocker: an 8-bit output sits around its middle, not at 0
      const tt = i / sr, env = tt < h ? (tt < atk ? tt / atk : sus + (1 - sus) * Math.exp(-6.9 * (tt - atk) / dec)) : gLv * Math.exp(-6.9 * (tt - h) / rel);
      out[i] = o * env;
    }
    return KeysUtil.finish(out, p, sr, 0.003);
  }
}

/* Chaos (original code, written from the published equations): chaotic systems as oscillators and modulation.
   Flows, integrated with fourth-order Runge–Kutta (RK4) in fixed small steps, as many a sample as the rate needs:
     Lorenz (1963)   x' = σ(y − x), y' = x(ρ − z) − y, z' = xy − βz               σ 10, β 8/3; Chaos: ρ 25…60
     Rössler (1976)  x' = −y − z, y' = x + ay, z' = b + z(x − c)                 a = b = 0.2; Chaos: c 2.6…6 (period 1, 2, 4… chaos)
     Chua (1983)     x' = α(y − x − f(x)), y' = x − y + z, z' = −βy                f the piecewise-linear diode, slopes −8/7 and −5/7,
                     β 14.29; Chaos: α 8.2…10.2 (a cycle, the single scroll, the double scroll)
     Thomas (1999)   x' = sin y − bx, y' = sin z − by, z' = sin x − bz             Chaos: b 0.32 (a cycle) … 0.13 (a wide strange attractor)
     Duffing (1918)  x' = y, y' = −δy + x − x³ + γ cos ωτ                          δ 0.3, ω 1.2; Chaos: γ 0.20 (period 1) … 0.50
   Maps, iterated twice per cycle of the rate (so a period-2 orbit sounds at the rate):
     Logistic (May 1976)  x ← r x (1 − x)                                           Chaos: r 3.4 (period 2) … 4
     Hénon (1976)         x ← 1 − a x² + y, y ← b x                                 b 0.3; Chaos: a 1.0 … 1.4
   The z of Duffing is its energy (½y² − ½x² + ¼x⁴); a map's y and z are its x one and two steps back.
   Rate is in cycles a second: each model's own time scale (the period of its main rotation, measured offline at each
   Chaos setting, TAB.p) sets how much system time a sample covers, so at audio rates the sound has a pitch-like centre
   near Rate (Lorenz: in z; its x mostly flips between the two lobes, see voice()); above a model's top rate
   (4–13 kHz at 48 kHz, maxRate) the steps stop getting longer and the pitch stops
   rising. Each output is centred and scaled by measured centres and ranges (TAB), with a peak follower (in cycles,
   not seconds) that only ever turns it down, so ox, oy and oz stay within ±1 at any rate. Anything that runs away
   (it shouldn't) restarts. Deterministic from the seed; reset() goes back to the same start. No allocation per sample.
   Use: const C = new ChaosCore(model, seed); C.setChaos(0…1); each sample C.tick(rateHz, sr); read C.ox, C.oy, C.oz. */
class ChaosCore {
  constructor(model, seed){
    this.seed0 = (seed >>> 0) || 0x2545f491; this.seed = this.seed0; this.model = -1; this.chaos = 0.5; this.P = 0;
    this.x = 0; this.y = 0; this.z = 0; this.ph = 0; this.x1 = 0; this.x2 = 0; this.dx = 0; this.dy = 0; this.dz = 0;
    this.tp = 1; this.cx = 0; this.cy = 0; this.cz = 0; this.rx = 1; this.ry = 1; this.rz = 1; this.kx = 1; this.ky = 1; this.kz = 1;
    this.ox = 0; this.oy = 0; this.oz = 0;
    this.setModel(model | 0);
  }
  static get MODELS(){ return ['Lorenz', 'Rössler', 'Chua', 'Thomas', 'Duffing', 'Logistic', 'Hénon']; }
  // h: largest internal step; map: iterated, not integrated; p0: start; lim: |x|,|y|,|z| beyond this is a runaway
  static get SPEC(){
    return ChaosCore._spec || (ChaosCore._spec = [{h:0.02, map:false, p0:[1, 1, 20], lim:400}, {h:0.06, map:false, p0:[1, -3, 0.1], lim:400}, {h:0.03, map:false, p0:[0.1, 0, 0], lim:40},
      {h:0.4, map:false, p0:[0.1, 0, -0.1], lim:400}, {h:0.08, map:false, p0:[0.5, 0, 0], lim:40}, {h:1, map:true, p0:[0.31, 0, 0], lim:4}, {h:1, map:true, p0:[0.1, 0, 0], lim:4}]);
  }
  static get MAXSUB(){ return 8; }
  static get TAB(){ return ChaosCore._tab || (ChaosCore._tab = ChaosCore.TABLES()); }
  static TABLES(){ return [
      /* Lorenz */ {p:[0.806, 0.732, 0.681, 0.646, 0.607, 0.573, 0.547, 0.53, 0.506],
        c:[[0.182, -0.0205, -0.0804, 0.00438, 0.0603, -0.104, 0.00806, -0.0476, -0.0106], [0.181, -0.0208, -0.0803, 0.0047, 0.0602, -0.103, 0.0089, -0.0477, -0.0102], [20.59, 24.93, 29.34, 33.71, 38, 42.23, 46.44, 50.64, 54.85]],
        r:[[18.12, 20.4, 22.32, 24.15, 25.68, 27.65, 28.99, 30.55, 31.6], [24.16, 28.5, 32.24, 36, 39.11, 43.43, 46.66, 50.32, 52.96], [21.42, 25.36, 28.58, 32.13, 34.77, 39.25, 42.22, 45.92, 48.37]]},
      /* Rössler */ {p:[5.755, 5.772, 5.764, 5.789, 5.806, 5.822, 5.839, 5.831, 5.848],
        c:[[0.14, 0.15, 0.156, 0.161, 0.167, 0.173, 0.174, 0.164, 0.172], [-0.699, -0.744, -0.779, -0.807, -0.841, -0.864, -0.875, -0.833, -0.865], [0.699, 0.745, 0.778, 0.806, 0.837, 0.865, 0.876, 0.831, 0.867]],
        r:[[4.85, 6.031, 6.955, 7.816, 8.708, 9.558, 10.4, 11.25, 12.07], [3.974, 5.031, 5.865, 6.645, 7.465, 8.26, 9.063, 9.919, 10.69], [2.678, 5.261, 7.411, 9.589, 12.33, 15.14, 18.17, 21.47, 24.85]]},
      /* Chua */ {p:[2.282, 2.304, 2.305, 2.308, 2.136, 2.173, 2.323, 2.335, 2.442],
        c:[[1.24, 1.218, 1.234, 0.0356, 0.0227, -0.00703, 0.000785, 0.0386, 0.0066], [0.0000348, 2.52e-7, 0.000016, -0.0000169, -0.0000035, -0.000038, 0.00000333, 0.0000115, -0.00000143], [-1.24, -1.218, -1.234, -0.0355, -0.0227, 0.00699, -0.00076, -0.0386, -0.00659]],
        r:[[1.006, 1.231, 1.546, 2.32, 2.324, 2.326, 2.337, 2.393, 2.377], [0.372, 0.386, 0.395, 0.405, 0.415, 0.425, 0.435, 0.446, 0.458], [1.888, 1.984, 2.186, 3.28, 3.29, 3.297, 3.313, 3.375, 3.366]]},
      /* Thomas */ {p:[11.7, 12.8, 14.52, 16.81, 20.67, 18.46, 22.42, 25.86, 23.14],
        c:[[-2.282, -2.261, -2.24, -2.221, -2.198, -1.837, 0.00152, 0.0836, 0.49], [-2.282, -2.261, -2.241, -2.221, -2.198, -1.913, 0.00263, 0.084, -1.563], [-2.282, -2.26, -2.241, -2.222, -2.198, -1.856, 0.000789, 0.0831, 0.694]],
        r:[[0.41, 0.812, 1.098, 1.344, 1.584, 3.199, 4.236, 4.796, 4.523], [0.41, 0.812, 1.099, 1.344, 1.585, 3.276, 4.238, 4.797, 3.979], [0.41, 0.812, 1.099, 1.345, 1.583, 3.218, 3.479, 4.796, 3.921]]},
      /* Duffing */ {p:[5.236, 5.236, 5.236, 6.205, 8.727, 6.253, 6.705, 6.924, 6.359],
        c:[[0.804, 0.777, 0.756, -0.0104, -0.00686, -0.025, -0.00262, 0.00539, -0.00454], [0.0000194, 0.0000254, 0.0000264, 0.0000931, -0.000135, 0.0000751, 0.0000798, -0.000122, -0.0000325], [-0.0659, -0.0472, -0.0415, -0.0548, -0.0916, -0.0431, -0.0372, -0.027, -0.00786]],
        r:[[0.554, 0.586, 0.618, 1.489, 1.46, 1.539, 1.535, 1.555, 1.572], [0.647, 0.683, 0.705, 0.808, 0.713, 0.883, 0.925, 0.974, 1.072], [0.0243, 0.0336, 0.0701, 0.199, 0.202, 0.278, 0.334, 0.39, 0.442]]},
      /* Logistic */ {p:[2, 2, 2, 2, 2, 2, 2, 2, 2],
        c:[[0.647, 0.646, 0.648, 0.651, 0.668, 0.634, 0.531, 0.596, 0.503], [0.647, 0.646, 0.648, 0.651, 0.668, 0.634, 0.531, 0.596, 0.503], [0.647, 0.646, 0.648, 0.651, 0.668, 0.634, 0.531, 0.596, 0.503]],
        r:[[0.199, 0.252, 0.299, 0.35, 0.419, 0.442, 0.44, 0.535, 0.513], [0.199, 0.252, 0.299, 0.35, 0.419, 0.442, 0.44, 0.535, 0.513], [0.199, 0.252, 0.299, 0.35, 0.419, 0.442, 0.44, 0.535, 0.513]]},
      /* Hénon */ {p:[2, 2, 2, 2, 2, 2, 2, 2, 2],
        c:[[0.367, 0.356, 0.343, 0.399, 0.384, 0.3, 0.289, 0.306, 0.256], [0.11, 0.107, 0.103, 0.12, 0.115, 0.0901, 0.0867, 0.0919, 0.0769], [0.367, 0.356, 0.343, 0.399, 0.384, 0.3, 0.289, 0.306, 0.256]],
        r:[[1.044, 1.096, 1.149, 1.396, 1.445, 1.418, 1.347, 1.559, 1.572], [0.313, 0.329, 0.345, 0.419, 0.433, 0.425, 0.404, 0.468, 0.472], [1.044, 1.096, 1.149, 1.396, 1.445, 1.418, 1.347, 1.559, 1.572]]}]; }
  rnd(){ let s = this.seed; s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; this.seed = s; return s / 4294967296; }
  setModel(m){
    m = Math.max(0, Math.min(6, m | 0)); if (m === this.model) return;
    this.model = m; this.sp = ChaosCore.SPEC[m]; this.T = ChaosCore.TAB[m]; this.chaos = -1; this.setChaos(0.5); this.reset();
  }
  setChaos(v){
    const c = Math.max(0, Math.min(1, +v || 0)); if (c === this.chaos) return; this.chaos = c;
    const P = ChaosCore.PR || (ChaosCore.PR = [[25, 35], [2.6, 3.4], [8.2, 2], [0.32, -0.19], [0.2, 0.3], [3.4, 0.5999], [1, 0.4]]);   // main parameter: start, span
    this.P = P[this.model][0] + P[this.model][1] * c;
    // the measured tables at 9 points across Chaos, read with linear interpolation
    const T = this.T, q = c * 8, i = Math.min(7, q | 0), f = q - i, L = a => a[i] + (a[i + 1] - a[i]) * f;
    this.tp = L(T.p); this.cx = L(T.c[0]); this.cy = L(T.c[1]); this.cz = L(T.c[2]); this.rx = L(T.r[0]); this.ry = L(T.r[1]); this.rz = L(T.r[2]);
  }
  // back to the seeded start, then on to the attractor (8 cycles), so the first cycles already sound like it
  reset(){
    const S = this.sp; this.seed = this.seed0;
    this.x = S.p0[0] + (this.rnd() - 0.5) * 0.02; this.y = S.p0[1] + (this.rnd() - 0.5) * 0.02; this.z = S.p0[2] + (this.rnd() - 0.5) * 0.02;
    this.x1 = this.x2 = this.x; this.ph = 0; this.kx = this.rx; this.ky = this.ry; this.kz = this.rz;
    if (S.map) for (let i = 0; i < 64; i++) this.iter();
    else { const h = S.h * 0.5, n = Math.ceil(8 * this.tp / h); for (let i = 0; i < n; i++) this.rk4(h); }
    this.norm(0);
  }
  deriv(x, y, z){
    const P = this.P;
    switch (this.model){
      case 0: this.dx = 10 * (y - x); this.dy = x * (P - z) - y; this.dz = x * y - 2.6666667 * z; return;
      case 1: this.dx = -y - z; this.dy = x + 0.2 * y; this.dz = 0.2 + z * (x - P); return;
      case 2: this.dx = P * (y - 0.2857143 * x + 0.2142857 * (Math.abs(x + 1) - Math.abs(x - 1))); this.dy = x - y + z; this.dz = -14.285714 * y; return;   // −x − f(x), f = −5/7 x − 3/14 (|x+1| − |x−1|)
      case 3: this.dx = Math.sin(y) - P * x; this.dy = Math.sin(z) - P * y; this.dz = Math.sin(x) - P * z; return;
      default: this.dx = y; this.dy = x - 0.3 * y - x * x * x + P * Math.cos(z); this.dz = 1.2; return;   // Duffing: z is the forcing phase ωτ
    }
  }
  rk4(h){
    const x = this.x, y = this.y, z = this.z, h2 = h * 0.5;
    this.deriv(x, y, z); const ax = this.dx, ay = this.dy, az = this.dz;
    this.deriv(x + h2 * ax, y + h2 * ay, z + h2 * az); const bx = this.dx, by = this.dy, bz = this.dz;
    this.deriv(x + h2 * bx, y + h2 * by, z + h2 * bz); const cx = this.dx, cy = this.dy, cz = this.dz;
    this.deriv(x + h * cx, y + h * cy, z + h * cz);
    const h6 = h / 6;
    this.x = x + h6 * (ax + 2 * (bx + cx) + this.dx); this.y = y + h6 * (ay + 2 * (by + cy) + this.dy); this.z = z + h6 * (az + 2 * (bz + cz) + this.dz);
    if (this.model === 4 && this.z > 6.283185307179586) this.z -= 6.283185307179586;   // keep the forcing phase small
    const L = this.sp.lim; if (!(this.x < L && this.x > -L && this.y < L && this.y > -L && this.z < L && this.z > -L)) this.restart();
  }
  iter(){
    this.x2 = this.x1; this.x1 = this.x;
    if (this.model === 5){
      let x = this.P * this.x * (1 - this.x);
      // rounding can land the orbit on 0 or a fixed point, where it would stay: nudge it back into play
      if (!(x > 1e-6 && x < 0.999999) || (x - this.x < 1e-9 && this.x - x < 1e-9)) x = 0.1 + this.rnd() * 0.8;
      this.x = x; this.y = this.x1; this.z = this.x2;
    } else {
      const x = 1 - this.P * this.x * this.x + this.y; this.y = 0.3 * this.x; this.x = x; this.z = this.x2;
      if (!(x < 4 && x > -4)) this.restart();
    }
  }
  restart(){ const S = this.sp; this.x = S.p0[0] + (this.rnd() - 0.5) * 0.02; this.y = S.p0[1]; this.z = S.p0[2]; this.x1 = this.x2 = this.x; }
  // centre and scale; cyc = cycles just run. The follower jumps up to any larger value and relaxes back to the table
  // range by about 2% a cycle, so a burst past the measured range is turned down, never clipped.
  norm(cyc){
    const kp = cyc > 0 ? 1 - Math.min(0.5, cyc * 0.02) : 1;
    let v = this.x - this.cx, a = v < 0 ? -v : v, k = this.rx + (this.kx - this.rx) * kp; this.kx = a > k ? a : k; this.ox = v / this.kx;
    v = this.y - this.cy; a = v < 0 ? -v : v; k = this.ry + (this.ky - this.ry) * kp; this.ky = a > k ? a : k; this.oy = v / this.ky;
    const z = this.model === 4 ? 0.5 * this.y * this.y + this.x * this.x * (0.25 * this.x * this.x - 0.5) : this.z;
    v = z - this.cz; a = v < 0 ? -v : v; k = this.rz + (this.kz - this.rz) * kp; this.kz = a > k ? a : k; this.oz = v / this.kz;
  }
  // one output sample at `rate` cycles a second (sample rate sr)
  tick(rate, sr){
    if (!(rate > 0)) return;
    const S = this.sp;
    if (S.map){
      this.ph += rate * this.tp / sr; if (this.ph < 1) return;
      let n = 0; while (this.ph >= 1 && n < 8){ this.ph -= 1; this.iter(); n++; } if (this.ph >= 1) this.ph -= Math.floor(this.ph);
      this.norm(n / this.tp); return;
    }
    let dt = rate * this.tp / sr; const MX = S.h * ChaosCore.MAXSUB; if (dt > MX) dt = MX;
    const n = Math.ceil(dt / S.h - 1e-9), h = dt / n;
    for (let i = 0; i < n; i++) this.rk4(h);
    this.norm(dt / this.tp);
  }
  // the output with the clearest pitch at audio rates: Lorenz's x mostly flips between its two lobes (a rough, slow
  // square), its z follows the rotation; for every other model it is x
  voice(){ return this.model === 0 ? this.oz : this.ox; }
  // the highest rate (Hz) that still runs at full speed at sample rate sr (above it the pitch stops rising)
  maxRate(sr){ return this.sp.map ? sr * 8 / this.tp : this.sp.h * ChaosCore.MAXSUB * sr / this.tp; }
}


/* ═══ Bytebeat + Chaos: shared modules for …chains / …waves (original code, Oct 2026) ═══
   Paste into chains/chains-worklet.js inside the ===SHARED MODULES=== block, AFTER `class BBExpr` and `class ChaosCore`
   (from engine.js, identical to the copies in index.html's ENGINE block) and after DIV_BEATS / restarted (it uses
   restarted), e.g. just before `const SHARED_MODULES = {`. Then add to SHARED_MODULES:
       bytebeat:Bytebeat, chaos:Chaos
   and run tools/sync-shared.py to copy the block into …waves. Uses only clamp, linked, nextSeed and restarted from the block.
   Pitch CV is the shared modules' convention: 0.5 = A4 (440 Hz), 0.125 an octave. */

/* Bytebeat: one of sixteen built-in formulas (classic bytebeat formulas, viznut 2011 and community, some reworked to
   use the knobs; the last few are new) run at its own clock. Rate is the clock at A4 (8 kHz is the classic); a cable
   in Pitch moves it (0.5 = A4, 0.125 an octave), so a sequencer plays it. a, b and c are the formula's knobs (0–255),
   Mode how its number becomes sound (8-bit, signed 8-bit, linear), Smooth a low-pass on the stair steps. Reset (a gate)
   starts t again from 0, and so does PLAY. The list is compiled once, here, with BBExpr. */
const BB_LIST = [
  ['Sierpinski', 't&t>>8'],
  ['Forty-two', 't*(42&t>>10)'],
  ['Viznut 1', 't*(t>>5|t>>8)>>(t>>16)'],
  ['Viznut 2', 't*((t>>12|t>>8)&63&t>>4)'],
  ['Crowd', '(t*5&t>>7)|(t*3&t>>10)'],
  ['Arp machine', 't*(((t>>11)&(a>>4|1))%(b%15+1)+1)>>(c>>6)'],
  ['Bit stairs', '(t>>6|t|t>>(t>>16))*10+((t>>11)&7)'],
  ['Pulse chord', '(t*a&t>>4|t*5&t>>7|t*3&t/1024)-1'],
  ['Buzz melody', 't*(0xCA98>>(t>>9&14)&15)|t>>8'],
  ['Modulo bass', '((t%(a+64))*(t>>(b%7+6)&7))&255'],
  ['Pipe drone', '(t*(t>>8&t>>13)&255)|(t>>4)*c>>6'],
  ['Hiss beat', '(t>>(a%8+4)&1?rnd(t)>>2:0)|(t*((t>>13&3)+1)>>1&64)'],
  ['Sine tune', 'sin(t*(4+(t>>11&3))>>2)*(b+64)>>7'],
  ['Triangle bass', 'tri(t*(1+(0x3231>>(t>>12&12)&15))>>3)+tri(t>>(c%4+2))>>1'],
  ['Glitch shuffle', '((t&4096)?(t*(t^t%255)|t>>4):t>>3|(t&8192?t<<2:t))&255'],
  ['Hash noise', 'rnd(t>>(a>>5))&(t>>(b>>5)|c)'],
];
const BB_PROGS = BB_LIST.map(([, src]) => BBExpr.compile(src));
const Bytebeat = {
  type:'bytebeat', scope:'mono', cost:.15,
  inputs:{ pitch:'cv', reset:'gate' }, outputs:{ out:'audio' },
  params:{ formula:[0,0,BB_LIST.length - 1,1], rate:[8000,1000,32000,1], a:[128,0,255,1], b:[128,0,255,1], c:[128,0,255,1], mode:[0,0,2,1], smooth:[0,0,1,.01], level:[.7,0,1,.01] },
  create:() => ({ t:0, ph:0, held:0, s1:0, s2:0, dx:0, dy:0, r:0, sn:-1 }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, o = io.out.out, P = BB_PROGS[clamp(p.formula | 0, 0, BB_PROGS.length - 1)], s8 = P.s8;
    const A = clamp(Math.round(p.a), 0, 255), B = clamp(Math.round(p.b), 0, 255), C = clamp(Math.round(p.c), 0, 255), mode = clamp(p.mode | 0, 0, 2);
    const pc = linked(io, 'pitch'), base = clamp(p.rate, 250, 64000) / sr, lv = p.level;
    const sm = p.smooth > .005, g = sm ? 1 - Math.exp(-2 * Math.PI * Math.min(sr * .45, 20000 * Math.pow(2, -7 * p.smooth)) / sr) : 1, dcR = 1 - 2 * Math.PI * 6 / sr;
    if (restarted(st, ctx)){ st.t = 0; st.ph = 0; }
    for (let i = 0; i < ctx.block; i++){
      const rs = io.in.reset[i] > .5; if (rs && !st.r){ st.t = 0; st.ph = 1; } st.r = rs;
      // the clock: at most 4 formula steps a sample (above that the extra ones are skipped, not run)
      st.ph += pc ? Math.min(4, base * Math.pow(2, clamp((io.in.pitch[i] - .5) * 8, -8, 4))) : Math.min(4, base);
      if (st.ph >= 1){ let acc = 0, k = 0; while (st.ph >= 1){ st.ph -= 1; acc += s8(st.t, A, B, C, mode); st.t = (st.t + 1) | 0; k++; } st.held = acc / (k * 128); }
      let y = st.held;
      if (sm){ st.s1 += (y - st.s1) * g; st.s2 += (st.s1 - st.s2) * g; y = st.s2; }
      const d = y - st.dx + dcR * st.dy; st.dx = y; st.dy = d;   // DC blocker
      o[i] = d * lv;
    }
    if (!(st.dy > -4 && st.dy < 4)){ st.dx = st.dy = st.s1 = st.s2 = 0; }   // never: values are bounded, but stay safe
  }
};

/* Chaos: a chaotic system (ChaosCore: Lorenz, Rössler, Chua, Thomas, Duffing, Logistic or Hénon) as a wandering
   modulation source or a noisy oscillator. Rate runs from 0.05 Hz (a slow drift) to 3.3 kHz (a gritty tone with a
   pitch-like centre near Rate); a cable in Rate adds ±4 octaves. Chaos moves the system's main parameter across its
   range, from an orderly cycle to full chaos. X, Y and Z are the system's three variables, each about ±1 (times Level);
   Out is X as audio (for Lorenz, Z: its X mostly flips between the two lobes, Z follows the rotation and holds the pitch); Trig fires when X crosses zero going up. Smooth low-passes all of them (relative to Rate, so it
   works the same slow or fast). Reset (a gate) starts it again from the same point (its seed comes from nextSeed, so in …waves renders, which seed
   the engine, it is the same every time). */
const CHAOS_RATE = v => .05 * Math.pow(2, clamp(v, 0, 1) * 16);   // 0.05 Hz … 3.28 kHz
const Chaos = {
  type:'chaos', scope:'mono', cost:.3,
  inputs:{ rate:'cv', reset:'gate' }, outputs:{ x:'cv', y:'cv', z:'cv', out:'audio', trig:'gate' },
  params:{ model:[0,0,6,1], rate:[.35,0,1,.001], chaos:[.6,0,1,.01], smooth:[0,0,1,.01], level:[.8,0,1,.01] },
  create:() => ({ C:new ChaosCore(0, nextSeed()), m:0, r:0, arm:0, tg:0, lx:0, ly:0, lz:0 }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, C = st.C, m = clamp(p.model | 0, 0, 6), lv = p.level;
    if (m !== st.m || C.model !== m){ st.m = m; C.setModel(m); st.lx = C.ox; st.ly = C.oy; st.lz = C.oz; }
    C.setChaos(p.chaos);
    const base = CHAOS_RATE(p.rate), rc = linked(io, 'rate'), sm = p.smooth > .005, tl = Math.max(1, Math.round(sr * .002));
    const O = io.out, X = O.x, Y = O.y, Z = O.z, A = O.out, T = O.trig, RS = io.in.reset, RC = io.in.rate;
    let g = 1;
    if (sm && !rc) g = 1 - Math.exp(-2 * Math.PI * Math.min(sr * .45, base * 64 * Math.pow(2, -9 * p.smooth)) / sr);
    for (let i = 0; i < ctx.block; i++){
      const rs = RS[i] > .5; if (rs && !st.r){ C.reset(); st.arm = 0; } st.r = rs;
      const hz = rc ? base * Math.pow(2, clamp(RC[i], -1, 1) * 4) : base;
      C.tick(hz, sr);
      let x = C.ox, y = C.oy, z = C.oz;
      if (sm){
        if (rc) g = 1 - Math.exp(-2 * Math.PI * Math.min(sr * .45, hz * 64 * Math.pow(2, -9 * p.smooth)) / sr);
        st.lx += (x - st.lx) * g; st.ly += (y - st.ly) * g; st.lz += (z - st.lz) * g; x = st.lx; y = st.ly; z = st.lz;
      }
      // Trig: X up through zero, after it has been below −0.1 (so jitter around zero doesn't chatter); 2 ms long
      if (x < -.1) st.arm = 1; else if (st.arm && x > 0){ st.arm = 0; st.tg = tl; }
      T[i] = st.tg > 0 ? 1 : 0; if (st.tg > 0) st.tg--;
      X[i] = x * lv; Y[i] = y * lv; Z[i] = z * lv; A[i] = (m === 0 ? z : x) * lv;   // Out: Lorenz's z holds its pitch (x mostly flips lobe to lobe)
    }
  }
};


/* ── Random generator and Function generator (Mutable Instruments Marbles and Tides 2, MIT) ──────────────────
   The two modules run Mutable Instruments' own DSP, compiled to WebAssembly (dsp/mods: the wrappers and build.sh). The
   module is small (~185 KB) and lives here as base64, so …chains and …waves need no extra file. It is compiled once per
   audio thread or worker, the first time one of them is made; every Random and Function generator is an object in that
   one copy (64 at most). If WebAssembly isn't there or the module fails, they output silence and never throw. */
const MODS_WASM_B64 = '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fz+5hX8/7pB/Px6cfz9Lp38/c7J/P5e9fz+1yH8/z9N/P+Tefz/z6X8//PR/PwAAgD8AAIA/AACAPwAApggfDLYO1xCqEkYUuBUHFzsYVxlfGlYbPRwWHeQdph5fHw4gtSBVIe0hgCIMI5IjEySQJAcleyXqJVYmviYjJ4Qn4ic+KJYo7ChAKZEp4CktKncqwCoHK0wrjyvRKxEsTyyMLMcsAS06LXItqC3dLREuRC52LqYu1i4FLzMvXy+LL7cv4S8LMDMwWzCDMKkwzzD0MBkxPTFgMYMxpTHGMecxCDIoMkcyZjKFMqIywDLdMvoyFjMyM00zaDOCM50ztjPQM+kzAjQaNDI0SjRhNHk0jzSmNLw00jToNP00EjUnNTw1UDVkNXg1jDWfNbI1xTXYNeo1/TUPNiA2MjZENlU2ZjZ3Nog2mDapNrk2yTbZNug2+DYHNxY3Jjc0N0M3UjdgN283fTeLN5k3pze0N8I3zzfcN+o39zcEOBA4HTgqODY4QzhPOFs4ZzhzOH84ijiWOKI4rTi4OMQ4zzjaOOU48Dj7OAU5EDkbOSU5Lzk6OUQ5TjlYOWI5bDl2OYA5ijmTOZ05pjmwObk5wjnMOdU53jnnOfA5+TkCOgs6EzocOiU6LTo2Oj46RzpPOlc6YDpoOnA6eDqAOog6kDqYOqA6qDqvOrc6vzrGOs461TrdOuQ67DrzOvo6AjsJOxA7FzseOyY7LTs0Ozs7QTtIO087VjtdO2Q7ajtxO3g7fjuFO4s7kjuYO587pTusO7I7uDu/O8U7yzvRO9c73jvkO+o78Dv2O/w7AjwIPA48FDwaPB88JTwrPDE8Nzw8PEI8SDxNPFM8WDxePGQ8aTxvPHQ8ejx/PIU8ijyPPJU8mjyfPKU8qjyvPLQ8ujy/PMQ8yTzOPNQ82TzePOM86DztPPI89zz8PAE9Bj0LPRA9FT0aPR49Iz0oPS09Mj03PTs9QD1FPUo9Tj1TPVg9XT1hPWY9aj1vPXQ9eD19PYI9hj2LPY89lD2YPZ09oT2mPao9rz2zPbg9vD3APcU9yT3OPdI91j3bPd894z3oPew98D30Pfk9/T0BPgU+Cj4OPhI+Fj4bPh8+Iz4nPis+Lz40Pjg+PD5APkQ+SD5MPlA+VT5ZPl0+YT5lPmk+bT5xPnU+eT59PoE+hT6JPo0+kT6VPpk+nT6hPqU+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VlNtU4RTm1OyU8lT4FP3Uw5UJVQ9VFRUa1SCVJlUsVTIVN9U91QOVSZVPVVVVWxVhFWbVbNVy1XiVfpVElYqVkFWWVZxVolWoVa5VtFW6VYBVxlXMldKV2JXeleTV6tXw1fcV/RXDVglWD5YVlhvWIhYoVi5WNJY61gEWR1ZNllPWWhZgVmaWbNZzVnmWf9ZGVoyWkxaZVp/WphaslrMWuZa/1oZWzNbTVtnW4Fbm1u1W9Bb6lsEXB9cOVxUXG5ciVyjXL5c2Vz0XA9dKl1FXWBde12WXbFdzV3oXQReH147Xldecl6OXqpexl7iXv5eGl82X1Nfb1+MX6hfxV/hX/5fG2A4YFVgcmCPYKxgymDnYAVhImFAYV5hfGGaYbhh1mH0YRJiMWJPYm5ijWKsYsti6mIJYyhjSGNnY4djpmPGY+ZjBmQmZEdkZ2SIZKhkyWTqZAtlLWVOZW9lkWWzZdVl92UZZjxmXmaBZqRmx2bqZg1nMWdUZ3hnnGfBZ+VnCmgvaFRoeWieaMRo6mgQaTZpXWmDaapp0Wn5aSFqSWpxaplqwmrrahVrPmtoa5JrvWvoaxNsPmxqbJdsw2zwbB5tS216baht120HbjduZ26Ybslu+24ub2FvlW/Jb/5vM3BqcKBw2HARcUpxhHG/cftxOHJ2crVy9XI2c3lzvXMCdEl0knTddCl1eHXJdRx2cnbMdih3iXfud1h4yXhAecF5THrmepV7ZXxzfXN9AAAgAEAAYACAAKAAwADgAAABIAFAAWABgAGgAcAB4AEAAiACQAJgAoACoALAAuACAAMgA0ADYAOAA6ADwAPgAwAEIARABGAEgASgBMAE4AQABSAFQAVgBYAFoAXABeAFAAYgBkAGYAaABqAGwAbgBgAHIAdAB2AHgAegB8AH4AcACCAIQAhgCIAIoAjACOAIAAkgCUAJYAmACaAJwAngCQAKIApACmAKgAqgCsAK4AoACyALQAtgC4ALoAvAC+ALAAwgDEAMYAyADKAMwAzgDAANIA1ADWANgA2gDcAN4A0ADiAOQA5gDoAOoA7ADuAOAA8gD0APYA+AD6APwA/gDwAQIBBAEGAQgBCgEMAQ4BAAESARQBFgEYARoBHAEeARABIgEkASYBKAEqASwBLgEgATIBNAE2ATgBOgE8AT4BMAFCAUQBRgFIAUoBTAFOAUABUgFUAVYBWAFaAVwBXgFQAWIBZAFmAWgBagFsAW4BYAFyAXQBdgF4AXoBfAF+AXABggGEAYYBiAGKAYwBjgGAAZIBlAGWAZgBmgGcAZ4BkAGiAaQBpgGoAaoBrAGuAaABsgG0AbYBuAG6AbwBvgGwAcIBxAHGAcgBygHMAc4BwAHSAdQB1gHYAdoB3AHeAdAB4gHkAeYB6AHqAewB7gHgAfIB9AH2AfgB+gH8Af4B8AICAgQCBgIIAgoCDAIOAgACEgIUAhYCGAIaAhwCHgIQAiICJAImAigCKgIsAi4CIAIyAjQCNgI4AjoCPAI+AjACQgJEAkYCSAJKAkwCTgJAAlICVAJWAlgCWgJcAl4CUAJiAmQCZgJoAmoCbAJuAmACcgJ0AnYCeAJ6AnwCfgJwAoIChAKGAogCigKMAo4CgAKSApQClgKYApoCnAKeApACogKkAqYCqAKqAqwCrgKgArICtAK2ArgCugK8Ar4CsALCAsQCxgLIAsoCzALOAsAC0gLUAtYC2ALaAtwC3gLQAuIC5ALmAugC6gLsAu4C4ALyAvQC9gL4AvoC/AL+AvADAgMEAwYDCAMKAwwDDgMAAxIDFAMWAxgDGgMcAx4DEAMiAyQDJgMoAyoDLAMuAyADMgM0AzYDOAM6AzwDPgMwA0IDRANGA0gDSgNMA04DQANSA1QDVgNYA1oDXANeA1ADYgNkA2YDaANqA2wDbgNgA3IDdAN2A3gDegN8A34DcAOCA4QDhgOIA4oDjAOOA4ADkgOUA5YDmAOaA5wDngOQA6IDpAOmA6gDqgOsA64DoAOyA7QDtgO4A7oDvAO+A7ADwgPEA8YDyAPKA8wDzgPAA9ID1APWA9gD2gPcA94D0APiA+QD5gPoA+oD7APuA+AD8gP0A/YD+AP6A/wD/gPwBAH0A/QF9Af0CfQL9A30D/QB9BP0FfQX9Bn0G/Qd9B/0EfQj9CX0J/Qp9Cv0LfQv9CH0M/Q19Df0OfQ79D30P/Qx9EP0RfRH9En0S/RN9E/0QfRT9FX0V/RZ9Fv0XfRf9FH0Y/Rl9Gf0afRr9G30b/Rh9HP0dfR39Hn0e/R99H/0cfSD9IX0h/SJ9Iv0jfSP9IH0k/SV9Jf0mfSb9J30n/SR9KP0pfSn9Kn0q/St9K/0ofSz9LX0t/S59Lv0vfS/9LH0w/TF9Mf0yfTL9M30z/TB9NP01fTX9Nn02/Td9N/00fTj9OX05/Tp9Ov07fTv9OH08/T19Pf0+fT79P30//Tx9QP1BfUH9Qn1C/UN9Q/1AfUT9RX1F/UZ9Rv1HfUf9RH1I/Ul9Sf1KfUr9S31L/Uh9TP1NfU39Tn1O/U99T/1MfVD9UX1R/VJ9Uv1TfVP9UH1U/VV9Vf1WfVb9V31X/VR9WP1ZfVn9Wn1a/Vt9W/1YfVz9XX1d/V59Xv1ffV/9XH1g/WF9Yf1ifWL9Y31j/WB9ZP1lfWX9Zn1m/Wd9Z/1kfWj9aX1p/Wp9av1rfWv9aH1s/W19bf1ufW79b31v/Wx9cP1xfXH9cn1y/XN9c/1wfXT9dX11/XZ9dv13fXf9dH14/Xl9ef16fXr9e317/Xh9fP19fX39fn1+/X99f/18fYD9gX2B/YJ9gv2DfYP9gH2E/YV9hf2GfYb9h32H/YR9iP2JfYn9in2K/Yt9i/2IfYz9jX2N/Y59jv2PfY/9jH2Q/ZF9kf2SfZL9k32T/ZB9lP2VfZX9ln2W/Zd9l/2UfZj9mX2Z/Zp9mv2bfZv9mH2c/Z19nf2efZ79n32f/Zx9oP2hfaH9on2i/aN9o/2gfaT9pX2l/aZ9pv2nfaf9pH2o/al9qf2qfar9q32r/ah9rP2tfa39rn2u/a99r/2sfbD9sX2x/bJ9sv2zfbP9sH20/bV9tf22fbb9t323/bR9uP25fbn9un26/bt9u/24fbz9vX29/b59vv2/fb/9vH3A/cF9wf3CfcL9w33D/cB9xP3FfcX9xn3G/cd9x/3Efcj9yX3J/cp9yv3Lfcv9yH3M/c19zf3Ofc79z33P/cx90P3RfdH90n3S/dN90/3QfdT91X3V/dZ91v3Xfdf91H3Y/dl92f3afdr9233b/dh93P3dfd393n3e/d993/3cfeD94X3h/eJ94v3jfeP94H3k/eV95f3mfeb9533n/eR96P3pfen96n3q/et96/3ofez97X3t/e597v3vfe/97H3w/fF98f3yffL9833z/fB99P31ffX99n32/fd99/30ffj9+X35/fp9+v37ffv9+H38/f19/f3+ff79/33/ffwAAAAAAAAEAAQACAAMABAAFAAYACAAJAAsADQAPABEAFAAWABkAHAAfACIAJQApACwAMAA0ADgAPABBAEUASgBPAFQAWQBeAGQAaQBvAHUAewCBAIgAjgCVAJwA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P1B/UL9Q/1A/UX9Rv1H/UT9Sf1K/Uv9SP1N/U79T/1M/VH9Uv1T/VD9Vf1W/Vf9VP1Z/Vr9W/1Y/V39Xv1f/Vz9Yf1i/WP9YP1l/Wb9Z/1k/Wn9av1r/Wj9bf1u/W/9bP1x/XL9c/1w/XX9dv13/XT9ef16/Xv9eP19/X79f/18/YH9gv2D/YD9hf2G/Yf9hP2J/Yr9i/2I/Y39jv2P/Yz9kf2S/ZP9kP2V/Zb9l/2U/Zn9mv2b/Zj9nf2e/Z/9nP2h/aL9o/2g/aX9pv2n/aT9qf2q/av9qP2t/a79r/2s/bH9sv2z/bD9tf22/bf9tP25/br9u/24/b39vv2//bz9wf3C/cP9wP3F/cb9x/3E/cn9yv3L/cj9zf3O/c/9zP3R/dL90/3Q/dX91v3X/dT92f3a/dv92P3d/d793/3c/eH94v3j/eD95f3m/ef95P3p/er96/3o/e397v3v/ez98f3y/fP98P31/fb99/30/fn9+v37/fj9/f3+/f/9//3+/f39/P3//fr9+f34/fv99v31/fT99/3y/fH98P3z/e797f3s/e/96v3p/ej96/3m/eX95P3n/eL94f3g/eP93v3d/dz93/3a/dn92P3b/db91f3U/df90v3R/dD90/3O/c39zP3P/cr9yf3I/cv9xv3F/cT9x/3C/cH9wP3D/b79vf28/b/9uv25/bj9u/22/bX9tP23/bL9sf2w/bP9rv2t/az9r/2q/an9qP2r/ab9pf2k/af9ov2h/aD9o/2e/Z39nP2f/Zr9mf2Y/Zv9lv2V/ZT9l/2S/ZH9kP2T/Y79jf2M/Y/9iv2J/Yj9i/2G/YX9hP2H/YL9gf2A/YP9fv19/Xz9f/16/Xn9eP17/Xb9df10/Xf9cv1x/XD9c/1u/W39bP1v/Wr9af1o/Wv9Zv1l/WT9Z/1i/WH9YP1j/V79Xf1c/V/9Wv1Z/Vj9W/1W/VX9VP1X/VL9Uf1Q/VP9Tv1N/Uz9T/1K/Un9SP1L/Ub9Rf1E/Uf9Qv1B/UD9Q/0+/T39PP0//Tr9Of04/Tv9Nv01/TT9N/0y/TH9MP0z/S79Lf0s/S/9Kv0p/Sj9K/0m/SX9JP0n/SL9If0g/SP9Hv0d/Rz9H/0a/Rn9GP0b/Rb9Ff0U/Rf9Ev0R/RD9E/0O/Q39DP0P/Qr9Cf0I/Qv9Bv0F/QT9B/0C/QH9AP0AAQMA/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c0AUQbJBT0LrQoVDHkS1REtF30VyRgNHk0ciSK9IOknFSU5K1kpcS+FLZUzoTGlN6U1oTuVOYk/dT1dQ0FBHUb1RM1KnUhpTjFP9U2xU21RIVbVVIFaKVvRWXFfDVypYj1jzWFdZuVkaWnta2lo5W5db81tPXKpcBF1dXbZdDV5kXrpeD19jX7ZfCWBaYKtg+2BLYZlh52E0YoBizGIXY2FjqmPzYztkgmTJZA5lVGWYZdxlH2ZiZqRm5WYmZ2ZnpWfkZyJoX2icaNloFWlQaYppxGn+aTdqb2qnat5qFWtLa4Frtmvrax9sU2yGbLhs6mwcbU1tfm2ubd5tDW48bmpumG7GbvNuH29Mb3dvo2/Nb/hvInBMcHVwnnDGcO5wFnE9cWRxi3GxcdZx/HEhckZyanKOcrJy1XL4chpzPXNfc4BzoXPCc+NzA3QjdEN0YnSBdKB0v3TddPt0GHU2dVN1b3WMdah1xHXfdft1FnYxdkt2ZnaAdpp2s3bMduV2/nYXdy93R3dfd3d3jneld7x303fqdwB4FngseEF4V3hseIF4lnireL9403jnePt4D3kieTV5SXlbeW55gXmTeaV5t3nJedt57Hn9eQ56H3owekF6UXpienJ6gnqSeqF6sXrAes963nrtevx6C3sZeyh7NntEe1J7YHtte3t7iHuWe6N7sHu9e8p71nvje+97+3sIfBR8IHwrfDd8Q3xOfFp8ZXxwfHt8hnyRfJt8pnyxfLt8xXzPfNp85HzufPd8AX0LfRR9Hn0nfTB9OX1CfUt9VH1dfWZ9b313fYB9iH2QfZl9oX2pfbF9uX3Bfch90H3Yfd99533uffV9/X0Efgt+En4ZfiB+J34tfjR+O35Bfkh+Tn5Vflt+YX5nfm5+dH56foB+hn6LfpF+l36dfqJ+qH6tfrN+uH6+fsN+yH7NftJ+2H7dfuJ+537sfvB+9X76fv9+A38Ifw1/EX8Wfxp/Hn8jfyd/K38wfzR/OH88f0B/RH9If0x/UH9Uf1h/XH9ff2N/Z39qf25/cn91f3l/fH+Af4N/h3+Kf41/kH+Uf5d/mn+df6B/pH+nf6p/rX+wf7N/tn+4f7t/vn/Bf8R/xn/Jf8x/z3/Rf9R/1n/Zf9x/3n/hf+N/5n/of+p/7X/vf/F/9H/2f/h/+3/9f/9//3//f/1/+3/4f/Z/9H/xf+9/7X/qf+h/5n/jf+F/3n/cf9l/1n/Uf9F/z3/Mf8l/xn/Ef8F/vn+7f7h/tn+zf7B/rX+qf6d/pH+gf51/mn+Xf5R/kH+Nf4p/h3+Df4B/fH95f3V/cn9uf2p/Z39jf19/XH9Yf1R/UH9Mf0h/RH9Afzx/OH80fzB/K38nfyN/Hn8afxZ/EX8Nfwh/A3//fvp+9X7wfux+537ift1+2H7Sfs1+yH7Dfr5+uH6zfq1+qH6ifp1+l36Rfot+hn6Afnp+dH5ufmd+YX5bflV+Tn5IfkF+O340fi1+J34gfhl+En4LfgR+/X31fe59533ffdh90H3IfcF9uX2xfal9oX2ZfZB9iH2AfXd9b31mfV19VH1LfUJ9OX0wfSd9Hn0UfQt9AX33fO585HzafM98xXy7fLF8pnybfJF8hnx7fHB8ZXxafE58Q3w3fCt8IHwUfAh8+3vve+N71nvKe717sHuje5Z7iHt7e217YHtSe0R7Nnsoexl7C3v8eu163nrPesB6sXqhepJ6gnpyemJ6UXpBejB6H3oOev157Hnbecl5t3mleZN5gXlueVt5SXk1eSJ5D3n7eOd403i/eKt4lniBeGx4V3hBeCx4FngAeOp303e8d6V3jnd3d193R3cvdxd3/nbldsx2s3aadoB2ZnZLdjF2Fnb7dd91xHWodYx1b3VTdTZ1GHX7dN10v3SgdIF0YnRDdCN0A3Tjc8JzoXOAc19zPXMac/hy1XKyco5yanJGciFy/HHWcbFxi3FkcT1xFnHucMZwnnB1cExwInD4b81vo293b0xvH2/zbsZumG5qbjxuDW7eba5tfm1NbRxt6my4bIZsU2wfbOtrtmuBa0trFWveaqdqb2o3av5pxGmKaVBpFWnZaJxoX2giaORnpWdmZyZn5WakZmJmH2bcZZhlVGUOZclkgmQ7ZPNjqmNhYxdjzGKAYjRi52GZYUth+2CrYFpgCWC2X2NfD1+6XmReDV62XV1dBF2qXE9c81uXWzlb2lp7WhpauVlXWfNYj1gqWMNXXFf0VopWIFa1VUhV21RsVP1TjFMaU6dSM1K9UUdR0FBXUN1PYk/lTmhO6U1pTehMZUzhS1xL1kpOSsVJOkmvSCJIk0cDR3JG30VLRbVEHkSFQ+tCT0KyQRRBc0DRPy4/iT7iPTo9kDzkOzc7iDrXOSU5cTi7NwM3STaONdE0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';
const MODS_WASI = { wasi_snapshot_preview1: { fd_close: () => 0, fd_seek: () => 0, fd_write: () => 0, proc_exit: () => 0 } };
const MODS_RT = { mod:null, x:null, gen:0, fails:0, pending:null, buf:null, IN:null, OUT:null };
/* Base64 to bytes by hand: an AudioWorklet has no atob. */
function modsBytes(s){
  const T = new Uint8Array(128), A = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
  for (let i = 0; i < 64; i++) T[A.charCodeAt(i)] = i;
  let n = s.length; while (n && s[n - 1] === '=') n--;
  const out = new Uint8Array((n * 3) >> 2);
  for (let i = 0, j = 0; i < n; i += 4){
    const v = (T[s.charCodeAt(i)] << 18) | (T[s.charCodeAt(i + 1)] << 12) | ((T[s.charCodeAt(i + 2)] || 0) << 6) | (T[s.charCodeAt(i + 3)] || 0);
    if (j < out.length) out[j++] = (v >> 16) & 255;
    if (j < out.length) out[j++] = (v >> 8) & 255;
    if (j < out.length) out[j++] = v & 255;
  }
  return out;
}
/* The shared WebAssembly instance (made on first use; made again, up to 3 times, if it ever crashes). An audio thread,
   a worker and Node compile it on the spot. A page's main thread may refuse to compile a module this size synchronously:
   then it is compiled in the background (modsPrepare) and the modules are silent until it is ready. */
function modsX(){
  const R = MODS_RT;
  if (R.x) return R.x;
  if (R.fails >= 3 || R.pending || typeof WebAssembly === 'undefined') return null;
  try {
    if (!R.mod) R.mod = new WebAssembly.Module(modsBytes(MODS_WASM_B64));
    modsUse(new WebAssembly.Instance(R.mod, MODS_WASI));
    return R.x;
  } catch (e){ modsPrepare(); return null; }
}
function modsUse(inst){
  const x = inst.exports; if (x._initialize) x._initialize();
  MODS_RT.x = x; MODS_RT.gen++; MODS_RT.buf = null;
}
/* Compiles and instantiates in the background; resolves true when the modules can run. A page can await it before an
   offline render on its main thread (export it next to wasmInit), so the first render already has them. */
function modsPrepare(){
  const R = MODS_RT;
  if (R.x) return Promise.resolve(true);
  if (R.pending) return R.pending;
  if (R.fails >= 3 || typeof WebAssembly === 'undefined' || !WebAssembly.instantiate) return Promise.resolve(false);
  const p = R.mod ? WebAssembly.instantiate(R.mod, MODS_WASI).then(inst => ({ module:R.mod, instance:inst })) : WebAssembly.instantiate(modsBytes(MODS_WASM_B64), MODS_WASI);
  R.pending = p.then(r => { R.mod = r.module; modsUse(r.instance); R.pending = null; return true; },
    () => { R.fails = 3; R.pending = null; return false; });
  return R.pending;
}
/* This module's object in the instance (st.h), made when needed. Null: output silence this block. */
function modsOpen(st, type, sr){
  const x = modsX(); if (!x) return null;
  if (st.gen !== MODS_RT.gen){ st.gen = MODS_RT.gen; st.h = -1; st.wait = 0; st.P = null; }
  if (st.h < 0){
    if (st.wait > 0){ st.wait--; return null; }
    try { st.h = x.mods_new(type, sr, st.seed >>> 0); } catch (e){ st.h = -1; }
    if (st.h < 0){ st.wait = 375; return null; }   // all 64 in use: try again in a second or so
    st.fresh = true;
  }
  const R = MODS_RT, buf = x.memory.buffer;
  // Views into the wasm memory, remade only when it grew (making an object grows it).
  if (R.buf !== buf){ R.buf = buf; R.IN = new Float32Array(buf, x.mods_in(), 4 * 128); R.OUT = new Float32Array(buf, x.mods_out(), 8 * 128); }
  if (!st.P || st.P.buffer !== buf) st.P = new Float32Array(buf, x.mods_params(st.h), 32);
  return x;
}
/* Runs it; false when it crashed (the instance is then thrown away and every module starts afresh in a new one). */
function modsRun(st, x, n){
  try {
    if (!x.mods_process(st.h, n)){ try { x.mods_free(st.h); } catch (e){} st.h = -1; }   // a bad value came out: start it afresh
    return true;
  } catch (e){ if (MODS_RT.x === x){ MODS_RT.x = null; MODS_RT.fails++; } st.h = -1; return false; }
}
function modsClose(st){
  if (st.h >= 0 && st.gen === MODS_RT.gen && MODS_RT.x) try { MODS_RT.x.mods_free(st.h); } catch (e){}
  st.h = -1;
}
const modsSilence = io => { for (const k in io.out) io.out[k].fill(0); };
/* A steady clock at the song tempo (high for the first half of each note value), for when nothing is patched to Clock.
   st.ck < 0: start a new pulse now (made, or the song started). A rising edge on rs (Reset) starts one too. */
function modsClock(st, IN, n, hz, sr, rs){
  const inc = hz / sr;
  if (st.ck < 0) st.ck = 0;
  for (let i = 0; i < n; i++){
    if (rs){ const r = rs[i] > .5; if (r && !st.rk) st.ck = 0; st.rk = r; }
    IN[i] = st.ck < .5 ? 1 : 0; st.ck += inc; if (st.ck >= 1) st.ck -= Math.floor(st.ck);
  }
}

/* Random generator, based on Mutable Instruments Marbles (MIT). Random gates (t1–t3) and random voltages (X1–X3, Y)
   that can repeat. Its clock is the song tempo at Step × Rate, or the pulses on Clock (Rate then divides or multiplies
   them, ÷4 … ×4); it starts over when the song starts and on Reset. t2 fires on every tick; t1 and t3 as Model and Bias
   decide; Jitter moves them off the grid. X1–X3 take a new value on t1, t2 and t3 (X clock picks another source, a
   cable into X clock overrides it): Spread, Bias and Steps shape the distribution, Steps also smooths (left) or snaps
   to Scale (right). Déjà vu: 0 always new, the middle loops the last Loop length values, the top shuffles that loop.
   X out as Pitch: Marbles' volts as pitch CV, 1 V = one octave = 0.125, 0 V = C4 (0.40625, so 0.5 is still A4), plus
   X octave; as CV: volts / 5 (±5 → ±1, 0…+5 → 0…1). Y is volts / 5 too. */
const MarblesM = {
  type:'marbles', scope:'mono', cost:1,
  inputs:{ clock:'gate', reset:'gate', xclock:'gate', dejavu:'cv' },
  outputs:{ t1:'gate', t2:'gate', t3:'gate', x1:'cv', x2:'cv', x3:'cv', y:'cv' },
  params:{ div:[3,0,9,1], rate:[4,0,8,1], model:[0,0,6,1], bias:[.5,0,1,.01], jitter:[0,0,1,.01], dejavu:[0,0,1,.01], length:[8,1,16,1],
    spread:[.5,0,1,.01], xbias:[.5,0,1,.01], steps:[.75,0,1,.01], scale:[0,0,5,1], xrange:[0,0,2,1], oct:[0,-4,3,1],
    glen:[.5,0,1,.01], grand:[0,0,1,.01], tdv:[1,0,2,1], xdv:[1,0,2,1], xmode:[0,0,2,1], xclk:[0,0,3,1], xout:[0,0,1,1],
    ydiv:[6,0,11,1], yspread:[.5,0,1,.01], ybias:[.5,0,1,.01], ysteps:[0,0,1,.01], yrange:[2,0,2,1] },
  create:() => ({ h:-1, gen:-1, seed:nextSeed(), ck:-1, rk:false, sn:undefined, P:null }),
  destroy(st){ modsClose(st); },
  process(st, io, ctx){
    const n = ctx.block, p = io.p, rs = restarted(st, ctx);
    if (rs) st.ck = -1;
    const x = modsOpen(st, 0, ctx.sr);
    if (!x){ modsSilence(io); return; }
    const P = st.P, IN = MODS_RT.IN, OUT = MODS_RT.OUT;
    P[0] = p.rate; P[1] = 1; P[2] = p.model; P[3] = p.bias; P[4] = p.jitter; P[5] = p.glen; P[6] = p.grand; P[7] = p.dejavu; P[8] = p.length;
    P[9] = p.tdv; P[10] = p.xdv; P[11] = p.spread; P[12] = p.xbias; P[13] = p.steps; P[14] = p.scale; P[15] = p.xrange; P[16] = p.xmode;
    P[17] = p.xclk; P[18] = p.yrange; P[19] = p.ydiv; P[20] = p.yspread; P[21] = p.ybias; P[22] = p.ysteps; P[23] = p.oct; P[24] = p.xout;
    P[28] = linked(io, 'xclock') ? 1 : 0; P[29] = rs || st.fresh ? 1 : 0; st.fresh = false;
    if (linked(io, 'clock')) IN.set(io.in.clock.subarray(0, n), 0);
    else modsClock(st, IN, n, (ctx.transport.tempo || 120) / 60 / divBeats(p.div), ctx.sr, io.in.reset);
    IN.set(io.in.reset.subarray(0, n), 128); IN.set(io.in.xclock.subarray(0, n), 256); IN.set(io.in.dejavu.subarray(0, n), 384);
    if (!modsRun(st, x, n)){ modsSilence(io); return; }
    const o = io.out;
    o.t1.set(OUT.subarray(0, n)); o.t2.set(OUT.subarray(128, 128 + n)); o.t3.set(OUT.subarray(256, 256 + n));
    o.x1.set(OUT.subarray(384, 384 + n)); o.x2.set(OUT.subarray(512, 512 + n)); o.x3.set(OUT.subarray(640, 640 + n)); o.y.set(OUT.subarray(768, 768 + n));
  }
};

/* Function generator, based on Mutable Instruments Tides 2 (MIT). Four related slopes from one generator: an envelope
   (AD on each Trig; AR rises while Trig is held), an LFO or, in the Audio range, an oscillator (Looping). Slope moves
   the peak, Shape bends the curves, Smoothness filters (left) or wave-folds (right). Outputs picks what the four jacks
   carry: Gates (1 the slope × Shift/level, 2 the plain slope, 3 end of attack, 4 end of release), Amplitudes (Shift
   pans the slope across 1–4), Slope phases (four copies, Shift spreads their phase) or Frequencies (four copies at
   ratios Shift picks). Pitch adds to Frequency (0.125 an octave: at 440 Hz it plays in tune, 0.5 = A4). A cable into
   Clock, or Sync to tempo, locks it to pulses or to the song at Division, times Clock ratio (÷16 … ×16).
   Looping outputs are −1…1 (Tides' ±5 V), AD and AR 0…1 (0…8 V), gates 0/1. */
const TidesM = {
  type:'tides', scope:'mono', cost:1,
  inputs:{ trig:'gate', clock:'gate', pitch:'cv' },
  outputs:{ out1:'cv', out2:'cv', out3:'cv', out4:'cv' },
  params:{ mode:[1,0,2,1], output:[0,0,3,1], range:[0,0,1,1], freq:[-105.38,-192,36,.01], shape:[.5,0,1,.01], slope:[.5,0,1,.01],
    smooth:[.5,0,1,.01], shift:[.75,0,1,.01], sync:[0,0,1,1], div:[6,0,9,1], ratio:[9,0,18,1] },
  create:() => ({ h:-1, gen:-1, seed:0, ck:-1, rk:false, sn:undefined, P:null }),
  destroy(st){ modsClose(st); },
  process(st, io, ctx){
    const n = ctx.block, p = io.p, rs = restarted(st, ctx);
    if (rs) st.ck = -1;
    const x = modsOpen(st, 1, ctx.sr);
    if (!x){ modsSilence(io); return; }
    const P = st.P, IN = MODS_RT.IN, OUT = MODS_RT.OUT, clk = linked(io, 'clock'), sync = !clk && p.sync >= .5;
    P[0] = p.mode; P[1] = p.output; P[2] = p.range; P[3] = p.freq; P[4] = p.shape; P[5] = p.slope; P[6] = p.smooth; P[7] = p.shift;
    P[8] = linked(io, 'pitch') ? 1 : 0; P[9] = linked(io, 'trig') ? 1 : 0; P[10] = clk || sync ? 1 : 0; P[11] = p.ratio; P[12] = rs ? 1 : 0;
    IN.set(io.in.trig.subarray(0, n), 0);
    if (sync) modsClock(st, IN.subarray(128), n, (ctx.transport.tempo || 120) / 60 / divBeats(p.div), ctx.sr);
    else IN.set(io.in.clock.subarray(0, n), 128);
    IN.set(io.in.pitch.subarray(0, n), 256);
    if (!modsRun(st, x, n)){ modsSilence(io); return; }
    const o = io.out;
    o.out1.set(OUT.subarray(0, n)); o.out2.set(OUT.subarray(128, 128 + n)); o.out3.set(OUT.subarray(256, 256 + n)); o.out4.set(OUT.subarray(384, 384 + n));
  }
};

/* ── Basic studio effects (Oct 2026, original code): the plain everyday tools a DAW has as stock effects, for pads,
   tracks and patches: Simple delay, Filter, Low and high cut, Classic compressor, Noise gate, Auto-pan and Pump.
   Filters are TPT state-variable filters (Zavalishin, "The Art of VA Filter Design"), as in the Envelope filter.
   Tempo-synced ones count from the song's start (restarted) at the song tempo, so they land on the beat. */
const SD_DIVS = [['1/32', .125], ['1/16T', 1 / 6], ['1/16', .25], ['1/16D', .375], ['1/8T', 1 / 3], ['1/8', .5], ['1/8D', .75], ['1/4T', 2 / 3], ['1/4', 1], ['1/4D', 1.5], ['1/2', 2], ['1 bar', 4]];
const LFO_DIVS = [['1/16', .25], ['1/8', .5], ['1/4', 1], ['1/2', 2], ['1 bar', 4], ['2 bars', 8], ['4 bars', 16]];
const PUMP_DIVS = [['1/8', .5], ['1/4', 1], ['1/2', 2], ['1 bar', 4]];
const bfTempo = ctx => Math.max((ctx.transport && ctx.transport.tempo) || 120, 20);
const bfK = (t, sr) => 1 - Math.exp(-1 / (sr * Math.max(t, 1e-5)));   // one-pole coefficient for a time constant in seconds
const bfSvf = () => ({ z1:0, z2:0, lp:0, bp:0, hp:0 });
// One TPT state-variable filter step: g = tan(π fc / sr), k = 1/Q (2: no peak, 1.414: Butterworth, → 0: self-oscillating).
function bfStep(s, x, g, k){
  const a1 = 1 / (1 + g * (g + k)), a2 = g * a1, a3 = g * a2, v3 = x - s.z2, v1 = a1 * s.z1 + a2 * v3, v2 = s.z2 + a2 * s.z1 + a3 * v3;
  s.z1 = 2 * v1 - s.z1; s.z2 = 2 * v2 - s.z2; s.lp = v2; s.bp = v1; s.hp = x - k * v1 - v2;
}
const bfTidy = s => { if (!(s.z1 > -1e4 && s.z1 < 1e4 && s.z2 > -1e4 && s.z2 < 1e4)){ s.z1 = s.z2 = 0; } s.z1 = fxClean(s.z1); s.z2 = fxClean(s.z2); };

/* Simple delay: a clean digital echo. Time in note values at the song tempo (Sync) or in ms; Feedback brings it round
   again (up to 95% a pass); Ping-pong bounces the echoes left, right, left from a mono sum; Low cut and High cut thin
   and darken the echoes (and so each repeat more than the last). Mix keeps the dry sound at full level up to 50%, then
   fades it, so 100% is echoes only. A new time glides over ~60 ms rather than clicking. */
const SimpleDelay = {
  type:'sdelay', scope:'mono', cost:.2,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ sync:[1,0,1,1], div:[8,0,SD_DIVS.length - 1,1], time:[.375,.01,2,.001], fb:[.35,0,1,.01], mode:[0,0,1,1], lowcut:[80,20,2000,1], highcut:[9000,1000,20000,1], mix:[.3,0,1,.01] },
  create(sr){ const N = 1 << Math.ceil(Math.log2(sr * 4.2)); return { L:new Float32Array(N), R:new Float32Array(N), M:N - 1, w:0, d:-1, ll:0, lr:0, hl:0, hr:0 }; },
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, BL = st.L, BR = st.R, M = st.M;
    const sec = p.sync >= .5 ? 60 / bfTempo(ctx) * SD_DIVS[clamp(Math.round(p.div), 0, SD_DIVS.length - 1)][1] : p.time;
    const D = clamp(sec * sr, 4, M - 4), kd = bfK(.06, sr), fb = u(p.fb) * .95, ping = p.mode >= .5, mx = u(p.mix);
    const dryG = mx <= .5 ? 1 : 2 - 2 * mx, wetG = mx >= .5 ? 1 : 2 * mx;
    const kh = 1 - Math.exp(-2 * Math.PI * clamp(p.lowcut, 5, sr * .2) / sr), kl = 1 - Math.exp(-2 * Math.PI * clamp(p.highcut, 200, sr * .45) / sr);
    let d = st.d < 0 ? D : st.d, w = st.w, ll = st.ll, lr = st.lr, hl = st.hl, hr = st.hr;
    for (let i = 0; i < B; i++){
      d += (D - d) * kd;
      const pos = w - d + M + 1, i0 = Math.floor(pos), f = pos - i0, a = i0 & M, b = (i0 + 1) & M;
      const yl = BL[a] + (BL[b] - BL[a]) * f, yr = BR[a] + (BR[b] - BR[a]) * f;
      ll += (yl - ll) * kl; lr += (yr - lr) * kl;   // High cut
      hl += (ll - hl) * kh; hr += (lr - hr) * kh;   // Low cut: the echo less its own low end
      const el = ll - hl, er = lr - hr;
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      if (ping){ BL[w] = fxClean(fxSat((xl + xr) * .5 + er * fb)); BR[w] = fxClean(fxSat(el * fb)); }
      else { BL[w] = fxClean(fxSat(xl + el * fb)); BR[w] = fxClean(fxSat(xr + er * fb)); }
      w = (w + 1) & M;
      oL[i] = xl * dryG + el * wetG; oR[i] = xr * dryG + er * wetG;
    }
    st.d = d; st.w = w; st.ll = fxClean(ll); st.lr = fxClean(lr); st.hl = fxClean(hl); st.hr = fxClean(hr);
  }
};

/* Filter: the plain resonant filter. Low-pass, high-pass, band-pass or notch, 12 or 24 dB an octave, Cutoff 20 Hz–20 kHz,
   Resonance up to a whistle (at 0 a 12 dB filter is flat, Butterworth), Drive saturates the input. The LFO sweeps the
   cutoff up to ±4 octaves (Mod depth) at a note value of the song tempo, in one of six shapes; it restarts with the song.
   The cutoff moves smoothly (5 ms), so a square or random LFO never clicks. */
const Filter = {
  type:'filter', scope:'mono', cost:.25,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[0,0,3,1], slope:[0,0,1,1], cutoff:[2000,20,20000,1], res:[.2,0,1,.01], drive:[0,0,1,.01], lfo:[0,0,1,.01], rate:[4,0,LFO_DIVS.length - 1,1], shape:[0,0,5,1], mix:[1,0,1,.01] },
  create:() => ({ a:bfSvf(), b:bfSvf(), c:bfSvf(), d:bfSvf(), lc:-1, ph:0, rv:0, sn:undefined, nr:rng(nextSeed()) }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR;
    if (ctx.transport && restarted(st, ctx)) st.ph = 0;   // the LFO starts on the downbeat with the song
    const mode = clamp(Math.round(p.mode), 0, 3), s24 = p.slope >= .5, r = u(p.res), dep = u(p.lfo) * 4, shape = clamp(Math.round(p.shape), 0, 5);
    // k: 12 dB, or the resonant 2nd stage of 24 dB (the 1st is a fixed Butterworth stage, k 1.848); band-pass and notch
    // use the 12 dB k for both stages
    const k12 = 1.414 - 1.38 * r, k2 = mode < 2 ? .765 - .73 * r : k12, k1 = mode < 2 ? 1.848 : k12;
    const inc = bfTempo(ctx) / 60 / LFO_DIVS[clamp(Math.round(p.rate), 0, LFO_DIVS.length - 1)][1] / sr;
    const base = Math.log2(clamp(p.cutoff, 10, sr * .45)), top = Math.log2(sr * .45), ks = bfK(.005, sr);
    const drv = u(p.drive), dr = .25 + drv * 8, dn = 1 / Math.tanh(dr), mx = u(p.mix), A = st.a, Bq = st.b, C = st.c, D = st.d;
    let lc = st.lc < 0 ? base : st.lc, ph = st.ph;
    for (let i = 0; i < B; i++){
      ph += inc; if (ph >= 1){ ph -= Math.floor(ph); st.rv = st.nr() * 2 - 1; }   // runs at depth 0 too, so it stays on the beat
      let lv = 0;
      if (dep > 0){
        lv = shape === 0 ? Math.sin(2 * Math.PI * ph) : shape === 1 ? (ph < .5 ? 4 * ph - 1 : 3 - 4 * ph) : shape === 2 ? 2 * ph - 1 : shape === 3 ? 1 - 2 * ph : shape === 4 ? (ph < .5 ? 1 : -1) : st.rv;
      }
      lc += (Math.min(top, Math.max(3.4, base + lv * dep)) - lc) * ks;
      const g = Math.tan(Math.PI * Math.pow(2, lc) / sr);
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      const il = drv > .001 ? Math.tanh(xl * dr) * dn : xl, ir = drv > .001 ? Math.tanh(xr * dr) * dn : xr;
      let yl, yr;
      if (!s24){
        bfStep(A, il, g, k12); bfStep(C, ir, g, k12);
        yl = mode === 0 ? A.lp : mode === 1 ? A.hp : mode === 2 ? k12 * A.bp : il - k12 * A.bp;
        yr = mode === 0 ? C.lp : mode === 1 ? C.hp : mode === 2 ? k12 * C.bp : ir - k12 * C.bp;
      } else {
        bfStep(A, il, g, k1); bfStep(C, ir, g, k1);
        const ml = mode === 0 ? A.lp : mode === 1 ? A.hp : mode === 2 ? k1 * A.bp : il - k1 * A.bp, mr = mode === 0 ? C.lp : mode === 1 ? C.hp : mode === 2 ? k1 * C.bp : ir - k1 * C.bp;
        bfStep(Bq, ml, g, k2); bfStep(D, mr, g, k2);
        yl = mode === 0 ? Bq.lp : mode === 1 ? Bq.hp : mode === 2 ? k2 * Bq.bp : ml - k2 * Bq.bp;
        yr = mode === 0 ? D.lp : mode === 1 ? D.hp : mode === 2 ? k2 * D.bp : mr - k2 * D.bp;
      }
      oL[i] = xl + (fxGuard(yl) - xl) * mx; oR[i] = xr + (fxGuard(yr) - xr) * mx;
    }
    st.lc = lc; st.ph = ph; bfTidy(A); bfTidy(Bq); bfTidy(C); bfTidy(D);
  }
};

/* Low and high cut: the clean-up filter. Low cut (a high-pass) takes away rumble and mud below its frequency, High cut
   (a low-pass) hiss and fizz above it; each is off at its end of the range. 6, 12 or 24 dB an octave, Butterworth
   (no bump at the corner). */
const LoHiCut = {
  type:'cut', scope:'mono', cost:.15,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ low:[20,20,2000,1], high:[20000,1000,20000,1], slope:[1,0,2,1] },
  create:() => ({ h:[bfSvf(), bfSvf(), bfSvf(), bfSvf()], l:[bfSvf(), bfSvf(), bfSvf(), bfSvf()], lo:-1, hi:-1, p1:[0, 0, 0, 0] }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, sl = clamp(Math.round(p.slope), 0, 2);
    const loOn = p.low > 20.5, hiOn = p.high < 19900;
    // the corners glide a little each block (about 7 ms), so turning a knob doesn't zipper
    const lt = Math.log2(clamp(p.low, 10, sr * .45)), ht = Math.log2(clamp(p.high, 10, sr * .45));
    st.lo = st.lo < 0 ? lt : st.lo + (lt - st.lo) * .3; st.hi = st.hi < 0 ? ht : st.hi + (ht - st.hi) * .3;
    const flo = Math.pow(2, st.lo), fhi = Math.pow(2, st.hi), gl = Math.tan(Math.PI * flo / sr), gh = Math.tan(Math.PI * fhi / sr);
    const cl = 1 - Math.exp(-2 * Math.PI * flo / sr), ch = 1 - Math.exp(-2 * Math.PI * fhi / sr), H = st.h, Lw = st.l, P = st.p1;
    for (let i = 0; i < B; i++){
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      if (loOn){
        if (sl === 0){ P[0] += (xl - P[0]) * cl; P[1] += (xr - P[1]) * cl; xl -= P[0]; xr -= P[1]; }
        else if (sl === 1){ bfStep(H[0], xl, gl, 1.414); bfStep(H[1], xr, gl, 1.414); xl = H[0].hp; xr = H[1].hp; }
        else { bfStep(H[0], xl, gl, 1.848); bfStep(H[1], xr, gl, 1.848); bfStep(H[2], H[0].hp, gl, .765); bfStep(H[3], H[1].hp, gl, .765); xl = H[2].hp; xr = H[3].hp; }
      }
      if (hiOn){
        if (sl === 0){ P[2] += (xl - P[2]) * ch; P[3] += (xr - P[3]) * ch; xl = P[2]; xr = P[3]; }
        else if (sl === 1){ bfStep(Lw[0], xl, gh, 1.414); bfStep(Lw[1], xr, gh, 1.414); xl = Lw[0].lp; xr = Lw[1].lp; }
        else { bfStep(Lw[0], xl, gh, 1.848); bfStep(Lw[1], xr, gh, 1.848); bfStep(Lw[2], Lw[0].lp, gh, .765); bfStep(Lw[3], Lw[1].lp, gh, .765); xl = Lw[2].lp; xr = Lw[3].lp; }
      }
      oL[i] = xl; oR[i] = xr;
    }
    for (const s of H) bfTidy(s); for (const s of Lw) bfTidy(s); for (let j = 0; j < 4; j++) P[j] = fxClean(P[j]);
  }
};

/* Classic compressor: Threshold, Ratio, Attack, Release, Knee and Makeup, the controls every DAW's compressor has.
   Both sides are turned down together (stereo-linked, peak detection). Source Sidechain listens to the mixer's
   sidechain bus instead (a kick ducking a bass), and a cable into SC always wins; Mix blends in the dry sound
   (parallel, "New York" compression). */
const ClassicComp = {
  type:'compressor', scope:'mono', cost:.15,
  inputs:{ in:'audio', inR:'audio', sc:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ thresh:[-18,-60,0,.1], ratio:[4,1,20,.1], attack:[.01,.0001,.25,.0001], release:[.15,.01,2,.001], knee:[6,0,24,.1], makeup:[0,0,24,.1], source:[0,0,1,1], mix:[1,0,1,.01] },
  create:() => ({ g:0, gr:0 }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR;
    const scP = linked(io, 'sc'), bus = !scP && p.source >= .5 && !!ctx.scL && typeof ctx.scOn === 'function' && !!ctx.scOn();
    const SL = bus ? ctx.scL : L, SR = bus ? (ctx.scR || ctx.scL) : R, SC = io.in.sc;
    const thr = p.thresh, sl = 1 - 1 / Math.max(1, p.ratio), kn = Math.max(0, p.knee), mk = p.makeup, ka = bfK(p.attack, sr), kr = bfK(p.release, sr), mx = u(p.mix);
    let g = st.g, most = 0;
    for (let i = 0; i < B; i++){
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      let det; if (scP) det = Math.abs(SC[i]); else { const a = Math.abs(SL[i]), b = Math.abs(SR[i]); det = a > b ? a : b; }
      const xdB = det > 1e-6 ? 8.685889638 * Math.log(det) : -120, over = xdB - thr;
      const t = over <= -kn / 2 ? 0 : over < kn / 2 ? sl * (over + kn / 2) * (over + kn / 2) / (2 * kn) : sl * over;   // gain reduction wanted, dB (soft knee)
      g += (t - g) * (t > g ? ka : kr);
      if (g > most) most = g;
      const gain = Math.exp((mk - g) * .1151292546);
      oL[i] = xl + (fxGuard(xl * gain) - xl) * mx; oR[i] = xr + (fxGuard(xr * gain) - xr) * mx;
    }
    st.g = g > 1e-6 && g < 200 ? g : 0; st.gr = Math.max(st.gr, most);
  },
  show(st){ const v = [-st.gr]; st.gr = 0; return v; }
};

/* Noise gate: closes (turns the sound down by Range) when it falls below Threshold, so hiss, hum and spill between
   hits disappear; it opens again a few dB higher than it closes, so it doesn't chatter. Attack is how fast it opens,
   Hold how long it stays open after the sound drops, Release how fast it closes. Source Sidechain lets the mixer's
   sidechain bus open it (a gated pad following the drums); a cable into SC always wins. */
const NoiseGate = {
  type:'ngate', scope:'mono', cost:.1,
  inputs:{ in:'audio', inR:'audio', sc:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ thresh:[-45,-80,0,.1], range:[90,0,90,1], attack:[.0005,.0001,.05,.0001], hold:[.03,0,.5,.001], release:[.12,.005,2,.001], source:[0,0,1,1] },
  create:() => ({ e:0, open:false, hold:0, g:0 }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR;
    const scP = linked(io, 'sc'), bus = !scP && p.source >= .5 && !!ctx.scL && typeof ctx.scOn === 'function' && !!ctx.scOn();
    const SL = bus ? ctx.scL : L, SR = bus ? (ctx.scR || ctx.scL) : R, SC = io.in.sc;
    const on = Math.pow(10, p.thresh / 20), off = on * .6, floor = p.range >= 89.5 ? 0 : Math.pow(10, -p.range / 20);
    const ka = bfK(p.attack, sr), kr = bfK(p.release, sr), ke = bfK(.005, sr), hn = Math.round(p.hold * sr);
    let e = st.e, open = st.open, hold = st.hold, g = st.g;
    for (let i = 0; i < B; i++){
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      let a; if (scP) a = Math.abs(SC[i]); else { const x = Math.abs(SL[i]), y = Math.abs(SR[i]); a = x > y ? x : y; }
      e = a > e ? a : e + (a - e) * ke;   // peak follower: instant up, ~5 ms down
      if (e > on){ open = true; hold = hn; } else if (open && e < off){ if (hold > 0) hold--; else open = false; }
      const t = open ? 1 : floor; g += (t - g) * (t > g ? ka : kr);
      oL[i] = xl * g; oR[i] = xr * g;
    }
    st.e = fxClean(e); st.open = open; st.hold = hold; st.g = g > 1e-7 ? g : 0;
  }
};

/* Auto-pan: moves the sound from side to side, at a note value of the song tempo (Sync) or a rate in Hz; Depth is how
   far, Shape a sine, a triangle or a (rounded) square. A stereo sound keeps its own image: each side is only ever
   turned down, never up. Synced, it restarts with the song. */
const AutoPan = {
  type:'autopan', scope:'mono', cost:.05,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ sync:[1,0,1,1], div:[2,0,LFO_DIVS.length - 1,1], rate:[2,.05,20,.01], depth:[.8,0,1,.01], shape:[0,0,2,1] },
  create:() => ({ ph:0, sn:undefined }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR, sync = p.sync >= .5;
    if (ctx.transport && restarted(st, ctx) && sync) st.ph = 0;
    const hz = sync ? bfTempo(ctx) / 60 / LFO_DIVS[clamp(Math.round(p.div), 0, LFO_DIVS.length - 1)][1] : clamp(p.rate, .01, 40);
    const inc = hz / sr, dep = u(p.depth), shape = clamp(Math.round(p.shape), 0, 2), sq = 1 / Math.tanh(4);
    let ph = st.ph;
    for (let i = 0; i < B; i++){
      ph += inc; if (ph >= 1) ph -= Math.floor(ph);
      const s = Math.sin(2 * Math.PI * ph), v = shape === 0 ? s : shape === 1 ? (ph < .25 ? 4 * ph : ph < .75 ? 2 - 4 * ph : 4 * ph - 4) : Math.tanh(4 * s) * sq;
      const th = (v * dep + 1) * Math.PI / 4, gl = Math.min(1, Math.SQRT2 * Math.cos(th)), gr = Math.min(1, Math.SQRT2 * Math.sin(th));
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      oL[i] = xl * gl; oR[i] = xr * gr;
    }
    st.ph = ph;
  }
};

/* Pump: the "sidechain" duck without a sidechain. On every note value (a quarter by default) the level drops by Depth
   and comes back over Release (a share of the note), Curve from a straight line to a fast early recovery: the pumping
   of a kick-ducked pad or bass, locked to the song. A 2 ms fade into each dip keeps it click-free. */
const Pump = {
  type:'pump', scope:'mono', cost:.03,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ div:[1,0,PUMP_DIVS.length - 1,1], depth:[.8,0,1,.01], release:[.7,.05,1,.01], curve:[.5,0,1,.01] },
  create:() => ({ ph:0, g:1, sn:undefined }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, B = ctx.block, L = io.in.in, R = inR(io), oL = io.out.out, oR = io.out.outR;
    if (ctx.transport && restarted(st, ctx)) st.ph = 0;
    const inc = bfTempo(ctx) / 60 / PUMP_DIVS[clamp(Math.round(p.div), 0, PUMP_DIVS.length - 1)][1] / sr;
    const dep = u(p.depth), rel = clamp(p.release, .02, 1), ex = 1 + u(p.curve) * 3, kg = bfK(.002, sr);
    let ph = st.ph, g = st.g;
    for (let i = 0; i < B; i++){
      ph += inc; if (ph >= 1) ph -= Math.floor(ph);
      const x = ph / rel, env = x >= 1 ? 0 : Math.pow(1 - x, ex), t = 1 - dep * env;
      g += (t - g) * (t < g ? kg : 1);   // into the dip over ~2 ms; out of it along the curve itself
      let xl = L[i], xr = R[i]; if (!(xl > -8 && xl < 8)) xl = 0; if (!(xr > -8 && xr < 8)) xr = 0;
      oL[i] = xl * g; oR[i] = xr * g;
    }
    st.ph = ph; st.g = g;
  }
};

/* STEREO (owner, 19 Oct): VCF, VCA, Sidechain Comp, Blades, Meta and Phaser got In R / Out R. In a patch made before
   (st2 not set) their right sides are patched wherever the left side is: a cable X.Out → one of them adds X.Out R →
   In R, one from their Out → a module with an In R adds Out R → In R (when that In R is free). The editor's normalize
   does the same and saves it (st2); here it only makes an unopened song play in stereo. Shared: …waves's Blades,
   Meta and Phaser got them too (in waves the six-name set only matches those three: its VCF/VCA have no In R). */
function stereoUpCables(d){
  if (!d || d.st2 || !Array.isArray(d.modules) || !Array.isArray(d.cables)) return [];
  const NEW = new Set(['vcf', 'vca', 'sccomp', 'blades', 'meta', 'phaser']), type = new Map(d.modules.map(m => [m && m.id, m && m.type]));
  const port = (id, side, p) => { const M = MODULES[type.get(id)]; return !!(M && M[side] && M[side][p]); };
  const taken = new Set(d.cables.map(c => c && c.to && c.to[0] + '.' + c.to[1])), add = [];
  for (const c of d.cables){
    if (!c || !c.from || !c.to || c.from[1] !== 'out' || c.to[1] !== 'in') continue;
    const a = c.from[0], b = c.to[0];
    if (!NEW.has(type.get(a)) && !NEW.has(type.get(b))) continue;
    if (port(a, 'outputs', 'outR') && port(b, 'inputs', 'inR') && !taken.has(b + '.inR')){ taken.add(b + '.inR'); add.push({from:[a, 'outR'], to:[b, 'inR']}); }
  }
  return add;
}
const SHARED_MODULES = { beads:Beads,
  flanger:FlangerFx, tremolo:TremoloFx, odrive:OdriveFx, folder:FolderFx, autowah:AutowahFx, pshift:PshiftFx, ladder:LadderFx, xmod:XmodFx, space:SpaceFx, lofi:LofiFx, drive:DriveFx, air:AirFx, room:RoomFx,
  comb:CombFx, transient:TransientFx, tgate:TgateFx, ott:OttFx, fshift:FshiftFx, vinyl:VinylFx, dub:DubFx, cab:CabFx, tube:TubeFx,
  envfol:EnvFol, gateex:GateEx, pitchfol:PitchFol, pitchtrk:PitchTrk, slew:Slew, snh:SampleHold, quant:Quant, blades:Blades, meta:Meta, phaser:Phaser, width:Width, pingpong:PingPong,
  meter:Meter, limiter:Limiter, utility:Utility, ringmod:RingMod, spring:Spring, plate:Plate, ensemble:Ensemble, math:MathM, logic:Logic, divider:Divider, euclid:Euclid,
  dimension:Dimension, bbd:BBD, harm:Harmonizer, blipdrum:BlipDrum, life:Life, degrade:Degrade, reson:Reson,
  flex:FlexM, sgate:StepGateM, retrig:RetrigM, pdelay:PDelayM, preverb:PReverbM,
  spectral:Spectral, stretch:XStretch, convolve:Convolve,
  autotune:PitchCorrect, vowel:VowelFilter, rotary:RotarySpk, gdelay:GrainDelay, reverse:ReverseEcho, envfilt:EnvFilter,
  bytebeat:Bytebeat, chaos:Chaos, marbles:MarblesM, tides:TidesM,
  sdelay:SimpleDelay, filter:Filter, cut:LoHiCut, compressor:ClassicComp, ngate:NoiseGate, autopan:AutoPan, pump:Pump };
/* ===SHARED MODULES END=== */

/* ---------- Karplus–Strong string (original code) ----------
   A plucked string: a burst of noise (darkened by Bright, notched by the pick Position) is fed round a delay line one
   period long, losing a little each pass (Decay) and its top end faster (the damping filter, also Bright). The delay
   is shortened by the filter's own delay so it stays in tune. A new pluck adds to a string that's still ringing.
   Self-contained: one copy in the pads' engine (the "Pluck string" layer) and one in …waves (the String module). */
class KSString {
  constructor(sr){ this.sr = sr; this.N = 4096; this.b = new Float32Array(this.N); this.w = 0; this.lp = 0; this.dc = 0; this.dx = 0; this.on = false; }
  // rnd: a function returning 0…1 (the caller's seeded random, so renders repeat exactly)
  pluck(P, bright, pos, amp, rnd){
    P = Math.max(2, Math.min(this.N - 4, P));
    const n = Math.ceil(P) + 2, k = 0.04 + 0.96 * bright * bright, ex = new Float32Array(n); let lp = 0, pk = 1e-9;
    for (let i = 0; i < n; i++){ lp += (rnd() * 2 - 1 - lp) * k; ex[i] = lp; }
    // the pick position: less a copy delayed by pos × period, which notches the harmonics a pluck there wouldn't excite
    const d = Math.max(1, Math.round(pos * P));
    if (pos > 0.01){ const src = ex.slice(), L = Math.max(2, Math.round(P)); for (let i = 0; i < n; i++) ex[i] = src[i] - src[((i - d) % L + L) % L]; }   // round the period, as the string is
    let m = 0; for (let i = 0; i < n; i++) m += ex[i]; m /= n;
    for (let i = 0; i < n; i++){ ex[i] -= m; pk = Math.max(pk, Math.abs(ex[i])); }
    const g = amp / pk, B = this.b, N = this.N;
    for (let i = 0; i < n; i++){ const j = (this.w - n + i + N) % N; B[j] += ex[i] * g; }
    this.on = true;
  }
  // one sample: P the period in samples, t60 the ring time in seconds, a the damping (0…1, 1 = bright)
  tick(P, t60, a){
    const B = this.b, N = this.N; P = Math.max(2, Math.min(N - 4, P));
    const dd = Math.max(1.2, P - (1 - a) / a), r = this.w - dd, ri = Math.floor(r), fr = r - ri;
    const i0 = ri < 0 ? ri + N : ri, i1 = i0 + 1 >= N ? 0 : i0 + 1, x = B[i0] + (B[i1] - B[i0]) * fr;
    this.lp += (x - this.lp) * a;
    if (P !== this.cP || t60 !== this.cT){ this.cP = P; this.cT = t60; this.g = Math.pow(10, -3 * (P / this.sr) / Math.max(0.01, t60)); }   // loss per pass
    const y = this.lp * this.g;
    B[this.w] = y; this.w = this.w + 1 === N ? 0 : this.w + 1;
    const o = y - this.dx + 0.995 * this.dc; this.dx = y; this.dc = o; return o;
  }
}

/* String: a Karplus–Strong plucked string per voice (see KSString). Gate plucks; while the gate is low the string is
   muted by Mute (its ring time cut). Pitch in semitones like the VCO. */
const KString = {
  type:'string', scope:'poly', cost:.3,
  inputs:{ pitch:'cv', gate:'gate' }, outputs:{ out:'audio' },
  params:{ tune:[0, -24, 24, 1], bright:[.6, 0, 1, .01], decay:[.55, 0, 1, .01], pos:[.13, 0, .5, .01], mute:[.4, 0, 1, .01], level:[.8, 0, 1.5, .01] },
  create:sr => ({ ks:new KSString(sr), lg:0, r:rng(nextSeed()) }),
  process(st, io, ctx){
    const p = io.p, sr = ctx.sr, a = .25 + .74 * p.bright, t60 = .08 * Math.pow(100, p.decay), tm = t60 * (1 - p.mute * .97), o = io.out.out, G = io.in.gate, ks = st.ks;
    for (let i = 0; i < ctx.block; i++){
      const P = sr / hzOf(io.in.pitch[i] + p.tune), on = G[i] >= .5;
      if (on && !st.lg) ks.pluck(P, p.bright, p.pos, .9, st.r);
      st.lg = on ? 1 : 0;
      o[i] = ks.tick(P, on ? t60 : tm, a) * p.level;
    }
  }
};

const MODULES = { ...SHARED_MODULES, string:KString, hostIn:HostIn, vco:VCO, fm:FM, additive:Additive, noise:Noise, plaits:Plaits, elements:Elements,
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
    // A poly module's "exit" outputs: audio that leaves the voice for a mono module (the bus, a mono effect). Those are
    // what the engine listens to when deciding that a released voice has gone silent (its own oscillator never does).
    for (const m of this.mods.values()) m.exits = null;
    for (const c of this.cables){ const a = this.mods.get(c.a[0]), b = this.mods.get(c.b[0]); if (!a || !b || !a.poly || b.poly || isParamPort(c.b[1])) continue;
      if (a.def.outputs[c.a[1]] !== 'audio') continue; (a.exits || (a.exits = [])).push(c.a[1]);
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
    this.voices = new VoicePool(opts.maxVoices ?? 8); this.voicesMax0 = this.voices.max;
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
    for (const c of stereoUpCables(doc)) this.connect(c.from, c.to);   // STEREO: an older patch's right sides (the editor saves them)
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
    // Modulation buffers live on io (io.mb, one per parameter, kept; io.mo, the set in use), so no array or object is
    // made per block: that garbage (one per modulated parameter per block) is what paused the audio thread on phones.
    if (!io.mb){ io.mb = {}; io.mo = {}; io.moVer = -1; }
    if (io.moVer !== this.graph.cables.length){ for (const k in io.mo) delete io.mo[k]; io.moVer = this.graph.cables.length; }   // the cables changed: start the set over
    else for (const k in io.mo) io.mo[k].fill(0);
    const N = this.voices.max;
    for (const c of this.graph.inc.get(m.id) || []){
      const src = this.graph.mods.get(c.a[0]); if (!src) continue;
      const port = c.b[1], param = isParamPort(port);
      let dst;
      if (param){
        const k = port.slice(2); if (!(k in m.def.params)) continue;
        mods = mods || io.mo; dst = mods[k] || (mods[k] = io.mb[k] || (io.mb[k] = new Float32Array(BLOCK)));
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
    const ctx = this._ctx, meter = this.telemetryOn === true;
    ctx.block = frames; ctx.quality = this.quality; ctx.notes = this.voices.rows; ctx.transport = this.transport;
    this.voices.now += frames;
    const hold = this.sr * 12, quietFor = this.sr * 0.25;
    for (let v = 0; v < this.voices.max; v++){
      ctx.voice = v;
      const row = this.voices.rows[v], awake = this.voices.awake(v, hold);
      let peak = 0;
      for (const id of this.graph.order){
        const m = this.graph.mods.get(id); if (!m.poly) continue;
        const io = this._bufs(m, v);
        // Silent voices are skipped (their outputs go quiet) to save CPU.
        if (!awake){ if (!io.asleep){ for (const k in io.out) io.out[k].fill(0); io.asleep = true; } continue; }
        io.asleep = false;
        this._gather(m, io, v);
        m.def.process(this._state(m, v), io, ctx);
        if (meter) this._jackPeaks(m.id, io, frames);
        // A released voice: watch its audio outputs, so it can sleep as soon as its envelope and tails have died
        // rather than running (Plaits, Elements, reverbs) for the full 12 s hold after every note.
        if (!row.gate && m.exits) for (const k of m.exits){ const b = io.out[k]; for (let i = 0; i < frames; i += 8){ const a = b[i] < 0 ? -b[i] : b[i]; if (a > peak) peak = a; } }
      }
      if (awake && !row.gate){ if (peak < 1e-5){ row.quiet = (row.quiet || 0) + frames; if (row.quiet >= quietFor) row.offAt = this.voices.now - hold; } else row.quiet = 0; }
      else row.quiet = 0;
    }
    ctx.voice = 0;
    for (const id of this.graph.order){
      const m = this.graph.mods.get(id); if (m.poly) continue;
      const io = this._bufs(m, -1); this._gather(m, io, -1);
      m.def.process(this._state(m, -1), io, ctx);
      if (meter) this._jackPeaks(m.id, io, frames);
    }
  }
  /* Jack meters (only while the editor shows them): per module, the loudest sample on each output and on each patched
     input since the last telemetry, across all its voices. One record per module, kept, so nothing is made per block. */
  _jackPeaks(id, io, n){
    const jp = this._jp || (this._jp = new Map());
    let r = jp.get(id); if (!r){ r = { o:{}, i:{} }; jp.set(id, r); }
    for (const k in io.out){ const b = io.out[k]; let x = r.o[k] || 0; for (let i = 0; i < n; i++){ const a = b[i] < 0 ? -b[i] : b[i]; if (a > x) x = a; } r.o[k] = x; }
    for (const k in io.in){ if (!io.linked.has(k)) continue; const b = io.in[k]; let x = r.i[k] || 0; for (let i = 0; i < n; i++){ const a = b[i] < 0 ? -b[i] : b[i]; if (a > x) x = a; } r.i[k] = x; }
  }
  /* Date.now() is coarse, but its average over many blocks is the real cost. */
  _tickLoad(ms, frames){
    this._busy += ms; this._frames += frames;
    if (this._frames < this.sr / 4) return;
    const l = this._busy / (this._frames / this.sr * 1000); this._busy = 0; this._frames = 0;
    this.load = this.load * .5 + l * .5;
    if (this.load > this.costCeiling) this._shed();
    else this._recover(this.load);
  }
  _shed(){
    let step = 0;
    if (this.voices.max > 4){ this.voices.allOff(); this.voices = new VoicePool(this.voices.max - 1); this._resetPoly(); step = 1; }
    else if (this.quality === 'full'){ this.quality = 'draft'; step = 2; }
    else { const t = this.voices.now; if (t - (this._shed3At || -1e12) < this.sr * 10) return; this._shed3At = t; this.allNotesOff(); step = 3; }   // once, not every half second
    this.load = 0; this._calm = 0;
    if (this.onShed) this.onShed({ step, voices:this.voices.max, quality:this.quality });
  }
  // Below half the ceiling for 8 s: step back up (draft → full first, then a voice at a time, only between notes),
  // so a busy moment doesn't leave the patch degraded for the rest of the session.
  _recover(load){
    if (load > this.costCeiling * .5){ this._calm = 0; return; }
    this._calm = (this._calm || 0) + .25; if (this._calm < 8) return; this._calm = 0;
    const full = this.voicesMax0 || this.voices.max;
    if (this.quality === 'draft') this.quality = 'full';
    else if (this.voices.max < full && !this.voices.rows.some(r => r.gate)){ this.voices = new VoicePool(this.voices.max + 1); this._resetPoly(); }
    else return;
    if (this.onShed) this.onShed({ step:0, voices:this.voices.max, quality:this.quality, recovered:true });
  }
  // The voice count changed: only the per-voice states go (a mono reverb or delay keeps its tail).
  _resetPoly(){ for (const [k, s] of [...this._states]) if (k.includes('#')){ this._destroy(s); this._states.delete(k); } for (const k of [...this._ios.keys()]) if (k.includes('#')) this._ios.delete(k); }
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
    // Jack meters: [module id, 'in' or 'out', port, peak] for every jack that carried anything, then start over.
    const jacks = [];
    if (this._jp) for (const [id, r] of this._jp){
      if (!this.graph.mods.has(id)){ this._jp.delete(id); continue; }
      for (const k in r.o){ const v = r.o[k]; if (v > 1e-4) jacks.push([id, 'out', k, Math.round(Math.min(v, 4) * 1000) / 1000]); r.o[k] = 0; }
      for (const k in r.i){ const v = r.i[k]; if (v > 1e-4) jacks.push([id, 'in', k, Math.round(Math.min(v, 4) * 1000) / 1000]); r.i[k] = 0; }
    }
    return { load:this.load, active, quality:this.quality, buses, mods, shows, jacks, wasm:{ dsp:!!WASM.dsp, tfx:!!WASM.tfxX } };
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
  globalThis.WavesDSP = { WavesEngine, wasmInit, modsPrepare, MODULES, WASM, BUS_COUNT };
}
})();
