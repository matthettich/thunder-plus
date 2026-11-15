// U11 / U12: the master chain in REAL Web Audio. master-worklet.js as an AudioWorkletNode in an OfflineAudioContext in headless Chrome, against the same
// worklet run in a Node vm (tools/master-dump.mjs's method), on 3 s of a song's mix (the Chrome reference render of sounds/ELECTRO, native/tools/render-compare.sh
// makes it in $THUNDER_REF, default /tmp/thunder-ref/ELECTRO/ref.f32): each stage alone, all three, and the page's whole master graph for a song with mastering
// on (input gain -> the EQ's BiquadFilterNodes -> thunder-master with sup + comp -> thunder-master with SAFE_LIM -> destination) against the vm + the EQ model of
// tools/eq-dump.mjs. Result committed in native/tests/golden/master-webaudio-check.txt. Not part of CI (needs Chrome, playwright and the reference render).
//   node native/tools/master-webaudio-check.mjs [--write]
import fs from 'node:fs'; import path from 'node:path'; import vm from 'node:vm'; import { pathToFileURL } from 'node:url';
import { eqNodes, modelRun, SHAPED, ROOT } from './eq-dump.mjs';
const PW = process.env.PLAYWRIGHT || '/opt/npm-tools/node_modules/playwright/index.mjs', CHROME = process.env.CHROME || '/usr/bin/google-chrome';
const refPath = (process.env.THUNDER_REF || '/tmp/thunder-ref') + '/ELECTRO/ref.f32';
if (!fs.existsSync(refPath) || !fs.existsSync(PW) || !fs.existsSync(CHROME)) { console.error('needs Chrome, playwright and ' + refPath + ' (render-compare.sh makes it)'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
const src = fs.readFileSync(path.join(ROOT, 'master-worklet.js'), 'utf8').replace(/\r\n/g, '\n'), SR = 48000, Q = 128;
function mk(settings) { let Cls; const ctx = vm.createContext({ console, sampleRate: SR, Date, registerProcessor(n, c) { Cls = c; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } } }); vm.runInContext(src, ctx); return new Cls({ processorOptions: { meters: false, set: settings } }); }
const buf = fs.readFileSync(refPath), all = new Float32Array(buf.buffer, buf.byteOffset, buf.length / 4), NN = all.length / 2, a0 = 2200000, n = Q * 1500;
const inL = all.slice(a0, a0 + n), inR = all.slice(NN + a0, NN + a0 + n);
const SUP = { on: true, depth: .4, sharp: .5, sel: 4, atk: 8, rel: 90, lo: 150, hi: 12000, mix: 1, delta: false }, COMP = { on: true, thr: -14, ratio: 2, atk: 10, rel: 150, auto: true, knee: 6, makeup: 0, automk: true, mix: 1, hpf: 80 }, LIM = { on: true, drive: 0, ceil: -1, rel: 120, look: 3, tp: true };
const OFF = { sup: { ...SUP, on: false }, comp: { ...COMP, on: false }, lim: { ...LIM, on: false } };
const cases = { sup: { ...OFF, sup: SUP }, comp: { ...OFF, comp: COMP }, lim: { ...OFF, lim: LIM }, all: { sup: SUP, comp: COMP, lim: LIM } };
const browser = await chromium.launch({ executablePath: CHROME }); const page = await browser.newPage();
await page.route('http://localhost/**', r => r.fulfill({ body: '<html></html>', contentType: 'text/html' })); await page.goto('http://localhost/');   // (a secure context: audioWorklet exists)
const chrome = browser.version(), lines = [`master-webaudio-check: Chrome ${chrome}, master-worklet.js in an OfflineAudioContext against the same file in a Node vm, ${n} frames of ELECTRO's mix`];
const worklet = (set, nodes) => page.evaluate(async ([code, set, L, R, nodes, safe]) => {
  const nn = L.length, oc = new OfflineAudioContext(2, nn, 48000), b = oc.createBuffer(2, nn, 48000); b.getChannelData(0).set(L); b.getChannelData(1).set(R);
  await oc.audioWorklet.addModule(URL.createObjectURL(new Blob([code], { type: 'text/javascript' })));
  const s = oc.createBufferSource(); s.buffer = b; let h = s;
  if (nodes) { const g = oc.createGain(); h.connect(g); h = g; for (const d of nodes) { const x = oc.createBiquadFilter(); x.type = d.type; x.frequency.value = d.f; x.Q.value = d.Q; x.gain.value = d.g; h.connect(x); h = x; } }
  const wk = st => new AudioWorkletNode(oc, 'thunder-master', { numberOfInputs: 1, numberOfOutputs: 1, outputChannelCount: [2], channelCount: 2, channelCountMode: 'explicit', processorOptions: { set: st, meters: false } });
  const w1 = wk(set); h.connect(w1); h = w1; if (safe) { const w2 = wk(safe); h.connect(w2); h = w2; }
  h.connect(oc.destination); s.start(0); const r = await oc.startRendering(); return [Array.from(r.getChannelData(0)), Array.from(r.getChannelData(1))]; }, [src, set, Array.from(inL), Array.from(inR), nodes || null, nodes ? { lim: LIM } : null]);
const run = (set, L, R, safe) => { const P = mk(set), S = safe ? mk(safe) : null, oL = new Float32Array(n), oR = new Float32Array(n);
  for (let b = 0; b < n / Q; b++) { let o = [new Float32Array(Q), new Float32Array(Q)]; P.process([[L.subarray(b * Q, b * Q + Q), R.subarray(b * Q, b * Q + Q)]], [o]); if (S) { const p = [new Float32Array(Q), new Float32Array(Q)]; S.process([[o[0], o[1]]], [p]); o = p; } oL.set(o[0], b * Q); oR.set(o[1], b * Q); } return [oL, oR]; };
const cmp = (name, ch, m) => { let pk = 0, err = 0, nd = 0; for (let i = 0; i < n; i++) { pk = Math.max(pk, Math.abs(ch[0][i])); const e = Math.max(Math.abs(ch[0][i] - m[0][i]), Math.abs(ch[1][i] - m[1][i])); if (e > 0) nd++; err = Math.max(err, e); }
  lines.push(`${name.padEnd(26)} ${err === 0 ? 'exact' : (20 * Math.log10(err / pk)).toFixed(1) + ' dB'}  (${nd} of ${n} samples differ)`); };
for (const [name, set] of Object.entries(cases)) cmp('worklet ' + name, await worklet(set), run(set, inL, inR));
const nodes = eqNodes({ on: true, bands: SHAPED() }), eL = modelRun(nodes, SR, inL), eR = modelRun(nodes, SR, inR), set = { sup: SUP, comp: COMP, lim: { ...LIM, on: false } };
cmp('whole master graph', await worklet(set, nodes), run(set, eL, eR, { lim: LIM }));
await browser.close(); console.log(lines.join('\n'));
if (process.argv.includes('--write')) fs.writeFileSync(path.join(ROOT, 'native/tests/golden/master-webaudio-check.txt'), lines.join('\n') + '\n');
