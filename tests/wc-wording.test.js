// …waves / …chains pass 1c, wording and type (13 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md, U3, U5 to U10, T11).
// Source guards, no browser: the font floor and the size scale in both editors (css, the help page's css and the canvas text), one name per
// module (the list, the title bar and Help read the module's own label; no two modules alike; sentence case), one word per signal kind, the
// five unlabelled ports, the title and value column rules, the menu scroll, the touch tip box, the help-card colour, and the two copies in
// step. The browser run is tests/sweep/wc-wording.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const ROOT_HTML = H.read('index.html');
const block = (src, a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, a + ' … ' + b + ' not found'); return src.slice(i, j); };
const rule = (src, sel) => { const i = src.indexOf(sel); assert.ok(i >= 0, sel + ' not found'); return src.slice(i, src.indexOf('}', i) + 1); };
const SHARED = src => block(src, '/* ===SHARED EDITOR START===', '/* ===SHARED EDITOR END=== */');
const SCALE = [11, 12.5, 14, 17, 22];   // U8: five sizes, 11 px the floor

// module label by type, as the source spells it: `type:{ label:'X'`, `type:Object.assign({ label:'X'`, `SHARED_MODULES.type = { label:'X'`
const labels = src => { const m = new Map(); for (const x of src.matchAll(/(?:^|[\s.,])(\w+)(?::|\s*=)\s*(?:Object\.assign\()?\{\s*label:'([^']+)'/gm)) m.set(x[1], x[2]); return m; };
// the palette tuples ['type', 'List label'] (only those whose type has a label of its own)
const tuples = (src, lab) => [...src.matchAll(/\[\s*'(\w+)'\s*,\s*'([^']*)'\s*\]/g)].filter(x => lab.has(x[1]) && !lab.has(x[2]) && !/^(in|out)\d*$/.test(x[1]));

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ' U8: no font size below 11 px, and every size is one of the five (css, help page, canvas text)', () => {
    const sizes = [...src.matchAll(/font-size:\s*(\d+(?:\.\d+)?)px/g)].map(m => +m[1]);
    assert.ok(sizes.length > 50);
    for (const s of sizes) assert.ok(SCALE.includes(s), s + ' px is not on the scale ' + SCALE.join(', '));
    const canvas = [...src.matchAll(/\.font = '(?:\d+ )?(\d+(?:\.\d+)?)px/g)].map(m => +m[1]);
    assert.ok(canvas.length >= 4); for (const s of canvas) assert.ok(s >= 11, 'canvas text ' + s + ' px');
    assert.doesNotMatch(src, /font-size:\s*(9|10|10\.5)px/);
    assert.match(src, /font-size:clamp\(34px, 6vw, 76px\)/, 'the empty-patch title is display type, outside the scale');
  });
  test(ed + ' U8: the CPU readout colours are mixed with the skin ink (the amber was 2.6 : 1)', () => {
    assert.doesNotMatch(src, /#c99a00; border-color:#c99a00/);
    assert.match(src, /\.fps\.ok\{--c:color-mix\(in srgb, #b07a00 45%, var\(--ink\)\)/);
    assert.match(src, /\.pgroup \.pcount\{margin-left:auto; color:var\(--ink\)/, 'the group count is ink, not the muted grey (4.3 : 1 on a tinted header)');
  });
  test(ed + ' U7: a title wraps instead of being cut, the value column grows to its readout', () => {
    const t = rule(src, '.node > header .t{');
    assert.doesNotMatch(t, /text-overflow|white-space:nowrap|overflow:hidden/); assert.match(t, /overflow-wrap:normal/); assert.match(t, /hyphens:manual/, 'a title breaks only between words (review fix, build 582)'); assert.match(src, /\.node > header\.tt1 \.byp\{padding:0 3px\} \.node > header\.tt2 \.t\{letter-spacing:-\.04em\} \.node > header\.tt3 \.t\{overflow-wrap:break-word\}/, 'a word longer than the line tightens BYP, then the letter-spacing, before it may break'); assert.match(t, /line-height:1\.15/);
    assert.match(rule(src, '.node > header{'), /gap:4px/, 'room for a long word beside BYP, ? and ×');
    assert.match(src, /\.row\{display:grid; grid-template-columns:70px minmax\(0,1fr\) minmax\(64px,max-content\);/);
    assert.match(rule(src, '.row .val{'), /white-space:nowrap/);
    assert.match(src, /\.row\.wide\{grid-template-columns:70px 1fr\}/, 'the wide row is as it was');
  });
  test(ed + ' U10: the right-click menu has a maximum height and scrolls, and stays on the screen', () => {
    const c = rule(src, '.ctxmenu{position:fixed;');
    assert.match(c, /max-height:calc\(100vh - 16px\)/); assert.match(c, /overflow-y:auto/); assert.match(c, /overscroll-behavior:contain/);
    assert.match(block(src, 'function showMenu(', 'function hideMenu'), /ctx\.style\.top = Math\.max\(8, Math\.min\(y, window\.innerHeight - r\.height - 8\)\) \+ 'px'/);
  });
  test(ed + ' T11: the tip box has a touch-only note and a Tips button; the mouse note is as it was', () => {
    const html = block(src, '<div class="hint" id="hint">', '<div class="lasso"');
    assert.match(html, /<span class="tipm">Drag from an output jack/); assert.match(html, /<span class="tipt">Tap a jack, then another jack, to patch\./);
    assert.match(html, /<button id="hintpill" class="hintpill" aria-label="Show tips" aria-expanded="false">Tips<\/button>/);
    const tipt = html.match(/<span class="tipt">([^<]*)</)[1];
    assert.doesNotMatch(tipt, /option|\balt\b|shift|ctrl|right-click|click|mouse|keyboard|scroll/i, 'the touch note names no mouse or keyboard gesture');
    assert.match(html, /Hold Option \(Alt on Windows\)/); assert.match(html, /right-click to group them/);
    assert.match(src, /@media \(pointer: coarse\)\{\n  \.hint \.tipm\{display:none\} \.hint \.tipt\{display:block\}/);
    assert.match(src, /\.hint\.min\{padding:0; border:0; background:none; opacity:1\} \.hint\.min \.tipt, \.hint\.min #hintx\{display:none\}/);
    assert.match(src, /\.hint \.tipt, \.hint \.hintpill\{display:none\}/);
    const js = block(src, '// T11: with a touch screen', 'tipsBox.checked = false; }); }');
    assert.match(js, /pill\.addEventListener\('click', \(\) => \{ tipOpen\(true\); document\.getElementById\('hintx'\)\.focus\(\); \}\);/, 'focus follows the control: × after opening, Tips after folding');
    assert.match(js, /settings\.tipSeen = true; saveSettings\(\);/, 'the first visit is remembered in the settings (no patch field, no new key)');
    assert.match(js, /if \(coarse\.matches\)\{ tipOpen\(false\); pill\.focus\(\); return; \}[^\n]*hint\.hidden = true; settings\.tips = false;/, '× folds on touch and switches tips off with a mouse, as before');
  });
  test(ed + ' U9: a Help card takes the module\'s category colour and a tint of it', () => {
    assert.match(src, /const d = MODULES\[t\], ed = CAT_COLOR\[t\] \|\| EDGE\[famOf\(t\)\];/);
    assert.match(src, /\.mcard\{background:color-mix\(in srgb, #fff 88%, var\(--ed\) 12%\)/);
  });
  test(ed + ' U5: one word for each signal kind: audio, control, gate (tooltips, groups, Help)', () => {
    assert.match(src, /const KIND_WORD = \{ audio:'audio', mod:'control', gate:'gate' \};/);
    assert.match(src, /portName\(n\.m\.type, port, dir\) \+ ': ' \+ KIND_WORD\[kind\] \+ \(dir === 'in' \? ' input' : ' output'\)/);
    assert.match(src, /' modulation: ' \+ KIND_WORD\[kind\] \+ ' input'/); assert.match(src, /' modulation: ' \+ KIND_WORD\.mod \+ ' input'/);
    assert.match(src, /\[\['audio', 'Audio', 'solid line, round jack'/);
    for (const old of ["'CV utilities'", "'Audio → CV'", "'§Audio → CV'", "cv:'CV'", 'control voltage', "['audio', 'Sound'"]) assert.ok(!src.includes(old), old + ' is still in the source');
    assert.match(src, /\['Control utilities', '#9a8fb8'/); assert.match(src, /\['Audio to control', '#4f6f9f'/);
    assert.match(src, /Teal solid cables carry audio, purple dashed cables carry control signals and pink dotted cables carry gates/);
  });
  test(ed + ' U3: the ports that showed their raw key have names', () => {
    const lab = block(src, 'const SHARED_PORT_LABEL = {', 'Palette groups');
    for (const k of ['res:\'Resonance\'', 'mod:\'Modulator\'', 'pat:\'Pattern\'', 'noise:\'Noise\'', 'ramp:\'Ramp\'', 'bar:\'Bar\'']) assert.ok(lab.includes(k), k);
  });
  test(ed + ' U6: one name per module: the list shows the module\'s own label, and the descriptor moved to the tooltip', () => {
    const lab = labels(SHARED(src) + src); assert.ok(lab.size > 100, 'labels found: ' + lab.size);
    // the list entries: the label itself
    for (const [, t, l] of tuples(src, lab)) if (t !== 'pump') assert.equal(l, lab.get(t), 'list entry for ' + t + ' reads "' + l + '", the title bar "' + lab.get(t) + '"');
    assert.match(src, /title:\(PAL_HINT\[type\] \? label \+ ' \(' \+ PAL_HINT\[type\] \+ '\)\. ' : ''\) \+ 'Click to add, or drag onto the patch'/);
    assert.match(src, /\|\| \(PAL_HINT\[t\] \|\| ''\)\.toLowerCase\(\)\.includes\(q\)/, 'quick add still finds a module by its descriptor');
    for (const k of ['reson', 'degrade', 'blipdrum', 'life', 'meter', 'utility', 'chaos', 'string']) assert.match(block(src, 'const PAL_HINT = {', '};'), new RegExp('\\b' + k + ':\''));
    // no two modules share a name (types of one editor; the two compressors are Compressor and Simple compressor)
    const seen = new Map(); for (const [t, l] of lab) { if (seen.has(l) && seen.get(l) !== t) assert.fail('"' + l + '" names both ' + seen.get(l) + ' and ' + t); seen.set(l, t); }
    assert.equal(lab.get('comp'), 'Simple compressor'); assert.equal(lab.get('compressor'), 'Compressor');
    // sentence case: a capital after the first word only for acronyms and In / Out
    for (const [t, l] of lab) for (const w of l.split(/\s+/).slice(1)) assert.ok(!/^[A-Z]/.test(w) || /^[A-Z0-9&+\/-]+$/.test(w) || w === 'In' || w === 'Out', t + ': "' + l + '" is not sentence case');
  });
  test(ed + ' U5: the bus meters of waves are Waves In, as the Out module says', () => {
    if (ed !== 'waves') return;
    assert.match(src, /id="meters" title="Waves In 1–8"/); assert.match(src, /title:'Waves In ' \+ \(k \+ 1\)/); assert.doesNotMatch(src, /title="Out 1–8"|'Out ' \+ \(k \+ 1\) \+ ' \/ Waves In'/);
  });
}

test('the two editors keep the 1c code and css in step', () => {
  assert.equal(SHARED(ED.chains), SHARED(ED.waves), 'the SHARED EDITOR block differs (run python3 tools/sync-shared.py)');
  for (const sel of ['.node > header .t{', '.node > header{', '.row .val{', '.ctxmenu{position:fixed;', '.hint .tipt, .hint .hintpill{', '.mcard{', '.pgroup .pcount{', '.fps.good{'])
    assert.equal(rule(ED.chains, sel), rule(ED.waves, sel), sel + ' differs between chains and waves');
  const tip = s => block(s, '// T11: with a touch screen', 'tipsBox.checked = false; }); }');
  assert.equal(tip(ED.chains), tip(ED.waves));
  const coarse = s => block(s, '@media (pointer: coarse){\n  .hint .tipm', '.hint button{min-width');
  assert.equal(coarse(ED.chains), coarse(ED.waves));
  const tipt = s => s.match(/<span class="tipt">[^<]*<\/span>/)[0];
  assert.equal(tipt(ED.chains), tipt(ED.waves));
  const kw = s => s.match(/const KIND_WORD = [^\n]*/)[0]; assert.equal(kw(ED.chains), kw(ED.waves));
});

test('every name a user reads for a module is the same in the two editors', () => {
  const a = labels(ED.chains), b = labels(ED.waves);
  for (const [t, l] of b) if (a.has(t)) assert.equal(a.get(t), l, t + ' is "' + a.get(t) + '" in chains and "' + l + '" in waves');
});

test('only words changed: the module types, port keys and the settings keys that were there are as before', () => {
  for (const src of Object.values(ED)) {
    assert.match(src, /KP = EMBED \? 'thunderplus\.(chains|waves)\.' : '(chains|waves)\.'/);
    assert.match(src, /const SETKEY = KP \+ 'settings';/);
    for (const k of ['envfol', 'gateex', 'pitchfol', 'pitchtrk', 'snh', 'buscomp', 'glue', 'slew', 'quant']) assert.match(src, new RegExp('\\b' + k + ':\\{ ?label:|\\b' + k + ':Object\\.assign\\(\\{ label:|SHARED_FAMILY.*\\b' + k + ':'), 'type id ' + k + ' is still there');
    assert.doesNotMatch(src, /tipSeen[^\n]*(patch|doc)\./, 'the first-visit flag is a setting, not part of a patch');
  }
});

test('the root app changed only for the release (no new colour variable was needed: the editors derive theirs from --ink)', () => {
  const css = ROOT_HTML.slice(ROOT_HTML.indexOf('function termFrameCss()'), ROOT_HTML.indexOf('function skinFrame(f)'));
  assert.doesNotMatch(css, /--cpu|--fs-/);
});
