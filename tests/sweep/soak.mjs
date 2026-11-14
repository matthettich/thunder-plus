// SOAK TEST (10 Nov 2026): plays a song in real time for N minutes in headless Chrome, samples the audio stability and the
// leak candidates every 5 s, does what a user does on a schedule (STOP/PLAY, screen tour, Settings / pump window / waves /
// chains, a song reload at half time), then fits a line to every series and prints PASS / FAIL / NOTE per series.
//
//   node tests/sweep/soak.mjs [--minutes=10] [--seq=page|core] [--song=sounds/ELECTRO.thunder] [--phone]
//                             [--only=stopplay,tour,panels,reload]   (run just some of the actions; default all)
//                             [--sample=5] [--warm=2]                  (sample seconds; warm-up minutes left out of the fits)
//
// --phone: touch + Mobile setup (platform 'mobile'), 412x915 window, and a 4x CPU throttle through CDP
//          (Emulation.setCPUThrottlingRate; override with --throttle=N). It does NOT have a phone's audio stack.
// Exit code 1 if any series FAILs. Raw samples: tests/sweep/out/soak-<seq>-<timestamp>.json (gitignored).
// Timing numbers on a shared machine are noisy: every sample records os.loadavg(); judge trends, not absolutes.
import fs from 'fs'; import os from 'os'; import path from 'path';
import { boot, ROOT, OUT, takeErrors } from './ui-lib.mjs';

const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const MIN = +arg('minutes', 10), SEQ = arg('seq', 'page'), SONG = arg('song', 'sounds/ELECTRO.thunder'), PHONE = process.argv.includes('--phone');
const DT = +arg('sample', 5) * 1000, WARM = +arg('warm', 2), THROTTLE = +arg('throttle', PHONE ? 4 : 1);
const ONLY = arg('only', 'stopplay,tour,panels,reload').split(',');
const T_END = MIN * 60 * 1000, sleep = ms => new Promise(r => setTimeout(r, ms));

// ---- in the page, before the app: counters for timers, intervals, AudioContexts, long tasks, rAF (in every frame) ----
const INIT = `(() => { if (window.__soak) return; const Z = window.__soak = {lt: [], raf: 0, iv: new Set(), to: new Set(), ctxs: [], ctxNew: 0};
  try { new PerformanceObserver(l => { for (const e of l.getEntries()) Z.lt.push(e.duration); }).observe({entryTypes: ['longtask']}); } catch(e){}
  const si = window.setInterval, ci = window.clearInterval, st = window.setTimeout, ct = window.clearTimeout;
  window.setInterval = function(f, t, ...a){ const id = si.call(this, f, t, ...a); Z.iv.add(id); return id; };
  window.clearInterval = function(id){ Z.iv.delete(id); return ci.call(this, id); };
  window.setTimeout = function(f, t, ...a){ if (typeof f !== 'function') return st.call(this, f, t, ...a); let id; const g = function(){ Z.to.delete(id); return f.apply(this, arguments); }; id = st.call(this, g, t, ...a); Z.to.add(id); return id; };
  window.clearTimeout = function(id){ Z.to.delete(id); return ct.call(this, id); };
  const AC = window.AudioContext; if (AC) window.AudioContext = class extends AC { constructor(...a){ super(...a); Z.ctxNew++; Z.ctxs.push(new WeakRef(this)); } };
  const tick = () => { Z.raf++; requestAnimationFrame(tick); }; requestAnimationFrame(tick);
})();`;

