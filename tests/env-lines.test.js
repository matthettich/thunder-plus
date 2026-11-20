// ENVELOPE TIMES IN LINES (build 658): an envelope may keep its times as tracker lines (layer amp `aU`, layer filter `fU`, pad amp
// `gU`); renderVoice turns them into ms at the song's BPM and LPB and lengthens the pad to fit. The engine block (the code the
// render Worker runs) is cut out of index.html and run in a vm with the Worker's own `S`.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const A = src.indexOf('/* ===ENGINE START=== */'), B = src.indexOf('/* ===ENGINE END=== */');
assert.ok(A > 0 && B > A, 'engine block moved');
const ctx = vm.createContext({ console: { log() {}, warn() {}, error() {} }, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object,
  JSON, Map, Set, WeakMap, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
ctx.globalThis = ctx; ctx.self = ctx;
vm.runInContext('var S = {bpm:120, lpb:4};\n' + src.slice(A, B) + '\n;globalThis.__E = {renderVoice, mkVoice, freshLayer, linesMs, msLines, envLinesFit, voiceLines, relOf, songLpb};', ctx);
const E = ctx.__E, SR = 48000;
const tempo = (bpm, lpb) => vm.runInContext(`S.bpm = ${bpm}; S.lpb = ${lpb};`, ctx);
const noise = (o, vo) => E.mkVoice('t', [Object.assign({type:'pink', mode:'AHD', atk:0.5, hold:0, dec:300, level:0.8, seed:7}, o)], Object.assign({len:4}, vo));
const peakAt = (d, from, to) => { let m = 0; for (let i = Math.max(0, from | 0); i < Math.min(d.length, to | 0); i++) m = Math.max(m, Math.abs(d[i])); return m; };
const firstAbove = (d, x) => { let run = 0; for (let i = 0; i < d.length; i++) { if (Math.abs(d[i]) >= x) return i; } return -1; };

test('lines -> seconds at several BPM and LPB', () => {
  assert.equal(E.linesMs(32, 120, 4), 4000);          // 32 lines, 4 a beat = 8 beats = 4 s at 120
  assert.equal(E.linesMs(64, 60, 4), 16000);
  assert.ok(Math.abs(E.linesMs(256, 131, 8) - 14656.489) < 0.01);   // 256 lines at 131 BPM, 8 a beat: about 14.7 s
  assert.equal(E.linesMs(256, 60, 4), 64000);         // 256 lines at 60 BPM, 4 a beat: 64 s (the one-stage cap)
  assert.equal(E.linesMs(1e6, 40, 1), 64000);         // never more than 64 s a stage
  assert.equal(E.linesMs(0, 120, 4), 0);
  assert.equal(E.msLines(4000, 120, 4), 32);          // and back, to the nearest whole line
  assert.equal(E.msLines(300, 131, 8), 5);
});

test('a noise layer with ATTACK 32 lines at 120 BPM / 4 LPB reaches full level at 4.000 s', () => {
  tempo(120, 4);
  const v = noise({aU:'l', atkL:32, holdL:0, decL:8, atk:0.5, dec:300});
  const d = E.renderVoice(v, SR, null, 0);
  assert.ok(d.length > 4.2 * SR, 'the pad lasts past the attack (Max len 4 s was too short): ' + d.length / SR);
  // the attack ramps linearly 0 -> 1 over 4 s: the block level at 1 s is about a quarter of that at 4 s
  const p1 = peakAt(d, 0.9 * SR, 1.0 * SR), p4 = peakAt(d, 3.9 * SR, 4.0 * SR);
  assert.ok(p4 > 3.2 * p1 && p4 < 5 * p1, `ramp 1 s ${p1} vs 4 s ${p4}`);
  // after the attack it decays: 4 s + 8 lines (1 s) later it is far quieter
  assert.ok(peakAt(d, 4.9 * SR, 5.0 * SR) < p4 * 0.05, 'decay follows');
  assert.ok(Math.abs(d.length / SR - 5.01) < 0.3, 'length = attack + decay (+ 4 ms): ' + d.length / SR);
});

test('the swell peaks at the right sample at 131 BPM / 8 LPB', () => {
  tempo(131, 8);
  const secs = 32 / 8 * 60 / 131;   // 1.8321 s
  const v = noise({type:'white', aU:'l', atkL:32, holdL:0, decL:1, atk:0.5, dec:300});
  const d = E.renderVoice(v, SR, null, 0);
  const W = Math.round(0.02 * SR); let best = 0, at = 0;   // the loudest 20 ms window (a noise peak by itself is chance)
  for (let i = 0; i + W <= d.length; i += W / 4) { let e = 0; for (let j = 0; j < W; j++) e += d[i + j] * d[i + j]; if (e > best) { best = e; at = i + W; } }
  assert.ok(Math.abs(at / SR - secs) < 0.04, `loudest window ends at ${at / SR} s, wanted ${secs}`);
});

test('a unit switch keeps the time; a BPM change re-times the swell', () => {
  tempo(120, 4);
  const ms = noise({atk:2000, hold:0, dec:300}), ln = noise({aU:'l', atkL:16, holdL:0, decL:2, atk:2000, dec:300});   // 16 lines = 2000 ms at 120 / 4
  const a = E.renderVoice(ms, SR, null, 0), b = E.renderVoice(ln, SR, null, 0);
  assert.ok(Math.abs(a.length - b.length) < 0.2 * SR, `${a.length} vs ${b.length}`);   // (decay 2 lines = 250 ms vs 300 ms)
  tempo(60, 4);   // half the tempo: the same lines take twice as long
  const c = E.renderVoice(ln, SR, null, 0);
  assert.ok(Math.abs(c.length / SR - (4 + 0.5 + 0.005)) < 0.1, 'attack 4 s + decay 0.5 s: ' + c.length / SR);
  tempo(120, 8);  // twice the lines a beat: the same lines take half as long
  const d = E.renderVoice(ln, SR, null, 0);
  assert.ok(Math.abs(d.length / SR - (1 + 0.125 + 0.005)) < 0.1, d.length / SR);
});

test('old voices (no unit field) render byte for byte as before', () => {
  tempo(120, 4);
  const v = noise({atk:3, hold:20, dec:200}), w = noise({atk:3, hold:20, dec:200});
  w.layers[0].atkL = 99; w.layers[0].decL = 5;   // lines fields without the unit do nothing
  assert.equal(E.voiceLines(v), false); assert.equal(E.voiceLines(w), false);
  assert.equal(E.envLinesFit(w, 0), null);
  const a = E.renderVoice(v, SR, null, 0), b = E.renderVoice(w, SR, null, 0);
  assert.equal(H.hashFloats(a), H.hashFloats(b));
  tempo(77, 6);   // the tempo does not touch a pad in ms
  assert.equal(H.hashFloats(E.renderVoice(v, SR, null, 0)), H.hashFloats(a));
});

test('the mod fast path (no route has an amount) is the same sound', () => {
  tempo(120, 4);
  const v = noise({atk:3, hold:20, dec:200, lpCut:3000, lpEnv:0.4}); v.lfo1Depth = 1; v.r1dst = 2;   // a route with amount 0
  const w = noise({atk:3, hold:20, dec:200, lpCut:3000, lpEnv:0.4});
  assert.equal(H.hashFloats(E.renderVoice(v, SR, null, 0)), H.hashFloats(E.renderVoice(w, SR, null, 0)));
  v.r1amt = 0.5; assert.notEqual(H.hashFloats(E.renderVoice(v, SR, null, 0)), H.hashFloats(E.renderVoice(w, SR, null, 0)), 'a real route still works');
});

test('a synth slot (ADSR, held): the pad lengthens to the swell plus a bar; Max len 6 s no longer cuts it', () => {
  tempo(120, 4);
  const v = E.mkVoice('s', [{type:'pink', mode:'ADSR', aU:'l', atkL:64, holdL:0, decL:4, relL:4, atk:1, dec:300, sus:1, rel:150, level:0.8}], {len:6, gate:400});
  const fit = E.envLinesFit(v, 6);
  assert.ok(fit.hold > 8 + 0.5 + 2, 'hold ' + fit.hold);   // 64 lines at 120 / 4 = 8 s, decay 0.5 s, a bar (2 s)
  const d = E.renderVoice(v, SR, null, 6);
  assert.ok(Math.abs(d.length / SR - fit.hold) < 0.05, `${d.length / SR} vs ${fit.hold}`);
  assert.ok(peakAt(d, 7.9 * SR, 8 * SR) > 3.5 * peakAt(d, 1.9 * SR, 2 * SR), 'still rising at 8 s');
  // the note-off fade follows the release in lines
  const rel = E.relOf(v); assert.ok(Math.abs(rel - 0.5) < 1e-9, 'release 4 lines = 0.5 s: ' + rel);
  // an ordinary synth slot keeps its 6 s
  const o = E.mkVoice('o', [{type:'pink', mode:'ADSR', atk:1, dec:300, sus:1, rel:150, level:0.8}], {len:6, gate:400});
  assert.equal(E.renderVoice(o, SR, null, 6).length, 6 * SR);
});

test('the longest case (8 bars, LPB 8, 90 BPM) fits the stated memory and time', () => {
  tempo(90, 8);
  const v = E.mkVoice('s', [{type:'white', mode:'ADSR', aU:'l', atkL:256, holdL:0, decL:0, relL:0, atk:1, dec:1, sus:1, rel:1, level:0.8}], {len:6, gate:400});
  const t0 = Date.now(), d = E.renderVoice(v, SR, null, 6), ms = Date.now() - t0;
  const secs = 256 / 8 * 60 / 90;   // 21.33 s
  assert.ok(Math.abs(d.length / SR - (secs + 240 / 90 + 0.005)) < 0.05, 'the swell and a bar of sustain: ' + d.length / SR);
  assert.ok(d.byteLength < 6e6, 'mono, about 4 MB at 48 kHz: ' + d.byteLength);
  assert.ok(ms < 20000, 'rendered in ' + ms + ' ms');
});

test('the pad amp (gU) and the filter envelope (fU) take lines too', () => {
  tempo(120, 4);
  const v = noise({atk:0.5, hold:12000, dec:300}, {len:20, gAmpOn:true, gMode:'AHD', gU:'l', gAtkL:24, gHoldL:0, gDecL:8, gAtk:1, gDec:1});
  const d = E.renderVoice(v, SR, null, 0);   // the pad amp rises over 3 s (24 lines at 120 / 4)
  const p1 = peakAt(d, 0.9 * SR, 1.0 * SR), p3 = peakAt(d, 2.9 * SR, 3.0 * SR);
  assert.ok(p3 > 2.2 * p1 && p3 < 4.2 * p1, `pad amp ramp ${p1} -> ${p3}`);
  const f = noise({fU:'l', fAtkL:32, fDecL:8, lpCut:200, lpEnv:0.5, atk:0.5, hold:2000, dec:300});
  assert.equal(E.voiceLines(f), true); assert.ok(E.renderVoice(f, SR, null, 0).length > 0);
});

test('source guards: the page re-renders on BPM / LPB, the Worker gets lpb, saving in ms writes nothing new', () => {
  assert.match(src, /if \(voiceLines\(v\)\) return songBpm\(\) \+ songLpb\(\) \/ 100;/, 'padBpm');
  assert.match(src, /bpm:songBpm\(\), lpb:songLpb\(\)/, 'renderAsync posts lpb');
  assert.match(src, /S = \{bpm:m\.bpm \|\| 120, lpb:m\.lpb \|\| 4\}/, 'the Worker keeps lpb');
  const fl = src.slice(src.indexOf('function freshLayer(o){'), src.indexOf('/* Voice modulation matrix'));
  assert.ok(!/aU|fU|atkL/.test(fl), 'freshLayer adds no new field');
});
