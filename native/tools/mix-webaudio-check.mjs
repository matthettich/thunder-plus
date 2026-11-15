// U09: checks the JS model of the page's mixer (mix-dump.mjs, class Model) against REAL Web Audio.
// For every reference A case with Web Audio's block size, headless Chrome builds the page's strip out of real nodes in an
// OfflineAudioContext (as makePlayer does: inp, dry, ins, g, cg, StereoPanner, a 0 s delay, pre as an explicit stereo pair, splitters and
// a 32-channel merger for a bus's sources, out, mo) and books the commands with the page player's own AudioParam calls (trackTo,
// 'mvol', 'mout', applyMix's 12 ms setTargetAtTime on every track): a different formulation from the model's (the core's lanes
// added to the nodes' params), which is what the port follows, so the two agreeing says both the node arithmetic and the lanes are right.
// Prints the difference per case in dB below the case's peak. Not run by ctest (needs Chrome + playwright); the result of the run made
// when the model was written is in native/tests/golden/mix-webaudio-check.txt.
//   node native/tools/mix-webaudio-check.mjs        (PLAYWRIGHT=/path/index.mjs CHROME=/path/chrome if not found)
import fs from 'node:fs';
import { pathToFileURL } from 'node:url';
import { CASES, CASE_NAMES, runCase, songOf, inputsOf, LAW_SRC, SR, NT } from './mix-dump.mjs';

const PW = process.env.PLAYWRIGHT || ['/opt/npm-tools/node_modules/playwright/index.mjs'].find(f => fs.existsSync(f));
const CHROME = process.env.CHROME || ['/usr/bin/google-chrome', '/usr/bin/chromium', '/usr/bin/chromium-browser'].find(f => fs.existsSync(f));
if (!PW || !CHROME) { console.error('needs playwright and Chrome (PLAYWRIGHT=, CHROME=)'); process.exit(2); }
const { chromium } = await import(pathToFileURL(PW).href);
const browser = await chromium.launch({ executablePath: CHROME });
const page = await browser.newPage();
await page.goto('about:blank');
await page.addScriptTag({ content: LAW_SRC + '\nwindow.__laws = { setS(s){ S = s; busPlanH = null; busPlanVal = null; } };' });

