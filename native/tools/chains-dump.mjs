// U31: THE REFERENCE OF THE …CHAINS ENGINE (exact, Node only): the real chains/chains-worklet.js run in Node.
//   node native/tools/chains-dump.mjs [outDir]   writes <outDir>/cases.json and <outDir>/<case>.f32 (every recorded array of the case, one after the other: the local sample-by-sample comparison)
//   node native/tools/chains-dump.mjs --golden   writes native/tests/golden/chains_cases.json (the cases, their parameters and the FNV-1a 32 hash of everything recorded): the gate on every platform
//   node native/tools/chains-dump.mjs --check    exits 1 if the committed file differs from what the JS gives now
// Three kinds of case:
//  mod   the 429 rows of tests/golden/chains-modules.json: tests/chains.test.js's renders, in ITS order on ONE shared engine (the seed counter, Math.random and the wasm instances are shared there, so the order matters)
//  proc  the 4 rows of chains-processor.json and chains-thunder-node.json (a fresh scope each)
//  scn   scenarios of this unit (buses, sends, sources, bypass, focus, a node's inputs, patch edits while playing): a fresh scope each, run through ChainsEngine or the registered 'thunder-chains' node
// Everything a case records is hashed as ONE stream: the arrays in the order the case lists them (see recorded()).
import fs from 'node:fs';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const require = createRequire(import.meta.url);
const H = require(path.join(ROOT, 'tests/helpers.js'));
const GOLD = path.join(ROOT, 'native/tests/golden/chains_cases.json');
const BYTES = new Uint8Array(fs.readFileSync(path.join(ROOT, 'chains/dsp/tfx.wasm')));
const PUMP = new Uint8Array(fs.readFileSync(path.join(ROOT, 'pump/pump.wasm')));
const BLOCKS = 400, TRANSPORT = { tempo: 120, lpb: 4, ticks: 6, playing: true };

function scope() { const ctx = H.freezeClock(H.workletScope()); H.load(ctx, 'chains/chains-worklet.js'); ctx.ChainsDSP.wasmInit({ tfxBytes: BYTES }); ctx.__pumpBytes = PUMP; return ctx; }
function engineOf(ctx, focus, tr) { return new ctx.ChainsDSP.ChainsEngine(48000, { focus, quality: 'full', transport: tr || TRANSPORT }); }
const SW = new Set(['mute']), KEEP_DEFAULT = new Set(['flip']);
const defaults = m => { const p = {}; for (const [k, d] of Object.entries(m.params || {})) p[k] = d[0]; return p; };
const stress = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (mx - mn === 1 && st === 1) { o[k] = mn; continue; } let v = mn + (mx - mn) * .7; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
const switchesOn = m => { const o = {}; for (const [k, d] of Object.entries(m.params || {})) { const [, mn, mx, st] = d; if (KEEP_DEFAULT.has(k)) { o[k] = d[0]; continue; } if (mx - mn === 1 && st === 1) { o[k] = SW.has(k) ? mn : mx; continue; } let v = mn + (mx - mn) * .3; if (st >= 1) v = Math.round((v - mn) / st) * st + mn; o[k] = v; } return o; };
const hashOf = arrs => H.hashFloats(arrs);
const cases = [], refs = {};   // refs: case name -> Float32Array[] (written to outDir)
const outDir = process.argv.find((a, i) => i > 1 && !a.startsWith('--'));

