/* …pump LIMIT pop-up (stage 2): plugins/limit/UILimit.cpp in the browser. The 5 s level / gain-reduction history with
   the draggable gain and ceiling, the loudness target and short-term line, In / GR / Out meters, the loudness screen
   and the controls below. */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, Dial, Feed} = U;
const HP = 1000, NSTY = 7, OSN = ['Off', '2x', '4x', '8x'], TGT = [0, -14, -16, -9, -9, -14, 0];
const DESC = [['Slow leveling does most', 'of the work: the cleanest,', 'least coloured limiting.'], ['Transients pass the', 'leveling stage and the', 'release snaps back.'],
  ['Brickwall only, with a', 'transient-aware release', 'that keeps micro-dynamics.'], ['Balanced leveling and', 'brickwall. A safe first', 'choice for any master.'],
  ['Soft clipping ahead of a', 'fast brickwall: loud and', 'dense, with some grit.'], ['RMS leveling, light', 'clipping and a fast', 'release: loud and clear.'],
  ['Early attack and slow', 'release: avoids audible', 'distortion at all costs.']];
const RANGE_KEY = 'thunderplus-pump-limit-range-v1';
const fmtDb = (db, plus) => !Number.isFinite(db) || db <= -119 ? '-inf' : (plus && db >= 0 ? '+' : '') + db.toFixed(1);
const meterT = db => db >= 0 ? 0 : db >= -12 ? 0.5 * (-db / 12) : db >= -48 ? 0.5 + 0.5 * ((-db - 12) / 36) : 1;   // Pro-L scale: the top 12 dB take half

