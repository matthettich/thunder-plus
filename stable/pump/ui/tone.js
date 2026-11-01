/* …pump TONE pop-up (stage 3): plugins/tone/UITone.cpp in the browser. Input (dim) and output (neon) spectra, the live
   balance curve around 0 dB (boosts neon, cuts in the second colour; the auto level shown on its own), the target curve
   faint on the spectrum scale, Alt-hold listen (PumpFX.hold kind 1), and the five balance controls as big dials. */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, Feed, Spectrum} = U;
const LO = 20, HI = 20000, NB = 48, F_LO = 25, F_HI = 18000, SPEC_TOP = 6, SPEC_BOTTOM = -90;
const RANGE_KEY = 'thunderplus-pump-tone-range-v1', TARGET_KEY = 'thunderplus-pump-tone-target-v1', LQ_KEY = 'thunderplus-pump-tone-lq-v1';
const lsGet = (k, d) => { try { const v = localStorage.getItem(k); return v == null ? d : v; } catch(e){ return d; } };
const lsPut = (k, v) => { try { localStorage.setItem(k, String(v)); } catch(e){} };
const erbNumber = f => 21.4 * Math.log10(1 + 0.00437 * f), erbToHz = e => (Math.pow(10, e / 21.4) - 1) / 0.00437;

