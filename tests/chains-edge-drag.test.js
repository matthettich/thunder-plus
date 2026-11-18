// EDGE DRAG (owner, 17 Nov): "if you hold a module and drag down to the edge of the screen, the module scrolls up and the screen down."
// While a title-bar drag sits in the 36 px edge zone the view pans (autoPanStep) and tells the drag how far (nodeDragging(v)). The
// drag keeps the pointer's distance from where it started in `extra`; the view moving by v on screen must ADD v to it (it was
// subtracted, so the module ran away from the pointer at twice the pan speed). Pinned as text in both editors (the Waves editor keeps
// its own copy of this code), plus an opt-in run in a real page (TP_BROWSER_TESTS=1): drag a module to each edge, hold, and the
// module's title bar stays where the pointer left it (within the 10 px grid snap and 2 px), the pan stops when the pointer leaves
// the zone, and one Undo gives the start position back.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), http = require('http');
const H = require('./helpers');

for (const f of ['chains/index.html', 'waves/index.html']) {
  const src = H.read(f);
  test(f + ': the edge pan moves the dragged module with the view (extra grows by v), and autosave waits for the drop', () => {
    assert.match(src, /nodeDragging = v => \{ extra\[0\] \+= v\[0\]; extra\[1\] \+= v\[1\]; place\(\); \};/, 'the pan compensation lost its sign');
    assert.doesNotMatch(src, /extra\[0\] -= v\[0\]/);
    assert.match(src, /const z = view\(\)\.zoom, dx = \(lastEv\.clientX - sx - extra\[0\]\) \/ z, dy = \(lastEv\.clientY - sy - extra\[1\]\) \/ z;/, 'place() no longer divides by the zoom');
    assert.match(src, /view\(\)\.x \+= autoPanV\[0\]; view\(\)\.y \+= autoPanV\[1\]; applyPan\(\);\n  if \(nodeDragging\) nodeDragging\(autoPanV\);/, 'the pan and the drag are no longer told together');
    assert.match(src, /clearTimeout\(store\._timer\);   \/\/ no autosave in the middle of a drag/, 'a drag held 1.2 s hands a half-dragged state to the song (a second Undo step)');
  });
  test(f + ': the pan direction and the compensation cancel (pointer at the bottom: view goes up, module goes down in the patch)', () => {
    // autoPanAt: at the bottom edge the y part is negative (the view moves up). Replay the maths of place() with the fixed sign.
    const zoom = 0.5, v = [0, -14]; let extra = [0, 0], viewY = 100; const sy = 300, py = 400, start = 50;   // pointer at 400, grabbed at 300
    const screenY = () => viewY + (start + (py - sy - extra[1]) / zoom) * zoom;
    const before = screenY();
    for (let i = 0; i < 20; i++) { viewY += v[1]; extra[1] += v[1]; }
    assert.ok(Math.abs(screenY() - before) < 1e-9, 'the module must stay at the same place on the screen while the view pans');
  });
}

