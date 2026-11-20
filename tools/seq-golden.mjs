// SEQUENCER GOLDEN TEST (docs/NATIVE-DESKTOP-PLAN.md, phase 0).
// Plays each test song through the app's own sequencer (makeSeq, sim mode: no audio, no renders) in headless Chrome and
// compares every event it produces with tests/golden/seq-<song>.json. The point: when the sequencer moves onto the audio
// thread (phase 2) and into C++ (phase 3), the event stream must not change.
//
//   node tools/seq-golden.mjs            compare (exit 1 on any difference; the first differing row is printed)
//   node tools/seq-golden.mjs --update   write the goldens (only after checking why they changed)
//   node tools/seq-golden.mjs --core     the same cases on the audio thread (core/seq-core.js): must match the same goldens
//   node tools/seq-golden.mjs --chop     the chop cases (applyChop of index.html, in a vm; see CHOP CASES)
//
// Needs Chrome (or Chromium) and the playwright package: CHROME=/path/to/chrome, PLAYWRIGHT=/path/to/playwright/index.mjs
// if they aren't found. The full event dumps go to tests/golden/out/ (not committed) for diffing.
import http from 'http'; import fs from 'fs'; import path from 'path'; import crypto from 'crypto'; import vm from 'vm'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const UPDATE = process.argv.includes('--update');
// Test cases: a song file, optionally changed in memory (`edit`), and how it is played (row: a mid-song start, which runs
// CHASE; loop: rows y0–y1 repeated, capped by maxRows).
const CMDS = [['ARP', 0x37], ['RET', 0x24], ['DEL', 0x03], ['PRB', 0x80], ['GEN', 0x41], ['CHA', 0x80], ['RND', 0x24], ['TSP', 0x05],
  ['PIT', 0x0C], ['VOL', 0x90], ['PAN', 0x40], ['NTH', 0x02], ['KIL', 0x04], ['OFF', 0x06], ['TIC', 0x03], ['RNL', 0x13]];