U.define('tone', {W:1000, H:620, nW:480, nH:886, bypass:true, back:8192, make(ui){
  const V = s => ui.value(s);
  let display = R(), meterL = R(), meterR = R(), panelA = R(), panelB = R(), panelC = R(), panelD = null, makeupRect = R();
  const lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0], pk = [0, 0];
  const gain = new Float32Array(NB), target = new Float32Array(NB).fill(-120), erb = new Float64Array(NB), freq = new Float64Array(NB);
  const gainT = new Float32Array(NB), targetT = new Float32Array(NB).fill(-120);
  let makeup = 0, makeupT = 0, mkRaw = 0, dbRange = [3, 6, 12].includes(+lsGet(RANGE_KEY, 6)) ? +lsGet(RANGE_KEY, 6) : 6, showTarget = lsGet(TARGET_KEY, '1') !== '0';
  let listenF = 1000, listenQ = clamp(+lsGet(LQ_KEY, 2) || 2, 0.3, 12), hoverX = -1, hoverY = -1, altHeld = false, dmode = 0, lastFrameT = 0, haveBands = false;
  const pre = new Spectrum(LO, HI), post = new Spectrum(LO, HI), fPre = new Feed(32768), fPost = new Feed(32768);
  // the band centres before the first frame arrives (ToneEngine::prepare)
  const setupBands = sr => { const fHi = Math.min(F_HI, 0.45 * sr), e0 = erbNumber(F_LO), e1 = erbNumber(fHi); for (let b = 0; b < NB; b++){ erb[b] = e0 + (e1 - e0) * b / (NB - 1); freq[b] = erbToHz(erb[b]); } };
  setupBands(48000);

  const recDial = ui.dial('recover', 'RECOVER', 'l'); recDial.tip = 'Recover: brings up frequencies that are masked or under-represented (never lifts noise)';
  const tameDial = ui.dial('tame', 'TAME', 'l'); tameDial.tip = 'Tame: pulls down frequencies that dominate the balance';
  const biasDial = ui.dial('bias', 'BIAS', 'l', true); biasDial.tip = 'Bias: + favours taming (cuts), - favours recovering (boosts)';
  const brightDial = ui.dial('brighten', 'BRIGHTEN', 'l', true); brightDial.tip = 'Brighten: tilts the target and the curve around 1 kHz (+ brighter, - darker)';
  const bodyDial = ui.dial('body', 'BODY', 'l', true); bodyDial.tip = 'Body: low-end shelf on the target, below about 150 Hz (+ fuller, - leaner)';
  recDial.tabGroup = tameDial.tabGroup = biasDial.tabGroup = 1; brightDial.tabGroup = bodyDial.tabGroup = 2;
  const speedDial = ui.dial('speed', 'SPEED'); speedDial.tip = 'Speed: how quickly the balance follows the music (relaxed = smooth and steady, fast = lively)';
  const mixDial = ui.dial('mix', 'MIX'); mixDial.tip = 'Mix: processed vs. dry (latency-aligned, no phasing)';
  const outDial = ui.dial('out_gain', 'OUTPUT', 'm', true); outDial.tip = 'Output gain';
  speedDial.tabGroup = mixDial.tabGroup = outDial.tabGroup = 3;
  const autoBtn = ui.toggle('auto_level', 'AUTO LEVEL'); autoBtn.tip = 'Auto level: keeps the loudness unchanged, so you hear the balance, not the level (A key)';
  const deltaBtn = ui.toggle('delta', 'DELTA'); deltaBtn.holdMomentary = true; deltaBtn.tip = 'Delta: hear only what TONE changes (click = latch, click-and-hold = while held; D key)';
  const qualSeg = ui.seg('quality', ['2048', '4096']); qualSeg.tip = 'Quality: FFT size (scaled with the sample rate). 4096 = finer low end, more latency';
  const rangeBtn = ui.button('±' + dbRange, () => { const r = [3, 6, 12]; ui.openMenu(rangeBtn.r, r.map(v => 'Curve scale ±' + v + ' dB'), Math.max(0, r.indexOf(dbRange)), i => setRange(r[i])); });
  rangeBtn.small = true; rangeBtn.tip = 'Display scale of the gain curve';
  const setRange = r => { dbRange = r; rangeBtn.label = '±' + r; lsPut(RANGE_KEY, r); };

  const xOfF = f => ui.freqToX(display, f, LO, HI), fOfX = x => ui.xToFreq(display, x, LO, HI);
  const yOfDb = db => display.cy - db / dbRange * (display.h * 0.5 - 14), yOfSpec = db => display.y + (SPEC_TOP - db) / (SPEC_TOP - SPEC_BOTTOM) * display.h;
  const fq = f => PumpFX.format('tone', 'audition_freq', f);
  // ToneEngine::interp: the curve's gain at f, interpolated on the ERB scale as the audio is
  const curveAt = f => { const x = erbNumber(f); if (x <= erb[0]) return gain[0]; if (x >= erb[NB - 1]) return gain[NB - 1];
    const tt = (x - erb[0]) / (erb[NB - 1] - erb[0]) * (NB - 1), b = Math.min(NB - 2, tt | 0), fr = tt - b; return gain[b] + (gain[b + 1] - gain[b]) * fr; };
  const pushAud = on => ui.hold(1, on ? 1 : 0, listenF, listenQ, 0);
  const sgn = v => (v >= 0 ? '+' : '') + v.toFixed(1);

  function drawBody(){
    const t = ui.th, g = ui.g; ui.screenBox(display);
    ui.clip(R(display.x + 1, display.y + 1, display.w - 2, display.h - 2), () => {
      for (let k = -2; k <= 2; k++){ const y = yOfDb(dbRange * k / 2); ui.line(display.x, y, display.r, y, k === 0 ? alpha(t.ink, 0.28) : t.grid, 1); }
      ui.drawFreqGrid(display, LO, HI);
      ui.drawSpectrum(pre, display, SPEC_TOP, SPEC_BOTTOM, alpha(t.ink, t.dark ? 0.08 : 0.05), alpha(t.ink, 0.30), LO, HI, 1.0);
      ui.drawSpectrum(post, display, SPEC_TOP, SPEC_BOTTOM, alpha(t.neon, t.dark ? 0.22 : 0.30), alpha(mix(t.neon, t.ink, 0.45), 0.75), LO, HI, 1.1);
      if (showTarget && haveBands){
        g.beginPath(); for (let b = 0; b < NB; b++){ const x = xOfF(freq[b]), y = clamp(yOfSpec(target[b]), display.y, display.b); b ? g.lineTo(x, y) : g.moveTo(x, y); }
        g.strokeStyle = css(alpha(t.ink, 0.45)); g.lineWidth = 1.4; g.lineJoin = 'round'; g.stroke();
        for (let b = 2; b < NB; b += 4){ const x = xOfF(freq[b]), y = yOfSpec(target[b]); if (y > display.y + 4 && y < display.b - 4){ g.beginPath(); g.arc(x, y, 2, 0, 7); g.fillStyle = css(alpha(t.ink, 0.45)); g.fill(); } } }
      // the gain curve: boosts neon, cuts in the second colour
      const N = 240, xs = new Float32Array(N), ys = new Float32Array(N), y0 = yOfDb(0);
      for (let p = 0; p < N; p++){ xs[p] = display.x + display.w * p / (N - 1); ys[p] = clamp(yOfDb(curveAt(fOfX(xs[p]))), display.y + 2, display.b - 2); }
      for (let s = 0; s < 2; s++){ g.beginPath(); g.moveTo(xs[0], y0); for (let p = 0; p < N; p++) g.lineTo(xs[p], s === 0 ? Math.min(y0, ys[p]) : Math.max(y0, ys[p])); g.lineTo(xs[N - 1], y0); g.closePath();
        g.fillStyle = css(s === 0 ? alpha(t.neon, t.dark ? 0.45 : 0.55) : alpha(t.second, t.dark ? 0.40 : 0.42)); g.fill(); }
      const path = () => { g.beginPath(); for (let p = 0; p < N; p++) p ? g.lineTo(xs[p], ys[p]) : g.moveTo(xs[p], ys[p]); };
      if (t.dark){ path(); g.strokeStyle = css(t.glow); g.lineWidth = 6; g.stroke(); }
      path(); g.strokeStyle = css(t.ink); g.lineWidth = 2.2; g.lineJoin = 'round'; g.stroke();
      if (V('auto_level') >= 0.5 && Math.abs(makeup) > 0.05){ const ym = yOfDb(makeup);
        for (let x = display.x + 4; x < display.r - 40; x += 10) ui.line(x, ym, x + 5, ym, alpha(t.ink, 0.45), 1);
        const s = 'AUTO LEVEL ' + sgn(makeup) + ' dB', ly = ym + (makeup >= 0 ? -9 : 9), lw = ui.textW(s, 'mono', 9.5) + 8;
        ui.fill(R(display.x + 6, ly - 7, lw, 14), alpha(t.screen, 0.85)); ui.text(display.x + 10, ly, s, 'mono', 9.5, t.ink2, 'left'); }
      for (let k = -2; k < 2; k++){ const db = dbRange * k / 2; ui.text(display.r - 8, yOfDb(db) - 7, k === 0 ? '0' : (db > 0 ? '+' : '') + db.toFixed(0), 'mono', 9.5, t.ink2, 'right'); }
      // legend
      let lx = display.x + 12; const keyFn = (c, s, ln) => { if (ln) ui.line(lx, display.y + 14, lx + 14, display.y + 14, c, 1.6); else { ui.fill(R(lx, display.y + 9, 12, 10), c); ui.stroke(R(lx, display.y + 9, 12, 10), t.ink, 1); }
        ui.text(lx + 18, display.y + 14.5, s, 'label', 10.5, t.ink, 'left', 1); lx += 18 + ui.textW(s, 'label', 10.5, 1) + 16; };
      keyFn(alpha(t.neon, 0.8), 'BOOST', false); keyFn(alpha(t.second, 0.7), 'CUT', false); if (showTarget) keyFn(alpha(t.ink, 0.5), 'TARGET', true);
      if (hoverX >= display.x && dmode === 0 && display.has(hoverX, hoverY)){ const f = fOfX(hoverX), gg = curveAt(f);
        ui.line(hoverX, display.y, hoverX, display.b, alpha(t.ink, 0.35), 1);
        const s = fq(f) + '  ' + sgn(gg) + ' dB', tw = ui.textW(s, 'mono', 11) + 14, yy = clamp(yOfDb(gg) - 26, display.y + 30, display.b - 30), tb = R(Math.max(display.x + 6, Math.min(display.r - tw - 6, hoverX + 10)), yy, tw, 22);
        ui.fill(tb, t.ink); ui.text(tb.cx, tb.cy, s, 'mono', 11, t.on, 'center'); }
      if (V('delta') >= 0.5){ const s = 'DELTA  ·  HEARING ONLY WHAT TONE CHANGES', tw = ui.textW(s, 'label', 12, 1) + 20, lb = R(display.cx - tw / 2, display.b - 36, tw, 22);
        ui.fill(lb, t.second); ui.text(lb.cx, lb.cy, s, 'label', 12, t.bg, 'center', 1); }
      if (dmode === 1){ const half = Math.asinh(1 / (2 * listenQ)) / Math.LN2, x0 = xOfF(listenF * Math.pow(2, -half)), x1 = xOfF(listenF * Math.pow(2, half)), sh = alpha(t.bg, 0.62);
        ui.fill(R(display.x, display.y, Math.max(0, x0 - display.x), display.h), sh); ui.fill(R(x1, display.y, Math.max(0, display.r - x1), display.h), sh);
        ui.line(x0, display.y, x0, display.b, t.second, 1.5); ui.line(x1, display.y, x1, display.b, t.second, 1.5);
        const s = 'LISTEN  ' + fq(listenF) + '  Q ' + listenQ.toFixed(1), tw = ui.textW(s, 'label', 12, 1) + 16, lb = R(Math.max(display.x + 6, Math.min(display.r - tw - 6, xOfF(listenF) - tw / 2)), display.y + 36, tw, 22);
        ui.fill(lb, t.second); ui.text(lb.cx, lb.cy, s, 'label', 12, t.bg, 'center', 1); }
      else if (altHeld && display.has(hoverX, hoverY)){ const s = 'ALT: CLICK AND HOLD TO LISTEN TO THIS REGION', tw = ui.textW(s, 'label', 11, 1) + 18, lb = R(display.cx - tw / 2, display.y + 36, tw, 22);
        ui.fill(lb, t.ink); ui.text(lb.cx, lb.cy, s, 'label', 11, t.on, 'center', 1); }
    });
    ui.drawLevelMeter(meterL, lvl[0], hold[0], -48, 6); ui.drawLevelMeter(meterR, lvl[1], hold[1], -48, 6);
    ui.text(meterL.r + 1, display.b - 7, 'OUT', 'label', 9, t.ink2, 'center', 0.8);
    for (const p of [panelA, panelB, panelC]) ui.stroke(p, t.dim, 1);
    ui.caption(panelA.x + 14, panelA.y + 15, 'BALANCE'); ui.caption(panelB.x + 14, panelB.y + 15, 'TARGET'); ui.caption(panelC.x + 14, panelC.y + 15, 'RESPONSE & OUTPUT');
    ui.caption(qualSeg.r.x, qualSeg.r.y - 10, 'RESOLUTION');
    ui.text(makeupRect.x, makeupRect.cy + 4, V('auto_level') >= 0.5 ? 'AUTO LEVEL ' + sgn(makeup) + ' dB' : 'AUTO LEVEL OFF', 'mono', 11.5, t.ink, 'left');
  }

  function displayMenu(x, y){
    ui.openMenu(R(x, y, 10, 10), ['Curve scale ±3 dB', 'Curve scale ±6 dB', 'Curve scale ±12 dB', '-', showTarget ? 'Hide target curve' : 'Show target curve'], -1, i => {
      if (i <= 2) setRange([3, 6, 12][i]); else if (i === 4){ showTarget = !showTarget; lsPut(TARGET_KEY, showTarget ? 1 : 0); } });
  }
  function layout(narrow){
    const W = ui.W, lh = U.Dial.heightFor('l'), mh = U.Dial.heightFor('m');
    if (!narrow){
      display = R(14, 58, W - 28 - 34, 350); const y0 = 424, ph = 160; panelD = null;
      panelA = R(14, y0, 330, ph); panelB = R(panelA.r + 12, y0, 226, ph); panelC = R(panelB.r + 12, y0, W - 14 - (panelB.r + 12), ph);
      const c = panelC, bx = c.x + 250, bw = c.r - 14 - bx;
      speedDial.r = R(c.x + 8, c.y + 34, 76, mh); mixDial.r = R(c.x + 84, c.y + 34, 76, mh); outDial.r = R(c.x + 160, c.y + 34, 76, mh);
      autoBtn.r = R(bx, c.y + 30, bw, 26); deltaBtn.r = R(bx, c.y + 66, bw, 26); qualSeg.r = R(bx, c.y + 122, bw, 24); makeupRect = R(c.x + 14, c.y + 128, 220, 22);
    } else {
      display = R(10, 94, W - 20 - 30, 300);
      panelA = R(6, 410, W - 12, 150); panelB = R(6, 572, 228, 150); panelC = R(240, 572, W - 246, 150);
      const c = panelC, d = R(6, 734, W - 12, 128);
      speedDial.r = R(c.x + 6, c.y + 34, 74, mh); mixDial.r = R(c.x + 80, c.y + 34, 74, mh); outDial.r = R(c.x + 154, c.y + 34, 74, mh);
      autoBtn.r = R(d.x + 14, d.y + 14, 210, 26); deltaBtn.r = R(d.x + 14, d.y + 50, 210, 26); qualSeg.r = R(d.x + 244, d.y + 32, d.w - 258, 24); makeupRect = R(d.x + 14, d.y + 88, 220, 22);
      panelD = d;
    }
    meterL = R(display.r + (narrow ? 8 : 12), display.y, 9, display.h - 18); meterR = R(meterL.r + 2, display.y, 9, display.h - 18);
    rangeBtn.r = R(display.r - 52, display.y + 8, 44, 22);
    const a = panelA, b = panelB;
    if (!narrow){ recDial.r = R(a.x + 12, a.y + 40, 96, lh); tameDial.r = R(a.x + 117, a.y + 40, 96, lh); biasDial.r = R(a.x + 222, a.y + 40, 96, lh); }
    else { const cw = (a.w - 24) / 3; [recDial, tameDial, biasDial].forEach((d, i) => d.r = R(a.x + 12 + cw * i + (cw - 96) / 2, a.y + 36, 96, lh)); }
    brightDial.r = R(b.x + 13, b.y + 40, 96, lh); bodyDial.r = R(b.x + 117, b.y + 40, 96, lh);
    if (narrow){ brightDial.r.y = bodyDial.r.y = b.y + 36; }
  }

  return {
    layout, geo:() => ({display}),   // (geo: for tests; the kit ignores it)
    drawBody(){ drawBody(); if (panelD) ui.stroke(panelD, ui.th.dim, 1); },
    down(x, y, btn, m){
      if (!display.has(x, y)) return false;
      if (btn === 2){ displayMenu(x, y); return false; } if (btn !== 0) return false;
      if (m.alt){ dmode = 1; listenF = fOfX(x); pushAud(true); ui.status = 'Listening around ' + fq(listenF) + '  (drag: sweep, wheel: width)'; return true; }
      return false;
    },
    drag(x, y){ hoverX = x; hoverY = y; if (dmode === 1){ listenF = fOfX(clamp(x, display.x, display.r)); pushAud(true); ui.status = 'Listening around ' + fq(listenF) + '  (wheel: width)'; } },
    up(){ if (dmode === 1){ pushAud(false); ui.status = ''; } dmode = 0; },
    hover(x, y){ hoverX = x; hoverY = y; if (display.has(x, y)) ui.status = fq(fOfX(x)) + '  ' + sgn(curveAt(fOfX(x))) + ' dB    Alt + hold: listen here · right-click: display options'; else hoverX = -1; },
    leave(){ hoverX = hoverY = -1; },
    wheel(x, y, dy, m){ if (dmode !== 1) return false; listenQ = clamp(listenQ * Math.pow(2, (dy > 0 ? 1 : -1) * (m.shift ? 0.05 : 0.25)), 0.3, 12); pushAud(true);
      ui.status = 'Listen width: Q ' + listenQ.toFixed(2) + '  (wheel up: narrower)'; lsPut(LQ_KEY, listenQ.toFixed(3)); return true; },
    key(e, m){ const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
      if (e.key === 'Alt'){ altHeld = true; return true; }
      if (!m.cmd && k === 'd'){ ui.setGesture('delta', V('delta') >= 0.5 ? 0 : 1); return true; }
      if (!m.cmd && k === 'a'){ ui.setGesture('auto_level', V('auto_level') >= 0.5 ? 0 : 1); return true; }
      if (!m.cmd && k === 't'){ showTarget = !showTarget; lsPut(TARGET_KEY, showTarget ? 1 : 0); return true; }
      return false; },
    keyUp(e){ if (e.key === 'Alt'){ altHeld = false; ui.dirty = true; } },
    frame(fr){
      const t = fr.t; pk[0] = Math.max(pk[0], t[1]); pk[1] = Math.max(pk[1], t[2]); mkRaw = t[5];
      for (let b = 0; b < NB; b++){ gainT[b] = t[6 + b] - t[5]; targetT[b] = t[54 + b]; freq[b] = t[150 + b]; erb[b] = t[198 + b]; }   // the balance curve without the auto level makeup (shown on its own)
      haveBands = true;
      for (const rg of fr.rings){ if (rg.k === 0) fPre.push(rg); else if (rg.k === 1) fPost.push(rg); }
      const n = U.analyzerSize(fr.sr || 48000); pre.setup(fr.sr, n); post.setup(fr.sr, n);
      const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      Spectrum.update(dt, pre, fPre, post, fPost);
    },
    tick(dt){
      for (let c = 0; c < 2; c++){ const p = U.dbOf(pk[c]); pk[c] = 0; lvl[c] = Math.max(p, lvl[c] - 26 * dt);
        if (p >= hold[c]){ hold[c] = p; holdT[c] = 1.5; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(p, hold[c] - 18 * dt); }
      const a = 1 - Math.exp(-dt / 0.08), at = 1 - Math.exp(-dt / 0.3);
      for (let b = 0; b < NB; b++){ gain[b] += (gainT[b] - gain[b]) * a; target[b] += (targetT[b] - target[b]) * at; }
      makeup += (mkRaw - makeup) * a;
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100; },
    footer(){ const ms = Math.round(1000 * ui.lat / (ui.sr || 48000)); return '48 ERB BANDS  ·  FFT ' + ['2048', '4096'][clamp(Math.round(V('quality')), 0, 1)] + '  ·  LATENCY ' + ms + ' MS'; },
    close(){ ui.hold(1, 0, listenF, listenQ, 0); },
    blur(){ altHeld = false; if (dmode === 1){ pushAud(false); dmode = 0; } },
    help(){ return [
      ['Alt + hold / drag the display', 'listen: hear only the region under the mouse (drag to sweep, wheel: width)'],
      ['Recover · Tame', 'lift what is masked · pull down what dominates (both follow the music)'],
      ['Bias · Brighten · Body', 'favour cuts (+) or boosts (-) · tilt the target · low-end shelf on the target'],
      ['D  ·  A  ·  T', 'delta on/off · auto level on/off · show/hide the target curve'],
      ['DELTA key', 'click = latch, click-and-hold = only while held'],
      ['Right-click the display (long-press)', 'curve scale, target curve']]; },
  };
}});
})();
