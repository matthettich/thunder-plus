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
if (!fs.existsSync(path.join(D, 'vendor', 'miniaudio', 'miniaudio.h'))) { console.error('io/vendor/miniaudio/miniaudio.h is missing: the vendored sources are not in the tree'); process.exit(1); }
const gyp = process.env.NODE_GYP || [path.join(path.dirname(process.execPath), '..', 'lib', 'node_modules', 'npm', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js'),
  path.join(path.dirname(process.execPath), 'node_modules', 'npm', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js')].find(f => fs.existsSync(f));
if (!gyp) { console.error('node-gyp not found: set NODE_GYP'); process.exit(1); }
const nd = process.env.NODEDIR ? ['--nodedir=' + process.env.NODEDIR] : [];
const build = arch => {
  execFileSync(process.execPath, [gyp, 'rebuild', '--release', ...nd, ...(arch ? ['--arch=' + arch] : [])], { cwd: D, stdio: 'inherit' });
  return path.join(D, 'build', 'Release', 'thunder_io.node');
};
fs.mkdirSync(OUT, { recursive: true });
const dst = path.join(OUT, 'thunder_io.node');
if (process.platform === 'darwin') {
  const parts = [];
  for (const a of ['arm64', 'x64']) { const f = path.join(OUT, `thunder_io-${a}.node`); fs.copyFileSync(build(a), f); parts.push(f); }
  execFileSync('lipo', ['-create', ...parts, '-output', dst], { stdio: 'inherit' });
  for (const f of parts) fs.unlinkSync(f);
} else fs.copyFileSync(build(), dst);
for (const f of fs.readdirSync(path.join(D, 'LICENSES'))) fs.copyFileSync(path.join(D, 'LICENSES', f), path.join(OUT, f));
fs.copyFileSync(path.join(D, 'NOTICE.md'), path.join(OUT, 'NOTICE.md'));
// Smoke test: it loads and answers (no device is opened, so nothing is touched)
const I = require(dst), v = I.version();
if (typeof v !== 'string' || !v.startsWith('thunder_io')) { console.error('thunder_io.node loaded but does not answer'); process.exit(1); }
const dv = I.devices({ backend: 'null' });
if (!Array.isArray(dv) || !dv.some(d => typeof d.inputs === 'number' && typeof d.outputs === 'number')) { console.error('thunder_io.node: devices({backend:"null"}) returned no usable entry'); process.exit(1); }
const t0 = I.now(), t1 = I.now();
if (typeof t0 !== 'number' || !(t1 > t0)) { console.error('thunder_io.node: now() does not increase'); process.exit(1); }
console.log('electron/io/thunder_io.node built (' + Math.round(fs.statSync(dst).size / 1024) + ' KB): ' + v);
