// U15: THE TEMPO-LOCKED EFFECTS' REFERENCE (exact, Node only): the real ThunderFx worklet (the TFX_SRC template of index.html, with FlexCore, StepGateCore,
// RetrigCore and clkSpb cut out of index.html as the page has them) run in Node on deterministic stereo signals and a beat clock made here, for Flex (95),
// Step gate (94) and Retrigger (93): default and other parameter sets, every Flex pattern (a sweep: FLX 01..FF, FLY 01..FF and FLZ 01..FF), the tests/flex.test.js
// scenarios (STARTS ON FLX / ON GAT, FLX unclamped, FLX 00, FXO on), tempo changes, the clock stopping and restarting, a clock jump (a song loop), the
// clock param constant or absent, odd block sizes (1, 37, 128, 480, 1024), commands on block edges, a chain set / FXV mid-run, the node's sleep rules
// (`ck`) and the three effects in series.
//   node native/tools/fx-clk-dump.mjs [outDir]   writes <outDir>/<case>.json (the case), <case>.in.f32 (L then R), <case>.bp.f32 (the clock param: one
//                                                float per frame), <case>.f32 (the output, L then R), maps.json (TFX map() on 200 value sets per effect)
//   node native/tools/fx-clk-dump.mjs --golden   writes native/tests/golden/fxclk_golden.inc (FNV-1a 64 of every case's output)
//   node native/tools/fx-clk-dump.mjs --check    exits 1 if the committed fxclk_golden.inc differs
// The clock (`bp`): the song position in beats, wrapping every 16, -1 stopped, as float32, from a program of segments {at (frame), bpm, beat?, stop?}. It
// reaches the worklet as the a-rate `bp` AudioParam does: one value per frame ('full'), or a single value when the param is constant over the block
// ('const'), or the param at its default, 0 ('none'). Commands (FXO / FLX / GAT / RTG) as in fx-dump.mjs: k-rate lanes read at the block start.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { ROOT, SR, load, clockedEffects } from './fx-src.mjs';
import { lcg, Auto, mkNode, f32b, fnv } from './fx-dump.mjs';

const F = load(), E = clockedEffects(), byKey = Object.fromEntries(E.map(e => [e.key, e]));
export const BLK = 128;

// ---- signals (float32) ------------------------------------------------------------------------------------------------------------------
export const SIG = {
  // tests/flex.test.js's steady signal (a sine and a sawtooth), a little different on the right
  steady(n) { const L = new Float32Array(n), R = new Float32Array(n); for (let s = 0; s < n; s++) { L[s] = Math.fround(Math.sin(s * 0.05) * 0.5 + ((s * 7) % 200) / 400); R[s] = Math.fround(Math.sin(s * 0.031) * 0.4 + ((s * 5) % 170) / 400 - 0.2); } return [L, R]; },
  // a drum loop at 120 BPM that goes on for the whole case: a kick and a hat noise burst on every beat, a snare on 2 and 4, a bass tone under it
  loop(n) { const r = lcg(4242), L = new Float32Array(n), R = new Float32Array(n), beat = 24000; let ph = 0;
    for (let i = 0; i < n; i++) { const p = i % beat, b = Math.floor(i / beat) & 3, kick = p < 9000 ? Math.sin(2 * Math.PI * (50 + 110 * Math.exp(-p / 700)) * p / SR) * Math.exp(-p / 2500) * 0.9 : 0,
        hat = p > 12000 && p < 15000 ? (r() * 2 - 1) * 0.35 * Math.exp(-(p - 12000) / 500) : 0, sn = (b & 1) && p < 7000 ? (r() * 2 - 1) * 0.5 * Math.exp(-p / 1500) : 0; ph += 2 * Math.PI * 82 / SR;
      L[i] = Math.fround(kick + hat + sn + 0.1 * Math.sin(ph)); R[i] = Math.fround(kick * 0.9 + hat * 1.1 + sn * 0.8 + 0.1 * Math.sin(ph + 0.5)); }
    return [L, R]; },
  // noise for 0.3 s, silence, noise again at the very end (the node sleeps in between, or does not)
  tail(n) { const r = lcg(99), L = new Float32Array(n), R = new Float32Array(n); for (let i = 0; i < 14400; i++) { L[i] = Math.fround((r() * 2 - 1) * 0.3); R[i] = Math.fround((r() * 2 - 1) * 0.3); }
    for (let i = n - 9000; i < n; i++) { L[i] = Math.fround((r() * 2 - 1) * 0.2); R[i] = Math.fround((r() * 2 - 1) * 0.2); } return [L, R]; },
};

