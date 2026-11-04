// SWEEP: Song / Arrange / Phrase / Mixer editing operations, each followed by Undo and Redo.
//   node tests/sweep/editing.mjs
// Each operation starts from ELECTRO freshly loaded, puts the cursor where the operation needs it, runs the app's own
// function (the one Edit ▾ / the right-click menu / the key calls), checks the song changed the way it should and stays
// consistent (every song cell's sect exists, every sect row's phrase exists), then Undo must give back exactly the song
// before (JSON of S compared) and Redo exactly the song after. Dialogs (askText / askPick) are answered through the DOM.
import { open, ev, table } from './lib.mjs';
import fs from 'fs';
import path from 'path';
import { ROOT } from './lib.mjs';

const { page, errors, close } = await open();
const rows = [];
const add = (n, ok, note) => { rows.push([n, ok ? 'PASS' : 'FAIL', note || '']); if (process.env.SWEEP_LOG) process.stderr.write(`${ok ? 'PASS' : 'FAIL'} ${n}\n`); };
const ELECTRO = fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8');

// Answer the next dialog: a list item index, a text value, or true (OK)
async function answer(a) {
  for (let i = 0; i < 100; i++) { if (await page.locator('.ask').count()) break; await page.waitForTimeout(50); }
  if (!(await page.locator('.ask').count())) return false;
  if (typeof a === 'number') await page.locator('.ask .askitem').nth(a).click();
  else if (typeof a === 'string') { await page.locator('.ask input').fill(a); await page.locator('.ask .btnrow .ok').click(); }
  else await page.locator('.ask .btnrow .ok').click();
  await page.waitForTimeout(100);
  return true;
}

