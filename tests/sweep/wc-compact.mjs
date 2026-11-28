// Pass 1a of the …waves / …chains UX rework (docs/audits/2026-11-13-waves-chains-ux.md, C1 to C9): the Compact collapsed state.
// Opens each editor on its own page (no app around it) in headless Chrome and checks the collapsed look: summary values and
// when they are written, heights, cable ends after collapse / expand / drag / zoom, collapse all and expand all as one undo
// step each, the touch double-tap, the chevron label, an old patch with collapsed:true, save then load, no page errors.
//   node tests/sweep/wc-compact.mjs            (set OLD_ROOT=/path/to/older/checkout to also load a saved patch in that build)
import http from 'http'; import fs from 'fs'; import path from 'path';
import { boot, app, Table, ROOT, serveDir, listenLoopback } from './ui-lib.mjs';

const T = new Table('wc-compact');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'ok   ' : 'FAIL ') + name + (note ? '  ' + note : '')); };

// ---- the two test patches: 8 modules, one port-heavy (a mixer with four inputs patched), two with modulation cables in ----
const P = (id, type, x, y, params = {}, collapsed = false) => ({ id, type, scope: 'mono', pos: [x, y], collapsed, params });
const C = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const SPEC = {
  chains: {
    key: 'chains.patch', heavy: 'm4', modded: 'm2', ioId: 'm1', ioSummary: /Track 1/, notIoTitle: 'Track In',
    doc: () => ({
      format: 'chains.patch', meta: { name: 'wc test' }, view: { x: 0, y: 0, zoom: 1, ui: 2 }, st2: 1, ti2: 1,
      modules: [P('m1', 'trackIn', 24, 40, { track: 1 }), P('m2', 'delay', 330, 40), P('m3', 'reverb', 660, 40), P('m4', 'smixer', 330, 420, { chans: 4 }),
        P('m5', 'lfo', 24, 330), P('m6', 'chainOut', 990, 40, { track: 1, level: 1 }), P('m7', 'vcf', 660, 420), P('m8', 'adsr', 24, 600)],
      cables: [C('m1', 'out', 'm2', 'in'), C('m1', 'outR', 'm2', 'inR'), C('m2', 'out', 'm3', 'in'), C('m2', 'outR', 'm3', 'inR'),
        C('m3', 'out', 'm4', 'in1'), C('m3', 'outR', 'm4', 'in1R'), C('m2', 'out', 'm4', 'in2'), C('m2', 'outR', 'm4', 'in2R'),
        C('m4', 'out', 'm6', 'in'), C('m4', 'outR', 'm6', 'inR'),
        C('m5', 'out', 'm2', 'p:tone'), C('m5', 'out', 'm2', 'p:mix'), C('m8', 'out', 'm7', 'p:cutoff'), C('m5', 'out', 'm3', 'p:mix')],
      groups: [], custom: [], transport: { tempo: 120 }
    }),
  },
  waves: {
    key: 'waves.patch', heavy: 'm5', modded: 'm4', ioId: 'm6', ioSummary: /Waves In 1/, notIoTitle: 'Out',
    doc: () => ({
      meta: { name: 'wc test' }, view: { x: 0, y: 0, zoom: 1 }, voice: { maxVoices: 8 },
      modules: [P('m1', 'vco', 24, 40, { wave: 1 }), P('m2', 'vcf', 330, 40), P('m3', 'vca', 660, 40), P('m4', 'delay', 330, 330), P('m5', 'mixer', 660, 330, { chans: 4 }),
        P('m6', 'out', 990, 40, { slot: 1, level: 1 }), P('m7', 'lfo', 24, 330), P('m8', 'adsr', 24, 560)],
      cables: [C('m1', 'out', 'm2', 'in'), C('m2', 'out', 'm3', 'in'), C('m3', 'out', 'm4', 'in'), C('m1', 'out', 'm5', 'in1'), C('m2', 'out', 'm5', 'in2'),
        C('m3', 'out', 'm5', 'in3'), C('m4', 'out', 'm5', 'in4'), C('m5', 'out', 'm6', 'in'),
        C('m7', 'out', 'm4', 'p:tone'), C('m7', 'out', 'm4', 'p:mix'), C('m8', 'out', 'm3', 'cv'), C('m8', 'out', 'm2', 'p:cutoff')],
      groups: [], custom: [], transport: { tempo: 120 }
    }),
  },
};

const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|ServiceWorker|sw\.js|autoplay/i;
const sleep = (page, ms) => page.waitForTimeout(ms);

// ---- page helpers (run inside the editor page; its script is a module, so everything goes through the DOM) ----
const PROBE = `(() => {
  const world = document.querySelector('.world'), m = new DOMMatrix(getComputedStyle(world).transform), wr = world.getBoundingClientRect();
  const nodes = {}; for (const el of document.querySelectorAll('.node')) nodes[el.dataset.id] = { l: el.offsetLeft, t: el.offsetTop, w: el.offsetWidth, h: el.offsetHeight, collapsed: el.classList.contains('collapsed') };
  const cab = [...document.querySelectorAll('svg.cables g')].filter(g => g.querySelector('path.cln')).map(g => { const d = g.querySelector('path.cln').getAttribute('d') || ''; const pts = d.match(/-?[\\d.]+,-?[\\d.]+/g) || [];
    const q = s => s ? s.split(',').map(Number) : null; return { a: q(pts[0]), b: q(pts[pts.length - 1]), shown: g.style.display !== 'none' }; });
  return { nodes, cab, zoom: m.a, ox: wr.left, oy: wr.top };
})()`;

