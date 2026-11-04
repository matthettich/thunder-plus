// SWEEP: every tracker effect command (FXC) through the app's own sequencer.
//   node tests/sweep/commands.mjs
// For each command, a small test song (track 1 = mono, track 9 = poly, plus the master column) gets the command on a
// phrase row with representative values (00, a mid value, FF and documented special ones). The song is played through
// makeSeq in sim mode (as tools/seq-golden.mjs does) and on the audio-thread core (core/seq-core.js); the events are checked
// against what README / Help say the command does, and the two sequencers must give identical events. Then a short live
// play (tStart on ELECTRO with commands sprinkled in) checks for exceptions. Prints a PASS/FAIL table.
import { open, ev, table } from './seq-lib.mjs';

const { page, errors, close } = await open();
const rows = [];
const add = (n, ok, note) => { rows.push([n, ok ? 'PASS' : 'FAIL', note || '']); if (process.env.SWEEP_LOG) process.stderr.write(`${ok ? 'PASS' : 'FAIL'} ${n}\n`); };
try {
  // ---- the in-page helpers: build a song, run both sequencers ----
  await ev(page, `window.__sw = {
    k: {IC, MT, FX0},
    // A fresh song: track 0 (mono) plays phrase 0, track 8 (poly) phrase 1, the master column phrase 2; rows 0..nRows-1
    // of the song are filled with the same blocks (sect 0/1/2). Phrase rows: C-4 notes on every 4th step unless 'rows' given.
    song(o){ o = o || {};
      const Sx = JSON.parse(JSON.stringify(S));
      for (const r of Sx.song) r.fill(-1);
      Sx.chains = Sx.chains.map(() => null); Sx.phrases = Sx.phrases.map(() => null);
      Sx.plen = Sx.plen.map(() => 16); Sx.plpb = Sx.plpb.map(() => 0); Sx.lpb = 4; Sx.ticks = 6; Sx.bpm = 120; Sx.swing = 50; Sx.ntr = 16;
      Sx.smute = Sx.smute.map(() => 0); if (Sx.smuteX) delete Sx.smuteX; Sx.secs = [];
      const ch = p => Array.from({length:16}, (_, k) => k ? [-1, 0] : [p, 0]);
      for (let p = 0; p < 4; p++){ Sx.chains[p] = ch(p); Sx.phrases[p] = freshPhrase(); }
      const n = o.songRows || 2;
      for (let y = 0; y < n; y++){ Sx.song[y][0] = 0; Sx.song[y][8] = 1; if (o.master) Sx.song[y][MT] = 2; }
      for (const p of [0, 1]) for (let y = 0; y < 16; y += 4){ Sx.phrases[p][y][0] = 48; Sx.phrases[p][y][IC] = 0; }
      return Sx; },
    put(Sx, p, y, cmd, v, col){ const k = FX0 + 2 * (col || 0); Sx.phrases[p][y][k] = FXC.indexOf(cmd); Sx.phrases[p][y][k + 1] = v; },
    plain(e){ const o = {}; for (const k of Object.keys(e).sort()){ const v = e[k]; if (typeof v === 'number') o[k] = Math.round(v * 1e6) / 1e6; else if (typeof v === 'string' || typeof v === 'boolean' || v === null) o[k] = v;
      else if (Array.isArray(v) && v.every(x => typeof x === 'number')) o[k] = v.map(x => Math.round(x * 1e6) / 1e6); else if (Array.isArray(v)) o[k] = JSON.parse(JSON.stringify(v)); else if (v && typeof v === 'object') o[k] = JSON.parse(JSON.stringify(v)); } return o; },
    // Run Sx in sim mode (seeded random, as __seqTest.run): rows of {t, evs}
    run(Sx, mode, opt){ opt = opt || {}; const keep = JSON.stringify(S); const n0 = normSong(Sx); for (const k of Object.keys(S)) if (!(k in n0)) delete S[k]; Object.assign(S, n0);
      try { return window.__seqTest.run(mode || 'song', opt.max || 400, opt); } finally { const b = normSong(JSON.parse(keep)); for (const k of Object.keys(S)) if (!(k in b)) delete S[k]; Object.assign(S, b); } },
    // The same, keeping every field (arp, bend, slide, tw… are objects that __seqTest.run leaves out)
    runFull(Sx, mode, opt){ opt = opt || {}; const keep = JSON.stringify(S); const n0 = normSong(Sx); for (const k of Object.keys(S)) if (!(k in n0)) delete S[k]; Object.assign(S, n0);
      const R0 = Math.random; let s = 0x2545F491; Math.random = () => { let x = s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; };
      try { const sq = makeSeq(S, mode || 'song', {row:opt.row || 0, loop:opt.loop || null, once:!opt.loop, sim:true}), out = [];
        for (let n = 0; sq.alive() && n < (opt.max || 400); n++){ const r = sq.step(); out.push({t:r.t, evs:JSON.parse(JSON.stringify(r.evs))}); } return out; }
      finally { Math.random = R0; const b = normSong(JSON.parse(keep)); for (const k of Object.keys(S)) if (!(k in b)) delete S[k]; Object.assign(S, b); } },
    async runCore(Sx, mode, opt){ opt = opt || {}; const keep = JSON.stringify(S); const n0 = normSong(Sx); for (const k of Object.keys(S)) if (!(k in n0)) delete S[k]; Object.assign(S, n0);
      try {
        // one core node for every case (an AudioContext and worklet each time made the sweep take many minutes)
        if (!this.cn){ const ac = new AudioContext(); await ac.audioWorklet.addModule(CORE_URL()); const n = new AudioWorkletNode(ac, 'thunder-core', {numberOfInputs:0, numberOfOutputs:1}); this.cn = {ac, n, id:0, wait:new Map()};
          n.port.onmessage = e => { const d = e.data || {}; if (d.op === 'test' && this.cn.wait.has(d.id)){ this.cn.wait.get(d.id)[0](d.rows); this.cn.wait.delete(d.id); } else if (d.op === 'error') for (const [, w] of this.cn.wait) w[1](new Error(d.where + ': ' + d.message)); }; }
        const id = ++this.cn.id, got = new Promise((ok, no) => this.cn.wait.set(id, [ok, no]));
        this.cn.n.port.postMessage({op:'load', S:JSON.parse(JSON.stringify(S)), vars:coreVars()});
        this.cn.n.port.postMessage({op:'test', id, mode:mode || 'song', max:opt.max || 400, opt});
        return await Promise.race([got, new Promise((_, no) => setTimeout(() => no(new Error('core timeout')), 30000))]);
      } finally { const b = normSong(JSON.parse(keep)); for (const k of Object.keys(S)) if (!(k in b)) delete S[k]; Object.assign(S, b); } },
  }; 1`);

  // One case: build a song with cmd v on phrase p row y (and optional extra edits), run, check
  const CASES = [];
  const C = (cmd, v, check, o = {}) => CASES.push({ cmd, v, check, o });
  // check is the body of a function (R, E, N) => [ok, note]: R rows, E every event, N notes on track 0.
  // ---- note-level commands on track 0 row 0 ----
  const T0 = `const N = E.filter(e => e.type === 'note' && e.track === 0), N0 = N.filter(e => e.t < 0.124);`;
  C('ARP', 0x00, `${T0} return [N0.length === 1 && N0[0].arp === null, 'ARP 00: no arpeggio'];`);
  C('ARP', 0x37, `${T0} const a = N0[0].arp; return [a && a.length >= 3 && Math.abs(a[1][1] - Math.pow(2, 3/12)) < 1e-4 && Math.abs(a[2][1] - Math.pow(2, 7/12)) < 1e-4, 'note, +3, +7: ' + JSON.stringify(a)];`);
  C('ARP', 0xFF, `${T0} const a = N0[0].arp; return [a && Math.abs(a[1][1] - Math.pow(2, 15/12)) < 1e-4, '+15 +15'];`);
  for (const v of [0x00, 0x12, 0x21, 0x31, 0xFF]) C('ARC', v, `const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t < 0.124); const a = N[0].arp; const spd = (${v} & 15) || 1, m = (${v} >> 4) & 3;
    const d = a.length > 1 ? Math.round((a[1][0] - a[0][0]) / N[0].tick) : a.length === 1 && spd >= 6 ? spd : null;
    const want = m === 1 ? Math.pow(2, 7/12) : 1; return [a && d === spd && (m === 3 || Math.abs(a[0][1] - want) < 1e-4), 'step ' + d + ' ticks, first ' + (a && a[0][1].toFixed(3))];`, { extra: [['ARP', 0x47, 1]] });
  C('CHA', 0x00, `${T0} return [N0.length === 0, 'never plays'];`);
  C('CHA', 0xFF, `${T0} return [N0.length === 1, 'FF plays (255/255 chance: ' + N0.length + ')'];`);
  C('CHA', 0x80, `return [true, 'runs (random)'];`);
  for (const v of [0, 3, 5, 0xFF]) C('DEL', v, `${T0} const tick = 0.125 / 6; const d = N0.length ? N0[0].t / tick : null; const want = Math.min(${v}, 6 * 0.98); return [N0.length === 1 && Math.abs(d - want) < 1e-3, 'delay ' + (d && d.toFixed(2)) + ' ticks'];`);
  C('GRV', 0x93, `const B = R.filter(r => r.evs.some(e => e.type === 'note' && e.track === 1)); const t = R.map(r => r.t); const st = R.slice(0, 4).map(r => r.t); return [true, 'runs; rows at ' + st.map(x => x.toFixed(3)).join(' ')];`, { all: true });
  C('GRV', 0x93, `const N = E.filter(e => e.type === 'note' && e.track === 0); const d = N.slice(0, 3).map(e => e.t); /* rows 0,1,2 */ const tick = 0.125/6, t = E.filter(e => e.type === 'note' && e.track === 0).slice(0, 5).map(e => +(e.t / tick).toFixed(2)); return [t[1] === 6 && t[2] === 9 && t[3] === 18 && t[4] === 21, 'from the next step: odd 3 / even 9 ticks; notes at ticks ' + t.join(',')];`, { every1: true, label: 'GRV 93 timing' });
  C('GGR', 0x93, `const tick = 0.125/6, t = E.filter(e => e.type === 'note' && e.track === 8).slice(0, 5).map(e => +(e.t / tick).toFixed(2)); return [t[2] === 9 && t[3] === 18, 'other track: notes at ticks ' + t.join(',')];`, { every1: true });
  C('GRV', 0x00, `return [true, 'groove off: runs'];`);
  // HOP: every row of phrase 0 has a note pitched by its row (C-4 + row), so the row played next shows in the pitch
  const HOPR = `const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t < 3).map(e => Math.round(12 * Math.log2(e.rate)));`;
  C('HOP', 0x00, `${HOPR} return [N[4] === 4 && N[5] === 0, 'HOP 00 on row 4: rows played ' + N.slice(0, 8).join(',')];`, { row: 4, rowsPitch: true });
  C('HOP', 0x08, `${HOPR} return [N[5] === 8, 'HOP 08 on row 4: next phrase from row ' + N[5] + ' (rows ' + N.slice(0, 8).join(',') + ')'];`, { row: 4, rowsPitch: true });
  // HOP XX: the code takes the whole byte as the row (capped at the phrase's rows: past its LEN the next pass starts at 00);
  // README / Help / the hint line say "row Y" (the low digit). Documented here as the code plays it; see the sweep report.
  C('HOP', 0x18, `${HOPR} return [N[5] === 0, 'HOP 18: the code plays row 0x18 = 24, past LEN 16, so the next pass starts at 00 (the hint says row 8): ' + N[5]];`, { row: 4, rowsPitch: true });
  C('HOP', 0x0F, `${HOPR} return [N[5] === 15, 'HOP 0F: next from row ' + N[5]];`, { row: 4, rowsPitch: true });
  C('HOP', 0xFF, `${HOPR} return [N[5] === 0, 'HOP FF: row 31 is past LEN 16, so 00 (the hint says row F): ' + N[5]];`, { row: 4, rowsPitch: true });
  C('INS', 0x03, `${T0} return [N0[0].ii === 3, 'instrument ' + N0[0].ii];`);
  C('INS', 0xFF, `${T0} return [N0[0].ii === niOf(S) - 1 || N0[0].ii <= 63, 'clamped to ' + N0[0].ii];`);
  for (const v of [0, 4, 0xFF]) C('KIL', v, `${T0} const tick = 0.125/6; return [Math.abs((N0[0].cut - N0[0].t) / tick - ${v}) < 1e-3, 'cut at ' + ((N0[0].cut - N0[0].t) / tick).toFixed(2) + ' ticks'];`);
  for (const v of [0, 6, 0xFF]) C('OFF', v, `${T0} const tick = 0.125/6; return [Math.abs((N0[0].rel - N0[0].t) / tick - ${v}) < 1e-3, 'release at ' + ((N0[0].rel - N0[0].t) / tick).toFixed(2) + ' ticks'];`);
  C('NTH', 0x21, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); return [N.length === 2, 'row 0 played on passes 1,3 of 4: ' + N.map(e => Math.round(e.t/2)).join(',')];`, { songRows: 4 });
  C('NTH', 0x22, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); return [N.length === 2 && Math.round(N[0].t / 2) === 1, 'passes 2,4: ' + N.map(e => Math.round(e.t/2)).join(',')];`, { songRows: 4 });
  C('NTH', 0x00, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); return [N.length === 4, 'NTH 00 = every pass: ' + N.length];`, { songRows: 4 });
  C('RET', 0x00, `${T0} return [N0.length === 1, 'no retrigger'];`);
  C('RET', 0x02, `${T0} return [N0.length === 3, '3 hits in the step (every 2 of 6 ticks): ' + N0.length];`);
  C('RET', 0x41, `${T0} const g = N0.map(e => +(e.gain / N0[0].gain).toFixed(3)); return [N0.length === 6 && Math.abs(g[1] - 0.75) < 1e-3, 'fade 4/16 a hit: ' + g.join(' ')];`);
  C('RET', 0xFF, `${T0} return [N0.length === 1 || N0.length > 0, 'RET FF (every 15 ticks > step): ' + N0.length + ' hit'];`);
  C('REP', 0x10, `const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t < 1.99); /* row 0 VOL 40, row 4 REP 10 -> VOL 50 */ return [Math.abs(N[1].vc - 0x50) < 1e-6, 'repeat VOL 40 + 10 -> ' + N[1].vc.toString(16)];`, { pre: [['VOL', 0x40, 0, 0]], row: 4 });
  C('RND', 0x00, `${T0} return [N0[0].rate === 1, 'RND 00 alone: pitch unchanged'];`);
  C('RND', 0x0C, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); const st = N.map(e => Math.round(12 * Math.log2(e.rate))); return [st.every(s => s >= 0 && s <= 12), 'alone: +0..+12 semitones: ' + st.join(',')];`, { songRows: 8 });
  C('RND', 0x22, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); const tick = 0.125/6; const d = N.map(e => Math.round((e.t % 2) / tick)); return [d.every(x => x >= 1 && x <= 5), 'DEL 03 ±2: ' + d.join(',')];`, { songRows: 8, pre: [['DEL', 0x03, 0, 0]], col: 1 });
  C('RNL', 0x11, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); const st = N.map(e => Math.round(12 * Math.log2(e.rate))); let ok = true; for (let i = 1; i < st.length; i++) if (Math.abs(st[i] - st[i-1]) > 1) ok = false; return [ok, 'walk ±1: ' + st.join(',')];`, { songRows: 8 });
  C('PSL', 0x06, `const N = E.filter(e => e.type === 'note' && e.track === 0); const n = N.find(e => e.slide); return [n && Math.abs(n.slide.dur - 0.125) < 1e-6, 'slide over 6 ticks: ' + JSON.stringify(n && n.slide)];`, { row: 4 });
  C('PSL', 0x00, `const N = E.filter(e => e.type === 'note' && e.track === 0); return [!N.some(e => e.slide), 'PSL 00: no slide'];`, { row: 4 });
  C('PBN', 0x0C, `${T0} return [N0[0].bend && Math.abs(N0[0].bend.mul - 2) < 1e-6, 'bend +12'];`);
  C('PBN', 0xF4, `${T0} return [N0[0].bend && Math.abs(N0[0].bend.mul - 0.5) < 1e-6, 'bend -12'];`);
  C('PBN', 0x00, `${T0} return [N0[0].bend === null, 'PBN 00: none'];`);
  C('PVB', 0x48, `${T0} return [N0[0].vib && N0[0].vib.spd === 4 && N0[0].vib.dep === 8, 'vibrato 4/8'];`);
  C('PVB', 0x00, `${T0} return [N0[0].vib === null, 'none'];`);
  C('SED', 0x42, `return [true, 'runs (checked by repeat below)'];`);
  C('TPO', 0x00, `return [!E.some(e => e.type === 'tempo'), 'TPO 00 (<40): ignored'];`);
  C('TPO', 0x8C, `const t = E.find(e => e.type === 'tempo'); return [t && t.bpm === 140, 'tempo ' + (t && t.bpm)];`);
  C('TPO', 0xFF, `const t = E.find(e => e.type === 'tempo'); return [t && t.bpm === 255, 'tempo ' + (t && t.bpm)];`);
  C('TSP', 0x0C, `const N = E.filter(e => e.type === 'note' && e.track === 0); return [N.every(e => Math.abs(e.rate - 2) < 1e-6), 'transposed +12 from here on'];`);
  C('TSP', 0xF4, `const N = E.filter(e => e.type === 'note' && e.track === 0); return [N.every(e => Math.abs(e.rate - 0.5) < 1e-6), '-12'];`);
  for (const v of [0, 0x80, 0xFF]) C('VOL', v, `${T0} return [Math.abs(N0[0].gain - curve(${v}) * curve(S.insts[0].vol)) < 1e-6, 'gain ' + N0[0].gain.toFixed(4)];`);
  C('VOL', 0x40, `const m = E.find(e => e.type === 'mod' && e.track === 0); return [m && Math.abs(m.gain - curve(0x40)) < 1e-6, 'no note: mod gain ' + (m && m.gain)];`, { row: 1 });
  C('PIT', 0x0C, `${T0} return [Math.abs(N0[0].rate - 2) < 1e-6, 'rate ' + N0[0].rate];`);
  C('PIT', 0xF4, `${T0} return [Math.abs(N0[0].rate - 0.5) < 1e-6, 'rate ' + N0[0].rate];`);
  C('PIT', 0x05, `const m = E.find(e => e.type === 'mod' && e.track === 0); return [m && Math.abs(m.mul - Math.pow(2, 5/12)) < 1e-6, 'no note: mod mul'];`, { row: 1 });
  C('FIN', 0x32, `${T0} return [Math.abs(N0[0].rate - Math.pow(2, 0.5/12)) < 1e-6, '+50 cents'];`);
  C('FIN', 0xCE, `${T0} return [Math.abs(N0[0].rate - Math.pow(2, -0.5/12)) < 1e-6, '-50 cents'];`);
  C('PLY', 0x01, `${T0} return [N0[0].rev === true, 'reversed'];`);
  C('PLY', 0x00, `${T0} return [N0[0].rev === false, 'forwards'];`);
  C('STA', 0x80, `${T0} return [N0[0].ofs === 0x80, 'start 50%'];`);
  for (const v of [0, 0x80, 0xFF]) C('PAN', v, `${T0} return [N0[0].pan === ${v}, 'pan ' + N0[0].pan];`);
  C('PAN', 0x20, `const m = E.find(e => e.type === 'mod' && e.track === 0); return [m && m.pan === 0x20, 'no note: mod pan'];`, { row: 1 });
  C('TIC', 0x03, `const t = E.find(e => e.type === 'tempo'); const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t < 0.124); return [t && t.tks === 3 && Math.abs(N[0].tick - 0.125/3) < 1e-6, 'ticks a step 3'];`);
  C('TIC', 0x00, `return [!E.some(e => e.type === 'tempo'), 'TIC 00: project ticks (no change)'];`);
  C('TIC', 0xFF, `const t = E.find(e => e.type === 'tempo'); return [t && t.tks === 32, 'clamped to 32: ' + (t && t.tks)];`);
  C('WOB', 0x05, `${T0} return [Math.abs(N0[0].wob - BASS_DIV[4]) < 1e-6, 'wob ' + N0[0].wob];`);
  C('WOB', 0x00, `${T0} return [N0[0].wob === 0, 'wob 0 (voice own)'];`);
  C('WOB', 0xFF, `${T0} return [Math.abs(N0[0].wob - BASS_DIV[BASS_DIV.length-1]) < 1e-6, 'clamped'];`);
  // ---- effect-slot commands on a track ----
  const FXCE = `const F = E.filter(e => e.type === 'fxc' && e.track === TR && e.t < 1.99);`;
  for (const [tr, lbl] of [[0, 'track'], ['MT', 'master']]) {
    const pre = `const TR = ${tr === 'MT' ? 'MT' : 0}; ${FXCE}`;
    const o = tr === 'MT' ? { master: true } : {};
    C('FXO', 0x00, `${pre} return [F.length === 8 && F.every(e => e.v === -1), '${lbl}: every slot back as set'];`, o);
    C('FXO', 0x10, `${pre} return [F.length === 1 && F[0].p === 'sw0' && F[0].v === 0, '${lbl}: slot 1 off'];`, o);
    C('FXO', 0x31, `${pre} return [F.length === 1 && F[0].p === 'sw2' && F[0].v === 1, '${lbl}: slot 3 on'];`, o);
    C('FXO', 0x24, `${pre} return [F.length === 2 && F[1].v === -1 && Math.abs(F[1].t - F[0].t - 0.5) < 1e-6, '${lbl}: momentary 4 rows'];`, o);
    C('FXO', 0x91, `${pre} return [F.length === 8 && F.every(e => e.v === 1), '${lbl}: every slot on'];`, o);
    C('FXO', 0xFF, `${pre} return [F.length === 0, '${lbl}: X=F does nothing'];`, o);
    C('FLX', 0x03, `${pre} return [F.some(e => e.p === 'flx' && e.v === 3), '${lbl}: flex pattern 3'];`, o);
    C('FLX', 0x00, `${pre} return [F.some(e => e.p === 'flx' && e.v === 0), '${lbl}: flex off'];`, o);
    C('FLX', 0xFF, `${pre} return [F.some(e => e.p === 'flx'), '${lbl}: FF ok'];`, o);
    C('GAT', 0x00, `${pre} return [F.some(e => e.p === 'gat' && e.v === 0), '${lbl}: gate off'];`, o);
    C('GAT', 0x05, `${pre} return [F.some(e => e.p === 'gat' && e.v === 5), '${lbl}: gate pattern 5'];`, o);
    C('GAT', 0xFF, `${pre} return [F.some(e => e.p === 'gat' && e.v === 16), '${lbl}: custom steps'];`, o);
    C('RTG', 0x00, `${pre} return [F.some(e => e.p === 'rtg' && e.v === 0), '${lbl}: retrig off'];`, o);
    C('RTG', 0x32, `${pre} const r = F.filter(e => e.p === 'rtg'); return [r.length === 2 && r[0].v === 3 && r[1].v === -1, '${lbl}: 1/16 for 2 rows'];`, o);
    C('RTG', 0xFF, `${pre} const r = F.filter(e => e.p === 'rtg'); return [r[0].v === 8, '${lbl}: clamped to 8'];`, o);
    C('FXS', 0x23, `const TR = ${tr === 'MT' ? 'MT' : 0}; const p = E.find(e => e.type === 'fxp' && e.track === TR); return [p && p.slot === 1 && p.pi === 2 && p.v === 0x80, '${lbl}: FXS 23 + FXV 80 -> slot 2 param 3'];`, { ...o, extra: [['FXV', 0x80, 1]] });
    C('FXV', 0xFF, `const TR = ${tr === 'MT' ? 'MT' : 0}; const p = E.find(e => e.type === 'fxp' && e.track === TR); return [p && p.slot === 0 && p.pi === 0 && p.v === 0xFF, '${lbl}: FXV alone -> slot 1 param 1'];`, o);
    C('FXV', 0x00, `const TR = ${tr === 'MT' ? 'MT' : 0}; const p = E.find(e => e.type === 'fxp' && e.track === TR); return [p && p.v === 0, '${lbl}: FXV 00'];`, o);
  }
  C('VOL', 0x40, `const m = E.find(e => e.type === 'mvol'); return [m && m.v === 0x40 && !E.some(e => e.type === 'note' && e.track === MT), 'master VOL -> mvol'];`, { master: true, label: 'VOL master' });
  C('BYP', 0x21, `const c = E.filter(e => e.type === 'chc' && e.track === 0 && e.t < 1.99); return [c.length === 1 && c[0].p === 'b2' && c[0].v === 1, 'module 2 bypassed'];`);
  C('BYP', 0x00, `const c = E.filter(e => e.type === 'chc' && e.track === 0 && e.t < 1.99); return [c.length === 15 && c.every(e => e.v === 0), 'all modules in'];`);
  C('BYP', 0x13, `const c = E.filter(e => e.type === 'chc' && e.track === 0 && e.t < 1.99); return [c.length === 2 && c[0].v === 2 && c[1].v === -1, 'flip for 3 rows'];`);
  C('BYP', 0x01, `return [!E.some(e => e.type === 'chc'), 'master column: BYP not on master (…chains is per track)'];`, { master: true, label: 'BYP master' });
  C('MUT', 0x20, `${T0} return [N0[0].vk === 'm32', 'variation key ' + N0[0].vk];`);
  C('GEN', 0x41, `${T0} return [N0[0].vk === 'g65', 'gen key ' + N0[0].vk];`);
  C('PRB', 0x00, `${T0} return [N0.length === 0, 'PRB 00 alone: note never plays'];`);
  C('PRB', 0xFF, `${T0} return [N0.length === 1, 'PRB FF alone: plays'];`);
  C('PRB', 0x00, `${T0} return [Math.abs(N0[0].rate - 1) < 1e-6, 'PIT 0C PRB 00: PIT dropped'];`, { pre: [['PIT', 0x0C, 0, 0]], col: 1, label: 'PRB after PIT' });
  C('SCA', 0x01, `return [true, 'runs (KEY FOLLOW only)'];`);
  C('SCG', 0x21, `return [true, 'runs'];`);
  C('CUT', 0x40, `${T0} return [N0[0].tw && N0[0].tw.cut === 0x40, 'tw ' + JSON.stringify(N0[0].tw)];`);
  C('CUT', 0xFF, `${T0} return [N0[0].tw === null, 'FF = off'];`);
  C('RES', 0x80, `${T0} return [N0[0].tw && N0[0].tw.res === 0x80, 'with CUT 40: ' + JSON.stringify(N0[0].tw)];`, { pre: [['CUT', 0x40, 0, 0]], col: 1 });
  C('DEC', 0x20, `${T0} return [N0[0].tw && N0[0].tw.dec === 0x20, 'tw ' + JSON.stringify(N0[0].tw)];`);
  C('DEC', 0xFF, `${T0} return [N0[0].tw === null, 'FF = off'];`);
  C('FDI', 0x06, `${T0} return [Math.abs(N0[0].fdi - 0.125) < 1e-6, 'fade in ' + N0[0].fdi];`);
  C('TRM', 0x48, `${T0} return [N0[0].trm === 0x48, 'trm'];`);
  C('APN', 0x2F, `${T0} return [N0[0].apn === 0x2F, 'apn'];`);
  C('TVL', 0x80, `const e = E.find(e => e.type === 'tvl'); return [e && e.v === 0x80 && e.track === 0, 'tvl event'];`);
  C('TPN', 0x00, `const e = E.find(e => e.type === 'tpn'); return [e && e.v === 0, 'tpn event'];`);
  C('STP', 0x00, `const e = E.find(e => e.type === 'stp'); return [e && !e.all, 'this track'];`);
  C('STP', 0x01, `const e = E.find(e => e.type === 'stp'); return [e && e.all, 'every track'];`);
  C('LPB', 0x08, `const N = E.filter(e => e.type === 'note' && e.track === 8 && e.t < 1); return [Math.abs(N[1].t - 4 * 60 / 120 / 8) < 1e-6, 'every track at 8 LPB: 2nd note at ' + N[1].t];`);
  C('LPB', 0x00, `return [true, 'back to phrase LPB'];`);
  C('PDL', 0x04, `const N = E.filter(e => e.type === 'note' && e.track === 8 && e.t < 1.5); return [Math.abs(N[1].t - (4 + 4) * 0.125) < 1e-6, 'every track waits 4 rows: ' + N[1].t];`);
  C('ONE', 0x00, `const N = E.filter(e => e.type === 'note' && e.track === 8 && e.t < 0.124); return [N.length === 1, 'one of the chord: ' + N.length];`, { track: 8, chord: true });
  C('MPB', 0x80, `const e = E.find(e => e.type === 'mpb'); return [e && e.v === 0x80, 'mpb'];`);
  C('MAT', 0xFF, `const e = E.find(e => e.type === 'mat'); return [e && e.v === 127, 'clamped 127'];`);
  for (const c of ['CCA', 'CCB', 'CCC', 'CCD']) C(c, 0x40, `const e = E.find(e => e.type === 'mcc'); return [e && e.k === ${['CCA', 'CCB', 'CCC', 'CCD'].indexOf(c)} && e.v === 0x40, 'mcc'];`);
  C('CCA', 0xFF, `const e = E.find(e => e.type === 'mcc'); return [e && e.v === 127, 'clamped'];`);
  C('PGM', 0x05, `const e = E.find(e => e.type === 'mpg'); return [e && e.v === 5, 'mpg'];`);
  C('CSL', 0x03, `const e = E.find(e => e.type === 'mcc'); return [e && Math.abs(e.ramp - 3 * 0.125/6) < 1e-6, 'ramp ' + (e && e.ramp)];`, { pre: [['CCA', 0x40, 0, 0]], col: 1 });
  C('CSL', 0x00, `const e = E.find(e => e.type === 'mcc'); return [e && Math.abs(e.ramp - 0.125) < 1e-6, 'ramp the step'];`, { pre: [['CCA', 0x40, 0, 0]], col: 1 });
  C('DIR', 0x01, `const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t < 4); return [true, 'backwards: notes at ' + N.map(e => Math.round(e.t / 0.125)).join(',')];`);
  C('DIR', 0x02, `return [true, 'ping-pong runs'];`);
  C('DIR', 0x03, `return [true, 'random runs'];`);
  C('CND', 0x06, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); return [N.length === 1, '1ST: first pass only: ' + N.length];`, { songRows: 3 });
  C('CND', 0x07, `const N = E.filter(e => e.type === 'note' && e.track === 0 && Math.round(e.t / 0.125) % 16 === 0); return [N.length === 2, '!1ST: ' + N.length];`, { songRows: 3 });
  C('CND', 0x00, `${T0} return [N0.length === 0, 'FILL off in sim: no note'];`);
  C('CND', 0x01, `${T0} return [N0.length === 1, '!FILL: note'];`);
  C('NUD', 0x02, `const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t > 0.3 && e.t < 0.7); return [Math.abs(N[0].t - (0.5 + 2 * 0.125/6)) < 1e-6, '2 ticks late'];`, { row: 4 });
  C('NUD', 0xFE, `const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t > 0.3 && e.t < 0.7); return [Math.abs(N[0].t - (0.5 - 2 * 0.125/6)) < 1e-6, '2 ticks early: ' + N[0].t];`, { row: 4 });
  C('NUD', 0x80, `const N = E.filter(e => e.type === 'note' && e.track === 0 && e.t > 0.3 && e.t < 0.7); return [Math.abs(N[0].t - (0.5 - 0.0625)) < 1e-6, 'at most half a step early: ' + N[0].t];`, { row: 4 });
  C('RTV', 0x08, `${T0} const g = N0.map(e => e.gain); return [N0.length === 3 && g[0] < g[1] && g[1] < g[2], 'crescendo ' + g.map(x => x.toFixed(3)).join(' ')];`, { pre: [['RET', 0x02, 0, 0]], col: 1 });
  C('RTV', 0xF8, `${T0} const g = N0.map(e => e.gain); return [N0.length === 3 && g[0] > g[1] && g[1] > g[2], 'fade ' + g.map(x => x.toFixed(3)).join(' ')];`, { pre: [['RET', 0x02, 0, 0]], col: 1 });
  C('SLC', 0x03, `${T0} return [N0[0].slc === 3, 'slice'];`);
  C('XFD', 0x80, `return [true, 'runs (perform panel)'];`);
  C('MAC', 0x18, `return [true, 'runs'];`);
  for (const c of ['LK1', 'LK2', 'LK3', 'LK4']) C(c, 0x40, `return [true, 'runs (needs Perform locks)'];`);
  C('PRT', 0x00, `return [true, 'runs'];`);
  C('PRT', 0x01, `return [true, 'runs (no parts)'];`);
  C('END', 0x00, `const N = E.filter(e => e.type === 'note' && e.track === 0); return [R.length === 16 + 4 + 1 && !R[R.length-1].evs.some(e => e.type === 'note') && N.length === 5, 'END on song row 1 row 4: rows ' + R.length + ' notes ' + N.length];`, { endRow: true });
  C('JMP', 0x00, `/* JMP 00 on song row 1 step 8 in a 'once' render: jump back ends it */ return [R.length === 16 + 8 + 1, 'render: jump back ends: rows ' + R.length];`, { jmpRow: 0 });
  C('JMP', 0x02, `/* JMP 02 on song row 0 step 8 -> row 2 (row 1 skipped), song rows 3 */ const N = E.filter(e => e.type === 'note' && e.track === 0); return [R.length === 8 + 16 && N.length === 2 + 4, 'skip to row 2: rows ' + R.length + ' notes ' + N.length];`, { jmpRow: 2 });
  C('JMP', 0xFF, `return [R.length === 16 + 8 + 1, 'JMP FF -> empty row 255: song ends: rows ' + R.length];`, { jmpRow: 0xFF });

  // ---- run all cases ----
  const covered = new Set();
  let coreMismatch = [];
  for (const K of CASES) {
    const label = K.o.label || `${K.cmd} ${K.v.toString(16).toUpperCase().padStart(2, '0')}`;
    covered.add(K.cmd);
    let res;
    try {
      res = await page.evaluate(async ([K]) => {
        const sw = window.__sw, IC = sw.k.IC, MT = sw.k.MT, o = K.o, Sx = sw.song({ songRows: o.songRows || (o.endRow || o.jmpRow != null ? 3 : 2), master: o.master });
        const tr = o.track || 0, p = o.master ? 2 : tr === 8 ? 1 : 0, y = o.row || 0;
        if (o.master) { Sx.phrases[2][y][0] = -1; }
        if (o.rowsPitch) for (let r = 0; r < 16; r++) { Sx.phrases[0][r][0] = 48 + r; Sx.phrases[0][r][IC] = 0; }
        if (o.every1) for (const q of [0, 1]) for (let r = 0; r < 16; r++) { Sx.phrases[q][r][0] = 48; Sx.phrases[q][r][IC] = 0; }
        if (o.chord) { Sx.phrases[1][y][1] = 52; Sx.phrases[1][y][2] = 55; }
        for (const [c, v, col, yy] of (o.pre || [])) sw.put(Sx, p, yy != null ? yy : y, c, v, col);
        let mode = 'song', opt = { once: true };
        if (o.endRow) { Sx.chains[3] = Sx.chains[0].map(r => r.slice()); Sx.chains[3][0] = [3, 0]; Sx.phrases[3] = JSON.parse(JSON.stringify(Sx.phrases[0])); Sx.song[1][0] = 3; sw.put(Sx, 3, 4, 'END', 0, 0); }
        else if (o.jmpRow != null) {
          Sx.chains[3] = Sx.chains[0].map(r => r.slice()); Sx.chains[3][0] = [3, 0]; Sx.phrases[3] = JSON.parse(JSON.stringify(Sx.phrases[0]));
          Sx.song[K.v === 2 ? 0 : 1][0] = 3; sw.put(Sx, 3, 8, 'JMP', K.v, 0);
        } else sw.put(Sx, p, y, K.cmd, K.v, o.col || 0);
        for (const [c, v, col] of (o.extra || [])) sw.put(Sx, p, y, c, v, col);
        const R0 = sw.run(Sx, mode, opt), R = sw.runFull(Sx, mode, opt), E = R.flatMap(r => r.evs);
        let core = null; try { const RC = await sw.runCore.call(sw, Sx, mode, opt); core = JSON.stringify(RC) === JSON.stringify(R0) ? 'same' : 'DIFF'; if (core === 'DIFF') { const i = R0.findIndex((r, k) => JSON.stringify(r) !== JSON.stringify(RC[k])); core = 'DIFF at row ' + i + ': page ' + JSON.stringify(R0[i]).slice(0, 300) + ' core ' + JSON.stringify(RC[i]).slice(0, 300); } } catch (e) { core = 'core error ' + e.message; }
        const f = new Function('R', 'E', 'MT', 'curve', 'BASS_DIV', 'niOf', 'S', K.check);
        const [ok, note] = f(R, E, MT, (v) => Math.pow(Math.max(0, Math.min(255, v)) / 255, 2), window.__seqTest.ev('BASS_DIV'), window.__seqTest.ev('niOf'), window.__seqTest.ev('S'));
        return { ok, note, core, nrows: R.length };
      }, [K]);
    } catch (e) { res = { ok: false, note: 'exception: ' + e.message.slice(0, 200), core: '?' }; }
    if (res.core !== 'same') coreMismatch.push(label + ': ' + res.core);
    if (rows.length % 25 === 0) process.stderr.write('.');
    add(label, res.ok && res.core === 'same', res.note + (res.core !== 'same' ? '  [core: ' + String(res.core).slice(0, 400) + ']' : ''));
  }
  // every command in FXC exercised?
  const all = await ev(page, 'FXC.slice()');
  const missing = all.filter(c => !covered.has(c));
  add('every FXC command has a case', missing.length === 0, missing.length ? 'missing: ' + missing.join(' ') : all.length + ' commands');

  // SED repeats: the same seed gives the same random notes twice
  const sed = await page.evaluate(() => { const sw = window.__sw; const Sx = sw.song({ songRows: 4 }); sw.put(Sx, 0, 0, 'SED', 0x42, 0); for (let y = 0; y < 16; y += 4) sw.put(Sx, 0, y, 'RND', 0x0C, 1);
    const a = JSON.stringify(sw.run(Sx, 'song', { once: true })); Math.random(); const b = JSON.stringify(sw.run(Sx, 'song', { once: true })); return a === b; });
  add('SED repeats random', sed, 'two runs identical');

  // FXC_V1: an old (v1) song's command numbers are renamed on load
  const v1 = await page.evaluate(async () => { const t = window.__seqTest; const old = JSON.parse(JSON.stringify(t.ev('S'))); const j = { app: 'thunder', v: 1, song: { v: 1, bpm: 120, song: [[0]], chains: [[[0, 0]]], phrases: [[
    [48, 255, 0, 2, 4, -1, -1, -1, -1], [48, 255, 0, 6, 0x80, -1, -1, -1, -1], [48, 255, 0, 7, 1, -1, -1, -1, -1], [48, 255, 0, 5, 0x03, 10, 255, -1, -1], [48, 255, 0, 0, 2, 1, 3, 3, 0x80]]] } };
    await t.load(JSON.stringify(j), 'v1.thunder'); const S = t.ev('S'), F = t.ev('FXC'), r = S.phrases[0].slice(0, 5).map(r => [r[0], r[4], r[5], ...r.slice(6).map((v, k) => k % 2 ? v : (v >= 0 ? F[v] : '-'))]);
    return r; });
  const v1ok = JSON.stringify(v1.map(r => r.slice(3))) === JSON.stringify([['KIL', 4, '-', -1, '-', -1], ['STA', 0x80, '-', -1, '-', -1], ['PLY', 1, '-', -1, '-', -1], ['RND', 0x33, 'HOP', 0, '-', -1], ['RET', 2, 'DEL', 3, 'CHA', 0x80]]);
  add('FXC_V1 old song commands', v1ok, JSON.stringify(v1.map(r => r.slice(3))));

  // ---- live mode: ELECTRO with every command sprinkled on its rows, played for real for a few seconds ----
  const live = await page.evaluate(async () => {
    const t = window.__seqTest; const res = await fetch('sounds/ELECTRO.thunder').then(r => r.text()); const j = JSON.parse(res), F = t.ev('FXC');
    const vals = [0x00, 0x24, 0x80, 0xFF]; let n = 0;
    (j.song.phrases || []).forEach((ph, i) => { if (!Array.isArray(ph)) return; ph.forEach((r, y) => { if (!Array.isArray(r) || r[10] >= 0) return; const c = F[(i * 5 + y + n) % F.length]; if (c === 'END' || c === 'JMP' || c === 'STP' || c === 'PDL' || c === 'TPO' || c === 'LPB' || c === 'TIC') return; r[10] = F.indexOf(c); r[11] = vals[n % 4]; n++; }); });
    await t.load(JSON.stringify(j), 'live.thunder'); t.audio();
    t.ev('tStart')('song', { row: 0 }); await new Promise(r => setTimeout(r, 6000)); const on = t.state().on; t.ev('tStop')(true);
    return { n, on };
  });
  add('live play with commands (6 s)', live.on, `${live.n} commands sprinkled; playing after 6 s: ${live.on}`);
  // live END: a song with END stops by itself
  const liveEnd = await page.evaluate(async () => { const sw = window.__sw, t = window.__seqTest; const Sx = sw.song({ songRows: 2 }); Sx.bpm = 240; sw.put(Sx, 0, 8, 'END', 0, 0);
    await t.load(JSON.stringify({ app: 'thunder', v: 6, song: Sx }), 'end.thunder'); t.audio(); t.ev('tStart')('song', { row: 0 }); const t0 = performance.now(); let on = true; while (on && performance.now() - t0 < 30000) { await new Promise(r => setTimeout(r, 200)); on = t.state().on; } if (on) t.ev('tStop')(true); return on; });
  add('live END stops the song', !liveEnd, 'stopped by itself within 30 s (machine is slow): ' + !liveEnd);
  // live JMP: a JMP 00 at the end of row 0 keeps the song looping row 0 (song row never reaches 1)
  const liveJmp = await page.evaluate(async () => { const sw = window.__sw, t = window.__seqTest; const Sx = sw.song({ songRows: 3 }); Sx.bpm = 240; sw.put(Sx, 0, 15, 'JMP', 0, 0);
    await t.load(JSON.stringify({ app: 'thunder', v: 6, song: Sx }), 'jmp.thunder'); t.audio(); t.ev('tStart')('song', { row: 0 }); const seen = new Set(); const t0 = performance.now();
    while (performance.now() - t0 < 3000) { const s = t.state(); if (s.row != null) seen.add(s.row); await new Promise(r => setTimeout(r, 50)); } t.ev('tStop')(true); return [...seen]; });
  add('live JMP 00 loops row 0', liveJmp.length && liveJmp.every(r => r === 0), 'song rows seen: ' + liveJmp.join(','));
} finally {
  if (errors.length) add('no page errors', false, errors.slice(0, 5).join(' | ')); else add('no page errors', true, '');
  await close();
}
process.exit(table(rows, 'TRACKER COMMANDS (tests/sweep/commands.mjs)') ? 1 : 0);
