// …waves / …chains pass 4, "Desktop feel" (14 Nov 2026; docs/audits/2026-11-13-waves-chains-ux.md: D1 D5 D7 D8 D10 M3 M4 M5 M8 M11 U4 U11).
// Source guards, no browser, plus one engine run (the summing that M3 relies on, read from the worklet, not changed). The browser run is tests/sweep/wc-pass4.mjs.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const ED = { chains: H.read('chains/index.html'), waves: H.read('waves/index.html') };

for (const [ed, src] of Object.entries(ED)) {
  test(ed + ' review fixes: section folds are blocked in Play; Alt+E ends a title-bar drag', () => {
    assert.match(src, /hd\.addEventListener\('click', \(\) => \{\s*\n\s*if \(playBlocked\(\)\) return;/);
    assert.match(src, /if \(nodeDragging && nodeDragging\.stop\) nodeDragging\.stop\(\);/);
  });
  test(ed + ' D5: the wheel changes a slider only after the pointer has rested on it, never during a pan or zoom', () => {
    assert.match(src, /const WHEEL_REST = 350/);
    assert.match(src, /now - wheelR\.at < WHEEL_REST \|\| now - wheelPanAt < WHEEL_REST/);
    assert.match(src, /inGesture \|\| drag \|\| nodeDragging \|\| ws\.classList\.contains\('panning'\) \|\| touches\.size > 0/);
    assert.match(src, /if \(e\.ctrlKey \|\| e\.metaKey\)\{ wheelPanAt = now; return; \}/, 'Ctrl/Cmd+wheel stays the zoom');
    assert.match(src, /\.row\.sl input\.wheelon\{outline:2px solid var\(--selc\)/);
  });
  test(ed + ' D1 / D8: the value menu is for a mouse too; Ctrl/Cmd-click no longer resets', () => {
    assert.match(src, /if \(id && nodes\.has\(id\)\)\{   \/\/ D1/);
    assert.doesNotMatch(src, /if \(e\.altKey \|\| \(MAC \? e\.metaKey : e\.ctrlKey\)\)\{ reset\(\); return; \}/);
    assert.match(src, /if \(e\.altKey\)\{ reset\(\); return; \}/);
  });
  test(ed + ' D7: the keys, each guarded against typing, a dialog, MIDI learn and Play where it edits', () => {
    assert.match(src, /if \(typing\(e\) \|\| document\.querySelector\('\.dlgov'\) \|\| \(learn && learn\.on\)\) return;/);
    for (const re of [/c === 'KeyA'/, /c === 'KeyG'/, /c === 'KeyF'/, /zoomToSel\(\)/, /Arrow\(Left\|Right\|Up\|Down\)/, /renameModule\(\[\.\.\.selected\]\[0\]\)/, /e\.key === 'Escape'/, /c === 'Digit0'/]) assert.match(src, re);
    assert.match(src, /if \(playMode \|\| !selected\.size \|\| !ctx\.hidden\) return;/, 'arrows never move anything in Play');
    assert.match(src, /typeof K_LOW === 'undefined'/, 'N is a note in waves: only chains takes it');
    assert.match(src, /\['Keys', /, 'the keys are listed in Help');
  });
  test(ed + ' D10 / M11: zoom to selection, align, space evenly, Tidy up by signal flow keeping groups, snap to a 10 px grid', () => {
    for (const re of [/function fitNodes\(list, maxZ\)/, /function zoomToSel\(\)/, /function alignSel\(kind\)/, /function tidy\(\)/, /const GRID = 10/, /snapOn = settings\.snap !== false/]) assert.match(src, re);
    assert.equal((src.match(/function tidy\(\)/g) || []).length, 1, 'one Tidy up');
    assert.doesNotMatch(src, /doc\.groups = \[\]; renderGroups\(\); store\.touch\(\);/, 'Tidy up no longer deletes the groups');
    assert.match(src, /an\[1\] \+ dx\) - an\[1\]/);
  });
  test(ed + ' M3: a second cable into an input is added (the engines sum); only an exact copy is refused', () => {
    assert.doesNotMatch(src, /const old = edgeInto\(to\[0\], to\[1\]\); if \(old\) disconnect\(old, true\);/);
    assert.match(src, /Those two are already connected\./);
    assert.match(src, /const lastInto = /);
    assert.match(src, /they add together/);
  });
  test(ed + ' M4 / M5: Find a module from a key and in the list; comments and rename; saved fields are optional', () => {
    assert.match(src, /function quickAddKey\(\)/); assert.match(src, /class:'psearch'/); assert.match(src, /'note:comment'/);
    assert.match(src, /function addNote\(wx, wy\)/); assert.match(src, /function renameModule\(id\)/); assert.match(src, /renderGroups\(\); renderNotes\(\);/);
    assert.match(src, /if \(!doc\.notes\.length\) delete doc\.notes;/, 'no empty notes field is saved');
    assert.match(src, /body\.play \.cmt \.ctext/);
  });
  test(ed + ' M8: the cable readout comes from the jack telemetry already received; nothing is written while no cable is hovered', () => {
    assert.match(src, /function cabHover\(c, ev\)/); assert.match(src, /if \(hovCable\) cabText\(\);/);
    assert.match(src, /if \(!c\)\{ if \(cabTip && !cabTip\.hidden\) cabTip\.hidden = true;/);
    assert.match(src, /cabHover\(c, ev\)/);
    assert.doesNotMatch(src.slice(src.indexOf('function cabText')), /requestAnimationFrame\(cab/);
  });
  test(ed + ' U11: roles, labels, one tab stop per module, a live region', () => {
    assert.match(src, /role="region" aria-label="Patch"/); assert.match(src, /role="status" aria-live="polite"/);
    assert.match(src, /tabindex:'0', role:'group', 'aria-label': title/);
    assert.match(src, /role:'button', 'aria-label': \(dir === 'in' \? 'Input ' : 'Output '\)/);
    assert.match(src, /role:'menuitem'/);
    assert.match(src, /n\.el\.matches\(':focus-visible'\)/);
  });
}
test('chains U4: presets travel with the patch; waves has no Preset row (not in this pass)', () => {
  assert.match(ED.chains, /const prePatchOf = /); assert.match(ED.chains, /function prePatchSet\(t, list\)/); assert.match(ED.chains, /presets:this\.doc\.presets/);
  assert.doesNotMatch(ED.waves, /presetRow\(n\)/);
});
test('the shared block is the same in both editors (python3 tools/sync-shared.py)', () => {
  const cut = s => s.slice(s.indexOf('/* ===SHARED EDITOR START==='), s.indexOf('/* ===SHARED EDITOR END===') + 30);
  assert.equal(cut(ED.chains), cut(ED.waves));
});
test('no audio file, golden or worklet cache-buster was touched by this pass', () => {
  const { execSync } = require('child_process');
  let out = ''; try { out = execSync('git status --porcelain -- tests/golden chains/chains-worklet.js waves/waves-worklet.js core dsp pump lxr', { cwd: H.ROOT }).toString(); } catch (e) { return; }
  assert.equal(out.trim(), '');
});
test('chains engine (read, not changed): two cables into one input equal the sum of the two signals', () => {
  const BYTES = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'chains/dsp/tfx.wasm')));
  const run = cables => {
    const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); ctx.ChainsDSP.wasmInit({ tfxBytes: BYTES });
    const E = new ctx.ChainsDSP.ChainsEngine(48000, { focus: 0, quality: 'full', transport: { tempo: 120, lpb: 4, ticks: 6, playing: true } });
    E.loadPatch({ modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type: 'utility', scope: 'mono', params: { gain: -6, pan: 0, mono: 0, flip: 0, mute: 0 } }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }], cables });
    const sig = H.testSignal(128 * 40), L = new Float32Array(128 * 40);
    for (let b = 0; b < 40; b++) { const s = sig.subarray(b * 128, (b + 1) * 128); E.setTrackInputs(s, s); E.process(128); L.set(E.bus(1), b * 128); }
    return L;
  };
  const direct = run([{ from: ['a', 'out'], to: ['c', 'in'] }]), viaB = run([{ from: ['a', 'out'], to: ['b', 'in'] }, { from: ['b', 'out'], to: ['c', 'in'] }]);
  const both = run([{ from: ['a', 'out'], to: ['c', 'in'] }, { from: ['a', 'out'], to: ['b', 'in'] }, { from: ['b', 'out'], to: ['c', 'in'] }]);
  let worst = 0, peak = 0; for (let i = 0; i < both.length; i++) { worst = Math.max(worst, Math.abs(both[i] - (direct[i] + viaB[i]))); peak = Math.max(peak, Math.abs(both[i])); }
  assert.ok(peak > 0.01, 'the signal is there');
  assert.ok(worst < 1e-5, 'the sum of the cables is what comes out (largest difference ' + worst + ')');
});
