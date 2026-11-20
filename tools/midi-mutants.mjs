#!/usr/bin/env node
// U35: hand mutation of native/core/src/midi/midisched.cpp. Each mutant is one exact text replacement (it must match exactly once, so a refactor of the source
// is noticed, not ignored); the mutated file is compiled in place of midisched.o and the three MIDI tests are linked and run against it. A mutant that no test
// fails is a SURVIVOR and the script exits 1: every R-M rule (docs/port/U35-NOTES.md Part 1 item 3) and every fix (B1..B9, B11) has one.
//   node tools/midi-mutants.mjs --lib native/build-u35/core/libthunder_core.a [--only R-M5] [--list] [--table]
// It needs g++ and a built libthunder_core.a (the mutant is found first by the linker, so the library's own midisched.o is not pulled in).
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';
import { execFileSync, spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
const opt = (n, d) => { const i = args.indexOf(n); return i < 0 ? d : args[i + 1]; };
const SRC = path.join(ROOT, 'native/core/src/midi/midisched.cpp');
const LIB = path.resolve(opt('--lib', path.join(ROOT, 'native/build-u35/core/libthunder_core.a')));
const ONLY = opt('--only', null);
const text = fs.readFileSync(SRC, 'utf8');

// [id, what, old, new, (nth occurrence, default the only one)]
const M = [
  ['R-M1', 'FX latency not added', 'double at = hasT ? t + fxLat_ - latSec(port) : now_;', 'double at = hasT ? t - latSec(port) : now_;'],
  ['R-M2', 'Gear delay not applied to notes', 'double at = hasT ? t + fxLat_ - latSec(port) : now_;', 'double at = hasT ? t + fxLat_ : now_;'],
  ['R-M3/B3', 'the clock / Start / join ignore the Gear delay', '  if (at < now_ && kind != MK_CLOCK)', '  if (kind == MK_CLOCK || kind == MK_START || kind == MK_JOIN) at += latSec(port);\n  if (at < now_ && kind != MK_CLOCK)'],
  ['R-M4', 'a late message is not clamped to now', 'if (at < now_ && kind != MK_CLOCK) at = now_;', ''],
  ['R-M5', 'a port with no cable still sends', 'if (port < 0 || port >= kMidiPorts || !conn_[port]) return;', 'if (port < 0 || port >= kMidiPorts) return;'],
  ['R-M6', 'a note-off sent as 0x90', 'const uint8_t b[3] = {(uint8_t)(0x80 | h.ch), h.n, 0};\n  send(b, 3, std::max(at, h.t + 0.001), true, h.p, MK_EV, 0);', 'const uint8_t b[3] = {(uint8_t)(0x90 | h.ch), h.n, 0};\n  send(b, 3, std::max(at, h.t + 0.001), true, h.p, MK_EV, 0);'],
  ['R-M7', 'velocity scale', 'const int vel = clampi(rnd(e.vel * 127), 1, 127);', 'const int vel = clampi(rnd(e.vel * 100), 1, 127);'],
  ['R-M8/B1', 'a silent note does not end the mono note', 'if (!e.poly) moOff(t, e.t); return 0; }', 'return 0; }'],
  ['R-M9a', 'channel mask', 'ch = I.mch & 15, rng = clampi(I.pbr', 'ch = I.mch & 7, rng = clampi(I.pbr'],
  ['R-M9b', 'port clamp', 'const int t = e.track, p = clampi(I.mpt, 0, kMidiPorts - 1), ch', 'const int t = e.track, p = clampi(I.mpt, 0, 1), ch'],
  ['R-M10', 'mono: a new note does not end the track\'s notes', '  if (!e.poly) moOff(t, e.t);\n  else {', '  if (!e.poly) {}\n  else {'],
  ['R-M11a', 'note end ignores the OFF (rel)', 'const double endE = std::min(cut, rel);', 'const double endE = cut;'],
  ['R-M11b', 'note length (mlen) ignored', 'const double end = std::min(endE, (I.mlen > 0 && e.tick > 0) ? e.t + I.mlen * e.tick : kInf);', 'const double end = endE;'],
  ['R-M12', 'an OFF event does not end the track\'s notes', 'if (e.has & SH_COLS) moOffCols(e.track, e.cols, e.t); else moOff(e.track, e.t);', 'if (e.has & SH_COLS) moOffCols(e.track, e.cols, e.t);'],
  ['R-M13', 'poly cap 8 instead of 4', 'moNote(e, I, arp, bpm, 4);', 'moNote(e, I, arp, bpm, 8);'],
  ['R-M14/B4a', 'no per-pitch count: an OFF always goes out', 'if (cnt_[idx] > 0 && --cnt_[idx] > 0) { st_.offsSuppressed++; return; }', 'if (cnt_[idx] > 0) cnt_[idx]--;'],
  ['B4b', 'a retrigger of a held pitch sends no OFF first', 'push(m.port, o, 3, fr, m.kind, 0, m.cable);   // off, then on', '(void)o;'],
  ['B4c', 'a collision is not counted', 'if (owner_[idx] != (int8_t)m.track) st_.collisions++; else st_.sameTrackRetrig++;', 'st_.sameTrackRetrig++;'],
  ['R-M15', 'a pad column (mo 0) does not end the gear\'s note', 'if (!I.mo) { moOff(e.track, e.t); return; }', 'if (!I.mo) return;'],
  ['R-M16', 'a muted track still plays', 'if (!audibleT(e.track)) { moOff(e.track, e.t); return; }                      // a muted', 'if (false) { return; }                      // a muted'],
  ['R-M17', 'PGM not 1 ms early', 'send(b, 2, e.t - 0.001, true, p, MK_EV, 0xFF); }\n  else if (e.type == SEV_MPB)', 'send(b, 2, e.t, true, p, MK_EV, 0xFF); }\n  else if (e.type == SEV_MPB)'],
  ['R-M18', 'default CC numbers', 'static const int kCcDef[4] = {74, 71, 1, 7};', 'static const int kCcDef[4] = {74, 71, 1, 8};'],
  ['R-M19a', 'CSL glide steps every 20 ms', 'n < 40; n++, r.at += kLfoDt) {\n      const int v = rnd(r.a', 'n < 40; n++, r.at += 2 * kLfoDt) {\n      const int v = rnd(r.a'],
  ['R-M19b', 'CSL: a new value does not cut the glide', 'glide_[i].t1 = e.t; glide_[i].cut = true;', ''],
  ['R-M20', 'pitch bend repeated', 'if (L.has && L.v == v && tt >= L.t) return;', ''],
  ['R-M21', 'ramp steps', 'const int n = ceilClamp((t1 - t0) / 0.01, 1, 48);', 'const int n = ceilClamp((t1 - t0) / 0.01, 1, 24);'],
  ['R-M22', 'ARP: no return to the own pitch', 'const bool pend = hasBack && back < end - 1e-6;', 'const bool pend = false;'],
  ['R-M23', 'ECHO length rule', 'std::max(0.005, std::min(dt * 0.9, end < kInf ? end - e.t : dt * 0.9))', 'std::max(0.005, std::min(dt * 0.8, end < kInf ? end - e.t : dt * 0.8))'],
  ['R-M24', 'LFO rate list', 'const double rate = kLfoRates[ri];', 'const double rate = kLfoRates[ri > 0 ? ri - 1 : 0];'],
  ['R-M25', 'clock ticks per beat', 'timeOfBeat((double)clk_.k / 24, seqTime, bpm, ok)', 'timeOfBeat((double)clk_.k / 12, seqTime, bpm, ok)'],
  ['R-M26', 'Start byte', 'const uint8_t c = 0xFA; sendClk(&c, 1, e.t, MK_START);', 'const uint8_t c = 0xFB; sendClk(&c, 1, e.t, MK_START);'],
  ['R-M27', 'Stop byte', 'const uint8_t b = 0xFC; push(p, &b, 1, f, MK_STOP, 0);', 'const uint8_t b = 0xFD; push(p, &b, 1, f, MK_STOP, 0);'],
  ['R-M28', 'a port lit mid-song does not join', 'if (clk_.on) joinPort(p, false, now);', 'if (clk_.on) {}'],
  ['R-M30', 'STOP does not clear what is queued', 'if (!playing) {\n    nPend_ = 0;', 'if (false) {\n    nPend_ = 0;'],
  ['R-M31', 'no outbox: everything goes out at booking', 'if (hasT && at - now_ > lead_) pendInsert(m); else emit(m);', 'emit(m);'],
  ['R-M32', 'live key offset', 'e.t = now + latSec(clampi(I.mpt, 0, kMidiPorts - 1)) + 0.001;', 'e.t = now + latSec(clampi(I.mpt, 0, kMidiPorts - 1)) + 0.002;'],
  ['R-M33', 'PANIC without All Notes Off', 'for (int cc : {64, 120, 123})', 'for (int cc : {64, 120})'],
  ['R-M34/B6', 'STOP does not forget the used channels', 'ccv_[i] = -1; used_ = 0; }', 'ccv_[i] = -1; }'],
  ['B5', 'STOP does not forget the last CC values', 'for (int i = 0; i < kMidiPorts * 16 * 128; i++) ccv_[i] = -1; used_ = 0; }', 'used_ = 0; }'],
  ['R-M35', 'PLAY does not reset the tempo map', 'now_ = now; nAnc_ = 0; haveT0_ = false; t0_ = 0; playing_ = true;', 'now_ = now; haveT0_ = false; t0_ = 0; playing_ = true;'],
  ['R-M36', 'a pad instrument sends CC', 'if (!I.mo || !(e.track >= 32 || audibleT(e.track))) return;', 'if (!(e.track >= 32 || audibleT(e.track))) return;'],
  ['R-M38', 'FX latency cached (only the first value counts)', 'void MidiSched::setFxLatency(double sec) { fxLat_ =', 'void MidiSched::setFxLatency(double sec) { if (fxLat_ != 0) return; fxLat_ ='],
  ['R-M40', 'equal times reorder', 'if (pend_[mid].at <= m.at) lo = mid + 1;', 'if (pend_[mid].at < m.at) lo = mid + 1;'],
  ['B2', 'ticks from the sequencer\'s own (retroactive) beat', 'const double b = nAnc_ ? anc_[nAnc_ - 1].b + (evs[i].t - anc_[nAnc_ - 1].t) * anc_[nAnc_ - 1].bpm / 60 : evs[i].v;', 'const double b = evs[i].v;'],
  ['B3b', 'Stop without the Gear delay', 'int64_t f = frameOf(now + fxLat_ - latSec(p));', 'int64_t f = frameOf(now + fxLat_);'],
  ['B7a', 'a late tick is sent', 'if (np && worst < now - kClockLateSec) {', 'if (false) {'],
  ['B7b', 'a tick in the outbox past its time is sent', 'if (pend_[i].kind == MK_CLOCK && pend_[i].at < now - kClockLateSec) st_.lateTicks++;', 'if (false) st_.lateTicks++;'],
  ['B7c', 'a late tick is clamped to now (the burst)', 'if (at < now_ && kind != MK_CLOCK)', 'if (at < now_)'],
  ['B7d', '512-tick cap', 'for (int n = 0; n < 4096; n++) {\n    bool ok;', 'for (int n = 0; n < 512; n++) {\n    bool ok;'],
  ['B7e', 'a late note is played', 'if (late) { st_.lateNotes++;', 'if (false) { st_.lateNotes++;'],
  ['B7f', 'ticks older than the oldest anchor stall the clock', 'if (clk_.k < first && anc_[0].t < now) { st_.lateTicks += (uint64_t)(first - clk_.k); clk_.k = first; }', ''],
  ['B8', 'Stop before the last tick sent', 'if (tf > f) f = tf; break;', 'break;'],
  ['B9a', 'an OFF for a note whose ON was never sent', 'if (cnt_[idx] == 0) { st_.offsSuppressed++; return; }', ''],
  ['B9b', 'release sends an OFF for a note not yet on', 'if (conn_[p] && onFrame_[idx] <= nf) {', 'if (conn_[p]) {'],
  ['B9c', 'release does not end a sounding note (relies on CC 123)', 'if (cnt_[idx] > 0) { if (conn_[p]', 'if (false) { if (conn_[p]'],
  ['R-M32b', 'live keys share one column (the web names them with a counter that never goes down)', 'e.col = keyCol_++;', 'e.col = 1000;'],
];

if (args.includes('--list')) { for (const [id, what] of M) console.log(id.padEnd(10), what); process.exit(0); }
if (args.includes('--check')) { let bad = 0; for (const [id, , from] of M) { const n = text.split(from).length - 1; if (n !== 1) { console.log(id + ': ' + n + ' matches'); bad++; } } console.log(bad ? bad + ' mutants do not match' : 'all ' + M.length + ' mutants match exactly once'); process.exit(bad ? 1 : 0); }
if (!fs.existsSync(LIB)) { console.error('no library at ' + LIB + ' (build native/ first, or pass --lib)'); process.exit(2); }
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'midimut-'));
const inc = ['-I' + path.join(ROOT, 'native/core/src'), '-I' + path.join(ROOT, 'native/core/include'), '-I' + path.join(ROOT, 'native/core/third_party'), '-I' + path.join(ROOT, 'native/tests')];
const CXX = ['-std=c++17', '-O1', ...inc, '-DTHUNDER_ROOT="' + ROOT + '"'];
const run = (cmd, a, o = {}) => spawnSync(cmd, a, { encoding: 'utf8', ...o });
// the tests, compiled once
const tests = ['test_midi_ledger', 'test_midi_sched', 'test_midi_api'];
for (const t of tests) { const r = run('g++', [...CXX, '-c', path.join(ROOT, 'native/tests', t + '.cpp'), '-o', path.join(tmp, t + '.o')]); if (r.status) { console.error(r.stderr); process.exit(2); } }
let survivors = 0; const table = [];
for (const [id, what, from, to, nth] of M) {
  if (ONLY && id !== ONLY) continue;
  const n = text.split(from).length - 1;
  if (n !== 1) { console.error(`mutant ${id}: the text to replace occurs ${n} times (must be exactly 1): the source moved, update tools/midi-mutants.mjs`); process.exit(2); }
  const mutSrc = path.join(tmp, 'midisched_mut.cpp'); fs.writeFileSync(mutSrc, text.replace(from, () => to));
  let r = run('g++', [...CXX, '-c', mutSrc, '-o', path.join(tmp, 'mut.o')]);
  if (r.status) { console.log(`${id.padEnd(10)} DOES NOT COMPILE (a compile failure is not a kill): ${what}\n${r.stderr.split('\n').slice(0, 5).join('\n')}`); survivors++; continue; }
  let killedBy = null;
  for (const t of tests) {
    const exe = path.join(tmp, t);
    r = run('g++', ['-o', exe, path.join(tmp, t + '.o'), path.join(tmp, 'mut.o'), LIB, '-lpthread']);
    if (r.status) { console.error(r.stderr.split('\n').slice(0, 8).join('\n')); process.exit(2); }
    const x = run(exe, [], { timeout: 300000 });
    if (x.status !== 0) { const first = (x.stdout.split('\n').find(l => l.startsWith('FAIL')) || (x.status === null ? 'timeout / crash' : 'exit ' + x.status)).slice(0, 110); killedBy = t + ': ' + first; break; }
  }
  if (!killedBy) { survivors++; console.log(`${id.padEnd(10)} SURVIVED: ${what}`); table.push([id, what, 'SURVIVED']); }
  else { console.log(`${id.padEnd(10)} killed by ${killedBy}   (${what})`); table.push([id, what, killedBy]); }
}
fs.rmSync(tmp, { recursive: true, force: true });
console.log(`${M.length} mutants, ${survivors} survivors`);
process.exit(survivors ? 1 : 0);
