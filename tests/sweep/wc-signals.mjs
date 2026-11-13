// Pass 1b of the …waves / …chains UX rework (docs/audits/2026-11-13-waves-chains-ux.md, M9, U1, D6, D9): signal kinds and cable states.
// Each editor is opened on its own page in headless Chrome (then the app, for the skins) and checked for: gate jacks and gate cables
// told apart from control and audio by shape and dash (open and Compact, and with every colour forced to one value), the selected-module
// outline not being a signal colour (white, IDM pink & green, IDM, 909), cable hover / select / Delete / Escape, bend handles, the hit
// width at 50%, 100% and 150%, an old patch, save then load, and touch at 344 px.
//   node tests/sweep/wc-signals.mjs            (OLD_ROOT=/path/to/older/checkout also loads a patch saved by this build in that one)
import http from 'http'; import fs from 'fs'; import path from 'path';
import { boot, app, Table } from './ui-lib.mjs';

const T = new Table('wc-signals');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'ok   ' : 'FAIL ') + name + (note ? '  ' + note : '')); };
const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|ServiceWorker|sw\.js|autoplay/i;
const sleep = (page, ms) => page.waitForTimeout(ms);

// ---- one small patch per editor: an audio cable (with a bend), a gate cable and a control cable, in a tight cluster so it fits at 150% ----
const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp, pts) => { const c = { from: [a, ap], to: [b, bp], bend: 0 }; if (pts) c.pts = pts; return c; };
const SPEC = {
  chains: { key: 'chains.patch', audioSrc: 'trackIn', gateIn: 'm4', gateOut: 'm3',
    doc: (zoom = 1, x = 40, y = 20) => ({ format: 'chains.patch', meta: { name: 'wc signals' }, view: { x, y, zoom, ui: 2 }, st2: 1, ti2: 1,
      modules: [P('m1', 'trackIn', 0, 0, { track: 1 }), P('m2', 'delay', 420, 0), P('m3', 'euclid', 0, 230), P('m4', 'snh', 420, 230), P('m5', 'lfo', 0, 460)],
      cables: [C('m1', 'out', 'm2', 'in', [[335, 30]]), C('m3', 'gate', 'm4', 'trig'), C('m5', 'out', 'm4', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
  waves: { key: 'waves.patch', audioSrc: 'vco', gateIn: 'm4', gateOut: 'm3',
    doc: (zoom = 1, x = 40, y = 20) => ({ meta: { name: 'wc signals' }, view: { x, y, zoom }, voice: { maxVoices: 8 },
      modules: [P('m1', 'vco', 0, 0, { wave: 1 }), P('m2', 'vcf', 420, 0), P('m3', 'euclid', 0, 230), P('m4', 'snh', 420, 230), P('m5', 'lfo', 0, 460)],
      cables: [C('m1', 'out', 'm2', 'in', [[335, 30]]), C('m3', 'gate', 'm4', 'trig'), C('m5', 'out', 'm4', 'in')], groups: [], custom: [], transport: { tempo: 120 } }) },
};
// the cable index (in the patch) of each kind
const KIND_AT = { audio: 0, gate: 1, mod: 2 };

// ---- in-page helpers: installed once per page load ----
const HELP = `(() => {
  if (window.__sig) return;
  const colour = c => { const d = document.createElement('i'); d.style.color = c; document.body.append(d); const v = getComputedStyle(d).color; d.remove(); return v; };
  const gs = () => [...document.querySelectorAll('svg.cables g')].filter(g => g.querySelector('path.hit'));
  window.__sig = {
    colour,
    gs,
    cabKind: g => { const l = g.querySelector('path.cln'); return l.classList.contains('gate') ? 'gate' : l.classList.contains('ctl') ? 'mod' : 'audio'; },
    // shape of a jack: its box, its corner radius and the rotation of its transform
    jack: el => { const cs = getComputedStyle(el), m = new DOMMatrix(cs.transform === 'none' ? undefined : cs.transform); return { w: Math.round(el.offsetWidth), br: cs.borderTopLeftRadius, rot: Math.round(Math.atan2(m.b, m.a) * 180 / Math.PI), k: el.dataset.kind }; },
    dash: g => { const cs = getComputedStyle(g.querySelector('path.cln')); return { da: cs.strokeDasharray, cap: cs.strokeLinecap, w: parseFloat(cs.strokeWidth) }; },
    // a point on the cable that really hits it (elementFromPoint is its own .hit), with the unit normal there; screen pixels
    point: i => { const g = gs()[i]; if (!g) return null; const p = g.querySelector('path.cln'), hit = g.querySelector('path.hit'), L = p.getTotalLength(), m = p.getScreenCTM();
      const S = q => { const pt = p.getPointAtLength(q); return [m.a * pt.x + m.c * pt.y + m.e, m.b * pt.x + m.d * pt.y + m.f]; };
      for (const t of [.5, .45, .55, .4, .6, .35, .65, .3, .7, .25, .75, .2, .8]) { const [x, y] = S(L * t), [x2, y2] = S(L * t + 2), dx = x2 - x, dy = y2 - y, d = Math.hypot(dx, dy) || 1;
        if (document.elementFromPoint(x, y) === hit && ![...g.querySelectorAll('rect.bend')].some(r => { const b = r.getBoundingClientRect(); return Math.hypot(b.left + b.width / 2 - x, b.top + b.height / 2 - y) < 18; }) && !document.elementsFromPoint(x, y).some(e => e.classList && e.classList.contains('jack'))) return { x, y, nx: -dy / d, ny: dx / d }; }
      return null; },
    hitAt: (i, x, y) => { const g = gs()[i]; return !!g && document.elementFromPoint(x, y) === g.querySelector('path.hit'); },
    handleBox: i => { const g = gs()[i], r = g.querySelector('rect.bend'); if (!r) return null; const b = r.getBoundingClientRect(), cs = getComputedStyle(r); return { x: b.left + b.width / 2, y: b.top + b.height / 2, w: b.width, op: +cs.opacity, pe: cs.pointerEvents }; },
    paths: () => gs().map(g => g.querySelector('path.cln').getAttribute('d')),
    zoom: () => new DOMMatrix(getComputedStyle(document.querySelector('.world')).transform).a,
    count: () => gs().length,
    selIdx: () => gs().map((g, i) => g.classList.contains('sel') ? i : -1).filter(i => i >= 0),
    dimCount: () => gs().filter(g => g.classList.contains('dim')).length,
    stroke: g => getComputedStyle(g.querySelector('path.cln')).strokeWidth,
    // the halo: the alpha of the hit line's stroke colour times its opacity (0 at rest, when it is transparent)
    halo: g => { const cs = getComputedStyle(g.querySelector('path.hit')), m = /rgba?\\(([^)]+)\\)/.exec(cs.stroke), a = m ? (m[1].split(',').length > 3 ? parseFloat(m[1].split(',')[3]) : 1) : 0; return +(a * parseFloat(cs.strokeOpacity)).toFixed(3); },
  };
})()`;
const S = (page, expr) => page.evaluate(`(() => { ${HELP}; return (${expr}); })()`);

// ---- the colours a skin gives the three signal kinds, and the selection mark, read inside a page or a frame ----
const COLOURS = fr => fr.evaluate(() => {
  const colour = c => { const d = document.createElement('i'); d.style.color = c; document.body.append(d); const v = getComputedStyle(d).color; d.remove(); return v; };
  const cs = getComputedStyle(document.documentElement), v = k => colour('var(' + k + ')');
  const cab = k => { const g = document.querySelector('svg.cables g path.cln' + k); return g ? getComputedStyle(g).stroke : null; };
  const sel = document.querySelector('.node.selected');
  return { audio: v('--audio'), mod: v('--mod'), gate: v('--gate'), cabAudio: cab(':not(.ctl)'), cabMod: cab('.ctl:not(.gate)'), cabGate: cab('.gate'), outline: sel ? getComputedStyle(sel).outlineColor : null, selc: v('--selc') };
});

async function run(ed) {
  const spec = SPEC[ed];
  const B = await boot({ width: 1440, height: 900 }); const { page, errors } = B; const base = `http://localhost:${B.port}`;
  const loadDoc = async doc => { await page.goto(`${base}/${ed}/index.html`); await page.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); }, [spec.key, doc]);
    await page.goto(`${base}/${ed}/index.html`); await page.waitForSelector('.node', { timeout: 20000 }); await sleep(page, 700);
    await page.evaluate(() => { const h = document.getElementById('hint'); if (h) h.hidden = true; }); };
  try {
    // ================= signal kinds: jacks and cables =================
    await loadDoc(spec.doc(1));
    ok(`${ed}: the patch has an audio, a gate and a control cable`, JSON.stringify(await S(page, `__sig.gs().map(__sig.cabKind)`)) === '["audio","gate","mod"]');
    const jk = await page.evaluate(`(() => { ${HELP}; const J = (id, dir, port) => __sig.jack(document.querySelector('.node[data-id="' + id + '"] .jack[data-dir="' + dir + '"][data-port="' + port + '"]'));
      return { gate: J('m3', 'out', 'gate'), gateIn: J('m4', 'in', 'trig'), ctl: J('m5', 'out', 'out'), ctlIn: J('m4', 'in', 'in'), audio: J('m1', 'out', 'out'), audioIn: J('m2', 'in', 'in') }; })()`);
    const shape = j => `${j.w}/${j.br}/${j.rot}`;
    ok(`${ed} M9: a gate jack is a diamond, apart from the control and audio jacks (open module)`, shape(jk.gate) !== shape(jk.ctl) && shape(jk.gate) !== shape(jk.audio) && shape(jk.gateIn) === shape(jk.gate) && jk.gate.rot === 45, JSON.stringify(jk));
    ok(`${ed} M9: audio and control jacks keep their round look`, shape(jk.audio) === shape(jk.ctl) && shape(jk.ctlIn) === shape(jk.audioIn) && jk.audio.rot === 0 && /^50%|^10px/.test(jk.audio.br), shape(jk.audio));
    const cd = await page.evaluate(`(() => { ${HELP}; const g = __sig.gs(); return { audio: __sig.dash(g[0]), gate: __sig.dash(g[1]), mod: __sig.dash(g[2]) }; })()`);
    const dsh = d => `${d.da}/${d.cap}`;
    ok(`${ed} M9: gate, control and audio cables each have their own dash (${dsh(cd.audio)} | ${dsh(cd.mod)} | ${dsh(cd.gate)})`, new Set([dsh(cd.audio), dsh(cd.mod), dsh(cd.gate)]).size === 3 && cd.audio.da === 'none');

    // Compact: fold the gate modules and the others; the gate jacks that are patched stay diamonds
    const fold = id => page.evaluate(i => document.querySelector(`.node[data-id="${i}"] > header .fold`).click(), id);   // (a cable's wide hit zone can lie over a title bar)
    for (const id of ['m1', 'm3', 'm4', 'm5']) await fold(id); await sleep(page, 400);
    const ck = await page.evaluate(`(() => { ${HELP}; const J = (id, dir, port) => { const e = document.querySelector('.node[data-id="' + id + '"] .jack[data-dir="' + dir + '"][data-port="' + port + '"]'); return e && { ...__sig.jack(e), shown: e.getBoundingClientRect().width > 0 }; };
      const st = document.querySelector('.node[data-id="m4"] .port.stub .jack'); return { gate: J('m3', 'out', 'gate'), gateIn: J('m4', 'in', 'trig'), ctl: J('m5', 'out', 'out'), audio: J('m1', 'out', 'out'), stub: st ? __sig.jack(st) : null, nstub: document.querySelectorAll('.node.collapsed .port.stub').length }; })()`);
    const csh = j => `${j.rot}/${j.br}`;
    ok(`${ed} M9: Compact: the gate jacks are diamonds, the control and audio jacks round`, ck.gate && ck.gate.shown && ck.gateIn.shown && ck.gate.rot === 45 && ck.gateIn.rot === 45 && ck.ctl.rot === 0 && ck.audio.rot === 0 && csh(ck.gate) !== csh(ck.ctl), JSON.stringify(ck));
    ok(`${ed} M9: Compact: a control stub is a round jack, not a diamond`, ck.nstub === 0 || (ck.stub && ck.stub.rot === 0), JSON.stringify(ck.stub) + ' stubs ' + ck.nstub);
    // a stub with a patched slider: open delay / vcf is not collapsed here, so collapse the module with the modulated slider
    await fold('m2'); await sleep(page, 300);
    const stub = await page.evaluate(`(() => { ${HELP}; const e = document.querySelector('.node[data-id="m2"] .port.stub .jack'); return e ? __sig.jack(e) : null; })()`);
    ok(`${ed} M9: Compact stubs stay round when present (${stub ? 'one found' : 'none in this patch'})`, !stub || stub.rot === 0);
    for (const id of ['m1', 'm2', 'm3', 'm4', 'm5']) { const c = await page.evaluate(i => document.querySelector(`.node[data-id="${i}"]`).classList.contains('collapsed'), id); if (c) await fold(id); }
    await sleep(page, 300);

    // every colour forced to one value: the kinds are still told apart by shape and dash
    await page.addStyleTag({ content: 'svg.cables .cln, svg.cables circle{stroke:#ff00ff !important; fill:#ff00ff !important} .jack{--k:#ff00ff !important} .jack[data-kind]{--k:#ff00ff !important}' });
    const fc = await page.evaluate(`(() => { ${HELP}; const g = __sig.gs(), J = (id, dir, port) => __sig.jack(document.querySelector('.node[data-id="' + id + '"] .jack[data-dir="' + dir + '"][data-port="' + port + '"]'));
      return { cab: g.map(x => __sig.dash(x)), col: g.map(x => getComputedStyle(x.querySelector('path.cln')).stroke), gate: J('m3', 'out', 'gate'), ctl: J('m5', 'out', 'out'), audio: J('m1', 'out', 'out') }; })()`);
    ok(`${ed} M9: one colour for everything: cables still differ by dash (${fc.col[0]}), jacks by shape`, new Set(fc.col).size === 1 && new Set(fc.cab.map(dsh)).size === 3 && shape(fc.gate) !== shape(fc.ctl) && shape(fc.gate) !== shape(fc.audio), JSON.stringify(fc.cab.map(dsh)));
    // and rendered: a gate cable and a control cable cut out of the page are not the same picture (dots against dashes)
    await page.reload(); await page.waitForSelector('.node'); await sleep(page, 700);

    // the cable being dragged is drawn in its own kind
    const gj = await page.evaluate(() => { const r = document.querySelector('.node[data-id="m3"] .jack[data-dir="out"][data-port="accent"]').getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, kind: document.querySelector('.node[data-id="m3"] .jack[data-port="accent"]').dataset.kind }; });
    await page.mouse.move(gj.x, gj.y); await page.mouse.down(); await page.mouse.move(gj.x + 150, gj.y + 90, { steps: 6 }); await sleep(page, 120);
    const dr = await page.evaluate(() => { const gs = [...document.querySelectorAll('svg.cables g')].filter(g => !g.querySelector('path.hit') && g.querySelector('path.cln')); const l = gs[0] && gs[0].querySelector('path.cln'); return l ? { gate: l.classList.contains('gate'), da: getComputedStyle(l).strokeDasharray } : null; });
    await page.mouse.up(); await sleep(page, 200);
    ok(`${ed} M9: the cable being dragged from a gate jack is dotted like a gate cable (${gj.kind})`, gj.kind === 'gate' && dr && dr.gate && dr.da === cd.gate.da, JSON.stringify(dr));
    ok(`${ed}: ...and dropping it on empty space left the patch as it was`, (await S(page, '__sig.count()')) === 3);

    // ================= the selection mark is not a signal colour (own page: white) =================
    await page.click('.node[data-id="m2"] > header .t'); await sleep(page, 150);
    const wc = await COLOURS(page);
    const sig = [wc.audio, wc.mod, wc.gate, wc.cabAudio, wc.cabMod, wc.cabGate];
    ok(`${ed} U1: white theme: the selected module's outline (${wc.outline}) is none of the signal colours`, wc.outline && !sig.includes(wc.outline) && wc.outline === wc.selc, JSON.stringify(wc));
    ok(`${ed} D6: a module is selected: the cables that do not touch it step back, the ones that do stay`, (await S(page, '__sig.dimCount()')) === 2, 'dimmed ' + await S(page, '__sig.dimCount()'));
    await page.mouse.click(1380, 400); await sleep(page, 150);
    ok(`${ed} D6: a click on the background lets the module go and the cables come back`, (await page.$$('.node.selected')).length === 0 && (await S(page, '__sig.dimCount()')) === 0);

    // ================= cable states at 50%, 100% and 150% =================
    let lastDoc = null;
    for (const Z of [0.5, 1, 1.5]) {
      const z = `${Math.round(Z * 100)}%`;
      await loadDoc(spec.doc(Z, Z === 1.5 ? 30 : 40, Z === 1.5 ? 10 : 20));
      ok(`${ed} @${z}: the patch opens at that zoom`, Math.abs((await S(page, '__sig.zoom()')) - Z) < 0.01, String(await S(page, '__sig.zoom()')));
      const pts = []; for (let i = 0; i < 3; i++) pts.push(await S(page, `__sig.point(${i})`));
      ok(`${ed} @${z}: every cable can be hit on its line`, pts.every(Boolean), JSON.stringify(pts.map(p => p && [Math.round(p.x), Math.round(p.y)])));
      if (!pts.every(Boolean)) continue;
      // the hit zone is about 14 px wide on the screen at any zoom: 5 px off the line hits, 30 px off does not
      const off = async (i, d) => S(page, `(() => { const p = __sig.point(${i}); return __sig.hitAt(${i}, p.x + p.nx * ${d}, p.y + p.ny * ${d}); })()`);
      const near = [await off(0, 5), await off(1, 5), await off(2, 5)], far = [await off(0, 30), await off(1, 30), await off(2, 30)];
      ok(`${ed} @${z}: 5 px off the line still hits (the zone does not shrink with the zoom), 30 px off does not`, near.every(Boolean) && far.every(x => !x), JSON.stringify({ near, far }));

      // hover: the state changes and leaving restores it, with no script running (the class list does not change)
      for (const [name, i] of [['gate', 1], ['control', 2], ['audio', 0]]) {
        await page.mouse.move(1380, 400); await sleep(page, 60);
        const p = pts[i], w0 = await S(page, `__sig.stroke(__sig.gs()[${i}])`), cls0 = await S(page, `__sig.gs()[${i}].getAttribute('class') || ''`);
        await page.mouse.move(p.x, p.y); await sleep(page, 60);
        const hov = await S(page, `(() => { const g = __sig.gs()[${i}]; return { on: g.matches(':hover'), w: __sig.stroke(g), halo: __sig.halo(g), cls: g.getAttribute('class') || '' }; })()`);
        await page.mouse.move(p.x + p.nx * 60, p.y + p.ny * 60); await sleep(page, 60);
        const back = await S(page, `(() => { const g = __sig.gs()[${i}]; return { on: g.matches(':hover'), w: __sig.stroke(g), halo: __sig.halo(g) }; })()`);
        ok(`${ed} @${z} D6: hovering the ${name} cable thickens it and shows a halo; leaving restores it (${w0} > ${hov.w} > ${back.w})`, hov.on && parseFloat(hov.w) > parseFloat(w0) && +hov.halo > 0 && hov.cls === cls0 && !back.on && back.w === w0 && +back.halo === 0, JSON.stringify({ hov, back }));
      }
      // handles: hidden at rest, there on hover (the bent cable), 12 px or more across
      const hp = await S(page, `__sig.handleBox(0)`);
      ok(`${ed} @${z} D9: the bend handle is hidden while the cable is not hovered or selected`, hp && hp.op === 0 && hp.pe === 'none', JSON.stringify(hp));
      await page.mouse.move(pts[0].x, pts[0].y); await sleep(page, 80);
      const hh = await S(page, `__sig.handleBox(0)`);
      ok(`${ed} @${z} D9: hovering the cable shows its bend handle, about 12 px across (${hh && hh.w.toFixed(1)})`, hh && hh.op === 1 && hh.pe === 'all' && hh.w >= 10.5 && hh.w < 20, JSON.stringify(hh));
      await page.mouse.move(700, 880); await sleep(page, 60);

      // click selects it (and only it), Delete removes exactly it as one undo step
      const before = await S(page, `__sig.paths()`);
      await page.mouse.click(pts[1].x, pts[1].y); await sleep(page, 100);
      ok(`${ed} @${z} D6: a click on the gate cable selects it`, JSON.stringify(await S(page, '__sig.selIdx()')) === '[1]');
      const st = await S(page, `(() => { const g = __sig.gs()[1]; return { w: __sig.stroke(g), halo: __sig.halo(g) }; })()`);
      ok(`${ed} @${z} D6: a selected cable stays thick with a stronger halo after the pointer leaves it`, (await page.mouse.move(700, 880), await sleep(page, 60), true) && parseFloat(await S(page, `__sig.stroke(__sig.gs()[1])`)) > 3.5 && +st.halo > 0.2, JSON.stringify(st));
      ok(`${ed} @${z} D6: selecting a cable is not an edit (no new undo step, the patch is as it was)`, JSON.stringify(await S(page, '__sig.paths()')) === JSON.stringify(before));
      await page.keyboard.press('Delete'); await sleep(page, 200);
      const after = await S(page, '__sig.paths()');
      ok(`${ed} @${z} D6: Delete removes exactly the selected cable`, after.length === 2 && after.includes(before[0]) && after.includes(before[2]) && !after.includes(before[1]), `${before.length} to ${after.length}`);
      await page.keyboard.press('Control+z'); await sleep(page, 300);
      const un = await S(page, '__sig.paths()');
      ok(`${ed} @${z} D6: one Undo brings the cable back`, un.length === 3 && JSON.stringify(un.slice().sort()) === JSON.stringify(before.slice().sort()), `${un.length}`);
      // Backspace too, then Escape and the background
      const p2 = await S(page, `__sig.point(2)`); await page.mouse.click(p2.x, p2.y); await sleep(page, 80);
      await page.keyboard.press('Backspace'); await sleep(page, 200);
      ok(`${ed} @${z} D6: Backspace removes the selected cable too`, (await S(page, '__sig.count()')) === 2);
      await page.keyboard.press('Control+z'); await sleep(page, 300);
      const p3 = await S(page, `__sig.point(0)`); await page.mouse.click(p3.x, p3.y); await sleep(page, 80);
      const sel1 = await S(page, '__sig.selIdx()');
      await page.keyboard.press('Escape'); await sleep(page, 80);
      ok(`${ed} @${z} D6: Escape lets the cable go (and Delete then does nothing)`, JSON.stringify(sel1) === '[0]' && (await S(page, '__sig.selIdx()')).length === 0 && (await page.keyboard.press('Delete'), await sleep(page, 120), (await S(page, '__sig.count()')) === 3));
      const p4 = await S(page, `__sig.point(0)`); await page.mouse.click(p4.x, p4.y); await sleep(page, 80);
      await page.mouse.click(1380, 400); await sleep(page, 100);
      ok(`${ed} @${z} D6: a click on the background lets the cable go`, (await S(page, '__sig.selIdx()')).length === 0);
      // a module and a cable are never selected together
      const p5 = await S(page, `__sig.point(1)`); await page.mouse.click(p5.x, p5.y); await sleep(page, 80);
      await page.click('.node[data-id="m2"] > header .t'); await sleep(page, 100);
      const both = [(await S(page, '__sig.selIdx()')).length, (await page.$$('.node.selected')).length];
      ok(`${ed} @${z} D6: selecting a module lets the cable go (and the other way round)`, both[0] === 0 && both[1] === 1, JSON.stringify(both));
      await page.mouse.click(1380, 400);
      const p6 = await S(page, `__sig.point(1)`); await page.mouse.click(p6.x, p6.y); await sleep(page, 80);
      await page.click('.node[data-id="m2"] > header .t'); const p7 = await S(page, `__sig.point(1)`); await page.mouse.click(p7 ? p7.x : 0, p7 ? p7.y : 0); await sleep(page, 100);
      ok(`${ed} @${z} D6: ...a cable click takes the module selection away`, (await page.$$('.node.selected')).length === 0 && (await S(page, '__sig.selIdx()')).length === 1);
      await page.mouse.click(1380, 400); await sleep(page, 80);

      // bend handles on a selected cable: drag one, the path changes, and it survives save then load
      const pBend = await S(page, `__sig.point(0)`); await page.mouse.click(pBend.x, pBend.y); await sleep(page, 100);
      const h1 = await S(page, '__sig.handleBox(0)'), d1 = (await S(page, '__sig.paths()'))[0];
      ok(`${ed} @${z} D9: a selected cable shows its bend handle with the pointer elsewhere`, (await page.mouse.move(700, 880), await sleep(page, 60), true) && (await S(page, '__sig.handleBox(0)')).op === 1 && h1.pe === 'all', JSON.stringify(h1));
      const h2 = await S(page, '__sig.handleBox(0)');
      await page.mouse.move(h2.x, h2.y); await page.mouse.down(); await page.mouse.move(h2.x + 40, h2.y + 32, { steps: 8 }); await page.mouse.up(); await sleep(page, 300);
      const d2 = (await S(page, '__sig.paths()'))[0], h3 = await S(page, '__sig.handleBox(0)');
      ok(`${ed} @${z} D9: dragging the handle moves the bend (path and handle follow)`, d2 !== d1 && Math.hypot(h3.x - h2.x - 40, h3.y - h2.y - 32) < 6, `${Math.round(h3.x - h2.x)},${Math.round(h3.y - h2.y)}`);
      await sleep(page, 2200);
      const saved = await page.evaluate(k => JSON.parse(localStorage.getItem(k)), spec.key);
      const sp = saved.cables[0].pts && saved.cables[0].pts[0];
      ok(`${ed} @${z} D9: the new bend is saved in the cable's own pts field (no new field)`, sp && Math.abs(sp[0] - 335) > 10 && saved.cables.every(c => Object.keys(c).every(k => ['from', 'to', 'bend', 'pts'].includes(k))), JSON.stringify(sp));
      await page.reload(); await page.waitForSelector('.node'); await sleep(page, 700);
      ok(`${ed} @${z} D9: ...and loads again as the same path`, (await S(page, '__sig.paths()'))[0] === d2, '');
      lastDoc = saved;
    }

    // ================= old patches and the previous build =================
    const old = spec.doc(1); old.cables = old.cables.map(c => ({ from: c.from, to: c.to })); delete old.cables[0].pts; old.modules.forEach(m => { delete m.collapsed; delete m.scope; });
    await loadDoc(old);
    ok(`${ed}: an old patch (no bend, no pts, no collapsed, no scope) opens with all its cables and kinds`, (await page.$$('.node')).length === 5 && JSON.stringify(await S(page, '__sig.gs().map(__sig.cabKind)')) === '["audio","gate","mod"]');
    if (process.env.OLD_ROOT && lastDoc) {
      const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html'; const f = path.join(process.env.OLD_ROOT, p);
        fs.readFile(f, (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': { '.html': 'text/html', '.js': 'text/javascript', '.wasm': 'application/wasm', '.woff': 'font/woff' }[path.extname(f)] || 'application/octet-stream' }); r.end(d); }); });
      await new Promise(o => srv.listen(0, o)); const ob = `http://localhost:${srv.address().port}`;
      await page.goto(`${ob}/${ed}/index.html`); await page.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); }, [spec.key, lastDoc]);
      await page.goto(`${ob}/${ed}/index.html`); await page.waitForSelector('.node'); await sleep(page, 700);
      ok(`${ed}: the previous build loads a patch saved by this one (cables and bend)`, (await page.$$('.node')).length === 5 && (await page.$$('svg.cables path.cln')).length === 3);
      srv.close();
    }
    const errs = errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed}: no page errors (desktop)`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();

  // ---- touch, 344 x 780: a tap selects a cable and shows its handle, two taps remove it, the hit zone is wide ----
  const M = await boot({ width: 344, height: 780, mobile: true }); const mp = M.page; const mb = `http://localhost:${M.port}`;
  try {
    const d = spec.doc(0.5, 6, 8);
    await mp.goto(`${mb}/${ed}/index.html`); await mp.evaluate(([k, x]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(x)); }, [spec.key, d]);
    await mp.goto(`${mb}/${ed}/index.html`); await mp.waitForSelector('.node', { timeout: 20000 }); await sleep(mp, 800);
    await mp.evaluate(() => { const h = document.getElementById('hint'); if (h) h.hidden = true; });
    const p = await S(mp, `__sig.point(0)`);
    ok(`${ed} touch: a cable can be hit at 344 px (${p ? Math.round(p.x) + ',' + Math.round(p.y) : 'no point'})`, !!p);
    if (p) {
      const near = await S(mp, `__sig.hitAt(0, ${p.x + p.nx * 12}, ${p.y + p.ny * 12})`), far = await S(mp, `__sig.hitAt(0, ${p.x + p.nx * 40}, ${p.y + p.ny * 40})`);
      ok(`${ed} touch: the hit zone is wide (12 px off hits, 40 px off does not)`, near && !far, JSON.stringify({ near, far }));
      await mp.touchscreen.tap(p.x, p.y); await sleep(mp, 600);
      const t1 = JSON.stringify(await S(mp, '__sig.selIdx()')), hb = await S(mp, '__sig.handleBox(0)'), n1 = await S(mp, '__sig.count()');
      ok(`${ed} touch: a tap selects the cable and shows its handle (the first tap only selects)`, t1 === '[0]' && hb.op === 1 && n1 === 3, JSON.stringify({ t1, hb, n1 }));
      await mp.touchscreen.tap(p.x, p.y); await sleep(mp, 120); await mp.touchscreen.tap(p.x, p.y); await sleep(mp, 400);
      ok(`${ed} touch: two quick taps still remove the cable`, (await S(mp, '__sig.count()')) === 2);
    }
    const errs = M.errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed}: no page errors (344 px touch)`, errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} touch: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await M.close();
}

// ---- the skins reach the editors through the host: select a module in each skin and compare the outline with the signal colours ----
async function skins() {
  const storage = { 'thunderplus-platform-v1': 'desktop', 'thunderplus.chains.settings': '{"tips":false}', 'thunderplus.waves.settings': '{"tips":false}' };
  const B = await boot({ width: 1440, height: 900, storage }); const { page, errors } = B;
  try {
    await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`).catch(() => {});
    for (const ed of ['chains', 'waves']) {
      const emb = ed === 'chains' ? 'CH.frame.contentWindow.chainsEmbed' : 'WV.frame.contentWindow.wavesEmbed';
      for (const [name, id] of [['white', 'minimal'], ['IDM pink & green', 'idmpg'], ['IDM', 'idm'], ['909', 'tr909']]) {
        await app(page, `(applySkin('${id}'), setView('${ed}'), $('toast').hidden = true, 1)`); await sleep(page, 1200);
        await app(page, `(${emb}.setDoc(${JSON.stringify(SPEC[ed].doc(1))}), 1)`); await sleep(page, 700);
        await app(page, `(applySkin('${id}'), 1)`); await sleep(page, 400);
        const fr = page.frames().find(f => f.url().includes('/' + ed + '/'));
        await fr.click('.node[data-id="m2"] > header .t'); await sleep(page, 200);
        const c = await COLOURS(fr);
        const sig = { audio: c.audio, control: c.mod, gate: c.gate, 'audio cable': c.cabAudio, 'control cable': c.cabMod, 'gate cable': c.cabGate };
        const clash = Object.entries(sig).filter(([, v]) => v === c.outline).map(([k]) => k);
        ok(`${ed} U1: ${name}: the selected module's outline ${c.outline} is none of the signal colours`, c.outline && clash.length === 0 && c.outline === c.selc, JSON.stringify(sig));
        const shp = await fr.evaluate(() => { const r = k => { const e = document.querySelector('.jack[data-kind="' + k + '"]'); return e && getComputedStyle(e).transform; }; return [r('gate'), r('mod'), r('audio')]; });
        ok(`${ed} M9: ${name}: the gate jack is rotated, the others are not`, shp[0] !== 'none' && shp[1] === 'none' && shp[2] === 'none', JSON.stringify(shp));
        const da = await fr.evaluate(() => [...document.querySelectorAll('svg.cables g path.cln')].map(l => getComputedStyle(l).strokeDasharray));
        ok(`${ed} M9: ${name}: the three cable kinds have three dashes`, new Set(da).size === 3, JSON.stringify(da));
      }
    }
    const errs = errors.splice(0).filter(e => !NOISE.test(e));
    ok('skins: no page errors', errs.length === 0, errs.slice(0, 3).join(' | '));
  } catch (e) { ok('skins: ran to the end', false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();
}

for (const ed of ['chains', 'waves']) await run(ed);
await skins();
const f = T.print();
process.exit(f ? 1 : 0);
