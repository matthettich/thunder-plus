// STABILITY BATCH 1 (10 Nov): the fixes F1-F7, each through the app in headless Chrome (F8 is a Node test, tests/stability-batch1.test.js).
//   node tests/sweep/stability-batch1.mjs [F1,F2,...]      each check prints PASS / FAIL; exit code 1 if any FAIL
import { openApp, table, live } from './lib.mjs';
const only = (process.argv[2] || '').split(',').filter(Boolean), want = k => !only.length || only.includes(k);
const rows = [], add = (n, ok, d) => { const r = [n, ok ? 'PASS' : 'FAIL', d || '']; rows.push(r); live(r); };
// a first PLAY whose live worklets are not ready yet: the warm-up waits 400 ms for them
const SLOW = `bassOK.delete(ctx); bassLoad.set(ctx, new Promise(r => setTimeout(() => r(true), 400)));`;
async function fresh(opts, init){ const A = await openApp(opts); if (init) await A.run(init); return A; }

if (want('F1')){   // PLAY then STOP inside the first-PLAY warm-up must not start the song
  const A = await fresh();
  const r = await A.run(SLOW + `const warm = !bassOK.has(ctx || {}) && kit.voices.some((v, i) => v && isLivePad(i)); tStart('song', {row:0}); const sync = tp.on;
    await new Promise(r => setTimeout(r, 30)); stopKey(); await new Promise(r => setTimeout(r, 2000)); const later = tp.on; tStop(true); return {warm, sync, later};`);
  add('F1 STOP inside the warm-up: the song does not start', r.warm && !r.sync && !r.later, JSON.stringify(r));
  await A.close();
  const B = await fresh();
  const r2 = await B.run(SLOW + `tStart('song', {row:0}); tStart('song', {row:0}); await new Promise(r => setTimeout(r, 1500)); const on = tp.on; tStop(true); return on;`).catch(e => 'err ' + e.message);
  add('F1 PLAY alone (pressed twice) still starts after the warm-up', r2 === true, String(r2));
  await B.close();
}
process.exit(table(rows) ? 1 : 0);
