// M5 (second half): the …chains screen's in / out level meters from the shell's ch.lvl, against a FAKE thunderCore (see native-client-meters.test.js for the rig).
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
const adv = async (r, ms) => { for (let t = 0; t < ms; t += 100) { r.beat(); await r.ck.advance(100); } };   // the engine's clock keeps coming
const J = x => JSON.parse(JSON.stringify(x));
const flush = async () => { for (let i = 0; i < 6; i++) await new Promise(r => setImmediate(r)); };

function rig({ meters2 = true, hold = false } = {}) {
  const ck = clock(), sent = [], handlers = {}, st = { hold, dyn: [], ch: [], lv: [] };
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
    meterHold: () => st.hold, chWant: () => st.ch, chLvlIn: (t, i, o) => { st.lv.push([t, i, o]); }, fxDynIn: (n, list) => { st.dyn.push([n, list]); } };
  const NC = sb.__thunderClient, emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m))), beat = () => emit('clock', { f: 0, sr: 48000 });
  return { sb, NC, tc, host, ck, sent, S, st, emit, beat, handlers, want: () => sent.filter(m => m.t === 'meters.want'), clear() { sent.length = 0; } };
}
async function up(o) { const r = rig(o); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); return r; }
async function playing(o) { const r = await up(o); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); await r.ck.advance(200); return r; }
const port = (r, tr) => r.NC.fxNode(tr).port;
const W = (fx, dyn, extra) => Object.assign({ t: 'meters.want', rate: 10, fx, dyn, chains: [], master: false }, extra || {});


test('chains: [tracks] goes only while a native PLAY runs, METERS is on and the chains screen shows tracks; [] otherwise', async () => {
  const r = await playing(); r.clear(); r.st.ch = [3, 1, 3, 40, -1, 1.5];
  await adv(r, 700);
  assert.deepEqual(r.want().map(m => m.chains), [[1, 3]]);   // each once, ascending, 0..31 only
  r.clear(); r.st.ch = []; await adv(r, 700);
  assert.deepEqual(r.want().map(m => m.chains), [[]]);   // screen left: off within the 500 ms tick + 100 ms
  r.clear(); r.st.ch = [2]; r.st.hold = true; await adv(r, 700);
  assert.deepEqual(r.want().map(m => m.chains), []);   // METERS off: still all off, nothing new to send
  r.st.hold = false; await adv(r, 700);
  assert.deepEqual(r.want().map(m => m.chains), [[2]]);
  r.clear(); r.NC.stop(); await adv(r, 700);
  assert.deepEqual(r.want().map(m => m.chains), [[]]);   // STOP: off
});
test('never a boolean: every meters.want carries chains as an array', async () => {
  const r = await playing(); r.st.ch = [0]; await adv(r, 700); r.NC.stop(); await adv(r, 700);
  assert.ok(r.want().length > 0); for (const m of r.want()) assert.ok(Array.isArray(m.chains));
});
test('no meters2 caps: no chains asked, ch.lvl ignored', async () => {
  const r = await playing({ meters2: false }); r.st.ch = [1]; await r.ck.advance(900);
  assert.equal(r.want().length, 0); r.emit('ch.lvl', { tr: 1, in: [0.5, 0.2], out: [0.1, 0.3] }); assert.equal(r.st.lv.length, 0);
});
test('ch.lvl reaches host.chLvlIn as max(l, r); a bad track or a missing array is safe; ignored when no PLAY runs', async () => {
  const r = await up(); r.emit('ch.lvl', { tr: 1, in: [0.5, 0.2], out: [0.1, 0.3] }); assert.equal(r.st.lv.length, 0);   // no PLAY yet
  const p = await playing();
  p.emit('ch.lvl', { tr: 1, seq: 1, f: 10, in: [0.5, 0.2], out: [0.1, 0.3] });
  p.emit('ch.lvl', { tr: 5, in: [0, 0], out: [0.25, 0] });
  p.emit('ch.lvl', { tr: 32, in: [1, 1], out: [1, 1] }); p.emit('ch.lvl', { tr: -1, in: [1, 1], out: [1, 1] }); p.emit('ch.lvl', { tr: 2 }); p.emit('ch.lvl', { tr: 3, in: ['x', -1], out: [NaN, 0.4] });
  assert.deepEqual(J(p.st.lv), [[1, 0.5, 0.3], [5, 0, 0.25], [2, 0, 0], [3, 0, 0.4]]);
  p.NC.stop(); await adv(p, 100); p.emit('ch.lvl', { tr: 1, in: [1, 1], out: [1, 1] }); assert.equal(p.st.lv.length, 4);   // after STOP: ignored
});
test('the page side: the chains screen feed reads the hook, ages out at 400 ms, is guarded by NCON() and tp.nc', () => {
  const fn = cut(/function chNativeWant\(\)[\s\S]*?\nidleTick\(\(\) => \{\n[^\n]*\n[^\n]*\n/, 'chNativeWant..timer');
  assert.ok(/if \(!NCON\(\) \|\| !tp\.nc \|\| view !== 'chains' \|\| document\.hidden\) return \[\];/.test(fn));
  assert.ok(/now - v\.t <= 400/.test(fn) && /api\.meters\(\{ peaks:out, peaksIn:inp \}\)/.test(fn));
  assert.ok(/if \(NCON\(\) && tp\.nc\)\{ chNativeFeed\(\); return; \}/.test(src));
  assert.ok(/chWant:\(\) => chNativeWant\(\), chLvlIn:\(t, i, o\) => chNativeIn\(t, i, o\)/.test(src));
  // run it: fresh data shows, old data is silent
  const sb = { NCON: () => true, tp: { nc: true }, view: 'chains', document: { hidden: false }, NT: 4, S: { chainPatch: { modules: [{ type: 'chainOut', params: { track: 2 } }, { type: 'trackIn', params: { track: 4 } }, { type: 'delay', params: { track: 1 } }, { type: 'chainOut', params: { track: 99 } }] } },
    int: (v, d) => (Number.isFinite(+v) ? Math.trunc(+v) : d), performance: { now: () => T }, got: null, chApi: () => ({ meters: p => { sb.got = p; } }) };
  var T = 1000; const vm2 = require('vm'); vm2.createContext(sb);
  vm2.runInContext(cut(/const ncChLv = \{\};[\s\S]*?function chNativeFeed\(\)\{[\s\S]*?\n\}\n/, 'feed') + '\nthis.W = chNativeWant; this.I = chNativeIn; this.F = chNativeFeed;', sb);
  assert.deepEqual(J(sb.W()), [1, 3]);
  sb.I(1, 0.4, 0.2); sb.F(); assert.deepEqual(J(sb.got), { peaks: [0, 0.2, 0, 0], peaksIn: [0, 0.4, 0, 0] });
  T = 1300; sb.F(); assert.equal(sb.got.peaks[1], 0.2);
  T = 1500; sb.F(); assert.deepEqual(J(sb.got), { peaks: [0, 0, 0, 0], peaksIn: [0, 0, 0, 0] });   // older than 400 ms: silent
  sb.tp.nc = false; assert.deepEqual(J(sb.W()), []); sb.tp.nc = true; sb.view = 'tracker'; assert.deepEqual(J(sb.W()), []);
});
