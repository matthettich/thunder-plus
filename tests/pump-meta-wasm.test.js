// PUMP: pump/pump-meta.js and pump/pump.wasm must agree on every plugin's parameter count (build 635, 16 Nov 2026). The app sends each plugin a value
// list as long as the meta's parameter table; pumpRun clamps it to the wasm's own pump_nparams, but a skewed pair would silently drop parameters, so
// the pair is checked here. (The wasm has no build stamp to compare with the meta's sha1; the parameter counts are what it can report.)
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const fs = require('fs'), path = require('path'), vm = require('vm');
const dir = path.resolve(__dirname, '..', 'pump');
test('pump-meta.js parameter counts equal pump.wasm pump_nparams, for every type', () => {
  const g = {}; vm.runInNewContext(fs.readFileSync(path.join(dir, 'pump-meta.js'), 'utf8'), g);
  const M = g.PUMP_META; assert.ok(M, 'PUMP_META');
  const z = () => 0, mod = new WebAssembly.Module(fs.readFileSync(path.join(dir, 'pump.wasm')));
  const x = new WebAssembly.Instance(mod, {wasi_snapshot_preview1: {fd_close: z, fd_seek: z, fd_write: z, proc_exit: z}}).exports;
  if (x._initialize) x._initialize();
  assert.equal(x.pump_types(), M.types.length, 'plugin count');
  M.types.forEach((t, i) => assert.equal(M.fx[t].params.length, x.pump_nparams(i), t + ': meta parameters vs wasm pump_nparams'));
});
