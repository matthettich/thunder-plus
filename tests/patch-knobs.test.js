// Patch knobs (22 Nov): the Knob module, the thunder-chains k1-k8 params, the KN1-KN8 tracker commands and the editor entries.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');

const loadWorklet = (rel, expose) => { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, rel, expose); return ctx; };
const run = (Knob, p, knobs, st, len = 128) => {
  const out = new Float32Array(len);
  Knob.process(st, { p, out: { out } }, { engine: knobs === undefined ? undefined : { knobs }, block: len });
  return out;
};
const mkKnobs = (o = {}) => { const k = new Float32Array(9).fill(-1); for (const i in o) k[i] = o[i]; return k; };
const P = (o = {}) => Object.assign({ n: 1, value: 0.5, min: 0, max: 1 }, o);
const approx = (a, b, e = 1e-6) => assert.ok(Math.abs(a - b) < e, `${a} vs ${b}`);

const { Knob } = loadWorklet('chains/chains-worklet.js', ['Knob']);

test('Knob: with no knobs (or -1) it outputs Value mapped to Min..Max, flat', () => {
  for (const K of [undefined, mkKnobs()]) {
    const st = Knob.create(), o = run(Knob, P({ value: .25, min: -1, max: 3 }), K, st);
    for (const v of o) approx(v, 0);        // -1 + 4 * .25
    approx(run(Knob, P({ value: 1, min: 2, max: 6 }), K, Knob.create())[5], 6);
  }
});
test('Knob: follows engine.knobs[n] when >= 0 and ignores other knobs', () => {
  const o = run(Knob, P({ n: 3, value: 0, min: 10, max: 20 }), mkKnobs({ 3: .5, 1: 1 }), Knob.create());
  for (const v of o) approx(v, 15);
  const z = run(Knob, P({ n: 2, value: .5 }), mkKnobs({ 2: 0 }), Knob.create());   // 0 is "set", not "absent"
  approx(z[0], 0);
  approx(run(Knob, P({ n: 1, value: 0 }), mkKnobs({ 1: 7 }), Knob.create())[0], 1, 1e-6);   // over 1 is clamped
});
test('Knob: n is rounded and clamped to 1..8', () => {
  const K = mkKnobs({ 1: .1, 8: .9 });
  approx(run(Knob, P({ n: 0 }), K, Knob.create())[0], .1);
  approx(run(Knob, P({ n: -5 }), K, Knob.create())[0], .1);
  approx(run(Knob, P({ n: 99 }), K, Knob.create())[0], .9);
  approx(run(Knob, P({ n: 7.6 }), K, Knob.create())[0], .9);
  approx(run(Knob, P({ n: NaN }), K, Knob.create())[0], .1);
});
test('Knob: first block jumps; a change then glides linearly across the block', () => {
  const st = Knob.create(), K = mkKnobs({ 1: 0 });
  const first = run(Knob, P(), K, st); assert.ok(first.every(v => v === 0), 'first block is flat');
  K[1] = 1;
  const g = run(Knob, P(), K, st);
  assert.notEqual(g[0], 1, 'first sample is not yet the target');
  assert.ok(g[0] > 0 && g[0] < .05);
  assert.equal(g[g.length - 1], 1, 'last sample equals the target');
  for (let i = 1; i < g.length; i++) assert.ok(g[i] > g[i - 1], 'monotonic');
  const flat = run(Knob, P(), K, st); assert.ok(flat.every(v => v === 1), 'then flat again');
});

