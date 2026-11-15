// MONO / POLY COUNTS, stage 1 (14 Nov; docs/MONO-POLY-PLAN.md): S.nmono / S.hmono, ntrPlan / ntrPerm, trackHolds, tracksHiddenBy, setTracks, setNtr and normSplit,
// cut out of index.html and run in a vm with stand-ins for the rest of the app (as perf-follows-tracks.test.js does).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const eq = (a, b, m) => assert.equal(JSON.stringify(a), JSON.stringify(b), m);   // the vm's arrays are from another realm
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };
const line = a => { const i = src.indexOf(a); assert.ok(i >= 0, 'moved: ' + a); return src.slice(i, src.indexOf('\n', i)); };
const code = cut('function ntrPermOld(', '// The window of tracks a Song or Mixer line has room for');
const splitCode = cut('function normSplit(', 'function normSong(');
// the old ntrPerm, as it was before this feature (a golden copy: ntrPermOld must stay equal to it)
const OLD = `
function ntrPerm(n0, n1){
  const m0 = Math.ceil(n0 / 2), m1 = Math.ceil(n1 / 2), p1 = n1 - m1, mono = [], poly = [], spare = [];
  for (let t = 0; t < m0; t++) (t < m1 ? mono : spare).push(t);
  for (let t = m0; t < n0; t++) (t - m0 < p1 ? poly : spare).push(t);
  const hid = []; for (let t = n0; t < NT; t++) hid.push(t);
  const free = spare.length ? hid.concat() : hid; let k = 0; const take = () => free[k++];
  while (mono.length < m1) mono.push(take());
  while (poly.length < p1) poly.push(take());
  const P = mono.concat(poly, spare, free.slice(k));   // P[new] = old
  return P.length === NT ? P : null;
}
`;
const NT = 32;
function rig(o = {}) {
  const c = { NT, MT: NT, SCK_SEND: 8, NPAD: 8, TRIM_0: 128, vtrk: !!o.vtrk, kit: o.vtrk ? { voices: [] } : null, perfLive: { sent: new Map(), pno: new Map() },
    clamp: (v, a, b) => Math.max(a, Math.min(b, v)), d2: n => String(n).padStart(2, '0'), freshMix: () => ({ vol: 224, trim: 128, pan: 128, mute: false, solo: false, sc: false, nm: '', col: 0 }),
    sckTrack: v => { v = Math.round(+v) || 0; return v > 8 ? v - 9 : -1; }, cTrack: 0, cs: { song: { x: 0 }, mixer: { x: 0 } }, cFxT: 0, tp: {}, busPlanH: 1, rt: null, CH: {}, chainsDropFrame() {}, PumpFX: { _notifyAll() {} },
    dirty: [], cache: [], arrSig: 'x', draw() {}, growKit() {}, saves: [], voiceToJSON: v => ({ name: v.name, x: v.x }),
    trackVoice: t => ({ name: 'T' + t, x: t >= c.polyFrom() ? 'poly' : 'mono' }), trackVoiceOf: (t, p) => ({ name: 'T' + t, x: p ? 'poly' : 'mono' }), saveS() { c.saves.push(JSON.stringify(c.S)); } };
  vm.createContext(c);
  vm.runInContext(line('const ntr = () => S.ntr') + '\n' + line('const polyFrom = ') + '\n' + code + splitCode + '\nthis.ntrPerm = ntrPerm; this.ntrPlan = ntrPlan; this.ntrPermOld = ntrPermOld; this.ntrRemap = ntrRemap; this.setTracks = setTracks; this.setNtr = setNtr; this.trackHolds = trackHolds; this.tracksHiddenBy = tracksHiddenBy; this.tracksCheck = tracksCheck; this.normSplit = normSplit; this.polyFrom = polyFrom;', c);
  const S = { ntr: o.ntr || 16, song: [], smute: [], tfx: Array.from({ length: NT }, () => []), mfx: [], chainOn: new Array(NT).fill(false), chains: [], phrases: [], chainPatch: null, perf: null, parts: [],
    mix: Array.from({ length: NT }, () => c.freshMix()) };
  // every slot t: a song block on row 0 holding a chain with one phrase whose first note is 40 + t, and a mixer volume of its own, unless o.empty names it
  const empty = new Set(o.empty || []);
  for (let y = 0; y < 3; y++) S.song.push(new Array(NT + 1).fill(-1));
  for (let t = 0; t < NT; t++) { if (empty.has(t)) continue; S.song[0][t] = t; S.chains[t] = [[t]]; S.phrases[t] = [[40 + t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1]]; S.mix[t].vol = 100 + t; }
  c.S = S; if (o.nmono) S.nmono = o.nmono; if (o.hmono != null) S.hmono = o.hmono;
  return c;
}
const noteAt = (c, t) => { const cid = c.S.song[0][t]; return cid >= 0 ? c.S.phrases[c.S.chains[cid][0][0]][0][0] : -1; };
const notes = (c, a, b) => { const r = []; for (let t = a; t < b; t++) r.push(noteAt(c, t)); return r; };
const set = (c, mono, poly) => { const r = c.setTracks(mono, poly); assert.equal(r.why, '', 'refused: ' + r.why); return r; };

