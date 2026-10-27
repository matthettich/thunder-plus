/* pump/synth/pump-synth-worklet.js: the …pump instruments played live (Mono live, Poly live, Coil live), as an
 * AudioWorklet processor. One node per track and pad; one pump-synth.wasm instance per audio thread, shared by the nodes
 * (each node is a handle in it), compiled from the bytes the first node brings.
 *
 * Notes arrive the way the other live engines' do (Bass, Acid, Machine): AudioParam automation, so a render in an
 * OfflineAudioContext (song export) plays them at the same samples as live. Four lanes (a poly track's four notes; a mono
 * track uses lane 0), each with r (playback rate against the pad's pitch, as for a rendered pad), g (gate), t (a counter
 * that steps on every note), l (legato: a slide into this note), v (velocity, 1-127).
 *
 * Here they become MIDI for the plugin: a lane's note on is the note nearest its pitch (60 + the pad's Tune + 12 log2 r,
 * so C-4 plays the plugin's 60, as the rendered engines do), with a legato note sent before the old one's note off (MONO
 * then doesn't retrigger); the pitch between notes (slides, bends, vibrato, arpeggios, Tune's fraction) is pitch bend
 * from the lane that played last, over ±24 semitones (COIL: ±12, its most). Bend is one per instrument in MIDI, so on a
 * poly track the chord bends together, as the tracker's pitch commands move a whole track.
 */
