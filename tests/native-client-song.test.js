// PC3 of the native page client (docs/port/N2-PAGE-CLIENT.md 4.4, 4.6, 8; CORE-BRIDGE 7): the client's song and transport side against a FAKE thunderCore.
// No browser: thunder-core-client.js runs in a vm with a fake host (the page's side), a fake core and a fake clock.
// Covers: the whole-song publish (debounce, order of keys, never song.patch), PLAY order (kit, pads, song.load verdict, play), the fallback (first PLAY per song only),
// no verdict in 5 s = not ok, the pending verdict, a fatal err, engine down / up, device stopped, the heartbeat, can(), the legacy corePost ops.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const CLIENT = H.read('thunder-core-client.js');

// A fake clock for the client's timers (setTimeout / setInterval); `advance(ms)` runs what is due, in order. Promises run on the real microtask queue: `flush()` lets them settle.
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

// The rig: client + fake core + fake host. `reply(msg)` answers an ask (default: hello ok, song.load ok); `holdSong` makes song.load wait until `release()`.
function rig({ hostName = 'android', core = 'core', verdictFor = null, noReply = false } = {}) {
  const ck = clock(), sent = [], asked = [], handlers = {}, log = [], toasts = [], pb = [];
  const sb = { window: null, Promise, Error, JSON, Object, Array, isFinite, Date, setTimeout: ck.setTimeout, clearTimeout: ck.clearTimeout, setInterval: ck.setInterval, clearInterval: ck.clearInterval };
  sb.window = sb;
  const releases = [];
  const tc = {
    host: hostName, send: m => { sent.push(JSON.parse(JSON.stringify(m))); log.push(m.t); },
    ask: (m, ms) => { asked.push(JSON.parse(JSON.stringify(m))); log.push(m.t + '?'); const k = m.t;
      if (k === 'hello') return Promise.resolve({ t: 'reply', v: 1, core: 'r1', caps: { nativeAudio: true }, sr: 48000, outLat: 480, host: hostName });
      if (k === 'song.load') { if (noReply) return new Promise(() => {}); const v = verdictFor ? verdictFor(m, asked.length) : { ok: true, rev: m.rev, reasons: [], warn: [] };
        if (v && v.__hold) return new Promise(res => releases.push(() => res(v.reply))); if (v instanceof Error) return Promise.reject(v); return Promise.resolve(Object.assign({ t: 'reply', rev: m.rev }, v)); }
      return Promise.resolve({}); },
    on: (t, fn) => { (handlers[t] = handlers[t] || []).push(fn); }, off: (t, fn) => { handlers[t] = (handlers[t] || []).filter(f => f !== fn); },
  };
  sb.thunderCore = tc; vm.createContext(sb); vm.runInContext(CLIENT, sb, { filename: 'thunder-core-client.js' });
  let S = { name: 'Night', song: [] }, ver = 1, lxp = 0, blocked = '';
  const tp = { on: false, nc: false, seq: null }, state = { fallbacks: [], stopped: 0, begun: [], prep: 0, resend: 0, prerender: 0 };
  const host = {
    S: () => S, songVer: () => ver, aset: { core }, toast: (t, ms) => toasts.push(t), tpLog: (...a) => log.push('tpLog ' + a.join(' ')), pbLog: s => pb.push(s), now: () => ck.now(),
    prerender: () => { state.prerender++; }, build: () => 678, vars: () => ({ vtrk: 1 }), title: () => 'Night drive', lxp: () => lxp, perf: () => null, blocked: () => blocked,
    prep: () => { state.prep++; log.push('prep'); },
    begin: (mode, o, seq) => { if (tp.nc) NC.stop(); tp.on = true; tp.nc = true; tp.seq = seq; seq._bpm = 120; state.begun.push([mode, o]); return o || {}; },
    fallback: (mode, o) => { if (tp.nc) { tp.nc = false; NC.stop(); } tp.on = false; state.fallbacks.push([mode, o]); },
    stopped: () => { state.stopped++; if (tp.on) { tp.on = false; if (tp.nc) { tp.nc = false; NC.stop(); } } },
  };
  const NC = sb.__thunderClient;
  const emit = (t, m) => (handlers[t] || []).forEach(f => f(Object.assign({ t }, m)));
  const beat = () => emit('clock', { f: 0, sr: 48000 });
  return { sb, NC, tc, host, ck, sent, asked, log, toasts, pb, tp, state, emit, beat, releases,
    set S(v) { S = v; }, get S() { return S; }, bump() { ver++; }, set lxp(v) { lxp = v; }, set blocked(v) { blocked = v; },
    msgs: t => sent.filter(m => m.t === t), songLoads: () => asked.filter(m => m.t === 'song.load') };
}
async function up(opts) { const r = rig(opts); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); return r; }   // attach, one heartbeat, the first publish (250 ms) done

