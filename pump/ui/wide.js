/* …pump WIDE pop-up (stage 3): plugins/wide/UIWide.cpp in the browser. Left: mid (fill) and side (line) spectra behind
   the band regions, each band's width as a draggable bar (100% = unchanged) with its stereoize amount, the mono-bass
   line, crossover handles and the S / M / B strip (multiband.js). Right: the polar-sample vectorscope (half disc,
   mono straight up, L and R on the diagonals, out-of-phase content toward the flat edge) with phosphor
   persistence, and the correlation meters, overall (with its lowest hold) and per band. */
(() => {
'use strict';
const U = window.PumpUI; if (!U || !U.Multiband) return;
const {R, clamp, css, mix, alpha, Feed, Spectrum} = U;
const NB = 4, FIELDS = ['width', 'stereoize', 'solo', 'mute', 'bypass'];

U.define('wide', {W:1000, H:620, nW:480, nH:974, bypass:true, back:8192, make(ui){
  const bs = (b, f) => 'b' + (b + 1) + '_' + f, V = s => ui.value(s);
  let scope = R(), corr = R(), bandPanel = R(), globalPanel = R(), bandInfo = R(), disp = R();
  const corrShow = [0, 0, 0, 0], lvShow = [-120, -120, -120, -120], corrT = [0, 0, 0, 0], lvT = [-120, -120, -120, -120];
  let corrAllShow = 0, corrAllT = 0, lvAll = -120, lvAllT = -120, corrMin = 1, corrMinT = 0, lastFrameT = 0;
  const mid = new Spectrum(15, 22000), side = new Spectrum(15, 22000), fMid = new Feed(32768), fSide = new Feed(32768), fL = new Feed(16384), fR = new Feed(16384);
  // the phosphor image: 2x the scope's logical size, splatted with new samples and decayed every frame
  let scopeT = 0, imgW = 0, imgH = 0, fb = null, off = null, octx = null, idata = null, scopePos = -1, fbMax = 0, imgColor = '';
  let grabMono = false, grabStart = 0, grabEff = 0, grabLastX = 0;

  const bandSel = ui.menuBtn(null); bandSel.arrows = true;
  bandSel.itemsFn = () => { const v = []; for (let b = 0; b < mb.numBands(); b++) v.push('Band ' + (b + 1) + '  ' + mb.bandName(b) + '  (' + mb.bandRange(b) + ')'); return v; };
  bandSel.getFn = () => mb.sel; bandSel.setFn = i => mb.select(i); bandSel.textFn = () => 'BAND ' + (mb.sel + 1) + '  ' + mb.bandName(mb.sel);
  bandSel.tip = 'Selected band (side arrows, keys 1-4 or click a band in the display)';
  const widthDial = ui.dial(null, 'WIDTH', 'l'); widthDial.tip = 'Width: 0% = mono, 100% = unchanged, 200% = twice the side (or drag the band’s bar in the display)';
  const stDial = ui.dial(null, 'STEREOIZE', 'l'); stDial.tip = 'Stereoize: adds a decorrelated copy of the band’s mid to its side; cancels in mono (Ctrl/Cmd + drag in the band)';
  widthDial.tabGroup = stDial.tabGroup = 1;
  const soloBtn = ui.toggle(null, 'SOLO'), muteBtn = ui.toggle(null, 'MUTE'), bypBtn = ui.toggle(null, 'BYPASS');
  soloBtn.tip = 'Solo the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band · Alt-hold in the display)';
  muteBtn.tip = 'Mute the selected band (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)';
  bypBtn.tip = 'Bypass the selected band’s width and stereoize (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)';
  const xoverSeg = ui.seg('xover_mode', ['ANALOG', 'LINEAR']); xoverSeg.tip = 'Crossover: Analog (Linkwitz-Riley, zero latency) or Linear phase (no phase shift, adds latency)';
  const bandsSeg = ui.seg('bands', ['1', '2', '3', '4']); bandsSeg.tip = 'Number of bands';
  const monoBtn = ui.toggle('mono_bass', 'MONO BASS'); monoBtn.small = true; monoBtn.tip = 'Mono bass: removes the side below the frequency (phase-coherent Linkwitz-Riley split)';
  const slopeSeg = ui.seg('mono_slope', ['24 dB', '48 dB']); slopeSeg.vertical = true; slopeSeg.tip = 'Mono bass slope: 24 dB/oct (LR4) or 48 dB/oct (LR8)';
  const monoDial = ui.dial('mono_freq', 'MONO FREQ'); monoDial.tip = 'Mono bass frequency: the side is removed below it (drag the dashed line in the display)';
  const balDial = ui.dial('balance', 'BALANCE', 'm', true); balDial.tip = 'Balance: turns the opposite channel down';
  const outDial = ui.dial('out_gain', 'OUTPUT', 'm', true); outDial.tip = 'Output gain';

  const wTop = () => disp.y + U.MB_STRIP + 16, wBot = () => disp.b - 52, pxPerPct = () => (wBot() - wTop()) / 200, yOfWidth = p => wBot() - p * pxPerPct();
  const mb = U.Multiband(ui, {
    main:b => bs(b, 'width'), second:b => bs(b, 'stereoize'), mainName:'width', secondName:'stereoize', fields:FIELDS,
    stripValue:b => V(bs(b, 'bypass')) >= 0.5 ? 'BYP' : V(bs(b, 'width')).toFixed(0) + '%',
    onSelect:() => rebind(),
    mainFromDrag:(b, start, up) => PumpFX.clamp('wide', bs(b, 'width'), start + up / pxPerPct()),
    secondFromDrag:(b, start, up) => PumpFX.clamp('wide', bs(b, 'stereoize'), start + up * 0.5),
    mainWheel:(b, v, dir, fine) => v + dir * (fine ? 1 : 5),
    regionGrab:(b, x, y) => { if (V('mono_bass') < 0.5 || Math.abs(x - mb.xOf(V('mono_freq'))) > (ui.narrow ? 9 : 5) || y < wTop()) return false;
      grabMono = true; grabStart = V('mono_freq'); grabEff = 0; grabLastX = x; ui.begin(); return true; },
    regionGrabDrag:(x, y, m) => { if (!grabMono) return; grabEff += (x - grabLastX) * (m.shift ? 0.15 : 1); grabLastX = x;
      ui.setParam('mono_freq', grabStart * Math.pow(mb.HI / mb.LO, grabEff / disp.w)); ui.status = ui.paramStatus('mono_freq'); },
    regionGrabEnd:() => { if (grabMono) ui.end(); grabMono = false; },
    help:[['Drag the dashed MONO line', 'mono bass frequency (when MONO BASS is on)'],
      ['Vectorscope', 'mono straight up · L / R on the diagonals · out of phase: toward the flat edge'],
      ['Correlation', '+1 mono · 0 wide · below 0 (red): phase trouble, it will thin out in mono']],
  });
  mb.panelFlags(soloBtn, muteBtn, bypBtn);
  function rebind(){
    const s = mb.sel = clamp(mb.sel, 0, mb.numBands() - 1);
    widthDial.sym = bs(s, 'width'); stDial.sym = bs(s, 'stereoize'); soloBtn.sym = bs(s, 'solo'); muteBtn.sym = bs(s, 'mute'); bypBtn.sym = bs(s, 'bypass');
    widthDial.enabled = stDial.enabled = V(bs(s, 'bypass')) < 0.5; monoDial.enabled = slopeSeg.enabled = V('mono_bass') >= 0.5; ui.dirty = true;
  }

  // ---- the vectorscope image
  function scopeSetup(){
    const w = Math.round(scope.w * 2), h = Math.round(scope.h * 2); if (w === imgW && h === imgH && fb) return;
    imgW = w; imgH = h; fb = new Float32Array(w * h); off = document.createElement('canvas'); off.width = w; off.height = h; octx = off.getContext('2d'); idata = octx.createImageData(w, h); fbMax = 0;
  }
  const radiusImg = () => Math.min(imgW * 0.5 - 36, imgH - 44 - 52);
  function scopeTick(dt){
    if (!fb) return; const decay = Math.exp(-dt / 0.16), was = fbMax;
    if (fbMax > 1e-3){ for (let i = 0; i < fb.length; i++) fb[i] *= decay; fbMax *= decay; } else if (fbMax > 0){ fb.fill(0); fbMax = 0; }
    if (fL.any && fR.any){
      const pos = Math.min(fL.pos, fR.pos) >>> 0; if (scopePos < 0) scopePos = (pos - 2048) >>> 0;
      let n = (pos - scopePos) >>> 0; if (n > 6000){ scopePos = (pos - 6000) >>> 0; n = 6000; }
      const cx = imgW * 0.5, cy = imgH - 44, Rr = radiusImg();
      for (let k = 0; k < n; k++){
        const l = fL.at(scopePos + k), r = fR.at(scopePos + k); let m = 0.5 * (l + r), s = 0.5 * (l - r);
        if (m < 0){ m = -m; s = -s; } const a = Math.sqrt(m * m + s * s); if (a < 1e-5) continue;
        const rd = Rr * Math.sqrt(Math.min(1, a)), x = (cx - rd * s / a) | 0, y = (cy - rd * m / a) | 0;
        for (let dy = 0; dy < 2; dy++) for (let dx = 0; dx < 2; dx++){ const xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= imgW || yy >= imgH) continue;
          const i = yy * imgW + xx, v = Math.min(1, fb[i] + 0.22); fb[i] = v; if (v > fbMax) fbMax = v; } }
      scopePos = pos;
    }
    if (was <= 0 && fbMax <= 0) return;
    const t = ui.th, c = t.dark ? t.neon : mix(t.neon, t.ink, 0.55), d = idata.data, cr = c[0] | 0, cg = c[1] | 0, cb = c[2] | 0;
    for (let i = 0, j = 0; i < fb.length; i++, j += 4){ d[j] = cr; d[j + 1] = cg; d[j + 2] = cb; d[j + 3] = 255 * Math.sqrt(fb[i]); }
    octx.putImageData(idata, 0, 0);
  }

  // ---- drawing
  const mixDeep = () => mix(ui.th.neon, ui.th.ink, 0.45);
  const corrColor = c => { const t = ui.th; if (c < 0) return t.red; return c < 0.25 ? mix(t.red, t.dark ? t.neon : mixDeep(), c / 0.25) : (t.dark ? t.neon : mixDeep()); };
  function drawBody(){
    const t = ui.th, g = ui.g; disp = mb.disp; ui.screenBox(disp);
    ui.clip(R(disp.x + 1, disp.y + 1, disp.w - 2, disp.h - 2), () => {
      mb.drawUnder(); const nb = mb.numBands(), S = U.MB_STRIP, sp = R(disp.x, disp.y + S, disp.w, disp.h - S);
      ui.drawFreqGrid(sp, mb.LO, mb.HI);
      ui.drawSpectrum(mid, sp, -6, -96, alpha(t.neon, t.dark ? 0.22 : 0.30), alpha(mixDeep(), 0.7), mb.LO, mb.HI, 1.0);
      ui.drawSpectrum(side, sp, -6, -96, alpha(t.second, t.dark ? 0.06 : 0.05), alpha(t.second, t.dark ? 0.65 : 0.60), mb.LO, mb.HI, 1.0);
      ui.text(disp.r - 8, disp.y + S + 10, 'MID', 'label', 9.5, mixDeep(), 'right', 1.2); ui.text(disp.r - 40, disp.y + S + 10, 'SIDE', 'label', 9.5, t.second, 'right', 1.2);
      const y100 = yOfWidth(100);
      for (const p of [0, 50, 150, 200]) ui.line(disp.x, yOfWidth(p), disp.r, yOfWidth(p), t.grid, 1);
      for (let x = disp.x; x < disp.r; x += 8) ui.line(x, y100, Math.min(disp.r, x + 4), y100, alpha(t.ink, 0.35), 1);
      ui.text(disp.x + 6, yOfWidth(200) - 7, '200%', 'mono', 9, t.ink2, 'left'); ui.text(disp.x + 6, y100 - 7, '100%', 'mono', 9, t.ink2, 'left'); ui.text(disp.x + 6, yOfWidth(0) - 7, 'MONO', 'label', 9, t.ink2, 'left', 1);
      if (V('mono_bass') >= 0.5){ const xm = mb.xOf(V('mono_freq'));
        ui.fill(R(disp.x, disp.y + S, xm - disp.x, disp.h - S), alpha(t.ink, t.dark ? 0.10 : 0.06));
        for (let y = disp.y + S; y < disp.b; y += 7) ui.line(xm, y, xm, Math.min(disp.b, y + 4), t.ink, 1.5);
        const s = 'MONO < ' + ui.fmt('mono_freq'), tw = ui.textW(s, 'label', 10, 0.8) + 12, tag = R(Math.max(disp.x + 4, Math.min(disp.r - tw - 4, xm + 5)), disp.y + S + 5, tw, 16);
        ui.fill(tag, t.ink); ui.text(tag.cx, tag.cy + 0.5, s, 'label', 10, t.on, 'center', 0.8); }
      for (let b = 0; b < nb; b++){
        const [x0, x1] = mb.regionX(b); if (x1 - x0 < 8) continue;
        const w = V(bs(b, 'width')), st = V(bs(b, 'stereoize')), isSel = b === mb.sel, byp = V(bs(b, 'bypass')) >= 0.5, yw = yOfWidth(byp ? 100 : w);
        const bx0 = x0 + (b === 0 ? 40 : 6), bx1 = x1 - 6; if (bx1 - bx0 < 4) continue;
        if (Math.abs(yw - y100) > 0.5) ui.fill(R(bx0, Math.min(yw, y100), bx1 - bx0, Math.abs(yw - y100)), yw < y100 ? alpha(t.neon, t.dark ? 0.30 : 0.42) : alpha(t.second, t.dark ? 0.26 : 0.20));
        if (st > 0.5 && !byp){ const h = st * 0.01 * 28; ui.clip(R(bx0, yw - h, bx1 - bx0, h), () => { for (let x = bx0 - h; x < bx1; x += 6) ui.line(x, yw, x + h, yw - h, alpha(t.second, 0.7), 1.2); }); }
        ui.line(bx0, yw, bx1, yw, isSel ? t.ink : alpha(t.ink, 0.75), isSel ? 2.4 : 1.6);
        if (bx1 - bx0 > 46){ const s = (byp ? 100 : w).toFixed(0) + '%', tw = ui.textW(s, 'mono', 10) + 10, below = (st > 0.5 && !byp) || yw < wTop() + 20, tag = R(bx0 + 2, below ? yw + 4 : yw - 17, tw, 15);
          ui.fill(tag, isSel ? t.ink : t.bg); ui.stroke(tag, t.ink, 1); ui.text(tag.cx, tag.cy + 0.5, s, 'mono', 10, isSel ? t.on : t.ink, 'center');
          if (st > 0.5 && bx1 - bx0 > 110) ui.text(tag.r + 6, tag.cy + 0.5, 'ST ' + st.toFixed(0) + '%', 'label', 10, t.second, 'left', 0.8); }
      }
      mb.drawOver();
    });
    drawScope(); drawCorrelation();
    ui.stroke(bandPanel, t.dim, 1); ui.stroke(globalPanel, t.dim, 1);
    ui.caption(globalPanel.x + 12, globalPanel.y + 16, 'CROSSOVER'); ui.caption(bandsSeg.r.x, globalPanel.y + 16, 'BANDS'); ui.caption(monoBtn.r.x, globalPanel.y + 16, 'LOW END'); ui.caption(slopeSeg.r.x, slopeSeg.r.y - 10, 'SLOPE');
    drawBandInfo();
  }
  function drawScope(){
    const t = ui.th, g = ui.g; ui.screenBox(scope); scopeSetup();
    // the phosphor image moves on when the window paints (at most 25 times a second), not every animation frame
    const tn = performance.now(); scopeTick(scopeT ? clamp((tn - scopeT) / 1000, 0.001, 0.25) : 1 / 25); scopeT = tn;
    const cx = scope.cx, cy = scope.b - 22, Rr = radiusImg() * 0.5, d = Rr * Math.SQRT1_2;
    g.beginPath(); g.arc(cx, cy, Rr, Math.PI, 0, false); g.strokeStyle = css(alpha(t.ink, 0.45)); g.lineWidth = 1.2; g.stroke();
    g.beginPath(); g.arc(cx, cy, Rr * 0.5, Math.PI, 0, false); g.strokeStyle = css(t.grid); g.lineWidth = 1; g.stroke();
    ui.line(cx - Rr - 6, cy, cx + Rr + 6, cy, alpha(t.ink, 0.45), 1.2);
    ui.line(cx, cy, cx, cy - Rr, t.grid, 1); ui.line(cx, cy, cx - d, cy - d, t.grid, 1); ui.line(cx, cy, cx + d, cy - d, t.grid, 1);
    ui.text(cx - d - 9, cy - d - 9, 'L', 'bold', 11, t.ink2, 'center'); ui.text(cx + d + 9, cy - d - 9, 'R', 'bold', 11, t.ink2, 'center'); ui.text(cx, cy - Rr - 10, 'M', 'bold', 11, t.ink2, 'center');
    ui.text(cx - Rr - 4, cy + 11, '-S', 'mono', 9.5, t.red, 'left'); ui.text(cx + Rr + 4, cy + 11, '+S', 'mono', 9.5, t.red, 'right');
    ui.caption(scope.x + 10, scope.y + 12, 'VECTORSCOPE');
    if (off && fbMax > 0) ui.clip(R(scope.x + 1, scope.y + 1, scope.w - 2, scope.h - 2), () => { g.imageSmoothingEnabled = true; g.drawImage(off, scope.x, scope.y, scope.w, scope.h); });
  }
  function corrMeter(r, c, big, silent){
    const t = ui.th; ui.fill(r, t.panel2); const zx = r.cx;
    if (!silent){ const x = r.x + (c + 1) * 0.5 * r.w; ui.fill(R(Math.min(zx, x), r.y, Math.abs(x - zx), r.h), corrColor(c)); ui.line(x, r.y - (big ? 3 : 1), x, r.b + (big ? 3 : 1), t.ink, big ? 2 : 1.4); }
    ui.line(zx, r.y, zx, r.b, alpha(t.ink, 0.6), 1); ui.stroke(r, t.ink, 1);
  }
  const cfmt = c => (c >= 0 ? '+' : '') + c.toFixed(2);
  function drawCorrelation(){
    const t = ui.th; ui.screenBox(corr); ui.caption(corr.x + 10, corr.y + 12, 'CORRELATION');
    const silent = lvAll < -90;
    ui.text(corr.r - 10, corr.y + 12, silent ? '--' : cfmt(corrAllShow), 'bold', 13, silent ? t.ink2 : corrAllShow < 0 ? t.red : t.ink, 'right');
    const big = R(corr.x + 28, corr.y + 26, corr.w - 56, 14); corrMeter(big, corrAllShow, true, silent);
    if (!silent && corrMin < 0){ const x = big.x + (corrMin + 1) * 0.5 * big.w; ui.line(x, big.y - 2, x, big.b + 2, t.red, 1.5); }
    ui.text(big.x - 6, big.cy, '-1', 'mono', 9, t.ink2, 'right'); ui.text(big.r + 6, big.cy, '+1', 'mono', 9, t.ink2, 'left');
    const nb = mb.numBands(), step = ui.narrow ? Math.min(28, (corr.h - 62) / 4) : 14;
    for (let b = 0; b < nb; b++){ const y = corr.y + 52 + step * b, bsil = lvShow[b] < -90;
      ui.text(corr.x + 14, y + 3, String(b + 1), 'bold', 9.5, b === mb.sel ? t.ink : t.ink2, 'center');
      corrMeter(R(corr.x + 28, y, corr.w - 90, 7), corrShow[b], false, bsil);
      ui.text(corr.r - 12, y + 3.5, bsil ? '--' : cfmt(corrShow[b]), 'mono', 9.5, bsil ? t.ink2 : corrShow[b] < 0 ? t.red : t.ink, 'right'); }
  }
  function drawBandInfo(){
    const t = ui.th, g = ui.g, r = bandInfo, b = mb.sel;
    ui.caption(r.x, r.y + 4, 'BAND ' + (b + 1) + '  ' + mb.bandName(b)); ui.text(r.r - 4, r.y + 4, mb.bandRange(b), 'label', 10.5, t.ink2, 'right', 0.6);
    const w = V(bs(b, 'width')) * 0.01, st = V(bs(b, 'stereoize')) * 0.01, d = R(r.x, r.y + 18, ui.narrow ? 104 : 120, 84);
    ui.fill(d, t.screen); ui.stroke(d, t.ink, 1.2);
    ui.clip(R(d.x + 1, d.y + 1, d.w - 2, d.h - 2), () => {
      const cx = d.cx, cy = d.b - 10, Rr = 54; g.beginPath(); g.arc(cx, cy, Rr, Math.PI, 0, false); g.strokeStyle = css(t.grid); g.lineWidth = 1; g.stroke();
      const ang = Math.atan(Math.min(4, w)), ang2 = Math.atan(Math.min(4, w + st * 0.8));
      const wedge = a => { g.beginPath(); g.moveTo(cx, cy); g.lineTo(cx - Rr * Math.sin(a), cy - Rr * Math.cos(a)); g.arc(cx, cy, Rr, -Math.PI / 2 - a, -Math.PI / 2 + a, false); g.closePath(); };
      wedge(ang2); g.fillStyle = css(alpha(t.second, 0.25)); g.fill();
      wedge(ang); g.fillStyle = css(alpha(t.neon, t.dark ? 0.55 : 0.65)); g.fill(); g.strokeStyle = css(t.ink); g.lineWidth = 1.4; g.stroke();
      ui.line(cx - Rr - 4, cy, cx + Rr + 4, cy, alpha(t.ink, 0.5), 1); });
    const tx = d.r + 14, silent = lvShow[b] < -90;
    ui.text(tx, d.y + 12, 'WIDTH', 'label', 10, t.ink2, 'left', 1); ui.text(r.r - 4, d.y + 12, (w * 100).toFixed(0) + '%', 'mono', 11, t.ink, 'right');
    ui.text(tx, d.y + 32, 'STEREOIZE', 'label', 10, t.ink2, 'left', 1); ui.text(r.r - 4, d.y + 32, (st * 100).toFixed(0) + '%', 'mono', 11, t.ink, 'right');
    ui.text(tx, d.y + 52, 'CORRELATION', 'label', 10, t.ink2, 'left', 1); ui.text(r.r - 4, d.y + 52, silent ? '--' : cfmt(corrShow[b]), 'mono', 11, !silent && corrShow[b] < 0 ? t.red : t.ink, 'right');
    corrMeter(R(tx, d.y + 66, r.r - 4 - tx, 9), corrShow[b], false, silent);
  }

  function layout(narrow){
    const W = ui.W; let y0, gx, y1;
    if (!narrow){ disp = mb.disp = R(14, 58, 606, 330); scope = R(638, 58, 348, 204); corr = R(638, 276, 348, 112); y0 = 404; y1 = y0;
      bandPanel = R(14, y0, 500, 184); globalPanel = R(528, y0, W - 542, 184); gx = 540;
      bandSel.r = R(26, y0 + 12, 170, 26); soloBtn.r = R(206, y0 + 12, 58, 26); muteBtn.r = R(272, y0 + 12, 58, 26); bypBtn.r = R(338, y0 + 12, 66, 26);
      widthDial.r = R(26, y0 + 58, 96, 100); stDial.r = R(130, y0 + 58, 96, 100); bandInfo = R(250, y0 + 58, 252, 110);
    } else { disp = mb.disp = R(10, 94, W - 20, 262); scope = R(10, disp.b + 14, 250, 180); corr = R(270, disp.b + 14, W - 280, 180); y0 = scope.b + 14; y1 = y0 + 196;
      bandPanel = R(6, y0, W - 12, 184); globalPanel = R(6, y1, W - 12, 184); gx = 18;
      bandSel.r = R(14, y0 + 12, 170, 26); soloBtn.r = R(192, y0 + 12, 58, 26); muteBtn.r = R(256, y0 + 12, 58, 26); bypBtn.r = R(320, y0 + 12, 66, 26);
      widthDial.r = R(14, y0 + 58, 96, 100); stDial.r = R(114, y0 + 58, 96, 100); bandInfo = R(226, y0 + 58, W - 240, 110);
    }
    xoverSeg.r = R(gx, y1 + 30, 160, 24); bandsSeg.r = R(gx + 172, y1 + 30, 112, 24); monoBtn.r = R(gx + 300, y1 + 30, globalPanel.r - 12 - (gx + 300), 24);
    balDial.r = R(gx - 4, y1 + 82, 72, 84); outDial.r = R(gx + 76, y1 + 82, 72, 84); slopeSeg.r = R(gx + 222, y1 + 90, 62, 56); monoDial.r = R(gx + 318, y1 + 82, 100, 84);
    mb.layoutStrip(); scopeSetup();
  }

  return {
    layout, drawBody, down:mb.down, drag:mb.drag, up:mb.up, hover:mb.hover, leave:mb.leave, wheel:mb.wheel, key:mb.key, keyUp:mb.keyUp,
    help:mb.help, blur:mb.blur, close:mb.close, geo:() => mb,   // (geo: geometry for tests; the kit ignores it)
    changed(s){ if (s === 'bands' || s === 'mono_bass' || /^b\d_bypass$/.test(s)) rebind(); },
    reload(){ mb.reload(); rebind(); },
    frame(fr){
      const t = fr.t;
      for (let b = 0; b < NB; b++){ corrT[b] = t[5 + b]; lvT[b] = t[9 + b]; } corrAllT = t[13]; lvAllT = t[14];
      for (const rg of fr.rings){ if (rg.k === 0) fL.push(rg); else if (rg.k === 1) fR.push(rg); else if (rg.k === 2) fMid.push(rg); else if (rg.k === 3) fSide.push(rg); }
      const n = U.analyzerSize(fr.sr || 48000); mid.setup(fr.sr, n); side.setup(fr.sr, n);
      const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      Spectrum.update(dt, mid, fMid, side, fSide);
    },
    tick(dt){
      const k = Math.min(1, dt * 6), stale = U.now() - ui.lastFrame > 0.5;
      for (let b = 0; b < NB; b++){ corrShow[b] += (corrT[b] - corrShow[b]) * k; lvShow[b] = stale ? -120 : lvT[b]; }
      corrAllShow += (corrAllT - corrAllShow) * k; lvAll = stale ? -120 : lvAllT;
      if (corrAllShow < corrMin || corrMinT <= 0){ corrMin = corrAllShow; corrMinT = 2; } else corrMinT -= dt;
    },
    animating(){ return U.now() - ui.lastFrame < 2 || fbMax > 1e-3; },
    footer(){ const nb = mb.numBands(); return nb + ' BAND' + (nb > 1 ? 'S' : '') + '   ' + (Math.round(V('xover_mode')) ? 'LINEAR PHASE' : 'ANALOG') + '   ' + (V('mono_bass') >= 0.5 ? 'MONO < ' + ui.fmt('mono_freq').toUpperCase() : 'STEREO BASS') + '   LATENCY ' + (ui.lat | 0); },
  };
}});
})();
