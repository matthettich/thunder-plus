// UI SWEEP: the owner's mouse rules for value controls (HANDOVER "3 Nov (8): one rule for value controls"), checked on every
// kind of control with the real mouse:
//   double-click resets · right-click opens Reset / Type (/ Learn) · Alt-click resets · a drag is relative (no jump on press)
//   · Shift held = a tenth of the plain drag · Ctrl held = four times · the wheel changes a control only after the pointer
//   rested on it (~350 ms) · Shift+wheel is the fine step.
// Plus: Song grid Alt-drag copies; track numbers: Alt-click mutes, right-click solos; Pattern view band click opens the track.
//   node tests/sweep/mouse.mjs
import fs from 'fs'; import path from 'path';
import { boot, takeErrors, Table, app, ROOT } from './lib.mjs';

const T = new Table('mouse');
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
const B = await boot({ width: 1600, height: 1000 });
const { page, errors } = B;
const A = s => app(page, s);
await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(400);
await A(`(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`);
const box = sel => page.evaluate(s => { const e = typeof s === 'string' ? document.querySelector(s) : null; if (!e || !e.getClientRects().length) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: r.width, h: r.height, l: r.left, t: r.top }; }, sel);
const menuItems = () => page.evaluate(() => { const m = document.querySelector('.sxmenu'); return m ? [...m.querySelectorAll('button')].map(b => b.textContent.trim()) : null; });
const closeMenus = () => A(`(sxMenuClose(), document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, 1)`);

