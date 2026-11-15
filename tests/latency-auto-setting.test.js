// LATENCY COMP AUTO, settings and wording (build 615): aset.pdcMode ('auto' | 'on' | 'off'), its migration from the old pdc boolean,
// saving, reset, the FX screen's wording by mode, the bus note from the effect's real latency, and the one-time notice.
// The code is cut out of index.html and run in a vm.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'index.html: could not find ' + what + '; the code moved, so this test needs updating'); return m[0]; };

function page(o = {}){
  const store = {}, toasts = [];
  const c = vm.createContext({Math, Object, String, NT:32, MT:32, aset:Object.assign({pdc:false}, o.aset), rendering:!!o.rendering, ctx:{sampleRate:48000},
    S:{tfx:o.tfx || [], mfx:[]}, TFX:{pcalm:{pump:'calm', long:'CALM'}, ptune:{pump:'tune'}, peq:{pump:'eq'}, comp:{}}, isPumpT:t => !!c.TFX[t].pump,
    pumpFx:t => ({name:t}), pumpIdx:{}, PumpFX:{latency:() => o.lat == null ? 2048 : o.lat}, lat:o.lats || {},
    pumpLatencyOf:f => c.lat[f.type] || 0, pumpSlotLat:f => f && c.lat[f.type] ? c.lat[f.type] : 0,
    pdcPlan:() => ({T:o.T || 0}), localStorage:{getItem:k => store[k] || null, setItem:(k, v) => { store[k] = v; }}, asetSave:() => { c.saved = JSON.stringify(c.aset); },
    toast:(m, ms) => toasts.push(m), moFxAt:5, rt:null, pdcNotice:null, saved:null});
  c.toasts = toasts; c.store = store;
  c.toastOnce = (key, msg, ms) => { const K = 'thunderplus-seen-' + key; if (store[K]) return; store[K] = '1'; toasts.push(msg); };
  vm.runInContext(cut(/const pdcModeOf = [^\n]*\n/, 'pdcModeOf'), c);
  vm.runInContext(cut(/const PDC_HELP = [\s\S]*?\n  on:[^\n]*\n/, 'PDC_HELP'), c);
  vm.runInContext(cut(/function pdcSet\(v\)\{[\s\S]*?\n  pdcNotice\(\); \}\n/, 'pdcSet'), c);
  vm.runInContext(cut(/function pdcWho\(sr\)\{[\s\S]*?\n  return w; \}\n/, 'pdcWho') + cut(/function pdcNotice\(\)\{[\s\S]*?\n  toastOnce\([^\n]*\n/, 'pdcNotice'), c);
  vm.runInContext(cut(/const pumpBusNote = [\s\S]*?\n  if \(!\(v > 0\)\)[^\n]*\n[^\n]*\n/, 'pumpBusNote'), c);
  vm.runInContext(cut(/function pumpLatTxt\(t, k\)\{[\s\S]*?\n\}\n/, 'pumpLatTxt'), c);
  vm.runInContext(cut(/function audResetAset\(a\)\{[^\n]*\n/, 'audResetAset'), c);
  vm.runInContext('this.pdcModeOf = pdcModeOf; this.pumpBusNote = pumpBusNote; this.pdcNotice = pdcNotice; this.PDC_HELP = PDC_HELP;', c);
  return c;
}

test('default is AUTO; a saved pdc true reads as ON, false and nothing as AUTO; pdcMode wins', () => {
  const run = (aset) => page({aset}).pdcModeOf();
  assert.equal(run({}), 'auto'); assert.equal(run({pdc:true}), 'on'); assert.equal(run({pdc:false}), 'auto');
  assert.equal(run({pdc:true, pdcMode:'off'}), 'off'); assert.equal(run({pdc:false, pdcMode:'on'}), 'on'); assert.equal(run({pdcMode:'junk'}), 'auto');
  assert.ok(!/const aset = \(\(\) => \{ const d = \{[^}]*pdcMode/.test(src), 'pdcMode is not in the defaults (a default would hide an old saved pdc: true)');
});

test('saving writes pdcMode and the old pdc boolean (true only for ON); the moFxLat cache is reset', () => {
  const c = page();
  for (const [v, pdc] of [['on', true], ['off', false], ['auto', false]]){
    c.moFxAt = 5; c.pdcSet(v); const j = JSON.parse(c.saved);
    assert.equal(j.pdcMode, v); assert.equal(j.pdc, pdc); assert.ok(c.moFxAt < 0, 'cache reset');
  }
  assert.ok(c.toasts.length >= 3);
  c.pdcSet('bogus'); assert.equal(c.aset.pdcMode, 'auto');
});

test('Reset audio settings puts it back to AUTO', () => {
  const c = page({aset:{pdc:true, pdcMode:'on'}}); c.audResetAset(c.aset);
  assert.equal(c.pdcModeOf(), 'auto'); assert.equal(c.aset.pdc, false);
});

test('help texts say what each value does', () => {
  const c = page(); const h = vm.runInContext('PDC_HELP', c);
  assert.match(h.auto, /while the song plays/); assert.match(h.auto, /Stopped, pads answer at once/); assert.match(h.on, /also while stopped/); assert.match(h.off, /plays late/);
  assert.match(src, /id="segPdc"/); assert.ok(!/id="ckPdc"/.test(src), 'the checkbox is gone');
});

test('FX screen: pumpLatTxt by mode; the master sentence stays', () => {
  for (const m of ['auto', 'on']){
    const c = page({aset:{pdcMode:m}}); assert.equal(c.pumpLatTxt(3, 0), 'latency 43 ms: the other tracks wait for it (LATENCY COMP)'.replace('43', '43'));
  }
  const o = page({aset:{pdcMode:'off'}}).pumpLatTxt(3, 0);
  assert.match(o, /plays that much late against the other tracks \(not compensated\)/); assert.match(o, /Audio & MIDI › LATENCY COMP lines it up/);
  assert.equal(page().pumpLatTxt(32, 0), 'latency 43 ms (the whole mix moves; nothing to line up)');
  assert.equal(page({lat:0}).pumpLatTxt(3, 0), 'no latency');
});

test('bus note comes from the effect\'s real latency (TUNE counts, no latency says nothing), worded by mode', () => {
  const lats = {pcalm:2048, ptune:852};
  const a = page({aset:{pdcMode:'auto'}, lats}), on = page({aset:{pdcMode:'on'}, lats}), off = page({aset:{pdcMode:'off'}, lats});
  assert.match(a.pumpBusNote('ptune', {type:'ptune'}), /looks 18 ms ahead: the other tracks will wait 18 ms while the song plays/);
  assert.match(on.pumpBusNote('pcalm', {type:'pcalm'}), /the other tracks will wait 43 ms$/);
  assert.match(off.pumpBusNote('ptune', {type:'ptune'}), /plays late against the other tracks.*LATENCY COMP lines it up/);
  assert.equal(a.pumpBusNote('peq', {type:'peq'}), '');
});

test('one-time notice: once, only when AUTO delays something, never in a render, names the effect and track', () => {
  const tfx = []; tfx[11] = [null, {type:'pcalm', on:true}];
  const lats = {pcalm:2208};
  let c = page({T:0, tfx, lats}); c.pdcNotice(); assert.equal(c.toasts.length, 0, 'T = 0: nothing');
  c = page({T:0.046, tfx, lats, rendering:true}); c.pdcNotice(); assert.equal(c.toasts.length, 0, 'render: nothing'); assert.equal(Object.keys(c.store).length, 0);
  for (const m of ['on', 'off']){ c = page({T:0.046, tfx, lats, aset:{pdcMode:m}}); c.pdcNotice(); assert.equal(c.toasts.length, 0, m); }
  c = page({T:0.046, tfx, lats}); c.pdcNotice(); c.pdcNotice(); c.pdcSet('auto'); c.pdcNotice();
  const n = c.toasts.filter(t => /look/.test(t) && /LATENCY COMP turns this off/.test(t)); assert.equal(n.length, 1, 'exactly one notice');
  assert.match(n[0], /^CALM on track 12 looks 46 ms ahead, so the other tracks now wait 46 ms while the song plays/);
  c = page({T:0.046, tfx:[], lats}); c.pdcNotice(); assert.match(c.toasts[0], /^An effect with look-ahead makes the other tracks wait 46 ms/);
  // a device that already saw it
  c = page({T:0.046, tfx, lats}); c.store['thunderplus-seen-pdc-auto'] = '1'; c.pdcNotice(); assert.equal(c.toasts.length, 0);
});

test('the notice is wired to effect changes and PLAY, and the displays add the compensation', () => {
  assert.match(src, /function fxChanged\(t\)\{[^\n]*pdcNotice\(\)/); assert.match(src, /pdcSync\(true\); \} catch\(e\)\{\}[^\n]*\n  try \{ pdcNotice\(\); \}/);
  assert.ok(!/function render[^\n]*pdcNotice/.test(src));
  assert.equal((src.match(/heardLat\(\) \+ moFxLat\(\)|heardLat\(\) - moFxLat\(\)/g) || []).length >= 5, true);
});