// ---- the song publish -------------------------------------------------------------------------------------------------------------------
test('publish: a whole song 250 ms after the last edit, keys in the order the shell reads them, never song.patch; the same version sends nothing', async () => {
  const r = await up();
  assert.equal(r.songLoads().length, 1, 'the first publish after hello');
  const m = r.songLoads()[0];
  assert.deepEqual(Object.keys(m).slice(0, 5), ['t', 'title', 'S', 'vars', 'rev'], 'the shell reads t and title from the head, rev and id from the tail');
  assert.equal(m.title, 'Night drive'); assert.equal(m.rev, 1); assert.deepEqual(m.S, { name: 'Night', song: [] }); assert.deepEqual(m.vars, { vtrk: 1 });
  r.bump(); r.NC.edit(); await r.ck.advance(100); r.bump(); r.NC.edit(); await r.ck.advance(200);
  assert.equal(r.songLoads().length, 1, '300 ms after the first edit but only 200 after the second: still waiting');
  await r.ck.advance(100); assert.equal(r.songLoads().length, 2); assert.equal(r.songLoads()[1].rev, 2);
  r.NC.edit(); await r.ck.advance(1000); assert.equal(r.songLoads().length, 2, 'nothing changed (same version): nothing sent');
  assert.ok(![...r.sent, ...r.asked].some(x => x.t === 'song.patch'), 'song.patch is never sent');
});
test('publish: a slow hand-over (4 ms or more) makes the next publish wait for a second of quiet', async () => {
  const r = rig(); await r.NC.attach(r.host); r.beat();
  const ask = r.tc.ask; r.tc.ask = (m, ms) => { const p = ask(m, ms); if (m.t === 'song.load') r.ck.tick(6); return p; };
  await r.ck.advance(300); assert.equal(r.songLoads().length, 1); assert.ok(r.NC.state().sendCost >= 4);
  r.bump(); r.NC.edit(); await r.ck.advance(500); assert.equal(r.songLoads().length, 1); await r.ck.advance(600); assert.equal(r.songLoads().length, 2);
});
test('publish: one song.load at a time; an edit while one is out goes after its answer', async () => {
  let n = 0; const r = rig({ verdictFor: () => (++n === 1 ? { __hold: true, reply: { ok: true, rev: 1, reasons: [], warn: [] } } : { ok: true, reasons: [], warn: [] }) });
  await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); assert.equal(r.songLoads().length, 1);
  r.bump(); r.NC.edit(); await r.ck.advance(300); assert.equal(r.songLoads().length, 1, 'the first answer is still out');
  r.releases[0](); await r.ck.advance(10); assert.equal(r.songLoads().length, 2, 'then the newest');
});

