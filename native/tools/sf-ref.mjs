// U32: the Node reference of the SoundFont player: the web app's own functions (index.html's SoundFont block, run in a `vm` as it is; core/seq-core.js's ThunderCore for the live note), driven with the same decoded PCM the native player has.
//   node native/tools/sf-ref.mjs --make-sf2 OUT.sf2          writes the small deterministic test font (integers only: the same bytes on every machine)
//   node native/tools/sf-ref.mjs --parse-dump FONT OUT.txt   the JS parse of a font as canonical text (native/tests/test_sf.cpp writes the same text)
//   node native/tools/sf-ref.mjs --ref DIR                   reads DIR/export.json + the PCM files the native player wrote (`test_sf export DIR`), writes DIR/ref_*.{f32,bin,txt}: the reference for every case
//   node native/tools/sf-ref.mjs --golden DIR | --check DIR  writes / checks native/tests/golden/sf_golden.inc (FNV-1a 64 of the reference of every case)
// The cases are native/tests/golden/sf_cases.json (shared with the C++ test). The decoder is the one thing the JS cannot run here (the page decodes SF3 samples with decodeAudioData): the native decode (stb_vorbis) is handed to
// both sides, so this layer is exact; the decoder difference is measured separately (native/tools/sf-chrome.mjs).
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
const CASES = JSON.parse(fs.readFileSync(path.join(ROOT, 'native/tests/golden/sf_cases.json'), 'utf8'));
const NT = 32;

// ---- the web app's SoundFont block, as it is ----------------------------------------------------------------------------------------
function loadPage() {
  const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
  const a = html.indexOf('/* ---------- SoundFont player (original code'), b = html.indexOf('const XUI = {', a);
  if (a < 0 || b < 0) throw new Error('SoundFont block not found in index.html');
  const norm = (V) => { const d = { bank: 0, prog: 0, trans: 0, velo: 0.7, atk: 1, rel: 1 }, o = V.sf, r = {}; for (const k in d) r[k] = o && typeof o[k] === 'number' && isFinite(o[k]) ? o[k] : d[k]; return r; };
  const ctx = vm.createContext({ console, TextDecoder, window: {}, toast() {}, toastErr() {}, setTimeout() {}, clamp: (v, lo, hi) => Math.max(lo, Math.min(hi, v)),
    xParams: (V) => Object.assign(norm(V), { tune: V.tune || 0, drive: V.drive || 0, level: V.level || 0 }) });
  ctx.globalThis = ctx;
  vm.runInContext(html.slice(a, b) + '\n;globalThis.__x = {parseSF, sfSrc, sfVoice, sfPick, sfPreset, sfRender, SF, GM_FONT};', ctx, { filename: 'index.html(SoundFont)' });
  return ctx.__x;
}

// ---- the processor (core/seq-core.js) in a vm, as native/tools/voice-dump.mjs ---------------------------------------------------------
function makeCore(sr) {
  let Cls = null;
  const src = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
  const ctx = vm.createContext({ console, registerProcessor(n, c) { Cls = c; }, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: sr, currentTime: 0, currentFrame: 0 });
  vm.runInContext(src + '\n;globalThis.__u32 = { setS(s){ S = s; } };', ctx, { filename: 'seq-core.js' });
  return { ctx, proc: new Cls(), U: ctx.__u32 };
}

// ---- fonts and the PCM the native player decoded ---------------------------------------------------------------------------------------
const fontFiles = {};
function fontOf(P, key, exp) {
  if (fontFiles[key]) return fontFiles[key];
  const buf = fs.readFileSync(path.join(ROOT, CASES.fonts[key]));
  const f = P.parseSF(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.length)); f.id = key === 'gm' ? P.GM_FONT.id : key; P.SF.fonts.set(f.id, f);
  return (fontFiles[key] = f);
}
function loadPcm(P, dir, exp) {
  for (const s of exp.samples) {
    const b = fs.readFileSync(path.join(dir, s.file)); const d = new Float32Array(b.buffer.slice(b.byteOffset, b.byteOffset + b.length));
    const f = fontFiles[s.font]; P.SF.smp.set(f.id + ':' + s.idx, { d, sr: s.sr, ls: s.ls, le: s.le, k: s.k });
  }
}

