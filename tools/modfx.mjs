#!/usr/bin/env node
/* Writes the MODFX table into index.html (between the ===MODFX START=== / ===MODFX END=== markers): the shared
   …chains/…waves modules that are also pad effects and track effects, with each module's own params ([default, min,
   max, step] in chains/chains-worklet.js) and the labels and formats in tools/modfx-spec.mjs.
   Run from the repo root after changing either:  node tools/modfx.mjs
   It also checks that every param has a row and every row a param, and that choices have one name per value. */
import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { SPEC } from './modfx-spec.mjs';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
(0, eval)(fs.readFileSync(path.join(ROOT, 'chains/chains-worklet.js'), 'utf8'));
const MODULES = globalThis.ChainsDSP.MODULES;
const q = s => "'" + String(s).replace(/\\/g, '\\\\').replace(/'/g, "\\'") + "'";
const num = v => String(+v.toFixed(6));
let bad = 0;
const lines = [];
for (const [k, e] of Object.entries(SPEC)){
  const def = MODULES[k];
  if (!def){ console.error(`${k}: no such module in chains/chains-worklet.js`); bad++; continue; }
  const ps = [];
  for (const [p, r] of Object.entries(e.rows)){
    const pr = def.params[p];
    if (!pr){ console.error(`${k}.${p}: no such param`); bad++; continue; }
    const [d, min, max] = pr;
    if (r.opts){
      if (r.opts.length !== max - min + 1 || min !== 0){ console.error(`${k}.${p}: ${r.opts.length} names for ${min}…${max}`); bad++; }
      ps.push(`[${q(p)}, ${q(r.l)}, 'opt', ${num(min)}, ${num(max)}, ${num(d)}, [${r.opts.map(q).join(', ')}]]`);
    } else {
      try { new Function('v', 'return ' + r.f)(d); } catch (err){ console.error(`${k}.${p}: bad format ${err.message}`); bad++; }
      ps.push(`[${q(p)}, ${q(r.l)}, ${q(r.c || 'lin')}, ${num(min)}, ${num(max)}, ${num(d)}, v => ${r.f}]`);
    }
  }
  for (const p of Object.keys(def.params)) if (!e.rows[p]){ console.error(`${k}.${p}: param has no row`); bad++; }
  lines.push(`  ${k}:{name:${q(e.name)}, short:${q(e.short)}, long:${q(e.long)}, tail:${!!e.tail}, pad:${e.pad !== false}, track:${e.track !== false}, group:${q(e.group || 'More')}, credit:${q(e.credit || 'Thunder original')}, ps:[\n    ${ps.join(',\n    ')}]}`);
}
if (bad){ console.error(`${bad} problem(s); index.html not changed`); process.exit(1); }
const block = `/* ===MODFX START=== (written by tools/modfx.mjs from tools/modfx-spec.mjs — edit those, not this)
   Shared …chains/…waves modules that are also pad and track effects. ps rows: [param, label, curve ('lin' | 'log' | 'int'
   | 'opt' for a choice), min, max, default, format (or the choice's names)]. */
const MODFX = {
${lines.join(',\n')}
};
/* ===MODFX END=== */`;
const file = path.join(ROOT, 'index.html');
let html = fs.readFileSync(file, 'utf8');
const re = /\/\* ===MODFX START===[\s\S]*?\/\* ===MODFX END=== \*\//;
if (!re.test(html)){ console.error('index.html has no MODFX markers'); process.exit(1); }
html = html.replace(re, () => block);
fs.writeFileSync(file, html);
console.log(`MODFX: ${Object.keys(SPEC).length} effects written to index.html`);
