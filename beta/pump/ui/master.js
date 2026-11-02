/* …pump MASTER pop-up (31 Oct 2026): plugins/master/UIMaster.cpp in the browser. The display shows the song's learned
   balance (dim), the style's target (dotted) and the tone curve MASTER applies (coloured by frequency: boosts strong,
   cuts light) over the live output spectrum; the column beside it the loudness (the song as learned, the plan, the
   output now) or, in ADVANCED, a meter bridge. Below: LEARN, COMPARE, ADVANCED, INTENSITY and STYLE, then one panel per
   stage (TONE, DYNAMICS, STEREO, LOUDNESS) with its key, its amount and what it is doing to this song. ADVANCED adds the
   offsets on top of what auto chose (the dials show the values they produce), auto's nine tone points (squares, dragged
   up and down) and eight free EQ bands (numbered circles). The look is Thunder Plus's Simple rainbow: every key and dial
   in its own hue (the plugin's Control::tint and Theme::tint, done here, since the kit has no tint).
   Telemetry: pump's wasm/fx/master.cpp (t[5 + i]); LEARN is PumpFX.hold(t, k, 3, 1 start / 2 finish / 3 cancel). */
(() => {
'use strict';
const U = window.PumpUI; if (!U) return;
const {R, clamp, css, alpha, Dial, Feed, Spectrum} = U;
const KLO = 20, KHI = 20000, NCP = 96, NB = 31, NEQ = 9, NUB = 8, B0 = 5;
const SHAPES = ['Bell', 'Low Shelf', 'High Shelf', 'Low Cut', 'High Cut'];
const AUTO_F = [60, 125, 250, 500, 1000, 2000, 4000, 8000, 12000];
const TRIMS = ['tone_trim_60', 'tone_trim_125', 'tone_trim_250', 'tone_trim_500', 'tone_trim_1k', 'tone_trim_2k', 'tone_trim_4k', 'tone_trim_8k', 'tone_trim_12k'];
const TRIM_SN = ['60', '125', '250', '500', '1k', '2k', '4k', '8k', '12k'];
const STAGE_ON = ['tone_on', 'dyn_on', 'stereo_on', 'loud_on'];
const UI_KEY = 'thunderplus-pump-master-ui-v1';   // the plugin's UI state (curve scale, ADVANCED), kept on this device
const bandFreq = b => 1000 * Math.pow(2, (b - 17) / 3);
const curveFreq = p => 20 * Math.pow(1000, p / (NCP - 1));
const bs = (b, f) => 'band' + (b + 1) + '_' + f;
const sgn = (v, d = 1) => (v >= 0 ? '+' : '') + v.toFixed(d);
const dbToG = d => Math.pow(10, d / 20), gToDb = g => 20 * Math.log10(Math.max(g, 1e-9));
const clock = s => { const t = Math.max(0, s | 0); return (t / 60 | 0) + ':' + String(t % 60).padStart(2, '0'); };

// Theme.hpp oklchColor: OKLCH to sRGB, [r, g, b, a] in the kit's colour form (0..255, alpha 0..1)
function oklch(L, C, hDeg, a = 1){
  const h = hDeg * Math.PI / 180, A = C * Math.cos(h), B = C * Math.sin(h);
  const l_ = L + 0.3963377774 * A + 0.2158037573 * B, m_ = L - 0.1055613458 * A - 0.0638541728 * B, s_ = L - 0.0894841775 * A - 1.2914855480 * B;
  const l = l_ * l_ * l_, m = m_ * m_ * m_, s = s_ * s_ * s_;
  return [4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s, -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s, -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s]
    .map(c => { c = clamp(c, 0, 1); return 255 * (c <= 0.0031308 ? 12.92 * c : 1.055 * Math.pow(c, 1 / 2.4) - 0.055); }).concat([a]);
}
// Theme::tint: one control's own colour (its outline, text and "on" fill take the hue), the rest of the theme stays
function tinted(th, hue){
  const t = Object.assign({}, th);
  if (!th.dark){ t.ink = oklch(0.55, 0.17, hue); t.ink2 = oklch(0.62, 0.12, hue); t.dim = oklch(0.86, 0.07, hue); t.neon = oklch(0.72, 0.14, hue); t.neonInk = oklch(0.30, 0.10, hue); t.on = [255, 255, 255, 1]; }
  else { t.ink = oklch(0.80, 0.14, hue); t.ink2 = oklch(0.68, 0.11, hue); t.dim = oklch(0.40, 0.08, hue); t.neon = oklch(0.72, 0.15, hue); t.neonInk = oklch(0.18, 0.04, hue); t.on = th.bg; t.glow = alpha(t.neon, 0.22); }
  return t;
}
// (kit gap) PumpUI's controls have no `tint`: wrap the control's draw so it runs with the tinted theme
function tint(c, hue){
  const d = c.draw; c.tint = hue;
  c.draw = function(ui){ const keep = ui.th; if (c._tFor !== keep){ c._tTh = tinted(keep, hue); c._tFor = keep; } ui.th = c._tTh; try { d.call(this, ui); } finally { ui.th = keep; } };
  return c;
}
const hue = (col, row) => ((col + row) % 12) * 30 + 10;

U.define('master', {W:940, H:560, nW:480, nH:1060, bypass:true, back:8192, make(ui){
  // ---- state (UIMaster's members)
  const st = (() => { try { const v = JSON.parse(localStorage.getItem(UI_KEY)); return v && typeof v === 'object' ? v : {}; } catch(e){ return {}; } })();
  let dbRange = [3, 6, 12].includes(st.range) ? st.range : 6, advanced = !!st.adv;
  const saveUi = () => { try { localStorage.setItem(UI_KEY, JSON.stringify({range:dbRange, adv:advanced})); } catch(e){} };
  const ana = {valid:false, lufs:-70, truePeak:-120, lra:0, seconds:0};
  let learning = false, planValid = false, learnSec = 0, gotFrame = false;
  const curve = new Float32Array(NCP), bandsCurve = new Float32Array(NCP), curveT = new Float32Array(NCP), bandsT = new Float32Array(NCP);
  const target = new Float32Array(NB), measured = new Float32Array(NB), eqGain = new Float32Array(NEQ), autoEq = new Float32Array(NEQ);
  let ratio = 1, thr = 0, liftMax = 0, lift = 0, sideLo = 0, sideHi = 0, makeup = 0, outPred = -70, cmpDb = 0, peakOver = 0, limLoss = 0, eqDelta = 0, inST = -Infinity, outST = -Infinity;
  let aRatio = 1, aThr = 0, aLift = 0, aMakeup = 0, aSideLoDb = 0, aSideHiDb = 0, compGr = 0, limGr = 0, pcg = 0, plg = 0;
  let specTop = 0, specBottom = -36, hoverX = -1, hoverY = -1, lastFrameT = 0;
  const pin = [0, 0], pout = [0, 0];
  const mkMeter = () => ({lvl:[-120, -120], hold:[-120, -120], holdT:[0, 0]});
  const outMeter = mkMeter(), inMeter = mkMeter();
  const meterTick = (m, pk, dt) => { for (let c = 0; c < 2; c++){ const p = U.dbOf(pk[c]); pk[c] = 0;
    m.lvl[c] = Math.max(p, m.lvl[c] - 26 * dt);
    if (p >= m.hold[c]){ m.hold[c] = p; m.holdT[c] = 1.5; } else if ((m.holdT[c] -= dt) <= 0) m.hold[c] = Math.max(p, m.hold[c] - 18 * dt); } };
  const post = new Spectrum(KLO, KHI), fPost = new Feed(32768);
  let display = R(), info = R(), mainP = R(); const stageP = [R(), R(), R(), R()], stageHue = [0, 0, 0, 0];
  let drag = 0, dragIdx = -1, dragLastY = 0, dragV = 0;   // drag 1 = a tone point (trim), 2 = a free band

  // the learned analysis kept in the slot, until the first telemetry frame says otherwise (the song may be stopped)
  function fromState(){
    if (gotFrame) return;
    const s = PumpFX.getState(ui.t, ui.k, 'analysis') || '', num = k => { const m = new RegExp(';' + k + '=(-?[0-9.]+)').exec(s); return m ? +m[1] : null; };
    ana.valid = s.startsWith('pumpmaster1') && num('lufs') != null;
    if (ana.valid){ ana.lufs = num('lufs'); ana.truePeak = num('tp') ?? -120; ana.lra = num('lra') ?? 0; ana.seconds = num('sec') ?? 0; }
  }

  // ---- controls (the plugin's constructor, in its order)
  const V = s => ui.value(s);
  const learnBtn = ui.button('LEARN', () => toggleLearn(), () => learning);
  learnBtn.tip = 'Learn: play the song (or its loudest part and a quiet part), press LEARN, press again when done (it also stops by itself when the song stops). L key';
  const styleSeg = ui.seg('style', ['BALANCED', 'WARM', 'OPEN', 'PUNCH']);
  styleSeg.tip = 'Style: the balance MASTER aims for. Balanced is the subtle reference; Warm darker, Open brighter and a touch wider, Punch with glue compression';
  const intDial = ui.dial('intensity', 'INTENSITY', 'l'); intDial.tip = 'Intensity: how much of everything (50 % = medium; 25 % light, 100 % twice medium)';
  const cmpBtn = ui.toggle('compare', 'COMPARE'); cmpBtn.holdMomentary = true;
  cmpBtn.tip = 'Compare: hear the original at the master’s loudness, so you judge the sound, not the level (click = latch, click-and-hold = while held; C key)';
  const SD = [['tone_on', 'tone_amount', 'TONE', 'Tone: broad EQ toward the style’s balance (about half of the way at medium)'],
    ['dyn_on', 'dyn_amount', 'DYNAMICS', 'Dynamics: lifts quiet sections slowly (Punch also glues the loud parts)'],
    ['stereo_on', 'stereo_amount', 'STEREO', 'Stereo: tightens a wide low end; Open and Punch nudge the width']];
  const stageBtn = [], stageAmt = [];
  for (let i = 0; i < 3; i++){ stageBtn[i] = ui.toggle(SD[i][0], SD[i][2]); stageBtn[i].tip = SD[i][3];
    stageAmt[i] = ui.dial(SD[i][1], 'AMOUNT', 's'); stageAmt[i].tip = SD[i][2] + ' amount (on top of INTENSITY; 100 % = as the style has it)'; }
  stageBtn[3] = ui.toggle('loud_on', 'LOUDNESS'); stageBtn[3].tip = 'Loudness: makeup gain and a true-peak limiter that only touches the peaks';
  // DESTINATION: where the master is going; picks TARGET (and TARGET mode; vinyl also folds the bass to mono).
  // Moving the TARGET dial anywhere else reads CUSTOM.
  const DEST = [
    { item: 'Streaming  -14 LUFS', face: 'STREAM', lufs: -14 },
    { item: 'Standard  -13.5 LUFS (the reference)', face: 'STANDARD', lufs: -13.5 },
    { item: 'Vinyl  -12 LUFS, bass mono below 150 Hz', face: 'VINYL', lufs: -12, vinyl: true },
    { item: 'Club  -9 LUFS', face: 'CLUB', lufs: -9 }];
  const VINYL_MONO_FREQ = 150, VINYL_MONO_TRIM = 100;
  const vinylBass = () => Math.abs(V('mono_freq') - VINYL_MONO_FREQ) < 0.5 && Math.abs(V('low_mono_trim') - VINYL_MONO_TRIM) < 0.5;
  const curDest = () => DEST.findIndex(d => Math.abs(V('target') - d.lufs) < 0.05 && (!d.vinyl || vinylBass()));
  const destM = ui.menuBtn(null);
  destM.itemsFn = () => DEST.map(d => d.item);
  destM.getFn = curDest;
  destM.setFn = i => {
    const d = DEST[i]; if (!d) return;
    const o = { target: d.lufs, loud_mode: 1, loud_on: 1 };
    if (d.vinyl){ o.mono_freq = VINYL_MONO_FREQ; o.low_mono_trim = VINYL_MONO_TRIM; }
    else if (vinylBass()){ o.mono_freq = ui.spec('mono_freq').def; o.low_mono_trim = ui.spec('low_mono_trim').def; }   // leaving vinyl (only if still vinyl's)
    ui.setMany(o); ui.status = 'Destination: ' + d.item;
  };
  destM.textFn = () => { const i = curDest(); return i >= 0 ? DEST[i].face + ' ' + DEST[i].lufs : 'CUSTOM ' + V('target').toFixed(1); };
  destM.tip = 'Destination: sets the loudness TARGET for where the master is going (Streaming -14, Standard -13.5 = the reference’s level, Vinyl -12 with the bass mono below 150 Hz, Club -9). Wheel steps through them';
  const modeSeg = ui.seg('loud_mode', ['GENTLE', 'TARGET']);
  modeSeg.tip = 'Gentle: only as loud as the peaks allow (a couple of dB of peak limiting at most), never past TARGET. Target: to TARGET, however much limiting it takes';
  const targetDial = ui.dial('target', 'TARGET', 's'); targetDial.tip = 'Loudness target (LUFS, integrated). In Gentle mode the most it will reach';
  const ceilDial = ui.dial('ceiling', 'CEILING', 's'); ceilDial.tip = 'True-peak ceiling (dBTP). -1 is safe for streaming encodes; 0 gives about 1 dB more level';
  targetDial.tabGroup = ceilDial.tabGroup = 2;
  // ADVANCED: offsets on top of what auto chose, and the details
  const advBtn = ui.button('ADVANCED', () => setAdvanced(!advanced), () => advanced);
  advBtn.tip = 'Advanced: tweak what auto set (every value is an offset on top of auto: zero = auto), plus eight free EQ bands. A key';
  const resetTrimBtn = ui.button('RESET TRIMS', () => { const o = {}; TRIMS.forEach(s => o[s] = 0); ui.setMany(o); ui.status = 'Tone back to what auto set'; });
  resetTrimBtn.small = true; resetTrimBtn.tip = 'Put the nine auto tone points back where auto set them';
  const clearBandsBtn = ui.button('CLEAR BANDS', () => { const o = {}; for (let b = 0; b < NUB; b++) o[bs(b, 'on')] = 0; ui.setMany(o); ui.status = 'Free bands off'; });
  clearBandsBtn.small = true; clearBandsBtn.tip = 'Switch every free EQ band off';
  const advDials = [];
  const adv = (sym, label, bip, tip) => { const d = ui.dial(sym, label, 's', bip); d.tip = tip; advDials.push(d); d.tabGroup = 3; return d; };
  // the values show what they set (auto's choice plus your offset); the dial's centre is auto's choice
  const gThrD = adv('glue_thr_trim', 'THRESHOLD', true, 'Compressor threshold (RMS of the bass-filtered signal, dBFS). Centre = auto’s');
  const ratioD = adv('ratio_trim', 'RATIO', true, 'Compressor ratio. Centre = auto’s (Balanced: 1:1, no compression)');
  const atkD = adv('glue_attack', 'ATTACK', false, 'Compressor attack');
  const relD = adv('glue_release', 'RELEASE', false, 'Compressor release');
  const liftD = adv('lift_trim', 'LEVELER', true, 'Leveler: how much quiet sections are lifted (slow, by short-term loudness). Centre = auto’s');
  const liftAtD = adv('lift_at', 'BELOW', true, 'Leveler: sections quieter than this (short-term LUFS) are lifted. Centre = auto’s');
  const monoD = adv('mono_freq', 'MONO BELOW', false, 'Bass mono crossover: the side below it is what BASS MONO folds');
  const lowMonoD = adv('low_mono_trim', 'BASS MONO', true, 'How much of the side below the crossover is folded to mono (100 % = mono). Centre = auto’s');
  const widthD = adv('width_trim', 'WIDTH', true, 'Stereo width above the crossover (side level: 100 % = unchanged). Centre = auto’s');
  const gainD = adv('gain_trim', 'INPUT', true, 'Limiter input gain (the makeup: more = louder, more limiting). Centre = auto’s');
  const limRelD = adv('lim_release', 'RELEASE', false, 'Limiter release');
  const limStyleM = ui.menuBtn('lim_style'); limStyleM.tip = 'Limiter style (LIMIT’s styles). Dynamic: brief peak-only limiting, the default';
  // the plugin's value text on every dial
  for (const c of ui.controls) if (c instanceof Dial && c.sym) { const s = c.sym; c.fmt = v => formatParam(s, v); }
  // the rainbow: hue = (column + row) x 30 + 10 degrees
  tint(learnBtn, hue(0, 0)); tint(intDial, hue(1, 0)); tint(cmpBtn, hue(0, 1));
  for (let i = 0; i < 4; i++) tint(stageBtn[i], stageHue[i] = hue(2 + i, 0));
  for (let i = 0; i < 3; i++) tint(stageAmt[i], hue(2 + i, 1));
  tint(destM, hue(6, 1)); tint(modeSeg, hue(5, 1)); tint(targetDial, hue(5, 2)); tint(ceilDial, hue(6, 2));
  tint(advBtn, hue(0, 2)); tint(styleSeg, hue(0, 3));
  tint(resetTrimBtn, hue(2, 2)); tint(clearBandsBtn, hue(3, 2));
  tint(gThrD, hue(3, 1)); tint(ratioD, hue(4, 1)); tint(atkD, hue(3, 2)); tint(relD, hue(4, 2)); tint(liftD, hue(3, 3)); tint(liftAtD, hue(4, 3));
  tint(monoD, hue(5, 1)); tint(lowMonoD, hue(6, 1)); tint(widthD, hue(5, 2));
  tint(gainD, hue(6, 3)); tint(limRelD, hue(7, 3)); tint(limStyleM, hue(6, 4));

  // ---- the ADVANCED offsets shown (and typed) as the values they produce
  const liftBase = () => (ana.valid ? ana.lufs + eqDelta : -14) - 5;
  function shownValue(s, v){
    switch (s){
      case 'ratio_trim': return Math.max(1, aRatio + v);
      case 'glue_thr_trim': return aThr + v;
      case 'lift_trim': return Math.max(0, aLift + v);
      case 'lift_at': return liftBase() + v;
      case 'low_mono_trim': return clamp((1 - dbToG(aSideLoDb)) * 100 + v, 0, 100);
      case 'width_trim': return 100 * dbToG(aSideHiDb + v);
      case 'gain_trim': return aMakeup + v;
    }
    const i = TRIMS.indexOf(s); return i >= 0 ? autoEq[i] + v : v;
  }
  function fromShown(s, x){
    switch (s){
      case 'ratio_trim': return x - aRatio;
      case 'glue_thr_trim': return x - aThr;
      case 'lift_trim': return x - aLift;
      case 'lift_at': return x - liftBase();
      case 'low_mono_trim': return x - (1 - dbToG(aSideLoDb)) * 100;
      case 'width_trim': return gToDb(Math.max(x, 1) / 100) - aSideHiDb;
      case 'gain_trim': return x - aMakeup;
    }
    const i = TRIMS.indexOf(s); return i >= 0 ? x - autoEq[i] : x;
  }
  const SHOWN = new Set(['ratio_trim', 'glue_thr_trim', 'lift_trim', 'lift_at', 'low_mono_trim', 'width_trim', 'gain_trim', ...TRIMS]);
  function formatParam(s, v){
    if (s === 'target') return v.toFixed(1) + ' LUFS';
    if (s === 'ceiling') return v.toFixed(1) + ' dBTP';
    if (s === 'intensity') return v.toFixed(0) + ' % ' + (v < 18 ? 'SUBTLE' : v < 38 ? 'LIGHT' : v < 63 ? 'MEDIUM' : v < 85 ? 'STRONG' : 'MAX');
    if (SHOWN.has(s)){ const sv = shownValue(s, v);
      if (s === 'ratio_trim') return sv.toFixed(2) + ':1';
      if (s === 'glue_thr_trim') return sv.toFixed(1) + ' dB';
      if (s === 'lift_trim') return '+' + sv.toFixed(1) + ' dB';
      if (s === 'lift_at') return sv.toFixed(1) + ' LUFS';
      if (s === 'low_mono_trim' || s === 'width_trim') return sv.toFixed(0) + ' %';
      return sgn(sv) + ' dB'; }
    return PumpFX.format('master', s, v);
  }

  // ---- LEARN, ADVANCED
  function toggleLearn(){
    if (learning){ ui.hold(3, 2); learning = false; ui.status = 'Learned: the master follows this song now'; }
    else { ui.hold(3, 1); learning = true; learnSec = 0; ui.status = 'Learning: play the song; press LEARN again when it has played (or stop the song)'; }
    ui.dirty = true;
  }
  function setAdvanced(a){ advanced = a; ui.layoutAll(); saveUi(); ui.dirty = true; }

  // ---- layout (desktop: the plugin's; narrow: the display, then MASTER beside the loudness column, then the stages 2 x 2)
  function layout(narrow){
    const W = ui.W, H = ui.H;
    let ph;
    if (!narrow){
      const dh = advanced ? 200 : 262;
      info = R(W - 14 - 208, 58, 208, dh); display = R(14, 58, info.x - 12 - 14, dh);
      const y0 = display.b + 12; ph = H - 24 - 12 - y0;
      mainP = R(14, y0, 262, ph);
      const sx = mainP.r + 10, sw = (W - 14 - sx - 3 * 10) / 4;
      for (let i = 0; i < 4; i++) stageP[i] = R(sx + i * (sw + 10), y0, sw, ph);
    } else {
      display = R(10, 94, W - 20, advanced ? 192 : 286);
      const y1 = display.b + 10, h1 = advanced ? 192 : 230; mainP = R(10, y1, 256, h1); info = R(276, y1, W - 10 - 276, h1);
      ph = advanced ? 258 : 192;   // (258: DYNAMICS' third row of dials fits inside its panel)
      const sy = mainP.b + 10, sw = (W - 20 - 10) / 2;
      for (let i = 0; i < 4; i++) stageP[i] = R(10 + (i & 1) * (sw + 10), sy + (i >> 1) * (ph + 10), sw, ph);
    }
    const y0 = mainP.y;
    learnBtn.r = R(mainP.x + 14, y0 + 30, 110, 40);
    cmpBtn.r = R(mainP.x + 14, y0 + 78, 110, 24);
    advBtn.r = R(mainP.x + 14, y0 + 108, 110, 24);
    intDial.r = R(mainP.x + 140, y0 + 26, 108, Dial.heightFor('l'));
    styleSeg.r = R(mainP.x + 14, mainP.b - 38, mainP.w - 28, 26);
    const sh = Dial.heightFor('s');
    for (let i = 0; i < 4; i++){ const p = stageP[i]; stageBtn[i].r = R(p.x + 10, p.y + 10, p.w - 20, 26); if (i < 3) stageAmt[i].r = R(p.cx - 36, p.y + 46, 72, sh); }
    const lp = stageP[3];
    destM.r = R(lp.x + 10, lp.y + 40, lp.w - 20, 22);
    modeSeg.r = R(lp.x + 10, lp.y + 66, lp.w - 20, 20);
    targetDial.r = R(lp.x + 6, lp.y + 88, lp.w * 0.5 - 6, sh);
    ceilDial.r = R(lp.cx, lp.y + 88, lp.w * 0.5 - 6, sh);
    // ADVANCED: two columns of small dials under each stage's key
    for (const c of advDials) c.visible = advanced;
    limStyleM.visible = resetTrimBtn.visible = clearBandsBtn.visible = advanced;
    for (let i = 0; i < 3; i++) stageAmt[i].visible = !(advanced && i === 1);
    const cell = (s, col, row) => { const p = stageP[s]; return R(col === 0 ? p.x + 6 : p.cx, p.y + 44 + row * (sh + 2), p.w * 0.5 - 6, sh); };
    if (advanced){
      const tp = stageP[0];
      stageAmt[0].r = R(tp.cx - 36, tp.y + 44, 72, sh);
      resetTrimBtn.r = R(tp.x + 10, tp.y + 44 + sh + 6, tp.w - 20, 22);
      clearBandsBtn.r = R(tp.x + 10, tp.y + 44 + sh + 34, tp.w - 20, 22);
      gThrD.r = cell(1, 0, 0); ratioD.r = cell(1, 1, 0); atkD.r = cell(1, 0, 1); relD.r = cell(1, 1, 1);
      liftD.r = cell(1, 0, 2); liftAtD.r = cell(1, 1, 2);
      stageAmt[2].r = cell(2, 0, 0); monoD.r = cell(2, 1, 0); lowMonoD.r = cell(2, 0, 1); widthD.r = cell(2, 1, 1);
      gainD.r = R(lp.x + 6, lp.y + 88 + sh, lp.w * 0.5 - 6, sh);
      limRelD.r = R(lp.cx, lp.y + 88 + sh, lp.w * 0.5 - 6, sh);
      limStyleM.r = R(lp.x + 10, Math.min(lp.y + 90 + 2 * sh, lp.b - 24), lp.w - 20, 22);
    }
  }

  // ---- helpers
  const xOfF = f => ui.freqToX(display, f, KLO, KHI), fOfX = x => ui.xToFreq(display, x, KLO, KHI);
  const pxPerDb = () => (display.h * 0.5 - 16) / dbRange;
  const yOfDb = db => display.cy - db * pxPerDb();
  const yOfSpec = db => display.y + (specTop - db) / (specTop - specBottom) * display.h;
  const interp = (a, f) => { const x = Math.log(f / 20) / Math.log(1000) * (NCP - 1), k = clamp(Math.floor(x), 0, NCP - 2), t = clamp(x - k, 0, 1); return a[k] * (1 - t) + a[k + 1] * t; };
  const curveAt = f => interp(curve, f), bandsAt = f => interp(bandsCurve, f), totalAt = f => curveAt(f) + bandsAt(f);
  const bandOn = b => V(bs(b, 'on')) >= 0.5;
  const bandIsCut = b => { const s = Math.round(V(bs(b, 'shape'))); return s === 3 || s === 4; };
  const bandY = b => yOfDb(bandIsCut(b) ? 0 : V(bs(b, 'gain')));
  const pointColour = (f, dark, a = 1) => oklch(dark ? 0.78 : 0.55, 0.16, 10 + 330 * Math.log(f / 20) / Math.log(1000), a);
  const hitR = () => ui.narrow ? 1.6 : 1;
  function hitTrim(x, y){ const r = 9 * hitR(); for (let i = 0; i < NEQ; i++){ const f = AUTO_F[i]; if (Math.abs(x - xOfF(f)) < r && Math.abs(y - yOfDb(curveAt(f))) < r) return i; } return -1; }
  function hitBand(x, y){ const r = 12 * hitR(); for (let b = NUB - 1; b >= 0; b--){ if (!bandOn(b)) continue; const dx = x - xOfF(V(bs(b, 'freq'))), dy = y - bandY(b); if (dx * dx + dy * dy < r * r) return b; } return -1; }
  const fmtHz = f => f < 1000 ? f.toFixed(0) + ' Hz' : (f / 1000).toFixed(1) + ' kHz';
  const bandStatus = b => 'Band ' + (b + 1) + '  ' + SHAPES[Math.round(V(bs(b, 'shape')))] + '  ' + PumpFX.format('master', bs(b, 'freq'), V(bs(b, 'freq'))) + '  ' + sgn(V(bs(b, 'gain'))) + ' dB  Q ' + V(bs(b, 'q')).toFixed(2);
  // the tone curve's colours, round the wheel from the lows to the top (boosts, cuts; light and dark), worked out once
  const NP = 200, segCol = {};
  const segColours = dark => segCol[dark] || (segCol[dark] = Array.from({length:NP}, (_, p) => { const h = 10 + 330 * p / (NP - 1);
    return [css(oklch(dark ? 0.66 : 0.74, 0.15, h, dark ? 0.75 : 0.85)), css(oklch(dark ? 0.45 : 0.87, 0.08, h, 0.85))]; }));

  // ---- drawing
  function drawBody(){
    const t = ui.th, g = ui.g;
    ui.screenBox(display);
    ui.clip(R(display.x + 1, display.y + 1, display.w - 2, display.h - 2), () => {
      for (let k = -2; k <= 2; k++){ const y = yOfDb(dbRange * k / 2); ui.line(display.x, y, display.r, y, k === 0 ? alpha(t.ink, 0.28) : t.grid, 1); }
      ui.drawFreqGrid(display, KLO, KHI);
      // the output now (its own scale, faint)
      if (post.sr) ui.drawSpectrum(post, display, 6, -84, alpha(t.neon, t.dark ? 0.12 : 0.18), alpha(t.ink, 0.18), KLO, KHI, 1.0);
      if (ana.valid && planValid){
        // the song as learned and the style's target (spectrum scale, aligned in the mids)
        const sr = ui.sr || 48000;
        const spec = (v, c, w, dots) => {
          g.beginPath(); let first = true;
          for (let b = 1; b < NB; b++){ const f = bandFreq(b); if (f > 0.47 * sr) break;
            const x = xOfF(f), y = Math.max(display.y + 2, Math.min(display.b - 2, yOfSpec(v[b]))); first ? g.moveTo(x, y) : g.lineTo(x, y); first = false; }
          g.strokeStyle = css(c); g.lineWidth = w; g.lineJoin = 'round'; g.stroke();
          if (dots) for (let b = 2; b < NB; b += 2){ const f = bandFreq(b); if (f > 0.47 * sr) break; g.beginPath(); g.arc(xOfF(f), yOfSpec(v[b]), 2, 0, 7); g.fillStyle = css(c); g.fill(); }
        };
        spec(measured, alpha(t.ink, 0.55), 1.6, false);
        spec(target, alpha(t.second, 0.75), 1.2, true);
        // the tone curve, coloured by frequency; cuts in the lighter shade
        const xs = new Float32Array(NP), ys = new Float32Array(NP), y0 = yOfDb(0), cols = segColours(!!t.dark);
        for (let p = 0; p < NP; p++){ xs[p] = display.x + display.w * p / (NP - 1); ys[p] = Math.max(display.y + 2, Math.min(display.b - 2, yOfDb(totalAt(fOfX(xs[p]))))); }
        for (let p = 0; p + 1 < NP; p++){ const ya = ys[p], yb = ys[p + 1];
          g.beginPath(); g.moveTo(xs[p], y0); g.lineTo(xs[p], ya); g.lineTo(xs[p + 1], yb); g.lineTo(xs[p + 1], y0); g.closePath();
          g.fillStyle = cols[p][ya + yb < 2 * y0 ? 0 : 1]; g.fill(); }
        g.beginPath(); for (let p = 0; p < NP; p++) p ? g.lineTo(xs[p], ys[p]) : g.moveTo(xs[p], ys[p]);
        g.strokeStyle = css(t.ink); g.lineWidth = 2.2; g.lineJoin = 'round'; g.stroke();
        if (advanced) drawHandles();
        for (let k = -2; k < 2; k++){ const v = dbRange * k / 2; ui.text(display.r - 8, yOfDb(v) - 7, k === 0 ? '0' : sgn(v, 0), 'mono', 9.5, t.ink2, 'right'); }
        // legend
        let lx = display.x + 12;
        const key = (c, s, lineKey) => {
          if (lineKey) ui.line(lx, display.y + 14, lx + 14, display.y + 14, c, 1.8);
          else { ui.fill(R(lx, display.y + 9, 12, 10), c); ui.stroke(R(lx, display.y + 9, 12, 10), t.ink, 1); }
          ui.text(lx + 18, display.y + 14.5, s, 'label', 10.5, t.ink, 'left', 1);
          lx += 18 + ui.textW(s, 'label', 10.5, 1) + 14;
        };
        key(oklch(t.dark ? 0.66 : 0.74, 0.15, 70), 'BOOST', false);
        key(oklch(t.dark ? 0.45 : 0.87, 0.08, 70), 'CUT', false);
        key(alpha(t.ink, 0.6), 'SONG', true);
        key(alpha(t.second, 0.8), 'TARGET', true);
      }
      // hover readout
      if (hoverX >= 0 && planValid && display.has(hoverX, hoverY)){
        const f = fOfX(hoverX), gn = curveAt(f);
        ui.line(hoverX, display.y, hoverX, display.b, alpha(t.ink, 0.35), 1);
        const s = (f < 1000 ? f.toFixed(0) + ' Hz  ' : (f / 1000).toFixed(1) + ' kHz  ') + sgn(gn) + ' dB';
        const tw = ui.textW(s, 'mono', 11) + 14, yy = Math.max(display.y + 30, Math.min(display.b - 30, yOfDb(gn) - 26));
        const tb = R(Math.max(display.x + 6, Math.min(display.r - tw - 6, hoverX + 10)), yy, tw, 22);
        ui.fill(tb, t.ink); ui.text(tb.cx, tb.cy, s, 'mono', 11, t.on, 'center');
      }
      // banners
      const banner = (s, bgc, fg, y) => { const tw = Math.min(display.w - 8, ui.textW(s, 'label', 12, 1) + 22), lb = R(display.cx - tw / 2, y, tw, 24);
        ui.fill(lb, bgc); ui.text(lb.cx, lb.cy, s, 'label', 12, fg, 'center', 1); };
      const nl = ui.narrow;   // (the narrow display is too narrow for the long lines: two shorter banners)
      if (learning){ if (nl){ banner('LEARNING  ' + clock(learnSec), t.ink, t.on, display.cy - 26); banner('PLAY THE SONG, THEN PRESS STOP', t.ink, t.on, display.cy + 2); }
        else banner('LEARNING  ' + clock(learnSec) + '  ·  PLAY THE SONG, THEN PRESS STOP', t.ink, t.on, display.cy - 12); }
      else if (!ana.valid){ if (nl){ banner('PLAY THE SONG AND PRESS LEARN', t.ink, t.on, display.cy - 26); banner('UNTIL THEN MASTER CHANGES NOTHING', t.ink, t.on, display.cy + 2); }
        else banner('PLAY THE SONG AND PRESS LEARN  ·  UNTIL THEN MASTER CHANGES NOTHING', t.ink, t.on, display.cy - 12); }
      if (V('compare') >= 0.5){
        if (nl) banner('COMPARE  ·  THE ORIGINAL, ' + sgn(cmpDb) + ' dB', t.second, t.bg, display.b - 36);
        else banner('COMPARE  ·  THE ORIGINAL, ' + sgn(cmpDb) + ' dB TO MATCH THE MASTER\'S LOUDNESS', t.second, t.bg, display.b - 36);
      }
    });
    drawInfo();
    ui.stroke(mainP, t.dim, 1);
    ui.caption(mainP.x + 14, mainP.y + 15, 'MASTER');
    ui.caption(styleSeg.r.x, styleSeg.r.y - 10, 'STYLE');
    for (let i = 0; i < 4; i++){ const on = V(STAGE_ON[i]) >= 0.5, c = stageDim(i); ui.stroke(stageP[i], on ? c : alpha(c, 0.45), on ? 1.5 : 1); }
    drawStageText();
  }
  // ADVANCED: auto's nine tone points (squares, dragged up and down) and the free bands (numbered circles)
  function drawHandles(){
    const t = ui.th, g = ui.g;
    for (let i = 0; i < NEQ; i++){
      const f = AUTO_F[i], x = xOfF(f), y = Math.max(display.y + 6, Math.min(display.b - 6, yOfDb(curveAt(f)))), r = drag === 1 && dragIdx === i ? 6.5 : 5;
      ui.fill(R(x - r, y - r, 2 * r, 2 * r), pointColour(f, t.dark)); ui.stroke(R(x - r, y - r, 2 * r, 2 * r), t.ink, 1.2);
      if (Math.abs(V(TRIMS[i])) > 0.05){ g.beginPath(); g.arc(x, y, 1.6, 0, 7); g.fillStyle = css(t.bg); g.fill(); }
    }
    for (let b = 0; b < NUB; b++){
      if (!bandOn(b)) continue;
      const f = V(bs(b, 'freq')), x = xOfF(f), y = Math.max(display.y + 8, Math.min(display.b - 8, bandY(b)));
      g.beginPath(); g.arc(x, y, drag === 2 && dragIdx === b ? 10 : 8.5, 0, 7); g.fillStyle = css(pointColour(f, t.dark)); g.fill();
      g.strokeStyle = css(t.ink); g.lineWidth = 1.4; g.stroke();
      ui.text(x, y + 0.5, String(b + 1), 'bold', 10, t.dark ? t.bg : [255, 255, 255, 1], 'center');
    }
  }
  // ADVANCED: a compressor-style meter bridge: input (with the glue threshold), glue reduction, leveler lift, limiter
  // reduction, output (with the ceiling)
  function drawBridge(){
    const t = ui.th, r = info;
    ui.stroke(r, t.dim, 1);
    ui.caption(r.x + 12, r.y + 15, 'METERS');
    ui.text(r.r - 12, r.y + 15, ana.valid && planValid ? ana.lufs.toFixed(1) + ' -> ' + outPred.toFixed(1) + ' LUFS' : 'NOT LEARNED', 'mono', 10, t.ink2, 'right');
    const top = r.y + 44, bot = r.b - 22, h = bot - top, lo = -48, hi = 0, grMax = 12;
    const yLvl = d => bot - h * clamp((d - lo) / (hi - lo), 0, 1), yGr = d => top + h * clamp(d / grMax, 0, 1);
    const gw = (r.w - 24) / 5, names = ['IN', 'COMP', 'LEVEL', 'LIMIT', 'OUT'];
    for (let gi = 0; gi < 5; gi++){
      const gx = r.x + 12 + gi * gw, c = oklch(t.dark ? 0.74 : 0.62, 0.15, 10 + 72 * gi);
      ui.text(gx + gw / 2, r.y + 32, names[gi], 'label', 9.5, t.ink, 'center', 0.8);
      if (gi === 0 || gi === 4){
        const m = gi === 0 ? inMeter : outMeter;
        for (let ch = 0; ch < 2; ch++){
          const bar = R(gx + 6 + ch * (gw / 2 - 4), top, gw / 2 - 10, h); ui.fill(bar, t.screen);
          const y = yLvl(m.lvl[ch]); ui.fill(R(bar.x, y, bar.w, bot - y), m.lvl[ch] > -0.1 ? t.red : c);
          ui.line(bar.x, yLvl(m.hold[ch]), bar.r, yLvl(m.hold[ch]), t.ink, 1.5); ui.stroke(bar, t.ink, 1);
        }
        // the glue threshold on the input, the ceiling on the output
        const mark = gi === 0 ? thr : V('ceiling');
        if ((gi === 4 || ratio > 1.001) && mark > lo) ui.line(gx + 2, yLvl(mark), gx + gw - 2, yLvl(mark), t.second, 2);
        const hv = Math.max(m.hold[0], m.hold[1]);
        ui.text(gx + gw / 2, bot + 11, hv < -90 ? '-inf' : hv.toFixed(1), 'mono', 9.5, t.ink2, 'center');
      } else {
        const bar = R(gx + gw / 2 - 7, top, 14, h); ui.fill(bar, t.screen);
        const v = gi === 1 ? compGr : gi === 3 ? limGr : lift;
        if (gi === 2){ const y = bot - h * clamp(v / grMax, 0, 1); ui.fill(R(bar.x, y, bar.w, bot - y), c); }   // the lift grows up from the bottom (gain added)
        else { const y = yGr(v); ui.fill(R(bar.x, top, bar.w, y - top), c); }
        ui.stroke(bar, t.ink, 1);
        ui.text(gx + gw / 2, bot + 11, (gi === 2 ? '+' : '-') + Math.abs(v).toFixed(1), 'mono', 9.5, t.ink2, 'center');
      }
    }
    // scales: dBFS for the level meters (left), dB of gain change for the middle three (right)
    for (const d of [0, -6, -12, -24, -36]) ui.text(r.x + 9, yLvl(d), String(d), 'mono', 7.5, t.ink2, 'right');
    for (const d of [0, 3, 6, 9, 12]) ui.text(r.r - 3, yGr(d), String(d), 'mono', 7.5, t.ink2, 'right');
  }
  function drawInfo(){
    if (advanced){ drawBridge(); return; }
    const t = ui.th, r = info; ui.stroke(r, t.dim, 1);
    let y = r.y + 15; ui.caption(r.x + 12, y, 'SONG (LEARNED)'); y += 18;
    const row = (k, v, c) => { ui.text(r.x + 12, y, k, 'label', 10.5, t.ink2, 'left', 0.8); ui.text(r.r - 12, y, v, 'mono', 11, c || t.ink, 'right'); y += 16; };
    if (ana.valid){ row('LOUDNESS', ana.lufs.toFixed(1) + ' LUFS'); row('TRUE PEAK', ana.truePeak.toFixed(1) + ' dBTP'); row('RANGE', ana.lra.toFixed(1) + ' LU'); row('LENGTH', clock(ana.seconds)); }
    else { row('LOUDNESS', '--'); row('TRUE PEAK', '--'); row('RANGE', '--'); row('LENGTH', '--'); }
    y += 6; ui.caption(r.x + 12, y, 'MASTER'); y += 18;
    if (ana.valid && planValid){ row('WILL BE', outPred.toFixed(1) + ' LUFS'); row('CHANGE', sgn(outPred - ana.lufs) + ' LU'); }
    else { row('WILL BE', '--'); row('CHANGE', '--'); }
    row('NOW', Number.isFinite(outST) && outST > -70 ? outST.toFixed(1) + ' LUFS' : '--');
    // meters: output level, compressor and limiter
    const my = r.b - 46; ui.caption(r.x + 12, my - 2, 'OUTPUT');
    hMeter(R(r.x + 12, my + 6, r.w - 24, 7), outMeter.lvl[0], -48, 0, t.neon);
    hMeter(R(r.x + 12, my + 15, r.w - 24, 7), outMeter.lvl[1], -48, 0, t.neon);
    ui.text(r.x + 12, r.b - 12, 'LIMIT ' + limGr.toFixed(1) + ' dB', 'mono', 10, limGr > 3 ? t.red : t.ink2, 'left');
    ui.text(r.r - 12, r.b - 12, 'LIFT ' + sgn(lift), 'mono', 10, t.ink2, 'right');
  }
  function hMeter(m, db, lo, hi, c){ const t = ui.th; ui.fill(m, t.screen); ui.fill(R(m.x, m.y, m.w * clamp((db - lo) / (hi - lo), 0, 1), m.h), c); ui.stroke(m, t.ink, 1); }
  const stageInk = i => ui.th.dark ? oklch(0.80, 0.14, stageHue[i]) : oklch(0.50, 0.17, stageHue[i]);
  const stageDim = i => ui.th.dark ? oklch(0.45, 0.09, stageHue[i]) : oklch(0.80, 0.10, stageHue[i]);
  function drawStageText(){
    const t = ui.th;
    const lines = (i, a, b, bc) => { const p = stageP[i], c = V(STAGE_ON[i]) >= 0.5 ? stageInk(i) : t.ink2;
      const lo = i === 3 ? 12 : 0;   // LOUDNESS: under its dials (DESTINATION moved them down)
      ui.text(p.cx, p.b - 34 + lo, a, 'mono', 10.5, c, 'center'); ui.text(p.cx, p.b - 18 + lo * 0.75, b, 'mono', 10.5, bc || c, 'center'); };
    if (!(ana.valid && planValid)){
      // (the plugin writes this in all four; in ADVANCED, DYNAMICS and LOUDNESS have their dials there, so not those two)
      // (and learned but no frame yet: the song is stopped, the effect asleep, so the plan isn't known here yet)
      for (let i = 0; i < 4; i++) if (!(advanced && (i === 1 || i === 3))) ana.valid && !gotFrame ? lines(i, 'LEARNED', 'PLAY TO SEE IT') : lines(i, 'WAITING FOR', 'LEARN');
      return;
    }
    // TONE: the largest move and where
    let bestF = 1000, bestG = 0;
    for (let k = 0; k < NCP; k++) if (Math.abs(curve[k]) > Math.abs(bestG)){ bestG = curve[k]; bestF = curveFreq(k); }
    lines(0, Math.abs(bestG) < 0.05 ? 'FLAT' : sgn(bestG) + ' dB @ ' + (bestF < 1000 ? bestF.toFixed(0) + ' Hz' : (bestF / 1000).toFixed(1) + ' kHz'), 'LEVEL ' + sgn(eqDelta) + ' dB');
    // DYNAMICS
    if (!advanced) lines(1, 'QUIET PARTS +' + liftMax.toFixed(1) + ' dB', ratio > 1.001 ? 'GLUE ' + ratio.toFixed(2) + ':1  GR ' + compGr.toFixed(1) : 'NO COMPRESSION');
    // STEREO
    lines(2, sideLo < -0.05 ? 'LOWS ' + sideLo.toFixed(1) + ' dB SIDE' : 'LOWS UNCHANGED', Math.abs(sideHi) > 0.05 ? 'WIDTH ' + sgn(sideHi) + ' dB' : 'WIDTH UNCHANGED');
    // LOUDNESS
    const heavy = -limLoss > 1 || peakOver > 6;
    if (!advanced) lines(3, 'GAIN ' + sgn(makeup) + ' dB', peakOver < 0.05 ? 'NO LIMITING' : 'PEAKS -' + peakOver.toFixed(1) + ' dB' + (heavy ? ' · HEAVY' : ''), heavy ? t.red : null);
  }

  // ---- mouse on the display
  function down(x, y, btn, m, dbl){
    if (!display.has(x, y)) return false;
    if (advanced && planValid){
      const b = hitBand(x, y), ti = b < 0 ? hitTrim(x, y) : -1;
      if (btn === 2 && b >= 0){ bandMenu(b, x, y); return false; }
      if (btn === 0){
        if (b >= 0 && dbl){ ui.setGesture(bs(b, 'on'), 0); ui.status = 'Band ' + (b + 1) + ' off'; return false; }
        if (ti >= 0 && dbl){ ui.setGesture(TRIMS[ti], 0); ui.status = 'Back to what auto set'; return false; }
        if (b >= 0){ drag = 2; dragIdx = b; ui.begin(); ui.status = bandStatus(b); return true; }
        if (ti >= 0){ drag = 1; dragIdx = ti; dragLastY = y; dragV = V(TRIMS[ti]); ui.begin(); return true; }
        // empty curve: a new band here (with its drag, one undo step)
        let free = -1; for (let k = 0; k < NUB; k++) if (!bandOn(k)){ free = k; break; }
        if (free < 0){ ui.status = 'All eight bands are in use: double-click one to remove it'; return false; }
        const f = fOfX(x), gn = clamp((display.cy - y) / pxPerDb(), -18, 18);
        ui.begin();
        ui.setParam(bs(free, 'shape'), 0); ui.setParam(bs(free, 'q'), 1); ui.setParam(bs(free, 'freq'), f);
        ui.setParam(bs(free, 'gain'), m.shift ? 0 : gn); ui.setParam(bs(free, 'on'), 1);
        drag = 2; dragIdx = free; ui.status = bandStatus(free); return true;
      }
    }
    if (btn === 2){
      const rr = [3, 6, 12];
      ui.openMenu(R(x, y, 10, 10), rr.map(v => 'Curve scale ±' + v + ' dB'), rr.indexOf(dbRange), i => { dbRange = rr[i]; saveUi(); });
      return false;
    }
    return false;
  }
  function dragFn(x, y, m){
    hoverX = x; hoverY = y; const fine = m.shift ? 0.2 : 1;
    if (drag === 1){
      dragV = clamp(dragV + (dragLastY - y) / pxPerDb() * fine, -6, 6); dragLastY = y;
      ui.setParam(TRIMS[dragIdx], dragV);
      ui.status = TRIM_SN[dragIdx] + ' Hz  ' + sgn(autoEq[dragIdx] + dragV) + ' dB  (auto ' + sgn(autoEq[dragIdx]) + ', yours ' + sgn(dragV) + ')';
    } else if (drag === 2){
      const b = dragIdx;
      ui.setParam(bs(b, 'freq'), fOfX(clamp(x, display.x, display.r)));
      if (!bandIsCut(b)) ui.setParam(bs(b, 'gain'), clamp((display.cy - y) / pxPerDb(), -18, 18));
      ui.status = bandStatus(b);
    }
  }
  function up(){ if (drag) ui.end(); drag = 0; dragIdx = -1; }
  function hover(x, y){
    hoverX = x; hoverY = y;
    if (display.has(x, y)){
      const f = fOfX(x), b = advanced && planValid ? hitBand(x, y) : -1, ti = advanced && planValid && b < 0 ? hitTrim(x, y) : -1;
      if (b >= 0) ui.status = bandStatus(b) + '    drag · wheel: Q · right-click: shape · double-click: remove';
      else if (ti >= 0) ui.status = TRIM_SN[ti] + ' Hz  ' + sgn(autoEq[ti] + V(TRIMS[ti])) + ' dB  (auto ' + sgn(autoEq[ti]) + ', yours ' + sgn(V(TRIMS[ti])) + ')    drag · double-click: back to auto';
      else ui.status = f.toFixed(0) + ' Hz  ' + sgn(curveAt(f)) + ' dB    right-click: curve scale' + (advanced && planValid ? ' · click: a new band' : '');
    } else hoverX = -1;
  }
  function wheel(x, y, dy, m){
    if (!advanced || !display.has(x, y)) return false;
    const b = hitBand(x, y); if (b < 0) return false;
    const q = clamp(V(bs(b, 'q')) * Math.pow(2, (dy > 0 ? 1 : -1) * (m.shift ? 0.05 : 0.2)), 0.2, 10);
    ui.setGesture(bs(b, 'q'), q); ui.status = 'Band ' + (b + 1) + '  Q ' + q.toFixed(2); return true;
  }
  function bandMenu(b, x, y){
    ui.openMenu(R(x, y, 10, 10), [...SHAPES, '-', 'Remove band'], Math.round(V(bs(b, 'shape'))), i => {
      if (i < SHAPES.length) ui.setGesture(bs(b, 'shape'), i); else if (i === SHAPES.length + 1){ ui.setGesture(bs(b, 'on'), 0); ui.status = 'Band ' + (b + 1) + ' off'; } });
  }
  function key(e, m){
    if (m.cmd || m.alt) return false;
    const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (k === 'l'){ toggleLearn(); return true; }
    if (k === 'c'){ ui.setGesture('compare', V('compare') >= 0.5 ? 0 : 1); return true; }
    if (k === 'a'){ setAdvanced(!advanced); return true; }
    if (k >= '1' && k <= '4'){ const s = STAGE_ON[+k - 1]; ui.setGesture(s, V(s) >= 0.5 ? 0 : 1); return true; }
    return false;
  }

  // ---- telemetry (pump's wasm/fx/master.cpp: t[5 + i])
  function frame(fr){
    const t = fr.t; if (!t) return;
    pin[0] = Math.max(pin[0], t[3]); pin[1] = Math.max(pin[1], t[4]); pout[0] = Math.max(pout[0], t[1]); pout[1] = Math.max(pout[1], t[2]);
    for (const rg of fr.rings || []) if (rg.k === 1) fPost.push(rg);
    if (fr.sr){ post.setup(fr.sr, U.analyzerSize(fr.sr)); const nowT = performance.now(), dt = lastFrameT ? clamp((nowT - lastFrameT) / 1000, 0.005, 0.25) : 1 / 31; lastFrameT = nowT;
      if (fPost.any) Spectrum.update(dt, post, fPost); }
    if (t.length < B0 + 303) return;
    gotFrame = true;
    learning = t[B0] >= 0.5; ana.valid = t[B0 + 1] >= 0.5; learnSec = t[B0 + 3]; planValid = t[B0 + 4] >= 0.5;
    if (ana.valid){ ana.lufs = t[B0 + 2]; ana.seconds = t[B0 + 5]; ana.lra = t[B0 + 6]; ana.truePeak = t[B0 + 7]; }
    for (let i = 0; i < NCP; i++){ curveT[i] = t[B0 + 8 + i]; bandsT[i] = t[B0 + 104 + i]; }
    for (let b = 0; b < NB; b++){ target[b] = t[B0 + 200 + b]; measured[b] = t[B0 + 231 + b]; }
    for (let i = 0; i < NEQ; i++){ eqGain[i] = t[B0 + 262 + i]; autoEq[i] = t[B0 + 271 + i]; }
    const v = i => t[B0 + 280 + i];
    ratio = v(0); thr = v(1); liftMax = v(2); lift = v(3); sideLo = v(4); sideHi = v(5); makeup = v(6); outPred = v(7);
    cmpDb = v(8); peakOver = v(9); limLoss = v(10); eqDelta = v(11); inST = v(12); outST = v(13);
    aRatio = v(14); aThr = v(15); aLift = v(16); aMakeup = v(17); aSideLoDb = v(18); aSideHiDb = v(19);
    pcg = Math.max(pcg, v(20)); plg = Math.max(plg, v(21));
  }
  function tick(dt){
    meterTick(outMeter, pout, dt); meterTick(inMeter, pin, dt);
    const k = 1 - Math.exp(-dt / 0.12);
    for (let i = 0; i < NCP; i++){ curve[i] += (curveT[i] - curve[i]) * k; bandsCurve[i] += (bandsT[i] - bandsCurve[i]) * k; }
    compGr = Math.max(pcg, compGr - 12 * dt); limGr = Math.max(plg, limGr - 12 * dt); pcg = plg = 0;
    learnBtn.label = learning ? 'STOP ' + clock(learnSec) : ana.valid ? 'RE-LEARN' : 'LEARN';
    // the spectrum scale follows the learned song
    if (ana.valid && planValid){ let mx = -200; for (let b = 0; b < NB; b++) if (bandFreq(b) < 16000) mx = Math.max(mx, measured[b]); specTop = mx + 6; specBottom = mx - 30; }
  }

  fromState(); learnBtn.label = ana.valid ? 'RE-LEARN' : 'LEARN';
  return {
    layout, drawBody, down, drag:dragFn, up, hover, wheel, key, frame, tick,
    leave(){ hoverX = -1; },
    reload(){ fromState(); },
    animating(){ return U.now() - ui.lastFrame < 2 || outMeter.lvl[0] > -100 || outMeter.lvl[1] > -100 || compGr > 0.05 || limGr > 0.05; },
    entryText(s){ if (!SHOWN.has(s)) return null; const sv = shownValue(s, V(s)); return s === 'ratio_trim' ? sv.toFixed(2) : s === 'low_mono_trim' || s === 'width_trim' ? sv.toFixed(0) : sv.toFixed(1); },
    preParse(s, txt){ if (!SHOWN.has(s)) return txt; const n = parseFloat(String(txt).trim().replace(',', '.')); return Number.isFinite(n) ? String(fromShown(s, n)) : txt; },
    fmtStatus:s => formatParam(s, V(s)),
    footer(){ return 'LATENCY ' + (1000 * (ui.lat || 0) / (ui.sr || 48000)).toFixed(1) + ' ms  ·  ' + (ana.valid ? 'LEARNED' : 'NOT LEARNED YET'); },
    help(){ return [
      ['L', 'learn: start, and stop when the song has played (stopping the song also finishes it)'],
      ['C', 'compare with the original at the same loudness (COMPARE key: hold = only while held)'],
      ['1  2  3  4', 'tone · dynamics · stereo · loudness on/off'],
      ['INTENSITY', 'everything at once: 25 % light, 50 % medium, 100 % strong'],
      ['AMOUNT', 'one stage on top of INTENSITY'],
      ['A', 'advanced view: everything auto set, as offsets you can tweak, and eight free EQ bands'],
      ['Advanced: drag a square', 'trim auto’s tone at that point (double-click: back to auto; Shift: fine)'],
      ['Advanced: click the curve', 'a new band there (drag: frequency and gain, wheel: Q, right-click: shape, double-click: remove)'],
      ['Right-click the display', 'curve scale']]; },
  };
}});
})();
