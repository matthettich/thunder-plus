// The native helper's own checks (plan §7, A1.1): the input ring under a real producer thread (io/test/ring-test.cc) and
// openInput / read / closeInput on miniaudio's null backend (io/test/input.js). Each is skipped when this machine can't
// run it (no C++ compiler; the addon not built: node io/build.js).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const { execFileSync, spawnSync } = require('child_process'), fs = require('fs'), os = require('os'), path = require('path');
const R = path.join(__dirname, '..');
const cxx = ['c++', 'g++', 'clang++'].find(c => spawnSync(c, ['--version']).status === 0);

test('io ring: a producer thread against the reader, in order, no torn samples, drops are exact gaps', { skip: !cxx && 'no C++ compiler' }, () => {
  const exe = path.join(os.tmpdir(), 'thunder-ring-test-' + process.pid);
  execFileSync(cxx, ['-std=c++17', '-O2', '-pthread', path.join(R, 'io/test/ring-test.cc'), '-o', exe]);
  try { assert.match(execFileSync(exe, { encoding: 'utf8' }), /ring ok/); } finally { fs.rmSync(exe, { force: true }); }
});

test('io input: null-backend open / read / close in real time', { skip: !fs.existsSync(path.join(R, 'electron/io/thunder_io.node')) && 'addon not built' }, () => {
  assert.match(execFileSync(process.execPath, [path.join(R, 'io/test/input.js')], { encoding: 'utf8' }), /input ok/);
});

test('io MIDI: the loopback port, sends at their OS time, same-moment order, clear, close', { skip: !fs.existsSync(path.join(R, 'electron/io/thunder_io.node')) && 'addon not built' }, () => {
  assert.match(execFileSync(process.execPath, [path.join(R, 'io/test/midi.js')], { encoding: 'utf8', env: Object.assign({}, process.env, { THUNDER_IO_TEST: '1' }) }), /midi ok/);
});
