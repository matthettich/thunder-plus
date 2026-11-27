// U07: sequencer cases the four committed goldens do not reach (they play ELECTRO, which has no PERFORM data and seven kinds of
// command), run in the JS (seq-dump.mjs --case) and in C++ (test_seq --case) and compared row by row, exactly as the goldens are.
//   node native/tools/seq-extra.mjs OUTDIR PATH/TO/test_seq [only-this-id]
// Each case is ELECTRO changed in memory by a seeded program (so the JS and the C++ read the same file from OUTDIR): commands of
// every kind in the free command columns, PERFORM scenes / macros / locks / LFOs / parts, key follow and human instruments, a
// scale, a tuning, muted master blocks; played as a song (render, live with the wrap, from the middle, BUS PLAY's set of
// tracks, with JMP and END), as a sect and as a phrase (with loops), with a seed and with a voice per track.
// The cases are compared "full": with what corePlain drops (the eng event's c and o, tw, slide, bend, vib, arp).
// Nothing here is a committed golden: the JS is run fresh each time, so this needs Node (ctest `seq_extra`).
import fs from 'node:fs';
import path from 'node:path';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url)), ROOT = path.resolve(here, '../..');
const out = path.resolve(process.argv[2] || '/tmp/seq-extra'), exe = process.argv[3], onlyId = process.argv[4] || null;
fs.mkdirSync(out, { recursive: true });
const FXC = ['ARP','ARC','CHA','DEL','GRV','GGR','HOP','INS','KIL','OFF','NTH','RET','REP','RND','RNL','PSL','PBN','PVB','SED','TPO','TSP','VOL','PIT','FIN','PLY','STA','PAN','TIC','WOB','FXO','FLX','GAT','RTG',
  'CCA','CCB','CCC','CCD','PGM','BYP','CSL','DIR','MUT','GEN','PRB','FXS','FXV','SCA','SCG','CUT','RES','DEC','FDI','TRM','APN','TVL','TPN','STP','LPB','PDL','ONE','MPB','MAT','CND','NUD','RTV','SLC','XFD','MAC','LK1','LK2','LK3','LK4','PRT','END','JMP','FLY','ATK','REL','TCH','SKP','CHD','KN1','KN2','KN3','KN4','KN5','KN6','KN7','KN8','FLZ'];
const rng = seed => () => { let x = seed += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; };
const base = () => JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));

// commands into the free second and third command columns (and some rows without a note), from `pool`, values by kind
function sprinkle(j, seed, pool, dens = 0.6) {
  const R = rng(seed), val = c => { const r = Math.floor(R() * 256);
    return c === 'TPO' ? 100 + r % 80 : c === 'TIC' ? r % 13 : c === 'LPB' ? r % 9 : c === 'PDL' ? r % 3 : c === 'HOP' ? r % 32 : c === 'DIR' ? r % 5 : c === 'PRT' ? r % 4 : c === 'INS' ? r % 70
      : c === 'CND' ? ((r & 3) << 4) | (r >> 4) % 10 : c === 'TCH' || c === 'CHA' || c === 'PRB' ? [0, 64, 128, 200, 255][r % 5] : c === 'SED' ? r % 6 : c === 'SKP' || c === 'CHD' ? r % 2 : c === 'JMP' ? r % 12 : r; };
  let n = 0;
  j.song.phrases.forEach(ph => { if (!Array.isArray(ph)) return; ph.forEach(r => { if (!Array.isArray(r)) return;
    for (const k of [8, 10]) { if (r[k] >= 0 || R() > (r[0] >= 0 ? dens : dens / 6)) continue; const c = pool[Math.floor(R() * pool.length)]; r[k] = FXC.indexOf(c); r[k + 1] = val(c); n++; } }); });
  return n;
}
const ALL = FXC.filter(c => c !== 'END' && c !== 'JMP');
const STEER = ['TPO', 'TIC', 'LPB', 'PDL', 'HOP', 'DIR', 'GRV', 'GGR'];
const NOTEY = ALL.filter(c => !STEER.includes(c));

