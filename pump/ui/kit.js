/* …pump pop-up windows: the shared kit (stage 2 of the pump effects, 23 Oct 2026).
   Each plugin's window is drawn on one canvas in the plugin's own logical coordinates, scaled to the window, so its
   layout, colours and mouse conventions are the plugin's: matthettich/pump common/ui/PumpUI.hpp (header, controls,
   typed entry, menus, help, status line) and common/ui/Theme.hpp (accents, light/dark palettes) are the reference, and
   each plugin's pump/ui/<type>.js follows its plugins/<p>/UI<Name>.cpp. Everything goes through window.PumpFX
   (docs/PUMP-FX.md; the kit's own API is documented there too, under "The pop-up kit").
   A plugin file calls PumpUI.define(type, {W, H, nW, nH, bypass, back, make(ui)}); make returns its hooks. */
(() => {
'use strict';
const FX = window.PumpFX;
if (!FX || !FX.meta) return;
const PM = FX.meta;
// A window's draw and tick run every animation frame, so a plugin whose code throws would otherwise log ~60 warnings a
// second for as long as the window is open. Each distinct message is reported once (to the page's tpLog when it has one).
const warnSeen = new Set();
function warnOnce(where, e){
  try {
    const msg = String(e && e.message || e), key = where + '|' + msg; if (warnSeen.has(key) || warnSeen.size >= 50) return; warnSeen.add(key);
    if (typeof window.tpLog === 'function') window.tpLog(where, e); else console.warn('[thunder] ' + where + ': ' + msg);
  } catch (_) { /* the reporter must never throw */ }
}

/* ---------------------------------------------------------------- colours (Theme.hpp) */
const hex = h => [(h >> 16) & 255, (h >> 8) & 255, h & 255, 1];
const mix = (a, b, t) => [a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t];
const alpha = (c, a) => [c[0], c[1], c[2], a];
const css = c => typeof c === 'string' ? c : `rgba(${Math.round(c[0])},${Math.round(c[1])},${Math.round(c[2])},${+c[3].toFixed(3)})`;
// the suite's accents: neon (fills, curves), deep (the ink in light mode), second (second curves, side, selections)
const ACCENTS = {eq:[0x4DF3FF, 0x00636D, 0xFF4FA0], comp:[0xFF74DC, 0x8E0F68, 0x19B5A5], limit:[0xC8FF4D, 0x3B5C00, 0x7A5CFF],
  calm:[0xB9A0FF, 0x4528B5, 0xFFAE3B], tone:[0x5CFFB4, 0x006E44, 0xFF6E4A], band:[0xFFB24F, 0x934000, 0x2F86FF],
  wide:[0x73B4FF, 0x1443AC, 0xFF8A3D], heat:[0xFF8676, 0xA21F12, 0x2A9BC9], meter:[0xFFF04F, 0x625800, 0xB64DFF],
  // (31 Oct) the windows of MASTER, GATE, TUNE and CHOP
  master:[0xDCDCE3, 0x3B3B44, 0x9A63FF], gate:[0x5CCBFF, 0x00527A, 0xFF8F3D], tune:[0xFFAA85, 0x8A3210, 0x2E86E0], chop:[0xC6FF6B, 0x3E6100, 0xD45CFF]};
function buildTheme(type, dark){
  const a = ACCENTS[type] || ACCENTS.eq, neon = hex(a[0]), deep = hex(a[1]), second = hex(a[2]), white = hex(0xFFFFFF), black = hex(0x07070B), t = {dark};
  if (!dark){
    t.bg = mix(white, neon, .16); t.panel = mix(white, neon, .08); t.panel2 = mix(white, neon, .30); t.ink = deep; t.ink2 = mix(deep, t.bg, .38);
    t.dim = mix(mix(neon, deep, .30), white, .42); t.neon = neon; t.neonInk = deep; t.on = mix(white, neon, .06); t.second = mix(second, black, .08);
    t.red = hex(0xD0302A); t.grid = alpha(deep, .11); t.screen = mix(white, neon, .05); t.glow = alpha(neon, 0);
  } else {
    t.bg = mix(black, neon, .07); t.panel = mix(black, neon, .10); t.panel2 = mix(black, neon, .19); t.ink = mix(neon, white, .18); t.ink2 = mix(neon, t.bg, .38);
    t.dim = mix(neon, t.bg, .70); t.neon = neon; t.neonInk = mix(black, neon, .05); t.on = mix(black, neon, .05); t.second = mix(second, white, .15);
    t.red = hex(0xFF5A4E); t.grid = alpha(neon, .10); t.screen = mix(black, neon, .04); t.glow = alpha(neon, .22);
  }
  return t;
}
// Light or dark: THEME 'skin' follows Thunder's skin (a dark skin gets pump's dark look), 'light' / 'dark' always that.
// The window's DARK key overrides it on this device (remembered for every window, as the plugin remembers its theme);
// Ctrl/Cmd-click DARK goes back to THEME. Change THEME here to change the default.
const THEME = 'skin', THEME_KEY = 'thunderplus-pump-theme-v1';
function skinDark(){
  try { const c = getComputedStyle(document.body).backgroundColor, m = c && c.match(/\d+(\.\d+)?/g);
    if (m && m.length >= 3 && !(m.length > 3 && +m[3] === 0)) return (0.2126 * m[0] + 0.7152 * m[1] + 0.0722 * m[2]) / 255 < 0.45;
    return getComputedStyle(document.documentElement).colorScheme.includes('dark'); } catch(e){ return false; }
}
/* SKIN THEME (owner, 24 Oct: the windows' own pink / cyan / lime palettes looked out of place in Thunder): unless the DARK
   key has picked pump's own light or dark look on this device, a window takes its colours from Thunder's skin, the
   tracker screen's (the --ts-* and --tk-* variables): its background and ink, the cursor's colour as the accent, the
   track colours as the second accent. themeFor() is the one entry; buildTheme() stays pump's own palettes. */
let probe = null;
function skinColor(name){
  try {
    const host = document.querySelector('.tscr') || document.documentElement;
    if (!probe || probe.parentNode !== host){ probe = document.createElement('i'); probe.style.cssText = 'position:absolute;width:0;height:0;overflow:hidden;visibility:hidden;pointer-events:none'; host.appendChild(probe); }
    probe.style.color = 'var(' + name + ')';
    const c = getComputedStyle(probe).color || '', n = (c.match(/-?\d*\.?\d+(e-?\d+)?/g) || []).map(Number);
    if (!probe.style.color || n.length < 3) return null;
    if (/^color\(/.test(c)) return [n[0] * 255, n[1] * 255, n[2] * 255, n.length > 3 ? n[3] : 1];   // color(srgb r g b / a), from color-mix()
    return [n[0], n[1], n[2], n.length > 3 ? n[3] : 1];
  } catch(e){ return null; }
}
const lum = c => (0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2]) / 255;
const near = (a, b) => Math.abs(a[0] - b[0]) + Math.abs(a[1] - b[1]) + Math.abs(a[2] - b[2]) < 40;
function skinTheme(){
  const bg = skinColor('--ts-bg'), ink = skinColor('--ts-ink'); if (!bg || !ink) return null;
  const flat = c => c && c[3] < 1 ? mix(alpha(c, 1), bg, 1 - c[3]).map((v, i) => i === 3 ? 1 : v) : c;
  bg[3] = ink[3] = 1;
  const cur = flat(skinColor('--ts-cur-bg')), curInk = flat(skinColor('--ts-cur')), mono = flat(skinColor('--tk-mono')), poly = flat(skinColor('--tk-poly'));
  const line = flat(skinColor('--ts-line')), mute = flat(skinColor('--ts-mute')), dark = lum(bg) < 0.45, t = {dark, skin:true};
  // the accent: the cursor's colour, unless that's just the ink (the plain skins), then a track colour
  const pick = (...L) => L.find(c => c && !near(c, ink) && !near(c, bg));
  const neon = pick(cur, mono, poly) || ink, second = pick(...[poly, mono, cur].filter(c => c && !near(c, neon))) || mix(neon, ink, 0.5);
  t.bg = bg; t.screen = mix(bg, ink, dark ? 0.05 : 0.025); t.panel = mix(bg, ink, 0.04); t.panel2 = mix(bg, ink, 0.10); t.ink = ink; t.ink2 = mix(ink, bg, 0.38);
  t.dim = line && !near(line, bg) ? line : mix(ink, bg, 0.65); t.neon = neon; t.neonInk = curInk && near(neon, cur || [-99, 0, 0]) ? curInk : (lum(neon) > 0.55 ? hex(0x07070B) : hex(0xFFFFFF));
  t.on = mix(bg, neon, 0.08); t.second = second; t.red = mute || hex(0xD0302A); t.grid = alpha(ink, dark ? 0.12 : 0.10); t.glow = alpha(neon, dark ? 0.22 : 0);
  return t;
}
function themeFor(type){
  let v = null; try { v = localStorage.getItem(THEME_KEY); } catch(e){}
  if (v === 'dark' || v === 'light') return buildTheme(type, v === 'dark');
  if (THEME === 'skin'){ const t = skinTheme(); if (t) return t; }
  return buildTheme(type, themeDark());
}
// Thunder calls this when the skin changes: open windows that follow it repaint in the new colours.
FX.skinChanged = () => { for (const w of wins.values()){ w.th = themeFor(w.type); w.dark = w.th.dark; w.styleDom(); w.dirty = true; } };
function themeDark(){
  let v = null; try { v = localStorage.getItem(THEME_KEY); } catch(e){}
  if (v === 'dark' || v === 'light') return v === 'dark';
  return THEME === 'dark' ? true : THEME === 'light' ? false : skinDark();
}

/* ---------------------------------------------------------------- fonts (the plugins': Share Tech Mono labels, JetBrains Mono values) */
const FAM = {label:'"Share Tech Mono","JetBrains Mono",ui-monospace,monospace', mono:'"JetBrains Mono",ui-monospace,monospace', bold:'"JetBrains Mono",ui-monospace,monospace'};
// NanoVG's font size is the ascent-to-descent height, CSS's the em: these turn the plugins' sizes into the same text
// (Share Tech Mono 1000 / (885 + 242), JetBrains Mono 1000 / (1020 + 300)). The narrow layouts read a little larger.
const EM = {label:0.8873, mono:0.7576, bold:0.7576}, NARROW_TEXT = 1.12;
const fontOf = (f, size, big) => (f === 'bold' ? '700 ' : '') + (size * EM[f] * (big ? NARROW_TEXT : 1)).toFixed(2) + 'px ' + FAM[f];
try { Promise.all([document.fonts.load(fontOf('label', 12)), document.fonts.load(fontOf('mono', 12)), document.fonts.load(fontOf('bold', 12))]).then(() => { for (const w of wins.values()) w.dirty = true; }, () => {}); } catch(e){}

/* ---------------------------------------------------------------- geometry */
class Rect {
  constructor(x = 0, y = 0, w = 0, h = 0){ this.x = x; this.y = y; this.w = w; this.h = h; }
  get r(){ return this.x + this.w; } get b(){ return this.y + this.h; } get cx(){ return this.x + this.w / 2; } get cy(){ return this.y + this.h / 2; }
  has(px, py){ return px >= this.x && py >= this.y && px < this.x + this.w && py < this.y + this.h; }
  inset(d){ return new Rect(this.x + d, this.y + d, this.w - 2 * d, this.h - 2 * d); }
}
const R = (x, y, w, h) => new Rect(x, y, w, h);
const clamp = (v, a, b) => v < a ? a : v > b ? b : v;
const dbOf = v => v > 1e-9 ? 20 * Math.log10(v) : -180;
const now = () => performance.now() / 1000;
const isMac = /Mac|iP(hone|ad|od)/.test(navigator.platform || navigator.userAgent || '');
const modsOf = e => ({shift:!!e.shiftKey, alt:!!e.altKey, cmd:isMac ? !!e.metaKey : !!(e.ctrlKey || e.metaKey), ctrl:!!e.ctrlKey});

/* ---------------------------------------------------------------- parameters: typed entry (ParamSpec::parse) */
const PF = {AUTO:1, INT:2, BOOL:4, OUT:8, BYP:16, HID:32, NOUNDO:64};
const skipFlags = p => !!(p.flags & (PF.OUT | PF.BYP | PF.NOUNDO | PF.HID));   // never in presets, A/B or undo (the plugins' rule)
function parseNumber(s){
  const m = /^\s*([+-]?)(\d+(?:[.,]\d*)?|[.,]\d+)(?:[eE]([+-]?\d+))?/.exec(s);
  if (!m) return null;
  let v = parseFloat(m[2].replace(',', '.')); if (m[3]) v *= Math.pow(10, +m[3]);
  return {v:m[1] === '-' ? -v : v, rest:s.slice(m[0].length)};
}
function parseParam(type, p, text){
  text = String(text).trim(); if (!text) return null;
  if (p.unit === 'Hz' && text.length >= 2){
    const m = /^([A-Ga-g])(#|b)?(-?\d+)([+-]\d+(?:[.,]\d+)?)?/.exec(text);
    if (m){ const semis = {A:9, B:11, C:0, D:2, E:4, F:5, G:7}; let s = semis[m[1].toUpperCase()] + (m[2] === '#' ? 1 : m[2] === 'b' ? -1 : 0);
      const midi = 12 * (+m[3] + 1) + s + (m[4] ? parseFloat(m[4].replace(',', '.')) / 100 : 0);
      return FX.clamp(type, p.s, 440 * Math.pow(2, (midi - 69) / 12)); }
  }
  if (text.length >= 2 && text.endsWith('%') && p.unit !== '%'){ const n = parseNumber(text); if (n) return FX.fromNorm(type, p.s, n.v / 100); }
  if (p.ch){ const lt = text.toLowerCase(); const i = p.ch.findIndex(c => c.toLowerCase().startsWith(lt)); if (i >= 0) return p.min + i; }
  if (/^-?inf/i.test(text)) return text[0] === '-' ? p.min : p.max;
  if (p.flags & PF.BOOL){ if (/^on/i.test(text)) return 1; if (/^of/i.test(text)) return 0; }
  const n = parseNumber(text); if (!n) return null;
  let v = n.v; const e = n.rest.trim();
  if (/^x/i.test(e) && p.unit === 'dB') v = 20 * Math.log10(Math.max(v, 1e-6));
  else if (/^k/i.test(e)) v *= 1000;
  else if (p.unit === 'ms' && /^s/i.test(e) && !/^ms/i.test(e)) v *= 1000;
  return Number.isFinite(v) ? FX.clamp(type, p.s, v) : null;
}

/* ---------------------------------------------------------------- sample feeds and the spectrum analyzer (SpectrumView) */
// A ring of a telemetry feed's samples by their absolute (uint32) position: push(frame ring) keeps it current.
class Feed {
  constructor(cap){ this.cap = cap; this.mask = cap - 1; this.buf = new Float32Array(cap); this.pos = 0; this.any = false; }
  push(rg){ const d = rg.d, n = d.length, p0 = (rg.pos - n) >>> 0; for (let i = 0; i < n; i++) this.buf[(p0 + i) & this.mask] = d[i]; this.pos = rg.pos >>> 0; if (n) this.any = true; }
  at(i){ return this.buf[(i >>> 0) & this.mask]; }
  latest(out, n){ const p0 = (this.pos - n) >>> 0; for (let i = 0; i < n; i++) out[i] = this.buf[(p0 + i) & this.mask]; }
}
const analyzerSize = sr => { let n = 8192; while (n < 32768 && sr / (n / 8192) > 66000) n *= 2; return n; };
const ffts = new Map();
function fftOf(N){   // complex radix-2, in place, forward (e^-i)
  if (ffts.has(N)) return ffts.get(N);
  const cs = new Float64Array(N / 2), sn = new Float64Array(N / 2), rev = new Uint32Array(N), bits = Math.log2(N) | 0;
  for (let i = 0; i < N / 2; i++){ cs[i] = Math.cos(2 * Math.PI * i / N); sn[i] = Math.sin(2 * Math.PI * i / N); }
  for (let i = 0; i < N; i++){ let r = 0; for (let b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b); rev[i] = r; }
  const f = (re, im) => {
    for (let i = 0; i < N; i++){ const j = rev[i]; if (j > i){ let t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; } }
    for (let size = 2; size <= N; size <<= 1){ const half = size >> 1, step = N / size;
      for (let i = 0; i < N; i += size) for (let j = 0, k = 0; j < half; j++, k += step){
        const a = i + j, b = a + half, xr = re[b], xi = im[b], c = cs[k], s = sn[k], tr = xr * c + xi * s, ti = xi * c - xr * s;
        re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti; } }
  };
  ffts.set(N, f); return f;
}
class Spectrum {
  constructor(lo = 18, hi = 22000){ this.P = 320; this.db = new Float32Array(this.P).fill(-120); this.lo = lo; this.hi = hi; this.sr = 0; }
  setup(sr, N){ N = N || analyzerSize(sr); if (sr === this.sr && N === this.N) return; this.sr = sr; this.N = N;
    this.win = new Float32Array(N); let ws = 0;
    for (let i = 0; i < N; i++){ const t = 2 * Math.PI * i / (N - 1), w = 0.35875 - 0.48829 * Math.cos(t) + 0.14128 * Math.cos(2 * t) - 0.01168 * Math.cos(3 * t); this.win[i] = w; ws += w; }
    this.norm = 2 / ws; this.pow = new Float64Array(N / 2); this.db.fill(-120); }
  setRange(lo, hi){ this.lo = lo; this.hi = hi; }
  // fold this analyzer's power spectrum into its 320 display points (SpectrumView::update's mapping and ballistics)
  points(dt, speed = 1){
    const P = this.P, N = this.N, binHz = this.sr / N, decay = 30 * dt * speed, rise = Math.min(1, 0.55 * speed * dt * 60), nyq = this.sr / 2, pw = this.pow, lo = this.lo, hi = this.hi;
    for (let p = 0; p < P; p++){
      const f0 = lo * Math.pow(hi / lo, p / P), f1 = lo * Math.pow(hi / lo, (p + 1) / P);
      if (f0 >= nyq){ this.db[p] = Math.max(-140, this.db[p] - decay); continue; }
      let k0 = Math.floor(f0 / binHz), k1 = Math.ceil(f1 / binHz); k0 = Math.max(1, k0); k1 = Math.min(N / 2 - 1, Math.max(k1, k0 + 1));
      let m;
      if (k1 - k0 <= 2){ const fc = Math.sqrt(f0 * f1) / binHz, k = Math.min(N / 2 - 2, Math.max(1, fc | 0)), t = fc - k; m = pw[k] * (1 - t) + pw[k + 1] * t; }
      else { let s = 0; for (let k = k0; k < k1; k++) s += pw[k]; m = s / (k1 - k0) * 2; }
      const fc = Math.sqrt(f0 * f1); let v = 10 * Math.log10(m + 1e-24) + 4.5 * Math.log2(fc / 1000); v = Math.max(v, -140);
      if (v > this.db[p]) this.db[p] += (v - this.db[p]) * rise; else this.db[p] = Math.max(v, this.db[p] - decay);
    }
  }
  pointDb(p){ const a = p > 0 ? p - 1 : p, b = p < this.P - 1 ? p + 1 : p; return 0.25 * this.db[a] + 0.5 * this.db[p] + 0.25 * this.db[b]; }
  pointFreq(p){ return this.lo * Math.pow(this.hi / this.lo, (p + 0.5) / this.P); }
  // One or two analyzers of the same size from their feeds (two real FFTs in one complex one).
  static update(dt, A, fa, B, fb){
    const N = A.N, re = Spectrum._re && Spectrum._re.length === N ? Spectrum._re : (Spectrum._re = new Float64Array(N)), im = Spectrum._im && Spectrum._im.length === N ? Spectrum._im : (Spectrum._im = new Float64Array(N));
    const ta = Spectrum._ta && Spectrum._ta.length === N ? Spectrum._ta : (Spectrum._ta = new Float32Array(N)), tb = Spectrum._tb && Spectrum._tb.length === N ? Spectrum._tb : (Spectrum._tb = new Float32Array(N));
    fa.latest(ta, N); if (B) fb.latest(tb, N);
    for (let i = 0; i < N; i++){ re[i] = ta[i] * A.win[i]; im[i] = B ? tb[i] * A.win[i] : 0; }
    fftOf(N)(re, im);
    const g = A.norm * A.norm;
    for (let k = 1; k < N / 2; k++){ const r1 = re[k], i1 = im[k], r2 = re[N - k], i2 = im[N - k];
      if (B){ const ar = (r1 + r2) / 2, ai = (i1 - i2) / 2, br = (i1 + i2) / 2, bi = -(r1 - r2) / 2; A.pow[k] = (ar * ar + ai * ai) * g; B.pow[k] = (br * br + bi * bi) * g; }
      else A.pow[k] = (r1 * r1 + i1 * i1) * g; }
    A.points(dt); if (B) B.points(dt);
  }
}

/* ---------------------------------------------------------------- controls (PumpUI.hpp's Dial, SliderRow, Button, Segmented, MenuButton) */
class Ctl {
  constructor(){ this.r = R(); this.sym = null; this.visible = true; this.enabled = true; this.label = ''; this.tip = ''; this.tabGroup = 0; }
  draw(){} down(){ return false; } drag(){} up(){} wheel(){ return false; }
  get wantsText(){ return this.sym != null; }
  valueRect(){ return this.r; }
}
class Dial extends Ctl {
  constructor(sym, label, size = 'm', bipolar = false){ super(); this.sym = sym; this.label = label; this.size = size; this.bipolar = bipolar; this.fmt = null; }
  get rad(){ return this.size === 's' ? 15 : this.size === 'l' ? 27 : 20; }
  static heightFor(s){ const rad = s === 's' ? 15 : s === 'l' ? 27 : 20; return 18 + 2 * rad + 22; }
  valueRect(){ const cy = this.r.y + 18 + this.rad; return R(this.r.x - 8, cy + this.rad + 3, this.r.w + 16, 18); }
  draw(ui){
    const t = ui.th, dis = !this.enabled, rad = this.rad, cx = this.r.cx, cy = this.r.y + 16 + rad + 2, g = ui.g, sw = this.size === 's' ? 4 : this.size === 'l' ? 6 : 5;
    ui.text(cx, this.r.y + 7, this.label, 'label', this.size === 's' ? 10.5 : 11.5, dis ? t.dim : t.ink2, 'center', 1);
    const a0 = Math.PI * 0.75, a1 = Math.PI * 2.25, n = ui.norm(this.sym);
    g.beginPath(); g.arc(cx, cy, rad, a0, a1); g.strokeStyle = css(t.dim); g.lineWidth = sw; g.stroke();
    const from = this.bipolar ? Math.PI * 1.5 : a0, to = a0 + (a1 - a0) * n;
    if (Math.abs(to - from) > 0.01){ g.beginPath(); g.arc(cx, cy, rad, Math.min(from, to), Math.max(from, to)); g.strokeStyle = css(dis ? t.dim : t.neon); g.lineWidth = sw; g.stroke(); }
    g.beginPath(); g.arc(cx, cy, rad - 5, 0, Math.PI * 2); g.fillStyle = css(t.bg); g.fill(); g.strokeStyle = css(dis ? t.dim : t.ink); g.lineWidth = 1.5; g.stroke();
    ui.line(cx + Math.cos(to) * (rad - 14) * 0.25, cy + Math.sin(to) * (rad - 14) * 0.25, cx + Math.cos(to) * (rad - 6.5), cy + Math.sin(to) * (rad - 6.5), dis ? t.dim : t.ink, 2.2);
    ui.text(cx, cy + rad + 12, this.fmt ? this.fmt(ui.value(this.sym)) : ui.fmt(this.sym), 'mono', this.size === 's' ? 10 : 11, dis ? t.dim : t.ink, 'center');
  }
  down(ui, x, y, btn){ if (btn !== 0) return false; this.ds = this.sym; this.n0 = ui.norm(this.ds); this.x0 = x; this.y0 = y; ui.begin(); return true; }
  drag(ui, x, y, m){ if (this.ds == null) return; const sens = m.shift ? 0.0008 : 0.005; ui.setNorm(this.ds, this.n0 + ((x - this.x0) - (y - this.y0)) * sens);
    if (m.shift){ this.n0 = ui.norm(this.ds); this.x0 = x; this.y0 = y; } }
  up(ui){ if (this.ds != null) ui.end(); this.ds = null; }
  wheel(ui, dy, m){ const p = ui.spec(this.sym); if (p.flags & (PF.INT | PF.BOOL)) ui.setGesture(this.sym, ui.value(this.sym) + (dy > 0 ? 1 : -1));
    else { const st = m.shift ? 0.002 : 0.02; ui.begin(); ui.setNorm(this.sym, ui.norm(this.sym) + (dy > 0 ? st : -st)); ui.end(); } return true; }
}
class Row extends Ctl {
  constructor(sym, label, bipolar = false){ super(); this.sym = sym; this.label = label; this.bipolar = bipolar; this.labelW = 92; this.valueW = 74; this.fmt = null; }
  valueRect(){ return R(this.r.r - this.valueW, this.r.y, this.valueW, this.r.h); }
  draw(ui){
    const t = ui.th, dis = !this.enabled, r = this.r;
    ui.text(r.x, r.cy, this.label, 'label', 12, dis ? t.dim : t.ink2, 'left', 0.8);
    const bar = R(r.x + this.labelW, r.cy - 6, r.w - this.labelW - this.valueW - 8, 12), n = ui.norm(this.sym);
    ui.fill(bar, t.panel2); const x0 = this.bipolar ? bar.x + bar.w / 2 : bar.x, x1 = bar.x + bar.w * n;
    ui.fill(R(Math.min(x0, x1), bar.y, Math.abs(x1 - x0), bar.h), dis ? t.dim : t.neon); ui.stroke(bar, dis ? t.dim : t.ink, 1.2);
    ui.fill(R(x1 - 1.5, bar.y - 3, 3, bar.h + 6), dis ? t.dim : t.ink);
    ui.text(r.r, r.cy, this.fmt ? this.fmt(ui.value(this.sym)) : ui.fmt(this.sym), 'mono', 11.5, dis ? t.dim : t.ink, 'right');
  }
  down(ui, x, y, btn){ if (btn !== 0) return false; this.ds = this.sym; this.n0 = ui.norm(this.ds); this.x0 = x; ui.begin(); return true; }
  drag(ui, x, y, m){ if (this.ds == null) return; const bw = Math.max(40, this.r.w - this.labelW - this.valueW - 8); ui.setNorm(this.ds, this.n0 + (x - this.x0) / bw * (m.shift ? 0.15 : 1));
    if (m.shift){ this.n0 = ui.norm(this.ds); this.x0 = x; } }
  up(ui){ if (this.ds != null) ui.end(); this.ds = null; }
  wheel(ui, dy, m){ return Dial.prototype.wheel.call(this, ui, dy, m); }
}
class Btn extends Ctl {
  constructor(label, onClick, isOn){ super(); this.label = label; this.onClick = onClick || null; this.isOnFn = isOn || null; this.small = false; this.pressed = false; this.holdMomentary = false; this.onExclusive = null; }
  get wantsText(){ return false; }
  isOn(ui){ return this.isOnFn ? !!this.isOnFn() : this.sym != null ? ui.value(this.sym) >= 0.5 : false; }
  draw(ui){ const t = ui.th, on = this.isOn(ui);
    ui.keyBox(this.r, on, this.pressed || on, null, this.small ? 3 : 4);
    ui.text(this.r.cx, this.r.cy + 0.5, this.label, 'label', this.small ? 11.5 : 12.5, on ? t.on : (this.enabled ? t.ink : t.dim), 'center', 1); }
  toggle(ui){ if (this.sym != null){ const p = ui.spec(this.sym); ui.setGesture(this.sym, ui.value(this.sym) >= 0.5 ? p.min : p.max); } else if (this.onClick) this.onClick(); }
  down(ui, x, y, btn, m){ if (btn !== 0) return false; this.pressed = true; this.t0 = now(); this.excl = false;
    if (this.onExclusive && m.cmd){ this.excl = true; this.onExclusive(); return true; }
    this.toggle(ui); return true; }
  up(ui){ this.pressed = false; if (!this.holdMomentary || this.excl) return; if (now() - this.t0 >= 0.3) this.toggle(ui); }   // held: momentary
}
class Seg extends Ctl {
  constructor(sym, options){ super(); this.sym = sym; this.options = options || null; this.getFn = null; this.setFn = null; this.vertical = false; }
  get wantsText(){ return false; }
  count(ui){ return this.options ? this.options.length : this.sym != null ? ui.spec(this.sym).max - ui.spec(this.sym).min + 1 : 0; }
  selected(ui){ return this.getFn ? this.getFn() : this.sym != null ? Math.round(ui.value(this.sym) - ui.spec(this.sym).min) : -1; }
  text(ui, i){ return this.options ? this.options[i] : (ui.spec(this.sym).ch || [])[i] || String(i); }
  draw(ui){ const t = ui.th, n = this.count(ui), r = this.r; if (n <= 0) return; const sel = this.selected(ui);
    ui.fill(R(r.x + 3, r.y + 3, r.w, r.h), t.bg); ui.stroke(R(r.x + 3, r.y + 3, r.w, r.h), t.dim, 1.5);
    for (let i = 0; i < n; i++){ const c = this.vertical ? R(r.x, r.y + r.h * i / n, r.w, r.h / n) : R(r.x + r.w * i / n, r.y, r.w / n, r.h), on = i === sel;
      ui.fill(c, on ? t.ink : t.bg); ui.stroke(c, t.ink, 1.5);
      ui.text(c.cx, c.cy + 0.5, this.text(ui, i), 'label', 11.5, on ? t.on : (this.enabled ? t.ink : t.dim), 'center', 0.8); } }
  down(ui, x, y, btn){ if (btn !== 0) return false; const n = this.count(ui); if (n <= 0) return true;
    const i = clamp(this.vertical ? Math.floor((y - this.r.y) / this.r.h * n) : Math.floor((x - this.r.x) / this.r.w * n), 0, n - 1);
    if (this.setFn) this.setFn(i); else if (this.sym != null) ui.setGesture(this.sym, ui.spec(this.sym).min + i); return true; }
}
// menu rows: '-' separator, '#TITLE' heading (not pickable), '>Action' (pickable; ◀ ▶ and the wheel skip it)
const pickable = s => !!s && s !== '-' && s[0] !== '#';
const steppable = s => pickable(s) && s[0] !== '>';
class Menu extends Ctl {
  constructor(sym, label = ''){ super(); this.sym = sym; this.label = label; this.itemsFn = null; this.getFn = null; this.setFn = null; this.textFn = null; this.arrows = false; }
  get wantsText(){ return false; }
  items(ui){ return this.itemsFn ? this.itemsFn() : this.sym != null ? (ui.spec(this.sym).ch || []).slice() : []; }
  current(ui){ return this.getFn ? this.getFn() : this.sym != null ? Math.round(ui.value(this.sym) - ui.spec(this.sym).min) : -1; }
  choose(ui, i){ if (this.setFn) this.setFn(i); else if (this.sym != null) ui.setGesture(this.sym, ui.spec(this.sym).min + i); }
  static step(it, from, dir, wrap){ const n = it.length; if (!n) return -1; let i = from < 0 ? (dir > 0 ? -1 : n) : from;
    for (let k = 0; k < n; k++){ i += dir; if (i < 0 || i >= n){ if (!wrap) return from; i = i < 0 ? n - 1 : 0; } if (steppable(it[i])) return i; } return -1; }
  draw(ui){ const t = ui.th, r = this.r; ui.keyBox(r, false, false, null, 3);
    const s = this.textFn ? this.textFn() : this.sym != null ? ui.fmt(this.sym) : this.label, pad = this.arrows ? 26 : 10;
    const showL = !!this.label && this.sym != null; if (showL) ui.text(r.x + pad, r.cy, this.label, 'label', 10.5, t.ink2, 'left', 1);
    const lw = showL ? ui.textW(this.label, 'label', 10.5, 1) + 8 : 0;
    ui.clip(R(r.x + pad + lw, r.y, r.w - 2 * pad - lw - 12, r.h), () => ui.text(r.x + pad + lw, r.cy + 0.5, s, 'label', 12.5, this.enabled ? t.ink : t.dim, 'left', 0.6));
    ui.text(r.r - pad, r.cy, '▾', 'mono', 12, t.ink, 'right');
    if (this.arrows){ ui.line(r.x + 20, r.y + 4, r.x + 20, r.b - 4, t.dim, 1); ui.line(r.r - 20, r.y + 4, r.r - 20, r.b - 4, t.dim, 1);
      ui.text(r.x + 10, r.cy, '◀', 'mono', 10, t.ink, 'center'); ui.text(r.r - 10, r.cy, '▶', 'mono', 10, t.ink, 'center'); } }
  down(ui, x, y, btn){ if (btn !== 0 && btn !== 2) return false; const it = this.items(ui); if (!it.length) return true;
    if (this.arrows && x < this.r.x + 20){ const t = Menu.step(it, this.current(ui), -1, true); if (t >= 0) this.choose(ui, t); return true; }
    if (this.arrows && x > this.r.r - 20){ const t = Menu.step(it, this.current(ui), 1, true); if (t >= 0) this.choose(ui, t); return true; }
    ui.openMenu(this.r, it, this.current(ui), i => this.choose(ui, i)); return true; }
  wheel(ui, dy){ const it = this.items(ui); if (!it.length) return true; const c = this.current(ui), t = Menu.step(it, c, dy > 0 ? -1 : 1, false); if (t >= 0 && t !== c) this.choose(ui, t); return true; }
}
// A small value box (LIMIT's custom target, METER's custom fields): drag up/down, wheel, double-click to type.
class Field extends Ctl {
  constructor(sym, label = '', unit = ''){ super(); this.sym = sym; this.label = label; this.unit = unit; this.textFn = null; this.round = false; }
  draw(ui){ const t = ui.th, r = this.r; ui.keyBox(r, false, false, null, 3);
    const s = this.textFn ? this.textFn() : ui.value(this.sym).toFixed(1) + this.unit;
    if (this.label){ ui.text(r.x + 8, r.cy + 0.5, this.label, 'label', 10, this.enabled ? t.ink2 : t.dim, 'left', 0.8); ui.text(r.r - 8, r.cy + 0.5, s, 'mono', 11.5, this.enabled ? t.ink : t.dim, 'right'); }
    else ui.text(r.cx, r.cy + 0.5, s, 'mono', 11.5, t.ink, 'center'); }
  down(ui, x, y, btn){ if (btn !== 0) return false; this.v0 = ui.value(this.sym); this.y0 = y; ui.begin(); return true; }
  drag(ui, x, y, m){ let v = this.v0 + (this.y0 - y) * (m.shift ? 0.01 : 0.1); if (this.round) v = Math.round(v * 10) / 10; ui.setParam(this.sym, v); if (m.shift){ this.v0 = ui.value(this.sym); this.y0 = y; } }
  up(ui){ ui.end(); }
  wheel(ui, dy, m){ ui.setGesture(this.sym, ui.value(this.sym) + (dy > 0 ? 1 : -1) * (m.shift ? 0.1 : 0.5)); return true; }
}

/* ---------------------------------------------------------------- the window */
const HEAD = 46, HEAD_N = 82, FOOT = 24, NARROW = 640, ANIM_MS = 40, MAX_DPR = 2;
const defs = {}, wins = new Map();
let zTop = 70, raf = 0, lastTs = 0, seq = 0;
const UPRE_KEY = 'thunderplus-pump-user-presets-v1', SIZE_KEY = 'thunderplus-pump-win-v1';
const abStore = new Map();   // per slot (this session): the A/B settings
const lsGet = (k, d) => { try { const v = JSON.parse(localStorage.getItem(k)); return v && typeof v === 'object' ? v : d; } catch(e){ return d; } };
const lsSet = (k, v) => { try { localStorage.setItem(k, JSON.stringify(v)); } catch(e){} };

class Win {
  constructor(t, k, info){
    this.t = t; this.k = k; this.type = info.type; this.key = info.key; this.id = 'pumpwin' + (++seq); this.D = defs[info.type];
    this.fx = PM.fx[this.type]; this.P = this.fx.params; this.I = {}; this.P.forEach((p, i) => this.I[p.s] = i);
    this.vals = info.values; this.on = info.on; this.sel = info.sel; this.preset = info.preset; this.lat = info.latency || 0; this.tname = FX.trackName(t);
    this.controls = []; this.status = ''; this.menu = null; this.help = false; this.active = null; this.bodyActive = false;
    this.depth = 0; this.pend = {}; this.liveQ = null; this.dirty = true; this.lastFrame = -1e9; this.lastPaint = 0; this.sr = 48000;
    this.th = themeFor(this.type); this.dark = this.th.dark; this.narrow = innerWidth < NARROW;
    this.ab = abStore.get(this.abKey()) || {sel:0, s:[null, null]}; abStore.set(this.abKey(), this.ab);
    this.buildDom(); this.buildHeader();
    this.h = this.D.make(this) || {};
    this.layoutAll(); this.place(true);
    this.unsub = FX.subscribe(t, k, fr => this.onFrame(fr), {back:this.D.back == null ? 8192 : this.D.back});
    this.offChange = FX.onChange((tt, src) => { if (tt === this.t && src !== this.id) this.reloadSoon(); });
    if (this.h.reload) this.h.reload();
  }
  abKey(){ return this.t + '/' + this.k + '/' + this.type; }
  // ---- values (PumpFX) ------------------------------------------------------------------------------------------
  spec(s){ return this.P[this.I[s]]; }
  value(s){ const v = this.vals[s]; return v === undefined ? (this.spec(s) ? this.spec(s).def : 0) : v; }
  norm(s){ return FX.toNorm(this.type, s, this.value(s)); }
  fmt(s, v){ return FX.format(this.type, s, v === undefined ? this.value(s) : v); }
  // A gesture (begin … end) is one undo step: values go to the effect live while it lasts, and once more at its end.
  begin(){ this.depth++; }
  end(){ if (this.depth <= 0) return; if (--this.depth === 0) this.flushGesture(); }
  setParam(s, v){
    const p = this.spec(s); if (!p) return; v = FX.clamp(this.type, s, v);
    if (v === this.value(s)) return;
    this.vals[s] = v; this.dirty = true;
    if (p.flags & PF.NOUNDO){ FX.set(this.t, this.k, {[s]:v}, {src:this.id, keepPreset:true}); if (this.h.changed) this.h.changed(s, v); return; }   // a display setting (analyzer): not a preset change
    if (this.depth > 0){ this.pend[s] = v; if (!this.liveQ){ this.liveQ = {}; queueMicrotask(() => { const q = this.liveQ; this.liveQ = null; if (q) FX.set(this.t, this.k, q, {live:true, src:this.id}); }); } this.liveQ[s] = v; }
    else FX.set(this.t, this.k, {[s]:v}, {src:this.id});
    if (this.h.changed) this.h.changed(s, v);
  }
  setNorm(s, n){ this.setParam(s, FX.fromNorm(this.type, s, clamp(n, 0, 1))); }
  setGesture(s, v){ this.begin(); this.setParam(s, v); this.end(); }
  setMany(obj){ this.begin(); for (const s in obj) this.setParam(s, obj[s]); this.end(); }
  flushGesture(){ const q = this.pend; this.pend = {}; if (this.liveQ){ Object.assign(q, this.liveQ); this.liveQ = null; } if (Object.keys(q).length) FX.set(this.t, this.k, q, {src:this.id}); }
  setOn(on){ this.on = !!on; FX.setOn(this.t, this.k, this.on); this.dirty = true; }
  hold(kind, a = 0, b = 0, c = 0, d = 0){ FX.hold(this.t, this.k, kind, a, b, c, d); }
  serialize(){ const o = {}; for (const p of this.P) if (!skipFlags(p)) o[p.s] = this.value(p.s); return o; }
  reloadSoon(){ if (this.relQ) return; this.relQ = true; queueMicrotask(() => { this.relQ = false; this.reload(); }); }
  reload(){
    if (this.closed) return; const info = FX.get(this.t, this.k);
    if (!info || info.type !== this.type){ this.close(); return; }
    if (this.depth > 0) Object.assign(info.values, this.pend);   // a drag in progress keeps its values
    this.vals = info.values; this.on = info.on; this.sel = info.sel; this.preset = info.preset; if (info.latency != null) this.lat = info.latency; this.tname = FX.trackName(this.t);
    if (this.h.reload) this.h.reload(); this.dirty = true;
  }
  onFrame(fr){
    if (fr.gone){ this.close(); return; }
    this.sr = fr.sr || this.sr; this.lat = fr.lat != null ? fr.lat : this.lat; this.lastFrame = now();
    if (this.h.frame) this.h.frame(fr); this.fresh = true;   // repainted at the animation rate (tick), not per frame
  }
  // ---- presets, A/B, undo ---------------------------------------------------------------------------------------
  presetRows(){
    const rows = [], items = [], add = (s, kind, i) => { items.push(s); rows.push({kind, i}); };
    let last = null;
    for (const p of FX.presets(this.type)){ if (p.section !== last){ add('#' + p.section, 'h'); last = p.section; } add(p.name, 'f', p.index); }
    const up = this.userPresets(), names = Object.keys(up).sort((a, b) => a.toLowerCase() < b.toLowerCase() ? -1 : 1);
    if (names.length){ add('#YOUR PRESETS', 'h'); names.forEach(n => add(n, 'u', n)); }
    add('-', 'h'); add('>Save preset…', 'save');
    if (this.preset && up[this.preset]) add('>Delete "' + this.preset + '"', 'del');
    this.pRows = rows; return items;
  }
  userPresets(){ const all = lsGet(UPRE_KEY, {}); return all[this.type] && typeof all[this.type] === 'object' ? all[this.type] : {}; }
  pickPreset(i){
    const r = this.pRows && this.pRows[i]; if (!r) return;
    if (r.kind === 'f'){ FX.applyPreset(this.t, this.k, r.i, {src:this.id}); this.reload(); this.status = 'Preset: ' + this.preset; }
    else if (r.kind === 'u'){ const v = this.userPresets()[r.i]; if (!v) return; const o = {};
      for (const p of this.P) if (!skipFlags(p)) o[p.s] = v[p.s] !== undefined ? v[p.s] : p.def;
      FX.set(this.t, this.k, o, {src:this.id, preset:r.i}); this.reload(); this.status = 'Loaded your preset "' + r.i + '"'; }
    else if (r.kind === 'save') this.openNameEntry(this.presetMenu.r, this.preset && this.userPresets()[this.preset] ? this.preset : 'My preset', name => {
      name = name.trim(); if (!name){ this.status = 'Not saved: the name is empty'; return; }
      const all = lsGet(UPRE_KEY, {}); all[this.type] = Object.assign({}, all[this.type] || {}, {[name]:this.serialize()}); lsSet(UPRE_KEY, all);
      FX.set(this.t, this.k, {}, {src:this.id, preset:name}); this.preset = name; this.status = 'Saved "' + name + '" (on this device)'; });
    else if (r.kind === 'del'){ const all = lsGet(UPRE_KEY, {}); if (all[this.type]) delete all[this.type][this.preset]; lsSet(UPRE_KEY, all); this.status = 'Deleted your preset "' + this.preset + '" (the settings stay)'; }
    this.dirty = true;
  }
  selectAB(i){
    if (i === this.ab.sel) return; this.ab.s[this.ab.sel] = this.serialize(); this.ab.sel = i;
    if (this.ab.s[i]){ FX.set(this.t, this.k, this.ab.s[i], {src:this.id}); this.reload(); } else this.ab.s[i] = this.serialize();
    this.status = 'Setting ' + 'AB'[i]; this.dirty = true;
  }
  copyAB(){ const o = 1 - this.ab.sel; this.ab.s[o] = this.serialize(); this.status = 'Copied to ' + 'AB'[o]; this.dirty = true; }
  undo(dir){ FX.undo(dir); this.reload(); this.status = (dir < 0 ? 'Undo' : 'Redo') + ' (Thunder’s undo: the song’s last change)'; }
  // ---- controls -------------------------------------------------------------------------------------------------
  add(c){ this.controls.push(c); return c; }
  dial(sym, label, size = 'm', bipolar = false){ return this.add(new Dial(sym, label, size, bipolar)); }
  row(sym, label, bipolar = false){ return this.add(new Row(sym, label, bipolar)); }
  toggle(sym, label){ const b = this.add(new Btn(label)); b.sym = sym; return b; }
  button(label, fn, on){ return this.add(new Btn(label, fn, on)); }
  seg(sym, opts){ return this.add(new Seg(sym, opts)); }
  menuBtn(sym, label = ''){ return this.add(new Menu(sym, label)); }
  field(sym, label, unit){ return this.add(new Field(sym, label, unit)); }
  buildHeader(){
    const pm = this.presetMenu = this.add(new Menu(null)); pm.arrows = true;
    pm.tip = 'Presets (the side arrows step through them; Save preset… at the end keeps your own on this device)';
    pm.itemsFn = () => this.presetRows();
    pm.getFn = () => { const items = this.presetRows(); for (let i = 0; i < this.pRows.length; i++){ const r = this.pRows[i]; if ((r.kind === 'f' || r.kind === 'u') && items[i] === this.preset) return i; } return -1; };
    pm.setFn = i => this.pickPreset(i); pm.textFn = () => this.preset || 'Default';
    this.undoBtn = this.button('UNDO', () => this.undo(-1)); this.undoBtn.small = true; this.undoBtn.tip = 'Undo (Ctrl/Cmd+Z): Thunder’s undo, so the song’s last change';
    this.redoBtn = this.button('REDO', () => this.undo(1)); this.redoBtn.small = true; this.redoBtn.tip = 'Redo (Shift+Ctrl/Cmd+Z or Ctrl+Y)';
    this.abSeg = this.add(new Seg(null, ['A', 'B'])); this.abSeg.getFn = () => this.ab.sel; this.abSeg.setFn = i => this.selectAB(i); this.abSeg.tip = 'Compare two settings: A and B';
    this.copyBtn = this.button('COPY', () => this.copyAB()); this.copyBtn.small = true; this.copyBtn.tip = 'Copy this setting to the other slot';
    this.helpBtn = this.button('?', () => { this.help = !this.help; }, () => this.help); this.helpBtn.small = true; this.helpBtn.tip = 'Mouse and keyboard shortcuts (?)';
    this.darkBtn = this.button('DARK', () => this.setDark(!this.dark), () => this.dark); this.darkBtn.small = true;
    this.darkBtn.tip = 'pump’s own light or dark colours (remembered on this device; Ctrl/Cmd-click: follow Thunder’s skin again, the default)';
    this.darkBtn.onExclusive = () => { try { localStorage.removeItem(THEME_KEY); } catch(e){} for (const w of wins.values()) w.setDark(themeDark(), true); this.status = 'Follows Thunder’s skin'; };
    if (this.D.bypass !== false){ this.bypBtn = this.button('BYPASS', () => this.setOn(!this.on), () => !this.on); this.bypBtn.small = true;
      this.bypBtn.tip = 'Bypass: the FX slot’s ACTIVE (latency-matched, so the timing never jumps)'; }
    this.closeBtn = this.button('✕', () => this.close()); this.closeBtn.small = true; this.closeBtn.tip = 'Close the window (Esc; Back on a phone)';
  }
  setDark(d, noStore){ if (!noStore){ try { localStorage.setItem(THEME_KEY, d ? 'dark' : 'light'); } catch(e){} for (const w of wins.values()) if (w !== this) w.setDark(d, true); } this.th = themeFor(this.type); this.dark = this.th.dark;
    this.styleDom(); this.dirty = true; }
  get headH(){ return this.narrow ? HEAD_N : HEAD; }
  kindLines(){ const k = String(this.fx.long || '').toUpperCase(); if (!k) return []; const i = k.indexOf(' '); return i < 0 ? [k] : [k.slice(0, i), k.slice(i + 1)]; }
  tagRight(){ return 14 + this.textW('…pump', 'bold', 21) + 10 + this.textW(this.fx.name, 'bold', 15) + 16; }
  brandRight(){ const kw = Math.max(0, ...this.kindLines().map(l => this.textW(l, 'label', 10, 1.2))); return this.tagRight() + (kw > 0 ? 9 + kw : 0); }
  layoutHeader(){
    const W = this.W, h = 28;
    let rx = W - 12; const right = (c, w, y) => { if (!c) return; c.r = R(rx - w, y, w, h); rx -= w + 8; };
    if (!this.narrow){
      const y = 9; let x = this.brandRight() + 18; const pw = Math.min(280, W * 0.25);
      this.presetMenu.r = R(x, y, pw, h); x += pw + 14;
      this.undoBtn.r = R(x, y, 50, h); x += 56; this.redoBtn.r = R(x, y, 50, h); x += 64;
      this.abSeg.r = R(x, y, 56, h); x += 64; this.copyBtn.r = R(x, y, 52, h); x += 66;
      right(this.closeBtn, 30, y); right(this.bypBtn, 74, y); right(this.darkBtn, 56, y); right(this.helpBtn, 30, y); rx -= 4;
      this.headerExtra = R(x, y, Math.max(0, rx - x), h);
    } else {
      const y = 8, y2 = 46;
      right(this.closeBtn, 30, y); right(this.bypBtn, 70, y); right(this.darkBtn, 52, y); right(this.helpBtn, 30, y); rx -= 4;
      this.headerExtra = R(Math.min(rx, this.brandRight() + 10), y, Math.max(0, rx - this.brandRight() - 10), h);
      let x2 = W - 12; const left = (c, w) => { c.r = R(x2 - w, y2, w, h); x2 -= w + 6; };
      left(this.copyBtn, 50); left(this.abSeg, 52); x2 -= 4; left(this.redoBtn, 48); left(this.undoBtn, 48); x2 -= 4;
      this.presetMenu.r = R(12, y2, Math.max(100, x2 - 12), h);
    }
  }
  layoutAll(){
    const D = this.D;
    this.W = this.narrow ? (D.nW || 480) : D.W; this.H = this.narrow ? (D.nH || D.H) : D.H;
    this.layoutHeader(); if (this.h.layout) this.h.layout(this.narrow); this.dirty = true;
  }
  // ---- drawing helpers (logical coordinates) --------------------------------------------------------------------
  font(f, size, sp = 0){ const g = this.g, fo = fontOf(f, size, this.narrow); if (fo !== this._f){ g.font = fo; this._f = fo; }   // (state changes cost: only when they differ)
    if (sp !== this._ls){ if ('letterSpacing' in g) g.letterSpacing = sp + 'px'; this._ls = sp; } }
  text(x, y, s, f, size, c, align = 'left', sp = 0){ if (s == null || s === '') return; const g = this.g; this.font(f, size, sp); g.fillStyle = css(c); g.textAlign = align; g.textBaseline = 'middle'; g.fillText(String(s), x, y); }
  textW(s, f, size, sp = 0){ if (!s) return 0; this.font(f, size, sp); return this.g.measureText(String(s)).width; }
  fill(r, c){ this.g.fillStyle = css(c); this.g.fillRect(r.x, r.y, r.w, r.h); }
  stroke(r, c, w = 1.5){ const g = this.g; g.strokeStyle = css(c); g.lineWidth = w; g.strokeRect(r.x + w / 2, r.y + w / 2, r.w - w, r.h - w); }
  line(x0, y0, x1, y1, c, w = 1){ const g = this.g; g.beginPath(); g.moveTo(x0, y0); g.lineTo(x1, y1); g.strokeStyle = css(c); g.lineWidth = w; g.stroke(); }
  clip(r, fn){ const g = this.g; g.save(); g.beginPath(); g.rect(r.x, r.y, r.w, r.h); g.clip(); try { fn(); } finally { g.restore(); this._f = this._ls = null; } }
  // a Simple-skin key / screen: fill, 1.5 px ink outline and an offset block behind it
  keyBox(r, filled, pressed, fillOverride, offset = 4){ const t = this.th, o = pressed ? 1 : offset;
    if (o > 0){ this.fill(R(r.x + o, r.y + o, r.w, r.h), t.bg); this.stroke(R(r.x + o, r.y + o, r.w, r.h), t.dim, 1.5); }
    this.fill(r, fillOverride || (filled ? t.ink : t.bg)); this.stroke(r, t.ink, 1.5); }
  screenBox(r){ const t = this.th; this.fill(R(r.x + 5, r.y + 5, r.w, r.h), t.bg); this.stroke(R(r.x + 5, r.y + 5, r.w, r.h), t.dim, 1.5); this.fill(r, t.screen); this.stroke(r, t.ink, 1.5); }
  caption(x, y, s, c){ this.text(x, y, s, 'label', 11, c || this.th.ink2, 'left', 1.2); }
  freqToX(r, f, lo = 20, hi = 20000){ return r.x + Math.log(f / lo) / Math.log(hi / lo) * r.w; }
  xToFreq(r, x, lo = 20, hi = 20000){ return lo * Math.pow(hi / lo, (x - r.x) / r.w); }
  drawFreqGrid(r, lo = 20, hi = 20000, labels = true){
    const t = this.th;
    for (const f of [20, 30, 40, 50, 60, 70, 80, 90, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 2000, 3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 20000]){
      if (f < lo || f > hi) continue; const x = this.freqToX(r, f, lo, hi), major = f === 100 || f === 1000 || f === 10000;
      this.line(x, r.y, x, r.b, major ? alpha(t.ink, t.dark ? 0.22 : 0.20) : t.grid, 1); }
    if (!labels) return;
    for (const [f, s] of [[50, '50'], [100, '100'], [200, '200'], [500, '500'], [1000, '1k'], [2000, '2k'], [5000, '5k'], [10000, '10k'], [20000, '20k']])
      if (f >= lo && f <= hi) this.text(this.freqToX(r, f, lo, hi), r.b - 8, s, 'mono', 9.5, t.ink2, 'center');
  }
  drawSpectrum(sv, r, top, bottom, fillC, lineC, lo = 20, hi = 20000, lw = 1.2){
    const g = this.g, yOf = d => Math.min(r.b, Math.max(r.y, r.y + (top - d) / (top - bottom) * r.h)), pts = [];
    for (let p = 0; p < sv.P; p++){ const f = sv.pointFreq(p); if (f < lo * 0.9 || f > hi * 1.1) continue; pts.push(this.freqToX(r, f, lo, hi), yOf(sv.pointDb(p))); }
    g.beginPath(); g.moveTo(r.x, r.b); for (let i = 0; i < pts.length; i += 2) g.lineTo(pts[i], pts[i + 1]); g.lineTo(r.r, r.b); g.closePath(); g.fillStyle = css(fillC); g.fill();
    g.beginPath(); for (let i = 0; i < pts.length; i += 2) i ? g.lineTo(pts[i], pts[i + 1]) : g.moveTo(pts[i], pts[i + 1]); g.strokeStyle = css(lineC); g.lineWidth = lw; g.lineJoin = 'round'; g.stroke();
  }
  drawLevelMeter(r, lv, pk, dbMin = -60, dbMax = 6){
    const t = this.th, yOf = d => r.b - (clamp(d, dbMin, dbMax) - dbMin) / (dbMax - dbMin) * r.h; this.fill(r, t.panel2);
    const y = yOf(lv), y0 = yOf(0);
    if (lv > dbMin){ this.fill(R(r.x, Math.max(y, y0), r.w, r.b - Math.max(y, y0)), t.neon); if (y < y0) this.fill(R(r.x, y, r.w, y0 - y), t.red); }
    if (pk > dbMin) this.line(r.x, yOf(pk), r.r, yOf(pk), pk > 0 ? t.red : t.ink, 1.5);
    this.stroke(r, t.ink, 1);
  }
  drawGRMeter(r, gr, range = 24){ const t = this.th; this.fill(r, t.panel2); const h = clamp(-gr / range, 0, 1) * r.h; if (h > 0.5) this.fill(R(r.x, r.y, r.w, h), t.second); this.stroke(r, t.ink, 1); }
  // ---- menus and typed entry ------------------------------------------------------------------------------------
  openMenu(anchor, items, current, onPick){
    const itemH = 22; let w = Math.max(anchor.w, 140); for (const s of items) w = Math.max(w, this.textW(s.replace(/^[#>]/, ''), 'label', 12.5, 0.6) + 28);
    w = Math.min(w, this.W - 12); const h = itemH * items.length + 8; let y = anchor.b + 2; if (y + h > this.H - 4) y = Math.max(4, anchor.y - h - 2);
    const x = clamp(anchor.x, 6, this.W - w - 6), r = R(x, y, w, Math.min(h, this.H - 8));
    let scroll = 0; if (h > this.H - 8 && current >= 0) scroll = clamp(current * itemH - (this.H - 8) / 2, 0, h - (this.H - 8));
    if (r.b > this.H - 4){ r.y = Math.max(4, this.H - 4 - r.h); }
    this.menu = {r, items, current, onPick, itemH, scroll, hover:-1}; this.dirty = true;
  }
  menuIndexAt(y){ const m = this.menu; return Math.floor((y - m.r.y - 4 + m.scroll) / m.itemH); }
  drawMenu(){
    const m = this.menu, r = m.r, t = this.th;
    this.fill(R(r.x + 5, r.y + 5, r.w, r.h), alpha(t.ink, 0.25)); this.fill(r, t.bg); this.stroke(r, t.ink, 1.5);
    this.clip(r, () => m.items.forEach((s, i) => {
      const iy = r.y + 4 + i * m.itemH - m.scroll; if (iy + m.itemH < r.y || iy > r.b) return;
      if (s === '-'){ this.line(r.x + 8, iy + m.itemH / 2, r.r - 8, iy + m.itemH / 2, t.dim, 1); return; }
      const hov = i === m.hover, cur = i === m.current;
      if (hov) this.fill(R(r.x + 3, iy, r.w - 6, m.itemH), t.ink); else if (cur) this.fill(R(r.x + 3, iy, r.w - 6, m.itemH), t.panel2);
      if (s[0] === '#') this.text(r.x + 12, iy + m.itemH / 2, s.slice(1), 'label', 10.5, hov ? t.on : t.ink2, 'left', 1.2);
      else if (s[0] === '>') this.text(r.x + 14, iy + m.itemH / 2, s.slice(1), 'label', 12.5, hov ? t.on : t.ink2, 'left', 0.6);
      else this.text(r.x + 14, iy + m.itemH / 2, s, 'label', 12.5, hov ? t.on : t.ink, 'left', 0.6); }));
  }
  // Typed value entry over a control (a real text field, so a phone shows its keyboard). ring: [[sym, rect], …] for Tab.
  openTextEntry(sym, r, ring, text){
    const p = this.spec(sym); if (!p) return;
    let s = text != null ? text : (this.h.entryText ? this.h.entryText(sym) : null);
    if (s == null){ s = this.fmt(sym); const sp = s.indexOf(' '); if (sp >= 0 && !p.ch) s = s.slice(0, sp); if (p.unit === 'Hz' && this.value(sym) >= 1000) s = (this.value(sym) / 1000).toFixed(2) + 'k'; }
    this.edit = {sym, r, ring:ring || [], onText:null, orig:s}; this.showEdit(s);
  }
  openNameEntry(r, initial, onText){ this.edit = {sym:null, r, ring:[], onText, orig:'\x01'}; this.showEdit(initial); this.status = 'Name the preset, then Enter to save (Esc cancels)'; }
  showEdit(s){
    const e = this.edit, inp = this.inp, sc = this.scale; inp.value = s; inp.hidden = false;
    const w = Math.max(e.r.w, 90);
    Object.assign(inp.style, {left:(this.ox + (e.r.x + (e.r.w - w) / 2) * sc) + 'px', top:(this.oy + e.r.y * sc) + 'px', width:(w * sc) + 'px', height:(Math.max(20, e.r.h) * sc) + 'px', fontSize:(12 * EM.mono * (this.narrow ? NARROW_TEXT : 1) * sc) + 'px'});
    this.styleDom(); inp.focus(); inp.select(); this.dirty = true;
  }
  commitEdit(cancel){
    const e = this.edit; if (!e) return; this.edit = null; this.inp.hidden = true; try { this.el.focus({preventScroll:true}); } catch(_){}
    if (cancel){ if (!e.sym) this.status = 'Not saved'; this.dirty = true; return; }
    const txt = this.inp.value;
    if (!e.sym){ if (e.onText) e.onText(txt); this.dirty = true; return; }
    if (txt !== e.orig){ const raw = this.h.preParse ? this.h.preParse(e.sym, txt) : txt, v = parseParam(this.type, this.spec(e.sym), raw);
      if (v != null) this.setGesture(e.sym, v); else this.status = 'Couldn’t read “' + txt + '”'; }
    this.dirty = true;
  }
  tabEdit(back){
    const e = this.edit; if (!e || !e.sym) return; const ring = e.ring, cur = e.sym; this.commitEdit(false);
    if (ring.length < 2) return; let k = ring.findIndex(q => q[0] === cur); k = (k + (back ? ring.length - 1 : 1)) % ring.length;
    this.openTextEntry(ring[k][0], ring[k][1], ring);
  }
  ringFor(c){ return c.tabGroup > 0 ? this.controls.filter(o => o.tabGroup === c.tabGroup && o.visible && o.enabled && o.sym != null).map(o => [o.sym, o.valueRect()]) : []; }
  // ---- the help overlay -----------------------------------------------------------------------------------------
  drawHelp(){
    const t = this.th, lines = [['Drag', 'change a control (up/right = more)'], ['Shift + drag / wheel', 'fine adjustment'],
      ['Double-click / right-click', 'type a value: 1k, A4, C#3+13, 2x (= +6 dB), 50% (halfway)'], ['Tab (while typing)', 'next field'],
      ['Ctrl/Cmd + click', 'reset to default (Alt + click works too)'], ['Mouse wheel', 'step a control'],
      ['Ctrl/Cmd + Z', 'undo (Thunder’s: the song’s last change)   Shift + Ctrl/Cmd + Z, Ctrl + Y: redo'],
      ['A / B, COPY', 'compare two settings, copy this one to the other slot'],
      ['Touch', 'double-tap: type a value · long-press: the right-click menu'], ['Esc · ✕ · Back', 'close this window (Space still plays / stops)']];
    const extra = this.h.help ? this.h.help() : []; if (extra.length){ lines.push(['', '']); lines.push(...extra); }
    const lh = this.narrow ? 30 : 21, w = Math.min(this.W - (this.narrow ? 24 : 80), 820), h = 74 + lh * lines.length;
    const r = R((this.W - w) / 2, Math.max(this.headH + 10, (this.H - h) / 2), w, Math.min(h, this.H - this.headH - 30));
    this.fill(R(0, this.headH, this.W, this.H - this.headH), alpha(t.bg, 0.55)); this.fill(R(r.x + 6, r.y + 6, r.w, r.h), t.dim); this.fill(r, t.bg); this.stroke(r, t.ink, 1.5);
    this.text(r.x + 20, r.y + 24, 'MOUSE AND KEYS', 'label', 13, t.ink, 'left', 2); this.text(r.r - 20, r.y + 24, 'click or Esc to close', 'label', 11, t.ink2, 'right', 0.8);
    this.line(r.x + 14, r.y + 42, r.r - 14, r.y + 42, t.dim, 1);
    let yy = r.y + 60; const kx = r.x + 20, dx = r.x + 20 + Math.min(300, r.w * 0.38);
    this.clip(r, () => { for (const [k, d] of lines){ if (yy > r.b - 10) break;
      if (k){ if (this.narrow){ this.text(kx, yy - 7, k, 'mono', 11, t.ink, 'left'); this.text(kx + 10, yy + 7, d, 'label', 11, t.ink2, 'left', 0.2); }
        else { this.text(kx, yy, k, 'mono', 11.5, t.ink, 'left'); this.text(dx, yy, d, 'label', 12, t.ink2, 'left', 0.4); } }
      yy += lh; } });
  }
  // ---- header and footer ----------------------------------------------------------------------------------------
  drawHeader(){
    const t = this.th, H = this.headH; this.fill(R(0, 0, this.W, H), t.panel2); this.line(0, H - 0.75, this.W, H - 0.75, t.ink, 1.5);
    const bx = 14, by = this.narrow ? 23 : H / 2;
    this.text(bx + 1.5, by + 2, '…pump', 'bold', 21, t.dim); this.text(bx, by, '…pump', 'bold', 21, t.ink);
    const tx = bx + this.textW('…pump', 'bold', 21) + 10, tw = this.textW(this.fx.name, 'bold', 15) + 16, tag = R(tx, by - 13, tw, 26);
    this.keyBox(tag, false, false, t.neon, 3); this.text(tag.cx, tag.cy + 0.5, this.fx.name, 'bold', 15, t.neonInk, 'center');
    const kl = this.kindLines(), kx = tag.r + 9;
    if (kl.length === 1) this.text(kx, tag.cy + 0.5, kl[0], 'label', 10, t.ink2, 'left', 1.2);
    else if (kl.length === 2){ this.text(kx, tag.cy - 5.5, kl[0], 'label', 10, t.ink2, 'left', 1.2); this.text(kx, tag.cy + 6, kl[1], 'label', 10, t.ink2, 'left', 1.2); }
  }
  drawFooter(){
    const t = this.th, f = R(0, this.H - FOOT, this.W, FOOT); this.fill(f, t.panel2); this.line(0, f.y + 0.75, this.W, f.y + 0.75, t.ink, 1.5);
    const info = this.h.footer ? this.h.footer() : '', iw = info ? this.textW(info, 'label', 11.5, 0.6) + 18 : 0;
    const st = this.status || (this.tname + ' · SLOT ' + (this.k + 1) + (!this.on ? ' · BYPASSED' : ''));
    this.clip(R(0, f.y, Math.max(0, this.W - 30 - iw), f.h), () => this.text(12, f.cy, st, 'label', 12, this.status ? t.ink : t.ink2, 'left', 0.6));
    this.text(this.W - 30, f.cy, info, 'label', 11.5, t.ink2, 'right', 0.6);
    if (!this.narrow){ const g = this.grip(); for (let i = 1; i <= 3; i++) this.line(g.r - 3 - i * 4, g.b - 3, g.r - 3, g.b - 3 - i * 4, t.ink2, 1.2); }
  }
  grip(){ return R(this.W - 18, this.H - 18, 18, 18); }
  latencyText(){ const ms = this.lat / (this.sr || 48000) * 1000; return 'LATENCY ' + (this.lat ? ms.toFixed(ms < 10 ? 1 : 0) + ' MS' : '0'); }
  paint(){
    const g = this.g; if (!g) return; const dpr = Math.min(MAX_DPR, devicePixelRatio || 1), sc = this.scale;
    const cw = Math.round(this.W * sc * dpr), ch = Math.round(this.H * sc * dpr);
    if (this.cv.width !== cw || this.cv.height !== ch){ this.cv.width = cw; this.cv.height = ch; }
    g.setTransform(sc * dpr, 0, 0, sc * dpr, 0, 0); g.lineCap = 'butt'; g.lineJoin = 'miter'; this._f = this._ls = null;
    this.fill(R(0, 0, this.W, this.H), this.th.bg);
    this.drawHeader();
    try { if (this.h.drawBody) this.h.drawBody(); } catch(e){ warnOnce('pump window draw (' + this.type + ')', e); }
    for (const c of this.controls) if (c.visible) c.draw(this);
    if (this.h.drawOverlay) this.h.drawOverlay();
    this.drawFooter();
    if (this.help) this.drawHelp();
    if (this.menu) this.drawMenu();
    this.dirty = false; this.lastPaint = performance.now();
  }
  // ---- DOM, placement ---------------------------------------------------------------------------------------------
  buildDom(){
    const el = this.el = document.createElement('div'); el.className = 'pumpwin'; el.tabIndex = 0; el.dataset.ownkeys = '1';
    el.setAttribute('role', 'dialog'); el.setAttribute('aria-label', 'pump ' + this.fx.name + ' · ' + FX.trackName(this.t) + ' slot ' + (this.k + 1));
    const cv = this.cv = document.createElement('canvas'); cv.setAttribute('aria-label', 'pump ' + this.fx.name + ' ' + this.fx.long);
    const inp = this.inp = document.createElement('input'); inp.type = 'text'; inp.className = 'pumpedit'; inp.hidden = true; inp.spellcheck = false; inp.autocomplete = 'off';
    inp.addEventListener('keydown', e => { e.stopPropagation();
      if (e.key === 'Enter'){ e.preventDefault(); this.commitEdit(false); } else if (e.key === 'Escape'){ e.preventDefault(); this.commitEdit(true); }
      else if (e.key === 'Tab'){ e.preventDefault(); this.tabEdit(e.shiftKey); } });
    inp.addEventListener('blur', () => { if (this.edit && !this.closed) setTimeout(() => { if (this.edit && document.activeElement !== inp) this.commitEdit(false); }, 0); });
    el.append(cv, inp); document.body.append(el);
    this.g = cv.getContext('2d');
    cv.addEventListener('pointerdown', e => this.pDown(e)); cv.addEventListener('pointermove', e => this.pMove(e));
    cv.addEventListener('pointerup', e => this.pUp(e)); cv.addEventListener('pointercancel', e => this.pUp(e, true));
    cv.addEventListener('pointerleave', () => { if (!this.active && !this.bodyActive && !this.menu){ this.status = ''; this.dirty = true; if (this.h.leave) this.h.leave(); } });
    cv.addEventListener('wheel', e => this.pWheel(e), {passive:false});
    cv.addEventListener('contextmenu', e => e.preventDefault());
    el.addEventListener('keydown', e => this.keyDown(e)); el.addEventListener('keyup', e => this.keyUp(e));
    el.addEventListener('pointerdown', () => this.raise(), true);
    el.addEventListener('blur', () => { if (this.h.blur) this.h.blur(); });
    this.styleDom();
  }
  styleDom(){ const t = this.th; this.el.style.setProperty('--pw-bg', css(t.bg)); this.el.style.setProperty('--pw-ink', css(t.ink)); this.el.style.setProperty('--pw-dim', css(t.dim)); this.el.style.setProperty('--pw-neon', css(t.neon)); }
  raise(){ this.el.style.zIndex = String(++zTop); }
  topBar(){ const tb = document.querySelector('.top'); return tb ? Math.max(0, Math.round(tb.getBoundingClientRect().bottom)) : 0; }
  // Desktop: a window of the plugin's size (scaled to fit, resizable from the corner, moved by its header). Narrow
  // screens (a folded phone): the narrow layout, filling the screen below the top bar.
  place(first){
    const vw = innerWidth, vh = innerHeight, top = this.topBar(), narrow = vw < NARROW;
    if (narrow !== this.narrow){ this.narrow = narrow; this.layoutAll(); }
    this.el.classList.toggle('narrow', narrow);
    if (narrow){
      const aw = vw, ah = vh - top; this.scale = Math.min(aw / this.W, ah / this.H);
      this.ox = Math.round((aw - this.W * this.scale) / 2); this.oy = 0;
      Object.assign(this.el.style, {left:'0px', top:top + 'px', width:aw + 'px', height:ah + 'px'});
    } else {
      const maxS = Math.min((vw - 24) / this.W, (vh - top - 24) / this.H);
      if (first){ const saved = lsGet(SIZE_KEY, {})[this.type]; this.scale = Math.min(saved > 0.4 ? saved : 1, maxS);
        const n = [...wins.values()].filter(w => !w.narrow).length; this.px = 24 + 28 * (n % 8); this.py = top + 10 + 28 * (n % 8); }
      else this.scale = Math.min(this.scale, maxS);
      this.scale = Math.max(0.4, this.scale);
      const w = Math.round(this.W * this.scale), h = Math.round(this.H * this.scale);
      this.px = clamp(this.px, 0, Math.max(0, vw - w)); this.py = clamp(this.py, top, Math.max(top, vh - h));
      this.ox = 0; this.oy = 0;
      Object.assign(this.el.style, {left:this.px + 'px', top:this.py + 'px', width:w + 'px', height:h + 'px'});
    }
    Object.assign(this.cv.style, {left:this.ox + 'px', top:this.oy + 'px', width:(this.W * this.scale) + 'px', height:(this.H * this.scale) + 'px'});
    if (this.edit) this.commitEdit(true);
    this.dirty = true;
  }
  // ---- pointer --------------------------------------------------------------------------------------------------
  pos(e){ const r = this.cv.getBoundingClientRect(); return [(e.clientX - r.left) / this.scale, (e.clientY - r.top) / this.scale]; }
  finishPointer(x, y){ if (this.active){ const c = this.active; this.active = null; c.up(this, x, y); } if (this.bodyActive){ this.bodyActive = false; if (this.h.up) this.h.up(x, y); } this.drag = null; }
  pDown(e){
    const [x, y] = this.pos(e), touch = e.pointerType === 'touch' || e.pointerType === 'pen';
    let btn = e.button, m = modsOf(e);
    if (isMac && btn === 0 && e.ctrlKey){ btn = 2; m = Object.assign({}, m, {ctrl:false}); }   // Ctrl-click on a Mac is a right-click
    try { this.cv.setPointerCapture(e.pointerId); } catch(_){}
    if (!touch) try { this.el.focus({preventScroll:true}); } catch(_){}
    e.preventDefault();
    this.finishPointer(x, y);
    const tNow = e.timeStamp, tol = touch ? 14 : 4;
    const dbl = tNow - (this.lastClick || -1e9) < 350 && Math.abs(x - this.lcx) < tol && Math.abs(y - this.lcy) < tol && btn === this.lcb;
    this.lastClick = dbl ? -1e9 : tNow; this.lcx = x; this.lcy = y; this.lcb = btn; this.pid = e.pointerId;
    this.dirty = true;
    if (this.menu){ const m2 = this.menu; if (m2.r.has(x, y)){ const i = this.menuIndexAt(y); if (i >= 0 && i < m2.items.length && pickable(m2.items[i])){ this.menu = null; m2.onPick(i); } else if (touch) return; }
      this.menu = null; return; }
    if (this.help){ this.help = false; return; }
    if (this.edit) this.commitEdit(false);
    if (!this.narrow && this.grip().has(x, y)){ this.drag = {kind:'size', x:e.clientX, s:this.scale}; return; }
    if (touch && btn === 0){ clearTimeout(this.lpT); this.lp = {x, y}; this.lpT = setTimeout(() => this.longPress(), 520); }
    for (let i = this.controls.length - 1; i >= 0; i--){
      const c = this.controls[i]; if (!c.visible || !c.enabled || !c.r.has(x, y)) continue;
      if (c.sym != null && c.wantsText){
        if (btn === 2 || (dbl && btn === 0)){ this.openTextEntry(c.sym, c.valueRect(), this.ringFor(c)); clearTimeout(this.lpT); return; }
        if ((m.cmd || m.alt) && btn === 0){ const p = this.spec(c.sym); this.setGesture(c.sym, this.h.defaultOf ? this.h.defaultOf(c.sym) : p.def); return; }
      }
      if (c.down(this, x, y, btn, m)) this.active = c;
      return;
    }
    if (y > this.headH && y < this.H - FOOT){ if (this.h.down && this.h.down(x, y, btn, m, dbl)) this.bodyActive = true; return; }
    if (!this.narrow && y <= this.headH){ this.drag = {kind:'move', x:e.clientX, y:e.clientY, px:this.px, py:this.py}; this.el.classList.add('moving'); }
  }
  longPress(){   // touch: a held finger that didn't move is a right-click (typed entry on a control, the menus in the displays)
    const lp = this.lp; this.lp = null; if (!lp) return;
    if (this.active && this.active.wantsText && this.active.sym != null){ const c = this.active; this.finishPointer(lp.x, lp.y); this.openTextEntry(c.sym, c.valueRect(), this.ringFor(c)); }
    else if (this.bodyActive || (!this.active && lp.y > this.headH && lp.y < this.H - FOOT)){ this.finishPointer(lp.x, lp.y); if (this.h.down) this.h.down(lp.x, lp.y, 2, {shift:false, alt:false, cmd:false, ctrl:false}, false); }
    this.dirty = true;
  }
  pMove(e){
    const [x, y] = this.pos(e), m = modsOf(e);
    if (this.lp && Math.abs(x - this.lp.x) + Math.abs(y - this.lp.y) > 8 / Math.max(0.3, this.scale)){ this.lp = null; clearTimeout(this.lpT); }
    if (this.drag){
      if (this.drag.kind === 'move'){ this.px = this.drag.px + e.clientX - this.drag.x; this.py = this.drag.py + e.clientY - this.drag.y; this.place(false); }
      else { const top = this.topBar(); this.scale = clamp(this.drag.s * (1 + (e.clientX - this.drag.x) / (this.W * this.drag.s)), 0.4, Math.min((innerWidth - this.px - 4) / this.W, (innerHeight - this.py - 4) / this.H, 2.5)); this.place(false); }
      return;
    }
    if (this.menu){ const i = this.menu.r.has(x, y) ? this.menuIndexAt(y) : -1; const hv = i >= 0 && i < this.menu.items.length && pickable(this.menu.items[i]) ? i : -1; if (hv !== this.menu.hover){ this.menu.hover = hv; this.dirty = true; } return; }
    if (this.active){ this.active.drag(this, x, y, m); if (this.active && this.active.sym != null) this.status = this.paramStatus(this.active.sym); this.dirty = true; return; }
    if (this.bodyActive){ if (this.h.drag) this.h.drag(x, y, m); this.dirty = true; return; }
    if (e.pointerType === 'touch') return;
    let st = '';
    for (let i = this.controls.length - 1; i >= 0; i--){ const c = this.controls[i]; if (c.visible && c.r.has(x, y)){ st = c.tip || (c.sym != null ? this.paramStatus(c.sym) : c.label); break; } }
    if (!st && y < this.headH && x < this.brandRight()) st = this.fx.about;
    this.status = st;
    if (y > this.headH && y < this.H - FOOT && this.h.hover) this.h.hover(x, y, m);
    this.dirty = true;
  }
  paramStatus(s){ const p = this.spec(s); return p ? p.n + '  ' + (this.h.fmtStatus ? this.h.fmtStatus(s) : this.fmt(s)) : ''; }
  pUp(e, cancel){
    const [x, y] = this.pos(e); this.lp = null; clearTimeout(this.lpT);
    try { this.cv.releasePointerCapture(e.pointerId); } catch(_){}
    if (this.drag){ if (this.drag.kind === 'size'){ const s = lsGet(SIZE_KEY, {}); s[this.type] = +this.scale.toFixed(3); lsSet(SIZE_KEY, s); } this.drag = null; this.el.classList.remove('moving'); return; }
    if (this.active){ const c = this.active; this.active = null; c.up(this, x, y); this.dirty = true; return; }
    if (this.bodyActive){ this.bodyActive = false; if (this.h.up) this.h.up(x, y, cancel); this.dirty = true; }
  }
  // WHEEL (27 Oct, Thunder Plus audit M2): the movement is added up and a step taken per 100 px (a mouse notch), as the
  // host's sliders do; one step per event made a trackpad flick (30-100 events) run a control across its whole range.
  wheelN(e){
    const raw = e.deltaY || e.deltaX, px = e.deltaMode === 1 ? raw * 40 : e.deltaMode === 2 ? raw * 400 : raw; if (!px) return 0;
    const now = performance.now(); if (now - (this.wT || 0) > 250 || Math.sign(this.wAcc || 0) !== Math.sign(px)) this.wAcc = 0;
    this.wAcc += px; this.wT = now; const n = Math.trunc(this.wAcc / 100); this.wAcc -= n * 100; return clamp(n, -8, 8);
  }
  pWheel(e){
    e.preventDefault(); const [x, y] = this.pos(e), m = modsOf(e); if (!(e.deltaY || e.deltaX)) return;
    this.dirty = true;
    if (this.menu){ const px = e.deltaMode === 1 ? e.deltaY * 40 : e.deltaY, tot = this.menu.itemH * this.menu.items.length + 8; this.menu.scroll = clamp(this.menu.scroll + px * 0.5, 0, Math.max(0, tot - this.menu.r.h)); return; }
    const n = this.wheelN(e); if (!n) return; const dy = n < 0 ? 1 : -1;
    for (let i = this.controls.length - 1; i >= 0; i--){ const c = this.controls[i]; if (c.visible && c.enabled && c.r.has(x, y)){ let ok = false; for (let k = 0; k < Math.abs(n); k++) ok = c.wheel(this, dy, m) || ok; if (ok && c.sym != null) this.status = this.paramStatus(c.sym); return; } }
    if (y > this.headH && y < this.H - FOOT && this.h.wheel) for (let k = 0; k < Math.abs(n); k++) this.h.wheel(x, y, dy, m);
  }
  // ---- keys -----------------------------------------------------------------------------------------------------
  keyDown(e){
    if (e.target === this.inp) return;
    const m = modsOf(e), k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    if (!this.owns(e)) return;   // Space (play / stop) and the Ctrl/Cmd shortcuts this window doesn't use (save, open…) are Thunder's
    e.preventDefault(); e.stopPropagation(); this.dirty = true;
    if (this.menu || this.help){ if (e.key === 'Escape' || (this.help && (e.key === '?' || e.key === '/' || e.key === 'F1'))){ this.menu = null; this.help = false; } return; }
    if (m.cmd && k === 'z'){ this.undo(e.shiftKey ? 1 : -1); return; }
    if (m.cmd && k === 'y'){ this.undo(1); return; }
    if (e.key === '?' || e.key === '/' || e.key === 'F1'){ this.help = !this.help; return; }
    if (this.h.key && this.h.key(e, m)) return;
    if (e.key === 'Escape'){ this.close(); return; }
  }
  owns(e){ if (e.key === ' ' && !e.ctrlKey && !e.metaKey && !e.altKey) return false;
    const m = modsOf(e), k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
    return !m.cmd || k === 'z' || k === 'y' || (this.D.cmdKeys || '').includes(k); }
  keyUp(e){ if (e.target === this.inp) return; if (this.h.keyUp) this.h.keyUp(e, modsOf(e)); }
  // ---- per frame and closing ------------------------------------------------------------------------------------
  tick(dt, ts){
    if (this.h.tick) this.h.tick(dt);
    // edits and pointer moves repaint at once; meters and displays at most 25 times a second (CPU on a phone)
    const anim = this.fresh || (this.h.animating ? this.h.animating() : now() - this.lastFrame < 2);
    if (this.dirty || (anim && ts - this.lastPaint >= ANIM_MS)){ this.fresh = false; this.paint(); }
  }
  close(){
    if (this.closed) return; this.closed = true; this.finishPointer(0, 0);
    try { if (this.h.close) this.h.close(); } catch(e){}
    try { this.unsub(); } catch(e){} try { this.offChange(); } catch(e){}
    this.el.remove(); wins.delete(this.t + '/' + this.k);
    const rest = [...wins.values()]; if (rest.length) try { rest[rest.length - 1].el.focus({preventScroll:true}); } catch(e){}
  }
}

function loop(ts){
  raf = 0; const dt = lastTs ? clamp((ts - lastTs) / 1000, 0.001, 0.25) : 1 / 30; lastTs = ts;
  for (const w of wins.values()) try { w.tick(dt, ts); } catch(e){ warnOnce('pump window tick (' + w.type + ')', e); }
  if (wins.size) raf = requestAnimationFrame(loop); else lastTs = 0;
}
function open(t, k, info){
  const id = t + '/' + k, have = wins.get(id);
  if (have && have.type === info.type){ have.raise(); try { have.el.focus({preventScroll:true}); } catch(e){} return have; }
  if (have) have.close();
  if (!defs[info.type]){ FX.toast('pump ' + PM.fx[info.type].name + ': its window comes in a later update. The FX boxes change its main settings.'); return null; }
  if (innerWidth < NARROW) for (const w of [...wins.values()]) w.close();   // a phone shows one window at a time
  const w = new Win(t, k, info); wins.set(id, w); w.raise();
  try { w.el.focus({preventScroll:true}); } catch(e){}
  if (!raf) raf = requestAnimationFrame(loop);
  return w;
}
addEventListener('resize', () => { for (const w of wins.values()) w.place(false); });

const style = document.createElement('style');
style.textContent = `
.pumpwin{position:fixed; z-index:70; background:var(--pw-bg); border:1.5px solid var(--pw-ink); box-shadow:6px 6px 0 0 var(--pw-dim); outline:none; overflow:hidden; box-sizing:content-box; touch-action:none; user-select:none; -webkit-user-select:none}
.pumpwin.narrow{border:0; box-shadow:none; box-sizing:border-box}
.pumpwin:focus-visible{box-shadow:6px 6px 0 0 var(--pw-neon)}
.pumpwin canvas{position:absolute; display:block; touch-action:none; cursor:default}
.pumpwin.moving canvas{cursor:move}
.pumpwin .pumpedit{position:absolute; z-index:2; box-sizing:border-box; margin:0; padding:0 4px; border:1.5px solid var(--pw-ink); background:var(--pw-bg); color:var(--pw-ink);
  font-family:"JetBrains Mono",ui-monospace,monospace; text-align:center; outline:none; border-radius:0; user-select:text; -webkit-user-select:text}
.pumpwin .pumpedit::selection{background:var(--pw-neon); color:#000}
.pumpwin .pumpedit[hidden]{display:none}`;
document.head.append(style);

/* ---------------------------------------------------------------- the API for the plugin files (docs/PUMP-FX.md, "The pop-up kit") */
Object.assign(window.PumpUI = window.PumpUI || {}, {
  define(type, spec){ defs[type] = spec; },
  open, windows:() => [...wins.values()], closeAll(){ for (const w of [...wins.values()]) w.close(); },
  Rect, R, clamp, dbOf, now, css, mix, alpha, hex, buildTheme, ACCENTS, Feed, Spectrum, analyzerSize,
  Dial, Row, Btn, Seg, Menu, Field, Ctl, parseParam, PF, skipFlags, FOOT, THEME, themeDark, isMac,
});
FX.openHook = (t, k, info) => { open(t, k, info); };
FX.ownsKey = e => { const el = e.target && e.target.closest && e.target.closest('.pumpwin'); if (!el) return false; if (e.target.classList && e.target.classList.contains('pumpedit')) return true;
  for (const w of wins.values()) if (w.el === el) return w.owns(e); return false; };
FX.backHook = () => { const all = [...wins.values()]; if (!all.length) return false; all.sort((a, b) => (+b.el.style.zIndex || 0) - (+a.el.style.zIndex || 0))[0].close(); return true; };
})();