// ---- canonical text of a parsed font -------------------------------------------------------------------------------------------------
function parseText(f) {
  const out = [`font ${f.name}|${f.sf3 ? 1 : 0}|${f.samples.length}|${f.presets.length}|${f.smpl.length}`];
  for (const s of f.samples) out.push(`s ${s.name}|${s.start}|${s.end}|${s.ls}|${s.le}|${s.sr}|${s.root}|${s.corr}|${s.link}|${s.type}`);
  for (const p of f.presets) {
    out.push(`p ${p.name}|${p.bank}|${p.prog}|${p.zones.length}`);
    for (const z of p.zones) {
      const g = Object.keys(z.g).map(Number).filter(k => k < 64).sort((a, b) => a - b).map(k => `${k}=${z.g[k]}`).join(',');
      const m = l => l.map(x => `${x.src},${x.dst},${x.amt},${x.as},${x.tr}`).join(';');
      out.push(`z ${z.klo},${z.khi},${z.vlo},${z.vhi},${z.s}|${g}|${m(z.im)}|${m(z.pm)}`);
    }
  }
  return out.join('\n') + '\n';
}

// ---- hashing ------------------------------------------------------------------------------------------------------------------------
const M64 = (1n << 64n) - 1n;
function fnv(u8) { let h = 0xcbf29ce484222325n; for (let i = 0; i < u8.length; i++) { h ^= BigInt(u8[i]); h = (h * 0x100000001b3n) & M64; } return h; }
const f32bytes = (f) => new Uint8Array(f.buffer, f.byteOffset, f.byteLength);

// ---- note -> key / velocity as the page's sfNote computes them (so the native side and this one ask for the same samples) ---------------------
export function noteKeyVel(P_, ev) {
  const cl = (v, a, b) => Math.max(a, Math.min(b, v)), lr = Math.log2(Math.max(1e-4, ev.rate));
  return { key: cl(Math.round(60 + 12 * lr + P_.trans), 0, 127), vel: cl(Math.round(127 * ((1 - P_.velo) + P_.velo * Math.sqrt(cl(ev.gain, 0, 1)))), 1, 127) };
}
const padDefaults = (p) => Object.assign({ bank: 0, prog: 0, trans: 0, velo: 0.7, atk: 1, rel: 1, level: 0, len: 0 }, p || {});

// ---- the cases -------------------------------------------------------------------------------------------------------------------------
function renderCase(P, c) {
  const f = fontOf(P, c.font), pad = padDefaults(c.pad);
  const V = { engine: 'sf', sfFont: f.id, sf: { bank: pad.bank, prog: pad.prog, trans: pad.trans, velo: pad.velo, atk: pad.atk, rel: pad.rel }, level: pad.level, len: pad.len };
  return P.sfRender(V, c.sr, c.hold);
}

function jsEvent(e, sr, bf) {
  const T = (k) => e[k + 'F'] != null ? e[k + 'F'] / sr : e[k];
  const o = { type: e.type, t: e.f / sr, track: e.track || 0 };
  if (e.type === 'note') { Object.assign(o, { poly: !!e.poly, col: e.col || 0, pad: e.pad || 0, rate: e.rate, gain: e.gain, pan: e.pan == null ? 128 : e.pan });
    if (e.slide) o.slide = { from: e.slide.from, dur: e.slide.dur }; if (e.bend) o.bend = { mul: e.bend.mul, t1: e.bend.t1F / sr }; if (e.vib) o.vib = { spd: e.vib.spd, dep: e.vib.dep, t1: e.vib.t1F / sr };
    if (e.arp) o.arp = e.arp.map(([f, m]) => [f / sr, m]); if (e.cutF != null) o.cut = e.cutF / sr; if (e.relF != null) o.rel = e.relF / sr; }
  if (e.type === 'off' && e.cols) o.cols = e.cols;
  if (e.type === 'stp') o.all = !!e.all;
  if (e.type === 'mod') { if (e.gain != null) o.gain = e.gain; if (e.pan != null) o.pan = e.pan; if (e.mul != null) o.mul = e.mul;
    if (e.bend) o.bend = { mul: e.bend.mul, t1: e.bend.t1F / sr }; if (e.vib) o.vib = { spd: e.vib.spd, dep: e.vib.dep, t1: e.vib.t1F / sr };
    if (e.arp) o.arp = e.arp.map(([f, m]) => [f / sr, m]); if (e.cutF != null) o.cut = e.cutF / sr; if (e.relF != null) o.rel = e.relF / sr; }
  return o;
}

