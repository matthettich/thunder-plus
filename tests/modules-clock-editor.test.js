// CLOCK AND TRIGGER TOOLS, editor side (14 Nov 2026, module queue group 2): Clock multiplier, Chance gate, Counter, Step switch, Burst,
// Gate delay, Flip-flop; and Steps and Clock, which moved from …chains' own list into the SHARED EDITOR block so …waves has them too.
// Source guards in the style of modules-maths-editor: each is defined in both editors with the engine's params, sits in the palette group in order,
// has about / use / hint text, a width and a Compact summary, and the SHARED EDITOR block is identical in both pages. (The headless-browser check
// of the seven in both editors was run by hand: open, Compact and Mini at 1440x900 and 344x780, and an old …chains patch with Steps and Clock.)
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const WK = { chains: H.read('chains/chains-worklet.js'), waves: H.read('waves/waves-worklet.js') };
const TYPES = ['clkmul', 'chance', 'counter', 'stepsw', 'burst', 'gatedly', 'flipflop'];   // the palette order, after Clock divider
const MOVED = ['steps', 'clock'];
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
const ev = s => vm.runInNewContext('(' + s + ')', { opts: names => names.map((t, i) => [i, t]), pct: v => Math.round(v * 100) + '%', secs: v => v < 1 ? Math.round(v * 1000) + ' ms' : v.toFixed(2) + ' s', whole: v => String(Math.round(v)) });
function entry(src, type) {
  const re = new RegExp('\\n  ' + type + ':\\{ label:'), m = re.exec(src); assert.ok(m, 'editor entry for ' + type);
  return literal(src, m.index + 1 + ('  ' + type + ':').length);
}
function engineParams(src, type) {
  const i = src.indexOf("type:'" + type + "', scope:'"); assert.ok(i > 0, 'worklet def for ' + type);
  const j = src.indexOf('params:', i); return ev(literal(src, src.indexOf('{', j)));
}
const LABELS = { clkmul: { mul: 'Times', width: 'Gate length' }, chance: { prob: 'Chance', seed: 'Seed' }, counter: { steps: 'Steps', mode: 'Direction' },
  stepsw: { steps: 'Steps', fade: 'Fade' }, burst: { count: 'Count', time: 'Time', bend: 'Bend', width: 'Gate length' }, gatedly: { delay: 'Delay', length: 'Length' }, flipflop: {} };

