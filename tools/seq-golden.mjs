// SEQUENCER GOLDEN TEST (docs/NATIVE-DESKTOP-PLAN.md, phase 0).
// Plays each test song through the app's own sequencer (makeSeq, sim mode: no audio, no renders) in headless Chrome and
// compares every event it produces with tests/golden/seq-<song>.json. The point: when the sequencer moves onto the audio
// thread (phase 2) and into C++ (phase 3), the event stream must not change.
//
//   node tools/seq-golden.mjs            compare (exit 1 on any difference; the first differing row is printed)
//   node tools/seq-golden.mjs --update   write the goldens (only after checking why they changed)
//   node tools/seq-golden.mjs --core     the same cases on the audio thread (core/seq-core.js): must match the same goldens
//
// Needs Chrome (or Chromium) and the playwright package: CHROME=/path/to/chrome, PLAYWRIGHT=/path/to/playwright/index.mjs
// if they aren't found. The full event dumps go to tests/golden/out/ (not committed) for diffing.
import http from 'http'; import fs from 'fs'; import path from 'path'; import crypto from 'crypto'; import vm from 'vm'; import { fileURLToPath, pathToFileURL } from 'url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const UPDATE = process.argv.includes('--update');
// Test cases: a song file, optionally changed in memory (`edit`), and how it is played (row: a mid-song start, which runs
// CHASE; loop: rows y0–y1 repeated, capped by maxRows).
const CMDS = [['ARP', 0x37], ['RET', 0x24], ['DEL', 0x03], ['PRB', 0x80], ['GEN', 0x41], ['CHA', 0x80], ['RND', 0x24], ['TSP', 0x05],
  ['PIT', 0x0C], ['VOL', 0x90], ['PAN', 0x40], ['NTH', 0x02], ['KIL', 0x04], ['OFF', 0x06], ['TIC', 0x03], ['RNL', 0x13]];
