// The desktop updater's version comparison (electron/updater.js newer()): semver order, prereleases below their final release (P1-1, 27 Nov review).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), path = require('path'), Module = require('module');
const H = require('./helpers'), UPD = path.join(H.ROOT, 'electron/updater.js');
const orig = Module._load; Module._load = function (r, ...a) { if (r === 'electron') return {}; if (r === 'electron-updater') throw new Error('not installed in tests'); return orig.call(this, r, ...a); };
const { newer } = require(UPD); Module._load = orig;

const NEWER = [['1.3.0', '1.3.0-beta.2'], ['1.3.0-beta.2', '1.3.0-beta.1'], ['1.3.1', '1.3.0'], ['1.10.0', '1.9.9'], ['1.3.0', '1.3.0-rc.1'], ['1.2.10', '1.2.9'],
  ['1.3.0-rc.1', '1.3.0-beta.9'], ['1.3.0-beta.10', '1.3.0-beta.9'], ['1.3.0-beta.1', '1.3.0-beta'], ['1.3.0-beta', '1.3.0-2'], ['v1.3.1', '1.3.0'], ['1.4.0-beta.1', '1.3.9']];
for (const [a, b] of NEWER) test('updater newer(): ' + a + ' > ' + b, () => { assert.equal(newer(a, b), true, a + ' should be newer than ' + b); assert.equal(newer(b, a), false, b + ' should not be newer than ' + a); });
for (const v of ['1.3.0', '1.3.0-beta.2', 'v1.3.0']) test('updater newer(): ' + v + ' is not newer than itself', () => assert.equal(newer(v, v.replace(/^v/, '')), false));
test('updater newer(): 1.2.9 < 1.2.10 (existing case)', () => assert.equal(newer('1.2.9', '1.2.10'), false));
