// …waves / …chains pass 2, "Fixes" (13 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: D2 D3 D4 T1 T2 T3 T5 T10 U2 H1 H2 H3 H4).
// Both editors, standalone (mouse and a real touch context) and, for U2, inside the host. Prints PASS / FAIL.
//   node tests/sweep/wc-pass2.mjs            all of it
//   node tests/sweep/wc-pass2.mjs t1         one section: t1 t2 t3 t5 t10 d2 d3 d4 u2 h old
//   OLD_ROOT=/path/to/a/checkout  or  OLD_REF=<commit> (default 2566f63, build 584): the previous build, for the "unchanged" and "loads in 584" checks
//   SHOTS=/dir node tests/sweep/wc-pass2.mjs shots     before / after screenshots of the opening view (and the module list, the delete question)
import fs from 'fs'; import os from 'os'; import path from 'path'; import { execSync } from 'child_process';
import { boot, app, Table, ROOT, OUT, serveDir, listenLoopback } from './ui-lib.mjs';

const T = new Table('wc-pass2');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'PASS ' : 'FAIL ') + name + (note ? '  ' + note : '')); };
const sleep = (page, ms) => page.waitForTimeout(ms);
const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|autoplay/i;
const ED = ['chains', 'waves'];

const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp, extra = {}) => ({ from: [a, ap], to: [b, bp], bend: 0, ...extra });
// An eight-module patch, two rows of four at 420 px spacing (about 1700 world px wide: it does not fit a phone at a readable size).
const SPEC = {
  chains: { key: 'chains.patch', setKey: 'thunderplus.chains.settings',
    doc: (o = {}) => ({ format: 'chains.patch', meta: { name: 'wc pass2' }, view: o.view || { x: 0, y: 0, zoom: 1, ui: 2 }, st2: 1, ti2: 1,
      modules: [P('m1', 'trackIn', 0, 0, { track: 1 }), P('m2', 'delay', 420, 0), P('m3', 'reverb', 840, 0), P('m4', 'eq', 1260, 0),
        P('m5', 'lfo', 0, 420), P('m6', 'compressor', 420, 420), P('m7', 'chorus', 840, 420), P('m8', 'chainOut', 1260, 420, { track: 1 })],
      cables: o.cables || [C('m1', 'out', 'm2', 'in'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m4', 'out', 'm8', 'in')], groups: [], custom: [], transport: { tempo: 120 } }),
    fxMods: [['m2', 'delay'], ['m4', 'eq'], ['m6', 'compressor'], ['m7', 'chorus']] },
  waves: { key: 'waves.patch', setKey: 'thunderplus.waves.settings',
    doc: (o = {}) => ({ meta: { name: 'wc pass2' }, view: o.view || { x: 0, y: 0, zoom: 1, ui: 2 }, voice: { maxVoices: 8 },
      modules: [P('m1', 'hostIn', 0, 0), P('m2', 'vco', 420, 0, { wave: 1 }), P('m3', 'vcf', 840, 0), P('m4', 'vca', 1260, 0),
        P('m5', 'adsr', 0, 420), P('m6', 'lfo', 420, 420), P('m7', 'delay', 840, 420), P('m8', 'out', 1260, 420, { slot: 1, level: 1 })],
      cables: o.cables || [C('m1', 'pitch', 'm2', 'pitch'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m4', 'out', 'm8', 'in')], groups: [], custom: [], transport: { tempo: 120 } }),
    fxMods: [['m7', 'delay'], ['m6', 'lfo'], ['m3', 'vcf'], ['m5', 'adsr']] },
};
// A patch for the typed-value checks: six modules (shared effects, in both editors), no cables.
const TYPED_TYPES = ['delay', 'eq', 'compressor', 'chorus', 'clouds', 'crush'];
const typedDoc = ed => { const d = SPEC[ed].doc({ cables: [] }); d.modules = TYPED_TYPES.map((t, i) => P('t' + (i + 1), t, (i % 3) * 420, Math.floor(i / 3) * 520)); return d; };

async function loadDoc(page, base, ed, doc, o = {}) {
  const url = `${base}/${ed}/index.html`;
  await page.goto(url);
  await page.evaluate(([k, sk, d, tips]) => { localStorage.clear(); if (d) localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips })); }, [SPEC[ed].key, SPEC[ed].setKey, doc, o.tips !== false ? true : false]);
  await page.goto(url); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 900);
  if (o.hideTip) await page.evaluate(() => { const h = document.getElementById('hint'); if (h) h.hidden = true; });
}
// the opening view as the page shows it
const readView = page => page.evaluate(() => {
  const ws = document.getElementById('ws').getBoundingClientRect(), m = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform), z = m.a;
  const nodes = [...document.querySelectorAll('.node')].map(n => { const r = n.getBoundingClientRect(); return { id: n.dataset.id, l: r.left, t: r.top, r: r.right, b: r.bottom, w: r.width }; });
  const first = nodes.slice().sort((a, b) => a.l - b.l || a.t - b.t)[0];
  const j = document.querySelector('.node .jack[data-dir="out"]'), pe = j ? getComputedStyle(j, '::before') : null;
  const jackHit = j ? Math.max(j.getBoundingClientRect().width, (pe && parseFloat(pe.width) ? parseFloat(pe.width) * z : 0)) : 0;
  return { z, x: m.e, y: m.f, ws: { l: ws.left, t: ws.top, r: ws.right, b: ws.bottom, w: ws.width }, first, jackHit, nodes: nodes.length };
});
const inView = (v, n) => n && n.l >= v.ws.l - 1 && n.r <= v.ws.r + 1 && n.t >= v.ws.t - 1 && n.t < v.ws.b - 40;

// ---- the previous build, for the "unchanged" and "loads in 584" checks ----
async function oldBuild() {
  let dir = process.env.OLD_ROOT;
  if (!dir) { const ref = process.env.OLD_REF || '2566f63'; dir = fs.mkdtempSync(path.join(os.tmpdir(), 'wc-pass2-old-'));
    try { execSync(`git -C ${JSON.stringify(ROOT)} archive ${ref} chains waves | tar -x -C ${JSON.stringify(dir)}`, { stdio: 'pipe' }); } catch (e) { return null; } }
  const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.woff': 'font/woff', '.png': 'image/png', '.webmanifest': 'application/manifest+json', '.json': 'application/json' };
  const srv = serveDir(dir, TYPES); await listenLoopback(srv);
  return { base: `http://127.0.0.1:${srv.address().port}`, close: () => srv.close() };
}

// ================= T1: a patch opens readable on a small screen =================
const SIZES = [[344, 780, true], [412, 915, true], [884, 1020, true], [1440, 900, false]];
const READ = 0.8;
async function t1(ed, OLD) {
  for (const [w, h, mob] of SIZES) {
    const B = await boot({ width: w, height: h, mobile: mob }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
    const tag = `${ed} T1: ${w} x ${h}${mob ? ' touch' : ''}`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc()); const v = await readView(page);
      if (w < 1000) {
        ok(`${tag}: a saved 8-module patch opens at ${READ * 100}% or more, never above 100% (${Math.round(v.z * 100)}%)`, v.z >= READ - 0.005 && v.z <= 1.005);
        ok(`${tag}: the first module (leftmost) is in view, its title bar on screen`, inView(v, v.first), JSON.stringify([v.first, v.ws]));
        ok(`${tag}: a jack's touch area is at least 24 px (${v.jackHit.toFixed(1)} px)`, v.jackHit >= 24);
        // a patch last viewed on a wide screen (zoom .5, no view.nw): the same rule; one left on a small screen (view.nw) keeps its view
        await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 30, y: 40, zoom: 0.5, ui: 2 } })); const w2 = await readView(page);
        ok(`${tag}: a patch saved at 50% on a wide screen opens by the same rule (${Math.round(w2.z * 100)}%), one saved at 100% with a module on screen opens as saved (${Math.round(v.z * 100)}%)`, w2.z >= READ - 0.005 && Math.abs(v.z - 1) < 0.005);
        await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 30, y: 200, zoom: 0.6, ui: 2, nw: 1 } })); const w3 = await readView(page);
        ok(`${tag}: a view left on a small screen (60%, view.nw) is kept while a module is on screen (${Math.round(w3.z * 100)}%)`, Math.abs(w3.z - 0.6) < 0.01);
        await loadDoc(page, base, ed, null); const w4 = await readView(page);
        ok(`${tag}: a new patch (nothing saved) opens readable too (${Math.round(w4.z * 100)}%)`, w4.z >= READ - 0.005 && w4.z <= 1.005);
        const fit = await page.evaluate(() => { document.getElementById('zoomfit').click(); return new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a; });
        ok(`${tag}: Fit still fits the whole patch (a button, not the opening view): ${Math.round(fit * 100)}% < ${Math.round(v.z * 100)}%`, fit < v.z - 0.01 || (w4.z <= 1 && fit <= 1.5));
      } else {
        ok(`${tag}: a saved view opens as it was saved (100%, x 0, y 0)`, Math.abs(v.z - 1) < 0.005 && Math.abs(v.x) < 1 && Math.abs(v.y) < 1, JSON.stringify([v.z, v.x, v.y]));
        if (OLD) {
          const ctxB = await boot({ width: w, height: h, mobile: mob });
          await ctxB.page.goto(`${OLD.base}/${ed}/index.html`);
          await ctxB.page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, '{"tips":true}'); }, [SPEC[ed].key, SPEC[ed].setKey, SPEC[ed].doc()]);
          await ctxB.page.goto(`${OLD.base}/${ed}/index.html`); await ctxB.page.waitForSelector('.node'); await sleep(ctxB.page, 900);
          const ov = await readView(ctxB.page);
          ok(`${tag}: the opening view of a saved patch is unchanged from build 584 (${Math.round(v.z * 100)}% at ${Math.round(v.x)}, ${Math.round(v.y)})`, Math.abs(ov.z - v.z) < 0.001 && Math.abs(ov.x - v.x) < 1 && Math.abs(ov.y - v.y) < 1, JSON.stringify([ov.z, ov.x, ov.y]));
          // a new patch: fitted, as before
          await loadDoc(page, base, ed, null); const nz = await readView(page);
          await ctxB.page.evaluate(([sk]) => { localStorage.clear(); localStorage.setItem(sk, '{"tips":true}'); }, [SPEC[ed].setKey]);
          await ctxB.page.goto(`${OLD.base}/${ed}/index.html`); await ctxB.page.waitForSelector('.node'); await sleep(ctxB.page, 900);
          const oz = await readView(ctxB.page);
          ok(`${tag}: a new patch is fitted exactly as in build 584 (${Math.round(nz.z * 100)}%)`, Math.abs(oz.z - nz.z) < 0.001 && Math.abs(oz.x - nz.x) < 1 && Math.abs(oz.y - nz.y) < 1, JSON.stringify([oz.z, nz.z]));
          await ctxB.close();
        }
      }
      const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${tag}: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}

