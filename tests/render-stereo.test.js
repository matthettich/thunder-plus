// STEREO PADS IN RENDERS: a pad with a stereo sample keeps its right channel in every offline render (song, stems, part).
// The data path (padRender, renderGetB, mkBuf) is cut out of index.html and run in a vm with stand-ins for the rest of the app;
// source guards keep renderOut and renderPart on it. Live check: tests/sweep/render-stereo.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };
const code = cut('function mkBuf(', 'async function pass(');

class FakeBuf { constructor(o){ this.length = o.length; this.numberOfChannels = o.numberOfChannels; this.sampleRate = o.sampleRate; this.ch = []; for (let i = 0; i < o.numberOfChannels; i++) this.ch.push(new Float32Array(o.length)); }
  copyToChannel(d, c){ this.ch[c].set(d); } }
function rig(o = {}) {
  const c = { AudioBuffer: FakeBuf, window: {}, idle: async () => {}, stKey: st => st ? 's' : '',
    loopSecs: () => 1, stretchData: d => d, renderAsync: o.renderAsync, renderVoice: o.renderVoice };
  vm.createContext(c); vm.runInContext(code, c); return c;
}

test('mkBuf: one channel for a mono pad, two for a stereo pad', () => {
  const c = rig(), L = Float32Array.of(1, 2, 3), R = Float32Array.of(-1, -2, -3);
  const m = c.mkBuf(L, 44100), s = c.mkBuf(L, 44100, R);
  assert.equal(m.numberOfChannels, 1); assert.equal(s.numberOfChannels, 2);
  assert.deepEqual([...s.ch[0]], [1, 2, 3]); assert.deepEqual([...s.ch[1]], [-1, -2, -3]);
});
test('mkBuf: a right channel of another length is cut or padded to the left one', () => {
  const c = rig(); const s = c.mkBuf(Float32Array.of(1, 2, 3), 44100, Float32Array.of(9, 8, 7, 6));
  assert.deepEqual([...s.ch[1]], [9, 8, 7]);
});
test('padRender: the Worker\'s data2 and renderVoice\'s .r both come through', async () => {
  const L = Float32Array.of(1, 2), R = Float32Array.of(3, 4);
  let c = rig({ renderAsync: async () => ({ data: L, data2: R }) }); let r = await c.padRender({}, 44100, 0);
  assert.equal(r.d, L); assert.equal(r.d2, R);
  const d = Float32Array.of(5, 6); d.r = Float32Array.of(7, 8);
  c = rig({ renderAsync: async () => { throw new Error('no worker'); }, renderVoice: () => d }); r = await c.padRender({}, 44100, 0);
  assert.equal(r.d, d); assert.equal(r.d2, d.r);
  c = rig({ renderAsync: async () => ({ data: L }) }); r = await c.padRender({}, 44100, 0); assert.equal(r.d2, null);
});
test('renderGetB: stereo pads reversed keep both channels; mono pads stay mono', () => {
  const c = rig(), pads = new Map([[0, Float32Array.of(1, 2, 3)], [1, Float32Array.of(4, 5, 6)]]), pads2 = new Map([[0, Float32Array.of(-1, -2, -3)]]);
  const g = c.renderGetB(pads, pads2, 44100);
  assert.equal(g(0, false).numberOfChannels, 2); assert.equal(g(1, false).numberOfChannels, 1);
  const r = g(0, true); assert.deepEqual([...r.ch[0]], [3, 2, 1]); assert.deepEqual([...r.ch[1]], [-3, -2, -1]);
  assert.equal(g(0, false), g(0, false), 'cached'); assert.equal(g(7, false), null);
});
test('source: renderOut and renderPart use padRender and renderGetB, never .data alone', () => {
  for (const [a, b] of [['async function renderOut(', 'async function renderPart('], ['async function renderPart(', '\nfunction ']]) {
    const f = cut(a, b); assert.ok(/padRender\(/.test(f) && /renderGetB\(/.test(f), a);
    assert.ok(!/renderAsync\(v, sr, hold, false\)\)\.data/.test(f), a + ' hands the player only .data');
  }
});
