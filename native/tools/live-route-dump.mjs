// LIVE ROUTE gate L10(f): the decision log of the core's live-engine player (core/seq-core.js = core-processor.js + the page's declarations: ThunderCore's liveNote / liveStart /
// kill / off / mod / stp / stopAll, voxG; with Auto.hold patched to the page's cancelAndHoldAtTime, D7), run in a Node `vm` with fake nodes registered in globalThis.__tcNodes. No audio: the log is what the
// core DECIDES, per event: which node key (and pump-synth lane) it drives, and each node's override Auto timelines afterwards (gate, trig, leg,
// rate, syl, same, acc, vel, wob, plus the level g and pan pn: every event {kind, time, value}). The native LivePool must give the same decisions.
//   node native/tools/live-route-dump.mjs              prints the log of every case (JSON lines) to stdout
//   node native/tools/live-route-dump.mjs --golden     writes native/tests/golden/live_route_log.json (the full logs) and live_route_golden.inc (FNV-1a 64 of each log)
//   node native/tools/live-route-dump.mjs --check      exits 1 if either committed file differs (Node 22 on Linux is the reference)
// Math.random is pinned to 0 so a node's trigger counter starts at 1e6 (the log writes trig values minus 1e6).
// Event times are whole samples at 48 kHz (a double s / 48000); a case's events are handed over at the block (128 frames) named, as the page does.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { D7_HOLD } from './d7-hold.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SR = 48000, NT = 32, N = 128;
const src = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');

const F = x => x / SR;
const FB = (b, o = 0) => F(b * N + o);
const note = o => Object.assign({ type: 'note', t: 0, track: 0, poly: false, col: 0, pad: 0, rate: 1, gain: 0.8, pan: 128 }, o);
const off = o => Object.assign({ type: 'off', t: 0, track: 0 }, o);
const mod = o => Object.assign({ type: 'mod', t: 0, track: 0 }, o);
const C0 = { cut: -1, res: -1, dec: -1, atk: -1, rel: -1 }, O0 = { cut: 0, res: 0, env: 0, atk: 0, dec: 0, sus: 0, rel: 0 };
const eng = (t, c, o, track = 0) => ({ type: 'eng', t, track, c: Object.assign({}, C0, c), o: Object.assign({}, O0, o) });

// the pads' kit rows (what the page sends as {op:'kit', kv}): lk = the live engine's kind
const LK = {
  bass: { kind: 'bass' }, vox: { kind: 'vox', vox: true }, acid: { kind: 'acid' }, mach: { kind: 'mach' },
  wavesl: { kind: 'wavesl' }, pmonol: { kind: 'pmonol', ps: true, vel: 100 },
};
const pad = (lk, extra) => Object.assign({ choke: 0, syn: true, rel: 0.1, live: true, lk: lk ? LK[lk] : undefined }, extra || {});
// kit: 0 bass, 1 vox, 2 acid, 3 mach, 4 pump synth, 5 sampler (the core's own), 6 bass in choke group 1, 7 sampler in group 1, 8 waves
const KV = [pad('bass'), pad('vox'), pad('acid'), pad('mach'), pad('pmonol'), { choke: 0, syn: false, rel: 0, live: false }, pad('bass', { choke: 1 }), { choke: 1, syn: false, rel: 0, live: false }, pad('wavesl')];
// the nodes a case registers: track:pad:kind[:j]
const nodeKeys = (tracks) => {
  const ks = [];
  for (const t of tracks) {
    for (const [p, k] of [[0, 'bass'], [2, 'acid'], [3, 'mach'], [4, 'pmonol'], [6, 'bass'], [8, 'wavesl']]) ks.push(`${t}:${p}:${k}`);
    ks.push(`${t}:1:vox`); for (let j = 1; j <= 4; j++) ks.push(`${t}:1:vox:${j}`);
  }
  return ks;
};

