// DRAW PASS, trace reader (13 Nov 2026): what is inside the main thread's long tasks. Input: a raw trace from
//   node tests/sweep/draw-pass.mjs --trace=2 --screens=pv ...   (tests/sweep/out/draw-<tag>-<screen>.trace.json)
//   node tests/sweep/draw-pass-trace.mjs <trace.json> [--min=50] [--worst=6]
// Per long task (renderer main thread, RunTask >= --min ms): the inclusive ms of its top-level children by kind (script by function name,
// Recalculate Style with element counts, Layout, Layerize, Paint, PrePaint, Commit, UpdateLayer, GC, HitTest), then the totals over all long tasks and
// over the whole window, the element counts of style recalculations, and what invalidated style (invalidation tracking: reason, selector / class, element).
import fs from 'fs';
const f = process.argv[2], MIN = +((process.argv.find(x => x.startsWith('--min=')) || '--min=50').slice(6)), WORST = +((process.argv.find(x => x.startsWith('--worst=')) || '--worst=6').slice(8));
const E = JSON.parse(fs.readFileSync(f, 'utf8')), tn = new Map();
for (const e of E) if (e.ph === 'M' && e.name === 'thread_name') tn.set(e.pid + ':' + e.tid, e.args.name);
const main = [...tn].filter(([, n]) => n === 'CrRendererMain').map(([k]) => k);
const mk = main.find(k => E.some(e => e.ph === 'X' && e.name === 'RunTask' && e.pid + ':' + e.tid === k && e.dur > 1000)) || main[0];
const M = E.filter(e => e.ph === 'X' && e.pid + ':' + e.tid === mk).sort((a, b) => a.ts - b.ts || b.dur - a.dur);
const tasks = M.filter(e => e.name === 'RunTask');
const span = (M[M.length - 1].ts + M[M.length - 1].dur - M[0].ts) / 1e6;
const KIND = new Set(['UpdateLayoutTree', 'Layout', 'Layerize', 'Paint', 'PrePaint', 'Commit', 'UpdateLayer', 'UpdateLayerTree', 'HitTest', 'FunctionCall', 'EvaluateScript', 'v8.callFunction', 'FireAnimationFrame', 'TimerFire', 'EventDispatch', 'HandlePostMessage', 'MajorGC', 'MinorGC', 'RunMicrotasks', 'ParseHTML', 'IntersectionObserverController::computeIntersections', 'FireIdleCallback', 'XHRReadyStateChange', 'ResourceFetch']);
// the top-level KIND events inside a task: nested ones (a FunctionCall inside FireAnimationFrame) count under the outer one, but are also reported by function name
function kids(t) { const out = []; let end = -1; for (const e of M) { if (e === t || e.ts < t.ts || e.ts + e.dur > t.ts + t.dur + 1) continue; if (e.name === 'RunTask' || !KIND.has(e.name)) continue; if (e.ts < end) continue; out.push(e); end = e.ts + e.dur; } return out; }
const label = e => e.name === 'FunctionCall' ? 'script ' + ((e.args.data && e.args.data.functionName) || '(anon)') + ' ' + (e.args.data && e.args.data.url ? e.args.data.url.split('/').pop().slice(0, 12) + ':' + e.args.data.lineNumber : '')
  : e.name === 'FireAnimationFrame' ? 'rAF' : e.name === 'TimerFire' ? 'timer' : e.name === 'EventDispatch' ? 'event ' + (e.args.data && e.args.data.type) : e.name;
