// ONE WRAP FOR THE WHOLE SONG (owner, 12 Nov: "looping around to the beginning sometimes jumps to a random section"). A track that runs into an
// empty cell waits; the song starts again for every track from one row when all are waiting; wake() brings a waiting track back at a later row
// where it has a block. Before: each track went back to the top of its own run of blocks. makeSeq from core/seq-core.js (generated).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ctx = H.workletScope(); vm.runInContext(H.read('core/seq-core.js'), ctx, { filename: 'seq-core.js' }); vm.runInContext('TFX = {};', ctx);
const NT = 32, MT = 32, FX0 = vm.runInContext('FX0', ctx), RW = vm.runInContext('RW', ctx), I = n => { const i = vm.runInContext(`FXC.indexOf('${n}')`, ctx); assert.ok(i >= 0, n); return i; };
function song(blocks, muted = []){   // blocks: {t, y, cmds:[[name, value]]} (each a 4-step phrase with a note on step 0)
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 4, plen: [], tune: new Array(12).fill(0), insts: [{}], parts: [], master: 255, mout: 255,
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [], phrases: [],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })), tfx: Array.from({ length: NT }, () => []), mfx: [], smute: new Array(256).fill(0),
    perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo: {} } };
  blocks.forEach((b, i) => { const ph = Array.from({ length: 4 }, () => new Array(RW).fill(-1)); ph[0][0] = 48; b.cmds.forEach((c, k) => { ph[0][FX0 + 2 * k] = I(c[0]); ph[0][FX0 + 2 * k + 1] = c[1]; });
    S.phrases[i] = ph; S.plen[i] = 4; S.chains[i] = [[i, 0]]; S.song[b.y][b.t] = i; });
  for (const [y, t] of muted) S.smute[y] |= 1 << t;
  return S;
}
function play(S, o, steps){
  ctx.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(ctx.__S, 'song', Object.assign({ row: 0, sim: true }, o)), evs = [];
  for (let i = 0; i < steps && sq.alive(); i++) evs.push(...sq.step().evs);
  return { evs: JSON.parse(JSON.stringify(evs)), bpm: sq.bpm };
}
const notes = (evs, t) => evs.filter(e => e.type === 'note' && e.track === t).map(e => Math.round(e.t * 1000) / 1000);
const B = (t, y) => ({ t, y, cmds: [] });   // a 4-step block: 0.5 s at 120 BPM, 4 lines a beat
test('a track whose blocks start at row 2 plays them at rows 2-3 on every pass, not against rows 0-1', () => {
  const S = song([B(0, 0), B(0, 1), B(0, 2), B(0, 3), B(1, 2), B(1, 3)]);
  const { evs } = play(S, { once: false, live: true }, 34);
  assert.deepEqual(notes(evs, 0).slice(0, 8), [0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5]);
  assert.deepEqual(notes(evs, 1).slice(0, 4), [1, 1.5, 3, 3.5]);
});
test('a gap in a track: it waits, comes back after the gap, and the song wraps once for all', () => {
  const S = song([B(0, 0), B(0, 1), B(0, 2), B(0, 3), B(1, 0), B(1, 2)]);
  const { evs } = play(S, { once: false, live: true }, 34);
  assert.deepEqual(notes(evs, 1).slice(0, 4), [0, 1, 2, 3]);
  assert.deepEqual(notes(evs, 0).slice(0, 8), [0, 0.5, 1, 1.5, 2, 2.5, 3, 3.5]);
});
test('a shorter track waits for the longest one: they start the next pass together', () => {
  const S = song([B(0, 0), B(0, 1), B(0, 2), B(1, 0)]);
  const { evs } = play(S, { once: false, live: true }, 30);
  assert.deepEqual(notes(evs, 1).slice(0, 3), [0, 1.5, 3]);
});
test('an entirely empty row still divides sections: the wrap goes to the top of the section being played', () => {
  const S = song([B(0, 0), B(0, 2), B(0, 3)]);
  const sq = ctx.makeSeq((ctx.__S = JSON.parse(JSON.stringify(S)), vm.runInContext('S = __S;', ctx), ctx.__S), 'song', { row: 2, once: false, live: true, sim: true }), rows = [];
  for (let i = 0; i < 20; i++){ const r = sq.step(); if (r.pos[0]) rows.push(r.pos[0].row); }
  assert.deepEqual([...new Set(rows)], [2, 3]);
});
test('END is an end, not a wrap, even with a track waiting', () => {
  const S = song([B(0, 0), { t: 0, y: 1, cmds: [['END', 0]] }, B(1, 0)]);
  ctx.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(ctx.__S, 'song', { row: 0, once: false, live: true, sim: true }); let n = 0; while (sq.alive() && n++ < 100) sq.step();
  assert.ok(n < 20, 'the song ended (' + n + ' steps)');
});
test('a render (once) and LOOP are as before', () => {
  const S = song([B(0, 0), B(0, 1), B(1, 1)]);
  assert.deepEqual(notes(play(S, { once: true }, 40).evs, 0), [0, 0.5]);
  const lp = play(S, { once: false, loop: { y0: 1, y1: 1 }, row: 1 }, 12).evs; assert.deepEqual(notes(lp, 1).slice(0, 3), [0, 0.5, 1]);
});
