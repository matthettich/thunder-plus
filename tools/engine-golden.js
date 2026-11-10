'use strict';
// Renders every factory voice / preset of Thunder Plus's JavaScript sound engines in plain Node (no browser, no audio
// device) and fingerprints each render. Used by tests/engine-golden.test.js; run `node tools/engine-golden.js` to print a
// summary. Test-only: nothing in the app is changed. The code under test is cut out of index.html by text anchors (not
// line numbers) and run in a vm, the same approach as tools/drums/render.mjs and tests/helpers.js.
//
// What a "render" is here: renderVoice(voice, 48000, null, hold), exactly what the app's pad render (Worker or page) calls.
// A pad is rendered ONCE, at its own pitch (C2 at Tune 0 for drums; the note and the velocity are applied when the pad is
// played back, they are not inputs of the render), so "fixed note and velocity" means these defaults. Length is the voice's
// own `len`, with hold = 0 for drum voices and hold = 1 s for synth voices (ADSR layers or a non-drum engine), as the
// sweep tests (tests/sweep/engines.mjs) render them.
const fs = require('fs'), path = require('path'), vm = require('vm'), crypto = require('crypto');
const ROOT = path.resolve(__dirname, '..');
const SR = 48000;

/* ---- cutting code out of index.html -------------------------------------------------------------------------- */
const between = (src, from, to, incl) => { const a = src.indexOf(from); if (a < 0) throw new Error('anchor not found: ' + from);
  const b = src.indexOf(to, a); if (b < 0) throw new Error('end anchor not found: ' + to); return src.slice(a, b + (incl ? to.length : 0)); };
const lineAt = (src, from) => { const a = src.indexOf(from); if (a < 0) throw new Error('anchor not found: ' + from); const b = src.indexOf('\n', a); return src.slice(a, b < 0 ? src.length : b); };

function sources() {
  const h = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
  return [
    between(h, '/* ===ENGINE START=== */', '/* ===ENGINE END=== */', true),
    // value formatters and helpers that the engine cards (XUI) use at load time
    between(h, 'const st = v => (v > 0', "const oct = v =>", false) + lineAt(h, 'const oct = v =>'),
    lineAt(h, 'const nameOf = list =>'),
    // factory presets of the Bass / Blip / Vox engines and the app's own preset functions (run against a stub voice)
    between(h, 'const BASS_PRE = [', '\n];', true), between(h, 'const BLIP_PRE = [', '\n];', true), between(h, 'const VOX_PRE = [', '\n];', true),
    lineAt(h, 'const VOX_BASE = {'), lineAt(h, 'const ENGINE_LEVEL = '),
    between(h, 'function bassPreset(i, quiet){', '\n}\n', true), between(h, 'function blipPreset(i, quiet){', '\n}\n', true),
    between(h, 'function voxPreset(i, quiet){', '\n}\n', true), between(h, 'function xPreset(i, quiet){', '\n}\n', true),
    // the whole "Presets: whole voices and single layers" part: PD / PS / PE, the drum kits, factoryVoicePresets, factoryLayerPresets
    between(h, 'const PFX = (t, o) =>', 'const flp = () =>', false) + lineAt(h, 'const flp = () =>'),
    between(h, 'const XUI = {', '\n};', true),
  ].join('\n');
}

let _ctx = null;
function engine() {
  if (_ctx) return _ctx;
  const ctx = vm.createContext({ console: { log() {}, warn() {}, error() {} }, Math, Float32Array, Float64Array, Int16Array, Int32Array, Uint8Array, Uint32Array, Array, Object,
    JSON, Map, Set, WeakMap, Number, String, Boolean, Error, Promise, setTimeout, clearTimeout, performance, WebAssembly, RegExp, Date, Symbol, isFinite, isNaN, parseInt, parseFloat });
  ctx.globalThis = ctx; ctx.self = ctx;
  // `cur` / `V` / `isSyn` / `changed` / `refreshAll` are the page's; the preset functions only touch them when `quiet` is false.
  const stub = 'let __cur = null; const V = () => __cur; const cur = 0; const GM_FONT = {id:"gm"}; const sfPreparePad = () => ({then(){}});\n';
  const tail = '\n;globalThis.__E = {XUI, XENG, TYPES, BASS_PRE, BLIP_PRE, VOX_PRE, fvp, flp, factoryKit, mkVoice, renderVoice, PD, PS, PE, normX, normBass, normBlip, normVox,\n' +
    ' bassPreset, blipPreset, voxPreset, xPreset, ENGINE_LEVEL, setSeed: n => { _seed = n >>> 0; }, setCur: v => { __cur = v; }};';
  vm.runInContext(stub + sources() + tail, ctx, { filename: 'index.html(engine)' });
  return (_ctx = ctx.__E);
}

