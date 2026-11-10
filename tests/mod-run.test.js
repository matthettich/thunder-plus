// MOD: LFOs ALWAYS RUN (owner, 10 Nov). The track LFOs (Perform -> LFO) used to be sent only with a track's own rows, so one on the
// master, on a bus or on a track with no pattern on the song row did nothing. makeSeq now sends them for every track on every step
// (a muted block still stops its own). No browser: makeSeq is read from core/seq-core.js (generated from index.html; the last test
// pins that the two copies of makeSeq are the same text), run in a vm with a hand-built song, and its events are checked.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const { execFileSync } = require('node:child_process');
const H = require('./helpers');

const NT = 32, MT = 32, BASE = '90ab6b8';   // BASE: the commit before this change (its core is the "old" sequencer)
function loadCore(text){
  const ctx = H.workletScope(); vm.runInContext(text, ctx, { filename: 'seq-core.js' });
  vm.runInContext("TFX = {eq:{ps:[{k:'treble',min:0,max:255},{k:'mid',min:0,max:100}]}};", ctx);
  return ctx;
}
const NEW = loadCore(H.read('core/seq-core.js'));

/* A song: `cells` = {track: [song rows with a pattern]}; every pattern is 4 empty rows (no notes: this is about the automation).
   120 bpm, 4 lines a beat: a step is 0.125 s, a song row 0.5 s. lfo = {track: [{id, sh, rt, dp}]}. */
function mkSong({ cells, lfo = {}, smute = [], chainPatch = null, chainOn = null }){
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 4, plen: [4], tune: new Array(12).fill(0), insts: [{}], parts: [],
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [[[0, 0]]],
    phrases: [Array.from({ length: 4 }, (_, y) => y % 2 ? [-1, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0] : [48 + y, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0])],   // a note on rows 0 and 2
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })),
    tfx: Array.from({ length: NT }, () => []), mfx: [{ type: 'eq', treble: 128, mid: 50 }],
    smute: new Array(256).fill(0), perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo } };
  for (const t in cells) for (const y of cells[t]) S.song[y][+t] = 0;
  smute.forEach(([y, t]) => { S.smute[y] |= 1 << t; });
  S.tfx[3] = [{ type: 'eq', treble: 128, mid: 50 }];
  if (chainPatch){ S.chainPatch = chainPatch; S.chainOn = chainOn; }
  return S;
}
/* Play it through core `ctx` (sim: a render, four points a row like live since 11 Nov; live: four) and give back every event. */
function play(ctx, S, mode, o = {}, steps = 64){
  ctx.__S = S; vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(S, mode, Object.assign({ row: 0, once: true, sim: true }, o)), evs = [];
  for (let i = 0; i < steps && sq.alive(); i++) evs.push(...sq.step().evs);
  return JSON.parse(JSON.stringify(evs));
}
// The value the LFO must have at second `t` (120 bpm = 2 beats a second) for a sine of rate index `rt` round `c`.
const RATES = [1 / 8, 1 / 4, 1 / 2, 1, 2, 4, 8, 16, 32];
const sineAt = (t, c, rt, dp) => { const bt = t * 2 / RATES[rt]; return Math.max(0, Math.min(255, Math.round(c + Math.sin((bt - Math.floor(bt)) * 2 * Math.PI) * dp / 2))); };
const of = (evs, type, track) => evs.filter(e => e.type === type && (track == null || e.track === track));

test('a song with no LFOs: the old sequencer and the new one send the same events', t => {
  let old; try { old = loadCore(execFileSync('git', ['show', BASE + ':core/seq-core.js'], { cwd: H.ROOT, encoding: 'utf8', maxBuffer: 1 << 28, stdio: ['ignore', 'pipe', 'ignore'] })); }
  catch (e){ t.skip('git history to ' + BASE + ' is not here'); return; }
  const shapes = [{ cells: { 0: [0, 1, 2, 3], 1: [0, 1], 2: [1, 2] } }, { cells: { 4: [0, 1, 2, 3], 0: [2, 3] } }, { cells: { 1: [0, 1, 2, 3], 5: [0] } }];
  let n = 0, total = 0;
  for (const sh of shapes) for (const [mode, o] of [['song', {}], ['song', { live: true, once: false }], ['song', { only: new Set([1]) }], ['sect', { track: 1, sect: 0 }], ['phrase', { track: 1, phrase: 0 }]]){
    const a = play(old, mkSong(sh), mode, o, 80), b = play(NEW, mkSong(sh), mode, o, 80);
    total += a.length; assert.deepEqual(b, a, mode + ' ' + JSON.stringify(o)); n++; }
  assert.equal(n, 15); assert.ok(total > 500, 'the songs produced events (' + total + ')');
});