// synthetic TFX (coreTfx()'s shape): every effect type in the song with its numeric fields as parameters 0-255, and one pump type
function tfxOf(j) {
  const T = {}; const see = f => { if (!f || T[f.type] || f.type === 'pumpx') return; T[f.type] = { pump: false, mod: false, cv: null, ps: Object.keys(f).filter(k => typeof f[k] === 'number').map(k => ({ k, min: 0, max: k === 'div' ? 8 : 255 })) }; };
  (j.song.tfx || []).forEach(A => (A || []).forEach(see)); (j.song.mfx || []).forEach(see);
  T.pumpx = { pump: true, mod: false, cv: null, ps: [{ k: 'thr', min: 0, max: 255, lfo: true, pi: 0 }, { k: 'gain', min: 0, max: 255 }, { k: 'bands', min: 0, max: 255, lfo: true, pi: 1, pb: [1, 2, 3] }],
    pd: [{ s: 'thr', min: -60, max: 0, def: -20, scale: 'lin', skew: 0, flags: 0 }, { s: 'b1', min: 20, max: 20000, def: 200, scale: 'log', skew: 0, flags: 0 }, { s: 'b2', min: 0, max: 10, def: 4, scale: 'pow', skew: 2, flags: 0 }, { s: 'b3', min: 0, max: 12, def: 3, scale: 'lin', skew: 0, flags: 2 }] };
  return T;
}
function perform(j) {
  const S = j.song;
  S.tfx[1][2] = { type: 'pumpx', on: true, pump: { thr: -33, b1: 1500, b3: 7 } };
  S.tfx[5][0] = { type: 'flex', on: true, pat: 3, mix: 200, xf: 20 }; S.tfx[5][1] = { type: 'sgate', on: false, mix: 255 }; S.tfx[8][3] = { type: 'retrig', on: true, mode: 0, div: 2, decay: 38, pitch: 24, gate: 255, mix: 255 };
  const sc = Array.from({ length: 16 }, () => ({ n: '', v: {} }));
  sc[2].v = { 'vol:0': 100, 'pan:1': 30, 'cut:2': 90, 'fx:3:0:1': 200, 'tch:4': 128, 'vol:32': 200, 'env:0': 50, 'fx:32:0:2': 99, 'fx:1:2:0': 40, 'fx:1:2:2:1': 77, 'bogus:1': 3, 'vol:x': 1, 'dec:8': 60 };
  sc[5].v = { 'pan:1': 220, 'vol:0': 240, 'res:2': 40, 'dec:8': 100, 'atk:6': 30, 'fx:5:0:1': 10, 'fx:1:2:2:0': 12, 'fx:9:9:9': 4, 'tch:8': 200, 'cut:0': 180 };
  const mac = Array.from({ length: 8 }, (_, i) => ({ n: 'M' + (i + 1), v: (i * 37) % 256, tg: [] }));
  mac[0].tg = [{ id: 'vol:2', lo: 40, hi: 250 }, { id: 'fx:8:3:4', lo: 0, hi: 255 }]; mac[1].tg = [{ id: 'cut:4', lo: 255, hi: 20 }, { id: 'pan:1', lo: 0, hi: 255 }]; mac[3].tg = [{ id: 'rel:0', lo: 10, hi: 90 }, { id: 'tch:9', lo: 255, hi: 60 }]; mac[7].tg = [{ id: 'vol:32', lo: 100, hi: 255 }];
  const lk = { 0: ['vol:0', 'tch:0', 'cut:0', 'fx:5:0:1'], 2: ['pan:2', null, 'tch:2', 'dec:2'], 4: ['tch:4', 'res:4', 'vol:32', 'sus:4'], 8: ['cut:8', 'pan:8', 'fx:8:3:2', 'tch:8'], 9: ['vol:9', 'vol:9', 'atk:9', 'pan:3'] };
  const lfo = { 0: [{ id: 'vol:0', sh: 0, rt: 3, dp: 80 }, { id: 'cut:0', sh: 1, rt: 4, dp: 120 }, { id: 'vol:0', sh: 5, rt: 6, dp: 30 }, { id: 'env:0', sh: 2, rt: 2, dp: 100 }],
    1: [{ id: 'pan:1', sh: 3, rt: 5, dp: 200 }, { id: 'fx:1:2:0', sh: 4, rt: 1, dp: 60 }, { id: 'fx:1:2:2:2', sh: 1, rt: 7, dp: 90 }],
    2: [{ id: 'res:2', sh: 5, rt: 8, dp: 255 }, { id: 'tch:2', sh: 2, rt: 0, dp: 180 }, null, { id: 'nope', sh: 0, rt: 3, dp: 9 }, { id: 'rel:3', sh: 1, rt: 99, dp: 44 }],
    6: [{ id: 'fx:5:0:2', sh: 0, rt: 4, dp: 77 }, { id: 'dec:6', sh: 4, rt: 3, dp: 140 }, { id: 'atk:6', sh: 3, rt: 2, dp: 50 }, { id: 'sus:7', sh: 2, rt: 5, dp: 20 }],
    12: [{ id: 'vol:12', sh: 1, rt: 4, dp: 100 }, { id: 'cut:12', sh: 0, rt: 3, dp: 90 }], 14: [{ id: 'pan:14', sh: 2, rt: 6, dp: 64 }], 20: [{ id: 'vol:20', sh: 0, rt: 3, dp: 10 }],
    32: [{ id: 'vol:32', sh: 0, rt: 2, dp: 40 }, { id: 'fx:32:1:1', sh: 1, rt: 5, dp: 120 }] };
  S.perf = { sc, xa: 2, xb: 5, xf: 77, mac, lk, lfo };
  const insts = JSON.parse(JSON.stringify(S.insts)); insts.forEach((I, i) => { I.vol = 255 - (i * 5) % 120; I.tsp = (i % 5) - 2; I.pan = (i * 29) % 256; I.ofs = (i * 3) % 40; I.rev = i % 4 === 1 ? 1 : 0; });
  const fxp = {}; S.tfx.forEach((A, t) => (A || []).forEach((f, s) => { if (f && f.type !== 'pumpx') { const o = { type: f.type }; for (const k of Object.keys(f)) if (typeof f[k] === 'number') o[k] = (f[k] * 7 + t + s) % 256; fxp[t + ':' + s] = o; } }));
  fxp['32:0'] = { type: 'retrig', mode: 1, div: 3, decay: 99, mix: 128 }; fxp['3:0'] = { type: 'not-this' }; fxp['40:1'] = { type: 'flex', pat: 2 };
  S.parts = [{ insts, mix: S.mix.map((m, t) => [(m.vol + 31 * t) % 256, (m.pan + 17 * t) % 256]), fx: fxp }, null, { insts: insts.slice(0, 4), mix: [[10, 20], [null, ''], ['77', 300]], fx: {} }];
}
function instruments(j) {
  const S = j.song; S.insts.forEach((I, i) => { if (i % 3 === 0) I.kf = 1; if (i % 4 === 1) I.hut = 5 + i % 40; if (i % 5 === 2) I.huv = 10 + (i * 7) % 80; if (i % 6 === 3) I.tsp = (i % 7) - 3; });
  S.scale = 5; S.sroot = 3; S.tune = [0, -12.5, 7, 0, 33, -50, 0, 4.25, 0, 0, 18, -3];
  S.smuteM = [1, 3, 9]; S.nmono = 5;
}
function withEndAndJmp(j, jmpRow, to, endRow) {   // a JMP in the master column's phrase at song row jmpRow, an END further on
  const S = j.song, put = (y, cmd, v) => { for (let t = 0; t < 33; t++) { const cid = S.song[y][t], ch = cid >= 0 ? S.chains[cid] : null, pid = ch && ch[0] ? ch[0][0] : -1, ph = pid >= 0 ? S.phrases[pid] : null;
      if (!ph) continue; const np = S.phrases.findIndex((p, i) => i > 300 && !p), c2 = S.chains.findIndex((p, i) => i > 300 && !p);   // its own copies, so other rows keep theirs
      S.phrases[np] = JSON.parse(JSON.stringify(ph)); S.plen[np] = S.plen[pid]; S.plpb[np] = S.plpb[pid]; S.phrases[np][20][6] = FXC.indexOf(cmd); S.phrases[np][20][7] = v;
      S.chains[c2] = JSON.parse(JSON.stringify(ch)); S.chains[c2][0][0] = np; S.song[y][t] = c2; return; } };
  put(jmpRow, 'JMP', to); put(endRow, 'END', 0);
}

