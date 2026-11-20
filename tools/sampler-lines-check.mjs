// SAMPLER CHECK (build 655): a loaded sample at its own pitch, and SYNC TO LINES, in headless Chrome through the app itself.
//   PITCH  a 1000 Hz sample loaded into a synth slot and a drum pad renders at 1000 Hz (C-4 = its own pitch)
//   SHEET  Sync to lines…: a chip, the type-in (2b, 1b8, a bad one), the sentence, Apply sets every instrument of the pad, one Undo
// Needs Chrome and playwright: CHROME=/path/to/chrome PLAYWRIGHT=/path/to/playwright/index.mjs
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';
const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PW = process.env.PLAYWRIGHT || ['/opt/node-tools/node_modules/playwright/index.mjs', '/opt/npm-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.png': 'image/png' };
const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); }); });
await new Promise(ok => srv.listen(0, ok));
const browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = false; const say = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };
try {
  const page = await (await browser.newContext({ viewport: { width: 1400, height: 900 } })).newPage();
  page.on('pageerror', e => { console.error('page error:', e.message); failed = true; });
  await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`, { waitUntil: 'domcontentloaded' }); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(1500);
  const r = await page.evaluate(async () => {
    const E = s => window.__seqTest.ev(s), wait = ms => new Promise(r => setTimeout(r, ms)), out = {};
    E('document.getElementById("view") && 0'); E('S.insts[5].pad = 9; S.insts[6].pad = 9; S.insts[7].pad = 2; S.lpb = 8; S.bpm = 131;');
    for (const [pad, key] of [[9, 'synth'], [2, 'drum']]) {
      E(`kit.voices[${pad}].engine = "layers"; kit.voices[${pad}].layers.forEach(L => L.on = false);`);
      E(`assignSample(${pad}, Float32Array.from({length:44100}, (_, i) => 0.5 * Math.sin(2 * Math.PI * 1000 * i / 44100)), 44100, "t")`);
      await wait(1200);
      out[key] = E(`(() => { const c = ensure(${pad}), d = c.data; let f0 = -1, l = -1, n = 0; for (let i = 1; i < Math.min(d.length, c.sr * 0.3); i++) if (d[i-1] <= 0 && d[i] > 0){ if (f0 < 0) f0 = i; l = i; n++; } return {hz: (n - 1) * c.sr / (l - f0), tune: kit.voices[${pad}].layers[kit.voices[${pad}].sel].tune, vt: kit.voices[${pad}].tune}; })()`);
    }
    E('cur = 9; refreshAll();'); out.btn = E('!document.getElementById("bSyncLines").hidden');
    // the sheet
    E('window.__p = syncToLines(9)'); await wait(100);
    const q = s => document.querySelector(s), chips = [...document.querySelectorAll('.ask .slchips')][0], inp = q('.ask input');
    out.chips = [...chips.children].map(b => b.textContent);
    const clickChip = t => [...chips.children].find(b => b.textContent === t).click();
    clickChip('2 bars (64 lines)'); out.afterChip = {val: inp.value, say: q('.ask .askmsg:nth-of-type(2)') && [...document.querySelectorAll('.ask .askmsg')].map(x => x.textContent)};
    const type = v => { inp.value = v; inp.dispatchEvent(new Event('input', {bubbles: true})); };
    type('1b8'); out.t1b8 = [...document.querySelectorAll('.ask .askmsg')].map(x => x.textContent).find(t => /lines a beat/.test(t));
    type('1.5'); out.bad = {say: [...document.querySelectorAll('.ask .askmsg')].map(x => x.textContent).join('|'), dis: [...document.querySelectorAll('.ask .btnrow button')].find(b => b.textContent === 'Apply').disabled};
    type('24'); [...document.querySelectorAll('.ask .slchips')][1].children[1].click();
    const before = E('JSON.stringify(S)'); [...document.querySelectorAll('.ask .btnrow button')].find(b => b.textContent === 'Apply').click(); await wait(200);
    out.after = E('S.insts.map(I => [I.pad, I.sync, I.lines])').filter(x => x[0] === 9 || x[0] === 2);
    out.toast = document.getElementById('toast').textContent;
    await wait(500); E('undoStep(-1)'); await wait(200);
    out.undone = E('S.insts.map(I => [I.pad, I.sync, I.lines])').filter(x => x[0] === 9);
    return out;
  });
  console.log(JSON.stringify(r, null, 1));
  say(Math.abs(r.synth.hz - 1000) < 3 && r.synth.tune === -24, `synth slot: ${r.synth.hz.toFixed(1)} Hz, layer tune ${r.synth.tune}, voice tune ${r.synth.vt}`);
  say(Math.abs(r.drum.hz - 1000) < 3 && r.drum.tune === 0, `drum pad: ${r.drum.hz.toFixed(1)} Hz`);
  say(r.btn, 'Sync to lines… shows on a sample layer');
  say(r.chips.includes('2 bars (64 lines)') && r.chips.includes('24 lines') !== undefined, 'the sheet lists lengths: ' + r.chips.join(' · '));
  say(r.afterChip.val === '64', 'a chip fills the type-in');
  say(/1 bar \+ 8 lines \(40 lines\) = 5 beats/.test(r.t1b8), '1b8: ' + r.t1b8);
  say(r.bad.dis && /Type lines/.test(r.bad.say), 'a bad entry is refused with a reason');
  say(r.after.filter(x => x[0] === 9).length >= 2 && r.after.filter(x => x[0] === 9).every(x => x[1] === 2 && x[2] === 24), 'Apply: every instrument on pad 9 has 24 lines, STRETCH: ' + JSON.stringify(r.after));
  say(r.after.filter(x => x[0] === 2).every(x => x[2] == null), 'the other pad is untouched');
  say(r.undone.every(x => x[2] == null && x[1] === 0), 'one Undo puts all of them back');
} catch (e) { console.error(e); failed = true; }
await browser.close(); srv.close(); process.exit(failed ? 1 : 0);
