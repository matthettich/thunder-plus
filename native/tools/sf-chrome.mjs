// U32: the page's own SoundFont decode and render in a real (headless) Chrome, to measure what the Node reference cannot: `decodeAudioData` on the GM font's Ogg Vorbis samples (and its resampling to the context rate) and the render built on it.
//   node native/tools/sf-chrome.mjs DIR [ctxRate]    writes DIR/chrome_pcm_<idx>.f32, DIR/chrome_samples.json and DIR/chrome_render_<case>.f32 for the GM cases of native/tests/golden/sf_cases.json (default context rate 48000)
//   then: test_sf chrome DIR                          compares the native decode and render with them (prints the dB figures)
// The page code is index.html's SoundFont block, unchanged (`sfSample` decodes with `OfflineAudioContext.decodeAudioData`, `sfRender` renders). Needs google-chrome (or chromium) on the PATH; no npm package: the DevTools protocol is spoken over Node's WebSocket.
import fs from 'node:fs';
import path from 'node:path';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const dir = process.argv[2], ctxRate = +(process.argv[3] || 48000);
if (!dir) { console.error('usage: sf-chrome.mjs DIR [ctxRate]'); process.exit(2); }
fs.mkdirSync(dir, { recursive: true });
const CASES = JSON.parse(fs.readFileSync(path.join(ROOT, 'native/tests/golden/sf_cases.json'), 'utf8'));
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
const a = html.indexOf('/* ---------- SoundFont player (original code'), b = html.indexOf('const XUI = {', a);
const block = html.slice(a, b);
const fontBytes = fs.readFileSync(path.join(ROOT, CASES.fonts.gm));

const server = http.createServer((req, res) => { res.setHeader('Access-Control-Allow-Origin', '*'); res.setHeader('Content-Type', 'application/octet-stream'); res.end(fontBytes); });
await new Promise(r => server.listen(0, '127.0.0.1', r));
const fontUrl = `http://127.0.0.1:${server.address().port}/font`;

const script = `(async () => {
  var ctx = {sampleRate: ${ctxRate}}, srGuess = ${ctxRate};
  const toast = () => {}, toastErr = () => {}, clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const norm = (V) => { const d = {bank:0, prog:0, trans:0, velo:0.7, atk:1, rel:1}, o = V.sf, r = {}; for (const k in d) r[k] = o && typeof o[k] === 'number' && isFinite(o[k]) ? o[k] : d[k]; return r; };
  const xParams = (V) => Object.assign(norm(V), {tune:V.tune || 0, drive:V.drive || 0, level:V.level || 0});
  ${block}
  const b64 = (f32) => { const u = new Uint8Array(f32.buffer, f32.byteOffset, f32.byteLength); let s = ''; for (let i = 0; i < u.length; i += 32768) s += String.fromCharCode.apply(null, u.subarray(i, i + 32768)); return btoa(s); };
  const buf = await (await fetch(${JSON.stringify(fontUrl)})).arrayBuffer();
  const font = parseSF(buf); font.id = GM_FONT.id; SF.fonts.set(font.id, font);
  const cases = ${JSON.stringify(CASES.renders.filter(c => c.font === 'gm'))};
  const out = { ctxRate: ctx.sampleRate, ua: navigator.userAgent, samples: [], renders: [] }, done = new Set();
  for (const c of cases) {
    const p = Object.assign({bank:0, prog:0, trans:0, velo:0.7, atk:1, rel:1, level:0, len:0}, c.pad);
    const pr = sfPreset(font, Math.round(p.bank), Math.round(p.prog)), key = clamp(60 + Math.round(p.trans), 0, 127), picks = sfPick(font, pr, key, 100);
    for (const {z} of picks) { const want = [z.s]; const s0 = font.samples[z.s]; if ((s0.type & 6) && s0.link < font.samples.length) want.push(s0.link);
      for (const i of want) { if (done.has(i)) continue; done.add(i); const r = await sfSample(font, i);
        out.samples.push({idx:i, sr:r.sr, ls:r.ls, le:r.le, k:r.k, n:r.d.length, hdrSr:font.samples[i].sr, type:font.samples[i].type, d:b64(r.d)}); } }
    const V = {engine:'sf', sfFont:font.id, sf:{bank:p.bank, prog:p.prog, trans:p.trans, velo:p.velo, atk:p.atk, rel:p.rel}, level:p.level, len:p.len};
    const o = sfRender(V, c.sr, c.hold); out.renders.push({id:c.id, n:o.length, d:b64(o)});
  }
  return JSON.stringify(out);
})()`;

const bin = ['google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser'].find(n => { try { return fs.existsSync('/usr/bin/' + n) || fs.existsSync('/usr/local/bin/' + n); } catch { return false; } });
if (!bin) { console.error('no Chrome found'); process.exit(1); }
const port = 9300 + Math.floor(Math.random() * 500), prof = fs.mkdtempSync('/tmp/sf-chrome-');
const chrome = spawn(bin, ['--headless=new', '--no-sandbox', '--disable-gpu', '--user-data-dir=' + prof, '--remote-debugging-port=' + port, '--autoplay-policy=no-user-gesture-required', 'about:blank'], { stdio: 'ignore' });
const sleep = (ms) => new Promise(r => setTimeout(r, ms));
let target = null;
for (let i = 0; i < 100 && !target; i++) { await sleep(200); try { const l = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json(); target = l.find(t => t.type === 'page'); } catch {} }
if (!target) { chrome.kill(); console.error('Chrome did not start'); process.exit(1); }
const ws = new WebSocket(target.webSocketDebuggerUrl); await new Promise((r, j) => { ws.onopen = r; ws.onerror = j; });
const reply = new Map(); let nid = 0; ws.onmessage = (m) => { const d = JSON.parse(m.data); if (d.id && reply.has(d.id)) { reply.get(d.id)(d); reply.delete(d.id); } };
const send = (method, params) => new Promise(r => { const id = ++nid; reply.set(id, r); ws.send(JSON.stringify({ id, method, params })); });
const res = await send('Runtime.evaluate', { expression: script, awaitPromise: true, returnByValue: true, timeout: 600000 });
chrome.kill(); server.close();
if (res.result.exceptionDetails || res.error) { console.error(JSON.stringify(res.result.exceptionDetails || res.error).slice(0, 2000)); process.exit(1); }
const o = JSON.parse(res.result.result.value);
const meta = { ctxRate: o.ctxRate, ua: o.ua, samples: [], renders: [] };
for (const s of o.samples) { const f = `chrome_pcm_${s.idx}.f32`; fs.writeFileSync(path.join(dir, f), Buffer.from(s.d, 'base64')); meta.samples.push({ idx: s.idx, sr: s.sr, ls: s.ls, le: s.le, k: s.k, n: s.n, hdrSr: s.hdrSr, type: s.type, file: f }); }
for (const r of o.renders) { const f = `chrome_render_${r.id}.f32`; fs.writeFileSync(path.join(dir, f), Buffer.from(r.d, 'base64')); meta.renders.push({ id: r.id, n: r.n, file: f }); }
fs.writeFileSync(path.join(dir, 'chrome_samples.json'), JSON.stringify(meta));
console.log(`Chrome (${o.ua}), context rate ${o.ctxRate}: ${meta.samples.length} samples decoded, ${meta.renders.length} renders`);
process.exit(0);