// ---- mod: tests/chains.test.js renderModule, in its order on one shared engine ----
{
  const ctx = scope(), E = engineOf(ctx, 0), M = ctx.ChainsDSP.MODULES; ctx.__pumpTypes = new Set(['ptube','psnap','pgate','pecho','pspring','pverb','pgrain','pvox','ptune','pcrush','pswirl','pstutter','ptwang']); const sig = H.testSignal(128 * BLOCKS);
  function render(type, over) {
    const m = M[type], params = {}; for (const [k, d] of Object.entries(m.params || {})) params[k] = d[0]; Object.assign(params, over);
    const aIn = Object.keys(m.inputs || {}).filter(n => m.inputs[n] === 'audio'), outs = Object.keys(m.outputs || {}), cab = [];
    if (aIn.includes('in')) cab.push({ from: ['a', 'out'], to: ['b', 'in'] }); if (aIn.includes('inR')) cab.push({ from: ['a', 'outR'], to: ['b', 'inR'] });
    const sink = ['in', 'inR']; outs.slice(0, 2).forEach((p, i) => cab.push({ from: ['b', p], to: ['c', sink[i]] }));
    const probe = m.probe === true; if (probe) cab.push({ from: ['a', 'out'], to: ['b', 'a'] }, { from: ['a', 'outR'], to: ['b', 'b'] });
    const patch = { modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type, scope: 'mono', params }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }], cables: cab };
    E.loadPatch(patch);
    if (probe) E.handleMessage({ op: 'telemetry', value: true });
    const ports = probe ? ['show'] : Object.keys(m.outputs || {}), rec = {}; for (const p of ports) rec[p] = new Float32Array(probe ? 3 + 512 : 128 * BLOCKS);
    const L = new Float32Array(128 * BLOCKS), R = new Float32Array(128 * BLOCKS);
    for (let b = 0; b < BLOCKS; b++) {
      const s = sig.subarray(b * 128, (b + 1) * 128); E.setTrackInputs(s, s); E.process(128);
      L.set(E.bus(1), b * 128); R.set(E.busR(1), b * 128);
      const io = E._ios.get('b'); if (io && !probe) for (const p of ports) rec[p].set(io.out[p], b * 128);
    }
    if (probe) { const sh = E.telemetry().shows.find(x => x[0] === 'b'); if (sh) rec.show.set(sh[1]); E.handleMessage({ op: 'telemetry', value: false }); }
    if (E.quality !== 'full') throw new Error('engine shed to draft quality while rendering ' + type);
    return { L, R, rec, ports, probe, params, patch };
  }
  const gold = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests/golden/chains-modules.json'), 'utf8'));
  for (const type of Object.keys(M).sort()) {
    const m = M[type]; if (!m || !m.process || type === 'trackIn' || type === 'chainOut') continue;
    for (const [set, over] of [['default', undefined], ['stressed', stress(m)], ['switches-on', switchesOn(m)]]) {
      const r = render(type, over), arrs = [r.L, r.R, ...r.ports.map(p => r.rec[p])], h = hashOf(arrs), key = `${type} @${set}`;
      if (gold[key] !== h) throw new Error(`the JS gives ${h} for "${key}", the committed golden has ${gold[key]}`);
      const lib = m.effect === true || type === 'marbles' || type === 'tides' || ctx.__pumpTypes.has(type);
      const c = { name: `mod/${type}/${set}`, kind: 'mod', lib, type, patch: r.patch, probe: r.probe, ports: r.ports, hash: h };
      if (r.probe) c.hashLR = hashOf([r.L, r.R]);
      cases.push(c); refs[c.name] = arrs;
    }
  }
  if (Object.keys(gold).length !== cases.length) throw new Error(`the committed golden has ${Object.keys(gold).length} rows, this makes ${cases.length}`);
}

