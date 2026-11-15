// U12: THE MASTER EQ'S REFERENCE (exact, Node only): the page's own eqBuild (index.html: grabbed by regex and run, with a stand-in
// `ac.createBiquadFilter()`), then a small JS MODEL of Chromium's BiquadFilterNode (third_party/blink/renderer/platform/audio/biquad.cc /
// biquad_dsp_kernel.cc, the Web Audio spec's formulas): float32 parameter values, frequency normalised by Nyquist, coefficients in doubles
// divided through a0 by multiplying 1/a0, direct form I in doubles with float32 y states, A = 10^(gain/40) held as a float, a float32 buffer between nodes. The model is checked against a real
// OfflineAudioContext in headless Chrome by native/tools/eq-webaudio-check.mjs (its result is native/tests/golden/eq-webaudio-check.txt).
//   node native/tools/eq-dump.mjs [outDir]     writes <outDir>/<case>.json (sr, eqOn, bands, the node list), <case>.in.f32 and <case>.f32 (L then R)
//   node native/tools/eq-dump.mjs --golden     writes native/tests/golden/eq_golden.inc;   --check: exits 1 if it differs
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
const grab = re => { const m = html.match(re); if (!m) throw new Error('index.html: not found: ' + re); return m[0]; };
const PAGE = [grab(/^const BUTTER = .*$/m), grab(/^function eqBuild\(ac, eq\)\{[\s\S]*?\n\}/m)].join('\n');

// the page's eqBuild, on stand-in nodes: what it would set on each BiquadFilterNode (float32, as an AudioParam stores it)
export function eqNodes(eq) {
  const make = () => { const n = { type: 'lowpass', frequency: { value: 350 }, Q: { value: 1 }, gain: { value: 0 } }; return n; };
  const ctx = vm.createContext({ Math, nodes: [] });
  vm.runInContext(PAGE + '\nglobalThis.eqBuild = eqBuild;', ctx);
  const nodes = ctx.eqBuild({ createBiquadFilter: make }, eq);
  return nodes.map(n => ({ type: n.type, f: Math.fround(n.frequency.value), Q: Math.fround(n.Q.value), g: Math.fround(n.gain.value) }));
}

// ---- Chromium's coefficients (the spec's formulas) ----------------------------------------------------------------------------------
const norm = (b0, b1, b2, a0, a1, a2) => { const i = 1 / a0; return { b0: b0 * i, b1: b1 * i, b2: b2 * i, a1: a1 * i, a2: a2 * i }; };
const clamp01 = v => v < 0 ? 0 : v > 1 ? 1 : v;
export function coef(n, sr) {
  const fr = n.f / (sr / 2), q = n.Q, gain = n.g;
  switch (n.type) {
    case 'lowpass': case 'highpass': {
      const cutoff = clamp01(fr);
      if (cutoff === 1 || cutoff === 0) return norm(n.type === 'lowpass' ? cutoff : 1 - cutoff, 0, 0, 1, 0, 0);
      const res = Math.fround(Math.pow(10, q / 20)), theta = Math.PI * cutoff, alpha = Math.sin(theta) / (2 * res), cosw = Math.cos(theta);
      if (n.type === 'lowpass') { const beta = (1 - cosw) / 2; return norm(beta, 2 * beta, beta, 1 + alpha, -2 * cosw, 1 - alpha); }
      const beta = (1 + cosw) / 2; return norm(beta, -2 * beta, beta, 1 + alpha, -2 * cosw, 1 - alpha);
    }
    case 'peaking': {
      const f = clamp01(fr), qq = Math.max(0, q), a = Math.fround(Math.pow(10, gain / 40));   // (Chromium keeps A as a float)
      if (f > 0 && f < 1) { if (qq > 0) { const w0 = Math.PI * f, alpha = Math.sin(w0) / (2 * qq), k = Math.cos(w0); return norm(1 + alpha * a, -2 * k, 1 - alpha * a, 1 + alpha / a, -2 * k, 1 - alpha / a); } return norm(a * a, 0, 0, 1, 0, 0); }
      return norm(1, 0, 0, 1, 0, 0);
    }
    case 'lowshelf': case 'highshelf': {
      const f = clamp01(fr), a = Math.fround(Math.pow(10, gain / 40)), lo = n.type === 'lowshelf';
      if (f === 1) return lo ? norm(a * a, 0, 0, 1, 0, 0) : norm(1, 0, 0, 1, 0, 0);
      if (f > 0) {
        const w0 = Math.PI * f, s = 1, alpha = 0.5 * Math.sin(w0) * Math.sqrt((a + 1 / a) * (1 / s - 1) + 2), k = Math.cos(w0), k2 = 2 * Math.sqrt(a) * alpha, ap1 = a + 1, am1 = a - 1;
        return lo ? norm(a * (ap1 - am1 * k + k2), 2 * a * (am1 - ap1 * k), a * (ap1 - am1 * k - k2), ap1 + am1 * k + k2, -2 * (am1 + ap1 * k), ap1 + am1 * k - k2)
                  : norm(a * (ap1 + am1 * k + k2), -2 * a * (am1 + ap1 * k), a * (ap1 + am1 * k - k2), ap1 - am1 * k + k2, 2 * (am1 - ap1 * k), ap1 - am1 * k - k2);
      }
      return lo ? norm(1, 0, 0, 1, 0, 0) : norm(a * a, 0, 0, 1, 0, 0);
    }
  }
  throw new Error('type ' + n.type);
}
// the chain on one channel: each node a float32 buffer out
export function modelRun(nodes, sr, x) {
  let cur = Float32Array.from(x);
  for (const n of nodes) {
    const c = coef(n, sr), out = new Float32Array(cur.length); let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < cur.length; i++) { const xi = cur[i], y = c.b0 * xi + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2; out[i] = y; x2 = x1; x1 = xi; y2 = y1; y1 = Math.fround(y); }   // (Chromium feeds the float32 output back: the state is a float)
    cur = out;
  }
  return cur;
}

