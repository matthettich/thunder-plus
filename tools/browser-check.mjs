// SAMPLE BROWSER CHECK (5 Nov): the Samples panel in headless Chrome.
//   FOLDER   a folder handle (a stand-in: headless Chrome has no folder picker) with a subfolder: every audio file found,
//            search narrows them, ▼ auditions, Enter loads into the pad
//   FILES    ＋ Files… with two WAVs: both listed in RECENT for this session; one loads
//   RECENT   what was loaded is at the top of RECENT, stereo noted; after a reload it is still there and loads from the
//            app's sample store
// Needs Chrome and playwright: CHROME=/path/to/chrome PLAYWRIGHT=/path/to/playwright/index.mjs
import http from 'http'; import fs from 'fs'; import os from 'os'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PW = process.env.PLAYWRIGHT || ['/opt/node-tools/node_modules/playwright/index.mjs', '/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
// two small WAVs (16-bit): a stereo one with different sides, a mono one
function wav(chs, sr = 44100) {
  const n = chs[0].length, C = chs.length, b = Buffer.alloc(44 + n * C * 2);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * C * 2, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(C, 22);
  b.writeUInt32LE(sr, 24); b.writeUInt32LE(sr * C * 2, 28); b.writeUInt16LE(C * 2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * C * 2, 40);
  for (let i = 0; i < n; i++) for (let c = 0; c < C; c++) b.writeInt16LE(Math.round(chs[c][i] * 32000), 44 + (i * C + c) * 2);
  return b;
}
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'smb-')), N = 22050;
fs.writeFileSync(path.join(dir, 'Kick stereo.wav'), wav([Array.from({ length: N }, (_, i) => Math.sin(i / 10) * (1 - i / N)), Array.from({ length: N }, (_, i) => Math.sin(i / 7) * (1 - i / N))]));
fs.writeFileSync(path.join(dir, 'Hat mono.wav'), wav([Array.from({ length: N / 4 }, (_, i) => Math.sin(i * 1.3) * (1 - 4 * i / N))]));
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.png': 'image/png' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = false;
const say = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };
const url = `http://localhost:${srv.address().port}/index.html?seqtest=1`;
try {
  const c = await browser.newContext({ viewport: { width: 1400, height: 900 } }), page = await c.newPage();
  page.on('pageerror', e => console.error('page error:', e.message));
  await page.goto(url); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(1500);
  const wavs = { kick: [...fs.readFileSync(path.join(dir, 'Kick stereo.wav'))], hat: [...fs.readFileSync(path.join(dir, 'Hat mono.wav'))] };
  // FOLDER: a stand-in handle — Samples/ with Kick stereo.wav, readme.txt and Drums/Hat mono.wav
  const r1 = await page.evaluate(async wavs => {
    const E = s => window.__seqTest.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
    const file = (n, a) => new File([new Uint8Array(a)], n, { type: 'audio/wav' });
    const fh = (n, a) => ({ kind: 'file', name: n, getFile: async () => file(n, a) });
    const dh = (n, kids) => ({ kind: 'directory', name: n, async *entries(){ for (const k of kids) yield [k.name, k]; }, queryPermission: async () => 'granted', requestPermission: async () => 'granted' });
    window.__fakeDir = dh('Samples', [fh('Kick stereo.wav', wavs.kick), fh('readme.txt', [1, 2, 3]), dh('Drums', [fh('Hat mono.wav', wavs.hat)])]);
    E('kit.voices[4].engine = "layers"; kit.voices[5].engine = "layers"; localStorage.removeItem(SMB_K);');
    await E('smbPanel(4)'); E("SMB.dir = window.__fakeDir; SMB.perm = 'granted'; SMB.files = null; SMB.tab = 'folder';"); await E('smbScan()');
    const found = E('SMB.files.map(f => f.path + f.name)');
    E("SMB.q = 'hat'; smbUi();"); const q = E('smbRows().map(r => r.sub + r.name)');
    E("SMB.q = ''; SMB.sel = 0; smbUi();");
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'ArrowDown', bubbles: true })); await wait(400);
    const sel = E('smbRows()[SMB.sel].name'), aud = E('!!SMB.prev');
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true })); await wait(900);
    const v4 = E('(() => { const L = kit.voices[4].layers[kit.voices[4].sel]; return L.sample ? {name:L.sample.name, st:!!L.sample.data2, n:L.sample.data.length} : null; })()');
    return { found, q, sel, aud, v4, recent: E('smbRecent().map(r => r.name + (r.st ? " (st)" : ""))') };
  }, wavs);
  say(JSON.stringify(r1.found) === JSON.stringify(['Drums/Hat mono', 'Kick stereo']), `FOLDER: found ${JSON.stringify(r1.found)} (subfolder read, readme.txt left out)`);
  say(JSON.stringify(r1.q) === JSON.stringify(['Drums/Hat mono']), `FOLDER: search "hat" → ${JSON.stringify(r1.q)}`);
  say(r1.sel === 'Kick stereo' && r1.aud, `FOLDER: ▼ selects ${r1.sel} and auditions it (${r1.aud})`);
  say(r1.v4 && r1.v4.name === 'Kick stereo' && r1.v4.st && r1.v4.n === 22050, `FOLDER: Enter loads it into pad 04: ${JSON.stringify(r1.v4)}`);
  // FILES: two at once go to RECENT for this session
  await page.evaluate(() => window.__seqTest.ev("SMB.tab = 'recent'; smbUi();"));
  await page.setInputFiles('.smbask input[type=file]', [path.join(dir, 'Kick stereo.wav'), path.join(dir, 'Hat mono.wav')]);
  await page.waitForTimeout(300);
  const r2 = await page.evaluate(async () => { const E = s => window.__seqTest.ev(s); const rows = E('smbRows().map(r => r.k + ":" + r.name)');
    const i = E("smbRows().findIndex(r => r.k === 's' && r.name === 'Hat mono')"); E(`SMB.sel = ${i}; smbUi();`); await E(`smbLoad(${i})`); await new Promise(r => setTimeout(r, 900));
    return { rows, recent: E('smbRecent().map(r => r.name + (r.st ? " (st)" : ""))') }; });
  say(r2.rows.filter(x => x.startsWith('s:')).length === 2, `FILES: both added for this session: ${JSON.stringify(r2.rows)}`);
  say(r2.recent[0] === 'Hat mono' && r2.recent.includes('Kick stereo (st)'), `RECENT: newest first, stereo noted: ${JSON.stringify(r2.recent)}`);
  // RECENT after a reload: still listed, loads from the sample store into pad 05
  await page.waitForTimeout(800); await page.reload(); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(1500);
  const r3 = await page.evaluate(async () => { const E = s => window.__seqTest.ev(s); E('kit.voices[5].engine = "layers";'); await E('smbPanel(5)'); E("SMB.tab = 'recent'; smbUi();");
    const rows = E('smbRows().map(r => r.k + ":" + r.name)'); const i = E("smbRows().findIndex(r => r.k === 'r' && r.name === 'Kick stereo')");
    await E(`smbLoad(${i})`); await new Promise(r => setTimeout(r, 900));
    return { rows, v5: E('(() => { const L = kit.voices[5].layers[kit.voices[5].sel]; return L.sample ? {name:L.sample.name, st:!!L.sample.data2, n:L.sample.data.length} : null; })()') }; });
  say(r3.rows.includes('r:Kick stereo') && r3.rows.includes('r:Hat mono') && !r3.rows.some(x => x.startsWith('s:')), `RECENT after a reload: ${JSON.stringify(r3.rows)} (this session's files are gone, as they should be)`);
  say(r3.v5 && r3.v5.name === 'Kick stereo' && r3.v5.st && r3.v5.n === 22050, `RECENT: loads from the store into pad 05: ${JSON.stringify(r3.v5)}`);
  await c.close();
} finally { await browser.close(); srv.close(); fs.rmSync(dir, { recursive: true, force: true }); }
process.exit(failed ? 1 : 0);
