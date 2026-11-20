// OLDSCHOOL (build 660, docs/STRETCH-DESIGN.md section 7): the cyclic ramp stretch rendered into the stretch cache. The guarantees of the design's
// prototype (tools/experiments/stretch/oldschool.mjs on branch beta-stretch-design) are tests here; the classic modes are checked in
// tests/stretch-picker.test.js against build 658.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers'), S = require('./sampler-harness'), R = require('./stretch-record');
const src = H.read('index.html'), J = x => JSON.parse(JSON.stringify(x));
const E = S.loadSampler(src, 'stretchData, stKey, oldschoolData, osStarts, osGrainSecs, stLiveBpm, stq, STQ_QUIET, SY_PICK, syPut, OS_RAMPS, OS_DIV_NAMES,');
const sr = 44100, sine = (hz, secs) => S.sine(hz, sr, secs), L2 = sine(220, 2), srcStep = 0.5 * 2 * Math.PI * 220 / sr;
const st = (o) => Object.assign({ mode: 3, sx: 1, bpm: 140, beats: 4, grain: 1, gs: 0.012, rp: 0, x: 0, pt: 0 }, o);
const T = s0 => Math.max(64, Math.round(s0.beats * 60 / s0.bpm * sr));
const render = (d, o) => E.stretchData(d, sr, d.length / sr, st(o));
const steps = (out, k) => { const j = []; let mx = 0; for (let n = 1; n < out.length; n++) { const dd = Math.abs(out[n] - out[n - 1]); if (dd > mx) mx = dd; if (dd > k * srcStep) j.push(n); } return { j, mx }; };

test('the output is exactly the length, every ramp, smooth and pitch (stretchData and oldschoolData)', () => {
  for (const rp of [0, 1, 2, 3]) for (const x of [0, 5, 50]) for (const pt of [0, 7, -12]) for (const [bpm, beats, gs] of [[140, 4, 0.012], [90, 1, 0.0005], [174, 3, 0.25], [131, 2, 0.03]]) {
    const o = st({ rp, x, pt, bpm, beats, gs }), out = E.stretchData(L2, sr, 2, o);
    assert.equal(out.length, T(o), JSON.stringify(o));
    for (const v of out) if (!Number.isFinite(v)) assert.fail('finite');
  }
  assert.equal(E.oldschoolData(L2, sr, 2, st({ beats: 1, bpm: 300, gs: 0.5 })).length, Math.round(60 / 300 * sr), 'a grain longer than the length: one grain, same length');
});

test('SMOOTH 0 is the authentic hard cut: every jump is on a grain boundary, and they come every g samples (all four ramps)', () => {
  const gs = 0.012, g = Math.round(gs * sr);
  for (const rp of [0, 1, 2, 3]) {
    const out = render(L2, { rp, x: 0, gs }), { j, mx } = steps(out, 4);
    assert.ok(j.length > 100, 'a hard cut buzzes: ' + j.length + ' jumps');
    assert.ok(j.every(n => n % g === 0), 'ramp ' + rp + ': a jump off the grid at ' + j.find(n => n % g !== 0));
    assert.ok(mx > 20 * srcStep, 'and they are real jumps (' + (mx / srcStep).toFixed(0) + 'x the source step)');
  }
  const out = render(L2, { x: 0, gs }), { j } = steps(out, 4), gaps = new Set(); for (let i = 1; i < j.length; i++) gaps.add(j[i] - j[i - 1]);
  assert.ok([...gaps].every(d => d % g === 0), 'gaps are whole grains');
});

test('SMOOTH > 0 (straight-read crossfade): no step beyond the source’s own, at the edges too', () => {
  for (const [x, bound] of [[5, 1.8], [25, 1.3], [50, 1.3]]) for (const rp of [0, 1, 2]) {
    const out = render(L2, { x, rp, gs: 0.012 }), { j, mx } = steps(out, 4);
    assert.equal(j.length, 0, `x ${x}% ramp ${rp}: no step over 4x the source's`);
    assert.ok(mx <= bound * srcStep, `x ${x}% ramp ${rp}: max step ${(mx / srcStep).toFixed(2)}x the source (bound ${bound})`);
  }
  // the first sample starts at full level (no fade-in from nothing) and the last grain is not faded out
  const o = render(L2, { x: 50, gs: 0.012 }); assert.ok(Math.abs(o[10]) > 0 && Math.abs(o[0]) < 1e-3);
});

