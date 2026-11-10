// Stability batch 1 (10 Nov): the parts of the eight fixes that can be checked without a browser. The browser checks (PLAY / STOP,
// the player, the analyser) are in tests/sweep/stability-batch1.mjs; run that after a change to tStart, tStop, pumpRun or makePlayer.
// Like app-logic.test.js, the code under test is cut out of the real index.html and run in a vm.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
function cut(re, what) { const m = src.match(re); assert.ok(m, `index.html: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }

/* ───────── F8: MUT copies the sample objects, not their sound through base64 ───────── */
const VOICE_SRC = [
  /function smpToJSON\(S\)\{[\s\S]*?\nfunction smpB64\(d\)\{[\s\S]*?\n\}\n/, /function smpFromJSON\(j\)\{[\s\S]*?\nfunction smpUnB64\(b\)\{[\s\S]*?\n\}\n/,
  /function layerToJSON\(L, noSmp\)\{[^\n]*\n/, /function layerFromJSON\(j\)\{[^\n]*\n/, /function voiceToJSON\(v, noSmp\)\{[^\n]*\n/, /function voiceFromJSON\(j\)\{[^\n]*\n/,
  /function freshLayer\(o\)\{[\s\S]*?\n\}\n/, /function mkVoice\(name, layers, o\)\{[\s\S]*?\n\}\n/,
  /const varNoise = [^\n]*\n/, /const genN = [^\n]*\n/, /const genX = [^\n]*\n/, /function bkHash\(a, b, c\)\{[^\n]*\n/,
  /const MUT_LAYER = \{[\s\S]*?\n(?=function mutVoice)/, /function mutVoice\(base, seed, amt\)\{[\s\S]*?\n\}\n/].map((re, i) => cut(re, 'voice block ' + i)).join('\n');
function env(mutSrc) {
  const c = vm.createContext({ Math, Object, Array, Number, JSON, Float32Array, Int16Array, Uint8Array, String, Map, Set, Error,
    btoa: s => Buffer.from(s, 'binary').toString('base64'), atob: b => Buffer.from(b, 'base64').toString('binary'),
    clamp: (v, a, b) => Math.min(b, Math.max(a, v)), TYPE: new Proxy({}, { get: () => true }), smpId: () => 'id' });
  vm.runInContext('var _seed = 1000;\n' + mutSrc.replace(/^const /gm, 'var ').replace(/^function (\w+)/gm, 'var $1 = function $1'), c);
  return c;
}
const NEW = VOICE_SRC;
// the pre-fix line, for comparison: every sample layer through base64 (voiceToJSON(base, false))
const OLD_LINE = 'const v = voiceFromJSON(JSON.parse(JSON.stringify(voiceToJSON(base, false)))), r = (i, j) => varNoise(seed, i, j) * 2 - 1;';
const rng = seed => () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296; };
function kitVoice(c, onGrid) {
  const R = rng(7), data = n => Float32Array.from({ length: n }, (_, i) => onGrid ? Math.fround(Math.round((Math.sin(i / 9) * 0.7 + (R() - 0.5) * 0.2) * 32767) / 32767) : Math.sin(i / 9) * 0.7 + (R() - 0.5) * 0.2);
  const v = c.mkVoice('T', [{ type: 'sample', tune: 2, dec: 400, level: 0.8 }, { type: 'sine', dec: 200 }, { type: 'sample', a: 0.2, dec: 300 }]);
  v.layers[0].sample = { sr: 44100, name: 'A', id: 'sa', data: data(4410) };
  v.layers[2].sample = { sr: 22050, name: 'B', id: 'sb', data: data(2205), data2: data(2205) };
  return v;
}

test('F8: mutVoice shares the base sample objects (no base64 round trip) and does not touch the base', () => {
  assert.ok(VOICE_SRC.includes(OLD_LINE) === false, 'mutVoice no longer base64s the samples');
  const c = env(NEW), base = kitVoice(c, false), lens = base.layers.map(L => L.sample ? [L.sample.data.length, L.sample.data2 ? L.sample.data2.length : 0] : null), snap = JSON.stringify(c.voiceToJSON(base, false));
  for (const seed of [1, 5, 17, 40]) for (const amt of [0.1, 0.5, 1]) {
    const v = c.mutVoice(base, seed, amt);
    base.layers.forEach((L, i) => { assert.equal(v.layers[i].sample, L.sample, `layer ${i}: the same sample object`); });
    assert.equal(v.layers.length, 8); assert.notEqual(v.layers[0], base.layers[0]); assert.equal(v.layers[1].sample, null, 'a layer without a sample stays without');
    assert.deepEqual(base.layers.map(L => L.sample ? [L.sample.data.length, L.sample.data2 ? L.sample.data2.length : 0] : null), lens, 'no sample changed length');
  }
  assert.equal(JSON.stringify(c.voiceToJSON(base, false)), snap, 'the base voice is unchanged by MUT');
});

test('F8: old and new mutVoice give the same voice: identical on the 16-bit grid, within half a step otherwise', () => {
  assert.ok(src.includes(OLD_LINE.replace('voiceToJSON(base, false)', 'voiceToJSON(base, true)')), 'new line present');
  const newSrc = NEW, oldSrc = NEW.replace(OLD_LINE.replace('voiceToJSON(base, false)', 'voiceToJSON(base, true)'), OLD_LINE).replace(/\n\s*v\.layers\.forEach\(\(L, i\) => \{ const s = base\.layers\[i\][^\n]*\n/, '\n');
  assert.notEqual(oldSrc, newSrc, 'the old line was put back for the comparison');
  assert.ok(oldSrc.includes(OLD_LINE), 'the comparison is the pre-fix line');
  const cn = env(newSrc), co = env(oldSrc);
  for (const onGrid of [true, false]) {
    const bn = kitVoice(cn, onGrid), bo = kitVoice(co, onGrid); let worst = 0;
    for (const seed of [1, 2, 3, 9, 25]) for (const amt of [0.05, 0.4, 1]) {
      const n = cn.mutVoice(bn, seed, amt), o = co.mutVoice(bo, seed, amt);
      n.layers.forEach((L, i) => {
        const a = L.sample, b = o.layers[i].sample; assert.equal(!!a, !!b, `layer ${i}`);
        if (!a) return;
        for (const k of ['data', 'data2']) { if (!a[k]) { assert.ok(!b[k]); continue; } assert.equal(a[k].length, b[k].length); for (let j = 0; j < a[k].length; j++) worst = Math.max(worst, Math.abs(a[k][j] - b[k][j])); }
        assert.equal(a.sr, b.sr); assert.equal(a.name, b.name);
      });
      const strip = v => JSON.stringify(cn.voiceToJSON(v, true)); const strip2 = v => JSON.stringify(co.voiceToJSON(v, true));
      assert.equal(strip(n), strip2(o), 'every other field is the same');
    }
    if (onGrid) assert.equal(worst, 0, 'data on the 16-bit grid: bit-identical'); else assert.ok(worst < 2e-5, 'a float sample moves by less than half a 16-bit step, got ' + worst);
  }
});

test('F8: the other users of a shared sample only read it (render, vint cache)', () => {
  // renderVoice copies a layer's sample before it swaps in the right channel; the sample layer reads S.data and nothing writes into it
  assert.match(src, /Object\.assign\(\{\}, L\.sample, \{data:L\.sample\.data2, data2:null, _vint:null\}\)/, 'renderVoice copies the sample before swapping channels');
  assert.match(src, /PW\.w\.postMessage\(\{op:'render', id, v, sr, hold, stages:!!stages, bpm:songBpm\(\), waves:WV\.doc\}\);/, 'the render Worker is sent the voice with no transfer list');
});
