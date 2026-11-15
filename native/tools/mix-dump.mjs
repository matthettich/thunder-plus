// U09: THE MIXER'S REFERENCE A (exact, Node only) and the busPlan reference.
//
// In the web app the mixer is Web Audio nodes built by index.html's makePlayer, with part of it (TVL / TVA / TPN / MVOL / MOUT) driven
// from the audio thread by the core's mix lanes (core/seq-core.js: mixEv, mixOut, mixFill). There is no JS that does the nodes' own
// arithmetic, so this file has a small MODEL of it (class Model below): the page's strip, node for node, in float32 as Chrome's nodes
// are, with every level an `Auto` timeline (the core's own class, run in a Node `vm`) and the gain laws taken from the app's own source
// (curve / trackGain / busPlan / chainsFeed from core/seq-core.js, trimGain / audible from index.html, both by the vm, never retyped).
// The model was checked against real Web Audio (an OfflineAudioContext in headless Chrome building the same graph with the page
// player's own AudioParam calls): native/tools/mix-webaudio-check.mjs; its result is quoted in native/tests/golden/mix-webaudio-check.txt.
//
//   node native/tools/mix-dump.mjs [outDir]     writes <outDir>/<case>.json (the case), <case>.in.f32 (the tracks' inputs, planar: per
//                                               input L then R) and <case>.f32 (the master output, L then R); default /tmp/u09/mix-ref
//   node native/tools/mix-dump.mjs --golden     writes native/tests/golden/mix_golden.inc (FNV-1a 64 of every case's output and of the
//                                               busPlan results)
//   node native/tools/mix-dump.mjs --check      exits 1 if the committed mix_golden.inc differs
//   node native/tools/mix-dump.mjs --busplan [outDir]   writes <outDir>/busplan.json: the cases and what the JS busPlan / chainsFeed /
//                                               audible give for them (for ctest `busplan`); default /tmp/u09/busplan
//
// THE STRIP (makePlayer, "GAIN STAGING"), per track t of 0..NT (NT is the preview bus: every level 1):
//   voices -> inp (TRIM) -> [chains: dry 0 / wet 1 when the track runs through chains] -> [track FX] -> ins -> g (VOL x audible) -> cg (the
//   core's TVL / TVA multiplier) -> p (StereoPanner: the mixer's pan + the core's TPN offset, clamped to -1..1) -> dl (0 s) -> out
//   ins -> pre (audible ? 1 : 0, an explicit stereo pair) -> a PRE send's bus;  p -> a POST send's bus;  a pure source leaves `out`.
//   out (MASTER VOL + the core's MVOL offset) -> [master FX, mastering, limiter] -> mo (OUT + the core's MOUT offset) -> the device.
// A bus exists only while its track has a chains node (busSync: `b.chn`); its sources arrive as extra inputs of that node, so what a bus
// does with them is the patch's business. The model's stand-in chain (and the C++ test's, and Chrome's in the check) adds the track's
// own input and every source 1:1.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
export const SR = 48000, NT = 32;
const coreSrc = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
const grab = re => { const m = html.match(re); if (!m) throw new Error('index.html: not found: ' + re); return m[0]; };
// the page's own source for what the core copy does not hold
export const PAGE_SRC = [grab(/^const TRIM_0 = .*$/m), grab(/^const trimDb = .*$/m), grab(/^const trimGain = .*$/m), grab(/^const audible = t => \{[\s\S]*?\n  return false; \};/m)].join('\n');
// the same laws for a blank page (the Chrome check): the core copy's lines + the page's
export const LAW_SRC = ['const NT = 32, MT = NT;', coreSrc.match(/^const curve = .*$/m)[0], coreSrc.match(/^const TRK_UNITY = .*$/m)[0], coreSrc.match(/^const trackGain = .*$/m)[0],
  coreSrc.match(/^const clamp = .*$/m)[0], coreSrc.match(/^const int = .*$/m)[0], coreSrc.match(/^function chainsTracks\(\)\{[\s\S]*?\n\}/m)[0], coreSrc.match(/^const chainsFeed = .*$/m)[0],
  coreSrc.match(/^let busPlanH = null;\nlet busPlanVal = null;\nfunction busPlan\(\)\{[\s\S]*?\n\}/m)[0], 'let S = null;', PAGE_SRC].join('\n');

