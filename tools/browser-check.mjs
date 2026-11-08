// SAMPLE BROWSER CHECK (5 Nov): the Samples panel in headless Chrome.
//   FOLDER   a folder handle (a stand-in: headless Chrome has no folder picker) with a subfolder: every audio file found,
//            search narrows them, ▼ auditions, Enter loads into the pad
//   FILES    ＋ Files… with two WAVs: both listed in RECENT for this session; one loads
//   EDIT     Edit sample… on a looped layer: crop moves Start and the loop with the audio; Apply, Undo; Snap, Invert, To mono
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
  // EDIT SAMPLE: pad 05's stereo sample with a loop; crop off the first 20 %: the loop and Start move with the audio
  const r4 = await page.evaluate(async () => { const E = s => window.__seqTest.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
    if (E('askOpen')) E('askOpen(null)');
    E(`(() => { select(5); const v = kit.voices[5], L = v.layers[v.sel]; L.lpMode = 1; L.lpS = 0.5; L.lpE = 0.75; L.a = 0.1; L.mode = 'ADSR'; })()`);
    await E('rcdPanel(5, {edit:{pad:5, li:kit.voices[5].sel}})');
    const n0 = E('RCD.take.data.length'), hidden = E('RCD.ui.rec.hidden'), mk0 = E('RCD.take.mk');
    E(`RCD.sel = [Math.round(${n0} * 0.2), ${n0}]; rcdUi();`); E("[...document.querySelectorAll('.recops button')].find(b => b.textContent === 'Crop').click()");
    const mk1 = E('RCD.take.mk'), n1 = E('RCD.take.data.length');
    E("[...document.querySelectorAll('.recedit button')].find(b => b.textContent === 'Apply to the layer').click()"); await wait(300);
    const L = E('(() => { const v = kit.voices[5], L = v.layers[v.sel]; return {n:L.sample.data.length, st:!!L.sample.data2, a:L.a, lpS:L.lpS, lpE:L.lpE}; })()');
    // the newly exposed ones: To mono, Snap, Invert (on the editor's take)
    E("[...document.querySelectorAll('.recops button')].find(b => b.textContent === 'To mono').click()"); const mono = E('!RCD.take.data2');
    E("[...document.querySelectorAll('.recops button')].find(b => b.textContent === 'Snap').click()");
    E(`RCD.sel = [1001, 5003]; rcdSnapSel();`); const sel = E('RCD.sel'), d = E('Array.from(RCD.take.data.slice(RCD.sel[0] - 1, RCD.sel[0] + 1))');
    const before = E('RCD.take.data[1234]'); E("[...document.querySelectorAll('.recops button')].find(b => b.textContent === 'Invert').click()"); const after = E('RCD.take.data[1234]');
    // ZOOM: the wheel at a point keeps that frame under the pointer; a click places the cursor and + zooms there; Shift+wheel
    // scrolls; two touch points pinch; a click on the overview moves there; FIT shows it all
    const zoom = await (async () => { const w = document.querySelector('.recwave'), r = w.getBoundingClientRect(), y = r.top + r.height / 2, at = x => r.left + x * r.width;
      const frameAt = x => { const [a, b] = E('rcdView()'); return a + x * (b - a); };
      E('RCD.view = null; RCD.sel = null; RCD.cursor = null; rcdUi();'); const f0 = frameAt(0.3);
      w.dispatchEvent(new WheelEvent('wheel', {deltaY:-300, clientX:at(0.3), clientY:y, bubbles:true, cancelable:true})); const v1 = E('rcdView()'), f1 = frameAt(0.3);
      const pe = (t, x, id, type) => w.dispatchEvent(new PointerEvent(t, {clientX:x, clientY:y, pointerId:id, pointerType:type || 'mouse', isPrimary:id === 1, bubbles:true}));
      pe('pointerdown', at(0.6), 1); pe('pointerup', at(0.6), 1); const cur = E('RCD.cursor');
      E('RCD.ui.edit.querySelectorAll(".recz button")[1].click()'); const [za, zb] = E('rcdView()'), zoomedAtCursor = cur > za && cur < zb && (zb - za) < (v1[1] - v1[0]);
      w.dispatchEvent(new WheelEvent('wheel', {deltaY:200, shiftKey:true, clientX:at(0.5), clientY:y, bubbles:true, cancelable:true})); const [sa] = E('rcdView()'), scrolled = sa > za;
      E('RCD.view = null; rcdUi();'); pe('pointerdown', at(0.4), 11, 'touch'); pe('pointerdown', at(0.6), 12, 'touch'); pe('pointermove', at(0.2), 11, 'touch'); pe('pointermove', at(0.8), 12, 'touch');
      pe('pointerup', at(0.2), 11, 'touch'); pe('pointerup', at(0.8), 12, 'touch'); const [pa, pb] = E('rcdView()'), n = E('RCD.take.data.length'), pinched = (pb - pa) < n * 0.4 && (pb - pa) > n * 0.25;
      const o = document.querySelector('.recov'), orc = o.getBoundingClientRect(); o.dispatchEvent(new PointerEvent('pointerdown', {clientX:orc.left + orc.width * 0.9, clientY:orc.top + 5, pointerId:3, bubbles:true}));
      o.dispatchEvent(new PointerEvent('pointerup', {clientX:orc.left + orc.width * 0.9, clientY:orc.top + 5, pointerId:3, bubbles:true})); const [oa, ob] = E('rcdView()'), moved = (oa + ob) / 2 > n * 0.6;
      E('RCD.ui.edit.querySelectorAll(".recz button")[2].click()'); const fit = E('RCD.view === null');
      return {px:(v1[1] - v1[0]) / r.width, f0, f1, zoomedIn:(v1[1] - v1[0]) < E('RCD.take.data.length'), cur, zoomedAtCursor, scrolled, pinched, moved, fit}; })();
    E("askOpen && askOpen(null)"); const cleared = E('RCD.take === null');
    await wait(1200); E('undoStep(-1)'); await wait(400);
    const undone = E('(() => { const v = kit.voices[5], L = v.layers[v.sel]; return L.sample.data.length; })()');
    return {n0, hidden, mk0, mk1, n1, L, mono, sel, d, before, after, cleared, undone, zoom}; });
  const near = (a, b) => Math.abs(a - b) < 2e-4;
  say(r4.hidden && JSON.stringify(r4.mk0) === JSON.stringify([2205, 11025, 16538]), `EDIT: opens on the layer's sample without the record button; markers ${JSON.stringify(r4.mk0)} (Start, Loop start, Loop end)`);
  say(r4.n1 === 17640 && JSON.stringify(r4.mk1) === JSON.stringify([0, 6615, 12128]), `EDIT: crop the first 20 %: ${r4.n1} frames, markers move with the audio ${JSON.stringify(r4.mk1)}`);
  say(r4.L.n === 17640 && r4.L.st && r4.L.a === 0 && near(r4.L.lpS, 6615 / 17640) && near(r4.L.lpE, 12128 / 17640), `EDIT: Apply writes it back: ${JSON.stringify(r4.L)}`);
  say(r4.mono, 'EDIT: To mono leaves one channel');
  say(r4.sel[0] !== 1001 && Math.sign(r4.d[0]) !== Math.sign(r4.d[1]) || r4.d[1] === 0, `EDIT: Snap moves the selection to zero crossings: ${JSON.stringify(r4.sel)} (around the start: ${r4.d.map(x => x.toFixed(4))})`);
  say(Math.abs(r4.before + r4.after) < 1e-9 && r4.before !== 0, `EDIT: Invert flips the polarity (${r4.before.toFixed(4)} → ${r4.after.toFixed(4)})`);
  { const z = r4.zoom; say(z.zoomedIn && Math.abs(z.f0 - z.f1) < z.px, `ZOOM: the wheel zooms in at the pointer (frame there ${Math.round(z.f0)} → ${Math.round(z.f1)}, within a pixel: ${z.px.toFixed(1)} frames)`);
    say(z.cur != null && z.zoomedAtCursor, `ZOOM: a click places the cursor (${z.cur}) and + zooms in around it`);
    say(z.scrolled, 'ZOOM: Shift+wheel scrolls'); say(z.pinched, 'ZOOM: two fingers pinching apart zoom in'); say(z.moved, 'ZOOM: a click on the overview moves the view there'); say(z.fit, 'ZOOM: FIT shows the whole take'); }
  say(r4.cleared, 'EDIT: closing lets the edit go (Record a sample… starts clean)');
  say(r4.undone === 22050, `EDIT: the Synth page's Undo takes the Apply back (${r4.undone} frames)`);
  // KEYS (owner, 8 Nov, desktop app): Space plays / stops even with a menu button focused (it used to open the menu); solo and
  // mute keys work on the Synth page too. Real key presses through the page's keyboard, focus set the way a click leaves it.
  { const E = s => page.evaluate(x => window.__seqTest.ev(x), s), wait = ms => page.waitForTimeout(ms);
    await E("setView('tracker'); if (tp.on) tStop();"); await wait(300);
    const q = async () => ({ on: await E('!!tp.on'), file: await E("!$('gfile').hidden"), edit: await E("!$('gedit').hidden"), focus: await E('document.activeElement && document.activeElement.id') });
    await page.focus('#bFile'); const f0 = await q(); await page.keyboard.press('Space'); await wait(500); const f1 = await q();
    say(f0.focus === 'bFile' && f1.on && !f1.file && f1.focus === 'bFile', `KEYS: Space on the focused File ▾ button starts the song and leaves the menu closed (${JSON.stringify(f1)})`);
    await page.keyboard.press('Space'); await wait(500); const f2 = await q();
    say(!f2.on && !f2.file, `KEYS: Space again stops it (${JSON.stringify(f2)})`);
    await page.focus('#bEdit'); await page.keyboard.press('Space'); await wait(500); const f3 = await q(); await page.keyboard.press('Space'); await wait(500);
    say(f3.on && !f3.edit && !(await q()).on, `KEYS: the same on Edit ▾ (${JSON.stringify(f3)})`);
    await page.keyboard.press('ArrowDown'); await wait(200); const f4 = await q();
    say(f4.file || f4.edit, 'KEYS: ▼ on a focused menu button still opens it');
    await E("gfileToggle(false); geditToggle(false);");
    await E("setView('synth'); document.activeElement && document.activeElement.blur(); S.mix.forEach(m => { m.solo = false; m.mute = false; });"); await wait(200);
    const t = await E('dkTrack()');
    await page.keyboard.press('Control+/'); await wait(200); const s1 = await E('S.mix.map(m => m.solo)');
    say(s1[t] === true && s1.filter(Boolean).length === 1, `KEYS: Ctrl+/ on the Synth page solos the current track (${t + 1}) ${JSON.stringify(s1.slice(0, 4))}`);
    await page.keyboard.press('Control+Shift+/'); await wait(200); const s2 = await E('S.mix.some(m => m.solo)');
    say(s2 === false, 'KEYS: Ctrl+Shift+/ on the Synth page clears every solo');
    await page.keyboard.press('/'); await wait(200); const m1 = await E('S.mix.map(m => m.mute)');
    say(m1[t] === true && m1.filter(Boolean).length === 1, `KEYS: / on the Synth page mutes the current track (${t + 1}) ${JSON.stringify(m1.slice(0, 4))}`);
    await page.keyboard.press('\\'); await wait(200); const m2 = await E('S.mix.some(m => m.mute)');
    say(m2 === false, 'KEYS: \\ unmutes it again');
    await E("setView('tracker')"); }
  await c.close();
} finally { await browser.close(); srv.close(); fs.rmSync(dir, { recursive: true, force: true }); }
process.exit(failed ? 1 : 0);
