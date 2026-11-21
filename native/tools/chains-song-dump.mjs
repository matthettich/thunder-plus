// C1g: THE SONG-LEVEL GOLDEN OF THE CHAINS HOST (exact, Node only). README (the whole of it is this comment; notes: docs/port/C1G-NOTES.md)
//
// WHAT IT IS. For a song fragment (bpm, lpb, ticks, mixer, CHN flags, chains patch), a set of synthetic track inputs and a list of `chc` / `tempo` events, it renders the MASTER OUTPUT of the song's mixer WITH the
// chains on, quantum by quantum (128 frames, 48 kHz), the way the web page's offline render (index.html pass()) wires it, and writes the FNV-1a hash of the output bits. The real chains DSP is the page's own
// `thunder-chains` worklet class (a verbatim copy of beta's chains/chains-worklet.js in native/tests/web-ref, loaded into a Node vm exactly as chains-dump.mjs loads it), one instance per CHN track. The C++
// ChainsHost (unit C1) must produce the same bits through `Mixer` + `ChainsHost` (what it must do: docs/port/C1G-NOTES.md "How C1 reproduces a case").
//
// HOW TO RUN (Node >= 20, no packages; from the repository root):
//   node native/tools/chains-song-dump.mjs --list                  one line per case
//   node native/tools/chains-song-dump.mjs --check                 re-runs every case and compares the whole file with native/tests/golden/chains_song_cases.json (exit 1 on any difference);
//                                                                  also re-checks the negative controls (a variant that must NOT give the golden's hash)
//   node native/tools/chains-song-dump.mjs --check --case f/byp    only the cases whose name contains the text
//   node native/tools/chains-song-dump.mjs --golden                writes native/tests/golden/chains_song_cases.json (run it twice, or three times, and compare: the bytes are identical)
//   node native/tools/chains-song-dump.mjs --dump DIR [--case x]   writes DIR/<case>.f32 (the master output: all of L, then all of R, float32 little endian) for a local sample-by-sample comparison
//   node native/tools/chains-song-dump.mjs --extract-beta SONG     writes native/tests/golden/chains_song_beta_fragment.json from sounds/BETA-TESTING-ELECTRO.thunder of beta (case b reads it; it is committed)
//
// WHAT IS REAL PAGE CODE (run, never retyped):
//   chains/chains-worklet.js  (pinned: native/tests/web-ref/chains-worklet.js) the class ThunderChains registered as 'thunder-chains': its constructor (processorOptions: track, patch, transport, busMap, tapMap),
//                             its message handler (transport), its k-rate b1..b15 parameters and p1(): inputs 0 / 4 / 5, the output 0 return. The engine and every module (Track In, Stereo mixer, Chain Out, VCA, LFO).
//   core/seq-core.js          (pinned) busPlan, chainsFeed, chainsTracks, curve, trackGain, the mixer lanes (mixOut) and the Auto timelines, through mix-dump.mjs's makeCore.
//   index.html                (pinned: native/tests/web-ref/mix-excerpt.js) trimGain, audible, busStemPlan, busClosure: the TEXT of the page's lines, run in the same vm.
//   mix-dump.mjs              the strip model (class Model: trim, pre, fader, the core's cg, the equal-power panner, the master sum): imported and subclassed; its panner / Auto / strip arithmetic is what mix-webaudio-check.mjs checked in Chrome.
// WHAT IS GLUE (this file; each piece names the page lines it copies; "index.html:N" is beta's index.html at native/tests/web-ref/BETA-COMMIT, readable in native/tests/web-ref/chains-wiring-excerpt.js):
//   G1 ChainModel.want       index.html:23495-23500 (applyMix): a render's mix is its `keep` (filter = keep.has(t)), gain 0 and pre 0 for a track that is not kept.
//   G2 ChainModel.plan       index.html:23547 (routeTrack: wantChn = chainsFeed(t) && patch format), 21137-21160 (chainsNode busMap = busPlan().feeds.get(track).slice(0, 32)), 22924-22962 (busSync: want, routed, feed order),
//                            25859-25862 (pass(): nodes only for `keep` tracks), and mix-dump.mjs Model.plan for the order (sources before the buses that take them).
//   G3 ChainModel.block      mix-dump.mjs Model.block (lines 225-250), with its 1:1 stand-in chain replaced by the real node: the strip is the same arithmetic (TRIM -> node -> wet 1 -> pre / fader / cg / pan -> master).
//                            index.html:23523-23529 (chnIn: inp -> node -> wet -> mid; offline dry 0, wet 1 at once: 23513-23520 ramp() sets the value), 22947-22952 (feed pairs: source j on channels 2*(j&15), +1 of merger j>>4, which
//                            goes to input 4 + (j>>4); PRE feeds from `pre`, the others from `p`), 22929 / 22961 (a pure source of a bus that has a node leaves the master).
//   G4 node call             the AudioWorkletNode's process(inputs, outputs, parameters): inputs [track pair, [], [], [], merger 0, merger 1], outputs [pair, 8 sends], parameters b1..b15 as 1-sample arrays (k-rate).
//   G5 processorOptions      index.html:21137-21160 (chainsNode): {track, patch, quality 'full', transport {tempo S.bpm, lpb S.lpb||4, ticks S.ticks||6, playing false (tp.on after tStop)}, busMap, tapMap [], bytes}.
//                            (cid null, irs and pump bytes are left out: no module of any case reads them.)
//   G6 initial transport     index.html:25862 (pass()): after every node is made, {tempo S.bpm, lpb S.lpb||4, ticks S.ticks||6, playing true} is posted to every node, BEFORE the first quantum. loadPatch has already
//                            applied the PATCH's saved transport (chains-worklet.js:5986), so the song's wins (case j).
//   G7 event delivery        TEMPO, index.html:23222-23236 (tempoTo, offline branch): `q = 128 / sr, at = Math.ceil(ev.t / q) * q`; at <= 0 or at >= the render length: sent at once (before quantum 0); else ac.suspend(at): sent before the
//                            quantum that starts at `at`, i.e. quantum ceil(ev.t / q); messages of the same `at` in event order (MEASURED in Chrome 153, chains-krate-check.mjs: agrees for every k). CHC, index.html:23272: setValueAtTime(ev.v, max(ev.t, 0)) on the node's k-rate parameter b<k>;
//                            Chrome's measured rule (chains-krate-check.mjs, 2400 measurements per rate, 48000 and 44100 Hz, no exception) is quantum = ceil(ceil(t * sr) / 128), t * sr in double; NOT the page's ceil(t / q) (that differs when t is exactly a quantum start, e.g. k = 5 at 48 kHz).
//   G8 keep rule             `noteTracks` (explicit in every case and in `expect`) is the page's `notes.some(e => e.track === t)`: a track with no notes is not kept, so it gets no node and no sound from a patch alone (case l). index.html:25909-25913 (renderOut: keepT = audible tracks that have notes, busStemPlan adds buses) and 25946-25952 (a stem: Set([t])); pass() then busClosure (25829).
//   G9 the inputs            the synthetic generator below (not the page's voices).
// WHAT IT DOES NOT PROVE: it is not Chrome. (1) The event timing is MEASURED (chc and tempo, see G7) but only with a tiny worklet, not the page's whole graph; port-message delivery of the initial transport (G6) is not measured. (2) The mixer strip is the model of mix-dump.mjs (Chrome-checked there for the chains-off strip; with a node in it the node's output is taken as exact and gain 1 / pre / fader are the model's).
// (3) The sum of the wet path and the dry path (dry gain 0) is taken as the wet path alone (a signed zero of -0 + +0 could differ). (4) No track effects, no master effects, no mastering, no limiter, no PDC: the output is the master
// strip's sum x MASTER x OUT, which is what the C++ `Mixer` renders without them. (5) Its inputs are synthetic. (6) Taps, sends, sidechains, Resample, quality shedding and patch edits are not exercised.
//
// THE INPUT GENERATOR (also in the golden's header; this is what C++ must reproduce bit for bit). For track t (0-based) and sample index i (from 0 at the first sample of the render), per channel c in {L, R}:
//   state:   a 32-bit xorshift per channel, seeded  sL = ((uint32)(t + 1) * 2654435761u) ^ 0x2545F491u,  sR = sL ^ 0xA5A5A5A5u  (a seed of 0 becomes 1; neither can be 0 for t < 32).
//   per sample, in this order: advance BOTH states (even for a mono track):  s ^= s << 13; s ^= s >> 17; s ^= s << 5  (uint32 arithmetic);  uL = sL / 4294967296.0,  uR = sR / 4294967296.0 (doubles).
//   phase:   inc = 25769803 + 3231531 * t (uint32);  ph = (uint32)(i * inc) (mod 2^32);  phR = (uint32)(ph + 0x2AAAAAAB).
//   wave(p): x = (double)p / 4294967296.0;  w = x < 0.5 ? x : x - 1.0;  a = w < 0 ? -w : w;  y = 16.0 * w * (0.5 - a)      (a parabolic sine: no libm; evaluate as (16.0 * w) * (0.5 - a)).
//   burst:   b = (((i >> 11) + t) & 3) == 0   (2048 samples of noise out of every 8192, a different quarter per track).
//   sample:  X = (double)( 0.25 * wave(p) + (b ? 0.2 * (2.0 * u - 1.0) : 0.0) )   evaluated in IEEE double, left to right, then ONE conversion to float32 (Math.fround / (float)); the addend 0.0 is always added.
//            L uses (ph, uL), R uses (phR, uR). A MONO track has only L: the mixer reads it as a one-channel input (R = L through the strip; into a chains node the page's input 0 up-mixes it to L = R = the strip's TRIM output).
//   C++ must compile this with no fused multiply-add and no fast-math (-ffp-contract=off) so that the double operations are the JS ones.
// THE HASH (as chains_cases.json): FNV-1a 32 (offset 0x811c9dc5, prime 0x01000193) over the bytes of the float32 samples, little endian; here per quantum q = 0..quanta-1: the 128 left samples of the quantum, then the 128 right
// samples of the quantum; hex, 8 digits. `qh` is the same hash of each quantum alone (8 digits each, concatenated): the first quantum that differs says where to look. `head` is the first 16 samples of each channel as 8-digit hex of the float bits.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { Model, NT, SR, useSources, makeCore } from './mix-dump.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
const H = require(path.join(ROOT, 'tests/helpers.js'));
const REF = path.join(ROOT, 'native/tests/web-ref');
const GOLD = path.join(ROOT, 'native/tests/golden/chains_song_cases.json');
const BETA_FRAG = path.join(ROOT, 'native/tests/golden/chains_song_beta_fragment.json');
const WORKLET = 'native/tests/web-ref/chains-worklet.js';
const BYTES = new Uint8Array(fs.readFileSync(path.join(ROOT, 'chains/dsp/tfx.wasm')));
const PUMP = new Uint8Array(fs.readFileSync(path.join(ROOT, 'pump/pump.wasm')));
const N = 128, QUANTA = 400, f = Math.fround;
const Q = 128 / SR;   // the page's q (index.html:23229)

