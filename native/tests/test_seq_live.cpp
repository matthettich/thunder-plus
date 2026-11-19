// SEQ-LIVE-DESIGN Part A: the sequencer's clock setters and the block runner (ctest `seq_live`).
//   1. the JS's live-clock goldens (native/tests/golden/seq-*-live*.json, made by tools/seq-golden.mjs): time set before the first step,
//      bpm changed mid-song, shift (the catch-up skip and RESUME's), all together on electro-cmds. Every row {t, evs, clk} and the sha256, exactly.
//   2. the setters' own rules (no base yet, refusals, clamp), the SeqRunner on a hand-built song, block sizes 1 / 128 / 4096.
//   3. hostile values to each setter (run it under ASan + UBSan: -DTHUNDER_SANITIZE=ON): finite and bounded, nothing hangs.
// The allocation guard around pump / setTime / setBpm / shift is in rt_harness (run_seq_runner).
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rt/json.h"
#include "seq/runner.h"
#include "seq/seq.h"
#include "song/song.h"

#include "seq_plain.h"
#include "sha256.h"

using namespace thunder;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static bool readFile(const std::string& path, std::string& out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); out = s.str(); return true; }

// tools/seq-golden.mjs's withCommands (see test_seq.cpp)
static std::string withCommands(const std::string& text) {
  static const int CMDS[16][2] = {{0, 0x37}, {11, 0x24}, {3, 0x03}, {43, 0x80}, {42, 0x41}, {2, 0x80}, {13, 0x24}, {20, 0x05}, {22, 0x0C}, {21, 0x90}, {26, 0x40}, {10, 0x02}, {8, 0x04}, {9, 0x06}, {27, 0x03}, {14, 0x13}};
  JsonDoc d; if (!d.parse(text)) return text;
  const Json root = d.root(); std::string o = "{";
  for (size_t i = 0; i < root.size(); i++) { if (i) o += ','; o += '"'; o += root.keyAt(i); o += "\":";
    if (root.keyAt(i) != "song") { json_write(root.valAt(i), o); continue; }
    const Json S = root.valAt(i); o += '{';
    for (size_t j = 0; j < S.size(); j++) { if (j) o += ','; o += '"'; o += S.keyAt(j); o += "\":";
      if (S.keyAt(j) != "phrases") { json_write(S.valAt(j), o); continue; }
      const Json P = S.valAt(j); o += '[';
      for (size_t p = 0; p < P.size(); p++) { if (p) o += ','; const Json ph = P.at(p); if (!ph.isArr()) { json_write(ph, o); continue; }
        o += '[';
        for (size_t y = 0; y < ph.size(); y++) { if (y) o += ','; const Json r = ph.at(y);
          if (!r.isArr() || r.size() < 12 || r.at(0).asNum() < 0 || r.at(10).asNum() >= 0) { json_write(r, o); continue; }
          const int* cv = CMDS[(p * 7 + y) % 16]; o += '[';
          for (size_t k = 0; k < r.size(); k++) { if (k) o += ','; if (k == 10) o += std::to_string(cv[0]); else if (k == 11) o += std::to_string(cv[1]); else json_write(r.at(k), o); }
          o += ']'; }
        o += ']'; }
      o += ']'; }
    o += '}'; }
  o += '}'; return o;
}

static bool loadSong(Song& s, const std::string& js) { JsonError je; return s.load(js.data(), js.size(), &je); }

