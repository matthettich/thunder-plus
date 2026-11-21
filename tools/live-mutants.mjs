#!/usr/bin/env node
// LIVE ROUTE: hand mutation of the live pool, the shell, and the song-end release (verify-265 F5: "the builder's mutants are not in the repo"). Each mutant is one exact text replacement (it must match
// exactly once, so a refactor is noticed, not ignored); the mutated file is compiled in place of its object (a header mutant: the pool's and the shell's TUs are compiled against the mutated header) and the live tests
// are linked and run against it. A mutant that no test fails is a SURVIVOR and the script exits 1.
//   node tools/live-mutants.mjs --build native/build [--only X08] [--list] [--check]
// It needs a built native/ tree (the tests' objects and libthunder_core.a; the compile flags and link lines are read from the build directory).
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(n); return i < 0 ? d : args[i + 1]; };
const BUILD = path.resolve(opt('--build', path.join(ROOT, 'native/build')));
const ONLY = opt('--only', null);
const SRC = { pool: 'native/core/src/engine/live_pool.cpp', shell: 'native/core/src/engine/live_shell.h', render: 'native/core/src/render/render.cpp', vox: 'native/core/src/engine/vox_core.cpp', engt: 'native/core/src/engine/eng_targets.h' };
const HDR = { shell: 'live_shell.h', engt: 'eng_targets.h' };   // the header mutants: compiled against a mutated copy found first on the include path
const text = Object.fromEntries(Object.entries(SRC).map(([k, v]) => [k, fs.readFileSync(path.join(ROOT, v), 'utf8')]));

