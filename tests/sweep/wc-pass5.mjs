// …waves / …chains pass 5, "Structure" (14 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: M1 module widths, the Mini collapsed state, M6 groups), plus the six review fixes of pass 4.
// Both editors, standalone, with a real mouse and keyboard (playwright) at 1440x900, and real touch (CDP) at 344x780. Prints PASS / FAIL (NOTE for what was not built).
//   node tests/sweep/wc-pass5.mjs               all of it
//   node tests/sweep/wc-pass5.mjs rf w mini grp old touch      sections: rf (review fixes) w (widths) mini grp old (build 587 loads a Mini patch) touch
//   BASE_ROOT=/path/to/a/build-587/checkout   (default: a temporary git worktree of origin/beta, removed afterwards)
//   SHOTS=/dir node tests/sweep/wc-pass5.mjs shots     screenshots for the owner
import fs from 'fs'; import os from 'os'; import path from 'path'; import { execSync } from 'child_process';
import { boot, Table, ROOT, serveDir, listenLoopback } from './ui-lib.mjs';

const T = new Table('wc-pass5');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'PASS ' : 'FAIL ') + name + (note ? '  ' + note : '')); };
const note = (name, why) => console.log('NOTE ' + name + '  ' + why);
const sleep = (page, ms) => page.waitForTimeout(ms);
const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|autoplay/i;
const ED = ['chains', 'waves'];

