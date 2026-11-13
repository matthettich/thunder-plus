// TO TOP button (13 Nov, build 577): the corner button in Arrange's header (and the classic Song grid's corner), in every layout that shows them.
//   node tests/sweep/to-top.mjs        screenshots go to /tmp/to-top/ (or $OUT)
// Checks per layout: the button is there, at least ~36 px to tap, does not shift the track columns or change the header's height, sits over the
// empty corner (no other control under its middle, none overlapped by it), and one click: song.y = 0 on the same track, song.top = 0, pg cleared, the
// holds (workT, arrActT, arrHandT, navT) are now, the grid scrolled to the top, play / stop never called, and it works while playing.
import fs from 'fs'; import path from 'path';
import { boot, app, Table, takeErrors, ROOT } from './ui-lib.mjs';
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
const OUTD = process.env.OUT || '/tmp/to-top'; fs.mkdirSync(OUTD, { recursive: true });
const T = new Table('to-top');
const LAYOUTS = [
  { n: 'unfolded-song-arrange', w: 884, h: 1020, screen: 'song', btn: '#tArrTop' },
  { n: 'unfolded-phrase-beside', w: 884, h: 1020, screen: 'phrase', btn: '#tArrTop' },
  { n: 'unfolded-phrase-beside-keys', w: 884, h: 1020, screen: 'phrase', keys: true, btn: '#tArrTop' },
  { n: 'unfolded-mixer-beside', w: 884, h: 1020, screen: 'mixer', btn: '#tArrTop' },
  { n: 'unfolded-classic-song', w: 884, h: 1020, screen: 'song', classic: true, btn: '#tGridTop' },
  { n: 'folded-song-arrange', w: 412, h: 915, screen: 'song', btn: '#tArrTop' },
  { n: 'folded-classic-song', w: 412, h: 915, screen: 'song', classic: true, btn: '#tGridTop', noArr: true },
  { n: 'folded-phrase-no-arrange', w: 412, h: 915, screen: 'phrase', none: true },
  { n: 'desktop-phrase-beside', w: 1600, h: 1000, screen: 'phrase', desktop: true, btn: '#tArrTop' },
];
const SKINS = ['idm', 'idmpink', 'idmpg', 'tr909', 'lowpower', 'lowpowermustard', 'lowpowerpink'];
for (const L of LAYOUTS) {
  const B = await boot({ width: L.w, height: L.h, storage: { 'thunderplus-platform-v1': L.desktop ? 'desktop' : 'mobile' }, mobile: !L.desktop });
  const { page, errors } = B; const id = s => `${L.n}: ${s}`;
  try {
    await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(400);
    await app(page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, $('toast').hidden = true, 1)`);
    if (L.classic) await app(page, 'setSongGrid(true)');
    await app(page, `go('${L.screen}')`); await page.waitForTimeout(400);
    if (L.keys) { await app(page, `apFocus('arr'), draw(), 1`); await page.waitForTimeout(200); }
    if (L.none) { const vis = await app(page, `!!($('tArrTop').offsetWidth || $('tArr').offsetWidth)`); T.add(id('Arrange not shown, so no button'), !vis, ''); continue; }
    if (L.noArr) { const vis = await app(page, `!!$('tArrTop').offsetWidth`); T.add(id('Arrange panel not shown here'), !vis, ''); }
    const sel = L.btn;
    const g = await page.evaluate(s => { const b = document.querySelector(s); if (!b || b.hidden) return null; const r = b.getBoundingClientRect(); return r.width ? { x: r.left, y: r.top, w: r.width, h: r.height, label: b.getAttribute('aria-label') || '', title: b.title } : null; }, sel);
    if (!g) { T.add(id('button shown'), false, 'missing'); continue; }
    T.add(id('button shown, labelled "Top of the song"'), g.label === 'Top of the song' && g.title === 'Top of the song', `${Math.round(g.w)}x${Math.round(g.h)} at ${Math.round(g.x)},${Math.round(g.y)}`);
    T.add(id('target >= 36 px high, >= 26 wide'), g.h >= 35.5 && g.w >= 25.5, `${g.w}x${g.h}`);
    // nothing else under the button's centre / corners, and no other control overlapped
    const hit = await page.evaluate(s => { const b = document.querySelector(s), r = b.getBoundingClientRect(); const out = [];
      for (const [fx, fy] of [[.5, .5], [.2, .2], [.8, .2], [.2, .8], [.8, .8]]) { const e = document.elementFromPoint(r.left + r.width * fx, r.top + r.height * fy); out.push(!e || e === b || b.contains(e) ? 'ok' : (e.id || e.className || e.tagName)); }
      const over = [...document.querySelectorAll('button, [data-t], .rn, [data-ry]')].filter(e => e !== b && !b.contains(e) && e.offsetWidth && !e.closest('[hidden]')).filter(e => { const q = e.getBoundingClientRect(); return Math.min(q.right, r.right) - Math.max(q.left, r.left) > 1 && Math.min(q.bottom, r.bottom) - Math.max(q.top, r.top) > 1; }).map(e => e.id || e.className || e.tagName).slice(0, 4);
      return { out, over }; }, sel);
    T.add(id('the button covers nothing else (5 points hit it; no control overlaps it)'), hit.out.every(x => x === 'ok') && !hit.over.length, JSON.stringify(hit));
    // geometry that must not change: header heights and the track columns, with the button and without it
    const geo = () => page.evaluate(() => { const r = s => { const e = document.querySelector(s); if (!e || !e.offsetWidth) return null; const q = e.getBoundingClientRect(); return [q.left, q.top, q.width, q.height].map(v => +v.toFixed(2)); };
      const th = [...document.querySelectorAll('#tArrTh i')].slice(0, 3).map(e => { const q = e.getBoundingClientRect(); return +q.left.toFixed(2); });
      return JSON.stringify([r('.tahd'), r('#tArrGp'), r('#tArrTh'), r('#tArrGrid'), th]); });
    if (!L.classic || !L.noArr) {
      const a = await geo(); await page.addStyleTag({ content: '#tArrTop,.tatop{display:none!important}' }); const b = await geo();
      T.add(id('header heights and track columns unchanged by the button'), a === b, a === b ? '' : a + ' vs ' + b);
      await page.evaluate(() => { for (const s of document.querySelectorAll('style')) if (s.textContent === '#tArrTop,.tatop{display:none!important}') s.remove(); });
    }
    // sticky: scrolling the rows leaves it in place
    await app(page, `($('tArrGrid') && ($('tArrGrid').scrollTop = 200), 1)`); await page.waitForTimeout(150);
    const g2 = await page.evaluate(s => { const r = document.querySelector(s).getBoundingClientRect(); return [r.left, r.top]; }, sel);
    T.add(id('does not scroll away with the rows'), Math.abs(g2[0] - g.x) < .6 && Math.abs(g2[1] - g.y) < .6, '');
    // the effect of one tap, parked and playing
    await app(page, `(window.__tt = {start: 0, stop: 0}, window.__os = tStart, window.__ot = tStop, tStart = function(){ __tt.start++; return __os.apply(this, arguments); }, tStop = function(){ __tt.stop++; __tt.stk = new Error().stack.replace(/\\s+at /g, ' < ').slice(0, 400); return __ot.apply(this, arguments); }, 1)`);
    for (const playing of [false, true]) {
      if (playing) { await app(page, `(window.__os('song', {row: 20}), 1)`); await page.waitForTimeout(500); }
      await app(page, `(__tt.start = __tt.stop = 0, cTrack = 5, cs.song.x = 5, cs.song.y = 30, cs.song.top = 24, cs.song.pg = true, workT = arrActT = arrHandT = arrTouchT = navT = -1e9, draw(), 1)`); await page.waitForTimeout(200);
      const was = await app(page, `JSON.stringify({play: !!tp.on, tv})`), x0 = await app(page, 'cs.song.x');   // (Pattern view settles the cursor's track itself, so "same track" is the one it has before the tap)
      await page.evaluate(s => { const b = document.querySelector(s); const r = b.getBoundingClientRect(); window.__click = [r.left + r.width / 2, r.top + r.height / 2]; }, sel);
      const [cx, cy] = await page.evaluate(() => window.__click);
      const before = await app(page, 'performance.now()'); await page.mouse.click(cx, cy); await page.waitForTimeout(120);
      const st = JSON.parse(await app(page, `JSON.stringify({y: cs.song.y, x: cs.song.x, top: cs.song.top, pg: !!cs.song.pg, hold: [workT, arrActT, arrHandT, arrTouchT, navT].map(v => v >= ${before}), start: __tt.start, stop: __tt.stop, stk: __tt.stk, on: !!tp.on, sc: $('tArrGrid') ? Math.round($('tArrGrid').scrollTop) : null, vis: arrShown()})`));
      const w = playing ? 'playing' : 'stopped';
      T.add(id(`${w}: cursor row 000, same track`), st.y === 0 && st.x === x0, JSON.stringify(st));
      T.add(id(`${w}: top 0, pg cleared, grid scrolled to the top`), st.top === 0 && !st.pg && (st.sc == null || !st.vis || st.sc <= 2), JSON.stringify(st));
      T.add(id(`${w}: the four hold clocks are now`), st.hold.every(Boolean), JSON.stringify(st.hold));
      T.add(id(`${w}: never starts or stops playback (state kept)`), st.on === playing && st.start === 0 && st.stop === 0, `${was} -> on ${st.on}`);
      if (playing) {   // the playhead does not pull the view straight back for a while
        await page.waitForTimeout(1200);
        const after = JSON.parse(await app(page, `JSON.stringify({top: cs.song.top, y: cs.song.y})`));
        T.add(id('playing: still at the top 1.2 s later (the hold)'), after.top === 0 && after.y === 0, JSON.stringify(after));
        await app(page, `(window.__ot(), 1)`);
      }
    }
    // skins: shot in the layouts the owner asked for; the glyph's colour is the header's quiet ink, and nothing blue / cyan in the pink and mustard ones
    if (['unfolded-song-arrange', 'folded-song-arrange', 'unfolded-phrase-beside', 'unfolded-classic-song'].includes(L.n)) for (const sk of SKINS) {
      await app(page, `(applySkin('${sk}'), $('toast').hidden = true, draw(), 1)`); await page.waitForTimeout(900);
      const col = await page.evaluate(s => { const b = document.querySelector(s); return getComputedStyle(b).color + ' | ' + getComputedStyle(b).backgroundColor; }, sel);
      if (['idm', 'lowpower'].includes(sk)) await page.screenshot({ path: path.join(OUTD, `${L.n}-${sk}.png`) });
      const [r, gg, bb] = (col.match(/\d+(\.\d+)?/g) || []).map(Number);
      const coolish = bb > r + 40 && bb > gg + 10;   // a blue / cyan / purple cast
      T.add(id(`skin ${sk}: glyph ${col.split('|')[0].trim()}, resting background clear`), /rgba\(0, 0, 0, 0\)|transparent/.test(col.split('|')[1]) && !((sk === 'idmpink' || sk === 'idmpg' || sk === 'lowpowerpink' || sk === 'lowpowermustard') && coolish), col);
    }
    const errs = takeErrors(errors); T.add(id('no page errors'), !errs.length, errs.join(' / ').slice(0, 200));
  } catch (e) { T.add(id('threw'), false, e.message.split('\n')[0]); }
  await B.close();
}
const f = T.print(); process.exit(f ? 1 : 0);
