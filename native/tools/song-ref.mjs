// U09: REFERENCE B, the whole song as the web app renders it today.
// Loads a .thunder song into the real app in headless Chrome and renders it with the app's own render path: renderOut()'s own steps
// (makeSeq in song mode with once:true from RENDER FROM, the pads rendered by renderAsync / renderVoice, the end time, then pass():
// an OfflineAudioContext, makePlayer, every event played by the page player), at 48 kHz, WITH THESE SWITCHED OFF, in the song text
// before it is loaded (so the C++ sequencer reads the very same song) and checked again in memory:
//   S.tfx      every track's effect slots emptied          S.mfx   the master's effect slots emptied
//   S.chainOn  CHN off on every track (no chains, so no bus exists: every track plays straight to the master)
//   S.mst.on   mastering off                               S.limiter false, and pass() is called with lim = false (no safety limiter)
//   live engines: a pad with a live engine plays its rendered pad (the page's own fallback; the engines are later port units)
// and renderOut's own last steps left out: no tail trim (there is no tail), no Normalise, no file. Math.random is the seeded stream the
// sequencer goldens use (0x2545F491) while the sequencer runs, as the C++ sequencer's stand-in for Math.random is.
// With --master the folder is <name>-master and the reference is the page's render with that chain on and pass(lim = true).
// What is written to <outDir> (default /tmp/thunder-ref/<song name>):
//   song.thunder   the song as rendered (the switched-off copy): thunder-render's input
//   index.json     {sr, frames, vars (coreVars()), kit (coreKitV()), pads: [{pad, file, len, sr, stereo}], off, stats, sha256 of ref.f32}
//   pad-<n>.f32    the rendered pads the song render used (mono float32)
//   ref.f32        the render: float32, the left channel's `frames` samples, then the right's
//   summary.json   the reference's hashes and a 2 x 64 peak fingerprint; compared with native/tests/golden/song-ref/<name>.json (committed)
//   node native/tools/song-ref.mjs sounds/ELECTRO.thunder [outDir] [--golden]     (--golden: write the committed summary)
import http from 'node:http'; import fs from 'node:fs'; import path from 'node:path'; import crypto from 'node:crypto'; import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const songFile = process.argv[2]; if (!songFile) { console.error('usage: song-ref.mjs <song.thunder> [outDir]'); process.exit(2); }
// --master (U12): the app's own master chain switched ON (track effects, master effects, chains and live engines stay off): S.mst on with a shaped EQ,
// the suppressor, the compressor (the freshMst defaults) and its own limiter OFF, so that the safety limiter (S.limiter, pass(lim = true)) is in the path too.
const MASTER = process.argv.includes('--master'), FXALL = process.argv.includes('--fx-all'), FX = process.argv.includes('--fx') || FXALL;   // --fx-all (FX1): EVERY effect slot of the song stays ON (tfx, JS cores, pump, MODFX; pump mastering in S.mfx), chains / mastering (S.mst) / limiter off   // --fx (U14): the 34 tfx effects on the tracks and the master stay ON; every other effect is removed (not yet ported: U15 to U18)
const FXTYPES = FX ? new Set((await import('./fx-src.mjs')).effects().map(e => e.key)) : new Set();
const keepFx = f => FX && f && FXTYPES.has(f.type);
const name = path.basename(songFile).replace(/\.thunder$/, '') + (MASTER ? '-master' : FXALL ? '-fx-all' : FX ? '-fx' : '');
const OUT = (process.argv[3] && !process.argv[3].startsWith('--') ? process.argv[3] : null) || '/tmp/thunder-ref/' + name;
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', path.join(ROOT, 'node_modules/playwright/index.mjs')].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW || !CHROME) { console.error('needs playwright and Chrome (PLAYWRIGHT=, CHROME=)'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
fs.mkdirSync(OUT, { recursive: true });

