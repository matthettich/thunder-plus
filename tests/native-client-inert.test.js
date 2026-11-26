// PC1 of the native page client (docs/port/N2-PAGE-CLIENT.md sections 4.3 and 5): with no window.thunderCore and Settings > Audio > Sequencer
// not on "Native core (test)", the page behaves as before: no branch is taken, nothing is fetched, no new global appears at load.
// Parts (a) behaviour of the pieces cut out of index.html, (b) source guards that count the branches, (c) loadClient is never called with the switch off.
// Also: the client's hello handshake against a fake thunderCore, and the service worker entry.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html'), sw = H.read('sw.js');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'not found: ' + (what || re)); return m[0]; };

// The pieces PC1 adds or touches, as they stand in index.html.
const LOADER = cut(/\/\* NATIVE CORE CLIENT \(PC1[\s\S]*?document\.head\.appendChild\(s\); \};\n/, 'the NC / NCON / loadClient block');
const SRGUESS = cut(/let srGuess = 48000;\ntry \{[\s\S]*?\n\} catch\(e\)\{\}\n/, 'the srGuess block');
const PADSR = cut(/const padSR = [^\n]*\n/, 'padSR');
const CALL = cut(/\nif \(window\.thunderCore \|\| aset\.core === 'native'\) loadClient\(\);[^\n]*\n/, 'the loadClient call site').trim();

// A page-like sandbox: `window` is the sandbox itself, so window.thunderCore / window.__thunderClient are plain properties of it.
function rig({ core = 'core', thunderCore = null, rate = 0, withCtx = null } = {}) {
  const log = { ac: 0, scripts: [], appended: [], fetches: 0, creates: [], tp: [] };
  const sb = { aset: { core, rate }, BUILD: 678, tpLog: (...a) => log.tp.push(a.join(' ')), latOf: () => 'interactive', ctx: withCtx,
    fetch: () => { log.fetches++; return Promise.reject(new Error('no network in tests')); },
    XMLHttpRequest: function () { log.fetches++; },
    AudioContext: function () { log.ac++; this.sampleRate = 44100; this.close = () => Promise.resolve(); },
    document: { createElement: tag => { log.creates.push(tag); const e = { tag }; if (tag === 'script') log.scripts.push(e); return e; }, head: { appendChild: e => log.appended.push(e) } },
    Promise, Error, JSON, Object, Array, isFinite };
  sb.window = sb; if (thunderCore) sb.thunderCore = thunderCore;
  vm.createContext(sb); sb.__log = log;
  return sb;
}
const run = (sb, code) => vm.runInContext(code, sb);

