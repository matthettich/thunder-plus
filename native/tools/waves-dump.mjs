// U30: THE REFERENCE OF THE …WAVES ENGINE (exact, Node only): the real waves/waves-worklet.js run in Node, one FRESH context (so one fresh wasm instance and one fresh Math.random per case).
//   node native/tools/waves-dump.mjs [outDir]    writes <outDir>/cases.json (the cases and their hashes) and <outDir>/<case>.f32 (L then R of the bus, for the local sample-by-sample comparison)
//   node native/tools/waves-dump.mjs --golden    writes native/tests/golden/waves_cases.json (the cases and the FNV-1a 32 hash of [L, R] of every one): the gate on every platform
//   node native/tools/waves-dump.mjs --check     exits 1 if the committed file differs from what the JS gives now
// A case: {name, sr, maxVoices, seed (or null), patch, blocks, events: [[block, op, ...args]], bus, telemetry}. Ops: noteOn p v, noteOff p, param id name v, connect/disconnect a0 a1 b0 b1, allOff.
// Rendered as tests/waves.test.js renders (notes at block 0 and 70). The patches are the ones of that test (patchFor), five more "factory-like" ones made here, and the feedback ones.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
const H = require(path.join(ROOT, 'tests/helpers.js'));
const GOLD = path.join(ROOT, 'native/tests/golden/waves_cases.json');
const TFX = new Uint8Array(fs.readFileSync(path.join(ROOT, 'waves/dsp/tfx.wasm'))), DSP = new Uint8Array(fs.readFileSync(path.join(ROOT, 'waves/dsp/thunder-dsp.wasm')));
const tfxMod = new WebAssembly.Module(TFX), dspMod = new WebAssembly.Module(DSP);
const TRANSPORT = { tempo: 120, lpb: 4, ticks: 6, playing: true };

