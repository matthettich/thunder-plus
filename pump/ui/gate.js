/* …pump GATE pop-up (31 Oct 2026): plugins/gate/UIGate.cpp in the browser. The step grid is the main screen: one bar
   per step (height = level, slots as wide as their swung duration), the current step lit, beat and bar lines, the
   gate's envelope over the bars (left in ink, the offset right channel in the second colour), the playhead, and under
   the bars the output's level along the last cycle. Click a step = on / off, drag up / down = its level, drag across
   steps = paint on / off, Alt- or right-drag = draw levels freehand (a phone: long-press then drag, or the DRAW key of
   the narrow layout). Pattern keys (clear, fill, invert, random, shift), then rhythm, envelope, mode and output.
   The envelope display runs GateEngine.hpp's own clock and envelope (locate, patternStep, gateTarget, Env, simulate),
   ported below. Telemetry t[5 + i] (wasm/fx/gate.cpp): 0, 1 opening now L, R; 2 position (quarters); 3 tempo;
   4 quarters a bar; 5 locked to the song; 6…261 the level display (8 bins a step, linear). */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, Dial} = U;

/* ---------------------------------------------------------------- GateParams.hpp / Tempo.hpp / GateEngine.hpp */
const MAX_STEPS = 32, BINS_PER_STEP = 8, ENV_POINTS = 1024, VOLUME = 0, FILTER = 1;
const MODE_NAMES = ['Volume', 'Filter', 'Both'];
const NOTE_NAMES = ['1/64', '1/32T', '1/32', '1/32D', '1/16T', '1/16', '1/16D', '1/8T', '1/8', '1/8D', '1/4T', '1/4', '1/4D', '1/2T', '1/2', '1/2D', '1/1', '2/1'];
const NOTE_Q = [1 / 16, 1 / 12, 1 / 8, 3 / 16, 1 / 6, 1 / 4, 3 / 8, 1 / 3, 1 / 2, 3 / 4, 2 / 3, 1, 1.5, 4 / 3, 2, 3, 4, 8];
const noteQuarters = i => NOTE_Q[clamp(i | 0, 0, NOTE_Q.length - 1)];
const STEP = [...Array(MAX_STEPS)].map((_, k) => 'step' + String(k + 1).padStart(2, '0'));
const ROUND_TAU = 0.00035;
const tauCoef = (sec, sr) => sec <= 0 ? 0 : Math.exp(-1 / (sec * sr));

// the pattern clock: step number from the song's start and the phase through its (swung) duration
function locate(q, stepQ, swing){
  const s = q / stepQ + 1e-7, pair = Math.floor(s * 0.5), w = s - 2 * pair, split = 1 + swing;
  return w < split ? {global:2 * pair, phase:w / split} : {global:2 * pair + 1, phase:(w - split) / (2 - split)};
}
function patternStep(global, steps, rotate){ let m = (global - rotate) % steps; if (m < 0) m += steps; return m; }
function gateTarget(q, stepQ, swing, steps, rotate, length, lv01){
  const p = locate(q, stepQ, swing); return p.phase < length ? lv01[patternStep(p.global, steps, rotate)] : 0;
}
function envCoefs(sr, attackMs, releaseMs, smoothPct, stepSeconds){
  const sm = clamp(smoothPct, 0, 100) * 0.01;
  return {up:attackMs > 0 ? Math.min(1, 1 / (attackMs * 0.001 * sr)) : 1, down:releaseMs > 0 ? Math.min(1, 1 / (releaseMs * 0.001 * sr)) : 1,
    glide:sm > 0 ? tauCoef(0.45 * sm * Math.sqrt(sm) * stepSeconds, sr) : 0, round:1 - Math.exp(-1 / (ROUND_TAU * sr))};
}
// The gate's openness after DEPTH over [q0, q1) quarter notes at `points` samples, after one pattern's warm-up.
function simulate(s, bpm, q0, q1, points, right, out){
  if (points <= 0 || !(q1 > q0)) return;
  const steps = clamp(s.steps, 1, MAX_STEPS), stepQ = noteQuarters(s.rate), secPerQ = 60 / clamp(bpm, 20, 999), dq = (q1 - q0) / points;
  const c = envCoefs(1 / (dq * secPerQ), s.attack, s.release, s.smooth, stepQ * secPerQ);
  const lv = s.level.map(v => clamp(v, 0, 100) * 0.01);
  const sw = clamp(s.swing, 0, 75) * 0.01, len = clamp(s.length, 5, 100) * 0.01, rot = right ? clamp(s.offset, 0, 16) : 0, dep = clamp(s.depth, 0, 100) * 0.01;
  const warm = Math.min(100000, Math.ceil(steps * stepQ / dq));
  let glide, slew, r1, o; glide = slew = r1 = o = gateTarget(q0 - warm * dq, stepQ, sw, steps, rot, len, lv);
  for (let i = -warm; i < points; i++){
    const target = gateTarget(q0 + i * dq, stepQ, sw, steps, rot, len, lv);
    glide = target + (glide - target) * c.glide;
    const d = glide - slew; slew += d > 0 ? Math.min(d, c.up) : Math.max(d, -c.down);
    r1 += (slew - r1) * c.round; o += (r1 - o) * c.round;
    if (i >= 0) out[i] = 1 - dep * (1 - o);
  }
}
const msText = ms => ms >= 1000 ? (ms / 1000).toFixed(2) + ' s' : ms >= 100 ? ms.toFixed(0) + ' ms' : ms.toFixed(1) + ' ms';
// the plugin's formatParam
function gateFmt(sym, v){
  if ((sym === 'attack' || sym === 'release') && v < 0.005) return '0 ms';
  if (sym === 'stereo_offset'){ if (v < 0.5) return 'OFF'; return Math.round(v) + ' STEP' + (v > 1.5 ? 'S' : ''); }
  return PumpFX.format('gate', sym, v);
}

