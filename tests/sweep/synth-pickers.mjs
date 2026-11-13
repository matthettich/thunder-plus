// SYNTH PICKERS check (13 Nov): makePick replaced native selects on the Synth page. For each picker that is built after the
// page loads (Macro osc model, waves bus, starter wavetable, an effect's long list, Add effect, tuning preset) a pick through
// the picker's own list must (1) change the same field as setting the hidden select directly does, (2) update the button's
// label to the chosen item, and (3) audition the pad once.   node tests/sweep/synth-pickers.mjs
// The SoundFont selects need a font (General MIDI is a 24 MB download, or the user's own file): not exercised here.
import { boot, app } from './ui-lib.mjs';
const B = await boot({ width: 1440, height: 900 }), { page } = B;
let failed = false; const say = (ok, m) => { console.log(`${ok ? 'ok  ' : 'FAIL'} ${m}`); if (!ok) failed = true; };
const wait = ms => page.waitForTimeout(ms);
const esc = s => s.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
try {
  await app(page, "setView('synth')"); await page.mouse.click(700, 5); await wait(300);   // a real gesture: user activation
  await page.evaluate(() => { window.__plays = 0; const st = AudioBufferSourceNode.prototype.start; AudioBufferSourceNode.prototype.start = function(){ window.__plays++; return st.apply(this, arguments); }; });
  const plays = () => page.evaluate(() => { const n = window.__plays; window.__plays = 0; return n; });

  // one case: setup() puts the pad in the state the picker shows in; sel: JS for the <select>; field: JS giving the saved value
  async function run(name, { setup, sel, field, optText, settle = 2200 }) {
    await app(page, setup); await wait(1200); await plays();
    const info = JSON.parse(await app(page, `(() => { const s = ${sel}; if (!s || !s._pk) return JSON.stringify({err: 'no picker'}); s._pk.btn.setAttribute('data-tp', '1');
      return JSON.stringify({opts: [...s.options].map(o => [o.value, o.textContent]), cur: s.selectedIndex}); })()`));
    if (info.err) return say(false, `${name}: ${info.err}`);
    const target = info.opts.find((o, i) => i !== info.cur && o[0] !== '' && (optText ? o[1] === optText : true)) || info.opts[1];
    const before = await app(page, field);
    // 1) through the picker
    await page.evaluate(() => document.querySelector('[data-tp]').click()); await wait(250);
    const item = page.locator('.ask .askitem', { hasText: new RegExp('^' + esc(target[1]) + '$') }).first();
    if (!await item.count()) return say(false, `${name}: "${target[1]}" is not in the picker's list`);
    await item.click(); await wait(settle);
    const viaPick = await app(page, field), n = await plays();
    const label = await page.evaluate(() => document.querySelector('[data-tp]').textContent);
    const dlg = await page.evaluate(() => !!document.querySelector('.ask'));
    // 2) the same pick on the hidden select, from the same starting state
    await app(page, setup); await wait(1200); await plays();
    await app(page, `(() => { const s = ${sel}; s.value = ${JSON.stringify(target[0])}; s.dispatchEvent(new Event('change', {bubbles:true})); })()`); await wait(settle);
    const direct = await app(page, field), n2 = await plays();
    const keepsPlaceholder = target[0] !== '' && /Starter tables|Add effect/.test(info.opts[0][1]);
    say(viaPick !== before, `${name}: the picker changed the field (${before} → ${viaPick})`);
    say(viaPick === direct, `${name}: same field as setting the hidden select (${viaPick} vs ${direct})`);
    say(keepsPlaceholder ? true : label.replace('▾', '').trim() === target[1], `${name}: label follows (“${label.replace('▾', '').trim()}”${keepsPlaceholder ? ', a placeholder picker' : ''})`);
    say(n === 1 && n2 === 1, `${name}: the pad auditions once (picker ${n}, direct ${n2})`);
    say(!dlg, `${name}: the list closed`);
    await page.evaluate(() => document.querySelector('[data-tp]') && document.querySelector('[data-tp]').removeAttribute('data-tp'));
  }

  await run('Macro osc model', { setup: "(() => { select(0); const v = V(); v.engine = 'layers'; const L = Lc(); L.type = 'plaits'; L.on = true; L.pEng = 0; refreshLayer(); changed(cur); })()",
    sel: "$('plEng')", field: 'String(Lc().pEng)' });
  await run('waves bus', { setup: "(() => { select(0); const v = V(); v.engine = 'layers'; const L = Lc(); L.type = 'waves'; L.on = true; L.wBus = 1; refreshLayer(); changed(cur); })()",
    sel: "$('wvBus')", field: 'String(Lc().wBus)' });
  await run('wavetable starter', { setup: "(() => { select(0); const v = V(); v.engine = 'table'; delete v.wtData; refreshAll(); changed(cur); })()",
    sel: "$('wtPick')", field: "(() => { const v = V(); return v.wtData ? v.wtData.name + '|' + v.wtData.nf + '|' + (v.table && v.table.table) : 'none'; })()", settle: 3500 });
  // effects: Add effect, then the first effect with a list of more than six choices
  const fxk = await app(page, "(() => { for (const k of Object.keys(FX)){ const d = FX[k]; if (d.segs && d.segs.some(s => s[2].length > 6)) return k + '|' + d.segs.find(s => s[2].length > 6)[0]; } return ''; })()");
  if (!fxk) say(false, 'no effect with a long list found');
  else {
    const [k, key] = fxk.split('|');
    await run('Add effect', { setup: "(() => { select(0); const v = V(); v.engine = 'layers'; v.fx = []; refreshAll(); changed(cur); })()",
      sel: "$('fxAdd')", field: "String((V().fx || []).map(f => f.type))", optText: await app(page, `FX[${JSON.stringify(k)}].name`) });
    await run(`effect list (${k} ${key})`, { setup: `(() => { select(0); const v = V(); v.engine = 'layers'; v.fx = [freshFx(${JSON.stringify(k)})]; refreshAll(); changed(cur); })()`,
      sel: "document.querySelector('select.fxsel')", field: `String((V().fx || []).map(f => f[${JSON.stringify(key)}]))` });
  }
  await run('tuning preset', { setup: "(() => { select(0); S.tune = new Array(12).fill(0); refreshTune(); changed(cur); })()",
    sel: "[...document.querySelectorAll('select')].find(s => s.getAttribute('aria-label') === 'Tuning preset')", field: 'JSON.stringify(S.tune)', optText: 'Just' });
  say(!B.errors.filter(e => /pageerror/.test(e)).length, 'no page errors ' + B.errors.filter(e => /pageerror/.test(e)).join(' | '));
} finally { await B.close(); }
process.exit(failed ? 1 : 0);
