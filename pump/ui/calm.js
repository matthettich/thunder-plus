/* …pump CALM pop-up (stage 3): plugins/calm/UICalm.cpp in the browser. The display: input spectrum (dim),
   output spectrum (neon), the live reduction hanging from the top (one or two detectors), the sensitivity curve with
   its six draggable nodes, the focus range with draggable edges, Alt-listen and Alt-hold node solo (PumpFX.hold
   kind 1, the audition filter with its region), and the suppression / timing / stereo panels. */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, Feed, Spectrum} = U;
const LO = 20, HI = 20000, NN = 6, NP = 220, UI_LO = 20, UI_HI = 20000;
const NODE_NAMES = ['Low shelf', 'Bell 2', 'Bell 3', 'Bell 4', 'Bell 5', 'High shelf'];
const DEF_F = [120, 350, 1200, 3500, 8000, 12000], DEF_Q = [0.71, 1, 1, 1, 1, 0.71], QUAL = ['1024', '2048', '4096', '8192'];
const BAND = 0, BELOW = 1, ABOVE = 2;   // the audition region (pump::Audition)
const GR_KEY = 'thunderplus-pump-calm-gr-v1', LQ_KEY = 'thunderplus-pump-calm-lq-v1';
const lsNum = (k, d, ok) => { try { const v = +localStorage.getItem(k); return ok(v) ? v : d; } catch(e){ return d; } };
const lsPut = (k, v) => { try { localStorage.setItem(k, String(v)); } catch(e){} };

// the analog shape of a node (Biquad.hpp analogPrototype: bell, or a 12 dB/oct shelf), as |H(jw)|^2 sections
function nodeProto(n, freq, gainDb, q, sr){
  const w0 = 2 * Math.PI * clamp(freq, 1, 0.75 * sr) / sr, G = Math.pow(10, gainDb / 20), sec = []; q = clamp(q, 0.025, 40); let gain = 1;
  if (n > 0 && n < NN - 1){ const A = Math.sqrt(G); sec.push([w0 * w0, A * w0 / q, 1, w0 * w0, w0 / (A * q), 1]); }
  else { const high = n === NN - 1, gk = Math.pow(G, 1 / 4), rz = high ? w0 / gk : w0 * gk, rp = high ? w0 * gk : w0 / gk, qq = Math.SQRT1_2 * q / Math.SQRT1_2;
    sec.push([rz * rz, rz / qq, 1, rp * rp, rp / qq, 1]); gain = high ? G : 1; }
  return {sec, gain};
}
function magSq(a, w){ let v = a.gain * a.gain; const w2 = w * w;
  for (const s of a.sec){ const nr = s[0] - s[2] * w2, ni = s[1] * w, dr = s[3] - s[5] * w2, di = s[4] * w; v *= (nr * nr + ni * ni) / Math.max(dr * dr + di * di, 1e-300); } return v; }

