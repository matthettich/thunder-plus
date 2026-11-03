// Builds link/build/Release/thunder_link.node and copies it, with this folder's LICENSE and NOTICE, to electron/link/,
// where electron/main.js looks for it. Mac: an arm64 and an x86_64 build joined with lipo (the app is universal).
//
//   node link/build.js            needs link/sdk (sh link/fetch-sdk.sh), a C++17 compiler and node-gyp (npm's own copy is used
//                                 when there is no other; NODE_GYP=path/to/node-gyp.js to choose; NODEDIR=… for offline headers)
//
// Plain N-API: built against this Node's headers, the file loads in Electron as well (no electron-rebuild). A failure here
// must not stop the desktop build: the workflow runs it with continue-on-error, and the app then says Link is not available.
'use strict';
const { execFileSync } = require('child_process'), fs = require('fs'), path = require('path');
const D = __dirname, OUT = path.join(D, '..', 'electron', 'link');
if (!fs.existsSync(path.join(D, 'sdk', 'include', 'ableton', 'Link.hpp'))) { console.error('link/sdk is missing: run  sh link/fetch-sdk.sh  first'); process.exit(1); }
const gyp = process.env.NODE_GYP || [path.join(path.dirname(process.execPath), '..', 'lib', 'node_modules', 'npm', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js'),
  path.join(path.dirname(process.execPath), 'node_modules', 'npm', 'node_modules', 'node-gyp', 'bin', 'node-gyp.js')].find(f => fs.existsSync(f));
if (!gyp) { console.error('node-gyp not found: set NODE_GYP'); process.exit(1); }
const nd = process.env.NODEDIR ? ['--nodedir=' + process.env.NODEDIR] : [];
const build = arch => {
  execFileSync(process.execPath, [gyp, 'rebuild', '--release', ...nd, ...(arch ? ['--arch=' + arch] : [])], { cwd: D, stdio: 'inherit' });
  return path.join(D, 'build', 'Release', 'thunder_link.node');
};
fs.mkdirSync(OUT, { recursive: true });
const dst = path.join(OUT, 'thunder_link.node');
if (process.platform === 'darwin') {
  const parts = [];
  for (const a of ['arm64', 'x64']) { const f = path.join(OUT, `thunder_link-${a}.node`); fs.copyFileSync(build(a), f); parts.push(f); }
  execFileSync('lipo', ['-create', ...parts, '-output', dst], { stdio: 'inherit' });
  for (const f of parts) fs.unlinkSync(f);
} else fs.copyFileSync(build(), dst);
for (const f of ['LICENSE', 'NOTICE.md']) fs.copyFileSync(path.join(D, f), path.join(OUT, f));
// Smoke test: it loads, and its clock runs (Link isn't enabled, so nothing goes on the network)
const L = require(dst), a = L.now(); if (!(L.now() >= a && L.snapshot(4).tempo > 0)) { console.error('thunder_link.node loaded but does not answer'); process.exit(1); }
console.log('electron/link/thunder_link.node built (' + Math.round(fs.statSync(dst).size / 1024) + ' KB)');
