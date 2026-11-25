// PC4 of the native page client (docs/port/N2-PAGE-CLIENT.md 4.4 step 1, CORE-BRIDGE 7): live edits against a FAKE thunderCore. No browser: the client runs in a vm with a fake host and a fake clock.
// Covers: one small message per changed thing (mix.set, fx.plan, fx.set, master.config, pdc.set, chain.edit), coalescing (a drag is one message per 16 ms, no song.load storm),
// the whole song still following, nothing sent while stopped, and every path that resets the `sent` signature (a stale one is a silent fader).
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
function rig({ verdictFor = null, hold = false } = {}) {
  const ck = clock(), sent = [], asked = [], handlers = {}, log = [];
  const sb = { window: null, Promise, Error, JSON, Object, Array, Map, isFinite, Math, Date, setTimeout: ck.setTimeout, clearTimeout: ck.clearTimeout, setInterval: ck.setInterval, clearInterval: ck.clearInterval };
  sb.window = sb; const releases = [];
  const tc = {
    host: 'android', send: m => { sent.push(JSON.parse(JSON.stringify(m))); log.push(m.t); },
    ask: (m, ms) => { asked.push(JSON.parse(JSON.stringify(m))); log.push(m.t + '?');
      if (m.t === 'hello') return Promise.resolve({ t: 'reply', v: 1, core: 'r1', caps: { nativeAudio: true }, sr: 48000, outLat: 480, host: 'android' });
      if (m.t === 'song.load') { const v = verdictFor ? verdictFor(m, asked.length) : { ok: true, rev: m.rev, reasons: [], warn: [] };
        if (hold && !r.released) return new Promise(res => releases.push(() => res(Object.assign({ t: 'reply', rev: m.rev }, v)))); return Promise.resolve(Object.assign({ t: 'reply', rev: m.rev }, v)); }
      return Promise.resolve({}); },
    on: (t, fn) => { (handlers[t] = handlers[t] || []).push(fn); }, off: (t, fn) => { handlers[t] = (handlers[t] || []).filter(f => f !== fn); },
  };
  sb.thunderCore = tc; vm.createContext(sb); vm.runInContext(CLIENT, sb, { filename: 'thunder-core-client.js' });
  let S = mkS(), ver = 1, pdc = 1; const tp = { on: false, nc: false, seq: null }, toasts = [];
  const host = {
    S: () => S, songVer: () => ver, aset: { core: 'native' }, toast: t => toasts.push(t), tpLog: (...a) => log.push('tpLog ' + a.join(' ')), pbLog: () => {}, now: () => ck.now(),
    prerender: () => {}, build: () => 679, vars: () => ({}), title: () => 'x', lxp: () => 0, perf: () => null, blocked: () => '', prep: () => {},
    begin: (mode, o, seq) => { tp.on = true; tp.nc = true; tp.seq = seq; return o || {}; }, fallback: () => { tp.on = false; tp.nc = false; }, stopped: () => { tp.on = false; tp.nc = false; },
    fxSlot, pdc: () => pdc, chainMods: () => { const o = {}; for (const m of (S.chainPatch && S.chainPatch.modules) || []) o[m.id] = m.type; return o; },
  };
  const NC = sb.__thunderClient, emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m))), beat = () => emit('clock', { f: 0, sr: 48000 });
  const r = { sb, NC, tc, host, ck, sent, asked, log, toasts, tp, emit, beat, releases, released: false,
    get S() { return S; }, set S(v) { S = v; }, bump() { ver++; }, set pdc(v) { pdc = v; },
    msgs: (...t) => sent.filter(m => t.includes(m.t)), songLoads: () => asked.filter(m => m.t === 'song.load'), clear() { sent.length = 0; asked.length = 0; log.length = 0; },
    // an edit as the page makes it: change S, then saveS() = bump + NC.edit()
    edit(fn) { fn(S); ver++; NC.edit(); } };
  return r;
}
async function up() { const r = rig(); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); return r; }          // hello, first publish accepted
async function playing() { const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); r.clear(); return r; }   // PLAY sent, baseline = the accepted song

const small = r => r.msgs('mix.set', 'fx.plan', 'fx.set', 'master.config', 'pdc.set', 'chain.edit');