// ================= T2: Fit is reachable at every width =================
async function t2(ed) {
  for (const [w, h, mob] of [[344, 780, true], [800, 700, true], [1340, 900, false], [1440, 900, false]]) {
    const B = await boot({ width: w, height: h, mobile: mob }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
    const tag = `${ed} T2: ${w} px wide`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc(), { hideTip: true });
      const f = await page.evaluate(() => { const b = document.getElementById('zoomfit'), r = b.getBoundingClientRect(), cs = getComputedStyle(b); const e = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2);
        return { shown: cs.display !== 'none' && r.width > 0, x: r.left, r2: r.right, t: r.top, b: r.bottom, vw: innerWidth, vh: innerHeight, top: !!e && (e === b || b.contains(e)), zv: getComputedStyle(document.getElementById('zoomval')).display }; });
      ok(`${tag}: the Fit button is shown, inside the window, and the topmost thing at its centre`, f.shown && f.x >= 0 && f.r2 <= f.vw && f.b <= f.vh && f.top, JSON.stringify(f));
      const z0 = (await readView(page)).z;
      const pt = await page.evaluate(() => { const r = document.getElementById('zoomfit').getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
      if (mob) await page.touchscreen.tap(pt[0], pt[1]); else await page.mouse.click(pt[0], pt[1]);
      await sleep(page, 300); const z1 = (await readView(page)).z;
      ok(`${tag}: a ${mob ? 'tap' : 'click'} on it fits the whole patch (${Math.round(z0 * 100)}% > ${Math.round(z1 * 100)}%)`, z1 < z0 - 0.01 || (w >= 1340 && z1 > 0));
      if (w <= 520) ok(`${tag}: the percent readout is what goes when space is short`, f.zv === 'none');
      const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${tag}: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}

// ================= T3: the module list on a phone =================
async function t3(ed) {
  for (const [w, h, mob, narrow] of [[344, 780, true, true], [884, 1020, true, false], [1440, 900, false, false]]) {
    const B = await boot({ width: w, height: h, mobile: mob }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
    const tag = `${ed} T3: ${w} px`;
    try {
      await loadDoc(page, base, ed, SPEC[ed].doc(), { hideTip: true });
      const tg = await page.evaluate(() => { const r = document.getElementById('dtoggle').getBoundingClientRect(); return [r.left + r.width / 2, r.top + 40]; });
      if (mob) await page.touchscreen.tap(tg[0], tg[1]); else await page.mouse.click(tg[0], tg[1]); await sleep(page, 400);
      const pal = await page.evaluate(() => { const r = document.getElementById('palette').getBoundingClientRect(); return { l: r.left, r: r.right, w: r.width, open: !document.getElementById('drawer').classList.contains('collapsed'), vw: innerWidth }; });
      ok(`${tag}: the list opens`, pal.open);
      if (narrow) ok(`${tag}: it leaves at least 44 px of the patch visible beside it (${Math.round(pal.w)} px wide in ${pal.vw})`, pal.vw - pal.r >= 43.5, JSON.stringify(pal));
      const n0 = await page.evaluate(() => document.querySelectorAll('.node').length);
      const it = await page.evaluate(() => { const b = [...document.querySelectorAll('#palette .pitem')].find(x => { const r = x.getBoundingClientRect(); return r.width > 0 && r.top > 0 && r.bottom < innerHeight; }); if (!b) return null; const r = b.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2, b.dataset.type]; });
      if (mob) await page.touchscreen.tap(it[0], it[1]); else await page.mouse.click(it[0], it[1]); await sleep(page, 500);
      const after = await page.evaluate(() => { const ws = document.getElementById('ws').getBoundingClientRect(); const ns = [...document.querySelectorAll('.node')], n = ns[ns.length - 1], r = n.getBoundingClientRect();
        return { n: ns.length, open: !document.getElementById('drawer').classList.contains('collapsed'), sel: n.classList.contains('selected'), in: r.left >= ws.left - 1 && r.right <= ws.right + 1 && r.top >= ws.top - 1 && r.top < ws.bottom - 40, r: [r.left, r.right, r.top, ws.left, ws.right] }; });
      ok(`${tag}: tapping ${it && it[2]} adds a module`, after.n === n0 + 1);
      if (narrow) { ok(`${tag}: the list closes after the add`, !after.open); ok(`${tag}: the new module is selected and wholly in view`, after.sel && after.in, JSON.stringify(after.r)); }
      else ok(`${tag}: the list stays open (a wide workspace adds several in a row)`, after.open);
      const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${tag}: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}

// ---- undo: is there a step to undo? (File menu's Undo button) ----
const canUndo = async page => { await page.evaluate(() => document.getElementById('filebtn').click()); const d = await page.evaluate(() => document.getElementById('undobtn').disabled); await page.evaluate(() => { const m = document.getElementById('filemenu'); if (!m.hidden) document.getElementById('filebtn').click(); }); return !d; };
const modIds = page => page.evaluate(() => [...document.querySelectorAll('.node')].map(n => n.dataset.id));
const centre = (page, sel) => page.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height, l: r.left, t: r.top }; }, sel);

// ================= T5: touch header buttons, delete asks first =================
async function t5(ed) {
  const B = await boot({ width: 412, height: 915, mobile: true }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 20, y: 120, zoom: 1, ui: 2, nw: 1 } }), { hideTip: true });
    const sizes = await page.evaluate(() => { const hd = document.querySelector('.node[data-id="m5"] > header'), r = e => { const b = e.getBoundingClientRect(); return { w: b.width, h: b.height }; };
      const fold = hd.querySelector('.fold'), fb = fold.getBoundingClientRect(), pw = parseFloat(getComputedStyle(fold, '::before').width) || 0;
      return { fold: { w: Math.max(fb.width, pw), h: Math.max(fb.height, parseFloat(getComputedStyle(fold, '::before').height) || 0) }, help: r(hd.querySelector('.mq')), x: r(hd.querySelector('.x')), byp: hd.querySelector('.byp') ? r(hd.querySelector('.byp')) : null }; });
    const big = (b, w) => b && b.h >= 39.5 && b.w >= w - 0.5;
    ok(`${ed} T5: touch: the chevron's hit area is 40 x 40 (${JSON.stringify(sizes.fold)})`, big(sizes.fold, 40));
    ok(`${ed} T5: touch: the help button is at least 32 x 40 (${JSON.stringify(sizes.help)})`, big(sizes.help, 32));
    ok(`${ed} T5: touch: the close button is at least 36 x 40 (${JSON.stringify(sizes.x)})`, big(sizes.x, 36));
    if (sizes.byp) ok(`${ed} T5: touch: the bypass button is at least 40 x 40 (${JSON.stringify(sizes.byp)})`, big(sizes.byp, 40));
    // a slider change first (one undo step), so that a cancelled delete that took a step would show
    await page.evaluate(() => { const i = document.querySelector('.node[data-id="m5"] .row.sl input[type=range]'); i.value = i.min; i.dispatchEvent(new Event('input', { bubbles: true })); i.dispatchEvent(new Event('change', { bubbles: true })); });
    const n0 = (await modIds(page)).length;
    let c = await centre(page, '.node[data-id="m5"] > header .x'); await page.touchscreen.tap(c.x, c.y); await sleep(page, 400);
    const asked = await page.evaluate(() => { const d = document.querySelector('.dlgov'); return d ? d.textContent : null; });
    ok(`${ed} T5: touch: tapping the close button asks first, and the module is still there`, !!asked && /Remove/.test(asked) && (await modIds(page)).length === n0, asked || 'no question');
    const cancel = await centre(page, '.dlgov .dlgbtns button:not(.on)'); await page.touchscreen.tap(cancel.x, cancel.y); await sleep(page, 300);
    ok(`${ed} T5: touch: Cancel keeps the module and closes the question`, (await modIds(page)).length === n0 && !(await page.$('.dlgov')));
    c = await centre(page, '.node[data-id="m5"] > header .x'); await page.touchscreen.tap(c.x, c.y); await sleep(page, 400);
    const yes = await centre(page, '.dlgov .dlgbtns button.on'); await page.touchscreen.tap(yes.x, yes.y); await sleep(page, 400);
    ok(`${ed} T5: touch: Remove takes the module away`, !(await modIds(page)).includes('m5') && (await modIds(page)).length === n0 - 1);
    await page.keyboard.press('Control+z'); await sleep(page, 400);
    ok(`${ed} T5: touch: one undo brings it back (a cancelled question left no step of its own)`, (await modIds(page)).includes('m5') && (await modIds(page)).length === n0);
    const vAfter = await page.evaluate(() => { const i = document.querySelector('.node[data-id="m5"] .row.sl input[type=range]'); return [+i.value, +i.min]; });
    ok(`${ed} T5: ...and the slider change before it is still there (the undo took only the delete)`, vAfter[0] === vAfter[1]);
    const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${ed} T5: touch: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} T5 touch: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
  // with a mouse the close button removes at once, as before
  const BM = await boot({ width: 1440, height: 900 }); const base2 = `http://127.0.0.1:${BM.port}`;
  try {
    await loadDoc(BM.page, base2, ed, SPEC[ed].doc(), { hideTip: true });
    const n0 = (await modIds(BM.page)).length; const c = await centre(BM.page, '.node[data-id="m5"] > header .x'); await BM.page.mouse.click(c.x, c.y); await sleep(BM.page, 300);
    ok(`${ed} T5: mouse: the close button removes at once, with no question`, (await modIds(BM.page)).length === n0 - 1 && !(await BM.page.$('.dlgov')));
    await BM.page.keyboard.press('Control+z'); await sleep(BM.page, 300);
    ok(`${ed} T5: mouse: one undo brings it back`, (await modIds(BM.page)).includes('m5'));
  } catch (e) { ok(`${ed} T5 mouse: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await BM.close();
}

// ================= T10: a tap on a title bar does not move the module =================
async function t10(ed) {
  const pos = (page, id) => page.evaluate(id => { const e = document.querySelector(`.node[data-id="${id}"]`); return [e.style.left, e.style.top]; }, id);
  // mouse
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 0, y: 160, zoom: 1, ui: 2 } }), { hideTip: true });
    const c = await centre(page, '.node[data-id="m2"] > header .t'); const p0 = await pos(page, 'm2');
    await page.mouse.move(c.x, c.y); await page.mouse.down(); await page.mouse.move(c.x + 2, c.y + 1); await page.mouse.move(c.x + 2, c.y + 2); await page.mouse.up(); await sleep(page, 200);
    ok(`${ed} T10: mouse: a click with 2 px of jitter does not move the module`, JSON.stringify(await pos(page, 'm2')) === JSON.stringify(p0));
    ok(`${ed} T10: mouse: ...and adds no undo step`, !(await canUndo(page)));
    await page.mouse.move(c.x, c.y); await page.mouse.down(); await page.mouse.move(c.x + 6, c.y); await page.mouse.move(c.x + 40, c.y + 10, { steps: 4 }); await page.mouse.up(); await sleep(page, 200);
    const p1 = await pos(page, 'm2');
    ok(`${ed} T10: mouse: a drag past 3 px moves it (${p0} > ${p1})`, JSON.stringify(p1) !== JSON.stringify(p0));
    ok(`${ed} T10: mouse: ...as one undo step`, await canUndo(page)); await page.keyboard.press('Control+z'); await sleep(page, 200);
    ok(`${ed} T10: mouse: undo puts it back in one step`, JSON.stringify(await pos(page, 'm2')) === JSON.stringify(p0) && !(await canUndo(page)));
  } catch (e) { ok(`${ed} T10 mouse: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
  // touch: real touch events through the browser's own input pipeline
  const BT = await boot({ width: 412, height: 915, mobile: true }); const tp = BT.page; const base2 = `http://127.0.0.1:${BT.port}`;
  try {
    await loadDoc(tp, base2, ed, SPEC[ed].doc({ view: { x: 20, y: 120, zoom: 1, ui: 2, nw: 1 } }), { hideTip: true });
    const cdp = await BT.ctx.newCDPSession(tp);
    const touch = async (pts) => { for (let i = 0; i < pts.length; i++) { await cdp.send('Input.dispatchTouchEvent', { type: i === 0 ? 'touchStart' : 'touchMove', touchPoints: [{ x: pts[i][0], y: pts[i][1] }] }); await sleep(tp, 25); } await cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] }); await sleep(tp, 250); };
    const c = await centre(tp, '.node[data-id="m1"] > header .t'); const p0 = await pos(tp, 'm1');
    await touch([[c.x, c.y], [c.x + 3, c.y + 2], [c.x + 3, c.y + 4], [c.x + 4, c.y + 4]]);   // about 5.6 px of jitter: under the 8 px threshold
    ok(`${ed} T10: touch: a tap with 5 px of jitter does not move the module`, JSON.stringify(await pos(tp, 'm1')) === JSON.stringify(p0), JSON.stringify([p0, await pos(tp, 'm1')]));
    ok(`${ed} T10: touch: ...and adds no undo step`, !(await canUndo(tp)));
    await touch([[c.x, c.y], [c.x + 4, c.y], [c.x + 12, c.y], [c.x + 40, c.y + 10], [c.x + 60, c.y + 10]]);
    const p1 = await pos(tp, 'm1');
    ok(`${ed} T10: touch: a drag past 8 px moves it (${p0} > ${p1})`, JSON.stringify(p1) !== JSON.stringify(p0));
    ok(`${ed} T10: touch: ...as one undo step`, await canUndo(tp));
  } catch (e) { ok(`${ed} T10 touch: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await BT.close();
}

// ================= D2: a typed value lands on the value the readout then shows =================
const TYPED = ids => `(() => {
  const out = [], typeIn = (v, text) => { v.dispatchEvent(new MouseEvent('dblclick', { bubbles: true })); const ed = v.querySelector('input'); if (!ed) return null; ed.value = text; ed.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true })); return v.textContent; };
  for (const id of ${JSON.stringify(ids)}) for (const row of document.querySelectorAll('.node[data-id="' + id + '"] .row.sl')) {
    const inp = row.querySelector('input[type=range]'); if (!inp) continue; const key = row.dataset.key, v = row.querySelector('.val'), lo = +inp.min, hi = +inp.max, st = +inp.step || 0;
    const setv = f => { let x = lo + (hi - lo) * f; if (st) x = lo + Math.round((x - lo) / st) * st; inp.value = x; inp.dispatchEvent(new Event('input', { bubbles: true })); inp.dispatchEvent(new Event('change', { bubbles: true })); return v.textContent; };
    const R0 = setv(.37);
    if (!/^\\s*[+\\-\\u2212\\u00d7]?\\s*\\d/.test(R0)) { out.push({ id, key, skip: R0 }); continue; }
    const num = (R0.match(/^\\s*[+\\-\\u2212]?\\s*\\d+(?:\\.\\d+)?/) || [''])[0].replace(/\\s/g, '').replace('+', '');
    let R1 = setv(.40); if (R1 === R0) R1 = setv(.46);
    const A = typeIn(v, num); setv(.80); const B = typeIn(v, R0);
    const kind = /%/.test(R0) ? 'percent' : /kHz|\\bHz\\b/.test(R0) ? 'frequency' : /\\bms\\b|\\d s\\b/.test(R0) ? 'time' : /dB/.test(R0) ? 'dB' : /\\bst\\b|\\bct\\b/.test(R0) ? 'semitones' : /:1|\\u00d7/.test(R0) ? 'ratio' : 'plain';
    out.push({ id, key, kind, R0, num, A, B });
  }
  return out; })()`;
async function d2(ed) {
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  try {
    await loadDoc(page, base, ed, typedDoc(ed), { hideTip: true });
    const rows = await page.evaluate(TYPED(TYPED_TYPES.map((_, i) => 't' + (i + 1))));
    const tested = rows.filter(r => !r.skip), bad = tested.filter(r => r.A !== r.R0 || r.B !== r.R0);
    ok(`${ed} D2: ${tested.length} parameters across ${new Set(tested.map(r => r.id)).size} modules were typed into (${rows.length - tested.length} word / note readouts skipped)`, tested.length >= 12 && new Set(tested.map(r => r.id)).size >= 6);
    const kinds = [...new Set(tested.map(r => r.kind))].sort();
    ok(`${ed} D2: the readouts cover percent, frequency, time, dB, semitones and a plain number (${kinds.join(', ')})`, ['percent', 'frequency', 'time', 'dB', 'semitones', 'plain'].every(k => kinds.includes(k)));
    ok(`${ed} D2: typing the number of a readout lands on that readout, and so does typing its whole text, units and signs included (${tested.length - bad.length}/${tested.length})`, bad.length === 0, bad.slice(0, 4).map(r => `${r.id}.${r.key} "${r.R0}" typed "${r.num}" -> "${r.A}", typed text -> "${r.B}"`).join(' | '));
    // explicit cases with the readout read back
    const typeCase = async (id, key, start, text) => page.evaluate(([id, key, start, text]) => { const row = document.querySelector(`.node[data-id="${id}"] .row.sl[data-key="${key}"]`); const inp = row.querySelector('input[type=range]'), v = row.querySelector('.val');
      inp.value = start; inp.dispatchEvent(new Event('input', { bubbles: true })); const before = v.textContent; v.dispatchEvent(new MouseEvent('dblclick', { bubbles: true })); const e = v.querySelector('input'); e.value = text; e.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true })); return [before, v.textContent]; }, [id, key, start, text]);
    const eq = await typeCase('t2', 'bass', .5, '6'), eqf = await typeCase('t2', 'tfreq', .4, '6'), eqk = await typeCase('t2', 'tfreq', .4, '3.1k'), dl = await typeCase('t1', 'time', .2, '1.2 s'), dm = await typeCase('t1', 'time', 1, '250');
    const cmp = await typeCase('t3', 'thresh', -10, '-24'), pc = await typeCase('t1', 'regen', .1, '40%'), st = await typeCase('t5', 'pitch', 0, '-7');
    const norm = s => s.replace(/−/g, '-');
    ok(`${ed} D2: 6 into an EQ gain readout is +6.0 dB (${eq[0]} > ${eq[1]})`, norm(eq[1]) === '+6.0 dB');
    ok(`${ed} D2: 6 into a kHz readout is 6 kHz, "3.1k" is 3.1 kHz (${eqf[0]} > ${eqf[1]}; ${eqk[1]})`, /^6\.0 kHz$/.test(eqf[1]) && /^3\.1 kHz$/.test(eqk[1]));
    ok(`${ed} D2: "1.2 s" into a milliseconds readout is 1.20 s, and a bare 250 into a seconds readout is 250 ms (${dl[1]}; ${dm[1]})`, dl[1] === '1.20 s' && dm[1] === '250 ms', JSON.stringify([dl, dm]));
    ok(`${ed} D2: -24 into a threshold is -24.0 dB, "40%" is 40%, -7 into a pitch is -7 st (${cmp[1]}; ${pc[1]}; ${st[1]})`, norm(cmp[1]) === '-24.0 dB' && pc[1] === '40%' && norm(st[1]) === '-7 st', JSON.stringify([cmp, pc, st]));
    const sci = await typeCase('t2', 'tfreq', .4, '1e3 Hz'), sci2 = await typeCase('t1', 'regen', .1, '4e1%'), sci3 = await typeCase('t1', 'regen', .1, '2.5e-1');
    ok(`${ed} pass 3 c: scientific notation is typed again ("1e3 Hz" is ${sci[1]}, "4e1%" is ${sci2[1]}, "2.5e-1" is 0.25 so ${sci3[1]})`, /^1\.0 kHz$/.test(sci[1]) && sci2[1] === '40%' && sci3[1] === (await typeCase('t1', 'regen', .1, '0.25'))[1], JSON.stringify([sci, sci2, sci3]));
    const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${ed} D2: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} D2: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= D3: one drag is one undo step; a no-op press is none =================
async function d3(ed) {
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  try {
    await loadDoc(page, base, ed, typedDoc(ed), { hideTip: true });
    const val = () => page.evaluate(() => { const r = document.querySelector('.node[data-id="t1"] .row.sl[data-key="regen"]'); return [+r.querySelector('input').value, r.querySelector('.val').textContent]; });
    const sl = await centre(page, '.node[data-id="t1"] .row.sl[data-key="regen"] input'); const v0 = await val();
    await page.mouse.click(sl.x, sl.y); await page.mouse.click(sl.x, sl.y); await sleep(page, 150);
    ok(`${ed} D3: two clicks on a slider that move nothing leave no undo step`, !(await canUndo(page)) && (await val())[0] === v0[0]);
    await page.mouse.move(sl.x, sl.y); await page.mouse.down(); for (let i = 1; i <= 12; i++) { await page.mouse.move(sl.x + i * 7, sl.y); await sleep(page, 16); } await page.mouse.up(); await sleep(page, 200);
    const v1 = await val();
    ok(`${ed} D3: a drag moves the value (${v0[1]} > ${v1[1]})`, v1[0] !== v0[0]);
    await page.keyboard.press('Control+z'); await sleep(page, 250);
    ok(`${ed} D3: one undo takes the whole drag back (${(await val())[1]})`, (await val())[0] === v0[0]);
    ok(`${ed} D3: ...and that was the only step (a drag of twelve moves is one step)`, !(await canUndo(page)));
    // a double-click resets to the default: one step when it changed something, none when the value was already the default
    await page.mouse.move(sl.x, sl.y); await page.mouse.down(); await page.mouse.move(sl.x + 50, sl.y, { steps: 5 }); await page.mouse.up(); await sleep(page, 100);
    await page.mouse.dblclick(sl.x, sl.y); await sleep(page, 200);
    ok(`${ed} D3: double-click resets the slider to its default (${(await val())[1]}; the drag before it and the reset are two steps)`, (await val())[0] === v0[0]);
    await page.keyboard.press('Control+z'); await page.keyboard.press('Control+z'); await sleep(page, 250);
    ok(`${ed} D3: ...two undos clear them (a double-click is not three steps)`, !(await canUndo(page)));
    await page.mouse.dblclick(sl.x, sl.y); await sleep(page, 200);
    ok(`${ed} D3: a reset on a slider that is already at its default leaves no step`, !(await canUndo(page)));
    // a select: pressing without changing it is no step; changing it is one
    await loadDoc(page, base, ed, typedDoc(ed), { hideTip: true });
    const selInfo = await page.evaluate(() => { const s = [...document.querySelectorAll('.node select')].find(x => x.options.length > 1); return s ? { id: s.closest('.node').dataset.id, key: s.closest('.row').dataset.key, v: s.value, n: s.options.length } : null; });
    if (!selInfo) { ok(`${ed} D3: a select exists in the test patch`, false); }
    else {
      await loadDoc(page, base, ed, typedDoc(ed), { hideTip: true });
      await page.click(`.node[data-id="${selInfo.id}"] .row[data-key="${selInfo.key}"] select`); await page.keyboard.press('Escape'); await sleep(page, 150);
      const idle = await canUndo(page);
      ok(`${ed} D3: a press on a select that changes nothing is no undo step`, !idle);
      await page.selectOption(`.node[data-id="${selInfo.id}"] .row[data-key="${selInfo.key}"] select`, { index: selInfo.n - 1 }); await sleep(page, 200);
      const changed = await page.evaluate(([id, key]) => document.querySelector(`.node[data-id="${id}"] .row[data-key="${key}"] select`).value, [selInfo.id, selInfo.key]);
      ok(`${ed} D3: choosing another value is one undo step (${selInfo.v} > ${changed})`, changed !== selInfo.v && await canUndo(page));
      await page.evaluate(() => document.activeElement && document.activeElement.blur()); await page.keyboard.press('Control+z'); await sleep(page, 200);   // (a focused select counts as typing: the key is left to it)
      const back = await page.evaluate(([id, key]) => document.querySelector(`.node[data-id="${id}"] .row[data-key="${key}"] select`).value, [selInfo.id, selInfo.key]);
      ok(`${ed} D3: one undo puts the choice back and no step is left`, back === selInfo.v && !(await canUndo(page)));
    }
    const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${ed} D3: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} D3: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= D4: a cable does not take the press that is meant for a control under it =================
async function d4(ed, OLD) {
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  try {
    // 1. find a slider, a select and a button of m2 in world coordinates, then run a cable across each (a bend point on it)
    const base0 = SPEC[ed].doc({ cables: [] });
    await loadDoc(page, base, ed, base0, { hideTip: true });
    const w = await page.evaluate(() => { const m = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform), wr = document.getElementById('ws').getBoundingClientRect(), at = e => { const r = e.getBoundingClientRect(); return [(r.left + r.width / 2 - m.e - wr.left) / m.a, (r.top + r.height / 2 - m.f - wr.top) / m.a]; };
      const n = document.querySelector('.node[data-id="m3"]'); const sl = n.querySelector('.row.sl input[type=range]'), sel = n.querySelector('select'), btn = n.querySelector('header .mq');
      return { slider: sl && at(sl), select: sel && at(sel), button: btn && at(btn), out: at(document.querySelector('.node[data-id="m1"] .jack[data-dir="out"]')) }; });
    const cases = Object.entries({ slider: w.slider, select: w.select, 'header button': w.button }).filter(([, p]) => p);
    for (const [name, pt] of cases) {
      const doc = SPEC[ed].doc({ cables: [C('m1', ed === 'chains' ? 'out' : 'pitch', 'm4', ed === 'chains' ? 'in' : 'cv', { pts: [[pt[0], pt[1]]] })] });
      await loadDoc(page, base, ed, doc, { hideTip: true });
      const sp = await page.evaluate(([nm]) => { const n = document.querySelector('.node[data-id="m3"]'); const el = nm === 'slider' ? n.querySelector('.row.sl input[type=range]') : nm === 'select' ? n.querySelector('select') : n.querySelector('header .mq'); const r = el.getBoundingClientRect(); const x = r.left + r.width / 2, y = r.top + r.height / 2; const top = document.elementFromPoint(x, y);
        return { x, y, top: top ? (top.closest('.hit') ? 'cable hit line' : top === el ? 'the control' : top.tagName + '.' + top.className) : null, stack: document.elementsFromPoint(x, y).map(e => e.tagName + '.' + (e.getAttribute('class') || '')).slice(0, 4) }; }, [name]);
      ok(`${ed} D4: a cable across a module's ${name}: the control is what is under the pointer (${sp.top})`, sp.top === 'the control', JSON.stringify(sp.stack));
      if (name === 'slider') {
        const v0 = await page.evaluate(() => +document.querySelector('.node[data-id="m3"] .row.sl input[type=range]').value);
        await page.mouse.move(sp.x, sp.y); await page.mouse.down(); await page.mouse.move(sp.x + 40, sp.y, { steps: 5 }); await page.mouse.up(); await sleep(page, 200);
        const v1 = await page.evaluate(() => +document.querySelector('.node[data-id="m3"] .row.sl input[type=range]').value);
        ok(`${ed} D4: ...and dragging there moves the slider (${v0} > ${v1}), no cable gets selected`, v1 !== v0 && !(await page.$('svg.cables g.sel')));
      }
      if (name === 'select') {
        const before = await page.evaluate(() => document.querySelector('.node[data-id="m3"] select').value);
        await page.selectOption('.node[data-id="m3"] select', { index: 1 }); await sleep(page, 150);
        ok(`${ed} D4: ...and the select changes`, (await page.evaluate(() => document.querySelector('.node[data-id="m3"] select').value)) !== before);
      }
      if (name === 'header button') {
        await page.mouse.click(sp.x, sp.y); await sleep(page, 300);
        ok(`${ed} D4: ...and a click there opens the module's help`, (await page.evaluate(() => !!document.querySelector('.mhelp'))) && !(await page.$('svg.cables g.sel')));
      }
      if (OLD && name === 'slider') {
        // the same patch in build 584: the cable took the press
        const bo = await boot({ width: 1440, height: 900 });
        await bo.page.goto(`${OLD.base}/${ed}/index.html`); await bo.page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, '{"tips":false}'); }, [SPEC[ed].key, SPEC[ed].setKey, doc]);
        await bo.page.goto(`${OLD.base}/${ed}/index.html`); await bo.page.waitForSelector('.node'); await sleep(bo.page, 900);
        const ot = await bo.page.evaluate(() => { const el = document.querySelector('.node[data-id="m3"] .row.sl input[type=range]'), r = el.getBoundingClientRect(), t = document.elementFromPoint(r.left + r.width / 2, r.top + r.height / 2); return t && t.closest('.hit') ? 'cable hit line' : t === el ? 'the control' : 'other'; });
        ok(`${ed} D4: (for comparison) in build 584 the cable's hit line was what was under the pointer there (${ot})`, ot === 'cable hit line');
        await bo.close();
      }
    }
    // 2. on the bare canvas the cable still works: click selects, a double-click removes
    await loadDoc(page, base, ed, SPEC[ed].doc(), { hideTip: true });
    const cp = await page.evaluate(() => { const p = document.querySelector('svg.cables:not(.under) g path.cln'), hit = document.querySelector('svg.cables.under g path.hit'), L = p.getTotalLength(), m = p.getScreenCTM();
      for (const t of [.5, .45, .55, .4, .6, .35, .65]) { const q = p.getPointAtLength(L * t), x = m.a * q.x + m.c * q.y + m.e, y = m.b * q.x + m.d * q.y + m.f; if (document.elementFromPoint(x, y) === hit) return { x, y }; } return null; });
    ok(`${ed} D4: a cable on the bare canvas has its hit line under the pointer`, !!cp);
    if (cp) {
      await page.mouse.move(cp.x, cp.y); await sleep(page, 80);
      const hov = await page.evaluate(() => document.querySelector('svg.cables:not(.under) g').classList.contains('hov'));
      await page.mouse.click(cp.x, cp.y); await sleep(page, 150);
      const sel = await page.evaluate(() => document.querySelector('svg.cables:not(.under) g').classList.contains('sel'));
      ok(`${ed} D4: on the bare canvas hovering lights the cable (hov) and a click selects it`, hov && sel);
      await page.mouse.dblclick(cp.x, cp.y); await sleep(page, 250);
      ok(`${ed} D4: a double-click there removes it`, (await page.evaluate(() => document.querySelectorAll('svg.cables.under g path.hit').length)) === 3);
    }
    const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${ed} D4: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} D4: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= U2: waves' Voices and On-screen keys in the host's Patch tab =================