// ---- proc: the two processors of tests/chains.test.js, hot and quiet ----
function hotPatch(gain) {
  return { modules: [{ id: 'a', type: 'trackIn', scope: 'mono', params: { track: 1 } }, { id: 'b', type: 'utility', scope: 'mono', params: { gain, pan: 0, mono: 0, flip: 0, mute: 0 } }, { id: 'c', type: 'chainOut', scope: 'mono', params: { track: 1, level: 1 } }],
    cables: [{ from: ['a', 'out'], to: ['b', 'in'] }, { from: ['a', 'outR'], to: ['b', 'inR'] }, { from: ['b', 'out'], to: ['c', 'in'] }, { from: ['b', 'outR'], to: ['c', 'inR'] }] };
}
function driveProcessor(name, gain, blocks = 120) {
  const ctx = scope(), Proc = ctx.__registered[name], p = new Proc({ processorOptions: { quality: 'full', track: 0, patch: name === 'thunder-chains' ? hotPatch(gain) : undefined } });
  if (name === 'chains') p.engine.loadPatch(hotPatch(gain));
  const sig = H.testSignal(128 * blocks), L = new Float32Array(128 * blocks), R = new Float32Array(128 * blocks);
  for (let b = 0; b < blocks; b++) {
    const s = sig.subarray(b * 128, (b + 1) * 128), out = [[new Float32Array(128), new Float32Array(128)]];
    if (name === 'chains') p.engine.setTrackInputs(s, s);
    p.process([[s, s], []], out, {}); L.set(out[0][0], b * 128); R.set(out[0][1], b * 128);
  }
  return [L, R];
}
{
  const g1 = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests/golden/chains-processor.json'), 'utf8')), g2 = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests/golden/chains-thunder-node.json'), 'utf8'));
  for (const [node, gold] of [['chains', g1], ['thunder-chains', g2]]) for (const [nm, gain] of [['quiet', 0], ['hot', 12]]) {
    const arrs = driveProcessor(node, gain), h = hashOf(arrs); if (gold[nm] !== h) throw new Error(`${node} ${nm}: the JS gives ${h}, the committed golden has ${gold[nm]}`);
    const c = { name: `proc/${node}/${nm}`, kind: 'proc', node, gain, blocks: 120, hash: h }; cases.push(c); refs[c.name] = arrs;
  }
}

