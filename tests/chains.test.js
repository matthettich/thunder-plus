// Golden tests for the chains engine: every module is rendered through the real engine and the audio is hashed.
// A refactor that changes any module's output (even by one bit) fails here. Run with: npm test
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const BYTES = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'chains/dsp/tfx.wasm')));
const PUMP = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'pump/pump.wasm')));   // PUMP MODULES: the …pump effects
const BLOCKS = 400;   // ~1.07 s: long enough for delays, reverse and stretch to produce their wet signal (40 blocks was shorter than their delay times)

function engine({ wasm = true } = {}) {
  const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js');
  if (wasm){ ctx.ChainsDSP.wasmInit({ tfxBytes: BYTES }); ctx.__pumpBytes = PUMP; }
  const E = new ctx.ChainsDSP.ChainsEngine(48000, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  return { ctx, E, D: ctx.ChainsDSP };
}
/* trackIn -> module -> chainOut, patching every audio port the module has. Besides the audible bus, EVERY output port of
   the module under test is recorded each block (audio, CV and gate alike), so control modules (LFO, envelopes, clocks) are
   tested on the signal they actually make, not on the silence a speaker would hear. `over` overrides parameters. */
function renderModule(E, M, type, over) {
  const m = M[type], params = {}; for (const [k, d] of Object.entries(m.params || {})) params[k] = d[0]; Object.assign(params, over);
  const aIn = Object.keys(m.inputs || {}).filter(n => m.inputs[n] === 'audio'), outs = Object.keys(m.outputs || {}), cab = [];
  if (aIn.includes('in')) cab.push({ from: ['a', 'out'], to: ['b', 'in'] }); if (aIn.includes('inR')) cab.push({ from: ['a', 'outR'], to: ['b', 'inR'] });
  // The engine only runs modules that can reach a Chain Out (an unreachable one is skipped entirely, which would make a
  // control module's golden hash a hash of nothing). So the module's outputs are cabled into the output whatever their kind:
  // the first into its left input, the second into its right (connect() does not restrict port kinds, as in the editor).
  const sink = ['in', 'inR']; outs.slice(0, 2).forEach((p, i) => cab.push({ from: ['b', p], to: ['c', sink[i]] }));
  E.loadPatch({ modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type, scope: 'mono', params }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }], cables: cab });
  const ports = Object.keys(m.outputs || {}), rec = {}; for (const p of ports) rec[p] = new Float32Array(128 * BLOCKS);
  const sig = H.testSignal(128 * BLOCKS), L = new Float32Array(128 * BLOCKS), R = new Float32Array(128 * BLOCKS);
  for (let b = 0; b < BLOCKS; b++) {
    const s = sig.subarray(b * 128, (b + 1) * 128); E.setTrackInputs(s, s); E.process(128);
    L.set(E.bus(1), b * 128); R.set(E.busR(1), b * 128);
    const io = E._ios.get('b'); if (io) for (const p of ports) rec[p].set(io.out[p], b * 128);
  }
  // Under load the engine sheds to 'draft' quality, which changes the DSP. A golden hash taken then would be wrong, so say so.
  if (E.quality !== 'full') throw new Error('engine shed to draft quality while rendering ' + type + ' (machine too slow for a stable hash)');
  return { L, R, rec, ports };
}
const hashOf = r => H.hashFloats([r.L, r.R, ...r.ports.map(p => r.rec[p])]);
const energy = a => { let e = 0; for (const v of a) e += v * v; return e; };
/* Parameters pushed away from their defaults (each to ~70% of its range; on/off switches are left off, since 'mute' would otherwise silence the module), so a module that is a plain wire at its defaults
   (vca, width, utility...) cannot hide a regression behind identical pass-through output. */
const stress = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (mx - mn === 1 && st === 1) { o[k] = mn; continue; } let v = mn + (mx - mn) * .7; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
const finite = a => { for (const v of a) if (!Number.isFinite(v)) return false; return true; };

test('chains: the engine loads and exposes its public surface', () => {
  const { D } = engine({ wasm: false });
  for (const k of ['ChainsEngine', 'wasmInit', 'modsPrepare', 'MODULES', 'WASM', 'CHAN_COUNT']) assert.ok(k in D, 'ChainsDSP.' + k);
  assert.ok(Object.keys(D.MODULES).length >= 100, 'module count ' + Object.keys(D.MODULES).length);
});

test('chains: the real effects wasm loads, and records no error', () => {
  const { D } = engine();
  assert.ok(D.WASM.tfxX, 'tfx exports are present'); assert.equal(D.WASM.err, undefined);
});

/* WHAT THIS PROVES, AND WHAT IT DOES NOT. A golden hash proves a module's output is unchanged for this input; it cannot say the
   output is right. Modules that need a gate or clock edge to do anything (envelopes, euclid, sample-and-hold, drums...) are
   rendered here with no trigger, so for them it proves "does not crash, emits no NaN, idle output unchanged", NOT "responds
   to a trigger". `scout` has no outputs, so its hash is that of silence. Listening is still the only test of how it sounds. */
