// U16: THE REFERENCE OF THE FOUR JS-ONLY TRACK EFFECTS (exact, Node only): the real ThunderFx worklet (the TFX_SRC template of index.html with GrainsCore, ScrambleCore,
// GlitchCore, VSampCore, beatLock and bkHash cut out of index.html as the page has them) run in Node on deterministic stereo signals and a beat clock made here, for
// Beat grains (100), Beat scramble (101), Glitch sequencer (102) and the Vintage sampler (103): default and other parameter sets, every control at its extremes, random
// parameter sets, tempo changes, the clock stopping / jumping / absent, FXO, chain sets, FXV, odd block sizes, the node's sleep rules, the four in series.
// It reuses the machinery of fx-clk-dump.mjs (the clock, the signals, runCase) and writes the same file kinds:
//   node native/tools/fx-idm-dump.mjs [outDir]   writes <outDir>/<case>.json (the case), <case>.in.f32 (L then R), <case>.bp.f32 (the clock param: one float per frame),
//                                                <case>.f32 (the output, L then R), maps.json (TFX map() on 200 value sets per effect)
//   node native/tools/fx-idm-dump.mjs --golden   writes native/tests/golden/fxidm_golden.inc (FNV-1a 64 of every case's output)
//   node native/tools/fx-idm-dump.mjs --check    exits 1 if the committed fxidm_golden.inc differs
// WHERE THE REFERENCE COMES FROM: the cores call Math.exp / pow / sin / cos / log / tanh. Node's Math.* is not the same on every platform (macOS arm64 rounds some results
// differently), so the committed golden is made on x86-64 Linux, Node 22, and the C++ is compared with it on every platform (the same rule as libm_golden: see U16-NOTES.md).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROOT, SR, load, idmEffects } from './fx-src.mjs';
import { lcg, f32b, fnv } from './fx-dump.mjs';
import { SIG, runCase, hex, BLK } from './fx-clk-dump.mjs';

const F = load(), E = idmEffects(), byKey = Object.fromEntries(E.map(e => [e.key, e]));
const run = (bpm, beat = 0) => [{ at: 0, bpm, beat }];
const run120 = run(120);
const t = f => f / SR;

// louder signals for the Vintage sampler's converter (clipping, DRIVE) and a very quiet one (the hiss)
SIG.loud = n => { const [L, R] = SIG.loop(n); for (let i = 0; i < n; i++) { L[i] = Math.fround(L[i] * 3.2); R[i] = Math.fround(R[i] * 2.6); } return [L, R]; };
SIG.quiet = n => { const [L, R] = SIG.steady(n); for (let i = 0; i < n; i++) { L[i] = Math.fround(L[i] * 0.01); R[i] = Math.fround(R[i] * 0.004); } return [L, R]; };

const fresh = (type, over = {}) => { const o = { type, on: true }; for (const p of byKey[type].ps) o[p.k] = p.d; return Object.assign(o, over); };
const grains = (o = {}) => fresh('grains', o), scramble = (o = {}) => fresh('scramble', o), glitch = (o = {}) => fresh('glitch', o), vsamp = (o = {}) => fresh('vsamp', o);
// a random set of values over each control's whole range
const randSet = (type, seed, over = {}) => { const r = lcg(seed * 7919 + byKey[type].id), o = { type, on: true }; for (const p of byKey[type].ps) o[p.k] = p.min + Math.floor(r() * (p.max - p.min + 1)); return Object.assign(o, over); };
function chainOf(objs, bpm = 120) { const A = new Array(8).fill(null); objs.forEach((o, i) => { A[i] = o && F.normTfx(o); }); return F.tfxChain(A, SR, bpm); }

const CASES = [];
const add = c => CASES.push(Object.assign({ sig: 'loop', frames: 96000, clock: run120, bpMode: 'full' }, c));

