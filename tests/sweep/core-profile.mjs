// CORE PROFILE (11 Nov 2026): where does the audio-thread sequencer ("core") spend more audio-thread time than the page
// sequencer, and where does its memory growth come from? Report: docs/audits/2026-11-11-core-profile.md.
//
//   node tests/sweep/core-profile.mjs --seq=page|core --do=stats|cpu|heap [--secs=150] [--warm=20] [--song=sounds/ELECTRO.thunder]
//        [--interval=250]   (cpu profile sampling interval in microseconds)  [--main]  (cpu: profile the main thread instead)
//
//   stats  no profiler attached. Every 5 s: dropouts (glitch.n, playbackStats), the browser's render capacity (the app's CPU meter),
//          CPU seconds per renderer thread from /proc (the audio thread's busy fraction), main-thread task / script time.
//   cpu    a V8 CPU profile of the AudioWorklet thread (child target of the page, attached with flatten: true; playwright cannot
//          do that, so this script has its own CDP client, cdp-lib.mjs) for --secs, aggregated by function self time.
//   heap   Runtime.getHeapUsage of the page and of the worklet after a GC at 0, 1, 2, 3 ... minutes, plus a sampling heap profile
//          of the worklet and of the page (HeapProfiler.startSampling) over the whole run.
// One browser at a time. Nothing else should run on the machine.
import fs from 'fs'; import path from 'path'; import os from 'os';
import { ROOT, sleep, serve, launch, threads, sched, machine } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SEQ = arg('seq', 'page'), DO = arg('do', 'stats'), SECS = +arg('secs', 150), WARM = +arg('warm', 20), SONG = arg('song', 'sounds/ELECTRO.thunder'), INTV = +arg('interval', 250), MAIN = process.argv.includes('--main');
const OUT = path.join(ROOT, 'tests/sweep/out'); fs.mkdirSync(OUT, { recursive: true });
{ const m0 = machine(); await sleep(3000); const m1 = machine(), b = (m1.busy - m0.busy) / ((m1.busy - m0.busy) + (m1.idle - m0.idle)) * 100;
  if (b > 15) { console.error(`machine not quiet before the run: ${b.toFixed(0)}% busy (is an old Chrome still running? pgrep -f chromium)`); process.exit(2); } console.log('machine busy before the run', b.toFixed(1) + '%'); }
