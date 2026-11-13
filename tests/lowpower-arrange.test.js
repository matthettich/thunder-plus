// LOW POWER SKINS: WHAT IS SELECTED IN ARRANGE (12 Nov, owner: "On the low power skins, we should have an easier time seeing what is selected on arrange").
// Source checks on the CSS block that follows the Low power skins' own Arrange rules (the look itself is checked by screenshots: tests/sweep/README.md, layout.mjs).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const i0 = src.indexOf('/* LOW POWER ARRANGE: WHAT IS SELECTED, WHERE THE CURSOR IS'), blkc = i0 < 0 ? '' : src.slice(i0, src.indexOf('\n', src.indexOf('b.c.p:not(#zz)', i0))), blk = blkc.replace(/\/\*[\s\S]*?\*\//g, '');
const hsl = h => { const [r, g, b] = [1, 3, 5].map(i => parseInt(h.slice(i, i + 2), 16) / 255), mx = Math.max(r, g, b), mn = Math.min(r, g, b), d = mx - mn;
  const hue = !d ? 0 : mx === r ? ((g - b) / d + 6) % 6 * 60 : mx === g ? ((b - r) / d + 2) * 60 : ((r - g) / d + 4) * 60; return { h: hue, s: !d ? 0 : d / (1 - Math.abs(mx + mn - 1)), l: (mx + mn) / 2 }; };
const lum = h => { const f = v => (v /= 255) <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); return .2126 * f(parseInt(h.slice(1, 3), 16)) + .7152 * f(parseInt(h.slice(3, 5), 16)) + .0722 * f(parseInt(h.slice(5, 7), 16)); };
const ratio = (a, b) => { const [x, y] = [lum(a), lum(b)].sort((p, q) => q - p); return (x + .05) / (y + .05); };
const vars = skin => { const m = blk.match(new RegExp(':root\\[data-skin="' + skin + '"\\]\\{([^}]*)\\}')); assert.ok(m, skin + ' has its colours'); return Object.fromEntries(m[1].split(';').map(s => s.trim().split(':')).filter(a => a[1]).map(([k, v]) => [k.trim(), v.trim()])); };

test('the block is there, for the three Low power skins, with selection, cursor and cursor-on-playing rules', () => {
  assert.ok(blk.length > 500, 'block found');
  for (const k of ['lowpower', 'lowpowerpink', 'lowpowermustard']) assert.ok(vars(k)['--lpsl'] && vars(k)['--lpcur'] && vars(k)['--lppl'], k);
  assert.match(blk, /b\.sl:not\(#zz\):not\(#zz\)\{background:var\(--lpslbg\)!important;[^}]*outline:2px dashed var\(--lpsl\); outline-offset:-1px\}/, 'selected blocks: tint, dashed outline');
  assert.match(blk, /b\.c:not\(#zz\):not\(#zz\):not\(#zz\)\{background:var\(--lpcur\)!important;[^}]*box-shadow:inset 0 0 0 2px #000000!important; outline:2px solid #ffffff; outline-offset:0\}/, 'cursor: fill, black ring inside, white outside');
  assert.match(blk, /b\.c\.p:not\(#zz\):not\(#zz\):not\(#zz\)\{box-shadow:inset 0 0 0 2px #000000, inset 0 0 0 4px #ffffff!important\}/, 'cursor on the playing block');
});
test('no layout shift, no glow, no animation: outline and inset box-shadow only', () => {
  assert.doesNotMatch(blk, /border-width|border:|transition|animation|blur|drop-shadow/);
  assert.doesNotMatch(blk.replace(/inset 0 0 0 \dpx/g, ''), /box-shadow:[^;]*[1-9]px[^;]*[1-9]px/, 'every box-shadow is an inset ring');
});
test('pink and mustard: only their own accent family (no plum, purple, violet, blue or cyan); every skin keeps a black background', () => {
  for (const k of ['lowpowerpink', 'lowpowermustard']) for (const [n, v] of Object.entries(vars(k))) { const c = hsl(v);
    if (c.s > .2 && c.l > .05) assert.ok(c.h < 75 || c.h >= 330, `${k} ${n} ${v} has hue ${Math.round(c.h)}`); }
  for (const k of ['lowpower', 'lowpowerpink', 'lowpowermustard']) assert.ok(lum(vars(k)['--lpslbg']) < .05, k + ': a selection tint is a dark colour, not a lit area');
  assert.doesNotMatch(blk, /background:#(?!000)[0-9a-f]{3,6}[;!]/i, 'no literal fill that is not black outside the variables');
});
test('colours differ: the selection ring is neither the cursor fill nor the playing ring in each skin; the selection reads on black', () => {
  for (const k of ['lowpower', 'lowpowerpink', 'lowpowermustard']) { const v = vars(k);
    assert.notEqual(v['--lpsl'].toLowerCase(), v['--lpcur'].toLowerCase()); assert.notEqual(v['--lpsl'].toLowerCase(), v['--lppl'].toLowerCase());   // (the cursor and the playing ring share pink in the mustard skin since build 568: a fill with a white ring against a ring on a dark tint)
    assert.ok(ratio(v['--lpsl'], '#000000') >= 4.5, `${k}: selection ring on black ${ratio(v['--lpsl'], '#000000').toFixed(1)}`);
    assert.ok(ratio(v['--lpcur'], '#000000') >= 4.5, `${k}: cursor fill against black ${ratio(v['--lpcur'], '#000000').toFixed(1)}`);
    assert.ok(ratio('#ffffff', v['--lpslbg']) >= 4.5, `${k}: white text on the selection tint`); assert.ok(ratio('#ffffff', v['--lppbg']) >= 4.5, `${k}: white text on the playing tint`); }
});