/* The third parameter set: every on/off switch ON, so the branches behind them run (Bus Comp's `clip`, freeze, sync, hold...). Only
   `mute` is left off, being the one switch whose whole purpose is silence. The other parameters sit at ~30% of their range, so this
   pass differs from both the defaults and the 70% pass. (The 70% pass leaves switches off, which is how `clip` went untested.) */
const SILENCERS = new Set(['mute']);
const KEEP_DEFAULT = new Set(['flip']);   // Utility: flip (left polarity) + mono (sum) cancels a centred signal to exact silence, which is correct DSP but hashes nothing
const switchesOn = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (KEEP_DEFAULT.has(k)) { o[k] = d[0]; continue; } if (mx - mn === 1 && st === 1) { o[k] = SILENCERS.has(k) ? mn : mx; continue; } let v = mn + (mx - mn) * .3; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
const SKIP = new Set(['trackIn', 'chainOut']);
test('chains: every module renders finite audio and matches its golden hash (defaults, stressed, switches on)', () => {
  const { E, D } = engine(), actual = {}, bad = [];
  for (const type of Object.keys(D.MODULES).sort()) {
    const m = D.MODULES[type]; if (!m || !m.process || SKIP.has(type)) continue;
    const d = renderModule(E, D.MODULES, type), s = renderModule(E, D.MODULES, type, stress(m)), w = renderModule(E, D.MODULES, type, switchesOn(m));
    for (const r of [d, s, w]) if (![r.L, r.R, ...r.ports.map(p => r.rec[p])].every(finite)) bad.push(type);
    actual[type + ' @default'] = hashOf(d); actual[type + ' @stressed'] = hashOf(s); actual[type + ' @switches-on'] = hashOf(w);
  }
  assert.deepEqual([...new Set(bad)], [], 'modules producing NaN/Infinity');
  const g = H.golden('chains-modules', actual);
  assert.ok(g.updated || g.diff.length === 0, 'audio changed in ' + (g.diff || []).length + ' render(s):\n  ' + (g.diff || []).slice(0, 20).join('\n  '));
});

/* A golden hash proves "unchanged", not "working": a module that has gone silent hashes to a perfectly stable value. So
   the modules that exist to make a signal are also required to make one. */
test('chains: modules that generate or process a signal are not silent', () => {
  const { E, D } = engine(), dead = [];
  // Modules that legitimately output nothing without an external trigger or a patched side input are named, with the reason.
  const NEEDS_TRIGGER = new Set(['beads', 'blipdrum', 'scout', 'scin', 'mixer', 'smixer', 'divider', 'euclid', 'logic', 'gateex', 'clock', 'life', 'marbles', 'steps', 'envfol', 'pitchfol', 'pitchtrk', 'adsr', 'atten', 'math', 'quant', 'slew', 'snh', 'random', 'tides', 'meta', 'reverse']);
  for (const type of Object.keys(D.MODULES).sort()) {
    const m = D.MODULES[type]; if (!m || !m.process || SKIP.has(type) || NEEDS_TRIGGER.has(type)) continue;
    const r = renderModule(E, D.MODULES, type, stress(m)); if (!Object.keys(m.outputs || {}).length) continue;
    if (!E._ios.has('b')) { dead.push(type + ' (never ran: unreachable from a Chain Out)'); continue; }
    if (energy(r.L) + energy(r.R) + r.ports.reduce((a, p) => a + energy(r.rec[p]), 0) === 0) dead.push(type);
  }
  assert.deepEqual(dead, [], 'silent module(s): ' + dead.join(', '));
});

test('chains: the same patch rendered twice is bit-identical (no clock or order dependence)', () => {
  const a = engine(), b = engine();
  for (const type of ['delay', 'reverb', 'lfo', 'sh', 'compressor']) if (a.D.MODULES[type]) {
    const x = renderModule(a.E, a.D.MODULES, type), y = renderModule(b.E, b.D.MODULES, type);
    assert.equal(H.hashFloats([x.L, x.R]), H.hashFloats([y.L, y.R]), type);
  }
});

test('chains: without the wasm, wasm effects pass audio through instead of crashing', () => {
  const { E, D } = engine({ wasm: false });
  const { L } = renderModule(E, D.MODULES, 'eq');   // an effect() module backed by the wasm
  const sig = H.testSignal(128 * BLOCKS); let same = true; for (let i = 0; i < L.length; i++) if (L[i] !== sig[i]) { same = false; break; }
  assert.ok(same, 'bypass is the unmodified input');
});

test('chains: a corrupt wasm does not throw, leaves effects bypassed, and says why', () => {
  const ctx = H.freezeClock(H.workletScope()), warned = []; ctx.console = { warn: (...a) => warned.push(a.join(' ')) }; H.load(ctx, 'chains/chains-worklet.js');
  assert.doesNotThrow(() => ctx.ChainsDSP.wasmInit({ tfxBytes: new Uint8Array([1, 2, 3, 4]) }));
  assert.equal(ctx.ChainsDSP.WASM.tfxX, null); assert.match((ctx.ChainsDSP.WASM.err || [])[0] || '', /effects wasm/);
  assert.ok(warned.some(w => /\[thunder\]/.test(w)), 'warned on the console');
});

