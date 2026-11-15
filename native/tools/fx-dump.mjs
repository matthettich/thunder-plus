// U14: THE TRACK-EFFECT HOST'S REFERENCE (exact, Node only): the real ThunderFx worklet (the TFX_SRC template of index.html), run in
// Node with the committed tfx.wasm, on deterministic stereo signals (a fixed LCG): each of the 34 wasm effects alone at its default
// settings and at one non-default setting, then host behaviour: slots in series, a slot switched off and on (the 5 ms fade), FXO / FLX /
// GAT / RTG commands, fxreset, FXV (`pset`) mid-block, the node resting and sleeping, block sizes, telemetry, junk samples.
//   node native/tools/fx-dump.mjs [outDir]   writes <outDir>/<case>.json (the case + what the worklet posted), <case>.in.f32 (L then R),
//                                            <case>.f32 (the output, L then R), maps.json (TFX map() on 200 value sets per effect); default /tmp/u14/fx-ref
//   node native/tools/fx-dump.mjs --golden   writes native/tests/golden/fx_golden.inc (FNV-1a 64 of every case's output)
//   node native/tools/fx-dump.mjs --check    exits 1 if the committed fx_golden.inc differs
// HOW THE COMMANDS REACH THE WORKLET (the same as the audio thread's): FXO / FLX / GAT / RTG are k-rate AudioParams the worklet reads at
// index 0, so a command at time t acts from the first block that starts at or after t; the timelines are the core's own `Auto` (cut out of
// core/seq-core.js) read at the block start, as __tcOv does. FXV is `pset` in the core's queue (`node.__q`): applied by the first block
// whose start is within 64 samples of its time.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { ROOT, SR, load, effects } from './fx-src.mjs';

const rd = f => fs.readFileSync(path.join(ROOT, f), 'utf8').replace(/\r\n/g, '\n');
const F = load(), E = effects();
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };

// ---- the core's Auto (timelines of the k-rate params) ------------------------------------------------------------------------------------
const seqSrc = rd('core/seq-core.js'), a0 = seqSrc.indexOf('\nclass Auto {');
const autoCtx = vm.createContext({ Math }); vm.runInContext(seqSrc.slice(a0, seqSrc.indexOf('\n}\n', a0) + 3) + '\nglobalThis.Auto = Auto;', autoCtx);
const Auto = autoCtx.Auto;

// ---- the worklet ------------------------------------------------------------------------------------------------------------------------
function mkNode(chain) {
  const reg = {}, clk = {}, posts = [];
  const fn = new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', '__clk', 'let currentTime = 0; __clk.set = v => { currentTime = v; };\n' + F.workletSrc);
  fn(class { constructor() { this.port = { postMessage: m => posts.push(m), onmessage: null }; } }, (n, c) => { reg[n] = c; }, SR, clk);
  const node = new reg['thunder-tfx']({ processorOptions: { bytes: new Uint8Array(F.tfxWasm), chain } });
  return { node, clk, posts };
}

// ---- signals (float32) --------------------------------------------------------------------------------------------------------------------
const NF = 32768;
const SIG = {
  noiseTail(n) { const r = lcg(12345), L = new Float32Array(n), R = new Float32Array(n), k = 12000; for (let i = 0; i < k; i++) { L[i] = Math.fround((r() * 2 - 1) * 0.3); R[i] = Math.fround((r() * 2 - 1) * 0.3); } return [L, R]; },
  drumsweep(n) { const r = lcg(777), L = new Float32Array(n), R = new Float32Array(n), beat = 6000; let ph = 0;
    for (let i = 0; i < n; i++) { let l = 0, rr = 0;
      if (i < 12000) { const p = i % beat, kick = p < 4000 ? Math.sin(2 * Math.PI * (55 + 120 * Math.exp(-p / 800)) * p / SR) * Math.exp(-p / 1500) * 0.9 : 0, hat = p > 3000 && p < 4500 ? (r() * 2 - 1) * 0.4 * Math.exp(-(p - 3000) / 300) : 0; l = kick + hat; rr = kick * 0.9 + hat * 1.1; }
      else if (i < 20000) { const f = 100 * Math.pow(100, (i - 12000) / 8000); ph += 2 * Math.PI * f / SR; l = 0.5 * Math.sin(ph); rr = 0.45 * Math.sin(ph + 0.3); }
      L[i] = Math.fround(l); R[i] = Math.fround(rr); }
    return [L, R]; },
  junk(n) { const [a, b] = SIG.noiseTail(n); a[1000] = NaN; b[2000] = Infinity; a[3000] = 100; b[4000] = -100; a[5000] = -Infinity; a[6000] = 9; b[6001] = -30; return [a, b]; },
  long(n) { const [a, b] = SIG.noiseTail(n); const r = lcg(5); for (let i = n - 9000; i < n; i++) { a[i] = Math.fround((r() * 2 - 1) * 0.2); b[i] = Math.fround((r() * 2 - 1) * 0.2); } return [a, b]; },
};

