// Golden tests for the mastering chain (master-worklet.js): resonance suppressor -> compressor -> limiter, driven through the real
// MasterProcessor. The signals are loud enough to ENGAGE the compressor and limiter (a quiet one passes through all three untouched,
// which is how an output-stage bug can hide), and each stage is also checked for what it is supposed to do, not only for "unchanged".
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const SR = 48000, BLOCK = 128;

function processor(settings, opts = {}) {
  const ctx = H.freezeClock(H.workletScope({ sampleRate: SR })); H.load(ctx, 'master-worklet.js');
  const P = new (ctx.__registered['thunder-master'])({ processorOptions: Object.assign({ meters: false, set: settings }, opts) });
  return { ctx, P };
}
/* A signal with a resonance (a loud narrow tone on top of broadband content), a transient burst, and a level that rises, so the
   suppressor has something to find, the compressor something to squash and the limiter something to catch. Deterministic. */
function loud(n, gainDb = 0) {
  const out = new Float32Array(n), g = Math.pow(10, gainDb / 20); let s = 0x1234567;
  for (let i = 0; i < n; i++) {
    s ^= s << 13; s >>>= 0; s ^= s >>> 17; s ^= s << 5; s >>>= 0; const noise = (s / 4294967296 - .5) * .3, t = i / SR, ramp = .4 + .6 * Math.min(1, i / (n * .5));
    const burst = (i % 12000) < 400 ? .9 * Math.sin(2 * Math.PI * 90 * t) : 0;
    out[i] = Math.fround(g * ramp * (.35 * Math.sin(2 * Math.PI * 440 * t) + .5 * Math.sin(2 * Math.PI * 3150 * t) + noise + burst));
  }
  return out;
}
function run(settings, gainDb = 0, blocks = 400, opts) {
  const { P } = processor(settings, opts), src = loud(BLOCK * blocks, gainDb), L = new Float32Array(BLOCK * blocks), R = new Float32Array(BLOCK * blocks);
  for (let b = 0; b < blocks; b++) {
    const a = src.subarray(b * BLOCK, (b + 1) * BLOCK), out = [new Float32Array(BLOCK), new Float32Array(BLOCK)];
    P.process([[a, a]], [out]); L.set(out[0], b * BLOCK); R.set(out[1], b * BLOCK);
  }
  return { L, R, src };
}
const peak = a => { let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return m; };
const rms = a => { let e = 0; for (const v of a) e += v * v; return Math.sqrt(e / a.length); };
const finite = a => { for (const v of a) if (!Number.isFinite(v)) return false; return true; };
const db = x => 20 * Math.log10(Math.max(x, 1e-12));
const tail = (a, k) => a.subarray(a.length - k);

const SETTINGS = {
  off: {},
  sup: { sup: { on: true } },
  comp: { comp: { on: true, thr: -24, ratio: 4, atk: 5, rel: 120, auto: false, knee: 6, makeup: 0, automk: false, mix: 1, hpf: 80 } },
  lim: { lim: { on: true, drive: 6, ceil: -1, rel: 120, look: 3, tp: true } },
  all: { sup: { on: true }, comp: { on: true, thr: -20, ratio: 3, atk: 8, rel: 150, auto: true, knee: 6, makeup: 0, automk: true, mix: 1, hpf: 80 }, lim: { on: true, drive: 3, ceil: -1, rel: 120, look: 3, tp: true } },
  compParallel: { comp: { on: true, thr: -30, ratio: 8, atk: 2, rel: 80, auto: false, knee: 0, makeup: 3, automk: false, mix: .5, hpf: 20 } },
};

test('master: it registers as thunder-master and is a bit-exact pass-through when every stage is off', () => {
  const { ctx } = processor({}); assert.ok(ctx.__registered['thunder-master']);
  const r = run(SETTINGS.off, 0, 50); for (let i = 0; i < r.L.length; i++) if (r.L[i] !== r.src[i] || r.R[i] !== r.src[i]) assert.fail('sample ' + i + ' changed with every stage off');
});

test('master: every configuration is finite and matches its golden hash', () => {
  const actual = {};
  for (const [name, set] of Object.entries(SETTINGS)) for (const gain of [-12, 0, 9]) {
    const r = run(set, gain); assert.ok(finite(r.L) && finite(r.R), name + ' @' + gain + ' dB produced NaN/Infinity');
    actual[name + ' @' + gain + 'dB'] = H.hashFloats([r.L, r.R]);
  }
  const g = H.golden('master', actual);
  assert.ok(g.updated || g.diff.length === 0, 'mastering output changed in ' + (g.diff || []).length + ' configuration(s):\n  ' + (g.diff || []).join('\n  '));
});

