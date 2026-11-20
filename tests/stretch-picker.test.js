// STRETCH PICKER (build 659, docs/STRETCH-DESIGN.md steps 1; docs/STRETCH-BUILD-NOTES.md): one grouped list of modes, I.sx, the REPITCH sentence.
// No audio change: everything below that is old behaviour is compared with what build 658 (origin/beta 1dfbf7ad) did, recorded in
// tests/fixtures/stretch-before-659.json by tests/stretch-record.js.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const path = require('path'), H = require('./helpers'), S = require('./sampler-harness'), R = require('./stretch-record');
const src = H.read('index.html'), before = JSON.parse(H.read('tests/fixtures/stretch-before-659.json')), root = path.join(__dirname, '..');
const J = x => JSON.parse(JSON.stringify(x));   // (values made inside the vm are of another realm)
const E = S.loadSampler(src, 'stretchData, stKey, SY_PICK, SY_GROUPS, syPick, syPut, sxLoad, syList, syRepitchText,');

test('old songs: the stretched audio is bit for bit as in build 658 (STRETCH and LOOP x GRAIN 0..4 x 3 tempos x 3 lengths: 90 renders)', () => {
  const now = R.record(src);
  assert.equal(before.stretch.length, 90);
  assert.deepEqual(now.stretch, before.stretch);
});

test('old songs: every note event and cache key applyChop writes is as in build 658 (sync 0..3 x GRAIN 0..4 and absent x slices x lines x tempo x LPB x note: 768 cases)', () => {
  const now = R.record(src);
  assert.equal(before.chop.length, 768);
  assert.deepEqual(now.chop, before.chop);
  assert.ok(before.chop.some(c => c[11] && c[11][4].startsWith(':3:')), 'the cases include stretched notes with their keys');
});

test('old songs: loading and saving the instruments is byte for byte as in build 658 (the two shipped songs, 128 instruments, and a constructed song: sync 0..3 x grain 0..4 x lines)', () => {
  const now = R.recordLoads(src, root);
  assert.deepEqual(Object.keys(now).sort(), Object.keys(before.load).sort());
  assert.equal(now.ELECTRO.length, 64); assert.equal(now['BETA-TESTING-ELECTRO'].length, 64); assert.equal(now.constructed.length, 40);
  for (const k in now) assert.deepEqual(now[k], before.load[k], k);
  for (const k in now) for (const j of now[k]) assert.ok(!('sx' in JSON.parse(j)), 'an old song grows no sx field');
  // the constructed song really covers every pair
  const seen = new Set(R.songInsts(root).constructed.map(o => o.sync + ':' + o.grain)); assert.equal(seen.size, 20);
});

test('the picker lists only modes that exist, in groups, and each is a (sync, sx) pair', () => {
  assert.deepEqual(J(E.SY_PICK.map(m => m.n)), ['OFF', 'REPITCH', 'GRANULAR', 'LOOP']);
  assert.deepEqual(J(E.SY_PICK.map(m => [m.sync, m.sx])), [[0, 0], [1, 0], [2, 0], [3, 0]]);
  const { L, map } = E.syList();
  assert.deepEqual(J(L), ['OFF', { head: 'Repitch' }, 'REPITCH', { head: 'Character' }, 'GRANULAR', 'LOOP']);
  assert.deepEqual(J(map), [0, 1, 2, 3]);
  assert.ok(!L.some(e => e && e.head === 'Clean' || e && e.head === 'Drums'), 'no empty groups, no entries for modes that do not exist yet');
  for (const m of E.SY_PICK) assert.ok(m.tip && (m.grp === '' || E.SY_GROUPS.includes(m.grp)));
});

test('syPut / syPick: a classic mode writes no I.sx; every picker index reads back; an unknown pair reads as the classic mode of its sync', () => {
  for (let p = 0; p < E.SY_PICK.length; p++) { const I = { sync: 0, beats: 4, grain: 1 }; E.syPut(I, p); assert.equal(E.syPick(I), p); assert.equal(I.sync, E.SY_PICK[p].sync); assert.ok(!('sx' in I)); }
  const I = { sync: 3, sx: 7 }; assert.equal(E.syPick(I), 3, 'sync 3 with an sx this build does not know is LOOP');
  const J = { sync: 2, sx: 1 }; assert.equal(E.syPick(J), 2);
  const K = { sync: 3, sx: 1 }; E.syPut(K, 2); assert.ok(!('sx' in K), 'changing to a classic mode drops a stale sx');
});

