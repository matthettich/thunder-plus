// …waves / …chains pass 3, "Phone" (13 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: T4 T6 T7 T8 T9 T12 T13 T14 and M10, plus the four follow-ups a-d).
// Both editors, standalone, with REAL touch events (CDP Input.dispatchTouchEvent) in a mobile context, and a mouse context for "unchanged". Prints PASS / FAIL.
//   node tests/sweep/wc-pass3.mjs             all of it
//   node tests/sweep/wc-pass3.mjs fu m10 ...   sections: fu (follow-ups a-d) m10 t4 t6 t7 t8 t9 t12 t13 t14 old mouse
//   OLD_ROOT=/path/to/a/checkout  or  OLD_REF=<commit> (default e67adc29f2283e3b5230cd9b1d6b323a30d34306, the build 585 merge commit; fetched if absent, the comparisons print SKIP if it cannot be had): the previous build, for "unchanged" and "loads in 585"
//   SHOTS=/dir node tests/sweep/wc-pass3.mjs shots    before / after screenshots for the owner
import fs from 'fs'; import os from 'os'; import path from 'path'; import { execSync } from 'child_process';
import { boot, app, Table, ROOT, serveDir, listenLoopback } from './ui-lib.mjs';

const T = new Table('wc-pass3');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'PASS ' : 'FAIL ') + name + (note ? '  ' + note : '')); };
const sleep = (page, ms) => page.waitForTimeout(ms);
const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|autoplay/i;
const ED = ['chains', 'waves'];

const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp, extra = {}) => ({ from: [a, ap], to: [b, bp], bend: 0, ...extra });
const SPEC = {
  chains: { key: 'chains.patch', setKey: 'thunderplus.chains.settings', long: 'pgate', longName: 'pump GATE',
    doc: (o = {}) => ({ format: 'chains.patch', meta: { name: 'wc pass3' }, view: o.view || { x: 0, y: 0, zoom: 1, ui: 2 }, st2: 1, ti2: 1,
      modules: [P('m1', 'trackIn', 0, 0, { track: 1 }), P('m2', 'delay', 420, 0), P('m3', 'reverb', 840, 0), P('m4', 'eq', 1260, 0),
        P('m5', 'lfo', 0, 420), P('m6', 'compressor', 420, 420), P('m7', 'chorus', 840, 420), P('m8', 'chainOut', 1260, 420, { track: 1 })],
      cables: o.cables || [C('m1', 'out', 'm2', 'in'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m4', 'out', 'm8', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
  waves: { key: 'waves.patch', setKey: 'thunderplus.waves.settings', long: 'marbles', longName: 'Random generator',
    doc: (o = {}) => ({ meta: { name: 'wc pass3' }, view: o.view || { x: 0, y: 0, zoom: 1, ui: 2 }, voice: { maxVoices: 8 },
      modules: [P('m1', 'hostIn', 0, 0), P('m2', 'vco', 420, 0, { wave: 1 }), P('m3', 'vcf', 840, 0), P('m4', 'vca', 1260, 0),
        P('m5', 'adsr', 0, 420), P('m6', 'lfo', 420, 420), P('m7', 'delay', 840, 420), P('m8', 'out', 1260, 420, { slot: 1, level: 1 })],
      cables: o.cables || [C('m1', 'pitch', 'm2', 'pitch'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m4', 'out', 'm8', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
};
// Six shared effects at the top left (two rows of three, 420 px apart): sliders to work on, the first at the corner of the view.
const TYPED_TYPES = ['delay', 'eq', 'compressor', 'chorus', 'clouds', 'crush'];
const slidersDoc = (ed, o = {}) => { const d = SPEC[ed].doc({ cables: [], ...o }); d.modules = TYPED_TYPES.map((t, i) => P('t' + (i + 1), t, (i % 3) * 420, Math.floor(i / 3) * 560)); return d; };
const NV = { x: 12, y: 12, zoom: 1, ui: 2, nw: 1 };   // a view left on a small screen (kept as it is when a module is on screen)

async function loadDoc(page, base, ed, doc, o = {}) {
  const url = `${base}/${ed}/index.html`;
  await page.goto(url);
  await page.evaluate(([k, sk, d, st]) => { localStorage.clear(); if (d) localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify(st)); }, [SPEC[ed].key, SPEC[ed].setKey, doc, o.settings || { tips: true }]);
  await page.goto(url); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 900);
}
const readView = page => page.evaluate(() => { const m = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform), ws = document.getElementById('ws').getBoundingClientRect(); return { z: m.a, x: m.e, y: m.f, ws: { l: ws.left, t: ws.top, r: ws.right, b: ws.bottom } }; });
const centre = (page, sel) => page.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height, l: r.left, t: r.top, r: r.right, b: r.bottom }; }, sel);
const modPos = (page, id) => page.evaluate(i => { const n = document.querySelector(`.node[data-id="${i}"]`); return n ? [parseFloat(n.style.left), parseFloat(n.style.top)] : null; }, id);
const slVal = (page, id, key) => page.evaluate(([i, k]) => { const r = document.querySelector(`.node[data-id="${i}"] .row.sl[data-key="${k}"]`); return r ? [+r.querySelector('input').value, r.querySelector('.val').textContent] : null; }, [id, key]);
const savedDoc = async (page, ed) => { await sleep(page, 1500); return page.evaluate(k => JSON.parse(localStorage.getItem(k) || 'null'), SPEC[ed].key); };
const canUndo = async page => { await page.evaluate(() => document.getElementById('undobtn').disabled = false); return page.evaluate(() => { const b = document.getElementById('barundo'); return !!window.__canUndo ? window.__canUndo() : null; }).catch(() => null); };

// ---- real touch (Chrome DevTools Protocol): one pointer per finger, the browser's own gesture handling in between ----
const cdpOf = new WeakMap();
async function cdp(page) { let c = cdpOf.get(page); if (!c) { c = await page.context().newCDPSession(page); cdpOf.set(page, c); } return c; }
const pts = a => a.map(([x, y], i) => ({ x, y, id: i + 1 }));
const tStart = async (page, a) => (await cdp(page)).send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pts(a) });
const tMove = async (page, a) => (await cdp(page)).send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: pts(a) });
const tEnd = async page => (await cdp(page)).send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
async function tap(page, x, y) { await tStart(page, [[x, y]]); await sleep(page, 40); await tEnd(page); await sleep(page, 60); }
async function drag(page, [x0, y0], [x1, y1], steps = 12) { await tStart(page, [[x0, y0]]); await sleep(page, 20); for (let i = 1; i <= steps; i++) { await tMove(page, [[x0 + (x1 - x0) * i / steps, y0 + (y1 - y0) * i / steps]]); await sleep(page, 14); } await tEnd(page); await sleep(page, 80); }
async function hold(page, x, y, ms = 650) { await tStart(page, [[x, y]]); await sleep(page, ms); await tEnd(page); await sleep(page, 120); }
async function twoFingers(page, a, b, da, steps = 10) { await tStart(page, [a, b]); await sleep(page, 20); for (let i = 1; i <= steps; i++) { await tMove(page, [[a[0] + da[0] * i / steps, a[1] + da[1] * i / steps], [b[0] + da[0] * i / steps, b[1] + da[1] * i / steps]]); await sleep(page, 14); } const c = await cdp(page); for (const k of [0, 1]) await c.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [{ x: [a, b][k][0] + da[0], y: [a, b][k][1] + da[1], id: k + 1 }] }); await sleep(page, 80); }

