/* …pump BAND pop-up (stage 3): plugins/band/UIBand.cpp in the browser. The spectrum (in / out) behind the band regions,
   each band's threshold as a draggable line on the level scale with its detector level beside it, the GAIN lane on top
   with the live combined response (drag in it: the band's makeup), crossover handles and the S / M / B strip
   (multiband.js), the selected band's controls with its transfer curve, and the globals. */
(() => {
'use strict';
const U = window.PumpUI; if (!U || !U.Multiband) return;
const {R, clamp, css, mix, alpha, Dial, Feed, Spectrum} = U;
const NB = 4, LANE_KEY = 'thunderplus-pump-band-lane-v1', FIELDS = ['mode', 'thresh', 'ratio', 'attack', 'release', 'knee', 'range', 'makeup', 'solo', 'mute', 'bypass'];
const COMPRESS = 0, EXPAND = 1, UPWARD = 2;
// BandEngine.hpp staticGain: dB in -> gain change in dB (shared by the engine and the UI)
function staticGain(mode, x, thr, ratio, knee, range){
  const inf = ratio >= 19.999; let slope, d;
  if (mode === EXPAND){ slope = inf ? 100 : Math.max(ratio, 1) - 1; d = thr - x; }
  else if (mode === UPWARD){ slope = inf ? 1 : 1 - 1 / Math.max(ratio, 1); d = thr - x; }
  else { slope = inf ? 1 : 1 - 1 / Math.max(ratio, 1); d = x - thr; }
  let g; if (knee > 0 && Math.abs(d) <= 0.5 * knee) g = slope * (d + 0.5 * knee) * (d + 0.5 * knee) / (2 * knee); else g = d > 0 ? slope * d : 0;
  g = Math.min(g, Math.max(range, 0)); return mode === UPWARD ? g : -g;
}

U.define('band', {W:1000, H:640, nW:480, nH:940, bypass:true, back:8192, make(ui){
  const bs = (b, f) => 'b' + (b + 1) + '_' + f, V = s => ui.value(s);
  let meterL = R(), meterR = R(), bandPanel = R(), globalPanel = R(), grBar = R(), inset = R();
  const lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0], pk = [0, 0];
  const grShow = [0, 0, 0, 0], lvShow = [-120, -120, -120, -120], mkShow = [0, 0, 0, 0], grT = [0, 0, 0, 0], lvT = [-120, -120, -120, -120];
  let laneRange = (() => { try { const v = +localStorage.getItem(LANE_KEY); return [6, 12, 24, 48].includes(v) ? v : 6; } catch(e){ return 6; } })();
  const pre = new Spectrum(15, 22000), post = new Spectrum(15, 22000), fPre = new Feed(32768), fPost = new Feed(32768);
  let lastFrameT = 0, grabBand = -1, grabStart = 0, grabEff = 0, grabLastY = 0;

  // ---- the selected band (the plugin's constructor order)
  const bandSel = ui.menuBtn(null); bandSel.arrows = true;
  bandSel.itemsFn = () => { const v = []; for (let b = 0; b < mb.numBands(); b++) v.push('Band ' + (b + 1) + '  ' + mb.bandName(b) + '  (' + mb.bandRange(b) + ')'); return v; };
  bandSel.getFn = () => mb.sel; bandSel.setFn = i => mb.select(i); bandSel.textFn = () => 'BAND ' + (mb.sel + 1) + '  ' + mb.bandName(mb.sel);
  bandSel.tip = 'Selected band (side arrows, keys 1-4 or click a band in the display)';
  const modeSeg = ui.seg(null, ['COMPRESS', 'EXPAND', 'UPWARD']);
  modeSeg.tip = 'Compress: turn down above the threshold · Expand: turn down below it · Upward: lift quiet parts up toward it';
  const thrDial = ui.dial(null, 'THRESH'), ratioDial = ui.dial(null, 'RATIO'), kneeDial = ui.dial(null, 'KNEE'), rangeDial = ui.dial(null, 'RANGE');
  const atkDial = ui.dial(null, 'ATTACK'), relDial = ui.dial(null, 'RELEASE'), mkDial = ui.dial(null, 'MAKEUP', 'm', true);
  const dials = [thrDial, ratioDial, kneeDial, rangeDial, atkDial, relDial, mkDial]; dials.forEach(d => d.tabGroup = 1);
  thrDial.tip = 'Threshold: Compress acts above it, Expand and Upward below it (or drag the band’s line in the display)';
  ratioDial.tip = 'Ratio (20 = infinite: limit, gate or full lift) · Ctrl/Cmd + drag up/down in the band does the same';
  kneeDial.tip = 'Knee: how gradually the band starts to act around the threshold (dB)';
  atkDial.tip = 'Attack: how fast the band reacts as it gets louder'; relDial.tip = 'Release: how fast the band recovers as it gets quieter';
  mkDial.tip = 'Makeup gain for this band (or drag in the GAIN lane at the top of the display)';
  const soloBtn = ui.toggle(null, 'SOLO'), muteBtn = ui.toggle(null, 'MUTE'), bypBtn = ui.toggle(null, 'BYPASS');
  soloBtn.tip = 'Solo the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band · Alt-hold in the display)';
  muteBtn.tip = 'Mute the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)';
  bypBtn.tip = 'Bypass the selected band’s dynamics (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)';
  // ---- globals
  const xoverSeg = ui.seg('xover_mode', ['ANALOG', 'LINEAR']); xoverSeg.tip = 'Crossover: Analog (Linkwitz-Riley, zero latency) or Linear phase (no phase shift, adds latency)';
  const bandsSeg = ui.seg(null, ['1', '2', '3', '4']); bandsSeg.getFn = () => mb.numBands() - 1; bandsSeg.setFn = i => ui.setGesture('bands', i + 1); bandsSeg.tip = 'Number of bands';
  const detSeg = ui.seg('detection', ['PEAK', 'RMS']); detSeg.tip = 'Detection: Peak (fast, catches transients) or RMS (average level, smoother)';
  const autoBtn = ui.toggle('auto_makeup', 'AUTO MAKEUP'); autoBtn.small = true;
  autoBtn.tip = 'Auto makeup (Compress bands): adds back half of what the band’s curve takes off a -6 dBFS level; fixed, so it never pumps';
  const laDial = ui.dial('lookahead', 'LOOKAHEAD'); laDial.tip = 'Lookahead: the bands’ audio is delayed so the detectors see peaks coming (adds latency)';
  const linkDial = ui.dial('stereo_link', 'LINK'); linkDial.tip = 'Stereo link: 100% = both channels get the same gain, 0% = each channel on its own';
  const mixDial = ui.dial('mix', 'MIX'); mixDial.tip = 'Mix: parallel processing (each band blends with its own unprocessed signal, so no phase issues)';
  const outDial = ui.dial('out_gain', 'OUTPUT', 'm', true); outDial.tip = 'Output gain';
  const laneBtn = ui.button('GAIN ±' + laneRange, () => { const r = [6, 12, 24, 48]; laneRange = r[(Math.max(0, r.indexOf(laneRange)) + 1) % 4]; laneBtn.label = 'GAIN ±' + laneRange; try { localStorage.setItem(LANE_KEY, String(laneRange)); } catch(e){} });
  laneBtn.small = true; laneBtn.tip = 'Range of the GAIN lane (click to cycle ±6 / ±12 / ±24 / ±48 dB)';

  // ---- scales
  let disp = R();
  const laneTop = () => disp.y + U.MB_STRIP + 1, laneBot = () => disp.y + U.MB_STRIP + 76, laneZero = () => (laneTop() + laneBot()) / 2;
  const pxPerDbLane = () => (laneBot() - laneTop() - 12) * 0.5 / laneRange;
  const lvTop = () => laneBot() + 12, lvBot = () => disp.b - 46, pxPerDbLevel = () => (lvBot() - lvTop()) / 60, yOfLevel = db => lvTop() - db * pxPerDbLevel();

  const mb = U.Multiband(ui, {
    main:b => bs(b, 'thresh'), second:b => bs(b, 'ratio'), mainName:'threshold', secondName:'ratio', fields:FIELDS,
    stripValue:b => { if (V(bs(b, 'bypass')) >= 0.5) return 'BYP'; return Math.abs(grShow[b]) < 0.05 ? '0.0' : (grShow[b] > 0 ? '+' : '') + grShow[b].toFixed(1); },
    onSelect:() => rebind(),
    mainFromDrag:(b, start, up) => PumpFX.clamp('band', bs(b, 'thresh'), start + up / pxPerDbLevel()),
    mainWheel:(b, v, dir, fine) => v + dir * (fine ? 0.1 : 0.5),
    regionGrab:(b, x, y) => { if (y < laneTop() || y > laneBot()) return false; grabBand = b; grabStart = V(bs(b, 'makeup')); grabEff = 0; grabLastY = y; ui.begin(); return true; },
    regionGrabDrag:(x, y, m) => { if (grabBand < 0) return; grabEff += (grabLastY - y) * (m.shift ? 0.15 : 1); grabLastY = y; const s = bs(grabBand, 'makeup'); ui.setParam(s, grabStart + grabEff / pxPerDbLane()); ui.status = ui.paramStatus(s); },
    regionGrabEnd:() => { if (grabBand >= 0) ui.end(); grabBand = -1; },
    help:[['Drag in the GAIN lane', 'the band’s makeup gain (Shift: fine)'], ['GAIN ± key', 'range of the GAIN lane: 6 / 12 / 24 / 48 dB']],
  });
  mb.panelFlags(soloBtn, muteBtn, bypBtn);

  function rebind(){
    const s = mb.sel = clamp(mb.sel, 0, mb.numBands() - 1), bind = (c, f) => c.sym = bs(s, f);
    bind(modeSeg, 'mode'); bind(thrDial, 'thresh'); bind(ratioDial, 'ratio'); bind(kneeDial, 'knee'); bind(rangeDial, 'range');
    bind(atkDial, 'attack'); bind(relDial, 'release'); bind(mkDial, 'makeup'); bind(soloBtn, 'solo'); bind(muteBtn, 'mute'); bind(bypBtn, 'bypass');
    rangeDial.tip = Math.round(V(bs(s, 'mode'))) === UPWARD ? 'Range: the most this band may be lifted' : 'Range: the most this band may be turned down';
    const byp = V(bs(s, 'bypass')) >= 0.5; dials.forEach(d => d.enabled = !byp);
    ui.dirty = true;
  }

  // ---- drawing
  const mixDeep = () => mix(ui.th.neon, ui.th.ink, 0.45);
  function drawBody(){
    const t = ui.th, g = ui.g; disp = mb.disp; ui.screenBox(disp);
    ui.clip(R(disp.x + 1, disp.y + 1, disp.w - 2, disp.h - 2), () => {
      mb.drawUnder();
      const nb = mb.numBands(), e = mb.effXo(), sr = mb.rate();
      for (let db = 0; db >= -60; db -= 12){ const y = yOfLevel(db); ui.line(disp.x, y, disp.r, y, db === 0 ? alpha(t.ink, 0.18) : t.grid, 1); }
      ui.drawFreqGrid(R(disp.x, laneBot(), disp.w, disp.b - laneBot()), mb.LO, mb.HI);
      const sp = R(disp.x, lvTop(), disp.w, disp.b - lvTop()), dbBottom = -(disp.b - lvTop()) / pxPerDbLevel();
      ui.drawSpectrum(pre, sp, 0, dbBottom, alpha(t.ink, t.dark ? 0.08 : 0.05), alpha(t.ink, 0.22), mb.LO, mb.HI, 1.0);
      ui.drawSpectrum(post, sp, 0, dbBottom, alpha(t.neon, t.dark ? 0.24 : 0.34), alpha(mixDeep(), 0.75), mb.LO, mb.HI, 1.1);
      for (let db = -12; db >= -48; db -= 12) ui.text(disp.x + 5, yOfLevel(db) - 6, String(db), 'mono', 9, t.ink2, 'left');
      // per band: the zone it acts in, its threshold line, its detector level
      for (let b = 0; b < nb; b++){
        const [x0, x1] = mb.regionX(b); if (x1 - x0 < 8) continue;
        const mode = Math.round(V(bs(b, 'mode'))), thr = V(bs(b, 'thresh')), yt = yOfLevel(thr), isSel = b === mb.sel;
        const zt = mode === COMPRESS ? lvTop() : yt, zb = mode === COMPRESS ? yt : lvBot();
        if (zb - zt > 1){ const zc = css(alpha(t.second, t.dark ? 0.16 : 0.13)), z0 = css(alpha(t.second, 0)), gr = g.createLinearGradient(0, zt, 0, zb);
          gr.addColorStop(0, mode === COMPRESS ? z0 : zc); gr.addColorStop(1, mode === COMPRESS ? zc : z0); g.fillStyle = gr; g.fillRect(x0, zt, x1 - x0, zb - zt); }
        const lv = Math.max(-80, lvShow[b]), bar = R(x1 - 13, lvTop(), 6, lvBot() - lvTop());
        if (x1 - x0 > 30){ ui.fill(bar, alpha(t.ink, 0.06)); const yl = clamp(yOfLevel(lv), bar.y, bar.b), active = mode === COMPRESS ? lv > thr : lv < thr;
          ui.fill(R(bar.x, yl, bar.w, bar.b - yl), active ? t.second : alpha(mixDeep(), 0.8)); ui.stroke(bar, alpha(t.ink, 0.5), 1); }
        const lx0 = x0 + (b === 0 ? 28 : 4), lx1 = x1 - (x1 - x0 > 30 ? 17 : 4);
        ui.line(lx0, yt, lx1, yt, isSel ? t.ink : alpha(t.ink, 0.7), isSel ? 2.2 : 1.5);
        if (lx1 - lx0 > 50){ const s = thr.toFixed(1), tw = ui.textW(s, 'mono', 10) + 10, tag = R(lx0 + 2, yt - 17, tw, 15);
          ui.fill(tag, isSel ? t.ink : t.bg); ui.stroke(tag, t.ink, 1); ui.text(tag.cx, tag.cy + 0.5, s, 'mono', 10, isSel ? t.on : t.ink, 'center');
          if (lx1 - lx0 > 130){ const ra = V(bs(b, 'ratio')), mn = ['COMP', 'EXP', 'UP'][mode]; ui.text(tag.r + 6, tag.cy + 0.5, ra >= 19.99 ? mn + ' INF' : mn + ' ' + ra.toFixed(1) + ':1', 'label', 10, t.ink2, 'left', 0.8); } }
      }
      // the GAIN lane: the combined response, sum of band magnitudes x band gains
      const lane = R(disp.x, laneTop(), disp.w, laneBot() - laneTop()); ui.fill(lane, alpha(t.panel, t.dark ? 0.55 : 0.65)); ui.line(disp.x, laneBot(), disp.r, laneBot(), t.dim, 1);
      const y0 = laneZero(), half = laneRange * 0.5 * pxPerDbLane();
      ui.line(disp.x, y0 - half, disp.r, y0 - half, t.grid, 1); ui.line(disp.x, y0 + half, disp.r, y0 + half, t.grid, 1); ui.line(disp.x, y0, disp.r, y0, alpha(t.ink, 0.30), 1);
      const mx = V('mix') * 0.01, gl = [];
      for (let b = 0; b < NB; b++){ const w = V(bs(b, 'bypass')) >= 0.5 ? 1 : Math.pow(10, (grShow[b] + mkShow[b]) / 20); gl[b] = 1 + mx * (w - 1); }
      const NP = 240, xs = new Float32Array(NP), ys = new Float32Array(NP);
      for (let p = 0; p < NP; p++){ xs[p] = disp.x + disp.w * p / (NP - 1); const f = mb.fOf(xs[p]); let gg = 0; for (let b = 0; b < nb; b++) gg += mb.bandMag(nb, e, b, f, sr) * gl[b];
        ys[p] = clamp(y0 - 20 * Math.log10(Math.max(gg, 1e-6)) * pxPerDbLane(), lane.y + 2, lane.b - 2); }
      const fillSide = (fn, c) => { g.beginPath(); g.moveTo(xs[0], y0); for (let p = 0; p < NP; p++) g.lineTo(xs[p], fn(ys[p], y0)); g.lineTo(xs[NP - 1], y0); g.closePath(); g.fillStyle = css(c); g.fill(); };
      fillSide(Math.max, alpha(t.second, t.dark ? 0.40 : 0.32)); fillSide(Math.min, alpha(t.neon, t.dark ? 0.55 : 0.75));
      g.beginPath(); for (let p = 0; p < NP; p++) p ? g.lineTo(xs[p], ys[p]) : g.moveTo(xs[p], ys[p]); g.strokeStyle = css(t.ink); g.lineWidth = 1.6; g.lineJoin = 'round'; g.stroke();
      ui.text(disp.x + 6, y0 - half, '+' + (laneRange / 2 | 0), 'mono', 9, t.ink2, 'left'); ui.text(disp.x + 6, y0 + half, '-' + (laneRange / 2 | 0), 'mono', 9, t.ink2, 'left');
      mb.drawOver();
    });
    ui.drawLevelMeter(meterL, lvl[0], hold[0], -48, 6); ui.drawLevelMeter(meterR, lvl[1], hold[1], -48, 6);
    ui.text(meterL.r + 1, disp.b - 7, 'OUT', 'label', 9, t.ink2, 'center', 0.8);
    ui.stroke(bandPanel, t.dim, 1); ui.stroke(globalPanel, t.dim, 1);
    drawBandExtras();
    ui.caption(globalPanel.x + 12, globalPanel.y + 16, 'CROSSOVER'); ui.caption(bandsSeg.r.x, globalPanel.y + 16, 'BANDS'); ui.caption(globalPanel.x + 12, globalPanel.y + 66, 'DETECTION');
  }
  function drawBandExtras(){
    const t = ui.th, g = ui.g, b = mb.sel, gg = grShow[b] + mkShow[b];
    ui.fill(grBar, t.panel2); const cx = grBar.cx, w = Math.min(1, Math.abs(gg) / 24) * grBar.w * 0.5;
    if (w > 0.5) ui.fill(R(gg < 0 ? cx - w : cx, grBar.y, w, grBar.h), gg < 0 ? t.second : t.neon);
    ui.line(cx, grBar.y - 2, cx, grBar.b + 2, t.ink, 1.2); ui.stroke(grBar, t.ink, 1);
    ui.text(grBar.r, grBar.y - 10, (Math.abs(gg) < 0.05 ? '+0.0' : (gg > 0 ? '+' : '') + gg.toFixed(1)) + ' dB', 'mono', 10.5, t.ink, 'right');
    ui.caption(grBar.x, grBar.y - 10, 'GAIN'); ui.text(grBar.x + 40, grBar.y - 10, mb.bandRange(b), 'label', 10.5, t.ink2, 'left', 0.6);
    // transfer curve: input level -> output level (-60..0 dBFS), with the live detector level as a dot
    const r = inset; ui.caption(r.x, r.y - 14, 'CURVE'); ui.fill(r, t.screen);
    for (let d = -12; d > -60; d -= 12){ const u = (d + 60) / 60; ui.line(r.x + u * r.w, r.y, r.x + u * r.w, r.b, t.grid, 1); ui.line(r.x, r.b - u * r.h, r.r, r.b - u * r.h, t.grid, 1); }
    ui.line(r.x, r.b, r.r, r.y, alpha(t.ink, 0.25), 1);
    const mode = Math.round(V(bs(b, 'mode'))), thr = V(bs(b, 'thresh')), ra = V(bs(b, 'ratio')), kn = V(bs(b, 'knee')), rg = V(bs(b, 'range')), mk = mkShow[b];
    const px = i => r.x + (i + 60) / 60 * r.w, py = o => clamp(r.b - (o + 60) / 60 * r.h, r.y, r.b);
    ui.line(px(thr), r.y, px(thr), r.b, alpha(t.second, 0.6), 1);
    ui.clip(r, () => {
      g.beginPath(); for (let i = 0; i <= 120; i++){ const inp = -60 + 60 * i / 120, o = inp + staticGain(mode, inp, thr, ra, kn, rg) + mk; i ? g.lineTo(px(inp), py(o)) : g.moveTo(px(inp), py(o)); }
      g.strokeStyle = css(t.ink); g.lineWidth = 2; g.lineJoin = 'round'; g.stroke();
      const lv = lvShow[b]; if (lv > -60){ const o = lv + staticGain(mode, lv, thr, ra, kn, rg) + mk; g.beginPath(); g.arc(px(lv), py(o), 4.5, 0, 7); g.fillStyle = css(t.neon); g.fill(); g.strokeStyle = css(t.ink); g.lineWidth = 1.5; g.stroke(); }
    });
    ui.stroke(r, t.ink, 1.2); ui.text(r.r - 4, r.b - 8, 'IN', 'label', 9, t.ink2, 'right', 1); ui.text(r.x + 4, r.y + 8, 'OUT', 'label', 9, t.ink2, 'left', 1);
  }

  // ---- layout (desktop: the plugin's; narrow: the same parts stacked for a folded phone)
  function layout(narrow){
    const W = ui.W;
    if (!narrow){
      disp = mb.disp = R(14, 58, W - 28 - 34, 334);
      meterL = R(disp.r + 12, disp.y, 9, disp.h - 18); meterR = R(meterL.r + 2, disp.y, 9, disp.h - 18);
      const y0 = 408; bandPanel = R(14, y0, 626, 192);
      bandSel.r = R(26, y0 + 12, 170, 26); modeSeg.r = R(206, y0 + 12, 254, 26);
      dials.forEach((d, i) => d.r = R(22 + 63 * i, y0 + 50, 62, 84));
      soloBtn.r = R(26, y0 + 150, 58, 26); muteBtn.r = R(92, y0 + 150, 58, 26); bypBtn.r = R(158, y0 + 150, 66, 26);
      grBar = R(242, y0 + 162, 222, 12); inset = R(484, y0 + 34, 144, 144);
      globalPanel = R(654, y0, W - 668, 192); const gx = 666;
      xoverSeg.r = R(gx, y0 + 30, 170, 24); bandsSeg.r = R(gx + 182, y0 + 30, 126, 24); detSeg.r = R(gx, y0 + 80, 120, 24); autoBtn.r = R(gx + 182, y0 + 80, 126, 24);
      [laDial, linkDial, mixDial, outDial].forEach((d, i) => d.r = R(gx - 4 + 80 * i, y0 + 110, 72, 80));
    } else {
      disp = mb.disp = R(10, 94, W - 20 - 30, 320);
      meterL = R(disp.r + 8, disp.y, 9, disp.h - 18); meterR = R(meterL.r + 2, disp.y, 9, disp.h - 18);
      const y0 = 430; bandPanel = R(6, y0, W - 12, 272);
      bandSel.r = R(14, y0 + 10, 170, 26); modeSeg.r = R(192, y0 + 10, W - 206, 26);
      dials.forEach((d, i) => d.r = R(12 + 66 * i, y0 + 46, 62, 84));
      soloBtn.r = R(14, y0 + 144, 58, 26); muteBtn.r = R(80, y0 + 144, 58, 26); bypBtn.r = R(146, y0 + 144, 66, 26);
      grBar = R(14, y0 + 214, 318, 12); inset = R(W - 14 - 108, y0 + 156, 108, 108);
      const y1 = y0 + 284; globalPanel = R(6, y1, W - 12, 196); const gx = 18;
      xoverSeg.r = R(gx, y1 + 30, 210, 24); bandsSeg.r = R(gx + 226, y1 + 30, W - 18 - gx - 226, 24); detSeg.r = R(gx, y1 + 80, 150, 24); autoBtn.r = R(gx + 226, y1 + 80, W - 18 - gx - 226, 24);
      [laDial, linkDial, mixDial, outDial].forEach((d, i) => d.r = R(gx + 20 + 111 * i, y1 + 112, 72, 80));
    }
    laneBtn.r = R(disp.r - 82, laneTop() + 5, 74, 18);
    mb.layoutStrip();
  }

  return {
    layout, drawBody, down:mb.down, drag:mb.drag, up:mb.up, hover:mb.hover, leave:mb.leave, wheel:mb.wheel, key:mb.key, keyUp:mb.keyUp,
    help:mb.help, blur:mb.blur, close:mb.close, geo:() => mb,   // (geo: geometry for tests; the kit ignores it)
    changed(s){ if (s === 'bands' || /^b\d_(bypass|mode)$/.test(s)) rebind(); },
    reload(){ mb.reload(); rebind(); },
    frame(fr){
      const t = fr.t; pk[0] = Math.max(pk[0], t[1]); pk[1] = Math.max(pk[1], t[2]);
      for (let b = 0; b < NB; b++){ grT[b] = t[5 + b]; lvT[b] = t[9 + b]; mkShow[b] = t[13 + b]; }
      for (const rg of fr.rings){ if (rg.k === 0) fPre.push(rg); else if (rg.k === 1) fPost.push(rg); }
      const n = U.analyzerSize(fr.sr || 48000); pre.setup(fr.sr, n); post.setup(fr.sr, n);
      const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      Spectrum.update(dt, pre, fPre, post, fPost);
    },
    tick(dt){
      for (let c = 0; c < 2; c++){ const p = U.dbOf(pk[c]); pk[c] = 0; lvl[c] = Math.max(p, lvl[c] - 26 * dt);
        if (p >= hold[c]){ hold[c] = p; holdT[c] = 1.5; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(p, hold[c] - 18 * dt); }
      const k = Math.min(1, dt * 18), kl = Math.min(1, dt * 8);
      for (let b = 0; b < NB; b++){ grShow[b] += (grT[b] - grShow[b]) * k; const l = lvT[b]; lvShow[b] = l > lvShow[b] ? l : lvShow[b] + (l - lvShow[b]) * kl; }
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100; },
    footer(){ const nb = mb.numBands(), ms = ui.lat / (ui.sr || 48000) * 1000; return nb + ' BAND' + (nb > 1 ? 'S' : '') + '   ' + (Math.round(V('xover_mode')) ? 'LINEAR PHASE' : 'ANALOG') + '   LATENCY ' + (ui.lat | 0) + ' (' + ms.toFixed(1) + ' ms)'; },
  };
}});
})();
