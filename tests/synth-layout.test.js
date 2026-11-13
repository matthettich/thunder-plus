// SYNTH UX, Phase A (13 Nov): layout and density of the Synth page. Static pins on index.html (no browser): the engine picker sits
// above the waveform, the folds (Pad, Sequence, Tuning) keep their state under one key, the tuning sliders obey the mouse rules.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');

test('synth layout: the engine picker comes before the waveform in the markup, right after the name row', () => {
  const i = src.indexOf('id="vEngine"'), n = src.indexOf('id="vName"'), w = src.indexOf('id="wave"');
  assert.ok(n > 0 && i > n && w > i, 'name row, then engine picker, then waveform');
  assert.ok(src.indexOf('id="vEngLbl"') > i && src.indexOf('id="vEngLbl"') < w, 'the short engine line stays with the picker');
  assert.match(src, /\.wave\{width:100%; height:84px/, 'the waveform is 84px');
  assert.match(src, /\.top button\{padding-inline:8px\}\n  \.wave\{height:64px\}/, '64px on a phone');
});

test('synth layout: the folds share one saved object and are real buttons', () => {
  assert.match(src, /const SYNFOLD_KEY = 'thunderplus-synfold-v1'/, 'the fold key');
  assert.match(src, /setAttribute\('aria-expanded', open \? 'true' : 'false'\)/, 'aria-expanded');
  for (const id of ['pad', 'seq', 'tuning']) assert.match(src, new RegExp(`makeFold\\('${id}'`), 'fold ' + id);
  assert.match(src, /padFold\.body\.append\(r\.r\)|\? \$\('vRows'\) : padFold\.body\)\.append/, 'Drive, Gate, Max len live in the Pad fold');
  assert.match(src, /makeRow\(VP\[k\], k, V, \{idp:'v'\}\)/, 'the pad rows are still makeRow rows with their r_v ids');
});

test('synth layout: the tuning sliders go through makeRow, the Sequence steps carry _reset', () => {
  assert.match(src, /makeRow\(\{l:NOTE\[pc\][^\n]*\{idp:'tn', noTrack:true, on:\(\) => \{ saveS\(\); changed\(\); \}\}\)/, 'tuning rows: same handler as before');
  assert.doesNotMatch(src, /tuneUI/, 'the plain-input list is gone');
  assert.match(src, /inp\._reset = stepReset/, 'step sliders reset by Alt-click');
});

test('synth layout: one 104px label column on the Synth page, labels wrap instead of ending in an ellipsis', () => {
  assert.match(src, /:root\{--lblw:104px\}/);
  assert.match(src, /:is\(#main, #synRight\) \.row label\{white-space:normal; overflow:visible; text-overflow:clip/);
  assert.match(src, /:is\(#main, #synRight\) \.readout\{padding-left:calc\(var\(--lblw\) \+ 6px\)\}/);
  assert.match(src, /\.secs\{display:grid; grid-template-columns:repeat\(auto-fill,minmax\(min\(100%,322px\),1fr\)\)/);
  assert.match(src, /\.dkrows \.row|\.dkcg \.row|\.row\{display:grid; grid-template-columns:70px minmax\(0,1fr\) 64px/, 'the plain .row keeps its 70px for the deck and Settings');
});