test('sxLoad: a song with I.sx loads on a build that has no such mode as the classic mode of its sync (nearest classic), and the loader of build 658 simply ignores the field', () => {
  const songs = [{ pad: 3, sync: 3, sx: 1, beats: 4, grain: 2 }, { pad: 3, sync: 2, sx: 2, beats: 4, grain: 2 }, { pad: 3, sync: 1, sx: 5 }];
  const now = R.loadInsts(src, songs).map(JSON.parse);
  for (const o of now) assert.ok(!('sx' in o), 'this build has no such mode: the field is not kept');
  assert.deepEqual(now.map(o => o.sync), [3, 2, 1]);
  // "a loader that ignores the field" = the same loader lines without the sxLoad call = build 658's
  const noSx = R.loadInsts(src.replace(/\n\s*sxLoad\(o, I\);[^\n]*/, ''), songs).map(JSON.parse);
  assert.notEqual(src.length, src.replace(/\n\s*sxLoad\(o, I\);[^\n]*/, '').length, 'the call is there to remove');
  assert.deepEqual(noSx.map(o => o.sync), [3, 2, 1]); for (const o of noSx) assert.ok(!('sx' in o));
  assert.deepEqual(noSx, now, 'with no mode defined the two loaders agree');
});

test('REPITCH sentence: the numbers come from the real sample length', () => {
  const t = 8 * 60 / 140;   // 8 beats at 140 BPM = 3.4286 s
  assert.equal(E.syRepitchText(0.823 * t, t), 'plays 17.7% slower, 3.4 semitones down');
  assert.equal(E.syRepitchText(1.15 * t, t), 'plays 15.0% faster, 2.4 semitones up');
  assert.equal(E.syRepitchText(t, t), 'plays at its own speed and pitch');
  assert.equal(E.syRepitchText(2 * t, t), 'plays 100.0% faster, 12.0 semitones up');
  assert.equal(E.syRepitchText(0.5 * t, t), 'plays 50.0% slower, 12.0 semitones down');
  assert.equal(E.syRepitchText(t * Math.pow(2, 1 / 12), t), 'plays 5.9% faster, 1.0 semitone up');
  assert.equal(E.syRepitchText(1, 0), '');
  // the rate is the one applyChop uses: loop / target
  const I = { pad: 0, tsp: 0, slc: 0, smode: 0, sync: 1, beats: 8, grain: 1, ofs: 0 }, d = new Float32Array(44100 * 2.5), ev = { type: 'note', t: 1, rate: 1, ofs: 0, cut: null, slc: null };
  E.setKit({ voices: [E.mkVoice('S', [], { tune: 0, len: 1.2, gate: 400, tail: 0 })] }, { bpm: 140, lpb: 8, insts: [] });
  E.applyChop(ev, I, 0, 48, 0, 140, d, 44100);
  const loop = E.loopSecs(0, d, 44100); assert.ok(Math.abs(ev.rate - loop / t) < 1e-12, 'ev.rate is loop / target');
  assert.equal(E.syRepitchText(loop, t), 'plays ' + (Math.abs(ev.rate - 1) * 100).toFixed(1) + '% ' + (ev.rate > 1 ? 'faster' : 'slower') + ', ' + Math.abs(12 * Math.log2(ev.rate)).toFixed(1) + ' semitones ' + (ev.rate > 1 ? 'up' : 'down'));
});

test('the sheet text says it too (slPlan), and the picker is one list for both places', () => {
  assert.match(E.slPlan(32, 8, 140, 2.82, 1), /the sample is 2\.82 s: it plays \d+\.\d% (faster|slower), \d\.\d semitones? (up|down)\. The length is in lines/);
  assert.match(E.slPlan(32, 8, 140, 2.82, 2), /pitch stays/);
  assert.match(src, /\{l:'SYNC', opts:true, get:\(\) => syPick\(I\), set:v => syPut\(I, v\), tap:\(\) => \{ const \{L, map\} = syList\(\)/, 'the SYNC row opens the grouped list');
  assert.match(src, /const \{L, map\} = syList\(\); let box2/, 'the SYNC TO LINES sheet builds its mode choice from the same list');
  assert.match(src, /syRepitchRow\(I\)/, 'the sentence is in the SYNC and BEATS rows');
  assert.match(src, /syRepitchText\(loop, slSecs\(r\.lines, lpb, bpm\)\)/, 'and in the toast');
  assert.ok(!/I\.sync = (mode|1|0)[;\s]/.test(src.slice(src.indexOf('===STRETCH PICKER START==='), src.indexOf('===SYNC LINES END==='))), 'nothing in the sync code writes I.sync directly any more (syPut does)');
});

test('SYNC TO LINES: slSet writes the picker mode (sync and, later, sx) and OFF clears both', () => {
  const insts = [{ pad: 0, sync: 0 }, { pad: 0, sync: 3, sx: 1 }, { pad: 1, sync: 2 }];
  E.slSet(insts, 0, 16, 2, 8); assert.deepEqual(insts.map(i => [i.sync, i.sx]), [[2, undefined], [2, undefined], [2, undefined]]);
  E.slSet(insts, 0, 0, 0, 8); assert.deepEqual(insts.map(i => i.sync), [0, 0, 2]); assert.ok(!('sx' in insts[1]));
});
