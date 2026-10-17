// Render pad voices offline in Node with the app's own ENGINE block, as 16-bit WAVs, and check them.
// usage: node tools/drums/render.mjs index.html kits|<voices.js> <outdir> [filter]
//   kits      every drum kit voice (drumKitGroups() in index.html: the classic machines and the acoustic kits)
//   voices.js a file holding one function that returns [[group, [[name, () => voice], ...]], ...]
//             (mkVoice, rA, cA, PD, PFX and factoryKit are in scope)
// Prints each voice's length, peak, RMS over the first 100 ms and render time, and flags NaNs and any
// parameter outside the range its slider allows (RANGE).
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';

const [,, htmlPath, voicesPath, outDir, filter] = process.argv;
const html = fs.readFileSync(htmlPath, 'utf8');
const A = html.indexOf('/* ===ENGINE START=== */'), B = html.indexOf('/* ===ENGINE END=== */');
if (A < 0 || B < 0) throw new Error('engine markers not found');
const engine = html.slice(A, B);
const helpers = `
const PFX = (t, o) => Object.assign(freshFx(t), o || {});
const PD = (name, layers, o) => mkVoice(name, layers, o);
`;
const ctx = vm.createContext({console, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object, JSON, Map, Set, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly});
ctx.globalThis = ctx; ctx.self = ctx;
vm.runInContext(engine + helpers + `\n;globalThis.__E = {renderVoice, mkVoice, freshFx, FX, rA, cA, PD, PFX, voiceLen, factoryKit};`, ctx, {filename: 'engine.js'});
const E = ctx.__E;
let src;
if (voicesPath === 'kits' || voicesPath === 'x0x'){
  const a = html.indexOf('function drumKitGroups(){'), b = html.indexOf('// The kits: eight sounds', a);
  if (a < 0 || b < 0) throw new Error('drumKitGroups not found');
  src = html.slice(a, b);
} else src = fs.readFileSync(voicesPath, 'utf8');
const groups = vm.runInContext(`(() => { const {mkVoice, rA, cA, PD, PFX, factoryKit} = __E; return (${src})(); })()`, ctx);

function wav16(data, sr){
  const n = data.length, b = Buffer.alloc(44 + n * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 2, 4); b.write('WAVE', 8); b.write('fmt ', 12); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34);
  b.write('data', 36); b.writeUInt32LE(n * 2, 40);
  for (let i = 0; i < n; i++){ const v = Math.max(-1, Math.min(1, data[i])); b.writeInt16LE(Math.round(v * 32767), 44 + i * 2); }
  return b;
}
const sr = 48000; fs.mkdirSync(outDir, {recursive: true});
const rows = [];
for (const [machine, list] of groups){
  for (const [name, make] of list){
    const key = `${machine}__${name}`.replace(/[^A-Za-z0-9_]+/g, '_');
    if (filter && !key.toLowerCase().includes(filter.toLowerCase())) continue;
    const v = make(); const t0 = performance.now();
    const LR = {tune:[-48,48], fine:[-100,100], pAmt:[-48,48], pDec:[1,2000], atk:[0.05,2000], hold:[0,30000], dec:[1,6000], lpCut:[20,20000], hpCut:[20,20000], lpRes:[0,1], hpRes:[0,1], delay:[0,250], burst:[0,8], spacing:[2,60], level:[0,1], a:[0,1], b:[0,1]};
    const VR = {tune:[-24,24], level:[-36,6], drive:[0,1], len:[0.1,30], gPAmt:[-48,48], gPDec:[1,2000], gHold:[0,30000], gDec:[1,6000], gLpCut:[20,20000]};
    v.layers.forEach((L, i) => { if (!L.on) return; for (const k in LR) if (L[k] < LR[k][0] || L[k] > LR[k][1]) console.log('RANGE', key, 'layer', i, k, L[k]); if (!Number.isInteger(L.tune)) console.log('RANGE', key, 'tune not int', L.tune); });
    for (const k in VR) if (v[k] < VR[k][0] || v[k] > VR[k][1]) console.log('RANGE', key, 'voice', k, v[k]);
    const d = E.renderVoice(v, sr, null);
    const ms = performance.now() - t0;
    let pk = 0, nan = false, ss = 0; for (let i = 0; i < d.length; i++){ const a = Math.abs(d[i]); if (!(a === a)) nan = true; if (a > pk) pk = a; if (i < sr * 0.1) ss += d[i] * d[i]; }
    const rms = Math.sqrt(ss / Math.min(d.length, sr * 0.1));
    fs.writeFileSync(path.join(outDir, key + '.wav'), wav16(d, sr));
    rows.push(`vl=${v.level} ${key.padEnd(44)} len ${(d.length / sr).toFixed(2)}s peak ${(20 * Math.log10(pk + 1e-9)).toFixed(1)} dB rms100 ${(20 * Math.log10(rms + 1e-9)).toFixed(1)} dB ${nan ? 'NaN!' : ''} ${ms.toFixed(0)}ms`);
  }
}
console.log(rows.join('\n'));
