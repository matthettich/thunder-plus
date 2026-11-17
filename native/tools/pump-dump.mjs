// U18: THE PUMP HOSTING REFERENCE (exact, Node only): the real ThunderFx worklet (the TFX_SRC template of index.html, loaded by fx-src.mjs) run in Node with a pump.wasm,
// on deterministic stereo signals: every one of the 25 pump effects alone at its defaults and with one factory preset, then host behaviour: ON / OFF / switching mid-run (the
// plugin's own bypass), the OFF rest and its wake (by values moving, by FXO, by ON), latency, block sizes 1 / 37 / 128 / 480 / 1024 / mixed, slots in series (with the
// latency of those before a clocked one), two nodes side by side (one pump.wasm instance for the thread, as the page has), COMP's External sidechain (a send, a track's tap),
// notes for the creative effects (pmidi), FXV for a pump slot (pset pv), pump_hold, a runaway plugin (the fresh instance), the telemetry the node posts.
//   node native/tools/pump-dump.mjs [--wasm PUMP.wasm --meta META(.json|.js)] [outDir]   writes <outDir>/<case>.json (the case + what the worklet posted), <case>.in.f32 (L then R, then the
//                                  taps and sends), <case>.f32 (the output: L then R, then the second node's), lat.json (pumpLatencyOf of every type, default and preset), index.txt
//   node native/tools/pump-dump.mjs --golden / --check   the case list (names): native/tests/golden/pump_cases.inc
// WHICH pump.wasm: --wasm / PUMP_WASM, else the repo's shipped pump/pump.wasm with its pump/pump-meta.js (always there: what the web app runs today). parity-u18.sh also runs a pump.wasm BUILT
// FROM THE PIN (build-wasm.sh) with the pin's own meta (pump-table.mjs --meta). Since web build 638 the shipped wasm is the pin's build (same size; see U18-NOTES.md), so the two runs have the same reference.
// The page's tfxChain pump branch is repeated by hand in pumpEntry() below ({key, pt, pv, bi, on, slot, sck, cr, clk}); pump-table.mjs --check fails when the page's text of it changes.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { ROOT, SR, load } from './fx-src.mjs';
import { Auto, lcg, f32b } from './fx-dump.mjs';

const F = load();
const args = process.argv.slice(2); const opt = {}; const pos = [];
for (let i = 0; i < args.length; i++) { if (args[i] === '--wasm') opt.wasm = args[++i]; else if (args[i] === '--meta') opt.meta = args[++i]; else pos.push(args[i]); }
const WASM = opt.wasm || process.env.PUMP_WASM || path.join(ROOT, 'pump/pump.wasm'), METAF = opt.meta || process.env.PUMP_META || path.join(ROOT, 'pump/pump-meta.js');
function loadMeta() {
  const t = fs.readFileSync(METAF, 'utf8');
  if (METAF.endsWith('.json')) return JSON.parse(t);
  const c = vm.createContext({}); c.globalThis = c; vm.runInContext(t, c); return c.PUMP_META;
}
const M = loadMeta(), TYPES = M.types, pumpBytes = fs.readFileSync(WASM), pumpMod = new WebAssembly.Module(pumpBytes);
const PUMP_CREATIVE = new Set(['tube', 'snap', 'gate', 'echo', 'spring', 'verb', 'grain', 'vox', 'tune', 'crush', 'swirl', 'stutter', 'chop', 'twang', 'strip']);   // (index.html: PUMP_CREATIVE)
const PUMP_START = { limit: { oversampling: 0 }, heat: { oversampling: 0 } };   // (index.html: a new slot starts with these)
const PUMP_NOTES = new Set(['vox', 'tune', 'twang', 'stutter', 'chop']);

