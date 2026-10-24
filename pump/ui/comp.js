/* …pump COMP pop-up (stage 2): plugins/comp/UIComp.cpp in the browser. The scrolling level / gain-reduction history
   with the draggable threshold line, the transfer-curve inset, In / Out / GR meters, the sidechain (Thunder's sends and
   tracks feed the External sidechain) and the controls. */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, mix, alpha, Dial, Feed} = U;
const STYLES = ['Clean', 'Glue', 'Opto', 'Punch', 'Vari-Mu', 'Pump'], CHANS = ['Stereo', 'Mid/Side', 'Mid only', 'Side only'], OS = ['1x', '2x', '4x'];
const STYLE_TIP = ['Clean: feed-forward peak detector, exact curve, lowest distortion', 'Glue: bus compression; release slows as it compresses deeper',
  'Opto: smooth RMS leveling, fast-then-slow release, softer at depth', 'Punch: feedback design that lets the first milliseconds of transients through',
  'Vari-Mu: tube; the ratio rises with level, gentle colour', 'Pump: aggressive ducking with a breathing release (try the external sidechain)'];
const hpfOn = f => f > 20.5, lpfOn = f => f < 19900;
const RANGE_KEY = 'thunderplus-pump-comp-range-v1';
// CompEngine.hpp Curve: the gain change (dB, <= 0) for a detector level
function curve(style, T, ratio, knee, range){
  const s = ratio >= 99.95 ? 1 : 1 - 1 / Math.max(1, ratio), W = Math.max(0, knee), rg = Math.max(0, range);
  const std = L => { const o = L - T; if (W > 0 && Math.abs(o) < W / 2) return -s * (o + W / 2) ** 2 / (2 * W); return o > 0 ? -s * o : 0; };
  return L => { let g; if (style === 2){ g = std(L); g = g / (1 + Math.abs(g) / 30); } else if (style === 4){ const Wk = 12 + W, u = L - T + Wk / 2; g = u > 0 ? -s * u * u / (u + Wk) : 0; } else g = std(L); return Math.max(g, -rg); };
}
// the plugin's value text (more precise than the generic one, OFF end stops)
function compFmt(sym, v){
  const f = (x, d) => x.toFixed(d);
  switch (sym){
    case 'threshold': case 'knee': return f(v, 1) + ' dB';
    case 'range': return v >= 59.95 ? '60 dB' : f(v, v < 9.95 ? 1 : 0) + ' dB';
    case 'attack': return (v < 0.0995 ? f(v, 3) : v < 9.95 ? f(v, 2) : v < 99.5 ? f(v, 1) : f(v, 0)) + ' ms';
    case 'hold': return v < 0.05 ? '0 ms' : f(v, v < 9.95 ? 1 : 0) + ' ms';
    case 'lookahead': return v < 0.005 ? '0 ms' : f(v, 1) + ' ms';
    case 'sc_hpf': if (!hpfOn(v)) return 'OFF'; break;
    case 'sc_lpf': if (!lpfOn(v)) return 'OFF'; break;
  }
  return PumpFX.format('comp', sym, v);
}

