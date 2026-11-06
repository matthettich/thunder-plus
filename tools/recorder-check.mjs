// RECORDER CHECK (5 Nov): Record into a pad, end to end, in headless Chrome with its fake microphone (a beep), on both
// sequencers. A song of 2-second patterns (120 BPM, LPB 4, 16 rows); then:
//   FREE         record ~1.2 s, stop: a take of about that length that isn't silent
//   PATTERN ×1   pressed while stopped: the song starts at the cursor's row; the take is exactly one pattern (to the frame)
//   PATTERN ×1   armed while the song plays: starts at the next pattern edge; exactly one pattern
//   UNTIL STOP   Stop pressed during the 2nd pattern: the take ends at that pattern's end: exactly two patterns
//   KEEP         into pad 3: the pad's sample is the take, its instruments are fitted (SYNC on, BEATS = the pattern's beats)
//
//   node tools/recorder-check.mjs            Page, then Audio thread
//   node tools/recorder-check.mjs --page | --core
// Needs Chrome and playwright: CHROME=/path/to/chrome PLAYWRIGHT=/path/to/playwright/index.mjs
import http from 'http'; import fs from 'fs'; import path from 'path'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const PW = process.env.PLAYWRIGHT || ['/opt/node-tools/node_modules/playwright/index.mjs', '/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
const MODES = process.argv.includes('--page') ? ['page'] : process.argv.includes('--core') ? ['core'] : ['page', 'core'];
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.png': 'image/png' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required', '--use-fake-device-for-media-stream', '--use-fake-ui-for-media-stream'] });
let failed = false;
const say = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };
try {
  for (const mode of MODES) {
    const ctxB = await browser.newContext({ permissions: ['microphone'], viewport: { width: 1400, height: 900 } });
    const page = await ctxB.newPage();
    page.on('pageerror', e => console.error('page error:', e.message));
    await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`);
    await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
    await page.waitForTimeout(1500);
    const res = await page.evaluate(async mode => {
      const T = window.__seqTest, E = s => T.ev(s), wait = ms => new Promise(r => setTimeout(r, ms));
      const until = async (f, ms) => { const t = Date.now(); while (!f() && Date.now() - t < ms) await wait(30); return f(); };
      const j = E('freshSong()'); j.bpm = 120; j.lpb = 4; j.name = 'recorder check';
      for (let y = 0; y < 6; y++){ const ph = E('freshPhrase()'); j.phrases[y] = ph; j.plen[y] = 16; const sect = Array.from({ length: 16 }, () => [-1, 0]); sect[0] = [y, 0]; j.chains[y] = sect; j.song[y][0] = y; }
      await T.load(JSON.stringify(j), 'recorder-check.thunder');
      const coreOk = await T.coreOn(mode === 'core');
      E("RCD.ui = null; rcdSet.mode = 'free'; rcdSet.ch = 'stereo'; rcdSet.lat = 0; rcdSet.fit = 1; rcdSet.input = '';");
      const out = { coreOk, sr: E('ctx.sampleRate') };
      // the pattern's length, as the sequencer plays it
      out.pat = E(`(() => { const sq = makeSeq(S, 'song', {row:0, once:true, sim:true}); let t = null; for (let n = 0; sq.alive() && n < 400; n++){ const r = sq.step(); const row = Math.max(...r.pos.filter(Boolean).map(p => p.row)); if (row >= 1){ t = r.t; break; } } return t; })()`);
      const take = () => E('RCD.take ? {n:RCD.take.data.length, st:!!RCD.take.data2, pk:sePeak(RCD.take).peak, how:RCD.take.how, beats:RCD.take.beats, pats:RCD.take.pats} : null');
      // FREE
      await E('rcdStart()'); await wait(1200); E('rcdStop()'); await until(() => E('!!RCD.take'), 4000); out.free = take();
      // PATTERN ×1 from stopped (cursor on song row 0)
      E("RCD.take = null; rcdSet.mode = 'sync'; rcdSet.pats = 1; cs.song.y = 0;"); await E('rcdStart()');
      await until(() => E('!!RCD.take'), 8000); out.sync1 = take(); out.playing1 = E('tp.on'); E('tStop(true)'); await wait(300);
      // PATTERN ×1 armed while playing (from row 2, armed half a pattern in)
      E("RCD.take = null; cs.song.y = 2;"); E(`tStart('song', {row:2})`); await wait(1000); await E('rcdStart()');
      out.armedState = E('RCD.sync && RCD.sync.phase');
      await until(() => E('!!RCD.take'), 8000); out.sync2 = take(); E('tStop(true)'); await wait(300);
      // UNTIL STOP: Stop pressed during the 2nd pattern
      E("RCD.take = null; rcdSet.pats = 0; cs.song.y = 0;"); await E('rcdStart()');
      await until(() => E('RCD.sync && RCD.sync.phase === "rec" && RCD.sync.n >= 1'), 8000); await wait(500); E('rcdStop()');
      await until(() => E('!!RCD.take'), 8000); out.until = take(); E('tStop(true)'); await wait(200);
      // KEEP into pad 3
      E("rcdKeep(3)"); out.keep = E(`(() => { const v = kit.voices[3], L = v.layers[v.sel]; const I = S.insts.filter(I => I.pad === 3);
        return {n:L.sample && L.sample.data.length, st:!!(L.sample && L.sample.data2), type:L.type, eng:v.engine || '', fit:I.length ? I.every(I => I.sync === 1) : null, beats:I.length ? I[0].beats : null, nI:I.length}; })()`);
      E('rcdClose()');
      return out;
    }, mode).catch(err => ({ err: String(err && err.message || err) }));
    if (res.err) { say(false, `${mode}: ${res.err}`); await ctxB.close(); continue; }
    const sr = res.sr, P = Math.round(res.pat * sr), who = `${mode}${mode === 'core' && !res.coreOk ? ' (audio thread did not load)' : ''}`;
    say(Math.abs(res.pat - 2) < 1e-6, `${who}: a pattern lasts ${res.pat} s (16 rows at 120 BPM, LPB 4)`);
    say(res.free && res.free.n > sr * 0.9 && res.free.n < sr * 1.8 && res.free.pk > 1e-3, `${who}: FREE take ${res.free ? (res.free.n / sr).toFixed(2) + ' s, peak ' + res.free.pk.toFixed(3) : 'missing'}`);
    say(res.sync1 && Math.abs(res.sync1.n - P) <= 1 && res.sync1.pk > 1e-3 && res.sync1.beats === 4, `${who}: PATTERN ×1 from stopped: ${res.sync1 ? res.sync1.n + ' frames (a pattern is ' + P + '), ' + res.sync1.beats + ' beats' : 'missing'}; the song kept playing: ${res.playing1}`);
    say(res.armedState === 'armed' && res.sync2 && Math.abs(res.sync2.n - P) <= 1, `${who}: PATTERN ×1 armed mid-pattern: waited (${res.armedState}), took ${res.sync2 ? res.sync2.n : 'nothing'} frames`);
    say(res.until && Math.abs(res.until.n - 2 * P) <= 1 && res.until.pats === 2, `${who}: UNTIL STOP, stopped in the 2nd pattern: ${res.until ? res.until.n + ' frames, ' + res.until.pats + ' patterns' : 'missing'} (two are ${2 * P})`);
    say(res.keep && res.keep.n === res.until.n && res.keep.type === 'sample' && (res.keep.nI === 0 || (res.keep.fit && res.keep.beats === 8)), `${who}: KEEP into pad 3: ${JSON.stringify(res.keep)}`);
    await ctxB.close();
  }
} finally { await browser.close(); srv.close(); }
process.exit(failed ? 1 : 0);
