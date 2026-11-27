// Build 688: the page's last inline script runs OUTSIDE the main script's scope. From build 677 to 687 it named `aset` and
// `loadClient` (both inside that scope) and threw a ReferenceError on every load: no native client on the Android app, and the
// service worker lines after it never ran. This test keeps every later inline script free of the main script's names.
const test = require('node:test'), assert = require('node:assert'), fs = require('node:fs'), path = require('node:path');
const src = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8');
test('inline scripts after the main one name nothing from its scope', () => {
  const main = src.indexOf('/* ===ENGINE START=== */'); assert.ok(main > 0);
  const end = src.indexOf('</script>', main); const rest = src.slice(end + 9);
  const inl = [...rest.matchAll(/<script>([\s\S]*?)<\/script>/g)].map(m => m[1]);
  assert.ok(inl.length >= 1);
  for (const code of inl) {
    for (const name of ['aset', 'loadClient', 'NCON', 'NC(', 'pbLog', 'nativeDiag', 'BUILD']) assert.ok(!new RegExp('(^|[^.\\w])' + name.replace('(', '\\(') + '(?![\\w])').test(code), 'the last script names ' + name);
    assert.doesNotThrow(() => new Function('window', 'navigator', 'location', 'tpLog', code)({ self: 1, top: 1 }, {}, { protocol: 'http:' }, () => {}));
  }
});
test('the main script hands the last one a boot function', () => {
  assert.ok(/window\.__ncBoot = \(\) => \{ if \(window\.thunderCore \|\| aset\.core === 'native'\) loadClient\(\); \};/.test(src));
  assert.ok(/if \(window\.__ncBoot\) window\.__ncBoot\(\);/.test(src));
});
