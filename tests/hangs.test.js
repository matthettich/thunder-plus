// Regression tests for modules that once hung the audio thread. A hang cannot be caught by an assertion in this process (it would
// freeze the test runner too), so each risky render runs in a CHILD process with a time limit, and "did not finish" is the failure.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), { spawnSync } = require('child_process');
const H = require('./helpers');
const LIMIT_MS = 8000;

function inChild(body) {
  const script = `const H=require(${JSON.stringify(require.resolve('./helpers'))}); (async()=>{ ${body} })().catch(e=>{console.log('ERR '+e.message);process.exit(3);});`;
  const r = spawnSync(process.execPath, ['-e', script], { cwd: H.ROOT, timeout: LIMIT_MS, encoding: 'utf8' });
  if (r.error && r.error.code === 'ETIMEDOUT') return { hung: true };
  return { hung: false, out: (r.stdout || '').trim(), code: r.status, err: r.stderr };
}
const tidesBody = (engine, extra) => `
  const sc = ${engine === 'waves' ? "H.pageScope()" : "H.workletScope()"}; H.load(sc, '${engine}/${engine}-worklet.js');
  const D = sc.${engine === 'waves' ? 'WavesDSP' : 'ChainsDSP'}; await D.modsPrepare();
  const p = {}; for (const [k, d] of Object.entries(D.MODULES.tides.params)) p[k] = d[0]; Object.assign(p, ${JSON.stringify(extra.params)});
  const E = new D.${engine === 'waves' ? 'WavesEngine' : 'ChainsEngine'}(48000, ${engine === 'waves' ? "{maxVoices:2,quality:'full'" : "{focus:0,quality:'full'"},transport:{tempo:${extra.tempo || 120},lpb:4,ticks:6,playing:true}});
  const mods = [{id:'b',type:'tides',scope:'mono',params:p}${extra.clock ? ",{id:'k',type:'euclid',scope:'mono',params:(()=>{const q={};for(const [k,d] of Object.entries(D.MODULES.euclid.params))q[k]=d[0];return q;})()}" : ''}${engine === 'chains' ? ",{id:'c',type:'chainOut',scope:'mono',params:{track:1,level:1}}" : ''}];
  const cab = [${extra.clock ? "{from:['k','gate'],to:['b','clock']}" : ''}${engine === 'chains' ? (extra.clock ? ',' : '') + "{from:['b','out1'],to:['c','in']}" : ''}];
  E.loadPatch({modules:mods, cables:cab}); for (let i = 0; i < 40; i++) E.process(128); console.log('OK');`;

for (const engine of ['chains', 'waves']) {
  test(`${engine}: Function generator in the Audio range with Sync to tempo does not hang (every division)`, () => {
    for (const div of [0, 3, 6, 9]) {
      const r = inChild(tidesBody(engine, { params: { range: 1, sync: 1, div } }));
      assert.ok(!r.hung, `tides range=Audio sync=on div=${div} did not finish in ${LIMIT_MS} ms: the audio thread would freeze`);
      assert.equal(r.out, 'OK', r.err || r.out);
    }
  });
  test(`${engine}: Function generator in the Audio range with a patched Clock cable does not hang`, () => {
    const r = inChild(tidesBody(engine, { params: { range: 1, sync: 0 }, clock: true }));
    assert.ok(!r.hung, `tides range=Audio + clock cable did not finish in ${LIMIT_MS} ms`);
    assert.equal(r.out, 'OK', r.err || r.out);
  });
}

test('the child-process harness can actually detect a hang (so a pass above means something)', () => {
  const r = spawnSync(process.execPath, ['-e', 'for(;;){}'], { timeout: 1500 });
  assert.ok(r.error && r.error.code === 'ETIMEDOUT', 'an infinite loop is reported as a timeout');
});

test('Control range still follows a clock (the guard must not change the default behaviour)', () => {
  // With a patched clock in the Control range the output must differ from the free-running one: the clock reaches the wasm.
  const run = clock => inChild(`
    const sc = H.workletScope(); H.load(sc, 'chains/chains-worklet.js'); const D = sc.ChainsDSP; await D.modsPrepare();
    const p = {}; for (const [k, d] of Object.entries(D.MODULES.tides.params)) p[k] = d[0]; p.range = 0; p.sync = ${clock ? 1 : 0};
    const E = new D.ChainsEngine(48000, {focus:0,quality:'full',transport:{tempo:90,lpb:4,ticks:6,playing:true}});
    E.loadPatch({modules:[{id:'b',type:'tides',scope:'mono',params:p},{id:'c',type:'chainOut',scope:'mono',params:{track:1,level:1}}],cables:[{from:['b','out1'],to:['c','in']}]});
    const L = new Float32Array(128 * 200); for (let b = 0; b < 200; b++) { E.process(128); L.set(E.bus(1), b * 128); }
    console.log(H.hashFloats([L]));`);
  const free = run(false), synced = run(true);
  assert.ok(!free.hung && !synced.hung, 'neither hangs');
  assert.notEqual(free.out, synced.out, 'Sync to tempo changes the Control-range output (the clock still reaches the module)');
});