// One control: how to find it, read its value, put it somewhere not default, which way a drag goes ('x' or 'y'), its default.
async function rules(name, c) {
  try {
    await c.setup(); await page.waitForTimeout(250);
    let b = await box(c.sel); if (!b) { T.add(`${name}: found`, false, 'not on screen: ' + c.sel); return; }
    const get = () => A(c.get), put = v => A(c.set(v));
    let mid = c.mid, def = c.def;
    if (c.pos) { await put(c.def); def = await get(); await put(c.mid); const pm = await get(); const set0 = c.set; c.set = v => v === pm ? set0(c.mid) : v === def ? set0(c.def) : set0(v); mid = pm; }
    // double-click resets
    await put(mid); b = await box(c.grab ? c.grab : c.sel);
    await page.mouse.dblclick(b.x, b.y); await page.waitForTimeout(250);
    let v = await get(); T.add(`${name}: double-click resets`, v === def, `${mid} → ${v} (default ${def})`);
    // Alt-click resets
    await put(mid); b = await box(c.grab ? c.grab : c.sel);
    await page.keyboard.down('Alt'); await page.mouse.click(b.x, b.y); await page.keyboard.up('Alt'); await page.waitForTimeout(200);
    v = await get(); T.add(`${name}: Alt-click resets`, v === def, `${mid} → ${v}`);
    // right-click menu
    await put(mid); b = await box(c.grab ? c.grab : c.sel);
    await page.mouse.click(b.x, b.y, { button: 'right' }); await page.waitForTimeout(250);
    const items = await menuItems(); const has = items && items.some(t => /^Reset/.test(t)) && items.some(t => /^Type/.test(t)), hasReset = items && items.some(t => /^Reset/.test(t));
    T.add(`${name}: right-click Reset / Type${c.learn ? ' / Learn' : ''}`, !!has && (!c.learn || items.some(t => /^Learn/.test(t))), items ? items.join(' · ') : 'no menu');
    if (hasReset) { await page.locator('.sxmenu button', { hasText: /^Reset/ }).click(); await page.waitForTimeout(200); v = await get(); T.add(`${name}: menu Reset works`, v === def, `→ ${v}`); }
    await closeMenus();
    if (has) { await put(mid); b = await box(c.grab ? c.grab : c.sel); await page.mouse.click(b.x, b.y, { button: 'right' }); await page.waitForTimeout(200);
      await page.locator('.sxmenu button:not([disabled])', { hasText: /^Type/ }).click({ timeout: 3000 }); await page.waitForTimeout(300);
      const dlg = await A(`!!document.querySelector('.ask input, .row output input, .pumpedit:not([hidden])')`); T.add(`${name}: menu Type opens a box`, dlg, '');
      await page.keyboard.press('Escape'); await closeMenus(); }
    // drag: relative (no jump on press), Shift a tenth, Ctrl four times
    const dragBy = async (px, mod) => {
      await put(mid); await page.waitForTimeout(100); b = await box(c.grab ? c.grab : c.sel); const v0 = await get();
      const sx = b.x + (c.off || 0), sy = b.y;
      await page.mouse.move(sx, sy); await page.mouse.down(); await page.waitForTimeout(60);
      const vPress = await get();
      if (mod) await page.keyboard.down(mod);
      const steps = 8; for (let i = 1; i <= steps; i++) { const d = px * i / steps; await page.mouse.move(c.dir === 'x' ? sx + d : sx, c.dir === 'y' ? sy - d : sy); await page.waitForTimeout(15); }
      await page.waitForTimeout(80);
      const v1 = await get(); await page.mouse.up(); if (mod) await page.keyboard.up(mod); await page.waitForTimeout(100);
      return { v0, vPress, v1, d: v1 - v0 };
    };
    const plain = await dragBy(c.px || 20);
    T.add(`${name}: press does not jump`, plain.vPress === plain.v0, `${plain.v0} → ${plain.vPress} on press`);
    T.add(`${name}: drag moves it`, plain.d !== 0, `Δ ${plain.d}`);
    const fine = await dragBy(c.px || 20, 'Shift');
    const big = await dragBy(c.px || 20, c.mac ? 'Meta' : 'Control');
    const rf = plain.d ? fine.d / plain.d : NaN, rb = plain.d ? big.d / plain.d : NaN;
    T.add(`${name}: Shift drag ≈ a tenth`, Math.abs(fine.d) < Math.abs(plain.d) && Math.abs(rf - 0.1) < 0.12, `plain ${plain.d}, Shift ${fine.d} (×${rf.toFixed(2)})`);
    T.add(`${name}: Ctrl drag ≈ ×4`, Math.abs(big.d) > Math.abs(plain.d) && (rb > 2.5 || big.v1 === c.max || big.v1 === c.min), `plain ${plain.d}, Ctrl ${big.d} (×${rb.toFixed(2)})`);
    // wheel: not before the rest; after the rest yes; Shift fine; Ctrl big
    await put(mid); b = await box(c.sel);
    await page.mouse.move(b.l - 30, b.y); await page.waitForTimeout(400);
    const tw = Date.now(); await page.mouse.move(b.x, b.y); await page.mouse.wheel(0, -100); const lag = Date.now() - tw; await page.waitForTimeout(120);
    const vQuick = await get(); T.add(`${name}: wheel ignored before the rest`, vQuick === mid ? true : lag > 300 ? 'SLOW' : false, `${mid} → ${vQuick}` + (lag > 300 ? ` (the wheel came ${lag} ms after the move, past the 350 ms rest: machine too slow to tell)` : ''));
    await page.waitForTimeout(500); await put(mid);
    await page.mouse.move(b.x, b.y + 1); await page.waitForTimeout(500); await page.mouse.wheel(0, -100); await page.waitForTimeout(150); const w1 = (await get()) - mid;
    T.add(`${name}: wheel after the rest`, w1 !== 0, `Δ ${w1} a notch`);
    await put(mid); await page.keyboard.down('Shift'); await page.mouse.wheel(0, -100); await page.keyboard.up('Shift'); await page.waitForTimeout(150); const wf = (await get()) - mid;
    await put(mid); await page.keyboard.down('Control'); await page.mouse.wheel(0, -100); await page.keyboard.up('Control'); await page.waitForTimeout(150); const wb = (await get()) - mid;
    T.add(`${name}: Shift+wheel finer`, Math.abs(wf) < Math.abs(w1) && wf !== 0, `plain ${w1}, Shift ${wf} (×${(wf / w1).toFixed(2)})`);
    T.add(`${name}: Ctrl+wheel bigger`, Math.abs(wb) > Math.abs(w1), `plain ${w1}, Ctrl ${wb} (×${(wb / w1).toFixed(2)})`);
    c.ratios = { drag: rf, wheel: wf / w1 };
    const errs = takeErrors(errors); if (errs.length) T.add(`${name}: no errors`, false, errs.join(' / ').slice(0, 200));
    await page.mouse.move(5, 500);
  } catch (e) { T.add(`${name}`, false, 'threw ' + e.message.split('\n')[0].slice(0, 200)); await closeMenus().catch(() => {}); }
}

