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
  let now = 1000; const c = run(blk + '\nthis.wheelSteps = wheelSteps;', { WeakMap, performance: { now: () => now } });
  const k1 = {}, k2 = {}, sum = (k, ev, n, dt = 8) => { let s = 0; for (let i = 0; i < n; i++){ now += dt; s += c.wheelSteps(k, ev); } return s; };
  assert.equal(c.wheelSteps(k1, { deltaY: 100, deltaMode: 0 }), 1, 'a mouse notch: one step');
  now += 500; assert.equal(sum(k1, { deltaY: 6, deltaMode: 0 }, 60), 3, 'a trackpad flick of 60 events x 6 px: 3 steps (was 60)');
  now += 500; assert.equal(c.wheelSteps(k1, { deltaY: 3, deltaMode: 1 }), 1, 'Firefox lines: 3 lines is a notch');
  now += 500; assert.equal(sum(k1, { deltaY: 60, deltaMode: 0 }, 1) + sum(k1, { deltaY: -60, deltaMode: 0 }, 1), 0, 'turning back starts again, no stray step');
  now += 500; c.wheelSteps(k1, { deltaY: 60, deltaMode: 0 }); now += 400; assert.equal(c.wheelSteps(k1, { deltaY: 60, deltaMode: 0 }), 0, 'a pause drops the rest');
  now += 500; c.wheelSteps(k1, { deltaY: 60, deltaMode: 0 }); assert.equal(c.wheelSteps(k2, { deltaY: 60, deltaMode: 0 }), 0, 'each slider adds up its own');
  now += 500; assert.equal(c.wheelSteps(k1, { deltaY: -5000, deltaMode: 0 }), -8, 'one huge event: at most 8 steps');
  assert.equal(c.wheelSteps(k1, { deltaY: 0, deltaX: 0 }), 0);
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
    moRelease: () => log.mo++, NPORT: 4, midiOut: p => p === 0 || p === 2, msend: (m, t, x, p) => log.sent.push([p, ...m]), toast: (m, ms) => log.toast.push([m, ms]), tpLog() {} });
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
  assert.match(src, /go\(VIEWS\[\+e\.code\.slice\(5\) - 1\]\); return; \}\n  if \(view !== 'synth'\) return;/, 'K8');
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
  assert.match(src, /closest\('\.mfbar'\); if \(!b \|\| tv !== 'mixer'\) return;\n  if \(e\.button !== 0\) return;/, 'M17');
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
  assert.match(src, /if \(e\.shiftKey && view === 'synth'\)\{ play\(cur\); return; \}\n\s*if \(tp\.on\) tStop\(\); else tStart\('song', \{row:songRow\(\)\}\); return; \}/, 'Synth page: Space the song, Shift+Space the pad');
  assert.doesNotMatch(src, /if \(e\.key === ' ' && !e\.target\.matches\('button'\)\)\{ play\(cur\);/, 'the old Synth-page Space = pad is gone');
  assert.match(src, /drag\.pos = \(drag\.pos \|\| 0\) \+ \(drag\.lastY - e\.clientY\) \/ \(e\.shiftKey \? 48 : 16\);\n\s*drag\.lastY = e\.clientY; drag\.big = !!\(e\.ctrlKey \|\| e\.metaKey\);/, 'Shift slows a value drag, Ctrl/⌘ takes big steps');
  assert.match(src, /bump\(n < 0 \? 1 : -1, false\);/, 'the wheel no longer takes big steps with Shift');
  assert.match(src, /if \(e\.altKey && y >= 0 && !inSel && altDrag\(e, x, y\)\) return;/, 'Alt-drag edits (gridDown)');
  assert.match(src, /if \(x != null && e\.altKey && altDrag\(e, x, m\.y\)\) return;/, 'Alt-drag edits (Pattern view)');
  assert.match(src, /const filled = was && getCell\(tv, x, y\) >= 0;/, 'the cursor\'s own cell selects too');
  assert.match(src, /if \(d\.altMute && !d\.moved && e\.type === 'pointerup'\)\{ toggleBlockMute/, 'an Alt-click on the Song still mutes');
  assert.match(src, /const holdOk = e\.pointerType !== 'mouse';/, 'Arrange hold-to-mute: touch and pen only');
});