// ---- scn: scenarios of this unit ----
// The inputs are made the same way on both sides: signal(seed) = H.testSignal(128 * blocks, 48000, seed) (the native test has the same generator), one stream per channel / tap / send / feed.
const P = (id, type, params, extra) => Object.assign({ id, type, scope: 'mono', params: params || {} }, extra || {}), C = (a, b, fb) => Object.assign({ from: a, to: b }, fb ? { fb: true } : {});
const TI = (id, t) => P(id, 'trackIn', { track: t }), CO = (id, t, level) => P(id, 'chainOut', { track: t, level: level == null ? 1 : level });
const scn = [];
const S = s => scn.push(Object.assign({ mode: 'engine', blocks: 90, chans: 2, taps: 0, tapmap: null, standalone: true, sc: false, sends: 0, feeds: 0, busmap: null, focus: -1, events: [], extra: {} }, s));
// buses: two chains to two outputs, one output fed by two (written in first-written order), levels
S({ name: 'bus/two-chains', patch: { modules: [TI('a', 1), P('g', 'vca', { gain: .5 }), CO('c', 1), TI('b', 2), CO('d', 2, .7), CO('e', 1, .3)], cables: [C(['a', 'out'], ['g', 'in']), C(['a', 'outR'], ['g', 'inR']), C(['g', 'out'], ['c', 'in']), C(['g', 'outR'], ['c', 'inR']), C(['b', 'out'], ['d', 'in']), C(['b', 'outR'], ['d', 'inR']), C(['b', 'out'], ['e', 'in'])] } });
S({ name: 'bus/order-written', patch: { modules: [CO('z', 5), TI('a', 1), CO('y', 3), CO('x', 5, .5)], cables: [C(['a', 'out'], ['y', 'in']), C(['a', 'out'], ['z', 'in']), C(['a', 'out'], ['x', 'in'])] } });
// a stereo chain: Track In -> VCF -> VCA (cv from an LFO) -> mixer -> Chain Out, with a parameter cable
S({ name: 'chain/vcf-vca-lfo', patch: { modules: [TI('a', 1), P('f', 'vcf', { cutoff: .4, res: .5 }), P('l', 'lfo', { rate: 3, depth: .8 }), P('g', 'vca', { gain: 1 }), CO('c', 1)], cables: [C(['a', 'out'], ['f', 'in']), C(['l', 'out'], ['f', 'p:cutoff']), C(['f', 'out'], ['g', 'in']), C(['l', 'out'], ['g', 'cv']), C(['g', 'out'], ['c', 'in']), C(['g', 'outR'], ['c', 'inR'])] } });
S({ name: 'chain/old-patch-stereo-up', patch: { modules: [TI('a', 1), P('f', 'vcf', { cutoff: .6 }), P('g', 'vca', {}), CO('c', 1)], cables: [C(['a', 'out'], ['f', 'in']), C(['f', 'out'], ['g', 'in']), C(['g', 'out'], ['c', 'in'])] } });
S({ name: 'chain/ti2-set', patch: { ti2: true, st2: true, modules: [TI('a', 1), P('f', 'vcf', { cutoff: .6 }), CO('c', 1)], cables: [C(['a', 'out'], ['f', 'in']), C(['f', 'out'], ['c', 'in'])] } });
S({ name: 'chain/trackIn-out-of-range', deviates: 'the JS reads past the host buffer and makes NaN; the native engine makes silence', chans: 1, patch: { modules: [TI('a', 3), CO('c', 1)], cables: [C(['a', 'out'], ['c', 'in'])] } });
S({ name: 'chain/any-cable-any-input', patch: { modules: [TI('a', 1), P('l', 'lfo', { rate: 2 }), P('g', 'vca', {}), CO('c', 1)], cables: [C(['l', 'out'], ['g', 'in']), C(['a', 'out'], ['g', 'cv']), C(['g', 'out'], ['c', 'inR']), C(['a', 'out'], ['c', 'in'])] } });
S({ name: 'chain/feedback', patch: { modules: [TI('a', 1), P('g', 'vca', { gain: .6 }), P('t', 'atten', { amount: .5 }), CO('c', 1)], cables: [C(['a', 'out'], ['g', 'in']), C(['g', 'out'], ['t', 'in']), C(['t', 'out'], ['g', 'in'], true), C(['g', 'out'], ['c', 'in'])] } });
// sends: Sidechain Out writes a send, Sidechain In reads a send (a block late, standalone) or a track's own sound (a tap)
S({ name: 'send/out-in-standalone', patch: { modules: [TI('a', 1), P('o', 'scout', { snd: 2, level: 1 }), P('i', 'scin', { sck: 2, gain: 1 }), P('g', 'vca', {}), CO('c', 1)], cables: [C(['a', 'out'], ['o', 'in']), C(['a', 'out'], ['g', 'in']), C(['i', 'out'], ['g', 'cv']), C(['g', 'out'], ['c', 'in'])] } });
S({ name: 'send/comp-key-send', patch: { modules: [TI('a', 1), P('o', 'scout', { snd: 1 }), P('k', 'sccomp', { sck: 1, thresh: .3, ratio: 6 }), CO('c', 1)], cables: [C(['a', 'out'], ['o', 'in']), C(['a', 'out'], ['k', 'in']), C(['k', 'out'], ['c', 'in']), C(['k', 'outR'], ['c', 'inR'])] } });
S({ name: 'send/comp-key-patched', patch: { modules: [TI('a', 1), TI('b', 2), P('k', 'sccomp', { thresh: .2, ratio: 8 }), CO('c', 1)], cables: [C(['a', 'out'], ['k', 'in']), C(['b', 'out'], ['k', 'sc']), C(['k', 'out'], ['c', 'in'])] } });
S({ name: 'src/tap-track', taps: 3, patch: { modules: [TI('a', 1), P('i', 'scin', { sck: 9 + 1 }), P('g', 'vca', {}), CO('c', 1)], cables: [C(['a', 'out'], ['g', 'in']), C(['i', 'out'], ['g', 'cv']), C(['g', 'out'], ['c', 'in'])] }, events: [[20, 'tapmap', [4, 1, 0]], [50, 'tapmap', [0, 1]]] });
S({ name: 'src/old-scin-snd-src', taps: 3, patch: { modules: [TI('a', 1), P('o', 'scout', { snd: 3 }), P('i', 'scin', { snd: 3, src: 2 }), P('j', 'scin', { src: 2 }), P('g', 'vca', {}), CO('c', 1)], cables: [C(['a', 'out'], ['o', 'in']), C(['a', 'out'], ['g', 'in']), C(['i', 'out'], ['g', 'cv']), C(['j', 'out'], ['g', 'cv']), C(['g', 'out'], ['c', 'in'])] } });
// bypass: the patch's own _byp, the module numbers, the tracker's switches (a one-block crossfade)
S({ name: 'byp/own-and-switch', patch: { modules: [TI('a', 1), P('f', 'vcf', { cutoff: .3, _byp: 1 }), P('g', 'vca', { gain: .5, _bn: 7 }), P('h', 'vca', { gain: .25 }), CO('c', 1)], cables: [C(['a', 'out'], ['f', 'in']), C(['f', 'out'], ['g', 'in']), C(['g', 'out'], ['h', 'in']), C(['h', 'out'], ['c', 'in'])] },
  events: [[10, 'bsw', 1, 1], [25, 'bsw', 1, 2], [40, 'bsw', 7, 1], [55, 'bsw', 1, -1], [60, 'param', 'f', '_byp', 0], [70, 'param', 'h', '_bn', 7], [80, 'bsw', 7, 0]] });
