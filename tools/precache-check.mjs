#!/usr/bin/env node
// PRECACHE CHECK (review P1-3): every file the app pages load with a ?v= must be in sw.js (FILES or OPTIONAL) at the SAME ?v=,
// and every precached path must exist. A stale or missing entry is how an offline install goes wrong. Exit 1 on any problem.
// Usage: node tools/precache-check.mjs [repoRoot]
import fs from 'node:fs'; import path from 'node:path'; import url from 'node:url';
const ROOT = path.resolve(process.argv[2] || path.join(path.dirname(url.fileURLToPath(import.meta.url)), '..'));
const sw = fs.readFileSync(path.join(ROOT, 'sw.js'), 'utf8');
const list = name => { const m = sw.match(new RegExp('const ' + name + '\\s*=\\s*\\[([\\s\\S]*?)\\];')); if (!m) throw new Error('sw.js: no ' + name); return [...m[1].matchAll(/'([^']+)'/g)].map(x => x[1]); };
const FILES = list('FILES'), OPTIONAL = list('OPTIONAL'), cached = new Map();
for (const f of [...FILES, ...OPTIONAL]){ const [p, q] = f.split('?'); cached.set(path.posix.normalize(p.replace(/^\.\//, '')) || '.', (q || '').replace(/^v=/, '')); }
const problems = [];
for (const f of FILES){ const p = f.split('?')[0].replace(/^\.\//, ''); if (p && !fs.existsSync(path.join(ROOT, p))) problems.push('sw.js FILES lists ' + f + ', which does not exist'); }
for (const page of ['index.html', 'chains/index.html', 'waves/index.html']){
  const src = fs.readFileSync(path.join(ROOT, page), 'utf8'), dir = path.posix.dirname(page);
  for (const m of src.matchAll(/["'`(]((?:\.{1,2}\/)?[\w./-]+\.(?:js|mjs|wasm|webmanifest|css|png|svg|woff2?))\?v=(\d+)/g)){
    const rel = m[1]; if (/^(https?:)?\/\//.test(rel)) continue;
    const p = path.posix.normalize(path.posix.join(dir, rel)), v = m[2];
    if (!fs.existsSync(path.join(ROOT, p))) { problems.push(page + ' loads ' + rel + '?v=' + v + ', which does not exist'); continue; }
    if (!cached.has(p)) problems.push(page + ' loads ' + rel + '?v=' + v + ', which sw.js does not precache');
    else if (cached.get(p) !== v) problems.push(page + ' loads ' + rel + '?v=' + v + ', but sw.js precaches ?v=' + (cached.get(p) || '(none)'));
  }
  // Unversioned local <link href> / <script src> (the pages' icons, review P3-5): must be precached too, or the page is incomplete offline.
  for (const m of src.matchAll(/<(?:link|script)\b[^>]*?\b(?:href|src)="((?:\.{1,2}\/)?[\w./-]+\.(?:js|mjs|png|svg|webmanifest|css))"/g)){
    const p = path.posix.normalize(path.posix.join(dir, m[1]));
    if (!cached.has(p)) problems.push(page + ' links ' + m[1] + ', which sw.js does not precache');
  }
}
if (problems.length){ console.error('precache check: ' + problems.length + ' problem(s)\n  ' + [...new Set(problems)].join('\n  ')); process.exit(1); }
console.log('precache check: ' + cached.size + ' precached files, every versioned file the pages load matches sw.js');
