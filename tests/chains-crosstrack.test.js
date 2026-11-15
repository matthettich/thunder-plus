// Cross-track cables (diagnosis, 15 Nov): a cable from one track's sub-graph into a module of another track's sub-graph
// carries no signal, because every track runs its own chains node. Drives the real engine, one node per Chain Out track,
// the way the host does (own track's audio on its Track In, sends one block late).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const B = 128;
function node(track){
  const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js');
  const E = new ctx.ChainsDSP.ChainsEngine(48000, { focus: track, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  return E;
}
const mod = (id, type, params) => ({ id, type, scope: 'mono', params: params || {} });
const cab = (a, pa, b, pb) => ({ from: [a, pa], to: [b, pb] });
const rms = a => { let e = 0; for (const v of a) e += v * v; return Math.sqrt(e / a.length); };
/* bus tracks 1, 2 (kick, loud) -> bus Mixer -> Chain Out 9; Track In 15 (steady tone) -> Sidechain comp -> Chain Out 15 */
function base(extra){
  return { modules: [mod('i1', 'trackIn', { track: 1 }), mod('i2', 'trackIn', { track: 2 }), mod('bx', 'smixer'), mod('o9', 'chainOut', { track: 9 }),
    mod('i15', 'trackIn', { track: 15 }), mod('sc', 'sccomp', { threshold: .1, ratio: 20, attack: .1, release: 50, ...(extra.scp || {}) }), mod('o15', 'chainOut', { track: 15 }), ...(extra.mods || [])],
    cables: [cab('i1', 'out', 'bx', 'in1'), cab('i2', 'out', 'bx', 'in2'), cab('bx', 'out', 'o9', 'in'), cab('i15', 'out', 'sc', 'in'), cab('sc', 'out', 'o15', 'in'), ...(extra.cabs || [])] };
}
/* run both nodes; slot k of a node's track input holds track k+1's audio only for the tracks the host feeds that node */
function run(patch, blocks, feedBusToo){
  const N9 = node(8), N15 = node(14); N9.loadPatch(patch); N15.loadPatch(patch);
  const out = new Float32Array(blocks * B), cnt = { act15: null, act9: null };
  const buf = () => new Float32Array(16 * B), tone = new Float32Array(B).map((_, i) => .5 * Math.sin(i * .2)), kick = new Float32Array(B).fill(.9);
  for (let b = 0; b < blocks; b++){
    const L9 = buf(), L15 = buf(); L9.set(kick, 0); L9.set(kick, B); L15.set(tone, 14 * B); if (feedBusToo){ L15.set(kick, 0); L15.set(kick, B); }
    N9.setTrackInputs(L9, L9); N9.sendIn = [null, null, null, null, null, null, null, null];
    N15.setTrackInputs(L15, L15);
    N15.sendIn = N9._lastSend || [];
    N9.process(B); N15.process(B);
    N9._lastSend = Array.from({ length: 8 }, (_, k) => N9.sendOut.slice(k * B, (k + 1) * B));
    out.set(N15.bus(15), b * B);
  }
  cnt.act9 = [...N9._active()].sort(); cnt.act15 = [...N15._active()].sort();
  return { out, ...cnt };
}
const tail = (o, blocks) => o.subarray((blocks - 20) * B);
const plain = (() => { const N = node(14); N.loadPatch({ modules: [mod('i15', 'trackIn', { track: 15 }), mod('o15', 'chainOut', { track: 15 })], cables: [cab('i15', 'out', 'o15', 'in')] });
  const o = new Float32Array(20 * B), tone = new Float32Array(16 * B); for (let i = 0; i < B; i++) tone[14 * B + i] = .5 * Math.sin(i * .2);
  for (let b = 0; b < 20; b++){ N.setTrackInputs(tone, tone); N.process(B); o.set(N.bus(15), b * B); } return rms(o); })();

test('cross-track: a cable from the bus into the other track\'s SC input is run in that track\'s node, on silence (the bus\'s Track Ins have no audio there)', () => {
  const r = run(base({ cabs: [cab('bx', 'out', 'sc', 'sc')] }), 120);
  assert.ok(r.act15.includes('bx') && r.act15.includes('i1'), 'node 15 runs the bus modules (reached backwards through the SC cable): ' + r.act15);
  assert.ok(rms(tail(r.out, 120)) > plain * .95, 'nothing is ducked: the SC input is silent in node 15 (rms ' + rms(tail(r.out, 120)) + ' vs ' + plain + ')');
  assert.ok(plain > .1, 'control: the tone is audible'); assert.ok(r.act9.includes('o9') && !r.act9.includes('o15'), 'node 9 plays the bus');
  const c = run(base({ cabs: [cab('bx', 'out', 'sc', 'sc')] }), 120, true);
  assert.ok(rms(tail(c.out, 120)) < plain * .5, 'control: were the bus tracks\' audio on node 15\'s Track Ins (the host does not send it: busMap omits them) the same cable would duck');
});
test('cross-track: Sidechain Out Send 1 in the bus + Source Send 1 on the Sidechain comp (no cable into SC) does duck', () => {
  const r = run(base({ mods: [mod('so', 'scout', { snd: 1 })], cabs: [cab('bx', 'out', 'so', 'in')], scp: { sck: 1 } }), 120);
  assert.ok(rms(tail(r.out, 120)) < plain * .5, 'ducked by the bus: ' + rms(tail(r.out, 120)) + ' vs ' + plain);
  assert.ok(r.act9.includes('so') && !r.act15.includes('so'), 'the Send runs in node 9 only');
});
test('cross-track: Source Send 1 on a Sidechain In cabled to SC ducks too', () => {
  const r = run(base({ mods: [mod('so', 'scout', { snd: 1 }), mod('si', 'scin', { sck: 1 })], cabs: [cab('bx', 'out', 'so', 'in'), cab('si', 'out', 'sc', 'sc')] }), 120);
  assert.ok(rms(tail(r.out, 120)) < plain * .5, 'ducked: ' + rms(tail(r.out, 120)));
});

/* The editor's marking: xcCables() cut out of chains/index.html and run in a vm. */
const vm = require('vm'), src = H.read('chains/index.html');
function xc(patch){
  const m = src.match(/function xcCables\(\)\{[\s\S]*?\n\}\n(?=function xcTip)/); assert.ok(m, 'chains/index.html: xcCables not found');
  const c = vm.createContext({ Set, Map, Array });
  vm.runInContext('const fbOk = c => !!(c && Array.isArray(c.from) && Array.isArray(c.to));' + m[0], c); c.doc = patch;
  return vm.runInContext('doc = this.doc; xcCables()', Object.assign(c, { doc: patch, this: c }));
}
test('cross-track editor mark: the cable from the bus into track 15\'s SC input is marked, the bus and the chain around it are not', () => {
  const p = base({ mods: [mod('mm', 'smixer')], cabs: [cab('bx', 'out', 'mm', 'in'), cab('mm', 'out', 'sc', 'sc')] });
  p.modules = p.modules.map(m => ({ ...m, params: m.params }));
  const X = xc(p), hit = [...X.keys()].map(c => c.from[0] + '>' + c.to[0] + '.' + c.to[1]);
  assert.deepEqual(hit, ['mm>sc.sc']);
  assert.equal([...X.values()][0].from.slice().sort().join(), '1,2'); assert.equal([...X.values()][0].to, 15);
});
test('cross-track editor mark: a Sidechain In or Receive-less chain, a bus, and an ordinary send are not marked', () => {
  assert.equal(xc(base({ mods: [mod('si', 'scin', { sck: 1 })], cabs: [cab('si', 'out', 'sc', 'sc')] })).size, 0, 'a Sidechain In has no Track In behind it');
  assert.equal(xc(base({})).size, 0, 'the bus and the chain on their own');
  const send = { modules: [mod('i4', 'trackIn', { track: 4 }), mod('o4', 'chainOut', { track: 4 }), mod('o6', 'chainOut', { track: 6 })], cables: [cab('i4', 'out', 'o4', 'in'), cab('i4', 'out', 'o6', 'in')] };
  assert.equal(xc(send).size, 0, 'a track also feeding a bus (a send) is fine');
});
test('cross-track editor mark: another track\'s Track In cabled into a chain that has its own Track In is marked', () => {
  const X = xc(base({ cabs: [cab('i1', 'out', 'sc', 'sc')] }));
  assert.ok([...X.keys()].some(c => c.from[0] === 'i1' && c.to[0] === 'sc'));
});
