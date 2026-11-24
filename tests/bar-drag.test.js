// BAR DRAG (19 Nov, build 649; owner: "make sliders like the swing slider controllable by clicking on the slider and dragging").
// The Settings menu's and the settings sheets' sliders (every row tsetRow draws with a bar, SWING, TEMPO, LPB…) take a press, a drag
// and a double-tap through script (barDrag in index.html). The pure parts (sdSnap, sdValue, sdFineValue, sdFineOn, sdGesture, sdDouble)
// are cut out of the page and run here: position -> value, snapping, clamping, the gesture classifier fed synthetic pointer
// sequences. The run in a real page is opt-in (TP_BROWSER_TESTS=1): SWING dragged 50 -> 66 by mouse is one Undo step, a late row
// (MONO TRACKS) applies on release only, a double click puts a row back to its default, a finger that starts on a bar and swipes
// up scrolls the sheet and changes nothing, a tap sets the value, a sideways finger drag scrubs it.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), http = require('http');
const H = require('./helpers');
const src = H.read('index.html');
const cut = src.match(/const SD = \{[\s\S]*?\nlet sdLast = null;/);
assert.ok(cut, 'index.html: the BAR DRAG block (const SD … let sdLast) is gone');
const SD = new Function(cut[0].replace(/let sdLast = null;$/, '') + '; return {SD, sdSnap, sdFrac, sdValue, sdFineValue, sdFineOn, sdGesture, sdDouble};')();
const { sdSnap, sdValue, sdFineValue, sdFineOn, sdGesture, sdDouble } = SD, K = SD.SD;
const box = { left: 100, width: 209, top: 50, height: 30 };   // the thumb's centre runs 104.5 .. 304.5 (200 px)

test('position -> value: linear, the thumb stays under the pointer, ends clamp', () => {
  assert.equal(sdValue(104.5, box, 50, 75, 1), 50);
  assert.equal(sdValue(304.5, box, 50, 75, 1), 75);
  assert.equal(sdValue(204.5, box, 50, 75, 1), 63);            // half way: 62.5 rounds up to 63
  assert.equal(sdValue(0, box, 50, 75, 1), 50, 'left of the bar clamps to min');
  assert.equal(sdValue(5000, box, 50, 75, 1), 75, 'right of the bar clamps to max');
  assert.equal(sdValue(204.5, box, -100, 300, 5), 100, 'negative min, step 5: 100 is on the grid');
  assert.equal(sdValue(204.5, box, 0, 1, 0.01), 0.5);
  // the same maths as the browser's thumb: value v sits at left + 4.5 + (v - min)/(max - min) * 200
  for (const v of [50, 55, 60, 66, 70, 75]) assert.equal(sdValue(104.5 + (v - 50) / 25 * 200, box, 50, 75, 1), v);
});

test('a row whose bar is not linear in the value: the bar is the input\'s own range, so the position maps through it (an index row)', () => {
  // ARRANGE LOOK-AHEAD: the value is an index 0..5 into [4, 8, 12, 16, 24, 32]; the bar is drawn over 0..5, the pointer maps over 0..5
  const idx = x => sdValue(x, box, 0, 5, 1);
  assert.deepEqual([104.5, 144.5, 184.5, 224.5, 264.5, 304.5].map(idx), [0, 1, 2, 3, 4, 5]);
});

test('snapping: the step grid from min, a max that is not on it, rounding noise', () => {
  assert.equal(sdSnap(63.4, 50, 75, 1), 63);
  assert.equal(sdSnap(66, 60, 200, 10), 70, '66 -> 70 (grid 60, 70, 80…)');
  assert.equal(sdSnap(200, 60, 195, 10), 190, 'a max off the grid: the last step inside it, as the browser does');
  assert.equal(sdSnap(0.30000000000000004, 0, 1, 0.1), 0.3);
  assert.equal(sdSnap(7, 5, 5, 1), 5, 'min = max');
  assert.equal(sdSnap(7, 0, 10, 0), 7, 'a step of 0 reads as 1');
  assert.equal(sdSnap(-3, 0, 10, 1), 0); assert.equal(sdSnap(99, 0, 10, 1), 10);
});

test('fine scrub: a tenth of the travel from the anchor, and it clamps', () => {
  const a = sdFineValue(204.5 + 100, 204.5, 63, box, 50, 75, 1);   // 100 px of 200 = half the range, a tenth of it = 1.25
  assert.ok(Math.abs(a.raw - 64.25) < 1e-9); assert.equal(a.v, 64);
  assert.equal(sdFineValue(-5000, 204.5, 63, box, 50, 75, 1).v, 50);
  assert.equal(sdFineValue(204.5, 204.5, 63.4, box, 50, 75, 1).raw, 63.4, 'no travel, no change (no jump into fine mode)');
  assert.equal(sdFineOn(false, true, 65, box), true, 'Shift on a mouse');
  assert.equal(sdFineOn(false, false, 5000, box), false, 'a mouse far above the bar is not fine');
  assert.equal(sdFineOn(true, false, 65, box), false, 'a finger on the bar');
  assert.equal(sdFineOn(true, false, 65 + 61, box), true, 'a finger more than 60 px from the bar');
  assert.equal(sdFineOn(true, false, 65 + 59, box), false);
});

const seq = (touch, pts, hold) => { const g = sdGesture(touch, pts[0][0], pts[0][1], 0); let s = g.st; for (const [x, y] of pts.slice(1)) s = g.move(x, y); if (hold) g.hold(hold); return { g, s, up: g.up() }; };
test('gesture: a mouse press is a drag at once; a still click is a tap', () => {
  assert.equal(sdGesture(false, 10, 10, 0).st, 'drag');
  assert.equal(seq(false, [[10, 10], [11, 10]]).up, 'tap');
  assert.equal(seq(false, [[10, 10], [60, 12]]).up, 'drag');
});
test('gesture: a finger: tap, sideways drag, vertical swipe (scroll), diagonals, press and hold', () => {
  assert.equal(seq(true, [[50, 50]]).up, 'tap', 'a tap');
  assert.equal(seq(true, [[50, 50], [53, 52], [52, 51]]).up, 'tap', 'a tap with a little jitter (under 8 px)');
  assert.equal(seq(true, [[50, 50], [56, 50], [70, 52]]).s, 'drag', 'sideways');
  assert.equal(seq(true, [[50, 50], [52, 60], [53, 80]]).s, 'scroll', 'a swipe up or down starting on the bar');
  assert.equal(seq(true, [[50, 50], [50, 20]]).up, 'scroll');
  assert.equal(seq(true, [[50, 50], [62, 58]]).s, 'drag', 'a diagonal more across than down is a drag');
  assert.equal(seq(true, [[50, 50], [58, 62]]).s, 'scroll', 'a diagonal more down than across scrolls');
  assert.equal(seq(true, [[50, 50], [58, 58]]).s, 'drag', 'exactly 45 degrees: a drag');
  // once decided it stays decided: a drag that wanders up and down is still a drag, a scroll that wanders sideways still scrolls
  assert.equal(seq(true, [[50, 50], [70, 50], [70, 200]]).s, 'drag');
  assert.equal(seq(true, [[50, 50], [50, 70], [200, 72]]).s, 'scroll');
  const h = seq(true, [[50, 50]], K.HOLD - 1); assert.equal(h.s, 'wait'); assert.equal(h.up, 'tap', 'let go just before the hold time: a tap');
  const h2 = seq(true, [[50, 50]], K.HOLD); assert.equal(h2.g.st, 'drag'); assert.equal(h2.up, 'drag', 'a still press of HOLD ms is a drag');
  const g = sdGesture(true, 50, 50, 0); g.move(50, 80); assert.equal(g.hold(1000), 'scroll', 'a hold after the swipe began changes nothing');
});
test('double tap: the same bar, the same spot, inside DBL_MS', () => {
  const r = {}, last = { r, t: 1000, x: 50, y: 50 };
  assert.equal(sdDouble(last, r, 1000 + K.DBL_MS - 1, 55, 52), true);
  assert.equal(sdDouble(last, r, 1000 + K.DBL_MS, 55, 52), false, 'too late');
  assert.equal(sdDouble(last, r, 1100, 50 + K.DBL_PX, 50), false, 'too far: a second tap to adjust, not to reset');
  assert.equal(sdDouble(last, {}, 1100, 50, 50), false, 'another bar');
  assert.equal(sdDouble(null, r, 1100, 50, 50), false);
});

test('source: the lock is gone, the bars take the pointer by script, vertical swipes keep scrolling', () => {
  assert.doesNotMatch(src, /Slider unlocked|function rlWire|function rlLockAll|class="unl"/);
  assert.match(src, /:is\(#gset,\.rlock\) input\[type=range\]\{pointer-events:none\}/, 'the browser must not take the press itself (it would jump the thumb on a scroll)');
  assert.match(src, /:is\(#gset,\.rlock\) :has\(> input\[type=range\]\)\{touch-action:pan-y\}/, 'pan-y: vertical swipes scroll, sideways ones are the script\'s');
  assert.match(src, /document\.addEventListener\('pointerdown', e => \{ if \(e\.button !== 0 \|\| !e\.isPrimary\) return; const r = sdAt\(e\); if \(r\) barDrag\(e, r\); \}, true\);/);
  assert.match(src, /rg\._reset = /, 'tsetRow gives the bar a default to go back to');
});

const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page: drag, tap, double-click, late row, finger scroll and finger drag', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 240000 }, async () => {
  const { chromium } = await import(PW);
  const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    fs.readFile(path.join(H.ROOT, path.normalize(p)), (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': p.endsWith('.html') ? 'text/html' : p.endsWith('.js') ? 'text/javascript' : p.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream' }); r.end(d); }); });
  await new Promise(ok => srv.listen(0, ok));
  const b = await chromium.launch({ executablePath: ['/usr/bin/google-chrome'].find(f => fs.existsSync(f)), args: ['--autoplay-policy=no-user-gesture-required'] });
  try {
    for (const touch of [false, true]) {
      const c = await b.newContext({ viewport: touch ? { width: 400, height: 330 } : { width: 1175, height: 760 }, hasTouch: touch, isMobile: touch }), page = await c.newPage();
      const errs = []; page.on('pageerror', e => errs.push(e.message));
      await page.addInitScript(`(() => { localStorage.setItem('thunderplus-platform-v1', '${touch ? 'mobile' : 'desktop'}'); localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); })()`);
      await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 30000 }); await page.waitForTimeout(2000);
      const ev = s => page.evaluate(x => window.__seqTest.ev(x), s);
      await ev("sheetGrp.song = 1; setSheet('song')"); await page.waitForTimeout(600);
      const bar = async lab => page.evaluate(l => { const r = [...document.querySelectorAll('.rlock input[type=range]')].find(x => x.getAttribute('aria-label') === l); r.scrollIntoView({ block: 'center' }); const q = r.getBoundingClientRect(); return { l: q.left, t: q.top, w: q.width, h: q.height }; }, lab);
      const outTxt = lab => page.evaluate(l => [...document.querySelectorAll('.rlock .tsr')].find(x => x.querySelector('.tsl') && x.querySelector('.tsl').textContent === l).querySelector('output').textContent, lab);
      const X = (q, v, lo, hi) => q.l + 4.5 + (v - lo) / (hi - lo) * (q.w - 9);
      const cdp = touch ? await c.newCDPSession(page) : null;
      const tch = async (type, x, y) => cdp.send('Input.dispatchTouchEvent', { type, touchPoints: type === 'touchEnd' ? [] : [{ x, y, id: 1 }] });
      const pt = async (q, v, fn) => fn(X(q, v, 50, 75), q.t + q.h / 2);
      let q = await bar('Swing'); const y = q.t + q.h / 2;
      assert.equal(await ev('S.swing'), 50); const past0 = await ev('undo.past.length');
      if (!touch) {
        await page.mouse.move(X(q, 50, 50, 75), y); await page.mouse.down();
        assert.equal(await ev('S.swing'), 50);
        for (let i = 1; i <= 12; i++) { await page.mouse.move(X(q, 50, 50, 75) + (X(q, 66, 50, 75) - X(q, 50, 50, 75)) * i / 12, y); await page.waitForTimeout(20); }
        await page.waitForTimeout(150);   // the song follows at most every 50 ms (SD.LIVE)
        assert.equal(await ev('S.swing'), 66, 'live while the button is down'); assert.equal(await outTxt('Swing'), '66%', 'the value text follows');
        assert.equal(await ev('undo.past.length'), past0, 'no Undo step while it is held');
        await page.mouse.up(); await page.waitForTimeout(100);
        assert.equal(await ev('S.swing'), 66); assert.equal(await ev('undo.past.length'), past0 + 1, 'one Undo step for the whole drag');
        await ev('undoStep(-1)'); assert.equal(await ev('S.swing'), 50, 'one Undo puts it back'); assert.equal(await ev('undo.past.length'), past0);
        // press on the bar sets the value there; Shift: a tenth of the travel
        await page.mouse.click(X(q, 70, 50, 75), y); assert.equal(await ev('S.swing'), 70);
        await page.waitForTimeout(500); await page.mouse.move(X(q, 70, 50, 75), y); await page.keyboard.down('Shift'); await page.mouse.down();
        await page.mouse.move(X(q, 70, 50, 75) + 40, y, { steps: 5 }); await page.mouse.up(); await page.keyboard.up('Shift');
        const sw = await ev('S.swing'); assert.ok([70, 71].includes(sw), 'got ' + sw + ': ' + 'Shift: 40 px of a 200 px bar is 5 of 25, a tenth is 0.5');
        // a double click puts it back to its default (50)
        await page.waitForTimeout(500); await page.mouse.dblclick(X(q, 60, 50, 75), y); await page.waitForTimeout(100);
        assert.equal(await ev('S.swing'), 50, 'double click: the default');
        // a late row: MONO TRACKS applies on release only
        await page.click('.rlock .pcat:first-of-type'); await page.waitForTimeout(300);
        const m = await bar('Mono tracks'), n0 = await ev('polyFrom()');
        await page.mouse.move(m.l + 6, m.t + m.h / 2); await page.mouse.down(); await page.mouse.move(m.l + m.w - 6, m.t + m.h / 2, { steps: 8 });
        assert.equal(await ev('polyFrom()'), n0, 'a late row does not apply while the button is down');
        await page.mouse.up(); await page.waitForTimeout(400);
        const dlg = await page.$('.ask, .askbox, [role=dialog]:not(.rlock)'); if (dlg) await page.keyboard.press('Escape');
      } else {
        // a finger: a tap sets the value
        await tch('touchStart', X(q, 62, 50, 75), y); await tch('touchEnd'); await page.waitForTimeout(150);
        assert.equal(await ev('S.swing'), 62, 'a tap sets the value at that point');
        // a swipe up that starts ON the bar scrolls the sheet and changes nothing
        q = await bar('Swing'); const sy0 = await page.evaluate(() => (() => { let e = [...document.querySelectorAll('.rlock input[type=range]')][0]; while (e && !(e.scrollHeight > e.clientHeight + 2 && /auto|scroll/.test(getComputedStyle(e).overflowY))) e = e.parentElement; return e ? e.scrollTop : -1; })());
        await tch('touchStart', X(q, 55, 50, 75), q.t + q.h / 2);
        for (let i = 1; i <= 12; i++) { await tch('touchMove', X(q, 55, 50, 75) + i, q.t + q.h / 2 - i * 12); await page.waitForTimeout(16); }
        await tch('touchEnd'); await page.waitForTimeout(300);
        assert.equal(await ev('S.swing'), 62, 'a vertical swipe from the bar must not change the value');
        const sy1 = await page.evaluate(() => (() => { let e = [...document.querySelectorAll('.rlock input[type=range]')][0]; while (e && !(e.scrollHeight > e.clientHeight + 2 && /auto|scroll/.test(getComputedStyle(e).overflowY))) e = e.parentElement; return e ? e.scrollTop : -1; })());
        assert.ok(sy1 > sy0 + 20, 'and the list scrolled (' + sy0 + ' -> ' + sy1 + ')');
        // a sideways drag scrubs, with one Undo step
        q = await bar('Swing'); const yy = q.t + q.h / 2, p0 = await ev('undo.past.length');
        await tch('touchStart', X(q, 60, 50, 75), yy);
        for (let i = 1; i <= 12; i++) { await tch('touchMove', X(q, 60, 50, 75) + (X(q, 70, 50, 75) - X(q, 60, 50, 75)) * i / 12, yy); await page.waitForTimeout(20); }
        await page.waitForTimeout(150);
        assert.equal(await ev('S.swing'), 70, 'a sideways drag follows the finger'); await tch('touchEnd'); await page.waitForTimeout(100);
        assert.equal(await ev('undo.past.length'), p0 + 1, 'one Undo step');
      }
      assert.deepEqual(errs, [], 'no page errors');
      await c.close();
    }
  } finally { await b.close(); srv.close(); }
});
