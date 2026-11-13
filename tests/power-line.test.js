// POWER STATE IN THE PLAYBACK REPORT (12 Nov): the pure formatter pwFmt(), cut out of index.html.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function pwFmt\(st, full\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'could not find pwFmt; this test needs updating');
const c = vm.createContext({}); vm.runInContext(m[0] + '\nthis.f = pwFmt;', c); const f = c.f;

test('power line: battery level and charging, rounded; the short form for a change', () => {
  assert.equal(f({ level: 0.34, charging: false }, false), 'power: battery 34% · on battery');
  assert.equal(f({ level: 0.809, charging: true }, false), 'power: battery 81% · charging');
  assert.equal(f({ level: 1, charging: true, cores: 8 }, false), 'power: battery 100% · charging', 'the short form has no hardware');
});
test('power line: the full form adds what the browser exposes, and leaves out what it does not', () => {
  assert.equal(f({ level: 0.5, charging: false, cores: 8, mem: 4, saveData: false, reduced: true }, true), 'power: battery 50% · on battery · 8 cores · 4 GB memory · data saver off · reduced motion on');
  assert.equal(f({ level: null, cores: 8, mem: 0, saveData: null, reduced: false }, true), 'device: 8 cores · reduced motion off', 'no Battery API: no battery text at all');
  assert.equal(f({ level: null, cores: 0, mem: 0, saveData: null, reduced: null }, true), '', 'nothing known: nothing written');
  assert.equal(f({ level: null }, false), '');
});
test('power line: wired in at PLAY, in the header settings line, and only listeners while stopped', () => {
  assert.match(src, /pbLog\('PLAY ' \+ mode[^\n]*pwFmt\(pwState\(\), true\)/); assert.match(src, /runaway kick \$\{rkOn \? 'on' : 'off'\}\$\{\(\(\) => \{ try \{ const t = pwFmt/);
  assert.match(src, /function pwNote\(why\)\{ if \(!tp\.on \|\| !pwB\) return;/, 'a battery event while stopped does nothing');
});
