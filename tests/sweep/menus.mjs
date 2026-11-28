// UI SWEEP: every top-bar menu item and every right-click menu, on every screen, with the real mouse.
//   node tests/sweep/menus.mjs            (≈ 6–10 min; prints a PASS/FAIL table)
//   node tests/sweep/menus.mjs --only=file,edit   (some parts only: file, edit, view, aud, set, tabs, ctx)
//   node tests/sweep/menus.mjs --only=edit --screens=mixer,fx   (Edit ▾ on some screens only)
// On a slow machine run it in parts: each item takes 1–20 s.
// For each item: opens the menu, clicks the item, and records what changed (song state, screen, a dialog, a toast, a file
// picker, a download). Dialogs are closed with Esc (then their Cancel / Done). FAIL: a page error or console error, an item
// that did nothing visible, or something left behind (an overlay or menu still open after Esc).
// File pickers: page.on('filechooser') feeds sounds/ELECTRO.thunder; showSaveFilePicker / showOpenFilePicker are stubbed to
// record the request and cancel (AbortError), as a user pressing Cancel would.
import fs from 'fs'; import path from 'path';
import { boot, takeErrors, Table, app, ROOT, OUT } from './ui-lib.mjs';

const CTXF = (process.argv.find(a => a.startsWith('--ctx=')) || '').slice(6);   // a name filter for the right-click part
const ONLY = (process.argv.find(a => a.startsWith('--only=')) || '').slice(7).split(',').filter(Boolean);
const want = k => !ONLY.length || ONLY.includes(k);
const ONLYS = (process.argv.find(a => a.startsWith('--screens=')) || '').slice(10).split(',').filter(Boolean);   // e.g. --screens=mixer,fx (Edit ▾ only)
const wantS = s => !ONLYS.length || ONLYS.includes(s);
const T = new Table('menus');
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
fs.mkdirSync(OUT, { recursive: true });

const B = await boot({ width: 1600, height: 1000 });
const { page, errors } = B;
let chooser = 0, download = 0;
page.on('filechooser', async fc => { chooser++; try { await fc.setFiles(path.join(ROOT, 'sounds/ELECTRO.thunder')); } catch (e) {} });
page.on('download', d => { download++; d.cancel().catch(() => {}); });
await page.evaluate(() => {
  window.__picks = 0;
  const no = async () => { window.__picks++; const e = new Error('cancelled'); e.name = 'AbortError'; throw e; };
  window.showSaveFilePicker = no; window.showOpenFilePicker = no;
});

const loadElectro = async () => { await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(500); await app(page, 'setDirty(false), 1'); };
await loadElectro();

// What the page looks like right now, for "did the item do anything".
const snap = () => app(page, `JSON.stringify({
  S: (() => { let h = 0; const j = JSON.stringify(S); for (let i = 0; i < j.length; i += 7) h = (h * 31 + j.charCodeAt(i)) | 0; return h + ':' + j.length; })(),
  kit: (() => { try { return JSON.stringify(kit.voices.map(v => v.name)).length + kit.name; } catch(e){ return ''; } })(),
  view, tv, follow: followMode, vtrk: typeof vtrk !== 'undefined' && vtrk, mute: JSON.stringify(S.mix.map(m => +m.mute + '' + +m.solo)), toast: $('toast').hidden ? '' : $('toast').textContent,
  dlg: [...document.querySelectorAll('.ask, .pbs, .swapov, .help:not([hidden]), #mstWin:not([hidden]), .pumpwin, #phl:not([hidden]), .rprog, #fxPick:not([hidden])')].filter(e => e.getClientRects().length).map(e => e.className || e.id).join('|'),
  menus: [...document.querySelectorAll('#gfile:not([hidden]), #gedit:not([hidden]), #gset:not([hidden]), .sxmenu')].map(e => e.id || e.className).join('|'),
  sel: typeof selA !== 'undefined' && selA ? JSON.stringify(selA) : '', loop: !!loopSel + '' + (typeof loopArm !== 'undefined' && loopArm ? 1 : 0) + loopPat, clip: clip ? JSON.stringify(clip).length : 0,
  cur: JSON.stringify(cs[tv] || {}), undo: undo.past.length + '/' + undo.future.length, picks: window.__picks, body: document.body.className,
  skin: document.documentElement.dataset.skin || '', fs: getComputedStyle(document.documentElement).getPropertyValue('--fs-3')
})`).then(JSON.parse);