// ---- the clock ----------------------------------------------------------------------------------------------------------------------------
export function clock(n, prog) {
  const out = new Float32Array(n); let on = false, beat = 0, bpm = 120, si = 0; const segs = [...prog].sort((a, b) => a.at - b.at);
  for (let i = 0; i < n; i++) {
    while (si < segs.length && segs[si].at <= i) { const s = segs[si++]; if (s.stop) on = false; else { on = true; if (s.bpm) bpm = s.bpm; if (s.beat !== undefined) beat = s.beat; } }
    out[i] = on ? Math.fround(beat % 16) : -1; if (on) beat += bpm / 60 / SR;
  }
  return out;
}
const run120 = [{ at: 0, bpm: 120, beat: 0 }];
const running = (bpm, beat = 0) => [{ at: 0, bpm, beat }];

// ---- chains (the page's own normTfx + tfxChain) -----------------------------------------------------------------------------------------
// the controls as freshTfx makes a new slot: every default of the table (STARTS ON FLX / ON GAT)
const fresh = (type, over = {}) => { const o = { type, on: true }; for (const p of byKey[type].ps) o[p.k] = p.d; return Object.assign(o, over); };
const flex = (o = {}) => fresh('flex', o), sgate = (o = {}) => fresh('sgate', o), retrig = (o = {}) => fresh('retrig', o);
function chainOf(objs, bpm = 120) { const A = new Array(8).fill(null); objs.forEach((o, i) => { A[i] = o && F.normTfx(o); }); return F.tfxChain(A, SR, bpm); }

// ---- cases ------------------------------------------------------------------------------------------------------------------------------
// {name, sig, frames, sizes, chain, clock: program, bpMode: 'full' | 'const' | 'none', ops: [[param, t, v]], msgs: [{b, msg}], q: [[at, msg]]}
const CASES = [];
const t = f => f / SR;
const add = c => CASES.push(Object.assign({ sig: 'steady', frames: 48000, clock: run120, bpMode: 'full' }, c));

// Flex: default, alternative sets
add({ name: 'flex_def', sig: 'loop', frames: 96000, chain: chainOf([flex()]), ops: [['flx', t(20000), 0x12], ['flx', t(40000), 0], ['flx', t(60000), 5], ['flx', t(80000), -1]] });
add({ name: 'flex_waits', sig: 'loop', frames: 48000, chain: chainOf([flex({ pat: 5 })]) });   // STARTS ON FLX, no command: the sound as it is
add({ name: 'flex_atplay', clock: running(480), chain: chainOf([flex({ pat: 5, wait: 0, mix: 255, xf: 2 })]) });
add({ name: 'flex_old_slot', clock: running(480), chain: chainOf([(() => { const o = flex({ pat: 6 }); delete o.wait; return o; })()]) });   // a song saved before STARTS: normTfx makes it AT PLAY
add({ name: 'flex_alt1', sig: 'loop', frames: 96000, clock: running(130, 3.3), chain: chainOf([flex({ pat: 0x73, wait: 0, mix: 128, xf: 0 })]) });
add({ name: 'flex_alt2', sig: 'loop', frames: 96000, clock: running(97), chain: chainOf([flex({ pat: 0x9a, wait: 0, mix: 200, xf: 255 })]) });
add({ name: 'flex_alt3', sig: 'loop', frames: 96000, clock: running(143, 14.5), chain: chainOf([flex({ pat: 0xa4, wait: 0, mix: 90, xf: 120 })]) });
// the scenarios of tests/flex.test.js: FLX / FXO / FLX 00 / FLX beyond 255 (FLY) reach the effect unclamped
add({ name: 'flex_flx', clock: running(480), frames: 60000, chain: chainOf([flex({ pat: 5, mix: 255, xf: 2 })]), ops: [['flx', t(10000), 5], ['flx', t(20000), 0], ['flx', t(30000), 300], ['flx', t(40000), 511], ['flx', t(50000), 16]] });
add({ name: 'flex_fxo', clock: running(480), frames: 70000, chain: chainOf([flex({ pat: 6, on: false, mix: 255, xf: 2 })]), ops: [['sw0', t(10000), 1], ['sw0', t(25000), 0], ['sw0', t(35000), -1], ['sw0', t(45000), 1], ['flx', t(55000), 0x22]] });   // fed while off: switching on mid-bar already has the bar
add({ name: 'flex_fxo_wait', clock: running(480), frames: 40000, chain: chainOf([flex({ pat: 5, wait: 1 })]), ops: [['sw0', t(8000), 1], ['sw0', t(20000), -1]] });   // FXO on: its own PATTERN even when it waits
// every pattern: eight sweeps, one bar of 600 BPM (19200 frames) each, a new FLX at every bar line
{ const groups = [[1, 64], [65, 128], [129, 192], [193, 255], [257, 320], [321, 384], [385, 448], [449, 511], [513, 576], [577, 640], [641, 704], [705, 767]];   // 513-767: FLZ (the third bank, chains of FLX / FLY stages)
  groups.forEach(([a, b], gi) => { const ops = []; for (let k = a; k <= b; k++) ops.push(['flx', t((k - a) * 19200), k]);
    add({ name: `flex_sweep${gi}`, sig: gi % 2 ? 'loop' : 'steady', frames: (b - a + 1) * 19200, clock: running(600, gi), chain: chainOf([flex({ pat: 1, wait: 1, mix: 255, xf: 40 })]), ops }); }); }
