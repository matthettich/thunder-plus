// Shared by tests/stretch-picker.test.js (and tests/stretch-oldschool.test.js): records what the SYNC code does for a fixed set of
// inputs, from any index.html text. The "before" fixture (tests/fixtures/stretch-before-659.json) was recorded from build 658
// (origin/beta 1dfbf7ad) with:  node tests/stretch-record.js <path to that index.html>  > tests/fixtures/stretch-before-659.json
// Not a test (no .test.js), so `npm test` does not run it.
'use strict';
const fs = require('fs'), path = require('path');
const S = require('./sampler-harness');

// a deterministic one-second "break" (noise bursts and a sine), not a recording
function source(sr) {
  let s = 12345; const rnd = () => { s = (Math.imul(s, 1664525) + 1013904223) >>> 0; return s / 4294967296 * 2 - 1; };
  const n = Math.round(sr * 1.5), d = new Float32Array(n);
  for (let i = 0; i < n; i++) { const t = i / sr, ph = (t * 4) % 1; d[i] = 0.5 * Math.sin(2 * Math.PI * (110 + 40 * ph) * t) * Math.exp(-ph * 6) + 0.3 * rnd() * Math.exp(-((t * 8) % 1) * 20); }
  return d;
}
const EXTRA = 'stretchData, stKey, GRAIN_BEATS,';

function record(html) {
  const E = S.loadSampler(html, EXTRA), out = { stretch: [], chop: [], load: {} };
  const sr = 44100, d = source(sr);
  // 1. the stretched audio: mode 2 (STRETCH) and 3 (LOOP) x GRAIN 0..4 x tempos x lengths
  for (const mode of [2, 3]) for (let grain = 0; grain < 5; grain++) for (const bpm of [90, 131, 174]) for (const beats of [1, 2, 3]) {
    const st = { mode, bpm, beats, grain }, o = E.stretchData(d, sr, 1.5, st);
    out.stretch.push([mode, grain, bpm, beats, E.stKey(st), o.length, S.hash(o)]);
  }
  // 2. the note events applyChop writes: sync 0..3 x GRAIN (0..4 and absent) x slices x lines x tempo x LPB x notes
  const kit = { voices: [E.mkVoice('S', [], { tune: 0, len: 1.2, gate: 400, tail: 0 })] }, St = { bpm: 131, lpb: 8, insts: [] };
  E.setKit(kit, St); E.ctx.__cache[0] = { data: d, sr };
  for (const sync of [0, 1, 2, 3]) for (const grain of [0, 1, 2, 3, 4, null]) for (const slc of [0, 4]) for (const lines of [0, 12]) for (const bpm of [100, 131]) for (const lpb of [4, 8]) for (const n of [48, 55]) {
    St.bpm = bpm; St.lpb = lpb;
    const I = { pad: 0, tsp: 0, slc, smode: 0, sync, beats: 3, ofs: 0 }; if (grain != null) I.grain = grain; if (lines) I.lines = lines;
    const ev = { type: 'note', t: 1, rate: Math.pow(2, (n - 48) / 12), ofs: 0, cut: null, slc: null };
    E.applyChop(ev, I, 0, n, 0, bpm, d, sr);
    out.chop.push([sync, grain, slc, lines, bpm, lpb, n, ev.rate, ev.ofsSec == null ? null : ev.ofsSec, ev.durSec == null ? null : ev.durSec, ev.cut, ev.st ? [ev.st.mode, ev.st.bpm, ev.st.beats, ev.st.grain, E.stKey(ev.st)] : null]);
  }
  return out;
}
// What the song loader does to an instrument's sync fields: cut the loader's own lines out of the page text and run them on a
// list of saved instruments; returns each instrument as it would be saved again (JSON text).
function loadInsts(html, insts, extra) {
  const E = S.loadSampler(html, EXTRA + (extra || ''));
  const a = html.indexOf('I.slc = clamp(int(o.slc'), b = html.indexOf('I.mo = clamp(int(o.mo', a);
  if (a < 0 || b < 0) throw new Error('the instrument loader lines moved');
  const frag = html.slice(a, b);
  require('vm').runInContext('globalThis.__loadInst = (o, I) => { const int = (v, d) => { v = +v; return Number.isFinite(v) ? Math.trunc(v) : d; };\n' + frag + '\n}', E.ctx);
  return insts.map(o => { const I = { pad: 0, tsp: 0, vol: 255, pan: 128, rev: 0, ofs: 0, slc: 0, smode: 0, sync: 0, beats: 4, grain: 1, mo: 0, mch: 0 }; E.ctx.__loadInst(o, I); return JSON.stringify(I); });
}
// every instrument of the shipped songs, and a constructed song that covers sync 0..3 x grain 0..4 (with and without lines)
function songInsts(root) {
  const all = {};
  for (const f of ['ELECTRO', 'BETA-TESTING-ELECTRO']) all[f] = JSON.parse(fs.readFileSync(path.join(root, 'sounds', f + '.thunder'), 'utf8')).song.insts;
  const c = []; for (let sync = 0; sync <= 3; sync++) for (let grain = 0; grain <= 4; grain++) for (const lines of [0, 24]) { const o = { pad: c.length % 8, slc: 0, sync, beats: 4, grain, ofs: 0 }; if (lines) o.lines = lines; c.push(o); }
  all.constructed = c; return all;
}
function recordLoads(html, root) { const r = {}, songs = songInsts(root); for (const k in songs) r[k] = loadInsts(html, songs[k]); return r; }
module.exports = { record, loadInsts, songInsts, recordLoads, source };
if (require.main === module) { const html = fs.readFileSync(process.argv[2], 'utf8'); const r = record(html); r.load = recordLoads(html, path.join(__dirname, '..')); process.stdout.write(JSON.stringify(r)); }
