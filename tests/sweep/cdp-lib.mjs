// Tiny raw-CDP harness for the profiling scripts (core-profile.mjs): its own Chrome (the same flags playwright gives headless
// Chrome, plus a debugging port), a static server for the repo, and a WebSocket client that can talk to child sessions
// (flatten: true), which playwright's CDPSession cannot. That is how the AudioWorklet thread gets its own Profiler.
import http from 'http'; import fs from 'fs'; import path from 'path'; import os from 'os'; import { spawn } from 'child_process'; import { fileURLToPath } from 'url';
export const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm', '.json': 'application/json', '.woff2': 'font/woff2', '.png': 'image/png', '.svg': 'image/svg+xml', '.css': 'text/css', '.webmanifest': 'application/manifest+json', '.wav': 'audio/wav' };
export const sleep = ms => new Promise(r => setTimeout(r, ms));
export function serve(overrides = {}) {   // overrides: { '/core/seq-core.js': '/abs/path/to/a/changed/copy.js' }: a changed copy is served in place of the repo's file; the repo is never edited
  const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    const f = overrides[p] || path.normalize(path.join(ROOT, p)); if (!overrides[p] && f !== ROOT && !f.startsWith(ROOT + path.sep)) { r.writeHead(404); return r.end(); }
    fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': TYPES[path.extname(f)] || 'application/octet-stream' }); r.end(d); }); });
  return new Promise(ok => srv.listen(0, '127.0.0.1', () => ok({ srv, port: srv.address().port })));
}
export async function launch(extra = []) {
  const bin = '/opt/pw-browsers/chromium-1243/real/chrome-linux64/chrome', dir = fs.mkdtempSync(path.join(os.tmpdir(), 'cpchrome-'));
  const args = ['--enable-features=SendGPUChannelEarly,CDPScreenshotNewSurface', '--js-flags=--no-enable-lazy-source-positions', '--no-sandbox', '--disable-field-trial-config', '--disable-background-networking',
    '--disable-background-timer-throttling', '--disable-backgrounding-occluded-windows', '--disable-back-forward-cache', '--disable-breakpad', '--disable-component-update', '--no-default-browser-check', '--disable-default-apps',
    '--disable-extensions', '--disable-hang-monitor', '--disable-ipc-flooding-protection', '--disable-popup-blocking', '--disable-renderer-backgrounding', '--force-color-profile=srgb', '--metrics-recording-only', '--no-first-run',
    '--password-store=basic', '--use-mock-keychain', '--disable-infobars', '--disable-sync', '--enable-unsafe-swiftshader', '--headless', '--hide-scrollbars', '--mute-audio', '--autoplay-policy=no-user-gesture-required',
    '--user-data-dir=' + dir, '--remote-debugging-port=0', ...extra, 'about:blank'];
  const p = spawn(bin, args, { stdio: ['ignore', 'ignore', 'pipe'] });
  const url = await new Promise((ok, no) => { let buf = ''; p.stderr.on('data', d => { buf += d; const m = /DevTools listening on (ws:\S+)/.exec(buf); if (m) ok(m[1]); }); p.on('exit', () => no(new Error('chrome exited'))); setTimeout(() => no(new Error('no devtools url')), 20000); });
  const ws = new WebSocket(url); await new Promise((ok, no) => { ws.onopen = ok; ws.onerror = no; });
  let id = 0; const pend = new Map(), ons = [];
  ws.onmessage = m => { const d = JSON.parse(m.data); if (d.id) { const p = pend.get(d.id); if (p) { pend.delete(d.id); d.error ? p[1](new Error(d.error.message)) : p[0](d.result); } } else for (const f of ons) f(d); };
  const send = (method, params = {}, sessionId) => new Promise((ok, no) => { const i = ++id; pend.set(i, [ok, no]); ws.send(JSON.stringify({ id: i, method, params, ...(sessionId ? { sessionId } : {}) })); });
  const on = f => ons.push(f);
  const close = async () => { try { ws.close(); } catch (e) {} p.kill('SIGKILL'); await sleep(300); fs.rmSync(dir, { recursive: true, force: true }); };
  return { send, on, close, pid: p.pid };
}
export function threads(pid) {   // cpu seconds per thread name from /proc (utime + stime in clock ticks, 100 Hz)
  const o = {}, d = `/proc/${pid}/task`; let names = {};
  for (const t of fs.readdirSync(d)) { try { const st = fs.readFileSync(`${d}/${t}/stat`, 'utf8'), nm = fs.readFileSync(`${d}/${t}/comm`, 'utf8').trim(), f = st.slice(st.lastIndexOf(')') + 2).split(' ');
    o[t + ':' + nm] = (+f[11] + +f[12]) / 100; } catch (e) {} }
  return o;
}

// run-queue delay: /proc/<pid>/task/<tid>/schedstat = cpu ns, ns spent waiting for a CPU, timeslices. The waiting time is what a
// thread loses to other threads (a descheduled audio thread misses its deadline); nonvoluntary switches count the preemptions.
export function sched(pid, nameRe) {
  const o = { run: 0, wait: 0, slices: 0, nvcsw: 0, nivcsw: 0 };
  for (const t of fs.readdirSync(`/proc/${pid}/task`)) { try { const nm = fs.readFileSync(`/proc/${pid}/task/${t}/comm`, 'utf8').trim(); if (!nameRe.test(nm)) continue;
    const [r, w, n] = fs.readFileSync(`/proc/${pid}/task/${t}/schedstat`, 'utf8').trim().split(' ').map(Number); o.run += r; o.wait += w; o.slices += n;
    const st = fs.readFileSync(`/proc/${pid}/task/${t}/status`, 'utf8'); o.nvcsw += +/voluntary_ctxt_switches:\s+(\d+)/.exec(st)[1]; o.nivcsw += +/nonvoluntary_ctxt_switches:\s+(\d+)/.exec(st)[1]; } catch (e) {} }
  return o;
}
export function machine() { const f = fs.readFileSync('/proc/stat', 'utf8').split('\n')[0].split(/\s+/).slice(1).map(Number); return { busy: f[0] + f[1] + f[2] + f[5] + f[6] + (f[7] || 0), idle: f[3] + (f[4] || 0) }; }
