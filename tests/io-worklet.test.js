// The native input's worklet (index.html IOIN_WORKLET, plan §4, A1.3), run frame by frame in Node with a stand-in for the
// AudioWorklet globals. Each input frame must come out at exactly the audio frame its timestamp says (+ D); while a take
// records, two drifting clocks must not shift it until 20 ms; gaps land at their true place; the monitor follows drift.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const SR = 48000, Q = 128;

function make() {
  const src = H.read('index.html').match(/const IOIN_WORKLET = `([\s\S]*?)`;/)[1];
  const g = { sampleRate: SR, currentFrame: 0, proc: null };
  class AWP { constructor() { this.port = { onmessage: null, sent: [], postMessage(m) { this.sent.push(m); } }; } }
  new Function('AudioWorkletProcessor', 'registerProcessor', 'G', src.replace(/\bsampleRate\b/g, 'G.sampleRate').replace(/\bcurrentFrame\b/g, 'G.currentFrame'))(
    AWP, (name, C) => { g.C = C; }, g);
  const p = new g.C();
  const msg = d => p.port.onmessage({ data: d });
  const outs = [[new Float32Array(Q), new Float32Array(Q)], [new Float32Array(Q), new Float32Array(Q)]];
  const run = n => { const rec = [[], []], mon = [[], []];   // n quanta; returns what both outputs played
    for (let k = 0; k < n; k++) { for (const o of outs) for (const c of o) c.fill(0); p.process([], outs);
      for (let i = 0; i < Q; i++) { rec[0].push(outs[0][0][i]); rec[1].push(outs[0][1][i]); mon[0].push(outs[1][0][i]); } g.currentFrame += Q; }
    return { rec, mon }; };
  return { p, g, msg, run };
}
// a chunk of nch channels whose channel c holds a code for (input frame, channel), so any misplacement shows
const code = (f, c) => ((f % 4096) + 1) / 8192 + c / 64;
function chunk(f0, n, t, nch = 28) { const data = new Float32Array(n * nch); for (let i = 0; i < n; i++) for (let c = 0; c < nch; c++) data[i * nch + c] = code(f0 + i, c); return { f0, t, n, nch, data }; }

test('native input worklet: each input frame plays at exactly its timestamp + D, on the picked channels', () => {
  const { msg, run, g } = make(), D = 0.05;
  msg({ D }); msg({ pick: [11, 16] });   // IN 12 and IN 17 (0-based 11, 16)
  // input frame f reached the computer when audio time 1.0 + f / SR was heard
  for (let f = 0; f < SR; f += 256) msg(chunk(f, 256, 1.0 + f / SR));
  g.currentFrame = Math.round(1.0 * SR);   // start rendering where frame 0 belongs, minus nothing: it plays D later
  const { rec } = run(Math.ceil((D * SR + 4000) / Q));
  const off = Math.round(D * SR);   // input frame f is at output index f + off
  for (const f of [0, 1, 255, 256, 1000, 3999]) {
    assert.equal(rec[0][f + off], Math.fround(code(f, 11)), 'left = IN 12, frame ' + f);
    assert.equal(rec[1][f + off], Math.fround(code(f, 16)), 'right = IN 17, frame ' + f);
  }
  assert.equal(rec[0][off - 1], 0, 'nothing before frame 0');
});

test('native input worklet: during a take the anchor holds through drift (no shift, no skipped frame) until 20 ms', () => {
  const { msg, run, g } = make(), D = 0.05;
  msg({ D }); msg({ pick: [0, 1] }); msg({ rec: true });
  // the interface runs 100 ppm fast against the audio clock: its timestamps creep 4.8 frames per second
  const T = f => 2.0 + f / SR / 1.0001;
  let f = 0; const feed = until => { for (; f < until; f += 480) msg(chunk(f, 480, T(f))); };
  feed(SR * 2); g.currentFrame = Math.round((T(0) + D) * SR);
  const a = run(Math.round(1.5 * SR / Q)).rec[0];
  // contiguous: consecutive output samples are consecutive input frames, all the way through (the drift is ~9 frames)
  const first = a.findIndex(v => v !== 0);
  for (let i = first; i < first + Math.round(1.4 * SR); i++) assert.equal(a[i], Math.fround(code(i - first, 0)), 'contiguous at ' + i);
});

test('native input worklet: not recording, the anchor follows the timestamps within 1 ms; a 20 ms drift moves it even mid-take', () => {
  const { msg, p } = make(), D = 0.05;
  msg({ D }); msg(chunk(0, 480, 1.0));
  const A0 = p.A; assert.equal(A0, Math.round((1.0 + D) * SR) - 0);
  msg(chunk(480, 480, 1.0 + 480 / SR + 0.0005)); assert.equal(p.A, A0, 'half a ms: within 1 ms, kept');
  msg(chunk(960, 480, 1.0 + 960 / SR + 0.002)); assert.equal(p.A, A0 + Math.round(0.002 * SR), '2 ms off, not recording: re-anchored');
  msg({ rec: true }); const A1 = p.A;
  msg(chunk(1440, 480, 1.0 + 1440 / SR + 0.012)); assert.equal(p.A, A1, '10 ms more while recording: held');
  msg(chunk(1920, 480, 1.0 + 1920 / SR + 0.025)); assert.notEqual(p.A, A1, 'past 20 ms: moved even while recording');
});

test('native input worklet: a gap from the helper is silence at its true place; late chunks are counted', () => {
  const { msg, run, g, p } = make(), D = 0.02;
  msg({ D }); msg(chunk(0, 480, 1.0)); msg(chunk(960, 480, 1.0 + 960 / SR));   // frames 480–959 never came
  g.currentFrame = Math.round((1.0 + D) * SR);
  const { rec } = run(Math.ceil(1440 / Q));
  assert.equal(rec[0][479], Math.fround(code(479, 0))); assert.equal(rec[0][480], 0); assert.equal(rec[0][959], 0);
  assert.equal(rec[0][960], Math.fround(code(960, 0)), 'after the gap, frames are where they belong');
  // playing past what has arrived counts as late
  let under = 0; for (let k = 0; k < 200; k++) { run(1); } for (const m of p.port.sent) under += m.under || 0;
  assert.ok(under > 0, 'playing past the newest frame is reported');
});

test('native input worklet: the monitor plays with a ~12 ms cushion and follows a drifting input without jumps', () => {
  const { msg, run } = make();
  msg({ D: 0.05 });
  let f = 0, mon = [];
  // feed 1.001x real time (the interface 1000 ppm fast: an extreme case), 5 ms chunks, and render in between
  for (let k = 0; k < 400; k++) { msg(chunk(f, 241, 1.0 + f / SR)); f += 241; mon = mon.concat(run(2).mon[0]); }
  // after settling, the monitor stays continuous except single-frame nudges (never a jump of more than 2 frames)
  let jumps = 0, big = 0, prev = null;
  for (let i = Math.round(0.5 * SR); i < mon.length; i++) { const v = mon[i]; if (!v) continue; const fr = Math.round((v - 0) * 8192 - 1);
    if (prev != null) { const d = fr - prev; if (d !== 1 && d !== 1 - 4096) { jumps++; if (Math.abs(d) > 2 && Math.abs(d - (1 - 4096)) > 2) big++; } } prev = fr; }
  assert.equal(big, 0, 'no big jumps in the monitor');
  assert.ok(jumps > 0, 'it nudged to follow the drift (' + jumps + ')');
});