// ---- the pinned web sources the model runs on (mix-dump.mjs's makeCore reads them through useSources) ----
const betaCommit = fs.readFileSync(path.join(REF, 'BETA-COMMIT'), 'utf8').trim();
useSources(fs.readFileSync(path.join(REF, 'seq-core.js'), 'utf8').replace(/\r\n/g, '\n'), fs.readFileSync(path.join(REF, 'mix-excerpt.js'), 'utf8').replace(/\r\n/g, '\n'));

// ---- the chains scope: exactly chains-dump.mjs's scope(), on the pinned worklet ----
function chainsScope() { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, WORKLET, ['soft']); ctx.ChainsDSP.wasmInit({ tfxBytes: BYTES }); ctx.__pumpBytes = PUMP; return ctx; }

// ---- the inputs (G9) ----
const xs = s => { s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; return s >>> 0; };
const wave = p => { const x = p / 4294967296, w = x < 0.5 ? x : x - 1, a = w < 0 ? -w : w; return 16 * w * (0.5 - a); };
export function makeInput(t, mono, n) {
  const L = new Float32Array(n), R = mono ? null : new Float32Array(n), inc = (25769803 + 3231531 * t) >>> 0;
  let sL = ((Math.imul(t + 1, 2654435761) ^ 0x2545F491) >>> 0) || 1, sR = ((sL ^ 0xA5A5A5A5) >>> 0) || 1;
  for (let i = 0; i < n; i++) {
    sL = xs(sL); sR = xs(sR);
    const ph = Math.imul(i, inc) >>> 0, phR = (ph + 0x2AAAAAAB) >>> 0, burst = (((i >>> 11) + t) & 3) === 0;
    L[i] = f(0.25 * wave(ph) + (burst ? 0.2 * (2 * (sL / 4294967296) - 1) : 0));
    if (R) R[i] = f(0.25 * wave(phR) + (burst ? 0.2 * (2 * (sR / 4294967296) - 1) : 0));
  }
  return [L, R || L];
}

