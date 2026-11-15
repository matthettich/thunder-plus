// Mixer OUT meter (15 Nov): both mixers report the peak they send out through show(), which the engine's telemetry carries to the editor.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const cabl = (a, pa, b, pb) => ({ from: [a, pa], to: [b, pb] });
function run(type, gain, silent){
  const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js');
  const E = new ctx.ChainsDSP.ChainsEngine(48000, { focus: -1, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
  E.loadPatch({ modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'm', type, scope: 'mono', params: { l1: gain, chans: 2 } }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }],
    cables: [...(silent ? [] : [cabl('a', 'out', 'm', 'in1'), ...(type === 'smixer' ? [cabl('a', 'outR', 'm', 'in1R')] : [])]), cabl('m', 'out', 'c', 'in')] });
  E.handleMessage({ op: 'telemetry', value: true });
  const s = new Float32Array(128).fill(.5);
  for (let b = 0; b < 4; b++){ E.setTrackInputs(s, s); E.process(128); }
  return E.telemetry().shows.find(x => x[0] === 'm');
}
for (const type of ['mixer', 'smixer']){
  test(type + ': the OUT meter reports the peak sent out (dB), and nothing for silence', () => {
    const v = run(type, 1, false); assert.ok(v, 'a show entry'); assert.ok(Math.abs(v[1][0] - 20 * Math.log10(.5)) < .2, 'about -6 dB: ' + v[1][0]);
    const q = run(type, 1, true); assert.equal(q[1][0], -120);
  });
  test(type + ': the meter does not change the audio (gain 1.5 sends 0.5 * 1.5 * level)', () => {
    const v = run(type, 1.5, false); assert.ok(Math.abs(v[1][0] - 20 * Math.log10(.5 * 1.5)) < .2, String(v[1][0]));
  });
}
test('waves mixer has the same OUT meter', () => {
  const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js'); const M = ctx.WavesDSP.MODULES.mixer;
  const st = M.create(48000), o = new Float32Array(128), x = new Float32Array(128).fill(.25), io = { p: { chans: 1, l1: 1, level: 1 }, out: { out: o }, in: { in1: x }, linked: new Set(['in1']) };
  M.process(st, io, { block: 128, sr: 48000 });
  assert.ok(Math.abs(M.show(st)[0] - 20 * Math.log10(.25)) < .01); assert.equal(M.show(st)[0], -120, 'read-and-reset');
});
