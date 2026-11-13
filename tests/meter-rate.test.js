// METERS (13 Nov 2026): Settings -> METERS: SMOOTH | LIGHT | OFF WHILE PLAYING. The gate, the highest-since-last-paint rule, no write for an unchanged value,
// stored / restored / reset, default SMOOTH. Sandboxed pieces of index.html; no browser.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = re => { const m = src.match(re); assert.ok(m, 'not found: ' + re); return m[0]; };

function gate(stored, o = {}) {
  const store = Object.assign({}, stored), c = { tp: { on: true }, lpOn: !!o.lpOn, LP_GATE: 38, HM: { drawn: '' }, paintMeters() {}, mtrWake() {}, sndWake() {},
    localStorage: { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } }, store, performance: { now: () => 0 } };
  vm.createContext(c);
  vm.runInContext(cut(/var MTR_K = [\s\S]*?var mtrBlank = false;/) + '\nthis.mode = () => mtrMode;', c);
  vm.runInContext(cut(/function setMeters\(v\)\{[\s\S]*?\n\}\n/), c);
  return c;
}
// paints a frame at 60 Hz for ten seconds, counting the paints the gate lets through
function paints(c, secs = 10, hz = 60) { let last = -1e9, n = 0; for (let t = 0; t < secs * 1000; t += 1000 / hz) if (c.meterDue(t, last, 0)) { last = t; n++; } return n / secs; }

test('default is SMOOTH on every device, and the choice is stored under its own key', () => {
  const c = gate({}); assert.equal(c.mode(), 'smooth');
  assert.equal(gate({ 'thunderplus-meters-v1': 'junk' }).mode(), 'smooth');
  c.setMeters('light'); assert.equal(c.store['thunderplus-meters-v1'], 'light');
});
test('stored choice is restored on load; setMeters repaints and wakes the parked loops', () => {
  assert.equal(gate({ 'thunderplus-meters-v1': 'off' }).mode(), 'off'); assert.equal(gate({ 'thunderplus-meters-v1': 'light' }).mode(), 'light');
  const c = gate({}); let p = 0, w = 0; c.paintMeters = () => p++; c.mtrWake = () => w++; c.setMeters('off'); assert.equal(p, 1); assert.equal(w, 1); assert.equal(c.HM.drawn, '');
});
test('the gate: SMOOTH as the loop alone (every frame), LIGHT about 10 a second, OFF none while playing, everything when stopped', () => {
  const c = gate({}); assert.ok(paints(c) >= 59);
  c.setMeters('light'); const l60 = paints(c, 10, 60), l120 = paints(c, 10, 120); assert.ok(l60 >= 9.5 && l60 <= 10.5, 'light at 60 Hz: ' + l60); assert.ok(l120 >= 9.5 && l120 <= 10.5, 'light at 120 Hz: ' + l120);
  c.setMeters('off'); assert.equal(paints(c), 0); assert.equal(c.meterHold(), true);
  c.tp.on = false; assert.equal(c.meterHold(), false); assert.ok(paints(c) >= 59, 'stopped: nothing is held');
});
test('a loop base gap still counts, and Low power is independent: the slower of the two wins', () => {
  const c = gate({}, { lpOn: true }); assert.equal(c.meterGap(33), 38); c.setMeters('light'); assert.equal(c.meterGap(33), 92); c.setMeters('smooth'); assert.equal(c.meterGap(50), 50);
  const d = gate({}); d.setMeters('light'); assert.equal(d.meterGap(100), 100, 'a loop slower than LIGHT keeps its own rate');
  d.setMeters('off'); assert.equal(d.meterGap(33, true), 92, 'a mark that is not a meter (the piano) keeps LIGHT rate at OFF');
  assert.equal(d.lpHold(0), true); d.tp.on = false; assert.equal(d.lpHold(0), false);
});
test('Smooth and Light: Low power alone behaves as before (38 ms), METERS SMOOTH adds nothing', () => {
  const c = gate({}, { lpOn: true }); assert.equal(c.meterDue(37, 0, 0), false); assert.equal(c.meterDue(38, 0, 0), true);
});

