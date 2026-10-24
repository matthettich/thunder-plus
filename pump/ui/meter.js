/* …pump METER pop-up (stage 2): plugins/meter/UIMeter.cpp in the browser. The loudness screen (integrated,
   short-term, momentary, maxima, LRA, true peak, PLR, PSR), the loudness history since the reset, the spectrum, the
   stereo meters, the target / history / scale settings and the L/R peak + RMS meters. */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, Feed, Spectrum} = U;
const T_LUFS = [-14, -16, -14, -11, -9, -23, -24, -14], T_TP = [-1, -1, -1, -2, -0.3, -1, -2, -1], T_TOL = [1, 1, 1, 1, 1, 0.5, 2, 1], HIST_MIN = [1, 3, 10, 30], RATE = 10, CUSTOM = 7;
const ok = v => Number.isFinite(v) && v > -70;
const fmt = (v, d = 1, plus) => !Number.isFinite(v) ? (v > 0 ? '+inf' : '-inf') : (plus && v >= 0 ? '+' : '') + v.toFixed(d);
const fmtTime = secs => { const s = Math.floor(Math.max(0, secs)); return s >= 3600 ? Math.floor(s / 3600) + ':' + String(Math.floor(s / 60) % 60).padStart(2, '0') + ':' + String(s % 60).padStart(2, '0') : Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0'); };
const meterT = db => db >= 0 ? 0 : db >= -12 ? 0.4 * (-db / 12) : db >= -30 ? 0.4 + 0.3 * ((-db - 12) / 18) : db >= -60 ? 0.7 + 0.3 * ((-db - 30) / 30) : 1;
function scaleRange(scale, target){ switch (scale){ case 0: return [target - 18, target + 9]; case 1: return [target - 36, target + 18]; case 2: return [-60, 0]; default: return [-36, 0]; } }

U.define('meter', {W:1000, H:620, nW:480, nH:930, bypass:false, back:32768, make(ui){
  let loud = R(), hist = R(), specR = R(), stereoR = R(), panel = R(), meters = R(), barL = R(), barR = R();
  const lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0], tpHold = [-Infinity, -Infinity], rmsS = [-120, -120], pk = [0, 0], ptp = [0, 0];
  let corrS = 0, corrMin = 1, balS = 0, lastReset = -1, T = null, lastFrameT = 0;
  const spec = new Spectrum(20, 24000), fSpec = new Feed(32768), hM = new Feed(32768), hS = new Feed(32768), hI = new Feed(32768);
  const resetBtn = ui.button('RESET', () => resetAll()); resetBtn.small = true; resetBtn.tip = 'Reset every measurement: loudness, true peak, history, holds (R)';
  const targetMenu = ui.menuBtn('target'); targetMenu.tip = 'Loudness target: colours the readouts and draws the target band';
  const customLufs = ui.field('custom_lufs', 'LUFS'); customLufs.round = true; customLufs.tip = 'Custom target loudness (drag, wheel or double-click to type)';
  const customTP = ui.field('custom_tp', 'TP'); customTP.round = true; customTP.tip = 'Custom true-peak limit in dBTP (drag, wheel or double-click to type)'; customLufs.tabGroup = customTP.tabGroup = 1;
  const histSeg = ui.seg('history', ['1M', '3M', '10M', '30M']); histSeg.tip = 'History length: 1, 3, 10 or 30 minutes';
  const scaleMenu = ui.menuBtn('scale', 'SCALE'); scaleMenu.tip = 'Meter scale: EBU +9 / +18 (relative to the target) or absolute LUFS';

  const V = s => ui.value(s), tgtI = () => clamp(Math.round(V('target')), 0, 7);
  const tgt = () => tgtI() === CUSTOM ? V('custom_lufs') : T_LUFS[tgtI()], tpLimit = () => tgtI() === CUSTOM ? V('custom_tp') : T_TP[tgtI()], tol = () => T_TOL[tgtI()];
  const loudColor = v => { const t = ui.th; if (!ok(v)) return t.ink2; const d = v - tgt(); return d > tol() ? t.red : d < -tol() ? t.second : t.ink; };
  const tv = i => T ? T[i] : -Infinity;
  function resetAll(){ const v = Math.max(V('reset'), T ? T[22] : 0) + 1; PumpFX.set(ui.t, ui.k, {reset:v}, {live:true, keepPreset:true, src:ui.id}); ui.vals.reset = v; clearHolds(); ui.status = 'All measurements reset'; }
  function clearHolds(){ for (let c = 0; c < 2; c++){ hold[c] = -120; holdT[c] = 0; tpHold[c] = -Infinity; } corrMin = 1; }
  const rebind = () => { customLufs.visible = customTP.visible = tgtI() === CUSTOM; };

  function drawLoudness(){
    const t = ui.th; ui.screenBox(loud); const n = ui.narrow;
    const I = tv(7), S = tv(6), Mo = tv(5), x = loud.x + 16, rx = loud.r - 16; let y = loud.y + 22;
    ui.caption(x, y, 'INTEGRATED'); ui.text(rx, y, fmtTime(T ? T[19] : 0), 'mono', 11, t.ink2, 'right');
    y += n ? 36 : 42; const big = n ? 46 : 54, iv = ok(I) ? fmt(I) : '--.-';
    ui.text(x - 2, y, iv, 'bold', big, loudColor(I), 'left'); ui.text(x + ui.textW(iv, 'bold', big) + 6, y + 13, 'LUFS', 'label', 12, t.ink2, 'left', 1);
    y += n ? 38 : 44;
    { let s, f = t.panel2, tc = t.ink; if (!ok(I)) s = 'WAITING FOR AUDIO'; else { const d = I - tgt();
        if (Math.abs(d) <= tol()){ s = 'ON TARGET  ' + fmt(d, 1, true) + ' LU'; f = t.neon; tc = t.neonInk; } else if (d > 0){ s = 'TOO LOUD  ' + fmt(d, 1, true) + ' LU'; f = t.red; tc = t.bg; } else s = 'BELOW TARGET  ' + fmt(d, 1, true) + ' LU'; }
      const tw = ui.textW(s, 'label', 11.5, 1) + 18; ui.fill(R(x, y - 11, tw, 22), f); ui.text(x + 9, y + 0.5, s, 'label', 11.5, tc, 'left', 1); }
    y += 26;
    { const [lo, hi] = scaleRange(Math.round(V('scale')), tgt()), bar = R(x, y, rx - x, 14), bx = v => bar.x + (clamp(v, lo, hi) - lo) / (hi - lo) * bar.w;
      ui.fill(bar, t.panel2); if (ok(S)) ui.fill(R(bar.x, bar.y, bx(S) - bar.x, bar.h), t.neon);
      ui.fill(R(bx(tgt() - tol()), bar.y - 3, bx(tgt() + tol()) - bx(tgt() - tol()), bar.h + 6), alpha(t.second, 0.28)); ui.line(bx(tgt()), bar.y - 4, bx(tgt()), bar.b + 4, t.second, 2);
      if (ok(Mo)) ui.line(bx(Mo), bar.y + 2, bx(Mo), bar.b - 2, alpha(t.ink, 0.5), 1.5); if (ok(I)) ui.line(bx(I), bar.y - 2, bx(I), bar.b + 2, t.ink, 3); ui.stroke(bar, t.ink, 1);
      const st = (hi - lo) > 40 ? 10 : 6; for (let v = Math.ceil(lo / st) * st; v <= hi; v += st) ui.text(bx(v), bar.b + 10, String(v), 'mono', 9, t.ink2, 'center'); }
    y += n ? 44 : 52; const cw = (rx - x) / 2;
    ui.caption(x, y, 'SHORT-TERM'); ui.caption(x + cw, y, 'MOMENTARY'); y += n ? 22 : 26;
    const mid = (cx, v) => { const s = ok(v) ? fmt(v) : '--.-', sz = n ? 24 : 28; ui.text(cx, y, s, 'bold', sz, loudColor(v), 'left'); ui.text(cx + ui.textW(s, 'bold', sz) + 4, y + 7, 'LUFS', 'label', 10, t.ink2, 'left', 0.8); };
    mid(x, S); mid(x + cw, Mo); y += n ? 22 : 28; ui.line(x, y, rx, y, t.dim, 1);
    const TP = tv(11), lra = tv(8), plr = tv(13), psr = tv(14), mM = tv(9), mS = tv(10), stp = tv(12), rowH = n ? 36 : 47, vs = n ? 16 : 19;
    const cell = (col, row, cap, v, unit, c) => { const cx = x + col * cw, cy = y + (n ? 18 : 22) + row * rowH; ui.caption(cx, cy, cap);
      ui.text(cx, cy + (n ? 17 : 22), v, 'mono', vs, c, 'left'); ui.text(cx + ui.textW(v, 'mono', vs) + 5, cy + (n ? 19 : 25), unit, 'label', 9.5, t.ink2, 'left', 0.8); };
    cell(0, 0, 'MAX MOMENTARY', ok(mM) ? fmt(mM) : '--.-', 'LUFS', loudColor(mM)); cell(1, 0, 'MAX SHORT-TERM', ok(mS) ? fmt(mS) : '--.-', 'LUFS', loudColor(mS));
    cell(0, 1, 'LOUDNESS RANGE', ok(I) && Number.isFinite(lra) ? fmt(lra) : '--.-', 'LU', t.ink); cell(1, 1, 'TRUE PEAK MAX', Number.isFinite(TP) && TP > -120 ? fmt(TP) : '-inf', 'dBTP', Number.isFinite(TP) && TP > tpLimit() ? t.red : t.ink);
    cell(0, 2, 'PLR', Number.isFinite(plr) && ok(I) ? fmt(plr) : '--.-', 'LU', t.ink); cell(1, 2, 'PSR', Number.isFinite(psr) && psr > -100 ? fmt(psr) : '--.-', 'LU', t.ink);
    const g2t = ok(I) ? tgt() - I : NaN, hr = Number.isFinite(TP) && TP > -120 ? tpLimit() - TP : NaN;
    cell(0, 3, 'GAIN TO TARGET', Number.isFinite(g2t) ? fmt(g2t, 1, true) : '--.-', 'dB', t.ink); cell(1, 3, 'TP HEADROOM', Number.isFinite(hr) ? fmt(hr, 1, true) : '--.-', 'dB', Number.isFinite(hr) && hr < 0 ? t.red : t.ink);
    const fy = loud.b - 22; ui.line(x, fy - 18, rx, fy - 18, t.dim, 1); ui.text(x, fy, 'SHORT-TERM TRUE PEAK', 'label', 10, t.ink2, 'left', 1);
    ui.text(rx, fy, (Number.isFinite(stp) && stp > -120 ? fmt(stp) : '-inf') + ' dBTP', 'mono', 11.5, Number.isFinite(stp) && stp > tpLimit() ? t.red : t.ink, 'right');
  }
  function drawHistory(){
    const t = ui.th, g = ui.g; ui.screenBox(hist); ui.caption(hist.x + 12, hist.y + 16, 'LOUDNESS HISTORY');
    const [lo, hi] = scaleRange(Math.round(V('scale')), tgt()), gr = R(hist.x + 40, hist.y + 42, hist.w - 80, hist.h - 66), yOf = v => gr.y + (hi - clamp(v, lo, hi)) / (hi - lo) * gr.h;
    { let lx = hist.r - 12; const item = (s, c, band) => { const tw = ui.textW(s, 'label', 9.5, 0.8); ui.text(lx, hist.y + 16, s, 'label', 9.5, t.ink2, 'right', 0.8); lx -= tw + 18;
        if (band) ui.fill(R(lx, hist.y + 12, 12, 8), c); else ui.line(lx, hist.y + 16, lx + 12, hist.y + 16, c, 2); lx -= 14; };
      item('TARGET', alpha(t.second, 0.5), true); item('INTEGRATED', t.second, false); if (!ui.narrow){ item('SHORT-TERM', t.ink, false); item('MOMENTARY', alpha(mix(t.neon, t.ink, t.dark ? 0 : 0.25), 0.7), true); } }
    const step = (hi - lo) > 40 ? 6 : 3, rel = Math.round(V('scale')) <= 1;
    for (let v = Math.ceil(lo / step) * step; v <= hi + 0.01; v += step){ ui.line(gr.x, yOf(v), gr.r, yOf(v), t.grid, 1); ui.text(gr.x - 6, yOf(v), String(Math.round(v)), 'mono', 9, t.ink2, 'right'); }
    if (rel) for (let d = -36; d <= 18; d += 9){ const v = tgt() + d; if (v < lo - 0.01 || v > hi + 0.01) continue; ui.text(gr.r + 6, yOf(v), d === 0 ? '0' : (d > 0 ? '+' : '') + d, 'mono', 9, t.ink2, 'left'); }
    ui.text(gr.x - 6, gr.y - 13, 'LUFS', 'label', 8.5, t.ink2, 'right', 0.8); if (rel) ui.text(gr.r + 6, gr.y - 13, 'LU', 'label', 8.5, t.ink2, 'left', 0.8);
    ui.fill(R(gr.x, yOf(tgt() + tol()), gr.w, yOf(tgt() - tol()) - yOf(tgt() + tol())), alpha(t.second, t.dark ? 0.16 : 0.12));
    for (let xx = gr.x; xx < gr.r; xx += 9) ui.line(xx, yOf(tgt()), Math.min(gr.r, xx + 5), yOf(tgt()), alpha(t.second, 0.9), 1.2);
    const N = Math.round(HIST_MIN[clamp(Math.round(V('history')), 0, 3)] * 60 * RATE);
    // historyWindow: the newest N points since the reset (MeterEngine.hpp)
    let count = 0, base = 0, first = 0;
    if (T && hI.any){ const start = T[23] >>> 0, end = hI.pos, total = (end - start) >>> 0, avail = Math.min(total, 32767), nn = Math.min(avail, N); count = nn; base = (end - nn) >>> 0; first = total - nn; }
    const secW = gr.w / (N / RATE), span = N / RATE, tstep = span <= 60 ? 10 : span <= 180 ? 30 : span <= 600 ? 60 : 300, t0 = first / RATE;
    for (let tt = Math.ceil(t0 / tstep) * tstep; tt <= t0 + span + 0.01; tt += tstep){ const xx = gr.x + (tt - t0) * secW; ui.line(xx, gr.y, xx, gr.b, t.grid, 1); ui.text(xx, gr.b + 13, fmtTime(tt), 'mono', 9, t.ink2, 'center'); }
    ui.stroke(gr, t.dim, 1);
    if (count <= 0){ ui.text(gr.cx, gr.cy, 'WAITING FOR AUDIO', 'label', 13, t.ink2, 'center', 2); return; }
    ui.clip(gr, () => {
      const cols = Math.floor(gr.w), ppc = N / cols, mc = alpha(mix(t.neon, t.ink, t.dark ? 0 : 0.25), t.dark ? 0.45 : 0.55), sv = new Float32Array(cols).fill(NaN), iv = new Float32Array(cols).fill(NaN);
      g.fillStyle = css(mc);
      for (let c = 0; c < cols; c++){ const a = Math.floor(c * ppc), b = Math.max(Math.floor((c + 1) * ppc), a + 1); if (a >= count) break; let mn = 1e9, mx = -1e9;
        for (let i = a; i < Math.min(b, count); i++){ const m = hM.at(base + i); if (Number.isFinite(m) && m > -90){ mn = Math.min(mn, m); mx = Math.max(mx, m); } }
        if (mx > -1e8){ const top = yOf(mx); g.fillRect(gr.x + c, top, 1, Math.max(1, yOf(mn) - top)); }
        const li = Math.min(b, count) - 1; sv[c] = hS.at(base + li); iv[c] = hI.at(base + li); }
      const poly = (v, col, w) => { g.beginPath(); let pen = false; for (let c = 0; c < cols; c++){ const val = v[c]; if (!Number.isFinite(val) || val < -90){ pen = false; continue; } const yy = yOf(val); if (!pen){ g.moveTo(gr.x + c, yy); pen = true; } else g.lineTo(gr.x + c + 0.5, yy); }
        g.strokeStyle = css(col); g.lineWidth = w; g.lineJoin = 'round'; g.stroke(); };
      poly(iv, t.second, 1.6); poly(sv, t.ink, 1.8);
    });
  }
  function drawSpectrum(){
    const t = ui.th; ui.screenBox(specR); const gg = R(specR.x + 1, specR.y + 1, specR.w - 2, specR.h - 2);
    ui.clip(gg, () => { for (let d = -12; d >= -84; d -= 12){ const yy = gg.y + (6 - d) / 96 * gg.h; ui.line(gg.x, yy, gg.r, yy, t.grid, 1); }
      ui.drawFreqGrid(gg, 20, 24000, true); ui.drawSpectrum(spec, gg, 6, -90, alpha(t.neon, t.dark ? 0.35 : 0.55), mix(t.neon, t.ink, t.dark ? 0.1 : 0.55), 20, 24000, 1.2); });
    ui.caption(specR.x + 10, specR.y + 14, 'SPECTRUM');
  }
  function drawStereo(){
    const t = ui.th; ui.screenBox(stereoR); const x = stereoR.x + 96, w = stereoR.w - 112;
    const bar = (y, cap, v, vmin, lo, hi, l0, lm, l1, neg) => { ui.caption(stereoR.x + 10, y, cap); const b = R(x, y - 6, w, 12); ui.fill(b, t.panel2);
      const cx = b.x + (0 - lo) / (hi - lo) * b.w, vx = b.x + (clamp(v, lo, hi) - lo) / (hi - lo) * b.w;
      ui.fill(R(Math.min(cx, vx), b.y, Math.abs(vx - cx), b.h), neg && v < 0 ? t.red : t.neon);
      if (vmin < hi){ const mx = b.x + (clamp(vmin, lo, hi) - lo) / (hi - lo) * b.w; ui.line(mx, b.y - 2, mx, b.b + 2, t.red, 1.5); }
      ui.line(cx, b.y - 3, cx, b.b + 3, t.ink, 1.2); ui.line(vx, b.y - 3, vx, b.b + 3, t.ink, 2); ui.stroke(b, t.ink, 1);
      ui.text(b.x, b.b + 8, l0, 'mono', 8.5, t.ink2, 'left'); ui.text(cx, b.b + 8, lm, 'mono', 8.5, t.ink2, 'center'); ui.text(b.r, b.b + 8, l1, 'mono', 8.5, t.ink2, 'right'); };
    bar(stereoR.y + 18, 'CORRELATION', corrS, corrMin, -1, 1, '-1', '0', '+1', true); bar(stereoR.y + 46, 'BALANCE', -balS, 99, -6, 6, 'L 6', 'C', '6 R', false);
  }
  function drawMeters(){
    const t = ui.th; ui.screenBox(meters); const tpl = tpLimit();
    for (let c = 0; c < 2; c++){ const b = c ? barR : barL, h = R(b.cx - 13, meters.y + 10, 26, 30), over = Number.isFinite(tpHold[c]) && tpHold[c] > tpl;
      ui.fill(h, over ? t.red : t.panel2); ui.text(h.cx, h.y + 9, c ? 'R' : 'L', 'label', 9, over ? t.bg : t.ink2, 'center', 0.8); ui.text(h.cx, h.y + 21, Number.isFinite(tpHold[c]) ? fmt(tpHold[c]) : '-inf', 'mono', 9.5, over ? t.bg : t.ink, 'center'); }
    for (let c = 0; c < 2; c++){ const b = c ? barR : barL; ui.fill(b, t.panel2); const tp = meterT(lvl[c]), tr = meterT(rmsS[c]);
      if (tp < 0.999) ui.fill(R(b.x, b.y + tp * b.h, b.w, b.h - tp * b.h), t.neon);
      if (tr < 0.999) ui.fill(R(b.x + 5, b.y + tr * b.h, b.w - 10, b.h - tr * b.h), t.dark ? alpha(t.bg, 0.6) : alpha(t.ink, 0.45));
      const th = meterT(hold[c]); if (th < 0.999) ui.line(b.x, b.y + th * b.h, b.r, b.y + th * b.h, hold[c] > tpl ? t.red : t.ink, 1.5); ui.stroke(b, t.ink, 1); }
    const yl = barL.y + meterT(tpl) * barL.h; ui.line(barL.x - 3, yl, barR.r + 3, yl, t.second, 1.5);
    for (const m of [0, 3, 6, 9, 12, 18, 24, 30, 40, 50, 60]){ const yy = barL.y + meterT(-m) * barL.h; ui.line(barR.r + 3, yy, barR.r + 6, yy, t.dim, 1); ui.text(barR.r + 20, yy, String(m), 'mono', 9, t.ink2, 'center'); }
    const by = barL.b + 16; ui.text(meters.cx, by, 'RMS', 'label', 9.5, t.ink2, 'center', 1);
    for (let c = 0; c < 2; c++){ const b = c ? barR : barL; ui.text(b.cx, by + 18, rmsS[c] > -99 ? fmt(rmsS[c]) : '-inf', 'mono', ui.narrow ? 8 : 9.5, t.ink, 'center'); }
  }
  function drawBody(){
    const t = ui.th; drawLoudness(); drawHistory(); drawSpectrum(); drawStereo(); drawMeters();
    ui.stroke(panel, t.dim, 1); ui.caption(panel.x + 12, panel.y + 13, 'TARGET');
    if (tgtI() !== CUSTOM){ const s = Math.round(tgt()) + ' LUFS  ·  ' + tpLimit().toFixed(1) + ' dBTP  ·  ±' + tol().toFixed(1) + ' LU';
      if (ui.narrow){ ui.text(customLufs.r.x, customLufs.r.cy - 6, Math.round(tgt()) + ' LUFS · ' + tpLimit().toFixed(1) + ' dBTP', 'mono', 10, t.ink2, 'left'); ui.text(customLufs.r.x, customLufs.r.cy + 8, '±' + tol().toFixed(1) + ' LU', 'mono', 10, t.ink2, 'left'); }
      else ui.text(customLufs.r.x, customLufs.r.cy, s, 'mono', 10.5, t.ink2, 'left'); }
    ui.caption(panel.x + 12, histSeg.r.y - 13, 'HISTORY');
  }
  function layout(narrow){
    if (!narrow){
      resetBtn.r = R(ui.headerExtra.r - 76, ui.headerExtra.y, 76, ui.headerExtra.h);
      loud = R(14, 58, 300, 526); hist = R(326, 58, 548, 306); specR = R(326, 378, 316, 124); stereoR = R(326, 516, 316, 68); panel = R(654, 378, 220, 206); meters = R(886, 58, 100, 526);
    } else {
      resetBtn.r = R(ui.headerExtra.r - 64, ui.headerExtra.y, 64, ui.headerExtra.h);
      loud = R(10, 94, 356, 410); meters = R(376, 94, 94, 410); hist = R(10, 516, 460, 196); specR = R(10, 724, 300, 96); stereoR = R(10, 832, 300, 64); panel = R(320, 724, 150, 172);
    }
    const px = panel.x + 12, pw = panel.w - 24;
    targetMenu.r = R(px, panel.y + 26, pw, 26);
    customLufs.r = R(px, panel.y + 62, (pw - 8) / 2, 24); customTP.r = R(px + (pw + 8) / 2, panel.y + 62, (pw - 8) / 2, 24);
    histSeg.r = R(px, panel.y + (narrow ? 112 : 122), pw, 24); scaleMenu.r = R(px, panel.y + (narrow ? 144 : 166), pw, 26);
    const mt = meters.y + 52, mb = meters.b - 56; barL = R(meters.x + 14, mt, 22, mb - mt); barR = R(meters.x + 40, mt, 22, mb - mt);
    rebind();
  }
  return {
    layout, drawBody,
    down(x, y, btn){
      if (btn === 2 && (loud.has(x, y) || hist.has(x, y))){ ui.openMenu(R(x, y, 10, 10), ['Reset all measurements', 'Reset peak holds', 'Reset correlation hold'], -1, i => {
        if (i === 0) resetAll(); else if (i === 1){ for (let c = 0; c < 2; c++){ hold[c] = -120; tpHold[c] = -Infinity; } ui.status = 'Peak holds reset'; } else { corrMin = 1; ui.status = 'Correlation hold reset'; } }); return false; }
      if (btn !== 0) return false;
      if (loud.has(x, y)){ resetAll(); return false; }
      if (meters.has(x, y)){ for (let c = 0; c < 2; c++){ hold[c] = -120; tpHold[c] = -Infinity; } ui.status = 'Peak and true-peak holds reset'; return false; }
      if (stereoR.has(x, y)){ corrMin = 1; ui.status = 'Correlation hold reset'; return false; }
      return false;
    },
    hover(x, y){
      if (loud.has(x, y)) ui.status = 'Loudness (EBU R128): click any readout to reset all measurements (R)';
      else if (meters.has(x, y)) ui.status = 'Sample peak (bar), RMS (inner bar), true-peak hold (top): click to reset the holds';
      else if (stereoR.has(x, y)) ui.status = 'Correlation (red mark: lowest since reset) and balance: click to reset the correlation hold';
      else if (hist.has(x, y)) ui.status = 'Short-term (ink), momentary (band) and integrated (line) loudness since the reset; right-click: reset menu';
      else if (specR.has(x, y)) ui.status = 'Spectrum (mid, +4.5 dB/oct tilt: pink noise reads flat)';
    },
    key(e, m){ const k = e.key.length === 1 ? e.key.toLowerCase() : e.key; if (m.cmd) return false;
      if (k === 'r'){ resetAll(); return true; }
      if (k === 'p'){ for (let c = 0; c < 2; c++){ hold[c] = -120; tpHold[c] = -Infinity; } corrMin = 1; ui.status = 'Holds reset'; return true; }
      if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){ ui.setGesture('target', (tgtI() + (e.key === 'ArrowRight' ? 1 : 7)) % 8); ui.status = ui.paramStatus('target'); return true; }
      if (k >= '1' && k <= '4'){ ui.setGesture('history', +k - 1); ui.status = ui.paramStatus('history'); return true; }
      return false; },
    changed(){ rebind(); }, reload(){ rebind(); },
    frame(fr){
      const t = T = fr.t; if (t[21] !== lastReset){ if (lastReset >= 0) clearHolds(); lastReset = t[21]; }
      pk[0] = Math.max(pk[0], t[1]); pk[1] = Math.max(pk[1], t[2]); ptp[0] = Math.max(ptp[0], t[3]); ptp[1] = Math.max(ptp[1], t[4]);
      for (const rg of fr.rings){ const f = [fSpec, hM, hS, hI][rg.k]; if (f) f.push(rg); }
      spec.setup(fr.sr); const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      Spectrum.update(dt, spec, fSpec);
    },
    tick(dt){
      for (let c = 0; c < 2; c++){ const p = U.dbOf(pk[c]), tp = U.dbOf(ptp[c]); pk[c] = ptp[c] = 0;
        lvl[c] = Math.max(p, lvl[c] - 20 * dt); if (p >= hold[c]){ hold[c] = p; holdT[c] = 2; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(p, hold[c] - 12 * dt);
        if (tp > -119) tpHold[c] = Number.isFinite(tpHold[c]) ? Math.max(tpHold[c], tp) : tp;
        const rms = T ? T[15 + c] : -120; rmsS[c] = rms > rmsS[c] ? rms : Math.max(rms, rmsS[c] - 15 * dt); }
      if (T){ corrS += (T[17] - corrS) * Math.min(1, dt * 8); if (!T[20]) corrMin = Math.min(corrMin, T[17]); balS += (T[18] - balS) * Math.min(1, dt * 8); }
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100; },
    footer(){ return (ui.narrow ? 'EBU R128   ' : 'BS.1770-4 / EBU R128   ') + ((ui.sr || 48000) / 1000).toFixed(1) + ' kHz' + (ui.narrow ? '' : '   PASS-THROUGH (BIT-EXACT)'); },
    help(){ return [['R  ·  RESET', 'reset every measurement (loudness, LRA, true peak, history, holds)'], ['Click the loudness readouts', 'reset every measurement (same as R)'],
      ['Click the level meters', 'reset the peak and true-peak holds (P: all holds)'], ['Click the stereo meters', 'reset the lowest-correlation hold'],
      ['Right-click readouts / history', 'reset menu'], ['Left / Right', 'previous / next loudness target'], ['1  2  3  4', 'history length 1 / 3 / 10 / 30 minutes']]; },
  };
}});
})();
