// MIDI LEDGER (U35, docs/port/U35-NOTES.md): runs the web app's OWN scheduling code for outgoing MIDI against a FAKE output port that
// records (timestamp, bytes), for a set of small songs, and writes the ledgers as goldens for the C++ scheduler's test.
//
//   node tools/midi-ledger.mjs                  compare every case with native/tests/golden/midi-ledger-<case>.json (exit 1 on any difference)
//   node tools/midi-ledger.mjs --update         write the goldens (only after checking why they changed)
//   node tools/midi-ledger.mjs --out DIR        write the ledgers into DIR instead (the determinism proof: two runs, `diff -r`)
//   node tools/midi-ledger.mjs --list           the case names
//   node tools/midi-ledger.mjs --case NAME      one case, printed
//
// WHAT RUNS (nothing is re-implemented that the app has as a function; each piece is CUT OUT OF index.html by a pattern and run in a `vm`;
// if a pattern does not match, the tool stops with the name of the piece: there is no silent empty ledger):
//   index.html  "MIDI out and MIDI clock" (msend, the outbox, the hidden-page ledger, moEv, moNote, moMod, moCmd, moPlay, moClock, moLfo,
//               moRamp, moEcho, moStart, moRelease, moStop, the song-position code), clkPortToggled / clkJoin, panic, susUp, ccNum, CC_DEF,
//               mLatOf, midi (the settings object), audible, mstLatency, safeLatency, nmValid (the native port's message check)
//   core/seq-core.js  makeSeq: the sequencer, a generated copy of the page's (tools/make-core.mjs --check is run first: it must be current)
// WHAT IS A COPY (small, marked "COPY of index.html:NNNN" in HARNESS below, and each is checked to still exist by a pattern): the page's pump tick
//   (the step loop and the four booking calls), tStart / tStop's MIDI-relevant lines, the end-of-song check in frame(), the cable-change handler,
//   and preview() / previewOff()'s calls into moNote / offNote.
// The clock is virtual: performance.now() and the audio clock advance together in 1 ms steps; the pump runs every 25 ms (pumpTimer), the outbox
// every 15 ms (mobPut's setInterval), frame() every 17 ms. No wall time, no Math.random (coreSeeded): two runs give identical files.
// A ledger's `ms` is the timestamp the port was given, minus 1000, i.e. the audio clock in ms mapped by amap (offset exactly 1000 plus the
// case's output latency): `ms = 1000 * audioTime + 1000 * outputLatency`. `sent` is the (virtual) moment of the send() call: informational.
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const GOLD = path.join(ROOT, 'native/tests/golden');
const args = process.argv.slice(2);
const UPDATE = args.includes('--update');
const outIdx = args.indexOf('--out'), OUT = outIdx >= 0 ? path.resolve(args[outIdx + 1]) : null;
const caseIdx = args.indexOf('--case'), ONLY = caseIdx >= 0 ? args[caseIdx + 1] : null;
const die = m => { console.error('midi-ledger: ' + m); process.exit(2); };

