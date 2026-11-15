// EFFECTS THAT REST (build 606; the owner's CPU items of 14 Nov, measured on his song `acid`):
//  1. a switched-OFF pump effect still cost its plugin's whole process call (pump MASTER about 8% of the audio thread). Its OFF is exactly
//     "the input, late by the latency", so ThunderFx now plays that delay itself and leaves the plugin alone (pumpRest);
//  2. a muted track's effects kept running. The node now hears silence while its track cannot be heard, and the sleep rules take it from there;
//  3. the node reports each slot's share of the time, for the CPU panel.
// The real ThunderFx (cut out of index.html, as idle-shortcut.test.js does) with the real pump.wasm and tfx.wasm, in Node. Needs acorn.
'use strict';
const test = require('node:test'), assert = require('node:assert/strict'), vm = require('vm'), fs = require('fs'), path = require('path');
const H = require('./helpers');
const HTML = H.read('index.html'), SR = 48000, B = 128;
const ACORN = ['/opt/npm-tools/node_modules', path.join(H.ROOT, 'node_modules'), '/usr/lib/node_modules', '/usr/local/lib/node_modules'].find(d => fs.existsSync(path.join(d, 'acorn')));
const skip = { skip: !ACORN && 'needs acorn' };
const WK = ['rvMix', 'DegradeCore', 'VSampCore', 'ResonCore', 'PitchFxCore', 'clkSpb', 'FlexCore', 'StepGateCore', 'RetrigCore', 'beatLock', 'bkHash', 'GrainsCore', 'ScrambleCore', 'GlitchCore'];
let SRC = null;
function tfxSrc(){
  if (SRC) return SRC;
  const h = HTML, acorn = require(path.join(ACORN, 'acorn')), i0 = h.indexOf('<script>\n/* ===ENGINE START=== */'), s0 = i0 + 9, src = h.slice(s0, h.indexOf('\n</script>', s0));
  const ast = acorn.parse(src, { ecmaVersion: 'latest', sourceType: 'script', allowReturnOutsideFunction: true }), body = [...ast.body];
  for (const st of ast.body) { const c = st.type === 'ExpressionStatement' && st.expression.type === 'CallExpression' && st.expression.callee; if (c && c.body && c.body.body && c.end - c.start > 200000) body.push(...c.body.body); }
  const by = new Map(); for (const st of body) if ((st.type === 'FunctionDeclaration' || st.type === 'ClassDeclaration') && !by.has(st.id.name)) by.set(st.id.name, src.slice(st.start, st.end));
  const a = h.indexOf('const TFX_SRC = `') + 'const TFX_SRC = `'.length, b = h.indexOf('`;\nlet tfxBytes', a);
  return SRC = WK.map(n => by.get(n)).join('\n') + '\n' + new Function('return `' + h.slice(a, b) + '`')();
}
const meta = vm.createContext({}); meta.globalThis = meta; vm.runInContext(H.read('pump/pump-meta.js'), meta);
const PM = meta.PUMP_META, TFXWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'chains/dsp/tfx.wasm'))), PUMPWASM = new Uint8Array(fs.readFileSync(path.join(H.ROOT, 'pump/pump.wasm')));
// A pump slot as the page describes it to the node (pumpChain in index.html)
const pslot = (type, on, slot = 0, over = {}) => { const fx = PM.fx[type]; return { key: slot + ':p' + type, pt: PM.types.indexOf(type), pv: fx.params.map(p => over[p.s] ?? p.def), bi: fx.params.findIndex(p => p.s === 'bypass'), on, slot, sck: 0 }; };
// A node with its own clock (the worklet's currentTime), fresh pump module per node
function mkNode(chain){
  const reg = {}, clk = {}, posts = [];
  delete globalThis.__pumpX; delete globalThis.__pumpMod; delete globalThis.__pumpBytes;
  new Function('AudioWorkletProcessor', 'registerProcessor', 'sampleRate', '__clk', 'let currentTime = 0; __clk.set = v => { currentTime = v; };\n' + tfxSrc())(class { constructor(){ this.port = { postMessage: m => posts.push(m), onmessage: null }; } }, (n, c) => { reg[n] = c; }, SR, clk);
  const node = new reg['thunder-tfx']({ processorOptions: { bytes: TFXWASM, pump: PUMPWASM, chain } });
  let blk = 0, sd = 12345; const rnd = () => { sd = (sd * 1664525 + 1013904223) >>> 0; return sd / 4294967296 - 0.5; };
  const P = sw => { const p = { bp: new Float32Array(B).fill(-1), flx: Float32Array.of(-1), gat: Float32Array.of(-1), rtg: Float32Array.of(-1) }; for (let k = 0; k < 8; k++) p['sw' + k] = Float32Array.of(sw && sw[k] != null ? sw[k] : -1); return p; };
  const inp = [], out = [];
  const step = (kind, sw) => { clk.set(blk * B / SR); const a = new Float32Array(B); if (kind !== 0) for (let k = 0; k < B; k++) a[k] = Math.fround(0.25 * Math.sin((blk * B + k) * 0.031) + 0.2 * rnd());
    const o = [new Float32Array(B), new Float32Array(B)]; node.process([[a, a], [], []], [o], P(sw)); inp.push(a); out.push(o[0]); blk++; };
  const flat = A => { const f = new Float32Array(A.length * B); A.forEach((a, i) => f.set(a, i * B)); return f; };
  return { node, posts, step, msg: m => node.port.onmessage({ data: m }), inp: () => flat(inp), out: () => flat(out), blocks: () => blk };
}
const count = (node, name) => { let c = 0; const f = node[name]; node[name] = function (...a){ c++; return f.apply(this, a); }; return () => c; };
const secs = s => Math.ceil(s * SR / B);

