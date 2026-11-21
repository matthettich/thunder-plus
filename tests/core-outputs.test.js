// The audio-thread core node's output count lives in ONE place, coreLoad() in index.html (NT + 5 since MOD vol:MT, build 556).
// tools/flex-check.mjs and tools/sampler-check.mjs do not build the node: they call __seqTest.coreOn(), which runs coreLoad(), so they follow the app
// (checked 10 Nov: both pass on the audio thread with NT + 5). This guards that they stay that way: a tool that builds 'thunder-core' itself
// with its own output count would drift again.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
test('coreLoad builds the core node with NT + 5 outputs, and the MOD master-out offset is output NT + 4', () => {
  assert.match(src, /new AudioWorkletNode\(ctx, 'thunder-core', \{numberOfInputs:0, numberOfOutputs:NT \+ 5, outputChannelCount:\[\.\.\.new Array\(NT\)\.fill\(2\), 1, NT, NT, 1, 1\]\}\)/);
  assert.match(src, /n\.connect\(rt\.mo\.gain, NT \+ 4\)/);
});
for (const f of ['tools/flex-check.mjs', 'tools/sampler-check.mjs']) test(f + ' goes through the app to start the core, not its own node', () => {
  const t = H.read(f);
  assert.match(t, /coreOn\(/); assert.doesNotMatch(t, /thunder-core/); assert.doesNotMatch(t, /numberOfOutputs:\s*(NT|\d{2})/);
});
