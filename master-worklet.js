/* ═══ ...Thunder Plus mastering: resonance suppressor → compressor → limiter, with meters ═══
   One AudioWorklet so the three stages run in a fixed mastering order on the master bus (the EQ before it is
   native biquads). Settings arrive as {set:{sup, comp, lim}}; meters go back about 20 times a second.
   Every stage is stereo-linked: both channels get the same gain, so the stereo image never shifts.

   Suppressor (in the spirit of Soothe / Gullfoss): a short-time spectrum (2048-point FFT, 75% overlap) is compared
   with a smoothed version of itself. Bins that stick out above that smooth shape by more than Selectivity are
   resonances; each is turned down by Depth times its excess (capped), with per-bin attack and release, then the
   reduction curve is smoothed across frequency (Sharpness: narrow notches or broad dips). Focus limits it to a
   frequency range with soft edges. Delta plays only what is being removed. Latency: 2048 samples.

   Compressor: feed-forward, peak detector on a high-passed sidechain (so the bass doesn't pump everything),
   soft knee, smooth attack/release on the gain, optional program-dependent release (longer when it's been working
   hard), auto make-up, and a parallel Mix.

   Limiter: brickwall with lookahead. Each sample's needed gain (ceiling / peak, with intersample peaks estimated
   at 4x) is held over the lookahead window and box-averaged over it, so the gain is already down when a peak
   arrives and never clicks; release is exponential. A final clamp keeps the output at the ceiling. */

const TAU = Math.PI * 2;
const dbToLin = d => Math.pow(10, d / 20);
const linToDb = x => 20 * Math.log10(Math.max(x, 1e-12));
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;