test('thunder-chains: parameterDescriptors has b1-b15 unchanged and k1-k8 default -1', () => {
  const ctx = loadWorklet('chains/chains-worklet.js');
  assert.ok(ctx.__registered['thunder-chains'], 'registered');
  const d = JSON.parse(JSON.stringify(ctx.__registered['thunder-chains'].parameterDescriptors)), by = Object.fromEntries(d.map(x => [x.name, x]));
  for (let i = 1; i <= 15; i++) assert.deepEqual(by['b' + i], { name: 'b' + i, defaultValue: -1, minValue: -1, maxValue: 2, automationRate: 'k-rate' });
  for (let i = 1; i <= 8; i++) assert.deepEqual(by['k' + i], { name: 'k' + i, defaultValue: -1, minValue: -1, maxValue: 1, automationRate: 'k-rate' });
  assert.equal(d.length, 23);
  assert.equal(new Set(d.map(x => x.name)).size, 23);
});
test('ChainsEngine: has knobs Float32Array(9) filled with -1, and a patched Knob reaches the output', () => {
  const ctx = loadWorklet('chains/chains-worklet.js');
  const E = new ctx.ChainsDSP.ChainsEngine(48000, { focus: -1, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  assert.ok(E.knobs instanceof Float32Array && E.knobs.length === 9 && E.knobs.every(v => v === -1));
  const mod = (id, type, params) => ({ id, type, scope: 'mono', params: params || {} });
  const patch = { modules: [mod('k', 'knob', { n: 2, value: .5, min: 0, max: 1 }), mod('o', 'chainOut', { track: 1, level: 1 })],
    cables: [{ from: ['k', 'out'], to: ['o', 'in'] }] };
  E.loadPatch(patch);
  const B = 128, mean = () => { const x = new Float32Array(B); E.setTrackInputs(x, x); E.process(B); const b = E.bus(1); let s = 0; for (const v of b) s += v; return s / b.length; };
  for (let i = 0; i < 30; i++) mean();
  const a = mean(); E.knobs[2] = 1;
  let b = 0; for (let i = 0; i < 30; i++) b = mean();
  assert.ok(Math.abs(b) > Math.abs(a) * 1.5 && b > 0, `knob 2 at 1 is louder than Value .5 (${a} -> ${b})`);
  E.knobs[2] = -1; let c = 0; for (let i = 0; i < 30; i++) c = mean();
  approx(c, a, 1e-4);
});
test('waves worklet: has the same Knob module (no engine knobs: uses Value)', () => {
  const ctx = loadWorklet('waves/waves-worklet.js', ['Knob']);
  assert.equal(ctx.Knob.type, 'knob');
  assert.deepEqual(JSON.parse(JSON.stringify(ctx.Knob.params)), JSON.parse(JSON.stringify(Knob.params)));
  const o = run(ctx.Knob, P({ value: .75 }), undefined, ctx.Knob.create()); approx(o[0], .75);
});

const src = H.read('index.html'), core = H.read('core/seq-core.js');
const KN = Array.from({ length: 8 }, (_, i) => 'KN' + (i + 1));
const fxcOf = s => { const m = s.match(/const FXC = \[([\s\S]*?)\];/); assert.ok(m, 'FXC found'); return m[1].match(/'([^']+)'/g).map(x => x.slice(1, -1)); };

test('FXC ends with ...CHD, KN1..KN8, FLZ in order (saved songs keep their numbers) in index.html and core', () => {
  for (const s of [src, core]) {
    const f = fxcOf(s), i = f.indexOf('CHD');
    assert.ok(i > 0);
    assert.deepEqual(f.slice(i), ['CHD', ...KN, 'FLZ']);   // FLZ (23 Nov) goes after them
    assert.equal(f.filter(x => x === 'KN1').length, 1);
  }
  assert.deepEqual(fxcOf(src), fxcOf(core), 'core FXC matches the app');
});
test('KN1-KN8 are in the picker group, tooltip table, help text and describer; not in MST_FXC', () => {
  const grp = src.match(/\['Effects and FX patterns', \[([^\]]*)\]\]/); assert.ok(grp);
  const g = grp[1].match(/'([^']+)'/g).map(x => x.slice(1, -1));
  for (const k of KN) assert.ok(g.includes(k), k + ' in group');
  assert.ok(src.includes("const FXB = {") && /\.\.\.Object\.fromEntries\(\[1, 2, 3, 4, 5, 6, 7, 8\]\.map\(k => \['KN' \+ k, `Knob K\$\{k\}/.test(src), 'tooltip table generates KN1-8');
  assert.ok(/<code>KN1<\/code>[^<]*<code>KN8<\/code>|<code>KN1<\/code>–<code>KN8<\/code>/.test(src), 'help text');
  assert.ok(/\['KN' \+ k, v => `Set knob K\$\{k\}/.test(src), 'describer generates KN1-8');
  const mst = src.match(/const MST_FXC = \[([^\]]*)\]/); assert.ok(mst);
  for (const k of KN) assert.ok(!mst[1].includes("'" + k + "'"), k + ' not in MST_FXC');
});
test('sequencer emits chc k-events (v/255) for KN1-KN8 on tracks only, in index.html and core', () => {
  for (const s of [src, core]) {
    assert.ok(s.includes("if (t < MT) for (let k = 1; k <= 8; k++){ const v = fx['KN' + k]; if (v != null) evs.push({type:'chc', t:at, track:t, p:'k' + k, v:v / 255}); }"));
  }
  // run the emission line for a sample row
  const line = core.match(/if \(t < MT\) for \(let k = 1; k <= 8; k\+\+\)\{ const v = fx\['KN' \+ k\][^\n]*/)[0];
  const run1 = (t, fx) => { const evs = []; new Function('t', 'MT', 'fx', 'evs', 'at', line)(t, 8, fx, evs, 5); return evs; };
  assert.deepEqual(run1(2, { KN3: 255, KN8: 0, KN1: 51 }), [{ type: 'chc', t: 5, track: 2, p: 'k1', v: .2 }, { type: 'chc', t: 5, track: 2, p: 'k3', v: 1 }, { type: 'chc', t: 5, track: 2, p: 'k8', v: 0 }]);
  assert.deepEqual(run1(9, { KN1: 1 }), [], 'not on the master column');
  assert.deepEqual(run1(0, {}), []);
});
test('stop resets b1-b15 and k1-k8 to -1 via CHN_RESET', () => {
  const m = src.match(/const CHN_RESET = \[([^\]]*)\]/); assert.ok(m);
  const r = m[1].match(/'([^']+)'/g).map(x => x.slice(1, -1));
  assert.deepEqual(r, [...Array.from({ length: 15 }, (_, i) => 'b' + (i + 1)), ...Array.from({ length: 8 }, (_, i) => 'k' + (i + 1))]);
  assert.ok(/for \(const k of CHN_RESET\)\{ const q = b\.chn\.parameters\.get\(k\); if \(q\) try \{ q\.cancelScheduledValues\(at\); q\.setValueAtTime\(-1, at\)/.test(src), 'stop code sets -1');
});

test('editor Knob entry params equal the worklet params (chains and waves)', () => {
  const wp = JSON.stringify(Knob.params);
  for (const f of ['chains/index.html', 'waves/index.html']) {
    const s = H.read(f), m = s.match(/\n  knob:\{ label:'Knob'[\s\S]*?params:(\{[^\n]*\}),\n/);
    assert.ok(m, f + ': knob entry');
    assert.equal(JSON.stringify(new Function('return ' + m[1])()), wp, f);
    assert.ok(/knob:'mod'/.test(s), f + ': PAL_LAYOUT Control utilities / palette');
    assert.ok(/knob:'tracker KN1–KN8'/.test(s), f + ': PAL_HINT');
    assert.ok(/knob:\['n', 'value'\]/.test(s), f + ': SUMMARY_KEYS');
    assert.ok(/knob:'Knob 1 into/.test(s), f + ': use text');
    assert.ok(/knob:NARROW_M/.test(s), f + ': width');
  }
});
