// The service worker's precache (review P1-3): tools/precache-check.mjs keeps sw.js in step with what the pages load, and one
// file that fails to fetch no longer stops a new worker from installing (it is taken from the previous cache instead).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), os = require('os'), vm = require('vm'), { spawnSync } = require('child_process');
const H = require('./helpers'), CHECK = path.join(H.ROOT, 'tools/precache-check.mjs');
const run = root => spawnSync(process.execPath, [CHECK, root], { encoding: 'utf8' });

test('precache check: the repo as it is passes', () => { const r = run(H.ROOT); assert.equal(r.status, 0, r.stderr + r.stdout); });

test('precache check: a wrong ?v= in sw.js fails it', () => {
  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'pc-'));
  try {
    for (const n of fs.readdirSync(H.ROOT)) if (n !== 'sw.js' && n !== '.git') fs.symlinkSync(path.join(H.ROOT, n), path.join(tmp, n));
    const sw = fs.readFileSync(path.join(H.ROOT, 'sw.js'), 'utf8'), m = sw.match(/'\.\/master-worklet\.js\?v=(\d+)'/); assert.ok(m, 'sw.js precaches master-worklet.js');
    fs.writeFileSync(path.join(tmp, 'sw.js'), sw.replace(m[0], "'./master-worklet.js?v=" + (Number(m[1]) + 1) + "'"));
    const r = run(tmp); assert.equal(r.status, 1); assert.match(r.stderr, /master-worklet\.js\?v=\d+, but sw\.js precaches/);
  } finally { fs.rmSync(tmp, { recursive: true, force: true }); }
});

test('service worker: one file that 404s still lets the new worker install, with that file taken from the old cache', async () => {
  const src = fs.readFileSync(path.join(H.ROOT, 'sw.js'), 'utf8'), L = {}, stores = new Map(), key = r => typeof r === 'string' ? r : r.url;
  const mkCache = () => { const m = new Map(); return { m, add: async r => { const u = key(r); if (/master-worklet/.test(u)) throw new TypeError('404'); m.set(u, 'net:' + u); }, put: async (r, v) => { m.set(key(r), v); } }; };
  const old = mkCache(); stores.set('thunderplus-v1', old);
  const failing = (src.match(/'(\.\/master-worklet\.js\?v=\d+)'/) || [])[1]; old.m.set(failing, 'old copy');
  const ctx = { self: { addEventListener: (t, f) => { L[t] = f; }, skipWaiting() {}, clients: { claim() {} } }, console: { warn() {} }, Request: function (u) { this.url = u; }, URL, location: { origin: 'x' },
    caches: { open: async n => { if (!stores.has(n)) stores.set(n, mkCache()); return stores.get(n); }, match: async r => { for (const c of stores.values()) if (c.m.has(key(r))) return c.m.get(key(r)); }, keys: async () => [...stores.keys()], delete: async n => stores.delete(n) } };
  vm.runInNewContext(src, ctx);
  let waited; L.install({ waitUntil: p => { waited = p; } });
  await assert.doesNotReject(waited, 'install resolves even though one file failed');
  const name = (src.match(/const CACHE = '([^']+)'/) || [])[1], nu = stores.get(name);
  assert.equal(nu.m.get(failing), 'old copy', 'the failed file is the previous cache\'s copy');
  assert.ok(nu.m.get('./index.html'), 'the rest is precached from the network');
});

test('service worker: offline, a page URL with a query (./?shared=1) is served from the precached page (review P3-7)', async () => {
  const src = fs.readFileSync(path.join(H.ROOT, 'sw.js'), 'utf8'), L = {}, page = 'https://x.test/app/';
  const store = new Map([[page, 'cached page']]), strip = u => u.split('?')[0];
  const ctx = { self: { addEventListener: (t, f) => { L[t] = f; }, skipWaiting() {}, clients: { claim() {} }, registration: { scope: page } }, console, URL, location: { origin: 'https://x.test' },
    fetch: () => Promise.reject(new TypeError('offline')), Request: function (u) { this.url = u; },
    caches: { open: async () => ({ put() {} }), match: async (r, o) => { const u = r.url || r; if (store.has(u)) return store.get(u); if (o && o.ignoreSearch) for (const [k, v] of store) if (strip(k) === strip(u)) return v; } } };
  vm.runInNewContext(src, ctx);
  let res; L.fetch({ request: { url: page + '?shared=1', method: 'GET', mode: 'navigate' }, respondWith: p => { res = p; } });
  assert.equal(await res, 'cached page');
});
