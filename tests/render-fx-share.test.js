// RENDERS GO SILENT AFTER ABOUT 32 (15 Nov): the effect nodes of an offline render share one WebAssembly instance (tfxShared in TFX_SRC),
// and a render whose effects could not start stops with a message instead of writing a silent file. Cut out of index.html and run in a vm.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };

function rig() {
  let made = 0; const g = { WebAssembly: { Instance: function () { made++; this.exports = { tfx_new() {}, memory: {} }; } }, globalThis: null };
  g.globalThis = g; g.__tfxMod = {}; vm.createContext(g);
  vm.runInContext(cut('const tfxInst = ', 'const hotBad'), g); return { g, made: () => made };
}
test('tfxShared: nodes of one audio thread share instances, 32 handles each, a node reserves its chain length plus 2', () => {
  const r = rig(), x = [];
  for (let i = 0; i < 12; i++) x.push(vm.runInContext('tfxShared(4)', r.g));   // 12 nodes x 6 = 72 handles: 3 instances (5 per instance)
  assert.equal(r.made(), 3);
  assert.equal(x[0], x[4]); assert.notEqual(x[4], x[5]);
});
test('tfxShared: a chain of 30 gets an instance of its own, an empty chain still reserves', () => {
  const r = rig(); vm.runInContext('tfxShared(4)', r.g); vm.runInContext('tfxShared(30)', r.g); assert.equal(r.made(), 2);
  const r2 = rig(); for (let i = 0; i < 10; i++) vm.runInContext('tfxShared(0)', r2.g); assert.equal(r2.made(), 1); vm.runInContext('tfxShared(0)', r2.g); assert.equal(r2.made(), 2);
});
test('source: a render\'s effect nodes ask to share, live nodes do not; a render with a node that never started is refused', () => {
  const node = cut('function tfxNode(', 'const tfxErrAc');
  assert.ok(/shareX:isOfflineAc\(ac\)/.test(node), 'processorOptions.shareX only for offline contexts');
  assert.ok(/this\.x = op\.shareX \? tfxShared\(/.test(src), 'the processor takes the shared instance');
  assert.ok(/if \(isOfflineAc\(ac\)\) return;/.test(node), 'no "rendered without it" toast: the render is refused instead');
  const pass = cut('async function pass(', 'function wavN(');
  assert.ok(/if \(tfxErrAc\.has\(oc\)\) throw new Error\(RENDER_FX_FAIL\)/.test(pass), 'pass() throws when an effect node errored');
  assert.ok(/if \(wantFx && !ok && !NOWK\) throw new Error\(RENDER_FX_FAIL\)/.test(pass), 'and when the effects module did not load');
  assert.ok(/n\._wasmGiven && isOfflineAc\(ac\) && d\.value && d\.value\.wasm && d\.value\.wasm\.tfx === false\) tfxErrAc\.add\(ac\)/.test(src), 'a chains node that started without its effects wasm fails the render too');
  assert.ok(/Render failed: /.test(cut('async function renderOut(', 'async function renderPart(')), 'renderOut shows the error');
  assert.ok(/Render failed: /.test(cut('async function renderPart(', '\nfunction ')), 'renderPart too');
});
test('source: TFX_SRC (a template string) stays one piece: the first backtick after its start ends it, and it holds the sharing code', () => {
  const a = src.indexOf('const TFX_SRC = `'), end = src.indexOf('`', a + 18);
  assert.ok(a >= 0 && src.slice(end, end + 2) === '`;', 'a backtick inside TFX_SRC would end it early');
  const body = src.slice(a, end); assert.ok(/function tfxShared\(/.test(body) && /const tfxInst = /.test(body));
});
