/* tools/pump-chains.mjs — the …pump creative effects as …chains modules, from pump/pump-meta.js.
   Writes two generated blocks (run again after pump-meta.js changes; it edits only between the markers):
     chains/chains-worklet.js  /* ===PUMP MODULES START=== … END=== *\/   the DSP: each module runs the plugin's own code
                                                                         in pump/pump.wasm (globalThis.__pumpX)
     chains/index.html         /* ===PUMP EDITOR START=== … END=== *\/    the editor entries: label, knobs, palette group
   Run from the repo root:  node tools/pump-chains.mjs
   Knobs: a choice, switch or whole number is the plugin's own value; a plain range too (step: a thousandth of it); a
   curved one (log or power, e.g. Hz and ms) is 0…1 along the plugin's own knob curve, so cables modulate it evenly.
   Left out: output meters, bypass, listening state, hidden placeholders. CHOP is left out of …chains (its pads are
   played by notes: it is a track effect with NOTES FROM). VOX starts on Split L/R: In L the carrier, In R the voice. */
import fs from 'fs';
import path from 'path';
import url from 'url';
const ROOT = path.resolve(path.dirname(url.fileURLToPath(import.meta.url)), '..');
globalThis.PUMP_META = null;
await import(url.pathToFileURL(path.join(ROOT, 'pump/pump-meta.js')).href);
const M = globalThis.PUMP_META;
const KEYS = ['tube', 'snap', 'gate', 'echo', 'spring', 'verb', 'grain', 'vox', 'tune', 'crush', 'swirl', 'stutter', 'twang'];
const GROUP = {tube:'Drive and lo-fi', crush:'Drive and lo-fi', snap:'Dynamics', gate:'Rhythmic effects', stutter:'Rhythmic effects',
  echo:'Delays', spring:'Reverbs', verb:'Reverbs', grain:'Granular and spectral', vox:'Pitch and voice', tune:'Pitch and voice',
  swirl:'Modulation effects', twang:'Resonators'};
const DEF = {vox:{route:3}};   // VOX: Split L/R (carrier left, voice right), the natural …chains patch
const COST = {tube:1.2, snap:.4, gate:.3, echo:.8, spring:1.2, verb:1.5, grain:1.2, vox:2, tune:1.2, crush:.5, swirl:.8, stutter:.6, twang:1.5};
const SKIP = 8 | 16 | 32 | 64;
const r6 = v => +(+v).toPrecision(7);
const spec = {}, edit = {}, about = {};
for (const k of KEYS){
  const fx = M.fx[k], t = M.types.indexOf(k); if (!fx || t < 0) throw new Error('no ' + k + ' in pump-meta.js');
  const key = 'p' + k;
  const ps = [];
  fx.params.forEach((p, i) => {
    if (p.flags & SKIP) return;
    const whole = !!(p.ch || (p.flags & (2 | 4)));
    const curved = !whole && (p.scale === 'log' || (p.scale === 'pow' && p.skew !== 1));
    let def = DEF[k] && DEF[k][p.s] != null ? DEF[k][p.s] : p.def;
    if (curved){ def = p.scale === 'log' ? Math.log(def / p.min) / Math.log(p.max / p.min) : Math.pow((def - p.min) / (p.max - p.min), 1 / p.skew); def = Math.min(1, Math.max(0, def)); }
    // [symbol, plugin index, 'n' (0…1 on the curve) | 'v' (the value), min, max, scale, skew, default, step]
    ps.push([p.s, i, curved ? 'n' : 'v', r6(p.min), r6(p.max), p.scale, r6(p.skew), r6(def), whole ? 1 : curved ? .001 : r6((p.max - p.min) / 1000)]);
  });
  spec[key] = {t, np:fx.params.length, cost:COST[k] || 1, ps};
  const ui = {};
  for (const q of ps){ const p = fx.params[q[1]];
    ui[q[0]] = {l:p.sn || p.n, u:p.unit, ch:p.ch || null, mn:p.min, fl:p.flags}; }
  edit[key] = {label:'pump ' + fx.name, long:fx.long, ps, ui, group:GROUP[k]};
  about[key] = 'pump ' + fx.name + ' (' + fx.long.toLowerCase() + '), the …pump plugin’s own sound. ' + (fx.about || '') +
    (k === 'vox' ? ' Here it starts on Split L/R: patch the carrier (a synth) into In L and the voice into In R.' : '');
}

