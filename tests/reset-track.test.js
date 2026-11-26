// RESET TRACK (19 Nov, build 650; docs/RESET-TRACK-NOTES.md): Cmd+Shift+T (Ctrl+Shift+T elsewhere) clears one track as Renoise's delete track does, but the track stays in place:
// its notes and commands on every song row, its mixer strip, its effect slots, CHN and what PERFORM / the parts hold for it. One undo step. The code under test is cut out of
// index.html and run in a vm; the sequencer is the audio-thread bundle (core/seq-core.js), the same makeSeq the page runs, on a real song (sounds/ELECTRO.thunder).
// The end-to-end run in a real page (the key, both sequencers while playing, undo) is opt-in: TP_BROWSER_TESTS=1.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i + a.length); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };
const line = a => { const i = src.indexOf(a); assert.ok(i >= 0, 'moved: ' + a); return src.slice(i, src.indexOf('\n', i)); };
const plain = x => JSON.parse(JSON.stringify(x));   // (the vm's objects are from another realm)
const NT = 32, MT = 32;

// ---------- the data function, on song objects ----------
const dataCode = cut('function resetTrackData(', '// The track a reset means');
const freshMixCode = line('const freshMix = () =>');
function dataVm() { const c = { NT, MT, TRIM_0: 128 }; vm.createContext(c); vm.runInContext(freshMixCode + '\n' + dataCode + '\nthis.resetTrackData = resetTrackData; this.freshMix = freshMix;', c); return c; }
const D = dataVm();
const fresh = () => plain(D.freshMix());

// ---------- the sequencer, on a song ----------
const seqCtx = H.workletScope(); vm.runInContext(H.read('core/seq-core.js'), seqCtx, { filename: 'seq-core.js' }); vm.runInContext('TFX = {};', seqCtx);
function rows(S, o, max) {   // every row makeSeq steps through, as plain data
  seqCtx.__S = plain(S); vm.runInContext('S = __S;', seqCtx);
  const sq = seqCtx.makeSeq(seqCtx.__S, 'song', Object.assign({ row: 0, sim: true }, o)), out = [];
  for (let i = 0; i < max && sq.alive(); i++) { const r = sq.step(); out.push({ t: r.t, evs: plain(r.evs), pos: plain(r.pos) }); }
  return out;
}
const without = (rs, t) => rs.map(r => ({ t: r.t, evs: r.evs.filter(e => e.track !== t), pos: r.pos.filter((_, i) => i !== t) }));
const ELECTRO = () => plain(JSON.parse(fs.readFileSync(require('path').join(__dirname, '..', 'sounds/ELECTRO.thunder'), 'utf8')).song);
const cellsOf = (S, t) => S.song.filter(r => r[t] >= 0).length;

test('ELECTRO: every track that plays, reset alone: its cells are gone, it plays nothing, the other tracks give exactly the same events (render and live loop)', () => {
  const E = ELECTRO(), base = rows(E, { once: true }, 4000), live0 = rows(E, { once: false, live: true }, 400);
  assert.ok(base.length > 500, 'the song plays');
  let n = 0;
  for (let t = 0; t < NT; t++) {
    if (!cellsOf(E, t)) continue;
    const S = plain(E), r = D.resetTrackData(S, t, {}); n++;
    assert.equal(r.changed, true); assert.equal(cellsOf(S, t), 0, 'track ' + t + ' has no cells');
    const after = rows(S, { once: true }, 4000);
    assert.ok(after.every(x => x.evs.every(e => e.track !== t || e.type === 'stp')), 'track ' + t + ' plays no note and no command');
    assert.deepEqual(without(after, t), without(base, t), 'track ' + t + ': the other tracks are unchanged (render)');
    assert.deepEqual(without(rows(S, { once: false, live: true }, 400), t), without(live0, t), 'track ' + t + ': the other tracks are unchanged (live loop)');
  }
  assert.ok(n >= 8, 'tracks reset: ' + n);
});