export function makeCore() {
  let Cls = null;
  const ctx = vm.createContext({ console, registerProcessor(n, c) { Cls = c; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
    sampleRate: SR, currentTime: 0, currentFrame: 0 });
  vm.runInContext(coreSrc + '\n' + PAGE_SRC + '\n;globalThis.__u09 = { Auto, curve, trackGain, trimGain, audible, busPlan, chainsFeed, chainsTracks, setS(s){ S = s; busPlanH = null; busPlanVal = null; } };', ctx, { filename: 'seq-core.js' });
  return { ctx, proc: new Cls(), U: ctx.__u09 };
}

// ---- the song's mixer part ----------------------------------------------------------------------------------------------------
const MIX0 = () => ({ vol: 224, trim: 128, pan: 128, mute: false, solo: false });
export function songOf(C) {
  const mix = Array.from({ length: NT }, (_, t) => Object.assign(MIX0(), (C.mix || {})[t]));
  const chainOn = Array.from({ length: NT }, (_, t) => !!(C.chainOn || []).includes(t));
  return { bpm: 120, mix, master: C.master == null ? 255 : C.master, mout: C.mout == null ? 255 : C.mout, chainOn, chainPatch: C.patch || null, insts: [] };
}
// a chains patch from a short description: [[kind, track(1-based), pre?], ...] modules m0, m1, ... and cables [[from, to], ...]
export const patch = (mods, cables) => ({ format: 'chains.patch', version: 1,
  modules: mods.map(([type, track, pre], i) => ({ id: 'm' + i, type, params: type === 'trackIn' ? { track, pre: pre ? 1 : 0 } : type === 'chainOut' ? { track } : {} })),
  cables: cables.map(([a, b]) => ({ from: ['m' + a, 'out'], to: ['m' + b, 'in'] })) });

// ---- the inputs (what the voices would have written into the tracks) ------------------------------------------------------------
// k picks the kind: 0 a decaying sine (mono), 1 noise (mono), 2 a stereo pair (a sine left, a slower one + noise right)
export function signal(kind, t, n) {
  const L = new Float32Array(n), R = new Float32Array(n); let s = (12345 + t * 7919) >>> 0;
  const f1 = 180 + 97 * t, f2 = 310 + 53 * t;
  for (let i = 0; i < n; i++) {
    s = (Math.imul(s, 1664525) + 1013904223) >>> 0; const nz = (s / 4294967296) * 2 - 1;
    if (kind === 0) L[i] = R[i] = 0.7 * Math.sin(2 * Math.PI * f1 * i / SR) * (1 - 0.3 * i / n);
    else if (kind === 1) L[i] = R[i] = 0.5 * nz;
    else { L[i] = 0.6 * Math.sin(2 * Math.PI * f1 * i / SR); R[i] = 0.45 * Math.sin(2 * Math.PI * f2 * i / SR + 1) + 0.1 * nz; }
  }
  return [L, R];
}

// ---- the cases ------------------------------------------------------------------------------------------------------------------
const F = x => x / SR, FB = (b, o = 0, N = 128) => F(b * N + o);
const ev = (b, type, track, v, o = 0) => [b, Object.assign({ op: 'ev', type, v, t: FB(b + 3, o) }, track == null ? {} : { track })];   // booked 3 blocks ahead, as the sequencer books ahead
const set = (b, tr, k, v, x) => [b, Object.assign({ op: 'set', tr, k, v }, x || {})];
const st = t => ({ t, kind: 2, mono: false }), mo = (t, kind = 0) => ({ t, kind, mono: true });
// ins: the tracks that get an input. A mono input is a one-channel connection (the panner's mono law); the app's own voices are always stereo.
export const CASES = {
  fader_const: { ins: [st(0)], mix: { 0: { vol: 200 } }, cmds: [] },
  fader_plus6: { ins: [st(0)], mix: { 0: { vol: 255 } }, cmds: [] },
  fader_zero: { ins: [st(0)], mix: { 0: { vol: 0 } }, cmds: [] },
  fader_tvl: { ins: [st(0)], mix: { 0: { vol: 208 } }, cmds: [ev(4, 'tvl', 0, 64, 17), ev(30, 'tvl', 0, 255), ev(55, 'tvl', 0, 128, 90)] },
  // a TVL that interrupts one still moving (web: 'stale': Chrome's own setTargetAtTime then starts from the value ONE SAMPLE BEFORE the
  // event's time, measured 15 Nov with Chrome 153; the core's Auto, which the port follows, starts from the value at the event's time, as the Web Audio text says)
  fader_tvl_quick: { web: 'stale', ins: [st(0)], mix: { 0: { vol: 208 } }, cmds: [ev(4, 'tvl', 0, 64, 17), ev(30, 'tvl', 0, 255), ev(31, 'tvl', 0, 128, 90)] },
  fader_tva: { ins: [st(0)], mix: { 0: { vol: 208 } }, cmds: [ev(4, 'tva', 0, 96, 5), ev(30, 'tva', 0, 255, 77)] },
  fader_set: { ins: [st(0)], cmds: [set(6, 0, 'vol', 96), set(40, 0, 'vol', 240, { at: FB(40, 51) })] },
  fader_set_ramp: { ins: [st(0)], cmds: [set(6, 0, 'vol', 96, { ramp: 300 }), set(40, 0, 'vol', 240, { ramp: 0 })] },
  trim_00: { ins: [st(0)], mix: { 0: { trim: 0 } }, cmds: [] },
  trim_80: { ins: [st(0)], mix: { 0: { trim: 128 } }, cmds: [] },
  trim_ff: { ins: [st(0)], mix: { 0: { trim: 255 } }, master: 64, cmds: [] },
  trim_set: { ins: [st(0)], cmds: [set(5, 0, 'trim', 30), set(40, 0, 'trim', 200, { at: FB(40, 9) })] },
  pan_mono_l: { ins: [mo(0)], mix: { 0: { pan: 0 } }, cmds: [] },
  pan_mono_c: { ins: [mo(0)], mix: { 0: { pan: 128 } }, cmds: [] },
  pan_mono_r: { ins: [mo(0)], mix: { 0: { pan: 255 } }, cmds: [] },
  pan_mono_70: { ins: [mo(0, 1)], mix: { 0: { pan: 70 } }, cmds: [] },
  pan_st_l: { ins: [st(0)], mix: { 0: { pan: 0 } }, cmds: [] },
  pan_st_c: { ins: [st(0)], mix: { 0: { pan: 128 } }, cmds: [] },
  pan_st_r: { ins: [st(0)], mix: { 0: { pan: 255 } }, cmds: [] },
  pan_st_70: { ins: [st(0)], mix: { 0: { pan: 70 } }, cmds: [] },
  pan_st_200: { ins: [st(0)], mix: { 0: { pan: 200 } }, cmds: [] },
  pan_tpn_mono: { ins: [mo(0)], mix: { 0: { pan: 100 } }, cmds: [ev(4, 'tpn', 0, 255, 33), ev(30, 'tpn', 0, 0), ev(50, 'tpn', 0, 128, 64)] },
  pan_tpn_st: { ins: [st(0)], mix: { 0: { pan: 100 } }, cmds: [ev(4, 'tpn', 0, 255, 33), ev(30, 'tpn', 0, 0), ev(50, 'tpn', 0, 128, 64)] },
  pan_set: { ins: [st(0), mo(1)], cmds: [set(6, 0, 'pan', 0), set(6, 1, 'pan', 255), set(40, 0, 'pan', 180, { at: FB(40, 100) })] },
  mute_mid: { ins: [st(0), mo(1, 1)], cmds: [set(8, 0, 'mute', 1, { at: FB(8, 61) }), set(45, 0, 'mute', 0, { at: FB(45, 3) })] },
  mute_init: { ins: [st(0), mo(1)], mix: { 0: { mute: true } }, cmds: [set(30, 0, 'mute', 0)] },
  mute_ramp: { ins: [st(0)], cmds: [set(8, 0, 'mute', 1, { at: FB(8, 61), ramp: 96 }), set(45, 0, 'mute', 0, { ramp: 480 })] },
  mute_now: { ins: [st(0)], cmds: [set(8, 0, 'mute', 1, { ramp: 0 }), set(45, 0, 'mute', 0, { ramp: 0, at: FB(45, 77) })] },
  solo_mid: { ins: [st(0), mo(1, 1), st(2)], mix: { 2: { mute: true } }, cmds: [set(8, 1, 'solo', 1, { at: FB(8, 40) }), set(30, 2, 'solo', 1), set(50, 1, 'solo', 0, { at: FB(50, 99) }), set(60, 2, 'solo', 0)] },
  // tracks 1 and 2 into bus 3 (both pure: their Track In reaches only track 4's Chain Out); POST: after their fader and pan
  send_post: { ins: [st(0), st(1), mo(2, 1), mo(3)], mix: { 0: { vol: 180 }, 1: { vol: 160, pan: 60 }, 2: { vol: 200, pan: 220 }, 3: { vol: 210, pan: 100 } }, chainOn: [1, 2, 3],
    patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4], ['fx']], [[0, 3], [1, 3], [3, 2]]), cmds: [set(20, 1, 'vol', 40), ev(40, 'tpn', 2, 20)] },
  // PRE: the bus hears them before their fader and pan (the fader move and the TPN change nothing in the bus); mute still silences
  send_pre: { ins: [st(0), st(1), mo(2, 1), mo(3)], mix: { 0: { vol: 180 }, 1: { vol: 160, pan: 60 }, 2: { vol: 200, pan: 220 }, 3: { vol: 210, pan: 100 } }, chainOn: [1, 2, 3],
    patch: patch([['trackIn', 2, 1], ['trackIn', 3, 1], ['chainOut', 4], ['fx']], [[0, 3], [1, 3], [3, 2]]), cmds: [set(20, 1, 'vol', 40), ev(40, 'tpn', 2, 20), set(55, 2, 'mute', 1, { at: FB(55, 13) })] },
  // a send: track 1 keeps its own Chain Out (not pure) and also feeds bus 3 (PRE); track 2 feeds it POST and is pure
  send_mixed: { ins: [st(1), mo(2, 1)], mix: { 1: { vol: 190, pan: 90 }, 2: { vol: 224, pan: 160 }, 3: { vol: 200 } }, chainOn: [1, 2, 3],
    patch: patch([['trackIn', 2, 1], ['chainOut', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 1], [0, 3], [2, 3]]), cmds: [ev(20, 'tvl', 1, 80)] },
  // a bus of buses: 1 -> bus 2 -> bus 3; the bus's own fader, pan and TRIM
  bus_of_bus: { ins: [st(0), st(1), mo(2)], mix: { 0: { pan: 40 }, 1: { vol: 200, pan: 200 }, 2: { vol: 180, pan: 90, trim: 100 } }, chainOn: [0, 1, 2],
    patch: patch([['trackIn', 1], ['chainOut', 2], ['fx'], ['chainOut', 3]], [[0, 1], [1, 2], [2, 3]]), cmds: [set(20, 1, 'vol', 90), set(40, 2, 'trim', 160)] },
  // soloing a bus keeps its sources playing into it; soloing a source keeps its bus
  solo_quick: { web: 'stale', ins: [st(0), mo(1, 1)], cmds: [set(8, 1, 'solo', 1), set(9, 1, 'solo', 0, { at: FB(9, 30) })] },
  bus_solo: { blocks: 100, ins: [st(0), st(1), mo(2, 1), mo(3)], mix: { 1: { pan: 60 }, 2: { pan: 220 } }, chainOn: [1, 2, 3],
    patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), cmds: [set(10, 3, 'solo', 1), set(30, 3, 'solo', 0), set(52, 1, 'solo', 1, { at: FB(52, 30) }), set(74, 1, 'solo', 0)] },
  // the patch says bus, but track 4 does not run through chains (CHN off): no bus, the sources play on their own
  bus_absent: { ins: [st(1), mo(2, 1)], mix: { 1: { pan: 60 }, 2: { pan: 220 } }, chainOn: [1, 2],
    patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), cmds: [] },
  master_const: { ins: [st(0), mo(5, 1)], master: 180, mout: 200, cmds: [] },
  master_mvol: { ins: [st(0)], master: 223, cmds: [ev(4, 'mvol', null, 100, 20), ev(30, 'mvol', null, 255, 5), ev(50, 'mvol', null, 40, 100)] },
  // a second MVOL inside the first one's 4 ms ramp (web: 'page': the PAGE player jumps to the first one's target and ramps from there;
  // the core holds the value it has reached and ramps from that, which the port follows)
  master_mvol_quick: { web: 'page', ins: [st(0)], master: 223, cmds: [ev(4, 'mvol', null, 100, 20), ev(30, 'mvol', null, 255, 5), ev(30, 'mvol', null, 40, 100)] },
  master_mout: { ins: [st(0)], mout: 240, cmds: [ev(4, 'mout', null, 100, 20), ev(30, 'mout', null, 255, 5)] },
  master_set: { ins: [st(0)], master: 223, cmds: [set(10, NT, 'vol', 128), set(30, NT, 'out', 128, { at: FB(30, 31) }), set(55, NT, 'vol', 255, { ramp: 200 })] },
  preview_bus: { ins: [st(NT), mo(0)], master: 200, cmds: [] },
  combo: { blocks: 170, ins: [st(0), mo(1, 1), st(2), mo(3), st(7)], mix: { 0: { vol: 200, pan: 100, trim: 140 }, 1: { vol: 230, pan: 30 }, 2: { vol: 190, pan: 250, trim: 90 }, 3: { vol: 224 }, 7: { vol: 150, pan: 128, mute: true } },
    master: 223, mout: 250, chainOn: [0, 2, 3], patch: patch([['trackIn', 1, 1], ['trackIn', 3], ['chainOut', 4], ['chainOut', 3]], [[0, 2], [1, 2], [1, 3]]),
    cmds: [set(0, NT, 'vol', 240), set(4, 2, 'vol', 120), set(8, 7, 'mute', 0, { at: FB(8, 100) }), set(12, 3, 'solo', 1), set(18, 0, 'trim', 255), set(24, 3, 'pan', 30, { ramp: 240 }), set(30, 3, 'solo', 0, { at: FB(30, 1) }),
      ev(90, 'tvl', 0, 128, 3), ev(95, 'tpn', 1, 200, 60), ev(100, 'mvol', null, 180), ev(110, 'tva', 2, 255, 11), ev(120, 'mout', null, 128, 40), ev(130, 'tpn', 0, 0), ev(140, 'tvl', 0, 255), ev(150, 'mvol', null, 255, 77)] },
  // the mixer moved by hand while the song's own glides run (web: 'live': the page's applyMix then re-aims every param of every track with its
  // 12 ms law, the song's 4 ms glides included, and a TVA lands on a fader still gliding; the core keeps the song's lanes apart from the mixer's)
  combo_live: { web: 'live', blocks: 120, ins: [st(0), mo(1, 1), st(2), mo(3), st(7)], mix: { 0: { vol: 200, pan: 100, trim: 140 }, 1: { vol: 230, pan: 30 }, 2: { vol: 190, pan: 250, trim: 90 }, 3: { vol: 224 }, 7: { vol: 150, pan: 128, mute: true } },
    master: 223, mout: 250, chainOn: [0, 2, 3], patch: patch([['trackIn', 1, 1], ['trackIn', 3], ['chainOut', 4], ['chainOut', 3]], [[0, 2], [1, 2], [1, 3]]),
    cmds: [set(0, NT, 'vol', 240), ev(2, 'tvl', 0, 128, 3), ev(5, 'tpn', 1, 200, 60), ev(9, 'mvol', null, 180), set(15, 2, 'vol', 120), set(22, 7, 'mute', 0, { at: FB(22, 100) }), ev(30, 'tva', 2, 255, 11), set(40, 3, 'solo', 1),
      ev(50, 'mout', null, 128, 40), set(70, 0, 'trim', 255), ev(80, 'tpn', 0, 0), set(86, 3, 'solo', 0, { at: FB(86, 1) }), set(100, 3, 'pan', 30, { ramp: 240 })] },
  // a block size that is not Web Audio's (events and commands land mid-block everywhere)
  combo_n7: { N: 7, blocks: 1800, ins: [st(0), mo(1, 1)], mix: { 0: { vol: 200, pan: 100 }, 1: { pan: 30 } }, master: 223,
    cmds: [[40, { op: 'ev', type: 'tvl', track: 0, v: 99, t: F(40 * 7 + 3 + 500) }], [300, { op: 'set', tr: 1, k: 'mute', v: 1 }], [600, { op: 'ev', type: 'tpn', track: 0, v: 250, t: F(600 * 7 + 1000) }],
      [900, { op: 'set', tr: 1, k: 'mute', v: 0, at: F(900 * 7 + 5) }], [1200, { op: 'ev', type: 'mvol', v: 120, t: F(1200 * 7 + 100) }]] },
};
for (const C of Object.values(CASES)) C.cmds.forEach((c, i) => { if (i && c[0] < C.cmds[i - 1][0]) throw new Error('a case\'s commands must be in block order'); });
export const CASE_NAMES = Object.keys(CASES);