// Every phrase row with a note gets one command in its third effect column (if free), cycling through CMDS. FXC (the
// command numbers) is read from index.html so the file stays right if commands are added.
function withCommands(song) {
  const src = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8'), m = src.match(/const FXC = \[([\s\S]*?)\];/);
  const FXC = [...m[1].matchAll(/'([A-Z0-9]{3})'/g)].map(x => x[1]);
  let n = 0;
  (song.song.phrases || []).forEach((ph, i) => { if (!Array.isArray(ph)) return; ph.forEach((r, y) => {
    if (!Array.isArray(r) || r[0] < 0 || r[10] >= 0) return; const [c, v] = CMDS[(i * 7 + y) % CMDS.length]; r[10] = FXC.indexOf(c); r[11] = v; n++; }); });
  return n;
}
const SONGS = [
  { id: 'electro', file: 'sounds/ELECTRO.thunder', mode: 'song' },
  { id: 'electro-mid', file: 'sounds/ELECTRO.thunder', mode: 'song', opt: { row: 9 } },
  { id: 'electro-loop', file: 'sounds/ELECTRO.thunder', mode: 'song', opt: { loop: { y0: 4, y1: 6 } }, max: 800 },
  { id: 'electro-cmds', file: 'sounds/ELECTRO.thunder', mode: 'song', edit: withCommands },
];
// LIVE-CLOCK CASES (docs/port/SEQ-LIVE-DESIGN.md A3; goldens native/tests/golden/seq-*-live*.json, read by ctest `seq_live`).
// The page's makeSeq is not reachable from outside index.html (it is not a global), so these run the audio thread's copy, core/seq-core.js's
// makeSeq, loaded into a `vm` context exactly as native/tools/seq-dump.mjs does: no browser needed. THAT PATH IS PROVEN FIRST: the four cases above
// are run through it and must equal their committed goldens (made by the page in Chrome) byte for byte, or the live cases are not written / compared.
// The clock is written between steps exactly as core-processor.js writes it: `time = t0` before the first step (play, :423), `bpm = v` (the 'bpm'
// message, :354), and the catch-up skip / RESUME shift `base += d; time += d; if (btT != null) btT += d` (:778, :421).
// live: {time0, ops: [{after: N, bpm: v | 'song', shift: d}]}: an op runs after the N-th step (1 = after the first). Each row also records the
// clock after the step: {t, evs, clk: {time, base, bt, btT, bpm}}. 'song' = the song's own bpm (S.bpm), written into the golden as a number.
const LIVE_SONGS = [
  { id: 'electro-live-time', file: 'sounds/ELECTRO.thunder', mode: 'song', live: { time0: 1234.5678, ops: [] } },
  { id: 'electro-live-bpm', file: 'sounds/ELECTRO.thunder', mode: 'song', live: { ops: [{ after: 40, bpm: 97.3 }, { after: 120, bpm: 'song' }] } },
  { id: 'electro-live-shift', file: 'sounds/ELECTRO.thunder', mode: 'song', live: { ops: [{ after: 25, shift: 0.37 }, { after: 60, shift: -0.011 }] } },
  { id: 'electro-cmds-live-all', file: 'sounds/ELECTRO.thunder', mode: 'song', edit: withCommands,
    live: { time0: 1234.5678, ops: [{ after: 25, shift: 0.37 }, { after: 40, bpm: 97.3 }, { after: 60, shift: -0.011 }, { after: 120, bpm: 'song' }] } },
];
const LIVE_DIR = path.join(ROOT, 'native/tests/golden');
function liveHarness() {
  const sctx = vm.createContext({ console, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 48000, currentTime: 0, currentFrame: 0 });
  vm.runInContext(fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8') + '\n;globalThis.__u = { makeSeq, coreSeeded, corePlain, coreNum, FXC,' +
    ' load(s, v){ S = s; if (v){ if ("vtrk" in v) vtrk = v.vtrk; if ("NSLOT" in v) NSLOT = v.NSLOT; if (v.TFX) TFX = v.TFX; } busPlanH = null; busPlanVal = null; } };', sctx, { filename: 'seq-core.js' });
  return { ctx: sctx, U: sctx.__u };
}
// one case on the core's makeSeq; `live` null = a plain case (the proof path), else the clock ops above. Returns {rows, info}.
function runVm(H, text, mode, max, opt, live) {
  const j = JSON.parse(text), bpm = j.song.bpm;
  const Sv = vm.runInContext('(' + JSON.stringify(j.song) + ')', H.ctx), vars = vm.runInContext('({vtrk:false,NSLOT:16,TFX:{}})', H.ctx);
  H.U.load(Sv, vars);
  H.ctx.__c = { mode, max, o: opt || {}, live: live ? JSON.parse(JSON.stringify({ time0: live.time0 ?? null, ops: live.ops.map(o => ({ ...o, ...(o.bpm === 'song' ? { bpm } : {}) })) })) : null };
  const rows = vm.runInContext(`(() => { const d = __c, o = d.o, L = d.live; return __u.coreSeeded(() => { const out = [];
    const sq = __u.makeSeq(S, d.mode || 'song', {row:o.row || 0, loop:o.loop || null, once:!o.loop, sim:true});
    if (L && L.time0 != null) sq.time = L.time0;
    for (let n = 0; sq.alive() && n < (d.max || 20000); n++){ const r = sq.step(); const row = {t:__u.coreNum(r.t), evs:r.evs.map(__u.corePlain)};
      if (L){ row.clk = {time:__u.coreNum(sq.time), base:__u.coreNum(sq.base), bt:__u.coreNum(sq.bt), btT:__u.coreNum(sq.btT), bpm:__u.coreNum(sq.bpm)};
        for (const p of L.ops) if (p.after === n + 1){ if (p.bpm != null) sq.bpm = p.bpm; if (p.shift != null){ sq.base += p.shift; sq.time += p.shift; if (sq.btT != null) sq.btT += p.shift; } } }
      out.push(row); }
    return out; }); })()`, H.ctx);
  return { rows, bpm: j.song.bpm, live: L2(live, bpm) };
  function L2(l, b) { return l ? { time0: l.time0 ?? null, ops: l.ops.map(o => ({ ...o, ...(o.bpm === 'song' ? { bpm: b } : {}) })) } : null; }
}
function liveCases() {
  if (process.argv.includes('--no-live')) return true;
  const H = liveHarness(); let bad = false; const sha = t => crypto.createHash('sha256').update(t).digest('hex');
  // 1. the proof: the four plain cases through this path equal the committed goldens
  for (const S of SONGS) {
    let t0 = fs.readFileSync(path.join(ROOT, S.file), 'utf8'); if (S.edit) { const j = JSON.parse(t0); S.edit(j); t0 = JSON.stringify(j); }
    const { rows } = runVm(H, t0, S.mode, S.max || 0, S.opt || null, null);
    const g = JSON.parse(fs.readFileSync(path.join(ROOT, `tests/golden/seq-${S.id}.json`), 'utf8')), h = sha(rows.map(r => JSON.stringify(r)).join('\n') + '\n');
    const ok = h === g.sha256 && rows.length === g.rows; if (!ok) bad = true;
    console.log(`${ok ? 'ok  ' : 'FAIL'} core-path proof ${S.id}: ${rows.length} rows ${ok ? '= the page\'s golden' : 'DIFFERS from the page\'s golden: live cases not written'}`);
  }
  if (bad) return false;
  // 2. the live cases
  fs.mkdirSync(LIVE_DIR, { recursive: true });
  for (const S of LIVE_SONGS) {
    let t0 = fs.readFileSync(path.join(ROOT, S.file), 'utf8'); if (S.edit) { const j = JSON.parse(t0); S.edit(j); t0 = JSON.stringify(j); }
    const { rows, bpm, live } = runVm(H, t0, S.mode, S.max || 0, S.opt || null, S.live), text = rows.map(r => JSON.stringify(r)).join('\n') + '\n';
    const counts = {}; for (const r of rows) for (const e of r.evs) counts[e.type] = (counts[e.type] || 0) + 1;
    const now = { song: S.file, mode: S.mode, live, bpm, rows: rows.length, events: Object.values(counts).reduce((a, b) => a + b, 0), counts, sha256: sha(text),
      rowHashes: rows.map(r => crypto.createHash('sha256').update(JSON.stringify(r)).digest('hex').slice(0, 12)) };
    const gf = path.join(LIVE_DIR, `seq-${S.id}.json`);
    if (UPDATE || !fs.existsSync(gf)) { fs.writeFileSync(gf, JSON.stringify(now, null, 1) + '\n'); console.log(`wrote ${path.relative(ROOT, gf)}: ${now.rows} rows, ${now.events} events`); continue; }
    const was = JSON.parse(fs.readFileSync(gf, 'utf8')), ok = was.sha256 === now.sha256; if (!ok) bad = true;
    console.log(`${ok ? 'ok  ' : 'FAIL'} ${S.id}: ${now.rows} rows, ${now.events} events${ok ? '' : ' (first differing row ' + now.rowHashes.findIndex((h, k) => h !== was.rowHashes[k]) + ')'}`);
  }
  return !bad;
}
const LIVE_OK = liveCases();
if (process.argv.includes('--live-only')) process.exit(LIVE_OK ? 0 : 1);
const TIMING = process.argv.includes('--timing');
const CORE = process.argv.includes('--core');   // run each case on the audio thread (core/seq-core.js) instead: the same goldens must match
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser', 'C:/Program Files/Google/Chrome/Application/chrome.exe', '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'].find(f => fs.existsSync(f));
if (!PW) { console.error('playwright not found: npm i -g playwright, or set PLAYWRIGHT'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.webmanifest': 'application/manifest+json' };
const srv = http.createServer((q, r) => {
  let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const port = srv.address().port;
const browser = await chromium.launch({ ...(CHROME ? { executablePath: CHROME } : {}), args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = !LIVE_OK;
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  page.on('pageerror', e => console.error('page error:', e.message));
  await page.goto(`http://localhost:${port}/index.html?seqtest=1`);
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
  await page.waitForTimeout(1500);   // let the app's own first-run work settle
  fs.mkdirSync(path.join(ROOT, 'tests/golden/out'), { recursive: true });
  for (const S of SONGS) {
    let text0 = fs.readFileSync(path.join(ROOT, S.file), 'utf8');
    if (S.edit) { const j = JSON.parse(text0); const n = S.edit(j); text0 = JSON.stringify(j); console.log(`     ${S.id}: ${n} commands added`); }
    const info = await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [text0, path.basename(S.file)]);
    const rows = await page.evaluate(([m, mx, o, c]) => c ? window.__seqTest.runCore(m, mx, o) : window.__seqTest.run(m, mx, o), [S.mode, S.max || 0, S.opt || null, CORE]);
    const text = rows.map(r => JSON.stringify(r)).join('\n') + '\n';
    fs.writeFileSync(path.join(ROOT, `tests/golden/out/seq-${S.id}.jsonl`), text);
    const counts = {}; for (const r of rows) for (const e of r.evs) counts[e.type] = (counts[e.type] || 0) + 1;
    const now = { song: S.file, mode: S.mode, bpm: info.bpm, tracks: info.nt, rows: rows.length, events: Object.values(counts).reduce((a, b) => a + b, 0), counts,
      sha256: crypto.createHash('sha256').update(text).digest('hex'), rowHashes: rows.map(r => crypto.createHash('sha256').update(JSON.stringify(r)).digest('hex').slice(0, 12)) };
    const gf = path.join(ROOT, `tests/golden/seq-${S.id}.json`);
    if (UPDATE || !fs.existsSync(gf)) { fs.writeFileSync(gf, JSON.stringify(now, null, 1) + '\n'); console.log(`wrote ${path.relative(ROOT, gf)}: ${now.rows} rows, ${now.events} events`); continue; }
    const was = JSON.parse(fs.readFileSync(gf, 'utf8'));
    if (was.sha256 === now.sha256) { console.log(`ok   ${S.id}: ${now.rows} rows, ${now.events} events`); continue; }
    failed = true;
    const i = now.rowHashes.findIndex((h, k) => h !== was.rowHashes[k]);
    console.log(`FAIL ${S.id}: rows ${was.rows} → ${now.rows}, events ${was.events} → ${now.events}; first difference at step ${i < 0 ? Math.min(was.rows, now.rows) : i}`);
    // (reporting only) the goldens keep counts and row hashes, not the streams, so say what can be said from those
    const delta = Object.keys({ ...was.counts, ...counts }).map(k => [k, (counts[k] || 0) - (was.counts?.[k] || 0)]).filter(([, d]) => d).map(([k, d]) => `${k} ${d > 0 ? '+' : ''}${d}`);
    const bad = now.rowHashes.map((h, k) => h !== was.rowHashes[k] ? k : -1).filter(k => k >= 0);
    console.log(`  events by type, golden → now: ${delta.length ? delta.join(', ') : '(counts equal: values, order or times differ)'}; ${bad.length} of ${now.rows} rows differ, first steps: ${bad.slice(0, 10).join(' ')}`);
    if (i >= 0) {   // the first differing row, one short line per event (notes: track, pad, mn, rate; others: type, p, track, v), with its step and time
      const short = e => e.type === 'note' ? `note t${e.track} pad${e.pad} mn${e.mn} rate${e.rate}` : `${e.type}${e.p ? ':' + e.p : ''} t${e.track ?? '-'} ${e.v ?? e.b ?? ''}@${e.t}`;
      console.log(`  now, step ${i} (t=${rows[i].t}): ${rows[i].evs.map(short).join(' | ').slice(0, 900)}`);
    }
    console.log('  (the golden has no events to diff against: to see exactly what changed, run the last good commit and diff the two out/*.jsonl files)');
    console.log(`  full stream: tests/golden/out/seq-${S.id}.jsonl (compare with a run of the last good commit)`);
  }
  if (TIMING) {   // plan section 12: how far ahead notes are booked, idle and with the main thread stalled
    await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'), 'ELECTRO.thunder']);
    await page.mouse.click(5, 5);   // a gesture, so audio may start
    for (const [stall, every] of [[-1, 0], [0, 0], [100, 1000], [200, 1000], [400, 1000]]) {
      const r = await page.evaluate(([a, b]) => window.__seqTest.timing(12000, Math.max(0, a), b), [stall, every]); if (stall < 0) { console.log('timing  (warm-up run, renders the song: ' + r.audio_ms.late + ' late)'); continue; }
      console.log(`timing  stall ${stall} ms every ${every || '-'} ms: ` + JSON.stringify(r));
    }
  }
} finally { await browser.close(); srv.close(); }
process.exit(failed ? 1 : 0);
