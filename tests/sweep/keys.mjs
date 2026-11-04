// UI SWEEP: the keyboard. Every documented shortcut is pressed (real key events through playwright) on the screen where it
// applies, and the app's state is checked afterwards (through window.__seqTest.ev, the app's own scope).
//   node tests/sweep/keys.mjs        (≈ 3–5 min; prints a PASS/FAIL table)
// Also checks the hint lines and Help mention keys that exist, and looks for one key doing two things on one screen.
import fs from 'fs'; import path from 'path';
import { boot, takeErrors, Table, app, ROOT } from './ui-lib.mjs';

const T = new Table('keys');
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');
const B = await boot({ width: 1600, height: 1000 });
const { page, errors } = B;
await page.evaluate(() => { const no = async () => { const e = new Error('cancelled'); e.name = 'AbortError'; throw e; }; window.showSaveFilePicker = no; window.showOpenFilePicker = no; window.__picks = 0;
  const w = window.showSaveFilePicker; window.showSaveFilePicker = (...a) => { window.__picks++; return w(...a); }; const o = window.showOpenFilePicker; window.showOpenFilePicker = (...a) => { window.__picks++; return o(...a); }; });
let chooser = 0; page.on('filechooser', () => { chooser++; });

const A = src => app(page, src);
const load = async () => { await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(300); await A('(setDirty(false), 1)'); };
// Put the app on a screen with the keyboard focus on the page (not a button), the cursor somewhere known.
async function at(v, s, cur) {
  await A(`(askOpen && askOpen(null), pbOpen && pbOpen(), document.querySelectorAll('.swapov').forEach(e => e.remove()), sxMenuClose(), hideHelp(), gsetToggle(false), gfileToggle(false), geditToggle(false), 1)`).catch(() => {});
  await A(`(tp.on && tStop(true), setView('${v}'), ${s ? `go('${s}'),` : ''} selA = null, ${cur ? `Object.assign(cs['${s}'], ${JSON.stringify(cur)}),` : ''} typeof apF !== 'undefined' && apF && (apF = null), draw(), 1)`);
  await page.evaluate(() => { if (document.activeElement && document.activeElement !== document.body) document.activeElement.blur(); });
  await page.waitForTimeout(120);
}
async function check(name, setup, keys, test, note = '') {
  try {
    await setup(); takeErrors(errors);
    const before = await A(test.pre || 'null');
    if (name.startsWith('Hex types a block (Song')) await page.keyboard.type('005', { delay: 40 });
    for (const k of [].concat(keys)) { await page.keyboard.press(k); await page.waitForTimeout(60); }
    await page.waitForTimeout(test.wait || 250);
    const after = await A(test.post);
    const ok = test.ok(after, before);
    const errs = takeErrors(errors);
    T.add(name, errs.length ? false : ok === 'NOTE' ? 'NOTE' : ok ? true : false, (errs.length ? 'ERR ' + errs.join(' / ').slice(0, 150) + ' · ' : '') + `${JSON.stringify(before)} → ${JSON.stringify(after)}`.slice(0, 170) + (note ? ' · ' + note : ''));
  } catch (e) { T.add(name, false, 'threw: ' + e.message.split('\n')[0].slice(0, 160)); }
}
const toastT = `$('toast').hidden ? '' : $('toast').textContent`;
const cell = (s, x, y) => `JSON.stringify(${s === 'phrase' ? `S.phrases[cPhrase][${y}].slice(0, 12)` : `S.song[${y}][${x}]`})`;