const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp, extra = {}) => ({ from: [a, ap], to: [b, bp], bend: 0, ...extra });
const SPEC = {
  chains: { key: 'chains.patch', setKey: 'chains.settings', src: 'chains/index.html',
    doc: (o = {}) => ({ format: 'chains.patch', meta: { name: 'wc pass5' }, view: o.view || { x: 20, y: 20, zoom: 1, ui: 2, nw: 1 }, st2: 1, ti2: 1,
      modules: [P('m1', 'trackIn', 0, 0, { track: 1 }), P('m2', 'delay', 420, 0), P('m3', 'reverb', 840, 0), P('m4', 'chainOut', 1260, 0, { track: 1 }),
        P('m5', 'lfo', 0, 560), P('m6', 'atten', 420, 560), P('m7', 'math', 700, 560), P('m8', 'eq', 960, 560)],
      cables: o.cables || [C('m1', 'out', 'm2', 'in'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m5', 'out', 'm6', 'in'), C('m6', 'out', 'm2', 'p:time')], groups: o.groups || [], custom: [], transport: { tempo: 120 } }) },
  waves: { key: 'waves.patch', setKey: 'waves.settings', src: 'waves/index.html',
    doc: (o = {}) => ({ meta: { name: 'wc pass5' }, view: o.view || { x: 20, y: 20, zoom: 1, ui: 2, nw: 1 }, voice: { maxVoices: 8 },
      modules: [P('m1', 'hostIn', 0, 0), P('m2', 'vco', 420, 0, { wave: 1 }), P('m3', 'vcf', 840, 0), P('m4', 'out', 1260, 0, { slot: 1, level: 1 }),
        P('m5', 'lfo', 0, 560), P('m6', 'atten', 420, 560), P('m7', 'math', 700, 560), P('m8', 'delay', 960, 560)],
      cables: o.cables || [C('m1', 'pitch', 'm2', 'pitch'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m5', 'out', 'm6', 'in'), C('m6', 'out', 'm2', 'p:fine')], groups: o.groups || [], custom: [], transport: { tempo: 120 } }) },
};
const GROUP = (ids, o = {}) => ({ id: 'g1', name: 'Core', color: '#8a73c9', modules: ids, collapsed: !!o.collapsed, bounds: o.bounds || [396, -54, 920, 380] });

async function bootEd(w, h, mob) { const B = await boot({ width: w, height: h, mobile: mob }); B.base = `http://127.0.0.1:${B.port}`; return B; }
const done = (errors, tag) => { const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok(`${tag}: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | ')); };
async function loadDoc(page, base, ed, doc, o = {}) {
  const url = `${base}/${ed}/index.html`;
  await page.goto(url);
  await page.evaluate(([k, sk, d, st]) => { localStorage.clear(); if (d) localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify(st)); }, [SPEC[ed].key, SPEC[ed].setKey, doc, o.settings || { tips: false }]);
  await page.goto(url); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 900);
}
// the same page with the module widths replaced (route-served source), to try widths no module declares yet (narrowest, widest, a wide one with a long title)
async function loadStress(page, base, ed, doc, widths) {
  const url = `${base}/${ed}/index.html`, src = fs.readFileSync(path.join(ROOT, SPEC[ed].src), 'utf8');
  const lit = Object.entries(widths).map(([k, v]) => `${k}:${v}`).join(', ');
  const out = src.replace('function widthOf(type){', `Object.assign(MODULE_WIDTH, { ${lit} });\nfunction widthOf(type){`);
  if (out === src) throw new Error('widthOf not found in ' + ed);
  await page.route(`**/${ed}/index.html`, r => r.fulfill({ status: 200, contentType: 'text/html', body: out }));
  await page.goto(url);
  await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips: false })); }, [SPEC[ed].key, SPEC[ed].setKey, doc]);
  await page.goto(url); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 900);
}
const savedDoc = async (page, ed) => { await sleep(page, 1700); return page.evaluate(k => JSON.parse(localStorage.getItem(k) || 'null'), SPEC[ed].key); };
const node = id => `.node[data-id="${id}"]`;
const stateOf = (page, id) => page.evaluate(i => { const n = document.querySelector(`.node[data-id="${i}"]`); return n ? (n.classList.contains('mini') ? 'mini' : n.classList.contains('collapsed') ? 'compact' : 'open') : null; }, id);
const allStates = page => page.evaluate(() => [...document.querySelectorAll('.node')].map(n => n.classList.contains('mini') ? 'mini' : n.classList.contains('collapsed') ? 'compact' : 'open'));
const shown = (page, sel) => page.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return getComputedStyle(e).display !== 'none' && r.width > 0 && r.height > 0; }, sel);
const box = (page, sel) => page.evaluate(s => { const e = document.querySelector(s); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height, l: r.left, t: r.top, r: r.right, b: r.bottom }; }, sel);
const toastText = page => page.evaluate(() => { const t = document.getElementById('toast'); return t && !t.hidden ? t.textContent : ''; });
async function setPlay(page, on) { await page.click(on ? '#modeplay' : '#modeedit'); await sleep(page, 150); }
async function press(page, combo) { await page.mouse.move(700, 880); await page.keyboard.press(combo); await sleep(page, 350); }
const chev = id => `${node(id)} > header .fold`;
async function blur(page) { await page.evaluate(() => document.activeElement && document.activeElement.blur()); await page.mouse.click(5, 880); }

// ---- real touch (Chrome DevTools Protocol) ----
const cdpOf = new WeakMap();
async function cdp(page) { let c = cdpOf.get(page); if (!c) { c = await page.context().newCDPSession(page); cdpOf.set(page, c); } return c; }
const tStart = async (page, x, y) => (await cdp(page)).send('Input.dispatchTouchEvent', { type: 'touchStart', touchPoints: [{ x, y, id: 1 }] });
const tEnd = async page => (await cdp(page)).send('Input.dispatchTouchEvent', { type: 'touchEnd', touchPoints: [] });
async function tap(page, x, y) { await tStart(page, x, y); await sleep(page, 40); await tEnd(page); await sleep(page, 80); }
async function hold(page, x, y, ms = 700) { await tStart(page, x, y); await sleep(page, ms); await tEnd(page); await sleep(page, 160); }

// ---- the previous build (build 587): a temporary git worktree of origin/beta, removed afterwards ----
async function baseBuild() {
  let dir = process.env.BASE_ROOT, wt = null;
  if (!dir) {
    try { const sha = execSync(`git -C ${JSON.stringify(ROOT)} rev-parse origin/beta`, { stdio: 'pipe' }).toString().trim(); wt = path.join(os.tmpdir(), 'wc-pass5-base-' + process.pid);
      execSync(`git -C ${JSON.stringify(ROOT)} worktree add --detach ${JSON.stringify(wt)} ${sha}`, { stdio: 'pipe' }); dir = wt; }
    catch (e) { console.log('SKIP the "build 587" comparisons: no origin/beta worktree could be made (' + String(e.message).split('\n')[0] + ')'); return null; }
  }
  const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.woff': 'font/woff', '.png': 'image/png', '.webmanifest': 'application/manifest+json', '.json': 'application/json' };
  const srv = serveDir(dir, TYPES); await listenLoopback(srv);
  return { base: `http://127.0.0.1:${srv.address().port}`, dir, close: () => { srv.close(); if (wt) try { execSync(`git -C ${JSON.stringify(ROOT)} worktree remove --force ${JSON.stringify(wt)}`, { stdio: 'pipe' }); } catch (e) { try { fs.rmSync(wt, { recursive: true, force: true }); execSync(`git -C ${JSON.stringify(ROOT)} worktree prune`, { stdio: 'pipe' }); } catch (_) {} } } };
}

// the declared widths, read from the source the page is served from (NARROW_S / NARROW_M and the table)
function declaredWidths(ed) {
  const src = fs.readFileSync(path.join(ROOT, SPEC[ed].src), 'utf8');
  const s = /const NARROW_S = (\d+), NARROW_M = (\d+);/.exec(src), t = /const MODULE_WIDTH = \{([\s\S]*?)\n\};/.exec(src);
  if (!s || !t) return null;
  return new Function('NARROW_S', 'NARROW_M', `return ({${t[1]}\n})`)(+s[1], +s[2]);
}

// ---- what to check about a module's frame: nothing overflows it, the title is on one line, whole words ----
const frameCheck = (page, id) => page.evaluate(i => {
  const n = document.querySelector(`.node[data-id="${i}"]`); if (!n) return { err: 'missing' };
  const z = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a, nr0 = n.getBoundingClientRect(), nr = { left: nr0.left, right: nr0.right, width: nr0.width, height: nr0.height }, bad = [];
  for (const e of n.querySelectorAll('*')) {
    if (e.closest('svg') || e.closest('.jack, .pm, .port, .cmval, .cs-t')) continue;   // jacks stand half outside the frame by design; the summary line is cut with an ellipsis
    const r = e.getBoundingClientRect(); if (!r.width || !r.height) continue;
    if (r.right > nr.right + 1.5 * z || r.left < nr.left - 1.5 * z) bad.push('out:' + (e.className && e.className.baseVal === undefined ? e.className : e.tagName) + ' ' + Math.round(r.left - nr.left) + '/' + Math.round(r.right - nr.right));
  }
  for (const e of n.querySelectorAll('header .t, .val, .lbl')) if (e.getBoundingClientRect().width && e.scrollWidth > e.clientWidth + 1.5) bad.push('clip:' + e.className);
  const t = n.querySelector('header .t'), hd = n.querySelector('header'), tr = t.getBoundingClientRect();
  return { w: Math.round(nr.width / z * 10) / 10, h: Math.round(nr.height / z), bad, oneLine: tr.height / z <= 24, whole: !hd.classList.contains('tt3'), title: t.textContent };
}, id);
// where the cables end: on the jack when it is drawn; on the module's edge (inputs left, outputs right) when it is Mini
const cableEnds = (page, cables) => page.evaluate(cs => {
  const gs = [...document.querySelectorAll('svg.cables:not(.under) > g')].filter(g => g.querySelector('circle') && g.style.display !== 'none'), res = [];
  const jackRect = (id, port, dir) => { const list = [...document.querySelectorAll(`.jack[data-id="${id}"][data-port="${port}"][data-dir="${dir}"]`)].map(j => j.getBoundingClientRect()).filter(r => r.width > 0 && r.height > 0); return list[0] || null; };
  cs.forEach((c, k) => {
    const g = gs[k]; if (!g) { res.push('no cable ' + k); return; }
    const ce = [...g.querySelectorAll('circle')].map(x => { const r = x.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; });
    [[0, c.from, 'out'], [1, c.to, 'in']].forEach(([i, [id, port], dir]) => {
      const nd = document.querySelector(`.node[data-id="${id}"]`); if (!nd || getComputedStyle(nd).display === 'none') return;
      const jr = jackRect(id, port, dir), p = ce[i];
      if (jr) { const d = Math.hypot(p[0] - (jr.left + jr.width / 2), p[1] - (jr.top + jr.height / 2)); if (d > 3.5) res.push(`${id}.${port}: end ${d.toFixed(1)}px from its jack`); }
      else if (nd.classList.contains('mini')) { const nr = nd.getBoundingClientRect(), hr = nd.querySelector('header').getBoundingClientRect(), x = dir === 'in' ? nr.left : nr.right; if (Math.abs(p[0] - x) > 3 || p[1] < hr.top - 1 || p[1] > hr.bottom + 1) res.push(`${id}.${port}: Mini end not on the title bar edge (${p.map(Math.round)} vs x ${Math.round(x)}, y ${Math.round(hr.top)}-${Math.round(hr.bottom)})`); }
      else res.push(`${id}.${port}: no jack drawn and not Mini`);
    });
  });
  return res;
}, cables);

// ================= review fixes of pass 4 =================
async function rf() {
  // r4, r5, r6 are about the tests themselves: read the files
  const rd = f => fs.readFileSync(path.join(ROOT, f), 'utf8');
  ok('review 4: wc-pass3 compares against the full 40-character merge id', /const BASE585 = '[0-9a-f]{40}'/.test(rd('tests/sweep/wc-pass3.mjs')));
  ok('review 4: the wc-pass3 round-trip fixture has explicit non-default values', /P\('s1', 'delay', 420, 0, \{ time: 0\.62/.test(rd('tests/sweep/wc-pass3.mjs')));
  ok('review 5: the "looks the same as build 586" check no longer needs Pillow or /tmp', !/PIL|Pillow|\/tmp\/u11/.test(rd('tests/sweep/wc-pass4.mjs')));
  ok('review 6: the audio-untouched test no longer reads git status', !/git status --porcelain/.test(rd('tests/wc-pass4.test.js')) && /merge-base/.test(rd('tests/wc-pass4.test.js')));
  for (const ed of ED) {
    const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} review`;
    try {
      const d = SPEC[ed].doc({ cables: [] }); await loadDoc(page, base, ed, d);
      // r1: wheeling slider B within 500 ms of slider A must not cancel A's pending change
      const keys = await page.evaluate(() => [...document.querySelectorAll('.row.sl[data-key] input[type=range]')].slice(0, 40).map(i => i.closest('.row').dataset.id + '|' + i.closest('.row').dataset.key));
      const ra = keys[0], rb = keys.find(k => k.split('|')[0] === 'm2' && k !== ra) || keys[1];
      const sel = k => `.row.sl[data-id="${k.split('|')[0]}"][data-key="${k.split('|')[1]}"] input[type=range]`;
      await page.evaluate(([a, b]) => { window.__ch = {}; for (const [k, s] of [['a', a], ['b', b]]) { const el = document.querySelector(s); el.addEventListener('change', () => { window.__ch[k] = (window.__ch[k] || 0) + 1; window.__ch[k + 't'] = performance.now(); }); } }, [sel(ra), sel(rb)]);
      const A = await box(page, sel(ra)), Bx = await box(page, sel(rb));
      await page.mouse.move(A.x - 20, A.y); await page.mouse.move(A.x, A.y); await sleep(page, 480);
      await page.mouse.wheel(0, -120); await sleep(page, 60);
      await page.mouse.move(Bx.x - 20, Bx.y); await page.mouse.move(Bx.x, Bx.y); await sleep(page, 480);
      await page.mouse.wheel(0, -120); await sleep(page, 800);
      const ch = await page.evaluate(() => window.__ch);
      ok(`${tag}: the wheel on slider B does not cancel slider A's pending change (changes ${JSON.stringify(ch)})`, ch.a >= 1 && ch.b >= 1);
      ok(`${tag}: A's change was delivered when the wheel moved on, before B's own 500 ms ran out`, ch.at <= ch.bt - 300);
      // r2: letter shortcuts follow the letter the key types (AZERTY: the key printed A sits where QWERTY has Q)
      const kd = (key, code, mods = {}) => page.evaluate(([k, c, m]) => window.dispatchEvent(new KeyboardEvent('keydown', { key: k, code: c, bubbles: true, cancelable: true, ...m })), [key, code, mods]);
      const qaOpen = () => page.evaluate(() => { const m = document.getElementById('ctxmenu'); return !m.hidden && !!m.querySelector('.qainp'); });
      await page.mouse.move(700, 880); await blur(page);
      await kd('a', 'KeyQ'); await sleep(page, 250); const aOk = await qaOpen(); await page.keyboard.press('Escape'); await sleep(page, 150);
      await kd('q', 'KeyA'); await sleep(page, 250); const qNo = !(await qaOpen()); await page.keyboard.press('Escape'); await sleep(page, 150);
      ok(`${tag}: A (the key that types "a", wherever it sits) opens quick add; the key at the physical A position that types "q" does not`, aOk && qNo);
      await kd('a', 'KeyQ', { ctrlKey: true }); await sleep(page, 200);
      const nSel = await page.evaluate(() => document.querySelectorAll('.node.selected').length), nAll = await page.evaluate(() => document.querySelectorAll('.node').length);
      ok(`${tag}: Ctrl+A by letter picks every module (${nSel} of ${nAll})`, nSel === nAll);
      await blur(page);
      const z0 = (await page.evaluate(() => new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a));
      await kd('f', 'KeyU'); await sleep(page, 400); const z1 = await page.evaluate(() => new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a);
      ok(`${tag}: F by letter fits the patch (zoom ${z0.toFixed(2)} to ${z1.toFixed(2)})`, Math.abs(z1 - z0) > 0.02);
      await kd('ç', 'KeyC', { altKey: true }); await sleep(page, 300);
      const st = await allStates(page); ok(`${tag}: Alt+C still works where Option types another character (key "ç", position C)`, st.every(s => s === 'compact'));
      await kd('Ç', 'KeyC', { altKey: true, shiftKey: true }); await sleep(page, 300);
      ok(`${tag}: Alt+Shift+C (Option on a Mac) takes every module to Mini`, (await allStates(page)).every(s => s === 'mini'));
      // r3: a jack is not offered to the keyboard as a button
      const jr = await page.evaluate(() => { const j = [...document.querySelectorAll('.jack:not([aria-hidden])')]; return { n: j.length, img: j.every(x => x.getAttribute('role') === 'img'), btn: j.filter(x => x.getAttribute('role') === 'button').length, tab: j.filter(x => x.tabIndex >= 0).length, label: j.every(x => (x.getAttribute('aria-label') || '').length > 3) }; });
      ok(`${tag}: ${jr.n} jacks are role=img with a label (not role=button; no tab stops, since patching is by pointer)`, jr.n > 10 && jr.img && jr.btn === 0 && jr.tab === 0 && jr.label);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    done(errors, tag); await B.close();
  }
}

// ================= M1: module widths =================
async function widths() {
  for (const ed of ED) {
    const decl = declaredWidths(ed), types = Object.keys(decl || {});
    ok(`${ed} M1: the width table is declared (${types.length} modules: ${types.map(t => t + ' ' + decl[t]).join(', ')})`, decl && types.length >= 5 && types.every(t => decl[t] >= 150 && decl[t] <= 600));
    if (!decl) continue;
    const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} M1`;
    try {
      // a patch of every declared module, side by side in rows, with real cables: out of each into the next one's first input, and one into a slider
      const mk = (cables, zoom) => ({ ...SPEC[ed].doc({ cables: cables || [], view: { x: 10, y: 10, zoom: zoom || 1, ui: 2, nw: 1 } }), modules: types.map((t, i) => P('w' + i, t, (i % 5) * 320, Math.floor(i / 5) * 520)) });
      await loadDoc(page, base, ed, mk());
      const ports = await page.evaluate(() => [...document.querySelectorAll('.node')].map(n => ({ id: n.dataset.id, ins: [...n.querySelectorAll('.ins .jack')].map(j => j.dataset.port), outs: [...n.querySelectorAll('.outs .jack')].map(j => j.dataset.port), mods: [...n.querySelectorAll('.row.sl .jack.small')].map(j => j.dataset.port) })));
      const cables = [];
      for (let i = 0; i + 1 < ports.length; i++) { const a = ports[i], b = ports[i + 1]; if (a.outs[0] && b.ins[0]) cables.push(C(a.id, a.outs[0], b.id, b.ins[0])); }
      for (let i = 0; i + 2 < ports.length; i += 2) { const a = ports[i], b = ports[i + 2]; if (a.outs[0] && b.mods[0]) cables.push(C(a.id, a.outs[0], b.id, b.mods[0])); }
      ok(`${tag}: a test patch with ${cables.length} real cables between the narrow modules`, cables.length >= types.length - 1);
      for (const [zoom, w, h, mob] of [[0.5, 1440, 900, false], [1, 1440, 900, false], [1.5, 1440, 900, false]]) {
        await loadDoc(page, base, ed, mk(cables, zoom)); const z = await page.evaluate(() => new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a);
        for (const st of ['open', 'compact', 'mini']) {
          if (st === 'compact') await press(page, 'Alt+c'); if (st === 'mini') await press(page, 'Alt+Shift+C');
          await sleep(page, 400);
          const bads = []; for (let i = 0; i < types.length; i++) { const r = await frameCheck(page, 'w' + i); const want = decl[types[i]];
            if (r.err) { bads.push(types[i] + ' ' + r.err); continue; }
            if (r.w < want - 0.6 || (st !== 'compact' && r.w > want + 0.6)) bads.push(`${types[i]} is ${r.w}px, declared ${want}`);
            if (r.bad.length) bads.push(`${types[i]} overflows: ${r.bad.slice(0, 2).join(', ')}`); if (!r.oneLine) bads.push(types[i] + ' title on two lines'); if (!r.whole) bads.push(types[i] + ' title broken inside a word'); }
          ok(`${tag} @${Math.round(z * 100)}% ${st}: every declared width holds (${types.length} modules): nothing overflows the frame, the title is whole and on one line`, !bads.length, bads.slice(0, 4).join(' | '));
          const ce = await cableEnds(page, cables); ok(`${tag} @${Math.round(z * 100)}% ${st}: every cable end is on its jack (Mini: on the title bar's edge) (${cables.length} cables)`, !ce.length, ce.slice(0, 3).join(' | '));
          if (st === 'mini') { const hs = await page.evaluate(() => [...document.querySelectorAll('.node')].map(n => Math.round(n.getBoundingClientRect().height / (new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a)))); ok(`${tag} @${Math.round(z * 100)}% Mini: every module is about 42 px high (${Math.min(...hs)} to ${Math.max(...hs)})`, Math.min(...hs) >= 36 && Math.max(...hs) <= 46); }
        }
      }
      // opening again returns to the declared width (compact's growth is only drawn)
      await press(page, 'Alt+Shift+C'); await sleep(page, 300);
      const ws = await page.evaluate(() => { const z = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a; return [...document.querySelectorAll('.node')].map(n => Math.round(n.getBoundingClientRect().width / z)); });
      ok(`${tag}: open again, each module is back at its declared width`, ws.every((w, i) => Math.abs(w - decl[types[i]]) <= 1), ws.join(','));
      // nothing about the width is saved per module
      const sd = await savedDoc(page, ed);
      ok(`${tag}: the saved patch carries no width (modules: ${Object.keys(sd.modules[0]).join(',')})`, sd.modules.every(m => !('w' in m) && !('width' in m) && !('nw' in m)));
      // the stress patch: widths no module declares (narrowest 150, a wide one, the widest 600), at 3 zooms, in the three states
      const stress = ed === 'chains' ? { atten: 150, eq: 340, lfo: 500, slew: 600, adsr: 150, compressor: 190 } : { atten: 150, vcf: 340, lfo: 500, slew: 600, adsr: 150, delay: 190 };
      const st = Object.keys(stress), sdoc = z => ({ ...SPEC[ed].doc({ cables: [], view: { x: 10, y: 10, zoom: z, ui: 2, nw: 1 } }), modules: st.map((t, i) => P('s' + i, t, (i % 3) * 640, Math.floor(i / 3) * 620)) });
      const B2 = await bootEd(1440, 900, false); const p2 = B2.page;
      for (const z of [0.5, 1, 1.5]) {
        await loadStress(p2, B2.base, ed, sdoc(z), stress);
        for (const s of ['open', 'compact', 'mini']) {
          if (s === 'compact') await press(p2, 'Alt+c'); if (s === 'mini') await press(p2, 'Alt+Shift+C'); await sleep(p2, 400);
          const bads = []; for (let i = 0; i < st.length; i++) { const r = await frameCheck(p2, 's' + i); if (r.err) { bads.push(st[i] + ' ' + r.err); continue; } const want = stress[st[i]];
            if (s !== 'compact' && r.w < want - 0.6) bads.push(`${st[i]} ${r.w}px < declared ${want}`); if (s === 'open' && r.w > want + 40 && want > 200) bads.push(`${st[i]} ${r.w}px far above ${want}`);
            if (r.bad.length) bads.push(`${st[i]} overflows: ${r.bad.slice(0, 2).join(', ')}`); if (!r.whole) bads.push(st[i] + ' title broken inside a word'); }
          ok(`${tag} stress @${Math.round(z * 100)}% ${s}: widths ${st.map(t => t + ' ' + stress[t]).join(', ')} hold, nothing overflows`, !bads.length, bads.slice(0, 4).join(' | '));
        }
      }
      await B2.close();
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    done(errors, tag); await B.close();
  }
}

// ================= Mini =================
async function mini() {
  for (const ed of ED) {
    const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} Mini`;
    try {
      const doc = SPEC[ed].doc(); await loadDoc(page, base, ed, doc);
      ok(`${tag}: a fresh patch opens with every module Open`, (await allStates(page)).every(s => s === 'open'));
      // the cycle: Open -> Compact (click) -> Mini (Shift+click) -> Open (click)
      await page.click(chev('m2')); await sleep(page, 200); const s1 = await stateOf(page, 'm2');
      const shows = { csum: await shown(page, `${node('m2')} > .csum`), body: await shown(page, `${node('m2')} > .body`), ports: await shown(page, `${node('m2')} > .ports`) };
      await page.click(chev('m2'), { modifiers: ['Shift'] }); await sleep(page, 200); const s2 = await stateOf(page, 'm2');
      const mshows = { csum: await shown(page, `${node('m2')} > .csum`), body: await shown(page, `${node('m2')} > .body`), ports: await shown(page, `${node('m2')} > .ports`), stubs: await shown(page, `${node('m2')} > .cstubs`), head: await shown(page, `${node('m2')} > header`), help: await shown(page, `${node('m2')} > header .mq`), x: await shown(page, `${node('m2')} > header .x`), fold: await shown(page, `${node('m2')} > header .fold`) };
      const mh = (await box(page, node('m2'))).h;
      await page.click(chev('m2')); await sleep(page, 200); const s3 = await stateOf(page, 'm2');
      ok(`${tag}: the cycle is Open, Compact (click), Mini (Shift+click), Open (click): ${s1}, ${s2}, ${s3}`, s1 === 'compact' && s2 === 'mini' && s3 === 'open');
      ok(`${tag}: Compact shows the summary line and the ports and hides the body (${JSON.stringify(shows)})`, shows.csum && shows.ports !== false && !shows.body);
      ok(`${tag}: Mini shows only the title bar: chevron, title, close, ${ed === 'chains' ? 'BYP, ' : ''}no summary, ports, stubs, body or ? (${JSON.stringify(mshows)}; ${Math.round(mh)} px high)`, mshows.head && mshows.fold && mshows.x && !mshows.csum && !mshows.ports && !mshows.stubs && !mshows.body && !mshows.help && mh >= 36 && mh <= 46);
      await page.click(chev('m2'), { modifiers: ['Shift'] }); await sleep(page, 150); await page.click(chev('m2'), { modifiers: ['Shift'] }); await sleep(page, 150);
      ok(`${tag}: Shift+click on a Mini module goes back to Compact`, (await stateOf(page, 'm2')) === 'compact');
      await page.click(chev('m2'), { modifiers: ['Shift'] }); await sleep(page, 150); await page.click(`${node('m2')} > header .t`, { clickCount: 2 }); await sleep(page, 650);
      ok(`${tag}: a double-click on a Mini module's title opens it`, (await stateOf(page, 'm2')) === 'open');
      await page.click(`${node('m2')} > header .t`, { clickCount: 2 }); await sleep(page, 650); const dc = await stateOf(page, 'm2');
      await page.click(`${node('m2')} > header .t`, { clickCount: 2 }); await sleep(page, 650);
      ok(`${tag}: a double-click on the title still folds to Compact and opens again (${dc}, then ${await stateOf(page, 'm2')})`, dc === 'compact' && (await stateOf(page, 'm2')) === 'open');
      // labels
      await page.click(chev('m2'), { modifiers: ['Shift'] }); await sleep(page, 150);
      const lab = await page.evaluate(i => { const f = document.querySelector(`.node[data-id="${i}"] > header .fold`); return [f.getAttribute('aria-label'), f.getAttribute('aria-expanded'), f.title]; }, 'm2');
      ok(`${tag}: the chevron of a Mini module says Expand and explains Shift+click (${lab[0]}; ${lab[1]})`, /^Expand /.test(lab[0]) && lab[1] === 'false' && /Shift\+click/.test(lab[2]));
      await page.click(chev('m2')); await sleep(page, 150);
      // what is saved
      await page.click(chev('m3')); await sleep(page, 150); await page.click(chev('m4'), { modifiers: ['Shift'] }); await sleep(page, 150);
      const sd = await savedDoc(page, ed); const M = id => sd.modules.find(m => m.id === id);
      ok(`${tag}: saved: Open has collapsed:false and no view; Compact collapsed:true and no view; Mini collapsed:true with view:"mini" (${JSON.stringify([M('m2').collapsed, M('m2').view, M('m3').collapsed, M('m3').view, M('m4').collapsed, M('m4').view])})`,
        M('m2').collapsed === false && !('view' in M('m2')) && M('m3').collapsed === true && !('view' in M('m3')) && M('m4').collapsed === true && M('m4').view === 'mini');
      await page.reload(); await page.waitForSelector('.node'); await sleep(page, 900);
      ok(`${tag}: save then load gives the same three states (Open, Compact, Mini): ${[await stateOf(page, 'm2'), await stateOf(page, 'm3'), await stateOf(page, 'm4')]}`, (await stateOf(page, 'm2')) === 'open' && (await stateOf(page, 'm3')) === 'compact' && (await stateOf(page, 'm4')) === 'mini');
      // legacy
      const old = SPEC[ed].doc(); old.modules[1].collapsed = true; old.modules[2].collapsed = true; old.modules[2].view = 'tiny'; old.modules[3].collapsed = false; old.modules[3].view = 'mini';
      await loadDoc(page, base, ed, old);
      ok(`${tag}: collapsed:true with no view opens as Compact; an unknown view too; view:"mini" on an open module is ignored (${[await stateOf(page, 'm2'), await stateOf(page, 'm3'), await stateOf(page, 'm4')]})`, (await stateOf(page, 'm2')) === 'compact' && (await stateOf(page, 'm3')) === 'compact' && (await stateOf(page, 'm4')) === 'open');
      // Alt+C and Alt+Shift+C
      await loadDoc(page, base, ed, SPEC[ed].doc());
      await press(page, 'Alt+c'); const a1 = await allStates(page);
      await press(page, 'Alt+Shift+C'); const a2 = await allStates(page);
      await press(page, 'Control+z'); const a3 = await allStates(page);
      await press(page, 'Alt+Shift+C'); const a4 = await allStates(page); await press(page, 'Alt+Shift+C'); const a5 = await allStates(page);
      const all = (a, s) => a.every(x => x === s);
      ok(`${tag}: Alt+C collapses all to Compact; Alt+Shift+C then takes all to Mini; Ctrl+Z undoes that in one step (back to Compact); Alt+Shift+C again Mini; once more opens all`, all(a1, 'compact') && all(a2, 'mini') && all(a3, 'compact') && all(a4, 'mini') && all(a5, 'open'), [a1[0], a2[0], a3[0], a4[0], a5[0]].join(','));
      await press(page, 'Alt+c'); await press(page, 'Alt+c'); ok(`${tag}: Alt+C still collapses then expands every module (the pass 1 behaviour)`, all(await allStates(page), 'open'));
      // the menus
      await page.click(`${node('m3')} > header .t`, { button: 'right' }); await sleep(page, 200);
      const items = await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].map(e => e.textContent));
      ok(`${tag}: the module menu lists Collapse, Mini and About (${items.filter(t => /Collapse|Mini|About/.test(t)).join(' / ')})`, items.some(t => /^Collapse this module/.test(t)) && items.some(t => /^Mini this module/.test(t)) && items.some(t => /^About this module/.test(t)));
      await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(e => /^Mini this module/.test(e.textContent)).click()); await sleep(page, 250);
      ok(`${tag}: "Mini this module" does it`, (await stateOf(page, 'm3')) === 'mini');
      await page.click(`${node('m3')} > header .t`, { button: 'right' }); await sleep(page, 200);
      await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].find(e => /^About this module/.test(e.textContent)).click()); await sleep(page, 300);
      ok(`${tag}: "About this module" opens the help card (a Mini module has no ? button)`, await page.evaluate(() => { const h = document.getElementById('mhelp'); return !!h && !h.hidden && h.textContent.length > 20; }));
      await page.keyboard.press('Escape'); await blur(page);
      await page.click('#ws', { button: 'right', position: { x: 700, y: 700 } }); await sleep(page, 200);
      const bg = await page.evaluate(() => [...document.querySelectorAll('#ctxmenu .ctxitem')].map(e => e.textContent));
      ok(`${tag}: the background menu offers "Mini all modules (Alt+Shift+C)" (${bg.filter(t => /Mini/.test(t)).join(' / ')})`, bg.some(t => /Mini all modules \(Alt\+Shift\+C\)/.test(t)));
      await page.keyboard.press('Escape');
      // cables on a Mini module: several on one side spread along the title bar
      const two = SPEC[ed].doc({ cables: ed === 'chains' ? [C('m1', 'out', 'm2', 'in'), C('m1', 'outR', 'm2', 'inR'), C('m2', 'out', 'm3', 'in'), C('m2', 'outR', 'm3', 'inR')] : [C('m1', 'pitch', 'm2', 'pitch'), C('m1', 'gate', 'm2', 'fm'), C('m2', 'out', 'm3', 'in'), C('m5', 'out', 'm3', 'p:cutoff')] });
      await loadDoc(page, base, ed, two); await press(page, 'Alt+Shift+C'); await press(page, 'Alt+Shift+C'); await press(page, 'Alt+c'); await press(page, 'Alt+Shift+C');
      const ends = await page.evaluate(() => [...document.querySelectorAll('svg.cables:not(.under) > g')].filter(g => g.style.display !== 'none').map(g => [...g.querySelectorAll('circle')].map(c => { const r = c.getBoundingClientRect(); return [Math.round(r.left + r.width / 2), Math.round(r.top + r.height / 2)]; })));
      const ys = ends.slice(0, 2).map(e => e[1][1]);
      ok(`${tag}: two cables into one Mini module end at different heights on its edge (${ys.join(' and ')})`, ends.length >= 2 && Math.abs(ys[0] - ys[1]) >= 3);
      const ce = await cableEnds(page, two.cables); ok(`${tag}: every cable of the all-Mini patch ends on the title bar's edge`, !ce.length, ce.slice(0, 3).join(' | '));
      // Play: no folding
      await loadDoc(page, base, ed, SPEC[ed].doc()); await setPlay(page, true);
      await page.click(chev('m2')); await sleep(page, 150); const p1 = await stateOf(page, 'm2');
      await page.click(chev('m2'), { modifiers: ['Shift'] }); await sleep(page, 150); const p2 = await stateOf(page, 'm2'); const tt = await toastText(page);
      await press(page, 'Alt+c'); await press(page, 'Alt+Shift+C');
      ok(`${tag}: in Play nothing folds: the chevron, Shift+click, Alt+C and Alt+Shift+C all leave every module Open (${p1}, ${p2}; "${tt}")`, p1 === 'open' && p2 === 'open' && (await allStates(page)).every(s => s === 'open') && /Play/.test(tt));
      await setPlay(page, false);
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    done(errors, tag); await B.close();
  }
}

// ================= M6: groups =================
const groupGeo = page => page.evaluate(() => { const b = document.querySelector('#groups .decor.box'); if (!b) return null; const r = b.getBoundingClientRect(), g = getComputedStyle(b); return { l: r.left, t: r.top, r: r.right, b: r.bottom, w: r.width, h: r.height, col: b.classList.contains('gcol'), txt: (b.querySelector('.dsum') || {}).textContent || '' }; });
const unionOf = (page, ids) => page.evaluate(is => { let l = 1e9, t = 1e9, r = -1e9, b = -1e9; const z = new DOMMatrix(getComputedStyle(document.getElementById('world')).transform).a; for (const i of is) { const e = document.querySelector(`.node[data-id="${i}"]`); if (!e || getComputedStyle(e).display === 'none') continue; const q = e.getBoundingClientRect(); l = Math.min(l, q.left); t = Math.min(t, q.top); r = Math.max(r, q.right); b = Math.max(b, q.bottom); } return { l: l - 24 * z, t: t - 54 * z, r: r + 24 * z, b: b + 24 * z }; }, ids);
const near = (a, b, tol = 2.5) => a && b && Math.abs(a.l - b.l) <= tol && Math.abs(a.t - b.t) <= tol && Math.abs(a.r - b.r) <= tol && Math.abs(a.b - b.b) <= tol;
async function grp() {
  for (const ed of ED) {
    const B = await bootEd(1440, 900, false); const { page, errors, base } = B; const tag = `${ed} M6`;
    try {
      const gdoc = o => SPEC[ed].doc({ groups: [GROUP(['m2', 'm3'], o)], view: { x: 10, y: 120, zoom: 1, ui: 2, nw: 1 } });
      // an old patch with a stale box: the stored box is used as it is until a member changes
      await loadDoc(page, base, ed, gdoc());
      const g0 = await groupGeo(page), un0 = await unionOf(page, ['m2', 'm3']); const z = (await box(page, node('m2'))).w / 250;
      ok(`${tag}: a stored box is drawn as stored on load (it is not recomputed: the patch looks as it did)`, g0 && Math.abs(g0.w - 920 * z) < 2 && Math.abs(g0.h - 380 * z) < 2 && !near(g0, un0, 30));
      // drag a member: the box follows
      const h2 = await box(page, `${node('m2')} > header .t`); await page.mouse.move(h2.x, h2.y); await page.mouse.down(); await page.mouse.move(h2.x + 60, h2.y + 80, { steps: 6 }); await page.mouse.up(); await sleep(page, 250);
      ok(`${tag}: dragging a member, the group's box is drawn round its members again`, near(await groupGeo(page), await unionOf(page, ['m2', 'm3'])), JSON.stringify([await groupGeo(page), await unionOf(page, ['m2', 'm3'])]));
      // fold a member: the box shrinks round it
      await page.click(chev('m3')); await sleep(page, 250);
      ok(`${tag}: folding a member to Compact, the box shrinks to fit`, near(await groupGeo(page), await unionOf(page, ['m2', 'm3'])));
      await page.click(chev('m3'), { modifiers: ['Shift'] }); await sleep(page, 250); await page.click(chev('m3'), { modifiers: ['Shift'] }); await sleep(page, 250);
      // arrow keys nudge the picked ones
      await page.click(`${node('m2')} > header .t`); await page.keyboard.press('ArrowRight'); await page.keyboard.press('ArrowRight'); await sleep(page, 250);
      ok(`${tag}: nudging a member with the arrow keys moves the box too`, near(await groupGeo(page), await unionOf(page, ['m2', 'm3'])));
      // remove a member: the box follows; remove the last: the group goes
      await page.click(`${node('m3')} > header .x`); await sleep(page, 250);
      ok(`${tag}: removing a member, the box closes round the rest`, near(await groupGeo(page), await unionOf(page, ['m2'])));
      await page.click(`${node('m2')} > header .x`); await sleep(page, 300);
      ok(`${tag}: removing the last member removes the group`, (await groupGeo(page)) === null && (await page.evaluate(() => document.querySelectorAll('#groups .decor.box').length)) === 0);
      // collapse: one block, hidden members, only the crossing cables, on its edges
      await loadDoc(page, base, ed, gdoc()); const nCab = SPEC[ed].doc().cables.length;
      await page.click('.dfold'); await sleep(page, 400);
      const g1 = await groupGeo(page), hid = await page.evaluate(() => ['m2', 'm3'].map(i => getComputedStyle(document.querySelector(`.node[data-id="${i}"]`)).display)), vis = await page.evaluate(() => [...document.querySelectorAll('svg.cables:not(.under) > g')].filter(g => g.style.display !== 'none').length);
      const cross = ed === 'chains' ? 2 : 2;   // chains: m1 -> m2, m3 -> m4 (m2 -> m3 is inside) plus m6 -> m2.p:time; waves: m1 -> m2, m3 -> m4, m6 -> m2.p:fine
      ok(`${tag}: collapsing the group makes it one block, its members hidden (${hid.join(', ')}); the block says "${g1 && g1.txt}"`, g1 && g1.col && hid.every(d => d === 'none') && /^2 modules · \d+ in · \d+ out$/.test(g1.txt));
      ok(`${tag}: the cable inside the group is not drawn; the others are (${vis} of ${nCab})`, vis === nCab - 1);
      const ends = await page.evaluate(() => [...document.querySelectorAll('svg.cables:not(.under) > g')].map(g => g.style.display === 'none' ? null : [...g.querySelectorAll('circle')].map(c => { const r = c.getBoundingClientRect(); return [r.left + r.width / 2, r.top + r.height / 2]; })));
      const cabs = SPEC[ed].doc().cables, bad = [];
      cabs.forEach((c, k) => { const e = ends[k]; if (!e) return; if (['m2', 'm3'].includes(c.to[0])) { if (Math.abs(e[1][0] - g1.l) > 3 || e[1][1] < g1.t || e[1][1] > g1.b) bad.push('in ' + c.to.join('.')); } if (['m2', 'm3'].includes(c.from[0])) { if (Math.abs(e[0][0] - g1.r) > 3 || e[0][1] < g1.t || e[0][1] > g1.b) bad.push('out ' + c.from.join('.')); } });
      ok(`${tag}: the cables that cross the boundary end on the block's left (inputs) and right (outputs) edge`, !bad.length, bad.join(' | '));
      await page.keyboard.press('Control+a'); await sleep(page, 200);
      ok(`${tag}: select all does not pick hidden modules (${await page.evaluate(() => document.querySelectorAll('.node.selected').length)} picked of ${await page.evaluate(() => [...document.querySelectorAll('.node')].filter(n => getComputedStyle(n).display !== 'none').length)} drawn)`, await page.evaluate(() => [...document.querySelectorAll('.node.selected')].every(n => getComputedStyle(n).display !== 'none') && document.querySelectorAll('.node.selected').length === [...document.querySelectorAll('.node')].filter(n => getComputedStyle(n).display !== 'none').length));
      await blur(page);
      // the block moves its members
      const p2a = await page.evaluate(() => { const n = document.querySelector('.node[data-id="m2"]'); return [parseFloat(n.style.left), parseFloat(n.style.top)]; });
      const lab = await box(page, '#groups .decor.box .dtext'); await page.mouse.move(lab.x + 20, lab.y); await page.mouse.down(); await page.mouse.move(lab.x + 20 + 80, lab.y + 40, { steps: 6 }); await page.mouse.up(); await sleep(page, 300);
      const p2b = await page.evaluate(() => { const n = document.querySelector('.node[data-id="m2"]'); return [parseFloat(n.style.left), parseFloat(n.style.top)]; });
      ok(`${tag}: dragging the block by its label moves the hidden modules with it (${p2b[0] - p2a[0]}, ${p2b[1] - p2a[1]} at zoom ${z.toFixed(2)})`, Math.abs(p2b[0] - p2a[0] - 80 / z) < 3 && Math.abs(p2b[1] - p2a[1] - 40 / z) < 3);
      // saved and honoured on load
      const sd = await savedDoc(page, ed);
      ok(`${tag}: saved with collapsed:true on the group`, sd.groups[0].collapsed === true);
      await page.reload(); await page.waitForSelector('.node'); await sleep(page, 900);
      ok(`${tag}: after a reload the group is still one block (the stored flag is honoured)`, (await groupGeo(page)).col && (await page.evaluate(() => getComputedStyle(document.querySelector('.node[data-id="m2"]')).display)) === 'none');
      // undo
      await page.click('.dfold'); await sleep(page, 300); ok(`${tag}: the fold button opens it again; its modules are back`, !(await groupGeo(page)).col && (await shown(page, node('m2'))));
      await page.mouse.move(700, 880); await page.keyboard.press('Control+z'); await sleep(page, 400);
      ok(`${tag}: Ctrl+Z undoes the expand (the block is back)`, (await groupGeo(page)).col);
      // Play
      await setPlay(page, true); await page.evaluate(() => document.querySelector('.dfold').click()); await sleep(page, 200); const tt = await toastText(page);
      ok(`${tag}: in Play the group does not fold or unfold ("${tt}")`, (await groupGeo(page)).col && /Play/.test(tt)); await setPlay(page, false);
      // a Mini module inside a collapsed group, and a module expanded again
      await loadDoc(page, base, ed, gdoc({ collapsed: true })); await page.click('.dfold'); await sleep(page, 300);
      ok(`${tag}: a patch saved with a collapsed group opens it as a block, and expanding shows the members where they were`, !(await groupGeo(page)).col && (await shown(page, node('m3'))));
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    done(errors, tag); await B.close();
  }
  note('M6 exposing chosen controls (macros)', 'not built: needs a design for what a macro knob drives; no engine change would be needed, but it is more than the group box');
  note('M7 waves bottom-panel mapping', 'not built: the host has a chains section only; waves needs a new section, routing to the waves engine and the editor half (see HANDOVER)');
}

// ================= build 587 loads a patch saved here =================
async function oldB() {
  const OLD = await baseBuild(); if (!OLD) return;
  for (const ed of ED) {
    const B = await bootEd(1440, 900, false); const { page, errors } = B; const tag = `${ed} old`;
    try {
      const d = SPEC[ed].doc({ groups: [GROUP(['m2', 'm3'], { collapsed: true })] }); d.modules[1].collapsed = true; d.modules[1].view = 'mini'; d.modules[4].collapsed = true; d.modules[5].collapsed = true; d.modules[5].view = 'mini';
      await loadDoc(page, OLD.base, ed, d);
      const st = await allStates(page);
      ok(`${tag}: build 587 loads a patch with Mini modules and a collapsed group without error (${st.length} modules)`, st.length === d.modules.length);
      ok(`${tag}: ...and shows a Mini module as Compact (${st[1]}), the group's members open (${await page.evaluate(() => getComputedStyle(document.querySelector('.node[data-id="m3"]')).display)})`, st[1] === 'compact' && st[5] === 'compact' && (await shown(page, node('m3'))));
      done(errors, tag);
      // and the patch this build saved loads in 587: make one here, save it, load the saved text there
      const B2 = await bootEd(1440, 900, false); await loadDoc(B2.page, B2.base, ed, SPEC[ed].doc());
      await B2.page.click(chev('m2'), { modifiers: ['Shift'] }); await B2.page.click(chev('m3')); await B2.page.click('body', { position: { x: 5, y: 880 } });
      const saved = await savedDoc(B2.page, ed); await B2.close();
      ok(`${tag}: the patch saved by this build has a Mini and a Compact module (${saved.modules[1].view}, ${saved.modules[2].collapsed})`, saved.modules[1].view === 'mini' && saved.modules[2].collapsed === true);
      await loadDoc(page, OLD.base, ed, saved); const st2 = await allStates(page);
      ok(`${tag}: build 587 loads it (Mini shows as Compact: ${st2.slice(0, 4).join(', ')})`, st2.length === saved.modules.length && st2[1] === 'compact' && st2[2] === 'compact');
      done(errors, tag + ' (saved here, loaded there)');
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
  OLD.close();
}

// ================= touch at 344 x 780 =================
async function touch() {
  for (const ed of ED) {
    const B = await bootEd(344, 780, true); const { page, errors, base } = B; const tag = `${ed} touch 344`;
    try {
      const decl = declaredWidths(ed) || {}, types = Object.keys(decl);
      const d = SPEC[ed].doc({ view: { x: 12, y: 12, zoom: 1, ui: 2, nw: 1 }, cables: [] });
      d.modules = [P('m1', types[0], 0, 0), P('m2', types[1], 0, 300), P('m3', types[4] || types[0], 0, 600), P('m4', 'delay', 0, 900)];
      await loadDoc(page, base, ed, d, { settings: { tips: false } });
      const fits = [];
      for (const id of ['m1', 'm2', 'm3']) { const r = await frameCheck(page, id), want = decl[d.modules.find(m => m.id === id).type]; fits.push(r.w >= want - 0.6 && r.w <= 344 - 12 && !r.bad.length && r.oneLine && r.whole); }
      ok(`${tag}: narrow modules keep their width (or their content's) and fit the screen at 100%, title on one line, nothing overflowing`, fits.every(Boolean), fits.join());
      const cb = await box(page, chev('m1')); const hb = await page.evaluate(() => { const b = document.querySelector('.node[data-id="m1"] > header .fold'); const c = getComputedStyle(b, '::before'); return [b.getBoundingClientRect().height, parseFloat(c.left), parseFloat(c.right)]; });
      await tap(page, cb.x, cb.y); await sleep(page, 250); ok(`${tag}: a tap on the chevron folds an open module to Compact`, (await stateOf(page, 'm1')) === 'compact');
      await hold(page, cb.x, cb.y, 760); await sleep(page, 250); ok(`${tag}: holding the chevron for half a second makes it Mini (and the tap that ends the hold does nothing more)`, (await stateOf(page, 'm1')) === 'mini');
      const mh = (await box(page, node('m1'))).h; ok(`${tag}: a Mini module is ${Math.round(mh)} px high on a touch screen`, mh >= 40 && mh <= 58);
      const cb2 = await box(page, chev('m1')); await tap(page, cb2.x, cb2.y); await sleep(page, 250); ok(`${tag}: a tap on a Mini module's chevron opens it`, (await stateOf(page, 'm1')) === 'open');
      const hd = await box(page, `${node('m2')} > header .t`); await tap(page, hd.x, hd.y); await tap(page, hd.x, hd.y); await sleep(page, 300);
      ok(`${tag}: a double-tap on a title folds it to Compact`, (await stateOf(page, 'm2')) === 'compact');
      await sleep(page, 700); await hold(page, hd.x, hd.y, 760); await sleep(page, 300);
      ok(`${tag}: a long press on a title still opens the module menu (not Mini)`, await page.evaluate(() => !document.getElementById('ctxmenu').hidden) && (await stateOf(page, 'm2')) === 'compact', await page.evaluate(() => String(document.getElementById('ctxmenu').hidden)) + ' ' + await stateOf(page, 'm2')); await page.keyboard.press('Escape'); await tap(page, 330, 770);
      // Mini header buttons are touchable
      await page.evaluate(() => { const f = document.querySelector('.node[data-id="m3"] > header .fold'); f.dispatchEvent(new MouseEvent('click', { bubbles: true, shiftKey: true })); }); await sleep(page, 250);
      const xb = await box(page, `${node('m3')} > header .x`); ok(`${tag}: a Mini module's close button is ${Math.round(xb.w)} x ${Math.round(xb.h)} px (32 to 40 wide)`, (await stateOf(page, 'm3')) === 'mini' && xb.w >= 30 && xb.w <= 42 && xb.h >= 36);
      // a group's fold button
      const g = SPEC[ed].doc({ view: { x: -300, y: 130, zoom: 0.8, ui: 2, nw: 1 }, groups: [GROUP(['m2', 'm3'], { bounds: [396, -54, 920, 380] })] });
      await loadDoc(page, base, ed, g); const fb = await box(page, '.dfold'); await tap(page, fb.x, fb.y); await sleep(page, 300);
      ok(`${tag}: the group's fold button is ${Math.round(fb.w)} x ${Math.round(fb.h)} px and a tap collapses the group`, fb.w / 0.8 >= 35 && fb.h / 0.8 >= 35 && (await groupGeo(page)).col, JSON.stringify([fb, await groupGeo(page)]));
    } catch (e) { ok(`${tag}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    done(errors, tag); await B.close();
  }
}

// ================= screenshots =================
async function shots() {
  const OUTD = process.env.SHOTS || '/home/claude/work/shots/pass5'; fs.mkdirSync(OUTD, { recursive: true });
  const skinCss = async page => { await page.addStyleTag({ content: `:root{--bench:#0b0710; --panel:#0b0710; --panel-2:#1c1224; --edge:#ff2d8a; --ink:#ff2d8a; --muted:#c070a0; --track:#1c1224; --side:#140c1b; --frame:#140c1b; --video:#39ff88; --audio:#39ff88; --audio-dark:#11a85a; --mod:#ff2d8a; --mod-dark:#a01058; --gate:#ff2d8a; --gate-dark:#a01058; --selc:#fff200}
    :is(.tb,.pgroup,.pitem,.numsmall,select,.btn,button,input[type=text],input[type=search],input[type=number],textarea){background:#0b0710!important; color:#ff2d8a; border-color:#ff2d8a!important}
    :is(.tb.on,button.on){background:#ff2d8a!important; color:#0b0710!important}` }); };
  const OLD = await baseBuild();
  const mixed = (ed, w, narrowView) => { const d = SPEC[ed].doc({ view: narrowView ? { x: 12, y: 12, zoom: 0.62, ui: 2, nw: 1 } : { x: 20, y: 20, zoom: 0.78, ui: 2, nw: 1 }, groups: [] });
    d.modules.push(P('m9', 'slew', 960, 900), P('m10', 'logic', 1240, 900), P('m11', 'quant', 700, 900)); d.cables.push(ed === 'chains' ? C('m8', 'out', 'm9', 'in') : C('m8', 'out', 'm9', 'in')); return d; };
  for (const ed of ED) for (const [w, h, mob] of [[1440, 900, false], [344, 780, true]]) for (const skin of ['white', 'idmpg']) {
    const B = await bootEd(w, h, mob); const { page, base } = B; const f = `${OUTD}/${ed}-${w}x${h}-${skin}`;
    try {
      const d = mixed(ed, w, mob);
      const load = async (doc, b) => { await loadDoc(page, b || base, ed, doc); if (skin === 'idmpg') { await skinCss(page); await sleep(page, 200); } };
      await load(d); await page.screenshot({ path: `${f}-1-mixed-widths-open.png` });
      await press(page, 'Alt+c'); await sleep(page, 400); await page.screenshot({ path: `${f}-2-all-compact.png` });
      await press(page, 'Alt+Shift+C'); await sleep(page, 400); await page.screenshot({ path: `${f}-3-all-mini.png` });
      const dg = SPEC[ed].doc({ view: mob ? { x: -120, y: 120, zoom: 0.5, ui: 2, nw: 1 } : { x: 20, y: 120, zoom: 0.7, ui: 2, nw: 1 }, groups: [GROUP(['m2', 'm3'])] });
      await load(dg); await page.screenshot({ path: `${f}-4-group-open.png` });
      await page.evaluate(() => document.querySelector('.dfold').click()); await sleep(page, 400); await page.screenshot({ path: `${f}-5-group-collapsed.png` });
      if (OLD) { await loadDoc(page, OLD.base, ed, d); if (skin === 'idmpg') { await skinCss(page); await sleep(page, 200); } await page.screenshot({ path: `${f}-0-before-build587.png` }); }
    } catch (err) { console.log('shot failed', f, err.message.split('\n')[0]); }
    await B.close();
  }
  if (OLD) OLD.close();
  console.log('screenshots in ' + OUTD);
}

const want = process.argv.slice(2);
const run = n => !want.length || want.includes(n);
(async () => {
  if (want.includes('shots')) { await shots(); return; }
  if (run('rf')) await rf();
  if (run('w')) await widths();
  if (run('mini')) await mini();
  if (run('grp')) await grp();
  if (run('old')) await oldB();
  if (run('touch')) await touch();
  const bad = T.rows ? T.rows.filter(r => r[1] === 'FAIL').length : 0;
  console.log(`\n${T.rows ? T.rows.length : '?'} checks, ${bad} FAIL`);
  try { T.print && T.print(); } catch (_) {}
  process.exit(bad ? 1 : 0);
})();
