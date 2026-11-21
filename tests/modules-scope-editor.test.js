// SCOPE, editor side (14 Nov 2026, module queue): the Scope module in both editors. Source guards in the style of modules-clock-editor:
// the definition equals the worklet's params, the palette / about / use / width / Compact entries are there, the shared editor block is
// identical in …chains and …waves, the draw function reads the engine's 2 + 512 layout, and the picture's colours come from CSS variables only.
// (The headless-browser check, opt-in with TP_BROWSER_TESTS=1, is tests/sweep/scope-editor.mjs: a Scope fed by an LFO / an oscillator
// paints its canvas in both editors, and Freeze stops it changing.)
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const WK = { chains: H.read('chains/chains-worklet.js'), waves: H.read('waves/waves-worklet.js') };
const block = (src, tag) => { const m = new RegExp('/\\* ===' + tag + ' START===[\\s\\S]*?/\\* ===' + tag + ' END=== \\*/').exec(src); assert.ok(m, tag); return m[0]; };
function literal(src, i) {
  const open = src[i], close = open === '{' ? '}' : ']'; let d = 0, q = null;
  for (let j = i; j < src.length; j++) {
    const c = src[j];
    if (q) { if (c === '\\') j++; else if (c === q) q = null; continue; }
    if (c === "'" || c === '"' || c === '`') q = c; else if (c === open) d++; else if (c === close && --d === 0) return src.slice(i, j + 1);
  }
  throw new Error('unbalanced');
}
const ev = s => vm.runInNewContext('(' + s + ')', { opts: names => names.map((t, i) => [i, t]), secs: v => v < 1 ? Math.round(v * 1000) + ' ms' : v.toFixed(2) + ' s' });
const entry = src => { const m = /\n  scope:\{ label:/.exec(src); assert.ok(m, 'editor entry for scope'); return literal(src, m.index + '\n  scope:'.length); };
const workletParams = src => { const i = src.indexOf("type:'scope', scope:'"); assert.ok(i > 0); const j = src.indexOf('params:', i); return ev(literal(src, src.indexOf('{', j))); };

for (const ed of ['chains', 'waves']) {
  const src = ED[ed];
  test(ed + ': Scope is defined with the engine\'s params and ports, no outputs, and a label for every control', () => {
    const e = entry(src), params = ev(literal(e, e.indexOf('params:') + 7)), ui = ev(literal(e, e.indexOf('ui:') + 3));
    assert.equal(JSON.stringify(params), JSON.stringify(workletParams(WK[ed])), 'params match the worklet');
    assert.equal(JSON.stringify(ev(literal(e, e.indexOf('inputs:') + 7))), JSON.stringify({ a: 'audio', b: 'audio', trig: 'gate' }));
    assert.equal(JSON.stringify(ev(literal(e, e.indexOf('outputs:') + 8))), '{}');
    assert.match(e, /display:'scope'/); assert.match(e, /scope:'mono'/);
    assert.equal(Object.keys(ui).join(), Object.keys(params).join());
    assert.equal([ui.time.l, ui.trig.l, ui.level.l, ui.freeze.l, ui.zoom.l].join(), 'Time,Trigger,Level,Freeze,Zoom');
    assert.equal(ui.time.f(.02), '20 ms'); assert.equal(ui.time.f(1.5), '1.50 s'); assert.equal(ui.zoom.f(2), '×2'); assert.equal(ui.level.f(.5), '+0.50');
    assert.equal(ui.trig.c.map(x => x[1]).join(), 'Free,Rising,Gate'); assert.equal(ui.freeze.c.map(x => x[1]).join(), 'Off,On');
  });
  test(ed + ': the about text says what it shows, the two traces, Trigger, Freeze, and that it only runs while the editor is open', () => {
    const a = /about:'([^']*)'/.exec(entry(src))[1];
    for (const re of [/what is on its inputs/, /A and B are two traces on the same time base/, /Rising waits for A to climb through Level so a steady wave holds still/, /Freeze keeps the picture/, /only runs while this editor is open/, /costs nothing while the song plays with the editor closed/]) assert.match(a, re);
  });
  test(ed + ': palette (right after Meter, in both lists), hint, use line, width, Compact keys, port labels', () => {
    assert.match(src, /\['Utilities', \[\['meter','Meter'\],\['scope','Scope'\],/);
    assert.match(/const PAL_LAYOUT = \[[\s\S]*?\n\];/.exec(src)[0], /\['Mix and level', '[^']+', \[[^\]]*'meter', 'scope'\]\]/);
    assert.match(/const PAL_HINT = \{[^}]*\}/.exec(src)[0], /\bscope:'see a signal'/);
    assert.match(src, /\n  scope:'Patch an LFO into A and the envelope it shapes into B to see both move together\.'/);
    assert.match(src, /MODULE_WIDTH = \{[\s\S]*?\bscope:300\b/); assert.match(src, /\bscope:'util'/);
    assert.match(/const SUMMARY_KEYS = \{[\s\S]*?\n\};/.exec(src)[0], /\bscope:\['time', 'trig'\]/);
    for (const k of ['a:\'A\'', 'b:\'B\'', 'trig:\'Trig\'']) assert.ok(src.includes(k), 'port label ' + k);
  });
  test(ed + ': Help has one paragraph on seeing signals', () => {
    assert.match(src, /\['Seeing signals', 'Meter \(in Mix and level in the Modules list\)[^']*Scope draws a signal[^']*Freeze[^']*only work while this editor is open\.'\]/);
  });
}
test('the Scope\'s editor code lives in the shared block, and the block is identical in both editors (and the shared worklet block too)', () => {
  const sh = block(ED.chains, 'SHARED EDITOR');
  for (const w of ['function buildScope', 'function drawScope', 'function scopeColors', 'const SCOPE_W = 128', "if (kind === 'scope') return buildScope(n)", "S.kind === 'scope'"]) assert.ok(sh.includes(w), w);
  assert.equal(sh, block(ED.waves, 'SHARED EDITOR')); assert.equal(block(WK.chains, 'SHARED MODULES'), block(WK.waves, 'SHARED MODULES'));
});
const SH = block(ED.chains, 'SHARED EDITOR');
const fn = name => { const i = SH.indexOf('function ' + name + '('); assert.ok(i > 0, name); return literal(SH, SH.indexOf('{', i)); };
test('drawScope reads the engine\'s layout: 128 columns of lowA, highA, lowB, highB after the sweep number and the B flag; a new picture only when the sweep number moves', () => {
  const d = fn('drawScope');
  assert.match(d, /d\[1\] === 1/, 'B patched flag'); assert.match(d, /d\[2 \+ i \* 4 \+ hi\]/); assert.match(d, /d\[2 \+ i \* 4 \+ lo\]/);
  assert.match(d, /trace\(0, 1, C\.a/); assert.match(d, /trace\(2, 3, C\.b/); assert.match(d, /yb - yt < 1\.5/, 'at least 1.5 px');
  assert.match(d, /\(p\.trig \| 0\) === 1/, 'the level tick only for Rising'); assert.match(d, /pk-pk /); assert.match(d, /secs\(/);
  assert.match(SH, /v\.length >= 2 \+ SCOPE_W \* 4 && v\[0\] !== S\.seq/, 'redraw only on a new sweep number');
  assert.match(SH, /p\.zoom, p\.time, p\.trig, p\.level, p\.freeze, w/, 'and on a setting or size change');
});
test('the Scope\'s colours are CSS variables only: no literal colour in its CSS or its draw code', () => {
  const code = [fn('buildScope'), fn('scopeColors'), fn('drawScope')].join('\n'), css = SH.split('\n').filter(l => /scopebox|scopecv/.test(l)).join('\n');
  assert.ok(css.length > 50);
  for (const t of [code, css]) { assert.doesNotMatch(t, /#[0-9a-fA-F]{3,8}\b/); assert.doesNotMatch(t, /\brgba?\s*\(|\bhsla?\s*\(/); assert.doesNotMatch(t, /['"](?:white|black|red|green|blue|gray|grey|teal|pink|purple|orange|yellow)['"]/); }
  for (const v of ['--audio', '--mod', '--ink', '--muted', '--panel-2']) assert.ok(fn('scopeColors').includes(v), v);
});
test('the canvas takes no pointer events (a drag on it pans in Play, as on the other displays) and sits outside the body so Compact keeps it and Mini hides it', () => {
  const b = fn('buildScope'); assert.doesNotMatch(b, /addEventListener|onpointer|setPointerCapture/); assert.match(b, /n\.mini = box/);
  assert.match(SH, /\.node > \.scopebox\{display:block/);
  assert.match(ED.chains, /\.node\.collapsed\.mini > :not\(header\)\{display:none !important\}/);
  assert.match(ED.chains, /\.node\.collapsed > \.body\{display:none\}/);
});

/* THE REAL PAGE (opt-in: TP_BROWSER_TESTS=1; boots headless Chrome twice, about 20 s). In each editor a Scope fed by an LFO (…chains) or an LFO behind a voice
   (…waves) paints non-background pixels on its canvas within 2 s of the page being up, keeps changing while Trigger is Free, and stops changing on Freeze. */
const fs = require('fs');
const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const P = (id, type, x, y, params = {}) => ({ id, type, scope: 'mono', pos: [x, y], collapsed: false, params });
const C = (a, ap, b, bp) => ({ from: [a, ap], to: [b, bp], bend: 0 });
const SPEC = {
  chains: { key: 'chains.patch', sk: 'chains.settings', mods: [P('i', 'trackIn', 0, 0, { track: 1 }), P('o', 'chainOut', 300, 0, { track: 1 }), P('l', 'lfo', 0, 300, { rate: 7.3, shape: 0, depth: 1 }), P('q', 'lfo', 0, 560, { rate: 2, shape: 1, depth: 1 }), P('s', 'scope', 330, 300, { time: .5, trig: 0 })],
    cab: [C('i', 'out', 'o', 'in'), C('l', 'out', 's', 'a'), C('q', 'out', 's', 'b')] },
  waves: { key: 'waves.patch', sk: 'waves.settings', mods: [P('h', 'hostIn', 0, 0), P('v', 'vco', 300, 0, { wave: 1 }), P('o', 'out', 600, 0, { slot: 1, level: 1 }), P('l', 'lfo', 0, 300, { rate: 7.3, shape: 0, depth: 1 }), P('q', 'lfo', 0, 560, { rate: 2, shape: 1, depth: 1 }), P('s', 'scope', 330, 300, { time: .5, trig: 0 })],
    cab: [C('h', 'pitch', 'v', 'pitch'), C('v', 'out', 'o', 'in'), C('l', 'out', 's', 'a'), C('q', 'out', 's', 'b')] },
};
test('the Scope paints in a real page, keeps moving on Free, and Freeze stops it (both editors)', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1 (boots Chrome, about 20 s)' : !PW && 'no Playwright here', timeout: 180000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot } = await import('./sweep/ui-lib.mjs');
  const B = await boot({ width: 1440, height: 900 }); const { page } = B, base = 'http://127.0.0.1:' + B.port;
  try {
    for (const ed of ['chains', 'waves']) {
      const S = SPEC[ed], doc = { meta: { name: 'scope test' }, view: { x: 20, y: 20, zoom: 1, ui: 2, nw: 1 }, modules: S.mods, cables: S.cab, groups: [], custom: [], transport: { tempo: 120 } };
      const url = base + '/' + ed + '/index.html'; await page.goto(url);
      await page.evaluate(([k, sk, d]) => { localStorage.clear(); localStorage.setItem(k, JSON.stringify(d)); localStorage.setItem(sk, JSON.stringify({ tips: false })); }, [S.key, S.sk, doc]);
      await page.goto(url); await page.waitForSelector('.node[data-id="s"] canvas.scopecv', { timeout: 20000 });
      const snap = () => page.evaluate(() => { const c = document.querySelector('.node[data-id="s"] canvas.scopecv'), g = c.getContext('2d'), d = g.getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 4) if (d[i] > 0) n++; return { n, url: c.toDataURL(), w: c.clientWidth, h: c.clientHeight }; });
      // a note makes …waves' own engine sound (the LFO is a control; the engine only needs to be running)
      if (ed === 'waves') { await page.mouse.click(700, 700); await page.keyboard.press('z'); }
      let s, t0 = Date.now(); do { await page.waitForTimeout(150); s = await snap(); } while (Date.now() - t0 < 2000 && s.n < 600);
      assert.ok(s.w >= 250 && s.h >= 100, ed + ': the canvas has a size ' + s.w + 'x' + s.h);
      // centre line + the two ±1 lines alone are about 3 x width pixels; the traces add far more
      assert.ok(s.n > 3 * s.w * 2 * 1.2, ed + ': the traces are painted, not only the grid (' + s.n + ' pixels)');
      const a = await snap(); let b = a; for (const t1 = Date.now(); Date.now() - t1 < 2500 && b.url === a.url;){ await page.waitForTimeout(150); b = await snap(); }   // a 500 ms sweep plus the telemetry's delay: wait for a new picture, not a fixed time
      assert.notEqual(a.url, b.url, ed + ': the picture moves while Trigger is Free');
      await page.evaluate(() => { const r = [...document.querySelectorAll('.node[data-id="s"] .row')].find(x => /Freeze/.test(x.textContent)); const sel = r && r.querySelector('select'); sel.value = '1'; sel.dispatchEvent(new Event('change', { bubbles: true })); });
      await page.waitForTimeout(700); const f1 = await snap(); await page.waitForTimeout(600); const f2 = await snap();
      assert.equal(f1.url, f2.url, ed + ': Freeze keeps the picture'); assert.ok(f2.n > 600);
    }
  } finally { await B.close(); }
});