function liveCase(P, c) {
  const sr = c.sr || 48000, f = fontOf(P, c.font), pad = padDefaults(c.pad), N = 128, K = c.tracks || 1, blocks = Math.ceil(c.frames / N);
  const { ctx, proc } = makeCore(sr);
  const pr = P.sfPreset(f, Math.round(pad.bank), Math.round(pad.prog));
  const want = new Set(); for (const z of pr.zones) { want.add(z.s); const s0 = f.samples[z.s]; if (s0 && (s0.type & 6) && s0.link < f.samples.length) want.add(s0.link); }
  const samples = new Array(f.samples.length), smp = {};
  for (const k of want) { const a = P.SF.smp.get(f.id + ':' + k); if (!a) continue; const s0 = f.samples[k]; samples[k] = { root: s0.root, corr: s0.corr, type: s0.type, link: s0.link }; smp[k] = { d: a.d, sr: a.sr, ls: a.ls, le: a.le }; }
  // (samples the native player did not decode for this case are not in the page's message either: such a zone is skipped, as in the core)
  proc.msg({ op: 'sf', pad: 0, data: { P: { bank: pad.bank, prog: pad.prog, trans: pad.trans, velo: pad.velo, atk: pad.atk, rel: pad.rel, level: pad.level, len: pad.len }, font: { samples }, pr: { zones: pr.zones }, smp } });
  proc.kv = [Object.assign({ live: false, sf: true }, { choke: (c.kit && c.kit.choke) || 0, syn: !(c.kit && c.kit.syn === false), rel: 0 })];
  const out = []; for (let t = 0; t < K; t++) out.push([new Float32Array(blocks * N), new Float32Array(blocks * N)]);
  const outputs = []; outputs.length = NT + 6; for (let t = 0; t < K; t++) outputs[t] = [new Float32Array(N), new Float32Array(N)];
  const evs = c.events.map((e, i) => ({ b: Math.floor(e.f / N), e, i })); let alive = 0;
  for (let b = 0; b < blocks; b++) {
    ctx.currentTime = b * N / sr; ctx.currentFrame = b * N;
    for (const { b: bb, e } of evs) if (bb === b) { if (e.type === 'stopAll') proc.stopAll(e.f / sr); else { const ev = jsEvent(e, sr); ev.pad = 0; proc.ev(JSON.parse(JSON.stringify(ev))); } }
    for (let t = 0; t < K; t++) { outputs[t][0].fill(0); outputs[t][1].fill(0); }
    proc.process([], outputs);
    for (let t = 0; t < K; t++) { out[t][0].set(outputs[t][0], b * N); out[t][1].set(outputs[t][1], b * N); }
    alive = Math.max(alive, proc.alive.size);
  }
  const total = new Float32Array(K * 2 * blocks * N); let o = 0; for (let t = 0; t < K; t++) for (let k = 0; k < 2; k++) { total.set(out[t][k], o); o += blocks * N; }
  return total;
}

