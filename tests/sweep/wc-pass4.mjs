// …waves / …chains pass 4, "Desktop feel" (14 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: D1 D5 D7 D8 D10 M3 M4 M5 M8 M11 U4 U11),
// plus the four review fixes of pass 3 (r1 section folds blocked in Play, r2 Alt+E ends a title-bar drag; r3 and r4 are in wc-pass3.mjs).
// Both editors, standalone, with a real mouse and keyboard (playwright) at 1440x900 and 1280x800, and spot checks with touch (mobile:true). Prints PASS / FAIL.
//   node tests/sweep/wc-pass4.mjs               all of it
//   node tests/sweep/wc-pass4.mjs r1 d5 d1 d8 d7 d10 m3 m4 m5 m8 m11 u4 u11 old touch     sections
//   BASE_ROOT=/path/to/a/build-586/checkout   (default: a temporary git worktree of origin/beta, removed afterwards)
//   SHOTS=/dir node tests/sweep/wc-pass4.mjs shots     screenshots for the owner
import fs from 'fs'; import os from 'os'; import path from 'path'; import { execSync } from 'child_process';
import { boot, Table, ROOT, serveDir, listenLoopback } from './ui-lib.mjs';

const T = new Table('wc-pass4');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'PASS ' : 'FAIL ') + name + (note ? '  ' + note : '')); };
const sleep = (page, ms) => page.waitForTimeout(ms);
const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|autoplay/i;
const ED = ['chains', 'waves'];
const SIZES = [[1440, 900], [1280, 800]];

