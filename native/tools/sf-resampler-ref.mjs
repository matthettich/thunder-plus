// U32: reference data for the fitted resampler (sf_decode.cpp `resampleTo`): what Chrome's decodeAudioData makes of a known 16-bit WAV at two rate pairs. Writes native/tests/golden/sf_resampler_chrome.json,
// which `test_sf unit` compares native/core/src/sf's resampler with, so a change of its filter fails in ctest without Chrome. Rerun (needs google-chrome): node native/tools/sf-resampler-ref.mjs
// The input is an integer LCG noise (white: it exercises the whole band, so the cutoff, window and kernel quantisation all show): s = s * 1664525 + 1013904223 (uint32), v = ((s >> 16) & 0x3fff) - 0x2000, 1500 samples, seed 12345.
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const CASES = [{ from: 44100, to: 48000 }, { from: 48000, to: 44100 }, { from: 32000, to: 48000 }, { from: 22050, to: 44100 }], N = 1500, SEED = 12345;
const port = 9300 + Math.floor(Math.random() * 500), prof = fs.mkdtempSync('/tmp/sf-rs-');
const chrome = spawn('google-chrome', ['--headless=new', '--no-sandbox', '--disable-gpu', '--user-data-dir=' + prof, '--remote-debugging-port=' + port, 'about:blank'], { stdio: 'ignore' });
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
let target = null;
for (let i = 0; i < 100 && !target; i++) { await sleep(200); try { target = (await (await fetch(`http://127.0.0.1:${port}/json/list`)).json()).find(t => t.type === 'page'); } catch {} }
if (!target) { chrome.kill(); console.error('Chrome did not start'); process.exit(1); }
const ws = new WebSocket(target.webSocketDebuggerUrl); await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
const reply = new Map(); let nid = 0; ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && reply.has(d.id)) { reply.get(d.id)(d); reply.delete(d.id); } };
const send = (method, params) => new Promise(r => { const id = ++nid; reply.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const expr = `(async () => {
  const out = { ua: navigator.userAgent, cases: [] };
  for (const c of ${JSON.stringify(CASES)}) {
    let s = ${SEED} >>> 0; const x = new Int16Array(${N}); for (let i = 0; i < ${N}; i++) { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; x[i] = ((s >>> 16) & 0x3fff) - 0x2000; }
    const wav = new ArrayBuffer(44 + 2 * ${N}), v = new DataView(wav), w = (o, t) => { for (let i = 0; i < 4; i++) v.setUint8(o + i, t.charCodeAt(i)); };
    w(0, 'RIFF'); v.setUint32(4, 36 + 2 * ${N}, true); w(8, 'WAVE'); w(12, 'fmt '); v.setUint32(16, 16, true); v.setUint16(20, 1, true); v.setUint16(22, 1, true); v.setUint32(24, c.from, true); v.setUint32(28, c.from * 2, true); v.setUint16(32, 2, true); v.setUint16(34, 16, true); w(36, 'data'); v.setUint32(40, 2 * ${N}, true);
    for (let i = 0; i < ${N}; i++) v.setInt16(44 + 2 * i, x[i], true);
    const b = await new OfflineAudioContext(1, 1, c.to).decodeAudioData(wav); out.cases.push({ from: c.from, to: c.to, out: Array.from(b.getChannelData(0)) });
  }
  return JSON.stringify(out);
})()`;
const res = await send('Runtime.evaluate', { expression: expr, awaitPromise: true, returnByValue: true, timeout: 120000 });
chrome.kill();
if (res.result.exceptionDetails) { console.error(JSON.stringify(res.result.exceptionDetails).slice(0, 1500)); process.exit(1); }
const o = JSON.parse(res.result.result.value);
const g = { comment: `Chrome's decodeAudioData output (Float32) for a 16-bit mono WAV of ${N} LCG-noise samples (seed ${SEED}; see native/tools/sf-resampler-ref.mjs) at rate 'from', context rate 'to'. Captured with ${o.ua} on ${new Date().toISOString().slice(0, 10)}.`, n: N, seed: SEED, ua: o.ua, cases: o.cases.map(c => ({ from: c.from, to: c.to, out: c.out.map(v => +v.toPrecision(9)) })) };
fs.writeFileSync(path.join(ROOT, 'native/tests/golden/sf_resampler_chrome.json'), JSON.stringify(g));
console.log(o.ua, g.cases.map(c => `${c.from}->${c.to}: ${c.out.length} frames`).join(', '));
process.exit(0);
