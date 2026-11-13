// WRAP CLOCK (12 Nov 2026): what happens to timing the FIRST time the song comes round, compared with later passes, and why.
// Report: docs/audits/2026-11-12-wrap-clock.md. Measurement only: no app file is changed (the instrumentation is injected into the page
// at run time through __seqTest.ev, and wraps functions in the page's own scope; nothing is written back).
//
//   node tests/sweep/wrap-clock.mjs --seq=page|core --screen=song|pv|mixer --follow=on|off --throttle=1|4|6 --song=s8|full|s<N>
//        [--loop=y0-y1] [--passes=5] [--size=open|cover] [--prof=none|main|worklet|both] [--tag=name] [--start=row]
//
//   One run = one browser, one Fold-like phone profile (touch, Android user agent, Mobile setup, 884x1020 unfolded or 412x915 folded,
//   CPU throttle through CDP Emulation.setCPUThrottlingRate (main thread only: the audio thread is not throttled)), one song played from
//   row 0 (or --start) until `passes` passes have gone round (a pass is song -> wrap). --song=s8 keeps rows 0-7 of ELECTRO (14.7 s a
//   pass), sN the first N rows, full the whole 56 rows (102.5 s a pass). --loop=2-5 plays with a LOOP over rows 2..5 instead.
//   Raw output: tests/sweep/out/wrap-<tag>.json (gitignored); wrap-report.mjs turns raw files into the per-pass tables.
//
//   Recorded, against performance.now() (ms):
//     clk   every 100 ms: ctx.currentTime, outputLatency, baseLatency, getOutputTimestamp()   (the audio clock against the wall clock)
//     ev    every event the page player books (rt.pl.play): its lead (e.t - ctx.currentTime), the call's own cost; coreTake for the core's notes
//     rows  every row the sequencer steps (page: tp.q.push; core: the rows it sends up): due time, the row, the clock when the page saw it
//     fn    cost of the page's own functions (draw, drawArr0, followPlay, arrFollowPage, pvFollow, pvPlayMark, bgRender, ensurePlay,
//           liveWarm, sfPrepare, wkFxAsk, varWant, varBuf, coreSong, coreDiff, coreBuf, rtFx ...): calls and the calls over 1.5 ms
//     lt / loaf  long tasks and long animation frames (with the scripts the browser blames)
//     m     every 500 ms: Performance.getMetrics (task, script, layout, style time), the core's own counters (coreStats: late, worst,
//           least lead, skips, ms a block), PB (page ticks, gap, least lead, moved on, held, kicked), playbackStats (underruns), glitch.n
//     cpu   --prof: the V8 profile of the main thread and / or the worklet thread, for the whole run (1 ms sampling), kept as samples
import fs from 'fs'; import path from 'path'; import os from 'os';
import { ROOT, sleep, serve, launch, machine, threads } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SEQ = arg('seq', 'core'), SCREEN = arg('screen', 'song'), FOLLOW = arg('follow', 'on'), THR = +arg('throttle', 4), SONG = arg('song', 's8'), LOOP = arg('loop', ''), PASSES = +arg('passes', 5);
const SIZE = arg('size', 'open'), PROF = arg('prof', 'none'), START = +arg('start', 0), TAG = arg('tag', `${SEQ}-${SCREEN}-${FOLLOW}-x${THR}-${SONG}${LOOP ? '-loop' + LOOP : ''}-${SIZE}${PROF !== 'none' ? '-' + PROF : ''}`);
const OUT = path.join(ROOT, 'tests/sweep/out'); fs.mkdirSync(OUT, { recursive: true });
let PREBUSY = null;   // wait (up to 90 s) for a quiet machine; if it never is, go on and record how busy it was
const ABORT = arg('abort', SONG === 'full' ? '0' : '1') === '1';   // abort (exit 3) when something else keeps the machine busy during the run: it is rerun later
for (let i = 0; i < 12; i++) { const m0 = machine(); await sleep(2000); const m1 = machine(), b = (m1.busy - m0.busy) / ((m1.busy - m0.busy) + (m1.idle - m0.idle)) * 100; PREBUSY = b; if (b < 18) break;
  if (i % 5 === 4) console.log(`machine ${b.toFixed(0)}% busy before the run; busiest processes: ` + (await import('child_process')).execSync("ps -eo pcpu,pid,args --sort=-pcpu | head -4 | cut -c1-110").toString().replace(/\n/g, ' | ')); }
