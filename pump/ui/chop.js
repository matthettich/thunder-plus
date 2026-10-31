/* …pump CHOP pop-up (31 Oct 2026): plugins/chop/UIChop.cpp in the browser. REGION: the captured bar's waveform cut into
   its slices, the slices the held pads play lit, the playhead, the bar coming in now (pink outline, green write head)
   and whether it follows the song or is held. NOW: the pad playing (or LIVE). PADS: hold one (mouse, touch, its key or
   its MIDI note) to play it; each shows its slice, its note and key, and a LEARN box (then a note or a key). Below: the
   selected pad (slice, pitch, reverse, effect and amount, level) and the chopper (slices, length, quantise, hold,
   two-pad loops, tempo, mix, output). Telemetry: wasm/fx/chop.cpp in pump (t[5 + i], positions relative to the samples
   written so far). Pads from the window: PumpFX.hold kind 4 on pad<n>_trigger (momentary, never saved).
   Kit gaps handled here: the pads take their pointers themselves (a capture listener on the window), so several
   fingers can hold several pads (two-pad loops on a phone) and a held finger is never turned into a long-press. */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, hex, Dial} = U;
const PADS = 8, WAVE = 512, BASE = 5;
const FX_KEYS = ['-', 'LP', 'HP', 'CRUSH', 'RATCHET', 'TAPE', 'GATE', 'DRIVE'];
const FX_TIP = 'The pad’s effect: low-pass, high-pass, crush, ratchet (back to the slice’s start), tape stop, gate, drive';
const LEN_NAMES = ['1/2 bar', '1 bar', '2 bars', '4 bars'], Q_NAMES = ['Off', '1/32', '1/16', '1/8', '1/4'];
// Thunder's note names (C-4 = MIDI 60, so the plugin's first pad, MIDI 48, reads C-3)
const NN = ['C-', 'C#', 'D-', 'D#', 'E-', 'F-', 'F#', 'G-', 'G#', 'A-', 'A#', 'B-'];
const noteName = n => { n = clamp(Math.round(n), 0, 127); return NN[n % 12] + (Math.floor(n / 12) - 1); };
const keyName = k => (k <= 32 || k > 126) ? '-' : String.fromCharCode(k).toUpperCase();
const ps = (p, f) => 'pad' + (p + 1) + '_' + f;
const selKept = new Map();   // the selected pad per slot, for this session (the plugin's own UI state)