// ---- normMst: the page's own (a song's `mst` filled in and clamped), for the C++ master_chain_from_song ---------------------------------
export const NORM_CASES = [
  null, {}, { on: true }, { on: 1, sup: { on: true, depth: 5, sharp: -1, lo: '300', hi: null, delta: true, mix: 'x' }, comp: { on: false, thr: -99, ratio: 100, auto: false, hpf: 1000, makeup: '3' }, lim: { on: false, drive: 40, ceil: 3, look: 0, rel: 5, tp: false } },
  { on: true, eq: { on: false } }, { on: true, eq: { bands: [] } },
  { on: true, eq: { on: true, bands: [{ type: 'bell', f: 5, g: 40, q: 0 }, { type: 'bogus', f: 1000 }, { type: 'lowcut', f: 99999, g: -99, q: 99, slope: 48 }, { type: 'highcut', f: '800', slope: 7, on: false }, null, { type: 'lowshelf', f: 0, g: 0, q: 0, on: true },
    { type: 'highshelf', f: 3000, g: 2 }, { type: 'bell' }, { type: 'bell', f: 1, on: 0 }, { type: 'bell', f: 2000 }] } },
  { on: 'yes', sup: { on: 'true' }, comp: { thr: 'abc' }, lim: 5 }, { sup: 3, comp: [], eq: 5 },
];
function normMstPage() {
  const ctx = vm.createContext({ Math, Number, Array });
  vm.runInContext('const clamp = (v, a, b) => v < a ? a : v > b ? b : v;\n' + [grab(/^const MST_TYPES = .*$/m), grab(/^const freshBand = .*$/m), grab(/^const freshMst = \(\) => \(\{[\s\S]*?\n\s*lim:\{[^\n]*\}\}\);/m), grab(/^const MST_RANGES = [\s\S]*?\};/m), grab(/^function normMst\(j\)\{[\s\S]*?\n\}/m)].join('\n') + '\nglobalThis.normMst = normMst;', ctx);
  return ctx.normMst;
}

