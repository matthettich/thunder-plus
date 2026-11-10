// MOD: the master volume as a target (11 Nov): vol:MT, the master OUT fader (S.mout; after the limiter), never MASTER (S.master, the drive).
// Listed for the master only, sent by makeSeq as 'mout' events (the master has no pattern: LFOs always run), applied by the page player (setTargetAtTime,
// the 4 ms glide the track level uses) and by the audio-thread core (an offset on its output NT + 4, into the OUT gain), with the OUT fader's own curve.
// The song's S.mout is never written; STOP puts the page's gain back. No browser: pieces of index.html are cut out and run in a vm (as mod-pump.test.js does),
// makeSeq and the core's mixer are read from core/seq-core.js; the core before this change (BASE) pins that a song with no LFO is unchanged.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const { execFileSync } = require('node:child_process');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what){ const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
const NT = 32, MT = 32, BASE = 'b3594f2';   // BASE: the commit before this change (its core is the "old" sequencer)
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const curve = v => Math.pow(Math.max(0, Math.min(255, v)) / 255, 2);

// the page's targets (PERFORM block) in a vm
const PAGE = vm.createContext({ Math, JSON, Object, Array, Map, Set, String, Number, isNaN, NT, MT, TFX: {}, FX_GROUP: {}, FX_CREDIT: {}, PUMP_META: {}, clamp,
  int: (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d, d2: n => String(n).padStart(2, '0'), isBusT: () => true, isPumpT: () => false, globalThis: null });
PAGE.globalThis = PAGE;
vm.runInContext(cut(/\/\* ===== PERFORM \(owner, 1 Nov[\s\S]*?\n(?=function makeSeq\(S, mode, o\)\{)/, 'the PERFORM block'), PAGE);
const run = code => vm.runInContext(code, PAGE);

function mkSong({ cells, lfo = {}, mout = 200, master = 255 }){
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 4, plen: [4], tune: new Array(12).fill(0), insts: [{}], parts: [], master, mout,
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [[[0, 0]]],
    phrases: [Array.from({ length: 4 }, (_, y) => y % 2 ? [-1, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0] : [48 + y, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0])],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })),
    tfx: Array.from({ length: NT }, () => []), mfx: [], smute: new Array(256).fill(0), perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo } };
  for (const t in cells) for (const y of cells[t]) S.song[y][+t] = 0;
  return S;
}
function loadCore(text){ const ctx = H.workletScope(); vm.runInContext(text, ctx, { filename: 'seq-core.js' }); vm.runInContext('TFX = {};', ctx); return ctx; }
const NEW = loadCore(H.read('core/seq-core.js'));
function play(ctx, S, o = {}, steps = 64){
  ctx.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(ctx.__S, 'song', Object.assign({ row: 0, once: true, sim: true }, o)), evs = [];
  for (let i = 0; i < steps && sq.alive(); i++) evs.push(...sq.step().evs);
  return JSON.parse(JSON.stringify(evs));
}
const RATES = [1 / 8, 1 / 4, 1 / 2, 1, 2, 4, 8, 16, 32];
const sineAt = (t, c, rt, dp) => { const bt = t * 2 / RATES[rt]; return Math.max(0, Math.min(255, Math.round(c + Math.sin((bt - Math.floor(bt)) * 2 * Math.PI) * dp / 2))); };
const of = (evs, type) => evs.filter(e => e.type === type);

test('tgtList: vol:MT is listed for the master only, as "VOL · master out"; it parses, resolves and is the OUT fader\'s own byte', () => {
  PAGE.S = mkSong({ cells: { 1: [0] }, mout: 77 }); PAGE.S.tfx = Array.from({ length: NT }, () => []); PAGE.S.mfx = [];
  const id = 'vol:' + MT, M = run('tgtList(MT)');
  assert.deepEqual(JSON.parse(JSON.stringify(M.filter(([i]) => i === id))), [[id, 'VOL · master out']]);
  for (let t = 0; t < NT; t++) assert.ok(!run(`tgtList(${t})`).some(([i]) => i === id), 'track ' + t + ' does not list the master');
  assert.ok(run(`tgtOk('${id}')`)); assert.deepEqual(JSON.parse(JSON.stringify(run(`tgtParse('${id}')`))), { k: 'vol', t: MT, s: -1, p: -1 });
  assert.equal(run(`tgtLabel('${id}')`), 'MASTER · VOL (master out)'); assert.equal(run(`tgtLabel('${id}', true)`), 'VOL (master out)');
  assert.equal(run(`tgtOwn('${id}')`), 77, 'the fader\'s own byte (S.mout)');
  delete PAGE.S.mout; assert.equal(run(`tgtOwn('${id}')`), 255, 'a song with no mout: FF');
  assert.equal(run("tgtOwn('vol:3')"), 128, 'a track\'s own is still its mixer fader');
});