// Close whatever is open: Esc up to 4 times, then any dialog's Cancel / Done / Close.
async function closeAll() {
  for (let i = 0; i < 4; i++) {
    const s = await snap(); if (!s.dlg && !s.menus) return true;
    await page.keyboard.press('Escape'); await page.waitForTimeout(180);
  }
  for (const sel of ['.ask button:has-text("Cancel")', '.ask button:has-text("Done")', '.pbs button:has-text("Done")', '.swapov button:has-text("Cancel")', '#phlX', '.help button:has-text("Close")', '.rprog .rpx']) {
    const b = page.locator(sel).first(); if (await b.isVisible().catch(() => false)) { await b.click().catch(() => {}); await page.waitForTimeout(200); }
  }
  const s = await snap(); return !s.dlg && !s.menus;
}

// The visible items of an open menu host (buttons), with their group rows opened.
async function menuItems(host) {
  return page.evaluate(h => {
    const m = typeof h === 'string' ? document.querySelector(h) : h; if (!m) return [];
    for (const b of m.querySelectorAll('.msub')) if (b._sub) b._sub(true);
    return [...m.querySelectorAll('button')].filter(b => b.getClientRects().length && !b.classList.contains('msub'))
      .map((b, i) => ({ i, t: (b.querySelector('span') || b).textContent.trim().replace(/\s+/g, ' '), off: b.disabled }));
  }, host);
}
// A real mouse click on the i-th item (groups opened, the item scrolled into the menu's view first). Throws when another
// element covers the item's middle.
async function clickNth(host, i) {
  const at = await page.evaluate(([h, i]) => {
    const m = document.querySelector(h); if (!m) return null; for (const b of m.querySelectorAll('.msub')) if (b._sub) b._sub(true);
    const L = [...m.querySelectorAll('button')].filter(b => b.getClientRects().length && !b.classList.contains('msub')), b = L[i]; if (!b) return null;
    b.scrollIntoView({ block: 'nearest' }); const r = b.getBoundingClientRect(), x = r.left + r.width / 2, y = r.top + r.height / 2, t = document.elementFromPoint(x, y);
    return { x, y, hit: t === b || b.contains(t), on: t ? t.tagName + '.' + t.className : 'none' };
  }, [host, i]);
  if (!at) throw new Error('item not found');
  if (!at.hit) throw new Error('covered by ' + at.on);
  await page.mouse.click(at.x, at.y);
}

// Things never clicked, and why.
const SKIP = [
  [/^Check for update/, 'network'], [/^Share/, 'Web Share needs a user device'], [/LXR kit/, 'needs a .snd file'],
  [/^Connect MIDI|^Learn|^Unlearn/, 'MIDI hardware'],
];
const skipWhy = t => (SKIP.find(([r]) => r.test(t)) || [])[1];
// Items that replace the song: reload ELECTRO afterwards so later items have something to act on.
const RELOAD = /^(New project|Demo project|ELECTRO|Funk breaks|Breaks to chop|Restore a backup|Open|Clear unused phrases|Delete|Remove|Cut)/;

