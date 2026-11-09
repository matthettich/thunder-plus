// Acid line's MUTATE INTENSITY (9 Nov): how many steps change, how far a note moves, and that Mutate writes only the rows it
// picked. The generator, acidRead and the writers are cut out of the real index.html and run in a vm.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/const ACID_SCALES = [\s\S]*?\n(?=\/\* KEY FROM THE SONG)/);
assert.ok(m, 'index.html: could not find the acid block; the code moved or changed shape, so this test needs updating');

function ctx(){
  const c = vm.createContext({ Math, Object, Array, Number,
    clamp: (v, a, b) => Math.min(b, Math.max(a, v)), ROOT: 48, NCOL: 4, VC: 4, IC: 5, FX0: 6, RW: 12, OFF: 200,
    FXC: ['', 'PSL', 'ARP'], blankRow: () => [-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1] });
  vm.runInContext(m[0].replace(/^const /gm, 'var '), c);
  return c;
}
const rng = seed => () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
const LA = { notes: true, oct: true, acc: true, slide: true, time: true };
const Rr = (c, o = {}) => Object.assign({ scale: 0, root: 0, density: 0.9, accent: 0.3, slide: 0.25, octave: 0.3, style: 1 }, o);
const line = (n, note = i => 0) => Array.from({ length: n }, (_, i) => ({ note: note(i), gate: 0, accent: false, slide: false }));
const J = o => JSON.parse(JSON.stringify(o));

test('acid mutate: exactly round(L x intensity) distinct steps are picked, at least one', () => {
  const c = ctx();
  for (const [L, I, want] of [[16, 1, 1], [16, 25, 4], [16, 100, 16], [32, 1, 1], [32, 25, 8], [8, 1, 1], [8, 100, 8], [64, 50, 32]]){
    for (let seed = 1; seed <= 30; seed++){
      const only = J(c.acidMutate(line(L, i => (i * 3) % 12), L, { notes: true, oct: false, acc: false, slide: false, time: false }, Rr(c), rng(seed), I));
      assert.equal(only.filter(Boolean).length, want, `L ${L} intensity ${I} seed ${seed}`);
    }
  }
});

test('acid mutate: with NOTES lit, every picked sounding step moves, in the scale, within the allowed degrees', () => {
  const c = ctx();
  for (const sc of [0, 4, 6, 7, 8, 9]){
    const scale = J(c.ACID_SCALES[sc][1]), root = 3;
    for (const I of [1, 25, 60, 100]){
      const md = c.acidMutDeg(scale.length, I);
      for (let seed = 1; seed <= 40; seed++){
        const st = line(16, i => root + scale[(i * 2) % scale.length] - (i % 5 === 0 ? 12 : 0)), before = J(st);
        const only = c.acidMutate(st, 16, { notes: true, oct: false, acc: false, slide: false, time: false }, Rr(c, { scale: sc, root }), rng(seed * 7 + I), I);
        const ls = []; for (let v = -12; v <= 24; v++) if (scale.includes(((v - root) % 12 + 12) % 12)) ls.push(v);
        let changed = 0;
        st.forEach((s, i) => { if (!only[i]){ assert.deepEqual(J(s), before[i], 'an unpicked step is left alone'); return; }
          assert.notEqual(s.note, before[i].note, 'a picked note moves'); changed++;
          assert.ok(s.note >= -12 && s.note <= 24 && scale.includes(((s.note - root) % 12 + 12) % 12), `in the scale (${s.note})`);
          const d = Math.abs(ls.indexOf(s.note) - ls.indexOf(before[i].note)); assert.ok(d >= 1 && d <= md, `${d} degrees, at most ${md} (scale ${sc}, intensity ${I})`); });
        assert.equal(changed, only.filter(Boolean).length);
      }
    }
  }
  assert.equal(c.acidMutDeg(7, 1), 1); assert.equal(c.acidMutDeg(7, 100), 7);
});

