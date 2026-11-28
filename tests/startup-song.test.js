// START-UP SONG (build 674, 24 Nov: "the app stopped opening the song I was working on last"). Cause: from build 660 (20 Nov) normSong
// reached `const OS_KEYS` / `const SY_PICK`, declared far below the saved-song load at start-up. A const read before its line has run
// throws ReferenceError, normSong threw for every saved song, the start-up catch silently opened the starter beat, and the next autosave
// (also the one on pagehide) wrote it over the song. Pinned here:
//   1. a source guard: nothing normSong / demoSong / freshSong reach may be a let / const / class declared below the saved-song load;
//   2. the start-up block run on its own: saved song / first run / unreadable song (kept, never autosaved over, copied aside, reported);
//   3. source pins for every writer of the song key, the release rules, the Restore list, Settings → App, the playback report, the build;
//   4. opt-in (TP_BROWSER_TESTS=1): the real page opens songs from older builds and odd fields, and an unreadable one is kept.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), http = require('http'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');

/* ---------- 1. the temporal-dead-zone guard ---------- */
// Returns [[function reached, name declared too late, line of that declaration]]. Works on column-0 statements; strips comments and strings;
// follows only direct calls `name(`; skips a chunk's own locals, parameters and object keys. A guard, not a parser: it must catch the
// build-660 shape (a late const read inside a function the saved-song load calls) without crying wolf on the current file.
function lateDeclarations(text, roots, startsWith) {
  const lines = text.split('\n'), startIdx = lines.findIndex(l => l.startsWith(startsWith));
  if (startIdx < 0) throw new Error('the saved-song load moved: ' + startsWith);
  const strip = t => t.replace(/\/\*[\s\S]*?\*\//g, ' ').replace(/(^|[^:'"\\])\/\/.*$/gm, '$1').replace(/'(?:\\.|[^'\\\n])*'/g, "''").replace(/"(?:\\.|[^"\\\n])*"/g, '""').replace(/`(?:\\.|[^`\\])*`/g, '``');
  const chunks = []; let cur = null;
  lines.forEach((l, i) => { if (/^[A-Za-z_$]/.test(l)) { cur = { start: i, text: l }; chunks.push(cur); } else if (cur) cur.text += '\n' + l; });
  const declRe = /^(?:async\s+)?(?:function\*?\s+([\w$]+)|(const|let|var|class)\s+([\w$]+))/;
  const funcs = new Map(), late = new Map();
  for (const c of chunks) {
    const m = declRe.exec(c.text); if (!m) continue;
    if (m[1]) { funcs.set(m[1], c); continue; }
    const names = [m[3]];
    if (m[2] !== 'var') { const first = strip(c.text.split('\n')[0]); let d = 0;
      for (let k = 0; k < first.length; k++) { const ch = first[k]; if ('([{'.includes(ch)) d++; else if (')]}'.includes(ch)) d--; else if (ch === ',' && d === 0) { const mm = /^\s*([\w$]+)\s*=/.exec(first.slice(k + 1)); if (mm) names.push(mm[1]); } } }
    for (const n of names) { if (c.start > startIdx && m[2] !== 'var') late.set(n, c); funcs.set(n, c); }
  }
  const seen = new Set(), hits = [], q = [...roots];
  while (q.length) {
    const n = q.pop(); if (seen.has(n)) continue; seen.add(n); const c = funcs.get(n); if (!c) continue;
    const body = strip(c.text), locals = new Set();
    for (const m of body.matchAll(/\b(?:const|let|var|function)\s+\[?\{?\s*([\w$]+(?:\s*,\s*[\w$]+)*)/g)) m[1].split(/\s*,\s*/).forEach(x => locals.add(x));
    for (const m of body.matchAll(/\(([^()]*)\)\s*=>|\b([\w$]+)\s*=>/g)) (m[1] != null ? m[1] : (m[2] || '')).split(/[\s,={}\[\]]+/).forEach(x => x && locals.add(x));
    for (const m of body.matchAll(/function\s*[\w$]*\s*\(([^()]*)\)/g)) m[1].split(/[\s,={}\[\]]+/).forEach(x => x && locals.add(x));
    for (const m of body.matchAll(/for\s*\(\s*(?:const|let)\s+(?:\[([^\]]*)\]|([\w$]+))/g)) (m[1] || m[2] || '').split(/[\s,]+/).forEach(x => x && locals.add(x));
    for (const m of body.matchAll(/(?<![.\w$])([A-Za-z_$][\w$]*)\b(?!\s*:(?!:))(\s*\()?/g)) {
      const w = m[1];
      if (late.has(w) && w !== n && !locals.has(w) && late.get(w) !== c) hits.push([n, w, late.get(w).start + 1]);
      if (m[2] && funcs.has(w) && !seen.has(w)) q.push(w);
    }
  }
  return { hits, visited: seen.size };
}
const ROOTS = ['normSong', 'demoSong', 'freshSong'];
test('nothing the saved-song load reaches (normSong, demoSong, freshSong and what they call) is a const / let / class declared below it', () => {
  const r = lateDeclarations(src, ROOTS, 'let S = (() => {');
  assert.ok(r.visited > 20, 'the guard followed the calls (' + r.visited + ' functions)');
  assert.deepEqual(r.hits, [], 'declared below the saved-song load but read while it runs (a ReferenceError at start-up): ' + JSON.stringify(r.hits)
    + '. Move the declaration above the "let S = (() =>" line (see the STRETCH PICKER DATA block).');
});
test('the guard itself: it flags the build-660 shape and passes a declaration made above the load', () => {
  const bad = ['const A = 1;', 'let S = (() => { return normSong({}); })();', 'function normSong(j){ return sxLoad(j); }', 'function sxLoad(o){ return LATE.some(x => x); }', 'const LATE = [1];'].join('\n');
  assert.deepEqual(lateDeclarations(bad, ['normSong'], 'let S = (() => {').hits, [['sxLoad', 'LATE', 5]]);
  const ok = ['const LATE = [1];', 'let S = (() => { return normSong({}); })();', 'function normSong(j){ return sxLoad(j); }', 'function sxLoad(o){ const Z = 1; return LATE.some(x => x) + Z; }', 'const NOTUSED = 2;'].join('\n');
  assert.deepEqual(lateDeclarations(ok, ['normSong'], 'let S = (() => {').hits, []);
  // and on the real file from before the fix (the commit that was live on 23 Nov), when the repo history has it
  const rev = (() => { try { return require('child_process').execFileSync('git', ['show', 'e8232d7:index.html'], { cwd: H.ROOT, maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] }).toString(); } catch (e) { return null; } })();
  if (rev) { const h = lateDeclarations(rev, ROOTS, 'let S = (() => { try').hits.map(x => x[1]); assert.ok(h.includes('OS_KEYS') && h.includes('SY_PICK'), 'build 673 is flagged: ' + h); }
});

/* ---------- 2. the start-up block, run on its own ---------- */
function startupBlock() {
  const a = src.indexOf('var START = {'), endMark = "tpLog('start-up rescue failed', e); } }, 1200);", b = src.indexOf(endMark, a);
  assert.ok(a > 0 && b > a, 'the start-up block moved');
  return src.slice(a, b + endMark.length);
}
function boot(o) {
  o = o || {};
  const ls = Object.assign({}, o.ls || {}), kv = o.kv || {}, writes = [], log = [], timers = [], els = [];
  const localStorage = { getItem: k => { if (o.readThrows) throw new Error('SecurityError'); return k in ls ? ls[k] : null; }, setItem: (k, v) => { if (o.setThrows && k.includes('rescue')) throw new Error('quota'); ls[k] = String(v); } };
  const doc = { getElementById: id => id === 'appOpened' ? o.appOpened || null : null, createElement: () => { const e = { addEventListener() {}, remove() {} }; els.push(e); return e; }, body: { append() {} } };
  const ctx = vm.createContext({ console, JSON, Math, Date, Promise, Array, Object, String, Error, TypeError, setTimeout: (f, ms) => { timers.push([f, ms]); return timers.length; },
    localStorage, document: doc, performance: { now: () => 0 }, addEventListener() {}, TK: 'thunderplus-song-v1',
    demoSong: () => ({name: 'Starter beat'}), normSong: j => { if (o.normThrows) throw new TypeError('boom'); return {name: j.name || 'Song 1', from: 'normSong'}; },
    songOut: S => ({out: S.name}), storeWrite: (k, v) => { writes.push([k, v]); ls[k] = v; return true; }, tpLog: (w, e) => log.push([w, String(e && e.message || e)]), BUILD: 674,
    kvGet: async k => kv[k], kvSet: async (k, v) => { kv[k] = v; }, kvDel: async k => { delete kv[k]; }, restoreBackup() {}, toastErr() {}, FIRST_SONG: false });
  vm.runInContext('let FIRST_SONG = false; let S = null;', ctx);
  vm.runInContext(startupBlock().replace(/^var START/, 'var START').replace('let S = (() => {', 'S = (() => {'), ctx, { filename: 'start-up block' });
  vm.runInContext('globalThis.__api = {START, getS: () => S, songStore, songRelease, startLine, startHow, startThen, startRescue, FIRST: () => FIRST_SONG, setS: v => { S = v; }}', ctx);
  return { ls, kv, writes, log, timers, els, ctx, api: ctx.__api };
}
const SONG = JSON.stringify({ name: 'MY SONG', bpm: 120 });
test('a saved song is opened and autosaves as before', () => {
  const t = boot({ ls: { 'thunderplus-song-v1': SONG } });
  assert.equal(t.api.getS().name, 'MY SONG'); assert.equal(t.api.START.how, 'saved'); assert.equal(t.api.START.held, false); assert.equal(t.api.FIRST(), false);
  t.api.songStore(); assert.equal(t.writes.length, 1); assert.equal(t.writes[0][0], 'thunderplus-song-v1');
  assert.match(t.api.startLine(), /^Song at start: your saved song/);
});
test('the old Thunder key still opens when the Plus key is empty; nothing saved at all is the first run', () => {
  const t = boot({ ls: { 'thunder-song-v1': SONG } }); assert.equal(t.api.getS().name, 'MY SONG'); assert.equal(t.api.START.how, 'saved');
  const f = boot({}); assert.equal(f.api.getS().name, 'Starter beat'); assert.equal(f.api.START.how, 'first'); assert.equal(f.api.FIRST(), true); assert.equal(f.api.START.held, false);
  assert.match(f.api.startLine(), /first run/);
});
for (const [label, o, raw] of [['JSON that does not parse', {}, '{"name":"X","insts":[{"pad":1,'], ['normSong throwing (the build-660 failure)', { normThrows: true }, SONG], ['a list', {}, '[1,2,3]'], ['null', {}, 'null'], ['a number', {}, '5']]) {
  test('an unreadable saved song (' + label + ') is kept: starter beat shown, nothing written over it, copied aside, reported', async () => {
    const t = boot(Object.assign({ ls: { 'thunderplus-song-v1': raw } }, o));
    assert.equal(t.api.getS().name, 'Starter beat'); assert.equal(t.api.START.how, 'error'); assert.equal(t.api.START.held, true); assert.equal(t.api.FIRST(), false, 'not a first run');
    assert.match(t.api.startLine(), /could not be read/); assert.match(t.api.startLine(), /autosave of this song is OFF/);
    t.api.songStore(); t.api.songStore(); assert.equal(t.writes.length, 0, 'songStore writes nothing while held (autosave, pagehide, reload)');
    assert.equal(t.ls['thunderplus-song-v1'], raw, 'the song key still holds the saved text');
    await t.api.startRescue();
    assert.equal(t.ls['thunderplus-song-rescue-v1'], raw, 'a second localStorage copy');
    const idx = t.kv.rescueIndex; assert.equal(idx.length, 1); assert.equal(typeof t.kv[idx[0].k], 'string', 'an IndexedDB copy');
    let parsed = null; try { parsed = JSON.parse(raw); } catch (e) {}
    assert.equal(idx[0].text, !!parsed && typeof parsed === 'object' && !Array.isArray(parsed), 'a project the list can open only when the text is a song object; else the text is kept to save as a file');
    if (idx[0].text) { const p = JSON.parse(t.kv[idx[0].k]); assert.equal(p.app, 'thunder-project'); assert.deepEqual(p.song, parsed, 'a project the Restore list can open'); } else assert.equal(t.kv[idx[0].k], raw);
    assert.ok(t.log.some(l => /could not be read/.test(l[0])), 'logged with tpLog');
    assert.equal(t.els.length, 1, 'the red message bar is made');
    await t.api.startRescue(); assert.equal(t.kv.rescueIndex.length, 1, 'the same song is not copied twice (every launch runs this)');
  });
}
test('rescue copies are limited to the last three different songs, and a full localStorage does not stop the IndexedDB copy', async () => {
  const kv = {}; let last;
  for (let i = 0; i < 5; i++) { const t = boot({ ls: { 'thunderplus-song-v1': '{"name":"S' + i + '","bpm":' + i + ',' }, kv, setThrows: true }); await t.api.startRescue(); last = t; }
  assert.equal(kv.rescueIndex.length, 3); assert.equal(kv.rescueIndex[0].name, 'S4'); assert.equal(Object.keys(kv).filter(k => /^rescue\d/.test(k)).length, 3, 'old copies are deleted, not leaked');
  assert.equal(last.ls['thunderplus-song-rescue-v1'], undefined, 'the quota failure was swallowed');
});
test('storage that cannot be read: the starter beat, said so, nothing held (nothing can be written either)', () => {
  const t = boot({ readThrows: true }); assert.equal(t.api.getS().name, 'Starter beat'); assert.equal(t.api.START.how, 'nostore'); assert.equal(t.api.FIRST(), false); assert.match(t.api.startLine(), /would not let the app read/);
});
test('autosave comes back when the song is released (a deliberate change, or any song opened); the line says so', () => {
  const t = boot({ ls: { 'thunderplus-song-v1': '{broken' } }); t.api.songRelease('you changed the song');
  assert.equal(t.api.START.held, false); t.api.songStore(); assert.equal(t.writes.length, 1); assert.match(t.api.startLine(), /then: autosave back on \(you changed the song\)/);
});
test('the line follows the share link, the launch file and the first-run song', () => {
  const t = boot({}); t.api.startHow('first', 'nothing saved in this browser yet, so the bundled X.thunder'); assert.match(t.api.startLine(), /first run: nothing saved in this browser yet, so the bundled X/);
  t.api.startThen('opened “X”'); assert.doesNotMatch(t.api.startLine(), /then:/, 'the song the first run opens is not a second event');
  const s = boot({ ls: { 'thunderplus-song-v1': SONG } }); s.api.startHow('share', ''); assert.match(s.api.startLine(), /shared project \(share link\)/);
  const f = boot({ ls: { 'thunderplus-song-v1': SONG } }); f.api.startHow('file', 'a.thunder'); assert.match(f.api.startLine(), /project file opened with the app · a\.thunder/);
  const u = boot({ ls: { 'thunderplus-song-v1': SONG } }); u.api.startThen('restored the backup “Y”'); assert.match(u.api.startLine(), /your saved song · then: restored the backup/);
});

/* ---------- 3. the wiring, pinned as text ---------- */
test('every write of the song key goes through songStore; markChange and setSong release it; autosave, flush and the audio reload use it', () => {
  const direct = [...src.matchAll(/storeWrite\(TK\b/g)]; assert.equal(direct.length, 1, 'only songStore writes the song key');
  assert.match(src, /function songStore\(\)\{ if \(START\.held\) return true; return storeWrite\(TK,/);
  assert.match(src, /function flushS\(\)\{ clearTimeout\(sT\); sPend = false; songStore\(\); \}/);
  assert.equal([...src.matchAll(/sPend = false; songStore\(\); \}, 600\)/g)].length, 2, 'both autosave timers');
  assert.match(src, /okS = songStore\(\);/);
  assert.match(src, /function markChange\(\)\{ if \(!dirtyArmed\) return; if \(START\.held && performance\.now\(\) - START\.inT < 3000\) songRelease\('you changed the song'\);/);
  assert.match(src, /function setSong\(s, msg\)\{ songRelease\('a song was opened'\);/);
  assert.match(src, /for \(const ev of \['pointerdown', 'keydown', 'touchstart', 'input', 'wheel', 'paste'\]\) addEventListener\(ev, \(\) => \{ START\.inT = performance\.now\(\); \}, true\);/);
});
test('Restore a backup lists the unreadable songs first (kept apart from the 10 newest); Settings → App and the playback report carry the line', () => {
  const r = src.slice(src.indexOf('async function restoreBackup()'), src.indexOf('// DESKTOP APP: closing the window'));
  assert.match(r, /kvGet\('rescueIndex'\)/); assert.match(r, /\[\.\.\.resc, \.\.\.\(\(await kvGet\('bkIndex'\)\) \|\| \[\]\)\]/); assert.match(r, /idx\[k\]\.text === false/); assert.match(r, /thunder-unreadable-song\.json/);
  assert.match(src, /id="appBuild"><\/span><\/div>\n\s*<div class="btnrow"><span class="lbl" id="appOpened"><\/span><\/div>/);
  assert.match(src, /const head = `Thunder Plus build \$\{BUILD\} · \$\{navigator\.userAgent\}\\n\$\{startLine\(\)\}\\n/);
  assert.match(src, /startHow\('share', ''\)/); assert.match(src, /startHow\('file', /); assert.match(src, /startHow\('first', 'nothing saved in this browser yet, so the bundled '/);
});
test('the saved song is read after the picker data it needs, and the start-up block comes before the saved song is used', () => {
  const d = src.indexOf('/* ===STRETCH PICKER DATA START==='), e = src.indexOf('/* ===STRETCH PICKER DATA END=== */'), s = src.indexOf('let S = (() => {');
  assert.ok(d > 0 && e > d && s > e, 'SY_PICK / OS_KEYS are declared above the load'); const blk = src.slice(d, e);
  for (const n of ['SY_GROUPS', 'SY_PICK', 'OS_KEYS', 'OS_DEF']) assert.ok(new RegExp('(const |, )' + n + ' = ').test(blk), n + ' is in the data block');
  assert.equal([...src.matchAll(/^const SY_PICK = /gm)].length, 1);
});
/* ---------- duplicate names (review P2-5): a var or function declared twice in the same top-level scope silently wins ---------- */
// The app's body is one big function (an IIFE) inside the main <script>: its var / function names, and each <script>'s own, may each be
// declared once. The PERFORM sizes were set twice (above the saved-song load for normSong, and again in the PERFORM block) until build 698.
let acorn = null; try { acorn = require('acorn'); } catch (e) {}
function duplicateNames(html) {
  const out = [];
  const scan = (body, base) => { const seen = new Map();
    for (const n of body) { const names = n.type === 'VariableDeclaration' ? n.declarations.flatMap(d => d.id.type === 'Identifier' ? [d.id.name] : []) : n.type === 'FunctionDeclaration' ? [n.id.name] : [];
      for (const k of names) { if (seen.has(k)) out.push(k + ' (lines ' + (seen.get(k) + base) + ' and ' + (n.loc.start.line + base) + ')'); else seen.set(k, n.loc.start.line); } } };
  for (const m of html.matchAll(/^<script>\n([\s\S]*?)^<\/script>/gm)) {
    const base = html.slice(0, m.index).split('\n').length, ast = acorn.parse(m[1], { ecmaVersion: 'latest', locations: true });
    scan(ast.body, base);
    for (const n of ast.body) { const f = n.type === 'ExpressionStatement' && n.expression.type === 'CallExpression' && n.expression.callee; if (f && /Function/.test(f.type) && f.body.type === 'BlockStatement') scan(f.body.body, base); }
  }
  return out;
}
test('no var or function is declared twice in the same top-level scope (review P2-5)', { skip: !acorn && 'acorn is not installed (npm i --no-save acorn@8)' }, () => {
  assert.deepEqual(duplicateNames(src), [], 'declared twice; the second silently replaces the first');
});
test('the duplicate-name guard itself: it flags the build-697 shape', { skip: !acorn && 'acorn is not installed' }, () => {
  const html = '<script>\n(() => {\nvar PERF_SCENES = 16, PLFO_MAX = 4;\nfunction a(){}\nvar PERF_SCENES = 16;\n})();\n</script>\n';
  assert.deepEqual(duplicateNames(html), ['PERF_SCENES (lines 3 and 5)']);
});
test('build 674: the number, the cache name and the changelog agree', () => {
  const b = +src.match(/^const BUILD = (\d+);/m)[1]; assert.ok(b >= 674);
  assert.ok(H.read('sw.js').includes("const CACHE = 'thunderplus-v" + b + "'")); assert.ok(src.includes('\n  [' + b + ", '"));
});

/* ---------- 4. opt-in: the real page ---------- */
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page: songs from older builds and with odd fields open as themselves; an unreadable one is kept and not overwritten', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 300000 }, async () => {
  const { chromium } = await import(PW);
  const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    fs.readFile(path.join(H.ROOT, path.normalize(p)), (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': p.endsWith('.html') ? 'text/html' : p.endsWith('.js') ? 'text/javascript' : p.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream' }); r.end(d); }); });
  await new Promise(ok => srv.listen(0, ok));
  const b = await chromium.launch({ executablePath: ['/usr/bin/google-chrome'].find(f => fs.existsSync(f)), args: ['--autoplay-policy=no-user-gesture-required'] });
  const base = JSON.parse(H.read('sounds/ELECTRO.thunder')).song, songs = {};
  const mk = (n, f) => { const s = JSON.parse(JSON.stringify(base)); s.name = n; f(s); songs[n] = JSON.stringify(s); };
  mk('plain', () => {}); mk('oldschool', s => { s.insts[0].sync = 3; s.insts[0].sx = 1; s.insts[0].og = 5; }); mk('unknown-sx', s => { s.insts[0].sync = 1; s.insts[0].sx = 9; });
  mk('v1', s => { s.v = 1; }); mk('v3', s => { s.v = 3; }); mk('no-flx-gat', s => { delete s.flx256; delete s.gat00; });
  mk('nulls', s => { s.perf = null; s.parts = null; s.presets = null; s.fxPre = null; s.tfx = [null, null]; s.mfx = null; s.mst = null; });
  mk('wrong-types', s => { s.perf = 'x'; s.parts = 5; s.presets = 7; s.insts = [{ pad: 'a', cc: 5 }, null, 3]; s.mix = [1, 2]; s.irs = [null, 5]; });
  mk('track-lfo', s => { s.perf = { tl: [[{ sh: 9, rate: 3 }]] }; }); mk('future', s => { s.v = 99; s.zzz = { a: 1 }; s.ntr = 99; s.nmono = 50; s.hmono = 50; });
  mk('nan', s => { s.bpm = 'abc'; s.swing = NaN; s.master = {}; s.tune = [1, 'x', null]; });
  for (const f of fs.readdirSync(path.join(H.ROOT, 'sounds'))) if (f.endsWith('.thunder')) { try { const j = JSON.parse(H.read('sounds/' + f)); if (j.song) { j.song.name = 'file ' + f; songs['file ' + f] = JSON.stringify(j.song); } } catch (e) {} }
  const url = `http://localhost:${srv.address().port}/index.html?seqtest=1`;
  try {
    for (const [name, text] of Object.entries(songs)) {
      const c = await b.newContext({ viewport: { width: 1175, height: 760 } }), page = await c.newPage();
      await page.addInitScript(([t]) => { localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); localStorage.setItem('thunderplus-song-v1', t); }, [text]);
      await page.goto(url); await page.waitForFunction(() => window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(1500);
      assert.equal(await page.evaluate(() => window.__seqTest.ev('S.name')), name, name + ' opens as itself, not the starter beat');
      assert.equal(await page.evaluate(() => window.__seqTest.ev('START.how')), 'saved'); await c.close();
    }
    const raw = '{"name":"BROKEN","bpm":123,"insts":[{"pad":1,';
    const c = await b.newContext({ viewport: { width: 1175, height: 760 } }), page = await c.newPage(), ev = e => page.evaluate(x => window.__seqTest.ev(x), e);
    await page.addInitScript(([t]) => { if (sessionStorage.getItem('seeded')) return; sessionStorage.setItem('seeded', '1'); localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); localStorage.setItem('thunderplus-song-v1', t); }, [raw]);
    await page.goto(url); await page.waitForFunction(() => window.__seqTest); await page.waitForTimeout(3500);
    assert.equal(await ev('S.name'), 'Starter beat'); assert.ok(await page.evaluate(() => !!document.getElementById('rescueB')), 'the red bar'); assert.match(await page.evaluate(() => document.getElementById('appOpened').textContent), /could not be read/);
    await ev('flushS()'); await ev('saveS()'); await page.waitForTimeout(900);
    assert.equal(await page.evaluate(() => localStorage.getItem('thunderplus-song-v1')), raw, 'flush and a programmatic save leave the song key alone');
    assert.equal((await ev("kvGet('rescueIndex')")).length, 1);
    await page.reload(); await page.waitForFunction(() => window.__seqTest); await page.waitForTimeout(2500);
    assert.equal(await page.evaluate(() => localStorage.getItem('thunderplus-song-v1')), raw, 'still there after a reload'); assert.equal((await ev("kvGet('rescueIndex')")).length, 1);
    await page.mouse.click(600, 400); await ev("(S.name = 'EDITED', saveS(), 1)"); await page.waitForTimeout(900);
    assert.equal(await page.evaluate(() => JSON.parse(localStorage.getItem('thunderplus-song-v1')).name), 'EDITED', 'a deliberate change autosaves again'); assert.equal((await ev("kvGet('rescueIndex')")).length, 1, 'the copy stays');
  } finally { await b.close(); srv.close(); }
});
