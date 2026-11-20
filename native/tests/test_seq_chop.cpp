// CHOP (docs/port/CHOP-NOTES.md, ctest `seq_chop`): the native sequencer chops the notes of sliced (slc > 1) and synced instruments as the page's applyChop does.
//   1. goldens made by tools/seq-golden.mjs --chop: the audio thread's makeSeq (sim off) with the REAL applyChop / loopSecs / slicePoints text of index.html and the tool's own pads.
//      The loop length and SMODE hits the JS computed are in the golden and handed to the Seq here (SeqChop). Every row, the sha256 and the counts, exactly:
//      chop-all (slc 4 around the root, slc 16 + SLC, sync 1 at another tempo, sync 2 / 3 with STA, TPO mid-song, RET, KIL, SMODE hits), chop-rev (PLY 1),
//      chop-edit (through a SeqSession: the song is edited between steps, slc / sync of running instruments change, a takeOver at each edit).
//   2. the rules of the Seq's own: no table / no pad / slc 0 or 1 / sim; hostile tables and instruments; SeqChop's argument checks.
// Run it under -DTHUNDER_SANITIZE=ON too. The allocation guard around step() with chop on is in rt_harness (run_seq_chop).
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "rt/json.h"
#include "rt/snap.h"
#include "seq/runner.h"
#include "seq/seq.h"
#include "seq/session.h"
#include "song/song.h"

#include "seq_plain.h"
#include "sha256.h"

using namespace thunder;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static bool readFile(const std::string& path, std::string& out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); out = s.str(); return true; }
static std::string writeStr(Json v) { std::string o; json_write(v, o); return o; }
static std::shared_ptr<Song> loadSong(const std::string& js) { std::shared_ptr<Song> s = std::make_shared<Song>(); JsonError je; if (!s->load(js.data(), js.size(), &je)) { std::printf("song does not load: %s\n", je.msg); return nullptr; } return s; }

// the golden's pad table -> a SeqChop: {"<pad>": {loop, hits: {"<n>": [points]}}}
static bool chopOf(Json pads, SeqChop& ch) {
  for (size_t i = 0; i < pads.size(); i++) {
    const int pad = std::atoi(std::string(pads.keyAt(i)).c_str()); const Json P = pads.valAt(i);
    if (!ch.setLoop(pad, P.num("loop"))) return false;
    const Json H = P.obj("hits");
    for (size_t k = 0; k < H.size(); k++) { const int n = std::atoi(std::string(H.keyAt(k)).c_str()); std::vector<double> pts; const Json A = H.valAt(k); for (size_t q = 0; q < A.size(); q++) pts.push_back(A.at(q).asNum());
      if (!ch.setHits(pad, n, pts.data(), (int)pts.size())) return false; }
  }
  return true;
}

// ---- the edit machinery (a copy of test_seq_takeover's: the patch ops of the golden applied to the song's own text) --------------------------------------
struct SText { std::vector<std::pair<std::string, std::string>> kv;
  std::string text() const { std::string o = "{"; for (size_t i = 0; i < kv.size(); i++) { if (i) o += ','; o += '"' + kv[i].first + "\":" + kv[i].second; } return o + "}"; } };
