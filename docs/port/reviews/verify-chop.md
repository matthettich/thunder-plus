# Verification of PR #245 (n1/chop): the native sequencer chops sliced and synced instruments

Verifier: independent (Claude Fable 5.2). Branch under test: n1/chop at 7b106dc4. STATUS: FINAL (60 min box used).


## Verdict: SOUND WITH CONDITIONS
The arithmetic is the JS's to the last bit on 2 520 random combinations; the voice starts and stops where the chop says (sample-accurate at pad rates 22 050 .. 96 000 Hz, rate 0.5 / 1 / 2, reversed, block-size independent); hostile input is clean under ASan + UBSan; the allocation guard holds. One real test gap (F1: a mutation in the reversed-chop offset survives every committed test) and notes. Nothing found makes the sound wrong today, so nothing blocks the merge; F1 is a condition to fix soon (a test, no code change).

## Findings
- **F1 (test gap; does not block; add the test before the reversed path is relied on).** The voice's reversed offset `max(0, dur - ofsSec - durSec)` (voice.cpp, the `off` line after "ev.ofsSec / ev.durSec", ~:178) is not tested at render level. Mutation M4 (`dur - e.ofsSec`) passes `seq_chop`, `render_chop` and `rt_harness`. test_render_chop.cpp's header promises "reversed notes read the slice back to front" but its `songJson(.., rev)` flag is only ever passed as `zero == 1` in the hostile loop, so reversed and zero-length pads are conflated and no reversed output is checked. The current code is RIGHT: my scratch (section 3) measures a reversed slice-1 note starting at ramp value 0.47998 vs 0.48 expected at 22 050 / 44 100 / 48 000 / 96 000 Hz. Repro: change the line as above, rebuild, run ctest `seq_chop|render_chop`: 0 FAIL lines.
- **F2 (note) the tool's `ensure` is dead code.** tools/seq-golden.mjs replaces the stub line of core/seq-core.js with `__ensure` (kit.voices check + the tool's pad table), but core/seq-core.js:911-912 then reassigns `ensurePlay` / `ensure` to `coreBufs.get(pad)`, which does not test `kit.voices[i]` (the page's `ensure`, index.html:8856, does, and for a synth pad renders it). The goldens are honest for what applyChop computes (it reads only `loop` and `slicePoints` through `data` / `sr`), but "ensure over the tool's pad table" is not what runs. What the C++ cannot check: that the host hands in `loopSecs` of the page's RENDERED pad (synth pad: whole = rendered length at padSR).
- **F3 (note) the note's end is cut + the 4 ms release.** The note ends at `cut = t + durSec / rate` and then the voice's exponential release (about 134 frames to half level at 48 kHz): measured 12 134 vs 12 000 expected. Same as the page (`kill(v, ev.cut)`). A slice that ends at the pad's end stops with the data (11 999 / 12 000). Not a defect.
- **F4 (note) parts (PERFORM) are in no golden and not in my differential.** seq.cpp buildParts reads a part instrument's slc / smode / sync / grain / beats unclamped (`js_to_int32(jnum(...))`), as the page's part JSON is not clamped; with slc > 64 in a part, `nn = min(slc, 64)` with `% slc` and the grid `loop * i / nn` would differ from the JS (C++-only). Unreachable from the page's UI (max 64).
- **F5 (note, the builder's choice 5) an SLC / k outside the handed-in points plays slice 0** where the JS reads `pts[k]` = undefined (NaN offset). Reachable only with SMODE and fewer hits than slices. My differential never hit it (0 mismatches), so it is not exercised either way.
- **F6 (note) sizes.** sizeof(SeqEvent) 456 (confirmed by a compile), kSeqStepEvents 12288 x 456 = 5 603 328 B; kRenderEvents = kSeqStepEvents, so the renderer's event buffer is that.

## 3. Voice / render side (scratch `vchop_render.cpp`, below)
Ramp pad (value = frame / len), slc 4 and 7, notes through the C API, pad rates 22 050 / 44 100 / 48 000 / 96 000 Hz at a 48 kHz output: the start position (ramp level 20 ms in) equals `s0 + 0.02 * rate` to 5 digits for rate 1, rate 2 (SLC 1 on C-5), rate 0.5 (SLC 2 on C-2) and reversed (starts at the slice end, runs back); the end frame is cut + release (F3) within 1 frame for a slice ending at the data end and within the release (134 frames) for the others; every output is bit-identical at blocks 1 / 333 / 4096. Out-of-range `ofsSec` / `durSec`: the voice clamps the start to `dur - 0.001` like the page; a loop beyond the pad plays the last millisecond; durSec floors at 0.005. An `st` note: counted in `notes_stretched` (SH_ST), played unstretched at `rate` from `ofsSec` until `cut`, as ruled (audible; the owner's list).

## 4. Hostile input
Committed tests under ASan + UBSan (my own -DTHUNDER_SANITIZE=ON build): `seq_chop` PASS, `render_chop` PASS. My `vchop_hostile.cpp` (below), 480 renders under ASan + UBSan: loops 1e-6 / 0.01 / 1 / 2 / 1e6; hits unsorted, duplicate, all 0, beyond the buffer (999 999 s), 1 point of 64, 64 descending; slc 2..64 with SMODE; sync 0 / 2; reversed on / off; 100-frame and 2 s pads; pad replaced AFTER the chop data: all finite, no sanitizer report, and a replaced pad leaves no stale chop data (`seq_chopped` 0). NaN / inf / negative points and loops are refused by the API (committed test). Not exercised by me: a chop edit through `takeOver` while a sliced note sounds, beyond the committed chop-edit golden (a takeOver at each edit, rows equal to the JS's).

## 5. Real-time rules
`rt_harness` run by me (Release): "sequencer with chop: 3000 steps, 12000 chopped notes (3000 with st), no allocation". The guard's sensitivity is the builder's mutation 4; I did not repeat it. applyChop allocates nothing by inspection (stack doubles; `SeqChop::hits` is a binary search returning a pointer).

## 6. Mutations (mine; each reverted; `seq_chop` + `render_chop` + `rt_harness` run each time)
| # | mutation | caught? |
|---|---|---|
| M1 | wrap `((n - ROOT) % slc + slc) % slc` -> `(n - ROOT) % slc` (seq.cpp) | yes, seq_chop 9 FAIL |
| M2 | STA offset `/ 256` -> `/ 255` (seq.cpp) | yes, seq_chop 9 FAIL |
| M3 | note end `t + durSec / rate` -> `t + durSec` (seq.cpp) | yes, seq_chop 9 FAIL |
| M4 | voice reversed offset `dur - ofsSec - durSec` -> `dur - ofsSec` (voice.cpp) | NO (F1) |
| M6 | `setLoop(pad, 0)` no longer drops the pad's hits (chop.h) | yes, seq_chop 2 FAIL |

## 7. Differential (JS text vs C++)
Result: seeds 1..4, 420 + 3 x 700 = 2 520 random instrument/track combinations (7 per song; song / sect / phrase modes), 74 000+ notes (retrigger copies included), exact equality on t, track, pad, rate, ofs, ofsSec, durSec, cut, rev and st mode / bpm / beats / grain: 0 mismatches, 0 last-ulp differences; `chopSkipped` / `chopNoHits` / `dropped` all 0. Covered: notes 0..127 (far below and above the root), slc {0,2,3,4,5,7,8,16,17,31,32,63,64, random}, smode on/off, sync 0..3, beats {1,2,3,4,5,7,16,64} or unset, grain 0..4 or unset, ofs {0,1,64,128,255}, rev, tsp -24..24, commands SLC 0..255, STA 0..255, KIL, TPO, RET, PLY, PIT, FIN, ARP, PBN, PSL, OFF, TSP; 22 050 / 44 100 / 48 000 Hz pads with random layers (so random `loopSecs`). Not reachable from a loaded song (the loader clamps as the page's loader does, index.html:20377): slc > 64, beats 0, non-integer beats. Reproduce: `node vchop-gen.mjs <worktree> cases.json 700 <seed>`; `vchop_diff cases.json cpp.json`; `node vchop-cmp.mjs cases.json cpp.json`.

### vchop-gen.mjs
```js
// Scratch differential, JS side: node vchop-gen.mjs <worktree> <out.json> [N=300] [seed=1]
// Builds random songs (7 instruments / tracks each), runs the page's REAL applyChop/loopSecs/slicePoints/findHits (text from index.html) inside core/seq-core.js's makeSeq (sim off), writes cases + JS notes.
import fs from 'fs'; import path from 'path'; import vm from 'vm';
const ROOT = path.resolve(process.argv[2]), OUT = process.argv[3], N = +(process.argv[4] || 300); let seed = +(process.argv[5] || 1) >>> 0;
const rnd = () => ((seed = (seed * 1664525 + 1013904223) >>> 0) / 4294967296), ri = (a, b) => a + Math.floor(rnd() * (b - a + 1)), pick = a => a[Math.floor(rnd() * a.length)];
const idx = fs.readFileSync(path.join(ROOT, 'index.html'), 'utf8'), core = fs.readFileSync(path.join(ROOT, 'core/seq-core.js'), 'utf8');
const STUBS = 'function ensurePlay(){ return null; } function ensure(){ return null; } function varWant(){} function applyChop(){}';
if (!core.includes(STUBS)) throw new Error('stub line changed');
const fn = name => { const i = idx.indexOf('\nfunction ' + name + '('); let k = idx.indexOf('{', i), d = 0; for (; k < idx.length; k++) { const c = idx[k]; if (c === '{') d++; else if (c === '}' && --d === 0) break; } return idx.slice(i + 1, k + 1); };
const src = core.replace(STUBS, 'function ensurePlay(i){ return __ensure(i); } function ensure(i){ return __ensure(i); } function varWant(){}') + '\n' + [fn('loopSecs'), fn('findHits'), 'const sliceMemo = new Map();', fn('slicePoints'), fn('applyChop')].join('\n') +
`\nconst __pads = []; function __ensure(i){ return kit.voices[i] ? __pads[i] || null : null; }
globalThis.__u = { coreBufs, makeSeq, coreSeeded, coreNum, __pads, loopSecs, slicePoints, sliceMemo, setKit(k){ kit = k; }, load(s){ S = s; busPlanH = null; busPlanVal = null; } };`;
const sctx = vm.createContext({ console, Math, registerProcessor() {}, AudioWorkletProcessor: class { constructor() { this.port = { postMessage() {}, onmessage: null }; } }, sampleRate: 48000, currentTime: 0, currentFrame: 0 });
vm.runInContext(src, sctx, { filename: 'core+chop.js' }); const U = sctx.__u;
const FX = ['ARP','ARC','CHA','DEL','GRV','GGR','HOP','INS','KIL','OFF','NTH','RET','REP','RND','RNL','PSL','PBN','PVB','SED','TPO','TSP','VOL','PIT','FIN','PLY','STA','PAN','TIC','WOB','FXO','FLX','GAT','RTG'];
const fxSrc = fs.readFileSync(path.join(ROOT, 'tools/seq-golden.mjs'), 'utf8'); const m = fxSrc.match(/const FXN = n => (\[[^\]]*\])/s); const FXLIST = eval(m[1]);
const FXN = n => FXLIST.indexOf(n); if (FXN('SLC') < 0 || FXN('STA') < 0) throw new Error('FXN');
function mkPad(secs, sr) { const n = Math.round(secs * sr), d = new Float32Array(n); let s = ri(1, 1e6); const r = () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296) - 0.5; const per = pick([0.1, 0.25, 0.3]);
  for (let i = 0; i < n; i++) { const ph = (i / sr) % per; d[i] = ph < 0.04 ? r() * Math.exp(-ph * 60) : r() * 0.01; } return d; }
const J = x => JSON.parse(JSON.stringify(x));
const tpl = J(JSON.parse(fs.readFileSync(path.join(ROOT, 'sounds/ELECTRO.thunder'), 'utf8')));
const cases = [];
for (let c = 0; c < Math.ceil(N / 7); c++) {
  const j = J(tpl), S = j.song, keep = ['song', 'chains', 'phrases', 'plen', 'insts']; for (const k of Object.keys(S)) if (!keep.includes(k) && JSON.stringify(S[k]).length > 4000) delete S[k];
  const t0 = S.insts[0];
  S.insts = []; for (let t = 0; t < 7; t++) { const I = { ...t0, pad: ri(0, 3), tsp: pick([0, 0, 0, -12, -5, 7, 12, ri(-24, 24)]), slc: pick([0, 2, 3, 4, 5, 7, 8, 16, 17, 31, 32, 63, 64, ri(2, 64)]), smode: ri(0, 1), sync: pick([0, 0, 1, 2, 3]),
      beats: pick([1, 2, 3, 4, 5, 7, 16, 64]), grain: ri(0, 4), ofs: pick([0, 0, 1, 64, 128, 255, ri(0, 255)]), rev: ri(0, 1) };
    if (rnd() < 0.15) delete I.beats; if (rnd() < 0.15) delete I.grain; if (rnd() < 0.2) { I.slc = 0; } S.insts.push(I); }
  S.bpm = ri(40, 255); S.lpb = pick([4, 4, 2, 8]); S.ntr = 8;
  const PH = [];
  for (let t = 0; t < 7; t++) { const rows = []; const ncols = 2;
    for (let y = 0; y < 32; y++) { const r = Array(12).fill(-1); if (y < 16 && rnd() < 0.75) { r[0] = pick([48, 48, 49, 50, ri(0, 127), ri(20, 47), ri(49, 100), ri(36, 72)]); r[5] = t; } else r[5] = y < 16 ? t : 0; // note, instrument col 5
      if (y < 16) for (let col = 0; col < ncols; col++) if (rnd() < 0.3) { const nm = pick(['SLC', 'SLC', 'STA', 'STA', 'KIL', 'TPO', 'RET', 'PLY', 'PIT', 'FIN', 'ARP', 'PBN', 'PSL', 'OFF', 'TSP']); const v = nm === 'TPO' ? ri(30, 255) : nm === 'KIL' || nm === 'OFF' ? ri(0, 6) : ri(0, 255);
        r[6 + 2 * col] = FXN(nm); r[7 + 2 * col] = v; }
      rows.push(r); } PH.push(rows); }
  S.phrases = PH; S.plen = PH.map(() => 16); S.chains = PH.map((_, t) => [[t, 0]]); S.song = Array.from({ length: 256 }, (_, r) => Array.from({ length: 33 }, (_, t) => r < 4 && t < 7 ? t : -1));
  // kit: random loops; pad data random length 0.05..3 s at 48000 / 44100 / 22050
  const kit = { voices: [] }; U.__pads.length = 0;
  for (let p = 0; p < 4; p++) { const sr = pick([48000, 44100, 22050]), secs = pick([0.05, 0.4, 1, 2, 3]); const L = (s2, sr2, tune, fine, a, delay) => ({ on: true, type: 'sample', sample: { data: new Float32Array(Math.round(s2 * sr2)), sr: sr2 }, tune, fine, a, delay });
    kit.voices[p] = { tune: ri(-3, 3), layers: rnd() < 0.5 ? [] : [L(pick([0.3, 1, 2, 4]), 44100, ri(-12, 12), ri(-50, 50), pick([0, 0.1, 0.5]), pick([0, 15, 300]))] }; U.__pads[p] = { data: mkPad(secs, sr), sr }; }
    // the real kit object lives in the vm: build it there from a descriptor
  const desc = kit.voices.map(v => ({ tune: v.tune, layers: v.layers.map(l => ({ on: l.on, type: l.type, tune: l.tune, fine: l.fine, a: l.a, delay: l.delay, n: l.sample.data.length, sr: l.sample.sr })) }));
  sctx.__desc = desc; vm.runInContext(`__u.setKit({voices: __desc.map(v => ({tune: v.tune, layers: v.layers.map(l => ({on: l.on, type: l.type, tune: l.tune, fine: l.fine, a: l.a, delay: l.delay, sample: {data: new Float32Array(l.n), sr: l.sr}}))}))}); __u.sliceMemo.clear(); __u.__pads.forEach((p, i) => __u.coreBufs.set(i, p));`, sctx);
  // the vm pads are the ones in __pads (made in this realm; Float32Array from node realm is accepted: .length is all loopSecs / findHits use besides indexing)
  const mode = pick(['song', 'song', 'song', 'sect', 'phrase']), track = ri(0, 6), opt = mode === 'song' ? { row: 0, loop: null, once: true, sim: false } : mode === 'sect' ? { track, sect: track, loop: null, once: true, sim: false } : { track, phrase: track, loop: null, once: true, sim: false };
  const pads = {}; for (let p = 0; p < 4; p++) { const P = U.__pads[p]; pads[p] = { loop: U.loopSecs(p, P.data, P.sr), hits: {} }; }
  for (const I of S.insts) { const n = I.slc | 0; if (n > 1 && I.smode) { const P = U.__pads[I.pad]; pads[I.pad].hits[n] = U.slicePoints(I.pad, { slc: n, smode: 1 }, P.data, P.sr); } }
  sctx.__S = S; sctx.__o = opt; sctx.__mode = mode;
  let rows; try { rows = vm.runInContext(`(() => { __u.load(__S); return __u.coreSeeded(() => { const out = []; const sq = __u.makeSeq(S, __mode, __o);
      for (let n = 0; sq.alive() && n < 400; n++){ const r = sq.step(); for (const e of r.evs) if (e.type === 'note') out.push({ t: e.t, track: e.track, pad: e.pad, rate: e.rate, ofs: e.ofs, ofsSec: e.ofsSec === undefined ? null : e.ofsSec, durSec: e.durSec === undefined ? null : e.durSec, cut: e.cut == null ? null : e.cut, rev: e.rev ? 1 : 0, st: e.st ? [e.st.mode, e.st.bpm, e.st.beats, e.st.grain] : null }); } return out; }); })()`, sctx); }
  catch (e) { console.error('js threw case', c, mode, e.stack.split('\n').slice(0,3).join(' | ')); continue; }
  cases.push({ id: c, mode, opt, song: S, pads, js: JSON.parse(JSON.stringify(rows, (k, v) => typeof v === 'number' && !isFinite(v) ? String(v) : v)) });
}
fs.writeFileSync(OUT, JSON.stringify({ cases }));
console.log('cases', cases.length, 'combos', cases.length * 7, 'js notes', cases.reduce((a, c) => a + c.js.length, 0), 'chopped', cases.reduce((a, c) => a + c.js.filter(n => n.ofsSec !== null).length, 0));
```

### vchop_diff.cpp
```cpp
// Scratch differential, C++ side: vchop_diff <cases.json> <out.json>. Reads the cases the JS side wrote, plays each with Seq (sim off, SeqChop from the JS's own loop / hits), prints the notes.
#include <cmath>
#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "rt/json.h"
#include "seq/seq.h"
#include "song/song.h"
using namespace thunder;
static std::string wr(Json v) { std::string o; json_write(v, o); return o; }
static void num(std::string& o, double v) { char b[64]; if (!std::isfinite(v)) { o += std::isnan(v) ? "\"NaN\"" : v > 0 ? "\"Infinity\"" : "\"-Infinity\""; return; } std::snprintf(b, sizeof b, "%.17g", v); o += b; }
int main(int argc, char** argv) {
  std::ifstream f(argv[1], std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); JsonDoc d; if (!d.parse(ss.str())) { std::puts("bad cases"); return 2; }
  const Json cs = d.root().get("cases"); std::string out = "[";
  for (size_t ci = 0; ci < cs.size(); ci++) {
    const Json C = cs.at(ci); SeqChop ch; const Json pads = C.get("pads");
    for (size_t i = 0; i < pads.size(); i++) { const int pad = std::atoi(std::string(pads.keyAt(i)).c_str()); const Json P = pads.valAt(i); if (!ch.setLoop(pad, P.num("loop"))) std::printf("case %zu: loop refused %g\n", ci, P.num("loop"));
      const Json H = P.obj("hits"); for (size_t k = 0; k < H.size(); k++) { const int n = std::atoi(std::string(H.keyAt(k)).c_str()); std::vector<double> pts; const Json A = H.valAt(k); for (size_t q = 0; q < A.size(); q++) pts.push_back(A.at(q).asNum()); if (!ch.setHits(pad, n, pts.data(), (int)pts.size())) std::printf("case %zu: hits refused\n", ci); } }
    const std::string st = wr(C.get("song")); auto song = std::make_shared<Song>(); JsonError je; if (!song->load(st.data(), st.size(), &je)) { std::printf("case %zu song load: %s\n", ci, je.msg); continue; }
    SeqOpts o; o.sim = false; o.once = true; o.chop = &ch; const std::string mode(C.str("mode")); const Json op = C.get("opt");
    SeqMode m = mode == "song" ? SeqMode::Song : mode == "sect" ? SeqMode::Sect : SeqMode::Phrase; o.track = (int)op.num("track"); if (m == SeqMode::Sect) o.sect = (int)op.num("sect"); if (m == SeqMode::Phrase) o.phrase = (int)op.num("phrase");
    SeqVars vars; Seq sq(*song, vars, m, o); std::vector<SeqEvent> evs(12288); SeqStep s2; out += ci ? ",[" : "[";
    bool first = true;
    for (int n = 0; sq.alive() && n < 400; n++) { const int k = sq.step(evs.data(), 12288, &s2);
      for (int i = 0; i < k; i++) { const SeqEvent& e = evs[i]; if (e.type != SEV_NOTE) continue; if (!first) out += ','; first = false; out += "[";
        num(out, e.t); out += ','; num(out, e.track); out += ','; num(out, e.pad); out += ','; num(out, e.rate); out += ','; num(out, e.ofs); out += ',';
        if (e.has & SH_CHOP) num(out, e.ofsSec); else out += "null"; out += ','; if (e.has & SH_CHOP) num(out, e.durSec); else out += "null"; out += ','; if (e.has & SH_CUT) num(out, e.cut); else out += "null"; out += ',';
        out += e.rev ? "1" : "0"; out += ','; if (e.has & SH_ST) { out += "["; num(out, e.stMode); out += ','; num(out, e.stBpm); out += ','; num(out, e.stBeats); out += ','; num(out, e.stGrain); out += "]"; } else out += "null"; out += "]"; } }
    out += "]"; std::printf("case %zu %s: chopped %llu skipped %llu nohits %llu dropped %llu\n", ci, mode.c_str(), (unsigned long long)sq.chopped(), (unsigned long long)sq.chopSkipped(), (unsigned long long)sq.chopNoHits(), (unsigned long long)sq.dropped());
  }
  out += "]"; std::ofstream(argv[2]) << out; return 0; }
```

### vchop-cmp.mjs
```js
// compare: node vchop-cmp.mjs cases.json cpp.json
import fs from 'fs'; const C = JSON.parse(fs.readFileSync(process.argv[2])).cases, X = JSON.parse(fs.readFileSync(process.argv[3]));
const K = ['t', 'track', 'pad', 'rate', 'ofs', 'ofsSec', 'durSec', 'cut', 'rev', 'st']; let bad = 0, tot = 0, loose = 0; const num = v => v === 'NaN' ? NaN : v === 'Infinity' ? Infinity : v === '-Infinity' ? -Infinity : v;
const eq = (a, b) => { a = num(a); b = num(b); if (Array.isArray(a) || Array.isArray(b)) return Array.isArray(a) && Array.isArray(b) && a.every((x, i) => eq(x, b[i])); return Object.is(a, b) || (a === b); };
const near = (a, b) => { a = num(a); b = num(b); if (Array.isArray(a)) return a.every((x, i) => near(x, b[i])); if (a === null || b === null) return a === b; return Math.abs(a - b) <= 1e-9 * Math.max(1, Math.abs(a)); };
C.forEach((c, i) => { const js = c.js, cp = X[i]; if (!cp) { console.log('case', i, 'no cpp'); bad++; return; }
  if (js.length !== cp.length) { console.log(`case ${i} (${c.mode}): note count js ${js.length} cpp ${cp.length}`); bad++; return; }
  js.forEach((n, q) => { tot++; const row = cp[q], diffs = []; K.forEach((k, j) => { if (!eq(n[k], row[j])) diffs.push(k + ' js=' + JSON.stringify(n[k]) + ' cpp=' + JSON.stringify(row[j])); });
    if (diffs.length) { if (diffs.every((d, _) => true) && K.every((k, j) => near(n[k], row[j]))) { loose++; } else { bad++; if (bad < 15) console.log(`MISMATCH case ${i} ${c.mode} opt ${JSON.stringify(c.opt)} note ${q} track ${n.track}: ${diffs.join('; ')}\n   inst ${JSON.stringify(c.song.insts[n.track])}`); } } }); });
console.log(`notes compared ${tot}, exact mismatches beyond 1e-9: ${bad}, only-last-ulp differences: ${loose}`);
```

### vchop_render.cpp
```cpp
// Scratch render check: a RAMP pad (value = frame / len), slc 4, notes through the C API at pad rates 22050 / 44100 / 48000 / 96000, rate 1 / 2 (SLC) / reversed; measures where each note starts
// (ramp value 10 ms in, scaled by a constant pad's gain) and where it ends (first frame < 50 % of its plateau after the cut), blocks 1 / 333 / 4096 bit-identical.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "thunder/render.h"
static const int OSR = 48000;
// one note at row 0 of a 32 row phrase (0.125 s a row at 120 bpm / lpb 4); note n, optional SLC value, optional PLY 1
static std::string song(int note, int slcv, bool rev, int slc) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[32],\"insts\":[{\"pad\":0,\"slc\":" + std::to_string(slc) + "}],\"phrases\":[[";
  for (int y = 0; y < 32; y++) { s += y ? "," : ""; if (y == 0) { s += "[" + std::to_string(note) + ",-1,-1,-1,-1,0," + (slcv >= 0 ? "65," + std::to_string(slcv) : "-1,-1") + "," + (rev ? "24,1" : "-1,-1") + ",-1,-1]"; } else s += "[-1,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"; }
  return s + "]]}";
}
static bool render(const std::string& sg, const std::vector<float>& pad, double sr, double loop, int block, std::vector<float>& L, std::vector<float>& R, int frames) {
  thunder_render_opts op; std::memset(&op, 0, sizeof op); op.size = sizeof op; op.use_frames = 1; op.frames = frames; char err[200];
  thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &op, err, sizeof err); if (!r) { std::printf("create: %s\n", err); return false; }
  if (thunder_render_set_pad(r, 0, pad.data(), nullptr, (int64_t)pad.size(), sr)) { std::puts("set_pad"); return false; } if (loop > 0 && thunder_render_set_pad_chop(r, 0, loop)) { std::puts("set_chop"); return false; }
  L.assign(frames, 0); R.assign(frames, 0); int64_t got = 0; while (got < frames) { int64_t n = thunder_render_read(r, L.data() + got, R.data() + got, std::min<int64_t>(block, frames - got)); if (n <= 0) break; got += n; }
  thunder_render_free(r); return got == frames; }
int main() {
  const int F = 3 * OSR; int bad = 0;
  for (double psr : {22050.0, 44100.0, 48000.0, 96000.0}) {
    const double loop = 1.0; const int len = (int)(psr * 1.0); std::vector<float> ramp(len), dc(len, 0.5f); for (int i = 0; i < len; i++) ramp[i] = (float)i / len;
    struct C { const char* name; int note, slcv; bool rev; int slc; double rate; };
    const C cs[] = {{"slc4 k=1 rate1 (note 49)", 49, -1, false, 4, 1}, {"slc4 k=3 (note 51)", 51, -1, false, 4, 1}, {"SLC 1 note 60 rate 2", 60, 1, false, 4, 2}, {"SLC 2 note 36 rate .5", 36, 2, false, 4, 0.5}, {"slc4 k=1 reversed", 49, -1, true, 4, 1}, {"slc 7 k=5 (note 53)", 53, -1, false, 7, 1}};
    for (const C& c : cs) {
      const std::string sg = song(c.note, c.slcv, c.rev, c.slc); std::vector<float> L, R, Ld, Rd; if (!render(sg, ramp, psr, loop, 4096, L, R, F) || !render(sg, dc, psr, loop, 4096, Ld, Rd, F)) { bad++; continue; }
      double g = 0; for (int i = 1000; i < 5000; i++) g += std::fabs(Ld[i]); g /= 4000 * 0.5;   // the mixer's gain on a 0.5 constant (the plateau is the whole slice)
      const int k = c.slcv >= 0 ? c.slcv % c.slc : (((c.note - 48) % c.slc) + c.slc) % c.slc; const double s0 = loop * k / c.slc, s1 = loop * (k + 1) / c.slc, dur = s1 - s0;
      // where does the output start: the ramp value at output time tau = 0.02 s, expected (s0 + tau * rate) / loop for forward (ramp over the pad's `loop` = 1 s)
      const int a = (int)(0.02 * OSR); const double v = L[a] / g, expect = c.rev ? (1.0 - (s0 + 0.02 * c.rate)) : (s0 + 0.02 * c.rate) / 1.0;   // reversed: the ramp read backwards from the far end of the slice
      double expRev = (s1 - 0.02 * c.rate);   // reversed: starts at the slice's end and runs backwards
      const double want = c.rev ? expRev : expect;
      // end: last frame whose |x| > 0.5 * |x at 0.02 s|-ish is unreliable on a ramp: use the DC pad (constant), the first frame after 0.5*dur/rate where |x| < 0.5 * plateau
      const double plateau = std::fabs(Ld[a]); int endF = -1; for (int i = a; i < F; i++) if (std::fabs(Ld[i]) < 0.5 * plateau) { endF = i; break; }
      const double wantEnd = dur / c.rate * OSR;
      const bool ok = std::fabs(v - want) < 2e-3 && endF > 0 && std::fabs(endF - wantEnd) < 0.006 * OSR;
      if (!ok) bad++;
      std::printf("%s pad %5.0f Hz %-26s: start ramp %.5f want %.5f | end frame %d want %.1f (diff %+.1f frames)\n", ok ? "ok  " : "DIFF", psr, c.name, v, want, endF, wantEnd, endF - wantEnd);
      for (int b : {1, 333}) { std::vector<float> L2, R2; render(sg, ramp, psr, loop, b, L2, R2, F); if (L2 != L || R2 != R) { std::printf("  BLOCK %d differs from 4096\n", b); bad++; } }
    } }
  std::printf("bad %d\n", bad); return 0; }
```

### vchop_hostile.cpp
```cpp
// Scratch hostile run (ASan+UBSan build): unsorted / duplicate / beyond-the-buffer hits, count 1 of n 64, slc 64 with SMODE, loop far beyond the pad, loop tiny, pad replaced after chop, rev + sync 2, all rendered; checks finite output.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "thunder/render.h"
static std::string song(const std::string& inst, bool rev) { std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[32],\"insts\":[" + inst + "],\"phrases\":[[";
  for (int y = 0; y < 32; y++) { s += y ? "," : ""; const bool h = y % 2 == 0 && y < 16; s += "[" + std::to_string(h ? 30 + y * 4 : -1) + ",-1,-1,-1,-1,0," + (h && y % 4 == 0 ? "65," + std::to_string(y * 9) : "-1,-1") + "," + (h && rev ? "24,1" : "-1,-1") + ",-1,-1]"; } return s + "]]}"; }
int main() { int bad = 0, runs = 0; const int SR = 48000;
  struct H { const char* name; int n; std::vector<double> p; };
  const std::vector<H> hs = {{"unsorted", 4, {1.5, 0.2, 0.9, 0}}, {"dups", 4, {0.5, 0.5, 0.5, 0.5}}, {"beyond", 4, {0, 1, 500000, 999999}}, {"one of 64", 64, {0.3}}, {"all 64 descending", 64, {}}, {"zero pts", 4, {0, 0, 0, 0}}};
  for (double loop : {1e-6, 0.01, 1.0, 2.0, 1e6}) for (int ph = 0; ph < 2; ph++) for (const H& h : hs) for (int rev = 0; rev < 2; rev++) for (int sync : {0, 2}) for (int ps : {0, 1}) {
    std::vector<double> pts = h.p; if (h.n == 64 && pts.empty()) for (int i = 0; i < 64; i++) pts.push_back(64 - i);
    const std::string sg = song("{\"pad\":0,\"slc\":" + std::to_string(h.n) + ",\"smode\":1,\"sync\":" + std::to_string(sync) + ",\"beats\":3}", rev);
    thunder_render_opts op; std::memset(&op, 0, sizeof op); op.size = sizeof op; op.use_frames = 1; op.frames = 2 * SR; char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &op, err, sizeof err); if (!r) { bad++; continue; }
    const int len = ps ? 100 : 96000; std::vector<float> p(len); for (int i = 0; i < len; i++) p[i] = std::sin(i * 0.05f);
    thunder_render_set_pad(r, 0, p.data(), nullptr, len, SR); int a = thunder_render_set_pad_chop(r, 0, loop); thunder_render_set_pad_hits(r, 0, h.n, pts.data(), (int)pts.size());
    if (ph) { thunder_render_set_pad(r, 0, p.data(), nullptr, len / 2, SR); }   // replaced AFTER the chop data: it must be dropped (skipped notes), not stale
    std::vector<float> L(2 * SR), R(2 * SR); int64_t got = 0; while (got < 2 * SR) { int64_t n = thunder_render_read(r, L.data() + got, R.data() + got, 1000); if (n <= 0) break; got += n; }
    bool fin = got == 2 * SR; for (int i = 0; i < 2 * SR; i++) if (!std::isfinite(L[i]) || !std::isfinite(R[i])) fin = false; runs++;
    thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; thunder_render_stats_get(r, &s);
    if (ph && s.seq_chopped) { std::printf("STALE: chopped %llu after set_pad (loop %g %s)\n", (unsigned long long)s.seq_chopped, loop, h.name); bad++; }
    if (!fin || (a != 0 && loop >= 1e-6 && loop <= 1e6)) { std::printf("BAD fin=%d a=%d loop %g %s\n", fin, a, loop, h.name); bad++; }
    thunder_render_free(r); }
  std::printf("runs %d bad %d\n", runs, bad); return bad ? 1 : 0; }
```

