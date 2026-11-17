// U26: HOW THE PAGE'S OWN LIVE NODES (BASS_PROC / VOX_PROC of index.html, the real process() code run over the AudioParam lanes the page schedules) DIFFER FROM THE LIVE RULE (the rendered path's rule; see docs/port/U26-NOTES.md).
//   node native/tools/engine-u26-dump.mjs --page-live     prints the table for U26-NOTES.md (not a gate; it needs no committed reference)
// The lanes are built the way bassNote / kill schedule them: gate (setValueAtTime 1 at note-on, 0 at note-off), trig (a counter, +1 at every note-on), leg, syl, same (set at note-on), rate (a step, or setValueAtTime + linear ramp).
// The node runs in 128-sample quanta. "rule" is seqRender (docs/port/U26-NOTES.md): control blocks anchored at the last NOTE event; "page" is BassProc.process / VoxProc.process.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { engineOf } from './u26-tables.mjs';
import { seqRender } from './engine-u26-dump.mjs';

const SR = 48000, Q = 128;

function procClass(root, which) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
  const a = html.indexOf('const ' + which + ' = `'), b = html.indexOf('registerProcessor(', a);
  const body = html.slice(html.indexOf('`', a) + 1, b);
  return body;
}

// the page's lanes as per-sample arrays (a-rate), from the sequence's events
function lanes(c, N) {
  const g = new Float32Array(N), tr = new Float64Array(N), lg = new Float32Array(N), sy = new Float64Array(N), sm = new Float32Array(N), rt = new Float64Array(N);
  const ev = c.events.map((e, i) => ({ ...e, i })).sort((a, b) => a.t - b.t || a.i - b.i);
  let gv = 0, tv = 0, lv = 0, sv = 0, mv = 0, n = 0, ei = 0;
  let v0 = 1, t0 = 0, v1 = 1, t1 = 0;
  const at = t => t >= t1 ? v1 : v0 + (v1 - v0) * ((t - t0) / (t1 - t0));
  for (let s = 0; s < N; s++) {
    while (ei < ev.length && ev[ei].t <= s) {
      const e = ev[ei++];
      if (e.type === 'on') { gv = 1; tv = ++n; lv = e.leg ? 1 : 0; sv = e.syl || 0; mv = e.same ? 1 : 0; }
      else if (e.type === 'off') gv = 0;
      else if (e.type === 'rate') { v0 = v1 = e.v; t0 = t1 = e.t; }
      else if (e.type === 'ramp') { const cur = at(e.t); v0 = cur; t0 = e.t; v1 = e.v; t1 = e.t + e.dur; }
    }
    g[s] = gv; tr[s] = tv; lg[s] = lv; sy[s] = sv; sm[s] = mv; rt[s] = at(s);
  }
  return { g, tr, lg, sy, sm, rt };
}

export function runPage(root, E, c) {
  const which = c.kind === 'bass' ? 'BASS_PROC' : 'VOX_PROC', src = procClass(root, which), N = c.N;
  const ctx = vm.createContext({ Math, Float32Array, Float64Array, Date, BassCore: E.BassCore, VoxCore: E.VoxCore, sampleRate: c.sr, globalThis: null, console });
  ctx.globalThis = ctx;
  let Cls = null; ctx.AudioWorkletProcessor = class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }; ctx.registerProcessor = (n, k) => { Cls = k; };
  vm.runInContext(src + '\nregisterProcessor("x", ' + (c.kind === 'bass' ? 'BassProc' : 'VoxProc') + ');', ctx);
  const node = new Cls({ processorOptions: { p: c.p } });
  const ln = lanes(c, N + Q), L = new Float32Array(N + Q), R = new Float32Array(N + Q);
  // bpm / wob (k-rate): the events' last value up to the quantum's start
  const kv = { bpm: 120, wob: 0 };
  const evK = c.events.filter(e => e.type === 'bpm' || e.type === 'wob').sort((a, b) => a.t - b.t);
  let ki = 0;
  for (let q = 0; q * Q < N; q++) {
    const s0 = q * Q; while (ki < evK.length && evK[ki].t <= s0) { kv[evK[ki].type] = evK[ki].v; ki++; }
    const sl = a => a.subarray(s0, s0 + Q), o = [new Float32Array(Q), new Float32Array(Q)];
    const P = { rate: sl(ln.rt), gate: sl(ln.g), trig: sl(ln.tr), leg: sl(ln.lg), syl: sl(ln.sy), same: sl(ln.sm), bpm: [kv.bpm], wob: [kv.wob] };
    node.process([], [o], P); L.set(o[0], s0); R.set(o[1], s0);
  }
  return [L.slice(0, N), R.slice(0, N)];
}

const rms = a => { let s = 0; for (const x of a) s += x * x; return Math.sqrt(s / (a.length || 1)); };
function compare(a, b) {
  let d = 0, mx = 0, same = 0; const diff = new Float32Array(a.length);
  for (let i = 0; i < a.length; i++) { diff[i] = a[i] - b[i]; if (diff[i] === 0) same++; mx = Math.max(mx, Math.abs(diff[i])); }
  const sr = rms(a), dr = rms(diff);
  return { identical: same === a.length, firstDiff: diff.findIndex(x => x !== 0), maxAbs: mx, relDb: dr > 0 && sr > 0 ? 20 * Math.log10(dr / sr) : -Infinity };
}

export function main(root) {
  const E = engineOf(root), cases = JSON.parse(fs.readFileSync('/tmp/u26/ref/cases.json', 'utf8')).cases.filter(c => c.live && (c.kind === 'bass' || c.kind === 'vox'));
  console.log('page live (BassProc / VoxProc.process in 128-sample quanta) against the live rule (seqRender), per sequence; relDb = RMS of the difference relative to the RMS of the signal');
  let nSame = 0; const rows = [];
  for (const c0 of cases) {
    const c = { ...c0, N: Math.ceil(c0.N / Q) * Q };   // the node runs whole quanta: the stream ends on a quantum boundary, so the last control block is the same in both
    const rule = seqRender(E, c), page = runPage(root, E, c), cmp = compare(rule[0], page[0]);
    // does "the rule with host cuts at the quantum boundaries" explain it? (the page's block ends at every quantum end; its rate is read at the segment's start)
    const cuts = []; for (let s = Q; s < c.N; s += Q) cuts.push(s);
    const withCuts = compare(seqRender(E, c, cuts)[0], page[0]);
    if (cmp.identical) nSame++;
    rows.push({ name: c.name, ...cmp, cutsIdentical: withCuts.identical, cutsDb: withCuts.relDb });
  }
  for (const r of rows) console.log(`${r.name.padEnd(34)} ${r.identical ? 'identical' : 'differs ' + r.relDb.toFixed(1) + ' dB, max ' + r.maxAbs.toExponential(2) + ', first at ' + r.firstDiff}   | rule + cuts at every 128: ${r.cutsIdentical ? 'identical' : r.cutsDb.toFixed(1) + ' dB'}`);
  console.log(`${nSame} of ${rows.length} sequences are bit-identical to the rule; ${rows.filter(r => r.cutsIdentical).length} are bit-identical to the rule with a control-block cut at every 128-sample quantum`);
}
