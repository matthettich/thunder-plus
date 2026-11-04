// SWEEP: song files, presets and kits, autosave, rendering and the transport.
//   node tests/sweep/files.mjs
// Files: every bundled song (sounds/*.thunder) loads without errors and plays (sim, then live for a few seconds); save
// (projText, what File ▾ → Save writes) then load gives the same song; an old song (v1: FXC_V1 commands, one note column)
// and a v3 song load; File ▾ → Open project's file input; song presets export → import; kit save → load (the same
// kitToJSON); the autosave in localStorage restores the song after a reload.
// Rendering: Render song (WAV) and Render stems (.zip) through the app's renderOut with the file picker sending it to a
// download, render a phrase to a slot (renderPart), Render Cancel; checks the WAV length against the sequencer's song
// length, finite and not silent.
// Transport: play song / play phrase (Shift+Space) / stop, LOOP (pattern), LOOP SEL, tempo change while playing, FOLLOW,
// and Arrange paging with FOLLOW off (owner, 3 Nov: pages when the playhead passes the end; not while you work in Arrange;
// again after a minute without cursor moves).
import { open, ev, table, ROOT } from './seq-lib.mjs';
import fs from 'fs'; import path from 'path';

const rows = [];
const add = (n, ok, note) => { rows.push([n, ok ? 'PASS' : 'FAIL', note || '']); if (process.env.SWEEP_LOG) process.stderr.write(`${ok ? 'PASS' : 'FAIL'} ${n}\n`); };
let B = await open();
let { page, errors } = B;   // (reassigned when transport opens a fresh page)
const SONGS = fs.readdirSync(path.join(ROOT, 'sounds')).filter(f => f.endsWith('.thunder'));
const sleep = ms => new Promise(r => setTimeout(r, ms));
const inPage = (src, arg) => page.evaluate(([s, a]) => { const f = window.__seqTest.ev('(async (arg) => { ' + s + ' })'); return f(a); }, [src, arg]);

// WAV parse: {sr, ch, bits, frames, peak, finite}
function wav(buf) {
  const dv = new DataView(buf.buffer, buf.byteOffset, buf.byteLength); let p = 12, fmt = null, out = null;
  while (p + 8 <= buf.length) { const id = buf.toString('ascii', p, p + 4), n = dv.getUint32(p + 4, true);
    if (id === 'fmt ') fmt = { tag: dv.getUint16(p + 8, true), ch: dv.getUint16(p + 10, true), sr: dv.getUint32(p + 12, true), bits: dv.getUint16(p + 22, true) };
    if (id === 'data' && fmt) { const bps = fmt.bits / 8, frames = Math.floor(n / bps / fmt.ch); let peak = 0, finite = true, sum = 0;
      for (let i = 0; i < frames * fmt.ch; i++) { const o = p + 8 + i * bps; let v;
        if (fmt.tag === 3) v = dv.getFloat32(o, true); else if (fmt.bits === 16) v = dv.getInt16(o, true) / 32768; else if (fmt.bits === 24) { v = (buf[o] | buf[o + 1] << 8 | buf[o + 2] << 16); if (v & 0x800000) v -= 0x1000000; v /= 8388608; } else v = dv.getInt32(o, true) / 2147483648;
        if (!Number.isFinite(v)) finite = false; const a = Math.abs(v); if (a > peak) peak = a; sum += v * v; }
      out = { ...fmt, frames, secs: frames / fmt.sr, peak, rms: Math.sqrt(sum / Math.max(1, frames * fmt.ch)), finite }; break; }
    p += 8 + n + (n & 1); }
  return out;
}
// The app saves through showSaveFilePicker when it exists: make it fail (not AbortError) so it downloads instead.
const noPicker = () => ev(page, `(window.showSaveFilePicker = async () => { throw new Error('no picker in the test'); }, window.showOpenFilePicker = undefined, 1)`);