// ---- PLAY ---------------------------------------------------------------------------------------------------------------------------------
test('PLAY order: kit, pads, then song.load and its verdict, and only then play; the run is lit at once', async () => {
  const r = await up(); r.bump();
  r.NC.kit = () => r.log.push('kit.load'); r.NC.settle = ms => { r.log.push('pads?' + ms); return Promise.resolve(); };
  r.log.length = 0; assert.equal(r.NC.can('song', { row: 3 }), true);
  r.NC.start('song', { row: 3, loop: { v: 'song', y0: 3, y1: 5 }, only: [1, 2] });
  assert.equal(r.tp.on && r.tp.nc, true, 'tp says playing before the verdict');
  await r.ck.advance(1);
  const order = r.log.filter(x => ['kit.load', 'pads?1500', 'song.load?', 'play'].includes(x));
  assert.deepEqual(order, ['kit.load', 'pads?1500', 'song.load?', 'play']);
  assert.ok(r.log.indexOf('prep') >= 0 && r.log.indexOf('prep') < r.log.indexOf('play'));
  const p = r.msgs('play')[0]; assert.equal(p.mode, 'song'); assert.equal(p.row, 3); assert.deepEqual(p.loop, { y0: 3, y1: 5 }); assert.deepEqual(p.only, [1, 2]); assert.ok(Number.isInteger(p.seed));
  assert.ok(r.msgs('lead').length >= 1, 'the look-ahead goes with PLAY');
  assert.equal(r.NC.state().playing, true);
});
test('PLAY waits for the verdict: nothing is played before the answer', async () => {
  let first = true; const r = rig({ verdictFor: () => { if (first) { first = false; return { ok: true, reasons: [], warn: [] }; } return { __hold: true, reply: { ok: true, rev: 2, reasons: [], warn: [] } }; } });
  await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.bump();
  r.NC.can('song', { row: 0 }); r.NC.start('song', { row: 0 }); await r.ck.advance(50);
  assert.equal(r.msgs('play').length, 0); r.releases[0](); await r.ck.advance(5); assert.equal(r.msgs('play').length, 1);
});
test('PLAY section and phrase modes carry sect / phrase / track; a song starts at its row', async () => {
  const r = await up(); r.NC.start('sect', { sect: 2, track: 4 }); await r.ck.advance(5);
  const p = r.msgs('play')[0]; assert.equal(p.mode, 'sect'); assert.equal(p.sect, 2); assert.equal(p.track, 4); assert.equal(p.row, 0);
});
test('STOP: sends stop once, only when play went out; a STOP before the verdict cancels the PLAY', async () => {
  const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5); r.NC.stop(); r.NC.stop();
  assert.equal(r.msgs('stop').length, 1);
  r.bump(); r.releases.length = 0; r.sb.thunderCore.ask; // a held verdict next
  const r2 = rig({ verdictFor: (m, n) => (n > 1 ? { __hold: true, reply: { ok: true, rev: 2, reasons: [], warn: [] } } : { ok: true, reasons: [], warn: [] }) });
  await r2.NC.attach(r2.host); r2.beat(); await r2.ck.advance(300); r2.bump(); r2.NC.start('song', { row: 0 }); await r2.ck.advance(20); r2.NC.stop(); r2.releases[0](); await r2.ck.advance(20);
  assert.equal(r2.msgs('play').length, 0, 'cancelled'); assert.equal(r2.msgs('stop').length, 0, 'nothing was playing');
});
test('PLAY again while playing: the run before is stopped by the page (begin) and the new one is not cancelled by it', async () => {
  const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5); r.NC.start('song', { row: 4 }); await r.ck.advance(5);
  assert.deepEqual(r.msgs('play').map(m => m.row), [0, 4]); assert.equal(r.msgs('stop').length, 1);
});
test('hold, bpm, loop, perf: the corePost ops of the JS core become bridge messages, only while a native PLAY runs; a tempo set while waiting is sent after play', async () => {
  const r = await up(); r.NC.legacy({ op: 'bpm', v: 140 }); assert.equal(r.msgs('bpm').length, 0, 'not playing: nothing');
  r.NC.start('song', { row: 0 }); r.NC.legacy({ op: 'bpm', v: 133 }); await r.ck.advance(5);
  assert.deepEqual(r.msgs('bpm').map(m => m.v), [133], 'the tempo set while the verdict was out goes right after play');
  r.tp.seq.bpm = 150; assert.deepEqual(r.msgs('bpm').map(m => m.v), [133, 150], 'the stand-in sequencer sets it');
  r.tp.seq.setLoop({ v: 'song', y0: 2, y1: 6 }); r.tp.seq.setLoop(null);
  assert.deepEqual(r.msgs('loop'), [{ t: 'loop', has: true, y0: 2, y1: 6 }, { t: 'loop', has: false, y0: 0, y1: 0 }]);
  r.NC.legacy({ op: 'perf', xf: 90, mac: [10, null, 30], fill: true });
  assert.deepEqual(r.msgs('perf'), [{ t: 'perf', what: 0, i: 0, v: 90 }, { t: 'perf', what: 1, i: 0, v: 10 }, { t: 'perf', what: 1, i: 2, v: 30 }, { t: 'perf', what: 2, i: 0, v: 1 }]);
  r.NC.legacy({ op: 'perf', fill: false }); assert.deepEqual(r.msgs('perf').pop(), { t: 'perf', what: 2, i: 0, v: 0 });
  r.NC.legacy({ op: 'hold' }); assert.equal(r.msgs('hold').length, 1);
  r.NC.legacy({ op: 'load', S: {} }); r.NC.legacy({ op: 'patch', ops: [] }); r.NC.legacy({ op: 'kit' }); assert.ok(!r.log.some(x => ['load', 'patch', 'kit'].includes(x)), 'the JS core ops that are not live commands are not sent');
  r.NC.legacy({ op: 'lxp', st: 3 }); r.NC.legacy({ op: 'lxp', st: 3 }); assert.deepEqual(r.msgs('lxp'), [{ t: 'lxp', st: 3 }]);
});
test('the first PERFORM state goes with PLAY', async () => {
  const r = await up(); r.host.perf = () => ({ xf: 12, mac: [null, 5], fill: false }); r.NC.start('song', { row: 0 }); await r.ck.advance(5);
  assert.deepEqual(r.msgs('perf').map(m => m.what + ':' + m.i + ':' + m.v), ['0:0:12', '1:1:5', '2:0:0']);
});

