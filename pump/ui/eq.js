/* …pump EQ pop-up (stage 2): plugins/eq/UIEQ.cpp in the browser. The curve display with the pre/post analyzers, the
   band nodes (drag, double-click to add, Alt-hold to solo, Alt-drag empty space to listen, box select, wheel for Q),
   the selected band's panel and the globals, through PumpUI (kit.js) and window.PumpFX. */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, Dial, Feed, Spectrum} = U;
const NB = 24, LO = 15, HI = 22000, NP = 220;
const SHAPES = ['Bell', 'Low Shelf', 'High Shelf', 'Low Cut', 'High Cut', 'Notch', 'Band Pass', 'Tilt Shelf'];
const SLOPES = ['6 dB/oct', '12 dB/oct', '18 dB/oct', '24 dB/oct', '30 dB/oct', '36 dB/oct', '48 dB/oct', '72 dB/oct', '96 dB/oct'];
const SLOPE_V = [6, 12, 18, 24, 30, 36, 48, 72, 96];
const PLACES = ['Stereo', 'Left', 'Right', 'Mid', 'Side'];
const hasGain = s => s === 0 || s === 1 || s === 2 || s === 7;
const hasSlope = s => s !== 0;
const isShelf = s => s === 1 || s === 2 || s === 7;
const RANGE_KEY = 'thunderplus-pump-eq-range-v1';
let clip = [];   // copied bands (shared by every EQ window, as the plugin shares them between instances)

// ---- the analog prototype of a band (common/dsp/Biquad.hpp analogPrototype): the curve the plugin draws
function bwQs(order){ const n2 = order >> 1, first = (order & 1) !== 0, qs = [];
  for (let k = 0; k < n2; k++){ const a = first ? Math.PI * (k + 1) / order : Math.PI * (2 * k + 1) / (2 * order); qs.push(1 / (2 * Math.cos(a))); }
  qs.sort((a, b) => a - b); return {qs, first}; }
function proto(shape, freq, gainDb, q, slope, sr){
  const out = {sec:[], gain:1}, w0 = 2 * Math.PI * clamp(freq, 1, 0.75 * sr) / sr; q = clamp(q, 0.025, 40); const G = Math.pow(10, gainDb / 20);
  const S = (n0, n1, n2, d0, d1, d2) => out.sec.push([n0, n1, n2, d0, d1, d2]);
  if (shape === 0){ const A = Math.sqrt(G); S(w0 * w0, A * w0 / q, 1, w0 * w0, w0 / (A * q), 1); }
  else if (isShelf(shape)){
    const high = shape !== 1, order = slope <= 6 ? 1 : slope <= 12 ? 2 : 4, gk = Math.pow(G, 1 / (2 * order)), rz = high ? w0 / gk : w0 * gk, rp = high ? w0 * gk : w0 / gk;
    const {qs, first} = bwQs(order); if (qs.length) qs[qs.length - 1] *= q / Math.SQRT1_2;
    if (first) S(rz, 1, 0, rp, 1, 0);
    for (const qq of qs) S(rz * rz, rz / qq, 1, rp * rp, rp / qq, 1);
    out.gain = shape === 7 ? Math.sqrt(G) : high ? G : 1;
  } else if (shape === 3 || shape === 4){
    const hp = shape === 3, order = clamp(Math.floor(slope / 6), 1, 16), {qs, first} = bwQs(order); if (qs.length) qs[qs.length - 1] *= q / Math.SQRT1_2;
    if (first) hp ? S(0, 1, 0, w0, 1, 0) : S(w0, 0, 0, w0, 1, 0);
    for (const qq of qs) hp ? S(0, 0, 1, w0 * w0, w0 / qq, 1) : S(w0 * w0, 0, 0, w0 * w0, w0 / qq, 1);
  } else if (shape === 5){ for (let s = 0; s < (slope >= 24 ? 2 : 1); s++) S(w0 * w0, 0, 1, w0 * w0, w0 / q, 1); }
  else if (shape === 6){ for (let s = 0; s < clamp(Math.floor(slope / 12), 1, 4); s++) S(0, w0 / q, 0, w0 * w0, w0 / q, 1); }
  return out;
}
function magSq(a, w){ let v = a.gain * a.gain; const w2 = w * w;
  for (const s of a.sec){ const nr = s[0] - s[2] * w2, ni = s[1] * w, dr = s[3] - s[5] * w2, di = s[4] * w; v *= (nr * nr + ni * ni) / Math.max(dr * dr + di * di, 1e-300); }
  return v; }

