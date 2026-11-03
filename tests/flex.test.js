// FLEX CHECK (3 Nov): every FLX pattern does what its name says; FLX values reach the effect unclamped; STARTS ON FLX /
// ON GAT keep the slot quiet until its command; the FX bar's pattern state. The code under test is cut out of index.html.
// The end-to-end check in a real browser, both sequencers, is tools/flex-check.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what) { const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
const clk = cut(/function clkSpb\(o, bp, n\)\{[\s\S]*?\n\}\n/, 'clkSpb');
const FC = new Function(clk + cut(/class FlexCore \{[\s\S]*?\n\}\n/, 'class FlexCore') + cut(/class StepGateCore \{[\s\S]*?\n\}\n/, 'class StepGateCore') + 'return {FlexCore, StepGateCore};')();
const { FlexCore, StepGateCore } = FC;
// a name's note value in beats: 1/16 → 0.25, 1/8T → 1/3, 3/16 → 0.75, BAR → 4
const beatsOf = s => { if (s === 'BAR') return 4; const m = /^(\d+)\/(\d+)(T?)$/.exec(s); assert.ok(m, 'not a note value: ' + s); return +m[1] / +m[2] * 4 * (m[3] ? 2 / 3 : 1); };
const near = (a, b, what) => assert.ok(Math.abs(a - b) < 1e-9, `${what}: ${a} vs ${b}`);

