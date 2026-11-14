// The usability audit's Phase 1 fixes (27 Oct; docs/USABILITY-AUDIT.md, ids K, M, B, V): the code under test is cut out
// of index.html and pump/ui/kit.js at run time, as in app-logic.test.js, so a change that undoes a fix fails here.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');

function cut(rel, re, what) { const m = H.read(rel).match(re); assert.ok(m, `${rel}: could not find ${what}; the code moved or changed shape, so this test needs updating`); return m[0]; }
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function run(src, extra) { const c = Object.assign({ Math, JSON, String, Number, Array, Object, Proxy, clamp, console: { warn() {}, log() {} } }, extra); vm.createContext(c); vm.runInContext(src, c); return c; }

/* ───────── B3: toasts honour their time, and a long one isn't wiped by the next ───────── */
test('toast (B3): honours the time asked for, keeps a long message up, queues the next long ones and shows the latest short one after', () => {
  const blk = cut('index.html', /const toastQ = \[\];[\s\S]*?\nfunction toastNext\(\)\{[\s\S]*?if \(n\) toastShow\(n\[0\], n\[1\], n\[1\] >= 4000\); \}\n/, 'toast, toastShow and toastNext');
  const el = { hidden: true, textContent: '', on: {}, addEventListener(t, f) { this.on[t] = f; } };
  let timers = [];
  const c = run('var tt = 0, busNote = "";\n' + blk + '\nthis.api = {toast, toastNext, st: () => ({q: toastQ.length, long: toastLong})};', {
    $: () => el, setTimeout: (f, ms) => { timers.push({ f, ms }); return timers.length; }, clearTimeout() {} });
  timers.shift().f();   // the start-up hook: a tap on the toast dismisses it
  assert.equal(typeof el.on.click, 'function', 'a tap dismisses');
  const last = () => timers[timers.length - 1], fire = () => { const t = last(); timers = []; t.f(); };
  c.api.toast('Saved');
  assert.equal(el.textContent, 'Saved'); assert.equal(el.hidden, false); assert.ok(last().ms >= 1300 && last().ms <= 2000, 'a short note: 1.3-2 s (' + last().ms + ')');
  c.api.toast('Newer build on this device', 6000);
  assert.equal(el.textContent, 'Newer build on this device', 'a short toast is replaced at once'); assert.equal(last().ms, 6000, 'the time asked for');
  c.api.toast('Loaded song.thunder');
  assert.equal(el.textContent, 'Newer build on this device', 'a short toast does not wipe a long one (it was gone in 2 s)');
  c.api.toast('Second warning', 5000); c.api.toast('Third', 5000); c.api.toast('Fourth', 5000); c.api.toast('Fifth', 5000);
  assert.equal(c.api.st().q, 3, 'at most three long ones wait');
  fire(); assert.equal(el.textContent, 'Second warning', 'long ones in order');
  fire(); fire(); fire(); assert.equal(el.textContent, 'Loaded song.thunder', 'then the latest short one');
  fire(); assert.equal(el.hidden, true, 'then it goes');
  c.api.toast('Panic', 99999); assert.equal(last().ms, 12000, 'capped at 12 s');
  c.api.toastNext(); c.api.toast('Couldn’t save to song.thunder: disk full'); assert.ok(last().ms >= 4000, 'an error stays up 4 s or more (' + last().ms + ')');
  el.on.click(); assert.equal(el.hidden, true, 'a tap dismisses it');
  assert.match(H.read('index.html'), /<div class="toast" id="toast" role="status" aria-live="polite"/, 'screen readers hear it');
});

