// AUDITION check (13 Nov): on the Synth page an edit plays the pad once its fresh render has landed; the Audition button
// turns that off; Random plays once, not twice; a pad with no render yet shows "rendering…" and not the pad before's line.
//   node tests/sweep/audition.mjs
import { boot, app } from './ui-lib.mjs';
const B = await boot({ width: 1440, height: 900 }), { page } = B;
let failed = false; const say = (ok, m) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${m}`); if (!ok) failed = true; };
const wait = ms => page.waitForTimeout(ms);
try {
  await app(page, "setView('synth')"); await page.mouse.click(700, 5); await wait(300);   // a real gesture: user activation
  // plays counted where they reach the audio: every pad play starts one buffer source
  await page.evaluate(() => { window.__plays = []; const st = AudioBufferSourceNode.prototype.start; AudioBufferSourceNode.prototype.start = function(){ window.__plays.push({t:Math.round(performance.now()), stale:window.__seqTest.ev('stale(cur)')}); return st.apply(this, arguments); }; });
  const plays = () => page.evaluate(() => window.__plays.splice(0));
  const drag = (id, vals, gap) => page.evaluate(async ([id, vals, gap]) => { const s = document.getElementById(id); for (const v of vals){ s.value = v; s.dispatchEvent(new Event('input', {bubbles:true})); await new Promise(r => setTimeout(r, gap)); } }, [id, vals, gap]);
  say(await app(page, 'audOn') === true, 'Audition is on by default');
  await drag('r_vdrive', [300], 0); await wait(1500);
  let p = await plays(); say(p.length === 1 && !p[0].stale, `one edit → one play of a current render (${JSON.stringify(p)})`);
  await drag('r_vdrive', [310, 320, 330, 340, 350, 360], 30); await wait(1500);
  p = await plays(); say(p.length >= 1 && p.length <= 2 && !p[p.length - 1].stale, `a short drag → 1 or 2 plays, the last one current (${p.length})`);
  await drag('r_vdrive', Array.from({length: 40}, (_, k) => 400 + k * 5), 50); await wait(1500);
  p = await plays(); say(p.length >= 2 && p.length <= 8 && !p[p.length - 1].stale, `a 2 s drag that never pauses still plays along (${p.length} plays), the last one current`);
  await page.click('#vRand'); await wait(1800);
  p = await plays(); say(p.length === 1, `Random plays once (${p.length})`);
  await page.click('#vAud'); say(await app(page, 'audOn') === false && await page.getAttribute('#vAud', 'aria-pressed') === 'false', 'the Audition button turns it off');
  await drag('r_vdrive', [500], 0); await wait(1200);
  p = await plays(); say(p.length === 0, `off: an edit is silent (${p.length})`);
  await page.click('#vAud');
  await app(page, "setView('tracker')"); await drag('r_vdrive', [520], 0); await wait(900);
  p = await plays(); say(p.length === 0, `not on the Synth page: silent (${p.length})`);
  await app(page, "setView('synth')");
  // STALE WAVE: a pad with no render
  const meta = await page.evaluate(() => window.__seqTest.ev("(() => { const j = cur === 3 ? 4 : 3; cache[j] = null; dirty[j] = true; const bg = bgRender; cur = j; drawWave(); return $('wMeta').textContent; })()"));
  say(/rendering/.test(meta) && !/peak/.test(meta), `a pad with no render yet: "${meta}"`);
  say(!B.errors.filter(e => /pageerror/.test(e)).length, 'no page errors ' + B.errors.filter(e => /pageerror/.test(e)).join(' | '));
} finally { await B.close(); }
process.exit(failed ? 1 : 0);
