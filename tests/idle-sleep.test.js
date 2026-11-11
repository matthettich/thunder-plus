// IDLE SLEEP (10 Nov): bgWait / bgArm / idleHold / idleQuiet / bgIdle are cut out of index.html and run in a vm with a fake AudioContext, a fake
// clock and a fake timer queue. No browser, no audio device. Checks the arming logic of the on-screen sleep and that the hidden (20 s) and
// Low power (30 s) sleeps are unchanged.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const blk = cut(/var IDLE_SLEEP = true[\s\S]*?\n\}\n(?=document\.addEventListener\('visibilitychange')/, 'the idle sleep block');

function rig(o = {}) {
  const clock = { now: 1000 }, log = [], calls = [], timers = [];
  const ctx = { state: 'running', suspend() { calls.push('suspend@' + Math.round(clock.now / 1000)); this.state = 'suspended'; return Promise.resolve(); } };
  const analyser = { fftSize: 2048, level: 0, getFloatTimeDomainData(b) { b.fill(0); if (this.level) b[10] = this.level; } };
  const sb = vm.createContext({ Float32Array, Math, Promise, console, performance: { now: () => clock.now },
    setTimeout: (f, ms) => { const t = { f, at: clock.now + ms, id: timers.length + 1 }; timers.push(t); return t.id; },
    clearTimeout: id => { const t = timers.find(x => x.id === id); if (t) t.dead = true; },
    document: { hidden: !!o.hidden }, lpOn: !!o.lp, ctx, bgSus: false, lastSnd: 0, audioWake: 0, bgSusT: 0,
    seqHandHere: () => o.phone !== false, tp: { on: false }, rendering: false, midi: { access: null, clkIn: false, clk: [false, false] }, midiHeld: new Set(), kb: { held: new Set() },
    synHeld: [], padSrcs: [], RCD: { stream: null, io: null, on: false, sync: null, mon: null }, IOH: { state: 'off' }, lk: { on: false }, warmTok: null,
    rkBusy: () => false, DR: { busy: false }, view: 'synth', sndPH: [], rt: { man: [analyser] },
    pbLog: s => log.push(s), pbState: () => 'state ' + ctx.state });
  sb.IDLE_SLEEP = true;
  vm.runInContext(blk + '\nthis.api = {bgArm, bgIdle, bgWait, idleHold};', sb);
  const run = secs => {   // advance the fake clock, firing due timers in order
    const end = clock.now + secs * 1000;
    for (;;) { const due = timers.filter(t => !t.dead && t.at <= end).sort((a, b) => a.at - b.at)[0]; if (!due) break; due.dead = true; clock.now = Math.max(clock.now, due.at); due.f(); }
    clock.now = end;
  };
  return { sb, ctx, analyser, log, calls, clock, run, arm: () => sb.api.bgArm(), sound: () => { sb.audioWake = clock.now; sb.api.bgArm(); } };
}
const slept = R => R.calls.filter(c => /suspend/.test(c));