static std::vector<std::string> elemsOf(const std::string& t) { std::vector<std::string> v; JsonDoc d; if (!d.parse(t) || !d.root().isArr()) return v; for (size_t i = 0; i < d.root().size(); i++) v.push_back(writeStr(d.root().at(i))); return v; }
static std::string joinArr(const std::vector<std::string>& v) { std::string o = "["; for (size_t i = 0; i < v.size(); i++) { if (i) o += ','; o += v[i]; } return o + "]"; }
static void applyOps(SText& S, Json ops) {
  for (size_t q = 0; q < ops.size(); q++) {
    const Json op = ops.at(q); const std::string k(op.at(0).asStr()); const Json I = op.at(1), V = op.at(2);
    size_t at = S.kv.size(); for (size_t i = 0; i < S.kv.size(); i++) if (S.kv[i].first == k) at = i;
    if (I.isNull()) { if (at == S.kv.size()) S.kv.push_back({k, ""}); S.kv[at].second = writeStr(V); }
    else if (I.isStr() && I.asStr() == "del") { if (at < S.kv.size()) S.kv.erase(S.kv.begin() + (long)at); }
    else if (I.isStr() && I.asStr() == "len") { if (at == S.kv.size() || S.kv[at].second.empty() || S.kv[at].second[0] != '[') continue; std::vector<std::string> e = elemsOf(S.kv[at].second); e.resize((size_t)V.asNum(), "null"); S.kv[at].second = joinArr(e); }
    else { if (at == S.kv.size()) S.kv.push_back({k, "[]"}); std::vector<std::string> e = elemsOf(S.kv[at].second); const size_t i = (size_t)I.asNum(); if (e.size() <= i) e.resize(i + 1, "null"); e[i] = writeStr(V); S.kv[at].second = joinArr(e); }
  }
}
struct Host {
  std::shared_ptr<SeqSession> sess; SnapSlot<SongSnap> slot; SeqRunner run; SeqVars vars;
  Host(SeqMode m, const SeqOpts& o) : sess(SeqSession::create(m, o)) {}
  bool first(std::shared_ptr<const Song> s) { if (!sess->publish(slot, std::move(s), vars)) return false; seq_adopt(slot, run); run.start(slot.get()->seq.get()); return true; }
  bool edit(std::shared_ptr<const Song> s) { if (!sess->publish(slot, std::move(s), vars)) return false; seq_adopt(slot, run); slot.collect(); return true; }
  Seq* seq() const { return run.seq(); }
};

// ---- 1. the goldens ---------------------------------------------------------------------------------------------------------------------------------------
static bool goldenCase(const std::string& root, const std::string& id, bool viaSession) {
  std::string gtext; if (!readFile(root + "/native/tests/golden/seq-" + id + ".json", gtext)) { CHECK(false, "%s: cannot read the golden", id.c_str()); return false; }
  JsonDoc gd; if (!gd.parse(gtext)) { CHECK(false, "%s: the golden is not JSON", id.c_str()); return false; }
  const Json G = gd.root(), RH = G.arr("rowHashes"), edits = G.arr("edits");
  SeqChop chop; if (!chopOf(G.obj("pads"), chop)) { CHECK(false, "%s: the golden's pad table is refused", id.c_str()); return false; }
  const std::string fileText(G.str("fileText"));
  SText S; { JsonDoc d; if (!d.parse(fileText)) { CHECK(false, "%s: fileText", id.c_str()); return false; } const Json SS = d.root().get("song"); for (size_t i = 0; i < SS.size(); i++) S.kv.push_back({std::string(SS.keyAt(i)), writeStr(SS.valAt(i))}); }
  std::shared_ptr<const Song> song = loadSong(S.text()); if (!song) return false;
  SeqOpts o; o.sim = false; o.once = true; o.chop = &chop;
  SeqVars vars0; std::unique_ptr<Seq> own; std::unique_ptr<Host> host;   // (Seq keeps a reference to its vars: they must outlive it)
  if (viaSession) { host.reset(new Host(SeqMode::Song, o)); if (!host->first(song)) { CHECK(false, "%s: first publish", id.c_str()); return false; } }
  else own.reset(new Seq(*song, vars0, SeqMode::Song, o));
  const auto cur = [&]() { return viaSession ? host->seq() : own.get(); };
  const int CAP = 4096; std::vector<SeqEvent> evs((size_t)CAP); SeqStep st; Sha256 all; int n = 0, firstBad = -1; std::string badMine; int chopped = 0, stN = 0; const int maxRows = (int)G.num("rows");
  for (; cur()->alive() && n < maxRows; n++) {
    Seq* sq = cur(); const int k = sq->step(evs.data(), CAP, &st);
    for (int i = 0; i < k; i++) if (evs[i].type == SEV_NOTE) { if (evs[i].has & SH_CHOP) chopped++; if (evs[i].has & SH_ST) stN++; }
    std::string row = seqplain::row(st.t, evs.data(), k); const std::string h = sha256_hex(row).substr(0, 12);
    if (firstBad < 0 && (n >= (int)RH.size() || h != RH.at((size_t)n).asStr())) { firstBad = n; badMine = row; }
    row += '\n'; all.add(row);
    for (size_t i = 0; i < edits.size(); i++) { const Json e = edits.at(i); if ((int)e.num("after") != n + 1) continue;
      applyOps(S, e.get("patch")); song = loadSong(S.text()); if (!song || !host->edit(song)) { CHECK(false, "%s: edit at %d", id.c_str(), n); return false; } }
  }
  Seq* sq = cur();
  const bool ok = firstBad < 0 && n == maxRows && all.hex() == G.str("sha256") && chopped == (int)G.num("chopped") && stN == (int)G.num("stretched") && sq->dropped() == 0 && sq->chopSkipped() == 0 && sq->chopNoHits() == 0;
  CHECK(ok, "%s: rows %d (golden %d), first differing row %d, chopped %d (golden %d), st %d (golden %d), skipped %llu, no-hits %llu\n  C++ row: %s", id.c_str(), n, maxRows, firstBad, chopped, (int)G.num("chopped"), stN, (int)G.num("stretched"),
        (unsigned long long)sq->chopSkipped(), (unsigned long long)sq->chopNoHits(), badMine.c_str());
  std::printf("%s %s: %d rows, %d chopped notes, %d with st%s\n", ok ? "ok  " : "FAIL", id.c_str(), n, chopped, stN, viaSession ? " (a takeOver at each edit)" : "");
  return ok;
}

