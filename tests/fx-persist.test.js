// EFFECT COMMAND STATE (owner, 12 Nov): "A pattern effect that has been turned off or 00'd should persist as off if patterns below it are played
// without crossing the fx command" and "I could still hear fx from a pattern that was muted in the arrangement window". Decision (owner: "Changes to
// old songs are ok"): the state a command sets persists across patterns and song wraps and only STOP resets it (no 'fxreset' while a song plays),
// a muted TRACK block drops its notes and effect commands but still runs the commands that steer the song (tempo, groove, jumps, END), and a
// muted MASTER block (masterBlockMuted) runs none of its commands. Both sequencers.
// No browser: makeSeq is read from core/seq-core.js (generated from index.html; tests/core.test.js keeps them equal).
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
test('a song wrap does not reset effect switches: no fxreset while the song plays (only STOP resets)', () => {
  const S = song([{ t: 0, y: 0, cmds: [['FXO', 0x10]] }, { t: 0, y: 1, cmds: [] }]);
  const { evs } = play(S, { once: false }, 40);   // 2 rows of 4 steps: wraps several times
  assert.ok(evs.filter(e => e.type === 'fxc').length >= 2, 'FXO 10 is still sent on each pass: its row plays again');
  assert.deepEqual(evs.filter(e => e.type === 'fxreset'), []);
});
test('the master column wrapping on its own does not reset the master effects either', () => {
  const S = song([{ t: 0, y: 0, cmds: [] }, { t: 0, y: 1, cmds: [] }, { t: 0, y: 2, cmds: [] }, { t: MT, y: 0, cmds: [['FLX', 0]] }]);
  const { evs } = play(S, { once: false }, 60);
  assert.deepEqual(evs.filter(e => e.type === 'fxreset'), []);
});
test('a muted TRACK block: its notes and effect commands are dropped, but the commands that steer the song still run (tempo here)', () => {
  const S = song([{ t: 0, y: 0, cmds: [] }, { t: 0, y: 1, cmds: [['FXO', 0x10], ['TPO', 90]] }, { t: 0, y: 2, cmds: [] }], [[1, 0]]);
  const { evs, bpm } = play(S, { once: true }, 40);
  assert.deepEqual(evs.filter(e => e.type === 'fxc'), [], 'no effect command from the muted block');
  assert.equal(bpm, 90, 'TPO in a muted block still sets the tempo: muting never changes how the song runs');
  assert.equal(evs.filter(e => e.type === 'note' && e.track === 0).length, 2, 'rows 0 and 2 still play their notes; the muted row\'s is dropped');
});
test('a muted TRACK block\'s END still ends the song (the song goes where it went before the mute)', () => {
  const S = song([{ t: 0, y: 0, cmds: [] }, { t: 0, y: 1, cmds: [['END', 0]] }, { t: 0, y: 2, cmds: [] }]), M = JSON.parse(JSON.stringify(S)); M.smute[1] |= 1;
  const a = play(S, { once: true }, 60).evs.filter(e => e.type === 'note').length, b = play(M, { once: true }, 60).evs.filter(e => e.type === 'note').length;
  assert.equal(a - b <= 1 && a >= b, true, 'muted: at most the muted row\'s own note is missing; row 2 is reached or not exactly as unmuted (' + a + ' vs ' + b + ')');
});
test('an unmuted block still runs its commands (the same song, no mute)', () => {
  const S = song([{ t: 0, y: 0, cmds: [] }, { t: 0, y: 1, cmds: [['FXO', 0x10], ['TPO', 90]] }, { t: 0, y: 2, cmds: [] }]);
  const { evs } = play(S, { once: true }, 40);
  assert.ok(evs.some(e => e.type === 'fxc' && e.p === 'sw0'));
  assert.ok(evs.some(e => e.type === 'tempo' && e.bpm === 90));
});
test('ITEM 5: masterBlockMuted(S, y) skips the master column\'s commands for that row (inert while the helper does not exist)', () => {
  const S = song([{ t: 0, y: 0, cmds: [] }, { t: 0, y: 1, cmds: [] }, { t: MT, y: 0, cmds: [['FLX', 5]] }, { t: MT, y: 1, cmds: [['GAT', 3]] }]);
  const none = play(S, { once: true }, 40).evs.filter(e => e.track === MT && (e.type === 'fxc' || e.type === 'mvol'));
  assert.ok(none.some(e => e.p === 'flx') && none.some(e => e.p === 'gat'), 'no helper: the master plays as before');
  vm.runInContext('var masterBlockMuted = (S, y) => y === 0;', ctx);   // the UI branch's helper: row 0's master block is muted
  try {
    const m = play(S, { once: true }, 40).evs.filter(e => e.track === MT && (e.type === 'fxc' || e.type === 'mvol' || e.type === 'off'));
    assert.ok(!m.some(e => e.p === 'flx' || e.type === 'off'), 'row 0 master block: its commands do not execute, and no note-off is sent to the master');
    assert.ok(m.some(e => e.p === 'gat'), 'row 1 master block still plays');
  } finally { vm.runInContext('masterBlockMuted = undefined; delete globalThis.masterBlockMuted;', ctx); }
});
