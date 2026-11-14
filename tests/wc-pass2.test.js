// …waves / …chains pass 2, "Fixes" (13 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: D2 D3 D4 T1 T2 T3 T5 T10 U2 H1 H2 H3 H4).
// Source guards, no browser: the opening-zoom rule, the title-bar drag threshold, the touch delete question, the typed-value search (run
// here on its own, with the editors' own formatters), one undo step per drag, the cable hit layer under the modules, Fit at every
// width, the module list on a phone, and the two copies in step. The browser run is tests/sweep/wc-pass2.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('node:vm');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };
const ROOT_HTML = H.read('index.html');
const fn = (src, name) => { const i = src.indexOf('function ' + name + '('); assert.ok(i >= 0, 'function ' + name + ' not found'); const j = src.indexOf('\n}\n', i); return src.slice(i, j + 3); };

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ' T1: a small screen opens readable: 80% floor, 100% ceiling, the first module at the left, a saved small-screen view kept', () => {
    const o = fn(src, 'openView');
    assert.match(src, /const READ_ZOOM = 0\.8;/);
    assert.match(src, /const smallWs = \(\) => ws\.clientWidth > 0 && ws\.clientWidth < 1000;/);
    assert.match(o, /fz >= READ_ZOOM/); assert.match(o, /v\.zoom = Math\.min\(1, fz\)/); assert.match(o, /v\.zoom = READ_ZOOM/);
    assert.match(o, /viewWasNarrow/, 'a view saved on a small screen is the only one kept');
    assert.match(o, /if \(!smallWs\(\)\)\{ if \(firstRun\) fitAll\(\); return; \}/, 'a wide workspace is untouched: a saved view stays, a new patch fits as before');
    assert.match(src, /requestAnimationFrame\(\(\) => openView\(\)\)/);
    assert.doesNotMatch(src, /if \(firstRun\) requestAnimationFrame\(\(\) => fitAll\(\)\)/, 'the old opening fit is gone');
    assert.match(src, /const viewWasNarrow = !!\(doc\.view && doc\.view\.nw\);/, 'read before the first draw stamps the view');
    assert.match(src, /if \(ws\.clientWidth && !!v\.nw !== smallWs\(\)\)\{ if \(smallWs\(\)\) v\.nw = 1; else delete v\.nw; \}/, 'view.nw is the only new saved field, optional');
    assert.match(src, /\bfitAll\(\)/, 'Fit is still the whole-patch view');
  });
  test(ed + ' T10: the title bar moves a module after 8 screen px on a touch screen, 3 with a mouse, and does not auto-pan before that', () => {
    assert.match(src, /thr = e\.pointerType === 'touch' \|\| e\.pointerType === 'pen' \? 8 : dup \? 4 : 3/);
    assert.match(src, /Math\.hypot\(dx, dy\) \* z > thr/, 'the threshold is in screen pixels, at any zoom');
    assert.match(src, /if \(moved \|\| Math\.hypot\(ev\.clientX - sx, ev\.clientY - sy\) > thr\) autoPanAt\(ev\)/);
    assert.doesNotMatch(src, /Math\.hypot\(dx, dy\) > \(dup \? 4 : 1\)/, 'the 1 px threshold is gone');
  });
  test(ed + ' T5: a finger asks before it removes a module (cancel keeps it, Remove is one undo step); a mouse removes at once; the buttons are 40 px tall', () => {
    assert.match(src, /onclick: ev => \{ if \(playBlocked\(\)\) return; if \(ev && \(ev\.pointerType === 'touch' \|\| ev\.pointerType === 'pen'\)\) askRemove\(m\.id, title\); else \{ store\.snapshot\(\); removeModule\(m\.id\); \} \}/);
    const a = fn(src, 'askRemove');
    assert.match(a, /askDialog\('Remove “' \+ title \+ '”\? Undo brings it back\.', \{ ok:'Remove' \}\)/);
    assert.match(a, /if \(yes && nodes\.has\(id\)\)\{ store\.snapshot\(\); removeModule\(id\); \}/, 'the snapshot is taken after the answer: Cancel leaves no step');
    const css = src.slice(src.indexOf('/* T5 (13 Nov, audit T5'), src.indexOf('@media (prefers-reduced-motion: reduce){ .node > header .fold svg'));
    assert.match(css, /button\.mq\{width:32px; height:40px/); assert.match(css, /button\.x, \.node\.collapsed > header button\.x\{width:36px; height:40px\}/);
    assert.match(css, /button\.byp\{height:40px; min-width:40px\}/); assert.match(css, /\.fold::before\{inset:0 -10px\}/);
  });
  test(ed + ' D3: the undo step is taken on the first change, not on the press; a select takes one when its value changes', () => {
    const s = fn(src, 'slider'), c = fn(src, 'choice');
    assert.match(s, /let snapped = false; const snapOnce = \(\) => \{ if \(!snapped\)\{ snapped = true; store\.snapshot\(\); \} \};/);
    assert.match(s, /inp\.addEventListener\('input', \(\) => \{ snapOnce\(\); setParam/);
    assert.doesNotMatch(s.slice(s.indexOf("inp.addEventListener('pointerdown'"), s.indexOf("inp.addEventListener('pointerdown'") + 120), /store\.snapshot\(\)/, 'the press itself takes no step');
    assert.match(s, /const reset = \(\) => \{ if \(\+n\.m\.params\[key\] !== \+def\) store\.snapshot\(\);/, 'a reset at the default takes none');
    assert.doesNotMatch(c, /pointerdown', \(\) => store\.snapshot\(\)/);
    assert.match(c, /sel\.addEventListener\('change', \(\) => \{ store\.snapshot\(\); setParam\(n, key, \+sel\.value, true\); \}\)/);
  });
  test(ed + ' D2: the typing box searches the parameter\'s range through the formatter', () => {
    assert.match(src, /let v = typedToParam\(fmt, \+min, \+max, \+step \|\| 0, shown, txt\);/);
    assert.match(src, /if \(v != null && v !== \+n\.m\.params\[key\]\)\{ store\.snapshot\(\); setParam\(n, key, \+v\.toFixed\(6\), true\); inp\.value = v; \}/, 'no step when the value does not change');
    assert.doesNotMatch(src, /if \(fmt\(max\)\.includes\('%'\)\) v \/= 100/, 'the old special cases are gone');
  });
  test(ed + ' D4: the cable hit lines are in their own layer under the modules; the drawn cables stay on top and take no press', () => {
    assert.match(src, /<svg class="cables under" id="cablehits"/);
    assert.match(src, /svg\.cables\.under\{z-index:2\}/);
    assert.match(src, /svg\.cables\{position:absolute;[^}]*pointer-events:none; z-index:1000000\}/, 'the drawn cables are above and let every press through');
    assert.match(src, /\.decor\{position:absolute; z-index:1;/, 'group boxes are below the hit lines, the modules (11 and up) are above');
    assert.match(src, /zTop = 10/);
    assert.match(src, /el\.gh = svgEl\('g', \{\}\); el\.gh\.append\(hit\); el\.hit = hit; svgHits\.append\(el\.gh\);/);
    assert.match(src, /hit\.addEventListener\('mouseenter', \(\) => el\.g\.classList\.add\('hov'\)\)/);
    assert.match(src, /svg\.cables g\.hov > \.cln, svg\.cables g\.sel > \.cln\{stroke-width:4px\}/);
    assert.match(src, /svgHits\.style\.setProperty\('--iz'/, 'the hit width keeps its screen size');
  });
  test(ed + ' T2 / T3: Fit stays at every width (the readout goes first); the module list leaves a strip of the patch, and closes after an add on a narrow screen', () => {
    assert.match(src, /@media \(max-width: 520px\)\{ \.bar #zoomval\{display:none\} \}/);
    assert.match(src, /@media \(max-width: 1340px\)\{ \.bar \.vsep\{display:none\}/);
    assert.doesNotMatch(src, /\.bar \.zoomctl #zoomfit/, 'nothing hides Fit any more');
    assert.match(src, /max-width:calc\(100vw - 38px - 44px\)/);
    assert.match(src, /const narrowWs = \(\) => ws\.clientWidth < 760;/);
    const a = fn(src, 'afterAdd'); assert.match(a, /if \(narrowWs\(\) && !drawer\.classList\.contains\('collapsed'\)\) setDrawer\(false\)/); assert.match(a, /applyPan\(\)/);
    const add = fn(src, 'addItem'); assert.match(add, /afterAdd\(\[m\.id\]\)/); assert.match(add, /afterAdd\(ids\)/);
  });
  test(ed + ' H1 to H4: no failed service-worker registration, no dead settings call, no cancelled radii', () => {
    assert.doesNotMatch(src, /serviceWorker\.register/);
    assert.doesNotMatch(src, /settings\(\)\{ const b = document\.getElementById\('setbtn'\)/);
    for (const sel of ['.node > header .byp{', '.mk.dk{', '.port .pm{', '.mark{', '.shw .tr{']) { const i = src.indexOf('\n' + sel); if (i < 0) continue; assert.doesNotMatch(src.slice(i, src.indexOf('}', i)), /border-radius/, sel + ' keeps a radius that *{border-radius:0 !important} cancels'); }
    assert.match(src, /\*:not\(\.jack\):not\(\.sw\):not\(input\)\{border-radius:0 !important\}/, 'the square look is unchanged');
  });
}
test('chains H4: Space is only taken when embedded; H2: the hidden keys checkbox is gone', () => {
  assert.match(ED.chains, /if \(!EMBED \|\| e\.key !== ' ' \|\| e\.metaKey \|\| e\.ctrlKey \|\| e\.altKey \|\| e\.repeat\) return;/);
  assert.doesNotMatch(ED.chains, /id="setkeys"/); assert.doesNotMatch(ED.chains, /function paintKeys/);
});
test('U2: waves reports and sets Voices and On-screen keys; the host Patch tab shows them for waves only', () => {
  assert.match(ED.waves, /voices: \(doc\.voice && doc\.voice\.maxVoices\) \|\| 8, keys: !!keysBox\.checked/);
  assert.match(ED.waves, /else if \(k === 'voices'\)\{ const nv = clamp\(Math\.round\(\+v\) \|\| 8, 1, 16\); vSel\.value = String\(nv\); vSel\.dispatchEvent\(new Event\('change'\)\); \}/);
  assert.match(ED.waves, /else if \(k === 'keys'\)\{ keysBox\.checked = !!v; keysBox\.dispatchEvent\(new Event\('change'\)\); \}/);
  assert.doesNotMatch(ED.chains, /voices: /);
  const b = fn(ROOT_HTML, 'buildPatch');
  assert.match(b, /if \(s\.voices != null\)\{/); assert.match(b, /row\('Voices', vs\)/); assert.match(b, /'On-screen keys'/);
});
test('both copies in step: the typed-value search and the shared block are the same text in chains and waves', () => {
  assert.equal(fn(ED.chains, 'typedToParam'), fn(ED.waves, 'typedToParam'));
  const blk = s => { const a = s.indexOf('/* ===SHARED EDITOR START==='), b = s.indexOf('/* ===SHARED EDITOR END=== */'); assert.ok(a > 0 && b > a); return s.slice(a, b); };
  assert.equal(blk(ED.chains), blk(ED.waves), 'run python3 tools/sync-shared.py');
  for (const n of ['openView', 'afterAdd', 'askRemove']) assert.equal(fn(ED.chains, n).replace(/\s+/g, ' '), fn(ED.waves, n).replace(/\s+/g, ' '), n + ' differs between the editors');
});
test('no audio code: the worklets still carry the cache-buster the host loads (pass 2 touches no worklet)', () => {
  for (const f of ['chains/chains-worklet.js', 'waves/waves-worklet.js']) assert.ok(H.read(f).length > 1e5, f);
  assert.doesNotMatch(H.read('chains/chains-worklet.js'), /typedToParam|openView|afterAdd/);
});

// ---- the typed-value search itself, with the editors' own formatters ----
const run = src => { const sb = {}; vm.createContext(sb); vm.runInContext(fn(src, 'typedToParam') + ';this.T = typedToParam;', sb); return sb.T; };
const T = run(ED.chains);
const pct = v => Math.round(v * 100) + '%', db = v => (v > 0 ? '+' : '') + v.toFixed(1) + ' dB', semis = v => (v > 0 ? '+' : '') + Math.round(v) + ' st';
const khz = v => v >= 1000 ? (v / 1000).toFixed(1) + ' kHz' : Math.round(v) + ' Hz', secs = v => v < 1 ? Math.round(v * 1000) + ' ms' : v.toFixed(2) + ' s', ratio = v => v.toFixed(1) + ':1';
const land = (fmt, min, max, step, cur, typed) => { const v = T(fmt, min, max, step, fmt(cur), typed); return v == null ? null : fmt(v); };
test('D2: a typed value lands on the value the readout shows (percent, dB, Hz / kHz, ms / s, semitones, a ratio, a plain number)', () => {
  assert.equal(land(pct, 0, 1, .01, .25, '40'), '40%'); assert.equal(land(pct, 0, 1, .01, .25, '100%'), '100%'); assert.equal(land(pct, -1, 1, .01, 0, '-50'), '-50%'.replace('-', '-'));
  assert.equal(land(db, -60, 12, .1, -3, '6'), '+6.0 dB'); assert.equal(land(db, -60, 12, .1, -3, '-12 dB'), '-12.0 dB'); assert.equal(land(db, -60, 12, 0, -3, '6'), '+6.0 dB');
  assert.equal(land(khz, 20, 20000, 0, 1200, '3'), '3.0 kHz', 'a bare number is in the unit the readout shows');
  assert.equal(land(khz, 20, 20000, 0, 1200, '800'), '800 Hz', 'out of the kHz range: read as Hz');
  assert.equal(land(khz, 20, 20000, 0, 100, '2.5k'), '2.5 kHz'); assert.equal(land(khz, 20, 20000, 0, 100, '440'), '440 Hz');
  assert.equal(land(secs, .001, 4, 0, .25, '300'), '300 ms'); assert.equal(land(secs, .001, 4, 0, .25, '1.5 s'), '1.50 s'); assert.equal(land(secs, .001, 4, 0, 1.5, '250 ms'), '250 ms'); assert.equal(land(secs, .001, 4, 0, 1.5, '2'), '2.00 s');
  assert.equal(land(semis, -24, 24, 1, 0, '-7'), '-7 st'); assert.equal(land(semis, -24, 24, 1, 0, '7 st'), '+7 st');
  assert.equal(land(ratio, 1, 20, .1, 4, '8'), '8.0:1');
  assert.equal(land(v => v.toFixed(2), 0, 1, .01, .5, '.25'), '0.25');
  assert.equal(T(v => ['C', 'D', 'E'][Math.round(v)], 0, 2, 1, 'C', '1'), null, 'a readout that is not a number leaves it to the raw parameter');
  assert.equal(T(pct, 0, 1, .01, '25%', 'abc'), null);
});
test('D2: beyond the range it takes the end; a rounding readout takes the middle of the values that show the same text', () => {
  assert.equal(T(db, -60, 12, 0, db(0), '99'), 12); assert.equal(T(db, -60, 12, 0, db(0), '-999'), -60);
  const v = T(db, -60, 12, 0, db(0), '6'); assert.ok(Math.abs(v - 6) < 0.02, 'got ' + v);
  assert.equal(T(pct, 0, 1, 0, '5%', '100'), 1); assert.equal(T(pct, 0, 1, 0, '5%', '0'), 0);
});
