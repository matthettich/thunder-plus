// D4: THE REFERENCE OF native/tests/test_u15song.cpp (exact, Node only): sounds/ELECTRO.thunder with Flex / Step gate / Retrigger on tracks and tracker commands
// (tests/golden/u15song-patch.json), rendered by the REAL JS: one real ThunderFx worklet per effects node (tracks and the master; tfxChain / normTfx of the page) and
// the JS model of the page's Web Audio strip (tools/mix-dump.mjs's Model, the one test_mix is checked against) with the effects where the page has them
// (after TRIM, before the fader; the master's after MASTER VOL, before OUT).
// What Node does not make (the voices and the sequencer: other port units) is read from <dir>, where `test_u15song --dump <dir>` wrote it: meta.json, events.json (the
// sequencer's FXO/FLX/GAT/RTG lane events and the mixer's TVL/TVA/TPN/MVOL/MOUT events by renderer quantum), in.f32 (what each effects node is given, L then R, per track
// of meta.tracks), bp.f32 (the beat clock each node hears, per meta.bpTracks).
// The commands reach a node as the page player books them: at max(t, the quantum's start) (+ the node's clock delay, 0: no latency plan here); k-rate lanes read at the block start.
//   node native/tools/u15-song-dump.mjs <dir>            writes <dir>/ref.f32 (L then R) and <dir>/ref.json
//   node native/tools/u15-song-dump.mjs <dir> --golden   writes native/tests/golden/u15song_golden.inc (input hash, FNV-1a 64 of the output)
//   node native/tools/u15-song-dump.mjs <dir> --check    exits 1 if the committed u15song_golden.inc differs
import fs from 'node:fs';
import path from 'node:path';
import { ROOT, SR, load } from './fx-src.mjs';
import { Auto, mkNode, f32b, fnv } from './fx-dump.mjs';
import { Model, NT, panMono, panStereo } from './mix-dump.mjs';

const dir = process.argv[2]; if (!dir) { console.error('usage: u15-song-dump.mjs <dir> [--golden | --check]'); process.exit(2); }
const mode = process.argv[3];
const F = load(), f = Math.fround, N = 128;
const rd = n => fs.readFileSync(path.join(dir, n));
const meta = JSON.parse(rd('meta.json')), frames = meta.frames, nq = frames / N;
const evs = JSON.parse(rd('events.json'));
const inB = rd('in.f32'), bpB = rd('bp.f32'), evB = rd('events.json');
const inF = new Float32Array(inB.buffer.slice(inB.byteOffset, inB.byteOffset + inB.length)), bpF = new Float32Array(bpB.buffer.slice(bpB.byteOffset, bpB.byteOffset + bpB.length));
const patch = JSON.parse(fs.readFileSync(path.join(ROOT, 'native/tests/golden/u15song-patch.json'), 'utf8'));
const song = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')).song;
const MIXED = Array.from({ length: NT }, (_, t) => Object.assign({ vol: 224, trim: 128, pan: 128, mute: false, solo: false }, song.mix[t]));
const S = { bpm: song.bpm, mix: MIXED, master: song.master, mout: song.mout == null ? 255 : song.mout, chainOn: new Array(NT).fill(false), chainPatch: null, insts: [] };

const M = new Model(S, N);
for (let t = 0; t < NT; t++) if (M.U.trimGain(MIXED[t].trim) !== 1) throw new Error('track ' + t + ' has a TRIM other than unity: the taps are after TRIM, this tool does not undo it');

// ---- the effects nodes ------------------------------------------------------------------------------------------------------------------
const chainOf = slots => { const A = new Array(8).fill(null); (slots || []).forEach((o, i) => { A[i] = o && F.normTfx(o); }); return F.tfxChain(A, SR, song.bpm); };
const LANES = ['sw0', 'sw1', 'sw2', 'sw3', 'sw4', 'sw5', 'sw6', 'sw7', 'flx', 'gat', 'rtg'];
const nodes = new Map();   // track (32: master) -> { node, clk, lanes }
const addNode = (t, slots) => { if (!slots || !slots.some(Boolean)) return; const { node, clk } = mkNode(chainOf(slots)), lanes = {}; for (const k of LANES) lanes[k] = new Auto(-1); nodes.set(t, { node, clk, lanes }); };
for (let t = 0; t < NT; t++) addNode(t, patch.tfx[t]);
addNode(NT, song.mfx);
const inIdx = new Map(meta.tracks.map((t, i) => [t, i])), bpIdx = new Map(meta.bpTracks.map((t, i) => [t, i]));
for (const t of nodes.keys()) if (!bpIdx.has(t)) throw new Error('no clock was captured for the node of track ' + t);
const TY = { 1: 'tva', 2: 'tpn', 3: 'mout', 4: 'tvl', 9: 'mvol' };
const slice = (t, c, q) => { const i = inIdx.get(t); return i === undefined ? null : inF.subarray((i * 2 + c) * frames + q * N, (i * 2 + c) * frames + q * N + N); };

// run one node over one block, in place on the arrays l, r
function runNode(t, q, l, r) {
  const nd = nodes.get(t); if (!nd) return; const cur = q * N / SR; nd.clk.set(cur);
  const P = { bp: bpF.slice(bpIdx.get(t) * frames + q * N, bpIdx.get(t) * frames + q * N + N) };
  for (const k of LANES) { nd.lanes[k].prune(cur); P[k] = Float32Array.of(f(nd.lanes[k].at(cur))); }
  const o = [new Float32Array(N), new Float32Array(N)]; nd.node.process([[Float32Array.from(l), Float32Array.from(r)], [], []], [o], P); l.set(o[0]); r.set(o[1]);
}