// ---- Beat grains --------------------------------------------------------------------------------------------------------------------------
add({ name: 'grains_def', chain: chainOf([grains()]) });
add({ name: 'grains_def_steady', sig: 'steady', chain: chainOf([grains()]) });
for (let d = 0; d <= 6; d++) add({ name: `grains_div${d}`, chain: chainOf([grains({ div: d, density: 255, size: 90 })]) });                          // every RATE, with the second start (density > 0.6)
for (let r = 0; r <= 3; r++) add({ name: `grains_rpitch${r}`, frames: 144000, chain: chainOf([grains({ rpitch: r, div: 2, density: 230, pitch: 24 + (r ? 0 : 7), spread: 255 })]) });   // RANDOM PITCH: off, octaves, octaves + 5ths, any
add({ name: 'grains_rev', chain: chainOf([grains({ rev: 255, div: 2, density: 255, spread: 0 })]) });
add({ name: 'grains_rev_half', chain: chainOf([grains({ rev: 128, div: 3, density: 200, pitch: 31, spread: 200 })]) });
add({ name: 'grains_pitch_down', chain: chainOf([grains({ pitch: 0, div: 1, density: 255, size: 255 })]) });
add({ name: 'grains_shape0', chain: chainOf([grains({ shape: 0, div: 2, density: 255, size: 128 })]) });
add({ name: 'grains_shape255', chain: chainOf([grains({ shape: 255, div: 2, density: 255, size: 128 })]) });
add({ name: 'grains_size_max', chain: chainOf([grains({ size: 255, div: 3, density: 255 })]) });                                                      // 24 voices: the oldest are dropped
add({ name: 'grains_size_min', chain: chainOf([grains({ size: 0, div: 3, density: 255 })]) });
add({ name: 'grains_spray', chain: chainOf([grains({ pos: 0, spray: 255, div: 2, density: 255 })]) });
add({ name: 'grains_pos_far', clock: run(30), frames: 144000, chain: chainOf([grains({ pos: 255, spray: 255, div: 0, density: 255 })]) });              // BACK past the ring: the grain is not started
add({ name: 'grains_density0', chain: chainOf([grains({ density: 0 })]) });
add({ name: 'grains_density_mid', chain: chainOf([grains({ density: 150, div: 2 })]) });
add({ name: 'grains_mix0', chain: chainOf([grains({ mix: 0 })]) });
add({ name: 'grains_freeze', frames: 144000, chain: chainOf([grains({ freeze: 1, div: 2, density: 255 })]), msgs: [{ b: 150, msg: chainOf([grains({ freeze: 1, div: 2, density: 255, mix: 255 })]) }] });
add({ name: 'grains_freeze_late', frames: 144000, chain: chainOf([grains({ freeze: 0, div: 2, density: 255 })]), msgs: [{ b: 300, msg: chainOf([grains({ freeze: 1, div: 2, density: 255 })]) }, { b: 800, msg: chainOf([grains({ freeze: 0, div: 2, density: 255 })]) }] });
for (let i = 0; i < 12; i++) add({ name: `grains_rand${i}`, frames: 120000, sig: i % 2 ? 'loop' : 'steady', clock: run(100 + 37 * i, i * 1.3), chain: chainOf([randSet('grains', i + 1)]) });
add({ name: 'grains_stopped', clock: [{ at: 0, stop: true }], chain: chainOf([grains({ div: 2, density: 255 })]) });                                       // free-runs at the last tempo
add({ name: 'grains_stops', frames: 150000, clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 50001, stop: true }, { at: 80000, bpm: 150, beat: 6.5 }, { at: 110000, stop: true }, { at: 110500, bpm: 140, beat: 1 }], chain: chainOf([grains({ div: 2, density: 230, rpitch: 2 })]) });
add({ name: 'grains_tempo', frames: 240000, clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 48000, bpm: 150 }, { at: 96000, bpm: 90 }, { at: 144000, bpm: 91.5 }, { at: 192000, bpm: 180 }], chain: chainOf([grains({ div: 2, density: 230, rpitch: 1 })]) });
add({ name: 'grains_jump', frames: 180000, clock: [{ at: 0, bpm: 120, beat: 0.25 }, { at: 70000, bpm: 120, beat: 1.75 }, { at: 120000, bpm: 120, beat: 15.9 }], chain: chainOf([grains({ div: 3, density: 230 })]) });
add({ name: 'grains_bp_const', bpMode: 'const', chain: chainOf([grains({ div: 2, density: 230 })]) });
add({ name: 'grains_bp_none', bpMode: 'none', chain: chainOf([grains({ div: 2, density: 230 })]) });
add({ name: 'grains_fxo', frames: 96000, chain: chainOf([grains({ on: false, div: 2, density: 255 })]), ops: [['sw0', t(10000), 1], ['sw0', t(40000), 0], ['sw0', t(60000), -1], ['sw0', t(75000), 1]] });

