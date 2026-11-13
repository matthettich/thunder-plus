// DRAW PASS (13 Nov 2026): what the screen costs while a song plays. Report: docs/audits/2026-11-13-draw-pass.md.
// Measurement only: the instrumentation is injected into the page at run time through __seqTest.ev (the app is not edited).
//
//   node tests/sweep/draw-pass.mjs --seq=page|core --size=open|cover --throttle=1|4|6 [--screens=song,phrase,pv,sect,inst,mixer,fx,mod,project]
//        [--secs=10] [--warm=3] [--tag=name] [--prefix=before-]
//
//   One run = one Chrome (Fold profile as wrap-clock.mjs: touch, Android UA, Mobile setup, DPR 2.5, 884x1020 or 412x915), ELECTRO from row 0 restarted for
//   each screen; 3 s to settle, then a 10 s window. Per screen: draw() inclusive ms and calls (wrapped here, top level only), the inclusive ms of its heavy
//   children, frame() inclusive ms, long tasks (count, total ms, longest), CDP layout / style recalculation counts and times, the page sequencer's
//   least lead and late events (events booked by rt.pl.play), and the core's own late notes. Output: tests/sweep/out/draw-<prefix><tag>.json
//   (draw-pass-report.mjs makes the tables).
import fs from 'fs'; import path from 'path';
import { ROOT, sleep, serve, launch } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SEQ = arg('seq', 'page'), SIZE = arg('size', 'open'), THR = +arg('throttle', 4), SECS = +arg('secs', 10), WARM = +arg('warm', 3);
const PROF = arg('prof', '0') === '1';
const SCREENS = arg('screens', 'song,phrase,pv,sect,inst,mixer,fx,mod,project').split(','), TAG = arg('tag', `${SEQ}-${SIZE}-x${THR}`);
const OUT = path.join(ROOT, 'tests/sweep/out'); fs.mkdirSync(OUT, { recursive: true });
const songText = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
const { srv, port } = await serve(); const C = await launch(); const { send } = C;
const LAYERS = arg('layers', '0') === '1', layerSet = { layers: [] }; C.on(d => { if (d.method === 'LayerTree.layerTreeDidChange' && d.params.layers) layerSet.layers = d.params.layers; });
const TRACE = arg('trace', '0') === '1', traceEv = []; C.on(d => { if (d.method === 'Tracing.dataCollected') traceEv.push(...d.params.value); });
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S);
const ev = async (expr, aw = true) => { const r = await P('Runtime.evaluate', { expression: expr, awaitPromise: aw, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text); return r.result.value; };
const app = src => ev(`window.__seqTest.ev(${JSON.stringify('(async () => {' + src + '\n})()')})`);
await P('Page.enable'); await P('Runtime.enable'); await P('Performance.enable');
const W = SIZE === 'cover' ? [412, 915] : [884, 1020];
await P('Emulation.setDeviceMetricsOverride', { width: W[0], height: W[1], deviceScaleFactor: 2.5, mobile: true });
await P('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
await P('Emulation.setUserAgentOverride', { userAgent: 'Mozilla/5.0 (Linux; Android 14; SM-F956B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Mobile Safari/537.36', userAgentMetadata: { platform: 'Android', platformVersion: '14', architecture: '', model: 'SM-F956B', mobile: true, brands: [{ brand: 'Chromium', version: '153' }], fullVersionList: [{ brand: 'Chromium', version: '153.0.0.0' }] } });
await P('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { try { localStorage.setItem('thunderplus-platform-v1', 'mobile'); localStorage.setItem('thunderplus-mobile-chains-v1', '1'); localStorage.setItem('thunderplus-mobile-waves-v1', '1'); } catch(e){}
  const Z = window.__wo = {lt: [], raf: 0};
  try { new PerformanceObserver(l => { for (const e of l.getEntries()) Z.lt.push([e.startTime, e.duration]); }).observe({type: 'longtask', buffered: true}); } catch(e){}
  const tick = () => { Z.raf++; requestAnimationFrame(tick); }; requestAnimationFrame(tick); })();` });
await P('Page.navigate', { url: `http://localhost:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500);
await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`);
await sleep(800);
await ev(`window.__seqTest.load(${JSON.stringify(songText)}, 'ELECTRO.thunder')`); await sleep(500);
const coreOK = await ev(`window.__seqTest.coreOn(${SEQ === 'core'})`); if (SEQ === 'core' && !coreOK) throw new Error('core did not load');
await app(`setFollow(2); setView('tracker'); setSects(true); return 1;`);
// the instrumentation: wrappers that add up inclusive ms (a call inside a call of the same function counts once)
await app(`
const Z = window.__dp = {fn: {}, lead: [], on: false};
const KIDS = ['draw','drawArr','drawArr0','pvLines','pvPlayMark','pvFollow','pvSync','pvBusFrame','followPlay','mixFaders','mixOutBar','paintMeters','deckMetersPaint','instList','apAfterDraw','cmdBarState','deckDraw','drawPat','groupBoxes','fitCheck','hintFit','stageLayout','stagePeek','tickLen','chwSet','fxPaneFit','modScSync','drawKeypad','cmdTips','keyVis','syncLoop','topSelVis','frame','deckScope','deckStripMeter','stageTick','cpuMeter','tickDots','modScTick','arrKeepRow','trackWin','spSync','arrPlace','phStageSwitch'];
for (const n of KIDS){ if (Z.fn[n]) continue; let f; try { f = eval(n); } catch(e){ continue; } if (typeof f !== 'function') continue; Z.fn[n] = {n: 0, tot: 0, max: 0, depth: 0};
  const w = function(...a){ const q = Z.fn[n]; const t = performance.now(); q.depth++; try { return f.apply(this, a); } finally { q.depth--; if (!q.depth){ const d = performance.now() - t; if (Z.on){ q.n++; q.tot += d; if (d > q.max) q.max = d; } } } };
  try { eval(n + ' = w'); } catch(e){ delete Z.fn[n]; } }
Z.mo = {rebuild: 0, attr: 0, text: 0, add: 0}; try { new MutationObserver(l => { if (!Z.on) return; for (const m of l){ if (m.type === 'childList'){ Z.mo.add += m.addedNodes.length; if (m.target === grid) Z.mo.rebuild++; } else if (m.type === 'attributes') Z.mo.attr++; else Z.mo.text++; } }).observe(document.body, {subtree: true, childList: true, attributes: true, characterData: true}); } catch(e){}
{ const pl = rt && rt.pl; if (pl){ const p0 = pl.play; pl.play = function(e){ if (Z.on && e.type === 'note' && typeof e.t === 'number') Z.lead.push(e.t - ctx.currentTime); return p0.apply(this, arguments); }; } }
return Object.keys(Z.fn).length;`);
const metricsNow = async () => { const m = {}; for (const x of (await P('Performance.getMetrics')).metrics) m[x.name] = x.value; return m; };
const result = { tag: TAG, args: { SEQ, SIZE, THR, SECS, WARM }, screens: {} };
const SET = { song: `go('song');`, phrase: `go('phrase'); pvSetMode(false);`, pv: `go('phrase'); pvSetMode(true);`, sect: `go('sect');`, inst: `go('inst');`, mixer: `go('mixer');`, fx: `go('fx');`, mod: `go('mod');`, project: `go('project');` };
if (THR > 1) await P('Emulation.setCPUThrottlingRate', { rate: THR });
for (const sc of SCREENS) {
  await app(`if (tp.on) tStop(); window.__dp.lead.length = 0; return 1;`); await sleep(400);
  const info = await app(`${SET[sc]} cs.song.y = 0; cs.song.top = 0; draw(); return {tv, view, pv: pvActive(), asong: asongOn(), w: innerWidth, h: innerHeight, arrMode, sects: sectsOn};`); await sleep(500);
  await app(`tStart('song', {row:0}); return 1;`); await sleep(WARM * 1000);
  const m0 = await metricsNow(); const t0 = await ev('performance.now()');
  const c0 = SEQ === 'core' ? await ev('window.__seqTest.coreStats()').catch(() => null) : null;
  await app(`const Z = window.__dp; for (const k in Z.fn){ const q = Z.fn[k]; q.n = 0; q.tot = 0; q.max = 0; } Z.lead.length = 0; Z.mo.rebuild = Z.mo.attr = Z.mo.text = Z.mo.add = 0; Z.raf0 = window.__wo.raf; Z.on = true; return 1;`);
  if (PROF){ await P('Profiler.enable'); await P('Profiler.setSamplingInterval', { interval: 500 }); await P('Profiler.start'); }
  if (LAYERS){ await P('LayerTree.enable'); await sleep(1500); const L = layerSet.layers, reasons = {}; for (const l of L.slice(0, 200)){ try { const q = await P('LayerTree.compositingReasons', { layerId: l.layerId }); for (const x of q.compositingReasonIds || q.compositingReasons || []) reasons[x] = (reasons[x] || 0) + 1; } catch(e){} }
    console.log(`  LAYERS ${sc}: ${L.length} layers; paint counts total ${L.reduce((a, l) => a + (l.paintCount || 0), 0)}; sizes ` + L.slice(0, 12).map(l => (l.width | 0) + 'x' + (l.height | 0)).join(' ') + ' reasons ' + JSON.stringify(reasons)); await P('LayerTree.disable'); }
  if (TRACE){ traceEv.length = 0; await P('Tracing.start', { categories: 'devtools.timeline,disabled-by-default-devtools.timeline', transferMode: 'ReportEvents' }); }
  await sleep(SECS * 1000);
  if (TRACE){ const done = new Promise(ok => C.on(d => { if (d.method === 'Tracing.tracingComplete') ok(); })); await P('Tracing.end'); await done; }
  let prof = null; if (PROF){ prof = (await P('Profiler.stop')).profile; }
  const t1 = await ev('performance.now()'); const m1 = await metricsNow();
  const D = await app(`const Z = window.__dp; Z.on = false; const L = Z.lead.slice().sort((a, b) => a - b); return {fn: Z.fn, leadMin: L.length ? L[0] : null, leadN: L.length, late: L.filter(x => x < 0).length, lead5: L.length ? L[Math.floor(L.length * 0.05)] : null, raf: window.__wo.raf - Z.raf0, mo: Z.mo, on: tp.on, lt: window.__wo.lt.filter(x => x[0] >= ${t0}), row: tp.hiRow};`);
  const c1 = SEQ === 'core' ? await ev('window.__seqTest.coreStats()').catch(() => null) : null;
  const win = (t1 - t0) / 1000, lt = D.lt.filter(x => x[0] < t1);
  const r = { info, secs: win, on: D.on, draws: D.fn.draw.n, drawMs: D.fn.draw.tot, drawMax: D.fn.draw.max, frameMs: D.fn.frame ? D.fn.frame.tot : 0, rafN: D.raf,
    ltN: lt.length, ltMs: lt.reduce((a, x) => a + x[1], 0), ltMax: lt.reduce((a, x) => Math.max(a, x[1]), 0),
    layoutN: m1.LayoutCount - m0.LayoutCount, styleN: m1.RecalcStyleCount - m0.RecalcStyleCount, layoutMs: (m1.LayoutDuration - m0.LayoutDuration) * 1000, styleMs: (m1.RecalcStyleDuration - m0.RecalcStyleDuration) * 1000,
    scriptMs: (m1.ScriptDuration - m0.ScriptDuration) * 1000, taskMs: (m1.TaskDuration - m0.TaskDuration) * 1000, leadMin: D.leadMin, lead5: D.lead5, leadN: D.leadN, late: D.late, row: D.row, mo: D.mo,
    core: c1 && c0 ? { late: c1.late, lead: c1.lead, worst: c1.worst } : null,   // (the core's stats read resets its late counter: c1.late is already the window's count; c0 is read to clear the warm-up's)
    kids: Object.fromEntries(Object.entries(D.fn).filter(([k, v]) => v.n && k !== 'draw').map(([k, v]) => [k, { n: v.n, ms: +v.tot.toFixed(1), max: +v.max.toFixed(1) }])) };
  result.screens[sc] = r;
  if (TRACE){ const by = {}; for (const e of traceEv){ if (e.ph === 'X' && e.dur != null && e.name !== 'RunTask' || (e.ph === 'X' && e.name === 'RunTask')){ const b = by[e.name] || (by[e.name] = [0, 0]); b[0] += e.dur / 1000; b[1]++; } }
    const top = Object.entries(by).filter(([k]) => /^(Layout|UpdateLayoutTree|PrePaint|Paint|Layerize|Commit|RunTask|FunctionCall|FireAnimationFrame|EventDispatch|TimerFire|HitTest|IntersectObserverController::computeIntersections|UpdateLayer|CompositeLayers|ScheduleStyleRecalculation|ParseHTML|MajorGC|MinorGC|V8\.GC.*|RunMicrotasks|v8.callFunction|ResourceFetch|HandlePostMessage|FireIdleCallback)$/.test(k)).sort((a, b) => b[1][0] - a[1][0]);
    r.trace = Object.fromEntries(top.map(([k, v]) => [k, [+v[0].toFixed(0), v[1]]])); console.log('  TRACE ' + sc + ': ' + top.map(([k, v]) => k + ' ' + v[0].toFixed(0) + 'ms/' + v[1]).join(' | ')); }
  if (prof){ const nodes = new Map(prof.nodes.map(n => [n.id, n])), self = {}; let tot = 0;
    for (let i = 0; i < prof.samples.length; i++){ const n = nodes.get(prof.samples[i]), cf = n.callFrame, k = (cf.functionName || '(anon)') + ' ' + cf.url.split('/').pop().slice(0, 14) + ':' + (cf.lineNumber + 1), dt = (prof.timeDeltas[i + 1] != null ? prof.timeDeltas[i + 1] : prof.timeDeltas[i]) / 1000; self[k] = (self[k] || 0) + dt; tot += dt; }
    const top = Object.entries(self).sort((a, b) => b[1] - a[1]).slice(0, 18); r.prof = top.map(([k, v]) => [k, +v.toFixed(0)]); console.log(`  PROFILE ${sc}: total ${tot.toFixed(0)} ms: ` + top.map(([k, v]) => k + ' ' + v.toFixed(0)).join(' | ')); }
  const f = x => x == null ? '-' : x.toFixed(1);
  console.log(`${TAG} ${sc.padEnd(7)} playing=${r.on} draws/s ${(r.draws / win).toFixed(1)} drawMs/s ${f(r.drawMs / win)} per draw ${f(r.drawMs / Math.max(1, r.draws))} frame ms/s ${f(r.frameMs / win)} long ${r.ltN}/${f(r.ltMs)}ms max ${f(r.ltMax)} layout ${(r.layoutN / win).toFixed(0)}/s style ${(r.styleN / win).toFixed(0)}/s lead ${f(r.leadMin)} late ${r.late} core ${JSON.stringify(r.core)} dom/s rebuild ${(r.mo.rebuild / win).toFixed(1)} added ${(r.mo.add / win).toFixed(0)} attr ${(r.mo.attr / win).toFixed(0)} text ${(r.mo.text / win).toFixed(0)}`);
}
await app(`if (tp.on) tStop(); return 1;`);
fs.writeFileSync(path.join(OUT, `draw-${arg('prefix', '')}${TAG}.json`), JSON.stringify(result));
await C.close(); srv.close(); process.exit(0);