// ---- the mixer ----------------------------------------------------------------------------------------------------------------------------
test('a changed fader is one mix.set (page values 0..255, the page\'s 12 ms law), and nothing else is sent for it', async () => {
  const r = await playing();
  r.edit(S => { S.mix[3].vol = 100; }); await r.ck.advance(16);
  assert.deepEqual(r.msgs('mix.set'), [{ t: 'mix.set', tr: 3, k: 'vol', v: 100, ramp: -1 }]); assert.equal(small(r).length, 1);
  r.clear(); r.edit(S => { S.mix[3].vol = 100; }); await r.ck.advance(16); assert.equal(small(r).length, 0, 'the same value again: nothing');
});
test('trim, pan, mute, solo, master and OUT: one message each; mute and solo glide 12 ms in frames; the master is track 32', async () => {
  const r = await playing();
  r.edit(S => { S.mix[0].trim = 140; S.mix[1].pan = 20; S.mix[2].mute = true; S.mix[4].solo = true; S.master = 200; S.mout = 180; }); await r.ck.advance(16);
  assert.deepEqual(r.msgs('mix.set'), [
    { t: 'mix.set', tr: 0, k: 'trim', v: 140, ramp: -1 }, { t: 'mix.set', tr: 1, k: 'pan', v: 20, ramp: -1 }, { t: 'mix.set', tr: 2, k: 'mute', v: 1, ramp: 576 },
    { t: 'mix.set', tr: 4, k: 'solo', v: 1, ramp: 576 }, { t: 'mix.set', tr: 32, k: 'vol', v: 200, ramp: -1 }, { t: 'mix.set', tr: 32, k: 'out', v: 180, ramp: -1 }]);
  r.clear(); r.edit(S => { S.mix[2].mute = false; }); await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set'), [{ t: 'mix.set', tr: 2, k: 'mute', v: 0, ramp: 576 }]);
});
test('every key the page can send is one the shell takes (MIX_KEYS), and send is never sent', async () => {
  const r = await playing(); r.edit(S => { S.mix.forEach((m, i) => { m.vol = i; m.trim = i; m.pan = i; m.mute = true; m.solo = true; S.mix[i].sc = true; S.mix[i].send = 77; }); S.master = 1; S.mout = 2; }); await r.ck.advance(16);
  const keys = new Set(r.msgs('mix.set').map(m => m.k)); assert.deepEqual([...keys].sort(), ['mute', 'out', 'pan', 'solo', 'trim', 'vol']);
  for (const m of r.msgs('mix.set')) { assert.ok(m.tr >= 0 && m.tr <= 32); assert.ok(m.v >= 0 && m.v <= 255); }
});
test('a fader drag: sixty edits in a second are about sixty mix.set (one per 16 ms, the last value wins), and ONE song.load after the quiet 250 ms', async () => {
  const r = await playing();
  for (let i = 0; i < 20; i++) { r.S.mix[5].vol = 100 + i; r.NC.edit(); await r.ck.advance(5); }   // pointer moves every 5 ms: mfSet changes S and calls edit(), no saveS yet
  await r.ck.advance(16);
  const ms = r.msgs('mix.set'); assert.ok(ms.length >= 5 && ms.length <= 7, 'one per 16 ms, not one per move: ' + ms.length); assert.equal(ms[ms.length - 1].v, 119); assert.ok(ms.every(m => m.tr === 5));
  assert.equal(r.songLoads().length, 0, 'no song.load during the drag (the song was not saved: mfSet does not saveS)');
  r.bump(); r.NC.edit();   // pointer up: mixChanged -> saveS
  await r.ck.advance(260); assert.equal(r.songLoads().length, 1, 'one whole song after the drag');
  assert.ok(r.log.lastIndexOf('mix.set') < r.log.indexOf('song.load?'), 'the small messages go before the song');
});
test('with saveS on every move (the wheel, the keys): still one song.load, 250 ms after the last', async () => {
  const r = await playing();
  for (let i = 0; i < 30; i++) { r.edit(S => { S.mix[1].pan = 60 + i; }); await r.ck.advance(10); }
  assert.equal(r.songLoads().length, 0); await r.ck.advance(260); assert.equal(r.songLoads().length, 1);
});
test('edits are not sent while stopped; the song takes them, and PLAY then sends no mix.set for them', async () => {
  const r = await up(); r.clear();
  r.edit(S => { S.mix[1].vol = 50; }); await r.ck.advance(300);
  assert.equal(small(r).length, 0, 'stopped: the whole song only'); assert.equal(r.songLoads().length, 1);
  r.NC.start('song', { row: 0 }); await r.ck.advance(50); assert.equal(r.msgs('play').length, 1); assert.equal(small(r).length, 0, 'the accepted song is the baseline: the fader is not sent again');
  r.edit(S => { S.mix[1].vol = 51; }); await r.ck.advance(16); assert.equal(r.msgs('mix.set').length, 1);
});