const R = {};
// a synth page slider (makeRow): Level of pad 0
R.synth = { setup: () => A(`(setView('synth'), 1)`), sel: '#r_vlevel', dir: 'x', px: 10,
  get: `+document.querySelector('#r_vlevel').value`, set: v => `(kit.voices[cur].level = ${v}, vRows.forEach(r => r.refresh()), 1)`, mid: -12, def: 0, learn: true, pos: true };   // read as the slider's position (0–1000)
await rules('Synth slider (Level)', R.synth);
// the Mixer's VOL fader bar for track 2
R.mfbar = { setup: () => A(`(setView('tracker'), go('mixer'), draw(), 1)`), sel: '#tGrid .mfbar[data-t="1"]', dir: 'y', px: 20,
  get: 'S.mix[1].vol', set: v => `(S.mix[1].vol = ${v}, draw(), 1)`, mid: 128, def: 224 };
await rules('Mixer VOL bar', R.mfbar);
R.mobar = { setup: () => A(`(setView('tracker'), go('mixer'), draw(), 1)`), sel: '#tGrid .mobar', dir: 'x', px: 20,
  get: '(S.mout == null ? 255 : S.mout)', set: v => `(S.mout = ${v}, draw(), 1)`, mid: 160, def: 255 };
await rules('Mixer master OUT bar', R.mobar);
// the bottom strip fader: grab its cap
R.strip = { setup: () => A(`(setView('tracker'), go('phrase'), cTrack = 0, draw(), deckStrip(), 1)`), sel: '#stVol', grab: '#stVol .stcap', dir: 'y', px: 20,
  get: 'S.mix[dkTrack()].vol', set: v => `(S.mix[dkTrack()].vol = ${v}, deckStrip(), 1)`, mid: 128, def: 224 };
await rules('Strip VOL fader (cap)', R.strip);
// strip PAN (a plain range with the document's generic slider rules)
R.pan = { setup: () => A(`(setView('tracker'), go('phrase'), cTrack = 0, draw(), deckStrip(), 1)`), sel: '#stPan', dir: 'x', px: 20,
  get: 'S.mix[dkTrack()].pan', set: v => `(S.mix[dkTrack()].pan = ${v}, deckStrip(), 1)`, mid: 60, def: 128 };
await rules('Strip PAN slider', R.pan);
// FX bar value: put an EQ… a compressor on track 1, first value
R.fxbar = { setup: async () => { await A(`(setView('tracker'), go('phrase'), cTrack = 0, window.__seqTest.fxSet(0, 'comp'), draw(), typeof dkFxBuild === 'function' && dkFxBuild(), 1)`); await page.waitForTimeout(400); },
  sel: '#dkFxBar .dkfc .fr span', dir: 'y', px: 20,
  get: `(() => { const r = document.querySelector('#dkFxBar .dkfc .fr'); const f = S.tfx[0][0]; return f[r._fb.q.k]; })()`,
  set: v => `(() => { const r = document.querySelector('#dkFxBar .dkfc .fr'); S.tfx[0][0][r._fb.q.k] = ${v}; return 1; })()`, mid: 0, def: null };
{ const d = await A(`(setView('tracker'), go('phrase'), cTrack = 0, window.__seqTest.fxSet(0, 'comp'), draw(), 1)`).then(() => page.waitForTimeout(500)).then(() => A(`(() => { const r = document.querySelector('#dkFxBar .dkfc .fr'); return r ? JSON.stringify({d:r._fb.q.d, min:r._fb.q.min, max:r._fb.q.max, l:r._fb.q.l}) : null; })()`));
  if (d) { const q = JSON.parse(d); R.fxbar.def = q.d; R.fxbar.mid = Math.round((q.min + q.max) / 2) === q.d ? q.min + Math.round((q.max - q.min) / 4) : Math.round((q.min + q.max) / 2); R.fxbar.min = q.min; R.fxbar.max = q.max;
    await rules(`FX bar value (${q.l})`, R.fxbar); } else T.add('FX bar value', false, 'no FX bar value on screen'); }