test('FLX values reach the effect unclamped (the tfx params go to 255; were 64: FLX 41–FF all played 40 on the Page sequencer)', () => {
  const d = cut(/static get parameterDescriptors\(\)\{ const k = \(name, d\) => \(\{name, defaultValue:d, minValue:-1, maxValue:(\d+)/, 'the thunder-tfx parameterDescriptors');
  assert.ok(+/maxValue:(\d+)/.exec(d)[1] >= 255);
});

test('FLX names say note values and match the patterns (every family, 11–FF)', () => {
  const T = FlexCore.SPEC(), N = FlexCore.NAMES; let n = 0;
  for (let k = 17; k < 256; k++) {
    const o = T[k], nm = N[k]; assert.ok(o && nm && nm === o.n, 'pattern ' + k.toString(16)); let m;
    switch (o.f) {
      case 'roll': m = /^ROLL (\S+) FROM BEAT ([\d.]+)$/.exec(nm); near(beatsOf(m[1]), o.L, nm); near(+m[2] - 1, o.s, nm); break;
      case 'stut': m = /^STUTTER (\S+) EVERY (\S+)$/.exec(nm); near(beatsOf(m[1]), o.L, nm); near(beatsOf(m[2]), o.C, nm); break;
      case 'build': m = /^(?:BUILD|SLOW DOWN) ([\d/T ]+?)(?: FROM BEAT ([\d.]+))?$/.exec(nm); m[1].split(' ').forEach((x, i) => near(beatsOf(x), o.sg[i][1], nm)); near(m[2] ? +m[2] - 1 : 0, o.sg[0][0], nm); break;
      case 'rev': m = /^REVERSE (?:EVERY OTHER |HALF SPEED |RANDOM )?(\S+?)S?(?: IN BEAT 4)?$/.exec(nm); near(beatsOf(m[1] === 'BAR' ? 'BAR' : m[1]), o.C, nm); break;
      case 'down': case 'up': m = /^(?:DOWN|UP) ([-+]\d+) EVERY (\S+)/.exec(nm); assert.equal(+m[1], Math.round(12 * Math.log2(o.s)), nm); near(beatsOf(m[2]), o.C, nm); break;
      case 'scr': m = /^(?:SCRATCH|CHIRP|TRANSFORM) (\S+) ×(\d+)$/.exec(nm); near(beatsOf(m[1]), o.C, nm); assert.equal(+m[2], o.k, nm); break;
      case 'echo': m = /^FADE REPEAT (\S+) EVERY (\S+)$/.exec(nm); near(beatsOf(m[1]), o.L, nm); near(beatsOf(m[2]), o.C, nm); break;
      case 'jug': m = /^JUGGLE (?:ALL )?(\S+)/.exec(nm); near(beatsOf(m[1]), o.G, nm); break;
      case 'chaos': m = /^CHAOS (\S+)/.exec(nm); near(beatsOf(m[1]), o.G, nm); break;
      case 'swing': m = /^SWING (\S+)/.exec(nm); near(beatsOf(m[1]), o.C, nm); break;
      case 'pump': m = /^PUMP (\S+)$/.exec(nm); near(beatsOf(m[1]), o.C, nm); break;
      case 'gate': {   // the named length is the gate's whole period (as GATE 1/16, pattern 0B): on half, off half
        m = /^GATE (\S+)$/.exec(nm); const P = beatsOf(m[1]), f = new FlexCore(48000);
        for (let i = 0; i < 64; i++) { const p = (i + 0.5) / 64 * 4; f.ext(k, p, 0); assert.equal(f.pg, (p % P) < P / 2 ? 1 : 0, nm + ' at ' + p); } break; }
      case 'filt': if ((m = /WOBBLE (\S+)$/.exec(nm))) near(beatsOf(m[1]), 1 / (o.lpw ? o.lpw[0] : 1), nm); break;
    }
    n++;
  }
  assert.equal(n, 239);
});

test('the first sixteen FLX patterns do what they are called', () => {
  const f = new FlexCore(48000), r = (k, p) => f.pat(k, p, 0), P = [0.1, 0.6, 1.3, 2.2, 2.9, 3.4, 3.8];
  for (const p of P.filter(p => p < 3)) near(r(1, p + 1), r(1, p), 'REPEAT 1/4 repeats a beat');
  for (const p of P) assert.ok(r(1, p) < 1, 'REPEAT 1/4 plays the first beat');
  for (const p of P) near(r(2, p), Math.floor(p) + p % 0.5, 'REPEAT 1/8');
  for (const p of P) near(r(3, p), p < 3 ? p : 3 + (p - 3) % 0.25, 'FILL 1/16: a 16th roll in beat 4');
  for (const p of P) near(r(5, p), p / 2, 'HALF SPEED');
  for (const p of P) { f.pat(11, p, 0); assert.equal(f.pg, p % 0.25 < 0.125 ? 1 : 0, 'GATE 1/16'); }
  for (const p of P) near(r(7, p), Math.floor(p) - p % 1, 'REVERSE BEATS: the beat before, backwards');
  for (const p of P) near(r(15, p) - (Math.floor(p) - 1), 2 * (p % 1), 'OCTAVE UP: the beat before at double speed');
});

// run a block of a steady signal through an effect with the clock running, return how far the output is from the input
function runFx(cls, dp, tr, blocks = 40) {
  const fx = new cls(48000), n = 128; let e = 0, a = 0, beat = 0;
  for (let b = 0; b < blocks; b++) {
    const L = new Float32Array(n), R = new Float32Array(n), bp = new Float32Array(n);
    for (let i = 0; i < n; i++) { const s = b * n + i; L[i] = R[i] = Math.sin(s * 0.05) * 0.5 + ((s * 7) % 200) / 400; bp[i] = beat % 16; beat += 2 / 48000 * 8; }   // 480 BPM: bars go by fast
    const L0 = L.slice(); fx.process(L, R, n, dp, Object.assign({ bp }, tr));
    if (b > blocks / 2) for (let i = 0; i < n; i++) { e += (L[i] - L0[i]) ** 2; a += L0[i] ** 2; }
  }
  return e / a;
}
test('STARTS ON FLX: a Flex plays nothing until an FLX (or FXO on); AT PLAY plays its PATTERN as before', () => {
  const dp = { pat: 5, mix: 1, xf: 2, wait: 1 };
  assert.ok(runFx(FlexCore, dp, { flx: -1, sw: -1 }) < 1e-9, 'waiting: the sound as it is');
  assert.ok(runFx(FlexCore, dp, { flx: 5, sw: -1 }) > 1e-3, 'an FLX: the pattern');
  assert.ok(runFx(FlexCore, dp, { flx: 0, sw: -1 }) < 1e-9, 'FLX 00: off');
  assert.ok(runFx(FlexCore, dp, { flx: -1, sw: 1 }) > 1e-3, 'FXO on: its own PATTERN');
  assert.ok(runFx(FlexCore, Object.assign({}, dp, { wait: 0 }), { flx: -1, sw: -1 }) > 1e-3, 'AT PLAY: its PATTERN from PLAY');
});
test('STARTS ON GAT: a Step gate stays open until a GAT (or FXO on)', () => {
  const dp = { pat: 2, m1: 0, m2: 0, div: 1, len: 0.5, depth: 1, smooth: 1, wait: 1 };
  assert.ok(runFx(StepGateCore, dp, { gat: -1, sw: -1 }) < 1e-9, 'waiting: open');
  assert.ok(runFx(StepGateCore, dp, { gat: 3, sw: -1 }) > 1e-2, 'a GAT: gating');
  assert.ok(runFx(StepGateCore, dp, { gat: -1, sw: 1 }) > 1e-2, 'FXO on: its PATTERN');
  assert.ok(runFx(StepGateCore, Object.assign({}, dp, { wait: 0 }), { gat: -1, sw: -1 }) > 1e-2, 'AT PLAY');
});

test('STARTS: new Flex / Step gate slots wait, slots from older songs keep playing from PLAY; old FLX 11–FF are written 10', () => {
  assert.match(cut(/flex:\{n:'FLEX'[\s\S]*?map:f => [^\n]*/, 'TFX.flex'), /EN\('wait', 'STARTS', 1, \['AT PLAY', 'ON FLX'\]\)[\s\S]*f\.wait \| 0\]/);
  assert.match(cut(/sgate:\{n:'SGAT'[\s\S]*?map:f => [^\n]*/, 'TFX.sgate'), /EN\('wait', 'STARTS', 1, \['AT PLAY', 'ON GAT'\]\)[\s\S]*f\.wait \| 0\]/);
  const nt = cut(/function normTfx\(o\)\{[\s\S]*?\n\}\n/, 'normTfx');
  const TFX = { flex: { ps: [{ k: 'pat', d: 1, min: 0, max: 255 }, { k: 'wait', d: 1, min: 0, max: 1 }] }, eq: { ps: [{ k: 'g', d: 3, min: 0, max: 9 }] } };
  const f = new Function('TFX', 'clamp', 'int', 'pumpNorm1', 'freshTfx', nt + 'return normTfx;')(TFX, (v, a, b) => Math.max(a, Math.min(b, v)), (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d, null,
    type => { const o = { type, on: true }; for (const p of TFX[type].ps) o[p.k] = p.d; return o; });
  assert.equal(f({ type: 'flex', pat: 3 }).wait, 0, 'a saved slot without STARTS: AT PLAY');
  assert.equal(f({ type: 'flex', pat: 3, wait: 1 }).wait, 1);
  assert.equal(f({ type: 'flex', pat: 3, wait: 0 }).wait, 0);
  assert.match(cut(/else if \(!j\.flx256\)\{[^\n]*\n[^\n]*/, 'normSong FLX'), /r\[m \+ 1\] > 16\)\{ r\[m \+ 1\] = 16; n\+\+; \}/);
  // the event v the sequencer sends (flxCap): the value as written, unless the song has the old flag
  const cap = cut(/const flxCap = [^\n]*/, 'flxCap'); const g = new Function('S', cap + ';return flxCap;');
  assert.equal(g({})(0x92), 0x92); assert.equal(g({ flxOld: true })(0x92), 16);
});

test('FX bar: the pattern a Flex / Step gate / Retrigger slot plays now (command, its own, waiting, FXO)', () => {
  const fn = cut(/function dkFxPatState\(f, k, t, at\)\{[\s\S]*?\n\}\n/, 'dkFxPatState');
  const mk = vals => new Function('fxcNow', 'FlexCore', 'StepGateCore', 'RetrigCore', 'hx', fn + 'return dkFxPatState;')(
    (t, p) => (vals[p] ?? -1), FlexCore, StepGateCore, { NAMES: ['1/4', '1/8', '1/16'] }, v => v.toString(16).toUpperCase().padStart(2, '0'));
  let s = mk({})({ type: 'flex', on: true, pat: 5, wait: 1 }, 0, 0, 0); assert.equal(s.wait, true); assert.equal(s.p, 0); assert.equal(s.src, 'WAITS FOR FLX');
  s = mk({ flx: 0x2a })({ type: 'flex', on: true, pat: 5, wait: 1 }, 0, 0, 0); assert.equal(s.p, 0x2a); assert.equal(s.src, 'FLX'); assert.match(s.name, /^FLX 2A · STUTTER 1\/16 EVERY 1\/2$/);
  s = mk({ sw0: 1 })({ type: 'flex', on: false, pat: 5, wait: 1 }, 0, 0, 0); assert.equal(s.on, true); assert.equal(s.p, 5, 'FXO on: its own pattern');
  s = mk({})({ type: 'flex', on: true, pat: 5, wait: 0 }, 0, 0, 0); assert.equal(s.p, 5); assert.equal(s.src, 'PATTERN');
  s = mk({})({ type: 'sgate', on: true, pat: 3, wait: 1 }, 0, 0, 0); assert.equal(s.name, 'OPEN');
  s = mk({ gat: 4 })({ type: 'sgate', on: true, pat: 3, wait: 1 }, 0, 0, 0); assert.equal(s.p, 4);
  s = mk({})({ type: 'retrig', on: true, mode: 0, div: 2 }, 0, 0, 0); assert.equal(s.src, 'WAITS FOR RTG');
  s = mk({ rtg: 2 })({ type: 'retrig', on: true, mode: 0, div: 2 }, 0, 0, 0); assert.equal(s.name, 'REPEATING 1/8');
});