S({ name: 'byp/many-modules', patch: { modules: [TI('a', 1), ...Array.from({ length: 18 }, (_, i) => P('m' + i, 'vca', { gain: .9, ...(i % 5 === 0 ? { _bn: 3 } : {}) })), CO('c', 1)], cables: [C(['a', 'out'], ['m0', 'in']), ...Array.from({ length: 17 }, (_, i) => C(['m' + i, 'out'], ['m' + (i + 1), 'in'])), C(['m17', 'out'], ['c', 'in'])] },
  events: [[5, 'bsw', 3, 1], [15, 'bsw', 15, 1], [25, 'bsw', 2, 1], [35, 'bsw', 3, -1]] });
// focus: a track's node runs only the modules that lead to its Chain Out (and a Sidechain Out of the lowest track of its chain)
for (const f of [0, 1, 2]) S({ name: `focus/${f}`, focus: f, chans: 3, patch: { modules: [TI('a', 1), TI('b', 2), TI('d', 3), P('o', 'scout', { snd: 1 }), P('g', 'vca', { gain: .5 }), CO('c', 1), CO('e', 2), CO('h', 3), P('x', 'vca', { gain: .2 })], cables: [C(['a', 'out'], ['g', 'in']), C(['g', 'out'], ['c', 'in']), C(['g', 'out'], ['o', 'in']), C(['b', 'out'], ['e', 'in']), C(['d', 'out'], ['x', 'in']), C(['x', 'out'], ['h', 'in']), C(['x', 'out'], ['o', 'in'])] } });
S({ name: 'focus/telemetry', focus: 1, chans: 2, patch: { modules: [TI('a', 1), TI('b', 2), P('i', 'scin', { sck: 1 }), P('g', 'vca', {}), CO('c', 2), CO('e', 1)], cables: [C(['b', 'out'], ['c', 'in']), C(['a', 'out'], ['g', 'in']), C(['g', 'out'], ['e', 'in'])] }, events: [[10, 'telemetry', true], [50, 'telemetry', false]] });
S({ name: 'focus/track-param', focus: 0, chans: 2, patch: { modules: [TI('a', 1), TI('b', 2), CO('c', 1), CO('e', 2)], cables: [C(['a', 'out'], ['c', 'in']), C(['b', 'out'], ['e', 'in'])] }, events: [[30, 'param', 'c', 'track', 2], [60, 'param', 'e', 'track', 1]] });
// patch edits while playing, the protocol's ops, a new patch replacing the old
S({ name: 'edit/live', patch: { modules: [TI('a', 1), P('g', 'vca', { gain: .5 }), CO('c', 1)], cables: [C(['a', 'out'], ['g', 'in']), C(['g', 'out'], ['c', 'in'])] },
  events: [[10, 'add', 'f', 'vcf', { cutoff: .3 }], [12, 'connect', 'g', 'out', 'f', 'in'], [14, 'connect', 'f', 'out', 'c', 'inR'], [30, 'param', 'g', 'gain', .9], [40, 'disconnect', 'g', 'out', 'c', 'in'], [50, 'remove', 'f'], [60, 'add', 'k', 'sccomp', { thresh: .4 }], [61, 'connect', 'a', 'out', 'k', 'in'], [62, 'connect', 'k', 'out', 'c', 'in'], [70, 'params', [['k', 'ratio', 7], ['g', 'gain', .2]]]] });
