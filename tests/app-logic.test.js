// Tests for logic that lives in the app's HTML and page scripts (no DOM or audio device needed): the code under test is cut out of the
// real files at run time, so these fail if the files change in a way that breaks it, and cannot drift from a copy.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');

/* Cut one block out of a source file by a start and end pattern, failing loudly (not skipping) if the app no longer contains it. */
function cut(rel, re, what) { const m = H.read(rel).match(re); assert.ok(m, `${rel}: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
function sandbox(extra) { const c = Object.assign({ console: { warn() {}, log() {} }, JSON, Math, Date, Set, Map, Error, String, Number, Array, Object, Promise, URL, Headers, Response, Uint8Array, Float32Array }, extra); vm.createContext(c); return c; }

/* ───────── the page logger: tpLog + the global error handlers (index.html) ───────── */
test('page logger: dedupes identical messages, caps a flood, survives odd inputs, and registers the global handlers', () => {
  const blk = cut('index.html', /\(function\(\)\{\n  const seen = new Set\(\), logs = \[\]; let n = 0;[\s\S]*?\n\}\)\(\);/, 'the tpLog block');
  const warns = [], on = {}, win = { console: { warn: (...a) => warns.push(a.join(' ')) }, addEventListener: (t, f) => { on[t] = f; } }; win.window = win;
  const c = sandbox(win); vm.runInContext(blk.replace(/\bconsole\b/g, 'window.console').replace(/(^|[^.\w])addEventListener/g, '$1window.addEventListener'), c);
  c.tpLog = win.tpLog; c.tpLogs = win.tpLogs;   // the block publishes on `window`, which here is `win`
  c.tpLog('a', new Error('boom')); c.tpLog('a', new Error('boom')); assert.equal(warns.length, 1, 'identical message printed once');
  c.tpLog('b', 'plain string'); c.tpLog('c', undefined); c.tpLog('d', null); assert.equal(warns.length, 4, 'strings, undefined and null do not throw');
  for (let i = 0; i < 1000; i++) c.tpLog('flood', 'msg' + i);
  assert.ok(warns.length <= 200 && c.tpLogs.length <= 200, 'a flood is capped at 200 (got ' + warns.length + ')');
  assert.equal(typeof on.error, 'function'); assert.equal(typeof on.unhandledrejection, 'function');
  const before = warns.length; on.unhandledrejection({ reason: new Error('late') }); assert.equal(warns.length, before, 'the cap also holds for handler-driven logs');
});

/* ───────── the pump window warn-once guard (pump/ui/kit.js) ───────── */
test('pump kit: a draw that throws every animation frame reports once, not 60 times a second', () => {
  const blk = cut('pump/ui/kit.js', /const warnSeen = new Set\(\);\nfunction warnOnce\(where, e\)\{[\s\S]*?\n\}\n/, 'warnOnce');
  const mk = tp => { const out = []; const w = { console: { warn: (...a) => out.push('console:' + a.join(' ')) } }; if (tp) w.tpLog = (wh, e) => out.push('tpLog:' + wh + '|' + (e && e.message)); w.window = w; const c = sandbox(w); vm.runInContext(blk + '\nthis.warnOnce=warnOnce;', c); return { c, out }; };
  let { c, out } = mk(true); for (let f = 0; f < 600; f++) c.warnOnce('pump window draw (eq)', new Error('x is undefined')); assert.equal(out.length, 1, '600 identical frames -> 1 report'); assert.match(out[0], /^tpLog:/);
  c.warnOnce('pump window draw (eq)', new Error('different')); c.warnOnce('pump window draw (comp)', new Error('x is undefined')); assert.equal(out.length, 3, 'a different error or window is still reported');
  ({ c, out } = mk(true)); for (let i = 0; i < 1000; i++) c.warnOnce('w', new Error('d' + i)); assert.equal(out.length, 50, 'at most 50 distinct reports');
  ({ c, out } = mk(false)); c.warnOnce('tick', new Error('boom')); assert.match(out[0], /^console:\[thunder\]/, 'falls back to a prefixed console.warn');
  ({ c, out } = mk(false)); c.console.warn = () => { throw new Error('broken'); }; assert.doesNotThrow(() => c.warnOnce('x', new Error('e')), 'a broken console cannot take the animation loop down');
});

/* ───────── PatchStore, in both editors (the dirty flag after a failed hand-off) ───────── */
for (const file of ['waves/index.html', 'chains/index.html']) {
  test(`${file}: PatchStore keeps a failed hand-off dirty so it is retried, and clears it only when the page really has the patch`, () => {
    const cls = cut(file, /class PatchStore \{[\s\S]*?\n\}\n/, 'class PatchStore');
    const make = (embed, { failHand = () => false, lsFull = false } = {}) => {
      const store = {}, calls = { hand: 0 }, logs = [];
      const c = sandbox({ STORE_KEY: 'k', BAK_KEY: 'b', MAX_BAK: 5, EMBED: embed, setTimeout: () => 0, clearTimeout() {}, tpLog: w => logs.push(w),
        localStorage: { getItem: k => (k in store ? store[k] : null), setItem: (k, v) => { if (lsFull) throw new Error('QuotaExceededError'); store[k] = v; } },
        TW: embed ? { save: () => { calls.hand++; if (failHand(calls.hand)) throw new Error('hand-off failed'); } } : null });
      vm.runInContext(cls + '\nthis.PatchStore=PatchStore;', c); return { p: new c.PatchStore({ meta: {}, modules: [] }), calls, logs, store };
    };
    let t = make(true); t.p.dirty = true; t.p.save(); assert.equal(t.p.dirty, false, 'embedded, success: clean'); assert.equal(t.calls.hand, 1);
    t = make(true, { failHand: () => true }); t.p.dirty = true; t.p.save(); assert.equal(t.p.dirty, true, 'embedded, FAILED hand-off stays dirty (this was the bug)'); assert.match(t.logs[0], /retried/);
    t = make(true, { failHand: n => n === 1 }); t.p.dirty = true; t.p.save(); t.p.save(); assert.equal(t.p.dirty, false, 'a retry that works clears it'); assert.equal(t.calls.hand, 2);
    t = make(true); t.p.save(); assert.equal(t.calls.hand, 0, 'not dirty: nothing to do');
    t = make(true, { lsFull: true }); t.p.dirty = true; t.p.save(); assert.equal(t.p.dirty, false, 'local storage full but the page has it: clean');
    t = make(true, { failHand: () => true }); t.p.dirty = true; for (let i = 0; i < 50; i++) t.p.save(); assert.equal(t.calls.hand, 50, 'a persistent failure: one attempt per trigger, no internal loop');
    t = make(false); t.p.dirty = true; t.p.save(); assert.equal(t.p.dirty, false, 'standalone: a good write clears it'); assert.equal(t.calls.hand, 0);
    t = make(false, { lsFull: true }); t.p.dirty = true; t.p.save(); assert.equal(t.p.dirty, true, 'standalone: full storage stays dirty');
  });
}

/* ───────── the sample-memory prune, including the IRDATA leak fix (index.html) ───────── */
test('sample prune: frees impulse responses nothing refers to, keeps those the song, undo history or presets do', () => {
  const blk = cut('index.html', /setInterval\(\(\) => \{\n  try \{\n    if \(\(typeof tp !== 'undefined' && tp\.on\) \|\| smpHeld\.size < 8\) return;[\s\S]*?\n\}, 60000\);/, 'the 60 s prune');
  const body = blk.replace(/^setInterval\(/, '(').replace(/, 60000\);$/, ')');
  const run = state => { const logs = []; const c = sandbox(Object.assign({ tp: { on: false }, kit: { voices: [] }, undo: { past: [], future: [] }, S: { presets: {}, irs: [] }, smpHeld: new Map(), IRDATA: new Map(), tpLog: w => logs.push(w) }, state)); vm.runInContext('this.cb=' + body, c); c.cb(); c.logs = logs; return c; };
  const fill = (m, ids) => ids.forEach(i => m.set(i, new Float32Array(4))), held = () => { const h = new Map(); fill(h, 'abcdefgh'.split('')); return h; };
  let ir = new Map(); fill(ir, ['cur', 'old1', 'old2']); let c = run({ smpHeld: held(), IRDATA: ir, S: { presets: {}, irs: [{ L: { ref: 'cur' } }] } });
  assert.ok(c.IRDATA.has('cur') && !c.IRDATA.has('old1') && !c.IRDATA.has('old2'), 'current kept, previous songs freed (the leak)');
  ir = new Map(); fill(ir, ['L1', 'R1', 'zz']); c = run({ smpHeld: held(), IRDATA: ir, S: { presets: {}, irs: [{ L: { ref: 'L1' }, R: { ref: 'R1' } }] } }); assert.ok(c.IRDATA.has('L1') && c.IRDATA.has('R1') && !c.IRDATA.has('zz'), 'both channels kept');
  ir = new Map(); fill(ir, ['inUndo', 'orphan']); c = run({ smpHeld: held(), IRDATA: ir, undo: { past: [{ k: 'S', j: '{"irs":[{"L":{"ref":"inUndo"}}]}' }], future: [] } }); assert.ok(c.IRDATA.has('inUndo') && !c.IRDATA.has('orphan'), 'an IR that undo can bring back survives');
  ir = new Map(); fill(ir, ['x']); assert.ok(run({ smpHeld: new Map(), IRDATA: ir }).IRDATA.has('x'), 'under 8 held samples: untouched (the original early return)');
  ir = new Map(); fill(ir, ['x']); assert.ok(run({ smpHeld: held(), IRDATA: ir, tp: { on: true } }).IRDATA.has('x'), 'while playing: untouched');
  assert.equal(run({ smpHeld: held(), IRDATA: undefined }).logs.length, 0, 'IRDATA not declared yet: no error');
  assert.match(run({ smpHeld: held(), S: null }).logs[0], /trim the held samples/, 'a failure is logged, not swallowed');
});

/* ───────── the escaping helper that every dynamic innerHTML relies on ───────── */
test('esc(): escapes the characters that can break out of text or a quoted attribute', () => {
  const c = sandbox({}); vm.runInContext(cut('index.html', /const esc = s => String\(s\)\.replace\(\/\[&<>"\]\/g[^\n]*\n/, 'esc'), c); const esc = vm.runInContext('esc', c);
  assert.equal(esc('<script>alert(1)</script>'), '&lt;script&gt;alert(1)&lt;/script&gt;'); assert.equal(esc('" onmouseover="x'), '&quot; onmouseover=&quot;x'); assert.equal(esc('R&B'), 'R&amp;B');
  assert.equal(esc(null), 'null'); assert.equal(esc(0), '0'); assert.equal(esc('plain'), 'plain');
  assert.ok(!/[<>"]/.test(esc('<img src=x onerror="a">')), 'no raw angle bracket or quote survives');
});

/* ───────── MIXER SIZE (Project): its own size, separate from TEXT SIZE ───────── */
test('mixer size: defaults to 100 %, honours a stored value, and ignores a missing, garbled or out-of-range one', () => {
  const blk = cut('index.html', /const MSK = 'thunderplus-mixsize-v1';[\s\S]*?let mixScale = \(\(\) => \{[\s\S]*?\}\)\(\);\n/, 'the mixer size block');
  const value = raw => { const c = sandbox({ localStorage: { getItem: () => raw } }); vm.runInContext(blk + '\nthis.mixScale=mixScale;this.mixScaleDef=mixScaleDef;', c); return { v: c.mixScale, d: c.mixScaleDef() }; };
  assert.deepEqual(value(null), { v: 100, d: 100 }, 'nothing stored: 100 % (fills the screen; the old 80 % Desktop text size no longer applies to the Mixer)');
  assert.equal(value('150').v, 150, 'a stored size is used');
  assert.equal(value('60').v, 60); assert.equal(value('200').v, 200, 'both ends of the range');
  for (const bad of ['40', '210', '', 'abc', 'NaN']) assert.equal(value(bad).v, 100, 'out of range or garbled (' + JSON.stringify(bad) + ') falls back to the default');
  const c = sandbox({ localStorage: { getItem: () => { throw new Error('blocked'); } } }); vm.runInContext(blk + '\nthis.mixScale=mixScale;', c);
  assert.equal(c.mixScale, 100, 'blocked storage does not throw');
});

test('phrase and pattern text size stays fixed on resize and refits when TEXT SIZE changes', () => {
  const blk = cut('index.html', /let phraseFitBase = null, phraseFitScale = -1;\nfunction fit\(\)\{[\s\S]*?\n\}\n/, 'phrase fit');
  let pvCalls = 0;
  const c = sandbox({ view: 'tracker', tv: 'phrase', fitTries: 0, layoutSig: '', scr: { clientWidth: 824, clientHeight: 560, style: { fontSize: '' } },
    grid: { clientHeight: 420 }, spActive: () => false, spCols: () => 0, FITROWS: 18, BASEW: 52, CW: 0.6, mixScale: 100, tScale: 100,
    pvActive: () => true, pvFit: fs => { pvCalls++; return Math.min(fs, 12); } });
  vm.runInContext(blk + '\nthis.runFit=fit;', c);
  c.runFit(); const initial = c.scr.style.fontSize;
  c.scr.clientWidth = 1400; c.scr.clientHeight = 1000; c.grid.clientHeight = 850; c.runFit();
  assert.equal(c.scr.style.fontSize, initial, 'resizing preserves the fitted phrase/pattern size'); assert.equal(pvCalls, 1, 'pattern fit runs once, not on resize');
  c.tScale = 150; c.runFit(); const scaled = c.scr.style.fontSize; assert.notEqual(scaled, initial, 'changing TEXT SIZE refits'); assert.equal(pvCalls, 2);
  c.scr.clientWidth = 1800; c.scr.clientHeight = 1400; c.grid.clientHeight = 1200; c.runFit(); assert.equal(c.scr.style.fontSize, scaled, 'resizing after TEXT SIZE preserves the new size');
});

test('pattern controls are in the title line and the KEYS command bar stays below the tracker', () => {
  const src = H.read('index.html');
  assert.match(src, /pvSeg\.after\(pvBar\)/); assert.doesNotMatch(src, /grid\.before\(pvBar\)/);
  assert.match(src, /if \(bar\.parentNode !== cmdHome\) cmdHome\.insertBefore\(bar, cmdHomeNext\)/);
  assert.doesNotMatch(src, /scr\.insertBefore\(bar, info\)/);
});

/* ───────── the build number (owner, 25 Oct): one number, shown in the View / Project menus ───────── */
test('build number: the page\'s BUILD is the service-worker cache number, so "which build is this?" cannot lie', () => {
  const build = cut('index.html', /const BUILD = (\d+);/, 'const BUILD');
  const n = Number(build.match(/(\d+)/)[1]);
  const cache = cut('sw.js', /const CACHE = 'thunderplus-v(\d+)'/, 'the CACHE name');
  assert.equal(n, Number(cache.match(/v(\d+)/)[1]), 'index.html says build ' + n + ' but sw.js caches v' + cache.match(/v(\d+)/)[1] + ': bump both together');
  assert.ok(n > 0);
});

/* ───────── the Electron CSP header (electron/main.js) ───────── */
test('electron CSP: report-only, no outside origins, wasm and blob allowed, and the response is passed through intact', async () => {
  const src = H.read('electron/main.js'), csp = src.match(/const CSP = \[[\s\S]*?\]\.join\('; '\);/)[0], wc = src.match(/const withCsp = res => \{[^\n]*\};/)[0];
  const c = sandbox({}); vm.runInContext(csp + '\n' + wc + '\nthis.withCsp=withCsp;this.CSP=CSP;', c);
  const pol = c.CSP.split('; ').reduce((o, d) => { const [k, ...v] = d.split(' '); o[k] = v; return o; }, {});
  assert.ok(!/https?:|\*/.test(c.CSP.replace(/'self'/g, '')), 'no outside origins, no wildcards'); assert.ok(pol['script-src'].includes("'wasm-unsafe-eval'") && pol['script-src'].includes('blob:'));
  assert.ok(!pol['script-src'].includes("'unsafe-eval'"), "no 'unsafe-eval'"); assert.deepEqual([pol['object-src'][0], pol['base-uri'][0], pol['form-action'][0]], ["'none'", "'none'", "'none'"]);
  const html = Buffer.from(H.read('index.html')), out = await c.withCsp(new Response(html, { status: 200, statusText: 'OK', headers: { 'content-type': 'text/html; charset=utf-8', 'x-keep': '1' } }));
  assert.equal(out.status, 200); assert.equal(out.headers.get('content-type'), 'text/html; charset=utf-8'); assert.equal(out.headers.get('x-keep'), '1');
  assert.equal(out.headers.get('content-security-policy-report-only'), c.CSP); assert.equal(out.headers.get('content-security-policy'), null, 'report-only, so nothing can be blocked');
  assert.ok(Buffer.from(await out.arrayBuffer()).equals(html), 'body byte-identical'); assert.equal((await c.withCsp(new Response(null, { status: 404 }))).status, 404);
});

/* ───────── the service worker's file list against the files that exist ───────── */
test('service worker: every precached file exists, and every versioned asset the pages load carries the version the worker caches', () => {
  const fs = require('fs'), path = require('path'), sw = H.read('sw.js'), files = [...sw.matchAll(/'\.\/([^']+)'/g)].map(m => m[1]).filter(f => !f.includes('lxr/'));
  const missing = files.map(f => f.replace(/\?.*$/, '')).filter(f => f && !fs.existsSync(path.join(H.ROOT, f))); assert.deepEqual(missing, [], 'precached but missing: ' + missing.join(', '));
  const ver = (s, name) => (s.match(new RegExp(name.replace(/[.\/]/g, m => '\\' + m) + '\\?v=(\\d+)', 'g')) || []).map(x => x.split('=')[1]);
  for (const name of ['waves-worklet.js', 'chains-worklet.js', 'master-worklet.js', 'pump/ui/kit.js']) {
    const inSw = new Set(ver(sw, name)), pages = new Set([...ver(H.read('index.html'), name), ...ver(H.read('waves/index.html'), name), ...ver(H.read('chains/index.html'), name)]);
    assert.ok(inSw.size === 1, name + ': one version in sw.js (' + [...inSw] + ')'); assert.deepEqual([...pages].filter(v => !inSw.has(v)), [], name + ': a page loads a version the worker does not cache (a stale worklet next to a new page)');
  }
});

/* ───────── opening a project from the phone: Files / email / share sheet ───────── */
test('phone file handling: .thunder files open with the installed app, and a shared project reaches the page', () => {
  const m = JSON.parse(H.read('manifest.webmanifest'));
  const fh = (m.file_handlers || []).find(h => h.action === './');
  assert.ok(fh, 'manifest: a file_handlers entry for the app page');
  assert.ok(fh.accept['application/octet-stream'].includes('.thunder'), 'manifest: .thunder as application/octet-stream (what an unknown extension arrives as)');
  assert.ok(fh.accept['application/json'].includes('.thunder'), 'manifest: .thunder as application/json');
  const sh = m.share_target || {}, sf = ((sh.params || {}).files || []).find(f => f.name === 'project');
  assert.equal(sh.method, 'POST'); assert.equal(sh.enctype, 'multipart/form-data');
  assert.ok(sf && sf.accept.includes('.thunder') && sf.accept.includes('application/octet-stream'), 'manifest: the share target takes .thunder');
  const src = H.read('index.html');
  assert.match(src, /launchQueue\.setConsumer/, 'index.html: file_handlers files are handed to loadProjFile');
  assert.ok(src.includes("$('projIn').accept = '*/*'"), 'index.html: the phone picker shows unknown file types instead of hiding .thunder');
  assert.match(src, /openSharedProject/, 'index.html: a share_target project is read from the cache');
  const sw = H.read('sw.js');
  assert.match(sw, /share-target/, 'sw.js: receives the share_target POST');
  assert.match(sw, /thunder-share/, 'sw.js: parks the shared file for the page');
  assert.doesNotMatch(sw, /'thunderplus-share'/, 'sw.js: the waiting-project cache must not be deleted by the activate clean-up');
});

/* ───────── …chains bus routing (owner's report, 25 Oct) ─────────
   The plan decides which tracks a chain node hears as buses. Three rules, confirmed with the owner:
   1. a Chain Out for a track that has its own Track In is that track's own output, not a bus;
   2. a track with a Track In and no Chain Out of its own is a "pure" source: muted, heard only through its bus;
   3. both the source and the bus still need CHN on. */
function planOf(mods, cables, onTracks, NT = 16) {
  const S = { chainPatch: { format: 'chains.patch', modules: mods, cables }, chainOn: new Array(NT).fill(0) };
  for (const t of onTracks) S.chainOn[t - 1] = 1;
  const c = sandbox({ S, NT, int: (v, d) => { const n = parseInt(v, 10); return Number.isFinite(n) ? n : d; }, busPlanVal: null, busPlanH: 0 });
  vm.runInContext(cut('index.html', /function chainsTracks\(\)\{[\s\S]*?\n\}\n/, 'chainsTracks') + cut('index.html', /const chainsFeed = [^\n]*\n/, 'chainsFeed') + cut('index.html', /function busPlan\(\)\{[\s\S]*?\n\}\n/, 'busPlan') + '\nthis.plan=busPlan;', c);
  const P = c.plan();
  // Host-realm copies (Array.from), not the vm's arrays: deepStrictEqual also compares prototypes, and a cross-realm array
  // has a different Array.prototype, which fails even when the contents are identical.
  return { feeds: Array.from(P.feeds, ([b, src]) => [b + 1, Array.from(src, x => x + 1)]), targets: Array.from(P.targets, ([t, b]) => [t + 1, Array.from(b, x => x + 1)]), pure: Array.from(P.pure, x => x + 1), loop: P.loop };
}
const T = (id, type, track) => ({ id, type, params: track == null ? {} : { track } });
const Cb = (a, b) => ({ from: [a, 'out'], to: [b, 'in'] });

test('bus: Track In 1+2 -> mixer -> compressor -> Chain Out 7 is a bus: both muted, both heard on 7', () => {
  const p = planOf([T('i1', 'trackIn', 1), T('i2', 'trackIn', 2), T('mx', 'mixer'), T('cp', 'compressor'), T('o7', 'chainOut', 7)],
    [Cb('i1', 'mx'), Cb('i2', 'mx'), Cb('mx', 'cp'), Cb('cp', 'o7')], [1, 2, 7]);
  assert.deepEqual(p.feeds, [[7, [1, 2]]], 'bus 7 is fed by 1 and 2');
  assert.deepEqual(p.pure, [1, 2], 'both sources are muted on their own');
  assert.deepEqual(p.targets, [[1, [7]], [2, [7]]], 'the Mixer shows BS for both');
  assert.equal(p.loop, false);
});

test('send: tracks 4 and 5 keep their own outs and also feed bus 6, with no false loop', () => {
  const p = planOf([T('i4', 'trackIn', 4), T('i5', 'trackIn', 5), T('mx', 'mixer'), T('rv', 'reverb'), T('o4', 'chainOut', 4), T('o5', 'chainOut', 5), T('o6', 'chainOut', 6)],
    [Cb('i4', 'mx'), Cb('i5', 'mx'), Cb('mx', 'rv'), Cb('rv', 'o4'), Cb('rv', 'o5'), Cb('rv', 'o6')], [4, 5, 6]);
  assert.deepEqual(p.feeds, [[6, [4, 5]]], 'only track 6 is a bus; Chain Out 4 and 5 are the tracks\' own outputs');
  assert.deepEqual(p.pure, [], 'nothing is muted: both keep their outs, so this is a send');
  assert.equal(p.loop, false, 'the rule removes the false feedback loop the old plan found here');
});

test('bus: a whole-bus feed still works when the sources have no outputs of their own', () => {
  const p = planOf([T('i4', 'trackIn', 4), T('i5', 'trackIn', 5), T('mx', 'mixer'), T('rv', 'reverb'), T('o6', 'chainOut', 6)],
    [Cb('i4', 'mx'), Cb('i5', 'mx'), Cb('mx', 'rv'), Cb('rv', 'o6')], [4, 5, 6]);
  assert.deepEqual(p.feeds, [[6, [4, 5]]]); assert.deepEqual(p.pure, [4, 5]);
});

test('bus routing needs CHN on both ends: the bus off means nothing is routed (this is the MISS the Mixer showed)', () => {
  const mods = [T('i1', 'trackIn', 1), T('i2', 'trackIn', 2), T('mx', 'mixer'), T('cp', 'compressor'), T('o7', 'chainOut', 7)], cables = [Cb('i1', 'mx'), Cb('i2', 'mx'), Cb('mx', 'cp'), Cb('cp', 'o7')];
  assert.deepEqual(planOf(mods, cables, [1, 2]).feeds, [], 'sources on, bus off: no routing');
  assert.deepEqual(planOf(mods, cables, [7]).feeds, [], 'bus on, sources off: no routing');
  assert.deepEqual(planOf(mods, cables, [1, 2, 7]).feeds, [[7, [1, 2]]], 'both on: routed');
});

test('CHN: switching it on leaves a track that already has I/O in the patch alone (no second Track In, no added Chain Out)', () => {
  const ensure = cut('index.html', /function ensureChainPair\(t\)\{[\s\S]*?\n\}\n/, 'ensureChainPair');
  const mk = mods => { const S = { chainPatch: { format: 'chains.patch', modules: mods, cables: [] } }, CH = {};
    const c = sandbox({ S, CH, Date, chainsBottom: () => 0 }); vm.runInContext(ensure + '\nthis.ensure=ensureChainPair;this.S=S;this.CH=CH;', c); return c; };

  // a pure bus source: a Track In, no Chain Out. Enabling CHN must not give it an output.
  let c = mk([{ id: 'tin1', type: 'trackIn', params: { track: 1 } }]);
  assert.equal(c.ensure(0), false, 'nothing added'); assert.equal(c.S.chainPatch.modules.length, 1, 'still just the Track In (was: a second Track In plus a Chain Out)');

  // a normal pair: no duplicate either
  c = mk([{ id: 'tin1', type: 'trackIn', params: { track: 1 } }, { id: 'tou1', type: 'chainOut', params: { track: 1 } }]);
  assert.equal(c.ensure(0), false); assert.equal(c.S.chainPatch.modules.length, 2);

  // untouched track: still gets the straight-through pair, so one switch from the Mixer is all it takes
  c = mk([]);
  assert.equal(c.ensure(3), true, 'pair added'); assert.equal(c.S.chainPatch.modules.length, 2);
  const [a, b] = c.S.chainPatch.modules;
  assert.deepEqual([a.type, a.params.track, b.type, b.params.track], ['trackIn', 4, 'chainOut', 4], 'Track In + Chain Out for track 4');
  assert.equal(c.S.chainPatch.cables.length, 2, 'left and right wired through');
  assert.equal(c.CH.doc, c.S.chainPatch, 'the editor is given the patch');
});

/* ───────── the two worklets share one block, kept in step by tools/sync-shared.py ───────── */
test('shared blocks: waves and chains carry byte-identical SHARED MODULES and SHARED EDITOR blocks (run tools/sync-shared.py if this fails)', () => {
  const blk = (rel, tag) => cut(rel, new RegExp('/\\* ===' + tag + ' START===[\\s\\S]*?/\\* ===' + tag + ' END=== \\*/\\n'), 'the ' + tag + ' block');
  assert.equal(blk('chains/chains-worklet.js', 'SHARED MODULES'), blk('waves/waves-worklet.js', 'SHARED MODULES'), 'DSP block differs: edit chains/ (the source of truth) and run python3 tools/sync-shared.py');
  assert.equal(blk('chains/index.html', 'SHARED EDITOR'), blk('waves/index.html', 'SHARED EDITOR'), 'editor block differs: edit chains/ and run python3 tools/sync-shared.py');
});
