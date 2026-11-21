// SAVE BEFORE PLAY (12 Nov, owner: notes typed into a new pattern were gone after PLAY hung). A change still waiting for the 600 ms autosave is
// written at the top of tStart, before the audio engine starts. Source checks: the app has no module to load under node.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const src = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
test('both autosave timers mark a save as pending and clear the mark when they write', () => {
  const m = src.match(/sPend = true; sT = setTimeout\(\(\) => \{ sPend = false; storeWrite\(TK, JSON\.stringify\(songOut\(S\)\)\); \}, 600\);/g);
  assert.equal(m && m.length, 2);
  assert.doesNotMatch(src, /sT = setTimeout\(\(\) => storeWrite\(TK/);
});
test('flushS clears the mark; tStart writes a pending change first, before the warm-up', () => {
  assert.match(src, /function flushS\(\)\{ clearTimeout\(sT\); sPend = false; storeWrite\(TK, JSON\.stringify\(songOut\(S\)\)\); \}/);
  const i = src.indexOf('function tStart(mode, o){'), j = src.indexOf('if (sPend && !tp.on && !(o && o._warm)) try { flushS(); } catch(e){}', i), k = src.indexOf('rtInit()', i);
  assert.ok(i > 0 && j > i && k > j, 'the flush comes before the audio engine is started');
});
