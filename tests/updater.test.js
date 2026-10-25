// The desktop updater's download check (electron/updater.js): the real download() runs against a fake `electron` and a fake network.
// A download is verified against the SHA-256 GitHub lists for the file; a file that does not match is deleted and never opened.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), os = require('os'), crypto = require('crypto'), Module = require('module');
const H = require('./helpers'), UPD = path.join(H.ROOT, 'electron/updater.js');
const payload = crypto.randomBytes(300000), goodSha = crypto.createHash('sha256').update(payload).digest('hex');

async function run(digest, body) {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'upd-')), ev = { opened: [], boxes: [] };
  const E = { app: { getPath: () => tmp, getVersion: () => '1.0.0', isPackaged: false, quit() {} },
    dialog: { showMessageBox: (...a) => { ev.boxes.push(a[a.length - 1]); return Promise.resolve({ response: 1 }); } },
    shell: { openPath: p => ev.opened.push(p), showItemInFolder: p => ev.opened.push(p), openExternal() {} }, BrowserWindow: { getFocusedWindow: () => null, getAllWindows: () => [] },
    net: { fetch: async url => {
      if (/releases\/latest/.test(url)) return { ok: true, json: async () => ({ tag_name: 'v9.9.9' }) };
      if (/releases\/tags/.test(url)) { const asset = n => Object.assign({ name: n, browser_download_url: 'https://x/' + n, size: payload.length }, digest === undefined ? {} : { digest }); return { ok: true, json: async () => ({ assets: [asset('Thunder-Plus-9.9.9-mac-universal.dmg'), asset('Thunder-Plus-9.9.9-portable.exe')] }) }; }
      let sent = false; return { ok: true, headers: { get: () => String(body.length) }, body: { getReader: () => ({ read: async () => { if (sent) return { done: true }; sent = true; return { done: false, value: new Uint8Array(body) }; } }) } };
    } } };
  const orig = Module._load; Module._load = function (r, ...a) { if (r === 'electron') return E; if (r === 'electron-updater') throw new Error('not installed in tests'); return orig.call(this, r, ...a); };
  delete require.cache[require.resolve(UPD)]; const { setupUpdates } = require(UPD); Module._load = orig;
  const u = setupUpdates({ name: 'T', page: 'p', repo: 'o/r' });
  await u.check(); await new Promise(r => setTimeout(r, 50)); await u.act(); await new Promise(r => setTimeout(r, 400));
  ev.files = fs.readdirSync(tmp); fs.rmSync(tmp, { recursive: true, force: true }); return ev;
}

test('updater: a download that matches GitHub\'s SHA-256 is opened and kept, with no .part left behind', async () => {
  const e = await run('sha256:' + goodSha, payload);
  assert.equal(e.opened.length, 1, 'opened'); assert.ok(!e.files.some(n => n.endsWith('.part')), 'no .part'); assert.equal(e.files.length, 1, 'the finished file is kept');
});
test('updater: a tampered or corrupt download is NEVER opened, is deleted, and the user is told', async () => {
  const e = await run('sha256:' + goodSha, crypto.randomBytes(300000));
  assert.equal(e.opened.length, 0, 'not opened'); assert.deepEqual(e.files, [], 'the file and its .part are deleted'); assert.ok(e.boxes.some(b => /Couldn/.test(b.message || '')), 'the user is told it failed');
});
test('updater: the digest compares case-insensitively', async () => { assert.equal((await run('sha256:' + goodSha.toUpperCase(), payload)).opened.length, 1); });
test('updater: an older release with no digest still downloads and opens (unchanged behaviour)', async () => { assert.equal((await run(undefined, payload)).opened.length, 1); });
test('updater: a malformed or non-sha256 digest is ignored, neither trusted nor a crash', async () => { assert.equal((await run('md5:abc', crypto.randomBytes(300000))).opened.length, 1); });
