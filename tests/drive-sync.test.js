// GOOGLE DRIVE (build 668, web only): the Drive block of index.html (between DRIVE SYNC BEGIN / END) runs in a vm sandbox with a mocked
// fetch and a mocked Google sign-in, so the queries, the save paths and the toasts are checked without a browser or a network.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html'), sw = H.read('sw.js');
const blk = (src.match(/\/\* DRIVE SYNC BEGIN \*\/([\s\S]*?)\/\* DRIVE SYNC END \*\//) || [])[1];

function make({ clientId = 'cid.apps.googleusercontent.com', desktop = false, routes = [], confirm = true, openOk = true } = {}) {
  const calls = [], toasts = [], opened = [], store = {}, tokens = { n: 0 };
  const win = {};
  const sb = {
    console, JSON, Math, Date, Promise, Error, String, Number, Array, Object, URL, Blob, File, encodeURIComponent, setTimeout,
    window: win, DRIVE_CLIENT_ID: clientId, document: { createElement: () => ({}), head: { appendChild() {} } },
    localStorage: { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { store[k] = String(v); } },
    toast: (m) => toasts.push(m), toastErr: (m) => toasts.push('ERR ' + m),
    flushS() {}, presetSmpFill: async () => {}, smpSaveGate: async () => true, projName: () => 'My Song.thunder', projBlob: () => new Blob(['{"app":"thunder-project"}']),
    askYes: async () => confirm, askPick: async (m, list) => { sb.__picked = list; return 1; },
    openProjectFile: async (f) => { opened.push(f); return openOk; },
    fetch: async (url, opt) => {
      calls.push({ url: String(url), opt: opt || {} });
      for (const r of routes) if (r.test(String(url), opt || {})) { const x = r.reply(String(url), opt || {}); return typeof x === 'function' ? x() : mkRes(x.status || 200, x.body); }
      return mkRes(404, {});
    },
  };
  if (desktop) win.thunderDesktop = {};
  win.google = { accounts: { oauth2: { initTokenClient: (o) => ({ requestAccessToken() { tokens.n++; tokens.opt = o; Promise.resolve().then(() => o.callback({ access_token: 'tok' + tokens.n, expires_in: 3600 })); } }) } } };
  sb.globalThis = sb; vm.createContext(sb);
  vm.runInContext(blk + '\n;globalThis.__x = {driveItems, driveRun, driveSave, driveOpenLatest, driveOpenPick, driveChoose, drv};', sb);
  return { sb, x: sb.__x, calls, toasts, opened, store, tokens };
}
const mkRes = (status, body) => ({ ok: status >= 200 && status < 300, status, json: async () => body, blob: async () => new Blob([typeof body === 'string' ? body : JSON.stringify(body)]) });
const folderRoute = { test: u => u.includes('mimeType') && u.includes('google-apps.folder') && !u.includes('upload'), reply: () => ({ body: { files: [{ id: 'F1' }] } }) };
const dec = u => decodeURIComponent(u);

test('the block exists, the client id const holds a Web client ID (owner, 23 Nov), and the menu lists the four items in both File menus', () => {
  assert.ok(blk, 'DRIVE SYNC block missing');
  assert.match(src, /const DRIVE_CLIENT_ID = '[0-9]+-[a-z0-9]+\.apps\.googleusercontent\.com';/);
  assert.equal((src.match(/\.\.\.driveItems\(\)/g) || []).length, 2);
  const { x } = make(); assert.deepEqual(Array.from(x.driveItems(), i => i.l), ['Save to Drive', 'Open latest from Drive', 'Open from Drive…', 'Drive folder: Thunder Plus…']);
});
test('hidden in the desktop app (window.thunderDesktop)', () => { assert.equal(make({ desktop: true }).x.driveItems().length, 0); });
test('an empty client id toasts the setup message and makes no request', async () => {
  const t = make({ clientId: '' }); await t.x.driveRun(t.x.driveSave);
  assert.match(t.toasts[0], /Google Drive isn’t set up yet \(needs a client ID: see docs\/GOOGLE-DRIVE-SETUP\.md\)/); assert.equal(t.calls.length, 0); assert.equal(t.tokens.n, 0);
});
test('folder: found by name in My Drive root; created when missing; cached after', async () => {
  const t = make({ routes: [folderRoute, { test: u => u.includes('orderBy'), reply: () => ({ body: { files: [{ id: 'A', name: 'a.thunder', modifiedTime: '2026-11-23T10:00:00Z' }] } }) }, { test: (u, o) => u.includes('alt=media'), reply: () => ({ body: '{"app":"thunder-project"}' }) }] });
  await t.x.driveRun(t.x.driveOpenLatest);
  const q = dec(t.calls[0].url); assert.match(q, /mimeType='application\/vnd\.google-apps\.folder' and name='Thunder Plus' and 'root' in parents and trashed=false/);
  await t.x.driveRun(t.x.driveOpenLatest); assert.equal(t.calls.filter(c => dec(c.url).includes("name='Thunder Plus'")).length, 1, 'folder id cached');
  const t2 = make({ routes: [{ test: u => dec(u).includes("name='Thunder Plus'"), reply: () => ({ body: { files: [] } }) }, { test: (u, o) => o.method === 'POST' && !u.includes('upload'), reply: () => ({ body: { id: 'NEW' } }) }, { test: u => u.includes('orderBy'), reply: () => ({ body: { files: [] } }) }] });
  await t2.x.driveRun(t2.x.driveOpenLatest);
  const post = t2.calls.find(c => c.opt.method === 'POST'); const b = JSON.parse(post.opt.body);
  assert.deepEqual([b.name, b.mimeType, b.parents], ['Thunder Plus', 'application/vnd.google-apps.folder', ['root']]); assert.match(dec(t2.calls.at(-1).url), /'NEW' in parents/);
});
test('open latest: newest .thunder in the folder (modifiedTime desc, 1), downloads, opens through openProjectFile, toasts, remembers', async () => {
  const t = make({ routes: [folderRoute, { test: u => u.includes('orderBy'), reply: () => ({ body: { files: [{ id: 'A', name: 'song.thunder', modifiedTime: new Date(Date.now() - 3 * 3600e3).toISOString() }] } }) }, { test: u => u.includes('/A?alt=media'), reply: () => ({ body: '{"x":1}' }) }] });
  await t.x.driveRun(t.x.driveOpenLatest);
  const l = t.calls.find(c => c.url.includes('orderBy')); const q = dec(l.url);
  assert.match(q, /'F1' in parents and name contains '\.thunder' and trashed=false/); assert.match(q, /orderBy=modifiedTime desc/); assert.match(q, /pageSize=1\b/); assert.match(q, /fields=files\(id,name,modifiedTime\)/);
  assert.equal(t.calls.find(c => c.url.includes('alt=media')).opt.headers.Authorization, 'Bearer tok1');
  assert.equal(t.opened.length, 1); assert.equal(t.opened[0].name, 'song.thunder');
  assert.match(t.toasts.at(-1), /^Opened song\.thunder from Thunder Plus \(saved 3 h ago\)$/);
  assert.ok(JSON.parse(t.store['thunderplus-drive-v1']).seen.A);
});
test('open latest: if the normal open path declines (unsaved-changes confirm cancelled) there is no "Opened" toast', async () => {
  const t = make({ openOk: false, routes: [folderRoute, { test: u => u.includes('orderBy'), reply: () => ({ body: { files: [{ id: 'A', name: 's.thunder', modifiedTime: '2026-11-23T10:00:00Z' }] } }) }, { test: u => u.includes('alt=media'), reply: () => ({ body: '{}' }) }] });
  await t.x.driveRun(t.x.driveOpenLatest); assert.equal(t.opened.length, 1); assert.ok(!t.toasts.some(m => /^Opened/.test(m)));
});
test('open from Drive…: lists 30, the chooser pick opens that file', async () => {
  const files = [{ id: 'A', name: 'a.thunder', modifiedTime: '2026-11-23T10:00:00Z' }, { id: 'B', name: 'b.thunder', modifiedTime: '2026-11-22T10:00:00Z' }];
  const t = make({ routes: [folderRoute, { test: u => u.includes('orderBy'), reply: () => ({ body: { files } }) }, { test: u => u.includes('/B?alt=media'), reply: () => ({ body: '{}' }) }] });
  await t.x.driveRun(t.x.driveOpenPick);
  assert.match(t.calls.find(c => c.url.includes('orderBy')).url, /pageSize=30/); assert.equal(t.sb.__picked.length, 2); assert.match(t.sb.__picked[0], /^a\.thunder/);
  assert.equal(t.opened[0].name, 'b.thunder');
});
const listReply = f => ({ test: u => dec(u).includes("name='My Song.thunder'"), reply: () => ({ body: { files: f } }) });
test('save: no same-named file -> multipart create in the folder, remembers id + modifiedTime', async () => {
  const t = make({ routes: [folderRoute, listReply([]), { test: u => u.includes('uploadType=multipart'), reply: () => ({ body: { id: 'N1', modifiedTime: '2026-11-23T12:00:00Z' } }) }] });
  await t.x.driveRun(t.x.driveSave);
  const c = t.calls.find(c => c.url.includes('uploadType=multipart')); assert.equal(c.opt.method, 'POST'); assert.match(c.opt.headers['Content-Type'], /^multipart\/related; boundary=/);
  assert.match(dec(t.calls.find(c => dec(c.url).includes('trashed=false') && c.url.includes('canEdit')).url), /fields=files\(id,name,modifiedTime,capabilities\/canEdit\)/);
  assert.equal(JSON.parse(t.store['thunderplus-drive-v1']).seen.N1, '2026-11-23T12:00:00Z'); assert.match(t.toasts.at(-1), /Saved My Song\.thunder to Drive/);
  const text = await new Response(c.opt.body).text().catch(() => null); if (text) { assert.match(text, /"name":"My Song\.thunder"/); assert.match(text, /"parents":\["F1"\]/); }
});
test('save: an editable same-named file this device has seen is updated in place (PATCH media), no confirm', async () => {
  let asked = 0;
  const t = make({ routes: [folderRoute, listReply([{ id: 'E1', name: 'My Song.thunder', modifiedTime: '2026-11-23T10:00:00Z', capabilities: { canEdit: true } }]), { test: (u, o) => o.method === 'PATCH', reply: () => ({ body: { id: 'E1', modifiedTime: '2026-11-23T12:00:00Z' } }) }] });
  t.sb.askYes = async () => { asked++; return true; };
  t.store['thunderplus-drive-v1'] = JSON.stringify({ seen: { E1: '2026-11-23T10:00:00Z' } });
  await t.x.driveRun(t.x.driveSave);
  const p = t.calls.find(c => c.opt.method === 'PATCH'); assert.ok(p.url.includes('/upload/drive/v3/files/E1?uploadType=media')); assert.equal(asked, 0);
  assert.equal(JSON.parse(t.store['thunderplus-drive-v1']).seen.E1, '2026-11-23T12:00:00Z');
});
test('save: a copy on Drive newer than the one this device saw asks first; No leaves Drive untouched, Yes updates', async () => {
  const mk = confirm => { const t = make({ confirm, routes: [folderRoute, listReply([{ id: 'E1', name: 'My Song.thunder', modifiedTime: '2026-11-23T11:00:00Z', capabilities: { canEdit: true } }]), { test: (u, o) => o.method === 'PATCH', reply: () => ({ body: { id: 'E1', modifiedTime: '2026-11-23T12:00:00Z' } }) }] }); t.store['thunderplus-drive-v1'] = JSON.stringify({ seen: { E1: '2026-11-23T10:00:00Z' } }); return t; };
  let t = mk(false); await t.x.driveRun(t.x.driveSave); assert.ok(!t.calls.some(c => c.opt.method === 'PATCH' || c.opt.method === 'POST'));
  t = mk(true); await t.x.driveRun(t.x.driveSave); assert.ok(t.calls.some(c => c.opt.method === 'PATCH'));
});
test('save: a file that cannot be edited (canEdit false) is not patched; a new copy is created', async () => {
  const t = make({ routes: [folderRoute, listReply([{ id: 'R1', name: 'My Song.thunder', modifiedTime: '2026-11-23T11:00:00Z', capabilities: { canEdit: false } }]), { test: u => u.includes('uploadType=multipart'), reply: () => ({ body: { id: 'N2', modifiedTime: '2026-11-23T12:00:00Z' } }) }] });
  await t.x.driveRun(t.x.driveSave); assert.ok(!t.calls.some(c => c.opt.method === 'PATCH')); assert.ok(t.calls.some(c => c.url.includes('multipart')));
});
test('errors: a 401 gets one new token and one retry; offline and a closed popup only toast, never throw', async () => {
  let n = 0;
  const t = make({ routes: [{ test: u => u.includes('mimeType'), reply: () => () => (++n === 1 ? mkRes(401, {}) : mkRes(200, { files: [{ id: 'F1' }] })) }, { test: u => u.includes('orderBy'), reply: () => ({ body: { files: [] } }) }] });
  await t.x.driveRun(t.x.driveOpenLatest); assert.equal(t.tokens.n, 2, 'a second token was requested'); assert.match(t.toasts.at(-1), /No \.thunder files/);
  const off = make({ routes: [{ test: () => true, reply: () => () => { throw new TypeError('Failed to fetch'); } }] });
  await assert.doesNotReject(off.x.driveRun(off.x.driveOpenLatest)); assert.match(off.toasts.at(-1), /online/);
  const pc = make(); pc.sb.window.google.accounts.oauth2.initTokenClient = o => ({ requestAccessToken() { o.error_callback({ type: 'popup_closed' }); } });
  await assert.doesNotReject(pc.x.driveRun(pc.x.driveSave)); assert.match(pc.toasts.at(-1), /closed/);
  const bad = make({ routes: [{ test: () => true, reply: () => ({ status: 401, body: {} }) }] });
  await assert.doesNotReject(bad.x.driveRun(bad.x.driveOpenLatest)); assert.equal(bad.tokens.n, 2, 'only one retry');
});
test('token: kept in memory until its expiry; first sign-in asks for consent, later ones prompt ""; scopes are the two Drive ones', async () => {
  const t = make({ routes: [folderRoute, { test: u => u.includes('orderBy'), reply: () => ({ body: { files: [] } }) }] });
  await t.x.driveRun(t.x.driveOpenLatest); await t.x.driveRun(t.x.driveOpenLatest); assert.equal(t.tokens.n, 1);
  assert.equal(t.tokens.opt.prompt, 'consent'); assert.equal(t.tokens.opt.scope, 'https://www.googleapis.com/auth/drive');
  t.x.drv.exp = 0; await t.x.driveRun(t.x.driveOpenLatest); assert.equal(t.tokens.n, 2); assert.equal(t.tokens.opt.prompt, '');
});
test('service worker leaves Google sign-in and Drive requests alone; build and cache agree (668 or later)', () => {
  assert.match(sw, /accounts\.google\.com[\s\S]{0,200}www\.googleapis\.com[\s\S]{0,200}return;/); const b = +(/const BUILD = (\d+);/.exec(src) || [])[1]; assert.ok(b >= 668); assert.ok(sw.includes("'thunderplus-v" + b + "'"));
});

const LS = 'thunderplus-drive-v1';
const subRoute = (map) => ({ test: u => dec(u).includes("mimeType='application/vnd.google-apps.folder'") && dec(u).includes(' in parents') && dec(u).includes('orderBy=name'), reply: u => { const m = /'([^']+)' in parents/.exec(dec(u)); return { body: { files: map[m ? m[1] : '@'] || [] } }; } });
test('folder chooser: Into / Up navigation, query shape, Use stores {id,name}; menu label and later queries use it', async () => {
  const map = { root: [{ id: 'B', name: 'Beats' }, { id: 'A', name: 'Albums' }], A: [{ id: 'A2', name: '2026' }] };
  const t = make({ routes: [subRoute(map), { test: u => u.includes('orderBy=modifiedTime'), reply: () => ({ body: { files: [] } }) }] });
  const seen = []; const script = [(l) => l.indexOf('Albums ›'), (l) => l.indexOf('↑ Up'), (l) => l.indexOf('Albums ›'), (l) => l.indexOf('2026 ›'), (l) => 0];
  t.sb.askPick = async (m, l) => { seen.push(Array.from(l)); return script[seen.length - 1](l); };
  await t.x.driveRun(t.x.driveChoose);
  assert.deepEqual(seen[0], ['✓ Use this folder (My Drive)', 'Shared with me ›', 'Albums ›', 'Beats ›'], 'sorted by name, no Up at root');
  assert.deepEqual(seen[1], ['✓ Use this folder (Albums)', '↑ Up', '2026 ›']); assert.equal(seen[2][0], '✓ Use this folder (My Drive)');
  assert.deepEqual(seen[4], ['✓ Use this folder (2026)', '↑ Up']);
  const q = dec(t.calls[0].url); assert.match(q, /'root' in parents and mimeType='application\/vnd\.google-apps\.folder' and trashed=false/); assert.match(q, /orderBy=name/); assert.match(q, /pageSize=200/); assert.match(q, /fields=nextPageToken,files\(id,name\)/);
  assert.deepEqual(JSON.parse(t.store[LS]).folder, { id: 'A2', name: '2026' }); assert.match(t.toasts.at(-1), /Drive folder: 2026/);
  assert.equal(t.x.driveItems()[3].l, 'Drive folder: 2026…');
  await t.x.driveRun(t.x.driveOpenLatest);   // chosen folder verified once, then listed by its id
  assert.ok(t.calls.some(c => /'A2' in parents and name contains/.test(dec(c.url)))); assert.match(t.toasts.at(-1), /No \.thunder files in the “2026” folder/);
});
test('folder chooser: cancel changes nothing; Shared with me lists sharedWithMe and has no Use row; pages are followed', async () => {
  const t = make({ routes: [{ test: u => dec(u).includes('orderBy=name') && !dec(u).includes('pageToken'), reply: u => ({ body: dec(u).includes('sharedWithMe') ? { files: [{ id: 'S', name: 'Shared' }] } : { files: [{ id: 'A', name: 'a' }], nextPageToken: 'P2' } }) }, { test: u => dec(u).includes('pageToken=P2'), reply: () => ({ body: { files: [{ id: 'B', name: 'b' }] } }) }] });
  const seen = []; t.sb.askPick = async (m, l) => { seen.push(Array.from(l)); return seen.length === 1 ? l.indexOf('Shared with me ›') : seen.length === 2 ? -1 : 0; };
  await t.x.driveRun(t.x.driveChoose);
  assert.ok(t.calls.some(c => /sharedWithMe and mimeType/.test(dec(c.url)))); assert.deepEqual(seen[1], ['↑ Up', 'Shared ›']); assert.ok(!(LS in t.store) || !JSON.parse(t.store[LS]).folder);
  const t2 = make({ routes: [{ test: u => dec(u).includes('orderBy=name') && !dec(u).includes('pageToken'), reply: () => ({ body: { files: [{ id: 'A', name: 'a' }], nextPageToken: 'P2' } }) }, { test: u => dec(u).includes('pageToken=P2'), reply: () => ({ body: { files: [{ id: 'B', name: 'b' }] } }) }] });
  let l0; t2.sb.askPick = async (m, l) => { l0 = Array.from(l); return -1; }; await t2.x.driveRun(t2.x.driveChoose); assert.deepEqual(l0.slice(-2), ['a ›', 'b ›']);
});
test('folder: choosing My Drive itself is allowed (id root, no files.get); save queries use it and toasts name it', async () => {
  const t = make({ routes: [listReply([]), { test: u => u.includes('uploadType=multipart'), reply: () => ({ body: { id: 'N1', modifiedTime: '2026-11-23T12:00:00Z' } }) }, subRoute({ root: [] })] });
  t.sb.askPick = async () => 0; await t.x.driveRun(t.x.driveChoose); assert.deepEqual(JSON.parse(t.store[LS]).folder, { id: 'root', name: 'My Drive' });
  const t2 = make({ routes: [listReply([]), { test: u => u.includes('uploadType=multipart'), reply: () => ({ body: { id: 'N1', modifiedTime: '2026-11-23T12:00:00Z' } }) }] }); t2.store[LS] = JSON.stringify({ folder: { id: 'root', name: 'My Drive' } });
  await t2.x.driveRun(t2.x.driveSave); assert.ok(t2.calls.some(c => /'root' in parents and name='My Song\.thunder'/.test(dec(c.url)))); assert.ok(!t2.calls.some(c => c.url.includes('/root?'))); assert.match(t2.toasts.at(-1), /Saved My Song\.thunder to Drive \(My Drive\)/);
});
test('folder: a stored choice is verified once per session; gone (404) or trashed falls back to Thunder Plus with a toast and clears it', async () => {
  const ok = make({ routes: [{ test: u => u.includes('/CH?'), reply: () => ({ body: { id: 'CH', name: 'Renamed', trashed: false } }) }, { test: u => u.includes('orderBy=modifiedTime'), reply: () => ({ body: { files: [] } }) }] }); ok.store[LS] = JSON.stringify({ folder: { id: 'CH', name: 'Old' } });
  await ok.x.driveRun(ok.x.driveOpenLatest); await ok.x.driveRun(ok.x.driveOpenLatest);
  assert.equal(ok.calls.filter(c => c.url.includes('/CH?')).length, 1); assert.match(dec(ok.calls.at(-1).url), /'CH' in parents/); assert.equal(JSON.parse(ok.store[LS]).folder.name, 'Renamed');
  for (const gone of [{ status: 404, body: {} }, { body: { id: 'CH', name: 'Old', trashed: true } }]) {
    const t = make({ routes: [{ test: u => u.includes('/CH?'), reply: () => gone }, folderRoute, { test: u => u.includes('orderBy=modifiedTime'), reply: () => ({ body: { files: [] } }) }] }); t.store[LS] = JSON.stringify({ folder: { id: 'CH', name: 'Old' }, consented: true });
    await t.x.driveRun(t.x.driveOpenLatest);
    assert.ok(t.toasts.some(m => /“Old” is gone or in the trash: using “Thunder Plus”/.test(m))); assert.match(dec(t.calls.find(c => c.url.includes('orderBy=modifiedTime')).url), /'F1' in parents/);
    assert.equal(JSON.parse(t.store[LS]).folder, undefined); assert.equal(JSON.parse(t.store[LS]).consented, true); assert.equal(t.x.driveItems()[3].l, 'Drive folder: Thunder Plus…');
  }
});