// the song, with everything after the mixer's own strip switched off
const file = JSON.parse(fs.readFileSync(path.resolve(songFile), 'utf8')), S0 = file.song;
if (FXALL) { for (const a of (S0.tfx || [])) for (const f of (a || [])) if (f) FXTYPES.add(f.type); for (const f of (S0.mfx || [])) if (f) FXTYPES.add(f.type); }   // (the page's own list for the in-memory pass below)
const off = { tfx: 0, mfx: 0, chainOn: 0, mst: !!(S0.mst && S0.mst.on), limiter: !!S0.limiter };
S0.tfx = (S0.tfx || []).map(a => (a || []).map(f => { if (f) off.tfx++; return keepFx(f) ? f : null; }));
S0.mfx = (S0.mfx || []).map(f => { if (f) off.mfx++; return keepFx(f) ? f : null; });
S0.chainOn = (S0.chainOn || []).map(v => { if (v) off.chainOn++; return false; });
if (MASTER) { const B = (type, f, g = 0, q = 1, slope = 24, on = true) => ({ type, f, g, q, slope, on });
  S0.mst = { on: true, eq: { on: true, bands: [B('lowcut', 28, 0, .71, 24), B('lowshelf', 110, 2, .7), B('bell', 350, -3, 1.2), B('bell', 2500, 2, 1.2), B('highshelf', 9000, 1.5, .7), B('highcut', 19000, 0, .71, 12, true)] },
    sup: { on: true }, comp: { on: true }, lim: { on: false } };
  if (process.env.MSTJSON) S0.mst = JSON.parse(process.env.MSTJSON);   // (an experiment: another master chain)
  S0.limiter = process.env.LIMITER !== '0'; }