// ---- chains (the page's own normTfx + tfxChain) ----------------------------------------------------------------------------------------
const byKey = Object.fromEntries(E.map(e => [e.key, e]));
function slotObj(key, alt, on = true, seed = 1) {
  const o = { type: key, on }; if (alt) { const r = lcg(seed * 7919 + byKey[key].id); for (const p of byKey[key].ps) o[p.k] = p.min + Math.floor(r() * (p.max - p.min + 1)); }
  return o;
}
function chainOf(objs, bpm = 120) { const A = new Array(8).fill(null); objs.forEach((o, i) => { A[i] = o && F.normTfx(o); }); return F.tfxChain(A, SR, bpm); }

// ---- cases ------------------------------------------------------------------------------------------------------------------------------
// {name, sig, frames, sizes, chain, ops: [[param, t, v]] (param 'sw0'..'sw7','flx','gat','rtg','reset'), msgs: [{b, msg}], q: [[at, msg]]}
const CASES = [];
for (const e of E) {
  CASES.push({ name: `e_${e.key}_def`, sig: 'noiseTail', chain: chainOf([slotObj(e.key, false)]) });
  CASES.push({ name: `e_${e.key}_alt`, sig: 'drumsweep', chain: chainOf([slotObj(e.key, true)]) });
}
const S8 = ['eq', 'comp', 'density', 'tape', 'chorus', 'flanger', 'delay', 'reverb'];
CASES.push({ name: 'h_series2', sig: 'noiseTail', chain: chainOf([slotObj('density', true, true, 3), slotObj('eq', true, true, 4)]) });
CASES.push({ name: 'h_series2_drums', sig: 'drumsweep', chain: chainOf([slotObj('tape', false), slotObj('delay', true, true, 5)]) });
CASES.push({ name: 'h_series8', sig: 'drumsweep', chain: chainOf(S8.map((k, i) => slotObj(k, i % 2 === 1, true, i + 1))) });
// a slot switched off and on mid-stream: a new chain (set) at block 60 and 140 (the 5 ms fade), another by FXO
{ const on = [true, true], off = [false, true];
  const mk = o => chainOf([slotObj('delay', true, o[0], 2), slotObj('chorus', true, o[1], 3)]);
  CASES.push({ name: 'h_onoff', sig: 'drumsweep', chain: mk(on), msgs: [{ b: 60, msg: mk(off) }, { b: 140, msg: mk(on) }, { b: 141, msg: mk([true, false]) }, { b: 200, msg: mk(on) }] }); }
// FXO / FLX / GAT / RTG at exact times (not on block starts: frame 4802, 12001, ...)
{ const t = f => f / SR, ch = chainOf([slotObj('chorus', true, false, 2), slotObj('tape', true, true, 3), slotObj('delay', true, true, 4)]);
  CASES.push({ name: 'h_fxo', sig: 'drumsweep', chain: ch, ops: [['sw0', t(4802), 1], ['sw1', t(9600), 0], ['sw0', t(16001), 0], ['sw2', t(20000), 0], ['sw0', t(24000.5), -1], ['sw1', t(26000), -1], ['sw2', t(30000), 1], ['sw5', t(3), 1]] });
  CASES.push({ name: 'h_flxgatrtg', sig: 'drumsweep', chain: ch, ops: [['flx', t(1000), 3], ['gat', t(5000), 4], ['rtg', t(9000), 17], ['flx', t(14000), 300], ['gat', t(15000), 16], ['rtg', t(20000), -1], ['sw1', t(12000), 0]] });
  CASES.push({ name: 'h_reset', sig: 'drumsweep', chain: ch, ops: [['sw0', t(3000), 1], ['sw1', t(5000), 0], ['sw2', t(8000), 0], ['reset', t(12001), -1], ['sw1', t(16000), 0], ['sw0', t(25000), 1], ['reset', t(25001), -1]] }); }