const sum = (list) => { const m = new Map(); for (const e of list) { const k = e.name === 'FunctionCall' || e.name === 'EventDispatch' ? label(e) : e.name; const o = m.get(k) || [0, 0]; o[0] += e.dur / 1000; o[1]++; m.set(k, o); } return [...m].sort((a, b) => b[1][0] - a[1][0]); };
const long = tasks.filter(t => t.dur >= MIN * 1000), all = new Set();
console.log(`main thread ${tn.get(mk)} (${mk}): window ${span.toFixed(1)} s, ${tasks.length} tasks, ${long.length} long (>= ${MIN} ms), long total ${(long.reduce((a, t) => a + t.dur, 0) / 1000).toFixed(0)} ms, all tasks ${(tasks.reduce((a, t) => a + t.dur, 0) / 1000).toFixed(0)} ms`);
const totL = [], totA = [];
for (const t of long) totL.push(...kids(t)); for (const t of tasks) totA.push(...kids(t));
const show = (title, L) => { console.log('-- ' + title); for (const [k, v] of sum(L).slice(0, 22)) console.log(`${v[0].toFixed(0).padStart(7)} ms  ${String(v[1]).padStart(5)} x  ${k}`); };
show('inside the long tasks, top-level children (inclusive)', totL); show('all tasks, top-level children (inclusive)', totA);
// what is in a task that is not a KIND child: time with no child = script that the trace did not cut out, or idle gaps inside the task
// nested: Recalculate Style / Layout are mostly INSIDE the rAF callback (a DOM read after a write forces them). "Forced inside rAF" counts only the events that lie within the time range of a
// rAF callback of a long task; style / layout after the callback (the frame's own pipeline) or inside a timer or event handler are not subtracted from the callback's script time.
const inTask = (t, name) => M.filter(e => e.name === name && e.ts >= t.ts && e.ts + e.dur <= t.ts + t.dur + 1);
const nest = name => long.reduce((a, t) => a + inTask(t, name).reduce((b, e) => b + e.dur, 0), 0) / 1000, nestN = name => long.reduce((a, t) => a + inTask(t, name).length, 0);
console.log('-- anywhere inside the long tasks (ms / runs): ' + ['UpdateLayoutTree', 'Layout', 'PrePaint', 'Paint', 'Layerize', 'Commit', 'HitTest', 'MinorGC', 'MajorGC'].map(n => `${n} ${nest(n).toFixed(0)}/${nestN(n)}`).join(' | '));
const rafs = long.flatMap(t => kids(t).filter(e => e.name === 'FireAnimationFrame')), rafMs = rafs.reduce((a, e) => a + e.dur, 0) / 1000;
const inRaf = name => M.filter(e => e.name === name && rafs.some(r => e.ts >= r.ts && e.ts + e.dur <= r.ts + r.dur + 1)).reduce((a, e) => a + e.dur, 0) / 1000;
const rafStyle = inRaf('UpdateLayoutTree'), rafLayout = inRaf('Layout');
console.log(`   rAF callbacks ${rafMs.toFixed(0)} ms: of which Recalculate Style ${rafStyle.toFixed(0)} + Layout ${rafLayout.toFixed(0)} forced inside the callbacks, the rest is script ${Math.max(0, rafMs - rafStyle - rafLayout).toFixed(0)} ms`);
// who forced it: the top app frame of the stack recorded on Recalculate Style / Layout (blank when the browser did it at the frame's end)
const topf = e => { const st = (e.args && ((e.args.beginData && e.args.beginData.stackTrace) || (e.args.data && e.args.data.stackTrace))) || []; const a = st.find(x => /index\.html/.test(x.url || '')) || st[0]; return a ? `${a.functionName || '(anon)'} :${a.lineNumber}` : '(no script: the browser, at the end of the frame)'; };
for (const nm of ['UpdateLayoutTree', 'Layout']) { const m = new Map(); for (const t of long) for (const e of inTask(t, nm)) { const k = topf(e), o = m.get(k) || [0, 0, 0]; o[0] += e.dur / 1000; o[1]++; o[2] = Math.max(o[2], e.args.elementCount || 0); m.set(k, o); }
  console.log(`-- ${nm} in long tasks, by the app function that forced it (ms / runs / most elements)`); for (const [k, v] of [...m].sort((a, b) => b[1][0] - a[1][0]).slice(0, 8)) console.log(`${v[0].toFixed(0).padStart(7)} ms ${String(v[1]).padStart(4)} x  ${String(v[2]).padStart(5)} el  ${k}`); }