// In the page: the graph and the page player's calls.
async function inPage({ C, S0, ins, SR, NT, n }) {
  __laws.setS(S0); // eslint-disable-line no-undef
  const oc = new OfflineAudioContext(2, n, SR), out = oc.createGain(), mo = oc.createGain();
  out.gain.value = curve(S.master); mo.gain.value = curve(S.mout == null ? 255 : S.mout); out.connect(mo); mo.connect(oc.destination);
  const buses = [], P = busPlan(), isChn = t => chainsFeed(t) && !!S.chainPatch && S.chainPatch.format === 'chains.patch';
  for (let t = 0; t <= NT; t++) {
    const inp = oc.createGain(), dry = oc.createGain(), ins = oc.createGain(), g = oc.createGain(), cg = oc.createGain(), p = oc.createStereoPanner(), pre = oc.createGain(), dl = oc.createDelay(1);
    pre.channelCount = 2; pre.channelCountMode = 'explicit'; pre.channelInterpretation = 'speakers'; dl.delayTime.value = 0;
    inp.connect(dry); dry.connect(ins); ins.connect(g); g.connect(cg); cg.connect(p); p.connect(dl); dl.connect(out); ins.connect(pre);
    buses.push({ inp, dry, ins, g, cg, p, pre, dl, chn: null });
  }
  // the stand-in chains node: the track's own input and every source's stereo pair, 1:1 (inp -> chn -> wet -> ins, dry 0)
  for (let t = 0; t < NT; t++) if (isChn(t)) { const b = buses[t]; b.chn = oc.createGain(); b.wet = oc.createGain(); b.inp.connect(b.chn); b.chn.connect(b.wet); b.wet.connect(b.ins); b.dry.gain.value = 0; }
  // busSync
  const routed = new Set();
  for (const [bt, src] of P.feeds) { const b = buses[bt]; if (!b.chn) continue;
    const m = oc.createChannelMerger(32), sp = oc.createChannelSplitter(32), m2 = oc.createChannelMerger(2);
    src.forEach((t, j) => { const q = buses[t]; let s;
      if (P.pre.has(t)) { if (!q.splP) { q.splP = oc.createChannelSplitter(2); q.fdlP = oc.createDelay(1); q.fdlP.delayTime.value = 0; q.pre.connect(q.fdlP); q.fdlP.connect(q.splP); } s = q.splP; }
      else { if (!q.spl) { q.spl = oc.createChannelSplitter(2); q.fdl = oc.createDelay(1); q.fdl.delayTime.value = 0; q.p.connect(q.fdl); q.fdl.connect(q.spl); } s = q.spl; }
      s.connect(m, 0, j * 2); s.connect(m, 1, j * 2 + 1); sp.connect(m2, j * 2, 0); sp.connect(m2, j * 2 + 1, 1);
      if (P.pure.has(t)) routed.add(t); });
    m.connect(sp); m2.connect(b.wet); }
  for (const t of routed) buses[t].dl.disconnect(out);
  // the inputs
  C.ins.forEach((q, k) => { const ab = oc.createBuffer(q.mono ? 1 : 2, n, SR); ab.copyToChannel(new Float32Array(ins[k][0]), 0); if (!q.mono) ab.copyToChannel(new Float32Array(ins[k][1]), 1);
    const s = oc.createBufferSource(); s.buffer = ab; s.connect(buses[q.t].inp); s.start(0); });
  // the page's state and calls
  const tvlOv = new Map(), tpnOv = new Map(); let mvol = null;
  const vals = t => { const m = S.mix[t], on = audible(t), tv = tvlOv.has(t) ? tvlOv.get(t) / 255 : 1, tpv = tpnOv.has(t) ? tpnOv.get(t) : m.pan;
    return { gv: on ? trackGain(m.vol) * tv : 0, pv: clamp((tpv - 128) / 127, -1, 1), tg: trimGain(m.trim == null ? TRIM_0 : m.trim), prv: on ? 1 : 0 }; };
  for (let t = 0; t < NT; t++) { const b = buses[t], v = vals(t); b.g.gain.value = v.gv; b.p.pan.value = v.pv; b.inp.gain.value = v.tg; b.pre.gain.value = v.prv; b.last = v; }
  buses[NT].g.gain.value = 1;
  const N = C.N || 128;
  for (const [bb, c] of C.cmds) {
    const cur = bb * N / SR;
    if (c.op === 'ev') {
      const at = Math.max(c.t, cur);
      if (c.type === 'mvol') { const to = curve(c.v); out.gain.setValueAtTime(mvol == null ? curve(S.master) : mvol, at); out.gain.linearRampToValueAtTime(to, at + 0.004); mvol = to; continue; }
      if (c.type === 'mout') { mo.gain.setTargetAtTime(curve(c.v), at, 0.004); continue; }
      const b = buses[c.track], m = S.mix[c.track];
      if (c.type === 'tvl') { tvlOv.set(c.track, c.v); b.g.gain.setTargetAtTime(audible(c.track) ? trackGain(m.vol) * c.v / 255 : 0, c.t, 0.004); }
      else if (c.type === 'tva') { const fg = trackGain(m.vol), g = trackGain(c.v); tvlOv.set(c.track, fg > 1e-6 ? g / fg * 255 : 0); b.g.gain.setTargetAtTime(audible(c.track) ? g : 0, at, 0.004); }
      else { tpnOv.set(c.track, c.v); b.p.pan.setTargetAtTime(clamp((c.v - 128) / 127, -1, 1), at, 0.004); }
      continue;
    }
    const at = Math.max(c.at == null ? cur : c.at, cur);
    // (a ramp is the port's own addition; the cases use it on a param at rest, so its start value is the last one set: `from`)
    const mv = (q, v, from) => { if (c.ramp > 0) { q.setValueAtTime(from, at); q.linearRampToValueAtTime(v, at + c.ramp / SR); } else q.setValueAtTime(v, at); };
    if (c.tr >= NT) {
      if (c.k === 'vol') { const was = curve(S.master); S.master = c.v; if (c.ramp == null) out.gain.setValueAtTime(curve(c.v), at); else mv(out.gain, curve(c.v), was); }
      else { const was = curve(S.mout); S.mout = c.v; if (c.ramp == null) mo.gain.setTargetAtTime(curve(c.v), at, 0.012); else mv(mo.gain, curve(c.v), was); }
      continue;
    }
    const m = S.mix[c.tr];
    if (c.k === 'vol') m.vol = c.v; else if (c.k === 'trim') m.trim = c.v; else if (c.k === 'pan') m.pan = c.v; else if (c.k === 'mute') m.mute = !!c.v; else if (c.k === 'solo') m.solo = !!c.v;
    for (let t = 0; t < NT; t++) { const b = buses[t], v = vals(t), l = b.last; b.last = v;
      if (c.ramp == null) { b.g.gain.setTargetAtTime(v.gv, at, 0.012); b.p.pan.setTargetAtTime(v.pv, at, 0.012); b.inp.gain.setTargetAtTime(v.tg, at, 0.012); b.pre.gain.setTargetAtTime(v.prv, at, 0.012); }   // applyMix, live: every track, every param
      else { if (v.gv !== l.gv) mv(b.g.gain, v.gv, l.gv); if (v.pv !== l.pv) mv(b.p.pan, v.pv, l.pv); if (v.tg !== l.tg) mv(b.inp.gain, v.tg, l.tg); if (v.prv !== l.prv) mv(b.pre.gain, v.prv, l.prv); } }
  }
  const r = await oc.startRendering();
  return [Array.from(r.getChannelData(0)), Array.from(r.getChannelData(1))];
}

