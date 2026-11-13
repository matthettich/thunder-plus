// FIRST PLAY PRESS (13 Nov 2026): a real touch on the PLAY pad key (#kPlay) in the Fold profile right after load (nothing warmed), with long animation frames and the
// cost of the functions the press runs. node tests/sweep/first-play.mjs [--throttle=4] [--size=open]
import fs from 'fs'; import path from 'path'; import { ROOT, sleep, serve, launch } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const THR = +arg('throttle', 4), W = arg('size', 'open') === 'cover' ? [412, 915] : [884, 1020];
const { srv, port } = await serve(); const C = await launch(); const { send } = C;
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S);
const ev = async e => { const r = await P('Runtime.evaluate', { expression: e, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception ? r.exceptionDetails.exception.description : r.exceptionDetails.text); return r.result.value; };
const app = src => ev(`window.__seqTest.ev(${JSON.stringify('(async () => {' + src + '\n})()')})`);
await P('Page.enable'); await P('Runtime.enable');
await P('Emulation.setDeviceMetricsOverride', { width: W[0], height: W[1], deviceScaleFactor: 2.5, mobile: true }); await P('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
await P('Emulation.setUserAgentOverride', { userAgent: 'Mozilla/5.0 (Linux; Android 14; SM-F956B) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/153.0.0.0 Mobile Safari/537.36', userAgentMetadata: { platform: 'Android', platformVersion: '14', architecture: '', model: 'SM-F956B', mobile: true, brands: [{ brand: 'Chromium', version: '153' }], fullVersionList: [{ brand: 'Chromium', version: '153.0.0.0' }] } });
await P('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { try { localStorage.setItem('thunderplus-platform-v1', 'mobile'); localStorage.setItem('thunderplus-mobile-chains-v1', '1'); localStorage.setItem('thunderplus-mobile-waves-v1', '1'); } catch(e){}
  window.__fp = {loaf: [], lt: []}; try { new PerformanceObserver(l => { for (const e of l.getEntries()) window.__fp.loaf.push({t: e.startTime, d: e.duration, rs: e.renderStart, sc: (e.scripts || []).map(s => [s.invoker, s.sourceFunctionName, s.sourceCharPosition, +s.duration.toFixed(1)])}); }).observe({type: 'long-animation-frame', buffered: true}); } catch(e){} })();` });
await P('Page.navigate', { url: `http://localhost:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500); await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`); await sleep(800);
await ev(`window.__seqTest.load(${JSON.stringify(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'))}, 'ELECTRO.thunder')`); await sleep(500);
await app(`setView('tracker'); go('song'); const wrapFn = (n) => { const f = eval(n); if (typeof f !== 'function') return; window.__fn = window.__fn || {}; window.__fn[n] = {n:0, tot:0}; eval(n + ' = function(...a){ const t = performance.now(); try { return f.apply(this, a); } finally { const q = window.__fn["' + n + '"]; q.n++; q.tot += performance.now() - t; } }'); };
  for (const n of ['tStart','draw','makeSeq','liveWarm','ensureLive','sfPrepare','rtInit','moStart','pump','songBtn','chainsTempo','hmFrame','hmDraw','coreStart','corePrep','bgPlay','wakeLock','varPrefetch','playWaitFor','rtAllFx','chaseFx','pbLvReset','flushS']) wrapFn(n); return 1;`);
await sleep(1500);
if (THR > 1) await P('Emulation.setCPUThrottlingRate', { rate: THR });
const r = await app(`const b = document.getElementById('kPlay').getBoundingClientRect(); return {x: b.left + b.width / 2, y: b.top + b.height / 2, w: b.width, h: b.height};`); console.log('kPlay at', JSON.stringify(r));
const t0 = await ev('performance.now()');
await P('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x: r.x, y: r.y }] }); await sleep(60); await P('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
await sleep(3000);
const D = await ev(`JSON.stringify({fn: window.__fn, loaf: window.__fp.loaf.filter(l => l.t >= ${t0} - 50), on: window.__seqTest.ev('tp.on')})`); const o = JSON.parse(D);
console.log('tp.on', o.on); for (const [k, v] of Object.entries(o.fn).filter(([, v]) => v.n).sort((a, b) => b[1].tot - a[1].tot)) console.log('  ' + k.padEnd(12) + v.n + ' calls ' + v.tot.toFixed(1) + ' ms');
for (const l of o.loaf.slice(0, 8)) console.log(`LoAF at ${(l.t - t0).toFixed(0)} ms: ${l.d.toFixed(0)} ms, render start +${(l.rs - l.t).toFixed(0)}; scripts ` + l.sc.map(s => s.join(' ')).join(' | '));
await C.close(); srv.close(); process.exit(0);