S({ name: 'edit/patch-replaced', patch: { modules: [TI('a', 1), CO('c', 1)], cables: [C(['a', 'out'], ['c', 'in'])] },
  events: [[30, 'patch', { modules: [TI('a', 1), P('f', 'vcf', { cutoff: .35 }), CO('c', 1)], cables: [C(['a', 'out'], ['f', 'in']), C(['f', 'out'], ['c', 'in'])], transport: { tempo: 90, playing: true } }]] });
S({ name: 'edit/transport', patch: { modules: [TI('a', 1), P('l', 'lfo', { rate: 4, sync: 1 }), P('g', 'vca', {}), CO('c', 1)], cables: [C(['a', 'out'], ['g', 'in']), C(['l', 'out'], ['g', 'cv']), C(['g', 'out'], ['c', 'in'])] }, events: [[20, 'transport', { playing: false }], [40, 'transport', { playing: true, tempo: 133 }], [60, 'quality', 'draft']] });
// found by the mutation check: parts no row of chains-modules.json reaches (the Vocoder's spectral mode, a compressor ratio below 2, an unlinked modulator)
S({ name: 'mod2/vocoder-spectral', patch: { modules: [TI('a', 1), TI('b', 2), P('v', 'vocoder', { mode: 6, mix: .8, level: 1.2 }), CO('c', 1)], cables: [C(['a', 'out'], ['v', 'in']), C(['b', 'out'], ['v', 'inR']), C(['v', 'out'], ['c', 'in'])] } });
S({ name: 'mod2/vocoder-spectral-unlinked', patch: { modules: [TI('a', 1), P('v', 'vocoder', { mode: 6, mix: 1 }), CO('c', 1)], cables: [C(['a', 'out'], ['v', 'in']), C(['v', 'out'], ['c', 'in'])] } });
S({ name: 'mod2/comp-low-ratio', patch: { modules: [TI('a', 1), P('k', 'sccomp', { threshold: .1, ratio: 1.4, mix: .9 }), CO('c', 1)], cables: [C(['a', 'out'], ['k', 'in']), C(['k', 'out'], ['c', 'in'])] } });
// the node: ThunderChains (track, bus feeds, the sidechain input, taps, sends in and out)
const nodeS = s => S(Object.assign({ mode: 'node', track: 0, standalone: false }, s));
nodeS({ name: 'node/basic', track: 0, patch: { modules: [TI('a', 1), P('g', 'vca', { gain: .6 }), CO('c', 1), CO('d', 2)], cables: [C(['a', 'out'], ['g', 'in']), C(['g', 'out'], ['c', 'in']), C(['g', 'out'], ['d', 'in'])] } });
nodeS({ name: 'node/track2-bus', track: 2, feeds: 2, busmap: [0, 1, 2, 3], taps: 0, patch: { modules: [TI('a', 1), TI('b', 2), TI('d', 3), P('m', 'smixer', {}), CO('c', 3)], cables: [C(['a', 'out'], ['m', 'in1']), C(['b', 'out'], ['m', 'in2']), C(['d', 'out'], ['m', 'in3']), C(['m', 'out'], ['c', 'in'])] } });
nodeS({ name: 'node/sends-in-out', track: 0, sends: 8, taps: 3, sc: true, tapmap: [0, 1, 2], patch: { modules: [TI('a', 1), P('o', 'scout', { snd: 3 }), P('i', 'scin', { sck: 4 }), P('j', 'scin', { sck: 10 }), P('g', 'vca', {}), CO('c', 1)], cables: [C(['a', 'out'], ['o', 'in']), C(['a', 'out'], ['g', 'in']), C(['i', 'out'], ['g', 'cv']), C(['j', 'out'], ['g', 'cv']), C(['g', 'out'], ['c', 'in'])] } });
nodeS({ name: 'node/long-block', track: 0, blockLen: 300, blocks: 30, patch: { modules: [TI('a', 1), P('f', 'vcf', { cutoff: .5 }), CO('c', 1)], cables: [C(['a', 'out'], ['f', 'in']), C(['f', 'out'], ['c', 'in'])] } });
nodeS({ name: 'node/byp-params', track: 0, patch: { modules: [TI('a', 1), P('g', 'vca', { gain: .5 }), CO('c', 1)], cables: [C(['a', 'out'], ['g', 'in']), C(['g', 'out'], ['c', 'in'])] }, events: [[10, 'bsw', 1, 1], [30, 'bsw', 1, 2], [50, 'bsw', 1, 0]] });
nodeS({ name: 'node/huge-block', track: 0, blockLen: 5000, blocks: 3, patch: { modules: [TI('a', 1), CO('c', 1)], cables: [C(['a', 'out'], ['c', 'in'])] } });

