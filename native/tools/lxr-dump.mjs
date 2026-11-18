// U04: THE LXR REFERENCE (Node only): the web app's own render path, lxr/lxr-engine.js (LxrCore.render: the page's parameter mapping, the call sequence into the firmware, the resample, Level / Drive,
// the trim), UNMODIFIED, with the shipped lxr/lxr.wasm, dumping SAMPLES. The wasm is built with -O3 -ffast-math (lxr/build.sh), so no native build reproduces it bit for bit; the gate is
// native/tests/test_lxr.cpp (the rule of docs/port/TODAY.md "Evening, 16 Nov" item 1: strict native, every case within -100 dB of the reference's peak, exact cases kept exact, named exceptions).
//   node native/tools/lxr-dump.mjs [--wasm LXR.wasm] [outDir]    writes <outDir>/cases.json, <outDir>/<case>.f32 (the finished pad sound, mono float32) and <outDir>/<case>.raw.f32 (the firmware's own
//                                                                 voice at 44.14 kHz before the resample; what the wasm's lxr_render produced after the hit)
//   node native/tools/lxr-dump.mjs --check                        the case list (names) against native/tests/golden/lxr_cases.inc
//   node native/tools/lxr-dump.mjs --write-cases                  writes that list (committed)
// How the page's call is seen without changing it: the script gets a WebAssembly whose Instance hands back the exports wrapped, so each render's real calls are recorded (the firmware parameter bytes
// at lxr_load, the bpm, the trigger, the voice's blocks after it). Those are what the C++ must reproduce: the bytes exactly, the samples within the gate.
// CASES: every factory preset of the four engines (Init and the 36 others), the init kit's seven pads (LxrCore.initKit), the LXR voices of sounds/*.thunder (ELECTRO.thunder has two), and direct cases: every
// engine with all knobs at 0, at 127, at 255 (the page clamps to a byte), at the middle and four seeded random sets, each velocity / LFO target, the pad's Tune / Hit / Level / Drive / hold / length, the song tempo
// with a synced LFO, the open hat, and the sample rates 8000 to 96000.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const args = process.argv.slice(2); const opt = {}; const pos = [];
for (let i = 0; i < args.length; i++) { if (args[i] === '--wasm') opt.wasm = args[++i]; else if (args[i].startsWith('--')) opt[args[i].slice(2)] = true; else pos.push(args[i]); }
const WASM = opt.wasm || process.env.LXR_WASM || path.join(ROOT, 'lxr/lxr.wasm');
const JS = fs.readFileSync(path.join(ROOT, 'lxr/lxr-engine.js'), 'utf8');

