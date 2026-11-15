// U11: THE MASTER PROCESSOR'S REFERENCE (exact, Node only): master-worklet.js itself, run in a Node `vm` (stub registerProcessor /
// AudioWorkletProcessor / sampleRate), on deterministic test signals: a sine sweep, pink-ish noise from a fixed LCG, a drum-like burst
// train, silence (after a burst, so the SILENCE SHORTCUT runs), a full-scale square, a tonal signal with resonances; each stage alone
// and all together at a few settings, with settings changed mid-stream (stage on / off, parameters, the limiter's look-ahead, junk values),
// and the 18 configurations of tests/golden/master.json (signals and settings taken from tests/master.test.js).
//   node native/tools/master-dump.mjs [outDir]   writes <outDir>/<case>.json (the case + every meter message), <case>.in.f32 (L then R),
//                                                <case>.f32 (the output, L then R); default /tmp/u11/master-ref
//   node native/tools/master-dump.mjs --golden   writes native/tests/golden/master_golden.inc (FNV-1a 64 of every case's output)
//   node native/tools/master-dump.mjs --check    exits 1 if the committed master_golden.inc differs
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const SR = 48000, Q = 128;
const rd = f => fs.readFileSync(path.join(ROOT, f), 'utf8').replace(/\r\n/g, '\n');
const workletSrc = rd('master-worklet.js'), testSrc = rd('tests/master.test.js'), goldenMaster = JSON.parse(rd('tests/golden/master.json'));

// ---- the processor, in a vm -------------------------------------------------------------------------------------------------------
function makeProc(settings, meters) {
  let Cls = null, posted = []; let t = 1e12;
  const ctx = vm.createContext({ console, sampleRate: SR, currentTime: 0, currentFrame: 0, Date: class extends Date { static now() { return t += 1; } },
    registerProcessor(n, c) { Cls = c; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage: m => posted.push(m), onmessage: null }; } } });
  vm.runInContext(workletSrc, ctx, { filename: 'master-worklet.js' });
  const P = new Cls({ processorOptions: { meters, set: settings } });
  return { P, posted };
}
// JSON cannot hold NaN / Infinity: the case files write them as strings and the settings are revived here (the C++ test revives them too)
const revive = o => JSON.parse(JSON.stringify(o), (k, v) => v === 'NaN' ? NaN : v === 'Infinity' ? Infinity : v === '-Infinity' ? -Infinity : v);