test('phone: sleeps after 90 s of nothing (plus the two quiet readings), logs once', () => {
  const R = rig(); R.arm(); R.run(89); assert.equal(slept(R).length, 0, 'not before 90 s'); R.run(20);
  assert.equal(slept(R).length, 1); assert.equal(R.sb.bgSus, true);
  assert.equal(R.log.length, 1); assert.match(R.log[0], /^engine asleep after (9\d|10\d) s idle · state running$/);
});
test('desktop: 300 s', () => {
  const R = rig({ phone: false }); R.arm(); R.run(290); assert.equal(slept(R).length, 0); R.run(30); assert.equal(slept(R).length, 1);
});
test('kill switch off: never sleeps visible', () => {
  const R = rig(); R.sb.IDLE_SLEEP = false; R.arm(); R.run(3600); assert.equal(slept(R).length, 0);
});
test('a sound during the wait restarts it', () => {
  const R = rig(); R.arm(); R.run(60); R.sound(); R.run(60); assert.equal(slept(R).length, 0, '60 s after the sound: still awake'); R.run(40); assert.equal(slept(R).length, 1);
});
test('a tail still audible keeps it awake until silent', () => {
  const R = rig(); R.analyser.level = 0.01; R.arm(); R.run(200); assert.equal(slept(R).length, 0); R.analyser.level = 0; R.run(12); assert.equal(slept(R).length, 1);
});
test('a tail that returns between the two quiet readings is caught', () => {
  const R = rig(); R.arm(); R.run(90.5); assert.equal(slept(R).length, 0, 'first quiet reading only'); R.analyser.level = 0.01; R.run(6); assert.equal(slept(R).length, 0);
  R.analyser.level = 0; R.run(12); assert.equal(slept(R).length, 1);
});
const holds = {
  'transport playing': R => { R.sb.tp.on = true; }, 'render / freeze': R => { R.sb.rendering = true; }, 'MIDI clock in': R => { R.sb.midi.clkIn = true; },
  'MIDI input connected': R => { R.sb.midi.access = { inputs: { size: 1 } }; }, 'a key held': R => { R.sb.kb.held.add('a'); },
  'a sample playing': R => { R.sb.padSrcs.push(new Set([1])); }, 'recording take': R => { R.sb.RCD.on = true; }, 'input monitoring': R => { R.sb.RCD.mon = {}; },
  'recorder input open': R => { R.sb.RCD.stream = {}; }, 'native helper input': R => { R.sb.RCD.io = {}; }, 'native helper starting': R => { R.sb.IOH.state = 'starting'; },
  'MIDI clock out': R => { R.sb.midi.clk[1] = true; }, 'Link': R => { R.sb.lk.on = true; }, 'first-PLAY warm-up': R => { R.sb.warmTok = {}; },
  'kick in flight': R => { R.sb.rkBusy = () => true; }, 'device recovery running': R => { R.sb.DR.busy = true; }, 'waves screen': R => { R.sb.view = 'waves'; },
  'chains screen': R => { R.sb.view = 'chains'; }, 'playheads travelling': R => { R.sb.sndPH.push({}); },
};
for (const k of Object.keys(holds)) test('never sleeps while: ' + k, () => {
  const R = rig(); holds[k](R); R.arm(); R.run(2000); assert.equal(slept(R).length, 0);
});
test('a hold that ends: sleeps afterwards (no stuck timer)', () => {
  const R = rig(); R.sb.view = 'chains'; R.arm(); R.run(400); assert.equal(slept(R).length, 0); R.sb.view = 'synth'; R.run(200); assert.equal(slept(R).length, 1);
});
test('Low power: still 30 s, no on-screen log, and the holds do not apply', () => {
  const R = rig({ lp: true }); R.sb.view = 'waves'; R.arm(); R.run(29); assert.equal(slept(R).length, 0); R.run(3); assert.equal(slept(R).length, 1); assert.equal(R.log.length, 0);
  assert.equal(R.sb.api.bgWait(), 30000);
});
test('hidden and stopped: still 20 s; busy still refuses', () => {
  const R = rig({ hidden: true }); R.arm(); R.run(19); assert.equal(slept(R).length, 0); R.run(2); assert.equal(slept(R).length, 1); assert.equal(R.log.length, 0);
  const B = rig({ hidden: true }); B.sb.tp.on = true; B.arm(); B.run(300); assert.equal(slept(B).length, 0);
});
test('hidden with the kill switch off: unchanged (20 s)', () => {
  const R = rig({ hidden: true }); R.sb.IDLE_SLEEP = false; R.arm(); R.run(21); assert.equal(slept(R).length, 1);
});
test('a context that is not running is left alone, and the timer is re-armed while visible', () => {
  const R = rig(); R.ctx.state = 'suspended'; R.arm(); R.run(100); assert.equal(slept(R).length, 0); R.ctx.state = 'running'; R.run(200); assert.equal(slept(R).length, 1);
});