const W_START = '/* ===PUMP MODULES START===', W_END = '/* ===PUMP MODULES END=== */\n';
const wblock = `${W_START} (written by tools/pump-chains.mjs from pump/pump-meta.js: edit that, not this)
   The …pump creative effects as modules, each running its plugin's own code in pump/pump.wasm. The bytes reach this
   audio thread as processorOptions.pump (or with a track-effect node in …Thunder Plus, which shares the thread);
   without them a module passes its input through. */
const PUMP_CH = ${JSON.stringify(spec)};
function pumpWasmX(){
  const g = globalThis; if (g.__pumpX) return g.__pumpX; if (!g.__pumpBytes) return null;
  try { const z = () => 0, m = new WebAssembly.Module(g.__pumpBytes);
    const x = new WebAssembly.Instance(m, {wasi_snapshot_preview1:{fd_close:z, fd_seek:z, fd_write:z, proc_exit:z}}).exports;
    if (x._initialize) x._initialize(); g.__pumpX = x; return x; } catch (e){ g.__pumpBytes = null; return null; }
}
function pumpCurve(q, v){ v = v < 0 ? 0 : v > 1 ? 1 : v; return q[5] === 'log' ? q[3] * Math.pow(q[4] / q[3], v) : q[3] + (q[4] - q[3]) * Math.pow(v, q[6] > 0 ? q[6] : 1); }
function pumpModule(key, d){
  const params = {}; for (const q of d.ps) params[q[0]] = [q[7], q[2] === 'n' ? 0 : q[3], q[2] === 'n' ? 1 : q[4], q[8]];
  const value = (q, raw) => { const v = +raw; if (!(v === v)) return q[7]; return q[2] === 'n' ? pumpCurve(q, v) : v; };
  return {
    type:key, scope:'mono', cost:d.cost, effect:true,
    inputs:{ in:'audio', inR:'audio' }, outputs:{ out:'audio', outR:'audio' }, params,
    create(){ return { h:-1, last:new Float64Array(d.ps.length).fill(NaN), pos:0, sn:-1 }; },
    destroy(st){ const x = globalThis.__pumpX; if (x && st.h >= 0) try { x.pump_free(st.h); } catch (e){} st.h = -1; },
    process(st, io, ctx){
      const L0 = io.in.in, R0 = linked(io, 'inR') ? io.in.inR : io.in.in, n = ctx.block, x = st.dead ? null : pumpWasmX();
      if (!x){ io.out.out.set(L0); io.out.outR.set(R0); return; }
      if (st.h < 0){
        const sg = new Float32Array(x.memory.buffer, x.pump_stage(), 512);
        for (let i = 0; i < d.ps.length; i++){ const q = d.ps[i], raw = io.p[q[0]]; sg[q[1]] = value(q, raw); st.last[i] = raw; }
        try { st.h = x.pump_new(d.t, ctx.sr); } catch (e){ st.h = -1; }
        if (st.h < 0){ st.dead = true; io.out.out.set(L0); io.out.outR.set(R0); return; }
      } else {
        let V = null;
        for (let i = 0; i < d.ps.length; i++){ const q = d.ps[i], raw = io.p[q[0]];
          if (raw !== st.last[i]){ st.last[i] = raw; (V || (V = new Float32Array(x.memory.buffer, x.pump_values(st.h), d.np)))[q[1]] = value(q, raw); } }
        if (V) x.pump_commit(st.h);
      }
      // the transport: tempo, playing, and the position since PLAY (the song's own grid when …Thunder Plus plays it)
      const T = ctx.transport || {}, bpm = T.tempo > 0 ? T.tempo : 120;
      if (!T.playing || T.startN !== st.sn){ st.sn = T.startN; st.pos = 0; }
      x.pump_transport(st.h, bpm, st.pos * bpm / 60 / ctx.sr, T.playing ? 1 : 0);
      if (T.playing) st.pos += n;
      if (st.buf !== x.memory.buffer){ st.buf = x.memory.buffer; st.io = [0, 1].map(c => new Float32Array(st.buf, x.pump_io(c), 128)); }
      const L = st.io[0], R = st.io[1];
      for (let i = 0; i < n; i++){ const a = L0[i], b = R0[i], dn = i & 1 ? 1e-18 : -1e-18; L[i] = (a > -8 && a < 8 ? a : 0) + dn; R[i] = (b > -8 && b < 8 ? b : 0) + dn; }
      try { x.pump_process(st.h, n, 0); }
      catch (e){ st.dead = true; io.out.out.set(L0); io.out.outR.set(R0);
        try { console.warn('[thunder] chains pump effect ' + key + ' crashed and is bypassed', e); } catch (_) {} return; }
      if (st.buf !== x.memory.buffer){ st.buf = x.memory.buffer; st.io = [0, 1].map(c => new Float32Array(st.buf, x.pump_io(c), 128)); }
      const oL = st.io[0], oR = st.io[1]; let bad = false;
      for (let i = 0; i < n; i++){ const a = oL[i], b = oR[i]; if (!(a > -8 && a < 8 && b > -8 && b < 8)){ bad = true; break; } }
      if (bad){ try { x.pump_free(st.h); } catch (e){} st.h = -1; st.last.fill(NaN); io.out.out.fill(0); io.out.outR.fill(0); return; }   // gone bad: afresh next block
      io.out.out.set(oL.subarray(0, n)); io.out.outR.set(oR.subarray(0, n));
    }
  };
}
const PUMP_MODULES = {}; for (const k in PUMP_CH) PUMP_MODULES[k] = pumpModule(k, PUMP_CH[k]);
${W_END}`;