// Inst / project screen values aren't sliders (keys and Alt-drag), so they're covered in keys.mjs.

// The step sizes side by side: one rule means one Shift ratio everywhere
const rs = Object.entries(R).filter(([, c]) => c.ratios).map(([k, c]) => `${k}: drag ×${c.ratios.drag.toFixed(2)}, wheel ×${c.ratios.wheel.toFixed(2)}`);
T.add('Shift ratio across controls', 'NOTE', rs.join(' · '));

// ---- the tracker grid's Alt-drag value change, Shift fine there ----
try {
  await A(`(setView('tracker'), go('phrase'), pvMode = false, cs.phrase.x = 4, cs.phrase.y = 2, S.phrases[cPhrase][2][4] = 0x40, draw(), 1)`); await page.waitForTimeout(300);
  const cellBox = async () => page.evaluate(() => { const e = document.querySelector('#tGrid [data-x="4"][data-y="2"]'); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  const altDrag = async (mod) => { await A(`(S.phrases[cPhrase][2][4] = 0x40, draw(), 1)`); const b = await cellBox(); if (!b) return null; await page.keyboard.down('Alt'); if (mod) await page.keyboard.down(mod);
    await page.mouse.move(b.x, b.y); await page.mouse.down(); for (let i = 1; i <= 6; i++) { await page.mouse.move(b.x, b.y - i * 16); await page.waitForTimeout(30); } await page.mouse.up(); if (mod) await page.keyboard.up(mod); await page.keyboard.up('Alt'); await page.waitForTimeout(150); return (await A('S.phrases[cPhrase][2][4]')) - 0x40; };
  const p = await altDrag(), s = await altDrag('Shift'), c = await altDrag('Control');
  T.add('Tracker Alt-drag changes the value', p > 0, `Δ ${p}`);
  T.add('Tracker Alt+Shift-drag finer', s != null && Math.abs(s) < Math.abs(p), `Δ ${s} (×${(s / p).toFixed(2)}: 16 vs 48 px a step = ×0.33, not the ×0.1 of the other controls)`);
  T.add('Tracker Alt+Ctrl-drag bigger', c != null && Math.abs(c) > Math.abs(p), `Δ ${c}`);
} catch (e) { T.add('tracker Alt-drag', false, e.message.split('\n')[0]); }

// ---- track numbers: plain click picks, Alt-click mutes, right-click solos; on the Song text grid, Arrange and the meters ----
try {
  for (const [where, setup, sel] of [
    ['Arrange header (beside Phrase)', `(setView('tracker'), go('phrase'), draw(), 1)`, '#tArrTh i[data-t="2"], .arrth i[data-t="2"]'],
    ['Meters strip', `(setView('tracker'), go('phrase'), draw(), 1)`, '#tMeters .mtr:nth-child(3)'],
  ]) {
    await A(setup); await A(`(S.mix.forEach(m => { m.mute = false; m.solo = false; }), mixChanged(), draw(), 1)`); await page.waitForTimeout(300);
    const b = await box(sel); if (!b) { T.add(`${where}: track number`, false, 'not found ' + sel); continue; }
    await page.mouse.click(b.x, b.y); await page.waitForTimeout(200);
    let st = await A(`JSON.stringify([S.mix[2].mute, S.mix[2].solo])`); T.add(`${where}: click picks (no mute)`, st === '[false,false]', st);
    await page.keyboard.down('Alt'); await page.mouse.click(b.x, b.y); await page.keyboard.up('Alt'); await page.waitForTimeout(200);
    st = await A(`JSON.stringify([S.mix[2].mute, S.mix[2].solo])`); T.add(`${where}: Alt-click mutes`, st === '[true,false]', st);
    await page.mouse.click(b.x, b.y, { button: 'right' }); await page.waitForTimeout(200);
    st = await A(`JSON.stringify([S.mix[2].mute, S.mix[2].solo, !!document.querySelector('.sxmenu')])`); T.add(`${where}: right-click solos (no menu)`, st === '[true,true,false]', st);
    await closeMenus();
  }
  // Song text grid header (Classic song) if shown
  await A(`(setView('tracker'), go('song'), S.mix.forEach(m => { m.mute = false; m.solo = false; }), mixChanged(), draw(), 1)`); await page.waitForTimeout(300);
  const sh = await page.evaluate(() => { const e = [...document.querySelectorAll('#tArr [data-t], #tArrTh i[data-t], #tGrid [data-t]')].find(x => x.getClientRects().length && x.dataset.t === '2'); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, tag: e.tagName + '.' + e.className }; });
  if (sh) { await page.mouse.click(sh.x, sh.y); await page.waitForTimeout(150); let st = await A(`JSON.stringify([S.mix[2].mute, S.mix[2].solo])`); T.add(`Song header (${sh.tag}): click picks`, st === '[false,false]', st);
    await page.keyboard.down('Alt'); await page.mouse.click(sh.x, sh.y); await page.keyboard.up('Alt'); await page.waitForTimeout(150); st = await A(`JSON.stringify([S.mix[2].mute, S.mix[2].solo])`); T.add('Song header: Alt-click mutes', st === '[true,false]', st);
    await page.mouse.click(sh.x, sh.y, { button: 'right' }); await page.waitForTimeout(150); st = await A(`JSON.stringify([S.mix[2].mute, S.mix[2].solo])`); T.add('Song header: right-click solos', st === '[true,true]', st); await closeMenus(); }
  else T.add('Song header track number', 'NOTE', 'not found');
  await A(`(S.mix.forEach(m => { m.mute = false; m.solo = false; }), mixChanged(), 1)`);
  // Pattern view band: click opens that track
  await A(`(setView('tracker'), go('phrase'), pvMode = true, cTrack = 0, draw(), 1)`); await page.waitForTimeout(400);
  const band = await page.evaluate(() => { const hs = [...document.querySelectorAll('#tGrid .pvh')].filter(e => e.getClientRects().length); const e = hs[0]; if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width * 0.45, y: r.top + r.height / 2 }; });
  if (band) { await page.mouse.click(band.x, band.y); await page.waitForTimeout(300); const t = await A('cTrack + "/" + S.mix.some(m => m.mute)'); T.add('Pattern view band: click opens the track (no mute)', /\/false$/.test(t), 'cTrack/muted ' + t);
    await page.mouse.click(band.x, band.y, { button: 'right' }); await page.waitForTimeout(200); const so = await A('S.mix.filter(m => m.solo).length'); T.add('Pattern view band: right-click solos', so === 1, 'solos ' + so); }
  else T.add('Pattern view band', 'NOTE', 'band not found');
  await A(`(S.mix.forEach(m => { m.mute = false; m.solo = false; }), mixChanged(), pvMode = false, draw(), 1)`);
} catch (e) { T.add('track numbers', false, e.message.split('\n')[0]); }