// ---- effects ------------------------------------------------------------------------------------------------------------------------------
const withFx = async S0 => { const r = rig(); r.S = Object.assign(mkS(), S0); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.beat(); r.clear(); return r; };
test('a knob on an effect that stays in its slot: fx.set with the 12 mapped values, only for the slots that changed', async () => {
  const r = await withFx({ tfx: Array.from({ length: 32 }, (_, t) => t === 2 ? [{ type: 'delay', on: true, time: 0.25, fb: 0.4 }, { type: 'verb', on: true, time: 1, fb: 0.2 }, null, null] : [null, null, null, null]) });
  r.edit(S => { S.tfx[2][0].fb = 0.6; }); await r.ck.advance(16);
  assert.equal(small(r).length, 1); const m = r.msgs('fx.set')[0]; assert.equal(m.tr, 2); assert.equal(m.slot, 0); assert.equal(m.p.length, 12); assert.equal(m.p[1], 0.6); assert.equal(m.p[0], 0.25);
});
test('a new effect, a removed one, a different type, on / off: fx.plan with the track\'s slot list and the tempo; never fx.set', async () => {
  const r = await withFx({ tfx: Array.from({ length: 32 }, (_, t) => t === 2 ? [{ type: 'delay', on: true, time: 0.25, fb: 0.4 }, null, null, null] : [null, null, null, null]) });
  r.edit(S => { S.tfx[2][1] = { type: 'verb', on: true, time: 1, fb: 0.2 }; }); await r.ck.advance(16);
  assert.equal(r.msgs('fx.plan').length, 1); const m = r.msgs('fx.plan')[0]; assert.equal(m.tr, 2); assert.equal(m.bpm, 120); assert.equal(m.slots.length, 4); assert.equal(m.slots[1].type, 'verb'); assert.equal(small(r).length, 1);
  r.clear(); r.edit(S => { S.tfx[2][0].on = false; }); await r.ck.advance(16); assert.equal(r.msgs('fx.plan').length, 1, 'on / off is a plan change');
  r.clear(); r.edit(S => { S.tfx[2][0].type = 'verb'; }); await r.ck.advance(16); assert.equal(r.msgs('fx.plan').length, 1);
  r.clear(); r.edit(S => { S.tfx[2][1] = null; }); await r.ck.advance(16); assert.equal(r.msgs('fx.plan').length, 1); assert.equal(r.msgs('fx.set').length, 0);
});
test('the master effects are track 32; a tempo change moves a tempo-mapped effect with fx.set, an unrelated edit sends nothing', async () => {
  const r = await withFx({ mfx: [{ type: 'delay', on: true, time: 0.25, fb: 0.4 }, null, null, null] });
  r.edit(S => { S.mfx[0].fb = 0.1; }); await r.ck.advance(16); assert.equal(r.msgs('fx.set')[0].tr, 32);
  r.clear(); r.edit(S => { S.bpm = 60; }); await r.ck.advance(16); assert.equal(r.msgs('fx.set').length, 1, 'p[0] depends on the tempo'); assert.equal(r.msgs('fx.set')[0].p[0], 0.5);
  r.clear(); r.edit(S => { S.name = 'Other'; S.song[3] = [1]; }); await r.ck.advance(16); assert.equal(small(r).length, 0);
});
test('a pump or modular slot: its parameters go with the song (no fx.set exists for them), a change of what is in the slot is an fx.plan and recomputes the PDC plan', async () => {
  const r = await withFx({ tfx: Array.from({ length: 32 }, (_, t) => t === 1 ? [{ type: 'pumpA', on: true, pump: { a: 1 } }, { type: 'mod1', on: true, k: 1 }, null, null] : [null, null, null, null]) });
  r.edit(S => { S.tfx[1][0].pump.a = 2; S.tfx[1][1].k = 2; }); await r.ck.advance(16); assert.equal(small(r).length, 0, 'a pump / module knob: the song');
  r.clear(); r.edit(S => { S.tfx[1][0].on = false; }); await r.ck.advance(16);
  assert.deepEqual(small(r).map(m => m.t), ['fx.plan', 'pdc.set']); assert.equal(r.msgs('pdc.set')[0].mode, 1);
  r.clear(); r.edit(S => { S.tfx[1][0].pump.a = 3; }); await r.ck.advance(16); assert.equal(small(r).length, 0, 'the baseline took the plan: a later pump knob is still the song');
});
test('an effect whose mapped values the shell would refuse (not finite) is left to the song; an unknown effect type is a structure only', async () => {
  const r = await withFx({ tfx: Array.from({ length: 32 }, (_, t) => t === 2 ? [{ type: 'delay', on: true, time: 0.25, fb: 0.4 }, { type: 'zzz', on: true }, null, null] : [null, null, null, null]) });
  r.edit(S => { S.tfx[2][0].fb = Infinity; S.tfx[2][1].x = 5; }); await r.ck.advance(16); assert.equal(small(r).length, 0);
});