// ---- cases -----------------------------------------------------------------------------------------------------------------------------
const B = (type, f, g = 0, q = 1, slope = 24, on = true) => ({ type, f, g, q, slope, on });
const DEFAULT = () => [B('lowcut', 28, 0, .71, 24), B('lowshelf', 110, 0, .7), B('bell', 350, 0, 1.2), B('bell', 2500, 0, 1.2), B('highshelf', 9000, 0, .7), B('highcut', 19000, 0, .71, 12, false)];
export const SHAPED = () => [B('lowcut', 28, 0, .71, 24), B('lowshelf', 110, 2, .7), B('bell', 350, -3, 1.2), B('bell', 2500, 2, 1.2), B('highshelf', 9000, 1.5, .7), B('highcut', 19000, 0, .71, 12, true)];
export const CASES = [];
const add = (name, bands, sr = 48000, eqOn = true) => CASES.push({ name, sr, eqOn, bands });
add('default_flat', DEFAULT()); add('shaped', SHAPED()); add('shaped_44k', SHAPED(), 44100); add('shaped_96k', SHAPED(), 96000); add('eq_off', SHAPED(), 48000, false);
for (const s of [12, 24, 48]) { add('lowcut' + s, [B('lowcut', 200, 0, 1, s)]); add('highcut' + s, [B('highcut', 8000, 0, 1, s)]); }
add('lowcut_20', [B('lowcut', 20, 0, 1, 48)]); add('highcut_20k', [B('highcut', 20000, 0, 1, 48)]);
add('lowshelf_up', [B('lowshelf', 200, 9)]); add('lowshelf_dn', [B('lowshelf', 5000, -12)]); add('highshelf_up', [B('highshelf', 3000, 12)]); add('highshelf_dn', [B('highshelf', 200, -18)]);
add('lowshelf_20', [B('lowshelf', 20, 18)]); add('highshelf_20k', [B('highshelf', 20000, -18)]);
for (const q of [0.1, 1, 18]) for (const [f, g] of [[20, 18], [1000, -18], [19999, 12]]) add(`bell_q${q}_f${f}`, [B('bell', f, g, q)]);
add('eight_bands', [B('lowcut', 40, 0, 1, 48), B('lowshelf', 90, 3, .7), B('bell', 250, -4, 2), B('bell', 800, 3, .5), B('bell', 3000, -2, 6), B('highshelf', 8000, -3, .7), B('highcut', 16000, 0, 1, 24), B('bell', 12000, 5, 3)]);
add('off_bands', [B('bell', 1000, 12, 1, 24, false), B('lowcut', 100, 0, 1, 24, false)]); add('no_bands', []);
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296 * 2 - 1; }; };
export const N = 65536;
export function signal() {   // noise (the first 40000 samples), then silence with an impulse at 50000; R a little different
  const r = lcg(2024), L = new Float32Array(N), R = new Float32Array(N);
  for (let i = 0; i < 40000; i++) { L[i] = Math.fround(0.5 * r()); R[i] = Math.fround(0.4 * r()); }
  L[50000] = 1; R[50000] = -0.5; return [L, R];
}
const f32bytes = a => new Uint8Array(a.buffer, a.byteOffset, a.byteLength);
export function runCase(c) {
  const [L, R] = signal(), nodes = c.eqOn ? eqNodes({ on: true, bands: c.bands }) : [];
  return { L, R, nodes, oL: modelRun(nodes, c.sr, L), oR: modelRun(nodes, c.sr, R) };
}
const M64 = (1n << 64n) - 1n;
function fnv(u) { let h = 0xcbf29ce484222325n; for (let i = 0; i < u.length; i++) { h ^= BigInt(u[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {   // (not a string compare with 'file://': a Windows path has a drive letter and backslashes)
  const a = process.argv.slice(2), golden = a[0] === '--golden' || a[0] === '--check', outDir = golden ? null : (a[0] || '/tmp/u11/eq-ref');
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = [];
  for (const c of CASES) {
    const r = runCase(c), outB = Buffer.concat([f32bytes(r.oL), f32bytes(r.oR)]); rows.push(`  {"${c.name}", ${hex(fnv(outB))}, ${2 * N}},`);
    if (outDir) { fs.writeFileSync(path.join(outDir, c.name + '.json'), JSON.stringify({ name: c.name, sr: c.sr, eqOn: c.eqOn, bands: c.bands, nodes: r.nodes })); fs.writeFileSync(path.join(outDir, c.name + '.in.f32'), Buffer.concat([f32bytes(r.L), f32bytes(r.R)])); fs.writeFileSync(path.join(outDir, c.name + '.f32'), outB); }
  }
  if (outDir) { const nm = normMstPage(); fs.writeFileSync(path.join(outDir, 'normmst.json'), JSON.stringify(NORM_CASES.map(raw => ({ raw, want: JSON.parse(JSON.stringify(nm(raw === null ? null : JSON.parse(JSON.stringify(raw))))) })))); }
  const inc = ['// GENERATED by native/tools/eq-dump.mjs (--golden): the JS model of Chromium\'s BiquadFilterNode chain. Do not edit: a mismatch means the port is wrong.',
    'struct EqGolden { const char* name; unsigned long long hash; int floats; };', 'static const EqGolden kEqGolden[] = {', ...rows, '};', `static const int kEqGoldenN = ${rows.length};`, ''].join('\n');
  const incPath = path.join(ROOT, 'native/tests/golden/eq_golden.inc');
  if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
  else if (a[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('eq_golden.inc differs from what the model gives now'); process.exit(1); } console.log('eq_golden.inc is current'); }
  else console.log(`wrote ${CASES.length} cases to ${outDir}`);
}