// FXV (pset, the core's queue) and a parameter moved by a new chain: mid-block times; a slot that does not exist; a reverb (REVERB MIX)
{ const ch = chainOf([slotObj('eq', true, true, 1), slotObj('reverb', true, true, 2), slotObj('delay', true, true, 3)]), pv = (slot, key, over, bpm = 120) => { const o = slotObj(key, true, true, slot + 1); Object.assign(o, over); const c = chainOf([o])[0]; return { pset: { slot, p: c.p } }; };
  CASES.push({ name: 'h_pset', sig: 'drumsweep', chain: ch, q: [[100 / SR + 0.0000001, pv(0, 'eq', { treble: 250, bass: 3 })], [5000 / SR, pv(1, 'reverb', { mix: 255, big: 10 })], [9100.3 / SR, pv(1, 'reverb', { mix: 0 })], [9200 / SR, pv(5, 'eq', {})], [14000 / SR, pv(2, 'delay', { regen: 250, steps: 2 })], [21500.5 / SR, pv(0, 'eq', { treble: 0 })]] });
  CASES.push({ name: 'h_pset_odd', sig: 'drumsweep', sizes: [100], chain: ch, q: [[333 / SR, pv(0, 'eq', { treble: 250 })], [5001 / SR, pv(2, 'delay', { regen: 0 })]] }); }
// the node rests (a muted track) and wakes; sleeps in silence; sleeps again after a long silence, wakes with sound
CASES.push({ name: 'h_rest', sig: 'long', frames: 98304, chain: chainOf([slotObj('delay', true, true, 2), slotObj('reverb', true, true, 3)]), msgs: [{ b: 100, msg: { rest: true } }, { b: 400, msg: { rest: false } }] });
CASES.push({ name: 'h_sleep_long', sig: 'long', frames: 33 * SR + 9000, chain: chainOf([slotObj('delay', true, true, 2), slotObj('chorus', false)]) });
// block sizes
for (const n of [128, 100, 37, 1, 64, 200, 256, 4096, 5000]) CASES.push({ name: `h_blk${n}`, sig: 'drumsweep', frames: 24000, sizes: [n], chain: chainOf([slotObj('eq', true, true, 1), slotObj('delay', true, true, 2), slotObj('chorus', true, true, 3)]), ops: [['sw1', 4000 / SR, 0], ['sw1', 12000 / SR, -1]] });
CASES.push({ name: 'h_blk_mixed', sig: 'drumsweep', frames: 24000, sizes: [128, 7, 130, 1, 99, 128, 4097, 300], chain: chainOf([slotObj('eq', true, true, 1), slotObj('delay', true, true, 2)]) });
// telemetry: slot levels (lvl) and the dynamics slots (tel)
CASES.push({ name: 'h_tel', sig: 'drumsweep', chain: chainOf([slotObj('comp', true, true, 1), slotObj('transient', true, true, 2), slotObj('ott', true, true, 3), slotObj('eq', false, true)]), msgs: [{ b: 0, msg: { lvl: true } }, { b: 0, msg: { tel: true } }, { b: 200, msg: { tel: false } }] });
CASES.push({ name: 'h_hot', sig: 'junk', chain: chainOf([slotObj('density', true, true, 1), slotObj('comp', false), slotObj('crush', true, true, 2)]) });