U.define('chop', {W:1000, H:628, nW:480, nH:978, bypass:true, back:0, make(ui){
  let regionR = R(), nowR = R(), meterL = R(), meterR = R(), editR = R(), kitR = R();
  const padR = Array.from({length:PADS}, () => R()), learnR = Array.from({length:PADS}, () => R());
  let sel = selKept.get(ui.t + '/' + ui.k) | 0, learn = -1, noteSeen = null, ninOff = false, ninT = 0;
  // telemetry
  const wave = new Float32Array(WAVE), live = new Float32Array(WAVE).fill(-1), glow = new Float32Array(PADS);
  let rs = 0, rl = 1, ph = NaN, ls = 0, ll = 1, padNow = -1, heldNow = 0, pendNow = 0;
  const pk = [0, 0], lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0];
  // pads held from this window: pad -> set of sources ('p' + pointerId, 'k' + key code)
  const holders = Array.from({length:PADS}, () => new Set()), ptrPad = new Map(), keyPads = new Map();
  let heldBits = 0;

  const V = s => ui.value(s);
  const trigIdx = p => ui.I[ps(p, 'trigger')];
  function setHeldBit(p, on){
    const was = !!(heldBits >> p & 1); if (was === on) return;
    heldBits = on ? (heldBits | 1 << p) : (heldBits & ~(1 << p));
    const i = trigIdx(p); if (i != null) ui.hold(4, i, on ? 1 : 0);
    if (on) glow[p] = 1;
    ui.dirty = true;
  }
  const press = (p, src) => { holders[p].add(src); setHeldBit(p, true); };
  const release = (p, src) => { holders[p].delete(src); if (!holders[p].size) setHeldBit(p, false); };
  function releaseAll(){ for (let p = 0; p < PADS; p++){ holders[p].clear(); setHeldBit(p, false); } ptrPad.clear(); keyPads.clear(); }
  function releaseKeys(){ for (const [code, list] of keyPads) for (const p of list) release(p, 'k' + code); keyPads.clear(); }

  // ---- the selected pad's controls (one set, rebound to the selected pad: the plugin's eight sets, one visible)
  const sliceD = ui.dial(null, 'SLICE', 'm'); sliceD.tip = 'Which slice of the bar this pad plays'; sliceD.fmt = v => String(Math.round(v)); sliceD.tabGroup = 1;
  const pitchD = ui.dial(null, 'PITCH', 'm', true); pitchD.fmt = v => Math.abs(v) < 0.5 ? '0 ST' : (v > 0 ? '+' : '') + Math.round(v) + ' ST'; pitchD.tabGroup = 1;
  const revB = ui.toggle(null, 'REVERSE');
  const fxSeg = ui.seg(null, FX_KEYS); fxSeg.tip = FX_TIP;
  const amtD = ui.dial(null, 'AMOUNT', 'm'); amtD.tabGroup = 1;
  const levelD = ui.dial(null, 'LEVEL', 'm', true); levelD.tabGroup = 1;
  // ---- the chopper
  const slicesSeg = ui.seg('slices', ['8', '16']); slicesSeg.tip = 'How many slices the bar is cut into';
  const lenMenu = ui.menuBtn('length'); lenMenu.arrows = true; lenMenu.tip = 'How much of the past the pads chop';
  const quantMenu = ui.menuBtn('quantise'); quantMenu.arrows = true; quantMenu.tip = 'A press waits for the next grid line (a release never waits)';
  const holdB = ui.toggle('hold', 'HOLD'); holdB.tip = 'Keep the captured bar after the pads are up (chop it again and again)';
  const notesB = ui.toggle('midi_notes', 'PADS ON MIDI NOTES'); notesB.tip = 'Off: the pads ignore MIDI notes (the window’s pads and keys still play them)';
  const loopB = ui.toggle('loop_two', 'TWO-PAD LOOP'); loopB.tip = 'Holding two or more pads loops from the lowest slice to the end of the highest';
  const bpmD = ui.dial('bpm', 'TEMPO', 'm'); bpmD.tip = 'The tempo when the host sends none (in Thunder the song’s tempo leads)'; bpmD.fmt = v => Math.round(v) + ' BPM'; bpmD.tabGroup = 2;
  const mixD = ui.dial('mix', 'MIX', 'm'); mixD.tabGroup = 2;
  const outD = ui.dial('out_gain', 'OUTPUT', 'm', true); outD.tabGroup = 2;

  function select(p){
    sel = clamp(p, 0, PADS - 1); selKept.set(ui.t + '/' + ui.k, sel);
    sliceD.sym = ps(sel, 'slice'); pitchD.sym = ps(sel, 'pitch'); revB.sym = ps(sel, 'reverse'); fxSeg.sym = ps(sel, 'fx');
    amtD.sym = ps(sel, 'fx_amount'); levelD.sym = ps(sel, 'level'); ui.dirty = true;
  }
  select(sel);
  const readNin = () => { const i = PumpFX.get(ui.t, ui.k); ninOff = !!i && !(i.nin | 0); };
  readNin();

  // ---- layout
  function layout(narrow){
    const hm = Dial.heightFor('m');
    if (!narrow){
      const top = 58, sh = 176;
      regionR = R(14, top, 708, sh); nowR = R(734, top, 226, sh);
      meterL = R(966, top, 8, sh - 12); meterR = R(977, top, 8, sh - 12);
      const y1 = top + sh + 12, padH = 92, pw = (972 - 7 * 10) / 8;
      for (let p = 0; p < PADS; p++){ padR[p] = R(14 + p * (pw + 10), y1, pw, padH); learnR[p] = R(padR[p].r - 50, padR[p].y + 8, 44, 16); }
      const y2 = y1 + padH + 12, ph2 = 116; editR = R(14, y2, 972, ph2);
      sliceD.r = R(22, y2 + 28, 80, hm); pitchD.r = R(102, y2 + 28, 80, hm); revB.r = R(194, y2 + 54, 94, 24);
      fxSeg.r = R(302, y2 + 54, 486, 24); amtD.r = R(794, y2 + 28, 90, hm); levelD.r = R(884, y2 + 28, 90, hm);
      const y3 = y2 + ph2 + 12, cy = y3 + 54; kitR = R(14, y3, 972, ph2);
      slicesSeg.r = R(26, cy, 90, 24); lenMenu.r = R(128, cy, 118, 24); quantMenu.r = R(258, cy, 108, 24);
      holdB.r = R(378, cy, 70, 24); loopB.r = R(458, cy, 124, 24); notesB.r = R(592, cy, 168, 24);
      bpmD.r = R(766, y3 + 28, 70, hm); mixD.r = R(836, y3 + 28, 70, hm); outD.r = R(906, y3 + 28, 70, hm);
    } else {
      const W = ui.W, x0 = 10, fw = W - 20;
      regionR = R(x0, 94, fw, 150);
      nowR = R(x0, 256, fw - 34, 80); meterL = R(W - 28, 256, 8, 80); meterR = R(W - 18, 256, 8, 80);
      const y1 = 348, padH = 84, pw = (fw - 3 * 8) / 4;
      for (let p = 0; p < PADS; p++){ const c = p % 4, rw = p >> 2; padR[p] = R(x0 + c * (pw + 8), y1 + rw * (padH + 8), pw, padH); learnR[p] = R(padR[p].r - 50, padR[p].y + 8, 44, 16); }
      const y2 = y1 + 2 * padH + 8 + 12; editR = R(x0, y2, fw, 178);
      revB.r = R(W - 10 - 104, y2 + 8, 96, 24);
      const cw = fw / 4, place = (d, i) => d.r = R(x0 + cw * i + (cw - 80) / 2, y2 + 40, 80, hm);
      place(sliceD, 0); place(pitchD, 1); place(amtD, 2); place(levelD, 3);
      fxSeg.r = R(x0 + 8, y2 + 142, fw - 16, 26);
      const y3 = y2 + 178 + 12; kitR = R(x0, y3, fw, 208);
      const ca = y3 + 48; slicesSeg.r = R(x0 + 8, ca, 80, 26); lenMenu.r = R(x0 + 100, ca, 128, 26); quantMenu.r = R(x0 + 240, ca, 120, 26); holdB.r = R(x0 + 372, ca, fw - 380, 26);
      const cb = y3 + 88; loopB.r = R(x0 + 8, cb, 150, 26); notesB.r = R(x0 + 170, cb, fw - 178, 26);
      const cd = fw / 3, placeK = (d, i) => d.r = R(x0 + cd * i + (cd - 80) / 2, y3 + 122, 80, hm);
      placeK(bpmD, 0); placeK(mixD, 1); placeK(outD, 2);
    }
  }

  // ---- drawing
  function drawRegion(){
    const t = ui.th, g = ui.g, r = regionR; ui.screenBox(r);
    ui.caption(r.x + 10, r.y + 14, 'REGION');
    const held = heldNow || pendNow || V('hold') >= 0.5;
    ui.text(r.r - 10, r.y + 14, (LEN_NAMES[clamp(Math.round(V('length')), 0, 3)] + (held ? '  ·  HELD' : '  ·  FOLLOWING THE SONG')).toUpperCase(), 'label', 9.5, t.ink2, 'right', 0.6);
    const p = R(r.x + 10, r.y + 26, r.w - 20, r.h - 36), slices = V('slices') >= 0.5 ? 16 : 8;
    for (let q = 0; q < PADS; q++) if ((heldNow | pendNow) >> q & 1){
      const s = clamp(Math.round(V(ps(q, 'slice'))), 1, slices) - 1;
      ui.fill(R(p.x + p.w * s / slices, p.y, p.w / slices, p.h), alpha(t.neon, (heldNow >> q & 1) ? 0.35 : 0.15)); }
    const cy = p.cy; let mx = 0.05; for (let i = 0; i < WAVE; i++) mx = Math.max(mx, wave[i], live[i]);
    g.beginPath(); const bw = Math.max(1, p.w / WAVE);
    for (let i = 0; i < WAVE; i++){ const h = Math.min(1, wave[i] / mx) * p.h * 0.46; if (h > 0.05) g.rect(p.x + p.w * i / WAVE, cy - h, bw, 2 * h); }
    g.fillStyle = css(alpha(t.ink, 0.7)); g.fill();
    for (let s = 0; s <= slices; s++){ const x = p.x + p.w * s / slices;
      ui.line(x, p.y, x, p.b, s % 4 === 0 ? t.dim : alpha(t.dim, 0.5), 1);
      if (s < slices) ui.text(x + 4, p.y + 8, String(s + 1), 'mono', 8, t.ink2, 'left'); }
    // the bar coming in now, over the captured one: its outline in pink, the write head in green
    const pink = mix(hex(0xFF4FA0), t.ink, t.dark ? 0 : 0.15), green = t.dark ? t.neon : mix(t.neon, t.ink, 0.35);
    let last = -1; for (let i = 0; i < WAVE; i++) if (live[i] >= 0) last = i;
    if (last > 0) for (let side = 0; side < 2; side++){
      g.beginPath();
      for (let i = 0; i <= last; i++){ const h = Math.min(1, Math.max(0, live[i]) / mx) * p.h * 0.46, x = p.x + p.w * (i + 0.5) / WAVE, y = side ? cy + h : cy - h; i ? g.lineTo(x, y) : g.moveTo(x, y); }
      g.strokeStyle = css(alpha(pink, 0.9)); g.lineWidth = 1.4; g.lineJoin = 'round'; g.stroke(); }
    if (ll > 0){ const x = p.x + p.w * clamp(-ls / ll, 0, 1);
      ui.line(x, p.y, x, p.b, alpha(green, 0.95), 1.5); g.beginPath(); g.arc(x, p.y + 3, 3, 0, 7); g.fillStyle = css(green); g.fill(); }
    ui.text(p.x + 4, p.b - 8, 'IN NOW', 'label', 8, pink, 'left', 0.8);
    if (Number.isFinite(ph) && rl > 0){ const x = p.x + p.w * clamp((ph - rs) / rl, 0, 1); ui.line(x, p.y, x, p.b, mix(t.neon, t.ink, t.dark ? 0 : 0.5), 2.5); }
  }
  function drawNow(){
    const t = ui.th, r = nowR; ui.screenBox(r); ui.caption(r.x + 10, r.y + 14, 'NOW');
    const nar = ui.narrow, b = nar ? R(r.x + 60, r.y + 8, 150, r.h - 16) : R(r.x + 12, r.y + 30, r.w - 24, 70);
    if (padNow >= 0){
      ui.fill(b, t.neon);
      ui.text(b.cx, b.cy - (nar ? 7 : 8), 'PAD ' + (padNow + 1), 'label', nar ? 14 : 16, t.neonInk, 'center', 1.5);
      ui.text(b.cx, b.cy + (nar ? 12 : 14), 'SLICE ' + Math.round(V(ps(padNow, 'slice'))), 'label', 10, t.neonInk, 'center', 0.8);
    } else { ui.stroke(b, t.dim, 1.5); ui.text(b.cx, b.cy, pendNow ? (nar ? 'WAITING' : 'WAITING FOR THE GRID') : 'LIVE', 'label', 13, t.ink2, 'center', 1.2); }
    let n = 0; for (let q = 0; q < PADS; q++) n += heldNow >> q & 1;
    const l1 = n >= 2 && V('loop_two') >= 0.5 ? n + ' PADS: LOOPING' : n + ' PAD' + (n === 1 ? '' : 'S') + ' HELD';
    const l2 = 'QUANTISE ' + Q_NAMES[clamp(Math.round(V('quantise')), 0, 4)].toUpperCase();
    const l3 = learn >= 0 ? 'LEARNING PAD ' + (learn + 1) + ': A NOTE OR A KEY' : (ninOff && V('midi_notes') >= 0.5 ? 'NO NOTES IN: SET NOTES FROM' : '');
    const l3c = learn >= 0 ? t.ink : t.red;
    if (!nar){ ui.text(r.x + 12, r.y + 122, l1, 'label', 9.5, t.ink2, 'left', 0.6); ui.text(r.x + 12, r.y + 142, l2, 'label', 9.5, t.ink2, 'left', 0.6);
      if (l3) ui.text(r.x + 12, r.y + 164, l3, 'label', 9, l3c, 'left', 0.4); }
    else { const x = b.r + 12; ui.text(x, r.y + 18, l1, 'label', 9.5, t.ink2, 'left', 0.6); ui.text(x, r.y + 38, l2, 'label', 9.5, t.ink2, 'left', 0.6);
      if (l3) ui.clip(R(x, r.y + 48, r.r - x - 4, 24), () => ui.text(x, r.y + 60, learn >= 0 ? 'LEARNING PAD ' + (learn + 1) : 'NOTES FROM: OFF', 'label', 9, l3c, 'left', 0.4)); }
  }
  const drawMeter = () => { ui.drawLevelMeter(meterL, lvl[0], hold[0], -48, 6); ui.drawLevelMeter(meterR, lvl[1], hold[1], -48, 6); };   // drawOutMeter
  function drawPad(p){
    const t = ui.th, r = padR[p], gl = clamp(glow[p], 0, 1), waiting = !!(pendNow >> p & 1);
    ui.fill(r, mix(t.panel2, t.neon, 0.12 + 0.88 * Math.sqrt(gl)));
    ui.stroke(r, p === sel ? t.ink : t.dim, p === sel || waiting ? 2 : 1);
    const ink = gl > 0.45 ? t.neonInk : t.ink, ink2 = gl > 0.45 ? t.neonInk : t.ink2;
    ui.text(r.x + 8, r.y + 16, String(p + 1), 'label', 14, ink, 'left', 0);
    ui.text(r.x + 8, r.y + (ui.narrow ? 38 : 42), 'SLICE ' + Math.round(V(ps(p, 'slice'))), 'label', 9.5, ink, 'left', 0.6);
    const fx = FX_KEYS[clamp(Math.round(V(ps(p, 'fx'))), 0, 7)], pit = Math.round(V(ps(p, 'pitch')));
    const s = (V(ps(p, 'reverse')) >= 0.5 ? 'REV ' : '') + (pit ? pit + 'ST ' : '') + (fx === '-' ? '' : fx);
    ui.clip(R(r.x + 4, r.y + 40, r.w - 8, 30), () => ui.text(r.x + 8, r.y + (ui.narrow ? 53 : 58), s, 'label', 8.5, ink2, 'left', 0.4));
    ui.text(r.x + 8, r.b - 12, (V('midi_notes') >= 0.5 ? noteName(V(ps(p, 'note'))) : 'OFF') + ' · ' + keyName(Math.round(V(ps(p, 'key')))), 'mono', 9, ink2, 'left');
    const lr = learnR[p];
    if (learn === p) ui.fill(lr, t.ink);
    ui.stroke(lr, ink2, 1);
    ui.text(lr.cx, lr.cy, 'LEARN', 'label', 7.5, learn === p ? t.bg : ink2, 'center', 0.4);
  }
  function drawBody(){
    const t = ui.th;
    drawRegion(); drawNow(); drawMeter();
    for (let p = 0; p < PADS; p++) drawPad(p);
    ui.stroke(editR, t.dim, 1); ui.caption(editR.x + 12, editR.y + 16, 'PAD ' + (sel + 1));
    ui.caption(fxSeg.r.x, fxSeg.r.y - 10, 'EFFECT');
    ui.stroke(kitR, t.dim, 1); ui.caption(kitR.x + 12, kitR.y + 16, 'CHOPPER');
    ui.caption(slicesSeg.r.x, slicesSeg.r.y - 10, 'SLICES'); ui.caption(lenMenu.r.x, lenMenu.r.y - 10, 'LENGTH'); ui.caption(quantMenu.r.x, quantMenu.r.y - 10, 'QUANTISE');
  }

  // ---- the pads' pointers: taken before the kit sees them (several at once; no long-press on a held pad)
  const padAt = (x, y) => { for (let p = 0; p < PADS; p++) if (padR[p].has(x, y)) return p; return -1; };
  const learnAt = (x, y) => { for (let p = 0; p < PADS; p++) if (learnR[p].has(x, y)) return p; return -1; };
  function toggleLearn(p){
    learn = learn === p ? -1 : p; select(p);
    ui.status = learn >= 0 ? 'Pad ' + (p + 1) + ': press a MIDI note or a key' + (ninOff ? ' (MIDI: set the slot’s NOTES FROM on the FX screen)' : '') : '';
    ui.dirty = true;
  }
  const onDown = e => {
    if (ui.closed || ui.menu || ui.help || ui.edit || e.button !== 0) return;
    const [x, y] = ui.pos(e); if (y <= ui.headH || y >= ui.H - U.FOOT) return;
    const lp = learnAt(x, y), p = lp >= 0 ? -1 : padAt(x, y); if (lp < 0 && p < 0) return;
    e.stopPropagation(); e.preventDefault();
    if (e.pointerType === 'mouse') try { ui.el.focus({preventScroll:true}); } catch(_){}
    if (lp >= 0){ toggleLearn(lp); return; }
    try { ui.cv.setPointerCapture(e.pointerId); } catch(_){}
    select(p); ptrPad.set(e.pointerId, p); press(p, 'p' + e.pointerId);
    ui.status = 'Pad ' + (p + 1) + ': slice ' + Math.round(V(ps(p, 'slice')));
  };
  const onUp = e => {
    if (!ptrPad.has(e.pointerId)) return; e.stopPropagation();
    const p = ptrPad.get(e.pointerId); ptrPad.delete(e.pointerId); release(p, 'p' + e.pointerId);
    try { ui.cv.releasePointerCapture(e.pointerId); } catch(_){}
  };
  const onMove = e => { if (ptrPad.has(e.pointerId)) e.stopPropagation(); };
  ui.el.addEventListener('pointerdown', onDown, true);
  ui.el.addEventListener('pointerup', onUp, true); ui.el.addEventListener('pointercancel', onUp, true);
  ui.el.addEventListener('lostpointercapture', onUp, true); ui.el.addEventListener('pointermove', onMove, true);

  // ---- keys: each pad's key (the plugin's keyPress / keyRelease), LEARN takes the next one
  const codeOf = e => e.code || e.key;
  function key(e, m){
    if (m.cmd || m.alt || e.ctrlKey || e.metaKey) return false;
    if (e.key === 'Escape' && learn >= 0){ learn = -1; ui.status = 'Learn cancelled'; return true; }
    if (e.key.length !== 1) return false;
    const k = e.key.toLowerCase().charCodeAt(0); if (k <= 32 || k > 126) return false;
    if (e.repeat) return keyPads.has(codeOf(e));
    if (learn >= 0){ ui.setGesture(ps(learn, 'key'), k); ui.status = 'Pad ' + (learn + 1) + ': key ' + keyName(k); learn = -1; return true; }
    const list = []; for (let p = 0; p < PADS; p++) if (Math.round(V(ps(p, 'key'))) === k){ list.push(p); press(p, 'k' + codeOf(e)); }
    if (!list.length) return false;
    keyPads.set(codeOf(e), list); select(list[0]); return true;
  }
  function keyUp(e){ const c = codeOf(e), list = keyPads.get(c); if (!list) return; keyPads.delete(c); for (const p of list) release(p, 'k' + c); ui.dirty = true; }

  const help = () => {
    const h = [['Hold a pad', 'its slice plays (mouse, touch, its key, its MIDI note)'], ['Two pads', 'loop between their slices (two fingers on a phone)'],
      ['LEARN', 'then a MIDI note or a computer key: that pad’s switch (Esc cancels)'], ['HOLD', 'keep the captured bar after letting go'],
      ['MIDI notes', 'come from the slot’s NOTES FROM (FX screen): a track’s notes' + (ninOff ? '. It is OFF now' : '')]];
    return h;
  };
  return {
    layout, drawBody, key, keyUp, help,
    hover(x, y){
      const lp = learnAt(x, y); if (lp >= 0){ ui.status = 'Learn: then press a MIDI note or a computer key for pad ' + (lp + 1); return; }
      const p = padAt(x, y); if (p >= 0){ ui.status = 'Hold to play pad ' + (p + 1) + ' (its key ' + keyName(Math.round(V(ps(p, 'key')))) + ', its MIDI note ' + noteName(V(ps(p, 'note'))) + '); two held loop'; return; }
      if (regionR.has(x, y)) ui.status = 'The captured bar, cut into slices: it follows the song until a pad goes down';
      else if (nowR.has(x, y) && ninOff) ui.status = 'MIDI notes reach CHOP from the slot’s NOTES FROM (FX screen); it is OFF';
    },
    entryText(s){ const v = V(s); if (/_slice$|_pitch$/.test(s)) return String(Math.round(v)); if (s === 'bpm') return String(Math.round(v)); return null; },
    fmtStatus(s){ const v = V(s); if (/_pitch$/.test(s)) return pitchD.fmt(v); if (/_slice$/.test(s)) return String(Math.round(v)); if (/_note$/.test(s)) return noteName(v); if (/_key$/.test(s)) return keyName(Math.round(v)); if (s === 'bpm') return bpmD.fmt(v); return ui.fmt(s); },
    frame(fr){
      const t = fr.t; if (!t || t.length < BASE + 1035) return;
      pk[0] = Math.max(pk[0], t[1]); pk[1] = Math.max(pk[1], t[2]);
      rs = t[BASE]; rl = t[BASE + 1]; ph = t[BASE + 2]; padNow = t[BASE + 4] | 0; heldNow = t[BASE + 5] >>> 0; pendNow = t[BASE + 6] >>> 0;
      ls = t[BASE + 7]; ll = t[BASE + 8];
      const note = t[BASE + 9] | 0, cnt = t[BASE + 10];
      if (noteSeen === null) noteSeen = cnt;
      else if (cnt !== noteSeen){ noteSeen = cnt;
        if (learn >= 0){ ui.setGesture(ps(learn, 'note'), note); ui.status = 'Pad ' + (learn + 1) + ': MIDI ' + noteName(note); learn = -1; } }
      wave.set(t.subarray(BASE + 11, BASE + 11 + WAVE)); live.set(t.subarray(BASE + 11 + WAVE, BASE + 11 + 2 * WAVE));
    },
    tick(dt){
      for (let c = 0; c < 2; c++){ const d = U.dbOf(pk[c]); pk[c] = 0;
        lvl[c] = Math.max(d, lvl[c] - 26 * dt);
        if (d >= hold[c]){ hold[c] = d; holdT[c] = 1.5; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(d, hold[c] - 18 * dt); }
      for (let p = 0; p < PADS; p++) glow[p] = ((heldNow | heldBits) >> p & 1) ? 1 : Math.max(0, glow[p] - dt * 5);
      if ((ninT += dt) > 1){ ninT = 0; const was = ninOff; readNin(); if (was !== ninOff) ui.dirty = true; }
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100 || glow.some(g => g > 0) || heldBits !== 0; },
    reload(){ readNin(); select(sel); },
    footer(){ return (padNow >= 0 ? 'PAD ' + (padNow + 1) : 'LIVE') + '   ' + (V('slices') >= 0.5 ? 16 : 8) + ' SLICES'; },
    blur(){ releaseKeys(); },
    leave(){},
    close(){ releaseAll(); learn = -1;
      ui.el.removeEventListener('pointerdown', onDown, true); ui.el.removeEventListener('pointerup', onUp, true); ui.el.removeEventListener('pointercancel', onUp, true);
      ui.el.removeEventListener('lostpointercapture', onUp, true); ui.el.removeEventListener('pointermove', onMove, true); },
  };
}});
})();
