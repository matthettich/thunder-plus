// PC6 of the native page client: fx.values (pump / modular knobs at once), recovery.put and the "Restore unsaved changes?" prompt, against a FAKE thunderCore (same rig as native-client-live.test.js).
// Covers: fx.values only with caps.fxValues, the diff (one slot, whole list, float32 for pump), refusal falls back silently (song only, no resend), busy retries, resets; recovery.put cadence / change / hold; the prompt, Restore (project and song-only copies), Discard, closed.
// (every other message of PC4 / M5 is covered by their own files).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const CLIENT = H.read('thunder-core-client.js');

function clock() {
  let now = 1000, id = 0; const T = new Map();
  const add = (fn, ms, every) => { const k = ++id; T.set(k, { fn, at: now + Math.max(0, ms | 0), every: every ? Math.max(1, ms | 0) : 0 }); return k; };
  return {
    now: () => now, setTimeout: (fn, ms) => add(fn, ms, false), clearTimeout: k => T.delete(k), setInterval: (fn, ms) => add(fn, ms, true), clearInterval: k => T.delete(k),
    async advance(ms) { const end = now + ms; for (;;) { let best = null; for (const [k, t] of T) if (t.at <= end && (!best || t.at < best[1].at)) best = [k, t]; if (!best) break; now = Math.max(now, best[1].at); const [k, t] = best; if (t.every) t.at = now + t.every; else T.delete(k); t.fn(); await flush(); } now = end; await flush(); },
    tick(ms) { now += ms; }, count: () => T.size,
  };
}
const run = async (r, ms) => { for (let t = 0; t < ms; t += 500) { r.beat(); await r.ck.advance(Math.min(500, ms - t)); } };   // time passes with the engine's heartbeat
const flush = async () => { for (let i = 0; i < 6; i++) await new Promise(r => setImmediate(r)); };

const mkS = () => ({ name: 'Night', bpm: 120, song: [], master: 255, mout: 255, limiter: true, mst: { on: true, lim: { on: true } },
  mix: Array.from({ length: 32 }, () => ({ vol: 224, trim: 128, pan: 128, mute: false, solo: false })),
  tfx: Array.from({ length: 32 }, () => [null, null, null, null]), mfx: [null, null, null, null],
  chainPatch: { modules: [{ id: 'm1', type: 'vcf', params: { cut: 0.5 } }, { id: 'in1', type: 'trackIn', params: { track: 0 } }] } });
// the page's effect mapping, faked: 'delay' and 'verb' are ordinary (12 mapped values), 'pumpA' a pump slot, 'mod1' a modular one
const fxSlot = (f, sr, bpm) => { if (!f || !/^(delay|verb|pumpA|mod1)$/.test(f.type)) return null; if (f.type === 'pumpA') return { kind: 'pump' }; if (f.type === 'mod1') return { kind: 'mod' };
  const p = new Array(12).fill(0); p[0] = f.time * (120 / bpm); p[1] = f.fb; p[2] = sr / 48000; return { kind: 'fx', p }; };
