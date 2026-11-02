/* …pump TUNE pop-up (31 Oct 2026): plugins/tune/UITune.cpp in the browser. One big screen with a keyboard down its side:
   LIVE   the last six seconds: the pitch sung (thin ink), the pitch that comes out (neon), the target notes, the harmony
          voices (second colour); scale rows tinted, held MIDI notes lit on the keyboard.
   GRAPH  the song's timeline: CAPTURE records the voice's pitch while the song plays and finds its notes (the plugin's
          TuneNotes.hpp, ported below). Drag notes up and down (snaps to semitones; Alt = free), double-click one to snap it
          to the scale, drag on empty space to select several; the strip below sets the selected notes' STRAIGHTEN,
          VIBRATO, FORMANT and GAIN. The corrected pitch is drawn over the sung one.
   Below: CORRECTION, PITCH, HARMONY and OUTPUT. The notes are the slot's 'notes' state (what the DSP plays; an edit is
   one Thunder undo step, a drag or a capture pass one step), the captured pitch its 'curve' state (the window's own).
   Thunder additions (no wheel on a phone): drag the time ruler (the plot's top strip) to scroll, drag the keyboard to
   scroll the pitch, right-click / long-press the graph for a menu (zoom, fit, select, snap, reset). */
(() => {
'use strict';
const U = window.PumpUI, FX = window.PumpFX; if (!U || !FX) return;
const {R, clamp, mix, alpha, Dial, Ctl} = U;

/* ---------------------------------------------------------------- common/dsp/Pitch.hpp: scales */
const KEYS = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const SCALES = ['Chromatic', 'Major', 'Minor', 'Harmonic Minor', 'Melodic Minor', 'Dorian', 'Phrygian', 'Lydian', 'Mixolydian', 'Locrian', 'Major Pentatonic', 'Minor Pentatonic', 'Blues', 'Whole Tone'];
const MASKS = [0xFFF, 0xAB5, 0x5AD, 0x9AD, 0xAAD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x295, 0x4A9, 0x4E9, 0x555];
const RANGES = ['High', 'Mid', 'Low'];
const lround = v => v < 0 ? -Math.round(-v) : Math.round(v);
const pclass = n => ((n % 12) + 12) % 12;
const inScale = (n, key, mask) => ((mask >> pclass(n - key)) & 1) === 1;
function nearestInScale(midi, key, mask){
  if ((mask & 0xFFF) === 0) mask = 0xFFF;
  const c = Math.floor(midi + 0.5);
  for (let d = 0; d <= 6; d++){
    const dn = inScale(c - d, key, mask), up = inScale(c + d, key, mask);
    if (dn && up) return Math.abs(midi - (c - d)) <= Math.abs((c + d) - midi) ? c - d : c + d;
    if (dn) return c - d; if (up) return c + d;
  }
  return c;
}
const noteName = n => KEYS[pclass(n)] + clamp(Math.floor(n / 12) - 1, -9, 99);
const signed = (v, d) => (v >= 0 ? '+' : '') + v.toFixed(d);

/* ---------------------------------------------------------------- plugins/tune/TuneNotes.hpp: the states' text and the note finder */
// "notes": "tune-notes 1", then a line a note: t0 t1 src dst straighten vibrato formant gain
const newNote = (t0, t1, m) => ({t0, t1, src:m, dst:m, straighten:0, vibrato:1, formant:0, gainDb:0});
function notesToText(notes){
  let s = 'tune-notes 1\n';
  for (const n of notes) s += n.t0.toFixed(4) + ' ' + n.t1.toFixed(4) + ' ' + n.src.toFixed(3) + ' ' + n.dst.toFixed(3) + ' ' + n.straighten.toFixed(3) + ' ' +
    n.vibrato.toFixed(3) + ' ' + n.formant.toFixed(2) + ' ' + n.gainDb.toFixed(2) + '\n';
  return s;
}
function notesFromText(text){
  const out = []; if (!text || !text.startsWith('tune-notes')) return out;
  const lines = text.split('\n');
  for (let i = 1; i < lines.length; i++){
    const tok = lines[i].trim().split(/\s+/); if (tok.length < 8) continue;
    const v = tok.slice(0, 8).map(s => parseFloat(s.replace(',', '.')));
    if (!(v[1] > v[0]) || !v.every(Number.isFinite)) continue;
    const n = {t0:v[0], t1:v[1], src:clamp(v[2], 0, 127), dst:clamp(v[3], 0, 127), straighten:clamp(v[4], 0, 1), vibrato:clamp(v[5], 0, 2), formant:clamp(v[6], -12, 12), gainDb:clamp(v[7], -48, 12)};
    const last = out[out.length - 1];
    if (last && n.t0 < last.t1){ if (n.t0 < last.t0) continue; n.t0 = last.t1; if (!(n.t1 > n.t0)) continue; }
    out.push(n);
  }
  return out;
}
// "curve": "tune-curve 1", then runs "R <t0> <hop> <count>" and a line of two characters a frame (1/32 semitones, 0 unvoiced)
const B64 = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
function curveToText(c){
  const parts = ['tune-curve 1\n']; let i = 0;
  while (i < c.length){
    let j = i + 1; const hop = j < c.length ? c[j].t - c[i].t : 0.0053;
    while (j < c.length && j - i < 100000 && hop > 0 && Math.abs((c[j].t - c[j - 1].t) - hop) < 0.1 * hop) ++j;
    parts.push('R ' + c[i].t.toFixed(5) + ' ' + (hop > 0 && hop < 0.05 ? hop : 0.0053).toFixed(7) + ' ' + (j - i) + '\n');
    let s = '';
    for (let k = i; k < j; k++){ const q = c[k].midi > 0 ? clamp(lround(c[k].midi * 32), 1, 4095) : 0; s += B64[q >> 6] + B64[q & 63]; }
    parts.push(s + '\n'); i = j;
  }
  return parts.join('');
}
function curveFromText(text){
  const out = []; if (!text || !text.startsWith('tune-curve')) return out;
  const lines = text.split('\n');
  for (let i = 1; i < lines.length; i++){
    const L = lines[i]; if (L[0] !== 'R') continue;
    const tok = L.slice(1).trim().split(/\s+/), t0 = parseFloat(tok[0]), hop = parseFloat(tok[1]), cnt = clamp(Math.floor(parseFloat(tok[2])) || 0, 0, 1e7);
    if (!Number.isFinite(t0) || !Number.isFinite(hop)) break;
    const d = lines[i + 1] || ''; i++;
    for (let k = 0; k < cnt; k++){
      const a = B64.indexOf(d[2 * k]), b = a >= 0 ? B64.indexOf(d[2 * k + 1]) : -1; if (a < 0 || b < 0 || d[2 * k + 1] === undefined) break;
      const q = (a << 6) | b; out.push({t:t0 + k * hop, midi:q > 0 ? q / 32 : -1});
    }
  }
  return out;
}
// Voiced frames grouped into stretches (gaps up to 40 ms bridged), split where the pitch moves to a new level for 45 ms
// (more than 0.6 semitone from the running centre); notes under 50 ms join a neighbour. A note's pitch: its median.
function findNotes(c, from, to){
  const notes = []; to = Math.min(to, c.length); if (from >= to) return notes;
  const median = (a, b) => { const v = []; for (let k = a; k < b; k++) if (c[k].midi > 0) v.push(c[k].midi); if (!v.length) return 60; v.sort((x, y) => x - y); return v[v.length >> 1]; };
  const hopAt = k => k + 1 < c.length ? Math.min(0.02, c[k + 1].t - c[k].t) : 0.0053;
  let k = from;
  while (k < to){
    while (k < to && !(c[k].midi > 0)) ++k;
    if (k >= to) break;
    const s = k; let e = k + 1, lastV = k;
    while (e < to){ if (c[e].midi > 0) lastV = e; else if (c[e].t - c[lastV].t > 0.040) break; ++e; }
    e = lastV + 1;
    let ns = s, centre = c[s].midi, sum = 0, cnt = 0, away = 0, isAway = false;
    const emit = (a, b) => { if (b <= a) return; const m = median(a, b); notes.push(newNote(c[a].t - 0.5 * hopAt(a), c[b - 1].t + 0.5 * hopAt(b - 1), m)); };
    for (let i = s; i < e; i++){
      if (!(c[i].midi > 0)) continue;
      const m = c[i].midi;
      if (Math.abs(m - centre) > 0.6){
        if (!isAway){ isAway = true; away = i; }
        else if (c[i].t - c[away].t >= 0.045){ emit(ns, away); ns = away; centre = m; sum = m; cnt = 1; isAway = false; }
      } else { isAway = false; sum += m; ++cnt; centre = cnt < 8 ? sum / cnt : centre + (m - centre) * 0.05; }
    }
    emit(ns, e);
    k = e;
  }
  for (let i = 0; i < notes.length;){
    if (notes[i].t1 - notes[i].t0 >= 0.050 || notes.length === 1){ ++i; continue; }
    const hasL = i > 0 && notes[i].t0 - notes[i - 1].t1 < 0.045, hasR = i + 1 < notes.length && notes[i + 1].t0 - notes[i].t1 < 0.045;
    if (!hasL && !hasR){ notes.splice(i, 1); continue; }
    const left = hasL && (!hasR || Math.abs(notes[i - 1].src - notes[i].src) <= Math.abs(notes[i + 1].src - notes[i].src));
    if (left) notes[i - 1].t1 = notes[i].t1; else notes[i + 1].t0 = notes[i].t0;
    notes.splice(i, 1);
  }
  return notes;
}
const lowerT = (arr, v, f) => { let lo = 0, hi = arr.length; while (lo < hi){ const m = (lo + hi) >> 1; if (f(arr[m]) < v) lo = m + 1; else hi = m; } return lo; };

/* ---------------------------------------------------------------- NoteSlider: a slider row that edits the selected notes, not a parameter */
class NoteSlider extends Ctl {
  constructor(label, mn, mx, def, fmt, tip){ super(); Object.assign(this, {label, mn, mx, def, fmtV:fmt, tip}); this.get = null; this.set = null; this.begin = null; this.end = null; this.dragging = false; this.lastClick = -1e9; }
  get wantsText(){ return false; }
  dims(ui){ return ui.narrow ? [74, 52] : [86, 62]; }
  draw(ui){
    const t = ui.th, v = this.get(), has = Number.isFinite(v), r = this.r, [lw, vw] = this.dims(ui);
    ui.text(r.x, r.cy, this.label, 'label', 11, has ? t.ink2 : t.dim, 'left', 0.8);
    const bar = R(r.x + lw, r.cy - 5, r.w - lw - vw - 6, 10); ui.fill(bar, t.panel2);
    if (has){ const n = (v - this.mn) / (this.mx - this.mn), z = (this.def - this.mn) / (this.mx - this.mn), x0 = bar.x + bar.w * Math.min(n, z), x1 = bar.x + bar.w * Math.max(n, z);
      ui.fill(R(x0, bar.y, Math.max(1, x1 - x0), bar.h), t.neon); ui.fill(R(bar.x + bar.w * n - 1.5, bar.y - 3, 3, bar.h + 6), t.ink); }
    ui.stroke(bar, has ? t.ink : t.dim, 1.2);
    ui.text(r.r, r.cy, has ? this.fmtV(v) : '-', 'mono', 11, has ? t.ink : t.dim, 'right');
  }
  down(ui, x, y, btn, m){
    if (btn !== 0) return false; const v = this.get(); if (!Number.isFinite(v)) return true;
    const tn = U.now(), dbl = tn - this.lastClick < 0.35; this.lastClick = tn;
    if (dbl || m.cmd || m.alt){ this.begin(); this.set(this.def); this.end(); ui.status = this.label + '  ' + this.fmtV(this.def) + ' (reset)'; return true; }
    this.dragging = true; this.v0 = v; this.x0 = x; this.begin(); return true;
  }
  drag(ui, x, y, m){
    if (!this.dragging) return; const [lw, vw] = this.dims(ui), bw = Math.max(40, this.r.w - lw - vw - 6);
    this.set(this.v0 + (x - this.x0) / bw * (this.mx - this.mn) * (m.shift ? 0.15 : 1)); ui.status = this.label + '  ' + this.fmtV(this.get());
  }
  up(){ if (this.dragging){ this.dragging = false; this.end(); } }
  wheel(ui, dy, m){ const v = this.get(); if (!Number.isFinite(v)) return true; this.begin(); this.set(v + (dy > 0 ? 1 : -1) * (this.mx - this.mn) * (m.shift ? 0.005 : 0.05)); this.end(); ui.status = this.label + '  ' + this.fmtV(this.get()); return true; }
}

const FOLLOW_KEY = 'thunderplus-pump-tune-follow-v1', DEG = ['', '2nd', '3rd', '4th', '5th', '6th', '7th', '8ve'], NV = 4, HIST = 360, B = 5;

U.define('tune', {W:1100, H:620, nW:480, nH:1000, bypass:true, cmdKeys:'a', make(ui){
  const V = s => ui.value(s), graphMode = () => Math.round(V('mode')) === 1;
  const key = () => clamp(Math.round(V('key')), 0, 11), mask = () => MASKS[clamp(Math.round(V('scale')), 0, MASKS.length - 1)];
  // UITune::formatParam
  function tfmt(sym, v){
    if (sym === 'flex' || sym === 'vib_depth') return v < 0.5 ? 'OFF' : v.toFixed(0) + ' c';
    if (sym === 'formant' || sym === 'harm_formant') return Math.abs(v) < 0.05 ? '0.0 st' : signed(v, 1) + ' st';
    if (sym === 'transpose'){ const n = lround(v); return n === 0 ? '0 st' : (n > 0 ? '+' : '') + n + ' st'; }
    if (/^h[1-4]$/.test(sym)){ const n = lround(v); if (!n) return 'OFF'; if (Math.round(V('harm_mode')) === 1 && Math.abs(n) <= 7) return (n > 0 ? '+' : '-') + DEG[Math.abs(n)]; return (n > 0 ? '+' : '') + n; }
    if (/^h[1-4]_pan$/.test(sym)) return Math.abs(v) < 0.5 ? 'C' : (v < 0 ? 'L' : 'R') + Math.abs(v).toFixed(0);
    if (sym === 'retune' && v < 0.5) return 'INSTANT';
    return FX.format('tune', sym, v);
  }
  const dial = (sym, label, size, bip, tip, group) => { const d = ui.dial(sym, label, size, bip); d.tip = tip; d.fmt = v => tfmt(sym, v); if (group) d.tabGroup = group; return d; };

  // ---- CORRECTION ----
  const modeSeg = ui.seg('mode', ['LIVE', 'GRAPH']); modeSeg.tip = 'Live: corrects to the key and scale (or MIDI) as you sing · Graph: capture the voice as notes and edit them   (Tab switches)';
  const keyMenu = ui.menuBtn('key'); keyMenu.tip = 'Key'; keyMenu.textFn = () => 'KEY ' + KEYS[key()];
  const scaleMenu = ui.menuBtn('scale'); scaleMenu.tip = 'Scale: the notes the voice may land on (Chromatic = every semitone)';
  const midiMenu = ui.menuBtn('midi', 'MIDI'); midiMenu.tip = 'MIDI sent to TUNE (the FX screen’s NOTES FROM picks the track): Lead notes = the voice goes to the held note nearest it; Harmony notes = each held note becomes a harmony voice';
  const rangeSeg = ui.seg('range', ['HIGH', 'MID', 'LOW']); rangeSeg.tip = 'Voice range: High 100-1200 Hz, Mid 70-900 Hz, Low 50-600 Hz (lower ranges need a little more latency)';
  const retuneDial = dial('retune', 'RETUNE', 'm', false, 'Retune speed: how fast the voice is pulled onto the note (0 = at once: the hard, stepped sound)', 1);
  const amountDial = dial('amount', 'AMOUNT', 'm', false, 'Amount of correction', 1);
  const humanDial = dial('humanize', 'HUMANIZE', 'm', false, 'Humanize: correct the note’s centre and leave vibrato and small movements alone', 1);
  const flexDial = dial('flex', 'FLEX', 'm', false, 'Flex: notes already within this many cents of the target are left alone (only bigger misses are fixed)', 1);
  // ---- PITCH / FORMANT ----
  const vibDial = dial('vib_depth', 'VIBRATO', 's', false, 'Added vibrato depth (cents), faded in after each new note', 2);
  const rateDial = dial('vib_rate', 'RATE', 's', false, 'Added vibrato rate', 2);
  const transDial = dial('transpose', 'TRANSPOSE', 's', true, 'Transpose (semitones)', 2);
  const formDial = dial('formant', 'FORMANT', 's', true, 'Formant: the voice’s character up (smaller, brighter) or down (bigger, darker) without changing the pitch', 2);
  const followBtn = ui.toggle('formant_follow', 'FORMANTS FOLLOW'); followBtn.small = true; followBtn.tip = 'Formants follow the pitch (the chipmunk / giant sound); off keeps them where they were';
  // ---- HARMONY ----
  const harmSeg = ui.seg('harm_mode', ['OFF', 'SCALE', 'FIXED']); harmSeg.tip = 'Harmony: Scale = steps of the scale above or below the corrected note (2 = a third, 4 = a fifth); Fixed = semitones; with MIDI = Harmony notes the held notes';
  const intDial = [], lvlDial = [], panDial = [];
  for (let h = 0; h < NV; h++){
    intDial[h] = dial('h' + (h + 1), 'VOICE ' + (h + 1), 's', true, 'Harmony interval (scale steps in Scale, semitones in Fixed)', 3);
    lvlDial[h] = dial('h' + (h + 1) + '_level', 'LEVEL', 's', false, 'Harmony level (0 = off)', 3);
    panDial[h] = dial('h' + (h + 1) + '_pan', 'PAN', 's', true, 'Harmony pan', 3);
  }
  // ---- OUTPUT ----
  const leadDial = dial('lead', 'LEAD', 's', false, 'The corrected lead’s level', 4);
  const harmLvlDial = dial('harm_level', 'HARMONY', 's', true, 'All harmonies’ level', 4);
  const harmFormDial = dial('harm_formant', 'H FORMANT', 's', true, 'Harmonies’ formant shift (other singers: down for deeper voices, up for lighter ones)', 4);
  const harmHumDial = dial('harm_humanize', 'H HUMAN', 's', false, 'Harmony humanize: each voice drifts by a few cents and a few ms, like separate singers', 4);
  const outDial = dial('out_gain', 'OUTPUT', 's', true, 'Output gain', 4);

  // ---- the graph: state ----
  let notes = [], sel = [], curve = [], lp = [], lastNotesText = null, lastCurveText = null;
  let notesDirty = false, curveDirty = false, lastSend = 0, lastCurveSend = 0, group = 0;
  let capturing = false, passOpen = false, passStart = 0, passEnd = 0, capQ = [], capSentAt = 0;
  let vt0 = 0, vSpan = 8, vp0 = 48, vSpanP = 24, playhead = 0, playing = false, clearArmed = 0, fitWhenLoaded = true;
  let dmode = 0, dragY = 0, dragStart = [], mx0 = 0, my0 = 0, mx1 = 0, my1 = 0, dragX0 = 0, dragV0 = 0;   // dmode 1 notes, 2 marquee, 3 ruler, 4 keyboard
  let followView = (() => { try { return localStorage.getItem(FOLLOW_KEY) !== '0'; } catch(e){ return true; } })();
  // live history (60 a second, six seconds)
  const hIn = new Float32Array(HIST).fill(-1), hOut = new Float32Array(HIST).fill(-1), hTgt = new Float32Array(HIST).fill(-1), hHarm = [0, 1, 2, 3].map(() => new Float32Array(HIST).fill(-1));
  let hPos = 0, histT = 0, liveLo = 50, liveHi = 66, lastFr = -1e9;
  const now = {inM:-1, outM:-1, tgt:-1, harm:[-1, -1, -1, -1]}, held = new Uint16Array(8);

  // ---- the graph's tools ----
  const captureBtn = ui.button('CAPTURE', () => setCapture(!capturing), () => capturing); captureBtn.small = true;
  captureBtn.tip = 'Capture: while on, playing the song records the voice’s pitch here and finds its notes (play the part once, then edit)';
  const followViewBtn = ui.button('FOLLOW', () => setFollow(!followView), () => followView); followViewBtn.small = true; followViewBtn.tip = 'Follow: the view scrolls with the playhead';
  const snapBtn = ui.button('SNAP', () => snapNotes()); snapBtn.small = true; snapBtn.tip = 'Snap the selected notes (all when none are) to the key and scale (key S)';
  const resetBtn = ui.button('RESET', () => resetNotes()); resetBtn.small = true; resetBtn.tip = 'Reset the selected notes (all when none are) to how they were sung (Delete)';
  const clearBtn = ui.button('CLEAR', () => clearAll(), () => clearArmed > 0); clearBtn.small = true; clearBtn.tip = 'Clear everything captured (click twice)';
  const anySel = () => sel.some(Boolean);
  const mkSlider = (label, mn, mx, def, field, fmt, tip) => {
    const s = ui.add(new NoteSlider(label, mn, mx, def, fmt, tip));
    s.get = () => { for (let i = 0; i < notes.length; i++) if (sel[i]) return notes[i][field]; return notes.length ? notes[0][field] : NaN; };
    s.set = v => { v = clamp(v, mn, mx); const any = anySel(); for (let i = 0; i < notes.length; i++) if (!any || sel[i]) notes[i][field] = v; notesChanged(false); };
    s.begin = () => beginGroup(); s.end = () => { endGroup(); notesChanged(true); };
    return s;
  };
  const pct = v => (100 * v).toFixed(0) + '%';
  const straightSl = mkSlider('STRAIGHTEN', 0, 1, 0, 'straighten', pct, 'Straighten: pulls the selected notes’ slow drift toward their centre (100 % = a flat line)');
  const vibSl = mkSlider('VIBRATO', 0, 2, 1, 'vibrato', pct, 'Vibrato: the selected notes’ own vibrato, removed (0) to doubled (200 %)');
  const formSl = mkSlider('FORMANT', -12, 12, 0, 'formant', v => signed(v, 1) + ' st', 'Formant of the selected notes');
  const gainSl = mkSlider('GAIN', -24, 12, 0, 'gainDb', v => signed(v, 1) + ' dB', 'Level of the selected notes');

  let screenR = R(), corrPanel = R(), pitchPanel = R(), harmPanel = R(), outPanel = null, sepY = 0;
  function setFollow(on){ followView = on; try { localStorage.setItem(FOLLOW_KEY, on ? '1' : '0'); } catch(e){} }
  function updateVisibility(){
    const g = graphMode();
    for (const c of [captureBtn, followViewBtn, snapBtn, resetBtn, clearBtn, straightSl, vibSl, formSl, gainSl]) c.visible = g;
    const midi = Math.round(V('midi')), harm = Math.round(V('harm_mode')) !== 0 || midi === 2;
    for (let h = 0; h < NV; h++){ intDial[h].enabled = harm && midi !== 2; lvlDial[h].enabled = panDial[h].enabled = harm; }
    for (const c of [retuneDial, amountDial, humanDial, flexDial]) c.enabled = !g;
  }

  // ---- notes: the slot's state (what the DSP plays) and Thunder's undo ----
  // A gesture (a drag, a slider) sends quietly while it lasts and once more at its end: one undo step. A capture sends
  // quietly too (one step a pass). Anything else is its own step.
  function beginGroup(){ group++; }
  function endGroup(){ if (group > 0) group--; }
  function sendNotes(final){
    const tx = notesToText(notes); notesDirty = false; lastSend = U.now();
    const quiet = !final && (group > 0 || capturing);
    if (tx === lastNotesText && quiet) return;
    lastNotesText = tx; FX.setState(ui.t, ui.k, 'notes', tx, quiet ? {quiet:true, src:ui.id} : {src:ui.id});
  }
  function sendCurve(){ const tx = curveToText(curve); curveDirty = false; lastCurveSend = U.now(); if (tx === lastCurveText) return; lastCurveText = tx; FX.setState(ui.t, ui.k, 'curve', tx, {quiet:true, src:ui.id}); }
  function notesChanged(final){ notesDirty = true; if (final || U.now() - lastSend > 0.06) sendNotes(final); ui.dirty = true; }
  function readState(){
    const nt = FX.getState(ui.t, ui.k, 'notes') || '', ct = FX.getState(ui.t, ui.k, 'curve') || '';
    const had = notes.length + curve.length;
    if (nt !== lastNotesText){ notes = notesFromText(nt); sel = notes.map(() => false); lastNotesText = nt; notesDirty = false; }
    if (ct !== lastCurveText){ curve = curveFromText(ct); recomputeLp(); lastCurveText = ct; curveDirty = false; }
    if (!had && notes.length + curve.length) fitWhenLoaded = true;
  }
  function snapNotes(){
    const any = anySel(), k = key(), m = mask();
    for (let i = 0; i < notes.length; i++) if (!any || sel[i]) notes[i].dst = nearestInScale(notes[i].dst, k, m);
    notesChanged(true); ui.status = any ? 'Snapped the selected notes to the scale' : 'Snapped every note to the scale';
  }
  function resetNotes(){
    const any = anySel();
    for (let i = 0; i < notes.length; i++) if (!any || sel[i]) Object.assign(notes[i], {dst:notes[i].src, straighten:0, vibrato:1, formant:0, gainDb:0});
    notesChanged(true); ui.status = any ? 'Reset the selected notes' : 'Reset every note';
  }
  function clearAll(){
    if (clearArmed <= 0){ clearArmed = 3; ui.status = 'Click CLEAR again to delete everything captured'; return; }
    clearArmed = 0; notes = []; sel = []; curve = []; lp = []; passOpen = false;
    sendCurve(); sendNotes(true);   // (the curve quietly first: the notes' step takes both)
    ui.status = 'Cleared';
  }
  function setCapture(on){
    capturing = on; ui.hold(3, 1, on ? 1 : 0); capSentAt = U.now();
    if (!on){ if (capQ.length){ const b = capQ; capQ = []; drainCapture(b); } if (curveDirty) sendCurve(); if (notesDirty) sendNotes(true); }
    passOpen = false;
    if (on && !graphMode()) ui.setGesture('mode', 1);
    ui.status = on ? 'Capturing: play the song (the voice’s pitch is recorded while Thunder plays)' : 'Capture off'; ui.dirty = true;
  }

  // ---- capture: frames from the telemetry into the curve, notes found as they arrive (UITune::drainCapture) ----
  function drainCapture(batch){
    if (!batch.length) return;
    const f0 = batch[0].t, fN = batch[batch.length - 1].t;
    if (!passOpen || f0 < passEnd - 0.002 || f0 > passEnd + 0.1){ passOpen = true; passStart = f0; passEnd = passStart - 1e-6; }
    const a = passEnd + 1e-6, b = fN + 1e-4, ft = f => f.t;
    const lo = lowerT(curve, a, ft), hi = Math.max(lo, lowerT(curve, b, ft));
    curve.splice(lo, hi - lo, ...batch);
    passEnd = fN;
    let from = Math.max(passStart, a - 0.4); const old = [];
    for (let i = 0; i < notes.length;){
      if (notes[i].t1 > from && notes[i].t0 < b + 0.05){ from = Math.min(from, notes[i].t0); old.push(notes[i]); notes.splice(i, 1); sel.splice(i, 1); }
      else ++i;
    }
    from = Math.max(from, passStart - 0.0001);
    const found = findNotes(curve, lowerT(curve, from, ft), lowerT(curve, b, ft));
    for (const n of found) for (const o of old){   // a note sung again in the same place keeps its edits
      const ov = Math.min(n.t1, o.t1) - Math.max(n.t0, o.t0);
      if (ov > 0.5 * Math.min(n.t1 - n.t0, o.t1 - o.t0) && Math.abs(n.src - o.src) < 0.5){ Object.assign(n, {dst:n.src + (o.dst - o.src), straighten:o.straighten, vibrato:o.vibrato, formant:o.formant, gainDb:o.gainDb}); break; }
    }
    for (const n of found){ const k = lowerT(notes, n.t0, x => x.t0); notes.splice(k, 0, n); sel.splice(k, 0, false); }
    for (let i = 1; i < notes.length; i++) if (notes[i].t0 < notes[i - 1].t1) notes[i].t0 = notes[i - 1].t1;
    for (let i = 0; i < notes.length;){ if (!(notes[i].t1 > notes[i].t0 + 0.005)){ notes.splice(i, 1); sel.splice(i, 1); } else ++i; }
    recomputeLp(); curveDirty = true; notesDirty = true; ui.dirty = true;
  }
  const isSpike = i => {
    if (i === 0 || i + 1 >= curve.length) return false;
    const a = curve[i - 1].midi, b = curve[i].midi, c = curve[i + 1].midi; if (a <= 0 && c <= 0) return false;
    return (a <= 0 || Math.abs(b - a) > 1.5) && (c <= 0 || Math.abs(b - c) > 1.5);
  };
  function recomputeLp(){   // the pitch's slow centre (two one-poles at 2.5 Hz, like the engine), for the corrected curve
    lp = new Float32Array(curve.length); let a = -1, b = -1, lastT = -1e9;
    for (let i = 0; i < curve.length; i++){
      const m = curve[i].midi; if (m <= 0 || isSpike(i)){ lp[i] = -1; continue; }
      const dt = clamp(curve[i].t - lastT, 0.001, 0.05);
      if (a < 0 || Math.abs(m - b) > 1.5 || curve[i].t - lastT > 0.1) a = b = m;
      const k = 1 - Math.exp(-dt * 2 * Math.PI * 2.5); a += (m - a) * k; b += (a - b) * k;
      lp[i] = b; lastT = curve[i].t;
    }
  }
  function fitView(){
    if (!notes.length && !curve.length) return;
    let lo = 1e9, hi = -1e9;
    for (const n of notes){ lo = Math.min(lo, n.src, n.dst); hi = Math.max(hi, n.src, n.dst); }
    if (lo > hi) for (const f of curve) if (f.midi > 0){ lo = Math.min(lo, f.midi); hi = Math.max(hi, f.midi); }
    if (lo > hi) return;
    vSpanP = clamp(hi - lo + 8, 12, 48); vp0 = 0.5 * (lo + hi) - 0.5 * vSpanP;
    vt0 = notes.length ? notes[0].t0 - 0.5 : curve[0].t - 0.5;
    const tEnd = notes.length ? notes[notes.length - 1].t1 : curve[curve.length - 1].t;
    if (tEnd - vt0 + 0.5 < 600) vSpan = clamp(Math.max(vSpan, Math.min(tEnd - vt0 + 0.5, 12)), 0.25, 600);   // (Thunder: a short capture fits in view)
    vp0 = clamp(vp0, 0, 127 - vSpanP);
  }

  // ---- geometry ----
  const plotRect = () => R(screenR.x + 44, screenR.y + 36, screenR.w - 54, screenR.h - 58);
  const keysRect = () => { const p = plotRect(); return R(screenR.x + 8, p.y, 32, p.h); };
  const rulerRect = () => { const p = plotRect(); return R(p.x, p.y, p.w, 16); };
  const pLo = () => graphMode() ? vp0 : liveLo, pHi = () => graphMode() ? vp0 + vSpanP : liveHi;
  const yOf = (p, m) => p.b - (m - pLo()) / Math.max(1e-3, pHi() - pLo()) * p.h;
  const mOfY = (p, y) => pLo() + (p.b - y) / p.h * (pHi() - pLo());
  const xOfT = (p, t) => p.x + (t - vt0) / vSpan * p.w, tOfX = (p, x) => vt0 + (x - p.x) / p.w * vSpan;
  const last = h => h[(hPos + HIST - 1) % HIST];

  // ---- drawing ----
  function strokePath(c, w){ const g = ui.g; g.strokeStyle = U.css(c); g.lineWidth = w; g.lineJoin = 'round'; g.stroke(); }
  function drawScreen(){
    const r = screenR, t = ui.th, g = graphMode(); ui.screenBox(r);
    ui.caption(r.x + 10, r.y + 18, g ? 'GRAPH' : 'LIVE');
    if (g){
      ui.text(r.x + 70, r.y + 18, notes.length + ' NOTES', 'mono', 10, t.ink2, 'left');
      if (capturing){ const blink = playing ? (U.now() % 1) < 0.6 : true;
        ui.text(r.x + 160, r.y + 18, playing ? 'CAPTURING' : 'CAPTURE ARMED: PLAY THE SONG', 'label', 11, blink ? t.red : t.ink2, 'left', 1.2); }
    } else ui.text(r.x + 60, r.y + 18, (KEYS[key()] + ' ' + SCALES[clamp(Math.round(V('scale')), 0, 13)]).toUpperCase(), 'label', 11, t.ink2, 'left', 1);
    const p = plotRect(), k = key(), m = mask();
    ui.clip(p, () => {
      for (let n = Math.floor(pLo()); n <= Math.ceil(pHi()); n++){
        const y0 = yOf(p, n + 0.5), y1 = yOf(p, n - 0.5);
        if (inScale(n, k, m)) ui.fill(R(p.x, y0, p.w, y1 - y0), alpha(t.panel2, t.dark ? 0.55 : 0.45));
        if (pclass(n) === 0) ui.line(p.x, y1, p.r, y1, alpha(t.ink, 0.22), 1);
      }
      if (g) drawGraph(p); else drawLive(p);
    });
    drawKeys(keysRect());
    ui.stroke(p, alpha(t.ink, 0.35), 1);
  }
  function drawKeys(kr){
    const t = ui.th, p = plotRect(), tgt = last(hTgt), g = graphMode();
    ui.clip(kr, () => {
      for (let n = Math.floor(pLo()) - 1; n <= Math.ceil(pHi()) + 1; n++){
        const y0 = yOf(p, n + 0.5), y1 = yOf(p, n - 0.5), c = pclass(n), black = c === 1 || c === 3 || c === 6 || c === 8 || c === 10;
        const lit = (n >= 0 && n < 128 && ((held[n >> 4] >> (n & 15)) & 1)) || (!g && tgt > 0 && lround(tgt) === n);
        ui.fill(R(kr.x, y0, black ? kr.w * 0.62 : kr.w, y1 - y0), lit ? t.neon : (black ? t.ink : t.bg));
        ui.line(kr.x, y1, kr.r, y1, alpha(t.ink, 0.35), 1);
        if (c === 0 && y1 - y0 > 6) ui.text(kr.r - 2, 0.5 * (y0 + y1), noteName(n), 'mono', Math.min(9, y1 - y0), t.ink2, 'right');
      }
    });
    ui.stroke(kr, t.ink, 1);
  }
  function drawLive(p){
    const t = ui.th, g = ui.g;
    for (let s = 1; s < 6; s++) ui.line(p.x + p.w * s / 6, p.y, p.x + p.w * s / 6, p.b, t.grid, 1);
    const xAt = i => p.x + p.w * i / (HIST - 1);
    const trace = (h, c, w) => { let pen = false; g.beginPath();
      for (let i = 0; i < HIST; i++){ const v = h[(hPos + i) % HIST]; if (v <= 0){ pen = false; continue; } const x = xAt(i), y = yOf(p, v); if (!pen){ g.moveTo(x, y); pen = true; } else g.lineTo(x, y); }
      strokePath(c, w); };
    for (let i = 0; i < HIST;){   // the target notes as bars
      const v = hTgt[(hPos + i) % HIST]; let j = i + 1; while (j < HIST && hTgt[(hPos + j) % HIST] === v) ++j;
      if (v > 0){ const x0 = xAt(i), x1 = xAt(j - 1), y0 = yOf(p, v + 0.5), y1 = yOf(p, v - 0.5); ui.fill(R(x0, y0, Math.max(1, x1 - x0), y1 - y0), alpha(t.neon, 0.35)); }
      i = j;
    }
    for (let h = 0; h < NV; h++) trace(hHarm[h], alpha(t.second, 0.9), 2);
    trace(hIn, alpha(t.ink, 0.55), 1.2); trace(hOut, t.ink, 3.2); trace(hOut, t.neon, 1.6);
    ui.text(p.x + 6, p.b - 10, 'SUNG', 'label', 9.5, t.ink2, 'left', 1);
    ui.text(p.x + 44, p.b - 10, 'OUT', 'label', 9.5, mix(t.neon, t.ink, 0.5), 'left', 1);
    ui.text(p.x + 76, p.b - 10, 'HARMONY', 'label', 9.5, t.second, 'left', 1);
  }
  function drawGraph(p){
    const t = ui.th, g = ui.g;
    let st = 60; for (const s of [0.1, 0.25, 0.5, 1, 2, 5, 10, 30, 60]) if (vSpan / s <= (ui.narrow ? 7 : 14)){ st = s; break; }
    ui.fill(R(p.x, p.y, p.w, 16), alpha(t.screen, 0.6));   // (Thunder: the ruler strip, drag it to scroll)
    for (let tt = Math.ceil(vt0 / st) * st; tt < vt0 + vSpan; tt += st){
      const x = xOfT(p, tt); ui.line(x, p.y, x, p.b, t.grid, 1);
      if (tt < -1e-6) continue;   // (the song starts at 0:00)
      const mm = Math.trunc(tt / 60), sec = tt - 60 * mm;
      ui.text(x + 3, p.y + 8, mm + ':' + (st < 1 ? sec.toFixed(2).padStart(5, '0') : sec.toFixed(0).padStart(2, '0')), 'mono', 9, t.ink2, 'left');
    }
    if (!notes.length && !curve.length){
      ui.text(p.cx, p.cy - 10, 'TURN CAPTURE ON AND PLAY THE SONG', 'label', 13, t.ink2, 'center', 1.5);
      ui.text(p.cx, p.cy + 12, 'the voice is recorded as notes you can drag up and down', 'label', 11.5, t.ink2, 'center', 0.6);
    }
    const rowH = p.h / Math.max(1, vSpanP), h = Math.max(5, rowH * 0.8);
    for (let i = 0; i < notes.length; i++){   // the sung place as an outline, the new place filled
      const n = notes[i]; if (n.t1 < vt0 || n.t0 > vt0 + vSpan) continue;
      const x0 = xOfT(p, n.t0), x1 = xOfT(p, n.t1);
      if (Math.abs(n.dst - n.src) > 0.01){ const ys = yOf(p, n.src);
        ui.stroke(R(x0, ys - h / 2, Math.max(2, x1 - x0), h), alpha(t.ink, 0.35), 1); ui.line((x0 + x1) / 2, ys, (x0 + x1) / 2, yOf(p, n.dst), alpha(t.ink, 0.35), 1); }
      const nr = R(x0, yOf(p, n.dst) - h / 2, Math.max(2, x1 - x0), h);
      ui.fill(nr, sel[i] ? t.ink : alpha(t.neon, t.dark ? 0.5 : 0.8)); ui.stroke(nr, sel[i] ? t.neon : t.ink, sel[i] ? 2 : 1.2);
      if (nr.w > 18 && (n.straighten > 0.01 || Math.abs(n.vibrato - 1) > 0.01 || Math.abs(n.formant) > 0.05 || Math.abs(n.gainDb) > 0.05))
        ui.text(nr.x + 3, nr.cy, '*', 'bold', 10, sel[i] ? t.on : t.ink, 'left');
    }
    // the pitch: sung (thin) and corrected (thick)
    const ft = f => f.t, a = lowerT(curve, vt0 - 0.1, ft), b = lowerT(curve, vt0 + vSpan + 0.1, ft), stride = Math.max(1, Math.floor((b - a) / Math.max(1, p.w * 1.5)));
    const curveTrace = (corrected, c, w) => {
      g.beginPath(); let pen = false, lastT = -1e9, ni = 0;
      for (let i = a; i < b; i += stride){
        const f = curve[i]; if (f.midi <= 0 || f.t - lastT > 0.05) pen = false; if (f.midi <= 0 || isSpike(i)) continue;
        let mm = f.midi;
        if (corrected){ while (ni < notes.length && notes[ni].t1 <= f.t) ++ni;
          if (ni < notes.length && f.t >= notes[ni].t0){ const n = notes[ni], l = lp[i] > 0 ? lp[i] : f.midi; mm = n.dst + (l - n.src) * (1 - n.straighten) + (f.midi - l) * n.vibrato; } }
        const x = xOfT(p, f.t), y = yOf(p, mm); if (!pen){ g.moveTo(x, y); pen = true; } else g.lineTo(x, y); lastT = f.t;
      }
      strokePath(c, w);
    };
    curveTrace(false, alpha(t.ink, 0.45), 1.1); curveTrace(true, t.second, 1.8);
    if (playhead >= vt0 && playhead <= vt0 + vSpan){ const x = xOfT(p, playhead); ui.line(x, p.y, x, p.b, playing ? t.red : alpha(t.ink, 0.5), 1.5); }
    if (dmode === 2){ const mr = R(Math.min(mx0, mx1), Math.min(my0, my1), Math.abs(mx1 - mx0), Math.abs(my1 - my0)); ui.fill(mr, alpha(t.neon, 0.15)); ui.stroke(mr, t.ink, 1); }
    ui.text(p.x + 6, p.b - 10, 'SUNG', 'label', 9.5, t.ink2, 'left', 1);
    ui.text(p.x + 44, p.b - 10, 'CORRECTED', 'label', 9.5, t.second, 'left', 1);
  }
  function drawBody(){
    const t = ui.th; drawScreen();
    for (const r of [corrPanel, pitchPanel, harmPanel, outPanel]) if (r) ui.stroke(r, t.dim, 1);
    ui.caption(corrPanel.x + 12, corrPanel.y + 16, 'CORRECTION');
    if (graphMode()){ if (ui.narrow) ui.text(corrPanel.r - 12, corrPanel.y + 16, 'LIVE ONLY: THE GRAPH SETS ITS NOTES', 'label', 10, t.ink2, 'right', 1);
      else ui.caption(corrPanel.x + 12, retuneDial.r.y - 12, 'LIVE ONLY (THE GRAPH SETS ITS OWN NOTES)'); }
    ui.caption(pitchPanel.x + 12, pitchPanel.y + 16, 'PITCH');
    ui.caption(harmPanel.x + 12, harmPanel.y + 16, 'HARMONY');
    if (outPanel) ui.caption(outPanel.x + 12, outPanel.y + 16, 'OUTPUT');
    else { ui.line(pitchPanel.x + 8, sepY, pitchPanel.r - 8, sepY, t.dim, 1); const w = ui.textW('OUTPUT', 'label', 11, 1.2) + 12;
      ui.fill(R(pitchPanel.x + 6, sepY - 7, w, 14), t.bg); ui.caption(pitchPanel.x + 12, sepY, 'OUTPUT');
      ui.text(followBtn.r.cx, vibDial.r.y + 7, 'FORMANTS', 'label', 10.5, t.ink2, 'center', 1);
      ui.line(harmPanel.cx, harmPanel.y + 26, harmPanel.cx, harmPanel.b - 6, t.dim, 1); }
    if (!graphMode()){   // the live readout where the graph's strip would be
      const inM = last(hIn), outM = last(hOut);
      let a = 'SING: THE PITCH SUNG AND THE PITCH THAT COMES OUT ARE DRAWN ABOVE', b = '';
      if (inM > 0 && outM > 0){ const ai = lround(inM), co = lround(outM);
        a = 'SUNG ' + noteName(ai) + ' ' + signed(lround(100 * (inM - ai)), 0) + ' c      SOUNDS ' + noteName(co) + ' ' + signed(lround(100 * (outM - co)), 0) + ' c';
        b = 'CORRECTION ' + signed(lround(100 * (outM - inM)), 0) + ' c'; }
      if (!ui.narrow){ const ry = straightSl.r.cy; ui.text(20, ry, b ? a + '      ' + b : a, 'label', 11.5, t.ink2, 'left', 1); ui.text(ui.W - 20, ry, ui.latencyText(), 'label', 11, t.ink2, 'right', 1); }
      else { const y1 = captureBtn.r.cy, y2 = straightSl.r.cy + 4;
        if (b){ ui.text(16, y1, a, 'label', 11.5, t.ink2, 'left', 0.6); ui.text(16, y2, b, 'label', 11.5, t.ink2, 'left', 0.6); }
        else { ui.text(16, y1, 'SING: THE PITCH SUNG AND THE PITCH THAT', 'label', 11, t.ink2, 'left', 0.6); ui.text(16, y2, 'COMES OUT ARE DRAWN ABOVE', 'label', 11, t.ink2, 'left', 0.6); }
        ui.text(ui.W - 16, y2, ui.latencyText(), 'label', 11, t.ink2, 'right', 0.6); }
    }
  }

  // ---- layout: the plugin's (1100 x 620) and a narrow one (480 wide) ----
  function layout(narrow){
    const dh = Dial.heightFor('m'), sh = Dial.heightFor('s');
    if (!narrow){
      const ph = 238, y0 = ui.H - U.FOOT - 10 - ph, sy = y0 - 12 - 26;
      screenR = R(14, 56, 1072, sy - 12 - 56);
      const by = screenR.y + 7, bh = 22; let x = screenR.r - 10;
      const place = (b, w) => { x -= w; b.r = R(x, by, w, bh); x -= 6; };
      place(clearBtn, 62); place(resetBtn, 62); place(snapBtn, 56); place(followViewBtn, 70); place(captureBtn, 82);
      const sw = 254, sg = 18.7; [straightSl, vibSl, formSl, gainSl].forEach((s, i) => s.r = R(18 + i * (sw + sg), sy, sw, 26));
      corrPanel = R(14, y0, 410, ph);
      modeSeg.r = R(26, y0 + 28, 120, 26); keyMenu.r = R(156, y0 + 28, 82, 26); scaleMenu.r = R(244, y0 + 28, 168, 26);
      midiMenu.r = R(26, y0 + 64, 212, 26); rangeSeg.r = R(244, y0 + 64, 168, 26);
      [retuneDial, amountDial, humanDial, flexDial].forEach((d, i) => d.r = R(30 + 96 * i, y0 + 128, 90, dh));
      pitchPanel = R(436, y0, 180, ph);
      vibDial.r = R(446, y0 + 30, 76, sh); rateDial.r = R(530, y0 + 30, 76, sh); transDial.r = R(446, y0 + 98, 76, sh); formDial.r = R(530, y0 + 98, 76, sh);
      followBtn.label = 'FORMANTS FOLLOW'; followBtn.r = R(448, y0 + 186, 156, 26);
      harmPanel = R(628, y0, 290, ph); harmSeg.r = R(724, y0 + 6, 182, 20);
      for (let h = 0; h < NV; h++){ const hx = 636 + h * 69; intDial[h].r = R(hx, y0 + 30, 68, sh); lvlDial[h].r = R(hx, y0 + 98, 68, sh); panDial[h].r = R(hx, y0 + 166, 68, sh); }
      outPanel = R(930, y0, 156, ph);
      leadDial.r = R(936, y0 + 30, 72, sh); harmLvlDial.r = R(1008, y0 + 30, 72, sh); harmFormDial.r = R(936, y0 + 98, 72, sh); harmHumDial.r = R(1008, y0 + 98, 72, sh); outDial.r = R(972, y0 + 166, 72, sh);
    } else {
      // mode, key and scale over the screen; the graph's tools and note sliders under it; then CORRECTION, PITCH with
      // OUTPUT, and HARMONY (two voices a row)
      modeSeg.r = R(10, 90, 132, 28); keyMenu.r = R(150, 90, 92, 28); scaleMenu.r = R(250, 90, 220, 28);
      screenR = R(10, 128, 460, 226);
      const ty = 364; captureBtn.r = R(10, ty, 96, 26); followViewBtn.r = R(112, ty, 84, 26); snapBtn.r = R(202, ty, 76, 26); resetBtn.r = R(284, ty, 86, 26); clearBtn.r = R(376, ty, 94, 26);
      straightSl.r = R(10, 398, 226, 24); vibSl.r = R(244, 398, 226, 24); formSl.r = R(10, 428, 226, 24); gainSl.r = R(244, 428, 226, 24);
      corrPanel = R(10, 460, 460, 146); midiMenu.r = R(20, 484, 216, 24); rangeSeg.r = R(244, 484, 216, 24);
      [retuneDial, amountDial, humanDial, flexDial].forEach((d, i) => d.r = R(10 + 115 * i + 12.5, 516, 90, dh));
      pitchPanel = R(10, 614, 460, 184); outPanel = null; sepY = 714;
      const cw = 92, col = (d, i, y) => d.r = R(10 + cw * i + (cw - 76) / 2, y, 76, sh);
      [vibDial, rateDial, transDial, formDial].forEach((d, i) => col(d, i, 636)); followBtn.label = 'FOLLOW'; followBtn.r = R(10 + cw * 4 + 6, 662, cw - 12, 26);
      [leadDial, harmLvlDial, harmFormDial, harmHumDial, outDial].forEach((d, i) => col(d, i, 722));
      harmPanel = R(10, 806, 460, 166); harmSeg.r = R(200, 810, 260, 20);
      const hw = 460 / 6;
      for (let h = 0; h < NV; h++){ const y = 830 + (h >> 1) * 70, c0 = (h & 1) * 3; [intDial[h], lvlDial[h], panDial[h]].forEach((d, j) => d.r = R(10 + hw * (c0 + j) + (hw - 68) / 2, y, 68, sh)); }
    }
    updateVisibility();
  }

  // ---- graph interaction ----
  function noteAt(x, y){
    const p = plotRect(), h = Math.max(ui.narrow ? 12 : 7, p.h / Math.max(1, vSpanP) * 0.8), t = tOfX(p, x);
    for (let i = 0; i < notes.length; i++){ const n = notes[i]; if (t < n.t0 || t > n.t1) continue; if (Math.abs(yOf(p, n.dst) - y) <= h / 2 + 1) return i; }
    return -1;
  }
  const clampView = () => { vSpan = clamp(vSpan, 0.25, 600); vSpanP = clamp(vSpanP, 6, 72); vp0 = clamp(vp0, 0, 127 - vSpanP); };
  function zoomTime(f, x){ const p = plotRect(); x = x == null ? p.cx : x; const t = tOfX(p, x); vSpan = clamp(vSpan * f, 0.25, 600); vt0 = t - (x - p.x) / p.w * vSpan; }
  function zoomPitch(f, y){ const p = plotRect(); y = y == null ? p.cy : y; const m = mOfY(p, y); vSpanP = clamp(vSpanP * f, 6, 72); vp0 = m - (p.b - y) / p.h * vSpanP; clampView(); }
  function graphMenu(x, y){   // (Thunder: right-click / long-press)
    const hit = noteAt(x, y);
    if (hit >= 0 && !sel[hit]){ sel.fill(false); sel[hit] = true; }
    const any = anySel(), items = [], acts = [];
    const add = (s, fn) => { items.push(s); acts.push(fn || null); };
    add('#' + (any ? 'SELECTED NOTES' : 'EVERY NOTE'));
    add('Snap to the scale', snapNotes); add('Reset to how it was sung', resetNotes);
    add('Up a semitone', () => moveSel(1)); add('Down a semitone', () => moveSel(-1));
    add('-'); add('#SELECT'); add('Select all', () => sel.fill(true)); add('Select none', () => sel.fill(false));
    add('-'); add('#VIEW'); add('Fit the notes', fitView); add('Zoom in (time)', () => zoomTime(0.8, x)); add('Zoom out (time)', () => zoomTime(1.25, x));
    add('Zoom in (pitch)', () => zoomPitch(0.85, y)); add('Zoom out (pitch)', () => zoomPitch(1.18, y)); add('Go to the playhead', () => { vt0 = playhead - vSpan * 0.15; });
    add((followView ? 'Don’t follow' : 'Follow') + ' the playhead', () => setFollow(!followView));
    ui.openMenu(R(x, y, 1, 1), items, -1, i => { if (acts[i]) acts[i](); ui.dirty = true; });
  }
  function moveSel(d){ let any = false; for (let i = 0; i < notes.length; i++) if (sel[i]){ notes[i].dst = clamp(notes[i].dst + d, 0, 127); any = true; } if (any) notesChanged(true); }
  function down(x, y, btn, m, dbl){
    if (!graphMode()) return false;
    const p = plotRect(), kr = keysRect();
    if (btn === 0 && kr.has(x, y)){ dmode = 4; dragY = y; dragV0 = vp0; return true; }   // (Thunder) drag the keyboard: scroll the pitch
    if (!p.has(x, y)) return false;
    if (btn === 2){ graphMenu(x, y); return false; }
    if (btn !== 0) return false;
    const hit = noteAt(x, y);
    if (hit >= 0){
      if (dbl){ notes[hit].dst = nearestInScale(notes[hit].dst, key(), mask()); notesChanged(true); ui.status = noteName(lround(notes[hit].dst)) + ': snapped to the scale'; return false; }
      if (m.shift) sel[hit] = !sel[hit]; else if (!sel[hit]){ sel.fill(false); sel[hit] = true; }
      dmode = 1; dragY = y; dragStart = notes.map(n => n.dst); beginGroup(); return true;
    }
    if (rulerRect().has(x, y)){ dmode = 3; dragX0 = x; dragV0 = vt0; return true; }   // (Thunder) drag the ruler: scroll the time
    if (!m.shift) sel.fill(false);
    dmode = 2; mx0 = mx1 = x; my0 = my1 = y; return true;
  }
  function drag(x, y, m){
    const p = plotRect();
    if (dmode === 1){
      let d = (dragY - y) / (p.h / Math.max(1, vSpanP)); if (m.shift) d *= 0.2;
      for (let i = 0; i < notes.length; i++) if (sel[i]) notes[i].dst = clamp(m.alt ? dragStart[i] + d : Math.round(dragStart[i] + d), 0, 127);
      notesChanged(false);
      const i = sel.indexOf(true); if (i >= 0){ const nn = lround(notes[i].dst); ui.status = noteName(nn) + ' ' + signed(lround(100 * (notes[i].dst - nn)), 0) + ' c   (' + signed(notes[i].dst - notes[i].src, 2) + ' semitones from where it was sung)'; }
    } else if (dmode === 2){
      mx1 = clamp(x, p.x, p.r); my1 = clamp(y, p.y, p.b);
      const t0 = tOfX(p, Math.min(mx0, mx1)), t1 = tOfX(p, Math.max(mx0, mx1)), m0 = mOfY(p, Math.max(my0, my1)), m1 = mOfY(p, Math.min(my0, my1));
      for (let i = 0; i < notes.length; i++) if (notes[i].t1 > t0 && notes[i].t0 < t1 && notes[i].dst > m0 - 0.4 && notes[i].dst < m1 + 0.4) sel[i] = true;
    } else if (dmode === 3){ vt0 = dragV0 - (x - dragX0) / p.w * vSpan; ui.status = 'Scroll: ' + vt0.toFixed(2) + ' s'; }
    else if (dmode === 4){ vp0 = clamp(dragV0 + (y - dragY) / (p.h / Math.max(1, vSpanP)), 0, 127 - vSpanP); }
  }
  function up(){ if (dmode === 1){ endGroup(); notesChanged(true); } dmode = 0; }
  function hover(x, y){
    const p = plotRect();
    if (graphMode() && keysRect().has(x, y)){ ui.status = 'Drag the keyboard: scroll the pitch   Alt+wheel: zoom pitch'; return; }
    if (!p.has(x, y)) return;
    if (!graphMode()){ ui.status = 'The pitch sung (thin), what comes out (neon), the target notes and the harmonies (second colour)'; return; }
    const hit = noteAt(x, y);
    if (hit >= 0){ const n = notes[hit], nn = lround(n.dst);
      ui.status = noteName(nn) + ' ' + signed(lround(100 * (n.dst - nn)), 0) + ' c  (sung ' + signed(n.dst - n.src, 2) + ' st)   ' + (n.t1 - n.t0).toFixed(2) + ' s   drag: move (Alt free)   double-click: snap to the scale'; }
    else if (rulerRect().has(x, y)) ui.status = 'Drag the ruler: scroll   Ctrl/Cmd+wheel: zoom time   right-click: view menu';
    else ui.status = 'Drag: select notes   Wheel: scroll   Cmd/Ctrl+wheel: zoom time   Shift+wheel: scroll pitch   Alt+wheel: zoom pitch   right-click: menu';
  }
  function wheel(x, y, dy, m){
    if (!screenR.has(x, y)) return false; if (!graphMode()) return true;
    const p = plotRect();
    if (m.cmd) zoomTime(dy > 0 ? 0.8 : 1.25, x);
    else if (m.alt) zoomPitch(dy > 0 ? 0.85 : 1.18, y);
    else if (m.shift) vp0 += dy > 0 ? 1 : -1;
    else vt0 += (dy > 0 ? -0.1 : 0.1) * vSpan;
    vp0 = clamp(vp0, 0, 127 - vSpanP); void p; return true;
  }
  function keyH(e, m){
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (!graphMode()){ if (k === 'Tab'){ ui.setGesture('mode', 1); return true; } return false; }
    if (k === 'Tab'){ ui.setGesture('mode', 0); return true; }
    if (m.cmd && k === 'a'){ sel.fill(true); return true; }
    if (m.cmd) return false;
    if (k === 'ArrowUp' || k === 'ArrowDown'){ moveSel((k === 'ArrowUp' ? 1 : -1) * (m.alt ? 0.05 : m.shift ? 12 : 1)); return true; }
    if (k === 'Delete' || k === 'Backspace'){ if (anySel()) resetNotes(); return true; }
    if (k === 'Escape'){ if (!anySel()) return false; sel.fill(false); return true; }   // (none selected: Esc closes, as every window)
    if (k === 's'){ snapNotes(); return true; }
    if (k === 'c'){ setCapture(!capturing); return true; }
    if (k === 'f'){ setFollow(!followView); return true; }
    return false;
  }

  // ---- telemetry: t[5 + i] (wasm/fx/tune.cpp) ----
  function frame(fr){
    const t = fr.t; if (!t || t.length < B + 19) return;
    now.inM = t[B]; now.outM = t[B + 1]; now.tgt = t[B + 2]; playhead = t[B + 3]; playing = t[B + 4] >= 0.5;
    for (let h = 0; h < NV; h++) now.harm[h] = t[B + 6 + h];
    for (let i = 0; i < 8; i++) held[i] = t[B + 10 + i] & 0xFFFF;
    const n = Math.min(t[B + 18] | 0, ((t.length - B - 19) / 3) | 0);
    for (let i = 0; i < n; i++){ const o = B + 19 + 3 * i; capQ.push({t:t[o], midi:t[o + 1] > 0 ? t[o + 1] : -1}); }
    if (!capturing) capQ.length = 0;
    lastFr = U.now();
  }
  function tick(dt){
    const tn = U.now();
    if (clearArmed > 0){ clearArmed -= dt; if (clearArmed <= 0) ui.dirty = true; }
    if (capQ.length){ const b = capQ; capQ = []; drainCapture(b); }
    const stale = tn - lastFr > 0.35;
    if (stale && playing){ playing = false; ui.dirty = true; }
    if (!playing && passOpen){ passOpen = false; if (curveDirty) sendCurve(); }
    if (notesDirty && dmode !== 1 && group === 0 && tn - lastSend > (capturing ? 0.25 : 0.06)) sendNotes(false);
    if (curveDirty && tn - lastCurveSend > 1) sendCurve();
    if (capturing && tn - capSentAt > 2){ ui.hold(3, 1, 1); capSentAt = tn; }   // (the effect may have been rebuilt)
    histT += dt;
    while (histT >= 1 / 60){
      histT -= 1 / 60;
      const o = stale ? -1 : now.outM, tg = stale ? -1 : now.tgt;
      hIn[hPos] = stale ? -1 : now.inM; hOut[hPos] = o; hTgt[hPos] = tg;
      for (let h = 0; h < NV; h++){ const hn = stale ? -1 : now.harm[h]; hHarm[h][hPos] = hn > 0 && o > 0 ? o + (hn - tg) : -1; }
      hPos = (hPos + 1) % HIST;
    }
    if (graphMode()){
      if (fitWhenLoaded){ fitWhenLoaded = false; fitView(); ui.dirty = true; }
      if (followView && playing && !dmode && (playhead < vt0 || playhead > vt0 + vSpan * 0.85)){ vt0 = playhead - vSpan * 0.15; ui.dirty = true; }
    } else {
      let lo = 1e9, hi = -1e9;
      for (let i = 0; i < HIST; i++){ const a = hIn[i], b = hOut[i]; if (a > 0){ lo = Math.min(lo, a); hi = Math.max(hi, a); } if (b > 0){ lo = Math.min(lo, b); hi = Math.max(hi, b); } }
      if (lo > hi){ lo = 52; hi = 64; }
      const span = Math.max(15, hi - lo + 6), mid = 0.5 * (lo + hi), k = Math.min(1, dt * 3);
      liveLo += ((mid - span / 2) - liveLo) * k; liveHi += ((mid + span / 2) - liveHi) * k;
    }
  }
  const PAN_RE = /^\s*([LRC])\s*(\d*(?:[.,]\d+)?)\s*$/i;
  return {
    layout, drawBody, down, drag, up, hover, wheel, frame, tick, key:keyH,
    leave(){ },
    animating(){ return U.now() - lastFr < 2 || capturing || clearArmed > 0 || Math.abs(liveHi - liveLo - 16) > 0.5 && U.now() - lastFr < 4; },
    changed(sym){ if (sym === 'mode' || sym === 'harm_mode' || sym === 'midi') updateVisibility(); },
    reload(){ readState(); updateVisibility(); },
    fmtStatus:s => tfmt(s, V(s)),
    entryText(s){ if (/^h[1-4]$|_pan$|^transpose$|^flex$|^vib_depth$|^retune$|formant$/.test(s)){ const v = V(s); return String(+v.toFixed(Number.isInteger(v) ? 0 : 1)); } return null; },
    preParse(s, txt){
      if (/_pan$/.test(s)){ const m = PAN_RE.exec(txt); if (m) return m[1].toUpperCase() === 'C' ? '0' : (m[1].toUpperCase() === 'L' ? '-' : '') + (m[2] || '0'); }
      if (/^(off|instant)/i.test(txt.trim())) return '0';
      if (/^h[1-4]$/.test(s)){ const m = /^\s*([+-]?)\s*(2nd|3rd|4th|5th|6th|7th|8ve)\s*$/i.exec(txt); if (m) return (m[1] === '-' ? '-' : '') + DEG.indexOf(m[2].toLowerCase()); }
      return txt;
    },
    footer(){ return ((graphMode() ? 'GRAPH' : 'LIVE') + '   RANGE ' + RANGES[clamp(Math.round(V('range')), 0, 2)] + '   ' + ui.latencyText()).toUpperCase(); },
    close(){ if (capturing) setCapture(false); else { if (curveDirty) sendCurve(); if (notesDirty) sendNotes(true); } },
    help(){ return [['Tab', 'switch between Live and Graph'], ['C (graph)', 'capture on / off, then play the song'],
      ['Drag a note', 'move it (snaps to semitones; Alt = free, Shift = fine)'], ['Double-click a note', 'snap it to the key and scale'],
      ['Up / Down', 'selected notes a semitone (Shift = octave, Alt = 5 cents)'], ['Cmd/Ctrl + A, Esc', 'select every note, select none'],
      ['S, Delete', 'snap the selection to the scale, reset it to how it was sung'], ['Wheel', 'scroll time; Cmd/Ctrl = zoom time; Shift = scroll pitch; Alt = zoom pitch'],
      ['Drag the ruler / keyboard', 'scroll time / pitch (Thunder: for touch)'], ['Right-click / long-press the graph', 'snap, reset, select, zoom, fit'],
      ['F', 'the view follows the playhead'], ['Note sliders', 'drag; double-click or Ctrl/Cmd-click: reset; each edit is one undo step']]; },
  };
}});
})();