// ---- signals (float32, deterministic) ------------------------------------------------------------------------------------------------
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296 * 2 - 1; }; };
const SIG = {
  sweep(n) { const L = new Float32Array(n), R = new Float32Array(n); const f0 = 20, f1 = 20000, T = n / SR, k = Math.log(f1 / f0); let ph = 0;
    for (let i = 0; i < n; i++) { const f = f0 * Math.exp(k * (i / SR) / T); ph += 2 * Math.PI * f / SR; L[i] = Math.fround(0.8 * Math.sin(ph)); R[i] = Math.fround(0.7 * Math.sin(ph + 0.3)); } return [L, R]; },
  noise(n) { const r = lcg(12345), L = new Float32Array(n), R = new Float32Array(n); let b0 = 0, b1 = 0, b2 = 0, c0 = 0, c1 = 0, c2 = 0;
    for (let i = 0; i < n; i++) { const w = r(), v = r(); b0 = 0.99765 * b0 + w * 0.0990460; b1 = 0.963 * b1 + w * 0.2965164; b2 = 0.57 * b2 + w * 1.0526913;
      c0 = 0.99765 * c0 + v * 0.0990460; c1 = 0.963 * c1 + v * 0.2965164; c2 = 0.57 * c2 + v * 1.0526913; L[i] = Math.fround((b0 + b1 + b2 + w * 0.1848) * 0.22); R[i] = Math.fround((c0 + c1 + c2 + v * 0.1848) * 0.22); } return [L, R]; },
  drums(n) { const r = lcg(777), L = new Float32Array(n), R = new Float32Array(n); const beat = Math.round(SR * 0.25);
    for (let i = 0; i < n; i++) { const p = i % beat, q = i % (beat * 2), kick = q < SR * 0.15 ? Math.sin(2 * Math.PI * (55 + 120 * Math.exp(-q / 800)) * q / SR) * Math.exp(-q / 3000) * 1.3 : 0,
      hat = (p > beat / 2 && p < beat / 2 + 1500) ? r() * 0.5 * Math.exp(-(p - beat / 2) / 400) : 0; L[i] = Math.fround(kick + hat); R[i] = Math.fround(kick * 0.9 + hat * 1.1); } return [L, R]; },
  silence(n) { return [new Float32Array(n), new Float32Array(n)]; },
  square(n) { const L = new Float32Array(n), R = new Float32Array(n); for (let i = 0; i < n; i++) { L[i] = Math.floor(i / 24) % 2 ? -1 : 1; R[i] = Math.floor(i / 30) % 2 ? -1 : 1; } return [L, R]; },
  tonal(n) { const r = lcg(99), L = new Float32Array(n), R = new Float32Array(n);
    for (let i = 0; i < n; i++) { const t = i / SR, v = 0.25 * Math.sin(2 * Math.PI * 1000 * t) + 0.2 * Math.sin(2 * Math.PI * 3150 * t) + 0.12 * r() + ((i % 12000) < 400 ? 0.5 * Math.sin(2 * Math.PI * 90 * t) : 0); L[i] = Math.fround(v); R[i] = Math.fround(0.9 * v + 0.05 * r()); } return [L, R]; },
  burstThenSilence(n) { const [a, b] = SIG.tonal(n); const k = Math.round(n * 0.2); a.fill(0, k); b.fill(0, k); return [a, b]; },
  junk(n) { const [a, b] = SIG.tonal(n); a[1000] = NaN; b[2000] = Infinity; a[3000] = 100; b[4000] = -100; a[5000] = -Infinity; return [a, b]; },
};
// the golden signal of tests/master.test.js, the page's own `loud` (read from the test's text, not retyped)
const loudSrc = testSrc.match(/function loud\(n, gainDb = 0\) \{[\s\S]*?\n\}/)[0];
const loud = vm.runInContext('(function(){ const SR = 48000; ' + loudSrc + '; return loud; })()', vm.createContext({ Math, Float32Array }));
const SETTINGS = vm.runInContext('(' + testSrc.match(/const SETTINGS = (\{[\s\S]*?\n\});/)[1] + ')', vm.createContext({}));

// ---- cases -------------------------------------------------------------------------------------------------------------------------
const SUP = { on: true }, COMP = { on: true, thr: -24, ratio: 4, atk: 5, rel: 120, auto: false, knee: 6, makeup: 0, automk: false, mix: 1, hpf: 80 };
const COMPA = { on: true, thr: -20, ratio: 3, atk: 8, rel: 150, auto: true, knee: 6, makeup: 0, automk: true, mix: 1, hpf: 80 };
const LIM = { on: true, drive: 6, ceil: -1, rel: 120, look: 3, tp: true };
const CASES = [];
const add = (name, sig, secs, settings, extra = {}) => CASES.push({ name, sig, secs, settings, events: [], meters: true, ...extra });
for (const s of ['sweep', 'noise', 'drums', 'tonal']) { add('off_' + s, s, 1, {});
  add('sup_' + s, s, 2, { sup: { ...SUP, depth: 0.7, sel: 2 } }); add('comp_' + s, s, 2, { comp: COMP }); add('lim_' + s, s, 2, { lim: LIM }); }
