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

if (want('F2')){   // one event that throws is skipped; a flood still stops the song
  const A = await fresh();
  const r = await A.run(`aset.core = 'page'; let n = 0, ok = 0, threw = 0; const o = rt.pl.play; rt.pl.play = function(e){ if (e.type === 'note' && ++n % 3 === 0){ threw++; throw new Error('bad event'); } if (e.type === 'note') ok++; return o.apply(this, arguments); };
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 3000)); const on = tp.on, usedCore = !!tp.core; tStop(true); rt.pl.play = o; return {on, usedCore, ok, threw};`);
  add('F2 page sequencer, every third note throws: the song keeps playing, the others play', r.on && !r.usedCore && r.threw >= 2 && r.ok >= 3, JSON.stringify(r));
  const r2 = await A.run(`let threw = 0; const o = rt.pl.play; rt.pl.play = function(e){ threw++; throw new Error('broken engine'); };
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 3000)); const on = tp.on; tStop(true); rt.pl.play = o; return {on, threw};`);
  add('F2 page sequencer, every event throws: the song stops as before', r2.on === false, JSON.stringify(r2));
  // the audio-thread sequencer's batch: a bad event in the middle of a row must not drop the rest of the batch
  const r3 = await A.run(`aset.core = 'core'; rtInit(); coreLoad(); for (let i = 0; i < 60 && !core.ready; i++) await new Promise(r => setTimeout(r, 100));
    tStart('song', {row:0}); await new Promise(r => setTimeout(r, 500)); const usedCore = !!tp.core, got = []; const o = rt.pl.play;
    rt.pl.play = function(e){ if (e.mark === 'bad') throw new Error('bad event'); if (e.mark) { got.push(e.mark); return; } return o.apply(this, arguments); };
    const t = ctx.currentTime + 0.3, ev = m => ({type:'off', track:0, t, mark:m}); let err = null;
    try { coreMsg({op:'rows', gen:core.gen, time:t, bpm:S.bpm, tks:6, base:0, btT:0, alive:true, rows:[{t, pos:[], bpm:S.bpm, tks:6, line:0, evs:[ev('a'), ev('bad'), ev('b')]}, {t:t + 0.1, pos:[], bpm:S.bpm, tks:6, line:1, evs:[ev('c')]}]}); } catch(e){ err = e.message; }
    const on = tp.on; tStop(true); rt.pl.play = o; return {usedCore, on, got, err};`);
  add('F2 audio-thread sequencer batch: the bad event is skipped, the rest of the batch plays', r3.usedCore && !r3.err && r3.got.join() === 'a,b,c', JSON.stringify(r3));
  await A.close();
}
process.exit(table(rows) ? 1 : 0);
