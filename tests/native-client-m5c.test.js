// M5 (master meters): the master panel from the shell's master.lvl, against a FAKE thunderCore (see native-client-meters.test.js for the rig).
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
  const ck = clock(), sent = [], handlers = {}, st = { hold, dyn: [], ch: [], lv: [], mw: false, ms: [] };
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
    meterHold: () => st.hold, chWant: () => st.ch, chLvlIn: (t, i, o) => { st.lv.push([t, i, o]); }, mstWant: () => st.mw, mstIn: o => { st.ms.push(o); if (st.throwIn) throw new Error('x'); }, fxDynIn: (n, list) => { st.dyn.push([n, list]); } };
  const NC = sb.__thunderClient, emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m))), beat = () => emit('clock', { f: 0, sr: 48000 });
  return { sb, NC, tc, host, ck, sent, S, st, emit, beat, handlers, want: () => sent.filter(m => m.t === 'meters.want'), clear() { sent.length = 0; } };
}
async function up(o) { const r = rig(o); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); return r; }
async function playing(o) { const r = await up(o); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); await r.ck.advance(200); return r; }
const port = (r, tr) => r.NC.fxNode(tr).port;
const W = (fx, dyn, extra) => Object.assign({ t: 'meters.want', rate: 10, fx, dyn, chains: [], master: false }, extra || {});



const REC = (x) => Object.assign({ seq: 1, f: 100, inPk: 0.5, outPk: 0.4, tp: 0.45, inLu: -14.2, outLu: -10.1, comp: -2.5, compIn: -18, lim: -1.2, load: 0.03, sup: null }, x || {});

test('master: true goes only while a native PLAY runs, METERS is on and the master panel is open (host.mstWant); false otherwise', async () => {
  const r = await playing(); r.clear(); r.st.mw = true; await adv(r, 700);
  assert.deepEqual(r.want().map(m => m.master), [true]);
  r.clear(); r.st.mw = false; await adv(r, 700); assert.deepEqual(r.want().map(m => m.master), [false]);   // panel closed
  r.clear(); r.st.mw = true; r.st.hold = true; await adv(r, 700); assert.deepEqual(r.want().map(m => m.master), []);   // METERS off: still all off
  r.st.hold = false; await adv(r, 700); assert.deepEqual(r.want().map(m => m.master), [true]);
  r.clear(); r.NC.stop(); await adv(r, 700); assert.deepEqual(r.want().map(m => m.master), [false]);   // STOP
});
test('no mstWant hook, or one that throws: master stays false; no meters2: nothing asked, master.lvl ignored', async () => {
  const r = await playing(); delete r.host.mstWant; r.clear(); await adv(r, 700); assert.ok(r.want().every(m => m.master === false));
  const t = await playing(); t.host.mstWant = () => { throw new Error('x'); }; t.clear(); await adv(t, 700); assert.ok(t.want().every(m => m.master === false));
  const n = await playing({ meters2: false }); n.st.mw = true; await n.ck.advance(900); assert.equal(n.want().length, 0); n.emit('master.lvl', REC()); assert.equal(n.st.ms.length, 0);
});
test('master.lvl reaches host.mstIn with the page\'s field names; sup null stays null; ignored when no PLAY runs and after STOP', async () => {
  const r = await up(); r.emit('master.lvl', REC()); assert.equal(r.st.ms.length, 0);   // no PLAY yet
  const p = await playing(); p.emit('master.lvl', REC());
  assert.deepEqual(J(p.st.ms[0]), { inPk: 0.5, outPk: 0.4, tp: 0.45, inLu: -14.2, outLu: -10.1, comp: -2.5, compIn: -18, lim: -1.2, load: 0.03, sup: null });
  const sup = Array.from({ length: 48 }, (_, i) => i / 4); p.emit('master.lvl', REC({ sup }));
  assert.deepEqual(J(p.st.ms[1].sup), sup);
  p.NC.stop(); await adv(p, 100); p.emit('master.lvl', REC()); assert.equal(p.st.ms.length, 2);
});
test('master.lvl guards: bad numbers become the silent values, a wrong-sized or bad sup becomes null, a record with no fields is silent, a throwing hook is caught', async () => {
  const p = await playing();
  p.emit('master.lvl', REC({ inPk: 'x', outPk: NaN, tp: -1, inLu: null, outLu: Infinity, comp: 5, compIn: undefined, lim: 'a', load: -3, sup: [1, 2, 3] }));
  assert.deepEqual(J(p.st.ms[0]), { inPk: 0, outPk: 0, tp: 0, inLu: -180, outLu: -180, comp: 0, compIn: -200, lim: 0, load: 0, sup: null });
  p.emit('master.lvl', REC({ sup: Array.from({ length: 48 }, (_, i) => (i === 3 ? NaN : -2)) })); assert.equal(p.st.ms[1].sup[3], 0); assert.equal(p.st.ms[1].sup[4], 0);
  const n = p.st.ms.length; p.emit('master.lvl', null); p.emit('master.lvl', 7); p.emit('master.lvl'); assert.equal(p.st.ms.length, n + 3); assert.deepEqual(J(p.st.ms[n]), J(p.st.ms[n + 2]));   // a record with no fields is the silent record
  p.st.throwIn = true; p.emit('master.lvl', REC());   // must not throw
  delete p.host.mstIn; p.emit('master.lvl', REC());
});
test('the page side: mstNativeIn stores with a time, mstM ages out at 600 ms with a silent record, is the plain mstMeter with the switch off; the panel reads mstM', () => {
  const sb = { NCON: () => true, tp: { nc: true }, mwOpenNow: true, document: { hidden: false }, performance: { now: () => T } }; var T = 1000;
  const vm2 = require('vm'); vm2.createContext(sb);
  vm2.runInContext(cut(/let mstMeter = null, mstMeterT = 0;[\s\S]*?\nfunction mstM\(\)\{[\s\S]*?\n\}\n/, 'mstM') + '\nthis.W = mstNativeWant; this.I = mstNativeIn; this.M = mstM; this.set = v => { mstMeter = v; };', sb);
  assert.equal(sb.W(), true); sb.mwOpenNow = false; assert.equal(sb.W(), false); sb.mwOpenNow = true; sb.document.hidden = true; assert.equal(sb.W(), false); sb.document.hidden = false;
  sb.tp.nc = false; assert.equal(sb.W(), false); sb.set({ web: 1 }); T = 99999; assert.deepEqual(J(sb.M()), { web: 1 });   // switch off: the worklet's value, no age limit
  sb.tp.nc = true; sb.I({ inLu: -12, sup: null }); assert.equal(sb.M().inLu, -12); T += 600; assert.equal(sb.M().inLu, -12); T += 1; assert.equal(sb.M().inLu, -180);
  assert.equal(sb.M().tp, 0); assert.equal(sb.M().sup, null); assert.equal(sb.M().compIn, -200); sb.I(null); T += 0; assert.equal(sb.M().inLu, -180);
  assert.ok(/mstWant:\(\) => mstNativeWant\(\), mstIn:o => mstNativeIn\(o\)/.test(src));
  assert.ok(/const m = mstM\(\), live = !!\(m && S\.mst\.on/.test(src) && /const m = mstM\(\), live = !!\(m && M\.on/.test(src) && /mm = mstM\(\), red/.test(src));
  assert.ok(/e\.data\.meter\) mstMeter = e\.data\.meter/.test(src), 'the web worklet path is unchanged');
});