// ---- the model ----------------------------------------------------------------------------------------------------------------
const f = Math.fround;
// StereoPannerNode (Chrome's equal-power laws; the gains in double, the sample rounded to float32):
//   mono in:   x = (pan + 1) / 2;            L = in cos(x pi/2), R = in sin(x pi/2)
//   stereo in: pan <= 0: x = pan + 1;        L = inL + inR cos(x pi/2), R = inR sin(x pi/2)
//              pan > 0:  x = pan;            L = inL cos(x pi/2),       R = inR + inL sin(x pi/2)
export function panMono(x, pan, o) { const a = (pan * 0.5 + 0.5) * (Math.PI / 2); o[0] = f(x * Math.cos(a)); o[1] = f(x * Math.sin(a)); }
export function panStereo(l, r, pan, o) {
  if (pan <= 0) { const a = (pan + 1) * (Math.PI / 2); o[0] = f(l + r * Math.cos(a)); o[1] = f(r * Math.sin(a)); }
  else { const a = pan * (Math.PI / 2); o[0] = f(l * Math.cos(a)); o[1] = f(r + l * Math.sin(a)); }
}

export class Model {
  constructor(S, N) {
    const { ctx, proc, U } = makeCore(); this.ctx = ctx; this.proc = proc; this.U = U; this.S = S; this.N = N; U.setS(S);
    const A = U.Auto; this.Auto = A;
    proc.mixReset();
    this.outs = []; this.outs.length = NT + 6;
    this.G = Array.from({ length: NT }, () => new Float32Array(N)); this.P = Array.from({ length: NT }, () => new Float32Array(N)); this.M = new Float32Array(N); this.O = new Float32Array(N);
    this.outs[NT + 1] = this.G; this.outs[NT + 2] = this.P; this.outs[NT + 3] = [this.M]; this.outs[NT + 4] = [this.O];
    // the page's own params, as a render sets them (applyMix, live false: at once)
    this.tr = []; for (let t = 0; t <= NT; t++) { const v = this.want(t); this.tr.push({ inp: new A(v.tg), g: new A(v.gv), pre: new A(v.prv), pan: new A(v.pv), last: v, mono: false,
      c: { inp: new Float32Array(N), g: new Float32Array(N), pre: new Float32Array(N), pan: new Float32Array(N) }, L: new Float32Array(N), R: new Float32Array(N), pL: new Float32Array(N), pR: new Float32Array(N), ch: 0 }); }
    this.out = new A(U.curve(S.master)); this.mo = new A(U.curve(S.mout == null ? 255 : S.mout)); this.cOut = new Float32Array(N); this.cMo = new Float32Array(N);
    this.inL = Array.from({ length: NT + 1 }, () => new Float32Array(N)); this.inR = Array.from({ length: NT + 1 }, () => new Float32Array(N));
    this.plan();
  }
  // applyMix's values for track t
  want(t) {
    if (t >= NT) return { gv: 1, pv: 0, tg: 1, prv: 1 };
    const U = this.U, m = this.S.mix[t], on = U.audible(t);
    return { gv: on ? U.trackGain(m.vol) : 0, pv: Math.max(-1, Math.min(1, (m.pan - 128) / 127)), tg: U.trimGain(m.trim == null ? 128 : m.trim), prv: on ? 1 : 0 };
  }
  // busSync: which buses are there (their track runs through chains), who is taken off the master, the order to work in
  plan() {
    const U = this.U, P = U.busPlan(); this.chn = new Set(); for (let t = 0; t < NT; t++) if (U.chainsFeed(t) && this.S.chainPatch && this.S.chainPatch.format === 'chains.patch') this.chn.add(t);
    this.feeds = new Map(); this.routed = new Set();
    for (const [bt, src] of P.feeds) if (this.chn.has(bt)) { this.feeds.set(bt, src.slice(0, 32).map(t => ({ t, pre: P.pre.has(t) }))); for (const t of src) if (P.pure.has(t)) this.routed.add(t); }
    const order = [], seen = new Set(), go = t => { if (seen.has(t)) return; seen.add(t); for (const s of this.feeds.get(t) || []) go(s.t); order.push(t); };
    for (let t = 0; t <= NT; t++) go(t); this.order = order;
  }
  // a command: an event of the sequencer's (the core's mixEv) or mix.set {tr, k, v, ramp?, at?}
  cmd(c, cur) {
    const S = this.S, U = this.U;
    if (c.op === 'ev') { this.proc.mixEv(JSON.parse(JSON.stringify(c))); return; }
    const at = Math.max(c.at == null ? cur : c.at, cur);
    // no ramp given: the page's law (applyMix, live: an approach with a 12 ms time constant); ramp 0: at once; ramp n: linear over n frames
    const mv = (A, v) => { if (c.ramp == null) A.target(v, at, 0.012); else if (c.ramp > 0) { A.hold(at); A.lin(v, at + c.ramp / SR); } else { A.cancel(at); A.set(v, at); } };
    if (c.tr >= NT) {
      if (c.k === 'vol') { S.master = c.v; if (c.ramp == null) { this.out.cancel(at); this.out.set(U.curve(c.v), at); } else mv(this.out, U.curve(c.v)); }   // rtRoute: rt.out.gain.value = curve(S.master)
      else if (c.k === 'out') { S.mout = c.v; mv(this.mo, U.curve(c.v)); }                                                                                 // moApply
      return;
    }
    const m = S.mix[c.tr];
    if (c.k === 'vol') m.vol = c.v; else if (c.k === 'trim') m.trim = c.v; else if (c.k === 'pan') m.pan = c.v; else if (c.k === 'mute') m.mute = !!c.v; else if (c.k === 'solo') m.solo = !!c.v; else return;
    for (let t = 0; t < NT; t++) { const T = this.tr[t], w = this.want(t), l = T.last;   // applyMix: only what moved gets a new event
      if (w.gv !== l.gv) mv(T.g, w.gv); if (w.pv !== l.pv) mv(T.pan, w.pv); if (w.tg !== l.tg) mv(T.inp, w.tg); if (w.prv !== l.prv) mv(T.pre, w.prv); T.last = w; }
  }
  fill(C, A, cur) { const N = this.N, dt = 1 / SR; A.prune(cur); const c = A.constBlk(cur, cur + N * dt); if (c != null) { C.fill(c); return; } A.fill(C, cur, dt, N); }
  // one block at time cur: the inputs are in inL / inR (mono tracks: inL); writes oL / oR
  block(cur, oL, oR) {
    const N = this.N; this.ctx.currentTime = cur;
    this.proc.mixOut(this.outs, N, 1 / SR);
    this.fill(this.cOut, this.out, cur); this.fill(this.cMo, this.mo, cur);
    const o = [0, 0];
    for (const t of this.order) {
      const T = this.tr[t], c = T.c; this.fill(c.inp, T.inp, cur); this.fill(c.g, T.g, cur); this.fill(c.pre, T.pre, cur); this.fill(c.pan, T.pan, cur);
      const iL = this.inL[t], iR = this.inR[t], hasChn = this.chn.has(t), mono = T.mono && !hasChn, fd = this.feeds.get(t) || [];
      const G = t < NT ? this.G[t] : null, Pn = t < NT ? this.P[t] : null;
      for (let i = 0; i < N; i++) {
        let l = f(iL[i] * c.inp[i]), r = mono ? l : f((T.mono ? iL[i] : iR[i]) * c.inp[i]);                 // inp (TRIM)
        if (hasChn) for (const s of fd) { const q = this.tr[s.t]; l = f(l + (s.pre ? q.L[i] : q.pL[i])); r = f(r + (s.pre ? q.R[i] : q.pR[i])); }   // the stand-in chain: own input + every source
        T.L[i] = f(l * c.pre[i]); T.R[i] = f(r * c.pre[i]);                                                 // pre (an explicit stereo pair)
        l = f(l * c.g[i]); r = f(r * c.g[i]);                                                               // g
        if (G) { l = f(l * G[i]); r = f(r * G[i]); }                                                        // cg
        const pan = Math.max(-1, Math.min(1, Pn ? f(c.pan[i] + Pn[i]) : c.pan[i]));                         // p's pan: its own value + the core's offset, clamped
        if (mono) panMono(l, pan, o); else panStereo(l, r, pan, o);
        T.pL[i] = o[0]; T.pR[i] = o[1];
      }
    }
    for (let i = 0; i < N; i++) {
      let l = 0, r = 0; for (let t = 0; t <= NT; t++) if (!this.routed.has(t)) { l = f(l + this.tr[t].pL[i]); r = f(r + this.tr[t].pR[i]); }
      const gm = f(this.cOut[i] + this.M[i]), go = f(this.cMo[i] + this.O[i]);
      oL[i] = f(f(l * gm) * go); oR[i] = f(f(r * gm) * go);
    }
  }
}

