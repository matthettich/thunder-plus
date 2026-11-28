// STEP COMPONENTS (build 704; owner, 28 Nov, after the OP-Z / OP-XY step components): RST RUP RDN MUL SWP GTO SPK PUL HLD.
// Run through makeSeq from core/seq-core.js (generated from index.html: tests/core.test.js checks that), 120 BPM, 4 lines a beat: a step is 0.125 s.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ctx = H.workletScope(); vm.runInContext(H.read('core/seq-core.js'), ctx, { filename: 'seq-core.js' }); vm.runInContext('TFX = {};', ctx);
const NT = 32, FX0 = vm.runInContext('FX0', ctx), RW = vm.runInContext('RW', ctx);
const I = n => { const i = vm.runInContext(`FXC.indexOf('${n}')`, ctx); assert.ok(i >= 0, n); return i; };
// tracks: {t: [phrase, ...]}, a phrase = {len, rows:{y: {n: note or null, fx: [[name, value], ...]}}, rep: its chain plays it that many times, chain: phrase numbers (counted over all tracks, in order), hidden: no song row of its own}; phrase i of a track sits on song row i
function song(tracks, o = {}){
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 16, plen: [], tune: new Array(12).fill(0), insts: o.insts || [{ tsp: 0, vol: 255, pan: 128, pad: 0, ofs: 0 }], parts: [], master: 255, mout: 255,
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [], phrases: [],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })), tfx: Array.from({ length: NT }, () => []), mfx: [], smute: new Array(256).fill(0),
    perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo: {} } };
  if (o.scale != null){ S.scale = o.scale; S.sroot = o.sroot || 0; }
  let pi = 0;
  for (const t of Object.keys(tracks)) tracks[t].forEach((p, y) => { const len = p.len || 16, ph = Array.from({ length: len }, () => new Array(RW).fill(-1));
    for (const [yy, r] of Object.entries(p.rows || {})){ if (r.n !== null) ph[yy][0] = r.n == null ? 48 : r.n; (r.fx || []).forEach((c, k) => { ph[yy][FX0 + 2 * k] = I(c[0]); ph[yy][FX0 + 2 * k + 1] = c[1]; }); }
    S.phrases[pi] = ph; S.plen[pi] = len; S.chains[pi] = p.chain ? p.chain.map(c => [c, 0]) : Array.from({ length: p.rep || 1 }, () => [pi, 0]); if (!p.hidden) S.song[y][+t] = pi; pi++; });
  return S;
}
function play(S, o, steps){
  ctx.__S = JSON.parse(JSON.stringify(S)); vm.runInContext('S = __S;', ctx);
  const sq = ctx.makeSeq(ctx.__S, 'song', Object.assign({ row: 0, sim: true, once: true }, o)), evs = [];
  for (let i = 0; i < (steps || 4000) && sq.alive(); i++) evs.push(...sq.step().evs);
  return JSON.parse(JSON.stringify(evs));
}
const STEP = 0.125, TICK = STEP / 6;
const st = e => Math.round(e.t / STEP * 1000) / 1000;
const notes = (evs, t = 0) => evs.filter(e => e.type === 'note' && e.track === t);
const steps = (evs, t = 0) => notes(evs, t).map(st);
const mns = (evs, t = 0) => notes(evs, t).map(e => e.mn);
const near = (a, b, m) => { assert.equal(a.length, b.length, m + ': ' + JSON.stringify(a)); a.forEach((x, i) => assert.ok(Math.abs(x - b[i]) < 1e-3, m + ': ' + JSON.stringify(a))); };
const N = (fx, n) => ({ n, fx });   // a row: note n (48 = C-4) with the commands fx
// the same row, k rows in a row
const rep = (k, fx, n) => Object.fromEntries(Array.from({ length: k }, (_, y) => [y, N(fx, n)]));
const KF = { tsp: 0, vol: 255, pan: 128, pad: 0, ofs: 0, kf: true }, PL = { tsp: 0, vol: 255, pan: 128, pad: 0, ofs: 0 };
const one = (fx, o, rep = 8) => song({ 0: [{ len: 1, rep, rows: { 0: N(fx) } }] }, o);   // one row, played rep times (a one-row phrase repeated by its chain)
const near1 = (a, b, m) => assert.ok(Math.abs(a - b) < 1e-3, m + ': ' + a + ' vs ' + b);

