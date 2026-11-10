// RUNAWAY KICK (10 Nov, experiment): rsTick + rsHold + rkTick are cut out of index.html and driven at 25 ms ticks against a fake AudioContext
// (currentTime, state, suspend(), resume()) and a fake performance.now. The fake clock runs at a chosen speed while 'running' and stands still
// while suspended; the song's side of rsHold is stubbed.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const blk = cut(/const RS_W = 0\.6[^\n]*\n/, 'RS constants') + cut(/function rsTick\(s\)\{[\s\S]*?\n\/\/ Everything/, 'rsTick').replace(/\n\/\/ Everything$/, '\n') +
  cut(/function rsHold\(s, d\)\{[\s\S]*?held the song: audio clock leapt[^\n]*\n/, 'rsHold') +
  cut(/const RUNKICK = true;[\s\S]*?\n\/\/ One pump tick/, 'the kick').replace(/\n\/\/ One pump tick$/, '\n');

function rig(o = {}) {
  const clock = { now: 5000 }, log = [], calls = [];
  const ctx = { currentTime: 100, state: 'running', suspend() { calls.push('suspend@' + clock.now); this.state = 'suspended'; return Promise.resolve(); }, resume() { calls.push('resume@' + clock.now); this.state = 'running'; return Promise.resolve(); } };
  const mx = { lfo: [], pend: [], ccr: [], echo: [] };
  const sb = vm.createContext({ Math, Float64Array, Object, Number, isFinite, console, performance: { now: () => clock.now }, ctx, mx,
    tp: { on: true, core: false, t0: 0, st: 0, rsUntil: 0 }, stgT0: 0, rkOn: o.on !== false, rsScope: () => sb.scopeOn,
    PB: { log, rk: { sum: 0, hot: 0, win: 0, n: 0, last: -1e9, watch: null } }, pbLog: s => log.push((clock.now / 1000).toFixed(2) + ' ' + s), pbState: () => 'state ' + ctx.state });
  sb.scopeOn = true; sb.rkKick = null;
  vm.runInContext(blk + '\nthis.api = {rsTick, rkTick, rkReset, kick: () => rkKick};', sb);
  const s = { base: 0, time: 0, alive: () => true, btT: null };
  // speed: clock seconds per wall second (a number or a function of seconds since the run began); jitter: +-jitter s on each tick's clock reading
  function run(secs, speed, jitter = 0, leapAt = -1) {
    const t00 = clock.now, ticks = Math.round(secs * 40);
    for (let i = 0; i < ticks; i++) {
      clock.now += 25; const t = (clock.now - t00) / 1000;
      if (ctx.state === 'running') ctx.currentTime += 0.025 * (typeof speed === 'function' ? speed(t) : speed);
      if (leapAt >= 0 && Math.abs(t - leapAt) < 1e-6) ctx.currentTime += 32;
      const real = ctx.currentTime; if (jitter) ctx.currentTime += (Math.random() * 2 - 1) * jitter;
      sb.api.rsTick(s); sb.api.rkTick(); ctx.currentTime = real;
    }
  }
  return { sb, ctx, run, log, calls, clock, s };
}
const kicks = R => R.log.filter(l => /kick: audio clock/.test(l));
const t = c => parseFloat(c.split('@')[1]);

test('kick: a healthy clock (1.0x, +-30 ms jitter) for 60 s never kicks', () => {
  const R = rig(); R.run(60, 1, 0.03);
  assert.equal(R.calls.length, 0, R.calls.join()); assert.equal(R.sb.PB.rk.n, 0);
});

test('kick: 14x from t=5 s: one suspend 1.5-2.1 s in, resume 150 ms later, the result logged 2 s after', () => {
  const R = rig(); R.run(5, 1); const t0 = R.clock.now; R.run(8, 14);
  const sus = R.calls.filter(c => /^suspend/.test(c)), res = R.calls.filter(c => /^resume/.test(c));
  assert.equal(sus.length, 1, 'one suspend: ' + R.calls);
  const at = (t(sus[0]) - t0) / 1000;
  assert.ok(at >= 1.4 && at <= 2.2, 'first suspend ' + at + ' s into the runaway');
  const dt = t(res[0]) - t(sus[0]);
  assert.ok(dt >= 150 && dt <= 200, 'resume ' + dt + ' ms later');
  assert.match(kicks(R)[0], /ran ×1[34]\.\d for 1\.5 s: pausing the engine 150 ms/);
  assert.ok(R.log.some(l => /kick result: clock \+\d+\.\d\d s in 2\.00 s/.test(l)), R.log.join('\n'));
  console.log('  first kick', at.toFixed(2), 's into the runaway; resume after', dt, 'ms;', R.log.find(l => /kick result/.test(l)));
});

test('kick: the clock keeps running 14x after the resume: kicks at least RK_GAP apart, at most RK_MAX', () => {
  const R = rig(); const t0 = R.clock.now; R.run(60, 14);
  const sus = R.calls.filter(c => /^suspend/.test(c)).map(t);
  assert.equal(sus.length, 4, 'RK_MAX kicks: ' + sus.length);
  for (let i = 1; i < sus.length; i++) assert.ok(sus[i] - sus[i - 1] > 8000, 'gap ' + (sus[i] - sus[i - 1]));
  assert.equal(R.sb.api.kick(), null);
  console.log('  suspends at (s):', sus.map(x => ((x - t0) / 1000).toFixed(2)).join(', '));
});

test('kick: a single +32 s leap in one tick, then 1.0x: no kick', () => {
  const R = rig(); R.run(5, 1); R.run(20, 1, 0, 3); assert.equal(R.calls.length, 0, R.calls.join());
  assert.ok(R.sb.PB.rk.hot === 0);
});

test('kick: setting Off, or rsScope() false: no kick', () => {
  const A = rig({ on: false }); A.run(20, 14); assert.equal(A.calls.length, 0);
  const B = rig(); B.sb.scopeOn = false; B.run(20, 14); assert.equal(B.calls.length, 0);
});

test('kick: STOP (rkReset) while a kick is pending resumes at once; a pending kick resumes even with the setting turned off', () => {
  const R = rig(); R.run(5, 14);
  let guard = 0; while (!R.sb.api.kick() && guard++ < 400) R.run(0.025, 14);
  assert.ok(R.sb.api.kick(), 'a kick is pending'); assert.equal(R.ctx.state, 'suspended');
  R.sb.api.rkReset(); assert.equal(R.sb.api.kick(), null); assert.equal(R.ctx.state, 'running', 'resumed immediately'); assert.equal(R.sb.PB.rk.n, 0, 'count cleared for the next PLAY');
  const Q = rig(); Q.run(5, 14); guard = 0; while (!Q.sb.api.kick() && guard++ < 400) Q.run(0.025, 14);
  Q.sb.rkOn = false; Q.run(0.3, 14); assert.equal(Q.ctx.state, 'running', 'the setting cannot leave it suspended');
});

test('kick: wiring in index.html (audio() and audioInterrupted() leave a kick alone; report line; reset on PLAY and STOP)', () => {
  assert.match(src, /if \(rkKick\) return;/); assert.match(src, /ctx\.state !== 'closed' && !rkKick\)\{ bgSus = false/);
  assert.match(src, /, kicked \$\{PB\.rk\.n\}×/); assert.match(src, /rkReset\(\); pbLog\('PLAY '/); assert.match(src, /G\('runaway kick', rkReset\)/);
  assert.match(src, /rsTick\(s\); else PB\.rsA = null;\n  if \(RUNKICK\) rkTick\(\);/); assert.match(src, /thunderplus-runkick-v1/);
});