test('an LFO on a master effect with no MST cell: fxp events on every step, following the shape', () => {
  const S = mkSong({ cells: { 1: [0, 1, 2, 3] }, lfo: { [MT]: [{ id: 'fx:32:0:0', sh: 0, rt: 5, dp: 100 }] } });
  const ev = of(play(NEW, S, 'song'), 'fxp', MT);
  assert.equal(ev.length, 16 * 4, '4 points a step in a render (as live), 16 steps');
  const times = ev.map(e => e.t); assert.ok(times.every((x, i) => i === 0 || x > times[i - 1]), 'in time order');
  ev.forEach(e => { assert.equal(e.slot, 0); assert.equal(e.pi, 0); assert.equal(e.v, sineAt(e.t, 128, 5, 100), 'follows the sine at t=' + e.t); });
  const vs = ev.map(e => e.v); assert.ok(Math.max(...vs) >= 128 + 49 && Math.min(...vs) <= 128 - 49, 'swings about 128 by dp/2 = 50');
  assert.ok(Math.max(...vs) <= 178 && Math.min(...vs) >= 78);
  // 4 beats a cycle at 120 bpm is 2 s, and the song is 2 s long: one whole cycle, so it ends where it began
  assert.ok(Math.abs(vs[0] - 128) <= 1 && ev[ev.length - 1].t > 1.8);
});

test('an LFO on a track idle on song rows 2 and 3: events on every step, the phase runs on from the song', () => {
  const S = mkSong({ cells: { 0: [0, 1], 1: [0, 1, 2, 3] }, lfo: { 0: [{ id: 'vol:0', sh: 0, rt: 5, dp: 100 }] } });
  const ev = of(play(NEW, S, 'song'), 'tva', 0);
  assert.equal(ev.length, 64, 'every one of the 16 steps (4 points each, as live): the 8 of its own rows and the 8 idle');
  assert.ok(ev.some(e => e.t >= 1.0) && ev.some(e => e.t < 1.0));
  ev.forEach(e => assert.equal(e.v, sineAt(e.t, 128, 5, 100), 'the song\'s clock, never restarted: t=' + e.t));
  const gaps = ev.slice(1).map((e, i) => e.t - ev[i].t); assert.ok(gaps.every(g => Math.abs(g - 0.03125) < 1e-9), 'an even 4 points a step across the hand-over at 1.0 s');
  for (const sh of [1, 2, 3, 4]){ const E = of(play(NEW, mkSong({ cells: { 0: [0, 1], 1: [0, 1, 2, 3] }, lfo: { 0: [{ id: 'vol:0', sh, rt: 4, dp: 80 }] } }), 'song'), 'tva', 0); assert.equal(E.length, 64, 'shape ' + sh); }
});

test('a muted block\'s LFO stays still; an unmuted idle row moves again', () => {
  const lfo = { 0: [{ id: 'vol:0', sh: 0, rt: 5, dp: 100 }], [MT]: [{ id: 'fx:32:0:0', sh: 0, rt: 5, dp: 100 }] };
  const all = [0, 1, 2, 3].map(y => [y, 0]);
  let evs = play(NEW, mkSong({ cells: { 0: [0, 1], 1: [0, 1, 2, 3] }, lfo, smute: all }), 'song');
  assert.equal(of(evs, 'tva', 0).length, 0, 'muted on its rows and on the idle rows after');
  assert.equal(of(evs, 'fxp', MT).length, 64, 'the master has no block to mute');
  evs = play(NEW, mkSong({ cells: { 0: [0, 1], 1: [0, 1, 2, 3] }, lfo, smute: [[0, 0], [1, 0]] }), 'song');
  const ev = of(evs, 'tva', 0); assert.equal(ev.length, 32, 'only the idle rows 2 and 3'); assert.ok(ev.every(e => e.t >= 1.0 - 1e-9));
});