test('FXC: the nine step components are on its end, after FLZ, in order', () => {
  const f = Array.from(vm.runInContext('FXC', ctx)), i = f.indexOf('FLZ');
  assert.deepEqual(f.slice(i), ['FLZ', 'RST', 'RUP', 'RDN', 'MUL', 'SWP', 'GTO', 'SPK', 'PUL', 'HLD']);
});

test('RUP: each play of the row climbs X semitones (Y plays, then the written note again); X and Y 0 mean 1 and 4; no scale on a plain instrument', () => {
  assert.deepEqual(mns(play(one([['RUP', 0x14]]))), [60, 61, 62, 63, 60, 61, 62, 63]);
  assert.deepEqual(mns(play(one([['RUP', 0x23]]))), [60, 62, 64, 60, 62, 64, 60, 62]);
  assert.deepEqual(mns(play(one([['RUP', 0]]))), [60, 61, 62, 63, 60, 61, 62, 63], 'RUP 00: X 1, Y 4');
  assert.deepEqual(mns(play(one([['RUP', 0x10]]))), [60, 61, 62, 63, 60, 61, 62, 63], 'RUP 10: Y 4');
  assert.deepEqual(mns(play(one([['RUP', 0x14]], { scale: 1 }))), [60, 61, 62, 63, 60, 61, 62, 63], 'a scale set but the instrument is not KEY FOLLOW: semitones');
  assert.deepEqual(steps(play(one([['RUP', 0x14]]))), [0, 1, 2, 3, 4, 5, 6, 7]);
});
test('RUP on a KEY FOLLOW instrument steps along the scale (the song’s, or SCA’s); RDN falls', () => {
  assert.deepEqual(mns(play(one([['RUP', 0x14]], { insts: [KF], scale: 1, sroot: 0 }))), [60, 62, 64, 65, 60, 62, 64, 65], 'C major, one degree a play');
  assert.deepEqual(mns(play(one([['RUP', 0x23]], { insts: [KF], scale: 1, sroot: 0 }))), [60, 64, 67, 60, 64, 67, 60, 64], 'two degrees a play');
  assert.deepEqual(mns(play(song({ 0: [{ len: 1, rep: 6, rows: { 0: N([['SCA', 0x01], ['RUP', 0x13]]) } }] }, { insts: [KF] }))), [60, 62, 64, 60, 62, 64], 'SCA 01 (C major) on the row');
  assert.deepEqual(mns(play(one([['RUP', 0x14]], { insts: [KF] }))), [60, 61, 62, 63, 60, 61, 62, 63], 'KEY FOLLOW but no scale: semitones');
  assert.deepEqual(mns(play(one([['RDN', 0x23]]))), [60, 58, 56, 60, 58, 56, 60, 58], 'RDN: semitones down');
  assert.deepEqual(mns(play(one([['RDN', 0x13]], { insts: [KF], scale: 1 }))), [60, 59, 57, 60, 59, 57, 60, 59], 'RDN along C major: B, A');
});
test('RUP: a counter for each row (and track); another row’s plays do not move it', () => {
  const S = song({ 0: [{ len: 2, rep: 4, rows: { 0: N([['RUP', 0x1F]]), 1: N([['RUP', 0x2F]], 50) } }], 1: [{ len: 1, rep: 4, rows: { 0: N([['RUP', 0x1F]]) } }] });
  const e = play(S);
  assert.deepEqual(mns(e).filter((_, i) => i % 2 === 0), [60, 61, 62, 63], 'row 0');
  assert.deepEqual(mns(e).filter((_, i) => i % 2 === 1), [62, 64, 66, 68], 'row 1 (note 50 = 62)');
  assert.deepEqual(mns(e, 1), [60, 61, 62, 63], 'the same row on another track starts at the written note');
});
test('RST: this track’s counters start again from its row; RST 01 every track’s; NTH on the RST row counts the pass as the first', () => {
  assert.deepEqual(steps(play(one([['NTH', 0x21]]))), [0, 2, 4, 6], 'NTH 21 alone: every other pass');
  assert.deepEqual(steps(play(one([['NTH', 0x21], ['RST', 0]]))), [0, 1, 2, 3, 4, 5, 6, 7], 'with RST on the row: always the first pass');
  const two = rst => song({ 0: [{ len: 2, rep: 6, rows: { 0: N([['NTH', 0x21]]), 1: { n: null, fx: rst == null ? [] : [['RST', rst]] } } }], 1: [{ len: 2, rep: 6, rows: { 0: N([['NTH', 0x21]]) } }] });
  let e = play(two(null)); assert.deepEqual(steps(e, 0), [0, 4, 8]); assert.deepEqual(steps(e, 1), [0, 4, 8]);
  e = play(two(0)); assert.deepEqual(steps(e, 0), [0, 2, 4, 6, 8, 10], 'RST 00 resets its own track: NTH 21 plays every pass'); assert.deepEqual(steps(e, 1), [0, 4, 8], 'the other track counts on');
  e = play(two(1)); assert.deepEqual(steps(e, 0), [0, 2, 4, 6, 8, 10]); assert.deepEqual(steps(e, 1), [0, 2, 4, 6, 8, 10], 'RST 01 resets every track');
  // RUP's ramp
  const rr = song({ 0: [{ len: 1, rows: { 0: N([['RUP', 0x1F]]) }, chain: [0, 0, 1, 0, 0] }, { len: 1, hidden: true, rows: { 0: { n: null, fx: [['RST', 0]] } } }] });
  assert.deepEqual(mns(play(rr)), [60, 61, 60, 61], 'RST between two plays of the row: the ramp starts over');
});