// ---- the song fragment ----
const MIXK = ['vol', 'trim', 'pan', 'mute', 'solo'];
export function fragmentOf(song) {
  const mix = Array.from({ length: NT }, (_, t) => { const m = (song.mix || [])[t] || {}; return { vol: m.vol == null ? 224 : m.vol, trim: m.trim == null ? 128 : m.trim, pan: m.pan == null ? 128 : m.pan, mute: !!m.mute, solo: !!m.solo }; });
  const chainOn = Array.from({ length: NT }, (_, t) => !!(song.chainOn || [])[t]);
  return { bpm: song.bpm, lpb: song.lpb || 4, ticks: song.ticks || 6, master: song.master == null ? 255 : song.master, mout: song.mout == null ? 255 : song.mout, mix, chainOn, chainPatch: JSON.parse(JSON.stringify(song.chainPatch)) };
}
const clone = x => JSON.parse(JSON.stringify(x));
const electroFrag = () => fragmentOf(JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')).song);
const betaFrag = () => { if (!fs.existsSync(BETA_FRAG)) throw new Error(BETA_FRAG + ' is missing: node native/tools/chains-song-dump.mjs --extract-beta <beta sounds/BETA-TESTING-ELECTRO.thunder>'); return JSON.parse(fs.readFileSync(BETA_FRAG, 'utf8')).fragment; };

// ---- patch helpers (the page's own patch format, as the editor saves it: stereo cables, st2 / ti2) ----
const mod = (id, type, params) => ({ id, type, params });
const TI = (id, track, pre) => mod(id, 'trackIn', { track, pre: pre ? 1 : 0 }), CO = (id, track, level) => mod(id, 'chainOut', { track, level: level == null ? 1 : level });
const SMX = (id, levels, pans) => mod(id, 'smixer', Object.assign({ chans: levels.length, level: 1 }, ...levels.map((l, i) => ({ ['l' + (i + 1)]: l, ['p' + (i + 1)]: (pans || [])[i] || 0 }))));
const cab = (a, pa, b, pb) => [{ from: [a, pa], to: [b, pb] }, { from: [a, pa + 'R'], to: [b, pb + 'R'] }];   // a stereo pair of cables
const cabs = (...l) => l.flat();
const PATCH = (modules, cables) => ({ format: 'chains.patch', version: 1, transport: { tempo: 120, lpb: 4, ticks: 6, swing: 0 }, modules, cables, st2: 1, ti2: 1 });
const outCab = (a, b, pb) => [{ from: [a, 'out'], to: [b, pb] }, { from: [a, 'outR'], to: [b, pb + 'R'] }];   // out / outR -> pb / pbR
// "out"-port names of a Track In are out / outR; a mixer's are out / outR; a Chain Out's inputs are in / inR
const wire = (a, b, pb) => outCab(a, b, pb || 'in');

// ---- the cases ----
const cases = [];
const addCase = c => cases.push(Object.assign({ stem: null, events: [], quanta: QUANTA, controls: [], noteTracks: c.inputs.map(q => q.t).sort((a, b) => a - b) }, c));   // noteTracks: the tracks that HAVE NOTES (index.html:25909); by default the tracks that have an input
const st = t => ({ t, mono: false }), mo = t => ({ t, mono: true });
const frameT = frames => frames / SR;   // a time in seconds from a frame count: the way Chrome reads currentTime
// ELECTRO's own patch: tracks 1, 2, 3, 7 -> Stereo mixer -> Chain Out of track 4 (bus 3); sources 0, 1, 2, 6 are pure; the bus track 3 has CHN on and a Chain Out
const ELECTRO_IN = [st(0), st(1), st(2), st(3), st(4), st(6), st(7)];
addCase({ name: 'a/electro', note: "ELECTRO's own chains patch and bus plan: Track In 1, 2, 3, 7 into a Stereo mixer into Chain Out 4; tracks 4 (bus, own sound ignored: it has no Track In), 5 and 8 play straight to the master", song: electroFrag(), inputs: ELECTRO_IN });
addCase({ name: 'b/beta-testing-electro', note: "BETA-TESTING-ELECTRO's patch: Track In 1-6 and 9 into a Stereo mixer (7 inputs) into Chain Out 8; CHN on 1-6, 8, 9; track 7 plays straight", song: () => betaFrag(), inputs: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9].map(st) });
// bus into bus: tracks 1, 2 -> mixer A -> Chain Out 4 (bus 3); track 4's OWN Track In (it reads its own output after chain, fader and pan) and track 3 (PRE send) -> mixer B -> Chain Out 6 (bus 5)
addCase({ name: 'c/bus-into-bus', note: 'bus 3 (fed by 0, 1) feeds bus 5 (also fed by 2 with a PRE send): two nodes, the middle bus processed (fader, pan) before it is heard in bus 5; levels and pans differ on every input',
  song: () => { const s = electroFrag(); s.chainOn = s.chainOn.map(() => false); [0, 1, 2, 3, 5].forEach(t => { s.chainOn[t] = true; });
    s.chainPatch = PATCH([TI('i1', 1), TI('i2', 2), SMX('mA', [0.8, 0.6], [-0.4, 0.5]), CO('o4', 4), TI('i4', 4), TI('i3', 3, true), SMX('mB', [0.7, 0.9], [0.2, -0.3]), CO('o6', 6, 0.9)],
      cabs(wire('i1', 'mA', 'in1'), wire('i2', 'mA', 'in2'), wire('mA', 'o4'), wire('i4', 'mB', 'in1'), wire('i3', 'mB', 'in2'), wire('mB', 'o6')));
    s.mix[0] = { vol: 200, trim: 150, pan: 70, mute: false, solo: false }; s.mix[1] = { vol: 190, trim: 128, pan: 200, mute: false, solo: false }; s.mix[2] = { vol: 210, trim: 110, pan: 160, mute: false, solo: false };
    s.mix[3] = { vol: 180, trim: 128, pan: 90, mute: false, solo: false }; s.mix[5] = { vol: 230, trim: 128, pan: 128, mute: false, solo: false }; return s; }, inputs: [st(0), st(1), st(2), st(3), st(4), st(5)] });