add('all_tonal', 'tonal', 2.5, { sup: SUP, comp: COMPA, lim: { ...LIM, drive: 3 } });
add('all_drums', 'drums', 2.5, { sup: { on: true, depth: 0.9, sharp: 0.2, sel: 1, mix: 0.6, lo: 80, hi: 16000 }, comp: { ...COMPA, thr: -30, ratio: 6, mix: 0.7, hpf: 120 }, lim: { ...LIM, drive: 9, ceil: -3, look: 5, rel: 60, tp: false } });
add('all_noise', 'noise', 2, { sup: { ...SUP, delta: true }, comp: { ...COMPA, knee: 0, atk: 0.5, hpf: 0 }, lim: LIM });
add('comp_square', 'square', 1, { comp: { ...COMP, thr: -12, ratio: 20 } }); add('lim_square', 'square', 1, { lim: { ...LIM, drive: 12 } });
add('all_square', 'square', 1, { sup: SUP, comp: COMPA, lim: LIM });
add('silence_all', 'silence', 1, { sup: SUP, comp: COMPA, lim: LIM }); add('silence_nosup', 'silence', 1, { comp: COMPA, lim: LIM });
add('shortcut_comp_lim', 'burstThenSilence', 5, { comp: COMPA, lim: LIM }); add('shortcut_lim', 'burstThenSilence', 5, { lim: LIM });
add('shortcut_none', 'burstThenSilence', 3, {});
add('junk_all', 'junk', 1, { sup: SUP, comp: COMPA, lim: LIM });
add('hpf_off', 'tonal', 1, { comp: { ...COMP, hpf: 10 } }); add('lim_look1', 'tonal', 1, { lim: { ...LIM, look: 1 } }); add('lim_look10', 'tonal', 1, { lim: { ...LIM, look: 10, rel: 10 } });
// settings changed mid-stream (block numbers at 48 kHz / 128 frames)
add('mid_toggle', 'tonal', 3, { sup: SUP, comp: COMPA, lim: LIM }, { events: [
  { b: 100, set: { sup: { on: false } } }, { b: 200, set: { sup: { on: true, depth: 0.9 } } }, { b: 250, set: { comp: { on: false } } }, { b: 300, set: { comp: { ...COMPA, thr: -30 } } },
  { b: 330, set: { lim: { on: false } } }, { b: 360, set: { lim: { on: true, drive: 6, ceil: -2 } } }, { b: 380, set: { lim: { on: true, look: 5, drive: 6 } } }, { b: 390, resetPeak: true } ] });
add('mid_params', 'drums', 3, { sup: SUP, comp: COMPA, lim: LIM }, { events: [
  { b: 50, set: { sup: { on: true, sharp: 1, lo: 40, hi: 8000, atk: 2, rel: 30 } } }, { b: 120, set: { comp: { on: true, ratio: 12, atk: 1, rel: 40, mix: 0.5, makeup: 4, hpf: 200 } } },
  { b: 150, set: { lim: { on: true, rel: 20, look: 2, tp: false } } }, { b: 200, set: { sup: { on: true, delta: true } } }, { b: 260, set: { sup: { on: true, delta: false, mix: 0.3 } } } ] });
add('mid_junk', 'tonal', 2, { sup: SUP, comp: COMPA, lim: LIM }, { events: [
  { b: 40, set: { sup: { on: true, depth: 'NaN', lo: 0, hi: -5, sel: 'Infinity' }, comp: { on: true, ratio: 0, thr: 'NaN', knee: '-Infinity' }, lim: { on: true, drive: 'NaN', ceil: 'Infinity', look: 'NaN', rel: 'NaN' } } },
  { b: 60, set: { comp: { on: true, ratio: -2 } } } ] });
add('mid_stage_missing', 'tonal', 1, { sup: SUP, comp: COMPA, lim: LIM }, { events: [ { b: 20, set: { comp: { thr: -40 } } }, { b: 40, set: { lim: { drive: 3 } } }, { b: 60, set: {} } ] });
add('nometers_all', 'drums', 1, { sup: SUP, comp: COMPA, lim: LIM }, { meters: false });
// tests/golden/master.json: the 18 configurations (signal of the test, 400 blocks)
for (const [cn, set] of Object.entries(SETTINGS)) for (const g of [-12, 0, 9]) CASES.push({ name: `gold_${cn}@${g}`, loud: g, secs: 400 * Q / SR, settings: set, events: [], meters: false, goldenKey: `${cn} @${g}dB` });