async function state(page) { return page.evaluate(PROBE); }
async function load(page, base, ed, doc) {
  await page.goto(`${base}/${ed}/index.html`); await page.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); }, [SPEC[ed].key, doc]);
  await page.goto(`${base}/${ed}/index.html`); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 700);
}
const ids = doc => doc.modules.map(m => m.id);
const fold = (page, id) => page.click(`.node[data-id="${id}"] > header .fold`);
const collapsedIds = async page => page.evaluate(() => [...document.querySelectorAll('.node.collapsed')].map(e => e.dataset.id).sort());
const bgPoint = async page => page.evaluate(() => { const r = document.getElementById('ws') ? document.getElementById('ws').getBoundingClientRect() : document.querySelector('.ws').getBoundingClientRect(); return { x: Math.round(r.right - 30), y: Math.round(r.bottom - 30) }; });

// every cable end lies on the box of its own module (a few px of slack for the jack that sits on the edge)
// (a cable whose `shown` flag is false is hidden and keeps its last path: it fails, unless hiddenOk says hidden is the state expected)
function endsOnBoxes(s, doc, slack = 3, hiddenOk = false) {
  const bad = [];
  doc.cables.forEach((c, i) => {
    const e = s.cab[i]; if (!e || !e.a || !e.b) { bad.push(`cable ${i} has no path`); return; }
    if (e.shown === false && !hiddenOk) { bad.push(`cable ${i} is hidden`); return; }
    for (const [pt, id] of [[e.a, c.from[0]], [e.b, c.to[0]]]) {
      const n = s.nodes[id]; if (!n) { bad.push(`no module ${id}`); continue; }
      if (pt[0] < n.l - slack || pt[0] > n.l + n.w + slack || pt[1] < n.t - slack || pt[1] > n.t + n.h + slack) bad.push(`cable ${i} end ${pt.map(Math.round)} off ${id} box ${n.l},${n.t} ${n.w}x${n.h}`);
    }
  });
  return bad;
}