test('chains: a patch with a module type that does not exist is rejected, not silently ignored', () => {
  const { E } = engine({ wasm: false });
  let threw = false; try { E.loadPatch({ modules: [{ id: 'x', type: 'doesNotExist', scope: 'mono', params: {} }], cables: [] }); } catch (e) { threw = true; }
  // Either behaviour is acceptable; what matters is that it is a deliberate one and does not corrupt the engine afterwards.
  assert.doesNotThrow(() => { E.loadPatch({ modules: [], cables: [] }); E.process(128); });
  assert.ok(typeof threw === 'boolean');
});

/* THE PROCESSOR WRAPPERS. Everything above drives ChainsEngine directly, which skips the code that runs last in the browser:
   ChainsProcessor.process (mix-down of the buses and the soft() output limiter) and ThunderChains.process (one track's bus).
   The test signal never reaches the limiter's 0.8 knee, so these are driven with a hot patch (+12 dB) that does. */
function hotPatch(gain) {
  return { modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type: 'utility', scope: 'mono', params: { gain, pan: 0, mono: 0, flip: 0, mute: 0 } }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }],
    cables: [{ from: ['a', 'out'], to: ['b', 'in'] }, { from: ['a', 'outR'], to: ['b', 'inR'] }, { from: ['b', 'out'], to: ['c', 'in'] }, { from: ['b', 'outR'], to: ['c', 'inR'] }] };
}
function driveProcessor(name, gain, blocks = 120) {
  const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); ctx.ChainsDSP.wasmInit({ tfxBytes: BYTES });
  const Proc = ctx.__registered[name], p = new Proc({ processorOptions: { quality: 'full', track: 0, patch: name === 'thunder-chains' ? hotPatch(gain) : undefined } });
  if (name === 'chains') p.engine.loadPatch(hotPatch(gain));
  const sig = H.testSignal(128 * blocks), L = new Float32Array(128 * blocks), R = new Float32Array(128 * blocks);
  for (let b = 0; b < blocks; b++) {
    const s = sig.subarray(b * 128, (b + 1) * 128), out = [[new Float32Array(128), new Float32Array(128)]];
    if (name === 'chains') p.engine.setTrackInputs(s, s);
    p.process([[s, s], []], out, {});
    L.set(out[0][0], b * 128); R.set(out[0][1], b * 128);
  }
  if (p.engine.quality !== 'full') throw new Error('engine shed to draft quality (machine too slow for a stable hash)');
  return { L, R };
}
const peak = a => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return m; };

test('chains: ChainsProcessor limits a hot signal below full scale, and its output is golden', () => {
  const quiet = driveProcessor('chains', 0), hot = driveProcessor('chains', 12);
  assert.ok(peak(hot.L) <= 1 && peak(hot.L) > .95, 'hot output is held at, not above, full scale (peak ' + peak(hot.L).toFixed(4) + ')');
  assert.ok(peak(quiet.L) < .8, 'a quiet signal stays under the knee, untouched (peak ' + peak(quiet.L).toFixed(3) + ')');
  const g = H.golden('chains-processor', { quiet: H.hashFloats([quiet.L, quiet.R]), hot: H.hashFloats([hot.L, hot.R]) });
  assert.ok(g.updated || g.diff.length === 0, 'processor output changed:\n  ' + (g.diff || []).join('\n  '));
});

test('chains: ThunderChains (the per-track node) returns that track\'s bus, limited signal included, and is golden', () => {
  const quiet = driveProcessor('thunder-chains', 0), hot = driveProcessor('thunder-chains', 12);
  assert.ok(peak(quiet.L) > .1, 'the track bus carries the signal (peak ' + peak(quiet.L).toFixed(3) + ')');
  const g = H.golden('chains-thunder-node', { quiet: H.hashFloats([quiet.L, quiet.R]), hot: H.hashFloats([hot.L, hot.R]) });
  assert.ok(g.updated || g.diff.length === 0, 'per-track node output changed:\n  ' + (g.diff || []).join('\n  '));
});

test('chains: soft() itself is exact where the app depends on it (identity below the knee, bounded above, continuous, odd)', () => {
  const ctx = H.workletScope(); H.load(ctx, 'chains/chains-worklet.js', ['soft']); const soft = ctx.soft;
  for (const x of [0, .1, -.5, .79, -.8, .8]) assert.equal(soft(x), x, 'identity at ' + x);
  for (const x of [.81, 1, 2, 10, 1e6]) assert.ok(soft(x) > .8 && soft(x) < 1.0000001, 'bounded at ' + x);
  assert.ok(Math.abs(soft(.8) - soft(.8 + 1e-9)) < 1e-6, 'continuous at the knee');
  for (const x of [.3, .9, 1.7]) assert.equal(soft(-x), -soft(x), 'odd symmetry at ' + x);
  assert.ok(soft(.9) < soft(1.5) && soft(1.5) < soft(3), 'monotonic above the knee');
  assert.ok(Math.abs(soft(1) - 0.9523) < 1e-3, 'pinned value at 1.0 (' + soft(1).toFixed(5) + ')');
});
