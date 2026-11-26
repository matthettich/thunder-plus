// PUMP: the web app's pump.wasm and pump-meta.js carry exactly the 25 effect types, in this order (build 643, 17 Nov 2026).
// The order is load-bearing: a pump effect's slot is saved by type name, but `pt` (the wasm's type index) is PUMP_TYPES.indexOf(type),
// and the chains editor block, the FX picker and the telemetry offsets are built from the same list.
// FLEX (pump's 26th plugin, commit e7c55dc) is NOT in the web build: pump's wasm/build.sh and wasm/pump_meta.cpp do not list it, and the owner
// decided (17 Nov) "FLX / FLY can stay hidden": the web app already has its own FLX / FLY. If a later pump rebuild adds it, this fails
// and the picker, the lists and this pin have to be looked at on purpose.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const fs = require('fs'), path = require('path'), vm = require('vm');
const ROOT = path.resolve(__dirname, '..'), dir = path.join(ROOT, 'pump');
const TYPES = ['eq', 'comp', 'limit', 'calm', 'tone', 'band', 'wide', 'heat', 'meter', 'tube', 'snap', 'gate', 'echo', 'spring', 'verb', 'grain', 'vox', 'tune', 'crush', 'swirl', 'stutter', 'chop', 'twang', 'master', 'strip'];

test('pump meta and wasm: 25 types in the saved-song order, no FLEX', () => {
  const g = {}; vm.runInNewContext(fs.readFileSync(path.join(dir, 'pump-meta.js'), 'utf8'), g);
  assert.deepEqual([...g.PUMP_META.types], TYPES);
  assert.ok(!('flex' in g.PUMP_META.fx), 'no flex in the meta');
  const z = () => 0, x = new WebAssembly.Instance(new WebAssembly.Module(fs.readFileSync(path.join(dir, 'pump.wasm'))), { wasi_snapshot_preview1: { fd_close: z, fd_seek: z, fd_write: z, proc_exit: z } }).exports;
  x._initialize(); assert.equal(x.pump_types(), 25, 'the wasm has 25 types');
  assert.equal(x.pump_nparams(25), 0, 'no type 25 in the wasm');
});

test('pump types: the effect lists are built from the meta, so the picker offers those 25 and nothing named flex', () => {
  const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
  assert.ok(html.includes('const PUMP_TYPES = PM ? PM.types.slice() : [];'), 'PUMP_TYPES is the meta list');
  assert.ok(html.includes('...PUMP_TYPES.map(PUMP_KEY)'), 'the picker list maps PUMP_TYPES');
  assert.ok(!/['"]pflex['"]/.test(html) && !/PUMP_KEY\(['"]flex['"]\)/.test(html), 'no pump flex key anywhere in the page');
  const chains = fs.readFileSync(path.join(ROOT, 'chains/index.html'), 'utf8');
  assert.ok(!/['"]pflex['"]/.test(chains), 'no pump flex module in the chains editor');
});