const E_START = '/* ===PUMP EDITOR START===', E_END = '/* ===PUMP EDITOR END=== */\n';
const eblock = `${E_START} (written by tools/pump-chains.mjs from pump/pump-meta.js: edit that, not this)
   The …pump creative effects' editor entries (their DSP: chains-worklet.js, PUMP MODULES). Each sits in the palette group
   of its kind, beside …chains' own. */
const PUMP_EDIT = ${JSON.stringify(edit)};
const PUMP_ABOUT = ${JSON.stringify(about)};
function pumpTxt(q, u){
  return v => {
    if (u.ch){ const s = u.ch[Math.round(v - u.mn)]; return s != null ? s : String(v); }
    if (u.fl & 4) return v >= .5 ? 'On' : 'Off';
    if (q[2] === 'n'){ v = Math.min(1, Math.max(0, v)); v = q[5] === 'log' ? q[3] * Math.pow(q[4] / q[3], v) : q[3] + (q[4] - q[3]) * Math.pow(v, q[6] > 0 ? q[6] : 1); }
    const a = Math.abs(v), f = (x, k) => x.toFixed(k);
    switch (u.u){
      case 'dB': return v <= -119.5 ? '-inf' : (v > 0 ? '+' : '') + f(v, a < 9.95 ? 1 : 0) + ' dB';
      case 'Hz': return v >= 1000 ? f(v / 1000, v >= 10000 ? 1 : 2) + ' kHz' : f(v, v >= 100 ? 0 : 1) + ' Hz';
      case 'ms': return v >= 1000 ? f(v / 1000, 2) + ' s' : f(v, v >= 100 ? 0 : v >= 10 ? 1 : 2) + ' ms';
      case '%': return f(v, 0) + '%';
      case 's': return f(v, a >= 10 ? 1 : 2) + ' s';
      case 'x': return f(v, 1) + 'x';
      case 'oct': return f(v, 2) + ' oct';
      case 'ratio': return f(v, 1) + ':1';
      default: return u.fl & 2 ? f(v, 0) : f(v, a >= 100 ? 0 : 2);
    }
  };
}
for (const [k, e] of Object.entries(PUMP_EDIT)){
  const params = {}, ui = {};
  for (const q of e.ps){ const u = e.ui[q[0]];
    params[q[0]] = [q[7], q[2] === 'n' ? 0 : q[3], q[2] === 'n' ? 1 : q[4], q[8]];
    ui[q[0]] = u.ch ? {l:u.l, c:u.ch.map((s, i) => [u.mn + i, s])} : (u.fl & 4) ? {l:u.l, c:[[0, 'Off'], [1, 'On']]} : {l:u.l, f:pumpTxt(q, u)}; }
  MODULES[k] = {label:e.label, fx:true, params, ui, pump:true};
  const g = PAL_LAYOUT.find(r => r[0] === e.group); if (g && g[2] && !g[2].includes(k)) g[2].push(k);
}
${E_END}`;

function put(file, start, end, block, anchor, anchorAfter){
  const f = path.join(ROOT, file); let s = fs.readFileSync(f, 'utf8');
  const a = s.indexOf(start);
  if (a >= 0){ const b = s.indexOf(end, a); if (b < 0) throw new Error(file + ': no end marker'); s = s.slice(0, a) + block + s.slice(b + end.length); }
  else { const i = s.indexOf(anchor); if (i < 0) throw new Error(file + ': no ' + anchor); const at = anchorAfter ? i + anchor.length : i; s = s.slice(0, at) + block + s.slice(at); }
  fs.writeFileSync(f, s);
  console.log(file + ': ' + block.length + ' bytes');
}
put('chains/chains-worklet.js', W_START, W_END, wblock, 'const MODULES = { ...SHARED_MODULES');
put('chains/index.html', E_START, E_END, eblock, 'Object.assign(MODULES, SHARED_MODULES);\n', true);
// their palette labels (organizePalette then files each in its kind's group, as PAL_LAYOUT now lists it)
put('chains/index.html', '/* ===PUMP PALETTE=== */', '/* ===PUMP PALETTE END=== */\n',
  "/* ===PUMP PALETTE=== */ PALETTE.push(['…pump', Object.entries(PUMP_EDIT).map(([k, e]) => [k, e.label + ' · ' + e.long])]); GROUP_FAM['…pump'] = 'afx'; /* ===PUMP PALETTE END=== */\n",
  'mergeShared(PALETTE, GROUP_FAM, PORT_LABEL);\n', false);