// paintMeters: the highest since the last paint, and no DOM write when the shown value is unchanged
function rig() {
  const c = { performance: { now: () => 0 }, clamp: (v, a, b) => Math.min(b, Math.max(a, v)), lvl: v => v, MH: 4, mixL: 0, mixN: 1, BLK: ' ▁▂▃▄▅▆▇█'.split(''), mcutOn: false, mCut: () => false, trimEls: [], trimLit: '',
    meterHold: () => c.hold, hold: false, masterEls: [], MW: 40, writes: 0 };
  const mk = () => { const e = { _h: undefined, style: { set height(v) { c.writes++; } } }; return e; };
  c.bar = mk(); c.document = { querySelectorAll: () => [c.bar] };
  c.meterEls = [0, 1, 2, 3].map(() => ({ _t: '', get textContent() { return this._t; }, set textContent(v) { c.writes++; this._t = v; } }));
  c.bus = { lv: 0.1, lvA: 0.1 }; c.rt = { pl: { buses: [c.bus] }, mlv: [0, 0], mlA: [0, 0], mpk: [0, 0] };
  vm.createContext(c); vm.runInContext(cut(/function paintMeters\(\)\{[\s\S]*?\n\}\n/), c); return c;
}
test('LIGHT paints the highest level seen since the last paint, not the latest sample, then starts again from the current one', () => {
  const c = rig(); c.bus.lv = 0.9; c.bus.lvA = 0.9; c.bus.lv = 0.1;   // a peak, then it fell before the paint
  c.paintMeters(); assert.equal(c.bar._h, '90%'); assert.equal(c.bus.lvA, 0.1);
  c.paintMeters(); assert.equal(c.bar._h, '10%');
});
test('an unchanged value writes nothing (a steady or idle meter is silent)', () => {
  const c = rig(); c.paintMeters(); const w = c.writes; assert.ok(w > 0); c.paintMeters(); c.paintMeters(); assert.equal(c.writes, w);
  c.bus.lv = c.bus.lvA = 0.1004; c.paintMeters(); assert.equal(c.writes, w, 'a change under a whole per cent is not a write');
});
test('OFF while playing paints empty once (the levels are not shown), and the measured levels are untouched', () => {
  const c = rig(); c.bus.lv = c.bus.lvA = 0.8; c.hold = true; c.paintMeters(); assert.equal(c.bar._h, '0%'); assert.equal(c.bus.lv, 0.8);
});

