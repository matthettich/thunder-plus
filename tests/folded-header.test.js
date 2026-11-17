// THE FOLDED PHONE'S HEADER (build 631, owner: "Let's make the top of the folded phone look a little less awkward"): on the Mobile setup under 600 px, ROWS has no − / +, and REC is gone from the
// line and is File › Record (the same setRec, with its state). Build 636 (owner, 16 Nov): BPM / LPB, ROWS and LEFT moved up into the top bar after the CPU meter (the same elements, in that order), so the
// line under the tabs is gone. Unfolded Mobile and desktop are unchanged. Source pins; the browser check was 344 px (and 320 / 360 / 400 / 599) in IDM, IDM pink, IDM pink & green, 909 and Low power.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const src = require('./helpers').read('index.html');
test('CSS: order ROWS, tempo, time left; the − / + and REC buttons hidden; only below 600 px on the Mobile setup', () => {
  const m = src.match(/@media \(max-width:599px\)\{ body\.mob #tClk\{order:-2; border-right:0\} body\.mob #tRowsBar\{order:-1\}[^\n]*\}/); assert.ok(m);
  for (const w of ['body.mob #tRowsDn, body.mob #tRowsUp, body.mob #tRec{display:none}']) assert.ok(m[0].includes(w), w);
  assert.match(src, /<span class="trows" id="tRowsBar"/); assert.match(src, /\$\('tRowsV'\)\.addEventListener\('click', async \(\) => \{ const id = rowsId\(\), n = await askText/, 'tap the number to type it: already there');
});
test('applyPlatform: fold is Mobile under 600 px (the same condition as the CSS)', () => {
  assert.match(src, /const fold = m && !mqUnfolded\.matches;/);
});
test('File menu: a Record entry on the folded phone only, doing setRec(!rec.on) and saying the state', () => {
  assert.match(src, /\.\.\.\(isMobile\(\) && !mqUnfolded\.matches \? \[\{h:'Record'\}, \{l:'REC: ' \+ \(rec\.cnt > 0 \? 'counting in…' : rec\.on \? 'ON · tap to turn off' : 'OFF · tap to turn on'\), f:\(\) => setRec\(!rec\.on\)\}\] : \[\]\)/);
  assert.match(src, /\$\('tRec'\)\.addEventListener\('click', \(\) => setRec\(!rec\.on\)\)/, 'the header button did the same');
});

test('build 636: applyPlatform moves #tClk, #tRowsBar, #tLen into the top bar after #tCpu only when folded, and puts them back otherwise', () => {
  assert.match(src, /if \(fold\)\{ if \(ck\.parentNode !== cp\.parentNode\)\{ cp\.after\(ck\); ck\.after\(rw\); rw\.after\(len\); \} \}/, 'order in the top bar: CPU, BPM / LPB, ROWS, LEFT');
  assert.match(src, /if \(ck\.parentNode === cp\.parentNode\)\{ tpb\.prepend\(ck\); ck\.after\(rw\); rb\.before\(len\); \}/, 'unfolded / desktop: back on the line, BPM / LPB, ROWS, LEFT, then REC');
  assert.match(src, /if \(m && len\.parentNode !== fw\.parentNode\) fw\.before\(len\); else if \(!m && len\.parentNode === fw\.parentNode\) rb\.before\(len\);/, 'unfolded Mobile still has LEFT left of File');
  for (const id of ['tClk', 'tRowsBar', 'tLen', 'tDots']) assert.equal((src.match(new RegExp('id="' + id + '"', 'g')) || []).length, 1, id + ' exists once: moved, never copied');
});
test('build 636 CSS: the top-bar rules are inside max-width:599px and body.mob only; the empty line is collapsed but not display:none', () => {
  const i = src.indexOf('/* FOLDED PHONE, ONE TOP ROW (build 636'); assert.ok(i > 0);
  const j = src.indexOf('\n}\n', i), blk = src.slice(i, j + 3), at = blk.indexOf('@media (max-width:599px){'); assert.ok(at > 0, 'one media block');
  const rules = blk.slice(at + '@media (max-width:599px){'.length).split('\n').map(l => l.trim()).filter(l => l && !l.startsWith('/*'));
  assert.ok(rules.length > 15);
  for (const r of rules.filter(x => !x.startsWith('}'))) assert.match(r, /^body\.mob/, 'every selector starts with body.mob: ' + r.slice(0, 60));
  assert.ok(blk.includes('body.mob .top #tClk, body.mob .top #tRowsBar{order:0}'), 'the 631 order:-2 / -1 are for the line; in the top bar the order is the DOM order');
  assert.ok(blk.includes('body.mob:not(.trkv) .top #tClk, body.mob:not(.trkv) .top #tRowsBar{display:none}'), 'not on the Synth page');
  assert.ok(blk.includes('body.mob.trkv .top:not(:has(#segView button:nth-child(3))){flex-wrap:nowrap}'), 'Tracker / Synth only: one row');
  assert.ok(!/body\.mob\.trkv \.top\{[^}]*nowrap/.test(blk), 'with Chains / Waves shown (three or four tabs) the switch keeps its own row and the rest wrap under it; the squeeze is only for two tabs');
  assert.ok(blk.includes('body.mob.trkv .top:not(:has(#segView button:nth-child(3))) #segView button{'), 'the narrower tabs, File, Settings, Play and CPU are for the two-tab bar only');
  assert.match(blk, /body\.mob\.inpad \.ttabbar \.tpat\{width:0; height:0; overflow:hidden; border:0; padding:0\}/, 'no height, no border when empty');
  assert.doesNotMatch(blk, /\.tpat\{[^}]*display:none/, 'display:none would make patFit() return early (offsetParent) and PROJ would not shorten');
  assert.match(blk, /body\.mob\.inpad \.ttabbar \.ttabs\{border-bottom:0\}/, 'one bottom line, not two');
  assert.match(blk, /#tDots\{grid-area:3 \/ 1 \/ 4 \/ 3;/, 'the tick dots are a thin strip under BPM / LPB');
  assert.match(blk, /#tClk \.tcin\{width:3ch;/, 'the typing box is no wider than the digits');
});
