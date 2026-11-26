#!/usr/bin/env node
// A7: usage census. Reads every .thunder under the folders (or files) given and counts which engines, layer types,
// effects, pump types, chains / waves modules and tracker commands the songs use.
//   node native/tools/census.mjs <folder|file>... [--md out.md] [--json out.json]
// The songs are DATA: they are parsed with JSON.parse and never executed. No dependencies.
// Field names: docs/port/ENGINE-INVENTORY.md section 8. Anything not understood is counted under "raw" so nothing is
// silently skipped.
import fs from 'node:fs';
import path from 'node:path';

// index.html line ~18016 (const FXC). A command's code in a phrase row is its INDEX in this list (-1 = none). The test
// checks this list against index.html so it cannot drift silently.
export const FXC = ['ARP','ARC','CHA','DEL','GRV','GGR','HOP','INS','KIL','OFF','NTH','RET','REP','RND','RNL','PSL','PBN','PVB','SED','TPO','TSP','VOL','PIT','FIN','PLY','STA','PAN','TIC','WOB','FXO','FLX','GAT','RTG',
  'CCA','CCB','CCC','CCD','PGM','BYP','CSL','DIR','MUT','GEN','PRB','FXS','FXV','SCA','SCG','CUT','RES','DEC','FDI','TRM','APN','TVL','TPN','STP','LPB','PDL','ONE','MPB','MAT','CND','NUD','RTV','SLC','XFD','MAC','LK1','LK2','LK3','LK4','PRT','END','JMP','FLY','ATK','REL','TCH','SKP','CHD','KN1','KN2','KN3','KN4','KN5','KN6','KN7','KN8','FLZ'];
const FX0 = 6, RW = 12;   // a phrase row: 4 notes, volume, instrument, then 3 (command, value) pairs

const CATS = ['engines', 'layerTypes', 'trackFx', 'masterFx', 'pumpTypes', 'chainsModules', 'wavesModules', 'commands', 'masteringStages', 'trackFxOn', 'masterFxOn', 'raw'];

export function newCounts() { const o = {}; for (const c of CATS) o[c] = {}; o.scalars = {}; return o; }
const inc = (o, k, n = 1) => { o[k] = (o[k] || 0) + n; };
const isObj = (x) => x && typeof x === 'object' && !Array.isArray(x);

function b64Samples(b64) { if (typeof b64 !== 'string' || !b64) return 0; const pad = b64.endsWith('==') ? 2 : b64.endsWith('=') ? 1 : 0; return Math.floor(((b64.length * 3) / 4 - pad) / 2); }   // int16 mono

