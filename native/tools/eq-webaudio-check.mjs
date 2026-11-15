// U12: the master EQ's JS model (tools/eq-dump.mjs) against REAL Web Audio: every case of eq-dump.mjs rendered in an OfflineAudioContext in
// headless Chrome with BiquadFilterNodes set as the page's eqBuild sets them (the node list eqBuild gives: type, frequency, Q, gain), in series,
// the same stereo input. Reports per case (a) the largest sample difference against the model's output in dB of the model's peak, and (b) the
// largest magnitude-response difference in dB over 240 log-spaced frequencies (20 Hz .. 20 kHz, where the model's response is above -80 dB) between Chrome's and the model's impulse responses.
// Gate (WORKPLAN U12): the EQ response within 0.01 dB of Web Audio's. Not part of CI (needs Chrome and playwright):
//   node native/tools/eq-webaudio-check.mjs            (PLAYWRIGHT=, CHROME= name them; defaults /opt/npm-tools/node_modules/playwright, /usr/bin/google-chrome)
import fs from 'node:fs'; import path from 'node:path'; import { pathToFileURL } from 'node:url';
import { CASES, runCase, signal, modelRun, N, ROOT } from './eq-dump.mjs';

const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW || !CHROME) { console.error('needs playwright and Chrome (PLAYWRIGHT=, CHROME=)'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
const browser = await chromium.launch({ executablePath: CHROME });
const page = await browser.newPage(); await page.goto('about:blank');
const chrome = browser.version();

// Chrome side: input (stereo Float32 arrays as plain arrays) + node list -> output
const render = (sr, nodes, L, R) => page.evaluate(async ([sr, nodes, L, R]) => {
  const n = L.length, oc = new OfflineAudioContext(2, n, sr), buf = oc.createBuffer(2, n, sr);
  buf.getChannelData(0).set(L); buf.getChannelData(1).set(R);
  const src = oc.createBufferSource(); src.buffer = buf; let head = src;
  for (const d of nodes) { const b = oc.createBiquadFilter(); b.type = d.type; b.frequency.value = d.f; b.Q.value = d.Q; b.gain.value = d.g; head.connect(b); head = b; }
  head.connect(oc.destination); src.start(0);
  const r = await oc.startRendering(); return [Array.from(r.getChannelData(0)), Array.from(r.getChannelData(1))];
}, [sr, nodes, Array.from(L), Array.from(R)]);

// magnitude of the impulse response h (an impulse at k0 in x, response y): |H(f)| by DFT over the response
const freqs = Array.from({ length: 240 }, (_, i) => 20 * Math.pow(1000, i / 239));
const mag = (h, k0, sr) => freqs.map(f => { let re = 0, im = 0; const w = 2 * Math.PI * f / sr; for (let i = k0; i < h.length; i++) { re += h[i] * Math.cos(w * (i - k0)); im -= h[i] * Math.sin(w * (i - k0)); } return Math.hypot(re, im); });
const FLOOR = Math.pow(10, -80 / 20);
const lines = []; let worstResp = 0, worstT = -Infinity;
const ONLY = process.env.ONLY ? new RegExp(process.env.ONLY) : null;
for (const c of CASES) {
  if (ONLY && !ONLY.test(c.name)) continue;
  const r = runCase(c), [cL, cR] = await render(c.sr, r.nodes, r.L, r.R);
  let pk = 0, err = 0; for (let i = 0; i < N; i++) { pk = Math.max(pk, Math.abs(r.oL[i]), Math.abs(r.oR[i])); err = Math.max(err, Math.abs(cL[i] - r.oL[i]), Math.abs(cR[i] - r.oR[i])); }
  const tdb = err === 0 ? -Infinity : 20 * Math.log10(err / Math.max(pk, 1e-30));
  // impulse responses: a lone impulse in a silent input (the model and Chrome on the same input), compared as a response in dB
  const imp = new Float32Array(N); imp[0] = 1; const z = new Float32Array(N);
  const [iC] = await render(c.sr, r.nodes, imp, z), iM = modelRun(r.nodes, c.sr, imp);
  const A = mag(iC, 0, c.sr), Bm = mag(iM, 0, c.sr); let rdb = 0; for (let i = 0; i < freqs.length; i++) if (Bm[i] > FLOOR) rdb = Math.max(rdb, Math.abs(20 * Math.log10(Math.max(A[i], 1e-30) / Bm[i])));   // (below -80 dB the float32 output's own noise is all there is)
  worstResp = Math.max(worstResp, rdb); worstT = Math.max(worstT, tdb);
  lines.push(`${c.name.padEnd(22)} ${String(r.nodes.length).padStart(2)} nodes  samples ${isFinite(tdb) ? tdb.toFixed(1) + ' dB' : 'exact'}`.padEnd(54) + `response ${rdb.toExponential(2)} dB${rdb <= 0.01 ? '' : '  FAIL (> 0.01 dB)'}`);
}
await browser.close();
const out = [`eq-webaudio-check: Chrome ${chrome}, ${CASES.length} cases (the JS model of tools/eq-dump.mjs against an OfflineAudioContext)`, ...lines,
  `worst: samples ${worstT.toFixed(1)} dB of the peak, magnitude response ${worstResp.toExponential(2)} dB (gate 0.01 dB): ${worstResp <= 0.01 ? 'PASS' : 'FAIL'}`];
console.log(out.join('\n'));
if (process.argv.includes('--write')) fs.writeFileSync(path.join(ROOT, 'native/tests/golden/eq-webaudio-check.txt'), out.join('\n') + '\n');
process.exit(worstResp <= 0.01 ? 0 : 1);
