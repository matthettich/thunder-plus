// SAMPLER (build 655): job A, a loaded sample plays at its own pitch at C-4 (docs/SAMPLER-NOTES.md); job B, SYNC TO LINES (below).
// The code is cut out of index.html and run in a vm (tests/sampler-harness.js). The "before" numbers were recorded from build 647.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers'), S = require('./sampler-harness'), src = H.read('index.html');
const E = S.loadSampler(src);

const BEFORE_RENDER = { 44100: ['9e5f7f49', 4000], 48000: ['383a2c4e', 4000], 96000: ['dd080bd7', 4000] };   // build 647: an old-style sample layer (tune 0) in a synth slot
const oldVoice = (fsr, vtune) => { const v = E.mkVoice('S', [], { tune: vtune, len: 1.2, gate: 400, tail: 0 });
  v.layers[0] = E.freshLayer({ on: true, type: 'sample', level: 0.9, mode: 'AHD', atk: 0.1, hold: 1000, dec: 40, sample: { data: S.sine(1000, fsr, 1), sr: fsr, name: 't' } }); return v; };
// a fresh load through the app's own assignSample, into a slot whose voice has this tune
function freshLoad(fsr, vtune) {
  const v = E.mkVoice('S', [], { tune: vtune, len: 1.2, gate: 400, tail: 0 }); E.setKit({ voices: [v] }, { bpm: 120, lpb: 8, insts: [] }); E.setCur(0);
  E.assignSample(0, S.sine(1000, fsr, 1), fsr, 'fresh'); return v;
}

test('A: the cause, measured: an old-style sample layer in a synth slot (voice tune 24) sounds two octaves up at C-4 (rate 1)', () => {
  for (const fsr of [44100, 48000, 96000]) { const d = E.renderVoice(oldVoice(fsr, 24), 48000, null, 0);
    assert.equal(S.hash(d), BEFORE_RENDER[fsr][0], `an old song's sample at ${fsr} Hz renders bit for bit as in build 647`);
    assert.ok(Math.abs(S.freqOf(d, 48000, 0.2) - 4000) < 2, 'a 1000 Hz sample comes out at 4000 Hz'); }
});

test('A: a freshly loaded sample plays at its own pitch at C-4 on a synth slot (voice tune 24) and on a drum pad (0), at 44.1, 48 and 96 kHz', () => {
  for (const vt of [24, 0]) for (const fsr of [44100, 48000, 96000]) {
    const v = freshLoad(fsr, vt), L = v.layers[0];
    assert.equal(L.tune + v.tune, 0, `voice ${vt}: layer + voice tune = 0`);
    const d = E.renderVoice(v, 48000, null, 0);
    assert.ok(Math.abs(S.freqOf(d, 48000, 0.2) - 1000) < 1, `voice tune ${vt}, file ${fsr}: ${S.freqOf(d, 48000, 0.2)} Hz, want 1000 (rate 1 is the file's own speed)`);
    assert.ok(Math.abs(d.length / 48000 - 1) < 0.01, `voice tune ${vt}, file ${fsr}: the render is as long as the file (${d.length / 48000} s)`);
  }
  // rendering at another pad rate too: the render rate does not matter
  const d44 = E.renderVoice(freshLoad(48000, 24), 44100, null, 0); assert.ok(Math.abs(S.freqOf(d44, 44100, 0.2) - 1000) < 1);
});

test('A: the three ways of hearing a pad give rate 1 at C-4 (tap / Shift+Space, the keyboard at octave 4, the tracker)', () => {
  assert.match(src, /const NT = 32, NI = 64, ROOT = 48,/, 'ROOT is note 48, C-4');
  assert.match(src, /const n = clamp\(kOct \* 12 \+ semi, 0, 119\)/, 'the tracker keyboard: note = octave * 12 + semitone');
  assert.match(src, /let kOct = 4,/, 'the keyboard starts at octave 4');
  assert.match(src, /const st = scaleKey\(kOct \* 12 \+ pn\) - ROOT;/, 'the Synth page keyboard: semitones from ROOT');
  assert.match(src, /rate:Math\.pow\(2, \(note \+ I\.tsp - ROOT\) \/ 12\)/, 'the tracker preview: rate 2^((note - ROOT) / 12)');
  assert.match(src, /holdNote\(i, 0, vel\)/, 'a tap on a synth slot, or Shift+Space: 0 semitones');
  const noteRate = new Function(src.match(/function noteRate\(semis\)\{[\s\S]*?\n\}\n/)[0].replace('S.tune', 'null') + '; return noteRate;')();
  assert.equal(noteRate(0), 1); assert.equal(noteRate(12), 2);
  assert.equal(Math.pow(2, (48 + 0 - 48) / 12), 1, 'C-4 with TRANSPOSE 0');
});

test('A: nothing but a NEW load changes: the formulas are untouched, a saved layer keeps its tune, loading into a drum pad is as before', () => {
  assert.match(src, /const f0 = BASE \* Math\.pow\(2, \(L\.tune \+ V\.tune \+ L\.fine\/100\) \/ 12\);/, 'renderLayer pitch formula unchanged');
  assert.match(src, /const rate = Math\.pow\(2, \(\(L\.tune \|\| 0\) \+ \(v\.tune \|\| 0\) \+ \(L\.fine \|\| 0\) \/ 100\) \/ 12\);/, 'loopSecs unchanged');
  assert.equal(freshLoad(44100, 0).layers[0].tune, 0, 'a drum pad: tune 0 as always');
  assert.equal(freshLoad(44100, 24).layers[0].tune, -24);
  assert.equal(E.smpOwnTune({ tune: 60 }), -48, 'clamped to a layer’s range'); assert.equal(E.smpOwnTune({ tune: 0 }), 0); assert.ok(Object.is(E.smpOwnTune({ tune: 0 }), 0));
  assert.equal(E.smpOwnTune({ tune: -7 }), 7);
  // an old song: the sample layer is read from JSON (layerFromJSON: freshLayer(j)): the tune in the file is the tune it gets
  assert.equal(E.freshLayer({ type: 'sample', tune: 0 }).tune, 0); assert.equal(E.freshLayer({ type: 'sample', tune: -24 }).tune, -24);
});

test('A: a song with an old-style sample in a synth slot gives the same events and rates as build 647 (applyChop: 216 cases)', () => {
  const before = JSON.parse(H.read('tests/fixtures/sampler-chop-before-655.json'));
  assert.deepEqual(JSON.parse(JSON.stringify(S.chopFixture(E))), before);
});