test('MUL: Y hits evenly across the step whatever the ticks (3 and 5 at 6 ticks); 0 and 1 are off', () => {
  near(steps(play(one([['MUL', 3]], {}, 1))), [0, 1 / 3, 2 / 3], 'MUL 03');
  near(steps(play(one([['MUL', 5]], {}, 1))), [0, 0.2, 0.4, 0.6, 0.8], 'MUL 05');
  for (const tk of [4, 5, 7, 8]){ const S = one([['MUL', 3]], {}, 1); S.ticks = tk; near(steps(play(S)), [0, 1 / 3, 2 / 3], 'MUL 03 at ' + tk + ' ticks'); }
  near(steps(play(one([['MUL', 0x0F]], {}, 1))).slice(-1), [14 / 15], 'MUL 0F: 15 hits');
  assert.equal(notes(play(one([['MUL', 0x0F]], {}, 1))).length, 15);
  assert.equal(notes(play(one([['MUL', 0]], {}, 1))).length, 1, 'MUL 00 off');
  assert.equal(notes(play(one([['MUL', 1]], {}, 1))).length, 1, 'MUL 01 off');
  assert.equal(notes(play(one([['MUL', 0x40]], {}, 1))).length, 1, 'MUL 40: Y 0, off');
});
test('MUL: X fades each hit like RET’s X; RTV shapes the hits; MUL wins over RET; RET alone is unchanged', () => {
  assert.deepEqual(notes(play(one([['MUL', 0x43]], {}, 1))).map(e => +e.gain.toFixed(3)), [1, 0.75, 0.5], 'MUL 43: each hit 4/16 softer');
  assert.deepEqual(notes(play(one([['RET', 0x42]], {}, 1))).map(e => +e.gain.toFixed(3)), [1, 0.75, 0.5], 'RET 42 fades the same way');
  assert.deepEqual(notes(play(one([['MUL', 3], ['RTV', 0x08]], {}, 1))).map(e => +e.gain.toFixed(3)), [0.5, 0.75, 1], 'RTV +08: a crescendo up to the last hit');
  assert.deepEqual(notes(play(one([['MUL', 3], ['RTV', 0xF8]], {}, 1))).map(e => +e.gain.toFixed(3)), [1, 0.75, 0.5], 'RTV −08: a fade');
  near(steps(play(one([['RET', 4]], {}, 1))), [0, 4 / 6], 'RET 04 (4 ticks of 6): two hits, as before');
  near(steps(play(one([['MUL', 3], ['RET', 4]], {}, 1))), [0, 1 / 3, 2 / 3], 'MUL and RET on one row: MUL');
  near(steps(play(one([['RET', 2]], {}, 1))), [0, 1 / 3, 2 / 3], 'RET 02 at 6 ticks: three hits');
});

