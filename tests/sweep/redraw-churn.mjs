// REDRAW CHURN (10 Nov 2026): plays a song in headless Chrome and, per tracker screen, counts the DOM writes the page makes in 5 s
// (a MutationObserver on the page: added nodes, text changes, attribute changes), and how many of them leave the value as it was,
// grouped by the element written (the top 12 no-op writers). Every write that leaves the same value is garbage and a style / layout invalidation; the redraw paths should write only what changed.
//   node tests/sweep/redraw-churn.mjs [--secs=5] [--screens=song,phrase,mixer,fx,inst,project,mod] [--json]
// Prints, per screen, the total and the top writers. Under 3 minutes. Needs Chrome and playwright (see tests/sweep/README.md).
import fs from 'fs'; import path from 'path';
import { boot, ROOT } from './ui-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SECS = +arg('secs', 5), SCREENS = arg('screens', 'song,phrase,mixer,fx,inst,project').split(','), JSON_OUT = process.argv.includes('--json');
const sleep = ms => new Promise(r => setTimeout(r, ms));
const B = await boot({ width: 1400, height: 900 });
const { page } = B;
const run = src => page.evaluate(s => window.__seqTest.ev('(async () => {' + s + '\n})()'), src);
await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 60000 });
const songText = fs.readFileSync(path.resolve(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [songText, 'ELECTRO.thunder']); await sleep(500);
await run(`setView('tracker'); tStart('song', {row:0}); return 1;`); await sleep(2500);
const OBS = `window.__ch = {n:0, same:0, by:{}, sameBy:{}}; const lab = n => { const e = n.nodeType === 1 ? n : n.parentElement; if (!e) return '?'; return (e.tagName === 'OUTPUT' && e.parentElement ? (e.parentElement.closest('[id]') || e.parentElement).id + '>' : '') + e.tagName.toLowerCase() + (e.id ? '#' + e.id : '') + (e.className && typeof e.className === 'string' ? '.' + e.className.trim().split(/\\s+/).join('.') : ''); };
  const ser = n => n.nodeType === 1 ? n.outerHTML : n.textContent;
  window.__mo = new MutationObserver(l => { const last = new Map();   // an attribute write is a no-op when the value before it equals the value after it (the next record's old value, or the current one)
    for (let i = l.length - 1; i >= 0; i--){ const m = l[i]; let k, c = 1, same = false;
      if (m.type === 'childList'){ c = m.addedNodes.length; if (!c) continue; k = 'add ' + lab(m.target); same = m.removedNodes.length === c && [...m.addedNodes].every((a, j) => ser(a) === ser(m.removedNodes[j])); }
      else if (m.type === 'characterData'){ k = 'text ' + lab(m.target); same = m.oldValue === m.target.data; }
      else { k = 'attr ' + m.attributeName + ' ' + lab(m.target); const key = m.target; const mk = key; let m2 = last.get(mk); if (!m2) m2 = last.set(mk, {}).get(mk);
        const cur = m.attributeName in m2 ? m2[m.attributeName] : m.target.getAttribute(m.attributeName); same = (m.oldValue === cur); m2[m.attributeName] = m.oldValue; }
      window.__ch.n += c; window.__ch.by[k] = (window.__ch.by[k] || 0) + c; if (same){ window.__ch.same += c; window.__ch.sameBy[k] = (window.__ch.sameBy[k] || 0) + c; } } });
  window.__mo.observe(document.body, {subtree:true, childList:true, characterData:true, characterDataOldValue:true, attributes:true, attributeOldValue:true}); return 1;`;
const out = {};
for (const s of SCREENS) {
  await run(`go('${s}'); return 1;`); await sleep(800);
  await run(OBS); await sleep(SECS * 1000);
  const r = await run(`window.__mo.disconnect(); return window.__ch;`);
  out[s] = r; if (!JSON_OUT) { console.log(`${s.padEnd(8)} ${String(r.n).padStart(6)} writes in ${SECS} s, ${r.same} of them leave the same value`); for (const [k, v] of Object.entries(r.sameBy).sort((a, b) => b[1] - a[1]).slice(0, +arg('top', 12))) console.log('    ' + String(v).padStart(5) + ' same  ' + k.slice(0, 110)); }
}
if (JSON_OUT) console.log(JSON.stringify(out));
await B.close();
