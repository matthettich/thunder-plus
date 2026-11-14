// CLOCK AND TRIGGER TOOLS (the owner's module queue, group 2): Clock multiplier, Chance gate (seeded), Counter, Step switch,
// Burst, Gate delay, Flip-flop; and Steps and Clock, which moved into the shared block so …waves has them too.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const N = 128, SR = 48000;
const chainsD = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx.ChainsDSP; };
const wavesD = () => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), tfxBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))) }); return ctx.WavesDSP; };
const BOTH = [['chains', chainsD()], ['waves', wavesD()]];
const TYPES = ['clkmul', 'chance', 'counter', 'stepsw', 'burst', 'gatedly', 'flipflop'];
/* One instance run over `blocks` blocks; ins: { port: fn(sample index) }. `tr` is the transport (startN counts song starts). */
function runner(D, type, over) {
  const m = D.MODULES[type], p = {}; for (const [k, d] of Object.entries(m.params)) p[k] = d[0]; Object.assign(p, over);
  const st = m.create(SR), io = { in: {}, out: {}, p, linked: new Set() }; let at = 0;
  for (const k in m.inputs) io.in[k] = new Float32Array(N); for (const k in m.outputs) io.out[k] = new Float32Array(N);
  return { p, run(ins, blocks = 1, tr = { tempo: 120, startN: 0 }) {
    const rec = {}; for (const k in m.outputs) rec[k] = new Float32Array(N * blocks);
    for (const k in ins || {}) io.linked.add(k);
    for (let b = 0; b < blocks; b++) {
      for (const k in io.in) { const s = ins && ins[k]; for (let i = 0; i < N; i++) io.in[k][i] = s ? s(at + i) : 0; }
      m.process(st, io, { block: N, sr: SR, transport: tr }); for (const k in rec) rec[k].set(io.out[k], b * N); at += N;
    }
    return rec; } };
}
const run = (D, type, over, ins, blocks, tr) => runner(D, type, over).run(ins, blocks, tr);
const pulse = (every, width = 10, off = 0) => i => (i - off) >= 0 && (i - off) % every < width ? 1 : 0;
const rises = g => { const a = []; for (let i = 0; i < g.length; i++) if (g[i] >= .5 && (i === 0 || g[i - 1] < .5)) a.push(i); return a; };