// ---------------------------------------------------------------------------------------------------------------- the app's code, cut out
const html = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8');
const seqSrc = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
const cut = (re, what) => { const m = html.match(re); if (!m) die(`the app's code moved: could not find ${what} in index.html (pattern ${re}). Update tools/midi-ledger.mjs.`); return m[0]; };
const PIECES = [
  ['DEFNOTES', /const DEFNOTES = \[[^\]]*\];/],
  ['ports', /const NPORT = 4, PORTL = 'ABCD';/],
  ['midi settings', /const midi = \{on:false,[\s\S]*?names:\{\}\};/],
  ['mLatOf', /const mLatOf = p => midi\.lats\[p \| 0\] \|\| 0;/],
  ['MIDI out and MIDI clock (msend ... moStop)', /\/\* ---------- MIDI out and MIDI clock ---------- \*\/[\s\S]*?\nfunction moStop\(\)\{[\s\S]*?\n\}\n/],
  ['clkPortToggled + clkJoin', /\/\/ A port's clock switched while the song plays[\s\S]*?function clkJoin\(p, all\)\{[\s\S]*?\n\}\n/],
  ['panic', /function panic\(\)\{[\s\S]*?\n\}\n(?=function tStop)/],
  ['susUp', /function susUp\(\)\{[^\n]*\n/],
  ['CC_DEF', /const CC_DEF = \[[^\]]*\];/],
  ['ccNum', /const ccNum = [^\n]*\n/],
  ['audible', /const audible = t => \{[\s\S]*?return false; \};/],
  ['SAFE_LIM', /const SAFE_LIM = \{lim:\{[^\n]*\n/],
  ['safeLatency', /const safeLatency = [^\n]*\n/],
  ['mstLatency', /const mstLatency = [^\n]*\n/],
  ['pumpChainLatency', /const pumpChainLatency = [^\n]*\n/],
  ['NM_LEN', /const NM_LEN = [^\n]*\n/],
  ['pvHoldN', /let pvHoldN = 0;/],
  ['nmValid', /function nmValid\(b\)\{[\s\S]*?\n  return true; \}\n/],
];
const code = Object.fromEntries(PIECES.map(([n, re]) => [n, cut(re, n)]));
// Lines of index.html the harness COPIES (so a change there is noticed, not ignored). Each must still be in the file, verbatim.
const COPIES = [
  ['pump: the step loop', "const r = s.step(); if (rcdGateEdge(r)) break; for (const e of r.evs){ if (moEv(e)) continue; playEv(e); }"],
  ['pump: ARP returns', "mx.pend.forEach((P, t) => { if (P && P.length) pendFlush(t, r.t); });   // ARP returns due by this row, on tracks the row left alone"],
  ['pump: the four bookings', "moClock(s); moLfo(s); moRamp(); moEcho(); metRun(s);"],
  ['pump: look-ahead', "const want = document.hidden && !(tp.rsUntil > performance.now()) ? 1.2 : Math.max(0.25, outLat + 0.1)"],
  ['pump: timer period', "pumpW.postMessage(25);"],
  ['tStart: moStart before the start time', "moStart(mode === 'song' ? o.row : -1);"],
  ['tStart: start 80 ms ahead', "let t0start = lkp ? lkp.t : ctx.currentTime + 0.08 + ci + (warmNew ? 0.15 : 0);"],
  ['tStart: first pump', "tp.timer = pumpTimer(); pump();"],
  ['tStop: moStop', "if (!handing || mx.clk.on) G('MIDI stop', moStop);"],
  ['frame: song end', "if (!tp.seq.alive() && !tp.q.length && ctx.currentTime > tp.seq.time + 0.3) tStop();"],
  ['cable change', "const was = midiOut(p); moRelease(); if (was && mx.clk.on && midi.clk[p]) try { was.send([0xFC]); } catch(err){}"],
  ['preview: a held key', "const mt = hold && !when ? p2a(performance.now() + mLatOf(portOf(I)) + 1) : t;"],
  ['preview: the MIDI note', "const nid = moNote(Object.assign({}, ev, {t:mt, poly:true, cap:hold ? 16 : 4, mn:note + I.tsp + 12, vel:vol >= 0 ? vol / 255 : 1, rel:hold ? null : t + 0.3}), I);"],
  ['preview: the live key column naming (pvHoldN, a counter that never goes down)', "col = hold ? 'h' + (++pvHoldN) : undefined"],
  ['previewOff', "if (h.nid != null) offNote(NT, h.nid, at ? t : p2a(performance.now() + mLatOf(h.p) + 1));"],
];
for (const [n, s] of COPIES) if (!html.includes(s)) die(`the app's code moved: the harness copies a line of index.html ("${n}") that is no longer there verbatim. Update tools/midi-ledger.mjs.`);
// the page's sequencer (index.html) and the core's copy must be the same code
try { execFileSync(process.execPath, [path.join(ROOT, 'tools/make-core.mjs'), '--check'], { cwd: ROOT, stdio: 'pipe' }); }
catch (e) { die('core/seq-core.js is out of date against index.html (node tools/make-core.mjs --check failed): the ledger would not be of the page\'s sequencer.\n' + String(e.stdout || e.message)); }

// ---------------------------------------------------------------------------------------------------------------- the harness (runs in the vm)
const HARNESS = `
var VNOW = 1000;
var performance = { now: () => VNOW };
var document = { hidden: false, addEventListener(){} };
var ctx = { currentTime: 0, sampleRate: 48000, baseLatency: 0, outputLatency: 0, state: 'running',
  getOutputTimestamp(){ return { performanceTime: VNOW, contextTime: ctx.currentTime - (ctx.outputLatency || 0) }; } };
var TIMERS = [], TID = 0;
function setInterval(f, ms){ const t = {id: ++TID, f, ms, due: VNOW + ms}; TIMERS.push(t); return t.id; }
function clearInterval(id){ TIMERS = TIMERS.filter(t => t.id !== id); }
function setTimeout(f){ return 0; }
var toast = () => {}, tpLog = () => {}, rt = null, kb = {susTo: null};
var aset = {}, CASE = {pdcT: 0, mstOn: false};
var pdcModeOf = () => 'auto', pdcPlan = sr => ({T: CASE.pdcT || 0});
var pumpSlotLat = () => 0;
var KIND = 'other', LOG = [], DROPS = [], PADLOG = [], EVTALLY = {}, handing = null;
var tp = {on: false, seq: null, mode: '', q: [], stopAt: null, ahead: 0, aheadT: 0, timer: 0};
var mkPort = (idx, id, native) => { const o = {id, name: 'Fake ' + id, native: !!native, _idx: idx,
  send(b, ts){ const a = Array.from(b, x => +x); if (native && !nmValid(a)) throw new TypeError('invalid MIDI message: ' + a.join(','));
    LOG.push({ms: ts - 1000, sent: VNOW - 1000, p: idx, id, b: a, kind: KIND, drop: false, o}); } };
  if (native) o.clear = function(){ let n = 0; for (const m of LOG) if (m.o === o && !m.drop && m.ms + 1000 > VNOW){ m.drop = true; n++; } DROPS.push({sent: VNOW - 1000, port: id, what: 'clear', dropped: n}); };
  return o; };
// the instrumented names: each sets KIND (which part of the app is sending) while it runs; function declarations are global properties, so this reaches the calls the app makes itself
(function(){ const wrap = (name, kind) => { const f = globalThis[name]; if (typeof f !== 'function') throw new Error('midi-ledger: ' + name + ' is not a function (the app moved)'); globalThis[name] = function(...a){ const k = KIND; KIND = kind; try { return f.apply(this, a); } finally { KIND = k; } }; };
  for (const [n, k] of [['moEv', 'ev'], ['moClock', 'clock'], ['moLfo', 'lfo'], ['moRamp', 'ramp'], ['moEcho', 'echo'], ['moRelease', 'release'], ['moStop', 'stop'], ['moStart', 'start'],
    ['clkPortToggled', 'toggle'], ['clkJoin', 'join'], ['panic', 'panic'], ['pendFlush', 'ev'], ['moledCut', 'ledgercut']]) wrap(n, k); })();
for (const n of ['msend', 'moEv', 'moNote', 'moMod', 'moCmd', 'moPlay', 'moClock', 'moLfo', 'moRamp', 'moEcho', 'moStart', 'moRelease', 'moStop', 'sppFor', 'clkJoin', 'clkPortToggled', 'panic', 'offNote', 'mapUpd', 'pendFlush', 'mobFlush', 'moFxLat'])
  if (typeof globalThis[n] !== 'function') throw new Error('midi-ledger: ' + n + ' is missing: the app moved');
var playEv = e => { if (e.type === 'note') PADLOG.push({t: e.t, track: e.track, ii: e.ii}); };

// COPY of index.html:23452-23457 (pump) with the look-ahead of 23447 (visible: max(0.25, outLat + 0.1)) and the catch-up skip left out (it never fires on a healthy clock)
function pump(){
  if (!tp.on) return;
  const s = tp.seq; let n = 0;
  const outLat = Math.min(0.6, (ctx.baseLatency || 0) + (ctx.outputLatency || 0));
  const want = document.hidden ? 1.2 : Math.max(0.25, outLat + 0.1);
  const ahead = tp.ahead = want <= (tp.ahead || 0.25) ? want : Math.min(want, (tp.ahead || 0.25) + 0.75 * 0.025);
  while (tp.stopAt == null && s.alive() && s.time < ctx.currentTime + ahead && n++ < (document.hidden ? 256 : 64)){
    const r = s.step(); for (const e of r.evs){ EVTALLY[e.type] = (EVTALLY[e.type] || 0) + 1; if (moEv(e)) continue; playEv(e); }
    mx.pend.forEach((P, t) => { if (P && P.length) pendFlush(t, r.t); });
  }
  moClock(s); moLfo(s); moRamp(); moEcho();
}
// COPY of tStart (index.html:23112-23161), the lines that reach MIDI: STOP first, moStart before the start time, start 80 ms ahead, the pump timer then a first pump
function tStart(mode, o){
  if (tp.on) tStop(true);
  const seq = makeSeq(S, mode, Object.assign({}, o, {live: true}));
  if (!seq.alive()) throw new Error('midi-ledger: the case song has nothing to play');
  moStart(mode === 'song' ? o.row : -1);
  seq.time = ctx.currentTime + 0.08;
  Object.assign(tp, {on: true, mode, seq, q: [], stopAt: null, ahead: 0, aheadT: 0});
  moFxAt = -1e9;
  tp.timer = setInterval(pump, 25); pump();
}
// COPY of tStop (index.html:23860-23890): tp.on is false before moStop runs
function tStop(quiet){
  if (tp.timer) clearInterval(tp.timer);
  tp.timer = 0; tp.on = false; moFxAt = -1e9; tp.q = []; tp.stopAt = null;
  moStop();
}
// COPY of the end-of-song check in frame() (index.html:33678), every 17 ms (tp.q, drained by the draw, is not modelled: the stop comes 0.3 s after the last booked step)
function frame(){ if (tp.on && !tp.seq.alive() && ctx.currentTime > tp.seq.time + 0.3) tStop(); }
// COPY of the cable-change handler (index.html:17665-17669), without the saved settings and the DOM
function cableChange(p, id){ const was = midiOut(p); moRelease(); if (was && mx.clk.on && midi.clk[p]) try { was.send([0xFC]); } catch(err){}
  midi.outIds[p] = id; if (mx.clk.on && midi.clk[p]) clkJoin(p); }
// COPY of preview() / previewOff() (index.html:24184-24202) for a held key on a MIDI instrument: the calls into moNote / offNote only
var HELD = new Map();
function keyOn(ii, note, vol){ const I = S.insts[ii], t = ctx.currentTime + 0.005, col = 'h' + (++pvHoldN);   /* the app's own counter (index.html pvHoldN, cut out above): the column is never reused */
  const ev = {type: 'note', t, track: NT, poly: true, col, pad: I.pad, rate: 1, gain: 1, pan: I.pan, ofs: I.ofs, rev: false, cut: null, slide: null, rel: null};
  const mt = p2a(performance.now() + mLatOf(portOf(I)) + 1);
  const nid = moNote(Object.assign({}, ev, {t: mt, poly: true, cap: 16, mn: note + I.tsp + 12, vel: vol >= 0 ? vol / 255 : 1, rel: null}), I);
  HELD.set(note, {nid, p: portOf(I)}); }
function keyOff(note){ const h = HELD.get(note); if (!h) return; HELD.delete(note); offNote(NT, h.nid, p2a(performance.now() + mLatOf(h.p) + 1)); }
`;

// ---------------------------------------------------------------------------------------------------------------- one case
const NVAL = 1000;   // the audio clock's origin in performance.now() ms
function freshContext() {
  const c = vm.createContext({ console, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 48000, currentTime: 0, currentFrame: 0, Math, Date });
  vm.runInContext(seqSrc, c, { filename: 'core/seq-core.js' });
  const pre = HARNESS.split('// COPY of index.html:23452')[0];   // stubs and fakes first: the app's pieces need them at definition time (wrap() checks them after)
  vm.runInContext(pre.split('// the instrumented names')[0], c, { filename: 'harness-stubs.js' });
  for (const [n] of PIECES) vm.runInContext(code[n], c, { filename: 'index.html:' + n });
  vm.runInContext('// the instrumented names' + pre.split('// the instrumented names')[1] + '// COPY of index.html:23452' + HARNESS.split('// COPY of index.html:23452')[1], c, { filename: 'harness.js' });
  return c;
}

const FXC = (() => { const c = vm.createContext({ console, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 48000 }); vm.runInContext(seqSrc + '\n;globalThis.__f = FXC;', c); return [...c.__f]; })();
const fxn = n => { const i = FXC.indexOf(n); if (i < 0) die('unknown command ' + n); return i; };

// ---- songs: ELECTRO's song object with its grid, phrases and mastering cleared, then filled by a small spec ------------------------------------
const ELECTRO = JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')).song;
const OFFN = 200;
// spec: {bpm, swing, lpb, ticks, ntr, insts: {ii: {mo, mch, mpt, ...}}, tracks: {t: {inst, phrases: [ {step: [notes | cmd...]} ], len}}, mute: [t], solo: [t], mst: bool}
// a step is {n: [48, 52], v: 255, i: 0, fx: [['KIL', 4], ...]} (n: tracker notes, C-4 = 48 = MIDI 60; OFFN = the OFF note; at most 3 commands)
function mkSong(spec) {
  const S = JSON.parse(JSON.stringify(ELECTRO));
  for (const k of ['chainPatch', 'presets', 'irs', 'secs', 'sc2', 'fxPre', 'flx256', 'mst', 'master']) delete S[k];   // (the C++ test reads the same compact song)
  S.song = Array.from({ length: 256 }, () => new Array(33).fill(-1));
  S.chains = []; S.phrases = []; S.plen = []; S.plpb = [];
  S.bpm = spec.bpm || 120; S.swing = spec.swing || 50; S.swingB = spec.swingB || 2; S.lpb = spec.lpb || 4; S.ticks = spec.ticks || 6; S.ntr = spec.ntr || 16;
  S.tfx = new Array(32).fill(null); S.mfx = []; S.mst = spec.mst ? ELECTRO.mst : { on: false };
  if (!spec.mst) delete S.mst;
  S.mix = S.mix.map(m => ({ vol: 224, trim: 128, pan: 128, mute: false, solo: false })); S.chainOn = new Array(32).fill(true);
  for (const t of spec.mute || []) S.mix[t].mute = true;
  for (const t of spec.solo || []) S.mix[t].solo = true;
  S.smute = new Array(256).fill(0);
  S.insts = S.insts.slice(0, 16);
  const I0 = JSON.parse(JSON.stringify(ELECTRO.insts[0]));
  for (const [ii, o] of Object.entries(spec.insts || {})) S.insts[+ii] = Object.assign({}, JSON.parse(JSON.stringify(S.insts[+ii] || I0)), o);
  let pid = 0, cid = 0, endDone = false;
  for (const [t, T] of Object.entries(spec.tracks || {})) {
    const chain = [];
    for (const ph of T.phrases || [T.rows]) {
      const id = pid++; chain.push([id, 0]);
      S.phrases[id] = Array.from({ length: 32 }, () => new Array(12).fill(-1));
      S.plen[id] = T.len || 16; S.plpb[id] = 0;
      for (const [y, st] of Object.entries(ph)) {
        const r = S.phrases[id][+y];
        (st.n || []).forEach((n, c) => { r[c] = n; });
        r[4] = st.v == null ? -1 : st.v; r[5] = st.i == null ? (T.inst == null ? 0 : T.inst) : st.i;
        (st.fx || []).forEach(([name, v], k) => { r[6 + 2 * k] = fxn(name); r[7 + 2 * k] = v; });
      }
    }
    while (chain.length < 16) chain.push([-1, 0]);
    if (!spec.noEnd && !endDone) {   // the song ends after one pass: END on the last step of the first track's last phrase (without it the song wraps)
      const last = S.phrases[chain.filter(c => c[0] >= 0).pop()[0]][(T.len || 16) - 1], k = [6, 8, 10].find(q => last[q] < 0); last[k] = fxn('END'); last[k + 1] = 0; endDone = true; }
    S.chains[cid] = chain; S.song[spec.row0 || 0][+t] = cid++;
    for (let y = 1; y < (spec.rows || 1); y++) S.song[y][+t] = S.song[0][+t];   // (one chain on the rows 0..rows-1: the song block is `rows` rows deep)
  }
  for (let i = 0; i < S.phrases.length; i++) if (!S.phrases[i]) S.phrases[i] = null;
  for (let i = 0; i < S.chains.length; i++) if (!S.chains[i]) S.chains[i] = null;
  return S;
}
const trim = S => { let n = S.song.length; while (n > 1 && S.song[n - 1].every(v => v < 0)) n--; S.song = S.song.slice(0, n); S.smute = S.smute.slice(0, n); return S; };
// a row of notes helper
const N = (n, extra) => Object.assign({ n: Array.isArray(n) ? n : [n] }, extra || {});

// ---------------------------------------------------------------------------------------------------------------- the cases
// Each: {id, why, song spec, play: {mode, row, loop}, ports: [..], clk: [port flags], lats: [..], native: bool|'both', outLat, pdcT, actions: [{at, op, ...}], end (audio ms), expect: {...}}
// `at` and `end` are on the audio clock in ms; PLAY is pressed at `play` ms (default 500).
const cases = [];
const add = c => cases.push(Object.assign({ play: 500, mode: 'song', o: {}, ports: [{ id: 'A' }], clk: [], lats: [0, 0, 0, 0], actions: [], end: 4000, native: true, outLat: 0, expect: {} }, c));
const MO1 = (extra) => Object.assign({ mo: 1, mch: 0, mpt: 0, pbr: 2 }, extra || {});

add({ id: 'mono-notes', why: 'one mono mo instrument: a note, an OFF note, KIL, the OFF command, the next note ending the last, a note held to the song end',
  song: { insts: { 0: MO1() }, tracks: { 0: { phrases: [{
    0: N(48, { v: 255 }), 2: N(OFFN),
    3: N(52, { v: 128 }), 5: N(55, { v: 255, fx: [['KIL', 2]] }),
    6: N(57, { v: 100, fx: [['OFF', 3]] }),
    8: N(60, { v: 200 }), 9: N(62, { v: 200 }),
    11: N(64, { v: 255 }),
  }], len: 16 } } }, expect: { on: 7 } });

add({ id: 'note-len', why: 'NOTE LEN (mlen): a MIDI instrument\'s notes end after N ticks unless OFF / KIL / the next note come first',
  song: { insts: { 0: MO1({ mlen: 3 }) }, tracks: { 0: { phrases: [{ 0: N(48), 2: N(50, { fx: [['KIL', 1]] }), 4: N(52), 5: N(OFFN), 6: N(53), 7: N(55) }], len: 12 } } }, expect: { on: 5 } });

add({ id: 'overlap-poly', why: 'a poly track (8): chords in 4 columns; a new note in a column ends that column\'s note; the same key again; a 5th note over the 4-note cap; a per-column OFF',
  song: { insts: { 0: MO1() }, tracks: { 8: { inst: 0, phrases: [{
    0: N([48, 52, 55]), 2: N([48, 53]), 4: N([60, 64, 67, 71]), 5: N([72]), 6: { n: [-1, OFFN] }, 8: N([60, 62]), 10: N([OFFN]), 12: N([50]),
  }], len: 14 } } }, expect: { on: 12 } });

add({ id: 'chan-share', why: 'two tracks, two instruments, ONE channel and port: MIDI cannot tell their notes apart (the same key: one OFF ends both)',
  song: { insts: { 0: MO1({ mch: 2 }), 1: MO1({ mch: 2 }) }, tracks: { 0: { inst: 0, phrases: [{ 0: N(48), 4: N(OFFN) }], len: 8 }, 1: { inst: 1, phrases: [{ 1: N(48), 2: N(OFFN), 3: N(50) }], len: 8 } } }, expect: { on: 3 } });

add({ id: 'two-ports', why: 'two instruments on two ports and channels, a per-port Gear delay (+10 ms, -5 ms), two letters on one cable (C = A) with clock on A, C and B: the cable gets clock once',
  ports: [{ id: 'A' }, { id: 'B' }, { id: 'A' }, null], clk: [true, true, true, false], lats: [10, -5, 0, 0],
  song: { insts: { 0: MO1({ mch: 0, mpt: 0 }), 1: MO1({ mch: 5, mpt: 1 }), 2: MO1({ mch: 9, mpt: 2 }), 3: MO1({ mch: 1, mpt: 3 }) },
    tracks: { 0: { inst: 0, phrases: [{ 0: N(48), 2: N(OFFN), 4: N(50) }], len: 8 }, 1: { inst: 1, phrases: [{ 0: N(60), 1: N(62), 4: N(OFFN) }], len: 8 },
      2: { inst: 2, phrases: [{ 0: N(36), 2: N(OFFN) }], len: 8 }, 3: { inst: 3, phrases: [{ 0: N(40) }], len: 8 } } }, end: 2500, expect: { on: 5, clock: 24 } });

add({ id: 'velocity', why: 'velocity: vol 00 (no MIDI note at all), 01, 02, 7F, 80, FF, no vol (= FF); the VOL command; the note column\'s OFF',
  song: { insts: { 0: MO1() }, tracks: { 0: { phrases: [{
    0: N(48, { v: 255 }), 1: N(49, { v: 1 }), 2: N(50, { v: 2 }), 3: N(51, { v: 127 }), 4: N(52, { v: 128 }), 5: N(53, { v: 0 }), 6: N(54), 7: N(55, { fx: [['VOL', 64]] }), 8: N(56, { v: 200, fx: [['VOL', 255]] }),
  }], len: 10 } } }, expect: { on: 8 } });

add({ id: 'tempo-clock', why: 'MIDI clock on A with a tempo change mid-song (TPO 90 at step 16) and swing 60 % (swing moves notes, not the clock)',
  clk: [true], swing: 60, song: { bpm: 120, swing: 60, insts: { 0: MO1() }, tracks: { 0: { phrases: [{
    0: N(48), 2: N(50), 4: N(52), 6: N(53), 8: N(55), 10: N(57), 12: N(59), 14: N(60), 16: N(62, { fx: [['TPO', 90]] }), 18: N(64), 20: N(65), 22: N(67), 24: N(69), 26: N(71), 28: N(72), 30: N(OFFN) }], len: 32 } } },
  end: 7500, expect: { clock: 100 } });

add({ id: 'tempo-live', why: 'MIDI clock on A; the tempo changed live (the tempo box / Link / clock in: tp.seq.bpm = v) at 2.0 s and 3.0 s',
  clk: [true], song: { insts: { 0: MO1() }, tracks: { 0: { phrases: [{ 0: N(48), 4: N(50), 8: N(52), 12: N(OFFN), 16: N(53), 20: N(OFFN) }], len: 24 } } },
  actions: [{ at: 2000, op: 'bpm', v: 140 }, { at: 3000, op: 'bpm', v: 97.5 }], end: 7000, expect: { clock: 100 } });

add({ id: 'start-stop', why: 'PLAY, then STOP while a note is held and with notes and clock booked ahead (what STOP drops, the note-off, All Notes Off, the clock Stop 1 ms after the last booked tick)',
  clk: [true], song: { insts: { 0: MO1() }, tracks: { 0: { phrases: [{ 0: N(48), 4: N(50), 8: N(52), 12: N(53), 16: N(55) }], len: 24 } } },
  actions: [{ at: 1900, op: 'stop' }], end: 2500, native: 'both', expect: { on: 3, stop: 1 } });

add({ id: 'song-end', why: 'the song runs out with a note held and clock on: the transport stops itself 0.3 s after the last step (frame()), then Stop and All Notes Off',
  clk: [true], song: { insts: { 0: MO1() }, tracks: { 0: { phrases: [{ 0: N(48), 4: N(52), 7: { n: [], fx: [['END', 0]] } }], len: 8 } } }, end: 3000, native: 'both', expect: { on: 2, stop: 1 } });

add({ id: 'start-midway', why: 'PLAY from song row 2 with clock on: Song Position Pointer (a little ahead) then Continue instead of Start',
  clk: [true], mode: 'song', o: { row: 2 }, rows: 4,
  song: { rows: 4, noEnd: true, insts: { 0: MO1() }, tracks: { 0: { phrases: [{ 0: N(48), 4: N(52) }], len: 8 } } }, actions: [{ at: 3500, op: 'stop' }], end: 4000, expect: { spp: 1, cont: 1 } });

add({ id: 'clock-port-toggle', why: 'a port\'s clock switched ON mid-song (position + Continue, then ticks) and another switched OFF (Stop) and a cable changed',
  ports: [{ id: 'A' }, { id: 'B' }], clk: [true, false], song: { insts: { 0: MO1() }, tracks: { 0: { phrases: [{ 0: N(48), 8: N(52), 16: N(OFFN) }], len: 24 } } },
  actions: [{ at: 1500, op: 'clk', p: 1, on: true }, { at: 2200, op: 'clk', p: 0, on: false }, { at: 2600, op: 'cable', p: 1, id: 'A' }], end: 4000, native: 'both', expect: { clock: 20 } });

add({ id: 'loop', why: 'LOOP the pattern (song row 0 only): the notes and the clock carry on across the wrap; a note held over the loop edge; STOP at 5.5 s',
  clk: [true], o: { loop: { v: 'pat', y0: 0, y1: 0 } }, song: { noEnd: true, insts: { 0: MO1() }, tracks: { 0: { phrases: [{ 0: N(48), 2: N(OFFN), 4: N(52), 6: N(OFFN), 7: N(55) }], len: 8 } } },
  actions: [{ at: 5500, op: 'stop' }], end: 6500, expect: { on: 6, clock: 100 } });

add({ id: 'cc-program', why: 'CCA..CCD (the instrument\'s own CC numbers), PGM a millisecond before its note, MPB, MAT, CSL glides (a CC value every ~10 ms), INS (the row\'s instrument), a CC value > 127, a CC on a pad instrument (not sent), a muted track (not sent)',
  song: { insts: { 0: MO1({ cc: [20, 21, 22, 23], mch: 3 }), 1: { mo: 0 } }, mute: [2],
    tracks: { 0: { inst: 0, phrases: [{ 0: N(48, { fx: [['PGM', 5], ['CCA', 100], ['CCB', 127]] }), 2: { n: [], fx: [['CCC', 64], ['CCD', 200]] }, 4: N(50, { fx: [['MPB', 192], ['MAT', 90]] }),
        6: { n: [], fx: [['CCA', 0], ['CSL', 4]] }, 8: { n: [], fx: [['CCA', 127], ['CSL', 0]] }, 12: N(OFFN, { fx: [['CCB', 1]] }) }], len: 14 },
      1: { inst: 1, phrases: [{ 0: { n: [], fx: [['CCA', 50]] } }], len: 4 }, 2: { inst: 0, phrases: [{ 0: N(60, { fx: [['CCA', 77]] }) }], len: 4 } } },
  end: 3500, expect: { cc: 8, pgm: 1 } });

add({ id: 'cc-ins', why: 'the INS command picks the instrument a CC / PGM goes to (another port and channel), on a row with no note',
  ports: [{ id: 'A' }, { id: 'B' }], song: { insts: { 0: MO1({ mch: 0, mpt: 0 }), 1: MO1({ mch: 7, mpt: 1, cc: [1, 2, 3, 4] }) }, tracks: { 0: { inst: 0, phrases: [{ 0: { n: [], fx: [['INS', 1], ['CCA', 33], ['PGM', 9]] }, 2: N(48, { fx: [['CCA', 11]] }) }], len: 4 } } },
  end: 2000, expect: { cc: 2, pgm: 1 } });

add({ id: 'slide-arp', why: 'PSL glides, PBN bends, PVB wobbles, PIT / FIN retune (a note\'s fraction of a semitone is a bend), ARP steps on a note and on a held note (a row with no note), BEND RANGE 12',
  song: { insts: { 0: MO1({ pbr: 12 }) }, tracks: { 0: { phrases: [{
    0: N(48), 2: N(55, { fx: [['PSL', 3]] }), 4: N(52, { fx: [['PBN', 12]] }), 6: N(53, { fx: [['PVB', 0x34]] }), 8: N(48, { fx: [['ARP', 0x37]] }), 10: N(OFFN),
    11: N(60), 12: { n: [], fx: [['ARP', 0x47]] }, 14: { n: [], fx: [['PIT', 3]] }, 15: { n: [], fx: [['FIN', 50]] }, 16: N(OFFN) }], len: 18 } } }, end: 4500, expect: { bend: 5, on: 5 } });

add({ id: 'bend-fine', why: 'a note with FIN (fraction of a semitone, a bend) and PBN on a poly chord (one ramp per channel and row)',
  song: { insts: { 0: MO1({ pbr: 2 }) }, tracks: { 8: { inst: 0, phrases: [{ 0: N([48, 52, 55], { fx: [['FIN', 40]] }), 2: N([50, 54], { fx: [['PBN', 4]] }), 6: N([OFFN]) }], len: 8 } } }, end: 2500, expect: { bend: 3 } });

add({ id: 'modes-mixed', why: 'OUTPUT modes in one song: 0 PAD (no MIDI), 1 MIDI (gear only: the pad never sees the note), 2 PAD+MIDI (both); the pad log shows which notes the pad would play',
  song: { insts: { 0: { mo: 0 }, 1: MO1({ mch: 1 }), 2: { mo: 2, mch: 2, mpt: 0, pbr: 2 } }, tracks: { 0: { inst: 0, phrases: [{ 0: N(48), 2: N(OFFN) }], len: 4 }, 1: { inst: 1, phrases: [{ 0: N(50), 2: N(OFFN) }], len: 4 }, 2: { inst: 2, phrases: [{ 0: N(52), 2: N(OFFN) }], len: 4 } } },
  end: 2000, expect: { on: 2, pad: 2 } });

add({ id: 'column-switch', why: 'a track whose column switches from a MIDI instrument to a pad instrument: the MIDI note it held is ended (moOff on a non-MIDI note); a muted track lets its gear go quiet',
  song: { insts: { 0: MO1(), 1: { mo: 0 } }, tracks: { 0: { inst: 0, phrases: [{ 0: N(48), 2: N(50, { i: 1 }), 4: N(52, { i: 0 }) }], len: 6 } } }, end: 2000, expect: { on: 2 } });

add({ id: 'echo', why: 'ECHO on a MIDI instrument: ECO 3 repeats ECR 0.5 beat apart, 30 % softer each, 2 semitones higher each; an echo is dropped while a real note holds its key',
  song: { insts: { 0: MO1({ eco: 3, ecr: 0.5, ecf: 30, ecp: 2 }) }, tracks: { 0: { phrases: [{ 0: N(48), 8: N(60) }], len: 16 } } }, end: 4000, expect: { on: 7 } });

add({ id: 'lfo-cc', why: 'the instrument LFO into a CC: FREE (from PLAY) SINE on CC 74 at 1/4, and NOTE-triggered SAW UP on CC 71; a value every ~10 ms, only when it changes',
  song: { insts: { 0: MO1({ lcc: 74, lsh: 0, lrb: 1, ldp: 64, lct: 64, lrg: 0 }), 1: MO1({ mch: 1, lcc: 71, lsh: 2, lrb: 2, ldp: 100, lct: 60, lrg: 1 }) },
    tracks: { 0: { inst: 0, phrases: [{ 0: N(48), 4: N(OFFN) }], len: 8 }, 1: { inst: 1, phrases: [{ 2: N(50), 6: N(OFFN) }], len: 8 } } }, end: 2500, expect: { cc: 20 } });

add({ id: 'fx-latency', why: 'every timed message goes out later by the master chain\'s latency (mastering 2560 samples + limiter look-ahead + safety limiter) plus the delay compensation, and by the output latency (the audio-clock mapping)',
  clk: [true], outLat: 0.02, pdcT: 0.0123, lats: [4, 0, 0, 0], song: { mst: true, insts: { 0: MO1() }, tracks: { 0: { phrases: [{ 0: N(48), 4: N(OFFN) }], len: 6 } } }, end: 2500, expect: { on: 1, clock: 10 } });

add({ id: 'muted-solo', why: 'a muted track sends no notes (its gear goes quiet: moOff); a soloed other track mutes this one too',
  song: { insts: { 0: MO1(), 1: MO1({ mch: 1 }) }, solo: [1], tracks: { 0: { inst: 0, phrases: [{ 0: N(48), 2: N(50) }], len: 4 }, 1: { inst: 1, phrases: [{ 0: N(60), 2: N(62) }], len: 4 } } }, end: 1800, expect: { on: 2 } });

add({ id: 'panic-keys', why: 'live: a held key on a MIDI instrument (preview: out at once, +Gear delay), another key, key up; then PANIC (F8): sustain, All Sound Off, All Notes Off on every channel of every connected port',
  lats: [3, 0, 0, 0], song: { insts: { 0: MO1({ mch: 4 }) }, tracks: { 0: { inst: 0, phrases: [{ 0: N(48) }], len: 2 } } }, play: 5000,
  actions: [{ at: 600, op: 'keyOn', ii: 0, n: 48, v: 100 }, { at: 700, op: 'keyOn', ii: 0, n: 52, v: 127 }, { at: 900, op: 'keyOff', n: 48 }, { at: 1000, op: 'keyOn', ii: 0, n: 55, v: 64 }, { at: 1300, op: 'panic' }], end: 1600, native: 'both', noPlay: true, expect: { on: 3, panic: 1 } });

// ---------------------------------------------------------------------------------------------------------------- run
function runCase(C, native) {
  const c = freshContext();
  const run = s => vm.runInContext(s, c, { filename: 'case.js' });
  c.__spec = vm.runInContext('(' + JSON.stringify(mkSong(C.song)) + ')', c);
  run('S = __spec;');
  run('CASE = ' + JSON.stringify({ pdcT: C.pdcT || 0 }) + ';');
  // the settings the Settings → MIDI page holds
  c.__ports = C.ports; c.__clk = C.clk; c.__lats = C.lats; c.__native = !!native;
  run(`midi.access = {outputs: new Map(), native: __native}; const made = new Map();
    __ports.forEach((P, p) => { if (!P){ midi.outIds[p] = ''; return; } if (!made.has(P.id)){ const o = mkPort(p, P.id, __native); made.set(P.id, o); midi.access.outputs.set(P.id, o); } midi.outIds[p] = P.id; });
    __clk.forEach((b, p) => { midi.clk[p] = !!b; }); __lats.forEach((v, p) => { midi.lats[p] = v; });`);
  run(`ctx.outputLatency = ${C.outLat || 0};`);
  const actions = C.actions.slice().sort((a, b) => a.at - b.at); let ai = 0, started = false, playDone = false;
  const startAt = C.noPlay ? Infinity : C.play, endMs = C.end;
  // fixed order inside one ms: actions, then the timers due (by due time, then creation); the 17 ms frame
  run('var FRAMEID = setInterval(frame, 17);');
  const stepMs = () => {
    run(`VNOW += 1; ctx.currentTime = (VNOW - ${NVAL}) / 1000;`);
    const at = vm.runInContext('VNOW', c) - NVAL;
    if (!started && at >= startAt) { started = true; run(`tStart(${JSON.stringify(C.mode)}, ${JSON.stringify(Object.assign({ row: 0 }, C.o))});`); }
    while (ai < actions.length && actions[ai].at <= at) {
      const a = actions[ai++];
      if (a.op === 'stop') run('tStop();');
      else if (a.op === 'bpm') run(`if (tp.on && tp.seq) tp.seq.bpm = ${a.v};`);
      else if (a.op === 'clk') run(`midi.clk[${a.p}] = ${!!a.on}; clkPortToggled(${a.p});`);
      else if (a.op === 'cable') run(`cableChange(${a.p}, ${JSON.stringify(a.id)});`);
      else if (a.op === 'keyOn') run(`keyOn(${a.ii}, ${a.n}, ${a.v});`);
      else if (a.op === 'keyOff') run(`keyOff(${a.n});`);
      else if (a.op === 'panic') run('panic();');
      else die('unknown action ' + a.op);
    }
    run(`for (;;){ const due = TIMERS.filter(t => t.due <= VNOW).sort((a, b) => a.due - b.due || a.id - b.id)[0]; if (!due) break; due.due += due.ms; due.f(); }`);
  };
  const total = endMs;
  for (let k = 0; k < total; k++) stepMs();
  const get = e => vm.runInContext(e, c);
  const log = get('JSON.stringify(LOG.map(m => ({ms: m.ms, sent: m.sent, p: m.p, id: m.id, b: m.b, kind: m.kind, drop: m.drop})))');
  return { log: JSON.parse(log), drops: JSON.parse(get('JSON.stringify(DROPS)')), pad: JSON.parse(get('JSON.stringify(PADLOG)')), tally: JSON.parse(get('JSON.stringify(EVTALLY)')), alive: get('tp.on') };
}

const r6 = x => Math.round(x * 1e6) / 1e6;
const kindName = b => { const s = b[0] & 0xF0; return b[0] === 0xF8 ? 'clock' : b[0] === 0xFA ? 'start' : b[0] === 0xFB ? 'continue' : b[0] === 0xFC ? 'stop' : b[0] === 0xF2 ? 'spp' : s === 0x90 ? (b[2] ? 'on' : 'off') : s === 0x80 ? 'off' : s === 0xB0 ? 'cc' : s === 0xC0 ? 'pgm' : s === 0xD0 ? 'at' : s === 0xE0 ? 'bend' : 'other'; };

// What the gear is left doing at the end: a replay of the delivered stream in time order
function replay(msgs) {
  const down = new Map(), order = msgs.map((m, i) => ({ m, i })).sort((a, b) => a.m.ms - b.m.ms || a.i - b.i), findings = [];
  const sus = new Set();
  for (const { m } of order) {
    const b = m.b, ty = b[0] & 0xF0, k = m.id + ':' + (b[0] & 15);
    if (ty === 0x90 && b[2] > 0) down.set(k + ':' + b[1], m.ms);
    else if (ty === 0x80 || (ty === 0x90 && !b[2])) down.delete(k + ':' + b[1]);
    else if (ty === 0xB0 && (b[1] === 123 || b[1] === 120)) for (const d of [...down.keys()]) if (d.startsWith(k + ':')) down.delete(d);
  }
  for (const [k, t] of down) findings.push(`note left sounding at the end: ${k} (on at ${r6(t)} ms)`);
  return findings;
}
function summarize(C, run, native) {
  const msgs = run.log.filter(m => !m.drop).map(m => ({ ms: r6(m.ms), sent: r6(m.sent), p: m.p, id: m.id, k: m.kind, b: m.b }));
  const counts = {}; for (const m of msgs) { const n = kindName(m.b); counts[n] = (counts[n] || 0) + 1; }
  const dropped = run.log.filter(m => m.drop).length;
  const findings = replay(msgs);
  const E = C.expect || {};
  const need = (cond, what) => { if (!cond && process.env.MLDEBUG) { console.log('EXPECT FAIL', C.id, what, JSON.stringify(counts)); return; } if (!cond) die(`case ${C.id} (${native ? 'native' : 'web'}): expectation failed: ${what} (counts ${JSON.stringify(counts)}). The app's code changed or the hook no longer reaches it: no silent empty ledger.`); };
  need(msgs.length > 0, 'the ledger is empty');
  if (E.on != null) need((counts.on || 0) >= E.on, `at least ${E.on} note-ons`);
  if (E.clock != null) need((counts.clock || 0) >= E.clock, `at least ${E.clock} clock ticks`);
  if (E.cc != null) need((counts.cc || 0) >= E.cc, `at least ${E.cc} CCs`);
  if (E.pgm != null) need((counts.pgm || 0) >= E.pgm, `at least ${E.pgm} program changes`);
  if (E.bend != null) need((counts.bend || 0) >= E.bend, `at least ${E.bend} pitch bends`);
  if (E.spp != null) need((counts.spp || 0) >= E.spp, 'a Song Position Pointer');
  if (E.cont != null) need((counts.continue || 0) >= E.cont, 'a Continue');
  if (E.stop != null) need((counts.stop || 0) >= E.stop, 'a clock Stop');
  if (E.pad != null) need(run.pad.length >= E.pad, `at least ${E.pad} notes reaching the pad`);
  if (E.panic != null) need(msgs.filter(m => m.k === 'panic').length >= 48, '48 panic messages');
  const song = mkSong(C.song);
  return { case: C.id, mode: native ? 'native' : 'web', why: C.why,
    setup: { bpm: song.bpm, swing: song.swing, lpb: song.lpb, ticks: song.ticks, ports: C.ports.map(p => p && p.id), clock: C.clk, gearDelayMs: C.lats, outputLatencyS: C.outLat || 0, pdcT: C.pdcT || 0, masteringOn: !!(C.song && C.song.mst),
      playAtMs: C.noPlay ? null : C.play, play: C.mode, playOpts: C.o, actions: C.actions, endMs: C.end, insts: C.song.insts, tracks: Object.keys(C.song.tracks || {}).map(Number) },
    seqEvents: run.tally, counts, droppedByClear: dropped, drops: run.drops, pad: run.pad.map(x => ({ t: r6(x.t), track: x.track, ii: x.ii })), findings, msgs };
}

// ---------------------------------------------------------------------------------------------------------------- main
if (args.includes('--list')) { for (const c of cases) console.log(c.id); process.exit(0); }
const todo = cases.filter(c => !ONLY || c.id === ONLY);
if (!todo.length) die('no such case');
const outDir = OUT || GOLD;
if (OUT) fs.mkdirSync(OUT, { recursive: true });
const files = [], index = [];
for (const C of todo) {
  const modes = C.native === 'both' ? [true, false] : [!!C.native];
  for (const native of modes) {
    const s = summarize(C, runCase(C, native), native);
    const name = `midi-ledger-${C.id}${C.native === 'both' && !native ? '-web' : ''}.json`;
    const text = '{\n' + Object.entries(s).filter(([k]) => k !== 'msgs').map(([k, v]) => ` ${JSON.stringify(k)}: ${JSON.stringify(v)}`).join(',\n') + ',\n "msgs": [\n' + s.msgs.map(m => '  ' + JSON.stringify(m)).join(',\n') + '\n ]\n}\n';
    files.push([name, text]);
    { const sn = `midi-ledger-${C.id}.song.json`; if (!files.some(f => f[0] === sn)) files.push([sn, JSON.stringify({ app: 'thunder-song', v: 6, song: trim(mkSong(C.song)) }) + '\n']); }
    index.push({ file: name, case: C.id, mode: s.mode, msgs: s.msgs.length, bytes: Buffer.byteLength(text), sha256: crypto.createHash('sha256').update(text).digest('hex'), counts: s.counts, findings: s.findings.length });
  }
}
let bad = 0;
if (ONLY) { process.stdout.write(files[0][1]); process.exit(0); }
const idxText = JSON.stringify({ note: 'tools/midi-ledger.mjs: one ledger per case; msgs are in the order the port was handed them; ms = the timestamp given to send() minus 1000 (the audio clock, ms); kind k says which part of the app sent it', cases: index }, null, 1) + '\n';
files.push(['midi-ledger-index.json', idxText]);
for (const [name, text] of files) {
  const f = path.join(outDir, name);
  if (OUT || UPDATE || !fs.existsSync(f)) { fs.writeFileSync(f, text); console.log(`wrote ${path.relative(ROOT, f)} (${text.length} bytes)`); continue; }
  const ok = fs.readFileSync(f, 'utf8') === text; if (!ok) bad++;
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${name}`);
}
for (const e of index) console.log(`${e.case} [${e.mode}]: ${e.msgs} msgs ${JSON.stringify(e.counts)}${e.findings ? '  FINDINGS ' + e.findings : ''}`);
process.exit(bad ? 1 : 0);