// ---- 1. the goldens -------------------------------------------------------------------------------------------------------
static bool goldenCase(const std::string& root, const std::string& id, const std::string& electro, const std::string& cmds) {
  std::string gtext; if (!readFile(root + "/native/tests/golden/seq-" + id + ".json", gtext)) { CHECK(false, "%s: cannot read the golden", id.c_str()); return false; }
  JsonDoc gd; if (!gd.parse(gtext)) { CHECK(false, "%s: the golden is not JSON", id.c_str()); return false; }
  const Json G = gd.root(), RH = G.arr("rowHashes"), L = G.obj("live"), ops = L.arr("ops");
  Song song; if (!loadSong(song, id.rfind("electro-cmds", 0) == 0 ? cmds : electro)) { CHECK(false, "%s: the song does not load", id.c_str()); return false; }
  SeqVars vars; SeqOpts o; o.once = true; o.sim = true; Seq sq(song, vars, SeqMode::Song, o);
  if (L.get("time0").isNum()) sq.setTime(L.get("time0").asNum());
  const int CAP = 4096; std::vector<SeqEvent> evs((size_t)CAP); SeqStep st; Sha256 all; int n = 0, firstBad = -1; std::string badMine;
  for (; sq.alive() && n < 20000; n++) {
    const int k = sq.step(evs.data(), CAP, &st);
    std::string row = seqplain::row(st.t, evs.data(), k); row.pop_back();
    row += ",\"clk\":{\"time\":"; seqplain::num(row, sq.time()); row += ",\"base\":"; seqplain::num(row, sq.base()); row += ",\"bt\":"; seqplain::num(row, sq.bt());
    row += ",\"btT\":"; seqplain::num(row, sq.btT()); row += ",\"bpm\":"; seqplain::num(row, sq.bpm()); row += "}}";
    const std::string h = sha256_hex(row).substr(0, 12);
    if (firstBad < 0 && (n >= (int)RH.size() || h != RH.at((size_t)n).asStr())) { firstBad = n; badMine = row; }
    row += '\n'; all.add(row);
    for (size_t i = 0; i < ops.size(); i++) { const Json op = ops.at(i); if ((int)op.num("after") != n + 1) continue;
      if (op.get("bpm").isNum()) sq.setBpm(op.get("bpm").asNum());
      if (op.get("shift").isNum()) sq.shift(op.get("shift").asNum()); }
  }
  const bool ok = firstBad < 0 && n == (int)G.num("rows") && all.hex() == G.str("sha256") && sq.badSets() == 0 && sq.dropped() == 0;
  CHECK(ok, "%s: rows %d (golden %d), first differing row %d, refused sets %llu\n  C++ row: %s", id.c_str(), n, (int)G.num("rows"), firstBad, (unsigned long long)sq.badSets(), badMine.c_str());
  std::printf("%s %s: %d rows\n", ok ? "ok  " : "FAIL", id.c_str(), n);
  return ok;
}

// ---- 2. the setters and the runner ----------------------------------------------------------------------------------------
// A hand-built song: one track, a phrase of 16 rows (a note on rows 0, 4, 8, 12), `rows` song rows all playing chain 0. 120 bpm, 4 lines per beat:
// a step every 0.125 s exactly.
static std::string handSong(int songRows, int plen = 16) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[";
  for (int i = 0; i < songRows; i++) s += std::string(i ? "," : "") + "[0]";
  s += "],\"chains\":[[[0,0]]],\"plen\":[" + std::to_string(plen) + "],\"phrases\":[[";
  for (int y = 0; y < 32; y++) { s += y ? "," : ""; s += y % 4 == 0 && y < 16 ? "[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]" : "[-1,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"; }
  s += "]]}"; return s;
}
struct Fix {
  Song song; SeqVars vars; std::unique_ptr<Seq> sq; std::vector<SeqEvent> evs; SeqStep st;
  Fix(int songRows, bool once, double t0, int plen = 16) : evs(4096) { if (!loadSong(song, handSong(songRows, plen))) { std::printf("FAIL: the hand-built song does not load\n"); fails++; }
    SeqOpts o; o.once = once; o.sim = true; sq.reset(new Seq(song, vars, SeqMode::Song, o)); sq->setTime(t0); }
};
struct Got { std::vector<double> t; std::vector<std::string> rows; };