// ---- the fallback -------------------------------------------------------------------------------------------------------------------------
test('a verdict that is not ok: the old engine plays, nothing is sent to play, one message for the first PLAY of the song only', async () => {
  const r = await up({ verdictFor: () => ({ ok: false, reasons: ['3 pads use stretch'], warn: [] }) });
  assert.equal(r.NC.can('song', { row: 0 }), false, 'the verdict from the first publish is already in: decided before anything starts');
  assert.deepEqual(r.toasts, ['Playing with the old engine: 3 pads use stretch']);
  assert.equal(r.NC.can('song', { row: 0 }), false); assert.equal(r.toasts.length, 1, 'the second PLAY of the same song: no second message');
  assert.equal(r.pb.filter(x => /old engine plays/.test(x)).length, 2, 'the playback report keeps every one');
  assert.equal(r.msgs('play').length, 0);
  r.S = { name: 'Other', song: [] }; r.bump(); await r.NC.start; // a new song: a new verdict, a new message
  r.NC.edit(); await r.ck.advance(300); r.NC.can('song', { row: 0 }); assert.equal(r.toasts.length, 2, 'another song, its first PLAY');
});
test('an unknown verdict (edited since the last answer): PLAY asks, and a not-ok answer hands the original PLAY back to the old engine', async () => {
  let n = 0; const r = rig({ verdictFor: () => (++n === 1 ? { ok: true, reasons: [], warn: [] } : { ok: false, reasons: ['resample'], warn: [] }) });
  await r.NC.attach(r.host); r.beat(); await r.ck.advance(300); r.bump();
  const o = { row: 2 }; assert.equal(r.NC.can('song', o), true); r.NC.start('song', o); await r.ck.advance(5);
  assert.equal(r.msgs('play').length, 0); assert.equal(r.state.fallbacks.length, 1); assert.equal(r.state.fallbacks[0][0], 'song'); assert.equal(r.state.fallbacks[0][1], o);
  assert.deepEqual(r.toasts, ['Playing with the old engine: resample']); assert.equal(r.tp.on, false);
});
test('no verdict within 5 s is NOT ok (reason no-verdict): the old engine plays, never the new one (CORE-BRIDGE 7 G14, the lead\'s ruling of 25 Nov)', async () => {
  const r = rig({ noReply: true }); await r.NC.attach(r.host); r.beat(); await r.ck.advance(300);
  r.NC.can('song', { row: 0 }); r.NC.start('song', { row: 0 }); await run(r, 4000); assert.equal(r.msgs('play').length, 0, 'still waiting at 4 s');
  await run(r, 2500);
  assert.equal(r.msgs('play').length, 0); assert.equal(r.state.fallbacks.length, 1);
  assert.deepEqual(r.toasts, ['Playing with the old engine: the native engine did not answer']);
  const st = r.NC.state().verdict; assert.equal(JSON.stringify(st.reasons), '["no-verdict"]'); assert.equal(st.ok, false);
  // the bridge's own timeout rejecting the ask is the same thing
  const q = rig({ verdictFor: () => new Error('ask timeout') }); await q.NC.attach(q.host); q.beat(); await q.ck.advance(300);
  assert.equal(q.NC.can('song', { row: 0 }), false); assert.match(q.toasts[0], /did not answer/);
});
test('pending (no kit yet): not a verdict; the shell\'s fit event after kit.load decides', async () => {
  let n = 0; const r = rig({ verdictFor: () => (++n === 1 ? { ok: false, pending: true, reasons: ['no-kit'], warn: [] } : { ok: true, reasons: [], warn: [] }) });
  await r.NC.attach(r.host); r.beat(); await r.ck.advance(300);
  assert.equal(r.NC.can('song', { row: 0 }), true, 'pending is not a refusal'); assert.equal(r.NC.state().verdict.pending, true);
  r.NC.start('song', { row: 0 }); await r.ck.advance(100); assert.equal(r.msgs('play').length, 0, 'waits for the fit event');
  r.emit('fit', { rev: 1, ok: true, reasons: [], reasonTexts: [], warn: ['2 pads without their pad effects'] }); await r.ck.advance(5);
  assert.equal(r.msgs('play').length, 1); assert.deepEqual(r.toasts, ['Native engine: 2 pads without their pad effects'], 'warnings are said once');
  // a fit event for another song revision is not ours
  const r2 = rig({ verdictFor: () => ({ ok: false, pending: true, reasons: ['no-kit'], warn: [] }) }); await r2.NC.attach(r2.host); r2.beat(); await r2.ck.advance(300);
  r2.NC.start('song', { row: 0 }); await r2.ck.advance(100); r2.emit('fit', { rev: 99, ok: true }); await r2.ck.advance(5); assert.equal(r2.msgs('play').length, 0);
  await run(r2, 5500); assert.equal(r2.msgs('play').length, 0); assert.equal(r2.state.fallbacks.length, 1, 'pending for 5 s is no verdict');
});
test('a fit event changes the verdict (kit.load, lxp): a song that fitted may stop fitting', async () => {
  const r = await up(); r.emit('fit', { rev: 1, ok: false, reasons: [{ code: 'lxp' }], reasonTexts: ['live transpose (LXP)'], warn: [] });
  assert.equal(r.NC.can('song', { row: 0 }), false); assert.deepEqual(r.toasts, ['Playing with the old engine: live transpose (LXP)']);
});
test('can(): the page asked for its own engine (page, hand-over, the fallback itself), Settings on Page (old), a host that is not Android without the pick: false and no message', async () => {
  const r = await up();
  for (const o of [{ page: true }, { _nf: true }, { rz: { row: 0 } }]) assert.equal(r.NC.can('song', o), false);
  r.host.aset.core = 'page'; assert.equal(r.NC.can('song', {}), false); r.host.aset.core = 'core'; assert.equal(r.NC.can('song', {}), true);
  assert.equal(r.toasts.length, 0);
  const w = await up({ hostName: 'web' }); assert.equal(w.NC.can('song', {}), false, 'the web copy only on the owner\'s pick'); w.host.aset.core = 'native'; assert.equal(w.NC.can('song', {}), true);
});
test('can(): what only the page knows (recording take, count-in, Link, MIDI clock follow) sends the PLAY to the old engine with its reason', async () => {
  const r = await up(); r.blocked = 'Ableton Link needs the old engine'; assert.equal(r.NC.can('song', {}), false); assert.deepEqual(r.toasts, ['Playing with the old engine: Ableton Link needs the old engine']);
  r.blocked = ''; assert.equal(r.NC.can('song', {}), true);
});
test('can(): no heartbeat (a clock event within 2 s) means the engine is not running', async () => {
  const r = await up(); await r.ck.advance(2500); assert.equal(r.NC.can('song', {}), false); assert.match(r.toasts[0], /not running/);
  const n = rig(); await n.NC.attach(n.host); assert.equal(n.NC.can('song', {}), false, 'no clock yet');
});