function engineCtx() {
  const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js');
  ctx.WavesDSP.wasmInit({ dspModule: dspMod, tfxModule: tfxMod });
  return ctx.WavesDSP;
}
const D0 = engineCtx();
const defaults = m => { const p = {}; for (const [k, d] of Object.entries(m.params || {})) p[k] = d[0]; return p; };
const SW = new Set(['mute']), KEEP_DEFAULT = new Set(['flip']);
const stress = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (mx - mn === 1 && st === 1) { o[k] = mn; continue; } let v = mn + (mx - mn) * .7; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
const switchesOn = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (KEEP_DEFAULT.has(k)) { o[k] = d[0]; continue; } if (mx - mn === 1 && st === 1) { o[k] = SW.has(k) ? mn : mx; continue; } let v = mn + (mx - mn) * .3; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
// patchFor: copied from tests/waves.test.js
function patchFor(M, type, params) {
  const m = M[type], ins = Object.keys(m.inputs || {}), outs = Object.keys(m.outputs || {}), poly = m.scope === 'poly', aIn = ins.filter(n => m.inputs[n] === 'audio');
  const mods = [{ id: 'h', type: 'hostIn', scope: 'poly', params: { transpose: 0 } }], cab = [];
  const vco = () => { mods.push({ id: 'v', type: 'vco', scope: 'poly', params: { wave: 1, tune: 0, fine: 0 } }); cab.push({ from: ['h', 'pitch'], to: ['v', 'pitch'] }); };
  const env = () => { mods.push({ id: 'e', type: 'adsr', scope: 'poly', params: { a: .005, d: .2, s: .7, r: .3 } }); cab.push({ from: ['h', 'gate'], to: ['e', 'gate'] }); };
  const amp = () => mods.push({ id: 'g', type: 'vca', scope: 'poly', params: { gain: 1 } }, { id: 'o', type: 'out', scope: 'mono', params: { slot: 1, level: 1 } });
  if (poly) {
    env(); amp();
    if (['adsr', 'vca', 'atten', 'mixer'].includes(type)) {
      vco(); mods.push({ id: 'b', type, scope: 'poly', params });
      if (type === 'adsr') cab.push({ from: ['h', 'gate'], to: ['b', 'gate'] }, { from: ['v', 'out'], to: ['g', 'in'] }, { from: ['b', 'out'], to: ['g', 'cv'] });
      else if (type === 'vca') cab.push({ from: ['v', 'out'], to: ['b', 'in'] }, { from: ['e', 'out'], to: ['b', 'cv'] }, { from: ['b', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
      else cab.push({ from: ['v', 'out'], to: ['b', 'in'] }, { from: ['b', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
    } else {
      mods.push({ id: 'b', type, scope: 'poly', params });
      if (ins.includes('pitch')) cab.push({ from: ['h', 'pitch'], to: ['b', 'pitch'] }); if (ins.includes('gate')) cab.push({ from: ['h', 'gate'], to: ['b', 'gate'] });
      if (aIn.length) { vco(); cab.push({ from: ['v', 'out'], to: ['b', aIn[0]] }); }
      cab.push({ from: ['b', outs.find(n => m.outputs[n] === 'audio') || outs[0]], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
    }
    cab.push({ from: ['g', 'out'], to: ['o', 'in'] });
  } else {
    vco(); env(); amp(); mods.push({ id: 'b', type, scope: 'mono', params });
    cab.push({ from: ['v', 'out'], to: ['g', 'in'] }, { from: ['e', 'out'], to: ['g', 'cv'] });
    if (aIn.includes('in')) cab.push({ from: ['g', 'out'], to: ['b', 'in'] }); if (aIn.includes('inR')) cab.push({ from: ['g', 'out'], to: ['b', 'inR'] });
    outs.slice(0, 2).forEach((p, i) => cab.push({ from: ['b', p], to: ['o', ['in', 'inR'][i]] }));
    if (!aIn.length) cab.push({ from: ['g', 'out'], to: ['o', 'in'] });
    if (m.probe === true) cab.push({ from: ['g', 'out'], to: ['b', 'a'] }, { from: ['e', 'out'], to: ['b', 'b'] });
  }
  return { modules: mods, cables: cab };
}
const NOTES = [[0, 'noteOn', 60, 1], [0, 'noteOn', 67, .7], [70, 'noteOff', 60]];
const cases = [];
const add = c => cases.push(Object.assign({ sr: 48000, maxVoices: 4, seed: 7, blocks: 120, events: NOTES, bus: 1, telemetry: false }, c));
const SKIP = new Set(['hostIn', 'out']);
for (const type of Object.keys(D0.MODULES).sort()) {
  const m = D0.MODULES[type]; if (!m || !m.process || SKIP.has(type)) continue;
  const tel = m.probe === true;
  add({ name: `mod/${type}/default`, patch: patchFor(D0.MODULES, type, defaults(m)), telemetry: tel });
  add({ name: `mod/${type}/stressed`, patch: patchFor(D0.MODULES, type, Object.assign(defaults(m), stress(m))), telemetry: tel });
  add({ name: `mod/${type}/switches`, patch: patchFor(D0.MODULES, type, Object.assign(defaults(m), switchesOn(m))), telemetry: tel });
  for (const sr of [44100, 96000]) add({ name: `mod/${type}/default@${sr}`, sr, patch: patchFor(D0.MODULES, type, defaults(m)), telemetry: tel });
}
// "factory-like" patches: a subtractive voice with an effect chain, a Plaits voice, an FM voice into a reverb, an additive voice into a delay, and a sequenced one
const P = (id, type, params) => ({ id, type, params: params || {} }), C = (a, b, fb) => Object.assign({ from: a, to: b }, fb ? { fb: true } : {});
const voice = (extra, tailCab, finalFrom) => ({ modules: [P('h', 'hostIn'), P('v', 'vco', { wave: 0 }), P('f', 'vcf', { cutoff: .55, res: .45 }), P('e', 'adsr'), P('g', 'vca'), ...extra, P('o', 'out')],
  cables: [C(['h', 'pitch'], ['v', 'pitch']), C(['h', 'gate'], ['e', 'gate']), C(['v', 'out'], ['f', 'in']), C(['e', 'out'], ['f', 'cutoff']), C(['f', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), ...tailCab, C(finalFrom, ['o', 'in'])] });
const FACT = {
  'sub-chorus-reverb': voice([P('c', 'chorus'), P('r', 'reverb', { mix: .3 })], [C(['g', 'out'], ['c', 'in']), C(['c', 'out'], ['r', 'in']), C(['c', 'outR'], ['r', 'inR'])], ['r', 'out']),
  'sub-delay': voice([P('d', 'delay', { time: .3, mix: .35 })], [C(['g', 'out'], ['d', 'in'])], ['d', 'out']),
  'plaits': { modules: [P('h', 'hostIn'), P('p', 'plaits', { engine: 2 }), P('e', 'adsr'), P('g', 'vca'), P('o', 'out')], cables: [C(['h', 'pitch'], ['p', 'pitch']), C(['h', 'gate'], ['p', 'gate']), C(['h', 'gate'], ['e', 'gate']), C(['p', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), C(['g', 'out'], ['o', 'in'])] },
  'fm-lfo-space': { modules: [P('h', 'hostIn'), P('m', 'fm', { model: 2, index: .5 }), P('l', 'lfo', { rate: 3, depth: .4 }), P('e', 'adsr'), P('g', 'vca'), P('s', 'space'), P('o', 'out')], cables: [C(['h', 'pitch'], ['m', 'pitch']), C(['h', 'gate'], ['e', 'gate']), C(['l', 'out'], ['m', 'index']), C(['m', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), C(['g', 'out'], ['s', 'in']), C(['s', 'out'], ['o', 'in']), C(['s', 'outR'], ['o', 'inR'])] },
  'additive-string-mixer': { modules: [P('h', 'hostIn'), P('a', 'additive', { model: 3 }), P('k', 'string'), P('x', 'mixer'), P('e', 'adsr'), P('g', 'vca'), P('o', 'out')], cables: [C(['h', 'pitch'], ['a', 'pitch']), C(['h', 'gate'], ['a', 'gate']), C(['h', 'pitch'], ['k', 'pitch']), C(['h', 'gate'], ['k', 'gate']), C(['h', 'gate'], ['e', 'gate']), C(['a', 'out'], ['x', 'in1']), C(['k', 'out'], ['x', 'in2']), C(['x', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), C(['g', 'out'], ['o', 'in'])] },
  'seq-noise-param-cable': { modules: [P('q', 'seq', { run: 1, rate: 2 }), P('h', 'hostIn'), P('v', 'vco'), P('n', 'noise', { model: 4 }), P('r', 'random', { rate: 6 }), P('e', 'adsr'), P('g', 'vca'), P('o', 'out')], cables: [C(['h', 'pitch'], ['v', 'pitch']), C(['h', 'gate'], ['e', 'gate']), C(['r', 'out'], ['v', 'p:tune']), C(['v', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), C(['g', 'out'], ['o', 'in'])] },
  'expr': { modules: [P('h', 'hostIn'), P('v', 'vco'), P('x', 'expr', { k1: 2 }), P('g', 'vca'), P('o', 'out')], cables: [C(['h', 'pitch'], ['v', 'pitch']), C(['v', 'out'], ['x', 'a']), C(['x', 'out'], ['g', 'in']), C(['g', 'out'], ['o', 'in'])] },
};
FACT['expr'].modules[2].text = 'tanh(a * k1) * 0.5 + sin(t * 440 * 2 * pi) * 0.1';
for (const [name, patch] of Object.entries(FACT)) for (const sr of [44100, 48000, 96000]) add({ name: `factory/${name}@${sr}`, sr, patch, blocks: 200, events: [[0, 'noteOn', 60, 1], [0, 'noteOn', 64, .8], [30, 'noteOn', 67, 1], [90, 'noteOff', 60], [100, 'noteOff', 64], [110, 'noteOff', 67]] });
// feedback (tests/feedback.test.js shapes): a loop through a fb cable, one quieter than 1 and one louder (the guard), a feedback cable out of a poly module into a mono one, and into a voice
const fbPatch = g => ({ modules: [P('h', 'hostIn'), P('v', 'vco'), P('e', 'adsr'), P('g', 'vca'), P('a', 'atten', { amount: g }), P('o', 'out')],
  cables: [C(['h', 'pitch'], ['v', 'pitch']), C(['h', 'gate'], ['e', 'gate']), C(['v', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), C(['g', 'out'], ['a', 'in']), C(['a', 'out'], ['g', 'in'], true), C(['g', 'out'], ['o', 'in'])] });
for (const g of [.5, 1, .9]) add({ name: `fb/loop${g}`, patch: fbPatch(g), blocks: 80 });
add({ name: 'fb/mono-to-voice', patch: { modules: [P('h', 'hostIn'), P('v', 'vco'), P('e', 'adsr'), P('g', 'vca'), P('m', 'mixer'), P('o', 'out')], cables: [C(['h', 'pitch'], ['v', 'pitch']), C(['h', 'gate'], ['e', 'gate']), C(['v', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), C(['g', 'out'], ['m', 'in1']), C(['m', 'out'], ['o', 'in']), C(['m', 'out'], ['g', 'in'], true)] }, blocks: 80 });
// patch edits while playing (add / connect / param / disconnect events)
add({ name: 'edit/live', patch: FACT['sub-delay'], blocks: 160, events: [[0, 'noteOn', 60, 1], [20, 'param', 'v', 'wave', 2], [30, 'connect', 'e', 'out', 'v', 'p:fine'], [50, 'disconnect', 'e', 'out', 'f', 'cutoff'], [60, 'param', 'o', 'slot', 2], [80, 'noteOn', 62, 1], [100, 'allOff']] });

// ---- run one case in a fresh context ----
function run(c) {
  const D = engineCtx();
  const opts = { maxVoices: c.maxVoices, quality: 'full', transport: TRANSPORT }; if (c.seed != null) opts.seed = c.seed;
  const E = new D.WavesEngine(c.sr, opts); E.loadPatch(c.patch);
  if (c.telemetry) E.handleMessage({ op: 'telemetry', value: true });
  const L = new Float32Array(128 * c.blocks), R = new Float32Array(128 * c.blocks);
  for (let b = 0; b < c.blocks; b++) {
    for (const ev of c.events) if (ev[0] === b) {
      const [, op, ...a] = ev;
      if (op === 'noteOn') E.noteOn(a[0], a[1]); else if (op === 'noteOff') E.noteOff(a[0]); else if (op === 'allOff') E.allNotesOff();
      else if (op === 'param') E.setParam(a[0], a[1], a[2]); else if (op === 'connect') E.connect([a[0], a[1]], [a[2], a[3]], !!a[4]); else if (op === 'disconnect') E.disconnect([a[0], a[1]], [a[2], a[3]]);
    }
    E.process(128); L.set(E.bus(c.bus), b * 128); R.set(E.busR(c.bus), b * 128);
  }
  if (E.quality !== 'full') throw new Error('shed to draft: ' + c.name);
  return { L, R };
}
const out = []; const outDir = process.argv.find((a, i) => i > 1 && !a.startsWith('--'));
if (outDir) fs.mkdirSync(outDir, { recursive: true });
for (const c of cases) {
  let r; try { r = run(c); } catch (e) { out.push(Object.assign({}, c, { hash: 'throws', error: String(e && e.message || e) })); continue; }
  const h = H.hashFloats([r.L, r.R]);
  out.push(Object.assign({}, c, { hash: h }));
  if (outDir) { const b = Buffer.alloc(r.L.length * 8); Buffer.from(r.L.buffer).copy(b, 0); Buffer.from(r.R.buffer).copy(b, r.L.length * 4); fs.writeFileSync(path.join(outDir, c.name.replace(/[\/@]/g, '_') + '.f32'), b); }
}
// ---- the page's own renders (index.html wavesPre / wavesLiveRender), run as they are ----
const HTML = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
const a0 = HTML.indexOf('function wavesDoc(){'), a1 = HTML.indexOf('/* PWM sampler'); if (a0 < 0 || a1 < a0) throw new Error('waves-dump: index.html no longer has wavesDoc / wavesPre / wavesLiveRender where this tool looks');
const PAGE_SRC = HTML.slice(a0, a1);
function pageRender(kind, patch, args) {
  const ctx = H.freezeClock(H.pageScope()); H.load(ctx, 'waves/waves-worklet.js'); ctx.WavesDSP.wasmInit({ dspModule: dspMod, tfxModule: tfxMod });
  ctx.console = { warn() {}, log() {} };
  vm.runInContext('var S = {bpm: 120}; const WV = { ready:false, doc:null, frame:null };' + PAGE_SRC + ';globalThis.__page = { wavesPre, wavesLiveRender, WV };', ctx);
  const pg = ctx.__page; pg.WV.doc = patch; ctx.S.bpm = args.tempo || 120;
  if (kind === 'pre') { const r = pg.wavesPre({ wBus: args.bus, tune: args.tune, fine: args.fine }, { tune: args.vtune, gate: args.gate }, args.dur, args.pA, args.ptau); return r ? r.data : null; }
  return pg.wavesLiveRender({ bus: args.bus, tune: args.tune, level: args.level, drive: args.drive, vel: args.vel }, args.sr, args.hold, {}, args.bpm);
}
const REND = []; const EXACT_PATCHES = { basic: patchFor(D0.MODULES, 'vca', defaults(D0.MODULES.vca)), 'sub-delay': FACT['sub-delay'], 'seq-noise-param-cable': FACT['seq-noise-param-cable'], expr: FACT['expr'], 'additive-string-mixer': FACT['additive-string-mixer'], 'fm-lfo': { modules: FACT['fm-lfo-space'].modules.filter(m => m.type !== 'space').map(m => m), cables: [] } };
EXACT_PATCHES['fm-lfo'] = { modules: [P('h', 'hostIn'), P('m', 'fm', { model: 2, index: .5 }), P('l', 'lfo', { rate: 3, depth: .4 }), P('e', 'adsr'), P('g', 'vca'), P('o', 'out', { slot: 3 })], cables: [C(['h', 'pitch'], ['m', 'pitch']), C(['h', 'gate'], ['e', 'gate']), C(['l', 'out'], ['m', 'index']), C(['m', 'out'], ['g', 'in']), C(['e', 'out'], ['g', 'cv']), C(['g', 'out'], ['o', 'in']), C(['g', 'out'], ['o', 'inR'])] };
for (const [name, patch] of Object.entries(EXACT_PATCHES)) {
  const bus = name === 'fm-lfo' ? 3 : 1;
  for (const args of [{ bus, tune: 0, fine: 0, vtune: 0, gate: 250, dur: 1, pA: 0, ptau: .05, tempo: 120 }, { bus, tune: 7, fine: 13, vtune: -2, gate: 80, dur: 1.5, pA: 5, ptau: .03, tempo: 97 }]) {
    const d = pageRender('pre', patch, args); REND.push({ name: `pre/${name}/${args.tune}`, kind: 'pre', patch, args, len: d ? d.length : 0, hash: d ? H.hashFloats([d]) : 'none' });
  }
  for (const args of [{ bus, tune: 0, level: 0, drive: 0, vel: 100, hold: 0.4, bpm: 120, sr: 48000 }, { bus, tune: -5, level: -6, drive: .6, vel: 60, hold: 0, bpm: 90, sr: 44100 }, { bus, tune: 3, level: 3, drive: 0, vel: 127, hold: 0.2, bpm: 120, sr: 96000 }]) {
    const d = pageRender('live', patch, args); REND.push({ name: `live/${name}/${args.sr}/${args.tune}`, kind: 'live', patch, args, len: d.length, hash: H.hashFloats([d]) });
  }
}
const RGOLD = path.join(ROOT, 'native/tests/golden/waves_render.json'), rtext = JSON.stringify(REND) + '\n';
if (process.argv.includes('--golden')) fs.writeFileSync(RGOLD, rtext);
else if (process.argv.includes('--check')) { if ((fs.existsSync(RGOLD) ? fs.readFileSync(RGOLD, 'utf8') : '') !== rtext) { console.error('waves_render.json differs from the JS'); process.exit(1); } }
const text = JSON.stringify(out) + '\n';
if (process.argv.includes('--golden')) { fs.writeFileSync(GOLD, text); console.log('wrote', GOLD, out.length, 'cases'); }
else if (process.argv.includes('--check')) { const cur = fs.existsSync(GOLD) ? fs.readFileSync(GOLD, 'utf8') : ''; if (cur !== text) { console.error('waves_cases.json differs from the JS'); process.exit(1); } console.log('waves_cases.json matches the JS'); }
else if (outDir) fs.writeFileSync(path.join(outDir, 'cases.json'), text);
console.log(out.length, 'cases,', out.filter(o => o.hash === 'throws').length, 'throw');