test('ramps: FWD walks the whole sample start to end, REV end to start, PING there and back, EASE slow at both ends', () => {
  const Ls = 88200, Tn = 176400, g = 529, r = 1, span = g, top = Ls - span - 1;
  const f = E.osStarts(Ls, Tn, g, r, 0), v = E.osStarts(Ls, Tn, g, r, 1), p = E.osStarts(Ls, Tn, g, r, 2), e = E.osStarts(Ls, Tn, g, r, 3), tol = Math.ceil(Ls * g / Tn) + 1;
  assert.equal(f.length, Math.ceil(Tn / g));
  assert.equal(f[0], 0); assert.ok(top - f[f.length - 1] <= 2 * tol, 'FWD reaches the end of the sample');
  assert.equal(v[0], top); assert.ok(v[v.length - 1] <= 2 * tol, 'REV reaches the start');
  for (let k = 1; k < f.length; k++) { assert.ok(f[k] >= f[k - 1]); assert.ok(v[k] <= v[k - 1]); }
  const peak = Math.max(...p), at = p.indexOf(peak); assert.ok(top - peak <= 2 * tol, 'PING reaches the end of the sample'); assert.ok(Math.abs(at - p.length / 2) < 3, 'in the middle');
  assert.equal(p[0], 0); assert.ok(p[p.length - 1] <= 2 * tol, 'and comes back to the start');
  for (let k = 1; k <= at; k++) assert.ok(p[k] >= p[k - 1]); for (let k = at + 1; k < p.length; k++) assert.ok(p[k] <= p[k - 1]);
  assert.ok(e[10] < f[10] && e[e.length - 10] > f[f.length - 10], 'EASE: behind FWD at the start, ahead at the end'); assert.ok(e[e.length >> 1] > 0);
  // and in the audio: a ramp-shaped source read at x = 0 shows the same walk (REV decreasing, PING up then down)
  const rampSrc = Float32Array.from({ length: 44100 }, (_, i) => i / 44100), o = render(rampSrc, { rp: 1, x: 0, gs: 0.012, bpm: 120, beats: 2 }), G = Math.round(0.012 * sr);
  const firsts = []; for (let n = 0; n < o.length; n += G) firsts.push(o[n]);
  for (let k = 1; k < firsts.length; k++) assert.ok(firsts[k] <= firsts[k - 1] + 1e-9, 'REV in the audio');
  assert.ok(firsts[0] > 0.9 && firsts[firsts.length - 1] < 0.05, 'REV in the audio: from the end to the start');
  const pi = render(rampSrc, { rp: 2, x: 0, gs: 0.012, bpm: 120, beats: 2 }), pf = []; for (let n = 0; n < pi.length; n += G) pf.push(pi[n]);
  assert.ok(Math.max(...pf) > 0.9 && pf[0] < 0.05 && pf[pf.length - 1] < 0.05, 'PING in the audio: up to the end and back');
});

test('PITCH: grains at 2^(st/12) of the own pitch (12 semitones doubles a sine), 0 = the source untouched', () => {
  const z = (o, a, b) => { let c = 0; for (let n = a + 1; n < b; n++) if (o[n - 1] <= 0 && o[n] > 0) c++; return c; };
  const a = Math.round(0.05 * sr), b = Math.round(0.4 * sr), big = { gs: 0.5, bpm: 60, beats: 2 };
  const o0 = render(L2, Object.assign({ pt: 0 }, big)), o12 = render(L2, Object.assign({ pt: 12 }, big)), om = render(L2, Object.assign({ pt: -12 }, big));
  assert.ok(Math.abs(z(o0, a, b) / 0.35 - 220) < 6); assert.ok(Math.abs(z(o12, a, b) / 0.35 - 440) < 8); assert.ok(Math.abs(z(om, a, b) / 0.35 - 110) < 6);
  // pitch 0 reads the source samples as they are (integer path): a long grain at x 0 is a copy
  const copy = render(L2, Object.assign({ pt: 0, rp: 0 }, big)); for (let n = 0; n < 1000; n++) assert.equal(copy[n], L2[n]);
});

test('LOOP length: ms, or a division of a line that follows tempo and LPB', () => {
  assert.equal(E.osGrainSecs({ ou: 0, og: 12 }, 120, 4), 0.012); assert.equal(E.osGrainSecs({ og: 12 }, 120, 4), 0.012, 'ou absent = ms');
  assert.equal(E.osGrainSecs({ ou: 1 }, 120, 4), 0.125, '1 line at 120 BPM, 4 lines a beat');
  assert.equal(E.osGrainSecs({ ou: 1 }, 120, 8), 0.0625); assert.equal(E.osGrainSecs({ ou: 3 }, 120, 4), 0.125 / 4); assert.equal(E.osGrainSecs({ ou: 6 }, 120, 4), 0.125 / 32);
  assert.equal(E.osGrainSecs({ ou: 1 }, 60, 4), 0.25, 'slower tempo, longer line');
  assert.equal(E.osGrainSecs({ ou: 0, og: 0.1 }, 120, 4), 0.0005, 'clamped to 0.5 ms'); assert.equal(E.osGrainSecs({ ou: 0, og: 999 }, 120, 4), 0.25, 'and 250 ms');
  assert.deepEqual(J(E.OS_DIV_NAMES), ['MS', '1 LINE', '1/2 LINE', '1/4 LINE', '1/8 LINE', '1/16 LINE', '1/32 LINE']); assert.deepEqual(J(E.OS_RAMPS), ['FWD', 'REV', 'PING', 'EASE']);
});

