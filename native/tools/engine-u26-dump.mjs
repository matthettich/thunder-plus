// U26: THE REFERENCE OF THE BASS AND VOICE ENGINES (rendered through renderBass / renderVox) AND OF THE LIVE SHELL'S RULE (exact, Node only): the real ENGINE block of index.html run in Node's vm, on
// (1) every factory voice of beta's tests/golden/engines.json whose engine is bass or vox (SHA-256 of the float32 bytes of renderVoice(v, 48000, null, hold), as beta fingerprints them),
// (2) direct cases: random parameters from the engine cards' ranges (BU, VU), every switch (shape, parts, wobble shape / division / delay, vowel, comb, reese, choir, rough, robot, ...), other sample rates, holds,
// (3) CORE SCRIPTS: BassCore / VoxCore driven directly (legato, glide, set() between notes, syllable changes, `same`, rests, odd and fractional block bounds, bpm / WOB changes, and the NaN case of tests/bass-core.test.js),
// (4) PARSE cases: VoxCore.parse of thousands of lyrics (batches), compared as syllable lists,
// (5) SEQUENCES: a note sequence (notes on / off / legato / retrigger, rate steps and ramps, bpm / wob, syllables) rendered by THE LIVE RULE (docs/port/U26-NOTES.md: events at their exact sample, control blocks of the core's size
//     anchored at the last note event, rate read at each control block's first sample): the reference of native/tests/test_live_u26.cpp, which must give the same bytes through LiveShell at every host block size.
//   node native/tools/engine-u26-dump.mjs --fixture <betaRoot>   (by hand, needs a checkout of `beta`) writes native/tests/golden/engine_u26_voices.json, engine_u26_voices_golden.inc (beta's sha256 and length of each, copied, never
//        made here) and u26_ranges.json (min / max / curve / default of every parameter: BU, VU).
//   node native/tools/engine-u26-dump.mjs [outDir]               the local Node's reference: <outDir>/ref.json (hashes), <outDir>/cases.json (the case definitions), with --samples <outDir>/u<n>.f32 and c<n>.f32.
//   node native/tools/engine-u26-dump.mjs --golden               writes native/tests/golden/engine_u26_golden.inc (the cases' hashes: sha256 of the real JS's render, Node 22 x86-64 Linux).
//   node native/tools/engine-u26-dump.mjs --check                exits 1 if the local Node's renders do not give the committed hashes (beta's for the voices, engine_u26_golden.inc for the cases) or vox_tables.inc is not current.
//   node native/tools/engine-u26-dump.mjs --page-live            prints how the page's own live nodes (BASS_PROC / VOX_PROC / XPROC, run with the real cores) differ from the live rule (for U26-NOTES.md; not a gate).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';
import { engineOf, tablesText } from './u26-tables.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '../..');
const SR = 48000;
const KINDS = ['bass', 'vox'];
const KSET = new Set(KINDS);
const GOLD = path.join(ROOT, 'native/tests/golden');
const shaOf = arrs => { const h = crypto.createHash('sha256'); for (const a of arrs) h.update(Buffer.from(a.buffer, a.byteOffset, a.byteLength)); return h.digest('hex'); };
const same = (a, b) => JSON.stringify(a) === JSON.stringify(b);

const defaultsOf = E => { const v = E.mkVoice('x', [], {}); delete v.layers; return JSON.parse(JSON.stringify(v)); };
function slim(v, D) {
  const out = { v: {}, fx: (v.fx || []).map(f => ({ ...f })) };
  for (const k of Object.keys(v)) if (k !== 'layers' && k !== 'fx' && k !== 'name' && !same(v[k], D[k])) out.v[k] = JSON.parse(JSON.stringify(v[k]));
  return out;
}
const build = (E, s, name) => { const v = E.mkVoice(name, [], JSON.parse(JSON.stringify(s.v))); v.fx = s.fx.map(f => ({ ...f })); return v; };

