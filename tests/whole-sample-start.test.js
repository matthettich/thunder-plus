// WHOLE-SAMPLE STARTS (build 608): a sample note starts on a sample (the browser interpolates a start that falls between two, which
// dulled renders against the audio-thread sequencer; measured by tests/sweep/render-vs-live.mjs). Source guard plus the arithmetic.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers'), src = H.read('index.html');
test('startNote moves a sample’s start up to the next whole sample, and the fade-in and the source use that time', () => {
  const m = src.match(/const st0 = (Math\.ceil\(Math\.max\(ev\.t, ac\.currentTime\) \* ac\.sampleRate - 1e-6\) \/ ac\.sampleRate);\n[^\n]*\n\s*if \(off > 0\.0005\)\{ f\.gain\.setValueAtTime\(0, st0\); f\.gain\.linearRampToValueAtTime\(1, st0 \+ 0\.0015\); \}\n\s*src\.start\(st0,/);
  assert.ok(m, 'index.html: the whole-sample start in startNote moved or changed shape');
  const f = new Function('ev', 'ac', 'return ' + m[1]);
  for (const sr of [44100, 48000, 96000]) for (const t of [0, 0.1, 1 / 3, 1.23456789, 227.118, 600.5]){
    const st = f({ t }, { currentTime: 0, sampleRate: sr }), k = st * sr;
    assert.ok(Math.abs(k - Math.round(k)) < 1e-6, `on a sample at ${sr}: ${t} -> frame ${k}`);
    assert.ok(st >= t - 1e-9 && st - t < 1 / sr + 1e-9, 'never early, less than a sample late');
  }
  assert.equal(f({ t: 480 / 48000 }, { currentTime: 0, sampleRate: 48000 }), 0.01, 'a start already on a sample stays');
  assert.equal(f({ t: 0.5 }, { currentTime: 2, sampleRate: 48000 }), 2, 'a late note starts now');
});
