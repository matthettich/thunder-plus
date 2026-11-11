// Reads .cpuprofile files from core-profile.mjs --do=cpu: self time by script, top functions, callers of a function.
//   node tests/sweep/cp-analyse.mjs <file.cpuprofile> [--top=25] [--callers=name1,name2] [--tree=funcName]
import fs from 'fs';
const f = process.argv[2], arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const P = JSON.parse(fs.readFileSync(f, 'utf8')), TOP = +arg('top', 25), callers = arg('callers', '').split(',').filter(Boolean);
const nodes = new Map(P.nodes.map(n => [n.id, n])), parent = new Map(); for (const n of P.nodes) for (const c of n.children || []) parent.set(c, n.id);
const selfN = new Map(), td = P.timeDeltas; let tot = 0;
for (let i = 0; i < P.samples.length; i++) { const d = td[i + 1] != null ? td[i + 1] : td[i]; selfN.set(P.samples[i], (selfN.get(P.samples[i]) || 0) + d); tot += d; }
const key = n => { const cf = n.callFrame; return `${cf.functionName || '(anon)'} ${(cf.url || '').split('/').pop().slice(0, 36)}#${cf.scriptId}:${cf.lineNumber + 1}`; };
const mod = n => { const cf = n.callFrame; return cf.url ? cf.url.split('/').pop().split('?')[0].slice(0, 40) : (cf.functionName.startsWith('(') ? cf.functionName : '?'); };
const byMod = new Map(), byFn = new Map(), incl = new Map();
for (const [id, d] of selfN) { const n = nodes.get(id); byMod.set(mod(n), (byMod.get(mod(n)) || 0) + d); byFn.set(key(n), (byFn.get(key(n)) || 0) + d);
  const seen = new Set(); for (let x = id; x != null; x = parent.get(x)) { const k = key(nodes.get(x)); if (!seen.has(k)) { seen.add(k); incl.set(k, (incl.get(k) || 0) + d); } } }
const pct = v => (v / tot * 100).toFixed(1).padStart(5) + '%';
console.log('total', (tot / 1e6).toFixed(1), 's; busy (not idle)', pct(tot - (byFn.get('(idle) #0:0') || 0)));
console.log('-- self by script'); for (const [k, v] of [...byMod].sort((a, b) => b[1] - a[1]).slice(0, 12)) console.log(pct(v), k);
console.log('-- top self'); for (const [k, v] of [...byFn].sort((a, b) => b[1] - a[1]).slice(0, TOP)) console.log(pct(v), k);
console.log('-- top inclusive'); for (const [k, v] of [...incl].sort((a, b) => b[1] - a[1]).slice(0, TOP)) console.log(pct(v), k);
for (const c of callers) { const m = new Map(); for (const [id, d] of selfN) { const n = nodes.get(id); if (!key(n).startsWith(c)) continue; const p = nodes.get(parent.get(id)); const k = p ? key(p) : '-'; m.set(k, (m.get(k) || 0) + d); }
  console.log('-- callers of', c); for (const [k, v] of [...m].sort((a, b) => b[1] - a[1]).slice(0, 8)) console.log(pct(v), k); }
// --lines=name1,name2: self ticks per source line inside those functions (V8 positionTicks)
for (const c of arg('lines', '').split(',').filter(Boolean)) { const m = new Map(); let sum = 0;
  for (const n of P.nodes) { if (!key(n).startsWith(c)) continue; for (const t of n.positionTicks || []) { m.set(t.line, (m.get(t.line) || 0) + t.ticks); sum += t.ticks; } }
  console.log('-- self ticks by line in', c, '(total', sum, 'ticks)'); for (const [l, t] of [...m].sort((a, b) => b[1] - a[1]).slice(0, 8)) console.log(String(t).padStart(7), (t / sum * 100).toFixed(0).padStart(3) + '%  line', l); }