// a small song with every kind of sharing: A owns block 10 (phrases 1, 2); phrase 2 is also in block 11 (track B); block 12 is on A and on the master column; block 13 is off the song and holds phrase 1
function mini() {
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 4, plen: [], plpb: [], tune: new Array(12).fill(0), insts: [{}], parts: [], master: 255, mout: 255, bcol: {}, rcol: {},
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [], phrases: [], mix: Array.from({ length: NT }, () => ({ vol: 224, trim: 128, pan: 128, mute: false, solo: false, sc: false, nm: '', col: 0 })),
    tfx: Array.from({ length: NT }, () => new Array(8).fill(null)), mfx: [], chainOn: new Array(NT).fill(false), smute: new Array(256).fill(0),
    perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo: {} } };
  const P = (n) => { const ph = Array.from({ length: 4 }, () => new Array(12).fill(-1)); ph[0][0] = n; ph[2][0] = n + 2; return ph; };
  for (const [id, n] of [[1, 40], [2, 44], [3, 50], [4, 60]]) { S.phrases[id] = P(n); S.plen[id] = 4; S.plpb[id] = 0; }
  S.chains[10] = [[1, 0], [2, 0]]; S.chains[11] = [[2, 0], [3, 0]]; S.chains[12] = [[4, 0]]; S.chains[13] = [[1, 0]];
  S.song[0][0] = 10; S.song[1][0] = 12; S.song[0][1] = 11; S.song[1][1] = 11; S.song[1][MT] = 12; S.song[2][0] = 10; S.song[2][1] = 11;   // (B plays as long as A: a song's loop is as long as its longest track)
  return S;
}
test('shared blocks and phrases: a block only this track plays is freed; a block or phrase anything else reaches is left exactly as it is; the track unlinks from it', () => {
  const S = mini(), keep = plain({ p2: S.phrases[2], p3: S.phrases[3], p4: S.phrases[4], c11: S.chains[11], c12: S.chains[12], c13: S.chains[13] }), base = rows(S, { once: true }, 400);
  S.bcol[10] = 3; S.bcol[11] = 5; S.plen[1] = 3; S.plpb[1] = 2;
  const r = plain(D.resetTrackData(S, 0, {}));
  assert.deepEqual(r.blocksFreed, [10]); assert.deepEqual(r.blocksShared, [12]);
  assert.equal(S.chains[10], null, 'block 10 freed'); assert.equal(S.bcol[10], undefined, 'its colour goes with it'); assert.equal(S.bcol[11], 5, 'another block keeps its colour');
  assert.deepEqual(r.phrasesShared, [1, 2], 'phrase 2 is in block 11 too, phrase 1 in the unplaced block 13'); assert.deepEqual(r.phrasesFreed, [], 'so none is freed');
  assert.deepEqual(S.phrases[2], keep.p2); assert.deepEqual(S.phrases[3], keep.p3); assert.deepEqual(S.phrases[4], keep.p4); assert.deepEqual(S.chains[11], keep.c11); assert.deepEqual(S.chains[12], keep.c12); assert.deepEqual(S.chains[13], keep.c13);
  assert.ok(S.song.every(row => row[0] === -1), 'track 0 has no cell'); assert.equal(S.song[1][MT], 12, 'the master column still plays block 12'); assert.equal(S.song[0][1], 11);
  const after = rows(S, { once: true }, 400);
  assert.deepEqual(without(after, 0).filter(r => r.evs.length), without(base, 0).filter(r => r.evs.length), 'track B and the master column sound the same');
  assert.ok(after.every(x => x.evs.every(e => e.track !== 0 || e.type !== 'note')), 'track A plays no note');
  // with block 13 gone too, phrase 1 is used by nothing else: it is freed, its length back to the default
  const S2 = mini(); S2.chains[13] = null; S2.plen[1] = 3; S2.plpb[1] = 2; const r2 = plain(D.resetTrackData(S2, 0, {}));
  assert.deepEqual(r2.phrasesFreed, [1]); assert.equal(S2.phrases[1], null); assert.equal(S2.plen[1], S2.dlen); assert.equal(S2.plpb[1], 0); assert.ok(S2.phrases[2], 'the shared phrase stays');
});
test('a phrase one of the freed blocks plays and a block that stays plays too is kept (the same phrase twice in the track\'s own blocks is freed once)', () => {
  const S = mini(); S.chains[13] = null; S.chains[10] = [[1, 0], [1, 0], [2, 0]]; const r = plain(D.resetTrackData(S, 0, {}));
  assert.deepEqual(r.phrasesFreed, [1]); assert.deepEqual(r.phrasesShared, [2]);
});

