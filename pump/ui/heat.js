/* …pump HEAT pop-up (stage 3): plugins/heat/UIHeat.cpp in the browser. Input (faint) and output spectra behind the band
   regions; each band glows with how hard it drives its curve (its level plus drive), shows its drive as a draggable
   bar (0..24 dB) with the curve name and band mix; crossover handles and the S / M / B strip (multiband.js). Below:
   the selected band (curve, drive, mix, level) with its transfer curve, and the globals. */
(() => {
'use strict';
const U = window.PumpUI; if (!U || !U.Multiband) return;
const {R, clamp, css, mix, alpha, Feed, Spectrum} = U;
const NB = 4, FIELDS = ['mode', 'drive', 'mix', 'level', 'solo', 'mute', 'bypass'];
const MODES = ['Warm', 'Tape', 'Tube', 'Triode', 'Retro', 'Clip'], OS = ['1x', '2x', '4x', '8x'], CLIP_KNEE = 0.4;
// HeatEngine.hpp curve(): the saturation curves (also the plugin's transfer plot)
function curve(type, x){
  switch (type){
    case 1: { const u = x / Math.sqrt(1 + x * x); return u + 0.05 * u * u; }
    case 2: { const y = x >= 0 ? Math.tanh(1.15 * x) / 1.15 : Math.tanh(0.85 * x) / 0.85; return y - 0.08 * y * y; }
    case 3: return x >= 0 ? x / Math.cbrt(1 + Math.pow(x / 0.65, 3)) : 1.35 * Math.tanh(x / 1.35);
    case 4: { const c = clamp(x, -1.5, 1.5); return c - (4 / 27) * c * c * c; }
    case 5: { const a = Math.abs(x), s = x < 0 ? -1 : 1, k = CLIP_KNEE; if (a < 1 - k) return x; if (a > 1 + k) return s; const d = a - (1 - k); return s * (a - d * d / (4 * k)); }
    default: return Math.tanh(x);
  }
}

U.define('heat', {W:1000, H:620, nW:480, nH:916, bypass:true, back:8192, make(ui){
  const bs = (b, f) => 'b' + (b + 1) + '_' + f, V = s => ui.value(s);
  let disp = R(), meterL = R(), meterR = R(), bandPanel = R(), globalPanel = R(), readout = R(), inset = R(), latInfo = R();
  const lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0], pk = [0, 0];
  const inShow = [-120, -120, -120, -120], agShow = [0, 0, 0, 0], inT = [-120, -120, -120, -120], agT = [0, 0, 0, 0];
  const pre = new Spectrum(15, 22000), post = new Spectrum(15, 22000), fPre = new Feed(32768), fPost = new Feed(32768);
  let lastFrameT = 0;

  const bandSel = ui.menuBtn(null); bandSel.arrows = true;
  bandSel.itemsFn = () => { const v = []; for (let b = 0; b < mb.numBands(); b++) v.push('Band ' + (b + 1) + '  ' + mb.bandName(b) + '  (' + mb.bandRange(b) + ')'); return v; };
  bandSel.getFn = () => mb.sel; bandSel.setFn = i => mb.select(i); bandSel.textFn = () => 'BAND ' + (mb.sel + 1) + '  ' + mb.bandName(mb.sel);
  bandSel.tip = 'Selected band (side arrows, keys 1-4 or click a band in the display)';
  const modeSeg = ui.seg(null, ['WARM', 'TAPE', 'TUBE', 'TRIODE', 'RETRO', 'CLIP']);
  modeSeg.tip = 'Curve: Warm (smooth, odd) · Tape (highs first, HF loss) · Tube (even) · Triode (asymmetric) · Retro (transformer) · Clip (rounded clipper)';
  const driveDial = ui.dial(null, 'DRIVE', 'l'); driveDial.tip = 'Drive into the curve, 0..24 dB (or drag the band’s bar in the display / mouse wheel on the band)';
  const mixDial = ui.dial(null, 'BAND MIX'); mixDial.tip = 'Band mix: saturated vs clean for this band (Ctrl/Cmd + drag up/down in the band)';
  const levelDial = ui.dial(null, 'LEVEL', 'm', true); levelDial.tip = 'Band output level';
  driveDial.tabGroup = mixDial.tabGroup = levelDial.tabGroup = 1;
  const soloBtn = ui.toggle(null, 'SOLO'), muteBtn = ui.toggle(null, 'MUTE'), bypBtn = ui.toggle(null, 'BYPASS');
  soloBtn.tip = 'Solo the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band · Alt-hold in the display)';
  muteBtn.tip = 'Mute the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)';
  bypBtn.tip = 'Bypass the selected band’s saturation (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)';
  const osSeg = ui.seg('oversampling', ['1x', '2x', '4x', '8x']); osSeg.tip = 'Oversampling: each band is saturated at this multiple of the sample rate (less aliasing, a little latency; more CPU)';
  const autoBtn = ui.toggle('auto_gain', 'AUTO GAIN'); autoBtn.small = true; autoBtn.tip = 'Auto gain: keeps the level steady as you change drive (divides by the drive, then a slow level match)';
  const xoverSeg = ui.seg('xover_mode', ['ANALOG', 'LINEAR']); xoverSeg.tip = 'Crossover: Analog (Linkwitz-Riley, zero latency) or Linear phase (no phase shift, adds latency)';
  const bandsSeg = ui.seg('bands', ['1', '2', '3', '4']); bandsSeg.tip = 'Number of bands';
  const gmixDial = ui.dial('mix', 'MIX'); gmixDial.tip = 'Global mix: blends the whole processed signal with the clean one (latency-aligned)';
  const outDial = ui.dial('out_gain', 'OUTPUT', 'm', true); outDial.tip = 'Output gain';

  const bTop = () => disp.y + U.MB_STRIP + 22, bBot = () => disp.b - 46, pxPerDb = () => (bBot() - bTop()) / 24, yOfDrive = d => bBot() - d * pxPerDb();
  const mb = U.Multiband(ui, {
    main:b => bs(b, 'drive'), second:b => bs(b, 'mix'), mainName:'drive', secondName:'band mix', fields:FIELDS,
    stripValue:b => V(bs(b, 'bypass')) >= 0.5 ? 'BYP' : V(bs(b, 'drive')).toFixed(1) + ' dB',
    onSelect:() => rebind(),
    mainFromDrag:(b, start, up) => PumpFX.clamp('heat', bs(b, 'drive'), start + up / pxPerDb()),
    secondFromDrag:(b, start, up) => PumpFX.clamp('heat', bs(b, 'mix'), start + up * 0.5),
    mainWheel:(b, v, dir, fine) => v + dir * (fine ? 0.1 : 0.5),
    help:[['Band glow', 'how hard the band hits its curve (its level plus drive)']],
  });
  mb.panelFlags(soloBtn, muteBtn, bypBtn);
  function rebind(){
    const s = mb.sel = clamp(mb.sel, 0, mb.numBands() - 1);
    modeSeg.sym = bs(s, 'mode'); driveDial.sym = bs(s, 'drive'); mixDial.sym = bs(s, 'mix'); levelDial.sym = bs(s, 'level');
    soloBtn.sym = bs(s, 'solo'); muteBtn.sym = bs(s, 'mute'); bypBtn.sym = bs(s, 'bypass');
    driveDial.enabled = mixDial.enabled = levelDial.enabled = modeSeg.enabled = V(bs(s, 'bypass')) < 0.5; ui.dirty = true;
  }
  // 0..1: how hard the band drives its curve (RMS into the curve from -30 to -6 dBFS)
  const heat = b => (V(bs(b, 'bypass')) >= 0.5 || V(bs(b, 'mix')) < 0.5) ? 0 : clamp((inShow[b] + V(bs(b, 'drive')) + 30) / 24, 0, 1);
  const mixDeep = () => mix(ui.th.neon, ui.th.ink, 0.45);

  function drawBody(){
    const t = ui.th, g = ui.g; disp = mb.disp; ui.screenBox(disp); const S = U.MB_STRIP;
    ui.clip(R(disp.x + 1, disp.y + 1, disp.w - 2, disp.h - 2), () => {
      mb.drawUnder(); const nb = mb.numBands();
      for (let b = 0; b < nb; b++){ const [x0, x1] = mb.regionX(b), h = heat(b); if (h <= 0.01) continue;
        const top = disp.y + S + (1 - h) * (disp.h - S) * 0.55, gr = g.createLinearGradient(0, top, 0, disp.b);
        gr.addColorStop(0, css(alpha(t.neon, 0))); gr.addColorStop(1, css(alpha(t.neon, (t.dark ? 0.26 : 0.34) * (0.30 + 0.70 * h)))); g.fillStyle = gr; g.fillRect(x0, top, x1 - x0, disp.b - top); }
      const sp = R(disp.x, disp.y + S, disp.w, disp.h - S); ui.drawFreqGrid(sp, mb.LO, mb.HI);
      ui.drawSpectrum(pre, sp, -6, -96, alpha(t.ink, t.dark ? 0.07 : 0.05), alpha(t.ink, 0.25), mb.LO, mb.HI, 1.0);
      ui.drawSpectrum(post, sp, -6, -96, alpha(t.neon, t.dark ? 0.20 : 0.28), alpha(mixDeep(), 0.75), mb.LO, mb.HI, 1.1);
      ui.text(disp.r - 8, disp.y + S + 10, 'OUT', 'label', 9.5, mixDeep(), 'right', 1.2); ui.text(disp.r - 38, disp.y + S + 10, 'IN', 'label', 9.5, t.ink2, 'right', 1.2);
      for (let d = 0; d <= 24; d += 6) ui.line(disp.x, yOfDrive(d), disp.r, yOfDrive(d), t.grid, 1);
      for (let d = 6; d <= 24; d += 6) ui.text(disp.x + 5, yOfDrive(d) - 6, String(d), 'mono', 9, t.ink2, 'left');
      ui.text(disp.x + 5, yOfDrive(24) - 18, 'DRIVE dB', 'label', 9, t.ink2, 'left', 1);
      for (let b = 0; b < nb; b++){
        const [x0, x1] = mb.regionX(b); if (x1 - x0 < 8) continue;
        const drv = V(bs(b, 'drive')), bm = V(bs(b, 'mix')), mode = Math.round(V(bs(b, 'mode'))), isSel = b === mb.sel, byp = V(bs(b, 'bypass')) >= 0.5, yd = yOfDrive(drv);
        const bx0 = x0 + (b === 0 ? 36 : 8), bx1 = x1 - 8; if (bx1 - bx0 < 4) continue;
        const op = byp ? 0.08 : (0.12 + 0.38 * bm * 0.01) * (t.dark ? 0.8 : 1);
        ui.fill(R(bx0, yd, bx1 - bx0, bBot() - yd), alpha(t.neon, op)); ui.line(bx0, bBot(), bx1, bBot(), alpha(t.ink, 0.35), 1);
        ui.line(bx0, yd, bx1, yd, byp ? t.dim : isSel ? t.ink : alpha(t.ink, 0.75), isSel ? 2.4 : 1.6);
        if (bx1 - bx0 > 50){ const s = drv.toFixed(1), tw = ui.textW(s, 'mono', 10) + 10, tag = R(bx0 + 2, yd < bTop() + 18 ? yd + 4 : yd - 17, tw, 15);
          ui.fill(tag, isSel ? t.ink : t.bg); ui.stroke(tag, t.ink, 1); ui.text(tag.cx, tag.cy + 0.5, s, 'mono', 10, isSel ? t.on : t.ink, 'center');
          let m = byp ? 'BYPASS' : MODES[mode].toUpperCase(); if (!byp && bx1 - bx0 > 140) m += '  MIX ' + bm.toFixed(0) + '%';
          if (bx1 - bx0 > 90) ui.text(tag.r + 6, tag.cy + 0.5, m, 'label', 10, t.ink2, 'left', 0.8); }
      }
      mb.drawOver();
    });
    ui.drawLevelMeter(meterL, lvl[0], hold[0], -48, 6); ui.drawLevelMeter(meterR, lvl[1], hold[1], -48, 6);
    ui.text(meterL.r + 1, disp.b - 7, 'OUT', 'label', 9, t.ink2, 'center', 0.8);
    ui.stroke(bandPanel, t.dim, 1); ui.stroke(globalPanel, t.dim, 1);
    ui.caption(globalPanel.x + 12, globalPanel.y + 16, 'OVERSAMPLING'); ui.caption(globalPanel.x + 12, globalPanel.y + 66, 'CROSSOVER'); ui.caption(bandsSeg.r.x, globalPanel.y + 66, 'BANDS');
    drawReadout(); drawCurve();
    const lat = ui.lat | 0, sr = ui.sr || 48000;
    ui.caption(latInfo.x, latInfo.y + 6, 'LATENCY'); ui.text(latInfo.r, latInfo.y + 6, lat + ' smp', 'mono', 11, t.ink, 'right');
    ui.text(latInfo.r, latInfo.y + 24, (1000 * lat / sr).toFixed(2) + ' ms', 'mono', 10.5, t.ink2, 'right');
    ui.text(latInfo.r, latInfo.y + 42, (sr * (1 << clamp(Math.round(V('oversampling')), 0, 3)) / 1000).toFixed(0) + ' kHz inside', 'label', 10, t.ink2, 'right', 0.6);
  }
  function drawReadout(){
    const t = ui.th, r = readout, b = mb.sel;
    ui.caption(r.x, r.y + 4, 'INTO CURVE'); ui.text(r.r, r.y + 4, inShow[b] < -100 ? '--' : (inShow[b] + V(bs(b, 'drive'))).toFixed(1) + ' dB', 'mono', 11, t.ink, 'right');
    const hb = R(r.x, r.y + 16, r.w, 9), h = heat(b); ui.fill(hb, t.panel2); if (h > 0) ui.fill(R(hb.x, hb.y, hb.w * h, hb.h), mix(t.neon, t.red, h * h * 0.6)); ui.stroke(hb, t.ink, 1);
    ui.caption(r.x, r.y + 42, 'AUTO GAIN'); const ag = agShow[b] - V(bs(b, 'drive'));
    ui.text(r.r, r.y + 42, V('auto_gain') < 0.5 ? 'OFF' : (ag >= 0 ? '+' : '') + ag.toFixed(1) + ' dB', 'mono', 11, t.ink, 'right');
    ui.text(r.x, r.y + 62, mb.bandRange(b), 'label', 10.5, t.ink2, 'left', 0.6);
  }
  function drawCurve(){
    const t = ui.th, g = ui.g, r = inset, b = mb.sel, mode = Math.round(V(bs(b, 'mode')));
    ui.caption(r.x, r.y - 14, 'CURVE'); ui.text(r.r, r.y - 14, MODES[mode], 'label', 10.5, t.ink2, 'right', 1); ui.fill(r, t.screen);
    const cx = r.cx, cy = r.cy, sx = r.w * 0.5, sy = r.h * 0.5 / 1.25;
    for (const v of [-1, -0.5, 0.5, 1]){ ui.line(cx + v * sx, r.y, cx + v * sx, r.b, t.grid, 1); ui.line(r.x, cy - v * sy, r.r, cy - v * sy, t.grid, 1); }
    ui.line(r.x, cy, r.r, cy, alpha(t.ink, 0.3), 1); ui.line(cx, r.y, cx, r.b, alpha(t.ink, 0.3), 1);
    ui.line(r.x, r.b - (r.h - 2 * sy) * 0.5, r.r, r.y + (r.h - 2 * sy) * 0.5, alpha(t.ink, 0.18), 1);
    const gg = Math.pow(10, V(bs(b, 'drive')) / 20), ag = V('auto_gain') >= 0.5;
    if (inShow[b] > -100){ const a = Math.min(1, Math.pow(10, inShow[b] / 20) * Math.SQRT2); ui.fill(R(cx - a * sx, r.y + 1, 2 * a * sx, r.h - 2), alpha(t.neon, t.dark ? 0.14 : 0.22)); }
    ui.clip(r, () => { g.beginPath(); for (let i = 0; i <= 160; i++){ const x = -1 + 2 * i / 160; let y = curve(mode, gg * x); if (ag) y /= gg; const px = cx + x * sx, py = cy - y * sy; i ? g.lineTo(px, py) : g.moveTo(px, py); }
      g.strokeStyle = css(t.ink); g.lineWidth = 2; g.lineJoin = 'round'; g.stroke(); });
    ui.stroke(r, t.ink, 1.2); ui.text(r.r - 4, cy + 8, 'IN', 'label', 9, t.ink2, 'right', 1); ui.text(cx + 5, r.y + 8, 'OUT', 'label', 9, t.ink2, 'left', 1);
  }

  function layout(narrow){
    const W = ui.W; let y0, gx, gy;
    if (!narrow){
      disp = mb.disp = R(14, 58, W - 28 - 34, 322); meterL = R(disp.r + 12, disp.y, 9, disp.h - 18);
      y0 = 396; bandPanel = R(14, y0, 626, 192);
      bandSel.r = R(26, y0 + 12, 170, 26); soloBtn.r = R(206, y0 + 12, 58, 26); muteBtn.r = R(272, y0 + 12, 58, 26); bypBtn.r = R(338, y0 + 12, 66, 26);
      modeSeg.r = R(26, y0 + 50, 440, 26); driveDial.r = R(22, y0 + 86, 96, 100); mixDial.r = R(126, y0 + 92, 72, 84); levelDial.r = R(204, y0 + 92, 72, 84);
      readout = R(292, y0 + 96, 174, 80); inset = R(484, y0 + 34, 144, 144);
      globalPanel = R(654, y0, W - 668, 192); gx = 666; gy = y0;
    } else {
      disp = mb.disp = R(10, 94, W - 20 - 30, 290); meterL = R(disp.r + 8, disp.y, 9, disp.h - 18);
      y0 = 398; bandPanel = R(6, y0, W - 12, 286);
      bandSel.r = R(14, y0 + 12, 170, 26); soloBtn.r = R(192, y0 + 12, 58, 26); muteBtn.r = R(256, y0 + 12, 58, 26); bypBtn.r = R(320, y0 + 12, 66, 26);
      modeSeg.r = R(14, y0 + 50, W - 28, 26); driveDial.r = R(12, y0 + 86, 96, 100); mixDial.r = R(112, y0 + 92, 72, 84); levelDial.r = R(190, y0 + 92, 72, 84);
      readout = R(16, y0 + 196, 250, 80); inset = R(W - 14 - 182, y0 + 104, 182, 170);
      globalPanel = R(6, y0 + 298, W - 12, 192); gx = 18; gy = y0 + 298;
    }
    meterR = R(meterL.r + 2, disp.y, 9, disp.h - 18);
    const gw = globalPanel.r - 12 - gx;
    osSeg.r = R(gx, gy + 30, 180, 24); autoBtn.r = R(gx + 192, gy + 30, gw - 192, 24); xoverSeg.r = R(gx, gy + 80, 180, 24); bandsSeg.r = R(gx + 192, gy + 80, gw - 192, 24);
    gmixDial.r = R(gx - 4, gy + 110, 72, 80); outDial.r = R(gx + 74, gy + 110, 72, 80); latInfo = R(gx + 160, gy + 120, gw - 160, 60);
    mb.layoutStrip();
  }

  return {
    layout, drawBody, down:mb.down, drag:mb.drag, up:mb.up, hover:mb.hover, leave:mb.leave, wheel:mb.wheel, key:mb.key, keyUp:mb.keyUp,
    help:mb.help, blur:mb.blur, close:mb.close, geo:() => mb,   // (geo: geometry for tests; the kit ignores it)
    changed(s){ if (s === 'bands' || /^b\d_bypass$/.test(s)) rebind(); },
    reload(){ mb.reload(); rebind(); },
    frame(fr){
      const t = fr.t; pk[0] = Math.max(pk[0], t[1]); pk[1] = Math.max(pk[1], t[2]);
      for (let b = 0; b < NB; b++){ agT[b] = t[5 + b]; inT[b] = t[9 + b]; }
      for (const rg of fr.rings){ if (rg.k === 0) fPre.push(rg); else if (rg.k === 1) fPost.push(rg); }
      const n = U.analyzerSize(fr.sr || 48000); pre.setup(fr.sr, n); post.setup(fr.sr, n);
      const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      Spectrum.update(dt, pre, fPre, post, fPost);
    },
    tick(dt){
      for (let c = 0; c < 2; c++){ const p = U.dbOf(pk[c]); pk[c] = 0; lvl[c] = Math.max(p, lvl[c] - 26 * dt);
        if (p >= hold[c]){ hold[c] = p; holdT[c] = 1.5; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(p, hold[c] - 18 * dt); }
      const k = Math.min(1, dt * 10), stale = U.now() - ui.lastFrame > 0.5;
      for (let b = 0; b < NB; b++){ inShow[b] += ((stale ? -120 : inT[b]) - inShow[b]) * k; agShow[b] += (agT[b] - agShow[b]) * k; }
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100; },
    footer(){ const nb = mb.numBands(); return nb + ' BAND' + (nb > 1 ? 'S' : '') + '   ' + (Math.round(V('xover_mode')) ? 'LINEAR PHASE' : 'ANALOG') + '   ' + OS[clamp(Math.round(V('oversampling')), 0, 3)] + ' OVERSAMPLING   LATENCY ' + (ui.lat | 0); },
  };
}});
})();
