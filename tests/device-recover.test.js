// DEVICE RECOVERY (10 Nov): devRecover (and devOnState) are cut out of index.html and run in a vm with a fake AudioContext, fake
// enumerateDevices, a fake clock and fake toasts. No browser, no audio device.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const blk = cut(/const DEV_RECOVER = true;[\s\S]*?finally \{ DR\.busy = false; \}\n\}\n/, 'devRecover') + cut(/function devOnState\(\)\{[^\n]*\n/, 'devOnState');

function rig(o = {}) {
  const clock = { now: 10000 }, log = [], calls = [], toasts = [], errs = [], timers = [];
  const devs = o.devs || [{ kind: 'audiooutput', deviceId: 'usb1', label: 'Interface' }, { kind: 'audiooutput', deviceId: 'spk', label: 'Speakers' }];
  const st = { enum: 0 };
  const ctx = { state: o.state || 'running', sinkId: o.sinkId || '', setSinkId(id) { calls.push('sink:' + id); this.sinkId = id; return Promise.resolve(); }, resume() { calls.push('resume'); this.state = 'running'; return Promise.resolve(); } };
  const sb = vm.createContext({ ctx, aset: { sink: o.sink === undefined ? '' : o.sink }, sinkOk: o.sinkOk !== false, bgSus: !!o.bgSus, document: { hidden: false },
    navigator: { mediaDevices: { enumerateDevices: () => { st.enum++; return Promise.resolve(devs); } } },
    rkBusy: () => !!sb.kicking, kicking: false, performance: { now: () => clock.now }, setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; }, clearTimeout() {},
    pbLog: s => log.push(s), pbState: () => 'state ' + ctx.state, audStatus() {}, wvApi: () => null, toast: m => toasts.push(m), toastErr: m => errs.push(m), Promise, console });
  vm.runInContext(blk + '\nthis.api = {devRecover, devOnState, DR};', sb);
  return { sb, ctx, calls, toasts, errs, log, devs, st, clock, timers, run: (why, again) => sb.api.devRecover(why, again) };
}

test('chosen output gone: falls back to the default, one error toast, the saved choice stays', async () => {
  const R = rig({ sink: 'usb1', sinkId: 'usb1' });
  await R.run('devicechange');   // present: nothing to do, but the name is learnt
  assert.deepEqual(R.calls, []); assert.equal(R.errs.length, 0);
  R.devs.shift(); R.clock.now += 1000;
  await R.run('devicechange');
  assert.deepEqual(R.calls, ['sink:']); assert.equal(R.errs.length, 1); assert.match(R.errs[0], /“Interface” is gone/);
  assert.equal(R.sb.aset.sink, 'usb1', 'saved choice kept');
  R.clock.now += 1000; await R.run('again'); await R.run('again2', true);
  assert.equal(R.errs.length, 1, 'said once'); assert.deepEqual(R.calls, ['sink:']);
  assert.ok(R.log.some(l => /^device: done/.test(l)), 'the change is logged'); assert.ok(!R.log.some(l => /^device: devicechange/.test(l)), 'a trigger with a running engine is not logged (lead, 10 Nov)');
});

test('the output comes back: switched back, plain toast', async () => {
  const R = rig({ sink: 'usb1', sinkId: 'usb1' });
  await R.run('x'); R.devs.shift(); R.clock.now += 1000; await R.run('gone');
  R.devs.unshift({ kind: 'audiooutput', deviceId: 'usb1', label: 'Interface' }); R.clock.now += 1000; await R.run('back');
  assert.deepEqual(R.calls, ['sink:', 'sink:usb1']); assert.deepEqual(R.toasts, ['Audio output “Interface” is back.']); assert.equal(R.errs.length, 1);
  assert.equal(R.sb.api.DR.fb, null);
});

test('a suspended context that is not asleep on purpose is resumed; bgSus: left alone', async () => {
  const R = rig({ state: 'suspended' }); await R.run('engine suspended'); assert.deepEqual(R.calls, ['resume']);
  const I = rig({ state: 'interrupted' }); await I.run('x'); assert.deepEqual(I.calls, ['resume']);
  const B = rig({ state: 'suspended', bgSus: true }); await B.run('x'); assert.deepEqual(B.calls, []);
  const D = rig({ state: 'suspended' }); D.sb.document.hidden = true; D.sb.api.devOnState(); await Promise.resolve(); assert.deepEqual(D.calls, [], 'hidden: devOnState does nothing');
  const S = rig({ state: 'suspended' }); S.sb.api.devOnState(); await new Promise(r => setImmediate(r)); assert.deepEqual(S.calls, ['resume']);
});

test('a runaway kick in flight: nothing happens', async () => {
  const R = rig({ state: 'suspended', sink: 'gone', sinkId: 'gone' }); R.sb.kicking = true;
  await R.run('x'); assert.deepEqual(R.calls, []); assert.equal(R.st.enum, 0); assert.equal(R.log.length, 0);
  R.sb.kicking = false; await R.run('x'); assert.ok(R.calls.includes('resume'));
});

test('running with no chosen output: returns at once, no enumerateDevices, no log, no timer', async () => {
  const R = rig(); await R.run('window focus'); await R.run('screen back');
  assert.equal(R.st.enum, 0); assert.equal(R.log.length, 0); assert.equal(R.timers.length, 0); assert.deepEqual(R.calls, []);
});

test('no setSinkId in this browser: only the resume part', async () => {
  const R = rig({ state: 'suspended', sink: 'usb1', sinkOk: false }); await R.run('x');
  assert.deepEqual(R.calls, ['resume']); assert.equal(R.st.enum, 0);
});

test('an empty device list is not taken for "gone"', async () => {
  const R = rig({ sink: 'usb1', sinkId: 'usb1', devs: [] }); await R.run('x'); assert.deepEqual(R.calls, []); assert.equal(R.errs.length, 0);
});

test('debounce: a second run within 500 ms is dropped; a follow-up is booked 1 s later; the follow-up runs', async () => {
  const R = rig({ state: 'suspended' });
  await R.run('a'); R.ctx.state = 'suspended'; R.clock.now += 200; await R.run('b');
  assert.deepEqual(R.calls, ['resume']); assert.equal(R.timers.length, 1); assert.equal(R.timers[0].ms, 1000);
  R.ctx.state = 'suspended'; R.clock.now += 1000; await R.timers[0].f();
  assert.deepEqual(R.calls, ['resume', 'resume'], 'the follow-up is not debounced');
  assert.equal(R.timers.length, 1, 'and books no further one');
  R.ctx.state = 'suspended'; R.clock.now += 600; await R.run('c'); assert.equal(R.calls.length, 3, 'after 500 ms it runs again');
});

test('kill switch and shape: DEV_RECOVER exists, the triggers are wired', () => {
  assert.match(src, /const DEV_RECOVER = true;/);
  assert.match(src, /addEventListener\('devicechange', \(\) => \{[^\n]*devRecover\('devicechange'\)/);
  assert.match(src, /addEventListener\('sinkchange'[^\n]*devRecover\('sinkchange'\)/);
  assert.match(src, /devRecover\('woke from sleep'\)/); assert.match(src, /devRecover\('reset audio engine'\)/);
  assert.match(H.read('electron/main.js'), /powerMonitor\.on\(ev[^\n]*'thunder:power'/);
  assert.match(H.read('electron/preload.js'), /power: \{ on: cb =>[^\n]*'thunder:power'/);
});
