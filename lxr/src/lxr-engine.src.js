/* lxr/lxr-engine.js: the LXR-02's drum, snare, cymbal and hi-hat voices as Thunder Plus engines.
 *
 * Copyright (C) 2026 the Thunder Plus authors. The sound engine is the LXR firmware (lxr/lxr.wasm, built from
 * lxr/src), Copyright (C) 2012-2026 Erica Synths.
 * SPDX-License-Identifier: GPL-3.0-only. See lxr/LICENSE and lxr/NOTICE.md (and their additional terms).
 *
 * Not official Erica Synths firmware or software, and not affiliated with or endorsed by Erica Synths.
 * "Erica Synths", "LXR" and "LXR-02" are Erica Synths' names.
 *
 * This folder is self-contained. index.html loads this file if it is there (the page and the render Worker); without
 * the folder Thunder Plus runs exactly as before, with no "Firmware" engines. Remove lxr/ to ship without GPL code.
 *
 * The voice parameters are the machine's own: 0-127 knob values with the same pages, value names and ranges as its
 * menus (read from the firmware's tables by lxr/tools/lxr_meta.py into META below). A pad renders the way the machine
 * sounds a hit: power-on state, the init kit, this pad's values loaded as a kit is, the voice triggered, then the
 * voice's own blocks at 44.14 kHz, resampled to the app's rate.
 */