// ---- master chain, PDC ----------------------------------------------------------------------------------------------------------------
test('master.config carries the mastering object and the safety limiter, once per change', async () => {
  const r = await playing();
  r.edit(S => { S.mst.lim.on = false; }); await r.ck.advance(16); assert.deepEqual(r.msgs('master.config'), [{ t: 'master.config', mst: { on: true, lim: { on: false } }, safety: true }]);
  r.clear(); r.edit(S => { S.limiter = false; }); await r.ck.advance(16); assert.equal(r.msgs('master.config')[0].safety, false); assert.equal(small(r).length, 1);
  r.clear(); r.edit(S => { S.name = 'x'; }); await r.ck.advance(16); assert.equal(small(r).length, 0);
});
test('PDC: PLAY carries the mode; a mode change while playing is one pdc.set (the page calls edit() from pdcSet: no song change)', async () => {
  const r = await up(); r.pdc = 2; r.NC.start('song', { row: 0 }); await r.ck.advance(50); assert.equal(r.msgs('play')[0].pdc, 2); r.clear();
  await r.ck.advance(100); assert.equal(small(r).length, 0, 'unchanged');
  r.pdc = 0; r.NC.edit(); await r.ck.advance(16); assert.deepEqual(r.msgs('pdc.set'), [{ t: 'pdc.set', mode: 0 }]); r.NC.edit(); await r.ck.advance(16); assert.equal(r.msgs('pdc.set').length, 1);
  assert.equal(r.songLoads().length, 0, 'a settings change is not a song change');
});

// ---- chains -------------------------------------------------------------------------------------------------------------------------------
test('a chains slider while playing: one chain.edit {ops} (the last value per module and parameter), and the song follows once', async () => {
  const r = await playing();
  for (let i = 0; i < 10; i++) { r.S.chainPatch.modules[0].params.cut = i / 10; r.NC.chainParam('m1', 'cut', i / 10); await r.ck.advance(3); }
  await r.ck.advance(16); const ce = r.msgs('chain.edit'); assert.ok(ce.length >= 1 && ce.length <= 3, 'a few, not ten: ' + ce.length);
  assert.deepEqual(ce[ce.length - 1].ops, [{ op: 'param', id: 'm1', name: 'cut', value: 0.9 }]);
  assert.equal(r.msgs('mix.set', 'fx.set', 'fx.plan').length, 0);
  await r.ck.advance(260); assert.equal(r.songLoads().length, 1, 'param() does not saveS: the client makes the song stale itself');
});
test('two parameters in the same 16 ms are one chain.edit with two ops', async () => {
  const r = await playing(); r.NC.chainParam('m1', 'cut', 0.3); r.NC.chainParam('m1', 'res', 0.4); r.NC.chainParam('m1', 'cut', 0.35); await r.ck.advance(16);
  assert.deepEqual(r.msgs('chain.edit').map(m => m.ops), [[{ op: 'param', id: 'm1', name: 'cut', value: 0.35 }, { op: 'param', id: 'm1', name: 'res', value: 0.4 }]]);
});
test('what chain_edit would refuse (all or nothing) is not sent: Track In / Chain Out / sidechain modules, track / src / pre, a module the shell does not have yet, a value that is not a number; the song carries it', async () => {
  const r = await playing();
  r.NC.chainParam('in1', 'gain', 1); r.NC.chainParam('m1', 'track', 3); r.NC.chainParam('m1', 'src', 1); r.NC.chainParam('m1', 'pre', 1); r.NC.chainParam('newmod', 'cut', 0.1); r.NC.chainParam('m1', 'mode', 'hp'); r.NC.chainParam('m1', 'cut', NaN);
  await r.ck.advance(16); assert.equal(r.msgs('chain.edit').length, 0); await r.ck.advance(260); assert.equal(r.songLoads().length, 1);
});
test('a chains slider while stopped: only the song (stale), so a PLAY at once carries the value', async () => {
  const r = await up(); r.clear(); r.NC.chainParam('m1', 'cut', 0.7); await r.ck.advance(16); assert.equal(small(r).length, 0);
  r.NC.start('song', { row: 0 }); await r.ck.advance(20); assert.equal(r.songLoads().length, 1, 'PLAY publishes the stale song first'); assert.equal(r.msgs('play').length, 1);
});
test('a song with chains accepted while playing recomputes the PDC plan (live.h: "after a song_publish that changes the chains patch")', async () => {
  const r = await playing(); r.edit(S => { S.name = 'x'; }); await r.ck.advance(300); assert.equal(r.songLoads().length, 1); assert.deepEqual(r.msgs('pdc.set'), [{ t: 'pdc.set', mode: 1 }]);
});

