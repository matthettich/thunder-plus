// U06 golden: what the JS's own JSON.parse / JSON.stringify and the song's saved values give for sounds/ELECTRO.thunder and the
// JSON goldens in tests/golden/, for native/tests/test_song.cpp to compare the C++ reader and writer against.
//   node native/tools/song-golden.mjs            writes native/tests/golden/song_golden.inc
//   node native/tools/song-golden.mjs --check    exits 1 if the committed file differs
// Typed values are read from the saved file as index.html's normSong would leave them (for ELECTRO, a v6 song, the clamps are
// the identity: this script throws if a value is outside its clamp, so a change to the file shows up here, not as a wrong golden).
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const M = (1n << 64n) - 1n;
const fnv = () => { let h = 0xcbf29ce484222325n; const b = new DataView(new ArrayBuffer(8));
  const byte = x => { h ^= BigInt(x); h = (h * 0x100000001b3n) & M; };
  return { int(x) { b.setInt32(0, x, true); for (let i = 0; i < 4; i++) byte(b.getUint8(i)); }, f64(x) { b.setFloat64(0, x, true); for (let i = 0; i < 8; i++) byte(b.getUint8(i)); },
    bytes(buf) { for (let i = 0; i < buf.length; i++) { h ^= BigInt(buf[i]); h = (h * 0x100000001b3n) & M; } }, get: () => h }; };
const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';
const chk = (v, lo, hi, what) => { if (!(Number.isInteger(v) && v >= lo && v <= hi)) throw new Error(what + ' = ' + v + ' is outside ' + lo + '..' + hi + ': the golden would not be the identity of normSong'); return v; };
const NPH = 4096, RW = 12, PROWS = 32, NT = 32;

const ePath = 'sounds/ELECTRO.thunder';
const text = fs.readFileSync(path.join(root, ePath), 'utf8');
const F = JSON.parse(text), S = F.song;
if (F.app !== 'thunder-project') throw new Error('not a project file');

const g = {};
g.build = chk(F.build, 0, 1e6, 'build'); g.v = chk(S.v, 1, 99, 'v');
g.bpm = chk(S.bpm, 40, 300, 'bpm'); g.ntr = chk(S.ntr, 8, 32, 'ntr'); g.lpb = chk(S.lpb, 1, 16, 'lpb'); g.ticks = chk(S.ticks, 1, 32, 'ticks');
g.dlen = chk(S.dlen, 1, 32, 'dlen'); g.swing = chk(S.swing, 50, 75, 'swing'); g.swingB = chk(S.swingB, 1, 7, 'swingB');
g.master = chk(S.master, 0, 255, 'master'); g.mout = chk(S.mout, 0, 255, 'mout'); g.ni = chk(S.ni, 16, 64, 'ni');

let nonNull = 0, nonEmpty = 0; const HP = fnv();
for (let i = 0; i < NPH; i++) {
  const p = S.phrases[i];
  if (!Array.isArray(p)) { HP.int(0); continue; }
  nonNull++; HP.int(1); let any = false;
  for (let k = 0; k < PROWS; k++) { const r = Array.isArray(p[k]) ? p[k] : []; for (let m = 0; m < RW; m++) { const v = r[m] === undefined ? -1 : chk(r[m], -1, 255, 'phrase value'); if (v >= 0) any = true; HP.int(v); } }
  if (any) nonEmpty++;
}
g.phNonNull = nonNull; g.phNonEmpty = nonEmpty; g.phHash = HP.get();

const HG = fnv(); for (let y = 0; y < 256; y++) for (let t = 0; t <= NT; t++) HG.int(chk(S.song[y][t], -1, NPH - 1, 'song cell')); g.gridHash = HG.get();
{ let n = 0; for (let y = 0; y < 256; y++) for (let t = 0; t <= NT; t++) if (S.song[y][t] >= 0) n++; g.gridCells = n; }
const HC = fnv(); let nch = 0;
for (let i = 0; i < NPH; i++) { const c = S.chains[i]; if (!Array.isArray(c)) { HC.int(0); continue; } nch++; HC.int(1);
  for (let k = 0; k < 16; k++) { const r = c[k]; HC.int(chk(r[0], -1, NPH - 1, 'chain phrase')); HC.int(chk(r[1], -128, 127, 'chain transpose')); } }
g.chNonNull = nch; g.chHash = HC.get();
const HL = fnv(), HB = fnv(); let plenSum = 0;
for (let i = 0; i < NPH; i++) {
  const p = S.phrases[i]; const empty = !p || p.every(r => r.every(v => v < 0));
  const L = empty ? S.dlen : chk(S.plen[i], 1, 32, 'plen'); HL.int(L); plenSum += L; HB.int(chk(S.plpb[i], 0, 16, 'plpb'));
}
g.plenHash = HL.get(); g.plenSum = plenSum; g.plpbHash = HB.get();
const HS = fnv(); for (let y = 0; y < 256; y++) HS.int(S.smute[y] | 0); g.smuteHash = HS.get();

