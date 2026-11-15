// CHANCE AND SEEDS (build 610; owner, 14 Nov). Chance rolls (CHA, TCH, PRB) are a hash of the seed, the track and the place in the song,
// not draws from one shared stream: seeded, the same notes fall every time (a render matches what was heard), from the top or the middle,
// and one track's rolls do not depend on another's notes. SED in the master column seeds the song, on a track that track; TCH is the
// track's sticky chance (times CHA; the master column's: every track; MOD target tch:t); SKP what a skipped note does; CHD chords as one
// or note by note; CND 08 turns the row's NTH round. makeSeq from core/seq-core.js (generated from index.html: tests/core.test.js checks that).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ctx = H.workletScope(); vm.runInContext(H.read('core/seq-core.js'), ctx, { filename: 'seq-core.js' }); vm.runInContext('TFX = {};', ctx);
const NT = 32, MT = 32, FX0 = vm.runInContext('FX0', ctx), RW = vm.runInContext('RW', ctx), NCOL = vm.runInContext('NCOL', ctx);
const I = n => { const i = vm.runInContext(`FXC.indexOf('${n}')`, ctx); assert.ok(i >= 0, n); return i; };
const LEN = 16;
// blocks: {t, y, rows:{step:[[name, value], ...]}, all:[[name, value]] (on every step), chord:true (four notes), notes:false (no notes)}
function song(blocks, lfo = {}){
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: LEN, plen: [], tune: new Array(12).fill(0), insts: [{}], parts: [], master: 255, mout: 255,
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [], phrases: [],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })), tfx: Array.from({ length: NT }, () => []), mfx: [], smute: new Array(256).fill(0),
    perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo } };
  blocks.forEach((b, i) => { const ph = Array.from({ length: LEN }, () => new Array(RW).fill(-1));
    for (let y = 0; y < LEN; y++){ if (b.notes !== false && b.t < MT){ ph[y][0] = 48; if (b.chord) for (let c = 1; c < NCOL; c++) ph[y][c] = 48 + 3 * c; }
      const cm = (b.all || []).concat((b.rows && b.rows[y]) || []); cm.forEach((c, k) => { ph[y][FX0 + 2 * k] = I(c[0]); ph[y][FX0 + 2 * k + 1] = c[1]; }); }
    S.phrases[i] = ph; S.plen[i] = LEN; S.chains[i] = [[i, 0]]; S.song[b.y][b.t] = i; });
  return S;
}
function play(S, o, steps){
  ctx.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(ctx.__S, 'song', Object.assign({ row: 0, sim: true, once: true }, o)), evs = [];
  for (let i = 0; i < (steps || 4000) && sq.alive(); i++) evs.push(...sq.step().evs);
  return JSON.parse(JSON.stringify(evs));
}
const STEP = 0.125;   // 120 BPM, 4 lines a beat
const hits = (evs, t) => [...new Set(evs.filter(e => e.type === 'note' && e.track === t).map(e => Math.round(e.t / STEP)))];   // the steps that sounded (a chord counts once)
const row = (t, n, extra = {}) => Array.from({ length: n }, (_, y) => Object.assign({ t, y }, extra));
const CHA = v => [['CHA', v]];