// ---- 2. the Seq's own rules -------------------------------------------------------------------------------------------------------------------------------
// one track, one phrase of 16 rows with a note on rows 0 (48), 4 (49), 8 (50), 12 (51) on instrument 0; instrument 0 as given
static std::string handSong(const std::string& inst, int bpm = 120) {
  std::string s = "{\"v\":6,\"bpm\":" + std::to_string(bpm) + ",\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[16],\"insts\":[" + inst + "],\"phrases\":[[";
  for (int y = 0; y < 32; y++) { s += y ? "," : ""; const int n = y == 0 ? 48 : y == 4 ? 49 : y == 8 ? 50 : y == 12 ? 51 : -1; s += "[" + std::to_string(n) + ",-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"; }
  return s + "]]}";
}
struct Run { std::vector<SeqEvent> notes; uint64_t chopped = 0, skipped = 0, noHits = 0, stretched = 0; };
static Run play(const std::string& songText, const SeqChop* chop, bool sim = false, int steps = 64) {
  Run r; const std::shared_ptr<Song> song = loadSong(songText); if (!song) { fails++; return r; }
  SeqVars vars; SeqOpts o; o.sim = sim; o.once = true; o.chop = chop; Seq sq(*song, vars, SeqMode::Song, o);
  std::vector<SeqEvent> evs(4096); SeqStep st;
  for (int i = 0; i < steps && sq.alive(); i++) { const int k = sq.step(evs.data(), 4096, &st); for (int q = 0; q < k; q++) if (evs[q].type == SEV_NOTE) r.notes.push_back(evs[q]); }
  r.chopped = sq.chopped(); r.skipped = sq.chopSkipped(); r.noHits = sq.chopNoHits(); r.stretched = sq.chopStretched(); return r;
}
static bool noteOk(const SeqEvent& e) {
  return std::isfinite(e.rate) && e.rate > 0 && std::isfinite(e.t) && (!(e.has & SH_CHOP) || (std::isfinite(e.ofsSec) && e.ofsSec >= 0 && std::isfinite(e.durSec) && e.durSec >= 0.005)) && (!(e.has & SH_CUT) || std::isfinite(e.cut));
}

// song whose track plays through PART 1 (a PRT 01 on row 0): the song's own instrument 0 is `own`, the part's instrument 0 is `part`
static std::string partSong(const std::string& own, const std::string& part) {
  // a part instrument holds only what it overrides: the page leaves tsp / vol / pan / ofs undefined (NaN) when absent, so give the plain ones
  const std::string partInst = "{\"tsp\":0,\"vol\":255,\"pan\":128,\"ofs\":0," + part.substr(1);
  std::string s = handSong(own); const std::string r0 = "[48,-1,-1,-1,-1,0,-1,-1,", r1 = "[48,-1,-1,-1,-1,0,72,1,";
  const size_t k = s.find(r0); s.replace(k, r0.size(), r1);
  s.pop_back(); return s + ",\"parts\":[{\"mix\":[],\"fx\":{},\"insts\":[" + partInst + "]}]}";
}
static bool sameNotes(const Run& a, const Run& b) {
  if (a.notes.size() != b.notes.size() || a.notes.empty()) return false;
  for (size_t i = 0; i < a.notes.size(); i++) { const SeqEvent &x = a.notes[i], &y = b.notes[i]; if (x.rate != y.rate || x.ofsSec != y.ofsSec || x.durSec != y.durSec || x.cut != y.cut || x.has != y.has || x.stMode != y.stMode || x.stBeats != y.stBeats || x.stGrain != y.stGrain) return false; }
  return true;
}

