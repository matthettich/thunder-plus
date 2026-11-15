// U07: runs the sequencer golden cases of tools/seq-golden.mjs WITHOUT a browser, for diffing the C++ port against.
// core/seq-core.js is a plain script: its declarations (makeSeq and what it reads) are loaded into a `vm` context with the
// worklet globals stubbed, and each case is run the way the processor's op:'test' does (sim mode, coreSeeded random).
//   node native/tools/seq-dump.mjs [outDir]          the four ELECTRO cases -> <outDir>/seq-<id>.jsonl (default /tmp/seqdump-node)
//                                                    exit 1 unless each sha256 equals tests/golden/seq-<id>.json's
//   node native/tools/seq-dump.mjs --cmds-song FILE  only writes the `electro-cmds` case's edited song file (for the C++ test)
//   node native/tools/seq-dump.mjs --case FILE.json [outDir]   one extra case {id, file | song, vars, mode, opt, max, edit:'cmds'}
//                                                    (no golden: prints its sha256; used for the U07 extra checks, seq-extra.mjs).
//                                                    "full": true keeps what corePlain drops (the eng event's c and o, tw, slide, bend, vib, arp)
// The song is the saved file's `song` object as it is (the page hands the core its S after normSong; for a current song that
// is the same values, which the sha256 check proves for these cases).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const src = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
const ctx = vm.createContext({ console, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } },
  sampleRate: 48000, currentTime: 0, currentFrame: 0 });
vm.runInContext(src + '\n;globalThis.__u07 = { makeSeq, coreSeeded, corePlain, coreNum, FXC,' +
  ' load(s, v){ S = s; if (v){ if ("vtrk" in v) vtrk = v.vtrk; if ("NSLOT" in v) NSLOT = v.NSLOT; if (v.TFX) TFX = v.TFX; } busPlanH = null; busPlanVal = null; } };', ctx, { filename: 'seq-core.js' });
const U = ctx.__u07;

// tools/seq-golden.mjs's CMDS and withCommands (FXC from the core's own copy: the same list index.html has)
const CMDS = [['ARP', 0x37], ['RET', 0x24], ['DEL', 0x03], ['PRB', 0x80], ['GEN', 0x41], ['CHA', 0x80], ['RND', 0x24], ['TSP', 0x05],
  ['PIT', 0x0C], ['VOL', 0x90], ['PAN', 0x40], ['NTH', 0x02], ['KIL', 0x04], ['OFF', 0x06], ['TIC', 0x03], ['RNL', 0x13]];
function withCommands(song) {
  const FXC = [...U.FXC]; let n = 0;
  (song.song.phrases || []).forEach((ph, i) => { if (!Array.isArray(ph)) return; ph.forEach((r, y) => {
    if (!Array.isArray(r) || r[0] < 0 || r[10] >= 0) return; const [c, v] = CMDS[(i * 7 + y) % CMDS.length]; r[10] = FXC.indexOf(c); r[11] = v; n++; }); });
  return n;
}
const SONGS = [
  { id: 'electro', file: 'sounds/ELECTRO.thunder', mode: 'song' },
  { id: 'electro-mid', file: 'sounds/ELECTRO.thunder', mode: 'song', opt: { row: 9 } },
  { id: 'electro-loop', file: 'sounds/ELECTRO.thunder', mode: 'song', opt: { loop: { y0: 4, y1: 6 } }, max: 800 },
  { id: 'electro-cmds', file: 'sounds/ELECTRO.thunder', mode: 'song', edit: 'cmds' },
];

function run(C) {
  const j = C.song ? { song: C.song } : JSON.parse(fs.readFileSync(path.resolve(ROOT, C.file), 'utf8'));
  if (C.edit === 'cmds') withCommands(j);
  // into the context's own realm, so `Array.isArray`, Map and friends see its own objects
  const S = vm.runInContext('(' + JSON.stringify(j.song) + ')', ctx), vars = vm.runInContext('(' + JSON.stringify(C.vars || { vtrk: false, NSLOT: 16, TFX: {} }) + ')', ctx);
  U.load(S, vars);
  const o = C.opt || {}, max = C.max || 0;
  ctx.__case = { mode: C.mode || 'song', o, max, full: !!C.full };
  // op:'test', statement for statement (extra options of a --case file are passed through: sect, track, phrase, only, seed, live)
  const rows = vm.runInContext(`(() => { const d = __case, o = d.o; return __u07.coreSeeded(() => { const out = [];
      const sq = __u07.makeSeq(S, d.mode || 'song', Object.assign({}, o, {row:o.row || 0, loop:o.loop || null, once:'once' in o ? !!o.once : !o.loop, sim:true}, o.only ? {only:new Set(o.only)} : {}));
      const full = v => typeof v === 'number' ? __u07.coreNum(v) : Array.isArray(v) ? v.map(full) : v && typeof v === 'object' ? Object.fromEntries(Object.keys(v).sort().map(k => [k, full(v[k])])) : v;
      for (let n = 0; sq.alive() && n < (d.max || 20000); n++){ const r = sq.step(); out.push({t:__u07.coreNum(r.t), evs:r.evs.map(d.full ? full : __u07.corePlain)}); }
      return out; }); })()`, ctx);
  const text = rows.map(r => JSON.stringify(r)).join('\n') + '\n';
  return { rows, text, sha256: crypto.createHash('sha256').update(text).digest('hex') };
}

const a = process.argv.slice(2);
if (a[0] === '--cmds-song') {
  const j = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')); withCommands(j);
  fs.mkdirSync(path.dirname(path.resolve(a[1])), { recursive: true }); fs.writeFileSync(a[1], JSON.stringify(j)); process.exit(0);
}
if (a[0] === '--case') {
  const C = JSON.parse(fs.readFileSync(a[1], 'utf8')), out = a[2] || '/tmp/seqdump-node'; fs.mkdirSync(out, { recursive: true });
  const r = run(C); fs.writeFileSync(path.join(out, `seq-${C.id}.jsonl`), r.text);
  const counts = {}; for (const w of r.rows) for (const e of w.evs) counts[e.type] = (counts[e.type] || 0) + 1;
  fs.writeFileSync(path.join(out, `seq-${C.id}.json`), JSON.stringify({ rows: r.rows.length, sha256: r.sha256, counts,
    rowHashes: r.rows.map(w => crypto.createHash('sha256').update(JSON.stringify(w)).digest('hex').slice(0, 12)) }));
  console.log(`${C.id}: ${r.rows.length} rows, sha256 ${r.sha256}, ${JSON.stringify(counts)}`); process.exit(0);
}
const out = a[0] || '/tmp/seqdump-node'; fs.mkdirSync(out, { recursive: true });
let bad = false;
for (const C of SONGS) {
  const r = run(C); fs.writeFileSync(path.join(out, `seq-${C.id}.jsonl`), r.text);
  const g = JSON.parse(fs.readFileSync(path.join(ROOT, `tests/golden/seq-${C.id}.json`), 'utf8'));
  const ok = g.sha256 === r.sha256 && g.rows === r.rows.length; if (!ok) bad = true;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${C.id}: ${r.rows.length} rows, sha256 ${r.sha256}${ok ? '' : ' (golden ' + g.sha256 + ', ' + g.rows + ' rows)'}`);
}
process.exit(bad ? 1 : 0);