const CASES = {
  bass_mono: { tracks: [0], evs: [
    [0, note({ t: FB(1), rate: 1, wob: 0.5 })], [0, note({ t: FB(10, 7), rate: 1.5, slide: { from: 1, dur: 0.02 }, gain: 0.5, pan: 60 })],
    [0, note({ t: FB(20), rate: 2, arp: [[FB(20), 1], [FB(22), 1.5], [FB(24), 2]] })], [0, note({ t: FB(30), rate: 0.75, bend: { mul: 2, t1: FB(40) } })],
    [10, mod({ t: FB(35), mul: 1.25 })], [20, off({ t: FB(50, 3) })], [20, note({ t: FB(60), rate: 1 })], [20, note({ t: FB(60, 40), rate: 1.2, cut: FB(70) })] ] },
  bass_kill_back: { tracks: [0], evs: [   // an off before a booked later note takes the note back (kill: at < lastOn); one after it does not
    [0, note({ t: FB(10) })], [0, off({ t: FB(5) })], [0, note({ t: FB(20) })], [0, off({ t: FB(25) })], [0, note({ t: FB(30), rel: FB(32) })] ] },
  bass_same_sample: { tracks: [0], evs: [   // an off and a note at the same sample (D4: the note takes back the booked off)
    [0, note({ t: FB(5) })], [0, off({ t: FB(12) })], [0, note({ t: FB(12) })], [0, note({ t: FB(12), rate: 2 })] ] },
  acid_mach: { tracks: [0, 1], evs: [
    [0, note({ t: FB(1), pad: 2, acc: true, rate: 1.1 })], [0, note({ t: FB(3), pad: 2, acc: false, slide: { from: 1, dur: 0.01 }, rate: 1.4 })],
    [0, note({ t: FB(2), pad: 3, track: 1, acc: true, vel: 0.5 })], [5, note({ t: FB(8), pad: 3, track: 1, acc: false })], [9, off({ t: FB(12), track: 1 })] ] },
  vox_mono: { tracks: [0], evs: [
    [0, note({ t: FB(1), pad: 1 })], [0, note({ t: FB(4), pad: 1, rate: 1.2 })], [0, note({ t: FB(7), pad: 1, rate: 1.5, slide: { from: 1.2, dur: 0.02 } })],
    [0, note({ t: FB(9), pad: 1, rate: 0.9 })], [0, note({ t: FB(9, 100), pad: 1, rate: 0.9 })] ] },
  vox_poly: { tracks: [0], evs: [   // chords (4 ms rule, voxG), a 5th note, a slide in a chord, same-col pick, overflow guard
    [0, note({ t: FB(1), pad: 1, poly: true, col: 0, rate: 1 })], [0, note({ t: FB(1, 40), pad: 1, poly: true, col: 1, rate: 1.25 })],
    [0, note({ t: FB(1, 80), pad: 1, poly: true, col: 2, rate: 1.5 })], [0, note({ t: FB(1, 120), pad: 1, poly: true, col: 3, rate: 2 })],
    [0, note({ t: FB(4), pad: 1, poly: true, col: 4, rate: 0.5 })], [0, note({ t: FB(6), pad: 1, poly: true, col: 0, rate: 1.1, slide: { from: 1, dur: 0.02 } })],
    [0, note({ t: FB(8), pad: 1, poly: true, col: 1, rate: 1.3 })], [3, off({ t: FB(12), cols: [0, 1] })], [3, note({ t: FB(14), pad: 1, poly: true, col: 5, rate: 1 })],
    [3, note({ t: FB(14, 2), pad: 1, poly: true, col: 6, rate: 1 })], [5, off({ t: FB(20) })], [5, note({ t: FB(22), pad: 1, poly: true, col: 0 })] ] },
  vox_idle_gap: { tracks: [0], evs: [   // long silence between phrases (the idle-skip bit-compare case)
    [0, note({ t: FB(1), pad: 1 })], [0, off({ t: FB(10) })], [0, note({ t: FB(900), pad: 1, rate: 1.5 })], [800, note({ t: FB(1200), pad: 1, poly: true, col: 0 })] ] },
  pump_lanes: { tracks: [0], evs: [
    [0, note({ t: FB(1), pad: 4, rate: 1 })], [0, note({ t: FB(3), pad: 4, rate: 1.5, vel: 0.5 })], [0, note({ t: FB(5), pad: 4, poly: true, col: 0, rate: 1, vc: 3 })],
    [0, note({ t: FB(5, 9), pad: 4, poly: true, col: 1, rate: 1.25 })], [0, note({ t: FB(5, 18), pad: 4, poly: true, col: 2, rate: 1.5 })],
    [0, note({ t: FB(5, 27), pad: 4, poly: true, col: 3, rate: 2 })], [0, note({ t: FB(5, 36), pad: 4, poly: true, col: 4, rate: 3 })],
    [4, off({ t: FB(9), cols: [1] })], [4, note({ t: FB(10), pad: 4, poly: true, col: 5 })] ] },
  waves_live: { tracks: [0], evs: [[0, note({ t: FB(2), pad: 8 })], [0, note({ t: FB(6), pad: 8, rate: 1.5, slide: { from: 1, dur: 0.03 } })], [3, off({ t: FB(9) })]] },
  takeover: { tracks: [0], evs: [   // a live note takes the track from the sampler voices; the sampler takes it from the live voice
    [0, note({ t: FB(1), pad: 5, poly: true, col: 0 })], [0, note({ t: FB(2), pad: 5, poly: true, col: 1 })], [0, note({ t: FB(3), pad: 0 })],
    [0, note({ t: FB(6), pad: 5, rate: 1 })], [0, note({ t: FB(8), pad: 1, poly: true, col: 0 })], [0, note({ t: FB(9), pad: 5, poly: true, col: 1 })] ] },
  choke_cross: { tracks: [0, 1], evs: [   // a choke group across tracks: live bass (pad 6) and a sampler pad (7), both ways
    [0, note({ t: FB(1), pad: 6, track: 0 })], [0, note({ t: FB(3), pad: 7, track: 1 })], [0, note({ t: FB(5), pad: 6, track: 1 })],
    [0, note({ t: FB(7), pad: 7, track: 0, poly: true, col: 0 })], [0, note({ t: FB(9), pad: 6, track: 0 })] ] },
  vox_samecol: { tracks: [0], evs: [   // the same-column pick: a column that is still sounding is re-taken by ITS node although a free one exists (and not when it started within 4 ms: then it is a chord note)
    [0, note({ t: FB(1), pad: 1, poly: true, col: 0 })], [0, note({ t: FB(3), pad: 1, poly: true, col: 1 })], [0, note({ t: FB(5), pad: 1, poly: true, col: 2 })],
    [0, note({ t: FB(7), pad: 1, poly: true, col: 1 })], [0, note({ t: FB(9), pad: 1, poly: true, col: 3 })], [0, note({ t: FB(9, 100), pad: 1, poly: true, col: 3 })],
    [0, off({ t: FB(12), cols: [2] })], [0, note({ t: FB(14), pad: 1, poly: true, col: 2 })] ] },
  poly_cap: { tracks: [0], evs: [   // the cap of 4 by serial across pools: sampler poly notes and vox live notes on one track
    ...[0, 1, 2].map(k => [0, note({ t: FB(1 + k), pad: 5, poly: true, col: k })]),
    [0, note({ t: FB(5), pad: 1, poly: true, col: 3 })], [0, note({ t: FB(6), pad: 1, poly: true, col: 4 })], [0, note({ t: FB(7), pad: 5, poly: true, col: 5 })] ] },
  mod_stp: { tracks: [0, 1], evs: [
    [0, note({ t: FB(1), pad: 0 })], [0, note({ t: FB(1), pad: 1, track: 1, poly: true, col: 0 })], [0, note({ t: FB(1, 5), pad: 1, track: 1, poly: true, col: 1 })],
    [2, mod({ t: FB(5), gain: 0.2, pan: 0 })], [2, mod({ t: FB(6), track: 1, pan: 255, mul: 1.5 })], [4, mod({ t: FB(8), bend: { mul: 0.5, t1: FB(20) } })],
    [10, { type: 'stp', t: FB(12), track: 0 }], [10, { type: 'stp', t: FB(13), all: true }], [12, note({ t: FB(14), pad: 0 })] ] },
  stop_all: { tracks: [0, 1], evs: [
    [0, note({ t: FB(1), pad: 0 })], [0, note({ t: FB(30), pad: 0 })], [0, note({ t: FB(2), pad: 1, track: 1, poly: true, col: 0 })], [0, note({ t: FB(3), pad: 4, track: 1 })],
    [10, { op: 'stopAll', at: FB(12) }], [11, note({ t: FB(14), pad: 0 })], [11, note({ t: FB(14), pad: 1, track: 1, poly: true })] ] },
  eng_basic: { tracks: [0], evs: [   // the targets reach a node that exists: set for the commands, set then ramps for the offsets; a default state with the targets on is put too; a note takes the state in force
    [0, note({ t: FB(1) })], [0, eng(FB(2), { cut: 100, atk: 20 }, { res: 30, env: -40 })], [0, eng(FB(5), { cut: 140, atk: 20 }, { res: 60, env: -40, rel: 12 })],
    [1, eng(FB(8), {}, {})], [1, note({ t: FB(10) })], [1, eng(FB(12), { dec: 200 }, { cut: -90 })], [2, note({ t: FB(14), rate: 1.5 })], [3, eng(FB(18), { dec: 200 }, { cut: 50 })] ] },
  eng_late_bind: { tracks: [0], evs: [   // an eng before the node's first note: kept (engSt), put at the note; a default state with the targets off puts nothing; the next eng reaches the nodes that exist (acid and mach, not bass)
    [0, eng(FB(1), { res: 90 }, { cut: 20 })], [0, note({ t: FB(3), pad: 2, acc: true })], [0, note({ t: FB(4), pad: 3 })], [1, eng(FB(6), { res: 10 }, { cut: -20, sus: 7 })],
    [2, eng(FB(8), {}, {})], [2, eng(FB(9), {}, {})], [3, note({ t: FB(11), pad: 2 })] ] },
  eng_vox_poly: { tracks: [0], evs: [   // a poly note makes the four nodes; the mono node does not exist until a mono note
    [0, eng(FB(1), { atk: 30 }, { cut: 11 })], [0, note({ t: FB(2), pad: 1, poly: true, col: 0 })], [0, eng(FB(4), { atk: 60 }, { cut: 22, rel: 5 })], [0, note({ t: FB(6), pad: 1, poly: true, col: 1 })],
    [0, note({ t: FB(8), pad: 1 })], [1, eng(FB(10), { rel: 40 }, { cut: 33 })] ] },
  eng_stop_all: { tracks: [0], evs: [
    [0, note({ t: FB(1) })], [0, eng(FB(2), { cut: 80 }, { res: 10 })], [0, eng(FB(4), { cut: 90 }, { res: 20 })], [5, { op: 'stopAll', at: FB(8) }], [6, note({ t: FB(10) })], [6, eng(FB(12), { res: 5 }, { cut: 5 })] ] },
  eng_default_off: { tracks: [0], evs: [   // a default state reaches a node only while its targets are on: not before the first real eng, again after it, not after stopAll
    [0, note({ t: FB(1) })], [0, eng(FB(3), {}, {})], [0, eng(FB(5), { cut: 70 }, {})], [0, eng(FB(7), {}, {})], [5, { op: 'stopAll', at: FB(9) }], [6, eng(FB(11), {}, {})], [6, eng(FB(13), { res: 30 }, {})] ] },
  eng_partial: { tracks: [0], evs: [   // keys that are missing are not numbers: nothing is put for them, and the state is not the default
    [0, note({ t: FB(1) })], [0, { type: 'eng', t: FB(2), track: 0, c: { cut: 50 }, o: {} }], [0, { type: 'eng', t: FB(4), track: 0, c: {}, o: { res: 9 } }], [0, { type: 'eng', t: FB(6), track: 0, c: { cut: -1 }, o: {} }] ] },
  no_node: { tracks: [], evs: [[0, note({ t: FB(1), pad: 0 })], [0, note({ t: FB(2), pad: 1, poly: true })]] },   // no node registered: liveNote is false, the page plays
  late: { tracks: [0], evs: [[10, note({ t: FB(2), pad: 0 })], [10, note({ t: FB(9), pad: 0, rate: 2 })]] },   // delivered after its time: set at the block's time
};
export const CASE_NAMES = Object.keys(CASES);