test('master: the same settings rendered twice are bit-identical', () => {
  for (const name of ['sup', 'comp', 'lim', 'all']) { const a = run(SETTINGS[name]), b = run(SETTINGS[name]); assert.equal(H.hashFloats([a.L, a.R]), H.hashFloats([b.L, b.R]), name); }
});

// A golden hash says "unchanged"; these say the stage does its job, so a stage that quietly stopped working cannot hide behind a stable hash.
test('master: the limiter holds the output at or under its ceiling, including true peaks', () => {
  const ceilLin = Math.pow(10, -1 / 20);
  for (const gain of [0, 6, 12]) {
    const r = run({ lim: { on: true, drive: 6, ceil: -1, rel: 120, look: 3, tp: true } }, gain, 500);
    const p = Math.max(peak(r.L.subarray(2000)), peak(r.R.subarray(2000)));   // after the look-ahead has filled
    assert.ok(p <= ceilLin + 1e-4, 'at +' + gain + ' dB in, the peak is ' + db(p).toFixed(3) + ' dBFS, over the -1 dB ceiling');
  }
  const hot = run({ lim: { on: true, drive: 6, ceil: -1, rel: 120, look: 3, tp: true } }, 12, 500);
  assert.ok(peak(hot.L.subarray(2000)) > ceilLin * .9, 'a hot signal is actually brought to the ceiling, not just crushed ' + db(peak(hot.L.subarray(2000))).toFixed(2) + ' dBFS');
});

test('master: the compressor reduces the level of a loud signal, and a higher ratio reduces it more', () => {
  const dry = run(SETTINGS.off, 0), a = run({ comp: { on: true, thr: -24, ratio: 2, atk: 5, rel: 120, auto: false, knee: 6, makeup: 0, automk: false, mix: 1, hpf: 80 } }),
    b = run({ comp: { on: true, thr: -24, ratio: 10, atk: 5, rel: 120, auto: false, knee: 6, makeup: 0, automk: false, mix: 1, hpf: 80 } });
  const k = 20000; assert.ok(rms(tail(a.L, k)) < rms(tail(dry.L, k)), 'ratio 2 lowers the level'); assert.ok(rms(tail(b.L, k)) < rms(tail(a.L, k)), 'ratio 10 lowers it further');
});

test('master: the compressor leaves a signal below its threshold alone', () => {
  const quiet = run({ comp: { on: true, thr: -6, ratio: 4, atk: 5, rel: 120, auto: false, knee: 0, makeup: 0, automk: false, mix: 1, hpf: 20 } }, -30), dry = run(SETTINGS.off, -30);
  const k = 20000, d = Math.abs(db(rms(tail(quiet.L, k))) - db(rms(tail(dry.L, k)))); assert.ok(d < .5, 'a -30 dB signal under a -6 dB threshold moves by only ' + d.toFixed(3) + ' dB');
});

test('master: the compressor\'s Mix blends dry and processed (0 = the dry signal, 1 = fully processed)', () => {
  const mk = mix => run({ comp: { on: true, thr: -30, ratio: 8, atk: 2, rel: 80, auto: false, knee: 0, makeup: 0, automk: false, mix, hpf: 20 } });
  const dry = run(SETTINGS.off), m0 = mk(0), m1 = mk(1), mh = mk(.5), k = 20000;
  assert.ok(Math.abs(db(rms(tail(m0.L, k))) - db(rms(tail(dry.L, k)))) < .05, 'mix 0 is the dry level');
  assert.ok(rms(tail(m1.L, k)) < rms(tail(mh.L, k)) && rms(tail(mh.L, k)) < rms(tail(m0.L, k)), 'mix 0.5 sits between');
});

test('master: the suppressor is transparent on broadband sound, reduces a pure tone (a resonance), and has its fixed latency', () => {
  const N = BLOCK * 600, k = 30000; let seed = 99;
  const noise = new Float32Array(N), tone = new Float32Array(N);
  for (let i = 0; i < N; i++) { seed ^= seed << 13; seed >>>= 0; seed ^= seed >>> 17; seed >>>= 0; seed ^= seed << 5; seed >>>= 0; noise[i] = Math.fround((seed / 4294967296 - .5) * .4); tone[i] = Math.fround(.3 * Math.sin(2 * Math.PI * 3150 * i / SR)); }
  const go = sig => { const { P } = processor(SETTINGS.sup), out = new Float32Array(N); for (let b = 0; b < 600; b++) { const a = sig.subarray(b * BLOCK, (b + 1) * BLOCK), o = [new Float32Array(BLOCK), new Float32Array(BLOCK)]; P.process([[a, a]], [o]); out.set(o[0], b * BLOCK); } return out; };
  const n = go(noise), t = go(tone);
  // Measured: white noise -0.17 dB, a pure 3150 Hz tone -11 dB. (A pure sine is one sharp resonance, which is what it exists to reduce.)
  assert.ok(Math.abs(db(rms(tail(n, k))) - db(rms(tail(noise, k)))) < 1, 'broadband noise passes with under 1 dB change');
  assert.ok(db(rms(tail(t, k))) - db(rms(tail(tone, k))) < -6, 'a pure tone is reduced by more than 6 dB');
  assert.ok(rms(n.subarray(0, 2048)) < rms(noise.subarray(0, 2048)) * .01, 'the first 2048 samples are the empty latency');
});