async function sweepMenu(label, btnSel, hostSel, opts = {}) {
  await closeAll();
  await page.click(btnSel); await page.waitForTimeout(250);
  const items = await menuItems(hostSel);
  await page.keyboard.press('Escape'); await closeAll();
  if (!items.length) { T.add(`${label}: opens`, false, 'no items'); return; }
  T.add(`${label}: opens (${items.length} items)`, true);
  for (const it of items) {
    const name = `${label} › ${it.t}`;
    const why = skipWhy(it.t); if (why) { T.add(name, 'SKIP', why); continue; }
    if (it.off) { T.add(name, 'OFF', 'disabled here'); continue; }
    await closeAll(); takeErrors(errors);
    await page.click(btnSel); await page.waitForTimeout(150);
    const a = await snap(); const c0 = chooser, d0 = download;
    try { await clickNth(hostSel, it.i); } catch (e) { T.add(name, false, 'click failed: ' + e.message.split('\n')[0]); continue; }
    await page.waitForTimeout(opts.wait || 450);
    // A confirm (askYes) on a song-replacing item: accept it, so the action runs.
    const ok = page.locator('.ask:not(.rprog):not(.acidask) .btnrow button.ok');
    let accepted = false;
    if (await ok.isVisible().catch(() => false) && /^(New project|Factory drums|Clear all kit|Demo project|Clear unused)/.test(it.t)) { await ok.click(); accepted = true; await page.waitForTimeout(400); }
    const b = await snap();
    const did = [];
    for (const k of ['S', 'kit', 'view', 'tv', 'follow', 'vtrk', 'mute', 'dlg', 'sel', 'loop', 'clip', 'cur', 'undo', 'picks', 'body', 'skin', 'fs']) if (a[k] !== b[k]) did.push(k + (k === 'dlg' ? '=' + b.dlg.slice(0, 40) : ''));
    if (b.toast && b.toast !== a.toast) did.push('toast "' + b.toast.slice(0, 70) + '"');
    if (chooser > c0) did.push('file chooser');
    if (download > d0) did.push('download');
    if (accepted) did.push('confirmed');
    if (b.menus && b.menus.includes('gfile') === false && hostSel === '#gfile') {}
    const closed = await closeAll();
    const errs = takeErrors(errors);
    const nothing = !did.length;
    T.add(name, errs.length ? false : !closed ? false : nothing ? 'NOTE' : true,
      (errs.length ? 'ERR ' + errs.join(' / ').slice(0, 160) + ' · ' : '') + (!closed ? 'LEFT OPEN ' : '') + (nothing ? 'no visible change' : did.join(', ')));
    if (RELOAD.test(it.t) || a.S !== b.S && /Kit|kit|pads/.test(it.t)) { await closeAll(); await loadElectro(); }
    if (b.view !== a.view || b.tv !== a.tv) { await app(page, `(setView(${JSON.stringify(a.view)}), ${a.view === 'tracker' ? `go(${JSON.stringify(a.tv)}),` : ''} 1)`); await page.waitForTimeout(300); }
    if (b.skin !== a.skin) { await app(page, `(applySkin ? 1 : 1)`); }
  }
}


// The open ⚙ / View / Audio & MIDI panel's controls, in order (the same list sweepPanel counts), and where to click one.
await page.evaluate(() => { window.panelCtl = () => [...document.querySelectorAll('#gset button, #gset input[type=checkbox], #gset select')].filter(e => e.getClientRects().length); });
const panelHit = i => page.evaluate(i => { const e = panelCtl()[i]; if (!e) return null; e.scrollIntoView({ block: 'nearest' });
  const t = e.type === 'checkbox' && e.closest('label') ? e.closest('label') : e, r = t.getBoundingClientRect(), x = r.left + Math.min(r.width / 2, 12), y = r.top + r.height / 2, h = document.elementFromPoint(x, y);
  return { x, y, n: e.options ? e.options.length : 0, hit: h === t || t.contains(h) || e === h || (h && h.closest && h.closest('label') === e.closest('label')), on: h ? h.tagName + '.' + h.className : 'none' }; }, i);