// BU / VU (the engine cards' rows) are outside the ENGINE block: cut out of index.html and evaluated with stubs for their formatters
function uiOf(root, name) {
  const html = fs.readFileSync(path.join(root, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
  const a = html.indexOf('const ' + name + ' = {'), b = html.indexOf('\n};', a); if (a < 0 || b < 0) throw new Error(name + ' not found');
  return vm.runInNewContext('const hz = v => v, ms = v => v, pct = v => v, st = v => v; ' + html.slice(a, b + 3) + '; ' + name, { Math });
}

function fixture(betaRoot) {
  const G = createRequire(import.meta.url)(path.join(betaRoot, 'tools/engine-golden.js'));
  const gold = JSON.parse(fs.readFileSync(path.join(betaRoot, 'tests/golden/engines.json'), 'utf8'));
  const E = engineOf(betaRoot), D = defaultsOf(E), voices = [], rows = [], per = {}, seen = new Set(), all = {};
  for (const it of G.listVoices()) {
    const w = gold.voices[it.key]; if (!w || w.error) continue;
    G.engine().setSeed(G.seedFor(it.key));
    const b = it.build(), v = b.v;
    if (!KSET.has(v.engine)) continue;
    if (seen.has(it.key)) throw new Error('fixture: a voice key twice: ' + it.key); seen.add(it.key);
    const s = slim(v, D);
    const r = E.renderVoice(build(E, s, 'x'), SR, null, b.hold);
    if (shaOf([r]) !== w.sha256 || r.length !== w.len) throw new Error('fixture: the slim voice does not reproduce the golden: ' + it.key);
    voices.push({ k: it.key, hold: b.hold, ...s }); rows.push(`  {${JSON.stringify(it.key)}, ${JSON.stringify(w.sha256)}, ${w.len}},`); per[v.engine] = (per[v.engine] || 0) + 1;
  }
  // completeness: the listing is walked a second time and every voice of these engines it names must be in the fixture
  for (const it of G.listVoices()) { const w = gold.voices[it.key]; if (!w || w.error) continue; G.engine().setSeed(G.seedFor(it.key)); const e = it.build().v.engine; if (KSET.has(e)) all[e] = (all[e] || 0) + 1; }
  if (!same(all, per)) throw new Error('fixture: incomplete ' + JSON.stringify(all) + ' vs ' + JSON.stringify(per));
  fs.writeFileSync(path.join(GOLD, 'engine_u26_voices.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-u26-dump.mjs --fixture from beta (' + gold.node + '): the factory voices whose engine is bass or vox. Only what differs from mkVoice defaults (and the engine\'s parameter object).', sampleRate: SR, voices }, null, 1) + '\n');
  fs.writeFileSync(path.join(GOLD, 'engine_u26_voices_golden.inc'), ['// GENERATED by native/tools/engine-u26-dump.mjs --fixture: tests/golden/engines.json of `beta` (made with Node ' + gold.node + ' on x86-64 Linux), the rows of the voices in engine_u26_voices.json, copied unchanged.',
    '// sha256 of the float32 bytes of renderVoice(v, 48000, null, hold), and its length in samples. The gate of test_engine_u26 on every platform.',
    'struct U26Golden { const char* key; const char* sha256; long len; };', 'static const U26Golden kU26Golden[] = {', ...rows, '};', 'static const int kU26GoldenN = ' + rows.length + ';', ''].join('\n'));
  const ranges = {}, UI = { bass: uiOf(betaRoot, 'BU'), vox: uiOf(betaRoot, 'VU') };
  for (const k of KINDS) {
    const d = k === 'bass' ? E.BassCore.defaults() : E.VoxCore.defaults(); ranges[k] = {};
    for (const p of Object.keys(d)) { if (p === 'tune' || p === 'drive' || p === 'level' || p === 'txt') continue; const r = UI[k][p]; ranges[k][p] = r ? { min: r.min, max: r.max, c: r.c, d: d[p] } : { d: d[p] }; }
  }
  fs.writeFileSync(path.join(GOLD, 'u26_ranges.json'), JSON.stringify({ _about: 'GENERATED by native/tools/engine-u26-dump.mjs --fixture from beta: every bass / vox parameter with the engine card\'s min / max / curve (BU, VU) and the default. The direct cases draw from it.', ranges }, null, 1) + '\n');
  console.log('wrote ' + rows.length + ' voices ' + JSON.stringify(per));
}

// ---- the direct cases: made from a fixed seed (the same cases every time) -------------------------------------------------------------------------------------------------------------------
const lcg = seed => { let s = seed >>> 0; return () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296; }; };
const TEXTS = ['hello world, I am a robot', 'funky shit', 'ba-na-na, la la la', 'a', '', '. . .', 'Thunder plus: seeds & waves!', "don't you know I'm gonna", '[HH EH - L OW] [K AE T]', 'strengths twelfths', 'the quick brown fox jumps over the lazy dog', 'rhythm psychology', 'aaa eee iii ooo uuu', 'hmm shh psst'];
function directCases(E) {
  const R = JSON.parse(fs.readFileSync(path.join(GOLD, 'u26_ranges.json'), 'utf8')).ranges, r = lcg(2626), cases = [];
  const pick = a => a[Math.floor(r() * a.length)], ur = (lo, hi) => lo + r() * (hi - lo), ui = (lo, hi) => Math.floor(ur(lo, hi + 1));
  const draw = q => { if (q.min === undefined) return q.d; const t = r(); if (q.c === 'log') return q.min * Math.pow(q.max / q.min, t); if (q.c === 'int') return Math.round(q.min + t * (q.max - q.min)); if (q.c === 'sq') return q.min + (q.max - q.min) * t * t; return q.min + t * (q.max - q.min); };
  const add = (name, kind, params, o = {}, sr = SR, hold = 0, bpm = 120) => cases.push({ name, kind, sr, hold, bpm, v: { engine: kind, [kind]: params, ...o }, fx: [] });
  const SRS = [48000, 44100, 32000, 96000, 88200, 22050, 48000];
  const PER = { bass: 40, vox: 40 };
  for (const k of KINDS) for (let i = 0; i < PER[k]; i++) {
    const params = {}; for (const p in R[k]) if (r() < 0.75) params[p] = draw(R[k][p]);
    if (k === 'vox' && r() < 0.7) params.txt = pick(TEXTS);
    const o = {}; if (r() < 0.5) o.level = ur(-12, 6); if (r() < 0.4) o.tune = ui(-12, 24); if (r() < 0.35) o.drive = r(); if (r() < 0.5) o.len = ur(0.15, 2.5);
    const sr = SRS[i % SRS.length], hold = k === 'vox' ? 0 : (i % 3 === 0 ? 0 : ur(0.1, 1.5));
    add(k + String(i).padStart(2, '0'), k, params, o, sr, hold, i % 4 === 0 ? ui(60, 200) : 120);
  }
  const e = (name, k, params, o, sr, hold, bpm) => add('edge_' + name, k, params, o, sr, hold, bpm);
  for (const k of KINDS) e(k + '_defaults', k, {}, {}, SR, k === 'bass' ? 1 : 0);
  // ---- bass: the source spectrum (shape x partials), odd / even, tilt, stretch, the filter, vowel / formant, comb, reese, sub, drive, level
  for (const s of [0, 0.25, 0.5, 0.51, 0.75, 1]) for (const parts of [1, 2, 13, 48, 64]) e('bass_shape' + s + '_p' + parts, 'bass', { shape: s, parts, tilt: s - 0.4, oddeven: (s - 0.5) * 1.6 }, {}, SR, 0.4);
  for (const t of [-1, -0.5, 0.5, 1]) for (const oe of [-1, -0.3, 0.3, 1]) e('bass_tilt' + t + '_oe' + oe, 'bass', { tilt: t, oddeven: oe, stretch: (t + 1) / 2, parts: 40 }, {}, SR, 0.3);
  for (const res of [0, 0.35, 0.8, 1]) for (const cut of [30, 120, 900, 5000, 18000]) e('bass_res' + res + '_cut' + cut, 'bass', { res, cut, fenv: res * 3, fdec: 80 + cut / 10 }, {}, SR, 0.4);
  for (const v of [0, 0.2, 0.5, 0.75, 1]) for (const f of [0, 0.5, 1]) e('bass_vowel' + v + '_form' + f, 'bass', { vowel: v, form: f, wVowel: f - 0.5, wShape: 1 }, {}, SR, 0.5);
  for (const c of [0, 0.4, 1]) for (const cf of [0, 0.5, 1]) e('bass_comb' + c + '_f' + cf, 'bass', { comb: c, combF: cf, wComb: c - 0.5, wShape: 4 }, {}, SR, 0.4);
  for (const d of [0, 0.4, 0.5, 0.6, 12, 30, 60]) e('bass_reese' + d, 'bass', { det: d, shape: 0.2, parts: 64, width: d / 60 }, {}, SR, 0.4);
  for (const w of [0, 0.5, 1]) e('bass_width' + w, 'bass', { width: w, parts: 30 }, {}, SR, 0.3);
  for (const so of [0, 1, 2]) for (const sub of [0, 0.5, 1]) e('bass_sub' + sub + '_o' + so, 'bass', { sub, subOct: so, oct: so - 1 }, {}, SR, 0.3);
  // the wobble: every shape (0 to 6: Sine to Steps, 6 is the default branch), every division (0 to 13, 13 clamps), the WOB sync, retrigger, start phase, smooth, delay / fade, depth to level
  for (let sh = 0; sh <= 6; sh++) for (const rt of [0, 1]) e('bass_wshape' + sh + '_rt' + rt, 'bass', { wShape: sh, wRetrig: rt, wPhase: rt ? 0.3 : 0, wCut: 3, wRate: 9 - sh, wSmooth: sh / 8 }, {}, SR, 0.8, 140);
  for (let dv = -1; dv <= 14; dv++) e('bass_wrate' + dv, 'bass', { wRate: dv, wCut: 4, wVowel: 0.5, wAmp: 0.4, wShape: dv % 6 }, {}, SR, 0.8, 90 + dv * 7);
  for (const sm of [0, 0.0005, 0.002, 0.15, 1]) e('bass_smooth' + sm, 'bass', { wSmooth: sm, wShape: 4, wCut: 3 }, {}, SR, 0.5);
  for (const [wd, wf] of [[0, 0], [300, 0], [0, 400], [200, 600], [0.4, 0.4], [4000, 4000]]) e('bass_wdelay' + wd + '_' + wf, 'bass', { wDelay: wd, wFade: wf, wCut: 4, wAmp: 0.5, wVowel: 0.5, wComb: 0.5 }, {}, SR, 1);
  for (const a of [0, 0.5, 1]) e('bass_wamp' + a, 'bass', { wAmp: a, wShape: 1, wRate: 8 }, {}, SR, 0.6, 160);
  for (const bpm of [10, 60, 175, 999]) e('bass_bpm' + bpm, 'bass', { wCut: 3, wRate: 7 }, {}, SR, 0.8, bpm);
  // the envelope: attack / decay / sustain / release stages (stage 3 when the sustain is reached), the filter envelope, key tracking
  for (const [atk, dec, sus, rel] of [[0.5, 1, 0, 1], [2, 300, 1, 80], [400, 50, 0.3, 900], [2000, 4000, 0.5, 4000], [1, 30, 0.7, 20], [0, 0, 3, 0]]) e('bass_env_' + atk + '_' + dec + '_' + sus + '_' + rel, 'bass', { atk, dec, sus, rel }, {}, SR, 0.5);
  for (const fe of [-6, -2, 0, 2, 6]) for (const key of [0, 0.5, 1]) e('bass_fenv' + fe + '_key' + key, 'bass', { fenv: fe, key, fdec: 400 }, { tune: key * 12 }, SR, 0.4);
  for (const g of [0, 0.4, 0.6, 400]) e('bass_glide' + g, 'bass', { glide: g, legato: g > 0.5 ? 1 : 0 }, {}, SR, 0.4);
  for (const dr of [0, 0.0005, 0.3, 1]) for (const vd of [0, 0.5, 2]) e('bass_drive' + dr + '_v' + vd, 'bass', { sub: 0.8 }, { drive: vd + dr }, SR, 0.3);
  for (const level of [-30, -6, 0, 6, 12]) e('bass_level' + level, 'bass', {}, { level }, SR, 0.3);
  for (const tune of [-24, -12, 0, 12, 24, 36, 48]) for (const oct of [-3, 0, 2]) e('bass_pitch' + tune + '_o' + oct, 'bass', { oct, parts: 64, det: tune > 12 ? 30 : 0 }, { tune }, SR, 0.3);
  e('bass_clamp', 'bass', { oct: 9, shape: 9, parts: 999, tilt: 9, oddeven: 9, stretch: 9, cut: 1e6, res: 9, fenv: 99, fdec: 0, key: 9, vowel: 9, form: 9, combF: 9, comb: 9, width: 9, det: 999, sub: 9, subOct: 99, wRate: 99, wShape: 99, atk: 0, dec: 0, sus: 9, rel: 0, glide: 1e5 }, {}, SR, 0.3);
  e('bass_neg', 'bass', { oct: -9, shape: -9, parts: -5, tilt: -9, oddeven: -9, stretch: -9, cut: -5, res: -9, fenv: -99, fdec: -5, key: -9, vowel: -9, form: -9, combF: -9, comb: -9, width: -9, det: -9, sub: -9, subOct: -99, wRate: -99, wShape: -99, atk: -1, dec: -1, sus: -9, rel: -1 }, { tune: -9 }, SR, 0.3);
  for (const sr of [8000, 11025, 22050, 32000, 44100, 88200, 96000, 192000]) e('bass_sr' + sr, 'bass', { parts: 64, det: 20, shape: 0.3, wCut: 2 }, { tune: 12 }, sr, 0.3);
  e('bass_len', 'bass', {}, { len: 0.2 }, SR, 0); e('bass_len_big', 'bass', {}, { len: 9 }, SR, 0); e('bass_hold_big', 'bass', { rel: 900 }, {}, 4000, 40); e('bass_hold_tiny', 'bass', {}, {}, SR, 0.001); e('bass_len_zero', 'bass', {}, { len: 0 }, SR, 0);
  e('bass_frac_sr', 'bass', { rel: 50 }, {}, 4000.3, 40); e('bass_frac_sr2', 'bass', {}, {}, 11025.5, 0.5); e('bass_sr100', 'bass', { oct: -3 }, {}, 100, 2);
  // ---- vox: the lyrics (every text), every switch of the voice
  TEXTS.forEach((t, i) => e('vox_text' + i, 'vox', { txt: t }, {}, SR, 0));
  for (const ch of [1, 2, 3, 5, 8, 9, 0, 0.4]) for (const sp of [0, 0.4, 1]) e('vox_choir' + ch + '_sp' + sp, 'vox', { choir: ch, spread: sp, txt: 'la la' }, {}, SR, 0);
  for (const ru of [0, 0.2, 0.31, 0.6, 1]) e('vox_rough' + ru, 'vox', { rough: ru, txt: 'oh ah' }, {}, SR, 0);
  for (const rb of [0, 0.3, 1]) for (const vc of [0, 0.5, 1]) e('vox_robot' + rb + '_voice' + vc, 'vox', { robot: rb, voice: vc, txt: 'hey you' }, {}, SR, 0);
  for (const fm of [-1, -0.3, 0, 0.5, 1]) for (const br of [0, 0.55, 1]) e('vox_form' + fm + '_bright' + br, 'vox', { form: fm, bright: br, txt: 'me we' }, {}, SR, 0);
  for (const cons of [0.3, 0.5, 1, 2, 5]) e('vox_cons' + cons, 'vox', { cons, txt: 'strengths ba-na-na' }, {}, SR, 0);
  for (const [atk, rel] of [[0.5, 1], [12, 140], [400, 3000], [0, 0]]) e('vox_env_' + atk + '_' + rel, 'vox', { atk, rel, txt: 'oh no' }, {}, SR, 0);
  for (const sl of [80, 320, 1200, 5, 0]) e('vox_sylLen' + sl, 'vox', { sylLen: sl, txt: 'one two three' }, {}, SR, 0);
  for (const [vb, vr, vd] of [[0, 5, 0], [1, 5.5, 250], [1, 9, 1500], [0.5, 2, 0]]) e('vox_vib' + vb + '_' + vr + '_' + vd, 'vox', { vib: vb, vibRate: vr, vibDelay: vd, txt: 'aah' }, {}, SR, 0);
  for (const g of [0, 0.4, 0.6, 40, 600]) e('vox_glide' + g, 'vox', { glide: g, txt: 'la la la' }, {}, SR, 0);
  for (const [cl, hs, bw, bth] of [[0, 0, 0, 0], [1, 1, 1, 1], [0.6, 0.5, 0.5, 0.3], [0.2, 2, 0.1, 0.9]]) e('vox_tone_' + cl + '_' + hs + '_' + bw + '_' + bth, 'vox', { clar: cl, hiss: hs, bw, breath: bth, txt: 'sh s th f' }, {}, SR, 0);
  for (const dr of [0, 0.3, 1]) for (const lv of [-20, 0, 6]) e('vox_drive' + dr + '_level' + lv, 'vox', { txt: 'go' }, { drive: dr, level: lv }, SR, 0);
  for (const tune of [-24, -7, 0, 7, 24]) for (const oct of [-2, 0, 1]) e('vox_pitch' + tune + '_o' + oct, 'vox', { oct, txt: 'la' }, { tune }, SR, 0);
  e('vox_clamp', 'vox', { oct: 3, form: 3, robot: 3, breath: 3, hiss: 3, clar: 1.2, bright: 3, vib: 3, vibRate: 20, vibDelay: 5000, glide: 5000, atk: 0, rel: 0, cons: 10, bw: 1.5, voice: 3, sylLen: 1200, rough: 3, choir: 20, spread: 3, txt: 'la na' }, {}, SR, 0);
  e('vox_neg', 'vox', { oct: -1, form: -1, robot: -0.5, breath: -0.5, hiss: -1, clar: -0.5, bright: -1, vib: -1, vibRate: -1, vibDelay: -1, glide: -1, atk: -1, rel: -1, cons: -1, bw: -0.5, voice: -1, sylLen: -1, rough: -1, choir: -1, spread: -1, txt: 'la na' }, { tune: -9 }, SR, 0);
  for (const sr of [8000, 11025, 22050, 32000, 44100, 88200, 96000]) e('vox_sr' + sr, 'vox', { txt: 'hello world', choir: 3, rough: 0.4 }, {}, sr, 0);
  e('vox_frac_sr', 'vox', { txt: 'hi' }, {}, 4000.3, 0); e('vox_frac_sr2', 'vox', { txt: 'hi there' }, {}, 11025.5, 0);
  e('vox_long_text', 'vox', { txt: TEXTS.slice(0, 9).join(' ') + ' ' + TEXTS[7], sylLen: 80 }, {}, 22050, 0);   // more than 30 s of syllables: the render stops at the cap
  e('vox_text_4000', 'vox', { txt: 'la '.repeat(1400) }, {}, 8000, 0);   // sliced at 4000 characters by normVox
  e('vox_text_not_string', 'vox', { txt: 12 }, {}, SR, 0);
  return cases;
}

