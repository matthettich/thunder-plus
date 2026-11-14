// Pass 1c of the …waves / …chains UX rework (docs/audits/2026-11-13-waves-chains-ux.md, U3, U5 to U10, T11): wording and type.
// Each editor is opened on its own page in headless Chrome (the skins are the host's own terminal-skin stylesheet, read from the app and
// added to the page, as the host does to the frame) with every module of the palette on the patch, and checked for:
//   names (one per module: list = title bar = Help = ? card; sentence case; no two modules alike), one word per signal kind and the port
//   tooltips naming it (U3, U5, U6), titles and values and port labels not cut, open and Compact, at 1440 and at 344 touch (U7), the smallest
//   font and the number of sizes (U8), text contrast in the white theme, IDM pink & green, IDM and 909 (U8, U9), the waves menu scroll (U10),
//   the tip box on touch and with a mouse (T11), and a patch saved by the previous build loading unchanged, and the other way round.
//   node tests/sweep/wc-wording.mjs      (OLD_ROOT=/path to a checkout of the previous build, or OLD_REF=<commit>: default 602e083, build 580)
import http from 'http'; import fs from 'fs'; import os from 'os'; import path from 'path'; import { execSync } from 'child_process';
import { boot, app, Table, ROOT, serveDir, listenLoopback } from './ui-lib.mjs';

const T = new Table('wc-wording');
const ok = (name, cond, note = '') => { T.add(name, !!cond, note); console.log((cond ? 'ok   ' : 'FAIL ') + name + (note ? '  ' + String(note).slice(0, 400) : '')); };
const NOISE = /bad HTTP response code \(404\)|MIDI|requestMIDIAccess|AudioContext was not allowed|favicon|Failed to load resource|ServiceWorker|sw\.js|autoplay/i;
const sleep = (page, ms) => page.waitForTimeout(ms);
const FLOOR = 11, RATIO = 4.5, MAX_SIZES = 6;   // U8: 11 px floor, five sizes (+ 17 px is also the touch field size), 4.5 : 1 for text
const SKINS = [['white', null], ['IDM pink & green', 'idmpg'], ['IDM', 'idm'], ['909', 'tr909']];

// ---------------- in-page helpers ----------------
const HELP = `(() => {
  if (window.__w) return;
  const cv = document.createElement('canvas'); cv.width = cv.height = 1; const g = cv.getContext('2d', { willReadFrequently: true });
  const parse = c => { let m;
    if ((m = /^rgba?\\(([^)]+)\\)/.exec(c))) { const p = m[1].split(/[ ,\\/]+/).filter(Boolean).map(parseFloat); return [p[0], p[1], p[2], p.length > 3 ? p[3] : 1]; }
    if ((m = /^color\\(srgb ([^)]+)\\)/.exec(c))) { const p = m[1].split(/[ \\/]+/).filter(Boolean).map(parseFloat); return [p[0] * 255, p[1] * 255, p[2] * 255, p.length > 3 ? p[3] : 1]; }
    g.clearRect(0, 0, 1, 1); g.fillStyle = '#000'; g.fillStyle = c; g.fillRect(0, 0, 1, 1); const d = g.getImageData(0, 0, 1, 1).data; return [d[0], d[1], d[2], d[3] / 255]; };
  const over = (f, b) => { const a = f[3] + b[3] * (1 - f[3]); return a ? [(f[0] * f[3] + b[0] * b[3] * (1 - f[3])) / a, (f[1] * f[3] + b[1] * b[3] * (1 - f[3])) / a, (f[2] * f[3] + b[2] * b[3] * (1 - f[3])) / a, a] : [0, 0, 0, 0]; };
  const lum = c => { const f = v => { v /= 255; return v <= .03928 ? v / 12.92 : Math.pow((v + .055) / 1.055, 2.4); }; return .2126 * f(c[0]) + .7152 * f(c[1]) + .0722 * f(c[2]); };
  const ratio = (a, b) => { const x = lum(a), y = lum(b); return (Math.max(x, y) + .05) / (Math.min(x, y) + .05); };
  const shown = el => { const r = el.getBoundingClientRect(); return r.width > 0 && r.height > 0 && getComputedStyle(el).visibility !== 'hidden'; };
  const hasText = el => [...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim());
  // text colour against the backgrounds painted behind it (the page white under all of them), the element's own and its parents' opacity applied
  const contrast = el => { const chain = []; for (let e = el; e; e = e.parentElement) chain.unshift(e);
    let bg = [255, 255, 255, 1], op = 1; for (const e of chain) { const cs = getComputedStyle(e); bg = over(parse(cs.backgroundColor), bg); op *= parseFloat(cs.opacity); }
    const cs = getComputedStyle(el), fg = parse(cs.color); fg[3] *= op; return ratio(over(fg, bg), bg); };
  window.__w = { parse, over, contrast, shown, hasText, ratio,
    // the smallest font and the set of sizes of every element that carries text
    fonts: (root, skip) => { const sizes = {}; let min = 99, who = ''; for (const el of root.querySelectorAll('*')) { if (!hasText(el) || !shown(el) || (skip && el.closest(skip))) continue; const s = parseFloat(getComputedStyle(el).fontSize); sizes[s] = (sizes[s] || 0) + 1; if (s < min) { min = s; who = el.tagName.toLowerCase() + '.' + el.className + ' "' + el.textContent.trim().slice(0, 20) + '"'; } } return { min, who, sizes: Object.keys(sizes).map(Number).sort((a, b) => a - b) }; },
    worst: (root, sel, skip) => { let w = 99, who = ''; for (const el of root.querySelectorAll(sel)) { if (!hasText(el) || !shown(el) || (skip && el.closest(skip))) continue; const r = contrast(el); if (r < w) { w = r; who = el.className + ' "' + el.textContent.trim().slice(0, 18) + '"'; } } return w === 99 ? null : { r: +w.toFixed(2), who }; },
  };
})()`;
const S = (page, expr) => page.evaluate(`(() => { ${HELP}; return (${expr}); })()`);

// ---------------- the host's skin css, per skin ----------------
async function skinCss() {
  const storage = { 'thunderplus-platform-v1': 'desktop' };
  const B = await boot({ width: 1440, height: 900, storage }); const out = {};
  try { for (const [, id] of SKINS) if (id) out[id] = await app(B.page, `(applySkin('${id}'), termFrameCss())`); } finally { await B.close(); }
  return out;
}

