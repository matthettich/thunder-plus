// IDLE WAKE-UPS (13 Nov 2026): what keeps running while the song is STOPPED and the app sits idle (battery). Counts timer, interval, rAF and observer callbacks
// in 10 s by call site, plus main-thread task time (CDP). Fold profile as draw-pass.mjs. node tests/sweep/idle-wake.mjs [--throttle=1] [--screen=song] [--secs=10]
import { ROOT, sleep, serve, launch } from './cdp-lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const THR = +arg('throttle', 1), SECS = +arg('secs', 10), SCREEN = arg('screen', 'song');
const { srv, port } = await serve(); const C = await launch(); const { send } = C;
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page');
const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S);
const ev = async e => { const r = await P('Runtime.evaluate', { expression: e, awaitPromise: true, returnByValue: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.text); return r.result.value; };
await P('Page.enable'); await P('Runtime.enable'); await P('Performance.enable');
await P('Emulation.setDeviceMetricsOverride', { width: 884, height: 1020, deviceScaleFactor: 2.5, mobile: true }); await P('Emulation.setTouchEmulationEnabled', { enabled: true, maxTouchPoints: 5 });
await P('Page.addScriptToEvaluateOnNewDocument', { source: `(() => { try { localStorage.setItem('thunderplus-platform-v1', 'mobile'); } catch(e){}
  const Z = window.__iw = {on: false, c: {}}, site = () => { const l = (new Error().stack || '').split('\\n'); for (let i = 3; i < l.length; i++){ const m = /index\\.html[^:]*:(\\d+):/.exec(l[i]); if (m) return m[1]; } return '?'; };
  const wrap = (obj, name, kind) => { const o = obj[name]; obj[name] = function(f, ...a){ const s = kind + '@' + site(); if (typeof f === 'function'){ const g = f; f = function(...x){ if (Z.on) Z.c[s] = (Z.c[s] || 0) + 1; return g.apply(this, x); }; } return o.call(this, f, ...a); }; };
  wrap(window, 'setTimeout', 'timeout'); wrap(window, 'setInterval', 'interval'); wrap(window, 'requestAnimationFrame', 'raf'); })();` });
await P('Page.navigate', { url: `http://localhost:${port}/index.html?seqtest=1` });
for (let i = 0; i < 200; i++) { if (await ev('!!window.__seqTest').catch(() => false)) break; await sleep(300); }
await sleep(2500); await ev(`(()=>{const b=[...document.querySelectorAll('button')].find(x=>/^Got it/.test(x.textContent)); if(b) b.click(); return 1})()`); await sleep(800);
await ev(`window.__seqTest.ev('go("${SCREEN}"); 1')`); await sleep(6000);   // let the start-up work and the 1.5 s audio wake settle
if (THR > 1) await P('Emulation.setCPUThrottlingRate', { rate: THR });
const mm = async () => { const m = {}; for (const x of (await P('Performance.getMetrics')).metrics) m[x.name] = x.value; return m; };
const m0 = await mm(); await ev('window.__iw.on = true; window.__iw.c = {}; 1'); await sleep(SECS * 1000); const m1 = await mm();
const c = await ev('JSON.stringify(window.__iw.c)'); const top = Object.entries(JSON.parse(c)).sort((a, b) => b[1] - a[1]);
console.log(`${SCREEN} stopped, idle, ${SECS} s at ${THR}x: task ${((m1.TaskDuration - m0.TaskDuration) * 1000 / SECS).toFixed(1)} ms/s, script ${((m1.ScriptDuration - m0.ScriptDuration) * 1000 / SECS).toFixed(1)} ms/s, layout ${(m1.LayoutCount - m0.LayoutCount) / SECS}/s, style ${(m1.RecalcStyleCount - m0.RecalcStyleCount) / SECS}/s`);
for (const [k, v] of top.slice(0, 15)) console.log('  ' + (v / SECS).toFixed(1).padStart(6) + '/s  ' + k);
await C.close(); srv.close(); process.exit(0);