// ---- Song grid Alt-drag copies (as Arrange) ----
try {
  await A(`(setView('tracker'), go('song'), selA = null, draw(), 1)`); await page.waitForTimeout(300);
  const src = await page.evaluate(() => { const e = document.querySelector('#tArr b[data-y="1"][data-t="0"], #tGrid [data-x="0"][data-y="1"]'); if (!e) return null; const r = e.getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2, h: r.height, sel: e.tagName }; });
  if (src) {
    const v0 = await A('S.song[1][0]'), below = await A('JSON.stringify(S.song.slice(1, 40).map(r => r[0]))');
    await page.keyboard.down('Alt'); await page.mouse.move(src.x, src.y); await page.mouse.down();
    for (let i = 1; i <= 10; i++) { await page.mouse.move(src.x, src.y + i * src.h * 0.6); await page.waitForTimeout(30); }
    await page.mouse.up(); await page.keyboard.up('Alt'); await page.waitForTimeout(300);
    const after = await A('JSON.stringify([S.song[1][0], S.song.slice(1, 40).map(r => r[0])])'); const [v1, col] = JSON.parse(after);
    const copies = col.filter((x, i) => x >= 0 && JSON.parse(below)[i] !== x).length;
    T.add('Song grid Alt-drag copies the block', v1 === v0 && copies >= 1, `source ${v0} → ${v1}, changed cells below: ${copies}`);
    await A('(undoStep(-1), 1)');
  } else T.add('Song grid Alt-drag', false, 'no block found');
} catch (e) { T.add('Song Alt-drag', false, e.message.split('\n')[0]); }

