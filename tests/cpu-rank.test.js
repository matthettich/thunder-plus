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