// ---- the worklet ------------------------------------------------------------------------------------------------------------------------------
// Every node of a case shares ONE pump.wasm instance (globalThis.__pumpX), as the nodes of an audio thread do; a case starts a fresh one.
export function mkNode(chain, tapMap) {
  const reg = {}, clk = {}, posts = [];
  const fn = new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', '__clk', 'let currentTime = 0; __clk.set = v => { currentTime = v; };\n' + F.workletSrc);
  fn(class { constructor() { this.port = { postMessage: m => posts.push(m), onmessage: null }; } }, (n, c) => { reg[n] = c; }, SR, clk);
  const node = new reg['thunder-tfx']({ processorOptions: { bytes: new Uint8Array(F.tfxWasm), pumpModule: pumpMod, tapMap, chain } });
  return { node, clk, posts };
}

// ---- chain entries (tfxChain's pump branch) ---------------------------------------------------------------------------------------------------------
const clampP = (p, v) => { v = +v; if (!Number.isFinite(v)) v = Number.isNaN(v) ? p.def : v > 0 ? p.max : p.min; v = Math.min(p.max, Math.max(p.min, v)); if (p.flags & 6) v = Math.round(v); return v; };   // pumpClamp (PF_INT | PF_BOOL)
export function pumpVals(type, preset) {   // a fresh slot's f.pump, with a factory preset's values on top (clamped as the page does)
  const f = {}; for (const p of M.fx[type].params) f[p.s] = p.def; Object.assign(f, PUMP_START[type] || {});
  if (preset !== undefined) { const pr = M.fx[type].presets[Math.min(preset, M.fx[type].presets.length - 1)]; for (const p of M.fx[type].params) if (pr.v[p.s] !== undefined) f[p.s] = clampP(p, pr.v[p.s]); }
  return f;
}
export function pumpEntry(type, o = {}) {
  const fx = M.fx[type], f = { ...pumpVals(type, o.preset), ...(o.set || {}) }, bi = fx.params.findIndex(p => p.s === 'bypass');
  const slot = o.slot ?? 0;
  return { key: slot + ':p' + type, pt: TYPES.indexOf(type), pv: fx.params.map(p => f[p.s] ?? p.def), bi: bi < 0 ? -1 : bi, on: o.on !== false, slot, sck: o.sck || 0,
    cr: PUMP_CREATIVE.has(type) || type === 'master', clk: type === 'gate' || type === 'stutter' || type === 'chop' };
}
const chainOf = (...es) => es;

// ---- signals (float32, deterministic) ------------------------------------------------------------------------------------------------------------
export const SIG = {
  // a drum loop with a sustained three-note pad and a little noise, all the way through: no silence
  mix(n) { const r = lcg(4242), L = new Float32Array(n), R = new Float32Array(n), beat = 6000, f = [220, 277.18, 329.63];
    for (let i = 0; i < n; i++) { const p = i % beat, kick = p < 4000 ? Math.sin(2 * Math.PI * (55 + 120 * Math.exp(-p / 800)) * p / SR) * Math.exp(-p / 1500) * 0.7 : 0, hat = p > 3000 && p < 4500 ? (r() * 2 - 1) * 0.3 * Math.exp(-(p - 3000) / 300) : 0;
      let pad = 0; for (let k = 0; k < 3; k++) pad += Math.sin(2 * Math.PI * f[k] * i / SR + k) * 0.08;
      const nz = (r() * 2 - 1) * 0.02; L[i] = Math.fround(kick + hat + pad + nz); R[i] = Math.fround(kick * 0.9 + hat * 1.1 + pad * 0.9 - nz); }
    return [L, R]; },
  // noise for k frames, then silence (a rest needs sound first, then a quiet stretch to compare)
  burst(n, k = 24000) { const r = lcg(99), L = new Float32Array(n), R = new Float32Array(n); for (let i = 0; i < Math.min(k, n); i++) { L[i] = Math.fround((r() * 2 - 1) * 0.3); R[i] = Math.fround((r() * 2 - 1) * 0.3); } return [L, R]; },
  // loud and broken: NaN, infinities, +-100, then a hot noise (the plugin's own gain can take it past +-64)
  hot(n) { const [a, b] = SIG.mix(n); for (let i = 0; i < n; i++) { a[i] = Math.fround(a[i] * 14); b[i] = Math.fround(b[i] * 14); } a[1000] = NaN; b[2000] = Infinity; a[3000] = 100; b[4000] = -100; return [a, b]; },
  tap(n) { const r = lcg(31337), L = new Float32Array(n); for (let i = 0; i < n; i++) { const p = i % 3000, e = p < 400 ? Math.exp(-p / 120) : 0; L[i] = Math.fround((r() * 2 - 1) * 0.6 * e + Math.sin(2 * Math.PI * 90 * i / SR) * 0.2 * e); } return L; },
  send(n) { const r = lcg(2718), L = new Float32Array(n); for (let i = 0; i < n; i++) { const p = i % 4500, e = p < 700 ? Math.exp(-p / 200) : 0; L[i] = Math.fround((r() * 2 - 1) * 0.5 * e); } return L; },
};