test('CHA 00 never plays, CHA FF always, CHA 80 about half; no chance command: every note', () => {
  assert.equal(hits(play(song(row(0, 4, { all: CHA(0) }))), 0).length, 0);
  assert.equal(hits(play(song(row(0, 4, { all: CHA(255) }))), 0).length, 64);
  assert.equal(hits(play(song(row(0, 4))), 0).length, 64);
  const n = hits(play(song([{ t: MT, y: 0, rows: { 0: [['SED', 7]] } }, ...row(0, 60, { all: CHA(128) })])), 0).length;
  assert.ok(n > 960 * 0.44 && n < 960 * 0.56, 'CHA 80 over 960 notes: ' + n);
});
test('SED in the master column: the same notes every play; another seed, other notes; no SED, a new roll each play; a render (once) = live', () => {
  const mk = sd => song([...(sd == null ? [] : [{ t: MT, y: 0, rows: { 0: [['SED', sd]] } }]), ...row(0, 4, { all: CHA(128) }), ...row(1, 4, { all: CHA(128) })]);
  const a = hits(play(mk(7)), 0), b = hits(play(mk(7)), 0), c = hits(play(mk(9)), 0);
  assert.deepEqual(a, b); assert.notDeepEqual(a, c, 'seed 09 falls differently from seed 07');
  assert.deepEqual(hits(play(mk(7), { once: false, live: true }, 64), 0), a, 'live playing = the render');
  assert.notDeepEqual(hits(play(mk(7)), 1), a, 'two tracks with the same seed do not play the same hits');
  assert.notDeepEqual(hits(play(mk(null)), 0), hits(play(mk(null)), 0), 'unseeded: each play its own roll (as before)');
  assert.deepEqual(hits(play(mk(null), { seed: 1234 }), 0), hits(play(mk(null), { seed: 1234 }), 0), 'a play seed handed in (o.seed) repeats');
  const z = song([{ t: MT, y: 0, rows: { 0: [['SED', 7]] } }, { t: MT, y: 2, rows: { 0: [['SED', 0]] } }, ...row(0, 4, { all: CHA(128) })]);
  const z1 = hits(play(z), 0), z2 = hits(play(z), 0);
  assert.deepEqual(z1.filter(s => s < 32), z2.filter(s => s < 32)); assert.deepEqual(z1.filter(s => s < 32), a.filter(s => s < 32), 'rows 0-1 under SED 07');
  assert.notDeepEqual(z1.filter(s => s >= 32), z2.filter(s => s >= 32), 'master SED 00 at row 2: a new roll each play from there');
});
test('one track’s rolls do not depend on another track’s notes or commands; starting in the middle falls as the whole song does', () => {
  const M = { t: MT, y: 0, rows: { 0: [['SED', 5]] } };
  const alone = hits(play(song([M, ...row(0, 4, { all: CHA(128) })])), 0);
  const busy = hits(play(song([M, ...row(0, 4, { all: CHA(128) }), ...row(1, 4, { all: [['CHA', 90], ['PRB', 100]] }), ...row(2, 4)])), 0);
  assert.deepEqual(busy, alone);
  const mid = hits(play(song([M, ...row(0, 4, { all: CHA(128) })]), { row: 2 }), 0);   // (the SED is on row 0: a start below it takes it along, see CHASE in makeSeq)
  assert.deepEqual(mid, alone.filter(s => s >= 32).map(s => s - 32), 'from row 2: the same hits rows 2-3 have in the whole song');
  const tc = song([M, { t: 0, y: 0, rows: { 0: [['TCH', 0]] } }, { t: 0, y: 1 }, { t: 0, y: 2 }, { t: 1, y: 0, rows: { 3: [['SED', 9]] }, all: CHA(128) }, ...[1, 2, 3].map(y => ({ t: 1, y, all: CHA(128) }))]);
  assert.equal(hits(play(tc, { row: 2 }), 0).length, 0, 'a TCH above the start row still holds');
  assert.deepEqual(hits(play(tc, { row: 2 }), 1), hits(play(tc), 1).filter(s => s >= 32).map(s => s - 32), 'and a track’s own SED above it');
});
test('a looped song rolls each pass differently, and the same way every time it is played', () => {
  const S = song([{ t: MT, y: 0, rows: { 0: [['SED', 3]] } }, ...row(0, 2, { all: CHA(128) })]);
  const a = hits(play(S, { once: false, live: true }, 96), 0), b = hits(play(S, { once: false, live: true }, 96), 0);
  assert.deepEqual(a, b);
  const p1 = a.filter(s => s < 32), p2 = a.filter(s => s >= 32 && s < 64).map(s => s - 32), p3 = a.filter(s => s >= 64).map(s => s - 64);
  assert.notDeepEqual(p1, p2); assert.notDeepEqual(p2, p3);
});
test('TCH: the track’s chance from there on, times CHA; FF switches it off; rows without a note are not touched; the master column’s thins every track', () => {
  const M = { t: MT, y: 0, rows: { 0: [['SED', 11]] } }, n = (S, t = 0) => hits(play(S), t).length;
  const half = n(song([M, { t: 0, y: 0, rows: { 0: [['TCH', 128]] } }, ...Array.from({ length: 59 }, (_, k) => ({ t: 0, y: k + 1 }))]));
  assert.ok(half > 960 * 0.44 && half < 960 * 0.56, 'TCH 80 set once, 60 blocks on: ' + half);
  const quarter = n(song([M, { t: 0, y: 0, rows: { 0: [['TCH', 128], ['CHA', 128]] }, all: [] }, ...Array.from({ length: 59 }, (_, k) => ({ t: 0, y: k + 1, all: CHA(128) }))]));
  assert.ok(quarter > 950 * 0.19 && quarter < 950 * 0.31, 'TCH 80 x CHA 80: about a quarter: ' + quarter);
  assert.equal(n(song([M, { t: 0, y: 0, rows: { 0: [['TCH', 0]], 8: [['TCH', 255]] } }, { t: 0, y: 1 }])), 24, 'TCH 00 silences the track until TCH FF hands back');
  const fxOnly = play(song([M, { t: 0, y: 0, notes: false, rows: { 0: [['TCH', 0]] }, all: [['TVL', 200]] }]));
  assert.equal(fxOnly.filter(e => e.type === 'tvl' && e.track === 0).length, LEN, 'a row with no note: its commands run whatever the track’s chance');
  const mst = song([{ t: MT, y: 0, rows: { 0: [['SED', 11], ['TCH', 0]] } }, { t: MT, y: 1, rows: { 0: [['TCH', 255]] } }, ...row(0, 2), ...row(1, 2)]);
  assert.deepEqual(hits(play(mst), 0), Array.from({ length: 16 }, (_, k) => 16 + k), 'master TCH 00 counts on its own row already, for every track; FF gives the notes back');
  assert.equal(hits(play(mst), 1).length, 16);
});
test('SED on a track: that track only, until its next SED; SED 00 hands it back to the song’s seed', () => {
  const M = { t: MT, y: 0, rows: { 0: [['SED', 7]] } }, base = play(song([M, ...row(0, 4, { all: CHA(128) }), ...row(1, 4, { all: CHA(128) })]));
  const blocks = [M, { t: 0, y: 0, all: CHA(128), rows: { 0: [['SED', 9]] } }, { t: 0, y: 1, all: CHA(128) }, { t: 0, y: 2, all: CHA(128), rows: { 0: [['SED', 0]] } }, { t: 0, y: 3, all: CHA(128) }, ...row(1, 4, { all: CHA(128) })];
  const ov = play(song(blocks));
  assert.deepEqual(hits(ov, 1), hits(base, 1), 'the other track is untouched');
  assert.notDeepEqual(hits(ov, 0).filter(s => s < 32), hits(base, 0).filter(s => s < 32), 'rows 0-1: its own seed');
  assert.deepEqual(hits(ov, 0).filter(s => s >= 32), hits(base, 0).filter(s => s >= 32), 'from SED 00 on: the song’s again');
  assert.deepEqual(hits(play(song(blocks)), 0), hits(ov, 0), 'and it repeats');
});
test('SKP 01: a skipped note stops the note before it; SKP 00 (and by default) it rings on', () => {
  const M = { t: MT, y: 0, rows: { 0: [['SED', 2]] } }, offs = S => play(S).filter(e => e.type === 'off' && e.track === 0).map(e => Math.round(e.t / STEP));
  assert.equal(offs(song([M, { t: 0, y: 0, all: CHA(128) }])).length, 0);
  const S1 = song([M, { t: 0, y: 0, all: CHA(128), rows: { 0: [['SKP', 1]] } }]), h = hits(play(S1), 0), o = offs(S1);
  assert.ok(o.length > 2 && o.length < 14); assert.deepEqual([...h, ...o].sort((a, b) => a - b), Array.from({ length: 16 }, (_, k) => k), 'every step either plays or stops the one before');
  assert.equal(offs(song([M, { t: 0, y: 0, all: CHA(128), rows: { 0: [['SKP', 1]], 1: [['SKP', 0]] } }])).filter(s => s > 0).length, 0, 'SKP 00 puts it back');
});
test('CHD: a chord on a poly track plays or is skipped as one; CHD 01 rolls each note', () => {
  const M = { t: MT, y: 0, rows: { 0: [['SED', 4]] } }, per = S => { const m = new Map(); for (const e of play(S)) if (e.type === 'note' && e.track === 5){ const k = Math.round(e.t / STEP); m.set(k, (m.get(k) || 0) + 1); } return [...m.values()]; };
  const whole = per(song([M, ...row(5, 4, { chord: true, all: CHA(128) })]));
  assert.ok(whole.length > 16 && whole.length < 48); assert.ok(whole.every(n => n === NCOL), 'as one: four notes or none');
  const each = per(song([M, { t: 5, y: 0, chord: true, all: CHA(128), rows: { 0: [['CHD', 1]] } }, ...Array.from({ length: 3 }, (_, k) => ({ t: 5, y: k + 1, chord: true, all: CHA(128) }))]));
  assert.ok(each.some(n => n > 0 && n < NCOL), 'note by note: some chords are thinned'); const tot = each.reduce((a, b) => a + b, 0); assert.ok(tot > 256 * 0.38 && tot < 256 * 0.62, 'about half of the 256 notes: ' + tot);
});
test('CND 08 (NOT) turns the row’s NTH round; PRB is seeded with the rest', () => {
  const S = song([{ t: 0, y: 0, notes: false }]); S.phrases[0][0][0] = 48; S.phrases[0][0][FX0] = I('NTH'); S.phrases[0][0][FX0 + 1] = 0x41; S.phrases[0][0][FX0 + 2] = I('CND'); S.phrases[0][0][FX0 + 3] = 8;
  for (let y = 1; y < 8; y++) S.song[y][0] = 0;
  assert.deepEqual(hits(play(S), 0), [16, 32, 48, 80, 96, 112], 'NTH 41 + CND 08: every pass but the first of four');
  const P = song([{ t: MT, y: 0, rows: { 0: [['SED', 6]] } }, ...row(0, 4, { all: [['PRB', 128]] })]), a = hits(play(P), 0);
  assert.deepEqual(hits(play(P), 0), a); assert.ok(a.length > 20 && a.length < 44, 'PRB 80 alone is the note’s chance: ' + a.length);
});
test('MOD: track chance is a target (tch:t); an LFO on it thins the track, and a TCH command on the track takes over', () => {
  assert.match(H.read('index.html'), /\['tch:' \+ t, 'CHANCE · track chance'\]/, 'in the MOD screen’s target list');
  assert.equal(ctx.tgtOk('tch:3'), true); ctx.__S = song([]); vm.runInContext('S = __S;', ctx);
  assert.equal(ctx.tgtOwn('tch:3', ctx.__S), 255, 'its own value: every note');
  const lfo = { 0: [{ id: 'tch:0', sh: 4, rt: 5, dp: 255 }] }, M = { t: MT, y: 0, rows: { 0: [['SED', 8]] } };
  const n = hits(play(song([M, ...row(0, 8)], lfo)), 0).length;
  assert.ok(n < 128 && n > 64, 'a square LFO at full depth: the low half of each cycle plays about half its notes: ' + n);
  assert.equal(hits(play(song([M, { t: 0, y: 0, rows: { 0: [['TCH', 254]] } }, ...Array.from({ length: 7 }, (_, k) => ({ t: 0, y: k + 1 }))], lfo)), 0).length >= 120, true, 'TCH FE on the track: the command’s chance, not the LFO’s');
});
test('review fixes: a JMP 00 loop rolls each pass differently; a SED on a track alone still seeds the whole song (songs from before); a muted block’s TCH above the start row counts', () => {
  // two rows of chance, then JMP 00 on the master at the top of row 2: three passes
  const J = song([{ t: MT, y: 0, rows: { 0: [['SED', 3]] } }, ...row(0, 2, { all: CHA(128) }), { t: MT, y: 2, rows: { 0: [['JMP', 0]] } }]);
  const a = hits(play(J, { once: false, live: true }, 96), 0), p1 = a.filter(s => s < 32), p2 = a.filter(s => s >= 32 && s < 64).map(s => s - 32);
  assert.ok(p1.length > 8 && p2.length > 8, 'it looped: ' + a.length); assert.notDeepEqual(p1, p2, 'pass 2 is not pass 1 again');
  assert.deepEqual(hits(play(J, { once: false, live: true }, 96), 0), a, 'and the whole thing repeats');
  const old = song([{ t: 0, y: 0, all: CHA(128), rows: { 0: [['SED', 7], ['CHA', 128]] } }, ...[1, 2, 3].map(y => ({ t: 0, y, all: CHA(128) })), ...row(1, 4, { all: CHA(128) })]);
  assert.deepEqual(hits(play(old), 1), hits(play(old), 1), 'SED on track 1 only: track 2 repeats too');
  const mu = song([{ t: 0, y: 0, rows: { 0: [['TCH', 0]] } }, { t: 0, y: 1 }, { t: 0, y: 2 }]); mu.smute[0] |= 1;
  assert.equal(hits(play(mu, { row: 1 }), 0).length, 0, 'the TCH 00 in the muted block above still holds (as it does when the song plays through it)');
  assert.equal(hits(play(mu), 0).length, 0);
});