// ---- the deterministic test font (SF2, 16-bit integers) ---------------------------------------------------------------------------------
function makeSf2() {
  const u16 = (v) => { const b = Buffer.alloc(2); b.writeUInt16LE(v & 0xffff); return b; }, i16 = (v) => { const b = Buffer.alloc(2); b.writeInt16LE(v); return b; }, u32 = (v) => { const b = Buffer.alloc(4); b.writeUInt32LE(v >>> 0); return b; };
  const name = (s, n = 20) => { const b = Buffer.alloc(n); b.write(s, 'latin1'); return b; };
  const chunk = (id, data) => Buffer.concat([Buffer.from(id, 'latin1'), u32(data.length), data, data.length & 1 ? Buffer.alloc(1) : Buffer.alloc(0)]);
  const list = (id, parts) => chunk('LIST', Buffer.concat([Buffer.from(id, 'latin1'), ...parts]));
  // samples: integer recurrences only. saw: ramp; sine: y = (c y1 - y2) in integer fixed point; stereo pair; noise: LCG
  const S = [];
  const mk = (n, f) => { const a = new Int16Array(n); for (let i = 0; i < n; i++) a[i] = f(i, a); return a; };
  S.push({ nm: 'saw', d: mk(400, (i) => ((i * 163) % 32768) - 16384), sr: 44100, ls: 20, le: 380, root: 60, corr: 0, link: 0, type: 1 });
  { let y1 = 12000, y2 = 0; S.push({ nm: 'sine', d: mk(600, (i) => { if (i === 0) return 12000; const y = Math.trunc((1.9 * 4096 * y1) / 4096 - y2); y2 = y1; y1 = Math.max(-30000, Math.min(30000, y)); return y1; }), sr: 22050, ls: 100, le: 500, root: 69, corr: -12, link: 0, type: 1 }); }
  S.push({ nm: 'stL', d: mk(800, (i) => ((i * 97) % 20000) - 10000), sr: 32000, ls: 50, le: 750, root: 64, corr: 5, link: 3, type: 4 });
  S.push({ nm: 'stR', d: mk(800, (i) => ((i * 61) % 16000) - 8000), sr: 32000, ls: 50, le: 750, root: 64, corr: 5, link: 2, type: 2 });
  { let s = 12345; S.push({ nm: 'noise', d: mk(3000, (i) => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return ((s >>> 17) - 16384) * (3000 - i) / 3000 | 0; }), sr: 48000, ls: 0, le: 0, root: 60, corr: 0, link: 0, type: 1 }); }
  S.push({ nm: 'badloop', d: mk(500, (i) => ((i * 211) % 30000) - 15000), sr: 44100, ls: 9, le: 100, root: 60, corr: 0, link: 0, type: 1 });   // its loop starts before its own start (see below): the font says start 100
  // shdr: sample frames are laid out one after another with 46 zero frames between them
  let pos = 0; const smpl = []; const shdr = [];
  S.forEach((s) => { s.start = pos; s.end = pos + s.d.length; smpl.push(Buffer.from(s.d.buffer)); smpl.push(Buffer.alloc(46 * 2)); pos = s.end + 46; });
  S.forEach((s, i) => {
    const ls = i === 5 ? s.start - 20 : s.start + s.ls, le = i === 5 ? s.start + 300 : s.start + s.le;   // the last: loop start 20 frames before the sample's start (a font defect)
    shdr.push(Buffer.concat([name(s.nm), u32(s.start), u32(s.end), u32(ls < 0 ? 0 : ls), u32(le), u32(s.sr), Buffer.from([s.root]), Buffer.from([s.corr & 255]), u16(s.link), u16(s.type)]));
  });
  shdr.push(Buffer.concat([name('EOS'), u32(0), u32(0), u32(0), u32(0), u32(0), Buffer.from([0, 0]), u16(0), u16(0)]));
  // generators
  const G = (op, v) => Buffer.concat([u16(op), i16(v)]), R = (op, lo, hi) => Buffer.concat([u16(op), Buffer.from([lo, hi])]), SID = (id) => Buffer.concat([u16(53), u16(id)]);
  const igen = [], ibag = [], imod = [], inst = []; const M = (src, dst, amt, as, tr) => Buffer.concat([u16(src), u16(dst), i16(amt), u16(as), u16(tr)]);
  const bag = (g, m) => Buffer.concat([u16(g), u16(m)]);
  const addInst = (nm, zones) => { inst.push(Buffer.concat([name(nm), u16(ibag.length)])); for (const z of zones) { ibag.push(bag(igen.length, imod.length)); for (const g of z.g) igen.push(g); for (const m of z.m || []) imod.push(m); } };
  // I0 layers: a global zone (sustain, release, a key-hold tracking), a looped saw below C4 with a filter and mod envelope, a looped sine above with a pan, a stereo pair (velocity < 91) and a note-number modulator
  addInst('Layers', [
    { g: [G(37, 150), G(38, -1500), G(39, 40), G(36, -1200)], m: [M(0x0102, 8, -2400, 0x0D02, 0)] },   // the global zone: also overrides the default velocity -> filter modulator (same source / destination / amount source / transform: replaces it)
    { g: [R(43, 0, 59), G(54, 1), G(8, 4000), G(9, 120), G(11, 2400), G(26, -3000), G(29, 400), G(28, -2000), G(30, -4000), SID(0)], m: [M(0x0003, 48, 120, 0, 0)] },
    { g: [R(43, 48, 127), G(54, 3), G(17, 200), G(48, 30), G(34, -2500), SID(1)], m: [M(0x0302, 8, -1500, 0, 0), M(0x0A03, 51, 200, 0x0002, 0)] },
    { g: [R(44, 0, 90), G(54, 1), G(17, 0), G(34, -1500), SID(2)] },
    { g: [R(44, 0, 90), G(54, 1), G(17, 0), G(34, -1500), SID(3)] },
  ]);
  // I1 drums: one-shot noise at a root note, a key range each, exclusive class, coarse / fine tune, an overriding root key; and the defective-loop sample
  addInst('Drums', [
    { g: [G(38, -3000)] },
    { g: [R(43, 36, 36), G(57, 1), G(58, 60), SID(4)] },
    { g: [R(43, 38, 40), G(57, 1), G(58, 60), G(51, 7), G(52, -30), G(48, 60), SID(4)] },
    { g: [R(43, 42, 46), G(54, 1), G(58, 60), SID(5)] },
  ]);
  inst.push(Buffer.concat([name('EOI'), u16(ibag.length)])); ibag.push(bag(igen.length, imod.length)); igen.push(Buffer.alloc(4)); imod.push(Buffer.alloc(10));
  // presets: P0 uses Layers with preset generators (attenuation, tuning, a key range) and a preset-level modulator; P1 layers Layers and Drums; P2 (bank 128) the drums
  const pgen = [], pbag = [], pmod = [], phdr = [];
  const addPre = (nm, bank, prog, zones) => { phdr.push(Buffer.concat([name(nm), u16(prog), u16(bank), u16(pbag.length), u32(0), u32(0), u32(0)])); for (const z of zones) { pbag.push(bag(pgen.length, pmod.length)); for (const g of z.g) pgen.push(g); for (const m of z.m || []) pmod.push(m); } };
  const INS = (i) => Buffer.concat([u16(41), u16(i)]);
  addPre('Test Layers', 0, 0, [{ g: [G(48, 20), G(52, 10), G(38, 300)], m: [M(0x0002, 8, -600, 0, 0)] }, { g: [R(43, 0, 127), G(51, -1), INS(0)] }]);
  addPre('Test Both', 0, 1, [{ g: [INS(0)] }, { g: [R(43, 30, 90), G(48, 10), INS(1)] }]);
  addPre('Test Drums', 128, 0, [{ g: [INS(1)] }]);
  addPre('Test Both B', 3, 5, [{ g: [R(44, 60, 127), INS(0)] }]);
  phdr.push(Buffer.concat([name('EOP'), u16(0), u16(0), u16(pbag.length), u32(0), u32(0), u32(0)])); pbag.push(bag(pgen.length, pmod.length)); pgen.push(Buffer.alloc(4)); pmod.push(Buffer.alloc(10));
  const info = list('INFO', [chunk('ifil', Buffer.concat([u16(2), u16(1)])), chunk('INAM', name('U32 test font', 14)), chunk('isng', name('EMU8000', 8))]);
  const sdta = list('sdta', [chunk('smpl', Buffer.concat(smpl))]);
  const pdta = list('pdta', [chunk('phdr', Buffer.concat(phdr)), chunk('pbag', Buffer.concat(pbag)), chunk('pmod', Buffer.concat(pmod)), chunk('pgen', Buffer.concat(pgen)), chunk('inst', Buffer.concat(inst)),
    chunk('ibag', Buffer.concat(ibag)), chunk('imod', Buffer.concat(imod)), chunk('igen', Buffer.concat(igen)), chunk('shdr', Buffer.concat(shdr))]);
  const body = Buffer.concat([Buffer.from('sfbk', 'latin1'), info, sdta, pdta]);
  return Buffer.concat([Buffer.from('RIFF', 'latin1'), u32(body.length), body]);
}