addCase({ name: 'd/mono-into-bus', note: 'two mono sources (panned off centre) into the bus, a mono track playing straight (the mono panner law) and a stereo source; the node hears a mono source as L = R', song: () => { const s = electroFrag(); s.mix[0].pan = 40; s.mix[1].pan = 230; s.mix[4].pan = 70; return s; },
  inputs: [mo(0), mo(1), st(2), mo(4), st(6)] });
// mute and solo (the offline render's keep rule: only heard tracks have a node; a bus that is not heard has none, so its sources play straight to the master)
const withMix = (k, set) => () => { const s = electroFrag(); for (const [t, p] of Object.entries(set)) Object.assign(s.mix[t], p); s.mix[0].pan = 60; s.mix[1].pan = 190; return s; };
addCase({ name: 'e/mute-source', note: 'track 2 (a source) muted: not kept, silent in the bus', song: withMix('m', { 1: { mute: true } }), inputs: ELECTRO_IN });
addCase({ name: 'e/solo-source', note: 'track 3 (a source) soloed: it and the bus (busAll) are heard, the other sources and the plain tracks are not', song: withMix('s', { 2: { solo: true } }), inputs: ELECTRO_IN });
addCase({ name: 'e/mute-bus', note: 'the bus track 4 muted: it is not kept, so it gets no node and its pure sources are NOT routed: they play straight to the master', song: withMix('m', { 3: { mute: true } }), inputs: ELECTRO_IN });
addCase({ name: 'e/solo-bus', note: 'the bus track 4 soloed: it and all its sources are heard (srcAll), nothing else', song: withMix('s', { 3: { solo: true } }), inputs: ELECTRO_IN });
// chc BYP: ELECTRO's patch with a VCA (module number 1) between the mixer and Chain Out; the tracker's BYP b1 = 1 (bypassed) then -1 (the patch's own)
const bypPatch = () => { const s = electroFrag(), p = s.chainPatch; p.modules = p.modules.concat([mod('byp1', 'vca', { gain: 0.5, _bn: 1 })]);
  p.cables = p.cables.filter(c => !(c.from[0] === 'mmvohebij95' && c.to[0] === 'toumvohdvso9da')).concat(wire('mmvohebij95', 'byp1'), wire('byp1', 'toumvohdvso9da')); return s; };
const chc = (t, v, p = 'b1') => ({ type: 'chc', t, track: 3, p, v });
const K = 150;   // the quantum under test: t = K * 128 / 48000 = 0.4 s exactly (ceil(t / q) = 150 in double arithmetic: checked at run time)
addCase({ name: 'f/byp-mid-quantum', note: 'BYP b1 = 1 at frame K*128 + 40 (inside quantum K): the page takes it from quantum K+1; b1 = -1 at frame 300*128 + 100 (from quantum 301)', song: bypPatch,
  inputs: ELECTRO_IN, events: [chc(frameT(K * 128 + 40), 1), chc(frameT(300 * 128 + 100), -1)] });
addCase({ name: 'f/byp-on-quantum-start', note: 'BYP b1 = 1 at EXACTLY the first frame of quantum K (t = K*128/48000): ceil(t / q) = K, so it is seen in quantum K itself (t == t0 lands in the SAME block)', song: bypPatch,
  inputs: ELECTRO_IN, events: [chc(frameT(K * 128), 1), chc(frameT(300 * 128), -1)] });
addCase({ name: 'f/byp-one-frame-before', note: 'BYP b1 = 1 one frame before quantum K (frame K*128 - 1): ceil gives K, quantum K', song: bypPatch, inputs: ELECTRO_IN, events: [chc(frameT(K * 128 - 1), 1), chc(frameT(300 * 128 - 1), -1)] });
addCase({ name: 'f/byp-one-frame-after', note: 'BYP b1 = 1 one frame after the start of quantum K (frame K*128 + 1): ceil gives K+1', song: bypPatch, inputs: ELECTRO_IN, events: [chc(frameT(K * 128 + 1), 1), chc(frameT(300 * 128 + 1), -1)] });
// the double-arithmetic trap: a time that IS a quantum start (k * 128 / 48000) but whose ceil(t / q) is k + 1 (0.18 of all k): the page's rule puts it in the NEXT quantum
const KF = (() => { for (let k = 180; ; k++) if (Math.ceil((k * 128 / SR) / Q) !== k) return k; })();
addCase({ name: 'f/byp-fuzz-quantum-start', note: `BYP b1 = 1 at t = ${KF}*128/48000, a quantum start whose ceil(t / q) is ${KF + 1} in double arithmetic (ceil(t / q) gives the NEXT quantum, and Chrome's rule ceil(ceil(t * sr) / 128) gives it too for this k): C++ must use the chc rule of the header`,
  song: bypPatch, inputs: ELECTRO_IN, events: [chc(frameT(KF * 128), 1), chc(frameT(300 * 128), -1)] });
addCase({ name: 'f/byp-chrome-rule-k5', note: 'BYP b1 = 1 at EXACTLY 5*128/48000: Chrome (measured, chains-krate-check.mjs) sees it in quantum 5; the old expression ceil(t / q) gives 6 (the control)', song: bypPatch, inputs: ELECTRO_IN,
  events: [chc(frameT(5 * 128), 1), chc(frameT(300 * 128), -1)], controls: ['oldChcRule'] });