U.define('eq', {W:1040, H:660, nW:480, nH:824, bypass:true, back:8192, cmdKeys:'acvd', make(ui){
  const b_ = (b, f) => 'b' + (b + 1) + '_' + f;
  let sel = -1, hover = -1, dragBand = -1, dmode = 0, dragSet = [], axis = 0, soloBand = 0, soloHeld = false, soloLatch = -1, lastSel = -1;
  const selMask = new Array(NB).fill(false), orig = [];
  let pressX = 0, pressY = 0, curX = 0, curY = 0, lastX = 0, lastY = 0, effX = 0, effY = 0, audF = 1000, audQ = 2;
  let dbRange = (() => { try { const v = +localStorage.getItem(RANGE_KEY); return [3, 6, 12, 24, 30].includes(v) ? v : 12; } catch(e){ return 12; } })();
  const dyn = new Float32Array(NB), lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0], pk = [0, 0];
  const pre = new Spectrum(LO, HI), post = new Spectrum(LO, HI), fPre = new Feed(32768), fPost = new Feed(32768);
  let display = R(), bandPanel = R(), globalPanel = R(), meterL = R(), meterR = R(), lastFrameT = 0;
  const curveX = new Float32Array(NP), curveDb = new Float64Array(NP); const cache = [];
  const DM = {none:0, nodes:1, rect:2, aud:3, solo:4, pend:5};

  // ---- controls (the plugin's constructor)
  const bandSel = ui.menuBtn(null); bandSel.arrows = true;
  bandSel.itemsFn = () => { const v = []; for (let b = 0; b < NB; b++) if (used(b)) v.push('Band ' + (b + 1) + '  ' + summary(b)); return v; };
  bandSel.getFn = () => { let k = 0; for (let b = 0; b < NB; b++){ if (!used(b)) continue; if (b === sel) return k; k++; } return -1; };
  bandSel.setFn = i => { let k = 0; for (let b = 0; b < NB; b++){ if (!used(b)) continue; if (k === i){ select(b); return; } k++; } };
  bandSel.textFn = () => sel >= 0 ? 'BAND ' + (sel + 1) : 'NO BAND'; bandSel.tip = 'Selected band (side arrows step through bands)';
  const shapeSeg = ui.seg(null, ['BELL', 'LO SH', 'HI SH', 'LO CUT', 'HI CUT', 'NOTCH', 'B PASS', 'TILT']);
  const freqDial = ui.dial(null, 'FREQ'), gainDial = ui.dial(null, 'GAIN', 'm', true), qDial = ui.dial(null, 'Q');
  freqDial.tabGroup = gainDial.tabGroup = qDial.tabGroup = 1;
  const slopeMenu = ui.menuBtn(null, 'SLOPE');
  const placeSeg = ui.seg(null, ['ST', 'L', 'R', 'M', 'S']); placeSeg.tip = 'Stereo placement: both channels, left, right, mid or side';
  const dynBtn = ui.toggle(null, 'DYNAMIC'); dynBtn.small = true;
  const rangeDial = ui.dial(null, 'RANGE', 's', true), threshDial = ui.dial(null, 'THRESH', 's'); rangeDial.tabGroup = threshDial.tabGroup = 2;
  const bandBypassBtn = ui.toggle(null, 'BYPASS'); bandBypassBtn.small = true;
  const soloBtn = ui.button('SOLO', () => { if (sel >= 0){ soloLatch = soloLatch === sel ? -1 : sel; pushSolo(); } }, () => sel >= 0 && soloLatch === sel);
  soloBtn.small = true; soloBtn.holdMomentary = true; soloBtn.tip = 'Hear only this band’s region (click = latch, click-and-hold = while held; Alt-hold a node does the same). Never saved';
  const delBtn = ui.button('DELETE', () => { if (sel >= 0) deleteBand(sel); }); delBtn.small = true;
  const phaseSeg = ui.seg('phase', ['ZERO LATENCY', 'LINEAR']);
  const qualMenu = ui.menuBtn('lin_quality', 'QUALITY'), anaMenu = ui.menuBtn('analyzer', 'ANALYZER');
  const scaleDial = ui.dial('gain_scale', 'SCALE', 's'), outDial = ui.dial('out_gain', 'OUTPUT', 's', true);
  const autoBtn = ui.toggle('auto_gain', 'AUTO GAIN'); autoBtn.small = true;
  const atkDial = ui.dial('dyn_attack', 'ATTACK', 's'), relDial = ui.dial('dyn_release', 'RELEASE', 's');
  atkDial.tip = 'Dynamic bands: how fast they react'; relDial.tip = 'Dynamic bands: how fast they recover'; scaleDial.tip = 'Gain Scale: scales every band’s gain (negative inverts the curve)';
  const rangeBtn = ui.button('±' + dbRange, () => { const r = [3, 6, 12, 24, 30]; ui.openMenu(rangeBtn.r, r.map(v => '±' + v + ' dB'), Math.max(0, r.indexOf(dbRange)), i => setRange(r[i])); });
  rangeBtn.small = true; rangeBtn.tip = 'Display range of the EQ curve';
  const setRange = v => { dbRange = v; rangeBtn.label = '±' + v; try { localStorage.setItem(RANGE_KEY, String(v)); } catch(e){} };

  // ---- helpers
  const V = (b, f) => ui.value(b_(b, f));
  const used = b => V(b, 'used') >= 0.5, bypassed = b => V(b, 'bypass') >= 0.5, shape = b => Math.round(V(b, 'shape'));
  const freq = b => V(b, 'freq'), gain = b => V(b, 'gain'), qOf = b => V(b, 'q'), place = b => Math.round(V(b, 'place'));
  const isDyn = b => V(b, 'dyn') >= 0.5 && hasGain(shape(b));
  const effGain = b => gain(b) * ui.value('gain_scale') * 0.01;
  const mouseScale = () => { const s = ui.value('gain_scale') * 0.01; return Math.abs(s) < 0.05 ? (s < 0 ? -0.05 : 0.05) : s; };
  const summary = b => SHAPES[shape(b)] + '  ' + ui.fmt(b_(b, 'freq'));
  const sr = () => ui.sr || 48000;
  const protoOf = (b, extra = 0) => proto(shape(b), freq(b), effGain(b) + extra, qOf(b), SLOPE_V[clamp(Math.round(V(b, 'slope')), 0, 8)], sr());
  const yOfDb = db => display.cy - db / dbRange * (display.h / 2 - 14), dbOfY = y => (display.cy - y) / (display.h / 2 - 14) * dbRange;
  const xOfF = f => ui.freqToX(display, f, LO, HI), fOfX = x => ui.xToFreq(display, x, LO, HI);
  function nodePos(b){
    const x = clamp(xOfF(freq(b)), display.x + 8, display.r - 8);
    const y = hasGain(shape(b)) ? yOfDb(effGain(b) + dyn[b]) : yOfDb(0);
    return [x, clamp(y, display.y + 8, display.b - 8)];
  }
  function nodeAt(x, y){ let best = -1, bd = 13 * 13 * (ui.narrow ? 2.2 : 1);
    for (let b = 0; b < NB; b++){ if (!used(b)) continue; const [nx, ny] = nodePos(b), d = (nx - x) ** 2 + (ny - y) ** 2; if (d < bd){ bd = d; best = b; } } return best; }
  const syncSel = () => { if (sel >= 0 && sel !== lastSel){ lastSel = sel; PumpFX.setSel(ui.t, ui.k, sel); } };
  function select(b){ sel = b; if (b >= 0 && !selMask[b]){ selMask.fill(false); selMask[b] = true; } rebind(); syncSel(); }
  function selectOnly(b){ selMask.fill(false); if (b >= 0) selMask[b] = true; sel = b; rebind(); syncSel(); }
  function selectedBands(){ const v = []; for (let b = 0; b < NB; b++) if (selMask[b] && used(b)) v.push(b); if (!v.length && sel >= 0 && used(sel)) v.push(sel); return v; }
  function rebind(){
    const has = sel >= 0 && used(sel); if (!has) sel = -1;
    const bind = (c, f) => { c.sym = has ? b_(sel, f) : null; c.visible = has; };
    bind(freqDial, 'freq'); bind(gainDial, 'gain'); bind(qDial, 'q'); bind(slopeMenu, 'slope'); bind(placeSeg, 'place'); bind(dynBtn, 'dyn');
    bind(rangeDial, 'dyn_range'); bind(threshDial, 'dyn_thresh'); bind(bandBypassBtn, 'bypass'); bind(shapeSeg, 'shape');
    soloBtn.visible = delBtn.visible = has;
    if (has){ const s = shape(sel); gainDial.enabled = hasGain(s); slopeMenu.enabled = hasSlope(s); dynBtn.enabled = hasGain(s); rangeDial.enabled = threshDial.enabled = isDyn(sel);
      if (isShelf(s)){ const b = sel, idx = () => { const v = Math.round(V(b, 'slope')); return v <= 0 ? 0 : v <= 1 ? 1 : 2; }, m = [0, 1, 3];
        slopeMenu.itemsFn = () => [SLOPES[0], SLOPES[1], SLOPES[3]]; slopeMenu.getFn = idx; slopeMenu.setFn = i => ui.setGesture(b_(b, 'slope'), m[clamp(i, 0, 2)]); slopeMenu.textFn = () => SLOPES[m[idx()]]; }
      else slopeMenu.itemsFn = slopeMenu.getFn = slopeMenu.setFn = slopeMenu.textFn = null;
      qDial.label = [1, 2, 3, 4, 7].includes(s) ? 'RESO' : 'Q'; }
    qualMenu.enabled = Math.round(ui.value('phase')) === 1;
  }
  const freeBand = () => { for (let b = 0; b < NB; b++) if (!used(b)) return b; return -1; };
  function createBand(f, g, dynamic){
    const b = freeBand(); if (b < 0){ ui.status = 'All 24 bands are in use'; return -1; }
    let sh = 0, slope = 1, q = 1;
    if (f < 30){ sh = 3; slope = 3; q = 0.707; g = 0; } else if (f > 17000){ sh = 4; slope = 3; q = 0.707; g = 0; }
    else if (f < 70){ sh = 1; q = 0.707; } else if (f > 9000){ sh = 2; q = 0.707; }
    const o = {}; const set = (fl, v) => o[b_(b, fl)] = v;
    set('shape', sh); set('freq', f); set('gain', Math.round(g * 10) / 10); set('q', q); set('slope', slope); set('place', 0); set('dyn', dynamic && sh <= 2 ? 1 : 0);
    set('bypass', 0); set('dyn_range', -6); set('dyn_thresh', -30); set('used', 1);
    ui.setMany(o); selectOnly(b); return b;
  }
  function deleteBand(b){ const o = {[b_(b, 'used')]:0}; ui.setMany(o); if (soloLatch === b){ soloLatch = -1; pushSolo(); } selMask[b] = false; if (sel === b) sel = -1; rebind(); }
  function deleteSelected(){ const v = selectedBands(); if (!v.length) return; const o = {}; for (const b of v){ o[b_(b, 'used')] = 0; selMask[b] = false; if (soloLatch === b) soloLatch = -1; } ui.setMany(o); sel = -1; pushSolo(); rebind(); }
  const pushSolo = () => ui.hold(0, soloHeld ? soloBand : soloLatch);
  const pushAud = on => ui.hold(1, on ? 1 : 0, audF, audQ, 0);
  const bandColor = b => { const t = ui.th; switch (place(b)){ case 3: return mix(t.neon, t.ink, 0.35); case 4: return t.second; case 1: case 2: return mix(t.second, t.ink, 0.3); default: return t.dark ? t.neon : mix(t.neon, t.ink, 0.55); } };
  const readout = b => { let s = 'Band ' + (b + 1) + '  ' + SHAPES[shape(b)] + '  ' + ui.fmt(b_(b, 'freq')); if (hasGain(shape(b))) s += '  ' + ui.fmt(b_(b, 'gain')); s += '  Q ' + qOf(b).toFixed(2);
    if (hasSlope(shape(b)) && !hasGain(shape(b))) s += '  ' + SLOPES[clamp(Math.round(V(b, 'slope')), 0, 8)]; return s; };
  function nearCurve(x, y){ for (let p = 0; p < NP - 1; p++) if (x >= curveX[p] && x <= curveX[p + 1]){ const t = (x - curveX[p]) / Math.max(1e-3, curveX[p + 1] - curveX[p]); return Math.abs(yOfDb(curveDb[p] + (curveDb[p + 1] - curveDb[p]) * t) - y) < 7; } return false; }
  function startNodeDrag(x, y){
    dragSet = selectedBands(); for (const b of dragSet) orig[b] = [freq(b), gain(b), qOf(b)];
    ui.begin(); dmode = DM.nodes; pressX = lastX = x; pressY = lastY = y; effX = effY = 0; axis = 0; dragBand = sel;
  }
  function openNodeEntry(b){
    const [nx, ny] = nodePos(b), r = R(clamp(nx - 50, display.x + 4, display.r - 104), ny < display.y + 50 ? ny + 16 : ny - 38, 100, 22);
    const ring = [[b_(b, 'freq'), r]]; if (hasGain(shape(b))) ring.push([b_(b, 'gain'), r]); ring.push([b_(b, 'q'), r]);
    ui.openTextEntry(b_(b, 'freq'), r, ring); ui.status = 'Type a value (1k, A4, C#3+13); Tab: next field (frequency, gain, Q); Enter: done';
  }
  function cycleShape(b){ const order = [0, 1, 2, 7, 3, 4, 5, 6]; const k = order.indexOf(shape(b)); ui.setGesture(b_(b, 'shape'), order[(k + 1) % 8]); ui.status = readout(b); rebind(); }
  function cycleSlope(b){ const sh = shape(b); let v = Math.round(V(b, 'slope')); v = isShelf(sh) ? (v <= 0 ? 1 : v <= 1 ? 3 : 0) : (v + 1) % 9; ui.setGesture(b_(b, 'slope'), v); ui.status = readout(b); }
  function copyBands(v){ if (!v.length){ ui.status = 'No band selected'; return; } clip = v.map(b => { const o = {}; for (const f of ['used', 'bypass', 'shape', 'freq', 'gain', 'q', 'slope', 'place', 'dyn', 'dyn_range', 'dyn_thresh']) o[f] = V(b, f); return o; });
    ui.status = 'Copied ' + v.length + (v.length === 1 ? ' band' : ' bands'); }
  function pasteBands(){
    if (!clip.length){ ui.status = 'Nothing copied yet'; return; } const o = {}; selMask.fill(false); let last = -1, n = 0; const taken = new Set();
    for (const c of clip){ let b = -1; for (let k = 0; k < NB; k++) if (!used(k) && !taken.has(k)){ b = k; break; } if (b < 0) break; taken.add(b); n++;
      for (const f in c) o[b_(b, f)] = c[f]; o[b_(b, 'used')] = 1; selMask[b] = true; last = b; }
    if (n) ui.setMany(o); if (last >= 0) sel = last; rebind();
    ui.status = n ? 'Pasted ' + n + (n === 1 ? ' band' : ' bands') + (n < clip.length ? ' (no free bands for the rest)' : '') : 'All 24 bands are in use';
  }
  function bandMenu(b, x, y){
    const it = ['#SHAPE', ...SHAPES.map((s, i) => (shape(b) === i ? '● ' : '   ') + s), '#PLACEMENT', ...PLACES.map((p, i) => (place(b) === i ? '● ' : '   ') + p), '-',
      isDyn(b) ? 'Dynamic: off' : 'Dynamic: on', bypassed(b) ? 'Enable band' : 'Bypass band', 'Copy', 'Delete band'];
    ui.openMenu(R(x, y, 10, 10), it, -1, i => { const sv = selectedBands(), o = {};
      if (i >= 1 && i <= 8) sv.forEach(k => o[b_(k, 'shape')] = i - 1);
      else if (i >= 10 && i <= 14) sv.forEach(k => o[b_(k, 'place')] = i - 10);
      else if (i === 16) o[b_(b, 'dyn')] = isDyn(b) ? 0 : 1;
      else if (i === 17){ const v = bypassed(b) ? 0 : 1; sv.forEach(k => o[b_(k, 'bypass')] = v); }
      else if (i === 18) copyBands(sv); else if (i === 19){ deleteSelected(); return; }
      if (Object.keys(o).length) ui.setMany(o); rebind(); });
  }
  function backgroundMenu(x, y){
    const rr = [3, 6, 12, 24, 30];
    ui.openMenu(R(x, y, 10, 10), ['Paste', 'Copy all bands', 'Select all', '-', 'Delete all bands', '-', ...rr.map(v => 'Range ±' + v + ' dB')], -1, i => {
      if (i === 0) pasteBands(); else if (i === 1){ const v = []; for (let b = 0; b < NB; b++) if (used(b)) v.push(b); copyBands(v); }
      else if (i === 2){ for (let b = 0; b < NB; b++) selMask[b] = used(b); rebind(); }
      else if (i === 4){ const o = {}; for (let b = 0; b < NB; b++) if (used(b)) o[b_(b, 'used')] = 0; ui.setMany(o); sel = -1; soloLatch = -1; pushSolo(); rebind(); }
      else if (i >= 6) setRange(rr[i - 6]); });
  }

  // ---- drawing
  function drawBody(){
    const t = ui.th, g = ui.g;
    ui.screenBox(display);
    ui.clip(R(display.x + 1, display.y + 1, display.w - 2, display.h - 2), () => {
      for (let k = -6; k <= 6; k++){ const y = yOfDb(dbRange * k / 6); ui.line(display.x, y, display.r, y, k === 0 ? alpha(t.ink, 0.28) : t.grid, 1); }
      ui.drawFreqGrid(display, LO, HI);
      const an = Math.round(ui.value('analyzer'));
      if (an === 1 || an === 3) ui.drawSpectrum(pre, display, 6, -90, alpha(t.ink, t.dark ? 0.10 : 0.06), alpha(t.ink, 0.25), LO, HI, 1.0);
      if (an >= 2) ui.drawSpectrum(post, display, 6, -90, alpha(t.neon, t.dark ? 0.26 : 0.34), alpha(mix(t.neon, t.ink, 0.45), 0.7), LO, HI, 1.1);
      const total = new Float64Array(NP), xs = curveX, S = sr(), W = new Float64Array(NP);
      for (let p = 0; p < NP; p++){ xs[p] = display.x + display.w * p / (NP - 1); W[p] = 2 * Math.PI * fOfX(xs[p]) / S; }
      const yc = db => clamp(yOfDb(db), display.y - 2, display.b + 2);
      for (let b = 0; b < NB; b++){
        if (!used(b) || bypassed(b)) continue;
        const a = protoOf(b, dyn[b]), isSel = b === sel, c = bandColor(b);
        if (isDyn(b)){ const a0 = protoOf(b), a1 = protoOf(b, V(b, 'dyn_range'));
          g.beginPath(); for (let p = 0; p < NP; p++){ const y = yOfDb(10 * Math.log10(magSq(a0, W[p]))); p ? g.lineTo(xs[p], y) : g.moveTo(xs[p], y); }
          for (let p = NP - 1; p >= 0; p--) g.lineTo(xs[p], yOfDb(10 * Math.log10(magSq(a1, W[p])))); g.closePath(); g.fillStyle = css(alpha(c, 0.16)); g.fill(); }
        // each band's curve is worked out again only when its values (or its dynamic movement) change
        const ck = [shape(b), freq(b), effGain(b) + dyn[b], qOf(b), V(b, 'slope'), S, display.w, display.x].join(','); let cc = cache[b];
        if (!cc || cc.k !== ck){ const d = new Float64Array(NP); for (let p = 0; p < NP; p++) d[p] = 10 * Math.log10(Math.max(magSq(a, W[p]), 1e-30)); cc = cache[b] = {k:ck, d}; }
        const dbs = cc.d; for (let p = 0; p < NP; p++) total[p] += dbs[p];
        g.beginPath(); g.moveTo(xs[0], yOfDb(0)); for (let p = 0; p < NP; p++) g.lineTo(xs[p], yc(dbs[p])); g.lineTo(xs[NP - 1], yOfDb(0)); g.closePath();
        g.fillStyle = css(alpha(c, isSel ? 0.30 : 0.14)); g.fill();
        if (isSel){ g.beginPath(); for (let p = 0; p < NP; p++) p ? g.lineTo(xs[p], yc(dbs[p])) : g.moveTo(xs[p], yc(dbs[p])); g.strokeStyle = css(alpha(c, 0.9)); g.lineWidth = 1.2; g.stroke(); }
      }
      curveDb.set(total);
      if (t.dark){ g.beginPath(); for (let p = 0; p < NP; p++) p ? g.lineTo(xs[p], yOfDb(total[p])) : g.moveTo(xs[p], yOfDb(total[p])); g.strokeStyle = css(t.glow); g.lineWidth = 7; g.stroke(); }
      g.beginPath(); for (let p = 0; p < NP; p++) p ? g.lineTo(xs[p], yc(total[p])) : g.moveTo(xs[p], yc(total[p])); g.strokeStyle = css(t.ink); g.lineWidth = 2.4; g.lineJoin = 'round'; g.stroke();
      // nodes
      for (let b = 0; b < NB; b++){
        if (!used(b)) continue; const [x, y] = nodePos(b), isS = b === sel || selMask[b], byp = bypassed(b), hov = b === hover, rr = b === sel ? 9 : 8;
        g.beginPath(); g.arc(x + 2, y + 2, rr, 0, 7); g.fillStyle = css(t.dim); g.fill();
        g.beginPath(); g.arc(x, y, rr, 0, 7); g.fillStyle = css(byp ? t.bg : isS ? t.neon : hov ? t.panel2 : t.bg); g.fill();
        g.strokeStyle = css(byp ? t.dim : t.ink); g.lineWidth = isS ? 2 : 1.5; g.stroke();
        ui.text(x, y + 0.5, String(b + 1), 'bold', 9.5, byp ? t.dim : isS ? t.neonInk : t.ink, 'center');
        const pl = place(b); if (pl) ui.text(x, y - rr - 7, ['', 'L', 'R', 'M', 'S'][pl], 'bold', 9, bandColor(b), 'center');
        if (isDyn(b)){ g.beginPath(); g.arc(x, y, rr + 3.5, 0, 7); g.strokeStyle = css(alpha(bandColor(b), 0.8)); g.lineWidth = 1; g.stroke(); }
      }
      for (let k = -3; k < 3; k++){ const db = dbRange * k / 3; ui.text(display.r - 8, yOfDb(db) - 7, k === 0 ? '0' : (db > 0 ? '+' : '') + Math.round(db), 'mono', 9.5, t.ink2, 'right'); }
      const info = dragBand >= 0 ? dragBand : hover;
      if (info >= 0 && used(info)){
        const [x, y] = nodePos(info); let s = (info + 1) + '  ' + ui.fmt(b_(info, 'freq')); if (hasGain(shape(info))) s += '  ' + ui.fmt(b_(info, 'gain')); s += '  Q ' + qOf(info).toFixed(2);
        const tw = ui.textW(s, 'mono', 11) + 14, tb = R(Math.min(display.r - tw - 6, x + 14), y < display.y + 40 ? y + 14 : y - 34, tw, 22);
        ui.fill(tb, t.ink); ui.text(tb.cx, tb.cy, s, 'mono', 11, t.on, 'center');
      } else if (sel < 0 && dmode === DM.none){
        ui.text(display.cx, display.cy - 40, ui.narrow ? 'DOUBLE-TAP TO ADD A BAND' : 'DOUBLE-CLICK TO ADD A BAND', 'label', 14, t.ink2, 'center', 2);
        ui.text(display.cx, display.cy - 18, ui.narrow ? 'LONG-PRESS: MENU' : 'HOLD ALT AND DRAG TO LISTEN TO A FREQUENCY RANGE', 'label', 11, t.ink2, 'center', 1.5);
      }
      if (dmode === DM.rect){ const rr = R(Math.min(pressX, curX), Math.min(pressY, curY), Math.abs(curX - pressX), Math.abs(curY - pressY)); ui.fill(rr, alpha(t.ink, 0.06)); ui.stroke(rr, t.ink2, 1); }
      const listen = dmode === DM.aud || soloHeld || (soloLatch >= 0 && used(soloLatch));
      if (listen){
        let f, q, label, side = 0;
        if (dmode === DM.aud){ f = audF; q = audQ; label = 'LISTEN  ' + PumpFX.format('eq', 'audition_freq', f); }
        else { const sb = soloHeld ? soloBand : soloLatch; f = freq(sb); q = clamp(qOf(sb), 0.3, 12); label = 'SOLO BAND ' + (sb + 1); const sh = shape(sb); if (sh === 1 || sh === 3) side = -1; else if (sh === 2 || sh === 4 || sh === 7) side = 1; }
        const half = Math.asinh(1 / (2 * q)) / Math.LN2; let x0 = xOfF(f * Math.pow(2, -half)), x1 = xOfF(f * Math.pow(2, half));
        if (side < 0){ x0 = display.x; x1 = xOfF(f); } else if (side > 0){ x0 = xOfF(f); x1 = display.r; }
        ui.fill(R(display.x, display.y, Math.max(0, x0 - display.x), display.h), alpha(t.bg, 0.55)); ui.fill(R(x1, display.y, Math.max(0, display.r - x1), display.h), alpha(t.bg, 0.55));
        ui.line(x0, display.y, x0, display.b, t.second, 1.5); ui.line(x1, display.y, x1, display.b, t.second, 1.5);
        const tw = ui.textW(label, 'label', 12, 1) + 16, lb = R(Math.max(display.x + 6, Math.min(rangeBtn.r.x - tw - 8, xOfF(f) - tw / 2)), display.y + 10, tw, 22);
        ui.fill(lb, t.second); ui.text(lb.cx, lb.cy, label, 'label', 12, t.bg, 'center', 1);
      }
    });
    ui.drawLevelMeter(meterL, lvl[0], hold[0], -48, 6); ui.drawLevelMeter(meterR, lvl[1], hold[1], -48, 6);
    ui.text(meterL.r + 1, display.b - 7, 'OUT', 'label', 9, t.ink2, 'center', 0.8);
    ui.stroke(bandPanel, t.dim, 1); ui.stroke(globalPanel, t.dim, 1);
    if (sel < 0) ui.text(bandPanel.cx, bandPanel.cy + 10, 'No band selected', 'label', 13, t.ink2, 'center', 1);
  }

  // ---- mouse on the display (node conventions plus Alt-solo; see UIEQ.cpp)
  function down(x, y, btn, m, dbl){
    if (!display.has(x, y)) return false;
    const b = nodeAt(x, y); pressX = curX = x; pressY = curY = y;
    if (btn === 2){ if (b >= 0){ if (!selMask[b]) selectOnly(b); else { sel = b; rebind(); } bandMenu(b, x, y); } else backgroundMenu(x, y); return false; }
    if (btn !== 0) return false;
    if (b >= 0){
      if (m.alt && m.cmd){ cycleShape(b); return false; }
      if (m.alt && m.shift){ cycleSlope(b); return false; }
      if (m.alt){ if (!selMask[b]) selectOnly(b); else { sel = b; rebind(); } soloBand = b; soloHeld = true; pushSolo(); dmode = DM.solo; return true; }
      if (m.cmd){ selMask[b] = !selMask[b]; const v = selectedBands(); sel = selMask[b] ? b : (v.length ? v[0] : -1); rebind(); return false; }
      if (m.shift && sel >= 0){ const f0 = Math.min(freq(sel), freq(b)), f1 = Math.max(freq(sel), freq(b)); for (let k = 0; k < NB; k++) if (used(k) && freq(k) >= f0 && freq(k) <= f1) selMask[k] = true; sel = b; rebind(); return false; }
      if (dbl){ selectOnly(b); openNodeEntry(b); return false; }
      if (!selMask[b]) selectOnly(b); else { sel = b; rebind(); syncSel(); }
      startNodeDrag(x, y); return true;
    }
    if (m.alt && !dbl){ dmode = DM.aud; lastY = y; audF = fOfX(x); pushAud(true); return true; }
    if (dbl || m.cmd){ const nb = createBand(fOfX(x), dbOfY(y) / mouseScale(), m.alt); if (nb >= 0){ startNodeDrag(x, y); return true; } return false; }
    if (nearCurve(x, y)){ dmode = DM.pend; return true; }
    dmode = DM.rect; return true;
  }
  function drag(x, y, m){
    curX = x; curY = y; const fine = m.shift ? 0.15 : 1;
    if (dmode === DM.aud){ audF = fOfX(clamp(x, display.x, display.r)); audQ = clamp(audQ * Math.pow(2, -(y - lastY) * fine / 60), 0.3, 12); lastY = y; pushAud(true);
      ui.status = 'Listening around ' + PumpFX.format('eq', 'audition_freq', audF) + '  Q ' + audQ.toFixed(2) + '  (up/down or wheel: width)'; return; }
    if (dmode === DM.solo){ if (Math.abs(x - pressX) + Math.abs(y - pressY) > 3){ startNodeDrag(pressX, pressY); drag(x, y, m); } return; }
    if (dmode === DM.pend){ if (Math.abs(x - pressX) + Math.abs(y - pressY) > 4){ const nb = createBand(fOfX(pressX), dbOfY(pressY) / mouseScale()); if (nb >= 0){ startNodeDrag(pressX, pressY); drag(x, y, m); } else dmode = DM.none; } return; }
    if (dmode === DM.nodes){
      effX += (x - lastX) * fine; effY += (y - lastY) * fine; lastX = x; lastY = y; let dx = effX, dy = effY;
      if (m.alt && !soloHeld){ if (axis === 0 && Math.abs(dx) + Math.abs(dy) > 6) axis = Math.abs(dx) >= Math.abs(dy) ? 1 : 2; if (axis === 1) dy = 0; else if (axis === 2) dx = 0; } else axis = 0;
      const sc = mouseScale();
      for (const b of dragSet){ const o = orig[b]; if (!o) continue;
        ui.setParam(b_(b, 'freq'), Math.min(HI, Math.max(LO, o[0] * Math.pow(HI / LO, dx / display.w))));
        if (m.cmd || soloHeld || !hasGain(shape(b))) ui.setParam(b_(b, 'q'), o[2] * Math.pow(2, -dy / 60));
        else ui.setParam(b_(b, 'gain'), o[1] - dy / (display.h / 2 - 14) * dbRange / sc); }
      if (dragBand >= 0) ui.status = readout(dragBand);
    }
  }
  function up(x, y){
    if (dmode === DM.nodes) ui.end();
    else if (dmode === DM.aud) pushAud(false);
    else if (dmode === DM.rect){ const rr = R(Math.min(pressX, x), Math.min(pressY, y), Math.abs(x - pressX), Math.abs(y - pressY));
      if (rr.w < 4 && rr.h < 4) selectOnly(-1);
      else { selMask.fill(false); sel = -1; for (let b = 0; b < NB; b++){ if (!used(b)) continue; const [nx, ny] = nodePos(b); if (rr.has(nx, ny)){ selMask[b] = true; if (sel < 0) sel = b; } } rebind(); syncSel(); } }
    else if (dmode === DM.pend) selectOnly(-1);
    if (soloHeld){ soloHeld = false; pushSolo(); }
    dmode = DM.none; dragBand = -1; dragSet = [];
  }
  function hoverFn(x, y){
    hover = display.has(x, y) ? nodeAt(x, y) : -1;
    if (hover >= 0) ui.status = readout(hover) + '    drag · Alt: solo · wheel: Q · right-click: menu';
    else if (display.has(x, y)){ const d = dbOfY(y); ui.status = PumpFX.format('eq', 'audition_freq', fOfX(x)) + '  ' + (d >= 0 ? '+' : '') + d.toFixed(1) + ' dB    double-click: add band · Alt-hold: listen · drag: select'; }
  }
  function wheel(x, y, dy, m){
    if (dmode === DM.aud){ audQ = clamp(audQ * Math.pow(2, dy > 0 ? 0.2 : -0.2), 0.3, 12); pushAud(true); ui.status = 'Listen width: Q ' + audQ.toFixed(2); return true; }
    if (!display.has(x, y)) return false; if (dmode === DM.nodes || dmode === DM.pend) return true;
    let b = nodeAt(x, y); if (b < 0 && sel >= 0 && used(sel) && Math.abs(x - xOfF(freq(sel))) < 30) b = sel; if (b < 0) return false;
    const dir = dy > 0 ? 1 : -1, sh = shape(b);
    if (m.alt && isDyn(b)) ui.setGesture(b_(b, 'dyn_range'), V(b, 'dyn_range') + dir * (m.shift ? 0.1 : 0.5));
    else if (m.cmd && hasGain(sh)) ui.setGesture(b_(b, 'gain'), gain(b) + dir * (m.shift ? 0.1 : 0.5));
    else if ((sh === 3 || sh === 4) && !m.shift) ui.setGesture(b_(b, 'slope'), V(b, 'slope') + dir);
    else ui.setGesture(b_(b, 'q'), qOf(b) * Math.pow(2, dir * (m.shift ? 0.02 : 0.1)));
    ui.status = readout(b); return true;
  }
  function key(e, m){
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (e.key === 'Delete' || e.key === 'Backspace'){ deleteSelected(); return true; }
    if (m.cmd && k === 'a'){ for (let b = 0; b < NB; b++) selMask[b] = used(b); if (sel < 0) for (let b = 0; b < NB; b++) if (used(b)){ sel = b; break; } rebind(); return true; }
    if (m.cmd && k === 'c'){ copyBands(selectedBands()); return true; }
    if (m.cmd && k === 'v'){ pasteBands(); return true; }
    if (m.cmd && k === 'd'){ if (selectedBands().length){ copyBands(selectedBands()); pasteBands(); } return true; }
    if (!m.cmd && k === 'b'){ const o = {}; for (const b of selectedBands()) o[b_(b, 'bypass')] = bypassed(b) ? 0 : 1; ui.setMany(o); return true; }
    if (e.key === 'Escape' && sel >= 0){ selectOnly(-1); return true; }
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){
      const v = []; for (let b = 0; b < NB; b++) if (used(b)) v.push(b); if (!v.length) return true; v.sort((a, b) => freq(a) - freq(b));
      let i = v.indexOf(sel); i = i < 0 ? (e.key === 'ArrowLeft' ? v.length - 1 : 0) : (i + (e.key === 'ArrowLeft' ? v.length - 1 : 1)) % v.length; selectOnly(v[i]); return true; }
    return false;
  }

  // ---- layout (desktop: the plugin's; narrow: the same parts stacked for a folded phone)
  function layout(narrow){
    const W = ui.W;
    if (!narrow){
      display = R(14, 58, W - 28 - 34, 392); meterL = R(display.r + 12, display.y, 9, display.h - 18); meterR = R(meterL.r + 2, display.y, 9, display.h - 18);
      rangeBtn.r = R(display.r - 52, display.y + 8, 44, 22);
      const y0 = 470; bandPanel = R(14, y0, 676, 164);
      bandSel.r = R(26, y0 + 10, 150, 26); shapeSeg.r = R(190, y0 + 10, 488, 26);
      freqDial.r = R(28, y0 + 52, 64, 84); gainDial.r = R(104, y0 + 52, 64, 84); qDial.r = R(180, y0 + 52, 64, 84);
      slopeMenu.r = R(262, y0 + 56, 160, 26); placeSeg.r = R(262, y0 + 96, 160, 26);
      dynBtn.r = R(440, y0 + 56, 88, 26); rangeDial.r = R(436, y0 + 88, 54, 70); threshDial.r = R(494, y0 + 88, 54, 70);
      bandBypassBtn.r = R(562, y0 + 56, 52, 26); soloBtn.r = R(620, y0 + 56, 52, 26); delBtn.r = R(562, y0 + 96, 110, 26);
      globalPanel = R(706, y0, W - 720, 164); const gx = 718;
      phaseSeg.r = R(gx, y0 + 10, 196, 26); qualMenu.r = R(gx + 206, y0 + 10, 102, 26); anaMenu.r = R(gx, y0 + 46, 196, 26); autoBtn.r = R(gx + 206, y0 + 46, 102, 26);
      scaleDial.r = R(gx - 4, y0 + 84, 60, 72); outDial.r = R(gx + 58, y0 + 84, 60, 72); atkDial.r = R(gx + 128, y0 + 84, 60, 72); relDial.r = R(Math.min(gx + 190, globalPanel.r - 66), y0 + 84, 60, 72);
    } else {
      display = R(10, 94, W - 20 - 30, 296); meterL = R(display.r + 8, display.y, 9, display.h - 18); meterR = R(meterL.r + 2, display.y, 9, display.h - 18);
      rangeBtn.r = R(display.r - 52, display.y + 8, 44, 22);
      const y0 = 414; bandPanel = R(6, y0 - 8, W - 12, 244);
      bandSel.r = R(14, y0, 150, 26); bandBypassBtn.r = R(176, y0, 62, 26); soloBtn.r = R(246, y0, 62, 26); delBtn.r = R(316, y0, W - 330, 26);
      shapeSeg.r = R(14, y0 + 38, W - 28, 26);
      freqDial.r = R(16, y0 + 76, 64, 84); gainDial.r = R(86, y0 + 76, 64, 84); qDial.r = R(156, y0 + 76, 64, 84);
      slopeMenu.r = R(234, y0 + 80, W - 248, 26); placeSeg.r = R(234, y0 + 118, W - 248, 26);
      dynBtn.r = R(234, y0 + 160, 92, 26); rangeDial.r = R(336, y0 + 158, 54, 70); threshDial.r = R(396, y0 + 158, 54, 70);
      const y1 = 664; globalPanel = R(6, y1 - 8, W - 12, 148);
      phaseSeg.r = R(14, y1, 220, 26); qualMenu.r = R(242, y1, 110, 26); autoBtn.r = R(360, y1, W - 374, 26); anaMenu.r = R(14, y1 + 34, 220, 26);
      scaleDial.r = R(244, y1 + 30, 56, 72); outDial.r = R(298, y1 + 30, 56, 72); atkDial.r = R(352, y1 + 30, 56, 72); relDial.r = R(406, y1 + 30, 56, 72);
    }
  }

  return {
    layout, drawBody, down, drag, up, hover:hoverFn, wheel, key,
    leave(){ hover = -1; },
    changed(s){ if (/_used$|_shape$|_dyn$|^phase$/.test(s)) rebind(); },
    reload(){
      const info = PumpFX.get(ui.t, ui.k);
      if (sel >= 0 && !used(sel)) sel = -1;
      if (info && info.sel !== lastSel && used(info.sel)){ sel = info.sel; lastSel = info.sel; }   // the FX screen's BAND moved: follow it
      for (let k = 0; k < NB; k++) selMask[k] = selMask[k] && used(k) || k === sel;
      if (soloLatch >= 0 && !used(soloLatch)){ soloLatch = -1; pushSolo(); }
      if (sel < 0 && lastSel < 0) for (let k = 0; k < NB; k++) if (used(k)){ sel = k; selMask[k] = true; break; }
      rebind();
    },
    frame(fr){
      const t = fr.t; for (let b = 0; b < NB; b++) dyn[b] = t[5 + b] || 0;
      pk[0] = Math.max(pk[0], t[1]); pk[1] = Math.max(pk[1], t[2]);
      for (const rg of fr.rings){ if (rg.k === 0) fPre.push(rg); else if (rg.k === 1) fPost.push(rg); }
      const n = analyzerSizeOf(fr.sr); pre.setup(fr.sr, n); post.setup(fr.sr, n);
      const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      const an = Math.round(ui.value('analyzer'));
      if (an === 3) Spectrum.update(dt, pre, fPre, post, fPost); else if (an === 1) Spectrum.update(dt, pre, fPre); else if (an === 2) Spectrum.update(dt, post, fPost);
    },
    tick(dt){
      for (let c = 0; c < 2; c++){ const p = U.dbOf(pk[c]); pk[c] = 0; lvl[c] = Math.max(p, lvl[c] - 26 * dt);
        if (p >= hold[c]){ hold[c] = p; holdT[c] = 1.5; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(p, hold[c] - 18 * dt); }
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100; },
    footer(){ let n = 0; for (let b = 0; b < NB; b++) n += used(b) ? 1 : 0; return n + '/' + NB + ' BANDS   ' + (Math.round(ui.value('phase')) ? 'LINEAR PHASE' : 'ZERO LATENCY') + '   LATENCY ' + (ui.lat | 0); },
    close(){ ui.hold(0, -1); ui.hold(1, 0, audF, audQ, 0); },
    blur(){ if (soloHeld){ soloHeld = false; pushSolo(); } if (dmode === DM.aud){ pushAud(false); dmode = DM.none; } },
    help(){ return [
      [(U.isMac ? 'Double-click / Cmd+click' : 'Double-click / Ctrl+click'), 'add a band there (Alt + double-click: a dynamic band); or drag the curve'],
      ['Drag a node', 'frequency and gain (all selected bands); Alt while dragging: one axis'],
      ['Ctrl/Cmd + drag up/down', 'Q (width); mouse wheel on a node: Q, on cuts: slope'],
      ['Ctrl/Cmd + wheel  ·  Alt + wheel', 'gain  ·  dynamic range'],
      ['Alt + hold a node', 'solo it while held; drag: left/right frequency, up = narrower, down = wider'],
      ['Alt + hold / drag empty space', 'listen to the region under the mouse; up/down (or wheel): width'],
      ['Ctrl/Cmd+click · Shift+click · drag', 'add to selection · select a range · box-select'],
      ['Ctrl/Cmd+Alt+click · Alt+Shift+click', 'next shape · next slope'],
      ['Double-click a node', 'type frequency, Tab to gain and Q (1k, A4, C#3+13)'],
      ['Right-click', 'band menu (shape, placement, dynamic, copy, delete) or display menu (paste)'],
      ['Delete · B · Esc · Left/Right', 'delete · bypass · deselect · previous/next band'],
      ['Ctrl/Cmd + A / C / V / D', 'select all / copy / paste / duplicate bands']]; },
  };
}});
const analyzerSizeOf = sr => U.analyzerSize(sr || 48000);
})();