// ---- the cases --------------------------------------------------------------------------------------------------------------------------------
// {name, sig, frames, sizes, chain, chain2, sig2, ops: [[param, t, v]], msgs: [{b, msg}], q: [[at, msg]], bp: 'stopped' | 'play' | 'const', bpm, tapMap, taps: ['tap'..], sends: ['send'..]}
const CASES = [], NF = 40960, TRACE = process.env.PUMP_TRACE;   // PUMP_TRACE=<case>: prints when a slot's rest state (rs 0 runs / 1 rests / 2 wakes, B bypassed) changes
for (const type of TYPES) {
  const k = TYPES.indexOf(type);
  CASES.push({ name: `p_${type}_def`, sig: 'mix', frames: NF, chain: chainOf(pumpEntry(type)) });
  CASES.push({ name: `p_${type}_alt`, sig: 'mix', frames: NF, bp: 'play', chain: chainOf(pumpEntry(type, { preset: 1 })),
    ...(PUMP_NOTES.has(type) ? { msgs: [{ b: 0, msg: { pmidi: [[0, 0.02, 0x90, 60, 100], [0, 0.11, 0x80, 60, 0], [0, 0.25, 0x90, 64, 90], [0, 0.26, 0x90, 67, 80], [0, 0.5, 0x80, 64, 0], [0, 0.7, 0x80, 67, 0]] } }] } : {}) });
}
const S = (type, o) => pumpEntry(type, o);
// slots in series; a clocked effect after a look-ahead one (the transport is earlier by the latency before it: build 616); the five mastering effects of a master chain
CASES.push({ name: 'h_series3', sig: 'mix', frames: NF, chain: chainOf(S('eq', { slot: 0, preset: 1 }), S('comp', { slot: 1, preset: 1 }), S('limit', { slot: 2, preset: 1 })) });
CASES.push({ name: 'h_series_clk', sig: 'mix', frames: NF, bp: 'play', chain: chainOf(S('calm', { slot: 0, preset: 1 }), S('gate', { slot: 1, preset: 1 }), S('tube', { slot: 2 })) });
CASES.push({ name: 'h_series_master', sig: 'mix', frames: NF, chain: chainOf(S('tone', { slot: 0, preset: 1 }), S('band', { slot: 1, preset: 1 }), S('heat', { slot: 2, preset: 1 }), S('wide', { slot: 3, preset: 1 }), S('limit', { slot: 4 }), S('meter', { slot: 5 })) });
// ON / OFF / switching: new chains at blocks (the plugin's own bypass, crossfaded inside the plugin), FXO lanes at exact times
{ const mk = on => chainOf(S('eq', { slot: 0, preset: 1, on: on[0] }), S('limit', { slot: 1, preset: 1, on: on[1] }), S('meter', { slot: 2, on: on[2] }));
  CASES.push({ name: 'h_onoff', sig: 'mix', frames: NF, chain: mk([true, true, true]), msgs: [{ b: 60, msg: mk([false, true, false]) }, { b: 140, msg: mk([true, true, true]) }, { b: 141, msg: mk([true, false, true]) }, { b: 200, msg: mk([true, true, false]) }] });
  const t = f => f / SR;
  CASES.push({ name: 'h_fxo', sig: 'mix', frames: NF, chain: chainOf(S('comp', { slot: 0, preset: 1, on: false }), S('limit', { slot: 1, preset: 1 }), S('tube', { slot: 2, on: false })),
    ops: [['sw0', t(4802), 1], ['sw1', t(9600), 0], ['sw2', t(12001), 1], ['sw0', t(16001), 0], ['sw0', t(24000.5), -1], ['sw1', t(26000), -1], ['sw2', t(30000), 0]] }); }