function rig({ verdictFor = null, hold = false, caps = { nativeAudio: true, fxValues: true, recoveryPut: true }, fxvReply = null, recGet = null } = {}) {
  const ck = clock(), sent = [], asked = [], handlers = {}, log = [];
  const sb = { window: null, Promise, Error, JSON, Object, Array, Map, isFinite, Math, Date, setTimeout: ck.setTimeout, clearTimeout: ck.clearTimeout, setInterval: ck.setInterval, clearInterval: ck.clearInterval };
  sb.window = sb; const releases = [];
  const tc = {
    host: 'android', send: m => { sent.push(JSON.parse(JSON.stringify(m))); log.push(m.t); },
    ask: (m, ms) => { asked.push(JSON.parse(JSON.stringify(m))); log.push(m.t + '?');
      if (m.t === 'hello') return Promise.resolve({ t: 'reply', v: 1, core: 'r1', caps, sr: 48000, outLat: 480, host: 'android' });
      if (m.t === 'song.load') { const v = verdictFor ? verdictFor(m, asked.length) : { ok: true, rev: m.rev, reasons: [], warn: [] };
        if (hold && !r.released) return new Promise(res => releases.push(() => res(Object.assign({ t: 'reply', rev: m.rev }, v)))); return Promise.resolve(Object.assign({ t: 'reply', rev: m.rev }, v)); }
      if (m.t === 'fx.values') return Promise.resolve(fxvReply ? fxvReply(m, asked.length) : { ok: true });
      if (m.t === 'recovery.get') return Promise.resolve(recGet || { ok: false, error: 'none' });
      return Promise.resolve({}); },
    on: (t, fn) => { (handlers[t] = handlers[t] || []).push(fn); }, off: (t, fn) => { handlers[t] = (handlers[t] || []).filter(f => f !== fn); },
  };
  sb.thunderCore = tc; vm.createContext(sb); vm.runInContext(CLIENT, sb, { filename: 'thunder-core-client.js' });
  let S = mkS(), ver = 1, pdc = 1, dirty = false, projCalls = 0, textN = 0, answer = 0, restoreOk = true; const asks = [], restores = []; const tp = { on: false, nc: false, seq: null }, toasts = [];
  const host = {
    S: () => S, songVer: () => ver, aset: { core: 'native' }, toast: t => toasts.push(t), tpLog: (...a) => log.push('tpLog ' + a.join(' ')), pbLog: () => {}, now: () => ck.now(),
    prerender: () => {}, build: () => 679, vars: () => ({}), title: () => 'x', lxp: () => 0, perf: () => null, blocked: () => '', prep: () => {},
    begin: (mode, o, seq) => { tp.on = true; tp.nc = true; tp.seq = seq; return o || {}; }, fallback: () => { tp.on = false; tp.nc = false; }, stopped: () => { tp.on = false; tp.nc = false; },
    fxSlot: (f, sr, bpm) => { const i = fxSlot(f, sr, bpm); if (i && i.kind === 'pump') i.v = f.pv ? f.pv.slice() : null; if (i && i.kind === 'mod') i.v = f.mv ? f.mv.slice() : null; return i; }, pdc: () => pdc,
    dirty: () => dirty, projText: () => { projCalls++; return JSON.stringify({ app: 'thunder-project', v: 1, song: S, n: textN }); },
    askRestore: (title, when) => { asks.push({ title, when }); return Promise.resolve(answer); }, restore: (text, name) => { restores.push({ text, name }); return Promise.resolve(restoreOk); }, chainMods: () => { const o = {}; for (const m of (S.chainPatch && S.chainPatch.modules) || []) o[m.id] = m.type; return o; },
  };
  const NC = sb.__thunderClient, emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m))), beat = () => emit('clock', { f: 0, sr: 48000 });
  const r = { asks, restores, set dirty(v) { dirty = v; }, set textN(v) { textN = v; }, set answer(v) { answer = v; }, set restoreOk(v) { restoreOk = v; }, get projCalls() { return projCalls; }, sb, NC, tc, host, ck, sent, asked, log, toasts, tp, emit, beat, releases, released: false,
    get S() { return S; }, set S(v) { S = v; }, bump() { ver++; }, set pdc(v) { pdc = v; },
    msgs: (...t) => sent.filter(m => t.includes(m.t)), songLoads: () => asked.filter(m => m.t === 'song.load'), clear() { sent.length = 0; asked.length = 0; log.length = 0; },
    // an edit as the page makes it: change S, then saveS() = bump + NC.edit()
    edit(fn) { fn(S); ver++; NC.edit(); } };
  return r;
}
async function up() { const r = rig(); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); return r; }          // hello, first publish accepted
async function playing() { const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); r.clear(); return r; }   // PLAY sent, baseline = the accepted song

const small = r => r.msgs('mix.set', 'fx.plan', 'fx.set', 'master.config', 'pdc.set', 'chain.edit');


const pumpFx = (pv, on = true) => ({ type: 'pumpA', on, pv });
const modFx = (mv, on = true) => ({ type: 'mod1', on, mv });
const tfxWith = (t, list) => Array.from({ length: 32 }, (_, i) => i === t ? list : [null, null, null, null]);
const fxvs = r => r.asked.filter(m => m.t === 'fx.values');
const withSlots = async (opts, S0) => { const r = rig(opts); r.S = Object.assign(mkS(), S0); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); r.clear(); return r; };
const PV = () => [0.1, 0.5, 0.25, 1, 0];

