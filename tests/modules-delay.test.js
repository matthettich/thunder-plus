// DELAY LINE (the owner's module queue): a bare delay with sample-exact feedback, capped and limited inside the loop.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const N = 128, SR = 48000;
const chainsD = () => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); return ctx.ChainsDSP; };
const wavesD = () => { const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/thunder-dsp.wasm'))), tfxBytes: new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'waves/dsp/tfx.wasm'))) }); return ctx.WavesDSP; };
function run(D, over, ins, blocks = 1, live) {
  const m = D.MODULES.dline, p = {}; for (const [k, d] of Object.entries(m.params)) p[k] = d[0]; Object.assign(p, over);
  const st = m.create(SR), io = { in: { in: new Float32Array(N), tmod: new Float32Array(N) }, out: { out: new Float32Array(N) }, p, linked: new Set(Object.keys(ins || {})) }, rec = new Float32Array(N * blocks);
  for (let b = 0; b < blocks; b++) { if (live) live(p, b); for (const k in io.in) { const s = ins && ins[k]; for (let i = 0; i < N; i++) io.in[k][i] = s ? s(b * N + i) : 0; } m.process(st, io, { block: N, sr: SR }); rec.set(io.out.out, b * N); }
  return rec;
}
const imp = i => i === 0 ? 1 : 0, sine = (hz, a = 1) => i => a * Math.sin(2 * Math.PI * hz * i / SR);
const peak = x => { let p = 0; for (const v of x) { assert.ok(Number.isFinite(v)); p = Math.max(p, Math.abs(v)); } return p; };

for (const [name, D] of [['chains', chainsD()], ['waves', wavesD()]]) {
  test(name + ': Delay line: only the delayed sound, exact to the sample', () => {
    assert.equal(D.MODULES.dline.scope, 'poly'); assert.doesNotMatch(D.MODULES.dline.process.toString(), /new |\.slice\(|\.map\(|=>\s*\{/);
    let r = run(D, { time: 10 }, { in: imp }, 6); assert.equal(r[0], 0, 'no dry sound'); assert.equal(r[480], 1); assert.equal(r[479], 0); assert.equal(r[481], 0);
    r = run(D, { time: 0 }, { in: imp }, 1); assert.equal(r[1], 1, 'the shortest delay is one sample'); assert.equal(r[0], 0);
    r = run(D, { time: 2000 }, { in: imp }, 760); assert.equal(r[96000], 1);
    r = run(D, { time: 1, fine: .5 }, { in: sine(1000) }, 20); const want = i => Math.sin(2 * Math.PI * 1000 * (i - 72) / SR); let err = 0; for (let i = 1000; i < r.length; i++) err = Math.max(err, Math.abs(r[i] - want(i))); assert.ok(err < 1e-4, 'Fine: 1.5 ms is 72 samples (' + err + ')');
    r = run(D, { time: 0, fine: .0302 }, { in: sine(500) }, 20); let e2 = 0; for (let i = 1000; i < r.length; i++) e2 = Math.max(e2, Math.abs(r[i] - Math.sin(2 * Math.PI * 500 * (i - 1.4496) / SR))); assert.ok(e2 < 2e-3, 'between samples: ' + e2);
  });
  test(name + ': feedback is inside, to the sample: echoes Time apart, each Feedback times the last; negative turns them over', () => {
    let r = run(D, { time: 5, fb: .5 }, { in: imp }, 12); assert.deepEqual([240, 480, 720, 960].map(i => r[i]), [1, .5, .25, .125]); assert.equal(r[300], 0);
    r = run(D, { time: 5, fb: -.5 }, { in: imp }, 12); assert.deepEqual([240, 480, 720].map(i => r[i]), [1, -.5, .25]);
    // 2 ms with feedback rings at 500 Hz: count the peaks of the ringing
    r = run(D, { time: 2, fb: .9 }, { in: imp }, 400); let n = 0; for (let i = 96; i < 48000; i += 96) if (r[i] > .001) n++; assert.ok(n > 40, 'it rings on at 1000 / 2 ms = 500 Hz (' + n + ' repeats heard)');
  });
  test(name + ': it cannot run away: Feedback stops at 99% and the loop is limited', () => {
    assert.ok(D.MODULES.dline.params.fb[2] <= .99 && D.MODULES.dline.params.fb[1] >= -.99);
    let r = run(D, { time: 3, fb: 5 }, { in: sine(333, 1) }, 3000); const pk = peak(r); assert.ok(pk <= 2.0001, 'a full-level tone on its own resonance with the slider forced past the top: peak ' + pk);
    r = run(D, { time: 3, fb: .99 }, { in: i => i < 4800 ? 50 : 0 }, 3000); assert.ok(peak(r) <= 2.0001, 'a very hot input: the loop holds 2'); assert.ok(Math.abs(r[r.length - 1]) < 1, 'and dies away after');
    r = run(D, { time: 3, fb: .9 }, { in: i => i === 7 ? NaN : i === 9 ? Infinity : 0 }, 20); peak(r);
    const quiet = run(D, { time: 3, fb: .5 }, { in: sine(700, .3) }, 100), big = run(D, { time: 3, fb: .5 }, { in: sine(700, .6) }, 100); let lin = 0; for (let i = 0; i < quiet.length; i++) lin = Math.max(lin, Math.abs(big[i] - 2 * quiet[i])); assert.ok(lin < 1e-5, 'inside -1..1 the limiter does nothing (' + lin + ')');
  });
  test(name + ': Damp dulls each repeat; the Time input moves the delay; turning Time glides', () => {
    const tail = d => { const r = run(D, { time: 1, fb: .95, damp: d }, { in: imp }, 200); let hf = 0; for (let i = 12000; i < r.length; i++) hf += Math.abs(r[i] - r[i - 1]); return hf; };
    assert.ok(tail(.6) < tail(0) * .2, 'the bright part of the ring goes first');
    let r = run(D, { time: 10, depth: 5 }, { in: imp, tmod: () => 1 }, 8); assert.equal(r[720], 1, '+5 ms at the input: 15 ms'); r = run(D, { time: 10, depth: 5 }, { in: imp, tmod: () => -1 }, 8); assert.equal(r[240], 1);
    r = run(D, { time: 10, depth: 0 }, { in: imp, tmod: () => 1 }, 8); assert.equal(r[480], 1, 'Mod depth 0: the input does nothing');
    // a slow wobble on a steady tone changes its pitch (vibrato): zero crossings no longer even
    r = run(D, { time: 10, depth: 2 }, { in: sine(1000), tmod: sine(5) }, 400); const gaps = []; let last = -1; for (let i = 4800; i < r.length; i++) if (r[i - 1] < 0 && r[i] >= 0) { if (last > 0) gaps.push(i - last); last = i; } assert.ok(Math.max(...gaps) - Math.min(...gaps) >= 2, 'vibrato');
    // moving the slider while a tone plays never jumps by more than the tone itself could
    r = run(D, { time: 20 }, { in: sine(200, .5) }, 300, (p, b) => { if (b === 100) p.time = 35; if (b === 200) p.time = 5; }); let j = 0; for (let i = 1; i < r.length; i++) j = Math.max(j, Math.abs(r[i] - r[i - 1])); assert.ok(j < .1, 'no click: largest step ' + j);
  });
}
test('waves: a Delay line fed by a voice is per voice (a string for each note)', () => {
  const D = wavesD(), E = new D.WavesEngine(SR, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch({ modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } }, { id: 'd', type: 'dline', scope: 'poly', params: { time: 4, fine: 0, fb: .8, damp: .2, depth: 0 } }, { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
    cables: [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['v', 'out'], to: ['d', 'in'] }, { from: ['d', 'out'], to: ['o', 'in'] }] });
  assert.equal(E.graph.mods.get('d').poly, true); E.noteOn(60, 1); E.noteOn(64, 1); let e = 0; for (let b = 0; b < 60; b++) { E.process(N); for (const v of E.bus(1)) { assert.ok(Number.isFinite(v)); e += v * v; } } assert.ok(e > 1);
});
test('waves: a short note through a long per-voice Delay line still gets its echo (the voice is kept awake for the delay)', () => {
  const D = wavesD(), E = new D.WavesEngine(SR, { voices: 4, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch({ modules: [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }, { id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } }, { id: 'e', type: 'adsr', scope: 'poly', params: { a: .001, d: .02, s: 0, r: .01 } }, { id: 'g', type: 'vca', scope: 'poly', params: { gain: 0 } },
    { id: 'd', type: 'dline', scope: 'poly', params: { time: 900, fine: 0, fb: 0, damp: 0, depth: 0 } }, { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } }],
    cables: [{ from: ['h', 'pitch'], to: ['v', 'pitch'] }, { from: ['h', 'gate'], to: ['e', 'gate'] }, { from: ['v', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'p:gain'] }, { from: ['g', 'out'], to: ['d', 'in'] }, { from: ['d', 'out'], to: ['o', 'in'] }] });
  assert.ok(E.graph.tails && E.graph.tails.length === 1);
  E.noteOn(60, 1); const out = new Float32Array(N * 560); for (let b = 0; b < 560; b++) { if (b === 8) E.noteOff(60); E.process(N); out.set(E.bus(1), b * N); }
  let early = 0, echo = 0; for (let i = 0; i < 40000; i++) early = Math.max(early, Math.abs(out[i])); for (let i = 43000; i < 48000; i++) echo = Math.max(echo, Math.abs(out[i]));
  assert.equal(early, 0, 'nothing before the delay'); assert.ok(echo > .05, 'the echo arrives 0.9 s later although the note ended long before (' + echo + ')');
  let late = 0; for (let i = N * 400; i < out.length; i++) late = Math.max(late, Math.abs(out[i])); assert.equal(late, 0);
  assert.ok(!E.voices.awake(0, SR * 12) || E.voices.rows[0].offAt < E.voices.now, 'and the voice is let go afterwards');
});
