// Is the thunder-core node's width (NT + 5 = 37 outputs, 131 channels) a cost on its own? A do-nothing worklet node with 1 output
// against one with the core's 37 outputs, traced the same way as core-profile.mjs --do=trace (nothing else on the page).
//   node tests/sweep/wide-outputs.mjs        (prints per-quantum us: AudioWorkletHandler::Process + v8 call + render total)
import { sleep, serve, launch, machine } from './cdp-lib.mjs';
const { srv, port } = await serve(); const C = await launch(); const { send } = C;
const { targetInfos } = await send('Target.getTargets'); const pt = targetInfos.find(t => t.type === 'page'); const { sessionId: S } = await send('Target.attachToTarget', { targetId: pt.targetId, flatten: true });
const P = (m, p) => send(m, p, S); await P('Page.enable'); await P('Runtime.enable'); await P('Page.navigate', { url: `http://localhost:${port}/tests/sweep/wide-outputs.html` }); await sleep(1500);
const evs = []; C.on(d => { if (d.method === 'Tracing.dataCollected') evs.push(...d.params.value); });
const mean = a => a.reduce((x, y) => x + y, 0) / (a.length || 1);
for (const [K, all, label] of [[1, false, '1 output (stereo)'], [37, false, '37 outputs, 131 channels, only #0 connected'], [37, true, '37 outputs, #0..#11 connected'], [1, false, '1 output again']]) {
  await P('Page.navigate', { url: `http://localhost:${port}/tests/sweep/wide-outputs.html` }); await sleep(1200);
  await P('Runtime.evaluate', { expression: `window.start(${K}, ${all})`, awaitPromise: true }); await sleep(2000); evs.length = 0;
  const done = new Promise(ok => C.on(d => { if (d.method === 'Tracing.tracingComplete') ok(); }));
  await send('Tracing.start', { traceConfig: { includedCategories: ['webaudio', 'disabled-by-default-webaudio.audionode', 'v8'] }, transferMode: 'ReportEvents' }); await sleep(6000); await send('Tracing.end'); await done;
  const tn = new Map(); for (const e of evs) if (e.ph === 'M' && e.name === 'thread_name') tn.set(e.pid + ':' + e.tid, e.args.name);
  const R = evs.filter(e => e.ph === 'X' && /Realtime AudioWorklet/.test(tn.get(e.pid + ':' + e.tid) || '')), q = R.filter(e => e.name === 'RealtimeAudioDestinationHandler::Render');
  const per = n => { const a = R.filter(e => e.name === n); return [a.reduce((x, e) => x + e.dur, 0) / q.length, a.reduce((x, e) => x + (e.tdur || 0), 0) / q.length]; };
  const aw = per('AudioWorkletHandler::Process'), v8 = per('v8.callFunction');
  console.log(`${label.padEnd(46)} quanta ${q.length}: AudioWorkletHandler::Process ${aw[0].toFixed(1)} us/q (cpu ${aw[1].toFixed(1)}), v8 call ${v8[0].toFixed(1)} us/q (cpu ${v8[1].toFixed(1)}), whole Render ${mean(q.map(e => e.dur)).toFixed(1)} us/q (cpu ${mean(q.map(e => e.tdur || 0)).toFixed(1)})`);
}
await C.close(); srv.close();