static void testRules() {
  SeqChop ch; CHECK(ch.setLoop(0, 2.0), "setLoop"); const double pts4[] = {0, 0.1, 0.5, 1.5}; CHECK(ch.setHits(0, 4, pts4, 4), "setHits");
  const std::string slc4 = handSong("{\"pad\":0,\"slc\":4}");
  { // the numbers, by hand: slc 4, loop 2 s: notes 48 49 50 51 = slices 0 1 2 3 at 0, 0.5, 1.0, 1.5 s of 0.5 s; rate 1 (no SLC: the note's pitch is ignored)
    const Run r = play(slc4, &ch); CHECK(r.notes.size() == 4 && r.chopped == 4 && r.skipped == 0, "slc 4: four chopped notes (%zu, %llu)", r.notes.size(), (unsigned long long)r.chopped);
    for (size_t k = 0; k < r.notes.size() && k < 4; k++) { const SeqEvent& e = r.notes[k];
      CHECK((e.has & SH_CHOP) && (e.has & SH_CUT) && e.rate == 1 && e.ofsSec == 0.5 * (double)k && e.durSec == 0.5 && !(e.has & SH_ST), "slc 4 note %zu: ofsSec %g durSec %g rate %g", k, e.ofsSec, e.durSec, e.rate);
      CHECK(std::fabs(e.cut - (e.t + 0.5)) < 1e-12, "slc 4 note %zu: cut = t + durSec / rate (%g vs %g)", k, e.cut, e.t + 0.5); } }
  { // no table, a table without the pad, sim mode, slc 0 and 1 with no sync: the notes are as the sequencer made them (and counted when the instrument is sliced)
    const Run a = play(slc4, nullptr), b = [&] { SeqChop o; o.setLoop(5, 1); return play(slc4, &o); }(), c = play(slc4, &ch, true), d = play(handSong("{\"pad\":0,\"slc\":1}"), &ch), e = play(handSong("{\"pad\":0,\"slc\":0}"), &ch);
    CHECK(a.skipped == 4 && a.chopped == 0 && b.skipped == 4 && b.chopped == 0, "no table / no pad: skipped (%llu, %llu)", (unsigned long long)a.skipped, (unsigned long long)b.skipped);
    for (const Run* r : {&a, &b, &c, &d, &e}) for (const SeqEvent& n : r->notes) CHECK(!(n.has & (SH_CHOP | SH_ST)) && !(n.has & SH_CUT) && n.rate > 0, "an unchopped note keeps no chop members");
    CHECK(c.skipped == 0 && c.chopped == 0 && d.skipped == 0 && e.skipped == 0 && d.chopped == 0 && e.chopped == 0, "sim, slc 1, slc 0: no chop and nothing counted"); }
  { // SMODE with the hits handed in, and without (the grid, counted)
    const Run r = play(handSong("{\"pad\":0,\"slc\":4,\"smode\":1}"), &ch); CHECK(r.notes.size() == 4 && r.noHits == 0, "hits used");
    CHECK(r.notes[1].ofsSec == 0.1 && r.notes[1].durSec == 0.5 - 0.1 + 0 && std::fabs(r.notes[1].durSec - 0.4) < 1e-12 && r.notes[3].ofsSec == 1.5 && r.notes[3].durSec == 0.5, "the hits: slice 1 from 0.1 to 0.5, the last to the loop's end (%g %g)", r.notes[1].ofsSec, r.notes[1].durSec);
    SeqChop g; g.setLoop(0, 2.0); const Run q = play(handSong("{\"pad\":0,\"slc\":4,\"smode\":1}"), &g); CHECK(q.noHits == 4 && q.notes[1].ofsSec == 0.5, "SMODE without hits: the grid, counted (%llu)", (unsigned long long)q.noHits); }
  { // sync 1: rate = 2^(semis/12) * loop / target; target = beats * 60 / bpm = 2 s at 120; the loop 2 s here -> 1; at 150 bpm (target 1.6) -> 1.25
    SeqChop g; g.setLoop(0, 2.0); const Run a = play(handSong("{\"pad\":0,\"sync\":1,\"beats\":4}"), &g), b = play(handSong("{\"pad\":0,\"sync\":1,\"beats\":4}", 150), &g);
    CHECK(a.notes.size() == 4 && std::fabs(a.notes[0].rate - 1) < 1e-12 && a.notes[0].ofsSec == 0 && a.notes[0].durSec == 2.0, "sync 1 at the loop's own tempo: rate %g", a.notes[0].rate);
    CHECK(std::fabs(b.notes[0].rate - 1.25) < 1e-12 && std::fabs(b.notes[0].durSec - 2.0) < 1e-12, "sync 1 at 150 bpm: rate %g", b.notes[0].rate); }
  { // sync 2 / 3: ev.st, scale = target / loop, counted; STA 128 on a synced loop starts at half of the loop
    SeqChop g; g.setLoop(0, 1.0); const Run a = play(handSong("{\"pad\":0,\"sync\":2,\"beats\":4,\"grain\":3}"), &g);
    CHECK(a.stretched == 4 && (a.notes[0].has & SH_ST) && a.notes[0].stMode == 2 && a.notes[0].stBpm == 120 && a.notes[0].stBeats == 4 && a.notes[0].stGrain == 3 && std::fabs(a.notes[0].durSec - 2.0) < 1e-12, "sync 2: st members, durSec = target (%g)", a.notes[0].durSec);
    std::string sg = handSong("{\"pad\":0,\"sync\":3,\"beats\":4}"); const Run b = play(sg, &g); CHECK(b.notes[0].stMode == 3 && b.notes[0].stGrain == 1, "sync 3, grain absent = 1 (%d)", b.notes[0].stGrain); }
  // hostile instruments (slc 0, 1, 64, 65, negative, huge) and a hostile table: finite, no crash, nothing out of range
  for (const char* slc : {"0", "1", "2", "63", "64", "65", "-3", "-1000000", "1e18", "0.5"}) for (const char* sync : {"0", "1", "2", "3", "7", "-1"}) for (const char* sm : {"0", "1"}) {
    const std::string inst = std::string("{\"pad\":0,\"slc\":") + slc + ",\"sync\":" + sync + ",\"smode\":" + sm + ",\"beats\":4}";
    const Run r = play(handSong(inst), &ch, false, 40); for (const SeqEvent& e : r.notes) CHECK(noteOk(e), "hostile instrument %s: a note is not sane", inst.c_str());
    const Run s = play(handSong(inst), nullptr, false, 40); for (const SeqEvent& e : s.notes) CHECK(noteOk(e), "no table, hostile instrument %s", inst.c_str());
  }
  { // the table's own argument checks: nothing changes on a refusal
    SeqChop t; const double nan = std::nan(""), inf = INFINITY;
    for (const double v : {nan, inf, -inf, -1.0, 1e-7, 1e7, DBL_MAX, -DBL_MAX, 5e-324}) CHECK(!t.setLoop(3, v) && !t.has(3), "setLoop(%g) refused", v);
    CHECK(!t.setLoop(-1, 1) && !t.setLoop(256, 1) && !t.setLoop(1 << 30, 1), "setLoop: pad out of range refused");
    CHECK(t.setLoop(3, 1.5) && t.has(3) && t.loop(3) == 1.5 && t.setLoop(3, 0) && !t.has(3) && t.loop(3) == 0, "setLoop sets, 0 removes");
    const double ok4[] = {0, 0.5, 1, 1.5}, bad1[] = {0, nan, 1, 1.5}, bad2[] = {0, -0.5, 1, 1.5}, bad3[] = {0, 1e7, 1, 1}, bad4[] = {0, inf, 1, 1};
    int cnt = 0; CHECK(t.setHits(3, 4, ok4, 4) && t.hits(3, 4, &cnt) && cnt == 4, "setHits ok");
    CHECK(!t.setHits(3, 4, bad1, 4) && !t.setHits(3, 4, bad2, 4) && !t.setHits(3, 4, bad3, 4) && !t.setHits(3, 4, bad4, 4), "setHits: NaN, negative, huge, inf refused");
    CHECK(!t.setHits(3, 1, ok4, 1) && !t.setHits(3, 65, ok4, 4) && !t.setHits(3, 4, ok4, 5) && !t.setHits(3, 4, nullptr, 2) && !t.setHits(-1, 4, ok4, 4) && !t.setHits(256, 4, ok4, 4) && !t.setHits(3, 4, ok4, -1), "setHits: n, count, null, pad refused");
    CHECK(t.hits(3, 4, &cnt) && cnt == 4 && t.hits(3, 4, &cnt)[1] == 0.5, "a refused call changed nothing");
    CHECK(t.hits(3, 5, &cnt) == nullptr && t.hits(2, 4, &cnt) == nullptr && t.hits(3, 1, &cnt) == nullptr && t.hits(-1, 4, &cnt) == nullptr, "lookups of what is not there are null");
    CHECK(t.setHits(3, 4, nullptr, 0) && t.hits(3, 4, &cnt) == nullptr, "count 0 removes"); t.setHits(3, 4, ok4, 4); t.setLoop(3, 0); CHECK(t.hitSets() == 0, "removing the loop drops the pad's hits");
  }
  { // unsorted and out-of-the-loop-range hits (accepted: the JS plays what it is given): finite, durSec at least 0.005, ofsSec >= 0
    SeqChop t; t.setLoop(0, 2.0); const double uns[] = {1.9, 0.2, 5.0, 0.0}; CHECK(t.setHits(0, 4, uns, 4), "unsorted hits accepted");
    const Run r = play(handSong("{\"pad\":0,\"slc\":4,\"smode\":1}"), &t); CHECK(r.notes.size() == 4, "four notes"); for (const SeqEvent& e : r.notes) CHECK(noteOk(e) && e.durSec >= 0.005, "unsorted hits: sane note (ofs %g dur %g)", e.ofsSec, e.durSec);
    const double one[] = {0.3}; SeqChop u; u.setLoop(0, 2.0); u.setHits(0, 4, one, 1); const Run q = play(handSong("{\"pad\":0,\"slc\":4,\"smode\":1}"), &u);   // fewer points than slices: the last point's slice runs to the loop's end
    CHECK(q.notes.size() == 4 && q.notes[0].ofsSec == 0.3 && q.notes[0].durSec == 1.7 && q.notes[1].ofsSec == 0.3, "one hit for four slices (%g %g)", q.notes[0].ofsSec, q.notes[0].durSec); }
  { // a loop of 1e-6 s and one of 1e6 s with extreme tempos: finite or left unchopped (counted), never NaN
    for (const double loop : {1e-6, 1e6, 0.01}) for (const int bpm : {1, 40, 255, 100000}) { SeqChop t; t.setLoop(0, loop);
      for (const char* inst : {"{\"pad\":0,\"sync\":1,\"beats\":64}", "{\"pad\":0,\"sync\":2,\"beats\":1}", "{\"pad\":0,\"slc\":64}"}) { const Run r = play(handSong(inst, bpm), &t, false, 30); for (const SeqEvent& e : r.notes) CHECK(noteOk(e), "loop %g bpm %d %s: note not sane (rate %g ofs %g dur %g)", loop, bpm, inst, e.rate, e.ofsSec, e.durSec); } } }
}

