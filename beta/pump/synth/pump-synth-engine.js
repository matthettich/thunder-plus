/* pump/synth/pump-synth-engine.js: …pump's instruments (MONO, POLY, COIL, ORBIT, FOLD, GLOSS, TRACT and the drum machines STRIKE, BOING, X0X)
 * as Thunder Plus synth engines.
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
 *
 * LIVE (26 Oct): each instrument also comes as a live engine (Mono live, Poly live, Coil live: pmonol, ppolyl, pcoill),
 * played by pump-synth-worklet.js in the tracker as the plugin is in a DAW (a note's own pitch, velocity, length, legato
 * and glide; POLY's chords through its own voices). A live engine and its rendered twin share one set of values: a pad
 * switched between them keeps its sound (index.html: setEngine copies them over). The safety net: whenever a live engine
 * can't play live (no AudioWorklet, the wasm didn't load, or the device couldn't keep up and the page switched live off),
 * the tracker plays the pad's render instead, which is the twin's render of the same values (XENG r below).
 */
(function(G){
'use strict';
const META = G.PUMP_SYNTH_META;
if (!META || !META.synth) return;

/* engine key in Thunder Plus -> the instrument (type id in pump-synth.wasm, its meta) */
const KINDS = {
  pmono:{t:0, k:'mono', name:'Mono', title:'pump MONO', tag:'three oscillators · sync, FM · ladder, SVF, diode, Sallen-Key', len:2},
  ppoly:{t:1, k:'poly', name:'Poly', title:'pump POLY', tag:'two oscillators · poly-mod · four filter voicings · chorus', len:4, trim:1.5},
  pcoil:{t:2, k:'coil', name:'Coil', title:'pump COIL', tag:'tuned springs kicked by the note · tanks, clang to pluck', len:4},
  porbit:{t:3, k:'orbit', name:'Orbit', title:'pump ORBIT', tag:'strange attractors as oscillators · chaos sweep · ladder filter · drift', len:3},
  pfold:{t:4, k:'fold', name:'Fold', title:'pump FOLD', tag:'west coast: complex oscillator · wavefolder · low-pass gate · function', len:2},
  pgloss:{t:5, k:'gloss', name:'Gloss', title:'pump GLOSS', tag:'hyperpop: rubber FM · chip · voice · supersaw · digital · string · drops', len:2},
  ptract:{t:6, k:'tract', name:'Tract', title:'pump TRACT', tag:'vocal-tract pad: modelled singers, vowels, breath, choirs', len:4},
  /* DRUM MACHINES (kit): a pad renders one of the machine's drums (Drum), on its note; Tune changes its speed (a drum's
     own Pitch is on its page). Played live, one track plays the whole kit: C-4 the first drum, then up the white keys
     (D-4, E-4, F-4…), as the plugin does from its C-4. */
  pstrike:{t:7, k:'strike', name:'Strike', title:'pump STRIKE', tag:'drum synth: modal, string, circuit, FM, west, additive, chaos · 8 drums', len:1.5, kit:true},
  pboing:{t:8, k:'boing', name:'Boing', title:'pump BOING', tag:'spring drums: a tuned spring and a drum voice per pad · spring tank · 8 drums', len:1.5, kit:true},
  px0x:{t:9, k:'x0x', name:'X0X', title:'pump X0X', tag:'rhythm machine: 11 analog drums on five classic machines’ circuits', len:1.5, kit:true},
  pfreeze:{t:11, k:'freeze', name:'Freeze', title:'pump FREEZE', tag:'spectral freeze: a sound as a cloud of frozen, shimmering spectral grains', len:4},
  pwob:{t:12, k:'wob', name:'Wob', title:'pump WOB', tag:'additive bass: 256 partials · morph · vowels · comb · reese · tempo wobble', len:2, bpm:true},
  plife:{t:10, k:'life', name:'Life', title:'pump LIFE', tag:'groovebox voices: 8 FM / ring / kick tones · pitch-shifting delay', len:1.5, kit:true},
};
/* The live twins: the same instrument, played live. Shown after their rendered engine in the picker. */
const LIVE = {
  pmonol:{twin:'pmono', name:'Mono live', title:'pump MONO live', tag:'played live: each note its own pitch, velocity and length · glide, legato'},
  ppolyl:{twin:'ppoly', name:'Poly live', title:'pump POLY live', tag:'played live: chords through its own voices, unison and chorus'},
  pcoill:{twin:'pcoil', name:'Coil live', title:'pump COIL live', tag:'played live: every note kicks its own springs, as hard as it is played'},
  porbitl:{twin:'porbit', name:'Orbit live', title:'pump ORBIT live', tag:'played live: chords through its own voices, glide, the drift keeps moving'},
  pfoldl:{twin:'pfold', name:'Fold live', title:'pump FOLD live', tag:'played live: chords, glide, the function and random source keep moving'},
  pglossl:{twin:'pgloss', name:'Gloss live', title:'pump GLOSS live', tag:'played live: chords, mono or legato lines, glide, bends'},
  ptractl:{twin:'ptract', name:'Tract live', title:'pump TRACT live', tag:'played live: a choir per chord, glide, vowels that move as you hold'},
  pstrikel:{twin:'pstrike', name:'Strike kit live', title:'pump STRIKE kit live', tag:'the whole kit on one track: C-4 D-4 E-4… play its eight drums'},
  pboingl:{twin:'pboing', name:'Boing kit live', title:'pump BOING kit live', tag:'the whole kit on one track: C-4 D-4 E-4… play its eight drums'},
  pfreezel:{twin:'pfreeze', name:'Freeze live', title:'pump FREEZE live', tag:'played live: chords of frozen grains, glide, moving through the sound as you hold'},
  pwobl:{twin:'pwob', name:'Wob live', title:'pump WOB live', tag:'played live: the wobble in time with the song, glide and legato, bends'},
  plifel:{twin:'plife', name:'Life kit live', title:'pump LIFE kit live', tag:'its eight tones on one track: C-4 D-4 E-4… (the Game of Life grid stays in the plugin)'},
  px0xl:{twin:'px0x', name:'X0X kit live', title:'pump X0X kit live', tag:'the whole kit on one track: C-4 D-4 E-4… play its eleven drums'},
};
const ORDER = ['pmono', 'pmonol', 'ppoly', 'ppolyl', 'pcoil', 'pcoill', 'porbit', 'porbitl', 'pfold', 'pfoldl', 'pgloss', 'pglossl', 'ptract', 'ptractl', 'pstrike', 'pstrikel', 'pboing', 'pboingl', 'px0x', 'px0xl', 'plife', 'plifel', 'pfreeze', 'pfreezel', 'pwob', 'pwobl'];
/* trim: POLY's level is set for chords (a voice sums in at 0.3, 8 of them to a chord), so one rendered note sits under
   MONO's and COIL's. A pad is one note, which the tracker stacks for chords, so POLY's render is 3.5 dB up (x1.5): as
   far as its loudest factory voice (Unison brass lead) allows without passing -1 dBFS, since the plugin's own safety
   limiter comes before this. Since pump's volume pass (every instrument at VOLUME 0 dB, about -20 dBFS RMS), one
   rendered note: MONO about -20 dBFS RMS, COIL -24, POLY -25. MONO and COIL are as the plugin. */
/* Kept in the pad (so it carries the plugin's whole state) but not shown: they do nothing to a single rendered note
   (the mod wheel, pitch bend, glide and note priority need a played line). */
const HIDE = new Set(['wheel_vib', 'glide', 'glide_mode', 'priority', 'legato', 'bend_range', 'wheel_mod', 'bend', 'voice_mode', 'voices', 'mode', 'follow', 'wheel']);   // (wheel: WOB's mod wheel to its wobble: no wheel here)   // (follow: the drum machines' editor follows the drum played)   // (ORBIT's voices and mode: a rendered pad is one note)
/* ...which a live twin shows (bend range and the mod wheel stay hidden: the tracker's pitch commands are sent as bend
   over the whole range, and there is no wheel). */
const LIVE_ROWS = {pmonol:[['Glide & legato', ['glide', 'glide_mode', 'legato', 'priority']]], ppolyl:[['Glide', ['glide']]], pcoill:[['Voices', ['voices']]], porbitl:[['Voices', ['voices', 'voice_mode', 'glide']]],
  pfoldl:[['Voices', ['voices', 'voice_mode', 'glide']]], pglossl:[['Voices', ['mode', 'glide']]], ptractl:[['Voices', ['voices', 'voice_mode', 'glide']]], pstrikel:[], pboingl:[], px0xl:[], plifel:[],
  pfreezel:[['Voices', ['voices', 'voice_mode', 'glide']]], pwobl:[['Glide & legato', ['glide', 'legato']]]};   // COIL's VOICES: how many notes ring at once (a rendered pad is one)
/* What the live instance is held to whatever the pad says: the widest bend, so a slide of an octave or two is one bend. */
const LIVE_FIXED = {pmonol:{bend_range:24}, ppolyl:{bend_range:24}, pcoill:{bend:12}, pglossl:{bend_range:24}};
/* Held whatever the pad says, rendered and live: X0X's own sequencer off (the tracker plays it). */
const FIXED = {px0x:{seq_mode:0}, plife:{mode:0}};
/* A kit's drums: the parameters named <drum>_… (in the plugin's order), the rest are the kit's. X0X's step patterns and
   its sequencer's settings are left out (the tracker is the sequencer). */
const KIT_SKIP = /(_steps|^seq_|^swing$|^ac_steps$|^(mode|rate|length|offset|sensitivity|x_wrap|y_wrap|colour|cols|rows|rule|trigger|pattern|reseed)$)/;
/* a kit's drums' prefixes (LIFE's tones are t1…t8; its fx_ rows are the kit's delay) */
const KIT_DRUM = {plife:/^t\d+$/};
const whiteKey = i => 48 + [0, 2, 4, 5, 7, 9, 11][i % 7] + 12 * Math.floor(i / 7);
const noteName = n => ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][n % 12] + '-' + (Math.floor(n / 12) - 1);   // the tracker's names (MIDI 60 is C-4)
/* Each card's sections (the plugins' own panels). */
const SECS = {
  pmono:[['Oscillator 1', ['osc1_wave', 'osc1_oct', 'osc1_level']], ['Oscillator 2', ['osc2_wave', 'osc2_oct', 'osc2_pitch', 'osc2_level', 'sync']],
    ['Oscillator 3', ['osc3_wave', 'osc3_oct', 'osc3_pitch', 'osc3_level', 'fm']], ['Mixer', ['pw', 'noise', 'overdrive']],
    ['Filter', ['filter_type', 'cutoff', 'res', 'filter_env', 'key_track', 'filter_mode', 'vel_filter']], ['Filter envelope', ['f_a', 'f_d', 'f_s', 'f_r']],
    ['Amp', ['a_a', 'a_d', 'a_s', 'a_r', 'vel_amp']], ['LFO', ['lfo_shape', 'lfo_rate', 'lfo_pitch', 'lfo_filter', 'lfo_pw']], ['Play', ['vel', 'drift', 'volume']]],
  porbit:[['Orbit', ['system', 'chaos', 'sync', 'axis', 'width', 'o_drive']], ['Filter', ['cutoff', 'reso', 'keytrack']],
    ['Mod envelope', ['env_cut', 'env_chaos', 'mod_attack', 'mod_decay', 'mod_sustain', 'mod_release']], ['Drift', ['drift_rate', 'drift_pitch', 'drift_cut', 'drift_chaos']],
    ['Amp', ['attack', 'decay', 'sustain', 'release', 'velocity']], ['Play', ['vel', 'octave', 'fine', 'volume']]],
  ppoly:[['Oscillator A', ['a_saw', 'a_pulse', 'a_freq', 'a_pw', 'sync', 'level_a']], ['Oscillator B', ['b_saw', 'b_tri', 'b_pulse', 'b_freq', 'b_fine', 'b_pw', 'b_low', 'level_b']],
    ['Mixer', ['noise', 'sub']], ['Poly-mod', ['pm_env', 'pm_oscb', 'pm_freq_a', 'pm_pw_a', 'pm_filter']],
    ['Filter', ['filter_type', 'cutoff', 'res', 'filter_env', 'key_track', 'hpf', 'vel_filter']], ['Filter envelope', ['f_a', 'f_d', 'f_s', 'f_r']],
    ['Amp', ['a_a', 'a_d', 'a_s', 'a_r', 'vel_amp']], ['LFO', ['lfo_shape', 'lfo_rate', 'lfo_delay', 'lfo_pitch', 'lfo_pw', 'lfo_filter']],
    ['Voices', ['voice_mode', 'voices', 'unison_detune', 'slop', 'chorus']], ['Play', ['vel', 'volume']]],
  pfold:[['Oscillator', ['shape', 'octave', 'fine']], ['Modulator', ['mod_wave', 'mod_ratio', 'mod_detune', 'fm_index', 'am_depth', 'mod_fold']],
    ['Folder', ['fold', 'symmetry', 'edge', 'stages']], ['Low-pass gate', ['gate_mode', 'gate_open', 'vactrol', 'gate_reso']],
    ['Function', ['func_mode', 'rise', 'fall', 'func_gate', 'func_fold', 'func_fm', 'func_pitch']], ['Random', ['rand_rate', 'rand_smooth', 'rand_fold', 'rand_fm', 'rand_pitch']],
    ['Play', ['vel', 'velocity', 'spread', 'volume']]],
  pgloss:[['Machine', ['machine', 'ctl_a', 'ctl_b', 'ctl_c', 'ctl_d']], ['Pitch', ['o_tune', 'pitch_env', 'pitch_time']],
    ['Filter', ['filter_type', 'cutoff', 'res', 'filter_env', 'filter_decay', 'key_track']], ['Amp', ['attack', 'decay', 'sustain', 'release', 'vel_amp']],
    ['Colour', ['o_drive', 'drive_type', 'crush_bits', 'crush_rate', 'width']], ['LFO', ['lfo_shape', 'lfo_rate', 'lfo_depth', 'lfo_dest']], ['Play', ['vel', 'volume']]],
  ptract:[['Voice', ['vowel', 'motion', 'motion_rate', 'size', 'tense', 'breath', 'bright']], ['Vibrato', ['vibrato', 'vib_rate', 'jitter']],
    ['Choir', ['singers', 'detune', 'size_spread', 'vowel_spread', 'spread']], ['Amp', ['attack', 'decay', 'sustain', 'release', 'velocity']], ['Play', ['vel', 'octave', 'volume']]],
  pfreeze:[['Sound', ['source', 'root', 'track']], ['Freeze', ['position', 'speed', 'spray', 'grains', 'blur']],
    ['Spectrum', ['stretch', 'tilt', 'shimmer', 'low', 'high', 'phase', 'width']], ['Amp', ['attack', 'decay', 'sustain', 'release', 'velocity']], ['Play', ['vel', 'octave', 'volume']]],
  pwob:[['Partials', ['shape', 'partials', 'tilt', 'odd_even', 'stretch', 'inharm', 'spray', 'drift', 'drift_rate']], ['Morph', ['shape_b', 'tilt_b', 'morph']],
    ['Filter', ['filter_mode', 'cutoff', 'res', 'filter_env', 'filter_decay', 'key_track']], ['Vowel & comb', ['vowel', 'formant', 'comb_freq', 'comb']],
    ['Reese & sub', ['detune', 'width', 'sub', 'sub_oct']],
    ['Wobble', ['wob_sync', 'wob_div', 'wob_rate', 'wob_shape', 'wob_retrig', 'wob_phase', 'wob_smooth', 'wob_delay', 'wob_fade', 'wob_depth']],
    ['Wobble moves', ['wob_cut', 'wob_vowel', 'wob_comb', 'wob_amp', 'wob_pitch', 'wob_morph']],
    ['Amp', ['attack', 'decay', 'sustain', 'release']], ['Play', ['vel', 'octave', 'o_tune', 'o_drive', 'volume']]],
  pcoil:[['Springs', ['model', 'tension', 'drip', 'harmonic', 'spread', 'wobble']], ['Ring', ['decay', 'release', 'tone', 'excite']],
    ['Tank & output', ['tank', 'grit', 'width']], ['Play', ['vel', 'velocity', 'octave', 'volume']]],
};
/* Row names where the plugin's short name needs its section's word or is a single letter. */
const LBL = {f_a:'Attack', f_d:'Decay', f_s:'Sustain', f_r:'Release', a_a:'Attack', a_d:'Decay', a_s:'Sustain', a_r:'Release',
  vel_filter:'Velocity', vel_amp:'Velocity', filter_type:'Model', filter_env:'Env amount', key_track:'Key follow', filter_mode:'SVF mode',
  osc2_pitch:'Detune', osc3_pitch:'Detune', a_freq:'Frequency', b_freq:'Frequency', b_fine:'Fine', a_pw:'Pulse width', b_pw:'Pulse width',
  a_saw:'Saw', a_pulse:'Pulse', b_saw:'Saw', b_tri:'Triangle', b_pulse:'Pulse', b_low:'LFO range', level_a:'Level', level_b:'Level',
  pm_env:'Filter env', pm_oscb:'Osc B', pm_freq_a:'→ Freq A', pm_pw_a:'→ PW A', pm_filter:'→ Filter', hpf:'High-pass',
  voice_mode:'Mode', unison_detune:'Unison', lfo_pitch:'→ Pitch', lfo_filter:'→ Filter', lfo_pw:'→ PW',
  lfo_delay:'Delay', lfo_shape:'Shape', env_cut:'Env → cutoff', env_chaos:'Env → chaos', mod_attack:'Attack', mod_decay:'Decay', mod_sustain:'Sustain', mod_release:'Release',
  drift_rate:'Rate', ctl_a:'A', ctl_b:'B', ctl_c:'C', ctl_d:'D', func_gate:'→ Gate', func_fold:'→ Fold', func_fm:'→ FM', func_pitch:'→ Pitch',
  rand_rate:'Rate', rand_smooth:'Smooth', rand_fold:'→ Fold', rand_fm:'→ FM', rand_pitch:'→ Pitch', gate_mode:'Mode', gate_open:'Open', gate_reso:'Resonance', func_mode:'Mode',
  mod_wave:'Wave', mod_ratio:'Ratio', mod_detune:'Detune', fm_index:'FM', am_depth:'AM', mod_fold:'→ Fold', filter_decay:'Env decay', vel_amp:'Velocity',
  drive_type:'Drive type', crush_bits:'Bits', crush_rate:'Rate', lfo_dest:'→', motion_rate:'Motion rate', vib_rate:'Rate', size_spread:'Size spread', vowel_spread:'Vowel spread', drift_pitch:'→ Pitch', drift_cut:'→ Cutoff', drift_chaos:'→ Chaos', keytrack:'Key follow', reso:'Resonance', fine:'Fine (cents)', lfo_rate:'Rate', sync:'Sync to 1', fm:'FM 3 → 1', pw:'Pulse width', glide_mode:'Glide when', priority:'Priority'};
/* A plugin parameter whose symbol is one of the pad's own (Thunder's Drive, Tune, Level) is kept in the pad under another
   name (ORBIT's drive is o_drive), so the pad's Drive doesn't overwrite it. */
const AL = {drive:'o_drive', tune:'o_tune', level:'o_level'}, al = s => AL[s] || s;
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
  for (const p of m.params){ defs[al(p.s)] = p.def; if (!HIDE.has(p.s) && !((p.flags | 0) & (8 | 32))) rows[al(p.s)] = rowOf(p); }
  if (key === 'pcoil' && rows.velocity) rows.velocity.l = 'Vel → kick';
  if (key === 'plife'){ rows.fm.l = 'FM (all)'; rows.decay.l = 'Decay (all)'; rows.pitch_range.l = 'LFO → pitch'; rows.lfo_rate.l = 'LFO rate'; rows.lfo_depth.l = 'LFO depth'; }   // COIL's: how hard a note kicks the springs
  if (rows.volume) rows.volume.noRnd = true;   // a random sound at -40 dB isn't a sound
  rows.vel = {l:'Hit', min:1, max:127, c:'int', d:100, f:v => String(Math.round(v)), noRnd:true}; defs.vel = 100;
  let secs;
  if (K.kit){   // the drums' pages (one shows at a time: the pad's Drum), then the kit's
    const drums = [], kitKeys = [];
    for (const p of m.params){ const s = al(p.s); if (KIT_SKIP.test(p.s)){ delete rows[s]; continue; } if (!rows[s]) continue;
      const i = p.s.indexOf('_'), pre = i > 0 ? p.s.slice(0, i) : '', d = pre && (!KIT_DRUM[key] || KIT_DRUM[key].test(pre)) && m.params.filter(q => q.s.startsWith(pre + '_')).length >= 5 ? pre : '';
      if (!d){ kitKeys.push(s); continue; }
      let g = drums.find(x => x.pre === d); if (!g){ g = {pre:d, ps:[], keys:[]}; drums.push(g); }
      g.ps.push(p); g.keys.push(s); }
    for (const g of drums){   // its name: the words its parameters' names start with ("Bass Drum"); a row: the rest ("Spring Tank")
      const W = g.ps.map(p => (p.n || '').split(' ')); let n = 0; while (W.every(w => w.length > n + 1 && w[n] === W[0][n])) n++;
      g.name = W[0].slice(0, n).join(' ') || g.pre.toUpperCase();
      g.ps.forEach((p, j) => { const w = W[j].slice(n).join(' '); rows[g.keys[j]] = Object.assign({}, rows[g.keys[j]], {l:w ? w.split(' ').map(x => /^[A-Z]{3,}$/.test(x) ? x[0] + x.slice(1).toLowerCase() : x).join(' ') : p.sn}); }); }
    rows.kd = {l:'Drum', min:0, max:drums.length - 1, c:'int', d:0, f:v => { const g = drums[Math.round(v)]; return g ? g.name : ''; }, noRnd:true}; defs.kd = 0;
    K.drums = drums.length; K.drumNames = drums.map(x => x.name);
    secs = [['Drum', ['kd', 'vel']], ...drums.map((g, i) => [g.name, g.keys, i]), ['Kit', kitKeys]];
  } else secs = SECS[key].map(([t, ks]) => [t, ks.filter(s => rows[s])]).filter(([, ks]) => ks.length);
  // a preset is the plugin's: its values over the defaults (Default is the card's Init)
  const pre = [['Init', {}]];
  for (const pr of m.presets) if (pr.n !== 'Default') pre.push([pr.n, Object.fromEntries(Object.entries(pr.v).map(([q, v]) => [al(q), v]))]);
  ENG[key] = {rows, defs, secs, pre, about:m.about || '', specs:m.params};
}
for (const key in LIVE){
  const T = ENG[LIVE[key].twin], m = META.synth[KINDS[LIVE[key].twin].k], rows = Object.assign({}, T.rows);
  for (const p of m.params) if (HIDE.has(p.s) && LIVE_ROWS[key].some(([, ks]) => ks.includes(al(p.s)))) rows[al(p.s)] = rowOf(p);
  rows.vel = Object.assign({}, rows.vel, {l:'Velocity'});   // live: the VOL column is the velocity; this is a note's without one
  const secs = T.secs.slice(), at = secs.findIndex(([t]) => t === 'Play' || t === 'Kit');
  secs.splice(at < 0 ? secs.length : at, 0, ...LIVE_ROWS[key]);
  ENG[key] = Object.assign({}, T, {rows, secs});
}
const twinOf = k => LIVE[k] ? LIVE[k].twin : k;

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
let bytes = null;
const PsynCore = {
  // every engine, in the picker's order: [key, name, tag]
  engines: Object.fromEntries(ORDER.map(k => [k, KINDS[k] || LIVE[k]])),
  rendered: Object.keys(KINDS),
  ready: () => !!X,
  module: () => mod,
  bytes: () => bytes,   // the wasm file, for the live engines' worklet (it compiles its own instance on the audio thread)
  // the page: fetch and compile the wasm, instantiate it here; the Module goes to the render Worker
  async load(url){ const r = await fetch(url); if (!r.ok) throw new Error(url + ' ' + r.status); bytes = new Uint8Array(await r.arrayBuffer()); mod = await WebAssembly.compile(bytes);
    attach(await WebAssembly.instantiate(mod, IMP)); return mod; },
  // the Worker: the page's compiled Module
  attachModule(m){ mod = m; attach(new WebAssembly.Instance(m, IMP)); },
  isKind: k => !!(KINDS[k] || LIVE[k]),
  isLive: k => !!LIVE[k],
  twin: k => LIVE[k] ? LIVE[k].twin : Object.keys(LIVE).find(q => LIVE[q].twin === k) || null,
  // what a live node is made with: the instrument, its parameter symbols in order, the values held fixed, the level trim
  liveSpec: k => { if (!LIVE[k]) return null; const tw = twinOf(k), K = KINDS[tw]; return {type:K.t, syms:ENG[k].specs.map(p => al(p.s)), fixed:Object.assign({}, FIXED[tw], LIVE_FIXED[k]), trim:K.trim || 1,
    range:tw === 'pcoil' ? 12 : LIVE_FIXED[k] && LIVE_FIXED[k].bend_range ? 24 : K.kit ? 24 : 2, nb:K.kit ? 48 : 60}; },   // range: the bend range (ORBIT, FOLD, TRACT ±2, fixed in the plugin); nb: the MIDI note C-4 plays (a kit: its first drum)   // range: the bend range (ORBIT's is fixed at ±2)
  /* XENG entries. Rendered (live:false) and live; both render with r (a live engine's render is its safety net, and what
     the Synth page plays), and rel is the amp's release in ms (how a held rendered note fades when let go). */
  xeng(){ const o = {}; for (const k of ORDER){ const tw = twinOf(k);
    o[k] = {live:!!LIVE[k], twin:LIVE[k] ? tw : PsynCore.twin(k), d:() => Object.assign({}, ENG[k].defs), r:(P, sr, hold, V, bpm) => PsynCore.render(tw, P, sr, hold, V, bpm),
      rel:P => tw === 'pcoil' ? (P.release || 0.4) * 1000 : (tw === 'porbit' || tw === 'pgloss' || tw === 'ptract') ? (P.release || 300) : tw === 'pfold' ? (P.fall || 300) : (tw === 'pfreeze' || tw === 'pwob') ? (P.release || 300) : KINDS[tw].kit ? 30 : (P.a_r || 150)}; }
    return o; },
  // XUI entries: the card's rows, sections and the plugin's factory presets
  xui(){
    const o = {};
    for (const k of ORDER){ const E = PsynCore.engines[k], tw = twinOf(k);
      o[k] = {title:E.title, tag:E.tag, rows:ENG[k].rows, secs:ENG[k].secs, pre:ENG[k].pre,
        note:ENG[k].about + ' The …pump plugin’s own engine and values: C-4 plays its MIDI note 60. ' + (LIVE[k]
          ? 'Played live in the tracker, like the plugin in a DAW: the VOL column is each note’s velocity (Velocity here when a note has none), a note sounds for as long as it is held, and slides, bends and vibrato bend its pitch. '
            + 'The Synth page and pad effects use its render, and so does the tracker on a device that can’t keep up (it says so). It shares its values with ' + KINDS[tw].title + ': switch between them and the sound stays.'
          : KINDS[tw].kit ? 'A pad plays one of the kit’s drums (Drum: its page shows); Hit is how hard. Tune speeds it up or slows it down. ' + KINDS[tw].title + ' kit live plays the whole kit from one track: ' + Array.from({length:KINDS[tw].drums || 8}, (_, i) => noteName(whiteKey(i) + 12)).join(' ') + '.' + (tw === 'px0x' ? ' Accent is for hits at or above Accent velocity.' : '')
          : 'Hit is how hard the note is played when the pad renders; ' + (tw === 'pcoil' ? 'the springs ring for their Decay and fade over Release when the note is let go.' : 'a held note fades over the Amp release when it is let go.'))
          + (tw === 'porbit' ? ' CHAOS and SYNC are the two to learn: low Chaos and high Sync is a clean, pitched tone; raise Chaos for period doubling and noise, lower Sync to let it drift. Slides and bends reach ±2 semitones (the plugin’s bend range).' : '')}; }
    return o;
  },
  len: k => (KINDS[twinOf(k)] && KINDS[twinOf(k)].len) || 2,
  // a drum machine pad's drum (its Drum, kd): its name; '' for the other instruments and for a kit live engine (the whole kit)
  drumName: (k, P) => { const K = KINDS[k]; return K && K.kit && K.drumNames ? K.drumNames[Math.max(0, Math.min(K.drumNames.length - 1, Math.round((P && P.kd) || 0)))] || '' : ''; },
  // the latency the instrument reports (samples at sr): MONO's oversampler, GLOSS's
  latency(k, sr){ const K = KINDS[twinOf(k)]; if (!X || !K) return 0; const h = X.psyn_new(K.t, sr || 48000); if (h < 0) return 0; try { return X.psyn_latency(h); } finally { X.psyn_free(h); } },
  /* One note: values staged, a fresh instance (so a pad renders the same every time), note on at 0, note off after the
     hold, then until it dies away (at most 12 s). Folded to mono, the plugin's latency (MONO's oversampler) taken off. */
  render(k, P, sr, hold, V, bpm){
    const K = KINDS[k];
    if (!X || !K) return new Float32Array(1);
    const specs = ENG[k].specs, st = f32(X.psyn_stage(), 512), fx = FIXED[k] || {};
    for (let i = 0; i < specs.length; i++){ const f = fx[specs[i].s], v = f != null ? f : P[al(specs[i].s)]; st[i] = typeof v === 'number' && isFinite(v) ? v : specs[i].def; }
    // the pad's Tune: whole semitones as the note, the rest as a rate (the render made a little slower or faster); a kit
    // plays its Drum's note, Tune all as the rate
    const tune = +P.tune || 0, semis = K.kit ? 0 : Math.round(tune), frac = tune - semis, rate = sr * Math.pow(2, -frac / 12);
    const h = X.psyn_new(K.t, rate);
    if (h < 0) return new Float32Array(1);
    if (X.psyn_tempo && bpm > 0) X.psyn_tempo(h, bpm);   // WOB's wobble: the song's tempo
    // FREEZE's File source: the pad's own sound (kept on its last layer, index.html's frzSample)
    if (k === 'pfreeze' && X.psyn_file && V && V.frzL != null){ const L = V.layers && V.layers[V.frzL], sm = L && L.sample; if (sm && sm.data && sm.data.length){
      const n = Math.min(sm.data.length, Math.round(sm.sr * 20)), p = X.psyn_buf(n); if (p){ f32(p, n).set(sm.data.subarray(0, n)); X.psyn_file(h, n, sm.sr); } } }
    try {
      const note = K.kit ? whiteKey(Math.max(0, Math.min((K.drums || 8) - 1, Math.round(P.kd || 0)))) : Math.max(0, Math.min(127, 60 + semis)), vel = Math.max(1, Math.min(127, Math.round(P.vel || 100)));
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