// Every phrase row with a note gets one command in its third effect column (if free), cycling through CMDS. FXC (the
// command numbers) is read from index.html so the file stays right if commands are added.
function withCommands(song) {
  const src = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8'), m = src.match(/const FXC = \[([\s\S]*?)\];/);
  const FXC = [...m[1].matchAll(/'([A-Z0-9]{3})'/g)].map(x => x[1]);
  let n = 0;
  (song.song.phrases || []).forEach((ph, i) => { if (!Array.isArray(ph)) return; ph.forEach((r, y) => {
    if (!Array.isArray(r) || r[0] < 0 || r[10] >= 0) return; const [c, v] = CMDS[(i * 7 + y) % CMDS.length]; r[10] = FXC.indexOf(c); r[11] = v; n++; }); });
  return n;
}
const SONGS = [
  { id: 'electro', file: 'sounds/ELECTRO.thunder', mode: 'song' },
  { id: 'electro-mid', file: 'sounds/ELECTRO.thunder', mode: 'song', opt: { row: 9 } },
  { id: 'electro-loop', file: 'sounds/ELECTRO.thunder', mode: 'song', opt: { loop: { y0: 4, y1: 6 } }, max: 800 },
  { id: 'electro-cmds', file: 'sounds/ELECTRO.thunder', mode: 'song', edit: withCommands },
];
// LIVE-CLOCK CASES (docs/port/SEQ-LIVE-DESIGN.md A3; goldens native/tests/golden/seq-*-live*.json, read by ctest `seq_live`).
// The page's makeSeq is not reachable from outside index.html (it is not a global), so these run the audio thread's copy, core/seq-core.js's
// makeSeq, loaded into a `vm` context exactly as native/tools/seq-dump.mjs does: no browser needed. THAT PATH IS PROVEN FIRST: the four cases above
// are run through it and must equal their committed goldens (made by the page in Chrome) byte for byte, or the live cases are not written / compared.
// The clock is written between steps exactly as core-processor.js writes it: `time = t0` before the first step (play, :423), `bpm = v` (the 'bpm'
// message, :354), and the catch-up skip / RESUME shift `base += d; time += d; if (btT != null) btT += d` (:778, :421).
// live: {time0, ops: [{after: N, bpm: v | 'song', shift: d}]}: an op runs after the N-th step (1 = after the first). Each row also records the
// clock after the step: {t, evs, clk: {time, base, bt, btT, bpm}}. 'song' = the song's own bpm (S.bpm), written into the golden as a number.
const LIVE_SONGS = [
  { id: 'electro-live-time', file: 'sounds/ELECTRO.thunder', mode: 'song', live: { time0: 1234.5678, ops: [] } },
  { id: 'electro-live-bpm', file: 'sounds/ELECTRO.thunder', mode: 'song', live: { ops: [{ after: 40, bpm: 97.3 }, { after: 120, bpm: 'song' }] } },
  { id: 'electro-live-shift', file: 'sounds/ELECTRO.thunder', mode: 'song', live: { ops: [{ after: 25, shift: 0.37 }, { after: 60, shift: -0.011 }] } },
  { id: 'electro-cmds-live-all', file: 'sounds/ELECTRO.thunder', mode: 'song', edit: withCommands,
    live: { time0: 1234.5678, ops: [{ after: 25, shift: 0.37 }, { after: 40, bpm: 97.3 }, { after: 60, shift: -0.011 }, { after: 120, bpm: 'song' }] } },
];
const LIVE_DIR = path.join(ROOT, 'native/tests/golden');
function liveHarness() {
  const sctx = vm.createContext({ console, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 48000, currentTime: 0, currentFrame: 0 });
  vm.runInContext(fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8') + '\n;globalThis.__u = { makeSeq, coreSeeded, corePlain, coreNum, FXC,' +
    ' load(s, v){ S = s; if (v){ if ("vtrk" in v) vtrk = v.vtrk; if ("NSLOT" in v) NSLOT = v.NSLOT; if (v.TFX) TFX = v.TFX; } busPlanH = null; busPlanVal = null; } };', sctx, { filename: 'seq-core.js' });
  return { ctx: sctx, U: sctx.__u };
}
// one case on the core's makeSeq; `live` null = a plain case (the proof path), else the clock ops above. Returns {rows, info}.
function runVm(H, text, mode, max, opt, live) {
  const j = JSON.parse(text), bpm = j.song.bpm;
  const Sv = vm.runInContext('(' + JSON.stringify(j.song) + ')', H.ctx), vars = vm.runInContext('({vtrk:false,NSLOT:16,TFX:{}})', H.ctx);
  H.U.load(Sv, vars);
  H.ctx.__c = { mode, max, o: opt || {}, live: live ? JSON.parse(JSON.stringify({ time0: live.time0 ?? null, ops: live.ops.map(o => ({ ...o, ...(o.bpm === 'song' ? { bpm } : {}) })) })) : null };
  const rows = vm.runInContext(`(() => { const d = __c, o = d.o, L = d.live; return __u.coreSeeded(() => { const out = [];
    const sq = __u.makeSeq(S, d.mode || 'song', {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:true});
    if (L && L.time0 != null) sq.time = L.time0;
    for (let n = 0; sq.alive() && n < (d.max || 20000); n++){ const r = sq.step(); const row = {t:__u.coreNum(r.t), evs:r.evs.map(__u.corePlain)};
      if (L){ row.clk = {time:__u.coreNum(sq.time), base:__u.coreNum(sq.base), bt:__u.coreNum(sq.bt), btT:__u.coreNum(sq.btT), bpm:__u.coreNum(sq.bpm)};
        for (const p of L.ops) if (p.after === n + 1){ if (p.bpm != null) sq.bpm = p.bpm; if (p.shift != null){ sq.base += p.shift; sq.time += p.shift; if (sq.btT != null) sq.btT += p.shift; } } }
      out.push(row); }
    return out; }); })()`, H.ctx);
  return { rows, bpm: j.song.bpm, live: L2(live, bpm) };
  function L2(l, b) { return l ? { time0: l.time0 ?? null, ops: l.ops.map(o => ({ ...o, ...(o.bpm === 'song' ? { bpm: b } : {}) })) } : null; }
}
function liveCases() {
  if (process.argv.includes('--no-live')) return true;
  const H = liveHarness(); let bad = false; const sha = t => crypto.createHash('sha256').update(t).digest('hex');
  // 1. the proof: the four plain cases through this path equal the committed goldens
  for (const S of SONGS) {
    let t0 = fs.readFileSync(path.join(ROOT, S.file), 'utf8'); if (S.edit) { const j = JSON.parse(t0); S.edit(j); t0 = JSON.stringify(j); }
    const { rows } = runVm(H, t0, S.mode, S.max || 0, S.opt || null, null);
    const g = JSON.parse(fs.readFileSync(path.join(ROOT, `tests/golden/seq-${S.id}.json`), 'utf8')), h = sha(rows.map(r => JSON.stringify(r)).join('\n') + '\n');
    const ok = h === g.sha256 && rows.length === g.rows; if (!ok) bad = true;
    console.log(`${ok ? 'ok  ' : 'FAIL'} core-path proof ${S.id}: ${rows.length} rows ${ok ? '= the page\'s golden' : 'DIFFERS from the page\'s golden: live cases not written'}`);
  }
  if (bad) return false;
  // 2. the live cases
  fs.mkdirSync(LIVE_DIR, { recursive: true });
  for (const S of LIVE_SONGS) {
    let t0 = fs.readFileSync(path.join(ROOT, S.file), 'utf8'); if (S.edit) { const j = JSON.parse(t0); S.edit(j); t0 = JSON.stringify(j); }
    const { rows, bpm, live } = runVm(H, t0, S.mode, S.max || 0, S.opt || null, S.live), text = rows.map(r => JSON.stringify(r)).join('\n') + '\n';
    const counts = {}; for (const r of rows) for (const e of r.evs) counts[e.type] = (counts[e.type] || 0) + 1;
    const now = { song: S.file, mode: S.mode, live, bpm, rows: rows.length, events: Object.values(counts).reduce((a, b) => a + b, 0), counts, sha256: sha(text),
      rowHashes: rows.map(r => crypto.createHash('sha256').update(JSON.stringify(r)).digest('hex').slice(0, 12)) };
    const gf = path.join(LIVE_DIR, `seq-${S.id}.json`);
    if (UPDATE || !fs.existsSync(gf)) { fs.writeFileSync(gf, JSON.stringify(now, null, 1) + '\n'); console.log(`wrote ${path.relative(ROOT, gf)}: ${now.rows} rows, ${now.events} events`); continue; }
    const was = JSON.parse(fs.readFileSync(gf, 'utf8')), ok = was.sha256 === now.sha256; if (!ok) bad = true;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${S.id}: ${now.rows} rows, ${now.events} events${ok ? '' : ' (first differing row ' + now.rowHashes.findIndex((h, k) => h !== was.rowHashes[k]) + ')'}`);
  }
  return !bad;
}
// EDIT CASES (docs/port/SEQ-LIVE-DESIGN.md Part B ruling 7; goldens native/tests/golden/seq-edit-*.json, read by ctest `seq_takeover`; --edit).
// The song is edited IN PLACE between steps of a RUNNING makeSeq, by the processor's own message handler (core-processor.js `msg`: the 'patch'
// and 'bpm' / 'loop' cases, which core/seq-core.js carries as ThunderCore.prototype.msg, called with a stub `this`), never by hand. Each edit is
// written here as a function over a copy of the song; the patch ops the page would send (coreDiff's [key, index, item] / [key, 'len', n] /
// [key, 'del'] / [key, null, value]) are computed from the difference, stored in the golden, and replayed by the C++ test through the same
// grammar. `after: N` = after the N-th step (as the live cases). The base song of a case (a variant of ELECTRO) is stored the same way (baseOps).
// Row = {t, evs, clk}, as the live goldens. The golden is the JS's result: the C++ sequencer, taking over at the same step, must equal it.
const EDIT_DIR = LIVE_DIR;
const clone = o => JSON.parse(JSON.stringify(o)), deq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
function diffOps(a, b) {
  const ops = [];
  for (const k of Object.keys(a)) if (!(k in b)) ops.push([k, 'del', null]);
  for (const k of Object.keys(b)) {
    if (!(k in a)) { ops.push([k, null, b[k]]); continue; }
    if (deq(a[k], b[k])) continue;
    if (Array.isArray(a[k]) && Array.isArray(b[k])) {
      for (let i = 0; i < Math.min(a[k].length, b[k].length); i++) if (!deq(a[k][i], b[k][i])) ops.push([k, i, b[k][i]]);
      for (let i = a[k].length; i < b[k].length; i++) ops.push([k, i, b[k][i]]);
      if (b[k].length < a[k].length) ops.push([k, 'len', b[k].length]);
    } else ops.push([k, null, b[k]]);
  }
  return ops;
}
const ph = (S, row, t) => { const c = S.chains[S.song[row][t]]; return c ? c[0][0] : -1; };   // the first phrase of the chain at (row, track)
const FXN = n => ['ARP','ARC','CHA','DEL','GRV','GGR','HOP','INS','KIL','OFF','NTH','RET','REP','RND','RNL','PSL','PBN','PVB','SED','TPO','TSP','VOL','PIT','FIN','PLY','STA','PAN','TIC','WOB','FXO','FLX','GAT','RTG',
  'CCA','CCB','CCC','CCD','PGM','BYP','CSL','DIR','MUT','GEN','PRB','FXS','FXV','SCA','SCG','CUT','RES','DEC','FDI','TRM','APN','TVL','TPN','STP','LPB','PDL','ONE','MPB','MAT','CND','NUD','RTV','SLC','XFD','MAC',
  'LK1','LK2','LK3','LK4','PRT','END','JMP','FLY','ATK','REL','TCH','SKP','CHD'].indexOf(n);
const setCmd = (S, pid, y, col, name, v) => { const r = S.phrases[pid][y]; r[6 + 2 * col] = FXN(name); r[7 + 2 * col] = v; };
const setNote = (S, pid, y, n) => { S.phrases[pid][y][0] = n; if (S.phrases[pid][y][5] < 0) S.phrases[pid][y][5] = 0; };
// variants of ELECTRO (base edits)
const vChain = S => { for (let t = 0; t <= 9; t++) { const cid = S.song[0][t]; if (cid < 0) continue; S.chains[cid][1] = [ph(S, 1, t), 0]; S.chains[cid][2] = [ph(S, 2, t), 3]; } };
const vPlen = S => { for (let t = 0; t <= 9; t++) { const p = ph(S, 0, t); if (p >= 0) S.plen[p] = 16; } };
const vPerf = S => {
  S.perf = { sc: [{ n: 'A', v: { 'vol:0': 90, 'pan:1': 40, 'vol:2': 200 } }, { n: 'B', v: { 'vol:0': 230, 'pan:1': 210, 'cut:3': 80 } }, { n: '', v: {} }, { n: '', v: {} }], xa: 0, xb: 1, xf: 60,
    mac: [{ n: 'M1', v: 100, tg: [{ id: 'vol:3', lo: 20, hi: 240 }, { id: 'pan:4', lo: 0, hi: 255 }] }, { n: 'M2', v: 30, tg: [{ id: 'res:2', lo: 0, hi: 200 }] }].concat(Array.from({ length: 6 }, (_, i) => ({ n: 'M' + (i + 3), v: 0, tg: [] }))),
    lk: { '1': ['vol:5', null, null, null] }, lfo: { '0': [{ id: 'pan:0', sh: 0, rt: 3, dp: 60 }], '2': [{ id: 'cut:2', sh: 1, rt: 4, dp: 90 }] } };
  S.parts = [{ mix: S.mix.map(m => [m.vol, m.pan]), fx: {}, insts: S.insts.slice(0, 16).map(I => ({ ...I, tsp: 0 })) }, { mix: S.mix.map(m => [200, 100]), fx: {}, insts: S.insts.slice(0, 16).map(I => ({ ...I, tsp: 5 })) }];
  const p0 = ph(S, 0, 0), p1 = ph(S, 0, 1), p2 = ph(S, 1, 0);
  setCmd(S, p0, 9, 0, 'XFD', 200); setCmd(S, p1, 12, 0, 'MAC', 0x1F); setCmd(S, p2, 4, 0, 'PRT', 2); setCmd(S, p2, 20, 0, 'PRT', 1); setCmd(S, p1, 20, 1, 'LK1', 0); setCmd(S, p0, 25, 0, 'XFD', 20);
  for (let r = 1; r < 8; r++) { const q = ph(S, r, 0); if (q >= 0 && S.phrases[q]) { setCmd(S, q, 9, 1, 'XFD', 100 + 17 * r); setCmd(S, q, 25, 1, 'XFD', 30 + 11 * r); } }   // a perfDiff every few steps, so an edit of a resting value would be heard
};
const vSeed = S => {
  const p0 = ph(S, 0, 0), p1 = ph(S, 0, 1), p2 = ph(S, 0, 2), p3 = ph(S, 1, 0), p4 = ph(S, 2, 0);
  setCmd(S, p0, 0, 2, 'SED', 7); setCmd(S, p0, 8, 0, 'PRB', 0x80); setCmd(S, p1, 4, 0, 'NTH', 0x02); setCmd(S, p1, 12, 0, 'NTH', 0x13); setCmd(S, p2, 6, 0, 'CND', 0x20); setCmd(S, p2, 10, 0, 'RND', 0x24);
  setCmd(S, p3, 5, 0, 'CHA', 0x90); setCmd(S, p3, 14, 0, 'TCH', 0x90); setCmd(S, p3, 18, 0, 'SKP', 1); setCmd(S, p4, 3, 0, 'NTH', 0x04); setCmd(S, p4, 16, 0, 'RNL', 0x13); setCmd(S, p4, 24, 0, 'JMP', 20);
};
const vFx = S => { const p0 = ph(S, 0, 0), p1 = ph(S, 0, 1); S.tfx[0][1] = { type: 'flex', on: true }; S.tfx[1][0] = { type: 'sgate', on: true }; S.tfx[1][2] = { type: 'retrig', on: true };
  setCmd(S, p0, 3, 0, 'FLX', 0x20); setCmd(S, p0, 9, 0, 'GAT', 0x30); setCmd(S, p1, 5, 0, 'RTG', 0x40); setCmd(S, p1, 13, 0, 'FXV', 0x10); setCmd(S, p0, 17, 0, 'FXO', 0x31); };
const vNth = S => {   // NTH on rows s and s + 16 (different values) of the first three phrases, which loop
  const P = [ph(S, 0, 0), ph(S, 0, 1), ph(S, 0, 2)];
  P.forEach((p, k) => [[2 + 2 * k, 0x21], [18 + 2 * k, 0x32]].forEach(([y, v], i) => { setNote(S, p, y, 58 + 3 * k + i); setCmd(S, p, y, 0, 'NTH', v); }));
  P.forEach((p, k) => [[4 + 2 * k, 0x03], [20 + 2 * k, 0x02]].forEach(([y, v], i) => { setNote(S, p, y, 66 + k + i); setCmd(S, p, y, 1, 'CND', v); }));
};
const vJmp = S => {   // no loop: each of tracks 0 to 2 plays its phrase 6 times in a chain; the master's chain ends in a phrase with a JMP to song row 1 (fresh() on every track: new NTH counters), where the same chains play again
  const m = S.chains[S.song[0][32]][0][0]; for (let t = 0; t < 3; t++) { const cid = S.song[0][t], p = S.chains[cid][0][0]; S.chains[cid] = Array.from({ length: 6 }, () => [p, 0]); S.song[1][t] = cid; }
  S.chains[S.song[0][32]] = [[m, 0], [m, 0], [m, 0], [m, 0], [4000, 0]]; S.phrases[4000] = Array.from({ length: 32 }, () => [-1, -1, -1, -1, -1, 0, -1, -1, -1, -1, -1, -1]); S.plen[4000] = 32; setCmd(S, 4000, 5, 0, 'JMP', 1); };
const vTch = S => { setCmd(S, ph(S, 0, 32), 0, 0, 'TCH', 0x40); for (let r = 0; r < 4; r++) for (let t = 0; t < 3; t++) { const p = ph(S, r, t); if (p < 0) continue; for (let y = 0; y < 32; y += 2) { setNote(S, p, y, 55 + ((y + t) % 9)); setCmd(S, p, y, 2, 'CHA', 0x90); } } };
const vTic = S => { const p = ph(S, 0, 0); setCmd(S, p, 3, 0, 'TIC', 0); setCmd(S, p, 17, 0, 'TIC', 0); setCmd(S, ph(S, 1, 0), 3, 0, 'TIC', 0); setCmd(S, ph(S, 2, 0), 3, 0, 'TIC', 0); };
const vGrv = S => { setCmd(S, ph(S, 0, 0), 2, 0, 'GGR', 0x24); setCmd(S, ph(S, 1, 0), 2, 0, 'TIC', 0); };
const vDlen = S => { for (let r = 1; r < 4; r++) for (let t = 0; t < 3; t++) { const cid = S.song[r][t]; if (cid >= 0) S.chains[cid][0] = [-1, 0]; } };
const compose = (...fs) => S => fs.forEach(f => f(S));
const EDIT_SONGS = [
  { id: 'notes', edits: [{ after: 40, fn: S => { setNote(S, ph(S, 2, 0), 4, 67); setNote(S, ph(S, 1, 0), 9, 70); } }, { after: 70, fn: S => { setNote(S, ph(S, 1, 1), 14, 55); } }] },
  { id: 'chain', base: vChain, edits: [{ after: 70, fn: S => { S.chains[S.song[0][0]][1][1] = 7; } }, { after: 100, fn: S => { S.chains[S.song[0][0]][0][1] = -5; S.chains[S.song[0][1]][2] = [ph(S, 5, 1), 2]; } }, { after: 150, fn: S => { S.chains[S.song[0][2]][2] = null; } }] },
  { id: 'cell-cleared', edits: [{ after: 100, fn: S => { S.song[3][0] = -1; } }, { after: 300, fn: S => { S.song[3][0] = S.song[2][0]; S.song[10][2] = -1; } }, { after: 400, fn: S => { for (let t = 0; t <= 9; t++) S.song[14][t] = -1; } }] },
  { id: 'chain-nulled', base: vChain, edits: [{ after: 40, fn: S => { S.chains[S.song[0][0]] = null; } }, { after: 70, fn: S => { S.chains[S.song[0][1]].length = 1; } }] },
  { id: 'plen', base: vPlen, edits: [{ after: 100, fn: S => { S.plen[ph(S, 3, 0)] = 2; } }, { after: 111, fn: S => { S.plen[ph(S, 3, 0)] = 1; S.phrases[ph(S, 3, 1)] = null; } }, { after: 200, fn: S => { S.plen[ph(S, 6, 0)] = 32; } }] },
  { id: 'instrument-removed', edits: [{ after: 30, fn: S => { S.insts[3] = null; S.insts.length = 12; } }] },
  { id: 'ntr', edits: [{ after: 50, fn: S => { S.ntr = 8; } }, { after: 200, fn: S => { S.ntr = 16; } }] },
  { id: 'bpm', edits: [{ after: 30, fn: S => { S.bpm = 150; } }, { after: 60, bpm: 97.3 }, { after: 120, bpm: 131 }, { after: 130, shift: 0.21 }] },
  { id: 'smute', edits: [{ after: 20, fn: S => { S.smute[0] = 5; S.smute[1] = 0xFF; S.smuteM = [2]; } }, { after: 70, fn: S => { S.smute[2] = 0x3; } }] },
  { id: 'bus', edits: [{ after: 30, fn: S => { S.chainOn[4] = true; S.chainPatch.modules[1].params.track = 5;   /* the patch's bus moves from track 4 (index 3, which has no notes) to track 5 (index 4, which has): the running JS keeps its PLAY's bus plan (D1) */ } }] },
  { id: 'tfx', base: vFx, edits: [{ after: 40, fn: S => { S.tfx[0][1] = null; S.tfx[1][2] = { type: 'flex', on: true }; } }, { after: 80, fn: S => { S.tfx[1][0] = null; } }] },
  { id: 'perf', base: vPerf, edits: [{ after: 5, fn: S => { S.perf.sc[1].v['vol:0'] = 10; } }, { after: 40, fn: S => { S.perf.sc[0].v['vol:0'] = 33; S.perf.lfo['0'][0].dp = 200; S.perf.lfo['2'][0].rt = 6; } },
    { after: 60, fn: S => { delete S.perf.sc[1].v['pan:1']; S.perf.mac[0].v = 250; S.perf.mac[1].v = 250; S.perf.mac[1].tg.push({ id: 'vol:6', lo: 5, hi: 6 }); } }, { after: 100, fn: S => { S.perf.lk['1'] = ['vol:5', 'pan:5', null, null]; S.perf.lfo['3'] = [{ id: 'pan:3', sh: 2, rt: 2, dp: 80 }]; S.parts[1].insts[0].tsp = 9; S.parts[1].mix[2] = [10, 10]; } }] },
  { id: 'seed', base: vSeed, edits: [{ after: 60, fn: S => { setCmd(S, ph(S, 0, 1), 20, 0, 'NTH', 0x03); } }, { after: 200, fn: S => { setCmd(S, ph(S, 0, 1), 4, 0, 'RNL', 0); setCmd(S, ph(S, 0, 0), 2, 1, 'SED', 9); } }, { after: 300, fn: S => { setCmd(S, ph(S, 0, 1), 4, 0, 'NTH', 0x02); setCmd(S, ph(S, 1, 0), 14, 0, 'TCH', 0x40); } }] },
  { id: 'seed-loop', base: vSeed, loop: { y0: 0, y1: 3 }, max: 700, edits: [{ after: 40, fn: S => { setCmd(S, ph(S, 1, 0), 11, 0, 'PRB', 0x40); } }, { after: 150, loop: { y0: 1, y1: 4 } }, { after: 260, fn: S => { S.song[1][0] = -1; } }, { after: 340, fn: S => { S.song[1][0] = S.song[0][0]; } }] },
  { id: 'sect', mode: 'sect', optx: { track: 0, sect: 0 }, base: vChain, max: 400, edits: [{ after: 20, fn: S => { setNote(S, ph(S, 0, 0), 6, 64); } }, { after: 60, fn: S => { S.chains[S.song[0][0]][1] = [ph(S, 4, 0), 0]; } }, { after: 130, fn: S => { S.plen[ph(S, 1, 0)] = 3; } }] },
  { id: 'phrase', mode: 'phrase', optx: { track: 0, phrase: -1 }, base: S => {}, phraseOf: S => ph(S, 0, 0), max: 300, edits: [{ after: 20, fn: S => { setNote(S, ph(S, 0, 0), 10, 66); } }, { after: 50, fn: S => { S.plen[ph(S, 0, 0)] = 5; } }] },
  { id: 'cmds', base: S => { withCommands({ song: S }); }, edits: [{ after: 25, fn: S => { S.bpm = 99; setCmd(S, ph(S, 1, 0), 3, 0, 'ARP', 0x47); } }, { after: 90, bpm: 100 }, { after: 200, fn: S => { setNote(S, ph(S, 7, 0), 2, 60); } }] },
  // F2 to F4 of verify-244 (the carried run state, and the song fields that are live or stay the PLAY's), added 19 Nov. Counters that live on across an edit:
  // NTH on rows s and s+16 of a looping phrase (the slot key), a JMP (fresh(): the track's NTH generation) before the edit, the master column's TCH.
  { id: 'nth-dense', base: vNth, loop: { y0: 0, y1: 1 }, max: 700, edits: [{ after: 100, fn: S => { setNote(S, ph(S, 0, 0), 10, 62); } }, { after: 230, fn: S => { setNote(S, ph(S, 0, 1), 12, 63); } }, { after: 360, fn: S => { setNote(S, ph(S, 0, 2), 14, 64); } }, { after: 500, fn: S => { setNote(S, ph(S, 0, 0), 11, 65); } }] },
  { id: 'nth-restart', base: compose(vNth, vJmp), max: 700, edits: [{ after: 100, fn: S => { setNote(S, ph(S, 0, 0), 10, 62); } }, { after: 175, fn: S => { setNote(S, ph(S, 0, 1), 12, 63); } }, { after: 210, fn: S => { setNote(S, ph(S, 0, 2), 14, 64); } }, { after: 245, fn: S => { setNote(S, ph(S, 0, 0), 11, 65); } }, { after: 300, fn: S => { setNote(S, ph(S, 0, 1), 13, 66); } }] },
  { id: 'master-tch', base: vTch, edits: [{ after: 60, fn: S => { for (let y = 0; y < 32; y += 2) setCmd(S, ph(S, 2, 0), y, 2, 'CHA', 0xFF); } }, { after: 130, fn: S => { for (let y = 0; y < 32; y += 2) setCmd(S, ph(S, 3, 1), y, 2, 'CHA', 0xFF); } }] },
  { id: 'ticks', base: vTic, edits: [{ after: 40, fn: S => { S.ticks = 4; } }, { after: 90, fn: S => { S.ticks = 9; } }] },
  { id: 'lpb', edits: [{ after: 30, fn: S => { S.lpb = 6; } }, { after: 80, fn: S => { S.lpb = 3; } }] },
  { id: 'grv', base: vGrv, edits: [{ after: 40, fn: S => { S.ticks = 4; } }, { after: 70, fn: S => { setNote(S, ph(S, 1, 0), 9, 70); } }] },
  { id: 'dlen', base: vDlen, edits: [{ after: 20, fn: S => { S.dlen = 8; } }, { after: 70, fn: S => { S.dlen = 4; } }] },
];
function editHarness() {
  const sctx = vm.createContext({ console, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 48000, currentTime: 0, currentFrame: 0 });
  vm.runInContext(fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8') + '\n;globalThis.__u = { makeSeq, coreSeeded, corePlain, coreNum, ThunderCore,' +
    ' load(s, v){ S = s; if (v){ if ("vtrk" in v) vtrk = v.vtrk; if ("NSLOT" in v) NSLOT = v.NSLOT; if (v.TFX) TFX = v.TFX; } busPlanH = null; busPlanVal = null; } };', sctx, { filename: 'seq-core.js' });
  return { ctx: sctx, U: sctx.__u };
}
function editCases() {
  const H = editHarness(), sha = t => crypto.createHash('sha256').update(t).digest('hex'), j0 = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')); let bad = false, wrote = [];
  // the handler under test is the processor's own: the text of its 'patch' case must be in core-processor.js too (the harness runs core/seq-core.js's copy)
  const cp = fs.readFileSync(path.join(ROOT, 'core/core-processor.js'), 'utf8'), sc = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
  for (const key of ["case 'patch': {", "case 'bpm': if (this.seq) this.seq.bpm = d.v; return;", "case 'loop': if (this.seq) this.seq.setLoop(d.l || null); return;"]) {
    const a = cp.indexOf(key), b = sc.indexOf(key); if (a < 0 || b < 0) { console.log(`FAIL handler text missing: ${key}`); bad = true; }
  }
  const patchBody = t => t.slice(t.indexOf("case 'patch': {"), t.indexOf("case 'vars':"));
  if (patchBody(cp) !== patchBody(sc)) { console.log('FAIL the patch handler of core-processor.js and core/seq-core.js differ'); bad = true; }
  for (const C of EDIT_SONGS) {
    const base = clone(j0.song), orig = clone(j0.song); if (C.base) C.base(base);
    const baseOps = diffOps(orig, base), mode = C.mode || 'song', opt = { ...(C.optx || {}) }; if (C.phraseOf) opt.phrase = C.phraseOf(base); if (C.loop) opt.loop = C.loop;
    // the edits as patch ops, against a shadow copy of the song as the sequencer has it
    let shadow = clone(base); const eds = C.edits.map(e => { if (!e.fn) return { ...e }; const nx = clone(shadow), r = e.fn(nx); void r; const ops = diffOps(shadow, nx); shadow = nx; return { after: e.after, patch: ops }; });
    const vars = vm.runInContext('({vtrk:false,NSLOT:16,TFX:{}})', H.ctx);
    H.U.load(vm.runInContext('(' + JSON.stringify(base) + ')', H.ctx), vars);
    H.ctx.__c = { mode, max: C.max || 20000, o: opt, eds: clone(eds) };
    const rows = vm.runInContext(`(() => { const d = __c, o = d.o; return __u.coreSeeded(() => { const out = [];
      const sq = __u.makeSeq(S, d.mode, {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:true, track:o.track, sect:o.sect, phrase:o.phrase});
      const P = Object.create(__u.ThunderCore.prototype); P.chase = null; P.seq = sq;
      for (let n = 0; sq.alive() && n < d.max; n++){ const r = sq.step(); const row = {t:__u.coreNum(r.t), evs:r.evs.map(__u.corePlain)};
        row.clk = {time:__u.coreNum(sq.time), base:__u.coreNum(sq.base), bt:__u.coreNum(sq.bt), btT:__u.coreNum(sq.btT), bpm:__u.coreNum(sq.bpm)};
        out.push(row);
        for (const e of d.eds) if (e.after === n + 1){
          if (e.patch) P.msg({op:'patch', ops:e.patch}); if (e.bpm != null) P.msg({op:'bpm', v:e.bpm}); if (e.loop) P.msg({op:'loop', l:e.loop});
          if (e.shift != null){ sq.base += e.shift; sq.time += e.shift; if (sq.btT != null) sq.btT += e.shift; } } }
      return out; }); })()`, H.ctx);
    const text = rows.map(r => JSON.stringify(r)).join('\n') + '\n', counts = {}; for (const r of rows) for (const e of r.evs) counts[e.type] = (counts[e.type] || 0) + 1;
    const now = { song: 'sounds/ELECTRO.thunder', mode, opt, baseOps, edits: eds, rows: rows.length, events: Object.values(counts).reduce((a, b) => a + b, 0), counts, sha256: sha(text),
      rowHashes: rows.map(r => crypto.createHash('sha256').update(JSON.stringify(r)).digest('hex').slice(0, 12)) };
    // the edits must have done something: a case whose edits change no event would prove nothing. Compare with the same run without the edits.
    H.U.load(vm.runInContext('(' + JSON.stringify(base) + ')', H.ctx), vars); H.ctx.__c.eds = [];
    const plain = vm.runInContext(`(() => { const d = __c, o = d.o; return __u.coreSeeded(() => { const out = [];
      const sq = __u.makeSeq(S, d.mode, {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:true, track:o.track, sect:o.sect, phrase:o.phrase});
      for (let n = 0; sq.alive() && n < d.max; n++){ const r = sq.step(); out.push(JSON.stringify({t:__u.coreNum(r.t), evs:r.evs.map(__u.corePlain)})); } return out; }); })()`, H.ctx);
    let differ = 0; for (let i = 0; i < rows.length; i++) if (JSON.stringify({ t: rows[i].t, evs: rows[i].evs }) !== plain[i]) differ++;
    now.rowsChangedByTheEdits = differ;
    if (process.env.SEQ_EDIT_DUMP) { fs.mkdirSync(process.env.SEQ_EDIT_DUMP, { recursive: true }); fs.writeFileSync(path.join(process.env.SEQ_EDIT_DUMP, `seq-edit-${C.id}.jsonl`), text); }   // (debugging: the JS rows, one a line)
    const gf = path.join(EDIT_DIR, `seq-edit-${C.id}.json`);
    if (UPDATE || !fs.existsSync(gf)) { fs.writeFileSync(gf, JSON.stringify(now) + '\n'); wrote.push(C.id); console.log(`wrote ${path.relative(ROOT, gf)}: ${now.rows} rows, ${now.events} events, ${differ} rows differ from the unedited run`); continue; }
    const was = JSON.parse(fs.readFileSync(gf, 'utf8')), ok = was.sha256 === now.sha256 && deq(was.edits, now.edits) && deq(was.baseOps, now.baseOps); if (!ok) bad = true;
    console.log(`${ok ? 'ok  ' : 'FAIL'} edit-${C.id}: ${now.rows} rows, ${differ} differ from the unedited run${ok ? '' : ' (first differing row ' + now.rowHashes.findIndex((h, k) => h !== was.rowHashes[k]) + ')'}`);
  }
  fs.writeFileSync(path.join(EDIT_DIR, 'seq-edit-cases.json'), JSON.stringify({ ids: EDIT_SONGS.map(c => c.id) }) + '\n');
  return !bad;
}

// CHOP CASES (docs/port/CHOP-NOTES.md; goldens native/tests/golden/seq-chop-*.json, read by ctest `seq_chop`; --chop).
// The page's chop is `applyChop` (index.html), which core/seq-core.js carries as an empty stub (the worklet has no pad data). Here the audio thread's makeSeq runs in a `vm` as in the
// live cases, but with sim OFF and with the stub line of core/seq-core.js replaced AT RUN TIME by `ensure` over this tool's own pad table, and the REAL text of loopSecs, findHits,
// sliceMemo, slicePoints and applyChop taken out of index.html by name (nothing is copied into this tool). The pad data (samples, kit layers) is the tool's own test data. The
// golden stores what the real functions return for the pads (loop length, the SMODE hits) so the C++ test hands in exactly that; the rows (events with ofsSec, durSec and st) are the JS's.
// PROOF FIRST: the four plain cases through this harness (sim off) must equal their committed goldens: ELECTRO has no sliced or synced instrument, so the real functions change nothing.
const CORE_STUBS = 'function ensurePlay(){ return null; } function ensure(){ return null; } function varWant(){} function applyChop(){}';
function chopSource() {
  const idx = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8'), core = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
  if (!core.includes(CORE_STUBS)) throw new Error('core/seq-core.js: the stub line changed');
  const fn = name => { const i = idx.indexOf('\nfunction ' + name + '('); if (i < 0) throw new Error('index.html: no function ' + name);
    let k = idx.indexOf('{', i), d = 0; for (; k < idx.length; k++) { const c = idx[k]; if (c === '{') d++; else if (c === '}' && --d === 0) break; } return idx.slice(i + 1, k + 1); };
  const memo = 'const sliceMemo = new Map();'; if (!idx.includes(memo)) throw new Error('index.html: no sliceMemo');
  const real = [fn('loopSecs'), fn('findHits'), memo, fn('slicePoints'), fn('applyChop')].join('\n');
  return core.replace(CORE_STUBS, 'function ensurePlay(i){ return __ensure(i); } function ensure(i){ return __ensure(i); } function varWant(){}') + '\n' + real +
    `\nconst __pads = []; function __ensure(i){ return kit.voices[i] ? __pads[i] || null : null; }
const __cp = e => { const o = {}; for (const k of Object.keys(e).sort()){ const v = e[k];
  if (typeof v === 'number') o[k] = coreNum(v); else if (typeof v === 'string' || typeof v === 'boolean' || v === null) o[k] = v;
  else if (k === 'st' && v){ const s = {}; for (const q of Object.keys(v).sort()) s[q] = typeof v[q] === 'number' ? coreNum(v[q]) : v[q]; o[k] = s; }
  else if (Array.isArray(v) && v.every(x => typeof x === 'number')) o[k] = v.map(coreNum); } return o; };
// the tool's own pad data: bursts every 0.25 s on a noise floor (so SMODE finds hits); pad 0 and 2 have a sample layer (loopSecs reads it), pad 1 none (the whole pad)
function __mk(secs, sr, seed){ const n = Math.round(secs * sr), d = new Float32Array(n); let s = seed; const r = () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296) - 0.5;
  for (let i = 0; i < n; i++){ const ph = (i / sr) % 0.25; d[i] = ph < 0.05 ? r() * Math.exp(-ph * 60) : r() * 0.01; } return d; }
function __setup(){ kit = {voices:[]};
  const L = (secs, sr, tune, fine, a, delay) => ({on:true, type:'sample', sample:{data:new Float32Array(Math.round(secs * sr)), sr}, tune, fine, a, delay});
  kit.voices[0] = {tune:0, layers:[L(2, 44100, -2, 10, 0.05, 15)]}; __pads[0] = {data:__mk(2.0, 48000, 11), sr:48000};
  kit.voices[1] = {tune:0, layers:[]}; __pads[1] = {data:__mk(1.6, 48000, 22), sr:48000};
  kit.voices[2] = {tune:0, layers:[L(1.5, 48000, 0, 0, 0, 0)]}; __pads[2] = {data:__mk(2.0, 48000, 33), sr:48000};
  kit.voices[3] = {tune:0, layers:[]}; __pads[3] = {data:__mk(1.0, 48000, 44), sr:48000}; sliceMemo.clear(); coreBufs.clear(); __pads.forEach((p, i) => coreBufs.set(i, p)); }   // (core/seq-core.js :912 gives ensure / ensurePlay over coreBufs: the pad table is put there)
function __padInfo(want){ const out = {}; for (const [pad, n, sm] of want){ const k = String(pad); const I = out[k] || (out[k] = {loop:loopSecs(pad, __pads[pad].data, __pads[pad].sr), hits:{}});
    if (sm) I.hits[n] = slicePoints(pad, {slc:n, smode:1}, __pads[pad].data, __pads[pad].sr); } return out; }
globalThis.__u = { makeSeq, coreSeeded, corePlain, coreNum, ThunderCore, __cp, __setup, __padInfo,
  load(s, v){ S = s; if (v){ if ("vtrk" in v) vtrk = v.vtrk; if ("NSLOT" in v) NSLOT = v.NSLOT; if (v.TFX) TFX = v.TFX; } busPlanH = null; busPlanVal = null; } };`;
}
function chopHarness() {
  const sctx = vm.createContext({ console, Math, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 48000, currentTime: 0, currentFrame: 0 });
  vm.runInContext(chopSource(), sctx, { filename: 'seq-core+applyChop.js' }); sctx.__u.__setup();
  return { ctx: sctx, U: sctx.__u };
}
// a song for the chop cases: ELECTRO's frame (mix, tfx, ... so the JS and C++ both load it), seven tracks, one phrase each, instruments 0-7 (see CHOP_INSTS)
const CHOP_INSTS = [   // [pad, fields]
  [0, { slc: 4 }], [1, { slc: 16 }], [2, { sync: 1, beats: 4 }], [1, { sync: 2, beats: 4, grain: 1 }], [0, { sync: 3, beats: 2, grain: 2 }], [0, { slc: 4, smode: 1 }], [2, { slc: 8 }], [3, {}]];
function chopSong(variant) {
  const j = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')), S = j.song, mine = ['song', 'chains', 'phrases', 'plen', 'insts'];
  for (const k of Object.keys(S)) if (!mine.includes(k) && JSON.stringify(S[k]).length > 4000) delete S[k];
  const tpl = S.insts[0]; S.insts = CHOP_INSTS.map(([pad, f]) => ({ ...tpl, pad, ...f })); S.bpm = 120; S.lpb = 4; S.ntr = 8;
  const row = (n, ins) => [n, -1, -1, -1, -1, ins, -1, -1, -1, -1, -1, -1], PH = [];
  const notes = [[48, 49, 50, 52, 46, 44, 51, 53, 48, 47, 45, 55, 54, 48, 56, 60], [48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48], [48, 55, 48, 60, 48, 52, 48, 40, 48, 48, 53, 48, 61, 48, 48, 50],
    [48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48, 48], [48, 50, 48, 48, 52, 48, 48, 48, 49, 48, 48, 48, 48, 55, 48, 48], [48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 47, 46, 45],
    [40, 44, 48, 52, 56, 60, 64, 68, 72, 36, 41, 47, 50, 53, 59, 66]];
  for (let t = 0; t < 7; t++) PH.push(Array.from({ length: 32 }, (_, y) => y < 16 ? row(notes[t][y], t) : row(-1, 0)));
  const cmd = (t, y, col, name, v) => { const r = PH[t][y]; r[6 + 2 * col] = FXN(name); r[7 + 2 * col] = v; };
  [0, 1, 2, 3, 15, 4, 20, 17, 5, 9, 11, 13, 14, 7, 31 % 16 + 12, 8].forEach((v, y) => cmd(1, y, 0, 'SLC', v));   // slc 16 with the SLC command (20 wraps)
  [64, 128, 200, 32, 255].forEach((v, k) => { cmd(3, 2 * k, 0, 'STA', v); cmd(4, 2 * k + 1, 0, 'STA', v); });   // sync 2 and 3 with STA (offset) notes
  cmd(2, 3, 0, 'STA', 100); cmd(0, 8, 0, 'TPO', 90); cmd(0, 12, 0, 'KIL', 2); cmd(6, 5, 0, 'RET', 0x24); cmd(6, 9, 1, 'KIL', 1);   // a bpm change mid-song, a KIL shorter than the slice, retriggers
  if (variant === 'sync-rev') PH.forEach((p, t) => p.forEach((r, y) => { if (r[0] >= 0 && y % 3 === 0) { r[6 + 2 * 2] = FXN('PLY'); r[7 + 2 * 2] = 1; } }));   // PLY 1: reversed notes
  S.phrases = PH; S.plen = PH.map(() => 16); S.chains = PH.map((_, t) => [[t, 0]]); S.song = Array.from({ length: 256 }, (_, r) => Array.from({ length: 33 }, (_, t) => r < 8 && t < 7 ? t : -1));
  return { v: j.v, song: S };
}
const CHOP_CASES = [
  { id: 'chop-all', variant: 'all', desc: 'slc 4 around the root, slc 16 + SLC, sync 1 on a loop at another tempo, sync 2 / 3 with STA, TPO mid-song, retriggers, KIL' },
  { id: 'chop-rev', variant: 'sync-rev', desc: 'the same song with PLY 1 on every third note (reversed: the offset counts from the far end)' },
  { id: 'chop-edit', variant: 'all', edits: [{ after: 20, fn: S => { S.insts[0].slc = 8; S.insts[5].slc = 6; } }, { after: 60, fn: S => { S.insts[2].sync = 0; S.insts[6].slc = 0; S.insts[7].sync = 1; } }],
    desc: 'a takeOver after edits that change slc (and sync on / off) of running instruments' },
];
function chopCases() {
  const H = chopHarness(), sha = t => crypto.createHash('sha256').update(t).digest('hex'); let bad = false;
  // 1. the proof: the four plain cases through THIS harness, sim OFF, equal their committed goldens
  for (const C of SONGS) {
    let t0 = fs.readFileSync(path.join(ROOT, C.file), 'utf8'); if (C.edit) { const j = JSON.parse(t0); C.edit(j); t0 = JSON.stringify(j); }
    const j = JSON.parse(t0); H.U.__setup(); H.U.load(vm.runInContext('(' + JSON.stringify(j.song) + ')', H.ctx), vm.runInContext('({vtrk:false,NSLOT:16,TFX:{}})', H.ctx));
    H.ctx.__c = { mode: C.mode, max: C.max || 20000, o: C.opt || {} };
    const rows = vm.runInContext(`(() => { const d = __c, o = d.o; return __u.coreSeeded(() => { const out = [];
      const sq = __u.makeSeq(S, d.mode, {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:false});
      for (let n = 0; sq.alive() && n < d.max; n++){ const r = sq.step(); out.push({t:__u.coreNum(r.t), evs:r.evs.map(__u.corePlain)}); } return out; }); })()`, H.ctx);
    const g = JSON.parse(fs.readFileSync(path.join(ROOT, `tests/golden/seq-${C.id}.json`), 'utf8')), ok = sha(rows.map(r => JSON.stringify(r)).join('\n') + '\n') === g.sha256 && rows.length === g.rows; if (!ok) bad = true;
    console.log(`${ok ? 'ok  ' : 'FAIL'} chop-harness proof ${C.id} (real applyChop, sim off): ${rows.length} rows ${ok ? '= the page\'s golden' : 'DIFFERS'}`);
  }
  if (bad) return false;
  // 2. the chop cases
  for (const C of CHOP_CASES) {
    const f = chopSong(C.variant), base = clone(f.song); let shadow = clone(base);
    const eds = (C.edits || []).map(e => { const nx = clone(shadow); e.fn(nx); const ops = diffOps(shadow, nx); shadow = nx; return { after: e.after, patch: ops }; });
    const want = [], addWant = S => S.insts.forEach(I => { if ((I.slc | 0) > 1 && I.smode && !want.some(w => w[0] === I.pad && w[1] === I.slc)) want.push([I.pad, I.slc, 1]); });
    addWant(base); let s2 = clone(base); for (const e of eds) { const a = clone(s2); for (const [k, i, v] of e.patch) { if (i === null) a[k] = v; else if (i === 'len') a[k].length = v; else if (i !== 'del') a[k][i] = v; else delete a[k]; } s2 = a; addWant(s2); }
    H.U.__setup(); const pads = H.ctx.__want = want, info = JSON.parse(JSON.stringify(vm.runInContext('__u.__padInfo(__want).constructor === Object ? __u.__padInfo(__want) : null', H.ctx)));
    for (const p of [0, 1, 2, 3]) if (!info[p]) info[p] = JSON.parse(JSON.stringify(vm.runInContext(`__u.__padInfo([[${p}, 2, 0]])`, H.ctx)))[p];
    H.U.__setup(); H.U.load(vm.runInContext('(' + JSON.stringify(base) + ')', H.ctx), vm.runInContext('({vtrk:false,NSLOT:16,TFX:{}})', H.ctx));
    H.ctx.__c = { max: 20000, eds: clone(eds) };
    const rows = vm.runInContext(`(() => { const d = __c; return __u.coreSeeded(() => { const out = [];
      const sq = __u.makeSeq(S, 'song', {row:0, loop:null, once:true, sim:false});
      const P = Object.create(__u.ThunderCore.prototype); P.chase = null; P.seq = sq;
      for (let n = 0; sq.alive() && n < d.max; n++){ const r = sq.step(); out.push({t:__u.coreNum(r.t), evs:r.evs.map(__u.__cp)});
        for (const e of d.eds) if (e.after === n + 1) P.msg({op:'patch', ops:e.patch}); }
      return out; }); })()`, H.ctx);
    const text = rows.map(r => JSON.stringify(r)).join('\n') + '\n', counts = {}; let chopped = 0, stN = 0;
    if (process.env.CHOP_DUMP) fs.writeFileSync(path.join(process.env.CHOP_DUMP, C.id + '.jsonl'), text);   // (debugging: the JS rows, one a line)
    for (const r of rows) for (const e of r.evs) { counts[e.type] = (counts[e.type] || 0) + 1; if (e.type === 'note') { if (e.ofsSec != null) chopped++; if (e.st) stN++; } }
    const now = { desc: C.desc, fileText: JSON.stringify(f), pads: info, edits: eds, rows: rows.length, events: Object.values(counts).reduce((a, b) => a + b, 0), counts, chopped, stretched: stN, sha256: sha(text),
      rowHashes: rows.map(r => crypto.createHash('sha256').update(JSON.stringify(r)).digest('hex').slice(0, 12)) };
    if (process.env.CHOP_DEBUG) console.log(JSON.stringify(counts), JSON.stringify(rows.flatMap(r => r.evs).filter(e => e.type === 'note').slice(0, 3)));
    if (!chopped || (C.variant === 'all' && !stN)) { console.log(`FAIL ${C.id}: no chopped notes (${chopped}) / no st notes (${stN}): the case proves nothing`); bad = true; continue; }
    const gf = path.join(LIVE_DIR, `seq-${C.id}.json`);
    if (UPDATE || !fs.existsSync(gf)) { fs.writeFileSync(gf, JSON.stringify(now) + '\n'); console.log(`wrote ${path.relative(ROOT, gf)}: ${now.rows} rows, ${now.events} events, ${chopped} chopped notes, ${stN} with st`); continue; }
    const was = JSON.parse(fs.readFileSync(gf, 'utf8')), ok = was.sha256 === now.sha256 && deq(was.pads, now.pads) && was.fileText === now.fileText && deq(was.edits, now.edits); if (!ok) bad = true;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${C.id}: ${now.rows} rows, ${chopped} chopped notes, ${stN} with st`);
  }
  return !bad;
}
if (process.argv.includes('--chop')) { process.exit(chopCases() ? 0 : 1); }
const LIVE_OK = liveCases();
if (process.argv.includes('--edit')) { const ok = editCases(); process.exit(LIVE_OK && ok ? 0 : 1); }
if (process.argv.includes('--live-only')) process.exit(LIVE_OK ? 0 : 1);
const TIMING = process.argv.includes('--timing');
const CORE = process.argv.includes('--core');   // run each case on the audio thread (core/seq-core.js) instead: the same goldens must match
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', 'C:/Program Files/Google/Chrome/Application/chrome.exe', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: npm i -g playwright, or set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.webmanifest': 'application/manifest+json' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const port = srv.address().port;
const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = !LIVE_OK;
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  page.on('pageerror', e => console.error('page error:', e.message));
  await page.goto(`http://localhost:${port}/index.html?seqtest=1`);
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
  await page.waitForTimeout(1500);   // let the app's own first-run work settle
  fs.mkdirSync(path.join(ROOT, 'tests/golden/out'), { recursive: true });
  for (const S of SONGS) {
    let text0 = fs.readFileSync(path.join(ROOT, S.file), 'utf8');
    if (S.edit) { const j = JSON.parse(text0); const n = S.edit(j); text0 = JSON.stringify(j); console.log(`     ${S.id}: ${n} commands added`); }
    const info = await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [text0, path.basename(S.file)]);
    const rows = await page.evaluate(([m, mx, o, c]) => c ? window.__seqTest.runCore(m, mx, o) : window.__seqTest.run(m, mx, o), [S.mode, S.max || 0, S.opt || null, CORE]);
    const text = rows.map(r => JSON.stringify(r)).join('\n') + '\n';
    fs.writeFileSync(path.join(ROOT, `tests/golden/out/seq-${S.id}.jsonl`), text);
    const counts = {}; for (const r of rows) for (const e of r.evs) counts[e.type] = (counts[e.type] || 0) + 1;
    const now = { song: S.file, mode: S.mode, bpm: info.bpm, tracks: info.nt, rows: rows.length, events: Object.values(counts).reduce((a, b) => a + b, 0), counts,
      sha256: crypto.createHash('sha256').update(text).digest('hex'), rowHashes: rows.map(r => crypto.createHash('sha256').update(JSON.stringify(r)).digest('hex').slice(0, 12)) };
    const gf = path.join(ROOT, `tests/golden/seq-${S.id}.json`);
    if (UPDATE || !fs.existsSync(gf)) { fs.writeFileSync(gf, JSON.stringify(now, null, 1) + '\n'); console.log(`wrote ${path.relative(ROOT, gf)}: ${now.rows} rows, ${now.events} events`); continue; }
    const was = JSON.parse(fs.readFileSync(gf, 'utf8'));
    if (was.sha256 === now.sha256) { console.log(`ok   ${S.id}: ${now.rows} rows, ${now.events} events`); continue; }
    failed = true;
    const i = now.rowHashes.findIndex((h, k) => h !== was.rowHashes[k]);
    console.log(`FAIL ${S.id}: rows ${was.rows} → ${now.rows}, events ${was.events} → ${now.events}; first difference at step ${i < 0 ? Math.min(was.rows, now.rows) : i}`);
    // (reporting only) the goldens keep counts and row hashes, not the streams, so say what can be said from those
    const delta = Object.keys({ ...was.counts, ...counts }).map(k => [k, (counts[k] || 0) - (was.counts?.[k] || 0)]).filter(([, d]) => d).map(([k, d]) => `${k} ${d > 0 ? '+' : ''}${d}`);
    const bad = now.rowHashes.map((h, k) => h !== was.rowHashes[k] ? k : -1).filter(k => k >= 0);
    console.log(`  events by type, golden → now: ${delta.length ? delta.join(', ') : '(counts equal: values, order or times differ)'}; ${bad.length} of ${now.rows} rows differ, first steps: ${bad.slice(0, 10).join(' ')}`);
    if (i >= 0) {   // the first differing row, one short line per event (notes: track, pad, mn, rate; others: type, p, track, v), with its step and time
      const short = e => e.type === 'note' ? `note t${e.track} pad${e.pad} mn${e.mn} rate${e.rate}` : `${e.type}${e.p ? ':' + e.p : ''} t${e.track ?? '-'} ${e.v ?? e.b ?? ''}@${e.t}`;
      console.log(`  now, step ${i} (t=${rows[i].t}): ${rows[i].evs.map(short).join(' | ').slice(0, 900)}`);
    }
    console.log('  (the golden has no events to diff against: to see exactly what changed, run the last good commit and diff the two out/*.jsonl files)');
    console.log(`  full stream: tests/golden/out/seq-${S.id}.jsonl (compare with a run of the last good commit)`);
  }
  if (TIMING) {   // plan section 12: how far ahead notes are booked, idle and with the main thread stalled
    await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'), 'ELECTRO.thunder']);
    await page.mouse.click(5, 5);   // a gesture, so audio may start
    for (const [stall, every] of [[-1, 0], [0, 0], [100, 1000], [200, 1000], [400, 1000]]) {
      const r = await page.evaluate(([a, b]) => window.__seqTest.timing(12000, Math.max(0, a), b), [stall, every]); if (stall < 0) { console.log('timing  (warm-up run, renders the song: ' + r.audio_ms.late + ' late)'); continue; }
      console.log(`timing  stall ${stall} ms every ${every || '-'} ms: ` + JSON.stringify(r));
    }
  }
} finally { await browser.close(); srv.close(); }
process.exit(failed ? 1 : 0);