// ---- main -----------------------------------------------------------------------------------------------------------------------------
const a = process.argv.slice(2);
if (a[0] === '--make-sf2') { fs.writeFileSync(a[1], makeSf2()); console.log('wrote ' + a[1]); }
else if (a[0] === '--parse-dump') {
  const P = loadPage(), buf = fs.readFileSync(a[1]); const f = P.parseSF(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.length));
  fs.writeFileSync(a[2], parseText(f)); console.log('wrote ' + a[2]);
} else if (a[0] === '--ref' || a[0] === '--golden' || a[0] === '--check') {
  const dir = a[1], exp = JSON.parse(fs.readFileSync(path.join(dir, 'export.json'), 'utf8')), P = loadPage();
  for (const k of Object.keys(CASES.fonts)) fontOf(P, k);
  loadPcm(P, dir, exp);
  const res = [];
  for (const c of CASES.renders) { const o = renderCase(P, c); res.push({ id: 'render_' + c.id, data: o }); }
  for (const c of CASES.lives) { const o = liveCase(P, c); res.push({ id: 'live_' + c.id, data: o }); }
  // the zone logic: sfVoice for every zone of the first font's presets at a few (key, vel), sfPick for every key and a few velocities, sfSrc over many codes
  { const f = fontOf(P, 'gm'), recs = [];
    const KV = [[60, 100], [21, 1], [127, 127], [77, 64]];
    for (const p of f.presets) for (const z of p.zones) for (const [k, v] of KV) { const o = P.sfVoice(f, z, k, v); recs.push(o.root, o.excl, o.modes, o.scale, o.tune, o.atten, o.pan, o.fc, o.q, o.modEnvToFc, o.ofs.start, o.ofs.end, o.ofs.ls, o.ofs.le,
      o.v.delay, o.v.atk, o.v.hold, o.v.dec, o.v.sus, o.v.rel, o.m.delay, o.m.atk, o.m.hold, o.m.dec, o.m.sus, o.m.rel); }
    res.push({ id: 'voice_gm', data: new Float64Array(recs) });
    const pk = []; for (const p of f.presets) for (let key = 0; key < 128; key++) for (const vel of [1, 64, 100, 127]) { const r = P.sfPick(f, p, key, vel); pk.push(r.length); for (const x of r) pk.push(p.zones.indexOf(x.z), x.stereo ? 1 : 0); }
    res.push({ id: 'pick_gm', data: new Int32Array(pk) });
    const sy = fontOf(P, 'syn'), rs = []; for (const p of sy.presets) for (const z of p.zones) for (const [k, v] of KV.concat([[0, 1], [59, 91], [100, 127]])) { const o = P.sfVoice(sy, z, k, v); rs.push(o.root, o.excl, o.modes, o.scale, o.tune, o.atten, o.pan, o.fc, o.q, o.modEnvToFc, o.ofs.start, o.ofs.end, o.ofs.ls, o.ofs.le,
      o.v.delay, o.v.atk, o.v.hold, o.v.dec, o.v.sus, o.v.rel, o.m.delay, o.m.atk, o.m.hold, o.m.dec, o.m.sus, o.m.rel); }
    res.push({ id: 'voice_syn', data: new Float64Array(rs) });
    const sp = []; for (const p of sy.presets) for (let key = 0; key < 128; key++) for (const vel of [1, 64, 90, 91, 127]) { const r = P.sfPick(sy, p, key, vel); sp.push(r.length); for (const x of r) sp.push(p.zones.indexOf(x.z), x.stereo ? 1 : 0); }
    res.push({ id: 'pick_syn', data: new Int32Array(sp) });
    const src = []; let s = 777; const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s; };
    for (let i = 0; i < 4000; i++) { const code = i < 2000 ? (rnd() & 0xffff) : ((rnd() & 0x3) << 10) | (rnd() & 0x3ff); for (const [k, v] of [[60, 100], [0, 1], [127, 127], [37, 64]]) for (const am of [false, true]) src.push(P.sfSrc(code, k, v, am)); }
    res.push({ id: 'src', data: new Float64Array(src) });
    { const xs = []; for (let i = 0; i < 128; i++) { const u = i / 128; xs.push((1 - u) * (1 - u), u * u); }
      let q = 4242; const r = () => { q = (Math.imul(q, 1664525) + 1013904223) >>> 0; return q / 4294967296; };
      for (let i = 0; i < 20000; i++) xs.push(r() * r() + 1e-9, Math.exp((r() - 0.5) * 600)); xs.push(1, 0.5, 1e-310, 5e-324, 1e300, 10, 100, 1000, 0.1);
      res.push({ id: 'log10_in', data: new Float64Array(xs) }); res.push({ id: 'log10', data: Float64Array.from(xs, Math.log10) }); }
    for (const k of Object.keys(CASES.fonts)) { const buf = fs.readFileSync(path.join(ROOT, CASES.fonts[k])); fs.writeFileSync(path.join(dir, 'ref_parse_' + k + '.txt'), parseText(P.parseSF(buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.length)))); }
  }
  if (a[0] === '--ref') { for (const r of res) fs.writeFileSync(path.join(dir, 'ref_' + r.id + '.bin'), Buffer.from(r.data.buffer, r.data.byteOffset, r.data.byteLength)); console.log('wrote ' + res.length + ' references to ' + dir); }
  else {
    const rows = res.filter(r => /^(render|live)_/.test(r.id)).map(r => ({ id: r.id, h: '0x' + fnv(new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength)).toString(16).padStart(16, '0') + 'ull', n: r.data.length }));
    const inc = ['// GENERATED by native/tools/sf-ref.mjs (--golden DIR) from index.html\'s SoundFont block and core/seq-core.js. Do not edit: a mismatch means the port is wrong.',
      '// FNV-1a 64 over each case\'s float32 output. Cases of the test font ("syn") do not depend on any decoder: the same on every platform. Cases of the GM font ("gm") depend on stb_vorbis\'s output (libm-dependent in its setup):',
      '// compared on Linux x86-64 only; every platform compares native against the Node reference built from the SAME decoded PCM.',
      'struct SfGolden { const char* id; unsigned long long hash; int floats; };', 'static const SfGolden kSfGolden[] = {', ...rows.map(r => `  {"${r.id}", ${r.h}, ${r.n}},`), '};', `static const int kSfGoldenN = ${rows.length};`, ''].join('\n');
    const incPath = path.join(ROOT, 'native/tests/golden/sf_golden.inc');
    if (a[0] === '--golden') { fs.writeFileSync(incPath, inc); console.log('wrote ' + incPath); }
    else { const cur = fs.readFileSync(incPath, 'utf8'); if (cur !== inc) { console.error('sf_golden.inc differs from what the JS gives now'); process.exit(1); } console.log('sf_golden.inc is current'); }
  }
} else { console.error('usage: see the top of this file'); process.exit(2); }
