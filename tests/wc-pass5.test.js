// …waves / …chains pass 5, "Structure" (14 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: M1 module widths, the Mini collapsed state, M6 groups; and the six review fixes of pass 4).
// Source guards, no browser. The browser run is tests/sweep/wc-pass5.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path'), vm = require('vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ' review 1: the wheel keeps one pending change per slider', () => {
    assert.match(src, /const wheelChT = new Map\(\)/);
    assert.match(src, /function wheelFlush\(except\)/);
    assert.match(src, /wheelFlush\(sl\); clearTimeout\(wheelChT\.get\(sl\)\);/);
    assert.doesNotMatch(src, /wheelChT = setTimeout/);
  });
  test(ed + ' review 2: letter shortcuts follow the letter typed (e.key), the physical position only where Option changes the character', () => {
    assert.match(src, /const letterKey = e =>/);
    for (const re of [/ks === 'a'/, /ks === 'g'/, /ks === 'f'/, /ks === 'n'/, /letterKey\(e\) !== 'c'/, /letterKey\(e\) !== 'e'/]) assert.match(src, re);
    assert.doesNotMatch(src, /c === 'Key[AGFN]'/);
    assert.doesNotMatch(src, /e\.code !== 'Key[CE]'/);
    const f = /const letterKey = e => \{[^\n]*\};/.exec(src); assert.ok(f);
    const letterKey = vm.runInNewContext('(' + f[0].replace(/^const letterKey = /, '').replace(/;$/, '') + ')');
    assert.equal(letterKey({ key: 'a', code: 'KeyQ' }), 'a');   // AZERTY: the key printed A
    assert.equal(letterKey({ key: 'Q', code: 'KeyA' }), 'q');
    assert.equal(letterKey({ key: 'ç', code: 'KeyC', altKey: true }), 'c');   // Option+C on a Mac
    assert.equal(letterKey({ key: 'ç', code: 'KeyC', altKey: false }), '');
  });
  test(ed + ' review 3: a jack is an image with a label, not a button nothing can operate', () => {
    assert.match(src, /role:'img', 'aria-label': \(dir === 'in' \? 'Input ' : 'Output '\)/);
    assert.doesNotMatch(src, /role:'button', 'aria-label': \(dir === 'in'/);
  });
  test(ed + ' M1: widths come from widthOf (the definition\'s w, or MODULE_WIDTH), 150 to 600 px; nothing is saved per module', () => {
    assert.match(src, /const NODE_W = 250;/);
    assert.match(src, /function widthOf\(type\)\{ const d = MODULES\[type\], w = d && d\.w != null \? d\.w : MODULE_WIDTH\[type\]; return typeof w === 'number' && w >= 150 && w <= 600 \? Math\.round\(w\) : NODE_W; \}/);
    assert.match(src, /const w = widthOf\(m\.type\); if \(w !== NODE_W\)\{ n\.el\.classList\.add\('nwd'\); n\.el\.style\.setProperty\('--mw', w \+ 'px'\); if \(w < NODE_W\) n\.el\.classList\.add\('nar'\); \}/);
    assert.match(src, /\.node\.nwd\{width:var\(--mw\); min-width:min-content\}/);
    assert.match(src, /\.node\.nwd \.cs-t\{contain:inline-size\}/, 'the summary line must not set the least width');
    assert.match(src, /\.node\.nar \.row\.sl\{grid-template-columns:minmax\(0,1fr\) auto; grid-template-areas:"l v" "r r"/);
    assert.match(src, /\/ v\.zoom - widthOf\(type\) \/ 2 \+/, 'a new module is centred by its own width');
    assert.doesNotMatch(src, /m\.w\b\s*=|\.style\.width = widthOf/, 'no width written into the module');
  });
  test(ed + ' Mini: three states, one saved field (view:"mini" with collapsed:true), the title bar alone', () => {
    assert.match(src, /const viewOf = m => !m\.collapsed \? 'open' : m\.view === 'mini' \? 'mini' : 'compact';/);
    assert.match(src, /function setView\(n, v, quiet\)\{/);
    assert.match(src, /if \(v === 'mini'\) m\.view = 'mini'; else delete m\.view;/);
    assert.match(src, /function setCollapsed\(n, c, quiet\)\{ setView\(n, c \? \(viewOf\(n\.m\) === 'mini' \? 'mini' : 'compact'\) : 'open', quiet\); \}/);
    assert.match(src, /setView\(n, ev\.shiftKey \? \(v === 'mini' \? 'compact' : 'mini'\) : v === 'open' \? 'compact' : 'open'\)/);
    assert.match(src, /if \(m\.collapsed\) setView\(n, viewOf\(m\), true\);/, 'a patch with collapsed:true and no view (or an unknown one) opens as Compact');
    assert.match(src, /\.node\.collapsed\.mini > :not\(header\)\{display:none !important\}/);
    assert.match(src, /\.node\.collapsed\.mini > header \.mq\{display:none\}/);
    assert.match(src, /if \(!isFinger\(ev\) \|\| playMode\) return;/, 'a long press on the chevron is for a finger and never in Play');
    assert.match(src, /if \(playBlocked\(\)\) return; store\.snapshot\(\);\s*\n\s*const v = viewOf\(m\);/, 'the chevron does nothing in Play');
    assert.match(src, /function setAllMini\(ids\)/);
    assert.match(src, /if \(e\.shiftKey\)\{\s*\n\s*const k = setAllMini\(\) \|\| setAllCollapsed\(false\);/);
    assert.match(src, /'Mini all modules \(Alt\+Shift\+C\)'/);
    assert.match(src, /'About this module'/);
  });
  test(ed + ' Mini: every cable ends on the title bar\'s edge, spread along it', () => {
    assert.match(src, /if \(!r\.width && n\.m\.view === 'mini'\)\{/);
    assert.match(src, /f = mi \? \.2 \+ \.6 \* \(mi\[0\] \+ 1\) \/ \(mi\[1\] \+ 1\) : \.5/);
    assert.match(src, /if \(n\.m\.view === 'mini'\)\{ el\.style\.removeProperty\('--cr'\); el\.style\.removeProperty\('--sc'\); return; \}/, 'Mini keeps the declared width');
  });
  test(ed + ' M6: groups follow their members, a collapsed group is one block, hidden modules are never picked', () => {
    for (const re of [/function groupsFollow\(ids\)/, /function groupBounds\(g\)/, /function groupCross\(\)/, /function gcolPos\(n, dir, port, wr\)/, /function applyGroupHides\(\)/, /const gBox = new WeakMap\(\)/]) assert.match(src, re);
    assert.match(src, /groupsFollow\(start\.map\(q => q\[0\]\.m\.id\)\)/, 'dragging');
    assert.match(src, /groupsFollow\(\[\.\.\.selected\]\); markScene\(\); store\.touch\(\); return;/, 'nudging');
    assert.match(src, /groupsFollow\(ns\.map\(n => n\.m\.id\)\); markScene\(\); store\.touch\(\); return true;/, 'aligning');
    assert.match(src, /groupsFollow\(\[m\.id\]\);/, 'folding');
    assert.match(src, /doc\.groups = doc\.groups\.filter\(g => \(g\.modules \|\| \[\]\)\.length\)/, 'a group with no module left goes');
    assert.match(src, /if \(n\.gcol\) return gcolPos\(n, dir, port, wr\);/);
    assert.match(src, /if \(na && nb && na\.gcol && na\.gcol === nb\.gcol\) show = false;/);
    assert.match(src, /function setSel\(id, on\)\{ const n = nodes\.get\(id\); if \(on && n && n\.gcol\) return;/);
    assert.match(src, /\.node\.ghid\{display:none\}/);
    assert.match(src, /fold\.addEventListener\('click', \(\) => \{ if \(playBlocked\(\)\) return;/, 'no folding in Play');
    assert.doesNotMatch(src, /_el\b.*doc\.groups/, 'the box element is kept in a WeakMap, not on the group (which is saved)');
  });
}

test('the shared block is the same in both editors (python3 tools/sync-shared.py)', () => {
  const cut = s => s.slice(s.indexOf('/* ===SHARED EDITOR START==='), s.indexOf('/* ===SHARED EDITOR END===') + 30);
  assert.equal(cut(ED.chains), cut(ED.waves));
});

test('the declared widths: each from 150 to 600 px, narrow ones only for modules without a bypass button, and the same table in both editors', () => {
  const table = src => { const s = /const NARROW_S = (\d+), NARROW_M = (\d+);/.exec(src), t = /const MODULE_WIDTH = \{([\s\S]*?)\n\};/.exec(src); assert.ok(s && t); return vm.runInNewContext('({' + t[1].replace(/NARROW_S/g, s[1]).replace(/NARROW_M/g, s[2]) + '\n})'); };
  const c = table(ED.chains), w = table(ED.waves);
  assert.deepEqual(JSON.parse(JSON.stringify(c)), JSON.parse(JSON.stringify(w)));
  assert.ok(Object.keys(c).length >= 8);
  for (const [t, px] of Object.entries(c)) assert.ok(px >= 150 && px <= 600, t + ' ' + px);
  for (const [ed, src] of Object.entries(ED)) for (const t of Object.keys(c)) assert.match(src, new RegExp('\\n  ' + t + ':\\{ label:'), ed + ' has a module ' + t);
});

test('the modules that are narrow keep to sliders, selects and ports (no canvas display, no preset row, no bypass button)', () => {
  // read the definitions out of a running page would need a browser; here: none of the narrow modules names a display or fx:true in the definition line
  const c = /const MODULE_WIDTH = \{([\s\S]*?)\n\};/.exec(ED.chains)[1], types = [...c.matchAll(/(\w+):NARROW/g)].map(m => m[1]);
  for (const t of types) { const line = new RegExp('\\n  ' + t + ':\\{ label:[^\\n]*').exec(ED.chains)[0]; assert.doesNotMatch(line, /fx:true/, t + ' has a bypass button'); assert.doesNotMatch(line, /display:/, t + ' draws a display'); }
});

test('no audio file, golden or worklet differs from the merge base with origin/beta (skipped when no base can be resolved)', t => {
  const { execFileSync } = require('child_process');
  const git = args => execFileSync('git', args, { cwd: H.ROOT, stdio: ['ignore', 'pipe', 'pipe'] }).toString().trim();
  let base = ''; try { base = git(['merge-base', 'HEAD', 'origin/beta']); } catch (e) { return t.skip('no origin/beta merge base in this checkout'); }
  if (!base) return t.skip('no merge base');
  let changed = false; try { git(['diff', '--quiet', base, '--', 'tests/golden', 'chains/chains-worklet.js', 'waves/waves-worklet.js', 'core', 'dsp', 'pump', 'lxr']); } catch (e) { changed = e.status === 1; if (!changed) return t.skip('git diff could not run'); }
  assert.equal(changed, false, 'an audio file, golden or worklet differs from ' + base);
});

test('the new CSS uses the editors\' variables, not literal colours, for the Mini, width and group-fold rules', () => {
  for (const [ed, src] of Object.entries(ED)) {
    const css = src.slice(src.indexOf('/* WIDTHS (pass 5, M1)'), src.indexOf('/* WIDTHS (pass 5, M1)') + 2500) + src.slice(src.indexOf('/* MINI (pass 5)'), src.indexOf('/* MINI (pass 5)') + 800) + src.slice(src.indexOf('/* GROUP FOLD (pass 5'), src.indexOf('/* GROUP FOLD (pass 5') + 1000);
    assert.doesNotMatch(css.replace(/\/\*[\s\S]*?\*\//g, ''), /#[0-9a-fA-F]{3,6}\b|rgba?\(/, ed + ': a literal colour in the pass 5 CSS');
  }
});

test('release notes: build 588 is announced, the service worker cache is v588', () => {
  const root = H.read('index.html'), sw = H.read('sw.js');
  assert.match(root, /const BUILD = 588;/);
  assert.match(root, /\[588, '14 Nov', \[/);
  assert.match(sw, /thunderplus-v588/);
});