/* ───────── M2: the mouse wheel adds up movement (a trackpad flick no longer runs a slider end to end) ───────── */
test('wheel (M2): host sliders take a step per 100 px (a mouse notch), trackpad deltas add up, and a pause or turn starts again', () => {
  const blk = cut('index.html', /const wheelAcc = new WeakMap\(\);\nfunction wheelSteps\(key, e, per = 100\)\{[\s\S]*?\n\}\n/, 'wheelSteps');
  let now = 1000; const c = run('var wheelSign = 1;\n' + blk + '\nthis.wheelSteps = wheelSteps; this.sign = v => { wheelSign = v; };', { WeakMap, performance: { now: () => now } });
  const k1 = {}, k2 = {}, sum = (k, ev, n, dt = 8) => { let s = 0; for (let i = 0; i < n; i++){ now += dt; s += c.wheelSteps(k, ev); } return s; };
  assert.equal(c.wheelSteps(k1, { deltaY: 100, deltaMode: 0 }), 1, 'a mouse notch: one step');
  now += 500; assert.equal(sum(k1, { deltaY: 6, deltaMode: 0 }, 60), 3, 'a trackpad flick of 60 events x 6 px: 3 steps (was 60)');
  now += 500; assert.equal(c.wheelSteps(k1, { deltaY: 3, deltaMode: 1 }), 1, 'Firefox lines: 3 lines is a notch');
  now += 500; assert.equal(sum(k1, { deltaY: 60, deltaMode: 0 }, 1) + sum(k1, { deltaY: -60, deltaMode: 0 }, 1), 0, 'turning back starts again, no stray step');
  now += 500; c.wheelSteps(k1, { deltaY: 60, deltaMode: 0 }); now += 400; assert.equal(c.wheelSteps(k1, { deltaY: 60, deltaMode: 0 }), 0, 'a pause drops the rest');
  now += 500; c.wheelSteps(k1, { deltaY: 60, deltaMode: 0 }); assert.equal(c.wheelSteps(k2, { deltaY: 60, deltaMode: 0 }), 0, 'each slider adds up its own');
  now += 500; assert.equal(c.wheelSteps(k1, { deltaY: -5000, deltaMode: 0 }), -8, 'one huge event: at most 8 steps');
  assert.equal(c.wheelSteps(k1, { deltaY: 0, deltaX: 0 }), 0);
  // ONE WHEEL DIRECTION (owner, 7 Nov): with macOS Natural scrolling the deltaY comes flipped, so the steps flip back with it
  c.sign(-1); now += 500; assert.equal(c.wheelSteps(k1, { deltaY: -100, deltaMode: 0 }), 1, 'Natural scrolling: pulling down (deltaY < 0) still steps down');
  assert.match(H.read('index.html'), /function wheelNotch\(key, e\)\{\n  const raw = \(e\.deltaY \|\| e\.deltaX\) \* wheelSign,/, 'rows and tracker values flip too');
  assert.match(H.read('index.html'), /const dy = \(e\.deltaMode === 1 \? e\.deltaY \* 40 : e\.deltaY\) \* wheelSign;/, 'Arrange flips too');
  assert.match(H.read('electron/preload.js'), /naturalScroll: \(\) => ipcRenderer\.invoke\('thunder:natural-scroll'\)/, 'the desktop app reads the setting');
});

test('wheel (M2): …pump windows do the same for their knobs (kit.js wheelN)', () => {
  const m = cut('pump/ui/kit.js', /  wheelN\(e\)\{[\s\S]*?\n  \}\n/, 'Win.wheelN');
  let now = 1000; const c = run('this.w = {' + m + '};', { performance: { now: () => now } });
  let s = 0; for (let i = 0; i < 60; i++){ now += 8; s += c.w.wheelN({ deltaY: -8, deltaMode: 0 }); }
  assert.equal(s, -4, 'a 480 px flick: 4 steps (was 60)');
  now += 500; assert.equal(c.w.wheelN({ deltaY: 100, deltaMode: 0 }), 1);
  assert.match(H.read('pump/ui/kit.js'), /for \(let k = 0; k < Math\.abs\(n\); k\+\+\) ok = c\.wheel\(this, dy, m\) \|\| ok;/, 'a knob takes each step');
});

/* ───────── K6: AltGr symbols reach the tracker ───────── */
test('AltGr (K6): { } [ ] \\ typed with AltGr (Ctrl+Alt on Windows) arrive without Ctrl and Alt; Ctrl+Alt+letters keep them', () => {
  const blk = cut('index.html', /const altGrSym = e => [^\n]*\nconst noAltGr = e => [^\n]*\n/, 'altGrSym and noAltGr');
  const c = run(blk + '\nthis.noAltGr = noAltGr;');
  const ev = (key, altGr) => ({ key, ctrlKey: true, altKey: true, shiftKey: false, getModifierState: m => altGr && m === 'AltGraph', pd: 0, preventDefault() { this.pd++; } });
  for (const k of ['{', '}', '[', ']', '\\', '|', '@']){
    const e0 = ev(k, true), e = c.noAltGr(e0);
    assert.equal(e.ctrlKey, false, k + ': no Ctrl'); assert.equal(e.altKey, false, k + ': no Alt'); assert.equal(e.key, k);
    e.preventDefault(); assert.equal(e0.pd, 1, k + ': preventDefault reaches the real event');
  }
  for (const k of ['c', 'C', 'd', '1']) assert.equal(c.noAltGr(ev(k, true)).ctrlKey, true, k + ': Ctrl+Alt+' + k + ' is still a command (clone, duplicate)');
  const plain = ev('[', false); assert.equal(c.noAltGr(plain), plain, 'without AltGr nothing changes');
});

/* ───────── B18: F8 (Stop) again = panic ───────── */
test('panic (B18): Stop while playing stops; Stop again lets every note go and sends All Notes Off on each MIDI port', () => {
  const blk = cut('index.html', /function stopKey\(\)\{[^\n]*\nfunction panic\(\)\{[\s\S]*?\n\}\n/, 'stopKey and panic');
  const log = { stop: 0, all: 0, mo: 0, sent: [], toast: [] };
  const c = run(blk + '\nthis.stopKey = stopKey;', { tp: { on: true }, tStop: () => log.stop++, ctx: { currentTime: 0 }, rt: { pl: { stopAll: () => log.all++ } },
    moRelease: () => log.mo++, NPORT: 4, midiOut: p => p === 0 || p === 2, msend: (m, t, x, p) => log.sent.push([p, ...m]), toast: (m, ms) => log.toast.push([m, ms]), tpLog() {}, warmTok: null, lpRestore() {}, lpSelRestore() {}, draw() {} });
  c.stopKey(); assert.equal(log.stop, 1); assert.equal(log.sent.length, 0, 'playing: just stops');
  c.tp.on = false; c.stopKey();
  assert.equal(log.all, 1, 'the synths let go'); assert.equal(log.mo, 1, 'held MIDI notes too');
  assert.equal(log.sent.length, 2 * 16 * 3, 'two ports x 16 channels x (sustain off, all sound off, all notes off)');
  assert.deepEqual([...new Set(log.sent.map(m => m[2]))].sort((a, b) => a - b), [64, 120, 123]);
  assert.deepEqual([...new Set(log.sent.map(m => m[0]))], [0, 2], 'only the ports that are open');
  assert.match(log.toast[0][0], /2 MIDI ports/); assert.match(H.read('index.html'), /else stopKey\(\);/, 'F8 goes through stopKey');
});

/* ───────── K2 / K4: rows move inside the phrase; Delete on a selection clears it (the clipboard stays) ───────── */
test('rows (K2, K4): Insert and Ctrl+Backspace move rows only inside the phrase\'s LEN and keep its 32 rows; with a selection they clear it', () => {
  const blk = cut('index.html', /function rowOp\(op\)\{[\s\S]*?\n  saveS\(\); draw\(\);\n\}\n/, 'rowOp');
  const mk = () => Array.from({ length: 32 }, (_, i) => 'r' + i);
  let A = mk(); const log = { clear: 0, copy: 0 };
  const c = run('var tv = "phrase", selA = null;\n' + blk + '\nthis.rowOp = rowOp; this.set = (k, v) => { if (k === "selA") selA = v; };', {
    onPset: () => false, cs: { phrase: { x: 0, y: 2 } }, rowsArr: () => A, plen: () => 16, cPhrase: 0, rowBlank: () => '-', down1() {}, saveS() {}, draw() {}, del() {},
    clearSel: () => log.clear++, copySel: () => log.copy++ });
  c.rowOp('insert');
  assert.equal(A.length, 32, 'the phrase keeps its 32 rows (it was cut to 16)');
  assert.deepEqual(A.slice(0, 5), ['r0', 'r1', '-', 'r2', 'r3']); assert.equal(A[15], 'r14', 'row 15 pushed out of the LEN goes');
  assert.deepEqual(A.slice(16), mk().slice(16), 'rows kept past the LEN stay put');
  A = mk(); c.rowOp('remove');
  assert.equal(A.length, 32); assert.deepEqual(A.slice(0, 4), ['r0', 'r1', 'r3', 'r4']); assert.equal(A[15], '-', 'a blank comes in at the LEN'); assert.deepEqual(A.slice(16), mk().slice(16));
  A = mk(); c.cs.phrase.y = 20; c.rowOp('insert'); assert.deepEqual(A, mk(), 'past the LEN: nothing moves');
  c.set('selA', { x: 0, y: 0 }); c.rowOp('clear'); assert.equal(log.clear, 1); assert.equal(log.copy, 0, 'a selection is cleared, not cut (the clipboard stays)');
  assert.match(H.read('index.html'), /function del\(\)\{\n[\s\S]{0,900}?if \(selA\) return clearSel\(\);/, 'Delete on a selection clears it too');
  assert.doesNotMatch(cut('index.html', /function clearSel\(\)\{[\s\S]*?\n\}\n/, 'clearSel'), /\bclip\b/, 'clearSel leaves the clipboard alone');
});

/* ───────── the rest: guards whose shape is the fix ───────── */
test('guards: KEYS mode modifiers, the Synth page keys, dialogs over Settings, unsaved-change marks, Open, leaving the page', () => {
  const src = H.read('index.html');
  // K1: in KEYS mode the PAD table (Shift/Z/X as held modifiers) is never reached, so nothing stays held
  assert.match(src, /if \(e\.ctrlKey \|\| e\.metaKey \|\| e\.altKey\) return;\n(?:\s*\/\/[^\n]*\n)*\s*if \(inMode === 'keys'\) return;\n\s*const b = KEYB\[e\.key\];/, 'K1: KEYS mode stops before the PAD table');
  assert.match(src, /const b = KEYB\[e\.key\]; if \(b && \(inMode !== 'keys' \|\| b in held\)\)\{ e\.preventDefault\(\); release\(b, true\); \}/, 'K1: and a held PAD key is let go in either mode');
  // K8: the Synth page's keys only on the Synth page
  assert.match(src, /\n  if \(view !== 'synth'\) return;   \/\/ FIX \(27 Oct, audit K8\)[^\n]*\n[\s\S]{0,400}const pd = \/\^Digit\[1-8\]\$\//, 'K8');
  // 1 Nov (audit #1): F2–F9 on the Synth page (F5 reloaded the page there)
  assert.match(src, /F-KEYS OFF THE TRACKER[\s\S]{0,600}\/\^F\[2-9\]\$\/\.test\(e\.key\)/, 'Synth page F-keys');
  // B12: a dialog sits over the Settings menu and gets its keys
  assert.match(src, /\.ask\{[^}]*z-index:125;/, 'B12: .ask above .setmenu (120)');
  assert.ok(+src.match(/\.ask\{[^}]*z-index:(\d+);/)[1] > +src.match(/\.setmenu\{[^}]*z-index:(\d+)/)[1], 'B12: above the menu');
  // B1: kit edits mark the project unsaved
  assert.match(src, /function setKit\([^)]*\)\{\s*try \{ markChange\(\); \} catch\(e\)\{\}/, 'B1: setKit');
  assert.match(src, /function wavesChanged\(edit\)\{ if \(edit\) try \{ markChange\(\); \}/, 'B1: …waves edits');
  // B2: Open starts a fresh undo history (undo can't bring the old song back over the new file)
  assert.match(src, /function undoReset\(\)\{ undo\.past\.length = 0; undo\.future\.length = 0; undo\.cur = JSON\.stringify\(S\);/, 'B2: undoReset');
  assert.match(src, /setTimeout\(undoReset, 0\);/, 'B2: Open calls it');
  // B8: the browser asks before leaving with unsaved changes (not on our own update reload, not in the desktop app)
  assert.match(src, /addEventListener\('beforeunload', e => \{ if \(selfReload \|\| DESK \|\| !projDirty\) return; e\.preventDefault\(\); e\.returnValue = ''; \}\);/, 'B8');
  assert.match(src, /selfReload = true;/, 'B8: the update reload passes');
  // M17: only the left button drags a mixer fader
  assert.match(src, /closest\('\.mfbar:not\(\.mobar\)'\); if \(!b \|\| tv !== 'mixer'\) return;[^\n]*\n  if \(e\.button !== 0 \|\| ctxPress\(e\)\) return;/, 'M17');   // (27 Oct: the master OUT bar has its own handler)
  assert.match(src, /closest\('\.mobar'\); if \(!b \|\| tv !== 'mixer' \|\| e\.button !== 0 \|\| ctxPress\(e\)\) return;/, 'M17: the master OUT bar too');
  // M3: a …chains slider row in the bottom panel opens one menu (its own, with the panel items), not that and a modal
  assert.match(src, /makeRow\(def, x\.p, getObj, \{[^}]*menu:\(\) => dkcItems\(x\)\}\);/, 'M3: the slider row carries the items');
  assert.match(src, /if \(Array\.isArray\(x\.o\) && x\.o\.length\) r\.addEventListener\('contextmenu', e => \{ e\.preventDefault\(\); dkcMenu\(x, e\); \}\);/, 'M3: only list rows get the second menu');
  assert.match(src, /function dkcMenu\(d, e\)\{ sxMenu\(/, 'M3: and it is a menu, not a modal');
  // V1: narrow desktop windows put the transport on its own row
  assert.match(src, /\.ttabbar\.wrap2\{flex-wrap:wrap\}/, 'V1');
  // the build and the service worker cache agree (a stale cache would keep the old fixes out)
  const b = src.match(/const BUILD = (\d+);/)[1]; assert.match(H.read('sw.js'), new RegExp("const CACHE = 'thunderplus-v" + b + "'"));
});

/* ───────── K3: the docs say what the keys do now ───────── */
test('docs (K3): Help, the key legend, the tour and the README match the keys', () => {
  const src = H.read('index.html'), rd = H.read('README.md');
  const help = src.slice(src.indexOf('<section id="hAll">'), src.indexOf('<section id="hPad">'));
  assert.doesNotMatch(help, /<kbd>Ctrl\/⌘<\/kbd> <kbd>Shift<\/kbd> <kbd>arrows<\/kbd><\/td><td>Change screen/, 'screens are Alt/⌥+arrows');
  assert.doesNotMatch(help, /CLN↓ \(Song\)<\/td><td><kbd>Ctrl\/⌘<\/kbd> <kbd>D<\/kbd>/, 'CLN is Ctrl/⌘+Alt+C (Ctrl+D duplicates)');
  assert.match(help, /<kbd>Ctrl\/⌘<\/kbd> <kbd>T<\/kbd>/, 'Ctrl+T listed'); assert.match(help, /<kbd>Ctrl<\/kbd> <kbd>Space<\/kbd>/, 'Ctrl+Space listed');
  assert.match(help, /Mute or unmute the track/, '/ and \\ listed'); assert.match(help, /F8<\/kbd> again/, 'panic listed');
  assert.match(src, /id="tLegSpace"/); assert.match(src, /playKeySwap\(\) \? '<kbd>Space<\/kbd> plays the song/, 'the tour follows the Space order');
  assert.doesNotMatch(rd, /Delete clears the whole row and steps down; Shift\+Delete clears just the cell/, 'README: Delete clears the cell');
});

/* ───────── Phase 2 (27 Oct, the owner's calls): one meaning for Space, Shift, drag and hold ───────── */
test('Phase 2 keys and mouse: Space = the song everywhere, Shift = fine, a drag selects (Alt-drag edits), hold-to-mute is touch only', () => {
  const src = H.read('index.html');
  assert.match(src, /function playKeySwap\(\)\{ return true; \}/, 'Space plays the song in every mode');
  assert.match(src, /if \(e\.shiftKey && view === 'synth'\)\{ play\(cur\); return; \}\n\s*if \(tp\.on\) tStop(?:User)?\(\); else tStart\('song', \{row:songRow\(\)\}\); return; \}/, 'Synth page: Space the song, Shift+Space the pad');
  assert.doesNotMatch(src, /if \(e\.key === ' ' && !e\.target\.matches\('button'\)\)\{ play\(cur\);/, 'the old Synth-page Space = pad is gone');
  assert.match(src, /drag\.pos = \(drag\.pos \|\| 0\) \+ \(drag\.lastY - e\.clientY\) \/ \(e\.shiftKey \? 48 : 16\);\n\s*drag\.lastY = e\.clientY; drag\.big = !!\(e\.ctrlKey \|\| e\.metaKey\);/, 'Shift slows a value drag, Ctrl/⌘ takes big steps');
  assert.match(src, /bump\(n < 0 \? 1 : -1, false\);/, 'the wheel no longer takes big steps with Shift');
  assert.match(src, /if \(e\.altKey && y >= 0 && !inSel && altDrag\(e, x, y\)\) return;/, 'Alt-drag edits (gridDown)');
  assert.match(src, /if \(x != null && e\.altKey && altDrag\(e, x, m\.y\)\) return;/, 'Alt-drag edits (Pattern view)');
  assert.match(src, /const filled = was && getCell\(tv, x, y\) >= 0;/, 'the cursor\'s own cell selects too');
  assert.match(src, /if \(d\.altMute && !d\.moved && e\.type === 'pointerup'\)\{ toggleBlockMute/, 'an Alt-click on the Song still mutes');
  assert.match(src, /const holdOk = e\.pointerType !== 'mouse';/, 'Arrange hold-to-mute: touch and pen only');
});

test('Phase 2 sliders and wheel: relative drag, Shift finer, touch must slide, Alt/Ctrl-click resets; the wheel waits for the pointer to rest; editors scroll', () => {
  const src = H.read('index.html');
  assert.match(src, /v = clamp\(v \+ \(ev\.clientX - lastX\) \/ w \* \(mx - mn\) \* dragMul\(ev\), mn, mx\); lastX = ev\.clientX;/, 'relative, re-anchored each move (Shift mid-drag never jumps)');
  assert.match(src, /const dragMul = e => e\.shiftKey \? 0\.1 : \(e\.ctrlKey \|\| e\.metaKey\) \? 4 : 1;/, 'ONE RULE: Shift a tenth, Ctrl/⌘ ×4');
  assert.match(src, /if \(Math\.abs\(dx\) < 6\) return; live = true;/, 'touch: nothing until it slides sideways');
  assert.match(src, /if \(!touch && \(e\.altKey \|\| \(MACK \? e\.metaKey : e\.ctrlKey\)\)\)\{ sldReset\(r\); return; \}/, 'Alt / Ctrl / ⌘-click resets');
  assert.match(src, /inp\._reset = reset; inp\._out = out;/, 'makeRow gives the reset and the readout');
  assert.match(src, /@media \(pointer:coarse\)\{ input\[type=range\]:not\(:is\(#gset, \.rlock\) \*\)\{pointer-events:none\} \}/, 'no native jump on touch');
  assert.match(src, /if \(r !== wheelR\)\{ if \(r === wheelCand\) wheelArm\(r\); return; \}/, 'the wheel scrolls past until the slider is armed');
  for (const f of ['waves/index.html', 'chains/index.html']) {
    const w = H.read(f);
    assert.match(w, /if \(e\.ctrlKey \|\| e\.metaKey\)\{ e\.preventDefault\(\); if \(inGesture\) return; zoomAt\(/, f + ': Ctrl/⌘+wheel zooms');
    assert.doesNotMatch(w, /if \(mouse\)\{ zoomAt\(/, f + ': a plain mouse wheel no longer zooms');
  }
});

test('Phase 2 editors: app keys pass up, Ctrl+S/O/N are the project, one undo history with the song', () => {
  const src = H.read('index.html');
  assert.match(src, /function editorKey\(e\)\{/, 'the app keys from the editors');
  assert.match(src, /appKey: e => editorKey\(e\),[\s\S]*appKey: e => editorKey\(e\),/, 'both editors get them');
  assert.match(src, /undo: dir => undoStep\(dir\), canUndo: \(\) => undo\.past\.length > 0/, 'their undo is the song\'s');
  assert.match(src, /if \(e\.k === 'W'\)\{/, '…waves patch edits are steps in the song\'s history');
  assert.match(src, /if \(a && a\.setDoc && CH\.frame\) try \{ a\.setDoc\(S\.chainPatch\); ok = true; \}/, 'undo shows the patch in the open editor (no reload)');
  for (const f of ['waves/index.html', 'chains/index.html']) {
    const w = H.read(f);
    assert.match(w, /function undo\(\)\{ if \(sharedUndo\(\)\)\{ flushToHost\(\); TW\.undo\(-1\); return; \}/, f + ': Undo goes to the song');
    assert.match(w, /if \(EMBED && this\.dirty\)\{ clearTimeout\(this\._timer\); this\.save\(\); \}/, f + ': each edit hands off before the next (a step each)');
    assert.match(w, /setDoc\(d\)\{ if \(!store\) return;/, f + ': setDoc');
    assert.match(w, /if \(TW\.appKey\(\{key:e\.key/, f + ': APP KEYS');
  }
});

test('Phase 2 editors: modules take Max/MSP edit-mode gestures', () => {
  for (const f of ['waves/index.html', 'chains/index.html']) {
    const w = H.read(f);
    assert.match(w, /if \(maxOperate\(e\)\) return;   \/\/ MAX: Ctrl\/⌘-click works the module/, f + ': Ctrl/⌘-click never selects');
    assert.match(w, /if \(e\.altKey && maxHelpPend\(e, n\)\) return;/, f + ': Alt-click opens help (Alt-drag copies)');
    assert.match(w, /const box = e\.button === 0 && \(e\.shiftKey \|\| selMode\) && !playMode;[^\n]*\n  if \(box\)\{/, f + ': a Shift-drag on the background is a selection box (a plain drag pans, 3 Nov)');
    assert.match(w, /if \(\(e\.key !== 'Delete' && e\.key !== 'Backspace'\)[^\n]*\n[^\n]*\n  e\.preventDefault\(\); store\.snapshot\(\); const ids = \[\.\.\.selected\]; for \(const x of ids\) removeModule\(x\);/, f + ': Delete removes the selection');
    assert.match(w, /n\.el\.addEventListener\('pointerdown', e => \{ if \(e\.button === 1 && e\.pointerType === 'mouse'\) return; if \(playMode && !\(e\.target\.closest && e\.target\.closest\(PLAY_CTL\)\)\) return;[^\n]*\n[^\n]*e\.stopPropagation\(\); n\.el\.style\.zIndex = \+\+zTop; if \(MAC && e\.ctrlKey && e\.pointerType === 'mouse'\) return; maxBodyDown\(e, n\); \}\);/, f + ': a click on a module selects it');
  }
});

test('Phase 2 rest: one menu (press-drag-release, pads and layers), one confirm, note keys by position, one undo step per gesture, touch sizes, pinch-zoom', () => {
  const src = H.read('index.html');
  assert.match(src, /if \(ptrHeld\)\{ const t0 = performance\.now\(\)/, 'menus pick on release');
  assert.match(src, /function ctxBind\(el, fn, opts = \{\}\)\{/, 'right-click and long-press menus');
  assert.match(src, /ctxBind\(\$\('pads'\)/, 'pads'); assert.match(src, /ctxBind\(\$\('strip'\)/, 'layers');
  assert.match(src, /function ask\(msg, fn\)\{ askYes\(msg, 'Yes'\)\.then/, 'the inline Yes / Cancel row is the dialog');
  assert.doesNotMatch(src, /'Tap again: ' \+ it\[0\]/, 'no "Tap again" in the File menu');
  assert.doesNotMatch(src, /return toast\('Tap EDIT again to '/, 'no "Tap EDIT twice"');
  assert.match(src, /const PIANO_CODE = \{KeyZ:0, KeyS:1,/, 'note keys by e.code');
  assert.match(src, /const pn = e\.code === 'Slash' \? 16 : pianoOf\(e\);   \/\/ NOTE KEYS BY POSITION/, 'Synth page too');
  assert.match(H.read('waves/index.html'), /const keyOf = e => K_CODE\[e\.code\]/, '…waves too');
  assert.match(src, /if \(undoHeld && !undo\.busy && !recTake\)\{ undoWant = true; return; \}/, 'one step per gesture');
  assert.match(src, /@media \(pointer:coarse\)\{\n  \.mfbar::before\{top:-19px; bottom:-19px\}/, '40 px touch targets');
  assert.match(src, /<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">/, 'pinch-zoom allowed');
  assert.match(src, /html\{-webkit-touch-callout:none; touch-action:manipulation;/, 'pinch-zoom allowed (CSS)');
  assert.match(src, /\.pads, \.pad\{touch-action:pan-x pan-y\}/, 'but not on the pads');
});

test('owner\'s queue (27 Oct): CLN↓ inserts a row, Remove row, a bus back to a normal track, chains of a CHN-off track drawn dashed', () => {
  const src = H.read('index.html'), ch = H.read('chains/index.html');
  assert.match(src, /if \(clash && !songRowsInsert\(d0, h\)\) return toast/, 'CLN↓ inserts new rows below only when a clone would land on a block (2 Nov)');
  assert.match(src, /function songRowRemove\(rr\)\{/, 'Remove row');
  assert.match(src, /songRowRemove\(rr\); sxPicks = \[\];/, 'the menu passes the selected rows (2 Nov fix)');
  assert.match(src, /Remove row ' \+ rS\(r\.y0, 3\) \+ ' \(the rows below move up\)'/, 'in the Song / Arrange menu');
  assert.match(src, /function unBus\(t\)\{/, 'bus back to a track');
  assert.match(src, /\{a:'unbus', l:'Make this track a normal track \(not a bus\)'/, 'in the track menus');
  assert.match(src, /chainOn: \(\) => Array\.from\(\{length:NT\}/, 'the editor can ask which chains are heard');
  assert.match(ch, /function paintChainOff\(\)\{/, 'the editor draws CHN-off chains');
  assert.match(ch, /\.node\.chnoff\{outline:2px dashed/, 'dashed');
});

test('Phase 2 review fixes: slow right-click, editor hand-offs, double EDIT, empty cursor cell, a stuck pointer', () => {
  const src = H.read('index.html');
  assert.match(src, /if \(Math\.hypot\(ev\.clientX - x0, ev\.clientY - y0\) < 6\) return;   \/\/ let go without moving/, 'a menu item needs a slide');
  assert.match(src, /function patchKey\(d\)\{/, 'view-only hand-offs are no step');
  assert.match(src, /if \(performance\.now\(\) - t0 < 400\)\{ if \(opts\.dblOk\) done\(true\); return; \}/, 'a double EDIT goes through');
  assert.match(src, /else if \(m\.was && !m\.filled && \(tv === 'song' \|\| tv === 'sect'\)\) editTap\(\);/, 'an empty cursor cell click adds');
  assert.match(src, /addEventListener\('keydown', \(\) => \{ if \(undoHeld\) undoLetGo\(\); \}, true\);/, 'a key lets a stuck pointer go');
});

/* ───────── 3 Nov: the wheel over a scroll bar scrolls that bar (sbHit / sbWheel) ───────── */
test('wheel on a scrollbar (3 Nov): scrolls the bar it is over and stops the app\'s own wheel; off the bar nothing changes', () => {
  const blk = cut('index.html', /function sbHit\(el, x, y\)\{[\s\S]*?\naddEventListener\('wheel', sbWheel, \{capture:true, passive:false\}\);\n/, 'sbHit, sbWheel and their listener');
  let reg = null;
  // a 200×100 box at (10, 20) with a 1 px border, a 14 px vertical bar and a 10 px horizontal bar
  const el = { nodeType: 1, clientWidth: 184, clientHeight: 88, offsetWidth: 200, offsetHeight: 100, scrollTop: 0, scrollLeft: 0,
    getBoundingClientRect: () => ({ left: 10, top: 20, right: 210, bottom: 120 }) };
  const style = { borderLeftWidth: '1px', borderTopWidth: '1px', borderRightWidth: '1px', borderBottomWidth: '1px', overflowY: 'auto', overflowX: 'auto' };
  const c = run(blk + '\nthis.api = {sbHit, sbWheel};', { getComputedStyle: () => style, addEventListener: (t, f, o) => { reg = { t, f, o }; },
    document: { documentElement: {}, body: {}, scrollingElement: null }, innerWidth: 1600, innerHeight: 1000 });
  assert.equal(reg.t, 'wheel'); assert.equal(reg.o.capture, true, 'capture: before the app\'s handlers'); assert.equal(reg.o.passive, false);
  // inside the border: x 11..195 content, 195..209 the vertical bar; y 21..109 content, 109..119 the horizontal bar
  assert.equal(c.api.sbHit(el, 100, 60), '', 'the content is no bar');
  assert.equal(c.api.sbHit(el, 200, 60), 'v', 'the right strip is the vertical bar');
  assert.equal(c.api.sbHit(el, 100, 114), 'h', 'the bottom strip is the horizontal bar');
  assert.equal(c.api.sbHit(el, 200, 114), '', 'the corner is neither');
  assert.equal(c.api.sbHit(el, 209.5, 60), '', 'the border is no bar');
  const ev = (x, y, o = {}) => Object.assign({ target: el, clientX: x, clientY: y, deltaX: 0, deltaY: 100, deltaMode: 0, ctrlKey: false, defaultPrevented: false,
    stopped: false, prevented: false, preventDefault() { this.prevented = true; }, stopImmediatePropagation() { this.stopped = true; } }, o);
  let e = ev(200, 60); c.api.sbWheel(e);
  assert.equal(el.scrollTop, 100); assert.ok(e.stopped && e.prevented, 'the app\'s handlers never see it');
  e = ev(100, 114); c.api.sbWheel(e); assert.equal(el.scrollLeft, 100, 'a horizontal bar: the wheel\'s up/down scrolls sideways');
  e = ev(100, 114, { deltaY: 0, deltaX: 3, deltaMode: 1 }); c.api.sbWheel(e); assert.equal(el.scrollLeft, 220, 'Shift+wheel (deltaX), in lines');
  e = ev(100, 60); c.api.sbWheel(e); assert.ok(!e.stopped && !e.prevented, 'off the bar the event goes on untouched');
  e = ev(200, 60, { ctrlKey: true }); c.api.sbWheel(e); assert.ok(!e.stopped, 'Ctrl+wheel (zoom) is left alone');
  style.overflowY = 'hidden'; assert.equal(c.api.sbHit(el, 200, 60), '', 'no overflow, no bar');
  el.offsetWidth = 186; style.overflowY = 'auto'; assert.equal(c.api.sbHit(el, 196, 60), '', 'an overlay bar (no room) is not taken');
});

test('mouse audit (3 Nov): Mac Ctrl+click is the menu, middle-click mutes, top menus switch, a notch is a row, editor sliders', () => {
  const src = H.read('index.html');
  assert.match(src, /const ctxPress = e => e\.button === 2 \|\| \(MACK && e\.ctrlKey && e\.button === 0 && e\.pointerType !== 'touch'\);/, 'ctxPress');
  assert.match(src, /if \(ctxPress\(e\)\) return true;   \/\/ a right-click \(Ctrl\+click on a Mac/, 'Arrange');
  assert.match(src, /MIDDLE-CLICK \(mouse audit, 3 Nov\)[\s\S]{0,500}toggleBlockMute\(y, x\)/, 'Song middle-click mutes');
  assert.match(src, /!onTopMenuBtn\(e\.target\)\)\{ gfileToggle\(false\); swallowTap\(e\); \}/, 'File: another menu button is not outside');
  assert.match(src, /!onTopMenuBtn\(e\.target\)\)\{ geditToggle\(false\); swallowTap\(e\); \}/, 'Edit too');
  assert.match(src, /b\.addEventListener\('pointerenter', e => \{ if \(e\.pointerType !== 'mouse'/, 'hover switches menus');
  assert.match(src, /const n = wheelNotch\(WK_ROWS, e\); if \(!n\) return;/, 'a row a notch');
  assert.match(src, /if \(e\.ctrlKey && !e\.altKey\) return;   \/\/ CTRL\+WHEEL/, 'Ctrl+wheel moves no cursor');
  assert.match(src, /input:not\(\[type=range\]\)/, 'no browser menu on sliders');
  for (const f of ['waves/index.html', 'chains/index.html']) {
    const w = H.read(f);
    assert.match(w, /ONE SLIDER \(mouse audit, 3 Nov[\s\S]{0,5000}if \(e\.altKey \|\| \(MAC \? e\.metaKey : e\.ctrlKey\)\)\{ reset\(\); return; \}[\s\S]{0,2500}ev\.shiftKey \? 0\.1/, f + ': the host slider rule');
    assert.match(w, /if \(ev\.pointerType === 'mouse' && ev\.buttons === 0 && !drag\.drawing && !drag\.armed\)/, f + ': a cable let go outside ends');
    assert.match(w, /\['Remove cable', \(\) =>/, f + ': Remove cable');
  }
});

test('one rule for value controls (3 Nov): faders, strip, FX bar and pump drag relative; double-click resets; right-click menu; wheel after the rest', () => {
  const src = H.read('index.html');
  assert.match(src, /mfDrag = \{t, id:e\.pointerId, ly:e\.clientY, vy:mfY\(/, 'VOL fader relative');
  assert.match(src, /D\.vy = clamp\(D\.vy \+ \(e\.clientY - D\.ly\) \* dragMul\(e\)/, 'VOL fader: dragMul each move');
  assert.match(src, /D\.vx = clamp\(D\.vx \+ \(e\.clientX - D\.lx\) \* dragMul\(e\)/, 'OUT bar: dragMul each move');
  assert.match(src, /base = clamp\(base \+ \(from - ev\.clientY\) \/ h \* 255 \* dragMul\(ev\), 0, 255\); from = ev\.clientY;/, 'strip fader');
  assert.match(src, /x = clamp\(x \+ \(ly - ev\.clientY\) \/ per \* dragMul\(ev\), q\.min, q\.max\); ly = ev\.clientY;/, 'FX bar values');
  assert.match(src, /out\.addEventListener\('dblclick', e => \{ e\.stopPropagation\(\); reset\(\); \}\);/, 'a slider readout double-click resets');
  assert.match(src, /valMenu\(e, q\.l, \{reset:\(\) => set\(q\.d\), type:typeIt, lfo:[^}]*\}\)/, 'FX bar menu (and its Add LFO…)');
  assert.match(src, /const wheelOk = r => !!r && \(r\.hasAttribute\('data-wheel'\)/, 'data-wheel controls take the wheel after the rest');
  const k = H.read('pump/ui/kit.js');
  assert.match(k, /const dragMul = m => m\.shift \? 0\.1 : m\.cmd \? 4 : 1;/, 'pump: dragMul');
  assert.match(k, /'Reset to default', 'Type a value…'[,\]]/, 'pump: right-click menu');
  assert.match(k, /if \(c !== this\.wheelC\) return;/, 'pump: the wheel waits for the rest');
});

test('owner calls (3 Nov): Song Alt-drag copies, Arrange Ctrl-click picks / Ctrl-drag copies, Song track numbers mute, bus outline', () => {
  const src = H.read('index.html');
  assert.match(src, /SONG ALT-DRAG COPIES[\s\S]{0,900}if \(mselDown\(e, x, y\)\)\{ msel\.one = true; draw\(\); return; \}/, 'Song Alt-drag');
  assert.match(src, /sxDrag = \{id:e\.pointerId, x0:e\.clientX, y0:e\.clientY, y, t, mode:null, alt:false, pick:true,/, 'Arrange Ctrl press waits');
  assert.match(src, /if \(!d\.mode && d\.pick\)\{/, 'a Ctrl-click picks on release');
  assert.match(src, /TRACK NUMBERS \(owner, 3 Nov\)/, 'Song track numbers mute');
  assert.match(src, /\.agbus\{position:absolute/, "bus frame");
});

test('vintage sampler (3 Nov): a JS track effect wired into the table, groups, worklet and its Blob', () => {
  const src = H.read('index.html');
  assert.match(src, /class VSampCore \{/, 'the core');
  assert.match(src, /vsamp:\{n:'VSMP'[^\n]*id:103, ps:\[/, 'the table entry');
  assert.match(src, /vsamp:'Drive and lo-fi'/, 'its group');
  assert.match(src, /'cab', 'tube', 'vsamp',/, 'in the picker');
  assert.match(src, /if \(c\.t === 103\)\{[^\n]*\n[^\n]*new VSampCore\(sampleRate\)/, 'the worklet makes it');
  assert.match(src, /VSampCore\.toString\(\)/, 'and has its code');
  assert.match(H.read('core/seq-core.js'), /"vsamp":f =>/, 'the audio-thread core knows its map');
});

test('row colours (3 Nov): right-click a row number, kept in the song, moved with rows', () => {
  const src = H.read('index.html');
  assert.match(src, /S\.rcol = \{\}; if \(j\.rcol/, 'loaded');
  assert.match(src, /async function rowColour\(y\)\{/, 'the sheet');
  assert.match(src, /if \(S\.rcol\)\{ const o = \{\}; for \(const k in S\.rcol\)\{ const y = \+k; if \(y < at\) o\[y\] = S\.rcol\[k\]; else if \(y \+ h < 256\)/, 'insert moves them');
});

test('acid key from the song (3 Nov): KEY FOLLOW instruments only, the song SCALE first', () => {
  const src = H.read('index.html');
  assert.match(src, /function songKey\(skipPh\)\{\n  if \(\(S\.scale \| 0\) > 0\)/, 'the song SCALE first');
  assert.match(src, /if \(!I \|\| !I\.kf\) continue;/, 'melodic instruments only');
  assert.match(src, /const found = songKey\(cPhrase\);/, 'the panel uses it');
});

test('acid (3 Nov): 16th grid from the LPB, composed lines', () => {
  const src = H.read('index.html');
  assert.match(src, /spr = Math\.max\(1, Math\.round\(lpb \/ 4\)\), Ls = Math\.max\(1, Math\.floor\(L \/ spr\)\)/, 'steps from the LPB');
  assert.match(src, /function acidWrite\(P, y0, L, base, st, ins, spr = 1\)\{/, 'write on the grid');
  assert.match(src, /if \(!only && \(R\.style \| 0\) > 0\) return acidCompose\(st, L, lanes, R, rnd\);/, 'composed styles');
});

test('build 394: slow double-click, Shift+Up wrap, Alt+B / Alt+S, stop cursor stays', () => {
  const src = require('fs').readFileSync(require('path').join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(src, /arrClicks = e\.detail/);
  assert.match(src, /arrGo\(p\.y, p\.t, e\.pointerType === 'mouse' && arrClicks >= 2\)/);
  assert.match(src, /function shiftSel\(dir, noWrap\)/);
  assert.match(src, /e\.code === 'KeyB'\)\{ if \(!e\.repeat\) acidLine\(\)/);
  assert.match(src, /e\.code === 'KeyS'\)\{ if \(!e\.repeat && !pbOpen\) setSheet\('song'\)/);
  assert.match(src, /thunderplus-stopcur-v2/);
});

test('build 395: Enter on a song block picks it up', () => {
  const src = require('fs').readFileSync(require('path').join(__dirname, '..', 'index.html'), 'utf8');
  assert.match(src, /if \(tv === 'song' && enter && v >= 0\)\{[^\n]*\n[^\n]*\n\s*else \{ last\.sect = v;/);
});

test('build 398: …waves live engine', () => {
  const fs = require('fs'), path = require('path'), src = fs.readFileSync(path.join(__dirname, '..', 'index.html'), 'utf8'), wk = fs.readFileSync(path.join(__dirname, '..', 'waves', 'waves-worklet.js'), 'utf8');
  assert.match(wk, /registerProcessor\('thunder-wlive', WavesLiveProc\)/);
  assert.match(src, /wavesl:\{live:true, d:\(\) => \(\{bus:1, vel:100\}\), r:/);
  assert.match(src, /if \(isWlV\(V\)\) return wlNode\(t, pad, V, kind, key\)/);
  assert.match(src, /ps = isPsV\(v\) \|\| isWlV\(v\)/);
  assert.ok(!/waves-worklet\.js\?v=31/.test(src));
});

/* ───────── FOLLOW PATTERN jumps on a change only (owner, 7 Nov: one phrase on a track trapped the cursor) ───────── */
test('FOLLOW PATTERN: a track playing one phrase lets the cursor move off it; a new phrase or row still pulls it over', () => {
  const blk = cut('index.html', /var fpHome = null;[^\n]*\nvar fpMem = null;[\s\S]*?\nfunction followPlay\(\)\{[\s\S]*?\n\}\n(?=\/\/ ARRANGE PAGES)/, 'fpMem and followPlay');
  const c = run(`var tp = {on:true, mode:'song', pos:null}, sdrag = 0, drag = 0, tv = 'phrase', cTrack = 0, followMode = 1, selA = null,
    cSect = 0, cPhrase = 0, arrHandT = -1e9, workT = -1e9, FOLLOW_HOLD = 4000, apReady = false, apF = '', apLastKey = '', cs = {phrase:{y:0}, song:{x:0, y:0}};
    const plen = () => 16, pvActive = () => false, pvFollow = () => {}, arrFollowPage = () => {}, asongOn = () => false;
    ${blk}
    this.api = { step(P){ tp.pos = P; followPlay(); }, st: () => ({cPhrase, y: cs.song.y}), set(o){ if ('cPhrase' in o) cPhrase = o.cPhrase; if ('y' in o) cs.song.y = o.y; if ('tv' in o) tv = o.tv; },
      stop(){ tp.on = false; fpMem = null; tp.on = true; } };`, { performance: { now: () => 1e6 } });
  const P = (phrase, row) => [{ phrase, row, step: 0, sect: 0 }];
  c.api.step(P(3, 0)); assert.equal(c.api.st().cPhrase, 3, 'the first step shows what plays');
  c.api.set({ cPhrase: 7 });
  for (let i = 0; i < 8; i++) c.api.step(P(3, 0));
  assert.equal(c.api.st().cPhrase, 7, 'the same phrase playing on: the cursor stays where it was moved');
  c.api.step(P(4, 1)); assert.equal(c.api.st().cPhrase, 4, 'a new phrase on the track still follows');
  c.api.set({ tv: 'song' }); c.api.step(P(4, 1)); c.api.set({ y: 9 });
  for (let i = 0; i < 8; i++) c.api.step(P(4, 1));
  assert.equal(c.api.st().y, 9, 'Song screen: one block playing, the cursor stays on the row it was moved to');
  c.api.step(P(5, 2)); assert.equal(c.api.st().y, 2, 'the next row playing still moves it');
});

test('dialog sliders take touch: the shared drag serves .ask on touch, and the row doesn\'t let the browser pan it away', () => {
  const src = H.read('index.html');
  assert.match(src, /const sliderOk = \(r, touch\) =>[^\n]*touch \? '#gset, \.rlock' : '#gset, \.rlock, \.ask'/, 'touch reaches dialogs');
  assert.match(src, /!sliderOk\(r, e\.pointerType === 'touch'\)/, 'the drag says it is a touch');
  assert.match(src, /\.acidr, \.acidf:has\(input\[type=range\]\)\{touch-action:pan-y\}/, 'no horizontal pan cancels the drag');
});

test('instrument big step: past either end it stays put rather than clamping to 00', () => {
  assert.match(H.read('index.html'), /case 'inst': \{ if \(big && \(v \+ d \* 8 < 0 \|\| v \+ d \* 8 > NI - 1\)\) return;/);
});

test('mobile stalls: playhead-only Arrange redraws are thinned on touch, and the page sequencer moves on sooner there', () => {
  const src = H.read('index.html');
  assert.match(src, /if \(playDraw && COARSE\)\{ const t = performance\.now\(\); if \(t - arrPlayT < 240\) return;/);
  assert.match(src, /s\.time < ctx\.currentTime - \(COARSE && !document\.hidden \? Math\.max\(0\.15, outLat \+ 0\.05\) : 0\.5\)/, 'desktop keeps 0.5 s');
});

test('Mobile: SHIFT+▲ goes to Arrange from every screen', () => {
  assert.match(H.read('index.html'), /if \(dir === 'up' && isMobile\(\)\)\{ if \(apF === 'arr'\) return null; if \(apArrOk\(\)\)\{ apFocus\('arr', true\);/);
});

test('the pad has latching MUTE and SOLO', () => {
  const src = H.read('index.html');
  assert.match(src, /data-b="mute" id="kMute"/); assert.match(src, /data-b="solo" id="kSolo"/);
  assert.match(src, /if \(b === 'mute' \|\| b === 'solo'\)\{ clearTimeout\(rep\); [\s\S]{0,300}b === 'mute' \? muteKey\(true\) : soloKey\(\); return; \}/);
});

test('Phrase ◀ ▶ step one column at a time (no skipped INST / FX value / chord columns)', () => {
  const blk = cut('index.html', /function move\(dx, dy\)\{[\s\S]*?\n\}\n/, 'move');
  const run1 = (cols, from, dx) => {
    const cs = { phrase: { x: from, y: 0 } };
    const c = run(blk + '\nthis.go = () => move(' + dx + ', 0);', { tv: 'phrase', cs, dims: () => ({ w: 12, h: 16 }), pvActive: () => false, pCols: () => cols,
      mstView: () => false, busFxView: () => false, draw() {}, FX0: 6 });
    c.go(); return cs.phrase.x;
  };
  const mono = [0, 4, 5, 6, 7, 8, 9, 10, 11], poly = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];
  for (const cols of [mono, poly]) for (let i = 0; i < cols.length; i++) {
    assert.equal(run1(cols, cols[i], 1), cols[Math.min(i + 1, cols.length - 1)], 'right from ' + cols[i]);
    assert.equal(run1(cols, cols[i], -1), cols[Math.max(i - 1, 0)], 'left from ' + cols[i]);
  }
});

/* ───────── usability audit leftovers (10 Nov): U4 sheet buttons, U6, U12, U14, U15, U16 ───────── */
test('U4: sheet Close / Done buttons are at least 40 px tall on a touch screen', () => {
  const html = H.read('index.html');
  const blk = html.match(/@media \(pointer:coarse\)\{\n  \.top \.tsong[\s\S]*?\n\}\n/);
  assert.ok(blk, 'the TOUCH TARGETS block moved');
  for (const sel of ['.pbs .phd button', '.pbs .pft button', '.pbs .btnrow button', '.pbs .smbfoot button', '.phlhd button']) assert.ok(blk[0].includes(sel), sel + ' is in the coarse block');
  assert.match(blk[0], /\.phlhd button\{min-height:40px\}\n\}\n$/, 'the sheet buttons get min-height 40px');
  assert.match(html, /\.gsethead button\{min-height:40px/, 'the Settings Done stays 40 px');
});

test('U6: picking an engine on touch scrolls its editor into view; a mouse is left alone', () => {
  const html = H.read('index.html');
  assert.match(html, /if \(k !== curK\)\{ setEngine\(k\); engShowEditor\(\); \}/, 'the engine sheet calls engShowEditor after setEngine');
  const fn = cut('index.html', /function touchUi\(\)[^\n]*\nfunction engShowEditor\(\)\{[\s\S]*?\n\}\n/, 'touchUi and engShowEditor');
  let scrolled = 0, coarse = true;
  const card = { hidden: false, offsetParent: {}, scrollIntoView(o) { scrolled++; assert.equal(o.block, 'start'); } };
  const mk = () => run(fn + '\nthis.go = engShowEditor;', { isMobile: () => false, matchMedia: () => ({ matches: coarse }),
    requestAnimationFrame: f => f(), document: { getElementById: id => id === 'blipCard' ? card : null } });
  mk().go(); assert.equal(scrolled, 1, 'touch: scrolled to the visible engine card');
  coarse = false; mk().go(); assert.equal(scrolled, 1, 'mouse: no scroll');
});

test('U12: the right-click wording in two settings is for the mouse only', () => {
  const html = H.read('index.html');
  for (const l of ['SONG MOUSE', 'ROW COLOR IN PATTERN']) {
    const m = html.match(new RegExp("\\{l:'" + l + "', get ck\\(\\)\\{ return touchUi\\(\\) \\? '([^']*)' : '([^']*)'; \\}"));
    assert.ok(m, l + ' has a touch and a mouse label');
    assert.ok(!/right-click/i.test(m[1]), l + ': the touch label has no right-click'); assert.ok(/right-click/i.test(m[2]), l + ': the mouse label keeps it');
  }
  assert.match(html, /info:\(\) => touchUi\(\) \? 'In Pattern view, press and hold/, 'ROW COLOR IN PATTERN info');
});

test('U14: a picker row with no description has no second line', () => {
  assert.match(H.read('index.html'), /if \(gn\) b\.append\(el\('small', null, gn\)\)/, 'perfPick adds the small line only when there is a group name');
});

test('U15: hints show the platform key (MODK), never the literal "Ctrl/⌘", in script strings', () => {
  const html = H.read('index.html'), i0 = html.indexOf('/* ===ENGINE START=== */'), i1 = html.indexOf('<script src="pump/ui/kit.js');
  assert.ok(i0 > 0 && i1 > i0);
  const decl = cut('index.html', /const MODK = [^\n]*;/, 'MODK');   // the page's own line, run as shipped
  // a literal "Ctrl/⌘" may stay in comments, the What's new entries and the static Help page (before the script), nowhere else
  const live = html.slice(i0, i1).split('\n').filter(l => l.includes('Ctrl/⌘') && !/^\s*(\/\/|\/\*|\*|-|\[\d+, '|'pump windows)/.test(l))
    .filter(l => !/^\s{2,}[^'"`]*$/.test(l) && /['"`][^'"`]*Ctrl\/⌘[^'"`]*['"`]/.test(l.replace(/\/\/.*$/, '')));
  assert.deepEqual(live.map(l => l.trim().slice(0, 80)), [], 'these still show Ctrl/⌘ to the user');
  const m = run(decl + ' this.v = MODK;', { navigator: { platform: 'MacIntel' } });
  assert.equal(m.v, '⌘'); assert.equal(run(decl + ' this.v = MODK;', { navigator: { platform: 'Win32' } }).v, 'Ctrl'); assert.equal(run(decl + ' this.v = MODK;', {}).v, 'Ctrl', 'no navigator (the engine golden harness): Ctrl');
});

test('U16: under-contrast text in IDM, IDM pink & green, IDM pink paper and 909 gets a darker colour; backgrounds are untouched', () => {
  const html = H.read('index.html'), at = html.indexOf('/* CONTRAST (usability audit U16)'); assert.ok(at > 0, 'the U16 block');
  const css = html.slice(at, html.indexOf('</style>', at));
  assert.match(css, /\.tclk\.live b\{color:var\(--mx\)\}/, 'BPM / LPB while playing');
  assert.match(css, /idmpink"\]\.tpon[^{]*\{color:#0a1a12!important\}/, 'Stop on pink paper');
  assert.match(css, /\.stgph\{color:var\(--ts-rn\)\}/, 'the stage label');
  assert.match(css, /tr909"\] :is\(\.tagp b\.gp, \.tgrid span\.pvh\.gp\)\{color:var\(--mxon\)\}/, 'POLY labels on the 909');
  assert.ok(!/background/.test(css.replace(/\/\*[\s\S]*?\*\//g, '')), 'colour only: no background is changed');
});

// (11 Nov, build 564) The title bar: the name is hidden when it does not fit; folded, the bar starts at the screen edge.
test('title bar: brandFit hides a clipped name on Mobile only, and the bar loses its left padding when the name is gone', () => {
  const src = require('fs').readFileSync(require('path').join(__dirname, '..', 'index.html'), 'utf8');
  const m = src.match(/function brandFit\(\)\{[\s\S]*?\n\}/); assert.ok(m, 'brandFit exists');
  const mk = (mob, cw, sw) => { const cls = new Set(); const b = { clientWidth: cw, scrollWidth: sw, classList: { add: c => cls.add(c), remove: c => cls.delete(c), contains: c => cls.has(c) } };
    const document = { querySelector: () => b, body: { classList: { contains: c => c === 'mob' && mob } } };
    new Function('document', m[0] + '; brandFit();')(document); return cls.has('bhide'); };
  assert.strictEqual(mk(true, 69, 78), true, 'clipped on Mobile: hidden');
  assert.strictEqual(mk(true, 78, 78), false, 'fits: shown');
  assert.strictEqual(mk(false, 69, 78), false, 'desktop layout: left alone');
  assert.strictEqual(mk(true, 0, 0), false, 'already display:none (folded): no class');
  assert.match(src, /body\.mob \.top \.brand\.bhide\{display:none\}/);
  assert.match(src, /body\.mob \.top:has\(\.brand\.bhide\)\{padding-left:0\}/);
  assert.match(src, /@media \(max-width:480px\)\{ body\.mob \.top\{padding-left:0\}/);
});
