// The 1 Nov PERFORM block (index.html: targets, scenes, macros, LFO shapes, the song fields) and latency compensation's plan,
// cut out of the real file and run in a vm, plus source guards for the sequencer's hooks (CND, NUD, RTV, SLC, XFD, MAC,
// LK1–LK4, PRT, the track LFO).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');

function cut(re, what){ const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }

function perfCtx(){
  const blk = cut(/\/\* ===== PERFORM \(owner, 1 Nov[\s\S]*?\n(?=function makeSeq\(S, mode, o\)\{)/, 'the PERFORM block');
  const NT = 4, MT = 4;
  const c = vm.createContext({ Math, JSON, Object, Array, Map, Set, String, Number, isNaN, NT, MT,
    clamp: (v, a, b) => Math.min(b, Math.max(a, v)), int: (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d,
    d2: n => String(n).padStart(2, '0'), hx: n => n.toString(16).toUpperCase().padStart(2, '0'),
    TFX: { eq: { n: 'EQ', ps: [{ k: 'treble', l: 'TREBLE', min: 0, max: 255 }, { k: 'mid', l: 'MID', min: 0, max: 100 }] } }, isPumpT: () => false,
    ctx: null, rt: null });
  vm.runInContext(blk, c);
  c.S = { mix: Array.from({ length: NT }, () => ({ vol: 224, pan: 128 })), tfx: Array.from({ length: NT }, () => [null]), mfx: [null], insts: [{ pad: 0 }] };
  c.S.tfx[1][0] = { type: 'eq', on: true, treble: 64, mid: 50 };
  c.S.perf = c.freshPerf();
  return c;
}

test('perform: targets, own values, labels and the song fields survive a save and load', () => {
  const c = perfCtx();
  assert.ok(c.tgtOk('vol:3') && c.tgtOk('fx:1:0:1') && c.tgtOk('cut:0') && !c.tgtOk('vol') && !c.tgtOk('fx:1:0') && !c.tgtOk('x:1'));
  assert.equal(c.tgtOwn('vol:0'), 224); assert.equal(c.tgtOwn('pan:2'), 128);
  assert.equal(c.tgtOwn('fx:1:0:0'), 64); assert.equal(c.tgtOwn('fx:1:0:1'), 128, 'MID 50 of 0–100 is 80 hex');
  assert.equal(c.tgtOwn('cut:0'), 255); assert.equal(c.tgtOwn('res:0'), 0);
  assert.match(c.tgtLabel('fx:1:0:1'), /TRACK 02 · FX1 EQ MID/);
  const P = c.S.perf; P.sc[2] = { n: 'drop', v: { 'vol:0': 10, 'bogus': 3 } }; P.mac[1].tg.push({ id: 'pan:1', lo: 20, hi: 300 }); P.lk[2] = ['dec:2', null, 'x', null]; P.lfo[1] = { id: 'pan:1', sh: 9, rt: 99, dp: 80 };
  const back = c.normPerf(JSON.parse(JSON.stringify(P)));
  assert.deepEqual(JSON.parse(JSON.stringify(back.sc[2])), { n: 'drop', v: { 'vol:0': 10 } }, 'an unknown target is dropped');
  assert.equal(back.mac[1].tg[0].hi, 255, 'values clamp to FF');
  assert.deepEqual(JSON.parse(JSON.stringify(back.lk[2])), ['dec:2', null, null, null]);
  assert.equal(back.lfo[1].sh, c.PLFO_SHAPES.length - 1); assert.equal(back.lfo[1].rt, c.PLFO_RATES.length - 1);
  assert.equal(c.normPerf(null).sc.length, 16); assert.equal(c.normParts('x').length, 16);
});

test('perform: the crossfader morphs A to B (from the song’s own where a scene lacks a target) and macros win', () => {
  const c = perfCtx(), P = c.S.perf;
  P.xa = 0; P.xb = 1; P.sc[0].v = { 'vol:0': 0 }; P.sc[1].v = { 'vol:0': 200, 'pan:1': 0 };
  let m = c.perfResolve({ xf: 0, mac: [] }); assert.equal(m.get('vol:0'), 0); assert.equal(m.get('pan:1'), 128, 'pan:1 at A: the song’s own');
  m = c.perfResolve({ xf: 255, mac: [] }); assert.equal(m.get('vol:0'), 200); assert.equal(m.get('pan:1'), 0);
  m = c.perfResolve({ xf: 128, mac: [] }); assert.equal(m.get('vol:0'), 100); assert.equal(m.get('pan:1'), 64);
  P.mac[0].tg = [{ id: 'vol:0', lo: 255, hi: 55 }];
  m = c.perfResolve({ xf: 255, mac: [255] }); assert.equal(m.get('vol:0'), 55, 'a macro over a scene; HIGH under LOW turns it the other way');
  m = c.perfResolve({ xf: 255, mac: [null] }); assert.equal(m.get('vol:0'), 255, 'no live value: the macro’s start value (0 → LOW)');
});

test('perform: target events, part events and LFO shapes', () => {
  const c = perfCtx(), pno = new Map();
  assert.deepEqual(JSON.parse(JSON.stringify(c.tgtEvs('vol:2', 99, 1, pno))), [{ type: 'tva', t: 1, track: 2, v: 99 }]);
  assert.deepEqual(JSON.parse(JSON.stringify(c.tgtEvs('fx:1:0:1', null, 2, pno))), [{ type: 'fxp', t: 2, track: 1, slot: 0, pi: 1, v: 128 }], 'null: back to the slot’s own');
  assert.deepEqual(c.tgtEvs('fx:2:0:0', 5, 0, pno).length, 0, 'an empty slot gets nothing');
  c.tgtEvs('cut:3', 40, 0, pno); assert.equal(pno.get(3).cut, 40); c.tgtEvs('cut:3', null, 0, pno); assert.equal(pno.get(3).cut, undefined);
  const ev = c.partEvs(c.partSnap(), 0); assert.equal(ev.filter(e => e.type === 'tva').length, 4); assert.equal(ev.filter(e => e.type === 'fxp').length, 2);
  for (let sh = 0; sh < 6; sh++) for (let k = 0; k <= 20; k++){ const v = c.lfoAt(sh, k / 20, 3); assert.ok(v >= -1 && v <= 1, `shape ${sh} in range`); }
  assert.equal(c.lfoAt(5, 0.1, 7), c.lfoAt(5, 0.9, 7), 'random holds its value for a cycle');
});

test('sequencer hooks: conditions, nudge, retrigger curve, slices, perform commands and the LFO', () => {
  for (const n of ['CND', 'NUD', 'RTV', 'SLC', 'XFD', 'MAC', 'LK1', 'LK2', 'LK3', 'LK4', 'PRT']) assert.match(cut(/const FXC = \[[\s\S]*?\];/, 'FXC'), new RegExp(`'${n}'`), n + ' in FXC');
  assert.match(src, /if \(fx\.CND != null\)\{ const ty = Math\.min\(7, fx\.CND & 15\)/, 'CND');
  assert.match(src, /const nud = fx\.NUD != null \? s8\(fx\.NUD\) \* tick : 0;/, 'NUD');
  assert.match(src, /rv = fx\.RTV != null \? s8\(fx\.RTV\) : null/, 'RTV');
  assert.match(src, /k = sl \? ev\.slc % slc :/, 'SLC in applyChop');
  assert.match(src, /if \(fx\.XFD != null\)\{ PF\.xf = fx\.XFD; perfDiff\(evs, at\); \}/, 'XFD');
  assert.match(src, /TRACK LFO \(owner, 1 Nov/, 'track LFO');
  assert.match(src, /else if \(ev\.type === 'tva'\)/, 'the player takes TVA');
  assert.match(src, /S\.perf = normPerf\(j\.perf\); S\.parts = normParts\(j\.parts\);/, 'normSong keeps them');
});

test('latency compensation: the plan lines direct tracks, sends and bus sources up', () => {
  const blk = cut(/function pdcPlan\(sr\)\{[\s\S]*?\n\}\n/, 'pdcPlan');
  const NT = 4, lat = { 0: 0, 1: 0, 2: 0, 3: 100 };   // samples; track 3 is a bus with a latent effect
  const c = vm.createContext({ Math, Map, Set, NT, aset: { pdc: true }, S: { tfx: [[], [], [], []] },
    pumpChainLatency: (A, sr) => lat[c.S.tfx.indexOf(A)] || 0,
    busPlan: () => ({ feeds: new Map([[3, [0, 1]]]), pure: new Set([0]) }) });
  vm.runInContext(blk, c);
  const p = c.pdcPlan(1);
  assert.equal(p.T, 100);
  assert.equal(p.d.get(0), 0, 'a pure source: heard only through its bus');
  assert.equal(p.d.get(1), 100, 'a send: its direct copy waits for the bus');
  assert.equal(p.d.get(2), 100, 'a plain track waits');
  assert.equal(p.d.get(3), 0, 'the latest path waits for nothing');
  c.aset.pdc = false; assert.equal(c.pdcPlan(1).T, 0, 'off: nothing moves');
});
