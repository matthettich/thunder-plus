// Builds io/build/Release/thunder_io.node and copies it, with this folder's licences and NOTICE, to electron/io/,
// where the desktop app looks for it. Mac: an arm64 and an x86_64 build joined with lipo (the app is universal).
//
//   node io/build.js              needs a C++17 compiler and node-gyp (npm's own copy is used when there is no other;
//                                 NODE_GYP=path/to/node-gyp.js to choose; NODEDIR=… for offline headers)
//
// Plain N-API: built against this Node's headers, the file loads in Electron as well (no electron-rebuild). A failure here
// must not stop the desktop build: the workflow runs it with continue-on-error, and the app then says the audio helper is not available.
'use strict';
const { execFileSync } = require('child_process'), fs = require('fs'), path = require('path');
const D = __dirname, OUT = path.join(D, '..', 'electron', 'io');
const dst = path.join(OUT, 'thunder_io.node'), parts = ['arm64', 'x64'].map(a => path.join(OUT, `thunder_io-${a}.node`));
// A FAILED BUILD SHIPS NOTHING (CodeRabbit, PR #14): electron-builder packs every io/*.node, so on any failure (a missing
// prerequisite too, CodeRabbit: an addon from an earlier build would otherwise ship) the staged addon and its parts are removed.
const fail = msg => { for (const f of [dst, ...parts]) { try { fs.unlinkSync(f); } catch (e) {} } console.error(msg); process.exit(1); };
if (!fs.existsSync(path.join(D, 'vendor', 'miniaudio', 'miniaudio.h'))) fail('io/vendor/miniaudio/miniaudio.h is missing: the vendored sources are not in the tree');
const gyp = process.env.NODE_GYP || [path.join(path.dirname(process.execPath), '..', 'lib', 'node_modules', 'npm', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js'),
  path.join(path.dirname(process.execPath), 'node_modules', 'npm', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js')].find(f => fs.existsSync(f));
if (!gyp) fail('node-gyp not found: set NODE_GYP');
const nd = process.env.NODEDIR ? ['--nodedir=' + process.env.NODEDIR] : [];
const build = arch => {
  execFileSync(process.execPath, [gyp, 'rebuild', '--release', ...nd, ...(arch ? ['--arch=' + arch] : [])], { cwd: D, stdio: 'inherit' });
  return path.join(D, 'build', 'Release', 'thunder_io.node');
};
fs.mkdirSync(OUT, { recursive: true });
let v;
try {
  if (process.platform === 'darwin') {
    ['arm64', 'x64'].forEach((a, i) => fs.copyFileSync(build(a), parts[i]));
    execFileSync('lipo', ['-create', ...parts, '-output', dst], { stdio: 'inherit' });
    for (const f of parts) fs.unlinkSync(f);
  } else fs.copyFileSync(build(), dst);
  for (const f of fs.readdirSync(path.join(D, 'LICENSES'))) fs.copyFileSync(path.join(D, 'LICENSES', f), path.join(OUT, f));
  fs.copyFileSync(path.join(D, 'NOTICE.md'), path.join(OUT, 'NOTICE.md'));
  // Smoke test: it loads and answers (no device is opened, so nothing is touched)
  const I = require(dst); v = I.version();
  if (typeof v !== 'string' || !v.startsWith('thunder_io')) fail('thunder_io.node loaded but does not answer');
  const dv = I.devices({ backend: 'null' });
  if (!Array.isArray(dv) || !dv.some(d => typeof d.inputs === 'number' && typeof d.outputs === 'number')) fail('thunder_io.node: devices({backend:"null"}) returned no usable entry');
  const t0 = I.now(), t1 = I.now();
  if (typeof t0 !== 'number' || !(t1 >= t0)) fail('thunder_io.node: now() runs backwards');
  // the input opens and closes on the null backend (A1.1: no device is touched; the frames themselves: io/test/input.js)
  const op = I.openInput('', { rate: 48000, backend: 'null' }); I.closeInput();
  if (!op || !op.ok || op.rate !== 48000) fail('thunder_io.node: openInput on the null backend: ' + ((op && op.why) || 'no answer'));
  // MIDI (phase B): the port list answers, and the loopback test port opens, sends and closes (no MIDI device is touched)
  const mp = I.midiPorts(); if (!mp || !Array.isArray(mp.outs) || !Array.isArray(mp.ins)) fail('thunder_io.node: midiPorts() returned ' + JSON.stringify(mp));
  const mo = I.midiOpenOut('test:loop'); if (!mo || !mo.ok || !I.midiSend(mo.port, [0xF8], 0)) fail('thunder_io.node: the MIDI test port did not open or send');
  I.midiClose(mo.port);
} catch (e) { fail('io/build.js failed: ' + ((e && e.message) || e)); }
console.log('electron/io/thunder_io.node built (' + Math.round(fs.statSync(dst).size / 1024) + ' KB): ' + v);