// ---- the text -> syllables cases (VoxCore.parse) ---------------------------------------------------------------------------------------------------------------------------------------------------
const fmtSyl = syl => syl.map(s => s.rest ? 'R' : s.on.join(' ') + '|' + s.nu + '|' + s.co.join(' ')).join(';');
function parseOne(E, t) { try { return fmtSyl(E.VoxCore.parse(t)); } catch (e) { return 'THROW'; } }
function parseTexts(E) {
  const r = lcg(777), T = [], D = Object.keys(E.VoxCore.dict());
  const pick = a => a[Math.floor(r() * a.length)];
  T.push(...D);
  for (const w of D) for (const suf of ['s', 'ing', 'ed', 'er', 'ly', 'e', "'s", 'es', 'y']) T.push(w + suf);
  const V = 'aeiouy', C = 'bcdfghjklmnpqrstvwxz', ALL = 'abcdefghijklmnopqrstuvwxyz';
  for (let i = 0; i < 3500; i++) { const n = 1 + Math.floor(r() * 10); let w = ''; for (let k = 0; k < n; k++) w += r() < 0.4 ? pick([...V]) : r() < 0.85 ? pick([...C]) : pick([...ALL]); if (r() < 0.05) w = "'" + w + "'"; if (r() < 0.04) w += "'" + pick([...C]); T.push(w); }
  const SEPS = [' ', ', ', '-', '. ', '  ', '_', '\n', ' - ', '; ', '!', '?', '\t', ':'];
  for (let i = 0; i < 700; i++) { const n = 2 + Math.floor(r() * 5), parts = []; for (let k = 0; k < n; k++) parts.push(r() < 0.5 ? pick(D) : (r() < 0.5 ? pick(D) + pick(['s', 'ing', 'ed']) : (() => { let w = ''; for (let q = 0; q < 2 + Math.floor(r() * 5); q++) w += pick([...ALL]); return w; })())); let t = parts[0]; for (let k = 1; k < n; k++) t += pick(SEPS) + parts[k]; T.push(r() < 0.2 ? t.toUpperCase() : t); }
  const PHs = Object.keys(E.VoxCore.PH);
  for (let i = 0; i < 500; i++) { const n = 1 + Math.floor(r() * 8), parts = []; for (let k = 0; k < n; k++) parts.push(r() < 0.1 ? '-' : r() < 0.08 ? pick(['XX', 'aa1', 'ey2', 'q', '', '12']) : pick(PHs) + (r() < 0.2 ? String(Math.floor(r() * 3)) : '')); T.push('[' + parts.join(pick([' ', ',', ', ', '  ', '\t'])) + ']' + (r() < 0.3 ? ' ' + pick(D) : '')); }
  T.push('', ' ', '[]', '[ ]', '[', 'a[', 'a[b', '[AA]]', '[[AA]', '[AA] [IY]', '[AA', ']', '-', '--', 'a-', '-a', 'a--b', "a-'b", "'", "''", "'a'", "a''b", '...', '._,', '123', 'a1b2', 'x-y-z', 'hel-lo', 'ab-', 'zzz', '[HH EH - L OW]', '[hh eh - l ow]', 'one two. three, four', 'ab  cd', 'A', 'AEIOU', 'bcdfg', 'hmm', 'shh', 'nth', 'pst');
  // the odd characters: String.prototype.toLowerCase gives ASCII from U+0130 and U+212A; toUpperCase from the others; /\s/ matches the Unicode spaces
  T.push('İstanbul', 'Kelvin', 'KK', '[ıy]', '[ıh]', '[ſh]', '[ß]', '[sß]', '[ﬁ]', '[ﬀ EH]', '[A A]', '[AA IY]', '[AA​IY]', '[AA\u0085IY]', '[AA　IY]', '[AA﻿IY]', '[AA IY]',
    'café naïve', 'zürich', 'ß', 'straße', '😀 smile', 'a😀b', '\uD83D', 'a\uD800b', '[İY]', '[K]', '[KAA]', '[ȦA]', 'i̇');
  // the dictionary's one hazard: D is a plain object, so the word "constructor" finds Object (and `...Object` throws)
  T.push('constructor', 'Constructor', 'my constructor', 'constructor-x', 'x-constructor', "'constructor'", 'constructors', 'toString', 'valueOf', 'hasOwnProperty', '__proto__', 'prototype', 'isPrototypeOf', '[constructor]');
  for (let i = 0; i < 40; i++) T.push(pick(D) + ' ' + pick(['constructor', 'x']) + ' ' + pick(D));
  return T;
}
function parseCases(E) {
  const T = parseTexts(E), out = [], B = 50;
  for (let i = 0; i < T.length; i += B) out.push({ name: 'parse' + String(i / B).padStart(3, '0'), parse: true, texts: T.slice(i, i + B) });
  return out;
}