const LANES = 4;
const WASI = {wasi_snapshot_preview1:{fd_close:() => 0, fd_seek:() => 0, fd_write:() => 0, proc_exit:() => 0}};
function psynX(){
  const g = globalThis; if (g.__psynX) return g.__psynX; if (!g.__psynBytes || g.__psynFailed) return null;
  try { const x = new WebAssembly.Instance(new WebAssembly.Module(g.__psynBytes), WASI).exports; if (x._initialize) x._initialize(); g.__psynX = x; return x; }
  catch (e){ g.__psynFailed = true; return null; }
}
class PsynProc extends AudioWorkletProcessor {
  static get parameterDescriptors(){
    const d = [];
    for (let j = 0; j < LANES; j++) d.push(
      {name:'r' + j, defaultValue:1, minValue:0, maxValue:256, automationRate:'a-rate'},
      {name:'g' + j, defaultValue:0, minValue:0, maxValue:1, automationRate:'a-rate'},
      {name:'t' + j, defaultValue:0, automationRate:'a-rate'},
      {name:'l' + j, defaultValue:0, minValue:0, maxValue:1, automationRate:'a-rate'},
      {name:'v' + j, defaultValue:100, minValue:1, maxValue:127, automationRate:'a-rate'});
    return d;
  }
  constructor(o){
    super();
    const op = o.processorOptions || {};
    if (op.bytes && !globalThis.__psynBytes) globalThis.__psynBytes = op.bytes;
    this.type = op.type | 0; this.syms = op.syms || []; this.fixed = op.fixed || {}; this.p = op.p || {}; this.trim = op.trim || 1;   // trim: as the render's (POLY +8 dB)
    this.range = this.type === 2 ? 12 : 24;   // the bend range the instance is set to (COIL's Bend goes to 12)
    this.h = -1; this.dirty = false; this.dead = false;
    this.lane = Array.from({length:LANES}, () => ({note:-1, lt:0, lg:false}));
    this.bl = 0; this.sent = 8192; this.quiet = 1e9; this.ms = 0; this.fr = 0;
    this.port.onmessage = e => { const d = e.data || {}; if (d.set){ this.p = d.set; this.dirty = true; } if (d.dispose) this.dead = true; };
  }
  values(arr){
    for (let i = 0; i < this.syms.length; i++){ const s = this.syms[i], f = this.fixed[s], v = f != null ? f : this.p[s]; if (typeof v === 'number' && isFinite(v)) arr[i] = v; }
  }
  open(X){
    const st = new Float32Array(X.memory.buffer, X.psyn_stage(), 512);
    for (let i = 0; i < this.syms.length; i++) st[i] = 0;
    this.values(st);
    this.h = X.psyn_new(this.type, sampleRate); this.dirty = false;
  }
  semis(r){ return (+this.p.tune || 0) + 12 * Math.log2(r > 1e-6 ? r : 1e-6); }
  bendOf(r, note){ const b = 60 + this.semis(r) - note, k = Math.max(-1, Math.min(1, b / this.range)); return Math.max(0, Math.min(16383, Math.round(8192 + k * 8191))); }
  bend(X, i, r, note){ const v = this.bendOf(r, note); if (v !== this.sent){ X.psyn_midi(this.h, i, 0xE0, v & 127, v >> 7); this.sent = v; } }
  process(ins, outs, P){
    const X = psynX(), o = outs[0], L = o[0], R = o[1] || o[0], n = L.length;
    if (this.dead){ if (X && this.h >= 0) X.psyn_free(this.h); this.h = -1; return false; }
    if (!X){ L.fill(0); if (R !== L) R.fill(0); return true; }
    const t0 = Date.now();
    if (this.h < 0){ this.open(X); if (this.h < 0){ L.fill(0); if (R !== L) R.fill(0); return true; } }
    if (this.dirty){ const v = new Float32Array(X.memory.buffer, X.psyn_values(this.h), this.syms.length); this.values(v); X.psyn_commit(this.h); this.dirty = false; }
    const at = (A, i) => A.length > 1 ? A[i] : A[0];
    let any = false, bfrom = 0;   // bfrom: where the bend lane's note started in this block (bend before it is the old note's)
    for (let j = 0; j < LANES; j++){
      const ln = this.lane[j], T = P['t' + j], G = P['g' + j];
      // nothing changes in this block on this lane (both constant and as before): skip the per-sample scan
      if (T.length === 1 && G.length === 1 && T[0] === ln.lt && (G[0] >= 0.5) === ln.lg) continue;
      for (let i = 0; i < n; i++){
        const tg = at(T, i), ga = at(G, i) >= 0.5;
        if (tg === ln.lt && ga === ln.lg) continue;
        const r = at(P['r' + j], i);
        if ((tg !== ln.lt && ga) || (ga && !ln.lg)){
          const leg = tg !== ln.lt && ln.lg && at(P['l' + j], i) >= 0.5, note = Math.max(0, Math.min(127, 60 + Math.round(this.semis(r)))), vel = Math.max(1, Math.min(127, Math.round(at(P['v' + j], i))));
          const old = ln.note;
          if (old >= 0 && !leg) X.psyn_midi(this.h, i, 0x80, old, 0);
          this.bl = j; bfrom = i; this.bend(X, i, r, note);
          X.psyn_midi(this.h, i, 0x90, note, vel);
          if (old >= 0 && leg && old !== note) X.psyn_midi(this.h, i, 0x80, old, 0);
          ln.note = note; any = true;
        } else if (!ga && ln.lg){ if (ln.note >= 0) X.psyn_midi(this.h, i, 0x80, ln.note, 0); ln.note = -1; }
        ln.lt = tg; ln.lg = ga;
      }
    }
    // pitch between notes: the last-played lane's rate as bend, every 32 samples (COIL retunes its springs on each
    // bend, so it gets one a block and only for a change worth hearing)
    const bl = this.lane[this.bl], rr = P['r' + this.bl];
    if (bl.note >= 0){
      if (this.type === 2){ const v = this.bendOf(at(rr, bfrom), bl.note); if (Math.abs(v - this.sent) > 6){ X.psyn_midi(this.h, bfrom, 0xE0, v & 127, v >> 7); this.sent = v; } }
      else if (rr.length > 1) for (let i = bfrom; i < n; i += 32) this.bend(X, i, rr[i], bl.note);
      else this.bend(X, bfrom, rr[0], bl.note);
    }
    // asleep: no note held and the output silent for a second (a phone's battery): skip the engine until a note comes
    const held = this.lane.some(l => l.note >= 0);
    if (!held && !any && this.quiet > sampleRate){ L.fill(0); if (R !== L) R.fill(0); return true; }
    X.psyn_process(this.h, n);
    const oL = new Float32Array(X.memory.buffer, X.psyn_out(0), n), oR = new Float32Array(X.memory.buffer, X.psyn_out(1), n);
    let pk = 0;
    // the pad's Level and Drive, as the render applies them (finish() in pump-synth-engine.js), and the trim
    const tr = this.trim * Math.pow(10, (+this.p.level || 0) / 20), dv = +this.p.drive || 0, dr = dv > 0.001 ? 1 + dv * dv * 10 : 0, dn = dr ? 1 / Math.tanh(dr) : 1;
    for (let i = 0; i < n; i++){
      let a = oL[i] * tr, b = oR[i] * tr; if (dr){ a = Math.tanh(a * dr) * dn; b = Math.tanh(b * dr) * dn; }
      L[i] = a; if (R !== L) R[i] = b; const m = (a < 0 ? -a : a) + (b < 0 ? -b : b); if (m > pk) pk = m; }
    this.quiet = held || pk > 2e-5 ? 0 : this.quiet + n;
    this.ms += Date.now() - t0; this.fr += n;
    if (this.fr >= sampleRate / 2){ this.port.postMessage({load:this.ms / (this.fr / sampleRate * 1000)}); this.ms = 0; this.fr = 0; }
    return true;
  }
}
registerProcessor('thunder-psyn', PsynProc);