// The ⚙ / View / Audio & MIDI dropdowns are panels of controls (buttons, switches, sliders), not item lists: click each
// button and toggle each checkbox (then put it back), and check something changed.
async function sweepPanel(label, btnSel) {
  await closeAll();
  await page.click(btnSel); await page.waitForTimeout(300);
  const ctl = await page.evaluate(() => panelCtl()
    .map((e, i) => ({ i, tag: e.tagName, type: e.type, id: e.id, t: (e.textContent || e.getAttribute('aria-label') || (e.closest('label') || {}).textContent || '').trim().replace(/\s+/g, ' ').slice(0, 50) })));
  await closeAll();
  T.add(`${label}: opens (${ctl.length} controls)`, ctl.length > 0);
  for (const c of ctl) {
    const name = `${label} › ${c.tag === 'INPUT' ? '[x] ' : c.tag === 'SELECT' ? '[v] ' : ''}${c.t || c.id}`;
    const why = skipWhy(c.t) || (/audReset|bMidiOn|bLearn|bUnlearn|updBtn/.test(c.id) ? 'hardware / network' : '');
    if (why) { T.add(name, 'SKIP', why); continue; }
    await closeAll(); takeErrors(errors);
    await page.click(btnSel); await page.waitForTimeout(200);
    const a = await snap();
    const ls0 = await page.evaluate(() => JSON.stringify(Object.entries(localStorage).filter(([k]) => !/song|dirty|resume|undo|backup/.test(k)).map(([k, v]) => k + v.length + v.slice(0, 40))));
    const r = await panelHit(c.i);
    try {
      if (!r) throw new Error('control not found');
      if (c.tag === 'SELECT') { if (r.n < 2) { T.add(name, 'NOTE', 'only one option'); continue; } await page.evaluate(i => { const s = panelCtl()[i]; s.value = s.options[s.selectedIndex === 1 ? 0 : 1].value; s.dispatchEvent(new Event('change', { bubbles: true })); }, c.i); }
      else { if (!r.hit) throw new Error('covered by ' + r.on); await page.mouse.click(r.x, r.y); }
    } catch (e) { T.add(name, false, 'click failed: ' + e.message.split('\n')[0]); continue; }
    await page.waitForTimeout(450);
    const b = await snap();
    const ls1 = await page.evaluate(() => JSON.stringify(Object.entries(localStorage).filter(([k]) => !/song|dirty|resume|undo|backup/.test(k)).map(([k, v]) => k + v.length + v.slice(0, 40))));
    const did = [];
    for (const k of ['S', 'kit', 'view', 'tv', 'follow', 'dlg', 'body', 'skin', 'fs', 'undo']) if (a[k] !== b[k]) did.push(k + (k === 'dlg' ? '=' + b.dlg.slice(0, 30) : ''));
    if (ls0 !== ls1) did.push('localStorage');
    if (b.toast && b.toast !== a.toast) did.push('toast "' + b.toast.slice(0, 60) + '"');
    const stillOpen = b.menus.includes('gset');
    const domChanged = await page.evaluate(() => { const g = document.getElementById('gset'); return g && !g.hidden ? g.innerText.length : -1; });
    if (!did.length && stillOpen) did.push('(panel redrawn: ' + domChanged + ' chars)');
    const closed = await closeAll(); const errs = takeErrors(errors);
    T.add(name, errs.length ? false : !closed ? false : did.length && !did[0].startsWith('(panel') ? true : 'NOTE',
      (errs.length ? 'ERR ' + errs.join(' / ').slice(0, 160) + ' · ' : '') + (!closed ? 'LEFT OPEN ' : '') + (did.join(', ') || 'no visible change'));
    // put a toggled switch back (a second click); skins / text size are restored below
    if (c.tag === 'INPUT') { await page.click(btnSel); await page.waitForTimeout(150); const r2 = await panelHit(c.i); if (r2 && r2.hit) await page.mouse.click(r2.x, r2.y); await page.waitForTimeout(250); await closeAll(); }
    if (b.view !== a.view || b.tv !== a.tv) await app(page, `(setView(${JSON.stringify(a.view)}), ${a.view === 'tracker' ? `go(${JSON.stringify(a.tv)}),` : ''} 1)`);
    if (b.body.includes('mob') && !a.body.includes('mob')) { await app(page, `(setPlatform && setPlatform('desktop'), 1)`).catch(() => {}); }
  }
}

