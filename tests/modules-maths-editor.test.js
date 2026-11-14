// CONTROL MATHS, editor side (14 Nov 2026, module queue group 1): Constant, Invert / rectify, Scale / map, Comparator, Switch, Crossfade.
// Source guards in the style of wc-pass5: each is defined in both editors with the engine's params, sits in the palette group, has about and use text,
// a width and a Compact summary that makes sense, and the SHARED EDITOR block is identical in both pages. (The headless-browser check of the six in both editors was run by hand: open, Compact and Mini at 1440x900 and 344x780.)
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const WK = { chains: H.read('chains/chains-worklet.js'), waves: H.read('waves/waves-worklet.js') };
const TYPES = ['const', 'rect', 'scale', 'compare', 'switch', 'xfade'];   // the palette order
const block = (src, tag) => { const m = new RegExp('/\\* ===' + tag + ' START===[\\s\\S]*?/\\* ===' + tag + ' END=== \\*/').exec(src); assert.ok(m, tag); return m[0]; };
/* The balanced {...} or [...] literal that starts at src[i]. */
function literal(src, i) {
  const open = src[i], close = open === '{' ? '}' : ']'; let d = 0, q = null;
  for (let j = i; j < src.length; j++) {
    const c = src[j];
    if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; }
    if (c === "'" || c === '"' || c === '`') q = c; else if (c === open) d++; else if (c === close && --d === 0) return src.slice(i, j + 1);
  }
  throw new Error('unbalanced');
}
const ev = s => vm.runInNewContext('(' + s + ')', { opts: names => names.map((t, i) => [i, t]), pct: v => Math.round(v * 100) + '%', secs: v => String(v) });
/* The editor entry `  type:{ label:…` of a shared module, as text. */
function entry(src, type) {
  const re = new RegExp('\\n  ' + type + ':\\{ label:'), m = re.exec(src); assert.ok(m, 'editor entry for ' + type);
  return literal(src, m.index + 1 + ('  ' + type + ':').length);
}
/* The worklet's params for a module: its `type:'x'` object's params literal. */
function enginePrams(src, type) {
  const i = src.indexOf("type:'" + type + "', scope:'poly'"); assert.ok(i > 0, 'worklet def for ' + type);
  const j = src.indexOf('params:', i); return ev(literal(src, src.indexOf('{', j)));
}

for (const ed of ['chains', 'waves']) {
  const src = ED[ed];
  for (const t of TYPES) {
    test(ed + ': ' + t + ' is defined with the engine\'s params, ui and an about sentence', () => {
      const e = entry(src, t), params = ev(literal(e, e.indexOf('params:') + 7));
      assert.equal(JSON.stringify(params), JSON.stringify(enginePrams(WK[ed], t)), 'params match the worklet');
      const ui = ev(literal(e, e.indexOf('ui:') + 3));
      for (const k of Object.keys(params)) assert.ok(ui[k] && ui[k].l, t + '.' + k + ' has a label');
      assert.match(e, /about:'[^']{40,}'/, 'about');
      assert.match(e, /scope:'mono'/);
    });
    test(ed + ': ' + t + ' has a palette entry, a hint, a use line, a width and a family', () => {
      assert.match(src, new RegExp("\\['" + t + "','[^']+'\\]"), 'SHARED_PALETTE');
      assert.match(src, new RegExp("\\n  " + t + ":'[^'\\n]{30,}'"), 'MODULE_USE line');
      assert.match(/const PAL_HINT = \{[^}]*\}/.exec(src)[0], new RegExp('\\b' + t + ":'[^']+'"), 'PAL_HINT');
      assert.match(src, new RegExp('MODULE_WIDTH = \\{[\\s\\S]*?\\b' + t + ':(?:NARROW_[SM]|\\d{3})'), 'MODULE_WIDTH');
      assert.match(src, new RegExp('\\b' + t + ":'(mod|util)'"), 'SHARED_FAMILY');
    });
  }
  test(ed + ': Control utilities lists them in order, after Math, in PAL_LAYOUT', () => {
    const lay = /\['Control utilities', '#9a8fb8', \[([^\]]*)\]\]/.exec(src); assert.ok(lay);
    const ids = lay[1].split(',').map(s => s.trim().replace(/'/g, ''));
    assert.deepEqual(ids.filter(t => TYPES.includes(t)), TYPES);
    assert.equal(ids.indexOf('const'), ids.indexOf('math') + 1);
    const pal = /\['Control utilities', \[([^\n]*)\], 'control'\]/.exec(src); assert.ok(pal);
    assert.deepEqual([...pal[1].matchAll(/\['(\w+)',/g)].map(m => m[1]).filter(t => TYPES.includes(t)), TYPES);
  });
  test(ed + ': port labels, Compact summary keys and the Help paragraph', () => {
    for (const [k, l] of [['gt', 'A > B'], ['lt', 'A < B'], ['inv', 'Invert'], ['sel', 'Select']]) assert.ok(src.includes(k + ":'" + l + "'"), 'port label ' + k);
    assert.match(src, /scale:\['olo', 'ohi', 'clip'\], xfade:\['mix', 'curve'\]/);
    assert.match(src, /\['Basic operations', 'Add, subtract, multiply, smaller and larger are the outputs of Math[^']*AND, OR, XOR and NOT are the outputs of Logic[^']*Invert \/ rectify[^']*Scale \/ map[^']*Comparator[^']*Constant[^']*Switch[^']*Crossfade/);
  });
  test(ed + ': the choices read as the owner named them', () => {
    const sw = entry(src, 'switch'), xf = entry(src, 'xfade'), sc = entry(src, 'scale');
    assert.match(sw, /pick:\{l:'Pick', c:opts\(\['A', 'B'\]\)\}/); assert.match(sw, /fade:\{l:'Fade', f:secs\}/);
    assert.match(xf, /c:opts\(\['Straight', 'Equal power'\]\)/); assert.match(sc, /c:opts\(\['Off', 'On'\]\)/);
  });
}
test('the SHARED EDITOR block is identical in …chains and …waves, and so is the shared worklet block', () => {
  assert.equal(block(ED.chains, 'SHARED EDITOR'), block(ED.waves, 'SHARED EDITOR'));
  assert.equal(block(WK.chains, 'SHARED MODULES'), block(WK.waves, 'SHARED MODULES'));
});
