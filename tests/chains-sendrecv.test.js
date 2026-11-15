// Send / Receive (15 Nov): the graph's compile step turns each cable out of a Receive into cables from the sources of the Send with the same label,
// so the engine runs ordinary cables. Real engine; a pair must give the same samples as a cable.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const B = 128, cab = (a, pa, b, pb, fb) => ({ from: [a, pa], to: [b, pb], ...(fb ? { fb: true } : {}) });
const mod = (id, type, params, text) => ({ id, type, scope: 'mono', params: params || {}, ...(text != null ? { text } : {}) });
function engine(){ const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return new ctx.ChainsDSP.ChainsEngine(48000, { focus: -1, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } }); }
function render(patch, blocks = 60, tweak){
  const E = engine(); E.loadPatch(patch); if (tweak) tweak(E);
  const L = new Float32Array(blocks * B), R = new Float32Array(blocks * B), s = H.testSignal(B * blocks);
  for (let b = 0; b < blocks; b++){ const x = s.subarray(b * B, (b + 1) * B), y = x.map(v => v * .6); E.setTrackInputs(x, y); E.process(B); L.set(E.bus(1), b * B); R.set(E.busR(1), b * B); }
  return { E, L, R };
}
const same = (a, b) => { for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false; return true; };
const energy = a => { let e = 0; for (const v of a) e += v * v; return e; };
const tin = () => mod('i', 'trackIn', { track: 1 }), tout = () => mod('o', 'chainOut', { track: 1, level: 1 });
const direct = () => ({ modules: [tin(), mod('x', 'comp'), tout()], cables: [cab('i', 'out', 'x', 'in'), cab('i', 'outR', 'x', 'inR'), cab('x', 'out', 'o', 'in'), cab('x', 'outR', 'o', 'inR')] });
const viaPair = (sp, rp, st, rt) => ({ modules: [tin(), mod('s', 'send', sp, st), mod('r', 'recv', rp, rt), mod('x', 'comp'), tout()],
  cables: [cab('i', 'out', 's', 'in'), cab('i', 'outR', 's', 'inR'), cab('r', 'out', 'x', 'in'), cab('r', 'outR', 'x', 'inR'), cab('x', 'out', 'o', 'in'), cab('x', 'outR', 'o', 'inR')] });

test('send/receive: a pair gives the same samples as a cable (stereo)', () => {
  const a = render(direct()), b = render(viaPair({ lab: 1 }, { lab: 1 }));
  assert.ok(energy(a.L) > 1 && energy(a.R) > 1, 'the control is not silent');
  assert.ok(same(a.L, b.L) && same(a.R, b.R));
});
test('send/receive: the label is A-H or free text (trimmed, any case), and a Receive with another label is silent', () => {
  const a = render(direct());
  assert.ok(same(a.L, render(viaPair({ lab: 3 }, { lab: 3 })).L), 'C and C');
  assert.ok(same(a.L, render(viaPair({ lab: 0 }, { lab: 0 }, ' Bus 9 ', 'bus 9')).L), 'custom text');
  for (const p of [viaPair({ lab: 1 }, { lab: 2 }), viaPair({ lab: 0 }, { lab: 0 }, 'a', 'b'), viaPair({ lab: 0 }, { lab: 0 }, '', ''), viaPair({ lab: 0 }, { lab: 1 }, 'a', undefined)]){
    const q = render(p); assert.equal(energy(q.L) + energy(q.R), 0, 'no matching Send: silence');
  }
});
test('send/receive: several Receives share one Send; several Sends with one label add; In R empty feeds In to both sides on a Receive\'s Out R', () => {
  const p = { modules: [tin(), mod('s', 'send', { lab: 1 }), mod('r1', 'recv', { lab: 1 }), mod('r2', 'recv', { lab: 1 }), mod('m', 'smixer', { chans: 2, l1: 1, l2: 1, level: 1 }), tout()],
    cables: [cab('i', 'out', 's', 'in'), cab('r1', 'out', 'm', 'in1'), cab('r2', 'out', 'm', 'in2'), cab('m', 'out', 'o', 'in'), cab('m', 'outR', 'o', 'inR')] };
  const one = { modules: [tin(), mod('m', 'smixer', { chans: 2, l1: 1, l2: 1, level: 1 }), tout()], cables: [cab('i', 'out', 'm', 'in1'), cab('i', 'out', 'm', 'in2'), cab('m', 'out', 'o', 'in'), cab('m', 'outR', 'o', 'inR')] };
  assert.ok(same(render(p).L, render(one).L), 'two Receives = two cables from the same source');
  const two = { modules: [tin(), mod('s1', 'send', { lab: 2 }), mod('s2', 'send', { lab: 2 }), mod('r', 'recv', { lab: 2 }), tout()],
    cables: [cab('i', 'out', 's1', 'in'), cab('i', 'out', 's2', 'in'), cab('r', 'out', 'o', 'in')] };
  const sum = { modules: [tin(), tout()], cables: [cab('i', 'out', 'o', 'in'), cab('i', 'out', 'o', 'in')] };
  const r = render(two), d = render({ modules: [tin(), mod('a', 'utility'), tout()], cables: [cab('i', 'out', 'o', 'in')] });
  assert.ok(Math.abs(energy(r.L) / energy(d.L) - 4) < 1e-6, 'two Sends of one label add (twice the signal, four times the energy)');
  const mono = { modules: [tin(), mod('s', 'send', { lab: 1 }), mod('r', 'recv', { lab: 1 }), tout()], cables: [cab('i', 'out', 's', 'in'), cab('r', 'out', 'o', 'in'), cab('r', 'outR', 'o', 'inR')] };
  mono.ti2 = 1; const q = render(mono); assert.ok(same(q.L, q.R), 'only In patched on the Send: Out R carries In');
});
test('send/receive: changing the label (param or text) re-resolves live; removing the Send silences the Receive', () => {
  const a = render(direct());
  const b = render(viaPair({ lab: 1 }, { lab: 2 }), 60, E => E.handleMessage({ op: 'param', id: 'r', name: 'lab', value: 1 }));
  assert.ok(same(a.L, b.L), 'Receive set to A after loading');
  const c = render(viaPair({ lab: 0 }, { lab: 0 }, 'x', 'y'), 60, E => E.handleMessage({ op: 'text', id: 'r', value: 'x' }));
  assert.ok(same(a.L, c.L), 'text changed');
  const d = render(viaPair({ lab: 1 }, { lab: 1 }), 60, E => E.handleMessage({ op: 'remove', id: 's' }));
  assert.equal(energy(d.L), 0);
});
test('send/receive: a loop through a pair is marked as feedback, runs, and stays finite and bounded', () => {
  const p = { modules: [tin(), mod('m', 'smixer', { chans: 2, l1: 1, l2: 1, level: 1 }), mod('x', 'comp'), mod('s', 'send', { lab: 4 }), mod('r', 'recv', { lab: 4 }), tout()],
    cables: [cab('i', 'out', 'm', 'in1'), cab('i', 'outR', 'm', 'in1R'), cab('r', 'out', 'm', 'in2'), cab('r', 'outR', 'm', 'in2R'), cab('m', 'out', 'x', 'in'), cab('m', 'outR', 'x', 'inR'),
      cab('x', 'out', 's', 'in'), cab('x', 'outR', 's', 'inR'), cab('x', 'out', 'o', 'in'), cab('x', 'outR', 'o', 'inR')] };
  const q = render(p, 200); assert.ok(energy(q.L) > 0 && [...q.L].every(Number.isFinite) && Math.max(...q.L.map(Math.abs)) < 4);
  assert.ok(q.E.graph.inc.get('m').some(c => c.a[0] === 'x' && c.fb === true), 'the cable x -> m (through the pair) is marked fb');
  assert.ok(q.E.graph.order.indexOf('m') < q.E.graph.order.indexOf('x'), 'm runs before x');
});
test('send/receive: a patch with no pair runs on the very cables that were drawn (no copy)', () => {
  const E = engine(); E.loadPatch(direct()); const g = E.graph;
  for (const [id, list] of g.inc) for (const c of list) assert.ok(g.cables.includes(c), id);
});