test('mixer strip, effect slots, CHN, block mutes: as a new song has them; other tracks untouched; a bus keeps CHN (keepChn)', () => {
  const S = mini(); for (const t of [0, 1]) { Object.assign(S.mix[t], { vol: 90 + t, trim: 40, pan: 9, mute: t === 0, solo: t === 1, nm: 'BASS', col: 4, sc: false }); S.tfx[t][0] = { type: 'eq', on: true, bass: 3 }; S.tfx[t][5] = { type: 'comp', on: false }; S.chainOn[t] = true; }
  S.smute[0] = 0b11; S.smute[1] = 0b01; S.mix[0].trim = 40;
  const mix1 = plain(S.mix[1]), fx1 = plain(S.tfx[1]), P = plain(S.chainPatch || null);
  const r = plain(D.resetTrackData(S, 0, {}));
  assert.deepEqual(plain(S.mix[0]), Object.assign({}, fresh(), {}), 'strip = freshMix()'); assert.ok(S.tfx[0].every(f => f === null) && S.tfx[0].length === 8, 'every slot empty, same length');
  assert.equal(S.chainOn[0], false); assert.equal(r.chn, true); assert.equal(r.fx, 2); assert.equal(r.blockMutes, 2); assert.equal(S.smute[0], 0b10); assert.equal(S.smute[1], 0);
  assert.deepEqual(plain(S.mix[1]), mix1); assert.deepEqual(plain(S.tfx[1]), fx1); assert.equal(S.chainOn[1], true); assert.deepEqual(plain(S.chainPatch || null), P);
  const B = mini(); B.chainOn[0] = true; B.mix[0].vol = 7; const rb = plain(D.resetTrackData(B, 0, { keepChn: true }));
  assert.equal(B.chainOn[0], true); assert.equal(rb.chnKept, true); assert.equal(rb.chn, false); assert.deepEqual(plain(B.mix[0]), fresh());
  assert.equal(D.freshMix().vol, 224); assert.equal(D.freshMix().trim, 128);
});

test('PERFORM and parts: the track\'s locks, LFOs and every target that names it go; other tracks\' stay; the …chains patch and other tracks\' sidechain sources are not touched', () => {
  const S = mini();
  S.perf = { sc: [{ n: 'A', v: { 'vol:0': 10, 'pan:0': 20, 'vol:1': 30, 'fx:0:1:2': 4, 'fx:1:1:2': 5, 'vol:32': 9 } }, null], xa: 0, xb: 1, xf: 0,
    mac: [{ n: 'M1', v: 0, tg: [{ id: 'cut:0', lo: 0, hi: 255 }, { id: 'cut:1', lo: 0, hi: 255 }] }],
    lk: { 0: ['vol:0', null, 'pan:1', null], 1: ['vol:0', 'vol:1', null, null], 2: ['vol:0', null, null, null] }, lfo: { 0: [{ id: 'vol:0', sh: 0, rt: 3, dp: 128 }], 1: [{ id: 'vol:1', sh: 0, rt: 3, dp: 128 }, { id: 'pan:0', sh: 1, rt: 2, dp: 90 }] } };
  S.parts = [{ n: 'P', insts: [], mix: [[10, 20], [30, 40]], fx: { '0:1': { type: 'eq' }, '1:1': { type: 'eq' }, '32:0': { type: 'eq' } } }, null];
  S.tfx[1][0] = { type: 'comp', on: true, sck: 9 }; S.chainPatch = { format: 'chains.patch', modules: [{ id: 'a', type: 'trackIn', params: { track: 1 } }], cables: [] };
  const patch = plain(S.chainPatch), sck = plain(S.tfx[1]);
  const r = plain(D.resetTrackData(S, 0, {}));
  assert.deepEqual(plain(S.perf.sc[0].v), { 'vol:1': 30, 'fx:1:1:2': 5, 'vol:32': 9 }); assert.deepEqual(plain(S.perf.mac[0].tg), [{ id: 'cut:1', lo: 0, hi: 255 }]);
  assert.deepEqual(plain(S.perf.lk), { 1: [null, 'vol:1', null, null] }); assert.deepEqual(plain(S.perf.lfo), { 1: [{ id: 'vol:1', sh: 0, rt: 3, dp: 128 }] });
  assert.deepEqual(plain(S.parts[0].mix), [[224, 128], [30, 40]]); assert.deepEqual(plain(S.parts[0].fx), { '1:1': { type: 'eq' }, '32:0': { type: 'eq' } });
  assert.deepEqual(plain(S.chainPatch), patch, 'the patch is not edited'); assert.deepEqual(plain(S.tfx[1]), sck, 'another track\'s sidechain Source (track 1 listens to 0 here) is left as it is');
  assert.equal(r.changed, true);
});

test('a track with nothing on it: nothing changes (changed is false), the song is byte for byte the same; other tracks and out-of-range numbers are not touched', () => {
  const S = mini(), j = JSON.stringify(S), r = plain(D.resetTrackData(S, 5, {}));
  assert.equal(r.changed, false); assert.equal(JSON.stringify(S), j);
  for (const t of [-1, NT, MT, 99, undefined]) { const r2 = plain(D.resetTrackData(S, t, {})); assert.equal(r2.changed, false); }
  assert.equal(JSON.stringify(S), j);
});

