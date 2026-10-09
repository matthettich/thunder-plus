// Copies the web app (one folder up: page, manifest, icons, fonts) into ./app, so the desktop build always uses the same index.html as the site.
const fs = require('fs'), path = require('path');
const src = path.join(__dirname, '..'), dst = path.join(__dirname, 'app');
fs.rmSync(dst, {recursive:true, force:true});
fs.mkdirSync(dst, {recursive:true});
for (const f of ['index.html', 'manifest.webmanifest', 'icons', 'fonts', 'waves', 'chains']) fs.cpSync(path.join(src, f), path.join(dst, f), {recursive:true});
console.log('Copied the app into electron/app');
