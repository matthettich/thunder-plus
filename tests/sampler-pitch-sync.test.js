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

/* ---------------- job B: SYNC TO LINES ---------------- */
const loadInst = o => { const int = (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d, clamp = (v, a, b) => Math.max(a, Math.min(b, v));
  const code = src.match(/I\.slc = clamp\(int\(o\.slc, 0\), 0, 64\);[\s\S]*?else delete I\.lines;/)[0];
  const I = { slc: 0, beats: 4, sync: 0, lines: 999 }; new Function('I', 'o', 'int', 'clamp', code)(I, o, int, clamp); return I; };
// a song's pad with a sample of 2.10 s in it, an instrument on it
function withPad(bpm, lpb, I) {
  const v = E.mkVoice('S', [], { tune: 0, len: 3, gate: 400, tail: 0 }); E.setKit({ voices: [v] }, { bpm, lpb, insts: [I] }); E.setCur(0);
  E.assignSample(0, S.sine(220, 44100, 2.1), 44100, 'brk'); const d = E.renderVoice(v, 48000, null, 0); E.ctx.__cache[0] = { data: d, sr: 48000 };
  return { d, loop: E.loopSecs(0, d, 48000) };
}
const chopOf = (bpm, lpb, I, n = 48) => { const { d, loop } = withPad(bpm, lpb, I), ev = { type: 'note', t: 1, rate: 1, ofs: 0, cut: null, slc: null };
  E.applyChop(ev, I, 0, n, 0, bpm, d, 48000); return { ev, loop }; };

test('B: a length in lines becomes seconds at the song’s LPB and BPM, and the sample is fitted to it (REPITCH rate = loop / target)', () => {
  for (const [lines, lpb, bpm] of [[32, 8, 131], [16, 4, 120], [12, 8, 90], [24, 8, 174], [1, 16, 60], [128, 4, 100], [1024, 16, 300]]) {
    const secs = lines * 60 / (bpm * lpb); assert.equal(E.slSecs(lines, lpb, bpm), secs);
    const { ev, loop } = chopOf(bpm, lpb, { pad: 0, tsp: 0, slc: 0, sync: 1, beats: 4, lines, grain: 1, ofs: 0 });
    assert.ok(Math.abs(ev.rate - loop / secs) < 1e-12, `${lines} lines at LPB ${lpb}, ${bpm} BPM: rate ${ev.rate} want ${loop / secs}`);
    assert.ok(Math.abs(ev.durSec / ev.rate - secs) < 1e-9, 'the note lasts exactly the length');
  }
  const { ev } = chopOf(131, 8, { pad: 0, tsp: 0, slc: 0, sync: 1, beats: 4, lines: 32, grain: 1, ofs: 0 });
  assert.ok(Math.abs(ev.rate - 2.1 / (32 * 60 / (131 * 8))) < 0.001, '32 lines at LPB 8 = 4 beats = 1.83 s: the 2.10 s sample plays 15% faster'); assert.ok(Math.abs(ev.rate - 1.1463) < 0.001);
  assert.equal(chopOf(131, 8, { pad: 0, tsp: 0, slc: 0, sync: 1, beats: 4, lines: 32, grain: 1, ofs: 0 }).ev.rate, chopOf(131, 8, { pad: 0, tsp: 0, slc: 0, sync: 1, beats: 4, grain: 1, ofs: 0 }).ev.rate, '32 lines at LPB 8 is BEATS 4, exactly');
});

test('B: lines that are not a whole number of beats are exact (12 lines at LPB 8 = 1.5 beats); the stretch modes get the fractional beats', () => {
  const r = chopOf(120, 8, { pad: 0, tsp: 0, slc: 0, sync: 2, beats: 4, lines: 12, grain: 1, ofs: 0 });
  assert.equal(r.ev.st.beats, 1.5); assert.equal(r.ev.st.mode, 2);
  assert.ok(Math.abs(r.ev.durSec - 0.75) < 1e-9, '1.5 beats at 120 BPM = 0.75 s'); assert.equal(r.ev.rate, 1);
  assert.equal(chopOf(120, 7, { pad: 0, tsp: 0, slc: 0, sync: 3, beats: 4, lines: 10, grain: 1, ofs: 0 }).ev.st.beats, 1.428571, 'rounded to 6 places for the stretch cache key');
});

test('B: the length follows the song’s LPB (the lines stay, the time changes); a phrase’s own LPB does not enter', () => {
  const I = { pad: 0, tsp: 0, slc: 0, sync: 1, beats: 4, lines: 32, grain: 1, ofs: 0 };
  const a = chopOf(120, 8, I).ev.durSec / chopOf(120, 8, I).ev.rate, b = chopOf(120, 4, I).ev.durSec / chopOf(120, 4, I).ev.rate;
  assert.ok(Math.abs(a - 2) < 1e-9 && Math.abs(b - 4) < 1e-9, `${a} s at LPB 8, ${b} s at LPB 4`);
  assert.match(src, /const instBeats = I => \(I\.lines \| 0\) > 0 \? I\.lines \/ \(S\.lpb \|\| 4\) : \(I\.beats \|\| 4\);/);
});

test('B: exact storage: `lines` round-trips through the song loader; an old song (no field) loads and plays as before', () => {
  assert.equal(loadInst({ sync: 1, beats: 4, lines: 37 }).lines, 37);
  assert.equal(loadInst(JSON.parse(JSON.stringify({ sync: 2, beats: 8, lines: 1024 }))).lines, 1024);
  assert.equal(loadInst({ lines: 5000 }).lines, 1024, 'clamped'); assert.ok(!('lines' in loadInst({ sync: 1, beats: 8 })), 'an old song: no field (a stale one is removed)');
  assert.ok(!('lines' in loadInst({ lines: 0 })) && !('lines' in loadInst({ lines: -3 })) && !('lines' in loadInst({ lines: 'x' })));
  assert.equal(loadInst({ sync: 1, beats: 8 }).beats, 8);
  assert.doesNotMatch(src.match(/const freshInst = [^\n]*\n[^\n]*\n/)[0], /lines/, 'a new instrument has no lines field: saved songs do not change');
  // BEATS-only instruments are exactly as in build 647 (the 216-case fixture above), and any write of BEATS drops lines
  const I = { beats: 4, lines: 12 }; E.setBeats(I, 8); assert.equal(JSON.stringify(I), '{"beats":8}');
  assert.match(src, /set:v => setBeats\(I, v\), min:1, max:64/, 'the BEATS row writes beats and drops lines');
});

test('B: SYNC TO LINES sets every instrument that plays the pad, and only those; OFF turns SYNC off; one change to the song (one undo step)', () => {
  const insts = [{ pad: 3, sync: 0, beats: 4 }, { pad: 5, sync: 2, beats: 2 }, { pad: 3, sync: 1, beats: 8, lines: 9 }, null, { pad: 3, sync: 0, beats: 1, mo: 1 }];
  assert.equal(E.slSet(insts, 3, 24, 2, 8), 3);
  assert.deepEqual(insts.filter(i => i && i.pad === 3).map(i => [i.lines, i.sync]), [[24, 2], [24, 2], [24, 2]]);
  assert.equal(JSON.stringify(insts[1]), '{"pad":5,"sync":2,"beats":2}', 'another pad’s instrument is untouched');
  assert.equal(E.slSet(insts, 3, 0, 0, 8), 3); assert.deepEqual(insts.filter(i => i && i.pad === 3).map(i => i.sync), [0, 0, 0]);
  E.slSet(insts, 3, 4000.4, 9, 8); assert.equal(insts[0].lines, 1024); assert.equal(insts[0].sync, 1, 'a mode out of range becomes REPITCH');
  // one undo step: the sheet applies with slSet then ONE saveS() (snap() coalesces what changes in the same turn into one step); undo restores the JSON before
  const fn = src.match(/async function syncToLines\(pad\)\{[\s\S]*?\n\}\n/)[0];
  assert.equal((fn.match(/saveS\(\)/g) || []).length, 2, 'one saveS() on OFF, one on Apply (never both)'); assert.equal((fn.match(/slSet\(/g) || []).length, 2);
  const before = JSON.stringify([{ pad: 3, sync: 0, beats: 4 }]), after = [JSON.parse(before)[0]]; E.slSet(after, 3, 24, 1, 8);
  assert.notEqual(JSON.stringify(after), before); assert.ok(!('lines' in JSON.parse(before)[0]), 'the undo snapshot has no field: restoring it removes the length');
  assert.match(fn, /toast\(slName\(r\.lines, lpb\)/, 'a toast says what was done');
});

test('B: the type-in takes lines, bars, or bars + lines, 1 to 1024, and says why when it can’t', () => {
  const p = (t, lpb = 8) => E.slParse(t, lpb);
  assert.equal(p('24').ok, true); assert.equal(p('24').lines, 24); assert.equal(p(' 24 lines ').lines, 24); assert.equal(p('1 line').lines, 1);
  assert.equal(p('2b').lines, 64); assert.equal(p('2b', 4).lines, 32); assert.equal(p('1b8').lines, 40); assert.equal(p('2 bars + 8 lines').lines, 72); assert.equal(p('1 bar').lines, 32);
  assert.equal(p('1024').ok, true); assert.equal(p('1025').ok, false); assert.equal(p('0').ok, false); assert.equal(p('32b').ok, true); assert.equal(p('33b').ok, false, '33 bars at LPB 8 is over the maximum');
  for (const bad of ['', 'abc', '-4', '1.5', '2.5b', '1e3', '12 beats', '2b-1', '1b8b', '0b0']) assert.equal(p(bad).ok, false, JSON.stringify(bad) + ' is refused');
  assert.match(p('x').err, /lines|bars/); assert.match(p('0').err, /least 1/); assert.match(p('2000').err, /1024/);
  assert.match(p('99999999999').err, /At most 1024 lines \(32 bars at 8 lines a beat\)/, 'a very long number says the limit, not "type lines"'); assert.match(p('1b99999999').err, /At most 1024/);
});

test('B: the wording: "N bars + M lines", whole bars show the lines in brackets, and the sentence says what will happen', () => {
  assert.equal(E.slName(64, 8), '2 bars (64 lines)'); assert.equal(E.slName(40, 8), '1 bar + 8 lines (40 lines)'); assert.equal(E.slName(24, 8), '24 lines');
  assert.equal(E.slName(32, 8), '1 bar (32 lines)'); assert.equal(E.slName(1, 8), '1 line'); assert.equal(E.slName(16, 4), '1 bar (16 lines)'); assert.equal(E.slName(33, 8), '1 bar + 1 line (33 lines)');
  assert.deepEqual(Array.from(E.slChoices(8)), [8, 16, 32, 64, 128, 256], '8 to 128 lines, and 1 2 4 8 bars (32 64 128 256), merged');
  assert.deepEqual(Array.from(E.slChoices(4)), [8, 16, 32, 64, 128], 'at LPB 4 the bars land on the same numbers');
  assert.equal(E.slPlan(64, 8, 131, 2.1, 1), '2 bars (64 lines) = 8 beats at 8 lines a beat = 3.66 s at 131 BPM; the sample is 2.10 s: it plays 42.7% slower, 9.6 semitones down. The length is in lines, so it follows LPB.');
  assert.match(E.slPlan(32, 8, 131, 2.1, 1), /^1 bar \(32 lines\) = 4 beats at 8 lines a beat = 1\.83 s at 131 BPM; the sample is 2\.10 s: it plays 14\.6% faster, 2\.4 semitones up/);
  assert.match(E.slPlan(12, 8, 120, 0.75, 1), /1\.5 beats.*already fits/); assert.match(E.slPlan(32, 8, 131, 2.1, 2), /stretched out to fit \(15%\), the pitch stays/);
});

test('B: the sheet and the buttons exist (Sync to lines… on the sample layer, › SYNC TO LINES on INST); the audio-thread core does not rewrite SYNC notes', () => {
  assert.match(src, /id="bSyncLines"[^>]*hidden>Sync to lines…<\/button>/); assert.match(src, /\$\('bSyncLines'\)\.hidden = \$\('bEditSmp'\)\.hidden;/);
  assert.match(src, /\{l:'› SYNC TO LINES', act:\(\) => syncToLines\(I\.pad\)/);
  const core = H.read('core/seq-core.js'); assert.match(core, /function applyChop\(\)\{\}/, 'the core’s applyChop is a stub: SYNC is a page-sequencer feature today');
  assert.match(core, /if \(c\)\{ applyChop\(ev, I, ev\.pad/, 'and it is only reached when ensure() gives a buffer, which the core’s ensure() never does');
  assert.match(core, /function ensure\(\)\{ return null; \}/);
});

/* ---------------- review fixes (F1, F2, F3, F6) ---------------- */
test('F1: loading over an existing Sample layer keeps its pitch settings and says so; a new layer gets the corrected tune', () => {
  const mk = (vt, tune, fine) => { const v = E.mkVoice('S', [], { tune: vt, len: 1.2, gate: 400, tail: 0 });
    v.layers[0] = E.freshLayer({ on: true, type: 'sample', level: 0.9, mode: 'AHD', atk: 0.1, hold: 1000, dec: 40, tune, fine: fine || 0, pAmt: 5, pDec: 70, sample: { data: S.sine(1000, 44100, 1), sr: 44100, name: 'old' } }); return v; };
  // an old tune-0 sample layer on a synth slot: replaced, it keeps tune 0 (and fine, pitch envelope), so existing notes sound the same
  const v = mk(24, 0, 12); E.setKit({ voices: [v] }, { bpm: 120, lpb: 8, insts: [] }); E.setCur(0);
  const before = E.renderVoice(v, 48000, null, 0), hz0 = S.freqOf(before, 48000, 0.2);
  const msg = E.assignSample(0, S.sine(1000, 44100, 1), 44100, 'new');
  const L = v.layers[0]; assert.equal(L.tune, 0); assert.equal(L.fine, 12); assert.equal(L.pAmt, 5); assert.equal(L.pDec, 70); assert.equal(L.sample.name, 'new');
  assert.ok(Math.abs(S.freqOf(E.renderVoice(v, 48000, null, 0), 48000, 0.2) - hz0) < 8, 'same rendered pitch as the old layer gave (' + hz0 + ' Hz)');
  assert.equal(msg, 'Kept this layer\u2019s tuning: it plays at its own pitch at C-2. Set TUNE to \u221224 for C-4.');
  // the toast numbers are real: a layer at +7 in a drum pad plays at its own pitch at F-3 (7 semitones below C-4... note 48 - 7 = 41)
  const v2 = mk(0, 7); E.setKit({ voices: [v2] }, { bpm: 120, lpb: 8, insts: [] }); assert.equal(E.assignSample(0, S.sine(1000, 44100, 1), 44100, 'n'), 'Kept this layer\u2019s tuning: it plays at its own pitch at F-3. Set TUNE to 0 for C-4.');
  const v3 = mk(24, -24); E.setKit({ voices: [v3] }, { bpm: 120, lpb: 8, insts: [] }); assert.equal(E.assignSample(0, S.sine(1000, 44100, 1), 44100, 'n'), null, 'C-4 already right: no toast');
  assert.equal(v3.layers[0].tune, -24);
  // a slot with no Sample layer: a new layer, corrected
  const v4 = E.mkVoice('S', [{ type: 'saw' }], { tune: 24, len: 1, gate: 400, tail: 0 }); E.setKit({ voices: [v4] }, { bpm: 120, lpb: 8, insts: [] });
  assert.equal(E.assignSample(0, S.sine(1000, 44100, 1), 44100, 'n'), null); assert.equal(v4.layers[0].tune, -24);
  assert.ok(Math.abs(S.freqOf(E.renderVoice(v4, 48000, null, 0), 48000, 0.2) - 1000) < 1, '1000 Hz at C-4');
});

test('F3: SYNC TO LINES also writes the nearest whole BEATS for a build without `lines`', () => {
  const insts = [{ pad: 1, sync: 0, beats: 4 }];
  E.slSet(insts, 1, 64, 1, 8); assert.equal(insts[0].beats, 8); assert.equal(insts[0].lines, 64);
  E.slSet(insts, 1, 12, 1, 8); assert.equal(insts[0].beats, 2, '1.5 beats: the nearest whole'); E.slSet(insts, 1, 1, 1, 16); assert.equal(insts[0].beats, 1, 'at least 1');
  E.slSet(insts, 1, 1024, 1, 4); assert.equal(insts[0].beats, 64, 'at most 64');
  const I = { pad: 0, tsp: 0, slc: 0, sync: 1, beats: 4, grain: 1, ofs: 0 }; E.slSet([I], 0, 64, 1, 8);
  const old = Object.assign({}, I); delete old.lines;   // a 647-style reader sees only beats
  assert.equal(E.instBeats(old) * 60 / 120, E.instBeats(Object.assign({}, I)) * 60 / 120, 'the same target for a whole-beat length');
  const before = JSON.stringify(insts[0]); E.setBeats(insts[0], 3); assert.ok(!('lines' in insts[0])); assert.notEqual(JSON.stringify(insts[0]), before);
});

test('F2: a recorder take kept into a slot on another engine restores the Layers tune first; F6: the audio-thread line is calm', () => {
  assert.match(src, /if \(eng\)\{ v\.engine = 'layers'; if \(typeof v\.tuneL === 'number'\) v\.tune = v\.tuneL; \}/);
  assert.match(src, /On the Audio thread sequencer the sample plays unsynced for now; that is being worked on\./); assert.doesNotMatch(src, /known open bug: sliced/);
});