export function censusSong(file) {
  const c = newCounts(), sc = c.scalars;
  const add = (k, n = 1) => inc(sc, k, n);
  const j = file;
  if (!isObj(j) || !isObj(j.song)) { inc(c.raw, 'not a thunder file (no song object)'); return c; }
  const S = j.song, kit = isObj(j.kit) ? j.kit : {};
  add('songs');
  inc(c.raw, 'file build ' + j.build);
  inc(c.raw, 'song v' + S.v);
  // --- kit voices: engines, layer types, samples
  const voices = Array.isArray(kit.voices) ? kit.voices : [];
  add('voices', voices.length);
  const smp = (s, where) => {
    if (!isObj(s)) return;
    add('samples'); inc(c.raw, 'sample in ' + where);
    if (typeof s.b64 === 'string') { const n = b64Samples(s.b64) + 0; add('sampleFrames', n); add('sampleSeconds', n / (s.sr || 44100)); if (s.b64r) add('stereoSamples'); }
    else { add('samplesWithoutAudio'); }   // a reference only (see A9)
  };
  for (const v of voices) {
    if (!isObj(v)) { inc(c.raw, 'voice not an object'); continue; }
    const eng = (v.engine == null || v.engine === 'layers') ? 'layers (layer stack)' : String(v.engine);
    if (/^p(mono|poly|coil|orbit|fold|gloss|tract|strike|boing|x0x)/.test(eng)) inc(c.pumpTypes, 'synth:' + eng);
    inc(c.engines, eng);
    if (/^sf/.test(eng)) add('soundfontVoices');
    const layered = v.engine == null || v.engine === 'layers';
    if (Array.isArray(v.layers)) for (const l of v.layers) {
      if (!isObj(l)) continue;
      if (l.sample) smp(l.sample, 'voice layer');
      if (layered && l.on !== false) inc(c.layerTypes, String(l.type));
      else if (l.on !== false && l.type && !layered) { /* layers of a whole-voice engine are not played */ }
    }
    if (Array.isArray(v.fx) && v.fx.length) for (const f of v.fx) if (isObj(f)) inc(c.trackFx, 'voice-fx:' + f.type);
    if (v.wtData) add('wtDataVoices');
    if (v.rsData) add('rsDataVoices');
    if (v.bbExpr) add('bbExprVoices');
  }
  if (isObj(S.presets)) { for (const l of S.presets.layers || []) smp(l && l.sample, 'preset'); }
  if (isObj(S.irs)) for (const k of Object.keys(S.irs)) smp(S.irs[k], 'impulse response');
  // --- effects
  const fxScan = (slots, cat, onKey) => {
    if (!Array.isArray(slots)) return;
    for (const sl of slots) {
      if (!isObj(sl)) continue;
      inc(c[cat], String(sl.type)); if (sl.on !== false) inc(c[cat + 'On'], String(sl.type));
      if (isObj(sl.pump)) inc(c.pumpTypes, String(sl.type));
    }
  };
  if (Array.isArray(S.tfx)) for (const t of S.tfx) fxScan(t, 'trackFx');
  fxScan(S.mfx, 'masterFx');
  if (isObj(S.mstPump)) for (const k of Object.keys(S.mstPump)) inc(c.pumpTypes, 'mstPump:' + k);
  if (isObj(S.mst)) for (const k of Object.keys(S.mst)) if (isObj(S.mst[k]) && S.mst[k].on) inc(c.masteringStages, k);
  if (S.mst && S.mst.on) inc(c.masteringStages, '(mastering block on)');
  // --- chains / waves patches
  const mods = (patch, cat) => { if (isObj(patch) && Array.isArray(patch.modules) && patch.modules.length) { add(cat === 'chainsModules' ? 'chainsPatches' : 'wavesPatches'); for (const m of patch.modules) inc(c[cat], String(m && m.type)); } };
  mods(S.chainPatch, 'chainsModules'); mods(kit.waves, 'wavesModules');
  if (S.chainOn) { const n = Array.isArray(S.chainOn) ? S.chainOn.filter(Boolean).length : 0; add('tracksWithChain', n); }
  // --- tracker commands (every phrase row; unused phrases are empty so they add nothing)
  let rows = 0, notes = 0;
  if (Array.isArray(S.phrases)) for (const ph of S.phrases) {
    if (!Array.isArray(ph)) continue;
    for (const r of ph) {
      if (!Array.isArray(r)) continue;
      let any = false;
      for (let k = 0; k < 4; k++) if (r[k] >= 0) { notes++; any = true; }
      for (let m = FX0; m < RW; m += 2) if (r[m] >= 0) { any = true; const name = FXC[r[m]]; inc(c.commands, name || 'unknown code ' + r[m]); }
      if (any) rows++;
    }
  }
  add('phraseRowsUsed', rows); add('notes', notes);
  add('tracks', S.ntr || 0);
  return c;
}

export function merge(a, b) { for (const k of Object.keys(b)) for (const [n, v] of Object.entries(b[k])) a[k][n] = (a[k][n] || 0) + v; return a; }

export function findSongs(args) {
  const out = [];
  const walk = (p) => { const st = fs.statSync(p); if (st.isDirectory()) { for (const e of fs.readdirSync(p).sort()) walk(path.join(p, e)); } else if (p.endsWith('.thunder')) out.push(p); };
  for (const a of args) walk(a);
  return out;
}

