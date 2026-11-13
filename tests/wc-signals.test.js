// …waves / …chains pass 1b, signal kinds and cable states (13 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md, M9, U1, D6, D9).
// Source guards: both editors define the gate jack shape and the gate cable dash, the selection colour is not a signal colour (in the
// editors' own stylesheet and in the host's skin frame css), cable states are CSS only, the cable kind is still derived from the
// ports (nothing new saved), and the two copies stay in step. The browser run is tests/sweep/wc-signals.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const ROOT_HTML = H.read('index.html');
const fn = (src, name) => { const m = src.match(new RegExp('function ' + name + '\\([^)]*\\)\\s*\\{[\\s\\S]*?\\n\\}\\n')); assert.ok(m, 'function ' + name + ' not found'); return m[0]; };
const rule = (src, sel) => { const i = src.indexOf(sel); assert.ok(i >= 0, sel + ' not found'); return src.slice(i, src.indexOf('}', i) + 1); };

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ': a gate jack is a diamond (rotated square), also in Compact and for a touch target', () => {
    const j = rule(src, '.jack[data-kind="gate"]{width:15px');
    assert.match(j, /transform:rotate\(45deg\)/); assert.match(j, /border-radius:2px/);
    assert.match(src, /\.node\.collapsed \.jack\[data-kind="gate"\]\{transform:rotate\(45deg\) scale\(\.85\)\}/);
    assert.match(src, /\.jack\[data-kind="gate"\]::before\{inset:-11px\}/);
    // the audio and control jacks are still the plain round ones: no other rule shapes them by kind
    assert.doesNotMatch(src, /\.jack\[data-kind="(audio|mod)"\]\{[^}]*(transform|border-radius|width)/);
  });
  test(ed + ': the three cable kinds have three dashes: audio solid, control dashed, gate dotted with round dots', () => {
    assert.match(src, /svg\.cables \.cln\.ctl\{stroke-width:2px; stroke-dasharray:6 4\}/);
    assert.match(src, /svg\.cables \.cln\.gate\{stroke-width:3px; stroke-linecap:round; stroke-dasharray:\.1 7\}/);
    assert.doesNotMatch(src.slice(src.indexOf('svg.cables .cln{'), src.indexOf('svg.cables .cln{') + 100), /dasharray/, 'audio stays solid');
    assert.match(fn(src, 'cableGroup'), /\(kind === 'gate' \? ' gate' : ''\)/, 'the cable (and the one being dragged) gets the gate class from its kind');
  });
  test(ed + ': the cable kind is still derived from the output port, with no new saved field', () => {
    const k = fn(src, 'cableKind');
    assert.match(k, /MODULES\[src\.type\]\.outputs\[c\.from\[1\]\]/);
    assert.match(src, /const c = \{ from, to, bend:0 \};/, 'connect() writes the same cable shape as before');
    assert.doesNotMatch(src, /c\.kind\s*=|cable\.kind\s*=/);
  });
  test(ed + ': the selection colour is its own variable and none of the signal variables', () => {
    const root = src.slice(src.indexOf(':root{'), src.indexOf('}', src.indexOf(':root{')));
    const v = k => { const m = root.match(new RegExp('--' + k + ':\\s*([^;]+);')); assert.ok(m, '--' + k + ' defined'); return m[1].trim().replace(/\s*\/\*.*$/, ''); };
    const sel = v('selc');
    for (const s of ['audio', 'audio-dark', 'mod', 'mod-dark', 'gate', 'gate-dark']) assert.notEqual(sel, v(s), '--selc equals --' + s);
    assert.match(sel, /^#1{6}$/, 'a neutral ink in the white theme');
    const css = src.slice(src.indexOf('.node.selected{'), src.indexOf('.node.selected::after'));
    assert.match(css, /var\(--selc\)/); assert.doesNotMatch(css, /var\(--(audio|mod|gate)/);
    assert.doesNotMatch(src.slice(src.indexOf('svg.cables g.sel'), src.indexOf('svg.cables g.sel') + 600), /var\(--(audio|mod|gate)/, 'the cable halo is the selection colour too');
  });
  test(ed + ': cable hover and selected are CSS only; selection state is written when it changes, never per frame', () => {
    assert.match(src, /svg\.cables g:hover > \.hit\{stroke:var\(--selc\); stroke-opacity:\.16\}/);
    assert.match(src, /svg\.cables g\.sel > \.hit\{stroke:var\(--selc\); stroke-opacity:\.32\}/);
    assert.match(src, /svg\.cables g:hover > rect\.bend, svg\.cables g\.sel > rect\.bend, body\.alting svg\.cables rect\.bend\{opacity:1; pointer-events:all\}/);
    assert.doesNotMatch(fn(src, 'drawCables'), /selCable|classList|\.sel\b/, 'drawCables does no selection work');
    assert.doesNotMatch(fn(src, 'placeCable'), /selCable|classList/);
    const loop = fn(src, 'loop');
    assert.match(loop, /if \(relDirty\) paintRel\(\);/);
    assert.match(fn(src, 'paintRel'), /if \(el\.dim !== dim\)/, 'a cable is only touched when its dimmed state changes');
    // the hit zone follows the zoom, written when the zoom changes
    assert.match(src, /svg\.cables \.hit\{cursor:pointer; stroke-width:calc\(14px \* var\(--iz, 1\)\)\}/);
    assert.match(src, /svg\.cables \.hit\{stroke-width:calc\(26px \* var\(--iz, 1\)\)\}/, 'touch keeps its wide zone');
    assert.match(fn(src, 'applyPan'), /if \(v\.zoom !== hsZ\)\{ hsZ = v\.zoom; svg\.style\.setProperty\('--iz'/);
  });
  test(ed + ': a click selects a cable, Delete / Backspace removes just it in one undo step, Escape and the background let go', () => {
    assert.match(fn(src, 'selectCable'), /clearCableSel\(\); if \(selected\.size\) clearSel\(\);/, 'cable and module selection are exclusive');
    assert.match(src, /function setSel\(id, on\)\{[^\n]*if \(on\) clearCableSel\(\);/);
    assert.match(src, /selectCable\(c\);\n      if \(ev\.altKey\)/, 'pointerdown on the cable selects it (before the Alt-bend branch)');
    const k0 = src.indexOf("window.addEventListener('keydown', e => {\n  if (!selCable) return;"); assert.ok(k0 > 0, 'cable key handler');
    const key = src.slice(k0, src.indexOf('\n});', k0) + 4);
    assert.match(key, /e\.key === 'Escape'\)\{ clearCableSel\(\); return; \}/);
    assert.match(key, /store\.snapshot\(\); disconnect\(c\); toast\('Cable removed \(Undo brings it back\)\.'\);/);
    assert.equal((key.match(/store\.snapshot\(\)/g) || []).length, 1, 'one snapshot, so one undo step');
    assert.match(src, /clearSel\(\); clearCableSel\(\); \}   \/\/ \(a middle-button pan keeps the selection\)/);
    assert.match(src, /if \(selCable && !cableEls\.has\(selCable\)\) selCable = null;/, 'a stale selection (undo rebuilt the patch) is dropped');
  });
  test(ed + ': bend handles show on a hovered or selected cable; a plain click on one is not an undo step', () => {
    const b = fn(src, 'bendDrag');
    assert.match(b, /function bendDrag\(ev, c, idx, lazy\)/); assert.match(b, /if \(!snapped\)\{ snapped = true; store\.snapshot\(\); \}/);
    assert.match(src, /bendDrag\(ev, c, \(\) => el\.hdl\.indexOf\(r\), true\)/);
    assert.match(src, /transform:scale\(var\(--hs, 1\)\)/, 'a handle keeps about 12 px on screen when zoomed out');
    assert.match(fn(src, 'syncHandles'), /pts\[i\]\[0\] - 5/, 'handles are still placed from the saved pts');
  });
  test(ed + ': the help explains the signal kinds in words and in a drawn legend', () => {
    assert.match(src, /pink dotted cables carry gates; gate jacks are diamonds, the others round/);
    assert.match(src, /Click a cable to select it: Delete removes it, Escape lets go/);
    assert.match(fn(src, 'helpDoc'), /const legend = '<div class="legend"><b>Signal kinds<\/b>'/);
    assert.match(src, /'<\/nav><main>' \+ legend \+ '<div class="guide">'/);
    assert.match(src, /\.pt i\.g\{[^}]*rotate\(45deg\)/);
    assert.match(src, /Gate cables are dotted and gate jacks are diamonds\./, 'the tip box says it too');
  });
}

test('the two editors keep the signal-kind and cable-state code in step', () => {
  for (const f of ['cableGroup', 'cableKind', 'selectCable', 'clearCableSel', 'paintRel', 'bendDrag', 'syncHandles', 'cableBendAt', 'placeCable', 'drawCables', 'jackPos'])
    assert.equal(fn(ED.chains, f), fn(ED.waves, f), f + ' differs between chains and waves');
  const css = s => s.slice(s.indexOf('svg.cables .cln.ctl{'), s.indexOf('.node{position:absolute'));
  assert.equal(css(ED.chains), css(ED.waves), 'cable css differs');
  const jack = s => s.slice(s.indexOf('.jack[data-kind="audio"]'), s.indexOf('.jack.small{'));
  assert.equal(jack(ED.chains), jack(ED.waves), 'jack css differs');
  const sel = s => s.slice(s.indexOf('.node.selected{'), s.indexOf('.node.selected::after'));
  assert.equal(sel(ED.chains), sel(ED.waves));
});

test('the host\'s skin frame css gives the terminal skins a selection colour that is not audio, control or gate, and a gate cable colour', () => {
  const css = ROOT_HTML.slice(ROOT_HTML.indexOf('function termFrameCss()'), ROOT_HTML.indexOf('function skinFrame(f)'));
  const root = css.slice(css.indexOf(':root{'), css.indexOf('--font:'));
  const sel = root.match(/--selc:([^;]+);/)[1].trim(), vars = k => root.match(new RegExp('--' + k + ':([^;]+);'))[1].trim();
  assert.match(sel, /color-mix\(in srgb, \$\{mx\} 72%, \$\{bg\}\)/, 'a blend of the skin ink and its background');
  for (const k of ['audio', 'mod', 'gate']) assert.notEqual(sel, vars(k));
  assert.doesNotMatch(sel, /\$\{(poly|soft|mx2)\}$/, 'not poly (control), soft (gate) or mx2 (the 909 control colour)');
  assert.match(css, /svg\.cables \.cln\.gate\{stroke:\$\{soft\}!important\}/, 'the gate cable takes the gate colour in a terminal skin (it used to take the control colour)');
});