/* ---- the list of factory voices ----------------------------------------------------------------------------- */
const DRUM_ENGINES = new Set(['tone', 'fmdrum', 'skin', 'breath', 'blip']);
const NOT_COVERED = [
  { what: 'pump synths (pmono, ppoly, pcoil, porbit, pfold, pgloss, ptract, pstrike, pboing, px0x, plife, pfreeze, pwob and live twins)', reason: 'wasm (pump/synth/pump-synth.wasm); already hashed by tests/pump-synth.test.js and tests/golden/pump-synth.json' },
  { what: 'LXR drum voices (lxdrum, lxsnare, lxcym, lxhat), also inside ELECTRO.thunder', reason: 'lxr/lxr.wasm needs the lxr-engine.js glue loaded and a compiled module; no existing test loads it in Node, so it is not covered here (the sweep covers it in Chrome)' },
  { what: 'layer types plaits and elements, and wasm-backed layer presets', reason: 'need thunder-dsp.wasm (PL / TX), which the page instantiates; no Node loader exists in the tests' },
  { what: 'layer type waves and engine wavesl (waves live)', reason: 'need the waves engine (waves/waves-worklet.js, WavesDSP) and a patch; covered by tests/waves.test.js' },
  { what: 'engine sf (SoundFont / General MIDI)', reason: 'needs the 24 MB SF3 decoded by Web Audio decodeAudioData; the page renders it' },
  { what: 'layer types sample, grains, pwm, and resynth / table with imported data', reason: 'need sample or wavetable data from the song or a file; Resynth is covered through its built-in demo analysis, Table through its default tables' },
];
const NEEDS_DATA_TYPES = new Set(['plaits', 'elements', 'waves', 'sample', 'grains', 'pwm']);

function listVoices() {
  const E = engine(), out = [], seen = new Map();
  const add = (key, build) => { const n = (seen.get(key) || 0) + 1; seen.set(key, n); out.push({ key: n > 1 ? key + ' #' + n : key, build }); };
  const holdOf = v => (v.engine && v.engine !== 'layers' && !DRUM_ENGINES.has(v.engine)) || (v.layers || []).some(l => l.on && l.mode === 'ADSR') ? 1 : 0;
  // 1. the preset sheet's factory voices (drum kits, drum types, synth groups, engine presets, hack, owls...)
  const groups = E.fvp();
  for (const [g, list] of groups) for (const [name, make] of list) {
    add('voice|' + g + '|' + name, () => { const v = make(); return { v, hold: holdOf(v) }; });
  }
  // 2. the layer presets, each alone on a pad (drum layers on a drum pad, synth layers on a synth slot)
  for (const [g, list] of E.flp()) for (const [name, l, vt] of list) {
    add('layerpreset|' + g + '|' + name, () => { const v = (vt ? E.PS : E.PD)(name, [Object.assign({}, l)], {}); return { v, hold: vt ? 1 : 0 }; });
  }
  // 3. every layer oscillator type on its own, at its defaults
  for (const t of E.TYPES) {
    if (NEEDS_DATA_TYPES.has(t.id)) continue;
    add('layertype|' + t.id, () => { const v = E.PD('type ' + t.id, [{ type: t.id }], {}); return { v, hold: 0 }; });
  }
  // 4. every engine's own presets (the engine card's list, and Bass / Blip / Vox), on a fresh pad as setEngine() makes it
  const fresh = (k, name) => { const v = E.PD(name, [{ type: 'sine' }], {}); v.engine = k; v.tune = 0; v.level = E.ENGINE_LEVEL; return v; };
  for (const k of Object.keys(E.XENG)) {
    if (k === 'sf' || k === 'wavesl') continue;
    const pre = (E.XUI[k] && E.XUI[k].pre) || [];
    pre.forEach(([pn], i) => add('engine|' + k + '|' + pn, () => {
      const v = fresh(k, k + ' ' + pn); v[k] = E.normX(k, null); E.setCur(v); E.xPreset(i, true);
      return { v, hold: DRUM_ENGINES.has(k) ? 0 : 1 }; }));
  }
  E.BASS_PRE.forEach(([pn], i) => add('engine|bass|' + pn, () => { const v = fresh('bass', 'bass ' + pn); v.bass = E.normBass(null); E.setCur(v); E.bassPreset(i, true); return { v, hold: 1 }; }));
  E.BLIP_PRE.forEach(([pn], i) => add('engine|blip|' + pn, () => { const v = fresh('blip', 'blip ' + pn); v.blip = E.normBlip(null); E.setCur(v); E.blipPreset(i, true); return { v, hold: 0 }; }));
  E.VOX_PRE.forEach(([pn], i) => add('engine|vox|' + pn, () => { const v = fresh('vox', 'vox ' + pn); v.vox = E.normVox(null); E.setCur(v); E.voxPreset(i, true); return { v, hold: 1 }; }));
  // 5. the shipped starter kits: factoryKit() and the 40 voices of sounds/ELECTRO.thunder (LXR voices are not covered)
  E.factoryKit().voices.forEach((v0, i) => add('factorykit|' + i + '|' + v0.name, () => ({ v: E.factoryKit().voices[i], hold: 0 })));
  const song = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds', 'ELECTRO.thunder'), 'utf8'));
  (song.kit.voices || []).forEach((jv, i) => {
    const en = jv.engine || 'layers';
    if (!(en === 'layers' || en === 'bass' || en === 'vox' || en === 'blip' || (E.XENG[en] && en !== 'sf' && en !== 'wavesl'))) return;   // lx*, p* (pump), sf, wavesl: not covered
    add('electro|' + String(i).padStart(2, '0') + '|' + jv.name, () => { const v = JSON.parse(JSON.stringify(jv)); return { v, hold: holdOf(v) }; });
  });
  return out;
}
/* The voices that are not rendered because they need what plain Node does not have: `listVoices` skips them silently, so the
   list above is the only place a skipped voice is defined. These lines count them for the README / the test. */