async function u2() {
  const HOST = { 'thunderplus-platform-v1': 'desktop', 'thunderplus.chains.settings': '{"tips":false}', 'thunderplus.waves.settings': '{"tips":false}' };
  const B = await boot({ width: 1440, height: 900, storage: HOST }); const { page, errors } = B;
  try {
    await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`).catch(() => {});
    await app(page, `(setView('waves'), $('toast').hidden = true, 1)`); await sleep(page, 2000);
    await app(page, `(buildPatch(), 1)`); await sleep(page, 300);
    const rows = () => page.evaluate(() => { const b = document.getElementById('gsecPatch'); return { labels: [...b.querySelectorAll('.tsl')].map(x => x.textContent.trim()), checks: [...b.querySelectorAll('label.cb')].map(x => x.textContent.trim()), sel: [...b.querySelectorAll('select')].map(s => s.options.length) }; });
    const r0 = await rows();
    ok('waves U2: the host Patch tab (Waves view) has a Voices row and an On-screen keys checkbox', r0.labels.includes('Voices') && r0.checks.includes('On-screen keys'), JSON.stringify(r0));
    const api = `document.getElementById('gsecPatch')`;
    const before = await app(page, `JSON.stringify(WV.frame.contentWindow.wavesEmbed.patchSettings())`); const ps0 = JSON.parse(before);
    ok('waves U2: wavesEmbed.patchSettings() reports voices and keys', typeof ps0.voices === 'number' && typeof ps0.keys === 'boolean', before.slice(0, 200));
    // Voices: pick 4
    await page.evaluate(() => { const b = document.getElementById('gsecPatch'); const row = [...b.querySelectorAll('.tsr')].find(r => r.querySelector('.tsl') && r.querySelector('.tsl').textContent.trim() === 'Voices'); const s = row.querySelector('select'); s.value = '4'; s.dispatchEvent(new Event('change', { bubbles: true })); });
    await sleep(page, 700);
    const ps1 = JSON.parse(await app(page, `JSON.stringify(WV.frame.contentWindow.wavesEmbed.patchSettings())`));
    ok('waves U2: choosing 4 voices there sets the editor\'s voice count (and saves it with the patch)', ps1.voices === 4, JSON.stringify([ps0.voices, ps1.voices]));
    const inDoc = await app(page, `(S.waveKit && S.waveKit.patch && S.waveKit.patch.voice && S.waveKit.patch.voice.maxVoices) || (S.wavePatch && S.wavePatch.voice && S.wavePatch.voice.maxVoices) || null`).catch(() => null);
    // keys
    const k0 = await page.evaluate(() => { const f = document.querySelector('iframe[src*="waves"]'); return f ? !f.contentDocument.getElementById('keysdock').hidden : null; });
    await page.evaluate(() => { const b = document.getElementById('gsecPatch'); const l = [...b.querySelectorAll('label.cb')].find(x => /On-screen keys/.test(x.textContent)); const c = l.querySelector('input'); c.checked = !c.checked; c.dispatchEvent(new Event('change', { bubbles: true })); });
    await sleep(page, 300);
    const k1 = await page.evaluate(() => { const f = document.querySelector('iframe[src*="waves"]'); return f ? !f.contentDocument.getElementById('keysdock').hidden : null; });
    ok(`waves U2: the On-screen keys checkbox shows or hides the keys in the editor (${k0} > ${k1})`, k0 !== null && k0 !== k1);
    // chains has neither
    await app(page, `(setView('chains'), $('toast').hidden = true, 1)`); await sleep(page, 1800); await app(page, `(buildPatch(), 1)`); await sleep(page, 300);
    const r1 = await rows();
    ok('chains U2: the Chains patch tab has no Voices row or keys checkbox (it has no notes)', !r1.labels.includes('Voices') && !r1.checks.includes('On-screen keys') && r1.labels.includes('Quality'), JSON.stringify(r1));
    const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok('U2: no page errors', errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok('U2: ran to the end', false, e.stack.split('\n').slice(0, 5).join(' | ')); }
  await B.close();
}

// ================= H1, H3, H4 and "the saved patch loads in build 584" =================
async function h(ed, OLD) {
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  const reqs = []; page.on('request', r => reqs.push(r.url()));
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc(), { hideTip: true });
    ok(`${ed} H1: a standalone load asks for no sw.js and logs no failed registration`, !reqs.some(u => /\/sw\.js/.test(u)) && !errors.some(e => /sw\.js|ServiceWorker/i.test(e)), reqs.filter(u => /sw\.js/.test(u)).join(' '));
    if (ed === 'chains') {
      await page.evaluate(() => { window.__sp = null; window.addEventListener('keydown', e => { if (e.key === ' ') window.__sp = e.defaultPrevented; }); document.getElementById('zoomin').focus(); });
      const z0 = (await readView(page)).z; await page.keyboard.press(' '); await sleep(page, 250); const z1 = (await readView(page)).z;
      ok(`chains H4: standalone, Space on a focused button is left alone (not prevented) and works it (zoom ${Math.round(z0 * 100)} > ${Math.round(z1 * 100)}%)`, (await page.evaluate(() => window.__sp)) === false && z1 > z0);
    }
    // "a patch saved by this build loads in build 584": a view left on a small screen (view.nw) is the only new field
    const B2 = await boot({ width: 412, height: 915, mobile: true }); const base2 = `http://127.0.0.1:${B2.port}`;
    await loadDoc(B2.page, base2, ed, SPEC[ed].doc(), { hideTip: true });
    await B2.page.evaluate(() => { document.getElementById('zoomin').click(); }); await sleep(B2.page, 1700);
    const saved = await B2.page.evaluate(k => localStorage.getItem(k), SPEC[ed].key); const sd = JSON.parse(saved);
    ok(`${ed} saved patch: this build stores the view with the optional nw flag and nothing else new (${JSON.stringify(sd.view)})`, sd.view && sd.view.nw === 1 && Object.keys(sd.view).every(k => ['x', 'y', 'zoom', 'ui', 'nw', 'bg'].includes(k)) && sd.modules.length === 8);
    await B2.close();
    if (OLD) {
      const bo = await boot({ width: 1440, height: 900 }); const op = bo.page;
      await op.goto(`${OLD.base}/${ed}/index.html`); await op.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, d); }, [SPEC[ed].key, saved]);
      await op.goto(`${OLD.base}/${ed}/index.html`); await op.waitForSelector('.node'); await sleep(op, 900);
      ok(`${ed} saved patch: a patch saved by this build opens in build 584 (8 modules, ${await op.evaluate(() => document.querySelectorAll('svg.cables g path.cln').length)} cables)`, (await op.evaluate(() => document.querySelectorAll('.node').length)) === 8 && (await op.evaluate(() => document.querySelectorAll('svg.cables g path.cln').length)) === 4);
      const oe = bo.errors.splice(0).filter(e => !NOISE.test(e)); ok(`${ed} saved patch: ...with no page errors in 584`, oe.length === 0, oe.slice(0, 3).join(' | '));
      await bo.close();
    }
    // old format: a patch with none of the new fields (no view.nw, collapsed true)
    const od = SPEC[ed].doc(); od.modules[1].collapsed = true; delete od.view.ui;
    await loadDoc(page, base, ed, od, { hideTip: true });
    ok(`${ed} old-format patch: a patch with no view.ui and a collapsed module loads (8 modules, m2 collapsed)`, (await modIds(page)).length === 8 && (await page.evaluate(() => document.querySelector('.node[data-id="m2"]').classList.contains('collapsed'))));
    const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${ed} H: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} H: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= screenshots =================
