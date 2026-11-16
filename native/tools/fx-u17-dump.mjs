// U17: THE REFERENCE OF THE FOUR JS-ONLY EFFECTS DEGRADE, RESONATOR, PITCH DELAY AND PITCH REVERB (exact, Node only): the real ThunderFx worklet (the TFX_SRC template of
// index.html with DegradeCore, ResonCore, PitchFxCore and rvMix cut out of index.html as the page has them) run in Node on deterministic stereo signals, for Pitch reverb (96),
// Pitch delay (97), Resonator (98) and Degrade (99): default and other parameter sets, every control at its extremes, random parameter sets, FXO, chain sets, FXV, odd block
// sizes, the node's sleep and the slot nap, the four in series and beside the U16 / U15 cores. And the PADS' use: the real `applyFx` of index.html (with its FX table, rvMix and
// the cores, cut out as the page has them) run on whole sounds, for the four types at several sample rates.
// It reuses the machinery of fx-clk-dump.mjs (the signals, runCase) and writes the same file kinds (no .bp.f32 for a case that does not read the clock):
//   node native/tools/fx-u17-dump.mjs [outDir]   writes <outDir>/<case>.json (the case), <case>.in.f32 (L then R; a pad case: the mono sound), <case>.f32 (the output),
//                                                maps.json (TFX map() on 200 value sets per effect)
//   node native/tools/fx-u17-dump.mjs --golden   writes native/tests/golden/fxu17_golden.inc (FNV-1a 64 of every case's output)
//   node native/tools/fx-u17-dump.mjs --check    exits 1 if the committed fxu17_golden.inc differs
// WHERE THE REFERENCE COMES FROM: the cores call Math.exp / pow / sin / cos / tanh. Node's Math.* is not the same on every platform (macOS arm64 rounds some results
// differently), so the committed golden is made on x86-64 Linux, Node 22, and the C++ is compared with it on every platform (the same rule as U16: see U17-NOTES.md).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { ROOT, SR, load, u17Effects } from './fx-src.mjs';
import { lcg, f32b, fnv } from './fx-dump.mjs';
import { SIG, runCase, hex, BLK } from './fx-clk-dump.mjs';

const F = load(), E = u17Effects(), byKey = Object.fromEntries(E.map(e => [e.key, e]));
const run120 = [{ at: 0, bpm: 120, beat: 0 }];
const t = f => f / SR;

// signals: loud (clipping, the resonator's limiter), quiet (the hiss, Follow), an impulse and a short noise burst (the rings and echoes are easy to read)
SIG.loud = n => { const [L, R] = SIG.loop(n); for (let i = 0; i < n; i++) { L[i] = Math.fround(L[i] * 3.2); R[i] = Math.fround(R[i] * 2.6); } return [L, R]; };
SIG.quiet = n => { const [L, R] = SIG.steady(n); for (let i = 0; i < n; i++) { L[i] = Math.fround(L[i] * 0.01); R[i] = Math.fround(R[i] * 0.004); } return [L, R]; };
SIG.imp = n => { const r = lcg(31), L = new Float32Array(n), R = new Float32Array(n); L[50] = 0.9; R[70] = -0.7; for (let i = 3000; i < 3600; i++) { L[i] = Math.fround((r() * 2 - 1) * 0.4); R[i] = Math.fround((r() * 2 - 1) * 0.3); } return [L, R]; };

const fresh = (type, over = {}) => { const o = { type, on: true }; for (const p of byKey[type].ps) o[p.k] = p.d; return Object.assign(o, over); };
const degrade = (o = {}) => fresh('degrade', o), reson = (o = {}) => fresh('reson', o), pdelay = (o = {}) => fresh('pdelay', o), preverb = (o = {}) => fresh('preverb', o);
// a random set of values over each control's whole range (a range given in `rng` narrows one control)
const randSet = (type, seed, rng = {}) => { const r = lcg(seed * 7919 + byKey[type].id), o = { type, on: true }; for (const p of byKey[type].ps) { const [lo, hi] = rng[p.k] || [p.min, p.max]; o[p.k] = lo + Math.floor(r() * (hi - lo + 1)); } return o; };
function chainOf(objs, bpm = 120) { const A = new Array(8).fill(null); objs.forEach((o, i) => { A[i] = o && F.normTfx(o); }); return F.tfxChain(A, SR, bpm); }

