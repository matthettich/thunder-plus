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
  window.__ioStarts = 0; window.__ioOps = []; window.__micAsks = 0; window.__midiSent = [];
  window.thunderDesktop = { io: { available: true, mic() { window.__micAsks++; return Promise.resolve({ ok: true }); }, start() {
    window.__ioStarts++;
    if (mode === 'silent') return Promise.resolve({ ok: true });
    setTimeout(() => {
      const ch = new MessageChannel(), h = ch.port1; let timer = null, frame0 = 0, dead = false, nextH = 1;
      window.__midiDev = {}; const openH = new Set();   // handle -> the physical device it was opened on (a new handle every open, as the addon does); the open ones
      window.__ioKill = () => { dead = true; if (timer) clearInterval(timer); };   // this helper dies: no answer to anything any more
      const stop = () => { if (timer) clearInterval(timer); timer = null; };
      h.onmessage = e => { if (dead) return; const m = e.data; if (m && m.op && !m.__late) window.__ioOps.push(m.op);   // (a delayed answer's replay isn't a second request)
        if (m && m.op === 'devices') h.postMessage({ op: 'devices', id: m.id, devices: [
          { name: 'Built-in', inputs: 2, outputs: 2, backend: 'Core Audio', inId: 'in:bb', outId: 'out:bb', id: 'in:bb', rates: [48000], isDefaultIn: false, isDefaultOut: false },
          { name: '500R8', inputs: 28, outputs: 28, backend: 'Core Audio', inId: 'in:aa', outId: 'out:aa', id: 'in:aa', rates: [44100, 48000], isDefaultIn: true, isDefaultOut: true }] });
        else if (m && m.op === 'now') h.postMessage({ op: 'now', id: m.id, ns: performance.now() * 1e6 });
        else if (m && m.op === 'open' && window.__ioOpenFail) h.postMessage({ op: 'opened', id: m.id, ok: false, why: 'device busy' });
        else if (m && m.op === 'open' && window.__ioOpenDelay && !m.__late) { const d = window.__ioOpenDelay; window.__ioOpenDelay = 0; setTimeout(() => h.onmessage({ data: Object.assign({}, m, { __late: true }) }), d); }   // (the answer comes late: macOS still asking)
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
        } else if (m && m.op === 'close') { stop(); h.postMessage({ op: 'closed', id: m.id }); }
        // FAKE MIDI (phase B): one output "Gear" and one input "Clock"; what's sent is logged with its time
        // PHYSICAL DEVICES: __midiPhys lists the plugged-in "Gear"s (default one, "A"); they are named Gear, Gear #2… in that order
        else if (m && m.op === 'midiPorts') h.postMessage({ op: 'midiPorts', id: m.id, outs: (window.__midiPhys || ['A']).map((p, k) => { const n = k ? 'Gear #' + (k + 1) : 'Gear'; return { id: n, name: n }; }), ins: [{ id: 'Clock', name: 'Clock' }] });
        else if (m && m.op === 'midiStats') h.postMessage({ op: 'midiStats', id: m.id, queued: 0, late: window.__midiLate || 0 });
        else if (m && (m.op === 'midiOpenOut' || m.op === 'midiOpenIn')) { window.__midiOpens = (window.__midiOpens || 0) + 1; const port = nextH++;
          if (m.op === 'midiOpenOut') { const k = m.name === 'Gear' ? 0 : +(/#(\d+)$/.exec(m.name) || [0, 0])[1] - 1; window.__midiDev[port] = (window.__midiPhys || ['A'])[k]; }
          else window.__midiDev[port] = 'clock';
          openH.add(port);
          h.postMessage({ op: 'midiOpened', id: m.id, ok: true, port }); }
        else if (m && (m.op === 'midiSend' || m.op === 'midiClear' || m.op === 'midiClose')) {
          if (m.op === 'midiSend' && !openH.has(m.port)) return;   // (a closed handle sends nothing, as in the addon)
          if (m.op === 'midiClose') openH.delete(m.port);
          window.__midiSent.push({ op: m.op, port: m.port, bytes: m.bytes, at: m.at, t: performance.now() }); } };
      window.__midiIn = list => h.postMessage({ op: 'midiIn', list });
      h.postMessage(mode === 'ok' ? { op: 'hello', available: true, version: 'thunder_io 4 / miniaudio 0.11.26 / libremidi', midi: true } : { op: 'hello', available: false, why: 'addon missing' });
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
    ok(await page.evaluate(() => window.__micAsks) >= 1, 'opening the native input asks for the microphone (thunder:io-mic)');
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
    // CLOSED WHILE OPENING (owner's beta.14 test: distorted monitoring after closing and reopening the window): rcdClose while an open
    // waits leaves nothing open (no input, no monitor on the master, the helper told to close)
    const cwo = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E('rcdClose()'); await wait(100); window.__ioOps.length = 0; window.__ioOpenDelay = 600;
      const p = E('rcdOpen()'); await wait(150); E('rcdClose()'); const r = await p; await wait(300);
      return {r, io:E('!!RCD.io'), node:E('!!RCD.node'), mon:E('!!RCD.mon'), closes:window.__ioOps.filter(o => o === 'close').length}; });
    ok(cwo.r === false && !cwo.io && !cwo.node && !cwo.mon && cwo.closes >= 1, 'closing the window while the input opens leaves nothing half-open: ' + JSON.stringify(cwo));
    // CLOSE REALLY CLEANS UP (CodeRabbit: a comment had swallowed rcdClose's disconnects, so the monitor stayed on the master)
    const cl = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s);
      await E('rcdOpen()'); const mon = E('RCD.mon'), node = E('RCD.node'); let disc = false; const d0 = mon.disconnect.bind(mon); mon.disconnect = (...a) => { disc = true; return d0(...a); };
      E('rcdClose()'); return {disc, onmsg:node ? node.port.onmessage === null : null, mon:E('!!RCD.mon')}; });
    ok(cl.disc && cl.onmsg === true && !cl.mon, 'closing the recorder disconnects its monitor and node: ' + JSON.stringify(cl));
    // RESET AUDIO keeps the monitor: it follows the new master (it stayed on the old one)
    const rs = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      await E('rcdOpen()'); E('rcdSet.mon = 1; rcdMonSet();'); const old = E('master'); E('resetAudio(true)'); await wait(100);
      const an = E('ctx.createAnalyser()'); E('master').connect(an); await wait(400); const a = new Float32Array(2048); an.getFloatTimeDomainData(a);
      let m = 0; for (const v of a) m = Math.max(m, Math.abs(v)); E('rcdSet.mon = 0; rcdMonSet();'); return {newMaster:E('master') !== old, lvl:+m.toFixed(3)}; });
    ok(rs.newMaster && rs.lvl > 0.005, 'after Reset audio engine the monitor still reaches the (new) master: ' + JSON.stringify(rs));
    // ...and reopening straight after the close opens afresh (it doesn't get the old open's "gave up")
    const reo = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E('rcdClose()'); await wait(100); window.__ioOpenDelay = 600; const p1 = E('rcdOpen()'); await wait(150); E('rcdClose()');
      const r2 = await E('rcdOpen()'); const r1 = await p1; return {r1, r2, io:E('!!RCD.io')}; });
    ok(reo.r1 === false && reo.r2 === true && reo.io, 'reopening straight after closing mid-open opens the input afresh: ' + JSON.stringify(reo));
    // ...and a queued open whose window closed too before it began never starts (CodeRabbit)
    const q2 = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      E('rcdClose()'); await wait(100); window.__ioOps.length = 0; window.__ioOpenDelay = 600; const p1 = E('rcdOpen()');
      for (let i = 0; i < 40 && !window.__ioOps.includes('open'); i++) await wait(25); const started = window.__ioOps.includes('open'); E('rcdClose()');
      const p2 = E('rcdOpen()'); await wait(50); E('rcdClose()'); const r = [await p1, await p2]; await wait(300);
      const out = {r, started, opens:window.__ioOps.filter(o => o === 'open').length, io:E('!!RCD.io'), mon:E('!!RCD.mon')}; await E('rcdOpen()'); return out; });
    ok(q2.r.join() === 'false,false' && q2.started && q2.opens === 1 && !q2.io && !q2.mon, 'an open queued behind one whose window closed, and closed itself before starting, never opens: ' + JSON.stringify(q2));
    await page.evaluate(async () => { await window.__seqTest.ev('rcdOpen()'); });
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
    ok(mg.n >= 6 && mg.ts >= mg.n && Math.abs(mg.med - 20) < 6 && mg.closed.n >= 6 && mg.closed.ts >= mg.closed.n && Math.abs(mg.closed.med - 20) < 6 && !mg.closed.ioAfter,   // (the old bugs read ~200)
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
// NATIVE MIDI (phase B) against the fake helper (its own try: a throw here is a failure, and the browser still closes; CodeRabbit)
try { const page = await open('ok');
  const nm = await page.evaluate(async () => { const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
    const o = {};
    o.row = !document.getElementById('midiEngRow').hidden;
    // a saved Web MIDI choice ("web1", named Gear) on port A, and Clock from "web2" (named Clock)
    E("midi.outIds[0] = 'web1'; midi.inId = ''; midi.clkId = 'web2'; midi.names = {web1:'Gear', web2:'Clock'}; midi.clkIn = true;");
    await E('connectMidi()');
    o.native = E('!!(midi.access && midi.access.native)'); o.outs = E('[...midi.access.outputs.keys()].join()'); o.ins = E('[...midi.access.inputs.keys()].join()');
    o.remap = E('midi.outIds[0] + "|" + midi.clkId'); o.mic = window.__micAsks; o.stat = document.getElementById('midiStat').textContent;
    // late messages: the helper says 3 late → the status gets " · 3 late" (nmScan asks midiStats; called now, not in 3 s)
    window.__midiLate = 3; await E('nmScan()'); await wait(100); o.late = document.getElementById('midiStat').textContent;
    // a note booked 200 ms ahead: straight to the helper (no OUTBOX), at the right OS time
    window.__midiSent.length = 0; E('audio()'); await wait(200);
    const t = E('ctx.currentTime + 0.2'); E('msend([0x90, 60, 100], ' + t + ', false, 0)');
    const want = E('a2p(' + t + ' + moFxLat())'), clk = E('IOH.clk'); await wait(50);
    const s1 = window.__midiSent.find(x => x.op === 'midiSend');
    o.sent = s1 ? {port:s1.port, bytes:s1.bytes.join(), errMs:+((s1.at / 1e6 + clk) - want).toFixed(3)} : null; o.outbox = E('MOB.q.length');
    // VALID MESSAGES ONLY (owner's beta.14 test: "Sysex interrupt" on the Cirklon): what Web MIDI would refuse throws and isn't sent
    window.__midiSent.length = 0; const g = E('midi.access.outputs.get("n:Gear")'); const bad = [];
    for (const b of [[0x90, 60, 128], [0xF0, 0x7E, 0xF7], [0x90, 60], [0x3C, 1, 2], [0xF7], [0xB0, 7, 100, 0x90]]) { try { g.send(b); bad.push('sent'); } catch(e){ bad.push('thrown'); } }
    let good = 0; for (const b of [[0x90, 60, 100], [0xF8], [0xB0, 7, 100, 0xC0, 5], [0xF2, 0, 1]]) { try { g.send(b); good++; } catch(e){} }
    await wait(30); o.valid = {bad:bad.join(), good, sent:window.__midiSent.filter(x => x.op === 'midiSend').length};
    // STOP: the queue is cleared first, then the note offs
    window.__midiSent.length = 0; E('mx.used.add(0); moRelease()'); await wait(50);
    o.stop = window.__midiSent.map(x => x.op + (x.bytes ? ':' + x.bytes[0].toString(16) : '')).slice(0, 3).join(',');
    // MIDI IN: clock ticks with the helper's timestamps reach the clock-in (Clock from: the remapped input)
    E('mx.inT = []'); const n0 = performance.now(); window.__midiIn([0, 1, 2, 3, 4, 5].map(k => ({ port: 2, bytes: [0xF8], ns: (n0 - E('IOH.clk') + k * 20.833) * 1e6 })));
    await wait(80); o.clkIn = E('(mx.inT || []).length');
    // SAME-NAME DEVICES (CodeRabbit): "Gear" and "Gear #2"; the first is unplugged, so "Gear #2" is now "Gear": the group is closed
    // and opened again (the survivor mustn't keep the gone device's handle)
    window.__midiPhys = ['A', 'B']; await E('nmScan()'); const two = E('[...midi.access.outputs.keys()].join()');
    E("midi.outIds[0] = 'n:Gear'; midi.outIds[1] = 'n:Gear #2'");   // letter A on the first Gear (device A), B on the second (device B)
    window.__midiSent.length = 0; window.__midiPhys = ['B']; await E('nmScan()');   // device A unplugged: B is now "Gear"
    window.__midiSent.length = 0; E('msend([0x90, 64, 100], null, false, 0); msend([0x90, 65, 100], null, false, 1)'); await wait(30);
    const sends = window.__midiSent.filter(x => x.op === 'midiSend');
    o.dup = {two, after:E('[...midi.access.outputs.keys()].join()'), letters:E('midi.outIds.slice(0, 2).join()'), to:sends.map(x => window.__midiDev[x.port] + ':' + x.bytes[1]).join()};
    window.__midiPhys = null; E("midi.outIds[1] = ''");
    // HELPER DIED: no answer twice → its ports are dropped, MIDI connects again through a fresh helper (a second start)
    const starts = window.__ioStarts; window.__ioKill(); await E('nmScan()'); await E('nmScan()'); await wait(800);
    for (let i = 0; i < 40 && !E('!!(midi.access && midi.access.native && NM.acc)'); i++) await wait(100);
    o.revive = {starts:window.__ioStarts - starts, native:E('!!(midi.access && midi.access.native)'), outs:E('midi.access ? [...midi.access.outputs.keys()].join() : ""')};
    // Engine Browser: the helper's ports are closed
    window.__midiSent.length = 0; E("midiEngineSet('browser')"); await wait(100);
    o.closed = window.__midiSent.filter(x => x.op === 'midiClose').length; o.after = E('midi.access ? (midi.access.native ? "native" : "web") : "none"');
    E("aset.midiEngine = 'native'; asetSave();");
    return o; });
  console.log('native MIDI', JSON.stringify(nm));
  ok(nm.row, 'Settings › MIDI shows the Engine row in the desktop app');
  ok(nm.native && nm.outs === 'n:Gear' && nm.ins === 'n:Clock', 'MIDI connects through the helper: ' + nm.outs + ' / ' + nm.ins);
  ok(nm.remap === 'n:Gear|n:Clock', 'saved Web MIDI choices are mapped to the helper\'s ports by name: ' + nm.remap);
  ok(nm.mic === 0, 'connecting MIDI asks for no microphone');
  ok(/· native/.test(nm.stat), 'the status says native: ' + nm.stat);
  ok(/ · native · 3 late$/.test(nm.late), 'late MIDI messages show in the status: ' + nm.late);
  ok(nm.sent && nm.sent.port === 1 && nm.sent.bytes === '144,60,100' && Math.abs(nm.sent.errMs) < 0.5 && nm.outbox === 0, 'a note booked ahead goes straight to the helper at its OS time (no OUTBOX): ' + JSON.stringify(nm.sent));
  ok(nm.valid.bad === 'thrown,thrown,thrown,thrown,thrown,thrown' && nm.valid.good === 4 && nm.valid.sent === 4, 'malformed MIDI and SysEx are refused as Web MIDI refuses them; valid messages go out: ' + JSON.stringify(nm.valid));
  ok(/^midiClear/.test(nm.stop) && /midiSend:b0/.test(nm.stop), 'STOP clears the helper\'s queue, then sends the offs: ' + nm.stop);
  ok(nm.clkIn >= 5, 'MIDI clock in arrives with its timestamps: ' + nm.clkIn + ' ticks');
  ok(nm.dup.two === 'n:Gear,n:Gear #2' && nm.dup.after === 'n:Gear' && nm.dup.letters === 'n:Gear,n:Gear #2' && nm.dup.to === 'B:64', 'two same-name devices, one unplugged: the group is reopened (a note on "Gear" reaches the device still there, through an open handle) and no choice is moved: ' + JSON.stringify(nm.dup));
  ok(nm.revive.starts === 1 && nm.revive.native && nm.revive.outs === 'n:Gear', 'a helper that died is noticed and MIDI reconnects through a fresh one: ' + JSON.stringify(nm.revive));
  ok(nm.closed === 2 && nm.after !== 'native', 'Engine Browser closes the helper\'s ports: ' + nm.closed + ' closed, now ' + nm.after);
  await page.close(); } catch (e) { console.error(e); fails++; }

await browser.close(); srv.close();
console.log(fails ? fails + ' FAILED' : 'all ok'); process.exit(fails ? 1 : 0);