// patterns that change every bar, held for eight bars (240 BPM)
for (const [nm, k] of [['chaos', 0xf1], ['mut', 256 + 0x81], ['prob', 256 + 0x33], ['chaos2', 256 + 0xf1], ['grain', 256 + 0xd2], ['tex', 256 + 0xe1], ['flz_stutter', 512 + 0x21], ['flz_juggle', 512 + 0x2d], ['flz_echo3', 512 + 0xb3], ['flz_chaos', 512 + 0xff]])
  add({ name: `flex_bars_${nm}`, sig: 'loop', frames: 8 * 48000 * 4 / 4, clock: running(240), chain: chainOf([flex({ pat: 1, wait: 1 })]), ops: [['flx', 0, k]] });
// tempo changes mid-run (a real change snaps and crossfades; a small one glides), the clock stopping and restarting, a jump (a song loop), a wrap past 16
add({ name: 'flex_tempo', sig: 'loop', frames: 240000, clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 48000, bpm: 150 }, { at: 96000, bpm: 90 }, { at: 144000, bpm: 91.5 }, { at: 192000, bpm: 180 }], chain: chainOf([flex({ pat: 0x2a, wait: 0, mix: 255, xf: 60 })]) });
add({ name: 'flex_tempo_scr', sig: 'loop', frames: 192000, clock: [{ at: 0, bpm: 100, beat: 0 }, { at: 40000, bpm: 133 }, { at: 90001, bpm: 70 }, { at: 150000, bpm: 100 }], chain: chainOf([flex({ pat: 0x86, wait: 0 })]) });
add({ name: 'flex_stop', sig: 'loop', frames: 150000, clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 50001, stop: true }, { at: 80000, bpm: 120, beat: 6.5 }, { at: 110000, stop: true }, { at: 110500, bpm: 140, beat: 1 }], chain: chainOf([flex({ pat: 0x10, wait: 0 })]) });
add({ name: 'flex_jump', sig: 'loop', frames: 180000, clock: [{ at: 0, bpm: 120, beat: 0.25 }, { at: 70000, bpm: 120, beat: 1.75 }, { at: 120000, bpm: 120, beat: 15.9 }], chain: chainOf([flex({ pat: 0x33, wait: 0 })]) });
add({ name: 'flex_wrap', sig: 'loop', frames: 144000, clock: running(120, 13), chain: chainOf([flex({ pat: 0x45, wait: 0 })]) });
// the clock param constant over the block, or not there at all
add({ name: 'flex_bp_const', clock: running(480), bpMode: 'const', chain: chainOf([flex({ pat: 5, wait: 0 })]) });
add({ name: 'flex_bp_none', clock: running(480), bpMode: 'none', chain: chainOf([flex({ pat: 5, wait: 0 })]) });
add({ name: 'flex_bp_stopped', clock: [{ at: 0, stop: true }], chain: chainOf([flex({ pat: 5, wait: 0 })]) });