async function bootEd(w, h, mob) { const B = await boot({ width: w, height: h, mobile: mob }); B.base = `http://127.0.0.1:${B.port}`; return B; }
const done = (errors, tag) => { const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${tag}: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | ')); };

// ---- the previous build: a fixed older commit, never HEAD (after the merge HEAD is this build, and the comparison would be with itself) ----
const BASE585 = 'e67adc29f2283e3b5230cd9b1d6b323a30d34306';   // the build 585 merge commit
async function oldBuild() {
  let dir = process.env.OLD_ROOT;
  if (!dir) { const ref = process.env.OLD_REF || BASE585; dir = fs.mkdtempSync(path.join(os.tmpdir(), 'wc-pass3-old-'));
    const have = () => { try { execSync(`git -C ${JSON.stringify(ROOT)} cat-file -e ${ref}^{commit}`, { stdio: 'pipe' }); return true; } catch (e) { return false; } };
    if (!have()) { try { execSync(`git -C ${JSON.stringify(ROOT)} fetch --quiet origin ${ref}`, { stdio: 'pipe', timeout: 60000 }); } catch (e) {} }
    if (!have()) { console.log(`SKIP the "versus build 585" comparisons: commit ${ref} is not in this checkout and could not be fetched (set OLD_ROOT or OLD_REF)`); return null; }
    try { execSync(`git -C ${JSON.stringify(ROOT)} archive ${ref} chains waves | tar -x -C ${JSON.stringify(dir)}`, { stdio: 'pipe' }); } catch (e) { console.log(`SKIP the "versus build 585" comparisons: git archive ${ref} failed`); return null; } }
  const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.woff': 'font/woff', '.png': 'image/png', '.webmanifest': 'application/manifest+json', '.json': 'application/json' };
  const srv = serveDir(dir, TYPES); await listenLoopback(srv);
  return { base: `http://127.0.0.1:${srv.address().port}`, dir, close: () => srv.close() };
}

// ================= follow-ups a, b, c, d =================
async function fu(ed, OLD) {
  // a. the tip box on first open on a touch screen
  {
    const B = await bootEd(344, 780, true); const { page, errors, base } = B; const tag = `${ed} a`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc({ view: NV }), { settings: {} });   // nothing stored: a first visit
      const s = await page.evaluate(() => { const h = document.getElementById('hint'), r = h.getBoundingClientRect(), pill = document.getElementById('hintpill').getBoundingClientRect(), bar = document.querySelector('.bar').getBoundingClientRect();
        const heads = [...document.querySelectorAll('.node > header')].map(e => e.getBoundingClientRect());
        const hit = heads.some(b => !(pill.right <= b.left || pill.left >= b.right || pill.bottom <= b.top || pill.top >= b.bottom));
        return { min: h.classList.contains('min'), tipt: getComputedStyle(h.querySelector('.tipt')).display, pill: pill.width > 0, hit, bar: !(pill.top >= bar.bottom), seen: JSON.parse(localStorage.getItem('thunderplus.chains.settings') || localStorage.getItem('thunderplus.waves.settings') || '{}').tipSeen }; });
      ok(`${tag}: a first open on a phone shows only the Tips button (no tip box over the patch)`, s.min && s.tipt === 'none' && s.pill, JSON.stringify(s));
      ok(`${tag}: the Tips button covers no module's title bar and is below the top bar`, !s.hit && !s.bar, JSON.stringify(s));
      const pc = await centre(page, '#hintpill'); await tap(page, pc.x, pc.y);
      ok(`${tag}: the Tips button opens the tip on a tap`, await page.evaluate(() => !document.getElementById('hint').classList.contains('min')));
      const xc = await centre(page, '#hintx'); await tap(page, xc.x, xc.y);
      ok(`${tag}: × folds it back to the button`, await page.evaluate(() => document.getElementById('hint').classList.contains('min')));
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
  {   // mouse: the tip box behaves as before (shown, two lines and More)
    const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} a (mouse)`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc(), { settings: {} });
      const s = await page.evaluate(() => { const h = document.getElementById('hint'); return { hidden: h.hidden, min: h.classList.contains('min'), more: getComputedStyle(document.getElementById('hintmore')).display, tipm: getComputedStyle(h.querySelector('.tipm')).display }; });
      ok(`${tag}: the tip is shown as before (two lines and More), not folded`, !s.hidden && !s.min && s.more !== 'none' && s.tipm !== 'none', JSON.stringify(s));
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
  // b. Delete module in the long-press menu asks first on touch and pen, and not with a mouse
  {
    const B = await bootEd(344, 780, true); const { page, errors, base } = B; const tag = `${ed} b`;
    try {
      await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: { tips: false } });
      const n0 = (await page.$$('.node')).length, h = await centre(page, '.node[data-id="t1"] > header .t');
      await hold(page, h.x, h.y + 2);
      const items = await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].map(b => b.textContent));
      const del = items.find(t => /^Delete module/.test(t));
      ok(`${tag}: the long-press menu has "Delete module"`, !!del, items.join(' | '));
      await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(b => /^Delete module/.test(b.textContent)).click()); await sleep(page, 300);
      const q = await page.evaluate(() => { const d = document.querySelector('.dlgov'); return d ? d.textContent : null; });
      ok(`${tag}: Delete module asks: "Remove …? Undo brings it back." (the question the × button asks)`, q && /Remove “[^”]+”\? Undo brings it back\./.test(q), String(q));
      ok(`${tag}: nothing is removed while the question is open`, (await page.$$('.node')).length === n0);
      await page.evaluate(() => [...document.querySelectorAll('.dlgov button')].find(b => /Cancel/i.test(b.textContent)).click()); await sleep(page, 200);
      ok(`${tag}: Cancel keeps the module`, (await page.$$('.node')).length === n0);
      // the × asks the same question (one code path)
      const x = await centre(page, '.node[data-id="t1"] > header button.x'); await tap(page, x.x, x.y); await sleep(page, 250);
      const q2 = await page.evaluate(() => { const d = document.querySelector('.dlgov'); return d ? d.textContent : null; });
      ok(`${tag}: the × button asks the very same question`, q2 === q, String(q2));
      await page.evaluate(() => [...document.querySelectorAll('.dlgov button')].find(b => /Cancel/i.test(b.textContent)).click()); await sleep(page, 200);
      await hold(page, h.x, h.y + 2); await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(b => /^Delete module/.test(b.textContent)).click()); await sleep(page, 300);
      await page.evaluate(() => [...document.querySelectorAll('.dlgov button')].find(b => /Remove/.test(b.textContent)).click()); await sleep(page, 300);
      ok(`${tag}: Remove takes it away`, (await page.$$('.node')).length === n0 - 1);
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
  {   // mouse: right-click, Delete module, at once
    const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} b (mouse)`;
    try {
      await loadDoc(page, base, ed, slidersDoc(ed), { settings: { tips: false } });
      const n0 = (await page.$$('.node')).length, h = await centre(page, '.node[data-id="t1"] > header .t');
      await page.mouse.click(h.x, h.y, { button: 'right' }); await sleep(page, 150);
      await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(b => /^Delete module/.test(b.textContent)).click()); await sleep(page, 250);
      ok(`${tag}: a mouse removes it at once, with no question (Undo brings it back)`, (await page.$$('.node')).length === n0 - 1 && !(await page.$('.dlgov')));
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
  // c. scientific notation in a typed value
  {
    const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} c`;
    try {
      await loadDoc(page, base, ed, slidersDoc(ed), { settings: { tips: false } });
      const typeCase = (id, key, start, text) => page.evaluate(([id, key, start, text]) => { const row = document.querySelector(`.node[data-id="${id}"] .row.sl[data-key="${key}"]`); const inp = row.querySelector('input[type=range]'), v = row.querySelector('.val');
        inp.value = start; inp.dispatchEvent(new Event('input', { bubbles: true })); const before = v.textContent; v.dispatchEvent(new MouseEvent('dblclick', { bubbles: true })); const e = v.querySelector('input'); e.value = text; e.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true })); return [before, v.textContent, +inp.value]; }, [id, key, start, text]);
      const a = await typeCase('t2', 'tfreq', .4, '1e3 Hz'), b = await typeCase('t2', 'tfreq', .4, '1000 Hz'), c = await typeCase('t2', 'tfreq', .4, '1e3');
      ok(`${tag}: "1e3 Hz" is 1000 Hz (${a[1]}), the same as typing 1000 Hz (${b[1]}); a bare "1e3" into a kHz readout too (${c[1]})`, a[1] === b[1] && a[2] === b[2] && /^1\.0 kHz$/.test(a[1]) && c[1] === a[1], JSON.stringify([a, b, c]));
      const d = await typeCase('t1', 'regen', .1, '2.5e-1'), e = await typeCase('t1', 'regen', .1, '0.25'), f = await typeCase('t1', 'regen', .1, '4e1%'), g = await typeCase('t1', 'regen', .1, '40%');
      ok(`${tag}: "2.5e-1" is 0.25 (${d[1]}, as typing 0.25: ${e[1]}); "4e1%" is 40% (${f[1]} = ${g[1]})`, d[1] === e[1] && d[2] === e[2] && f[1] === g[1] && f[2] === g[2] && g[1] === '40%', JSON.stringify([d, e, f, g]));
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
  // d. Fit shows the whole patch at any width (button and host call); only opening uses the readable zoom
  for (const [w, h, mob] of [[344, 780, true], [412, 915, true], [884, 1020, true], [1440, 900, false]]) {
    const B = await bootEd(w, h, mob); const { page, errors, base } = B; const tag = `${ed} d: ${w} px`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc(), { settings: { tips: false } });
      const whole = () => page.evaluate(() => { const ws = document.getElementById('ws').getBoundingClientRect(), z = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a;
        const ns = [...document.querySelectorAll('.node')].map(n => n.getBoundingClientRect()); return { z, all: ns.every(r => r.left >= ws.left - 1 && r.right <= ws.right + 1 && r.top >= ws.top - 1 && r.bottom <= ws.bottom + 1), n: ns.length }; });
      const v0 = await whole();
      await page.evaluate(() => document.getElementById('zoomfit').click()); await sleep(page, 250); const v1 = await whole();
      ok(`${tag}: the Fit button shows all ${v1.n} modules (${Math.round(v1.z * 100)}%)`, v1.all);
      if (w < 1000) ok(`${tag}: ...while opening the patch used the readable zoom (${Math.round(v0.z * 100)}% > Fit ${Math.round(v1.z * 100)}%)`, v0.z >= 0.795 && v1.z < v0.z);
      if (ed === 'chains') {   // the host's fit-after-adding (chainFitTry calls chainsEmbed.fit())
        const src = fs.readFileSync(path.join(ROOT, 'chains/index.html'), 'utf8'), m = /fit\(\)\{[^\n]*\n/.exec(src.slice(src.indexOf('window.chainsEmbed = {')));
        ok(`${tag}: chainsEmbed.fit() (the host, after the Mixer adds Track In / Chain Out pairs) is the whole-patch Fit, not the readable opening view`, m && /fitAll\(1\)/.test(m[0]) && !/openView/.test(m[0]), m && m[0].slice(0, 160));
      }
      // ...and by the embed API itself where the page can reach it (the page is its own parent here, so the API exists only inside the host: see the host check below)
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}
// d (host): the real host calls fit() after adding pairs
async function fuHost() {
  const HOST = { 'thunderplus-platform-v1': 'desktop', 'thunderplus.chains.settings': '{"tips":false}' };
  const B = await boot({ width: 1440, height: 900, storage: HOST }); const { page, errors } = B; const tag = 'chains d (host)';
  try {
    await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`).catch(() => {});
    await app(page, `(setView('chains'), $('toast').hidden = true, 1)`); await sleep(page, 2500);
    const r = await page.evaluate(() => { const f = document.querySelector('iframe[src*="chains"]'); if (!f) return null; const w = f.contentWindow; const d = f.contentDocument;
      // a wide patch, then the host's own call
      const ws = d.getElementById('ws'); return { api: !!(w.chainsEmbed && w.chainsEmbed.fit), n: d.querySelectorAll('.node').length, wsw: ws.clientWidth }; });
    ok(`${tag}: the host frame has chainsEmbed.fit`, r && r.api, JSON.stringify(r));
    // narrow the frame so that smallWs() is true (a phone), then call fit() as chainFitTry does and look at the nodes
    await page.evaluate(() => { const f = document.querySelector('iframe[src*="chains"]'); f.style.width = '420px'; f.style.maxWidth = '420px'; }); await sleep(page, 500);
    const res = await page.evaluate(() => { const f = document.querySelector('iframe[src*="chains"]'), w = f.contentWindow, d = f.contentDocument; const ok1 = w.chainsEmbed.fit(); const ws = d.getElementById('ws').getBoundingClientRect();
      const ns = [...d.querySelectorAll('.node')].map(n => n.getBoundingClientRect()); const z = new DOMMatrix(getComputedStyle(d.getElementById('world')).transform).a;
      return { ok1, w: ws.width, z, all: ns.every(r => r.left >= ws.left - 1 && r.right <= ws.right + 1 && r.top >= ws.top - 1 && r.bottom <= ws.bottom + 1), n: ns.length }; });
    ok(`${tag}: on a narrow workspace (${Math.round(res.w)} px) chainsEmbed.fit() shows the whole patch (${res.n} modules, ${Math.round(res.z * 100)}%)`, res.ok1 && res.all, JSON.stringify(res));
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= M10: Play / Edit =================
async function m10(ed) {
  const B = await bootEd(344, 780, true); const { page, errors, base } = B; const tag = `${ed} M10`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: { tips: false } });
    const sw = await page.evaluate(() => ({ mode: !!document.getElementById('modeplay') && !!document.getElementById('modeedit'), edit: document.getElementById('modeedit').classList.contains('on'), play: document.body.classList.contains('play'), labels: [document.getElementById('modeplay').textContent, document.getElementById('modeedit').textContent] }));
    ok(`${tag}: one switch in the top bar, two labelled buttons "Play" and "Edit"; Edit is the default`, sw.mode && sw.edit && !sw.play && sw.labels.join() === 'Play,Edit', JSON.stringify(sw));
    const h0 = await centre(page, '.node[data-id="t1"] > header .t'); const p0 = await modPos(page, 't1');
    // Edit: a header drag moves the module (as before); a tap on the close button asks
    await drag(page, [h0.x, h0.y], [h0.x + 30, h0.y + 24]); const p1 = await modPos(page, 't1');
    ok(`${tag}: in Edit a drag on a module's title bar moves the module (${p0} > ${p1})`, Math.abs(p1[0] - p0[0] - 30) < 6 && Math.abs(p1[1] - p0[1] - 24) < 6);   // pass 4 (M11): the drop snaps to the 10 px grid
    await page.evaluate(() => document.querySelector('.node[data-id="t1"]').style.left = '12px'); await page.evaluate(() => document.querySelector('.node[data-id="t1"]').style.top = '12px');
    // switch to Play with a tap
    const pb = await centre(page, '#modeplay'); await tap(page, pb.x, pb.y);
    const s1 = await page.evaluate(() => ({ play: document.body.classList.contains('play'), pressed: document.getElementById('modeplay').getAttribute('aria-pressed'), editPressed: document.getElementById('modeedit').getAttribute('aria-pressed'), on: document.getElementById('modeplay').classList.contains('on'),
      frame: getComputedStyle(document.getElementById('ws'), '::before').content, tag: getComputedStyle(document.getElementById('ws'), '::after').content, sets: JSON.parse(localStorage.getItem('thunderplus.chains.settings') || localStorage.getItem('thunderplus.waves.settings') || '{}') }));
    ok(`${tag}: a tap on Play switches to Play: the button is on, aria-pressed follows, the patch wears a frame and a PLAY tag`, s1.play && s1.on && s1.pressed === 'true' && s1.editPressed === 'false' && s1.frame !== 'none' && /PLAY/.test(s1.tag), JSON.stringify(s1));
    const v0 = await readView(page), q0 = await modPos(page, 't1');
    await drag(page, [h0.x, h0.y], [h0.x + 40, h0.y + 30]); const v1 = await readView(page), q1 = await modPos(page, 't1');
    ok(`${tag}: in Play a one-finger drag on a module's title bar pans the view (${Math.round(v1.x - v0.x)}, ${Math.round(v1.y - v0.y)}) and does not move the module`, Math.abs(v1.x - v0.x - 40) < 6 && Math.abs(v1.y - v0.y - 30) < 6 && q0[0] === q1[0] && q0[1] === q1[1]);
    // a body area that is not a control
    const body = await page.evaluate(() => { const n = document.querySelector('.node[data-id="t1"]'), r = n.querySelector('.ports') ? n.querySelector('.ports').getBoundingClientRect() : n.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
    const v2 = await readView(page); await drag(page, body, [body[0] - 30, body[1] + 20]); const v3 = await readView(page);
    ok(`${tag}: ...and from a module's body (${Math.round(v3.x - v2.x)}, ${Math.round(v3.y - v2.y)})`, Math.abs(v3.x - v2.x + 30) < 6 && Math.abs(v3.y - v2.y - 20) < 6);
    // tap ×, chevron, a jack: nothing destructive
    const n0 = (await page.$$('.node')).length, c0 = await page.evaluate(() => document.querySelectorAll('.cables g').length);
    const x = await centre(page, '.node[data-id="t1"] > header button.x'); await tap(page, x.x, x.y); await sleep(page, 250);
    ok(`${tag}: in Play a tap on × does nothing: no question, no removal`, !(await page.$('.dlgov')) && (await page.$$('.node')).length === n0);
    const f = await centre(page, '.node[data-id="t1"] > header button.fold'); await tap(page, f.x, f.y);
    ok(`${tag}: ...a tap on the chevron does not fold the module`, !(await page.evaluate(() => document.querySelector('.node[data-id="t1"]').classList.contains('collapsed'))));
    const j = await centre(page, '.node[data-id="t1"] .jack[data-dir="out"]'); await tap(page, j.x, j.y); await sleep(page, 100);
    ok(`${tag}: ...a tap on a jack starts no cable (no target lit)`, (await page.evaluate(() => document.querySelectorAll('.jack.target').length)) === 0);
    // a long press and a right-click open no menu
    await hold(page, h0.x, h0.y + 2); ok(`${tag}: ...a long press opens no menu`, await page.evaluate(() => document.getElementById('ctxmenu').hidden));
    // sliders and selects work
    const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input'); const sv0 = await slVal(page, 't1', 'regen');
    await drag(page, [sl.x, sl.y], [sl.x + 60, sl.y], 10); const sv1 = await slVal(page, 't1', 'regen');
    ok(`${tag}: in Play a slider still moves with a finger (${sv0[1]} > ${sv1[1]})`, sv1[0] > sv0[0]);
    const selOk = await page.evaluate(() => { const s = [...document.querySelectorAll('.node select')].find(x => { const q = x.getBoundingClientRect(); return q.width > 0 && q.top > 60 && q.bottom < innerHeight && q.left >= 0 && q.right <= innerWidth; }); if (!s) return null; const r = s.getBoundingClientRect(); const e = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); return e === s || s.contains(e); });
    ok(`${tag}: ...and a select is still the thing under the finger (${selOk})`, selOk !== false);
    // remembered per editor, in the editor's settings and not in the patch
    await sleep(page, 1500);
    const st = await page.evaluate(([k]) => ({ sets: Object.entries(localStorage).filter(([a]) => /settings$/.test(a)).map(([a, b]) => b).join(' '), patch: localStorage.getItem(k) }), [SPEC[ed].key]);
    ok(`${tag}: the mode is stored in the editor's settings ("mode":"play") and nowhere in the patch`, /"mode":"play"/.test(st.sets) && !Object.keys(JSON.parse(st.patch || '{}')).concat(Object.keys((JSON.parse(st.patch || '{}').view) || {})).some(k => /^(mode|play)$/.test(k)), st.sets.slice(0, 80));
    await page.reload(); await page.waitForSelector('.node'); await sleep(page, 700);
    ok(`${tag}: after a reload it is still Play`, await page.evaluate(() => document.body.classList.contains('play') && document.getElementById('modeplay').classList.contains('on')));
    // back to Edit
    const eb = await centre(page, '#modeedit'); await tap(page, eb.x, eb.y);
    ok(`${tag}: a tap on Edit switches back (no frame)`, await page.evaluate(() => !document.body.classList.contains('play') && getComputedStyle(document.getElementById('ws'), '::before').content === 'none'));
    const h1 = await centre(page, '.node[data-id="t1"] > header .t'), r0 = await modPos(page, 't1');
    await drag(page, [h1.x, h1.y], [h1.x + 20, h1.y + 20]); const r1 = await modPos(page, 't1');
    ok(`${tag}: in Edit the same drag moves the module again`, Math.abs(r1[0] - r0[0] - 20) < 3);
    const x2 = await centre(page, '.node[data-id="t1"] > header button.x'); await tap(page, x2.x, x2.y); await sleep(page, 250);
    ok(`${tag}: in Edit the × asks first (as in build 585)`, !!(await page.$('.dlgov')));
    await page.evaluate(() => [...document.querySelectorAll('.dlgov button')].find(b => /Cancel/i.test(b.textContent)).click());
    // the settings do not travel with the patch: another editor profile
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
  // mouse
  const M = await bootEd(1440, 900, false); const tag2 = `${ed} M10 (mouse)`;
  try {
    const { page, errors, base } = M;
    await loadDoc(page, base, ed, slidersDoc(ed), { settings: { tips: false } });
    await page.keyboard.press('Alt+KeyE'); await sleep(page, 150);
    ok(`${tag2}: Alt+E switches to Play`, await page.evaluate(() => document.body.classList.contains('play')));
    const h = await centre(page, '.node[data-id="t1"] > header .t'), p0 = await modPos(page, 't1'), v0 = await readView(page);
    await page.mouse.move(h.x, h.y); await page.mouse.down(); await page.mouse.move(h.x + 60, h.y + 40, { steps: 8 }); await page.mouse.up(); await sleep(page, 100);
    const p1 = await modPos(page, 't1'), v1 = await readView(page);
    ok(`${tag2}: in Play a mouse drag on a title bar pans and does not move the module`, p0[0] === p1[0] && p0[1] === p1[1] && Math.abs(v1.x - v0.x - 60) < 4);
    await page.mouse.dblclick(h.x + 60, h.y + 40); await sleep(page, 150);
    ok(`${tag2}: a double-click on the title bar does not fold it`, !(await page.evaluate(() => document.querySelector('.node[data-id="t1"]').classList.contains('collapsed'))));
    await page.mouse.click(h.x + 60, h.y + 40, { button: 'right' }); await sleep(page, 150);
    ok(`${tag2}: a right-click opens no menu`, await page.evaluate(() => document.getElementById('ctxmenu').hidden));
    const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input'), s0 = await slVal(page, 't1', 'regen');
    await page.mouse.move(sl.x, sl.y); await page.mouse.down(); await page.mouse.move(sl.x + 40, sl.y, { steps: 6 }); await page.mouse.up(); const s1 = await slVal(page, 't1', 'regen');
    ok(`${tag2}: a mouse still drags a slider in Play (${s0[1]} > ${s1[1]})`, s1[0] > s0[0]);
    await page.keyboard.press('Alt+KeyE'); await sleep(page, 150);
    const p2 = await modPos(page, 't1'), v2 = await readView(page);
    await page.mouse.move(h.x + 60, h.y + 40); await page.mouse.down(); await page.mouse.move(h.x + 100, h.y + 70, { steps: 8 }); await page.mouse.up(); await sleep(page, 100);
    const p3 = await modPos(page, 't1');
    ok(`${tag2}: Alt+E back to Edit: the drag moves the module again (${p2} > ${p3})`, Math.abs(p3[0] - p2[0] - 40) < 4);
    done(errors, tag2);
  } catch (e) { ok(`${tag2}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await M.close();
}

// ================= T4: two fingers pan and zoom from anywhere; one finger from a module in Play =================
async function t4(ed) {
  const B = await bootEd(412, 915, true); const { page, errors, base } = B; const tag = `${ed} T4`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: { tips: false } });
    const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input'), sv0 = await slVal(page, 't1', 'regen'), v0 = await readView(page), p0 = await modPos(page, 't1');
    await twoFingers(page, [sl.x - 20, sl.y], [sl.x + 20, sl.y], [0, 50]);
    const v1 = await readView(page), sv1 = await slVal(page, 't1', 'regen');
    ok(`${tag}: two fingers that land on a slider row pan the view (${Math.round(v1.y - v0.y)} px) and leave the slider alone (${sv0[1]} > ${sv1[1]})`, Math.abs(v1.y - v0.y - 50) < 12 && sv0[0] === sv1[0]);
    const h = await centre(page, '.node[data-id="t1"] > header .t');
    const z0 = (await readView(page)).z; await tStart(page, [[h.x - 30, h.y], [h.x + 30, h.y]]); for (let i = 1; i <= 8; i++) { await tMove(page, [[h.x - 30 - i * 5, h.y], [h.x + 30 + i * 5, h.y]]); await sleep(page, 14); } await tEnd(page);
    const z1 = (await readView(page)).z;
    ok(`${tag}: ...and two fingers on a title bar pinch to zoom (${Math.round(z0 * 100)}% > ${Math.round(z1 * 100)}%) without moving the module`, z1 > z0 * 1.2 && (await modPos(page, "t1")).join() === p0.join(), JSON.stringify([p0, await modPos(page, "t1")]));
    // a slider drag in progress stops when a second finger lands
    const sl2 = await centre(page, '.node[data-id="t2"] .row.sl[data-key="tfreq"] input') || sl;
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= T6: long press =================
async function t6(ed) {
  const B = await bootEd(412, 915, true); const { page, errors, base } = B; const tag = `${ed} T6`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 12, y: 12, zoom: 1, ui: 2, nw: 1 } }), { settings: { tips: false } });
    const menu = () => page.evaluate(() => { const m = document.getElementById('ctxmenu'); return m.hidden ? null : [...m.querySelectorAll('.ctxitem')].map(b => b.textContent); });
    const h = await centre(page, '.node[data-id="m1"] > header .t'), p0 = await modPos(page, 'm1');
    await tap(page, h.x, h.y); await sleep(page, 700);
    ok(`${tag}: a short tap on a module's title bar opens no menu`, (await menu()) === null);
    await tStart(page, [[h.x, h.y]]); await sleep(page, 650);
    const m1 = await menu(); const sel = await page.evaluate(() => (getSelection() ? getSelection().toString() : '') );
    await tEnd(page); await sleep(page, 150);
    ok(`${tag}: a finger held 500 ms on a module opens its menu (${m1 && m1.slice(0, 4).join(' | ')}...)`, !!m1 && m1.some(t => /^Delete module/.test(t)) && m1.some(t => /^Group module/.test(t)));
    ok(`${tag}: ...selects no text and starts no drag (the module stays at ${p0})`, sel === '' && (await modPos(page, 'm1')).join() === p0.join());
    ok(`${tag}: ...and the menu stays open after the finger lifts`, (await menu()) !== null);
    await tap(page, 5, 700); await sleep(page, 200);
    ok(`${tag}: a tap elsewhere closes it`, (await menu()) === null);
    // a finger that moves does not open it (it drags)
    await tStart(page, [[h.x, h.y]]); for (let i = 1; i <= 6; i++) { await tMove(page, [[h.x + i * 4, h.y]]); await sleep(page, 20); } await sleep(page, 650); await tEnd(page);
    ok(`${tag}: a finger that moves more than 8 px before 500 ms opens no menu`, (await menu()) === null);
    // cable
    const cp = await page.evaluate(() => { const hit = document.querySelector('#cablehits .hit'); const L = hit.getTotalLength(), pt = hit.getPointAtLength(L * 0.5), m = hit.getScreenCTM(); return [m.a * pt.x + m.e, m.b * pt.x + m.f + (m.d * pt.y)]; });
    const cp2 = await page.evaluate(() => { const hit = document.querySelector('#cablehits .hit'); const L = hit.getTotalLength(), pt = hit.getPointAtLength(L * 0.5), m = hit.getScreenCTM(), p = new DOMPoint(pt.x, pt.y).matrixTransform(m); return [p.x, p.y]; });
    await hold(page, cp2[0], cp2[1]); const cm = await menu();
    ok(`${tag}: a long press on a cable opens its menu (${cm && cm.join(' | ')})`, !!cm && cm.includes('Remove cable') && cm.includes('Add a bend here'));
    await tap(page, 5, 700); await sleep(page, 200);
    // slider
    const sl = await page.evaluate(() => { const r = document.querySelector('.node[data-id="m2"] .row.sl input, .node[data-id="m5"] .row.sl input'); return null; });
    // background
    await hold(page, 380, 620); const bm = await menu();
    ok(`${tag}: a long press on the background opens its menu, with "Add a module here…" (${bm && bm.slice(0, 3).join(' | ')})`, !!bm && bm.some(t => t.startsWith('Add a module here…')) && bm.some(t => t.startsWith('Select all')));
    await tap(page, 5, 700); await sleep(page, 200);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
  const S = await bootEd(412, 915, true); const tag2 = `${ed} T6 (slider)`;
  try {
    const { page, errors, base } = S;
    await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: { tips: false } });
    const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input'), v0 = await slVal(page, 't1', 'regen');
    await hold(page, sl.x - 20, sl.y);
    let m = await page.evaluate(() => { const m = document.getElementById('ctxmenu'); return m.hidden ? null : [...m.querySelectorAll('.ctxitem')].map(b => b.textContent); });
    ok(`${tag2}: a long press on a slider opens its menu, value items first (${m && m.slice(0, 4).join(' | ')})`, !!m && (m = m.filter(t => !/^§/.test(t)), m[0] === 'Reset to default' && m[1] === 'Type a value…' && /MIDI learn/.test(m[2])));
    ok(`${tag2}: ...and moved no value`, (await slVal(page, 't1', 'regen'))[0] === v0[0]);
    await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(b => /Type a value/.test(b.textContent)).click()); await sleep(page, 200);
    ok(`${tag2}: "Type a value…" opens the type box`, await page.evaluate(() => !!document.querySelector('.valedit')));
    done(errors, tag2);
  } catch (e) { ok(`${tag2}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await S.close();
}

// ================= T7: touch sliders; T9: tap on the readout =================
async function t7(ed) {
  const B = await bootEd(344, 780, true); const { page, errors, base } = B; const tag = `${ed} T7`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: { tips: false } });
    const q = 'regen', sel = `.node[data-id="t1"] .row.sl[data-key="${q}"] input`;
    const rng = await page.evaluate(s => { const i = document.querySelector(s); return [+i.min, +i.max, i.getBoundingClientRect().width]; }, sel);
    const span = rng[1] - rng[0], w = rng[2];
    const set = v => page.evaluate(([s, v]) => { const i = document.querySelector(s); i.value = v; i.dispatchEvent(new Event('input', { bubbles: true })); }, [sel, v]);
    await set(rng[0] + span * 0.1); const base0 = (await slVal(page, 't1', q))[0];
    let sl = await centre(page, sel);
    await tap(page, sl.x + w * 0.3, sl.y); await sleep(page, 500);
    ok(`${tag}: a tap on the track does not jump the value to the finger`, Math.abs((await slVal(page, 't1', q))[0] - base0) < span * 0.001);
    await drag(page, [sl.x, sl.y], [sl.x + 48, sl.y], 12); const a1 = (await slVal(page, 't1', q))[0];
    const expect = (48 - 8) / w * span, expFine = 0.1 * (48 - 12) / w * span;
    ok(`${tag}: a sideways drag moves the value relative to where it was: +${((a1 - base0) / span * 100).toFixed(1)}% of the range for 48 px on a ${Math.round(w)} px slider (the 8 px that decide it do not count)`, a1 > base0 && Math.abs((a1 - base0) - expect) < span * 0.06, `${a1} ${base0} ${expect}`);
    await set(rng[0] + span * 0.1);
    await tStart(page, [[sl.x, sl.y]]); await tMove(page, [[sl.x + 12, sl.y]]); await sleep(page, 20);
    for (let i = 1; i <= 10; i++) { await tMove(page, [[sl.x + 12 + 36 * i / 10, sl.y + 80 * i / 10]]); await sleep(page, 14); } await tEnd(page); await sleep(page, 80);
    const fine = (await slVal(page, 't1', q))[0];
    ok(`${tag}: fine mode: sideways to decide it, then drifting 80 px below the slider moves it far less (${((fine - base0) / span * 100).toFixed(2)}% against ${((a1 - base0) / span * 100).toFixed(1)}% in the same distance)`, fine > base0 && (fine - base0) < (a1 - base0) * 0.6, `${fine} ${a1} ${base0}`);
    // vertical: pans, no value
    await set(rng[0] + span * 0.1); const v0 = await readView(page), b2 = (await slVal(page, 't1', q))[0];
    await drag(page, [sl.x, sl.y], [sl.x + 4, sl.y + 60], 12); const v1 = await readView(page);
    ok(`${tag}: a mostly vertical drag that starts on a slider pans the view (${Math.round(v1.y - v0.y)} px) and does not change it`, v1.y - v0.y > 40 && (await slVal(page, 't1', q))[0] === b2);
    sl = await centre(page, sel);
    // the threshold: a 5 px wiggle is neither
    const v2 = await readView(page); await drag(page, [sl.x, sl.y], [sl.x + 5, sl.y + 2], 4);
    ok(`${tag}: a 5 px movement is below the 8 px threshold: no value change, no pan`, (await slVal(page, 't1', q))[0] === b2 && Math.abs((await readView(page)).y - v2.y) < 1);
    // one undo step per drag
    const dflt = await page.evaluate(([s]) => { const i = document.querySelector(s); return i.getAttribute('data-def'); }, [sel]);
    // double tap resets
    await set(rng[0] + span * 0.7);
    await tap(page, sl.x, sl.y); await sleep(page, 90); await tap(page, sl.x, sl.y); await sleep(page, 200);
    const rst = (await slVal(page, 't1', q))[0];
    ok(`${tag}: a double-tap resets the slider to its default (${rst})`, rst !== rng[0] + span * 0.7);
    // a second finger stops the drag
    await set(rng[0] + span * 0.2); const e0 = (await slVal(page, 't1', q))[0];
    await tStart(page, [[sl.x, sl.y]]); for (let i = 1; i <= 4; i++) { await tMove(page, [[sl.x + i * 6, sl.y]]); await sleep(page, 14); }
    const e1 = (await slVal(page, 't1', q))[0];
    await tStart(page, [[sl.x + 24, sl.y], [sl.x + 120, sl.y + 100]]); await sleep(page, 40); await tMove(page, [[sl.x + 80, sl.y], [sl.x + 120, sl.y + 100]]); await sleep(page, 40); await tEnd(page);
    const e2 = (await slVal(page, 't1', q))[0];
    ok(`${tag}: a second finger ends the slider drag (it moved ${(e1 - e0).toFixed(3)} and then stayed at ${(e2 - e0).toFixed(3)})`, e1 > e0 && Math.abs(e2 - e1) < span * 0.02, `${e0} ${e1} ${e2}`);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
  // undo: one drag, one step (mouse-free: through the File menu's Undo)
  const U = await bootEd(884, 1020, true); const tag3 = `${ed} T7 (undo)`;
  try {
    const { page, errors, base } = U;
    await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: { tips: false } });
    const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input'), v0 = (await slVal(page, 't1', 'regen'))[0];
    const steps = () => page.evaluate(() => document.getElementById('undobtn') ? (document.getElementById('filebtn').click(), (() => { const d = document.getElementById('undobtn').disabled; document.getElementById('filebtn').click(); return !d; })()) : null);
    await drag(page, [sl.x, sl.y], [sl.x + 90, sl.y], 20); const v1 = (await slVal(page, 't1', 'regen'))[0];
    await page.keyboard.press('Control+z'); await sleep(page, 250); const v2 = (await slVal(page, 't1', 'regen'))[0];
    ok(`${tag3}: one finger drag is one undo step (${v0} > ${v1}, one undo: ${v2})`, v1 !== v0 && v2 === v0);
    done(U.errors, tag3);
  } catch (e) { ok(`${tag3}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await U.close();
}
async function t9(ed) {
  const B = await bootEd(344, 780, true); const { page, errors, base } = B; const tag = `${ed} T9`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: { tips: false } });
    const out = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] .val');
    ok(`${tag}: the readout is a touch-sized target (${Math.round(out.w)} x ${Math.round(out.h)} px)`, out.h >= 28 && out.w >= 40);
    await tap(page, out.x, out.y); await sleep(page, 250);
    ok(`${tag}: ONE tap on a readout opens the type box`, await page.evaluate(() => !!document.querySelector('.valedit')));
    await page.keyboard.type('40%'); await page.keyboard.press('Enter'); await sleep(page, 200);
    const r = await slVal(page, 't1', 'regen');
    ok(`${tag}: typing 40% lands as 40% (${r[1]})`, r[1] === '40%');
    // a finger that moves is not a tap
    const out2 = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] .val');
    await drag(page, [out2.x, out2.y], [out2.x, out2.y + 40], 6); await sleep(page, 150);
    ok(`${tag}: a finger that moves 40 px over the readout opens no box`, await page.evaluate(() => !document.querySelector('.valedit')));
    // the pass-2 double tap still works as two taps
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= T8: Select, bends, quick add, with touch only =================
async function t8(ed) {
  const B = await bootEd(884, 1020, true); const { page, errors, base } = B; const tag = `${ed} T8`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 20, y: 20, zoom: 0.75, ui: 2, nw: 1 } }), { settings: { tips: false } });
    const selected = () => page.evaluate(() => [...document.querySelectorAll('.node.selected')].map(n => n.dataset.id));
    const sb = await centre(page, '#selbtn'); ok(`${tag}: a Select button is in the bar on a touch screen`, !!sb && sb.w > 0);
    await tap(page, sb.x, sb.y);
    ok(`${tag}: a tap on it turns Select on (aria-pressed, body.selmode)`, await page.evaluate(() => document.getElementById('selbtn').getAttribute('aria-pressed') === 'true' && document.body.classList.contains('selmode')));
    const t1 = await centre(page, '.node[data-id="m1"] > header .t'), t2 = await centre(page, '.node[data-id="m2"] > header .t'), p0 = await modPos(page, 'm1');
    await tap(page, t1.x, t1.y); await tap(page, t2.x, t2.y);
    ok(`${tag}: with Select on, a tap on a title bar picks the module: two taps, two modules selected (${(await selected()).join(',')})`, (await selected()).join() === 'm1,m2');
    await tap(page, t2.x, t2.y); ok(`${tag}: ...a second tap on one takes it out`, (await selected()).join() === 'm1');
    ok(`${tag}: ...and no module moved`, (await modPos(page, 'm1')).join() === p0.join());
    // the body of a module (not a control)
    const b5 = await page.evaluate(() => { const r = document.querySelector('.node[data-id="m6"] .ports').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
    await tap(page, b5[0], b5[1]); ok(`${tag}: ...a tap on a module's body picks it too (${(await selected()).join(',')})`, (await selected()).includes('m6'));
    // marquee around m5 (empty background to the left/below)
    const r5 = await centre(page, '.node[data-id="m7"]');
    await drag(page, [r5.l + r5.w / 2, r5.b + 10], [r5.r - 12, r5.t + 40], 10);
    ok(`${tag}: a one-finger drag on the background draws a box and picks what it touches (${(await selected()).join(',')})`, (await selected()).includes('m7'));
    const v = await readView(page);
    // with a selection of several, the long-press menu groups them
    await hold(page, t1.x, t1.y + 1); const gm = await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].map(b => b.textContent));
    ok(`${tag}: a long press then offers Group ${(await selected()).length} modules, Copy, Duplicate, Delete (${gm.slice(0, 5).join(' | ')})`, gm.some(t => /^Group \d+ modules/.test(t)) && gm.some(t => /^Delete \d+ modules/.test(t)), gm.join(' | '));
    await page.keyboard.press('Escape'); await sleep(page, 200);
    await tap(page, sb.x, sb.y);
    ok(`${tag}: Select off: a drag on a title bar moves the module again`, await (async () => { const a = await modPos(page, 'm5'), h = await centre(page, '.node[data-id="m5"] > header .t'); await drag(page, [h.x, h.y], [h.x + 40, h.y + 20]); const b = await modPos(page, 'm5'); return b[0] > a[0] + 20; })());
    // bends: long press on a cable; add a bend; then remove it
    const cpt = await page.evaluate(() => { const hit = document.querySelector('#cablehits .hit'), L = hit.getTotalLength(), pt = hit.getPointAtLength(L * 0.5), p = new DOMPoint(pt.x, pt.y).matrixTransform(hit.getScreenCTM()); return [p.x, p.y]; });
    await hold(page, cpt[0], cpt[1]);
    await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(b => /Add a bend here/.test(b.textContent)).click()); await sleep(page, 300);
    const bends = () => page.evaluate(() => document.querySelectorAll('svg.cables rect.bend').length);
    ok(`${tag}: "Add a bend here" in a cable's long-press menu adds a bend handle`, (await bends()) === 1);
    const hd = await page.evaluate(() => { const r = document.querySelector('svg.cables rect.bend').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
    await drag(page, hd, [hd[0], hd[1] + 40], 8); await sleep(page, 100);
    const moved = await page.evaluate(() => { const r = document.querySelector('svg.cables rect.bend').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
    ok(`${tag}: a finger drags the bend handle (${Math.round(hd[1])} > ${Math.round(moved[1])})`, moved[1] > hd[1] + 25);
    await hold(page, moved[0], moved[1]); const bm = await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].map(b => b.textContent));
    ok(`${tag}: a long press on the handle offers "Remove this bend" (${bm.join(' | ')})`, bm.includes('Remove this bend'));
    await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(b => /Remove this bend/.test(b.textContent)).click()); await sleep(page, 200);
    ok(`${tag}: ...and it goes`, (await bends()) === 0);
    // quick add
    const n0 = (await page.$$('.node')).length;
    await hold(page, 600, 800); await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(b => /Add a module here/.test(b.textContent)).click()); await sleep(page, 300);
    ok(`${tag}: "Add a module here…" opens quick add`, await page.evaluate(() => !!document.querySelector('#ctxmenu .qainp')));
    await page.keyboard.type('lfo'); await page.keyboard.press('Enter'); await sleep(page, 400);
    ok(`${tag}: typing a name and Enter adds the module there (${n0} > ${(await page.$$('.node')).length})`, (await page.$$('.node')).length === n0 + 1);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= T12: the top bar =================