const mk = (id, edit, extra = {}) => ({ id, edit, ...extra });
const CASES = [
  mk('x-all', j => { sprinkle(j, 11, NOTEY); }),
  mk('x-all-steer', j => { instruments(j); sprinkle(j, 12, ALL, 0.5); }, { max: 2500 }),
  mk('x-perf', j => { perform(j); instruments(j); sprinkle(j, 13, ['XFD', 'MAC', 'LK1', 'LK2', 'LK3', 'LK4', 'PRT', 'CUT', 'RES', 'DEC', 'ATK', 'REL', 'TCH', 'CHA', 'FXV', 'FXS', 'CND', 'NTH', 'VOL', 'INS', 'SED', 'CHD', 'SKP', 'PRB', 'ONE'], 0.7); }),
  mk('x-perf-plain', j => { perform(j); }),
  mk('x-perf-live', j => { perform(j); sprinkle(j, 14, NOTEY, 0.3); }, { opt: { once: false, live: true }, max: 2600 }),
  mk('x-wrap', j => { sprinkle(j, 15, ['DIR', 'HOP', 'GRV', 'GGR', 'TIC', 'NTH', 'CND', 'REP', 'RND', 'RNL', 'ARP', 'ARC', 'RET', 'RTV', 'NUD', 'DEL'], 0.5); }, { opt: { once: false }, max: 3000 }),
  mk('x-mid', j => { instruments(j); sprinkle(j, 16, ['SED', 'TCH', 'SKP', 'CHD', 'CHA', 'PRB', 'RND', 'ONE', 'DIR', 'ARC', 'ARP'], 0.8); }, { opt: { row: 14 } }),
  mk('x-seed', j => { sprinkle(j, 17, ['TCH', 'CHA', 'PRB', 'CHD', 'SKP', 'CND'], 0.8); }, { opt: { seed: 123456789, row: 3 } }),
  mk('x-jmp', j => { withEndAndJmp(j, 6, 2, 30); sprinkle(j, 18, ['NTH', 'CND', 'CHA', 'TCH', 'CUT', 'SED'], 0.5); }, { opt: { once: false }, max: 1500 }),
  mk('x-jmp-once', j => { withEndAndJmp(j, 6, 12, 20); }),
  mk('x-jmp-back-once', j => { withEndAndJmp(j, 6, 2, 30); }),
  mk('x-at', j => { sprinkle(j, 25, ['LPB', 'TCH', 'NTH'], 0.1); }, { opt: j => { const S = j.song, q = (t, row, step) => { const c = S.chains[S.song[row][t]]; return { row, cr: 0, phrase: c ? c[0][0] : -1, step }; };
      const at = new Array(33).fill(null); at[0] = q(0, 4, 5); at[1] = q(1, 4, 9); at[2] = q(2, 5, 3); at[4] = q(4, 3, 30); at[8] = q(8, 6, 0); at[6] = { row: 300, cr: 0, phrase: 1, step: 0 }; at[32] = q(32, 5, 2); return { row: 4, once: false, at, atRef: 0 }; }, max: 900 }),
  mk('x-only', j => { perform(j); }, { opt: { only: [0, 2, 4, 8], once: false }, max: 1500 }),
  mk('x-loop', j => { sprinkle(j, 19, ALL, 0.4); }, { opt: { loop: { y0: 2, y1: 11 } }, max: 1500 }),
  mk('x-vtrk', j => { sprinkle(j, 20, NOTEY, 0.2); }, { vars: { vtrk: true } }),
  mk('x-sect', j => { perform(j); sprinkle(j, 21, ALL, 0.5); }, { mode: 'sect', opt: { sect: 4, track: 8, once: false }, max: 700 }),
  mk('x-sect-loop', j => { sprinkle(j, 22, NOTEY, 0.5); }, { mode: 'sect', opt: { sect: 0, track: 0, loop: { y0: 0, y1: 2 } }, max: 500 }),
  mk('x-phrase', j => { perform(j); sprinkle(j, 23, ALL, 0.7); }, { mode: 'phrase', opt: { phrase: 9, track: 9, once: false }, max: 700 }),
  mk('x-phrase-loop', j => { sprinkle(j, 24, ['DIR', 'HOP', 'NTH', 'CND', 'RND', 'ARP', 'TIC', 'GRV'], 0.9); }, { mode: 'phrase', opt: { phrase: 3, track: 2, loop: { y0: 4, y1: 13 } }, max: 600 }),
];