// ---- the page's script, unmodified, with a recording WebAssembly ------------------------------------------------------------------------------------------------------------------
const rec = { seed: null, params: null, bpm: null, trig: null, raw: [] };
const realWA = WebAssembly;
const WA = {
  Module: realWA.Module, compile: realWA.compile,
  Instance: function (m, imp) {
    const inst = new realWA.Instance(m, imp), ex = inst.exports, w = Object.create(null);
    for (const k of Object.keys(ex)) w[k] = ex[k];
    w.lxr_init = s => { rec.seed = s >>> 0; rec.params = null; rec.bpm = null; rec.trig = null; rec.raw = []; return ex.lxr_init(s); };
    w.lxr_set_bpm = b => { rec.bpm = b | 0; return ex.lxr_set_bpm(b); };
    w.lxr_load = () => { rec.params = Array.from(new Uint8Array(ex.memory.buffer, ex.lxr_params(), ex.lxr_num_params())); return ex.lxr_load(); };
    w.lxr_trigger = (v, vel, note) => { rec.trig = [v | 0, vel | 0, note | 0]; return ex.lxr_trigger(v, vel, note); };
    w.lxr_render = (v, n) => { const c = ex.lxr_render(v, n); if (rec.trig) rec.raw.push(Float32Array.from(new Float32Array(ex.memory.buffer, ex.lxr_out(), c))); return c; };
    return { exports: w };
  },
};
function loadCtx(text, extra) {
  const c = { console: { warn() {}, log() {} }, Math, Float32Array, Uint8Array, WebAssembly: extra || WA, Object, Array, Set, Map, JSON, String, Number, isFinite };
  c.self = c; c.globalThis = c; vm.createContext(c); vm.runInContext(text, c, { filename: 'lxr-engine.js' }); return c;
}
const wasmBytes = fs.readFileSync(WASM);
const C = loadCtx(JS); const L = C.LxrCore; L.attachModule(new realWA.Module(wasmBytes));
// the page's tables, for naming parameters (a second copy of the script with the one export line the table generator also adds; never used to render)
const hook = 'G.LxrCore = LxrCore;';
const C2 = loadCtx(JS.replace(hook, hook + ' G.__ENG = ENG; G.__META = META;'), realWA);
const META = C2.__META, ENG = C2.__ENG;
const PN = {}; for (const k in META.par) if (META.par[k] < META.nparams) PN[k.replace(/^PAR_/, '')] = META.par[k];
const KINDS = ['lxdrum', 'lxsnare', 'lxcym', 'lxhat'];
const xe = L.xeng(), ui = L.xui();
const mb32 = seed => { let a = seed >>> 0; return () => { a = (a + 0x6D2B79F5) >>> 0; let t = a; t = Math.imul(t ^ (t >>> 15), t | 1); t ^= t + Math.imul(t ^ (t >>> 7), t | 61); return ((t ^ (t >>> 14)) >>> 0) / 4294967296; }; };
const hashOf = s => { let h = 0x811c9dc5 >>> 0; for (const ch of Buffer.from(s)) { h ^= ch; h = Math.imul(h, 0x01000193) >>> 0; } return h; };
const knobs = k => Object.keys(xe[k].d()).filter(s => /^p\d+$/.test(s));
// the page's normX + xParams: the engine's defaults, overridden by the finite numbers of the keys it has, then the voice's tune / drive / level
function pad(k, o, v = {}) { const d = xe[k].d(), r = {}; for (const s in d) r[s] = o && typeof o[s] === 'number' && isFinite(o[s]) ? o[s] : d[s]; return Object.assign(r, { tune: v.tune || 0, drive: v.drive || 0, level: v.level || 0 }); }