// Step gate
add({ name: 'sgate_def', sig: 'loop', frames: 96000, chain: chainOf([sgate()]), ops: [['gat', t(20000), 3], ['gat', t(40000), 0], ['gat', t(50000), 16], ['gat', t(70000), -1]] });
add({ name: 'sgate_atplay', clock: running(480), chain: chainOf([sgate({ pat: 4, wait: 0, div: 2 })]) });
add({ name: 'sgate_old_slot', clock: running(480), chain: chainOf([(() => { const o = sgate({ pat: 6 }); delete o.wait; return o; })()]) });
add({ name: 'sgate_custom', sig: 'loop', frames: 96000, chain: chainOf([sgate({ pat: 0, m1: 0xb6, m2: 0x5a, div: 1, len: 120, depth: 200, smooth: 10, wait: 0 })]) });
add({ name: 'sgate_alt', sig: 'loop', frames: 96000, clock: running(133, 2.2), chain: chainOf([sgate({ pat: 11, div: 4, len: 255, depth: 128, smooth: 255, wait: 0 })]) });
add({ name: 'sgate_alt2', sig: 'loop', frames: 96000, clock: running(75), chain: chainOf([sgate({ pat: 0, m1: 255, m2: 1, div: 3, len: 0, depth: 255, smooth: 0, wait: 0 })]) });
{ const ops = []; for (let g = 0; g <= 17; g++) ops.push(['gat', t(g * 24000), g]); ops.push(['gat', t(18 * 24000), -1]);   // GAT 00 (open), 1-15, 16 CUSTOM, beyond 16, then its own
  add({ name: 'sgate_gat_sweep', clock: running(480), frames: 19 * 24000, chain: chainOf([sgate({ pat: 2, m1: 0x96, m2: 0x69, div: 2, wait: 1 })]), ops }); }
add({ name: 'sgate_fxo', clock: running(480), frames: 60000, chain: chainOf([sgate({ pat: 3, on: false, div: 1, wait: 1 })]), ops: [['sw0', t(10000), 1], ['sw0', t(30000), 0], ['sw0', t(40000), -1], ['gat', t(45000), 5]] });
add({ name: 'sgate_div_sets', sig: 'loop', frames: 120000, clock: running(120), chain: chainOf([sgate({ pat: 5, div: 0, wait: 0 })]),
  msgs: [1, 2, 3, 4].map((d, i) => ({ b: (i + 1) * 150, msg: chainOf([sgate({ pat: 5 + i, div: d, len: 100 + 30 * i, wait: 0 })]) })) });
add({ name: 'sgate_stop', sig: 'loop', frames: 100000, clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 30001, stop: true }, { at: 60000, bpm: 120, beat: 2 }], chain: chainOf([sgate({ pat: 4, wait: 0, div: 2 })]) });

// Retrigger
add({ name: 'rtg_def', sig: 'loop', frames: 120000, chain: chainOf([retrig()]), ops: [['rtg', t(10000), 1], ['rtg', t(30000), 0], ['rtg', t(40000), 3], ['rtg', t(60000), 5], ['rtg', t(75000), 8], ['rtg', t(90000), -1], ['rtg', t(100000), 2]] });
add({ name: 'rtg_always', sig: 'loop', frames: 96000, chain: chainOf([retrig({ mode: 1, div: 2 })]) });
add({ name: 'rtg_pitch', sig: 'loop', frames: 96000, chain: chainOf([retrig({ mode: 1, div: 3, decay: 128, pitch: 36, gate: 128, mix: 200 })]) });
add({ name: 'rtg_pitch_down', sig: 'loop', frames: 96000, clock: running(147), chain: chainOf([retrig({ mode: 1, div: 1, decay: 20, pitch: 10, gate: 255, mix: 255 })]) });
add({ name: 'rtg_alt', sig: 'loop', frames: 96000, clock: running(90, 0.5), chain: chainOf([retrig({ mode: 1, div: 6, decay: 255, pitch: 30, gate: 0, mix: 100 })]) });
add({ name: 'rtg_fxo', sig: 'loop', frames: 96000, chain: chainOf([retrig({ mode: 1, div: 2, on: false })]), ops: [['sw0', t(10000), 1], ['sw0', t(25000), 0], ['sw0', t(40000), -1], ['sw0', t(55000), 1], ['sw0', t(70000), -1]] });   // momentary
{ const ops = []; for (let d = 1; d <= 8; d++) ops.push(['rtg', t((d - 1) * 20000), d], ['rtg', t((d - 1) * 20000 + 12000), -1]);
  add({ name: 'rtg_div_sweep', sig: 'loop', frames: 8 * 20000, chain: chainOf([retrig({ mode: 0, decay: 60, pitch: 26 })]), ops }); }