// ---- core scripts -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
function coreScripts(E) {
  const out = [], add = (name, kind, sr, len, p, ops) => out.push({ name, script: true, kind, sr, len, p, ops });
  const on = (leg, extra = {}) => ({ o: 'on', leg, ...extra }), off = { o: 'off' }, rr = (a, b, rate = 1) => ({ o: 'r', a, b, rate });
  // ---- bass
  add('bass_leg', 'bass', SR, 16000, {}, [on(false), rr(0, 4000), on(true), rr(4000, 9000), off, rr(9000, 16000)]);
  add('bass_legato_param', 'bass', SR, 16000, { legato: 1 }, [on(false), rr(0, 4000), on(false), rr(4000, 9000), off, rr(9000, 12000), on(false), rr(12000, 16000)]);
  add('bass_nolegato_retrig', 'bass', SR, 12000, {}, [on(false), rr(0, 4000), on(false), rr(4000, 8000), off, rr(8000, 12000)]);
  add('bass_legato_in_release', 'bass', SR, 12000, {}, [on(false), rr(0, 3000), off, rr(3000, 3500), on(true), rr(3500, 8000), off, rr(8000, 12000)]);
  for (const g of [0.5, 0.6, 30, 250, 1000]) add('bass_glide' + g, 'bass', SR, 14000, { glide: g }, [on(false), rr(0, 3000), on(true), rr(3000, 6000, 1.5), rr(6000, 9000, 0.75), on(false), rr(9000, 11000, 2), off, rr(11000, 14000, 2)]);
  add('bass_glide_first_note_low', 'bass', SR, 9000, { glide: 200, oct: -3 }, [on(false), rr(0, 3000, 0), on(true), rr(3000, 9000, 4)]);   // the first pitch is below 1 Hz (lf <= 0)
  add('bass_wretrig0', 'bass', SR, 12000, { wRetrig: 0, wCut: 3, wShape: 5, wRate: 9 }, [on(false), rr(0, 3000), on(false), rr(3000, 6000), off, rr(6000, 8000), on(false), rr(8000, 12000)]);
  add('bass_wretrig_phase', 'bass', SR, 12000, { wRetrig: 1, wPhase: 0.6, wCut: 3, wShape: 5, wRate: 9, wDelay: 100, wFade: 300 }, [on(false), rr(0, 3000), on(true), rr(3000, 6000), on(false), rr(6000, 12000)]);
  add('bass_set_mid', 'bass', SR, 14000, {}, [on(false), rr(0, 3000), { o: 'set', p: { cut: 3000, det: 30, parts: 64, shape: 0.6, wCut: 2 } }, rr(3000, 7000), { o: 'set', p: { parts: 5, det: 0 } }, rr(7000, 9000), off, rr(9000, 14000)]);
  add('bass_bpm_wob', 'bass', SR, 24000, { wCut: 3, wVowel: 0.4, wShape: 1 }, [{ o: 'bpm', v: 60 }, on(false), rr(0, 4000), { o: 'bpm', v: 200 }, rr(4000, 8000), { o: 'bpm', v: 999 }, rr(8000, 12000), { o: 'wob', v: 0.25 }, rr(12000, 16000), { o: 'wob', v: 3 }, rr(16000, 20000), { o: 'wob', v: 0 }, off, rr(20000, 24000)]);
  add('bass_blocks_odd', 'bass', SR, 3000, {}, [on(false), ...[7, 33, 100, 1, 31, 64, 5, 90, 200, 481, 33].reduce((a, n) => { const s = a.pos; a.ops.push(rr(s, s + n, 1 + n / 200)); a.pos += n; return a; }, { pos: 0, ops: [] }).ops, off, rr(1100, 3000)]);
  add('bass_frac_bounds', 'bass', SR, 400, {}, [on(false), rr(0, 70.5), rr(70.5, 140), rr(20.25, 90, 1.5), off, rr(140, 299.5)]);
  add('bass_edges', 'bass', SR, 700, {}, [rr(0, 64), on(true), rr(64, 200), off, off, rr(200, 260), on(true), rr(260, 600, 2)]);
  add('bass_out_of_range', 'bass', SR, 300, {}, [on(false), rr(0, 500), off, rr(250, 700), rr(-40, 20), rr(1e9, 1e9 + 50)]);   // writes outside the array are ignored
  add('bass_release_end', 'bass', SR, 12000, { rel: 20 }, [on(false), rr(0, 3000), off, rr(3000, 12000)]);   // the release runs to the end (env < 1e-5: the stage becomes 0 and the blocks write zeros)
  add('bass_release_end_again', 'bass', SR, 14000, { rel: 20, dec: 30, sus: 0.5 }, [on(false), rr(0, 3000), off, rr(3000, 9000), on(true), rr(9000, 10000), off, rr(10000, 14000)]);
  add('bass_stage3', 'bass', SR, 16000, { dec: 40, sus: 0.4 }, [on(false), rr(0, 9000), off, rr(9000, 16000)]);   // the sustain is reached (stage 3)
  add('bass_nospan', 'bass', SR, 100, {}, [on(false), rr(10, 10), rr(50, 20), rr(0, 1), off, rr(1, 2)]);   // empty and reversed ranges
  add('bass_rate_extremes', 'bass', SR, 12000, { parts: 64, det: 30 }, [on(false), rr(0, 2000, 0), rr(2000, 4000, 256), rr(4000, 6000, 0.0001), rr(6000, 8000, 1000), rr(8000, 10000, 1), off, rr(10000, 12000, 1)]);
  add('bass_hi_rate_jump', 'bass', SR, 20000, { parts: 64, det: 0 }, [on(false), rr(0, 3000, 1), off, on(false), rr(3000, 3128, 16), rr(3128, 4000, 16), rr(4000, 9000, 1)]);
  add('bass_nan_gate', 'bass', SR, 0, {}, [{ o: 'nan1', det: 0, trials: 60 }, { o: 'nan1', det: 30, trials: 60 }, { o: 'nan2', det: 0 }, { o: 'nan2', det: 30 }]);
  for (const m of [0, 1]) add('bass_sr_low' + m, 'bass', 8000, 6000, { parts: 64, det: m ? 30 : 0, oct: 2 }, [on(false), rr(0, 2000, 1), rr(2000, 3000, 8), on(true), rr(3000, 6000, 12)]);
  // ---- vox
  const VS = { txt: 'hello world, I am a robot' };
  add('vox_seq', 'vox', SR, 24000, VS, [on(false, { n: 0, same: false }), rr(0, 3000), off, rr(3000, 4000), on(true, { n: 1, same: false }), rr(4000, 8000), off, rr(8000, 12000), on(false, { n: 2, same: false }), rr(12000, 16000), off, rr(16000, 24000)]);
  add('vox_same', 'vox', SR, 16000, VS, [on(false, { n: 0, same: false }), rr(0, 5000), on(true, { n: 1, same: true }), rr(5000, 9000), on(true, { n: 2, same: false }), rr(9000, 12000), off, rr(12000, 16000)]);
  add('vox_same_early', 'vox', SR, 12000, VS, [on(false, { n: 0, same: false }), rr(0, 40), on(true, { n: 3, same: true }), rr(40, 6000), off, rr(6000, 12000)]);   // `same` before the vowel is held: the next syllable starts
  add('vox_wrap', 'vox', SR, 20000, { txt: 'one two' }, [...[0, 1, 2, 3, 7, -1, -4, 17, 1e6, 2 ** 40, 0.0, -0.0].flatMap((n, i) => [on(i > 0, { n, same: false }), rr(i * 1600, i * 1600 + 1000), off, rr(i * 1600 + 1000, i * 1600 + 1600)])]);
  add('vox_rest', 'vox', SR, 14000, { txt: 'a, b . c' }, [...[0, 1, 2, 3, 4, 5].flatMap((n, i) => [on(false, { n, same: false }), rr(i * 2200, i * 2200 + 1200), off, rr(i * 2200 + 1200, i * 2200 + 2200)])]);
  add('vox_no_syllables', 'vox', SR, 4000, { txt: '' }, [on(false, { n: 0, same: false }), rr(0, 2000), off, rr(2000, 4000)]);
  add('vox_all_rests', 'vox', SR, 4000, { txt: '. , ;' }, [on(false, { n: 1, same: false }), rr(0, 2000), off, rr(2000, 4000)]);
  add('vox_off_first', 'vox', SR, 3000, VS, [off, rr(0, 500), on(false, { n: 0, same: false }), rr(500, 1500), off, off, rr(1500, 3000)]);
  add('vox_tight', 'vox', SR, 8000, VS, [on(false, { n: 0, same: false }), rr(0, 50), off, rr(50, 60), on(false, { n: 1, same: false }), rr(60, 70), off, rr(70, 4000), on(false, { n: 2, same: false }), rr(4000, 8000)]);
  add('vox_glide_rate', 'vox', SR, 16000, { glide: 120, txt: 'la la la' }, [on(false, { n: 0, same: false }), rr(0, 3000, 1), on(true, { n: 1, same: true }), rr(3000, 6000, 1.5), rr(6000, 9000, 0.7), on(true, { n: 2, same: false }), rr(9000, 12000, 2), off, rr(12000, 16000, 2)]);
  add('vox_set_text', 'vox', SR, 20000, VS, [on(false, { n: 0, same: false }), rr(0, 4000), { o: 'set', p: { txt: 'new words here' } }, off, rr(4000, 9000), on(false, { n: 1, same: false }), rr(9000, 13000), { o: 'set', p: { choir: 4, rough: 0.5, robot: 0.1 } }, rr(13000, 15000), off, rr(15000, 20000)]);   // the playing syllable outlives a new lyric
  add('vox_set_text_during_coda', 'vox', SR, 12000, { txt: 'strengths ba-na-na' }, [on(false, { n: 0, same: false }), rr(0, 3000), off, rr(3000, 3200), { o: 'set', p: { txt: 'x' } }, rr(3200, 9000), on(false, { n: 0, same: false }), rr(9000, 12000)]);
  add('vox_throw_syl', 'vox', SR, 9000, VS, [on(false, { n: 0, same: false }), rr(0, 2000), { ...on(false, { n: 0.5, same: false }), thr: true }, rr(2000, 4000), { ...on(false, { n: NaN, same: false }), thr: true }, rr(4000, 5000), { ...on(false, { n: Infinity, same: false }), thr: true }, rr(5000, 6000), { ...on(false, { n: -2.25, same: false }), thr: true }, off, rr(6000, 9000)]);
  add('vox_throw_set', 'vox', SR, 6000, VS, [on(false, { n: 0, same: false }), rr(0, 2000), { o: 'set', p: { txt: 'constructor' }, thr: true }, rr(2000, 3000), off, rr(3000, 4000), on(false, { n: 1, same: false }), rr(4000, 6000)]);   // the JS keeps the old syllables
  add('vox_blocks_odd', 'vox', SR, 3000, { txt: 'strengths' }, [on(false, { n: 0, same: false }), ...[7, 33, 100, 1, 15, 16, 17, 5, 90, 200, 481].reduce((a, n) => { const s = a.pos; a.ops.push(rr(s, s + n, 1 + n / 400)); a.pos += n; return a; }, { pos: 0, ops: [] }).ops, off, rr(1100, 3000)]);
  add('vox_frac_bounds', 'vox', SR, 400, VS, [on(false, { n: 0, same: false }), rr(0, 70.5), rr(70.5, 140), rr(20.25, 90, 1.5), off, rr(140, 299.5)]);
  add('vox_rate_extremes', 'vox', SR, 8000, VS, [on(false, { n: 0, same: false }), rr(0, 2000, 0), rr(2000, 4000, 256), rr(4000, 6000, 0.0001), off, rr(6000, 8000, 1000)]);
  add('vox_out_of_range', 'vox', SR, 300, VS, [on(false, { n: 0, same: false }), rr(0, 500), off, rr(250, 700), rr(-40, 20), rr(1e9, 1e9 + 50)]);
  add('vox_phonemes', 'vox', SR, 40000, { txt: '[B P T D K G CH JH F V TH DH S Z SH ZH HH M N NG L R W Y IY AY EY OW AW OY ER AX]', sylLen: 80, cons: 1.3 }, [...Array.from({ length: 32 }, (_, i) => [on(i > 0, { n: i, same: false }), rr(i * 1200, i * 1200 + 700), off, rr(i * 1200 + 700, i * 1200 + 1200)]).flat()]);
  return out;
}
// the two scripts of tests/bass-core.test.js, written as parametric scripts (the same generator in the C++ test): a stream of blocks, each checked finite
function nanScript(E, op) {
  const BassCore = E.BassCore, parts = [];
  const fin = (L, R) => { for (let k = 0; k < L.length; k++) if (!Number.isFinite(L[k]) || !Number.isFinite(R[k])) return false; return true; };
  const snap = (L, R) => { parts.push(Float32Array.from(L), Float32Array.from(R)); };
  if (op.o === 'nan1') {
    let seed = 7; const rnd = () => { seed = (seed * 16807) % 2147483647; return seed / 2147483647; }; const rates = [0.25, 0.5, 1, 2, 3, 4, 6, 8, 12, 16];
    for (let t = 0; t < op.trials; t++) {
      const c = new BassCore(48000); c.set({ parts: 64, det: op.det }); void rnd();
      const L = new Float32Array(128), R = new Float32Array(128); let ok = true;
      for (let blk = 0; blk < 60 && ok; blk++) {
        if (blk % 10 === 0) c.on(rnd() < .3);
        if (blk % 10 === 6 && rnd() < .5) c.off();
        const r = rates[Math.floor(rnd() * rates.length)], s = Math.floor(rnd() * 128);
        if (s > 0) c.render(L, R, 0, s, r);
        c.render(L, R, s, 128, r); ok = fin(L, R); snap(L, R);
      }
      if (!ok) throw new Error('the NaN case: non-finite output');
    }
  } else {
    const c = new BassCore(48000); c.set({ parts: 64, det: op.det });
    const L = new Float32Array(128), R = new Float32Array(128);
    c.on(false); for (let k = 0; k < 20; k++) { c.render(L, R, 0, 128, 1); snap(L, R); }
    c.off(); c.on(false);
    for (let k = 0; k < 5; k++) { c.render(L, R, 0, 128, 16); if (!fin(L, R)) throw new Error('the NaN case: after the jump'); snap(L, R); }
    for (let k = 0; k < 300; k++) { c.render(L, R, 0, 128, 1); if (!fin(L, R)) throw new Error('the NaN case: the voice stayed dead'); snap(L, R); }
  }
  return parts;
}
export function runScript(E, c) {
  const C = { bass: E.BassCore, vox: E.VoxCore }[c.kind], L = new Float32Array(c.len), R = new Float32Array(c.len), core = new C(c.sr); core.set(c.kind === 'vox' ? { ...c.p } : c.p);
  let stream = [];
  for (const op of c.ops) {
    if (op.o === 'nan1' || op.o === 'nan2') { stream.push(...nanScript(E, op)); continue; }
    const call = f => { if (op.thr) { let threw = false; try { f(); } catch (e) { threw = true; } if (!threw) throw new Error('script ' + c.name + ': expected a throw'); } else f(); };
    if (op.o === 'on') call(() => c.kind === 'vox' ? core.on(op.leg, op.n, op.same) : core.on(op.leg)); else if (op.o === 'off') core.off(); else if (op.o === 'set') call(() => core.set(op.p));
    else if (op.o === 'bpm') core.bpm = op.v; else if (op.o === 'wob') core.wob = op.v;
    else if (op.o === 'r') core.render(L, R, op.a, op.b, op.rate);
  }
  if (stream.length) return stream;
  if (c.kind === 'vox') for (let i = 0; i < c.len; i++) if (!Object.is(L[i], R[i]) && !(L[i] !== L[i] && R[i] !== R[i])) throw new Error('script: R differs from L in ' + c.name);
  return [L, R];
}

