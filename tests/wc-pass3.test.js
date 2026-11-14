// …waves / …chains pass 3, "Phone" (13 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: T4 T6 T7 T8 T9 T12 T13 T14, M10, follow-ups a-d).
// Source guards, no browser: Play / Edit, the touch slider rule, the long press, the section plan, scientific notation, Fit, the narrow bar,
// the keys, no audio files touched. The browser run is tests/sweep/wc-pass3.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ' M10: Play / Edit switch, remembered in the settings, Alt+E, controls only in Play', () => {
    assert.match(src, /id="modeplay"/); assert.match(src, /id="modeedit"/);
    assert.match(src, /const PLAY_CTL = /); assert.match(src, /function playBlocked\(\)/);
    assert.match(src, /settings\.mode/); assert.match(src, /setMode\(settings\.mode === 'play', true\)/);
    assert.match(src, /e\.code !== 'KeyE'/);
    assert.match(src, /body\.play \.ws::before/); assert.match(src, /body\.play \.ws::after/);
    assert.doesNotMatch(src, /doc\.mode|doc\.play/, 'the mode is not a patch field');
  });
  test(ed + ' T7: touch slider decides in the first 8 px (sideways = slider, vertical = pan), fine mode 40 px away, double-tap resets', () => {
    assert.match(src, /TOUCH_SLIDE_PX = 8/); assert.match(src, /Math\.abs\(dx\) >= Math\.abs\(dy\)/);
    assert.match(src, /Math\.abs\(ev\.clientY - y0\) > 40 \? 0\.1 : 1/);
    assert.match(src, /now - lastSlideTap\.t < 450/);
    assert.match(src, /function startPan\(pid, sx, sy\)/);
  });
  test(ed + ' T6/T4: long press 500 ms with 8 px slop opens the menu; a second finger always pinches', () => {
    assert.match(src, /LP_MS = 500, LP_SLOP = 8/); assert.match(src, /function ctxMenu\(target, x, y, o ?= ?\{\}\)/);
    assert.match(src, /nodeDragging\.stop/); assert.match(src, /touchSlideAbort/);
  });
  test(ed + ' b: touch Delete module asks with the same question; a: touch opens with the Tips pill; c: exponent in typed values; d: Fit', () => {
    assert.match(src, /askRemoveMany/); assert.match(src, /o\.touch/);
    assert.match(src, /tipOpen\(false\)/);
    assert.match(src, /\(\?:e\[\+\\-\]\?\\d\+\)\?/);
    assert.match(src, /FIT_MIN/);
  });
  test(ed + ' T8/T9/T12: Select button, bigger readout, one narrow bar with an overflow menu that holds every moved item', () => {
    assert.match(src, /id="selbtn"/); assert.match(src, /quickAddAt/); assert.match(src, /addBendAt/);
    assert.match(src, /\.row \.val\{padding:10px 0 10px 10px/);
    assert.match(src, /id="morebtn"/); assert.match(src, /id="morepop"/);
    for (const id of ['helpbtn', 'midilearn', 'selbtn', 'cpuv']) assert.ok(src.includes("'" + id + "'"), id + ' is moved, not removed');
    assert.match(src, /@media \(max-width: 640px\)\{\s*\.bar\{flex-wrap:nowrap; gap:6px; padding:6px 8px; min-width:0; z-index:30\}/);
  });
  test(ed + ' T14: sections live in the view (optional view.sec), never in a module', () => {
    assert.match(src, /function secPlan\(/); assert.match(src, /view\(\)\.sec/);
    assert.match(src, /SEC_MIN = 16, SEC_CHUNK = 12/);
    assert.doesNotMatch(src, /m\.sec\b|m\.params\.sec\b/);
  });
}
test('chains: the host Fit is the whole-patch fit, not the opening view', () => {
  const m = /fit\(\)\{[^\n]*\n/.exec(ED.chains.slice(ED.chains.indexOf('window.chainsEmbed = {')));
  assert.ok(m && /fitAll\(1\)/.test(m[0]) && !/openView/.test(m[0]));
});
test('waves T13: on-screen keys: 12 notes through engNote, octave buttons', () => {
  assert.match(ED.waves, /id="octdn"/); assert.match(ED.waves, /id="octup"/); assert.match(ED.waves, /touchkeys/); assert.match(ED.waves, /function setOct\(/);
  assert.match(ED.waves, /engNote\(/);
});
test('the editors still share one SHARED EDITOR block', () => {
  const b = s => s.slice(s.indexOf('===SHARED EDITOR START==='), s.indexOf('===SHARED EDITOR END==='));
  assert.equal(b(ED.chains), b(ED.waves));
});
