// MONO / POLY COUNTS, stage 3 (14 Nov; docs/MONO-POLY-PLAN.md): the Settings rows MONO TRACKS / POLY TRACKS, the question before hiding a track that holds something, the notice,
// the reason a refused growth shows, and the voice of a track that takes an over slot. Cut out of index.html and run in a vm, as mono-poly-counts.test.js does.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm');
const H = require('./helpers');
const src = H.read('index.html');
const cut = (a, b) => { const i = src.indexOf(a), j = src.indexOf(b, i); assert.ok(i >= 0 && j > i, 'moved: ' + a); return src.slice(i, j); };
const line = a => { const i = src.indexOf(a); assert.ok(i >= 0, 'moved: ' + a); return src.slice(i, src.indexOf('\n', i)); };
const code = cut('function ntrPermOld(', '// The window of tracks a Song or Mixer line has room for');
const splitCode = cut('function normSplit(', 'function normSong(');
const rowsCode = cut("{l:'MONO TRACKS'", "{l:'TEMPO'");
const NT = 32;
function rig(o = {}) {
  const c = { NT, MT: NT, SCK_SEND: 8, NPAD: 8, TRIM_0: 128, vtrk: !!o.vtrk, kit: o.vtrk ? { voices: [] } : null, perfLive: { sent: new Map(), pno: new Map() },
    clamp: (v, a, b) => Math.max(a, Math.min(b, v)), d2: n => String(n).padStart(2, '0'), freshMix: () => ({ vol: 224, trim: 128, pan: 128, mute: false, solo: false, sc: false, nm: '', col: 0 }),
    sckTrack: v => { v = Math.round(+v) || 0; return v > 8 ? v - 9 : -1; }, cTrack: 0, cs: { song: { x: 0 }, mixer: { x: 0 } }, cFxT: 0, tp: {}, busPlanH: 1, rt: null, CH: {}, chainsDropFrame() {}, PumpFX: { _notifyAll() {} },
    dirty: [], cache: [], arrSig: 'x', draws: 0, draw() { c.draws++; }, growKit() {}, saves: [], voiceToJSON: v => ({ name: v.name, x: v.x }),
    trackVoice: t => ({ name: 'T' + t, x: t >= c.polyFrom() ? 'poly' : 'mono' }), trackVoiceOf: (t, p) => ({ name: 'T' + t, x: p ? 'poly' : 'mono' }), saveS() { c.saves.push(JSON.stringify(c.S)); },
    toasts: [], briefs: [], asks: [], answer: true, toast(m) { c.toasts.push(m); }, toastBrief(k, m) { c.briefs.push(m); }, buildProj() {},
    askYes(msg, ok) { c.asks.push({ msg, ok }); return Promise.resolve(c.answer); } };
  vm.createContext(c);
  vm.runInContext(line('const ntr = () => S.ntr') + '\n' + line('const polyFrom = ') + '\n' + code + splitCode + '\nthis.polyFrom = polyFrom; this.ntr = ntr; this.setTracks = setTracks; this.setNtr = setNtr; this.tracksRowSet = tracksRowSet; this.tracksLimits = tracksLimits; this.tracksSay = tracksSay; this.trackHolds = trackHolds; this.tracksHiddenAll = tracksHiddenAll; this.ROWS = () => [' + rowsCode + '];', c);
  const S = { ntr: o.ntr || 16, song: [], smute: [], tfx: Array.from({ length: NT }, () => []), mfx: [], chainOn: new Array(NT).fill(false), chains: [], phrases: [], chainPatch: null, perf: null, parts: [],
    mix: Array.from({ length: NT }, () => c.freshMix()) };
  const empty = new Set(o.empty || []);
  for (let y = 0; y < 3; y++) S.song.push(new Array(NT + 1).fill(-1));
  for (let t = 0; t < NT; t++) { if (empty.has(t)) continue; S.song[0][t] = t; S.chains[t] = [[t]]; S.phrases[t] = [[40 + t, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1]]; S.mix[t].vol = 100 + t; }
  c.S = S; if (o.nmono) S.nmono = o.nmono;
  if (o.vtrk) { for (let t = 0; t < NT; t++) c.kit.voices[8 + t] = { name: 'T' + t, x: t >= c.polyFrom() ? 'poly' : 'mono' }; }
  return c;
}
const noteAt = (c, t) => { const cid = c.S.song[0][t]; return cid >= 0 ? c.S.phrases[c.S.chains[cid][0][0]][0][0] : -1; };
const row = (c, l) => { const r = c.ROWS().find(x => x.l === l); assert.ok(r, 'row ' + l); return r; };
const flush = () => new Promise(r => setImmediate(r));
const set = (c, m, p) => { const r = c.setTracks(m, p); assert.equal(r.why, '', 'refused: ' + r.why); };

