// PUMP CODE OVER THE PORT (build 689). In Chrome 153 a WebAssembly.Module posted with node.port.postMessage never reaches an
// AudioWorklet (the processor gets a messageerror); one in processorOptions does. A track-FX node made before pump.wasm has
// arrived gets the pump code later over its port (pumpWait / needPump): before build 689 that was the page-compiled Module, so
// its …pump slots passed the sound dry. This check makes that happen on purpose: pump.wasm is held back for 6 s, the song
// (one …pump effect alone on a track: CRUSH at 3 bits and 2 kHz, unmistakable) plays at once, and after the wasm has
// arrived the master's spectrum is measured, against the same with an empty chain on that track.
//   node tools/pump-port-check.mjs [--song=sounds/BETA-TESTING-ELECTRO.thunder] [--track=0]
// PASS: the effect changes the spectrum by well over what two takes with it differ. Needs playwright + Chrome (tests/sweep/README.md).
import fs from 'fs';
import { openApp } from '../tests/sweep/lib.mjs';
const arg = (k, d) => { const a = process.argv.find(x => x.startsWith('--' + k + '=')); return a ? a.slice(k.length + 3) : d; };
const SONG = arg('song', 'sounds/BETA-TESTING-ELECTRO.thunder'), T = +arg('track', 0), HOLD = 6000;
const proj = fs.readFileSync(SONG, 'utf8');
const song = on => { const j = JSON.parse(proj);
  for (let t = 0; t < j.song.tfx.length; t++) j.song.tfx[t] = (j.song.tfx[t] || []).map(f => f && /^p/.test(f.type) ? null : f);   // no other …pump effect anywhere
  j.song.mfx = (j.song.mfx || []).map(f => f && /^p/.test(f.type) ? null : f);
  const nt = j.song.tfx.length; j.song.song = j.song.song.map(r => r.map((v, c) => c < nt && c !== T ? -1 : v));   // only track T plays
  j.song.tfx[T] = on ? [{ type: 'pcrush', on: true, pump: { bits: 3, rate: 2000, drive: 12, mix: 100 } }] : [];
  return JSON.stringify(j); };
async function take(on) {
  const A = await openApp({ route: ['**/pump/pump.wasm*', async r => { await new Promise(z => setTimeout(z, HOLD)); await r.continue(); }] });
  try {
    await A.page.evaluate(t => window.__seqTest.load(t, 'check.thunder'), song(on));
    return await A.run(`
      aset.vol = 100; rtInit(); tStart('song', {row:0});   // PLAY at once: the track-FX nodes are made while pump.wasm is held back
      const loaded = () => typeof pumpBin === 'function' && !!pumpBin();
      for (let i = 0; i < 200 && !loaded(); i++) await new Promise(r => setTimeout(r, 50));
      if (!loaded()) return {err:'pump.wasm never arrived'};
      await new Promise(r => setTimeout(r, 1500));   // the nodes waiting for it get it now
      const an = ctx.createAnalyser(); an.fftSize = 4096; an.smoothingTimeConstant = 0; master.connect(an);
      const bins = new Float32Array(an.frequencyBinCount), sum = new Float64Array(16); let n = 0;
      for (let k = 0; k < 40; k++){ await new Promise(r => setTimeout(r, 100)); an.getFloatFrequencyData(bins);
        for (let i = 1; i < bins.length; i++){ const f = i * ctx.sampleRate / an.fftSize, b = Math.min(15, Math.max(0, Math.floor(2 * Math.log2(f / 40)))); sum[b] += Math.pow(10, bins[i] / 10); } n++; }
      tStop(); master.disconnect(an);
      return {bands: Array.from(sum, v => 10 * Math.log10(v / n + 1e-30))};`);
  } finally { await A.close(); }
}
const w1 = await take(true), w2 = await take(true), no = await take(false);
for (const r of [w1, w2, no]) if (r.err) { console.log('ERROR: ' + r.err); process.exit(2); }
const spread = (a, b) => Math.max(...a.bands.map((v, i) => Math.abs(v - b.bands[i])).slice(2));
const dFx = spread(w1, no), dSame = spread(w1, w2);
console.log(`track ${T}, pcrush added while pump.wasm was held back ${HOLD / 1000} s`);
console.log(`widest half-octave difference, with the effect against without: ${dFx.toFixed(1)} dB; two takes with it: ${dSame.toFixed(1)} dB`);
const ok = dFx > Math.max(3, 2 * dSame);
console.log(ok ? 'PASS: the …pump effect is heard' : 'FAIL: the track sounds dry: its node never got the pump code');
process.exit(ok ? 0 : 1);