// tempo: ELECTRO's patch with a tempo-synced LFO (triangle, 8 per beat-quarter) on a VCA after the mixer
const lfoPatch = () => { const s = electroFrag(), p = s.chainPatch; p.modules = p.modules.concat([mod('lfo1', 'lfo', { rate: 8, sync: 1, shape: 3, depth: 0.8 }), mod('vca1', 'vca', { gain: 1 })]);
  p.cables = p.cables.filter(c => !(c.from[0] === 'mmvohebij95' && c.to[0] === 'toumvohdvso9da')).concat(wire('mmvohebij95', 'vca1'), wire('vca1', 'toumvohdvso9da'), [{ from: ['lfo1', 'out'], to: ['vca1', 'cv'] }]); return s; };
const tempo = (t, bpm, tks = 6) => ({ type: 'tempo', t, bpm, tks });
addCase({ name: 'g/tempo-change', note: 'a tempo-synced LFO in the chain; tempo 131 -> 97 at frame 120*128 + 77 (mid-quantum: from quantum 121) -> 140 at exactly frame 250*128 (a quantum start: quantum 250)', song: lfoPatch, inputs: ELECTRO_IN,
  events: [tempo(frameT(120 * 128 + 77), 97), tempo(frameT(250 * 128), 140, 8)] });
addCase({ name: 'h/stem-bus', note: 'the stem of the bus track 4: keep = {3} + its audible sources (busClosure): the node is made, the sources feed it; the plain tracks are not heard', song: electroFrag, inputs: ELECTRO_IN, stem: 3 });
addCase({ name: 'h/stem-plain-track', note: 'the stem of the plain track 5 (no chains): no node at all, only track 5 reaches the master', song: electroFrag, inputs: ELECTRO_IN, stem: 4 });
addCase({ name: 'h/stem-source-only', note: 'a stem of the source track 1 alone (the page never asks: a track in its bus\'s stem has none; the rule is pinned anyway): the bus is not kept, so no node, and the source plays straight', song: electroFrag, inputs: ELECTRO_IN, stem: 0 });
addCase({ name: 'i/chain-off-bus-track', note: 'CHN off on the bus track 4: no node, no bus, the sources play straight to the master', song: () => { const s = electroFrag(); s.chainOn[3] = false; return s; }, inputs: ELECTRO_IN });
addCase({ name: 'i/no-chain-out', note: 'CHN on for track 4 but the patch has no Chain Out for it (chainsFeed false): no node, no bus', song: () => { const s = electroFrag(), p = s.chainPatch; p.modules = p.modules.filter(m => m.type !== 'chainOut'); p.cables = p.cables.filter(c => c.to[0] !== 'toumvohdvso9da'); return s; }, inputs: ELECTRO_IN });
addCase({ name: 'j/patch-transport-vs-song', note: "ELECTRO's patch (saved transport tempo 120, lpb 4) in the song at 131 / lpb 8, with a tempo-synced LFO: the song's transport must win (the control: without the page's initial transport message the hash differs)", song: lfoPatch, inputs: ELECTRO_IN, controls: ['noInitialTransport'] });
addCase({ name: 'k/twenty-sources', note: 'twenty sources into one bus (two Stereo mixers into one Chain Out): feed pairs 0..15 on input 4, 16..19 on input 5 (the second merger)',
  song: () => { const s = electroFrag(); s.chainOn = s.chainOn.map(() => false); for (let t = 0; t < 20; t++) s.chainOn[t] = true; s.chainOn[22] = true;
    const mods = [], cs = []; for (let t = 0; t < 20; t++) mods.push(TI('t' + t, t + 1, t % 5 === 4));
    mods.push(SMX('mx1', Array.from({ length: 16 }, (_, i) => 0.3 + 0.02 * i), Array.from({ length: 16 }, (_, i) => ((i % 5) - 2) * 0.2)), SMX('mx2', [0.5, 0.45, 0.4, 0.35]), CO('co', 23));
    for (let t = 0; t < 16; t++) cs.push(wire('t' + t, 'mx1', 'in' + (t + 1))); for (let t = 16; t < 20; t++) cs.push(wire('t' + t, 'mx2', 'in' + (t - 15)));
    cs.push(wire('mx1', 'co'), wire('mx2', 'co')); s.chainPatch = PATCH(mods, cabs(...cs)); for (let t = 0; t < 20; t++) { s.mix[t] = { vol: 150 + 3 * t, trim: 128, pan: 40 + 9 * t, mute: false, solo: false }; } return s; },
  inputs: Array.from({ length: 20 }, (_, t) => (t % 7 === 3 ? mo(t) : st(t))).concat([st(22), st(25)]) });

addCase({ name: 'l/chain-out-no-notes', note: 'track 9 has CHN on, a Chain Out and an oscillator patched straight into it, but NO notes: the page keeps only tracks with notes (and the bus closure), so it gets no node and is SILENT; track 5 (with notes) plays. Control treatAsNotes: with track 9 treated as having notes the oscillator is heard',
  song: () => { const s = electroFrag(); s.chainOn = s.chainOn.map(() => false); s.chainOn[8] = true; s.chainPatch = PATCH([mod('osc1', 'osc', { wave: 1, freq: 330, fm: 0, level: 0.5 }), CO('co9', 9)], [{ from: ['osc1', 'out'], to: ['co9', 'in'] }, { from: ['osc1', 'out'], to: ['co9', 'inR'] }]); return s; },
  inputs: [st(4)], noteTracks: [4], controls: ['treatAsNotes'], controlNote: 8 });

// ---- the library-effect rule: a case may not use one of the module types chains_cases.json leaves out as "within a recorded dB" ----
const LIB = (() => { const c = JSON.parse(fs.readFileSync(path.join(ROOT, 'native/tests/golden/chains_cases.json'), 'utf8')); return new Set(c.filter(x => x.kind === 'mod' && x.lib).map(x => x.type)); })();

