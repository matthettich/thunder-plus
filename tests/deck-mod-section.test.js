// MOD SECTION IN THE BOTTOM PANEL (build 630, owner: "a mod section to the fx bar at the bottom. Collapsible like the other sections…"). Source pins: it is a section like INST / FX (DK_SECS, DK_FOLD,
// saved with the panel, starting folded), built only when open, animated only under the same gates as the MOD screen's scopes plus ANIMATIONS, and hidden on the swipe layout of a phone.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const src = require('./helpers').read('index.html');
test('a section: markup, DK_SECS, DK_FOLD, a saved fold flag that starts folded', () => {
  assert.match(src, /<section class="dks" id="dkMod" data-s="mod">[\s\S]*?id="dkModT">MOD<\/button>/);
  assert.match(src, /\['mod', 'MOD'\], \['fx', 'FX'\], \['perf', 'PERFORM'\]/); assert.match(src, /new Set\(\['inst', 'chn', 'fx', 'mod', 'perf'\]\)/); assert.match(src, /perf:true, perfOn:false, mod:false\}/);
});
test('deckMod reads the focused track\'s LFOs, builds only when open and when something changed, and goes to the MOD screen on a click', () => {
  const f = src.slice(src.indexOf('function deckMod(){'), src.indexOf('function dkModGo('));
  assert.match(f, /if \(!deck\.mod\)\{ setText\(\$\('dkModT'\), 'MOD'\); return; \}/); assert.match(f, /if \(sig === dkMd\.sig\) return/); assert.match(f, /modMix\(\[L\]/); assert.match(f, /tgtLabel\(L\.id/);
  assert.match(src, /function dkModGo\(t\)\{ cFxT = t === MT \? NT : t; go\('mod'\); \}/); assert.match(src, /deckFx\(\); deckMod\(\); \}/);
});
test('the dot: only open, playing, ANIMATIONS on, LOW POWER DISPLAY off, visible; run from the frame loop beside the MOD screen\'s', () => {
  const f = src.slice(src.indexOf('function dkModTick(){'), src.indexOf('function deckDraw(){'));
  for (const w of ['!deck.mod', '!tp.on', '!animOn', 'lpOn', 'document.hidden', '!deckShown()']) assert.ok(f.includes(w), w);
  assert.match(src, /tickDots\(\); tickLen\(\); modScTick\(\); dkModTick\(\);/);
});
test('no audio code: nothing posts to the engine; skin colours only (CSS variables); hidden on the phone\'s swipe layout', () => {
  const f = src.slice(src.indexOf('var dkMd ='), src.indexOf('function deckDraw(){')); assert.doesNotMatch(f, /corePost|rt\.|postMessage|makeSeq/);
  assert.match(src, /#dkMod\{--dkc:#e6e0f3;/); assert.match(src, /@media \(max-width:700px\) and \(pointer:coarse\)\{ #dkMod\{display:none!important\} \}/);
});
test('build 634: MOD sits before FX (markup and DK_SECS) and takes the TERM skins\' per-section tone like the others (a missing --dkp / --dkc drew its head black)', () => {
  assert.ok(src.indexOf('id="dkChn" data-s="chn"') < src.indexOf('id="dkMod" data-s="mod"') && src.indexOf('id="dkMod" data-s="mod"') < src.indexOf('id="dkFx" data-s="fx"'));
  assert.match(src, /:is\(#dkScope,#dkInst,#dkChn,#dkMod,#dkFx,#dkPerf,#dkStrip\)\{--dkc:var\(--mx\)\}/);
  assert.match(src, /#dkMod\{--dkp:10%\}/); assert.match(src, /#dkPerf\{--dkp:13%\}/);
});