// REST AND WAKE (builds 607 / 609): a slot OFF by its own setting, with sound going through, rests after a quarter second; it wakes when its values move, when ON comes by chain,
// and when an FXO command switches it on (that one within the block). Two seconds, one slot with a look-ahead (LIMIT), one without a latency (COMP).
{ const lim = (on, v) => chainOf(S('limit', { slot: 0, preset: 1, on, set: v })), com = (on, v) => chainOf(S('comp', { slot: 0, preset: 1, on, set: v }), S('eq', { slot: 1, preset: 1, on: true }));
  CASES.push({ name: 'r_wake_on', sig: 'mix', frames: 98304, chain: lim(false), msgs: [{ b: 520, msg: lim(true) }, { b: 700, msg: lim(false) }] });
  CASES.push({ name: 'r_wake_values', sig: 'mix', frames: 98304, chain: lim(false), msgs: [{ b: 400, msg: lim(false, { gain: 6 }) }, { b: 520, msg: lim(false, { gain: 3, ceiling: -3 }) }] });
  CASES.push({ name: 'r_wake_fxo', sig: 'mix', frames: 98304, chain: lim(false), ops: [['sw0', 1.0 + 17 / SR, 1], ['sw0', 1.6, 0], ['sw0', 1.9, 1]] });
  CASES.push({ name: 'r_comp_eq', sig: 'mix', frames: 98304, chain: com(false), msgs: [{ b: 450, msg: com(true) }] });
  CASES.push({ name: 'r_burst', sig: 'burst', frames: 98304, chain: lim(false), msgs: [{ b: 300, msg: lim(true) }] });
  CASES.push({ name: 'r_sleep', sig: 'burst', frames: 33 * 48000 / 8, chain: chainOf(S('echo', { slot: 0, preset: 1 }), S('verb', { slot: 1, preset: 1 })) });
  CASES.push({ name: 'r_rest_node', sig: 'burst', frames: 98304, chain: chainOf(S('limit', { slot: 0, preset: 1 }), S('echo', { slot: 1, preset: 1 })), msgs: [{ b: 100, msg: { rest: true } }, { b: 300, msg: { rest: false } }] }); }