test('an LFO on a bus effect while only another track plays', () => {
  const lfo = { 3: [{ id: 'fx:3:0:0', sh: 0, rt: 5, dp: 100 }], 2: [{ id: 'vol:2', sh: 0, rt: 5, dp: 100 }], [MT]: [{ id: 'fx:32:0:0', sh: 0, rt: 5, dp: 100 }] };
  const cells = { 1: [0, 1, 2, 3] };
  let evs = play(NEW, mkSong({ cells, lfo }), 'song');
  assert.equal(of(evs, 'fxp', 3).length, 64, 'song: everything plays, so does track 4\'s FX');
  // one pattern on its own plays track 2 (index 1) only: what is not heard does not move, the master does
  evs = play(NEW, mkSong({ cells, lfo }), 'sect', { track: 1, sect: 0 });
  assert.equal(of(evs, 'fxp', 3).length, 0, 'not fed by the track: still'); assert.equal(of(evs, 'tva', 2).length, 0);
  assert.ok(of(evs, 'fxp', MT).length > 0, 'the master');
  // ...unless track 2 feeds track 4 (a Track In 2 patched to a Chain Out 4): a bus
  const chainPatch = { format: 'chains.patch', modules: [{ id: 'a', type: 'trackIn', params: { track: 2 } }, { id: 'b', type: 'chainOut', params: { track: 4 } }], cables: [{ from: ['a', 0], to: ['b', 0] }] };
  const chainOn = Object.assign(new Array(NT).fill(false), { 1: true, 3: true });
  evs = play(NEW, mkSong({ cells, lfo, chainPatch, chainOn }), 'sect', { track: 1, sect: 0 });
  assert.ok(of(evs, 'fxp', 3).length > 0, 'the bus it feeds moves'); assert.equal(of(evs, 'tva', 2).length, 0, 'a track that is no bus and not playing: still');
  evs = play(NEW, mkSong({ cells, lfo, chainPatch, chainOn }), 'song', { only: new Set([1]) });
  assert.ok(of(evs, 'fxp', 3).length > 0 && of(evs, 'fxp', MT).length > 0 && of(evs, 'tva', 2).length === 0, 'BUS PLAY of track 2: its bus and the master');
});

test('points a step: 4 live, 4 in a render (LFO_PTS_RENDER_*), 1 for cut / res / dec, none without LFOs', () => {
  const lfo = { 0: [{ id: 'vol:0', sh: 0, rt: 5, dp: 100 }], [MT]: [{ id: 'fx:32:0:0', sh: 2, rt: 4, dp: 60 }] };
  const cells = { 0: [0], 1: [0, 1, 2, 3] };   // track 1 sets the 16 steps; track 0 is idle from step 4
  const sim = play(NEW, mkSong({ cells, lfo }), 'song'), live = play(NEW, mkSong({ cells, lfo }), 'song', { live: true, sim: false, once: true });
  assert.equal(of(sim, 'tva', 0).length, 16 * 4, '4 a step in a render too'); assert.equal(of(live, 'tva', 0).length, 16 * 4, '4 a step live (ticks 6: min(4, 6))');
  assert.equal(of(sim, 'fxp', MT).length, 64, 'an effect target in a render: 4 a step as live (renders match live)'); assert.equal(of(live, 'fxp', MT).length, 64);
  const none = play(NEW, mkSong({ cells }), 'song'); assert.equal(none.filter(e => e.type === 'tva' || e.type === 'fxp' || e.type === 'tpn').length, 0, 'no LFOs: no automation at all');
  assert.equal(of(none, 'beat').length, 16, 'one beat event a step, as ever');
  // a filter target is one value a step, kept for the next note (nothing is sent: PF.pno), also on an idle track
  const cut = play(NEW, mkSong({ cells, lfo: { 0: [{ id: 'cut:0', sh: 0, rt: 5, dp: 100 }] } }), 'song'); assert.equal(cut.filter(e => e.type === 'tva' || e.type === 'fxp').length, 0);
});

test('the generated core has the page\'s makeSeq, word for word', () => {
  const fn = text => { const m = text.match(/function makeSeq\(S, mode, o\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'makeSeq found'); return m[0]; };
  assert.equal(fn(H.read('core/seq-core.js')), fn(H.read('index.html')), 'what the tests above ran is what the page runs');
  assert.match(H.read('index.html'), /lfoEvs\(t, t0, dI\)/);
});
