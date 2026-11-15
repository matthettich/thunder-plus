// SIDE PANEL TAKES THE SKIN (15 Nov, owner: "make the sidebar with the meters and instruments match the skin", "an awkward grey strip at the bottom if the command bar is on"). Source-level pins:
// the .tside ground is the screen's own (--panel, not the paler --side, which left a band under it beside the command bar), its cards' offset block is the skin's light, not white, the playing flash is the skin's accent, the text is the tracker's font.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const css = [...src.matchAll(/<style>([\s\S]*?)<\/style>/g)].map(m => m[1]).join('\n');
const rules = []; css.replace(/\/\*[\s\S]*?\*\//g, '').replace(/([^{}]+)\{([^{}]*)\}/g, (_, sel, body) => { rules.push([sel.trim(), body.trim()]); return ''; });
const rule = re => { const r = rules.filter(([s]) => re.test(s)); assert.ok(r.length, 'no rule for ' + re); return r; };
const get = (b, p) => (b.match(new RegExp('(?:^|;)\\s*' + p + ':\\s*([^;]+)')) || [])[1];

test('the side panel\'s ground is the screen\'s (--panel), as the command bar\'s, so no band shows under it', () => {
  const side = rule(/^\.tside$/).map(([, b]) => get(b, 'background')).filter(Boolean);
  assert.deepEqual(side, ['var(--panel)']);
  assert.equal(get(rule(/^\.tcmd\.cmini$/)[0][1], 'background'), 'var(--panel)');
});
test('the side panel\'s cards and instrument list take the skin\'s variables, no literal white or orange', () => {
  const card = rule(/^\.tside \.tcard$/)[0][1];
  assert.match(get(card, 'box-shadow'), /^4px 4px 0 0 var\(--inv, #fff\), 4px 4px 0 1\.5px var\(--line\)$/);
  assert.equal(get(card, 'background'), 'var(--panel)'); assert.equal(get(card, 'color'), 'var(--ink)');
  const hit = rule(/^\.tinl button\.hit$/)[0][1];
  assert.doesNotMatch(hit, /#fff\b|#ff5a1f\s*!important/); assert.match(hit, /var\(--acc, #ff5a1f\)/); assert.match(hit, /var\(--acc-on, #fff\)/);
  for (const re of [/^\.mtr$/, /^\.tside \.tinl button$/, /^\.tside \.tinlh$/]) assert.doesNotMatch(rule(re).map(r => r[1]).join(';'), /#[0-9a-f]{3,8}\b|rgba?\(/i);
});
test('the side panel is set in the tracker\'s font, over the skin\'s own button font', () => {
  assert.equal(get(rule(/^:root \.tside, :root \.tside :is\(button, select, input\)$/)[0][1], 'font-family'), 'var(--mono)');
});