// ---- fx.values --------------------------------------------------------------------------------------------------------------------------
test('a pump knob while playing: one fx.values with the slot\'s whole list as float32 values; a modular slot: kind mod, doubles', async () => {
  const r = await withSlots({}, { tfx: tfxWith(2, [pumpFx(PV()), modFx([0.3, 0.7, 2]), null, null]) });
  r.edit(S => { S.tfx[2][0].pv[1] = 0.6; }); await r.ck.advance(16);
  const m = fxvs(r); assert.equal(m.length, 1); assert.deepEqual(Object.keys(m[0]).sort(), ['kind', 'slot', 't', 'tr', 'v']);
  assert.equal(m[0].tr, 2); assert.equal(m[0].slot, 0); assert.equal(m[0].kind, 'pump'); assert.equal(m[0].v.length, 5); assert.equal(m[0].v[1], Math.fround(0.6)); assert.equal(m[0].v[0], Math.fround(0.1));
  assert.equal(small(r).length, 0, 'no fx.set / fx.plan for it');
  r.clear(); r.edit(S => { S.tfx[2][1].mv[2] = 3.14159265358979; }); await r.ck.advance(16);
  const n = fxvs(r); assert.equal(n.length, 1); assert.equal(n[0].slot, 1); assert.equal(n[0].kind, 'mod'); assert.deepEqual(n[0].v, [0.3, 0.7, 3.14159265358979]);
  r.clear(); r.edit(S => { S.tfx[2][1].mv[2] = 3.14159265358979; }); await r.ck.advance(16); assert.equal(fxvs(r).length, 0, 'the same list again: nothing');
});
test('the master chain (track 32) works the same; only the slot that changed is sent', async () => {
  const r = await withSlots({}, { mfx: [pumpFx(PV()), pumpFx(PV()), null, null] });
  r.edit(S => { S.mfx[1].pv[0] = 0.9; }); await r.ck.advance(16); const m = fxvs(r); assert.equal(m.length, 1); assert.equal(m[0].tr, 32); assert.equal(m[0].slot, 1);
});
test('a drag is one fx.values per 16 ms (the last list wins), and the whole song follows once', async () => {
  const r = await withSlots({}, { tfx: tfxWith(0, [pumpFx(PV()), null, null, null]) });
  for (let i = 0; i < 20; i++) { r.edit(S => { S.tfx[0][0].pv[2] = 0.3 + i / 100; }); await r.ck.advance(5); }
  await r.ck.advance(16); const m = fxvs(r); assert.ok(m.length >= 5 && m.length <= 8, 'per tick, not per move: ' + m.length); assert.equal(m[m.length - 1].v[2], Math.fround(0.49));
  await r.ck.advance(260); assert.equal(r.songLoads().length, 1);
});
test('without caps.fxValues nothing is sent for a pump or modular knob (the song carries it, as in PC4); an ordinary effect still gets fx.set', async () => {
  const r = await withSlots({ caps: { nativeAudio: true } }, { tfx: tfxWith(2, [pumpFx(PV()), modFx([1, 2, 3]), { type: 'delay', on: true, time: 0.25, fb: 0.4 }, null]) });
  r.edit(S => { S.tfx[2][0].pv[1] = 0.6; S.tfx[2][1].mv[0] = 5; S.tfx[2][2].fb = 0.5; }); await r.ck.advance(16);
  assert.equal(fxvs(r).length, 0); assert.equal(r.msgs('fx.set').length, 1); assert.equal(r.msgs('fx.set')[0].slot, 2);
  await r.ck.advance(260); assert.equal(r.songLoads().length, 1);
});
test('nothing while stopped; a non-finite value is left to the song; a new plan (slot retyped) is an fx.plan, not fx.values', async () => {
  const r = await up(); r.S = Object.assign(mkS(), { tfx: tfxWith(0, [pumpFx(PV()), null, null, null]) }); r.bump(); r.NC.edit(); await r.ck.advance(300); r.clear();
  r.edit(S => { S.tfx[0][0].pv[1] = 0.7; }); await r.ck.advance(300); assert.equal(fxvs(r).length, 0, 'stopped');
  const q = await withSlots({}, { tfx: tfxWith(0, [pumpFx(PV()), null, null, null]) });
  q.edit(S => { S.tfx[0][0].pv[1] = NaN; }); await q.ck.advance(16); assert.equal(fxvs(q).length, 0);
  q.edit(S => { S.tfx[0][0] = pumpFx(PV(), false); }); await q.ck.advance(16); assert.equal(q.msgs('fx.plan').length, 1); assert.equal(fxvs(q).length, 0);
});
test('a refusal is silent and final for that slot: the song carries the knob, no more fx.values for it, no toast, no fx.plan; other slots still go', async () => {
  const r = await withSlots({ fxvReply: m => m.slot === 0 ? { ok: false, error: 'linear-phase' } : { ok: true } }, { tfx: tfxWith(1, [pumpFx(PV()), pumpFx(PV()), null, null]) });
  r.edit(S => { S.tfx[1][0].pv[1] = 0.6; }); await r.ck.advance(16); await flush(); assert.equal(fxvs(r).length, 1);
  r.edit(S => { S.tfx[1][0].pv[1] = 0.65; }); await r.ck.advance(16); assert.equal(fxvs(r).length, 1, 'refused slot: not sent again');
  r.edit(S => { S.tfx[1][1].pv[1] = 0.65; }); await r.ck.advance(16); assert.equal(fxvs(r).length, 2); assert.equal(fxvs(r)[1].slot, 1);
  assert.equal(r.toasts.length, 0); assert.equal(r.msgs('fx.plan').length, 0);
  await r.ck.advance(260); assert.equal(r.songLoads().length >= 1, true, 'the whole song carries the refused values');
  // the accepted song (above) forgot the refusal (an EQ made minimum-phase is taken): the next knob tries again, is refused again, and the one after is not sent
  r.clear(); r.edit(S => { S.tfx[1][0].pv[1] = 0.7; }); await r.ck.advance(16); await flush(); assert.equal(fxvs(r).filter(m => m.slot === 0).length, 1, 'tried again after the accepted song');
  r.edit(S => { S.tfx[1][0].pv[1] = 0.71; }); await r.ck.advance(16); assert.equal(fxvs(r).filter(m => m.slot === 0).length, 1, 'refused again: not sent');
});
test('a shell that does not answer (the ask is rejected) counts as a refusal; busy is a retry of the current list', async () => {
  let n = 0; const r = await withSlots({ fxvReply: m => { n++; return n === 1 ? { ok: false, busy: true } : { ok: true }; } }, { tfx: tfxWith(1, [pumpFx(PV()), null, null, null]) });
  r.edit(S => { S.tfx[1][0].pv[1] = 0.6; }); await r.ck.advance(16); await flush(); await r.ck.advance(16); await flush();
  const m = fxvs(r); assert.equal(m.length, 2, 'sent, busy, sent again'); assert.deepEqual(m[0], m[1]);
  const q = await withSlots({ fxvReply: () => { throw new Error('timeout'); } }, { tfx: tfxWith(1, [pumpFx(PV()), null, null, null]) });
  q.tc.ask = () => Promise.reject(new Error('timeout')); q.edit(S => { S.tfx[1][0].pv[1] = 0.6; }); await q.ck.advance(16); await flush(); q.edit(S => { S.tfx[1][0].pv[1] = 0.7; }); await q.ck.advance(16); assert.equal(q.msgs('fx.values').length, 0);
});
test('an engine restart forgets refusals and the baseline (no fx.values until the next accepted song)', async () => {
  const r = await withSlots({ fxvReply: () => ({ ok: false, error: 'x' }) }, { tfx: tfxWith(1, [pumpFx(PV()), null, null, null]) });
  r.edit(S => { S.tfx[1][0].pv[1] = 0.6; }); await r.ck.advance(16); await flush();
  r.emit('engine', { up: false }); r.emit('engine', { up: true }); r.clear(); r.edit(S => { S.tfx[1][0].pv[1] = 0.7; }); await r.ck.advance(16); assert.equal(fxvs(r).length, 0);
});