// ---------- resetTrack(): the real function, one undo step, the live calls ----------
const undoCode = line('const undo = {past:[]') + '\n' + cut('function snap(){', 'const vUndo = ');
const trkCode = line('const TRK_SCREENS = ');
const liveCode = cut('function resetTrackFocus(){', '/* ---------- mastering on the master bus');
function liveVm(S, o = {}) {
  const log = { toast: [], play: [], fx: [], mix: 0, saves: 0, draws: 0, core: [], chains: 0, perf: 0 };
  const c = { NT, MT, S, console, JSON, Set, Map, Object, Array, Number, queueMicrotask, clearTimeout, TRIM_0: 128, rt: o.rt === undefined ? { pl: { play: e => log.play.push(plain(e)) } } : o.rt, ctx: { currentTime: 3.5 },
    undoHeld: false, undoWant: false, recTake: false, undoQuiet: false, ntr: () => S.ntr || 16, isBusT: t => !!(o.bus && o.bus.includes(t)), busPlan: () => ({ targets: o.feeds || new Map() }), feedsBus: t => !(o.bus && o.bus.includes(t)) && !!(o.feeds && o.feeds.has(t)), focusT: () => c.__focus, d2: n => String(n).padStart(2, '0'),
    keyHint: s => s.replace('mod+', c.MAC ? '\u2318' : 'Ctrl+'), MAC: false, toast: (m, ms) => log.toast.push(m), fxMem: new Map(), clip: null, sxPicks: [], tpLog() {}, view: 'tracker', tv: 'song', apReady: false, apF: null, cs: { song: { x: 0, y: 0 }, mixer: { x: 0, y: 0 } }, cTrack: 0, cFxT: 0,
    mixChanged: () => { log.mix++; c.saveS(); }, fxChanged: t => { log.fx.push(t); c.saveS(); }, chApi: () => null, chainsPush: () => { log.chains++; }, dkChnSig: 'x', perfLive: { init: true }, perfPush: () => { log.perf++; },
    saveS: () => { log.saves++; c.snap(); }, tp: { core: !!o.core }, core: { mir: {}, ready: !!o.core, sT: 7 }, coreOps: x => x, coreDiff: () => (o.core ? [['song', null, 1]] : []), corePost: m => log.core.push(plain(m)), arrSig: 'a', dkFbSig: 'b', draw: () => { log.draws++; } };
  vm.createContext(c);
  vm.runInContext(trkCode + '\n' + freshMixCode + '\n' + undoCode + '\n' + dataCode + '\n' + liveCode + '\nthis.resetTrack = resetTrack; this.resetTrackHere = resetTrackHere; this.resetTrackFocus = resetTrackFocus; this.undo = undo; this.snap = snap;', c);
  c.undo.cur = JSON.stringify(c.S); c.log = log; return c;
}
const settle = () => new Promise(r => setTimeout(r, 0));
function busy() {   // a song with three tracks that play, mixer settings and effects on track 1
  const S = mini(); S.mix[1] = { vol: 90, trim: 40, pan: 9, mute: true, solo: false, sc: false, nm: 'LEAD', col: 3 }; S.tfx[1][0] = { type: 'eq', on: true, bass: 3 }; S.tfx[1][3] = { type: 'comp', on: true }; S.chainOn[1] = true; S.smute[0] = 2;
  S.perf.lfo = { 1: [{ id: 'vol:1', sh: 0, rt: 3, dp: 128 }] }; S.parts = [{ n: 'P', insts: [], mix: [[1, 2], [3, 4]], fx: { '1:0': {} } }]; return S;
}
test('resetTrack: ONE undo step (the song JSON after Undo is identical to before), the toast, the live calls', async () => {
  const S = busy(), c = liveVm(S), before = JSON.stringify(S);
  c.resetTrack(1); await settle();
  assert.equal(c.undo.past.length, 1, 'one undo step for one reset (' + c.log.saves + ' saves)'); assert.ok(c.log.saves >= 2, 'several saves settled into that one step');
  assert.equal(c.undo.past[0].j, before, 'the step holds the song exactly as it was');
  assert.notEqual(JSON.stringify(c.S), before);
  assert.equal(JSON.stringify(Object.assign(JSON.parse(c.undo.past[0].j))), before, 'Undo puts back the same JSON');   // (what undoStep reads: undo.past's text, through normSong)
  assert.deepEqual(c.log.toast, ['Track 02 reset \u00b7 Ctrl+Z brings it back']);
  assert.deepEqual(c.log.fx, [1], 'the effects go through fxChanged (the one teardown path)'); assert.equal(c.log.mix, 1); assert.equal(c.log.chains, 1); assert.equal(c.log.perf, 1);
  assert.deepEqual(c.log.play, [{ type: 'stp', t: 3.5, track: 1 }], 'the player lets the track\'s voices go (STP)');
  assert.deepEqual(c.log.core, [], 'not playing on the audio thread: nothing sent');
  assert.equal(c.dkChnSig, '', 'the bottom panel\'s CHN row is rebuilt');
});
test('resetTrack: a second press on the now-empty track says so and makes no undo step; the bus rule; shared blocks in the toast', async () => {
  const c = liveVm(busy()); c.resetTrack(1); await settle(); c.log.toast.length = 0; const n = c.undo.past.length;
  c.resetTrack(1); await settle(); assert.deepEqual(c.log.toast, ['Track 02 is already empty and at its defaults.']); assert.equal(c.undo.past.length, n);
  const b = busy(); b.chainOn[0] = true; const d = liveVm(b, { bus: [0] }); d.resetTrack(0); assert.equal(d.S.chainOn[0], true); assert.match(d.log.toast[0], /^Track 01 reset \u00b7 Ctrl\+Z brings it back \u00b7 bus: CHN stays on \u00b7 1 shared block kept$/); assert.equal(d.log.chains, 0, 'CHN did not change: no re-route');
  { const f = busy(); const g = liveVm(f, { feeds: new Map([[1, [7]]]) }); g.resetTrack(1); assert.equal(g.S.chainOn[1], true, 'a feeder keeps CHN: its bus role does not change'); assert.match(g.log.toast[0], /CHN kept: it feeds the bus on track 08/); assert.equal(g.log.chains, 0); }
  const e = liveVm(busy()); e.MAC = true; e.resetTrack(1); assert.match(e.log.toast[0], /^Track 02 reset \u00b7 \u2318Z brings it back/);
});
test('resetTrack while the audio-thread sequencer plays: the new song reaches the core first (patch), then the track\'s voices go (stp); the page\'s own player gets STP too', async () => {
  const c = liveVm(busy(), { core: true }); c.resetTrack(1); await settle();
  assert.deepEqual(c.log.core, [{ op: 'patch', ops: [['song', null, 1]] }, { op: 'stp', track: 1 }]); assert.equal(c.core.sT, 0, 'the 100 ms sync timer is cleared, the diff was sent at once');
  assert.equal(c.log.play[0].type, 'stp');
});
test('resetTrack without audio (rt null): the song is still reset, nothing throws', async () => {
  const c = liveVm(busy(), { rt: null }); c.resetTrack(1); await settle(); assert.equal(c.undo.past.length, 1); assert.ok(c.S.song.every(r => r[1] === -1));
});
test('resetTrack: slot memory (fxMem) of that track is dropped, other tracks\' stays; a copied block and picked cells of the track go', async () => {
  const c = liveVm(busy()); c.fxMem.set('1:0:eq', {}); c.fxMem.set('2:0:eq', {}); c.fxMem.set('11:0:eq', {}); c.clip = { v: 'song' }; c.sxPicks = [{ y: 0, t: 1 }, { y: 0, t: 2 }];
  c.resetTrack(1); assert.deepEqual([...c.fxMem.keys()].sort(), ['11:0:eq', '2:0:eq']); assert.equal(c.clip, null); assert.deepEqual(plain(c.sxPicks), [{ y: 0, t: 2 }]);
});