U.define('comp', {W:1000, H:620, nW:480, nH:930, bypass:true, back:8191, make(ui){
  let graph = R(), inset = R(), inM = [R(), R()], outM = [R(), R()], grM = R(), inHit = R(), outHit = R(), grHit = R(), detBar = R(), scCapY = 0, sepY = 0, sepX = 0;
  let dispRange = (() => { try { const v = +localStorage.getItem(RANGE_KEY); return [24, 36, 48, 60, 72].includes(v) ? v : 48; } catch(e){ return 48; } })();
  const inLvl = [-120, -120], outLvl = [-120, -120], pin = [0, 0], pout = [0, 0]; let inHold = -120, outHold = -120, inHoldT = 0, outHoldT = 0;
  let grNow = 0, grHold = 0, grHoldT = 0, detNow = -200, scQuiet = 9, pgr = 0, pdet = -200, psc = 0, scConn = 0, dmode = 0, hoverThresh = false, lastX = 0, lastY = 0, dragStart = 0;
  const hIn = new Feed(8192), hOut = new Feed(8192), hGr = new Feed(8192), hDet = new Feed(8192);
  const cin = new Float32Array(1100), cout = new Float32Array(1100), cgr = new Float32Array(1100);
  const TOP = 3, DECAY = 1.1, IN_LO = -60, IN_HI = 0, M_TOP = 3, M_BOT = -60, GR_RANGE = 24;

  const dial = (sym, label, group, tip, bip = false, size = 'm') => { const d = ui.dial(sym, label, size, bip); d.tabGroup = group; d.tip = tip; d.fmt = v => compFmt(sym, v); return d; };
  const styleSeg = ui.seg('style', ['CLEAN', 'GLUE', 'OPTO', 'PUNCH', 'VARI-MU', 'PUMP']); styleSeg.tip = 'Compressor style';
  const autoRelBtn = ui.toggle('auto_release', 'AUTO REL'); autoRelBtn.small = true; autoRelBtn.tip = 'Auto release: program-dependent (fast after short peaks, slow under sustained compression)';
  const thr = dial('threshold', 'THRESHOLD', 1, 'Threshold: where compression starts (or drag the line in the graph)'), ratio = dial('ratio', 'RATIO', 1, 'Ratio (INF at the end): or Ctrl/Cmd + drag up/down on the transfer curve');
  const knee = dial('knee', 'KNEE', 1, 'Knee width: soft transition around the threshold (or mouse wheel on the transfer curve)'), range = dial('range', 'RANGE', 1, 'Range: the most gain reduction allowed');
  const atk = dial('attack', 'ATTACK', 1, 'Attack: how fast it reacts'), rel = dial('release', 'RELEASE', 1, 'Release: how fast it recovers');
  const hold = dial('hold', 'HOLD', 1, 'Hold: keeps the reduction this long before releasing'), la = dial('lookahead', 'LOOKAHEAD', 1, 'Lookahead: reacts before the audio arrives (adds this much latency)');
  const chanMenu = ui.menuBtn('channel_mode', 'CH'); chanMenu.tip = 'Channels: Stereo (L/R), Mid/Side (both, linked by Stereo Link), Mid only or Side only';
  const osMenu = ui.menuBtn('oversampling', 'OS'); osMenu.tip = 'Oversampling: detector, gain and tube colour at 2x or 4x (adds a little latency)';
  const autoGainBtn = ui.toggle('auto_makeup', 'AUTO GAIN'); autoGainBtn.small = true; autoGainBtn.tip = 'Auto makeup gain: compensates for the curve (half the reduction at 0 dBFS)';
  const inD = dial('in_gain', 'INPUT', 2, 'Input gain (before the compressor and the dry signal)', true), outD = dial('out_gain', 'OUTPUT', 2, 'Output gain', true);
  const mixD = dial('mix', 'MIX', 2, 'Mix: dry/wet for parallel compression (latency-aligned)'), linkD = dial('stereo_link', 'LINK', 2, 'Stereo link: 0% each channel on its own, 100% both follow the louder');
  // the sidechain: Internal, or External fed by one of Thunder's sends or tracks (the FX screen's SC FROM)
  const scMenu = ui.menuBtn(null); const names = () => PumpFX.sckNames();
  scMenu.itemsFn = () => ['Internal', '#EXTERNAL, FED BY', ...names().map((n, i) => i ? n.charAt(0) + n.slice(1).toLowerCase() : 'Nothing (silent)')];
  scMenu.getFn = () => Math.round(ui.value('sc_source')) === 0 ? 0 : 2 + sck();
  scMenu.setFn = i => { if (i === 0) ui.setGesture('sc_source', 0); else if (i >= 2){ if (i - 2 !== sck()) PumpFX.setSck(ui.t, ui.k, i - 2); ui.setGesture('sc_source', 1); } };
  scMenu.textFn = () => Math.round(ui.value('sc_source')) === 0 ? 'Internal' : 'Ext: ' + (sck() ? names()[sck()].charAt(0) + names()[sck()].slice(1).toLowerCase() : 'nothing');
  scMenu.tip = 'Sidechain: the input itself, or External: one of Thunder’s sends or tracks (the FX screen’s SC FROM)';
  const sck = () => { const i = PumpFX.get(ui.t, ui.k); return i ? i.sck | 0 : 0; };
  const scHp = dial('sc_hpf', 'HP', 3, 'Sidechain high-pass: keeps lows from triggering compression (left end = off)', false, 's');
  const scLp = dial('sc_lpf', 'LP', 3, 'Sidechain low-pass (right end = off)', false, 's');
  const scGain = dial('sc_gain', 'SC GAIN', 3, 'Sidechain gain: drives the detector harder or softer', true, 's');
  const listenBtn = ui.button('LISTEN', () => ui.setParam('sc_listen', ui.value('sc_listen') < 0.5 ? 1 : 0), () => ui.value('sc_listen') >= 0.5);
  listenBtn.holdMomentary = true; listenBtn.small = true; listenBtn.tip = 'Hear the (filtered) sidechain: click toggles, click-and-hold listens while held';
  const rangeBtn = ui.button(dispRange + ' dB', () => { const r = [24, 36, 48, 60, 72]; ui.openMenu(rangeBtn.r, r.map(v => v + ' dB'), Math.max(0, r.indexOf(dispRange)), i => { dispRange = r[i]; rangeBtn.label = dispRange + ' dB'; try { localStorage.setItem(RANGE_KEY, String(dispRange)); } catch(e){} }); });
  rangeBtn.small = true; rangeBtn.tip = 'Display range of the history graph';

  const V = s => ui.value(s), fmt = s => compFmt(s, V(s));
  const pxPerDb = () => graph.h / dispRange, yOfDb = db => graph.y + (TOP - db) * pxPerDb(), dbOfY = y => TOP - (y - graph.y) / pxPerDb(), yOfGr = gr => graph.y + Math.min(0, gr) * -pxPerDb();
  const insetX = d => inset.x + (d - IN_LO) / (IN_HI - IN_LO) * inset.w, insetY = d => inset.b - (d - IN_LO) / (IN_HI - IN_LO) * inset.h, insetDbOfX = x => IN_LO + (x - inset.x) / inset.w * (IN_HI - IN_LO);
  const cv = () => curve(Math.round(V('style')), V('threshold'), V('ratio'), V('knee'), V('range'));
  const external = () => Math.round(V('sc_source')) === 1, quiet = () => scQuiet > 0.6 || !scConn;
  const threshTag = () => { const s = 'THRESHOLD  ' + fmt('threshold'), tw = ui.textW(s, 'label', 11, 0.8) + 16, ty = yOfDb(V('threshold')); let y = ty - 20; if (y < graph.y + 4) y = ty + 4; return R(graph.x + 64, y, tw, 18); };
  const nearThresh = (x, y) => graph.has(x, y) && (Math.abs(y - yOfDb(V('threshold'))) < (ui.narrow ? 12 : 7) || threshTag().has(x, y));

  function drawHistory(){
    const t = ui.th, g = ui.g; ui.screenBox(graph);
    ui.clip(R(graph.x + 1, graph.y + 1, graph.w - 2, graph.h - 2), () => {
      const th = V('threshold'); let kw = V('knee'); if (Math.round(V('style')) === 4) kw += 12;
      if (kw > 0.1){ const yA = yOfDb(th + kw / 2), yT = yOfDb(th), yB = yOfDb(th - kw / 2), c0 = css(alpha(t.ink, 0)), c1 = css(alpha(t.ink, t.dark ? 0.07 : 0.05));
        let gr = g.createLinearGradient(0, yA, 0, yT); gr.addColorStop(0, c0); gr.addColorStop(1, c1); g.fillStyle = gr; g.fillRect(graph.x, yA, graph.w, yT - yA);
        gr = g.createLinearGradient(0, yT, 0, yB); gr.addColorStop(0, c1); gr.addColorStop(1, c0); g.fillStyle = gr; g.fillRect(graph.x, yT, graph.w, yB - yT); }
      const stepDb = dispRange <= 36 ? 6 : 12;
      for (let db = 0; db > TOP - dispRange; db -= stepDb) ui.line(graph.x, yOfDb(db), graph.r, yOfDb(db), db === 0 ? alpha(t.ink, 0.22) : t.grid, 1);
      const cols = Math.min(1100, Math.floor(graph.w)), step = 3;
      if (hDet.any){
        let end = hDet.pos; end -= end % step; const need = cols * step;
        for (let k = 0; k < cols; k++){ let a = -200, b = -200, gg = 0;
          for (let j = 0; j < step; j++){ const idx = end - need + k * step + j; if (idx < 0) continue; a = Math.max(a, hIn.at(idx)); b = Math.max(b, hOut.at(idx)); gg = Math.min(gg, hGr.at(idx)); }
          if (k > 0){ a = Math.max(a, cin[k - 1] - DECAY); b = Math.max(b, cout[k - 1] - DECAY); } cin[k] = a; cout[k] = b; cgr[k] = gg; }
        for (let k = 0; k < cols; k++){ const i0 = end - need + k * step; if (i0 >= 0 && (i0 % 400) < step) ui.line(graph.x + k + 0.5, graph.y, graph.x + k + 0.5, graph.b, t.grid, 1); }
        const yc = db => Math.min(graph.b + 2, Math.max(graph.y, yOfDb(db)));
        const area = v => { g.beginPath(); g.moveTo(graph.x, graph.b + 2); for (let k = 0; k < cols; k++) g.lineTo(graph.x + k + 0.5, yc(v[k])); g.lineTo(graph.r, graph.b + 2); g.closePath(); };
        const trace = v => { g.beginPath(); for (let k = 0; k < cols; k++) k ? g.lineTo(graph.x + k + 0.5, yc(v[k])) : g.moveTo(graph.x + 0.5, yc(v[k])); };
        area(cin); g.fillStyle = css(alpha(t.ink, t.dark ? 0.13 : 0.09)); g.fill();
        area(cout); g.fillStyle = css(alpha(t.neon, t.dark ? 0.30 : 0.42)); g.fill();
        trace(cout); g.strokeStyle = css(t.dark ? t.neon : mix(t.neon, t.ink, 0.40)); g.lineWidth = 1.4; g.lineJoin = 'round'; g.stroke();
        trace(cin); g.strokeStyle = css(alpha(t.ink, t.dark ? 0.45 : 0.38)); g.lineWidth = 1; g.stroke();
        g.beginPath(); g.moveTo(graph.x, graph.y); for (let k = 0; k < cols; k++) g.lineTo(graph.x + k + 0.5, yOfGr(cgr[k])); g.lineTo(graph.r, graph.y); g.closePath(); g.fillStyle = css(alpha(t.second, t.dark ? 0.16 : 0.13)); g.fill();
        g.beginPath(); for (let k = 0; k < cols; k++) k ? g.lineTo(graph.x + k + 0.5, yOfGr(cgr[k])) : g.moveTo(graph.x + 0.5, yOfGr(cgr[k])); g.strokeStyle = css(t.second); g.lineWidth = 2.6; g.stroke();
      }
      for (let db = 0; db > TOP - dispRange + 2; db -= stepDb){ const s = db === 0 ? '0 dB' : String(db), tw = ui.textW(s, 'mono', 9.5);
        ui.fill(R(graph.x + 4, yOfDb(db) - 14, tw + 6, 12), alpha(t.screen, 0.85)); ui.text(graph.x + 7, yOfDb(db) - 8, s, 'mono', 9.5, t.ink2, 'left'); }
      const ty = yOfDb(th), hot = dmode === 1 || hoverThresh;
      ui.line(graph.x, ty, graph.r, ty, hot ? t.ink : alpha(t.ink, 0.75), hot ? 2 : 1.5);
      const tag = threshTag(); ui.fill(tag, t.ink); ui.text(tag.cx, tag.cy + 0.5, 'THRESHOLD  ' + fmt('threshold'), 'label', 11, t.on, 'center', 0.8);
      if (external() && quiet()){ const s = 'EXTERNAL SIDECHAIN: NO SIGNAL', tw = ui.textW(s, 'label', 11, 1.2) + 20, b = R(graph.cx - tw / 2, graph.b - 40, tw, 24);
        ui.fill(b, t.bg); ui.stroke(b, t.red, 1.5); ui.text(b.cx, b.cy + 0.5, s, 'label', 11, t.red, 'center', 1.2); }
      if (V('sc_listen') >= 0.5){ const s = 'LISTENING TO THE SIDECHAIN', tw = ui.textW(s, 'label', 11, 1.2) + 20, b = R(graph.cx - tw / 2, graph.y + 10, tw, 24);
        ui.fill(b, t.second); ui.text(b.cx, b.cy + 0.5, s, 'label', 11, t.bg, 'center', 1.2); }
    });
    const ly = graph.b - 15, items = [['GAIN REDUCTION', t.second, true], ['OUTPUT', alpha(t.neon, t.dark ? 0.7 : 0.8), false], ['INPUT', alpha(t.ink, t.dark ? 0.3 : 0.22), false]];
    const lw = items.reduce((s, it) => s + ui.textW(it[0], 'label', 10, 1) + 32, 2); const lb = R(graph.r - 8 - lw, ly - 10, lw, 20);
    ui.fill(lb, alpha(t.screen, 0.92)); ui.stroke(lb, t.dim, 1);
    let lx = graph.r - 16;
    for (const [s, c, ln] of items){ const tw = ui.textW(s, 'label', 10, 1); ui.text(lx, ly, s, 'label', 10, t.ink2, 'right', 1); lx -= tw + 6;
      if (ln) ui.line(lx - 12, ly, lx, ly, c, 2.6); else ui.fill(R(lx - 12, ly - 4, 12, 8), c); lx -= 26; }
  }
  function drawDetector(){
    const t = ui.th, b = detBar; ui.text(b.x, b.y - 10, 'DETECTOR', 'label', 10, t.ink2, 'left', 1);
    ui.text(b.r, b.y - 10, detNow < -99 ? '-INF' : detNow.toFixed(1) + ' dB', 'mono', 10, t.ink, 'right'); ui.fill(b, t.panel2);
    const xOf = d => b.x + (clamp(d, -60, 0) + 60) / 60 * b.w, th = V('threshold');
    if (detNow > -60){ const x1 = xOf(detNow), xt = xOf(th); ui.fill(R(b.x, b.y, Math.min(x1, xt) - b.x, b.h), t.neon); if (x1 > xt) ui.fill(R(xt, b.y, x1 - xt, b.h), t.second); }
    ui.stroke(b, t.ink, 1); const xt = xOf(th); ui.line(xt, b.y - 3, xt, b.b + 3, t.ink, 2);
  }
  function drawMeters(){
    const t = ui.th, m = (r, l) => ui.drawLevelMeter(r, l, -200, M_BOT, M_TOP);
    m(inM[0], inLvl[0]); m(inM[1], inLvl[1]); m(outM[0], outLvl[0]); m(outM[1], outLvl[1]);
    const yLv = (r, d) => r.b - (clamp(d, M_BOT, M_TOP) - M_BOT) / (M_TOP - M_BOT) * r.h;
    if (inHold > M_BOT) ui.line(inM[0].x, yLv(inM[0], inHold), inM[1].r, yLv(inM[0], inHold), inHold > 0 ? t.red : t.ink, 1.5);
    if (outHold > M_BOT) ui.line(outM[0].x, yLv(outM[0], outHold), outM[1].r, yLv(outM[0], outHold), outHold > 0 ? t.red : t.ink, 1.5);
    ui.drawGRMeter(grM, grNow, GR_RANGE);
    if (grHold < -0.05){ const y = grM.y + Math.min(1, -grHold / GR_RANGE) * grM.h; ui.line(grM.x, y, grM.r, y, t.ink, 1.5); }
    const sx = (inM[1].r + outM[0].x) / 2; for (const d of [0, -6, -12, -18, -24, -36, -48]) ui.text(sx, yLv(inM[0], d), String(Math.abs(d)), 'mono', 8.5, t.ink2, 'center');
    for (const d of [3, 6, 12, 18, 24]){ const y = grM.y + d / GR_RANGE * grM.h; ui.line(grM.r, y, grM.r + 3, y, t.ink2, 1); ui.text(grM.r + 5, y, String(d), 'mono', 8.5, t.ink2, 'left'); }
    const num = (v, gr) => gr ? (v > -0.05 ? '0.0' : v.toFixed(1)) : (v < -99 ? '-INF' : v.toFixed(1));
    const ry = graph.y + 9, cIn = (inM[0].x + inM[1].r) / 2, cOut = (outM[0].x + outM[1].r) / 2;
    ui.text(cIn, ry, num(inHold), 'mono', 9.5, inHold > 0 ? t.red : t.ink, 'center'); ui.text(cOut, ry, num(outHold), 'mono', 9.5, outHold > 0 ? t.red : t.ink, 'center');
    ui.text(grM.cx + 2, ry, num(grHold, true), 'mono', 9.5, t.second, 'center');
    const by = graph.b - 7; ui.text(cIn, by, 'IN', 'label', 9.5, t.ink2, 'center', 0.8); ui.text(cOut, by, 'OUT', 'label', 9.5, t.ink2, 'center', 0.8); ui.text(grM.cx + 2, by, 'GR', 'label', 9.5, t.ink2, 'center', 0.8);
  }
  function drawInset(){
    const t = ui.th, g = ui.g; ui.screenBox(inset); const c = cv(), th = V('threshold'), NPt = 120;
    ui.clip(R(inset.x + 1, inset.y + 1, inset.w - 2, inset.h - 2), () => {
      for (let d = -48; d < 0; d += 12){ ui.line(insetX(d), inset.y, insetX(d), inset.b, t.grid, 1); ui.line(inset.x, insetY(d), inset.r, insetY(d), t.grid, 1); }
      ui.line(insetX(IN_LO), insetY(IN_LO), insetX(IN_HI), insetY(IN_HI), t.dim, 1); ui.line(insetX(th), inset.y, insetX(th), inset.b, alpha(t.ink, 0.25), 1);
      const path = () => { g.beginPath(); for (let p = 0; p <= NPt; p++){ const din = IN_LO + (IN_HI - IN_LO) * p / NPt, x = insetX(din), y = insetY(din + c(din)); p ? g.lineTo(x, y) : g.moveTo(x, y); } };
      path(); g.lineTo(inset.r, inset.b); g.lineTo(inset.x, inset.b); g.closePath(); g.fillStyle = css(alpha(t.neon, t.dark ? 0.20 : 0.32)); g.fill();
      if (t.dark){ path(); g.strokeStyle = css(t.glow); g.lineWidth = 6; g.stroke(); }
      path(); g.strokeStyle = css(t.dark ? t.neon : t.ink); g.lineWidth = 2.2; g.lineJoin = 'round'; g.stroke();
      if (detNow > IN_LO - 6){ const x = insetX(Math.min(IN_HI, detNow)), y = insetY(Math.min(IN_HI, detNow + grNow)); g.beginPath(); g.arc(x, y, 5, 0, 7); g.fillStyle = css(t.second); g.fill(); g.strokeStyle = css(t.ink); g.lineWidth = 1.5; g.stroke(); }
    });
    ui.text(inset.x + 8, inset.y + 12, STYLES[Math.round(V('style'))].toUpperCase(), 'label', 10.5, t.ink2, 'left', 1.2);
    ui.text(inset.x + 8, inset.y + 27, fmt('ratio'), 'mono', 12, t.ink, 'left');
    ui.text(inset.r - 8, inset.b - 10, 'KNEE ' + fmt('knee'), 'label', 10, t.ink2, 'right', 0.8);
  }
  function drawBody(){
    const t = ui.th; drawHistory(); drawMeters(); drawInset(); ui.caption(inset.x + (ui.narrow ? inset.w + 16 : 0), scCapY, 'SIDECHAIN'); drawDetector();
    if (external()){ const q = quiet(); ui.text(detBar.r, scCapY, q ? 'NO SIGNAL' : 'ACTIVE', 'label', 10, q ? t.red : t.ink2, 'right', 1); }
    ui.line(14, sepY, ui.W - 14, sepY, t.dim, 1); if (sepX) ui.line(sepX, sepY + 10, sepX, sepY + 128, t.dim, 1);
  }
  function layout(narrow){
    const W = ui.W, rows = [thr, ratio, knee, range, atk, rel, hold, la], row2 = [inD, outD, mixD, linkD], dh = Dial.heightFor('m');
    if (!narrow){
      graph = R(14, 58, 640, 392); const my = graph.y + 26, mh = graph.h - 46;
      inM = [R(664, my, 8, mh), R(673, my, 8, mh)]; outM = [R(706, my, 8, mh), R(715, my, 8, mh)]; grM = R(744, my, 12, mh);
      inHit = R(658, graph.y, 30, graph.h); outHit = R(700, graph.y, 30, graph.h); grHit = R(734, graph.y, 40, graph.h);
      rangeBtn.r = R(graph.r - 56, graph.y + 8, 48, 22);
      const rx = 786, rw = 200; inset = R(rx, graph.y, rw, 200); scCapY = inset.b + 18;
      scMenu.r = R(rx, inset.b + 30, 124, 26); listenBtn.r = R(rx + 132, inset.b + 30, rw - 132, 26);
      const dy = scMenu.r.b + 14; scHp.r = R(rx + 2, dy, 62, 70); scLp.r = R(rx + 69, dy, 62, 70); scGain.r = R(rx + 136, dy, 62, 70);
      detBar = R(rx, graph.b - 10, rw, 8);
      const rowA = 468, rowB = 510; styleSeg.r = R(14, rowA, 528, 28); autoRelBtn.r = R(556, rowA, 98, 28);
      rows.forEach((d, i) => d.r = R(14 + 80 * i + 8, rowB, 64, dh));
      const bx = 664, bw = W - 14 - bx; chanMenu.r = R(bx, rowA, 146, 28); osMenu.r = R(bx + 154, rowA, 72, 28); autoGainBtn.r = R(bx + 234, rowA, bw - 234, 28);
      row2.forEach((d, i) => d.r = R(bx + bw * i / 4 + (bw / 4 - 64) / 2, rowB, 64, dh));
      sepY = 458; sepX = 659;
    } else {
      graph = R(10, 94, 344, 270); const my = graph.y + 26, mh = graph.h - 46, mx = graph.r + 10;
      inM = [R(mx + 6, my, 8, mh), R(mx + 15, my, 8, mh)]; outM = [R(mx + 46, my, 8, mh), R(mx + 55, my, 8, mh)]; grM = R(mx + 84, my, 12, mh);
      inHit = R(mx, graph.y, 30, graph.h); outHit = R(mx + 40, graph.y, 30, graph.h); grHit = R(mx + 74, graph.y, 40, graph.h);
      rangeBtn.r = R(graph.r - 56, graph.y + 8, 48, 22);
      inset = R(10, 380, 196, 170); const sx = 222, sw = W - 10 - sx; scCapY = inset.y + 8;
      scMenu.r = R(sx, inset.y + 22, sw - 84, 26); listenBtn.r = R(sx + sw - 76, inset.y + 22, 76, 26);
      const dy = scMenu.r.b + 8; scHp.r = R(sx + 4, dy, 62, 70); scLp.r = R(sx + 4 + sw / 3, dy, 62, 70); scGain.r = R(sx + 4 + 2 * sw / 3, dy, 62, 70);
      detBar = R(sx, inset.b - 8, sw, 8);
      const rowA = 572; styleSeg.r = R(10, rowA, W - 20, 28);
      autoRelBtn.r = R(10, rowA + 38, 96, 28); chanMenu.r = R(114, rowA + 38, 140, 28); osMenu.r = R(262, rowA + 38, 74, 28); autoGainBtn.r = R(344, rowA + 38, W - 354, 28);
      const cw = (W - 20) / 4, place = (d, i, y) => d.r = R(10 + cw * i + (cw - 64) / 2, y, 64, dh);
      rows.forEach((d, i) => place(d, i % 4, rowA + 80 + (i >> 2) * 84)); row2.forEach((d, i) => place(d, i, rowA + 80 + 2 * 84));
      sepY = rowA - 10; sepX = 0;
    }
  }
  const entryText = s => { const v = V(s);
    if (s === 'ratio') return v >= 99.95 ? 'inf' : v.toFixed(2);
    if (s === 'sc_hpf' || s === 'sc_lpf'){ if ((s === 'sc_hpf' && !hpfOn(v)) || (s === 'sc_lpf' && !lpfOn(v))) return 'off'; return v >= 1000 ? (v / 1000).toFixed(2) + 'k' : v.toFixed(0); }
    let t = compFmt(s, v); const sp = t.indexOf(' '); if (sp >= 0 && !ui.spec(s).ch) t = t.slice(0, sp); if (t.length > 1 && t[0] === '+') t = t.slice(1); if (t === 'OFF') t = '0'; return t; };
  return {
    layout, drawBody, entryText,
    preParse(s, txt){ if ((s === 'sc_hpf' || s === 'sc_lpf') && /^of/i.test(txt.trim())) return s === 'sc_hpf' ? '20' : '20000'; return txt; },
    fmtStatus:s => compFmt(s, V(s)),
    down(x, y, btn, m, dbl){
      if (btn !== 0 && btn !== 2) return false; lastX = x; lastY = y;
      if (inHit.has(x, y)){ inHold = Math.max(inLvl[0], inLvl[1]); ui.status = 'Input peak hold reset'; return false; }
      if (outHit.has(x, y)){ outHold = Math.max(outLvl[0], outLvl[1]); ui.status = 'Output peak hold reset'; return false; }
      if (grHit.has(x, y)){ grHold = grNow; ui.status = 'Gain reduction peak hold reset'; return false; }
      if (nearThresh(x, y)){
        if (btn === 2 || dbl){ ui.openTextEntry('threshold', threshTag()); return false; }
        if (m.cmd || m.alt){ ui.setGesture('threshold', ui.spec('threshold').def); ui.status = 'Threshold reset to ' + fmt('threshold'); return false; }
        ui.begin(); dragStart = V('threshold'); dmode = 1; return true; }
      if (inset.has(x, y)){
        if (btn === 2 || dbl){ const er = R(inset.x + 40, inset.cy - 11, inset.w - 80, 22); ui.openTextEntry('ratio', er, [['ratio', er], ['knee', er], ['threshold', er]]); ui.status = 'Type the ratio (inf = INF:1); Tab: knee, threshold; Enter: done'; return false; }
        if (m.cmd){ ui.begin(); dragStart = ui.norm('ratio'); dmode = 3; return true; }
        ui.begin(); dragStart = V('threshold'); dmode = 2; return true; }
      return false;
    },
    drag(x, y, m){ const fine = m.shift ? 0.15 : 1, dx = (x - lastX) * fine, dy = (y - lastY) * fine; lastX = x; lastY = y;
      if (dmode === 1){ dragStart -= dy / pxPerDb(); ui.setParam('threshold', dragStart); ui.status = 'Threshold  ' + fmt('threshold') + '    Shift: fine'; }
      else if (dmode === 2){ dragStart += (dx - dy) * (IN_HI - IN_LO) / inset.w; ui.setParam('threshold', dragStart); ui.status = 'Threshold  ' + fmt('threshold') + '    Ctrl/Cmd + drag: ratio · wheel: knee'; }
      else if (dmode === 3){ dragStart -= dy * 0.004; ui.setNorm('ratio', dragStart); ui.status = 'Ratio  ' + fmt('ratio') + '    Shift: fine'; } },
    up(){ if (dmode) ui.end(); dmode = 0; },
    hover(x, y){ hoverThresh = nearThresh(x, y);
      if (hoverThresh) ui.status = 'Threshold  ' + fmt('threshold') + '    drag (Shift: fine) · double-click: type · Ctrl/Cmd+click: reset';
      else if (graph.has(x, y)) ui.status = 'Level ' + dbOfY(y).toFixed(1) + ' dB · GR ' + (-(y - graph.y) / pxPerDb()).toFixed(1) + ' dB    drag the threshold line · wheel: threshold';
      else if (inset.has(x, y)){ const din = insetDbOfX(x); ui.status = 'In ' + din.toFixed(1) + ' -> out ' + (din + cv()(din)).toFixed(1) + ' dB    drag: threshold · Ctrl/Cmd+drag: ratio · wheel: knee'; }
      else if (inHit.has(x, y) || outHit.has(x, y) || grHit.has(x, y)) ui.status = 'Click to reset the peak hold';
      else if (styleSeg.r.has(x, y)) ui.status = STYLE_TIP[clamp(Math.floor((x - styleSeg.r.x) / styleSeg.r.w * 6), 0, 5)]; },
    leave(){ hoverThresh = false; },
    wheel(x, y, dy, m){ const dir = dy > 0 ? 1 : -1;
      if (inset.has(x, y)){ ui.setGesture('knee', V('knee') + dir * (m.shift ? 0.25 : 1)); ui.status = 'Knee  ' + fmt('knee'); return true; }
      if (graph.has(x, y)){ ui.setGesture('threshold', V('threshold') + dir * (m.shift ? 0.1 : 0.5)); ui.status = 'Threshold  ' + fmt('threshold'); return true; } return false; },
    key(e, m){
      if (e.key === 'ArrowUp' || e.key === 'ArrowDown'){ ui.setGesture('threshold', V('threshold') + (e.key === 'ArrowUp' ? 1 : -1) * (m.shift ? 0.1 : 0.5)); ui.status = 'Threshold  ' + fmt('threshold'); return true; }
      if (e.key === 'ArrowLeft' || e.key === 'ArrowRight'){ const s = Math.round(V('style')); ui.setGesture('style', (s + (e.key === 'ArrowRight' ? 1 : 5)) % 6); ui.status = 'Style  ' + STYLES[Math.round(V('style'))]; return true; }
      return false; },
    changed(){ linkD.enabled = Math.round(V('channel_mode')) <= 1; },
    reload(){ linkD.enabled = Math.round(V('channel_mode')) <= 1; },
    frame(fr){ const t = fr.t;
      pin[0] = Math.max(pin[0], t[3]); pin[1] = Math.max(pin[1], t[4]); pout[0] = Math.max(pout[0], t[1]); pout[1] = Math.max(pout[1], t[2]);
      pgr = Math.min(pgr, t[5], t[6]); pdet = Math.max(pdet, t[7]); psc = Math.max(psc, t[8]); scConn = t[10];
      for (const rg of fr.rings) [hIn, hOut, hGr, hDet][rg.k] && [hIn, hOut, hGr, hDet][rg.k].push(rg); },
    tick(dt){
      for (let c = 0; c < 2; c++){ const pi = U.dbOf(pin[c]), po = U.dbOf(pout[c]); pin[c] = pout[c] = 0;
        inLvl[c] = Math.max(pi, inLvl[c] - 26 * dt); outLvl[c] = Math.max(po, outLvl[c] - 26 * dt);
        if (Math.max(pi, -120) >= inHold){ inHold = Math.max(pi, -120); inHoldT = 2; } if (Math.max(po, -120) >= outHold){ outHold = Math.max(po, -120); outHoldT = 2; } }
      if ((inHoldT -= dt) <= 0) inHold = Math.max(inLvl[0], inLvl[1], inHold - 15 * dt);
      if ((outHoldT -= dt) <= 0) outHold = Math.max(outLvl[0], outLvl[1], outHold - 15 * dt);
      const gq = pgr; pgr = 0; grNow = gq < grNow ? gq : grNow + (gq - grNow) * Math.min(1, dt * 14);
      if (gq <= grHold){ grHold = gq; grHoldT = 2; } else if ((grHoldT -= dt) <= 0) grHold = Math.min(grNow, grHold + 10 * dt);
      const d = pdet; pdet = -200; detNow = d > detNow ? d : Math.max(d, detNow - 30 * dt);
      const sc = psc; psc = 0; scQuiet = sc > 1e-5 ? 0 : scQuiet + dt;
    },
    animating(){ return U.now() - ui.lastFrame < 2 || inLvl[0] > -100 || outLvl[0] > -100 || grNow < -0.05; },
    footer(){ return (STYLES[Math.round(V('style'))] + '  ' + CHANS[Math.round(V('channel_mode'))] + '  ' + OS[Math.round(V('oversampling'))] + '  ' + ui.latencyText()).toUpperCase(); },
    close(){ if (V('sc_listen') >= 0.5) ui.setParam('sc_listen', 0); },
    help(){ return [['Drag the threshold line', 'threshold (Shift: fine); double-click it or right-click: type a value'], ['Wheel over the graph', 'threshold in 0.5 dB steps (Shift: 0.1 dB)'],
      ['Drag the transfer curve', 'threshold;  Ctrl/Cmd + drag up/down: ratio'], ['Wheel over the transfer curve', 'knee (Shift: fine);  double-click: type ratio, Tab to knee and threshold'],
      ['Click a meter', 'reset its peak hold (In, Out, GR)'], ['SC LISTEN', 'click: toggle;  click and hold: listen only while held'],
      ['Up / Down  ·  Left / Right', 'threshold (Shift: fine)  ·  previous / next style'], ['Typing values', 'ratio: 4, inf;  sidechain filters: off, 120, 2k'],
      ['Sidechain menu', 'Internal, or External fed by one of Thunder’s sends or tracks']]; },
  };
}});
})();