try {
  await ev(page, `window.__ed = {
    k: {IC, MT, FX0, VC, NT},
    // consistency: song cells point at sects with a phrase or empty sects (allowed), sect rows at phrase arrays
    check(){ const bad = [];
      S.song.forEach((r, y) => r.forEach((v, t) => { if (v >= 0 && !Array.isArray(S.chains[v])) bad.push('song ' + y + ',' + t + ' -> missing sect ' + v); }));
      // (a sect row may point at a null phrase: the app reads null as an empty phrase, and ELECTRO has one: sect F3 -> phrase F3)
      S.chains.forEach((c, i) => { if (c) c.forEach((r, k) => { if (r[0] >= S.phrases.length || (S.phrases[r[0]] != null && !Array.isArray(S.phrases[r[0]]))) bad.push('sect ' + i + ' row ' + k + ' -> bad phrase ' + r[0]); }); });
      for (const p of S.phrases) if (p && (p.length !== PROWS || p.some(r => r.length !== RW || r.some(v => !Number.isInteger(v))))) { bad.push('malformed phrase'); break; }
      return bad.slice(0, 3); },
    j(){ return JSON.stringify(S); },
    uses(){ const m = new Map(); for (const r of S.song) for (const v of r) if (v >= 0) m.set(v, (m.get(v) || 0) + 1); return m; },
  }; 1`);

  // ELECTRO is loaded once (with its kit); each operation then starts from that song, put back in place (as undo does)
  await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), ELECTRO); await page.waitForTimeout(500);
  await ev(page, `(window.__ed.base = JSON.stringify(S), 1)`);
  const load = async () => { await ev(page, `(() => { if (askOpen) askOpen(null); if (tp.on) tStop(true); loopSel = false; loopArm = null; loopPat = false; const n = normSong(JSON.parse(__ed.base)); for (const k of Object.keys(S)) if (!(k in n)) delete S[k]; Object.assign(S, n); saveS(); return 1; })()`); await page.waitForTimeout(30);
    await ev(page, `(undo.past.length = 0, undo.future.length = 0, undo.cur = JSON.stringify(S), selA = null, clip = null, sxPicks = [], 1)`); await page.waitForTimeout(50); };
  // nav: {view:'tracker', tv, songY, songX, track, phrase, x, y}
  const nav = o => page.evaluate(o => window.__seqTest.nav(o), o);

  // One operation: setup (in page), run (in page, may be async + dialog answers), verify (in page) -> [ok, note]
  async function op(name, { setup = '', run, answers = [], verify = 'return [true, ""]', noChange = false }) {
    try {
      await load();
      if (setup) await ev(page, `(() => { ${setup} })()`);
      await page.waitForTimeout(30);
      const before = await ev(page, '__ed.j()'); await ev(page, `(undo.cur = JSON.stringify(S), 1)`);
      const pr = page.evaluate(src => { const f = window.__seqTest.ev('(async () => { ' + src + ' })'); return f(); }, run);
      for (const a of answers) await answer(a);
      await pr; await page.waitForTimeout(80);   // snap() runs in a microtask
      const after = await ev(page, '__ed.j()');
      const [vok, vnote] = await ev(page, `(() => { ${verify} })()`);
      const bad = await ev(page, '__ed.check()');
      if (noChange) { add(name, after === before && vok, vnote || 'no change expected'); return; }
      if (after === before) { add(name, false, 'song did not change. ' + (vnote || '')); return; }
      await ev(page, 'undoStep(-1)'); await page.waitForTimeout(50);
      const u = await ev(page, '__ed.j()');
      await ev(page, 'undoStep(1)'); await page.waitForTimeout(50);
      const r = await ev(page, '__ed.j()');
      const uok = u === before, rok = r === after;
      let diff = '';
      if (!uok) diff = ' undo differs: ' + firstDiff(JSON.parse(before), JSON.parse(u));
      if (!rok) diff += ' redo differs: ' + firstDiff(JSON.parse(after), JSON.parse(r));
      add(name, vok && !bad.length && uok && rok, `${vnote || ''}${bad.length ? ' INCONSISTENT: ' + bad.join('; ') : ''} undo ${uok ? 'ok' : 'BAD'} redo ${rok ? 'ok' : 'BAD'}${diff}`);
    } catch (e) { add(name, false, 'exception: ' + e.message.slice(0, 300)); }
  }
  function firstDiff(a, b, p = '') {
    if (JSON.stringify(a) === JSON.stringify(b)) return '';
    if (a && b && typeof a === 'object' && typeof b === 'object') { for (const k of new Set([...Object.keys(a), ...Object.keys(b)])) { const d = firstDiff(a[k], b[k], p + '.' + k); if (d) return d; } }
    return `${p}: ${JSON.stringify(a)?.slice(0, 80)} vs ${JSON.stringify(b)?.slice(0, 80)}`;
  }

  const SONG = `setView('tracker'); go('song'); cs.song.y = 0; cs.song.x = 0; selA = null;`;
  const PHRASE = (p, T = 0) => `setView('tracker'); cs.song.y = 0; cs.song.x = ${T}; cTrack = ${T}; cSect = S.song[0][${T}]; cPhrase = ${p || 'firstPhrase(S.song[0][' + T + '])'}; go('phrase'); cs.phrase.x = 0; cs.phrase.y = 0; selA = null;`;
  // ---------------- SONG ----------------
  await op('Song: Delete block (phrase too)', { setup: SONG + `window.__b = S.song[0][0];`, run: `cmdAction('clr')`, verify: `return [S.song[0][0] < 0, 'cell cleared']` });
  await op('Song: Remove from song (keep phrase)', { setup: SONG + `window.__b = S.song[0][0]; window.__p = JSON.stringify(S.phrases[firstPhrase(window.__b)]);`, run: `cmdAction('rmv')`, verify: `return [S.song[0][0] < 0 && JSON.stringify(S.phrases[firstPhrase(window.__b)]) === window.__p, 'cell gone, phrase kept']` });
  await op('Song: Cut + Paste block', { setup: SONG + `window.__b = S.song[0][0]; let y = 0; while (S.song[y][0] >= 0) y++; window.__y = y;`, run: `cmdAction('cut'); cs.song.y = window.__y; cmdAction('paste');`, verify: `return [S.song[0][0] < 0 && S.song[window.__y][0] === window.__b, 'moved to row ' + window.__y]` });
  await op('Song: Copy + Paste block (shares)', { setup: SONG + `window.__b = S.song[0][0]; let y = 0; while (S.song[y][0] >= 0) y++; window.__y = y;`, run: `cmdAction('copy'); cs.song.y = window.__y; cmdAction('paste');`, verify: `return [S.song[0][0] === window.__b && S.song[window.__y][0] === window.__b, 'same block on both rows']` });
  await op('Song: Mix-paste (empty cells only)', { setup: SONG + `selA = {x:1, y:1, v:'song'}; copySel(false); cs.song.y = 0; cs.song.x = 0; S.song[0][1] = -1; saveS(); window.__k = S.song[0][0];`, run: `pasteMix()`, verify: `return [S.song[0][0] === window.__k && S.song[0][1] >= 0, 'filled cell kept, empty filled']` });
  await op('Song: Clone + paste (unique)', { setup: SONG + `window.__b = S.song[0][0]; let y = 0; while (S.song[y][0] >= 0) y++; window.__y = y;`, run: `cmdAction('clone'); cs.song.y = window.__y; cmdAction('paste');`, verify: `const v = S.song[window.__y][0]; return [v >= 0 && v !== window.__b && JSON.stringify(S.phrases[firstPhrase(v)]) === JSON.stringify(S.phrases[firstPhrase(window.__b)]) && firstPhrase(v) !== firstPhrase(window.__b), 'new block ' + v + ' same notes, own phrase']` });
  await op('Song: Clone down', { setup: SONG, run: `cmdAction('cldn')`, verify: `return [S.song[1][0] >= 0 && S.song[1][0] !== S.song[0][0], 'row 1 got a new block']` });
  await op('Song: Duplicate pattern (Ctrl+D)', { setup: SONG + `window.__r0 = S.song[0].filter(v => v >= 0).length; window.__r1 = JSON.stringify(S.song[1]);`, run: `dupKey(false)`, verify: `return [S.song[1].filter(v => v >= 0).length === window.__r0 && JSON.stringify(S.song[2]) === window.__r1 && S.song[1].every((v, t) => v < 0 || v !== S.song[0][t]), 'row 0 duplicated below as unique copies, old row 1 now row 2']` });
  await op('Song: Insert paste', { setup: SONG + `selA = {x:MT, y:1, v:'song'}; cs.song.x = 0; cs.song.y = 0; copySel(false); cs.song.x = 0; cs.song.y = 0; window.__r1 = JSON.stringify(S.song[1]); window.__r0 = JSON.stringify(S.song[0]);`, run: `insertPaste()`, verify: `return [JSON.stringify(S.song[3]) === window.__r1 && JSON.stringify(S.song[1]) === window.__r0, 'rows 0-1 inserted at 1, old row 1 at 3']` });
  await op('Song: Make unique', { setup: SONG + `window.__b = S.song[0][0];`, run: `cmdAction('uniq')`, verify: `return [S.song[0][0] !== window.__b && S.song[0][0] >= 0, 'new block ' + S.song[0][0]]` });
  await op('Song: Insert song row', { setup: SONG + `window.__r0 = JSON.stringify(S.song[0]);`, run: `songRowInsert()`, verify: `return [S.song[0].every(v => v < 0) && JSON.stringify(S.song[1]) === window.__r0, 'row 0 empty, old row 0 at 1']` });
  await op('Song: Remove song row', { setup: SONG + `window.__r1 = JSON.stringify(S.song[1]);`, run: `songRowRemove()`, verify: `return [JSON.stringify(S.song[0]) === window.__r1, 'row 1 moved up']` });
  await op('Song: Remove rows (menu, 0-1)', { setup: SONG + `window.__r2 = JSON.stringify(S.song[2]);`, run: `songRowRemove({y0:0, y1:1})`, verify: `return [JSON.stringify(S.song[0]) === window.__r2, 'rows 0-1 gone']` });
  await op('Song: Delete selection', { setup: SONG + `selA = {x:3, y:1, v:'song'};`, run: `cmdAction('clr')`, verify: `return [[0,1].every(y => [0,1,2,3].every(t => S.song[y][t] < 0)), 'rect cleared']` });
  await op('Song: Delete keeps shared phrase', { setup: SONG + `S.song[100][0] = S.song[0][0]; saveS(); window.__b = S.song[0][0]; window.__p = JSON.stringify(S.phrases[firstPhrase(window.__b)]);`, run: `cmdAction('clr')`, verify: `return [S.song[100][0] === window.__b && JSON.stringify(S.phrases[firstPhrase(window.__b)]) === window.__p, 'other copy still plays it']` });
  await op('Song: Mute block (M / menu)', { setup: SONG, run: `sxMuteRect({x0:0, x1:0, y0:0, y1:0})`, verify: `return [blockMuted(S, 0, 0), 'muted']` });
  await op('Song: Duplicate track with patterns', { setup: SONG + `cs.song.x = 9;`, run: `dupTrack(true, 9)`, verify: `const to = 10; return [S.song.every((r, y) => (r[9] >= 0) === (r[to] >= 0)) && S.song.every(r => r[9] < 0 || r[to] !== r[9]), 'track 10 -> 11, unique blocks on the same rows']` });
  await op('Song: Duplicate track, none free (refused)', { setup: SONG, run: `dupTrack(true, 0)`, noChange: true, verify: `return [true, 'ELECTRO uses every mono track: "No empty mono track" (nothing changes)']` });
  await op('Song: Mute track (/)', { setup: SONG, run: `muteKey()`, verify: `return [S.mix[0].mute === true, 'mix mute']` });
  await op('Song: Solo track (Ctrl+/)', { setup: SONG, run: `soloKey(false)`, verify: `return [S.mix[0].solo === true, 'mix solo']` });
  await op('Song: Name track', { setup: SONG, run: `const p = nameTrackHere(); await p;`, answers: ['KICKS'], verify: `return [JSON.stringify(S).includes('KICKS'), 'name stored']` });
  await op('Song: Name a section', { setup: SONG, run: `await sxSecEdit(0)`, answers: ['INTRO'], verify: `return [(S.secs || []).some(s => s.y === 0 && s.n === 'INTRO'), JSON.stringify(S.secs)]` });
  await op('Song: Colour block', { setup: SONG, run: `const p = sxColour({x0:0, x1:0, y0:0, y1:0}); await new Promise(r => setTimeout(r, 100)); document.querySelectorAll('.askswatch button')[2].click(); document.querySelector('.ask .btnrow .ok').click(); await p;`, verify: `return [S.bcol && S.bcol[S.song[0][0]] === 2, JSON.stringify(S.bcol)]` });
  await op('Song: Transpose block up (Alt+.)', { setup: SONG + `window.__p = firstPhrase(S.song[0][0]); window.__n = S.phrases[window.__p].find(r => r[0] >= 0 && r[0] < 120)[0];`, run: `transposeSel(1)`, verify: `const n = S.phrases[window.__p].find(r => r[0] >= 0 && r[0] < 120)[0]; return [n === window.__n + 1, window.__n + ' -> ' + n]` });
  await op('Song: Loop these rows (no song change)', { setup: SONG, run: `sxLoopRows(0, 1)`, noChange: true, verify: `return [true, 'loop state only']` });
  // UNIQUE PER TRACK: paste a block from track 1 onto track 2 -> own copy; off -> shared
  await op('Song: UNIQUE PER TRACK on (paste)', { setup: SONG + `sxSet('uniq', true); window.__b = S.song[0][0]; let y = 0; while (S.song[y][1] >= 0) y++; window.__y = y;`, run: `cmdAction('copy'); cs.song.x = 1; cs.song.y = window.__y; cmdAction('paste');`, verify: `const v = S.song[window.__y][1]; return [v >= 0 && v !== window.__b, 'pasted on another track as ' + v + ' (own copy)']` });
  await op('Song: UNIQUE PER TRACK off (paste)', { setup: SONG + `sxSet('uniq', false); window.__b = S.song[0][0]; let y = 0; while (S.song[y][1] >= 0) y++; window.__y = y;`, run: `cmdAction('copy'); cs.song.x = 1; cs.song.y = window.__y; cmdAction('paste'); sxSet('uniq', true);`, verify: `const v = S.song[window.__y][1]; return [v === window.__b, 'shared block ' + v]` });
  await op('Song: master column keeps own block', { setup: SONG + `window.__b = S.song[0][0]; let y = 0; while (S.song[y][MT] >= 0) y++; window.__y = y;`, run: `cmdAction('copy'); cs.song.x = MT; cs.song.y = window.__y; cmdAction('paste');`, verify: `const v = S.song[window.__y][MT]; return [v >= 0 && v !== window.__b, 'master got ' + v]` });
  // ---------------- PHRASE ----------------
  await op('Phrase: type a note (key Z)', { setup: PHRASE() + `cs.phrase.y = 1; inMode = 'keys';`, run: `document.dispatchEvent(new KeyboardEvent('keydown', {key:'z', code:'KeyZ', bubbles:true})); document.dispatchEvent(new KeyboardEvent('keyup', {key:'z', code:'KeyZ', bubbles:true}));`, verify: `return [S.phrases[cPhrase][1][0] >= 0, 'note ' + S.phrases[cPhrase][1][0]]` });
  await op('Phrase: Delete cell', { setup: PHRASE(), run: `cmdAction('clr')`, verify: `return [S.phrases[cPhrase][0][0] < 0, 'cleared']` });
  await op('Phrase: Select all + Delete', { setup: PHRASE() + `selectAll(); selectAll();`, run: `cmdAction('clr')`, verify: `return [S.phrases[cPhrase].slice(0, plen(cPhrase)).every(r => r.every(v => v < 0)), 'phrase empty']` });
  await op('Phrase: Copy lines + paste in another phrase', { setup: PHRASE() + `selectAll(); selectAll(); copySel(false); window.__src = JSON.stringify(S.phrases[cPhrase].slice(0, plen(cPhrase))); window.__p0 = cPhrase; cPhrase = S.phrases.findIndex((p, i) => i > 0 && (!p || p.every(r => r.every(v => v < 0)))); if (!S.phrases[cPhrase]) S.phrases[cPhrase] = freshPhrase(); cs.phrase.y = 0; selA = null;`, run: `cmdAction('paste')`, verify: `return [JSON.stringify(S.phrases[cPhrase].slice(0, plen(window.__p0))) === window.__src, 'copied to ' + cPhrase]` });
  await op('Phrase: Cut cell + paste', { setup: PHRASE(), run: `cmdAction('cut'); cs.phrase.y = 1; cmdAction('paste');`, verify: `return [S.phrases[cPhrase][0][0] < 0 && S.phrases[cPhrase][1][0] >= 0, 'moved']` });
  await op('Phrase: Mix-paste', { setup: PHRASE() + `selectAll(); selectAll(); copySel(false); cs.phrase.y = 1; window.__n = S.phrases[cPhrase].slice(0, plen(cPhrase)).filter(r => r[0] >= 0).length;`, run: `pasteMix()`, verify: `const n = S.phrases[cPhrase].slice(0, plen(cPhrase)).filter(r => r[0] >= 0).length; return [n > window.__n, 'notes ' + window.__n + ' -> ' + n]` });
  await op('Phrase: Insert row', { setup: PHRASE() + `window.__r0 = JSON.stringify(S.phrases[cPhrase][0]);`, run: `rowOp('insert')`, verify: `return [S.phrases[cPhrase][0].slice(0, 4).every(v => v < 0) && JSON.stringify(S.phrases[cPhrase][1]) === window.__r0 || S.phrases[cPhrase][0][0] < 0, 'blank row in']` });
  await op('Phrase: Remove row', { setup: PHRASE() + `window.__r1 = JSON.stringify(S.phrases[cPhrase][1]);`, run: `rowOp('remove')`, verify: `return [JSON.stringify(S.phrases[cPhrase][0]) === window.__r1, 'row 1 moved up']` });
  await op('Phrase: Remove selected rows', { setup: PHRASE() + `selA = {x:0, y:0, v:'phrase', line:true}; cs.phrase.y = 1; window.__r2 = JSON.stringify(S.phrases[cPhrase][2]);`, run: `rowsRemove()`, verify: `return [JSON.stringify(S.phrases[cPhrase][0]) === window.__r2, 'rows 0-1 gone']` });
  await op('Phrase: Transpose up a semitone', { setup: PHRASE() + `window.__n = S.phrases[cPhrase][0][0];`, run: `transposeSel(1)`, verify: `return [S.phrases[cPhrase][0][0] === window.__n + 1, window.__n + ' -> ' + S.phrases[cPhrase][0][0]]` });
  await op('Phrase: Transpose down an octave', { setup: PHRASE() + `window.__n = S.phrases[cPhrase][0][0];`, run: `transposeSel(-12)`, verify: `return [S.phrases[cPhrase][0][0] === window.__n - 12, window.__n + ' -> ' + S.phrases[cPhrase][0][0]]` });
  await op('Phrase: Interpolate volume', { setup: PHRASE() + `const P = S.phrases[cPhrase]; P[0][VC] = 0; P[4][VC] = 0x80; cs.phrase.x = VC; selA = {x:VC, y:0, v:'phrase'}; cs.phrase.y = 4; saveS();`, run: `interpSel()`, verify: `const P = S.phrases[cPhrase]; return [P[2][VC] === 0x40, 'row 2 vol ' + P[2][VC]]` });
  await op('Phrase: Volume… scale 50%', { setup: PHRASE() + `window.__v = S.phrases[cPhrase][0][VC] >= 0 ? S.phrases[cPhrase][0][VC] : 255;`, run: `await volSel()`, answers: [0, '50'], verify: `return [S.phrases[cPhrase][0][VC] === Math.round(window.__v / 2), 'vol ' + S.phrases[cPhrase][0][VC]]` });
  await op('Phrase: Volume… fade out', { setup: PHRASE(), run: `await volSel()`, answers: [2], verify: `const P = S.phrases[cPhrase]; return [P[0][VC] >= 0, 'first ' + P[0][VC]]` });
  await op('Phrase: Instrument… set 05', { setup: PHRASE(), run: `await instSel()`, answers: [0, '05'], verify: `return [S.phrases[cPhrase].filter(r => r[0] >= 0 && r[0] < 120).every(r => r[IC] === 5), 'all 05']` });
  await op('Phrase: Instrument… swap', { setup: PHRASE() + `window.__i = S.phrases[cPhrase][0][IC];`, run: `await instSel()`, answers: [1, (0).toString(16).padStart(2, '0') + ' 07'], verify: `return [S.phrases[cPhrase][0][IC] === (window.__i === 0 ? 7 : window.__i === 7 ? 0 : window.__i), 'ins ' + window.__i + ' -> ' + S.phrases[cPhrase][0][IC]]` });
  await op('Phrase: Shrink', { setup: PHRASE() + `window.__r2 = JSON.stringify(S.phrases[cPhrase][2]);`, run: `stretchSel(false)`, verify: `return [JSON.stringify(S.phrases[cPhrase][1]) === window.__r2, 'row 2 -> row 1']` });
  await op('Phrase: Expand', { setup: PHRASE() + `window.__r1 = JSON.stringify(S.phrases[cPhrase][1]);`, run: `stretchSel(true)`, verify: `return [JSON.stringify(S.phrases[cPhrase][2]) === window.__r1, 'row 1 -> row 2']` });
  await op('Phrase: Acid line generator', { setup: PHRASE(), run: `acidLine(); await new Promise(r => setTimeout(r, 100)); const g = [...document.querySelectorAll('.acidask .btnrow button')]; g.find(b => /Generate/.test(b.textContent)).click(); await new Promise(r => setTimeout(r, 50)); g.find(b => /Done/.test(b.textContent)).click();`, verify: `return [S.phrases[cPhrase].slice(0, plen(cPhrase)).some(r => r[0] >= 0), 'notes written']` });
  await op('Phrase: Euclidean fill 5 16', { setup: PHRASE() + `S.phrases[cPhrase] = freshPhrase(); S.phrases[cPhrase][0][0] = 48; S.phrases[cPhrase][0][IC] = 0; saveS();`, run: `await euclidFill()`, answers: ['5 16'], verify: `const n = S.phrases[cPhrase].slice(0, 16).filter(r => r[0] >= 0 && r[0] < 120).length; return [n === 5, n + ' hits']` });
  await op('Phrase: Clear selection rect (Del)', { setup: PHRASE() + `selA = {x:0, y:0, v:'phrase'}; cs.phrase.y = 3; cs.phrase.x = IC;`, run: `cmdAction('clr')`, verify: `return [S.phrases[cPhrase].slice(0, 4).every(r => r.slice(0, IC + 1).every(v => v < 0)), 'cleared']` });
  await op('Phrase: Duplicate track (no notes)', { setup: PHRASE(null, 9) + `S.mix[9].vol = 0x77; saveS();`, run: `dupKey(false)`, verify: `const t = S.mix.findIndex((m, i) => i > 9 && i < 16 && m.vol === 0x77); return [t > 9 && S.song.every(r => r[t] < 0), 'mix to track ' + (t + 1) + ', no blocks']` });
  await op('Phrase: Duplicate track with patterns', { setup: PHRASE(null, 9), run: `dupKey(true)`, verify: `return [S.song.every((r, y) => (r[9] >= 0) === (r[10] >= 0)), 'track 10 -> 11 with blocks']` });
  // ---------------- MIXER ----------------
  await op('Mixer: bump volume', { setup: `setView('tracker'); go('mixer'); cs.mixer.x = 0; cs.mixer.y = 0; window.__v = JSON.stringify(S.mix[0]);`, run: `cmdAction('b:1')`, verify: `return [JSON.stringify(S.mix[0]) !== window.__v, JSON.stringify(S.mix[0]).slice(0, 80)]` });
  await op('Mixer: duplicate track', { setup: `setView('tracker'); go('mixer'); cs.mixer.x = 9; cs.mixer.y = 0; S.mix[9].pan = 0x30; saveS();`, run: `dupKey(false)`, verify: `const t = S.mix.findIndex((m, i) => i > 9 && m.pan === 0x30); return [t > 9, 'to ' + (t + 1)]` });
  // ---------------- INSTRUMENT ----------------
  await op('Inst: bump a value', { setup: `setView('tracker'); go('inst'); cInst = 0; cs.inst.y = 0; cs.inst.x = 0; window.__i = JSON.stringify(S.insts[0]);`, run: `cmdAction('b:1')`, verify: `return [JSON.stringify(S.insts[0]) !== window.__i, 'inst 0 changed']` });
  // ---------------- PROJECT / transport-state ops (no song change) ----------------
  await op('Loop pattern toggle', { setup: SONG, run: `toggleLoopPat(); window.__lp = loopPat; toggleLoopPat();`, noChange: true, verify: `return [window.__lp === true && loopPat === false, 'on then off']` });
  await op('Loop selection toggle', { setup: SONG, run: `selA = {x:0, y:1, v:'song'}; cs.song.y = 0; toggleLoop(); window.__la = JSON.stringify(loopArm); toggleLoop();`, noChange: true, verify: `return [window.__la && window.__la.includes('"y1":1'), 'armed ' + window.__la]` });
  await op('Follow toggle', { setup: SONG + `window.__f = followMode;`, run: `followKey(); window.__f2 = followMode; followKey();`, noChange: true, verify: `return [window.__f2 !== window.__f && followMode === window.__f, window.__f + ' -> ' + window.__f2 + ' -> ' + followMode]` });
  // Undo of a sequence: 3 edits, undo 3x -> original
  try {
    await load(); await ev(page, SONG); await ev(page, `(undo.cur = JSON.stringify(S), 1)`); const b = await ev(page, '__ed.j()');
    await ev(page, `cmdAction('cldn')`); await page.waitForTimeout(30); await ev(page, `cs.song.y = 5; cmdAction('clr')`); await page.waitForTimeout(30); await ev(page, `songRowInsert()`); await page.waitForTimeout(30);
    const a = await ev(page, '__ed.j()');
    for (let i = 0; i < 3; i++) { await ev(page, 'undoStep(-1)'); await page.waitForTimeout(30); }
    const u = await ev(page, '__ed.j()');
    for (let i = 0; i < 3; i++) { await ev(page, 'undoStep(1)'); await page.waitForTimeout(30); }
    const r = await ev(page, '__ed.j()');
    add('Undo x3 / Redo x3 chain', u === b && r === a, `back to start ${u === b}, forward again ${r === a}`);
  } catch (e) { add('Undo x3 / Redo x3 chain', false, e.message); }
  // Undo / redo by key (Ctrl+Z, Ctrl+Shift+Z) in the tracker
  try {
    await load(); await ev(page, SONG + `inMode = 'keys';`); await ev(page, `(undo.cur = JSON.stringify(S), 1)`); const b = await ev(page, '__ed.j()');
    await ev(page, `cmdAction('clr')`); await page.waitForTimeout(30); const a = await ev(page, '__ed.j()');
    await page.keyboard.press('Control+z'); await page.waitForTimeout(100); const u = await ev(page, '__ed.j()');
    await page.keyboard.press('Control+Shift+z'); await page.waitForTimeout(100); const r = await ev(page, '__ed.j()');
    await page.keyboard.press('Control+z'); await page.keyboard.press('Control+y'); await page.waitForTimeout(100); const r2 = await ev(page, '__ed.j()');
    add('Undo/redo keys (Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y)', u === b && r === a && r2 === a, `undo ${u === b} redo ${r === a} ctrl+y ${r2 === a}`);
  } catch (e) { add('Undo/redo keys', false, e.message); }
  // Edit menu: every item of cmdItems(true) has a function (and none throws when listed on each screen)
  try {
    const m = await ev(page, `(() => { const out = []; for (const v of ['song', 'phrase', 'sect', 'inst', 'mixer', 'fx', 'project']) { go(v); const flat = []; const walk = L => L.forEach(x => { if (!x) return; if (x.sub) walk(x.sub); else if (!x.h) flat.push(x); }); walk(cmdItems(true)); out.push([v, flat.length, flat.filter(x => typeof x.f !== 'function').map(x => x.l)]); } go('song'); return out; })()`);
    add('Edit ▾ items on every screen', m.every(x => !x[2].length), m.map(x => x[0] + ':' + x[1]).join(' '));
  } catch (e) { add('Edit ▾ items on every screen', false, e.message); }
} finally {
  if (errors.length) add('no page errors', false, errors.slice(0, 5).join(' | ')); else add('no page errors', true, '');
  await close();
}
process.exit(table(rows, 'EDITING (tests/sweep/editing.mjs)') ? 1 : 0);
