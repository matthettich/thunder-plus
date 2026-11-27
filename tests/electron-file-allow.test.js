// S-1 (26 Nov 2026, build 684): the desktop shell's file channels only touch a path the person picked or the shell handed over.
// electron/path-allow.js is the list; main.js is loaded against a fake `electron` to check each channel really goes through it.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), os = require('os'), Module = require('module');
const H = require('./helpers'), A = require(path.join(H.ROOT, 'electron/path-allow.js'));
const tmp = () => fs.mkdtempSync(path.join(os.tmpdir(), 'allow-'));

test('allow-list: nothing is allowed until it is picked; extension is still checked', () => {
  const a = A.createAllowList({ file: '' }), d = tmp(), f = path.join(d, 'a.thunder');
  assert.match(a.check(f, 'proj'), /^not allowed/);
  a.allow(f); assert.equal(a.check(f, 'proj'), null);
  assert.equal(a.check(path.join(d, 'a.txt'), 'proj'), 'not a project file');
  assert.equal(a.check(path.join(d, 'x.json'), 'proj') && a.check(path.join(d, 'x.json'), 'proj').slice(0, 11), 'not allowed');
  assert.equal(a.check(f, 'out'), 'not a render file', 'a project path is not a render path');
  const w = path.join(d, 'r.WAV'); assert.match(a.check(w, 'out'), /^not allowed/); a.allow(w); assert.equal(a.check(w, 'out'), null);
});
test('allow-list: relative paths, traversal, NUL bytes and non-strings are refused or normalised away', () => {
  const a = A.createAllowList({ file: '' }), d = tmp(); a.allow(path.join(d, 'ok.thunder'));
  assert.equal(a.allow('ok.thunder'), '', 'a relative path is never allowed'); assert.equal(a.allow(''), ''); assert.equal(a.allow(null), '');
  assert.equal(a.has(path.join(d, 'sub', '..', 'ok.thunder')), true, 'the same file by another spelling');
  assert.equal(a.has(path.join(d, 'sub', '..', '..', 'ok.thunder')), false);
  assert.equal(a.has(path.join(d, 'ok.thunder\0.wav')), false); assert.equal(a.has({}), false); assert.equal(a.has(undefined), false);
  assert.equal(a.has(path.join(d, 'ok.thunder.saving')), false, 'a neighbour is not the file');
});
test('allow-list: Windows paths compare case-insensitively and with either slash; elsewhere case matters', () => {
  const w = A.createAllowList({ file: '', win32: true }); w.allow('C:\\Music\\Song.thunder');
  assert.equal(w.has('c:/music/SONG.thunder'), true); assert.equal(w.has('C:\\Music\\Other.thunder'), false);
  const l = A.createAllowList({ file: '', win32: false }); l.allow('/home/m/Song.thunder'); assert.equal(l.has('/home/m/song.thunder'), false);
});
test('allow-list: the kept list survives a restart, holds the last 64, and a hand-edited bad line is ignored', () => {
  const d = tmp(), file = path.join(d, 'sub', 'allowed-paths.txt'), a = A.createAllowList({ file });
  for (let i = 0; i < 70; i++) a.allow(path.join(d, 's' + i + '.thunder'));
  const b = A.createAllowList({ file });
  assert.equal(b.list().length, 64); assert.equal(b.has(path.join(d, 's69.thunder')), true); assert.equal(b.has(path.join(d, 's5.thunder')), false, 'the oldest fell off');
  assert.equal(b.has(path.join(d, 's6.thunder')), true);
  fs.appendFileSync(file, 'relative.thunder\n'); assert.equal(A.createAllowList({ file }).list().every(p => path.isAbsolute(p)), true);
  a.allow(path.join(d, 's6.thunder')); assert.equal(A.createAllowList({ file }).list().pop(), path.join(d, 's6.thunder'), 'a re-picked path moves to the newest end');
});
test('allow-list: an unreadable or unwritable list file never throws (it only costs Open recent across runs)', () => {
  const d = tmp(), a = A.createAllowList({ file: path.join(d, 'f.txt', 'x', 'allowed.txt'), fsImpl: Object.assign({}, fs, { writeFileSync() { throw new Error('read-only'); } }) });
  assert.doesNotThrow(() => a.allow(path.join(d, 'a.thunder'))); assert.equal(a.has(path.join(d, 'a.thunder')), true, 'still allowed this session');
  fs.writeFileSync(path.join(d, 'junk.txt'), Buffer.from([0xff, 0xfe, 0, 1])); assert.doesNotThrow(() => A.createAllowList({ file: path.join(d, 'junk.txt') }));
});
test('url guards: only app://thunder is the app; http(s) goes to the browser; file / data / blob / javascript are neither', () => {
  assert.equal(A.isAppUrl('app://thunder/index.html'), true); assert.equal(A.isAppUrl('app://thunder'), true);
  for (const u of ['app://evil/x', 'https://thunder/', 'file:///etc/passwd', 'data:text/html,hi', 'blob:app://thunder/1', 'javascript:alert(1)', '', null, 'app://thunder.evil.com/']) assert.equal(A.isAppUrl(u), false, String(u));
  assert.equal(A.isWebUrl('https://github.com/x'), true); assert.equal(A.isWebUrl('http://x.y'), true);
  for (const u of ['file:///x', 'app://thunder/', 'ftp://x', 'javascript:1', 'smb://host/share']) assert.equal(A.isWebUrl(u), false, u);
});