async function run(ed) {
  const spec = SPEC[ed], doc = spec.doc(), MD = spec.modded;
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  try {
    await load(page, base, ed, doc);
    await page.click('#zoomfit'); await sleep(page, 500); await page.evaluate(m => { window.MD = m; }, MD);
    ok(`${ed}: patch opens with ${doc.modules.length} modules`, (await page.$$('.node')).length === doc.modules.length);
    ok(`${ed}: nothing collapsed to start`, (await collapsedIds(page)).length === 0);
    const open = await state(page);
    ok(`${ed}: cable ends on their boxes (open)`, endsOnBoxes(open, doc).length === 0, endsOnBoxes(open, doc).join('; '));

    // C9: the chevron's label
    const lab0 = await page.getAttribute(`.node[data-id="${MD}"] > header .fold`, 'aria-label');
    await fold(page, MD); await sleep(page, 200);
    const lab1 = await page.getAttribute(`.node[data-id="${MD}"] > header .fold`, 'aria-label'), tt1 = await page.getAttribute(`.node[data-id="${MD}"] > header .fold`, 'title');
    ok(`${ed} C9: chevron label says Collapse, then Expand`, /^Collapse /.test(lab0) && /^Expand /.test(lab1) && /^Expand/.test(tt1), `${lab0} / ${lab1} / ${tt1}`);

    // C1: summary values
    const sum = async id => page.evaluate(i => { const s = document.querySelector(`.node[data-id="${i}"] > .csum`); return s ? { text: s.querySelector('.cs-t').innerText, shown: getComputedStyle(s).display !== 'none', n: s.querySelectorAll('.cs-i').length } : null; }, id);
    const s2 = await sum(MD);
    ok(`${ed} C1: a collapsed module shows its summary (${JSON.stringify(s2 && s2.text)})`, s2 && s2.shown && s2.n >= 2 && /ms|s\b/.test(s2.text));
    ok(`${ed} C1 (583): the default summary leads with the wet/dry Mix`, /^Mix /.test(s2.text), s2.text);
    // the summary reads the way the sliders do
    const same = await page.evaluate(i => { const g = k => document.querySelector(`.node[data-id="${i}"] .row[data-key="${k}"] .val`).textContent; return { time: g('time') }; }, MD);
    ok(`${ed} C1: summary shows the sliders' own readout`, s2.text.includes(same.time), `${same.time} in ${s2.text}`);

    // updates when the parameter changes, and is not rewritten when it does not
    const setRange = (id, key, frac) => page.evaluate(([i, k, f]) => { const inp = document.querySelector(`.node[data-id="${i}"] .row[data-key="${k}"] input[type=range]`);
      const lo = +inp.min, hi = +inp.max; inp.value = f === 'same' ? inp.value : lo + (hi - lo) * f; inp.dispatchEvent(new Event('input', { bubbles: true })); return document.querySelector(`.node[data-id="${i}"] .row[data-key="${k}"] .val`).textContent; }, [id, key, frac]);
    await page.evaluate(i => { const s = document.querySelectorAll(`.node[data-id="${i}"] > .csum .cs-i`)[1]; window.__mut = 0; window.__mo = new MutationObserver(l => { window.__mut += l.length; }); window.__mo.observe(s, { subtree: true, childList: true, characterData: true, attributes: true }); }, MD);
    await setRange(MD, 'reso', .9); await sleep(page, 650);   // not a summary value (and past the 200 ms modulation repaint)
    const mutOther = await page.evaluate(() => window.__mut);
    ok(`${ed} C1: a parameter outside the summary does not touch it`, mutOther === 0, 'mutations ' + mutOther);
    const ro = await setRange(MD, 'time', .77); await sleep(page, 100);
    const s2b = await sum(MD), mutChange = await page.evaluate(() => window.__mut);
    ok(`${ed} C1: summary follows a changed parameter (${ro})`, s2b.text.includes(ro) && s2b.text !== s2.text && mutChange > 0, s2b.text);
    await page.evaluate(() => { window.__mut = 0; });
    await setRange(MD, 'time', 'same'); await sleep(page, 650);
    ok(`${ed} C1: the same value again writes nothing`, (await page.evaluate(() => window.__mut)) === 0);
    await page.evaluate(() => window.__mo.disconnect());

    // C3 / C8: stubs, "+N"
    const stubs = await page.evaluate(() => [...document.querySelectorAll('.node[data-id="' + MD + '"] .port.stub')].map(r => ({ t: r.textContent.trim(), w: r.querySelector('.jack').getBoundingClientRect().width })));
    ok(`${ed} C3: one labelled stub per modulated parameter`, stubs.length === 2 && stubs.every(s => s.t && s.w > 0) && stubs.map(s => s.t).join() !== '', JSON.stringify(stubs));
    const live = await page.evaluate(() => [...document.querySelectorAll('.node[data-id="' + MD + '"] > .csum .cs-i.modded')].length);
    ok(`${ed} C8: a modulated value is marked on the summary line`, live >= 1, 'modded items ' + live);
    const more = await page.evaluate(() => { const n = document.querySelector('.node[data-id="' + MD + '"]'); const b = n.querySelector('.mk.more'); return { txt: b && b.textContent, free: n.querySelectorAll('.port.free').length, hiddenAll: [...n.querySelectorAll('.port.free')].every(p => getComputedStyle(p).display === 'none') }; });
    ok(`${ed} C2: unpatched ports are hidden and counted`, more.free > 0 ? more.txt === '+' + more.free && more.hiddenAll : true, JSON.stringify(more));
    await sleep(page, 300);
    const one = await state(page);
    ok(`${ed} C3: cable ends on their boxes (one collapsed)`, endsOnBoxes(one, doc).length === 0, endsOnBoxes(one, doc).join('; '));
    const stubEnds = await page.evaluate(() => { const world = document.querySelector('.world'), m = new DOMMatrix(getComputedStyle(world).transform), wr = world.getBoundingClientRect(), out = [];
      for (const j of document.querySelectorAll('.node[data-id="' + MD + '"] .port.stub .jack')){ const r = j.getBoundingClientRect(); out.push([(r.left + r.width / 2 - wr.left) / m.a, (r.top + r.height / 2 - wr.top) / m.a]); } return out; });
    const toStubs = doc.cables.map((c, i) => [c, i]).filter(([c]) => c.to[0] === spec.modded && c.to[1].startsWith('p:')).map(([, i]) => one.cab[i].b);
    ok(`${ed} C3: modulation cables end on the stubs`, toStubs.length > 0 && toStubs.every(p => stubEnds.some(q => Math.hypot(p[0] - q[0], p[1] - q[1]) < 2.5)), JSON.stringify({ toStubs, stubEnds }));
    // "+N" opens the module
    if (more.free > 0) { await page.click('.node[data-id="' + MD + '"] .mk.more'); await sleep(page, 150);
      ok(`${ed} C2: clicking +N opens the module`, !(await page.evaluate(() => document.querySelector('.node[data-id="' + MD + '"]').classList.contains('collapsed')))); await fold(page, MD); await sleep(page, 150); }

    // C6: I/O titles and summaries
    await fold(page, spec.ioId); await sleep(page, 150);
    const io = await page.evaluate(i => ({ title: document.querySelector(`.node[data-id="${i}"] > header .t`).textContent, text: document.querySelector(`.node[data-id="${i}"] > .csum .cs-t`).innerText }), spec.ioId);
    ok(`${ed} C6: the I/O title stays short, the summary carries the track`, io.title === spec.notIoTitle && spec.ioSummary.test(io.text), JSON.stringify(io));
    await fold(page, spec.ioId);

    // C7: level strip on a module with an audio output
    ok(`${ed} C7: a module with an audio output has a level strip`, (await page.$$('.node[data-id="m2"] > .csum .clvl b')).length === 1);

    // C4: collapse all by the background menu, one undo step; expand all by Alt+C, one undo step
    const bp = await bgPoint(page);
    const before = await collapsedIds(page);
    await page.mouse.click(bp.x, bp.y, { button: 'right' }); await sleep(page, 150);
    await page.click('#ctxmenu .ctxitem:has-text("Collapse all modules")'); await sleep(page, 400);
    const all = await collapsedIds(page);
    ok(`${ed} C4: Collapse all collapses every module`, all.length === doc.modules.length, all.join());
    const afterAll = await state(page);
    ok(`${ed} C4: cable ends on their boxes (all collapsed)`, endsOnBoxes(afterAll, doc).length === 0, endsOnBoxes(afterAll, doc).join('; '));
    const heights = Object.fromEntries(Object.entries(afterAll.nodes).map(([k, v]) => [k, Math.round(v.h)]));
    const stubCount = Object.fromEntries(doc.modules.map(m => [m.id, new Set(doc.cables.filter(c => c.to[0] === m.id && c.to[1].startsWith('p:')).map(c => c.to[1])).size]));
    const bad = Object.entries(heights).filter(([k, h]) => h < 64 || h > (stubCount[k] ? 140 : 110));   // (build 583: the modulation stubs have rows of their own under the inputs: up to 140 px with them)
    ok(`${ed} C2: collapsed heights are 64 to 110 px (140 with modulation stubs)`, bad.length === 0, JSON.stringify(heights));
    await page.keyboard.press('Control+z'); await sleep(page, 400);
    const afterUndo = await collapsedIds(page);
    ok(`${ed} C4: Collapse all is one undo step`, JSON.stringify(afterUndo) === JSON.stringify(before), `${afterUndo.join()} vs ${before.join()}`);
    await page.keyboard.press('Alt+c'); await sleep(page, 200);
    ok(`${ed} C4: Alt+C collapses all (some were open)`, (await collapsedIds(page)).length === doc.modules.length);
    await page.keyboard.press('Alt+c'); await sleep(page, 200);
    ok(`${ed} C4: Alt+C again expands all`, (await collapsedIds(page)).length === 0);
    await page.keyboard.press('Control+z'); await sleep(page, 300);
    ok(`${ed} C4: Expand all is one undo step`, (await collapsedIds(page)).length === doc.modules.length);
    // the selection menu
    await page.keyboard.press('Alt+c'); await sleep(page, 200);   // all open again (the undo above put them back collapsed)
    const openAgain = (await collapsedIds(page)).length === 0;
    await page.click('.node[data-id="m3"] > header .t'); await page.mouse.click(0, 0);
    await page.click('.node[data-id="m3"] > header .t', { button: 'right' }); await sleep(page, 150);
    const hasSel = await page.$('#ctxmenu .ctxitem:has-text("Collapse this module")');
    if (hasSel) { await hasSel.click(); await sleep(page, 200); }
    ok(`${ed} C4: the module menu collapses just that module`, openAgain && !!hasSel && (await collapsedIds(page)).join() === 'm3', (await collapsedIds(page)).join());
    await fold(page, 'm3'); await sleep(page, 100);

    // heights for the report: open and collapsed, five modules
    const hOpen = open.nodes, report = ['m1', 'm2', 'm3', spec.heavy, 'm8'].map(k => `${k} ${Math.round(hOpen[k].h)}->${heights[k]}`).join('  ');
    console.log(`     heights open->compact (${ed}): ${report}`);

    // drag a collapsed module, zoom: cable ends stay on the boxes
    await page.keyboard.press('Alt+c'); await sleep(page, 300);
    const hdr = await page.evaluate(() => { const r = document.querySelector('.node[data-id="m3"] > header .t').getBoundingClientRect(); return { x: r.left + 20, y: r.top + r.height / 2 }; });
    const pos0 = (await state(page)).nodes.m3;
    await page.mouse.move(hdr.x, hdr.y); await page.mouse.down(); await page.mouse.move(hdr.x + 60, hdr.y + 40, { steps: 6 }); await page.mouse.up(); await sleep(page, 400);
    const afterDrag = await state(page);
    ok(`${ed}: a collapsed module drags (${pos0.l},${pos0.t} to ${afterDrag.nodes.m3.l},${afterDrag.nodes.m3.t}) and its cables follow`, Math.abs(afterDrag.nodes.m3.l - pos0.l) > 20 && endsOnBoxes(afterDrag, doc).length === 0, endsOnBoxes(afterDrag, doc).join('; '));
    await page.click('#zoomin'); await page.click('#zoomin'); await sleep(page, 500);
    const zin = await state(page);
    ok(`${ed}: cable ends on their boxes after zooming in (${zin.zoom.toFixed(2)})`, zin.zoom > afterDrag.zoom && endsOnBoxes(zin, doc).length === 0, endsOnBoxes(zin, doc).join('; '));
    await page.click('#zoomout'); await page.click('#zoomout'); await page.click('#zoomout'); await sleep(page, 500);
    const zout = await state(page);
    ok(`${ed}: ... and zooming out (${zout.zoom.toFixed(2)})`, zout.zoom < zin.zoom && endsOnBoxes(zout, doc).length === 0, endsOnBoxes(zout, doc).join('; '));
    await page.keyboard.press('Alt+c'); await sleep(page, 400);   // expand all
    const ex = await state(page);
    ok(`${ed}: cable ends on their boxes after expanding all`, endsOnBoxes(ex, doc).length === 0, endsOnBoxes(ex, doc).join('; '));

    // save then load: collapse two modules, wait for the autosave, check what was stored, reopen
    await fold(page, 'm3'); await fold(page, 'm5'); await sleep(page, 2200);
    const saved = await page.evaluate(k => JSON.parse(localStorage.getItem(k)), spec.key);
    const sm = Object.fromEntries(saved.modules.map(m => [m.id, m]));
    ok(`${ed}: save writes collapsed:true and no new field`, sm.m3.collapsed === true && sm.m5.collapsed === true && sm.m1.collapsed === false && saved.modules.every(m => Object.keys(m).sort().join() === 'collapsed,id,params,pos,scope,type'), Object.keys(sm.m3).join());
    await page.reload(); await page.waitForSelector('.node'); await sleep(page, 700);
    ok(`${ed}: load puts the two modules back in Compact`, (await collapsedIds(page)).join() === 'm3,m5' && !!(await sum('m3')).shown && (await sum('m3')).text.length > 0);
    if (process.env.OLD_ROOT) {   // the previous build opens a patch saved by this one
      const srv = serveDir(process.env.OLD_ROOT); await listenLoopback(srv); const ob = `http://127.0.0.1:${srv.address().port}`;
      await page.goto(`${ob}/${ed}/index.html`); await page.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); }, [spec.key, saved]);
      await page.goto(`${ob}/${ed}/index.html`); await page.waitForSelector('.node'); await sleep(page, 700);
      ok(`${ed}: the previous build loads a patch saved by this one`, (await collapsedIds(page)).join() === 'm3,m5' && (await page.$$('.node')).length === doc.modules.length);
      srv.close();
    }

    // an old patch: collapsed:true and nothing else
    const old = spec.doc(); old.modules[1].collapsed = true; old.modules[3].collapsed = true; old.modules[1].view = 'tiny';   // a view this build does not know reads as Compact ('mini' is Mini since pass 5)
    await load(page, base, ed, old);
    const oc = await collapsedIds(page), s1 = await sum('m2');
    ok(`${ed}: an old patch with collapsed:true opens as Compact`, oc.join() === 'm2,m4' && s1 && s1.shown && s1.text.length > 0, oc.join() + ' ' + JSON.stringify(s1));

    // review fix (13 Nov): Sidechain Out has no outputs, but a collapsed one still gets the level strip (chains has the module; waves does not)
    if (ed === 'chains') {
      await load(page, base, ed, { ...doc, modules: [{ id: 'sc1', type: 'scout', scope: 'mono', pos: [40, 40], collapsed: true, params: {} }], cables: [], groups: [] });
      const sc = await page.evaluate(() => { const n = document.querySelector('.node[data-id="sc1"]'), c = n && n.querySelector('.csum .clvl'), r = c && c.getBoundingClientRect(); return { node: !!n, strip: !!c, h: r ? Math.round(r.height) : 0, w: r ? Math.round(r.width) : 0 }; });
      ok(`${ed}: a collapsed Sidechain Out has the level strip (${JSON.stringify(sc)})`, sc.node && sc.strip && sc.h > 0 && sc.w > 20);
    }

    // no page errors
    const errs = errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed}: no page errors (desktop)`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed}: ran to the end`, false, e.stack.split('\n').slice(0, 3).join(' | ')); }
  await B.close();

  // ---- touch: double-tap on the title bar, the wider chevron, at 344 x 780 ----
  const M = await boot({ width: 344, height: 780, mobile: true }); const mp = M.page; const mb = `http://127.0.0.1:${M.port}`;
  try {
    const tdoc = spec.doc(); tdoc.modules.find(m => m.id === 'm3').pos = [20, 120];
    await load(mp, mb, ed, tdoc);
    const hp = await mp.evaluate(() => { const r = document.querySelector('.node[data-id="m3"] > header .t').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, vis: r.bottom < innerHeight && r.top > 0 }; });
    if (!hp.vis) { await mp.evaluate(() => document.querySelector('.node[data-id="m3"]').scrollIntoView()); }
    // bring the module into view: pan the patch with the view-fit button if there is one, else the zoom field
    if (!hp.vis) await mp.evaluate(() => { const f = document.getElementById('zoomfit'); if (f && f.offsetWidth) f.click(); });   // (13 Nov, pass 2: Fit is now there at every width, so it is used only when the module is not already on screen)
    await sleep(mp, 400);
    const p2 = await mp.evaluate(() => { const r = document.querySelector('.node[data-id="m3"] > header .t').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, vis: r.bottom < innerHeight && r.top > 0 && r.left > 0 && r.left < innerWidth }; });
    if (p2.vis) {
      await mp.touchscreen.tap(p2.x, p2.y); await sleep(mp, 120); await mp.touchscreen.tap(p2.x, p2.y); await sleep(mp, 500);
      const c1 = (await collapsedIds(mp)).includes('m3');
      ok(`${ed} C5: a double-tap on the title bar collapses (once)`, c1);
      await mp.touchscreen.tap(p2.x, p2.y); await sleep(mp, 120); await mp.touchscreen.tap(p2.x, p2.y); await sleep(mp, 500);
      ok(`${ed} C5: a second double-tap expands`, !(await collapsedIds(mp)).includes('m3'));
      const hit = await mp.evaluate(() => { const f = document.querySelector('.node[data-id="m3"] > header .fold'), r = f.getBoundingClientRect(), cx = r.left + r.width / 2, cy = r.top + r.height / 2, coarse = matchMedia('(pointer: coarse)').matches;
        // the glyph is 20 x 22 layout px; the target must reach 40 x 40: sample 15 px out (outside the glyph, inside the target), scaled by the editor's zoom, and 25 px out (outside the target)
        const z = r.width / f.offsetWidth, at = (dx, dy) => { const e = document.elementFromPoint(cx + dx * z, cy + dy * z); return !!(e && (e === f || f.contains(e))); };
        return { coarse, z: +z.toFixed(2), centre: at(0, 0), side: at(-15, 0) && at(15, 0), vert: at(0, -15) && at(0, 15), corner: at(-15, -15) && at(15, 15), beyond: at(-25, 0) || at(25, 0) || at(0, -25) || at(0, 25) }; });
      ok(`${ed} C5: the chevron's touch target is 40 x 40 px, wider than the 20 x 22 px glyph (${JSON.stringify(hit)})`, hit.coarse && hit.centre && hit.side && hit.vert && hit.corner && !hit.beyond);
    } else ok(`${ed} C5: the title bar of m3 can be reached at 344 px`, false, JSON.stringify(p2));
    const errs = M.errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed}: no page errors (344 px touch)`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} touch: ran to the end`, false, e.stack.split('\n').slice(0, 3).join(' | ')); }
  await M.close();
}