/* ---- rendering and fingerprints ------------------------------------------------------------------------------ */
function seedFor(key) { let h = 0x811c9dc5; for (let i = 0; i < key.length; i++) { h ^= key.charCodeAt(i); h = Math.imul(h, 0x01000193) >>> 0; } return h || 1; }
const sig6 = x => Number(x.toPrecision(6));
function fingerprint(data) {
  const n = data.length, sha = crypto.createHash('sha256');
  const chans = data.r ? [data, data.r] : [data];
  for (const c of chans) sha.update(Buffer.from(c.buffer, c.byteOffset, c.byteLength));
  let pk = 0, ss = 0, bad = 0; const sl = new Array(8).fill(0);
  for (let i = 0; i < n; i++) { const x = data[i]; if (!Number.isFinite(x)) { bad++; continue; } const a = Math.abs(x); if (a > pk) pk = a; ss += x * x; sl[Math.min(7, Math.floor(i * 8 / n))] += x * x; }
  const rms = Math.sqrt(ss / n), per = n / 8;
  return { len: n, sha256: sha.digest('hex'), peak: sig6(pk), rms: sig6(rms), slices: sl.map(s => sig6(Math.sqrt(s / per))), ...(bad ? { nonFinite: bad } : {}), ...(data.r ? { stereo: true } : {}) };
}
function renderOne(item) {
  const E = engine();
  E.setSeed(seedFor(item.key));   // the layers' noise seeds come from a counter that every voice built bumps: pin it per voice
  const { v, hold } = item.build();
  return E.renderVoice(v, SR, null, hold);
}
/* Render a voice: { fp } on success, { error } when the engine throws (an error message is a stable fingerprint too). */
function renderFingerprint(item) {
  try { return { fp: fingerprint(renderOne(item)) }; } catch (e) { return { error: String(e && e.message || e).slice(0, 200) }; }
}

module.exports = { SR, ROOT, NOT_COVERED, listVoices, renderFingerprint, renderOne, fingerprint, engine, seedFor };

if (require.main === module) {
  const t0 = Date.now(), all = listVoices(); let n = 0, err = 0, nf = 0;
  const filter = process.argv[2];
  for (const it of all) { if (filter && !it.key.toLowerCase().includes(filter.toLowerCase())) continue; const t = Date.now(), r = renderFingerprint(it); n++;
    if (r.error) { err++; console.log('ERROR', it.key, r.error); } else if (r.fp.nonFinite) { nf++; console.log('NONFINITE', it.key, r.fp.nonFinite); }
    if (process.env.VERBOSE) console.log(it.key, r.fp ? r.fp.peak : '', Date.now() - t, 'ms'); }
  console.log(n + ' voices, ' + err + ' errors, ' + nf + ' non-finite, ' + (Date.now() - t0) + ' ms');
}