// ---- in the app's scope (needs ?seqtest=1): everything the app counts itself; each read guarded, a missing name gives null ----
const SAMPLE = `const g = f => { try { const v = f(); return v === undefined ? null : v; } catch(e){ return null; } }, o = {};
  o.ct = g(() => ctx.currentTime); o.state = g(() => ctx.state); o.pn = performance.now(); o.sr = g(() => ctx.sampleRate);
  const ps = g(() => ctx.playbackStats); if (ps){ o.psEv = ps.underrunEvents; o.psDur = ps.underrunDuration; o.psTot = ps.totalDuration; o.psLat = ps.averageLatency; o.psMax = ps.maximumLatency; }
  o.on = g(() => tp.on); o.core = g(() => !!tp.core); o.glitch = g(() => glitch.n);
  o.cpuAvg = g(() => cpu.cap ? cpu.cap.avg : null); o.cpuPeak = g(() => cpu.cap ? cpu.cap.peak : null); o.cpuUnder = g(() => cpu.cap ? cpu.cap.under : null); o.cpuMeter = g(() => globalCpuValue());
  o.alive = g(() => rt && rt.pl.aliveN ? rt.pl.aliveN() : null); o.bassN = g(() => bassN.size); o.bassLive = g(() => bassLive.size);
  o.padSrcs = g(() => padSrcs.reduce((a, s) => a + (s ? s.size : 0), 0)); o.varMap = g(() => VAR.map.size); o.varBytes = g(() => VAR.bytes); o.varPend = g(() => VAR.pend.size);
  o.tpQ = g(() => tp.q.length); o.mobQ = g(() => MOB.q.length); o.moled = g(() => MOLED.length); o.pbLog = g(() => PB.log.length);
  o.pbN = g(() => PB.n); o.pbLate = g(() => PB.late); o.pbDrop = g(() => PB.drop); o.pbSkips = g(() => PB.skips); o.pbStray = g(() => PB.stray);
  o.pgN = g(() => PB.pg ? PB.pg.n : null); o.pgGap = g(() => PB.pg ? PB.pg.gap : null); o.pgLead = g(() => PB.pg ? PB.pg.lead : null); o.pgRe = g(() => PB.pg ? PB.pg.re : null); o.pgHeld = g(() => PB.pg ? PB.pg.rs : null);
  o.undo = g(() => undo.past.length); o.ls = g(() => { let n = 0; for (let i = 0; i < localStorage.length; i++){ const k = localStorage.key(i); n += k.length + localStorage.getItem(k).length; } return n; });
  o.view = g(() => view); o.tv = g(() => tv);
  const Z = window.__soak; o.lt = Z.lt.splice(0); o.raf = Z.raf; o.iv = Z.iv.size; o.to = Z.to.size; o.ctxLive = Z.ctxs.filter(w => { const c = w.deref(); return c && c.state !== 'closed'; }).length; o.ctxNew = Z.ctxNew;
  let fi = 0, fto = 0; for (const f of document.querySelectorAll('iframe')){ try { const z = f.contentWindow.__soak; if (z){ fi += z.iv.size; fto += z.to.size; } } catch(e){} } o.frameIv = fi; o.frameTo = fto; o.frames = document.querySelectorAll('iframe').length;
  return o;`;

// ---- the user's actions (in the app's scope) ----
const A_STOPPLAY = `tStop(); await new Promise(r => setTimeout(r, 1500)); tStart('song', {row:0}); return 1;`;
const A_TOUR = `const W = ms => new Promise(r => setTimeout(r, ms)); setView('tracker'); for (const s of ['song', 'phrase', 'inst', 'mixer', 'fx', 'project']){ go(s); await W(400); }
  for (const v of ['synth', 'waves', 'chains']){ setView(v); await W(1500); } setView('tracker'); go('song'); return 1;`;
const A_PUMP = `const W = ms => new Promise(r => setTimeout(r, ms)); let hit = null;
  for (let t = 0; t <= NT && !hit; t++){ const A = t >= NT ? S.mfx : S.tfx[t]; if (A) A.forEach((f, i) => { if (!hit && f && isPumpT(f.type)) hit = [t, i]; }); }
  if (!hit) return 'no pump effect in the song'; go('fx'); await W(400); PumpFX.open(hit[0], hit[1]); let n = 0; for (let i = 0; i < 40 && !n; i++){ await W(100); n = document.querySelectorAll('.pumpwin, .mwpump').length; }
  await W(500); if (window.PumpUI) PumpUI.closeAll(); await W(300); go('song'); return 'pump window ' + hit.join(':') + ' (' + n + ' open)';`;
const A_FRAMES = `const W = ms => new Promise(r => setTimeout(r, ms)); for (const v of ['waves', 'chains', 'waves', 'chains']){ setView(v); await W(1500); } setView('tracker'); go('song'); return 1;`;

// ---- statistics ----
function fit(xs, ys){   // least squares: slope per x unit
  const n = xs.length; if (n < 3) return { slope: NaN }; const mx = xs.reduce((a, b) => a + b, 0) / n, my = ys.reduce((a, b) => a + b, 0) / n;
  let sxy = 0, sxx = 0; for (let i = 0; i < n; i++){ sxy += (xs[i] - mx) * (ys[i] - my); sxx += (xs[i] - mx) ** 2; }
  return { slope: sxx ? sxy / sxx : 0 };
}
const avg = a => a.length ? a.reduce((x, y) => x + y, 0) / a.length : NaN;
const f3 = v => !isFinite(v) ? String(v) : Math.abs(v) >= 1000 ? Math.round(v).toLocaleString('en-US') : Math.abs(v) >= 10 ? v.toFixed(1) : v.toFixed(3).replace(/0+$/, '').replace(/\.$/, '');