test('a switched-OFF pump effect rests: the plugin is no longer run, and the output is still the input late by its latency, to the sample', skip, () => {
  for (const type of ['calm', 'master', 'comp']){
    const N = mkNode([pslot(type, false)]), runs = count(N.node, 'pumpRun'), s = N.node.slots[0];
    for (let b = 0; b < secs(1); b++) N.step(1);
    const r1 = runs(); assert.equal(s.rs, 1, type + ': resting after a second of sound'); assert.ok(r1 < secs(0.45), type + ': the plugin ran only while it was being checked (' + r1 + ' blocks)');
    for (let b = 0; b < secs(1); b++) N.step(1);
    assert.equal(runs(), r1, type + ': not run at all while it rests');
    const lat = s.rd.n, x = N.inp(), y = N.out(); let worst = 0;
    for (let i = lat + 2400; i < y.length; i++) worst = Math.max(worst, Math.abs(y[i] - x[i - lat]));   // (the plugin's own start: a new one fades into its bypass over 15 ms)
    assert.ok(worst <= 1e-6, type + ': input late by ' + lat + ' samples all the way through, before and after it rests (worst ' + worst + ')');
    if (type === 'calm') assert.equal(lat, 2048);
  }
});
test('switching it ON wakes it: a fresh plugin hears the sound first, the output has no step, and then the effect is working', skip, () => {
  const N = mkNode([pslot('comp', false, 0, { threshold: -40 })]), s = N.node.slots[0], runs = count(N.node, 'pumpRun');
  for (let b = 0; b < secs(1); b++) N.step(1);
  assert.equal(s.rs, 1); const r0 = runs(), at = N.blocks();
  N.msg([pslot('comp', true, 0, { threshold: -40 })]);
  N.step(1); assert.equal(s.rs, 2, 'waking'); assert.ok(runs() > r0, 'the plugin hears the sound again');
  for (let b = 0; b < secs(0.5); b++) N.step(1);
  assert.equal(s.rs, 0, 'and has taken over'); assert.equal(s.byp, false, 'switched on');
  const x = N.inp(), y = N.out(), w0 = at * B, w1 = w0 + 1024;   // (COMP has no latency: the warm-up is 1024 samples)
  for (let i = w0; i < w1; i++) assert.ok(Math.abs(y[i] - x[i]) <= 1e-6, 'still the dry sound while it warms up');
  let jump = 0, g0 = 1, lo = 9; for (let i = w0 - 64; i < w1 + 8192; i++){ if (Math.abs(x[i]) < 0.05) continue; const g = y[i] / x[i]; jump = Math.max(jump, Math.abs(g - g0)); g0 = g; lo = Math.min(lo, g); }
  assert.ok(jump < 0.05 && lo > 0.02 && lo < 0.9, 'the compressor’s gain comes in smoothly, no step in the sound: biggest move ' + jump + ' a sample, down to ' + lo);
  let diff = 0; for (let i = y.length - 4096; i < y.length; i++) diff = Math.max(diff, Math.abs(y[i] - x[i])); assert.ok(diff > 1e-3, 'the compressor is working at the end');
  // against a node whose slot never rested: the same sound once both are on
  const M = mkNode([pslot('comp', false, 0, { threshold: -40 })]); M.node.slots[0].fxo = true;
  for (let b = 0; b < secs(1); b++) M.step(1); assert.ok(!M.node.slots[0].rs, 'an FXO-held slot never rests');
  M.msg([pslot('comp', true, 0, { threshold: -40 })]); for (let b = 0; b < secs(0.5) + 1; b++) M.step(1);
  const z = M.out(); let d2 = 0, pk = 0; for (let i = z.length - 4096; i < z.length; i++){ d2 = Math.max(d2, Math.abs(z[i] - y[i])); pk = Math.max(pk, Math.abs(z[i])); }
  assert.ok(d2 < 0.02 * pk + 1e-4, 'half a second on, it sounds as the never-rested one does (worst ' + d2 + ', peak ' + pk + ')');
});
test('it does not rest when an FXO switch holds it, on silence alone the first time, or when the effect has no bypass; a change of its values wakes it', skip, () => {
  const F = mkNode([pslot('calm', true)]), fr = count(F.node, 'pumpRun');
  for (let b = 0; b < secs(1); b++) F.step(1, [0]);   // FXO holds slot 0 off
  assert.equal(fr(), secs(1), 'held OFF by FXO: the plugin keeps running, so the next FXO lands on the beat'); assert.ok(!F.node.slots[0].rs);
  const Z = mkNode([pslot('calm', false)]); for (let b = 0; b < secs(1); b++) Z.step(0); assert.ok(!Z.node.slots[0].rs, 'silence proves nothing about the bypass: not resting yet');
  const M = mkNode([pslot('meter', false)]); for (let b = 0; b < secs(0.5); b++) M.step(1); assert.ok(!M.node.slots[0].rs, 'METER has no bypass: it is simply skipped when OFF');
  const C = mkNode([pslot('tone', false)]); for (let b = 0; b < secs(1); b++) C.step(1); assert.equal(C.node.slots[0].rs, 1);
  C.msg([pslot('tone', false)]); C.step(1); assert.equal(C.node.slots[0].rs, 1, 'the same values again (another slot was edited): still resting');
  const p = PM.fx.tone.params.find(q => q.s !== 'bypass' && q.max > q.min), ch = {}; ch[p.s] = p.def === p.max ? p.min : p.max;
  C.msg([pslot('tone', false, 0, ch)]); C.step(1); assert.equal(C.node.slots[0].rs, 2, 'new values: woken to take them (its latency may have changed)');
  for (let b = 0; b < secs(1); b++) C.step(1); assert.equal(C.node.slots[0].rs, 1, 'and back to rest');
  const x = C.inp(), y = C.out(); let worst = 0; for (let i = 2048 + 4 * B; i < y.length; i++) worst = Math.max(worst, Math.abs(y[i] - x[i - 2048])); assert.ok(worst <= 1e-6, 'the timing never moved: ' + worst);
});
test('a muted track: the node hears silence a tenth of a second after the mute, its echoes die, it sleeps; unmuted, the sound is back in the next block', skip, () => {
  const N = mkNode([pslot('echo', true)]), work = count(N.node, 'slotRun');
  for (let b = 0; b < secs(0.5); b++) N.step(1);
  N.msg({ rest: true }); const m0 = N.blocks();
  for (let b = 0; b < secs(0.05); b++) N.step(1);
  { const y = N.out(); let pk = 0; for (let i = y.length - B; i < y.length; i++) pk = Math.max(pk, Math.abs(y[i])); assert.ok(pk > 0.05, 'the mute fade is still running: the node has not let go yet'); }
  for (let b = 0; b < secs(40); b++) N.step(1);   // the track keeps playing into a muted node
  const w0 = work(); for (let b = 0; b < 200; b++) N.step(1);
  assert.equal(work(), w0, 'asleep: no effect is run although the track is still sounding');
  { const y = N.out(); for (let i = y.length - 200 * B; i < y.length; i++) assert.equal(y[i], 0); }
  N.msg({ rest: false }); N.step(1);
  assert.ok(work() > w0, 'unmuted: running in the very next block');
  { const y = N.out(); let pk = 0; for (let i = y.length - B; i < y.length; i++) pk = Math.max(pk, Math.abs(y[i])); assert.ok(pk > 0.05, 'and the sound is there'); }
  assert.ok(m0 > 0);
});
test('the node reports each slot’s share of the time with its load', skip, () => {
  const N = mkNode([pslot('comp', true, 0), pslot('calm', true, 1)]);
  const real = Date.now; let fake = 1000; Date.now = () => (fake += 1);   // every look at the clock is a millisecond later
  try { for (let b = 0; b < secs(0.6); b++) N.step(1); } finally { Date.now = real; }
  const m = N.posts.filter(p => p && typeof p.load === 'number' && p.sl).pop(); assert.ok(m, 'a load report with slots');
  assert.deepEqual(m.sl.map(r => r[0]), [0, 1], 'both slots, by slot number'); for (const r of m.sl) assert.ok(r[1] > 0 && r[1] <= m.load + 1e-9, 'a share of the node’s load');
});
test('the page: applyMix tells a track’s effects node when the track cannot be heard, once per change, live only', () => {
  const cut = (re, what) => { const m = HTML.match(re); assert.ok(m, 'index.html: could not find ' + what); return m[0]; };
  const f = cut(/function fxRest\(t, on\)\{[^\n]*\n/, 'fxRest');
  const run = live => { const sent = [], buses = [{ fx: { port: { postMessage: m => sent.push(m) } } }, {}]; const c = vm.createContext({ live, buses, NT: 32 }); vm.runInContext(f + 'this.fxRest = fxRest;', c); return { c, sent }; };
  const a = run(true); a.c.fxRest(0, false); a.c.fxRest(0, false); assert.equal(JSON.stringify(a.sent), '[{"rest":true}]', 'muted: told once');
  a.c.fxRest(0, true); assert.equal(JSON.stringify(a.sent[1]), '{"rest":false}'); a.c.fxRest(1, false); assert.equal(a.sent.length, 2, 'a track with no effects node: nothing');
  const r = run(false); r.c.fxRest(0, false); assert.equal(r.sent.length, 0, 'a render never rests a node');
  assert.match(HTML, /pv = clamp\(\(tpv - 128\) \/ 127, -1, 1\);[^\n]*\n\s*if \(typeof fxRest === 'function'\) fxRest\(t, on\);/, 'applyMix calls it for every track');
  assert.match(HTML, /if \(live && b\.fx && t < NT\) fxRest\(t, audible\(t\)\);/, 'and routeTrack for a node made while its track is muted');
});
test('the CPU panel names the dearest effects of a row', () => {
  const m = HTML.match(/function cpuFxTop\(A, sl, nameOf\)\{[\s\S]*?\n\}\n/); assert.ok(m, 'index.html: could not find cpuFxTop');
  const c = vm.createContext({ Math }); vm.runInContext(m[0] + 'this.f = cpuFxTop;', c);
  const A = [{ type: 'spring', on: true }, null, { type: 'pmaster', on: false }, { type: 'sdelay', on: true }], nm = k => k.toUpperCase();
  assert.equal(c.f(A, new Map([[0, 0.31], [2, 0.08], [3, 0.02]]), nm), ': SPRING 31%, PMASTER (off) 8%', 'the two dearest, with their share; a switched-off one says so');
  assert.equal(c.f(A, new Map([[3, 0.004]]), nm), '', 'under half a percent: nothing to name');
  assert.equal(c.f(A, new Map([[1, 0.2]]), nm), '', 'a slot that has gone: nothing');
  assert.equal(c.f(A, null, nm), ''); assert.equal(c.f(null, new Map([[0, 1]]), nm), '');
  assert.match(HTML, /'Effects · ' \+ tn\(t\) \+ cpuFxTop\(S\.tfx\[t\], b\.fx\.sl, fxNameOf\)/); assert.match(HTML, /'Master effects' \+ cpuFxTop\(S\.mfx, rt\.mfx\.sl, fxNameOf\)/);
});