else { if (S0.mst) S0.mst.on = false; S0.limiter = false; }
const text = JSON.stringify(file);
fs.writeFileSync(path.join(OUT, 'song.thunder'), text);

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff': 'font/woff', '.woff2': 'font/woff2', '.png': 'image/png', '.webmanifest': 'application/manifest+json' };
const srv = http.createServer((q, r) => {
  const u = new URL(q.url, 'http://x');
  if (q.method === 'POST' && u.pathname === '/__save') { const f = path.join(OUT, path.basename(u.searchParams.get('name'))), parts = []; q.on('data', d => parts.push(d)); q.on('end', () => { fs.writeFileSync(f, Buffer.concat(parts)); r.writeHead(200); r.end('ok'); }); return; }
  let p = decodeURIComponent(u.pathname); if (p.endsWith('/')) p += 'index.html';
  const f = path.normalize(path.join(ROOT, p)); if (!f.startsWith(ROOT)) { r.writeHead(404); return r.end(); }
  fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); });
});
await new Promise(ok => srv.listen(0, ok));
const browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required'] });
let info; const chrome = browser.version();
try {
  const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
  page.on('pageerror', e => console.error('page error:', e.message));
  await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`);
  await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
  await page.waitForTimeout(1500);
  await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [text, name + '.thunder']);
  /* eslint-disable no-undef */
  // (the app's code lives in one closure: __seqTest.ev runs a line inside it, where S, kit, makeSeq, pass... are)
  const inApp = async (MASTER, FXK) => {
    const save = (nm, f32) => fetch('/__save?name=' + nm, { method: 'POST', body: new Blob([f32.buffer.slice(f32.byteOffset, f32.byteOffset + f32.byteLength)]) }).then(r => r.text());
    // in memory too (the loader may fill in defaults)
    S.tfx = S.tfx.map(a => (a || []).map(f => f && FXK.includes(f.type) ? f : null)); S.mfx = S.mfx.map(f => f && FXK.includes(f.type) ? f : null); S.chainOn = S.chainOn.map(() => false); if (MASTER) { if (!(S.mst && S.mst.on)) return { err: 'the master chain did not load' }; } else { if (S.mst) S.mst.on = false; S.limiter = false; }
    // LIVE ENGINES OFF: a pad with a live engine (acid, mach, vox, bass, the pump synths, waves) plays its RENDERED pad, as the page itself
    // does wherever the live module is not loaded (bassNode returns null: "play the render"). The live modules still load into the
    // render's context; the sets that say "loaded" are kept empty, so no live node is ever made.
    for (const set of [typeof bassOK !== 'undefined' && bassOK, typeof psOK !== 'undefined' && psOK, typeof wlOK !== 'undefined' && wlOK]) if (set) set.add = function () { return this; };
    const sr = 48000;
    // renderOut(), its own steps
    const R = Math.random; let s = 0x2545F491;
    Math.random = () => { let x = s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61); return ((x ^ x >>> 14) >>> 0) / 4294967296; };
    let seq, evs, guard = 0, steps = 0;
    try { seq = makeSeq(S, 'song', { row: S.renderFrom, once: true }); if (!seq.alive()) return { err: 'nothing to render' };
      evs = (S.renderFrom | 0) > 0 ? chaseEvs(S.renderFrom | 0, seq.time) : [];
      while (seq.alive() && seq.time < 1200 && guard++ < 100000) { const r = seq.step(); steps++; for (const e of r.evs) evs.push(e); } } finally { Math.random = R; }
    const notes = evs.filter(e => e.type === 'note' && kit.voices[e.pad]);
    const keepT = []; for (let t = 0; t < NT; t++) if (audible(t) && notes.some(e => e.track === t)) keepT.push(t);
    const BP = busPlan(); for (const [b, src] of BP.feeds) if (!keepT.includes(b) && audible(b) && src.some(t => keepT.includes(t))) keepT.push(b);
    keepT.sort((a, b) => a - b);
    const pads = new Map();
    for (const e of notes) if (!pads.has(e.pad)) { const v = kit.voices[e.pad], hold = isSyn(e.pad) ? v.len : 0;
      let d; try { d = (await renderAsync(v, sr, hold, false)).data; } catch (err) { d = renderVoice(v, sr, null, hold); } pads.set(e.pad, d); }
    const bufs = new Map(); let stretched = 0;
    const getB = (pad, rev, st) => { const k = pad + (rev ? 'r' : 'f') + stKey(st); let b = bufs.get(k);
      if (!b) { const d0 = pads.get(pad); if (!d0) return null; if (st) stretched++; let d = st ? stretchData(d0, sr, loopSecs(pad, d0, sr), st) : d0; b = mkBuf(rev ? d.slice().reverse() : d, sr); bufs.set(k, b); } return b; };
    let end = seq.time;
    for (const e of notes) { const len = (e.st ? e.st.beats * 60 / e.st.bpm : pads.get(e.pad).length / sr) / Math.min(e.rate, e.slide ? e.slide.from : e.rate); end = Math.max(end, e.cut != null ? Math.min(e.t + len, e.cut + 0.06) : e.t + len); }
    const N = Math.ceil((end + 0.05) * sr);
    const mix = await pass(evs, new Set(keepT), N, sr, getB, !!(MASTER && S.limiter), true);
    // what the song asks of the player (so a difference can be read)
    const cnt = {}; for (const e of evs) cnt[e.type] = (cnt[e.type] || 0) + 1;
    const nk = k => notes.filter(e => e[k] != null && e[k] !== false && e[k] !== 0).length;
    const stats = { steps, events: evs.length, byType: cnt, notes: notes.length, notesNoVoice: evs.filter(e => e.type === 'note').length - notes.length, keepT, seqEnd: seq.time, end, bpm: S.bpm,
      live: notes.filter(e => isLivePad(e.pad)).length, sf: notes.filter(e => kit.voices[e.pad].engine === 'sf').length, midiOnly: notes.filter(e => e.ii != null && (S.insts[e.ii] || {}).mo === 1).length,
      st: nk('st'), tw: nk('tw'), vk: nk('vk'), rev: nk('rev'), slide: nk('slide'), cut: nk('cut'), rel: nk('rel'), arp: nk('arp'), bend: nk('bend'), vib: nk('vib'), wob: nk('wob'), trm: nk('trm'), apn: nk('apn'), fdi: nk('fdi'), ofs: nk('ofs'),
      poly: notes.filter(e => e.poly).length, rateNot1: notes.filter(e => e.rate !== 1).length, stretched, offGrid: notes.filter(e => Math.abs(e.t * sr - Math.round(e.t * sr)) > 1e-6).length,
      mix: S.mix.slice(0, 12).map(m => [m.vol, m.trim, m.pan, +m.mute, +m.solo].join('/')), master: S.master, mout: S.mout };
    const off2 = { tfx: S.tfx.flat().filter(Boolean).length, mfx: S.mfx.filter(Boolean).length, chainOn: S.chainOn.filter(Boolean).length, mst: !!(S.mst && S.mst.on), limiter: !!S.limiter, passLim: !!MASTER, master: !!MASTER, normalise: false, liveEngines: false };
    const padList = [];
    for (const [pad, d] of pads) { await save('pad-' + pad + '.f32', d); padList.push({ pad, file: 'pad-' + pad + '.f32', len: d.length, sr, stereo: false }); }
    const all = new Float32Array(2 * N); all.set(mix[0], 0); all.set(mix[1], N); await save('ref.f32', all);
    return { sr, frames: N, vars: coreVars(), kit: coreKitV(), pads: padList, offAfter: off2, stats };
  };
  info = await page.evaluate(src => window.__seqTest.ev(src), '(' + inApp.toString() + ')(' + MASTER + ', ' + JSON.stringify([...FXTYPES]) + ')');
} finally { await browser.close(); srv.close(); }
if (!info || info.err) { console.error('song-ref: ' + (info ? info.err : 'no result')); process.exit(1); }
const ref = fs.readFileSync(path.join(OUT, 'ref.f32')), f = new Float32Array(ref.buffer, ref.byteOffset, ref.length / 4);
let pk = 0; for (let i = 0; i < f.length; i++) pk = Math.max(pk, Math.abs(f[i]));
// a short fingerprint a later session can compare: the peak of each channel over 64 equal slices
const N = info.frames, fp = []; for (let c = 0; c < 2; c++) for (let k = 0; k < 64; k++) { let m = 0; for (let i = Math.floor(k * N / 64); i < Math.floor((k + 1) * N / 64); i++) m = Math.max(m, Math.abs(f[c * N + i])); fp.push(+m.toFixed(5)); }
info.off = off; info.song = songFile; info.chrome = chrome; info.peak = pk; info.sha256 = crypto.createHash('sha256').update(ref).digest('hex'); info.fingerprint = fp;
info.padSha256 = Object.fromEntries(info.pads.map(p => [p.pad, crypto.createHash('sha256').update(fs.readFileSync(path.join(OUT, p.file))).digest('hex').slice(0, 16)]));
fs.writeFileSync(path.join(OUT, 'index.json'), JSON.stringify(info));
// the short summary a later session compares with the committed one (native/tests/golden/song-ref/<name>.json; --golden rewrites it)
const summary = { song: songFile, chrome, sr: info.sr, frames: N, peak: +pk.toFixed(6), sha256: info.sha256, padSha256: info.padSha256, off, offAfter: info.offAfter, stats: info.stats, fingerprint: fp };
fs.writeFileSync(path.join(OUT, 'summary.json'), JSON.stringify(summary, null, 1) + '\n');
const gp = path.join(ROOT, 'native/tests/golden/song-ref', name + '.json');
if (process.argv.includes('--golden')) { fs.mkdirSync(path.dirname(gp), { recursive: true }); fs.writeFileSync(gp, JSON.stringify(summary, null, 1) + '\n'); console.log('wrote ' + gp); }
else if (fs.existsSync(gp)) { const g = JSON.parse(fs.readFileSync(gp, 'utf8'));
  if (g.sha256 === info.sha256) console.log('reference: the same bits as the committed summary (sha256 ' + info.sha256.slice(0, 16) + ')');
  else { let d = 0; for (let i = 0; i < fp.length; i++) d = Math.max(d, Math.abs(fp[i] - (g.fingerprint[i] || 0)));
    console.log('reference: sha256 differs from the committed summary (' + g.sha256.slice(0, 16) + ', made with Chrome ' + g.chrome + '; this is Chrome ' + chrome + '); frames ' + g.frames + ' -> ' + N + ', largest fingerprint difference ' + d.toFixed(5) + (g.frames === N && d <= 2e-4 ? ': the same render to the fingerprint\'s precision' : ': THE REFERENCE HAS MOVED')); } }
console.log(`song-ref: ${name}: ${N} frames (${(N / info.sr).toFixed(2)} s) at ${info.sr} Hz, peak ${pk.toFixed(4)}, ${info.pads.length} pads, sha256 ${info.sha256.slice(0, 16)} -> ${OUT}`);
console.log('switched off: ' + JSON.stringify(off) + '; in memory after: ' + JSON.stringify(info.offAfter));
console.log('stats: ' + JSON.stringify(info.stats));