// ---------- which track, on which screen ----------
test('which track: Song and Arrange: the song cursor\'s column; Sect and Phrase: the open track; Mixer: its strip (the MASTER row is none); FX and MOD: the FX track; INST, PROJECT, the master and other views: a toast and nothing happens', async () => {
  const focus = cut('function focusT(){', 'const dkMst = ');
  const c = liveVm(busy()); vm.runInContext(focus + '\nthis.focusT = focusT;', c);   // the real focusT replaces the stand-in
  const at = (v) => Object.assign(c, v), why = () => plain(c.resetTrackFocus());
  at({ tv: 'song' }); c.cs.song.x = 3; assert.deepEqual(why(), { t: 3 });
  c.cs.song.x = MT; assert.match(why().why, /master is not a track/);
  at({ tv: 'phrase', cTrack: 5 }); assert.deepEqual(why(), { t: 5 }); at({ tv: 'sect' }); assert.deepEqual(why(), { t: 5 });
  at({ tv: 'mixer' }); c.cs.mixer.x = 2; c.cs.mixer.y = 1; assert.deepEqual(why(), { t: 2 }); c.cs.mixer.y = 7; assert.match(why().why, /master/);
  at({ tv: 'fx', cFxT: 4 }); assert.deepEqual(why(), { t: 4 }); at({ tv: 'mod', cFxT: NT }); assert.match(why().why, /master/);
  at({ tv: 'inst' }); assert.match(why().why, /No track in focus/); at({ tv: 'project' }); assert.match(why().why, /No track in focus/);
  at({ tv: 'inst', apReady: true, apF: 'arr' }); c.cs.song.x = 6; assert.deepEqual(why(), { t: 6 }, 'Arrange holding the keys is the Song screen');
  at({ view: 'synth', tv: 'song', apReady: false }); assert.match(why().why, /Open a track first/);
  // nothing happens when no track is in focus (the toast is the only effect)
  at({ view: 'tracker', tv: 'inst' }); const j = JSON.stringify(c.S); c.log.toast.length = 0; c.resetTrackHere(); await settle(); assert.equal(JSON.stringify(c.S), j); assert.equal(c.undo.past.length, 0); assert.equal(c.log.toast.length, 1);
  at({ tv: 'phrase', cTrack: 1 }); c.resetTrackHere(); await settle(); assert.equal(c.undo.past.length, 1, 'with a track in focus it acts on it');
  // a hidden track (past the song\'s track count) is no track in focus
  const h = liveVm(busy()); vm.runInContext(focus + '\nthis.focusT = focusT;', h); h.S.ntr = 8; h.tv = 'fx'; h.cFxT = 20; assert.deepEqual(plain(h.resetTrackFocus()), { t: 7 }, 'focusT clamps to the last visible track, as the rest of the panel does');
});