// ---- …pump window controls (canvas): Dial, Row and Field under the same drag arithmetic, with a stand-in window ----
try {
  const r = await page.evaluate(() => {
    const U = window.PumpUI; if (!U) return null;
    const mk = (C, args) => { const c = new C(...args); c.r = U.R(0, 0, 200, 30); return c; };
    const fake = (v0, mn, mx) => { const ui = { type: 'meter', v: v0, value() { return this.v; }, norm() { return (this.v - mn) / (mx - mn); }, setNorm(s, n) { this.v = mn + Math.max(0, Math.min(1, n)) * (mx - mn); }, setParam(s, v) { this.v = Math.max(mn, Math.min(mx, v)); }, begin() {}, end() {}, spec() { return { flags: 0 }; } }; return ui; };
    const run = (C, args, mods, dir) => { const out = {}; for (const [k, m] of Object.entries(mods)) { const ui = fake(-20, -60, 60), c = mk(C, args); c.down(ui, 100, 100, 0);
      for (let i = 1; i <= 10; i++) c.drag(ui, dir === 'x' ? 100 + i * 2 : 100, dir === 'y' ? 100 - i * 2 : 100, m); c.up(ui); out[k] = Math.round((ui.v + 20) * 1000) / 1000; } return out; };
    const M = { plain: { shift: false, cmd: false }, shift: { shift: true, cmd: false }, cmd: { shift: false, cmd: true } };
    // Shift pressed mid-drag: no jump back
    const mid = C => { const ui = fake(-20, -60, 60), c = mk(C, ['custom_lufs']); c.down(ui, 0, 100, 0); for (let i = 1; i <= 10; i++) c.drag(ui, 0, 100 - i * 2, M.plain); const a = ui.v; c.drag(ui, 0, 100 - 22, M.shift); return [a, ui.v]; };
    return { field: run(U.Field, ['custom_lufs'], M, 'y'), fieldMid: mid(U.Field) };
  });
  if (!r) T.add('…pump Field', 'SKIP', 'PumpUI not loaded');
  else {
    const f = r.field; T.add('…pump Field: Shift drag a tenth', Math.abs(f.shift / f.plain - 0.1) < 0.02, JSON.stringify(f));
    T.add('…pump Field: Cmd/Ctrl drag ×4', Math.abs(f.cmd / f.plain - 4) < 0.2, JSON.stringify(f));
    T.add('…pump Field: Shift mid-drag no jump', r.fieldMid[1] >= r.fieldMid[0], JSON.stringify(r.fieldMid));
  }
} catch (e) { T.add('…pump Field', false, e.message.split('\n')[0]); }

const f = T.print();
await B.close();
process.exit(f ? 1 : 0);