// ---- the model with the real node in it (G1, G2, G3) ----
class ChainModel extends Model {
  want(t) {   // G1: index.html:23495-23500, filter = keep.has(t)
    if (t >= NT) return { gv: 1, pv: 0, tg: 1, prv: 1 };
    const U = this.U, m = this.S.mix[t], on = this.S.__keep.has(t);
    return { gv: on ? U.trackGain(m.vol) : 0, pv: Math.max(-1, Math.min(1, (m.pan - 128) / 127)), tg: U.trimGain(m.trim == null ? 128 : m.trim), prv: on ? 1 : 0 };
  }
  plan() {   // G2
    const U = this.U, P = U.busPlan(), keep = this.S.__keep, okPatch = !!(this.S.chainPatch && this.S.chainPatch.format === 'chains.patch');
    this.chn = new Set(); this.nodeOrder = []; for (const t of keep) if (t < NT && U.chainsFeed(t) && okPatch) { this.chn.add(t); this.nodeOrder.push(t); }
    this.feeds = new Map(); this.routed = new Set(); this.busMaps = {}; this.preSet = [];
    for (const [bt, src] of P.feeds) if (this.chn.has(bt)) { this.feeds.set(bt, src.slice(0, 32).map(t => ({ t, pre: P.pre.has(t) }))); this.busMaps[bt] = src.slice(0, 32); for (const t of src) { if (P.pure.has(t)) this.routed.add(t); if (P.pre.has(t) && !this.preSet.includes(t)) this.preSet.push(t); } }
    const order = [], seen = new Set(), go = t => { if (seen.has(t)) return; seen.add(t); for (const s of this.feeds.get(t) || []) go(s.t); order.push(t); };
    for (let t = 0; t <= NT; t++) go(t); this.order = order;
  }
  attach(nodes) { this.nodes = nodes; }
  block(cur, oL, oR) {   // G3: mix-dump.mjs Model.block, the chain's stand-in replaced by the node
    const n = this.N; this.ctx.currentTime = cur;
    this.proc.mixOut(this.outs, n, 1 / SR);
    this.fill(this.cOut, this.out, cur); this.fill(this.cMo, this.mo, cur);
    const o = [0, 0];
    for (const t of this.order) {
      const T = this.tr[t], c = T.c; this.fill(c.inp, T.inp, cur); this.fill(c.g, T.g, cur); this.fill(c.pre, T.pre, cur); this.fill(c.pan, T.pan, cur);
      const iL = this.inL[t], iR = this.inR[t], hasChn = this.chn.has(t), mono = T.mono && !hasChn, fd = this.feeds.get(t) || [];
      const G = t < NT ? this.G[t] : null, Pn = t < NT ? this.P[t] : null;
      let nL = null, nR = null;
      if (hasChn) {   // G4
        const nd = this.nodes.get(t);
        for (let i = 0; i < n; i++) { nd.in0[0][i] = f(iL[i] * c.inp[i]); nd.in0[1][i] = f((T.mono ? iL[i] : iR[i]) * c.inp[i]); }   // inp (TRIM) -> input 0 (a mono track up-mixed)
        fd.forEach((s, j) => { const q = this.tr[s.t], m = nd.mergers[j >> 4], ch = (j & 15) * 2; m[ch].set(s.pre ? q.L : q.pL); m[ch + 1].set(s.pre ? q.R : q.pR); });   // the sources' pairs (after pan, or PRE)
        nd.node.process(nd.ins, nd.outs, nd.params); nL = nd.outs[0][0]; nR = nd.outs[0][1];   // wet gain 1, dry 0
      }
      for (let i = 0; i < n; i++) {
        let l, r;
        if (hasChn) { l = nL[i]; r = nR[i]; } else { l = f(iL[i] * c.inp[i]); r = mono ? l : f((T.mono ? iL[i] : iR[i]) * c.inp[i]); }   // inp (TRIM)
        T.L[i] = f(l * c.pre[i]); T.R[i] = f(r * c.pre[i]);                                                 // pre
        l = f(l * c.g[i]); r = f(r * c.g[i]);                                                               // g
        if (G) { l = f(l * G[i]); r = f(r * G[i]); }                                                        // cg
        const pan = Math.max(-1, Math.min(1, Pn ? f(c.pan[i] + Pn[i]) : c.pan[i]));                         // p
        if (mono) { const a = (pan * 0.5 + 0.5) * (Math.PI / 2); o[0] = f(l * Math.cos(a)); o[1] = f(l * Math.sin(a)); }
        else if (pan <= 0) { const a = (pan + 1) * (Math.PI / 2); o[0] = f(l + r * Math.cos(a)); o[1] = f(r * Math.sin(a)); }
        else { const a = pan * (Math.PI / 2); o[0] = f(l * Math.cos(a)); o[1] = f(r + l * Math.sin(a)); }
        T.pL[i] = o[0]; T.pR[i] = o[1];
      }
    }
    for (let i = 0; i < n; i++) {
      let l = 0, r = 0; for (let t = 0; t <= NT; t++) if (!this.routed.has(t)) { l = f(l + this.tr[t].pL[i]); r = f(r + this.tr[t].pR[i]); }
      const gm = f(this.cOut[i] + this.M[i]), go = f(this.cMo[i] + this.O[i]);
      oL[i] = f(f(l * gm) * go); oR[i] = f(f(r * gm) * go);
    }
  }
}

