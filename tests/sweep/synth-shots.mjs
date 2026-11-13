// Synth page screenshots at the owner's sizes, for looking at a layout change (13 Nov).
//   node tests/sweep/synth-shots.mjs <tag> [pad] [skin]      → $OUT or tests/sweep/out/synth-<tag>-<size>.png
// pad: the slot to select (default 0: a drum pad; 8 is usually a synth slot). Each size gets the first screenful and,
// for phones, a full-page shot too (how long the page is).
import fs from 'fs'; import path from 'path'; import { boot, app, OUT } from './ui-lib.mjs';
const [tag = 'now', pad = '0', skin = ''] = process.argv.slice(2), out = process.env.OUT || OUT; fs.mkdirSync(out, { recursive: true });
const SIZES = [['1920', 1920, 1080], ['1440', 1440, 900], ['1280', 1280, 800], ['fold-open', 884, 1020, 1], ['fold-412', 412, 915, 1], ['fold-360', 360, 800, 1]];
for (const [n, width, height, mobile] of SIZES) {
  const B = await boot({ width, height, mobile: !!mobile });
  try {
    if (skin) await app(B.page, `(() => { try { localStorage.setItem(SKK, ${JSON.stringify(skin)}); applySkin(${JSON.stringify(skin)}); } catch(e){ return String(e); } })()`);
    await app(B.page, `(() => { setView('synth'); select(${+pad}); })()`); await B.page.waitForTimeout(1200);
    const h = await B.page.evaluate(() => { const m = document.getElementById('main'); return { doc: document.documentElement.scrollHeight, main: m ? m.scrollHeight : 0, eng: (() => { const e = document.getElementById('vEngBtn') || document.getElementById('vEngine'); return e ? Math.round(e.getBoundingClientRect().top) : null; })() }; });
    await B.page.screenshot({ path: path.join(out, `synth-${tag}-${n}.png`) });
    if (mobile) await B.page.screenshot({ path: path.join(out, `synth-${tag}-${n}-full.png`), fullPage: true });
    const errs = B.errors.filter(e => /pageerror/.test(e));
    console.log(`${n.padEnd(10)} ${width}x${height}  page height ${h.doc} (main ${h.main})  engine picker top ${h.eng}px${errs.length ? '  ERRORS ' + errs.join(' | ') : ''}`);
  } finally { await B.close(); }
}