const CASES = [], PADS = [];
const add = c => CASES.push(Object.assign({ sig: 'loop', frames: 24000, clock: [], bpMode: 'none' }, c));

// ---- Degrade ------------------------------------------------------------------------------------------------------------------------------
add({ name: 'degrade_def', chain: chainOf([degrade()]) });
add({ name: 'degrade_def_steady', sig: 'steady', chain: chainOf([degrade()]) });
for (const b of [1, 2, 4, 8, 12, 16, 23, 24]) add({ name: `degrade_bits${b}`, sig: b % 2 ? 'steady' : 'loop', chain: chainOf([degrade({ bits: b, noise: 0 })]) });
for (const r of [0, 1, 64, 128, 200, 254, 255]) add({ name: `degrade_rate${r}`, chain: chainOf([degrade({ rate: r, bits: 24, noise: 0 })]) });
for (const nz of [1, 128, 255]) for (const fo of [0, 255]) add({ name: `degrade_noise${nz}_follow${fo}`, sig: 'quiet', chain: chainOf([degrade({ noise: nz, follow: fo, bits: 24 })]) });
add({ name: 'degrade_noise0_quiet', sig: 'quiet', chain: chainOf([degrade({ noise: 0, bits: 6 })]) });
for (const c of [0, 128, 255]) add({ name: `degrade_color${c}`, sig: 'quiet', chain: chainOf([degrade({ color: c, noise: 200, follow: 0, bits: 24 })]) });
for (const [nm, o] of [['dry255_wet0', { dry: 255, wet: 0 }], ['dry128_wet128', { dry: 128, wet: 128 }], ['dry0_wet0', { dry: 0, wet: 0 }], ['dry255_wet255', { dry: 255, wet: 255 }]]) add({ name: `degrade_${nm}`, chain: chainOf([degrade(Object.assign({ bits: 6, rate: 100 }, o))]) });
add({ name: 'degrade_loud', sig: 'loud', chain: chainOf([degrade({ bits: 5, rate: 120, noise: 150 })]) });
add({ name: 'degrade_tail', sig: 'tail', frames: 72000, chain: chainOf([degrade({ noise: 200, follow: 255 })]) });
for (let i = 0; i < 12; i++) add({ name: `degrade_rand${i}`, frames: 24000, sig: ['loop', 'steady', 'loud', 'quiet'][i % 4], chain: chainOf([randSet('degrade', i + 1)]) });

// ---- Resonator ----------------------------------------------------------------------------------------------------------------------------
add({ name: 'reson_def', sig: 'imp', chain: chainOf([reson()]) });
for (let c = 0; c <= 11; c++) add({ name: `reson_chord${c}`, sig: c % 2 ? 'imp' : 'loop', chain: chainOf([reson({ chord: c, decay: 160 })]) });
for (const n of [24, 36, 48, 72, 96]) add({ name: `reson_note${n}`, sig: 'imp', chain: chainOf([reson({ note: n, chord: 4, decay: 200 })]) });
for (const [c, m] of [[3, 1], [10, 1], [0, 1], [11, 1], [2, 0]]) add({ name: `reson_tube_c${c}_m${m}`, sig: 'imp', chain: chainOf([reson({ chord: c, mode: m, decay: 180 })]) });
for (const d of [0, 1, 128, 255]) add({ name: `reson_decay${d}`, sig: 'imp', frames: d === 255 ? 96000 : 32000, chain: chainOf([reson({ decay: d, chord: 3 })]) });
for (const b of [0, 128, 255]) add({ name: `reson_bright${b}`, sig: 'imp', chain: chainOf([reson({ bright: b, decay: 200, chord: 8 })]) });
for (const s of [0, 255]) add({ name: `reson_spread${s}`, sig: 'imp', chain: chainOf([reson({ spread: s, chord: 6 })]) });
for (const m of [0, 128, 255]) add({ name: `reson_mix${m}`, sig: 'loop', chain: chainOf([reson({ mix: m, decay: 150 })]) });
add({ name: 'reson_loud', sig: 'loud', chain: chainOf([reson({ decay: 255, mix: 255, chord: 1 })]) });
add({ name: 'reson_quiet', sig: 'quiet', chain: chainOf([reson({ decay: 255, mix: 200 })]) });
for (let i = 0; i < 12; i++) add({ name: `reson_rand${i}`, frames: 24000, sig: ['loop', 'steady', 'imp', 'loud'][i % 4], chain: chainOf([randSet('reson', i + 1)]) });