// ---- sequences: THE LIVE RULE -------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// A note sequence rendered the way the live shell does: each NOTE event (on, off) at its exact sample ends the control block before it, blocks of the core's size (BASS 32, the others 16) run from the last note event, a
// rate / bpm / wob event takes effect at the first control block that starts at or after it (the rate lane is a Web Audio parameter: a step, or a linear ramp from the value it has when the ramp's event is reached).
// `cuts` are sample positions where a host block ends (the shell's immediate mode also ends a control block there).
const BLK = { bass: 32, vox: 16, acid: 16, mach: 16 };
export function seqRender(E, c, cuts = []) {
  const C = { bass: E.BassCore, vox: E.VoxCore, acid: E.AcidCore, mach: E.MachCore }[c.kind], core = new C(c.sr); core.set(c.p);
  const N = c.N, L = new Float32Array(N), R = new Float32Array(N), BL = BLK[c.kind];
  const ev = c.events.map((e, i) => ({ ...e, i })).filter(e => e.t < N).sort((a, b) => a.t - b.t || a.i - b.i);
  let v0 = 1, t0 = 0, v1 = 1, t1 = 0;
  const at = t => t >= t1 ? v1 : v0 + (v1 - v0) * ((t - t0) / (t1 - t0));
  const note = e => e.type === 'on' || e.type === 'off';
  const apply = e => {
    switch (e.type) {
      case 'on': if (c.kind === 'vox') core.on(!!e.leg, Math.round(e.syl || 0), !!e.same); else if (c.kind === 'acid') core.on(!!e.leg, !!e.acc); else core.on(!!e.leg); break;
      case 'off': core.off(); break;
      case 'rate': v0 = v1 = e.v; t0 = t1 = e.t; break;
      case 'ramp': { const cur = at(e.t); v0 = cur; t0 = e.t; v1 = e.v; t1 = e.t + e.dur; break; }
      case 'bpm': core.bpm = e.v; break;
      case 'wob': core.wob = e.v; break;
    }
  };
  let ei = 0, s = 0;
  while (s < N) {
    while (ei < ev.length && ev[ei].t <= s) apply(ev[ei++]);
    let nn = Infinity; for (let j = ei; j < ev.length; j++) if (note(ev[j])) { nn = ev[j].t; break; }
    let end = Math.min(s + BL, nn, N);
    for (const cu of cuts) if (cu > s && cu < end) end = cu;
    core.render(L, R, s, end, at(s)); s = end;
  }
  return [L, R];
}
function seqCases(E) {
  const fx = JSON.parse(fs.readFileSync(path.join(GOLD, 'engine_u26_voices.json'), 'utf8')), out = [];
  const bassOf = n => { const v = fx.voices.find(x => x.k === 'engine|bass|' + n); const o = JSON.parse(JSON.stringify((v.v && v.v.bass) || {})); return o; };
  const voxOf = n => { const v = fx.voices.find(x => x.k === 'engine|vox|' + n); return JSON.parse(JSON.stringify((v.v && v.v.vox) || {})); };
  const add = (name, kind, p, N, events, sr = SR) => out.push({ name, live: true, kind, sr, p, N, events });
  const on = (t, o = {}) => ({ t, type: 'on', ...o }), off = t => ({ t, type: 'off' }), rate = (t, v) => ({ t, type: 'rate', v }), ramp = (t, v, dur) => ({ t, type: 'ramp', v, dur });
  // bass: nine scenarios on six sounds
  const BP = { init: {}, wobble: bassOf('Wobble 1/4'), reese: bassOf('Reese (DnB)'), acid: bassOf('Acid wobble 1/16'), pluck: bassOf('Pluck (DnB)'), talk: bassOf('Talk box'), glide: { ...bassOf('Laser'), glide: 90 } };
  const BS = {
    single: [on(0), off(6000)], single_late: [on(37), off(5003)], legato: [on(100), on(3000, { leg: true }), off(6000)], retrig: [on(0), on(2500), on(2501), off(7000)],
    glide: [on(0), rate(3000, 1.5), on(3000, { leg: true }), off(7000)], slide: [rate(0, 1), ramp(0, 2, 2000), on(0), rate(4000, 1), ramp(4000, 0.5, 1500), on(4000, { leg: true }), off(7500)],
    wob: [{ t: 0, type: 'bpm', v: 140 }, on(10), { t: 3000, type: 'wob', v: 0.5 }, { t: 5000, type: 'wob', v: 0 }, { t: 6000, type: 'bpm', v: 70 }, off(9000)],
    offsets: [on(1), off(2), on(33), off(64), on(65, { leg: true }), off(127), on(128), off(129), on(130), on(160, { leg: true }), rate(161, 3), off(255), on(256), off(257), on(300), rate(310, 2), off(1000)],
    off_early: [on(5), off(9), on(9), off(10), on(11), off(4000)],
    // the tempo and wobble events on control-block starts (the blocks start at 10 + 32 k after the note): one sample late changes the sound
    wobedge: [{ t: 0, type: 'bpm', v: 140 }, on(10), { t: 3018, type: 'wob', v: 0.5 }, { t: 5002, type: 'wob', v: 0 }, { t: 5994, type: 'bpm', v: 70 }, off(9000)],
    // D7: a slide cut by the next note glides ON up to it (the page's cancelAndHoldAtTime), then the note's own rate
    slidecut: [rate(0, 1), on(0), rate(2000, 1.25), ramp(2000, 2, 6000), on(2000, { leg: true }), rate(4000, 0.8), on(4000), off(9000)],
  };
  for (const [pn, p] of Object.entries(BP)) for (const [sn, evs] of Object.entries(BS)) add('seq_bass_' + pn + '_' + sn, 'bass', p, sn === 'offsets' ? 3000 : 10000, evs);
  add('seq_bass_sr22050', 'bass', BP.reese, 8000, BS.legato, 22050);
  // vox: seven scenarios on four sounds
  const VP = { init: {}, choir: voxOf('Choir'), rasp: voxOf('Raspy rock'), robot: { ...voxOf('Robot'), txt: 'ba-na-na, la' } };
  const VSC = {
    single: [on(0, { syl: 0 }), off(5000)], sylchange: [on(0, { syl: 0 }), off(3000), on(3100, { syl: 1 }), off(6000), on(6200, { syl: 2, leg: true }), off(9000), on(9001, { syl: 3 }), off(14000)],
    same: [on(0, { syl: 0 }), on(4000, { syl: 1, same: true, leg: true }), on(7000, { syl: 2, leg: true }), off(10000)], rest: [on(0, { syl: 0 }), off(3000), on(3500, { syl: 1 }), off(5000), on(6000, { syl: 4 }), off(9000), on(9500, { syl: 99 }), off(13000)],
    glide: [rate(0, 1), on(0, { syl: 0 }), rate(4000, 1.5), ramp(4000, 2, 3000), on(4000, { syl: 1, same: true, leg: true }), off(9000)], tight: [on(0, { syl: 0 }), off(49), on(55, { syl: 1 }), off(70), on(71, { syl: 2 }), off(9000)],
    slidecut: [rate(0, 1), on(0, { syl: 0 }), rate(2000, 1.25), ramp(2000, 2, 6000), on(2000, { syl: 0, same: true, leg: true }), rate(4000, 0.8), on(4000, { syl: 1 }), off(9000)],
    offsets: [on(1, { syl: 0 }), off(2), on(17, { syl: 1 }), off(31), on(32, { syl: 2 }), off(33), on(48, { syl: 3, leg: true }), off(500), on(501, { syl: 4 }), rate(505, 2), off(3000)],
  };
  for (const [pn, p] of Object.entries(VP)) for (const [sn, evs] of Object.entries(VSC)) add('seq_vox_' + pn + '_' + sn, 'vox', p, sn === 'offsets' ? 4000 : 16000, evs);
  // acid / mach: five scenarios on three sounds each
  const AP = { init: {}, fast: { slide: 200, accent: 1, cut: 900, res: 0.85 }, sub: { sub: 0.5, noise: 0.2, wave: 0.7 } }, MP = { init: {}, ens: { machine: 4, a: 0.35, glide: 150 }, lfo: { machine: 3, lfoDest: 2, lfoDepth: 0.7, lfoRate: 5 } };
  const XS = { slidecut: [on(0), rate(2000, 1.25), ramp(2000, 2, 6000), on(2000, { leg: true }), rate(4000, 0.8), on(4000), off(6500)], single: [on(0), off(4000)], legato: [on(10), on(2000, { leg: true }), off(4500)], retrig: [on(0, { acc: true }), on(1500), on(1501, { acc: true }), off(4000)], glide: [on(0), rate(2000, 2), on(2000, { leg: true, acc: true }), ramp(3000, 0.5, 1000), off(5000)], offsets: [on(1), off(2), on(17), off(31), on(32, { leg: true }), off(33), on(34), off(900)] };
  for (const [pn, p] of Object.entries(AP)) for (const [sn, evs] of Object.entries(XS)) add('seq_acid_' + pn + '_' + sn, 'acid', p, 7000, evs);
  for (const [pn, p] of Object.entries(MP)) for (const [sn, evs] of Object.entries(XS)) add('seq_mach_' + pn + '_' + sn, 'mach', p, 7000, evs);
  return out;
}

