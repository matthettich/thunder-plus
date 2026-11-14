// MASTER-COLUMN BLOCK MUTES (12 Nov, owner: "Make m.pat mute patterns on master column"): UI and storage only; the sequencer still ignores them.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const src = require('./helpers').read('index.html');
const grab = re => { const m = src.match(re); assert.ok(m, 'not found: ' + re); return m[0]; };
const code = grab(/const masterBlockMuted = [^\n]*\n/) + grab(/function setMasterBlockMute\(y, on\)\{[\s\S]*?\n\}\n/) + grab(/function smuteInsertRows\(at, h\)\{[\s\S]*?\n\}\n/) + grab(/function smuteRemoveRows\(at, h\)\{[\s\S]*?\n\}\n/);
const rig = () => { const c = { MT: 32, S: { smute: new Array(256).fill(0) } }; vm.createContext(c); vm.runInContext(code + '\nthis.f = {masterBlockMuted, setMasterBlockMute, smuteInsertRows, smuteRemoveRows};', c); return c; };
test('masterBlockMuted(S, y) is pure: reads S.smuteM only; false for a song without it', () => {
  const c = rig(); assert.equal(c.f.masterBlockMuted(c.S, 3), false); assert.equal(c.f.masterBlockMuted({}, 3), false); assert.equal(c.f.masterBlockMuted(null, 3), false);
  c.f.setMasterBlockMute(3, true); assert.equal(c.f.masterBlockMuted(c.S, 3), true); assert.equal(c.f.masterBlockMuted(c.S, 4), false);
});
test('stored as a sorted row list; the key is gone again when empty (so old songs save as before); smute is untouched', () => {
  const c = rig(); c.f.setMasterBlockMute(9, true); c.f.setMasterBlockMute(2, true); c.f.setMasterBlockMute(9, true);
  assert.equal(JSON.stringify(c.S.smuteM), '[2,9]'); assert.ok(c.S.smute.every(v => v === 0));
  c.f.setMasterBlockMute(2, false); c.f.setMasterBlockMute(9, false); assert.ok(!('smuteM' in c.S));
});
test('rows inserted or removed carry the mutes with them', () => {
  const c = rig(); c.f.setMasterBlockMute(1, true); c.f.setMasterBlockMute(5, true); c.f.smuteInsertRows(2, 1); assert.equal(JSON.stringify(c.S.smuteM), '[1,6]');
  c.f.smuteRemoveRows(1, 1); assert.equal(JSON.stringify(c.S.smuteM), '[5]'); c.f.smuteRemoveRows(5, 1); assert.ok(!('smuteM' in c.S));
});
test('wiring: the sequencer\'s blockMuted still has no master; load reads and cleans the key; M.PAT, Alt-click and paste take the master', () => {
  assert.match(src, /if \(!Sx\.smute \|\| !\(y >= 0 && y < 256 && t >= 0 && t < MT\)\) return false;/);
  assert.match(src, /if \(Array\.isArray\(j\.smuteM\)\)\{ const M = \[\.\.\.new Set\(j\.smuteM\.map\(v => int\(v, -1\)\)\.filter\(v => v >= 0 && v < 256\)\)\]\.sort/);
  assert.doesNotMatch(src, /The master column can’t be muted/); assert.match(src, /const cols = sxCols\(r\), cells = \[\];/);
  assert.match(src, /if \(t === MT \|\| t < NT\) setBlockMute\(y, t, !!p\.mu\)/);
});