// ---- the run ----------------------------------------------------------------------------------------------------------------------------
const f32b = a => Buffer.from(a.buffer, a.byteOffset, a.byteLength);
function runCase(c) {
  const n = c.frames || NF, [iL, iR] = SIG[c.sig](n), { node, clk, posts } = mkNode(c.chain);
  const lanes = {}; for (const k of ['sw0', 'sw1', 'sw2', 'sw3', 'sw4', 'sw5', 'sw6', 'sw7', 'flx', 'gat', 'rtg']) lanes[k] = new Auto(-1);
  for (const [k, t, v] of c.ops || []) { if (k === 'reset') { for (const L of Object.values(lanes)) { L.cancel(t); L.set(-1, t); } } else lanes[k].set(v, t); }
  for (const [at, msg] of c.q || []) { const q = node.__q || (node.__q = []); let i = q.length; while (i > 0 && q[i - 1][0] > at) i--; q.splice(i, 0, [at, msg]); }
  const oL = new Float32Array(n), oR = new Float32Array(n), sizes = c.sizes || [128], tel = []; let f0 = 0, b = 0, si = 0;
  const msgs = c.msgs || [];
  while (f0 < n) {
    const m = Math.min(sizes[si++ % sizes.length], n - f0), t0 = f0 / SR; clk.set(t0);
    for (const e of msgs) if (e.b === b) node.port.onmessage({ data: e.msg });
    const P = { bp: new Float32Array(m).fill(-1) };
    for (const k in lanes) { lanes[k].prune(t0); P[k] = Float32Array.of(Math.fround(lanes[k].at(t0))); }
    const a = iL.slice(f0, f0 + m), a2 = iR.slice(f0, f0 + m), o = [new Float32Array(m), new Float32Array(m)], np = posts.length;
    node.process([[a, a2], [], []], [o], P);
    oL.set(o[0], f0); oR.set(o[1], f0);
    for (let i = np; i < posts.length; i++) { const p = posts[i]; if (p.lvl || p.dyn || p.failed) tel.push({ b, f: f0, m: p }); }
    f0 += m; b++;
  }
  return { n, iL, iR, oL, oR, tel };
}
const M64 = 0xffffffffffffffffn;
export function fnv(u) { let h = 0xcbf29ce484222325n; for (let i = 0; i < u.length; i++) { h ^= BigInt(u[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';

// ---- map() value sets ---------------------------------------------------------------------------------------------------------------------
function mapSets() {
  const out = [], r = lcg(2024);
  for (const e of E) {
    const sets = [];
    for (let i = 0; i < 200; i++) {
      const o = { type: e.key, on: i % 13 !== 12 }; for (const p of e.ps) o[p.k] = i === 0 ? p.d : i === 1 ? p.min : i === 2 ? p.max : p.min + Math.floor(r() * (p.max - p.min + 1));
      if (i % 11 === 10) e.ps.forEach((p, j) => { const m = (i + j) % 5; if (m === 0) o[p.k] = p.max + 40; else if (m === 1) o[p.k] = String(o[p.k]); else if (m === 2) delete o[p.k]; else if (m === 3) o[p.k] = -5.7; else o[p.k] = p.min + 0.4; });
      const sr = i % 5 === 4 ? 44100 : SR, bpm = [120, 87.5, 140, 33, 999][i % 5], rn = i % 7;
      F.setRestartN(rn);
      const norm = F.normTfx(o), ch = F.tfxChain([norm], sr, bpm)[0];
      sets.push({ obj: o, on: ch.on, key: ch.key, v: e.ps.map(p => norm[p.k]), sr, bpm, restartN: rn, p: ch.p });
    }
    out.push({ key: e.key, id: e.id, sets });
  }
  F.setRestartN(0); return out;
}

export function main(argv) {
  const golden = argv[0] === '--golden' || argv[0] === '--check', outDir = golden ? null : (argv[0] || '/tmp/u14/fx-ref');
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = [];
  for (const c of CASES) {
    const r = runCase(c), all = new Float32Array(r.n * 2); all.set(r.oL, 0); all.set(r.oR, r.n);
    rows.push(`  {"${c.name}", ${hex(fnv(new Uint8Array(f32b(all))))}, ${r.n * 2}},`);
    if (outDir) {
      const inn = new Float32Array(r.n * 2); inn.set(r.iL, 0); inn.set(r.iR, r.n);
      fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify({ name: c.name, n: r.n, sizes: c.sizes || [128], chain: c.chain, ops: c.ops || [], msgs: c.msgs || [], q: c.q || [], posted: r.tel }));
      fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), f32b(inn)); fs.writeFileSync(path.join(outDir, c.name + '.f32'), f32b(all));
    }
  }
  if (outDir) { fs.writeFileSync(path.join(outDir, 'maps.json'), JSON.stringify(mapSets())); console.log(`wrote ${CASES.length} cases to ${outDir}`); }
  const inc = ['// GENERATED by native/tools/fx-dump.mjs (--golden) from the ThunderFx worklet and tfx.wasm. Do not edit: a mismatch means the port is wrong.',
    '// FNV-1a 64 over each case\'s float32 output (L then R), the real ThunderFx run in Node with chains/dsp/tfx.wasm.',
    'struct FxGolden { const char* name; unsigned long long hash; int floats; };', 'static const FxGolden kFxGolden[] = {', ...rows, '};', 'static const int kFxGoldenN = ' + rows.length + ';', ''].join('\n');
  const incPath = path.join(ROOT, 'native/tests/golden/fx_golden.inc');
  if (argv[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
  else if (argv[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('fx_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('fx_golden.inc is current (' + rows.length + ' cases)'); }
}
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
