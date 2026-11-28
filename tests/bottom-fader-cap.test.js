// BOTTOM-PANEL FADER CAP (14 Nov, owner: "a line at the top, rather than a round top, so it matches the mixer"). Source-level pins: the TRACK strip's fader (.stcap, also the bus and master strips,
// one element) is the Mixer's flat bar (.mfbar): a line, not a round knob; the skin's ink with a halo of its ground; no literal colour; the fader's behaviour code is untouched.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const css = [...src.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(m => m[1]).join('\n');   // every style block, in order
const rules = []; css.replace(/\/\*[\s\S]*?\*\//g, '').replace(/([^{}]+)\{([^{}]*)\}/g, (_, sel, body) => { rules.push([sel.trim(), body.trim()]); return ''; });
const rule = re => { const r = rules.filter(([s]) => re.test(s)); assert.ok(r.length, 'no rule for ' + re); return r; };
const LITERAL = /#[0-9a-f]{3,8}\b|rgba?\(|hsla?\(|oklch\(|\b(?:white|black|orange|yellow)\b/i;


test('the bottom panel fader cap is a flat bar like the Mixer\'s .mfbar, not a round knob', () => {
  const cap = rule(/^\.stcap$/)[0][1], bar = rule(/^\.mfbar$/)[0][1];
  assert.doesNotMatch(cap, /border-radius:\s*50%/); assert.doesNotMatch(cap, LITERAL, 'no literal colour');
  const get = (b, p) => (b.match(new RegExp('(?:^|;)\\s*' + p + ':\\s*([^;]+)')) || [])[1];
  assert.equal(get(cap, 'height'), get(bar, 'height'), 'the same 3 px line');
  assert.equal(get(cap, 'border-radius'), get(bar, 'border-radius'));
  assert.ok(parseInt(get(cap, 'width')) > 3 * parseInt(get(cap, 'height')), 'wide, not square');
  assert.equal(get(cap, 'background'), 'var(--ink)');   // the Mixer's is --ts-ink (the tracker's ink); the panel has no tracker variables, its ink is --ink
  assert.match(get(cap, 'box-shadow'), /^0 0 0 1px var\(--panel\)$/); assert.match(get(bar, 'box-shadow'), /^0 0 0 1px var\(--ts-bg\)$/, 'the same 1 px halo of the ground');
  const hov = rule(/\.stfad:is\(:hover,:active,:focus-visible\) \.stcap/)[0][1]; assert.match(hov, /height:4px/, 'a pixel thicker on hover, as .mfbar:hover');
});
test('the fader\'s behaviour code is untouched: the cap is still positioned with style.bottom at stPos, the drag is on the strip', () => {
  assert.match(src, /stVol\.querySelector\('\.stcap'\)\.style\.bottom = fy \+ 'px'/);
  assert.match(src, /const stPos = v => \{ const h = stVol\.clientHeight - 16; return 8 \+ v \/ 255 \* h; \};/);
  assert.match(src, /stVol\.addEventListener\('dblclick', stVolType\)/); assert.match(src, /stVol\.dataset\.wheel = ''; stVol\._wheel = /); assert.match(src, /stVol\.addEventListener\('contextmenu', e => valMenu\(/);
  assert.equal(rules.filter(([s]) => /\.stcap/.test(s)).every(([, b]) => !/pointer-events/.test(b)), true);
});
