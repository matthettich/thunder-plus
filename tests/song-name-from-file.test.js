// THE SONG'S NAME FROM ITS FILE (14 Nov, owner: "the program still thinks every track I load is called 'song 1' instead of the file name").
// A song still named by its default ("Song 1", "Song N", empty) takes the file's name (no .thunder / .json) on every load path and on Save as; a name the
// user chose stays; loading is never an unsaved change; a backup is restored as it was. The FILE SAFETY functions are cut out of index.html and run with stubs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const src = require('./helpers').read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ' in index.html; this test needs updating'); return m[0]; };
const helpers = cut(/const songNameDefault = [^\n]*\nconst fileSongName = [^\n]*\n/, 'songNameDefault / fileSongName');
const adopt = cut(/function adoptFileName\(fname\)\{[^\n]*\n/, 'adoptFileName');
const loaders = cut(/async function loadProjText\(text, name, where\)\{[\s\S]*?\n\}\nasync function loadProjFile\(f, h\)\{[\s\S]*?\n\}\n/, 'loadProjText / loadProjFile');

function rig(cur = 'Song 1') {
  const ev = { saveS: 0, setSong: [], dirty: [], toasts: [], titles: 0 };
  const c = vm.createContext({ console, JSON, String, Promise, Error, Number, Math, setTimeout: () => 0, ev, startName: cur, BUILD: 610 });
  vm.runInContext(`
    let S = {name:startName}, projLoose = null, projDirty = false, projP = null, projH = null;
    const normSong = j => Object.assign({name:'Song 1'}, j, {name:typeof j.name === 'string' ? j.name.slice(0, 40) : 'Song 1'});
    const kitFromJSON = k => k, kitReplaceOk = async () => true, setKit = () => {}, presetsByRef = () => {}, undoReset = () => {};
    const setSong = (s) => { ev.setSong.push(s.name); S = s; projLoose = null; };
    const projSet = (p, h) => { projP = p || null; projH = h || null; projLoose = null; };
    const setDirty = on => { projDirty = !!on; ev.dirty.push(!!on); };
    const saveS = () => { ev.saveS = (ev.saveS || 0) + 1; projDirty = true; }, draw = () => {}, msMeta = () => {}, dirtyShow = () => {};
    const toast = m => ev.toasts.push(m), toastErr = m => ev.toasts.push(m), tpLog = () => {};
    ${helpers}${adopt}${loaders}
    this.api = {loadProjText, loadProjFile, adoptFileName, songNameDefault, fileSongName, get S(){ return S; }, get loose(){ return projLoose; }, get dirty(){ return projDirty; }, get path(){ return projP; }, get h(){ return projH; }};
  `, c);
  return { api: c.api, ev };
}
const proj = (name, extra) => JSON.stringify({ app: 'thunder-project', v: 1, build: 609, song: Object.assign({ name }, extra), kit: null });

test('a default-named song takes the file name on every load path that has a file', async () => {
  for (const [label, where] of [['file picker with a handle', { h: {}, file: true }], ['Open recent (handle)', { h: {}, file: true }], ['desktop open / Open with / Open recent (path)', { path: '/Users/m/Music/acid.thunder', file: true }],
    ['drop or Android picker, no handle', { file: true }]]) {
    const R = rig(); assert.equal(await R.api.loadProjText(proj('Song 1'), 'acid.thunder', where), true, label);
    assert.equal(R.api.S.name, 'acid', label); assert.equal(R.api.dirty, false, label + ': no unsaved-changes mark'); assert.equal(R.ev.saveS, 0, label + ': nothing saved or marked as an edit');
  }
});
test('loadProjFile (picker, launchQueue, share target) with and without a handle', async () => {
  const f = { name: 'acid.thunder', text: async () => proj('Song 1') };
  let R = rig(); await R.api.loadProjFile(f); assert.equal(R.api.S.name, 'acid'); assert.equal(R.api.loose.f, 'acid.thunder'); assert.equal(R.api.loose.s, 'acid'); assert.equal(R.api.dirty, false);
  R = rig(); await R.api.loadProjFile(f, { name: 'acid.thunder' }); assert.equal(R.api.S.name, 'acid'); assert.ok(R.api.h); assert.equal(R.api.dirty, false);
});
test('default names: Song 1, Song 12, song 3, empty, missing, spaces', async () => {
  for (const n of ['Song 1', 'Song 12', 'song 3', 'SONG', '', '  ', undefined]) {
    const R = rig(); await R.api.loadProjText(proj(n), 'x.thunder', { path: '/a/x.thunder', file: true }); assert.equal(R.api.S.name, 'x', JSON.stringify(n));
  }
});
test('a song the user named keeps its name; the file name stays what the window uses', async () => {
  for (const n of ['Night drive', 'Song One', 'My Song 1', 'Songs 1']) {
    const R = rig(); await R.api.loadProjText(proj(n), 'acid.thunder', { file: true }); assert.equal(R.api.S.name, n);
    assert.equal(R.api.loose.f, 'acid.thunder'); assert.equal(R.api.loose.s, n);   // projTitle() shows the file's name
  }
});
test('a backup, the first-run song and the test hook (no where.file) keep the name they have', async () => {
  let R = rig(); await R.api.loadProjText(proj('Song 1'), 'Song 1', { dirty: true }); assert.equal(R.api.S.name, 'Song 1'); assert.equal(R.api.dirty, true);
  R = rig(); await R.api.loadProjText(proj('Song 1'), 'ELECTRO.thunder'); assert.equal(R.api.S.name, 'Song 1'); assert.equal(R.api.loose, null);
  R = rig(); await R.api.loadProjText(proj('Night drive'), 'x.thunder', { dirty: true }); assert.equal(R.api.S.name, 'Night drive');
});
test('names with dots, spaces and non-ASCII; the extension is stripped only for .thunder / .json', () => {
  const R = rig(), f = R.api.fileSongName;
  assert.equal(f('acid.thunder'), 'acid'); assert.equal(f('ACID.THUNDER'), 'ACID'); assert.equal(f('v2.1 final.thunder'), 'v2.1 final'); assert.equal(f('a.b.c.json'), 'a.b.c');
  assert.equal(f('Ünïcode 音楽 — 夜.thunder'), 'Ünïcode 音楽 — 夜'); assert.equal(f('  spaced name .thunder'), 'spaced name');
  assert.equal(f('/Users/m/My Music/acid.thunder'), 'acid'); assert.equal(f('C:\\Users\\m\\acid.thunder'), 'acid');
  assert.equal(f('demo.mid'), 'demo.mid'); assert.equal(f('take.wav'), 'take.wav'); assert.equal(f('acid'), 'acid'); assert.equal(f('acid.thunder.bak'), 'acid.thunder.bak');
  assert.equal(f('.thunder'), ''); assert.equal(f(''), ''); assert.equal(f(null), '');
  assert.equal(f('x'.repeat(60) + '.thunder').length, 40);
});
test('an empty file name leaves the default', async () => {
  const R = rig(); await R.api.loadProjText(proj('Song 1'), '.thunder', { file: true }); assert.equal(R.api.S.name, 'Song 1');
});
test('a .json project with a dotted name loads under that name', async () => {
  const R = rig(); await R.api.loadProjText(proj('Song 1'), 'my.song.v2.json', { file: true }); assert.equal(R.api.S.name, 'my.song.v2');
});
test('Save as renames a default-named song, not a named one', () => {
  let R = rig('Song 1'); assert.equal(R.api.adoptFileName('/Users/m/acid2.thunder'), true); assert.equal(R.api.S.name, 'acid2');
  R = rig('Song 7'); assert.equal(R.api.adoptFileName('night.thunder'), true); assert.equal(R.api.S.name, 'night');
  R = rig('Night drive'); assert.equal(R.api.adoptFileName('other.thunder'), false); assert.equal(R.api.S.name, 'Night drive');
  R = rig('acid'); assert.equal(R.api.adoptFileName('acid.thunder'), false);
  R = rig('Song 1'); assert.equal(R.api.adoptFileName('.thunder'), false); assert.equal(R.api.S.name, 'Song 1');
});
test('source: every load path says it comes from a file; Save and Save as adopt the name; Restore passes none', () => {
  assert.equal((src.match(/loadProjText\(r\.text, r\.name, \{path:r\.path, file:true\}\)/g) || []).length, 3, 'desktop Open, Open recent and the open-path hand-off');
  assert.match(src, /loadProjText\(JSON\.stringify\(j\), f\.name, path \? \{path, file:true\} : \{file:true\}\)/, 'drop');
  assert.match(src, /h \? \{h, file:true\} : \{file:true\}/, 'loadProjFile');
  assert.match(src, /adoptFileName\(h\.name\); await writeTo\(h\)/, 'browser Save: the name goes in before the file is written');
  assert.match(src, /if \(adoptFileName\(r\.path\)\)\{ try \{ await DESK\.save\(r\.path, projText\(\)\)/, 'desktop Save as: the file is rewritten with the new name');
  assert.match(src, /loadProjText\(text, idx\[k\]\.name, \{dirty:true\}\)/, 'Restore a backup');
});
