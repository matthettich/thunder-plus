// ANIMATIONS ON / OFF (14 Nov, owner: wants to turn the dancing dogs and the other animations off). Source pins always; real-page run (computer and 344x780) opt-in: TP_BROWSER_TESTS=1.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
test('source: one saved flag, a Settings row with an info line, and the gates on each decoration', () => {
  assert.match(src, /var animOn = \(\(\) => \{ try \{ return localStorage\.getItem\('thunderplus-anim-v1'\) !== '0'; \}/, 'default ON, saved in localStorage');
  assert.match(src, /\{l:'ANIMATIONS', ck:[^\n]*set:v => setAnim\(!!v\)[^\n]*\n\s*info:\(\) => /);
  assert.match(src, /const stageKind = \(\) => \{ const k = stageKind0\(\); return animOn \|\|/, 'no dogs when off');
  assert.match(src, /\|\| !tp\.on \|\| !ctx \|\| !animOn\) return;/, 'the dogs\' beat work does not run');
  assert.match(src, /function dkOk\(\)\{ return animOn && /, 'the drum hits neither');
  assert.match(src, /if \(!animOn\)\{\}[^\n]*\n\s*else if \(isSpringFx\(f\)\)/, 'no effect pictures on the FX bar');
  assert.match(src, /const sig = \(animOn \? 'a' : 'n'\) \+ t/, 'the bar rebuilds when it flips');
  assert.match(src, /animOn === false \|\| matchMedia\('\(prefers-reduced-motion: reduce\)'\)\.matches\) return;\s*\n\s*rbT0 = /, 'the rainbow skin holds still');
  assert.match(src, /\{l:'Hide animations', f:\(\) => setAnim\(false, true\)\}/, 'the dogs\' and the card\'s menus');
});
test('source: functional displays are not gated (meters, scopes, piano, playhead)', () => {
  const t = src.match(/function stageTick\(now\)\{[\s\S]*?\n\}\n/)[0];
  assert.doesNotMatch(t.split("if ((stgKindNow !== 'dogs'")[0], /animOn/, 'piano / spec / chain frames come before the dogs gate and ignore it');
});
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const DECK = JSON.stringify({ on: true, scope: true, inst: true, chn: true, fx: true, strip: true, h: 0, ws: 300, wf: 320, fxbar: true, perf: true, perfOn: false });
const opt = { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 };
for (const [name, o] of [['computer 1440x900', { width: 1440, height: 900, storage: { 'thunderplus-deck-v1': DECK } }],
  ['phone 344x780', { width: 344, height: 780, mobile: true, storage: { 'thunderplus-platform-v1': 'mobile', 'thunderplus-input-v1': 'keys', 'thunderplus-keyspad-v1': '0', 'thunderplus-deck-v1': DECK } }]]) {
  test('in a real page (' + name + '): OFF removes the dogs and the effect pictures and keeps the meters; ON brings them back; it is saved', opt, async () => {
    process.env.PLAYWRIGHT = PW;
    const { boot, app } = await import('./sweep/ui-lib.mjs');
    const B = await boot(o);
    const st = () => app(B.page, `JSON.stringify({dogs: !$('tStage').hidden && !$('tStageDogs').hidden, fspr: document.querySelectorAll('#dkFxBar .fspr').length, fm: document.querySelectorAll('#dkFxBar .fm i').length, saved: localStorage.getItem('thunderplus-anim-v1')})`).then(JSON.parse);
    try {
      await app(B.page, `(S.tfx[0][0] = freshTfx('plate'), cTrack = 0, cs.song.x = 0, go('song'), setStage('dogs'), draw(), 1)`); await B.page.waitForTimeout(700);
      const a = await st(); assert.equal(a.fspr, 1, 'the room picture is there'); assert.ok(a.fm >= 1);
      await app(B.page, 'setAnim(false), 1'); await B.page.waitForTimeout(500);
      const b = await st(); assert.equal(b.dogs, false); assert.equal(b.fspr, 0); assert.equal(b.fm, a.fm, 'the level meter stays'); assert.equal(b.saved, '0');
      await app(B.page, 'setAnim(true), 1'); await B.page.waitForTimeout(500);
      const c = await st(); assert.equal(c.fspr, 1); assert.equal(c.saved, '1');
    } finally { await B.close(); }
  });
}