static void testSetters() {
  { Fix f(1, true, 0);   // before the first step: setTime sets time_ only, the base comes from it at the first step
    f.sq->setTime(1234.5);
    CHECK(f.sq->time() == 1234.5 && !f.sq->hasBase() && !f.sq->hasBtT(), "setTime: time, no base, no btT yet");
    f.sq->shift(0.25); CHECK(f.sq->time() == 1234.75 && !f.sq->hasBase(), "shift with no base moves time only");
    f.sq->step(f.evs.data(), 4096, &f.st);
    CHECK(f.sq->hasBase() && f.sq->base() == 1234.75 && f.st.t == 1234.75 && f.sq->hasBtT() && f.sq->btT() == 1234.75 && f.sq->bt() == 0, "the first step takes the base from time");
    CHECK(f.sq->time() == 1234.875, "the next step is due 0.125 later");
    const double b = f.sq->base(), tt = f.sq->time(), bt = f.sq->btT();
    f.sq->shift(-0.5); CHECK(f.sq->base() == b - 0.5 && f.sq->time() == tt - 0.5 && f.sq->btT() == bt - 0.5, "shift moves base, time and btT together");
    f.sq->setBpm(60); f.sq->step(f.evs.data(), 4096, &f.st);
    CHECK(f.sq->bpm() == 60 && f.sq->time() == tt - 0.5 + 0.25, "a bpm of 60 is heard from the next step (a line is 0.25 s)");
    CHECK(f.sq->badSets() == 0, "no refused sets so far"); }
  { Fix f(1, true, 0);   // refusals
    const double nan = std::nan(""), inf = INFINITY;
    f.sq->setBpm(nan); f.sq->setBpm(inf); f.sq->setBpm(-inf); f.sq->setBpm(0); f.sq->setBpm(-3); CHECK(f.sq->bpm() == 120 && f.sq->badSets() == 5, "setBpm: NaN, inf, -inf, 0, negative are ignored and counted (%llu)", (unsigned long long)f.sq->badSets());
    f.sq->setTime(nan); f.sq->setTime(inf); CHECK(f.sq->time() == 0 && f.sq->badSets() == 7, "setTime: NaN, inf ignored and counted");
    f.sq->shift(nan); f.sq->shift(-inf); CHECK(f.sq->time() == 0 && f.sq->badSets() == 9, "shift: NaN, -inf ignored and counted");
    f.sq->step(f.evs.data(), 4096, &f.st); f.sq->shift(DBL_MAX); f.sq->shift(DBL_MAX); CHECK(std::isfinite(f.sq->time()) && std::isfinite(f.sq->base()) && std::isfinite(f.sq->btT()) && f.sq->badSets() == 10, "shift: a second DBL_MAX would overflow: refused (%llu)", (unsigned long long)f.sq->badSets());
    f.sq->setBpm(1e9); CHECK(f.sq->bpm() == kSeqBpmMax, "setBpm clamps a huge value to kSeqBpmMax"); f.sq->setBpm(1e-9); CHECK(f.sq->bpm() == kSeqBpmMin, "setBpm clamps a tiny value to kSeqBpmMin"); }
}

// Plays `Fix` through a runner with `frames` per block at 48 kHz; the callback records each step. lead as given. Stops after `maxBlocks` or when `until` says so.
template <class U> static int blocks(Fix& f, SeqRunner& r, double now0, int frames, double lead, int maxBlocks, Got* g, U&& until) {
  double now = now0; int b = 0;
  for (; b < maxBlocks; b++) {
    const SeqPump p = r.pump(now, lead, f.evs.data(), 4096, &f.st, [&](const SeqEvent* e, int n, const SeqStep& s) { if (g) { g->t.push_back(s.t); g->rows.push_back(seqplain::row(s.t, e, n)); } });
    if (until(p)) { b++; break; }
    now += (double)frames / 48000.0;
  }
  return b;
}

