// AUDIO BUILDING BLOCKS, editor side (14 Nov 2026, module queue): One-pole filter, Allpass, DC blocker and Mid / side in both editors (the shared
// block), and Oscillator and Noise in …chains only (…waves has its own VCO and Noise). Source guards in the style of modules-clock-editor: each has the
// engine's params, ui labels, about / use / hint text, a width, a Compact summary and its palette place, and the SHARED EDITOR block is identical in both pages.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const WK = { chains: H.read('chains/chains-worklet.js'), waves: H.read('waves/waves-worklet.js') };
const SHARED = ['onepole', 'allpass', 'dcblock', 'midside'];   // the palette order, group "Building blocks"
const CHAINS_ONLY = ['osc', 'wnoise'];
const block = (src, tag) => { const m = new RegExp('/\\* ===' + tag + ' START===[\\s\\S]*?/\\* ===' + tag + ' END=== \\*/').exec(src); assert.ok(m, tag); return m[0]; };
function literal(src, i) {
  const open = src[i], close = open === '{' ? '}' : ']'; let d = 0, q = null;
  for (let j = i; j < src.length; j++) {
    const c = src[j];
    if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; }
    if (c === "'" || c === '"' || c === '`') q = c; else if (c === open) d++; else if (c === close && --d === 0) return src.slice(i, j + 1);
  }
  throw new Error('unbalanced');
}
const ctx = { opts: names => names.map((t, i) => [i, t]), pct: v => Math.round(v * 100) + '%', spct: v => (v > 0 ? '+' : '') + Math.round(v * 100) + '%', secs: v => v < 1 ? Math.round(v * 1000) + ' ms' : v.toFixed(2) + ' s',
  whole: v => String(Math.round(v)), khz: v => v >= 1000 ? (v / 1000).toFixed(1) + ' kHz' : Math.round(v) + ' Hz' };
const ev = s => vm.runInNewContext('(' + s + ')', ctx);
function entry(src, type) {
  const re = new RegExp('\\n  ' + type + ':\\{ label:'), m = re.exec(src); assert.ok(m, 'editor entry for ' + type);
  return literal(src, m.index + 1 + ('  ' + type + ':').length);
}
function engineParams(src, type) {
  const i = src.indexOf("type:'" + type + "', scope:'"); assert.ok(i > 0, 'worklet def for ' + type);
  const j = src.indexOf('params:', i); return ev(literal(src, src.indexOf('{', j)));
}
const LABELS = { onepole: { freq: 'Freq' }, allpass: { mode: 'Mode', freq: 'Freq', time: 'Time', gain: 'Gain' }, dcblock: { freq: 'Freq' }, midside: { mode: 'Mode' },
  osc: { wave: 'Wave', freq: 'Freq', fm: 'FM depth', level: 'Level' }, wnoise: { color: 'Colour', level: 'Level' } };
const PORTS = { onepole: [['in'], ['lp', 'hp']], allpass: [['in'], ['out']], dcblock: [['in'], ['out']], midside: [['in', 'inR'], ['out', 'outR']], osc: [['fm', 'sync'], ['out']], wnoise: [[], ['out']] };
const KINDS = { fm: 'cv', sync: 'gate', in: 'audio', inR: 'audio', out: 'audio', outR: 'audio', lp: 'audio', hp: 'audio' };

