// ARRANGE JUMP AND BACK (14 Nov, owner: "a command to quickly jump to the arrange panel and then back to wherever you were editing").
// Alt/⌥+A (arrKeyToggle) gives Arrange the keys and arrBack remembers where you were; Alt+A again or Esc puts it all back. The
// functions are cut out of index.html and run in a vm with small stand-ins for the app's state; the wiring is pinned as text; the
// real-page run (real key events) is opt-in (TP_BROWSER_TESTS=1).
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, 'could not find ' + what + ': the code moved, this test needs updating'); return m[0]; };
const code = cut(/function arrSnap\(\)\{[\s\S]*?\nfunction arrKeyToggle\(\)\{[\s\S]*?\n\}\n/, 'arrSnap … arrKeyToggle');
const typingSrc = cut(/function typing\(e\)\{[^\n]*\n[^\n]*\n/, 'typing');

function rig(o = {}) {
  const log = [];
  const c = Object.assign({
    pendPlace: null, goNoNew: false, view: 'tracker', tv: 'phrase', arrMode: 1, apOn: true, apF: null, apLastKey: 'x', arrBack: null, room: true,
    cs: { song: { x: 2, y: 30, top: 25 }, sect: { x: 0, y: 0 }, phrase: { x: 4, y: 11, top: 6 }, inst: { x: 0, y: 0 }, mixer: { x: 1, y: 2 }, fx: { x: 0, y: 3 }, mod: { x: 0, y: 0 }, project: { x: 0, y: 0 } },
    cSect: 5, cPhrase: 9, cInst: 2, cTrack: 2, cFxT: 2, cFxS: 1, selA: { x: 4, y: 11, v: 'phrase' },
    grid: { scrollTop: 123, scrollLeft: 45 }, VNAME: { song: 'SONG', phrase: 'PHRASE', mixer: 'MIXER', fx: 'FX', inst: 'INST', sect: 'SECT', mod: 'MOD', project: 'PROJECT' },
    log, toast: m => log.push('toast:' + m), draw: () => log.push('draw'),
  }, o);
  Object.assign(c, {
    setArr(m) { c.arrMode = m; log.push('setArr' + m); },
    setView(v) { c.view = v; log.push('setView:' + v); },
    go(v) { c.tv = v; log.push('go:' + v + (c.goNoNew ? ':nonew' : '')); },
    apArrOk() { return c.apOn && c.view === 'tracker' && c.tv !== 'song' && c.arrMode > 0 && c.room; },
    apFocus(f) { if (f === c.apF) return; c.apF = f; c.selA = null; if (f === 'arr'){ c.cs.song.y = 12; c.cs.song.x = 5; c.cs.song.top = 0; } },
  }, o);
  vm.createContext(c);
  vm.runInContext(code.replace(/^let /gm, 'var '), c);
  vm.runInContext('this.arrKeyToggle = arrKeyToggle; this.arrBackGo = arrBackGo; this.arrBackLive = arrBackLive;', c);
  c.mine = () => JSON.stringify([c.view, c.tv, c.cs, c.cSect, c.cPhrase, c.cInst, c.cTrack, c.cFxT, c.cFxS, c.selA, c.pendPlace, c.arrMode, c.grid.scrollTop, c.grid.scrollLeft]);
  return c;
}
// what apAfterDraw does with arrBack (the line itself is pinned below)
const afterDraw = c => { if (c.arrBack && !c.arrBackLive()) c.arrBack = null; };

test('jump and back: screen, every cursor, selection, scroll and the phrase / track come back, whatever Arrange did meanwhile', () => {
  const c = rig(), before = c.mine();
  c.arrKeyToggle();
  assert.equal(c.apF, 'arr'); assert.equal(c.arrBack.kind, 'side'); assert.equal(c.selA, null, 'apFocus drops the selection while Arrange has the keys');
  // the user works in Arrange: the cursor moves, the phrase follows, the grid scrolls
  c.cs.song.y = 80; c.cs.song.x = 7; c.cs.song.top = 70; c.cPhrase = 40; c.cTrack = 7; c.cFxT = 7; c.grid.scrollTop = 0; afterDraw(c);
  assert.ok(c.arrBack, 'still live'); c.arrKeyToggle();
  assert.equal(c.apF, null); assert.equal(c.arrBack, null); assert.equal(c.mine(), before);
});
test('Esc with nothing selected in Arrange does the same (keysDown calls arrBackGo while arrBackLive)', () => {
  const c = rig(), before = c.mine(); c.arrKeyToggle(); c.cs.song.y = 99; c.cPhrase = 3;
  assert.ok(c.arrBackLive()); c.arrBackGo(); assert.equal(c.mine(), before);
  assert.match(src, /e\.key === 'Escape' && !xposeArm[^\n]*\(apF && !apSel\)[^\n]*if \(arrBackLive\(\)\) arrBackGo\(\); else \{ apFocus\(null\); draw\(\); \}/);
});
test('Mixer and FX: the screen, the track and the row you were on come back', () => {
  for (const v of ['mixer', 'fx']) { const c = rig({ tv: v, selA: null }), before = c.mine(); c.arrKeyToggle(); c.cs[v].y = 0; c.cs[v].x = 6; c.cTrack = 6; c.cFxT = 6; c.arrKeyToggle(); assert.equal(c.mine(), before, v); }
});
test('opening a block from Arrange ends the way back; the key then jumps to Arrange again from the new place', () => {
  const c = rig(); c.arrKeyToggle(); c.apF = null; c.tv = 'phrase'; c.cs.song.y = 80; c.cPhrase = 40; c.cs.phrase.y = 3; /* Enter / Alt+▶ on a block: go() ended the focus */ afterDraw(c);
  assert.equal(c.arrBack, null, 'no back any more');
  const here = c.mine(); c.arrKeyToggle(); assert.equal(c.apF, 'arr', 'a new jump'); assert.equal(c.arrBack.snap.cPhrase, 40, 'the new place is the one remembered');
  c.cs.song.y = 3; c.arrKeyToggle(); assert.equal(c.mine(), here);
  assert.match(src, /if \(arrBack && !arrBackLive\(\)\) arrBack = null;/, 'apAfterDraw drops a dead snapshot');
});
test('Arrange given the keys some other way (SHIFT+◀, a click) is not "back" territory: Alt+A hands them back as before', () => {
  const c = rig({ selA: null }); c.apF = 'arr'; const before = c.mine(); c.arrKeyToggle(); assert.equal(c.apF, null); assert.equal(c.arrBack, null); assert.equal(c.mine(), before);
  const d = rig(); d.arrKeyToggle(); d.apF = null; afterDraw(d); d.apF = 'arr'; /* SHIFT+◀ later */ assert.equal(d.arrBackLive(), false);
});
test('no room beside the screen: the Song screen opens, and back returns to the screen and its cursors', () => {
  const c = rig({ room: false }), before = c.mine(); c.arrKeyToggle();
  assert.equal(c.tv, 'song'); assert.equal(c.arrBack.kind, 'screen'); assert.ok(c.arrBackLive());
  c.cs.song.y = 200; afterDraw(c); assert.ok(c.arrBack); c.arrKeyToggle(); assert.equal(c.mine(), before);
  const d = rig({ room: false }); d.arrKeyToggle(); d.tv = 'phrase'; /* a tab or Enter: left the Song screen */ afterDraw(d); assert.equal(d.arrBack, null);
});
test('Arrange hidden (View ▾): it is shown when the window has room, and hidden again on the way back; no room: the Song screen', () => {
  const c = rig({ arrMode: 0 }), before = c.mine(); c.arrKeyToggle(); assert.equal(c.arrMode, 1); assert.equal(c.apF, 'arr'); c.arrKeyToggle(); assert.equal(c.arrMode, 0); assert.equal(c.mine(), before);
  const d = rig({ arrMode: 0, room: false }); d.arrKeyToggle(); assert.equal(d.arrMode, 0, 'put back'); assert.equal(d.tv, 'song');
});
test('from the Synth page: the tracker opens with Arrange, and back is the Synth page again', () => {
  const c = rig({ view: 'synth' }), before = c.mine(); c.arrKeyToggle(); assert.equal(c.view, 'tracker'); assert.equal(c.apF, 'arr'); c.arrKeyToggle(); assert.equal(c.view, 'synth'); assert.equal(c.mine(), before);
  const w = rig({ view: 'waves' }); w.arrKeyToggle(); assert.equal(w.view, 'waves'); assert.equal(w.apF, null);
});
test('already on the Song screen: nothing to jump to', () => { const c = rig({ tv: 'song' }); c.arrKeyToggle(); assert.equal(c.apF, null); assert.equal(c.arrBack, null); });

test('ignored while a text field has focus: the key handlers return on typing(e) first, and typing() is true for text fields', () => {
  const a = src.indexOf("document.addEventListener('keydown', e0 => {\n  const e = noAltGr(e0);"); assert.ok(a > 0);
  const body = src.slice(a, a + 1200); assert.ok(body.indexOf('if (typing(e)) return;') > 0 && body.indexOf('if (typing(e)) return;') < body.indexOf('globalKeys(e)'));
  const s = src.indexOf("if (view === 'tracker' || typing(e) || e.repeat) return;"); assert.ok(s > 0 && s < src.indexOf("e.code === 'KeyA' && view === 'synth'"));
  const c = vm.createContext({ window: {} }); vm.runInContext(typingSrc + '\nthis.typing = typing;', c);
  const el = tag => ({ target: { matches: sel => sel.split(',').some(x => x.startsWith(tag)), getClientRects: () => [1] } });
  assert.equal(c.typing(el('input')), true); assert.equal(c.typing(el('textarea')), true);
});
test('the Alt+A binding clashes with nothing: the only Alt+KeyA lines are the jump (globalKeys, the Synth page, the wrapper), and the Alt+arrow bindings are untouched', () => {
  const lines = src.split('\n').map((l, i) => [i + 1, l]).filter(([, l]) => /'KeyA'/.test(l) && /altKey/.test(l) && !/^\s*\/\//.test(l));
  assert.equal(lines.length, 3, 'globalKeys, the Synth page and the Alt+A-again wrapper: ' + lines.map(l => l[0]).join(','));
  assert.ok(lines.every(([, l]) => /arrKeyToggle|arrBackGo/.test(l)));
  assert.ok(!/PIANO_CODE = \{[^}]*KeyA/.test(src), 'KeyA is not a piano key');
  assert.match(src, /else if \(e\.altKey && \(arrow === 'up' \|\| arrow === 'down'\) && \(tv === 'phrase' \|\| tv === 'song'\)\)\{ if \(tv === 'phrase'\) optDir\(arrow, true\); else shiftAltArrow\(arrow\); \}\n\s+else if \(e\.altKey && \(arrow === 'up' \|\| arrow === 'down'\) && PATSTEP_V\.has\(tv\)\) otherPatStep\(arrow\);[^\n]*\n\s+else if \(e\.altKey\) nav\(arrow\);/, 'Alt+▲▼ keep their meaning on Phrase and Song');
  assert.match(src, /globalKeys = \(\.\.\.a\) => \{ const e = a\[0\];\n[^\n]*\n[^\n]*arrBackLive\(\)\)\{ arrBackGo\(\); return true; \}/, 'the second press runs outside apRun');
});
test('the help and README rows and the hint line say what the key does', () => {
  assert.match(src, /<dt>Alt \+ A<\/dt><dd>jump to Arrange[^<]*back to exactly where you were/);
  assert.match(src, /<kbd>A<\/kbd><\/td><td>Jump to Arrange from any screen[\s\S]{0,400}?back to exactly where you were/);
  assert.match(H.read('README.md'), /\*\*Alt\+A\*\* \(Option\+A on a Mac\) jumps to Arrange[^\n]*back to exactly where you were/);
  assert.match(src, /Alt\+A or Esc: back to exactly where you were/);
});


// ---- ⌥▲▼ on Mixer / FX / MOD / INST (14 Nov, owner: "persist across pages even if there is nothing to edit on the current page") ----
const stepSrc = cut(/const PATSTEP_V = [^\n]*\nfunction otherPatStep\(dir\)\{[\s\S]*?\n\}\n/, 'otherPatStep');
function srig(tv, o = {}) {
  const song = []; for (let y = 0; y < 256; y++) song.push(Array(33).fill(-1));
  song[4][2] = 7; song[9][2] = 8; song[9][3] = 1;
  const log = [];
  const c = Object.assign({ tv, NT: 31, MT: 32, patNavOn: true, navT: 0, performance: { now: () => 5 }, ntr: () => 8, S: { song }, selA: { v: 'x' }, apHost: 'phrase', apLastKey: 'k', apF: null, cTrack: 2, cFxT: 2,
    cs: { song: { x: 0, y: 4, top: 0, pg: true }, mixer: { x: 2, y: 0 } }, clamp: (v, a, b) => Math.max(a, Math.min(b, v)), log, draw: () => log.push('draw:' + c.apHost), apArrOk: () => true,
    apTop: () => log.push('apTop'), apFollow: () => { log.push('follow:' + c.apHost); return true; },
    songGridDir(d) { const y = c.cs.song.y + (d === 'down' ? 1 : -1); c.cs.song.y = y; log.push('grid:' + d + ':' + c.cs.song.x); } }, o);
  vm.createContext(c); vm.runInContext(stepSrc.replace(/^const /gm, 'var '), c); vm.runInContext('this.step = otherPatStep; this.V = PATSTEP_V;', c); return c;
}
test('Alt+▲▼ on Mixer, FX, MOD and INST: the song position steps on this screen\'s track, the screen stays, Arrange and the followers update in the same redraw', () => {
  for (const v of ['mixer', 'fx', 'mod', 'inst']) {
    const c = srig(v); c.step('down');
    assert.equal(c.tv, v, v + ' stays'); assert.equal(c.cs.song.x, 2, v + ': this track'); assert.equal(c.cs.song.y, 5); assert.equal(c.selA, null); assert.equal(c.cs.song.pg, false);
    assert.deepEqual(c.log, ['grid:down:2', 'apTop', 'follow:' + v, 'draw:phrase'], v + ': Arrange paged, the screen followed, one draw'); assert.equal(c.apHost, 'phrase', 'apHost put back');
  }
  const m = srig('mixer', { cs: { song: { x: 0, y: 4, top: 0 }, mixer: { x: 6, y: 0 } } }); m.step('up'); assert.equal(m.cs.song.x, 6, 'the Mixer\'s own track');
  const f = srig('fx', { cFxT: 31 }); f.step('up'); assert.equal(f.cs.song.x, 32, 'the master effects: the master column');
});
test('PATTERN NAV off: 16 rows, as on the Song screen', () => { const c = srig('mixer', { patNavOn: false }); c.cs.song.y = 20; c.step('down'); assert.equal(c.cs.song.y, 36); c.step('up'); c.step('up'); assert.equal(c.cs.song.y, 4); });
test('wiring: KEYS-mode Alt+▲▼ on those screens run it; Alt+◀▶, Sect and Project still change screen; PAD mode keeps optDir; Project is reachable by tab, Alt+8 and Alt+◀▶', () => {
  assert.match(src, /else if \(e\.altKey && \(arrow === 'up' \|\| arrow === 'down'\) && PATSTEP_V\.has\(tv\)\) otherPatStep\(arrow\);[^\n]*\n\s+else if \(e\.altKey\) nav\(arrow\);/);
  const c = srig('mixer'); assert.deepEqual([...c.V].sort(), ['fx', 'inst', 'mixer', 'mod']);
  assert.match(src, /if \(e\.altKey && !mod\)\{ const a = \{ArrowUp:'up'[^\n]*optDir\(a, true\); return true; \}/, 'the PAD branch returns before: PAD mode ALT+▲▼ is optDir\'s, unchanged');
  assert.match(src, /VIEWS = \['song', 'sect', 'phrase', 'inst', 'mixer', 'fx', 'mod', 'project'\]/); assert.match(src, /e\.altKey && !mod && \/\^Digit\[1-8\]\$\/\.test\(e\.code\)\)\{ go\(VIEWS\[\+e\.code\.slice\(5\) - 1\]\)/, 'Alt+8 = Project');
  assert.match(src, /if \(dir === 'up'\) return tv === 'mixer' \? go\(midV\) : tv !== 'project' \? go\('project'\) : null;/, 'nav() itself is untouched (Project, PAD SHIFT+▲)');
  assert.match(H.read('README.md'), /Mixer, FX, MOD and Inst in KEYS mode: Alt\/⌥\+▲▼ step to the pattern above/);
});


// ---- CodeRabbit on PR #148 ----
test('(1) Alt+▲▼ on the Mixer with the master selected searches the master column', () => {
  const c = srig('mixer', { cs: { song: { x: 0, y: 4, top: 0 }, mixer: { x: 3, y: 7 } } }); c.step('down'); assert.equal(c.cs.song.x, 32);
  const d = srig('mixer', { cs: { song: { x: 0, y: 4, top: 0 }, mixer: { x: 3, y: 2 } } }); d.step('down'); assert.equal(d.cs.song.x, 3);
});
test('(2) pendPlace comes back with the snapshot, whatever an Arrange tap on an empty cell did to it', () => {
  const c = rig({ pendPlace: { x: 2, y: 30, p: 9 } }), before = c.mine(); c.arrKeyToggle();
  c.pendPlace = { x: 5, y: 77, p: 40 }; c.arrKeyToggle(); assert.equal(c.mine(), before); assert.equal(JSON.stringify(c.pendPlace), JSON.stringify({ x: 2, y: 30, p: 9 }));
  const d = rig(); d.arrKeyToggle(); d.pendPlace = { x: 1, y: 1, p: 3 }; d.arrKeyToggle(); assert.equal(d.pendPlace, null, 'none before, none after');
});
test('(3) coming back from the full-screen fallback opens the screen without making a block (goNoNew around go())', () => {
  const c = rig({ room: false }); c.arrKeyToggle(); c.log.length = 0; c.arrKeyToggle(); assert.ok(c.log.includes('go:phrase:nonew'), c.log.join()); assert.equal(c.goNoNew, false, 'put back');
});
test('(4) from the Synth page with Song as the tracker\'s last screen: the snapshot is kept and Alt+A goes back to the Synth page', () => {
  const c = rig({ view: 'synth', tv: 'song', selA: null }); c.arrKeyToggle(); assert.equal(c.view, 'tracker'); assert.ok(c.arrBack && c.arrBackLive()); c.arrKeyToggle(); assert.equal(c.view, 'synth'); assert.equal(c.arrBack, null);
  const t = rig({ tv: 'song', selA: null }); t.arrKeyToggle(); assert.equal(t.arrBack, null, 'on the tracker\'s Song screen already: nothing to do');
});
test('(5) Esc goes back from the full-screen fallback too (no apF), but not with a selection open', () => {
  assert.match(src, /e\.key === 'Escape' && !xposeArm[^\n]*\(\(apF && !apSel\) \|\| \(!apF && arrBack && arrBack\.kind === 'screen' && !selA && arrBackLive\(\)\)\)\)\{ if \(arrBackLive\(\)\) arrBackGo\(\)/);
});
test('(6) no stale sentence about Option+▲ changing screen outside Phrase in the README or the Help', () => {
  const R = H.read('README.md'); assert.ok(!/changes? screen elsewhere/.test(R)); assert.match(R, /step between patterns on Phrase, Song, Mixer, FX, MOD and Inst, and change screen on Sect and Project/);
  assert.ok(!/Change screen \(▲ Project, ▼ Mixer, one step at a time/.test(src)); assert.match(src, /▲▼ step to the pattern above \/ below on this track on Phrase, Song, Mixer, FX, MOD and Inst/);
});

const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs', '/opt/node-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
test('in a real page, real key events: Alt+A jumps, Alt+A / Esc come back; a block opened in between cancels the back', { skip: !process.env.TP_BROWSER_TESTS ? 'opt-in: TP_BROWSER_TESTS=1' : false, timeout: 120000 }, async () => {
  process.env.PLAYWRIGHT = PW;
  const { boot, app, ROOT } = await import('./sweep/ui-lib.mjs');
  const ELECTRO = fs.readFileSync(require('path').join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
  const B = await boot({ width: 1600, height: 1000, storage: { 'thunderplus-platform-v1': 'desktop' } });
  const st = () => app(B.page, `JSON.stringify({view, tv, apF, song: cs.song, phrase: cs.phrase, mixer: cs.mixer, cPhrase, cTrack, sel: selA, back: !!arrBack})`).then(JSON.parse);
  try {
    await B.page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await B.page.waitForTimeout(500);
    await app(B.page, `(document.querySelectorAll('.ask').forEach(e => e.remove()), askOpen = null, setView('tracker'), go('phrase'), cs.phrase.y = 5, selA = {x:cs.phrase.x, y:5, v:'phrase'}, draw(), 1)`);
    const a = await st();
    await B.page.keyboard.press('Alt+KeyA'); await B.page.waitForTimeout(150);
    const j = await st(); assert.equal(j.apF, 'arr'); assert.equal(j.back, true);
    await B.page.keyboard.press('ArrowDown'); await B.page.keyboard.press('ArrowDown'); await B.page.keyboard.press('ArrowRight'); await B.page.waitForTimeout(100);
    await B.page.keyboard.press('Alt+KeyA'); await B.page.waitForTimeout(150);
    assert.deepEqual(await st(), a, 'Alt+A: exactly where it was');
    await B.page.keyboard.press('Alt+KeyA'); await B.page.keyboard.press('ArrowDown'); await B.page.keyboard.press('Escape'); await B.page.waitForTimeout(150);
    assert.deepEqual(await st(), a, 'Esc: exactly where it was');
    await B.page.keyboard.press('Alt+KeyA'); await B.page.keyboard.press('ArrowDown'); await B.page.keyboard.press('Alt+ArrowRight'); await B.page.waitForTimeout(200);
    const o = await st(); assert.equal(o.back, false, 'a block opened: no back'); assert.equal(o.apF, null);
    await B.page.keyboard.press('Alt+KeyA'); await B.page.waitForTimeout(150);
    assert.equal((await st()).apF, 'arr', 'jumps to Arrange again');
    await B.page.keyboard.press('Escape'); await app(B.page, `(typeof setInMode === 'function' ? setInMode('keys') : (inMode = 'keys'), go('mixer'), cs.mixer.x = 0, cs.song.x = 0, cs.song.y = 0, draw(), 1)`);
    for (const v of ['mixer', 'fx', 'mod', 'inst']) {
      await app(B.page, `(go('${v}'), cs.song.y = 0, draw(), 1)`); const y0 = await app(B.page, `cs.song.y`);
      await B.page.keyboard.press('Alt+ArrowDown'); await B.page.waitForTimeout(100);
      assert.equal(await app(B.page, `tv`), v, v + ' stays'); assert.ok(Number(await app(B.page, `cs.song.y`)) > Number(y0), v + ': the song position moved down');
    }
  } finally { await B.close(); }
});