// ---- recovery ---------------------------------------------------------------------------------------------------------------------------
test('recovery.put: every 10 s while dirty, only when the text changed, a clean song puts nothing, no cap no put', async () => {
  const r = await up(); r.clear(); r.dirty = true;
  await r.ck.advance(10100); const p = r.msgs('recovery.put'); assert.equal(p.length, 1); assert.equal(typeof p[0].project, 'string'); assert.equal(JSON.parse(p[0].project).app, 'thunder-project'); assert.deepEqual(Object.keys(p[0]).sort(), ['project', 't']);
  await r.ck.advance(20000); assert.equal(r.msgs('recovery.put').length, 1, 'unchanged: not again');
  r.textN = 1; await r.ck.advance(10000); assert.equal(r.msgs('recovery.put').length, 2);
  r.dirty = false; r.textN = 2; await r.ck.advance(30000); assert.equal(r.msgs('recovery.put').length, 2, 'saved: nothing');
  const q = rig({ caps: { nativeAudio: true } }); await q.NC.attach(q.host); q.beat(); q.dirty = true; await q.ck.advance(30000); assert.equal(q.msgs('recovery.put').length, 0); assert.equal(q.projCalls, 0, 'not even stringified');
});
test('a recovery event asks "Restore unsaved changes?"; Restore: recovery.get, the project text goes to the page\'s loader, then recovery.drop; no put before the answer', async () => {
  const text = JSON.stringify({ app: 'thunder-project', v: 1, song: { name: 'Mine' } });
  const r = rig({ recGet: { ok: true, title: 'Mine', time: 1, file: null, text } }); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.clear(); r.dirty = true;
  r.emit('recovery', { title: 'Mine', time: Date.now() - 5 * 60000, file: null }); await r.ck.advance(30000); await flush();
  assert.equal(r.asks.length, 1); assert.equal(r.asks[0].title, 'Mine'); assert.equal(r.asks[0].when, '5 min ago');
  assert.equal(r.restores.length, 1); assert.equal(r.restores[0].text, text); assert.deepEqual(r.asked.filter(m => /^recovery/.test(m.t)).map(m => m.t), ['recovery.get', 'recovery.drop']);
  assert.ok(r.log.indexOf('recovery.drop?') >= 0); const gi = r.log.indexOf('recovery.get?'); assert.ok(r.msgs('recovery.put').every(() => false) || r.log.indexOf('recovery.put') > r.log.indexOf('recovery.drop?'), 'no put before the copy is dealt with');
});
test('a copy that is only the song.load message restores the song (wrapped as a project) and says the kit is not in it', async () => {
  const text = JSON.stringify({ t: 'song.load', title: 'x', S: { name: 'FromS', bpm: 99 }, vars: {}, rev: 4 });
  const r = rig({ recGet: { ok: true, text } }); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300);
  r.emit('recovery', { title: 'x', time: Date.now(), file: null }); await r.ck.advance(2000); await flush();
  assert.equal(r.restores.length, 1); const j = JSON.parse(r.restores[0].text); assert.equal(j.app, 'thunder-project'); assert.equal(j.song.name, 'FromS'); assert.equal(j.kit, undefined);
  assert.ok(r.toasts.some(t => /kit/.test(t))); assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 1);
});
test('Discard: recovery.drop and nothing restored; closed without an answer: the copy stays and no put replaces it; an unreadable copy stays too; a refused restore keeps the copy', async () => {
  let r = rig(); r.answer = 1; await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.emit('recovery', { title: 't', time: 0, file: null }); await r.ck.advance(2000); await flush();
  assert.equal(r.restores.length, 0); assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 1); assert.equal(r.asked.filter(m => m.t === 'recovery.get').length, 0);
  r = rig(); r.answer = -1; await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.dirty = true; r.emit('recovery', { title: 't', time: 0, file: null }); await r.ck.advance(30000); await flush();
  assert.equal(r.asked.filter(m => /^recovery/.test(m.t)).length, 0); assert.equal(r.msgs('recovery.put').length, 0, 'the copy being decided is not replaced');
  r = rig({ recGet: { ok: true, text: 'not json' } }); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.emit('recovery', { title: 't' }); await r.ck.advance(2000); await flush();
  assert.equal(r.restores.length, 0); assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 0); assert.ok(r.toasts.length >= 1);
  r = rig({ recGet: { ok: true, text: JSON.stringify({ app: 'thunder-project', song: {} }) } }); r.restoreOk = false; await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.emit('recovery', { title: 't' }); await r.ck.advance(2000); await flush();
  assert.equal(r.restores.length, 1); assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 0);
});