// the events: an instrument in OLDSCHOOL, through the page's own applyChop
function chop(I0, bpm, lpb, live) {
  const d = L2, kit = { voices: [E.mkVoice('S', [], { tune: 0, len: 1.2, gate: 400, tail: 0 })] }, St = { bpm, lpb, insts: [] };
  E.setKit(kit, St); E.ctx.__cache[0] = { data: d, sr };
  const I = Object.assign({ pad: 0, tsp: 0, slc: 0, smode: 0, beats: 4, grain: 1, ofs: 0 }, I0); if (I.sx === 1) E.syPut(I, 4), Object.assign(I, I0);
  const ev = { type: 'note', t: 1, rate: 1, ofs: 0, cut: null, slc: null }; E.applyChop(ev, I, 0, 48, 0, bpm, d, sr, live); return { ev, key: E.stKey(ev.st) };
}
test('classic modes carry no OLDSCHOOL fields; OLDSCHOOL puts every setting into the cache key', () => {
  for (const sync of [2, 3]) { const { ev, key } = chop({ sync }, 120, 8); assert.ok(!('sx' in ev.st) && !('gs' in ev.st)); assert.equal(key, ':' + sync + ':120:4:1'); }
  const base = { sync: 3, sx: 1 }, k0 = chop(base, 120, 8).key, seen = new Set([k0]);
  const vary = [{ og: 20 }, { ou: 2 }, { orp: 1 }, { orp: 2 }, { orp: 3 }, { ox: 10 }, { opt: 3 }, { beats: 5 }, { grain: 3 }];
  for (const v of vary) { const k = chop(Object.assign({}, base, v), 120, 8).key; assert.ok(!seen.has(k), 'a change of ' + JSON.stringify(v) + ' changes the key: ' + k); seen.add(k); }
  assert.notEqual(chop(base, 121, 8).key, k0, 'tempo is in the key');
  assert.notEqual(chop({ sync: 3, sx: 1, ou: 2 }, 120, 4).key, chop({ sync: 3, sx: 1, ou: 2 }, 120, 8).key, 'a loop in divisions of a line: LPB is in the key through the loop length');
  assert.equal(chop(base, 120, 4).key.replace(/:4:/, ':X:'), chop(base, 120, 8).key.replace(/:4:/, ':X:'), 'a loop in ms does not move with LPB');
  assert.notEqual(chop(base, 120, 8).key, ':3:120:4:1');
  assert.match(k0, /^:3:120:4:1:sx1:0\.012:0:0:0$/);
  // and each differing key really is different audio
  const A = (o) => S.hash(render(L2, o)); const h0 = A({}), hs = new Set([h0]);
  for (const v of [{ gs: 0.02 }, { rp: 1 }, { rp: 2 }, { rp: 3 }, { x: 10 }, { pt: 3 }, { beats: 5 }]) { const h = A(v); assert.ok(!hs.has(h), 'audio differs for ' + JSON.stringify(v)); hs.add(h); }
});