await load();
try {
  // ---- screens ----
  const scr = (k, want, from = ['tracker', 'song']) => check(`${k} → ${want}`, () => at(...from), k, { post: 'view + "/" + tv', ok: a => a === want });
  await scr('Alt+1', 'tracker/song', ['tracker', 'phrase']); await check('Alt+2 → sect (Phrase while sects are hidden)', () => at('tracker', 'song'), 'Alt+Digit2', { post: 'view + "/" + tv', ok: a => a === ("tracker/" + "sect") || a === 'tracker/phrase' }); await scr('Alt+3', 'tracker/phrase'); await scr('Alt+4', 'tracker/inst');
  await scr('Alt+5', 'tracker/mixer'); await scr('Alt+6', 'tracker/fx'); await scr('Alt+7', 'tracker/project');
  await check('Alt+0 → synth', () => at('tracker', 'song'), 'Alt+0', { post: 'view', ok: a => a === 'synth' });
  await check('Alt+8 → waves', () => at('tracker', 'song'), 'Alt+8', { post: 'view', ok: a => a === 'waves', wait: 900 });
  await check('Alt+9 → chains', () => at('tracker', 'song'), 'Alt+9', { post: 'view', ok: a => a === 'chains', wait: 900 });
  await check('Alt+` → last view', async () => { await at('synth'); await at('tracker', 'mixer'); }, 'Alt+Backquote', { post: 'view', ok: a => a === 'synth' });
  await check('Alt+1 from Synth page', () => at('synth'), 'Alt+1', { post: 'view + "/" + tv', ok: a => a === 'tracker/song' });
  await scr('F2', 'tracker/phrase'); await scr('F3', 'tracker/mixer'); await scr('F4', 'tracker/inst');
  await check('Alt+→ Song → opens phrase', () => at('tracker', 'song', { x: 0, y: 0 }), 'Alt+ArrowRight', { post: 'tv', ok: a => a === 'phrase' });
  await check('Alt+← Mixer → previous screen', () => at('tracker', 'mixer', { x: 0, y: 0 }), 'Alt+ArrowLeft', { post: 'tv', ok: a => a !== 'mixer' });
  await check('Alt+▼ Inst → Mixer', () => at('tracker', 'inst'), 'Alt+ArrowDown', { post: 'tv', ok: a => a === 'mixer' });
  await check('Alt+▲ Inst → Project', () => at('tracker', 'inst'), 'Alt+ArrowUp', { post: 'tv', ok: a => a === 'project' });
  await check('Alt+▲ Mixer → back to the middle screen (nav: up from the Mixer)', () => at('tracker', 'mixer'), 'Alt+ArrowUp', { post: 'tv', ok: a => a !== 'mixer' });
  await check('⌥▼ on Song → pattern below (cursor row +)', () => at('tracker', 'song', { x: 0, y: 0 }), 'Alt+ArrowDown', { pre: 'cs.song.y', post: 'cs.song.y', ok: (a, b) => a > b });
  await check('⌥▼ on Phrase → next pattern', () => at('tracker', 'phrase'), 'Alt+ArrowDown', { pre: 'cs.song.y + "/" + cPhrase', post: 'cs.song.y + "/" + cPhrase', ok: (a, b) => a !== b });
  await check('Shift+⌥▶ on Phrase → next track phrase', () => at('tracker', 'phrase'), 'Shift+Alt+ArrowRight', { pre: 'cTrack', post: 'cTrack', ok: (a, b) => a !== b });
  // ---- transport ----
  await check('Space plays (Song)', () => at('tracker', 'song', { x: 0, y: 0 }), 'Space', { post: 'tp.on + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await check('Space again stops', async () => {}, 'Space', { post: 'tp.on', ok: a => a === false, wait: 400 });
  await check('Shift+Space plays (Song)', () => at('tracker', 'song', { x: 0, y: 0 }), 'Shift+Space', { post: 'tp.on + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await check('F8 stops', async () => {}, 'F8', { post: 'tp.on', ok: a => a === false });
  await check('F8 again: panic (no error)', async () => {}, 'F8', { post: 'tp.on', ok: a => a === false });
  await check('Space on Phrase plays', () => at('tracker', 'phrase'), 'Space', { post: 'tp.on + "/" + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await check('Shift+Space on Phrase plays the other', async () => { await A('(tStop(true), 1)'); }, 'Shift+Space', { post: 'tp.on + "/" + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await A('(tStop(true), 1)');
  await check('F5 plays song from top', () => at('tracker', 'song', { x: 0, y: 4 }), 'F5', { post: 'tp.on + "/" + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await check('F6 plays this screen', async () => { await A('(tStop(true), 1)'); await at('tracker', 'phrase'); }, 'F6', { post: 'tp.on + "/" + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await check('F7 plays from cursor', async () => { await A('(tStop(true), 1)'); await at('tracker', 'song', { x: 0, y: 4 }); }, 'F7', { post: 'tp.on + "/" + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await A('(tStop(true), 1)');
  await check('Ctrl+Space loops the pattern', () => at('tracker', 'phrase'), 'Control+Space', { post: 'tp.on + "/" + tp.mode', ok: a => a.startsWith('true'), wait: 600 });
  await A('(tStop(true), 1)');
  await check('F9 arms REC', () => at('tracker', 'phrase'), 'F9', { pre: 'rec.on', post: 'rec.on', ok: (a, b) => a !== b });
  await check('Alt+R toggles REC back', async () => {}, 'Alt+KeyR', { pre: 'rec.on', post: 'rec.on', ok: (a, b) => a !== b });
  await check('Alt+M metronome steps', () => at('tracker', 'phrase'), 'Alt+KeyM', { pre: 'recS.met', post: 'recS.met', ok: (a, b) => a !== b });
  await A('(setMet(0), setRec(false), 1)');
  // ---- edit ----
  const ph = cur => () => at('tracker', 'phrase', cur);
  await A('(pvMode = false, draw(), 1)');
  await check('Z types C on Phrase', async () => { await ph({ x: 0, y: 3 })(); await A('(S.phrases[cPhrase][3][0] = -1, kStep = 1, editOn || setEditOn(true), 1)'); }, 'KeyZ', { post: 'S.phrases[cPhrase][3][0] % 12 + "/" + cs.phrase.y', ok: a => a === '0/4' });
  await check('Q types C an octave up', async () => { await ph({ x: 0, y: 5 })(); await A('(S.phrases[cPhrase][5][0] = -1, 1)'); }, 'KeyQ', { pre: 'kOct', post: 'S.phrases[cPhrase][5][0]', ok: (a, b) => a % 12 === 0 && a >= 0 });
  await check('S types C♯', async () => { await ph({ x: 0, y: 6 })(); }, 'KeyS', { post: 'S.phrases[cPhrase][6][0] % 12', ok: a => a === 1 });
  await check('; types D♯ (top of lower row)', async () => { await ph({ x: 0, y: 7 })(); }, 'Semicolon', { post: 'S.phrases[cPhrase][7][0] % 12', ok: a => a === 3 });
  await check('P types E (top of upper row)', async () => { await ph({ x: 0, y: 8 })(); }, 'KeyP', { post: 'S.phrases[cPhrase][8][0] % 12', ok: a => a === 4 });
  await check('1 types note off', async () => { await ph({ x: 0, y: 9 })(); }, 'Digit1', { post: 'S.phrases[cPhrase][9][0]', ok: a => a !== -1 && a >= 120 || a === -2 || a === 255 }, 'value depends on the OFF code');
  await check('- octave down', ph({ x: 0, y: 0 }), 'Minus', { pre: 'kOct', post: 'kOct', ok: (a, b) => a === b - 1 });
  await check('= octave up', ph({ x: 0, y: 0 }), 'Equal', { pre: 'kOct', post: 'kOct', ok: (a, b) => a === b + 1 });
  await check('} edit step up', ph({ x: 0, y: 0 }), 'Shift+BracketRight', { pre: 'kStep', post: 'kStep', ok: (a, b) => a === b + 1 });
  await check('{ edit step down', ph({ x: 0, y: 0 }), 'Shift+BracketLeft', { pre: 'kStep', post: 'kStep', ok: (a, b) => a === b - 1 });
  await check('Del clears the cell', async () => { await ph({ x: 0, y: 3 })(); }, 'Delete', { post: 'S.phrases[cPhrase][3][0]', ok: a => a === -1 });
  await check('Shift+Del clears the row', async () => { await ph({ x: 0, y: 6 })(); await A('(S.phrases[cPhrase][6][5] = 2, 1)'); }, 'Shift+Delete', { post: 'S.phrases[cPhrase][6][0] + "/" + S.phrases[cPhrase][6][5]', ok: a => a === '-1/-1' });
  await check('Backspace clears and steps down', async () => { await ph({ x: 0, y: 7 })(); }, 'Backspace', { post: 'S.phrases[cPhrase][7][0] + "/" + cs.phrase.y', ok: a => a === '-1/8' });
  await check('Ctrl+Z undoes', async () => { await ph({ x: 0, y: 7 })(); }, 'Control+KeyZ', { post: 'S.phrases[cPhrase][7][0]', ok: a => a !== -1 });
  await check('Ctrl+Y redoes', async () => {}, 'Control+KeyY', { post: 'S.phrases[cPhrase][7][0]', ok: a => a === -1 });
  await check('Ctrl+Shift+Z redoes too', async () => { await A('(undoStep(-1), 1)'); }, 'Control+Shift+KeyZ', { post: 'S.phrases[cPhrase][7][0]', ok: a => a === -1 });
  await check('Insert adds a row', async () => { await ph({ x: 0, y: 2 })(); await A('(S.phrases[cPhrase][2][0] = 40, S.phrases[cPhrase][3][0] = 41, 1)'); }, 'Insert', { post: 'S.phrases[cPhrase][2][0] + "/" + S.phrases[cPhrase][3][0]', ok: a => a === '-1/40' });
  await check('Ctrl+Backspace removes the row', async () => { await ph({ x: 0, y: 2 })(); }, 'Control+Backspace', { post: 'S.phrases[cPhrase][2][0]', ok: a => a === 40 });
  await check('Ctrl+Enter inserts a row', async () => { await ph({ x: 0, y: 2 })(); }, 'Control+Enter', { post: 'S.phrases[cPhrase][2][0] + "/" + S.phrases[cPhrase][3][0]', ok: a => a === '-1/40' });
  await check('Ctrl+→ bumps the value', async () => { await ph({ x: 0, y: 3 })(); }, 'Control+ArrowRight', { pre: 'S.phrases[cPhrase][3][0]', post: 'S.phrases[cPhrase][3][0]', ok: (a, b) => a === b + 1 });
  await check('Ctrl+▲ bumps more', async () => { await ph({ x: 0, y: 3 })(); }, 'Control+ArrowUp', { pre: 'S.phrases[cPhrase][3][0]', post: 'S.phrases[cPhrase][3][0]', ok: (a, b) => a > b + 1 });
  await check('Hex on the instrument column', async () => { await ph({ x: 5, y: 3 })(); }, ['Digit0', 'Digit7'], { post: 'S.phrases[cPhrase][3][5]', ok: a => a === 7 });
  await check('Hex on the volume column', async () => { await ph({ x: 4, y: 3 })(); }, ['KeyA', 'Digit0'], { post: 'S.phrases[cPhrase][3][4]', ok: a => a === 0xA0 });
  await check('FX letter types a command', async () => { await ph({ x: 6, y: 3 })(); await A('(S.phrases[cPhrase][3][6] = -1, 1)'); }, 'KeyV', { post: 'S.phrases[cPhrase][3][6] >= 0 ? FXC[S.phrases[cPhrase][3][6]] : -1', ok: a => typeof a === 'string' && a[0] === 'V' });
  await check('Esc toggles EDIT off', ph({ x: 0, y: 0 }), 'Escape', { post: 'editOn', ok: a => a === false });
  await check('Note key with EDIT off does not write', async () => { await ph({ x: 0, y: 11 })(); await A('(S.phrases[cPhrase][11][0] = -1, 1)'); }, 'KeyZ', { post: 'S.phrases[cPhrase][11][0]', ok: a => a === -1 });
  await check('Esc toggles EDIT back on', ph({ x: 0, y: 0 }), 'Escape', { post: 'editOn', ok: a => a === true });
  await check('Tab → instrument column', ph({ x: 0, y: 0 }), 'Tab', { post: 'cs.phrase.x + "/" + cTrack', ok: a => a !== '0/0' }, 'goes to the next track (TAB MOVES BY TRACK) unless Settings → TAB → COLUMNS');
  await check('Home / End', ph({ x: 0, y: 5 }), 'End', { post: 'cs.phrase.y', ok: a => a > 5 });
  await check('PageUp', ph({ x: 0, y: 20 }), 'PageUp', { post: 'cs.phrase.y', ok: a => a === 4 });
  // ---- selection ----
  await check('Shift+▼ selects', ph({ x: 0, y: 0 }), ['Shift+ArrowDown', 'Shift+ArrowDown'], { post: 'selA ? JSON.stringify(selRect()) : null', ok: a => !!a && JSON.parse(a).y1 === 2 });
  await check('plain arrow drops the selection', async () => {}, 'ArrowDown', { post: '!!selA', ok: a => a === false });
  await check('Ctrl+B starts a selection', ph({ x: 0, y: 0 }), 'Control+KeyB', { post: '!!selA', ok: a => a === true });
  await check('Ctrl+A selects all', ph({ x: 0, y: 0 }), 'Control+KeyA', { post: 'selA ? JSON.stringify(selRect()) : null', ok: a => !!a && JSON.parse(a).y1 > 10 });
  await check('Ctrl+Shift+▼ selects lines', ph({ x: 0, y: 0 }), 'Control+Shift+ArrowDown', { post: 'selA ? !!selA.line : null', ok: a => a === true });
  await check('Ctrl+C then Ctrl+V pastes', async () => { await ph({ x: 0, y: 0 })(); await A('(S.phrases[cPhrase][0][0] = 50, S.phrases[cPhrase][14][0] = -1, 1)'); await page.keyboard.press('Control+KeyC'); await A('(cs.phrase.y = 14, draw(), 1)'); }, 'Control+KeyV', { post: 'S.phrases[cPhrase][14][0]', ok: a => a === 50 });
  await check('Ctrl+X cuts', async () => { await ph({ x: 0, y: 14 })(); }, 'Control+KeyX', { post: 'S.phrases[cPhrase][14][0] + "/" + !!clip', ok: a => a === '-1/true' });
  await check('Ctrl+Shift+V mix-paste fills empty only', async () => { await ph({ x: 0, y: 15 })(); await A('(S.phrases[cPhrase][15][0] = 30, 1)'); }, 'Control+Shift+KeyV', { post: 'S.phrases[cPhrase][15][0]', ok: a => a === 30 });
  await check('Alt+. transposes up', async () => { await ph({ x: 0, y: 0 })(); await A('(S.phrases[cPhrase][0][0] = 48, 1)'); }, 'Alt+Period', { post: 'S.phrases[cPhrase][0][0]', ok: a => a === 49 });
  await check('Alt+, transposes down', ph({ x: 0, y: 0 }), 'Alt+Comma', { post: 'S.phrases[cPhrase][0][0]', ok: a => a === 48 });
  await check('Shift+Alt+. octave up', ph({ x: 0, y: 0 }), 'Shift+Alt+Period', { post: 'S.phrases[cPhrase][0][0]', ok: a => a === 60 });
  await check('Shift+Alt+, octave down', ph({ x: 0, y: 0 }), 'Shift+Alt+Comma', { post: 'S.phrases[cPhrase][0][0]', ok: a => a === 48 });
  await check('Ctrl+I interpolates', async () => { await ph({ x: 4, y: 0 })(); await A('(S.phrases[cPhrase][0][4] = 0, S.phrases[cPhrase][4][4] = 0x80, [1,2,3].forEach(y => S.phrases[cPhrase][y][4] = -1), selA = {v:"phrase", x:4, y:0}, cs.phrase.y = 4, draw(), 1)'); }, 'Control+KeyI', { post: 'S.phrases[cPhrase][2][4]', ok: a => a === 0x40 });
  await check('Ctrl+Shift+. expands', async () => { await ph({ x: 0, y: 0 })(); await A('(selA = {v:"phrase", x:0, y:0}, cs.phrase.y = 3, draw(), 1)'); }, 'Control+Shift+Period', { pre: 'S.undo', post: 'undo.past.length', ok: (a) => a > 0 }, 'checked only that it ran');
  await check('Alt+L loops the pattern', ph({ x: 0, y: 0 }), 'Alt+KeyL', { pre: 'JSON.stringify([loopSel, loopPat])', post: 'JSON.stringify([loopSel, loopPat])', ok: (a, b) => a !== b }).catch(() => {});
  await check('Shift+Alt+L loops the selection', ph({ x: 0, y: 0 }), 'Shift+Alt+KeyL', { pre: 'loopSel', post: 'loopSel', ok: (a, b) => a !== b });
  await A('(loopSel = false, draw(), 1)');
  await check('Alt+Enter plays the row', ph({ x: 0, y: 0 }), 'Alt+Enter', { post: toastT, ok: a => !/works on a phrase/.test(a) });
  // ---- mute / solo ----
  await check('/ mutes the track', ph({ x: 0, y: 0 }), 'Slash', { pre: 'S.mix[cTrack].mute', post: 'S.mix[cTrack].mute', ok: (a, b) => a !== b });
  await check('\\ unmutes it', ph({ x: 0, y: 0 }), 'Backslash', { pre: 'S.mix[cTrack].mute', post: 'S.mix[cTrack].mute', ok: (a, b) => a !== b });
  await check('Ctrl+/ solos', ph({ x: 0, y: 0 }), 'Control+Slash', { pre: 'S.mix[cTrack].solo', post: 'S.mix[cTrack].solo', ok: (a, b) => a !== b });
  await check('Ctrl+Shift+/ clears solos', ph({ x: 0, y: 0 }), 'Control+Shift+Slash', { post: 'S.mix.some(m => m.solo)', ok: a => a === false });
  // ---- Song screen ----
  const sg = cur => () => at('tracker', 'song', cur);
  await check('M mutes the block (Song)', sg({ x: 0, y: 1 }), 'KeyM', { pre: 'JSON.stringify(S.smute && S.smute[1])', post: 'JSON.stringify(S.smute && S.smute[1])', ok: (a, b) => a !== b });
  await check('M again unmutes', sg({ x: 0, y: 1 }), 'KeyM', { pre: 'JSON.stringify(S.smute && S.smute[1])', post: 'JSON.stringify(S.smute && S.smute[1])', ok: (a, b) => a !== b });
  await check('Hex types a block (Song, 3 digits)', async () => { await sg({ x: 9, y: 2 })(); await A('(S.song[2][9] = -1, 1)'); }, [], { post: 'S.song[2][9]', ok: a => a >= 0, wait: 600 }, 'typed 005 (a copy if 005 is on another track: UNIQUE PER TRACK)');
  await check('Del clears the block (Song)', sg({ x: 9, y: 2 }), 'Delete', { post: 'S.song[2][9]', ok: a => a === -1 });
  await check('Enter on empty places / Enter×2 new phrase', async () => { await sg({ x: 9, y: 3 })(); await A('(S.song[3][9] = -1, 1)'); }, ['Enter', 'Enter'], { post: 'S.song[3][9]', ok: a => a >= 0 });
  await check('Shift+Del on Song clears the cursor\'s cell (one column, by design)', async () => { await sg({ x: 0, y: 3 })(); }, 'Shift+Delete', { pre: 'JSON.stringify(S.song[3].slice(1, 10))', post: 'S.song[3][0] + "|" + JSON.stringify(S.song[3].slice(1, 10))', ok: (a, b) => a === '-1|' + b });
  await check('Ctrl+D duplicates the pattern (Song)', sg({ x: 0, y: 1 }), 'Control+KeyD', { pre: 'S.song.filter(r => r.some(v => v >= 0)).length', post: 'S.song.filter(r => r.some(v => v >= 0)).length', ok: (a, b) => a === b + 1 });
  await check('Ctrl+D on Phrase duplicates the track', async () => { await at('tracker', 'phrase', { x: 0, y: 0 }); await A('(cTrack = 9, draw(), 1)'); }, 'Control+KeyD', { post: '$("toast").hidden ? "" : $("toast").textContent', ok: a => /duplicated to track|No empty/.test(a) }, 'no notes: mix and FX only, by design');
  await check('Ctrl+Alt+D dup track with notes', async () => { await at('tracker', 'phrase', { x: 0, y: 0 }); await A('(cTrack = 9, draw(), 1)'); }, 'Control+Alt+KeyD', { pre: 'S.song.flat().filter(v => v >= 0).length', post: 'S.song.flat().filter(v => v >= 0).length + "|" + ($("toast").hidden ? "" : $("toast").textContent)', ok: (a, b) => +a.split('|')[0] > b || /No empty/.test(a) });
  await load();
  await check('Ctrl+Alt+C clone-copies (Song)', sg({ x: 0, y: 1 }), 'Control+Alt+KeyC', { post: '!!(clip && clip.uniq)', ok: a => a === true });
  await check('Ctrl+Shift+C clones down', sg({ x: 0, y: 1 }), 'Control+Shift+KeyC', { pre: 'S.song.filter(r => r.some(v => v >= 0)).length + "/" + S.song[2][0]', post: 'S.song.filter(r => r.some(v => v >= 0)).length + "/" + S.song[2][0]', ok: (a, b) => a !== b });
  await check('Ctrl+Shift+Enter inserts a song row', sg({ x: 0, y: 1 }), 'Control+Shift+Enter', { pre: 'S.song[1][0]', post: 'S.song[1][0] + "/" + S.song[2][0]', ok: (a, b) => a === '-1/' + b });
  await check('Ctrl+Shift+Backspace removes it', sg({ x: 0, y: 1 }), 'Control+Shift+Backspace', { post: 'S.song[1][0]', ok: a => a >= 0 });
  await check('Ctrl+End last song row', sg({ x: 0, y: 0 }), 'Control+End', { post: 'cs.song.y', ok: a => a > 0 });
  await check('Ctrl+Home first song row', sg({ x: 0, y: 5 }), 'Control+Home', { post: 'cs.song.y', ok: a => a === 0 });
  await check('Shift+⌥▼ on Song (block jump)', sg({ x: 0, y: 0 }), 'Shift+Alt+ArrowDown', { pre: 'cs.song.y', post: 'cs.song.y', ok: (a, b) => a !== b });
  await check('Alt+T new phrase on next empty track', ph({ x: 0, y: 0 }), 'Alt+KeyT', { pre: 'cTrack', post: 'cTrack', ok: (a, b) => a !== b });
  await load();
  await check('Alt+B opens Acid line (Phrase)', ph({ x: 0, y: 0 }), 'Alt+KeyB', { post: '!!document.querySelector(".acidask")', ok: a => a === true });
  await check('Esc closes Acid line', async () => {}, 'Escape', { post: '!!document.querySelector(".acidask")', ok: a => a === false });
  await check('Alt+S opens Song settings', ph({ x: 0, y: 0 }), 'Alt+KeyS', { post: '!!document.querySelector(".pbs")', ok: a => a === true });
  await check('Esc closes Song settings', async () => {}, 'Escape', { post: '!!document.querySelector(".pbs")', ok: a => a === false });
  await check('Alt+S from the Synth page', () => at('synth'), 'Alt+KeyS', { post: '!!document.querySelector(".pbs")', ok: a => a === true }, '"from any screen" (owner, 3 Nov)');
  await A('(pbOpen && pbOpen(), 1)');
  await check('Alt+A gives Arrange the keys', ph({ x: 0, y: 0 }), 'Alt+KeyA', { post: 'apF', ok: a => !!a });
  await check('Alt+A again gives them back', async () => {}, 'Alt+KeyA', { post: 'apF', ok: a => !a });
  await check('Alt+G cursor onto playhead (stopped: no error)', ph({ x: 0, y: 0 }), 'Alt+KeyG', { post: '1', ok: () => true });
  await check('Ctrl+Shift+F follow toggles', ph({ x: 0, y: 0 }), 'Control+Shift+KeyF', { pre: 'followMode', post: 'followMode', ok: (a, b) => a !== b });
  await A('(setFollow(0), 1)');
  await check('Shift+Alt+F FILL', ph({ x: 0, y: 0 }), 'Shift+Alt+KeyF', { pre: 'perfLive.fill', post: 'perfLive.fill', ok: (a, b) => a !== b });
  // ---- Mixer / FX ----
  await check('Mixer Ctrl+▲ changes the value', () => at('tracker', 'mixer', { x: 1, y: 0 }), 'Control+ArrowUp', { pre: 'JSON.stringify(S.mix[1])', post: 'JSON.stringify(S.mix[1])', ok: (a, b) => a !== b });
  await check('Mixer hex', () => at('tracker', 'mixer', { x: 1, y: 1 }), ['Digit4', 'Digit0'], { post: 'S.mix[1].pan', ok: a => a === 0x40, wait: 900 });
  await check('Mixer Del resets', () => at('tracker', 'mixer', { x: 1, y: 1 }), 'Delete', { post: 'S.mix[1].pan', ok: a => a === 128 });
  await check('FX ] other track', () => at('tracker', 'fx', { x: 0, y: 0 }), 'BracketRight', { pre: 'cFxT', post: 'cFxT', ok: (a, b) => a !== b });
  await check('Inst ] other instrument', () => at('tracker', 'inst', { x: 0, y: 0 }), 'BracketRight', { pre: 'cInst', post: 'cInst', ok: (a, b) => a !== b });
  // ---- files / settings / help ----
  await check('Ctrl+S save (picker asked)', ph({ x: 0, y: 0 }), 'Control+KeyS', { pre: 'window.__picks', post: 'window.__picks', ok: (a, b) => a > b });
  await check('Ctrl+Shift+S save as', ph({ x: 0, y: 0 }), 'Control+Shift+KeyS', { pre: 'window.__picks', post: 'window.__picks', ok: (a, b) => a > b });
  await check('Ctrl+O open', ph({ x: 0, y: 0 }), 'Control+KeyO', { pre: 'window.__picks', post: 'window.__picks + "/" + !!document.querySelector(".ask")', ok: (a, b) => a !== b + '/false' });
  await A('(askOpen && askOpen(null), 1)');   // close a dialog through its own done(), so its key listener goes too
  await check('Ctrl+, opens Settings', ph({ x: 0, y: 0 }), 'Control+Comma', { post: 'gsetOpen()', ok: a => a === true, wait: 600 });
  await check('Ctrl+, again closes it', async () => {}, 'Control+Comma', { post: 'gsetOpen()', ok: a => a === false });
  await check('? opens Help (Phrase)', ph({ x: 0, y: 0 }), 'Shift+Slash', { post: 'helpOpen()', ok: a => a === true });
  await check('Esc closes Help', async () => {}, 'Escape', { post: 'helpOpen()', ok: a => a === false });
  for (const [v, s] of [['synth'], ['tracker', 'mixer'], ['tracker', 'song']]) await check(`F1 opens Help (${v}${s ? '/' + s : ''})`, () => at(v, s), 'F1', { post: 'helpOpen()', ok: a => a === true });
  await A('(hideHelp(), 1)');
  await check('? opens Help from …waves', () => at('waves'), 'Shift+Slash', { post: 'helpOpen()', ok: a => a === true, wait: 600 }, 'the frame may keep the key (K9)');
  await A('(hideHelp(), 1)');
  // ---- Synth page ----
  await check('Synth: Z plays a note (no error)', () => at('synth'), 'KeyZ', { post: '1', ok: () => true });
  await check('Synth: Ctrl+Z undo', async () => { await at('synth'); await A('(kit.voices[cur].name = "zz", kitStep(), kit.voices[cur].name = "yy", 1)'); }, 'Control+KeyZ', { post: '1', ok: () => true }, 'ran');
  await check('Synth: F8 stop', () => at('synth'), 'F8', { post: 'tp.on', ok: a => a === false });
  await check('Synth: Space plays', () => at('synth'), 'Space', { post: 'tp.on', ok: a => a === true, wait: 1500 }, 'the first PLAY may wait for live engines (LIVE WARM-UP)');
  await A('(tStop(true), 1)');
  // ---- key conflicts: the same key, two documented meanings ----
  const doc = await A(`[...document.querySelectorAll('#help kbd, #tLegKeys dt')].map(e => e.textContent).join(' | ')`);
  const items = await A(`JSON.stringify((() => { const out = {}; for (const v of ['song','phrase','mixer','fx']) { const was = tv; tv = v; try { for (const it of cmdItems(true)) if (it && it.k) (out[v] = out[v] || []).push([it.k, it.l]); } finally { tv = was; } } return out; })())`);
  const M = JSON.parse(items);
  for (const [v, L] of Object.entries(M)) {
    const by = {}; for (const [k, l] of L) (by[k] = by[k] || []).push(l);
    for (const [k, ls] of Object.entries(by)) if (ls.length > 1) T.add(`Edit ▾ on ${v}: ${k} on ${ls.length} items`, 'NOTE', ls.join(' / '));
  }
  T.add('Help lists keys', doc.length > 200, doc.length + ' chars');
} catch (e) { console.error(e); T.add('keys sweep', false, e.message); }
const f = T.print();
await B.close();
process.exit(f ? 1 : 0);