const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page: a module dragged to each edge of the Chains editor stays under the pointer while the view pans', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 240000 }, async () => {
  const { chromium } = await import(PW);
  const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    fs.readFile(path.join(H.ROOT, path.normalize(p)), (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': p.endsWith('.html') ? 'text/html' : p.endsWith('.js') ? 'text/javascript' : p.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream' }); r.end(d); }); });
  await new Promise(ok => srv.listen(0, ok));
  const b = await chromium.launch({ executablePath: ['/usr/bin/google-chrome'].find(f => fs.existsSync(f)), args: ['--autoplay-policy=no-user-gesture-required'] });
  try {
    const song = JSON.parse(H.read('sounds/BETA-TESTING-ELECTRO.thunder')), P = song.song.chainPatch;
    const M = P.modules.find(m => m.type !== 'comment' && !m.collapsed && m.pos && m.type !== 'trackIn');
    const c = await b.newContext({ viewport: { width: 1175, height: 760 } }), page = await c.newPage();
    await page.addInitScript(() => { localStorage.setItem('thunderplus-platform-v1', 'desktop'); localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); });
    await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(2500);
    const FR = () => page.frames().find(f => /chains/.test(f.url()));
    const load = async () => { await page.evaluate(async t => { await window.__seqTest.ev('(async()=>{await loadProjText(' + JSON.stringify(t) + ', "x.thunder")})()'); }, JSON.stringify(song)); await page.waitForTimeout(1500); await page.evaluate(() => window.__seqTest.ev("setView('chains')")); await page.waitForTimeout(3000); };
    const wsOf = () => FR().evaluate(() => { const r = document.getElementById('ws').getBoundingClientRect(); return [r.left, r.top, r.right, r.bottom]; });
    for (const zoom of [0.35, 1, 2]) for (const edge of ['bottom', 'top', 'left', 'right']) {
      P.view = { x: 0, y: 0, zoom }; await load(); const w0 = await wsOf();
      P.view = { x: (w0[2] - w0[0]) / 2 - (M.pos[0] + 125) * zoom, y: (w0[3] - w0[1]) / 2 - (M.pos[1] + 20) * zoom, zoom }; await load();
      const fr = FR(), off = await (await fr.frameElement()).boundingBox(), ws = await wsOf();
      await fr.evaluate(id => { window.__rd = () => { const n = document.querySelector('.node[data-id="' + id + '"]'), h = n.querySelector('header').getBoundingClientRect(), t = document.getElementById('world').style.transform; return { x: h.left, y: h.top, w: h.height, px: parseFloat(n.style.left), py: parseFloat(n.style.top), t }; }; }, M.id);
      const s0 = await fr.evaluate(() => window.__rd()), gx = s0.x + 60, gy = s0.y + s0.w / 2, T = 3;
      const tg = { bottom: [gx, ws[3] - T], top: [gx, ws[1] + T], left: [ws[0] + T, gy], right: [ws[2] - T, gy] }[edge], inw = { bottom: [0, -80], top: [0, 80], left: [80, 0], right: [-80, 0] }[edge];
      const mv = (x, y) => page.mouse.move(x + off.x, y + off.y);
      await mv(gx, gy); await page.mouse.down();
      for (let i = 1; i <= 25; i++) { await mv(gx + (tg[0] - gx) * i / 25, gy + (tg[1] - gy) * i / 25); await page.waitForTimeout(16); }
      let worst = 0; const t0 = await fr.evaluate(() => window.__rd().t);
      for (let k = 0; k < 8; k++) { await page.waitForTimeout(200); const r = await fr.evaluate(() => window.__rd());
        worst = Math.max(worst, Math.abs(r.x - tg[0] - (s0.x - gx)), Math.abs(r.y - tg[1] - (s0.y - gy))); }
      const t1 = await fr.evaluate(() => window.__rd().t);
      assert.notEqual(t1, t0, `${edge} ${zoom}: the view did not pan`);
      assert.ok(worst <= 5 * zoom + 2, `${edge} ${zoom}: the module drifted ${worst.toFixed(1)} px from the pointer (limit: half a 10 px grid step + 2)`);
      await mv(tg[0] + inw[0], tg[1] + inw[1]); await page.waitForTimeout(300);
      const a = await fr.evaluate(() => window.__rd().t); await page.waitForTimeout(400); assert.equal(await fr.evaluate(() => window.__rd().t), a, `${edge} ${zoom}: the pan went on after the pointer left the edge zone`);
      await page.mouse.up(); await page.waitForTimeout(300);
      await page.keyboard.press('Control+z'); await page.waitForTimeout(500);
      const u = await fr.evaluate(() => window.__rd()); assert.deepEqual([u.px, u.py], [s0.px, s0.py], `${edge} ${zoom}: one Undo did not bring the module back to where it started`);
    }
  } finally { await b.close(); srv.close(); }
});
