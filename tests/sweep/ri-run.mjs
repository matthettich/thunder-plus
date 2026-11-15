// node tests/sweep/ri-run.mjs --tag=all --from=70 --rows=13 [--solo=9,13] [--drop=9:1,13:0] [--dropAll=9] [--nolfo] [--lfoOnly=11] [--live=2] [--render=1] [--song=/path] [--out=/home/claude/scratch/ri]
// Plays / renders the song from row --from for --rows rows and compares the takes: render vs live, live vs live, render vs render.
import fs from 'fs'; import path from 'path';
import { session, variant, align, compare, fmt, save, load } from './ri-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x === '--' + k || x.startsWith('--' + k + '=')); return a == null ? d : a.includes('=') ? a.slice(k.length + 3) : true; };
const list = s => s ? String(s).split(',').filter(Boolean) : [];
const TAG = arg('tag', 'run'), OUT = arg('out', '/home/claude/scratch/ri'), FROM = +arg('from', 70), ROWS = +arg('rows', 13), NL = +arg('live', 2), NR = +arg('render', 1), WIN = +arg('win', 5);
fs.mkdirSync(OUT, { recursive: true });
const opts = { from: FROM, rows: ROWS, solo: list(arg('solo', '')).map(Number), dropFx: list(arg('drop', '')), dropAllFx: list(arg('dropAll', '')).map(Number), noLfo: !!arg('nolfo', false), noMst: !!arg('nomst', false), dropMaster: !!arg('dropmaster', false) };
if (arg('lfoOnly', null)) opts.lfoOnly = list(arg('lfoOnly', ''));
const proj = variant(fs.readFileSync(arg('song', '/home/claude/acid/acid.thunder'), 'utf8'), opts);
const takes = { live: [], render: [] }; const rowsT = ROWS * 1.832061;
const SKIP = new Set(list(arg('skip', '')));
for (let i = 0; i < NR; i++) { const S = await session(proj); const r = await S.render(); save(path.join(OUT, `${TAG}-render${i}.f32`), r.data); takes.render.push({ data: r.data, sr: r.sr }); console.log(`render ${i}: ${r.n / r.sr} s sr ${r.sr}`); await S.close(); }
for (let i = 0; i < NL; i++) { const S = await session(proj); const l = await S.live(rowsT + 1, FROM); console.log(`live ${i}: ${JSON.stringify(l.st)} f0 ${l.f0} n ${l.n}`); save(path.join(OUT, `${TAG}-live${i}.f32`), l.data); takes.live.push({ data: l.data, sr: l.st.sr, f0: l.f0, t0: l.st.t0, k0: l.k0 }); console.log('   expected offset k0 (t0*sr - f0):', l.k0, '=', (l.k0 / l.st.sr).toFixed(3), 's; ct0', l.st.ct0, 't0', l.st.t0, 't0now', l.st.t0now); await S.close(); }
function cmp(name, a, b) { const sr = a.sr; const al = align(Float32Array.from({ length: a.data.length / 2 }, (_, i) => (a.data[2*i] + a.data[2*i+1]) / 2), Float32Array.from({ length: b.data.length / 2 }, (_, i) => (b.data[2*i] + b.data[2*i+1]) / 2), sr);
  console.log(`\n== ${name}: offset ${al.k} samples (${(al.k / sr * 1000).toFixed(2)} ms), lowband r ${al.r.toFixed(4)}`);
  // b must be the later one when k > 0; if k < 0 swap
  const rows = compare(a.data, b.data, sr, al.k, WIN); console.log(fmt(rows)); return { al, rows }; }
const res = {};
if (takes.render[0] && takes.live[0]) res.rl = cmp('render0 vs live0', takes.render[0], takes.live[0]);
if (takes.live[1]) res.ll = cmp('live0 vs live1', takes.live[0], takes.live[1]);
if (takes.render[1]) res.rr = cmp('render0 vs render1', takes.render[0], takes.render[1]);
fs.writeFileSync(path.join(OUT, `${TAG}.json`), JSON.stringify({ opts, res }));