const sigOf = (n, seed) => H.testSignal(n, 48000, seed);
function runScn(c) {
  const ctx = scope(), D = ctx.ChainsDSP, B = c.blocks, N = c.blockLen || 128, rec = [];
  const chan = j => sigOf(N * B, 12345 + 77 * j), tapS = j => sigOf(N * B, 5000 + j), sendS = k => sigOf(N * B, 7000 + k), feedS = j => sigOf(N * B, 9000 + j), scS = sigOf(N * B, 4242);
  const apply = (E, ev, bsw) => {
    const [, op, ...a] = ev;
    if (op === 'param') E.setParam(a[0], a[1], a[2]); else if (op === 'add') E.addModule(a[0], a[1], { params: a[2] }); else if (op === 'remove') E.removeModule(a[0]);
    else if (op === 'connect') E.connect([a[0], a[1]], [a[2], a[3]], !!a[4]); else if (op === 'disconnect') E.disconnect([a[0], a[1]], [a[2], a[3]]);
    else if (op === 'params') E.handleMessage({ op: 'params', list: a[0] }); else if (op === 'patch') E.loadPatch(a[0]); else if (op === 'transport') E.handleMessage({ op: 'transport', value: a[0] });
    else if (op === 'quality') E.handleMessage({ op: 'quality', value: a[0] }); else if (op === 'telemetry') E.handleMessage({ op: 'telemetry', value: a[0] }); else if (op === 'tapmap') E.handleMessage({ op: 'tapmap', value: a[0] });
    else if (op === 'bsw') bsw(a[0], a[1]);
  };
  if (c.mode === 'engine') {
    const E = engineOf(ctx, c.focus); E.loadPatch(c.patch); if (c.tapmap) E.handleMessage({ op: 'tapmap', value: c.tapmap });
    const chs = Array.from({ length: c.chans }, (_, j) => chan(j)), taps = Array.from({ length: c.taps }, (_, j) => tapS(j));
    const mixL = new Float32Array(128 * B), mixR = new Float32Array(128 * B), snd = Array.from({ length: 8 }, () => new Float32Array(128 * B));
    let sendPrev = null;
    for (let b = 0; b < B; b++) {
      for (const ev of c.events) if (ev[0] === b) apply(E, ev, (n, v) => { E.bsw[n] = v; });
      const tl = new Float32Array(128 * c.chans), tr = new Float32Array(128 * c.chans);
      for (let j = 0; j < c.chans; j++) { tl.set(chs[j].subarray(b * 128, (b + 1) * 128), j * 128); tr.set(chs[(j + 1) % c.chans].subarray(b * 128, (b + 1) * 128), j * 128); }
      E.setTrackInputs(tl, tr); E.taps = c.taps ? taps.map(t => t.subarray(b * 128, (b + 1) * 128)) : null;
      E.process(128);
      if (c.standalone) { if (!E._sendPrev) { E._sendPrev = new Float32Array(8 * 128); E.sendIn = Array.from({ length: 8 }, (_, n) => E._sendPrev.subarray(n * 128, (n + 1) * 128)); } E._sendPrev.set(E.sendOut); }
      const L = new Float32Array(128), R = new Float32Array(128);
      for (const k of E._bwl) for (let i = 0; i < 128; i++) { L[i] += E.busesL[k * 128 + i]; R[i] += E.busesR[k * 128 + i]; }
      const soft = x => { const a = Math.abs(x); return a < .8 ? x : Math.sign(x) * (.8 + .2 * Math.tanh((a - .8) / .2)); };
      for (let i = 0; i < 128; i++) { L[i] = soft(L[i]); R[i] = soft(R[i]); }
      mixL.set(L, b * 128); mixR.set(R, b * 128); for (let k = 0; k < 8; k++) snd[k].set(E.sendOut.subarray(k * 128, (k + 1) * 128), b * 128);
    }
    rec.push(mixL, mixR, ...snd);
  } else {
    const o = { quality: 'full', track: c.track, patch: c.patch }; if (c.busmap) o.busMap = c.busmap; if (c.tapmap) o.tapMap = c.tapmap;
    const node = new ctx.__registered['thunder-chains']({ processorOptions: o }), E = node.engine;
    const inS = chan(0), scL = scS, feeds = Array.from({ length: c.feeds * 2 }, (_, j) => feedS(j)), taps = Array.from({ length: c.taps }, (_, j) => tapS(j)), sends = Array.from({ length: c.sends }, (_, k) => sendS(k));
    const oL = new Float32Array(N * B), oR = new Float32Array(N * B), so = Array.from({ length: 8 }, () => new Float32Array(N * B));
    const params = {}; let bsw = null;
    for (let b = 0; b < B; b++) {
      for (const ev of c.events) if (ev[0] === b) apply(node.engine, ev, (n, v) => { (bsw = bsw || {})['b' + n] = [v]; });
      const sl = s => s.subarray(b * N, (b + 1) * N);
      const ins = [[sl(inS), sl(inS)], c.sc ? [sl(scL), sl(scL)] : [], c.taps ? taps.map(sl) : [], c.sends ? sends.map(sl) : [], [], []];
      for (let j = 0; j < c.feeds; j++) { const g = ins[4 + (j >> 4)]; g[(j & 15) * 2] = sl(feeds[2 * j]); g[(j & 15) * 2 + 1] = sl(feeds[2 * j + 1]); }
      const outs = [[new Float32Array(N), new Float32Array(N)], Array.from({ length: 8 }, () => new Float32Array(N))];
      node.process(ins, outs, Object.assign({}, bsw)); oL.set(outs[0][0], b * N); oR.set(outs[0][1], b * N); for (let k = 0; k < 8; k++) so[k].set(outs[1][k], b * N);
    }
    rec.push(oL, oR, ...so);
  }
  return rec;
}
for (const c of scn) { const arrs = runScn(c), h = hashOf(arrs); const k = Object.assign({}, c, { kind: 'scn', hash: h }); delete k.extra; cases.push(k); refs[c.name] = arrs; }