// block sizes (the worklet's tcChunks cuts a long block into 128s; the clock and notes go with them)
for (const n of [1, 37, 128, 480, 1024]) CASES.push({ name: `b_blk${n}`, sig: 'mix', frames: n === 1 ? 6000 : 24000, sizes: [n], bp: 'play', chain: chainOf(S('eq', { slot: 0, preset: 1 }), S('comp', { slot: 1, preset: 1 }), S('gate', { slot: 2, preset: 1 })), ops: [['sw1', 2000 / SR, 0], ['sw1', 9000 / SR, -1]] });
CASES.push({ name: 'b_blk_mixed', sig: 'mix', frames: 24000, sizes: [128, 7, 130, 1, 99, 128, 481, 300], bp: 'play', chain: chainOf(S('calm', { slot: 0, preset: 1 }), S('stutter', { slot: 1, preset: 1 })), msgs: [{ b: 0, msg: { pmidi: [[1, 0.05, 0x90, 60, 100], [1, 0.12, 0x80, 60, 0]] } }] });
// two nodes side by side, one pump.wasm instance for both; the second hears other sound
CASES.push({ name: 'x_two_hosts', sig: 'mix', sig2: 'burst', frames: NF, chain: chainOf(S('comp', { slot: 0, preset: 1 }), S('limit', { slot: 1, preset: 1 })), chain2: chainOf(S('eq', { slot: 0, preset: 1 }), S('spring', { slot: 1, preset: 1 })) });
CASES.push({ name: 'x_two_hosts_same', sig: 'mix', sig2: 'mix', frames: NF, bp: 'play', chain: chainOf(S('tube', { slot: 0, preset: 1 }), S('crush', { slot: 1, preset: 1 })), chain2: chainOf(S('tube', { slot: 0, preset: 1 }), S('crush', { slot: 1, preset: 1 })) });
// COMP's External sidechain: a send (sck 1) and the tap of track 3 (sck 9 + 3); the chunked call; the same with no source connected
{ const ext = (sck, slot = 0) => S('comp', { slot, preset: 1, set: { sc_source: 1, threshold: -30, ratio: 8 }, sck });
  CASES.push({ name: 's_send', sig: 'mix', frames: NF, chain: chainOf(ext(1)), sends: ['send'] });
  CASES.push({ name: 's_tap', sig: 'mix', frames: NF, chain: chainOf(ext(12)), tapMap: [3], taps: ['tap'] });
  CASES.push({ name: 's_tap_chunked', sig: 'mix', frames: NF, sizes: [480, 37], chain: chainOf(ext(12)), tapMap: [5, 3], taps: ['send', 'tap'] });
  CASES.push({ name: 's_none', sig: 'mix', frames: 12000, chain: chainOf(ext(1), ext(14, 1)) });
  CASES.push({ name: 's_send2_missing', sig: 'mix', frames: 12000, chain: chainOf(ext(2)), sends: ['send'] }); }
// FXV (pset pv: a pump slot's whole value list at a time, mid-block), pump_hold (EQ listen), a runaway plugin (a fresh instance), the node's telemetry
{ const e0 = S('eq', { slot: 0, preset: 1 }), pv2 = pumpEntry('eq', { preset: 1, set: { out_gain: -6 } }).pv, pv3 = pumpEntry('limit', { preset: 0, set: { gain: 9 } }).pv;
  CASES.push({ name: 'v_pset', sig: 'mix', frames: NF, chain: chainOf(e0, S('limit', { slot: 1, preset: 1 })), q: [[5000 / SR + 1e-7, { pset: { slot: 0, pv: pv2 } }], [20000.5 / SR, { pset: { slot: 1, pv: pv3 } }], [30000 / SR, { pset: { slot: 7, pv: pv3 } }]] });
  CASES.push({ name: 'v_hold_eq', sig: 'mix', frames: NF, chain: chainOf(S('eq', { slot: 0, preset: 1 })), msgs: [{ b: 40, msg: { phold: { key: '0:peq', kind: 0, a: 1 } } }, { b: 200, msg: { phold: { key: '0:peq', kind: 0, a: -1 } } }] });
  CASES.push({ name: 'v_hot', sig: 'hot', frames: 24576, chain: chainOf(S('eq', { slot: 0, set: { out_gain: 24 } }), S('tube', { slot: 1 })) });
  CASES.push({ name: 'v_hot_low', sig: 'hot', frames: 24576, chain: chainOf(S('limit', { slot: 0, preset: 1 }), S('comp', { slot: 1 })) });
  CASES.push({ name: 'v_tel', sig: 'mix', frames: 49152, chain: chainOf(S('comp', { slot: 0, preset: 1 }), S('limit', { slot: 1, preset: 1 }), S('band', { slot: 2, preset: 1 }), S('meter', { slot: 3 }), S('eq', { slot: 4 })), msgs: [{ b: 0, msg: { lvl: true } }, { b: 0, msg: { tel: true } }, { b: 250, msg: { tel: false } }] }); }

