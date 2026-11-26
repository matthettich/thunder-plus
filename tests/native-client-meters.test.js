// M5 (first half) of the native page client (docs/port/METERS-DESIGN.md 4.4, 4.5, 5a): the effect meters, against a FAKE thunderCore. No browser: the client runs in a vm with a fake host and a fake clock.
// Covers: one debounced whole-state meters.want (100 ms), one track at a time, off under meterHold() and when no native PLAY runs, sent again on engine up, fx.lvl into the page's fxSlotLevel (400 ms age rule),
// a stale-type fx.dyn entry dropped, fx.dyn through the page's own fxDynIn key (slot:type), and nothing asked without caps.meters2.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const CLIENT = H.read('thunder-core-client.js'), src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'not found: ' + what); return m[0]; };

function clock() {
  let now = 1000, id = 0; const T = new Map();
  const add = (fn, ms, every) => { const k = ++id; T.set(k, { fn, at: now + Math.max(0, ms | 0), every: every ? Math.max(1, ms | 0) : 0 }); return k; };
  return { now: () => now, setTimeout: (fn, ms) => add(fn, ms, false), clearTimeout: k => T.delete(k), setInterval: (fn, ms) => add(fn, ms, true), clearInterval: k => T.delete(k),
    async advance(ms) { const end = now + ms; for (;;) { let best = null; for (const [k, t] of T) if (t.at <= end && (!best || t.at < best[1].at)) best = [k, t]; if (!best) break; now = Math.max(now, best[1].at); const [k, t] = best; if (t.every) t.at = now + t.every; else T.delete(k); t.fn(); await Promise.resolve(); } now = end; await new Promise(r => setImmediate(r)); } };
}
const J = x => JSON.parse(JSON.stringify(x));
const flush = async () => { for (let i = 0; i < 6; i++) await new Promise(r => setImmediate(r)); };

function rig({ meters2 = true, hold = false } = {}) {
  const ck = clock(), sent = [], handlers = {}, st = { hold, dyn: [] };
  const sb = { window: null, Promise, Error, JSON, Object, Array, Map, isFinite, Math, Date, setTimeout: ck.setTimeout, clearTimeout: ck.clearTimeout, setInterval: ck.setInterval, clearInterval: ck.clearInterval };
  sb.window = sb;
  const tc = { host: 'android', send: m => sent.push(JSON.parse(JSON.stringify(m))),
    ask: m => Promise.resolve(m.t === 'hello' ? { t: 'reply', v: 1, core: 'r1', caps: Object.assign({ nativeAudio: true }, meters2 ? { meters2: true } : {}), sr: 48000, outLat: 480, host: 'android' } : { t: 'reply', ok: true, rev: m.rev, reasons: [], warn: [] }),
    on: (t, fn) => { (handlers[t] = handlers[t] || []).push(fn); }, off: (t, fn) => { handlers[t] = (handlers[t] || []).filter(f => f !== fn); } };
  sb.thunderCore = tc; vm.createContext(sb); vm.runInContext(CLIENT, sb, { filename: 'thunder-core-client.js' });
  const S = { name: 'n', bpm: 120, song: [], master: 255, mout: 255, mix: [], mst: {}, tfx: Array.from({ length: 32 }, () => [null, null, null, null]), mfx: [null, null, null, null] };
  S.tfx[3] = [{ type: 'compressor', on: true }, { type: 'delay', on: true }, null, null]; S.mfx = [{ type: 'limiter', on: true }, null, null, null];
  const host = { S: () => S, songVer: () => 1, aset: { core: 'native' }, toast: () => {}, tpLog: () => {}, pbLog: () => {}, now: () => ck.now(), prerender: () => {}, build: () => 680, vars: () => ({}), title: () => 'x', lxp: () => 0,
    perf: () => null, blocked: () => '', prep: () => {}, begin: (mode, o) => o || {}, fallback: () => {}, stopped: () => {}, pdc: () => 1, fxSlot: () => null, chainMods: () => ({}),
    meterHold: () => st.hold, fxDynIn: (n, list) => { st.dyn.push([n, list]); } };
  const NC = sb.__thunderClient, emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m))), beat = () => emit('clock', { f: 0, sr: 48000 });
  return { sb, NC, tc, host, ck, sent, S, st, emit, beat, handlers, want: () => sent.filter(m => m.t === 'meters.want'), clear() { sent.length = 0; } };
}
async function up(o) { const r = rig(o); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); return r; }
async function playing(o) { const r = await up(o); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); await r.ck.advance(200); return r; }
const port = (r, tr) => r.NC.fxNode(tr).port;
const W = (fx, dyn, extra) => Object.assign({ t: 'meters.want', rate: 10, fx, dyn, chains: [], master: false }, extra || {});

