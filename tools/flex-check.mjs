// FLEX CHECK (3 Nov): does a FLX command play the Flex pattern it names, on both sequencers?
// Plays a test song in headless Chrome: track 1 has a Flex in slot 1 (its own pattern OFF) and a FLX command at the top of
// every bar, each bar a different pattern. A steady test signal (a saw on the left, noise on the right) goes straight into
// the track's effect node; its output and the beat clock the effects hear are recorded. The same input and clock are then
// run through FlexCore itself (the DSP the worklet runs) with the pattern each bar should play, and the two compared bar by
// bar. A bar whose output differs from the reference is a FLX that doesn't play its pattern (wrong value reaching the
// effect, a clamped parameter, the clock off, a value that sticks).
//
//   node tools/flex-check.mjs            Sequencer: Page, then Audio thread
//   node tools/flex-check.mjs --page     one of them
//   node tools/flex-check.mjs --core
//   node tools/flex-check.mjs --wait     also: before the first FLX the slot is off (PATTERN set, WAITS FOR FLX on)
//
// The Page sequencer books from the main thread, so a cold first bar can land a block late (one bar a few dB off, gone on
// a second run); the audio thread is exact.
// Needs Chrome and playwright: CHROME=/path/to/chrome PLAYWRIGHT=/path/to/playwright/index.mjs
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PW = process.env.PLAYWRIGHT || ['/opt/node-tools/node_modules/playwright/index.mjs', '/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
const MODES = process.argv.includes('--page') ? ['page'] : process.argv.includes('--core') ? ['core'] : ['page', 'core'];
// The patterns: the original sixteen, then two of every 256-family (the second digit 2 and 9), and the last one
const PATS = [...Array.from({ length: 16 }, (_, i) => i + 1), ...Array.from({ length: 15 }, (_, i) => [(i + 1) * 16 + 2, (i + 1) * 16 + 9]).flat(), 0x76, 0x77, 0x78, 0x79, 0xE8, 0xFF];   // + the FLX CHECK 2 fixes (5 Nov)

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.png': 'image/png' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = false;
try {
  for (const mode of MODES) {
    const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
    page.on('pageerror', e => console.error('page error:', e.message));
    await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`);
    await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
    await page.waitForTimeout(1500);
    const res = await page.evaluate(async ([mode, PATS, WAIT]) => {
      const T = window.__seqTest, E = s => T.ev(s);
      // the song: 180 BPM, LPB 4, 16 rows a bar; one FLX at the top of every bar (bars 0–2 FLX 00, or none with --wait)
      const BPM = 180, list = [0, 0, 0, ...PATS, 0];
      const j = E('freshSong()'); j.bpm = BPM; j.lpb = 4; j.name = 'flex check';
      const FI = E("FXC.indexOf('FLX')"), FX0 = E('FX0');
      list.forEach((k, b) => { const ph = E('freshPhrase()'); if (!(WAIT && (b < 3 || b === 4))) { ph[0][FX0] = FI; ph[0][FX0 + 1] = k; } j.phrases[b] = ph; j.plen[b] = 16;
        const sect = Array.from({ length: 16 }, () => [-1, 0]); sect[0] = [b, 0]; j.chains[b] = sect; j.song[b][0] = b; });
      // with --wait: bar 4 has no FLX of its own (bar 3's carries on), for the CHASE pass below
      const kAt = b => { if (!WAIT) return list[b] ?? 0; if (b < 3) return 0; if (b === 4) return list[3]; return list[b] ?? 0; };
      await T.load(JSON.stringify(j), 'flex-check.thunder');
      const coreOk = await T.coreOn(mode === 'core');
      E("S.tfx[0] = S.tfx[0] || []; S.tfx[0][0] = freshTfx('flex'); S.tfx[0][0].pat = " + (WAIT ? 5 : 0) + '; S.tfx[0][0].wait = ' + (WAIT ? 1 : 0) + '; saveS(); rtInit(); rtFx(0);');
      await new Promise(r => setTimeout(r, 400));
      const ac = E('ctx'), sr = ac.sampleRate, pl = E('rt.pl'), fx = pl.buses[0].fx;
      if (!fx) return { err: 'no effect node on track 1' };
      // the recorder: 5 channels (in L, in R, out L, out R, clock), whole render quanta, from 'go' until 'end'
      const code = `registerProcessor('flexrec', class extends AudioWorkletProcessor { constructor(){ super(); this.on = false; this.b = []; this.port.onmessage = e => { if (e.data === 'go'){ this.b = []; this.on = true; } else { this.on = false; this.port.postMessage(this.b); this.b = []; } }; }
        process(i){ if (this.on && i[0] && i[0].length === 5) this.b.push(i[0].map(c => c.slice())); return true; } });`;
      await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([code], { type: 'application/javascript' })));
      const rec = new AudioWorkletNode(ac, 'flexrec', { numberOfInputs: 1, numberOfOutputs: 0, channelCount: 5, channelCountMode: 'explicit', channelInterpretation: 'discrete' });
      const mg = ac.createChannelMerger(5); mg.connect(rec);
      const buf = ac.createBuffer(2, sr * 2, sr), L = buf.getChannelData(0), R = buf.getChannelData(1); let s = 12345;
      for (let i = 0; i < L.length; i++) { L[i] = 0.3 * (((i * 110 / sr) % 1) * 2 - 1); s = (s * 1103515245 + 12345) >>> 0; R[i] = 0.2 * (s / 2147483648 - 1); }
      const src = ac.createBufferSource(); src.buffer = buf; src.loop = true; src.start();
      const sp = ac.createChannelSplitter(2), so = ac.createChannelSplitter(2); src.connect(sp); sp.connect(mg, 0, 0); sp.connect(mg, 1, 1);
      fx.connect(so); so.connect(mg, 0, 2); so.connect(mg, 1, 3); pl.clkBus.connect(mg, 0, 4); src.connect(fx);
      const FC = E('FlexCore'), xf = 0.5 + (40 / 255) * 19.5, names = E('FlexCore.NAMES'), db = (e, a) => Math.round(10 * Math.log10(e / a + 1e-30) * 10) / 10;
      // one pass: play from song row `row` for `bars` bars (+ a little), recording from before PLAY to after STOP
      async function pass(row, bars) {
        rec.port.postMessage('go'); await new Promise(r => setTimeout(r, 150));
        E(`tStart('song', {row:${row}})`);
        await new Promise(r => setTimeout(r, bars * 4 * 60 / BPM * 1000 + 300));
        const corePlaying = E('tp.core');
        E('tStop(true)'); await new Promise(r => setTimeout(r, 250));
        const blocks = await new Promise(ok => { rec.port.onmessage = e => ok(e.data); rec.port.postMessage('end'); });
        // which bar each block is in: the clock unwrapped (−1 = stopped)
        let base = 0, last = -1; const bar = [];
        for (const B of blocks) { const c = B[4][0]; if (c < 0) { bar.push(-1); continue; } if (last >= 0 && c < last - 8) base += 16; last = c; bar.push(Math.floor((base + c) / 4 + 1e-6)); }
        // FLX CHECK 2 (5 Nov): the clock itself, against the tempo. The reference below runs on the recorded clock, so a broken
        // clock (the core's Auto.prune restarted each row's ramp at every block: a staircase) passed as long as both agreed.
        // Every sample of a playing stretch should move by BPM / 60 / sr (a wrap at 16 aside); worst miss in beats.
        let clkErr = 0; { const step = BPM / 60 / sr; let prev = null;
          for (const B of blocks) for (const c of B[4]) { if (c < 0 || prev == null || prev < 0) { prev = c; continue; } let d = c - prev; if (d < -8) d += 16; clkErr = Math.max(clkErr, Math.abs(d - step)); prev = c; } }
        const off = bar.find(b => b >= 0) ?? 0, b0 = row;   // the clock counts from the start row's bar
        // the reference: FlexCore on the same input and clock, with the pattern each bar should play (off while stopped)
        const ref = new FC(sr), stats = new Map(); let stopped = { e: 0, a: 0 }, seen = false;
        for (let i = 0; i < blocks.length; i++) {
          const B = blocks[i], n = B[0].length, b = bar[i] < 0 ? -1 : bar[i] - off + b0; if (b >= 0) seen = true;
          const Lr = B[0].slice(), Rr = B[1].slice();
          ref.process(Lr, Rr, n, { pat: 0, mix: 1, xf }, { bp: B[4], flx: b < 0 ? 0 : kAt(b) });
          if (b < 0) { if (seen && i > 0 && bar[i - 1] < 0 && bar[i - 2] < 0) for (let q = 0; q < n; q++) { stopped.e += (B[2][q] - B[0][q]) ** 2; stopped.a += B[0][q] ** 2; } continue; }
          if (bar[i - 2] !== bar[i] || bar[i + 2] !== bar[i]) continue;   // two blocks either side of a bar line (k-rate changes land on block edges)
          let e = 0, a = 0; for (let q = 0; q < n; q++) { const dl = B[2][q] - Lr[q], dr = B[3][q] - Rr[q]; e += dl * dl + dr * dr; a += Lr[q] * Lr[q] + Rr[q] * Rr[q] + 1e-12; }
          const st = stats.get(b) || { e: 0, a: 0 }; st.e += e; st.a += a; stats.set(b, st);
        }
        return { clkErr, corePlaying, blocks: blocks.length, stopped: stopped.a ? db(stopped.e, stopped.a) : null,
          bars: [...stats].map(([b, st]) => ({ bar: b, k: kAt(b), name: names[kAt(b)] + (WAIT && b < 3 ? ' (waiting: no FLX yet)' : WAIT && b === 4 ? ' (carried on from bar 3)' : ''), db: db(st.e, st.a) })).sort((x, y) => x.bar - y.bar) };
      }
      const out = { sr, coreOk, passes: [] };
      if (!WAIT) out.passes.push(['from the top', await pass(0, list.length)]);
      else {
        out.passes.push(['from the top: bars 0–2 wait, bar 3 turns it on, bar 4 keeps it', await pass(0, 7)]);
        out.passes.push(['PLAY again from the top: waiting again after STOP', await pass(0, 3)]);
        out.passes.push(['from bar 4 (CHASE brings bar 3\'s FLX)', await pass(4, 2)]);
      }
      return out;
    }, [mode, PATS, process.argv.includes('--wait')]);
    if (res.err) { console.log(`FAIL ${mode}: ${res.err}`); failed = true; await page.close(); continue; }
    for (const [what, P] of res.passes) {
      const bad = P.bars.filter(r => !(r.db < -40)), stopBad = P.stopped != null && !(P.stopped < -40);
      const clkBad = P.clkErr > 0.002;   // a step a sample is ~7e-5 beats at 180 BPM; a row's jump is 0.25
      if (clkBad) failed = true;
      console.log(`${clkBad ? 'FAIL' : 'ok  '} ${mode} beat clock: off by up to ${P.clkErr.toFixed(5)} beats in a sample`);
      console.log(`${bad.length || stopBad ? 'FAIL' : 'ok  '} ${mode} (played by: ${P.corePlaying ? 'the audio thread' : 'the page'}), ${what}: ${P.bars.length} bars at ${res.sr} Hz, ${bad.length} differ from FlexCore's own pattern; stopped: ${P.stopped} dB`);
      for (const r of (process.argv.includes('-v') ? P.bars : bad)) console.log(`   bar ${String(r.bar).padStart(2)} FLX ${r.k.toString(16).toUpperCase().padStart(2, '0')} ${r.name.padEnd(44)} ${r.db} dB`);
      if (bad.length || stopBad) failed = true;
    }
    await page.close();
  }
} finally { await browser.close(); srv.close(); }
process.exit(failed ? 1 : 0);