// ---- the strip with the effects in it (Model.block, with the page's insert points) ----------------------------------------------------------
let q = 0;
class FxModel extends Model {
  block(cur, oL, oR) {
    this.ctx.currentTime = cur; this.proc.mixOut(this.outs, N, 1 / SR);
    this.fill(this.cOut, this.out, cur); this.fill(this.cMo, this.mo, cur);
    const o = [0, 0], tl = new Float32Array(N), tr = new Float32Array(N);
    for (const t of this.order) {
      const T = this.tr[t], c = T.c; this.fill(c.inp, T.inp, cur); this.fill(c.g, T.g, cur); this.fill(c.pre, T.pre, cur); this.fill(c.pan, T.pan, cur);
      const iL = this.inL[t], iR = this.inR[t], G = t < NT ? this.G[t] : null, Pn = t < NT ? this.P[t] : null;
      for (let i = 0; i < N; i++) { tl[i] = f(iL[i] * c.inp[i]); tr[i] = f(iR[i] * c.inp[i]); }   // inp (TRIM)
      if (t < NT) runNode(t, q, tl, tr);                                                              // the track's effects
      for (let i = 0; i < N; i++) {
        let l = tl[i], r = tr[i];
        T.L[i] = f(l * c.pre[i]); T.R[i] = f(r * c.pre[i]);
        l = f(l * c.g[i]); r = f(r * c.g[i]);
        if (G) { l = f(l * G[i]); r = f(r * G[i]); }
        const pan = Math.max(-1, Math.min(1, Pn ? f(c.pan[i] + Pn[i]) : c.pan[i]));
        panStereo(l, r, pan, o); T.pL[i] = o[0]; T.pR[i] = o[1];
      }
    }
    const ml = new Float32Array(N), mr = new Float32Array(N);
    for (let i = 0; i < N; i++) {
      let l = 0, r = 0; for (let t = 0; t <= NT; t++) if (!this.routed.has(t)) { l = f(l + this.tr[t].pL[i]); r = f(r + this.tr[t].pR[i]); }
      const gm = f(this.cOut[i] + this.M[i]); ml[i] = f(l * gm); mr[i] = f(r * gm);                                    // out (MASTER VOL + MVOL)
    }
    runNode(NT, q, ml, mr);                                                                                            // the master's effects
    for (let i = 0; i < N; i++) { const go = f(this.cMo[i] + this.O[i]); oL[i] = f(ml[i] * go); oR[i] = f(mr[i] * go); }   // mo (OUT + MOUT)
  }
}
const FM = new FxModel(S, N);

const byQ = new Map(); for (const e of evs) { if (!byQ.has(e.q)) byQ.set(e.q, []); byQ.get(e.q).push(e); }
const L = new Float32Array(frames), R = new Float32Array(frames), oL = new Float32Array(N), oR = new Float32Array(N);
let unhandled = 0;
for (q = 0; q < nq; q++) {
  const cur = q * N / SR;
  for (const e of byQ.get(q) || []) {
    if (e.ty === 7) { const t = e.tr >= NT ? NT : e.tr, nd = nodes.get(t); if (!nd) continue; if (e.p < 0 || e.p > 10) continue; nd.lanes[LANES[e.p]].set(e.v, Math.max(e.t, cur)); }
    else if (TY[e.ty]) { const c = { op: 'ev', type: TY[e.ty], v: e.v, t: e.t }; if (e.tr >= 0) c.track = e.tr; FM.cmd(c, cur); }
    else unhandled++;
  }
  for (let t = 0; t < NT; t++) { const a = slice(t, 0, q), b = slice(t, 1, q); if (a) { FM.inL[t].set(a); FM.inR[t].set(b); } else { FM.inL[t].fill(0); FM.inR[t].fill(0); } }
  FM.block(cur, oL, oR); L.set(oL, q * N); R.set(oR, q * N);
}
if (unhandled) throw new Error(unhandled + ' events of a kind this tool does not apply');
const out = new Float32Array(2 * frames); out.set(L, 0); out.set(R, frames);
fs.writeFileSync(path.join(dir, 'ref.f32'), f32b(out));
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';
const inHash = (() => { const all = Buffer.concat([inB, bpB, evB]); return fnv(all); })(), outHash = fnv(new Uint8Array(f32b(out)));
fs.writeFileSync(path.join(dir, 'ref.json'), JSON.stringify({ inHash: hex(inHash), outHash: hex(outHash), frames, nodes: [...nodes.keys()] }));
console.log(`u15-song-dump: ${nodes.size} effects nodes (tracks ${[...nodes.keys()].join(', ')}), ${frames} frames; input ${hex(inHash)}, output ${hex(outHash)}`);
const inc = ['// GENERATED by native/tools/u15-song-dump.mjs (--golden) from the real ThunderFx worklet + the page-strip model run in Node 22 on x86-64 Linux. Do not edit: a mismatch means the port is wrong.',
  '// kU15SongInHash: FNV-1a 64 of what the C++ voices / sequencer hand to the effects (in.f32, bp.f32, events.json of test_u15song --dump); kU15SongOutHash: of the render (L then R).',
  `static const unsigned long long kU15SongInHash = ${hex(inHash)};`, `static const unsigned long long kU15SongOutHash = ${hex(outHash)};`, `static const int kU15SongFrames = ${frames};`, ''].join('\n');
const incPath = path.join(ROOT, 'native/tests/golden/u15song_golden.inc');
if (mode === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
else if (mode === '--check') { if (fs.readFileSync(incPath, 'utf8') !== inc) { console.error('u15song_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('u15song_golden.inc is current'); }