add({ name: 'rtg_tempo', sig: 'loop', frames: 150000, clock: [{ at: 0, bpm: 120 }, { at: 40000, bpm: 175 }, { at: 100000, bpm: 80 }], chain: chainOf([retrig({ mode: 0, div: 1, decay: 50 })]), ops: [['rtg', t(20000), 3], ['rtg', t(60000), -1], ['rtg', t(90000), 3], ['rtg', t(120000), 6]] });
add({ name: 'rtg_stopped_clock', sig: 'loop', frames: 96000, clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 30000, stop: true }], chain: chainOf([retrig({ mode: 1, div: 2 })]) });
add({ name: 'rtg_bp_none', sig: 'loop', frames: 60000, bpMode: 'none', chain: chainOf([retrig({ mode: 1, div: 2 })]) });
add({ name: 'rtg_bp_const', sig: 'loop', frames: 60000, bpMode: 'const', chain: chainOf([retrig({ mode: 1, div: 2 })]) });

// block sizes: the three in series, every command kind, a tempo change
for (const n of [1, 37, 128, 480, 1024]) {
  add({ name: `blk${n}`, sig: 'loop', frames: n === 1 ? 30000 : 96000, sizes: [n], clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 25000, bpm: 150 }], chain: chainOf([flex({ pat: 0x56, wait: 0, mix: 220 }), sgate({ pat: 2, div: 1, wait: 0 }), retrig({ mode: 0, div: 2 })]),
    ops: [['flx', t(8000), 0x2a], ['gat', t(12000), 3], ['rtg', t(16000), 2], ['rtg', t(20000), -1], ['sw1', t(22000), 0], ['sw1', t(26000), -1], ['flx', t(27000), 0x94]] });
}
add({ name: 'blk_mixed', sig: 'loop', frames: 96000, sizes: [128, 7, 130, 1, 99, 128, 4097, 300, 2, 64], clock: running(120), chain: chainOf([flex({ pat: 0x73, wait: 0 }), sgate({ pat: 5, wait: 0 }), retrig({ mode: 1, div: 2, on: false })]), ops: [['sw2', t(30000), 1], ['sw2', t(50000), -1]] });
// commands on block edges: at a block start, one frame after, one before, and between frames
add({ name: 'edges', clock: running(240), frames: 40960, chain: chainOf([flex({ pat: 1, wait: 1, xf: 2 }), sgate({ wait: 1, div: 2 }), retrig({ mode: 0 })]),
  ops: [['flx', t(128 * 10), 0x2a], ['flx', t(128 * 20 + 1), 0x33], ['flx', t(128 * 30 - 1), 0x45], ['gat', t(128 * 40), 4], ['gat', t(128 * 50 + 0.5), 0], ['gat', t(128 * 60), 8], ['rtg', t(128 * 70), 2], ['rtg', t(128 * 80 - 0.25), -1], ['rtg', t(128 * 90), 4],
    ['sw0', t(128 * 100), 0], ['sw0', t(128 * 110), -1], ['flx', t(128 * 120), 0], ['reset', t(128 * 130 + 1), -1], ['flx', t(128 * 140), 0x56]] });
// a chain set mid-run (settings change, the cores are kept; a slot comes and goes: a fresh core), FXV (pset) mid-block, a slot switched off and on (the 5 ms fade)
{ const mk = (p, on = [true, true, true]) => chainOf([flex({ pat: p, wait: 0, on: on[0] }), sgate({ pat: 3, wait: 0, on: on[1] }), retrig({ mode: 1, div: 2, on: on[2] })]);
  add({ name: 'host_set', sig: 'loop', frames: 96000, chain: mk(5, [true, false, false]), msgs: [{ b: 100, msg: mk(0x2a, [true, true, false]) }, { b: 200, msg: mk(0x2a, [true, true, true]) }, { b: 300, msg: mk(7, [false, true, true]) }, { b: 301, msg: mk(7, [true, true, true]) }, { b: 500, msg: chainOf([sgate({ pat: 3, wait: 0 })]) }, { b: 600, msg: mk(9) }] });
  const pv = (slot, key, over) => { const c = chainOf([fresh(key, over)])[0]; return { pset: { slot, p: c.p } }; };
  add({ name: 'host_pset', sig: 'loop', frames: 96000, chain: mk(5), q: [[1000 / SR + 1e-7, pv(0, 'flex', { pat: 0x2a, wait: 0 })], [20000.3 / SR, pv(2, 'retrig', { mode: 0, div: 4 })], [40000 / SR, pv(1, 'sgate', { pat: 8, wait: 0, div: 3 })], [60000 / SR, pv(9, 'flex', { pat: 3 })]] }); }