// ---- Beat scramble --------------------------------------------------------------------------------------------------------------------------
add({ name: 'scramble_def', clock: run(480), frames: 192000, chain: chainOf([scramble()]) });
for (let d = 0; d <= 2; d++) add({ name: `scramble_div${d}`, clock: run(480), frames: 192000, chain: chainOf([scramble({ div: d, chance: 200, rev: 90 })]) });
for (let v = 0; v <= 2; v++) add({ name: `scramble_vary${v}`, clock: run(480), frames: 288000, chain: chainOf([scramble({ vary: v, chance: 180, rev: 80, div: 1 })]) });   // three wraps of the clock (32 beats and more)
for (const [nm, o] of [['keep0', { keep: 0 }], ['keep1', { keep: 1 }], ['chance0', { chance: 0 }], ['chance255', { chance: 255, keep: 0 }], ['rev255', { rev: 255, chance: 255, keep: 0 }], ['rev0', { rev: 0, chance: 255, keep: 0 }],
  ['seed1', { seed: 1, chance: 150 }], ['seed77', { seed: 77, chance: 150 }], ['seed255', { seed: 255, chance: 150 }], ['xf0', { xf: 0, chance: 200 }], ['xf255', { xf: 255, chance: 200 }], ['mix128', { mix: 128, chance: 200 }], ['mix0', { mix: 0 }]])
  add({ name: `scramble_${nm}`, clock: run(480), frames: 192000, chain: chainOf([scramble(Object.assign({ div: 1, keep: 0 }, o))]) });
for (let i = 0; i < 12; i++) add({ name: `scramble_rand${i}`, frames: 200000, sig: i % 2 ? 'loop' : 'steady', clock: run(300 + 25 * i, i * 1.3), chain: chainOf([randSet('scramble', i + 1)]) });
add({ name: 'scramble_stopped', clock: [{ at: 0, stop: true }], chain: chainOf([scramble({ chance: 255 })]) });
add({ name: 'scramble_stops', frames: 200000, clock: [{ at: 0, bpm: 480, beat: 0 }, { at: 50001, stop: true }, { at: 80000, bpm: 480, beat: 6.5 }, { at: 110000, stop: true }, { at: 110500, bpm: 400, beat: 1 }], chain: chainOf([scramble({ div: 1, chance: 200, keep: 0 })]) });
add({ name: 'scramble_tempo', frames: 240000, clock: [{ at: 0, bpm: 240, beat: 0 }, { at: 48000, bpm: 300 }, { at: 96000, bpm: 180 }, { at: 144000, bpm: 182 }, { at: 192000, bpm: 360 }], chain: chainOf([scramble({ div: 1, chance: 220, keep: 0 })]) });
add({ name: 'scramble_jump', frames: 180000, clock: [{ at: 0, bpm: 480, beat: 0.25 }, { at: 70000, bpm: 480, beat: 9.75 }, { at: 120000, bpm: 480, beat: 15.9 }], chain: chainOf([scramble({ div: 2, chance: 220, keep: 0 })]) });
add({ name: 'scramble_wrap', frames: 192000, clock: run(480, 13), chain: chainOf([scramble({ div: 1, chance: 200, vary: 0 })]) });
add({ name: 'scramble_bp_const', bpMode: 'const', chain: chainOf([scramble({ chance: 200 })]) });
add({ name: 'scramble_bp_none', bpMode: 'none', chain: chainOf([scramble({ chance: 200 })]) });
add({ name: 'scramble_fxo', frames: 192000, clock: run(480), chain: chainOf([scramble({ on: false, chance: 255, keep: 0 })]), ops: [['sw0', t(20000), 1], ['sw0', t(60000), 0], ['sw0', t(90000), -1], ['sw0', t(120000), 1]] });

