// SAMPLER CHECK (5 Nov): stereo samples come out in stereo, on both sequencers, and long ones play to the end.
// A pad gets a stereo sample with sound on the LEFT only (a 220 Hz sine) and silence on the right; the song plays it from
// row 0 and the track's input is recorded. Left must carry it, right must stay near silent (a mono path would put it in both).
// A second pass loads a 40 s stereo sample (past the old 30 s limit, and the old 12 s pad-effects cut) and checks the
// pad's render reaches its end.
//
//   node tools/sampler-check.mjs            Page, then Audio thread
//   node tools/sampler-check.mjs --page | --core
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
const browser = await chromium.launch({ executablePath: CHROME, args: ['--autoplay-policy=no-user-gesture-required'] });
let failed = false;
const say = (ok, msg) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${msg}`); if (!ok) failed = true; };
try {
  for (const mode of MODES) {
    const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
    page.on('pageerror', e => console.error('page error:', e.message));
    await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`);
    await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 });
    await page.waitForTimeout(1500);
    const res = await page.evaluate(async mode => {
      const T = window.__seqTest, E = s => T.ev(s);
      const j = E('freshSong()'); j.bpm = 120; j.name = 'sampler check';
      const ph = E('freshPhrase()'); for (const y of [0, 4, 8, 12]){ ph[y][0] = E('ROOT'); ph[y][E('IC')] = 0; } j.phrases[0] = ph; j.plen[0] = 16;
      const sect = Array.from({ length: 16 }, () => [-1, 0]); sect[0] = [0, 0]; j.chains[0] = sect; j.song[0][0] = 0;
      await T.load(JSON.stringify(j), 'sampler-check.thunder');
      const coreOk = await T.coreOn(mode === 'core');
      // pad 0: a stereo sample, a sine on the left, silence on the right
      const stereo = E(`(() => { const sr = 48000, n = sr; const L = new Float32Array(n), R = new Float32Array(n);
        for (let i = 0; i < n; i++) L[i] = 0.5 * Math.sin(2 * Math.PI * 220 * i / sr);
        kit.voices[0].engine = 'layers'; kit.voices[0].layers.forEach(l => { l.on = false; }); kit.voices[0].sel = 0; assignSample(0, L, sr, 'stereo check', R);   // Layers (a fresh song's pad 1 is a Tone drum, which has no sample layers) dirty[0] = true;
        const d = renderVoice(kit.voices[0], sr); return {data2:!!kit.voices[0].layers[0].sample.data2, r:!!d.r}; })()`);
      E('rtInit(); ensure(0);'); for (let k = 0; k < 40 && E('stale(0) || !!pend[0]'); k++) await new Promise(r => setTimeout(r, 50));
      await new Promise(r => setTimeout(r, 600));
      const ac = E('ctx'), pl = E('rt.pl'), inp = pl.buses[0].inp;
      const code = `registerProcessor('smprec', class extends AudioWorkletProcessor { constructor(){ super(); this.on = false; this.e = [0, 0]; this.port.onmessage = e => { if (e.data === 'go'){ this.e = [0, 0]; this.on = true; } else { this.on = false; this.port.postMessage(this.e); } }; }
        process(i){ const a = i[0]; if (this.on && a && a.length){ for (let c = 0; c < 2; c++){ const x = a[Math.min(c, a.length - 1)]; for (let k = 0; k < x.length; k++) this.e[c] += x[k] * x[k]; } } return true; } });`;
      await ac.audioWorklet.addModule(URL.createObjectURL(new Blob([code], { type: 'application/javascript' })));
      const rec = new AudioWorkletNode(ac, 'smprec', { numberOfInputs: 1, numberOfOutputs: 0, channelCount: 2, channelCountMode: 'explicit' });
      inp.connect(rec);
      rec.port.postMessage('go'); E(`tStart('song', {row:0})`);
      await new Promise(r => setTimeout(r, 2500));
      const played = E('tp.core'); E('tStop(true)');
      const e = await new Promise(ok => { rec.port.onmessage = m => ok(m.data); rec.port.postMessage('end'); });
      // and a mono sample on the same pad: centred, the same on both sides
      E(`(() => { const sr = 48000, n = sr, M = new Float32Array(n); for (let i = 0; i < n; i++) M[i] = 0.5 * Math.sin(2 * Math.PI * 220 * i / sr); kit.voices[0].sel = 0; assignSample(0, M, sr, 'mono check'); })()`);
      E('ensure(0);'); for (let k = 0; k < 40 && E('stale(0) || !!pend[0]'); k++) await new Promise(r => setTimeout(r, 50));
      rec.port.onmessage = null; rec.port.postMessage('go'); E(`tStart('song', {row:0})`);
      await new Promise(r => setTimeout(r, 2500)); E('tStop(true)');
      const em = await new Promise(ok => { rec.port.onmessage = m => ok(m.data); rec.port.postMessage('end'); });
      // a long one: 40 s, stereo, with a pad effect on (was cut at 12 s), rendered here
      const long = E(`(() => { const sr = 22050, n = sr * 40; const L = new Float32Array(n), R = new Float32Array(n);
        for (let i = 0; i < n; i++){ L[i] = 0.3 * Math.sin(2 * Math.PI * 110 * i / sr); R[i] = 0.3 * Math.sin(2 * Math.PI * 165 * i / sr); }
        kit.voices[1].engine = 'layers'; kit.voices[1].layers.forEach(l => { l.on = false; }); kit.voices[1].sel = 0; assignSample(1, L, sr, 'long check', R);
        const v = kit.voices[1]; v.fx = [freshFx('eq')].filter(f => fxReady(f));
        const d = renderVoice(v, sr); let last = 0; for (let i = d.length - 1; i > 0; i--) if (Math.abs(d[i]) > 1e-3){ last = i; break; }
        return {secs:d.length / sr, lastSecs:last / sr, r:!!d.r, fx:(v.fx || []).length}; })()`);
      // loops: a 0.25 s sample (a 440 Hz sine fading out), looped 0.10–0.20 s on a 2 s gate: still sounding 1.5 s in, and no
      // click where it loops (the biggest step between samples stays near the sine's own)
      const loops = E(`(() => { const sr = 48000, n = sr / 4, M = new Float32Array(n); for (let i = 0; i < n; i++) M[i] = 0.5 * Math.sin(2 * Math.PI * 440 * i / sr) * (1 - i / n);
        const v = kit.voices[2]; v.engine = 'layers'; v.layers.forEach(l => { l.on = false; }); v.sel = 0; v.gate = 2000; v.len = 3; v.level = 0; v.drive = 0; v.fx = []; assignSample(2, M, sr, 'loop check');
        const L = v.layers[0], out = {};
        for (const [nm, m, x] of [['off', 0, 0], ['forward', 1, 0.2], ['ping-pong', 2, 0]]){ L.lpMode = m; L.lpS = 0.4; L.lpE = 0.8; L.lpX = x; if (m){ L.mode = 'ADSR'; L.sus = 1; L.rel = 60; }
          const d = renderVoice(v, sr); let late = 0, step = 0; for (let i = Math.round(1.4 * sr); i < Math.min(d.length, 1.6 * sr); i++) late = Math.max(late, Math.abs(d[i]));
          for (let i = 1; i < Math.min(d.length, 1.9 * sr); i++) step = Math.max(step, Math.abs(d[i] - d[i - 1]));
          out[nm] = {secs:d.length / sr, late, step}; }
        return out; })()`);
      return { coreOk, played, stereo, e, em, long, loops };
    }, mode).catch(err => ({ err: String(err && err.message || err) }));
    if (res.err) { say(false, `${mode}: ${res.err}`); await page.close(); continue; }
    const db = x => (10 * Math.log10(x + 1e-30)).toFixed(1);
    say(res.stereo.data2 && res.stereo.r, `${mode}: the sample keeps its right channel and the pad renders in stereo`);
    say(res.e[0] > 1 && res.e[1] < res.e[0] * 1e-4, `${mode} (played by ${res.played ? 'the audio thread' : 'the page'}): left ${db(res.e[0])} dB, right ${db(res.e[1])} dB (right should be ≥ 40 dB down)`);
    say(res.em[0] > 1 && Math.abs(10 * Math.log10(res.em[0] / res.em[1])) < 0.1, `${mode}: a mono sample plays centred (left ${db(res.em[0])} dB, right ${db(res.em[1])} dB)`);
    if (mode === MODES[0]){ const L = res.loops, sine = 0.5 * 2 * Math.PI * 440 / 48000;   // the biggest step a 0.5 sine at 440 Hz takes
      say(L.off.late < 1e-3, `loop off: the sample ends (${L.off.secs.toFixed(2)} s render)`);
      for (const k of ['forward', 'ping-pong']) say(L[k].late > 0.05 && L[k].step < sine * 1.6, `loop ${k}: still sounding 1.5 s in (peak ${L[k].late.toFixed(2)}), no click at the seam (step ${L[k].step.toFixed(3)} vs the sine's ${sine.toFixed(3)})`); }
    if (mode === MODES[0]) say(res.long.r && res.long.lastSecs > 39, `a 40 s stereo sample${res.long.fx ? ' with a pad effect' : ''} renders to ${res.long.lastSecs.toFixed(1)} s of ${res.long.secs.toFixed(1)} s`);
    await page.close();
  }
} finally { await browser.close(); srv.close(); }
process.exit(failed ? 1 : 0);