// ---- Pitch delay ------------------------------------------------------------------------------------------------------------------------
add({ name: 'pdelay_def', sig: 'imp', frames: 36000, chain: chainOf([pdelay()]) });
for (let m = 0; m <= 4; m++) {
  add({ name: `pdelay_mode${m}`, sig: 'imp', frames: 36000, chain: chainOf([pdelay({ mode: m, time: 60 })]) });
  add({ name: `pdelay_mode${m}_fb255`, sig: 'loop', frames: 36000, chain: chainOf([pdelay({ mode: m, time: 50, fb: 255 })]) });
}
for (const m of [0, 1, 2, 4]) for (const p of [0, 48]) add({ name: `pdelay_mode${m}_pitch${p}`, sig: 'imp', frames: 36000, chain: chainOf([pdelay({ mode: m, time: 70, pitch: p, fb: 180 })]) });
add({ name: 'pdelay_time0', sig: 'imp', frames: 24000, chain: chainOf([pdelay({ time: 0, fb: 200 })]) });
add({ name: 'pdelay_time1', sig: 'imp', frames: 24000, chain: chainOf([pdelay({ time: 1, fb: 200 })]) });
add({ name: 'pdelay_time255', sig: 'imp', frames: 120000, chain: chainOf([pdelay({ time: 255, fb: 160 })]) });                          // 2 s: the delay line's full length
add({ name: 'pdelay_time255_reverse', sig: 'imp', frames: 120000, chain: chainOf([pdelay({ time: 255, mode: 4, fb: 160 })]) });
add({ name: 'pdelay_time255_arp', sig: 'imp', frames: 120000, chain: chainOf([pdelay({ time: 200, mode: 2, fb: 160 })]) });
for (const tn of [0, 255]) add({ name: `pdelay_tone${tn}`, sig: 'loop', frames: 36000, chain: chainOf([pdelay({ tone: tn, time: 60, fb: 200 })]) });
for (const w of [0, 255]) add({ name: `pdelay_wow${w}`, sig: 'loop', frames: 36000, chain: chainOf([pdelay({ mode: 3, wow: w, time: 70, fb: 150 })]) });
for (const m of [0, 255]) add({ name: `pdelay_mix${m}`, sig: 'imp', frames: 36000, chain: chainOf([pdelay({ mix: m, time: 60 })]) });
add({ name: 'pdelay_loud', sig: 'loud', frames: 36000, chain: chainOf([pdelay({ time: 60, fb: 255 })]) });
for (let i = 0; i < 12; i++) add({ name: `pdelay_rand${i}`, frames: 36000, sig: ['loop', 'steady', 'imp', 'loud'][i % 4], chain: chainOf([randSet('pdelay', i + 1, { time: [0, 150] })]) });

// ---- Pitch reverb -------------------------------------------------------------------------------------------------------------------------
add({ name: 'preverb_def', sig: 'imp', frames: 36000, chain: chainOf([preverb()]) });
for (let m = 0; m <= 2; m++) {
  for (const sz of [0, 128, 255]) add({ name: `preverb_mode${m}_size${sz}`, sig: 'imp', frames: 36000, chain: chainOf([preverb({ mode: m, size: sz })]) });
  for (const fb of [0, 255]) add({ name: `preverb_mode${m}_fb${fb}`, sig: 'loop', frames: 36000, chain: chainOf([preverb({ mode: m, fb })]) });
}
for (const p of [0, 12, 48]) add({ name: `preverb_pitch${p}`, sig: 'imp', frames: 36000, chain: chainOf([preverb({ pitch: p, fb: 200 })]) });
for (const p of [0, 48]) add({ name: `preverb_dual_pitch2_${p}`, sig: 'imp', frames: 36000, chain: chainOf([preverb({ mode: 2, pitch2: p, fb: 200 })]) });
for (const tn of [0, 255]) add({ name: `preverb_tone${tn}`, sig: 'loop', frames: 36000, chain: chainOf([preverb({ tone: tn, fb: 220 })]) });
for (const m of [0, 255]) add({ name: `preverb_mix${m}`, sig: 'loop', frames: 24000, chain: chainOf([preverb({ mix: m })]) });
add({ name: 'preverb_shimmer_long', sig: 'loud', frames: 120000, chain: chainOf([preverb({ size: 255, fb: 255, mix: 255 })]) });             // the slow limiter on the shimmer loop
add({ name: 'preverb_dual_long', sig: 'loop', frames: 120000, chain: chainOf([preverb({ mode: 2, size: 255, fb: 255, pitch: 48, pitch2: 0 })]) });
for (let i = 0; i < 12; i++) add({ name: `preverb_rand${i}`, frames: 36000, sig: ['loop', 'steady', 'imp', 'loud'][i % 4], chain: chainOf([randSet('preverb', i + 1)]) });