U.define('calm', {W:1000, H:620, nW:480, nH:950, bypass:true, back:8192, make(ui){
  const V = s => ui.value(s), ns = (n, f) => 's' + (n + 1) + '_' + f;
  let display = R(), meterL = R(), meterR = R(), panelA = R(), panelB = R(), panelC = R(), grMeter = R();
  const lvl = [-120, -120], hold = [-120, -120], holdT = [0, 0], pk = [0, 0];
  const red = [new Float32Array(NP), new Float32Array(NP)], redT = [new Float32Array(NP), new Float32Array(NP)];
  let grNow = 0, grT = 0, grRange = lsNum(GR_KEY, 12, v => v === 6 || v === 12 || v === 24), listenQ = lsNum(LQ_KEY, 2, v => v >= 0.3 && v <= 12);
  let audF = 1000, audQ = 2, audRegion = BAND, sel = -1, hover = -1, hoverEdge = -1, dragNode = -1, dragEdge = -1, soloNode = 0, dmode = 0;
  let origF = 1000, origG = 0, origQ = 1, lastX = 0, lastY = 0, effX = 0, effY = 0, qBaseY = 0, mouseX = -1, mouseY = -1, qMode = false, altHeld = false, lastFrameT = 0;
  const pre = new Spectrum(LO, HI), post = new Spectrum(LO, HI), fPre = new Feed(32768), fPost = new Feed(32768);
  const DM = {none:0, node:1, focus:2, aud:3, solo:4};

  // ---- suppression
  const depthDial = ui.dial('depth', 'DEPTH', 'l'); depthDial.tip = 'Depth: how much the resonances are pulled down (0% = nothing changes)';
  const sharpDial = ui.dial('sharpness', 'SHARPNESS'); sharpDial.tip = 'Sharpness: narrow notches on exact frequencies (high) or broad, smooth dips (low)';
  const selDial = ui.dial('selectivity', 'SELECTIVITY'); selDial.tip = 'Selectivity: only peaks that stick out far and keep ringing (high), or anything above the spectral shape (low)';
  depthDial.tabGroup = sharpDial.tabGroup = selDial.tabGroup = 1;
  const modeSeg = ui.seg('mode', ['SOFT', 'HARD']); modeSeg.vertical = true; modeSeg.tip = 'Soft: gentle, leaves part of each peak (musical). Hard: pulls peaks right down onto the spectral shape';
  // ---- timing / resolution
  const atkDial = ui.dial('attack', 'ATTACK'); atkDial.tip = 'Attack: how fast the reduction grabs a resonance';
  const relDial = ui.dial('release', 'RELEASE'); relDial.tip = 'Release: how fast it lets go (longer = smoother, fewer artifacts)';
  atkDial.tabGroup = relDial.tabGroup = 2;
  const qualMenu = ui.menuBtn('quality', 'FFT');
  qualMenu.itemsFn = () => ['1024  fast, coarse', '2048  balanced', '4096  fine', '8192  finest, slow'];
  qualMenu.textFn = () => QUAL[clamp(Math.round(V('quality')), 0, 3)] + '  ' + latMs();
  qualMenu.tip = 'Quality: FFT size (scaled with the sample rate). Bigger = finer frequency resolution, more latency';
  // ---- stereo / output
  const stereoSeg = ui.seg('stereo', ['LINKED', 'L / R', 'M / S']); stereoSeg.tip = 'Stereo: Linked (one detector, same cut on both sides), Stereo L/R or Mid/Side (two detectors)';
  const deltaBtn = ui.toggle('delta', 'DELTA'); deltaBtn.holdMomentary = true; deltaBtn.tip = 'Delta: hear only what is being removed (click = latch, click-and-hold = while held; D key)';
  const balDial = ui.dial('balance', 'BALANCE', 'm', true); balDial.tip = 'Balance (L/R, M/S): where the processing goes. -100% = left/mid only, +100% = right/side only';
  const mixDial = ui.dial('mix', 'MIX'); mixDial.tip = 'Mix: processed vs. dry (latency-aligned, no phasing)';
  const trimDial = ui.dial('trim', 'TRIM', 'm', true); trimDial.tip = 'Output trim';
  balDial.tabGroup = mixDial.tabGroup = trimDial.tabGroup = 3;
  const grRangeBtn = ui.button('GR ' + grRange, () => { const r = [6, 12, 24]; ui.openMenu(grRangeBtn.r, r.map(v => 'Reduction scale ' + v + ' dB'), Math.max(0, r.indexOf(grRange)), i => setGrRange(r[i])); });
  grRangeBtn.small = true; grRangeBtn.tip = 'Scale of the reduction curve hanging from the top';
  const setGrRange = r => { grRange = r; grRangeBtn.label = 'GR ' + r; lsPut(GR_KEY, r); };

  // ---- helpers
  const nodeFreq = n => V(ns(n, 'freq')), nodeGain = n => V(ns(n, 'gain')), nodeQ = n => V(ns(n, 'q'));
  const xOfF = f => ui.freqToX(display, f, LO, HI), fOfX = x => ui.xToFreq(display, x, LO, HI);
  const sensMid = () => display.y + display.h * 0.56, sensScale = () => display.h * 0.34 / 12, yOfSens = db => sensMid() - db * sensScale();
  const yOfRed = db => display.y + db / grRange * (display.h * 0.42);
  const nodePos = n => [xOfF(nodeFreq(n)), clamp(yOfSens(nodeGain(n)), display.y + 9, display.b - 9)];
  function nodeAt(x, y){ let best = -1, bd = 144 * (ui.narrow ? 2.2 : 1); for (let n = 0; n < NN; n++){ const [nx, ny] = nodePos(n), d = (nx - x) ** 2 + (ny - y) ** 2; if (d < bd){ bd = d; best = n; } } return best; }
  function focusEdgeAt(x, y){
    if (!display.has(x, y)) return -1; const xl = xOfF(V('focus_low')), xh = xOfF(V('focus_high')), tol = ui.narrow ? 10 : 6;
    const hl = clamp(xl, display.x + 7, display.r - 7), hh = clamp(xh, display.x + 7, display.r - 7), row = y > display.b - 52 && y < display.b - 26;
    const dl = Math.min(Math.abs(x - xl), row ? Math.abs(x - hl) : 1e9), dh = Math.min(Math.abs(x - xh), row ? Math.abs(x - hh) : 1e9);
    if (dl < tol && dl <= dh) return 0; if (dh < tol) return 1; return -1; }
  const fq = f => PumpFX.format('calm', 'audition_freq', f);
  const nodeShort = n => 'S' + (n + 1) + '  ' + ui.fmt(ns(n, 'freq')) + '  ' + ui.fmt(ns(n, 'gain')) + '  Q ' + nodeQ(n).toFixed(2);
  const nodeReadout = n => 'Sensitivity ' + (n + 1) + ' (' + NODE_NAMES[n] + ')  ' + ui.fmt(ns(n, 'freq')) + '  ' + ui.fmt(ns(n, 'gain')) + '  Q ' + nodeQ(n).toFixed(2);
  const latMs = () => Math.round(1000 * ui.lat / (ui.sr || 48000)) + ' ms';
  const pushAud = on => { audF = clamp(audF, 20, 20000); audQ = clamp(audQ, 0.3, 12); ui.hold(1, on ? 1 : 0, audF, audQ, audRegion); };
  const updateEnabled = () => { balDial.enabled = Math.round(V('stereo')) !== 0; };
  const mixDeep = () => mix(ui.th.neon, ui.th.ink, 0.45), chan2 = () => mix(ui.th.second, ui.th.red, ui.th.dark ? 0.6 : 0.7);

  // ---- drawing
  function drawBody(){
    const t = ui.th, g = ui.g; ui.screenBox(display);
    ui.clip(R(display.x + 1, display.y + 1, display.w - 2, display.h - 2), () => {
      ui.drawFreqGrid(display, LO, HI);
      for (let k = -2; k <= 2; k++){ const y = yOfSens(6 * k); ui.line(display.x, y, display.r, y, k === 0 ? alpha(t.ink, 0.22) : t.grid, 1); }
      ui.drawSpectrum(pre, display, 18, -84, alpha(t.ink, t.dark ? 0.08 : 0.05), alpha(t.ink, 0.30), LO, HI, 1.0);
      ui.drawSpectrum(post, display, 18, -84, alpha(t.neon, t.dark ? 0.26 : 0.36), alpha(mixDeep(), 0.75), LO, HI, 1.1);
      // focus range: shade what is outside, edges with handles
      const xl = xOfF(V('focus_low')), xh = xOfF(V('focus_high')), shade = alpha(t.bg, t.dark ? 0.62 : 0.58);
      if (xl > display.x) ui.fill(R(display.x, display.y, xl - display.x, display.h), shade);
      if (xh < display.r) ui.fill(R(xh, display.y, display.r - xh, display.h), shade);
      for (let e = 0; e < 2; e++){ const x = e === 0 ? xl : xh, hot = hoverEdge === e || (dmode === DM.focus && dragEdge === e);
        ui.line(x, display.y, x, display.b, hot ? t.ink : t.ink2, hot ? 2 : 1.2);
        const hx = clamp(x, display.x + 7, display.r - 7), hnd = R(hx - 5, display.b - 50, 10, 22);
        ui.fill(hnd, hot ? t.neon : t.bg); ui.stroke(hnd, t.ink, 1.2); ui.line(hx - 1.5, hnd.y + 6, hx - 1.5, hnd.b - 6, t.ink, 1); ui.line(hx + 1.5, hnd.y + 6, hx + 1.5, hnd.b - 6, t.ink, 1); }
      // the live reduction, hanging from the top
      const two = Math.round(V('stereo')) !== 0;
      for (let c = two ? 1 : 0; c >= 0; c--){
        const col = c === 0 ? t.second : chan2(), pts = [];
        for (let p = 0; p < NP; p++) pts.push(xOfF(UI_LO * Math.pow(UI_HI / UI_LO, (p + 0.5) / NP)), Math.min(display.b, yOfRed(red[c][p])));
        g.beginPath(); g.moveTo(display.x, display.y); for (let i = 0; i < pts.length; i += 2) g.lineTo(pts[i], pts[i + 1]); g.lineTo(display.r, display.y); g.closePath();
        g.fillStyle = css(alpha(col, c === 0 ? (t.dark ? 0.30 : 0.26) : 0.16)); g.fill();
        g.beginPath(); for (let i = 0; i < pts.length; i += 2) i ? g.lineTo(pts[i], pts[i + 1]) : g.moveTo(pts[i], pts[i + 1]); g.strokeStyle = css(col); g.lineWidth = c === 0 ? 1.6 : 1.2; g.lineJoin = 'round'; g.stroke(); }
      for (let k = 1; k <= 2; k++){ const db = grRange * k / 2; ui.text(display.x + 6, yOfRed(db), '-' + db.toFixed(0), 'mono', 9.5, mix(t.second, t.ink, t.dark ? 0.15 : 0.45), 'left'); }
      if (two){ const ms = Math.round(V('stereo')) === 2; let lx = display.x + 40;
        for (let c = 0; c < 2; c++){ const s = c === 0 ? (ms ? 'MID' : 'LEFT') : (ms ? 'SIDE' : 'RIGHT'), sw = R(lx, display.y + 8, 10, 10);
          ui.fill(sw, c === 0 ? t.second : chan2()); ui.stroke(sw, t.ink, 1); ui.text(lx + 15, display.y + 13.5, s, 'label', 10.5, t.ink, 'left', 1); lx += 15 + ui.textW(s, 'label', 10.5, 1) + 16; } }
      // the sensitivity curve (and the focused node's own shape)
      const S = ui.sr || 48000, protos = [];
      for (let n = 0; n < NN; n++) protos[n] = Math.abs(nodeGain(n)) > 1e-4 ? nodeProto(n, nodeFreq(n), nodeGain(n), nodeQ(n), S) : null;
      const N = 200, xs = new Float32Array(N), ys = new Float32Array(N), W = new Float64Array(N);
      for (let p = 0; p < N; p++){ xs[p] = display.x + display.w * p / (N - 1); W[p] = 2 * Math.PI * fOfX(xs[p]) / S; let m = 1; for (const a of protos) if (a) m *= magSq(a, W[p]);
        ys[p] = clamp(yOfSens(10 * Math.log10(Math.max(m, 1e-30))), display.y + 2, display.b - 2); }
      const fn = dragNode >= 0 ? dragNode : hover >= 0 ? hover : sel;
      if (fn >= 0 && Math.abs(nodeGain(fn)) > 0.05 && protos[fn]){ g.beginPath(); g.moveTo(xs[0], yOfSens(0));
        for (let p = 0; p < N; p++) g.lineTo(xs[p], clamp(yOfSens(10 * Math.log10(Math.max(magSq(protos[fn], W[p]), 1e-30))), display.y + 2, display.b - 2));
        g.lineTo(xs[N - 1], yOfSens(0)); g.closePath(); g.fillStyle = css(alpha(t.neon, t.dark ? 0.22 : 0.30)); g.fill(); }
      g.beginPath(); g.moveTo(xs[0], yOfSens(0)); for (let p = 0; p < N; p++) g.lineTo(xs[p], ys[p]); g.lineTo(xs[N - 1], yOfSens(0)); g.closePath(); g.fillStyle = css(alpha(t.ink, 0.05)); g.fill();
      const curve = () => { g.beginPath(); for (let p = 0; p < N; p++) p ? g.lineTo(xs[p], ys[p]) : g.moveTo(xs[p], ys[p]); };
      if (t.dark){ curve(); g.strokeStyle = css(t.glow); g.lineWidth = 6; g.stroke(); }
      curve(); g.strokeStyle = css(t.ink); g.lineWidth = 2; g.lineJoin = 'round'; g.stroke();
      for (let k = -2; k <= 2; k++) ui.text(display.r - 8, yOfSens(6 * k) - 7, k === 0 ? '0' : (k > 0 ? '+' : '') + 6 * k, 'mono', 9.5, t.ink2, 'right');
      ui.text(display.r - 8, yOfSens(12) - 20, 'SENSITIVITY', 'label', 9.5, t.ink2, 'right', 1);
      // nodes
      for (let n = 0; n < NN; n++){ const [x, y] = nodePos(n), isSel = n === sel || n === dragNode, hov = n === hover, flat = Math.abs(nodeGain(n)) < 0.05, rr = isSel ? 9 : 8;
        g.beginPath(); g.arc(x + 2, y + 2, rr, 0, 7); g.fillStyle = css(t.dim); g.fill();
        g.beginPath(); g.arc(x, y, rr, 0, 7); g.fillStyle = css(isSel ? t.neon : hov ? t.panel2 : t.bg); g.fill();
        g.strokeStyle = css(flat && !isSel && !hov ? t.ink2 : t.ink); g.lineWidth = isSel ? 2 : 1.5; g.stroke();
        ui.text(x, y + 0.5, String(n + 1), 'bold', 9.5, isSel ? t.neonInk : flat ? t.ink2 : t.ink, 'center'); }
      const info = dragNode >= 0 ? dragNode : hover;
      if (info >= 0 && dmode !== DM.aud && dmode !== DM.solo){ const [x, y] = nodePos(info), s = (info + 1) + '  ' + ui.fmt(ns(info, 'freq')) + '  ' + ui.fmt(ns(info, 'gain')) + '  Q ' + nodeQ(info).toFixed(2);
        const tw = ui.textW(s, 'mono', 11) + 14, tb = R(Math.max(display.x + 6, Math.min(display.r - tw - 6, x + 14)), y < display.y + 40 ? y + 14 : y - 34, tw, 22);
        ui.fill(tb, t.ink); ui.text(tb.cx, tb.cy, s, 'mono', 11, t.on, 'center'); }
      const fe = dmode === DM.focus ? dragEdge : hoverEdge;
      if (fe >= 0 && info < 0){ const sym = fe === 0 ? 'focus_low' : 'focus_high', s = (fe === 0 ? 'FOCUS FROM ' : 'FOCUS TO ') + ui.fmt(sym), tw = ui.textW(s, 'label', 11, 0.8) + 16, x = fe === 0 ? xl : xh;
        const tb = R(Math.max(display.x + 6, Math.min(display.r - tw - 6, fe === 0 ? x + 10 : x - tw - 10)), display.b - 74, tw, 22); ui.fill(tb, t.ink); ui.text(tb.cx, tb.cy, s, 'label', 11, t.on, 'center', 0.8); }
      if (V('delta') >= 0.5){ const s = 'DELTA  ·  HEARING ONLY WHAT IS REMOVED', tw = ui.textW(s, 'label', 12, 1) + 20, lb = R(display.cx - tw / 2, display.b - 36, tw, 22);
        ui.fill(lb, t.second); ui.text(lb.cx, lb.cy, s, 'label', 12, t.bg, 'center', 1); }
      if (dmode === DM.aud || dmode === DM.solo){
        let x0 = display.x, x1 = display.r;
        if (audRegion === BAND){ const half = Math.asinh(1 / (2 * audQ)) / Math.LN2; x0 = xOfF(audF * Math.pow(2, -half)); x1 = xOfF(audF * Math.pow(2, half)); }
        else if (audRegion === BELOW) x1 = xOfF(audF); else x0 = xOfF(audF);
        const sh = alpha(t.bg, 0.62);
        if (x0 > display.x){ ui.fill(R(display.x, display.y, x0 - display.x, display.h), sh); ui.line(x0, display.y, x0, display.b, t.second, 1.5); }
        if (x1 < display.r){ ui.fill(R(x1, display.y, display.r - x1, display.h), sh); ui.line(x1, display.y, x1, display.b, t.second, 1.5); }
        const label = dmode === DM.aud ? 'LISTEN  ' + fq(audF) + '  Q ' + audQ.toFixed(1) : 'SOLO SENSITIVITY ' + (soloNode + 1) + '  ' + fq(audF);
        const tw = ui.textW(label, 'label', 12, 1) + 16, cx = audRegion === BAND ? xOfF(audF) : (x0 + x1) / 2, lb = R(Math.max(display.x + 6, Math.min(display.r - tw - 6, cx - tw / 2)), display.y + 36, tw, 22);
        ui.fill(lb, t.second); ui.text(lb.cx, lb.cy, label, 'label', 12, t.bg, 'center', 1);
      } else if (altHeld && display.has(mouseX, mouseY)){ const s = 'ALT: CLICK AND HOLD TO LISTEN  ·  ON A NODE: SOLO ITS BAND', tw = ui.textW(s, 'label', 11, 1) + 18, lb = R(display.cx - tw / 2, display.y + 36, tw, 22);
        ui.fill(lb, t.ink); ui.text(lb.cx, lb.cy, s, 'label', 11, t.on, 'center', 1); }
    });
    ui.drawLevelMeter(meterL, lvl[0], hold[0], -48, 6); ui.drawLevelMeter(meterR, lvl[1], hold[1], -48, 6);
    ui.text(meterL.r + 1, display.b - 7, 'OUT', 'label', 9, t.ink2, 'center', 0.8);
    for (const p of [panelA, panelB, panelC]) ui.stroke(p, t.dim, 1);
    ui.caption(panelA.x + 14, panelA.y + 15, 'SUPPRESSION'); ui.caption(modeSeg.r.x, panelA.y + 15, 'MODE');
    ui.caption(panelB.x + 14, panelB.y + 15, 'TIMING'); ui.caption(ui.narrow ? qualMenu.r.x : panelB.x + 14, qualMenu.r.y - 10, 'RESOLUTION');
    ui.caption(panelC.x + 14, panelC.y + 15, 'STEREO'); ui.caption(deltaBtn.r.x, panelC.y + 15, 'LISTEN');
    ui.caption(grMeter.x, grMeter.y - 11, 'REDUCTION'); ui.fill(grMeter, t.panel2);
    const w = clamp(grNow / 6, 0, 1) * grMeter.w; if (w > 0.5) ui.fill(R(grMeter.x, grMeter.y, w, grMeter.h), t.second);
    ui.stroke(grMeter, t.ink, 1); for (let k = 1; k < 6; k++){ const x = grMeter.x + grMeter.w * k / 6; ui.line(x, grMeter.b - 3, x, grMeter.b, t.ink2, 1); }
    ui.text(grMeter.x, grMeter.b + 14, grNow < 0.05 ? '0.0 dB' : '-' + grNow.toFixed(1) + ' dB', 'mono', 11, t.ink, 'left'); ui.text(grMeter.r, grMeter.b + 14, '6 dB', 'mono', 9, t.ink2, 'right');
  }

  // ---- mouse on the display (node conventions, Alt-solo; see UICalm.cpp)
  function startNodeDrag(n, x, y){ dragNode = n; origF = nodeFreq(n); origG = nodeGain(n); origQ = nodeQ(n); ui.begin(); dmode = DM.node; lastX = x; lastY = y; effX = effY = 0; qMode = false; }
  function openNodeEntry(n){ const [nx, ny] = nodePos(n), r = R(Math.max(display.x + 4, Math.min(display.r - 104, nx - 50)), ny < display.y + 50 ? ny + 16 : ny - 38, 100, 22);
    ui.openTextEntry(ns(n, 'freq'), r, [[ns(n, 'freq'), r], [ns(n, 'gain'), r], [ns(n, 'q'), r]]); ui.status = 'Type (1k, A4, C#3+13) · Tab: frequency, sensitivity, Q · Enter'; }
  function openFocusEntry(e){ const x = xOfF(V(e === 0 ? 'focus_low' : 'focus_high')), r = R(Math.max(display.x + 4, Math.min(display.r - 104, x - 50)), display.b - 76, 100, 22);
    ui.openTextEntry(e === 0 ? 'focus_low' : 'focus_high', r, e === 0 ? [['focus_low', r], ['focus_high', r]] : [['focus_high', r], ['focus_low', r]]); ui.status = 'Type the edge (150, 1.2k, A4) · Tab: other edge · Enter'; }
  const resetNodeVals = (n, o) => { o[ns(n, 'freq')] = DEF_F[n]; o[ns(n, 'gain')] = 0; o[ns(n, 'q')] = DEF_Q[n]; return o; };
  const resetNode = n => ui.setMany(resetNodeVals(n, {}));
  const resetAll = () => { const o = {}; for (let k = 0; k < NN; k++) resetNodeVals(k, o); ui.setMany(o); };
  const resetFocus = () => ui.setMany({focus_low:ui.spec('focus_low').def, focus_high:ui.spec('focus_high').def});
  function nodeMenu(n, x, y){
    ui.openMenu(R(x, y, 10, 10), ['#SENSITIVITY ' + (n + 1) + ' · ' + NODE_NAMES[n], 'Reset node', 'Sensitivity 0 dB', 'Reset width', 'Type values…', '-', 'Solo while held: Alt + click', '-', 'Reset all nodes'], -1, i => {
      if (i === 1) resetNode(n); else if (i === 2) ui.setGesture(ns(n, 'gain'), 0); else if (i === 3) ui.setGesture(ns(n, 'q'), DEF_Q[n]); else if (i === 4) openNodeEntry(n); else if (i === 8) resetAll(); });
  }
  function focusMenu(e, x, y){
    ui.openMenu(R(x, y, 10, 10), ['#FOCUS RANGE', 'Type value…', 'Reset this edge', 'Full range (20 Hz - 20 kHz)', 'Reset focus range'], -1, i => {
      const s = e === 0 ? 'focus_low' : 'focus_high';
      if (i === 1) openFocusEntry(e); else if (i === 2) ui.setGesture(s, ui.spec(s).def); else if (i === 3) ui.setMany({focus_low:20, focus_high:20000}); else if (i === 4) resetFocus(); });
  }
  function backgroundMenu(x, y){
    ui.openMenu(R(x, y, 10, 10), ['Reset sensitivity curve', 'Reset focus range', '-', 'Reduction scale 6 dB', 'Reduction scale 12 dB', 'Reduction scale 24 dB'], -1, i => {
      if (i === 0) resetAll(); else if (i === 1) resetFocus(); else if (i >= 3) setGrRange([6, 12, 24][i - 3]); });
  }
  function down(x, y, btn, m, dbl){
    if (!display.has(x, y)) return false;
    const n = nodeAt(x, y), edge = n < 0 ? focusEdgeAt(x, y) : -1; mouseX = x; mouseY = y;
    if (btn === 2){ if (n >= 0){ sel = n; nodeMenu(n, x, y); } else if (edge >= 0) focusMenu(edge, x, y); else backgroundMenu(x, y); return false; }
    if (btn !== 0) return false;
    if (m.alt){
      if (n >= 0){ soloNode = n; audRegion = n === 0 ? BELOW : n === NN - 1 ? ABOVE : BAND; audF = nodeFreq(n); audQ = Math.max(0.3, nodeQ(n)); pushAud(true); dmode = DM.solo;
        ui.status = 'Solo: only node ' + (n + 1) + '’s region (release to return)'; return true; }
      dmode = DM.aud; audRegion = BAND; audQ = listenQ; audF = fOfX(x); pushAud(true); ui.status = 'Listening around ' + fq(audF) + '  (drag: sweep, wheel: width)'; return true; }
    if (n >= 0){ sel = n; if (dbl){ openNodeEntry(n); return false; } startNodeDrag(n, x, y); return true; }
    if (edge >= 0){ if (dbl){ openFocusEntry(edge); return false; } dragEdge = edge; ui.begin(); dmode = DM.focus; return true; }
    sel = -1; return false;
  }
  function drag(x, y, m){
    mouseX = x; mouseY = y; const fine = m.shift ? 0.15 : 1;
    if (dmode === DM.aud){ audF = fOfX(clamp(x, display.x, display.r)); pushAud(true); ui.status = 'Listening around ' + fq(audF) + '  (wheel: width)'; return; }
    if (dmode === DM.focus){ const s = dragEdge === 0 ? 'focus_low' : 'focus_high'; let f = fOfX(clamp(x, display.x, display.r));
      f = dragEdge === 0 ? Math.min(f, V('focus_high') / 1.26) : Math.max(f, V('focus_low') * 1.26); ui.setParam(s, f); ui.status = ui.paramStatus(s); return; }
    if (dmode === DM.node){
      effX += (x - lastX) * fine; effY += (y - lastY) * fine; lastX = x; lastY = y; const n = dragNode;
      ui.setParam(ns(n, 'freq'), origF * Math.pow(HI / LO, effX / display.w));
      if (m.cmd){ if (!qMode){ qMode = true; qBaseY = effY; origQ = nodeQ(n); origG = nodeGain(n); } ui.setParam(ns(n, 'q'), origQ * Math.pow(2, -(effY - qBaseY) / 50)); }
      else { if (qMode){ qMode = false; origG = nodeGain(n); effY = 0; lastY = y; origF = nodeFreq(n); effX = 0; lastX = x; } ui.setParam(ns(n, 'gain'), origG - effY / sensScale()); }
      ui.status = nodeReadout(n); }
  }
  function up(){
    if (dmode === DM.node || dmode === DM.focus) ui.end();
    else if (dmode === DM.aud || dmode === DM.solo){ pushAud(false); ui.status = ''; }
    dmode = DM.none; dragNode = -1; dragEdge = -1; qMode = false;
  }
  function hoverFn(x, y){
    mouseX = x; mouseY = y; hover = display.has(x, y) ? nodeAt(x, y) : -1; hoverEdge = hover < 0 ? focusEdgeAt(x, y) : -1;
    if (hover >= 0) ui.status = nodeShort(hover) + '   drag · Ctrl: width · Alt: solo · right-click';
    else if (hoverEdge >= 0) ui.status = ui.paramStatus(hoverEdge === 0 ? 'focus_low' : 'focus_high') + '   drag: processing range edge';
    else if (display.has(x, y)){ const f = fOfX(x), p = clamp(Math.floor(Math.log(f / UI_LO) / Math.log(UI_HI / UI_LO) * NP), 0, NP - 1);
      ui.status = fq(f) + '  ·  reduction -' + red[0][p].toFixed(1) + ' dB    Alt + hold: listen here'; }
  }
  function wheel(x, y, dy, m){
    const dir = dy > 0 ? 1 : -1;
    if (dmode === DM.aud){ listenQ = clamp(audQ * Math.pow(2, dir * (m.shift ? 0.05 : 0.25)), 0.3, 12); audQ = listenQ; pushAud(true); ui.status = 'Listen width: Q ' + listenQ.toFixed(2) + '  (wheel up: narrower)'; lsPut(LQ_KEY, listenQ.toFixed(3)); return true; }
    let n = nodeAt(x, y); if (n < 0 && sel >= 0 && Math.abs(x - xOfF(nodeFreq(sel))) < 30) n = sel; if (n < 0) return false;
    if (m.cmd) ui.setGesture(ns(n, 'gain'), nodeGain(n) + dir * (m.shift ? 0.1 : 0.5)); else ui.setGesture(ns(n, 'q'), nodeQ(n) * Math.pow(2, dir * (m.shift ? 0.02 : 0.1)));
    ui.status = nodeReadout(n); return true;
  }
  function key(e, m){
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (e.key === 'Alt'){ altHeld = true; return true; }
    if (!m.cmd && k === 'd'){ ui.setGesture('delta', V('delta') >= 0.5 ? 0 : 1); return true; }
    if (!m.cmd && k === 'h'){ ui.setGesture('mode', V('mode') >= 0.5 ? 0 : 1); return true; }
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){ sel = sel < 0 ? (e.key === 'ArrowLeft' ? NN - 1 : 0) : (sel + (e.key === 'ArrowLeft' ? NN - 1 : 1)) % NN; ui.status = nodeReadout(sel); return true; }
    if ((e.key === 'ArrowUp' || e.key === 'ArrowDown') && sel >= 0){ ui.setGesture(ns(sel, 'gain'), nodeGain(sel) + (e.key === 'ArrowUp' ? 1 : -1) * (m.shift ? 0.1 : 0.5)); ui.status = nodeReadout(sel); return true; }
    if ((e.key === 'Delete' || e.key === 'Backspace') && sel >= 0){ resetNode(sel); ui.status = nodeReadout(sel); return true; }
    if (e.key === 'Escape' && sel >= 0){ sel = -1; return true; }   // (with nothing selected, Esc closes the window)
    return false;
  }

  // ---- layout (desktop: the plugin's; narrow: the three panels stacked under the display)
  function layout(narrow){
    const W = ui.W;
    if (!narrow){
      display = R(14, 58, W - 28 - 34, 350); const y0 = 424, ph = 160;
      panelA = R(14, y0, 392, ph); panelB = R(panelA.r + 12, y0, 196, ph); panelC = R(panelB.r + 12, y0, W - 14 - (panelB.r + 12), ph);
    } else {
      display = R(10, 94, W - 20 - 30, 300);
      panelA = R(6, 410, W - 12, 160); panelB = R(6, 582, W - 12, 160); panelC = R(6, 754, W - 12, 160);
    }
    meterL = R(display.r + (narrow ? 8 : 12), display.y, 9, display.h - 18); meterR = R(meterL.r + 2, display.y, 9, display.h - 18);
    grRangeBtn.r = R(display.r - 62, display.y + 8, 54, 22);
    const a = panelA, b = panelB, c = panelC, mh = U.Dial.heightFor('m');
    depthDial.r = R(a.x + 14, a.y + 34, 92, U.Dial.heightFor('l')); sharpDial.r = R(a.x + 116, a.y + 42, 82, mh); selDial.r = R(a.x + 204, a.y + 42, 82, mh);
    modeSeg.r = R(a.r - 88, a.y + 44, 74, 64);
    atkDial.r = R(b.x + 12, b.y + 26, 82, mh); relDial.r = R(b.x + 102, b.y + 26, 82, mh); qualMenu.r = narrow ? R(b.x + 210, b.y + 64, b.w - 224, 26) : R(b.x + 14, b.y + 124, b.w - 28, 24);
    stereoSeg.r = R(c.x + 14, c.y + 30, 222, 26); deltaBtn.r = R(c.x + 248, c.y + 30, c.w - 248 - 16, 26);
    balDial.r = R(c.x + 8, c.y + 72, 76, mh); mixDial.r = R(c.x + 86, c.y + 72, 76, mh); trimDial.r = R(c.x + 164, c.y + 72, 76, mh);
    grMeter = R(c.r - 108, c.y + 88, 92, 12);
  }

  return {
    layout, drawBody, down, drag, up, hover:hoverFn, wheel, key, geo:() => ({display, nodePos}),   // (geo: for tests; the kit ignores it)
    keyUp(e){ if (e.key === 'Alt'){ altHeld = false; ui.dirty = true; } },
    leave(){ hover = -1; hoverEdge = -1; mouseX = mouseY = -1; },
    changed(s){ if (s === 'stereo') updateEnabled(); },
    reload(){ updateEnabled(); },
    frame(fr){
      const t = fr.t; pk[0] = Math.max(pk[0], t[1]); pk[1] = Math.max(pk[1], t[2]); grT = Math.max(grT, t[5]);
      for (let c = 0; c < 2; c++) for (let p = 0; p < NP; p++) redT[c][p] = t[6 + c * NP + p] || 0;
      for (const rg of fr.rings){ if (rg.k === 0) fPre.push(rg); else if (rg.k === 1) fPost.push(rg); }
      const n = U.analyzerSize(fr.sr || 48000); pre.setup(fr.sr, n); post.setup(fr.sr, n);
      const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      Spectrum.update(dt, pre, fPre, post, fPost);
    },
    tick(dt){
      for (let c = 0; c < 2; c++){ const p = U.dbOf(pk[c]); pk[c] = 0; lvl[c] = Math.max(p, lvl[c] - 26 * dt);
        if (p >= hold[c]){ hold[c] = p; holdT[c] = 1.5; } else if ((holdT[c] -= dt) <= 0) hold[c] = Math.max(p, hold[c] - 18 * dt); }
      // reduction curves: rise at once, fall smoothly; with no frames (silence) they fall back to nothing
      const fall = 1 - Math.exp(-dt / 0.12), stale = U.now() - ui.lastFrame > 0.5;
      for (let c = 0; c < 2; c++) for (let p = 0; p < NP; p++){ const v = stale ? 0 : redT[c][p]; red[c][p] = v > red[c][p] ? v : red[c][p] + (v - red[c][p]) * fall; }
      const g = stale ? 0 : grT; grT = 0; grNow = g > grNow ? g : grNow + (g - grNow) * (1 - Math.exp(-dt / 0.25));
    },
    animating(){ return U.now() - ui.lastFrame < 2 || lvl[0] > -100 || lvl[1] > -100 || grNow > 0.05; },
    footer(){ return ['SOFT', 'HARD'][clamp(Math.round(V('mode')), 0, 1)] + '  ·  ' + ['LINKED', 'STEREO L/R', 'MID/SIDE'][clamp(Math.round(V('stereo')), 0, 2)] + '  ·  FFT ' + QUAL[clamp(Math.round(V('quality')), 0, 3)] + '  ·  LATENCY ' + latMs().toUpperCase(); },
    close(){ ui.hold(1, 0, audF, audQ, audRegion); },
    blur(){ altHeld = false; if (dmode === DM.aud || dmode === DM.solo){ pushAud(false); dmode = DM.none; } },
    help(){ return [
      ['Alt + hold / drag the display', 'listen: hear only the region under the mouse (drag to sweep, wheel: width)'],
      ['Alt + click-and-hold a node', 'solo: hear only that sensitivity node’s band while held'],
      ['Drag a node', 'frequency and sensitivity (up = more reduction there); Shift: fine'],
      ['Ctrl/Cmd + drag up/down', 'node width (Q); mouse wheel on a node: width, Ctrl/Cmd + wheel: sensitivity'],
      ['Double-click a node', 'type frequency, Tab to sensitivity and Q (1k, A4, C#3+13)'],
      ['Right-click a node / display', 'node menu (reset node, width) · display menu (reset curve, scale)'],
      ['Drag a focus edge', 'set the processing range (soft-edged); double-click: type it'],
      ['Left/Right · Up/Down · Delete', 'select node · its sensitivity ±0.5 dB (Shift ±0.1) · reset it'],
      ['D  ·  H', 'delta on/off (hear what is removed) · Soft/Hard mode'],
      ['DELTA key', 'click = latch, click-and-hold = only while held']]; },
  };
}});
})();
