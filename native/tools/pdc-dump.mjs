// U10: reference for pdc_plan (core/src/mix/pdc.cpp). Runs the web app's own pdcPlan, pdcModeOf, pumpSlotLat and pumpChainLatency (grabbed
// from index.html, never retyped) and the core's busPlan in a Node `vm`, on hand-made songs and on every song in sounds/, and writes what
// they give: the plan's T, d, f, c per track (seconds, doubles, exactly).
//   node native/tools/pdc-dump.mjs [outDir]   writes <outDir>/pdc.json (an array of cases: id, S {chainOn, chainPatch}, mode, aset, slots, want)
//   node native/tools/pdc-dump.mjs --golden   writes native/tests/golden/pdc_golden.inc (FNV-1a 64 over each case's `want` JSON)
//   node native/tools/pdc-dump.mjs --check    exits 1 if the committed pdc_golden.inc differs
// What the page supplies to the plan is stubbed by the case: S.tfx[t] is a list of {type, on, lat}; pumpLatencyOf returns lat (the real one asks
// pump.wasm, which is the caller's side in C++: U18); isPumpT is true for every type; pumpIdx[type].bypass is null for METER only.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';
import { ROOT, NT, LAW_SRC, patch, fnv } from './mix-dump.mjs';

const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8').replace(/\r\n/g, '\n');
const grab = re => { const m = html.match(re); if (!m) throw new Error('index.html: not found: ' + re); return m[0]; };
const PDC_SRC = [grab(/^const pumpSlotLat = .*$/m), grab(/^const pumpChainLatency = .*$/m), grab(/^function pdcModeOf\(\)\{.*$/m), grab(/^function pdcPlan\(sr\)\{[\s\S]*?\n\}/m)].join('\n');
const STUBS = `let aset = {}; const isPumpT = t => true, TFX = new Proxy({}, { get: (_, k) => ({ pump: k }) }), pumpIdx = new Proxy({}, { get: (_, k) => ({ bypass: k === 'METER' ? null : 0 }) }), pumpLatencyOf = (f, sr) => f.lat;`;

function makeVm() {
  const ctx = vm.createContext({ console });
  vm.runInContext(LAW_SRC + '\n' + STUBS + '\n' + PDC_SRC + '\n;globalThis.__u10 = { pdcPlan, pdcModeOf, setS(s, a){ S = s; aset = a; busPlanH = null; busPlanVal = null; } };', ctx, { filename: 'pdc-ref.js' });
  return ctx.__u10;
}
const U = makeVm();

const on = (...ts) => Array.from({ length: NT }, (_, i) => ts.includes(i + 1));
const SR = 48000;
const sl = (type, isOn, lat) => ({ type, on: isOn, lat });
// a case: patch, chainOn (1-based tracks), tfx {track (0-based): [slots]}, aset, sr
const bus46 = { patch: patch([['trackIn', 2], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), chainOn: on(2, 3, 4) };
const CASES = {
  none: {},
  none_bus: { ...bus46 },
  one_track: { tfx: { 2: [sl('LIMIT', true, 2048)] } },
  two_tracks: { tfx: { 0: [sl('LIMIT', true, 2048)], 5: [sl('TONE', true, 700), sl('CALM', true, 100)] } },
  off_counted: { tfx: { 1: [sl('LIMIT', false, 1000), sl('METER', false, 500), sl('CALM', true, 300), sl('METER', true, 40)] } },
  off_only_meter: { tfx: { 1: [sl('METER', false, 500)] } },
  bus_fx: { ...bus46, tfx: { 3: [sl('LIMIT', true, 1500)] } },
  bus_src_fx: { ...bus46, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  two_into_bus: { ...bus46, tfx: { 1: [sl('TONE', true, 800)], 2: [sl('LIMIT', true, 2000)], 3: [sl('CALM', true, 300)], 5: [sl('LIMIT', true, 100)] } },
  master_slot: { tfx: { 32: [sl('LIMIT', true, 4000)], 31: [sl('LIMIT', true, 64)] } },   // NT: the master's slots are not in S.tfx[t < NT]
  send: { patch: patch([['trackIn', 4], ['chainOut', 4], ['trackIn', 5], ['chainOut', 5], ['chainOut', 6]], [[0, 1], [2, 3], [0, 4], [2, 4]]), chainOn: on(4, 5, 6), tfx: { 3: [sl('LIMIT', true, 900)], 5: [sl('CALM', true, 250)] } },
  pre_feed: { patch: patch([['trackIn', 2, 1], ['trackIn', 3], ['chainOut', 4]], [[0, 2], [1, 2]]), chainOn: on(2, 3, 4), tfx: { 1: [sl('LIMIT', true, 1200)], 3: [sl('TONE', true, 50)] } },
  two_buses: { patch: patch([['trackIn', 1], ['trackIn', 2], ['chainOut', 7], ['trackIn', 3], ['trackIn', 4], ['chainOut', 8]], [[0, 2], [1, 2], [3, 5], [4, 5]]), chainOn: on(1, 2, 3, 4, 7, 8), tfx: { 0: [sl('LIMIT', true, 3000)], 6: [sl('LIMIT', true, 77)], 7: [sl('LIMIT', true, 4454)], 2: [sl('CALM', true, 10)] } },
  big: { tfx: { 0: [sl('LIMIT', true, 30000)], 1: [sl('LIMIT', true, 30000), sl('LIMIT', true, 30000)] } },
  odd_sr: { sr: 44100, tfx: { 0: [sl('LIMIT', true, 4454)], 1: [sl('LIMIT', true, 1023)], 2: [sl('TONE', true, 333)] } },
  tiny: { tfx: { 0: [sl('LIMIT', true, 0.5e-9 * SR)] } },   // a latency under 1e-9 s: c gets no entry
  mode_off: { ...bus46, aset: { pdcMode: 'off' }, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  mode_on: { ...bus46, aset: { pdcMode: 'on' }, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  mode_auto: { ...bus46, aset: { pdcMode: 'auto' }, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  mode_unset: { ...bus46, aset: {}, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  mode_legacy_on: { ...bus46, aset: { pdc: true }, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  mode_legacy_off: { ...bus46, aset: { pdc: false }, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  mode_junk: { ...bus46, aset: { pdcMode: 'maybe', pdc: true }, tfx: { 1: [sl('LIMIT', true, 2048)] } },
  mode_off_legacy_on: { aset: { pdcMode: 'off', pdc: true }, tfx: { 1: [sl('LIMIT', true, 2048)] } },
};

const song = (C) => ({ chainOn: C.chainOn || new Array(NT).fill(false), chainPatch: C.patch || null, mix: Array.from({ length: NT }, () => ({ vol: 224, trim: 128, pan: 128, mute: false, solo: false })), bpm: 120, insts: [],
  tfx: Array.from({ length: NT }, (_, t) => (C.tfx || {})[t] || []), mfx: [] });
function run(id, C, S0) {
  const S = S0 || song(C), aset = C.aset || {}, sr = C.sr || SR;
  U.setS(S, aset);
  const mode = U.pdcModeOf(), P = U.pdcPlan(sr);
  const arr = m => Array.from({ length: NT + 1 }, (_, t) => m.get(t) || 0);
  // the slots a caller reports: every tfx slot of tracks 0..NT-1, and what the page would put under track NT (no such list: kept to show it is ignored)
  const slots = []; const T = C.tfx || {}; for (const k of Object.keys(T)) T[k].forEach((f, i) => slots.push({ track: +k, slot: i, on: f.on, bypass: f.type !== 'METER', latency: f.lat }));
  return { id, sr, mode, aset, S: { chainOn: S.chainOn, chainPatch: S.chainPatch }, slots, want: { on: P.on, T: P.T, d: arr(P.d), f: arr(P.f), c: arr(P.c) } };
}
export function pdcCases() {
  const out = Object.entries(CASES).map(([id, C]) => run(id, C));
  for (const fn of fs.readdirSync(path.join(ROOT, 'sounds')).filter(x => x.endsWith('.thunder')).sort()) {
    const S = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds', fn), 'utf8')).song;
    const base = { chainOn: S.chainOn, chainPatch: S.chainPatch };
    out.push(run('file:' + fn, {}, Object.assign({}, base, { tfx: Array.from({ length: NT }, () => []), mfx: [] })));
    const all = new Array(NT).fill(true), lat = {}; for (let t = 0; t < NT; t++) lat[t] = [sl('LIMIT', true, 1000 + 37 * t)];
    out.push(run('file:' + fn + ':all-on-lat', { tfx: lat }, { chainOn: all, chainPatch: S.chainPatch, tfx: Array.from({ length: NT }, (_, t) => lat[t]), mfx: [] }));
  }
  return out;
}

const hex = h => '0x' + h.toString(16).padStart(16, '0') + 'ull';
const a = process.argv.slice(2), cases = pdcCases();
const inc = ['// GENERATED by native/tools/pdc-dump.mjs (--golden) from the web app\'s pdcPlan. Do not edit: a mismatch means the port is wrong.',
  '// FNV-1a 64 over the JSON of what the JS plan gives (T, d, f, c in seconds) for every case.',
  'struct PdcGolden { const char* name; unsigned long long hash; int floats; };',
  'static const PdcGolden kPdcGolden[] = {', ...cases.map(c => `  {"${c.id}", ${hex(fnv(Buffer.from(JSON.stringify(c.want))))}, 0},`), '};', `static const int kPdcGoldenN = ${cases.length};`, ''].join('\n');
const incPath = path.join(ROOT, 'native/tests/golden/pdc_golden.inc');
if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
else if (a[0] === '--check') { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('pdc_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('pdc_golden.inc is current'); }
else { const dir = a[0] || '/tmp/u10/pdc-ref'; fs.mkdirSync(dir, { recursive: true }); fs.writeFileSync(path.join(dir, 'pdc.json'), JSON.stringify(cases)); console.log('wrote ' + cases.length + ' pdc cases to ' + dir); }
