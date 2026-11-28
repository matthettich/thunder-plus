// FX PRESET KEYS (19 Nov, build 653, owner: "pressing Cmd plus left and right on FX presets scrolls through them").
// The stepping code (fxPresetItems ... fxPresetStep), fxPreApply and the undo's snap() are cut out of index.html and run against a stand-in song
// with a controllable clock; the key wiring (bump, keysDown, the key handler's order) is pinned as source.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (re, what) => { const m = src.match(re); assert.ok(m, `index.html: could not find ${what}`); return m[0]; };
const code = cut(/function fxPreApply\(t, k, vals, name\)\{[\s\S]*?\n\}\n/, 'fxPreApply') +
  cut(/function fxPresetItems\(type\)\{[\s\S]*?\nfunction fxPresetOpen\(t, k\)\{/, 'the stepping block').replace(/function fxPresetOpen\(t, k\)\{$/, '') +
  cut(/function snap\(\)\{\n  if \(undoHeld[\s\S]*?\n\}\n/, 'snap') +
  cut(/const UNDO_MAX = [^\n]*\nfunction undoTrim\(\)\{[^\n]*\n/, 'undoTrim (the undo byte budget, build 695)');
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function make() {
  const st = { now: 1000, timers: [], applied: [], toasts: [], pumpCalls: [] };
  const S = { tfx: [[{ type: 'reverb', on: true, big: 100, mix: 50 }, null, null, null], [{ type: 'pumpx', on: true }, null, null, null]], mfx: [{ type: 'reverb', on: true, big: 100, mix: 50 }, null, null, null], fxPre: { reverb: [{ name: 'Song one', v: { big: 7, mix: 9 } }] } };
  const TFX = { reverb: { ps: [{ k: 'big', d: 128, min: 0, max: 255 }, { k: 'mix', d: 100, min: 0, max: 255 }, { k: 'sck', d: 0, min: 0, max: 9 }] }, pumpx: { pump: 'px', ps: [] } };
  const FX_FACTORY = { reverb: [['Hall', { big: 200, mix: 80 }], ['Room', { big: 40 }]] };
  const PumpFX = { presets: () => [{ index: 0, name: 'Clean', section: 'FACTORY' }, { index: 1, name: 'Wet', section: 'SOURCES' }], applyPreset: (t, k, i) => { st.pumpCalls.push([t, k, i]); S.tfx[t][k].pname = ['Clean', 'Wet'][i]; st.save(); return true; } };
  const fxp = { open: false };
  const f = new Function('st', 'S', 'TFX', 'FX_FACTORY', 'PumpFX', 'fxp', 'clamp', 'NT', `
    let apF = null, undoHeld = false, recTake = false, undoQuiet = false; const undo = {past:[], future:[], cur:JSON.stringify(S), busy:false, pend:false};
    const performance = {now: () => st.now}, setTimeout = (fn, ms) => { st.timers.push({fn, ms}); return st.timers.length; };
    const toast = m => st.toasts.push(m), draw = () => {}, fxChanged = t => saveS(), saveS = () => snap(), fxUserList = () => [], MODK = 'Ctrl';
    const fxPreSongList = type => { const P = S.fxPre && S.fxPre[type]; return Array.isArray(P) ? P : []; };
    ${code}
    st.save = saveS;
    return {fxPresetStep, fxPresetItems, fxPkIndex, fxp, undo, setApF: v => { apF = v; }, fxPK};`);
  const api = f(st, S, TFX, FX_FACTORY, PumpFX, fxp, clamp, 32);
  return Object.assign(api, { st, S, fxp });
}
const tick = () => new Promise(r => setImmediate(r));
const settle = async () => { await tick(); await tick(); };

test('the list the keys step through is the picker\'s list: INIT, factory, then this song\'s', () => {
  const m = make(); assert.deepEqual(m.fxPresetItems('reverb').map(p => p.name), ['INIT', 'Hall', 'Room', 'Song one']);
});
test('next / previous, and the ends: stops, does not wrap (as the picker\'s list does)', async () => {
  const m = make(), f = m.S.tfx[0][0];
  m.fxPresetStep(0, 0, 1); await settle(); assert.equal(f.pname, 'INIT');   // a changed ("custom") slot: the first
  m.st.now += 100; m.fxPresetStep(0, 0, 1); await settle(); assert.equal(f.pname, 'Hall'); assert.equal(f.big, 200);
  m.st.now += 100; m.fxPresetStep(0, 0, -1); await settle(); assert.equal(f.pname, 'INIT'); assert.equal(f.big, 128, 'INIT: the defaults');
  m.st.now += 100; m.toasts = m.st.toasts.length = 0; m.fxPresetStep(0, 0, -1); await settle(); assert.equal(f.pname, 'INIT', 'stops at the first'); assert.match(m.st.toasts[0], /First preset/);
  for (let i = 0; i < 5; i++){ m.st.now += 100; m.fxPresetStep(0, 0, 1); await settle(); }
  assert.equal(f.pname, 'Song one'); m.st.now += 100; m.fxPresetStep(0, 0, 1); await settle(); assert.equal(f.pname, 'Song one', 'stops at the last');
  assert.deepEqual([f.big, f.mix], [7, 9]);
  assert.equal(m.fxPkIndex(-1, 4, -1), 0); assert.equal(m.fxPkIndex(2, 4, 1), 3); assert.equal(m.fxPkIndex(3, 4, 1), 3); assert.equal(m.fxPkIndex(0, 0, 1), -1);
});
test('stepping is the same apply as picking: the slot ends up exactly as fxPreApply on that list entry leaves it (master too)', async () => {
  for (const [t, A] of [[0, null], [32, null]]) {
    const m = make(), m2 = make(); const slot = x => (t >= 32 ? x.S.mfx : x.S.tfx[t])[0];
    m.fxPresetStep(t, 0, 1); await settle(); m.st.now += 100; m.fxPresetStep(t, 0, 1); await settle();   // INIT, Hall
    const p = m2.fxPresetItems('reverb')[1]; const g = slot(m2); for (const q of [{ k: 'big', d: 128 }, { k: 'mix', d: 100 }]) g[q.k] = q.k in p.vals ? p.vals[q.k] : q.d; g.pname = p.name;
    assert.deepEqual(slot(m), slot(m2));
  }
});
test('held: presses are not queued; the apply runs at most once per gap and always with the newest target', async () => {
  const m = make(), f = m.S.tfx[0][0];
  m.fxPresetStep(0, 0, 1); await settle();   // INIT, applied at once
  for (let i = 0; i < 3; i++){ m.st.now += 5; m.fxPresetStep(0, 0, 1); }   // 3 presses 5 ms apart, inside the gap
  assert.equal(m.st.timers.length, 1, 'one apply waiting, not three'); assert.equal(f.pname, 'INIT', 'nothing applied yet');
  m.st.now += 40; const t = m.st.timers.pop(); t.fn(); await settle(); assert.equal(f.pname, 'Song one', 'the newest target (INIT + 3)');
});
test('undo: a run of presses is one step back to where the user started; a pause starts a new step', async () => {
  const m = make(), start = JSON.stringify(m.S), n0 = m.undo.past.length;
  for (let i = 0; i < 3; i++){ m.st.now += 120; m.fxPresetStep(0, 0, 1); await settle(); }
  assert.equal(m.undo.past.length, n0 + 1, 'one step for the whole run'); assert.equal(m.undo.past[n0].j, start, 'it holds the state before the first press');
  m.st.now += 5000; m.fxPresetStep(0, 0, -1); await settle(); assert.equal(m.undo.past.length, n0 + 2, 'after a pause: its own step');
  m.st.now += 100; m.fxPresetStep(0, 0, -1); await settle(); assert.equal(m.undo.past.length, n0 + 2, 'and a new run');
});
test('undo run is cut when something else made a step in between', async () => {
  const m = make(); m.st.now += 100; m.fxPresetStep(0, 0, 1); await settle(); m.S.tfx[0][0].big = 1; m.st.save(); await settle(); const n = m.undo.past.length;
  m.st.now += 100; m.fxPresetStep(0, 0, 1); await settle(); assert.equal(m.undo.past.length, n + 1, 'a step of its own, not merged into the edit');
});
test('a pump effect steps its own factory list through PumpFX.applyPreset', async () => {
  const m = make(); m.fxPresetStep(1, 0, 1); await settle(); m.st.now += 100; m.fxPresetStep(1, 0, 1); await settle();
  assert.deepEqual(m.st.pumpCalls, [[1, 0, 0], [1, 0, 1]]); assert.match(m.st.toasts.join('|'), /Preset: Wet \(sources\)/);
});
test('nothing happens with the picker open, with Arrange holding the keys, or on an empty slot', async () => {
  const m = make(); m.fxp.open = true; m.fxPresetStep(0, 0, 1); await settle(); assert.equal(m.S.tfx[0][0].pname, undefined);
  m.fxp.open = false; m.setApF('arr'); m.fxPresetStep(0, 0, 1); await settle(); assert.equal(m.S.tfx[0][0].pname, undefined);
  m.setApF(null); m.fxPresetStep(0, 3, 1); await settle(); assert.equal(m.st.toasts.length, 0);
});

test('source: the key is the value key (bump) on a row with pstep; only ◀▶; the rows carry pstep; every other path is unchanged', () => {
  const b = cut(/function bump\(d, big\)\{[\s\S]*?\n\}\n/, 'bump');
  assert.match(b, /const r = rowsOf\(tv\)\[c\.y\]; if \(r && r\.pstep && !big\) return r\.pstep\(d\);/, '▲▼ (big) never step presets');
  assert.ok(b.indexOf('r.pstep') > b.indexOf("if (tv === 'mixer')") && b.indexOf('r.pstep') > b.indexOf('GRIDV.has(tv)'), 'after the grid and the mixer: those keep their meaning');
  const k = cut(/function keysDown\(e, pad\)\{[\s\S]*?\n\}\n/, 'keysDown');
  assert.match(k, /bump\(a === 'right' \|\| a === 'up' \? 1 : -1, a === 'up' \|\| a === 'down'\)/, 'Ctrl/Cmd + arrows: ◀▶ are bump(±1, false)');
  assert.equal((src.match(/pstep:d => fxPresetStep\(t, k, d\)/g) || []).length, 2, 'built-in and pump PRESET rows');
  const row = cut(/rows\.push\(\{l:'› PRESET' \+ \(f\.pname[^\n]*fxPresetOpen[^\n]*\n/, 'the preset row');
  assert.match(row, /MODK \+ '\+◀▶' \) \? ?|MODK \+ '\+◀▶'/, 'the hint names the key');
});
test('source: Ctrl/Cmd+◀▶ in the slot (parameter rows too) steps the preset; Shift keeps the value nudge; slot list, ACTIVE, other screens keep theirs', () => {
  const k = cut(/function keysDown\(e, pad\)\{[\s\S]*?\n\}\n/, 'keysDown');
  assert.match(k, /if \(tv === 'fx' && \(k === 'ArrowLeft' \|\| k === 'ArrowRight'\) && !e\.shiftKey && !e\.altKey\)\{ const r = rowsOf\('fx'\)\[cs\.fx\.y\]; if \(r && r\.pslot\)\{ r\.pslot\(k === 'ArrowRight' \? 1 : -1\); return true; \} \}/);
  assert.ok(k.indexOf("r.pslot") < k.indexOf("bump(a === 'right'"), 'before the value bump');
  const fr = cut(/function fxRows\(\)\{[\s\S]*?\n\}\n/, 'fxRows');
  assert.equal((fr.match(/pslot:d => fxPresetStep\(t, k, d\)/g) || []).length, 1, 'only the parameter rows');
  assert.match(fr, /for \(const q of TFX\[f\.type\]\.ps\) rows\.push\(\{l:q\.l, lfo:tgtRowId\(t, k, f, q\), pslot:/);
  assert.ok(!/pslot/.test(fr.split("l:'ACTIVE'")[0]), 'the TRACK and SLOT rows keep Ctrl/Cmd+◀▶ (track, effect)');
});
test('stepping from a parameter row leaves the cursor and the row alone: only the slot\'s values and pname change', async () => {
  const m = make(), f = m.S.tfx[0][0]; m.fxPresetStep(0, 0, 1); await settle(); m.st.now += 100; m.fxPresetStep(0, 0, 1); await settle();
  assert.equal(f.pname, 'Hall'); assert.equal(f.big, 200); assert.equal(f.on, true);
});
test('source: text fields never reach it (typing() returns before fileKeys / keysDown) and the handler prevents the browser default', () => {
  const h = cut(/document\.addEventListener\('keydown', e0 => \{[\s\S]*?\n\}\);\n/, 'the tracker key handler');
  assert.ok(h.indexOf('if (typing(e)) return;') >= 0 && h.indexOf('if (typing(e)) return;') < h.indexOf('keysDown('), 'typing first');
  assert.match(h, /if \(keysDown\(e, inMode === 'pad'\)\)\{ e\.preventDefault\(\); return; \}/, 'history back / forward is cancelled where the key is handled');
  assert.match(h, /if \(view !== 'tracker'\) return;/, 'other screens (the Synth page) are untouched');
  assert.match(src, /function typing\(e\)\{[\s\S]{0,400}input:not\(\[type=range\]\)[\s\S]{0,120}textarea,select/, 'inputs, textareas, selects');
});
test('source: Help & keys and the build', () => {
  assert.match(src, /<td>FX presets<\/td><td><kbd>Ctrl\/⌘<\/kbd> <kbd>◀▶<\/kbd> with the cursor anywhere in an effect slot’s controls/);
  assert.match(src, /EDIT \+ ◀▶ on a › PRESET row/); assert.ok(+/const BUILD = (\d+);/.exec(src)[1] >= 653, 'BUILD is this build or a later one'); assert.match(src, /\[653, '19 Nov'/);
  assert.ok(+/thunderplus-v(\d+)/.exec(H.read('sw.js'))[1] >= 653, 'sw cache is this build or a later one');
});