// ---- build 583 (owner's first results): a collapsed module grows to fit its patched ports; modulation stubs on the left with names ----
const G = {
  chains: { mix: 'smixer', chans: 7, stereo: true, src: 'delay', sink: 'chainOut', sinkIn: ['in', 'inR'], mixOuts: ['out', 'outR'], plain: 'delay', plainIns: ['in', 'inR'] },
  waves:  { mix: 'mixer', chans: 16, stereo: false, src: 'vco', sink: 'out', sinkIn: ['in'], mixOuts: ['out'], plain: 'delay', plainIns: ['in'] },
};
function growDoc(ed, { chans = G[ed].chans, mods = ['l2'], collapsed = true, zoom = 1 } = {}) {
  const g = G[ed], mx = P('mx', g.mix, 260, 120, { chans }, collapsed), cab = [];
  const mods0 = [mx, P('src', g.src, 20, 520), P('lf', 'lfo', 20, 120), P('snk', g.sink, 760, 120, g.sink === 'chainOut' ? { track: 1, level: 1 } : { slot: 1, level: 1 }), P('pl', g.plain, 760, 420, {}, collapsed), P('pls', g.src, 20, 760)];
  for (let i = 1; i <= chans; i++) { cab.push(C('src', 'out', 'mx', 'in' + i)); if (g.stereo) cab.push(C('src', 'outR', 'mx', 'in' + i + 'R')); }
  g.mixOuts.forEach((o, i) => cab.push(C('mx', o, 'snk', g.sinkIn[Math.min(i, g.sinkIn.length - 1)])));
  for (const k of mods) cab.push(C('lf', 'out', 'mx', 'p:' + k));
  g.plainIns.forEach((pi, i) => cab.push(C('pls', i ? 'outR' : 'out', 'pl', pi)));
  const base = SPEC[ed].doc(); const d = { ...base, modules: mods0, cables: cab, view: { x: 0, y: 0, zoom, ui: 2 } };
  return d;
}
// everything a collapsed module draws, in client pixels, relative to its own frame (inner edge of the border)
const GPROBE = `((id) => {
  const n = document.querySelector('.node[data-id="' + id + '"]'), nr = n.getBoundingClientRect(), cs = getComputedStyle(n), z = nr.width / n.offsetWidth;
  const bl = parseFloat(cs.borderLeftWidth) * z, bt = parseFloat(cs.borderTopWidth) * z, inner = { l: nr.left + bl, r: nr.right - bl, t: nr.top + bt, b: nr.bottom - bt };
  const jacks = [...n.querySelectorAll('.jack')].map(j => { const r = j.getBoundingClientRect(); return { port: j.dataset.port, dir: j.dataset.dir, stub: !!j.closest('.port.stub'), r: r.width ? { l: r.left, r: r.right, t: r.top, b: r.bottom, cx: r.left + r.width / 2, cy: r.top + r.height / 2 } : null }; }).filter(j => j.r);
  const stubs = [...n.querySelectorAll('.port.stub')].map(s => { const l = s.querySelector('.pl'), r = l.getBoundingClientRect(); return { text: l.textContent, shown: r.width > 0 && getComputedStyle(l).display !== 'none', clip: l.scrollWidth > l.clientWidth + .5, l: r.left, r: r.right, t: s.getBoundingClientRect().top, b: s.getBoundingClientRect().bottom }; });
  const sum = n.querySelector(':scope > .csum .cs-t');
  return { z, w: n.offsetWidth, h: n.offsetHeight, inline: n.style.width, cr: cs.getPropertyValue('--cr').trim(), inner, jacks, stubs, sum: sum ? sum.innerText : null, collapsed: n.classList.contains('collapsed') };
})`;
const gp = (page, id) => page.evaluate(`${GPROBE}('${id}')`);
const inside = (p, slack = .6) => p.jacks.concat(p.stubs.map(s => ({ port: 'stub label ' + s.text, r: { l: s.l, r: s.r, t: s.t, b: s.b } }))).filter(j => !(j.r.l >= p.inner.l - slack && j.r.r <= p.inner.r + slack && j.r.t >= p.inner.t - slack && j.r.b <= p.inner.b + slack)).map(j => `${j.dir || ''}:${j.port} ${Math.round(j.r.l)}-${Math.round(j.r.r)} outside ${Math.round(p.inner.l)}-${Math.round(p.inner.r)}`);
async function cableEndsOnJacks(page, doc, id) {
  return page.evaluate(([d, i]) => {
    const world = document.querySelector('.world'), m = new DOMMatrix(getComputedStyle(world).transform), wr = world.getBoundingClientRect(), bad = [];
    const gs = [...document.querySelectorAll('svg.cables g')].filter(g => g.querySelector('path.cln'));
    d.cables.forEach((c, k) => {
      for (const [end, dir, which] of [[c.from, 'out', 'a'], [c.to, 'in', 'b']]) {
        if (end[0] !== i) continue;
        const pts = (gs[k].querySelector('path.cln').getAttribute('d').match(/-?[\d.]+,-?[\d.]+/g) || []).map(s => s.split(',').map(Number)), pt = which === 'a' ? pts[0] : pts[pts.length - 1];
        let j = document.querySelector('.node[data-id="' + i + '"] .jack[data-dir="' + dir + '"][data-port="' + end[1] + '"]'); let r = j && j.getBoundingClientRect();
        if (!r || !r.width) { j = document.querySelector('.node[data-id="' + i + '"] .port.stub .jack[data-port="' + end[1] + '"]'); r = j && j.getBoundingClientRect(); }
        if (!r || !r.width) { bad.push('no jack for ' + end.join('.')); continue; }
        const cx = (r.left + r.width / 2 - wr.left) / m.a, cy = (r.top + r.height / 2 - wr.top) / m.a;
        if (Math.hypot(pt[0] - cx, pt[1] - cy) > 1.5) bad.push(end.join('.') + ' end ' + pt.map(Math.round) + ' jack ' + [cx, cy].map(Math.round));
      }
    });
    return bad;
  }, [doc, id]);
}
async function grow(ed) {
  const g = G[ed], B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  const nIn = g.chans * (g.stereo ? 2 : 1), nOut = g.mixOuts.length;
  try {
    // plain module (2 inputs patched, or 1): the width does not change
    for (const z of [.5, 1, 1.5]) {
      const d = growDoc(ed, { zoom: z }); await load(page, base, ed, d);
      const p = await gp(page, 'mx'), pl = await gp(page, 'pl');
      const tag = `${ed} grow @${Math.round(z * 100)}%`;
      ok(`${tag}: ${g.mix} with ${nIn} inputs patched is wider than the default (${p.w} px) and under the cap`, p.collapsed && p.w > 250 && p.w <= 500, `w ${p.w}`);
      const ins = p.jacks.filter(j => j.dir === 'in' && !j.stub), outs = p.jacks.filter(j => j.dir === 'out');
      ok(`${tag}: all ${nIn} input jacks, ${nOut} output(s) and the stub are drawn (${ins.length} / ${outs.length} / ${p.stubs.length})`, ins.length === nIn && outs.length === nOut && p.stubs.length === 1);
      ok(`${tag}: every jack, stub and label lies inside the module's frame`, inside(p).length === 0, inside(p).slice(0, 3).join('; '));
      const mid = (p.inner.l + p.inner.r) / 2;
      ok(`${tag}: the outputs stand at the right edge inside the frame (gap ${Math.round(p.inner.r - Math.max(...outs.map(o => o.r.r)))} px)`, outs.every(o => o.r.cx > mid) && (p.inner.r - Math.max(...outs.map(o => o.r.r))) <= 16 * p.z && ins.every(j => j.r.cx < Math.min(...outs.map(o => o.r.l))));
      const st = p.jacks.find(j => j.stub), lowestIn = Math.max(...ins.map(j => j.r.cy));
      ok(`${tag}: the modulation stub is on the left, below the audio inputs, named "${p.stubs[0] && p.stubs[0].text}"`, st && st.r.cx < mid && st.r.cy > lowestIn && p.stubs[0].shown && p.stubs[0].text.trim().length > 0 && !p.stubs[0].clip, JSON.stringify({ st: st && st.r, mid, lowestIn, s: p.stubs[0] }));
      ok(`${tag}: the summary starts with the master level (${JSON.stringify(p.sum)})`, /^Master 100%/.test(p.sum || ''));
      const bad = await cableEndsOnJacks(page, d, 'mx');
      ok(`${tag}: every cable end is on its jack (${d.cables.filter(c => c.from[0] === 'mx' || c.to[0] === 'mx').length} cables)`, bad.length === 0, bad.slice(0, 3).join('; '));
      ok(`${tag}: a plain module with ${g.plainIns.length} inputs keeps its width (${pl.w} px) and has nothing outside its frame`, pl.w === 250 && pl.inline === '' && inside(pl).length === 0, JSON.stringify({ w: pl.w, inline: pl.inline, o: inside(pl).slice(0, 2) }));
    }
    // expand: the normal width; collapse again: wide again. Nothing new is saved.
    await load(page, base, ed, growDoc(ed, { collapsed: false }));
    const o0 = await gp(page, 'mx'); ok(`${ed} grow: open, the module has its normal width (${o0.w} px)`, !o0.collapsed && o0.w === 250 && o0.inline === '');
    await fold(page, 'mx'); await sleep(page, 300);
    const c1 = await gp(page, 'mx'); ok(`${ed} grow: collapsing makes it wider (${c1.w} px) and everything fits`, c1.collapsed && c1.w > 250 && inside(c1).length === 0, inside(c1).slice(0, 2).join('; '));
    ok(`${ed} grow: cable ends on their jacks after collapsing by the chevron`, (await cableEndsOnJacks(page, growDoc(ed, { collapsed: false }), 'mx')).length === 0);
    await sleep(page, 2200);
    const saved = await page.evaluate(k => JSON.parse(localStorage.getItem(k)), SPEC[ed].key);
    ok(`${ed} grow: the saved patch has collapsed:true and no width or other new field`, saved.modules.every(m => Object.keys(m).sort().join() === 'collapsed,id,params,pos,scope,type') && saved.modules.find(m => m.id === 'mx').collapsed === true && !/width/i.test(JSON.stringify(saved)));
    await page.reload(); await page.waitForSelector('.node'); await sleep(page, 800);
    const c2 = await gp(page, 'mx'); ok(`${ed} grow: save then load gives the same wide module (${c2.w} px)`, c2.collapsed && c2.w === c1.w && inside(c2).length === 0);
    await fold(page, 'mx'); await sleep(page, 300);
    const e1 = await gp(page, 'mx'); ok(`${ed} grow: expanding returns the normal width (${e1.w} px, no inline width)`, !e1.collapsed && e1.w === 250 && e1.inline === '' && e1.cr === '');
    // a hand-made patch of build 579 to 582 (collapsed:true, nothing else) opens wide
    const old = growDoc(ed); old.modules.forEach(m => { delete m.view; });
    await load(page, base, ed, old); const c3 = await gp(page, 'mx');
    ok(`${ed} grow: an old patch with collapsed:true opens with the wider module (${c3.w} px)`, c3.collapsed && c3.w === c1.w && inside(c3).length === 0);
    // many modulated parameters: the stubs wrap into rows, all named, all inside
    const manyK = ['l1', 'l2', 'l3', 'l4', 'l5', ...(g.stereo ? ['p1', 'p2'] : ['l6', 'l7'])];
    const dm = growDoc(ed, { mods: manyK }); await load(page, base, ed, dm); const pm = await gp(page, 'mx');
    const rowsTop = new Set(pm.stubs.map(s => Math.round(s.t)));
    ok(`${ed} grow: ${manyK.length} modulation stubs wrap into ${rowsTop.size} row(s), all named, all inside the frame`, pm.stubs.length === manyK.length && rowsTop.size >= 1 && pm.stubs.every(s => s.shown && s.text.trim() && !s.clip) && inside(pm).length === 0 && (manyK.length < 4 || rowsTop.size >= 2), JSON.stringify({ rows: rowsTop.size, w: pm.w, o: inside(pm).slice(0, 2) }));
    ok(`${ed} grow: ... and their cables end on them`, (await cableEndsOnJacks(page, dm, 'mx')).length === 0);
    // the cap: more ports than 500 px hold: more rows, nothing outside
    if (g.stereo) {
      const dc = growDoc(ed, { chans: 16, mods: ['l1', 'l2', 'l3', 'l4', 'l5', 'l6'] }); await load(page, base, ed, dc); const pc = await gp(page, 'mx');
      const ys = new Set(pc.jacks.filter(j => j.dir === 'in' && !j.stub).map(j => Math.round(j.r.cy)));
      ok(`${ed} grow: at the width cap the ports take more rows (${pc.w} px wide, ${ys.size} rows, --cr ${pc.cr}) and everything is inside`, pc.w <= 500 && pc.w > 400 && ys.size >= 3 && inside(pc).length === 0, JSON.stringify({ w: pc.w, rows: ys.size, o: inside(pc).slice(0, 2) }));
      ok(`${ed} grow: ... the stubs are named and below the inputs; the cables end on their jacks`, pc.stubs.length === 6 && pc.stubs.every(s => s.shown && s.text.trim()) && Math.min(...pc.stubs.map(s => s.t)) >= Math.max(...pc.jacks.filter(j => j.dir === 'in' && !j.stub).map(j => j.r.cy)) - 1 && (await cableEndsOnJacks(page, dc, 'mx')).length === 0);
      await fold(page, 'mx'); await sleep(page, 300);
      const pe = await gp(page, 'mx'); ok(`${ed} grow: ... and expanding it clears the width and the rows`, !pe.collapsed && pe.w === 250 && pe.cr === '');
    }
    const errs = errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed} grow: no page errors`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} grow: ran to the end`, false, e.stack.split('\n').slice(0, 3).join(' | ')); }
  await B.close();
}