const covered = long.reduce((a, t) => a + kids(t).reduce((b, e) => b + e.dur, 0), 0), tl = long.reduce((a, t) => a + t.dur, 0); console.log(`children cover ${(covered / 1000).toFixed(0)} of ${(tl / 1000).toFixed(0)} ms of long-task time`);
const ut = M.filter(e => e.name === 'UpdateLayoutTree'), ly = M.filter(e => e.name === 'Layout');
const eg = (e, k) => e.args && (e.args.elementCount != null && k === 'n' ? e.args.elementCount : e.args.beginData && e.args.beginData[k]);
console.log(`-- Recalculate Style: ${ut.length} runs, ${(ut.reduce((a, e) => a + e.dur, 0) / 1000).toFixed(0)} ms, elements per run (median / max): ${(() => { const a = ut.map(e => e.args.elementCount || 0).sort((x, y) => x - y); return a.length ? a[a.length >> 1] + ' / ' + a[a.length - 1] : '-'; })()}`);
console.log(`-- Layout: ${ly.length} runs, ${(ly.reduce((a, e) => a + e.dur, 0) / 1000).toFixed(0)} ms, dirty / total objects (median): ${(() => { const d = ly.map(e => (e.args.beginData || {}).dirtyObjects || 0).sort((x, y) => x - y), t = ly.map(e => (e.args.beginData || {}).totalObjects || 0).sort((x, y) => x - y); return d.length ? d[d.length >> 1] + ' / ' + t[t.length >> 1] : '-'; })()}`);
const sync = ut.filter(e => !M.some(p => p.name === 'RunTask' && p.ts <= e.ts && p.ts + p.dur >= e.ts + e.dur && false)).length; void sync;
// invalidations
const inv = E.filter(e => /Invalidat/.test(e.name) && e.args && e.args.data), by = new Map();
for (const e of inv) { const d = e.args.data, k = `${e.name} ${d.reason || ''} ${d.invalidationSet || d.selector || ''} ${d.nodeName || ''}`.slice(0, 160); by.set(k, (by.get(k) || 0) + 1); }
if (by.size) { console.log('-- invalidation tracking (count)'); for (const [k, v] of [...by].sort((a, b) => b[1] - a[1]).slice(0, 14)) console.log(String(v).padStart(6) + '  ' + k); }
const w = long.slice().sort((a, b) => b.dur - a.dur).slice(0, WORST);
for (const t of w) { console.log(`\n== long task ${(t.dur / 1000).toFixed(0)} ms at +${((t.ts - M[0].ts) / 1e6).toFixed(2)} s`); for (const e of kids(t).slice(0, 14)) console.log(`   ${(e.dur / 1000).toFixed(1).padStart(6)} ms  ${label(e)}${e.name === 'UpdateLayoutTree' ? ' elements ' + e.args.elementCount : ''}${e.name === 'Layout' ? ' dirty ' + ((e.args.beginData || {}).dirtyObjects) + '/' + ((e.args.beginData || {}).totalObjects) : ''}`); }
// ---- the CPU profile in the same trace (draw-pass.mjs --trace=2 --v8prof=1): script time inside the long tasks, by function (self) and by app function on the stack (inclusive)
{ const prof = new Map();   // id -> {nodes, samples, deltas, start}
  for (const e of E) { if (e.name === 'Profile' && e.args && e.args.data && e.pid + ':' + e.tid === mk) { prof.set(e.id, { nodes: new Map(), samples: [], deltas: [], start: e.args.data.startTime }); }
    if (e.name === 'ProfileChunk' && e.args && e.args.data) { const p = prof.get(e.id); if (!p) continue; const c = e.args.data.cpuProfile || {}; for (const n of c.nodes || []) p.nodes.set(n.id, n); p.samples.push(...(c.samples || [])); p.deltas.push(...(e.args.data.timeDeltas || [])); } }
  const P = [...prof.values()].sort((a, b) => b.samples.length - a.samples.length)[0];
  if (P && P.samples.length) {
    const par = new Map(); for (const n of P.nodes.values()) { if (n.parent != null) par.set(n.id, n.parent); for (const c of n.children || []) par.set(c, n.id); }
    const nm = id => { const n = P.nodes.get(id), cf = n && n.callFrame; return cf ? (cf.functionName || '(anon)') + (cf.url ? ' ' + cf.url.split('/').pop().slice(0, 10) + ':' + (cf.lineNumber + 1) : '') : '?'; };
    let t = P.start; const S = P.samples.map((id, i) => { t += P.deltas[i] || 0; return { t, id, dt: P.deltas[i + 1] != null ? P.deltas[i + 1] : 1000 }; });
    const inLong = s => long.some(k => s.t >= k.ts && s.t < k.ts + k.dur), self = new Map(), incl = new Map(); let tot = 0;
    for (const s of S) { if (!inLong(s)) continue; const d = s.dt / 1000; tot += d; self.set(nm(s.id), (self.get(nm(s.id)) || 0) + d); const seen = new Set(); for (let id = s.id; id; id = par.get(id)) { const k = nm(id); if (!seen.has(k)) { seen.add(k); incl.set(k, (incl.get(k) || 0) + d); } } }
    console.log(`\n-- CPU profile samples inside the long tasks: ${tot.toFixed(0)} ms`);
    console.log('   self time (native work the page asked for shows under (program) / the DOM call that did it)'); for (const [k, v] of [...self].sort((a, b) => b[1] - a[1]).slice(0, 16)) console.log(`${v.toFixed(0).padStart(7)} ms  ${k}`);
    console.log('   inclusive, app functions on the stack'); for (const [k, v] of [...incl].filter(([k]) => /index\.html/.test(k)).sort((a, b) => b[1] - a[1]).slice(0, 22)) console.log(`${v.toFixed(0).padStart(7)} ms  ${k}`);
    // each long task by its main cost: the app function (below frame / playTick) with the most inclusive time in that task, and the tasks' durations
    const prim = new Map(), durs = []; for (const k of long) { const m = new Map(); for (const s of S) { if (s.t < k.ts || s.t >= k.ts + k.dur) continue; const seen = new Set(); for (let id = s.id; id; id = par.get(id)) { const n = nm(id); if (/index\.html/.test(n) && !/^(frame|playTick|\(anon\)|playLite) /.test(n) && !seen.has(n)) { seen.add(n); m.set(n, (m.get(n) || 0) + s.dt / 1000); } } }
      const top = [...m].sort((a, b) => b[1] - a[1])[0], key = top ? top[0].replace(/ index\.html:\d+/, '') : '(none: native or other thread work)', o = prim.get(key) || [0, 0]; o[0]++; o[1] += k.dur / 1000; prim.set(key, o); durs.push(k.dur / 1000); }
    if (!durs.length) console.log(`   no long tasks matched (none >= ${MIN} ms): no per-task breakdown`); else {
    durs.sort((a, b) => a - b); console.log(`   long task lengths (ms): min ${durs[0].toFixed(0)}, median ${durs[durs.length >> 1].toFixed(0)}, p90 ${durs[Math.floor(durs.length * .9)].toFixed(0)}, max ${durs[durs.length - 1].toFixed(0)}`);
    console.log('   long tasks by their main app function (count / total ms of those tasks)'); for (const [k, v] of [...prim].sort((a, b) => b[1][1] - a[1][1])) console.log(`${String(v[0]).padStart(7)} x ${v[1].toFixed(0).padStart(6)} ms  ${k}`);
    }
    // who calls the native work: the nearest app function above each native leaf, and the lines of the busiest app functions (positionTicks, 500 us per tick)
    const near = new Map(); for (const s of S) { if (!inLong(s)) continue; const n = P.nodes.get(s.id), cf = n && n.callFrame; if (!cf || /index\.html/.test(cf.url || '')) continue; let a = par.get(s.id); while (a && !/index\.html/.test((P.nodes.get(a).callFrame || {}).url || '')) a = par.get(a); const k = nm(s.id) + '  <-  ' + (a ? nm(a) : '(no app frame)'); near.set(k, (near.get(k) || 0) + s.dt / 1000); }
    console.log('   native leaves and the app function that called them'); for (const [k, v] of [...near].sort((a, b) => b[1] - a[1]).slice(0, 14)) console.log(`${v.toFixed(0).padStart(7)} ms  ${k}`);
    const lines = new Map(); for (const n of P.nodes.values()) { const cf = n.callFrame; if (!/index\.html/.test(cf.url || '') || !n.positionTicks) continue; for (const pt of n.positionTicks) { const k = (cf.functionName || '(anon)') + ' line ' + pt.line; lines.set(k, (lines.get(k) || 0) + pt.ticks); } }
    console.log('   busiest source lines (ticks over the whole profile)'); for (const [k, v] of [...lines].sort((a, b) => b[1] - a[1]).slice(0, 12)) console.log(`${String(v).padStart(7)}  ${k}`);
  } else console.log('\n(no CPU profile in this trace: run draw-pass.mjs with --trace=2 --v8prof=1)');
}