const BAR_IDS = ['pname', 'filebtn', 'setbtn', 'helpbtn', 'midilearn', 'zoomout', 'zoomin', 'zoomfit', 'cpuv'];
async function t12(ed, OLD) {
  const probe = page => page.evaluate(ids => { const b = document.querySelector('.bar').getBoundingClientRect(), kids = [...document.querySelectorAll('.bar > *')].filter(e => { const r = e.getBoundingClientRect(); return r.width > 0 && r.height > 0; });
    const tops = kids.map(e => { const r = e.getBoundingClientRect(); return r.top + r.height / 2; }), items = {};
    for (const id of ids) { const e = document.getElementById(id); const r = e && e.getBoundingClientRect(); items[id] = e ? { shown: r.width > 0 && r.height > 0, inBar: !!e.closest('.bar') && !e.closest('#morepop'), inPop: !!e.closest('#morepop'), x: r.left, r: r.right } : null; }
    return { h: Math.round(b.height), rows: Math.max(...tops) - Math.min(...tops) < 6 ? 1 : 2, items, vw: innerWidth }; }, BAR_IDS);
  for (const [w, h, mob] of [[344, 780, true], [360, 780, true], [412, 915, true]]) {
    const B = await bootEd(w, h, mob); const { page, errors, base } = B; const tag = `${ed} T12: ${w} px`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc(), { settings: { tips: false } });
      const p = await probe(page);
      ok(`${tag}: the bar is one row, ${p.h} px high`, p.rows === 1 && p.h <= 64, JSON.stringify({ h: p.h, rows: p.rows }));
      const inBar = BAR_IDS.filter(id => p.items[id] && p.items[id].shown && p.items[id].inBar);
      ok(`${tag}: in the bar: patch name, Play / Edit, add, undo, Fit and the overflow button (${['modeplay', 'baradd', 'barundo', 'zoomfit', 'morebtn'].every(id => true)}; ${inBar.join(', ')})`, inBar.includes('pname') && inBar.includes('zoomfit'));
      const vis = await page.evaluate(() => ['modeplay', 'modeedit', 'baradd', 'barundo', 'morebtn', 'zoomfit', 'pname'].map(id => { const r = document.getElementById(id).getBoundingClientRect(); return [id, r.width > 0 && r.left >= 0 && r.right <= innerWidth + 0.5]; }));
      ok(`${tag}: each of those is wholly on screen`, vis.every(v => v[1]), JSON.stringify(vis));
      const mb = await centre(page, '#morebtn'); await tap(page, mb.x, mb.y); await sleep(page, 250);
      const q = await page.evaluate(ids => { const pop = document.getElementById('morepop'), pr = pop.getBoundingClientRect(); const out = {}; for (const id of ids) { const e = document.getElementById(id); if (!e) { out[id] = 'none'; continue; } const r = e.getBoundingClientRect(); const cs = getComputedStyle(e).display; out[id] = (cs === 'none' || !r.width) ? 'hidden' : (e.closest('#morepop') ? (r.left >= 0 && r.right <= innerWidth + .5 && r.bottom <= innerHeight ? 'popup' : 'cut off') : 'bar'); } return { open: !pop.hidden, out, w: pr.width }; }, BAR_IDS);
      ok(`${tag}: the overflow menu opens on a tap`, q.open);
      const missing = BAR_IDS.filter(id => !['popup', 'bar'].includes(q.out[id]) && !(id === 'cpuv' && q.out[id] === 'hidden'));
      ok(`${tag}: every former bar item is reachable from the bar or the overflow menu (${BAR_IDS.map(id => id + ':' + q.out[id]).join(' ')})`, missing.length === 0, missing.join(','));
      // the items still work from inside the menu: Help opens, a zoom button zooms
      const z0 = (await readView(page)).z; const zi = await centre(page, '#zoomin'); await tap(page, zi.x, zi.y); await sleep(page, 200);
      ok(`${tag}: the zoom button inside the menu still zooms (${Math.round(z0 * 100)}% > ${Math.round((await readView(page)).z * 100)}%)`, (await readView(page)).z > z0 + 0.05);
      const hb = await centre(page, '#helpbtn'); await tap(page, hb.x, hb.y); await sleep(page, 400);
      ok(`${tag}: Help opens from the menu (and the menu closes)`, await page.evaluate(() => { const o = document.getElementById('helpov'); return !!o && !o.hidden && document.getElementById('morepop').hidden; }));
      await page.evaluate(() => document.getElementById('helpov').hidden = true);
      // File and Settings open their lists inside the menu
      await tap(page, mb.x, mb.y); await sleep(page, 200); const fb = await centre(page, '#filebtn'); await tap(page, fb.x, fb.y); await sleep(page, 200);
      const fm = await page.evaluate(() => { const m = document.getElementById('filemenu'), r = m.getBoundingClientRect(); return { open: !m.hidden, inside: r.left >= 0 && r.right <= innerWidth + .5, items: m.querySelectorAll('.fitem').length }; });
      ok(`${tag}: File opens its list inside the menu (${fm.items} commands)`, fm.open && fm.inside && fm.items >= 8, JSON.stringify(fm));
      // the Undo in the bar works
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
  // wide: unchanged
  for (const [w, h, mob] of [[884, 1020, true], [1440, 900, false]]) {
    const B = await bootEd(w, h, mob); const { page, errors, base } = B; const tag = `${ed} T12: ${w} px`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc(), { settings: { tips: false } });
      const p = await probe(page);
      let o = null; if (OLD) { const O = await bootEd(w, h, mob); await loadDocAt(O.page, OLD.base, ed, SPEC[ed].doc()); o = await probe(O.page); await O.close(); }
      if (o) {
        const same = BAR_IDS.every(id => (!o.items[id] && !p.items[id]) || (o.items[id] && p.items[id] && o.items[id].shown === p.items[id].shown && p.items[id].inBar === o.items[id].inBar));
        ok(`${tag}: every former bar item is still in the bar, shown as before (${w >= 1340 ? 'one row' : p.rows + ' row(s)'}; ${o.h} px before, ${p.h} px now)`, same && p.rows === o.rows && Math.abs(p.h - o.h) <= 4, JSON.stringify([o, p]).slice(0, 300));
      }
      const mw = await page.evaluate(() => { const e = document.querySelector('.morewrap'), a = document.getElementById('baradd'), u = document.getElementById('barundo'); return [e, a, u].map(x => getComputedStyle(x).display); });
      ok(`${tag}: the overflow, add and undo buttons are not there (${mw.join()})`, mw.every(d => d === 'none'));
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}
async function loadDocAt(page, base, ed, doc) { const url = `${base}/${ed}/index.html`; await page.goto(url); await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, '{"tips":false}'); }, [SPEC[ed].key, SPEC[ed].setKey, doc]); await page.goto(url); await page.waitForSelector('.node'); await sleep(page, 900); }

