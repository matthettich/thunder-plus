/* …pump pop-up windows, stage 3: what BAND, WIDE and HEAT share (matthettich/pump common/ui/MultibandUI.hpp and the
   crossover maths of common/dsp/Multiband.hpp). Band regions between draggable crossover handles, the strip of per-band
   S / M / B keys across the top of the display, and the multiband interaction set:
     drag a crossover handle (Shift: fine), double-click it to type (Tab: next crossover), wheel on it: 1/12 octave
     Alt + click-and-hold a band: hear only that band while held (slide across to switch bands; PumpFX.hold kind 0)
     click a band: select it; drag up/down in a band: its main amount; Ctrl/Cmd + drag up/down: its second amount
     wheel in a band: main amount (Shift: fine); double-click a band: type its main amount (Tab: second amount)
     right-click a band: band menu; keys 1-4 select, Left/Right step, S / M / B toggle the selected band, Esc unsolos
   S / M / B keys: click toggles, click-and-hold acts only while held, Ctrl/Cmd-click makes it exclusive.
   A plugin file calls PumpUI.Multiband(ui, cfg) in make() (after its own controls, as the plugin calls buildStrip())
   and forwards its hooks; see band.js, wide.js, heat.js and docs/PUMP-FX.md ("The pop-up kit"). */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, alpha} = U;
const LO = 15, HI = 22000, STRIP = 26, NB = 4, MIN_X = 20, MAX_X = 20000, MIN_RATIO = 1.2;
const FLAGS = ['solo', 'mute', 'bypass'], WHAT = ['Solo', 'Mute', 'Bypass'];
const TIPS = ['Solo this band (click: on/off · hold: only while held · Ctrl/Cmd+click: solo only this band)',
  'Mute this band (click: on/off · hold: only while held · Ctrl/Cmd+click: mute only this band)',
  'Bypass this band’s processing (click: on/off · hold: only while held · Ctrl/Cmd+click: only this band)'];
const NAMES = {1:['FULL'], 2:['LOW', 'HIGH'], 3:['LOW', 'MID', 'HIGH'], 4:['LOW', 'LO-MID', 'HI-MID', 'HIGH']};
const clipboard = {};   // copied band settings, per plugin type (as the plugin keeps one per plugin)

// Multiband.hpp effectiveCrossovers: the crossovers actually used (clamped, kept MIN_RATIO apart; unused ones above)
function effective(nb, x, sr){
  nb = clamp(nb, 1, NB); const nx = nb - 1, hi = Math.min(MAX_X, 0.45 * sr), out = [0, 0, 0];
  for (let k = 0; k < 3; k++) out[k] = clamp(x[k], MIN_X, hi);
  for (let k = 1; k < nx; k++) out[k] = Math.max(out[k], out[k - 1] * MIN_RATIO);
  for (let k = nx - 1; k >= 0; k--) out[k] = Math.min(out[k], hi / Math.pow(MIN_RATIO, nx - 1 - k));
  for (let k = 1; k < nx; k++) out[k] = Math.max(out[k], out[k - 1] * MIN_RATIO);
  for (let k = Math.max(nx, 1); k < 3; k++) out[k] = Math.min(hi, out[k - 1] * 2);
  return out;
}
// LR4 band magnitudes (not squared), as the digital filters respond; for any f the bands sum to 1
function lr4Lo(f, fc, sr){ if (f >= 0.4999 * sr) return 0; const r = Math.tan(Math.PI * f / sr) / Math.tan(Math.PI * Math.min(fc, 0.49 * sr) / sr), r2 = r * r; return 1 / (1 + r2 * r2); }
const lr4Hi = (f, fc, sr) => 1 - lr4Lo(f, fc, sr);
function bandMag(nb, e, k, f, sr){
  switch (nb){
    case 1: return k === 0 ? 1 : 0;
    case 2: return k === 0 ? lr4Lo(f, e[0], sr) : k === 1 ? lr4Hi(f, e[0], sr) : 0;
    case 3: if (k === 0) return lr4Lo(f, e[1], sr) * lr4Lo(f, e[0], sr); if (k === 1) return lr4Lo(f, e[1], sr) * lr4Hi(f, e[0], sr); return k === 2 ? lr4Hi(f, e[1], sr) : 0;
    default: if (k === 0) return lr4Lo(f, e[1], sr) * lr4Lo(f, e[0], sr); if (k === 1) return lr4Lo(f, e[1], sr) * lr4Hi(f, e[0], sr);
      if (k === 2) return lr4Hi(f, e[1], sr) * lr4Lo(f, e[2], sr); return lr4Hi(f, e[1], sr) * lr4Hi(f, e[2], sr);
  }
}
const kfmt = v => v >= 1000 ? +(v / 1000).toPrecision(3) + 'k' : String(Math.round(v));