const f32bytes = a => new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
export function runCase(c) {
  const n = Math.round(c.secs * SR / Q) * Q;
  let L, R; if (c.loud != null) { L = loud(n, c.loud); R = L; } else [L, R] = SIG[c.sig](n);
  const { P, posted } = makeProc(revive(c.settings), c.meters), oL = new Float32Array(n), oR = new Float32Array(n), meters = [];
  let ev = 0;
  for (let b = 0; b < n / Q; b++) {
    while (ev < c.events.length && c.events[ev].b === b) { const e = c.events[ev++]; P.port.onmessage({ data: e.set ? { set: revive(e.set) } : e.resetPeak ? { resetPeak: true } : {} }); }
    const o = [new Float32Array(Q), new Float32Array(Q)];
    P.process([[L.subarray(b * Q, b * Q + Q), R.subarray(b * Q, b * Q + Q)]], [o]); oL.set(o[0], b * Q); oR.set(o[1], b * Q);
    while (posted.length) { const m = posted.shift().meter; meters.push({ b, m: { ...m, sup: m.sup ? Array.from(m.sup) : null } }); }
  }
  return { n, L, R, oL, oR, meters };
}
export const M64 = (1n << 64n) - 1n;
export function fnv(u) { let h = 0xcbf29ce484222325n; for (let i = 0; i < u.length; i++) { h ^= BigInt(u[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const fnv32 = u => { let h = 0x811c9dc5 >>> 0; for (const b of u) { h ^= b; h = Math.imul(h, 0x01000193) >>> 0; } return h.toString(16).padStart(8, '0'); };
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';

const a = process.argv.slice(2);
if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {   // (not a string compare with 'file://': a Windows path has a drive letter and backslashes)
  const golden = a[0] === '--golden' || a[0] === '--check', outDir = golden ? null : (a[0] || '/tmp/u11/master-ref');
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = [];
  for (const c of CASES) {
    const r = runCase(c), outB = Buffer.concat([f32bytes(r.oL), f32bytes(r.oR)]);
    if (c.goldenKey) { const want = goldenMaster[c.goldenKey], got = fnv32(Buffer.concat([f32bytes(r.oL), f32bytes(r.oR)])); if (want !== got) { console.error(`${c.name}: tests/golden/master.json says ${want}, this run gives ${got}`); process.exit(1); } c.jsHash = got; }
    rows.push(`  {"${c.name}", ${hex(fnv(outB))}, ${r.n * 2}},`);
    if (outDir) {
      fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify({ name: c.name, n: r.n, settings: c.settings, events: c.events, meters: c.meters, jsHash: c.jsHash || null, goldenKey: c.goldenKey || null, posted: r.meters }, (k, v) => typeof v === 'number' && !isFinite(v) ? (isNaN(v) ? 'NaN' : v > 0 ? 'Infinity' : '-Infinity') : v));
      fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), Buffer.concat([f32bytes(r.L), f32bytes(r.R)])); fs.writeFileSync(path.join(outDir, c.name + '.f32'), outB);
    }
  }
  const inc = ['// GENERATED by native/tools/master-dump.mjs (--golden) from master-worklet.js. Do not edit: a mismatch means the port is wrong.',
    '// FNV-1a 64 over each case\'s float32 output (L then R). The gold_ cases are the 18 configurations of tests/golden/master.json (checked here against its hashes).',
    'struct MasterGolden { const char* name; unsigned long long hash; int floats; };', 'static const MasterGolden kMasterGolden[] = {', ...rows, '};', `static const int kMasterGoldenN = ${rows.length};`, ''].join('\n');
  const incPath = path.join(ROOT, 'native/tests/golden/master_golden.inc');
  if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
  else if (a[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('master_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('master_golden.inc is current'); }
  else console.log(`wrote ${CASES.length} cases to ${outDir}`);
}
