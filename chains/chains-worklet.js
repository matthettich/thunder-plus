/* ═══ …chains audio engine + worklet ═══
   Self-contained, no imports. Standalone, this file is both engine and worklet.
   Phase 1: UI-only clone of …waves for designing tracker FX chains.
   Track In modules are silence in standalone; in Thunder (Phase 2) the host
   fills per-track input buffers before rendering, and Chain Out writes to
   per-track return buses.

   Signals: modulation is roughly −1…1, audio is −1…1. Buses are stereo
   (one stereo bus per tracker track, 16 tracks).

   The WebAssembly effects come from ...Thunder (dsp/tfx.wasm: the effects).
   The page fetches them and hands the bytes over in processorOptions,
   since a worklet can't fetch. */

(() => {   // everything lives in this scope, so a page that loads it keeps its own names
const BLOCK = 128, CHAN_COUNT = 32, PROTOCOL = 2, TARGET = 'chains';

/* ── shared helpers ───────────────────────────────────────────────────────── */
const TS = 4096, SIN = new Float32Array(TS + 1);
for (let i = 0; i <= TS; i++) SIN[i] = Math.sin(i / TS * 2 * Math.PI);
const sinT = p => { p -= Math.floor(p); const x = p * TS, i = x | 0; return SIN[i] + (SIN[i + 1] - SIN[i]) * (x - i); };
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
function rng(seed){ let s = (seed >>> 0) || 1; return () => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; return s / 4294967296; }; }
let seedN = 12345;
const nextSeed = () => (seedN = (seedN * 1103515245 + 12345) >>> 0);
/* Gentle limiter on the speakers: untouched below 0.8, rounds off into ±1 above. */
const soft = x => { const a = Math.abs(x); return a < .8 ? x : Math.sign(x) * (.8 + .2 * Math.tanh((a - .8) / .2)); };
/* Did this input get a cable? (an unpatched input reads 0) */
const linked = (io, name) => io.linked.has(name);

/* ── WebAssembly from ...Thunder ──────────────────────────────────────────── */
const WASI = { wasi_snapshot_preview1: { fd_close: () => 0, fd_seek: () => 0, fd_write: () => 0, proc_exit: () => 0 } };
const WASM = { tfx: null, tfxX: null };
/* o: {tfxBytes} (bytes, compiled here) or {tfxModule} (already compiled, as on a page). */
function wasmInit(o){
  o = o || {};
  try {
    if (o.tfxModule || o.tfxBytes){
      // Compiled once per audio thread and kept: every track's node used to compile its own copy on the audio
      // thread as it was made, a hitch you could hear. Each node still gets its own instance (own memory).
      WASM.tfx = o.tfxModule || (WASM._bytesLen === o.tfxBytes.byteLength && WASM._mod) || new WebAssembly.Module(o.tfxBytes);
      if (!o.tfxModule){ WASM._mod = WASM.tfx; WASM._bytesLen = o.tfxBytes.byteLength; }
      const x = new WebAssembly.Instance(WASM.tfx, WASI).exports;
      if (x._initialize) x._initialize();
      WASM.tfxX = x;
    }
  } catch (e) { WASM.tfx = WASM.tfxX = null; }
}

/* ── module definitions ─────────────────────────────────────────────────────
   create(sr) → state.  process(state, io, ctx) renders one BLOCK.
   io.in[name]/io.out[name] are Float32Array(BLOCK); io.p = live params (with
   modulation applied); io.linked = the inputs that have cables.
   Everything is mono (one shared state): chains are track FX, not voices. */

const VCF = {
  type:'vcf', scope:'mono', cost:1,
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
  type:'vca', scope:'mono', cost:.2,
  inputs:{ in:'audio', cv:'cv' }, outputs:{ out:'audio' },
  params:{ gain:[1,0,4,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const g = io.p.gain, cv = linked(io, 'cv');
    for (let i = 0; i < ctx.block; i++) io.out.out[i] = io.in.in[i] * (cv ? io.in.cv[i] : 1) * g;
  }
};

const Mixer = {
  type:'mixer', scope:'mono', cost:.1,
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
/* Mutable Instruments Beads (MIT): time, size, pitch, shape, density, feedback, blend, reverb + media. */
const Beads = effect('beads', 10, { media:[3,0,3,1], freeze:[0,0,1,1], time:[.2,0,1,.01], size:[.5,0,1,.01], pitch:[.5,0,1,.01], shape:[.5,0,1,.01],
    density:[.85,0,1,.01], fb:[.2,0,1,.01], blend:[1,0,1,.01], verb:[.25,0,1,.01] },
  p => [p.media | 0, 0, p.freeze >= .5 ? 1 : 0, u(p.time), u(p.size), u(p.pitch), u(p.shape), u(p.density), u(p.fb), u(p.blend), u(p.verb), 0], 5);

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
  type:'adsr', scope:'mono', cost:.2,
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
  type:'atten', scope:'mono', cost:.05,
  inputs:{ in:'cv' }, outputs:{ out:'cv' },
  params:{ amount:[1,-1,1,.01], offset:[0,-1,1,.01] },
  create:() => ({}),
  process(st, io, ctx){ const a = io.p.amount, o = io.p.offset; for (let i = 0; i < ctx.block; i++) io.out.out[i] = io.in.in[i] * a + o; }
};

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


/* Track In: hard-assigned tracker track input (stereo). Standalone it is silence;
   in Thunder (Phase 2) the host fills per-track buffers before rendering. */
const TrackIn = {
  type:'trackIn', scope:'mono', cost:.02,
  inputs:{}, outputs:{ out:'audio', outR:'audio' },
  params:{ track:[1,1,CHAN_COUNT,1] },
  create:() => ({}),
  process(st, io, ctx){
    const k = clamp((io.p.track | 0) - 1, 0, CHAN_COUNT - 1);
    const L = ctx._inL ? ctx._inL.subarray(k * BLOCK, (k + 1) * BLOCK) : null;
    const R = ctx._inR ? ctx._inR.subarray(k * BLOCK, (k + 1) * BLOCK) : null;
    if (L && R){ io.out.out.set(L.subarray(0, ctx.block)); io.out.outR.set(R.subarray(0, ctx.block)); }
    else { io.out.out.fill(0); io.out.outR.fill(0); }
  }
};

/* Chain Out: hard-assigned return to the same tracker track (stereo, with level).
   With nothing in Right, In plays on both sides. */
const ChainOut = {
  type:'chainOut', scope:'mono', cost:.05,
  inputs:{ in:'audio', inR:'audio' }, outputs:{},
  params:{ track:[1,1,CHAN_COUNT,1], level:[1,0,2,.01] },
  create:() => ({}),
  process(st, io, ctx){
    const k = clamp((io.p.track | 0) - 1, 0, CHAN_COUNT - 1), g = io.p.level, L = ctx.busL(k), R = ctx.busR(k);
    const inR = linked(io, 'inR') ? io.in.inR : io.in.in;
    for (let i = 0; i < ctx.block; i++){ L[i] += io.in.in[i] * g; R[i] += inR[i] * g; }
  }
};

/* ── Sidechain ──────────────────────────────────────────────────────────────
   The Mixer's Sidechain Out sends a track's audio to a detector bus every chain node receives.
   Sidechain In copies that bus out (audio) and as an envelope (cv), so it can be patched anywhere;
   Sidechain Comp ducks its own input by that bus (or by whatever is patched into its SC input). */
const SidechainIn = {
  type:'scin', scope:'mono', cost:.05,
  inputs:{}, outputs:{ out:'audio', env:'cv' },
  params:{ send:[1,0,1,.01] },
  create:()=>({ env:0 }),
  process(st, io, ctx){
    const L = ctx.scL, R = ctx.scR, g = io.p.send;
    for (let i = 0; i < ctx.block; i++){
      const x = (L[i] + R[i]) * .5 * g, a = Math.abs(x);
      st.env += (a - st.env) * (a > st.env ? .5 : .02);
      io.out.out[i] = x; io.out.env[i] = st.env;
    }
  }
};
const SidechainComp = {
  type:'sccomp', scope:'mono', cost:.2,
  inputs:{ in:'audio', sc:'audio' }, outputs:{ out:'audio' },
  params:{ threshold:[.3,0,1,.001], ratio:[4,1,20,.1], attack:[6,.1,200,.1], release:[140,5,2000,1], makeup:[1,1,4,.01], mix:[1,0,1,.01] },
  create:()=>({ env:0 }),
  process(st, io, ctx){
    const patched = linked(io, 'sc'), busOn = ctx.scOn();
    const thr = -60 + io.p.threshold * 60, rat = Math.max(1, io.p.ratio);
    const ka = 1 - Math.exp(-1 / Math.max(1, io.p.attack / 1000 * ctx.sr)), kr = 1 - Math.exp(-1 / Math.max(1, io.p.release / 1000 * ctx.sr));
    const mk = io.p.makeup, mx = io.p.mix, L = ctx.scL, R = ctx.scR;
    for (let i = 0; i < ctx.block; i++){
      // Detector: the SC input if patched, else the Mixer sidechain bus, else the input itself (plain comp).
      const det = patched ? io.in.sc[i] : (busOn ? (L[i] + R[i]) * .5 : io.in.in[i]);
      const a = Math.abs(det);
      st.env += (a - st.env) * (a > st.env ? ka : kr);
      const over = 20 * Math.log10(st.env + 1e-9) - thr;
      const g = over > 0 ? Math.pow(10, -over * (1 - 1 / rat) / 20) : 1;
      io.out.out[i] = io.in.in[i] * (1 - mx) + io.in.in[i] * g * mk * mx;
    }
  }
};

/* ── Vocoder ────────────────────────────────────────────────────────────────
   Carrier = In (left), Modulator = In R (right). Bands 4/12/16/32/64 use a bank of band-passes; Talkbox
   is a small resonant bank with a little drive for the classic robot-mouth tone; Spectral is an FFT
   cross-synthesis (the carrier's spectrum takes the modulator's magnitude). No synth engine — audio in. */
const FFTN = 256, FFTH = FFTN >> 1;
function fftInPlace(re, im, inv){
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++){ let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit;
    if (i < j){ let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
  for (let len = 2; len <= n; len <<= 1){
    const ang = (inv ? 2 : -2) * Math.PI / len, wr = Math.cos(ang), wi = Math.sin(ang);
    for (let i = 0; i < n; i += len){ let cr = 1, ci = 0;
      for (let j = 0; j < len >> 1; j++){
        const ur = re[i + j], ui = im[i + j], vr = re[i + j + (len >> 1)] * cr - im[i + j + (len >> 1)] * ci, vi = re[i + j + (len >> 1)] * ci + im[i + j + (len >> 1)] * cr;
        re[i + j] = ur + vr; im[i + j] = ui + vi; re[i + j + (len >> 1)] = ur - vr; im[i + j + (len >> 1)] = ui - vi;
        const ncr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = ncr; } } }
  if (inv) for (let i = 0; i < n; i++){ re[i] /= n; im[i] /= n; }
}
const VOC_BANDS = [6, 4, 12, 16, 32, 64, 0];
const VOC_MODES = ['Talkbox', '4 bands', '12 bands', '16 bands', '32 bands', '64 bands', 'Spectral'];
const Vocoder = {
  type:'vocoder', scope:'mono', cost:8,
  inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' },
  params:{ mode:[0,0,6,1], res:[.5,0,1,.01], atk:[2,.1,100,.1], rel:[60,5,1000,1], mix:[1,0,1,.01], level:[1,0,2,.01] },
  create(){ return {
    mc1:new Float32Array(64), mc2:new Float32Array(64), cc1:new Float32Array(64), cc2:new Float32Array(64), env:new Float32Array(64), C:new Float32Array(192),
    win:null, pcar:new Float32Array(FFTH), pmod:new Float32Array(FFTH), acc:new Float32Array(FFTH),
    re:new Float32Array(FFTN), im:new Float32Array(FFTN), mre:new Float32Array(FFTN), mim:new Float32Array(FFTN) }; },
  process(st, io, ctx){
    const mode = clamp(io.p.mode | 0, 0, 6), mix = io.p.mix, lvl = io.p.level;
    const car = io.in.in, mod = linked(io, 'inR') ? io.in.inR : io.in.in, out = io.out.out, outR = io.out.outR;
    if (mode === 6) vocSpectral(st, ctx, car, mod, out, mix, lvl);
    else vocBands(st, io, ctx, VOC_BANDS[mode], car, mod, out, mix, lvl, mode === 0);
    for (let i = 0; i < ctx.block; i++) outR[i] = out[i];
  }
};
// Bank of band-passes: the modulator's band envelope scales the carrier's band. Talkbox = 6 resonant bands + drive.
function vocBands(st, io, ctx, N, car, mod, out, mix, lvl, talk){
  const sr = ctx.sr, nb = Math.min(N, ctx.quality === 'draft' ? 16 : N), n = ctx.block;
  const fmin = 90, fmax = talk ? 4200 : 6800, res = io.p.res;
  const damp = talk ? Math.max(.12, 1.2 - res * 1.05) : Math.max(.28, 1.5 - res * 1.15);
  const ka = 1 - Math.exp(-1 / (Math.max(.1, io.p.atk) / 1000 * sr)), kr = 1 - Math.exp(-1 / (Math.max(1, io.p.rel) / 1000 * sr));
  for (let k = 0; k < nb; k++){
    const fc = Math.min(fmin * Math.pow(fmax / fmin, nb > 1 ? k / (nb - 1) : .5), sr * 0.45);
    const g = Math.tan(Math.PI * fc / sr), a1 = 1 / (1 + g * (g + damp));
    st.C[k * 3] = a1; st.C[k * 3 + 1] = g * a1; st.C[k * 3 + 2] = g * g * a1;
  }
  const g2 = lvl * (talk ? 1.3 : 2.2 / Math.sqrt(nb));
  for (let i = 0; i < n; i++){
    const xm = mod[i], xc = car[i]; let acc = 0;
    for (let k = 0; k < nb; k++){
      const a1 = st.C[k * 3], a2 = st.C[k * 3 + 1], a3 = st.C[k * 3 + 2];
      const m3 = xm - st.mc2[k], m1 = a1 * st.mc1[k] + a2 * m3, m2 = st.mc2[k] + a2 * st.mc1[k] + a3 * m3;
      st.mc1[k] = 2 * m1 - st.mc1[k]; st.mc2[k] = 2 * m2 - st.mc2[k];
      const a = Math.abs(m1); st.env[k] += (a - st.env[k]) * (a > st.env[k] ? ka : kr);
      const c3 = xc - st.cc2[k], c1 = a1 * st.cc1[k] + a2 * c3, c2 = st.cc2[k] + a2 * st.cc1[k] + a3 * c3;
      st.cc1[k] = 2 * c1 - st.cc1[k]; st.cc2[k] = 2 * c2 - st.cc2[k];
      acc += c1 * st.env[k];
    }
    let y = acc * g2; if (talk) y = Math.tanh(y * 1.6) * .625;
    out[i] = car[i] * (1 - mix) + y * mix;
  }
}
// Spectral vocoder: 256-point FFT, 50% overlap. The carrier's bins take the modulator's magnitude.
function vocSpectral(st, ctx, car, mod, out, mix, lvl){
  if (!st.win){ st.win = new Float32Array(FFTN); for (let i = 0; i < FFTN; i++) st.win[i] = .5 - .5 * Math.cos(2 * Math.PI * i / FFTN); }
  const re = st.re, im = st.im, mre = st.mre, mim = st.mim, w = st.win;
  for (let i = 0; i < FFTN; i++){ const v = i < FFTH ? st.pcar[i] : car[i - FFTH]; re[i] = v * w[i]; im[i] = 0;
    const u = i < FFTH ? st.pmod[i] : mod[i - FFTH]; mre[i] = u * w[i]; mim[i] = 0; }
  fftInPlace(re, im, false); fftInPlace(mre, mim, false);
  for (let b = 0; b < FFTN; b++){
    const cmag = Math.hypot(re[b], im[b]), mmag = Math.hypot(mre[b], mim[b]), s = mmag / (cmag + 1e-6);
    re[b] *= s; im[b] *= s;
  }
  fftInPlace(re, im, true);
  const g = lvl;
  for (let i = 0; i < FFTH; i++){
    const norm = w[i] * w[i] + w[i + FFTH] * w[i + FFTH];
    const dry = st.pcar[i];
    const wet = norm > 1e-8 ? (st.acc[i] + re[i] * w[i]) / norm : 0;
    out[i] = dry * (1 - mix) + wet * g * mix;
  }
  for (let i = 0; i < FFTH; i++) st.acc[i] = re[FFTH + i] * w[FFTH + i];
  for (let i = 0; i < FFTH; i++){ st.pcar[i] = car[i]; st.pmod[i] = mod[i]; }
}

const MODULES = { trackIn:TrackIn,
  vocoder:Vocoder,
  vcf:VCF, vca:VCA, mixer:Mixer,
  eq:EQ, comp:Comp, saturation:Saturation, tape:Tape, chorus:Chorus, crush:Crush, delay:Delay, reverb:Reverb, rings:Rings, clouds:Clouds, beads:Beads,
  scin:SidechainIn, sccomp:SidechainComp,
  lfo:LFO, adsr:ADSR, random:Random, atten:Attenuverter,
  envfol:EnvFol, gateex:GateEx, pitchfol:PitchFol, slew:Slew, snh:SampleHold, quant:Quant,
  blades:Blades, meta:Meta, phaser:Phaser, width:Width, pingpong:PingPong, chainOut:ChainOut };

/* A cable into "p:name" modulates that parameter instead of feeding an input. */
const isParamPort = p => typeof p === 'string' && p.startsWith('p:');
class Graph {
  constructor(){ this.mods = new Map(); this.cables = []; this.order = []; this.inc = new Map(); this.ver = 0; }
  add(id, def, inst){ this.mods.set(id, { id, def, inst }); this.compile(); }
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
    this.order = order; this.ver++;
  }
}

/* ── the engine ───────────────────────────────────────────────────────────── */
class ChainsEngine {
  constructor(sampleRate, opts = {}){
    this.sr = sampleRate;
    this.transport = Object.assign({ tempo:120, lpb:4, ticks:6, ppq:0, playing:false }, opts.transport || {});
    this.quality = opts.quality || 'full';
    this.graph = new Graph();
    this.busesL = new Float32Array(CHAN_COUNT * BLOCK); this.busesR = new Float32Array(CHAN_COUNT * BLOCK);
    this.load = 0; this.telemetryOn = false;
    this._ios = new Map(); this._states = new Map();
    this._busy = 0; this._frames = 0;
    /* Phase 2: the host sets trackInL/trackInR so Track In modules read tracker audio. */
    this.trackInL = null; this.trackInR = null;
    /* Sidechain: a stereo detector bus the host feeds from the tracks' Sidechain Outs. Modules read it
       through ctx (Sidechain In copies it out; Sidechain Comp uses it when nothing is patched). */
    this.scL = new Float32Array(BLOCK); this.scR = new Float32Array(BLOCK); this.scOn = false;
    /* focus: in Thunder each track's node only runs the modules that lead to a Chain Out for ITS track (and what
       modulates them). It used to run the whole patch, so 4 tracks on …chains did 4× every track's effects. -1 = all. */
    this.focus = opts.focus == null ? -1 : opts.focus; this._act = null; this._actKey = '';
    this._ctx = { sr:sampleRate, block:BLOCK, quality:this.quality, transport:this.transport, engine:this,
      trackInL:k => this.trackInL ? this.trackInL.subarray(k * BLOCK, (k + 1) * BLOCK) : null,
      trackInR:k => this.trackInR ? this.trackInR.subarray(k * BLOCK, (k + 1) * BLOCK) : null,
      busL:k => this.busesL.subarray(k * BLOCK, (k + 1) * BLOCK), busR:k => this.busesR.subarray(k * BLOCK, (k + 1) * BLOCK),
      scL:this.scL, scR:this.scR, scOn:() => this.scOn };
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
  setParam(id, name, v){ const m = this.graph.mods.get(id); if (m && typeof v === 'number'){ m.inst.params[name] = v; if (name === 'track') this._actKey = ''; } }
  /* The modules this node has to run (null: all of them). */
  _active(){
    if (this.focus < 0) return null;
    const g = this.graph, key = g.ver + ':' + g.mods.size;
    if (this._act && this._actKey === key) return this._act;
    const act = new Set(), q = [];
    for (const m of g.mods.values()) if (m.def === ChainOut && clamp((m.inst.params.track | 0) - 1, 0, CHAN_COUNT - 1) === this.focus){ act.add(m.id); q.push(m.id); }
    while (q.length){ for (const c of g.inc.get(q.pop()) || []) if (!act.has(c.a[0])){ act.add(c.a[0]); q.push(c.a[0]); } }
    this._act = act; this._actKey = key; return act;
  }
  setParams(list){ for (const [id, name, v] of list) this.setParam(id, name, v); }
  setTransport(t){ Object.assign(this.transport, t); }
  /* Keep every module's state (filter memories, reverb tails) when the patch changes. */
  _sync(){
    for (const id of [...this._ios.keys()]) if (!this.graph.mods.has(id)) this._ios.delete(id);
    for (const [id, s] of this._states) if (!this.graph.mods.has(id)){ this._destroy(s); this._states.delete(id); }
  }
  _drop(id){ const s = this._states.get(id); if (s){ this._destroy(s); this._states.delete(id); } this._ios.delete(id); }
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
  setTrackInputs(L, R){ this.trackInL = L; this.trackInR = R; }
  /* The host feeds the sidechain bus here each block (stereo; n frames). scOn says whether any track is
     sending, so a Sidechain Comp with nothing patched falls back to its own input when nothing is sent. */
  setSidechain(L, R, n){
    n = Math.min(BLOCK, n || BLOCK);
    this.scL.fill(0); this.scR.fill(0);
    if (L) this.scL.set(L.subarray(0, n));
    if (R) this.scR.set(R.subarray(0, n)); else if (L) this.scR.set(L.subarray(0, n));
    this.scOn = !!L;
  }
  process(frames = BLOCK){
    const t0 = Date.now();
    this.busesL.fill(0); this.busesR.fill(0); this._render(frames);
    this._tickLoad(Date.now() - t0, frames);
  }
  bus(slot){ return this.busesL.subarray((slot - 1) * BLOCK, slot * BLOCK); }
  busR(slot){ return this.busesR.subarray((slot - 1) * BLOCK, slot * BLOCK); }

  _bufs(m){
    let io = this._ios.get(m.id);
    if (!io){
      io = { in:{}, out:{}, base:m.inst.params, p:m.inst.params, pm:null, linked:new Set(), mods:null };
      for (const n in m.def.inputs) io.in[n] = new Float32Array(BLOCK);
      for (const n in m.def.outputs) io.out[n] = new Float32Array(BLOCK);
      this._ios.set(m.id, io);
    }
    return io;
  }
  _state(m){
    let s = this._states.get(m.id);
    if (!s){ s = { st:m.def.create(this.sr), def:m.def }; this._states.set(m.id, s); }
    return s.st;
  }
  /* Sum every cable into this module's inputs, and work out its modulated parameters. */
  _gather(m, io){
    for (const k in io.in) io.in[k].fill(0);
    io.linked.clear();
    let mods = null;
    for (const c of this.graph.inc.get(m.id) || []){
      const src = this.graph.mods.get(c.a[0]); if (!src) continue;
      const port = c.b[1], param = isParamPort(port);
      let dst;
      if (param){
        const k = port.slice(2); if (!(k in m.def.params)) continue;
        mods = mods || {}; dst = mods[k] || (mods[k] = new Float32Array(BLOCK));
      } else { dst = io.in[port]; if (!dst) continue; io.linked.add(port); }
      const b = this._bufs(src).out[c.a[1]];
      if (b) for (let i = 0; i < BLOCK; i++) dst[i] += b[i];
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
    ctx.block = frames; ctx.quality = this.quality; ctx.transport = this.transport;
    ctx._inL = this.trackInL; ctx._inR = this.trackInR;
    const act = this._active();
    for (const id of this.graph.order){
      if (act && !act.has(id)) continue;
      const m = this.graph.mods.get(id);
      const io = this._bufs(m); this._gather(m, io);
      m.def.process(this._state(m), io, ctx);
    }
  }
  /* Date.now() is coarse, but its average over many blocks is the real cost. */
  _tickLoad(ms, frames){
    this._busy += ms; this._frames += frames;
    if (this._frames < this.sr / 4) return;
    const l = this._busy / (this._frames / this.sr * 1000); this._busy = 0; this._frames = 0;
    this.load = this.load * .5 + l * .5;
    if (this.load > .95 && this.quality === 'full'){ this.quality = 'draft'; this.load = 0; if (this.onShed) this.onShed({ quality:this.quality }); }
  }
  telemetry(){
    const buses = [];
    for (let k = 0; k < CHAN_COUNT; k++){
      let peak = 0;
      for (let i = 0; i < BLOCK; i++){ const a = Math.abs(this.busesL[k * BLOCK + i]), b = Math.abs(this.busesR[k * BLOCK + i]); if (a > peak) peak = a; if (b > peak) peak = b; }
      buses.push(peak);
    }
    // The live value of every modulated parameter, so the page can show it.
    const mods = [];
    for (const m of this.graph.mods.values()){
      const io = this._ios.get(m.id); if (!io || !io.mods) continue;
      for (const k in io.mods) mods.push([m.id, k, io.pm[k]]);
    }
    return { load:this.load, quality:this.quality, buses, mods, wasm:{ tfx:!!WASM.tfxX } };
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
    }
  }
}

/* ── protocol ─────────────────────────────────────────────────────────────── */
const OP = { patch:'patch', add:'add', remove:'remove', connect:'connect', disconnect:'disconnect',
  params:'params', quality:'quality', transport:'transport', telemetry:'telemetry',
  ready:'ready', meter:'meter', shed:'shed', error:'error' };

/* ── the worklet wrapper ──────────────────────────────────────────────────────
   Only inside an AudioWorklet. Loaded as a plain <script> on a page, the engine
   above is all there is: ChainsEngine, wasmInit, MODULES. */
if (typeof AudioWorkletProcessor !== 'undefined'){
class ChainsProcessor extends AudioWorkletProcessor {
  constructor(options){
    super();
    const o = options.processorOptions || {};
    wasmInit(o);
    this.engine = new ChainsEngine(sampleRate, {
      quality:o.quality ?? 'full',
      transport:Object.assign({ tempo:120, lpb:4, ticks:6, playing:false }, o.transport || {})
    });
    this.engine.onShed = info => this.port.postMessage({ target:TARGET, op:OP.shed, value:info });
    this._blocks = 0;
    this.port.onmessage = e => this._recv(e.data);
    this.port.postMessage({ target:TARGET, op:OP.ready, value:{ protocol:PROTOCOL, sr:sampleRate, block:BLOCK, wasm:{ tfx:!!WASM.tfxX } } });
  }
  _recv(m){
    if (!m || (m.target && m.target !== TARGET)) return;
    try { this.engine.handleMessage(m); }
    catch (err){ this.port.postMessage({ target:TARGET, op:OP.error, value:{ op:m.op, message:String(err?.message || err) } }); }
  }
  /* Standalone, every chain bus plays through the speakers; in Thunder each bus is its own track return. */
  process(_ins, outs){
    const e = this.engine;
    { const sc = _ins && _ins[1]; e.setSidechain(sc && sc[0], sc && (sc[1] || sc[0]), BLOCK); }
    try { e.process(BLOCK); }
    catch (err){ e.busesL.fill(0); e.busesR.fill(0); if ((this._blocks & 255) === 0) this.port.postMessage({ target:TARGET, op:OP.error, value:{ op:'process', message:String(err?.message || err) } }); }
    const out = outs[0], L = out[0], R = out[1] || out[0];
    L.fill(0); if (R !== L) R.fill(0);
    for (let k = 0; k < CHAN_COUNT; k++){
      const o = k * BLOCK;
      for (let i = 0; i < BLOCK; i++){ L[i] += e.busesL[o + i]; if (R !== L) R[i] += e.busesR[o + i]; }
    }
    for (let i = 0; i < BLOCK; i++){ L[i] = soft(L[i]); if (R !== L) R[i] = soft(R[i]); }
    if (e.telemetryOn && (++this._blocks % 24) === 0) this.port.postMessage({ target:TARGET, op:OP.meter, value:e.telemetry() });
    else if (!e.telemetryOn) this._blocks++;
    return true;
  }
}
registerProcessor('chains', ChainsProcessor);

/* ── Thunder host wrapper: one node per track ───────────────────────────────
   Phase 2 routing: Thunder inserts one ThunderChains node per track as
     inp -> ThunderChains -> fx -> g
   with real track audio on its stereo input. Track In t reads that input
   (hard-assigned: node for track t reads input, ignoring other tracks'
   Track In modules); every Chain Out t sums into output channel pair t.
   Anything after the track in the chain (native track FX, fader, mute/solo,
   pan, master) is untouched Thunder code downstream of this node. */
class ThunderChains extends AudioWorkletProcessor {
  constructor(options){
    super();
    const o = options.processorOptions || {};
    // The host hands the tfx bytes over as `bytes` (same convention as native thunder-tfx).
    wasmInit({ tfxBytes:o.bytes || o.tfxBytes, tfxModule:o.tfxModule });
    this.track = Math.max(0, Math.min(CHAN_COUNT - 1, (o.track | 0) || 0));
    this.engine = new ChainsEngine(sampleRate, {
      focus:this.track,
      quality:o.quality ?? 'full',
      transport:Object.assign({ tempo:120, lpb:4, ticks:6, playing:false }, o.transport || {})
    });
    if (o.patch && typeof o.patch === 'object'){
      try { this.engine.loadPatch(o.patch); } catch (err){}
    }
    this.engine.onShed = info => this.port.postMessage({ target:'chains-host', op:OP.shed, value:info });
    this._tmpL = new Float32Array(CHAN_COUNT * BLOCK);
    this._tmpR = new Float32Array(CHAN_COUNT * BLOCK);
    this._blocks = 0;
    this.port.onmessage = e => this._recv(e.data);
    this.port.postMessage({ target:'chains-host', op:OP.ready,
      value:{ protocol:PROTOCOL, sr:sampleRate, block:BLOCK, track:this.track, wasm:{ tfx:!!WASM.tfxX } } });
  }
  _recv(m){
    if (!m || (m.target && m.target !== 'chains-host')) return;
    try {
      if (m.op === 'dispose'){
        this.engine.dispose();
        this.port.postMessage({ target:'chains-host', op:'disposed', value:{ track:this.track } });
        return;
      }
      this.engine.handleMessage(m);
    }
    catch (err){ this.port.postMessage({ target:'chains-host', op:OP.error, value:{ op:m.op, message:String(err?.message || err) } }); }
  }
  process(ins, outs){
    const e = this.engine, i = ins[0], o = outs[0], n = o[0].length;
    const L = o[0], R = o[1] || o[0];
    // Feed this track's input into the engine's per-track buffers.
    this._tmpL.fill(0); this._tmpR.fill(0);
    if (i && i.length){ this._tmpL.set(i[0].subarray(0, n), this.track * BLOCK); this._tmpR.set((i[1] || i[0]).subarray(0, n), this.track * BLOCK); }
    e.setTrackInputs(this._tmpL, this._tmpR);
    { const sc = ins[1]; e.setSidechain(sc && sc[0], sc && (sc[1] || sc[0]), n); }
    try { e.process(BLOCK); }
    catch (err){ e.busesL.fill(0); e.busesR.fill(0); if ((this._blocks & 255) === 0) this.port.postMessage({ target:'chains-host', op:OP.error, value:{ op:'process', message:String(err?.message || err) } }); }
    // Return this track's bus (the sum of its Chain Out modules) downstream.
    const bL = e.busesL.subarray(this.track * BLOCK, this.track * BLOCK + n);
    const bR = e.busesR.subarray(this.track * BLOCK, this.track * BLOCK + n);
    L.set(bL); if (R !== L) R.set(bR);
    if (e.telemetryOn && (++this._blocks % 24) === 0) this.port.postMessage({ target:'chains-host', op:OP.meter, value:e.telemetry() });
    else if (!e.telemetryOn) this._blocks++;
    return true;
  }
}
registerProcessor('thunder-chains', ThunderChains);
} else if (typeof globalThis !== 'undefined'){
  globalThis.ChainsDSP = { ChainsEngine, wasmInit, MODULES, WASM, CHAN_COUNT };
}
})();
