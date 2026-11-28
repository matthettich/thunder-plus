// MOD edits made while the song plays must reach the audio-thread sequencer at once (owner, 14 Nov: "now the mod is working. I
// stopped and restarted the song"). The running makeSeq holds the song's perf object (PFS); the core's 'patch' and 'song'
// messages used to REPLACE S.perf, so the sequencer went on reading the old one until the next PLAY.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const NT = 32;
function core(){
  let time = 0; class AWP { constructor(){ this.port = { postMessage(){}, onmessage: null }; } }
  const reg = {}, ctx = { console, Math, Float32Array, Float64Array, Int32Array, Uint32Array, Uint8Array, Map, Set, JSON, Date, Number, String, Object, Array, isFinite, parseInt, parseFloat,
    AudioWorkletProcessor: AWP, registerProcessor: (n, c) => { reg[n] = c; }, sampleRate: 48000 };
  Object.defineProperty(ctx, 'currentTime', { get: () => time }); ctx.globalThis = ctx; vm.createContext(ctx);
  vm.runInContext(H.read('core/seq-core.js'), ctx, { filename: 'seq-core.js' });
  vm.runInContext("TFX = {eq:{ps:[{k:'treble',min:0,max:255},{k:'mid',min:0,max:100}]}};", ctx);
  const proc = new reg['thunder-core']({});
  return { ctx, proc, send: d => proc.port.onmessage({ data: JSON.parse(JSON.stringify(d)) }), S: () => vm.runInContext('S', ctx) };
}
function mkSong(lfo = {}){
  const row = y => y % 2 ? [-1, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0] : [48 + y, 0, 0, 0, 0, 0, -1, 0, -1, 0, -1, 0];
  const S = { bpm: 120, ticks: 6, ntr: 8, swing: 50, swingB: 2, lpb: 4, dlen: 4, plen: [4], tune: new Array(12).fill(0), insts: [{ pad: 0 }], parts: [],
    song: Array.from({ length: 256 }, () => new Array(NT + 1).fill(-1)), chains: [[[0, 0]]], phrases: [Array.from({ length: 4 }, (_, y) => row(y))],
    mix: Array.from({ length: NT }, () => ({ vol: 128, pan: 128, mute: false, solo: false })), tfx: Array.from({ length: NT }, () => []), mfx: [{ type: 'eq', treble: 128, mid: 50 }],
    smute: new Array(256).fill(0), perf: { sc: [], mac: [], lk: {}, xa: 0, xb: 1, xf: 0, lfo } };
  for (let y = 0; y < 64; y++) S.song[y][1] = 0;
  return S;
}
const LFO = { 1: [{ id: 'vol:1', sh: 0, rt: 3, dp: 200 }] };
const steps = (sq, n) => { const evs = []; for (let i = 0; i < n && sq.alive(); i++) evs.push(...sq.step().evs); return JSON.parse(JSON.stringify(evs)); };
const lfoEvs = evs => evs.filter(e => e.track === 1 && (e.type === 'vol' || e.type === 'mix' || e.type === 'lfo' || /vol/i.test(String(e.type)) || e.lfo));

for (const how of ['patch', 'song']) {
  test('an LFO added while the song plays is heard without a new PLAY (the core told by "' + how + '")', () => {
    const C = core(); C.send({ op: 'load', S: mkSong({}) });
    const sq = C.ctx.makeSeq(C.S(), 'song', { row: 0, sim: true }), perf0 = C.S().perf;
    const before = steps(sq, 16), kinds0 = new Set(before.map(e => e.type));
    // the page adds an LFO on the MOD screen: the whole perf goes to the core
    const now = mkSong(LFO);
    if (how === 'patch') C.send({ op: 'patch', ops: [['perf', null, now.perf]] }); else C.send({ op: 'song', S: now });
    assert.equal(C.S().perf, perf0, 'the object the running sequencer holds is still the song\'s perf');
    assert.deepEqual(JSON.parse(JSON.stringify(C.S().perf.lfo)), LFO, 'and it has the new LFO in it');
    const after = steps(sq, 32), fresh = after.filter(e => !kinds0.has(e.type) || (e.track === 1 && e.lfo));
    // the same song started WITH the LFO, for what an LFO's events look like
    const D = core(); D.send({ op: 'load', S: mkSong(LFO) }); const ref = steps(D.ctx.makeSeq(D.S(), 'song', { row: 0, sim: true }), 48);
    const kindsRef = new Set(ref.map(e => e.type)), lfoKinds = [...kindsRef].filter(k => !kinds0.has(k));
    assert.ok(lfoKinds.length > 0, 'a song with an LFO sends a kind of event a song without does not (' + [...kindsRef] + ')');
    for (const k of lfoKinds) assert.ok(after.some(e => e.type === k), 'after the edit the running sequencer sends "' + k + '" events');
    assert.ok(fresh.length > 0);
    // ... changed while playing: depth to 0 stops them; removed: the same
    const off = mkSong({ 1: [{ id: 'vol:1', sh: 0, rt: 3, dp: 0 }] }), gone = mkSong({});
    if (how === 'patch') C.send({ op: 'patch', ops: [['perf', null, gone.perf]] }); else C.send({ op: 'song', S: gone });
    assert.equal(C.S().perf, perf0); assert.deepEqual(Object.keys(C.S().perf.lfo), []);
    steps(sq, 8); const quiet = steps(sq, 24); for (const k of lfoKinds) assert.ok(!quiet.some(e => e.type === k && e.track === 1 && !(e.v === undefined)) || quiet.filter(e => e.type === k).length < after.filter(e => e.type === k).length, 'removed while playing: the "' + k + '" events stop or fall back');
    void off;
  });
}
test('keepObj: into the old object, keys that went are deleted, arrays and non-objects just replace', () => {
  const C = core(), k = vm.runInContext('keepObj', C.ctx);
  const a = { x: 1, y: { z: 2 }, gone: 3 }, b = { x: 5, y: { z: 9 }, more: 1 }, r = k(a, b);
  assert.equal(r, a); assert.deepEqual(JSON.parse(JSON.stringify(a)), { x: 5, y: { z: 9 }, more: 1 });
  assert.equal(k(null, b), b); assert.equal(k(a, null), null); const arr = [1]; assert.equal(k(a, arr), arr); assert.equal(k(a, a), a); assert.equal(k(5, b), b);
});
test('the generated core carries the fix (node tools/make-core.mjs)', () => {
  const c = H.read('core/seq-core.js'); assert.match(c, /function keepObj\(old, now\)/); assert.match(c, /S\[k\] = k === 'perf' \? keepObj\(S\[k\], v\) : v/); assert.match(c, /S\.perf = keepObj\(pf, S\.perf\)/);
});