// ---- Glitch sequencer -----------------------------------------------------------------------------------------------------------------------
add({ name: 'glitch_def', clock: run(480), frames: 192000, chain: chainOf([glitch()]) });
for (let d = 0; d <= 2; d++) add({ name: `glitch_div${d}`, clock: run(480), frames: 192000, chain: chainOf([glitch({ div: d, chance: 255 })]) });
for (let v = 0; v <= 2; v++) add({ name: `glitch_vary${v}`, clock: run(480), frames: 288000, chain: chainOf([glitch({ vary: v, chance: 200, div: 1 })]) });
// each action alone: STUTTER, REVERSE, OCTAVE (up and down), TAPE STOP, CRUSH, DROPOUT
for (const k of ['stut', 'rev', 'pitch', 'tape', 'crush', 'gate']) {
  const only = { stut: 0, rev: 0, pitch: 0, tape: 0, crush: 0, gate: 0, chance: 255, [k]: 255 };
  for (const d of [0, 1, 2]) add({ name: `glitch_only_${k}_d${d}`, clock: run(480), frames: 144000, chain: chainOf([glitch(Object.assign({ div: d }, only))]) });
}
for (const [nm, o] of [['chance0', { chance: 0 }], ['seed1', { seed: 1, chance: 180 }], ['seed99', { seed: 99, chance: 180 }], ['seed255', { seed: 255, chance: 180 }], ['mix128', { mix: 128, chance: 255 }], ['mix0', { mix: 0, chance: 255 }],
  ['weights0', { stut: 0, rev: 0, pitch: 0, tape: 0, crush: 0, gate: 0, chance: 255 }]])
  add({ name: `glitch_${nm}`, clock: run(480), frames: 192000, chain: chainOf([glitch(Object.assign({ div: 1 }, o))]) });
for (let i = 0; i < 12; i++) add({ name: `glitch_rand${i}`, frames: 200000, sig: i % 2 ? 'loop' : 'steady', clock: run(300 + 25 * i, i * 1.3), chain: chainOf([randSet('glitch', i + 1)]) });
add({ name: 'glitch_stopped', clock: [{ at: 0, stop: true }], chain: chainOf([glitch({ chance: 255 })]) });
add({ name: 'glitch_stops', frames: 200000, clock: [{ at: 0, bpm: 480, beat: 0 }, { at: 50001, stop: true }, { at: 80000, bpm: 480, beat: 6.5 }, { at: 110000, stop: true }, { at: 110500, bpm: 400, beat: 1 }], chain: chainOf([glitch({ div: 1, chance: 255 })]) });
add({ name: 'glitch_tempo', frames: 240000, clock: [{ at: 0, bpm: 240, beat: 0 }, { at: 48000, bpm: 300 }, { at: 96000, bpm: 180 }, { at: 144000, bpm: 182 }, { at: 192000, bpm: 360 }], chain: chainOf([glitch({ div: 1, chance: 255 })]) });
add({ name: 'glitch_jump', frames: 180000, clock: [{ at: 0, bpm: 480, beat: 0.25 }, { at: 70000, bpm: 480, beat: 9.75 }, { at: 120000, bpm: 480, beat: 15.9 }], chain: chainOf([glitch({ div: 2, chance: 255 })]) });
add({ name: 'glitch_bp_const', bpMode: 'const', chain: chainOf([glitch({ chance: 255 })]) });
add({ name: 'glitch_bp_none', bpMode: 'none', chain: chainOf([glitch({ chance: 255 })]) });
add({ name: 'glitch_fxo', frames: 192000, clock: run(480), chain: chainOf([glitch({ on: false, chance: 255 })]), ops: [['sw0', t(20000), 1], ['sw0', t(60000), 0], ['sw0', t(90000), -1], ['sw0', t(120000), 1]] });