// ---- hashing and the three outputs -----------------------------------------------------------------------------------------------------------------------------------------------------------------------
function renderCase(E, c) {
  if (c.script) return runScript(E, c);
  if (c.parse) return c.texts.map(t => parseOne(E, t)).join('\n');
  if (c.live) return seqRender(E, c);
  const v = E.mkVoice(c.name, [], JSON.parse(JSON.stringify(c.v))); v.fx = c.fx.map(f => ({ ...f })); E.setBpm(c.bpm); try { return [E.renderVoice(v, c.sr, null, c.hold)]; } finally { E.setBpm(120); }
}
const hashOf = r => typeof r === 'string' ? crypto.createHash('sha256').update(r, 'utf8').digest('hex') : shaOf(r);
const lenOf = r => typeof r === 'string' ? Buffer.byteLength(r, 'utf8') : r[0].length;
function sane(r, what) { if (typeof r === 'string') return; for (const a of r) for (let i = 0; i < a.length; i++) if (!Number.isFinite(a[i])) throw new Error('a render holds a non-finite sample (' + what + ' at ' + i + '): not allowed in a golden'); }
const rowRe = /\{("(?:[^"\\]|\\.)*"), "([0-9a-f]{64})", (\d+)\}/g;

function main(argv) {
  if (argv[0] === '--fixture') return fixture(path.resolve(argv[1] || '/home/claude/wt/beta-ref'));
  if (argv[0] === '--page-live') return import('./u26-pagelive.mjs').then(m => m.main(E_ROOT()));
  const check = argv[0] === '--check', golden = argv[0] === '--golden', withSamples = argv.includes('--samples'), outDir = (check || golden) ? null : argv.filter(a => !a.startsWith('--'))[0] || '/tmp/u26/engine-ref';
  const E = engineOf(ROOT), fx = JSON.parse(fs.readFileSync(path.join(GOLD, 'engine_u26_voices.json'), 'utf8'));
  const inc = fs.readFileSync(path.join(GOLD, 'engine_u26_voices_golden.inc'), 'utf8'), casesPath = path.join(GOLD, 'engine_u26_golden.inc');
  const gold = new Map([...inc.matchAll(rowRe)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const rows = []; let bad = 0;
  if (check) { const cur = fs.existsSync(path.join(ROOT, 'native/core/src/engine/vox_tables.inc')) ? fs.readFileSync(path.join(ROOT, 'native/core/src/engine/vox_tables.inc'), 'utf8') : ''; if (cur !== tablesText(ROOT)) { bad++; console.error('vox_tables.inc is not what index.html gives: run node native/tools/u26-tables.mjs'); } }
  fx.voices.forEach((s, i) => {
    const r = E.renderVoice(build(E, s, s.k), SR, null, s.hold), h = shaOf([r]), g = gold.get(s.k);
    if (check && (!g || g.sha !== h || g.len !== r.length)) { bad++; console.error('DIFFERS from the golden: ' + s.k); }
    rows.push({ k: s.k, sha256: h, len: r.length });
    if (outDir && withSamples) fs.writeFileSync(path.join(outDir, 'u' + i + '.f32'), Buffer.from(r.buffer, r.byteOffset, r.byteLength));
  });
  const only = process.env.U26_ONLY, cases = [...directCases(E), ...coreScripts(E), ...parseCases(E), ...seqCases(E)].filter(c => !only || c.name.includes(only)), names = new Set();
  const crow = cases.map((c, i) => {
    if (names.has(c.name)) throw new Error('case name twice: ' + c.name); names.add(c.name);
    const t0 = Date.now(), r = renderCase(E, c); if (process.env.U26_TIMES && Date.now() - t0 > 300) console.error(c.name, Date.now() - t0, 'ms'); try { sane(r, c.name); } catch (e) { if (process.env.U26_LIST_NONFINITE) { console.error(c.name); return { name: c.name, sha256: '', len: 0 }; } throw e; }
    if (outDir && withSamples && typeof r !== 'string') fs.writeFileSync(path.join(outDir, 'c' + i + '.f32'), Buffer.concat(r.map(a => Buffer.from(a.buffer, a.byteOffset, a.byteLength))));
    return { name: c.name, sha256: hashOf(r), len: lenOf(r), ...(typeof r === 'string' ? { s: r } : {}) };
  });
  if (golden) {
    fs.writeFileSync(casesPath, ['// GENERATED by native/tools/engine-u26-dump.mjs --golden from the real ENGINE block (the direct cases, core scripts, parse batches and live-rule sequences): sha256 of renderVoice(v, sr, null, hold) (of L then R for a script or a sequence; of the batch\'s syllable text for a parse case) and its length, Node 22 on x86-64 Linux.',
      '// Do not edit: a mismatch means the port is wrong (or the JS changed on purpose: regenerate with --golden and say so).',
      'struct U26CaseGolden { const char* name; const char* sha256; long len; };', 'static const U26CaseGolden kU26CaseGolden[] = {', ...crow.map(r => `  {${JSON.stringify(r.name)}, ${JSON.stringify(r.sha256)}, ${r.len}},`), '};',
      'static const int kU26CaseGoldenN = ' + crow.length + ';', ''].join('\n'));
    console.log('wrote ' + crow.length + ' cases'); return;
  }
  if (check) {
    const cg = new Map([...fs.readFileSync(casesPath, 'utf8').matchAll(rowRe)].map(m => [JSON.parse(m[1]), { sha: m[2], len: +m[3] }]));
    if (cg.size !== crow.length) { console.error('the committed case list has ' + cg.size + ' cases, the generator ' + crow.length); bad++; }
    for (const r of crow) { const g = cg.get(r.name); if (!g || g.sha !== r.sha256 || g.len !== r.len) { bad++; console.error('DIFFERS from the committed hash: case ' + r.name); } }
    if (bad) { console.error(bad + ' differ'); process.exit(1); }
    console.log('engine_u26: ' + rows.length + ' voices and ' + crow.length + ' cases match the committed hashes'); return;
  }
  fs.writeFileSync(path.join(outDir, 'ref.json'), JSON.stringify({ voices: rows, cases: crow }));
  fs.writeFileSync(path.join(outDir, 'cases.json'), JSON.stringify({ cases }));
  console.log('reference written to ' + outDir + ': ' + rows.length + ' voices, ' + cases.length + ' cases');
}
const E_ROOT = () => ROOT;
if (process.argv[1] === fileURLToPath(import.meta.url)) main(process.argv.slice(2));