// the three in series with every command
add({ name: 'host_series', sig: 'loop', frames: 192000, clock: [{ at: 0, bpm: 120 }, { at: 100000, bpm: 140 }], chain: chainOf([flex({ pat: 0x44, wait: 0, mix: 180 }), sgate({ pat: 6, div: 2, wait: 0 }), retrig({ mode: 0, div: 3, decay: 80 })]),
  ops: [['flx', t(20000), 0x2a], ['gat', t(30000), 9], ['rtg', t(50000), 2], ['rtg', t(60000), -1], ['sw1', t(70000), 0], ['sw1', t(90000), -1], ['flx', t(110000), 0x94], ['rtg', t(120000), 7], ['sw0', t(130000), 0], ['sw0', t(150000), 1], ['flx', t(160000), -1]] });
// THE NODE'S SLEEP (ck): 18 s of a short burst of noise, then silence, then noise again at the end
const SL = 18 * SR;
add({ name: 'sleep_flex_off', sig: 'tail', frames: SL, chain: chainOf([flex({ pat: 5, wait: 0, on: false })]), ops: [['sw0', t(SL - 8000), 1]] });          // ck 1: sleeps once the silence is longer than the 13 s ring, clock running or not
add({ name: 'sleep_flex_on_running', sig: 'tail', frames: SL, chain: chainOf([flex({ pat: 5, wait: 0 })]) });                                           // ck 2 with the clock running: never
add({ name: 'sleep_flex_on_stopped', sig: 'tail', frames: SL, clock: [{ at: 0, stop: true }], chain: chainOf([flex({ pat: 5, wait: 0 })]) });         // ck 2, clock stopped: sleeps after 14 s
add({ name: 'sleep_flex_on_stops', sig: 'tail', frames: SL, clock: [{ at: 0, bpm: 120, beat: 0 }, { at: 3 * SR, stop: true }], chain: chainOf([flex({ pat: 5, wait: 0 })]) });   // the clock stops at 3 s: asleep from 14 s of silence on
add({ name: 'sleep_flex_bp_none', sig: 'tail', frames: SL, bpMode: 'none', chain: chainOf([flex({ pat: 5, wait: 0 })]) });                           // the param at its default 0: a running clock for the sleep rule
add({ name: 'sleep_retrig_on', sig: 'tail', frames: SL, clock: [{ at: 0, stop: true }], chain: chainOf([retrig({ mode: 1, div: 2 })]) });             // ck 3: never
add({ name: 'sleep_retrig_off', sig: 'tail', frames: SL, chain: chainOf([retrig({ mode: 1, div: 2, on: false })]) });                                  // switched off: ck 0, sleeps after 1 s
add({ name: 'sleep_sgate_on_stopped', sig: 'tail', frames: SL, clock: [{ at: 0, stop: true }], chain: chainOf([sgate({ pat: 5, wait: 0 })]) });
add({ name: 'sleep_sgate_off', sig: 'tail', frames: SL, chain: chainOf([sgate({ pat: 5, wait: 0, on: false })]) });
add({ name: 'sleep_rest', sig: 'loop', frames: 96000, chain: chainOf([flex({ pat: 5, wait: 0 }), retrig({ mode: 1 })]), msgs: [{ b: 100, msg: { rest: true } }, { b: 500, msg: { rest: false } }] });   // a muted track: silence in, the tempo-locked slots still keep time