for (const ed of ['chains', 'waves']) {
  const src = ED[ed], types = ed === 'chains' ? SHARED.concat(CHAINS_ONLY) : SHARED;
  for (const t of types) {
    test(ed + ': ' + t + ' is defined with the engine\'s params, ports, labels and an about text', () => {
      const e = entry(src, t), params = ev(literal(e, e.indexOf('params:') + 7));
      assert.equal(JSON.stringify(params), JSON.stringify(engineParams(WK[ed], t)), 'params match the worklet');
      const ui = ev(literal(e, e.indexOf('ui:') + 3));
      for (const k of Object.keys(params)) assert.ok(ui[k] && ui[k].l, t + '.' + k + ' has a label');
      for (const [k, l] of Object.entries(LABELS[t])) assert.equal(ui[k].l, l, t + '.' + k);
      const ins = ev(literal(e, e.indexOf('inputs:') + 7)), outs = ev(literal(e, e.indexOf('outputs:') + 8));
      assert.equal(Object.keys(ins).join(), PORTS[t][0].join()); assert.equal(Object.keys(outs).join(), PORTS[t][1].join());
      for (const [k, v] of Object.entries(Object.assign({}, ins, outs))) assert.equal(v, KINDS[k], t + ' port ' + k);
      assert.match(e, /about:'[^']{120,}'/, 'about'); assert.match(e, /scope:'mono'/);
    });
    test(ed + ': ' + t + ' has a palette entry, a use line, a hint, a width, a family and a Compact summary', () => {
      if (t === 'osc' || t === 'wnoise') assert.match(src, new RegExp("\\['" + t + "','(Oscillator|Noise)'\\]"), 'PALETTE');
      else assert.match(src, new RegExp("\\['" + t + "','[^']+'\\]"), 'SHARED_PALETTE');
      assert.match(src, new RegExp("\\n  " + t + ":'[^'\\n]{30,}'"), 'MODULE_USE line');
      assert.match(/const PAL_LAYOUT = \[[\s\S]*?\n\];/.exec(src)[0], new RegExp("'" + t + "'"), 'PAL_LAYOUT');
      assert.match(/const PAL_HINT = \{[^}]*\}/.exec(src)[0], new RegExp('\\b' + t + ":'[^']+'"), 'PAL_HINT');
      if (CHAINS_ONLY.includes(t)) assert.match(entry(src, t), /scope:'mono', w:\d{3},/, 'width on the definition (the MODULE_WIDTH table is the same in both editors)');
      else assert.match(src, new RegExp('MODULE_WIDTH = \\{[\\s\\S]*?\\b' + t + ':(?:NARROW_[SM]|\\d{3})'), 'MODULE_WIDTH');
      assert.match(/const SUMMARY_KEYS = \{[\s\S]*?\n\};/.exec(src)[0], new RegExp('\\b' + t + ":\\['"), 'SUMMARY_KEYS');
      if (SHARED.includes(t)) assert.match(src, new RegExp('\\b' + t + ":'afx'"), 'SHARED_FAMILY');
    });
  }
  test(ed + ': Building blocks follows Filters and EQ in the layout, in order, and the palette group is made', () => {
    const lay = /\['Filters and EQ', '#[0-9a-f]{6}', \[[^\]]*\]\],\n  \['Building blocks', '#[0-9a-f]{6}', \[([^\]]*)\]\]/.exec(src); assert.ok(lay, 'Building blocks right after Filters and EQ');
    assert.deepEqual(lay[1].split(',').map(s => s.trim().replace(/'/g, '')), ['onepole', 'allpass', 'dline', 'dcblock', 'midside']);   // Delay line joined after Allpass (tests/modules-delay-editor.test.js)
    assert.match(src, /SHARED_PALETTE\.push\(\['Building blocks', \[\['onepole','One-pole filter'\],\['allpass','Allpass'\],\['dline','Delay line'\],\['dcblock','DC blocker'\],\['midside','Mid \/ side'\]\], 'audio'\]\)/);
  });
  test(ed + ': Mid / side names its own jacks; One-pole its two outputs; the Help paragraph names the four', () => {
    assert.match(entry(src, 'midside'), /plabels:\{ in:'L \/ Mid', inR:'R \/ Side', out:'Mid \/ L', outR:'Side \/ R' \}/);
    assert.match(src, /if \(d && d\.plabels && d\.plabels\[port\]\) return d\.plabels\[port\];/);
    assert.match(src, /lp:'Low', hp:'High'/);
    assert.match(src, /\['Building blocks', 'Building blocks in the Modules list[^']*One-pole filter[^']*Allpass[^']*DC blocker[^']*Mid \/ side/);
    assert.equal(/\['Building blocks', '([^']*)'\]/.exec(src)[1].includes('Oscillator'), ed === 'chains', 'Help names the Oscillator and Noise in …chains only');
    const al = entry(src, 'allpass'); const ui = ev(literal(al, al.indexOf('ui:') + 3));
    assert.equal(ui.time.f(.0105), '10.5 ms'); assert.equal(ui.gain.f(.5), '+50%'); assert.equal(JSON.stringify(ui.mode.c), '[[0,"Phase"],[1,"Echo"]]');
    assert.equal(JSON.stringify(ev(literal(entry(src, 'midside'), entry(src, 'midside').indexOf('ui:') + 3)).mode.c), '[[0,"Encode"],[1,"Decode"]]');
  });
}
test('Oscillator and Noise are in …chains\' own list and the Sound sources group, and not in …waves', () => {
  const c = ED.chains, w = ED.waves, sh = block(c, 'SHARED EDITOR');
  for (const t of CHAINS_ONLY) {
    assert.ok(!sh.includes('\n  ' + t + ':{ label:'), t + ' is not in the shared block');
    assert.ok(!new RegExp('\\n  ' + t + ':\\{ label:').test(w), t + ' is not defined in …waves');
    assert.ok(!new RegExp("'" + t + "'").test(w.replace(/const PAL_LAYOUT = \[[\s\S]*?\n\];/, '')), t + ' appears in …waves only in the shared layout list');
  }
  assert.match(c, /\['Sound sources', \[\['osc','Oscillator'\],\['wnoise','Noise'\]\]\]/);
  assert.match(c, /'elements', 'osc', 'wnoise', 'bytebeat', 'blipdrum'\]/);
  assert.match(w, /\['Sound sources', \[\['string','String'\],\['vco','VCO'\],\['fm','FM'\],\['additive','Additive'\],\['noise','Noise'\],\['plaits','Plaits'\],\['elements','Elements'\]\]\]/, '…waves\' own sources are as they were');
  assert.ok(!w.includes("label:'Oscillator'"));
  assert.ok(!/MODULE_WIDTH = \{[\s\S]*?\b(osc|wnoise):/.test(/const MODULE_WIDTH = \{[\s\S]*?\n\};/.exec(c)[0]), 'not in the shared width table');
  assert.match(c, /sync:'Sync',\n  cutoff:'Cutoff'/, '…chains labels the Sync jack');
  const worklets = ['chains', 'waves'].map(e => /const SHARED_MODULES = \{[\s\S]*?\n\};/.exec(WK[e])[0]);
  assert.ok(!/\bosc:|wnoise:/.test(worklets[0]) && !/\bosc:|wnoise:/.test(WK.waves.slice(WK.waves.indexOf('const MODULES = {'))), 'the engines agree: …chains only');
});
test('Sound sources in …chains: Oscillator and Noise lead Bytebeat and Blip drum', () => {
  const lay = /\['Sound sources', '#[0-9a-f]{6}', \[([^\]]*)\]\]/.exec(ED.chains)[1].split(',').map(s => s.trim().replace(/'/g, ''));
  const mine = lay.filter(t => ['osc', 'wnoise', 'bytebeat', 'blipdrum'].includes(t)); assert.deepEqual(mine, ['osc', 'wnoise', 'bytebeat', 'blipdrum']);
});
test('the Freq sliders are linear, like Filter\'s Cutoff (the editors have no log slider; the engine params are in Hz)', () => {
  for (const ed of ['chains', 'waves']) assert.doesNotMatch(entry(ED[ed], 'onepole'), /log:|curve:/);
});
test('the SHARED EDITOR block is identical in …chains and …waves, and so is the shared worklet block', () => {
  assert.equal(block(ED.chains, 'SHARED EDITOR'), block(ED.waves, 'SHARED EDITOR'));
  assert.equal(block(WK.chains, 'SHARED MODULES'), block(WK.waves, 'SHARED MODULES'));
});