test('a wish while a native PLAY runs goes as ONE whole-state meters.want 100 ms after the last change', async () => {
  const r = await playing(); r.clear();
  port(r, 3).postMessage({ lvl: true }); await r.ck.advance(60);
  port(r, 3).postMessage({ tel: true }); await r.ck.advance(60);   // 120 ms after the first, but only 60 after the last: still nothing
  assert.equal(r.want().length, 0);
  await r.ck.advance(60);
  assert.deepEqual(J(r.want()), [W([3], [3])]);   // the exact message as sent
  await r.ck.advance(1000); assert.equal(r.want().length, 1, 'nothing more without a change');
});
test('a burst of changes is one message; a change that goes back within 100 ms sends nothing', async () => {
  const r = await playing(); r.clear();
  for (let i = 0; i < 5; i++) { port(r, i).postMessage({ lvl: true }); await r.ck.advance(30); }
  await r.ck.advance(120); assert.deepEqual(J(r.want()), [W([4], [])]);
  r.clear(); port(r, 4).postMessage({ lvl: false }); await r.ck.advance(50); port(r, 4).postMessage({ lvl: true }); await r.ck.advance(300);
  assert.equal(r.want().length, 0);
});
test('ONE track at a time: asking for another track replaces the first, off removes it', async () => {
  const r = await playing(); r.clear();
  port(r, 2).postMessage({ lvl: true }); port(r, 5).postMessage({ lvl: true }); port(r, 5).postMessage({ tel: true }); await r.ck.advance(150);
  assert.deepEqual(J(r.want()), [W([5], [5])]);
  r.clear(); port(r, 2).postMessage({ lvl: false }); await r.ck.advance(150); assert.equal(r.want().length, 0, 'a track that is no longer wanted: no change');
  port(r, 5).postMessage({ lvl: false }); await r.ck.advance(150); assert.deepEqual(J(r.want()), [W([], [5])]);
  r.clear(); port(r, 32).postMessage({ lvl: true }); port(r, 5).postMessage({ tel: false }); port(r, 32).postMessage({ tel: true }); await r.ck.advance(150);
  assert.deepEqual(J(r.want()), [W([32], [32])], 'the master\'s effects are track 32');
});
test('nothing goes out while no native PLAY runs; PLAY sends the wish; STOP sends everything off', async () => {
  const r = await up(); r.clear();
  port(r, 3).postMessage({ lvl: true }); await r.ck.advance(300); assert.equal(r.want().length, 0, 'stopped: nothing asked');
  r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); await r.ck.advance(200);
  assert.deepEqual(J(r.want()), [W([3], [])]);
  r.clear(); r.NC.stop(); await r.ck.advance(200); assert.deepEqual(J(r.want()), [W([], [])]);
  r.clear(); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); await r.ck.advance(200); assert.deepEqual(J(r.want()), [W([3], [])], 'the wish survived the STOP');
});
test('everything off while the page\'s meterHold() is true (METERS OFF while playing), on again when it ends', async () => {
  const r = await playing(); r.clear();
  port(r, 3).postMessage({ lvl: true }); port(r, 3).postMessage({ tel: true }); await r.ck.advance(150); assert.deepEqual(J(r.want()), [W([3], [3])]);
  r.clear(); r.st.hold = true; await r.ck.advance(700); r.beat(); await r.ck.advance(300);   // the watchdog tick notices
  assert.deepEqual(J(r.want()), [W([], [])]);
  r.clear(); port(r, 4).postMessage({ lvl: true }); await r.ck.advance(300); assert.equal(r.want().length, 0, 'held: a new wish asks for nothing');
  r.st.hold = false; await r.ck.advance(700); r.beat(); await r.ck.advance(300); assert.deepEqual(J(r.want()), [W([4], [3])]);
});
test('the whole state is sent again on engine {up:true}; nothing while the engine is down', async () => {
  const r = await playing(); port(r, 3).postMessage({ lvl: true }); await r.ck.advance(150); r.clear();
  r.emit('engine', { up: false }); await r.ck.advance(300); assert.equal(r.want().length, 0, 'a PLAY that stopped with the engine: no message to a dead engine');
  r.emit('engine', { up: true, sr: 48000 }); await r.ck.advance(300);
  assert.deepEqual(J(r.want()), [W([], [])], 'whole state again, off because no PLAY runs');
  r.clear(); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); await r.ck.advance(300); assert.deepEqual(J(r.want()), [W([3], [])]);
  r.clear(); r.emit('engine', { up: true, sr: 48000 }); await r.ck.advance(300); assert.equal(r.want().length, 1, 'up again: sent again even though it is what the core was told before');
});
test('fx.lvl reaches the view the page reads, with an arrival time; fxSlotLevel (the page\'s own function) keeps its 400 ms rule and -54..0 dB map', async () => {
  const r = await playing(); const v = r.NC.fxNode(3); port(r, 3).postMessage({ lvl: true });
  r.emit('fx.lvl', { seq: 1, f: 100, tr: 3, a: [1, 0.1, 0, 0.001, 0, 0, 0, 0.5] });
  assert.deepEqual(J(v.lvl), [1, 0.1, 0, 0.001, 0, 0, 0, 0.5]); assert.equal(v.lvlT, r.ck.now());
  const sbp = { performance: { now: () => r.ck.now() }, clamp: (x, a, b) => Math.max(a, Math.min(b, x)), Math };
  vm.createContext(sbp); vm.runInContext(cut(/const fxSlotLevel = [^\n]*\n/, 'fxSlotLevel') + 'let fxLvlNode = null;', sbp);
  // (the page's fxLvlSet keeps fxLvlNode = the node it was given: the view)
  sbp.fxLvlNode = null; vm.runInContext('fxLvlNode = null', sbp); const set = n => { sbp.__n = n; vm.runInContext('fxLvlNode = __n', sbp); };
  set(v);
  assert.equal(vm.runInContext('fxSlotLevel(3, 0)', sbp), 1);                       // 0 dB
  assert.ok(Math.abs(vm.runInContext('fxSlotLevel(3, 1)', sbp) - (20 * Math.log10(0.1) + 54) / 54) < 1e-9);   // -20 dB
  assert.equal(vm.runInContext('fxSlotLevel(3, 2)', sbp), 0);
  await r.ck.advance(399); assert.ok(vm.runInContext('fxSlotLevel(3, 0)', sbp) > 0);
  await r.ck.advance(2); assert.equal(vm.runInContext('fxSlotLevel(3, 0)', sbp), 0, 'older than 400 ms: nothing');
});
test('fx.dyn goes through the page\'s fxDynIn with the key slot:type; an entry whose type is not the slot\'s type is dropped (a plan was just swapped)', async () => {
  const r = await playing(); const v = r.NC.fxNode(3);
  r.emit('fx.dyn', { seq: 1, f: 1, tr: 3, d: [[0, 'compressor', -6, -20, -18], [1, 'compressor', -3, -10, -9], [2, 'limiter', 0, 0, 0]] });
  assert.equal(r.st.dyn.length, 1); assert.equal(r.st.dyn[0][0], v);
  assert.deepEqual(J(r.st.dyn[0][1]), [[0, '0:compressor', [-6, -20, -18]]]);   // slot 1 is a delay now, slot 2 is empty: both dropped
  r.st.dyn.length = 0; r.emit('fx.dyn', { seq: 2, f: 2, tr: 3, d: [[1, 'compressor', -3, -10, -9]] }); assert.equal(r.st.dyn.length, 0, 'nothing left: fxDynIn is not called');
  r.emit('fx.dyn', { seq: 3, f: 3, tr: 32, d: [[0, 'limiter', -2, -1, -3]] }); r.emit('fx.dyn', { seq: 4, f: 3, tr: 32, d: [[0, 'compressor', -2, -1, -3]] });
  assert.equal(r.st.dyn.length, 0, 'tr 32 has no view until the page asked'); port(r, 32).postMessage({ tel: true });
  r.emit('fx.dyn', { seq: 5, f: 3, tr: 32, d: [[0, 'limiter', -2, -1, -3], [0, 'compressor', 0, 0, 0]] });
  assert.deepEqual(J(r.st.dyn.map(x => x[1])), [[[0, '0:limiter', [-2, -1, -3]]]], 'the master\'s effects are S.mfx');
  // the page's own fxDynIn takes the list as it is (key = slot + ':' + type)
  const pg = { performance: { now: () => 5 }, Math, Float32Array, String, Map, FXLV: new Set(['comp', 'ott', 'transient']), FXD_N: 80 };
  vm.createContext(pg); vm.runInContext(cut(/function fxDynNew\(key\)[^\n]*\n/, 'fxDynNew') + cut(/function fxDynFeed\(D, v, now, lv\)\{[\s\S]*?\n\}\n/, 'fxDynFeed') + cut(/function fxDynIn\(n, list\)\{[\s\S]*?\n\}\n/, 'fxDynIn') + 'const FXD_N = 80, FXLV = new Set();', pg);
  const n = { dyn: null }; pg.n = n; pg.list = [[0, '0:compressor', [-6, -20, -18]]]; vm.runInContext('fxDynIn(n, list)', pg);
  assert.equal(n.dyn.get(0).key, '0:compressor'); assert.deepEqual(Array.from(n.dyn.get(0).cur), [-6, -20, -18]);
});
test('without caps.meters2 nothing is asked, no view is made, the events are not listened to', async () => {
  const r = await up({ meters2: false });
  assert.equal(r.NC.fxNode(3), null);
  r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); await r.ck.advance(500); r.NC.metersSync(true); await r.ck.advance(500); r.emit('engine', { up: true, sr: 48000 }); await r.ck.advance(500);
  assert.equal(r.want().length, 0); assert.ok(!r.handlers['fx.lvl'] && !r.handlers['fx.dyn']);
});
test('page side: with the switch off fxDynNodeOf is what it was; the host hands the client meterHold and fxDynIn; the view is a node (port) the page\'s own fxLvlSet / fxTelSet drive', async () => {
  assert.match(src, /const fxDynNodeOf = t => NCON\(\) && tp\.nc \? NC\(\)\.fxNode\(t < 0 \? 32 : t\) : !rt \? null : t < 0 \? rt\.mfx \|\| null : \(rt\.pl\.buses\[t\] && rt\.pl\.buses\[t\]\.fx\) \|\| null;/);
  assert.match(src, /meterHold:\(\) => meterHold\(\), fxDynIn:\(n, list\) => fxDynIn\(n, list\),/);
  // the page's fxLvlSet / fxTelSet, unchanged, on the view: the old node off, the new one on
  const r = await playing(); r.clear();
  const pg = { fxLvlNode: null, fxTelNode: null }; vm.createContext(pg);
  vm.runInContext(cut(/function fxLvlSet\(node\)\{[\s\S]*?\n\}\n/, 'fxLvlSet') + cut(/function fxTelSet\(node\)\{[\s\S]*?\n\}\n/, 'fxTelSet') + 'let fxLvlNode = null, fxTelNode = null;', pg);
  pg.a = r.NC.fxNode(1); pg.b = r.NC.fxNode(2);
  vm.runInContext('fxLvlSet(a); fxTelSet(a); fxLvlSet(b); fxTelSet(b)', pg); await r.ck.advance(150); assert.deepEqual(J(r.want()), [W([2], [2])]);
  r.clear(); vm.runInContext('fxLvlSet(null); fxTelSet(null)', pg); await r.ck.advance(150); assert.deepEqual(J(r.want()), [W([], [])]);
});