/* cfg: {main(b) → symbol, second(b) → symbol, mainName, secondName, fields: the band's own parameter names (b1_<field>),
   stripValue(b), onSelect(b), mainFromDrag(b, start, up), secondFromDrag(b, start, up), mainWheel(b, v, dir, fine),
   regionGrab(b, x, y, mods) → true to own the drag, regionGrabDrag(x, y, mods), regionGrabEnd(), help: [[keys, what], …]} */
U.Multiband = function(ui, cfg){
  const bs = (b, f) => 'b' + (b + 1) + '_' + f;
  const DM = {none:0, xover:1, listen:2, main:3, second:4, grab:5};
  const mb = {LO, HI, STRIP, NB, disp:R(), sel:0, dmode:0, hoverXo:-1, hoverBand:-1, listenBand:-1, bandMag, sym:bs};
  let dragXo = -1, dragBand = -1, startV = 0, lastX = 0, lastY = 0, eff = 0, held = -1, lastSel = -1;
  const strip = [];

  // ---- geometry
  const rate = () => ui.sr || 48000;
  mb.rate = rate;
  mb.numBands = () => clamp(Math.round(ui.value('bands')), 1, NB);
  mb.effXo = () => effective(mb.numBands(), [ui.value('xover1'), ui.value('xover2'), ui.value('xover3')], rate());
  mb.xOf = f => ui.freqToX(mb.disp, f, LO, HI);
  mb.fOf = x => ui.xToFreq(mb.disp, x, LO, HI);
  mb.regionX = b => { const e = mb.effXo(), nb = mb.numBands(), d = mb.disp;
    return [b === 0 ? d.x : Math.max(d.x, mb.xOf(e[b - 1])), b >= nb - 1 ? d.r : Math.min(d.r, mb.xOf(e[b]))]; };
  mb.regionAt = x => { const e = mb.effXo(), nb = mb.numBands(); let b = 0; for (let k = 0; k < nb - 1; k++) if (x >= mb.xOf(e[k])) b = k + 1; return b; };
  mb.xoverTab = k => { const e = mb.effXo(), w = 62, x = mb.xOf(e[k]), d = mb.disp; let y = d.b - 40;
    if (k > 0 && x - mb.xOf(e[k - 1]) < w + 4 && (k % 2) === 1) y -= 22;   // stagger tabs that would overlap their left neighbour
    return R(Math.max(d.x + 2, Math.min(d.r - w - 2, x - w / 2)), y, w, 18); };
  mb.xoverAt = (x, y) => { const d = mb.disp; if (y < d.y + STRIP) return -1; const e = mb.effXo(), nb = mb.numBands(), tol = ui.narrow ? 10 : 6;
    let best = -1, bd = tol; for (let k = 0; k < nb - 1; k++){ if (mb.xoverTab(k).has(x, y)) return k; const dd = Math.abs(x - mb.xOf(e[k])); if (dd < bd){ bd = dd; best = k; } } return best; };
  mb.bandName = (b, nb = mb.numBands()) => (NAMES[nb] || NAMES[4])[Math.min(b, nb - 1)];
  mb.bandRange = b => { const e = mb.effXo(), nb = mb.numBands(); if (nb === 1) return 'full range';
    if (b === 0) return 'below ' + kfmt(e[0]) + ' Hz'; if (b === nb - 1) return 'above ' + kfmt(e[nb - 2]) + ' Hz'; return kfmt(e[b - 1]) + ' - ' + kfmt(e[b]) + ' Hz'; };

  // ---- selection, solo / mute / bypass
  const syncSel = () => { if (mb.sel !== lastSel){ lastSel = mb.sel; PumpFX.setSel(ui.t, ui.k, mb.sel); } };
  mb.select = b => { b = clamp(b, 0, mb.numBands() - 1); if (b === mb.sel){ syncSel(); return; } mb.sel = b; if (cfg.onSelect) cfg.onSelect(b); syncSel(); ui.dirty = true; };
  mb.flag = (b, t) => ui.value(bs(b, FLAGS[t])) >= 0.5;
  mb.anySolo = () => { for (let b = 0; b < mb.numBands(); b++) if (mb.flag(b, 0)) return true; return false; };
  const setListening = b => { held = b; ui.hold(0, b); };
  mb.listening = () => held;
  mb.stopListening = () => { if (held >= 0){ setListening(-1); ui.status = ''; } };
  // Ctrl/Cmd-click on S / M / B: this band only (again: clear them all)
  mb.exclusive = (b, t) => {
    const nb = mb.numBands(); let only = mb.flag(b, t); for (let k = 0; k < nb; k++) if (k !== b && mb.flag(k, t)) only = false;
    const o = {}; for (let k = 0; k < NB; k++) o[bs(k, FLAGS[t])] = only ? 0 : (k === b ? 1 : 0); ui.setMany(o);
    ui.status = only ? WHAT[t] + ' cleared on all bands' : WHAT[t] + ': band ' + (b + 1) + ' only'; };
  // the S / M / B keys in the strip (call after the plugin's own controls, as buildStrip())
  for (let b = 0; b < NB; b++){ strip[b] = [];
    for (let t = 0; t < 3; t++){ const bt = ui.toggle(bs(b, FLAGS[t]), 'SMB'[t]); bt.small = true; bt.holdMomentary = true; bt.tip = TIPS[t]; bt.onExclusive = () => mb.exclusive(b, t); bt.strip = true; strip[b][t] = bt; } }
  // the band panel's SOLO / MUTE / BYPASS keys: momentary when held, Ctrl/Cmd-click: only this band
  mb.panelFlags = (solo, mute, byp) => [solo, mute, byp].forEach((bt, t) => { bt.small = true; bt.holdMomentary = true; bt.onExclusive = () => mb.exclusive(mb.sel, t); });
  mb.layoutStrip = () => {
    const nb = mb.numBands(), d = mb.disp, bw = 19, gap = 4, need = 3 * bw + 2 * gap + 34;
    for (let b = 0; b < NB; b++){ let x0 = 0, x1 = 0; if (b < nb) [x0, x1] = mb.regionX(b);
      const show = b < nb && x1 - x0 >= need; let x = x1 - 7 - 3 * bw - 2 * gap;
      for (let t = 0; t < 3; t++){ const bt = strip[b][t]; bt.visible = show; bt.r = R(x, d.y + 4, bw, 17); x += bw + gap; } }
  };

  // ---- drawing
  // region tints and the strip (after screenBox, before the plugin's content)
  mb.drawUnder = () => {
    mb.layoutStrip(); const t = ui.th, d = mb.disp, nb = mb.numBands();
    for (let b = 0; b < nb; b++){ const [x0, x1] = mb.regionX(b);
      if (b === mb.sel) ui.fill(R(x0, d.y + STRIP, x1 - x0, d.h - STRIP), alpha(t.neon, t.dark ? 0.07 : 0.11));
      else if (b === mb.hoverBand && mb.dmode === DM.none) ui.fill(R(x0, d.y + STRIP, x1 - x0, d.h - STRIP), alpha(t.ink, 0.025));
      ui.fill(R(x0, d.y, x1 - x0, STRIP), b === mb.sel ? alpha(t.neon, t.dark ? 0.30 : 0.40) : alpha(t.panel2, 0.8)); }
    ui.line(d.x, d.y + STRIP, d.r, d.y + STRIP, t.dim, 1);
  };
  // crossover handles, strip labels and the solo / listen dimming (after the plugin's content)
  mb.drawOver = () => {
    const t = ui.th, d = mb.disp, nb = mb.numBands(), e = mb.effXo();
    const ls = mb.dmode === DM.listen ? mb.listenBand : held, solo = mb.anySolo();
    for (let b = 0; b < nb; b++){ const [x0, x1] = mb.regionX(b);
      const heard = ls >= 0 ? b === ls : (solo ? mb.flag(b, 0) : !mb.flag(b, 1));
      if (!heard) ui.fill(R(x0, d.y + STRIP, x1 - x0, d.h - STRIP), alpha(t.screen, ls >= 0 ? 0.72 : 0.55));
      if (mb.flag(b, 2) && x1 - x0 > 60) ui.text((x0 + x1) / 2, d.y + STRIP + 14, 'BYPASSED', 'label', 10, t.ink2, 'center', 1.5); }
    for (let k = 0; k < nb - 1; k++){
      const x = mb.xOf(e[k]), hot = k === mb.hoverXo || (mb.dmode === DM.xover && dragXo === k);
      ui.line(x, d.y, x, d.b, hot ? t.ink : alpha(t.ink, t.dark ? 0.55 : 0.45), hot ? 2 : 1.4);
      const tb = mb.xoverTab(k); ui.fill(R(tb.x + 2, tb.y + 2, tb.w, tb.h), t.dim); ui.fill(tb, hot ? t.ink : t.bg); ui.stroke(tb, t.ink, 1.2);
      ui.text(tb.cx, tb.cy + 0.5, ui.fmt('xover' + (k + 1), e[k]), 'mono', 10, hot ? t.on : t.ink, 'center'); }
    for (let b = 0; b < nb; b++){ const [x0, x1] = mb.regionX(b), w = x1 - x0; if (w < 22) continue;
      const key = R(x0 + 5, d.y + 4, 17, 17), isSel = b === mb.sel; ui.fill(key, isSel ? t.ink : t.bg); ui.stroke(key, t.ink, 1.2);
      ui.text(key.cx, key.cy + 0.5, String(b + 1), 'bold', 10.5, isSel ? t.on : t.ink, 'center');
      const nbtn = strip[b][0].visible ? 3 : 0, right = x1 - 7 - (nbtn ? nbtn * 19 + (nbtn - 1) * 4 + 6 : 0);
      const name = mb.bandName(b, nb), val = cfg.stripValue ? cfg.stripValue(b) : '', avail = right - (key.r + 6);
      const nw = ui.textW(name, 'label', 10.5, 1), vw = ui.textW(val, 'mono', 10.5);
      if (avail > nw + vw + 10){ ui.text(key.r + 6, key.cy + 0.5, name, 'label', 10.5, t.ink, 'left', 1); ui.text(right, key.cy + 0.5, val, 'mono', 10.5, t.ink, 'right'); }
      else if (avail > vw + 2) ui.text(right, key.cy + 0.5, val, 'mono', 10.5, t.ink, 'right'); }
    if (ls >= 0 && ls < nb){ const [x0, x1] = mb.regionX(ls), s = 'SOLO BAND ' + (ls + 1) + (mb.dmode === DM.listen ? '  (ALT)' : ''), tw = ui.textW(s, 'label', 11.5, 1) + 16;
      const lb = R(Math.max(d.x + 6, Math.min(d.r - tw - 6, (x0 + x1) / 2 - tw / 2)), d.y + STRIP + 8, tw, 21);
      ui.fill(lb, t.second); ui.text(lb.cx, lb.cy + 0.5, s, 'label', 11.5, t.dark ? t.on : t.bg, 'center', 1); }
  };

  // ---- mouse
  const xoLimits = k => { const e = mb.effXo(), nb = mb.numBands(); return [k > 0 ? e[k - 1] * MIN_RATIO : MIN_X, k < nb - 2 ? e[k + 1] / MIN_RATIO : Math.min(MAX_X, 0.45 * rate())]; };
  mb.openXoverEntry = k => { const ring = []; for (let j = 0; j < mb.numBands() - 1; j++) ring.push(['xover' + (j + 1), mb.xoverTab(j)]);
    ui.openTextEntry('xover' + (k + 1), mb.xoverTab(k), ring); ui.status = 'Type a frequency (120, 2k, A4); Tab: next crossover; Enter: done'; };
  mb.down = (x, y, btn, m, dbl) => {
    const d = mb.disp; if (!d.has(x, y)) return false;
    const b = mb.regionAt(x);
    if (btn === 2){ mb.select(b); bandMenu(b, x, y); return false; }
    if (btn !== 0) return false;
    if (m.alt){ mb.dmode = DM.listen; mb.listenBand = b; mb.select(b); setListening(b); ui.status = 'Listening to band ' + (b + 1) + ' (' + mb.bandRange(b) + ') while held'; return true; }
    const xo = mb.xoverAt(x, y);
    if (xo >= 0){ if (dbl){ mb.openXoverEntry(xo); return false; }
      mb.dmode = DM.xover; dragXo = xo; startV = mb.effXo()[xo]; lastX = x; eff = 0; ui.begin(); return true; }
    if (y < d.y + STRIP){ mb.select(b); return false; }
    mb.select(b);
    if (dbl && !m.cmd){ const r = R(Math.max(d.x + 4, Math.min(d.r - 104, x - 50)), Math.max(d.y + STRIP + 4, y - 30), 100, 22);
      ui.openTextEntry(cfg.main(b), r, [[cfg.main(b), r], [cfg.second(b), r]]); ui.status = 'Type the band’s ' + cfg.mainName + '; Tab: ' + cfg.secondName + '; Enter: done'; return false; }
    dragBand = b; lastY = y; eff = 0;
    if (m.cmd){ mb.dmode = DM.second; startV = ui.value(cfg.second(b)); ui.begin(); return true; }
    if (cfg.regionGrab && cfg.regionGrab(b, x, y, m)){ mb.dmode = DM.grab; return true; }
    mb.dmode = DM.main; startV = ui.value(cfg.main(b)); ui.begin(); return true;
  };
  mb.drag = (x, y, m) => {
    const fine = m.shift ? 0.15 : 1, d = mb.disp;
    switch (mb.dmode){
      case DM.listen: { const b = mb.regionAt(clamp(x, d.x, d.r - 1)); if (b !== mb.listenBand){ mb.listenBand = b; setListening(b); mb.select(b); ui.status = 'Listening to band ' + (b + 1) + ' (' + mb.bandRange(b) + ')'; } return; }
      case DM.xover: { eff += (x - lastX) * fine; lastX = x; const [lo, hi] = xoLimits(dragXo);
        const f = clamp(startV * Math.pow(HI / LO, eff / d.w), lo, hi), s = 'xover' + (dragXo + 1); ui.setParam(s, f); ui.status = 'Crossover ' + (dragXo + 1) + '  ' + ui.fmt(s, f); return; }
      case DM.main: eff += (lastY - y) * fine; lastY = y; ui.setParam(cfg.main(dragBand), mainFromDrag(dragBand, startV, eff)); ui.status = ui.paramStatus(cfg.main(dragBand)); return;
      case DM.second: eff += (lastY - y) * fine; lastY = y; ui.setParam(cfg.second(dragBand), secondFromDrag(dragBand, startV, eff)); ui.status = ui.paramStatus(cfg.second(dragBand)); return;
      case DM.grab: if (cfg.regionGrabDrag) cfg.regionGrabDrag(x, y, m); return;
    }
  };
  mb.up = () => {
    switch (mb.dmode){
      case DM.listen: mb.stopListening(); mb.listenBand = -1; break;
      case DM.xover: case DM.main: case DM.second: ui.end(); break;
      case DM.grab: if (cfg.regionGrabEnd) cfg.regionGrabEnd(); break;
    }
    mb.dmode = DM.none; dragXo = -1;
  };
  const normStep = (sym, start, up) => ui.spec(sym) ? PumpFX.fromNorm(ui.type, sym, clamp(PumpFX.toNorm(ui.type, sym, start) + up * 0.004, 0, 1)) : start;
  const mainFromDrag = (b, s, up) => cfg.mainFromDrag ? cfg.mainFromDrag(b, s, up) : normStep(cfg.main(b), s, up);
  const secondFromDrag = (b, s, up) => cfg.secondFromDrag ? cfg.secondFromDrag(b, s, up) : normStep(cfg.second(b), s, up);
  mb.hover = (x, y) => {
    const d = mb.disp; if (!d.has(x, y)){ mb.hoverXo = mb.hoverBand = -1; return; }
    mb.hoverXo = mb.xoverAt(x, y); mb.hoverBand = mb.regionAt(x);
    if (mb.hoverXo >= 0){ const s = 'xover' + (mb.hoverXo + 1); ui.status = ui.spec(s).n + '  ' + ui.fmt(s) + '    drag (Shift: fine) · double-click: type · wheel: 1/12 octave'; }
    else if (y >= d.y + STRIP) ui.status = 'Band ' + (mb.hoverBand + 1) + ' (' + mb.bandRange(mb.hoverBand) + ')    drag/wheel: ' + cfg.mainName + ' · Ctrl/Cmd+drag: ' + cfg.secondName + ' · Alt-hold: solo · right-click: menu';
  };
  mb.leave = () => { mb.hoverXo = mb.hoverBand = -1; };
  mb.wheel = (x, y, dy, m) => {
    const d = mb.disp; if (!d.has(x, y)) return false; const dir = dy > 0 ? 1 : -1, fine = m.shift;
    const xo = mb.xoverAt(x, y);
    if (xo >= 0){ const [lo, hi] = xoLimits(xo), s = 'xover' + (xo + 1), f = clamp(mb.effXo()[xo] * Math.pow(2, dir * (fine ? 1 / 48 : 1 / 12)), lo, hi);
      ui.setGesture(s, f); ui.status = 'Crossover ' + (xo + 1) + '  ' + ui.fmt(s, f); return true; }
    if (y < d.y + STRIP) return false;
    const b = mb.regionAt(x); mb.select(b); const s = cfg.main(b), v = ui.value(s);
    ui.setGesture(s, cfg.mainWheel ? cfg.mainWheel(b, v, dir, fine) : PumpFX.fromNorm(ui.type, s, clamp(PumpFX.toNorm(ui.type, s, v) + dir * (fine ? 0.004 : 0.025), 0, 1)));
    ui.status = ui.paramStatus(s); return true;
  };

  // ---- keys and the band menu
  mb.key = (e, m) => {
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key, nb = mb.numBands();
    if (!m.cmd && !m.alt && k >= '1' && k <= '4'){ const b = +k - 1; if (b < nb){ mb.select(b); ui.status = 'Band ' + (b + 1) + ' (' + mb.bandRange(b) + ')'; } return true; }
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){ mb.select((mb.sel + (e.key === 'ArrowLeft' ? nb - 1 : 1)) % nb); return true; }
    if (!m.cmd && (k === 's' || k === 'm' || k === 'b')){ const s = bs(mb.sel, FLAGS['smb'.indexOf(k)]); ui.setGesture(s, ui.value(s) >= 0.5 ? 0 : 1); return true; }
    if (e.key === 'Escape'){
      // the plugin's Esc clears every solo; with nothing to clear it closes the window (the kit's Esc)
      if (!mb.anySolo() && held < 0) return false;
      const o = {}; for (let b = 0; b < NB; b++) o[bs(b, 'solo')] = 0; ui.setMany(o); mb.stopListening(); ui.status = 'Solo cleared'; return true; }
    return false;
  };
  mb.keyUp = e => { if (e.key === 'Alt' && mb.dmode === DM.listen){ mb.stopListening(); mb.listenBand = -1; mb.dmode = DM.none; ui.bodyActive = false; ui.dirty = true; } };
  const isFlag = f => FLAGS.includes(f);
  function bandMenu(b, x, y){
    const clip = clipboard[ui.type];
    const it = [(mb.flag(b, 0) ? 'Unsolo' : 'Solo') + ' band ' + (b + 1), 'Solo only this band', (mb.flag(b, 1) ? 'Unmute' : 'Mute') + ' band ' + (b + 1),
      (mb.flag(b, 2) ? 'Enable' : 'Bypass') + ' band ' + (b + 1), '-', 'Copy band settings', clip ? 'Paste band settings' : 'Paste (nothing copied)', 'Reset band'];
    ui.openMenu(R(x, y, 10, 10), it, -1, i => {
      const s = f => bs(b, f);
      if (i === 0) ui.setGesture(s('solo'), mb.flag(b, 0) ? 0 : 1);
      else if (i === 1) mb.exclusive(b, 0);
      else if (i === 2) ui.setGesture(s('mute'), mb.flag(b, 1) ? 0 : 1);
      else if (i === 3) ui.setGesture(s('bypass'), mb.flag(b, 2) ? 0 : 1);
      else if (i === 5){ const c = {}; for (const f of cfg.fields) c[f] = ui.value(s(f)); clipboard[ui.type] = c; ui.status = 'Copied band ' + (b + 1); }
      else if (i === 6){ if (!clip) return; const o = {}; for (const f of cfg.fields) if (!isFlag(f) && clip[f] !== undefined) o[s(f)] = clip[f]; ui.setMany(o); ui.status = 'Pasted into band ' + (b + 1); }
      else if (i === 7){ const o = {}; for (const f of cfg.fields) o[s(f)] = ui.spec(s(f)).def; ui.setMany(o); ui.status = 'Band ' + (b + 1) + ' reset'; }
      if (cfg.onSelect) cfg.onSelect(mb.sel);
    });
  }
  mb.help = () => [
    ['Alt + hold a band', 'solo it while held (slide across to switch bands)'],
    ['Click a band · 1-4 · Left/Right', 'select it'],
    ['Drag / wheel in a band', cfg.mainName + ' (Shift: fine)'],
    ['Ctrl/Cmd + drag up/down in a band', cfg.secondName],
    ['Double-click a band', 'type its ' + cfg.mainName + ' (Tab: ' + cfg.secondName + ')'],
    ['Drag a crossover', 'move it (Shift: fine) · wheel: 1/12 octave'],
    ['Double-click a crossover', 'type it: 120, 2k, A4 (Tab: next crossover)'],
    ['S · M · B keys in the display', 'solo · mute · bypass (click: on/off · hold: while held)'],
    ['Ctrl/Cmd + click S / M / B', 'only this band (again: clear them all)'],
    ['S / M / B on the keyboard · Esc', 'toggle them on the selected band · clear every solo (then Esc closes)'],
    ['Right-click a band (long-press)', 'solo, mute, bypass, copy / paste settings, reset'],
    ...(cfg.help || [])];
  // the slot changed elsewhere: follow the FX screen's BAND, keep the selection in range
  mb.reload = () => {
    const info = PumpFX.get(ui.t, ui.k), nb = mb.numBands();
    if (info && info.sel !== lastSel && info.sel >= 0){ mb.sel = clamp(info.sel, 0, nb - 1); lastSel = info.sel; }
    if (mb.sel >= nb) mb.sel = nb - 1;
    if (held >= nb){ mb.stopListening(); }
    mb.layoutStrip();
    if (cfg.onSelect) cfg.onSelect(mb.sel);
  };
  mb.close = () => { ui.hold(0, -1); held = -1; };
  mb.blur = () => { if (mb.dmode === DM.listen){ mb.stopListening(); mb.listenBand = -1; mb.dmode = DM.none; } };
  mb.DM = DM;
  return mb;
};
U.mbEffective = effective; U.mbBandMag = bandMag; U.MB_STRIP = STRIP;
})();