// ---- one case ----
const fnv = (h, a) => { const u = new Uint8Array(a.buffer, a.byteOffset, a.byteLength); for (let i = 0; i < u.length; i++) { h ^= u[i]; h = Math.imul(h, 0x01000193) >>> 0; } return h; };
const hex8 = h => (h >>> 0).toString(16).padStart(8, '0');
const bits = x => { const a = new Float32Array([x]); return hex8(new Uint32Array(a.buffer)[0]); };
// the quantum an event is seen from (G7): chc: ceil(ceil(t * sr) / 128) (Chrome, measured); tempo: ceil(t / q) (the page's, measured); t <= 0 -> 0. The tempo message additionally goes at once when at = ceil * q is not inside (0, length) (index.html:23229-23231).
const blockOf = (ev, total, oldChc) => { const k = ev.type === 'chc' && !oldChc ? Math.ceil(Math.ceil(Math.max(ev.t, 0) * SR) / 128) : Math.ceil(Math.max(ev.t, 0) / Q); if (ev.type === 'tempo') { const at = Math.ceil(ev.t / Q) * Q; if (!(at > 0 && at < total * N / SR)) return 0; } return k; };
function run(c, opt = {}) {
  const S0 = typeof c.song === 'function' ? c.song() : c.song, S = Object.assign(clone(S0), { insts: [] });
  const total = c.quanta, n = total * N;
  // which types the patch uses (the library rule)
  for (const m of S.chainPatch.modules) if (LIB.has(m.type)) throw new Error(`${c.name}: module type ${m.type} is one of the library rows chains_cases.json leaves out as "within a recorded dB": not allowed in an exact case`);
  // G8: the keep rule
  const core = makeCore(); core.U.setS(S);
  let keep;
  if (c.stem != null) keep = new Set([c.stem]);
  else { const has = new Set(c.noteTracks.concat(opt.treatAsNotes != null ? [opt.treatAsNotes] : [])), keepT = [];   // G8: the page's `notes.some(e => e.track === t)`
    for (let t = 0; t < NT; t++) if (core.U.audible(t) && has.has(t)) keepT.push(t);
    core.ctx.busStemPlan(keepT, core.U.busPlan(), core.U.audible); keep = new Set(keepT); }
  keep = core.ctx.busClosure(keep);
  S.__keep = keep;
  const M = new ChainModel(S, N); c.inputs.forEach(q => { M.tr[q.t].mono = !!q.mono; });
  const ins = c.inputs.map(q => makeInput(q.t, q.mono, n));
  // the nodes (G5, G6)
  const ctx = chainsScope(), Cls = ctx.__registered['thunder-chains'], nodes = new Map();
  for (const t of M.nodeOrder) {
    const node = new Cls({ processorOptions: { cid: null, track: t, patch: clone(S.chainPatch), quality: 'full', transport: { tempo: S.bpm || 120, lpb: S.lpb || 4, ticks: S.ticks || 6, playing: false }, busMap: (M.busMaps[t] || []).slice(), tapMap: [], bytes: BYTES } });
    const nm = Math.ceil((M.busMaps[t] || []).length / 16), params = {}; for (let k = 1; k <= 15; k++) params['b' + k] = new Float32Array([-1]);
    const mergers = Array.from({ length: nm }, () => Array.from({ length: 32 }, () => new Float32Array(N)));
    const in0 = [new Float32Array(N), new Float32Array(N)];
    nodes.set(t, { node, params, mergers, in0, ins: [in0, [], [], [], mergers[0] || [], mergers[1] || []], outs: [[new Float32Array(N), new Float32Array(N)], Array.from({ length: 8 }, () => new Float32Array(N))] });
  }
  const send = m => { for (const t of M.nodeOrder) nodes.get(t).node.port.onmessage({ data: m }); };
  if (!opt.noInitialTransport) send({ target: 'chains-host', op: 'transport', value: { tempo: S.bpm, lpb: S.lpb || 4, ticks: S.ticks || 6, playing: true } });   // G6
  M.attach(nodes);
  // G7: the events, by quantum
  const due = Array.from({ length: total + 2 }, () => []); const evBlocks = [];
  c.events.forEach((ev, i) => { let b = blockOf(ev, total, opt.oldChcRule); evBlocks.push(b); b = Math.max(0, Math.min(total, b + (opt.shift || 0))); due[b].push(ev); });
  const oL = new Float32Array(N), oR = new Float32Array(N), L = new Float32Array(n), R = new Float32Array(n), qh = []; let h = 0x811c9dc5 >>> 0;
  for (let b = 0; b < total; b++) {
    for (const ev of due[b]) {
      if (ev.type === 'tempo') send({ target: 'chains-host', op: 'transport', value: { tempo: ev.bpm, ticks: ev.tks } });
      else if (ev.type === 'chc') { const nd = nodes.get(ev.track), q = nd && nd.params[ev.p]; if (q) q[0] = ev.v; }
    }
    ctx.currentTime = b * N / SR; ctx.currentFrame = b * N;
    c.inputs.forEach((q, k) => { M.inL[q.t].set(ins[k][0].subarray(b * N, b * N + N)); M.inR[q.t].set(ins[k][1].subarray(b * N, b * N + N)); });
    M.block(b * N / SR, oL, oR);
    L.set(oL, b * N); R.set(oR, b * N);
    let hq = 0x811c9dc5 >>> 0; hq = fnv(hq, oL); hq = fnv(hq, oR); qh.push(hex8(hq)); h = fnv(h, oL); h = fnv(h, oR);
  }
  return { hash: hex8(h), qh: qh.join(''), head: { L: Array.from(L.subarray(0, 16), bits), R: Array.from(R.subarray(0, 16), bits) }, L, R,
    expect: { noteTracks: c.noteTracks.slice(), heard: [...keep].sort((a, b) => a - b), nodes: M.nodeOrder.slice(), busMaps: M.busMaps, pre: M.preSet.slice().sort((a, b) => a - b), routed: [...M.routed].sort((a, b) => a - b), eventQuanta: evBlocks } };
}