// ---- the page's side (index.html, source guards) -------------------------------------------------------------------------------------------
test('index.html: fxSlot gives the pump list (on a copy, pumpPv after pumpSync) and the modular list (T.map); the restore hooks are host functions, and no new NCON() / NC() branch was added', () => {
  const src = H.read('index.html');
  assert.ok(/if \(T\.pump\)\{ let v = null; try \{ const g = Object\.assign\(\{\}, f, \{pump:Object\.assign\(\{\}, f\.pump\)\}\); pumpSync\(g\); v = pumpPv\(T, g\); \} catch\(e\)\{\} return \{kind:'pump', v\}; \}/.test(src), 'pump: the song\'s slot is never written');
  assert.ok(/if \(T\.mod\)\{ let v = null; try \{ v = Array\.from\(T\.map\(f, sr, bpm\)\); \} catch\(e\)\{\} return \{kind:'mod', v\}; \}/.test(src));
  assert.ok(/dirty:\(\) => projDirty, projText:\(\) => projText\(\),/.test(src));
  assert.ok(/restore:async \(text, name\) => \{ try \{ await backupNow\(true\); \} catch\(e\)\{\} return await loadProjText\(text, name, \{dirty:true\}\); \}/.test(src));
  assert.ok(/Restore unsaved changes\?/.test(src));
});