/* CodeRabbit findings on PR 161 */
test('send/receive: two Sends of one label, one with In R and one without: Out R gets the mono one on both sides (per Send)', () => {
  const p = { ti2: 1, modules: [tin(), mod('i2', 'trackIn', { track: 2 }), mod('s1', 'send', { lab: 1 }), mod('s2', 'send', { lab: 1 }), mod('r', 'recv', { lab: 1 }), tout()],
    cables: [cab('i', 'out', 's1', 'in'), cab('i', 'outR', 's1', 'inR'), cab('i2', 'out', 's2', 'in'), cab('r', 'out', 'o', 'in'), cab('r', 'outR', 'o', 'inR')] };
  const E = engine(); E.loadPatch(p);
  const into = E.graph.inc.get('o').filter(c => c.b[1] === 'inR').map(c => c.a[0] + '.' + c.a[1]).sort();
  assert.equal(into.join(), 'i.outR,i2.out', 'In R of s1 and the In fallback of s2');
});
test('send/receive: pairs in series resolve through (Track In -> Send A, Receive A -> Send B, Receive B -> out), and a loop of Receives does not hang', () => {
  const p = { modules: [tin(), mod('sa', 'send', { lab: 1 }), mod('ra', 'recv', { lab: 1 }), mod('sb', 'send', { lab: 2 }), mod('rb', 'recv', { lab: 2 }), tout()],
    cables: [cab('i', 'out', 'sa', 'in'), cab('i', 'outR', 'sa', 'inR'), cab('ra', 'out', 'sb', 'in'), cab('ra', 'outR', 'sb', 'inR'), cab('rb', 'out', 'o', 'in'), cab('rb', 'outR', 'o', 'inR')] };
  const direct = { modules: [tin(), tout()], cables: [cab('i', 'out', 'o', 'in'), cab('i', 'outR', 'o', 'inR')] };
  const a = render(p), d = render(direct); assert.ok(energy(a.L) > 1 && same(a.L, d.L) && same(a.R, d.R));
  const loop = { modules: [mod('sa', 'send', { lab: 1 }), mod('ra', 'recv', { lab: 1 }), tout()], cables: [cab('ra', 'out', 'sa', 'in'), cab('ra', 'out', 'o', 'in')] };
  assert.doesNotThrow(() => render(loop, 4));
});
test('waves: a label change through setText / setParam marks culling dirty', () => {
  const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  const src = H.read('waves/waves-worklet.js');
  assert.match(src, /this\.graph\.compile\(\); this\._sync\(\); \} \} \}\n/);
  assert.equal((src.match(/this\.graph\.compile\(\); this\._sync\(\);/g) || []).length, 2);
});