(function(G){
'use strict';
const META = /*@META*/null/*@END*/;
const PAR = META.par, NP = META.nparams, RATE = META.rate;
const NAME = []; for (const k in PAR) if (PAR[k] < NP) NAME[PAR[k]] = k.replace(/^PAR_/, '');   // a later alias wins (OSC_WAVE_DRUM1 over MOD_WHEEL)
const DT = META.dtypeIds;

/* engine key -> firmware voice: the menu page its knobs come from, the voice it renders, its trigger number */
const VOICES = {
  lxdrum:{page:0, voice:0, name:'Firmware drum', tag:'LXR-02 drum voice: osc, FM, pitch env, transient, filter, LFO', len:2},
  lxsnare:{page:3, voice:3, name:'Firmware snare', tag:'LXR-02 snare voice: osc + noise, repeats, transient, filter', len:2},
  lxcym:{page:4, voice:4, name:'Firmware cymbal', tag:'LXR-02 cymbal voice: three FM oscillators, filter, LFO', len:4},
  lxhat:{page:5, voice:5, name:'Firmware hat', tag:'LXR-02 hi-hat voice: closed or open, FM oscillators, filter', len:2},
};
const VOICE_NAMES = ['Drum 1', 'Drum 2', 'Drum 3', 'Snare', 'Cymbal', 'Closed hat', 'Open hat'];

/* labels for the firmware's 8-character names, by parameter */
const LABELS = [
  [/^COARSE\d$/, 'Coarse'], [/^FINE\d$/, 'Fine'], [/^(OSC_WAVE_|WAVE1_)/, 'Waveform'], [/^PWM_\d$/, 'Pulse width'],
  [/^VELOA\d$/, 'Attack'], [/^VELOD6_CLOSED$/, 'Decay (closed)'], [/^VELOD6_OPEN$/, 'Decay (open)'], [/^VELOD\d$/, 'Decay'], [/^VOL_SLOPE\d$/, 'Slope'], [/^REPEAT\d$/, 'Repeats'],
  [/^MOD_EG\d$/, 'Pitch decay'], [/^PITCH_SLOPE\d$/, 'Pitch slope'], [/^MODAMNT\d$/, 'Pitch amount'],
  [/^VEL_DEST_\d$/, 'Velocity to'], [/^VELO_MOD_AMT_\d$/, 'Velocity amount'], [/^VOLUME_MOD_ON_OFF\d$/, 'Velocity → volume'],
  [/^FMAMNT\d$/, 'FM amount'], [/^FM_FREQ\d$/, 'FM frequency'], [/^MOD_WAVE_DRUM\d$/, 'FM waveform'], [/^MIX_MOD_\d$/, 'FM mode'],
  [/^MOD_OSC_F1/, 'Mod osc 1 freq'], [/^MOD_OSC_F2/, 'Mod osc 2 freq'], [/^MOD_OSC_GAIN1/, 'Mod osc 1 level'], [/^MOD_OSC_GAIN2/, 'Mod osc 2 level'],
  [/^WAVE2_/, 'Mod osc 1 wave'], [/^WAVE3_/, 'Mod osc 2 wave'], [/^NOISE_FREQ1$/, 'Noise frequency'], [/^MIX1$/, 'Noise / osc mix'],
  [/^TRANS\d_WAVE$/, 'Transient'], [/^TRANS\d_VOL$/, 'Transient level'], [/^TRANS\d_FREQ$/, 'Transient pitch'],
  [/^FILTER_FREQ_\d$/, 'Cutoff'], [/^RESO_\d$/, 'Resonance'], [/^FILTER_TYPE_\d$/, 'Filter type'], [/^FILTER_DRIVE_\d$/, 'Filter drive'],
  [/^FREQ_LFO\d$/, 'LFO rate'], [/^SYNC_LFO\d$/, 'LFO sync'], [/^AMOUNT_LFO\d$/, 'LFO amount'], [/^WAVE_LFO\d$/, 'LFO wave'], [/^RETRIGGER_LFO\d$/, 'LFO retrigger'],
  [/^OFFSET_LFO\d$/, 'LFO offset'], [/^TARGET_LFO\d$/, 'LFO to'], [/^VOL\d$/, 'Volume'], [/^VOICE_DECIMATION\d$/, 'Sample rate'],
  [/^(DRIVE\d|SNARE_DISTORTION|CYMBAL_DISTORTION|HAT_DISTORTION)$/, 'Drive']];
const label = n => { const nm = NAME[n] || ''; for (const [re, l] of LABELS) if (re.test(nm)) return l; return nm.toLowerCase().replace(/_/g, ' '); };
/* the firmware's 3-letter option names, spelled out */
const NICE = {Sin:'Sine', Rec:'Square', Noi:'Noise', Nch:'Notch', Pek:'Peak', UBP:'BP (unity)', LP2:'LP 2', off:'Off', sin:'Sine', tri:'Triangle', sup:'Saw up', sdn:'Saw down',
  sqr:'Square', rnd:'Random', xup:'Exp up', xdn:'Exp down', Snp:'Snap', Ofs:'Offset', Clk:'Click', Ck2:'Click 2', Tik:'Tick', Kik:'Kick', Drp:'Drop', Clp:'Clap', Kk2:'Kick 2',
  Snr:'Snare', Sp2:'Snap 2', '12':'1/12', '16':'1/16', '32':'1/32', v1:'Voice 1', v2:'Voice 2', v3:'Voice 3', v4:'Voice 4', v5:'Voice 5', v6:'Voice 6'};
const nice = s => NICE[s] || s;
/* in its section a row needs fewer words (the section says FM, Filter, LFO…); the target lists keep the full names */
const SHORT = {'Pitch decay':'Decay', 'Pitch slope':'Slope', 'Pitch amount':'Amount', 'Velocity amount':'Amount', 'Velocity → volume':'Volume',
  'FM amount':'Amount', 'FM frequency':'Frequency', 'FM waveform':'Waveform', 'FM mode':'Mode', 'Mod osc 1 freq':'Osc 1 freq', 'Mod osc 2 freq':'Osc 2 freq',
  'Mod osc 1 level':'Osc 1 level', 'Mod osc 2 level':'Osc 2 level', 'Mod osc 1 wave':'Osc 1 wave', 'Mod osc 2 wave':'Osc 2 wave', 'Transient':'Sample',
  'Transient level':'Level', 'Transient pitch':'Pitch', 'Filter type':'Type', 'Filter drive':'Drive', 'LFO rate':'Rate', 'LFO sync':'Sync', 'LFO amount':'Amount',
  'LFO wave':'Wave', 'LFO retrigger':'Retrigger', 'LFO offset':'Offset', 'Noise frequency':'Noise freq', 'Noise / osc mix':'Noise mix',
  'Decay (closed)':'Closed decay', 'Decay (open)':'Open decay'};
const PM60 = /^(COARSE\d|FM_FREQ\d|MOD_OSC_F[12](_CYM)?)$/;   // shown as value − 60 (a note), like the machine; the other ±63 knobs as value − 63

/* Modulation targets. On the machine the velocity and the LFOs can reach any voice's parameters (modTargets); a pad's
   reach its own voice's knobs, so each engine lists those, and a pad stores 0 (off) or a position in that list. */
const modParam = g => { const t = META.modTargets[g]; return t ? t[1] : 0; };

function rowFor(n, v){
  const [dt, menu] = META.dtypes[n] || [DT['0B127'], 0], nm = NAME[n] || '', d = META.init[n] | 0, full = label(n), l = SHORT[full] || full;
  if (dt === DT.MENU){ const opts = META.menus[menu] || []; return {l, min:0, max:Math.max(0, opts.length - 1), c:'int', d:Math.min(d, opts.length - 1), f:x => nice(opts[Math.round(x)] || String(Math.round(x)))}; }
  if (dt === DT.PM63){ const z = PM60.test(nm) ? 60 : 63; return {l, min:0, max:127, c:'int', d, f:x => { const y = Math.round(x) - z; return (y > 0 ? '+' : '') + y; }}; }
  if (dt === DT.ON_OFF) return {l, min:0, max:1, c:'int', d:d ? 1 : 0, f:x => Math.round(x) ? 'On' : 'Off'};
  return {l, min:0, max:127, c:'int', d, f:x => String(Math.round(x))};
}
const SKIP = /^(PAN\d|AUDIO_OUT\d|VOICE_LFO\d)$/;

/* Each engine's controls: the voice's menu pages, one section a page, without what a pad has no use for (pan, the
   audio output, the LFO's destination voice: a pad's LFO modulates its own voice). */
const TITLES = ['Oscillator', 'Envelope', '', 'FM', 'Transient', 'Filter', 'LFO', 'Level'];
const ENG = {};
for (const k in VOICES){
  const V = VOICES[k], rows = {}, secs = [], defs = {};
  const pages = META.pages[V.page].map((sub, s) => sub.map(e => e[1]).filter(n => n > 0 && n < NP));
  if (k === 'lxhat'){ const op = PAR.PAR_VELOD6_OPEN; const amp = pages[1]; const i = amp.indexOf(PAR.PAR_VELOD6_CLOSED); if (i >= 0 && !amp.includes(op)) amp.splice(i + 1, 0, op); }
  pages.forEach((ns, s) => {
    const keys = [];
    for (const n of ns){
      const nm = NAME[n] || '';
      if (SKIP.test(nm)) continue;
      if (/^VEL_DEST_\d$/.test(nm)){ keys.push('vdst'); continue; }
      if (/^TARGET_LFO\d$/.test(nm)){ keys.push('ldst'); continue; }
      const key = 'p' + n; rows[key] = rowFor(n, V.voice); defs[key] = rows[key].d; keys.push(key);
    }
    if (k === 'lxhat' && s === 1){ rows.open = {l:'Plays', min:0, max:1, c:'int', d:0, f:x => Math.round(x) ? 'Open' : 'Closed'}; keys.unshift('open'); defs.open = 0; }
    if (s === 2){   // the machine's modulation page: the pitch envelope, then velocity (with how hard the pad is hit)
      const isVel = key => key === 'vdst' || /^(VELO_MOD_AMT_|VOLUME_MOD_ON_OFF)/.test(NAME[+key.slice(1)] || '');
      const pitch = keys.filter(key => !isVel(key)), vel = keys.filter(isVel);
      rows.vel = {l:'Hit', min:1, max:127, c:'int', d:127, f:x => String(Math.round(x))}; defs.vel = 127;
      if (pitch.length) secs.push(['Pitch envelope', pitch]);
      secs.push(['Velocity', ['vel', ...vel]]);
      return;
    }
    if (keys.length){ let t = TITLES[s]; if (s === 3 && k !== 'lxdrum') t = 'Mod oscillators'; secs.push([t, keys]); }
  });
  // the targets: this voice's range of modTargets, the ones that are knobs of this engine
  const [s0, e0] = META.modVoices[V.voice], tgt = [];
  for (let g = s0; g <= e0; g++){ const n = modParam(g); if (('p' + n) in defs && !tgt.some(h => modParam(h) === n)) tgt.push(g); }
  const tRow = (l, n) => ({l, min:0, max:tgt.length, c:'int', d:Math.max(0, tgt.indexOf(META.init[n] | 0) + 1), f:x => { const i = Math.round(x); return i > 0 && tgt[i - 1] ? label(modParam(tgt[i - 1])) : 'Off'; }});
  rows.vdst = tRow('Target', PAR['PAR_VEL_DEST_' + (V.voice + 1)]); defs.vdst = rows.vdst.d;
  rows.ldst = tRow('Target', PAR['PAR_TARGET_LFO' + (V.voice + 1)]); defs.ldst = rows.ldst.d;
  ENG[k] = {rows, secs, defs, tgt};
}

/* drum 2 and 3 use drum 1's controls: their parameters line up page by page and knob by knob */
function pageMap(fromPage, toPage){
  const m = new Map(), A = META.pages[fromPage], B = META.pages[toPage];
  A.forEach((sub, s) => sub.forEach((e, k) => { const a = e[1], b = B[s] && B[s][k] && B[s][k][1]; if (a > 0 && a < NP && b > 0 && b < NP) m.set(a, b); }));
  return m;
}
/* A pad's values from a kit's bytes, for firmware voice src (0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat). */
function fromKit(bytes, src){
  const kind = src < 3 ? 'lxdrum' : src === 3 ? 'lxsnare' : src === 4 ? 'lxcym' : 'lxhat', V = VOICES[kind], E = ENG[kind], o = Object.assign({}, E.defs);
  const map = pageMap(src < 3 ? src : src >= 5 ? 5 : src, V.page);   // kit voice's parameter -> this engine's
  if (kind === 'lxhat') map.set(PAR.PAR_VELOD6_OPEN, PAR.PAR_VELOD6_OPEN);   // on the open hat's page, not the closed one's
  for (const [a, b] of map){ if (('p' + b) in E.defs) o['p' + b] = bytes[a] | 0; }
  // modulation targets: the knob the kit's target points at, as this engine's (a drum 2 target becomes the same knob
  // of the drum this engine renders); a target on another voice can't sound on this pad, so it is dropped
  const vs = Math.min(src, 5);   // the kit voice's own number (the open hat is the hat voice)
  const rel = g => { const a = modParam(g), b = map.get(a); return b ? E.tgt.findIndex(h => modParam(h) === b) + 1 : 0; };
  o.vdst = rel(bytes[PAR.PAR_VEL_DEST_1 + vs] | 0);
  o.ldst = (bytes[PAR.PAR_VOICE_LFO1 + vs] | 0) === vs + 1 ? rel(bytes[PAR.PAR_TARGET_LFO1 + vs] | 0) : 0;
  if (kind === 'lxhat') o.open = src === 6 ? 1 : 0;
  return {kind, o};
}

/* ---------- presets ----------
   Written with short names (coarse, dec, cut…) that mean the same knob on every voice; named() turns them into this
   engine's parameters. Values are the machine's own 0-127 (menus by position: wave 0 sine, 1 tri, 2 saw, 3 square,
   4 noise, 5 PWM; filter 0 LP, 1 HP, 2 BP, 3 BP unity, 4 notch, 5 peak, 6 LP2, 7 off; transient 0 snap, 1 offset,
   2 click, 3 click 2, 4 tick, 5 kick, 6 rim, 7 drop, 8 hat, 9 clap, 10 kick 2, 11 snare, 12 tom, 13 snap 2).
   Coarse is about 12 steps an octave on the drum (31 ≈ 50 Hz, 48 ≈ 130 Hz, 64 ≈ 330 Hz). */
const WAVE = {lxdrum:'OSC_WAVE_DRUM1', lxsnare:'OSC_WAVE_SNARE', lxcym:'WAVE1_CYM', lxhat:'WAVE1_HH'};
const DIST = {lxdrum:'DRIVE1', lxsnare:'SNARE_DISTORTION', lxcym:'CYMBAL_DISTORTION', lxhat:'HAT_DISTORTION'};
function alias(k, a){
  const n = VOICES[k].voice + 1, cy = k === 'lxcym', mo = cy ? '_CYM' : '', mw = cy ? '_CYM' : '_HH';
  const A = {coarse:'COARSE' + n, fine:'FINE' + n, wave:WAVE[k], pw:'PWM_' + n, atk:'VELOA' + n, dec:k === 'lxhat' ? 'VELOD6_CLOSED' : 'VELOD' + n,
    decO:'VELOD6_OPEN', slope:'VOL_SLOPE' + n, rpt:'REPEAT' + n, pDec:'MOD_EG' + n, pSlope:'PITCH_SLOPE' + n, pAmt:'MODAMNT' + n,
    fmAmt:'FMAMNT' + n, fmFreq:'FM_FREQ' + n, fmWave:'MOD_WAVE_DRUM' + n, fmMode:'MIX_MOD_' + n, noiseF:'NOISE_FREQ1', noiseMix:'MIX1',
    m1f:'MOD_OSC_F1' + mo, m2f:'MOD_OSC_F2' + mo, m1l:'MOD_OSC_GAIN1' + mo, m2l:'MOD_OSC_GAIN2' + mo, m1w:'WAVE2' + mw, m2w:'WAVE3' + mw,
    tWave:'TRANS' + n + '_WAVE', tLvl:'TRANS' + n + '_VOL', tPitch:'TRANS' + n + '_FREQ', cut:'FILTER_FREQ_' + n, res:'RESO_' + n,
    ftype:'FILTER_TYPE_' + n, fdrive:'FILTER_DRIVE_' + n, lfoRate:'FREQ_LFO' + n, lfoSync:'SYNC_LFO' + n, lfoAmt:'AMOUNT_LFO' + n,
    lfoWave:'WAVE_LFO' + n, lfoOfs:'OFFSET_LFO' + n, vol:'VOL' + n, srate:'VOICE_DECIMATION' + n, drive:DIST[k]};
  return A[a];
}
function named(k, q){
  const o = {}, E = ENG[k];
  for (const a in q){
    if (a === 'open' || a === 'vdst' || a === 'ldst' || a === 'vel'){ o[a] = q[a]; continue; }
    const nm = alias(k, a), n = nm && PAR['PAR_' + nm];
    if (n == null || !(('p' + n) in E.defs)){ if (typeof console !== 'undefined') console.warn('LXR preset: no knob ' + a + ' on ' + k); continue; }
    o['p' + n] = q[a];
  }
  return o;
}
const PRESETS = {
  lxdrum:[
    ['Deep kick', {coarse:30, dec:30, slope:40, pAmt:62, pDec:24, pSlope:30, tWave:5, tLvl:45}],
    ['Punch kick', {coarse:36, dec:22, slope:50, pAmt:72, pDec:15, pSlope:24, tWave:2, tLvl:80, drive:85}],
    ['Long boom', {coarse:29, dec:52, slope:30, pAmt:38, pDec:30, pSlope:40}],
    ['Dirty kick', {coarse:34, dec:27, pAmt:78, pDec:18, drive:118, fdrive:70, cut:96, tWave:10, tLvl:60}],
    ['Crushed kick', {coarse:33, dec:26, pAmt:66, pDec:20, srate:38, drive:90}],
    ['Low tom', {coarse:42, dec:34, pAmt:34, pDec:32, tWave:12, tLvl:35}],
    ['Mid tom', {coarse:48, dec:30, pAmt:32, pDec:30, tWave:12, tLvl:35}],
    ['High tom', {coarse:54, dec:26, pAmt:30, pDec:28, tWave:12, tLvl:30}],
    ['FM perc', {coarse:58, dec:18, slope:60, pAmt:20, pDec:12, fmAmt:85, fmFreq:72, fmWave:0}],
    ['Metal knock', {coarse:62, dec:14, wave:3, fmAmt:60, fmFreq:66, fmWave:3, ftype:2, cut:92, res:40}],
    ['Clave', {coarse:92, dec:6, slope:70, pAmt:0, tWave:4, tLvl:50}],
    ['Rimshot', {coarse:72, dec:5, pAmt:10, pDec:6, tWave:6, tLvl:110, ftype:1, cut:62}],
    ['Zap', {coarse:66, dec:20, pAmt:118, pDec:26, pSlope:60}],
    ['Bleep', {coarse:76, dec:16, wave:3, pAmt:0}],
    ['Sub drop', {coarse:27, dec:58, pAmt:50, pDec:60, pSlope:80}],
  ],
  lxsnare:[
    ['Tight snare', {coarse:48, dec:10, noiseMix:70, pAmt:62, pDec:12, tWave:0, tLvl:60}],
    ['Fat snare', {coarse:43, dec:20, noiseMix:82, pAmt:70, pDec:16, ftype:0, cut:105, drive:85}],
    ['Clap', {dec:13, rpt:3, noiseMix:127, ftype:2, cut:80, res:25, tWave:9, tLvl:80}],
    ['Rimshot', {coarse:64, dec:5, noiseMix:22, pAmt:40, pDec:6, tWave:6, tLvl:72, vol:96}],
    ['Brush', {dec:26, atk:8, noiseMix:127, ftype:1, cut:78, vol:110}],
    ['Snap', {dec:6, noiseMix:100, ftype:1, cut:70, tWave:13, tLvl:90}],
    ['Crunch snare', {coarse:46, dec:14, noiseMix:75, pAmt:60, pDec:14, srate:48, drive:105}],
    ['Lo-fi snare', {coarse:45, dec:16, noiseMix:78, ftype:0, cut:82, srate:62}],
    ['Gated snare', {coarse:47, dec:18, slope:127, noiseMix:80, pAmt:55, pDec:14, drive:80}],
    ['Flam', {coarse:48, dec:11, rpt:1, noiseMix:72, pAmt:60, pDec:12}],
  ],
  lxcym:[
    ['Crash', {dec:118, ftype:1, cut:62}],
    ['Ride', {wave:3, coarse:100, m1f:70, m1l:90, m2f:104, m2l:80, m1w:3, m2w:3, dec:116, ftype:2, cut:96, res:18}],
    ['Splash', {dec:104, ftype:1, cut:74}],
    ['Trash cymbal', {dec:112, drive:96, srate:72, ftype:1, cut:58, vol:100}],
    ['Swell', {atk:95, dec:114, ftype:1, cut:66, vol:112}],
    ['Bell', {wave:0, coarse:84, m1w:0, m1f:60, m1l:75, m2w:0, m2f:92, m2l:50, dec:110, ftype:7, vol:112}],
    ['Dark cymbal', {dec:116, ftype:0, cut:90, res:15}],
  ],
  lxhat:[
    ['Tight hat', {dec:3, ftype:1, cut:112}],
    ['Metal hat', {wave:3, m1w:3, m2w:3, m1f:80, m2f:110, dec:6, ftype:2, cut:106, res:30}],
    ['Open hat', {open:1, decO:26}],
    ['Long open hat', {open:1, decO:44}],
    ['Crunchy hat', {dec:6, srate:60, drive:100}],
    ['Shaker', {wave:4, atk:16, dec:10, ftype:1, cut:100}],
    ['Pedal hat', {dec:4, ftype:2, cut:100, vol:127}],
  ],
};

/* ---------- WebAssembly ---------- */
let X = null, mod = null, fresh = null;
/* The firmware keeps state between hits (filters, oscillator phases, counters its init functions leave alone), as the
   machine does. A pad should render the same every time, so every render starts from a copy of the memory taken just
   after start-up: a freshly powered-on machine. */
function attach(inst){ X = inst.exports; if (X._initialize) X._initialize(); fresh = new Uint8Array(X.memory.buffer).slice(); }
const LxrCore = {
  engines: VOICES, voiceNames:VOICE_NAMES,
  ready: () => !!X,
  module: () => mod,
  // the page: fetch and compile the wasm next to this file, instantiate it here; the Module goes to the render Worker
  async load(url){ const r = await fetch(url); if (!r.ok) throw new Error(url + ' ' + r.status); mod = await WebAssembly.compile(await r.arrayBuffer());
    attach(await WebAssembly.instantiate(mod, {})); return mod; },
  // the Worker: the page's compiled Module (a Worker may instantiate synchronously)
  attachModule(m){ mod = m; attach(new WebAssembly.Instance(m, {})); },
  isKind: k => !!VOICES[k],
  // XENG entries (live:false: rendered one-shots); r renders
  xeng(){ const o = {}; for (const k in VOICES) o[k] = {live:false, d:() => Object.assign({}, ENG[k].defs), r:(P, sr, hold, V, bpm) => LxrCore.render(k, P, sr, hold, V, bpm)}; return o; },
  // XUI entries: the engine card's rows and sections, and presets (the init kit's voices)
  xui(){
    const o = {}, init = META.init;
    for (const k in VOICES){
      const pre = [['Init', {}]];   // the init kit's sound for this voice (drum: drum 1)
      if (k === 'lxdrum') pre.push(['Init kit drum 2', fromKit(init, 1).o], ['Init kit drum 3', fromKit(init, 2).o]);
      if (k === 'lxhat') pre.push(['Init kit open hat', {open:1}]);
      for (const [n, q] of PRESETS[k] || []) pre.push([n, named(k, q)]);
      o[k] = {title:VOICES[k].name, tag:VOICES[k].tag, rows:ENG[k].rows, secs:ENG[k].secs, pre,
        note:'The machine’s own 0–127 knobs, on its pages. Hit is how hard the pad is struck when it renders, for the Velocity target. The LFO and velocity reach this voice’s knobs; Volume is the voice’s own level, before the pad’s Level.'};
    }
    return o;
  },
  len: k => (VOICES[k] && VOICES[k].len) || 2,
  // the firmware's init kit as seven pads (the pad Presets list)
  initKit(){ const b = new Uint8Array(NP); b.set(META.init.slice(0, NP)); return VOICE_NAMES.map((n, i) => Object.assign({name:n}, fromKit(b, i))); },
  // does a pad follow the song tempo (an LFO synced to it)?
  synced(v){ const k = v && v.engine, P = v && v[k]; if (!VOICES[k] || !P) return false; const n = VOICES[k].voice + 1;
    return (P['p' + PAR['PAR_SYNC_LFO' + n]] | 0) > 0 && (P['p' + PAR['PAR_AMOUNT_LFO' + n]] | 0) > 0 && (P.ldst | 0) > 0; },
  // an LXR kit file (.snd: 8 name bytes, then the sound parameters): its name and its seven sounds as pads
  readKit(buf){
    const b = new Uint8Array(buf); if (b.length < 16) throw new Error('not an LXR kit');
    let name = ''; for (let i = 0; i < 8 && b[i]; i++) name += b[i] >= 32 && b[i] < 127 ? String.fromCharCode(b[i]) : '';
    const bytes = new Uint8Array(NP); bytes.set(b.subarray(8, 8 + NP));
    for (let i = 0; i < 6; i++){ if (bytes[PAR.PAR_VEL_DEST_1 + i] >= META.modTargets.length) bytes[PAR.PAR_VEL_DEST_1 + i] = 0; if (bytes[PAR.PAR_TARGET_LFO1 + i] >= META.modTargets.length) bytes[PAR.PAR_TARGET_LFO1 + i] = 0; }
    return {name:name.trim() || 'LXR kit', sounds:VOICE_NAMES.map((n, i) => Object.assign({name:n}, fromKit(bytes, i)))};
  },
  render(k, P, sr, hold, V, bpm){
    if (!X) return new Float32Array(1);
    const Vc = VOICES[k], vi = Vc.voice, blk = X.lxr_block();
    new Uint8Array(X.memory.buffer).set(fresh);
    X.lxr_init(0x2545F491); X.lxr_init_kit();
    const b = new Uint8Array(X.memory.buffer, X.lxr_params(), NP);
    for (let i = 0; i < 6; i++){ b[PAR.PAR_VEL_DEST_1 + i] = 0; b[PAR.PAR_TARGET_LFO1 + i] = 0; b[PAR.PAR_VOICE_LFO1 + i] = i + 1; }
    for (const key in P){ if (/^p\d+$/.test(key)){ const n = +key.slice(1); if (n < NP) b[n] = Math.max(0, Math.min(255, Math.round(P[key]) || 0)); } }
    const T = ENG[k].tgt, tg = r => { r = Math.round(r) || 0; return r > 0 && T[r - 1] ? T[r - 1] : 0; };
    b[PAR.PAR_VEL_DEST_1 + vi] = tg(P.vdst); b[PAR.PAR_TARGET_LFO1 + vi] = tg(P.ldst);
    X.lxr_set_bpm(Math.round(bpm || 120)); X.lxr_load();
    for (let i = 0; i < 20; i++) X.lxr_render(vi, 16);   // 1/3 s for the knob and fader smoothing to settle, as on the machine between hits
    const trig = k === 'lxhat' ? (Math.round(P.open) ? 6 : 5) : vi, note = Math.max(0, Math.min(127, 60 + Math.round(P.tune || 0)));
    X.lxr_trigger(trig, Math.max(1, Math.min(127, Math.round(P.vel || 127))), note);
    const maxS = Math.round(Math.min(12, Math.max(0.05, hold || (V && V.len) || Vc.len)) * RATE), raw = new Float32Array(maxS + blk * 16);
    let n = 0, quiet = 0;
    while (n < maxS){
      const c = X.lxr_render(vi, 16), o = new Float32Array(X.memory.buffer, X.lxr_out(), c);
      raw.set(o, n); n += c;
      let pk = 0; for (let i = 0; i < c; i++){ const a = Math.abs(o[i]); if (a > pk) pk = a; }
      quiet = pk < 3e-5 ? quiet + c : 0;
      if (n > RATE * 0.05 && quiet > RATE * 0.3) break;   // the voice has died away
    }
    return finish(resample(raw.subarray(0, n), RATE, sr), P, sr);
  },
};
/* windowed-sinc resampling from the machine's 44.14 kHz to the app's rate: 32 taps, a Hann window, the kernel from a
   table of 1024 fractional positions (each row summing to 1, so the level is unchanged) */
let KT = null, KTk = '';
function resample(x, from, to){
  if (Math.abs(from - to) < 0.5) return Float32Array.from(x);
  const A = 16, PH = 1024, ratio = from / to, fc = Math.min(1, to / from) * 0.97, key = from + '/' + to;
  if (KTk !== key){ KTk = key; KT = new Float32Array((PH + 1) * 2 * A);
    for (let p = 0; p <= PH; p++){ const f = p / PH; let w = 0;
      for (let m = 0; m < 2 * A; m++){ const d = f + m - (A - 1), u = d * fc, sc = u === 0 ? 1 : Math.sin(Math.PI * u) / (Math.PI * u), k = sc * (0.5 + 0.5 * Math.cos(Math.PI * d / A)); KT[p * 2 * A + m] = k; w += k; }
      for (let m = 0; m < 2 * A; m++) KT[p * 2 * A + m] /= w; } }
  const N = Math.floor(x.length / ratio), y = new Float32Array(N), L = x.length;
  for (let j = 0; j < N; j++){
    const t = j * ratio, i0 = Math.floor(t), row = Math.round((t - i0) * PH) * 2 * A; let s = 0;
    for (let m = 0; m < 2 * A; m++){ const i = i0 - m + A - 1; if (i >= 0 && i < L) s += x[i] * KT[row + m]; }
    y[j] = s;
  }
  return y;
}
/* the pad's Level (dB) and Drive, as the other engines apply them, but no normalising: the voice keeps the level its
   Volume knob gives it, so a kit's balance carries over. Silence at the end trimmed, a 3 ms fade. */
function finish(out, p, sr){
  const lv = Math.pow(10, (p.level || 0) / 20), dr = (p.drive || 0) > 0.001 ? 1 + p.drive * p.drive * 10 : 0, dn = dr ? 1 / Math.tanh(dr) : 1;
  for (let i = 0; i < out.length; i++){ let y = out[i] * lv; if (dr) y = Math.tanh(y * dr) * dn; out[i] = y; }
  let end = out.length; while (end > 1 && Math.abs(out[end - 1]) < 1e-4) end--;
  const res = out.slice(0, Math.max(end, 1)), fl = Math.min(res.length, Math.round(sr * 0.003)); for (let i = 0; i < fl; i++) res[res.length - 1 - i] *= i / fl;
  return res;
}
G.LxrCore = LxrCore;
})(typeof self !== 'undefined' ? self : globalThis);