test('tgtEvs: the master target is one "mout" event (OUT), the song\'s own byte for null; a track\'s is still tva; nothing writes the song', () => {
  const S = PAGE.S = mkSong({ cells: { 1: [0] }, mout: 90 }), before = JSON.stringify(S);
  assert.deepEqual(JSON.parse(JSON.stringify(run(`tgtEvs('vol:${MT}', 33, 1.5, new Map())`))), [{ type: 'mout', t: 1.5, track: MT, v: 33 }]);
  assert.deepEqual(JSON.parse(JSON.stringify(run(`tgtEvs('vol:${MT}', null, 2, new Map())`))), [{ type: 'mout', t: 2, track: MT, v: 90 }], 'back to the song\'s own');
  assert.equal(run("tgtEvs('vol:3', 40, 1, new Map())")[0].type, 'tva');
  assert.equal(JSON.stringify(S), before, 'S.mout and everything else untouched');
});

test('makeSeq: an LFO on vol:MT sends "mout" events on every step (the master has no pattern), around S.mout, never mvol', () => {
  const lfo = { [MT]: [{ id: 'vol:' + MT, sh: 0, rt: 5, dp: 100 }] }, S = mkSong({ cells: { 1: [0, 1, 2, 3] }, lfo, mout: 150, master: 200 });
  const sim = play(NEW, S), ev = of(sim, 'mout');
  assert.equal(ev.length, 16 * 4, '4 points a step in a render too (since 11 Nov), 16 steps');
  assert.equal(of(sim, 'mvol').length, 0, 'MASTER (the drive) is not touched');
  ev.forEach(e => { assert.equal(e.track, MT); assert.equal(e.v, sineAt(e.t, 150, 5, 100), 'follows the sine around the OUT fader\'s byte at t=' + e.t); });
  const vs = ev.map(e => e.v); assert.ok(Math.max(...vs) >= 150 + 49 && Math.min(...vs) <= 150 - 49);
  const live = play(NEW, S, { live: true, sim: false }); assert.equal(of(live, 'mout').length, 16 * 4, '4 a step live');
  assert.equal(S.mout, 150);
  // an LFO that is not on the master never sends mout; no LFO at all: none
  assert.equal(of(play(NEW, mkSong({ cells: { 1: [0, 1] }, lfo: { 1: [{ id: 'vol:1', sh: 0, rt: 5, dp: 100 }] } })), 'mout').length, 0);
  assert.equal(of(play(NEW, mkSong({ cells: { 1: [0, 1] } })), 'mout').length, 0);
});

