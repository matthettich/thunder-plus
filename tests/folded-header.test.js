// THE FOLDED PHONE'S HEADER (build 631, owner: "Let's make the top of the folded phone look a little less awkward"): on the Mobile setup under 600 px, the tempo line reads ROWS (no − / +),
// BPM / LPB, the time left; REC is gone from it and is File › Record (the same setRec, with its state). Unfolded Mobile and desktop are unchanged. Source pins; the browser check was 344 px, IDM and 909.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const src = require('./helpers').read('index.html');
test('CSS: order ROWS, tempo, time left; the − / + and REC buttons hidden; only below 600 px on the Mobile setup', () => {
  const m = src.match(/@media \(max-width:599px\)\{ body\.mob #tClk\{order:-2; border-right:0\} body\.mob #tRowsBar\{order:-1\}[^\n]*\}/); assert.ok(m);
  for (const w of ['body.mob #tRowsDn, body.mob #tRowsUp, body.mob #tRec{display:none}']) assert.ok(m[0].includes(w), w);
  assert.match(src, /<span class="trows" id="tRowsBar"/); assert.match(src, /\$\('tRowsV'\)\.addEventListener\('click', async \(\) => \{ const id = rowsId\(\), n = await askText/, 'tap the number to type it: already there');
});
test('applyPlatform: LEFT leaves the top bar only when folded (Mobile under 600 px)', () => {
  assert.match(src, /const fold = m && !mqUnfolded\.matches;/); assert.match(src, /if \(m && !fold && len\.parentNode !== fw\.parentNode\) fw\.before\(len\); else if \(\(!m \|\| fold\) && len\.parentNode === fw\.parentNode\) rb\.before\(len\);/);
});
test('File menu: a Record entry on the folded phone only, doing setRec(!rec.on) and saying the state', () => {
  assert.match(src, /\.\.\.\(isMobile\(\) && !mqUnfolded\.matches \? \[\{h:'Record'\}, \{l:'REC: ' \+ \(rec\.cnt > 0 \? 'counting in…' : rec\.on \? 'ON · tap to turn off' : 'OFF · tap to turn on'\), f:\(\) => setRec\(!rec\.on\)\}\] : \[\]\)/);
  assert.match(src, /\$\('tRec'\)\.addEventListener\('click', \(\) => setRec\(!rec\.on\)\)/, 'the header button did the same');
});