test('GTO: after the row the track goes on from row XX; the pass still lasts the phrase’s length, then the next song row plays', () => {
  const mk = (extra, len = 8) => { const rows = {}; for (let y = 1; y < len; y++) rows[y] = N([['PIT', y]]); rows[0] = N(extra); return song({ 0: [{ len, rows }, { len: 1, rows: { 0: N([['PIT', 9]]) } }] }); };
  assert.deepEqual(mns(play(mk([['GTO', 4]]))), [60, 64, 65, 66, 67, 60, 64, 65, 69], 'rows 0 4 5 6 7 0 4 5: 8 steps, row 1 of the song at step 8');
  assert.deepEqual(steps(play(mk([['GTO', 4]]))), [0, 1, 2, 3, 4, 5, 6, 7, 8]);
  assert.deepEqual(mns(play(mk([['GTO', 2]]))), [60, 62, 63, 64, 65, 66, 67, 60, 69], 'rows 0 2 3 4 5 6 7 0');
  assert.deepEqual(mns(play(mk([['GTO', 0x1F]]))), [60, 67, 60, 67, 60, 67, 60, 67, 69], 'past the phrase: clamped to its last row');
  assert.deepEqual(mns(play(mk([['GTO', 0]]))), [60, 60, 60, 60, 60, 60, 60, 60, 69], 'GTO 00: the first row over and over for the pass');
  assert.deepEqual(mns(play(mk([['GTO', 4]], 4))), [60, 63, 60, 63, 69], 'row 4 of a 4-row phrase: its last row');
});
test('GTO on a later row; with DIR: the jump, then the direction, the pass still 8 steps', () => {
  const rows = {}; for (let y = 0; y < 8; y++) rows[y] = N([['PIT', y]]); rows[3] = N([['PIT', 3], ['GTO', 1]]);
  assert.deepEqual(mns(play(song({ 0: [{ len: 8, rows }, { len: 1, rows: { 0: N([['PIT', 9]]) } }] }))), [60, 61, 62, 63, 61, 62, 63, 61, 69]);
  const mk = extra => { const r = {}; for (let y = 1; y < 8; y++) r[y] = N([['PIT', y]]); r[0] = N(extra); return song({ 0: [{ len: 8, rows: r }, { len: 1, rows: { 0: N([['PIT', 9]]) } }] }); };
  const a = play(mk([['GTO', 4], ['DIR', 1]])), b = play(mk([['GTO', 4], ['DIR', 2]]));
  assert.deepEqual(mns(a), [60, 64, 63, 62, 61, 60, 64, 63, 69], 'GTO 4 then backwards');
  assert.deepEqual(steps(a), [0, 1, 2, 3, 4, 5, 6, 7, 8], 'the pass lasts 8 steps');
  assert.deepEqual(mns(b), [60, 64, 65, 66, 67, 66, 65, 64, 69], 'GTO 4 then ping-pong');
  assert.deepEqual(steps(b), [0, 1, 2, 3, 4, 5, 6, 7, 8]);
  assert.deepEqual(mns(play(mk([['DIR', 1]]))), [60, 67, 66, 65, 64, 63, 62, 61, 69], 'DIR 01 alone, for reference');
});

