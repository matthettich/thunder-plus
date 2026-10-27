/* pump/synth/pump-synth-engine.js: …pump's instruments MONO, POLY and COIL as Thunder Plus synth engines.
 *
 * The sound is the plugins' own DSP (pump-synth.wasm, built by pump/wasm/build.sh from pump/wasm/pump_synth.cpp, the
 * plugins' engines without DPF). Their parameters and factory presets come from pump-synth-meta.js, generated from the
 * same Params.hpp tables the plugins use, so a pad and the plugin with the same values make the same sound (parity with
 * the VST / Android plans). A pad keeps the plugin's own parameter symbols and values, plus `vel` (how hard the note is
 * played when it renders).
 *
 * Rendered, as the LXR engines are: a pad's note is played into a fresh instance (MIDI note 60 + the pad's Tune, which
 * is C-4 in the tracker, as a DAW playing the plugin), held for the pad's length, released, and rendered until it has
 * died away; the pad then plays that recording re-pitched for other notes. The stereo output is folded to mono (pads are
 * mono). The page and the render Worker both load this file (after pump-synth-meta.js); without the folder Thunder Plus
 * runs as before, with no …pump engines.
 */
(function(G){
'use strict';
const META = G.PUMP_SYNTH_META;
if (!META || !META.synth) return;

/* engine key in Thunder Plus -> the instrument (type id in pump-synth.wasm, its meta) */
const KINDS = {
  pmono:{t:0, k:'mono', name:'Mono', title:'pump MONO', tag:'three oscillators · sync, FM · ladder, SVF, diode, Sallen-Key', len:2},
  ppoly:{t:1, k:'poly', name:'Poly', title:'pump POLY', tag:'two oscillators · poly-mod · four filter voicings · chorus', len:4, trim:2.5},
  pcoil:{t:2, k:'coil', name:'Coil', title:'pump COIL', tag:'tuned springs kicked by the note · tanks, clang to pluck', len:4},
};
/* trim: POLY's level is set for chords (a voice sums in at 0.3, 8 of them to a chord), so one rendered note sat 8-10 dB
   under MONO's and COIL's. A pad is one note, which the tracker stacks for chords, so POLY's render is 8 dB up (x2.5);
   the sound is unchanged. MONO and COIL are as the plugin. */
/* Kept in the pad (so it carries the plugin's whole state) but not shown: they do nothing to a single rendered note
   (the mod wheel, pitch bend, glide and note priority need a played line). */
const HIDE = new Set(['wheel_vib', 'glide', 'glide_mode', 'priority', 'legato', 'bend_range', 'wheel_mod', 'bend']);
/* Each card's sections (the plugins' own panels). */
const SECS = {
  pmono:[['Oscillator 1', ['osc1_wave', 'osc1_oct', 'osc1_level']], ['Oscillator 2', ['osc2_wave', 'osc2_oct', 'osc2_pitch', 'osc2_level', 'sync']],
    ['Oscillator 3', ['osc3_wave', 'osc3_oct', 'osc3_pitch', 'osc3_level', 'fm']], ['Mixer', ['pw', 'noise', 'overdrive']],
    ['Filter', ['filter_type', 'cutoff', 'res', 'filter_env', 'key_track', 'filter_mode', 'vel_filter']], ['Filter envelope', ['f_a', 'f_d', 'f_s', 'f_r']],
    ['Amp', ['a_a', 'a_d', 'a_s', 'a_r', 'vel_amp']], ['LFO', ['lfo_shape', 'lfo_rate', 'lfo_pitch', 'lfo_filter', 'lfo_pw']], ['Play', ['vel', 'drift', 'volume']]],
  ppoly:[['Oscillator A', ['a_saw', 'a_pulse', 'a_freq', 'a_pw', 'sync', 'level_a']], ['Oscillator B', ['b_saw', 'b_tri', 'b_pulse', 'b_freq', 'b_fine', 'b_pw', 'b_low', 'level_b']],
    ['Mixer', ['noise', 'sub']], ['Poly-mod', ['pm_env', 'pm_oscb', 'pm_freq_a', 'pm_pw_a', 'pm_filter']],
    ['Filter', ['filter_type', 'cutoff', 'res', 'filter_env', 'key_track', 'hpf', 'vel_filter']], ['Filter envelope', ['f_a', 'f_d', 'f_s', 'f_r']],
    ['Amp', ['a_a', 'a_d', 'a_s', 'a_r', 'vel_amp']], ['LFO', ['lfo_shape', 'lfo_rate', 'lfo_delay', 'lfo_pitch', 'lfo_pw', 'lfo_filter']],
    ['Voices', ['voice_mode', 'voices', 'unison_detune', 'slop', 'chorus']], ['Play', ['vel', 'volume']]],
  pcoil:[['Springs', ['model', 'tension', 'drip', 'harmonic', 'spread', 'wobble']], ['Ring', ['decay', 'release', 'tone', 'excite']],
    ['Tank & output', ['tank', 'grit', 'width']], ['Play', ['vel', 'velocity', 'octave', 'volume']]],
};
/* Row names where the plugin's short name needs its section's word or is a single letter. */
const LBL = {f_a:'Attack', f_d:'Decay', f_s:'Sustain', f_r:'Release', a_a:'Attack', a_d:'Decay', a_s:'Sustain', a_r:'Release',
  vel_filter:'Velocity', vel_amp:'Velocity', filter_type:'Model', filter_env:'Env amount', key_track:'Key follow', filter_mode:'SVF mode',
  osc2_pitch:'Detune', osc3_pitch:'Detune', a_freq:'Frequency', b_freq:'Frequency', b_fine:'Fine', a_pw:'Pulse width', b_pw:'Pulse width',
  a_saw:'Saw', a_pulse:'Pulse', b_saw:'Saw', b_tri:'Triangle', b_pulse:'Pulse', b_low:'LFO range', level_a:'Level', level_b:'Level',
  pm_env:'Filter env', pm_oscb:'Osc B', pm_freq_a:'→ Freq A', pm_pw_a:'→ PW A', pm_filter:'→ Filter', hpf:'High-pass',
  voice_mode:'Mode', unison_detune:'Unison', velocity:'Vel → kick', lfo_pitch:'→ Pitch', lfo_filter:'→ Filter', lfo_pw:'→ PW',
  lfo_delay:'Delay', lfo_shape:'Shape', lfo_rate:'Rate', sync:'Sync to 1', fm:'FM 3 → 1', pw:'Pulse width'};
const SEMIS = new Set(['osc2_pitch', 'osc3_pitch', 'a_freq', 'b_freq']);
const sg = v => (v > 0 ? '+' : v < 0 ? '−' : '') + Math.abs(v);
const hz = v => v >= 1000 ? (v / 1000).toFixed(v >= 10000 ? 1 : 2) + ' kHz' : v >= 100 ? Math.round(v) + ' Hz' : v.toFixed(v >= 10 ? 1 : 2) + ' Hz';
const msf = v => v >= 1000 ? (v / 1000).toFixed(2) + ' s' : v >= 10 ? Math.round(v) + ' ms' : v.toFixed(1) + ' ms';
function rowOf(p){
  const flags = p.flags | 0, isInt = !!(flags & 2), isBool = !!(flags & 4), l = LBL[p.s] || p.sn || p.n;
  if (p.ch) return {l, min:p.min, max:p.max, c:'int', d:p.def, f:v => p.ch[Math.round(v - p.min)] || String(Math.round(v))};
  if (isBool) return {l, min:0, max:1, c:'int', d:p.def, f:v => Math.round(v) ? 'On' : 'Off'};
  const c = isInt ? 'int' : p.scale === 'log' ? (p.min < 1 ? 'logf' : 'log') : p.scale === 'pow' ? 'sq' : 'lin';
  let f;
  if (SEMIS.has(p.s)) f = v => sg(+v.toFixed(Math.abs(v) < 1 ? 2 : 1)) + ' st';
  else if (p.s === 'b_fine') f = v => sg(Math.round(v)) + ' cents';
  else if (p.s === 'octave') f = v => Math.round(v) ? sg(Math.round(v)) + ' oct' : '0';
  else if (p.unit === '%') f = p.min < 0 ? v => sg(Math.round(v)) + '%' : v => Math.round(v) + '%';
  else if (p.unit === 'Hz') f = hz;
  else if (p.unit === 'ms') f = msf;
  else if (p.unit === 's') f = v => v < 1 ? Math.round(v * 1000) + ' ms' : v.toFixed(2) + ' s';
  else if (p.unit === 'dB') f = v => (v > 0 ? '+' : '') + v.toFixed(1) + ' dB';
  else f = v => isInt ? String(Math.round(v)) : v.toFixed(2);
  return {l, min:p.min, max:p.max, c, d:p.def, f};
}
const ENG = {};
for (const key in KINDS){
  const K = KINDS[key], m = META.synth[K.k], rows = {}, defs = {};
  for (const p of m.params){ defs[p.s] = p.def; if (!HIDE.has(p.s) && !((p.flags | 0) & (8 | 32))) rows[p.s] = rowOf(p); }
  if (rows.volume) rows.volume.noRnd = true;   // a random sound at -40 dB isn't a sound
  rows.vel = {l:'Hit', min:1, max:127, c:'int', d:100, f:v => String(Math.round(v)), noRnd:true}; defs.vel = 100;
  const secs = SECS[key].map(([t, ks]) => [t, ks.filter(s => rows[s])]).filter(([, ks]) => ks.length);
  // a preset is the plugin's: its values over the defaults (Default is the card's Init)
  const pre = [['Init', {}]];
  for (const pr of m.presets) if (pr.n !== 'Default') pre.push([pr.n, Object.assign({}, pr.v)]);
  ENG[key] = {rows, defs, secs, pre, about:m.about || '', specs:m.params};
}

/* ---------- WebAssembly ---------- */
const z = () => 0, IMP = {wasi_snapshot_preview1:{fd_close:z, fd_seek:z, fd_write:z, proc_exit:z}};
let X = null, mod = null;
function attach(inst){ X = inst.exports; if (X._initialize) X._initialize(); }
const f32 = (p, n) => new Float32Array(X.memory.buffer, p, n);
const BLK = 4096;
/* The pad's Level (dB) and Drive as the other engines apply them; no normalising, so the plugin's Volume is the level,
   as in a DAW. Silence at the end trimmed, a 3 ms fade. */
function finish(out, p, sr){
  const lv = Math.pow(10, (p.level || 0) / 20), dr = (p.drive || 0) > 0.001 ? 1 + p.drive * p.drive * 10 : 0, dn = dr ? 1 / Math.tanh(dr) : 1;
  for (let i = 0; i < out.length; i++){ let y = out[i] * lv; if (dr) y = Math.tanh(y * dr) * dn; out[i] = y; }
  let end = out.length; while (end > 1 && Math.abs(out[end - 1]) < 1e-4) end--;
  const res = out.slice(0, Math.max(end, 1)), fl = Math.min(res.length, Math.round(sr * 0.003)); for (let i = 0; i < fl; i++) res[res.length - 1 - i] *= i / fl;
  return res;
}
const PsynCore = {
  engines: KINDS,
  ready: () => !!X,
  module: () => mod,
  // the page: fetch and compile the wasm, instantiate it here; the Module goes to the render Worker
  async load(url){ const r = await fetch(url); if (!r.ok) throw new Error(url + ' ' + r.status); mod = await WebAssembly.compile(await r.arrayBuffer());
    attach(await WebAssembly.instantiate(mod, IMP)); return mod; },
  // the Worker: the page's compiled Module
  attachModule(m){ mod = m; attach(new WebAssembly.Instance(m, IMP)); },
  isKind: k => !!KINDS[k],
  // XENG entries (live:false: rendered); r renders, rel is the amp's release in ms (how a held note fades when let go)
  xeng(){ const o = {}; for (const k in KINDS) o[k] = {live:false, d:() => Object.assign({}, ENG[k].defs), r:(P, sr, hold, V) => PsynCore.render(k, P, sr, hold, V),
    rel:P => k === 'pcoil' ? (P.release || 0.4) * 1000 : (P.a_r || 150)}; return o; },
  // XUI entries: the card's rows, sections and the plugin's factory presets
  xui(){
    const o = {};
    for (const k in KINDS) o[k] = {title:KINDS[k].title, tag:KINDS[k].tag, rows:ENG[k].rows, secs:ENG[k].secs, pre:ENG[k].pre,
      note:ENG[k].about + ' The …pump plugin’s own engine and values: C-4 plays its MIDI note 60. Hit is how hard the note is played when the pad renders; '
        + (k === 'pcoil' ? 'the springs ring for their Decay and fade over Release when the note is let go.' : 'a held note fades over the Amp release when it is let go.')};
    return o;
  },
  len: k => (KINDS[k] && KINDS[k].len) || 2,
  /* One note: values staged, a fresh instance (so a pad renders the same every time), note on at 0, note off after the
     hold, then until it dies away (at most 12 s). Folded to mono, the plugin's latency (MONO's oversampler) taken off. */
  render(k, P, sr, hold, V){
    const K = KINDS[k];
    if (!X || !K) return new Float32Array(1);
    const specs = ENG[k].specs, st = f32(X.psyn_stage(), 512);
    for (let i = 0; i < specs.length; i++){ const v = P[specs[i].s]; st[i] = typeof v === 'number' && isFinite(v) ? v : specs[i].def; }
    // the pad's Tune: whole semitones as the note, the rest as a rate (the render made a little slower or faster)
    const tune = +P.tune || 0, semis = Math.round(tune), frac = tune - semis, rate = sr * Math.pow(2, -frac / 12);
    const h = X.psyn_new(K.t, rate);
    if (h < 0) return new Float32Array(1);
    try {
      const note = Math.max(0, Math.min(127, 60 + semis)), vel = Math.max(1, Math.min(127, Math.round(P.vel || 100)));
      const holdS = Math.min(12, Math.max(0.02, hold || (V && V.len) || K.len)), offAt = Math.round(holdS * rate);
      const maxS = Math.round(Math.min(12, holdS + 8) * rate), lat = X.psyn_latency(h);
      X.psyn_midi(h, 0, 0x90, note, vel);
      const raw = new Float32Array(maxS + BLK), g = K.trim || 1; let n = 0, quiet = 0, off = false;
      while (n < maxS + lat){
        if (!off && offAt < n + BLK){ X.psyn_midi(h, Math.max(0, offAt - n), 0x80, note, 0); off = true; }
        X.psyn_process(h, BLK);
        const L = f32(X.psyn_out(0), BLK), R = f32(X.psyn_out(1), BLK);
        let pk = 0;
        for (let i = 0; i < BLK; i++){ const y = 0.5 * g * (L[i] + R[i]), j = n + i - lat; if (j >= 0 && j < raw.length) raw[j] = y; const a = y < 0 ? -y : y; if (a > pk) pk = a; }
        n += BLK;
        quiet = pk < 3e-5 ? quiet + BLK : 0;
        if (off && n > offAt + rate * 0.02 && quiet > rate * 0.3) break;   // released and died away
      }
      return finish(raw.subarray(0, Math.min(raw.length, Math.max(1, n - lat))), P, sr);
    } finally { X.psyn_free(h); }
  },
};
G.PsynCore = PsynCore;
})(typeof self !== 'undefined' ? self : globalThis);