test('without nmono / hmono: ntrPerm is the old permutation for every n0 -> n1 in 8..24, and the old function is untouched', () => {
  const c = rig(); vm.runInContext(OLD.replace('function ntrPerm(', 'function ntrPermRef(') + '\nthis.ntrPermRef = ntrPermRef;', c);
  let n = 0;
  for (let a = 8; a <= 24; a++) for (let b = 8; b <= 24; b++) {
    const ref = c.ntrPermRef(a, b), half = x => Math.ceil(x / 2);
    eq(c.ntrPermOld(a, b), ref, 'ntrPermOld ' + a + '>' + b); eq(c.ntrPerm(a, half(a), b, half(b)), ref, 'ntrPerm ' + a + '>' + b); eq(c.ntrPerm(a, half(a), b, half(b), undefined, () => true), ref, 'with holds'); n++;
  }
  assert.equal(n, 17 * 17);
});
test('without nmono / hmono: setNtr gives the old song and the same bytes, and writes neither field', () => {
  for (const [a, b] of [[16, 17], [16, 15], [15, 24], [24, 8], [9, 16], [16, 16], [8, 32]]) {
    const n = rig({ ntr: a }), o = rig({ ntr: a });
    n.setNtr(b); const P = o.ntrPermOld(a, Math.max(8, b)); o.ntrRemap(P); o.S.ntr = Math.max(8, b);
    assert.equal(JSON.stringify(n.S), JSON.stringify(o.S), a + '>' + b);
    assert.ok(!('nmono' in n.S) && !('hmono' in n.S), 'no new fields ' + a + '>' + b);
  }
});
test('legacy 16 -> 15 -> 16: the same track comes back to the same place, as the old code did it', () => {
  const o = rig(), n = rig();
  o.ntrRemap(o.ntrPermOld(16, 15)); o.S.ntr = 15; o.ntrRemap(o.ntrPermOld(15, 16)); o.S.ntr = 16;
  n.setNtr(15); assert.equal(n.S.ntr, 15); eq(notes(n, 0, 15), [40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54], 'the visible 15');
  n.setNtr(16);
  assert.equal(JSON.stringify(n.S), JSON.stringify(o.S)); eq(notes(n, 0, 16), Array.from({ length: 16 }, (_, i) => 40 + i), 'every track back in its place');
  assert.equal(noteAt(n, 15), 55, 'the poly track the old code parked first is the one that returns');
  // and from the other side: the legacy parked kinds read the parked slots one growth step at a time
  const k = n.ntrPlan(15, 8, 16, 8); eq(k.P.slice(0, 16), [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15]);
});
test('uneven counts round trip: 8/8 -> mono 6 -> poly 6 -> mono 8 -> poly 8 gives every track back, with its own notes, in its old place', () => {
  const c = rig(), all = Array.from({ length: 16 }, (_, i) => 40 + i);
  set(c, 6, 8); assert.equal(c.S.ntr, 14); assert.equal(c.S.nmono, 6); assert.equal(c.polyFrom(), 6);
  eq(notes(c, 0, 14), [40, 41, 42, 43, 44, 45, 48, 49, 50, 51, 52, 53, 54, 55], 'mono 7 and 8 are hidden; the poly tracks moved down');
  set(c, 6, 6); assert.equal(c.S.ntr, 12); eq(notes(c, 0, 12), [40, 41, 42, 43, 44, 45, 48, 49, 50, 51, 52, 53]);
  set(c, 8, 6); assert.equal(c.S.ntr, 14); eq(notes(c, 0, 14), [40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53], 'the hidden MONO tracks come back, not poly ones');
  set(c, 8, 8); assert.equal(c.S.ntr, 16); eq(notes(c, 0, 16), all); assert.ok(!('nmono' in c.S), 'back on the half rule: no nmono');
  for (let t = 0; t < 16; t++) assert.equal(c.S.mix[t].vol, 100 + t, 'mixer of ' + t);
  // the hidden tracks are never the wrong kind: poly 8 -> 5 then mono 8 -> 11 takes mono ones only if any are hidden, else empties
  const d = rig(); set(d, 8, 5); set(d, 8, 8); eq(notes(d, 0, 16), all);
});
test('shrinking parks the LAST tracks of its kind in front of that kind\'s hidden run; growing takes the most recent first', () => {
  const c = rig(); set(c, 6, 8); const h = c.S.hmono; assert.ok(h >= 2);
  set(c, 7, 8); eq(notes(c, 0, 15), [40, 41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 53, 54, 55], 'track 7 (the one hidden last of the two) is back first');
  set(c, 4, 8); eq(notes(c, 0, 12), [40, 41, 42, 43, 48, 49, 50, 51, 52, 53, 54, 55]); set(c, 7, 8); eq(notes(c, 0, 15), [40, 41, 42, 43, 44, 45, 46, 48, 49, 50, 51, 52, 53, 54, 55]);
});
test('a mono-count change moves song rows, mixer, effects, side-chain picks, chains and Perform with their tracks', () => {
  const c = rig(), S = c.S;
  S.tfx[2] = [{ type: 'delay', sck: 9 + 9 }, null];   // a side-chain pick of track 9
  S.tfx[10] = [{ type: 'comp', sck: 9 + 3 }];         // track 3
  S.chainOn[9] = true; S.chainPatch = { modules: [{ type: 'trackIn', params: { track: 10 } }, { type: 'chainOut', params: { track: 12 } }] };
  S.perf = { sc: [{ n: 'A', v: { 'vol:9': 10, 'fx:3:0:0': 20, 'vol:32': 5 } }], mac: [{ n: 'M', v: 0, tg: [{ id: 'pan:11', lo: 0, hi: 255 }] }], lk: { 9: ['vol:9', null, null, null] }, lfo: { 9: [{ id: 'fx:9:0:0', sh: 0, rt: 3, dp: 9 }], 32: [{ id: 'vol:32' }] } };
  S.parts = [{ n: 'P', insts: [], mix: Array.from({ length: 32 }, (_, t) => [t, 200]), fx: { '9:0': { type: 'x' } } }];
  const r = set(c, 6, 8), tN = r.tN;
  assert.equal(tN(9), 7); assert.equal(tN(3), 3); assert.equal(tN(7), 15, 'track 8 (mono) is parked: the front of the hidden mono run is track 7 at 14');
  for (let t = 0; t < 32; t++) assert.equal(S.mix[tN(t)].vol, 100 + t, 'mix ' + t);
  assert.equal(noteAt(c, 7), 49); assert.equal(S.chainOn[7], true); assert.equal(S.chainOn[9], false);
  assert.equal(S.tfx[2][0].sck, 9 + 7, 'the side-chain pick of track 9 now names track 7 (Self / Off / send codes stay)');
  assert.equal(S.tfx[tN(10)][0].sck, 9 + 3);
  eq(S.chainPatch.modules.map(m => m.params.track), [8, 10]);
  assert.equal(S.perf.sc[0].v['vol:7'], 10); assert.equal(S.perf.sc[0].v['fx:3:0:0'], 20); assert.equal(S.perf.sc[0].v['vol:32'], 5);
  assert.equal(S.perf.mac[0].tg[0].id, 'pan:9'); eq(S.perf.lk[7], ['vol:7', null, null, null]); assert.equal(S.perf.lfo[7][0].id, 'fx:7:0:0'); assert.equal(S.perf.lfo[32].length, 1);
  assert.equal(S.parts[0].mix[7][0], 9); assert.ok(S.parts[0].fx['7:0']);
  assert.equal(c.cTrack, 0);
});
test('the limits: at least one mono, one poly, 8 in all, 24 / 32 at most; nothing changes when refused', () => {
  const c = rig(), before = JSON.stringify(c.S);
  for (const [m, p, w] of [[0, 16, /at least one/], [16, 0, /at least one/], [3, 4, /8 to 32/], [20, 13, /8 to 32/], [-1, 9, /at least one/], [NaN, 9, /at least one/]]) { const r = c.setTracks(m, p); assert.match(r.why, w, m + '/' + p); }
  assert.equal(JSON.stringify(c.S), before); assert.equal(c.saves.length, 0, 'no save, no undo step');
  const v = rig({ vtrk: true }); assert.match(v.setTracks(12, 13).why, /8 to 24/); assert.equal(v.setTracks(12, 12).why, '');
  // the total-only entry point clamps instead, and an uneven song keeps its MONO count (poly changes), never a kind with none
  const u = rig({ ntr: 16, nmono: 12 }); u.setNtr(8); assert.equal(u.S.ntr, 8); assert.equal(u.S.nmono, 7); assert.equal(u.polyFrom(), 7);
  const w = rig({ ntr: 16, nmono: 3 }); w.setNtr(24); assert.equal(w.S.ntr, 24); assert.equal(w.S.nmono, 3); w.setNtr(8); assert.equal(w.S.nmono, 3);
  const x = rig(); x.setNtr(5); assert.equal(x.S.ntr, 8); x.setNtr(99); assert.equal(x.S.ntr, 32);
  // the reader clamps whatever is stored
  const y = rig({ ntr: 16 }); y.S.nmono = 99; assert.equal(y.polyFrom(), 15); y.S.nmono = 0; assert.equal(y.polyFrom(), 8); y.S.nmono = 1; assert.equal(y.polyFrom(), 1);
});
test('may it grow: a count may not take a hidden track of the other kind that holds something; an empty one is overwritten', () => {
  const spare = Array.from({ length: 16 }, (_, i) => 16 + i), mk = emptySlot => { const c = rig({ empty: spare.concat(emptySlot == null ? [] : [emptySlot]) }); set(c, 2, 8); set(c, 2, 24); return c; };   // 6 hidden MONO tracks (3..8 -> parked), poly run used up
  const c = mk(null); assert.equal(c.S.ntr, 26); const was = JSON.stringify(c.S), nSaves = c.saves.length;
  const r = c.setTracks(2, 25); assert.match(r.why, /hidden mono tracks? \(.*\) still holds? something, so POLY cannot grow/); assert.equal(JSON.stringify(c.S), was, 'nothing changed'); assert.equal(c.saves.length, nSaves);
  assert.equal(c.setTracks(3, 24).why, '', 'MONO may take its own back');
  // one hidden mono track is empty (its slot 6, mono 7): POLY 25 takes that one, and nothing else of mono moves
  const d = mk(6); d.S.mix[6].vol = 224; const hid0 = d.S.ntr; set(d, 2, 25); assert.equal(d.S.ntr, hid0 + 1);
  assert.equal(noteAt(d, d.S.ntr - 1), -1, 'the new poly track is the empty one'); const others = []; for (let t = d.S.ntr; t < 32; t++) others.push(noteAt(d, t)); assert.ok(others.slice(0, 5).every(v => v >= 40), 'the other hidden mono tracks still hold their notes: ' + others);
  // the other way round: mono growth against hidden poly tracks that hold something
  const e = rig({ empty: spare }); set(e, 8, 2); set(e, 24, 2); assert.ok(/^1 hidden poly track|^\d+ hidden poly tracks? /.test(e.setTracks(25, 2).why), 'mono cannot take poly');
  // a hidden track that another track points at is not empty
  const f = rig({ empty: [6, 7] }); f.S.tfx[0] = [{ type: 'delay', sck: 9 + 7 }]; assert.equal(f.trackHolds(f.S, 7), true); assert.equal(f.trackHolds(f.S, 6), false);
});
test('trackHolds: notes through chains, mixer, effects, a chain, Perform data, parts, references from other tracks, a voice of its own', () => {
  const e = () => rig({ empty: [5] }), h = (c, t = 5) => c.trackHolds(c.S, t);
  assert.equal(h(e()), false, 'an empty track');
  let c = e(); c.S.song[1][5] = 5; c.S.chains[5] = [[5]]; c.S.phrases[5] = [[-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 3]]; assert.equal(h(c), true, 'a command counts as something in the phrase');
  c = e(); c.S.song[1][5] = 9; c.S.chains[9] = [[-1]]; assert.equal(h(c), false, 'a block with no phrase in it holds nothing');
  c = e(); c.S.mix[5].pan = 100; assert.equal(h(c), true, 'mixer'); c = e(); c.S.mix[5].mute = true; assert.equal(h(c), true);
  c = e(); c.S.tfx[5][1] = { type: 'delay' }; assert.equal(h(c), true, 'effects'); c = e(); c.S.chainOn[5] = true; assert.equal(h(c), true, 'chain');
  c = e(); c.S.perf = { sc: [], mac: [{ tg: [{ id: 'cut:5' }] }], lk: {}, lfo: {} }; assert.equal(h(c), true, 'macro target');
  c = e(); c.S.perf = { sc: [{ v: { 'fx:5:0:1': 3 } }], mac: [], lk: {}, lfo: {} }; assert.equal(h(c), true, 'scene');
  c = e(); c.S.perf = { sc: [], mac: [], lk: { 5: [null, 'vol:2', null, null] }, lfo: {} }; assert.equal(h(c), true, 'lock');
  c = e(); c.S.perf = { sc: [], mac: [], lk: {}, lfo: { 2: [{ id: 'vol:5' }] } }; assert.equal(h(c), true, 'LFO of another track aimed at it');
  c = e(); c.S.perf = { sc: [], mac: [], lk: {}, lfo: { 32: [{ id: 'vol:32' }] } }; assert.equal(h(c), false, 'the master is not a track');
  c = e(); c.S.parts = [{ mix: [], fx: { '5:0': {} } }]; assert.equal(h(c), true, 'a part with its effect'); c = e(); c.S.parts = [{ mix: Object.assign([], { 5: [224, 128] }), fx: {} }]; assert.equal(h(c), false, 'a part with the stock mix');
  c = e(); c.S.tfx[1] = [{ sck: 9 + 5 }]; assert.equal(h(c), true, 'side-chain source'); c = e(); c.S.mfx = [{ sck: 9 + 5 }]; assert.equal(h(c), true);
  c = e(); c.S.chainPatch = { modules: [{ type: 'chainOut', params: { track: 6 } }] }; assert.equal(h(c), true, 'bus feed / chain out'); c = e(); c.S.chainPatch = { modules: [{ type: 'scin', params: { src: 6 } }] }; assert.equal(h(c), true);
  c = e(); c.S.chainPatch = { modules: [{ type: 'trackIn', params: { track: 7 } }] }; assert.equal(h(c), false, 'another track\'s Track In');
  const v = rig({ vtrk: true, empty: [5] }); v.kit.voices[8 + 5] = { name: 'Drums T06', x: 'mono' }; assert.equal(h(v), false, 'the stock voice'); v.kit.voices[8 + 5] = { name: 'mine', x: 'mine' }; assert.equal(h(v), true, 'a voice of its own');
});
test('tracksHiddenBy: the visible tracks that would be hidden AND hold something', () => {
  const c = rig({ empty: [6, 12, 15] });
  eq(c.tracksHiddenBy(8, 8), []); eq(c.tracksHiddenBy(6, 8), [7], 'mono 7 and 8: only 8 holds notes (track index 7)'); eq(c.tracksHiddenBy(8, 6), [14]);
  eq(c.tracksHiddenBy(5, 5), [5, 7, 13, 14].filter(t => t !== 6 && t !== 12 && t !== 15), 'both at once'); eq(c.tracksHiddenBy(9, 9), []);
  c.S.mix[6].vol = 10; eq(c.tracksHiddenBy(6, 8), [6, 7], 'a mixer setting alone is something');
});
test('save and load: nmono only when it differs from the half rule; hmono clamped; both survive a JSON round trip', () => {
  const c = rig(), a = JSON.stringify(c.S); set(c, 8, 8); assert.equal(JSON.stringify(c.S), a, 'an untouched split saves nothing new');
  set(c, 5, 9); const j = JSON.parse(JSON.stringify(c.S)); assert.equal(j.nmono, 5); assert.equal(j.ntr, 14); assert.ok(j.hmono >= 0 && j.hmono <= 18);
  const l = rig({ ntr: 99 }), L = { ntr: 14 }; l.normSplit(j, Object.assign(L, { ntr: 14 })); assert.equal(L.nmono, 5); assert.equal(L.hmono, j.hmono);
  const t = (j, n) => { const S = { ntr: n }; c.normSplit(j, S); return S; };
  eq(t({}, 16), { ntr: 16 }, 'old song: neither'); eq(t({ nmono: 8 }, 16), { ntr: 16 }, 'the half rule is not stored'); eq(t({ nmono: 9 }, 17), { ntr: 17 });
  assert.equal(t({ nmono: 0 }, 16).nmono, 1); assert.equal(t({ nmono: 99 }, 16).nmono, 15); assert.equal(t({ nmono: -4 }, 16).nmono, 1); assert.equal(t({ nmono: 'x' }, 16).nmono, undefined); assert.equal(t({ nmono: null }, 16).nmono, undefined);
  assert.equal(t({ hmono: 99 }, 16).hmono, 16); assert.equal(t({ hmono: -3 }, 16).hmono, 0); assert.equal(t({ hmono: 3 }, 16).hmono, 3); assert.equal(t({ hmono: 'x' }, 16).hmono, undefined);
  assert.equal(t({ nmono: 3 }, 8).nmono, 3); assert.equal(t({ nmono: 8 }, 8).nmono, 7);
  assert.match(src, /S\.ntr = clamp\(int\(j\.ntr, 16\), 8, vtrk \? 24 : NT\);[^\n]*\n\s*normSplit\(j, S\);/, 'the loader calls it right after ntr is clamped');
  // a loaded uneven song goes on from where it was saved: the same counts and the same hidden kinds
  const r = rig({ nmono: 5, hmono: j.hmono, ntr: 14 }); Object.assign(r.S, { }); assert.equal(r.polyFrom(), 5);
});
test('undo: one saveS per change, and the snapshot before it restores the counts', () => {
  const c = rig(); set(c, 5, 9); assert.equal(c.saves.length, 1, 'one undo step'); const after = JSON.parse(JSON.stringify(c.S));
  const before = JSON.parse(c.saves[0]); assert.equal(before.nmono, 5, 'saveS runs after the change: the snapshot holds the new counts');
  set(c, 6, 4); assert.equal(c.saves.length, 2);
  const undone = JSON.parse(c.saves[0]), S = { ntr: undone.ntr }; c.normSplit(undone, S); assert.equal(S.nmono, 5); assert.equal(S.ntr, after.ntr); assert.equal(S.hmono, after.hmono);
  const o = rig(); o.setNtr(17); assert.equal(o.saves.length, 1);
  assert.match(src, /function snap\(\)\{[\s\S]*?JSON\.stringify\(S\)/); assert.match(src, /undo\.busy = true; const n = normSong\(JSON\.parse\(undo\.cur\)\)/);
});
test('setNtr on an uneven song keeps MONO and changes POLY (MORE TRACKS grows one of each kind through setTracks)', () => {
  const c = rig({ ntr: 16, nmono: 4 }); c.setNtr(18); assert.equal(c.S.ntr, 18); assert.equal(c.S.nmono, 4); assert.equal(c.polyFrom(), 4);
  assert.match(src, /setTracks\(PF \+ 1, n0 - PF \+ 1\)/, 'dupTrack grows one mono and one poly');
});