const HM = fnv(); g.mix = [];
for (let t = 0; t < NT; t++) { const m = S.mix[t]; HM.int(chk(m.vol, 0, 255, 'vol')); HM.int(chk(m.trim, 0, 255, 'trim')); HM.int(chk(m.pan, 0, 255, 'pan')); HM.int(m.mute ? 1 : 0); HM.int(m.solo ? 1 : 0); g.mix.push([m.vol, m.trim, m.pan, m.mute ? 1 : 0, m.solo ? 1 : 0]); }
g.mixHash = HM.get();
const HI = fnv(); g.pads = [];
const IF = ['pad:0:63', 'tsp:-48:48', 'vol:0:255', 'pan:0:255', 'rev:0:1', 'ofs:0:255', 'slc:0:64', 'smode:0:1', 'sync:0:3', 'beats:1:64', 'grain:0:4', 'mo:0:2', 'mch:0:15', 'mpt:0:3', 'pbr:1:48'];
const IF2 = ['lcc:-1:127', 'lsh:0:5', 'ldp:0:127', 'lct:0:127', 'lrg:0:1', 'kf:0:1', 'hut:0:50', 'huv:0:90', 'mlen:0:255', 'eco:0:16', 'ecf:0:100', 'ecp:-12:12', 'mrng:0:255', 'gtype:0:12', 'gcen:0:255'];
for (let i = 0; i < 64; i++) { const I = S.insts[i];
  for (const f of IF) { const [k, lo, hi] = f.split(':'); HI.int(chk(I[k], +lo, +hi, 'inst ' + k)); }
  for (let k = 0; k < 4; k++) HI.int(chk(I.cc[k], 0, 127, 'inst cc'));
  for (const f of IF2) { const [k, lo, hi] = f.split(':'); HI.int(chk(I[k], +lo, +hi, 'inst ' + k)); }
  HI.f64(I.lrb); HI.f64(I.ecr); g.pads.push(I.pad); }
g.instHash = HI.get();
{ const H = fnv(); for (let k = 0; k < 12; k++) H.f64(S.tune[k]); g.tuneHash = H.get(); }
g.hasPerf = S.perf != null ? 1 : 0; g.hasChainPatch = S.chainPatch && typeof S.chainPatch === 'object' ? 1 : 0; g.hasKit = F.kit && typeof F.kit === 'object' ? 1 : 0;
g.nVoices = Array.isArray(F.kit && F.kit.voices) ? F.kit.voices.length : -1;

// round trips: JSON.stringify(JSON.parse(text)) for ELECTRO and every .json under 5 MB in tests/golden/
const files = [ePath, ...fs.readdirSync(path.join(root, 'tests/golden')).filter(f => f.endsWith('.json')).sort().map(f => 'tests/golden/' + f)];
const rt = [];
for (const f of files) {
  const b = fs.readFileSync(path.join(root, f)); if (b.length >= 5e6) continue;
  const out = Buffer.from(JSON.stringify(JSON.parse(b.toString('utf8'))), 'utf8'); const H = fnv(); H.bytes(out);
  rt.push({ f, len: out.length, hash: H.get() });
}

const arr = (a) => '{' + a.join(', ') + '}';
let o = '// GENERATED by native/tools/song-golden.mjs from ' + ePath + ' and tests/golden/*.json. Do not edit: a mismatch means the port is wrong.\n';
for (const k of ['build', 'v', 'bpm', 'ntr', 'lpb', 'ticks', 'dlen', 'swing', 'swingB', 'master', 'mout', 'ni', 'phNonNull', 'phNonEmpty', 'gridCells', 'chNonNull', 'plenSum', 'hasPerf', 'hasChainPatch', 'hasKit', 'nVoices'])
  o += `static const int kSong_${k} = ${g[k]};\n`;
for (const k of ['phHash', 'gridHash', 'chHash', 'plenHash', 'plpbHash', 'smuteHash', 'mixHash', 'instHash', 'tuneHash']) o += `static const unsigned long long kSong_${k} = ${hex(g[k])};\n`;
o += `static const int kSong_mix[32][5] = {${g.mix.map(arr).join(', ')}};\n`;
o += `static const int kSong_pads[64] = ${arr(g.pads)};\n`;
o += `static const int kSongRoundTripN = ${rt.length};\nstatic const struct { const char* file; unsigned long long len, hash; } kSongRoundTrip[] = {\n` + rt.map(r => `  {"${r.f}", ${r.len}ull, ${hex(r.hash)}},`).join('\n') + '\n};\n';
const file = path.join(root, 'native/tests/golden/song_golden.inc');
if (process.argv.includes('--check')) { const have = fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : ''; if (have !== o) { console.error('song golden differs from the committed file'); process.exit(1); } console.log('song golden: unchanged'); }
else { fs.writeFileSync(file, o); console.log('wrote ' + path.relative(root, file)); }