for (const [name, D] of BOTH) {
  test(name + ': the seven are registered and per-voice capable; Steps and Clock are there too', () => {
    for (const t of TYPES) { assert.ok(D.MODULES[t], t); assert.equal(D.MODULES[t].type, t); assert.equal(D.MODULES[t].scope, 'poly'); }
    assert.ok(D.MODULES.steps && D.MODULES.clock); assert.equal(D.MODULES.steps.scope, 'mono');
    for (const t of TYPES) assert.doesNotMatch(D.MODULES[t].process.toString(), /new |\[\s*\.\.\.|\.map\(|\.slice\(|=>\s*\{/, t + ' makes garbage per block');
  });
  test(name + ': Clock multiplier', () => {
    const r = run(D, 'clkmul', { mul: 4, width: .5 }, { clock: pulse(1200, 100) }, 60), e = rises(r.out);
    assert.deepEqual(e.slice(0, 2), [0, 1200], 'before it knows the tempo it passes the clock');
    const third = e.filter(x => x >= 2400 && x < 3600); assert.deepEqual(third, [2400, 2700, 3000, 3300], 'four evenly spaced');
    assert.equal(r.out[2400 + 149], 1); assert.equal(r.out[2400 + 151], 0);   // half of 300
    const one = run(D, 'clkmul', { mul: 1 }, { clock: pulse(1200, 100) }, 60); assert.deepEqual(rises(one.out).slice(0, 5), [0, 1200, 2400, 3600, 4800]);
    // the clock slows down: no extra pulses while it waits for the late one
    const slow = run(D, 'clkmul', { mul: 2 }, { clock: i => (i < 2400 ? i % 600 < 50 : (i - 2400) % 1800 < 50) ? 1 : 0 }, 60);
    assert.deepEqual(rises(slow.out).filter(x => x >= 2400 && x < 4200), [2400, 2700], 'two for the pulse at 2400, none after until 4200');
    // Reset forgets the tempo
    const rs = run(D, 'clkmul', { mul: 4 }, { clock: pulse(1200, 100), reset: i => i === 3700 ? 1 : 0 }, 60);
    assert.deepEqual(rises(rs.out).filter(x => x >= 4800 && x < 6000), [4800], 'the first pulse after Reset passes alone');
  });
  test(name + ': Chance gate is seeded, repeats on Reset and on a song start, and splits every pulse one way or the other', () => {
    const clk = pulse(100, 40), pat = r => rises(r.out).map(x => x / 100);
    const a = run(D, 'chance', { prob: .5, seed: 7 }, { in: clk }, 100), b = run(D, 'chance', { prob: .5, seed: 7 }, { in: clk }, 100), c = run(D, 'chance', { prob: .5, seed: 8 }, { in: clk }, 100);
    assert.deepEqual(pat(a), pat(b), 'same seed, same choices'); assert.notDeepEqual(pat(a), pat(c), 'another seed, other choices');
    const n = pat(a).length; assert.ok(n > 40 && n < 88, 'about half of 128 passed: ' + n);
    for (let i = 0; i < a.out.length; i++) assert.equal(a.out[i] + a.other[i], clk(i), 'each pulse leaves by exactly one output, whole');
    assert.equal(pat(run(D, 'chance', { prob: 1, seed: 3 }, { in: clk }, 20)).length, 26); assert.equal(pat(run(D, 'chance', { prob: 0, seed: 3 }, { in: clk }, 20)).length, 0);
    // Reset at pulse 50: the run starts over
    const r = pat(run(D, 'chance', { prob: .5, seed: 7 }, { in: clk, reset: i => i === 4990 ? 1 : 0 }, 100));
    assert.deepEqual(r.filter(k => k >= 50).map(k => k - 50), pat(a).filter(k => k < 78), 'after Reset: the same choices from the top');
    // a song start (startN counts up): the same again
    const R = runner(D, 'chance', { prob: .5, seed: 7 }), first = pat(R.run({ in: pulse(100, 40) }, 25, { tempo: 120, startN: 1 })), again = pat(R.run({ in: i => pulse(100, 40)(i - 3200) }, 25, { tempo: 120, startN: 2 }));
    assert.deepEqual(again, first, 'the second play makes the choices of the first');
    // Seed 0: not the same twice
    const z1 = pat(run(D, 'chance', { prob: .5, seed: 0 }, { in: clk }, 100)), z2 = pat(run(D, 'chance', { prob: .5, seed: 0 }, { in: clk }, 100)); assert.notDeepEqual(z1, z2);
  });
  test(name + ': Counter: up, down, up and down; End on the last count; Reset', () => {
    const clk = pulse(64, 8), at = (r, k) => r.step[k * 64 + 4];
    let r = run(D, 'counter', { steps: 4, mode: 0 }, { clock: clk }, 6); assert.deepEqual([0, 1, 2, 3, 4, 5].map(k => at(r, k)), [0, 1 / 3, 2 / 3, 1, 0, 1 / 3].map(Math.fround));
    assert.deepEqual(rises(r.end), [3 * 64, 7 * 64, 11 * 64], 'End with every fourth pulse');
    r = run(D, 'counter', { steps: 4, mode: 1 }, { clock: clk }, 6); assert.deepEqual([0, 1, 2, 3, 4].map(k => at(r, k)), [1, 2 / 3, 1 / 3, 0, 1].map(Math.fround));
    r = run(D, 'counter', { steps: 4, mode: 2 }, { clock: clk }, 6); assert.deepEqual([0, 1, 2, 3, 4, 5, 6, 7].map(k => at(r, k)), [0, 1 / 3, 2 / 3, 1, 2 / 3, 1 / 3, 0, 1 / 3].map(Math.fround));
    r = run(D, 'counter', { steps: 1 }, { clock: clk }, 2); for (const v of r.step) assert.equal(v, 0);
    r = run(D, 'counter', { steps: 4 }, { clock: clk, reset: i => i === 150 ? 1 : 0 }, 6); assert.equal(at(r, 2), Math.fround(2 / 3)); assert.equal(r.step[160], 0, 'Reset: back to the start at once'); assert.equal(at(r, 3), 0, 'and the next pulse is the first count again'); assert.equal(at(r, 4), Math.fround(1 / 3));
  });
  test(name + ': Step switch: one input at a time, in order, with a fade that never jumps', () => {
    const ins = { i1: () => .1, i2: () => .2, i3: () => .3, i4: () => .4, clock: pulse(256, 8, 256) };
    let r = run(D, 'stepsw', { steps: 4, fade: 0 }, ins, 12); assert.deepEqual([100, 300, 600, 800, 1100, 1300].map(i => r.out[i]), [.1, .2, .3, .4, .1, .2].map(Math.fround));
    r = run(D, 'stepsw', { steps: 3, fade: 0 }, ins, 12); assert.deepEqual([100, 300, 600, 800].map(i => r.out[i]), [.1, .2, .3, .1].map(Math.fround));
    r = run(D, 'stepsw', { steps: 4, fade: .005 }, ins, 12); let jump = 0; for (let i = 1; i < r.out.length; i++) jump = Math.max(jump, Math.abs(r.out[i] - r.out[i - 1])); assert.ok(jump <= .3 / 240 + 1e-6, 'largest step ' + jump);
    assert.equal(r.out[256 + 239], Math.fround(.2)); assert.equal(r.out[255], Math.fround(.1));
    r = run(D, 'stepsw', { steps: 4, fade: 0 }, Object.assign({}, ins, { reset: i => i === 700 ? 1 : 0 }), 8); assert.equal(r.out[699], Math.fround(.3)); assert.equal(r.out[700], Math.fround(.1));
  });
  test(name + ': Burst: Count pulses, Time apart, bent closer or wider, with a falling Level', () => {
    let r = run(D, 'burst', { count: 4, time: .01, bend: 0, width: .5 }, { trig: i => i === 10 ? 1 : 0 }, 30), e = rises(r.out);
    assert.deepEqual(e, [10, 490, 970, 1450]); assert.equal(r.out[10 + 239], 1); assert.equal(r.out[10 + 241], 0);
    assert.deepEqual(e.map(i => r.level[i]), [1, 2 / 3, 1 / 3, 0].map(Math.fround)); assert.equal(r.out[1450 + 480], 0); assert.equal(r.level[1450 + 480], 0);
    r = run(D, 'burst', { count: 5, time: .02, bend: 1 }, { trig: i => i === 0 ? 1 : 0 }, 40); e = rises(r.out); assert.equal(e.length, 5);
    const gaps = e.slice(1).map((x, k) => x - e[k]); for (let k = 1; k < gaps.length; k++) assert.ok(gaps[k] < gaps[k - 1], 'closer each time: ' + gaps.join(' '));
    r = run(D, 'burst', { count: 5, time: .005, bend: -1 }, { trig: i => i === 0 ? 1 : 0 }, 80); e = rises(r.out); const g2 = e.slice(1).map((x, k) => x - e[k]); assert.equal(e.length, 5); for (let k = 1; k < g2.length; k++) assert.ok(g2[k] > g2[k - 1], 'wider each time');
    r = run(D, 'burst', { count: 4, time: .01 }, { trig: i => i === 0 || i === 700 ? 1 : 0 }, 30); assert.deepEqual(rises(r.out), [0, 480, 1180, 1660, 2140], 'a new pulse starts the run again (it lands inside the second pulse, which just carries on into the new run)'); assert.equal(r.out[700 + 239], 1); assert.equal(r.out[700 + 241], 0);
    r = run(D, 'burst', { count: 1, time: .01 }, { trig: i => i === 5 ? 1 : 0 }, 10); assert.deepEqual(rises(r.out), [5]); assert.equal(r.level[5], 1);
  });
  test(name + ': Gate delay: later by Delay, its own length or a set one', () => {
    const g = i => i >= 100 && i < 400 ? 1 : 0;
    let r = run(D, 'gatedly', { delay: .01, length: 0 }, { in: g }, 12); assert.deepEqual(rises(r.out), [580]); assert.equal(r.out[879], 1); assert.equal(r.out[880], 0);
    r = run(D, 'gatedly', { delay: 0, length: 0 }, { in: g }, 6); assert.equal(r.out[100], 1); assert.equal(r.out[99], 0); assert.equal(r.out[400], 0);
    r = run(D, 'gatedly', { delay: .01, length: .001 }, { in: g }, 12); assert.deepEqual(rises(r.out), [580]); assert.equal(r.out[580 + 47], 1); assert.equal(r.out[580 + 48], 0);
    r = run(D, 'gatedly', { delay: 2, length: 0 }, { in: g }, 800); assert.deepEqual(rises(r.out), [100 + 96000]);
    assert.ok(D.MODULES.gatedly.create(SR).b.length <= SR * 2 + 4, 'two seconds of gate, a byte a sample');
  });
  test(name + ': Flip-flop: Toggle turns it over, Set and Reset place it, a song start clears it', () => {
    let r = run(D, 'flipflop', {}, { toggle: pulse(200, 20, 50) }, 8);
    assert.equal(r.q[0], 0); assert.equal(r.q[60], 1); assert.equal(r.q[260], 0); assert.equal(r.q[460], 1); for (let i = 0; i < r.q.length; i++) assert.equal(r.q[i] + r.nq[i], 1);
    r = run(D, 'flipflop', {}, { set: i => i === 30 ? 1 : 0, reset: i => i === 500 ? 1 : 0 }, 6); assert.equal(r.q[29], 0); assert.equal(r.q[30], 1); assert.equal(r.q[499], 1); assert.equal(r.q[500], 0);
    const R = runner(D, 'flipflop', {}); R.run({ toggle: i => i === 10 ? 1 : 0 }, 2, { tempo: 120, startN: 1 }); const again = R.run({}, 1, { tempo: 120, startN: 2 }); assert.equal(again.q[0], 0);
  });
}

test('chains engine: Clock into the Clock multiplier into the Counter, through Scale / map to Chain Out', () => {
  const D = BOTH[0][1], E = new D.ChainsEngine(SR, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  const m = (id, type, params) => ({ id, type, scope: 'mono', params });
  E.loadPatch({ modules: [m('k', 'clock', { div: 3, swing: 0, width: .5, bars: 1 }), m('x', 'clkmul', { mul: 2, width: .5 }), m('n', 'counter', { steps: 4, mode: 0 }), m('c', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['k', 'gate'], to: ['x', 'clock'] }, { from: ['x', 'out'], to: ['n', 'clock'] }, { from: ['n', 'step'], to: ['c', 'in'] }] });
  const seen = new Set(), z = new Float32Array(N); let last = -1, back = 0;
  for (let b = 0; b < 1500; b++) { E.setTrackInputs(z, z); E.process(N); for (const v of E.bus(1)) { assert.ok(Number.isFinite(v)); const q = Math.round(v * 3); seen.add(q); if (last >= 0 && q !== last && q !== (last + 1) % 4) back++; last = q; } }
  assert.deepEqual([...seen].sort(), [0, 1, 2, 3], 'the staircase visits every step'); assert.equal(back, 0, 'always the next step, never a skip');
});

test('waves engine: Steps and Clock run there now; a Burst fed by the note gate is per voice', () => {
  const D = wavesD(), E = new D.WavesEngine(SR, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch({ modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } },
    { id: 'b', type: 'burst', scope: 'poly', params: { count: 3, time: .02, bend: 0, width: .5 } }, { id: 'a', type: 'vca', scope: 'poly', params: { gain: 0 } },
    { id: 'k', type: 'clock', scope: 'mono', params: { div: 3, swing: 0, width: .5, bars: 1 } }, { id: 's', type: 'steps', scope: 'mono', params: { div: 3, len: 4, glide: 0, depth: 1, s1: .2, s2: .4, s3: .6, s4: .8, s5: 0, s6: 0, s7: 0, s8: 0 } },
    { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }, { id: 'o2', type: 'out', scope: 'mono', params: { slot: 2, level: 1 } }],
    cables: [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['h', 'gate'], to: ['b', 'trig'] }, { from: ['v', 'out'], to: ['a', 'in'] }, { from: ['b', 'out'], to: ['a', 'p:gain'] }, { from: ['a', 'out'], to: ['o', 'in'] },
      { from: ['k', 'gate'], to: ['s', 'clock'] }, { from: ['s', 'out'], to: ['o2', 'in'] }] });
  assert.equal(E.graph.mods.get('b').poly, true); assert.equal(E.graph.mods.get('s').poly, false);
  E.noteOn(60, 1);
  const vals = new Set(); let e = 0;
  for (let b = 0; b < 900; b++) { E.process(N); for (const v of E.bus(1)) { assert.ok(Number.isFinite(v)); e += v * v; } vals.add(Math.round(E.bus(2)[64] * 10)); }
  assert.ok(e > 0, 'the burst opened the voice'); for (const q of [2, 4, 6, 8]) assert.ok(vals.has(q), 'Steps reached ' + q / 10 + ' (saw ' + [...vals] + ')');
});