// ---- main.js against a fake electron: every file channel ----
function boot() {
  const d = tmp(), calls = {}, on = {}, handlers = {}, wcOn = {}, opened = [], dlg = { open: null, save: null }, shown = [];
  const win = { loadURL() {}, focus() {}, restore() {}, webContents: { on: (n, f) => { wcOn[n] = f; }, setWindowOpenHandler: f => { wcOn.open = f; }, send() {}, postMessage() {}, isDestroyed: () => false }, on() {}, isDestroyed: () => false, isMinimized: () => false };
  const BW = function () { return win; }; BW.getAllWindows = () => [win];
  const E = {
    app: { getPath: n => n === 'userData' ? d : d, requestSingleInstanceLock: () => true, on: (n, f) => { on[n] = f; }, quit() {}, commandLine: { appendSwitch() {} }, whenReady: () => ({ then: f => { f(); } }) },
    BrowserWindow: BW, session: { defaultSession: { setPermissionRequestHandler() {}, setPermissionCheckHandler() {} } },
    shell: { openExternal: u => opened.push(u), showItemInFolder: p => shown.push(p) }, Menu: { setApplicationMenu() {} },
    protocol: { registerSchemesAsPrivileged() {}, handle() {} }, net: {}, powerSaveBlocker: {}, utilityProcess: {}, MessageChannelMain: function () {}, systemPreferences: {}, powerMonitor: { on() {} },
    ipcMain: { handle: (c, f) => { handlers[c] = f; }, on: (c, f) => { on[c] = f; } },
    dialog: { showOpenDialog: async () => dlg.open, showSaveDialog: async () => dlg.save }
  };
  const orig = Module._load; Module._load = function (r, ...a) { if (r === 'electron') return E; if (r === './updater') return { setupUpdates: () => ({ state() {}, check() {}, act() {} }) }; return orig.call(this, r, ...a); };
  const main = path.join(H.ROOT, 'electron/main.js'); delete require.cache[require.resolve(main)];
  // main.js has a top-level `return` (single-instance lock), so it is loaded as a function body like node does
  try { require(main); } finally { Module._load = orig; }
  const frame = { url: 'app://thunder/index.html' }, sender = { mainFrame: frame, isDestroyed: () => false, reload() {} };
  const ev = { sender, senderFrame: frame };
  return { d, handlers, on, wcOn, opened, dlg, shown, ev, frame, sender };
}
test('main.js: file-read / file-save / out-write refuse a path nobody picked, and the page gets an {error} reply (no throw)', async () => {
  const m = boot(), d = m.d;
  const secret = path.join(d, 'secret.json'); fs.writeFileSync(secret, '{"k":1}');
  const r = await m.handlers['thunder:file-read'](m.ev, secret); assert.match(r.error, /^not allowed/); assert.equal(r.text, undefined);
  const w = await m.handlers['thunder:file-save'](m.ev, secret, 'pwn'); assert.match(w.error, /^not allowed/); assert.equal(fs.readFileSync(secret, 'utf8'), '{"k":1}');
  const o = await m.handlers['thunder:out-write'](m.ev, path.join(d, 'evil.wav'), new ArrayBuffer(4)); assert.match(o.error, /^not allowed/); assert.equal(fs.existsSync(path.join(d, 'evil.wav')), false);
  assert.equal((await m.handlers['thunder:out-write'](m.ev, path.join(d, 'x.exe'), new ArrayBuffer(4))).error, 'not a render file');
  m.handlers['thunder:out-show'](m.ev, secret); assert.deepEqual(m.shown, []);
});
test('main.js: Open, Save as and Render to dialogs allow exactly the path picked; a later Save and the render write go through', async () => {
  const m = boot(), d = m.d, a = path.join(d, 'a.thunder'); fs.writeFileSync(a, '{"x":1}');
  m.dlg.open = { canceled: false, filePaths: [a] };
  const o = await m.handlers['thunder:file-open'](m.ev); assert.equal(o.text, '{"x":1}');
  assert.deepEqual(await m.handlers['thunder:file-save'](m.ev, a, '{"x":2}'), { ok: true, path: a }); assert.equal(fs.readFileSync(a, 'utf8'), '{"x":2}');
  assert.equal((await m.handlers['thunder:file-read'](m.ev, a)).text, '{"x":2}');
  m.dlg.open = { canceled: true, filePaths: [] }; assert.equal(await m.handlers['thunder:file-open'](m.ev), null);
  const b = path.join(d, 'b');   // Save as: the shell adds .thunder, writes it, and the page's follow-up save of the same path works
  m.dlg.save = { canceled: false, filePath: b }; const s = await m.handlers['thunder:file-saveas'](m.ev, 'Song.thunder', 'T1', a);
  assert.deepEqual(s, { ok: true, path: b + '.thunder' }); assert.deepEqual(await m.handlers['thunder:file-save'](m.ev, b + '.thunder', 'T2'), { ok: true, path: b + '.thunder' });
  m.dlg.save = { canceled: true }; assert.equal(await m.handlers['thunder:file-saveas'](m.ev, 'S.thunder', 't', ''), null);
  const w = path.join(d, 'out'); m.dlg.save = { canceled: false, filePath: w }; const p = await m.handlers['thunder:out-pick'](m.ev, 'Render.wav', '');
  assert.equal(p, w + '.wav'); assert.deepEqual(await m.handlers['thunder:out-write'](m.ev, p, new Uint8Array([1, 2, 3]).buffer), { ok: true, path: p }); assert.equal(fs.readFileSync(p).length, 3);
  m.handlers['thunder:out-show'](m.ev, p); assert.deepEqual(m.shown, [p]);
  assert.equal(await m.handlers['thunder:out-pick'](m.ev, 'x.exe', ''), null, 'only wav / zip / mid are offered');
  m.dlg.save = { canceled: false, filePath: path.join(d, 'z') }; assert.equal((await m.handlers['thunder:out-pick'](m.ev, 'Stems.zip', '')), path.join(d, 'z.zip'));
});
test('main.js: a dropped project (preload reports its path), an opened-from-desktop path, and a path kept from an earlier run are allowed; a drop of a non-project is not', async () => {
  const m = boot(), d = m.d, a = path.join(d, 'drop.thunder'), c = path.join(d, 'wav.wav'); fs.writeFileSync(a, '{}');
  m.on['thunder:path-drop'](m.ev, a); m.on['thunder:path-drop'](m.ev, c); m.on['thunder:path-drop'](m.ev, 'rel.thunder');
  assert.equal((await m.handlers['thunder:file-read'](m.ev, a)).text, '{}');
  assert.equal((await m.handlers['thunder:out-write'](m.ev, c, new ArrayBuffer(1))).error.slice(0, 11), 'not allowed');
  const b = path.join(d, 'open.thunder'); fs.writeFileSync(b, '{"o":1}'); m.on['open-file']({ preventDefault() {} }, b);   // macOS open-file
  assert.equal((await m.handlers['thunder:file-read'](m.ev, b)).text, '{"o":1}');
  assert.ok(fs.readFileSync(path.join(d, 'allowed-paths.txt'), 'utf8').includes(b), 'kept for Open recent next run');
  m.on['second-instance']({}, ['x', path.join(d, 'late.thunder')]);   // not there on disk: argPath ignores it
  assert.match((await m.handlers['thunder:file-read'](m.ev, path.join(d, 'late.thunder'))).error, /^not allowed/);
});
test('main.js: calls from any frame but the app page are refused on every channel', async () => {
  const m = boot(), d = m.d, a = path.join(d, 'a.thunder'); fs.writeFileSync(a, '{}'); m.on['thunder:path-drop'](m.ev, a);
  const sub = { url: 'app://thunder/chains/index.html' }, other = { url: 'https://evil.example/' };
  for (const ev of [{ sender: m.sender, senderFrame: sub }, { sender: m.sender, senderFrame: other }, { sender: m.sender, senderFrame: null }, { sender: m.sender }]) {
    assert.match((await m.handlers['thunder:file-read'](ev, a)).error, /^not allowed/); assert.match((await m.handlers['thunder:file-save'](ev, a, 'x')).error, /^not allowed/);
    assert.match((await m.handlers['thunder:out-write'](ev, path.join(d, 'a.wav'), new ArrayBuffer(1))).error, /^not allowed/);
    assert.equal(await m.handlers['thunder:out-pick'](ev, 'a.wav', ''), null); assert.match((await m.handlers['thunder:file-open'](ev)).error, /^not allowed/);
    assert.match((await m.handlers['thunder:file-saveas'](ev, 'a.thunder', 't', '')).error, /^not allowed/);
  }
  assert.equal(fs.readFileSync(a, 'utf8'), '{}');
  // a path-drop from a non-app frame does not allow anything
  const c = path.join(d, 'c.thunder'); fs.writeFileSync(c, '{}'); m.on['thunder:path-drop']({ sender: m.sender, senderFrame: other }, c);
  assert.match((await m.handlers['thunder:file-read'](m.ev, c)).error, /^not allowed/);
});
test('main.js: navigation and new windows: http(s) opens in the browser, everything else is blocked, frames only load the app', () => {
  const m = boot(); let h = m.on['activate'] && 1; void h;
  const ns = (fn, ...a) => { const e = { prevented: false, isMainFrame: true, preventDefault() { this.prevented = true; } }; return [e, fn(e, ...a)]; };
  let [e] = ns(m.wcOn['will-navigate'], 'file:///etc/passwd'); assert.equal(e.prevented, true); assert.deepEqual(m.opened, []);
  [e] = ns(m.wcOn['will-navigate'], 'https://github.com/matthettich'); assert.equal(e.prevented, true); assert.deepEqual(m.opened, ['https://github.com/matthettich']);
  [e] = ns(m.wcOn['will-navigate'], 'app://thunder/index.html'); assert.equal(e.prevented, true, 'reload goes through thunder:reload');
  assert.deepEqual(m.wcOn.open({ url: 'file:///x' }), { action: 'deny' }); assert.deepEqual(m.wcOn.open({ url: 'https://x.org/' }), { action: 'deny' }); assert.equal(m.opened.length, 2);
  const fr = (url, main) => { const x = { prevented: false, isMainFrame: !!main, preventDefault() { this.prevented = true; } }; m.wcOn['will-frame-navigate'](x, url); return x.prevented; };
  assert.equal(fr('app://thunder/waves/index.html'), false); assert.equal(fr('about:blank'), false);
  assert.equal(fr('https://evil.example/'), true); assert.equal(fr('file:///etc/passwd'), true); assert.equal(fr('data:text/html,x'), true);
  assert.equal(fr('https://evil.example/', true), false, 'top-frame navigations are will-navigate\'s business');
});
test('page: a refused recent file stays on the list with a "use Open" message; other errors still drop it; the page handles {error} on save / out-write', () => {
  const src = H.read('index.html');
  assert.match(src, /\/\^not allowed\/\.test\(r\.error\)\) return toastErr\('Couldn’t open ' \+ e\.name \+ ' from the list this time/);
  assert.match(src, /toastErr\('Couldn’t save to ' \+ projLabel\(\) \+ \(r && r\.error \? ': ' \+ r\.error : ''\) \+ '\. Choose where to save it\.'/);
  assert.match(src, /else toastErr\('Couldn’t save: ' \+ \(\(r && r\.error\) \|\| 'unknown error'\)\)/);
  assert.match(H.read('electron/preload.js'), /thunder:path-drop/);
  assert.ok(JSON.parse(H.read('electron/package.json')).build.files.includes('path-allow.js'), 'packaged with the app');
});