test('the two rows replace TRACKS: labels, steps, value text, and they are in the Song group', () => {
  assert.ok(!src.includes("{l:'TRACKS'"), 'the TRACKS row is gone');
  assert.match(src, /\['Song', \['NAME', 'MONO TRACKS', 'POLY TRACKS', 'TEMPO', 'LPB'\]\]/);
  const c = rig(), m = row(c, 'MONO TRACKS'), p = row(c, 'POLY TRACKS');
  assert.equal(m.big, 4); assert.equal(p.big, 4); assert.equal(m.get(), 8); assert.equal(p.get(), 8); assert.equal(m.fmt(8), '8 mono'); assert.equal(p.fmt(8), '8 poly');
  assert.ok(!m.step && !p.step, 'step 1');
});
test('limits: each at least 1, 8 to 32 in all (24 with a voice per track); a row stops there and its info line says why', () => {
  let c = rig(), m = row(c, 'MONO TRACKS'), p = row(c, 'POLY TRACKS');
  assert.equal(m.min, 1); assert.equal(m.max, 24); assert.equal(p.min, 1); assert.equal(p.max, 24);
  assert.match(m.info(), /Total 16 of 32/); assert.match(m.info(), /tracks 1 to 8/); assert.match(p.info(), /tracks 9 to 16/); assert.match(p.info(), /four notes each/);
  c = rig({ ntr: 8, nmono: 3 }); m = row(c, 'MONO TRACKS'); p = row(c, 'POLY TRACKS');   // 3 + 5: the total is at its least
  assert.equal(m.min, 3); assert.equal(p.min, 5); assert.match(m.info(), /Total 8 of 32\. At the least: the song needs 8 tracks in all; raise POLY TRACKS first/);
  c = rig({ ntr: 24, nmono: 20, vtrk: true }); m = row(c, 'MONO TRACKS'); p = row(c, 'POLY TRACKS');   // 20 + 4 with a voice per track: full
  assert.equal(m.max, 20); assert.equal(p.max, 4); assert.match(m.info(), /Total 24 of 24\. At the most: the song has 24 tracks at the most \(a voice per track\); lower POLY TRACKS to make room/);
  c = rig({ ntr: 16, nmono: 15 }); p = row(c, 'POLY TRACKS'); assert.equal(p.min, 1); assert.match(p.info(), /At the least: one of each kind/);
  // stepping the way the screen does (clamp to min / max, then set): lands on the limit
  c = rig({ ntr: 24, nmono: 20, vtrk: true }); m = row(c, 'MONO TRACKS'); m.set(c.clamp(m.get() + 4, m.min, m.max)); assert.equal(c.S.ntr, 24); assert.equal(c.polyFrom(), 20); assert.equal(c.saves.length, 0, 'nothing changed');
  // a stale limit (another row changed it) is still refused with its reason
  c = rig({ ntr: 24, vtrk: true }); c.tracksRowSet(20, 8); assert.match(c.toasts.at(-1), /8 to 24 tracks in all/); assert.equal(c.S.ntr, 24);
});
test('shrinking a track that holds nothing: no question, one brief notice naming it, one undo step', async () => {
  const c = rig({ empty: [7] }); row(c, 'MONO TRACKS').set(7); await flush();
  assert.equal(c.asks.length, 0, 'no question'); assert.equal(c.S.nmono, 7); assert.equal(c.S.ntr, 15); assert.equal(c.saves.length, 1, 'one undo step');
  assert.equal(c.briefs.length, 1); assert.match(c.briefs[0], /^Hid track 08 \(mono\)\. Nothing is deleted: raising MONO TRACKS brings it back\.$/);
  assert.equal(noteAt(c, 7), 48, 'the poly tracks moved up under it');
  // several empty ones, both kinds in one go
  const d = rig({ empty: [6, 7, 14, 15] }); d.tracksRowSet(6, 6); assert.equal(d.asks.length, 0); assert.match(d.briefs[0], /^Hid tracks 07, 08 \(mono\) and 15, 16 \(poly\)\./); assert.match(d.briefs[0], /MONO TRACKS or POLY TRACKS/);
  // growing says nothing
  const e = rig(); e.tracksRowSet(9, 8); assert.equal(e.briefs.length, 0); assert.equal(e.S.ntr, 17);
});
test('shrinking a track that holds something asks first: Cancel leaves everything, Hide hides and keeps the notes, growing brings them back', async () => {
  const c = rig(), was = JSON.stringify(c.S); c.answer = false;
  row(c, 'POLY TRACKS').set(7); await flush();
  assert.equal(c.asks.length, 1); assert.equal(c.asks[0].msg.title, 'Hide track 16 (poly)?'); assert.equal(c.asks[0].ok, 'Hide');
  assert.match(c.asks[0].msg.body, /^It holds notes, effects or other settings\. Nothing is deleted: raising POLY TRACKS brings it back\.$/);
  assert.equal(JSON.stringify(c.S), was, 'Cancel changes nothing'); assert.equal(c.saves.length, 0); assert.equal(c.briefs.length, 0);
  c.answer = true; row(c, 'POLY TRACKS').set(7); await flush();
  assert.equal(c.asks.length, 2); assert.equal(c.S.ntr, 15); assert.equal(c.polyFrom(), 8); assert.equal(c.saves.length, 1, 'one undo step'); assert.equal(c.briefs.length, 1); assert.match(c.briefs[0], /^Hid track 16 \(poly\)\./);
  assert.equal(noteAt(c, 14), 54); c.tracksRowSet(8, 8); assert.equal(c.asks.length, 2, 'growing back asks nothing'); assert.equal(c.S.ntr, 16); assert.equal(noteAt(c, 15), 55, 'its notes are back');
  // one empty, one holding: both named in the question, only the one that holds is described as holding
  const d = rig({ empty: [14] }); d.tracksRowSet(8, 6); assert.equal(d.asks.length, 1); assert.equal(d.asks[0].msg.title, 'Hide tracks 15, 16 (poly)?'); assert.match(d.asks[0].msg.body, /^It holds notes, effects or other settings \(16 \(poly\)\)\./);
});
test('a growth the may-it-grow rule refuses shows its reason and leaves the counts alone; setNtr shows it too', () => {
  const spare = Array.from({ length: 16 }, (_, i) => 16 + i), c = rig({ empty: spare }); set(c, 2, 8); set(c, 2, 24); const was = JSON.stringify(c.S), n = c.saves.length;
  row(c, 'POLY TRACKS').set(25);
  assert.equal(c.toasts.length, 1); assert.match(c.toasts[0], /hidden mono tracks? \(.*\) still holds? something, so POLY cannot grow into (it|them)\. Clear (it|them) first, or lower MONO\./);
  assert.equal(JSON.stringify(c.S), was); assert.equal(c.saves.length, n); assert.equal(c.asks.length, 0);
  c.setNtr(27); assert.equal(c.toasts.length, 2, 'the total-only entry point says why too'); assert.equal(JSON.stringify(c.S), was);
  // every caller of setNtr / setTracks shows why
  for (const l of src.split('\n')) { if (!/\bset(Ntr|Tracks)\(/.test(l) || /^\s*(\/\/|\/\*|function |const |    const r = setTracks\(m1)/.test(l)) continue; if (/function setTracks|function setNtr/.test(l)) continue; assert.match(l, /why|tracksRowSet|\.tN|return r\.tN/, 'unreported caller: ' + l.slice(0, 160)); }
});
test('a track that takes an over slot gets the voice of its own kind; a voice somebody made is kept', () => {
  const kinds = c => c.kit.voices.slice(8, 8 + c.S.ntr).map(v => v.x[0]).join('');
  // MONO grows past its own hidden tracks into hidden POLY slots (they hold the poly stock voice): the tracks that take them are mono
  const a = rig({ vtrk: true, empty: [...Array(24).keys()].map(x => x + 8) }); set(a, 8, 2); for (let t = 10; t < 18; t++) a.kit.voices[8 + t] = { name: 'T' + t, x: 'mono' };   /* the 8 parked slots of the mono kind keep a mono voice */
  set(a, 20, 2); assert.equal(a.polyFrom(), 20); assert.equal(kinds(a), 'm'.repeat(20) + 'pp', 'every mono track has a mono voice, the two poly tracks a poly one');
  // and the other way round: POLY grows past its own hidden tracks into hidden MONO slots
  const b = rig({ vtrk: true, empty: [...Array(30).keys()].map(x => x + 2) }); set(b, 2, 8); set(b, 2, 22);
  assert.equal(b.S.ntr, 24); assert.equal(kinds(b), 'mm' + 'p'.repeat(22), 'every poly track has a poly voice');
  // a voice somebody made is kept: growth takes the plain slots first and the custom one stays hidden, still there
  const c = rig({ vtrk: true, empty: [8, 9, 10, 11, 12, 13, 14, 15] }); set(c, 8, 2); for (let t = 10; t < 18; t++) c.kit.voices[8 + t] = { name: 'T' + t, x: 'mono' }; const mine = { name: 'mine', x: 'custom' }; c.kit.voices[8 + 31] = mine;
  set(c, 20, 2); assert.ok(c.kit.voices.includes(mine), 'the custom voice is still in the kit'); assert.ok(c.kit.voices.indexOf(mine) - 8 >= c.S.ntr, 'and still hidden'); assert.equal(kinds(c), 'm'.repeat(20) + 'pp');
  // nothing left but hidden tracks with voices of their own: refused, nothing changes
  const d = rig({ vtrk: true, empty: [8, 9, 10, 11, 12, 13, 14, 15] }); set(d, 8, 2); for (let t = 18; t < 32; t++) d.kit.voices[8 + t] = { name: 'mine' + t, x: 'custom' }; const n = d.saves.length, was = JSON.stringify(d.S);
  assert.match(d.setTracks(20, 2).why, /still hold/, 'growing into hidden tracks whose voices are the user\'s is refused'); assert.equal(d.saves.length, n); assert.equal(JSON.stringify(d.S), was);
});
test('undo: the change is one saveS, whose snapshot holds both counts; the code redraws the Arrange boxes (arrSig reset) after a change', async () => {
  const c = rig({ empty: [6, 7] }); c.tracksRowSet(6, 8); assert.equal(c.saves.length, 1); const s = JSON.parse(c.saves[0]); assert.equal(s.nmono, 6); assert.equal(s.ntr, 14);
  assert.equal(c.arrSig, '', 'the Arrange header re-reads polyFrom'); assert.ok(c.draws >= 1);
  assert.match(src, /undo\.busy = true; const n = normSong\(JSON\.parse\(undo\.cur\)\)/);
});
test('the help text and README no longer call the second half poly', () => {
  assert.ok(!/The second half of the tracks is polyphonic/.test(src)); assert.ok(!/The second half of the tracks is polyphonic/.test(H.read('README.md')));
  assert.match(H.read('README.md'), /MONO TRACKS\*\* and \*\*POLY TRACKS/);
});
test('Hide on a song that was replaced while the question was open hides nothing and says so', async () => {
  const c = rig(); let resolve; c.askYes = (msg, ok) => { c.asks.push({ msg, ok }); return new Promise(r => { resolve = r; }); };
  c.tracksRowSet(8, 7); assert.equal(c.asks.length, 1); const other = JSON.parse(JSON.stringify(c.S)); c.S = other;   /* a project dropped on the window */
  resolve(true); await flush();
  assert.equal(c.S.ntr, 16); assert.equal(c.saves.length, 0); assert.equal(c.toasts.at(-1), 'The song changed: nothing was hidden.'); assert.equal(c.briefs.length, 0);
  const d = rig(); let res2; d.askYes = () => new Promise(r => { res2 = r; }); d.tracksRowSet(8, 7); res2(true); await flush(); assert.equal(d.S.ntr, 15, 'the same song: it hides');
});
test('the Song sheet keeps keyboard focus on the MONO / POLY control after it redraws', () => {
  const code = cut('let tsetFk = ', '// The Project tab of the settings menu'), made = [];
  const mk = fk => { const n = { dataset: { fk }, disabled: false, focused: 0, focus() { this.focused++; } }; made.push(n); return n; };
  const ctx = { buildProj() { ctx.rebuilt = (ctx.rebuilt || 0) + 1; }, document: { activeElement: null, querySelectorAll: () => made } };
  vm.createContext(ctx); vm.runInContext(code + '\nthis.keep = buildProjKeep; this.setFk = v => { tsetFk = v; };', ctx);
  const rg = mk('MONO TRACKS|range'), plus = mk('MONO TRACKS|+'), other = mk('POLY TRACKS|+');
  ctx.document.activeElement = plus; ctx.keep(); assert.equal(plus.focused, 1, 'the + that was pressed'); assert.equal(other.focused, 0);
  ctx.document.activeElement = null; ctx.setFk('MONO TRACKS|range'); ctx.keep(); assert.equal(rg.focused, 1, 'the slider (the dialog had focus when Hide was accepted)');
  plus.disabled = true; ctx.document.activeElement = plus; ctx.keep(); assert.equal(rg.focused, 2, 'a button that went disabled at a limit hands focus to its slider');
  assert.match(src, /dataset\.fk = r\.l \+ '\|range'/); assert.ok(!/r\.late\) buildProj\(\)|\|\| r\.late\) buildProj/.test(src), 'no late row rebuilds without keeping focus');
});
