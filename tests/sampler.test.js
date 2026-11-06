// SAMPLER (5 Nov): stereo samples (the channel reader, the save format, the audio thread's pan law) and sample loops.
// The browser end to end, both sequencers: tools/sampler-check.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html'), core = H.read('core/core-processor.js');
function cut(text, re, what) { const m = text.match(re); assert.ok(m, `could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }

test('stereo: the audio thread pans a stereo pad as a StereoPannerNode does (balance), centred = untouched', () => {
  const stBal = new Function(cut(core, /function stBal\(pan\)\{[^\n]*\}/, 'stBal') + '; return stBal;')();
  assert.deepEqual(stBal(0).map(x => +x.toFixed(9)), [1, 0, 0, 1]);
  const [ll, rl, lr, rr] = stBal(-1); assert.equal(ll, 1); assert.ok(Math.abs(rl - 1) < 1e-9); assert.equal(lr, 0); assert.ok(Math.abs(rr) < 1e-9);   // hard left: R folds into L
  const R1 = stBal(1); assert.ok(Math.abs(R1[0]) < 1e-9); assert.ok(Math.abs(R1[2] - 1) < 1e-9); assert.equal(R1[3], 1);
});

test('stereo: an AudioBuffer is read as two channels, up to SMP_MAX seconds; identical channels count as mono', () => {
  const SMP_MAX = +/const SMP_MAX = (\d+);/.exec(src)[1]; assert.ok(SMP_MAX >= 120);
  const smpChannels = new Function('SMP_MAX', cut(src, /function smpChannels\(b\)\{[\s\S]*?\n\}\n/, 'smpChannels') + '; return smpChannels;')(SMP_MAX);
  const buf = chs => ({ numberOfChannels: chs.length, length: chs[0].length, sampleRate: 100, getChannelData: c => chs[c] });
  const L = Float32Array.from({ length: 50 }, (_, i) => Math.sin(i)), R = Float32Array.from({ length: 50 }, (_, i) => Math.cos(i));
  let r = smpChannels(buf([L, R])); assert.ok(r.d2); assert.equal(r.d[3], L[3]); assert.equal(r.d2[3], R[3]);
  r = smpChannels(buf([L, L.slice()])); assert.equal(r.d2, null, 'the same on both sides: kept as mono');
  r = smpChannels(buf([L])); assert.equal(r.d2, null); assert.equal(r.n, 50);
  const long = new Float32Array(100 * (SMP_MAX + 5)); r = smpChannels(buf([long])); assert.equal(r.n, 100 * SMP_MAX, 'cut at SMP_MAX');
});

test('stereo: a sample saves and loads its right channel (b64r), and mono files still load', () => {
  const env = new Function('btoa', 'atob', 'smpId', cut(src, /function smpToJSON\(S\)\{[\s\S]*?\nfunction smpB64\(d\)\{[\s\S]*?\n\}\n/, 'smpToJSON')
    + cut(src, /function smpFromJSON\(j\)\{[\s\S]*?\nfunction smpUnB64\(b\)\{[\s\S]*?\n\}\n/, 'smpFromJSON') + '; return {smpToJSON, smpFromJSON};')(
    s => Buffer.from(s, 'binary').toString('base64'), b => Buffer.from(b, 'base64').toString('binary'), () => 'id1');
  const S = { sr: 44100, name: 'x', id: 'a', data: Float32Array.from([0, 0.5, -0.5]), data2: Float32Array.from([0.25, 0, -1]) };
  const j = env.smpToJSON(S); assert.ok(j.b64 && j.b64r);
  const back = env.smpFromJSON(JSON.parse(JSON.stringify(j))); assert.ok(Math.abs(back.data2[0] - 0.25) < 1e-4 && Math.abs(back.data2[2] + 1) < 1e-4);
  const mono = env.smpFromJSON({ sr: 44100, name: 'm', b64: j.b64 }); assert.equal(mono.data2, undefined); assert.ok(Math.abs(mono.data[1] - 0.5) < 1e-4);
});

test('stereo: every page player builds two-channel buffers from c.data2 (not the other mkBuf)', () => {
  assert.match(src, /function stBuf\(ac, d, d2, sr\)\{/);
  assert.equal((src.match(/function mkBuf\(/g) || []).length, 1, 'one mkBuf only (a second one silently replaced the first: every Page note threw)');
  for (const re of [/bufs\[i\]\._d !== c\.data\)\{ const b = stBuf\(ctx, c\.data, c\.data2, c\.sr\)/, /const b = stBuf\(ctx, d, d2, c\.sr\); e = \{d:c\.data, b\}/]) assert.match(src, re);
  assert.match(src, /corePost\(\{op:'buf', pad:i, data:d, data2:d2, sr:c\.sr\}/);
  assert.match(core, /coreBufs\.set\(d\.pad, \{data:d\.data, data2:d\.data2 \|\| null, sr:d\.sr\}\)/);
  // SMP_MAX sits in the engine section the render Worker gets
  const A = src.indexOf('/* ===ENGINE START=== */'), B = src.indexOf('/* ===ENGINE END=== */'), k = src.indexOf('const SMP_MAX');
  assert.ok(k > A && k < B, 'SMP_MAX must be inside the engine section (the render Worker only has that)');
});