// ---- the case list -----------------------------------------------------------------------------------------------------------------------------------------------------------
const cases = [];
const add = (name, k, P, sr = 48000, hold = 0, vlen = null, bpm = 120) => cases.push({ name, k, P, sr, hold, vlen: vlen == null ? L.len(k) : vlen, bpm });
for (const k of KINDS) ui[k].pre.forEach(([n, o], i) => add('pre_' + k + '_' + i, k, pad(k, o), 48000, 0, L.len(k)));   // Init and the factory presets (the page's Presets list)
L.initKit().forEach((p, i) => add('kit_' + i + '_' + p.kind, p.kind, pad(p.kind, p.o), 48000, 0, L.len(p.kind)));        // the init kit's seven pads
for (const f of fs.readdirSync(path.join(ROOT, 'sounds')).filter(f => f.endsWith('.thunder')).sort()) {   // the LXR voices of the shipped songs
  let s; try { s = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds', f), 'utf8')); } catch { continue; }
  const vs = (s.kit && s.kit.voices) || [], bpm = (s.song && s.song.bpm) || 120;
  vs.forEach((v, i) => { if (v && KINDS.includes(v.engine)) add('song_' + f.replace(/\.thunder$/, '') + '_' + i + '_' + v.engine, v.engine, pad(v.engine, v[v.engine], v), 48000, 0, v.len, bpm); });
}
for (const k of KINDS) {
  const base = () => pad(k, {}), ks = knobs(k), nn = { lxdrum: 1, lxsnare: 4, lxcym: 5, lxhat: 6 }[k], volKnob = 'p' + PN['VOL' + nn];
  const audible = P => { rec.trig = null; const o = L.xeng()[k].r(P, 48000, 0.4, { len: 0.4 }, 120); let pk = 0; for (const x of o) { const a = Math.abs(x); if (a > pk) pk = a; } return pk > 0.02; };
  for (const [tag, val] of [['lo', 0], ['mid', 64], ['hi', 127], ['max', 255]]) { const P = base(); for (const s of ks) P[s] = val; P[volKnob] = 100; add(tag + '_' + k, k, P, 48000, 0.3, 0.3); }   // every knob together (the voice's volume held up)
  for (const s of ks) for (const val of [0, 127]) { const P = base(); P[s] = val; add('k' + val + '_' + k + '_' + s, k, P, 48000, 0.2, 0.2); }       // each knob alone at its extremes
  for (let j = 0, tries = 0; j < 4 && tries < 200; tries++) {   // four seeded random sets that sound (a random set of the voice's knobs is often silent)
    const r = mb32(hashOf(k) + 7919 * tries), P = base(); for (const s of ks) P[s] = tries % 2 ? Math.floor(r() * 128) : Math.round(r() * 1270) / 10; P[volKnob] = 90 + Math.floor(r() * 38);
    if (audible(P)) { add('rnd' + j + '_' + k, k, P, 48000, 0.4, 0.4); j++; }
  }
  // every velocity and LFO target of the engine, with the knobs that make it audible: the amount of velocity modulation / the LFO at full, a fast LFO
  const n = nn, nt = ui[k].rows.vdst.max;
  for (let t = 1; t <= nt; t++) { const P = pad(k, {}); P['p' + PN['VELO_MOD_AMT_' + n]] = 127; P['p' + PN['VOLUME_MOD_ON_OFF' + n]] = 0; P.vdst = t; P.vel = 40; add('vdst_' + k + '_' + t, k, P, 48000, 0.25, 0.25); }
  for (let t = 1; t <= nt; t++) { const P = pad(k, {}); P['p' + PN['AMOUNT_LFO' + n]] = 100; P['p' + PN['FREQ_LFO' + n]] = 90; P['p' + PN['WAVE_LFO' + n]] = t % 7; P.ldst = t; add('ldst_' + k + '_' + t, k, P, 48000, 0.25, 0.25); }
  for (const t of [-24, -7.6, 0.4, 0.5, 12, 24, 200, -200]) add('tune_' + k + '_' + t, k, Object.assign(base(), { tune: t }), 48000, 0.25, 0.25);
  for (const t of [1, 2, 40, 126, 127]) add('vel_' + k + '_' + t, k, Object.assign(base(), { vel: t }), 48000, 0.25, 0.25);
  for (const t of [0.5, 64.5, 1000, -5]) add('velx_' + k + '_' + t, k, Object.assign(base(), { vel: t }), 48000, 0.25, 0.25);
  for (const lv of [-24, -6, 6, 12]) add('level_' + k + '_' + lv, k, Object.assign(base(), { level: lv }), 48000, 0.25, 0.25);
  for (const dr of [0.0005, 0.002, 0.3, 1]) add('drive_' + k + '_' + dr, k, Object.assign(base(), { drive: dr }), 48000, 0.25, 0.25);
  for (const h of [0.01, 0.05, 0.3, 1, 3, 12, 40]) add('hold_' + k + '_' + h, k, base(), 48000, h, 2);
  for (const l of [0, 0.1, 5]) add('len_' + k + '_' + l, k, base(), 48000, 0, l);
  for (const b of [0, 20, 60, 131.6, 240, 400, 1000, 1e12]) { const P = pad(k, {}); P['p' + PN['SYNC_LFO' + n]] = 3; P['p' + PN['AMOUNT_LFO' + n]] = 110; P['p' + PN['FREQ_LFO' + n]] = 70; P.ldst = 1; add('bpm_' + k + '_' + b, k, P, 48000, 0.5, 0.5, b); }
  for (const sr of [8000, 11025, 22050, 32000, 44100, 44100.4, 44140, 44140.4, 48000, 88200, 96000]) add('sr_' + k + '_' + sr, k, base(), sr, 0.4, 0.4);
  add('sr_' + k + '_192000', k, base(), 192000, 0.15, 0.15);
}
{ const P = pad('lxhat', {}); P.open = 1; add('open_lxhat', 'lxhat', P, 48000, 0.5, 0.5); P.open = 0.4; add('open_lxhat_0.4', 'lxhat', P, 48000, 0.5, 0.5); }
{ const P = pad('lxhat', {}); P.open = 1; P.p61 = 127; add('open_lxhat_long', 'lxhat', P, 48000, 6, 6); }   // a long open hat: the voice dies away before the hold

// ---- run -----------------------------------------------------------------------------------------------------------------------------------------------------------------------
if (opt.check || opt['write-cases']) {
  const inc = '// The case list of tools/lxr-dump.mjs (names, in order). Written by `node native/tools/lxr-dump.mjs --write-cases`; never hand-edited.\nstatic const char* const kLxrCases[] = {\n' + cases.map(c => '  "' + c.name + '",\n').join('') + '};\nstatic const int kLxrCaseN = ' + cases.length + ';\n';
  const f = path.join(ROOT, 'native/tests/golden/lxr_cases.inc');
  if (opt['write-cases']) { fs.writeFileSync(f, inc); console.log('wrote ' + f + ' (' + cases.length + ' cases)'); }
  else { const have = fs.existsSync(f) ? fs.readFileSync(f, 'utf8') : ''; if (have !== inc) { console.error('native/tests/golden/lxr_cases.inc differs from the case list: run node native/tools/lxr-dump.mjs --write-cases'); process.exit(1); } console.log('lxr_cases.inc is current (' + cases.length + ' cases)'); }
  process.exit(0);
}
const out = pos[0] || path.join(ROOT, 'native/build-lxr-ref');
fs.mkdirSync(out, { recursive: true });
const f32 = (file, a) => fs.writeFileSync(path.join(out, file), Buffer.from(a.buffer, a.byteOffset, a.byteLength));
const index = [];
for (const c of cases) {
  rec.trig = null; rec.params = null; rec.raw = [];
  const o = L.xeng()[c.k].r(c.P, c.sr, c.hold, { len: c.vlen }, c.bpm);
  let nraw = 0; for (const b of rec.raw) nraw += b.length; const raw = new Float32Array(nraw); { let at = 0; for (const b of rec.raw) { raw.set(b, at); at += b.length; } }
  f32(c.name + '.f32', o); f32(c.name + '.raw.f32', raw);
  let pk = 0; for (const x of o) { const a = Math.abs(x); if (a > pk) pk = a; }
  index.push({ name: c.name, k: c.k, P: c.P, sr: c.sr, hold: c.hold, vlen: c.vlen, bpm: c.bpm, n: o.length, nraw, peak: pk, kit: rec.params, setBpm: rec.bpm, trig: rec.trig, sha: crypto.createHash('sha256').update(Buffer.from(o.buffer, o.byteOffset, o.byteLength)).digest('hex').slice(0, 16) });
}
const defaults = {}; for (const k of KINDS) defaults[k] = { d: xe[k].d(), len: L.len(k), tgt: ENG[k].tgt };
fs.writeFileSync(path.join(out, 'cases.json'), JSON.stringify({ defaults, wasm: WASM, wasmSha256: crypto.createHash('sha256').update(wasmBytes).digest('hex'), nparams: META.nparams, rate: META.rate, par: { velDest1: PN.VEL_DEST_1, targetLfo1: PN.TARGET_LFO1, voiceLfo1: PN.VOICE_LFO1 }, cases: index }));
console.log('wrote ' + index.length + ' cases to ' + out);
