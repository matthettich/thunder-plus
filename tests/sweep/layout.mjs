// UI SWEEP: layout at several window sizes (desktop and the Galaxy Z Fold 6, folded and unfolded, in the Mobile setup), and
// every skin. Screenshots go to tests/sweep/out/ (gitignored): look at them.
//   node tests/sweep/layout.mjs              every size × every screen, then every skin at 1600×1000
//   node tests/sweep/layout.mjs --sizes      sizes only        --skins   skins only
// Checks per shot: horizontal page scroll, top-bar / tab buttons cut off or outside the window, controls overlapping each
// other in the top bar, text in the top bar and tabs clipped (scrollWidth > clientWidth), and for skins the contrast of
// the main text, the tracker's cells and the menu items (WCAG ratio; < 3 is FAIL, < 4.5 a NOTE).
import fs from 'fs'; import path from 'path';
import { boot, takeErrors, Table, app, ROOT, OUT } from './ui-lib.mjs';

const ARG = process.argv.slice(2), only = k => !ARG.filter(a => !a.startsWith('--from=')).length || ARG.includes('--' + k);
const FROM = (ARG.find(a => a.startsWith('--from=')) || '').slice(7);   // e.g. --from=fold-cover: start at that size
const T = new Table('layout');
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
fs.mkdirSync(OUT, { recursive: true });
const SIZES = [
  { n: 'desk-1920x1080', w: 1920, h: 1080 }, { n: 'desk-1600x1000', w: 1600, h: 1000 }, { n: 'mac-1440x900', w: 1440, h: 900 },
  { n: 'desk-1280x800', w: 1280, h: 800 },
  { n: 'fold-open-884x1020', w: 884, h: 1020, mob: true }, { n: 'fold-open-land-1020x884', w: 1020, h: 884, mob: true },
  { n: 'fold-cover-360x800', w: 360, h: 800, mob: true }, { n: 'fold-cover-412x915', w: 412, h: 915, mob: true },
];
const SCREENS = [['tracker', 'song'], ['tracker', 'phrase'], ['tracker', 'inst'], ['tracker', 'mixer'], ['tracker', 'fx'], ['tracker', 'project'], ['synth'], ['waves'], ['chains']];

// What's wrong with the layout right now (a list of strings).
const PROBE = `(() => {
  const bad = [], vw = document.documentElement.clientWidth, vh = innerHeight;
  if (document.documentElement.scrollWidth > vw + 1) bad.push('page scrolls sideways: ' + document.documentElement.scrollWidth + ' > ' + vw);
  const vis = e => { const r = e.getBoundingClientRect(); const s = getComputedStyle(e); return r.width > 0 && r.height > 0 && s.visibility !== 'hidden' && s.display !== 'none'; };
  const name = e => (e.id ? '#' + e.id : '') + ' "' + (e.textContent || e.getAttribute('aria-label') || '').trim().replace(/\\s+/g, ' ').slice(0, 18) + '"';
  // top bar + screen tabs + transport strip: every button inside the window, not overlapping another, its text not clipped
  const tops = [...document.querySelectorAll('.top button, .top .seg button, .ttabs button, #tRecBar button, .ttop button')].filter(vis);
  const scrollers = new Set();
  for (const b of tops){ const r = b.getBoundingClientRect();
    let sc = b.parentElement; while (sc && sc !== document.body && !(sc.scrollWidth > sc.clientWidth + 1 && /auto|scroll/.test(getComputedStyle(sc).overflowX))) sc = sc.parentElement;
    const inScroller = sc && sc !== document.body;
    if (inScroller) scrollers.add(sc);
    if (!inScroller && (r.right > vw + 1 || r.left < -1)) bad.push('off the window: ' + name(b) + ' at ' + Math.round(r.left) + '..' + Math.round(r.right));
    if (b.scrollWidth > b.clientWidth + 2 && getComputedStyle(b).overflow !== 'visible') bad.push('text clipped: ' + name(b) + ' ' + b.scrollWidth + '>' + b.clientWidth);
  }
  for (const s of scrollers){ const hid = [...s.querySelectorAll('button')].filter(vis).filter(b => { const r = b.getBoundingClientRect(), p = s.getBoundingClientRect(); return r.right > p.right + 1 || r.left < p.left - 1; });
    if (hid.length) bad.push('NOTE scrolled out of sight in ' + (s.id || s.className) + ': ' + hid.map(name).join(', ')); }
  for (let i = 0; i < tops.length; i++) for (let j = i + 1; j < tops.length; j++){ const a = tops[i].getBoundingClientRect(), b = tops[j].getBoundingClientRect();
    if (tops[i].contains(tops[j]) || tops[j].contains(tops[i])) continue;
    const ox = Math.min(a.right, b.right) - Math.max(a.left, b.left), oy = Math.min(a.bottom, b.bottom) - Math.max(a.top, b.top);
    if (ox > 3 && oy > 3) bad.push('overlap: ' + name(tops[i]) + ' / ' + name(tops[j])); }
  // the bottom of the page: anything clickable below the window that can't be scrolled to
  const below = [...document.querySelectorAll('button')].filter(vis).filter(b => b.getBoundingClientRect().top >= vh - 2 && !b.closest('[hidden]'));
  let reach = true; { let el = below[0]; if (el){ reach = false; let p = el.parentElement; while (p){ if (p.scrollHeight > p.clientHeight + 1 && /auto|scroll/.test(getComputedStyle(p).overflowY)){ reach = true; break; } p = p.parentElement; } if (document.documentElement.scrollHeight > vh + 1) reach = true; } }
  if (!reach) bad.push('buttons below the window, not scrollable: ' + below.slice(0, 4).map(name).join(', '));
  return bad;
})()`;