static void testRunner() {
  const double none = -1;
  { // the catch-up skip: at time < now - 0.03, not at equality
    Fix f(1, true, 10); SeqRunner r; r.start(f.sq.get());
    SeqPump p = r.pump(10, 0.001, f.evs.data(), 4096, &f.st, [](const SeqEvent*, int, const SeqStep&) {});   // the first step (no base before it: no skip even if far behind)
    CHECK(p.steps == 1 && !p.skipped && f.sq->hasBase(), "first pump: one step");
    const double due = f.sq->time(); double now = due + 0.03; while (now - 0.03 > due) now = std::nextafter(now, -INFINITY); while (now - 0.03 < due) now = std::nextafter(now, INFINITY);
    CHECK(now - 0.03 == due, "test set-up: found a now with now - 0.03 == time");
    p = r.pump(now, none, f.evs.data(), 4096, &f.st, [](const SeqEvent*, int, const SeqStep&) {});
    CHECK(!p.skipped && r.skips() == 0 && f.sq->time() == due, "no skip at time == now - 0.03");
    const double now2 = std::nextafter(now, INFINITY);
    p = r.pump(now2, none, f.evs.data(), 4096, &f.st, [](const SeqEvent*, int, const SeqStep&) {});
    CHECK(p.skipped && r.skips() == 1 && std::fabs(f.sq->time() - (now2 + 0.02)) < 1e-9, "skip just past 0.03: time moves to now + 0.02 (time %.9f, now2 + 0.02 = %.9f)", f.sq->time(), now2 + 0.02);
    r.start(f.sq.get()); CHECK(r.skips() == 1, "start() keeps skips, as the JS (never reset)"); }
  { // verify-243 F3 (a): an unstepped Seq whose t0 is behind now (the page sends t0 a little behind currentTime): the first pump must NOT skip
    Fix f(1, true, 0); const double now = 10; f.sq->setTime(now - 1); SeqRunner r; r.start(f.sq.get()); std::vector<double> ts;
    const SeqPump p = r.pump(now, 0.001, f.evs.data(), 4096, &f.st, [&](const SeqEvent*, int, const SeqStep& s) { ts.push_back(s.t); });
    CHECK(!p.skipped && r.skips() == 0 && !ts.empty() && ts[0] == now - 1, "an unstepped Seq at now - 1: no skip, the first step at t0 (skipped %d, skips %llu, first t %f)", (int)p.skipped, (unsigned long long)r.skips(), ts.empty() ? -1.0 : ts[0]); }
  { // verify-243 F3 (b): a one-row song with a 1-step phrase looping, hold armed: the highest step never goes down, so it is never held (the JS's edge rule; F2)
    Fix f(1, false, 0, 1); SeqRunner r; r.start(f.sq.get()); r.hold(); Got g; bool held = false;
    blocks(f, r, 0, 128, 100, 100, &g, [&](const SeqPump& p) { held |= p.held; return p.held; });
    CHECK(!held && g.t.size() >= 100 && r.seq() != nullptr && r.holdArmed(), "one row, 1-step phrase looping, hold armed: never held (%zu steps played)", g.t.size()); }
  { // at most 3 steps a block; a long lead fills over later blocks
    Fix f(1, true, 0); SeqRunner r; r.start(f.sq.get()); std::vector<int> per;
    for (int b = 0; b < 5; b++) { const SeqPump p = r.pump(0, 1.0, f.evs.data(), 4096, &f.st, [](const SeqEvent*, int, const SeqStep&) {}); per.push_back(p.steps); }
    CHECK(per == std::vector<int>({3, 3, 2, 0, 0}), "lead 1.0 s at 0.125 s a step: 3, 3, 2, 0, 0 steps (got %d %d %d %d %d)", per[0], per[1], per[2], per[3], per[4]); }
  { // hold: a row change; the held step's events never reach onStep
    Fix f(2, true, 0); SeqRunner r; r.start(f.sq.get()); r.hold(); Got g; bool held = false, ended = false; double heldT = 0;
    blocks(f, r, 0, 128, 100, 200, &g, [&](const SeqPump& p) { if (p.held) { held = true; heldT = p.heldT; } ended |= p.ended; return held; });
    CHECK(held && g.t.size() == 16 && std::fabs(heldT - 2.0) < 1e-12, "hold at a row change: 16 steps of row 0 play, step 16 (t = 2.0) is held (played %zu, held t %f)", g.t.size(), heldT);
    bool seen = false; for (double t : g.t) seen |= t == heldT; CHECK(!seen, "the held step's events never reach onStep");
    CHECK(!ended && r.seq() == nullptr && !r.holdArmed() && r.heldPos()[0].valid && r.heldPos()[0].row == 1 && r.heldPos()[0].step == 0, "held: no end reported, sequencer dropped, hold disarmed, held pos = row 1 step 0");
    const SeqPump p = r.pump(1, 100, f.evs.data(), 4096, &f.st, [&](const SeqEvent*, int, const SeqStep&) { fails += 100; });
    CHECK(p.steps == 0 && !p.held && !p.ended, "after a hold the runner does nothing"); }
  { // hold: a row change WITHOUT a step wrap (phrases of one step: the highest step stays 0, only the row changes)
    Fix f(3, true, 0, 1); SeqRunner r; r.start(f.sq.get()); r.hold(); Got g; bool held = false; double heldT = 0;
    blocks(f, r, 0, 128, 100, 200, &g, [&](const SeqPump& p) { if (p.held) { held = true; heldT = p.heldT; } return held; });
    CHECK(held && g.t.size() == 1 && std::fabs(heldT - 0.125) < 1e-12 && r.heldPos()[0].row == 1 && r.heldPos()[0].step == 0, "hold at a row change with the step not going down: 1 step plays, row 1's first step is held (played %zu, held t %f)", g.t.size(), heldT); }
  { // hold: a step wrap on the same row (a one-row song that loops)
    Fix f(1, false, 0); SeqRunner r; r.start(f.sq.get()); r.hold(); Got g; bool held = false; double heldT = 0;
    blocks(f, r, 0, 128, 100, 200, &g, [&](const SeqPump& p) { if (p.held) { held = true; heldT = p.heldT; } return held; });
    CHECK(held && g.t.size() == 16 && std::fabs(heldT - 2.0) < 1e-12, "hold at a step wrap on the same row: 16 steps play, the wrapped step is held (played %zu)", g.t.size());
    CHECK(r.heldPos()[0].valid && r.heldPos()[0].row == 0 && r.heldPos()[0].step == 0, "held pos = row 0 step 0"); }
  { // not armed: the same song plays through the edge
    Fix f(2, true, 0); SeqRunner r; r.start(f.sq.get()); Got g; int ended = 0;
    blocks(f, r, 0, 128, 100, 400, &g, [&](const SeqPump& p) { ended += p.ended; return false; });
    CHECK(g.t.size() == 32 && ended == 1, "no hold: 32 steps play, the end is reported once (steps %zu, ended %d)", g.t.size(), ended); }
  { // start() disarms hold
    Fix f(2, true, 0); SeqRunner r; r.hold(); r.start(f.sq.get()); Got g; blocks(f, r, 0, 128, 100, 400, &g, [](const SeqPump&) { return false; });
    CHECK(g.t.size() == 32 && r.ended(), "start() resets hold: 32 steps played, the end reached"); }
  { // stop(): pump does nothing
    Fix f(1, true, 0); SeqRunner r; r.start(f.sq.get()); r.pump(0, 0.3, f.evs.data(), 4096, &f.st, [](const SeqEvent*, int, const SeqStep&) {}); const double t = f.sq->time(); r.stop();
    const SeqPump p = r.pump(5, 5, f.evs.data(), 4096, &f.st, [&](const SeqEvent*, int, const SeqStep&) { fails += 100; });
    CHECK(p.steps == 0 && !p.skipped && !p.ended && f.sq->time() == t, "stop() then pump does nothing (not even a skip)"); }
  { // block sizes 1, 128, 4096 frames: the same event list; and equal to stepping the sequencer directly
    Got ref; { Fix f(2, true, 5); while (f.sq->alive()) { const int k = f.sq->step(f.evs.data(), 4096, &f.st); ref.t.push_back(f.st.t); ref.rows.push_back(seqplain::row(f.st.t, f.evs.data(), k)); } }
    for (int frames : {1, 128, 4096}) { Fix f(2, true, 5); SeqRunner r; r.start(f.sq.get()); Got g; int ended = 0;
      blocks(f, r, 5, frames, 0.25, 4000000, &g, [&](const SeqPump& p) { ended += p.ended; return p.ended; });
      CHECK(g.rows == ref.rows && ended == 1 && r.skips() == 0, "block of %d frames: %zu steps, equal to the direct stepping (%zu), skips %llu", frames, g.rows.size(), ref.rows.size(), (unsigned long long)r.skips()); } }
}