const ONLY = process.env.SWEEP_ONLY || '';
const want = k => !ONLY || ONLY.split(',').includes(k);
try {
  await noPicker();
  if (want('files')) {
  // ---------------- every bundled song ----------------
  for (const f of SONGS) {
    const t = fs.readFileSync(path.join(ROOT, 'sounds', f), 'utf8'); const e0 = errors.length;
    try {
      const info = await page.evaluate(([t, n]) => window.__seqTest.load(t, n), [t, f]);
      const r = await page.evaluate(() => window.__seqTest.run('song', 3000, null));
      const notes = r.reduce((n, x) => n + x.evs.filter(e => e.type === 'note').length, 0);
      add(`load + play ${f} (sim)`, notes > 0 && errors.length === e0, `${info.name} ${info.bpm} BPM: ${r.length} steps, ${notes} notes`);
      await ev(page, `(window.__seqTest.audio(), tStart('song', {row:0}), 1)`); await sleep(4000);
      const on = await ev(page, 'tp.on'); await ev(page, 'tStop(true)');
      add(`play ${f} live (4 s)`, on && errors.length === e0, 'playing: ' + on + (errors.length > e0 ? ' errors: ' + errors.slice(e0).join(' | ') : ''));
    } catch (e) { add(`load ${f}`, false, e.message.slice(0, 200)); }
  }
  // ---------------- save -> load roundtrip ----------------
  try {
    await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));
    const r = await inPage(`flushS(); await presetSmpFill(); const a = JSON.stringify(S), txt = projText(); await loadProjText(txt, 'rt.thunder'); const b = JSON.stringify(S); const txt2 = projText();
      const ka = JSON.parse(txt).kit, kb = JSON.parse(txt2).kit; return {same:a === b, len:[a.length, b.length], txtSame:JSON.stringify(JSON.parse(txt).song) === JSON.stringify(JSON.parse(txt2).song), kitSame:JSON.stringify(ka) === JSON.stringify(kb)};`);
    add('save -> load roundtrip (song)', r.same && r.txtSame, JSON.stringify(r));
    add('save -> load roundtrip (kit)', r.kitSame, 'kitToJSON equal after reload');
  } catch (e) { add('save -> load roundtrip', false, e.message.slice(0, 200)); }
  // the File ▾ → Save path: saveProj with no picker downloads the .thunder; load that file back
  try {
    // (with a file picker that fails, saveProj says "Couldn't save there" and stops; deliver() is its download path, as on a phone)
    const dl = page.waitForEvent('download', { timeout: 120000 });
    await inPage(`flushS(); await presetSmpFill(); window.showSaveFilePicker = undefined; await writeOut({dl:true}, projName(), projBlob()); return 1;`);
    const d = await dl; const p = await d.path(); const txt = fs.readFileSync(p, 'utf8');
    const r = await inPage(`const a = JSON.stringify(S); await loadProjText(arg, 'dl.thunder'); return {same:a === JSON.stringify(S), name:S.name};`, txt);
    add('File ▾ Save (download) -> load', r.same, `${d.suggestedFilename()} ${txt.length} bytes, identical after load: ${r.same}`);
    // the same file through the Open project file input (projIn)
    const e0 = errors.length;
    await inPage(`S.name = 'CHANGED'; saveS(); return 1;`);
    await page.setInputFiles('#projIn', p); await sleep(1500);
    const nm = await ev(page, 'S.name');
    add('File ▾ Open project (file input)', nm !== 'CHANGED' && errors.length === e0, 'song name after open: ' + nm);
  } catch (e) { add('File ▾ Save / Open', false, e.message.slice(0, 200)); }
  // ---------------- old formats ----------------
  try {
    const v1 = { app: 'thunder', v: 1, song: { v: 1, name: 'OLD1', bpm: 100, song: [[0, -1, -1, -1, -1, -1, -1, -1, -1]], chains: [[[0, 0]]], phrases: [[
      [48, 200, 0, 2, 4, -1, -1, -1, -1], [50, 128, 1, 9, 0x90, -1, -1, -1, -1], [52, -1, 0, 10, 0xFF, 6, 0x40, -1, -1], [200, -1, -1, -1, -1, -1, -1, -1, -1]]] } };
    const r = await inPage(`const ok = await loadProjText(JSON.stringify(arg), 'v1.thunder'); const F = FXC; const p = S.phrases[0];
      const run = window.__seqTest.run('song', 64, null); const notes = run.reduce((n, x) => n + x.evs.filter(e => e.type === 'note').length, 0);
      return {ok, name:S.name, row0:p[0].slice(0, 12), cmds:p.slice(0, 3).map(r => [F[r[6]], r[7], r[8] >= 0 ? F[r[8]] : '-']), notes};`, v1);
    add('old song v1 (FXC_V1, one note column)', r.ok && r.cmds[0][0] === 'KIL' && r.cmds[1][0] === 'TPO' && r.cmds[2][0] === 'HOP' && r.cmds[2][1] === 0 && r.cmds[2][2] === 'STA' && r.row0[4] === 200 && r.notes > 0,
      JSON.stringify(r.cmds) + ' vol ' + r.row0[4] + ' notes ' + r.notes + ' (HOP FF -> 00: "end the phrase")');
    const v3 = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')); v3.song.v = 3; delete v3.song.ni;
    const r3 = await inPage(`const ok = await loadProjText(JSON.stringify(arg), 'v3.thunder'); return {ok, ni:S.ni, n:window.__seqTest.run('song', 400, null).length};`, v3);
    add('old song v3 (16 instruments)', r3.ok && r3.n > 0, JSON.stringify(r3));
    const r2 = await inPage(`const ok = await loadProjText(JSON.stringify({app:'thunder-song', v:2, name:'BARE', bpm:90, song:[[0]], chains:[[[0,0]]], phrases:[[[48,-1,0,-1,-1,-1,-1,-1,-1]]]}), 'bare.thunder'); return {ok, name:S.name, bpm:S.bpm};`);
    add('song-only file (app thunder-song, no kit)', r2.ok && r2.bpm === 90, JSON.stringify(r2));
    const bad = await inPage(`const ok = await loadProjText('{"hello":1}', 'bad.thunder'); return ok;`);
    add('not a project file is refused', bad === false, 'returns ' + bad);
  } catch (e) { add('old formats', false, e.message.slice(0, 200)); }
  // ---------------- presets ----------------
  try {
    await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));
    await inPage(`const P = SP(); P.voices.push({name:'SWEEP PRESET', v:voiceToJSON(kit.voices[0])}); saveS(); return 1;`);
    const dl = page.waitForEvent('download', { timeout: 30000 }); await inPage('exportPresets(); return 1;'); const d = await dl; const p = await d.path();
    const n0 = await ev(page, 'SP().voices.length');
    await page.setInputFiles('#presetIn', p); await sleep(1500);
    const n1 = await ev(page, 'SP().voices.length'), names = await ev(page, 'SP().voices.map(v => v.name)');
    add('song presets export -> import', n1 === n0 + 1, `${n0} -> ${n1}: ${names.join(', ')}`);
  } catch (e) { add('song presets export -> import', false, e.message.slice(0, 200)); }
  // ---------------- kit save / load ----------------
  try {
    const dl = page.waitForEvent('download', { timeout: 30000 }); await ev(page, `($('bSaveKit').click(), 1)`); const d = await dl; const p = await d.path();
    const k0 = await ev(page, 'JSON.stringify(kitToJSON(false))');
    await inPage(`kit.voices[0].name = 'CHANGED PAD'; return 1;`);
    const pr = page.setInputFiles('#kitIn', p); await pr;
    for (let i = 0; i < 40 && !(await page.locator('.ask').count()); i++) await sleep(100);
    if (await page.locator('.ask').count()) await page.locator('.ask .btnrow .ok, .ask .askitem').first().click();
    await sleep(1000);
    const k1 = await ev(page, 'JSON.stringify(kitToJSON(false))');
    add('kit save -> load', k0 === k1, `${d.suggestedFilename()}: identical kit after load ${k0 === k1} (${k0.length} vs ${k1.length})`);
  } catch (e) { add('kit save -> load', false, e.message.slice(0, 200)); }
  // ---------------- autosave restore (reload) ----------------
  try {
    await inPage(`S.name = 'AUTOSAVE TEST'; S.bpm = 133; S.song[200][0] = S.song[0][0]; saveS(); flushS(); flushKit(); return 1;`);
    const j0 = await ev(page, 'JSON.stringify(S)');
    await page.reload(); await page.waitForFunction(() => !!window.__seqTest, null, { timeout: 60000 }); await sleep(2000);
    const j1 = await ev(page, 'JSON.stringify(S)');
    add('autosave restores the song after reload', j0 === j1, `name ${await ev(page, 'S.name')}, identical ${j0 === j1}`);
    await noPicker();
  } catch (e) { add('autosave restore', false, e.message.slice(0, 200)); }

  }
  if (want('render')) {
  // ---------------- rendering ----------------
  await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));
  // shorten ELECTRO to its first 4 song rows so a render takes seconds, not minutes, on a slow machine
  const songLen = await inPage(`for (let y = 4; y < 256; y++) S.song[y].fill(-1); S.renderFrom = 0; saveS(); const sq = makeSeq(S, 'song', {row:0, once:true}); while (sq.alive()) sq.step(); return sq.time - (sq.base || 0);`);
  try {
    const dl = page.waitForEvent('download', { timeout: 600000 }); const e0 = errors.length;
    await inPage(`renderOut(false); return 1;`); const d = await dl; const w = wav(fs.readFileSync(await d.path()));
    add('Render song (WAV)', w && w.finite && w.peak > 0.01 && w.secs >= songLen && w.secs < songLen + 10 && errors.length === e0,
      `${d.suggestedFilename()}: ${w.sr} Hz ${w.bits}-bit ${w.ch} ch, ${w.secs.toFixed(2)} s (song ${songLen.toFixed(2)} s + tail), peak ${w.peak.toFixed(3)}, rms ${w.rms.toFixed(4)}`);
  } catch (e) { add('Render song (WAV)', false, e.message.slice(0, 200)); }
  try {
    const dl = page.waitForEvent('download', { timeout: 900000 });
    await inPage(`renderOut(true); return 1;`); const d = await dl; const buf = fs.readFileSync(await d.path());
    // a stored zip: walk the local headers
    const files = []; let p = 0; while (buf.readUInt32LE(p) === 0x04034b50) { const comp = buf.readUInt16LE(p + 8), sz = buf.readUInt32LE(p + 18), nl = buf.readUInt16LE(p + 26), xl = buf.readUInt16LE(p + 28);
      const name = buf.toString('utf8', p + 30, p + 30 + nl), data = buf.subarray(p + 30 + nl + xl, p + 30 + nl + xl + sz); files.push({ name, comp, w: comp === 0 ? wav(data) : null }); p += 30 + nl + xl + sz; }
    const okAll = files.length > 1 && files.every(f => f.w && f.w.finite) && files.filter(f => f.w.peak > 0.001).length >= files.length - 1;
    const lens = new Set(files.map(f => f.w && f.w.frames));
    add('Render stems (.zip)', okAll && lens.size === 1, `${files.length} files: ${files.map(f => f.name.replace(/^ELECTRO /, '') + (f.w ? ' ' + f.w.peak.toFixed(2) : ' ?')).join(', ')}; equal length ${lens.size === 1}`);
  } catch (e) { add('Render stems (.zip)', false, e.message.slice(0, 200)); }
  try {
    const r = await page.evaluate(async () => { const t = window.__seqTest; const p = t.ev('renderOut')(false);
      for (let i = 0; i < 200 && !document.querySelector('.rprog'); i++) await new Promise(r => setTimeout(r, 25));
      const had = !!document.querySelector('.rprog'); const b = document.querySelector('.rprog .rpx'); if (b) b.click();
      let dl = false; const a0 = HTMLAnchorElement.prototype.click; HTMLAnchorElement.prototype.click = function(){ if (this.download) dl = true; return a0.apply(this, arguments); };
      try { await p; } finally { HTMLAnchorElement.prototype.click = a0; }
      return {had, gone:!document.querySelector('.rprog'), rendering:t.ev('rendering'), saved:dl, toast:(document.getElementById('toast') || {}).textContent}; });
    add('Render Cancel', r.had && r.gone && !r.rendering && !r.saved, 'dialog closed, nothing saved, not rendering: ' + JSON.stringify(r));
  } catch (e) { add('Render Cancel', false, e.message.slice(0, 200)); }
  try {
    const r = await page.evaluate(async () => { const t = window.__seqTest; t.ev(`setView('tracker'); cs.song.y = 0; cs.song.x = 0; cTrack = 0; cSect = S.song[0][0]; cPhrase = firstPhrase(cSect); go('phrase'); selA = null;`);
      const p = t.ev('renderPart')('pad'); for (let i = 0; i < 100 && !document.querySelector('.ask .askitem'); i++) await new Promise(r => setTimeout(r, 50));
      const lab = document.querySelector('.ask .askitem').textContent; document.querySelector('.ask .askitem').click(); await p;
      const m = lab.match(/: ([0-9A-F]{2}) ·/); const pad = m ? parseInt(m[1], 16) : -1; const v = t.ev('kit').voices[pad];
      const L = v && v.layers.find(L => L.on && L.sample && L.sample.data); const d = L ? L.sample.data : null; let pk = 0, fin = true; if (d) for (const x of d) { if (!Number.isFinite(x)) fin = false; pk = Math.max(pk, Math.abs(x)); }
      return {pad, name:v && v.name, len:d ? d.length / (L.sample.sr || 48000) : 0, pk, fin, toast:(document.getElementById('toast') || {}).textContent}; });
    const phraseSecs = await ev(page, `plen(cPhrase) * 60 / S.bpm / plpbOf(S, cPhrase)`);
    add('Render phrase to a slot', r.len >= phraseSecs * 0.98 && r.pk > 0.01 && r.fin, `slot ${r.pad} "${r.name}": ${r.len.toFixed(2)} s (phrase ${phraseSecs.toFixed(2)} s), peak ${r.pk.toFixed(2)} · ${r.toast}`);
  } catch (e) { add('Render phrase to a slot', false, e.message.slice(0, 200)); }
  try {
    const dl = page.waitForEvent('download', { timeout: 300000 });
    await inPage(`setView('tracker'); go('song'); cs.song.y = 0; selA = null; renderPart('wav'); return 1;`); const d = await dl; const w = wav(fs.readFileSync(await d.path()));
    const rowSecs = await ev(page, `(() => { const sq = makeSeq(S, 'song', {row:0, once:true, loop:null}); let n = 0; const y0 = S.song[0]; let t = 0; while (sq.alive() && n++ < 2000){ const r = sq.step(); if (r.pos && r.pos.some(p => p && p.row > 0)) { t = r.t; break; } } return t - (sq.base || 0); })()`);
    add('Render song row as WAV', w && w.finite && w.peak > 0.01 && w.secs >= rowSecs * 0.98, `${w.secs.toFixed(2)} s (row ${rowSecs.toFixed(2)} s), peak ${w.peak.toFixed(2)}`);
  } catch (e) { add('Render song row as WAV', false, e.message.slice(0, 200)); }

  }
  if (want('transport')) {
  // ---------------- transport ----------------
  // a fresh page (after the renders the old one could hang on the first key press on a busy machine)
  if (want('files') || want('render')) { const old = errors.slice(); await B.close(); B = await open(); ({ page, errors } = B); errors.unshift(...old); await noPicker(); }
  try {
    await page.evaluate(t => window.__seqTest.load(t, 'ELECTRO.thunder'), fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8'));
    await ev(page, `(setView('tracker'), go('song'), cs.song.y = 0, cs.song.x = 0, inMode = 'keys', 1)`); await page.mouse.click(700, 5).catch(() => {});
    if (process.env.SWEEP_LOG) process.stderr.write('  transport: Space\n');
    await page.keyboard.press('Space'); await sleep(3000);
    const s1 = await ev(page, 'window.__seqTest.state()'); const m1 = await ev(page, 'tp.mode');
    await page.keyboard.press('Space'); await sleep(800); const s2 = await ev(page, 'tp.on');
    add('Space plays / stops the song', s1.on && m1 === 'song' && !s2, `playing ${s1.on} (${m1}, row ${s1.row}), stopped ${!s2}`);
    await ev(page, `(cTrack = 0, cSect = S.song[0][0], cPhrase = firstPhrase(cSect), go('phrase'), 1)`);
    await page.keyboard.press('Shift+Space'); await sleep(2500); const m2 = await ev(page, 'tp.mode'), on2 = await ev(page, 'tp.on');
    await page.keyboard.press('F8'); await sleep(800); const off2 = !(await ev(page, 'tp.on'));
    add('Shift+Space plays the phrase, F8 stops', on2 && m2 === 'phrase' && off2, `mode ${m2}, stopped ${off2}`);
    // tempo change while playing
    await ev(page, `(go('song'), tStart('song', {row:0}), 1)`); await sleep(2000);
    await ev(page, `(CLK.bpm.set(150), 1)`); await sleep(1500);
    const tb = await ev(page, `({bpm:S.bpm, seq:tp.seq && tp.seq.bpm, on:tp.on, core:!!tp.core})`);
    await ev(page, 'tStop(true)');
    add('tempo change while playing', tb.on && tb.bpm === 150, JSON.stringify(tb));
    // LOOP pattern: song row stays on the cursor row
    await ev(page, `(go('song'), cs.song.y = 2, toggleLoopPat(), tStart('song', {row:2}), 1)`);
    const seen = new Set(); for (let i = 0; i < 25; i++) { const s = await ev(page, 'window.__seqTest.state()'); if (s.row != null) seen.add(s.row); await sleep(250); }
    await ev(page, '(tStop(true), toggleLoopPat(), 1)');
    add('LOOP (pattern) repeats the row', [...seen].length === 1 && seen.has(2), 'rows seen ' + [...seen].join(','));
    // LOOP SEL on song rows 4-5
    await ev(page, `(go('song'), selA = {x:0, y:4, v:'song'}, cs.song.y = 5, toggleLoop(), tStart('song', {row:4, loop:{v:'song', y0:4, y1:5}}), 1)`);
    const seen2 = new Set(); for (let i = 0; i < 30; i++) { const s = await ev(page, 'window.__seqTest.state()'); if (s.row != null) seen2.add(s.row); await sleep(250); }
    await ev(page, '(tStop(true), toggleLoop(), selA = null, 1)');
    add('LOOP SEL loops song rows 4-5', [...seen2].every(r => r === 4 || r === 5) && seen2.size >= 1, 'rows seen ' + [...seen2].join(','));
    // LPB change while playing: S.lpb changed, keeps playing
    await ev(page, `(tStart('song', {row:0}), 1)`); await sleep(1500); await ev(page, `(S.lpb = 4, saveS(), 1)`); await sleep(1500);
    const l = await ev(page, `({on:tp.on})`); await ev(page, 'tStop(true)');
    add('LPB change while playing', l.on, 'still playing ' + l.on);
    // FOLLOW on: song cursor follows the playhead
    // (owner, 31 Oct, EDIT WHILE FOLLOWING: the screen follows the playhead, the cursor stays on its row so you can edit)
    await ev(page, `(followMode = 2, go('song'), cs.song.y = 0, cs.song.top = 0, arrHandT = -1e9, arrActT = -1e9, tStart('song', {row:40}), 1)`); await sleep(4000);
    const f = await ev(page, 'window.__seqTest.state()'); await ev(page, 'tStop(true)');
    add('FOLLOW: the view follows, the cursor stays', f.y === 0 && f.row >= 40 && f.row >= f.top && f.row < f.top + f.rows, `cursor ${f.y}, playhead ${f.row}, page ${f.top}–${f.top + f.rows - 1}`);
  } catch (e) { add('transport', false, e.message.slice(0, 300)); }
  // Arrange paging with FOLLOW off: a short song with blocks far down, a page that ends before them
  try {
    const r = await page.evaluate(async () => { const t = window.__seqTest;
      t.ev(`followMode = 0; setView('tracker'); go('song'); for (let y = 0; y < 256; y++) if (y >= 64) S.song[y].fill(-1); S.bpm = 300; saveS(); cs.song.y = 0; cs.song.top = 0; selA = null; arrHandT = -1e9; arrActT = -1e9; draw();`);
      const rows = t.state().rows; t.ev(`tStart('song', {row:Math.max(0, ${0})})`); const tops = []; const t0 = performance.now();
      while (performance.now() - t0 < 25000) { const s = t.state(); tops.push([s.row, s.top]); if (s.row != null && s.row > rows + 2) break; await new Promise(r => setTimeout(r, 200)); }
      const last = t.state(); t.ev('tStop(true)'); return {rows, last, tops:tops.filter((x, i) => i % 5 === 0).slice(0, 30)}; });
    const paged = r.last.row != null && r.last.row >= r.rows ? r.last.top > 0 : null;
    add('Arrange pages with FOLLOW off', paged !== false, paged === null ? `playhead did not pass the page (${r.rows} rows) in time: ` + JSON.stringify(r.last) : `page top ${r.last.top} with playhead at row ${r.last.row} (page ${r.rows} rows)`);
    // while working in Arrange (a cursor move just now) it does not page
    const r2 = await page.evaluate(async () => { const t = window.__seqTest;
      t.ev(`followMode = 0; go('song'); cs.song.y = 0; cs.song.top = 0; selA = null; draw();`); const rows = t.state().rows; t.ev(`tStart('song', {row:${0}})`); const t0 = performance.now(); let s;
      while (performance.now() - t0 < 25000) { t.ev(`arrHandT = performance.now()`); s = t.state(); if (s.row != null && s.row > rows + 2) break; await new Promise(r => setTimeout(r, 200)); }
      t.ev('tStop(true)'); return {rows, s}; });
    // a minute after the last key / click / wheel it pages again: back-date the last action by 61 s
    const r3 = await page.evaluate(async () => { const t = window.__seqTest;
      t.ev(`followMode = 0; go('song'); cs.song.y = 0; cs.song.top = 0; selA = null; arrActT = performance.now(); arrHandT = -1e9; draw();`); const rows = t.state().rows; t.ev(`tStart('song', {row:0})`); const t0 = performance.now(); let s, early = null;
      while (performance.now() - t0 < 25000) { s = t.state(); if (s.row != null && s.row > rows + 1 && early == null) { early = s.top; t.ev('arrActT = performance.now() - 61000'); } if (early != null && s.top > 0) break; await new Promise(r => setTimeout(r, 200)); }
      t.ev('tStop(true)'); return {rows, early, s}; });
    add('Arrange pages again after a minute idle', r3.early === 0 && r3.s.top > 0, `top while working ${r3.early}, after 61 s idle ${r3.s.top} (playhead ${r3.s.row})`);
    add('Arrange does not page while you work', !(r2.s.row > r2.rows) || r2.s.top === 0, `page top ${r2.s.top} playhead ${r2.s.row} (hand moved just now)`);
  } catch (e) { add('Arrange paging', false, e.message.slice(0, 300)); }
  }
} finally {
  if (errors.length) add('no page errors', false, errors.slice(0, 6).join(' | ')); else add('no page errors', true, '');
  await B.close();
}
process.exit(table(rows, 'FILES, RENDERING, TRANSPORT (tests/sweep/files.mjs)') ? 1 : 0);