const text = JSON.stringify(cases) + '\n';
if (outDir) {
  fs.mkdirSync(outDir, { recursive: true });
  for (const c of cases) { if (c.kind === 'mod' && !c.lib) continue; const a = refs[c.name], n = a.reduce((s, x) => s + x.length, 0), b = Buffer.alloc(n * 4); let o = 0; for (const x of a) { Buffer.from(x.buffer, x.byteOffset, x.byteLength).copy(b, o); o += x.byteLength; } fs.writeFileSync(path.join(outDir, c.name.replace(/[\/@]/g, '_') + '.f32'), b); }
  fs.writeFileSync(path.join(outDir, 'cases.json'), text);
}
if (process.argv.includes('--golden')) { fs.writeFileSync(GOLD, text); console.log('wrote', GOLD, cases.length, 'cases'); }
else if (process.argv.includes('--check')) { const cur = fs.existsSync(GOLD) ? fs.readFileSync(GOLD, 'utf8') : ''; if (cur !== text) { console.error('chains_cases.json differs from the JS'); process.exit(1); } console.log('chains_cases.json matches the JS'); }
console.log(cases.length, 'cases:', cases.filter(c => c.kind === 'mod').length, 'mod,', cases.filter(c => c.kind === 'proc').length, 'proc,', cases.filter(c => c.kind === 'scn').length, 'scn');