if (ABORT && PREBUSY >= 92) { console.error('machine never quiet'); process.exit(4); }
// ---- the song: ELECTRO, cut to the first N rows for a short pass ----
const j = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));
if (SONG !== 'full') { const N = +SONG.slice(1); j.song.song.forEach((r, y) => { if (y >= N) for (let t = 0; t < r.length; t++) r[t] = -1; }); j.song.name = 'ELECTRO-' + SONG; }
const songText = JSON.stringify(j);

const { srv, port } = await serve(); const C = await launch(); const { send } = C;
process.on('uncaughtException', e => { console.error(e); C.close().finally(() => process.exit(1)); });
process.on('unhandledRejection', e => { console.error(e); C.close().finally(() => process.exit(1)); });
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S);
const children = new Map(); C.on(d => { if (d.method === 'Target.attachedToTarget') children.set(d.params.sessionId, d.params.targetInfo); if (d.method === 'Target.detachedFromTarget') children.delete(d.params.sessionId); });
const ev = async (expr, aw = true) => { const r = await P('Runtime.evaluate', { expression: expr, awaitPromise: aw, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text); return r.result.value; };
const app = src => ev(`window.__seqTest.ev(${JSON.stringify('(async () => {' + src + '\n})()')})`);
await P('Page.enable'); await P('Runtime.enable'); await P('Performance.enable');
// the phone profile: Fold 6 unfolded 884x1020 or folded 412x915, touch, an Android Chrome user agent (so the app's own phone test, seqHandHere(), is true: Mobile layout, lazy live-engine worklets, the clock watch, RESYNC)
const W = SIZE === 'cover' ? [412, 915] : [884, 1020];
await P('Emulation.setDeviceMetricsOverride', { width: W[0], height: W[1], deviceScaleFactor: 2.5, mobile: true });
await P('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
await P('Emulation.setUserAgentOverride', { userAgent: 'Mozilla/5.0 (Linux; Android 14; SM-F956B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Mobile Safari/537.36', userAgentMetadata: { platform: 'Android', platformVersion: '14', architecture: '', model: 'SM-F956B', mobile: true, brands: [{ brand: 'Chromium', version: '153' }], fullVersionList: [{ brand: 'Chromium', version: '153.0.0.0' }] } });
// in the page before the app: the Mobile setup, and the observers (long tasks and long animation frames, with timestamps)
await P('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { try { localStorage.setItem('thunderplus-platform-v1', 'mobile'); localStorage.setItem('thunderplus-mobile-chains-v1', '1'); localStorage.setItem('thunderplus-mobile-waves-v1', '1'); } catch(e){}
  const Z = window.__wo = {lt: [], loaf: [], raf: 0};
  try { new PerformanceObserver(l => { for (const e of l.getEntries()) Z.lt.push([e.startTime, e.duration]); }).observe({type: 'longtask', buffered: true}); } catch(e){}
  try { new PerformanceObserver(l => { for (const e of l.getEntries()) Z.loaf.push({t: e.startTime, d: e.duration, b: e.blockingDuration, rs: e.renderStart, sl: e.styleAndLayoutStart, sc: (e.scripts || []).map(s => [s.invoker, s.invokerType, s.sourceFunctionName, s.sourceURL.split('/').pop(), s.sourceCharPosition, s.duration, s.startTime, s.forcedStyleAndLayoutDuration]) }); }).observe({type: 'long-animation-frame', buffered: true}); } catch(e){}
  const tick = () => { Z.raf++; requestAnimationFrame(tick); }; requestAnimationFrame(tick); })();` });
await P('Page.navigate', { url: `http://localhost:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500);
await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`);
await sleep(800);
await ev(`window.__seqTest.load(${JSON.stringify(songText)}, 'ELECTRO.thunder')`); await sleep(500);
const coreOK = await ev(`window.__seqTest.coreOn(${SEQ === 'core'})`); if (SEQ === 'core' && !coreOK) throw new Error('core did not load');
const env0 = await app(`return {seqHand: seqHandHere(), wkLazy: wkLazy(), COARSE, mobile: isMobile(), ua: navigator.userAgent.slice(0, 60), w: innerWidth, h: innerHeight, sr: (rtInit(), ctx.sampleRate), state: ctx.state, base: ctx.baseLatency, out: ctx.outputLatency, bpm: S.bpm, NT, coreWanted: coreWanted(), core: aset.core, dpr: devicePixelRatio};`);
console.log(TAG, JSON.stringify(env0));

// this machine is shared (another job's Chrome runs here at times): give this browser's threads the same priority as the harness (Chrome puts its renderers at +5), so the audio thread is not starved by it
const NICE = +arg('nice', -8);
const renice = async () => { if (!NICE) return; try { const { execSync } = await import('child_process'); const pids = (await send('SystemInfo.getProcessInfo')).processInfo.map(p => p.id); const tids = [];
  for (const pid of pids) { try { for (const t of fs.readdirSync(`/proc/${pid}/task`)) tids.push(t); } catch(e){} } execSync(`renice -n ${NICE} -p ${tids.join(' ')} >/dev/null 2>&1 || true`); } catch(e){ console.log('renice failed', e.message.slice(0, 80)); } };
await renice();
// the screen and FOLLOW
await app(`setFollow(${FOLLOW === 'on' ? 2 : 0}); setView('tracker'); ` + (SCREEN === 'mixer' ? `go('mixer');` : SCREEN === 'pv' ? `go('phrase'); if (typeof pvSetMode === 'function') pvSetMode(true);` : `go('song');`) + ` cs.song.y = 0; cs.song.top = 0; draw(); return 1;`);
await sleep(600);
const screenInfo = await app(`return {tv, view, follow: followMode, pv: pvActive(), asong: asongOn(), arrRows: arrRows(), songRows: songRows(), apF, apReady, arrMode};`); console.log('screen', JSON.stringify(screenInfo));

// ---- the instrumentation (in the app's scope) ----
await app(`
const Z = window.__wc = {t0: performance.now(), clk: [], ev: [], evN: {}, rows: [], fn: {}, slow: [], core: [], pb: [], m: []};
Z.fnNames = [];
const wrapFn = (name, f) => { Z.fn[name] = {n: 0, tot: 0, max: 0}; Z.fnNames.push(name); const o = function(...a){ const t = performance.now(); try { return f.apply(this, a); } finally { const d = performance.now() - t, q = Z.fn[name]; q.n++; q.tot += d; if (d > q.max) q.max = d; if (d >= 1.5 && tp.on) Z.slow.push([t, name, d, tp.hiRow]); } }; return o; };
for (const n of ['draw','drawArr0','followPlay','arrFollowPage','pvFollow','pvPlayMark','bgRender','ensurePlay','ensure','ensureLive','liveWarm','liveWarmSoon','sfPrepare','wkFxAsk','varWant','varVoice','varBuf','shapeData','rtBufPlain','coreSong','coreDiff','coreBuf','coreVar','coreSyncAll','coreKitCheck','rtFx','rtAllFx','corePump','pumpRun','frame','moClock','moLfo','chaseFx','coreMixT','coreWireT','wkLiveEnsure','prerender','bassReady','pbSum','rsTick','rkTick'])
  try { const f = eval(n); if (typeof f === 'function') eval(n + ' = wrapFn(n, f)'); } catch(e){ Z.fn['!' + n] = String(e).slice(0, 60); }
// clock sampler, 100 ms
Z.t100 = setInterval(() => { try { const pn = performance.now(), ct = ctx.currentTime, ts = ctx.getOutputTimestamp ? ctx.getOutputTimestamp() : {}; Z.clk.push([pn, ct, ctx.outputLatency, ctx.baseLatency, ts.contextTime, ts.performanceTime]); } catch(e){} }, 100);
// PB's 2 s summary (it resets its counters each time: keep what it had)
{ const o = pbSum; pbSum = function(){ const now = performance.now(); if (now - PB.t >= 2000) Z.pb.push([now, PB.n, PB.late, PB.drop, PB.ahead, PB.skips]); return o.apply(this, arguments); }; }
// the rows: tStart is wrapped so that the new tp.q logs what is pushed to it
{ const o = tStart; tStart = function(){ const r = o.apply(this, arguments); try { const q = tp.q; if (q && !q.__wc){ q.__wc = 1; const p0 = q.push; q.push = function(...a){ for (const x of a){ let row = -1, n = 0; if (x.pos) for (const p of x.pos) if (p){ n++; if (p.row > row) row = p.row; } Z.rows.push([performance.now(), ctx.currentTime, x.t, row, n]); } return p0.apply(this, a); }; } } catch(e){} return r; }; }
// every event the page player books
{ const pl = rt.pl, p0 = pl.play, tk0 = pl.coreTake, seen = new Set(), TY = Z.evN;
  const code = e => { const k = e.type + (e.type === 'note' ? (e.vk ? ':var' : e.tw ? ':tw' : '') : ''); return TY[k] != null ? TY[k] : (TY[k] = Object.keys(TY).length); };
  pl.play = function(e){ const t = performance.now(); try { return p0.apply(this, arguments); } finally { const d = performance.now() - t; let first = 0; if (e.type === 'note'){ const k = 'n' + e.pad; if (!seen.has(k)){ seen.add(k); first = 1; } }
    Z.ev.push([t, code(e), typeof e.t === 'number' ? e.t - ctx.currentTime : null, d, first, e.pad == null ? -1 : e.pad]); } };
  if (tk0) pl.coreTake = function(e){ const t = performance.now(); try { return tk0.apply(this, arguments); } finally { Z.ev.push([t, code({type: 'coreTake'}), typeof e.t === 'number' ? e.t - ctx.currentTime : null, performance.now() - t, 0, e.pad == null ? -1 : e.pad]); } }; }
return 1;`);

// wrap times from a sim of the same sequencer (a guide for how long to run; the real wraps come from the rows the run logs)
const sim = await app(`const sq = makeSeq(S, 'song', {row:${LOOP ? LOOP.split('-')[0] : START}, ${LOOP ? `loop:{v:'song', y0:${LOOP.split('-')[0]}, y1:${LOOP.split('-')[1]}}, ` : ''}live:true, sim:true}); const R = Math.random; let seed = 12345; Math.random = () => { seed = (seed * 1103515245 + 12345) & 0x7fffffff; return seed / 0x7fffffff; };
  const out = []; let last = -1, t0 = null, n = 0; try { while (sq.alive() && out.length < ${PASSES} && n++ < 200000){ const r = sq.step(); if (t0 == null) t0 = r.t; let row = -1; if (r.pos) for (const p of r.pos) if (p && p.row > row) row = p.row; if (last >= 0 && row >= 0 && row < last) out.push(r.t - t0); if (row >= 0) last = row; } } finally { Math.random = R; } return out;`);
// ---- start ----
const startOpt = LOOP ? `{row:${LOOP.split('-')[0]}, loop:{v:'song', y0:${LOOP.split('-')[0]}, y1:${LOOP.split('-')[1]}}}` : `{row:${START}}`;
if (THR > 1) await P('Emulation.setCPUThrottlingRate', { rate: THR });
// profilers
let wk = null; const profs = {};
const metricsNow = async () => { const m = {}; for (const x of (await P('Performance.getMetrics')).metrics) m[x.name] = x.value; return m; };
if (PROF === 'main' || PROF === 'both') { await P('Profiler.enable'); await P('Profiler.setSamplingInterval', { interval: 1000 }); await P('Profiler.start'); }
const pn0 = await ev('performance.now()'); const mt0 = await metricsNow(); const pn1 = await ev('performance.now()'); const mono = (mt0.Timestamp * 1e6) - ((pn0 + pn1) / 2) * 1000;   // CDP's monotonic clock (us) minus performance.now() (us)
await renice();
await app(`tStart('song', ${startOpt}); return 1;`);
const T0 = Date.now();
await sleep(1500);
if (PROF === 'worklet' || PROF === 'both') {
  await P('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }); await sleep(800);
  wk = [...children.entries()].find(([, t]) => t.type === 'worklet');
  if (!wk) console.log('NO worklet target'); else { await send('Profiler.enable', {}, wk[0]); await send('Profiler.setSamplingInterval', { interval: 1000 }, wk[0]); await send('Profiler.start', {}, wk[0]); }
}
console.log('started', JSON.stringify(await app(`return {on: tp.on, core: !!tp.core, seq: aset.core, t0: tp.t0, ct: ctx.currentTime, bpm: tp.bpm};`)));

console.log('sim wraps at', JSON.stringify(sim.map(x => +x.toFixed(2))));
const passLen = sim.length ? sim[0] : 15, runFor = (sim.length >= PASSES - 1 ? sim[PASSES - 2] : passLen * (PASSES - 1)) + 2.5 + passLen * 0.3;   // through the (PASSES-1)th wrap, then a bit
let RPID = null; try { const pi = (await send('SystemInfo.getProcessInfo')).processInfo.filter(p => p.type === 'renderer' && fs.existsSync('/proc/' + p.id)).sort((a, b) => b.cpuTime - a.cpuTime); RPID = pi.length ? pi[0].id : null; } catch(e){}
const audioCpu = () => { try { const t = threads(RPID); let a = 0, m = 0; for (const k of Object.keys(t)) { if (/AudioW/.test(k)) a += t[k]; if (/CrRendererMain/.test(k)) m += t[k]; } return [a, m]; } catch(e){ return [null, null]; } };
const metrics = [], othHist = []; let mPrev = machine();
const tEnd = Date.now() + runFor * 1000;
while (Date.now() < tEnd) {
  await sleep(500);
  const m = await metricsNow(), a = await app(`const g = f => { try { const v = f(); return v === undefined ? null : v; } catch(e){ return null; } }, o = {}, ps = g(() => ctx.playbackStats) || {}, pg = g(() => PB.pg) || null;
    o.pn = performance.now(); o.ct = ctx.currentTime; o.on = tp.on; o.psEv = ps.underrunEvents; o.psDur = ps.underrunDuration; o.glitch = g(() => glitch.n); o.cpuMeter = g(() => globalCpuValue()); o.raf = window.__wo.raf; o.state = ctx.state;
    o.pg = pg && {n: pg.n, gap: pg.gap, lead: pg.lead, re: pg.re, skip: pg.skip, rs: pg.rs, rsS: pg.rsS}; o.rsGap = PB.rsGap || 0; o.kick = PB.rk.n; o.skips = PB.skips; o.late = PB.late; o.drop = PB.drop; o.pbN = PB.n; o.stray = PB.stray; o.alive = g(() => rt.pl.aliveN ? rt.pl.aliveN() : null);
    o.v = g(() => ({map: VAR.map.size, pend: VAR.pend.size})); o.pend = g(() => pend.filter(Boolean).length); return o;`);
  let cs = null; if (SEQ === 'core') cs = await ev('window.__seqTest.coreStats()').catch(() => null);
  const mNow = machine(), busyPct = (mNow.busy - mPrev.busy) / ((mNow.busy - mPrev.busy) + (mNow.idle - mPrev.idle)) * 100; mPrev = mNow;
  let chromeCpu = null; try { chromeCpu = (await send('SystemInfo.getProcessInfo')).processInfo.reduce((a, p) => a + p.cpuTime, 0); } catch(e){}
  { const prev = metrics[metrics.length - 1]; if (prev && prev.chromeCpu != null && chromeCpu != null) { const dw = Date.now() / 1000 - prev.wallS, oth = busyPct - (chromeCpu - prev.chromeCpu) / dw / 2 * 100; othHist.push(oth); if (othHist.length >= 3 && othHist.slice(-3).every(v => v > 60) && ABORT) { console.error('contaminated: others busy ' + othHist.slice(-3).map(v => v.toFixed(0)).join(',') + '%'); try { await C.close(); } catch(e){} process.exit(3); } } }
  const [aCpu, mCpu] = audioCpu();
  metrics.push({ aCpu, mCpu, busy: busyPct, chromeCpu, wallS: Date.now() / 1000, m: { task: m.TaskDuration, script: m.ScriptDuration, layout: m.LayoutDuration, style: m.RecalcStyleDuration, heap: m.JSHeapUsedSize, nodes: m.Nodes, layoutCount: m.LayoutCount, styleCount: m.RecalcStyleCount }, a, cs: cs && { n: cs.n, late: cs.late, worst: cs.worst, lead: cs.lead, skips: cs.skips, ms: cs.ms, blocks: cs.blocks, voices: cs.voices, ovEv: cs.ovEv } });
}
// ---- collect ----
let mainProf = null, wkProf = null;
if (PROF === 'main' || PROF === 'both') { mainProf = (await P('Profiler.stop')).profile; }
if (wk) { try { wkProf = (await send('Profiler.stop', {}, wk[0])).profile; } catch(e){ console.log('worklet profile', e.message); } }
const dump = await app(`const Z = window.__wc, O = window.__wo; clearInterval(Z.t100); return JSON.stringify({clk: Z.clk, ev: Z.ev, evN: Z.evN, rows: Z.rows, fn: Z.fn, slow: Z.slow, pb: Z.pb, lt: O.lt, loaf: O.loaf, log: PB.log, tpt0: tp.t0});`);
const D = JSON.parse(dump);
const compact = pr => { if (!pr) return null; const nodes = new Map(pr.nodes.map(n => [n.id, n])), fnIdx = new Map(), fns = [];
  const fid = cf => { const k = (cf.functionName || '(anon)') + '|' + cf.url.split('/').pop().slice(0, 30) + ':' + (cf.lineNumber + 1); let x = fnIdx.get(k); if (x == null) { x = fns.length; fnIdx.set(k, x); fns.push(k); } return x; };
  const parent = new Map(); for (const n of pr.nodes) for (const c of n.children || []) parent.set(c, n.id);
  const path = new Map(); const pathOf = id => { let p = path.get(id); if (p) return p; const set = new Set(); let q = id; while (q) { set.add(fid(nodes.get(q).callFrame)); q = parent.get(q); } p = [...set]; path.set(id, p); return p; };
  // 100 ms buckets in performance.now() time: self ms and inclusive ms per function
  const self = {}, incl = {}; let t = pr.startTime;   // us, monotonic
  for (let i = 0; i < pr.samples.length; i++) { t += pr.timeDeltas[i]; const dt = (pr.timeDeltas[i + 1] != null ? pr.timeDeltas[i + 1] : pr.timeDeltas[i]) / 1000, b = Math.floor(((t - mono) / 1000) / 100), id = pr.samples[i];
    const sf = fid(nodes.get(id).callFrame), S1 = self[b] || (self[b] = {}), I1 = incl[b] || (incl[b] = {}); S1[sf] = (S1[sf] || 0) + dt; for (const f of pathOf(id)) I1[f] = (I1[f] || 0) + dt; }
  const r1 = o => { for (const b in o) for (const f in o[b]) o[b][f] = +o[b][f].toFixed(2); return o; };
  return { fns, self: r1(self), incl: r1(incl), n: pr.samples.length, ms: (pr.endTime - pr.startTime) / 1000 }; };
const raw = { tag: TAG, args: { SEQ, SCREEN, FOLLOW, THR, SONG, LOOP, PASSES, SIZE, PROF, START }, env: env0, screen: screenInfo, mono, sim, metrics, ...D, mainProf: compact(mainProf), wkProf: compact(wkProf), machine: { load: os.loadavg(), cpus: os.cpus().length, preBusy: PREBUSY }, wall0: T0 };
fs.writeFileSync(path.join(OUT, `wrap-${TAG}.json`), JSON.stringify(raw));
console.log('saved', `tests/sweep/out/wrap-${TAG}.json`, 'events', D.ev.length, 'rows', D.rows.length, 'clock samples', D.clk.length, 'long tasks', D.lt.length, 'loaf', D.loaf.length);
await C.close(); srv.close();
