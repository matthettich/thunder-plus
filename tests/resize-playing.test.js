// WINDOW RESIZE DURING PLAYBACK (24 Nov 2026, build 676): the refit is throttled during a drag and the stage is hidden until it ends.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = re => { const m = src.match(re); assert.ok(m, 'not found: ' + re); return m[0]; };

test('source: the resize marks a burst first, the stage is hidden while it lasts, the playhead does not redraw for the size', () => {
  assert.match(src, /addEventListener\('resize', rzMark\);[^\n]*\naddEventListener\('resize', \(\) => scheduleRefit\(false\)\);/);
  assert.match(src, /body\.rszing \.tstage\{visibility:hidden\}/);
  assert.match(src, /const plStamp = ns => \[[^\]]*ns \? 0 : vpW, ns \? 0 : vpH,/);
  assert.match(src, /plBase = \{t:now, sig:plStamp\(\), sigN:plStamp\(true\)\}/);
  assert.match(src, /if \(rzOn \? plStamp\(true\) !== plBase\.sigN : plStamp\(\) !== plBase\.sig\) return false;/);
  assert.match(src, /let vpW = innerWidth, vpH = innerHeight;/);
});
test('source: a grid scrolled sideways has no room beside its text (the SIGNAL chain over the master Pattern view), and its scroll re-places the stage', () => {
  assert.match(src, /const ovf = !asg && grid\.scrollLeft > 1;\n  return \{right, left, w:sr\.width - left - 14, top:gr\.top - sr\.top, h:gr\.height, ovf\};/);
  assert.match(src, /h >= 150 && !R\.ovf; box\.hidden = !show;/);
  assert.match(src, /grid\.addEventListener\('scroll', \(\) => \{ const o = grid\.scrollLeft > 1; if \(raf \|\| o === was\) return; was = o; raf = requestAnimationFrame\(\(\) => \{ raf = 0; stageLayout\(\); \}\); \}, \{passive:true\}\);/);
});
// scheduleRefit in a sandbox with a fake clock
function rig() {
  const c = { t: 1000, raf: [], timers: [], log: [], cls: new Set(), dkDragging: false, view: 'tracker', refitSize: '', scr: { clientWidth: 900, clientHeight: 600 }, document: { body: { classList: { add: x => c.cls.add(x), remove: x => c.cls.delete(x) } } },
    performance: { now: () => c.t }, requestAnimationFrame: f => (c.raf.push(f), c.raf.length), setTimeout: (f, ms) => { const T = { f, at: c.t + ms, id: ++c.tid }; c.timers.push(T); return T.id; }, clearTimeout: n => { const T = c.timers.find(x => x.id === n); if (T) T.f = null; }, tid: 0,
    measure() {}, arrLayout: f => { c.log.push(f ? 'layout!' : 'layout'); return true; }, fit() {}, draw() {}, dkDragHold: () => false };
  vm.createContext(c);
  vm.runInContext('let refitRaf = 0, refitForce = false, refitSize = "";\n' + cut(/const RZ_GAP = 100[\s\S]*?\naddEventListener\('resize', rzMark\);/).replace(/\naddEventListener\('resize', rzMark\);$/, '') + '\nthis.mark = rzMark; this.sched = scheduleRefit; this.on = () => rzOn;', c);
  c.frame = () => { const q = c.raf.splice(0); q.forEach(f => f()); }; c.tick = ms => { c.t += ms; for (const T of c.timers.splice(0)) if (T.f && T.at <= c.t) T.f(); else if (T.f) c.timers.push(T); };
  return c;
}
test('a drag: the first refit runs at once, the next ones wait RZ_GAP, one forced refit RZ_END ms after the last event; the class is on only meanwhile', () => {
  const c = rig(); let n = 0;
  for (let i = 0; i < 30; i++) { c.mark(); c.sched(false); c.frame(); n = c.log.length; c.tick(16); }   // 30 events 16 ms apart = 480 ms
  assert.ok(c.cls.has('rszing') && c.on());
  assert.ok(n >= 3 && n <= 6, 'refits during a 480 ms drag: ' + n);
  c.tick(300); c.frame();
  assert.ok(!c.cls.has('rszing') && !c.on());
  assert.equal(c.log[c.log.length - 1], 'layout!', 'the last refit is forced');
});
test('no drag: a lone resize (a fold) refits at once and the class goes after RZ_END', () => {
  const c = rig(); c.mark(); c.sched(false); c.frame(); assert.deepEqual(c.log, ['layout']); c.tick(200); c.frame(); assert.ok(!c.cls.has('rszing'));
});
test('outside a resize nothing is throttled: scheduleRefit(false) from an observer runs in the next frame', () => {
  const c = rig(); c.sched(false); c.frame(); c.sched(false); c.frame(); assert.deepEqual(c.log, ['layout', 'layout']);
});
