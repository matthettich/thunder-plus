// Copies the web app (one folder up) into ./app, so the desktop build always runs the same files as the site.
// It then checks that every file the page loads at run time made it in, and stops the build if one didn't.
const fs = require('fs'), path = require('path');
const src = path.join(__dirname, '..'), dst = path.join(__dirname, 'app');
// (pump/ holds the …pump effects' wasm, their parameter tables and, in pump/ui/, the pop-up windows: all copied, all checked below)
const PARTS = ['index.html', 'manifest.webmanifest', 'master-worklet.js', 'core', 'icons', 'fonts', 'waves', 'chains', 'pump', 'sounds', 'wavetables', 'THIRD_PARTY_NOTICES.md'];
fs.rmSync(dst, {recursive:true, force:true});
fs.mkdirSync(dst, {recursive:true});
for (const f of PARTS){
  const from = path.join(src, f);
  if (!fs.existsSync(from)){ console.error('Missing from the app: ' + f); process.exit(1); }
  fs.cpSync(from, path.join(dst, f), {recursive:true});
}
// BETA ICON: a desktop beta (THUNDER_BETA=1, set by desktop.yml for a pre-release or a -beta tag) swaps the two dogs' colours.
// It replaces only the 512 / 192 "any" icons inside ./app (the window icon, and electron-builder's .icns / .ico / AppImage icon
// read app/icons/plus-icon-512.png); the repo's icons/ folder and every stable build are untouched. Made by tools/make-beta-icon.py.
if (process.env.THUNDER_BETA === '1'){
  for (const f of ['plus-icon-512.png', 'plus-icon-192.png']){
    const from = path.join(__dirname, 'icons-beta', f);
    if (!fs.existsSync(from)){ console.error('Missing beta icon: ' + from); process.exit(1); }
    fs.copyFileSync(from, path.join(dst, 'icons', f));
  }
  console.log('Beta build: using the swapped-dogs icon');
}
// LXR ENGINES: lxr/ (the LXR-02 firmware's voices, GPL-3.0) when it is there, its source and licence included.
// Leave the folder out of the repo to build without it; the app then has no Firmware engines.
if (fs.existsSync(path.join(src, 'lxr'))) fs.cpSync(path.join(src, 'lxr'), path.join(dst, 'lxr'), {recursive:true});
// Files index.html fetches by name (worklets, fonts, icons): each must exist in ./app.
const html = fs.readFileSync(path.join(dst, 'index.html'), 'utf8');
const refs = new Set();
for (const m of html.matchAll(/["'`(]((?:waves|chains|pump|core|fonts|icons)\/[\w.\-\/]+|master-worklet\.js)(?:\?[^"'`)]*)?["'`)]/g)) refs.add(m[1]);
const missing = [...refs].filter(r => !fs.existsSync(path.join(dst, r)));
if (missing.length){ console.error('index.html loads files that are not in the app:\n  ' + missing.join('\n  ')); process.exit(1); }
console.log('Copied the app into electron/app (' + refs.size + ' referenced files checked)');