export function runCensus(files) {
  const total = newCounts(), per = [], songsUsing = {};
  for (const f of files) {
    let j; try { j = JSON.parse(fs.readFileSync(f, 'utf8')); } catch (e) { per.push({ file: path.basename(f), error: String(e.message) }); continue; }
    const c = censusSong(j); per.push({ file: path.basename(f), counts: c }); merge(total, c);
    for (const cat of CATS) for (const k of Object.keys(c[cat])) { (songsUsing[cat + '|' + k] ||= new Set()).add(path.basename(f)); }
  }
  return { total, per, songsUsing };
}

const table = (title, obj, songsUsing, cat, nSongs, on) => {
  const rows = Object.entries(obj).sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
  if (!rows.length) return `### ${title}\n\n(none)\n\n`;
  let s = `### ${title}\n\n| Name | Uses |${on ? ' Switched on |' : ''} Songs (of ${nSongs}) |\n|---|---:|${on ? '---:|' : ''}---:|\n`;
  for (const [k, v] of rows) s += `| ${k} | ${v} |${on ? ' ' + (on[k] || 0) + ' |' : ''} ${songsUsing[cat + '|' + k] ? songsUsing[cat + '|' + k].size : ''} |\n`;
  return s + '\n';
};

export function toMarkdown(r, label) {
  const n = r.per.filter((p) => p.counts).length;
  let md = `## Census totals (${n} songs: ${label})\n\n`;
  const sc = r.total.scalars;
  md += '| Scalar | Total |\n|---|---:|\n' + Object.entries(sc).map(([k, v]) => `| ${k} | ${Number.isInteger(v) ? v : v.toFixed(2)} |`).join('\n') + '\n\n';
  const T = [['Voice engines (kit.voices[].engine)', 'engines'], ['Layer oscillator types (layered voices, layers switched on)', 'layerTypes'], ['Track effects (S.tfx)', 'trackFx'], ['Master effects (S.mfx)', 'masterFx'], ['Pump types (slots carrying a pump object, and S.mstPump keys)', 'pumpTypes'], ['…chains modules (S.chainPatch)', 'chainsModules'], ['…waves modules (kit.waves)', 'wavesModules'], ['Tracker commands (FXC)', 'commands'], ['Mastering stages switched on (S.mst)', 'masteringStages'], ['Raw: things seen that need a look', 'raw']];
  for (const [t, c] of T) md += table(t, r.total[c], r.songsUsing, c, n, c === 'trackFx' ? r.total.trackFxOn : c === 'masterFx' ? r.total.masterFxOn : null);
  md += '### Per song\n\n| Song | Voices | Samples | Sample seconds | Phrase rows used | Commands used | Engines |\n|---|---:|---:|---:|---:|---:|---|\n';
  for (const p of r.per) {
    if (!p.counts) { md += `| ${p.file} | ERROR ${p.error} | | | | | |\n`; continue; }
    const s = p.counts.scalars;
    md += `| ${p.file} | ${s.voices || 0} | ${s.samples || 0} | ${(s.sampleSeconds || 0).toFixed(1)} | ${s.phraseRowsUsed || 0} | ${Object.values(p.counts.commands).reduce((a, b) => a + b, 0)} | ${Object.entries(p.counts.engines).sort((a, b) => b[1] - a[1]).map(([k, v]) => `${k} ${v}`).join(', ')} |\n`;
  }
  return md + '\n';
}

if (process.argv[1] && path.resolve(process.argv[1]) === path.resolve(new URL(import.meta.url).pathname)) {
  const a = process.argv.slice(2), paths = [];
  let md = null, js = null, label = '';
  for (let i = 0; i < a.length; i++) { if (a[i] === '--md') md = a[++i]; else if (a[i] === '--json') js = a[++i]; else if (a[i] === '--label') label = a[++i]; else paths.push(a[i]); }
  if (!paths.length) { console.error('usage: census.mjs <folder|file>... [--md out.md] [--json out.json] [--label text]'); process.exit(2); }
  const files = findSongs(paths);
  if (!files.length) { console.error('no .thunder files found'); process.exit(1); }
  const r = runCensus(files);
  const out = toMarkdown(r, label || files.map((f) => path.basename(f)).join(', '));
  if (md) fs.writeFileSync(md, out); else process.stdout.write(out);
  if (js) fs.writeFileSync(js, JSON.stringify({ total: r.total, per: r.per }, null, 1));
}