// ---- 3. hostile values ----------------------------------------------------------------------------------------------------
static bool allFinite(const Seq& s) { return std::isfinite(s.time()) && std::isfinite(s.bpm()) && (!s.hasBase() || std::isfinite(s.base())) && (!s.hasBtT() || (std::isfinite(s.btT()) && std::isfinite(s.bt()))); }
static void testHostile(const std::string& electro) {
  const double vals[] = {std::nan(""), INFINITY, -INFINITY, -1, 0, 1e300, -1e300, DBL_MAX, -DBL_MAX, DBL_MIN, 5e-324, 1e-9, 1e9, 1e-300, 1e15, -1e15};
  Song song; if (!loadSong(song, electro)) { CHECK(false, "ELECTRO does not load"); return; }
  const char* names[] = {"setTime", "setBpm", "shift"}; int cases = 0;
  for (int which = 0; which < 3; which++) for (const double v : vals) for (int when = 0; when < 2; when++) {   // when: 0 before the first step, 1 after 40 steps
    SeqVars vars; SeqOpts o; o.live = true; Seq sq(song, vars, SeqMode::Song, o); std::vector<SeqEvent> evs(12288); SeqStep st; SeqRunner r; r.start(&sq);
    double now = 50; sq.setTime(now);
    int steps = 0; const auto onStep = [&](const SeqEvent* e, int n, const SeqStep& s) { steps++; (void)e; (void)n; (void)s; };
    if (when == 1) for (int b = 0; b < 400 && steps < 40; b++) { r.pump(now, 0.25, evs.data(), 12288, &st, onStep); now += 128.0 / 48000; }
    if (which == 0) sq.setTime(v); else if (which == 1) sq.setBpm(v); else sq.shift(v);
    int maxPer = 0;
    for (int b = 0; b < 600; b++) { const SeqPump p = r.pump(now, 0.25, evs.data(), 12288, &st, onStep); if (p.steps > maxPer) maxPer = p.steps; now += 128.0 / 48000; }
    for (int k = 0; k < 300; k++) { sq.step(evs.data(), 12288, &st); if (!allFinite(sq) && which != 0 && which != 2) break; }   // direct steps: bounded, always returns
    cases++;
    const bool timeKept = which == 0 || which == 2;   // a finite 1e300 time / shift is accepted; everything else must stay finite
    CHECK(maxPer <= kRunMaxSteps, "%s(%g) %s: %d steps in one block", names[which], v, when ? "mid-song" : "at start", maxPer);
    if (which == 1) CHECK(allFinite(sq) && sq.bpm() >= kSeqBpmMin && sq.bpm() <= kSeqBpmMax, "setBpm(%g) %s: state finite and bpm in range (bpm %g)", v, when ? "mid-song" : "at start", sq.bpm());
    else CHECK(timeKept || allFinite(sq), "%s(%g): state finite", names[which], v);
    if (!std::isfinite(v)) CHECK(sq.badSets() >= 1, "%s(%g): refused value counted", names[which], v);
  }
  { // a bpm of 1e-9 or 1e9 cannot hang step(): 5000 direct steps each, bounded, finite
    for (const double v : {1e-9, 1e9, 1e-300, DBL_MAX}) { SeqVars vars; SeqOpts o; o.live = true; Seq sq(song, vars, SeqMode::Song, o); std::vector<SeqEvent> evs(12288); SeqStep st; sq.setTime(0); sq.setBpm(v);
      for (int k = 0; k < 5000; k++) sq.step(evs.data(), 12288, &st);
      CHECK(allFinite(sq) && sq.dropped() == 0, "bpm %g: 5000 steps, state finite (bt %g)", v, sq.bt()); } }
  std::printf("hostile: %d setter x value x moment cases\n", cases);
}

int main() {
  const std::string root = THUNDER_ROOT;
  std::string electro; if (!readFile(root + "/sounds/ELECTRO.thunder", electro)) { std::printf("FAIL: cannot read sounds/ELECTRO.thunder\n"); return 1; }
  const std::string cmds = withCommands(electro);
  int good = 0; for (const char* id : {"electro-live-time", "electro-live-bpm", "electro-live-shift", "electro-cmds-live-all"}) good += goldenCase(root, id, electro, cmds) ? 1 : 0;
  std::printf("%d of 4 live-clock goldens equal\n", good);
  testSetters(); testRunner(); testHostile(electro);
  std::printf(fails ? "FAILED (%d)\n" : "PASS\n", fails);
  return fails ? 1 : 0;
}