export function inputsOf(C) { const N = C.N || 128, n = (C.blocks || 80) * N; return C.ins.map(q => signal(q.kind, q.t, n)); }
export function runCase(name) {
  const C = CASES[name], N = C.N || 128, blocks = C.blocks || 80, n = blocks * N, S = songOf(C), M = new Model(S, N), ins = inputsOf(C);
  C.ins.forEach(q => { M.tr[q.t].mono = !!q.mono; });
  const L = new Float32Array(n), R = new Float32Array(n), oL = new Float32Array(N), oR = new Float32Array(N);
  for (let b = 0; b < blocks; b++) {
    const cur = b * N / SR;
    for (const [bb, c] of C.cmds) if (bb === b) M.cmd(c, cur);
    C.ins.forEach((q, k) => { M.inL[q.t].set(ins[k][0].subarray(b * N, b * N + N)); M.inR[q.t].set(ins[k][1].subarray(b * N, b * N + N)); });
    M.block(cur, oL, oR); L.set(oL, b * N); R.set(oR, b * N);
  }
  const total = new Float32Array(2 * n); total.set(L, 0); total.set(R, n);
  const inp = new Float32Array(C.ins.length * 2 * n); ins.forEach((q, k) => { inp.set(q[0], k * 2 * n); inp.set(q[1], k * 2 * n + n); });
  const S0 = songOf(C);
  const meta = { id: name, N, blocks, sr: SR, S: { mix: S0.mix, master: S0.master, mout: S0.mout, chainOn: S0.chainOn, chainPatch: S0.chainPatch }, ins: C.ins, cmds: C.cmds.map(([b, c]) => Object.assign({ b }, c)) };
  return { total, inp, meta, L, R };
}

