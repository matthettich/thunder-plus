// PATTERN VIEW PLAYHEAD, CHAINS FOLD, PATTERN ZOOM (owner, 17 Nov, build 640). Three rules pinned as text; the real-page run (his song, the
// pattern view in a 1408 x 800 window) is opt-in (TP_BROWSER_TESTS=1).
//  1. Pattern view has ONE playhead look: each track's columns inverted on the row that track plays, the open track included (its cursor cell keeps
//     its own colours, ringed), never a bar across all the tracks (.ln.plr is for the single-track screens), and stopped it stays where each track stopped.
//  2. A folded …CHAINS in the FX bar is only its vertical tab, as MOD's is (it used to keep a small chain diagram beside the tab).
//  3. FIT n ROWS and a stepwise text zoom for the pattern grid: whole-pixel row heights, a floor of 9 px, remembered per device.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), http = require('http');
const H = require('./helpers');
const src = H.read('index.html');

test('1. the playhead bar is per track: no whole-line bar in Pattern view, the open track has its bar, the stopped spot shows per track', () => {
  assert.match(src, /\.tscr \.tgrid\.pvon span\.pvpl:not\(\.cur\):not\(\.sel\):not\(#zz\)\{background:var\(--ts-ink\); color:var\(--ts-bg\)\}/);
  assert.match(src, /\.tscr \.tgrid\.pvon span\.pvpl\.cur:not\(#zz\)\{outline:2px solid var\(--ts-bg\)/, 'the cursor cell on a bar keeps its colours, ringed');
  assert.match(src, /if \(ln && !grid\.classList\.contains\('pvon'\)\)\{ ln\.classList\.add\('plr'\)/, 'litRows: the whole-line bar is not used in Pattern view');
  assert.match(src, /if \(tv === 'phrase' && pvActive\(\)\) pvPlayMark\(P\); else if \(pvLit\.size\) pvPlayMark\(null\);/, 'stopped, the bars stay where each track stopped (RESUME)');
  assert.ok(!/\.tgrid\.pvon span\.pvpl:not\(\.cur\):not\(\.sel\)\{background-image/.test(src), 'the old faint band is gone');
  // the single-track screens keep their line bar
  assert.match(src, /\.tscr \.tgrid \.ln\.plr\{background:var\(--ts-ink\)\}/);
});
test('2. a folded …CHAINS keeps no chain diagram: only its tab, like MOD', () => {
  const m = src.match(/function deckChnFold\(\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'deckChnFold moved');
  assert.ok(!/dkChnRail\(/.test(m[0]) && !/append\(/.test(m[0]), 'it builds nothing any more');
  assert.match(m[0], /r\.remove\(\)/);
  assert.ok(!/\.col\.hasrail/.test(src), 'the folded-rail CSS is gone');
  assert.match(src, /#tDeck\.fxbar #dkChn\.col\{flex:0 0 26px; width:26px; min-width:26px\}/);
});
test('3. pattern zoom: whole-pixel rows, floor 9 px, fit rows, keys that are free, remembered per device', () => {
  assert.match(src, /var PV_RH_MIN = 9, PV_RH_MAX = 30,/);
  assert.match(src, /\.tgrid\.pvrh \.ln\{line-height:var\(--pvrh\); height:var\(--pvrh\)\}/);
  assert.match(src, /JSON\.stringify\(\{m:pvMode, fx:pvFx, fold:pvFold, fit:pvFitOn, rh:pvRH\}\)/);
  assert.match(src, /e\.code === 'Digit0'\) && pvActive\(\)/); assert.match(src, /mod && e\.altKey && tv === 'phrase' && \(e\.code === 'Minus'/);
  assert.match(src, /data-pv="zout"[\s\S]*data-pv="zin"[\s\S]*data-pv="fit"/);
  assert.match(src, /l:'FIT PATTERN ROWS'/); assert.match(src, /l:'PATTERN TEXT'/);
  assert.match(src, /grid\.addEventListener\('wheel', e => \{\s*if \(!\(e\.ctrlKey \|\| \(MACK && e\.metaKey\)\) \|\| e\.altKey \|\| !pvActive\(\)\) return;/);
  // the keys are in the Help & keys table
  assert.match(src, /Ctrl\/⌘\+Alt\+0/);
});

const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page: bars per track (open track too, stopped too), …CHAINS folds to its tab, FIT 32 shows all 32 rows on whole pixels', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 180000 }, async () => {
  const { chromium } = await import(PW);
  const srv = http.createServer((q, r) => { let p = decodeURIComponent(new URL(q.url, 'http://x').pathname); if (p.endsWith('/')) p += 'index.html';
    fs.readFile(path.join(H.ROOT, path.normalize(p)), (e, d) => { if (e) { r.writeHead(404); return r.end(); } r.writeHead(200, { 'content-type': p.endsWith('.html') ? 'text/html' : p.endsWith('.js') ? 'text/javascript' : p.endsWith('.wasm') ? 'application/wasm' : 'application/octet-stream' }); r.end(d); }); });
  await new Promise(ok => srv.listen(0, ok));
  const b = await chromium.launch({ executablePath: ['/usr/bin/google-chrome'].find(f => fs.existsSync(f)), args: ['--autoplay-policy=no-user-gesture-required'] });
  try {
    const c = await b.newContext({ viewport: { width: 1408, height: 800 } }), page = await c.newPage();
    await page.addInitScript(() => { localStorage.setItem('thunderplus-platform-v1', 'desktop'); localStorage.setItem('thunderplus-welcome-v1', '1'); localStorage.setItem('thunderplus-seen-build', '9999'); localStorage.setItem('thunderplus-skin-v1', 'idmpink'); });
    await page.goto(`http://localhost:${srv.address().port}/index.html?seqtest=1`); await page.waitForFunction(() => window.__seqTest && window.__seqTest.ev('S.name') === 'bETA tESTING eLECTRO', null, { timeout: 30000 });
    await page.waitForTimeout(2500);
    const E = x => page.evaluate(t => window.__seqTest.ev(t), x);
    await page.evaluate(() => window.__seqTest.audio());
    await E(`(()=>{ tv='phrase'; pvGo(9,46,null,3); })()`); await page.waitForTimeout(700);
    const look = () => E(`JSON.stringify({plr:grid.querySelectorAll('.plr').length, tr:[...new Set([...grid.querySelectorAll('.pvpl')].map(e => (e.className.match(/pvt(\\d+)/) || [])[1]))].sort((a,b)=>a-b)})`).then(JSON.parse);
    // playing: every track on its own row, the open one (track 9) too
    await E(`(()=>{ const st=[15,15,9,3,20,15,22,4,15,15,15,15]; const pos=[]; for(let t=0;t<=MT;t++){ const cid=S.song[46][t]; const ph=cid>=0?firstPhrase(cid):-1; pos[t]=cid>=0?{row:46,phrase:ph,step:st[t%12],cr:0,sect:cid}:null; } tp.on=true; tp.mode='song'; tp.pos=pos; draw(); })()`); await page.waitForTimeout(700);
    let l = await look(); assert.equal(l.plr, 0, 'no whole-line bar'); assert.ok(l.tr.includes('9'), 'the open track has its bar'); assert.ok(l.tr.length >= 4, 'tracks have bars: ' + l.tr);
    // stopped: the same bars from RESUME's spot
    await E(`(()=>{ const pos=tp.pos; tp.on=false; tp.pos=null; tp.paused={pos,ref:9,row:46,step:15,key:curKey()}; draw(); })()`); await page.waitForTimeout(700);
    l = await look(); assert.equal(l.plr, 0); assert.ok(l.tr.includes('9') && l.tr.length >= 4, 'stopped bars: ' + l.tr);
    // …CHAINS folded: its tab only
    await E(`(()=>{ cTrack = 7; setDeck('mod', false); setDeck('chn', false); draw(); })()`); await page.waitForTimeout(700);
    const f = JSON.parse(await E(`JSON.stringify({rail:!!document.getElementById('dkChnRail'), w:Math.round(dkSec('chn').getBoundingClientRect().width), col:dkSec('chn').classList.contains('col')})`));
    assert.deepEqual(f, { rail: false, w: 26, col: true });
    // FIT 32 rows: whole-pixel rows, all 32 on screen
    await E(`(()=>{ tv='phrase'; pvGo(9,46,null,3); })()`); await page.waitForTimeout(700);
    await E(`pvFitToggle()`); await page.waitForTimeout(900);
    const z = JSON.parse(await E(`JSON.stringify({rh:pvRHnow, lnh:grid.querySelector('.ln').getBoundingClientRect().height, vr:pvVR(), rows:pvRows, short:pvFitShort, last:[...grid.querySelectorAll(':scope > .ln')].pop().getBoundingClientRect().bottom, bottom:grid.getBoundingClientRect().bottom})`));
    assert.equal(z.rows, 32); assert.equal(z.short, false); assert.ok(z.rh >= 9 && Number.isInteger(z.rh)); assert.equal(z.lnh, z.rh, 'a row is whole pixels'); assert.ok(z.vr >= 32 && z.last <= z.bottom + 0.5, 'all 32 rows show: ' + JSON.stringify(z));
    // zoom out one step turns fit off and is remembered
    await E(`pvZoomBy(-1)`); await page.waitForTimeout(500);
    const k = JSON.parse(await E(`JSON.stringify({fit:pvFitOn, rh:pvRH, saved:JSON.parse(localStorage.getItem(PVUK))})`));
    assert.equal(k.fit, false); assert.equal(k.rh, z.rh - 1); assert.equal(k.saved.rh, k.rh);
  } finally { await b.close(); srv.close(); }
});