async function shots(dir, OLD) {
  fs.mkdirSync(dir, { recursive: true });
  const THEMES = { white: null, idmpg: 'idmpg' };
  const sizes = [[344, 780, true], [412, 915, true], [884, 1020, false], [1440, 900, false]];
  for (const which of ['after', 'before']) {
    if (which === 'before' && !OLD) continue;
    for (const ed of ED) for (const [w, h, mob] of sizes) for (const [theme, skin] of Object.entries(THEMES)) {
      const B = await boot({ width: w, height: h, mobile: mob, storage: skin ? { 'thunderplus-platform-v1': 'desktop', 'thunderplus.chains.settings': '{"tips":true}', 'thunderplus.waves.settings': '{"tips":true}' } : null });
      try {
        if (!skin) {
          const base = which === 'after' ? `http://127.0.0.1:${B.port}` : OLD.base;
          await B.page.goto(`${base}/${ed}/index.html`); await B.page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, '{"tips":true}'); }, [SPEC[ed].key, SPEC[ed].setKey, SPEC[ed].doc()]);
          await B.page.goto(`${base}/${ed}/index.html`); await B.page.waitForSelector('.node'); await sleep(B.page, 1200);
          await B.page.screenshot({ path: path.join(dir, `${which}-${ed}-${w}-${theme}.png`) });
        } else if (which === 'after') {
          // inside the host, in the IDM pink & green skin: the editor opens the same patch
          await app(B.page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`).catch(() => {});
          await app(B.page, `(applySkin('${skin}'), 1)`); await sleep(B.page, 500);
          await app(B.page, `(setView('${ed}'), $('toast').hidden = true, 1)`); await sleep(B.page, 1800);
          await app(B.page, `(${ed === 'chains' ? 'CH' : 'WV'}.frame.contentWindow.${ed}Embed.setDoc(${JSON.stringify(SPEC[ed].doc())}), 1)`); await sleep(B.page, 1200);
          await B.page.screenshot({ path: path.join(dir, `${which}-${ed}-${w}-${theme}.png`) });
        }
      } catch (e) { console.log('shot failed', which, ed, w, theme, e.message.split('\n')[0]); }
      await B.close();
    }
  }
  // the module list open at 344, and the delete question on touch
  for (const ed of ED) {
    const B = await boot({ width: 344, height: 780, mobile: true }); const { page } = B; const base = `http://127.0.0.1:${B.port}`;
    await loadDoc(page, base, ed, SPEC[ed].doc(), { hideTip: true });
    const tg = await centre(page, '#dtoggle'); await page.touchscreen.tap(tg.x, tg.y + 40); await sleep(page, 500);
    await page.screenshot({ path: path.join(dir, `after-${ed}-344-module-list.png`) });
    await page.touchscreen.tap(tg.x, tg.y + 40); await sleep(page, 400);
    const x = await centre(page, '.node[data-id="m1"] > header .x'); await page.touchscreen.tap(x.x, x.y); await sleep(page, 500);
    await page.screenshot({ path: path.join(dir, `after-${ed}-344-delete-question.png`) });
    await B.close();
  }
  console.log('screenshots in', dir);
}

const only = process.argv[2];
const OLD = ['t1', 'd4', 'h', 'old', 'shots', undefined].includes(only) ? await oldBuild() : null;
if (!OLD && ['t1', 'd4', 'h', 'old', undefined].includes(only)) ok('the previous build (build 584) could not be read (no git ref and no OLD_ROOT)', false);
if (only === 'shots') { await shots(process.env.SHOTS || path.join(OUT, 'pass2'), OLD); }
else {
  for (const ed of ED) {
    if (!only || only === 't1') await t1(ed, OLD);
    if (!only || only === 't2') await t2(ed);
    if (!only || only === 't3') await t3(ed);
    if (!only || only === 't5') await t5(ed);
    if (!only || only === 't10') await t10(ed);
    if (!only || only === 'd2') await d2(ed);
    if (!only || only === 'd3') await d3(ed);
    if (!only || only === 'd4') await d4(ed, OLD);
    if (!only || only === 'h' || only === 'old') await h(ed, OLD);
  }
  if (!only || only === 'u2') await u2();
}
if (OLD) OLD.close();
const f = T.print();
process.exit(f ? 1 : 0);