// ---- host: sizes, series, chain sets, FXV, rest, FXO ----------------------------------------------------------------------------------------
const four = () => [preverb({ fb: 180 }), pdelay({ time: 60, fb: 150 }), reson({ decay: 150 }), degrade({ bits: 10, rate: 150 })];
for (const n of [1, 37, 128, 480, 1024]) add({ name: `blk${n}`, frames: n === 1 ? 12000 : 24000, sizes: [n], chain: chainOf(four()), ops: [['sw1', t(9000), 0], ['sw1', t(15000), -1], ['sw3', t(10500), 0]] });
add({ name: 'blk_mixed', sizes: [128, 7, 130, 1, 99, 128, 4097, 300, 2, 64], chain: chainOf(four()), ops: [['sw2', t(10000), 0], ['sw2', t(20000), -1]] });
add({ name: 'host_series', frames: 64000, chain: chainOf([degrade({ bits: 12 }), reson({ chord: 4, decay: 170 }), pdelay({ time: 80, fb: 170, mode: 1 }), preverb({ mode: 2, fb: 150 })]),
  ops: [['sw1', t(20000), 0], ['sw1', t(30000), -1], ['sw3', t(40000), 0], ['sw3', t(50000), 1], ['sw3', t(60000), -1]] });
add({ name: 'host_off_on', sig: 'imp', frames: 36000, chain: chainOf([pdelay({ on: false, time: 60, fb: 200 }), reson({ on: false, decay: 200 })]), ops: [['sw0', t(500), 1], ['sw0', t(20000), 0], ['sw1', t(5000), 1], ['sw1', t(30000), 0]] });
{ const mk = (a, on = [true, true, true, true]) => chainOf([preverb({ mode: a % 3, on: on[0] }), pdelay({ mode: a % 5, time: 60, on: on[1] }), reson({ chord: a, on: on[2] }), degrade({ bits: 6 + a, on: on[3] })]);
  add({ name: 'host_set', frames: 36000, chain: mk(1, [true, false, false, false]), msgs: [{ b: 40, msg: mk(2, [true, true, false, false]) }, { b: 80, msg: mk(3, [true, true, true, true]) }, { b: 120, msg: mk(4, [false, true, true, true]) }, { b: 121, msg: mk(4, [true, true, true, true]) }, { b: 180, msg: chainOf([degrade({ bits: 4 })]) }, { b: 200, msg: mk(5) }] });
  const pv = (slot, key, over) => { const c = chainOf([fresh(key, over)])[0]; return { pset: { slot, p: c.p } }; };
  add({ name: 'host_pset', chain: mk(1), q: [[1000 / SR + 1e-7, pv(0, 'preverb', { mode: 2, fb: 255 })], [8000.3 / SR, pv(1, 'pdelay', { mode: 3, wow: 255 })], [12000 / SR, pv(2, 'reson', { chord: 10, mode: 1 })], [16000 / SR, pv(3, 'degrade', { bits: 3 })], [20000 / SR, pv(9, 'reson', {})]] }); }