test('SPK 01: the note only is chance / NTH / CND’s; the row’s commands always run (FXV)', () => {
  const sp = fx => song({ 0: [{ len: 1, rep: 8, rows: { 0: N(fx.concat([['PIT', 3]])) } }] });
  assert.deepEqual(steps(play(sp([['NTH', 0x21], ['SPK', 1]]))), [0, 2, 4, 6], 'the note keeps its NTH');
  assert.deepEqual(mns(play(sp([['NTH', 0x21], ['SPK', 1]]))), [63, 63, 63, 63]);
  assert.equal(notes(play(sp([['CHA', 0], ['SPK', 1]]))).length, 0, 'CHA 00: no note');
  const fxp = e => e.filter(x => x.type === 'fxp').map(x => [st(x), x.v]);
  const fv = fx => song({ 0: [{ len: 1, rep: 4, rows: { 0: N(fx) } }] });
  assert.deepEqual(fxp(play(fv([['FXS', 0x11], ['NTH', 0x21], ['FXV', 100]]))), [[0, 100], [2, 100]], 'no SPK: FXV goes with the row');
  assert.deepEqual(fxp(play(fv([['NTH', 0x21], ['SPK', 1], ['FXV', 100]]))), [[0, 100], [1, 100], [2, 100], [3, 100]], 'SPK 01: FXV on every pass');
  assert.equal(fxp(play(fv([['CHA', 0], ['SPK', 1], ['FXV', 100]]))).length, 4, 'CHA 00 + SPK 01: no notes, FXV every pass');
});
test('SPK 02: the commands only are the condition’s; the note always plays, plain when it says no', () => {
  const sp = fx => song({ 0: [{ len: 1, rep: 8, rows: { 0: N(fx.concat([['PIT', 3]])) } }] });
  const e = play(sp([['NTH', 0x21], ['SPK', 2]]));
  assert.deepEqual(steps(e), [0, 1, 2, 3, 4, 5, 6, 7], 'a note every pass');
  assert.deepEqual(mns(e), [63, 60, 63, 60, 63, 60, 63, 60], 'PIT only on the passes NTH says yes');
  assert.deepEqual(mns(play(sp([['CHA', 0], ['SPK', 2]]))), [60, 60, 60, 60, 60, 60, 60, 60], 'CHA 00: the note plays, plain');
  const g = play(song({ 0: [{ len: 1, rep: 4, rows: { 0: N([['NTH', 0x21], ['SPK', 2], ['RET', 2]]) } }] }));
  assert.deepEqual(steps(g).map(x => Math.round(x * 3) / 3), [0, 1 / 3, 2 / 3, 1, 2, 2 + 1 / 3, 2 + 2 / 3, 3].map(x => Math.round(x * 3) / 3), 'RET repeats on the played passes, one plain note on the others');
  const v = play(song({ 0: [{ len: 1, rep: 4, rows: { 0: N([['NTH', 0x21], ['SPK', 2], ['VOL', 64]]) } }] }));
  assert.ok(notes(v)[0].gain < 0.5 && notes(v)[1].gain === 1, 'VOL 64 only on the passes NTH says yes');
});
test('SPK 02: “from here on” commands still run (TSP); SPK 00 is the same as no SPK', () => {
  const rows = { 0: N([['NTH', 0x22], ['SPK', 2], ['TSP', 12]]), 1: N([]) };
  assert.deepEqual(mns(play(song({ 0: [{ len: 2, rep: 2, rows }] }))), [72, 72, 72, 72], 'TSP 12 holds on the pass NTH says no: the note plays plain, but transposed');
  for (const fx of [[['NTH', 0x21]], [['CHA', 128], ['PIT', 2]], [['NTH', 0x31], ['VOL', 90]]]){
    const withSpk = play(song({ 0: [{ len: 1, rep: 12, rows: { 0: N(fx.concat([['SPK', 0]])) } }, { len: 1, hidden: true, rows: {} }] }), { seed: 5 });
    const without = play(song({ 0: [{ len: 1, rep: 12, rows: { 0: N(fx) } }, { len: 1, hidden: true, rows: {} }] }), { seed: 5 });
    assert.equal(JSON.stringify(withSpk), JSON.stringify(without), 'SPK 00 = no SPK: ' + JSON.stringify(fx));
  }
});

