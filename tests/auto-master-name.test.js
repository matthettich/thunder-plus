// AUTO MASTER (12 Nov, owner: 'Instead of "mastering off" in mixer, call it "auto master"'): the Mixer cell and the texts about that switch; song keys and identifiers unchanged.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const src = require('./helpers').read('index.html');
test('the Mixer cell, its info line, toasts and titles say auto master', () => {
  assert.match(src, /\{t:'AUTO MASTER ' \+ \(mstOn\(\) \? 'ON' : 'OFF'\)/); assert.doesNotMatch(src, /\{t:'MASTERING ' \+ \(mstOn/);
  assert.match(src, /return 'Auto master ' \+ \(S\.mst\.on \? 'on' : 'off'\)/);
  assert.match(src, /'Auto master on\. Renders of the song include it\.' : 'Auto master off: the master bus as before\.'/);
  assert.match(src, /toast\('Auto master: EDIT opens it/); assert.match(src, /id="mwOn" aria-pressed="false" title="Auto master on the master bus/);
});
test('code identifiers and the song file key stay', () => { assert.match(src, /S\.mstPump = !!j\.mstPump;/); assert.match(src, /\['MASTER', 'MASTERING', 'OUT'\]\[c\.mx \|\| 0\]/); });