add({ name: 'host_rest', frames: 36000, chain: chainOf([pdelay({ time: 60 }), reson(), degrade({ noise: 200 })]), msgs: [{ b: 40, msg: { rest: true } }, { b: 200, msg: { rest: false } }] });
add({ name: 'host_with_u16', frames: 96000, clock: run120, bpMode: 'full', chain: chainOf([{ type: 'grains', on: true, density: 200 }, reson({ decay: 160 }), pdelay({ time: 60, fb: 150 }), { type: 'vsamp', on: true }, degrade({ bits: 9 }), preverb()]) });
add({ name: 'host_with_retrig', frames: 96000, clock: run120, bpMode: 'full', chain: chainOf([{ type: 'retrig', on: true, mode: 1 }, pdelay({ time: 60, fb: 150 }), preverb({ mode: 1 })]) });
add({ name: 'host_chord_change', sig: 'imp', frames: 36000, chain: chainOf([reson({ chord: 2, decay: 200 })]), msgs: [{ b: 100, msg: chainOf([reson({ chord: 11, note: 60, mode: 1, decay: 200 })]) }, { b: 200, msg: chainOf([reson({ chord: 0, note: 36 })]) }] });
// THE NODE'S SLEEP and the slot's NAP: 3 s of a short burst of noise, then silence, then noise again at the end
const SL = 3 * SR;
for (const [nm, ch] of [['preverb', () => preverb({ fb: 220, size: 220 })], ['pdelay', () => pdelay({ time: 100, fb: 230 })], ['reson', () => reson({ decay: 255 })], ['degrade', () => degrade({ noise: 255, follow: 0 })]]) {
  add({ name: `sleep_${nm}_on`, sig: 'tail', frames: SL, chain: chainOf([ch()]) });
  add({ name: `sleep_${nm}_off`, sig: 'tail', frames: SL, chain: chainOf([Object.assign(ch(), { on: false })]), ops: [['sw0', t(SL - 8000), 1]] });
}
// the slot's own nap (30 s of silence in and out) is only reached when the node keeps running: a Retrigger that is on holds it awake (ck 3)
add({ name: 'nap_30s', sig: 'tail', frames: 31.5 * SR, clock: run120, bpMode: 'full', chain: chainOf([{ type: 'retrig', on: true }, reson({ decay: 255 }), pdelay({ time: 200, fb: 255 }), preverb({ size: 255, fb: 255 }), degrade({ noise: 255, follow: 0 })]) });