test('master: left and right get the same gain (stereo-linked), so the image never shifts', () => {
  const { P } = processor(SETTINGS.all), n = BLOCK * 300, a = loud(n), b = new Float32Array(n); for (let i = 0; i < n; i++) b[i] = a[i] * .5;   // right is exactly half of left
  const L = new Float32Array(n), R = new Float32Array(n);
  for (let k = 0; k < 300; k++) { const out = [new Float32Array(BLOCK), new Float32Array(BLOCK)]; P.process([[a.subarray(k * BLOCK, (k + 1) * BLOCK), b.subarray(k * BLOCK, (k + 1) * BLOCK)]], [out]); L.set(out[0], k * BLOCK); R.set(out[1], k * BLOCK); }
  let worst = 0; for (let i = 20000; i < n; i++) if (Math.abs(L[i]) > .05) worst = Math.max(worst, Math.abs(R[i] / L[i] - .5)); assert.ok(worst < .02, 'right stays half of left (worst ratio error ' + worst.toFixed(4) + ')');
});

test('master: a mono input (no right channel) and an unplugged input are handled', () => {
  const { P } = processor(SETTINGS.all); const a = loud(BLOCK);
  const o1 = [new Float32Array(BLOCK)]; assert.doesNotThrow(() => P.process([[a]], [o1])); assert.ok(finite(o1[0]));
  const o2 = [new Float32Array(BLOCK), new Float32Array(BLOCK)]; assert.doesNotThrow(() => P.process([[]], [o2])); assert.ok(finite(o2[0]) && peak(o2[0]) === 0, 'no input -> silence');
  assert.equal(P.process([[a, a]], []), true, 'no output buffer: keeps running'); P.port.onmessage({ data: { dispose: true } }); assert.equal(P.process([[a, a]], [o2]), false, 'dispose ends the node');
});

test('master: settings can be changed while running without producing NaN or a click', () => {
  const { P } = processor(SETTINGS.off), src = loud(BLOCK * 300); let prev = 0, maxJump = 0;
  for (let b = 0; b < 300; b++) {
    if (b === 50) P.port.onmessage({ data: { set: SETTINGS.comp } }); if (b === 100) P.port.onmessage({ data: { set: SETTINGS.lim } }); if (b === 150) P.port.onmessage({ data: { set: SETTINGS.sup } }); if (b === 200) P.port.onmessage({ data: { set: SETTINGS.all } });
    const a = src.subarray(b * BLOCK, (b + 1) * BLOCK), out = [new Float32Array(BLOCK), new Float32Array(BLOCK)]; P.process([[a, a]], [out]);
    assert.ok(finite(out[0]), 'finite at block ' + b); for (const v of out[0]) { maxJump = Math.max(maxJump, Math.abs(v - prev)); prev = v; }
  }
  assert.ok(maxJump < 2, 'no sample-to-sample jump beyond 2.0 (got ' + maxJump.toFixed(3) + ')');
});

test('master: enabling the limiter with no other settings uses the defaults (-1 dB ceiling, true-peak on), as a user would get', () => {
  // Every other test passes ceil explicitly, so a changed default would go unnoticed. This is the settings object a user's first click makes.
  const ceilLin = Math.pow(10, -1 / 20);
  const r = run({ lim: { on: true } }, 12, 500), p = Math.max(peak(r.L.subarray(2000)), peak(r.R.subarray(2000)));
  assert.ok(p <= ceilLin + 1e-4, 'default ceiling holds the peak at ' + db(p).toFixed(3) + ' dBFS, over the -1 dB default');
  assert.ok(p > ceilLin * .85, 'and it is a limiter, not a mute (peak ' + db(p).toFixed(2) + ' dBFS)');
  const c = run({ comp: { on: true } }, 6), dry = run({}, 6), k = 20000;
  assert.ok(rms(tail(c.L, k)) < rms(tail(dry.L, k)), 'enabling the compressor alone, with its defaults, does compress (thr -14, ratio 2)');
});
