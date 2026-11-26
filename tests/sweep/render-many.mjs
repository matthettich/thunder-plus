// RENDERS GO SILENT AFTER ABOUT 32 IN ONE PAGE SESSION (15 Nov). Renders the same one-note song N times (default 40) in ONE page with
// the app's own renderOut, with six tracks that each have a plate reverb, and the safety limiter on, so every render starts six effect nodes. A render may
// end in one of two ways: a sound (PASS) or a "Render failed" message with nothing written (the browser is out of room: allowed past
// --max-ok, FAIL before it). A render that writes a SILENT file is always a FAIL (that was the bug: V8 allows about 100 wasm memories
// in a page, every effect node of every render used one for ever, and the 33rd render started no effects).
//   node tests/sweep/render-many.mjs [--n=40] [--stems] [--fx=0] [--exhaust]
// --exhaust keeps going to 3 x --n and only checks that nothing silent was ever written. About 1 s per render.
import { open } from './seq-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const N = +arg('n', 40) * (process.argv.includes('--exhaust') ? 3 : 1), FX = arg('fx', '1') !== '0', STEMS = process.argv.includes('--stems'), MAXOK = +arg('n', 40);
const A = await open(); const { page } = A;
const run = src => page.evaluate(s => window.__seqTest.ev(s), `(async()=>{${src}\n})()`);
await run(`
  const II = S.insts.findIndex(I => I.pad === 0);
  for (let y = 0; y < 256; y++) S.song[y] = S.song[y].map(() => -1);
  /* six tracks, each with a note and its own plate: six effect nodes in every render (a 22-track song has more) */
  for (let t = 0; t < 6; t++){ S.song[0][t] = t; S.chains[t] = Array.from({length:16}, (_, k) => k ? [-1, 0] : [t, 0]);
    S.phrases[t] = freshPhrase(); S.plen[t] = 32; S.phrases[t][0][0] = 48 + t; S.phrases[t][0][IC] = II; }
  for (let t = 0; t < NT; t++) S.tfx[t] = S.tfx[t].map(() => null); S.mfx = S.mfx.map(() => null);
  ${FX ? "for (let t = 0; t < 6; t++) S.tfx[t][0] = freshTfx('plate');" : ''}
  S.mst.on = false; S.limiter = true; S.renderFrom = 0; S.bpm = 120; saveS();
  aset.vol = 100; ex.norm = false; ex.bits = 24; ex.sr = ctx ? ctx.sampleRate : 44100;
  window.__failMsg = ''; return 1;`);
let silent = 0, failed = 0, ok = 0, firstFail = 0; const rows = [];
for (let i = 1; i <= N; i++) {
  const r = await run(`
    const keep = wavN; window.__W = null; wavN = function(chs, sr, bits){ let pk = 0; for (const c of chs) for (let k = 0; k < c.length; k++){ const a = Math.abs(c[k]); if (a > pk) pk = a; } window.__W = {pk: window.__W ? Math.min(window.__W.pk, pk) : pk}; return new Uint8Array(1); };   /* the QUIETEST file of this render (mix and every stem): one silent stem fails it */
    pickOut = async () => ({dl:true}); writeOut = async () => {};
    const msgs = []; const mo = new MutationObserver(ms => { for (const m of ms) for (const n of m.addedNodes) { const t = (n.textContent || '').trim(); if (/Render failed|couldn/.test(t)) msgs.push(t.slice(0, 160)); } }); mo.observe(document.body, {childList:true, subtree:true});
    try { await renderOut(${STEMS}); } finally { wavN = keep; }
    await new Promise(r => setTimeout(r, 30)); mo.disconnect();
    return {pk: window.__W ? +window.__W.pk.toFixed(4) : null, msgs};`);
  const wrote = r.pk != null, snd = wrote && r.pk > 1e-4;
  if (snd) ok++; else if (wrote) { silent++; rows.push([`render ${i}`, 'FAIL', 'wrote a SILENT file']); }
  else { failed++; if (!firstFail) firstFail = i; if (i <= MAXOK) rows.push([`render ${i}`, 'FAIL', 'failed before ' + MAXOK + ': ' + (r.msgs[0] || 'no message')]); else if (!r.msgs.length) rows.push([`render ${i}`, 'FAIL', 'nothing written and NO message']); }
  if (i % 10 === 0) console.log(`  ${i} renders: ${ok} sound, ${failed} refused with a message, ${silent} silent files`);
}
rows.push(['renders with sound', ok >= Math.min(MAXOK, N) ? 'PASS' : 'FAIL', `${ok} of ${N}` + (firstFail ? ` (the browser ran out of room at render ${firstFail}: it said so)` : '')]);
rows.push(['silent files written', silent === 0 ? 'PASS' : 'FAIL', String(silent)]);
const w = 24; console.log('\n' + rows.map(r => r[0].padEnd(w) + r[1].padEnd(6) + r[2]).join('\n'));
const bad = rows.filter(r => r[1] === 'FAIL').length; console.log(`\n${rows.length - bad} pass, ${bad} fail`); console.log('page errors:', A.errors.length ? A.errors : 'none');
await A.close(); process.exit(bad ? 1 : 0);
