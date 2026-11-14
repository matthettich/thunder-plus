// …waves / …chains pass 1a, Compact collapsed modules (13 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md, C1 to C9).
// Source guards: both editors carry the summary-key table (default and per-module override), the legacy collapsed:true path,
// the same Compact functions, no audio code touched, and the pieces of the look use the skin variables. The browser run is
// tests/sweep/wc-compact.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const fn = (src, name) => { const m = src.match(new RegExp('function ' + name + '\\([^)]*\\)\\s*\\{[\\s\\S]*?\\n\\}\\n')); assert.ok(m, 'function ' + name + ' not found'); return m[0]; };

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ': the summary keys live in one small table, with the first select + first two sliders as the default', () => {
    assert.match(src, /const SUMMARY_KEYS = \{\n[^}]*\n\};/);
    assert.match(src, /const SUMMARY_DEFAULT = \{ selects:1, sliders:2 \};/);
    assert.match(src, /const own = def\.key \|\| SUMMARY_KEYS\[m\.type\];/);
    assert.match(src, /buildSummary\(n\);/, 'buildNode builds the summary line');
  });
  test(ed + ': an old patch with collapsed:true opens Compact (m.collapsed stays the only saved state; nothing new is written)', () => {
    assert.match(src, /if \(m\.collapsed\) setCollapsed\(n, true, true\);/);
    const sc = fn(src, 'setCollapsed');
    assert.match(sc, /n\.m\.collapsed = !!c;/);
    assert.doesNotMatch(sc, /\.view\s*=/, 'no new saved field');
    assert.doesNotMatch(src, /m\.view\s*=\s*'(compact|mini)'/);
  });
  test(ed + ': the chevron label flips with the state (C9), a title-bar double-tap is counted by hand (C5), collapse all is one undo step (C4)', () => {
    assert.match(fn(src, 'foldLabel'), /\(c \? 'Expand ' : 'Collapse '\) \+ t/);
    assert.match(src, /headTap\(n, ev\);/);
    assert.match(src, /@media \(pointer: coarse\)\{ \.node > header \.fold\{position:relative\} \.node > header \.fold::before\{content:''; position:absolute; inset:-9px -10px\} \}/);
    const all = fn(src, 'setAllCollapsed');
    assert.equal((all.match(/store\.snapshot\(\)/g) || []).length, 1, 'one snapshot for the whole batch');
    assert.match(src, /'Collapse all modules \(Alt\+C\)'/); assert.match(src, /Expand all modules/);
  });
  test(ed + ': cables into a collapsed module end on a stub or the title bar edge; text is written only when it changes', () => {
    const jp = fn(src, 'jackPos');
    assert.match(jp, /n\.stubs\[port\.slice\(2\)\]\.getBoundingClientRect\(\)/);
    assert.match(jp, /dir === 'in' \? hr\.left : hr\.right/);
    assert.match(fn(src, 'paintSum'), /if \(t !== it\.t\)\{ it\.t = t; it\.v\.textContent = t;/);
    assert.match(fn(src, 'setLvl'), /if \(w !== L\.shown\)/);
    assert.match(fn(src, 'paintMarks'), /if \(k === n\.mkKey\) return;/);
  });
  test(ed + ': the new CSS takes its colours from the skin variables (so the IDM and 909 skins restyle it) and adds none of its own', () => {
    const css = src.slice(src.indexOf('/* COMPACT (13 Nov'), src.indexOf('@media (pointer: coarse){ .node > header .fold{position:relative}'));
    assert.ok(css.length > 800, 'COMPACT css block found');
    assert.doesNotMatch(css, /#[0-9a-fA-F]{3,6}\b/, 'no literal colours in the Compact css');
    for (const v of ['--mod', '--audio', '--muted', '--ink', '--edge', '--gate']) assert.ok(css.includes('var(' + v), v + ' used');
  });
}

test('the two editors keep the Compact functions in step (everything but the chains-only bottom-panel dot and I/O title)', () => {
  for (const f of ['setCollapsed', 'foldLabel', 'foldToggle', 'headTap', 'buildSummary', 'paintSum', 'paintMarks', 'setLvl', 'meterSet', 'cableUse', 'stubJack', 'compactApply', 'compactAll', 'compactMidi', 'setAllCollapsed', 'jackPos'])
    assert.equal(fn(ED.chains, f), fn(ED.waves, f), f + ' differs between chains and waves');
  const css = s => s.slice(s.indexOf('/* COMPACT (13 Nov'), s.indexOf('@media (pointer: coarse){ .node > header .fold{position:relative}'));
  assert.equal(css(ED.chains), css(ED.waves), 'Compact css differs');
});

test('the Compact summary-key default and override (run on the shared function)', () => {
  const blk = ED.chains.match(/const SUMMARY_KEYS = \{[\s\S]*?\nfunction summaryKeys\(m\)\{[\s\S]*?\n\}\n/)[0];
  const MODULES = {
    delay: { params: { mode: [0, 0, 1, 1], time: [.3, 0, 1, .01], regen: [.4, 0, 1, .01], mix: [.2, 0, 1, .01], _byp: [0, 0, 1, 1] }, ui: { mode: { c: [[0, 'A'], [1, 'B']] } } },
    plain: { params: { a: [0, 0, 1, .1], b: [0, 0, 1, .1], c: [0, 0, 1, .1] }, ui: {} },
    wet: { params: { sel: [0, 0, 1, 1], a: [0, 0, 1, .1], b: [0, 0, 1, .1], gain: [1, 0, 2, .1], mix: [1, 0, 1, .1], out: [1, 0, 1, .1] }, ui: { sel: { c: [[0, 'p']] }, gain: { l: 'Fold' }, mix: { l: 'Mix' }, out: { l: 'Output' } } },
    lvl: { params: { a: [0, 0, 1, .1], b: [0, 0, 1, .1], c: [0, 0, 1, .1] }, ui: { c: { l: 'Level' } } },
    hid: { params: { sel: [0, 0, 1, 1], x: [0, 0, 1, .1], y: [0, 0, 1, .1], z: [0, 0, 1, .1] }, ui: { sel: { c: [[0, 'p']] }, x: { hide: true } } },
    own: { key: ['mix', 'nope', 'time', 'regen', 'mode'], params: { mode: [0, 0, 1, 1], time: [.3, 0, 1, .01], regen: [.4, 0, 1, .01], mix: [.2, 0, 1, .01] }, ui: { mode: { c: [[0, 'A']] } } },
  };
  const ctx = vm.createContext({ MODULES, choicesOf: (t, k) => (MODULES[t].ui[k] || {}).c || null, mixShown: () => true });
  vm.runInContext(blk + '\nthis.summaryKeys = summaryKeys; this.SUMMARY_KEYS = SUMMARY_KEYS;', ctx);
  const sk = t => Array.from(ctx.summaryKeys({ type: t, params: {} }));
  assert.deepEqual(sk('delay'), ['mode', 'mix', 'time'], 'first select, then the two sliders: Mix first (build 583), then the first slider, skipping _byp');
  assert.deepEqual(sk('wet'), ['sel', 'mix', 'out'], 'Mix, then the output level take the two slider places; a control labelled Fold is not a gain');
  assert.deepEqual(sk('lvl'), ['c', 'a'], 'a Level slider goes first, then the first slider');
  assert.deepEqual(sk('plain'), ['a', 'b'], 'no select: the first two sliders');
  assert.deepEqual(sk('hid'), ['sel', 'y', 'z'], 'a hidden control is skipped');
  assert.deepEqual(sk('own'), ['mix', 'time', 'regen'], 'a key list on the definition wins, unknown names go, three at most');
  assert.deepEqual(Array.from(ctx.SUMMARY_KEYS.smixer), ['level', 'l1'], 'the Stereo mixer leads with its master level');
  assert.deepEqual(Array.from(ctx.SUMMARY_KEYS.mixer), ['level', 'l1'], 'and so does the Mixer');
  assert.ok(/const SUMMARY_FIRST = \[/.test(blk), 'the preferred labels are one small list');
  ctx.SUMMARY_KEYS.delay = ['mix', 'time'];
  assert.deepEqual(sk('delay'), ['mix', 'time'], 'a line in SUMMARY_KEYS overrides the default');
});