const rows = [], add = (name, res, note) => { rows.push([name, res, note || '']); console.log(res.padEnd(5) + '  ' + name + (note ? '  ·  ' + note : '')); };
const RESCORE = arg('rescore', '');   // --rescore=<raw json>: score a finished run's samples again (no browser)
// ---- run ----
async function soak(){
const B = await boot({ width: PHONE ? 412 : 1400, height: PHONE ? 915 : 900, mobile: PHONE, storage: PHONE ? { 'thunderplus-platform-v1': 'mobile', 'thunderplus-mobile-chains-v1': '1', 'thunderplus-mobile-waves-v1': '1' } : null });
const { page, ctx: bctx, browser } = B;
page.on('dialog', d => d.accept().catch(() => {}));
const cdp = await bctx.newCDPSession(page), bcdp = await browser.newBrowserCDPSession().catch(() => null);
await cdp.send('Performance.enable'); await cdp.send('HeapProfiler.enable').catch(() => {}); await cdp.send('Runtime.enable');
if (PHONE && THROTTLE > 1) await cdp.send('Emulation.setCPUThrottlingRate', { rate: THROTTLE });
// (the init script has to be in before the page scripts run: add it and reload; the page keeps its localStorage)
await bctx.addInitScript(INIT); await page.reload(); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 90000 }); await sleep(2500);
{ const b = page.locator('button', { hasText: /^Got it/ }).first(); if (await b.isVisible().catch(() => false)) await b.click({ timeout: 4000 }).catch(() => {}); }
const run = src => page.evaluate(s => window.__seqTest.ev('(async () => {' + s + '\n})()'), src);
const songText = fs.readFileSync(path.resolve(ROOT, SONG), 'utf8'), songName = path.basename(SONG);
const loadSong = () => page.evaluate(([t, n]) => window.__seqTest.load(t, n), [songText, songName]);
await loadSong(); await sleep(500);
const coreOK = await page.evaluate(on => window.__seqTest.coreOn(on), SEQ === 'core');
if (SEQ === 'core' && !coreOK) { add('setup: audio-thread core loaded', 'FAIL', 'coreLoad() did not come ready'); await B.close(); process.exit(1); }
const metricNames = new Set();
async function metrics(){ const m = {}; for (const x of (await cdp.send('Performance.getMetrics')).metrics){ m[x.name] = x.value; metricNames.add(x.name); } return m; }
async function dom(){ try { return await cdp.send('Memory.getDOMCounters'); } catch(e){ return {}; } }
async function proc(){   // the browser's processes: cpu seconds and resident set (from /proc)
  const o = { rss: 0, rssRenderer: 0, cpu: 0, n: 0 }; if (!bcdp) return o;
  try { const { processInfo } = await bcdp.send('SystemInfo.getProcessInfo'); for (const p of processInfo){ o.n++; o.cpu += p.cpuTime; let rss = 0; try { rss = +(/VmRSS:\s+(\d+)/.exec(fs.readFileSync(`/proc/${p.id}/status`, 'utf8')) || [0, 0])[1] * 1024; } catch(e){} o.rss += rss; if (p.type === 'renderer') o.rssRenderer += rss; } } catch(e){}
  return o;
}
async function objs(){   // live JS wrapper counts (queryObjects forces its own GC)
  const o = {};
  for (const n of ['AudioNode', 'AudioBufferSourceNode', 'GainNode', 'AudioWorkletNode', 'AudioParam', 'AudioBuffer', 'AudioContext', 'MessagePort', 'HTMLCanvasElement']) {
    try { const { result } = await cdp.send('Runtime.evaluate', { expression: `typeof ${n} === 'undefined' ? null : ${n}.prototype` }); if (!result.objectId) continue;
      const q = await cdp.send('Runtime.queryObjects', { prototypeObjectId: result.objectId }); const r = await cdp.send('Runtime.callFunctionOn', { objectId: q.objects.objectId, functionDeclaration: 'function(){ return this.length; }', returnByValue: true });
      o[n] = r.result.value; await cdp.send('Runtime.releaseObject', { objectId: q.objects.objectId }).catch(() => {}); } catch(e){}
  }
  return o;
}
const gc = async () => { try { await cdp.send('HeapProfiler.collectGarbage'); } catch(e){} };