// [id, what, file, old, new]
const M = [
  ['X04', 'polyVox: the same-column pick is disabled', 'pool', 'b->curCol == e.col && b->lastOn < e.t - 0.004) pick', 'false && b->curCol == e.col && b->lastOn < e.t - 0.004) pick'],
  ['X23', 'polyVox: the same-column pick ignores the 4 ms rule', 'pool', 'b->curCol == e.col && b->lastOn < e.t - 0.004) pick', 'b->curCol == e.col) pick'],
  ['X08', 'the timeline\'s forced off discards nothing', 'pool', 'if (n.tl[i].T < e.T) n.tl[w++] = n.tl[i]; else st_.queueDropped++; }', 'n.tl[w++] = n.tl[i]; }'],
  ['X08b', 'a full timeline drops the note-off too', 'pool', 'if (e.type != LiveEvType::NoteOff && e.type != LiveEvType::End) { st_.queueDropped++; return; }', '{ st_.queueDropped++; return; }'],
  ['X10', 'tempo() is a no-op', 'pool', 'for (Node* n : active_) if (n->kind == LiveKind::Bass) { bookCtl(*n, LiveEvType::Bpm, t, bpm); n->lastBpm = bpm; }', ''],
  ['X11', 'the bpm at a note is not booked', 'pool', 'std::isfinite(bpm) && bpm != n.lastBpm) {', 'std::isfinite(bpm) && false) {'],
  ['X12', 'tempo() one sample late', 'pool', 'bookCtl(*n, LiveEvType::Bpm, t, bpm); n->lastBpm = bpm; }\n}', 'bookCtl(*n, LiveEvType::Bpm, t + 1.0 / sr_, bpm); n->lastBpm = bpm; }\n}'],
  ['X17', 'D6: a mod with wob does not move the bass wobble', 'pool', 'bookCtl(n, LiveEvType::Wob, e.t, e.wob);', '(void)0;'],
  ['X20', 'C4: the quiet window is one sample', 'pool', 'n.quiet >= 4096', 'n.quiet >= 1'],
  ['X14', 'C4: the held voices are released 5 s after the song\'s end', 'render', 'players_->songOver(seq_->time());', 'players_->songOver(seq_->time() + 5);'],
  ['F3', 'D7: a slide cut by the next note holds the FROM pitch (the core\'s Auto::hold)', 'pool', 'holdRamp(n.rate, t);', 'n.rate.hold(t);'],
  ['F2a', 'the shell drops a note-off when its queue is full', 'shell', '      if (e.type != LiveEvType::NoteOff && e.type != LiveEvType::End) { dropped_++; return; }', '      { dropped_++; return; }'],
  ['F2b', 'the shell keeps the events after a forced off', 'shell', 'if (p.t < t) pend_[(head_ + w++) % kPend] = p; else dropped_++;', 'pend_[(head_ + w++) % kPend] = p;'],
  ['F2c', 'the shell does not count a forced off', 'shell', '      forced_++;\n', ''],
  ['F4a', 'a note\'s non-finite time is not refused', 'pool', 'if (!std::isfinite(e0.t) || !std::isfinite(e0.rate)', 'if (!std::isfinite(e0.rate)'],
  ['F4b', 'a non-finite `mul` is applied', 'pool', 'if ((e.has & SH_MUL) && !f(e.mul)) drop(SH_MUL);', ''],
  ['F4c', 'a non-finite bend is applied', 'pool', 'if ((e.has & SH_BEND) && (!f(e.bendMul) || !f(e.bendT1))) drop(SH_BEND);', ''],
  ['F4d', 'a non-finite vibrato is applied', 'pool', 'if ((e.has & SH_VIB) && (!f(e.vibSpd) || !f(e.vibDep) || !f(e.vibT1))) drop(SH_VIB);', ''],
  ['F4e', 'magnitudes are not clamped (gain 1e300)', 'pool', 'static inline double mag(double v) { return v < -1e6 ? -1e6 : v > 1e6 ? 1e6 : v; }', 'static inline double mag(double v) { return v; }'],
  ['F4f', 'refusals are not counted', 'pool', 'auto drop = [&](uint32_t bit) { e.has &= ~bit; st_.refused++; };', 'auto drop = [&](uint32_t bit) { e.has &= ~bit; };'],
  ['F4g', 'a non-finite tempo is applied', 'pool', 'if (!std::isfinite(bpm) || !std::isfinite(t)) { st_.refused++; return; }', ''],
  // ENGINE TARGETS (1b)
  ['E01', 'an eng event puts nothing on the nodes', 'pool', 'engPut(*n, e.engC, e.engO, at, true); }', '(void)0; }'],
  ['E02', 'the track\'s engine state is not kept (a node bound later starts from the defaults)', 'pool', 'es.has = !def;', 'es.has = false;'],
  ['E03', 'the offsets are SET at the event, not ramped', 'pool', 'engPut(*n, e.engC, e.engO, at, true); }', 'engPut(*n, e.engC, e.engO, at, false); }'],
  ['E04', 'a default eng reaches a node whose targets are off', 'pool', 'if (def && !n->engOn) continue;', ''],
  ['E05', 'stopAll keeps the track\'s engine state', 'pool', 'for (EngSt& s : engSt_) s = EngSt();', ''],
  ['E06', 'stopAll leaves the lanes', 'pool', 'A.cancel(at); A.set(k < 5 ? -1 : 0, at); }', '(void)A; }'],
  ['E07', 'a poly vox note does not bind the four nodes', 'pool', 'for (int j = 0; j < kVoicePoly; j++) nodes[j]->bound = true;', ''],
  ['E08', 'eng_targets: bass cut and res swapped', 'engt', 'engMapBass() { static const EngMap<BassP> M = {{&BassP::cut, &BassP::res,', 'engMapBass() { static const EngMap<BassP> M = {{&BassP::res, &BassP::cut,'],
  ['E09', 'eng_targets: vox atk and rel swapped', 'engt', '{{&VoxP::bright, nullptr, nullptr, &VoxP::atk, nullptr, nullptr, &VoxP::rel}', '{{&VoxP::bright, nullptr, nullptr, &VoxP::rel, nullptr, nullptr, &VoxP::atk}'],
  ['E10', 'eng_targets: acid cut upper bound 6000 -> 5000', 'engt', '{6000, 1.05, 1, 0, 4000, 1, 1000}', '{5000, 1.05, 1, 0, 4000, 1, 1000}'],
  ['E11', 'eng_targets: mach lp lower bound 60 -> 50', 'engt', '{60, 0, 0, 0.5, 1, 0, 1}', '{50, 0, 0, 0.5, 1, 0, 1}'],
  ['E12', 'the swap is not restored after the block', 'shell', 'if (sw) A::engOut(core_, sv);', ''],
  ['E13', 'the engine lanes are read one sample late', 'shell', 'engRead(*eng_, (double)s / feedSr_, v)', 'engRead(*eng_, (double)(s + 1) / feedSr_, v)'],
  ['E14', 'the swap-in formula: the k=-1 (no cutoff) test is dropped', 'engt', 'if (c < 0 && o == 0) continue;', ''],
  // IDLE SKIP (1b)
  ['I01', 'vox idleBlocks advances nothing (the pitch latch, the clocks)', 'vox', 'for (uint64_t i = 0; i < nb; i++) { st_ += n; step(c, dt); }', '(void)nb;'],
  ['I02', 'vox idleBlocks does not advance the pitch', 'vox', 'for (uint64_t i = 0; i < nb; i++) { st_ += n; step(c, dt); }', 'for (uint64_t i = 0; i < nb; i++) { st_ += n; vt_ += dt; }'],
  ['I03', 'fastForward leaves the rings as they were', 'shell', '      for (uint64_t i = 0; i < cnt; i++) { ringL_[(last - 1 - i) & (kRing - 1)] = 0.0f; ringR_[(last - 1 - i) & (kRing - 1)] = 0.0f; }\n', ''],
  ['I04', 'fastForward does one control block too many', 'shell', 'const uint64_t nb = H > rendered_ ? (H - rendered_) / kBlk : 0;', 'const uint64_t nb = H > rendered_ ? (H - rendered_) / kBlk + 1 : 0;'],
  ['I05', 'the skip does not look at the rate lane', 'pool', 'if (n.rate.constFrom(t0 > 0 ? (double)t0 / sr_ : 0.0, &rv)) {', 'if (true || n.rate.constFrom(t0 > 0 ? (double)t0 / sr_ : 0.0, &rv)) {'],
  ['I06', 'engPut does not wake the node', 'pool', 'void LivePool::engPut(Node& n, const double c[5], const double o[7], double at, bool ramp) {\n  wake(n);', 'void LivePool::engPut(Node& n, const double c[5], const double o[7], double at, bool ramp) {'],
  ['I07', 'a tempo / wobble event does not wake the node', 'pool', 'void LivePool::bookCtl(Node& n, LiveEvType type, double t, double v) {\n  wake(n);', 'void LivePool::bookCtl(Node& n, LiveEvType type, double t, double v) {'],
  ['I08', 'a mod does not wake the node', 'pool', 'void LivePool::modVoice(Node& n, const SeqEvent& e, const double* arp) {\n  wake(n);', 'void LivePool::modVoice(Node& n, const SeqEvent& e, const double* arp) {'],
  ['I09', 'a note does not wake the node before its lanes are written', 'pool', 'bool hasFk) {\n  wake(n);', 'bool hasFk) {'],
  ['I10', 'an off does not wake the node', 'pool', 'void LivePool::kill(Node& n, double at) {   // kill(v, at) for a live voice: the gate goes low at `at`; a stop before a booked later note takes that note back\n  wake(n);', 'void LivePool::kill(Node& n, double at) {'],
  ['I11', 'the skipped node is brought to the wrong clock (the last quantum\'s start)', 'pool', 'eng.fastForward((uint64_t)(qEnd_ + (int64_t)lat));', 'eng.fastForward((uint64_t)(lastQ0_ + (int64_t)lat));'],
  ['I12', 'a node that never ran starts at the wrong quantum', 'pool', 'n.skip = true; n.firstQ0 = q0; skipped_++; return; }', 'n.skip = true; n.firstQ0 = q0 + 128; skipped_++; return; }'],
  ['I13', 'a voice that has not ended may be skipped', 'pool', '&& (!n.curLive || n.done) && eng.idle()', '&& eng.idle()'],
  ['I14', 'wake does not prune the lanes', 'pool', '  eng.fastForward((uint64_t)(qEnd_ + (int64_t)lat));\n  pruneLanes(n, lastQ0_);', '  eng.fastForward((uint64_t)(qEnd_ + (int64_t)lat));'],
  ['I15', 'the vox idle test ignores a plan that still walks', 'vox', 'return !(g && g->d >= 0 && (double)si_ < (double)nsegs_ - 1);', 'return true;'],
  ['I16', 'the first call\'s pre-roll is not run at wake', 'pool', 'float* sR = scr_.get() + kQuantum; eng.process(sL, sR, lat, nullptr, 0); }', 'float* sR = scr_.get() + kQuantum; (void)sL; (void)sR; }'],
];

