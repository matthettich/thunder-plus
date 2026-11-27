// Build 689: in Chrome 153 a WebAssembly.Module posted with node.port.postMessage never reaches an AudioWorklet (the processor
// gets a messageerror), live and offline. processorOptions still carry a Module, and a dedicated Worker still gets one. So on
// a port only wasm BYTES may go: the track-FX node asking for pump code (needPump) and the nodes waiting for pump.wasm got the
// page-compiled Module and played their pump slots dry. This test keeps every port message free of a Module.
const test = require('node:test'), assert = require('node:assert'), fs = require('node:fs'), path = require('node:path');
const root = path.join(__dirname, '..');
const files = ['index.html', 'chains/index.html', 'waves/index.html', 'pump/synth/pump-synth-engine.js', 'pump/synth/pump-synth-worklet.js', 'thunder-core-client.js'];
// the names the page keeps its compiled Modules under, and the message keys that would carry one
const MOD = /\b(pumpModOpt\(\)|pumpWasmMod|tfxModOpt\(\)|tfxMod|txMod|plMod|lxMod|psMod|PsynCore\.module\(\)|pumpModule|tfxModule|dspModule)\b/;
test('no port.postMessage carries a WebAssembly.Module', () => {
  let seen = 0;
  for (const f of files) {
    const p = path.join(root, f); if (!fs.existsSync(p)) continue;
    const src = fs.readFileSync(p, 'utf8');
    for (const m of src.matchAll(/port\.postMessage\(/g)) {
      seen++;
      // the call's argument list, up to its closing bracket
      let i = m.index + m[0].length, depth = 1; const a = i;
      while (i < src.length && depth > 0) { const c = src[i++]; if (c === '(') depth++; else if (c === ')') depth--; }
      const arg = src.slice(a, i - 1);
      assert.ok(!MOD.test(arg), f + ': port.postMessage(' + arg.slice(0, 120) + ') sends a Module: send bytes on a port');
    }
  }
  assert.ok(seen > 10);
});
test('the pump code reaches a track-FX node as bytes', () => {
  const src = fs.readFileSync(path.join(root, 'index.html'), 'utf8');
  assert.ok(/if \(d\.needPump\)\{ if \(pumpBin\(\)\) n\.port\.postMessage\(\{pumpBytes:pumpBin\(\)\}\);/.test(src));
  assert.ok(/for \(const n of pumpWait\) try \{ n\.port\.postMessage\(\{pumpBytes:b\}\); \}/.test(src));
  assert.ok(/e\.data\.pumpBytes && !globalThis\.__pumpBytes\) globalThis\.__pumpBytes = e\.data\.pumpBytes/.test(src));   // the worklet compiles them
});