// ---- the pads' use: the real applyFx on whole sounds ---------------------------------------------------------------------------------------
const padAdd = (name, fx, sig = 'loop', n = 14000, sr = SR) => PADS.push({ name: 'pad_' + name, fx, sig, n, sr });
const PADSIG = { loop: n => SIG.loop(n)[0], steady: n => SIG.steady(n)[0], loud: n => SIG.loud(n)[0], quiet: n => SIG.quiet(n)[0], imp: n => SIG.imp(n)[0] };
padAdd('degrade_def', { type: 'degrade', on: true, bits: 24, rate: 48000, noise: 0.15, color: 0.3, follow: 0.6, dry: 0, wet: 1 });
padAdd('degrade_8bit', { type: 'degrade', on: true, bits: 8, rate: 8000, noise: 0.1, color: 0.5, follow: 0.2, dry: 0.1, wet: 0.9 });
padAdd('degrade_1bit', { type: 'degrade', on: true, bits: 1, rate: 300, noise: 1, color: 1, follow: 0, dry: 0, wet: 1 }, 'quiet');
padAdd('degrade_clean', { type: 'degrade', on: true, bits: 24, rate: 48000, noise: 0, color: 0, follow: 0, dry: 0, wet: 1 });
padAdd('degrade_44k', { type: 'degrade', on: true, bits: 12.5, rate: 11025, noise: 0.4, color: 0.2, follow: 0.9, dry: 0.3, wet: 0.7 }, 'steady', 14001, 44100);
padAdd('degrade_32k_loud', { type: 'degrade', on: true, bits: 3.3, rate: 32000, noise: 0.05, color: 0.9, follow: 1, dry: 0, wet: 1 }, 'loud', 12345, 32000);
padAdd('degrade_96k', { type: 'degrade', on: true, bits: 10, rate: 20000, noise: 0.3, color: 0.3, follow: 0.5, dry: 0.5, wet: 0.5 }, 'loop', 13000, 96000);
padAdd('degrade_missing', { type: 'degrade', on: true, bits: 8, rate: 8000 });   // noise, color ... undefined: NaN in the JS
padAdd('reson_def', { type: 'reson', on: true, note: 48, chord: 3, decay: 0.5, bright: 0.6, mode: 0, spread: 0.5, mix: 0.5 }, 'imp');
for (const c of [0, 1, 4, 10, 11]) padAdd(`reson_chord${c}`, { type: 'reson', on: true, note: 40 + c, chord: c, decay: 0.7, bright: 0.4, mode: c % 2, spread: 1, mix: 1 }, 'imp');
padAdd('reson_tube', { type: 'reson', on: true, note: 96, chord: 3, decay: 1, bright: 1, mode: 1, spread: 0, mix: 0.8 }, 'loud');
padAdd('reson_44k', { type: 'reson', on: true, note: 24, chord: 5, decay: 0.2, bright: 0, mode: 0, spread: 0.3, mix: 0.4 }, 'steady', 14001, 44100);
padAdd('reson_22k', { type: 'reson', on: true, note: 60, chord: 7, decay: 0.9, bright: 0.9, mode: 0, spread: 0.6, mix: 0.6 }, 'loop', 9999, 22050);
padAdd('reson_odd_chord', { type: 'reson', on: true, note: 48, chord: 12.7, decay: 0.5, bright: 0.6, mode: 0.5, spread: 0.5, mix: 0.5 }, 'imp');   // chord past the table: clamped; mode 0.5 is a tube
for (let m = 0; m <= 4; m++) padAdd(`pdelay_mode${m}`, { type: 'pdelay', on: true, mode: m, time: 120, fb: 0.5, pitch: 12, tone: 0.6, wow: 0.3, mix: 0.5 }, 'imp');
padAdd('pdelay_nodefaults', { type: 'pdelay', on: true, mode: 0, time: 90, fb: 0.4, pitch: -5, tone: 0.5, mix: 0.4 }, 'imp');                    // pitch2, size, wow absent: the JS defaults
padAdd('pdelay_long', { type: 'pdelay', on: true, mode: 3, time: 2000, fb: 0.95, pitch: 7, tone: 0.3, wow: 1, mix: 0.6 }, 'imp', 3600);           // tail clamped to 8 s
padAdd('pdelay_44k', { type: 'pdelay', on: true, mode: 2, time: 250, fb: 0.6, pitch: 3, tone: 1, wow: 0, mix: 1 }, 'loop', 9000, 44100);
padAdd('pdelay_short', { type: 'pdelay', on: true, mode: 1, time: 20, fb: 0, pitch: 0, tone: 0, wow: 0, mix: 0 }, 'loop', 5000);
for (let m = 0; m <= 2; m++) padAdd(`preverb_mode${m}`, { type: 'preverb', on: true, mode: m, size: 0.6, fb: 0.45, pitch: 12, pitch2: -12, tone: 0.6, mix: 0.35 }, 'imp', 6000);
padAdd('preverb_nodefaults', { type: 'preverb', on: true, mode: 2, size: 0.2, fb: 0.8, pitch: 7, tone: 0.2, mix: 0.9 }, 'imp', 4000);              // pitch2 absent: -12
padAdd('preverb_big', { type: 'preverb', on: true, mode: 0, size: 1, fb: 0.9, pitch: 24, pitch2: 12, tone: 1, mix: 1 }, 'loud', 3000);          // 7 s tail
padAdd('preverb_44k', { type: 'preverb', on: true, mode: 1, size: 0.3, fb: 0.2, pitch: -12, pitch2: 5, tone: 0, mix: 0.5 }, 'steady', 4000, 44100);
padAdd('preverb_nosize', { type: 'preverb', on: true, mode: 0, fb: 0.2, pitch: 12, tone: 0.5, mix: 0.5 }, 'steady', 3000);                         // a JS exception: nothing out

function runPad(c) {
  const ctx = padCtx(), a = Float32Array.from(PADSIG[c.sig](c.n)); let o; try { o = ctx.applyFx(Object.assign({}, c.fx), a, c.sr); } catch (e) { o = new Float32Array(0); }
  return { a, o: Float32Array.from(o) };
}
let PADCTX = null;
function padCtx() {
  if (PADCTX) return PADCTX;
  const by = F.by, H = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8').replace(/\r\n/g, '\n'), need = ['applyFx', 'fxReady', 'FX', 'rvMix', 'DegradeCore', 'ResonCore', 'PitchFxCore'];
  for (let tries = 0; tries < 200; tries++) {
    const ctx = vm.createContext({ Math, Number, Array, Object, String, JSON, Map, Set, Float32Array, Float64Array, Int16Array, Uint8Array });
    const src = [...new Set(need.map(n => by.get(n)))].sort((x, y) => H.indexOf(x) - H.indexOf(y)).join('\n');
    try { vm.runInContext(src + '\nglobalThis.__A = { applyFx };', ctx); PADCTX = ctx.__A; return PADCTX; }
    catch (e) { const m = /^(\w+) is not defined/.exec(e.message) || /^Cannot access '(\w+)' before/.exec(e.message); if (!m || !by.has(m[1]) || need.includes(m[1])) throw e; need.push(m[1]); }
  }
  throw new Error('fx-u17-dump: applyFx could not be cut out of index.html');
}