// ---------- the key ----------
const keyLine = line("  if (mod && e.shiftKey && !e.altKey && e.code === 'KeyT')");
test('the key: Cmd+Shift+T on a Mac, Ctrl+Shift+T elsewhere, by position (e.code); plain Ctrl/Cmd+T is still the new phrase; Alt and a missing Shift or modifier do not match', () => {
  const calls = [], c = { resetTrackHere: () => calls.push('reset'), newPhraseRight: () => calls.push('newphrase') };
  vm.createContext(c);
  const fire = e => { calls.length = 0; const mod = !!(e.ctrlKey || e.metaKey); const ev = Object.assign({ ctrlKey: false, metaKey: false, shiftKey: false, altKey: false, repeat: false, code: 'KeyT', key: 't' }, e);
    c.e = ev; c.mod = !!(ev.ctrlKey || ev.metaKey); return vm.runInContext('(function(){ ' + keyLine + '\n if (mod && !e.shiftKey && !e.altKey && e.code === \'KeyT\'){ if (!e.repeat) newPhraseRight(); return true; } return false; })()', c); };
  assert.equal(fire({ metaKey: true, shiftKey: true, key: 'T' }), true); assert.deepEqual(calls, ['reset'], 'Cmd+Shift+T (Mac)');
  assert.equal(fire({ ctrlKey: true, shiftKey: true, key: 'T' }), true); assert.deepEqual(calls, ['reset'], 'Ctrl+Shift+T (Windows, Linux)');
  assert.equal(fire({ ctrlKey: true, shiftKey: true, key: '\u0422', code: 'KeyT' }), true); assert.deepEqual(calls, ['reset'], 'another layout: the key in the T position');
  fire({ ctrlKey: true, shiftKey: true, repeat: true }); assert.deepEqual(calls, [], 'a held key does not repeat the reset');
  fire({ ctrlKey: true }); assert.deepEqual(calls, ['newphrase'], 'Ctrl+T is the new phrase, as before'); fire({ metaKey: true }); assert.deepEqual(calls, ['newphrase']);
  fire({ shiftKey: true }); assert.deepEqual(calls, [], 'Shift+T alone is a note key, not this'); fire({ ctrlKey: true, shiftKey: true, altKey: true }); assert.deepEqual(calls, [], 'Alt+Ctrl+Shift+T is not this');
  fire({ ctrlKey: true, shiftKey: true, code: 'KeyY' }); assert.deepEqual(calls, []);
  assert.equal(fire({ altKey: true }), false); assert.deepEqual(calls, [], 'Alt+T is the new phrase in the line below (not in this harness)');
});
test('the key sits in globalKeys (both input modes, tracker view), after typing() and the dialogs have had the key; nothing else claims Ctrl/Cmd+Shift+T', () => {
  const gk = cut('function globalKeys(e){', '// Ctrl/\u2318+, (TRACKER KEYS');
  assert.ok(gk.includes(keyLine.trim().slice(0, 60)), 'in globalKeys');
  const h = cut("document.addEventListener('keydown', e0 => {\n  const e = noAltGr(e0);", "document.addEventListener('keyup', e => { if (view !== 'tracker'");
  assert.ok(h.indexOf('if (typing(e)) return;') > 0 && h.indexOf('if (typing(e)) return;') < h.indexOf('globalKeys(e)'), 'text fields are left alone before globalKeys');
  assert.ok(h.indexOf('globalKeys(e)') < h.indexOf('keysDown(e'), 'KEYS and PAD mode both get it: globalKeys runs before the mode\'s own keys');
  assert.match(h, /if \(view !== 'tracker'\) return;/, 'the tracker only: the Synth page and the editors do not take it');
  const uses = src.split('\n').filter(l => /e\.code === 'KeyT'/.test(l) && !/^\s*\[\d+, '/.test(l));
  assert.equal(uses.length, 3, 'only Ctrl/Cmd+T, Alt+T and this key use KeyT in a shortcut');
});
test('typing() leaves text fields alone: an input, textarea, select or contenteditable under the key is not a shortcut', () => {
  const code = cut('function typing(e){', '/* ---------- change handling');
  const c = { window: {} }; vm.createContext(c); vm.runInContext(code + '\nthis.typing = typing;', c);
  const el = sel => ({ matches: s => s.split(',').some(p => p.trim().startsWith(sel)), getClientRects: () => [1] });
  for (const t of ['input:not', 'textarea', 'select', '[contenteditable]']) assert.equal(c.typing({ target: el(t) }), true, t);
  assert.equal(c.typing({ target: { matches: () => false, getClientRects: () => [1] } }), false);
  assert.equal(c.typing({ target: { matches: () => true, getClientRects: () => [] } }), false, 'a hidden field is not typing');
});

// ---------- where the command lives, help, build ----------
test('the command is where the other track actions are: Edit menu (Track), the Song block\'s right-click, the phone\'s File menu; Help names the key and the browser-tab caveat', () => {
  assert.match(src, /\{a:'treset', l:'Reset this track \(notes, mixer and effects; undo brings it back\)', k:keyHint\('mod\+shift\+T'\), f:resetTrackHere, off:!TRK_SCREENS\.includes\(tv\)\}/);
  assert.match(src, /\{l:'Reset this track \(notes, mixer and effects; undo brings it back\)', k:ks \? keyHint\('mod\+shift\+T'\) : '', f:\(\) => resetTrack\(t\), off:t === MT\}/);
  assert.match(src, /\{l:'Reset this track \(notes, mixer and effects; undo brings it back\)', f:resetTrackHere, off:view !== 'tracker'\}\]\)/, 'File > Edit group, shown where there is no Edit menu (a phone)');
  assert.match(src, /<dt>Ctrl\/\u2318\+Shift\+T<\/dt><dd>reset the track you're on[^<]*In a browser tab the browser may take this key[^<]*Edit \u25be \u2192 Track \u2192 Reset this track[^<]*File \u2192 Edit \u2192 Reset this track/);
  assert.match(src, /const TRK_SCREENS = \['song', 'sect', 'phrase', 'mixer', 'fx', 'mod'\];/);
});
test('the audio-thread core takes the track\'s voices off on its own message; seq-core.js carries it (generated)', () => {
  const cp = H.read('core/core-processor.js'), sc = H.read('core/seq-core.js');
  assert.match(cp, /case 'stp': \{ const at = Math\.max\(currentTime, d\.t == null \? 0 : d\.t\); if \(d\.track >= 0 && d\.track < NT\)\{ for \(const v of this\.T\(d\.track\)\) this\.kill\(v, at, 0\.004, true\); this\.T\(d\.track\)\.length = 0; \} return; \}/);
  assert.ok(sc.includes("case 'stp': {"), 'in the generated bundle');
  assert.match(src, /corePost\(\{op:'patch', ops\}\); corePost\(\{op:'stp', track:t\}\)/, 'the page sends the song first, then the stop');
});
test('no raw U+2028 / U+2029 and no stray quote in what this build added', () => {
  const mine = [cut('const TRK_SCREENS', '/* ---------- mastering on the master bus'), keyLine].join('\n');
  assert.ok(!/[\u2028\u2029]/.test(src), 'no raw line separators in index.html'); assert.ok(!/[\u2028\u2029]/.test(H.read('core/core-processor.js')));
  assert.ok(!/[\u2018\u201c]/.test(mine.replace(/\/\*[\s\S]*?\*\//g, '').replace(/\/\/[^\n]*/g, '')), 'no curly opening quotes in code');
});

test('the SOLE feeder of a bus that holds notes: reset keeps its CHN, so the bus stays a bus and every other track plays the same (it failed when CHN went off)', () => {
  const E = ELECTRO(), P = E.chainPatch, gone = new Set(P.modules.filter(m => m.type === 'trackIn' && [2, 3, 7].includes(m.params.track)).map(m => m.id));
  P.modules = P.modules.filter(m => !gone.has(m.id)); P.cables = P.cables.filter(c => !gone.has(c.from[0]) && !gone.has(c.to[0]));
  for (const t of [1, 2, 6]) E.chainOn[t] = false;
  seqCtx.__S = plain(E); vm.runInContext('S = __S; busPlanH = null; __P = busPlan();', seqCtx);
  const feeds = plain([...seqCtx.__P.feeds]), targets = new Map(plain([...seqCtx.__P.targets]));
  assert.deepEqual(feeds, [[3, [0]]], 'bus 3 has one feeder, track 0'); assert.ok(cellsOf(E, 3) > 0, 'the bus holds notes');
  const before = rows(E, { once: true }, 4000), live0 = rows(E, { once: false, live: true }, 400);
  const c = liveVm(plain(E), { bus: [3], feeds: targets, core: true }); c.resetTrack(0);
  assert.equal(c.S.chainOn[0], true, 'CHN stays on'); assert.equal(cellsOf(c.S, 0), 0);
  assert.deepEqual(without(rows(plain(c.S), { once: true }, 4000), 0), without(before, 0), 'other tracks, render');
  assert.deepEqual(without(rows(plain(c.S), { once: false, live: true }, 400), 0), without(live0, 0), 'other tracks, live loop');
  assert.ok(rows(plain(c.S), { once: true }, 4000).every(r => r.evs.every(e => e.track !== 0)), 'track 0 is silent');
});

// ---------- in a real page (opt-in) ----------
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const opt = { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 170000 };
test('in a real page: the key resets the track under the cursor, one Ctrl+Z gives the identical song and the same events; other tracks play the same, stopped and playing on the page and on the audio-thread sequencer', opt, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app } = await import('./sweep/ui-lib.mjs');
  const B = await boot({ width: 1440, height: 900 });
  try {
    const text = fs.readFileSync(require('path').join(__dirname, '..', 'sounds/ELECTRO.thunder'), 'utf8');
    await B.page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), text);
    const run = () => B.page.evaluate(() => JSON.stringify(window.__seqTest.run('song', 4000))), sig = () => app(B.page, 'JSON.stringify(S)');
    for (const T of [0, 3, 9]) {   // 9 shares a block with the master column; 3 is a bus
      await app(B.page, `(() => { go('song'); cs.song.x = ${T}; cs.song.y = 0; draw(); undoReset(); return 1; })()`);
      const S0 = await sig(), E0 = JSON.parse(await run());
      await B.page.keyboard.press('Control+Shift+T'); await B.page.waitForTimeout(150);
      const S1 = await sig(), E1 = JSON.parse(await run());
      assert.notEqual(S0, S1, 'track ' + T + ' changed'); assert.ok(E1.every(r => r.evs.every(e => e.track !== T)), 'track ' + T + ' is silent');
      const o = e => e.track !== T, strip = E => JSON.stringify(E.map(r => ({ t: r.t, evs: r.evs.filter(o) })));
      assert.equal(strip(E1), strip(E0), 'the other tracks are unchanged');
      await B.page.keyboard.press('Control+z'); await B.page.waitForTimeout(150);
      assert.equal(await sig(), S0, 'Undo: the song JSON is identical'); assert.equal(JSON.stringify(JSON.parse(await run())), JSON.stringify(E0), 'and it plays the same');
    }
    for (const core of [false, true]) {   // while playing
      await B.page.evaluate(c => window.__seqTest.coreOn(c), core);
      await app(B.page, `(() => { go('song'); cs.song.x = 5; cs.song.y = 0; draw(); undoReset(); tStart('song', {row:0}); return 1; })()`); await B.page.waitForTimeout(2200);
      await B.page.keyboard.press('Control+Shift+T'); await B.page.waitForTimeout(400);
      assert.equal(await app(B.page, 'S.song.filter(r => r[5] >= 0).length'), 0);
      await B.page.waitForTimeout(0);   // (the level of the reset track within 300 ms of the key: a hung voice would still sound)
      const lv = await app(B.page, `(() => { const a = rt && rt.pl && rt.pl.buses && rt.pl.buses[5] && rt.pl.buses[5].an; if (!a) return -1; const b = new Float32Array(a.fftSize); a.getFloatTimeDomainData(b); let m = 0; for (const x of b) m = Math.max(m, Math.abs(x)); return m; })()`);
      assert.ok(lv === -1 || lv < 1e-4, 'track 6 is silent after the reset (level ' + lv + ')');
      if (core) { const sg = await B.page.evaluate(() => window.__seqTest.coreSig()); assert.equal(sg.same, true, 'the core has the song as the page has it'); }
      await B.page.keyboard.press('Control+z'); await B.page.waitForTimeout(300);
      assert.equal(await app(B.page, 'S.song.filter(r => r[5] >= 0).length'), 56, 'Undo brings the cells back while playing'); await app(B.page, 'tStop(); 1');
    }
    const errs = B.errors.filter(e => !/MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource/i.test(e)); assert.deepEqual(errs, []);
  } finally { await B.close(); }
});