// ---- the run ----------------------------------------------------------------------------------------------------------------------------
export function runCase(c) {
  const n = c.frames, [iL, iR] = SIG[c.sig](n), bpAll = clock(n, c.clock), { node, clk } = mkNode(c.chain);
  const lanes = {}; for (const k of ['sw0', 'sw1', 'sw2', 'sw3', 'sw4', 'sw5', 'sw6', 'sw7', 'flx', 'gat', 'rtg']) lanes[k] = new Auto(-1);
  for (const [k, tm, v] of c.ops || []) { if (k === 'reset') { for (const L of Object.values(lanes)) { L.cancel(tm); L.set(-1, tm); } } else lanes[k].set(v, tm); }
  for (const [at, msg] of c.q || []) { const q = node.__q || (node.__q = []); let i = q.length; while (i > 0 && q[i - 1][0] > at) i--; q.splice(i, 0, [at, msg]); }
  const oL = new Float32Array(n), oR = new Float32Array(n), sizes = c.sizes || [BLK], msgs = c.msgs || []; let f0 = 0, b = 0, si = 0, slept = 0;
  while (f0 < n) {
    const m = Math.min(sizes[si++ % sizes.length], n - f0), t0 = f0 / SR; clk.set(t0);
    for (const e of msgs) if (e.b === b) node.port.onmessage({ data: e.msg });
    const P = { bp: c.bpMode === 'const' ? Float32Array.of(bpAll[f0]) : c.bpMode === 'none' ? Float32Array.of(0) : bpAll.slice(f0, f0 + m) };
    for (const k in lanes) { lanes[k].prune(t0); P[k] = Float32Array.of(Math.fround(lanes[k].at(t0))); }
    const a = iL.slice(f0, f0 + m), a2 = iR.slice(f0, f0 + m), o = [new Float32Array(m), new Float32Array(m)];
    node.process([[a, a2], [], []], [o], P);
    oL.set(o[0], f0); oR.set(o[1], f0); f0 += m; b++;
  }
  return { n, iL, iR, oL, oR, bpAll };
}
export const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';

// ---- map() value sets (the three effects: normTfx + tfxChain) -------------------------------------------------------------------------------
function mapSets() {
  const out = [], r = lcg(2025);
  for (const e of E) {
    const sets = [];
    for (let i = 0; i < 200; i++) {
      const o = { type: e.key, on: i % 13 !== 12 }; for (const p of e.ps) o[p.k] = i === 0 ? p.d : i === 1 ? p.min : i === 2 ? p.max : p.min + Math.floor(r() * (p.max - p.min + 1));
      if (i % 11 === 10) e.ps.forEach((p, j) => { const m = (i + j) % 6; if (m === 0) o[p.k] = p.max + 40; else if (m === 1) o[p.k] = String(o[p.k]); else if (m === 2) delete o[p.k]; else if (m === 3) o[p.k] = -5.7; else if (m === 4) o[p.k] = null; else o[p.k] = p.min + 0.4; });
      if (i % 7 === 3) delete o.wait;   // a slot saved before STARTS (flex, sgate)
      if (i % 17 === 5) o.wait = null;
      const sr = i % 5 === 4 ? 44100 : SR, bpm = [120, 87.5, 140, 33, 999][i % 5];
      const norm = F.normTfx(o), ch = F.tfxChain([norm], sr, bpm)[0];
      sets.push({ obj: o, on: ch.on, key: ch.key, v: e.ps.map(p => norm[p.k]), sr, bpm, restartN: 0, p: ch.p });
    }
    out.push({ key: e.key, id: e.id, sets });
  }
  return out;
}

export function main(argv) {
  const golden = argv[0] === '--golden' || argv[0] === '--check', outDir = golden ? null : (argv[0] || '/tmp/u15/fxclk-ref');
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
    if (miss.length) { console.error(`fx-clk-dump: ${miss.length} reference files missing or empty in ${outDir}: ${miss.slice(0, 5).join(', ')}`); process.exit(1); }
  }
  const inc = ['// GENERATED by native/tools/fx-clk-dump.mjs (--golden) from the ThunderFx worklet (Flex, Step gate, Retrigger cut out of index.html). Do not edit: a mismatch means the port is wrong.',
    '// FNV-1a 64 over each case\'s float32 output (L then R), the real worklet run in Node 22.',
    'struct FxClkGolden { const char* name; unsigned long long hash; int floats; };', 'static const FxClkGolden kFxClkGolden[] = {', ...rows, '};', 'static const int kFxClkGoldenN = ' + rows.length + ';', ''].join('\n');
  const incPath = path.join(ROOT, 'native/tests/golden/fxclk_golden.inc');
  if (argv[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
  else if (argv[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('fxclk_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('fxclk_golden.inc is current (' + rows.length + ' cases)'); }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