// verify-chop F4: a PERFORM part's instrument is clamped like the main path (song.cpp: slc 0..64, sync 0..3, beats 1..64, grain 0..4): the part's chop equals the song's own instrument's chop for the same JSON
static void testPartClamp() {
  SeqChop ch; ch.setLoop(0, 2.0);
  { const Run a = play(partSong("{\"pad\":0}", "{\"pad\":0,\"slc\":4}"), &ch), b = play(handSong("{\"pad\":0,\"slc\":4}"), &ch);
    CHECK(a.notes.size() == 4 && a.chopped == 4 && sameNotes(a, b), "part path: an in-range part instrument chops like the main path (%zu)", a.notes.size()); }
  for (const char* inst : {"{\"pad\":0,\"slc\":100}", "{\"pad\":0,\"slc\":1e9}", "{\"pad\":0,\"slc\":64.4}", "{\"pad\":0,\"slc\":-5}", "{\"pad\":0,\"slc\":\"7\"}",
                           "{\"pad\":0,\"sync\":1,\"beats\":100}", "{\"pad\":0,\"sync\":1,\"beats\":-3}", "{\"pad\":0,\"sync\":1,\"beats\":2.6}", "{\"pad\":0,\"sync\":7,\"beats\":8,\"grain\":9}",
                           "{\"pad\":0,\"sync\":3,\"beats\":4,\"grain\":-2}", "{\"pad\":0,\"sync\":2,\"beats\":1e12,\"grain\":null}", "{\"pad\":0,\"slc\":100,\"smode\":1}"}) {
    const Run a = play(partSong("{\"pad\":0}", inst), &ch), b = play(handSong(inst), &ch);
    CHECK(!a.notes.empty() && sameNotes(a, b) && a.chopped == b.chopped && a.skipped == b.skipped && a.stretched == b.stretched, "part path = main path for %s (%zu vs %zu notes)", inst, a.notes.size(), b.notes.size());
    for (const SeqEvent& e : a.notes) CHECK(noteOk(e), "part %s: a sane note", inst); }
  // the page's `I.beats || 4` (index.html:22335, :22339): a part with beats 0 / null / "" / absent plays as 4 beats (NOT the loader's clamp to 1); a part with sync 1 at 120 bpm, loop 2 s: rate = loop / target
  { SeqChop g; g.setLoop(0, 2.0);
    const Run four = play(partSong("{\"pad\":0}", "{\"pad\":0,\"sync\":1,\"beats\":4}"), &g);
    CHECK(four.notes.size() == 4 && std::fabs(four.notes[0].rate - 1.0) < 1e-12, "beats 4: rate 1 (%g)", four.notes.empty() ? 0.0 : four.notes[0].rate);
    for (const char* b : {"0", "null", "\"\"", "false"}) {
      const Run r = play(partSong("{\"pad\":0}", std::string("{\"pad\":0,\"sync\":1,\"beats\":") + b + "}"), &g);
      CHECK(r.notes.size() == 4 && sameNotes(r, four) && std::fabs(r.notes[0].rate - 1.0) < 1e-12, "part beats %s plays as 4 beats (rate %g, 1 expected; beats 1 would give 0.25)", b, r.notes.empty() ? 0.0 : r.notes[0].rate); }
    const Run absent = play(partSong("{\"pad\":0}", "{\"pad\":0,\"sync\":1}"), &g); CHECK(sameNotes(absent, four), "part beats absent plays as 4 beats");
    const Run neg = play(partSong("{\"pad\":0}", "{\"pad\":0,\"sync\":1,\"beats\":-3}"), &g); CHECK(neg.notes.size() == 4 && std::fabs(neg.notes[0].rate - 4.0) < 1e-12, "part beats -3 (truthy, beyond the page's range): clamped to 1 (rate %g, 4 expected)", neg.notes.empty() ? 0.0 : neg.notes[0].rate);
    // slc truncates like `I.slc | 0`: 1.9 is 1 (no chop), 2.5 is 2; grain null -> 1, grain 0 stays 0
    const Run s1 = play(partSong("{\"pad\":0}", "{\"pad\":0,\"slc\":1.9}"), &g); CHECK(s1.chopped == 0 && s1.skipped == 0, "part slc 1.9 = 1: no chop");
    const Run g0 = play(partSong("{\"pad\":0}", "{\"pad\":0,\"sync\":2,\"grain\":0}"), &g), gn = play(partSong("{\"pad\":0}", "{\"pad\":0,\"sync\":2,\"grain\":null}"), &g);
    CHECK(g0.notes.size() == 4 && g0.notes[0].stGrain == 0 && gn.notes.size() == 4 && gn.notes[0].stGrain == 1, "part grain 0 stays 0, null is 1"); }
}

int main() {
  const std::string root = THUNDER_ROOT;
  int good = 0; const char* ids[] = {"chop-all", "chop-rev"};
  for (const char* id : ids) good += goldenCase(root, id, false) ? 1 : 0;
  good += goldenCase(root, "chop-edit", true) ? 1 : 0;
  goldenCase(root, "chop-all", true);   // the same through a session with no edit (the Seq of a session reads the same table)
  std::printf("%d of 3 chop goldens equal\n", good);
  testRules();
  testPartClamp();
  std::printf(fails ? "FAILED (%d)\n" : "PASS\n", fails);
  return fails ? 1 : 0;
}