// ---- the golden file ----
const HEADER = {
  format: 'chains.song.golden', version: 1, tool: 'native/tools/chains-song-dump.mjs', sampleRate: SR, quantum: N, defaultQuanta: QUANTA, webRef: betaCommit,
  hash: 'FNV-1a 32 (offset 0x811c9dc5, prime 0x01000193) over the little-endian float32 bytes of the MASTER output: per quantum the 128 left samples, then the 128 right samples; 8 hex digits. qh: the same per quantum, concatenated. head: first 16 samples per channel, float bits as hex.',
  output: 'the mixer strips summed (pure sources of a bus that has a node left out) x MASTER x OUT; no track effects, master effects, mastering, limiter or PDC. Only heard tracks have a chains node.',
  inputs: [
    'track t (0-based), sample index i from 0, channel L / R; all arithmetic IEEE double, ONE conversion to float32 at the end; no fused multiply-add, no fast-math',
    'seeds: sL = ((uint32)(t + 1) * 2654435761u) ^ 0x2545F491u; sR = sL ^ 0xA5A5A5A5u; (0 -> 1). Per sample first advance BOTH: s ^= s << 13; s ^= s >> 17; s ^= s << 5 (uint32). uL = sL / 4294967296.0, uR = sR / 4294967296.0',
    'inc = 25769803 + 3231531 * t; ph = (uint32)(i * inc); phR = (uint32)(ph + 0x2AAAAAAB); wave(p): x = p / 4294967296.0; w = x < 0.5 ? x : x - 1.0; a = w < 0 ? -w : w; y = (16.0 * w) * (0.5 - a)',
    'burst = (((i >> 11) + t) & 3) == 0;  X = 0.25 * wave(p) + (burst ? 0.2 * (2.0 * u - 1.0) : 0.0)  (the addend 0.0 is always added); L uses (ph, uL), R uses (phR, uR); a MONO track has only L',
  ],
  song: 'each case embeds the song fragment it plays: bpm, lpb, ticks, master, mout, mix[32] {vol, trim, pan, mute, solo}, chainOn[32], chainPatch (as saved). The keep rule: heard = audible tracks that have an input, plus buses busStemPlan adds (index.html:25816-25820), then busClosure (25823-25827); a stem: heard = {stem} then busClosure.',
  nodes: 'one thunder-chains node per heard track with chainsFeed(t) (CHN on and the patch has a Chain Out for it), made in the order of `expect.nodes`: processorOptions {track, patch, quality full, transport {tempo bpm, lpb, ticks, playing false}, busMap = the plan feeds of that track (<= 32, ascending), tapMap []}; then every node gets the transport message {tempo bpm, lpb, ticks, playing true} before quantum 0 (it overrides the patch\'s saved transport).',
  events: 'ev.t in seconds (double). An event is applied at the START of quantum k (t <= 0: quantum 0). chc: k = ceil(ceil(t * 48000.0) / 128) in double (the Chrome-measured k-rate rule). tempo: k = ceil(t / q) with q = 128.0 / 48000.0 in double, and quantum 0 when ceil(t / q) * q is not inside (0, quanta * q). chc: node of ev.track (if it has one) parameter p = v (-1 own, 0 in, 1 bypassed, 2 flipped), held until changed. tempo: transport message {tempo: bpm, ticks: tks} to every node. expect.eventQuanta lists k per event.',
};
function build() {
  const out = [], seenNames = new Set();
  for (const c of cases) {
    if (seenNames.has(c.name)) throw new Error('duplicate case ' + c.name); seenNames.add(c.name);
    const r = run(c), S0 = typeof c.song === 'function' ? c.song() : c.song;
    const controls = {};
    for (const v of c.controls) { const x = run(c, v === 'treatAsNotes' ? { treatAsNotes: c.controlNote } : { [v]: true }); if (x.hash === r.hash) throw new Error(`${c.name}: the control ${v} gives the same hash as the case: the case does not pin what it says`); controls[v] = x.hash; }
    if (c.events.length) for (const sh of [-1, 1]) { const x = run(c, { shift: sh }); if (x.hash === r.hash) throw new Error(`${c.name}: the events one quantum ${sh < 0 ? 'early' : 'late'} give the same hash: the events do not matter in this case`); controls['events' + (sh < 0 ? 'EarlyBy1' : 'LateBy1')] = x.hash; }
    out.push({ name: c.name, note: c.note, song: S0, inputs: c.inputs, noteTracks: c.noteTracks, stem: c.stem, events: c.events, quanta: c.quanta, expect: r.expect, hash: r.hash, head: r.head, qh: r.qh, controls });
  }
  return out;
}
const text = list => '{"header":' + JSON.stringify(HEADER, null, 1).replace(/\n\s*/g, ' ') + ',\n"cases":[\n' + list.map(c => JSON.stringify(c)).join(',\n') + '\n]}\n';
const args = process.argv.slice(2), has = a => args.includes(a), arg = a => { const i = args.indexOf(a); return i >= 0 ? args[i + 1] : null; };
const only = arg('--case');
if (has('--extract-beta')) {
  const file = arg('--extract-beta'), raw = fs.readFileSync(file), frag = fragmentOf(JSON.parse(raw.toString('utf8')).song);
  fs.writeFileSync(BETA_FRAG, JSON.stringify({ source: 'sounds/BETA-TESTING-ELECTRO.thunder of beta ' + betaCommit, sha256: crypto.createHash('sha256').update(raw).digest('hex'), fragment: frag }) + '\n'); console.log('wrote', BETA_FRAG);
} else if (has('--list')) {
  for (const c of cases) console.log(c.name.padEnd(30), c.note);
} else {
  const sel = cases.filter(c => !only || c.name.includes(only));
  if (has('--dump')) {
    const dir = arg('--dump'); fs.mkdirSync(dir, { recursive: true });
    for (const c of sel) { const r = run(c), b = Buffer.alloc(r.L.byteLength * 2); Buffer.from(r.L.buffer).copy(b, 0); Buffer.from(r.R.buffer).copy(b, r.L.byteLength); fs.writeFileSync(path.join(dir, c.name.replace(/\//g, '_') + '.f32'), b); console.log(c.name, r.hash); }
  } else {
    const t = text(build());
    if (has('--golden')) { fs.writeFileSync(GOLD, t); console.log('wrote', GOLD, cases.length, 'cases,', t.length, 'bytes'); }
    else if (has('--check')) {
      const cur = fs.existsSync(GOLD) ? fs.readFileSync(GOLD, 'utf8') : '';
      if (cur !== t) { const a = cur.split('\n'), b = t.split('\n'); for (let i = 0; i < Math.max(a.length, b.length); i++) if (a[i] !== b[i]) { const x = (a[i] || '').match(/"name":"([^"]+)"/), y = (b[i] || '').match(/"name":"([^"]+)"/); console.error('chains_song_cases.json differs at line ' + (i + 1) + (y ? ' (case ' + y[1] + ')' : '')); break; } process.exit(1); }
      console.log('chains_song_cases.json matches the JS:', cases.length, 'cases');
    } else process.stdout.write(t);
  }
}
