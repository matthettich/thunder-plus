// Which file paths the page may read or write through the desktop shell (S-1 from the native shell's verifier round, ported).
// The page is untrusted (a script in a shared .thunder file could run in it), so main.js only touches a path that
//   1. the person chose in a system dialog this session (Open, Save as, Render to),
//   2. the shell itself handed the page (a file opened from the desktop, a second launch, macOS open-file, a file dropped on the window), or
//   3. an earlier run kept in <userData>/allowed-paths.txt (the last 64, written only by the shell), so "Open recent" keeps working.
// Anything else is refused with { error: 'not allowed: ...' }. No Electron in here: main.js and the tests both load it.
'use strict';
const fs = require('fs'), path = require('path');
const MAX = 64;
const isProj = p => typeof p === 'string' && /\.(thunder|json)$/i.test(p);
const isOut = p => typeof p === 'string' && /\.(wav|zip|mid)$/i.test(p);

// createAllowList({ file, win32, fsImpl, max }): file is where the kept list lives ('' = memory only).
function createAllowList(o) {
  o = o || {};
  const F = o.fsImpl || fs, file = o.file || '', max = o.max || MAX;
  const win = o.win32 === undefined ? process.platform === 'win32' : !!o.win32;
  const P = win ? path.win32 : path;
  const norm = p => {                       // compare paths normalised (case-folded on Windows); '' for anything not an absolute path
    if (typeof p !== 'string' || !p || p.indexOf('\0') >= 0 || !P.isAbsolute(p)) return '';
    const n = P.normalize(p);
    return win ? n.toLowerCase() : n;
  };
  const session = new Set();                // picked / handed over this session
  let kept = [];                            // earlier runs, oldest first, [{key, p}]
  const load = () => {
    try { kept = String(F.readFileSync(file, 'utf8')).split(/\r?\n/).filter(Boolean).filter(p => norm(p)).slice(-max).map(p => ({ key: norm(p), p })); }
    catch (e) { kept = []; }
  };
  const save = () => { if (!file) return; try { F.mkdirSync(P.dirname(file), { recursive: true }); F.writeFileSync(file, kept.map(k => k.p).join('\n') + '\n', 'utf8'); } catch (e) { /* a list that can't be written only costs Open recent across runs */ } };
  const has = p => { const k = norm(p); return !!k && (session.has(k) || kept.some(x => x.key === k)); };
  if (file) load();
  return {
    norm,
    // the shell calls this for a path the person picked or the shell handed over; returns the path (or '' if it isn't an absolute path)
    allow(p) {
      const k = norm(p); if (!k) return '';
      session.add(k);
      const last = kept[kept.length - 1]; if (last && last.key === k && last.p === p) return p;   // already the newest: no disk write (review P3-8)
      kept = kept.filter(x => x.key !== k); kept.push({ key: k, p }); if (kept.length > max) kept = kept.slice(-max);
      save(); return p;
    },
    has,
    list() { return kept.map(x => x.p); },
    // the check each file channel makes: null when fine, else the error text for the page
    check(p, kind) {
      const okExt = kind === 'out' ? isOut(p) : isProj(p);
      if (!okExt) return kind === 'out' ? 'not a render file' : 'not a project file';
      if (!has(p)) return 'not allowed: Thunder Plus only opens or saves files you chose in its own dialogs (use Open or Save as)';
      return null;
    }
  };
}

// URL checks for the navigation guards in main.js: the app's own origin, and web links (which go to the browser).
const APP_ORIGIN = 'app://thunder';
const isAppUrl = u => { try { const x = new URL(String(u)); return x.protocol === 'app:' && x.host === 'thunder'; } catch (e) { return false; } };
const isWebUrl = u => { try { const x = new URL(String(u)); return x.protocol === 'https:' || x.protocol === 'http:'; } catch (e) { return false; } };

module.exports = { createAllowList, isProj, isOut, isAppUrl, isWebUrl, APP_ORIGIN, MAX };