U.define('gate', {W:1000, H:620, nW:480, nH:950, bypass:true, back:0, make(ui){
  let gridR = R(), bars = R(), lane = R(), meterL = R(), meterR = R(), rhythmP = R(), envP = R(), modeP = R(), outP = R(), hintY = 0, readout = null;
  // feeds and ballistics
  const openNow = [1, 1], lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0], pout = [0, 0], openShow = [1, 1];
  const binsRaw = new Float32Array(MAX_STEPS * BINS_PER_STEP), laneDb = new Float32Array(MAX_STEPS * BINS_PER_STEP).fill(-60), envBuf = new Float32Array(ENV_POINTS);
  let posQ = 0, posQ0 = 0, posT = -1e9, bpm = 120, barQ = 4, synced = false;
  // editing
  let drag = null, dragStep = -1, paintFrom = -1, hoverStep = -1, paintValue = 100, dragY0 = 0, dragLevel0 = 0, lastX = 0, lastY = 0, lastFine = false;
  let gOpen = false, pendTap = -1, drawLatch = false, ptrType = 'mouse';
  // (the pointer type of the press: a touch tap toggles on release, so a long-press can turn into a freehand draw)
  ui.cv.addEventListener('pointerdown', e => { ptrType = e.pointerType || 'mouse'; }, true);

  const V = s => ui.value(s);
  const btn = (label, fn, tip) => { const b = ui.button(label, fn); b.small = true; b.tip = tip; return b; };
  const clearBtn = btn('CLEAR', () => patternOp(0), 'Clear: every step of the pattern to 0 %');
  const fillBtn = btn('FILL', () => patternOp(1), 'Fill: every step of the pattern to 100 %');
  const invertBtn = btn('INVERT', () => patternOp(2), 'Invert: open steps close and closed steps open (levels flip around 50 %)');
  const randomBtn = btn('RANDOM', () => patternOp(3), 'Random: a new pattern (mostly on and off steps, a few accents; the first step stays on)');
  const shiftLBtn = btn('', () => patternOp(4), 'Shift the pattern one step earlier (Left arrow)');
  const shiftRBtn = btn('', () => patternOp(5), 'Shift the pattern one step later (Right arrow)');
  // (Thunder: a phone has no Alt or right button, so the narrow layout has a latch for freehand drawing)
  const drawBtn = ui.button('DRAW', () => { drawLatch = !drawLatch; ui.status = drawLatch ? 'Draw: drag across the steps to draw their levels' : 'Draw off: tap = on / off, drag = level'; }, () => drawLatch);
  drawBtn.small = true; drawBtn.tip = 'Draw: dragging on the pattern draws levels freehand (as Alt / right-drag; a long-press works too)';

  const dial = (sym, label, group, tip, size = 'm', bip = false) => { const d = ui.dial(sym, label, size, bip); d.tabGroup = group; d.tip = tip; d.fmt = v => gateFmt(sym, v); return d; };
  const rateMenu = ui.menuBtn('rate', 'RATE'); rateMenu.arrows = true;
  rateMenu.tip = 'Rate: the length of one step as a note value (T = triplet, D = dotted), locked to the host tempo';
  const stepsDial = dial('steps', 'STEPS', 1, 'Steps: the pattern’s length, 1..32 (odd lengths drift against the bar for polyrhythms)');
  const swingDial = dial('swing', 'SWING', 1, 'Swing: every second step starts later (33 % = a triplet shuffle)');
  const offsetDial = dial('stereo_offset', 'OFFSET', 1, 'Stereo offset: the right channel plays the pattern this many steps later (stereo motion; OFF = both the same)');
  const lengthDial = dial('length', 'LENGTH', 2, 'Gate length: how much of each step the gate stays open (100 % = legato)', 'l');
  const attackDial = dial('attack', 'ATTACK', 2, 'Attack: the time to open fully (a minimum ramp always keeps it click-free)');
  const releaseDial = dial('release', 'RELEASE', 2, 'Release: the time to close fully (a minimum ramp always keeps it click-free)');
  const smoothDial = dial('smooth', 'SMOOTH', 2, 'Smooth: glides between step levels, from hard chops to a rolling tremolo');
  const depthDial = dial('depth', 'DEPTH', 2, 'Depth: how far closed steps close (100 % = silence in Volume mode)');
  const modeSeg = ui.seg('mode', ['VOLUME', 'FILTER', 'BOTH']);
  modeSeg.tip = 'Mode: closed steps turn down (Volume), close a resonant low-pass (Filter), or both   (keys 1-3)';
  const lowDial = dial('filter_low', 'LOW', 3, 'Filter low: the cutoff of a fully closed step', 's');
  const highDial = dial('filter_high', 'HIGH', 3, 'Filter high: the cutoff of a fully open step', 's');
  const resoDial = dial('resonance', 'RESO', 3, 'Resonance of the step filter', 's');
  const mixDial = dial('mix', 'MIX', 0, 'Mix: gated vs dry (no latency, so it lines up exactly)');
  const outDial = dial('out_gain', 'OUTPUT', 0, 'Output gain', 'm', true);

  /* ---- the pattern as the UI sees it */
  const steps = () => clamp(Math.round(V('steps')), 1, MAX_STEPS);
  const stepQ = () => noteQuarters(Math.round(V('rate')));
  const swing = () => clamp(V('swing'), 0, 75) * 0.01;
  const level = k => V(STEP[k]);
  const uiSettings = () => ({steps:steps(), rate:Math.round(V('rate')), swing:V('swing'), length:V('length'), attack:V('attack'), release:V('release'),
    smooth:V('smooth'), depth:V('depth'), offset:Math.round(V('stereo_offset')), level:STEP.map(s => V(s))});
  const slotStart = k => { const q = stepQ(); return Math.floor(k * 0.5) * 2 * q + ((k & 1) ? (1 + swing()) * q : 0); };
  const slotEnd = k => k + 1 >= steps() ? steps() * stepQ() : slotStart(k + 1);
  const xOfQ = q => bars.x + q / (steps() * stepQ()) * bars.w;
  const stepAt = x => { if (x < bars.x || x >= bars.r) return -1; const q = (x - bars.x) / bars.w * steps() * stepQ();
    for (let k = 0; k < steps(); k++) if (q < slotEnd(k)) return k; return steps() - 1; };
  const stepAtClamped = x => stepAt(clamp(x, bars.x, bars.r - 0.01));
  const levelAtY = y => clamp((bars.b - y) / bars.h * 100, 0, 100);
  const editArea = (x, y) => x >= bars.x && x < bars.r && y >= bars.y - 4 && y < bars.b + 4;

  function layout(narrow){
    const dm = Dial.heightFor('m'), dl = Dial.heightFor('l'), ds = Dial.heightFor('s');
    if (!narrow){
      gridR = R(14, 58, 934, 292); bars = R(gridR.x + 14, gridR.y + 30, gridR.w - 28, gridR.h - 92);
      meterL = R(962, 58, 9, 272); meterR = R(973, 58, 9, 272);
      const ky = 364, kh = 24;
      clearBtn.r = R(14, ky, 70, kh); fillBtn.r = R(92, ky, 60, kh); invertBtn.r = R(160, ky, 74, kh); randomBtn.r = R(242, ky, 78, kh);
      shiftLBtn.r = R(384, ky, 32, kh); shiftRBtn.r = R(422, ky, 32, kh); drawBtn.visible = false; hintY = ky + kh / 2;
      const y0 = 404, ph = 182;
      rhythmP = R(14, y0, 226, ph); envP = R(254, y0, 346, ph); modeP = R(614, y0, 226, ph); outP = R(854, y0, 132, ph);
      lengthDial.r = R(264, y0 + 34, 86, dl);
      const my = y0 + 34 + 45 - 38;   // medium dials centred on the large one
      [attackDial, releaseDial, smoothDial, depthDial].forEach((d, i) => d.r = R(356 + 60 * i, my, 60, dm));
      mixDial.r = R(858, y0 + 30, 62, dm); outDial.r = R(920, y0 + 30, 62, dm);
      readout = {capX:outP.x + 12, capY:outP.y + 124, labX:outP.x + 12, barX:outP.x + 26, barY:outP.y + 140, barW:52, valX:outP.r - 12};
    } else {
      gridR = R(10, 94, 440, 250); bars = R(gridR.x + 10, gridR.y + 40, gridR.w - 20, gridR.h - 102);
      meterL = R(457, 94, 8, 232); meterR = R(467, 94, 8, 232);
      const ky = 356, kh = 30;
      clearBtn.r = R(10, ky, 58, kh); fillBtn.r = R(74, ky, 50, kh); invertBtn.r = R(130, ky, 64, kh); randomBtn.r = R(200, ky, 70, kh);
      shiftLBtn.r = R(300, ky, 34, kh); shiftRBtn.r = R(340, ky, 34, kh); drawBtn.r = R(404, ky, 66, kh); drawBtn.visible = true; hintY = ky + kh + 14;
      const y0 = 412, ph = 182;
      rhythmP = R(10, y0, 226, ph); modeP = R(244, y0, 226, ph); envP = R(10, y0 + ph + 10, 460, ph); outP = R(10, envP.b + 10, 460, 120);
      lengthDial.r = R(envP.x + 14, envP.y + 34, 86, dl);
      const my = envP.y + 34 + 45 - 38;
      [attackDial, releaseDial, smoothDial, depthDial].forEach((d, i) => d.r = R(envP.x + 120 + 85 * i, my, 64, dm));
      mixDial.r = R(outP.x + 12, outP.y + 30, 62, dm); outDial.r = R(outP.x + 82, outP.y + 30, 62, dm);
      readout = {capX:outP.x + 170, capY:outP.y + 40, labX:outP.x + 170, barX:outP.x + 184, barY:outP.y + 56, barW:196, valX:outP.r - 12};
    }
    lane = R(bars.x, bars.b + 20, bars.w, 24);
    // the rhythm and mode panels keep the plugin's positions inside them
    const rp = rhythmP, mp = modeP;
    rateMenu.r = R(rp.x + 12, rp.y + 30, 202, 26);
    [stepsDial, swingDial, offsetDial].forEach((d, i) => d.r = R(rp.x + 12 + 69 * i, rp.y + 74, 64, dm));
    modeSeg.r = R(mp.x + 12, mp.y + 30, 202, 26);
    [lowDial, highDial, resoDial].forEach((d, i) => d.r = R(mp.x + 12 + 69 * i, mp.y + 92, 64, ds));
  }

  /* ---- drawing */
  function drawBody(){
    const t = ui.th;
    drawGrid();
    ui.drawLevelMeter(meterL, lvl[0], hold[0], -48, 6); ui.drawLevelMeter(meterR, lvl[1], hold[1], -48, 6);
    ui.text((meterL.x + meterR.r) / 2, gridR.b - 9, 'OUT', 'label', 9, t.ink2, 'center', 0.8);
    if (!ui.narrow){
      ui.caption(shiftLBtn.r.x - 52, shiftLBtn.r.cy, 'SHIFT');
      ui.text(gridR.r, hintY, 'CLICK ON/OFF  ·  DRAG LEVEL  ·  ALT/RIGHT-DRAG DRAW  ·  WHEEL ±10%', 'label', 10, t.ink2, 'right', 0.8);
    } else ui.text(10, hintY, 'TAP ON/OFF  ·  DRAG LEVEL  ·  LONG-PRESS OR DRAW: FREEHAND', 'label', 10, t.ink2, 'left', 0.8);
    for (const p of [rhythmP, envP, modeP, outP]) ui.stroke(p, t.dim, 1);
    ui.caption(rhythmP.x + 12, rhythmP.y + 16, 'RHYTHM'); ui.caption(envP.x + 12, envP.y + 16, 'ENVELOPE');
    ui.caption(modeP.x + 12, modeP.y + 16, 'MODE'); ui.caption(modeP.x + 12, lowDial.r.y - 10, 'FILTER', Math.round(V('mode')) === VOLUME ? t.dim : null);
    ui.caption(outP.x + 12, outP.y + 16, 'OUTPUT');
    drawStepShape();
    // what the gate does right now, per channel: its gain (Volume, Both) or the filter's cutoff (Filter)
    const mode = Math.round(V('mode')), ro = readout;
    ui.caption(ro.capX, ro.capY, mode === FILTER ? 'CUTOFF Hz' : 'GATE dB');
    for (let c = 0; c < 2; c++){
      const o = clamp(openShow[c], 0, 1), b = R(ro.barX, ro.barY + 16 * c, ro.barW, 8);
      ui.text(ro.labX, b.cy, c ? 'R' : 'L', 'label', 10, t.ink2, 'left');
      ui.fill(b, t.panel2); ui.fill(R(b.x, b.y, b.w * o, b.h), t.neon); ui.stroke(b, t.ink, 1);
      let s;
      if (mode === FILTER){ const lo = Math.log(V('filter_low')), hi = Math.log(V('filter_high')), f = Math.exp(hi + (lo - hi) * (1 - o)); s = f >= 1000 ? (f / 1000).toFixed(1) + 'k' : f.toFixed(0); }
      else if (o < 0.0011) s = '-INF';
      else s = Math.min(0, 20 * Math.log10(o)).toFixed(1);
      ui.text(ro.valX, b.cy, s, 'mono', 10, t.ink, 'right');
    }
  }
  // One open step between closed ones, close up: the shape ATTACK, LENGTH, RELEASE and SMOOTH give a single hit
  function drawStepShape(){
    const t = ui.th, g = ui.g, r = R(envP.x + 12, envP.y + 142, envP.w - 24, 30); ui.fill(r, t.screen);
    const sq = stepQ(), s = uiSettings(); s.steps = 4; s.swing = 0; s.offset = 0; s.depth = 100; s.level = STEP.map((_, k) => k % 4 === 1 ? 100 : 0);
    const q0 = 0.5 * sq, q1 = 3 * sq, pts = Math.min(ENV_POINTS, Math.floor(r.w));
    simulate(s, bpm, q0, q1, pts, false, envBuf);
    const xq = q => r.x + (q - q0) / (q1 - q0) * r.w, xa = xq(sq), xb = xq(2 * sq), xl = xq(sq * (1 + V('length') * 0.01));
    ui.fill(R(xa, r.y, xl - xa, r.h), alpha(t.neon, t.dark ? 0.18 : 0.30));
    ui.line(xa, r.y, xa, r.b, t.grid, 1); ui.line(xb, r.y, xb, r.b, t.grid, 1);
    g.beginPath();
    for (let i = 0; i < pts; i++){ const x = r.x + r.w * (i + 0.5) / pts, y = r.b - 2 - clamp(envBuf[i], 0, 1) * (r.h - 5); if (i === 0) g.moveTo(r.x, y); g.lineTo(x, y); }
    g.strokeStyle = css(t.ink); g.lineWidth = 1.8; g.lineJoin = 'round'; g.stroke();
    ui.stroke(r, t.ink, 1);
    const stepMs = sq * 60000 / Math.max(20, bpm);
    ui.caption(r.x, r.y - 10, 'ONE STEP');
    ui.text(r.r, r.y - 10, 'STEP ' + msText(stepMs) + '   OPEN ' + msText(stepMs * V('length') * 0.01), 'mono', 10, t.ink2, 'right');
  }
  function drawGrid(){
    const t = ui.th, g = ui.g, r = gridR; ui.screenBox(r);
    ui.caption(r.x + 10, r.y + 14, 'PATTERN');
    const n = steps(), sq = stepQ(), cycQ = n * sq, offset = Math.round(V('stereo_offset'));
    // legend and info (the narrow layout puts the legend on a line of its own)
    const lx = ui.narrow ? r.x + 10 : r.x + 84, ly = ui.narrow ? r.y + 30 : r.y + 14;
    ui.line(lx, ly, lx + 16, ly, t.ink, 2);
    ui.text(lx + 21, ly, offset > 0 ? 'ENVELOPE L' : 'ENVELOPE', 'label', 10, t.ink2, 'left', 0.8);
    if (offset > 0){ ui.line(lx + 112, ly, lx + 128, ly, t.second, 2); ui.text(lx + 133, ly, 'R  +' + offset, 'label', 10, t.ink2, 'left', 0.8); }
    ui.text(r.r - 10, r.y + 14, n + ' x ' + NOTE_NAMES[clamp(Math.round(V('rate')), 0, NOTE_NAMES.length - 1)] + '   ' + bpm.toFixed(1) + ' BPM   ' + (synced ? 'HOST SYNC' : 'FREE RUN'), 'mono', 10, t.ink2, 'right');

    // beats: alternate shading, beat lines, stronger bar lines (thinned out when they'd crowd)
    let beatQ = 1; while (cycQ / beatQ > 64) beatQ *= 2;
    const bq = barQ > 0 ? barQ : 4;
    for (let b = 0; b * beatQ < cycQ - 1e-9; b++){ const q0 = b * beatQ, q1 = Math.min(cycQ, q0 + beatQ);
      if (b & 1) ui.fill(R(xOfQ(q0), bars.y, xOfQ(q1) - xOfQ(q0), bars.h), alpha(t.ink, t.dark ? 0.035 : 0.03)); }
    for (let b = 1; b * beatQ < cycQ - 1e-9; b++){ const q = b * beatQ, isBar = Math.abs(q / bq - Math.round(q / bq)) < 1e-6, x = xOfQ(q);
      ui.line(x, bars.y, x, lane.b, isBar ? alpha(t.ink, t.dark ? 0.30 : 0.26) : t.grid, 1); }

    // where the playhead is (the left channel's step; the right one's when offset)
    const sp = locate(posQ, sq, swing()), curL = patternStep(sp.global, n, 0), curR = patternStep(sp.global, n, offset);
    const s = uiSettings(), pts = Math.min(ENV_POINTS, Math.floor(bars.w));
    ui.clip(R(bars.x, bars.y - 2, bars.w, bars.h + 4), () => {
      for (let k = 0; k < n; k++){
        const x0 = xOfQ(slotStart(k)), x1 = xOfQ(slotEnd(k)), gap = (x1 - x0) > 10 ? 2 : 1;
        const slot = R(x0 + gap, bars.y, Math.max(1, x1 - x0 - 2 * gap), bars.h), cur = k === curL;
        if (cur) ui.fill(slot, alpha(t.neon, t.dark ? 0.16 : 0.22)); else if (k === hoverStep) ui.fill(slot, alpha(t.ink, 0.05));
        const h = clamp(level(k), 0, 100) * 0.01 * bars.h;
        if (h >= 1){ const bar = R(slot.x, bars.b - h, slot.w, h);
          ui.fill(bar, cur ? t.neon : mix(t.neon, t.screen, t.dark ? 0.55 : 0.45)); ui.stroke(bar, cur ? t.ink : alpha(t.ink, 0.55), cur ? 1.5 : 1); }
        else ui.fill(R(slot.x, bars.b - 3, slot.w, 3), t.dim);
        if (k === curR && offset > 0) ui.fill(R(slot.x, bars.y, slot.w, 4), t.second);
      }
      // the envelope: what the gate does over one cycle (left in ink; the offset right channel in the second colour)
      const curve = (right, c, w) => {
        simulate(s, bpm, 0, cycQ, pts, right, envBuf);
        const yOf = v => bars.b - clamp(v, 0, 1) * (bars.h - 2) - 1;
        g.beginPath();
        for (let i = 0; i < pts; i++){ const x = bars.x + bars.w * (i + 0.5) / pts, y = yOf(envBuf[i]); if (i === 0) g.moveTo(bars.x, y); g.lineTo(x, y); }
        g.lineTo(bars.r, yOf(envBuf[pts - 1]));
        g.strokeStyle = css(c); g.lineWidth = w; g.lineJoin = 'round'; g.stroke();
      };
      if (offset > 0) curve(true, t.second, 1.6);
      curve(false, t.ink, 2);
    });

    // the playhead (through the bars and the output lane)
    const px = xOfQ(slotStart(curL) + sp.phase * (slotEnd(curL) - slotStart(curL)));
    ui.line(px, bars.y, px, lane.b, alpha(t.ink, 0.75), 1.5);

    // step numbers (thinned out when the slots are narrow)
    const sw1 = bars.w / n, every = sw1 >= 18 ? 1 : sw1 >= 9 ? 2 : 4;
    for (let k = 0; k < n; k++){
      if (k % every !== 0 && k !== curL) continue;
      const cx = 0.5 * (xOfQ(slotStart(k)) + xOfQ(slotEnd(k)));
      ui.text(cx, bars.b + 10, String(k + 1), k === curL ? 'bold' : 'mono', 9.5, k === curL ? t.ink : t.ink2, 'center');
    }
    // the output's level along the last cycle (mirrored, -48..0 dBFS)
    ui.fill(lane, alpha(t.ink, 0.04));
    const nb = n * BINS_PER_STEP;
    g.beginPath();
    for (let b = 0; b < nb; b++){
      const a = clamp((laneDb[b] + 48) / 48, 0, 1) * lane.h * 0.5, x0 = lane.x + lane.w * b / nb, x1 = lane.x + lane.w * (b + 1) / nb;
      if (a > 0.3) g.rect(x0, lane.cy - a, Math.max(0.8, x1 - x0 - 0.6), 2 * a);
    }
    g.fillStyle = css(mix(t.neon, t.ink, t.dark ? 0.15 : 0.45)); g.fill();
    ui.line(lane.x, lane.cy, lane.r, lane.cy, alpha(t.ink, 0.25), 1);
    ui.text(lane.x, lane.b + 9, 'OUTPUT LEVEL, LAST CYCLE', 'label', 9, t.ink2, 'left', 0.8);

    // the level of the step under the mouse (or being edited)
    const shown = dragStep >= 0 ? dragStep : hoverStep;
    if (shown >= 0 && shown < n){
      const cx = 0.5 * (xOfQ(slotStart(shown)) + xOfQ(slotEnd(shown)));
      const y = Math.max(bars.y + 9, bars.b - clamp(level(shown), 0, 100) * 0.01 * bars.h - 10), str = level(shown).toFixed(0) + '%';
      const tw = ui.textW(str, 'mono', 10) + 8, tag = R(clamp(cx - tw * 0.5, bars.x, bars.r - tw), y - 8, tw, 16);
      ui.fill(tag, t.ink); ui.text(tag.cx, y, str, 'mono', 10, t.on, 'center');
    }
  }
  function drawOverlay(){
    // the shift keys' arrows
    const t = ui.th;
    ui.text(shiftLBtn.r.cx, shiftLBtn.r.cy, '◀', 'mono', 11, t.ink, 'center');
    ui.text(shiftRBtn.r.cx, shiftRBtn.r.cy, '▶', 'mono', 11, t.ink, 'center');
  }

  /* ---- editing steps: a press … release is one gesture, so one undo step */
  const beginG = () => { if (!gOpen){ gOpen = true; ui.begin(); } };
  const endG = () => { if (gOpen){ gOpen = false; ui.end(); } };
  const setStep = (k, v) => { if (k < 0 || k >= steps()) return; ui.setParam(STEP[k], Math.round(clamp(v, 0, 100))); };
  const stepStatus = k => 'Step ' + (k + 1) + '  ' + level(k).toFixed(0) + '%';

  function down(x, y, b, m){
    if (!editArea(x, y)) return false;
    const k = stepAt(x); if (k < 0) return false;
    if (b === 2 && pendTap >= 0) pendTap = -1;   // a long-press after a touch: no toggle, the same gesture draws
    else beginG();
    lastX = x; lastY = y;
    if (b === 2 || (b === 0 && (m.alt || drawLatch))){
      drag = 'draw'; dragStep = k; setStep(k, levelAtY(y));
      ui.bodyActive = true;   // (kit gap: a touch long-press calls down() but doesn't mark the body active, so the drag that follows would be lost)
    } else if (b === 0){
      dragStep = paintFrom = k; dragLevel0 = level(k); paintValue = dragLevel0 >= 50 ? 0 : 100; dragY0 = y;
      if (ptrType === 'touch' || ptrType === 'pen') drag = 'pend';   // toggled on release (or turned into a paint / level / draw)
      else { drag = 'toggle'; setStep(k, paintValue); }
    } else { endG(); return false; }
    ui.status = stepStatus(k);
    return true;
  }
  function dragTo(x, y, m){
    if (!drag) return;
    if (drag === 'draw'){
      // freehand: every step the pointer crossed since the last event gets the level along the way
      const nSeg = Math.max(1, Math.floor(Math.abs(x - lastX) / 2));
      for (let i = 1; i <= nSeg; i++){ const f = i / nSeg, xi = lastX + (x - lastX) * f, yi = lastY + (y - lastY) * f, k = stepAtClamped(xi); setStep(k, levelAtY(yi)); dragStep = k; }
      lastX = x; lastY = y; ui.status = stepStatus(dragStep); return;
    }
    const j = stepAtClamped(x);
    if (drag === 'toggle' || drag === 'pend'){
      const touch = drag === 'pend';
      if (j !== dragStep){ if (touch) setStep(dragStep, paintValue); drag = 'paint'; }
      else if (Math.abs(y - dragY0) > (touch ? 10 : 3)) drag = 'level';   // a vertical drag adjusts the level it had before the click
    }
    if (drag === 'paint'){
      // drum-machine style: every step crossed takes the clicked step's new state
      for (let k = Math.min(paintFrom, j); k <= Math.max(paintFrom, j); k++) setStep(k, paintValue);
      paintFrom = j; dragStep = j;
    } else if (drag === 'level'){
      const fine = !!m.shift, perPx = (fine ? 0.1 : 1) * 100 / bars.h;
      setStep(dragStep, dragLevel0 + (dragY0 - y) * perPx);
      if (fine || lastFine){ dragLevel0 = level(dragStep); dragY0 = y; }
      lastFine = fine;
    }
    ui.status = stepStatus(dragStep);
  }
  function up(x, y, cancel){
    if (!drag) return;
    if (drag === 'pend' && !cancel){
      // a touch tap: toggle now, unless a long-press takes over (it calls down() right after this, in the same task)
      const k = dragStep, v = paintValue; pendTap = k;
      queueMicrotask(() => { if (pendTap !== k) return; pendTap = -1; setStep(k, v); endG(); ui.status = stepStatus(k); ui.dirty = true; });
    } else endG();
    drag = null; dragStep = -1; lastFine = false;
    hoverStep = ptrType === 'mouse' && editArea(x, y) ? stepAt(x) : -1;
  }

  /* ---- pattern keys: each is one undo step (one PumpFX.set) */
  const OP_NAMES = ['Cleared', 'Filled', 'Inverted', 'Random pattern', 'Shifted one step earlier', 'Shifted one step later'];
  function patternOp(op){
    const n = steps(), now = STEP.map(s => V(s)), next = {};
    for (let k = 0; k < n; k++){
      let v;
      switch (op){
        case 0: v = 0; break;
        case 1: v = 100; break;
        case 2: v = 100 - now[k]; break;
        case 3: { const u = Math.random(); v = k === 0 || u < 0.45 ? 100 : u < 0.6 ? 10 * (3 + Math.floor(Math.random() * 7)) : 0; break; }   // accents 30..90 %
        case 4: v = now[(k + 1) % n]; break;
        default: v = now[(k + n - 1) % n];
      }
      next[STEP[k]] = v;
    }
    ui.setMany(next);
    ui.status = OP_NAMES[op];
  }

  const footerText = () => (MODE_NAMES[clamp(Math.round(V('mode')), 0, 2)] + '   ' + steps() + ' x ' + NOTE_NAMES[clamp(Math.round(V('rate')), 0, NOTE_NAMES.length - 1)] +
    (ui.narrow ? '' : '   SWING ' + V('swing').toFixed(0) + '%') + '   ' + (synced ? 'SYNC' : 'FREE') + ' ' + bpm.toFixed(ui.narrow ? 0 : 1) + ' BPM').toUpperCase();
  const syncEnabled = () => { const f = Math.round(V('mode')) !== VOLUME; lowDial.enabled = highDial.enabled = resoDial.enabled = f; };

  return {
    layout, drawBody, drawOverlay,
    down, drag:dragTo, up,
    hover(x, y){
      hoverStep = editArea(x, y) ? stepAt(x) : -1;
      if (hoverStep >= 0) ui.status = stepStatus(hoverStep) + '   (click on/off, drag level, Alt/right-drag draw)';
      else if (lane.has(x, y)) ui.status = 'The output’s level along the last cycle of the pattern';
      else if (gridR.has(x, y)) ui.status = 'The pattern: bars = step levels, line = the gate’s envelope, lit = the current step';
    },
    leave(){ hoverStep = -1; },
    wheel(x, y, dy, m){
      if (!editArea(x, y)) return false; const k = stepAt(x); if (k < 0) return false;
      const st = m.shift ? 1 : 10; ui.setGesture(STEP[k], clamp(Math.round(level(k) + (dy > 0 ? st : -st)), 0, 100)); ui.status = stepStatus(k); return true;
    },
    key(e, m){
      if (m.cmd) return false;
      if (e.key >= '1' && e.key <= '3' && e.key.length === 1){ ui.setGesture('mode', +e.key - 1); ui.status = 'Mode  ' + MODE_NAMES[+e.key - 1]; return true; }
      if (e.key === 'ArrowLeft'){ patternOp(4); return true; }
      if (e.key === 'ArrowRight'){ patternOp(5); return true; }
      return false;
    },
    entryText(s){ const v = V(s); if (s === 'stereo_offset') return String(Math.round(v)); if (s === 'attack' || s === 'release') return v < 0.005 ? '0' : PumpFX.format('gate', s, v).split(' ')[0]; return null; },
    preParse(s, txt){ return s === 'stereo_offset' && /^of/i.test(txt.trim()) ? '0' : txt; },
    fmtStatus:s => gateFmt(s, V(s)),
    changed: syncEnabled, reload: syncEnabled,
    frame(fr){ const t = fr.t; if (!t || t.length < 11) return;
      pout[0] = Math.max(pout[0], t[1]); pout[1] = Math.max(pout[1], t[2]);
      openNow[0] = t[5]; openNow[1] = t[6];
      posQ0 = t[7]; posT = U.now(); if (t[8] > 1) bpm = t[8]; if (t[9] > 0) barQ = t[9]; synced = t[10] >= 0.5;
      const nb = Math.min(binsRaw.length, t.length - 11); for (let b = 0; b < nb; b++) binsRaw[b] = t[11 + b];
    },
    tick(dt){
      for (let c = 0; c < 2; c++){   // OutMeter::tickDb
        const pk = U.dbOf(pout[c]); pout[c] = 0;
        lvl[c] = Math.max(pk, lvl[c] - 26 * dt);
        if (pk >= hold[c]){ hold[c] = pk; holdT[c] = 1.5; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(pk, hold[c] - 18 * dt);
        const o = openNow[c]; openShow[c] += (o - openShow[c]) * Math.min(1, dt * 40);
      }
      // (frames come ~31 a second; the playhead runs on at the tempo in between, as the plugin reads it every UI frame)
      const age = U.now() - posT; posQ = posQ0 + (age >= 0 && age < 0.15 ? age * bpm / 60 : 0);
      const nb = steps() * BINS_PER_STEP, kk = Math.min(1, dt * 10);
      for (let b = 0; b < nb; b++){ const d = U.dbOf(binsRaw[b]); laneDb[b] += (Math.max(-60, d) - laneDb[b]) * kk; }
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100; },
    footer: footerText,
    close(){ pendTap = -1; endG(); },
    blur(){},
    help(){ return [['Click a step', 'on / off (drag across steps to paint the same)'], ['Drag a step up / down', 'its level (Shift = fine)'],
      ['Alt + drag, right-drag', 'draw levels freehand across the steps (touch: long-press, or DRAW)'], ['Wheel on a step', 'level +-10 % (Shift +-1 %)'],
      ['Left / Right', 'shift the pattern one step'], ['1 - 3', 'mode: volume, filter, both']]; },
  };
}});
})();