if (args.includes('--list')) { for (const [id, what] of M) console.log(id.padEnd(8), what); process.exit(0); }
if (args.includes('--check')) { let bad = 0; for (const [id, , f, from] of M) { const n = text[f].split(from).length - 1; if (n !== 1) { console.log(id + ': ' + n + ' matches'); bad++; } } console.log(bad ? bad + ' mutants do not match' : 'all ' + M.length + ' mutants match exactly once'); process.exit(bad ? 1 : 0); }

const run = (cmd, a, o = {}) => spawnSync(cmd, a, { encoding: 'utf8', maxBuffer: 1 << 26, ...o });
const flagsOf = f => { const t = fs.readFileSync(path.join(BUILD, 'core/CMakeFiles/thunder_core.dir/flags.make'), 'utf8'); const g = k => ((t.match(new RegExp('^' + k + ' = (.*)$', 'm')) || [])[1] || '').split(/\s+/).filter(Boolean); return [...g('CXX_DEFINES'), ...g('CXX_INCLUDES'), ...g('CXX_FLAGS')]; };
const CXXF = flagsOf();
const tests = ['test_live_pool', 'test_live_idle', 'test_live_tracklist', 'test_live_decisions', 'test_live_reset', 'test_live_review', 'test_live_jsparity'];
const tdir = path.join(BUILD, 'tests');
const linkOf = t => fs.readFileSync(path.join(tdir, 'CMakeFiles', t + '.dir/link.txt'), 'utf8').trim();
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'livemut-'));
let survivors = 0; const table = [];
for (const [id, what, file, from, to] of M) {
  if (ONLY && id !== ONLY) continue;
  const n = text[file].split(from).length - 1;
  if (n !== 1) { console.error(`mutant ${id}: the text to replace occurs ${n} times (must be exactly 1): the source moved, update tools/live-mutants.mjs`); process.exit(2); }
  const mutated = text[file].replace(from, () => to), objs = [];
  const cc = (src, extra, out) => { const r = run('c++', [...extra, ...CXXF, '-c', src, '-o', out]); return r; };
  let r;
  if (HDR[file]) {   // a header: the pool's TU and the shell's explicit instantiations are compiled against the mutated copy (found first on the include path)
    fs.mkdirSync(path.join(tmp, 'inc/engine'), { recursive: true }); fs.writeFileSync(path.join(tmp, 'inc/engine', HDR[file]), mutated);
    for (const f of ['live_pool.cpp', 'live_shell.cpp']) { const o = path.join(tmp, f + '.o'); r = cc(path.join(ROOT, 'native/core/src/engine', f), ['-I' + path.join(tmp, 'inc')], o); if (r.status) break; objs.push(o); }
  } else { const src = path.join(tmp, path.basename(SRC[file])); fs.writeFileSync(src, mutated); const o = src + '.o'; r = cc(src, [], o); objs.push(o); }
  if (r.status) { console.log(`${id.padEnd(8)} DOES NOT COMPILE (a compile failure is not a kill): ${what}\n${r.stderr.split('\n').slice(0, 5).join('\n')}`); survivors++; continue; }
  let killedBy = null;
  for (const t of tests) {
    let tobj = `CMakeFiles/${t}.dir/${t}.cpp.o`, extra = '';
    if (HDR[file] && t === 'test_live_review') {   // the test drives a LiveShell directly: its own copy of the template is compiled against the mutated header too
      const tf = fs.readFileSync(path.join(tdir, 'CMakeFiles', t + '.dir/flags.make'), 'utf8'), g = k => ((tf.match(new RegExp('^' + k + ' = (.*)$', 'm')) || [])[1] || '').split(/\s+/).filter(Boolean);
      const o = path.join(tmp, t + '.mut.o'); const c2 = run('c++', ['-I' + path.join(tmp, 'inc'), ...g('CXX_DEFINES'), ...g('CXX_INCLUDES'), ...g('CXX_FLAGS'), '-c', path.join(ROOT, 'native/tests', t + '.cpp'), '-o', o]);
      if (c2.status) { console.error(c2.stderr.split('\n').slice(0, 8).join('\n')); process.exit(2); } tobj = o; }
    const line = linkOf(t).replace(`CMakeFiles/${t}.dir/${t}.cpp.o`, `${tobj} ${objs.join(' ')}`).replace(` -o ${t} `, ` -o ${path.join(tmp, t)} `);
    const l = run('sh', ['-c', line], { cwd: tdir });
    if (l.status) { console.error(l.stderr.split('\n').slice(0, 8).join('\n')); process.exit(2); }
    const x = run(path.join(tmp, t), t === 'test_live_jsparity' ? [path.join(tdir, 'engine-u26-ref')] : t === 'test_live_decisions' ? [path.join(ROOT, 'native/tests/golden/live_route_log.json')] : [], { timeout: 600000 });
    if (x.status === 2) { console.error(t + ' exited 2 (a usage error): the script\'s arguments are wrong'); process.exit(2); }
    if (x.status !== 0) { const first = (x.stdout.split('\n').find(l2 => /^FAIL|differs/.test(l2)) || (x.status === null ? 'timeout / crash' : 'exit ' + x.status)).slice(0, 110); killedBy = t + ': ' + first; break; }
  }
  if (!killedBy) { survivors++; console.log(`${id.padEnd(8)} SURVIVED: ${what}`); table.push([id, what, 'SURVIVED']); }
  else { console.log(`${id.padEnd(8)} killed by ${killedBy}   (${what})`); table.push([id, what, killedBy]); }
}
fs.rmSync(tmp, { recursive: true, force: true });
console.log(`${M.length} mutants, ${survivors} survivors`);
process.exit(survivors ? 1 : 0);