// ---- during play: errors, engine down / up, device ------------------------------------------------------------------------------------------
test('a fatal err during play stops the transport, says so, and the next PLAY of that song uses the old engine; a non-fatal err only goes to the report', async () => {
  const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5);
  r.emit('err', { where: 'REANCHOR', msg: 'x', fatal: false }); assert.equal(r.state.stopped, 0); assert.ok(r.pb.some(x => /REANCHOR/.test(x)));
  r.emit('err', { where: 'FATAL', msg: 'dead', fatal: true }); await r.ck.advance(5);
  assert.equal(r.state.stopped, 1); assert.equal(r.tp.on, false); assert.match(r.toasts.pop(), /^The native engine stopped: FATAL\. Press PLAY/);
  assert.equal(r.msgs('stop').length, 0, 'no stop to a dead engine');
  r.emit('engine', { up: true, sr: 48000 }); await r.ck.advance(300);
  assert.equal(r.NC.can('song', {}), true, 'a rebuilt engine is tried again');
  r.emit('err', { where: 'x', fatal: true }); r.NC.start('song', {}); r.NC.stop(); r.emit('engine', { up: true }); // (a fatal err with nothing playing marks the engine down, not the song)
});
test('the next PLAY of the song that killed the engine uses the old engine', async () => {
  const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5); r.emit('err', { where: 'FATAL', fatal: true }); await r.ck.advance(5);
  r.beat(); assert.equal(r.NC.can('song', {}), false, 'the engine is down');
  r.emit('engine', { up: true, sr: 48000 }); await r.ck.advance(300); r.beat();
  r.NC.start('song', { row: 0 }); await r.ck.advance(5); r.emit('err', { where: 'FATAL', fatal: true }); await r.ck.advance(5);
  assert.equal(r.NC.state().failed, true); assert.equal(r.NC.can('song', {}), false);
});
test('engine down while playing stops the transport; up sends the song again (and asks the pads hook to resend); nothing is sent while it is down', async () => {
  const r = await up(); r.NC.resend = () => { r.state.resend++; };
  r.NC.start('song', { row: 0 }); await r.ck.advance(5); const before = r.songLoads().length;
  r.emit('engine', { up: false }); await r.ck.advance(5);
  assert.equal(r.state.stopped, 1); assert.match(r.toasts.pop(), /restarting/); assert.equal(r.NC.can('song', {}), false);
  r.bump(); r.NC.edit(); await r.ck.advance(600); assert.equal(r.songLoads().length, before, 'down: no song.load');
  r.emit('engine', { up: true, sr: 44100 }); await r.ck.advance(5);
  assert.equal(r.songLoads().length, before + 1, 'the song again'); assert.equal(r.state.resend, 1); assert.equal(r.NC.sr, 44100); assert.equal(r.state.prerender, 2, 'pads render again at the new rate (hello + up)');
  r.beat(); assert.equal(r.NC.can('song', {}), true);
});
test('engine.reset: Settings > Reset audio engine sends it', async () => {
  const r = await up(); r.NC.reset(); assert.equal(r.msgs('engine.reset').length, 1);
});
test('device stopped (headphones, focus, stream): the transport shows stopped, one message, no stop sent back', async () => {
  for (const [reason, re] of [['noisy', /headphones/], ['focus', /another app/], ['stream', /output changed/]]) {
    const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5);
    r.emit('device', { reason, stopped: true }); await r.ck.advance(5);
    assert.equal(r.state.stopped, 1); assert.match(r.toasts.pop(), re); assert.equal(r.msgs('stop').length, 0); assert.equal(r.tp.on, false);
  }
  const q = await up(); q.emit('device', { reason: 'focus', stopped: true }); assert.equal(q.state.stopped, 0, 'nothing playing: nothing to show');
});
test('the song running out (state stopped, arg 1) ends the transport; a stop we asked for (arg 0) does not', async () => {
  const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5);
  r.emit('state', { state: 'stopped', arg: 0 }); assert.equal(r.state.stopped, 0);
  r.emit('state', { state: 'ended', arg: 0 }); assert.equal(r.state.stopped, 0);
  r.emit('state', { state: 'stopped', arg: 1 }); assert.equal(r.state.stopped, 1);
});
test('a silent engine while playing (no clock for 2.5 s) stops the transport and tells', async () => {
  const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5);
  for (let i = 0; i < 4; i++) { r.beat(); await r.ck.advance(500); } assert.equal(r.state.stopped, 0, 'the heartbeat keeps it alive');
  await r.ck.advance(3200); assert.equal(r.state.stopped, 1); assert.match(r.toasts.pop(), /stopped answering/);
});
test('live transpose (lxp) goes to the shell before the song, and a change after the last publish makes the song stale', async () => {
  const r = await up(); r.lxp = 5; r.NC.can('song', {}); r.NC.start('song', { row: 0 }); await r.ck.advance(5);
  const i = r.log.lastIndexOf('lxp'), j = r.log.lastIndexOf('song.load?'); assert.ok(i >= 0 && i < j, 'lxp before song.load'); assert.deepEqual(r.msgs('lxp').pop(), { t: 'lxp', st: 5 }); assert.equal(r.songLoads().length, 2);
});
test('the resume note comes once per session', async () => {
  const r = await up(); r.host.begin = (m, o, seq) => { r.tp.on = true; r.tp.nc = true; r.tp.seq = seq; return Object.assign({ _resumed: true }, o); };
  r.NC.start('song', { row: 1 }); await r.ck.advance(5); r.NC.stop(); r.NC.start('song', { row: 1 }); await r.ck.advance(5);
  assert.equal(r.toasts.filter(t => /Resume/.test(t)).length, 1);
});
test('detach() clears the timers', async () => {
  const r = await up(); r.NC.start('song', { row: 0 }); await r.ck.advance(5); r.bump(); r.NC.edit(); r.NC.detach(); assert.equal(r.ck.count(), 0); assert.equal(r.NC.on, false);
});
test('the page hands the client what it needs: the host object of index.html has every function the client calls', () => {
  const src = H.read('index.html'), host = (src.match(/const ncSongHost = \(\) => \(\{[\s\S]*?\n\}\);\n/) || [])[0] || '';
  for (const k of ['songVer', 'vars', 'title', 'lxp', 'pbLog', 'perf', 'blocked', 'prep', 'begin', 'fallback', 'stopped']) assert.match(host, new RegExp('\\b' + k + ':'), k);
  for (const k of ['S', 'songVer', 'aset', 'toast', 'now', 'vars', 'title', 'lxp', 'perf', 'blocked', 'prep', 'begin', 'fallback', 'stopped', 'pbLog']) assert.ok(CLIENT.includes('host.' + k), 'the client reads host.' + k + ' (' + k + ')');
});