const samples = [], events = [];
const ev = (t, what) => { events.push({ t: Math.round(t / 1000), what }); console.log(`  [${(t / 1000).toFixed(0).padStart(4)} s] ${what}`); };
await run(`tStart('song', {row:0}); return 1;`); await sleep(2500);
const started = await run(`return {on:tp.on, core:!!tp.core, seq:aset.core};`);
console.log(`soak: ${MIN} min, seq=${SEQ} (playing: on=${started.on}, core=${started.core}), song=${songName}, ${PHONE ? 'PHONE (touch, Mobile setup, CPU x' + THROTTLE + ')' : 'desktop'}, sample every ${DT / 1000} s, load ${os.loadavg().map(x => x.toFixed(2)).join(' ')}`);
if (!started.on) add('setup: the song plays', 'FAIL', JSON.stringify(started));
const t0 = performance.now(); let n = 0, lastGC = -1e9, prev = null, reloaded = false, restarts = 0; const actErr = [];
const done = { stopplay: new Set(), tour: new Set(), panels: new Set() };
const sched = (key, t, every, off) => { if (t < off) return false; const k = Math.floor((t - off) / every); if (done[key].has(k)) return false; done[key].add(k); return true; };
async function act(name, src){ try { const r = await run(src); ev(performance.now() - t0, name + (typeof r === 'string' ? ': ' + r : '')); } catch(e){ actErr.push(name + ': ' + e.message.slice(0, 160)); ev(performance.now() - t0, name + ' THREW ' + e.message.slice(0, 120)); } }
for (;;) {
  const t = performance.now() - t0; if (t >= T_END) break;
  // the schedule (offsets keep the actions apart): STOP/PLAY every 60 s; screen tour every 120 s from 90 s; Settings + pump window + waves/chains every 180 s from 150 s; a song reload at half time
  if (ONLY.includes('stopplay') && sched('stopplay', t, 60000, 60000)) await act('STOP then PLAY', A_STOPPLAY);
  if (ONLY.includes('tour') && sched('tour', t, 120000, 90000)) await act('screen tour (6 screens, synth, waves, chains, back)', A_TOUR);
  if (ONLY.includes('panels') && sched('panels', t, 180000, 150000)) {
    const sb = page.locator('#bSet'); let set = 'no visible #bSet';
    if (await sb.isVisible().catch(() => false)) { try { await sb.click({ timeout: 15000 }); await sleep(700); await page.keyboard.press('Escape'); await sleep(300); await page.keyboard.press('Escape'); set = 'Settings opened and closed'; } catch(e){ set = 'Settings: ' + e.message.slice(0, 80); } }
    ev(performance.now() - t0, set); await act('pump window', A_PUMP); await act('waves + chains screens', A_FRAMES);
  }
  if (ONLY.includes('reload') && !reloaded && t >= T_END / 2) { reloaded = true; try { await loadSong(); ev(performance.now() - t0, 'song loaded again (setSong path)'); await sleep(800); await run(`tStart('song', {row:0}); return 1;`); } catch(e){ actErr.push('reload: ' + e.message.slice(0, 160)); } }
  // keep it playing: if the song ended (or something stopped it), count that and start again
  const on = await run(`return tp.on;`).catch(() => null);
  if (on === false) { restarts++; ev(performance.now() - t0, 'song was not playing: PLAY again'); await run(`tStart('song', {row:0}); return 1;`).catch(() => {}); await sleep(1500); }
  const withGC = t - lastGC >= 30000; let objsN = null; if (withGC) { await gc(); lastGC = t; objsN = await objs(); }
  const a = await run(SAMPLE).catch(e => ({ err: e.message.slice(0, 100) })), m = await metrics(), d = await dom(), p = await proc(), wall = performance.now();
  const lt = a.lt || []; delete a.lt;
  const s = { t: (wall - t0) / 1000, load: os.loadavg()[0], ...a, ltN: lt.length, ltMs: lt.reduce((x, y) => x + y, 0), ltMax: Math.max(0, ...lt), heap: m.JSHeapUsedSize, heapTotal: m.JSHeapTotalSize, gcHeap: withGC ? m.JSHeapUsedSize : null,
    nodes: d.nodes ?? m.Nodes, listeners: d.jsEventListeners ?? m.JSEventListeners, docs: d.documents ?? m.Documents, framesM: m.Frames, script: m.ScriptDuration, task: m.TaskDuration, rss: p.rss, rssR: p.rssRenderer, procCpu: p.cpu, objs: objsN };
  if (prev) { const dw = (wall - prev.wall) / 1000, dc = (a.ct ?? NaN) - (prev.ct ?? NaN); s.driftMs = isFinite(dc) ? (dc - dw) * 1000 : null; s.rafHz = (a.raf - prev.raf) / dw; s.cpuPct = (p.cpu - prev.cpu) / dw * 100; }
  samples.push(s); prev = { wall, ct: a.ct, raf: a.raf, cpu: p.cpu }; n++;
  if (n % 6 === 0) console.log(`  [${s.t.toFixed(0).padStart(4)} s] heap ${f3(s.heap / 1e6)} MB nodes ${s.nodes} lst ${s.listeners} alive ${s.alive} bass ${s.bassN} VAR ${s.varMap}/${f3((s.varBytes || 0) / 1e6)} MB glitch ${s.glitch} ps ${s.psEv ?? '-'} lt ${s.ltN} drift ${s.driftMs != null ? s.driftMs.toFixed(0) : '-'} ms load ${s.load.toFixed(2)}`);
  const left = DT - (performance.now() - t0 - t); if (left > 0) await sleep(left);
}
  const errs = takeErrors(B.errors); await B.close().catch(() => {});
  return { samples, errs, restarts, actErr, started, events, metricNames: [...metricNames] };
}
let R; if (RESCORE) { const j = JSON.parse(fs.readFileSync(RESCORE, 'utf8')); R = { samples: j.samples, errs: [], restarts: j.events.filter(e => /not playing/.test(e.what)).length, actErr: [], started: j.started, events: j.events, metricNames: j.metricNames }; } else R = await soak();
const { samples, errs, restarts, actErr, started, events, metricNames } = R; const lastS = samples[samples.length - 1];
// ---- verdicts ----
// bound = the per-minute growth that still passes. kind 'leak': FAIL when the slope is over the bound and the late third is above the early third.
// 'load': timing, depends on the machine: only a NOTE. 'cap': printed, or (PB.log) checked against its cap.
const SERIES = [
  ['JS heap after forced GC (MB)', s => s.gcHeap == null ? null : s.gcHeap / 1e6, 1.0, 'leak'],
  ['DOM nodes (floor of each minute)', s => s.nodes, 20, 'leak', 'floor'], ['JS event listeners (floor of each minute)', s => s.listeners, 20, 'leak', 'floor'], ['documents', s => s.docs, 0.2, 'leak'],
  ['renderer RSS (MB, includes wasm + audio buffers)', s => s.rssR / 1e6, 3, 'leak'], ['all browser processes RSS (MB)', s => s.rss / 1e6, 5, 'leak'],
  ['app: voices alive (rt.pl.aliveN)', s => s.alive, 0.5, 'leak'], ['app: bassN size', s => s.bassN, 0.2, 'leak'], ['app: bassLive size', s => s.bassLive, 0.2, 'leak'],
  ['app: padSrcs entries', s => s.padSrcs, 0.5, 'leak'], ['app: VAR.map size', s => s.varMap, 0.2, 'leak'], ['app: VAR.bytes (MB)', s => s.varBytes / 1e6, 0.1, 'leak'],
  ['app: tp.q length', s => s.tpQ, 0.5, 'leak'], ['app: MOB.q length', s => s.mobQ, 0.5, 'leak'], ['app: MOLED length', s => s.moled, 0.5, 'leak'], ['app: PB.log length (cap 400)', s => s.pbLog, 1e9, 'cap'],
  ['app: undo.past length', s => s.undo, 0.5, 'leak'], ['localStorage chars', s => s.ls, 5000, 'leak'],
  ['live setInterval ids (top page)', s => s.iv, 0.2, 'leak'], ['pending setTimeout ids (top page)', s => s.to, 1, 'leak'], ['frames: live intervals', s => s.frameIv, 0.2, 'leak'], ['frames: pending timeouts', s => s.frameTo, 1, 'leak'],
  ['AudioContexts alive (top page and frames)', s => s.ctxLive, 0.1, 'leak'],
  ['AudioNode wrappers (queryObjects)', s => s.objs ? s.objs.AudioNode : null, 3, 'leak'], ['AudioBufferSourceNode wrappers', s => s.objs ? s.objs.AudioBufferSourceNode : null, 2, 'leak'],
  ['GainNode wrappers', s => s.objs ? s.objs.GainNode : null, 2, 'leak'], ['AudioWorkletNode wrappers', s => s.objs ? s.objs.AudioWorkletNode : null, 0.5, 'leak'], ['AudioParam wrappers (lazy: made when a param is first touched)', s => s.objs ? s.objs.AudioParam : null, 1e9, 'cap'],
  ['AudioBuffer wrappers', s => s.objs ? s.objs.AudioBuffer : null, 1, 'leak'], ['MessagePort wrappers', s => s.objs ? s.objs.MessagePort : null, 1, 'leak'], ['canvas elements (watch: screens build theirs lazily)', s => s.objs ? s.objs.HTMLCanvasElement : null, 0.2, 'watch'],
  ['clock drift per sample (ms, audio minus wall)', s => s.driftMs, 5, 'load'], ['long tasks > 50 ms per sample', s => s.ltN, 0.3, 'load'], ['long-task ms per sample', s => s.ltMs, 30, 'load'],
  ['rAF Hz', s => s.rafHz, 1e9, 'cap'], ['browser CPU % (all processes)', s => s.cpuPct, 1e9, 'cap'], ['load average (1 min)', s => s.load, 1e9, 'cap'],
  ['app dropout counter (glitch.n)', s => s.glitch, 0.5, 'load'], ['playbackStats underrun events', s => s.psEv, 0.5, 'load'], ['playbackStats underrun seconds', s => s.psDur, 0.05, 'load'], ['playbackStats average latency (ms)', s => s.psLat == null ? null : s.psLat * 1000, 2, 'load'],
  ['CPU meter (globalCpuValue, else cpu.cap.avg)', s => s.cpuMeter ?? s.cpuAvg, 0.02, 'load'], ['core skips (PB.skips)', s => s.pbSkips, 1, 'load'], ['latest note late: PB.late (s)', s => s.pbLate, 0.01, 'load'], ['dropped notes: PB.drop', s => s.pbDrop, 0.5, 'load'],
  ['page ticks: least lead (s)', s => s.pgLead === 9 ? null : s.pgLead, 1e9, 'cap'], ['page ticks: moved on (count in window)', s => s.pgRe, 0.5, 'load'], ['page ticks: held (count in window)', s => s.pgHeld, 0.5, 'load'],
];
console.log('\n' + '-'.repeat(120) + `\nSERIES (fit after the ${WARM}-minute warm-up; slope per minute; load mean ${f3(avg(samples.map(s => s.load)))}, max ${f3(Math.max(...samples.map(s => s.load)))})`);
const table = [];
for (const [name, get, bound, kind, mode] of SERIES) {
  const all = samples.map(s => [s.t / 60, get(s)]).filter(([, v]) => v != null && isFinite(v)); if (!all.length) { add(name, 'SKIP', 'not exposed'); continue; }
  let use = all.filter(([x]) => x >= WARM); const first = all[0][1], last = all[all.length - 1][1];
  // 'floor': a screen's DOM comes and goes (the tour, a frame loading), so judge the lowest value in each minute, not the sawtooth
  // (the song reload throws the waves / chains frames away; they come back on the next visit: leave out the 90 s after it)
  if (mode === 'floor') { const rl = events.find(e => /loaded again/.test(e.what)); const by = new Map(); for (const [x, v] of use) { if (rl && x * 60 >= rl.t && x * 60 < rl.t + 90) continue; const k = Math.floor(x); by.set(k, Math.min(by.has(k) ? by.get(k) : Infinity, v)); } use = [...by].map(([k, v]) => [k + 0.5, v]); }
  const f = fit(use.map(x => x[0]), use.map(x => x[1])), k = Math.max(1, Math.floor(use.length / 3)), early = avg(use.slice(0, k).map(x => x[1])), late = avg(use.slice(-k).map(x => x[1]));
  const mx = Math.max(...all.map(x => x[1])), mn = Math.min(...all.map(x => x[1]));
  const txt = `start ${f3(first)} end ${f3(last)} min ${f3(mn)} max ${f3(mx)} slope ${f3(f.slope)}/min (bound ${bound >= 1e9 ? '-' : f3(bound)}; early third ${f3(early)} late third ${f3(late)}; n ${use.length})`;
  table.push({ name, kind, first, last, min: mn, max: mx, slope: f.slope, bound, early, late, n: use.length });
  if (kind === 'cap') { add(name, name.includes('PB.log') ? (mx <= 400 ? 'PASS' : 'FAIL') : 'NOTE', txt); continue; }
  if (use.length < 4) { add(name, 'SKIP', 'too few samples after the warm-up: ' + txt); continue; }
  // a step (a screen's first visit, a cache filling) is not growth: it has to keep rising in the second half of the run too
  const half = use.filter(([x]) => x >= (use[0][0] + use[use.length - 1][0]) / 2), f2 = fit(half.map(x => x[0]), half.map(x => x[1]));
  const over = f.slope > bound && late > early && f2.slope > bound, step = f.slope > bound && late > early && !over;
  add(name, over ? (kind === 'leak' ? 'FAIL' : 'NOTE') : 'PASS', txt + (over && kind === 'load' ? ' (timing: may be machine load)' : '') + (step ? ` (a step, flat in the second half: slope ${f3(f2.slope)}/min)` : ''));
}
{ const r = samples.filter(s => s.objs && s.objs.AudioNode).map(s => s.objs.AudioParam / s.objs.AudioNode); const mxr = Math.max(...r); add('AudioParam wrappers per AudioNode wrapper stay bounded (<= 4)', mxr <= 4 ? 'PASS' : 'FAIL', `max ${f3(mxr)}, last ${f3(r[r.length - 1])} (a param's wrapper is made once, when the page first touches it, and lives as long as its node)`); }
const dr = samples.map(s => s.driftMs).filter(x => x != null), tot = dr.reduce((a, b) => a + b, 0);
add('audio clock vs wall clock over the run', Math.abs(tot) < 500 ? 'PASS' : 'NOTE', `${f3(tot)} ms total over ${f3(lastS.t)} s (${f3(tot / (lastS.t / 60))} ms/min; headless has no real device, a shared machine moves this)`);
add('dropouts in the whole run', (lastS.glitch || 0) === 0 && !(lastS.psEv > 0) ? 'PASS' : 'NOTE', `glitch.n ${lastS.glitch}, playbackStats events ${lastS.psEv ?? 'n/a'} (${lastS.psDur != null ? f3(lastS.psDur) + ' s' : 'n/a'}), latest note late max ${f3(Math.max(...samples.map(s => s.pbLate || 0)) * 1000)} ms, notes dropped max ${Math.max(...samples.map(s => s.pbDrop || 0))}; load max ${f3(Math.max(...samples.map(s => s.load)))}`);
add('song kept playing (no restarts needed)', restarts === 0 ? 'PASS' : 'NOTE', `${restarts} restart(s)`);
add('page errors during the run', errs.length === 0 ? 'PASS' : 'FAIL', errs.slice(0, 5).map(e => e.slice(0, 160)).join(' | '));
add('scheduled actions ran without throwing', actErr.length === 0 ? 'PASS' : 'FAIL', actErr.join(' | '));
if (RESCORE) { const bad0 = rows.filter(r => r[1] === 'FAIL'); console.log(`${rows.length} rows, ${rows.filter(r => r[1] === 'PASS').length} PASS, ${bad0.length} FAIL, ${rows.filter(r => r[1] === 'NOTE').length} NOTE, ${rows.filter(r => r[1] === 'SKIP').length} SKIP`); process.exit(bad0.length ? 1 : 0); }
fs.mkdirSync(OUT, { recursive: true });
const file = path.join(OUT, `soak-${SEQ}${PHONE ? '-phone' : ''}-${new Date().toISOString().replace(/[:.]/g, '-')}.json`);
fs.writeFileSync(file, JSON.stringify({ args: { MIN, SEQ, SONG, PHONE, THROTTLE, DT, WARM }, started, metricNames, events, table, samples }));
console.log(`\nraw samples: ${path.relative(ROOT, file)}`);
const bad = rows.filter(r => r[1] === 'FAIL'); console.log(`${rows.length} rows, ${rows.filter(r => r[1] === 'PASS').length} PASS, ${bad.length} FAIL, ${rows.filter(r => r[1] === 'NOTE').length} NOTE, ${rows.filter(r => r[1] === 'SKIP').length} SKIP`);
process.exit(bad.length ? 1 : 0);
