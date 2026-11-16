// PLAYBACK NEVER CHANGES A SELECTION (build 624, owner: "Selecting upward, then pressing playback with selection on undoes the selection a phrase at a time").
// Beside Phrase, Arrange's selection lives in apSel (selA belongs to the screen drawn), so followPlay's "a selection keeps the cursor" guard did not see it and
// FOLLOW PATTERN moved the cursor (the selection's moving end) to the playing row on every change. The guard now reads both. Pinned as source text; the real-page run is opt-in.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const fp = src.slice(src.indexOf('function followPlay(){'), src.indexOf('// ARRANGE PAGES (owner, 30 Oct)'));
test('followPlay: a selection in selA or in Arrange\'s apSel returns before anything moves the cursor, but still turns Arrange\'s pages', () => {
  assert.match(fp, /if \(selA \|\| \(apF && apSel\)\)\{ arrFollowPage\(own\); return; \}/);
  const guard = fp.indexOf('apF && apSel'), moves = fp.indexOf("fch('r' + cs.song.x"), moves2 = fp.indexOf("fch('a' + cs.song.x");
  assert.ok(guard > 0 && guard < moves && guard < moves2, 'the guard comes before every follow step that sets cs.song.y');
});