let worst = -Infinity, bad = 0;
// the two places where Web Audio as the page drives it and the core's lanes are known to differ (the port follows the core)
const WHY = { stale: 'Chrome starts a setTargetAtTime that interrupts a moving one from the value one sample before its time; the core from the value at its time',
  live: 'the page re-aims every param (12 ms) on a mixer move, song glides included; the core keeps the song\'s lanes apart from the mixer\'s',
  page: 'the page player jumps to the last MVOL target before ramping; the core holds the value reached' };
console.log('Chrome ' + browser.version() + ', OfflineAudioContext at ' + SR + ' Hz: the model minus Web Audio, dB below the case\'s peak');
for (const name of CASE_NAMES) {
  const C = CASES[name]; if ((C.N || 128) !== 128) { console.log('  ' + name.padEnd(18) + ' (not Web Audio\'s block size: model only)'); continue; }
  const m = runCase(name), n = m.L.length, ins = inputsOf(C).map(q => [Array.from(q[0]), Array.from(q[1])]);
  const [L, R] = await page.evaluate(inPage, { C: { ins: C.ins, cmds: C.cmds, N: 128 }, S0: songOf(C), ins, SR, NT, n });
  let pk = 0, d = 0, at = -1; for (let i = 0; i < n; i++) { pk = Math.max(pk, Math.abs(m.L[i]), Math.abs(m.R[i])); const e = Math.max(Math.abs(m.L[i] - L[i]), Math.abs(m.R[i] - R[i])); if (e > d) { d = e; at = i; } }
  if (process.env.DBG === name) { let o = ''; for (let b = 0; b < n / 128; b++) { let e = 0; for (let i = b * 128; i < b * 128 + 128; i++) e = Math.max(e, Math.abs(m.L[i] - L[i]), Math.abs(m.R[i] - R[i])); o += b + ':' + e.toExponential(1) + ' '; } console.log(o); }
  const db = d === 0 ? -Infinity : 20 * Math.log10(d / (pk || 1)), known = C.web ? WHY[C.web] : '';
  if (!known) { if (db > worst) worst = db; if (db > -80) bad++; }
  console.log('  ' + name.padEnd(18) + (d === 0 ? ' exact' : ' ' + db.toFixed(1) + ' dB (largest at frame ' + at + ')') + '   peak ' + pk.toFixed(4) + (known ? '   NOT COUNTED: ' + known : ''));
}
console.log('worst counted ' + worst.toFixed(1) + ' dB; ' + (bad ? bad + ' case(s) above -80 dB: FAIL' : 'all at or below -80 dB: PASS'));
await browser.close();
process.exit(bad ? 1 : 0);
