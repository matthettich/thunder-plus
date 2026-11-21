// C1g follow-up: MEASURES in headless Chrome the two things chains-song-dump.mjs infers (docs/port/C1G-NOTES.md, "Chrome k-rate check").
//   node native/tools/chains-krate-check.mjs [--runs 3]      needs playwright + Chrome (PLAYWRIGHT=, CHROME=; defaults as song-ref.mjs); prints the disagreements with the tool's rule, exit 1 if any
// A tiny AudioWorkletProcessor with one k-rate AudioParam 'v' writes, per render quantum, the param value (channel 0) and the id of the last port message received (channel 1) into its output.
//  1. k-rate: v.setValueAtTime(k, t) for t = k*128/sr (exact), (k*128-1)/sr, (k*128+1)/sr, k = 1..400; the quantum where the processor first sees v = k is compared with the tool's rule ceil(t / q), q = 128 / sr, in double.
//  2. tempo path (index.html:23222-23236, offline branch): at = Math.ceil(t / q) * q; ac.suspend(at).then(() => { port.postMessage(k); return ac.resume(); }); the quantum where the message with id k is first seen is compared the same way
//     (the page's `at > 0 && at < length` test sends at once otherwise; k >= 1 here keeps every at inside).
// Events of k mod 3 = r go in one context per r (two events in one quantum would hide one); each run uses 48000 Hz and 44100 Hz.
import http from 'node:http'; import fs from 'node:fs'; import path from 'node:path'; import { pathToFileURL } from 'node:url';
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium'].find(f => fs.existsSync(f));
const runs = +(process.argv[process.argv.indexOf('--runs') + 1]) || 3;
const { chromium } = await import(pathToFileURL(PW).href);
const PROC = `class P extends AudioWorkletProcessor { static get parameterDescriptors(){ return [{name:'v', defaultValue:0, automationRate:'k-rate'}]; }
  constructor(){ super(); this.m = 0; this.port.onmessage = e => { this.m = e.data; }; }
  process(i, o, p){ o[0][0].fill(p.v[0]); o[0][1].fill(this.m); return true; } }
registerProcessor('p', P);`;
const srv = http.createServer((q, r) => { if (q.url === '/p.js') { r.writeHead(200, { 'content-type': 'text/javascript' }); r.end(PROC); } else { r.writeHead(200, { 'content-type': 'text/html' }); r.end('<html></html>'); } });
await new Promise(ok => srv.listen(0, ok));
const browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage(); await page.goto(`http://localhost:${srv.address().port}/`);
const measure = async (sr) => page.evaluate(async ([sr, QN]) => {
  const q = 128 / sr, res = { krate: {}, tempo: {} };
  const first = (ch, id) => { for (let i = 0; i < ch.length; i += 128) if (ch[i] === id) return i / 128; return -1; };
  for (const fam of ['exact', 'before', 'after']) { res.krate[fam] = {}; res.tempo[fam] = {};
    const tOf = k => fam === 'exact' ? (k * 128) / sr : fam === 'before' ? (k * 128 - 1) / sr : (k * 128 + 1) / sr;
    for (let r = 0; r < 3; r++) {
      for (const mode of ['krate', 'tempo']) {
        const ac = new OfflineAudioContext(2, QN * 128, sr); await ac.audioWorklet.addModule('/p.js');
        const n = new AudioWorkletNode(ac, 'p', { numberOfInputs: 0, outputChannelCount: [2] }); n.connect(ac.destination);
        const ks = []; for (let k = 1 + r; k <= 400; k += 3) ks.push(k);
        if (mode === 'krate') for (const k of ks) n.parameters.get('v').setValueAtTime(k, Math.max(tOf(k), 0));
        else for (const k of ks) { const at = Math.ceil(tOf(k) / q) * q; ac.suspend(at).then(() => { n.port.postMessage(k); return ac.resume(); }); }
        const buf = await ac.startRendering(), ch = buf.getChannelData(mode === 'krate' ? 0 : 1);   // channel 0 = param value, 1 = last message id (the output's 2nd channel)
        const d = mode === 'krate' ? buf.getChannelData(0) : buf.getChannelData(1);
        for (const k of ks) res[mode][fam][k] = first(d, k);
      }
    } }
  return res;
}, [sr, 410]);
let bad = 0; const all = [];
for (const sr of [48000, 44100]) {
  const q = 128 / sr, runsR = []; for (let i = 0; i < runs; i++) runsR.push(await measure(sr));
  const stable = runsR.every(r => JSON.stringify(r) === JSON.stringify(runsR[0]));
  let tot = 0, dis = [], fuzz = 0, cand = 0;
  for (const mode of ['krate', 'tempo']) for (const fam of ['exact', 'before', 'after']) for (const k of Object.keys(runsR[0][mode][fam]).map(Number)) {
    const t = fam === 'exact' ? (k * 128) / sr : fam === 'before' ? (k * 128 - 1) / sr : (k * 128 + 1) / sr, rule = Math.ceil(t / q), got = runsR[0][mode][fam][k]; tot++;
    if (fam === 'exact' && Math.ceil(t / q) !== k) fuzz++;
    if (mode === 'krate' && got !== Math.ceil(Math.ceil(t * sr) / 128)) cand++;   // candidate Chrome rule: frame = ceil(t * sr), quantum = ceil(frame / 128)
    if (got !== rule) dis.push({ mode, fam, k, rule, chrome: got, ceilFuzz: fam === 'exact' && rule !== k });
  }
  console.log(`sr ${sr}: ${runs} runs ${stable ? 'identical' : 'DIFFER'}; ${tot} measurements; k with ceil(t/q) != k on the exact family: ${fuzz}; disagreements with ceil(t/q): ${dis.length}; k-rate disagreements with the candidate rule ceil(ceil(t*sr)/128): ${cand}`);
  const sum = {}; for (const d of dis) { const key = `${d.mode}/${d.fam} chrome-rule=${d.chrome - d.rule}`; (sum[key] = sum[key] || []).push(d.k); }
  for (const [k, v] of Object.entries(sum)) console.log('  ' + k + ': ' + v.length + ' k: ' + v.slice(0, 40).join(',') + (v.length > 40 ? ',...' : ''));
  if (dis.length || !stable) bad = 1; all.push({ sr, dis });
}
await browser.close(); srv.close(); process.exit(bad);