for (const ed of ['chains', 'waves']) await run(ed);
for (const ed of ['chains', 'waves']) await grow(ed);

// build 583, review fix: a collapsed Sidechain Out's level strip falls to nothing when its telemetry stops (through the host, as Thunder Plus runs it)
async function scoutStrip() {
  const storage = { 'thunderplus-platform-v1': 'desktop', 'thunderplus.chains.settings': '{"tips":false}' };
  const B = await boot({ width: 1440, height: 900, storage }); const { page } = B;
  try {
    const d = { format: 'chains.patch', meta: { name: 'scout' }, view: { x: 0, y: 0, zoom: 1, ui: 2 }, st2: 1, ti2: 1, modules: [P('so', 'scout', 100, 100, { snd: 1, level: 1 }, true)], cables: [], groups: [], custom: [], transport: { tempo: 120 } };
    await app(page, `(setView('chains'), 1)`); await sleep(page, 1200);
    await app(page, `(CH.frame.contentWindow.chainsEmbed.setDoc(${JSON.stringify(d)}), 1)`); await sleep(page, 800);
    const fr = page.frames().find(f => f.url().includes('/chains/')), strip = () => fr.evaluate(() => document.querySelector('.node[data-id="so"] .clvl b').style.transform);
    await app(page, `(CH.frame.contentWindow.chainsEmbed.meters({ peaks: [], shows: [['so', [-6]]] }), 1)`); await sleep(page, 100);
    const on = await strip(); await sleep(page, 900); const off = await strip();
    ok(`chains: a collapsed Sidechain Out's strip moves with the send (${on}) and falls to nothing when it stops (${off})`, /scaleX\(0\.[1-9]|scaleX\(1\)/.test(on) && off === 'scaleX(0)');
  } catch (e) { ok('chains scout strip: ran to the end', false, e.stack.split('\n').slice(0, 3).join(' | ')); }
  await B.close();
}
await scoutStrip();
const f = T.print();
process.exit(f ? 1 : 0);