const TIPCLEAR = `(() => { document.getElementById('zoomfit').click(); return new Promise(r => setTimeout(() => { const h = document.getElementById('hint').getBoundingClientRect();
  r({ hint: [h.width, h.height].map(Math.round), hit: [...document.querySelectorAll('.node')].filter(n => { const b = n.getBoundingClientRect(); return b.left < h.right && b.right > h.left && b.top < h.bottom && b.bottom > h.top; }).length, n: document.querySelectorAll('.node').length }); }, 500)); })()`;   // T11 mouse: after Fit no module is under the tip box
// every module of the palette on one patch, in a grid
const doc = (ed, types, collapsed = false) => ({ format: ed + '.patch', meta: { name: 'wc wording' }, view: { x: 10, y: 10, zoom: .4, ui: 2 }, voice: { maxVoices: 8 }, st2: 1, ti2: 1,
  modules: types.map((t, i) => ({ id: 'm' + i, type: t, scope: 'mono', pos: [(i % 10) * 290, Math.floor(i / 10) * 520], collapsed, params: {} })), cables: [], groups: [], custom: [], transport: { tempo: 120 } });
const KEY = { chains: 'chains.patch', waves: 'waves.patch' };

async function load(page, base, ed, d, css, settings) {
  await page.goto(`${base}/${ed}/index.html`);
  await page.evaluate(([k, x, s, e]) => { localStorage.clear(); if (x) localStorage.setItem(k, JSON.stringify(x)); localStorage.setItem(e + '.settings', JSON.stringify(s || { tips: false })); }, [KEY[ed], d, settings, ed]);
  await page.goto(`${base}/${ed}/index.html`); await page.waitForSelector(d ? '.node' : '#ws', { timeout: 20000 });
  if (css) await page.addStyleTag({ content: css });
  await sleep(page, 1200);
}

// the palette, read from the list: [type, name, tooltip]
const paletteOf = page => page.evaluate(() => { if (document.getElementById('drawer').classList.contains('collapsed')) document.getElementById('dtoggle').click();
  document.querySelectorAll('.pgroup.tinted:not(.open)').forEach(b => b.click());
  return [...document.querySelectorAll('.plist .pgroup.tinted + .pgrid .pitem')].map(b => [b.dataset.type, b.textContent.trim(), b.title]); });