// ---- busPlan ------------------------------------------------------------------------------------------------------------------
const on = (...t) => t.map(x => x - 1);   // 1-based track numbers, as the patch has them
export const BUSPLANS = {
  none: { patch: null, chainOn: [] },
  not_a_patch: { patch: { format: 'other', modules: [{ id: 'a', type: 'trackIn', params: { track: 1 } }] }, chainOn: on(1, 2) },
  one_bus: { patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4], ['fx']], [[0, 3], [1, 3], [3, 2]]), chainOn: on(2, 3, 4) },
  one_bus_pre: { patch: patch([['trackIn', 2, 1], ['trackIn', 3], ['chainOut', 4], ['fx']], [[0, 3], [1, 3], [3, 2]]), chainOn: on(2, 3, 4) },
  bus_off: { patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), chainOn: on(2, 3) },
  source_off: { patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), chainOn: on(3, 4) },
  bus_of_buses: { patch: patch([['trackIn', 1], ['chainOut', 2], ['fx'], ['chainOut', 3], ['trackIn', 5], ['fx']], [[0, 1], [1, 2], [2, 3], [4, 5], [5, 3], [4, 1]]), chainOn: on(1, 2, 3, 5) },
  // a track with its own Track In: its Chain Out is its own output, not a bus (owner, 25 Oct): 4 and 5 keep their outs and also feed 6
  own_in: { patch: patch([['trackIn', 4], ['chainOut', 4], ['trackIn', 5], ['chainOut', 5], ['chainOut', 6]], [[0, 1], [2, 3], [0, 4], [2, 4], [0, 3]]), chainOn: on(4, 5, 6) },
  send_keeps_out: { patch: patch([['trackIn', 1, 1], ['chainOut', 1], ['chainOut', 9]], [[0, 1], [0, 2]]), chainOn: on(1, 9) },
  // 1 into 4 and 4 into 1 with no Track In on the bus side is not a loop (ownIn); a real loop needs buses without Track Ins: none can be made, so the loop flag stays false
  cross: { patch: patch([['trackIn', 1], ['chainOut', 4], ['trackIn', 4], ['chainOut', 1]], [[0, 1], [2, 3]]), chainOn: on(1, 4) },
  bad_tracks: { patch: patch([['trackIn', 0], ['trackIn', 33], ['chainOut', 2], ['trackIn', 7], ['chainOut', 40], ['chainOut', 32]], [[0, 2], [1, 2], [3, 2], [3, 4], [3, 5]]), chainOn: on(1, 2, 7, 32) },
  // ids that repeat (the later module wins, in the first one's place), numbers and strings as ids, a missing params, a string track
  odd_ids: { patch: { format: 'chains.patch', modules: [{ id: 1, type: 'trackIn', params: { track: '3' } }, { id: '1', type: 'trackIn', params: { track: 5, pre: 1 } }, { id: 2, type: 'chainOut', params: { track: 8.4 } }, null,
      { id: 1, type: 'trackIn', params: { track: 4, pre: '1' } }, { id: 'x', type: 'chainOut' }, { type: 'chainOut', params: { track: 9 } }, { id: 9, type: 'trackIn', params: { track: null } }],
    cables: [{ from: [1, 'o'], to: [2, 'i'] }, { from: ['1', 'o'], to: [2, 'i'] }, null, { from: [9, 'o'], to: ['x', 'i'] }, { from: [9, 'o'], to: [2, 'i'] }, { to: [2] }] }, chainOn: on(1, 3, 4, 5, 8) },
  // two Track Ins of one track that both reach the bus: the JS list holds the track twice (busSync then wires it twice): ported as is
  twice: { patch: patch([['trackIn', 2], ['trackIn', 2, 1], ['chainOut', 5], ['trackIn', 3]], [[0, 2], [1, 2], [3, 2]]), chainOn: on(2, 3, 5) },
  // audible(): mute, solo, a soloed bus keeps its sources, a soloed source keeps its bus
  aud_mute: { patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), chainOn: on(2, 3, 4), mix: { 1: { mute: true }, 6: { mute: true } } },
  aud_solo_bus: { patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), chainOn: on(2, 3, 4), mix: { 3: { solo: true }, 1: { mute: true } } },
  aud_solo_src: { patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), chainOn: on(2, 3, 4), mix: { 2: { solo: true }, 9: { solo: true, mute: true } } },
  many: { patch: patch([...Array.from({ length: 20 }, (_, i) => ['trackIn', i + 1, i % 3 === 0]), ['chainOut', 30], ['chainOut', 31], ['chainOut', 32]],
      [...Array.from({ length: 20 }, (_, i) => [i, 20 + i % 3]), [3, 22], [4, 20]]), chainOn: on(...Array.from({ length: 32 }, (_, i) => i + 1)) },
};
export function busPlanOf(S) {
  const { U } = makeCore(); U.setS(S); const P = U.busPlan();
  const mask = it => { let m = 0; for (const t of it) m = (m | (1 << t)) >>> 0; return m >>> 0; };
  const feeds = new Array(NT).fill(0), targets = new Array(NT).fill(0); for (const [b, s] of P.feeds) feeds[b] = mask(s); for (const [t, s] of P.targets) targets[t] = mask(s);
  let cf = 0, au = 0; for (let t = 0; t < NT; t++) { if (U.chainsFeed(t)) cf = (cf | (1 << t)) >>> 0; if (U.audible(t)) au = (au | (1 << t)) >>> 0; }
  const lists = {}; for (const [b, src] of P.feeds) lists[b] = src.slice();
  return { feeds, lists, targets, pure: mask(P.pure), pre: mask(P.pre), loop: !!P.loop, chainsFeed: cf >>> 0, audible: au >>> 0 };
}
export function busPlanCases() {
  const out = [];
  for (const [id, C] of Object.entries(BUSPLANS)) { const S = songOf({ patch: C.patch, chainOn: C.chainOn, mix: C.mix }); out.push({ id, S: { chainOn: S.chainOn, chainPatch: S.chainPatch, mix: S.mix }, want: busPlanOf(S) }); }
  // the songs in sounds/: as saved, and with a track soloed and one muted (audible reads the plan)
  for (const fn of fs.readdirSync(path.join(ROOT, 'sounds')).filter(x => x.endsWith('.thunder')).sort()) {
    const S = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds', fn), 'utf8')).song;
    out.push({ id: 'file:' + fn, file: 'sounds/' + fn, want: busPlanOf(S) });
    const all = Object.assign({}, S, { chainOn: new Array(NT).fill(true) }); out.push({ id: 'file:' + fn + ':all-on', file: 'sounds/' + fn, allOn: true, want: busPlanOf(all) });
  }
  return out;
}