const CORE_SRC = arg('core', '');   // --core=<file>: serve this changed copy of core/seq-core.js to the page (to try a fix without editing the repo)
const { srv, port } = await serve(CORE_SRC ? { '/core/seq-core.js': path.resolve(CORE_SRC) } : {}); const C = await launch(); const { send } = C;
let SCRIPTS = new Map(); const children = new Map();   // sessionId -> targetInfo
C.on(d => { if (d.method === 'Target.attachedToTarget') children.set(d.params.sessionId, d.params.targetInfo); if (d.method === 'Target.detachedFromTarget') children.delete(d.params.sessionId); });
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S);
const ev = async (expr, aw = true) => { const r = await P('Runtime.evaluate', { expression: expr, awaitPromise: aw, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text); return r.result.value; };
const app = src => ev(`window.__seqTest.ev(${JSON.stringify('(async () => {' + src + '\n})()')})`);
await P('Page.enable'); await P('Runtime.enable'); await P('Performance.enable');
await P('Page.addScriptToEvaluateOnNewDocument', { source: `(()=>{window.__lt=[];try{new PerformanceObserver(l=>{for(const e of l.getEntries())window.__lt.push(e.duration)}).observe({entryTypes:['longtask']})}catch(e){}})()` });
if (DO === 'capture') await P('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { const AWN = window.AudioWorkletNode, cap = window.__cap = [];
  const b64 = u8 => { let s = ''; for (let i = 0; i < u8.length; i += 8192) s += String.fromCharCode.apply(null, u8.subarray(i, i + 8192)); return btoa(s); };
  const rep = (k, v) => ArrayBuffer.isView(v) ? { __ta: v.constructor.name, b: b64(new Uint8Array(v.buffer, v.byteOffset, v.byteLength)) } : v;
  window.AudioWorkletNode = class extends AWN { constructor(c, n, o) { super(c, n, o); if (n === 'thunder-core') { const pm = this.port.postMessage.bind(this.port), ctx = c;
    this.port.postMessage = (m, t) => { try { cap.push({ t: ctx.currentTime, m: JSON.stringify(m, rep) }); } catch (e) {} return pm(m, t); }; } } };
  window.__capDump = () => '[' + cap.map(x => '{"t":' + x.t + ',"m":' + x.m + '}').join(',') + ']'; })();` });
await P('Page.navigate', { url: `http://localhost:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500);
await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`);
await sleep(800);
const songText = fs.readFileSync(path.resolve(ROOT, SONG), 'utf8');
await ev(`window.__seqTest.load(${JSON.stringify(songText)}, ${JSON.stringify(path.basename(SONG))})`); await sleep(500);
const coreOK = await ev(`window.__seqTest.coreOn(${SEQ === 'core'})`); if (SEQ === 'core' && !coreOK) throw new Error('core did not load');
await app(`tStart('song', {row:0}); return 1;`); await sleep(2500);
console.log('started', JSON.stringify(await app(`return {on:tp.on, core:!!tp.core, seq:aset.core, ctx:ctx.state, sr:ctx.sampleRate, base:ctx.baseLatency, out:ctx.outputLatency, NT};`)), 'load', os.loadavg().map(x => x.toFixed(2)).join(' '));
const procs = (await send('SystemInfo.getProcessInfo')).processInfo, rpid = procs.filter(p => p.type === 'renderer' && fs.existsSync('/proc/' + p.id)).sort((a, b) => b.cpuTime - a.cpuTime)[0].id;   // (the page's renderer: the busiest)
// worklet target(s): auto-attach from the page
await P('Target.setAutoAttach', { autoAttach: true, waitForDebuggerOnStart: false, flatten: true }); await sleep(1000);
console.log('child targets', [...children.values()].map(t => t.type + ' ' + (t.url || '').slice(-12)).join(' | '));
const wk = [...children.entries()].find(([, t]) => t.type === 'worklet'); if (!wk && DO !== 'stats' && DO !== 'capture') throw new Error('no worklet target');
const W = (m, p) => send(m, p, wk[0]);
const SAMPLE = `const g = f => { try { const v = f(); return v === undefined ? null : v; } catch(e){ return null; } }, o = {}, ps = g(() => ctx.playbackStats) || {};
  o.ct = g(() => ctx.currentTime); o.pn = performance.now(); o.glitch = g(() => glitch.n); o.psEv = ps.underrunEvents; o.psDur = ps.underrunDuration; o.psTot = ps.totalDuration;
  o.capAvg = g(() => cpu.cap ? cpu.cap.avg : null); o.capPeak = g(() => cpu.cap ? cpu.cap.peak : null); o.capUnder = g(() => cpu.cap ? cpu.cap.under : null);
  o.meter = g(() => globalCpuValue()); o.pbN = g(() => PB.n); o.pbLate = g(() => PB.late); o.pbDrop = g(() => PB.drop); o.pbSkips = g(() => PB.skips); o.alive = g(() => rt && rt.pl.aliveN ? rt.pl.aliveN() : null);
  o.lt = window.__lt.splice(0); return o;`;
const metrics = async () => { const m = {}; for (const x of (await P('Performance.getMetrics')).metrics) m[x.name] = x.value; return m; };
const info = () => ({ load: os.loadavg()[0], thr: threads(rpid) });

if (DO === 'stats') {
  await sleep(WARM * 1000);
  if (SEQ === 'core') await ev('window.__seqTest.coreStats()');
  const rows = []; let prev = null; const t0 = Date.now(), sc0 = sched(rpid, /AudioW/), scM0 = sched(rpid, /^chrome$/), mc0 = machine(), pi0 = (await send('SystemInfo.getProcessInfo')).processInfo, thr0 = threads(rpid), tm0 = await metrics(), a0 = await app(SAMPLE);
  while (Date.now() - t0 < SECS * 1000) {
    await sleep(5000); const a = await app(SAMPLE), m = await metrics(), th = threads(rpid), t = (Date.now() - t0) / 1000;
    rows.push({ t, a, m, th, load: os.loadavg()[0] });
  }
  const cst = SEQ === 'core' ? await ev('window.__seqTest.coreStats()') : null; const sc1 = sched(rpid, /AudioW/), scM1 = sched(rpid, /^chrome$/), mc1 = machine(), pi1 = (await send('SystemInfo.getProcessInfo')).processInfo, t1 = threads(rpid), tm1 = await metrics(), a1 = rows[rows.length - 1].a, wall = (Date.now() - t0) / 1000;
  const thr = {}; for (const k of Object.keys(t1)) { const d = t1[k] - (thr0[k] || 0); if (d > 0) thr[k] = d / wall * 100; }
  const sel = (re) => Object.entries(thr).filter(([k]) => re.test(k)).reduce((x, [, v]) => x + v, 0);
  const res = { coreStats: cst && { msPerBlock: cst.ms / cst.blocks, blocks: cst.blocks, notesBooked: cst.n, late: cst.late, worstLate: cst.worst, voices: cst.voices, ovEv: cst.ovEv, skips: cst.skips, nodes: cst.nodes.length, tally: cst.tally }, seq: SEQ, secs: wall, glitchPerMin: (a1.glitch - a0.glitch) / wall * 60, psEvPerMin: (a1.psEv - a0.psEv) / wall * 60, psDur: a1.psDur - a0.psDur, meter: avg(rows.map(r => r.a.meter)), capAvg: avg(rows.map(r => r.a.capAvg)), capPeak: Math.max(...rows.map(r => r.a.capPeak || 0)), capUnder: avg(rows.map(r => r.a.capUnder)),
    mainTaskPct: (tm1.TaskDuration - tm0.TaskDuration) / wall * 100, mainScriptPct: (tm1.ScriptDuration - tm0.ScriptDuration) / wall * 100, ctDrift: ((a1.ct - a0.ct) - (a1.pn - a0.pn) / 1000) / wall * 60 * 1000,
    lt: rows.reduce((x, r) => x + r.a.lt.length, 0), ltMs: rows.reduce((x, r) => x + r.a.lt.reduce((p, q) => p + q, 0), 0), pbLate: a1.pbLate, pbDrop: a1.pbDrop, pbSkips: a1.pbSkips, threadsCpuPct: thr, audioSched: { runPct: (sc1.run - sc0.run) / 1e9 / wall * 100, waitPct: (sc1.wait - sc0.wait) / 1e9 / wall * 100, preemptPerSec: (sc1.nivcsw - sc0.nivcsw) / wall, volPerSec: (sc1.nvcsw - sc0.nvcsw) / wall, slicesPerSec: (sc1.slices - sc0.slices) / wall }, mainSched: { runPct: (scM1.run - scM0.run) / 1e9 / wall * 100, waitPct: (scM1.wait - scM0.wait) / 1e9 / wall * 100 },
    machineBusyPct: (mc1.busy - mc0.busy) / ((mc1.busy - mc0.busy) + (mc1.idle - mc0.idle)) * 100, procCpuPct: Object.fromEntries(pi1.map(p => { const q = pi0.find(x => x.id === p.id); return [p.type + ':' + p.id, q ? +((p.cpuTime - q.cpuTime) / wall * 100).toFixed(1) : null]; })), load: avg(rows.map(r => r.load)) };
  console.log(JSON.stringify(res, null, 1)); fs.writeFileSync(path.join(OUT, `cp-stats-${SEQ}-${Date.now()}.json`), JSON.stringify({ res, rows }));
} else if (DO === 'cpu') {
  await sleep(WARM * 1000);
  const T = MAIN ? P : W, ssid = MAIN ? S : wk[0];
  // which module is which: the blob scripts in the worklet are named by uuid; fingerprint each by its registerProcessor names (Debugger is on only while it lists them)
  const scripts = new Map(); C.on(d => { if (d.method === 'Debugger.scriptParsed' && d.sessionId === ssid) scripts.set(d.params.scriptId, d.params); });
  await send('Debugger.enable', {}, ssid); await sleep(500);
  for (const [id, sp] of scripts) { try { const { scriptSource } = await send('Debugger.getScriptSource', { scriptId: id }, ssid); sp.lines = scriptSource.split('\n').length; sp.reg = [...scriptSource.matchAll(/registerProcessor\(\s*['"]([^'"]+)['"]/g)].map(m => m[1]).join(','); } catch (e) {} }
  await send('Debugger.disable', {}, ssid); SCRIPTS = scripts;
  await send('Profiler.enable', {}, ssid); await send('Profiler.setSamplingInterval', { interval: INTV }, ssid);
  const thr0 = threads(rpid), t0 = Date.now(), a0 = await app(SAMPLE);
  await send('Profiler.start', {}, ssid); await sleep(SECS * 1000);
  const { profile } = await send('Profiler.stop', {}, ssid); const wall = (Date.now() - t0) / 1000, thr1 = threads(rpid), a1 = await app(SAMPLE);
  fs.writeFileSync(path.join(OUT, `cp-cpu-${MAIN ? 'main' : 'worklet'}-${SEQ}-${Date.now()}.cpuprofile`), JSON.stringify(profile));
  const thr = {}; for (const k of Object.keys(thr1)) { const d = thr1[k] - (thr0[k] || 0); if (d > 0.05) thr[k] = +(d / wall * 100).toFixed(1); }
  console.log('wall', wall.toFixed(1), 'samples', profile.samples.length, 'glitches in window', a1.glitch - a0.glitch, 'threads cpu %', JSON.stringify(thr), 'cap avg', a1.capAvg);
  report(profile);
} else if (DO === 'capture') {   // the messages the page posts to the core (song, pad samples, kit, play...), for core-offline-bench.mjs
  await sleep(SECS * 1000); const stt = await ev(`window.__seqTest.coreStats()`); const dump = await ev('window.__capDump()', false);
  fs.writeFileSync(path.join(OUT, 'cp-capture.json'), JSON.stringify({ song: SONG, stats: stt, msgs: JSON.parse(dump) })); console.log('captured', JSON.parse(dump).length, 'messages;', (dump.length / 1e6).toFixed(1), 'MB; stats', JSON.stringify(stt).slice(0, 600));
} else if (DO === 'trace') {   // Chrome tracing: which events does the audio thread emit? (--cats=a,b)
  await sleep(WARM * 1000); const evs = []; C.on(d => { if (d.method === 'Tracing.dataCollected') evs.push(...d.params.value); });
  const done = new Promise(ok => C.on(d => { if (d.method === 'Tracing.tracingComplete') ok(); }));
  await send('Tracing.start', { traceConfig: { includedCategories: arg('cats', 'audio,webaudio,disabled-by-default-webaudio.audionode,v8').split(','), recordMode: 'recordUntilFull' }, transferMode: 'ReportEvents' });
  await sleep(SECS * 1000); await send('Tracing.end'); await done;
  fs.writeFileSync(path.join(OUT, `cp-trace-${SEQ}-${Date.now()}.json`), JSON.stringify(evs));
  const tn = new Map(); for (const e of evs) if (e.ph === 'M' && e.name === 'thread_name') tn.set(e.pid + ':' + e.tid, e.args.name);
  const c = new Map(); for (const e of evs) { if (e.ph === 'M') continue; const k = (tn.get(e.pid + ':' + e.tid) || e.tid) + ' | ' + e.cat + ' | ' + e.name + ' | ' + e.ph; c.set(k, (c.get(k) || 0) + 1); }
  for (const [k, v] of [...c].sort((a, b) => b[1] - a[1]).slice(0, 40)) console.log(String(v).padStart(7), k);
} else if (DO === 'heap') {
  const MINS = Math.round(SECS / 60), SNAPS = arg('snap', '').split(',').filter(Boolean).map(Number); let MINSOFAR = 0; const snaps = [];
  async function snapshot() { const ch = []; const h = d => { if (d.method === 'HeapProfiler.addHeapSnapshotChunk' && d.sessionId === wk[0]) ch.push(d.params.chunk); }; C.on(h);
    await W('HeapProfiler.takeHeapSnapshot', { reportProgress: false, captureNumericValue: false }); await sleep(300); const j = JSON.parse(ch.join('')), M = j.snapshot.meta, nf = M.node_fields.length, ti = M.node_fields.indexOf('type'), ni = M.node_fields.indexOf('name'), si = M.node_fields.indexOf('self_size'), types = M.node_types[0], agg = {};
    for (let i = 0; i < j.nodes.length; i += nf) { const k = types[j.nodes[i + ti]] + ' ' + (types[j.nodes[i + ti]] === 'string' || types[j.nodes[i + ti]] === 'concatenated string' || types[j.nodes[i + ti]] === 'sliced string' ? '(strings)' : j.strings[j.nodes[i + ni]].slice(0, 60)); const a = agg[k] || (agg[k] = [0, 0]); a[0]++; a[1] += j.nodes[i + si]; }
    snaps.push({ min: MINSOFAR, agg }); return { nodes: j.nodes.length / nf }; }
  await send('HeapProfiler.enable', {}, wk[0]).catch(e => console.log('worklet HeapProfiler.enable', e.message)); await P('HeapProfiler.enable');
  await W('Runtime.enable').catch(e => console.log('worklet Runtime.enable', e.message));
  const samp = async () => { const o = {}; await P('HeapProfiler.collectGarbage'); await W('HeapProfiler.collectGarbage').catch(() => {});
    const ph = await P('Runtime.getHeapUsage'); o.pageUsed = ph.usedSize; o.pageTotal = ph.totalSize;
    try { const wh = await W('Runtime.getHeapUsage'); o.wkUsed = wh.usedSize; o.wkTotal = wh.totalSize; } catch (e) { o.wkErr = e.message; }
    const st = fs.readFileSync(`/proc/${rpid}/status`, 'utf8'); o.rssMB = +(/VmRSS:\s+(\d+)/.exec(st)[1]) / 1024;
    try { const sm = fs.readFileSync(`/proc/${rpid}/smaps_rollup`, 'utf8'); o.anonMB = +(/Anonymous:\s+(\d+)/.exec(sm)[1]) / 1024; o.privDirtyMB = +(/Private_Dirty:\s+(\d+)/.exec(sm)[1]) / 1024; } catch (e) {}
    if (SNAPS.includes(MINSOFAR)) o.snap = await snapshot(); return o; };
  await sleep(WARM * 1000);
  await send('HeapProfiler.startSampling', { samplingInterval: 8192 }, wk[0]).catch(e => console.log('worklet startSampling', e.message)); await P('HeapProfiler.startSampling', { samplingInterval: 8192 });
  const out = []; const t0 = Date.now(), ACT = process.argv.includes('--actions');   // --actions: what the soak does: STOP then PLAY every minute, the screen tour every 2 minutes
  const A_SP = `tStop(); await new Promise(r => setTimeout(r, 1500)); tStart('song', {row:0}); return 1;`, A_TOUR = `const W = ms => new Promise(r => setTimeout(r, ms)); setView('tracker'); for (const s of ['song', 'phrase', 'inst', 'mixer', 'fx', 'project']){ go(s); await W(400); } for (const v of ['synth', 'waves', 'chains']){ setView(v); await W(1500); } setView('tracker'); go('song'); return 1;`;
  let nextAct = t0 + 30000, nAct = 0;
  const actLoop = ACT ? (async () => { while (Date.now() < t0 + SECS * 1000) { if (Date.now() >= nextAct) { nextAct += 60000; nAct++; try { await app(A_SP); if (nAct % 2 === 0) await app(A_TOUR); } catch (e) { console.log('action threw', e.message.slice(0, 100)); } } await sleep(500); } })() : null;
  for (let m = 0; m <= MINS; m++) { MINSOFAR = m; const o = await samp(); o.min = m; out.push(o); console.log(JSON.stringify(o)); if (m < MINS) { const target = t0 + (m + 1) * 60000; await sleep(Math.max(0, target - Date.now())); } }
  if (actLoop) await actLoop;
  for (const [nm, f] of [['worklet', (m, p) => send(m, p, wk[0])], ['page', P]]) { try { const { profile } = await f('HeapProfiler.stopSampling'); fs.writeFileSync(path.join(OUT, `cp-heap-${nm}-${SEQ}-${Date.now()}.heapprofile`), JSON.stringify(profile)); console.log('--- allocation sampling', nm); heapReport(profile); } catch (e) { console.log(nm, 'stopSampling', e.message); } }
  fs.writeFileSync(path.join(OUT, `cp-heap-${SEQ}-${Date.now()}.json`), JSON.stringify(out));
  if (snaps.length > 1) { const a = snaps[0].agg, b = snaps[snaps.length - 1].agg, d = []; for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) { const x = a[k] || [0, 0], y = b[k] || [0, 0]; d.push([k, y[0] - x[0], y[1] - x[1], y[0]]); }
    console.log(`--- worklet heap snapshot diff, minute ${snaps[0].min} to ${snaps[snaps.length - 1].min}: by growth in self bytes (class, +count, +bytes, count now)`); for (const r of d.sort((p, q) => q[2] - p[2]).slice(0, 15)) console.log(r.join('  ')); }
}
await C.close(); srv.close();

function avg(a) { a = a.filter(x => x != null && isFinite(x)); return a.length ? a.reduce((x, y) => x + y, 0) / a.length : NaN; }
function report(profile) {
  const nodes = new Map(profile.nodes.map(n => [n.id, n])), self = new Map(), td = profile.timeDeltas; let tot = 0;
  for (let i = 0; i < profile.samples.length; i++) { const d = td[i + 1] != null ? td[i + 1] : td[i]; const n = nodes.get(profile.samples[i]); const cf = n.callFrame; const sp = SCRIPTS.get(cf.scriptId), mod = cf.url.includes('blob:') || /^[0-9a-f-]{36}/.test(cf.url.split('/').pop()) ? (sp && sp.reg ? '[' + sp.reg + ']' : cf.url.slice(-8)) : cf.url.split('/').pop(); const k = `${cf.functionName || '(anon)'} ${mod}:${cf.lineNumber + 1}`; self.set(k, (self.get(k) || 0) + d); tot += d; }
  const idle = [...self].filter(([k]) => /^\((idle|program|garbage collector|root)\)/.test(k)).reduce((a, [, v]) => a + v, 0);
  for (const [id, sp] of SCRIPTS) if (sp.reg) console.log('script', id, (sp.url || '').slice(-40), sp.lines + ' lines', sp.reg);
  console.log(`profile covers ${(tot / 1e6).toFixed(1)} s of samples; (idle) ${(((self.get('(idle) :0') || 0)) / tot * 100).toFixed(1)}%`);
  for (const [k, v] of [...self].sort((a, b) => b[1] - a[1]).slice(0, 30)) console.log(`${(v / tot * 100).toFixed(1).padStart(6)}%  ${(v / 1000).toFixed(0).padStart(7)} ms  ${k}`);
}
function heapReport(profile) {
  const by = new Map(); (function walk(n, path) { const cf = n.callFrame, k = `${cf.functionName || '(anon)'} ${cf.url.split('/').pop()}:${cf.lineNumber + 1}`; by.set(k, (by.get(k) || 0) + n.selfSize); for (const c of n.children || []) walk(c); })(profile.head);
  const tot = [...by.values()].reduce((a, b) => a + b, 0); console.log('total sampled live bytes', tot);
  for (const [k, v] of [...by].sort((a, b) => b[1] - a[1]).slice(0, 15)) console.log(`${String(v).padStart(10)}  ${k}`);
}