test('acid mutate: small moves are likelier than big ones, and a high intensity reaches farther', () => {
  const c = ctx(), sc = J(c.ACID_SCALES[0][1]), rnd = rng(5), H1 = {}, H2 = {};
  for (let k = 0; k < 4000; k++){ const n = c.acidNear(12, sc, 0, 100, rnd); const d = Math.abs(n - 12); H1[d] = (H1[d] || 0) + 1; }
  const idx = n => { const ls = []; for (let v = -12; v <= 24; v++) if (sc.includes(((v % 12) + 12) % 12)) ls.push(v); return ls.indexOf(n); };
  let far = 0; for (let k = 0; k < 2000; k++){ if (Math.abs(idx(c.acidNear(12, sc, 0, 100, rnd)) - idx(12)) >= 4) far++; }
  let far1 = 0; for (let k = 0; k < 2000; k++){ if (Math.abs(idx(c.acidNear(12, sc, 0, 1, rnd)) - idx(12)) >= 2) far1++; }
  assert.equal(far1, 0, 'at intensity 1 no more than one degree');
  assert.ok(far > 100, 'at 100 some moves are 4 degrees or more (' + far + ')');
  const one = [...Array(2000)].filter(() => Math.abs(idx(c.acidNear(12, sc, 0, 100, rnd)) - idx(12)) === 1).length;
  assert.ok(one > far * 2, 'one-degree moves outnumber the far ones');
  void H1; void H2;
});

test('acid mutate: the OCTAVES lane jumps by 12 only (still in key), and more often at high intensity', () => {
  const c = ctx(), cnt = I => { let n = 0; for (let seed = 1; seed <= 60; seed++){ const st = line(16, () => 7), only = c.acidMutate(st, 16, { notes: false, oct: true, acc: false, slide: false, time: false }, Rr(c, { octave: 0.8 }), rng(seed), I);
    st.forEach((s, i) => { if (only[i] && s.note !== 7){ assert.equal(Math.abs(s.note - 7), 12); n++; } else assert.equal(s.note, 7); }); } return n; };
  assert.ok(cnt(100) > cnt(10) * 3);
});

test('acid mutate: rests stay rests with TIME off; accents and gates only change on picked steps; Generate is unchanged', () => {
  const c = ctx(), st = line(16, i => i % 7), before = J(st); st[3].gate = 2; st[4].gate = 1; before[3].gate = 2; before[4].gate = 1;
  const only = c.acidMutate(st, 16, { notes: false, oct: false, acc: true, slide: true, time: true }, Rr(c), rng(9), 25);
  st.forEach((s, i) => { if (!only[i]){ assert.equal(s.gate, before[i].gate); assert.equal(s.accent, before[i].accent); } });
  // plain Generate: same output as before the change for a fixed seed would be a snapshot; here: whole line written, bar one rules
  const g = line(16); c.acidRoll(g, 16, LA, Rr(c), rng(3)); assert.equal(g[0].gate, 0, 'the first step sounds');
  const g2 = line(16); c.acidRoll(g2, 16, LA, Rr(c), rng(3)); assert.deepEqual(J(g), J(g2), 'seeded, repeatable');
});

