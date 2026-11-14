// CONTROL BAR STOP (14 Nov, owner: "make the control bar stop button look like the skin"). Source-level pins: the control bar's STOP (".tcmd button[data-c=song]", lit while playing) takes its look
// from the skin: no rule that is not a skin's own (:root[data-skin...]) gives it a colour or a shadow of its own; only the pad's PLAY key keeps the amber of 9 Nov. It is lit by .tcmd button.on /
// .tcmd.cmini button:is(.on,.dn), as the other control bar buttons.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const css = [...src.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(m => m[1]).join('\n');   // every style block, in order
const rules = []; css.replace(/\/\*[\s\S]*?\*\//g, '').replace(/([^{}]+)\{([^{}]*)\}/g, (_, sel, body) => { rules.push([sel.trim(), body.trim()]); return ''; });
const rule = re => { const r = rules.filter(([s]) => re.test(s)); assert.ok(r.length, 'no rule for ' + re); return r; };
const LITERAL = /#[0-9a-f]{3,8}\b|rgba?\(|hsla?\(|oklch\(|\b(?:white|black|orange|yellow)\b/i;


test('the control bar STOP has no colour or shadow of its own outside a skin', () => {
  const mine = rules.filter(([s]) => /\.tcmd[^,{]*\[data-c=song\]/.test(s) && !/data-skin/.test(s));
  assert.ok(mine.length >= 2, 'the control bar STOP rules are there');
  for (const [s, b] of mine) assert.doesNotMatch(b, LITERAL, 'a literal colour in: ' + s), assert.doesNotMatch(b, /box-shadow/, 'a shadow in: ' + s);
  // the amber, 4 px border and hard shadow are the PAD's PLAY key only
  const amber = rules.filter(([, b]) => /#ffc23d/i.test(b));
  assert.equal(amber.length, 1); assert.match(amber[0][0], /^#tPad #kPlay\.on/); assert.doesNotMatch(amber[0][0], /\.tcmd|data-c=song/);
});
test('the control bar STOP is lit by the bar\'s own on state, in the thin bar and the grid', () => {
  assert.ok(rule(/^\.tcmd button\.on$/).some(([, b]) => /background:var\(--pl\)/.test(b) && /color:var\(--inv\)/.test(b)));
  assert.ok(rule(/\.tcmd\.cmini button:is\(\.on,\.dn\)/).some(([, b]) => /background:var\(--ink\)!important/.test(b) && /color:var\(--inv\)/.test(b)));
  assert.ok(rule(/^\.tcmd\.cmini button,/).some(([, b]) => /box-shadow:none/.test(b)), 'the thin bar\'s buttons are flat');
  assert.match(src, /sb\.classList\.toggle\('on', tp\.on\)/, 'the song button still gets "on" while playing (what it does is unchanged)');
});
test('the skins\' own PLAYING accents still reach the control bar STOP (IDM, IDM pink, IDM pink & green)', () => {
  for (const k of ['idm', 'idmpink', 'idmpg']) assert.ok(rules.some(([s, b]) => s.includes('data-skin="' + k + '"') && s.includes('.tcmd button[data-c=song]') && /background:/.test(b)), k);
});