// ---- the run ----------------------------------------------------------------------------------------------------------------------------------
const sigOf = (name, n) => SIG[name](n);
function runCase(c) {
  const n = c.frames || NF, bpm = c.bpm || 120, sizes = c.sizes || [128];
  globalThis.__pumpX = undefined; globalThis.__pumpBytes = undefined; globalThis.__pumpMod = pumpMod;   // a fresh pump.wasm instance for the case
  const A = mkNode(c.chain, c.tapMap), nodes = [A]; if (c.chain2) nodes.push(mkNode(c.chain2, c.tapMap));
  const ins = nodes.map((_, i) => sigOf(i ? c.sig2 : c.sig, n)), taps = (c.taps || []).map(k => SIG[k](n)), sends = (c.sends || []).map(k => SIG[k](n));
  const lanes = {}; for (const k of ['sw0', 'sw1', 'sw2', 'sw3', 'sw4', 'sw5', 'sw6', 'sw7', 'flx', 'gat', 'rtg']) lanes[k] = new Auto(-1);
  for (const [k, t, v] of c.ops || []) { if (k === 'reset') { for (const L of Object.values(lanes)) { L.cancel(t); L.set(-1, t); } } else lanes[k].set(v, t); }
  for (const [at, msg] of c.q || []) { const q = A.node.__q || (A.node.__q = []); let i = q.length; while (i > 0 && q[i - 1][0] > at) i--; q.splice(i, 0, [at, msg]); }
  const outs = nodes.map(() => [new Float32Array(n), new Float32Array(n)]), tel = [], msgs = c.msgs || [];
  let f0 = 0, b = 0, si = 0, lastSt = '';
  while (f0 < n) {
    const m = Math.min(sizes[si++ % sizes.length], n - f0), t0 = f0 / SR;
    nodes.forEach((N, ni) => {
      N.clk.set(t0);
      if (ni === 0) for (const e of msgs) if (e.b === b) N.node.port.onmessage({ data: e.msg });
      const bp = new Float32Array(m); if (c.bp === 'play') for (let i = 0; i < m; i++) bp[i] = Math.fround(((f0 + i) * bpm / 60 / SR) % 16); else bp.fill(-1);
      const P = { bp: c.bp === 'const' ? Float32Array.of(-1) : bp };
      for (const k in lanes) { lanes[k].prune(t0); P[k] = Float32Array.of(Math.fround(ni ? -1 : lanes[k].at(t0))); }
      const a = ins[ni][0].slice(f0, f0 + m), a2 = ins[ni][1].slice(f0, f0 + m), o = [new Float32Array(m), new Float32Array(m)], np = N.posts.length;
      N.node.process([[a, a2], taps.map(t => t.slice(f0, f0 + m)), sends.map(t => t.slice(f0, f0 + m))], [o], P);
      outs[ni][0].set(o[0], f0); outs[ni][1].set(o[1], f0);
      if (TRACE === c.name && !ni) { const st = N.node.slots.map(x => (x.pu !== undefined ? 'h' + x.pu + ' rs' + (x.rs | 0) + (x.byp ? 'B' : '') + (x.dead ? 'D' : '') : '-')).join(' '); if (st !== lastSt) { console.log(`block ${b} frame ${f0}: ${st}`); lastSt = st; } }
      for (let i = np; i < N.posts.length; i++) { const p = N.posts[i]; if (p.lvl || p.dyn || p.pump || p.plat || p.failed) tel.push({ b, f: f0, node: ni, m: p }); }
    });
    f0 += m; b++;
  }
  return { n, ins, taps, sends, outs, tel };
}

