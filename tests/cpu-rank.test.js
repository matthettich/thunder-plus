// WHAT IS USING THE MOST CPU (12 Nov): the pure ranking behind the CPU meter's panel, cut out of index.html.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const m = src.match(/function cpuRank\(m\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'could not find cpuRank; this test needs updating');
const c = vm.createContext({ Math }); vm.runInContext(m[0] + '\nthis.f = cpuRank;', c);
const rank = x => JSON.parse(JSON.stringify(c.f(x)));
const base = { total: 0.4, peak: 0.45, parts: [], drops: 0, late: 0, worstLate: 0, stalls: 0, playing: true, engine: true, latency: 'balanced', lowPower: false };

test('ranked biggest first, rounded, the rest of the total shown as "everything else"; tiny parts dropped', () => {
  const r = rank({ ...base, parts: [{ name: 'Effects · Track 03 Bass', v: 0.0861 }, { name: 'Master effects', v: 0.121 }, { name: 'Chains · Track 01', v: 0.001 }] });
  assert.deepEqual(r.list.map(x => [x.name, x.share]), [['Master effects', '12%'], ['Effects · Track 03 Bass', '9%'], ['Everything else in the audio engine (drums, synth voices, mixing)', '19%']].sort((a, b) => parseInt(b[1]) - parseInt(a[1])));
  assert.equal(r.head, 'Audio engine: 40% of its time');
});
test('no total reported: only the measured parts, no invented remainder', () => {
  const r = rank({ ...base, total: null, peak: null, parts: [{ name: 'Master effects', v: 0.2 }] });
  assert.equal(r.list.length, 1); assert.match(r.head, /doesn’t report the whole audio engine/);
});
test('keep-up sentence: dropouts, late notes, over 100%, quiet, stopped, not started', () => {
  assert.match(rank({ ...base, drops: 3, late: 2 }).keep, /^Not keeping up: 3 dropouts in the last minute and 2 late notes\.$/);
  assert.match(rank({ ...base, drops: 1 }).keep, /1 dropout in/);
  assert.match(rank({ ...base, late: 1, worstLate: 0.123 }).keep, /^Mostly keeping up: 1 late note in the last minute \(worst 123 ms\), no dropouts\.$/);
  assert.match(rank({ ...base, total: 1.2 }).keep, /over 100%/);
  assert.equal(rank(base).keep, 'Keeping up: no dropouts or late notes in the last minute.');
  assert.match(rank({ ...base, stalls: 2 }).keep, /but 2 screen stalls/);
  assert.match(rank({ ...base, playing: false }).keep, /^Nothing playing\. No dropouts/);
  assert.equal(rank({ ...base, playing: false, engine: false }).keep, 'The audio engine has not started yet.');
});
test('the tip follows from what is measured', () => {
  assert.match(rank({ ...base, drops: 2, latency: 'interactive' }).tip, /Latency → Balanced/);
  assert.match(rank({ ...base, drops: 2, latency: 'balanced' }).tip, /Latency → Safest/);
  assert.match(rank({ ...base, drops: 2, latency: 'playback', parts: [{ name: 'Master effects', v: 0.3 }] }).tip, /Master effects is the heaviest/);
  assert.match(rank({ ...base, drops: 2, latency: 'playback' }).tip, /Reset audio engine/);
  assert.match(rank({ ...base, stalls: 3 }).tip, /Low power mode/); assert.doesNotMatch(rank({ ...base, stalls: 3, lowPower: true }).tip, /Low power/);
  assert.match(rank({ ...base, parts: [{ name: 'Master effects', v: 0.2 }] }).tip, /Master effects uses the most \(20%\)/);
  assert.equal(rank({ ...base, total: 0.1 }).tip, 'Nothing to fix: plenty of headroom.');
});
test('the panel costs nothing closed: its timer exists only while it is open', () => {
  assert.match(src, /cpuPanel = P; cpuFill\(\); P\.iv = setInterval\(cpuFill, 500\)/); assert.match(src, /function cpuClose\(\)\{ if \(!cpuPanel\) return; clearInterval\(cpuPanel\.iv\)/);
});
test('overloaded with no single heavy part and no dropouts counted: a suggestion, never "plenty of headroom" (review)', () => {
  const r = rank({ ...base, total: 1.12, parts: [{ name: 'A', v: 0.28 }, { name: 'B', v: 0.28 }, { name: 'C', v: 0.28 }, { name: 'D', v: 0.28 }] });
  assert.match(r.tip, /^Try: /); assert.doesNotMatch(r.tip, /headroom/);
});
test('THE SCREEN (13 Nov): drawing and the page sequencer are their own short section, a share of the page’s time, never added to the audio list', () => {
  assert.deepEqual(rank(base).screen, []); assert.equal(rank(base).screenHead, '');
  const r = rank({ ...base, screen: { ms: 0.126, n: 17.4 }, seqPage: { ms: 0.004, n: 40 } });
  assert.deepEqual(r.screen.map(x => [x.name, x.share, x.extra]), [['Screen drawing', '13% of the page’s time', '17 screen updates a second'], ['Sequencer (page)', '<1% of the page’s time', '']]);
  assert.match(r.screenHead, /separate budget/); assert.doesNotMatch(r.list.map(x => x.name).join(), /Screen/);
  assert.equal(r.head, 'Audio engine: 40% of its time');
});
test('a heavy screen with nothing else wrong suggests Low power mode (not when it is on)', () => {
  assert.match(rank({ ...base, total: 0.1, screen: { ms: 0.45, n: 17 } }).tip, /Low power mode \(Settings\): drawing the screen takes 45%/);
  assert.equal(rank({ ...base, total: 0.1, screen: { ms: 0.45, n: 17 }, lowPower: true }).tip, 'Nothing to fix: plenty of headroom.');
  assert.equal(rank({ ...base, total: 0.1, screen: { ms: 0.05, n: 17 } }).tip, 'Nothing to fix: plenty of headroom.');
});
test('the screen figure is two clock reads around draw(), added up per second; a stale figure reads as nothing', () => {
  assert.match(src, /const t0 = performance\.now\(\); try \{ return draw\(\); \} finally \{ busPlan\.m\.on = false; busPlan\.m\.v = null; scrAdd\('draw', performance\.now\(\) - t0\); \}/);
  assert.match(src, /pumpW\.onmessage = pumpTimed/);
  const f = src.match(/const scrT = [\s\S]*?const scrRead = [^\n]*\n/); assert.ok(f);
  const c = vm.createContext({ performance: { now: () => c.t }, t: 0 }); vm.runInContext(f[0] + '\nthis.add = scrAdd; this.read = scrRead;', c);
  assert.equal(c.read('draw'), null); c.t = 10; c.add('draw', 5); c.t = 600; c.add('draw', 5); c.t = 1100; c.add('draw', 10);
  const r = JSON.parse(JSON.stringify(c.read('draw'))); assert.ok(Math.abs(r.ms - 20 / 1090) < 1e-9 && Math.abs(r.n - 3000 / 1090) < 1e-9, JSON.stringify(r));
  c.t = 4000; assert.equal(c.read('draw').ms, 0);
});