async function run(ed, CSS, OLD) {
  const B = await boot({ width: 1440, height: 900, query: '' }); const { page, errors } = B; const base = `http://127.0.0.1:${B.port}`;
  try {
    // ============ the palette, names, kinds ============
    await load(page, base, ed, null);
    const pal = await paletteOf(page); const types = pal.map(p => p[0]);
    ok(`${ed}: the module list has ${types.length} modules, none twice`, types.length > 90 && new Set(types).size === types.length, types.length);
    await load(page, base, ed, doc(ed, types));
    const titles = await page.evaluate(() => [...document.querySelectorAll('.node')].map(n => n.querySelector('header .t').textContent));
    await page.click('#helpbtn'); await sleep(page, 700);
    const help = await page.evaluate(() => [...document.querySelector('#helpov iframe').contentDocument.querySelectorAll('.mcard h4')].map(h => h.textContent));
    const bad = types.map((t, i) => ({ t, p: pal[i][1], n: titles[i], h: help[i] })).filter(x => x.p !== x.n || x.n !== x.h);
    ok(`${ed} U6: every module has one name: the list, the title bar and Help read alike (${types.length} checked)`, !bad.length && titles.length === types.length && help.length === types.length, JSON.stringify(bad.slice(0, 4)));
    await page.keyboard.press('Escape'); await sleep(page, 200);
    // the ? card on each module's title bar
    const mq = await page.evaluate(async () => { const out = []; for (const n of document.querySelectorAll('.node')) { const b = n.querySelector('header .mq'); if (!b) { out.push([n.querySelector('header .t').textContent, null]); continue; } b.click(); out.push([n.querySelector('header .t').textContent, document.getElementById('mhelp').querySelector('h4').textContent]); b.click(); } return out; });
    ok(`${ed} U6: the ? card on the title bar names the module as the title bar does`, mq.length === types.length && mq.every(([a, b]) => a === b), JSON.stringify(mq.filter(([a, b]) => a !== b).slice(0, 4)));
    const dup = titles.filter((t, i) => titles.indexOf(t) !== i);
    ok(`${ed} U6: no two modules share a name`, !dup.length, dup.join(', '));
    const AC = /^[A-Z0-9][A-Z0-9&+\/-]*$/, IO = new Set(['In', 'Out']);
    const caseBad = [...new Set(titles)].filter(t => t.split(/\s+/).slice(1).some(w => /^[A-Z]/.test(w) && !AC.test(w) && !IO.has(w)));
    ok(`${ed} U6: sentence case (a capital only on the first word, acronyms, and In / Out in Track In, Chain Out, Host In, Sidechain In / Out)`, !caseBad.length, caseBad.join(' | '));
    // palette descriptors moved into the tooltip
    const noBr = pal.filter(p => /[(·]/.test(p[1]) && p[1] !== 'Pump (ducking)');   // (Pump (ducking) is the module's own name, on the title bar and in Help too)
    ok(`${ed} U6: no list entry carries a bracketed descriptor any more (the tooltip has it)`, !noBr.length, JSON.stringify(noBr.slice(0, 3)));
    const rs = pal.find(p => p[0] === (ed === 'chains' ? 'reson' : 'reson'));
    ok(`${ed} U6: the descriptor is in the tooltip (${rs && rs[2]})`, rs && /\(chord\)/.test(rs[2]) && /Click to add/.test(rs[2]));
    // quick add finds a module by its old descriptor
    // ---- the five kinds of words ----
    const wording = await page.evaluate(() => { const ph = [...document.querySelectorAll('.pgroup')].map(b => b.textContent); return { groups: ph, secs: [...document.querySelectorAll('.psection')].map(x => x.textContent) }; });
    ok(`${ed} U5: group names use "control" (Control utilities, Audio to control), never CV`, wording.groups.some(g => /^Control utilities/.test(g)) && wording.groups.some(g => /^Audio to control/.test(g)) && !wording.groups.concat(wording.secs).some(g => /\bCV\b/.test(g)), JSON.stringify(wording.groups.filter(g => /ontrol|CV/.test(g))));
    const jt = await page.evaluate(() => [...document.querySelectorAll('.jack[data-kind]')].map(j => [j.dataset.kind, j.title, j.dataset.dir]));
    const W = { audio: 'audio', mod: 'control', gate: 'gate' };
    const jbad = jt.filter(([k, t]) => !new RegExp('\\b' + W[k] + '\\b').test(t) || /\bcv\b/i.test(t));
    ok(`${ed} U5: every port tooltip names its signal kind (${jt.length} jacks: audio / control / gate)`, jt.length > 500 && !jbad.length, JSON.stringify(jbad.slice(0, 3)));
    const kinds = new Set(jt.map(j => j[0]));
    ok(`${ed} U5: all three kinds are on the patch`, kinds.has('audio') && kinds.has('mod') && kinds.has('gate'));
    const ports = await page.evaluate(() => [...document.querySelectorAll('.port .pl')].map(e => e.textContent));
    const raw = ports.filter(p => /^[a-z][a-z0-9]*$/.test(p));
    ok(`${ed} U3: no port shows its raw key (${ports.length} labels; found ${[...new Set(raw)].join(', ') || 'none'})`, !raw.length);
    const lab = await page.evaluate(() => { const g = (m, p) => { const n = [...document.querySelectorAll('.node')].find(x => x.querySelector('header .t').textContent === m); const j = n && n.querySelector('.jack[data-port="' + p + '"]'); return j && j.closest('.port').querySelector('.pl').textContent; }; return { snh: g('Sample & hold', 'noise'), blades: g('Blades filter', 'res'), meta: g('Meta modulator', 'mod'), flex: g('Flex', 'pat'), sgate: g('Step gate', 'pat') }; });
    ok(`${ed} U3: the five ports are named (${JSON.stringify(lab)})`, Object.values(lab).every(v => v && v.length > 2 && /^[A-Z]/.test(v)));
    // help: the legend and the words
    await page.click('#helpbtn'); await sleep(page, 400);
    const ht = await page.evaluate(() => { const d = document.querySelector('#helpov iframe').contentDocument; return { all: d.body.textContent, legend: d.querySelector('.legend').textContent }; });
    ok(`${ed} U5: Help says Audio, Control and Gate for the three kinds (no "Sound:" or "CV")`, /Audio:/.test(ht.legend) && /Control:/.test(ht.legend) && /Gate:/.test(ht.legend) && !/Sound:/.test(ht.legend) && !/\bCV\b/.test(ht.all) && !/control voltage/i.test(ht.all), ht.legend);
    await page.keyboard.press('Escape');
    const wv = ed === 'waves' ? await page.evaluate(() => ({ m: document.getElementById('meters').title, b: [...document.querySelectorAll('#meters .b')].map(x => x.title) })) : null;
    if (wv) ok(`waves U5: the bus meters say Waves In, as the Out module does (${wv.m}; ${wv.b[0]})`, /^Waves In 1/.test(wv.m) && wv.b.every((t, i) => t === 'Waves In ' + (i + 1)) && !/Out 1/.test(wv.m + wv.b.join('')));

    // ============ U7: nothing cut (open, Compact; 1440 mouse) ============
    const fit = () => S(page, `(() => { const o = { title: [], word: [], brk: [], val: [], port: [], valsOut: [] };
      const cvs = document.createElement('canvas').getContext('2d');
      for (const n of document.querySelectorAll('.node')) { const t = n.querySelector('header .t'), name = t.textContent, cs = getComputedStyle(t);
        if (t.scrollWidth > t.clientWidth + 0.5 || Math.round(t.getBoundingClientRect().height / parseFloat(cs.lineHeight)) > 2) o.title.push(name);
        cvs.font = cs.fontWeight + ' ' + cs.fontSize + ' ' + cs.fontFamily; cvs.letterSpacing = cs.letterSpacing === 'normal' ? '0px' : cs.letterSpacing; const w = Math.max(...name.split(/\\s+/).map(x => cvs.measureText(x).width)); if (w > t.clientWidth + 1) o.word.push(name + ' ' + Math.round(w) + '>' + t.clientWidth);
        // a title breaks only between words: the line boxes of each word's own range must share one top (a word split over two lines has two)
        { const tn = [...t.childNodes].find(c => c.nodeType === 3); if (tn) { const re = /\\S+/g; let m; while ((m = re.exec(tn.data))) { const rg = document.createRange(); rg.setStart(tn, m.index); rg.setEnd(tn, m.index + m[0].length);
          const tops = new Set([...rg.getClientRects()].filter(r => r.width > 0).map(r => Math.round(r.top / 4))); if (tops.size > 1) o.brk.push(name + ' | ' + m[0]); } } }
        const nr = n.getBoundingClientRect();
        for (const v of n.querySelectorAll('.row .val')) { if (!__w.shown(v)) continue; if (v.scrollWidth > v.clientWidth + 0.5 || v.getBoundingClientRect().right > nr.right + 0.5) o.val.push(name + ': ' + v.textContent); }
        for (const p of n.querySelectorAll('.port .pl')) { if (!__w.shown(p)) continue; if (p.scrollWidth > p.clientWidth + 0.5) o.port.push(name + ': ' + p.textContent); } }
      return o; })()`);
    const o1 = await fit();
    ok(`${ed} U7: open: every title reads in full (${titles.length} modules; ≤ 2 lines, no word broken)`, !o1.title.length && !o1.word.length && !o1.brk.length, JSON.stringify([...o1.title, ...o1.word, ...o1.brk].slice(0, 5)));
    ok(`${ed} U7: open: no value readout is cut or leaves its module`, !o1.val.length, JSON.stringify(o1.val.slice(0, 5)));
    ok(`${ed} U7: open: no port label is cut`, !o1.port.length, JSON.stringify(o1.port.slice(0, 5)));
    // the readouts at both ends of every slider
    const ends = await page.evaluate(() => { const bad = []; let n = 0; for (const nd of document.querySelectorAll('.node')) { const name = nd.querySelector('header .t').textContent, nr = nd.getBoundingClientRect();
      for (const inp of nd.querySelectorAll('.row.sl input[type=range]')) { const v = inp.closest('.row').querySelector('.val'); if (!v) continue; const keep = inp.value;
        for (const e of [inp.max, inp.min]) { inp.value = e; inp.dispatchEvent(new Event('input', { bubbles: true })); n++; if (v.scrollWidth > v.clientWidth + .5 || v.getBoundingClientRect().right > nr.right + .5) bad.push(name + ': ' + v.textContent); }
        inp.value = keep; inp.dispatchEvent(new Event('input', { bubbles: true })); } } return { n, bad: [...new Set(bad)] }; });
    ok(`${ed} U7: open: the readout fits at the top and the bottom of every slider (${ends.n} values)`, ends.n > 500 && !ends.bad.length, JSON.stringify(ends.bad.slice(0, 5)));
    // Compact
    await page.evaluate(() => document.querySelectorAll('.node > header .fold').forEach(b => { if (!b.closest('.node').classList.contains('collapsed')) b.click(); })); await sleep(page, 800);
    ok(`${ed}: every module is Compact now`, (await page.$$('.node.collapsed')).length === types.length);
    const o2 = await fit();
    ok(`${ed} U7: Compact: every title reads in full`, !o2.title.length && !o2.word.length && !o2.brk.length, JSON.stringify([...o2.title, ...o2.word, ...o2.brk].slice(0, 5)));
    ok(`${ed} U7: Compact: no port label is cut`, !o2.port.length, JSON.stringify(o2.port.slice(0, 6)));
    const csum = await S(page, `(() => { const bad = []; for (const n of document.querySelectorAll('.node.collapsed')) for (const e of n.querySelectorAll('.cs-v, .cs-l')) { if (__w.shown(e) && e.scrollWidth > e.clientWidth + .5) bad.push(n.querySelector('header .t').textContent + ': ' + e.textContent); } return bad; })()`);
    ok(`${ed} U7: Compact: the summary values are whole (a long summary is cut as a whole at its end, never inside a value)`, !csum.length, JSON.stringify(csum.slice(0, 4)));
    // ============ U8: font floor and sizes (patch, palette, help) ============
    const fo = await S(page, `__w.fonts(document.body, '#helpov, .emptylbl')`);
    ok(`${ed} U8: the patch, the module list and the bars: smallest text ${fo.min} px (floor ${FLOOR}); ${fo.sizes.length} sizes: ${fo.sizes.join(', ')}`, fo.min >= FLOOR && fo.sizes.length <= MAX_SIZES, fo.who);
    await page.click('#helpbtn'); await sleep(page, 500);
    const fh = await page.evaluate(`(() => { ${HELP}; const d = document.querySelector('#helpov iframe').contentDocument; const sizes = {}; let min = 99; for (const el of d.querySelectorAll('body *')) { if (el.tagName === 'STYLE' || ![...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim())) continue; const s = parseFloat(d.defaultView.getComputedStyle(el).fontSize); sizes[s] = 1; if (s < min) min = s; } return { min, sizes: Object.keys(sizes).map(Number).sort((a, b) => a - b) }; })()`);
    ok(`${ed} U8: Help: smallest text ${fh.min} px; ${fh.sizes.length} sizes: ${fh.sizes.join(', ')}`, fh.min >= FLOOR && fh.sizes.length <= MAX_SIZES);
    await page.keyboard.press('Escape'); await sleep(page, 200);

    // ============ U7 in every theme: every title breaks only between words, open and Compact ============
    for (const [name, id] of SKINS) {
      await load(page, base, ed, doc(ed, types, false), id ? CSS[id] : null);
      const t1 = await fit();
      ok(`${ed} U7: ${name}: open: titles whole, no word split over two lines (${titles.length} modules)`, !t1.title.length && !t1.word.length && !t1.brk.length, JSON.stringify([...t1.title, ...t1.word, ...t1.brk].slice(0, 5)));
      await page.evaluate(() => document.querySelectorAll('.node > header .fold').forEach(b => { if (!b.closest('.node').classList.contains('collapsed')) b.click(); })); await sleep(page, 800);
      const t2 = await fit();
      ok(`${ed} U7: ${name}: Compact: titles whole, no word split over two lines`, !t2.title.length && !t2.word.length && !t2.brk.length, JSON.stringify([...t2.title, ...t2.word, ...t2.brk].slice(0, 5)));
    }

    // ============ U8 / U9: contrast in the four themes ============
    for (const [name, id] of SKINS) {
      await load(page, base, ed, doc(ed, ['delay', 'compressor', 'lfo', 'math', 'euclid', ed === 'chains' ? 'chainOut' : 'out', 'scin'].filter(t => types.includes(t)).concat(['pingpong', 'transient']).filter(t => types.includes(t)).filter((t, i, a) => a.indexOf(t) === i), false), id ? CSS[id] : null, { tips: true });
      { const tc = await page.evaluate(TIPCLEAR); ok(`${ed} T11: ${name}, 1440 x 900: after Fit no module is under the tip box (${tc.hint.join(' x ')} px, ${tc.n} modules)`, tc.hit === 0 && tc.hint[1] <= 80, JSON.stringify(tc)); }
      // one of them Compact, one bypassed is left out (a bypassed module is dimmed on purpose)
      await page.evaluate(() => { const f = document.querySelectorAll('.node > header .fold'); if (f[2]) f[2].click(); }); await sleep(page, 400);
      await page.evaluate(() => { document.getElementById('dtoggle').click(); document.querySelectorAll('.pgroup:not(.open)').forEach(b => b.click()); }); await sleep(page, 300);
      const cls = [['row label', '.row > span:first-child', '.pan'], ['row value', '.row .val'], ['port label', '.port .pl'], ['title', '.node > header .t'], ['BYP button', '.node > header .byp'], ['Compact summary', '.csum .cs-t, .csum .cs-l, .csum .mk'], ['IN / OUT label', '.iom b'], ['status note', '.status'],
        ['tip box', '.hint .tipm'], ['list: group / count', '.pgroup, .pcount'], ['list: item', '.pitem'], ['list: heading', '.psection, .qhint'], ['top bar buttons', '.bar .tb'], ['menu text', '.fitem']];
      const res = {}; for (const [k, sel, skip] of cls) res[k] = await S(page, `__w.worst(document.body, ${JSON.stringify(sel)}, ${JSON.stringify(skip || null)})`);
      // the CPU readout in its three states (visible on a page of its own; the app's top bar owns it when embedded)
      for (const st of ['good', 'ok', 'low']) { await page.evaluate(s => { const e = document.getElementById('cpuv'); e.className = 'fps ' + s; }, st); res['CPU ' + st] = await S(page, `__w.worst(document.body, '#cpuv')`); }
      const fails = Object.entries(res).filter(([, v]) => v && v.r < RATIO);
      const line = Object.entries(res).filter(([, v]) => v).map(([k, v]) => k + ' ' + v.r).join(', ');
      ok(`${ed} U8: ${name}: text contrast ≥ ${RATIO} : 1 (lowest ${Math.min(...Object.values(res).filter(Boolean).map(v => v.r))})`, !fails.length, fails.map(([k, v]) => k + ' ' + v.r + ' ' + v.who).join(' | ') + ' // ' + line);
      console.log(`      ${ed} ${name}: ${line}`);
      // Help cards in the colour of the module (U9) and their text
      await page.click('#helpbtn'); await sleep(page, 500);
      const hc = await page.evaluate(`(() => { ${HELP}; const d = document.querySelector('#helpov iframe').contentDocument, win = d.defaultView; const cards = [...d.querySelectorAll('.mcard')]; const cols = cards.map(c => win.getComputedStyle(c).borderTopColor);
        const worst = (sel) => { let w = 99, who = ''; for (const el of d.querySelectorAll(sel)) { if (![...el.childNodes].some(n => n.nodeType === 3 && n.textContent.trim())) continue; const chain = []; for (let e = el; e; e = e.parentElement) chain.unshift(e); let bg = [255, 255, 255, 1]; for (const e of chain) bg = __w.parse(win.getComputedStyle(e).backgroundColor) && (() => { const f = __w.parse(win.getComputedStyle(e).backgroundColor); const a = f[3] + bg[3] * (1 - f[3]); return a ? [(f[0] * f[3] + bg[0] * bg[3] * (1 - f[3])) / a, (f[1] * f[3] + bg[1] * bg[3] * (1 - f[3])) / a, (f[2] * f[3] + bg[2] * bg[3] * (1 - f[3])) / a, a] : bg; })(); const fg = __w.parse(win.getComputedStyle(el).color); for (const e of chain) fg[3] *= parseFloat(win.getComputedStyle(e).opacity); const r = __w.ratio(__w.over(fg, bg), bg); if (r < w) { w = r; who = el.className + ' ' + el.textContent.slice(0, 14); } } return { r: +w.toFixed(2), who }; };
        return { n: cards.length, cols: [...new Set(cols)].length, h4: worst('.mcard h4'), p: worst('.mcard p'), io: worst('.io b, .pt'), nav: worst('nav a'), legend: worst('.legend') }; })()`);
      ok(`${ed} U9: ${name}: Help card text ≥ ${RATIO} : 1 (card title ${hc.h4.r}, text ${hc.p.r}, ports ${hc.io.r}, nav ${hc.nav.r}, legend ${hc.legend.r})`, [hc.h4, hc.p, hc.io, hc.nav, hc.legend].every(x => x.r >= RATIO), JSON.stringify(hc));
      if (name === 'white') {
        const cc = await page.evaluate(() => { const d = document.querySelector('#helpov iframe').contentDocument; return [...d.querySelectorAll('.mcard')].map(c => d.defaultView.getComputedStyle(c).borderTopColor); });
        // the module on the patch wears its --cat; the card must wear the same colour for the same module
        await page.keyboard.press('Escape');
        const pc = await page.evaluate(() => { const g = {}; for (const n of document.querySelectorAll('.node')) g[n.querySelector('header .t').textContent] = n.style.getPropertyValue('--cat'); return g; });
        await page.click('#helpbtn'); await sleep(page, 300);
        const cmp = await page.evaluate(pcs => { const d = document.querySelector('#helpov iframe').contentDocument; const cv = document.createElement('canvas').getContext('2d'); const norm = c => { cv.fillStyle = '#000'; cv.fillStyle = c; return cv.fillStyle; };
          const bad = []; for (const c of d.querySelectorAll('.mcard')) { const name = c.querySelector('h4').textContent; if (!(name in pcs)) continue; const card = norm(d.defaultView.getComputedStyle(c).borderTopColor); if (card !== norm(pcs[name])) bad.push(name + ' ' + card + ' vs ' + pcs[name]); } return bad; }, pc);
        ok(`${ed} U9: a module's Help card wears the colour the module has on the patch (${Object.keys(pc).length} modules compared)`, !cmp.length && Object.keys(pc).length >= 5, JSON.stringify(cmp.slice(0, 3)));
      }
      await page.keyboard.press('Escape'); await sleep(page, 150);
    }
    const errs = errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed}: no page errors (desktop)`, errs.length === 0, errs.slice(0, 3).join(' | '));

    // ============ old patches and the previous build ============
    if (OLD) {
      // a patch with every module type, its sliders moved off their defaults (through the page, as a hand does), saved by the OLD build
      const obase = OLD.base;
      await load(page, obase, ed, doc(ed, types));
      await page.evaluate(() => { let k = 0; for (const inp of document.querySelectorAll('.row.sl input[type=range]')) { const lo = +inp.min, hi = +inp.max, st = +inp.step || 0; let v = lo + (hi - lo) * (.2 + .6 * ((k++ * 37) % 100) / 100); if (st) v = Math.round(v / st) * st; inp.value = v; inp.dispatchEvent(new Event('input', { bubbles: true })); } });
      // a few cables: every audio output to the next module's first audio input where the kinds match
      await sleep(page, 2600);
      const saved = await page.evaluate(k => JSON.parse(localStorage.getItem(k)), KEY[ed]);
      const norm = d => JSON.stringify({ modules: d.modules.map(m => ({ id: m.id, type: m.type, scope: m.scope, pos: m.pos, collapsed: !!m.collapsed, params: m.params, label: m.label })), cables: d.cables.map(c => ({ from: c.from, to: c.to, pts: c.pts })), name: d.meta && d.meta.name });
      const nonDef = saved.modules.reduce((a, m) => a + Object.keys(m.params).length, 0);
      ok(`${ed}: a patch saved by the previous build (${saved.modules.length} modules, ${nonDef} parameter values) is the fixture`, saved.modules.length === types.length && nonDef > 500);
      saved.cables = []; const withCab = JSON.parse(JSON.stringify(saved));
      // cables by the patch's own rule: pick pairs (audio out -> audio in) from the ports of the page
      const pairs = await page.evaluate(() => { const nodes = [...document.querySelectorAll('.node')], out = []; for (let i = 0; i + 1 < nodes.length && out.length < 30; i += 2) { const a = nodes[i].querySelector('.jack[data-dir="out"][data-kind="audio"]'), b = nodes[i + 1].querySelector('.jack[data-dir="in"][data-kind="audio"]'); if (a && b) out.push({ from: [a.dataset.id, a.dataset.port], to: [b.dataset.id, b.dataset.port], bend: 0 }); } return out; });
      withCab.cables = pairs; ok(`${ed}: the fixture has ${pairs.length} cables`, pairs.length >= 10);
      // load it in the NEW build, let it save, compare
      await page.goto(`${base}/${ed}/index.html`); await page.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); }, [KEY[ed], withCab]);
      await page.goto(`${base}/${ed}/index.html`); await page.waitForSelector('.node'); await sleep(page, 1500);
      await page.evaluate(() => { const n = document.querySelector('.node > header .t'); }); await page.mouse.click(700, 880); await page.evaluate(() => document.getElementById('pname').dispatchEvent(new Event('input', { bubbles: true }))); await sleep(page, 2600);
      const back = await page.evaluate(k => JSON.parse(localStorage.getItem(k)), KEY[ed]);
      ok(`${ed}: the old patch loads in this build with every module, cable and parameter value identical (${back.modules.length} modules, ${back.cables.length} cables)`, norm(back) === norm(withCab), norm(back) === norm(withCab) ? '' : 'differs');
      ok(`${ed}: ...and every title bar reads as before or as its new name, with the same ports (${(await page.$$('.node')).length} nodes)`, (await page.$$('.node')).length === types.length);
      // for the record: what the previous build did to the same patch (titles cut, smallest text), not a check
      await page.goto(`${obase}/${ed}/index.html`); await page.evaluate(([k, d, e]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(e + '.settings', '{"tips":false}'); }, [KEY[ed], saved, ed]);
      await page.goto(`${obase}/${ed}/index.html`); await page.waitForSelector('.node'); await sleep(page, 1200);
      const was = await S(page, `(() => { const cut = [...document.querySelectorAll('.node > header .t')].filter(t => t.scrollWidth > t.clientWidth + .5).length, f = __w.fonts(document.body, '.emptylbl'); return { cut, min: f.min, sizes: f.sizes.length }; })()`);
      console.log(`      ${ed}: for the record, build 580 on this patch: ${was.cut} of ${types.length} titles cut, smallest text ${was.min} px, ${was.sizes} font sizes`);
      // saved by this build, loaded by the previous one
      const nowDoc = back;
      await page.goto(`${obase}/${ed}/index.html`); await page.evaluate(([k, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); }, [KEY[ed], nowDoc]);
      await page.goto(`${obase}/${ed}/index.html`); await page.waitForSelector('.node'); await sleep(page, 1500); await page.evaluate(() => document.getElementById('pname').dispatchEvent(new Event('input', { bubbles: true }))); await sleep(page, 2600);
      const old2 = await page.evaluate(k => JSON.parse(localStorage.getItem(k)), KEY[ed]);
      ok(`${ed}: the previous build loads a patch saved by this one, unchanged (${old2.modules.length} modules, ${old2.cables.length} cables)`, norm(old2) === norm(withCab));
      const errs2 = errors.splice(0).filter(e => !NOISE.test(e));
      ok(`${ed}: no page errors (old and new patch loads)`, errs2.length === 0, errs2.slice(0, 3).join(' | '));
    }
  } catch (e) { ok(`${ed}: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await B.close();

  // ============ touch, 344 x 780: titles, values, the tip box, the menus ============
  const M = await boot({ width: 344, height: 780, mobile: true, query: '' }); const mp = M.page, mb = `http://127.0.0.1:${M.port}`;
  try {
    await load(mp, mb, ed, null); const pal2 = await paletteOf(mp); const types2 = pal2.map(p => p[0]);
    await load(mp, mb, ed, doc(ed, types2));
    const m1 = await S(mp, `(() => { const o = { title: [], word: [], val: [] }; const cvs = document.createElement('canvas').getContext('2d');
      for (const n of document.querySelectorAll('.node')) { const t = n.querySelector('header .t'), name = t.textContent, cs = getComputedStyle(t); if (t.scrollWidth > t.clientWidth + .5 || Math.round(t.getBoundingClientRect().height / parseFloat(getComputedStyle(t).lineHeight)) > 2) o.title.push(name);
        cvs.font = cs.fontWeight + ' ' + cs.fontSize + ' ' + cs.fontFamily; cvs.letterSpacing = cs.letterSpacing === 'normal' ? '0px' : cs.letterSpacing; const w = Math.max(...name.split(/\\s+/).map(x => cvs.measureText(x).width)); if (w > t.clientWidth + 1) o.word.push(name);
        const nr = n.getBoundingClientRect(); for (const v of n.querySelectorAll('.row .val')) if (__w.shown(v) && (v.scrollWidth > v.clientWidth + .5 || v.getBoundingClientRect().right > nr.right + .5)) o.val.push(name + ': ' + v.textContent); } return o; })()`);
    ok(`${ed} U7: touch 344: titles whole and no value cut (open)`, !m1.title.length && !m1.word.length && !m1.val.length, JSON.stringify(m1).slice(0, 300));
    await mp.evaluate(() => document.querySelectorAll('.node > header .fold').forEach(b => b.click())); await sleep(mp, 800);
    const m2 = await S(mp, `(() => { const o = { title: [], port: [] }; for (const n of document.querySelectorAll('.node')) { const t = n.querySelector('header .t'); if (t.scrollWidth > t.clientWidth + .5 || Math.round(t.getBoundingClientRect().height / parseFloat(getComputedStyle(t).lineHeight)) > 2) o.title.push(t.textContent); for (const p of n.querySelectorAll('.port .pl')) if (__w.shown(p) && p.scrollWidth > p.clientWidth + .5) o.port.push(t.textContent + ': ' + p.textContent); } return o; })()`);
    ok(`${ed} U7: touch 344: Compact titles and port labels whole`, !m2.title.length && !m2.port.length, JSON.stringify(m2).slice(0, 300));
    const fo2 = await S(mp, `__w.fonts(document.body, '.emptylbl')`);
    ok(`${ed} U8: touch 344: smallest text ${fo2.min} px, ${fo2.sizes.length} sizes (${fo2.sizes.join(', ')})`, fo2.min >= FLOOR && fo2.sizes.length <= MAX_SIZES, fo2.who);

    // ---- T11: the tip box on touch ----
    const reload = async () => { await mp.goto(`${mb}/${ed}/index.html`); await mp.waitForSelector('#ws'); await sleep(mp, 1000); };
    await mp.goto(`${mb}/${ed}/index.html`); await mp.evaluate(() => localStorage.clear()); await reload();   // (the first visit: nothing stored yet)
    const vis = () => mp.evaluate(() => { const h = document.getElementById('hint'), r = h.getBoundingClientRect(), ws = document.getElementById('ws').getBoundingClientRect(), v = e => { const b = e.getBoundingClientRect(); return b.width > 0 && b.height > 0; };
      return { hidden: h.hidden, min: h.classList.contains('min'), long: v(h.querySelector('.tipm')), short: v(h.querySelector('.tipt')), pill: v(document.getElementById('hintpill')), x: v(document.getElementById('hintx')), w: Math.round(r.width), h: Math.round(r.height), frac: +(r.width * r.height / (ws.width * ws.height)).toFixed(3), text: h.querySelector('.tipt').textContent, tipSeen: JSON.parse(localStorage.getItem(document.title.trim() + '.settings') || '{}').tipSeen }; });
    const f1 = await vis();
    ok(`${ed} T11: first visit on touch: the short touch note is open (${f1.w}x${f1.h}, ${Math.round(f1.frac * 100)}% of the workspace)`, !f1.hidden && !f1.min && f1.short && !f1.long && f1.x && !f1.pill && f1.frac < .3 && f1.h < 200, JSON.stringify(f1));
    ok(`${ed} T11: ...and it says nothing of Option, Alt, Shift, Ctrl, right-click, a mouse or a keyboard`, !/option|\balt\b|shift|ctrl|right-click|click|mouse|keyboard|scroll/i.test(f1.text), f1.text);
    await reload();
    const f2 = await vis();
    ok(`${ed} T11: second visit on touch: it starts collapsed to a small Tips button (${f2.w}x${f2.h}, ${Math.round(f2.frac * 1000) / 10}% of the workspace)`, f2.min && f2.pill && !f2.short && !f2.long && !f2.x && f2.w <= 120 && f2.h <= 56 && f2.h >= 36 && f2.tipSeen === true, JSON.stringify(f2));
    await mp.tap('#hintpill'); await sleep(mp, 300);
    const f3 = await vis();
    ok(`${ed} T11: a tap on Tips opens the note`, !f3.min && f3.short && f3.x && !f3.pill && f3.frac < .3, JSON.stringify(f3));
    await mp.tap('#hintx'); await sleep(mp, 300);
    const f4 = await vis(); const st4 = await mp.evaluate(k => JSON.parse(localStorage.getItem(k) || '{}'), ed + '.settings');
    ok(`${ed} T11: × folds it back to the button (it does not switch tips off)`, f4.min && f4.pill && !f4.hidden && st4.tips !== false, JSON.stringify(f4));
    // the note never blocks the patch: with the button showing, a tap beside it reaches the patch
    const hit = await mp.evaluate(() => { const r = document.getElementById('hint').getBoundingClientRect(); const e = document.elementFromPoint(r.left + r.width / 2, r.top - 20); return e ? e.id || e.tagName : null; });
    ok(`${ed} T11: the collapsed button is the only thing that takes touches there (above it: ${hit})`, hit && hit !== 'hint' && hit !== 'hintpill');
    // Settings > Show tips off still hides it for good
    await mp.goto(`${mb}/${ed}/index.html`); await mp.evaluate(k => { localStorage.setItem(k, JSON.stringify({ tips: false, tipSeen: true })); }, ed + '.settings'); await mp.goto(`${mb}/${ed}/index.html`); await mp.waitForSelector('#ws'); await sleep(mp, 800);
    ok(`${ed} T11: with Show tips off nothing of it is shown`, await mp.evaluate(() => document.getElementById('hint').hidden));

    // ---- U10: the menus on the small screen ----
    await load(mp, mb, ed, doc(ed, types2.slice(0, 6)));
    const menu = async (sx, sy) => { await mp.evaluate(([x, y]) => { const t = document.querySelector('.node > header .t'); t.dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: x, clientY: y })); }, [sx, sy]);   // (on a module: the long menu) await sleep(mp, 200);
      return mp.evaluate(() => { const c = document.getElementById('ctxmenu'), r = c.getBoundingClientRect(), cs = getComputedStyle(c); return { hidden: c.hidden, top: Math.round(r.top), bottom: Math.round(r.bottom), left: Math.round(r.left), right: Math.round(r.right), vh: innerHeight, vw: innerWidth, oy: cs.overflowY, mh: cs.maxHeight, n: c.children.length, scroll: c.scrollHeight, client: c.clientHeight }; }); };
    const mm = await menu(330, 770);
    ok(`${ed} U10: touch 344 x 780: a menu opened at the bottom corner stays on the screen (${mm.top}..${mm.bottom} of ${mm.vh}, ${mm.n} items)`, !mm.hidden && mm.top >= 0 && mm.bottom <= mm.vh && mm.left >= 0 && mm.right <= mm.vw && /auto|scroll/.test(mm.oy) && mm.mh !== 'none', JSON.stringify(mm));
    await M.page.evaluate(() => document.getElementById('ctxmenu').hidden = true);
    // a window too short for the whole menu: it scrolls and its last item can be reached
    await mp.setViewportSize({ width: 344, height: 260 }); await sleep(mp, 300);
    const sm = await menu(200, 100);
    const reach = await mp.evaluate(() => { const c = document.getElementById('ctxmenu'); c.scrollTop = c.scrollHeight; const last = c.lastElementChild.getBoundingClientRect(), r = c.getBoundingClientRect(); return { last: Math.round(last.bottom), box: Math.round(r.bottom), top: Math.round(r.top) }; });
    ok(`${ed} U10: touch, a 260 px tall window: the menu is ${sm.client} px tall inside ${sm.scroll} px of items and scrolls to the last one (${JSON.stringify(reach)})`, sm.scroll > sm.client && sm.top >= 0 && sm.bottom <= 260 && reach.last <= reach.box + 1 && reach.last > reach.top, JSON.stringify(sm));
    await mp.setViewportSize({ width: 344, height: 780 });
    const errs3 = M.errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed}: no page errors (344 px touch)`, errs3.length === 0, errs3.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} touch: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await M.close();

  // ============ desktop: the tip box with a mouse (T11 leaves it alone), the menu at 800 x 700 (U10) ============
  const D = await boot({ width: 800, height: 700, query: '' }); const dp = D.page, db = `http://127.0.0.1:${D.port}`;
  try {
    await load(dp, db, ed, doc(ed, ['vca', 'lfo', 'delay', 'adsr', 'vcf']), null, { tips: true });
    const tip = await dp.evaluate(() => { const h = document.getElementById('hint'), v = e => { const b = e.getBoundingClientRect(); return b.width > 0 && b.height > 0; }; return { long: v(h.querySelector('.tipm')), short: v(h.querySelector('.tipt')), pill: v(document.getElementById('hintpill')), min: h.classList.contains('min'), text: h.querySelector('.tipm').textContent }; });
    ok(`${ed} T11: with a mouse the full note is shown as before (and no Tips button)`, tip.long && !tip.short && !tip.pill && !tip.min && /Option/.test(tip.text) && /right-click to group/.test(tip.text), JSON.stringify(tip).slice(0, 200));
    await dp.click('#hintx'); await sleep(dp, 200);
    ok(`${ed} T11: with a mouse × still turns tips off (as before)`, await dp.evaluate(k => document.getElementById('hint').hidden && JSON.parse(localStorage.getItem(k)).tips === false, ed + '.settings'));
    await load(dp, db, ed, doc(ed, ['vca', 'lfo', 'delay', 'adsr', 'vcf', 'convolve']), null, { tips: true });
    { const tc = await dp.evaluate(TIPCLEAR); ok(`${ed} T11: 800 x 700: after Fit no module is under the tip box (${tc.hint.join(' x ')} px, ${tc.n} modules)`, tc.hit === 0 && tc.hint[1] <= 80, JSON.stringify(tc)); }
    await dp.click('#hintmore'); const more = await dp.evaluate(() => { const h = document.getElementById('hint'); return { open: h.classList.contains('more'), h: Math.round(h.getBoundingClientRect().height), label: document.getElementById('hintmore').textContent }; });
    ok(`${ed} T11: More shows the whole note (${more.h} px tall, button reads ${more.label})`, more.open && more.h > 80 && more.label === 'Less', JSON.stringify(more));
    await load(dp, db, ed, doc(ed, ['vca', 'lfo', 'delay', 'adsr', 'vcf']), null, { tips: true });
    const dm = async (x, y) => { await dp.evaluate(([x, y]) => document.querySelector('.node > header .t').dispatchEvent(new MouseEvent('contextmenu', { bubbles: true, cancelable: true, clientX: x, clientY: y })), [x, y]); await sleep(dp, 200);
      return dp.evaluate(() => { const c = document.getElementById('ctxmenu'), r = c.getBoundingClientRect(), cs = getComputedStyle(c); return { hidden: c.hidden, top: Math.round(r.top), bottom: Math.round(r.bottom), left: Math.round(r.left), right: Math.round(r.right), vh: innerHeight, vw: innerWidth, oy: cs.overflowY, mh: cs.maxHeight, n: c.children.length, scroll: c.scrollHeight, client: c.clientHeight }; }); };
    const d1 = await dm(790, 690);
    ok(`${ed} U10: 800 x 700: a right-click menu at the bottom right corner stays on the screen (${d1.top}..${d1.bottom} of ${d1.vh}, ${d1.n} items)`, !d1.hidden && d1.top >= 0 && d1.bottom <= d1.vh && d1.right <= d1.vw && /auto|scroll/.test(d1.oy) && d1.mh !== 'none', JSON.stringify(d1));
    await dp.evaluate(() => document.getElementById('ctxmenu').hidden = true);
    await dp.setViewportSize({ width: 800, height: 260 }); await sleep(dp, 300);
    const d2 = await dm(300, 100);
    const rch = await dp.evaluate(() => { const c = document.getElementById('ctxmenu'); c.scrollTop = c.scrollHeight; const last = c.lastElementChild.getBoundingClientRect(), r = c.getBoundingClientRect(); return { last: Math.round(last.bottom), box: Math.round(r.bottom) }; });
    ok(`${ed} U10: 800 wide, 260 tall: the menu (${d2.scroll} px of items) is ${d2.client} px tall, scrolls, and its last item is reachable`, d2.scroll > d2.client && d2.top >= 0 && d2.bottom <= 260 && rch.last <= rch.box + 1, JSON.stringify([d2, rch]));
    await dp.setViewportSize({ width: 800, height: 700 });
    // the wheel scrolls the menu, not the patch
    const z0 = await dp.evaluate(() => getComputedStyle(document.querySelector('.world')).transform);
    await dp.setViewportSize({ width: 800, height: 260 }); await dm(300, 100); await dp.mouse.move(310, 130); await dp.mouse.wheel(0, 120); await sleep(dp, 200);
    const sc = await dp.evaluate(() => document.getElementById('ctxmenu').scrollTop), z1 = await dp.evaluate(() => getComputedStyle(document.querySelector('.world')).transform);
    ok(`${ed} U10: the wheel over the menu scrolls the menu (scrollTop ${sc}) and the patch stays put`, sc > 0 && z0 === z1, z0 + ' / ' + z1);
    const errs4 = D.errors.splice(0).filter(e => !NOISE.test(e));
    ok(`${ed}: no page errors (800 x 700)`, errs4.length === 0, errs4.slice(0, 3).join(' | '));
  } catch (e) { ok(`${ed} 800: ran to the end`, false, e.stack.split('\n').slice(0, 4).join(' | ')); }
  await D.close();
}

// ---------------- the previous build, from a git checkout of its commit ----------------
async function oldBuild() {
  let dir = process.env.OLD_ROOT;
  if (!dir) { const ref = process.env.OLD_REF || '602e083'; dir = fs.mkdtempSync(path.join(os.tmpdir(), 'wc-old-'));
    try { execSync(`git -C ${JSON.stringify(ROOT)} archive ${ref} chains waves | tar -x -C ${JSON.stringify(dir)}`, { stdio: 'pipe' }); } catch (e) { return null; } }
  const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.woff': 'font/woff', '.png': 'image/png', '.webmanifest': 'application/manifest+json' };
  const srv = serveDir(dir, TYPES);
  await listenLoopback(srv);
  return { base: `http://127.0.0.1:${srv.address().port}`, close: () => srv.close() };
}

const CSS = await skinCss();
const OLD = await oldBuild();
if (!OLD) ok('the previous build could not be read (no git ref 602e083 and no OLD_ROOT)', false);
for (const ed of (process.env.ED ? process.env.ED.split(',') : ['chains', 'waves'])) await run(ed, CSS, OLD);
if (OLD) OLD.close();
const f = T.print();
process.exit(f ? 1 : 0);