// pumpLatencyOf for every type: a fresh instance staged with the slot's values (no bypass of its own), as the page asks it on the main thread
function latencies() {
  const z = () => 0, X = new WebAssembly.Instance(pumpMod, { wasi_snapshot_preview1: { fd_close: z, fd_seek: z, fd_write: z, proc_exit: z } }).exports; if (X._initialize) X._initialize();
  const out = [];
  for (const type of TYPES) for (const [set, preset] of [['def', undefined], ['alt', 1]]) {
    const e = pumpEntry(type, { preset }), st = new Float32Array(X.memory.buffer, X.pump_stage(), 512); e.pv.forEach((v, i) => st[i] = v);
    const h = X.pump_new(e.pt, SR); const lat = h >= 0 ? X.pump_latency(h) : -1; if (h >= 0) X.pump_free(h);
    out.push({ type, pt: e.pt, set, lat, nparams: X.pump_nparams(e.pt), bi: e.bi, n: e.pv.length, on: true, pv: e.pv });
  }
  return out;
}

const jsonRep = (k, v) => ArrayBuffer.isView(v) ? Array.from(v) : (typeof v === 'number' && !Number.isFinite(v) ? String(v) : v);
export function main(a) {
  const golden = a[0] === '--golden' || a[0] === '--check', outDir = golden ? null : (a[0] || '/tmp/u18/pump-ref');
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const names = [];
  for (const c of CASES) {
    names.push(c.name);
    if (!outDir) continue;
    const r = runCase(c), nn = r.outs.length, all = new Float32Array(r.n * 2 * nn), inn = new Float32Array(r.n * (2 * nn + r.taps.length + r.sends.length));
    r.outs.forEach((o, i) => { all.set(o[0], i * 2 * r.n); all.set(o[1], (i * 2 + 1) * r.n); });
    r.ins.forEach((o, i) => { inn.set(o[0], i * 2 * r.n); inn.set(o[1], (i * 2 + 1) * r.n); });
    [...r.taps, ...r.sends].forEach((t, i) => inn.set(t, (2 * nn + i) * r.n));
    fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify({ name: c.name, n: r.n, sizes: c.sizes || [128], hosts: nn, chain: c.chain, chain2: c.chain2 || [], ops: c.ops || [], msgs: c.msgs || [], q: c.q || [], bp: c.bp || 'stopped', bpm: c.bpm || 120,
      tapMap: c.tapMap || [], ntaps: r.taps.length, nsends: r.sends.length, posted: r.tel }, jsonRep));
    fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), f32b(inn)); fs.writeFileSync(path.join(outDir, c.name + '.f32'), f32b(all));
  }
  const inc = ['// GENERATED by native/tools/pump-dump.mjs (--golden): the case list of the pump hosting reference. A case that is not here is not tested; test_pump fails for one whose reference file is missing.',
    'static const char* const kPumpCases[] = {', ...names.map(n => `  "${n}",`), '};', 'static const int kPumpCaseN = ' + names.length + ';', ''].join('\n');
  if (outDir) {
    fs.writeFileSync(path.join(outDir, 'lat.json'), JSON.stringify(latencies())); fs.writeFileSync(path.join(outDir, 'index.txt'), names.join('\n') + '\n');
    fs.writeFileSync(path.join(outDir, 'which.txt'), `${path.relative(ROOT, WASM) || WASM}\n${path.relative(ROOT, METAF) || METAF}\n`);
    const miss = ['lat.json', ...names.flatMap(n => [n + '.json', n + '.in.f32', n + '.f32'])].filter(n => { try { return fs.statSync(path.join(outDir, n)).size === 0; } catch (e) { return true; } });
    if (miss.length) { console.error(`pump-dump: ${miss.length} reference files missing or empty in ${outDir}: ${miss.slice(0, 5).join(', ')}`); process.exit(1); }
    console.log(`wrote ${names.length} cases to ${outDir} (pump.wasm ${WASM})`);
  }
  const incPath = path.join(ROOT, 'native/tests/golden/pump_cases.inc');
  if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
  else if (a[0] === '--check') { const cur = fs.existsSync(incPath) ? fs.readFileSync(incPath, 'utf8') : ''; if (cur !== inc) { console.error('pump_cases.inc differs from the case list now: run node native/tools/pump-dump.mjs --golden'); process.exit(1); } console.log('pump_cases.inc is current (' + names.length + ' cases)'); }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main(pos);
