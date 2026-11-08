// NATIVE INPUT ENGINE, the page's side against a FAKE helper (no Electron, no native addon): a stand-in for
// window.thunderDesktop.io whose start() hands the page a MessagePort that plays the helper (hello, devices, now, open,
// a stream of 28-channel chunks where channel c is the constant 0.01*(c+1), close).
//   - Settings › Audio has no 'Input engine' text any more (desktop and website)
//   - desktop, Record a sample: Engine row (Native selected), Input lists "500R8 (native, 28 inputs)", Inputs lists every
//     pair and channel, no "(all the app is given)"
//   - IN 17+18 recorded FREE: the take's left / right are channels 17 and 18 (0.17 / 0.18)
//   - Engine: Browser lists the browser's inputs again and closes the helper's input
//   - a helper that says available:false: the browser's list and one line saying why
//   - the website (no thunderDesktop): no Engine row, no 'Input engine' text, nothing throws
//
//   node tools/io-check.mjs        CHROME=… PLAYWRIGHT=… as for tools/seq-golden.mjs
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PW = process.env.PLAYWRIGHT || ['/opt/node-tools/node_modules/playwright/index.mjs', '/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.webmanifest': 'application/manifest+json' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const port = srv.address().port;


// The fake, in the page before the app runs. mode: 'ok' | 'off' (hello available:false) | 'silent' (no port at all)
const FAKE = mode => {
  window.__ioStarts = 0; window.__ioOps = [];
  window.thunderDesktop = { io: { available: true, start() {
    window.__ioStarts++;
    if (mode === 'silent') return Promise.resolve({ ok: true });
    setTimeout(() => {
      const ch = new MessageChannel(), h = ch.port1; let timer = null, frame0 = 0;
      const stop = () => { if (timer) clearInterval(timer); timer = null; };
      h.onmessage = e => { const m = e.data; if (m && m.op) window.__ioOps.push(m.op);
        if (m && m.op === 'devices') h.postMessage({ op: 'devices', id: m.id, devices: [
          { name: 'Built-in', inputs: 2, outputs: 2, backend: 'Core Audio', inId: 'in:bb', outId: 'out:bb', id: 'in:bb', rates: [48000], isDefaultIn: false, isDefaultOut: false },
          { name: '500R8', inputs: 28, outputs: 28, backend: 'Core Audio', inId: 'in:aa', outId: 'out:aa', id: 'in:aa', rates: [44100, 48000], isDefaultIn: true, isDefaultOut: true }] });
        else if (m && m.op === 'now') h.postMessage({ op: 'now', id: m.id, ns: performance.now() * 1e6 });
        else if (m && m.op === 'open' && window.__ioOpenFail) h.postMessage({ op: 'opened', id: m.id, ok: false, why: 'device busy' });
        else if (m && m.op === 'open') {
          const rate = m.rate; stop(); frame0 = 0;
          h.postMessage({ op: 'opened', id: m.id, ok: true, rate, channels: 28, deviceRate: window.__ioDevRate || rate, block: 256, latencyFrames: 96, latencyMs: 2, name: '500R8' });
          // A DEVICE'S CLOCK: frames at the true rate (as much as real time has made), each stamped on that steady clock (t0 + frame /
          // rate), as the addon does; a stall stops the stream and restarts the clock after it (the stale tests)
          let t0 = performance.now(), stalled = false;
          timer = setInterval(() => { if (window.__ioStall) { stalled = true; return; } const now = performance.now();
            if (stalled) { t0 = now - frame0 / rate * 1000; stalled = false; }
            const frames = Math.min(4096, Math.floor((now - t0) / 1000 * rate) - frame0); if (frames < 64) return;
            const data = new Float32Array(frames * 28);
            for (let f = 0; f < frames; f++) for (let c = 0; c < 28; c++) data[f * 28 + c] = 0.01 * (c + 1);
            // FAKE GEAR (Measure): a 3 ms burst on channels 1–2 at each OS time in __gearT (ms, performance.now's clock)
            const h0 = t0 + frame0 / rate * 1000; for (const g of (window.__gearT || [])) { const f1 = Math.round((g - h0) / 1000 * rate);
              for (let f = Math.max(0, f1); f < Math.min(frames, f1 + 144); f++) { const v = 0.9 * (1 - (f - f1) / 144); data[f * 28] = v; data[f * 28 + 1] = v; } }
            h.postMessage({ op: 'in', frame0, hostNs: h0 * 1e6, channels: 28, frames, data, overruns: 0 }, [data.buffer]);
            frame0 += frames;
          }, 5);
        } else if (m && m.op === 'close') { stop(); h.postMessage({ op: 'closed', id: m.id }); } };
      h.postMessage(mode === 'ok' ? { op: 'hello', available: true, version: 'thunder_io 1 / miniaudio 0.11.26' } : { op: 'hello', available: false, why: 'addon missing' });
      window.__ioPort = h; window.postMessage({ thunderIo: true }, '*', [ch.port2]);
    }, 20);
    return Promise.resolve({ ok: true });
  } } };
};

let fails = 0;
const ok = (c, msg) => { console.log((c ? 'ok   ' : 'FAIL ') + msg); if (!c) fails++; };
const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required', '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'] });
async function open(mode) {
  const page = await (await browser.newContext({ permissions: ['microphone'], viewport: { width: 1400, height: 900 } })).newPage();
  page.on('pageerror', e => { console.error('page error:', e.message); fails++; });
  page.on('console', m => { if (m.type() === 'error' && !/Failed to load resource/.test(m.text())) { console.error('console error:', m.text()); fails++; } });
  await page.addInitScript(() => { try { if (!localStorage.getItem('thunderplus-welcome-v1')) { localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); } } catch (e) {} });
  if (mode !== null) await page.addInitScript(FAKE, mode);
  await page.goto(`http://localhost:${port}/index.html?seqtest=1`);
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
  await page.waitForTimeout(800);
  return page;
}
const openAudio = async page => { await page.click('#bAud'); await page.waitForFunction(() => !document.getElementById('pAudio').hidden && document.getElementById('pAudio').offsetParent, null, { timeout: 5000 }); };
const openRec = async page => { await page.evaluate(() => window.__seqTest.ev('rcdPanel(0)')); await page.waitForSelector('.recask .recset', { timeout: 5000 }); };
// the window's rows: label -> {text, options[], value (select), on (seg)}
const rows = page => page.evaluate(() => { const o = {}; document.querySelectorAll('.recset > label').forEach(f => { const k = f.children[0].textContent, c = f.children[1];
  o[k] = c.tagName === 'SELECT' ? { opts: [...c.options].map(x => x.text), val: c.options[c.selectedIndex] && c.options[c.selectedIndex].text, dis: c.disabled } : { on: [...c.querySelectorAll('button.on')].map(b => b.textContent) }; }); return o; });