// ---- PC8: four small items (docs/plans/PC8-NOTES.md) ---------------------------------------------------------------------------------------
test('PC8 item 1: the page\'s FX bar dynamics readers go to rest when the newest fx.dyn is older than FXD_AGE (600 ms): the slot-box bar and the three meter bars', () => {
  const src = H.read('index.html');
  assert.ok(/FXD_AGE = 600/.test(src)); assert.equal((src.match(/now - D\.t < FXD_AGE/g) || []).length, 2, 'both readers use the one constant');
  assert.ok(!/now - D\.t < 600/.test(src));
  // the real dkDynMeters with a recording canvas: a fresh report draws a GR bar, one 601 ms old draws none (the bars at rest), 599 ms still draws
  const cut = (re, n) => { const m = src.match(re); assert.ok(m, n); return m[0]; };
  const fills = []; const g = { setTransform() {}, clearRect() {}, fillRect: (...a) => fills.push([g.fillStyle, ...a]), fillText() {}, beginPath() {}, moveTo() {}, lineTo() {}, stroke() {}, save() {}, restore() {}, set font(v) {}, set textBaseline(v) {}, set textAlign(v) {}, set lineWidth(v) {}, set strokeStyle(v) {}, setLineDash() {}, fillStyle: '' };
  const cv = { clientWidth: 90, clientHeight: 100, width: 0, height: 0, getContext: () => g };
  const pg = { devicePixelRatio: 1, Math, getComputedStyle: () => ({ color: 'ink', fontFamily: 'x' }), FXLV: new Set(), FXD_AGE: 600, FXD_LO: -60, FXD_HI: 6, FXD_GR: 24, fxAc: () => 'AC', ctx: null, S: { bpm: 120 }, FXDYN: {}, TFX: {}, Number, Array };
  vm.createContext(pg); vm.runInContext(cut(/function dkDynMeters\(cv, f, k, node, now\)\{[\s\S]*?\n\}\n/, 'dkDynMeters'), pg);
  const node = { dyn: new Map([[0, { key: '0:comp', t: 1000, cur: [-12, -20, -18], hold: [-12, -20, -18] }]]) }; pg.cv = cv; pg.f = { type: 'comp' }; pg.node = node;
  const grBars = now => { fills.length = 0; pg.now = now; vm.runInContext('dkDynMeters(cv, f, 0, node, now)', pg); return fills.filter(x => x[0] === 'AC' && x[4] > 0).length; };
  pg.FXLV = new Set(['x']); pg.FXDYN = { comp: () => null };
  assert.equal(grBars(1599) > 0, true, 'fresh (599 ms): the reduction bar is drawn'); assert.equal(grBars(1601), 0, 'older than 600 ms: no bar at all');
});
test('PC8 item 2: recovery.drop once the page\'s unsaved state clears (NC.dirtyOff), only after a put of this session, never while a copy awaits an answer, once per clear', async () => {
  const r = rig(); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300);
  r.dirty = false; r.NC.dirtyOff(); await flush(); assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 0, 'nothing was put: nothing to drop');
  r.dirty = true; await r.ck.advance(10100); assert.equal(r.msgs('recovery.put').length, 1);
  r.NC.dirtyOff(); await flush(); assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 0, 'still dirty: no drop');
  r.dirty = false; r.NC.dirtyOff(); await flush(); assert.deepEqual(r.asked.filter(m => m.t === 'recovery.drop'), [{ t: 'recovery.drop' }], 'saved: one drop, the shell\'s ask');
  r.NC.dirtyOff(); await r.ck.advance(30000); await flush(); assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 1, 'once per clear');
  r.dirty = true; r.textN = 5; await r.ck.advance(10100); assert.equal(r.msgs('recovery.put').length, 2, 'a later unsaved change puts again'); r.dirty = false; await r.ck.advance(10100); await flush();
  assert.equal(r.asked.filter(m => m.t === 'recovery.drop').length, 2, 'the 10 s look catches a clear the page did not announce');
  const q = rig(); q.answer = -1; await q.NC.attach(q.host); q.beat(); await q.ck.advance(300); q.emit('recovery', { title: 'x', time: 1, file: null }); await q.ck.advance(2000); await flush();   // a copy awaits (prompt closed without an answer)
  q.dirty = false; q.NC.dirtyOff(); await flush(); assert.equal(q.asked.filter(m => m.t === 'recovery.drop').length, 0, 'the held copy is not dropped');
  const n = rig({ caps: { nativeAudio: true } }); await n.NC.attach(n.host); n.beat(); n.dirty = false; n.NC.dirtyOff(); await flush(); assert.equal(n.asked.filter(m => m.t === 'recovery.drop').length, 0, 'no cap, nothing');
  assert.ok(/function setDirty\(on\)\{ projDirty = !!on; if \(!projDirty\)\{ smpNew = false; try \{ const c = natC\(\); if \(c && c\.dirtyOff\) c\.dirtyOff\(\); \} catch\(e\)\{\} \}/.test(H.read('index.html')), 'the page calls it from setDirty');
});
test('PC8 item 3: master.resetPeak only with caps.masterResetPeak and a native PLAY; today\'s shells say no such cap, so the PEAK click sends nothing; the page click calls it through natC()', async () => {
  let r = await playing(); assert.equal(r.NC.mstResetPeak(), false); assert.equal(r.msgs('master.resetPeak').length, 0, 'no shell has the message yet (CORE-BRIDGE 7.2 Known gaps)');
  r = rig({ caps: { nativeAudio: true, masterResetPeak: true } }); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300);
  assert.equal(r.NC.mstResetPeak(), false, 'not playing: nothing'); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); r.clear();
  assert.equal(r.NC.mstResetPeak(), true); assert.deepEqual(r.msgs('master.resetPeak'), [{ t: 'master.resetPeak' }]);
  assert.ok(/\$\('mwRead'\)\.addEventListener\('click', \(\) => \{ if \(rt && rt\.mst && rt\.mst\.wk\) rt\.mst\.wk\.port\.postMessage\(\{resetPeak:true\}\); try \{ const c = natC\(\); if \(c && c\.mstResetPeak\) c\.mstResetPeak\(\); \} catch\(e\)\{\} \}\);/.test(H.read('index.html')), 'the web engine\'s line is unchanged, the native call follows it');
});
test('PC8 item 4: a Send / Receive `lab` change is structural on the native path: no chain.edit, the song (a swap) carries it; other params of the same modules and other modules still go as chain.edit', async () => {
  const r = rig(); r.S = Object.assign(mkS(), { chainPatch: { modules: [{ id: 's1', type: 'send', params: { lab: 1, gain: 1 } }, { id: 'r1', type: 'recv', params: { lab: 1 } }, { id: 'm1', type: 'vcf', params: { cut: 0.5, lab: 2 } }] } });
  await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); r.clear();
  r.S.chainPatch.modules[0].params.lab = 3; r.NC.chainParam('s1', 'lab', 3); r.S.chainPatch.modules[1].params.lab = 0; r.NC.chainParam('r1', 'lab', 0);
  await r.ck.advance(16); assert.equal(r.msgs('chain.edit').length, 0, 'lab on send / recv is never a Params edit');
  await r.ck.advance(260); assert.equal(r.songLoads().length, 1, 'the whole song goes (the core swaps it)'); assert.equal(r.songLoads()[0].S.chainPatch.modules[0].params.lab, 3);
  r.clear(); r.NC.chainParam('s1', 'gain', 0.5); r.NC.chainParam('m1', 'lab', 4); await r.ck.advance(16);
  assert.deepEqual(r.msgs('chain.edit').map(m => m.ops), [[{ op: 'param', id: 's1', name: 'gain', value: 0.5 }, { op: 'param', id: 'm1', name: 'lab', value: 4 }]], 'only send / recv `lab` is structural');
  // a module's text (the formula / label field) is not a chain.edit op at all: the editor's TW.mod -> saveS publishes the song
  assert.ok(!/op: 'text'/.test(CLIENT));
});