// ---- the signature must never be stale -----------------------------------------------------------------------------------------------------
test('engine {up:true}: the signature is dropped; nothing is sent until the song is accepted again, then the baseline is the new song', async () => {
  const r = await playing(); r.edit(S => { S.mix[0].vol = 10; }); await r.ck.advance(16); assert.equal(r.msgs('mix.set').length, 1);
  r.emit('engine', { up: false }); r.emit('engine', { up: true }); r.clear();
  r.edit(S => { S.mix[0].vol = 11; }); await r.ck.advance(16);
  assert.equal(r.msgs('mix.set').length, 0, 'no baseline (the rebuilt core holds nothing): the whole song goes'); await r.ck.advance(260); assert.equal(r.songLoads().length >= 1, true);
  // the engine is back but the page is not playing (a rebuilt core stopped): the next PLAY sends the baseline's own values again, no stale skip
  r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.clear(); r.edit(S => { S.mix[0].vol = 10; }); await r.ck.advance(16);
  assert.deepEqual(r.msgs('mix.set'), [{ t: 'mix.set', tr: 0, k: 'vol', v: 10, ramp: -1 }], 'a value equal to the pre-rebuild one is sent: the baseline is the new song, not the old signature');
});
test('a new song (another S object, or NC.newSong): no comparison with the old one; the next accepted song is the baseline', async () => {
  const r = await playing(); const S2 = mkS(); S2.mix[0].vol = 7; r.S = S2; r.bump(); r.NC.edit(); await r.ck.advance(16);
  assert.equal(small(r).length, 0, 'never mix.set from the old song\'s numbers'); await r.ck.advance(260);
  r.clear(); r.edit(S => { S.mix[0].vol = 9; }); await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set').map(m => m.v), [9]);
  const r2 = await playing(); r2.NC.newSong(); r2.edit(S => { S.mix[0].vol = 9; }); await r2.ck.advance(16); assert.equal(r2.msgs('mix.set').length, 0, 'newSong drops the signature');
});
test('every accepted whole song replaces the baseline (it is what the core holds), also when small messages were sent before it', async () => {
  const r = await playing(); r.edit(S => { S.mix[0].vol = 10; }); await r.ck.advance(300); assert.equal(r.msgs('mix.set').length, 1); assert.equal(r.songLoads().length, 1);
  r.clear(); r.edit(S => { S.mix[0].vol = 224; }); await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set').map(m => m.v), [224], 'back to the very first value: still sent');
});
test('a change reverted while the song is out (the reply comes late) is still sent: the in-flight song hears what was sent after it', async () => {
  const r = rig({ hold: true }); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.releases[0](); await r.ck.advance(10);   // the first song accepted
  r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.clear();
  r.edit(S => { S.mix[0].vol = 100; }); await r.ck.advance(260); assert.equal(r.songLoads().length, 1, 'a second song (vol 100) is out, unanswered');
  r.edit(S => { S.mix[0].vol = 150; }); await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set').map(m => m.v), [100, 150]);
  r.edit(S => { S.mix[0].vol = 100; }); r.releases[1](); await flush();   // back to what the song says, and the song is accepted before the diff runs
  await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set').map(m => m.v), [100, 150, 100], 'the core holds 150: 100 must go');
});
test('a late `fit` event for a song already accepted does not put an older baseline back', async () => {
  const r = await playing(); r.edit(S => { S.mix[0].vol = 10; }); await r.ck.advance(300); const rev = r.songLoads()[0].rev;   // the song says 10
  r.edit(S => { S.mix[0].vol = 20; }); await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set').map(m => m.v), [10, 20]);
  r.emit('fit', { rev, ok: true, warn: [] }); await flush(); r.clear();
  r.edit(S => { S.mix[0].vol = 10; }); await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set').map(m => m.v), [10], 'the core holds 20: 10 must go (the old snapshot says 10)');
});
test('a song the shell refuses while playing: the old one plays on and the small messages still go, against the baseline of what the core holds', async () => {
  let n = 0; const r = rig({ verdictFor: m => (++n >= 2 ? { ok: false, rev: m.rev, reasons: ['too big'], warn: [] } : { ok: true, rev: m.rev, reasons: [], warn: [] }) });
  await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.clear();
  r.edit(S => { S.mix[0].vol = 10; }); await r.ck.advance(300); assert.equal(r.msgs('mix.set').length, 1);
  r.edit(S => { S.mix[0].vol = 224; }); await r.ck.advance(16); assert.deepEqual(r.msgs('mix.set').map(m => m.v), [10, 224]);
});
test('STOP and a new PLAY keep the baseline; a stop does not send anything; detach() clears every timer', async () => {
  const r = await playing(); r.edit(S => { S.mix[0].vol = 10; }); r.NC.stop(); await r.ck.advance(16); assert.equal(r.msgs('mix.set').length, 0, 'stopped before the diff ran: not sent (the song carries it)');
  await r.ck.advance(300); const q = await playing(); q.edit(S => { S.mix[1].vol = 10; }); q.NC.detach(); assert.equal(q.ck.count(), 0);
});
test('a host without the live hooks (fxSlot, chainMods, pdc): the mixer still works and nothing throws', async () => {
  const r = rig(); delete r.host.fxSlot; delete r.host.chainMods; delete r.host.pdc; await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.NC.start('song', { row: 0 }); await r.ck.advance(50); r.clear();
  r.edit(S => { S.mix[0].vol = 5; S.tfx[1][0] = { type: 'delay', on: true }; }); await r.ck.advance(16); assert.equal(r.msgs('mix.set').length, 1); assert.equal(r.msgs('play').length, 0);
});
test('the messages are the shapes the shell parses (BridgeTranslator): fx.set has exactly 12 numbers, mix.set keys in MIX_KEYS, master.config has mst, chain.edit has ops, pdc.set 0..2, fx.plan has slots and tr 0..32', async () => {
  const r = await withFx({ tfx: Array.from({ length: 32 }, (_, t) => t === 2 ? [{ type: 'delay', on: true, time: 0.25, fb: 0.4 }, null, null, null] : [null, null, null, null]) });
  r.edit(S => { S.tfx[2][0].fb = 0.9; S.mix[0].mute = true; S.mst.on = false; S.tfx[3][0] = { type: 'verb', on: true, time: 1, fb: 0 }; }); r.NC.chainParam('m1', 'cut', 0.2); r.pdc = 2; r.NC.edit(); await r.ck.advance(16);
  for (const m of r.msgs('fx.set')) { assert.equal(m.p.length, 12); assert.ok(m.p.every(Number.isFinite)); assert.ok(m.slot >= 0 && m.slot < 8); }
  for (const m of r.msgs('mix.set')) assert.ok(['vol', 'trim', 'pan', 'mute', 'solo', 'out'].includes(m.k));
  for (const m of r.msgs('fx.plan')) { assert.ok(Array.isArray(m.slots)); assert.ok(m.tr >= 0 && m.tr <= 32); assert.ok(m.bpm > 0); }
  assert.ok(r.msgs('master.config')[0].mst && typeof r.msgs('master.config')[0].mst === 'object'); assert.ok(Array.isArray(r.msgs('chain.edit')[0].ops)); assert.equal(r.msgs('pdc.set')[0].mode, 2);
  assert.ok(!r.sent.some(m => m.t === 'song.patch' || (m.t === 'mix.set' && m.k === 'send')));
});