for (const ed of ['chains', 'waves']) {
  const src = ED[ed];
  for (const t of TYPES.concat(MOVED)) {
    test(ed + ': ' + t + ' is defined with the engine\'s params, ui and an about sentence', () => {
      const e = entry(src, t), params = ev(literal(e, e.indexOf('params:') + 7));
      assert.equal(JSON.stringify(params), JSON.stringify(engineParams(WK[ed], t)), 'params match the worklet');
      const ui = ev(literal(e, e.indexOf('ui:') + 3));
      for (const k of Object.keys(params)) assert.ok(ui[k] && ui[k].l, t + '.' + k + ' has a label');
      for (const [k, l] of Object.entries(LABELS[t] || {})) assert.equal(ui[k].l, l, t + '.' + k);
      assert.match(e, /about:'[^']{40,}'/, 'about');
      assert.match(e, /scope:'mono'/);
    });
    test(ed + ': ' + t + ' has a palette entry, a use line, a width rule and is in its layout group', () => {
      assert.match(src, new RegExp("\\['" + t + "','?[^']+'?\\]|\\['" + t + "', '[^']+'\\]"), 'SHARED_PALETTE');
      assert.match(src, new RegExp("\\n  " + t + ":'[^'\\n]{30,}'"), 'MODULE_USE line');
      assert.match(/const PAL_LAYOUT = \[[\s\S]*?\n\];/.exec(src)[0], new RegExp("'" + t + "'"), 'PAL_LAYOUT');
    });
  }
  for (const t of TYPES) test(ed + ': ' + t + ' has a hint, a width, a family and a Compact summary (when it has params)', () => {
    assert.match(/const PAL_HINT = \{[^}]*\}/.exec(src)[0], new RegExp('\\b' + t + ":'[^']+'"), 'PAL_HINT');
    assert.match(src, new RegExp('MODULE_WIDTH = \\{[\\s\\S]*?\\b' + t + ':(?:NARROW_[SM]|\\d{3})'), 'MODULE_WIDTH');
    assert.match(src, new RegExp('\\b' + t + ":'(mod|util)'"), 'SHARED_FAMILY');
    if (t !== 'flipflop') assert.match(/const SUMMARY_KEYS = \{[\s\S]*?\n\};/.exec(src)[0], new RegExp('\\b' + t + ":\\['"), 'SUMMARY_KEYS');
  });
  test(ed + ': Clocks and rhythm lists the seven in order right after Clock divider', () => {
    const lay = /\['Clocks and rhythm', '#8c5a8c', \[([^\]]*)\]\]/.exec(src); assert.ok(lay);
    const ids = lay[1].split(',').map(s => s.trim().replace(/'/g, ''));
    assert.deepEqual(ids.filter(t => TYPES.includes(t)), TYPES);
    assert.deepEqual(ids.slice(ids.indexOf('divider') + 1, ids.indexOf('divider') + 8), TYPES);
    const pal = /\['Rhythm and logic', \[([^\n]*)\], 'control'\]/.exec(src); assert.ok(pal);
    assert.deepEqual([...pal[1].matchAll(/\['(\w+)',/g)].map(m => m[1]).filter(t => TYPES.includes(t)), TYPES);
    assert.ok(pal[1].indexOf("'divider'") < pal[1].indexOf("'clkmul'"));
  });
  test(ed + ': Steps stays with the Modulation sources and Clock leads Clocks and rhythm, as in …chains before', () => {
    assert.match(src, /\['Modulation sources', '#6e4fc0', \[[^\]]*'chaos', 'steps'\]\]/);
    assert.match(src, /\['Clocks and rhythm', '#8c5a8c', \['clock', 'divider'/);
    assert.match(src, /SHARED_PALETTE\.push\(\['Modulation sources', \[\['steps', 'Steps'\], \['clock', 'Clock'\]\], 'control'\]\)/);
  });
  test(ed + ': port labels, summary keys, the choices and the Help paragraph', () => {
    for (const [k, l] of [['step', 'Step'], ['end', 'End'], ['other', 'Other'], ['level', 'Level'], ['toggle', 'Toggle'], ['set', 'Set'], ['q', 'Q'], ['nq', 'Not Q'], ['i1', '1'], ['i4', '4']]) assert.ok(src.includes(k + ":'" + l + "'"), 'port label ' + k);
    assert.match(entry(src, 'counter'), /c:opts\(\['Up', 'Down', 'Up and down'\]\)/);
    const ch = ev(literal(entry(src, 'chance'), entry(src, 'chance').indexOf('ui:') + 3)), bu = ev(literal(entry(src, 'burst'), entry(src, 'burst').indexOf('ui:') + 3)), gd = ev(literal(entry(src, 'gatedly'), entry(src, 'gatedly').indexOf('ui:') + 3));
    assert.equal(ch.seed.f(0), 'Random'); assert.equal(ch.seed.f(7), '7');
    assert.equal(bu.bend.f(0), 'even'); assert.equal(bu.bend.f(.4), 'closer 40%'); assert.equal(bu.bend.f(-.4), 'wider 40%');
    assert.equal(gd.length.f(0), 'As it came'); assert.equal(gd.length.f(.25), '250 ms');
    assert.match(src, /\['Clocks and triggers', 'Clocks and rhythm in the Modules list[^']*Clock makes the pulses and Steps[^']*follow the song’s tempo, and Reset restarts them[^']*Clock multiplier[^']*Counter[^']*Chance gate[^']*Burst[^']*Gate delay[^']*Flip-flop[^']*Step switch/);
  });
  test(ed + ': Chance gate\'s about says what Seed does, Steps and Clock keep their text', () => {
    const a = entry(src, 'chance');
    assert.match(a, /same Seed gives the same run of hits every time, starting again when the song starts or at Reset, so a render matches what you heard/); assert.match(a, /Seed 0 is different every time/);
    assert.match(entry(src, 'steps'), /about:'Steps: an 8-step sequence of control values, one step per note value at the song tempo/);
    assert.match(entry(src, 'clock'), /about:'Clock: a gate every note value at the song tempo, with Swing on every second one/);
  });
}
test('Steps and Clock are defined once, in the shared block, and no longer in …chains\' own list or its PAGE_ABOUT', () => {
  const c = ED.chains, sh = block(c, 'SHARED EDITOR'), rest = c.replace(sh, '');
  for (const t of MOVED) {
    assert.equal(sh.split('\n  ' + t + ':{ label:').length - 1, 1, t + ' once in the shared block');
    assert.ok(!rest.includes('\n  ' + t + ':{ label:'), t + ' not defined again in …chains');
    assert.ok(!new RegExp('\\n  ' + t + ":'(Steps|Clock): ").test(rest), t + ' not in PAGE_ABOUT');
  }
  assert.ok(!/\['Modulation sources', \[\['lfo','LFO'\][^\n]*\['steps'/.test(c), 'not in …chains\' own palette');
});
test('Steps and Clock look as they always did in …chains: default family, no per-module width or summary override', () => {
  for (const ed of ['chains', 'waves']) for (const t of MOVED) {
    assert.ok(!new RegExp('\\b' + t + ":'(mod|util|afx)'").test(block(ED[ed], 'SHARED EDITOR').match(/Object\.assign\(SHARED_FAMILY[^;]*;/g).join('')), t + ' family');
    assert.ok(!new RegExp('\\b' + t + ':').test(/const MODULE_WIDTH = \{[\s\S]*?\n\};/.exec(ED[ed])[0]), t + ' width');
  }
});
test('the SHARED EDITOR block is identical in …chains and …waves, and so is the shared worklet block', () => {
  assert.equal(block(ED.chains, 'SHARED EDITOR'), block(ED.waves, 'SHARED EDITOR'));
  assert.equal(block(WK.chains, 'SHARED MODULES'), block(WK.waves, 'SHARED MODULES'));
});