/* ── FFT (radix-2, in place, complex) ── */
class FFT {
  constructor(n){
    this.n = n; this.rev = new Uint32Array(n); this.cos = new Float64Array(n / 2); this.sin = new Float64Array(n / 2);
    const bits = Math.log2(n) | 0;
    for (let i = 0; i < n; i++){ let r = 0; for (let b = 0; b < bits; b++) r = (r << 1) | ((i >> b) & 1); this.rev[i] = r; }
    for (let i = 0; i < n / 2; i++){ this.cos[i] = Math.cos(TAU * i / n); this.sin[i] = -Math.sin(TAU * i / n); }
  }
  run(re, im, inverse){
    const n = this.n, rev = this.rev;
    for (let i = 0; i < n; i++){ const j = rev[i]; if (j > i){ let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (let size = 2; size <= n; size <<= 1){
      const half = size >> 1, step = n / size;
      for (let i = 0; i < n; i += size){
        for (let j = 0, k = 0; j < half; j++, k += step){
          const wr = this.cos[k], wi = inverse ? -this.sin[k] : this.sin[k];
          const a = i + j, b = a + half;
          const tr = re[b] * wr - im[b] * wi, ti = re[b] * wi + im[b] * wr;
          re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        }
      }
    }
    if (inverse){ for (let i = 0; i < n; i++){ re[i] /= n; im[i] /= n; } }
  }
}

/* ── Resonance suppressor ── */
class Suppressor {
  constructor(sr){
    this.sr = sr; this.N = 2048; this.H = 512; this.K = this.N / 2 + 1;
    this.fft = new FFT(this.N);
    const N = this.N; this.win = new Float32Array(N);
    for (let i = 0; i < N; i++) this.win[i] = Math.sqrt(0.5 - 0.5 * Math.cos(TAU * i / N));   // sqrt-Hann both ways: Hann overall
    this.inL = new Float32Array(N); this.inR = new Float32Array(N);     // the last N input samples (ring)
    this.olaL = new Float32Array(N); this.olaR = new Float32Array(N);   // overlap-add output
    this.outL = new Float32Array(N); this.outR = new Float32Array(N);   // delayed dry for mix / delta
    this.w = 0; this.cnt = 0; this.rd = 0;
    this.reL = new Float64Array(N); this.imL = new Float64Array(N); this.reR = new Float64Array(N); this.imR = new Float64Array(N);
    this.db = new Float32Array(this.K); this.env = new Float32Array(this.K); this.pre = new Float64Array(this.K + 1);
    this.pw = new Float64Array(this.K);   // power per bin, averaged over ~60 ms: steady resonances stand out, noise flicker doesn't
    this.red = new Float32Array(this.K); this.redS = new Float32Array(this.K); this.tmp = new Float32Array(this.K);
    this.fw = new Float32Array(this.K);   // focus weight per bin
    this.p = {depth:.5, sharp:.5, sel:3, atk:8, rel:90, lo:150, hi:12000, mix:1, delta:false};
    this.setFocus();
    // the delayed dry signal lines up with the processed one: both are N samples late
    this.dryL = new Float32Array(N); this.dryR = new Float32Array(N); this.dw = 0;
  }
  set(p){ Object.assign(this.p, p); this.setFocus(); }
  setFocus(){
    const {lo, hi} = this.p, binHz = this.sr / this.N;
    for (let k = 0; k < this.K; k++){
      const f = Math.max(1, k * binHz), a = Math.log2(f / lo), b = Math.log2(hi / f);
      // soft edges, half an octave wide
      this.fw[k] = clamp(a * 2 + 1, 0, 1) * clamp(b * 2 + 1, 0, 1);
    }
  }
  latency(){ return this.N; }
  /* process one block in place */
  process(L, R, n){
    const N = this.N, H = this.H, p = this.p;
    for (let i = 0; i < n; i++){
      // keep the input, and a copy delayed by N for the dry path
      this.inL[this.w] = L[i]; this.inR[this.w] = R[i];
      const dl = this.dryL[this.dw], dr = this.dryR[this.dw]; this.dryL[this.dw] = L[i]; this.dryR[this.dw] = R[i]; this.dw = (this.dw + 1) % N;
      this.w = (this.w + 1) % N;
      // output: the overlap-added wet signal, aligned with dry
      const wl = this.olaL[this.rd], wr = this.olaR[this.rd]; this.olaL[this.rd] = 0; this.olaR[this.rd] = 0; this.rd = (this.rd + 1) % N;
      if (p.delta){ L[i] = dl - wl; R[i] = dr - wr; }
      else { L[i] = dl + (wl - dl) * p.mix; R[i] = dr + (wr - dr) * p.mix; }
      if (++this.cnt >= H){ this.cnt = 0; this.frame(); }
    }
  }
  frame(){
    const N = this.N, K = this.K, p = this.p, w = this.win;
    const reL = this.reL, imL = this.imL, reR = this.reR, imR = this.imR;
    for (let i = 0; i < N; i++){ const j = (this.w + i) % N; reL[i] = this.inL[j] * w[i]; imL[i] = 0; reR[i] = this.inR[j] * w[i]; imR[i] = 0; }
    this.fft.run(reL, imL, false); this.fft.run(reR, imR, false);
    // linked magnitude in dB
    const db = this.db;
    const av = 1 - Math.exp(-(this.H / this.sr) / 0.06);
    for (let k = 0; k < K; k++){ const m = (reL[k] * reL[k] + imL[k] * imL[k] + reR[k] * reR[k] + imR[k] * imR[k]) * 0.5;
      this.pw[k] += (m - this.pw[k]) * av; db[k] = 10 * Math.log10(this.pw[k] + 1e-20); }
    // smooth spectral shape: average in dB over a fractional-octave window (wider = only broad shapes count as "normal")
    const oct = 0.25 + (1 - p.sharp) * 1.25, half = Math.pow(2, oct / 2), pre = this.pre;
    pre[0] = 0; for (let k = 0; k < K; k++) pre[k + 1] = pre[k] + db[k];
    for (let k = 1; k < K; k++){
      const a = Math.max(1, Math.floor(k / half)), b = Math.min(K - 1, Math.ceil(k * half));
      this.env[k] = (pre[b + 1] - pre[a]) / (b - a + 1);
    }
    this.env[0] = this.env[1];
    // how far each bin sticks out above the shape, past Selectivity, weighted by the focus range
    const maxCut = 18 * p.depth + 2, sc = 0.35 + 1.15 * p.depth, tmp = this.tmp;
    for (let k = 0; k < K; k++){ const ex = db[k] - this.env[k] - p.sel; tmp[k] = ex > 0 ? Math.min(maxCut, ex * sc) * this.fw[k] : 0; }
    // spread the reduction a little across neighbouring bins (Sharpness: narrow notches ↔ broad dips)
    const rad = Math.round(1 + (1 - p.sharp) * 5);
    for (let k = 0; k < K; k++){ let m = 0; const a = Math.max(0, k - rad), b = Math.min(K - 1, k + rad);
      for (let j = a; j <= b; j++){ const v = tmp[j] * (1 - Math.abs(j - k) / (rad + 1)); if (v > m) m = v; } this.red[k] = m; }
    // per-bin attack and release, in frames
    const fr = this.H / this.sr, ca = Math.exp(-fr / Math.max(0.001, p.atk / 1000)), cr = Math.exp(-fr / Math.max(0.005, p.rel / 1000));
    for (let k = 0; k < K; k++){ const t = this.red[k], c = t > this.redS[k] ? ca : cr; this.redS[k] = t + (this.redS[k] - t) * c; }
    // apply the gain to both channels (mirror the negative frequencies), back to time, overlap-add
    for (let k = 0; k < K; k++){
      const g = Math.pow(10, -this.redS[k] / 20);
      reL[k] *= g; imL[k] *= g; reR[k] *= g; imR[k] *= g;
      if (k > 0 && k < N / 2){ const m = N - k; reL[m] *= g; imL[m] *= g; reR[m] *= g; imR[m] *= g; }
    }
    this.fft.run(reL, imL, true); this.fft.run(reR, imR, true);
    // sqrt-Hann² = Hann, which sums to 2 at 75% overlap: scale by 0.5
    for (let i = 0; i < N; i++){ const j = (this.rd + i) % N; this.olaL[j] += reL[i] * w[i] * 0.5; this.olaR[j] += reR[i] * w[i] * 0.5; }
  }
  // the reduction now, in dB, as `bands` log-spaced bands from 20 Hz to 20 kHz (for the display)
  bands(n){
    const out = new Float32Array(n), binHz = this.sr / this.N;
    for (let b = 0; b < n; b++){
      const f0 = 20 * Math.pow(1000, b / n), f1 = 20 * Math.pow(1000, (b + 1) / n);
      const k0 = Math.max(1, Math.floor(f0 / binHz)), k1 = Math.min(this.K - 1, Math.max(k0, Math.ceil(f1 / binHz)));
      let m = 0; for (let k = k0; k <= k1; k++) if (this.redS[k] > m) m = this.redS[k]; out[b] = m;
    }
    return out;
  }
}

/* ── Compressor ── */
class Compressor {
  constructor(sr){
    this.sr = sr; this.gr = 0; this.load = 0;
    this.p = {thr:-14, ratio:2, atk:10, rel:150, auto:true, knee:6, makeup:0, automk:true, mix:1, hpf:80};
    this.hp = [{x1:0, x2:0, y1:0, y2:0}, {x1:0, x2:0, y1:0, y2:0}]; this.setHpf();
  }
  set(p){ Object.assign(this.p, p); this.setHpf(); }
  setHpf(){
    // 2nd-order Butterworth high-pass on the detector only
    const f = Math.max(10, this.p.hpf || 10), w = TAU * f / this.sr, c = Math.cos(w), s = Math.sin(w), a = s / (2 * Math.SQRT1_2);
    const a0 = 1 + a; this.hc = {b0:(1 + c) / 2 / a0, b1:-(1 + c) / a0, b2:(1 + c) / 2 / a0, a1:-2 * c / a0, a2:(1 - a) / a0};
    this.hpOn = (this.p.hpf || 0) > 15;
  }
  hpOne(st, x){ const h = this.hc; const y = h.b0 * x + h.b1 * st.x1 + h.b2 * st.x2 - h.a1 * st.y1 - h.a2 * st.y2; st.x2 = st.x1; st.x1 = x; st.y2 = st.y1; st.y1 = y; return y; }
  curve(x){   // static curve: dB in → dB of gain change (≤ 0), soft knee
    const {thr, ratio, knee} = this.p, s = 1 - 1 / ratio, o = x - thr;
    if (knee > 0 && Math.abs(o) <= knee / 2) return -s * (o + knee / 2) * (o + knee / 2) / (2 * knee);
    return o > 0 ? -s * o : 0;
  }
  process(L, R, n){
    const p = this.p, sr = this.sr;
    const ca = Math.exp(-1 / (sr * Math.max(0.0001, p.atk / 1000)));
    const mk = p.automk ? -this.curve(0) * 0.5 + p.makeup : p.makeup, mkLin = dbToLin(mk);
    for (let i = 0; i < n; i++){
      const l = L[i], r = R[i];
      const dl = this.hpOn ? this.hpOne(this.hp[0], l) : l, dr = this.hpOn ? this.hpOne(this.hp[1], r) : r;
      const lv = linToDb(Math.max(Math.abs(dl), Math.abs(dr)));
      const target = this.curve(lv);   // ≤ 0
      if (target < this.gr) this.gr = target + (this.gr - target) * ca;
      else {
        // program-dependent release: longer when it has been working for a while
        const relMs = p.auto ? p.rel * (0.5 + 2.5 * this.load) : p.rel;
        const cr = Math.exp(-1 / (sr * Math.max(0.001, relMs / 1000)));
        this.gr = target + (this.gr - target) * cr;
      }
      this.load += ((this.gr < -1 ? 1 : 0) - this.load) * 0.00002;
      const g = dbToLin(this.gr) * mkLin, m = p.mix;
      L[i] = l * (1 - m) + l * g * m; R[i] = r * (1 - m) + r * g * m;
    }
  }
}

/* ── Limiter ── */
class Limiter {
  constructor(sr){
    this.sr = sr; this.p = {drive:0, ceil:-1, rel:120, look:3, tp:true};
    this.alloc(); this.env = 1; this.grMin = 1;
    this.h = [new Float32Array(4), new Float32Array(4)];   // last 4 samples per channel, for intersample peaks
  }
  set(p){ const old = this.p.look; Object.assign(this.p, p); if (this.p.look !== old) this.alloc(); }
  alloc(){
    const L = this.L = Math.max(1, Math.round(this.sr * this.p.look / 1000));
    this.dl = new Float32Array(L); this.dr = new Float32Array(L); this.di = 0;
    // sliding minimum over L+1 needed gains (monotonic deque), then a running box average over L
    this.qv = new Float32Array(L + 4); this.qi = new Float64Array(L + 4); this.qh = 0; this.qt = 0; this.t = 0;
    this.box = new Float32Array(L); this.bs = L; this.bi = 0; this.box.fill(1);
  }
  latency(){ return this.L; }
  tpPeak(h){
    // Catmull-Rom between h[1] and h[2] at 1/4, 1/2, 3/4 (a 4x oversampled estimate of intersample peaks)
    let m = Math.max(Math.abs(h[1]), Math.abs(h[2]));
    for (const t of [.25, .5, .75]){
      const t2 = t * t, t3 = t2 * t;
      const v = 0.5 * ((2 * h[1]) + (-h[0] + h[2]) * t + (2 * h[0] - 5 * h[1] + 4 * h[2] - h[3]) * t2 + (-h[0] + 3 * h[1] - 3 * h[2] + h[3]) * t3);
      if (Math.abs(v) > m) m = Math.abs(v);
    }
    return m;
  }
  process(L, R, n){
    const p = this.p, ceil = dbToLin(p.ceil), drive = dbToLin(p.drive), Lk = this.L, cap = Lk + 4;
    const cr = Math.exp(-1 / (this.sr * Math.max(0.001, p.rel / 1000)));
    let grMin = 1;
    for (let i = 0; i < n; i++){
      const l = L[i] * drive, r = R[i] * drive;
      let pk = Math.max(Math.abs(l), Math.abs(r));
      if (p.tp){ const a = this.h[0], b = this.h[1]; a[0] = a[1]; a[1] = a[2]; a[2] = a[3]; a[3] = l; b[0] = b[1]; b[1] = b[2]; b[2] = b[3]; b[3] = r;
        pk = Math.max(pk, this.tpPeak(a), this.tpPeak(b)); }
      const need = pk > ceil ? ceil / pk : 1;
      // push into the min deque
      const t = this.t++;
      while (this.qt > this.qh && this.qv[(this.qt - 1) % cap] >= need) this.qt--;
      this.qv[this.qt % cap] = need; this.qi[this.qt % cap] = t; this.qt++;
      while (this.qi[this.qh % cap] <= t - (Lk + 2)) this.qh++;   // one extra: the intersample estimate lags a sample
      const hmin = this.qv[this.qh % cap];
      // box average over L
      this.bs += hmin - this.box[this.bi]; this.box[this.bi] = hmin; this.bi = (this.bi + 1) % Lk;
      const G = Math.min(1, this.bs / Lk);
      this.env = G < this.env ? G : G + (this.env - G) * cr;
      // delay the audio by L so the gain is already down when a peak arrives
      const ol = this.dl[this.di], or = this.dr[this.di]; this.dl[this.di] = l; this.dr[this.di] = r; this.di = (this.di + 1) % Lk;
      let yl = ol * this.env, yr = or * this.env;
      if (yl > ceil) yl = ceil; else if (yl < -ceil) yl = -ceil;
      if (yr > ceil) yr = ceil; else if (yr < -ceil) yr = -ceil;
      L[i] = yl; R[i] = yr;
      if (this.env < grMin) grMin = this.env;
    }
    this.grMin = Math.min(this.grMin, grMin);
  }
}

/* ── Loudness (ITU-R BS.1770 K-weighting, short-term 3 s) ── */
class Loudness {
  constructor(sr){
    this.sr = sr;
    // K-weighting: a high shelf (+4 dB above ~1.5 kHz) then a high-pass at ~38 Hz, coefficients for this rate
    const f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
    const K = Math.tan(Math.PI * f0 / sr), Vh = Math.pow(10, G / 20), Vb = Math.pow(Vh, 0.4996667741545416), a0 = 1 + K / Q + K * K;
    this.s1 = {b0:(Vh + Vb * K / Q + K * K) / a0, b1:2 * (K * K - Vh) / a0, b2:(Vh - Vb * K / Q + K * K) / a0, a1:2 * (K * K - 1) / a0, a2:(1 - K / Q + K * K) / a0};
    const f1 = 38.13547087602444, Q1 = 0.5003270373238773, K1 = Math.tan(Math.PI * f1 / sr), a01 = 1 + K1 / Q1 + K1 * K1;
    this.s2 = {b0:1, b1:-2, b2:1, a1:2 * (K1 * K1 - 1) / a01, a2:(1 - K1 / Q1 + K1 * K1) / a01};
    this.st = [0, 1].map(() => [{x1:0, x2:0, y1:0, y2:0}, {x1:0, x2:0, y1:0, y2:0}]);
    this.blk = Math.round(sr * 0.1); this.acc = 0; this.cnt = 0; this.hist = new Float64Array(30); this.hi = 0; this.filled = 0;
  }
  bq(c, s, x){ const y = c.b0 * x + c.b1 * s.x1 + c.b2 * s.x2 - c.a1 * s.y1 - c.a2 * s.y2; s.x2 = s.x1; s.x1 = x; s.y2 = s.y1; s.y1 = y; return y; }
  process(L, R, n){
    for (let i = 0; i < n; i++){
      const l = this.bq(this.s2, this.st[0][1], this.bq(this.s1, this.st[0][0], L[i]));
      const r = this.bq(this.s2, this.st[1][1], this.bq(this.s1, this.st[1][0], R[i]));
      this.acc += l * l + r * r;
      if (++this.cnt >= this.blk){ this.hist[this.hi] = this.acc / this.cnt; this.hi = (this.hi + 1) % 30; this.filled = Math.min(30, this.filled + 1); this.acc = 0; this.cnt = 0; }
    }
  }
  shortTerm(){ if (!this.filled) return -Infinity; let s = 0; for (let i = 0; i < this.filled; i++) s += this.hist[i]; const ms = s / this.filled; return ms > 0 ? -0.691 + 10 * Math.log10(ms) : -Infinity; }
}

class MasterProcessor extends AudioWorkletProcessor {
  constructor(options){
    super();
    const o = (options && options.processorOptions) || {};
    this.sup = new Suppressor(sampleRate); this.comp = new Compressor(sampleRate); this.lim = new Limiter(sampleRate);
    this.inLu = new Loudness(sampleRate); this.outLu = new Loudness(sampleRate);
    this.on = {sup:false, comp:false, lim:false}; this.meters = o.meters !== false;
    this.L = new Float32Array(128); this.R = new Float32Array(128);
    this.inPk = 0; this.outPk = 0; this.tpMax = 0; this.compGr = 0; this.n = 0;
    if (o.set) this.apply(o.set);
    this.port.onmessage = e => { const d = e.data || {}; if (d.set) this.apply(d.set); if (d.resetPeak) this.tpMax = 0; if (d.dispose) this.dead = true; };
  }
  apply(s){
    if (s.sup){ this.on.sup = !!s.sup.on; this.sup.set(s.sup); }
    if (s.comp){ this.on.comp = !!s.comp.on; this.comp.set(s.comp); }
    if (s.lim){ this.on.lim = !!s.lim.on; this.lim.set(s.lim); }
  }
  process(inputs, outputs){
    if (this.dead) return false;   // replaced by a new chain: let the browser collect this node
    const inp = inputs[0], out = outputs[0]; if (!out || !out.length) return true;
    const n = out[0].length, L = this.L.length === n ? this.L : (this.L = new Float32Array(n)), R = this.R.length === n ? this.R : (this.R = new Float32Array(n));
    const a = inp && inp[0], b = inp && (inp[1] || inp[0]);
    const t0 = Date.now();
    if (a){ L.set(a); R.set(b); } else { L.fill(0); R.fill(0); }
    let ip = 0; for (let i = 0; i < n; i++){ const v = Math.max(Math.abs(L[i]), Math.abs(R[i])); if (v > ip) ip = v; }
    if (this.meters) this.inLu.process(L, R, n);
    if (this.on.sup) this.sup.process(L, R, n);
    if (this.on.comp){ this.comp.process(L, R, n); }
    if (this.on.lim) this.lim.process(L, R, n);
    let op = 0; for (let i = 0; i < n; i++){ const v = Math.max(Math.abs(L[i]), Math.abs(R[i])); if (v > op) op = v; }
    out[0].set(L); if (out[1]) out[1].set(R);
    // Its share of real time, averaged over half a second (Date.now() is coarse), for the CPU meter.
    this.ms = (this.ms || 0) + Date.now() - t0; this.fr = (this.fr || 0) + n;
    if (this.fr >= sampleRate / 2){ this.load = this.ms / (this.fr / sampleRate * 1000); this.ms = 0; this.fr = 0; }
    if (this.meters){
      this.outLu.process(L, R, n);
      this.inPk = Math.max(this.inPk, ip); this.outPk = Math.max(this.outPk, op); this.tpMax = Math.max(this.tpMax, op);
      this.compGr = Math.min(this.compGr, this.on.comp ? this.comp.gr : 0);
      if (++this.n >= Math.round(sampleRate / 128 / 20)){
        this.n = 0;
        this.port.postMessage({meter:{inPk:this.inPk, outPk:this.outPk, tp:this.tpMax, inLu:this.inLu.shortTerm(), outLu:this.outLu.shortTerm(),
          load:this.load || 0, comp:this.compGr, lim:this.on.lim ? linToDb(this.lim.grMin) : 0, sup:this.on.sup ? this.sup.bands(48) : null}});
        this.inPk = 0; this.outPk = 0; this.compGr = 0; this.lim.grMin = 1;
      }
    }
    return true;
  }
}
registerProcessor('thunder-master', MasterProcessor);