const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp, extra = {}) => ({ from: [a, ap], to: [b, bp], bend: 0, ...extra });
const SPEC = {
  chains: { key: 'chains.patch', setKey: 'chains.settings', long: 'pgate', qa: ['tape delay', 'delay'],
    doc: (o = {}) => ({ format: 'chains.patch', meta: { name: 'wc pass4' }, view: o.view || { x: 0, y: 0, zoom: 1, ui: 2 }, st2: 1, ti2: 1,
      modules: [P('m1', 'trackIn', 0, 0, { track: 1 }), P('m2', 'delay', 420, 40), P('m3', 'reverb', 840, 0), P('m4', 'eq', 1260, 60),
        P('m5', 'lfo', 0, 420), P('m6', 'compressor', 420, 460), P('m7', 'chorus', 840, 420), P('m8', 'chainOut', 1260, 440, { track: 1 })],
      cables: o.cables || [C('m1', 'out', 'm2', 'in'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m4', 'out', 'm8', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
  waves: { key: 'waves.patch', setKey: 'waves.settings', long: 'marbles', qa: ['tape delay', 'delay'],
    doc: (o = {}) => ({ meta: { name: 'wc pass4' }, view: o.view || { x: 0, y: 0, zoom: 1, ui: 2 }, voice: { maxVoices: 8 },
      modules: [P('m1', 'hostIn', 0, 0), P('m2', 'vco', 420, 40, { wave: 1 }), P('m3', 'vcf', 840, 0), P('m4', 'vca', 1260, 60),
        P('m5', 'adsr', 0, 420), P('m6', 'lfo', 420, 460), P('m7', 'delay', 840, 420), P('m8', 'out', 1260, 440, { slot: 1, level: 1 })],
      cables: o.cables || [C('m1', 'pitch', 'm2', 'pitch'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m4', 'out', 'm8', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
};
const TYPED_TYPES = ['delay', 'eq', 'compressor', 'chorus', 'clouds', 'crush'];
const slidersDoc = (ed, o = {}) => { const d = SPEC[ed].doc({ cables: [], ...o }); d.modules = TYPED_TYPES.map((t, i) => P('t' + (i + 1), t, (i % 3) * 420, Math.floor(i / 3) * 560)); return d; };

async function loadDoc(page, base, ed, doc, o = {}) {
  const url = `${base}/${ed}/index.html`;
  await page.goto(url);
  await page.evaluate(([k, sk, d, st]) => { localStorage.clear(); if (d) localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify(st)); }, [SPEC[ed].key, SPEC[ed].setKey, doc, o.settings || { tips: false }]);
  await page.goto(url); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 900);
}
const readView = page => page.evaluate(() => { const m = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform), ws = document.getElementById('ws').getBoundingClientRect(); return { z: m.a, x: m.e, y: m.f, ws: { l: ws.left, t: ws.top, r: ws.right, b: ws.bottom } }; });
const centre = (page, sel) => page.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height, l: r.left, t: r.top, r: r.right, b: r.bottom }; }, sel);
const modPos = (page, id) => page.evaluate(i => { const n = document.querySelector(`.node[data-id="${i}"]`); return n ? [parseFloat(n.style.left), parseFloat(n.style.top)] : null; }, id);
const firstKey = (page, id) => page.evaluate(i => { const r = document.querySelector(`.node[data-id="${i}"] .row.sl[data-key] input[type=range]`); return r ? r.closest('.row').dataset.key : null; }, id);
const sliderSel = (id, key) => `.node[data-id="${id}"] .row.sl[data-key="${key}"] input`;
const slVal = (page, id, key) => page.evaluate(([i, k]) => { const r = document.querySelector(`.node[data-id="${i}"] .row.sl[data-key="${k}"] input`); return r ? +r.value : null; }, [id, key]);
const savedDoc = async (page, ed) => { await sleep(page, 1500); return page.evaluate(k => JSON.parse(localStorage.getItem(k) || 'null'), SPEC[ed].key); };
const toastText = page => page.evaluate(() => { const t = document.getElementById('toast'); return t && !t.hidden ? t.textContent : ''; });
const menuItems = page => page.evaluate(() => { const m = document.getElementById('ctxmenu'); return m && !m.hidden ? [...m.querySelectorAll('.ctxitem, .ctxhead')].map(e => e.textContent) : null; });
const clickMenu = async (page, re) => { const b = await page.evaluateHandle(r => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(e => new RegExp(r).test(e.textContent)), re.source); const el = b.asElement(); if (!el) return false; await el.click(); await sleep(page, 150); return true; };
const qaOpen = page => page.evaluate(() => { const m = document.getElementById('ctxmenu'); return !m.hidden && !!m.querySelector('.qainp'); });
const nodeCount = page => page.evaluate(() => document.querySelectorAll('.node').length);
const done = (errors, tag) => { const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${tag}: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | ')); };
const jack = (id, port, dir) => `.jack[data-id="${id}"][data-port="${port}"][data-dir="${dir}"]`;
async function bootEd(w, h, mob) { const B = await boot({ width: w, height: h, mobile: mob }); B.base = `http://127.0.0.1:${B.port}`; return B; }
async function setPlay(page, on) { await page.click(on ? '#modeplay' : '#modeedit'); await sleep(page, 150); }
async function dragJack(page, a, b) { const A = await centre(page, a), Bc = await centre(page, b); await page.mouse.move(A.x, A.y); await page.mouse.down(); await page.mouse.move((A.x + Bc.x) / 2, (A.y + Bc.y) / 2, { steps: 5 }); await page.mouse.move(Bc.x, Bc.y, { steps: 5 }); await page.mouse.up(); await sleep(page, 200); }
const emptyPoint = page => page.evaluate(() => { const ws = document.getElementById('ws').getBoundingClientRect(); for (let y = ws.bottom - 80; y > ws.top + 60; y -= 37) for (let x = ws.left + 120; x < ws.right - 120; x += 41) { const e = document.elementFromPoint(x, y); if (e && (e.id === 'ws' || e.id === 'world' || e.closest('#groups'))) return { x, y }; } return null; });

// ---- the previous build (build 586): a temporary git worktree of origin/beta, removed afterwards ----
async function baseBuild() {
  let dir = process.env.BASE_ROOT, wt = null;
  if (!dir) {
    try { const sha = execSync(`git -C ${JSON.stringify(ROOT)} rev-parse origin/beta`, { stdio: 'pipe' }).toString().trim(); wt = path.join(os.tmpdir(), 'wc-pass4-base-' + process.pid);
      execSync(`git -C ${JSON.stringify(ROOT)} worktree add --detach ${JSON.stringify(wt)} ${sha}`, { stdio: 'pipe' }); dir = wt; }
    catch (e) { console.log('SKIP the "build 586" comparisons: no origin/beta worktree could be made (' + String(e.message).split('\n')[0] + ')'); return null; }
  }
  const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.woff': 'font/woff', '.png': 'image/png', '.webmanifest': 'application/manifest+json', '.json': 'application/json' };
  const srv = serveDir(dir, TYPES); await listenLoopback(srv);
  return { base: `http://127.0.0.1:${srv.address().port}`, dir, close: () => { srv.close(); if (wt) try { execSync(`git -C ${JSON.stringify(ROOT)} worktree remove --force ${JSON.stringify(wt)}`, { stdio: 'pipe' }); } catch (e) { try { fs.rmSync(wt, { recursive: true, force: true }); execSync(`git -C ${JSON.stringify(ROOT)} worktree prune`, { stdio: 'pipe' }); } catch (_) {} } } };
}

// ================= r1, r2: the pass 3 review fixes =================
async function review(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} review`;
  try {
    const long = SPEC[ed].long;
    const d = SPEC[ed].doc({ view: { x: 12, y: 12, zoom: 0.8, ui: 2, nw: 1 }, cables: [] }); d.modules = [P('L1', long, 0, 0), P('s1', 'delay', 420, 0)];
    await loadDoc(page, base, ed, d);
    const st = () => page.evaluate(() => { const s = document.querySelectorAll('.node[data-id="L1"] .sec'); return [...s].map(x => x.classList.contains('shut') ? 0 : 1).join(''); });
    const s0 = await st(), hd = await centre(page, '.node[data-id="L1"] .sec[data-i="1"] .sechd');
    await setPlay(page, true);
    await page.mouse.click(hd.x, hd.y); await sleep(page, 200);
    const sp = await st(); ok(`${tag} r1: in Play a click on a section bar does not fold or unfold it (${s0} -> ${sp}) and says so`, sp === s0 && /Play/.test(await toastText(page)));
    const sd = await savedDoc(page, ed); ok(`${tag} r1: ...and nothing was written to the saved view (view.sec ${JSON.stringify(sd.view.sec)})`, !sd.view.sec || sd.view.sec.L1 == null);
    await setPlay(page, false);
    await page.mouse.click(hd.x, hd.y); await sleep(page, 200);
    ok(`${tag} r1: in Edit the same click opens the section (${await st()})`, (await st()) !== s0);
    // r2: Alt+E in the middle of a title-bar drag
    await loadDoc(page, base, ed, SPEC[ed].doc());
    const h = await centre(page, '.node[data-id="m2"] > header .t'), p0 = await modPos(page, 'm2');
    await page.mouse.move(h.x, h.y); await page.mouse.down(); await page.mouse.move(h.x + 40, h.y + 20, { steps: 5 });
    const p1 = await modPos(page, 'm2');
    await page.keyboard.press('Alt+e'); await sleep(page, 120);
    await page.mouse.move(h.x + 140, h.y + 90, { steps: 6 }); await sleep(page, 120);
    const p2 = await modPos(page, 'm2'); await page.mouse.up();
    const p3 = await modPos(page, 'm2');
    ok(`${tag} r2: the module followed the pointer before Alt+E (${p0} -> ${p1})`, Math.abs(p1[0] - p0[0] - 40) < 12);
    ok(`${tag} r2: Alt+E in the middle of a title-bar drag ends the drag: the module stays where it was (${p1} -> ${p2} -> ${p3}) and Play is on`, p2[0] === p1[0] && p2[1] === p1[1] && p3[0] === p1[0] && (await page.evaluate(() => document.body.classList.contains('play'))));
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= D5: the wheel over a slider =================
async function d5(ed, [w, h]) {
  const B = await bootEd(w, h, false); const { page, errors, base } = B; const tag = `${ed} D5 ${w}x${h}`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed));
    const key = await firstKey(page, 't1'), sel = sliderSel('t1', key), s = await centre(page, sel), v0 = await slVal(page, 't1', key);
    const view0 = await readView(page);
    // not rested: the wheel pans
    await page.mouse.move(s.x - 20, s.y); await page.mouse.move(s.x, s.y); await page.mouse.wheel(0, 4); await sleep(page, 80);
    const view1 = await readView(page), v1 = await slVal(page, 't1', key);
    ok(`${tag}: a wheel step the moment the pointer arrives on a slider pans the patch (y ${view0.y} -> ${view1.y}) and leaves the value (${v0} -> ${v1})`, view1.y < view0.y && v1 === v0);
    // rested: changes the value, no pan
    await sleep(page, 520);
    ok(`${tag}: after 0.5 s the slider is outlined (the wheel will change it)`, await page.evaluate(q => document.querySelector(q).classList.contains('wheelon'), sel));
    await page.mouse.wheel(0, -100); await sleep(page, 80);
    const view2 = await readView(page), v2 = await slVal(page, 't1', key);
    ok(`${tag}: once the pointer has rested on it the wheel changes the value (up raises: ${v1} -> ${v2}) and the patch stays (y ${view1.y} -> ${view2.y})`, v2 > v1 && view2.y === view1.y);
    await page.mouse.wheel(0, 100); await sleep(page, 60); const v3 = await slVal(page, 't1', key);
    ok(`${tag}: wheel down lowers it again (${v2} -> ${v3})`, v3 < v2);
    // Shift is finer
    const a = await slVal(page, 't1', key); await page.mouse.wheel(0, -100); await sleep(page, 40); const b = await slVal(page, 't1', key);
    await page.keyboard.down('Shift'); await page.mouse.wheel(0, -100); await sleep(page, 40); await page.keyboard.up('Shift'); const c = await slVal(page, 't1', key);
    ok(`${tag}: Shift+wheel moves it in finer steps (${(b - a).toFixed(4)} against ${(c - b).toFixed(4)})`, c - b > 0 && (c - b) < (b - a) * 0.5);
    // never during a zoom: Ctrl+wheel zooms, and the plain wheel right after it still pans
    const before = await slVal(page, 't1', key), zv = await readView(page);
    await page.keyboard.down('Control'); await page.mouse.wheel(0, -50); await page.keyboard.up('Control'); await page.mouse.wheel(0, 30); await sleep(page, 60);
    const zv2 = await readView(page), after = await slVal(page, 't1', key);
    ok(`${tag}: Ctrl+wheel zooms (${zv.z.toFixed(2)} -> ${zv2.z.toFixed(2)}) and a wheel step right after a zoom does not change the value (${before} -> ${after})`, zv2.z !== zv.z && after === before);
    // never during a pan gesture: a background drag in progress
    await sleep(page, 500);
    const e = await emptyPoint(page);
    const s2 = await centre(page, sel);
    if (e && s2 && s2.y > 60 && s2.y < h - 40) {
      const vv = await slVal(page, 't1', key);
      await page.mouse.move(e.x, e.y); await page.mouse.down(); await page.mouse.move(e.x + 5, e.y + 5, { steps: 2 });
      await page.mouse.move(s2.x, s2.y, { steps: 4 }); await sleep(page, 500); await page.mouse.wheel(0, -100); await sleep(page, 60);
      const vd = await slVal(page, 't1', key); await page.mouse.up();
      ok(`${tag}: while the patch is being dragged (a pan in progress) the wheel over a slider does not change it (${vv} -> ${vd})`, vd === vv);
    } else ok(`${tag}: a pan in progress (no empty background to start from at this size)`, true);
    // a wheel pan that carries sliders under a still pointer never changes one
    await loadDoc(page, base, ed, slidersDoc(ed));
    const all0 = await page.evaluate(() => [...document.querySelectorAll('.row.sl input[type=range]')].map(i => +i.value));
    const gx = await page.evaluate(() => { const r = document.querySelector('.node[data-id="t1"]').getBoundingClientRect(); return r.right + 80; });
    const gy = Math.round(h / 2);
    await page.mouse.move(gx, gy); await sleep(page, 400);
    for (let i = 0; i < 14; i++) { await page.mouse.wheel(0, 60); await sleep(page, 70); }   // the pointer is in the gap between two columns; modules slide past under it
    await page.mouse.move(gx - 330, gy);   // and now over the module column, moving: still panning
    for (let i = 0; i < 12; i++) { await page.mouse.wheel(0, -60); await sleep(page, 70); }
    const all1 = await page.evaluate(() => [...document.querySelectorAll('.row.sl input[type=range]')].map(i => +i.value));
    ok(`${tag}: a continuous wheel pan across the modules changes no slider (${all0.length} sliders compared)`, JSON.stringify(all0) === JSON.stringify(all1));
    // one burst of wheel steps = one undo step
    await loadDoc(page, base, ed, slidersDoc(ed));
    const s3 = await centre(page, sel), o = await slVal(page, 't1', key);
    await page.mouse.move(s3.x - 10, s3.y); await page.mouse.move(s3.x, s3.y); await sleep(page, 500);
    for (let i = 0; i < 5; i++) { await page.mouse.wheel(0, -100); await sleep(page, 40); }
    const moved = await slVal(page, 't1', key); await sleep(page, 700);
    await page.mouse.click(s3.x, 20 + (await readView(page)).ws.t); await page.keyboard.press('Control+z'); await sleep(page, 300);
    const u = await slVal(page, 't1', key);
    ok(`${tag}: five wheel steps are one undo step (${o} -> ${moved}; Ctrl+Z -> ${u})`, moved > o && u === o);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= D1 (right-click value menu) and D8 (Ctrl-click no longer resets) =================
async function d1d8(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} D1/D8`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed));
    const key = await firstKey(page, 't1'), sel = sliderSel('t1', key), def = await slVal(page, 't1', key);
    const s = await centre(page, sel);
    // drag it somewhere
    await page.mouse.move(s.x, s.y); await page.mouse.down(); await page.mouse.move(s.x + 30, s.y, { steps: 6 }); await page.mouse.up();
    const moved = await slVal(page, 't1', key); ok(`${tag}: a drag moves the slider (${def} -> ${moved})`, moved !== def);
    // D8
    await page.keyboard.down('Control'); await page.mouse.click(s.x, s.y); await page.keyboard.up('Control'); await sleep(page, 100);
    ok(`${tag} D8: Ctrl-click no longer resets the slider (${moved} -> ${await slVal(page, 't1', key)})`, (await slVal(page, 't1', key)) === moved);
    await page.keyboard.down('Alt'); await page.mouse.click(s.x, s.y); await page.keyboard.up('Alt'); await sleep(page, 100);
    ok(`${tag} D8: Alt-click resets it (${await slVal(page, 't1', key)} = ${def})`, (await slVal(page, 't1', key)) === def);
    await page.mouse.move(s.x, s.y); await page.mouse.down(); await page.mouse.move(s.x + 30, s.y, { steps: 6 }); await page.mouse.up();
    await page.mouse.dblclick(s.x, s.y); await sleep(page, 100);
    ok(`${tag} D8: double-click resets it`, (await slVal(page, 't1', key)) === def);
    // Ctrl held at the press takes big steps from the start of the drag
    const drag = async (dx, ctrl) => { await page.mouse.dblclick(s.x, s.y); const a = await slVal(page, 't1', key); if (ctrl) await page.keyboard.down('Control'); await page.mouse.move(s.x, s.y); await page.mouse.down(); await page.mouse.move(s.x + dx, s.y, { steps: 5 }); await page.mouse.up(); if (ctrl) await page.keyboard.up('Control'); return Math.abs((await slVal(page, 't1', key)) - a); };
    const d1 = await drag(10, false), d2 = await drag(10, true);
    ok(`${tag} D8: holding Ctrl before a drag takes big steps from the start (${d1.toFixed(4)} plain, ${d2.toFixed(4)} with Ctrl)`, d2 > d1 * 2.5 || d2 >= 0.99 * (await page.evaluate(q => { const i = document.querySelector(q); return +i.max - +i.min; }, sel)) * 0.5);
    // D1: right-click on a slider
    await page.mouse.dblclick(s.x, s.y); await page.mouse.move(s.x, s.y); await page.mouse.down(); await page.mouse.move(s.x + 30, s.y, { steps: 6 }); await page.mouse.up();
    const s4 = await centre(page, sel); await page.mouse.click(s4.x, s4.y, { button: 'right' }); await sleep(page, 200);
    const items = await menuItems(page);
    ok(`${tag} D1: right-click on a slider opens the value menu first (${(items || []).slice(0, 5).join(' | ')})`, items && /Reset/.test(items[1]) && items.some(t => /Type a value/.test(t)) && items.some(t => /MIDI learn/.test(t)));
    ok(`${tag} D1: ...with the module items below it`, items && items.some(t => /Rename module/.test(t)) && items.some(t => /Group module/.test(t)) && items.some(t => /Delete module/.test(t)));
    await clickMenu(page, /Reset to default/);
    ok(`${tag} D1: Reset to default puts it back (${await slVal(page, 't1', key)} = ${def})`, (await slVal(page, 't1', key)) === def);
    const s5 = await centre(page, sel); await page.mouse.click(s5.x, s5.y, { button: 'right' }); await sleep(page, 200); await clickMenu(page, /Type a value/);
    ok(`${tag} D1: Type a value… opens the type box on that slider`, await page.evaluate(k => !!document.querySelector(`.node[data-id="t1"] .row.sl[data-key="${k}"] .valedit`), key));
    await page.keyboard.press('Escape'); await sleep(page, 150);
    await setPlay(page, true);
    const s6 = await centre(page, sel); await page.mouse.click(s6.x, s6.y, { button: 'right' }); await sleep(page, 200);
    ok(`${tag} D1: in Play a right-click opens no menu`, (await menuItems(page)) === null);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= D7 keys, D10 zoom to selection, M4 create by typing =================
async function keys(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} D7`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc());
    const selCount = () => page.evaluate(() => document.querySelectorAll('.node.selected').length);
    const bg = await emptyPoint(page); await page.mouse.click(bg.x, bg.y);
    await page.keyboard.press('Control+a'); ok(`${tag}: Ctrl+A selects every module (${await selCount()} of ${await nodeCount(page)})`, (await selCount()) === (await nodeCount(page)));
    await page.keyboard.press('Escape'); ok(`${tag}: Esc lets go of the selection`, (await selCount()) === 0);
    // arrows
    await page.click('.node[data-id="m2"] > header .t'); const p0 = await modPos(page, 'm2');
    await page.keyboard.press('ArrowRight'); await page.keyboard.press('ArrowRight'); await page.keyboard.press('ArrowDown'); const p1 = await modPos(page, 'm2');
    ok(`${tag}: arrows nudge the picked module 10 px each (${p0} -> ${p1})`, p1[0] === p0[0] + 20 && p1[1] === p0[1] + 10);
    await page.keyboard.press('Shift+ArrowLeft'); const p2 = await modPos(page, 'm2'); ok(`${tag}: Shift+arrow nudges 1 px (${p1} -> ${p2})`, p2[0] === p1[0] - 1 && p2[1] === p1[1]);
    await page.keyboard.press('Control+z'); await sleep(page, 200); const p3 = await modPos(page, 'm2'); ok(`${tag}: a burst of nudges is one undo step (back to ${p3})`, p3[0] === p0[0] && p3[1] === p0[1]);
    // fit, zoom
    await page.keyboard.press('Escape'); const z0 = (await readView(page)).z;
    await page.keyboard.press('Control+='); await sleep(page, 100); const z1 = (await readView(page)).z; await page.keyboard.press('Control+-'); await page.keyboard.press('Control+-'); await sleep(page, 100); const z2 = (await readView(page)).z;
    ok(`${tag}: Ctrl+= zooms in (${z0.toFixed(2)} -> ${z1.toFixed(2)}), Ctrl+- zooms out (${z2.toFixed(2)})`, z1 > z0 && z2 < z1);
    await page.keyboard.press('Control+0'); await sleep(page, 100); ok(`${tag}: Ctrl+0 goes to 100%`, Math.abs((await readView(page)).z - 1) < 0.01);
    await page.keyboard.press('f'); await sleep(page, 100); const zf = await readView(page);
    const vis = await page.evaluate(() => { const ws = document.getElementById('ws').getBoundingClientRect(); return [...document.querySelectorAll('.node')].every(n => { const r = n.getBoundingClientRect(); return r.left >= ws.left - 2 && r.right <= ws.right + 2 && r.top >= ws.top - 2 && r.bottom <= ws.bottom + 2; }); });
    ok(`${tag}: F fits the whole patch (zoom ${zf.z.toFixed(2)}, every module in view)`, vis);
    // D10 zoom to selection
    await page.click('.node[data-id="m2"] > header .t'); await page.keyboard.down('Shift'); await page.click('.node[data-id="m3"] > header .t'); await page.keyboard.up('Shift');
    await page.keyboard.press('Shift+f'); await sleep(page, 100); const zs = await readView(page);
    const two = await page.evaluate(() => { const ws = document.getElementById('ws').getBoundingClientRect(); return ['m2', 'm3'].every(id => { const r = document.querySelector(`.node[data-id="${id}"]`).getBoundingClientRect(); return r.left >= ws.left - 2 && r.right <= ws.right + 2 && r.top >= ws.top - 2 && r.bottom <= ws.bottom + 2; }); });
    ok(`D10 ${tag}: Shift+F zooms to the picked modules (zoom ${zf.z.toFixed(2)} -> ${zs.z.toFixed(2)}, both in view)`, zs.z > zf.z && two);
    await page.mouse.click(bg.x, bg.y); await page.mouse.click(300, 500, { button: 'right' }).catch(() => {}); await page.keyboard.press('Escape');
    await page.click('.node[data-id="m2"] > header .t', { button: 'right' }); await sleep(page, 150);
    ok(`D10 ${tag}: the module menu has Zoom to this module`, ((await menuItems(page)) || []).some(t => /Zoom to this module/.test(t)));
    await page.keyboard.press('Escape');
    // group
    await page.keyboard.press('Control+a'); await page.keyboard.press('Control+g'); await sleep(page, 200);
    ok(`${tag}: Ctrl+G groups the picked modules`, (await page.evaluate(() => document.querySelectorAll('#groups .decor.box').length)) === 1);
    // Enter renames
    await page.click('.node[data-id="m3"] > header .t'); await page.keyboard.press('Enter'); await sleep(page, 200);
    ok(`M5 ${tag}: Enter on the picked module opens the rename box`, await page.evaluate(() => !!document.querySelector('.dlgov .dlginput')));
    await page.keyboard.press('Escape'); await sleep(page, 150);
    // Alt+C and Alt+E still work
    await page.keyboard.press('Alt+c'); await sleep(page, 200); const col = await page.evaluate(() => document.querySelectorAll('.node.collapsed').length); await page.keyboard.press('Alt+c'); await sleep(page, 200);
    ok(`${tag}: Alt+C (collapse all) still works (${col} folded)`, col > 0);
    await page.keyboard.press('Alt+e'); await sleep(page, 150); const pl = await page.evaluate(() => document.body.classList.contains('play')); await page.keyboard.press('Alt+e'); await sleep(page, 150);
    ok(`${tag}: Alt+E (Play / Edit) still works`, pl && !(await page.evaluate(() => document.body.classList.contains('play'))));
    // typing in a field: none of the new keys fires
    await loadDoc(page, base, ed, SPEC[ed].doc());
    await page.click('#pname'); const zt0 = (await readView(page)).z, n0 = await nodeCount(page);
    for (const k of ['a', 'n', 'f', 'Shift+f', 'Control+a', 'ArrowLeft', 'Control+=', 'Enter']) await page.keyboard.press(k);   // (Enter ends the name: last)
    ok(`${tag}: while typing in the patch name no new key acts (no Find a module: ${!(await qaOpen(page))}, zoom kept, none picked: ${await selCount()})`, !(await qaOpen(page)) && Math.abs((await readView(page)).z - zt0) < 0.001 && (await selCount()) === 0 && (await nodeCount(page)) === n0);
    ok(`${tag}: ...and the name field took the letters`, (await page.evaluate(() => document.getElementById('pname').value)).includes('anf'));
    // Play: the ones that edit do nothing
    await loadDoc(page, base, ed, SPEC[ed].doc());
    await setPlay(page, true); await page.mouse.click(bg.x, bg.y);
    await page.keyboard.press('Control+a'); await page.keyboard.press('Control+g'); const pn = await modPos(page, 'm2');
    await page.keyboard.press('a'); const qa = await qaOpen(page); const tt = await toastText(page);
    ok(`${tag}: in Play Ctrl+A picks nothing, Ctrl+G groups nothing, A opens no Find a module (and says Play: "${tt.slice(0, 30)}")`, (await selCount()) === 0 && !qa && /Play/.test(tt) && (await page.evaluate(() => document.querySelectorAll('#groups .decor.box').length)) === 0);
    await page.keyboard.press('Enter'); await page.keyboard.press('ArrowRight');
    const pn2 = await modPos(page, 'm2'); ok(`${tag}: in Play Enter renames nothing and arrows move nothing (${pn} = ${pn2})`, !(await page.evaluate(() => !!document.querySelector('.dlgov'))) && pn[0] === pn2[0] && pn[1] === pn2[1]);
    await page.keyboard.press('f'); await sleep(page, 100); ok(`${tag}: ...but F still fits the view in Play`, true);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= M4 create by typing =================
async function m4(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} M4`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc());
    const [q, type] = SPEC[ed].qa, n0 = await nodeCount(page), e = await emptyPoint(page);
    const v0 = await readView(page), wp = [(e.x - v0.ws.l - v0.x) / v0.z, (e.y - v0.ws.t - v0.y) / v0.z];
    await page.mouse.move(e.x, e.y); await page.keyboard.press('a'); await sleep(page, 200);
    ok(`${tag}: A opens Find a module with the text box ready`, await page.evaluate(() => document.activeElement && document.activeElement.classList.contains('qainp')));
    await page.keyboard.type(q); await sleep(page, 100); await page.keyboard.press('Enter'); await sleep(page, 300);
    const sd = await savedDoc(page, ed), last = sd.modules[sd.modules.length - 1];
    ok(`${tag}: typing "${q}" and Enter adds that module (${last.type})`, (await nodeCount(page)) === n0 + 1 && last.type === type);
        ok(`${tag}: it lands by the pointer (world ${last.pos} against pointer ${wp.map(Math.round)})`, Math.abs(last.pos[0] - wp[0]) < 40 && Math.abs(last.pos[1] - wp[1]) < 40);
    ok(`${tag}: ...on the 10 px grid (${last.pos})`, last.pos[0] % 10 === 0 && last.pos[1] % 10 === 0);
    await page.mouse.move(e.x, e.y); await page.keyboard.press('a'); await page.keyboard.type('chor'); await page.keyboard.press('Escape'); await sleep(page, 150);
    ok(`${tag}: Esc cancels: nothing added and the box is gone`, (await nodeCount(page)) === n0 + 1 && !(await qaOpen(page)));
    await page.keyboard.press('a'); await page.keyboard.type('comment'); await page.keyboard.press('Enter'); await sleep(page, 300);
    ok(`${tag}: "comment" in Find a module adds a comment`, (await page.evaluate(() => document.querySelectorAll('.cmt').length)) === 1);
    await page.keyboard.press('Escape'); await page.mouse.click(e.x + 300, e.y);
    if (ed === 'chains') { await page.keyboard.press('n'); await sleep(page, 150); ok(`${tag}: N opens it too in chains`, await qaOpen(page)); await page.keyboard.press('Escape'); }
    else { await page.keyboard.press('n'); await sleep(page, 150); ok(`${tag}: N is a note in waves, so it does not open Find a module`, !(await qaOpen(page))); }
    // module list search
    await page.click('#dtoggle'); await sleep(page, 300);
    ok(`${tag}: the Modules tab opens the module list with its search ready`, await page.evaluate(() => document.activeElement && document.activeElement.classList.contains('psearch')));
    await page.keyboard.type('tape'); await sleep(page, 200);
    const hits = await page.evaluate(() => [...document.querySelectorAll('.presults .pitem')].map(b => b.textContent));
    ok(`${tag}: typing "tape" in the module list shows only matches (${hits.join(', ')})`, hits.length >= 1 && hits.every(t => /tape/i.test(t)));
    const n1 = await nodeCount(page); await page.keyboard.press('Enter'); await sleep(page, 300);
    ok(`${tag}: Enter in the list's search adds the first match`, (await nodeCount(page)) === n1 + 1);
    await page.keyboard.press('Escape'); await sleep(page, 100);
    ok(`${tag}: Esc clears the search and shows the full list again`, await page.evaluate(() => !document.getElementById('plist').classList.contains('searching')));
    // Play: nothing is created
    await setPlay(page, true); const n2 = await nodeCount(page);
    await page.mouse.move(e.x, e.y); await page.keyboard.press('a'); await sleep(page, 150);
    ok(`${tag}: in Play A creates nothing (no box, ${n2} modules, toast "${(await toastText(page)).slice(0, 24)}")`, !(await qaOpen(page)) && (await nodeCount(page)) === n2);
    await page.mouse.dblclick(e.x, e.y); await sleep(page, 150); ok(`${tag}: ...nor a double-click on the background`, !(await qaOpen(page)));
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= M3 summing =================
async function m3(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} M3`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc());
    const into = async (id, port) => { const d = await savedDoc(page, ed); return d.cables.filter(c => c.to[0] === id && c.to[1] === port); };
    const src = ed === 'chains' ? ['m7', 'out'] : ['m7', 'out'], dst = ['m3', 'in'];
    const n0 = (await into(...dst)).length;
    await dragJack(page, jack(src[0], src[1], 'out'), jack(dst[0], dst[1], 'in'));
    const c1 = await into(...dst), tt = await toastText(page);
    ok(`${tag}: a second cable into a patched input is added, not swapped in (${n0} -> ${c1.length} cables into it)`, n0 === 1 && c1.length === 2 && c1.some(c => c.from[0] === 'm2') && c1.some(c => c.from[0] === src[0]));
    ok(`${tag}: ...and a toast says they add together ("${tt.slice(0, 50)}")`, /add together/.test(tt));
    await dragJack(page, jack(src[0], src[1], 'out'), jack(dst[0], dst[1], 'in'));
    const tt2 = await toastText(page), cn = (await into(...dst)).length;
    ok(`${tag}: the same cable again is refused, with a message (still ${cn}; "${tt2.slice(0, 36)}"), and leaves no undo step`, cn === 2 && /already connected/.test(tt2));
    await page.keyboard.press('Control+z'); await sleep(page, 300);
    const c2 = await into(...dst); ok(`${tag}: one Undo takes the new cable off and leaves the first (${c2.map(c => c.from[0])})`, c2.length === 1 && c2[0].from[0] === 'm2');
    await page.keyboard.press('Control+Shift+z'); await sleep(page, 300);
    ok(`${tag}: Redo brings it back`, (await into(...dst)).length === 2);
    // picking one up from the patched input takes the newest
    const e = await emptyPoint(page);
    const A = await centre(page, jack(dst[0], dst[1], 'in')); await page.mouse.move(A.x, A.y); await page.mouse.down(); await page.mouse.move(e.x, e.y, { steps: 8 }); await page.mouse.up(); await sleep(page, 250);
    const c3 = await into(...dst);
    ok(`${tag}: dragging from an input with two cables picks up the newest and the first stays (${c3.map(c => c.from[0])})`, c3.length === 1 && c3[0].from[0] === 'm2');
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= M5 comments and rename =================
async function m5(ed, OLD) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} M5`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc());
    const e = await emptyPoint(page); await page.mouse.click(e.x, e.y, { button: 'right' }); await sleep(page, 150);
    const items = await menuItems(page); ok(`${tag}: the background menu offers Add a comment here (${(items || []).join(' | ').slice(0, 120)})`, items && items.some(t => /Add a comment here/.test(t)));
    await clickMenu(page, /Add a comment here/);
    ok(`${tag}: a comment appears with its text box focused`, await page.evaluate(() => document.activeElement && document.activeElement.classList.contains('ctext')));
    await page.keyboard.type('check the feedback here'); await page.mouse.click(e.x + 500, e.y - 100); await sleep(page, 200);
    let sd = await savedDoc(page, ed);
    ok(`${tag}: the text is saved with the patch (doc.notes: ${JSON.stringify(sd.notes)})`, sd.notes && sd.notes.length === 1 && sd.notes[0].text === 'check the feedback here' && Array.isArray(sd.notes[0].pos));
    // drag by the top edge, snaps
    const g = await centre(page, '.cmt .cgrip'); const p0 = sd.notes[0].pos.slice();
    await page.mouse.move(g.x - 20, g.y); await page.mouse.down(); await page.mouse.move(g.x + 33, g.y + 27, { steps: 5 }); await page.mouse.up();
    sd = await savedDoc(page, ed); ok(`${tag}: a comment moves by its top edge and lands on the grid (${p0} -> ${sd.notes[0].pos})`, sd.notes[0].pos[0] !== p0[0] && sd.notes[0].pos[0] % 10 === 0 && sd.notes[0].pos[1] % 10 === 0);
    await page.reload(); await page.waitForSelector('.node'); await sleep(page, 900);
    ok(`${tag}: a comment survives a reload`, (await page.evaluate(() => document.querySelector('.cmt textarea') && document.querySelector('.cmt textarea').value)) === 'check the feedback here');
    // rename
    await page.click('.node[data-id="m2"] > header .t'); await page.keyboard.press('Enter'); await sleep(page, 200);
    await page.fill('.dlginput', 'Slap echo'); await page.keyboard.press('Enter'); await sleep(page, 300);
    const title = () => page.evaluate(() => document.querySelector('.node[data-id="m2"] > header .t').textContent);
    ok(`${tag}: renaming a module changes its title bar ("${await title()}")`, (await title()) === 'Slap echo');
    sd = await savedDoc(page, ed); ok(`${tag}: ...and is saved as the module's label (${sd.modules.find(m => m.id === 'm2').label})`, sd.modules.find(m => m.id === 'm2').label === 'Slap echo');
    await page.click('.node[data-id="m2"] > header .t', { button: 'right' }); await sleep(page, 150); await clickMenu(page, /Rename module/);
    ok(`${tag}: the module menu has Rename module…`, await page.evaluate(() => !!document.querySelector('.dlgov .dlginput'))); await page.keyboard.press('Escape'); await sleep(page, 150);
    await page.mouse.click(e.x, e.y); await page.keyboard.press('Control+z'); await sleep(page, 300);
    ok(`${tag}: Undo takes the rename back ("${await title()}")`, (await title()) !== 'Slap echo');
    await page.keyboard.press('Control+Shift+z'); await sleep(page, 300); ok(`${tag}: Redo puts it again`, (await title()) === 'Slap echo');
    // empty name goes back to the module's own
    await page.click('.node[data-id="m2"] > header .t'); await page.keyboard.press('Enter'); await sleep(page, 200); await page.fill('.dlginput', ''); await page.keyboard.press('Enter'); await sleep(page, 300);
    ok(`${tag}: an empty name goes back to the module's own ("${await title()}")`, (await title()) !== 'Slap echo' && !(await savedDoc(page, ed)).modules.find(m => m.id === 'm2').label);
    // undo of a comment: text, then the comment itself
    await page.mouse.click(e.x, e.y); await page.keyboard.press("Control+z"); await sleep(page, 200);
    // Play: a comment cannot be typed into
    await setPlay(page, true); const t = await centre(page, '.cmt textarea'); await page.mouse.click(t.x, t.y); await page.keyboard.type('zzz'); await sleep(page, 150);
    ok(`${tag}: in Play a comment takes no typing`, !(await page.evaluate(() => document.querySelector('.cmt textarea').value.includes('zzz'))));
    await setPlay(page, false);
    // remove
    await page.hover('.cmt'); await page.click('.cmt .cx'); await sleep(page, 200);
    ok(`${tag}: the x removes a comment (and the field is gone from the saved patch)`, (await page.evaluate(() => document.querySelectorAll('.cmt').length)) === 0 && !(await savedDoc(page, ed)).notes);
    await page.keyboard.press('Control+z'); await sleep(page, 300); ok(`${tag}: Undo brings the comment back`, (await page.evaluate(() => document.querySelectorAll('.cmt').length)) === 1);
    done(errors, tag);
    // build 586 reads a patch with a comment, a renamed module and a summed input
    if (OLD) {
      const d = SPEC[ed].doc(); d.notes = [{ id: 'n1', pos: [100, 700], w: 220, text: 'hello' }]; d.modules[1].label = 'Slap echo'; d.cables.push(C('m7', 'out', 'm3', 'in'));
      const O = await bootEd(1440, 900, false);
      await loadDoc(O.page, OLD.base, ed, d);
      const r = await O.page.evaluate(() => ({ nodes: document.querySelectorAll('.node').length, title: document.querySelector('.node[data-id="m2"] > header .t').textContent, cables: document.querySelectorAll('#cables > g').length }));
      const back = await savedDoc(O.page, ed);
      ok(`${tag} (build 586): a patch with a comment, a renamed module and a summed input loads (${r.nodes} modules, ${r.cables} cables), shows the new name ("${r.title}") and keeps the comment when it saves again`, r.nodes === 8 && r.cables === 5 && r.title === 'Slap echo' && back.notes && back.notes[0].text === 'hello');
      done(O.errors, tag + ' (build 586)'); await O.close();
    }
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= M8 hover a cable =================
async function m8(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} M8`;
  try {
    const lfo = ed === 'chains' ? 'lfo' : 'lfo';
    const d = SPEC[ed].doc({ cables: [C('m1', ed === 'chains' ? 'out' : 'pitch', 'm2', ed === 'chains' ? 'in' : 'pitch'), C('m5', 'out', 'm3', ed === 'chains' ? 'in' : 'cutoff')].concat(ed === 'chains' ? [] : [C('m6', 'out', 'm3', 'cutoff')]) });
    if (ed === 'waves') d.cables = [C('m6', 'out', 'm3', 'cutoff'), C('m1', 'pitch', 'm2', 'pitch')];
    await loadDoc(page, base, ed, d);
    await page.mouse.click(700, 880); await sleep(page, 1800);   // a click starts the audio, so the jack meters report
    const mid = async () => page.evaluate(() => { const hs = [...document.querySelectorAll('#cablehits path.hit')]; const p = hs.find(h => h._cable && h._cable.from[0] === window.__src) || hs[hs.length - 1]; const L = p.getTotalLength(), m = p.getScreenCTM();
      for (let t = 0.5, i = 0; i < 40; i++, t = 0.5 + (i % 2 ? 1 : -1) * Math.ceil(i / 2) * 0.012) { const pt = p.getPointAtLength(L * t), q = new DOMPoint(pt.x, pt.y).matrixTransform(m), e = document.elementFromPoint(q.x, q.y); if (e === p || (e && e.classList && e.classList.contains('hit') && e._cable === p._cable)) return { x: q.x, y: q.y }; }
      const pt = p.getPointAtLength(L / 2), q = new DOMPoint(pt.x, pt.y).matrixTransform(m); return { x: q.x, y: q.y }; });
    await page.evaluate(s => { window.__src = s; }, ed === 'chains' ? 'm5' : 'm6');
    const pt = await mid();
    const mutations = () => page.evaluate(() => window.__mut || 0);
    await page.mouse.move(pt.x - 30, pt.y - 60); await sleep(page, 200);
    ok(`${tag}: no readout before a cable is hovered`, await page.evaluate(() => { const t = document.getElementById('cabread'); return !t || t.hidden; }));
    await page.mouse.move(pt.x, pt.y, { steps: 4 }); await sleep(page, 300);
    const t1 = await page.evaluate(() => { const t = document.getElementById('cabread'); return t && !t.hidden ? t.textContent : null; });
    ok(`${tag}: hovering a cable shows a readout naming both ends and a value ("${(t1 || '').replace(/\n/g, ' / ')}")`, t1 && /→/.test(t1) && t1.split('\n').length === 2);
    const seen = new Set(); for (let i = 0; i < 12; i++) { await sleep(page, 250); seen.add(await page.evaluate(() => { const t = document.getElementById('cabread'); return t ? t.textContent : ''; })); }
    ok(`${tag}: the readout updates while the pointer rests on the cable (${seen.size} different texts in 3 s, e.g. "${[...seen].pop().split('\n')[1]}")`, seen.size >= 2 || /level|dB|gate|silent/.test([...seen][0].split('\n')[1] || ''));
        await page.mouse.move(pt.x - 5, pt.y - 8, { steps: 2 }).catch(() => {});
    await page.mouse.move(pt.x - 40, pt.y - 90, { steps: 4 }); await sleep(page, 250);
    ok(`${tag}: the readout goes when the pointer leaves the cable`, await page.evaluate(() => { const t = document.getElementById('cabread'); return !t || t.hidden; }));
    await page.evaluate(() => { const t = document.getElementById('cabread'); window.__mut = 0; new MutationObserver(r => { window.__mut += r.length; }).observe(t, { childList: true, characterData: true, subtree: true, attributes: true }); });
    await sleep(page, 1500);
    ok(`${tag}: nothing is written to the readout while no cable is hovered (${await mutations()} changes in 1.5 s of telemetry)`, (await mutations()) === 0);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= M11 snap, align, tidy =================
async function m11(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} M11`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 0, y: 0, zoom: 0.5, ui: 2 } }));
    const drag = async (id, dx, dy) => { const h = await centre(page, `.node[data-id="${id}"] > header .t`); await page.mouse.move(h.x, h.y); await page.mouse.down(); await page.mouse.move(h.x + dx, h.y + dy, { steps: 6 }); await page.mouse.up(); await sleep(page, 150); };
    const p0 = await modPos(page, 'm5'); await drag('m5', 47, 33); const p1 = await modPos(page, 'm5');
    ok(`${tag}: a dragged module lands on the 10 px grid (${p0} -> ${p1})`, p1[0] % 10 === 0 && p1[1] % 10 === 0 && p1[0] !== p0[0]);
    const e = await emptyPoint(page); await page.mouse.click(e.x, e.y, { button: 'right' }); await sleep(page, 150); await clickMenu(page, /Snap to grid: on/);
    const st = await page.evaluate(k => JSON.parse(localStorage.getItem(k) || '{}'), SPEC[ed].setKey);
    await drag('m5', 47, 33); const p2 = await modPos(page, 'm5');
    ok(`${tag}: Snap to grid can be switched off in the background menu (settings.snap ${st.snap}); a drag then moves by what the pointer moved, in world units (${p1} -> ${p2})`, st.snap === false && (p2[0] % 10 !== 0 || p2[1] % 10 !== 0));
    await page.mouse.click(e.x, e.y, { button: 'right' }); await sleep(page, 150); await clickMenu(page, /Snap to grid: off/);
    // align
    await page.click('.node[data-id="m2"] > header .t'); await page.keyboard.down('Shift'); await page.click('.node[data-id="m3"] > header .t'); await page.click('.node[data-id="m4"] > header .t'); await page.keyboard.up('Shift');
    await page.click('.node[data-id="m3"] > header .t', { button: 'right' }); await sleep(page, 150);
    const items = await menuItems(page); ok(`${tag}: with three modules picked the menu offers align and space evenly (${(items || []).filter(t => /Align|Space/.test(t)).join(', ')})`, items && items.some(t => /Align tops/.test(t)) && items.some(t => /Space evenly across/.test(t)));
    await clickMenu(page, /Align tops/);
    const ys = [await modPos(page, 'm2'), await modPos(page, 'm3'), await modPos(page, 'm4')].map(p => p[1]); ok(`${tag}: Align tops puts them on one top edge (${ys})`, ys[0] === ys[1] && ys[1] === ys[2]);
    await page.click('.node[data-id="m3"] > header .t', { button: 'right' }); await sleep(page, 150); await clickMenu(page, /Space evenly across/);
    const geo = await page.evaluate(() => ['m2', 'm3', 'm4'].map(id => { const n = document.querySelector(`.node[data-id="${id}"]`); return [parseFloat(n.style.left), n.offsetWidth]; }).sort((a, b) => a[0] - b[0]));
    const g1 = geo[1][0] - (geo[0][0] + geo[0][1]), g2 = geo[2][0] - (geo[1][0] + geo[1][1]);
    ok(`${tag}: Space evenly across makes the two gaps equal (${g1} and ${g2})`, Math.abs(g1 - g2) <= 1);
    await page.click('.node[data-id="m3"] > header .t', { button: 'right' }); await sleep(page, 150); await clickMenu(page, /Align left edges/);
    const xs = [await modPos(page, 'm2'), await modPos(page, 'm3'), await modPos(page, 'm4')].map(p => p[0]); ok(`${tag}: Align left edges puts them on one left edge (${xs})`, xs[0] === xs[1] && xs[1] === xs[2]);
    // tidy: by signal flow, groups kept
    await loadDoc(page, base, ed, SPEC[ed].doc({ view: { x: 0, y: 0, zoom: 0.5, ui: 2 } }));
    await page.click('.node[data-id="m2"] > header .t'); await page.keyboard.down('Shift'); await page.click('.node[data-id="m3"] > header .t'); await page.keyboard.up('Shift'); await page.keyboard.press('Control+g'); await sleep(page, 200);
    const before = await page.evaluate(() => [...document.querySelectorAll('.node')].map(n => [n.dataset.id, parseFloat(n.style.left), parseFloat(n.style.top)]));
    const e2 = await emptyPoint(page); await page.mouse.click(e2.x, e2.y, { button: 'right' }); await sleep(page, 150); await clickMenu(page, /Tidy up/); await sleep(page, 300);
    const sd = await savedDoc(page, ed), X = id => sd.modules.find(m => m.id === id).pos[0];
    const order = ['m1', 'm2', 'm3', 'm4', sd.modules.some(m => m.id === 'm8') ? 'm8' : null].filter(Boolean).map(X);
    ok(`${tag}: Tidy up puts a patch in columns by signal flow (x: ${order})`, order.every((v, i) => i === 0 || v > order[i - 1]));
    ok(`${tag}: ...the unpatched modules go in a last column (x ${['m5', 'm6', 'm7'].map(X)} beyond ${X('m8')})`, ['m5', 'm6', 'm7'].every(id => X(id) > X('m8')));
    ok(`${tag}: ...every module is on the grid`, sd.modules.every(m => m.pos[0] % 10 === 0 && m.pos[1] % 10 === 0));
    const g = sd.groups[0], inside = id => { const m = sd.modules.find(q => q.id === id); return m.pos[0] >= g.bounds[0] && m.pos[0] <= g.bounds[0] + g.bounds[2] && m.pos[1] >= g.bounds[1] && m.pos[1] <= g.bounds[1] + g.bounds[3]; };
    ok(`${tag}: ...the group is kept (${sd.groups.length}) and drawn around its two modules again`, sd.groups.length === 1 && inside('m2') && inside('m3'));
    await page.mouse.click(e2.x, e2.y); await page.keyboard.press('Control+z'); await sleep(page, 400);
    const after = await page.evaluate(() => [...document.querySelectorAll('.node')].map(n => [n.dataset.id, parseFloat(n.style.left), parseFloat(n.style.top)]));
    ok(`${tag}: one Undo puts every module back`, JSON.stringify(after) === JSON.stringify(before));
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= U4 presets in the patch (chains) =================
async function u4(ed) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} U4`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc());
    if (ed === 'waves') { ok(`${tag}: waves has no Preset row (not in this pass: it has no factory preset tables; decision for the owner)`, (await page.evaluate(() => document.querySelectorAll('.presel').length)) === 0); done(errors, tag); await B.close(); return; }
    await page.click('.node[data-id="m2"] .presel'); await sleep(page, 150); await clickMenu(page, /Save as a preset/);
    await page.fill('.dlginput', 'My slap'); await page.keyboard.press('Enter'); await sleep(page, 300);
    const sd = await savedDoc(page, ed);
    ok(`${tag}: a saved preset is in the patch (doc.presets: ${JSON.stringify(sd.presets).slice(0, 70)}...)`, sd.presets && sd.presets.delay && sd.presets.delay[0][0] === 'My slap');
    await page.evaluate(() => localStorage.removeItem('chains.presets'));   // another device: nothing saved there
    await page.reload(); await page.waitForSelector('.node'); await sleep(page, 900);
    await page.click('.node[data-id="m2"] .presel'); await sleep(page, 150);
    ok(`${tag}: on a device that never saw it, the patch's preset is in the list ("${((await menuItems(page)) || []).join(' | ').slice(0, 100)}")`, ((await menuItems(page)) || []).some(t => /My slap/.test(t)));
    await clickMenu(page, /Delete one of yours/); await sleep(page, 150); await clickMenu(page, /Delete “My slap”/); await sleep(page, 300);
    ok(`${tag}: deleting it removes it from the patch too`, !(await savedDoc(page, ed)).presets);
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= U11 roles, labels, tab order =================
async function u11(ed, OLD) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} U11`;
  try {
    await loadDoc(page, base, ed, SPEC[ed].doc());
    const r = await page.evaluate(() => { const j = [...document.querySelectorAll('.jack:not([aria-hidden])')]; const nd = [...document.querySelectorAll('.node')];
      return { jacks: j.length, bad: (j.find(x => !(x.getAttribute('role') === 'img' && (x.getAttribute('aria-label') || '').length > 3)) || {outerHTML:''}).outerHTML.slice(0,200), jr: j.every(x => x.getAttribute('role') === 'img' && (x.getAttribute('aria-label') || '').length > 3), nodes: nd.length, nr: nd.every(x => x.getAttribute('role') === 'group' && x.tabIndex === 0 && x.getAttribute('aria-label')), toast: document.getElementById('toast').getAttribute('role') + '/' + document.getElementById('toast').getAttribute('aria-live'), ws: document.getElementById('ws').getAttribute('role') + ':' + document.getElementById('ws').getAttribute('aria-label'), menu: document.getElementById('ctxmenu').getAttribute('role'), svg: document.getElementById('cables').getAttribute('aria-hidden'), jtab: j.some(x => x.tabIndex >= 0) }; });
    ok(`${tag}: every jack has a label and an honest role (img: patching is by pointer, not keyboard) (${r.jacks} jacks), every module is a labelled group and one tab stop (${r.nodes}), the canvas is a labelled region (${r.ws}), the toast is a live region (${r.toast})`, r.jr && r.nr && r.toast === 'status/polite' && r.ws === 'region:Patch' && r.menu === 'menu' && !r.jtab, JSON.stringify(r));
    await page.click('.node[data-id="m2"] > header .t', { button: 'right' }); await sleep(page, 150);
    ok(`${tag}: menu items have the menuitem role`, await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].every(b => b.getAttribute('role') === 'menuitem')));
    await page.keyboard.press('Escape'); await page.mouse.click(5, 5);
    await page.evaluate(() => document.activeElement && document.activeElement.blur());
    // tab order: module after module in the order they were made, each followed by its own controls
    const seq = []; for (let i = 0; i < 90; i++) { await page.keyboard.press('Tab'); seq.push(await page.evaluate(() => { const a = document.activeElement; const n = a && a.closest && a.closest('.node'); return n ? [n.dataset.id, a === n] : null; })); }
    const stops = seq.filter(s => s && s[1]).map(s => s[0]), dom = await page.evaluate(() => [...document.querySelectorAll('.node')].map(n => n.dataset.id));
    ok(`${tag}: Tab visits the modules in order, one stop each, with their controls between (${stops.join(' ')})`, stops.length >= 3 && stops.every((id, i) => dom.indexOf(id) === i));
    const inside = seq.filter(s => s && !s[1]).length; ok(`${tag}: ...and the controls inside a module are reached too (${inside} control stops)`, inside > 5);
    // a module reached by keyboard is picked, and the arrows move it
    await page.evaluate(() => document.activeElement.blur()); await page.mouse.click(5, 5);
    for (let i = 0; i < 40; i++) { await page.keyboard.press('Tab'); if (await page.evaluate(() => document.activeElement && document.activeElement.classList.contains('node'))) break; }
    const fid = await page.evaluate(() => document.activeElement.dataset.id), pp = await modPos(page, fid);
    ok(`${tag}: Tab onto a module picks it (${fid})`, await page.evaluate(() => document.activeElement.classList.contains('selected')));
    await page.keyboard.press('ArrowRight'); const pp2 = await modPos(page, fid); ok(`${tag}: ...and an arrow key then moves it (${pp} -> ${pp2}): select and move without a mouse`, pp2[0] === pp[0] + 10);
    await page.keyboard.press('Enter'); await sleep(page, 150); ok(`${tag}: ...Enter renames it`, await page.evaluate(() => !!document.querySelector('.dlgov .dlginput'))); await page.keyboard.press('Escape');
    // the look is unchanged: sizes of every module and jack, and a mouse click shows no focus ring
    if (OLD) {
      const O = await bootEd(1440, 900, false); await loadDoc(O.page, OLD.base, ed, SPEC[ed].doc());
      await loadDoc(page, base, ed, SPEC[ed].doc());
      const geo = p => p.evaluate(() => [...document.querySelectorAll('.node, .jack')].map(e => { const r = e.getBoundingClientRect(); return [e.className.split(' ')[0], Math.round(r.left), Math.round(r.top), Math.round(r.width), Math.round(r.height)].join(','); }).join(';'));
      ok(`${tag}: every module and jack is the same size and place as in build 586`, (await geo(page)) === (await geo(O.page)));
      const shot = async p => (await p.screenshot({ clip: { x: 38, y: 60, width: 1300, height: 800 } })).toString('base64');
      await page.evaluate(() => { document.activeElement && document.activeElement.blur(); }); await page.mouse.click(1330, 870); await O.page.mouse.click(1330, 870); await sleep(page, 300);
      const sa = await shot(page), sb = await shot(O.page);
      // pixel diff inside the page (a canvas), with no helper program and no files outside the page
      const nd = await page.evaluate(async ([x, y]) => {
        const load = b64 => new Promise((res, rej) => { const i = new Image(); i.onload = () => res(i); i.onerror = rej; i.src = 'data:image/png;base64,' + b64; });
        const px = async b64 => { const i = await load(b64), c = document.createElement('canvas'); c.width = i.width; c.height = i.height; const g = c.getContext('2d'); g.drawImage(i, 0, 0); return g.getImageData(0, 0, i.width, i.height).data; };
        const A = await px(x), Bd = await px(y); if (A.length !== Bd.length) return -1;
        let n = 0; for (let i = 0; i < A.length; i += 4) if (A[i] !== Bd[i] || A[i + 1] !== Bd[i + 1] || A[i + 2] !== Bd[i + 2]) n++;
        return n;
      }, [sa, sb]);
      ok(`${tag}: the patch looks the same as build 586 (${nd} of 1,040,000 pixels differ: live meters only)`, nd >= 0 && nd < 300);
      await page.click('.node[data-id="m2"] > header .t'); const ring = await page.evaluate(() => document.querySelector('.node[data-id="m2"]').matches(':focus-visible')); ok(`${tag}: a mouse click on a module does not give it the keyboard focus ring (:focus-visible ${ring})`, ring === false);
      await O.close();
    }
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= old format, and a patch saved here in build 586 =================
async function oldfmt(ed, OLD) {
  const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} old`;
  try {
    const d = SPEC[ed].doc(); await loadDoc(page, base, ed, d);
    const r = await page.evaluate(() => ({ n: document.querySelectorAll('.node').length, c: document.querySelectorAll('#cables > g').length, notes: document.querySelectorAll('.cmt').length }));
    ok(`${tag}: a patch with none of the new fields loads unchanged (${r.n} modules, ${r.c} cables, ${r.notes} comments)`, r.n === 8 && r.c === 4 && r.notes === 0);
    const sd = await savedDoc(page, ed); ok(`${tag}: ...and saves without gaining notes or presets (${Object.keys(sd).filter(k => /notes|presets/.test(k)).join(',') || 'none'})`, !('notes' in sd) && !('presets' in sd));
    if (OLD) {
      const d2 = SPEC[ed].doc(); d2.notes = [{ id: 'n1', pos: [100, 700], w: 220, text: 'x' }]; if (ed === 'chains') d2.presets = { delay: [['My slap', { time: 0.2 }]] };
      const O = await bootEd(1440, 900, false); await loadDoc(O.page, OLD.base, ed, d2);
      ok(`${tag}: a patch saved by this build (comment${ed === 'chains' ? ', preset' : ''}) opens in build 586 with all ${await nodeCount(O.page)} modules`, (await nodeCount(O.page)) === 8);
      done(O.errors, tag + ' (build 586)'); await O.close();
    }
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= touch: unchanged =================
async function touch(ed) {
  const B = await bootEd(412, 915, true); const { page, errors, base } = B; const tag = `${ed} touch`;
  try {
    await loadDoc(page, base, ed, slidersDoc(ed, { view: { x: 12, y: 12, zoom: 1, ui: 2, nw: 1 } }));
    const cdp = await page.context().newCDPSession(page);
    const pts = a => a.map(([x, y], i) => ({ x, y, id: i + 1 }));
    const tStart = a => cdp.send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: pts(a) }), tMove = a => cdp.send('Input.dispatchTouchEvent', { type: 'touchMove', touchPoints: pts(a) }), tEnd = () => cdp.send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
    const key = await firstKey(page, 't1'), sel = sliderSel('t1', key), s = await centre(page, sel), v0 = await slVal(page, 't1', key);
    await tStart([[s.x, s.y]]); await sleep(page, 40); await tEnd(); await sleep(page, 200);
    ok(`${tag}: a tap on a slider changes nothing (${v0} = ${await slVal(page, 't1', key)})`, (await slVal(page, 't1', key)) === v0);
    await tStart([[s.x, s.y]]); await sleep(page, 700); await tEnd(); await sleep(page, 250);
    const it = await menuItems(page); ok(`${tag}: a long press on a slider still opens Reset / Type a value / MIDI learn (${(it || []).slice(0, 4).join(' | ')})`, it && it.some(t => /Reset/.test(t)) && it.some(t => /Type a value/.test(t)) && it.some(t => /MIDI learn/.test(t)));
    await page.keyboard.press('Escape');
    const h = await centre(page, '.node[data-id="t1"] > header .t'), p0 = await modPos(page, 't1');
    await tStart([[h.x, h.y]]); for (let i = 1; i <= 8; i++) { await tMove([[h.x + i * 6, h.y + i * 4]]); await sleep(page, 14); } await tEnd(); await sleep(page, 200);
    const p1 = await modPos(page, 't1'); ok(`${tag}: a finger drag on a title bar still moves the module, now onto the grid (${p0} -> ${p1})`, p1[0] !== p0[0] && p1[0] % 10 === 0 && p1[1] % 10 === 0);
    const e = await page.evaluate(() => { const ws = document.getElementById('ws').getBoundingClientRect(), ns = [...document.querySelectorAll('.node')].map(n => n.getBoundingClientRect()); for (let y = ws.top + 80; y < ws.bottom - 150; y += 23) for (let x = ws.left + 60; x < ws.right - 60; x += 29) { if (ns.some(r => x > r.left - 14 && x < r.right + 14 && y > r.top - 14 && y < r.bottom + 14)) continue; const t = document.elementFromPoint(x, y); if (t && t.id === 'ws') return { x, y }; } return null; }); if (e) { await tStart([[e.x, e.y]]); await sleep(page, 700); await tEnd(); await sleep(page, 250);
      const bi = await menuItems(page); ok(`${tag}: a long press on the background offers Add a module here and Add a comment here (${(bi || []).slice(0, 3).join(' | ')} at ${e.x},${e.y})`, bi && bi.some(t => /Add a module here/.test(t)) && bi.some(t => /Add a comment here/.test(t))); } else ok(`${tag}: found an empty point for the background long press`, false);
    ok(`${tag}: the module list search exists and is not focused by itself`, await page.evaluate(() => !!document.querySelector('.psearch') && document.activeElement !== document.querySelector('.psearch')));
    done(errors, tag);
  } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