test('PUL: the row plays Y times in all before the track moves on; later rows come later on this track only; 0 and 1 do nothing', () => {
  const pr = fx => song({ 0: [{ len: 4, rows: { 0: N([['PIT', 1]]), 1: N(fx.concat([['PIT', 2]])), 2: N([['PIT', 3]]), 3: N([['PIT', 4]]) } }, { len: 1, rows: { 0: N([['PIT', 9]]) } }],
    1: [{ len: 4, rows: { 0: N([['PIT', 1]]), 1: N([['PIT', 2]]), 2: N([['PIT', 3]]), 3: N([['PIT', 4]]) } }, { len: 1, rows: { 0: N([['PIT', 9]]) } }] });
  const e = play(pr([['PUL', 3]]));
  assert.deepEqual(steps(e, 0), [0, 1, 2, 3, 4, 5, 6]); assert.deepEqual(mns(e, 0), [61, 62, 62, 62, 63, 64, 69], 'row 1 three times, the rest 2 steps later');
  assert.deepEqual(steps(e, 1), [0, 1, 2, 3, 4], 'the other track is not affected'); assert.deepEqual(mns(e, 1), [61, 62, 63, 64, 69]);
  assert.deepEqual(steps(play(pr([['PUL', 1]]))), [0, 1, 2, 3, 4]); assert.deepEqual(steps(play(pr([['PUL', 0]]))), [0, 1, 2, 3, 4]);
  assert.equal(notes(play(pr([['PUL', 0x0F]])), 0).filter(x => x.mn === 62).length, 15, 'PUL 0F: 15 plays');
});
test('HLD: the note once, ringing on; the repeats emit no notes but wait the same; later rows 2 steps later on that track only', () => {
  const pr = fx => song({ 0: [{ len: 4, rows: { 0: N([['PIT', 1]]), 1: N(fx.concat([['PIT', 2]])), 2: N([['PIT', 3]]), 3: N([['PIT', 4]]) } }, { len: 1, rows: { 0: N([['PIT', 9]]) } }],
    1: [{ len: 4, rows: { 0: N([['PIT', 1]]), 1: N([['PIT', 2]]), 2: N([['PIT', 3]]), 3: N([['PIT', 4]]) } }, { len: 1, rows: { 0: N([['PIT', 9]]) } }] });
  const e = play(pr([['HLD', 3]]));
  assert.deepEqual(steps(e, 0), [0, 1, 4, 5, 6]); assert.deepEqual(mns(e, 0), [61, 62, 63, 64, 69], 'one note for the held row');
  assert.deepEqual(steps(e, 1), [0, 1, 2, 3, 4], 'the other track is not affected');
  assert.equal(e.filter(x => x.track === 0 && x.type === 'note' && st(x) > 1 && st(x) < 4).length, 0, 'the repeats read nothing');
  assert.equal(e.filter(x => x.track === 0 && (x.type === 'off' || x.type === 'stop' || x.type === 'kill')).length, 0, 'nothing stops the note');
});
test('PUL XY with X = 1: a random count 1 to Y from the seeded stream; SED repeats it; a row a condition skips does not pulse', () => {
  const rs = sd => song({ 0: [{ len: 1, rows: { 0: { n: null, fx: [['SED', sd]] } } }, { len: 1, rep: 10, rows: { 0: N([['PUL', 0x14]]) } }] });
  const a = steps(play(rs(7))), b = steps(play(rs(7))), c = steps(play(rs(9)));
  assert.deepEqual(a, b, 'the same counts every play under one seed'); assert.notDeepEqual(a, c, 'another seed, other counts');
  assert.ok(a.length > 10 && a.length <= 10 * 4, 'between 1 and 4 plays of the row on each of the 10 passes: ' + a.length); assert.notEqual(a.length, 10 * 4, 'not always the most');
  const e = play(song({ 0: [{ len: 2, rep: 4, rows: { 0: N([['NTH', 0x22], ['PUL', 3]]), 1: N([['PIT', 5]]) } }] }));
  assert.equal(steps(e)[0], 1, 'pass 1: NTH 22 says no: the row does not pulse and row 1 plays at step 1');
  assert.equal(mns(e)[0], 65);
});