test('the page player: "mout" glides the OUT gain to curve(v) over 4 ms (the track level\'s smoothing); S.mout is not written; STOP puts the fader\'s own back', () => {
  const line = cut(/    if \(ev\.type === 'mout'\)\{[^\n]*\n/, 'the mout handler'), rest = cut(/    if \(moOv\)\{[^\n]*\n/, 'the STOP restore');
  const calls = [], S = { mout: 120 }, mo = { gain: { setTargetAtTime: (...a) => calls.push(['target', ...a]), cancelScheduledValues: (...a) => calls.push(['cancel', ...a]), setValueAtTime: (...a) => calls.push(['set', ...a]) } };
  const ctx = vm.createContext({ Math, curve, S, mo, ac: { currentTime: 1 }, moOv: false });
  vm.runInContext(`function play(ev){\n${line}\n return 'other'; }\nfunction stop(at){\n${rest}\n}`, ctx);
  assert.equal(ctx.play({ type: 'mout', t: 2, v: 128 }), undefined, 'handled');
  assert.deepEqual(calls[0], ['target', curve(128), 2, 0.004], 'the OUT fader\'s curve, at its time, 4 ms');
  ctx.play({ type: 'mout', t: 0.5, v: 255 }); assert.equal(calls[1][2], 1, 'a time already past: now');
  assert.equal(ctx.play({ type: 'tva', t: 0, v: 1 }), 'other', 'other events fall through');
  assert.equal(S.mout, 120, 'the song\'s own is never written');
  const n = calls.length; vm.runInContext('stop(5)', ctx);
  assert.deepEqual(calls.slice(n), [['cancel', 5], ['set', curve(120), 5]], 'STOP: the fader\'s own value');
  const m = calls.length; vm.runInContext('stop(6)', ctx); assert.equal(calls.length, m, 'nothing moved since: nothing to restore');
  // with no OUT node (no live graph) the event is ignored
  const c2 = vm.createContext({ Math, curve, S, mo: null, ac: { currentTime: 1 }, moOv: false }); vm.runInContext(`function play(ev){\n${line}\n return 'other'; }`, c2); assert.equal(c2.play({ type: 'mout', t: 2, v: 1 }), undefined);
  assert.equal(c2.moOv, false);
});

test('the page: the player gets the OUT gain (live and in renders), the core\'s output NT + 4 is plugged into it, STOP restores it', () => {
  assert.match(src, /function makePlayer\(ac, out, getBuf, live, mo\)\{/);
  assert.match(src, /pl:makePlayer\(ctx, out, rtBuf, true, mo\)/); assert.match(src, /const pl = makePlayer\(oc, out, getB, false, mo\);/);
  assert.match(src, /numberOfOutputs:NT \+ 5, outputChannelCount:\[\.\.\.new Array\(NT\)\.fill\(2\), 1, NT, NT, 1, 1\]/);
  assert.match(src, /if \(e\.type === 'mout'\)\{[^\n]*n\.connect\(rt\.mo\.gain, NT \+ 4\)/);
  assert.match(src, /e\.type === 'mvol' \|\| e\.type === 'mout'\) coreMixT\(e\)/);
});

test('the core: "mout" is taken by the core\'s mixer, as an offset on output NT + 4 (curve(v) − curve(S.mout)), with the track level\'s 4 ms glide; mvol is still output NT + 3', () => {
  const C = loadCore(H.read('core/seq-core.js')); C.__S = mkSong({ cells: {}, mout: 120, master: 255 }); vm.runInContext('S = __S;', C);
  const core = vm.runInContext('Object.create(ThunderCore.prototype)', C); core.mixReset();
  const h = Object.getOwnPropertyNames(Object.getPrototypeOf(core)).find(n => n !== 'constructor' && typeof core[n] === 'function' && String(core[n]).includes('this.mixEv(ev)')); assert.ok(h, 'found the core\'s event method');
  C.currentTime = 1;
  assert.equal(core[h]({ type: 'mout', t: 1, track: MT, v: 200 }), true, 'the core takes it (the page then leaves the OUT gain alone)');
  assert.equal(core[h]({ type: 'mvol', t: 1, v: 100 }), true);
  const out = () => { const o = []; o[NT + 3] = [new Float32Array(128)]; o[NT + 4] = [new Float32Array(128)]; return o; };
  C.currentTime = 1; let o = out(); core.mixOut(o, 128, 1 / 48000); assert.ok(Math.abs(o[NT + 4][0][0]) < 1e-3, 'it starts at the fader\'s own (offset 0), then glides');
  C.currentTime = 3; o = out(); core.mixOut(o, 128, 1 / 48000);
  assert.ok(Math.abs(o[NT + 4][0][127] - (curve(200) - curve(120))) < 1e-6, 'settled on the offset: gain = OUT + offset = curve(200)');
  assert.ok(Math.abs(o[NT + 3][0][127] - (curve(100) - curve(255))) < 1e-6, 'the master drive\'s own output is separate');
  assert.equal(C.__S.mout, 120, 'S.mout untouched');
  core.mixReset(); C.currentTime = 4; o = out(); core.mixOut(o, 128, 1 / 48000); assert.equal(o[NT + 4][0][127], 0, 'STOP / PLAY: the offset is gone');
  core.mixOut([], 128, 1 / 48000);   // an older page with no such output: nothing to fill, no error
});

test('a song with no LFO (and one with LFOs elsewhere): the core before this change and the new one send the same events', t => {
  let old; try { old = loadCore(execFileSync('git', ['show', BASE + ':core/seq-core.js'], { cwd: H.ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] })); }
  catch (e){ t.skip('git history to ' + BASE + ' is not here'); return; }
  let n = 0, total = 0;
  for (const sh of [{ cells: { 0: [0, 1, 2, 3], 1: [0, 1] } }, { cells: { 1: [0, 1, 2, 3], 5: [0] }, lfo: { 1: [{ id: 'vol:1', sh: 0, rt: 5, dp: 100 }, { id: 'fx:32:0:0', sh: 2, rt: 3, dp: 60 }] } }])
    for (const o of [{}, { live: true, sim: false }, { only: new Set([1]) }]){
      const a = play(old, mkSong(sh), o, 80), b = play(NEW, mkSong(sh), o, 80); total += a.length; assert.deepEqual(b, a, JSON.stringify(o)); n++; }
  assert.equal(n, 6); assert.ok(total > 300 && !JSON.stringify(play(NEW, mkSong({ cells: { 1: [0] } }))).includes('"mout"'));
});
