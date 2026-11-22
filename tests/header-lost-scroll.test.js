// HEADER LOST (21 Nov, build 662): the document / body scrolled from code (focus, keyboard, notification) left the header off the top. Pins the safety net.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('node:vm');
const H = require('./helpers');
const src = H.read('index.html');
const fn = src.match(/function resetStrayScroll\(\)\{[\s\S]*?\n\}\n/);
test('resetStrayScroll puts html, body and the window back at 0,0', () => {
  assert.ok(fn, 'resetStrayScroll exists');
  const el = () => ({ scrollTop: 120, scrollLeft: 4 }), de = el(), bd = el(); let to = null;
  const ctx = { document: { scrollingElement: de, documentElement: de, body: bd }, scrollX: 0, scrollY: 120, scrollTo: (x, y) => { to = [x, y]; } };
  vm.runInNewContext(fn[0] + ';resetStrayScroll()', ctx);
  assert.deepEqual([de.scrollTop, de.scrollLeft, bd.scrollTop, bd.scrollLeft], [0, 0, 0, 0]); assert.deepEqual(to, [0, 0]);
});
test('it runs on return to the app, resize, visual viewport and scroll, and the return path re-measures like resize', () => {
  for (const re of [/addEventListener\('scroll', resetStrayScroll/, /addEventListener\('resize', resetStrayScroll/, /addEventListener\('pageshow', remeasureAfterReturn/, /addEventListener\('focus', remeasureAfterReturn/,
    /visualViewport\.addEventListener\('resize', resetStrayScroll/, /document\.addEventListener\('visibilitychange', \(\) => \{ if \(!document\.hidden\)\{ remeasureAfterReturn/]) assert.match(src, re);
  assert.match(src, /function remeasureAfterReturn\(\)\{ resetStrayScroll\(\); scheduleRefit\(true\); \}/);
});
test('build and service-worker cache agree at 662', () => {
  assert.match(src, /const BUILD = 662;/); assert.match(H.read('sw.js'), /thunderplus-v662/);
});