let bad = 0;
for (const C of CASES) {
  if (onlyId && C.id !== onlyId) continue;
  const j = base(); C.edit(j); if (typeof C.opt === 'function') C.opt = C.opt(j);
  const songFile = path.join(out, C.id + '.thunder'); fs.writeFileSync(songFile, JSON.stringify(j));
  const cf = path.join(out, C.id + '.case.json');
  fs.writeFileSync(cf, JSON.stringify({ id: C.id, full: true, file: songFile, mode: C.mode || 'song', opt: C.opt || {}, max: C.max || 0, vars: { vtrk: false, NSLOT: 16, TFX: tfxOf(j), ...(C.vars || {}) } }));
  const js = execFileSync(process.execPath, [path.join(here, 'seq-dump.mjs'), '--case', cf, out], { encoding: 'utf8' }).trim();
  if (!exe) { console.log(js); continue; }
  const r = spawnSync(exe, ['--case', cf, out], { encoding: 'utf8' });
  if (r.status !== 0) { bad++; console.log('FAIL ' + C.id + '\n  JS: ' + js + '\n' + (r.stdout || '').slice(0, 6000) + (r.stderr || '').slice(0, 2000)); }
  else console.log(r.stdout.trim() + '   [' + js.replace(/^[^,]*, sha256 [0-9a-f]*, /, '') + ']');
}
console.log(bad ? `seq_extra: ${bad} case(s) differ` : 'seq_extra: every case equal');
process.exit(bad ? 1 : 0);