test('SWP on TVL: glides over XY ticks from the last value, a point a tick; the first value jumps', () => {
  const tv = e => e.filter(x => x.type === 'tvl').map(x => [st(x), x.v]);
  const e = tv(play(song({ 0: [{ len: 4, rows: { 0: N([['TVL', 200]]), 2: N([['TVL', 20], ['SWP', 6]]) } }] })));
  assert.equal(e.length, 7, 'the jump, then 6 points'); assert.deepEqual(e[0], [0, 200]);
  e.slice(1).forEach(([t, v], k) => { near1(t, 2 + k / 6, 'time ' + k); assert.equal(v, 170 - 30 * k); });
  const f = tv(play(song({ 0: [{ len: 4, rows: { 0: N([['TVL', 200], ['SWP', 0]]), 2: N([['TVL', 20], ['SWP', 0]]) } }] })));
  assert.equal(f.length, 7, 'SWP 00 on the first TVL: it jumps; on the second: across the step (6 ticks, 6 points)'); assert.deepEqual(f[0], [0, 200]); assert.equal(f[6][1], 20);
  assert.equal(tv(play(song({ 0: [{ len: 4, rows: { 0: N([['TVL', 200]]), 2: N([['TVL', 20]]) } }] }))).length, 2, 'without SWP: it jumps');
  const S = song({ 0: [{ len: 4, rows: { 0: N([['TVL', 200]]), 2: N([['TVL', 20], ['SWP', 0]]) } }] }); S.ticks = 4;
  assert.equal(tv(play(S)).length, 5, 'SWP 00 across a 4-tick step: 4 points');
  assert.equal(tv(play(song({ 0: [{ len: 4, rows: { 0: N([['TVL', 0]]), 2: N([['TVL', 255], ['SWP', 0x40]]) } }] }))).length, 33, 'at most 32 points');
  const h = tv(play(song({ 0: [{ len: 4, rows: { 0: N([['TVL', 100]]), 2: N([['TVL', 100], ['SWP', 6]]) } }] })));
  assert.equal(h.length, 2, 'no change of value: no glide');
});
test('SWP on TPN and FXV; SWP 03 is 3 ticks; the track’s value is kept between rows', () => {
  const ty = (e, k) => e.filter(x => x.type === k).map(x => [st(x), x.v]);
  const p = ty(play(song({ 0: [{ len: 4, rows: { 0: N([['TPN', 0]]), 2: N([['TPN', 120], ['SWP', 6]]) } }] })), 'tpn');
  assert.deepEqual(p.map(x => x[1]), [0, 20, 40, 60, 80, 100, 120]); near1(p[1][0], 2, 'first point at the row');
  const x = ty(play(song({ 0: [{ len: 4, rows: { 0: N([['FXS', 0x11], ['FXV', 0]]), 2: N([['FXV', 120], ['SWP', 6]]) } }] })), 'fxp');
  assert.deepEqual(x.map(y => y[1]), [0, 20, 40, 60, 80, 100, 120], 'FXV: the first jumps, the second glides');
  const q = ty(play(song({ 0: [{ len: 4, rows: { 0: N([['TVL', 90]]), 2: N([['TVL', 0], ['SWP', 3]]) } }] })), 'tvl');
  assert.equal(q.length, 4, 'SWP 03: 3 points'); near1(q[3][0], 2 + 2 / 6, 'last point at 2 ticks');
  const m = ty(play(song({ 0: [{ len: 6, rows: { 0: N([['TVL', 100]]), 2: N([['TVL', 40], ['SWP', 0x06]]), 4: N([['TVL', 10], ['SWP', 0x06]]) } }] })), 'tvl');
  assert.equal(m.find(y => y[0] >= 4)[1], 35, 'the second glide starts from the first one’s target (40), not from where the page was');
});
test('PUL repeats are not new passes: the row’s NTH (and chance) is decided once, on its first read', () => {
  const S = one([['NTH', 0x21], ['PUL', 3]], {}, 4);   // played on pass 1 of every 2: passes 1 and 3 pulse three times, 2 and 4 are silent and do not pulse
  near(steps(play(S)), [0, 1, 2, 4, 5, 6], 'NTH 21 + PUL 03');
});
