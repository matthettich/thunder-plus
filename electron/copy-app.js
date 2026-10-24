// Copies the web app (one folder up) into ./app, so the desktop build always runs the same files as the site.
// It then checks that every file the page loads at run time made it in, and stops the build if one didn't.
const fs = require('fs'), path = require('path');
const src = path.join(__dirname, '..'), dst = path.join(__dirname, 'app');
// (pump/ holds the …pump effects' wasm, their parameter tables and, in pump/ui/, the pop-up windows: all copied, all checked below)
const PARTS = ['index.html', 'manifest.webmanifest', 'master-worklet.js', 'icons', 'fonts', 'waves', 'chains', 'pump', 'sounds', 'THIRD_PARTY_NOTICES.md'];
fs.rmSync(dst, {recursive:true, force:true});
fs.mkdirSync(dst, {recursive:true});
for (const f of PARTS){
  const from = path.join(src, f);
  if (!fs.existsSync(from)){ console.error('Missing from the app: ' + f); process.exit(1); }
  fs.cpSync(from, path.join(dst, f), {recursive:true});
}
// LXR ENGINES: lxr/ (the LXR-02 firmware's voices, GPL-3.0) when it is there, its source and licence included.
// Leave the folder out of the repo to build without it; the app then has no Firmware engines.
if (fs.existsSync(path.join(src, 'lxr'))) fs.cpSync(path.join(src, 'lxr'), path.join(dst, 'lxr'), {recursive:true});
// Files index.html fetches by name (worklets, fonts, icons): each must exist in ./app.
const html = fs.readFileSync(path.join(dst, 'index.html'), 'utf8');
const refs = new Set();
for (const m of html.matchAll(/["'`(]((?:waves|chains|pump|fonts|icons)\/[\w.\-\/]+|master-worklet\.js)(?:\?[^"'`)]*)?["'`)]/g)) refs.add(m[1]);
const missing = [...refs].filter(r => !fs.existsSync(path.join(dst, r)));
if (missing.length){ console.error('index.html loads files that are not in the app:\n  ' + missing.join('\n  ')); process.exit(1); }
console.log('Copied the app into electron/app (' + refs.size + ' referenced files checked)');