U.define('limit', {W:1000, H:620, nW:480, nH:984, bypass:true, back:4095, make(ui){
  let display = R(), meterArea = R(), inBars = R(), grBar = R(), outBars = R(), loud = R(), loudReadouts = R(), loudGrid = R(), ceilTag = R(), gainTag = R();
  let pStyle = R(), pOut = R(), pTime = R(), pLink = R(), pOpt = R();
  let range = (() => { try { const v = +localStorage.getItem(RANGE_KEY); return [12, 24, 36, 48].includes(v) ? v : 24; } catch(e){ return 24; } })();
  const hIn = new Feed(4096), hOut = new Feed(4096), hGR = new Feed(4096), hST = new Feed(4096);
  const aIn = new Float32Array(HP), aOut = new Float32Array(HP), aGR = new Float32Array(HP), aST = new Float32Array(HP);
  const inLvl = [-120, -120], inHold = [-120, -120], inHoldT = [0, 0], outLvl = [-120, -120], outHold = [-120, -120], outHoldT = [0, 0], inNum = [-120, -120], outNum = [-120, -120], pin = [0, 0], pout = [0, 0];
  let grLvl = 0, grHold = 0, grHoldT = 0, grNum = 0, pgr = 0, tel = null, dmode = 0, hoverCeil = false, dragStart = 0, dragY = 0;

  const styleMenu = ui.menuBtn('style'); styleMenu.arrows = true; styleMenu.tip = 'Limiting style (side arrows or the arrow keys step through them)';
  const gainD = ui.dial('gain', 'GAIN', 'l'); gainD.tip = 'Gain: drive into the limiter (drag in the display too)';
  const ceilD = ui.dial('ceiling', 'CEILING', 'l'); ceilD.tip = 'Output ceiling: the output never goes above it (drag the ceiling line too)'; gainD.tabGroup = ceilD.tabGroup = 1;
  const lookD = ui.dial('lookahead', 'LOOKAHEAD'), atkD = ui.dial('attack', 'ATTACK'), relD = ui.dial('release', 'RELEASE');
  lookD.tip = 'Lookahead: how far ahead the limiter sees peaks (longer = smoother, more latency)'; atkD.tip = 'Attack: shape of the gain change inside the lookahead (0% = late and steep, 100% = early and gentle)';
  relD.tip = 'Release: how fast the gain recovers (program dependent: the style adapts it)'; lookD.tabGroup = atkD.tabGroup = relD.tabGroup = 2;
  const linkT = ui.dial('link_transients', 'TRANSIENTS'), linkR = ui.dial('link_release', 'RELEASE');
  linkT.tip = 'Channel link for transients: 100% = both channels duck together on peaks, 0% = independent'; linkR.tip = 'Channel link for the release and leveling stage: lower keeps the stereo image wider'; linkT.tabGroup = linkR.tabGroup = 3;
  const tpBtn = ui.toggle('true_peak', 'TRUE PEAK'); tpBtn.small = true; tpBtn.tip = 'True peak limiting: also catch the peaks between samples (ITU-R BS.1770, 4x detector)';
  const dcBtn = ui.toggle('dc_filter', 'DC FILTER'); dcBtn.small = true; dcBtn.tip = 'DC offset filter (5 Hz high-pass) before the limiter';
  const osMenu = ui.menuBtn('oversampling', 'OS'); osMenu.tip = 'Oversampling for the limiter stage: less aliasing and fewer inter-sample peaks; more latency and CPU (keep it off on a phone)';
  const unityBtn = ui.button('UNITY', () => ui.setParam('unity', ui.value('unity') >= 0.5 ? 0 : 1), () => ui.value('unity') >= 0.5); unityBtn.small = true; unityBtn.holdMomentary = true;
  unityBtn.tip = 'Unity gain listen: hear the limiting at the loudness it came in (click = latch, click-and-hold = while held)';
  const dither = ui.menuBtn(null);
  dither.itemsFn = () => ['Off', '16-bit', '16-bit shaped', '24-bit', '24-bit shaped'];
  dither.getFn = () => { const d = Math.round(V('dither')), s = Math.round(V('noise_shaping')); return d === 0 ? 0 : d === 1 ? 1 + s : 3 + s; };
  dither.setFn = i => { const o = {dither:i === 0 ? 0 : i <= 2 ? 1 : 2}; if (i > 0) o.noise_shaping = (i === 2 || i === 4) ? 1 : 0; ui.setMany(o); };
  dither.textFn = () => { const d = Math.round(V('dither')); return d === 0 ? 'Dither: off' : 'Dither: ' + (d === 1 ? '16-bit' : '24-bit') + (V('noise_shaping') >= 0.5 ? ' shaped' : ''); };
  dither.tip = 'Dither for the final bit depth (TPDF; shaped = noise moved to where the ear is least sensitive)';
  const resetBtn = ui.button('RESET', () => resetLoudness()); resetBtn.small = true; resetBtn.tip = 'Reset the loudness measurement (or click any readout; key R)';
  const targetMenu = ui.menuBtn('target'); targetMenu.tip = 'Loudness target: drawn in the display and compared with the integrated loudness';
  const customBox = ui.field('target_custom'); customBox.textFn = () => V('target_custom').toFixed(1); customBox.tip = 'Custom target in LUFS (drag, wheel or double-click to type)';
  const rangeBtn = ui.button(range + ' dB', () => { const r = [12, 24, 36, 48]; ui.openMenu(rangeBtn.r, r.map(v => v + ' dB range'), Math.max(0, r.indexOf(range)), i => setRange(r[i])); });
  rangeBtn.small = true; rangeBtn.tip = 'Display range';

  const V = s => ui.value(s);
  const setRange = r => { range = r; rangeBtn.label = r + ' dB'; try { localStorage.setItem(RANGE_KEY, String(r)); } catch(e){} };
  const levelTop = () => display.y + 10, pxPerDb = () => (display.h - 30) / range, yOfDb = db => levelTop() - db * pxPerDb(), dbOfY = y => (levelTop() - y) / pxPerDb();
  const clampY = y => clamp(y, display.y + 1, display.b - 1);
  const targetLu = () => { const t = Math.round(V('target')); return t <= 0 || t > 6 ? 0 : t === 6 ? V('target_custom') : TGT[t]; };
  const tpLimit = () => { switch (Math.round(V('target'))){ case 0: return 0; case 3: case 4: return -0.3; default: return -1; } };
  const meterY = (r, db) => r.y + meterT(db) * r.h;
  function resetLoudness(){ ui.hold(2); ui.status = 'Loudness measurement reset'; }
  function resetHolds(w){ if (w < 0 || w === 0) inNum[0] = inNum[1] = -120; if (w < 0 || w === 1) grNum = 0; if (w < 0 || w === 2) outNum[0] = outNum[1] = -120;
    ui.status = w === 0 ? 'Input peak hold reset' : w === 1 ? 'Gain reduction hold reset' : w === 2 ? 'Output peak hold reset' : 'Peak holds reset'; }
  const nearCeil = (x, y) => display.has(x, y) && (Math.abs(y - yOfDb(V('ceiling'))) < (ui.narrow ? 11 : 6) || ceilTag.has(x, y));
  const rebind = () => { customBox.visible = Math.round(V('target')) === 6; targetMenu.r.w = customBox.visible ? targetMenu.wNarrow : targetMenu.wWide; };

  function drawBar(r, t, ht, fromTop, c){ const th = ui.th; ui.fill(r, th.panel2); t = clamp(t, 0, 1);
    if (fromTop){ if (t > 0.002) ui.fill(R(r.x, r.y, r.w, t * r.h), c); } else if (t < 0.998) ui.fill(R(r.x, r.y + t * r.h, r.w, r.h - t * r.h), c);
    if (ht > 0.002 && ht < 0.998) ui.line(r.x, r.y + ht * r.h, r.r, r.y + ht * r.h, th.ink, 1.5); ui.stroke(r, th.ink, 1); }
  function drawMeters(){
    const t = ui.th, ceil = V('ceiling'), inMax = Math.max(inNum[0], inNum[1]), outMax = Math.max(outNum[0], outNum[1]);
    const box = (bars, s, over) => { const b = R(bars.cx - 19, meterArea.y + 2, 38, 18); ui.fill(b, over ? t.red : t.panel2); ui.text(b.cx, b.cy + 0.5, s, 'mono', 10, over ? t.bg : t.ink, 'center'); };
    box(inBars, fmtDb(inMax), inMax > 0); box(grBar, grNum > 0.05 ? fmtDb(-grNum) : '0.0', false); box(outBars, fmtDb(outMax), outMax > ceil + 0.05);
    const bw = (inBars.w - 2) / 2;
    for (let c = 0; c < 2; c++){ drawBar(R(inBars.x + c * (bw + 2), inBars.y, bw, inBars.h), meterT(inLvl[c]), meterT(inHold[c]), false, t.neon);
      drawBar(R(outBars.x + c * (bw + 2), outBars.y, bw, outBars.h), meterT(outLvl[c]), meterT(outHold[c]), false, t.neon); }
    drawBar(grBar, meterT(-grLvl), grHold > 0.05 ? meterT(-grHold) : 0, true, t.second);
    const yc = meterY(outBars, ceil); ui.line(outBars.x - 4, yc, outBars.r + 4, yc, t.ink, 1.5);
    for (const m of [0, 3, 6, 9, 12, 18, 24, 36, 48]){ const y = meterY(inBars, -m); ui.text(outBars.r + 17, y, String(m), 'mono', 9.5, t.ink2, 'center');
      ui.line(inBars.r + 3, y, inBars.r + 6, y, t.dim, 1); ui.line(grBar.r + 3, y, grBar.r + 6, y, t.dim, 1); }
    const ly = inBars.b + 13; ui.text(inBars.cx, ly, 'IN', 'label', 10.5, t.ink2, 'center', 1); ui.text(grBar.cx, ly, 'GR', 'label', 10.5, t.ink2, 'center', 1); ui.text(outBars.cx, ly, 'OUT', 'label', 10.5, t.ink2, 'center', 1);
  }
  function drawDisplay(){
    const t = ui.th, g = ui.g; ui.screenBox(display);
    ui.clip(R(display.x + 1, display.y + 1, display.w - 2, display.h - 2), () => {
      const step = range <= 12 ? 2 : range <= 24 ? 3 : 6;
      for (let db = 0; db >= -range - 0.01; db -= step) ui.line(display.x, yOfDb(db), display.r, yOfDb(db), db === 0 ? alpha(t.ink, 0.22) : t.grid, 1);
      const ppp = (display.w - 2) / (HP - 1);
      for (let s = 1; s < 5; s++){ const x = display.r - 1 - s * 200 * ppp; ui.line(x, display.y, x, display.b, t.grid, 1); }
      const end = hST.pos, n = Math.min(HP, end);
      for (let k = 0; k < HP; k++){ if (k < HP - n || !hST.any){ aIn[k] = aOut[k] = -200; aGR[k] = 0; aST[k] = -Infinity; continue; }
        const idx = end - HP + k; aIn[k] = hIn.at(idx); aOut[k] = hOut.at(idx); aGR[k] = hGR.at(idx); aST[k] = hST.at(idx); }
      const x0 = display.x + 1, bot = display.b, xOf = k => x0 + k * ppp;
      const fillLevel = (v, c) => { g.beginPath(); g.moveTo(xOf(0), bot); for (let k = 0; k < HP; k++) g.lineTo(xOf(k), clampY(yOfDb(v[k]))); g.lineTo(xOf(HP - 1), bot); g.closePath(); g.fillStyle = css(c); g.fill(); };
      fillLevel(aIn, alpha(t.ink, t.dark ? 0.20 : 0.15)); fillLevel(aOut, alpha(t.neon, t.dark ? 0.62 : 0.80));
      g.beginPath(); for (let k = 0; k < HP; k++) k ? g.lineTo(xOf(k), clampY(yOfDb(aOut[k]))) : g.moveTo(xOf(k), clampY(yOfDb(aOut[k]))); g.strokeStyle = css(alpha(mix(t.neon, t.ink, 0.55), 0.9)); g.lineWidth = 1; g.stroke();
      const top = levelTop(), gy = k => Math.min(bot, top + Math.max(0, aGR[k]) * pxPerDb());
      g.beginPath(); g.moveTo(xOf(0), display.y); for (let k = 0; k < HP; k++) g.lineTo(xOf(k), gy(k)); g.lineTo(xOf(HP - 1), display.y); g.closePath(); g.fillStyle = css(alpha(t.second, t.dark ? 0.30 : 0.24)); g.fill();
      g.beginPath(); for (let k = 0; k < HP; k++) k ? g.lineTo(xOf(k), gy(k)) : g.moveTo(xOf(k), gy(k)); g.strokeStyle = css(t.second); g.lineWidth = 1.5; g.lineJoin = 'round'; g.stroke();
      g.beginPath(); let pen = false; for (let k = 0; k < HP; k++){ if (!Number.isFinite(aST[k]) || aST[k] < -90){ pen = false; continue; } const y = clampY(yOfDb(aST[k])); if (!pen){ g.moveTo(xOf(k), y); pen = true; } else g.lineTo(xOf(k), y); }
      g.strokeStyle = css(t.ink); g.lineWidth = 1.6; g.stroke();
      if (Math.round(V('target')) !== 0){ const tg = targetLu(), y = yOfDb(tg); for (let x = display.x; x < display.r; x += 10) ui.line(x, y, Math.min(display.r, x + 5), y, t.ink, 1.2);
        const s = 'TARGET ' + Math.round(tg) + ' LUFS', tw = ui.textW(s, 'label', 10.5, 0.8) + 12, tb = R(display.r - tw - 8, y - 19, tw, 16); ui.fill(tb, t.screen); ui.text(tb.cx, tb.cy + 0.5, s, 'label', 10.5, t.ink, 'center', 0.8); }
      const gs = 'GAIN ' + ui.fmt('gain'); gainTag = R(display.x + 8, display.y + 8, ui.textW(gs, 'label', 11, 0.8) + 16, 22);
      const yc = yOfDb(V('ceiling')), hot = dmode === 2 || hoverCeil; ui.line(display.x, yc, display.r, yc, hot ? t.red : t.ink, hot ? 2 : 1.5);
      ceilTag = R(display.x + 8 + gainTag.w + 8, yc + 4, ui.textW('CEILING -12.0 dB', 'label', 10.5, 0.8) + 14, 18);
      ui.fill(ceilTag, hot ? t.red : t.ink); ui.text(ceilTag.cx, ceilTag.cy + 0.5, 'CEILING ' + ui.fmt('ceiling'), 'label', 10.5, t.on, 'center', 0.8);
      for (let db = -step; db >= -range + 0.01; db -= step){ if (range > 12 && ((-db) % (step * 2)) > 0.01) continue; const s = String(Math.round(db)), tw = ui.textW(s, 'mono', 9.5) + 8, c = R(display.r - 4 - tw, yOfDb(db) - 14, tw, 13);
        ui.fill(c, alpha(t.screen, 0.85)); ui.text(c.cx, c.cy + 0.5, s, 'mono', 9.5, t.ink2, 'center'); }
      const ghot = dmode === 1; ui.fill(gainTag, ghot ? t.neon : t.screen); ui.stroke(gainTag, t.ink, 1.2); ui.text(gainTag.cx, gainTag.cy + 0.5, gs, 'label', 11, ghot ? t.neonInk : t.ink, 'center', 0.8);
      if (V('unity') >= 0.5){ const u = 'UNITY LISTEN  ' + ui.fmt('gain', -V('gain')), tw = ui.textW(u, 'label', 11, 0.8) + 16, ub = R(display.cx - tw / 2 + (ui.narrow ? 40 : 0), gainTag.y + (ui.narrow ? 26 : 0), tw, 22); ui.fill(ub, t.second); ui.text(ub.cx, ub.cy + 0.5, u, 'label', 11, t.bg, 'center', 0.8); }
      if (!ui.on){ const bb = R(display.cx - 50, display.cy - 14, 100, 28); ui.fill(bb, t.ink); ui.text(bb.cx, bb.cy + 0.5, 'BYPASSED', 'label', 13, t.on, 'center', 2); }
      for (let s = 1; s < 5; s++){ const x = display.r - 1 - s * 200 * ppp, txt = '-' + s + ' s', tw = ui.textW(txt, 'mono', 9) + 8, c = R(x - tw / 2, display.b - 17, tw, 14); ui.fill(c, alpha(t.screen, 0.85)); ui.text(c.cx, c.cy + 0.5, txt, 'mono', 9, t.ink2, 'center'); }
    });
  }
  function drawLoudness(){
    const t = ui.th, T = tel || new Float32Array(32).fill(-Infinity), ok = v => Number.isFinite(v) && v > -70;
    const mo = T[6], st = T[7], it = T[8], lra = T[9], tp = T[10], inI = T[15];
    ui.screenBox(loud);
    const narrow = ui.narrow, x = loud.x + 14, rx = narrow ? loud.x + loud.w * 0.5 - 8 : loud.r - 14;
    ui.caption(x, loud.y + 20, 'LOUDNESS'); ui.caption(x, loud.y + 46, 'SHORT-TERM');
    const big = (y, v, c) => { const s = ok(v) ? fmtDb(v) : '--.-'; ui.text(x - 1, y, s, 'bold', 31, c, 'left'); ui.text(x + ui.textW(s, 'bold', 31) + 5, y + 6, 'LUFS', 'label', 10.5, t.ink2, 'left', 0.8); };
    big(loud.y + 70, st, t.ink); ui.caption(x, loud.y + 98, 'INTEGRATED');
    const tg = targetLu(), hasT = Math.round(V('target')) !== 0;
    big(loud.y + 122, it, hasT && ok(it) && it - tg > 1 ? t.red : t.ink);
    if (hasT && ok(it)){ const d = it - tg, s = (d >= 0 ? '+' : '') + d.toFixed(1) + ' LU', on = Math.abs(d) <= 1, tw = ui.textW(s, 'mono', 11.5) + 12, db = R(rx - tw, loud.y + 113, tw, 19);
      ui.fill(db, on ? t.neon : d > 0 ? t.red : t.panel2); ui.text(db.cx, db.cy + 0.5, s, 'mono', 11.5, on ? t.neonInk : d > 0 ? t.bg : t.ink, 'center'); }
    { const bar = R(x, loud.y + 148, rx - x, 12), lo = -30, hi = -4, bx = v => bar.x + (clamp(v, lo, hi) - lo) / (hi - lo) * bar.w;
      ui.fill(bar, t.panel2); if (ok(st)) ui.fill(R(bar.x, bar.y, bx(st) - bar.x, bar.h), alpha(t.neon, 0.9));
      if (hasT){ ui.fill(R(bx(tg - 1), bar.y - 3, bx(tg + 1) - bx(tg - 1), bar.h + 6), alpha(t.second, 0.30)); ui.line(bx(tg), bar.y - 4, bx(tg), bar.b + 4, t.second, 2); }
      if (ok(it)) ui.line(bx(it), bar.y - 2, bx(it), bar.b + 2, t.ink, 2.5); ui.stroke(bar, t.ink, 1);
      for (const v of [-30, -24, -18, -14, -9, -4]) ui.text(bx(v), bar.b + 9, String(v), 'mono', 8.5, t.ink2, 'center'); }
    const gx = narrow ? loud.x + loud.w * 0.5 + 8 : x, grx = narrow ? loud.r - 14 : rx, gy = narrow ? loud.y + 40 : loud.y + 236, cw = (grx - gx) / 2;
    const cell = (col, row, cap, v, c) => { const cx = gx + col * cw, cy = gy + row * (narrow ? 44 : 40); ui.caption(cx, cy, cap); ui.text(cx, cy + 19, v, 'mono', 16, c, 'left'); };
    if (!narrow) ui.line(x, loud.y + 221, rx, loud.y + 221, t.dim, 1); else ui.line(loud.cx, loud.y + 14, loud.cx, loud.b - 14, t.dim, 1);
    cell(0, 0, 'MOMENTARY', ok(mo) ? fmtDb(mo) : '--.-', t.ink); cell(1, 0, 'RANGE LRA', ok(it) && Number.isFinite(lra) ? fmtDb(lra) + ' LU' : '--.-', t.ink);
    cell(0, 1, 'TRUE PEAK', Number.isFinite(tp) && tp > -120 ? fmtDb(tp) : '-inf', Number.isFinite(tp) && tp > tpLimit() + 0.05 ? t.red : t.ink);
    cell(1, 1, 'PLR', ok(it) && Number.isFinite(tp) ? fmtDb(tp - it) + ' LU' : '--.-', t.ink);
    cell(0, 2, 'INPUT', ok(inI) ? fmtDb(inI) : '--.-', t.ink2); cell(1, 2, 'LOUDER BY', ok(inI) && ok(it) ? fmtDb(it - inI, true) + ' LU' : '--.-', t.ink);
    loudReadouts = narrow ? R(loud.x, loud.y + 34, loud.w / 2, 144) : R(loud.x, loud.y + 34, loud.w, 144);
    loudGrid = narrow ? R(loud.cx, loud.y + 28, loud.w / 2, 140) : R(loud.x, loud.y + 223, loud.w, loud.h - 225);
  }
  function drawLegend(){
    const t = ui.th; let lx = display.x + 2; const ly = display.b + 19;
    const item = (c, s, ln) => { if (ln) ui.line(lx, ly, lx + 14, ly, c, 2); else { ui.fill(R(lx, ly - 4.5, 14, 9), c); ui.stroke(R(lx, ly - 4.5, 14, 9), t.dim, 1); }
      ui.text(lx + 19, ly + 0.5, s, 'label', 10, t.ink2, 'left', 0.9); lx += 19 + ui.textW(s, 'label', 10, 0.9) + (ui.narrow ? 10 : 18); };
    item(alpha(t.ink, t.dark ? 0.26 : 0.20), 'INPUT', false); item(t.neon, 'OUTPUT', false); item(t.second, ui.narrow ? 'GR' : 'GAIN REDUCTION', true); item(t.ink, ui.narrow ? 'SHORT-TERM' : 'SHORT-TERM LUFS', true);
    if (!ui.narrow) ui.text(display.r, ly + 0.5, '5 S HISTORY', 'label', 10, t.ink2, 'right', 0.9);
  }
  function drawBody(){
    const t = ui.th; drawDisplay(); drawLegend(); drawMeters(); drawLoudness();
    for (const p of [pStyle, pOut, pTime, pLink, pOpt]) ui.stroke(p, t.dim, 1);
    ui.caption(pStyle.x + 12, pStyle.y + 13, 'STYLE'); ui.caption(pOut.x + 12, pOut.y + 13, 'OUTPUT'); ui.caption(pTime.x + 12, pTime.y + 13, 'TIMING');
    ui.caption(pLink.x + 12, pLink.y + 13, 'CHANNEL LINK'); ui.caption(pOpt.x + 12, pOpt.y + 13, 'OPTIONS');
    const s = clamp(Math.round(V('style')), 0, NSTY - 1); for (let k = 0; k < 3; k++) ui.text(pStyle.x + 12, pStyle.y + (ui.narrow ? 72 : 76) + k * 16, DESC[s][k], 'label', 11, t.ink2, 'left', 0.3);
  }
  function layout(narrow){
    const dl = Dial.heightFor('l'), dm = Dial.heightFor('m');
    if (!narrow){
      display = R(14, 58, 578, 356); rangeBtn.r = R(display.r - 56, display.y + 8, 48, 22);
      meterArea = R(602, 58, 140, 356); const mt = 84, mb = 394; inBars = R(606, mt, 28, mb - mt); grBar = R(650, mt, 18, mb - mt); outBars = R(684, mt, 28, mb - mt);
      loud = R(752, 58, 234, 356); resetBtn.r = R(loud.r - 64, loud.y + 9, 54, 22);
      targetMenu.r = R(loud.x + 12, loud.y + 182, 150, 26); targetMenu.wNarrow = 150; targetMenu.wWide = 210; customBox.r = R(loud.x + 170, loud.y + 182, 52, 26);
      const y0 = 448, h = 136; pStyle = R(14, y0, 172, h); pOut = R(196, y0, 176, h); pTime = R(382, y0, 210, h); pLink = R(602, y0, 140, h); pOpt = R(752, y0, 234, h);
      styleMenu.r = R(pStyle.x + 12, y0 + 26, pStyle.w - 24, 28);
      const dy = y0 + 22; gainD.r = R(pOut.x + 6, dy + 4, 80, dl); ceilD.r = R(pOut.x + 90, dy + 4, 80, dl);
      const my = dy + 12; lookD.r = R(pTime.x + 4, my, 66, dm); atkD.r = R(pTime.x + 72, my, 66, dm); relD.r = R(pTime.x + 140, my, 66, dm);
      linkT.r = R(pLink.x + 2, my, 66, dm); linkR.r = R(pLink.x + 72, my, 66, dm);
      const ox = pOpt.x + 12, cw = (pOpt.w - 32) / 2; tpBtn.r = R(ox, y0 + 26, cw, 26); dcBtn.r = R(ox + cw + 8, y0 + 26, cw, 26);
      osMenu.r = R(ox, y0 + 62, cw, 26); unityBtn.r = R(ox + cw + 8, y0 + 62, cw, 26); dither.r = R(ox, y0 + 98, pOpt.w - 24, 26);
    } else {
      display = R(10, 94, 336, 236); rangeBtn.r = R(display.r - 56, display.y + 8, 48, 22);
      meterArea = R(352, 94, 124, 262); const mt = 120, mb = 330; inBars = R(356, mt, 28, mb - mt); grBar = R(396, mt, 18, mb - mt); outBars = R(426, mt, 28, mb - mt);
      loud = R(10, 368, 460, 214); resetBtn.r = R(loud.x + loud.w / 2 - 70, loud.y + 9, 54, 22);
      targetMenu.r = R(loud.x + loud.w / 2 + 8, loud.y + 176, 150, 26); targetMenu.wNarrow = 150; targetMenu.wWide = 210; customBox.r = R(loud.r - 62, loud.y + 176, 52, 26);
      const y0 = 596; pStyle = R(10, y0, 224, 124); pOut = R(246, y0, 224, 124); pTime = R(10, y0 + 136, 224, 110); pLink = R(246, y0 + 136, 224, 110); pOpt = R(10, y0 + 258, 460, 96);
      styleMenu.r = R(pStyle.x + 12, y0 + 24, pStyle.w - 24, 28);
      gainD.r = R(pOut.x + 22, y0 + 22, 80, dl); ceilD.r = R(pOut.x + 122, y0 + 22, 80, dl);
      const my = y0 + 136 + 22; lookD.r = R(pTime.x + 6, my, 66, dm); atkD.r = R(pTime.x + 79, my, 66, dm); relD.r = R(pTime.x + 152, my, 66, dm);
      linkT.r = R(pLink.x + 30, my, 66, dm); linkR.r = R(pLink.x + 126, my, 66, dm);
      const oy = y0 + 258, ox = pOpt.x + 12, cw = (pOpt.w - 24 - 24) / 4; tpBtn.r = R(ox, oy + 24, cw, 26); dcBtn.r = R(ox + cw + 8, oy + 24, cw, 26);
      osMenu.r = R(ox + 2 * (cw + 8), oy + 24, cw, 26); unityBtn.r = R(ox + 3 * (cw + 8), oy + 24, cw, 26); dither.r = R(ox, oy + 60, pOpt.w - 24, 26);
    }
    rebind();
  }
  return {
    layout, drawBody,
    down(x, y, btn, m, dbl){
      if (display.has(x, y)){
        const onC = nearCeil(x, y), p = onC ? 'ceiling' : 'gain';
        if (btn === 2){ ui.openMenu(R(x, y, 10, 10), ['Reset loudness', 'Reset peak holds', '-', '#DISPLAY RANGE', '12 dB', '24 dB', '36 dB', '48 dB'], -1, i => { if (i === 0) resetLoudness(); else if (i === 1) resetHolds(-1); else if (i >= 4) setRange([12, 24, 36, 48][i - 4]); }); return false; }
        if (btn !== 0) return false;
        if (dbl){ const r = onC ? R(ceilTag.x, ceilTag.y, Math.max(ceilTag.w, 110), 22) : R(clamp(x - 50, display.x + 4, display.r - 104), y - 11, 100, 22);
          ui.openTextEntry(p, r, [['gain', onC ? R(gainTag.x, gainTag.y, 110, 22) : r], ['ceiling', R(ceilTag.x, ceilTag.y, 110, 22)]]); ui.status = onC ? 'Type the ceiling (dB); Tab: gain' : 'Type the gain (dB, or 2x); Tab: ceiling'; return false; }
        if (m.cmd || m.alt){ ui.setGesture(p, ui.spec(p).def); return false; }
        dmode = onC ? 2 : 1; dragStart = V(p); dragY = y; ui.begin(); return true;
      }
      if (x >= inBars.x - 6 && x < outBars.r + 30 && y >= meterArea.y && y < meterArea.b){ resetHolds(x < grBar.x - 6 ? 0 : x < outBars.x - 6 ? 1 : 2); return false; }
      if (btn === 0 && loud.has(x, y) && (loudReadouts.has(x, y) || loudGrid.has(x, y))){ resetLoudness(); return false; }
      return false;
    },
    drag(x, y, m){ if (!dmode) return; const p = dmode === 2 ? 'ceiling' : 'gain', sens = 1 / pxPerDb() * (m.shift ? 0.1 : 1);
      ui.setParam(p, dragStart + (dragY - y) * sens); if (m.shift){ dragStart = V(p); dragY = y; } ui.status = ui.paramStatus(p); },
    up(){ if (dmode) ui.end(); dmode = 0; },
    hover(x, y){ hoverCeil = nearCeil(x, y);
      if (hoverCeil) ui.status = 'Ceiling ' + ui.fmt('ceiling') + '    drag to change · Shift: fine · double-click: type · Ctrl-click: reset';
      else if (display.has(x, y)) ui.status = dbOfY(y).toFixed(1) + ' dB    drag up/down: gain (' + ui.fmt('gain') + ') · wheel: gain · double-click: type · right-click: display menu';
      else if (x >= inBars.x - 6 && x < outBars.r + 30 && y >= meterArea.y && y < meterArea.b) ui.status = x < grBar.x - 6 ? 'Input level (L/R) and peak hold: click to reset the hold' : x < outBars.x - 6 ? 'Gain reduction and its hold: click to reset the hold' : 'Output level (L/R), ceiling mark and peak hold: click to reset the hold';
      else if (loud.has(x, y) && (loudReadouts.has(x, y) || loudGrid.has(x, y))) ui.status = 'Output loudness (EBU R128): click any readout to reset the measurement (key R)'; },
    leave(){ hoverCeil = false; },
    wheel(x, y, dy, m){ if (!display.has(x, y)) return false; const p = nearCeil(x, y) ? 'ceiling' : 'gain'; ui.setGesture(p, V(p) + (dy > 0 ? 1 : -1) * (m.shift ? 0.1 : 0.5)); ui.status = ui.paramStatus(p); return true; },
    key(e, m){ const k = e.key.length === 1 ? e.key.toLowerCase() : e.key; if (m.cmd) return false; const fine = m.shift ? 0.1 : 0.5;
      if (k === 'r'){ resetLoudness(); return true; } if (k === 'p'){ resetHolds(-1); return true; }
      if (k === 'u'){ ui.setParam('unity', V('unity') >= 0.5 ? 0 : 1); ui.status = ui.paramStatus('unity'); return true; }
      if (e.key === 'ArrowUp' || e.key === 'ArrowDown'){ ui.setGesture('gain', V('gain') + (e.key === 'ArrowUp' ? fine : -fine)); ui.status = ui.paramStatus('gain'); return true; }
      if (e.key === 'PageUp' || e.key === 'PageDown'){ ui.setGesture('ceiling', V('ceiling') + (e.key === 'PageUp' ? 0.1 : -0.1)); ui.status = ui.paramStatus('ceiling'); return true; }
      if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){ ui.setGesture('style', (Math.round(V('style')) + (e.key === 'ArrowRight' ? 1 : NSTY - 1)) % NSTY); ui.status = ui.paramStatus('style'); return true; }
      return false; },
    changed(){ rebind(); }, reload(){ rebind(); },
    frame(fr){ const t = fr.t; tel = t; pin[0] = Math.max(pin[0], t[3]); pin[1] = Math.max(pin[1], t[4]); pout[0] = Math.max(pout[0], t[1]); pout[1] = Math.max(pout[1], t[2]); pgr = Math.max(pgr, t[5]);
      for (const rg of fr.rings){ const f = [hIn, hOut, hGR, hST][rg.k]; if (f) f.push(rg); } },
    tick(dt){
      const bal = (c, L, H, HT, p) => { L[c] = Math.max(p, L[c] - 24 * dt); if (p >= H[c]){ H[c] = p; HT[c] = 2; } else if ((HT[c] -= dt) <= 0) H[c] = Math.max(p, H[c] - 15 * dt); };
      for (let c = 0; c < 2; c++){ const pi = U.dbOf(pin[c]), po = U.dbOf(pout[c]); pin[c] = pout[c] = 0; bal(c, inLvl, inHold, inHoldT, pi); bal(c, outLvl, outHold, outHoldT, po);
        inNum[c] = Math.max(inNum[c], pi); outNum[c] = Math.max(outNum[c], po); }
      const gq = pgr; pgr = 0; grLvl = Math.max(gq, grLvl - 20 * dt); if (gq >= grHold){ grHold = gq; grHoldT = 2; } else if ((grHoldT -= dt) <= 0) grHold = Math.max(gq, grHold - 12 * dt); grNum = Math.max(grNum, gq);
    },
    animating(){ return U.now() - ui.lastFrame < 2 || inLvl[0] > -100 || outLvl[0] > -100 || grLvl > 0.05; },
    footer(){ return (V('true_peak') >= 0.5 ? 'TRUE PEAK' : 'SAMPLE PEAK') + '   OS ' + OSN[clamp(Math.round(V('oversampling')), 0, 3)] + '   ' + ui.latencyText() + (ui.narrow ? '' : ' (' + (ui.lat | 0) + ')'); },
    close(){ if (V('unity') >= 0.5) ui.setParam('unity', 0); },
    help(){ return [['Drag up/down in the display', 'gain (the waveform follows the mouse); wheel: 0.5 dB steps'], ['Drag the ceiling line', 'output ceiling; Shift: fine; double-click: type (Tab: gain)'],
      ['Double-click the display', 'type the gain; Ctrl/Cmd-click: reset gain (on the line: ceiling)'], ['UNITY (click / hold)', 'latch / hear while held: the limiting at the loudness it came in'],
      ['Click a meter', 'reset that meter’s peak hold (IN, GR or OUT); P: all holds'], ['Click a loudness readout', 'reset the loudness measurement (also RESET or R)'],
      ['Up / Down  ·  PgUp / PgDn', 'gain ±0.5 dB (Shift ±0.1)  ·  ceiling ±0.1 dB'], ['Left / Right  ·  U', 'previous / next style  ·  unity listen on/off'], ['Right-click the display', 'reset loudness or holds, display range']]; },
  };
}});
})();