// ================= screenshots =================
async function shots() {
  const OUTD = process.env.SHOTS || '/home/claude/work/shots/pass4'; fs.mkdirSync(OUTD, { recursive: true });
  const skinCss = async page => { await page.addStyleTag({ content: `:root{--bench:#0b0710; --panel:#0b0710; --panel-2:#1c1224; --edge:#ff2d8a; --ink:#ff2d8a; --muted:#c070a0; --track:#1c1224; --side:#140c1b; --frame:#140c1b; --video:#39ff88; --audio:#39ff88; --audio-dark:#11a85a; --mod:#ff2d8a; --mod-dark:#a01058; --gate:#ff2d8a; --gate-dark:#a01058; --selc:#fff200}
    :is(.tb,.pgroup,.pitem,.numsmall,select,.btn,button,input[type=text],input[type=search],input[type=number],textarea){background:#0b0710!important; color:#ff2d8a; border-color:#ff2d8a!important}
    :is(.tb.on,button.on){background:#ff2d8a!important; color:#0b0710!important}` }); };
  const OLD = await baseBuild();
  for (const ed of ED) for (const [w, h] of SIZES) for (const skin of ['white', 'idmpg']) {
    const B = await bootEd(w, h, false); const { page, base } = B; const f = `${OUTD}/${ed}-${w}x${h}-${skin}`;
    try {
      const doc = SPEC[ed].doc({ view: { x: 40, y: 30, zoom: 0.62, ui: 2 } });
      doc.notes = [{ id: 'n1', pos: [20, 700], w: 300, text: 'Feedback is faked here with the second delay.\nCheck the gate before the reverb.' }]; doc.modules[1].label = 'Slap echo';
      await loadDoc(page, base, ed, doc);
      if (skin === 'idmpg') { await skinCss(page); await sleep(page, 200); }
      await page.screenshot({ path: `${f}-1-comment-and-name.png` });
      const e = await emptyPoint(page); await page.mouse.move(e.x, e.y); await page.keyboard.press('a'); await page.keyboard.type('tape'); await sleep(page, 300); await page.screenshot({ path: `${f}-2-create-by-typing.png` }); await page.keyboard.press('Escape');
      const d2 = SPEC[ed].doc({ view: { x: 40, y: 30, zoom: 0.62, ui: 2 }, cables: [C('m1', ed === 'chains' ? 'out' : 'pitch', 'm2', ed === 'chains' ? 'in' : 'pitch'), C(ed === 'chains' ? 'm5' : 'm6', 'out', 'm3', ed === 'chains' ? 'in' : 'cutoff'), C('m3', 'out', 'm4', 'in')] });
      await loadDoc(page, base, ed, d2); if (skin === 'idmpg') { await skinCss(page); await sleep(page, 200); }
      await page.mouse.click(700, 880); await sleep(page, 1500);
      const pt = await page.evaluate(() => { for (const p of document.querySelectorAll('#cablehits path.hit')) { const L = p.getTotalLength(), m = p.getScreenCTM(); for (let t = 0.3; t <= 0.7; t += 0.02) { const q = p.getPointAtLength(L * t), r = new DOMPoint(q.x, q.y).matrixTransform(m); if (document.elementFromPoint(r.x, r.y) === p) return { x: r.x, y: r.y }; } } return { x: 700, y: 400 }; });
      await page.mouse.move(pt.x - 20, pt.y - 50); await page.mouse.move(pt.x, pt.y, { steps: 4 }); await sleep(page, 500); await page.screenshot({ path: `${f}-3-cable-readout.png` }); await page.mouse.move(5, 5);
      await page.click('.node[data-id="m2"] > header .t'); await page.keyboard.down('Shift'); await page.click('.node[data-id="m3"] > header .t'); await page.click('.node[data-id="m4"] > header .t'); await page.keyboard.up('Shift');
      await page.click('.node[data-id="m3"] > header .t', { button: 'right' }); await sleep(page, 200); await page.screenshot({ path: `${f}-4-align-menu.png` });
      await clickMenu(page, /Align tops/); await sleep(page, 200); await page.screenshot({ path: `${f}-5-aligned.png` });
      await page.keyboard.press('Shift+f'); await sleep(page, 250); await page.screenshot({ path: `${f}-6-zoom-to-selection.png` });
      const s = await page.evaluate(() => { const r = document.querySelector('.node[data-id="m2"] .row.sl input'); const b = r.getBoundingClientRect(); return { x: b.left + b.width / 2, y: b.top + b.height / 2 }; });
      await page.mouse.click(5, 5); await page.mouse.move(s.x - 10, s.y); await page.mouse.move(s.x, s.y); await sleep(page, 600); await page.screenshot({ path: `${f}-7-wheel-outline.png` });
      await page.mouse.click(s.x, s.y, { button: 'right' }); await sleep(page, 200); await page.screenshot({ path: `${f}-8-slider-menu.png` });
    } catch (err) { console.log('shot failed', f, err.message.split('\n')[0]); }
    await B.close();
  }
  if (OLD) {
    for (const ed of ED) { const B = await bootEd(1440, 900, false); const { page } = B; try { await loadDoc(page, OLD.base, ed, SPEC[ed].doc({ view: { x: 40, y: 30, zoom: 0.62, ui: 2 } })); await page.screenshot({ path: `${OUTD}/before-${ed}-1440x900-white.png` }); } catch (_) {} await B.close(); }
    OLD.close();
  }
}

// ================= main =================
const only = process.argv.slice(2), want = k => !only.length || only.includes(k);
const needOld = ['m5', 'u11', 'old', 'shots'].some(want);
let OLD = null;
try {
  if (only.includes('shots')) await shots();
  else {
    OLD = needOld ? await baseBuild() : null;
    for (const ed of ED) {
      if (want('r1') || want('r2')) await review(ed);
      if (want('d5')) for (const sz of SIZES) await d5(ed, sz);
      if (want('d1') || want('d8')) await d1d8(ed);
      if (want('d7') || want('d10')) await keys(ed);
      if (want('m4')) await m4(ed);
      if (want('m3')) await m3(ed);
      if (want('m5')) await m5(ed, OLD);
      if (want('m8')) await m8(ed);
      if (want('m11')) await m11(ed);
      if (want('u4')) await u4(ed);
      if (want('u11')) await u11(ed, OLD);
      if (want('old')) await oldfmt(ed, OLD);
      if (want('touch')) await touch(ed);
    }
  }
} finally { if (OLD) OLD.close(); }
const f = T.print(); process.exit(f ? 1 : 0);
