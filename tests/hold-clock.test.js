// HOLD KEEPS THE TEMPO (11 Nov): a hold (rsHold) moves the song's next beat on by d, which used to slow the effects' beat clock for that row;
// the track-FX worklet measures the tempo for its modules (Simple delay, ...) from that clock's slope (clkSpb), so the delay TIME glided.
// The code under test is cut out of index.html: the 'beat' branch of the player's play(), and clkSpb. The clock is simulated the way an
// AudioParam plays it back (setValueAtTime / linearRampToValueAtTime), read in 128-frame blocks like the worklet's a-rate `bp`.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what) { const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
const clkSrc = cut(/function clkSpb\(o, bp, n\)\{[\s\S]*?\n\}\n/, 'clkSpb');
const beatBody = cut(/if \(ev\.type === 'beat'\)\{\n      const o = clock\.offset[\s\S]*?\n      return;\n    \}/, "play()'s beat branch");
assert.match(src, /const HOLD_CLK_FLAT = true;/, 'HOLD_CLK_FLAT kill switch present and on');
const SR = 48000, BPM = 120, ROW = 0.125, BEATS_PER_ROW = 0.25;   // 120 bpm, a row = 1/16

// a fake AudioParam: the events as the page books them
function makeParam() { const ev = []; return { ev, setValueAtTime: (v, t) => ev.push({ k: 's', v, t }), linearRampToValueAtTime: (v, t) => ev.push({ k: 'r', v, t }), cancelScheduledValues() {} }; }
function valueAt(ev, t) {   // the Web Audio rules for these two event types
  let prev = null; for (const e of ev) { if (e.t <= t) prev = e; else { if (e.k === 'r' && prev) return prev.v + (e.v - prev.v) * (t - prev.t) / (e.t - prev.t); return prev ? prev.v : 0; } }
  return prev ? prev.v : 0;
}
// book `rows` beat events; `holds` = {atRow: d}: rsHold ran just before that row's event was booked
function book(flat, rows, holds) {
  const clock = { offset: makeParam() }, clk = { on: false, base: 0 }, PB = { rsGap: 0 };
  const HOLD_CLK_FLAT = flat, ac = { currentTime: 0 };
  const play = new Function('ev', 'clock', 'clk', 'PB', 'HOLD_CLK_FLAT', 'ac', beatBody.replace(/\n    \}$/, '\n    }') + '\n');
  let t = 1, b = 0;
  for (let r = 0; r < rows; r++) {
    if (holds[r]) { t += holds[r]; PB.rsGap += holds[r]; }   // rsHold
    play({ type: 'beat', t, b }, clock, clk, PB, HOLD_CLK_FLAT, ac);
    t += ROW; b += BEATS_PER_ROW;
  }
  return clock.offset.ev;
}
function runSpb(ev, t0, t1) {   // clkSpb over 128-frame blocks; returns the samples-per-beat it settles on, block by block
  const o = { spb: SR / 2 }, F = new Function(clkSrc + 'return clkSpb;')(), out = [];
  for (let f0 = Math.floor(t0 * SR); f0 < t1 * SR; f0 += 128) {
    const bp = new Float32Array(128); for (let i = 0; i < 128; i++) bp[i] = valueAt(ev, (f0 + i) / SR) % 16;
    F(o, bp, 128); out.push(o.spb);
  }
  return out;
}
const holds = { 40: 0.2, 41: 0.1, 60: 0.35, 80: 0.05, 81: 0.05, 82: 0.05 };

test('without the fix (kill switch off) a hold slows the clock and the measured tempo moves', () => {
  const spb = runSpb(book(false, 100, holds), 1 + 30 * ROW, 1 + 110 * ROW), want = SR * 60 / BPM;
  assert.ok(Math.max(...spb.map(x => Math.abs(x / want - 1))) > 0.05, 'the old behaviour should move the tempo (else this test shows nothing)');
});

test('with the fix a hold leaves the measured tempo (so every delay time) unchanged', () => {
  const spb = runSpb(book(true, 100, holds), 1 + 30 * ROW, 1 + 110 * ROW), want = SR * 60 / BPM;
  const worst = Math.max(...spb.map(x => Math.abs(x / want - 1)));
  assert.ok(worst < 0.01, 'tempo moved by ' + (worst * 100).toFixed(2) + ' %');
});

test('with the fix the clock still ends up where the song is (phase-locked effects follow the new phase)', () => {
  const ev = book(true, 100, holds), tEnd = 1 + 99 * ROW + 0.2 + 0.1 + 0.35 + 0.15;   // the last row's time, holds included
  const v = valueAt(ev, tEnd), want = (99 * BEATS_PER_ROW) % 16;
  assert.ok(Math.abs(v - want) < 1e-6, v + ' vs ' + want);
});

test('no hold, no change: the booking is the plain ramp as before', () => {
  const a = book(true, 20, {}), b = book(false, 20, {});
  assert.deepEqual(a, b);
});
