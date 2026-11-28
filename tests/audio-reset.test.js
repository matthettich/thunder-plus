// Reset audio settings (Settings → Audio): the reset list is cut out of index.html and checked against the app's own aset defaults.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'index.html: could not find ' + what + '; the code moved, so this test needs updating'); return m; };

test('audResetAset puts every reset field back to the default aset starts with', () => {
  const d = vm.runInContext('(' + cut(/const aset = \(\(\) => \{ const d = (\{[^}]*\});/, 'the aset defaults')[1] + ')', vm.createContext({}));
  const fn = vm.runInContext(cut(/function audResetAset\(a\)\{[^\n]*\n/, 'audResetAset')[0] + ';audResetAset', vm.createContext({}));
  const a = Object.assign({}, d, { lat: 'playback', latPick: true, rate: 48000, pdc: true, psLive: 'render', core: 'page', corePick: true, noWk: true, sink: 'dev1', vol: 40, link: true, inEngine: 'browser', midiEngine: 'browser' });
  fn(a);
  for (const k of ['lat', 'rate', 'pdc', 'psLive', 'core']) assert.equal(a[k], d[k], k);
  for (const k of ['latPick', 'corePick', 'noWk']) assert.ok(!(k in a) || !a[k], k);
  // the owner's setup is left alone
  assert.deepEqual([a.sink, a.vol, a.link, a.inEngine, a.midiEngine], ['dev1', 40, true, 'browser', 'browser']);
});