// ---- map() value sets (the four effects: normTfx + tfxChain) -----------------------------------------------------------------------------------
function mapSets() {
  const out = [], r = lcg(2027);
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
  const golden = argv[0] === '--golden' || argv[0] === '--check', outDir = golden ? null : (argv[0] || '/tmp/u17/fxu17-ref');
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = [], files = [];
  for (const c of CASES) {
    const r = runCase(c), all = new Float32Array(r.n * 2); all.set(r.oL, 0); all.set(r.oR, r.n);
    rows.push(`  {"${c.name}", ${hex(fnv(new Uint8Array(f32b(all))))}, ${r.n * 2}},`);
    if (outDir) {
      const inn = new Float32Array(r.n * 2); inn.set(r.iL, 0); inn.set(r.iR, r.n);
      fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify({ name: c.name, n: r.n, sizes: c.sizes || [BLK], bpMode: c.bpMode, chain: c.chain, ops: c.ops || [], msgs: c.msgs || [], q: c.q || [] }));
      fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), f32b(inn)); fs.writeFileSync(path.join(outDir, c.name + '.f32'), f32b(all)); files.push(c.name + '.json', c.name + '.in.f32', c.name + '.f32');
      if (c.bpMode !== 'none') { fs.writeFileSync(path.join(outDir, c.name + '.bp.f32'), f32b(r.bpAll)); files.push(c.name + '.bp.f32'); }
    }
  }
  for (const c of PADS) {
    const { a, o } = runPad(c);
    rows.push(`  {"${c.name}", ${hex(fnv(new Uint8Array(f32b(o))))}, ${o.length}},`);
    if (outDir) {
      fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify({ name: c.name, pad: true, n: a.length, nout: o.length, sr: c.sr, fx: c.fx }));
      fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), f32b(a)); fs.writeFileSync(path.join(outDir, c.name + '.f32'), f32b(o)); files.push(c.name + '.json', c.name + '.in.f32');   // (an empty output is a result here: .f32 is not checked for size)
    }
  }
  if (outDir) { fs.writeFileSync(path.join(outDir, 'maps.json'), JSON.stringify(mapSets())); console.log(`wrote ${CASES.length} track cases and ${PADS.length} pad cases to ${outDir}`); }
  if (outDir) {   // fail loudly: every file the C++ test reads must be there and not empty (exit 1, never a quiet skip)
    const miss = ['maps.json', ...files].filter(n => { try { return fs.statSync(path.join(outDir, n)).size === 0; } catch (e) { return true; } });
    if (miss.length) { console.error(`fx-u17-dump: ${miss.length} reference files missing or empty in ${outDir}: ${miss.slice(0, 5).join(', ')}`); process.exit(1); }
  }
  const inc = ['// GENERATED by native/tools/fx-u17-dump.mjs (--golden) from the ThunderFx worklet and applyFx (Degrade, Resonator, Pitch delay, Pitch reverb cut out of index.html). Do not edit: a mismatch means the port is wrong.',
    '// FNV-1a 64 over each case\'s float32 output (L then R; a pad case: the sound), the real JS run in Node 22 on x86-64 Linux (the cores call Math.exp / pow / sin / cos / tanh: another platform\'s Node may round differently).',
    'struct FxU17Golden { const char* name; unsigned long long hash; int floats; };', 'static const FxU17Golden kFxU17Golden[] = {', ...rows, '};', 'static const int kFxU17GoldenN = ' + rows.length + ';', ''].join('\n');
  const incPath = path.join(ROOT, 'native/tests/golden/fxu17_golden.inc');
  if (argv[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
  else if (argv[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('fxu17_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('fxu17_golden.inc is current (' + rows.length + ' cases)'); }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
