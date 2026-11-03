// Builds the Table engine's starter wavetables (wavetables/*.json) from Adventure Kid Waveforms (AKWF, CC0).
//   git clone --depth 1 https://github.com/KristofferKarlAxelEkstrand/AKWF-FREE /tmp/akwf
//   node tools/akwf-tables.mjs /tmp/akwf/AKWF
// Each table stacks the single cycles of one or more AKWF folders, put in the order that morphs most smoothly
// (file order, darkest→brightest, or a nearest-neighbour walk from the darkest: whichever has the smallest steps),
// thinned evenly to 64 frames. The tables are made by the app's own code (TableCore.fromCycles, wtReadWav, taken out
// of index.html), so the files are exactly what Load wavetable… would make from the same waves.
import fs from 'fs'; import path from 'path'; import vm from 'vm'; import { fileURLToPath } from 'url';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..'), AK = process.argv[2];
if (!AK || !fs.existsSync(AK)) { console.error('usage: node tools/akwf-tables.mjs <AKWF folder (the one holding AKWF_bw_saw…)>'); process.exit(1); }
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
// a class, function or const from index.html, up to its closing brace or bracket
const block = start => { const i = html.indexOf(start); if (i < 0) throw new Error('not in index.html: ' + start);
  const j = html.slice(i).search(/[{[]/) + i, o = html[j], c = o === '{' ? '}' : ']'; let d = 0;
  for (let k = j; k < html.length; k++) { if (html[k] === o) d++; else if (html[k] === c && !--d) return html.slice(i, k + 1) + ';'; } };
const ctx = { Math, Float32Array, Float64Array, Int16Array, Uint8Array, DataView, Map, Date, String, Array, Object, isFinite };
ctx.globalThis = ctx; vm.createContext(ctx);
vm.runInContext(['class KeysUtil', 'class SpecUtil', 'class TableCore', 'function wtReadWav', 'const WT_STARTER'].map(block).join('\n')
  + '\nglobalThis.T = {SpecUtil, TableCore, wtReadWav, WT_STARTER};', ctx);
const { SpecUtil, TableCore, wtReadWav, WT_STARTER } = ctx.T;

// log-magnitude spectrum of a cycle (harmonics 1..64, normalised), for ordering
function spec(c) {
  const M = 1024, re = new Float64Array(M), im = new Float64Array(M), w = TableCore.resampleCycle(c, M); let dc = 0;
  for (let i = 0; i < M; i++) dc += w[i]; dc /= M; for (let i = 0; i < M; i++) re[i] = w[i] - dc;
  SpecUtil.fft(re, im, false); const m = new Float64Array(64); let pk = 1e-12;
  for (let n = 1; n <= 64; n++) { m[n - 1] = Math.hypot(re[n], im[n]); pk = Math.max(pk, m[n - 1]); }
  for (let n = 0; n < 64; n++) m[n] = Math.log10(m[n] / pk + 1e-4);
  return m;
}
const dist = (a, b) => { let s = 0; for (let i = 0; i < a.length; i++) s += (a[i] - b[i]) ** 2; return Math.sqrt(s); };
const centroid = m => { let a = 0, b = 0; m.forEach((v, i) => { const p = 10 ** v; a += p * (i + 1); b += p; }); return a / b; };
const cost = (ord, S) => ord.slice(1).reduce((s, k, i) => s + dist(S[ord[i]], S[k]), 0);

const out = path.join(ROOT, 'wavetables'); fs.mkdirSync(out, { recursive: true });
let total = 0;
for (const [id, name, , folders] of WT_STARTER) {
  const cyc = [];
  for (const f of folders) {
    const dir = path.join(AK, f), files = fs.readdirSync(dir).filter(x => /\.wav$/i.test(x)).sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));
    for (const x of files) { const b = fs.readFileSync(path.join(dir, x)), r = wtReadWav(b.buffer.slice(b.byteOffset, b.byteOffset + b.length)); if (r && r.x.length >= 16) cyc.push(r.x); }
  }
  const S = cyc.map(spec), file = cyc.map((_, k) => k), bright = file.slice().sort((a, b) => centroid(S[a]) - centroid(S[b]));
  const walk = [bright[0]], left = new Set(file); left.delete(bright[0]);
  while (left.size) { const l = walk[walk.length - 1]; let best = -1, bd = Infinity; for (const k of left) { const d = dist(S[l], S[k]); if (d < bd) { bd = d; best = k; } } walk.push(best); left.delete(best); }
  const ords = { file, bright, walk }, pick = Object.keys(ords).reduce((a, b) => cost(ords[a], S) <= cost(ords[b], S) ? a : b);
  const wt = TableCore.fromCycles(ords[pick].map(k => cyc[k]), name);
  wt.id = 'akwf-' + id; wt.from = 'starter';
  const txt = JSON.stringify(wt); fs.writeFileSync(path.join(out, id + '.json'), txt); total += txt.length;
  console.log(`${id.padEnd(14)} ${String(cyc.length).padStart(4)} waves → ${String(wt.nf).padStart(2)} frames, ${String(wt.h).padStart(3)} partials, ${pick.padEnd(6)} ${(txt.length / 1024).toFixed(1)} KB`);
}
console.log('total', (total / 1024).toFixed(0), 'KB in', WT_STARTER.length, 'files');