function makeCore() {
  let Cls = null;
  const ctx = vm.createContext({ console, registerProcessor(n, c) { Cls = c; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
    sampleRate: SR, currentTime: 0, currentFrame: 0 });
  vm.runInContext('Math.random = () => 0;', ctx);
  // D7 (verify-265 F3): the PAGE cuts a rate ramp at the next note with cancelAndHoldAtTime (index.html bassNote: the ramp goes on gliding up to the note); the core's Auto.hold drops the ramp's end event and leaves the lane at the ramp's start
  // (a core fault, reported to the owner). The log is the page's: hold() here keeps a ramp that is still running as a ramp to the value it has at t.
  const D7 = D7_HOLD;
  vm.runInContext(src + D7 + '\n;globalThis.__lr = { bufs: coreBufs, setS(s){ S = s; } };', ctx, { filename: 'seq-core.js' });
  return { ctx, proc: new Cls(), U: ctx.__lr };
}

const r6 = x => (typeof x === 'number' && Number.isFinite(x) ? x : String(x));
function ovDump(proc, trigBase) {
  const ov = proc.__ov; if (!ov) return null;
  const A2 = A => A.ev.map(e => [e.k, e.t, e.k === 0 || e.k === 1 ? r6(e.v) : null]);
  const out = {};
  for (const k of [...ov.p.keys()].sort()) out[k] = A2(ov.p.get(k)).map(e => (/^(trig|t\d)$/.test(k) && e[2] !== null ? [e[0], e[1], e[2] - trigBase] : e));
  out.g = A2(ov.g); out.pn = A2(ov.pn);
  return out;
}

function runCase(name) {
  const C = CASES[name], { ctx, proc, U } = makeCore();
  const sine = new Float32Array(22050); for (let i = 0; i < sine.length; i++) sine[i] = Math.fround((i % 100) / 100 - 0.5);
  U.bufs.set(5, { data: sine, data2: null, sr: 48000 }); U.bufs.set(7, { data: sine, data2: null, sr: 48000 });
  U.setS({ bpm: 120, insts: [] });
  proc.kv = KV.map(k => JSON.parse(JSON.stringify(k)));
  const reg = ctx.__tcNodes || (vm.runInContext('globalThis.__tcNodes = new Map()', ctx), ctx.__tcNodes);
  const nodes = new Map(); for (const k of nodeKeys(C.tracks)) { const p = {}; nodes.set(k, p); reg.set(k, p); }
  const log = [];
  const maxB = Math.max(...C.evs.map(([b]) => b)) + 1;
  for (let b = 0; b <= maxB; b++) {
    ctx.currentTime = b * N / SR; ctx.currentFrame = b * N;
    for (const [bb, e] of C.evs) if (bb === b) {
      const ev = JSON.parse(JSON.stringify(e));
      if (e.op === 'stopAll') { proc.stopAll(e.at); log.push({ op: 'stopAll', at: e.at, b }); }   // (the other entries carry `ev`: the event as it was handed in)
      else {
        const r = proc.ev(ev);
        const T = proc.T(ev.track != null ? ev.track : 0);
        log.push({ b, ev: JSON.parse(JSON.stringify(e)), type: ev.type, t: ev.t, track: ev.track, pad: ev.pad, ret: r === undefined ? null : r, live: ev.coreLive || null,
          voices: T.map(v => (v.live ? (v.L.key + (v.L.ps ? '#' + v.lane : '')) : 'smp') + (v.stopAt != null ? '/stop' : '')) });
      }
    }
  }
  const nd = {};
  for (const [k, p] of nodes) { const d = ovDump(p, 1e6); if (d) nd[k] = d; }
  const L = {}; for (const [k, l] of proc.lnodes) L[k] = { lastOn: l.lastOn, sylN: l.sylN, lanes: l.lanes ? l.lanes.map(q => q.lastOn) : undefined };
  const G = {}; for (const [k, g] of proc.voxG) G[k] = { sylN: g.sylN, lastT: g.lastT, lastK: g.lastK };
  return { id: name, tracks: C.tracks, events: log, nodes: nd, lnodes: L, voxG: G };
}

const M64 = (1n << 64n) - 1n;
function fnvStr(s) { const u = Buffer.from(s, 'utf8'); let h = 0xcbf29ce484222325n; for (let i = 0; i < u.length; i++) { h ^= BigInt(u[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';

const a = process.argv.slice(2);
const results = CASE_NAMES.map(n => runCase(n));
const jsonText = '[\n' + results.map(r => JSON.stringify(r)).join(',\n') + '\n]\n';
const inc = ['// GENERATED by native/tools/live-route-dump.mjs (--golden) from core/seq-core.js. Do not edit: a mismatch means the port is wrong.',
  '// FNV-1a 64 over each case\'s decision log as JSON (the same text as an element of live_route_log.json). Node 22 on Linux is the reference engine.',
  'struct LiveRouteGolden { const char* name; unsigned long long hash; };',
  'static const LiveRouteGolden kLiveRouteGolden[] = {',
  ...results.map(r => `  {"${r.id}", ${hex(fnvStr(JSON.stringify(r)))}},`), '};', `static const int kLiveRouteGoldenN = ${results.length};`, ''].join('\n');
const jsonPath = path.join(ROOT, 'native/tests/golden/live_route_log.json'), incPath = path.join(ROOT, 'native/tests/golden/live_route_golden.inc');
if (a[0] === '--golden') { fs.writeFileSync(jsonPath, jsonText); fs.writeFileSync(incPath, inc); console.log('wrote ' + jsonPath + ' and ' + incPath); }
else if (a[0] === '--check') {
  const bad = []; if (fs.readFileSync(jsonPath, 'utf8') !== jsonText) bad.push('live_route_log.json'); if (fs.readFileSync(incPath, 'utf8') !== inc) bad.push('live_route_golden.inc');
  if (bad.length) { console.error(bad.join(', ') + ' differs from what the JS gives now'); process.exit(1); } console.log('live route goldens are current');
} else process.stdout.write(jsonText);