const until = async (page, f) => { for (let i = 0; i < 50; i++) { const r = await rows(page); if (f(r)) return r; await page.waitForTimeout(100); } return rows(page); };
const noEngine = page => page.evaluate(() => /input engine/i.test(document.getElementById('pAudio').textContent + document.getElementById('pAudio').innerHTML));

try {
  // desktop, native
  { const page = await open('ok');
    ok(await page.evaluate(() => window.__ioStarts) === 0, 'the helper is not started at page load');
    await openAudio(page); await page.waitForTimeout(500);
    ok(!(await noEngine(page)), "desktop: no 'Input engine' text in Settings › Audio");
    ok(await page.evaluate(() => window.__ioStarts) === 0, 'desktop: opening Settings › Audio does not start the helper');
    await page.click('#bAud'); await page.waitForFunction(() => document.getElementById('pAudio').hidden || !document.getElementById('pAudio').offsetParent);
    await openRec(page);
    const r = await until(page, r => r.Input && r.Input.opts.some(t => /native/.test(t)));
    ok(r.Engine && r.Engine.on.join() === 'Native', 'Engine row shows Native selected: ' + JSON.stringify(r.Engine));
    ok(r.Input && r.Input.val === '500R8 (native, 28 inputs)', 'Input: ' + (r.Input && r.Input.opts.join(' | ')));
    ok(r.Inputs && r.Inputs.opts.includes('IN 27+28') && r.Inputs.opts.includes('IN 28 (mono)') && r.Inputs.opts.length === 14 + 28, 'Inputs lists every pair and channel (' + (r.Inputs && r.Inputs.opts.length) + ')');
    ok(!/all the app is given/.test(JSON.stringify(r)), "no '(all the app is given)' on the native path");
    ok(await page.evaluate(() => window.__ioStarts) === 1, 'the helper was started once, by the window');
    // METERS AT ONCE (owner, 7 Nov): the native input opens with the window, before Record
    ok(await until(page, () => true, 50) && await page.evaluate(async () => { for (let i = 0; i < 40 && !window.__ioOps.includes('open'); i++) await new Promise(r => setTimeout(r, 50)); return window.__ioOps.includes('open'); }), 'the native input opens with the window (meters before Record)');
    // IN 17+18, a FREE take of ~0.5 s
    await page.selectOption('.recset label:has(> span:text-is("Inputs")) select', 'p16');
    const take = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E("rcdSet.mode = 'free'; rcdSet.ch = 'stereo'; rcdSet.lat = 0; RCD.take = null;");
      await E('rcdStart()'); await wait(500); E('rcdStop()');
      for (let i = 0; i < 80 && !E('!!RCD.take'); i++) await wait(50);
      const lat = document.querySelector('.recset b') && [...document.querySelectorAll('.recset b')].map(b => b.textContent).join('|');
      return E('RCD.take ? (() => { const t = RCD.take, n = t.data.length; let l = 0, r = 0; for (let i = 0; i < n; i++){ l = Math.max(l, t.data[i]); r = Math.max(r, t.data2 ? t.data2[i] : NaN); } return {n, sr:t.sr, l, r, io:!!RCD.io, lat:' + JSON.stringify(lat) + '}; })() : null'); });
    ok(take && take.n > take.sr * 0.3 && Math.abs(take.l - 0.17) < 0.005 && Math.abs(take.r - 0.18) < 0.005, 'IN 17+18 FREE take: ' + JSON.stringify(take));
    ok(take && take.io && /native: placed by its timestamps/.test(take.lat), 'latency line says native once the input is open: ' + (take && take.lat));
    // STALE INPUT (owner, 7 Nov: "you have to close the recording window to get access to the inputs again"): the stream stops
    // under the window; the next Record opens it again and records
    const again = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      window.__ioStall = true; await wait(2300); window.__ioStall = false; window.__ioOps.length = 0;
      E("RCD.take = null;"); await E('rcdStart()'); await wait(500); E('rcdStop()');
      for (let i = 0; i < 80 && !E('!!RCD.take'); i++) await wait(50);
      return {ops:window.__ioOps.slice(), take:E('RCD.take ? (() => { const t = RCD.take; let l = 0; for (const v of t.data) l = Math.max(l, v); return {n:t.data.length, l}; })() : null')}; });
    ok(again.ops.includes('close') && again.ops.includes('open') && again.take && Math.abs(again.take.l - 0.17) < 0.005, 'a stopped input is reopened by the next Record, which records: ' + JSON.stringify(again));
    // ONE OPEN (owner's beta.4 test: 4–8 microphone prompts, "wouldn't open: no answer"): two opens at once are one open; an input
    // that hasn't sent audio yet (macOS still asking for the microphone) isn't reopened as stale
    const once = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E('rcdClose()'); await wait(100); window.__ioOps.length = 0; window.__ioStall = true;
      await Promise.all([E('rcdOpen()'), E('rcdOpen()'), E('rcdOpen()')]);
      await wait(2500); await E('rcdOpen()'); const ops = window.__ioOps.filter(o => o === 'open' || o === 'close');
      window.__ioStall = false; await wait(300); return {ops, got:E('!!(RCD.io && RCD.io.got)')}; });
    ok(once.ops.join() === 'open' && once.got, 'three opens at once open the interface once; no reopen while it waits for its first audio: ' + JSON.stringify(once));
    // NO SILENT FALLBACK (owner, 7 Nov: "kicking the inputs back to just 1&2"): a native open that fails says why and records
    // nothing; it never drops to the browser's input (inputs 1–2) on its own
    const nofb = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E('rcdClose()'); window.__ioOpenFail = true; const ok = await E('rcdOpen()'); window.__ioOpenFail = false; await wait(100);
      return {ok, stream:E('!!RCD.stream'), io:E('!!RCD.io'), note:E('RCD.note || ""')}; });
    ok(nofb.ok === false && !nofb.stream && !nofb.io && /device busy/.test(nofb.note), 'a failed native open says why and does not fall back to inputs 1–2: ' + JSON.stringify(nofb));
    await page.evaluate(async () => { await window.__seqTest.ev('rcdOpen()'); });
    // UNPLUGGED MID-TAKE (A2): the helper says the device went away during a FREE take: the take so far is kept, the input closes
    const unp = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E("RCD.take = null;"); await E('rcdStart()'); await wait(400);
      window.__ioStall = true; window.__ioPort.postMessage({op:'closed', why:'the interface went away (unplugged or switched off)'});
      for (let i = 0; i < 60 && !E('!!RCD.take'); i++) await wait(50);
      await wait(300); window.__ioStall = false;
      return {take:E('RCD.take ? RCD.take.data.length : 0'), sr:E('ctx.sampleRate'), io:E('!!RCD.io'), node:E('!!RCD.node')}; });
    ok(unp.take > unp.sr * 0.2 && !unp.io && !unp.node, 'unplugged mid-take: the take so far is kept and the input closed: ' + JSON.stringify(unp));
    // RATE (A2): the interface at 44.1 kHz with the app at 48: the window says so
    const rate = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      window.__ioDevRate = E('ctx.sampleRate') === 44100 ? 48000 : 44100; E('rcdClose()'); await E('rcdOpen()'); E('rcdUi()'); await wait(100); window.__ioDevRate = 0;
      return {note:E('RCD.note || ""'), lat:[...document.querySelectorAll('.recset b')].map(b => b.textContent).join('|')}; });
    ok(/runs at (44\.1|48) kHz and the app at/.test(rate.note) && /converted/.test(rate.note), 'a rate mismatch is said: ' + rate.note);
    ok(/2\.0 ms interface latency included/.test(rate.lat), 'the interface latency is shown: ' + rate.lat);
    // MONITOR AFTER A TAKE (owner's beta.5 test: silent after a PATTERN take until Monitor OFF → ON): a monitor level left at 0 by a
    // stuck curve is set back at the take's end, and the monitor plays the input again
    const mn = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      for (let i = 0; i < 60 && !E('!!RCD.io'); i++) await wait(50);
      E("rcdSet.mon = 1; rcdSet.mode = 'free'; rcdSet.inCh = 'p16'; rcdRoute(); window.__an = ctx.createAnalyser(); RCD.mon.connect(window.__an);");
      const lvl = () => E("(() => { const a = new Float32Array(2048); window.__an.getFloatTimeDomainData(a); let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); return +m.toFixed(3); })()");
      E('RCD.mon.gain.cancelScheduledValues(0); RCD.mon.gain.setValueAtTime(0, ctx.currentTime);'); await wait(300); const stuck = lvl();
      E('RCD.take = null'); await E('rcdStart()'); await wait(300); E('rcdStop()'); for (let i = 0; i < 80 && !E('!!RCD.take'); i++) await wait(50);
      await wait(400); const after = lvl(); E('rcdSet.mon = 0; rcdMonSet();'); return {stuck, after}; });
    ok(mn.stuck === 0 && mn.after > 0.15, 'monitoring comes back after a take even if its level was stuck at 0: ' + JSON.stringify(mn));
    // MEASURE'S INPUTS (owner, 7 Nov: "Measure needs the ability to change inputs"): the list is the native input's 28 channels,
    // and picking one sets the recorder's pair
    const ms = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E("midi.access = midi.access || {outputs:new Map(), inputs:new Map()}; midi.access.outputs.set('fake', {id:'fake', name:'Fake gear', send(){}}); midi.outIds[0] = 'fake';"); E('midiLatPanel()'); await wait(400);
      const sel = [...document.querySelectorAll('.genask label')].find(l => l.firstChild && l.firstChild.textContent === 'Inputs');
      if (!sel) return null; const s = sel.querySelector('select'); const n = s.options.length;
      s.value = 's11'; s.dispatchEvent(new Event('change')); const after = E('rcdSet.inCh');
      [...document.querySelectorAll('.genask button')].find(b => b.textContent === 'Done').click();
      E("rcdSet.inCh = 'p16'; rcdRoute(); midi.outIds[0] = ''; midi.access.outputs.delete('fake');"); return {n, after, txt:s.options[0].textContent}; });
    ok(ms && ms.n === 14 + 28 && ms.after === 's11', "Measure… has an Inputs list (the native input's pairs and channels) that sets the recorder's pick: " + JSON.stringify(ms));
    await openRec(page);   // (Measure's window closed Record a sample)
    // MEASURE WITHOUT THE CLICK ON THE INPUT (owner, 7 Nov: "Measure isn't seeing input even though I see the meters move"): the fake
    // gear sounds 20 ms after each note's MIDI time, only the gear reaches IN 1+2; the native timestamps time it
    const mg = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      window.__gearT = []; E("midi.access = midi.access || {outputs:new Map(), inputs:new Map()}; midi.access.outputs.set('fake', {id:'fake', name:'Fake gear', send(b, ts){ if ((b[0] & 0xF0) === 0x90 && b[2]) window.__gearT.push((ts || performance.now()) + 20); }}); midi.outIds[0] = 'fake'; midi.lats[0] = 0; rcdSet.inCh = 'p0'; rcdRoute();");
      for (let i = 0; i < 60 && !E('!!RCD.io'); i++) await wait(50);
      const r = await E('midiLatRun(0, 0, 60, 8)'); window.__gearT = [];
      // and from Settings (Record a sample closed: the input opens for Measure and closes before the analysis; CodeRabbit, PR #14)
      E('askOpen && askOpen(null)'); for (let i = 0; i < 20 && E('!!RCD.ui'); i++) await wait(50);
      const r2 = await E('midiLatRun(0, 0, 60, 8)'); window.__gearT = []; const io2 = E('!!RCD.io');
      E("midi.outIds[0] = ''; midi.access.outputs.delete('fake'); rcdSet.inCh = 'p16'; rcdRoute();");
      const md = q => { const v = q.map(x => x.ms).sort((a, b) => a - b); return v.length ? +v[v.length >> 1].toFixed(1) : null; };
      const closed = {n:r2.length, ts:r2.ts, med:md(r2), ioAfter:io2};
      const v = r.map(q => q.ms).sort((a, b) => a - b); return {closed, all:v.map(x=>+x.toFixed(0)).join(','), n:r.length, ts:r.ts, med:v.length ? +v[v.length >> 1].toFixed(1) : null}; });
    ok(mg.n >= 6 && mg.ts === mg.n && Math.abs(mg.med - 20) < 6 && mg.closed.n >= 6 && mg.closed.ts === mg.closed.n && Math.abs(mg.closed.med - 20) < 6 && !mg.closed.ioAfter,   // (the old bugs read ~200)
       'Measure… times the gear by the native timestamps when the click isn\'t on the input, with Record a sample open and from Settings (gear 20 ms late): ' + JSON.stringify(mg));
    await openRec(page);   // (Measure from Settings closed Record a sample)
    await page.evaluate(async () => { const E = s => window.__seqTest.ev(s); E('rcdClose()'); await E('rcdOpen()'); });
    // Engine: Browser
    await page.evaluate(() => { window.__ioOps.length = 0; });
    await page.locator('.recset .seg').first().getByText('Browser', { exact: true }).click();
    const b = await until(page, r => r.Input && r.Input.opts[0] === 'Default input');
    ok(b.Input.opts[0] === 'Default input' && !b.Input.opts.some(t => /native/.test(t)), 'Browser: the Input list is the browser’s: ' + b.Input.opts.join(' | '));
    ok(b.Engine.on.join() === 'Browser', 'Browser selected');
    ok((await page.evaluate(() => window.__ioOps)).includes('close'), "Browser: 'close' sent to the helper");
    ok(await page.evaluate(() => window.__seqTest.ev('aset.inEngine')) === 'browser', 'Browser saved in the settings');
    await page.close(); }
  // unavailable
  { const page = await open('off'); await openRec(page);
    const r = await until(page, r => r.Input && r.Input.opts.length);
    ok(r.Input.opts[0] === 'Default input', 'helper unavailable: the browser’s Input list');
    const st = await page.evaluate(() => document.querySelector('.recstate').textContent);
    ok(st === 'Native input not available (addon missing): using the browser’s input.', 'reason shown: "' + st + '"'); await page.close(); }
  // the website
  { const page = await open(null); await openAudio(page); await page.waitForTimeout(300);
    ok(!(await noEngine(page)), "website: no 'Input engine' text in Settings › Audio");
    await page.click('#bAud'); await page.waitForFunction(() => document.getElementById('pAudio').hidden || !document.getElementById('pAudio').offsetParent);
    await openRec(page); const r = await rows(page);
    ok(!r.Engine && r.Input && r.Inputs, 'website: no Engine row in Record a sample (' + Object.keys(r).join(', ') + ')');
    const txt = await page.evaluate(() => ({ t: /input engine/i.test(document.body.innerText), io: window.thunderDesktop || null }));
    ok(!txt.t, "website: no 'Input engine' text on the page"); ok(txt.io === null, 'website: no thunderDesktop'); await page.close(); }
} catch (e) { console.error(e); fails++; }
await browser.close(); srv.close();
console.log(fails ? fails + ' FAILED' : 'all ok'); process.exit(fails ? 1 : 0);
