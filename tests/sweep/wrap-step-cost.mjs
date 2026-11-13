// WRAP STEP COST (12 Nov 2026; docs/audits/2026-11-12-wrap-clock.md): what one step of the sequencer costs at the song wrap, the first time
// it is ever run and later. This is the same makeSeq the audio-thread core runs (core/seq-core.js is generated from it), stepped here on
// the page thread with performance.now() (100 us resolution), so it shows the cost of the code path (the wrap resets every track and
// settles each one), not the audio thread's cache state.
//   node tests/sweep/wrap-step-cost.mjs [--song=s8|full] [--passes=5] [--throttle=4] [--live=1]
import fs from 'fs'; import path from 'path';
import { ROOT, sleep, serve, launch } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SONG = arg('song', 's8'), PASSES = +arg('passes', 5), THR = +arg('throttle', 1);
const j = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));
if (SONG !== 'full') { const N = +SONG.slice(1); j.song.song.forEach((r, y) => { if (y >= N) for (let t = 0; t < r.length; t++) r[t] = -1; }); }
const { srv, port } = await serve(); const C = await launch(); const { send } = C;
process.on('uncaughtException', e => { console.error(e); C.close().finally(() => process.exit(1)); });
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true }); const P = (m, p) => send(m, p, S);
const ev = async expr => { const r = await P('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text); return r.result.value; };
const app = src => ev(`window.__seqTest.ev(${JSON.stringify('(async () => {' + src + '\n})()')})`);
await P('Page.enable'); await P('Runtime.enable');
await P('Page.navigate', { url: `http://localhost:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500); await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`); await sleep(500);
await ev(`window.__seqTest.load(${JSON.stringify(JSON.stringify(j))}, 'ELECTRO.thunder')`); await sleep(800);
if (THR > 1) await P('Emulation.setCPUThrottlingRate', { rate: THR });
const r = await app(`const sq = makeSeq(S, 'song', {row:0, live:true, sim:true}); const R = Math.random; let seed = 7; Math.random = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
  const rows = []; let last = -1, pass = 1, n = 0; try { while (sq.alive() && pass <= ${PASSES} && n++ < 400000){ const t0 = performance.now(), r = sq.step(), dt = performance.now() - t0; let row = -1; if (r.pos) for (const p of r.pos) if (p && p.row > row) row = p.row;
    const wrap = last >= 0 && row >= 0 && row < last; if (wrap) pass++; rows.push([pass, wrap ? 1 : 0, row, +dt.toFixed(3), r.evs.length]); if (row >= 0) last = row; } } finally { Math.random = R; } return rows;`);
const q = (A, f) => { const B = [...A].sort((x, y) => x - y); return B[Math.min(B.length - 1, Math.floor(f * B.length))]; };
console.log(`song=${SONG} throttle=${THR}x; steps ${r.length}`);
console.log('| pass | steps | median ms | p99 ms | max ms | wrap step ms (and the 2 steps after) |\n|---|---|---|---|---|---|');
for (let p = 1; p <= PASSES; p++) { const R = r.filter(x => x[0] === p); if (!R.length) continue; const dts = R.map(x => x[3]); const wi = R.findIndex(x => x[1]);
  console.log(`| ${p} | ${R.length} | ${q(dts, .5).toFixed(2)} | ${q(dts, .99).toFixed(2)} | ${Math.max(...dts).toFixed(2)} | ${wi >= 0 ? [R[wi][3], R[wi + 1] && R[wi + 1][3], R[wi + 2] && R[wi + 2][3]].filter(v => v != null).map(v => v.toFixed(2)).join(' / ') : '-'} |`); }
await C.close(); srv.close();