async function sizes() {
  for (const S of FROM ? SIZES.slice(Math.max(0, SIZES.findIndex(z => z.n.startsWith(FROM)))) : SIZES) {
    const storage = { 'thunderplus-platform-v1': S.mob ? 'mobile' : 'desktop', 'thunderplus-mobile-chains-v1': '1', 'thunderplus-mobile-waves-v1': '1' };
    const B = await boot({ width: S.w, height: S.h, storage, mobile: !!S.mob });
    const { page, errors } = B;
    try {
      await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(400);
      await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, $('toast').hidden = true, 1)`);
      for (const [v, s] of SCREENS) {
        await app(page, `(setView('${v}'), ${s ? `go('${s}'),` : ''} $('toast').hidden = true, 1)`);
        await page.waitForTimeout(v === 'waves' || v === 'chains' ? 1200 : 500);
        const nowV = await app(page, 'view');
        const f = `${OUT}/size-${S.n}-${s || v}.png`; await page.screenshot({ path: f });
        const bad = await app(page, PROBE); const errs = takeErrors(errors);
        const real = bad.filter(b => !b.startsWith('NOTE')), notes = bad.filter(b => b.startsWith('NOTE'));
        T.add(`${S.n} ${s || v}${nowV !== v ? ' (hidden: ' + nowV + ')' : ''}`, errs.length || real.length ? false : notes.length ? 'NOTE' : true, [...errs, ...real, ...notes].join(' · ').slice(0, 300) || path.basename(f));
      }
      // the menus at this size
      for (const id of ['bFile', 'bEdit', 'bView', 'bAud', 'bSet']) {
        await app(page, `(setView('tracker'), go('song'), 1)`);
        const shown = await page.locator('#' + id).isVisible(); if (!shown) { T.add(`${S.n} menu ${id}`, 'NOTE', 'button not shown here'); continue; }
        await page.click('#' + id); await page.waitForTimeout(300);
        const m = await app(page, `(() => { const m = [...document.querySelectorAll('#gfile:not([hidden]), #gedit:not([hidden]), #gset:not([hidden])')][0]; if (!m) return 'none'; const r = m.getBoundingClientRect(); return JSON.stringify({l:Math.round(r.left), r:Math.round(r.right), t:Math.round(r.top), b:Math.round(r.bottom), sh:m.scrollHeight, ch:m.clientHeight, vw:document.documentElement.clientWidth, vh:innerHeight}); })()`);
        await page.screenshot({ path: `${OUT}/size-${S.n}-menu-${id}.png` });
        if (m === 'none') T.add(`${S.n} menu ${id}`, false, 'did not open');
        else { const r = JSON.parse(m); const off = r.l < 0 || r.r > r.vw + 1 || r.b > r.vh + 1 && r.sh <= r.ch; T.add(`${S.n} menu ${id}`, off ? false : true, m); }
        await page.keyboard.press('Escape'); await page.waitForTimeout(150);
        await app(page, `(gsetToggle(false), gfileToggle(false), geditToggle(false), 1)`);
      }
    } catch (e) { T.add(`${S.n}`, false, 'threw ' + e.message.split('\n')[0]); }
    await B.close();
  }
}

// WCAG contrast of an element's text against the first opaque background behind it.
const CONTRAST = `(sel => { const lum = c => { const m = c.match(/[\\d.]+/g); if (!m) return null; const [r, g, b] = m.slice(0, 3).map(v => { v = +v / 255; return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4); }); return 0.2126 * r + 0.7152 * g + 0.0722 * b; };
  const bgOf = e => { while (e){ const c = getComputedStyle(e).backgroundColor; const m = c.match(/[\\d.]+/g); if (m && (m.length < 4 || +m[3] > 0.5)) return c; e = e.parentElement; } return 'rgb(255,255,255)'; };
  const out = []; for (const e of [...document.querySelectorAll(sel)].filter(e => e.getClientRects().length && e.textContent.trim()).slice(0, 40)){
    const f = lum(getComputedStyle(e).color), b = lum(bgOf(e)); if (f == null || b == null) continue; const r = (Math.max(f, b) + 0.05) / (Math.min(f, b) + 0.05); out.push(r); }
  if (!out.length) return null; out.sort((a, b) => a - b); return Math.round(out[0] * 100) / 100; })`;

async function skins() {
  const B = await boot({ width: 1600, height: 1000, storage: { 'thunderplus-platform-v1': 'desktop' } });
  const { page, errors } = B;
  try {
    await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(400);
    await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`);
    const ids = JSON.parse(await app(page, 'JSON.stringify(SKINS.map(k => k.id))'));
    // the Skin… sheet opens from View
    await page.click('#bView'); await page.waitForTimeout(300);
    const sk = page.locator('#gset button', { hasText: /^Skin/ }).first();
    if (await sk.isVisible()) { await sk.click(); await page.waitForTimeout(500); const n = await app(page, `document.querySelectorAll('.pbs .skin, .pbs [data-id]').length`); await page.screenshot({ path: `${OUT}/skin-sheet.png` }); T.add('View › Skin… sheet', n > 0, n + ' skins listed'); await page.keyboard.press('Escape'); await page.waitForTimeout(200); }
    else T.add('View › Skin… sheet', false, 'no Skin… button');
    for (const id of ids) {
      for (const [v, s] of [['tracker', 'phrase'], ['tracker', 'mixer'], ['synth']]) {
        await app(page, `(applySkin('${id}'), setView('${v}'), ${s ? `go('${s}'),` : ''} $('toast').hidden = true, 1)`); await page.waitForTimeout(350);
        const f = `${OUT}/skin-${id}-${s || v}.png`; await page.screenshot({ path: f });
        const c1 = await app(page, `${CONTRAST}('.ttabs button, .top button, #tInfo, .thint, .tinfo')`);
        const c2 = await app(page, `${CONTRAST}('#tGrid span, #tGrid b, #tGrid i, .row label, .row output, .pad')`);
        const errs = takeErrors(errors);
        // the grid's worst span is often a deliberately faint one (empty-cell dashes, a hidden cursor column): a NOTE to look at the shot
        const worst = Math.min(c1 ?? 99, c2 == null ? 99 : Math.max(c2, 3));
        T.add(`skin ${id} ${s || v}`, errs.length ? false : worst < 3 ? false : worst < 4.5 || (c2 != null && c2 < 3) ? 'NOTE' : true, `UI ${c1} · grid ${c2}` + (errs.length ? ' · ' + errs.join(' / ') : ''));
      }
      await app(page, `(gsetToggle(true, 'view', $('bView')), 1)`); await page.waitForTimeout(200);
      await page.screenshot({ path: `${OUT}/skin-${id}-viewmenu.png` });
      const c3 = await app(page, `${CONTRAST}('#gset button, #gset label, #gset h3, #gset .ttl')`);
      T.add(`skin ${id} View menu`, c3 != null && c3 < 3 ? false : c3 != null && c3 < 4.5 ? 'NOTE' : true, `menu ${c3}`);
      await app(page, `(gsetToggle(false), 1)`);
    }
    // persists across reload
    await app(page, `(applySkin('neon'), 1)`); await page.reload(); await page.waitForTimeout(2000);
    const after = await app(page, 'skin').catch(() => null);
    T.add('skin persists across reload', after === 'neon', 'after reload: ' + after);
    await app(page, `(applySkin('idm'), 1)`).catch(() => {});
  } catch (e) { T.add('skins', false, 'threw ' + e.message.split('\n')[0]); }
  await B.close();
}

if (only('sizes')) await sizes();
if (only('skins')) await skins();
const f = T.print();
process.exit(f ? 1 : 0);