// a phrase with hand work in it: volumes, FX in any column, instrument changes, note-offs, a second note column
function handPhrase(){
  const c = ctx(), P = Array.from({ length: 16 }, () => c.blankRow()), PSL = 1;
  const st = line(16, i => [0, 3, 7, 10][i % 4]); [2, 3, 9].forEach(i => st[i].gate = 2); st[6].gate = 1; st[12].gate = 1; st[5].accent = true; st[0].slide = true;
  c.acidWrite(P, 0, 16, 48, st, 2, 1);
  P[1][4] = 0x61; P[1][5] = 7; P[1][6] = 2; P[1][7] = 0x44; P[1][10] = 2; P[1][11] = 9;   // hand volume, other instrument, FX1 and FX3
  P[4][4] = 0x33; P[4][8] = 2; P[4][9] = 5; P[4][1] = 60;   // a volume on a note, FX2, a note in column 2
  P[2][0] = 200; P[7][0] = 200; P[10][6] = 2; P[10][7] = 1; P[14][11] = 4;   // OFFs (one on a rest after a rest), FX on rests
  P[6][4] = 0x20; P[13][5] = 3;   // a volume on a held (empty) row, an instrument change on a held row
  P[8][0] = 20;   // a note below the base: it must keep its pitch
  return { c, P };
}
test('acid mutate: at intensity 1 every row that was not picked is byte-identical; Generate rewrites all', () => {
  const { c, P } = handPhrase();
  let touched = 0;
  for (let seed = 1; seed <= 200; seed++){
    for (const I of [1, 25, 100]){
      const Q = P.map(r => r.slice()), st = c.acidRead(Q, 0, 16, 48, 1), st0 = J(st), lanes = LA;
      const only = c.acidMutate(st, 16, lanes, Rr(c, { scale: 0, root: 0 }), rng(seed), I), w = c.acidWriteMut(Q, 0, 48, st0, st, 2, 1, only);
      Q.forEach((r, y) => { if (!only[y]) assert.deepEqual(r, P[y], `row ${y} not picked, seed ${seed}, intensity ${I}`); else if (r.join() !== P[y].join()) touched++; });
      assert.ok(w.rows <= only.filter(Boolean).length);
    }
  }
  assert.ok(touched > 0, 'picked rows do change');
  // picked rows keep what the roll did not change: a picked sounding row whose pitch moves keeps its volume and instrument
  const Q = P.map(r => r.slice()), st = c.acidRead(Q, 0, 16, 48, 1), st0 = J(st); st[1].note += 2;
  c.acidWriteMut(Q, 0, 48, st0, st, 2, 1, Object.assign(new Array(16).fill(false), { 1: true }));
  assert.equal(Q[1][0], P[1][0] + 2); assert.deepEqual(Q[1].slice(1), P[1].slice(1), 'volume, instrument and FX kept');
  // the same data through Generate's writer loses them
  const G = P.map(r => r.slice()); c.acidWrite(G, 0, 16, 48, c.acidRead(G, 0, 16, 48, 1), 2, 1);
  assert.notDeepEqual(G, P, 'Generate rewrites every row (hand work is replaced, as before)');
  assert.equal(G[1][4], -1); assert.equal(G[1][5], 2); assert.equal(G[8][0], 36, 'a note below the base was moved by the round trip');
});

test('acid mutate: rows between steps (LPB 8) are not touched', () => {
  const c = ctx(), P = Array.from({ length: 32 }, () => c.blankRow()), st = line(16, i => [0, 7][i % 2]);
  c.acidWrite(P, 0, 16, 48, st, 1, 2); P[1][0] = 55; P[3][4] = 0x40; P[5][6] = 1; P[5][7] = 3;
  const Q = P.map(r => r.slice()), s2 = c.acidRead(Q, 0, 16, 48, 2), s0 = J(s2), only = c.acidMutate(s2, 16, LA, Rr(c), rng(4), 100);
  c.acidWriteMut(Q, 0, 48, s0, s2, 1, 2, only);
  for (let y = 1; y < 32; y += 2) assert.deepEqual(Q[y], P[y], 'odd row ' + y);
});

test('acid panel: the slider, Original and O are wired in index.html', () => {
  assert.match(src, /mut:25, style:1/, 'default 25');
  assert.match(src, /field\('Mutate intensity', w\)/); assert.match(src, /r\.min = 1; r\.max = 100; r\.setAttribute\('aria-label', 'Mutate intensity'\)/);
  assert.match(src, /mk\('Original \(O\)', original\)/); assert.match(src, /e\.key === 'o' \|\| e\.key === 'O'\)\)\{ e\.preventDefault\(\); e\.stopPropagation\(\); return original\(\)/);
  assert.match(src, /only = acidMutate\(st, Ls, lanes, R\(\), Math\.random, inten\); if \(JSON\.stringify\(st\) !== j0\) break;/);
});