// ---- Vintage sampler ------------------------------------------------------------------------------------------------------------------------
add({ name: 'vsamp_def', bpMode: 'none', chain: chainOf([vsamp()]) });
for (let m = 0; m <= 7; m++) {
  add({ name: `vsamp_model${m}`, bpMode: 'none', chain: chainOf([vsamp({ model: m })]) });
  add({ name: `vsamp_model${m}_raw`, bpMode: 'none', sig: 'steady', chain: chainOf([vsamp({ model: m, filter: 0, noise: 200 })]) });
  add({ name: `vsamp_model${m}_loud`, bpMode: 'none', sig: 'loud', chain: chainOf([vsamp({ model: m, drive: 200, filter: 255, rate: 0, noise: 0 })]) });
  add({ name: `vsamp_model${m}_quiet`, bpMode: 'none', sig: 'quiet', chain: chainOf([vsamp({ model: m, filter: 60, rate: 100, noise: 255 })]) });
}
for (const [nm, o] of [['drive0', { drive: 0 }], ['drive255', { drive: 255 }], ['filter1', { filter: 1 }], ['filter255', { filter: 255 }], ['rate0', { rate: 0 }], ['rate128', { rate: 128 }], ['noise0', { noise: 0 }], ['noise255', { noise: 255 }], ['mix0', { mix: 0 }], ['mix128', { mix: 128 }]])
  for (const m of [0, 2, 7]) add({ name: `vsamp_${nm}_m${m}`, bpMode: 'none', sig: nm.startsWith('drive') ? 'loud' : 'loop', chain: chainOf([vsamp(Object.assign({ model: m }, o))]) });
for (let i = 0; i < 16; i++) add({ name: `vsamp_rand${i}`, bpMode: 'none', frames: 72000, sig: ['loop', 'steady', 'loud', 'quiet'][i % 4], chain: chainOf([randSet('vsamp', i + 1)]) });
// a setting that changes while it runs (the converter's key changes: the filter states are kept), FXO, FXV
add({ name: 'vsamp_set', bpMode: 'none', chain: chainOf([vsamp({ model: 0 })]), msgs: [{ b: 100, msg: chainOf([vsamp({ model: 2, filter: 30 })]) }, { b: 200, msg: chainOf([vsamp({ model: 7, rate: 90 })]) }, { b: 300, msg: chainOf([vsamp({ model: 7, rate: 90, drive: 250, noise: 255 })]) }, { b: 500, msg: chainOf([vsamp({ model: 1, filter: 0 })]) }] });
add({ name: 'vsamp_pset', bpMode: 'none', chain: chainOf([vsamp()]), q: [[1000 / SR + 1e-7, { pset: { slot: 0, p: chainOf([vsamp({ model: 3, filter: 200 })])[0].p } }], [30000.3 / SR, { pset: { slot: 0, p: chainOf([vsamp({ model: 5, rate: 10 })])[0].p } }]] });
add({ name: 'vsamp_fxo', bpMode: 'none', chain: chainOf([vsamp({ on: false, model: 7, filter: 80 })]), ops: [['sw0', t(10000), 1], ['sw0', t(30000), 0], ['sw0', t(50000), -1], ['sw0', t(70000), 1]] });


// ---- host: sizes, series, chain sets, FXV, rest ----------------------------------------------------------------------------------------------
for (const n of [1, 37, 128, 480, 1024]) add({ name: `blk${n}`, frames: n === 1 ? 30000 : 96000, sizes: [n], clock: [{ at: 0, bpm: 480, beat: 0 }, { at: 25000, bpm: 600 }],
  chain: chainOf([grains({ div: 2, density: 230, rpitch: 2 }), scramble({ chance: 200, keep: 0, div: 1 }), glitch({ chance: 255, div: 1 }), vsamp({ model: 0 })]),
  ops: [['sw1', t(22000), 0], ['sw1', t(26000), -1], ['sw3', t(40000), 0]] });
