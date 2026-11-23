// Patch knobs as MOD/macro/scene targets (22 Nov, step 3): kn:t:k = knob Kk of track t's patch. Pieces of index.html run in a vm (as mod-mvol.test.js does).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what){ const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
const NT = 32, MT = 32;
const clamp = (v, a, b) => Math.min(b, Math.max(a, v));
const PAGE = vm.createContext({ Math, JSON, Object, Array, Map, Set, String, Number, isNaN, NT, MT, TFX: {}, FX_GROUP: {}, FX_CREDIT: {}, PUMP_META: {}, clamp, IC: 1, vtrk: false,
  int: (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d, d2: n => String(n).padStart(2, '0'), isBusT: () => true, isPumpT: () => false,
  isLivePad: p => p === 'live', isWlV: v => v === 'waves', kit: { voices: { live: 'x', wv: 'waves' } }, voiceForTrack: () => null, globalThis: null });
PAGE.globalThis = PAGE;
vm.runInContext(cut(/\/\* ===== PERFORM \(owner, 1 Nov[\s\S]*?\n(?=function makeSeq\(S, mode, o\)\{)/, 'the PERFORM block'), PAGE);
const run = code => vm.runInContext(code, PAGE), J = x => JSON.parse(JSON.stringify(x));
// a song where track 2 plays an instrument whose pad is `pad` (phrase rows: [note, inst])
function song(pad, chainOn){
  const S = { song: Array.from({ length: 4 }, () => new Array(NT + 1).fill(-1)), chains: [[[0, 0]]], phrases: [[[48, 0]]], insts: [{ pad }],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128 })), tfx: Array.from({ length: NT }, () => []), mfx: [], chainOn };
  S.song[0][2] = 0; return S;
}
const knIds = t => J(run(`tgtList(${t})`)).filter(([i]) => i.startsWith('kn:'));

test('tgtOk: kn:t:k for k 1-8 only', () => {
  for (const id of ['kn:0:1', 'kn:5:8', 'kn:31:4']) assert.ok(run(`tgtOk('${id}')`), id);
  for (const id of ['kn:0:0', 'kn:0:9', 'kn:x:1', 'kn:1', 'kn:1:1:1', 'kn::1']) assert.ok(!run(`tgtOk('${id}')`), id);
});
test('tgtLabel / tgtOwn', () => {
  assert.equal(run("tgtLabel('kn:2:3', true)"), 'K3 (patch knob)');
  const long = run("tgtLabel('kn:2:3')"); assert.ok(long.endsWith(' · K3 (patch knob)') && long.length > 'K3 (patch knob)'.length, long);
  PAGE.S = song('live'); assert.equal(run("tgtOwn('kn:2:3')"), 128);
});
test('tgtList: knobs only for a chains track or a waves instrument, never the master', () => {
  const want = Array.from({ length: 8 }, (_, i) => ['kn:2:' + (i + 1), 'K' + (i + 1) + ' · patch knob']);
  PAGE.S = song('live', []); assert.deepEqual(knIds(2), [], 'plain live pad, no chain');
  PAGE.S = song('live', []); PAGE.S.chainOn[2] = true; assert.deepEqual(knIds(2), want, 'chainOn');
  PAGE.S = song('wv', []); assert.deepEqual(knIds(2), want, 'waves instrument');
  PAGE.S = song('wv', [true]); PAGE.S.chainOn[MT] = true; assert.deepEqual(knIds(MT), [], 'master');
  PAGE.S = song('live', undefined); assert.deepEqual(knIds(2), [], 'no chainOn array at all');
});
test('tgtLive / tgtWaves share tgtPadAny', () => {
  PAGE.S = song('live', []); assert.ok(run('tgtLive(2)')); assert.ok(!run('tgtWaves(2)'));
  PAGE.S = song('wv', []); assert.ok(run('tgtWaves(2)')); assert.ok(!run('tgtLive(2)'));
  assert.ok(run("tgtPadAny(2, p => p === 'wv')")); assert.ok(!run("tgtPadAny(2, p => p === 'nope')")); assert.ok(!run(`tgtPadAny(${MT}, () => true)`));
});
test('tgtEvs: kn becomes one chc event (v/255 clamped, null: -1), master/out of range nothing', () => {
  PAGE.S = song('live', [true]);
  assert.deepEqual(J(run("tgtEvs('kn:2:3', 255, 1.5, new Map())")), [{ type: 'chc', t: 1.5, track: 2, p: 'k3', v: 1 }]);
  assert.deepEqual(J(run("tgtEvs('kn:2:8', 51, 2, new Map())")), [{ type: 'chc', t: 2, track: 2, p: 'k8', v: 0.2 }]);
  assert.equal(run("tgtEvs('kn:2:1', 999, 0, new Map())")[0].v, 1); assert.equal(run("tgtEvs('kn:2:1', -50, 0, new Map())")[0].v, 0);
  assert.deepEqual(J(run("tgtEvs('kn:2:5', null, 3, new Map())")), [{ type: 'chc', t: 3, track: 2, p: 'k5', v: -1 }]);
  assert.deepEqual(J(run(`tgtEvs('kn:${MT}:1', 10, 0, new Map())`)), []);
  assert.deepEqual(J(run('tgtEvs("kn:99:1", 10, 0, new Map())')), []);
});
test('normPerf keeps kn targets in macros and LFOs, drops bad ones', () => {
  PAGE.S = song('live', [true]);
  const j = { sc: [], mac: [{ v: 0, tg: [{ id: 'kn:2:3', lo: 0, hi: 255 }, { id: 'kn:2:9', lo: 0, hi: 255 }] }], lfo: { 2: [{ id: 'kn:2:1', sh: 0, rt: 3, dp: 100 }, { id: 'kn:2:0', sh: 0, rt: 3, dp: 100 }] } };
  PAGE.__j = j; const P = J(run('normPerf(__j)'));
  assert.deepEqual(P.mac[0].tg.map(g => g.id), ['kn:2:3']);
  assert.deepEqual(P.lfo[2].map(l => l.id), ['kn:2:1']);
});