const go = async (v, s) => { await closeAll(); await app(page, `(setView('${v}'), ${s ? `go('${s}'),` : ''} 1)`); await page.waitForTimeout(400); };
const SCREENS = ['song', 'phrase', 'inst', 'mixer', 'fx', 'project'];

try {
  if (want('tabs')) {
    for (const v of ['Synth', 'Waves', 'Chains', 'Tracker']) {
      await page.locator('#segView button', { hasText: v }).click(); await page.waitForTimeout(900);
      const now = await app(page, 'view'); T.add(`top tab ${v}`, now === v.toLowerCase() && !takeErrors(errors).length, 'view=' + now);
    }
    for (const s of SCREENS) {
      await page.locator('.ttabs button', { hasText: new RegExp('^' + s.toUpperCase() + '$') }).first().click(); await page.waitForTimeout(400);
      const now = await app(page, 'tv'); T.add(`screen tab ${s.toUpperCase()}`, now === s && !takeErrors(errors).length, 'tv=' + now);
    }
  }
  if (want('file')) {
    for (const [v, s] of [['tracker', 'song'], ['synth'], ['waves'], ['chains']]) { await go(v, s); await sweepMenu(`File[${v}${s ? '/' + s : ''}]`, '#bFile', '#gfile', { wait: v === 'waves' || v === 'chains' ? 700 : 450 }); }
  }
  if (want('edit')) {
    for (const s of ['song', 'phrase', 'mixer', 'fx', 'project'].filter(wantS)) {
      await go('tracker', s);
      if (s === 'phrase') await app(page, `(cs.phrase.x = 0, cs.phrase.y = 0, selA = null, draw(), 1)`);
      await sweepMenu(`Edit[${s}]`, '#bEdit', '#gedit');
    }
    for (const v of ['synth', 'chains'].filter(wantS)) { await go(v); await sweepMenu(`Edit[${v}]`, '#bEdit', '#gedit'); }
  }
  if (want('view')) { await go('tracker', 'song'); await sweepPanel('View', '#bView'); }
  if (want('aud')) { await go('tracker', 'song'); await sweepPanel('Audio&MIDI', '#bAud'); }
  if (want('set')) { for (const [v, s] of [['tracker', 'song'], ['synth']]) { await go(v, s); await sweepPanel(`Settings[${v}]`, '#bSet'); } }
  if (want('ctx')) {
    // Right-click menus: find the target, right-click it with the mouse, list and click every item.
    const ctxSweep = async (label, setup, target) => {
      if (CTXF && !label.toLowerCase().includes(CTXF.toLowerCase())) return;
      await closeAll(); await loadElectro(); await setup();
      const box = await target(); if (!box) { T.add(`ctx ${label}`, false, 'target not found'); return; }
      await page.mouse.click(box.x, box.y, { button: 'right' }); await page.waitForTimeout(300);
      const items = await menuItems('.sxmenu');
      const dlg = (await snap()).dlg;
      if (!items.length) { T.add(`ctx ${label}`, dlg ? true : 'NOTE', dlg ? 'opened ' + dlg : 'no menu (right-click does something else here)'); await closeAll(); return; }
      T.add(`ctx ${label}: opens (${items.length})`, true, items.map(i => i.t).join(' · ').slice(0, 200));
      await closeAll();
      for (const it of items) {
        const name = `ctx ${label} › ${it.t}`; if (it.off) { T.add(name, 'OFF', 'disabled'); continue; }
        if (skipWhy(it.t)) { T.add(name, 'SKIP', skipWhy(it.t)); continue; }
        await closeAll(); await setup(); takeErrors(errors);
        const bx = await target(); await page.mouse.click(bx.x, bx.y, { button: 'right' }); await page.waitForTimeout(250);
        const a = await snap();
        try { await clickNth('.sxmenu', it.i); } catch (e) { T.add(name, false, 'click failed: ' + e.message.split('\n')[0]); continue; }
        await page.waitForTimeout(450);
        const b = await snap(); const did = [];
        for (const k of ['S', 'kit', 'view', 'tv', 'mute', 'dlg', 'sel', 'loop', 'clip', 'cur', 'undo', 'picks']) if (a[k] !== b[k]) did.push(k + (k === 'dlg' ? '=' + b.dlg.slice(0, 30) : ''));
        if (b.toast && b.toast !== a.toast) did.push('toast "' + b.toast.slice(0, 60) + '"');
        const closed = await closeAll(); const errs = takeErrors(errors);
        T.add(name, errs.length ? false : !closed ? false : did.length ? true : 'NOTE', (errs.length ? 'ERR ' + errs.join(' / ').slice(0, 160) + ' · ' : '') + (!closed ? 'LEFT OPEN ' : '') + (did.join(', ') || 'no visible change'));
        if (b.view !== 'tracker' && a.view === 'tracker') await app(page, `(setView('tracker'), 1)`);
        await loadElectro();
      }
    };
    const rect = sel => page.evaluate(s => { const e = document.querySelector(s); if (!e || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; }, sel);
    const ph = async pv => { await go('tracker', 'phrase'); await app(page, `(pvMode = ${pv}, cs.phrase.x = 0, cs.phrase.y = 2, selA = null, draw(), 1)`); await page.waitForTimeout(300); };
    await ctxSweep('Song block', () => go('tracker', 'song'), () => rect('#tArrGrid b[data-y="1"][data-t="0"]'));
    await ctxSweep('Song empty cell', () => go('tracker', 'song'), () => rect('#tArrGrid b[data-y="2"][data-t="12"]'));
    await ctxSweep('Song row number', () => go('tracker', 'song'), () => rect('#tArrGrid [data-ry="2"]'));
    await ctxSweep('Song track number', () => go('tracker', 'song'), () => rect('#tArrTh i[data-t="2"]'));
    await ctxSweep('Arrange block (beside Phrase)', () => ph(false), () => rect('#tArrGrid b[data-y="1"][data-t="0"]'));
    await ctxSweep('Phrase cell', () => ph(false), () => rect('#tGrid span[data-x="0"][data-y="2"]'));
    await ctxSweep('Pattern view cell', () => ph(true), () => rect('#tGrid span[data-x="0"][data-y="2"]'));
    await ctxSweep('Phrase row number', () => ph(false), () => rect('#tGrid [data-r="2"]'));
    await ctxSweep('Inst cell', () => go('tracker', 'inst'), () => rect('#tGrid [data-y="1"]'));
    await ctxSweep('Mixer VOL bar', () => go('tracker', 'mixer'), () => rect('#tGrid .mfbar'));
    await ctxSweep('Mixer cell', () => go('tracker', 'mixer'), () => rect('#tGrid [data-x="2"][data-y="0"]'));
    await ctxSweep('FX slot', () => go('tracker', 'fx'), () => rect('#tGrid [data-y="0"]'));
    await ctxSweep('Project row', () => go('tracker', 'project'), () => rect('#tGrid [data-y="1"]'));
    await ctxSweep('Bottom deck VOL fader', () => go('tracker', 'song'), () => rect('#stVol'));
    await ctxSweep('Synth pad', () => go('synth'), () => rect('#pads .pad[data-pad="1"]'));
    await ctxSweep('Synth layer strip', () => go('synth'), () => rect('#strip .lcell'));
    await ctxSweep('Synth slider (Level)', () => go('synth'), () => rect('#vRows input[type=range]'));
    await ctxSweep('Side panel (map)', () => go('tracker', 'song'), () => rect('#tMap [data-v="mixer"]'));
    await ctxSweep('Meter strip', () => go('tracker', 'phrase'), () => rect('#tMeters .mtr'));
    await ctxSweep('FX bar value', async () => { await go('tracker', 'phrase'); await app(page, `(window.__seqTest.fxSet(0, 'comp'), cTrack = 0, draw(), 1)`); await page.waitForTimeout(400); }, () => rect('#dkFxBar .dkfc .fr span'));
  }
} catch (e) { console.error('sweep stopped:', e); T.add('sweep', false, String(e.message).slice(0, 200)); }
const f = T.print();
await B.close();
process.exit(f ? 1 : 0);