test('measuring is kept: the frame loop and the header still read the analysers; only the painting is gated (source guards)', () => {
  assert.match(src, /if \(!hold\) for \(let t = 0; t < ntr\(\); t\+\+\)\{ const b = rt\.pl\.buses\[t\]/);   // OFF: only the track meters (display only) are not read
  assert.match(src, /if \(rt\.man\)\{ const now = mt;[\s\S]*?rt\.mpk\[s\] = p;/, 'the master (peak, CLIP) is read at every setting');
  const hm = cut(/function hmDraw\(now\)\{[\s\S]*?if \(!c \|\| !c\.offsetParent\) return;/);
  assert.match(hm, /if \(p >= 0\.999\) HM\.clip = true;/); assert.doesNotMatch(hm, /meterDue|meterHold/, 'the header measures before any gate');
  assert.doesNotMatch(cut(/function pbLevel\(\)\{[^\n]*/), /meter/); assert.doesNotMatch(cut(/function pbLvRead\(\)\{[^\n]*/), /meter/);
});
test('Settings has the METERS row beside LOW POWER DISPLAY, three choices, default SMOOTH, and Reset audio settings puts it back', () => {
  const row = cut(/\{l:'METERS WHILE PLAYING', segs:true[^\n]*\n[^\n]*\n/);
  assert.match(row, /d:0/); assert.match(row, /'SMOOTH', 'LIGHT', 'OFF'/); assert.match(row, /set:v => setMeters\(MTR_MODES\[v\]\)/);
  assert.ok(src.indexOf("{l:'METERS WHILE PLAYING'") > src.indexOf("{l:'LOW POWER DISPLAY'") && src.indexOf("{l:'METERS WHILE PLAYING'") < src.indexOf("{l:'FOLLOW', segs"));
  assert.match(src, /if \(mtrMode !== 'smooth'\) setMeters\('smooth'\)/);
});

// ---- build 576 review ----
test('review 1: every meter loop the gate parks paints itself empty once first (meterBlank), then writes nothing until the hold ends', () => {
  const c = { tp: { on: true }, lpOn: false, LP_GATE: 38, localStorage: { getItem: () => 'off', setItem() {} }, HM: {}, paintMeters() {}, mtrWake() {} };
  vm.createContext(c); vm.runInContext(cut(/var MTR_K = [\s\S]*?var mtrBlank = false;/) + '\nthis.n = 0; this.blank = () => meterBlank("k", () => this.n++);', c);
  assert.equal(c.blank(), true); assert.equal(c.blank(), true); assert.equal(c.n, 1, 'painted empty once, then parked');
  c.tp.on = false; assert.equal(c.blank(), false, 'stopped: not held'); c.tp.on = true; c.blank(); assert.equal(c.n, 2, 'a new hold paints empty again');
  // each loop that returns early under OFF goes through meterBlank
  for (const [fn, key] of [['synMeterFrame', 'syn'], ['vuFrame', 'vu'], ['sndFrame', 'snd'], ['bassViz', 'bass'], ['mwLoop', 'mw']]) assert.match(cut(new RegExp('function ' + fn + '\\([^)]*\\)\\{[\\s\\S]*?\\n\\}\\n')), new RegExp("meterBlank\\('" + key + "'"), fn);
  assert.match(src, /meterBlank\('stg', \(\) => stageCanvas\(true\)\)/, 'stage spectrum: the idle picture once');
  assert.match(src, /held \? mtrB\.scope : !meterDue\(now, dkScopeT, 50\)/, 'deck scope: the grid once'); assert.match(src, /meterBlank\('fxbar'/, 'FX bar viz canvases cleared once');
  const syn = cut(/function synMeterFrame\(\)\{[\s\S]*?\n\}\n/); assert.match(syn, /scaleX\(0\)[^\n]*left = '0'[^\n]*remove\('over'\)/);
  // no loop is parked by a bare meterHold() return any more
  for (const fn of ['synMeterFrame', 'vuFrame', 'mwLoop']) assert.doesNotMatch(cut(new RegExp('function ' + fn + '\\([^)]*\\)\\{[\\s\\S]*?\\n\\}\\n')), /if \(meterHold\(\)\)/);
});
test('review 2: the header paints the maximum since the last paint (a snapshot), and the cache key and the bars use the same numbers', () => {
  const hm = cut(/function hmDraw\(now\)\{[\s\S]*?\n\}\n/);
  assert.match(hm, /HM\.lvs\[0\] = HM\.la\[0\]; HM\.lvs\[1\] = HM\.la\[1\]/); assert.match(hm, /const LV = hold \? HM\.zero : HM\.lvs/);
  assert.ok(hm.indexOf('HM.lvs[0] = HM.la[0]') < hm.indexOf('HM.la[0] = HM.lv[0]'), 'snapshot before the reset');
  assert.doesNotMatch(hm.slice(hm.indexOf('HM.la[1] = HM.lv[1];') + 20), /HM\.la\b/, 'nothing after the reset reads the accumulator');
  assert.doesNotMatch(hm, /\? \[0, 0\] :/, 'no array made per paint');
  // behaviour of the three lines: a peak between paints survives the reset
  const c = { HM: { la: [0.9, 0.8], lv: [0.1, 0.1] } }; vm.createContext(c);
  vm.runInContext('const hold = false; if (!HM.lvs){ HM.lvs = [0, 0]; HM.zero = [0, 0]; } if (!hold){ HM.lvs[0] = HM.la[0]; HM.lvs[1] = HM.la[1]; } const LV = hold ? HM.zero : HM.lvs; HM.la[0] = HM.lv[0]; HM.la[1] = HM.lv[1]; this.LV = LV;', c);
  assert.deepEqual([...c.LV], [0.9, 0.8]); assert.deepEqual([...c.HM.la], [0.1, 0.1]);
});
test('review 3: the FX bar at LIGHT samples every frame and paints the highest slot level since the last paint', () => {
  let now = 0, level = 0; const w = { style: {}, _w: undefined };
  const c = { performance: { now: () => now }, clamp: (v, a, b) => Math.min(b, Math.max(a, v)), mtrMode: 'light', mtrB: {}, tp: { on: true }, lpOn: false, LP_GATE: 38, NT: 32, S: { tfx: [[]], mfx: [] }, deck: { fxbar: true, fx: true },
    $: () => bar, deckShown: () => true, dkMst: () => false, dkTrack: () => 0, fxDynNodeOf: () => null, fxLvlSet() {}, isFxDyn: () => false, fxSlotLevel: () => level, dkFxViz() {}, dkAnim() {}, dkSpring() {} };
  const col = { dataset: { k: '0' }, _h: new Float32Array(8), _w: 0, querySelector: s => (s === '.fm i' ? w : null) }, bar = { children: [col], querySelectorAll: () => [] };
  vm.createContext(c);
  vm.runInContext(cut(/var MTR_K = [\s\S]*?var meterDue = [^\n]*\n/).replace(/var mtrMode = [^\n]*\n/, '') + cut(/function deckFxBarMeters\(\)\{[\s\S]*?\n\}\n/) + '\nthis.run = deckFxBarMeters;', c);
  vm.runInContext('var meterHold = () => false; var meterBlank = (k, f) => false;', c);
  now = 1000; level = 0.1; c.run();             // a paint is due: 10 %
  now = 1016; level = 0.9; c.run();             // a transient between paints
  now = 1033; level = 0.1; c.run();             // back down, still before the next paint
  assert.equal(w.style.width, '10%', 'nothing painted yet at 33 ms');
  now = 1100; level = 0.1; c.run(); assert.equal(w.style.width, '90%', 'the transient is painted');
  now = 1200; c.run(); assert.equal(w.style.width, '10%', 'and the maximum starts again');
  c.mtrMode = 'smooth'; now = 1216; level = 0.5; c.run(); assert.equal(w.style.width, '50%', 'SMOOTH paints every frame as before');
});