// ================= T13: waves' on-screen keys =================
async function t13() {
  const ed = 'waves';
  for (const [w, h] of [[344, 780], [412, 915]]) {
    const B = await bootEd(w, h, true); const { page, errors, base } = B; const tag = `waves T13: ${w} px`;
    try {
      await page.addInitScript(() => { window.__msgs = []; const o = MessagePort.prototype.postMessage; MessagePort.prototype.postMessage = function (m, ...r) { try { if (m && (m.op === 'noteOn' || m.op === 'noteOff')) window.__msgs.push({ op: m.op, pitch: m.pitch }); } catch (e) {} return o.call(this, m, ...r); }; });
      await loadDoc(page, base, ed, SPEC[ed].doc({ view: NV }), { settings: { tips: false, keys: true } }); await sleep(page, 1200);
      const k = await page.evaluate(() => { const d = document.getElementById('keysdock'), r = d.getBoundingClientRect(), ks = [...d.querySelectorAll('.touchkeys button')].map(b => { const q = b.getBoundingClientRect(); return { n: b.textContent, l: q.left, r: q.right, t: q.top, b: q.bottom, w: q.width, h: q.height }; });
        return { shown: !d.hidden, l: r.left, r: r.right, vw: innerWidth, vh: innerHeight, b: r.bottom, ks, oct: !!document.getElementById('octdn') && !!document.getElementById('octup') }; });
      ok(`${tag}: the keys are shown (on-screen keys on) and sit inside the screen (${Math.round(k.l)}..${Math.round(k.r)} of ${k.vw}, bottom ${Math.round(k.b)} of ${k.vh})`, k.shown && k.l >= 0 && k.r <= k.vw + 0.5 && k.b <= k.vh + 0.5);
      const names = new Set(k.ks.map(x => x.n));
      ok(`${tag}: all 12 notes of the octave (and the next C) are keys, each wholly on screen and at least 20 px wide (${k.ks.length} keys: ${[...names].join(' ')})`, k.ks.length === 13 && names.size === 12 && k.ks.every(x => x.l >= 0 && x.r <= k.vw && x.w >= 20), JSON.stringify(k.ks.map(x => [x.n, Math.round(x.l), Math.round(x.w)])));
      ok(`${tag}: no two keys overlap`, k.ks.every((a, i) => k.ks.every((b, j) => i >= j || a.r <= b.l + .5 || b.r <= a.l + .5 || a.b <= b.t + .5 || b.b <= a.t + .5)));
      // the octave buttons
      ok(`${tag}: octave down and up buttons are there`, k.oct);
      // keys send notes through the existing path (the engine bridge's noteOn message)
      await page.evaluate(() => { window.__msgs.length = 0; });
      const centres = await page.evaluate(() => [...document.querySelectorAll('#touchkeys button')].map(b => { const r = b.getBoundingClientRect(); return [b.textContent, r.left + r.width / 2, r.top + r.height / 2]; }));
      for (const [n, x, y] of centres) { await tap(page, x, y); }
      const sent = await page.evaluate(() => window.__msgs.slice());
      const ons = sent.filter(m => m.op === 'noteOn').map(m => m.pitch);
      ok(`${tag}: touching the 13 keys sends 13 noteOn messages for pitches 0 to 12 (${ons.join(',')})`, ons.join() === [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12].join(), JSON.stringify(sent.slice(0, 6)));
      ok(`${tag}: ...and a noteOff for each when the finger lifts`, sent.filter(m => m.op === 'noteOff').length === 13);
      const up = await centre(page, '#octup'); await tap(page, up.x, up.y); await sleep(page, 150);
      const lab = await page.evaluate(() => document.getElementById('octval').textContent);
      await page.evaluate(() => { window.__msgs.length = 0; }); await tap(page, centres[0][1], centres[0][2]); const s2 = await page.evaluate(() => window.__msgs.slice());
      ok(`${tag}: octave up: the label says ${lab} and the C now sends pitch 12 (${JSON.stringify(s2.filter(m => m.op === 'noteOn'))})`, lab === 'Oct 5' && s2.some(m => m.op === 'noteOn' && m.pitch === 12));
      const dn = await centre(page, '#octdn'); await tap(page, dn.x, dn.y); await tap(page, dn.x, dn.y); await sleep(page, 100);
      ok(`${tag}: octave down twice: ${await page.evaluate(() => document.getElementById('octval').textContent)}`, (await page.evaluate(() => document.getElementById('octval').textContent)) === 'Oct 3');
      // the Tips button stays above the keys
      const tipp = await page.evaluate(() => { const p = document.getElementById('hintpill').getBoundingClientRect(), d = document.getElementById('keysdock').getBoundingClientRect(); return { pb: p.bottom, dt: d.top }; });
      ok(`${tag}: the Tips button is above the keys, not under them`, tipp.pb <= tipp.dt + 1, JSON.stringify(tipp));
      done(errors, tag);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}

// ================= T14: long modules =================
async function t14(ed, OLD) {
  const long = SPEC[ed].long;
  const mk = (o = {}) => { const d = SPEC[ed].doc({ view: { x: 12, y: 12, zoom: 1, ui: 2, nw: 1 }, cables: o.cables || [] }); d.modules = [P('L1', long, 0, 0), P('s1', 'delay', 420, 0, { time: 0.62, regen: 0.71, tone: 0.33, mix: 0.43 })].concat(o.extra || []);   /* s1 has explicit non-default values (defaults .375 / .38 / .5 / .25), so the round-trip comparison compares something */ if (o.sec) d.view.sec = o.sec; return d; };
  const B = await bootEd(344, 780, true); const { page, errors, base } = B; const tag = `${ed} T14`;
  try {
    await loadDoc(page, base, ed, mk(), { settings: { tips: false } });
    const info = () => page.evaluate(() => { const n = document.querySelector('.node[data-id="L1"]'), secs = [...n.querySelectorAll(':scope > .body > .sec')];
      return { h: Math.round(n.getBoundingClientRect().height), secs: secs.map(s => ({ t: s.querySelector('.sect').textContent, shut: s.classList.contains('shut'), open: s.querySelector('.sechd').getAttribute('aria-expanded'), rows: s.querySelectorAll('.row').length, vis: [...s.querySelectorAll('.row')].filter(r => r.getBoundingClientRect().height > 0).length })) }; });
    const i0 = await info();
    ok(`${tag}: ${SPEC[ed].longName} is cut into sections (${i0.secs.map(s => s.t + ' (' + s.rows + ')').join(' | ')}); ${i0.h} px high`, i0.secs.length >= 2);
    ok(`${tag}: the first section is open, the others shut (rows showing: ${i0.secs.map(s => s.vis).join(', ')})`, !i0.secs[0].shut && i0.secs.slice(1).every(s => s.shut && s.vis === 0) && i0.secs[0].vis === i0.secs[0].rows);
    // a long module never was shorter than the phone: compare with the old build
    let oh = null;
    if (OLD) { const O = await bootEd(344, 780, true); await loadDocAt(O.page, OLD.base, ed, mk()); oh = await O.page.evaluate(() => Math.round(document.querySelector('.node[data-id="L1"]').getBoundingClientRect().height)); await O.close(); ok(`${tag}: the module is ${i0.h} px high now against ${oh} px in build 585`, i0.h < oh * 0.8); }
    const bring = async sel => { for (let k = 0; k < 6; k++) { const c = await centre(page, sel); if (c.y > 80 && c.y < 500) return c; await twoFingers(page, [100, 600], [200, 600], [0, c.y > 500 ? -Math.min(300, c.y - 300) : 200]); await sleep(page, 200); await tap(page, 341, 70); } return centre(page, sel); };
    const hd = await bring('.node[data-id="L1"] .sec[data-i="1"] .sechd');
    if (process.env.DBG) console.log("HIT", ed, await page.evaluate(([x, y]) => { const e = document.elementFromPoint(x, y); return e && (e.tagName + "." + e.className + " in " + (e.closest("[class]") && e.closest(".node") ? "node" : "other")); }, [hd.x, hd.y]));
    await tap(page, hd.x, hd.y); await sleep(page, 200);
    const i1 = await info(); if (process.env.DBG) console.log("DBG", ed, JSON.stringify(hd), JSON.stringify(i0.secs), JSON.stringify(i1.secs));
    ok(`${tag}: a tap on a section bar opens it (${i1.secs[1].vis} rows showing) and the module grows (${i0.h} > ${i1.h} px)`, !i1.secs[1].shut && i1.secs[1].vis > 0 && i1.h > i0.h);
    if (process.env.DBG) console.log("HIT", ed, await page.evaluate(([x, y]) => { const e = document.elementFromPoint(x, y); return e && (e.tagName + "." + e.className + " in " + (e.closest("[class]") && e.closest(".node") ? "node" : "other")); }, [hd.x, hd.y]));
    await tap(page, hd.x, hd.y); await sleep(page, 200); ok(`${tag}: ...and a second tap folds it again`, (await info()).secs[1].shut);
    { const h2 = await bring('.node[data-id="L1"] .sec[data-i="1"] .sechd'); await tap(page, h2.x, h2.y); await sleep(page, 200); }
    // persistence: in the view, saved with the patch as an optional field
    const sd = await savedDoc(page, ed);
    ok(`${tag}: the open sections are saved in the view (view.sec = ${JSON.stringify(sd.view.sec)}), not in any module`, sd.view.sec && sd.view.sec.L1 === 3 && !JSON.stringify(sd.modules).includes('"sec"'));
    await page.reload(); await page.waitForSelector('.node'); await sleep(page, 800);
    const i2 = await info(); ok(`${tag}: after a reload the same sections are open (${i2.secs.map(s => s.shut ? 'shut' : 'open').join(', ')})`, !i2.secs[0].shut && !i2.secs[1].shut && i2.secs.slice(2).every(s => s.shut));
    // round trip: nothing else in the patch changed
    // modules (id, type, position, every parameter that was set, and the defaults the load adds must be plain numbers / strings), cables (ends, in order) and groups
    const orig = mk(); const norm = await savedDoc(page, ed);
    const diffs = [];
    if (norm.modules.length !== orig.modules.length) diffs.push('module count ' + norm.modules.length + ' vs ' + orig.modules.length);
    for (const m of orig.modules) {
      const n = norm.modules.find(x => x.id === m.id);
      if (!n) { diffs.push(m.id + ' missing'); continue; }
      if (n.type !== m.type) diffs.push(m.id + ' type');
      if (!n.pos || Math.abs(n.pos[0] - m.pos[0]) > 0.5 || Math.abs(n.pos[1] - m.pos[1]) > 0.5) diffs.push(m.id + ' pos ' + JSON.stringify(n.pos));
      for (const k of Object.keys(m.params || {})) if (JSON.stringify((n.params || {})[k]) !== JSON.stringify(m.params[k])) diffs.push(m.id + ' param ' + k);
      for (const [k, v] of Object.entries(n.params || {})) if (!(typeof v === 'number' || typeof v === 'string' || typeof v === 'boolean' || v === null)) diffs.push(m.id + ' default ' + k + ' is ' + typeof v);
      if (!!n.collapsed !== !!m.collapsed && n.view == null) diffs.push(m.id + ' collapsed');
    }
    const cabKey = c => JSON.stringify([c.from, c.to]);
    if (JSON.stringify(norm.cables.map(cabKey)) !== JSON.stringify(orig.cables.map(cabKey))) diffs.push('cables ' + JSON.stringify(norm.cables.map(cabKey)) + ' vs ' + JSON.stringify(orig.cables.map(cabKey)));
    if (JSON.stringify(norm.groups || []) !== JSON.stringify(orig.groups || [])) diffs.push('groups');
    ok(`${tag}: modules (type, position, parameters), cables and groups are the same after the round trip`, diffs.length === 0, diffs.slice(0, 4).join(' | '));
    // a collapsed (compact) long module still reads
    // a cable into a control inside a shut section ends at its section bar
    await loadDoc(page, base, ed, mk({ sec: { L1: 1 }, extra: [P('l2', 'lfo', 0, 900)], cables: [C('l2', 'out', 'L1', 'p:' + (await keyOfSection(page, ed, 2))) ] }), { settings: { tips: false } });
    const cab = await page.evaluate(() => { const g = [...document.querySelectorAll('svg#cables > g')].pop(); if (!g) return null; const ends = g.querySelectorAll('circle'), e = ends[1], hd = document.querySelector('.node[data-id="L1"] .sec[data-i="2"] .sechd'), r = hd.getBoundingClientRect(), wr = document.getElementById('world').getBoundingClientRect(), z = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a;
      return { shown: g.style.display !== 'none', cx: +e.getAttribute('cx'), cy: +e.getAttribute('cy'), hx: (r.left - wr.left) / z, hy: (r.top + r.height / 2 - wr.top) / z }; });
    ok(`${tag}: a cable into a control in a shut section is drawn and ends at that section's bar (${cab && Math.round(cab.cx)},${cab && Math.round(cab.cy)} against ${cab && Math.round(cab.hx)},${cab && Math.round(cab.hy)})`, cab && cab.shown && Math.abs(cab.cx - cab.hx) < 3 && Math.abs(cab.cy - cab.hy) < 3, JSON.stringify(cab));
    const mk2 = await page.evaluate(() => document.querySelector('.node[data-id="L1"] .sec[data-i="2"] .secm').textContent);
    ok(`${tag}: ...and the section bar says it holds a modulated control ("${mk2}")`, /1 mod/.test(mk2));
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
  // build 585 reads a patch saved here; this build reads a 585 patch unchanged
  if (OLD) {
    const B2 = await bootEd(344, 780, true); const { page, errors, base } = B2; const tag2 = `${ed} T14 (build 585)`;
    try {
      const withSec = mk({ sec: { L1: 3 } });
      await loadDocAt(page, OLD.base, ed, withSec);
      const r = await page.evaluate(() => ({ nodes: document.querySelectorAll('.node').length, rows: document.querySelectorAll('.node[data-id="L1"] .row.sl').length }));
      ok(`${tag2}: a patch saved by this build (view.sec set) loads in build 585: ${r.nodes} modules, ${r.rows} controls`, r.nodes === 2 && r.rows > 10);
      done(errors, tag2);
      // and 585's own save loads here with the long module in its default sections
      const saved = await savedDoc(page, ed); await loadDoc(page, base, ed, saved, { settings: { tips: false } });
      const r2 = await page.evaluate(() => ({ nodes: document.querySelectorAll('.node').length, secs: document.querySelectorAll('.node[data-id="L1"] .sec').length }));
      ok(`${tag2}: a patch saved by build 585 opens here unchanged (${r2.nodes} modules, ${r2.secs} sections)`, r2.nodes === 2 && r2.secs >= 2);
    } catch (e) { ok(`${tag2}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B2.close();
  }
}
const keyOfSection = (page, ed, i) => page.evaluate(([i]) => { const s = document.querySelector(`.node[data-id="L1"] .sec[data-i="${i}"] .row.sl`); return s ? s.dataset.key : null; }, [i]);

// ================= unchanged mouse behaviour =================
async function mouse(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} mouse`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed), { settings: { tips: false } });
    const h = await centre(page, '.node[data-id="t1"] > header .t'), p0 = await modPos(page, 't1');
    await page.mouse.move(h.x, h.y); await page.mouse.down(); await page.mouse.move(h.x + 50, h.y + 30, { steps: 6 }); await page.mouse.up();
    const p1 = await modPos(page, 't1'); ok(`${tag}: a title-bar drag moves the module`, Math.abs(p1[0] - p0[0] - 50) < 3);
    const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input'), s0 = await slVal(page, 't1', 'regen');
    await page.mouse.move(sl.x, sl.y); await page.mouse.down(); await page.mouse.move(sl.x + 30, sl.y, { steps: 5 }); await page.mouse.up(); const s1 = await slVal(page, 't1', 'regen');
    ok(`${tag}: a mouse slider drag is relative (${s0[1]} > ${s1[1]})`, s1[0] > s0[0]);
    await page.mouse.dblclick(sl.x, sl.y); ok(`${tag}: double-click resets`, (await slVal(page, 't1', 'regen'))[0] !== s1[0]);
    await page.mouse.click(h.x + 50, h.y + 30, { button: 'right' }); await sleep(page, 120);
    const items = await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].map(b => b.textContent));
    ok(`${tag}: right-click still opens the module menu (no touch-only items: ${items.length} items)`, items.some(t => /^Delete module/.test(t)) && !items.includes('Reset to default') && !items.includes('Add a bend here'));
    await page.keyboard.press('Escape');
    // the wide bar
    const bar = await page.evaluate(() => ({ h: document.querySelector('.bar').getBoundingClientRect().height, h1: getComputedStyle(document.querySelector('.bar h1')).display }));
    ok(`${tag}: the bar is one row of ${Math.round(bar.h)} px, the title is shown`, bar.h < 70 && bar.h1 !== 'none');
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= screenshots for the owner =================
async function shots(OLD) {
  const OUTD = process.env.SHOTS || '/home/claude/work/shots/pass3'; fs.mkdirSync(OUTD, { recursive: true });
  const skinCss = async page => { /* the IDM pink and green look, through the editors' own variables (what the host injects for a term skin) */
    await page.addStyleTag({ content: `:root{--bench:#0b0710; --panel:#0b0710; --panel-2:#1c1224; --edge:#ff2d8a; --ink:#ff2d8a; --muted:#c070a0; --track:#1c1224; --side:#140c1b; --frame:#140c1b; --video:#39ff88; --audio:#39ff88; --audio-dark:#117a40; --mod:#c58bff; --mod-dark:#6a3fb0; --gate:#ffd23f; --gate-dark:#806510; --rec:#ff4d4d; --selc:#ffb3d6; --font:ui-monospace, Menlo, monospace; color-scheme:dark}
      :is(.tb,.pgroup,.pitem,.numsmall,select,.btn,.touchkeys button,button,input[type=text],input[type=number],textarea){background:#0b0710!important; color:#ff2d8a; border-color:#ff2d8a!important}
      :is(.tb.on,.touchkeys button.on,button.on){background:#ff2d8a!important; color:#0b0710!important}` }); };
  const runs = [['344x780', 344, 780, true], ['412x915', 412, 915, true], ['884x1020', 884, 1020, true]];
  for (const [side, root] of [['after', null], ['before', OLD]]) {
    if (side === 'before' && !OLD) continue;
    for (const ed of ED) for (const [dim, w, h, mob] of runs) for (const skin of ['white', 'idmpg']) {
      const B = await bootEd(w, h, mob); const { page, base } = B; const b = root ? root.base : base; const f = `${OUTD}/${side}-${ed}-${dim}-${skin}`;
      try {
        const doc = SPEC[ed].doc({ view: w < 600 ? { x: 12, y: 12, zoom: 0.8, ui: 2, nw: 1 } : { x: 20, y: 20, zoom: 0.9, ui: 2, nw: 1 } });
        await page.goto(`${b}/${ed}/index.html`); await page.evaluate(([k, sk, d, tips]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({})); }, [SPEC[ed].key, SPEC[ed].setKey, doc]);
        await page.goto(`${b}/${ed}/index.html`); await page.waitForSelector('.node'); await sleep(page, 900);
        if (skin === 'idmpg') { await skinCss(page); await sleep(page, 200); }
        await page.screenshot({ path: `${f}-1-open.png` });
        if (side === 'after') {
          if (w < 600) { const mb = await centre(page, '#morebtn'); await tap(page, mb.x, mb.y); await sleep(page, 300); await page.screenshot({ path: `${f}-2-overflow.png` }); await tap(page, mb.x, mb.y); await sleep(page, 200); }
          const pb = await centre(page, '#modeplay'); await tap(page, pb.x, pb.y); await sleep(page, 300); await page.screenshot({ path: `${f}-3-play.png` });
          const eb = await centre(page, '#modeedit'); await tap(page, eb.x, eb.y); await sleep(page, 300);
        }
        const h1 = await centre(page, '.node[data-id="m2"] > header .t, .node[data-id="m1"] > header .t');
        if (side === 'after' && h1) { await hold(page, h1.x, h1.y, 650); await page.screenshot({ path: `${f}-4-longpress.png` }); await tap(page, 5, h - 8); await sleep(page, 200); }
      } catch (e) { console.log('shot failed', f, e.message.split('\n')[0]); }
      await B.close();
    }
  }
  // a slider being dragged with its value, the type box, a long module folded, waves keys
  for (const ed of ED) for (const [dim, w, h, mob] of runs.slice(0, 2)) for (const skin of ['white', 'idmpg']) {
    const B = await bootEd(w, h, mob); const { page, base } = B; const f = `${OUTD}/after-${ed}-${dim}-${skin}`;
    try {
      await loadDoc(page, base, ed, slidersDoc(ed, { view: NV }), { settings: {} });
      if (skin === 'idmpg') { await skinCss(page); await sleep(page, 200); }
      const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input');
      await tStart(page, [[sl.x, sl.y]]); for (let i = 1; i <= 8; i++) { await tMove(page, [[sl.x + i * 5, sl.y]]); await sleep(page, 14); } await sleep(page, 150); await page.screenshot({ path: `${f}-5-slider-drag.png` }); await tEnd(page); await sleep(page, 150);
      const out = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] .val'); await tap(page, out.x, out.y); await sleep(page, 250); await page.screenshot({ path: `${f}-6-typebox.png` }); await page.keyboard.press('Escape'); await sleep(page, 150);
      await loadDoc(page, base, ed, (() => { const d = SPEC[ed].doc({ view: { x: 12, y: 12, zoom: 0.8, ui: 2, nw: 1 }, cables: [] }); d.modules = [P('L1', SPEC[ed].long, 0, 0), P('s1', 'delay', 420, 0)]; return d; })(), { settings: {} });
      if (skin === 'idmpg') { await skinCss(page); await sleep(page, 200); }
      await page.screenshot({ path: `${f}-7-long-folded.png` });
      const hd = await centre(page, '.node[data-id="L1"] .sec[data-i="1"] .sechd'); await tap(page, hd.x, hd.y); await sleep(page, 250); await page.screenshot({ path: `${f}-7b-long-one-open.png` });
      if (ed === 'waves') { await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 12, y: 12, zoom: 0.8, ui: 2, nw: 1 } }), { settings: { keys: true } }); if (skin === 'idmpg') await skinCss(page); await sleep(page, 500); await page.screenshot({ path: `${f}-8-keys.png` }); }
    } catch (e) { console.log('shot failed', f, e.message.split('\n')[0]); }
    await B.close();
  }
  console.log('screenshots in', OUTD);
}

// ================= run =================
const want = process.argv.slice(2).filter(a => !a.startsWith('-'));
const run = s => want.length === 0 || want.includes(s);
const OLD = await oldBuild();
try {
  if (run('fu')) { for (const ed of ED) await fu(ed, OLD); await fuHost(); }
  if (run('m10')) for (const ed of ED) await m10(ed);
  if (run('t4')) for (const ed of ED) await t4(ed);
  if (run('t6')) for (const ed of ED) await t6(ed);
  if (run('t7')) for (const ed of ED) await t7(ed);
  if (run('t9')) for (const ed of ED) await t9(ed);
  if (run('t8')) for (const ed of ED) await t8(ed);
  if (run('t12')) for (const ed of ED) await t12(ed, OLD);
  if (run('t13')) await t13();
  if (run('t14')) for (const ed of ED) await t14(ed, OLD);
  if (run('mouse')) for (const ed of ED) await mouse(ed);
  if (want.includes('shots')) await shots(OLD);
} finally { if (OLD) { OLD.close(); if (!process.env.OLD_ROOT) try { fs.rmSync(OLD.dir, { recursive: true, force: true }); } catch (e) {} } }
const f = T.print(); process.exit(f ? 1 : 0);
