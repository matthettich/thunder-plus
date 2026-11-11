// Reads a Chrome trace saved by core-profile.mjs --do=trace (webaudio + audionode categories) and reports, for the audio render
// thread: the cost of each render quantum (RealtimeAudioDestinationHandler::Render: wall and thread-CPU microseconds), and the
// SELF time of every node type (a node's ProcessIfNecessary minus the nodes it pulled from), with v8 calls split out.
//   node tests/sweep/cp-trace-analyse.mjs <trace.json> [--budget=2902]
import fs from 'fs';
const f = process.argv[2], B = +((process.argv.find(x => x.startsWith('--budget=')) || '--budget=2902').slice(9));
const E = JSON.parse(fs.readFileSync(f, 'utf8')), tn = new Map();
for (const e of E) if (e.ph === 'M' && e.name === 'thread_name') tn.set(e.pid + ':' + e.tid, e.args.name);
const R = E.filter(e => e.ph === 'X' && /Realtime AudioWorklet/.test(tn.get(e.pid + ':' + e.tid) || '')).sort((a, b) => a.ts - b.ts || b.dur - a.dur);
const q = R.filter(e => e.name === 'RealtimeAudioDestinationHandler::Render'), dur = q.map(e => e.dur).sort((a, b) => a - b), tdur = q.map(e => e.tdur || 0).sort((a, b) => a - b);
const pc = (a, p) => a[Math.min(a.length - 1, Math.floor(a.length * p))], mean = a => a.reduce((x, y) => x + y, 0) / a.length;
const span = (q[q.length - 1].ts - q[0].ts) / 1e6;
console.log(`quanta ${q.length} over ${span.toFixed(1)} s; wall us: mean ${mean(dur).toFixed(0)} p50 ${pc(dur, .5)} p90 ${pc(dur, .9)} p99 ${pc(dur, .99)} max ${dur[dur.length - 1]}; thread-cpu us: mean ${mean(tdur).toFixed(0)} p99 ${pc(tdur, .99)} max ${tdur[tdur.length - 1]}; over ${B} us: ${dur.filter(x => x > B).length}`);
console.log(`render-thread busy: wall ${(q.reduce((a, e) => a + e.dur, 0) / 1e6 / span * 100).toFixed(1)}% , cpu ${(q.reduce((a, e) => a + (e.tdur || 0), 0) / 1e6 / span * 100).toFixed(1)}% of the traced span`);
// self time by stack walk
const self = new Map(), cnt = new Map(), stack = []; const add = (k, d, t) => { const o = self.get(k) || [0, 0]; o[0] += d; o[1] += t; self.set(k, o); cnt.set(k, (cnt.get(k) || 0) + 1); };
const kids = new Map();
for (const e of R) { while (stack.length && stack[stack.length - 1].ts + stack[stack.length - 1].dur <= e.ts) stack.pop(); if (stack.length) { const p = stack[stack.length - 1]; const k = kids.get(p) || [0, 0]; k[0] += e.dur; k[1] += e.tdur || 0; kids.set(p, k); } stack.push(e); }
for (const e of R) { if (e.name === 'PushPullFIFO::Push' || e.name === 'PushPullFIFO::Push under lock' || e.name === 'AudioDestination::RequestRender') continue;
  const k = kids.get(e) || [0, 0], nm = e.name === 'AudioHandler::ProcessIfNecessary' ? 'node ' + (e.args['node type'] || '?') : e.name; add(nm, e.dur - k[0], (e.tdur || 0) - k[1]); }
const tot = q.reduce((a, e) => a + e.dur, 0);
console.log('-- self time by node type (share of render-thread wall time inside Render quanta; us per quantum; calls per quantum)');
for (const [k, v] of [...self].sort((a, b) => b[1][0] - a[1][0]).slice(0, 14)) console.log(`${(v[0] / tot * 100).toFixed(1).padStart(5)}%  ${(v[0] / q.length).toFixed(1).padStart(7)} us/q  cpu ${(v[1] / q.length).toFixed(1).padStart(7)} us/q  ${(cnt.get(k) / q.length).toFixed(1).padStart(6)} calls/q  ${k}`);
const gl = E.filter(e => e.name === 'Glitch!' && /Realtime/.test(tn.get(e.pid + ':' + e.tid) || '')).length; console.log('Glitch! events on the render thread', gl, `(${(gl / span * 60).toFixed(0)}/min)`);
if (process.argv.includes('--worst')) { const w = q.map(e => e).sort((a, b) => b.dur - a.dur).slice(0, 5); for (const e of w) { const inside = R.filter(x => x.ts >= e.ts && x.ts + x.dur <= e.ts + e.dur && x.name === 'v8.callFunction').sort((a, b) => b.dur - a.dur).slice(0, 3); console.log('worst quantum', e.dur, 'us tdur', e.tdur, 'top v8 calls', inside.map(x => x.dur).join(',')); } }
// per output callback (AudioDestination::RequestRender, about 10 ms of audio) and what the glitches line up with
{ const rr = R.filter(e => e.name === 'AudioDestination::RequestRender'), d = rr.map(e => e.dur).sort((a, b) => a - b);
  if (d.length) console.log(`callbacks ${d.length}: wall us mean ${mean(d).toFixed(0)} p50 ${pc(d, .5)} p90 ${pc(d, .9)} p99 ${pc(d, .99)} p99.9 ${pc(d, .999)} max ${d[d.length - 1]}; frames_to_render mean ${mean(rr.map(e => e.args.frames_to_render || 0)).toFixed(0)}; over 8 ms ${d.filter(x => x > 8000).length}, over 10 ms ${d.filter(x => x > 10000).length}`);
  const gcs = R.filter(e => e.name === 'V8.GCScavenger'), gl = E.filter(e => e.name === 'Glitch!' && /Realtime/.test(tn.get(e.pid + ':' + e.tid) || '')), ng = gl.length;
  let withGC = 0, withBig = 0, after = 0; const big = new Map();
  for (const g of gl) { const c = rr.filter(e => e.ts <= g.ts && e.ts + e.dur >= g.ts - 12000).sort((a, b) => b.ts - a.ts)[0]; if (!c) continue;
    const inside = R.filter(x => x.ts >= c.ts - 15000 && x.ts <= g.ts && x.name === 'v8.callFunction').sort((a, b) => b.dur - a.dur)[0]; if (inside && inside.dur > 1500) withBig++;
    if (gcs.some(x => x.ts >= c.ts - 15000 && x.ts <= g.ts)) withGC++; }
  console.log(`glitches ${ng}; with a scavenge GC in the 15 ms before ${withGC}; with a single v8 call over 1.5 ms in the 15 ms before ${withBig}; GC scavenges ${gcs.length} (${(gcs.reduce((a, e) => a + e.dur, 0) / 1e3).toFixed(1)} ms total, max ${Math.max(0, ...gcs.map(e => e.dur))} us)`);
  const v = R.filter(e => e.name === 'v8.callFunction').map(e => e.dur).sort((a, b) => a - b); if (v.length) console.log(`v8 calls ${v.length}: mean ${mean(v).toFixed(0)} us, p99 ${pc(v, .99)}, p99.9 ${pc(v, .999)}, max ${v[v.length - 1]}; over 1 ms ${v.filter(x => x > 1000).length}, over 2 ms ${v.filter(x => x > 2000).length}`); }