// ---- (a) behaviour with the switch off -----------------------------------------------------------------------------------------------
test('(a) switch off, no thunderCore: NCON() is false and nothing exists on the page object after the block runs', () => {
  const sb = rig(); const before = Object.keys(sb).sort();
  run(sb, LOADER);
  assert.deepEqual(Object.keys(sb).sort(), before, 'const declarations add no property to window');
  assert.equal(sb.__thunderClient, undefined);
  assert.equal(run(sb, 'NCON()'), false); assert.equal(run(sb, 'NC()'), undefined);
  assert.equal(sb.__log.creates.length, 0, 'defining the loader creates no element'); assert.equal(sb.__log.fetches, 0);
});
test('(a) switch off: padSR() is what it was (the context rate, else the guess)', () => {
  const off = rig(); run(off, LOADER + 'let srGuess = 48000;\n' + PADSR);
  assert.equal(run(off, 'padSR()'), 48000);
  off.ctx = null; run(off, 'srGuess = 44100'); assert.equal(run(off, 'padSR()'), 44100);
  const live = rig({ withCtx: { sampleRate: 96000 } }); run(live, LOADER + 'let srGuess = 48000;\n' + PADSR); assert.equal(run(live, 'padSR()'), 96000);
  // the old line and the new line differ only in the NCON() arm
  assert.equal(PADSR.trim(), "const padSR = () => NCON() ? NC().sr : ctx ? ctx.sampleRate : srGuess;");
});
test('(a) srGuess: switch off makes the throw-away AudioContext exactly as before; thunderCore or the native pick skips it', () => {
  const off = rig(); run(off, SRGUESS); assert.equal(off.__log.ac, 1); assert.equal(run(off, 'srGuess'), 44100);
  const fixed = rig({ rate: 96000 }); run(fixed, SRGUESS); assert.equal(fixed.__log.ac, 0, 'aset.rate wins as before'); assert.equal(run(fixed, 'srGuess'), 96000);
  const shell = rig({ thunderCore: {} }); run(shell, SRGUESS); assert.equal(shell.__log.ac, 0); assert.equal(run(shell, 'srGuess'), 48000);
  const pick = rig({ core: 'native' }); run(pick, SRGUESS); assert.equal(pick.__log.ac, 0);
});
test('(a) while on, padSR() is the engine rate with no AudioContext made', () => {
  const sb = rig({ core: 'native' }); run(sb, LOADER + SRGUESS + PADSR);
  sb.__thunderClient = { on: true, sr: 44100 }; assert.equal(run(sb, 'padSR()'), 44100); assert.equal(sb.__log.ac, 0);
  sb.__thunderClient.on = false; assert.equal(run(sb, 'padSR()'), 48000);
});
test('(a) the Settings value "Native core (test)" is listed only where window.thunderCore exists; the saved default and the old setter text are unchanged', () => {
  const seg = cut(/const updCore = makeSeg\([^\n]*\n[^\n]*\n[^\n]*\n/, 'updCore');
  assert.match(seg, /\[\['core', 'Audio thread'\], \['page', 'Page \(old\)'\]\]\.concat\(window\.thunderCore \? \[\['native', 'Native core \(test\)'\]\] : \[\]\)/);
  assert.match(seg, /\(\) => window\.thunderCore && aset\.core === 'native' \? 'native' : coreWanted\(\) \? 'core' : 'page'/);
  assert.match(seg, /'Sequencer: page, from the next PLAY\.', 2500\)/);
  assert.match(src, /const d = \{sink:'', lat:'interactive', rate:0, vol:90, psLive:'auto', pdc:false, core:'core',/, 'aset.core defaults to core');
  // run the segment's option list in a sandbox
  const opts = (thunderCore) => { const sb = rig({ thunderCore }); let got; sb.makeSeg = (h, o) => { got = o.map(x => x[0]); return () => {}; }; sb.$ = () => ({}); sb.coreWanted = () => true; sb.coreLoad = () => {}; sb.toast = () => {}; sb.asetSave = () => {};
    run(sb, seg + ';'); return got; };
  assert.equal(opts(null).join(), 'core,page');
  assert.equal(opts({}).join(), 'core,page,native');
});

// ---- (b) source guards -----------------------------------------------------------------------------------------------------------
test('(b) branch count: PC1 + PC3 own rows 1, 2, 3, 4, 5, 6, 15, 16, 17 and 18 of N2-PAGE-CLIENT 4.3 (and the two media-session guards), PC2 adds 7, 8 (two), 9, 10 (two); a new NCON() / tp.nc use needs a row there and a change here', () => {
  const at = re => (src.split('\n').map((l, i) => re.test(l) ? i + 1 : 0).filter(Boolean));
  assert.equal((src.match(/\bNCON\(\)/g) || []).length, 15, 'NCON() occurrences: padSR (PC1), tStart (4), saveS (6), resetAudio (17); coreBuf, coreKit, coreKitCheck, coreVar, setSong, setKit (PC2); mfSet, the OUT bar drag, pdcSet, thunderChains.param (PC4); fxDynNodeOf (M5)');
  assert.equal((src.match(/\bNC\(\)/g) || []).length, 18, 'NC() occurrences: padSR, tStart x2 (can, start), tStop (5), saveS, resetAudio, corePost (16); coreBuf, coreKit, coreKitCheck, setSong, setKit, newKit (PC2); mfSet, the OUT bar drag, pdcSet, param (PC4)');
  assert.equal((src.match(/\btp\.nc\b/g) || []).length, 10, 'tp.nc: perfPush, perfFill (15), corePost (16), tStop x2 (5), msMeta x2 (code and comment) and bgPlay (the one media session), the ncSongHost comment');
  for (const [re, fn] of [[/^function perfPush\(\)/, 'perfPush'], [/^function perfFill\(/, 'perfFill'], [/^const corePost = /, 'corePost'], [/^\s*if \(tp\.nc\)\{ tp\.nc = false;/, 'tStop'], [/const ms = navigator\.mediaSession; if \(!ms \|\| !bgOn/, 'msMeta'], [/^\s*if \(on && tp\.nc\) return;/, 'bgPlay']]) assert.equal(at(re).length, 1, fn);
  assert.equal(at(/\btp\.nc\b/).length, 8, 'tp.nc on eight lines (M5: fxDynNodeOf) (the tp literal and begin() say nc: without the tp. prefix)');
  for (const fn of ['coreBuf', 'coreKit', 'coreKitCheck', 'coreVar']) assert.ok(new RegExp('NCON\\(\\)[^\\n]*', 'm').test(cut(new RegExp('function ' + fn + '\\([^\\n]*(\\n[^\\n]*)?', 'm'), fn)), fn + ' has its branch');
  assert.equal((src.match(/\bncPadApi\b/g) || []).length, 8, 'ncPadApi: its definition, the five PC2 calls (coreVar does not need it), and PC3\'s padApi line (typeof test and call)');
  assert.equal((src.match(/\bloadClient\b/g) || []).length, 2, 'loadClient: its declaration and its one call site');
  assert.equal((src.match(/window\.thunderCore/g) || []).length, 6, 'window.thunderCore: the loader comment, srGuess, updCore (options, get, comment), the call site');
  assert.equal((src.match(/aset\.core === 'native'/g) || []).length, 3, "aset.core === 'native': srGuess, updCore's getter, the call site");
  assert.ok(/else if \(!\(window\.thunderCore \|\| aset\.core === 'native'\)\)\{ const AC = /.test(SRGUESS), 'branch 2');
  // the PC3 branches say what they are: tStart's is the first thing after the render prep and before the first PLAY's audio warm-up (rtInit)
  const ts = src.slice(src.indexOf('function tStart(mode, o){'), src.indexOf('function tStart(mode, o){') + 6000);
  assert.ok(ts.indexOf("if (NCON() && NC().can(mode, o)){ NC().start(mode, o); return; }") > 0 && ts.indexOf("if (NCON() && NC().can(mode, o))") < ts.indexOf('rtInit()'), 'the native branch comes before rtInit() (no AudioContext for a native PLAY)');
  assert.ok(/function saveS\(\)\{[^\n]*try \{ if \(NCON\(\)\) NC\(\)\.edit\(\); \} catch\(e\)\{\}/.test(src), 'saveS publishes through the client, after coreSong');
  assert.ok(/const corePost = \(m, tr\) => \{ if \(tp\.nc\) return NC\(\)\.legacy\(m\); try \{ core\.node\.port/.test(src), 'corePost: the JS core body is unchanged behind the tp.nc line');
});
test('(b) PC4: the four live-edit branches (mfSet, the OUT bar drag, pdcSet, param) are one guarded line each, and the host gives the client its three hooks', () => {
  assert.ok(/function mfSet\(t, v\)\{[^\n]*if \(rt\) rt\.pl\.applyMix\(\); try \{ if \(NCON\(\)\) NC\(\)\.edit\(\); \} catch\(e\)\{\}/.test(src), 'mfSet: after the old line, in a try');
  assert.ok(/  S\.mout = v; moApply\(\); try \{ if \(NCON\(\)\) NC\(\)\.edit\(\); \} catch\(e\)\{\} draw\(\); moToast\(\);/.test(src), 'the master OUT bar drag (moApply itself is untouched)');
  assert.ok(/function pdcSet\(v\)\{[^\n]*asetSave\(\); moFxAt = -1e9; try \{ if \(NCON\(\)\) NC\(\)\.edit\(\); \} catch\(e\)\{\}\n/.test(src), 'pdcSet');
  assert.ok(/\n    if \(NCON\(\)\) NC\(\)\.chainParam\(id, name, value\);[^\n]*\n    if \(!rt \|\| !rt\.pl\) return;/.test(src), 'thunderChains.param: after the patch update, before the rt guard (branch 13)');
  assert.equal((src.match(/\.chainParam\(/g) || []).length, 1); assert.equal((src.match(/NC\(\)\.edit\(\)/g) || []).length, 4, 'saveS, mfSet, moApply, pdcSet');
});
test('(b) __thunderClient appears only inside the loader block; thunder-core-client.js only in comments and loadClient\'s one src line', () => {
  const lines = src.split('\n'), start = lines.findIndex(l => l.startsWith('/* NATIVE CORE CLIENT (PC1')), endIdx = lines.findIndex((l, i) => i > start && l.includes('document.head.appendChild(s); };'));
  assert.ok(start > 0 && endIdx > start);
  lines.forEach((l, i) => {
    if (l.includes('__thunderClient')) assert.ok(i >= start && i <= endIdx, 'line ' + (i + 1) + ' mentions __thunderClient outside the loader: ' + l.slice(0, 80));
    if (l.includes('thunder-core-client.js')) assert.ok(/^\/\*/.test(l) || /^\s*s\.src = 'thunder-core-client\.js\?v=5';/.test(l) || /^\s*s\.onerror/.test(l), 'line ' + (i + 1) + ' names the client file: ' + l.slice(0, 80));
  });
  assert.equal(lines.filter(l => /^\s*s\.src = /.test(l) && l.includes('thunder-core-client.js')).length, 1);
});
test('(b) the call site is guarded by the switch and the shell, and is the only call; the loader makes one script element', () => {
  assert.equal(CALL.split(';')[0], "if (window.thunderCore || aset.core === 'native') loadClient()");
  assert.equal((src.match(/\bloadClient\(\)/g) || []).length, 1);
  assert.equal((LOADER.match(/createElement\(/g) || []).length, 1);
});
test('(b) nothing the generated cores read changed: make-core --check passes', () => {
  const r = require('child_process').spawnSync(process.execPath, ['tools/make-core.mjs', '--check'], { cwd: require('path').resolve(__dirname, '..') });
  assert.equal(r.status, 0, String(r.stdout) + String(r.stderr));
});

// ---- (c) nothing is loaded with the switch off --------------------------------------------------------------------------------------
test('(c) switch off, no thunderCore: the call site does not call loadClient; no script element, no fetch', () => {
  const sb = rig(); let called = 0; run(sb, LOADER);
  sb.loadClient = () => { called++; };   // a spy in front of the real one
  run(sb, CALL + ';'); assert.equal(called, 0);
  const real = rig(); run(real, LOADER + CALL + ';');
  assert.equal(real.__log.creates.length, 0); assert.equal(real.__log.appended.length, 0); assert.equal(real.__log.fetches, 0);
});
test('(c) with the shell or the native pick the loader makes one script for the client file, and not a second time', () => {
  for (const o of [{ thunderCore: {} }, { core: 'native' }]) {
    const sb = rig(o); run(sb, LOADER + CALL + ';'); assert.equal(sb.__log.creates.join(), 'script');
    assert.equal(sb.__log.scripts[0].src, 'thunder-core-client.js?v=5'); assert.equal(sb.__log.appended.length, 1);
    sb.__thunderClient = {}; run(sb, 'loadClient()'); assert.equal(sb.__log.creates.length, 1, 'already loaded: nothing more');
  }
});
test('(c) a load error leaves the page as it was: onerror only logs', () => {
  const sb = rig({ core: 'native' }); run(sb, LOADER + CALL + ';'); sb.__log.scripts[0].onerror();
  assert.equal(run(sb, 'NCON()'), false); assert.match(sb.__log.tp.join('|'), /did not load/);
});

// ---- the client file and sw.js --------------------------------------------------------------------------------------------------
function clientRig(thunderCore, hostExtra = {}) {
  const sb = { window: null, Promise, Error, JSON, Object, Array, isFinite, setTimeout, clearTimeout, setInterval, clearInterval }; sb.window = sb; if (thunderCore) sb.thunderCore = thunderCore;
  vm.createContext(sb); vm.runInContext(H.read('thunder-core-client.js'), sb, { filename: 'thunder-core-client.js' });
  const host = Object.assign({ tpLog: (...a) => (sb.__tp = sb.__tp || []).push(a.join(' ')), build: () => 678, prerender: () => { sb.__pre = (sb.__pre || 0) + 1; } }, hostExtra);
  return { sb, host };
}
const fakeCore = reply => { const sent = []; return { sent, ask: (m) => { sent.push(m); return reply instanceof Error ? Promise.reject(reply) : Promise.resolve(reply); } }; };
test('client: hello handshake turns it on only for v 1 + caps.nativeAudio + a sample rate, then re-renders pads once', async () => {
  const tc = fakeCore({ t: 'reply', v: 1, core: 'r1', caps: { nativeAudio: true }, sr: 44100, outLat: 256, host: 'android' }); const { sb, host } = clientRig(tc);
  assert.equal(sb.__thunderClient.on, false); assert.equal(await sb.__thunderClient.attach(host), true);
  assert.equal(JSON.stringify(tc.sent), JSON.stringify([{ t: 'hello', v: 1, live: true, app: 678 }])); assert.equal(sb.__thunderClient.on, true); assert.equal(sb.__thunderClient.sr, 44100); assert.equal(sb.__pre, 1);
  sb.__thunderClient.detach(); assert.equal(sb.__thunderClient.on, false);
});
test('client: every failure leaves it off and the page untouched', async () => {
  const cases = [[null, 'no window.thunderCore'], [fakeCore({ v: 2, caps: { nativeAudio: true }, sr: 48000 }), 'bridge version'], [fakeCore({ v: 1, caps: { nativeAudio: false }, sr: 48000 }), 'no native audio'],
    [fakeCore({ v: 1, caps: { nativeAudio: true } }), 'no sample rate'], [fakeCore(new Error('ask timeout')), 'hello failed'], [fakeCore(null), 'empty']];
  for (const [tc, why] of cases) { const { sb, host } = clientRig(tc); assert.equal(await sb.__thunderClient.attach(host), false); assert.equal(sb.__thunderClient.on, false); assert.match(sb.__thunderClient.reason, new RegExp(why)); assert.equal(sb.__pre, undefined); }
});
test('client: defines only window.__thunderClient, and loading it twice keeps the first', () => {
  const { sb } = clientRig(null); const first = sb.__thunderClient; vm.runInContext(H.read('thunder-core-client.js'), sb); assert.equal(sb.__thunderClient, first);
  assert.deepEqual(Object.keys(sb).filter(k => !['window', 'Promise', 'Error', 'JSON', 'Object', 'Array', 'isFinite', 'setTimeout', 'clearTimeout', 'setInterval', 'clearInterval'].includes(k)), ['__thunderClient']);
});
test('sw.js: the client is an OPTIONAL entry (a missing file cannot fail the install), the file exists, and BUILD matches the cache', () => {
  const fs = require('fs'), path = require('path');
  const opt = (sw.match(/const OPTIONAL = \[([^\]]*)\]/) || [])[1] || '', files = (sw.match(/const FILES = \[[\s\S]*?\];/) || [])[0] || '';
  assert.ok(opt.includes("'./thunder-core-client.js?v=5'")); assert.ok(!files.includes('thunder-core-client.js'));
  assert.ok(fs.existsSync(path.resolve(__dirname, '..', 'thunder-core-client.js')));
  assert.ok(src.includes("s.src = 'thunder-core-client.js?v=5'"), 'the page and the service worker name the same URL');
  const b = src.match(/const BUILD = (\d+);/)[1]; assert.ok(sw.includes("'thunderplus-v" + b + "'"));
});