// ---- main -----------------------------------------------------------------------------------------------------------------------
const M64 = (1n << 64n) - 1n;
export function fnv(u) { let h = 0xcbf29ce484222325n; for (let i = 0; i < u.length; i++) { h ^= BigInt(u[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const fnvF = a => fnv(new Uint8Array(a.buffer, a.byteOffset, a.byteLength));
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const a = process.argv.slice(2);
  if (a[0] === '--busplan') {
    const dir = a[1] || '/tmp/u09/busplan'; fs.mkdirSync(dir, { recursive: true });
    const cs = busPlanCases(); fs.writeFileSync(path.join(dir, 'busplan.json'), JSON.stringify(cs)); console.log('wrote ' + cs.length + ' busPlan cases to ' + dir);
  } else {
    const results = CASE_NAMES.map(n => ({ n, r: runCase(n) }));
    const bp = busPlanCases();
    const inc = ['// GENERATED by native/tools/mix-dump.mjs (--golden). Do not edit: a mismatch means the port is wrong.',
      '// FNV-1a 64 over each mixer case\'s float32 master output (L then R), made by the JS model of the page\'s Web Audio strip',
      '// (validated against Chrome: mix-webaudio-check.txt), and over the JSON of what the JS busPlan gives for every busPlan case.',
      'struct MixGolden { const char* name; unsigned long long hash; int floats; };',
      'static const MixGolden kMixGolden[] = {',
      ...results.map(({ n, r }) => `  {"${n}", ${hex(fnvF(r.total))}, ${r.total.length}},`), '};', `static const int kMixGoldenN = ${results.length};`,
      'static const MixGolden kBusPlanGolden[] = {',
      ...bp.map(c => `  {"${c.id}", ${hex(fnv(Buffer.from(JSON.stringify(c.want))))}, 0},`), '};', `static const int kBusPlanGoldenN = ${bp.length};`, ''].join('\n');
    const incPath = path.join(ROOT, 'native/tests/golden/mix_golden.inc');
    if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
    else if (a[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('mix_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('mix_golden.inc is current'); }
    else {
      const dir = a[0] || '/tmp/u09/mix-ref'; fs.mkdirSync(dir, { recursive: true });
      for (const { n, r } of results) { fs.writeFileSync(path.join(dir, n + '.f32'), Buffer.from(r.total.buffer)); fs.writeFileSync(path.join(dir, n + '.in.f32'), Buffer.from(r.inp.buffer)); fs.writeFileSync(path.join(dir, n + '.json'), JSON.stringify(r.meta)); }
      fs.writeFileSync(path.join(dir, 'cases.txt'), CASE_NAMES.join('\n') + '\n');
      console.log('wrote ' + results.length + ' cases to ' + dir);
    }
  }
}