add({ name: 'blk_mixed', sizes: [128, 7, 130, 1, 99, 128, 4097, 300, 2, 64], clock: run(480), chain: chainOf([grains({ div: 2, density: 230 }), scramble({ chance: 200, keep: 0 }), glitch({ chance: 255 }), vsamp({ model: 2, on: false })]), ops: [['sw3', t(30000), 1], ['sw3', t(50000), -1]] });
add({ name: 'host_series', frames: 192000, clock: [{ at: 0, bpm: 360 }, { at: 100000, bpm: 420 }], chain: chainOf([scramble({ chance: 150, keep: 0, div: 1 }), glitch({ chance: 200, div: 1 }), grains({ div: 3, density: 200, mix: 100 }), vsamp({ model: 7, filter: 100 })]),
  ops: [['sw1', t(70000), 0], ['sw1', t(90000), -1], ['sw0', t(130000), 0], ['sw0', t(150000), 1], ['sw0', t(170000), -1]] });
{ const mk = (a, on = [true, true, true, true]) => chainOf([grains({ div: a, density: 200, on: on[0] }), scramble({ chance: 150 + a * 10, keep: 0, on: on[1] }), glitch({ chance: 200, on: on[2] }), vsamp({ model: a, on: on[3] })]);
  add({ name: 'host_set', clock: run(480), chain: mk(1, [true, false, false, false]), msgs: [{ b: 100, msg: mk(2, [true, true, false, false]) }, { b: 200, msg: mk(3, [true, true, true, true]) }, { b: 300, msg: mk(4, [false, true, true, true]) }, { b: 301, msg: mk(4, [true, true, true, true]) }, { b: 500, msg: chainOf([glitch({ chance: 255 })]) }, { b: 600, msg: mk(5) }] });
  const pv = (slot, key, over) => { const c = chainOf([fresh(key, over)])[0]; return { pset: { slot, p: c.p } }; };
  add({ name: 'host_pset', clock: run(480), chain: mk(1), q: [[1000 / SR + 1e-7, pv(0, 'grains', { div: 4, density: 255 })], [20000.3 / SR, pv(1, 'scramble', { chance: 255, keep: 0 })], [40000 / SR, pv(2, 'glitch', { chance: 255, stut: 255 })], [60000 / SR, pv(3, 'vsamp', { model: 5 })], [70000 / SR, pv(9, 'grains', {})]] }); }
add({ name: 'host_rest', frames: 96000, clock: run(480), chain: chainOf([grains({ div: 2 }), glitch({ chance: 255 })]), msgs: [{ b: 100, msg: { rest: true } }, { b: 500, msg: { rest: false } }] });
// THE NODE'S SLEEP (ck): 18 s of a short burst of noise, then silence, then noise again at the end
const SL = 18 * SR;
for (const [nm, ch] of [['grains', () => grains({ div: 2, density: 255 })], ['scramble', () => scramble({ chance: 200 })], ['glitch', () => glitch({ chance: 255 })], ['vsamp', () => vsamp({ model: 7, noise: 255 })]]) {
  add({ name: `sleep_${nm}_on_running`, sig: 'tail', frames: SL, chain: chainOf([ch()]) });                                                                          // ck 3 (never; the Vintage sampler: ck 0, sleeps after 1 s)
  add({ name: `sleep_${nm}_on_stopped`, sig: 'tail', frames: SL, clock: [{ at: 0, stop: true }], chain: chainOf([ch()]) });
  add({ name: `sleep_${nm}_off`, sig: 'tail', frames: SL, chain: chainOf([Object.assign(ch(), { on: false })]), ops: [['sw0', t(SL - 8000), 1]] });                       // switched off: ck 0 (a Grains / Scramble / Glitch has no feed)
}
add({ name: 'sleep_mixed_vsamp_flexless', sig: 'tail', frames: SL, chain: chainOf([vsamp({ model: 0, noise: 255 }), grains({ on: false })]) });

