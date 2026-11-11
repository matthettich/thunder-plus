// OFFLINE COST OF THE CORE (11 Nov 2026): replays the messages a real page sent to the thunder-core worklet (captured by
// `core-profile.mjs --do=capture`: the song, the kit, 24 pad buffers, PLAY) into core/seq-core.js running in a Node vm, then times
// ThunderCore.process() per 128-frame block with process.hrtime, for: idle (loaded, not playing), playing (the song), playing with
// the core's sample voices off (dbg noSampler: it sequences only), and the per-block hooks the live-engine / effect processors call
// (__tcOv / __tcPost, which the page's other worklets run on the same thread when the core drives a node), on fake nodes with the
// same ids the live core had. Budget: 128 / 44100 = 2902 us (the headless Chrome here runs at 44.1 kHz).
//   node tests/sweep/core-offline-bench.mjs [--blocks=6000] [--sr=44100] [--capture=tests/sweep/out/cp-capture.json]
import fs from 'fs'; import vm from 'vm'; import path from 'path'; import { fileURLToPath } from 'url';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const HASH = process.argv.includes('--hash'), BLOCKS = +arg('blocks', 6000), SR = +arg('sr', 44100), CAP = arg('capture', path.join(ROOT, 'tests/sweep/out/cp-capture.json')), NT = 32, N = 128, BUDGET = N / SR * 1e6;
const cap = JSON.parse(fs.readFileSync(CAP, 'utf8')), src = fs.readFileSync(arg('src', path.join(ROOT, 'core/seq-core.js')), 'utf8');   // --src=<a changed copy of the core to try>: the app's file is never edited
const dec = (k, v) => v && v.__ta ? new (globalThis[v.__ta])(Uint8Array.from(Buffer.from(v.b, 'base64')).buffer) : v;
const msgs = cap.msgs.map(x => ({ t: x.t, m: typeof x.m === 'string' ? JSON.parse(x.m, dec) : JSON.parse(JSON.stringify(x.m), dec) }));
const playMsg = msgs.find(x => x.m.op === 'play').m, setup = msgs.filter(x => ['load', 'kit', 'vars', 'buf', 'lxp', 'lead', 'sf', 'vbuf'].includes(x.m.op));   // (all of them, also the pad buffers that arrive just after PLAY)
function make(opts = {}) {
  let time = 0; const posted = []; class AWP { constructor() { this.port = { postMessage(m) { posted.push(m); }, onmessage: null }; } }
  const reg = {}, sb = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Int16Array, Uint16Array, Int8Array, ArrayBuffer, Map, Set, WeakMap, JSON, Date, Number, String, Object, Array, Error, Symbol, Promise, isFinite, isNaN, parseInt, parseFloat, WebAssembly,
    AudioWorkletProcessor: AWP, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: SR, currentFrame: 0 };
  Object.defineProperty(sb, 'currentTime', { get: () => time }); sb.globalThis = sb; sb.self = sb; vm.createContext(sb); vm.runInContext(src, sb, { filename: 'seq-core.js' });
  const proc = new reg['thunder-core']({});
  return { proc, sb, posted, setTime: t => { time = t; sb.currentFrame = Math.round(t * SR); } };
}
const mkOut = () => { const o = []; for (let i = 0; i < NT + 5; i++) { const ch = i < NT ? 2 : i === NT ? 1 : i === NT + 1 || i === NT + 2 ? NT : 1; const a = []; for (let c = 0; c < ch; c++) a.push(new Float32Array(N)); o.push(a); } return o; };
const F64 = new Float64Array(1), I32 = new Int32Array(F64.buffer);   // (for hashing the override arrays by value: a length-1 array stands for 128 equal values)
const q = (a, p) => a[Math.min(a.length - 1, Math.floor(a.length * p))], mean = a => a.reduce((x, y) => x + y, 0) / a.length;
function run(label, { play, noSampler, hooks }) {
  const E = make(), { proc, sb, posted } = E, t0 = playMsg.t0; let t = t0 - 0.5; E.setTime(t);
  const send = m => proc.port.onmessage({ data: m }); for (const s of setup) send(s.m);
  if (noSampler) send({ op: 'dbg', noSampler: true });
  // fake live nodes with the captured ids; the real processors call the hooks below when their __ov is set
  const nodes = new Map(); sb.__tcNodes = nodes; for (const id of (cap.stats && cap.stats.nodes) || []) nodes.set(id, { id, port: { postMessage() {} }, __cid: id });
  if (play) send(Object.assign({}, playMsg, { t0 }));
  const out = mkOut(), tm = [], th = [], ins = [[]], P = {}, O = [new Float32Array(N), new Float32Array(N)];
  let rows = 0, maxLead = 0, hOv = 0, hPost = 0, hv = 2166136261 | 0, hO = 2166136261 | 0, hQ = 2166136261 | 0; const spikes = [];
  for (let b = 0; b < BLOCKS + 1500; b++) {
    for (const o of out) for (const c of o) c.fill(0); t = t0 - 0.5 + b * N / SR; E.setTime(t);
    if (b % 150 === 0) send({ op: 'ack' });
    const a = process.hrtime.bigint(); proc.process(ins, out); const m = process.hrtime.bigint();
    let hk = 0n; if (hooks) { for (const p of nodes.values()) { if (!p.__ov) continue; for (const k of p.__ov.p.keys()) P[k] = P[k] || [0.5]; O[0].fill(0.1); O[1].fill(0.1); const x = process.hrtime.bigint(); const Q = sb.__tcOv(p, P, N); const y = process.hrtime.bigint(); sb.__tcPost(p, O); const z = process.hrtime.bigint(); hk += z - x; hOv += Number(y - x); hPost += Number(z - y); if (HASH && b >= 1500) { for (const c of O) { const u = new Int32Array(c.buffer, c.byteOffset, c.length); for (let i = 0; i < u.length; i++) hO = Math.imul(hO ^ u[i], 16777619); } for (const k in Q) { const v = Q[k]; for (let i = 0; i < N; i++) { F64[0] = v.length > 1 ? v[i] : v[0]; hQ = Math.imul(hQ ^ I32[0], 16777619); hQ = Math.imul(hQ ^ I32[1], 16777619); } } } } }
    if (HASH && b >= 1500) { for (const o of out) for (const c of o) { const u = new Int32Array(c.buffer, c.byteOffset, c.length); for (let i = 0; i < u.length; i++) hv = Math.imul(hv ^ u[i], 16777619); } }
    if (b >= 1500) { tm.push(Number(m - a) / 1000); th.push(Number(hk) / 1000); if (Number(m - a) / 1000 > 1000) { const lr = posted.filter(x => x.op === 'rows').slice(-1)[0], ev = lr ? lr.rows.flatMap(r => r.evs.map(e => e.type)).reduce((o, k) => (o[k] = (o[k] || 0) + 1, o), {}) : null, ps = lr && lr.rows[0] && lr.rows[0].pos ? lr.rows[0].pos.filter(Boolean).map(p => p.row + ':' + p.step).slice(0, 3) : null; spikes.push([b - 1500, +(Number(m - a) / 1000).toFixed(0), posted.length > 0 && posted[posted.length - 1].op === 'rows' ? 'rows now' : '', JSON.stringify(ev), ps && ps.join(',')]); } }
  }
  if (hooks) console.log(`   hooks split per block: __tcOv ${(hOv / (BLOCKS + 1500) / 1000).toFixed(1)} us, __tcPost ${(hPost / (BLOCKS + 1500) / 1000).toFixed(1)} us (over all blocks incl. warm-up); process() blocks over 1 ms: ${JSON.stringify(spikes.slice(0, 8))}`);
  for (const p of posted) if (p.op === 'rows') rows += p.rows.length;
  const s = tm.slice().sort((x, y) => x - y), tot = tm.map((x, i) => x + th[i]).sort((x, y) => x - y), hs = th.slice().sort((x, y) => x - y);
  console.log(`${label.padEnd(44)} process(): mean ${mean(tm).toFixed(1).padStart(6)} p50 ${q(s, .5).toFixed(1).padStart(6)} p99 ${q(s, .99).toFixed(1).padStart(6)} max ${s[s.length - 1].toFixed(0).padStart(6)} us (${(mean(tm) / BUDGET * 100).toFixed(1)}% of ${BUDGET.toFixed(0)} us)` +
    (hooks ? `   hooks mean ${mean(th).toFixed(1)} p99 ${q(hs, .99).toFixed(1)} us (${[...nodes.values()].filter(p => p.__ov).length} nodes driven); total mean ${mean(tot).toFixed(1)} p99 ${q(tot, .99).toFixed(1)}` : '') + `   rows posted ${rows}, voices at end ${proc.alive.size}` + (HASH ? `   hash voices ${(hv >>> 0).toString(16)} hooks-out ${(hO >>> 0).toString(16)} hooks-params ${(hQ >>> 0).toString(16)}` : ''));
  return { mean: mean(tm), hooks: mean(th) };
}
console.log(`song ${cap.song}, ${BLOCKS} blocks measured after 1500 warm-up blocks, sr ${SR}, budget ${BUDGET.toFixed(0)} us; ${setup.length} setup messages (${setup.filter(x => x.m.op === 'buf').length} pad buffers)`);
run('core idle (song loaded, not playing)', { play: false });
run('core playing, sample voices off (sequencing)', { play: true, noSampler: true, hooks: true });
run('core playing (sequencing + sample voices)', { play: true, hooks: true });
run('core idle, again (JIT / order check)', { play: false });
