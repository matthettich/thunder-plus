// POP-UP MESSAGE WORDING (17 Nov, build 641): the owner said "apply all of the suggested toast text" from the audit sheet.
// tests/fixtures/toast-wording-639.json lists what was applied as {id, file, old, new}: source fragments (a string with its quotes, or the
// part of a longer expression that changed). No old fragment may remain in the app's files and every new one must be there.
// CHANGES history lines (the "[build, 'date', [...]" entries) keep the old wording on purpose and are left out of the search.
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const ROOT = path.join(__dirname, '..');
const list = JSON.parse(fs.readFileSync(path.join(__dirname, 'fixtures', 'toast-wording-639.json'), 'utf8'));
const text = {};
const src = f => text[f] || (text[f] = fs.readFileSync(path.join(ROOT, f), 'utf8').split('\n').filter(l => !/^\s*\[\d+, '/.test(l)).join('\n'));
test('the audit list is well formed', () => {
  assert.ok(list.length > 250, 'the applied list has ' + list.length + ' entries');
  for (const e of list) { assert.ok(e.id && e.file && typeof e.old === 'string' && typeof e.new === 'string' && e.old !== e.new, e.id); assert.ok(fs.existsSync(path.join(ROOT, e.file)), e.file); }
});
test('no old wording of the audit list remains in the source files', () => {
  const left = list.filter(e => src(e.file).includes(e.old)).map(e => e.id + ' ' + e.file + ' ' + e.old.slice(0, 70));
  assert.deepEqual(left, []);
});
test('every new wording of the audit list is in the source files', () => {
  const missing = list.filter(e => !src(e.file).includes(e.new)).map(e => e.id + ' ' + e.file + ' ' + e.new.slice(0, 70));
  assert.deepEqual(missing, []);
});
