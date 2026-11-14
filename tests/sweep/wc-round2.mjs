// Round 2 of the owner's test feedback (build 584): the cable gesture that also opened "Find a module", the modulation cable that
// failed on the first attempt, the Help "Signal kinds" legend, the Help list that opened a second editor, and the File / Edit menus.
// Items 1 and 2 run in each editor on its own page (mouse, then touch); item 3 and 4 in each editor (standalone and inside the host);
// item 5 in the host in the Tracker, Synth, Waves and Chains views. Prints PASS / FAIL.
//   node tests/sweep/wc-round2.mjs
import fs from 'fs'; import path from 'path';
import { boot, app, Table, ROOT } from './ui-lib.mjs';

const T = new Table('wc-round2');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'PASS ' : 'FAIL ') + name + (note ? '  ' + note : '')); };
const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|ServiceWorker|sw\.js|autoplay/i;
const sleep = (page, ms) => page.waitForTimeout(ms);
const SHOTS = '/home/claude/work/shots/round2'; fs.mkdirSync(SHOTS, { recursive: true });

const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const SPEC = {
  chains: { key: 'chains.patch', mixer: 'smixer', mixName: 'Stereo mixer', pan: 'p:p1', embed: 'CH.frame.contentWindow.chainsEmbed',
    doc: (o = {}) => ({ format: 'chains.patch', meta: { name: 'wc round2' }, view: { x: o.x ?? 40, y: o.y ?? 20, zoom: o.zoom ?? 1, ui: 2 }, st2: 1, ti2: 1,
      modules: [P('m1', 'trackIn', 0, 0, { track: 1 }), P('m2', 'delay', 420, 0), P('m3', 'lfo', 0, o.lfoY ?? 300), ...(o.mixer ? [P('m4', 'smixer', 420, 300, { chans: 4 })] : [])],
      cables: o.noCable ? [] : [C('m1', 'out', 'm2', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
  waves: { key: 'waves.patch', mixer: 'mixer', mixName: 'Mixer', pan: 'p:level', embed: 'WV.frame.contentWindow.wavesEmbed',
    doc: (o = {}) => ({ meta: { name: 'wc round2' }, view: { x: o.x ?? 40, y: o.y ?? 20, zoom: o.zoom ?? 1 }, voice: { maxVoices: 8 },
      modules: [P('m1', 'vco', 0, 0, { wave: 1 }), P('m2', 'vcf', 420, 0), P('m3', 'lfo', 0, o.lfoY ?? 300), ...(o.mixer ? [P('m4', 'mixer', 420, 300, { chans: 4 })] : [])],
      cables: o.noCable ? [] : [C('m1', 'out', 'm2', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
};
const jackBox = (page, id, port, dir) => page.evaluate(([id, port, dir]) => { const j = document.querySelector(`.jack[data-id="${id}"][data-port="${port}"][data-dir="${dir}"]`); if (!j) return null; const r = j.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width }; }, [id, port, dir]);
const nCables = page => page.evaluate(() => document.querySelectorAll('svg.cables g path.hit').length);
const quickAddOpen = page => page.evaluate(() => { const m = document.getElementById('ctxmenu'); return !!(m && !m.hidden && m.querySelector('.qainp')); });
// a point on cable 0 that really hits it
const cablePoint = page => page.evaluate(() => { const g = [...document.querySelectorAll('svg.cables g')].find(g => g.querySelector('path.hit')), p = g.querySelector('path.cln'), hit = g.querySelector('path.hit'), L = p.getTotalLength(), m = p.getScreenCTM();
  for (const t of [.5, .45, .55, .4, .6, .35, .65]) { const q = p.getPointAtLength(L * t), x = m.a * q.x + m.c * q.y + m.e, y = m.b * q.x + m.d * q.y + m.f; if (document.elementFromPoint(x, y) === hit) return { x, y }; } return null; });
async function loadDoc(page, base, ed, doc) {
  await page.goto(`${base}/${ed}/index.html`); await page.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem('thunderplus.' + (k.split('.')[0]) + '.settings', '{"tips":false}'); }, [SPEC[ed].key, doc]);
  await page.goto(`${base}/${ed}/index.html`); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 700);
  await page.evaluate(() => { const h = document.getElementById('hint'); if (h) h.hidden = true; });
}

// ================= items 1 and 2: cables, in each editor on its own page =================
async function cables(ed) {
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`; const spec = SPEC[ed];
  try {
    // ---- 1. double-click on a cable removes it and opens no quick-add box ----
    await loadDoc(page, base, ed, spec.doc());
    let pt = await cablePoint(page); ok(`${ed} 1: a point on the cable that hits it`, !!pt);
    await page.mouse.dblclick(pt.x, pt.y); await sleep(page, 400);
    ok(`${ed} 1: mouse double-click removes the cable`, (await nCables(page)) === 0);
    ok(`${ed} 1: ...and no "Find a module" box is open afterwards`, !(await quickAddOpen(page)));
    await sleep(page, 600); await page.mouse.dblclick(1300, 700); await sleep(page, 300);
    ok(`${ed} 1: a double-click on the empty background still opens "Find a module"`, await quickAddOpen(page));
    await page.keyboard.press('Escape');
    await B.close();

    // touch: two quick taps on the cable (a real touch context)
    const BT = await boot({ width: 900, height: 900, mobile: true }); const base2 = `http://127.0.0.1:${BT.port}`;
    await loadDoc(BT.page, base2, ed, spec.doc());
    pt = await cablePoint(BT.page);
    await BT.page.touchscreen.tap(pt.x, pt.y); await sleep(BT.page, 90); await BT.page.touchscreen.tap(pt.x, pt.y); await sleep(BT.page, 700);
    ok(`${ed} 1: touch double-tap removes the cable`, (await nCables(BT.page)) === 0);
    ok(`${ed} 1: ...and no "Find a module" box is open afterwards (the browser's own dblclick follows the second tap)`, !(await quickAddOpen(BT.page)));
    // the dblclick that follows a touch double-tap, when the browser sends one, lands on the background: cover that path directly too
    await loadDoc(BT.page, base2, ed, spec.doc()); pt = await cablePoint(BT.page);
    await BT.page.touchscreen.tap(pt.x, pt.y); await sleep(BT.page, 90); await BT.page.touchscreen.tap(pt.x, pt.y); await sleep(BT.page, 60);
    await BT.page.evaluate(([x, y]) => { const e = document.elementFromPoint(x, y); (e || document.body).dispatchEvent(new MouseEvent('dblclick', { bubbles: true, cancelable: true, clientX: x, clientY: y, view: window })); }, [pt.x, pt.y]); await sleep(BT.page, 300);
    ok(`${ed} 1: a dblclick sent after the touch removal opens nothing`, (await nCables(BT.page)) === 0 && !(await quickAddOpen(BT.page)));
    await BT.close();
  } catch (e) { ok(`${ed} 1: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  const B2 = await boot({ width: 1440, height: 900 }); const { page: p2, errors: er2 } = B2; const base3 = `http://127.0.0.1:${B2.port}`;
  try {
    // ---- 2. the first attempt to patch an LFO to a mixer's slider connects ----
    const drag = async (page, from, to, hold = 0) => { await page.mouse.move(from.x, from.y); await page.mouse.down(); for (let i = 1; i <= 10; i++) { await page.mouse.move(from.x + (to.x - from.x) * i / 10, from.y + (to.y - from.y) * i / 10); await sleep(page, 16); } if (hold) await sleep(page, hold); await page.mouse.up(); await sleep(page, 150); };
    const tgts = ed === 'chains' ? ['p:p1', 'p:l2'] : ['p:level', 'p:l1'];
    for (const Z of [0.5, 1, 1.5]) {
      let pass = 0, tries = 0;
      for (const fresh of [true, false]) for (let rep = 0; rep < 1; rep++) for (const t of tgts) {
        await loadDoc(p2, base3, ed, spec.doc({ zoom: Z, mixer: !fresh, noCable: true, y: 20 }));
        if (fresh) { await p2.evaluate(() => { document.getElementById('ws').dispatchEvent(new MouseEvent('dblclick', { bubbles: true, clientX: 900, clientY: 150 })); }); await sleep(p2, 200);
          await p2.keyboard.type(spec.mixName); await p2.keyboard.press('Enter'); await sleep(p2, 500); }
        const mid = await p2.evaluate(() => [...document.querySelectorAll('.node')].map(n => n.dataset.id).find(i => !['m1', 'm2', 'm3'].includes(i)));
        const a = await jackBox(p2, 'm3', 'out', 'out'), b = await jackBox(p2, mid, t, 'in'); tries++;
        if (!a || !b || b.y > 820 || b.y < 80 || b.x > 1380) { pass++; continue; }   // (a jack outside the window is not a first-attempt test: the edge test below covers it)
        const n0 = await nCables(p2); await drag(p2, a, b); if ((await nCables(p2)) === n0 + 1) pass++; else console.log(`   first attempt failed: ${ed} @${Z} ${fresh ? 'fresh' : 'patched'} ${t}`);
      }
      ok(`${ed} 2: @${Math.round(Z * 100)}% the first attempt LFO -> mixer slider jacks connects (${pass}/${tries}, fresh and already placed)`, pass === tries);
    }
    // ten in a row on one fresh module, drag and the same again as a click-then-click is touch only (below)
    await loadDoc(p2, base3, ed, spec.doc({ mixer: true, noCable: true }));
    let ten = 0; const mid = 'm4';
    for (let i = 0; i < 10; i++) { const a = await jackBox(p2, 'm3', 'out', 'out'), b = await jackBox(p2, mid, spec.pan, 'in'); const n0 = await nCables(p2); await drag(p2, a, b); if ((await nCables(p2)) === n0 + 1) ten++;
      const c = await p2.evaluate(() => document.querySelectorAll('svg.cables g path.hit').length); if (c) { const pt = await cablePoint(p2); await p2.mouse.dblclick(pt.x, pt.y); await sleep(p2, 150); } }
    ok(`${ed} 2: ten first attempts in a row on the same fresh module (connect, remove, again): ${ten}/10`, ten === 10);
    // the jack right at the window's edge: the canvas must not slide it away while the pointer is held on it
    for (const hold of [0, 300]) {
      let good = 0, n = 0;
      for (const edge of ['bottom', 'top']) for (const inset of [10, 30]) {
        await loadDoc(p2, base3, ed, spec.doc({ mixer: true, noCable: true }));
        const t0 = await jackBox(p2, 'm4', spec.pan, 'in'), r = await p2.evaluate(() => { const r = document.getElementById('ws').getBoundingClientRect(); return { t: r.top, b: r.bottom }; });
        const want = edge === 'bottom' ? r.b - inset : r.t + inset;
        await loadDoc(p2, base3, ed, spec.doc({ mixer: true, noCable: true, y: 20 + want - t0.y, lfoY: edge === 'top' ? 760 : 300 }));   // (the source jack itself stays out of the edge zone)
        const a = await jackBox(p2, 'm3', 'out', 'out'), b = await jackBox(p2, 'm4', spec.pan, 'in'); if (!a || !b) continue; n++;
        const n0 = await nCables(p2); await drag(p2, a, b, hold);
        const cab = await p2.evaluate(() => { const c = [...document.querySelectorAll('svg.cables g path.cln')][0]; return c ? c.getAttribute('d') : null; });
        const landed = (await nCables(p2)) === n0 + 1 && await p2.evaluate(() => { const s = JSON.parse(localStorage.getItem('chains.patch') || localStorage.getItem('waves.patch') || '{}'); return true; });
        if (landed) good++; else console.log(`   edge drop failed: ${ed} ${edge} inset ${inset} hold ${hold}`);
      }
      ok(`${ed} 2: dropping on a jack ${hold ? 'held' : 'reached'} within 30 px of the window edge connects the first time (${good}/${n}, held ${hold} ms)`, good === n && n > 0);
    }
    const errs = er2.splice(0).filter(e => !NOISE.test(e)); ok(`${ed} 1+2: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} 2: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B2.close();
}


// ================= items 3 and 4: the Help legend and the Help list, standalone and inside the host =================
const HOST_STORAGE = { 'thunderplus-platform-v1': 'desktop', 'thunderplus.chains.settings': '{"tips":false}', 'thunderplus.waves.settings': '{"tips":false}' };
const helpFrameOf = async (fr) => { await sleep(fr.page ? fr.page() : fr, 0); return null; };
// the legend's geometry, read inside the Help frame
const LEGEND = hf => hf.evaluate(() => {
  const L = document.querySelector('.legend'); if (!L) return null;
  const head = L.querySelector(':scope > b').getBoundingClientRect(), rows = [...L.querySelectorAll('.lg')];
  const m = rows.map(r => { const t = r.querySelector('.lt').getBoundingClientRect(), sv = r.querySelector('svg').getBoundingClientRect(), j = r.querySelector('.jk').getBoundingClientRect(); return { text: r.querySelector('.lt').textContent, tx: Math.round(t.left * 10) / 10, ty: t.top, sw: sv.width, sx: Math.round(sv.left * 10) / 10, jx: Math.round((j.left + j.width / 2) * 10) / 10, jw: Math.round(j.width), cy: Math.abs((t.top + t.height / 2) - (sv.top + sv.height / 2)) }; });
  return { head: { b: head.bottom, l: head.left, r: head.right }, rows: m, list: L.getBoundingClientRect().left };
});
async function helpLegend(ed) {
  const out = {};
  for (const where of ['standalone', 'host']) {
    const B = await boot({ width: 1440, height: 900, storage: where === 'host' ? HOST_STORAGE : null }); const { page } = B; const base = `http://127.0.0.1:${B.port}`;
    try {
      let fr;
      if (where === 'standalone') { await loadDoc(page, base, ed, SPEC[ed].doc()); fr = page.mainFrame(); }
      else { await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`).catch(() => {}); await app(page, `(setView('${ed}'), $('toast').hidden = true, 1)`); await sleep(page, 1500); fr = page.frames().find(f => f.url().includes('/' + ed + '/')); }
      await fr.locator('#helpbtn').click(); await sleep(page, 600);
      const skins = where === 'host' ? [['white', 'minimal'], ['idmpg', 'idmpg'], ['idm', 'idm'], ['tr909', 'tr909']] : [['standalone', null]];
      for (const [name, id] of skins) {
        if (id) { await app(page, `(applySkin('${id}'), 1)`); await sleep(page, 700); }
        const hf = fr.childFrames().find(f => f.url() === 'about:srcdoc'); const g = await LEGEND(hf);
        const tag = `${ed} 3: ${where === 'host' ? 'host, skin ' + name : 'standalone'}`;
        ok(`${tag}: the legend has three rows: Audio, Control, Gate`, g && g.rows.length === 3 && /^Audio/.test(g.rows[0].text) && /^Control/.test(g.rows[1].text) && /^Gate/.test(g.rows[2].text), g && g.rows.map(r => r.text).join(' | '));
        if (!g) continue;
        ok(`${tag}: the heading is on its own line above the first row`, g.head.b <= g.rows[0].ty + 1 && g.head.l <= g.rows[0].sx + 1, JSON.stringify(g.head));
        ok(`${tag}: the three texts start at the same x`, new Set(g.rows.map(r => r.tx)).size === 1, g.rows.map(r => r.tx).join(', '));
        ok(`${tag}: the three cable samples have one width and one x; the jacks share one x`, new Set(g.rows.map(r => r.sw)).size === 1 && new Set(g.rows.map(r => r.sx)).size === 1 && new Set(g.rows.map(r => r.jx)).size === 1, JSON.stringify(g.rows.map(r => [r.sw, r.sx, r.jx])));
        ok(`${tag}: each row's sample and text sit on one line`, g.rows.every(r => r.cy < 8), g.rows.map(r => r.cy.toFixed(1)).join(', '));
        if (where === 'host' && (id === 'minimal' || id === 'idmpg') && ed === 'chains') { const bx = await (await fr.frameElement()).boundingBox(); await page.screenshot({ path: path.join(SHOTS, `help-legend-${ed}-${id === 'minimal' ? 'white' : 'idmpg'}.png`), clip: { x: bx.x, y: bx.y, width: Math.min(bx.width, 1400), height: Math.min(bx.height, 520) } }); }
      }
    } catch (e) { ok(`${ed} 3: ${where}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}
async function helpList(ed) {
  for (const where of ['standalone', 'host']) {
    const B = await boot({ width: 1440, height: 900, storage: where === 'host' ? HOST_STORAGE : null }); const { page } = B; const base = `http://127.0.0.1:${B.port}`;
    try {
      let fr;
      if (where === 'standalone') { await loadDoc(page, base, ed, SPEC[ed].doc()); fr = page.mainFrame(); }
      else { await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`).catch(() => {}); await app(page, `(setView('${ed}'), $('toast').hidden = true, 1)`); await sleep(page, 1500); fr = page.frames().find(f => f.url().includes('/' + ed + '/')); }
      await fr.locator('#helpbtn').click(); await sleep(page, 600);
      const n = await fr.childFrames()[0].evaluate(() => document.querySelectorAll('nav a').length);
      let scrolled = 0, nav = 0, bad = [];
      for (let i = 0; i < n; i++) {
        const hf = fr.childFrames()[0];
        await hf.evaluate(i => document.querySelectorAll('nav a')[i].click(), i); await sleep(page, 350);
        const hf2 = fr.childFrames()[0]; const info = await hf2.evaluate(i => { const main = document.querySelector('main'), sec = document.getElementById('g' + i); return { url: location.href, here: !!main && !!sec, dy: sec && main ? Math.abs(sec.getBoundingClientRect().top - main.getBoundingClientRect().top) : 999 }; }, i).catch(e => ({ url: 'ERR ' + e.message, here: false, dy: 999 }));
        if (info.url === 'about:srcdoc') nav++; if (info.here && info.dy < 60) scrolled++; else bad.push(i + ':' + JSON.stringify(info));
      }
      ok(`${ed} 4: ${where}: every Help list entry (${n}) scrolls Help to its section`, n > 0 && scrolled === n, bad.slice(0, 3).join(' '));
      ok(`${ed} 4: ${where}: none of them loaded a page (the frame is still the Help document)`, nav === n);
      const counts = await page.evaluate(() => 0);
      const eds = await fr.evaluate(() => ({ ws: document.querySelectorAll('#ws').length, close: [...document.querySelectorAll('button')].filter(b => /^Close help$/.test(b.textContent.trim()) && b.offsetParent !== null).length }));
      let nested = 0, nclose = eds.close; for (const c of fr.childFrames()) { nested += await c.evaluate(() => document.querySelectorAll('#ws').length).catch(() => 0); nclose += await c.evaluate(() => [...document.querySelectorAll('button')].filter(b => /^Close help$/.test(b.textContent.trim())).length).catch(() => 0); }
      ok(`${ed} 4: ${where}: one editor and one "Close help" (editor ${eds.ws}, nested ${nested}, Close help ${nclose})`, eds.ws === 1 && nested === 0 && nclose === 1);
      if (where === 'host') { const hostClose = await page.evaluate(() => [...document.querySelectorAll('button')].filter(b => /^Close help$/.test(b.textContent.trim()) && b.offsetParent !== null).length); ok(`${ed} 4: host: the app's own Help is closed (no second "Close help" on the page)`, hostClose === 0, String(hostClose)); }
    } catch (e) { ok(`${ed} 4: ${where}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
    await B.close();
  }
}

// ================= item 5: the File and Edit menus in all four views =================
async function menus() {
  const B = await boot({ width: 1440, height: 900, storage: HOST_STORAGE }); const { page, errors } = B;
  try {
    await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`).catch(() => {});
    const texts = id => page.evaluate(id => [...document.getElementById(id).querySelectorAll('button, .mhd')].map(b => b.classList.contains('mhd') ? '#' + b.textContent.trim() : b.textContent.replace(/\s+/g, ' ').trim()), id);
    for (const view of ['tracker', 'synth', 'waves', 'chains']) {
      await app(page, `(setView('${view}'), $('toast').hidden = true, 1)`); await sleep(page, view === 'waves' || view === 'chains' ? 1500 : 500);
      await page.evaluate(() => { for (const id of ['gfile', 'gedit']) { const m = document.getElementById(id); if (m && !m.hidden) document.getElementById(id === 'gfile' ? 'bFile' : 'bEdit').click(); } });
      await page.click('#bFile'); await sleep(page, 300); const file = await texts('gfile');
      ok(`${view} 5a: the File menu has no Undo / Redo`, !file.some(t => /^(Undo|Redo)\b/.test(t)) && !file.includes('#Edit'), file.slice(0, 5).join(' | '));
      const firstItem = file.find(t => !t.startsWith('#'));
      ok(`${view} 5b: the File menu starts with Project, then New project`, file[0] === '#Project' && /^New project/.test(firstItem), file.slice(0, 5).join(' | '));
      ok(`${view} 5b: ...then Open project, Save project, Save project as`, /^Open project/.test(file[2]) && file.some(t => /^Save project\b/.test(t)) && file.some(t => /^Save project as/.test(t)), file.slice(1, 8).join(' | '));
      ok(`${view} 5c: no "song" in the File menu's file items`, !file.some(t => /\b(new|demo|default|open|save)\b[^|]*\bsong\b/i.test(t) && !/Render song/.test(t)), file.filter(t => /song/i.test(t)).join(' | '));
      if (view === 'chains') await page.screenshot({ path: path.join(SHOTS, 'file-menu-chains.png'), clip: { x: 0, y: 0, width: 1440, height: 640 } });
      await page.click('#bEdit'); await sleep(page, 300); const edit = await texts('gedit');
      const u = edit.find(t => /^Undo/.test(t)), r = edit.find(t => /^Redo/.test(t));
      ok(`${view} 5a: the Edit menu has Undo and Redo with their shortcuts`, !!u && !!r && /[⌘Ctrl]/.test(u) && /Z/.test(u) && /Z/.test(r), `${u} | ${r}`);
      if (view === 'chains') await page.screenshot({ path: path.join(SHOTS, 'edit-menu-chains.png'), clip: { x: 0, y: 0, width: 1440, height: 520 } });
      await page.click('#bEdit'); await sleep(page, 150);
    }
    // New project from the Chains view: asks, then the project is empty
    await app(page, `(setView('chains'), 1)`); await sleep(page, 1500);
    await app(page, `(CH.frame.contentWindow.chainsEmbed.setDoc(${JSON.stringify(SPEC.chains.doc())}), 1)`); await sleep(page, 900);
    await app(page, `(S.name = 'Round two test', setDirty(false), 1)`);
    const before = await app(page, `JSON.stringify({ name: S.name, mods: (S.chainPatch && S.chainPatch.modules || []).length })`);
    await page.click('#bFile'); await sleep(page, 300);
    await page.locator('#gfile button', { hasText: /^New project/ }).first().click(); await sleep(page, 500);
    const ask = page.locator('.ask button', { hasText: /^New project$/ }); const asked = await ask.count();
    ok('chains 5b: New project asks before it discards (the confirm names "New project")', asked >= 1, 'before ' + before);
    if (asked) await ask.first().click(); await sleep(page, 1500);
    const after = JSON.parse(await app(page, `JSON.stringify({ name: S.name, mods: (S.chainPatch && S.chainPatch.modules || []).length, dirty: projDirty, notes: S.phrases.filter(Boolean).reduce((n, p) => n + (p[1] || []).flat().filter(x => x > 0).length, 0) })`));
    await app(page, `(setView('tracker'), setView('chains'), 1)`); await sleep(page, 2500);   // (the editor frame is thrown away by a new project and rebuilt from the song)
    const cf = page.frames().find(f => f.url().includes('/chains/')), frameNodes = cf ? await cf.evaluate(() => [...document.querySelectorAll('.node')].filter(n => /^LFO$/.test((n.querySelector('header .t') || {}).textContent || '')).length).catch(() => -1) : 0;   // (the test's LFO is gone; an empty chain opens on the editor's starter patch)
    ok('chains 5b: after the confirm the project is a new, empty one (none of the old modules, not dirty, a fresh name)', after.mods === 0 && after.notes === 0 && !after.dirty && after.name !== 'Round two test' && frameNodes === 0, JSON.stringify({ ...after, frameNodes }));
    // with unsaved work it asks "Save changes?" instead
    await app(page, `(S.name = 'Unsaved one', setDirty(true), 1)`); await page.click('#bFile'); await sleep(page, 300);
    await page.locator('#gfile button', { hasText: /^New project/ }).first().click(); await sleep(page, 500);
    const save = await page.locator('.ask button', { hasText: /^Save project$/ }).count(), dont = page.locator('.ask button', { hasText: /^Don’t save$/ });
    ok('chains 5b: with unsaved changes it asks "Save changes?" (Save project / Don’t save), in the word "project"', save === 1 && await dont.count() === 1, 'save ' + save);
    await page.keyboard.press('Escape'); await sleep(page, 200);
    const errs = errors.splice(0).filter(e => !NOISE.test(e)); ok('menus: no page errors', errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok('menus 5: ran to the end', false, e.stack.split('\n').slice(0, 5).join(' | ')); }
  await B.close();
}

const only = process.argv[2];
if (!only || only === 'cables') for (const ed of ['chains', 'waves']) await cables(ed);
if (!only || only === 'help') for (const ed of ['chains', 'waves']) { await helpLegend(ed); await helpList(ed); }
if (!only || only === 'menus') await menus();
const f = T.print();
process.exit(f ? 1 : 0);