// ---- the run ----------------------------------------------------------------------------------------------------------------------------
// ---- map() value sets (the four effects: normTfx + tfxChain) -----------------------------------------------------------------------------------
function mapSets() {
  const out = [], r = lcg(2026);
  for (const e of E) {
    const sets = [];
    for (let i = 0; i < 200; i++) {
      const o = { type: e.key, on: i % 13 !== 12 }; for (const p of e.ps) o[p.k] = i === 0 ? p.d : i === 1 ? p.min : i === 2 ? p.max : p.min + Math.floor(r() * (p.max - p.min + 1));
      if (i % 11 === 10) e.ps.forEach((p, j) => { const m = (i + j) % 6; if (m === 0) o[p.k] = p.max + 40; else if (m === 1) o[p.k] = String(o[p.k]); else if (m === 2) delete o[p.k]; else if (m === 3) o[p.k] = -5.7; else if (m === 4) o[p.k] = null; else o[p.k] = p.min + 0.4; });
      const sr = i % 5 === 4 ? 44100 : SR, bpm = [120, 87.5, 140, 33, 999][i % 5];
      const norm = F.normTfx(o), ch = F.tfxChain([norm], sr, bpm)[0];
      sets.push({ obj: o, on: ch.on, key: ch.key, v: e.ps.map(p => norm[p.k]), sr, bpm, restartN: 0, p: ch.p });
    }
    out.push({ key: e.key, id: e.id, sets });
  }
  return out;
}

export function main(argv) {
  const golden = argv[0] === '--golden' || argv[0] === '--check', outDir = golden ? null : (argv[0] || '/tmp/u16/fxidm-ref');
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = [];
  for (const c of CASES) {
    const r = runCase(c), all = new Float32Array(r.n * 2); all.set(r.oL, 0); all.set(r.oR, r.n);
    rows.push(`  {"${c.name}", ${hex(fnv(new Uint8Array(f32b(all))))}, ${r.n * 2}},`);
    if (outDir) {
      const inn = new Float32Array(r.n * 2); inn.set(r.iL, 0); inn.set(r.iR, r.n);
      fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify({ name: c.name, n: r.n, sizes: c.sizes || [BLK], bpMode: c.bpMode, chain: c.chain, ops: c.ops || [], msgs: c.msgs || [], q: c.q || [] }));
      fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), f32b(inn)); fs.writeFileSync(path.join(outDir, c.name + '.bp.f32'), f32b(r.bpAll)); fs.writeFileSync(path.join(outDir, c.name + '.f32'), f32b(all));
    }
  }
  if (outDir) { fs.writeFileSync(path.join(outDir, 'maps.json'), JSON.stringify(mapSets())); console.log(`wrote ${CASES.length} cases to ${outDir}`); }
  if (outDir) {   // fail loudly: every file the C++ test reads must be there and not empty (exit 1, never a quiet skip)
    const miss = ['maps.json', ...CASES.flatMap(c => [c.name + '.json', c.name + '.in.f32', c.name + '.bp.f32', c.name + '.f32'])].filter(n => { try { return fs.statSync(path.join(outDir, n)).size === 0; } catch (e) { return true; } });
    if (miss.length) { console.error(`fx-idm-dump: ${miss.length} reference files missing or empty in ${outDir}: ${miss.slice(0, 5).join(', ')}`); process.exit(1); }
  }
  const inc = ['// GENERATED by native/tools/fx-idm-dump.mjs (--golden) from the ThunderFx worklet (Beat grains, Beat scramble, Glitch sequencer, Vintage sampler cut out of index.html). Do not edit: a mismatch means the port is wrong.',
    '// FNV-1a 64 over each case\'s float32 output (L then R), the real worklet run in Node 22 on x86-64 Linux (the cores call Math.exp / pow / sin / cos / log / tanh: another platform\'s Node may round differently).',
    'struct FxIdmGolden { const char* name; unsigned long long hash; int floats; };', 'static const FxIdmGolden kFxIdmGolden[] = {', ...rows, '};', 'static const int kFxIdmGoldenN = ' + rows.length + ';', ''].join('\n');
  const incPath = path.join(ROOT, 'native/tests/golden/fxidm_golden.inc');
  if (argv[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
  else if (argv[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('fxidm_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('fxidm_golden.inc is current (' + rows.length + ' cases)'); }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