test('a tempo or LPB change re-renders (new key); a TEMPO drag does not render at every step', () => {
  // exact tempo off the live path (previews, offline render): every tempo is its own key
  assert.notEqual(chop({ sync: 3, sx: 1 }, 120, 8).key, chop({ sync: 3, sx: 1 }, 121, 8).key);
  assert.notEqual(chop({ sync: 3, sx: 0 }, 120, 8).key, chop({ sync: 3, sx: 0 }, 140, 8).key);
  // a drag: the Settings TEMPO slider commits every 50 ms; the song plays a note every 100 ms for 2 s while it drags 120 to 140, then holds
  E.stq.bpm = 0; E.stq.seen = 0; E.stq.at = 0; E.stq.last = 0;
  const keys = []; let t = 10000, bpm = 120;
  const note = (b) => { keys.push(E.stLiveBpm(b, b, true, t)); };
  note(bpm); t += 100;
  for (let i = 0; i < 40; i++) { if (i % 1 === 0) bpm = Math.round((120 + i * 0.5) * 100) / 100; if (i % 2 === 1) note(bpm); t += 50; }   // 40 steps of 50 ms; a note every 100 ms
  const during = new Set(keys); assert.equal(during.size, 1, 'during the drag every note plays the stretch it had (one key: ' + [...during] + ')');
  assert.equal([...during][0], 120);
  for (let i = 0; i < 10; i++) { note(bpm); t += 100; }   // held at 139.5 BPM
  const after = [...new Set(keys.slice(21))]; assert.deepEqual(after, [120, 139.5], 'after it settles (300 ms) one new tempo, once');
  assert.ok(keys.indexOf(139.5) * 100 >= 300, 'not before the quiet time');
  // a TPO command’s own tempo is exact and moves nothing in the lagged one; a pause snaps; while playing a wobble under 0.5% is ignored
  assert.equal(E.stLiveBpm(150, 139.5, true, t), 150, 'TPO: exact');
  t += 5000; assert.equal(E.stLiveBpm(100, 100, false, t), 100, 'after a pause: snaps at once');
  for (let i = 0; i < 20; i++) { t += 100; E.stLiveBpm(100.3, 100.3, true, t); } assert.equal(E.stLiveBpm(100.3, 100.3, true, t + 100), 100, 'playing: a change under 0.5% never re-renders');
  for (let i = 0; i < 20; i++) { t += 100; E.stLiveBpm(100.3, 100.3, false, t); } assert.equal(E.stLiveBpm(100.3, 100.3, false, t + 100), 100.3, 'stopped: it follows');
  // through applyChop: live notes use it, the page keeps the exact tempo for REPITCH and for non-live callers
  E.stq.bpm = 0; E.stLiveBpm(120, 120, true, 50000);
  const live = chop({ sync: 2 }, 124, 8, true), exact = chop({ sync: 2 }, 124, 8, false);
  assert.equal(live.ev.st.bpm, 120, 'live: the lagged tempo'); assert.equal(exact.ev.st.bpm, 124, 'preview and offline render: exact');
  assert.equal(chop({ sync: 1 }, 124, 8, true).ev.st, undefined, 'REPITCH has no stretch and keeps the exact rate');
});

test('slices of OLDSCHOOL: equal parts of what you hear for REV / PING / EASE, even with SLICE BY HITS; FWD keeps the hit points', () => {
  const pad = (o) => chop(Object.assign({ sync: 3, sx: 1, slc: 4, smode: 1 }, o), 120, 8).ev;
  const fwd = pad({}), rev = pad({ orp: 1 }); assert.ok(rev.ofsSec >= 0 && rev.durSec > 0);
  // note C-4 = slice 0: starts at 0 and lasts a quarter of the length when the slices are equal
  assert.equal(rev.ofsSec, 0); assert.ok(Math.abs(rev.durSec - 2 / 4) < 1e-9, 'a quarter of the 2 s length: ' + rev.durSec);
  assert.ok(fwd.durSec > 0);
});

test('speed and the honest note', () => {
  const big = new Float32Array(10 * sr).map((_, i) => Math.sin(i * 0.01)), t0 = performance.now();
  const o = E.oldschoolData(big, sr, 10, { bpm: 120, beats: 16, gs: 0.012, rp: 0, x: 25, pt: 5 }); const ms = performance.now() - t0;
  assert.equal(o.length, 8 * sr); assert.ok(ms < 1500, '10 s in, 8 s out, pitched and smoothed: ' + ms.toFixed(0) + ' ms');
  assert.match(src, /On the Audio thread sequencer the sample plays unsynced for now; that is being worked on\. Settings › Audio › Sequencer › Page plays it synced\./g);
  assert.ok((src.match(/On the Audio thread sequencer the sample plays unsynced for now/g) || []).length >= 2, 'in the sheet and in the SYNC row’s note');
  assert.match(src, /\{l:'LOOP BY', opts:true/); assert.match(src, /\{l:'RAMP', opts:true/); assert.match(src, /\{l:'SMOOTH', get:/); assert.match(src, /\{l:'PITCH', get:\(\) => I\.opt/);
  assert.match(src, /\.\.\.\(syPick\(I\) === 4 \? \[/, 'the rows show only for OLDSCHOOL, and GRAIN only for the others');
});

test('a golden for the render (so the native port has one to match)', () => {
  const GOLD = {};   // filled by the first run, see below
  const cases = { fwd12: { gs: 0.012 }, rev30x25: { gs: 0.03, rp: 1, x: 25 }, ping16: { gs: 1 / 16 * 60 / 140, rp: 2 }, ease5st: { gs: 0.012, rp: 3, pt: 5, x: 10 } };
  const brk = R.source(sr), out = {}; for (const k in cases) out[k] = S.hash(E.stretchData(brk, sr, brk.length / sr, st(cases[k])));
  Object.assign(GOLD, JSON.parse(H.read('tests/fixtures/oldschool-golden-660.json'))); assert.deepEqual(out, GOLD);
});
