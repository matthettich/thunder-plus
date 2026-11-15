// PERFORM FOLLOWS THE TRACKS (14 Nov, owner: adding two tracks left his three LFOs on track number 11 while the track moved).
// ntrPerm / perfRemap / ntrRemap are cut out of index.html and run in a vm with stand-ins for the rest of the app.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const eq = (a, b, m) => assert.equal(JSON.stringify(a), JSON.stringify(b), m);   // the vm's arrays are from another realm
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };
const code = cut('function ntrPermOld(', 'function setTracks(');   // (the half-and-half renumbering is kept as ntrPermOld; the new one is tested in mono-poly-counts.test.js)

function rig() {
  const c = { NT: 32, MT: 32, SCK_SEND: 8, vtrk: false, kit: null, sckTrack: v => (v > 8 ? v - 9 : -1), perfLive: { sent: new Map(), pno: new Map() } };
  vm.createContext(c); vm.runInContext(code + '\nthis.ntrPerm = ntrPermOld; this.ntrRemap = ntrRemap;', c);
  const S = { ntr: 16, song: [], smute: [], tfx: Array.from({ length: 32 }, () => []), mfx: [], chainOn: new Array(32).fill(0),
    mix: Array.from({ length: 32 }, (_, t) => ({ vol: 100 + t, pan: 50 + t })), chainPatch: null };
  c.S = S;
  S.perf = { sc: [{ n: 'A', v: { 'vol:3': 10, 'fx:11:0:0': 20, 'fx:32:1:2': 30, 'vol:32': 40 } }, { n: '', v: {} }], xa: 0, xb: 1, xf: 0,
    mac: [{ n: 'M1', v: 0, tg: [{ id: 'pan:5', lo: 0, hi: 255 }, { id: 'fx:32:0:1', lo: 0, hi: 255 }, { id: 'cut:12', lo: 0, hi: 255 }] }],
    lk: { 11: ['vol:11', null, 'dec:2', 'res:32'], 32: ['fx:32:0:0', null, null, null] },
    lfo: { 11: [{ id: 'fx:11:0:0', sh: 0, rt: 3, dp: 128 }, { id: 'fx:11:1:0', sh: 1, rt: 3, dp: 128 }, { id: 'vol:11', sh: 2, rt: 3, dp: 128 }],
      4: [{ id: 'pan:13', sh: 0, rt: 3, dp: 1 }], 32: [{ id: 'vol:32', sh: 0, rt: 3, dp: 9 }, { id: 'fx:32:0:0', sh: 0, rt: 3, dp: 9 }] } };
  S.parts = [null, { n: 'P', insts: [], mix: Array.from({ length: 16 }, (_, t) => [t, 200 + t]), fx: { '11:0': { type: 'x' }, '32:0': { type: 'm' }, '2:1': { type: 'y' } } },
    { n: 'short', insts: [], mix: [[1, 2], [3, 4]], fx: {} }];
  return { c, S };
}
const apply = (r, n0, n1) => { const P = r.c.ntrPerm(n0, n1); assert.ok(P); const tN = r.c.ntrRemap(P); return { P, tN }; };

test('16 to 17: the LFOs, locks, scenes, macros and parts follow their tracks; the master stays', () => {
  const r = rig(), { P, tN } = apply(r, 16, 17), pf = r.S.perf, n = tN(11);
  assert.notEqual(n, 11, 'track 11 really moved');
  assert.ok(pf.lfo[n] && pf.lfo[n].length === 3, 'the three LFOs are on the moved track');
  eq(pf.lfo[n].map(L => L.id), ['fx:' + n + ':0:0', 'fx:' + n + ':1:0', 'vol:' + n]);
  assert.equal(pf.lfo[tN(4)][0].id, 'pan:' + tN(13), 'an LFO on one track aimed at another follows both');
  assert.equal(pf.lfo[32].length, 2); eq(pf.lfo[32].map(L => L.id), ['vol:32', 'fx:32:0:0'], 'master LFOs untouched');
  assert.equal(pf.sc[0].v['fx:' + n + ':0:0'], 20); assert.equal(pf.sc[0].v['vol:' + tN(3)], 10); assert.equal(pf.sc[0].v['fx:32:1:2'], 30); assert.equal(pf.sc[0].v['vol:32'], 40);
  eq(pf.mac[0].tg.map(g => g.id), ['pan:' + tN(5), 'fx:32:0:1', 'cut:' + tN(12)]);
  eq(pf.lk[n], ['vol:' + n, null, 'dec:' + tN(2), 'res:32']); eq(pf.lk[32], ['fx:32:0:0', null, null, null]);
  const p = r.S.parts[1]; assert.equal(p.mix.length, 32);
  for (let t = 0; t < 16; t++) eq(p.mix[tN(t)], [t, 200 + t], 'part mix of track ' + t);
  eq(Object.keys(p.fx).sort(), [n + ':0', '32:0', tN(2) + ':1'].sort());
  assert.equal(p.fx['32:0'].type, 'm');
  // the part with a short mix keeps the song's own for the tracks it never held
  const s = r.S.parts[2]; eq(s.mix[tN(0)], [1, 2]); eq(s.mix[tN(1)], [3, 4]); eq(s.mix[tN(5)], [r.S.mix[tN(5)].vol, r.S.mix[tN(5)].pan]);
});
test('19 to 21 (the owner\'s case): the LFOs on track 11 go with track 11', () => {
  const r = rig(); const t11 = 11; r.S.ntr = 19;
  const { tN } = apply(r, 19, 21), n = tN(t11);
  assert.ok(r.S.perf.lfo[n] && r.S.perf.lfo[n].length === 3);
  assert.ok(r.S.perf.lfo[n].every(L => L.id.split(':')[1] === String(n) || L.id.startsWith('fx:' + n)));
  if (n !== t11) assert.equal(r.S.perf.lfo[t11], undefined, 'nothing left behind on the old number');
});
test('round trips leave Perform identical: 16>17>16, 19>21>19, 21>19>21', () => {
  for (const [a, b] of [[16, 17], [19, 21], [21, 19], [16, 24], [24, 8]]) {
    const r = rig(); const before = JSON.stringify([r.S.perf, r.S.parts.map(p => p && Object.assign({}, p, { mix: null }))]);
    const mixBefore = JSON.stringify(r.S.parts[1].mix);
    apply(r, a, b); apply(r, b, a);
    assert.equal(JSON.stringify([r.S.perf, r.S.parts.map(p => p && Object.assign({}, p, { mix: null }))]), before, a + '>' + b + '>' + a);
    assert.equal(JSON.stringify(r.S.parts[1].mix.slice(0, 16)), mixBefore, 'mix of the first 16');
  }
});
test('the master is never renamed, whatever the count', () => {
  for (const [a, b] of [[16, 17], [19, 21], [21, 19]]) {
    const r = rig(); apply(r, a, b); const pf = r.S.perf;
    assert.equal(pf.lfo[32].length, 2); assert.equal(pf.lk[32][0], 'fx:32:0:0'); assert.equal(pf.sc[0].v['vol:32'], 40);
    assert.equal(r.S.parts[1].fx['32:0'].type, 'm');
  }
});
test('source: the remap is called from ntrRemap, and an undo step holds the whole song (S.perf and S.parts included)', () => {
  assert.match(code, /perm\(S\.mix\); perm\(S\.tfx\); perm\(S\.chainOn\);\s*perfRemap\(P, tN\);/);
  assert.match(src, /function snap\(\)\{[\s\S]*?JSON\.stringify\(S\)/);
});
