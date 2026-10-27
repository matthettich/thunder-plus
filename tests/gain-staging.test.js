// Gain staging (27 Oct; docs/GAIN-STAGING.md): TRIM before …chains, VOL after, the master OUT fader after the limiter, PRE
// bus sends, and the CHN crossfade. The code under test is cut out of index.html at run time (as in app-logic.test.js), so a
// change that undoes any of it fails here. The live audio itself was checked in headless Chromium (see HANDOVER.md).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');

function cut(rel, re, what) { const m = H.read(rel).match(re); assert.ok(m, `${rel}: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
function run(src, extra) { const c = Object.assign({ Math, JSON, String, Number, Array, Object, Set, Map, console: { warn() {}, log() {} } }, extra); vm.createContext(c); vm.runInContext(src, c); return c; }
const near = (a, b, e, msg) => assert.ok(Math.abs(a - b) <= e, `${msg}: ${a} is not within ${e} of ${b}`);
const HELPERS = () => cut('index.html', /const TRIM_0 = 128, TRIM_DB = 18;[\s\S]*?\nconst trimS = [^\n]*\n/, 'the TRIM helpers');

/* ───────── TRIM's scale ───────── */
test('TRIM: 80 is 0 dB, 00 −18 dB, FF +18 dB, even dB steps; shown signed', () => {
  const c = run(HELPERS() + '\nthis.api = {trimDb, trimGain, trimS, TRIM_0};');
  const { trimDb, trimGain, trimS, TRIM_0 } = c.api;
  assert.equal(TRIM_0, 128);
  assert.equal(trimDb(128), 0); near(trimDb(0), -18, 1e-9, '00'); near(trimDb(255), 18, 1e-9, 'FF');
  near(trimDb(170), 5.95, 0.01, '+16 hex is about +2.3 dB, 42 steps about +6 dB');
  near(trimDb(129) - trimDb(128), 18 / 127, 1e-12, 'a step above 80'); near(trimDb(128) - trimDb(127), 18 / 128, 1e-12, 'a step below 80');
  near(trimGain(128), 1, 1e-12, 'unity'); near(20 * Math.log10(trimGain(0)), -18, 1e-9, 'gain at 00');
  assert.equal(trimDb(-5), -18, 'clamped below'); assert.equal(trimDb(999), 18, 'clamped above'); assert.equal(trimDb(undefined), -18, 'nonsense reads as 00');
  assert.deepEqual([trimS(0), trimS(128), trimS(255), trimS(170)], ['-18.0 dB', '0.0 dB', '+18.0 dB', '+6.0 dB']);
});

/* ───────── applyMix: TRIM on the input, VOL on the fader, the PRE tap keeps mute and solo ───────── */
test('applyMix: TRIM on inp, VOL (with mute/solo) on g, pan on p, PRE tap 1 or 0 with mute/solo; renders set values, live glides', () => {
  const blk = cut('index.html', /  function applyMix\(filter\)\{[\s\S]*?\n  \}\n/, 'applyMix');
  const P = () => { const p = { value: 1, calls: [] }; p.setTargetAtTime = (v, t, k) => p.calls.push(['target', v, t, k]); return p; };
  const bus = () => ({ inp: { gain: P() }, g: { gain: P() }, p: { pan: P() }, scs: { gain: P() }, pre: { gain: P() } });
  for (const live of [false, true]) {
    const buses = [bus(), bus(), bus()], S = { mix: [{ vol: 224, trim: 170, pan: 128, mute: false }, { vol: 224, trim: 0, pan: 0, mute: true }] };
    const c = run(HELPERS() + '\n' + blk + '\nthis.applyMix = applyMix;', {
      S, NT: 2, buses, live, ac: { currentTime: 3 }, clamp: (v, a, b) => Math.max(a, Math.min(b, v)),
      trackGain: v => v / 224, audible: t => !S.mix[t].mute });
    c.applyMix();
    const val = p => live ? p.calls[p.calls.length - 1][1] : p.value;
    near(20 * Math.log10(val(buses[0].inp.gain)), 5.95, 0.01, 'track 1 TRIM on its input');
    assert.equal(val(buses[0].g.gain), 1, 'track 1 VOL on its fader'); assert.equal(val(buses[0].pre.gain), 1, 'track 1 PRE tap open');
    near(20 * Math.log10(val(buses[1].inp.gain)), -18, 1e-9, 'track 2 TRIM (muted tracks keep their trim)');
    assert.equal(val(buses[1].g.gain), 0, 'muted: fader closed'); assert.equal(val(buses[1].pre.gain), 0, 'muted: the PRE tap is closed too (owner: mute silences a PRE send)');
    assert.equal(val(buses[1].p.pan), -1, 'pan (00 is hard left)');
    assert.equal(buses[2].g.gain.value, 1, 'the preview bus: unity, set at once');
    if (live) assert.deepEqual(buses[0].inp.gain.calls[0].slice(2), [3, 0.012], 'live: glides from now (no zipper on a TRIM drag)');
    c.applyMix(t => t === 1);   // a render's filter: only track 2 is heard
    assert.equal(val(buses[0].pre.gain), 0, 'filtered out: PRE closed'); assert.equal(val(buses[1].pre.gain), 1, 'the filter decides, as for the fader');
  }
});

/* ───────── CHN SWITCH: the chain crossfades with the dry path ───────── */
function chnRig(live) {
  const blk = cut('index.html', /  const XF = 0\.02;\n  function ramp\(prm, to\)\{[\s\S]*?\n  function chnOut\(b\)\{[\s\S]*?\n  \}\n/, 'ramp, chnIn and chnOut');
  const ac = { currentTime: 10 }, timers = [], conns = [];
  const param = v0 => { const p = { value: v0, ev: [] }; p.cancelScheduledValues = t => p.ev.push(['cancel', t]); p.setValueAtTime = (v, t) => p.ev.push(['set', v, t]); p.linearRampToValueAtTime = (v, t) => { p.ev.push(['ramp', v, t]); p.value = v; }; return p; };
  const node = name => ({ name, gain: param(1), connect(to) { conns.push([name, to.name]); }, disconnect(to) { conns.push([name, '×' + (to ? to.name : 'all')]); } });
  ac.createGain = () => node('gain');
  const c = run(blk + '\nthis.api = {chnIn, chnOut, ramp};', { ac, live, setTimeout: (f, ms) => timers.push({ f, ms }), tapSync() {} });
  const b = { inp: node('inp'), dry: node('dry'), ins: node('ins'), chn: null, wet: null, _fade: new Set() };
  const n = node('chn'); n._live = false; n.anIn = node('anIn'); n.an = node('an'); const sent = []; n._send = m => sent.push(m.op);
  return { c, ac, b, n, timers, conns, sent };
}
test('CHN switch: a new chain fades in once its engine is ready, anchored at now; the dry path fades out with it', () => {
  const { c, ac, b, n, conns } = chnRig(true);
  b.chn = n; c.api.chnIn(b, b.ins);
  assert.equal(b.wet.gain.value, 0, 'the chain starts silent'); assert.equal(b.dry.gain.ev.length, 0, 'the dry path is untouched until the chain is ready');
  assert.equal(typeof n._onReady, 'function', 'waits for the engine (before that it plays silence)');
  assert.ok(conns.some(([a, z]) => a === 'inp' && z === 'chn') && conns.some(([a, z]) => a === 'chn' && z === 'gain'), 'inp → chain → wet');
  ac.currentTime = 10.5; n._onReady();
  assert.deepEqual(b.wet.gain.ev, [['cancel', 10.5], ['set', 0, 10.5], ['ramp', 1, 10.52]], 'wet: anchored at now, then 20 ms up (a ramp starts at the previous event: it came in at ~60% at once)');
  assert.deepEqual(b.dry.gain.ev, [['cancel', 10.5], ['set', 1, 10.5], ['ramp', 0, 10.52]], 'dry: 20 ms down');
});
test('CHN switch: off fades the chain out over 20 ms, then lets its node go; a render switches at once', () => {
  { const { c, ac, b, n, timers, conns, sent } = chnRig(true);
    b.chn = n; c.api.chnIn(b, b.ins); n._onReady(); b.dry.gain.value = 0; ac.currentTime = 20;
    const wet = b.wet; c.api.chnOut(b);
    assert.equal(b.chn, null, 'the track lets go of the chain at once (taps, sends and buses follow)'); assert.equal(b.wet, null);
    assert.deepEqual(b.dry.gain.ev.slice(-3), [['cancel', 20], ['set', 0, 20], ['ramp', 1, 20.02]], 'dry back up');
    assert.deepEqual(wet.gain.ev.slice(-1), [['ramp', 0, 20.02]], 'the chain down'); assert.ok(b._fade.has(wet), 'still fading');
    assert.equal(n._onReady, null); assert.equal(sent.length, 0, 'not disposed yet: its tail fades');
    assert.equal(timers.length, 1); assert.ok(timers[0].ms >= 20 && timers[0].ms <= 100, 'dropped just after the fade (' + timers[0].ms + ' ms)');
    timers[0].f();
    assert.deepEqual(sent, ['dispose']); assert.ok(!b._fade.has(wet)); assert.ok(conns.some(([a, z]) => a === 'inp' && z === '×chn'), 'the input lets go of it'); }
  { const { c, b, n, timers, sent } = chnRig(false);
    b.chn = n; c.api.chnIn(b, b.ins);
    assert.equal(b.wet.gain.value, 1, 'render: the chain is in from the first sample'); assert.equal(b.dry.gain.value, 0);
    c.api.chnOut(b); assert.deepEqual(sent, ['dispose'], 'render: gone at once'); assert.equal(timers.length, 0); assert.equal(b.dry.gain.value, 1); }
});

/* ───────── PRE SENDS: busPlan learns them from the Track In ───────── */
test('PRE sends: busPlan lists the sources whose Track In says "Before its fader", and its cache notices the switch', () => {
  const blk = cut('index.html', /let busPlanH = null, busPlanVal = null;\nfunction busPlan\(\)\{[\s\S]*?\n  return P;\n\}\n/, 'busPlan');
  const S = { chainOn: [true, true, true, false], chainPatch: { format: 'chains.patch', cables: [], modules: [
    { id: 'a', type: 'trackIn', params: { track: 1, pre: 1 } }, { id: 'b', type: 'trackIn', params: { track: 2 } },
    { id: 'c', type: 'chainOut', bus: 1, params: { track: 3 } }, { id: 'd', type: 'chainOut', params: { track: 2 } }] } };
  S.chainPatch.cables = [{ from: ['a', 'out'], to: ['c', 'in'] }, { from: ['b', 'out'], to: ['c', 'in'] }, { from: ['b', 'out'], to: ['d', 'in'] }];
  const outs = () => new Set(S.chainPatch.modules.filter(m => m.type === 'chainOut').map(m => m.params.track - 1));
  const c = run(blk + '\nthis.busPlan = busPlan;', { S, NT: 4, int: (v, d) => Number.isFinite(+v) ? Math.round(+v) : d, chainsFeed: t => !!S.chainOn[t] && outs().has(t) });
  let P = c.busPlan();
  assert.deepEqual([...P.feeds.get(2)], [0, 1], 'tracks 1 and 2 feed the bus on 3');
  assert.deepEqual([...P.pre], [0], 'track 1 is a PRE send; track 2 (no pre) is after its fader');
  assert.deepEqual([...P.pure], [0], 'track 1 plays only through the bus; track 2 keeps its own out (a send)');
  S.chainPatch.modules[0].params.pre = 0; P = c.busPlan(); assert.deepEqual([...P.pre], [], 'switched back: the cache saw it');
  S.chainPatch.modules[1].params.pre = 1; P = c.busPlan(); assert.deepEqual([...P.pre], [1], 'a send (with its own out) can be PRE too');
  S.chainOn[1] = false; P = c.busPlan(); assert.deepEqual([...P.pre], [], 'only tracks that feed a bus count');
});

/* ───────── the routing, the master OUT and the UI, as source guards ───────── */
test('sources: the per-track graph, PRE feeds, master OUT live and in renders, the Mixer rows, the song file, the editor', () => {
  const src = H.read('index.html');
  // per track: inp (TRIM) → dry / chain → wet → [FX] → ins → g → p, ins → pre
  assert.match(src, /inp\.connect\(dry\); dry\.connect\(ins\); ins\.connect\(g\); g\.connect\(p\); p\.connect\(out\); ins\.connect\(pre\);/, 'the per-track graph');
  assert.match(src, /const mid = b\.fx \|\| b\.ins;/, 'track FX sit between the chain and the fader');
  assert.doesNotMatch(src, /b\.inp\.disconnect\(\); b\.inp\.connect\(b\.g\)/, 'no more wholesale input rewiring (it dropped the taps and meters)');
  assert.match(src, /if \(P\.pre\.has\(t\)\)\{ if \(!q\.splP\)\{ q\.splP = ac\.createChannelSplitter\(2\); q\.pre\.connect\(q\.splP\); \}/, 'PRE sources feed their bus from the pre tap');
  assert.match(src, /src\.map\(t => t \+ \(P\.pre\.has\(t\) \? 'p' : ''\)\)/, 'a PRE switch rebuilds the bus feeds');
  assert.match(src, /if \(name === 'pre' && rt\.pl\.busSync\)\{ rt\.pl\.busSync\(\);/, 'the editor\'s switch reaches the live routing');
  assert.match(src, /if \(n\._onReady\)\{ const f = n\._onReady; n\._onReady = null;/, 'the chain node says when to fade in');
  // master OUT: after the limiter, live and in renders
  assert.match(src, /mo = ctx\.createGain\(\), lim = safeLim\(ctx, mo\);[^\n]*mo\.connect\(master\);/, 'live: limiter → OUT → device');
  assert.match(src, /const dest = S\.limiter && !mLim \? rt\.lim : rt\.mo;/, 'live: mastering\'s limiter (or none) → OUT');
  assert.match(src, /const mo = oc\.createGain\(\); mo\.gain\.value = curve\(S\.mout == null \? 255 : S\.mout\); mo\.connect\(oc\.destination\);/, 'renders: OUT last');
  assert.equal((src.match(/async function pass\([\s\S]*?\n\}\n/)[0].match(/connect\(oc\.destination\)/g) || []).length, 1, 'renders: only OUT goes to the output');
  assert.match(src, /function moApply\(\)\{ if \(rt && rt\.mo && ctx\) rt\.mo\.gain\.setTargetAtTime\(/, 'OUT glides, without rtRoute\'s rewire');
  assert.match(src, /node = k => k === 'm' \? rt\.mo :/, 'the bottom panel\'s master meter reads after OUT');
  assert.match(src, /const stGet = \(\) => dkMst\(\) \? \(S\.mout == null \? 255 : S\.mout\) : S\.mix\[dkTrack\(\)\]\.vol;/, 'the bottom panel: the master strip is OUT, a track\'s is VOL');
  // the Mixer: TRIM is the first row, OUT the master row's third cell, the bars are VOL and OUT
  assert.match(src, /\{t:'TRM ', cls:'lb'/, 'the TRIM row'); assert.match(src, /if \(c\.y === 0\) m\.trim = clamp\(/, 'EDIT+arrows on the first row move TRIM');
  assert.match(src, /else if \(c\.y === 0\) S\.mix\[c\.x\]\.trim = v;/, 'typed hex on the first row is TRIM');
  assert.match(src, /if \(c\.y === 0\) m\.trim = f\.trim;/, 'Delete resets TRIM');
  assert.match(src, /c\.mx = clamp\(\(c\.mx \|\| 0\) \+ dx, 0, 2\)/, 'MASTER, MASTERING, OUT');
  assert.match(src, /S\.mix\[D\.t\]\.vol = v;/, 'the meter\'s bar is still VOL');
  assert.match(src, /function mixOutBar\(\)\{/, 'the OUT bar on the master meter');
  assert.match(src, /e\.target\.closest\('\.mfbar:not\(\.mobar\)'\)/, 'the track bars leave the OUT bar alone');
  assert.match(src, /\{l:'MASTER OUT', get:\(\) => S\.mout == null \? 255 : S\.mout,/, 'Project → MASTER OUT');
  // the song file
  assert.match(src, /const freshMix = \(\) => \(\{vol:224, trim:TRIM_0,/); assert.match(src, /master:255, mout:255, chainPatch:null,/);
  assert.match(src, /m\.trim = clamp\(int\(o\.trim, TRIM_0\), 0, 255\);/, 'older songs: TRIM 0 dB'); assert.match(src, /S\.mout = clamp\(int\(j\.mout, 255\), 0, 255\);/, 'older songs: OUT 0 dB');
  assert.match(src, /b\.vol = a\.vol; b\.trim = a\.trim;/, 'Ctrl+D copies TRIM');
  // …chains: Track In's Bus send, in the editor and the engine (cache-buster moved with it)
  assert.match(H.read('chains/index.html'), /params:\{ track:\[1,1,CHAN_COUNT,1\], pre:\[0,0,1,1\] \},[\s\S]{0,400}pre:\{l:'Bus send', c:opts\(\['After its fader','Before its fader'\]\)\}/);
  assert.match(H.read('chains/chains-worklet.js'), /params:\{ track:\[1,1,CHAN_COUNT,1\], pre:\[0,0,1,1\] \}/);
  for (const f of ['index.html', 'chains/index.html', 'sw.js']) assert.match(H.read(f), /chains-worklet\.js\?v=39/, f + ': chains-worklet v=39');
});
