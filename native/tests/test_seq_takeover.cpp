// SEQ-LIVE-DESIGN Part B (ctest `seq_takeover`, `seq_takeover_threads`): the Song's lifetime under a running sequencer.
//   1. T0, the identity: every plain golden and every live golden (the JS's, tests/golden/seq-*.json and native/tests/golden/seq-*-live*.json) is met
//      exactly (a) by a Seq built through a SeqSession, (b) with a takeOver to an identical song at EVERY step, (c) the same with the Song re-read now and then.
//   2. the edit goldens (tools/seq-golden.mjs --edit: the JS sequencer with the song edited in place between steps, made by the processor's own
//      message handler): the C++ sequencer, taking over at the same step, must equal them exactly: with one snapshot per edit and with three
//      published at once of which the audio side adopts only the last (the skipped-snapshot test).
//   3. the rulings' own cases and the session's rules (rollback, other session, the ids cap, 4096 ids), hostile edits (the dangling positions of
//      SEQ-TAKEOVER-NOTES section 4 all at once, and random ones), run it under ASan + UBSan.
//   4. `--threads [seconds]`: the control thread publishes patches (and vars) while the audio side steps (a ThreadSanitizer build checks the races).
// The allocation guard around takeOver is in rt_harness (run_seq_takeover).
//
// DELIBERATE DIFFERENCES FROM THE JS (ruling 5), kept here:
//   - a perf deleted from the song: the JS orphans it (its LFOs and locks keep playing from the old object); the successor is rebuilt from the new
//     song, so they stop (testPerfDeleted).
//   - a song row that is missing (the song cut with `len`): the JS throws a TypeError at the next step; C++ treats the cell as empty (testMissingRow).
//   - past kSeqMaxIds (4096) distinct PERFORM target ids in one PLAY a new id is not interned (counted): its target is inert (testIdsCap).
#include <atomic>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
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
static std::shared_ptr<Song> loadSong(const std::string& js, bool pageS = true) { std::shared_ptr<Song> s = std::make_shared<Song>(); JsonError je; if (!s->load(js.data(), js.size(), &je, pageS)) { std::printf("FAIL: a song does not load: %s\n", je.msg ? je.msg : "?"); fails++; return nullptr; } return s; }

// tools/seq-golden.mjs's withCommands (see test_seq_live.cpp)
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

// ---- the host side of a PLAY: a session, the slot, the runner (the control thread publishes, the audio thread adopts) -------------------------------
struct Host {
  std::shared_ptr<SeqSession> sess; SnapSlot<SongSnap> slot; SeqRunner run; SeqVars vars; std::shared_ptr<const Song> decoy;
  Host(SeqMode m, const SeqOpts& o) : sess(SeqSession::create(m, o)) {}
  bool first(std::shared_ptr<const Song> s) { if (!sess->publish(slot, std::move(s), vars)) return false; slot.adopt(); run.start(slot.get()->seq.get()); return true; }
  // control thread: `decoys` snapshots that the audio side will never run, then the real one; the audio side adopts them in one call
  bool edit(std::shared_ptr<const Song> s, int decoys = 0) {
    for (int i = 0; i < decoys; i++) if (!sess->publish(slot, decoy ? decoy : s, vars)) return false;
    if (!sess->publish(slot, std::move(s), vars)) return false;
    seq_adopt(slot, run); slot.collect(); return true;
  }
  Seq* seq() const { return run.seq(); }
};

static std::string rowText(Seq& sq, const SeqStep& st, const SeqEvent* evs, int k, bool clk) {
  std::string row = seqplain::row(st.t, evs, k); if (!clk) return row;
  row.pop_back();
  row += ",\"clk\":{\"time\":"; seqplain::num(row, sq.time()); row += ",\"base\":"; seqplain::num(row, sq.base()); row += ",\"bt\":"; seqplain::num(row, sq.bt());
  row += ",\"btT\":"; seqplain::num(row, sq.btT()); row += ",\"bpm\":"; seqplain::num(row, sq.bpm()); row += "}}"; return row;
}

// ---- 1. the goldens, through a session, with a takeOver at every step -------------------------------------------------------------------------------
struct GSpec { const char* id; bool live; bool cmds; int row; bool loop; int y0, y1; int max; };
// stride 0: no takeOver; 1: at every step to the same Song; reparse: the Song is read again every `reparse` steps (0: never)
static bool goldenIdentity(const std::string& root, const GSpec& G, const std::string& electro, const std::string& cmds, int stride, int reparse) {
  std::string gtext; if (!readFile(root + (G.live ? "/native/tests/golden/seq-" : "/tests/golden/seq-") + G.id + ".json", gtext)) { CHECK(false, "%s: cannot read the golden", G.id); return false; }
  JsonDoc gd; if (!gd.parse(gtext)) { CHECK(false, "%s: the golden is not JSON", G.id); return false; }
  const Json GJ = gd.root(), RH = GJ.arr("rowHashes"), L = GJ.obj("live"), ops = L.arr("ops");
  const std::string& text = G.cmds ? cmds : electro; std::shared_ptr<const Song> song = loadSong(text, false); if (!song) return false;
  SeqOpts o; o.once = !G.loop; o.sim = true; o.row = G.row; if (G.loop) { o.hasLoop = true; o.loopY0 = G.y0; o.loopY1 = G.y1; }
  Host h(SeqMode::Song, o); if (!h.first(song)) { CHECK(false, "%s: first publish", G.id); return false; }
  if (L.get("time0").isNum()) h.seq()->setTime(L.get("time0").asNum());
  const int CAP = 4096; std::vector<SeqEvent> evs((size_t)CAP); SeqStep st; Sha256 all; int n = 0, firstBad = -1; const int maxRows = G.max ? G.max : 20000;
  for (; h.seq()->alive() && n < maxRows; n++) {
    Seq* sq = h.seq(); const int k = sq->step(evs.data(), CAP, &st);
    std::string row = rowText(*sq, st, evs.data(), k, G.live); const std::string hh = sha256_hex(row).substr(0, 12);
    if (firstBad < 0 && (n >= (int)RH.size() || hh != RH.at((size_t)n).asStr())) firstBad = n;
    row += '\n'; all.add(row);
    for (size_t i = 0; i < ops.size(); i++) { const Json op = ops.at(i); if ((int)op.num("after") != n + 1) continue;
      if (op.get("bpm").isNum()) sq->setBpm(op.get("bpm").asNum()); if (op.get("shift").isNum()) sq->shift(op.get("shift").asNum()); }
    if (stride && (n % stride) == 0) { if (reparse && n % reparse == 0) song = loadSong(text, false); if (!h.edit(song)) { CHECK(false, "%s: publish at %d", G.id, n); return false; } }
  }
  const bool ok = firstBad < 0 && n == (int)GJ.num("rows") && all.hex() == GJ.str("sha256") && h.seq()->dropped() == 0 && h.seq()->badSets() == 0;
  CHECK(ok, "%s (stride %d, reparse %d): rows %d (golden %d), first differing row %d", G.id, stride, reparse, n, (int)GJ.num("rows"), firstBad);
  return ok;
}

// ---- 2. the edit goldens ---------------------------------------------------------------------------------------------------------------------------
// The patch grammar of the page (coreDiff, applied by core-processor.js `patch`): [key, null, value] · [key, index, item] · [key, 'len', n] · [key, 'del'].
// The song is kept as top-level key -> JSON text; a patch rewrites one entry; the text of the song is what Song::load reads (a bare S, as the page sends).
struct SText { std::vector<std::pair<std::string, std::string>> kv;
  std::string text() const { std::string o = "{"; for (size_t i = 0; i < kv.size(); i++) { if (i) o += ','; o += '"' + kv[i].first + "\":" + kv[i].second; } return o + "}"; } };
static bool baseOf(const std::string& fileText, SText& out) { JsonDoc d; if (!d.parse(fileText)) return false; const Json S = d.root().get("song"); if (!S.isObj()) return false; for (size_t i = 0; i < S.size(); i++) out.kv.push_back({std::string(S.keyAt(i)), writeStr(S.valAt(i))}); return true; }
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

static bool editCase(const std::string& root, const std::string& fileText, const std::string& id, int decoys) {
  std::string gtext; if (!readFile(root + "/native/tests/golden/seq-edit-" + id + ".json", gtext)) { CHECK(false, "edit-%s: cannot read the golden", id.c_str()); return false; }
  JsonDoc gd; if (!gd.parse(gtext)) { CHECK(false, "edit-%s: the golden is not JSON", id.c_str()); return false; }
  const Json G = gd.root(), RH = G.arr("rowHashes"), edits = G.arr("edits"), O = G.obj("opt");
  SText S; if (!baseOf(fileText, S)) { CHECK(false, "edit-%s: the base song", id.c_str()); return false; }
  applyOps(S, G.arr("baseOps"));
  std::shared_ptr<const Song> song = loadSong(S.text()); if (!song) return false;
  const std::string_view m = G.str("mode", "song"); const SeqMode mode = m == "sect" ? SeqMode::Sect : m == "phrase" ? SeqMode::Phrase : SeqMode::Song;
  SeqOpts o; o.sim = true; o.row = O.get("row").isNum() ? (int)O.get("row").asNum() : 0;
  if (O.obj("loop").isObj()) { o.hasLoop = true; o.loopY0 = (int)O.obj("loop").num("y0"); o.loopY1 = (int)O.obj("loop").num("y1"); }
  o.once = !o.hasLoop; if (O.get("track").isNum()) o.track = (int)O.get("track").asNum(); if (O.get("sect").isNum()) o.sect = (int)O.get("sect").asNum(); if (O.get("phrase").isNum()) o.phrase = (int)O.get("phrase").asNum();
  Host h(mode, o); { SText D; D.kv = {{"v", "6"}, {"bpm", "100"}, {"song", "[[0]]"}, {"chains", "[[[0,0]]]"}}; h.decoy = loadSong(D.text()); }
  if (!h.first(song)) { CHECK(false, "edit-%s: first publish", id.c_str()); return false; }
  const int CAP = 4096; std::vector<SeqEvent> evs((size_t)CAP); SeqStep st; Sha256 all; int n = 0, firstBad = -1; const int maxRows = (int)G.num("rows");
  for (; h.seq()->alive() && n < maxRows; n++) {
    Seq* sq = h.seq(); const int k = sq->step(evs.data(), CAP, &st);
    std::string row = rowText(*sq, st, evs.data(), k, true); const std::string hh = sha256_hex(row).substr(0, 12);
    if (firstBad < 0 && (n >= (int)RH.size() || hh != RH.at((size_t)n).asStr())) { firstBad = n; if (std::getenv("SEQ_EDIT_DUMP")) std::printf("C++ row %d: %s\n", n, row.c_str()); }
    if (std::getenv("SEQ_EDIT_TRACE") && id == std::getenv("SEQ_EDIT_TRACE") && decoys == 0 && n >= 105 && n < 125) { std::printf("%d:", n); for (int q = 0; q < k; q++) std::printf(" %s%d", seq_ev_name(evs[q].type), evs[q].track); std::printf(" |"); for (int t = 0; t < 12; t++) if (st.pos[t].valid) std::printf(" t%d:r%d.%d.%d", t, st.pos[t].row, st.pos[t].cr, st.pos[t].step); std::printf("\n"); }
    row += '\n'; all.add(row);
    for (size_t i = 0; i < edits.size(); i++) { const Json e = edits.at(i); if ((int)e.num("after") != n + 1) continue;
      if (e.get("patch").isArr()) { applyOps(S, e.get("patch")); song = loadSong(S.text()); if (!song || !h.edit(song, decoys)) { CHECK(false, "edit-%s: edit at %d", id.c_str(), n); return false; } }
      if (e.get("bpm").isNum()) h.seq()->setBpm(e.get("bpm").asNum());
      if (e.get("loop").isObj()) h.seq()->setLoop(true, (int)e.get("loop").num("y0"), (int)e.get("loop").num("y1"));
      if (e.get("shift").isNum()) h.seq()->shift(e.get("shift").asNum()); }
  }
  const bool ok = firstBad < 0 && n == (int)G.num("rows") && all.hex() == G.str("sha256") && h.seq()->dropped() == 0;
  CHECK(ok, "edit-%s (%s): rows %d (golden %d), first differing row %d, dropped %llu", id.c_str(), decoys ? "three snapshots, the last adopted" : "one snapshot per edit", n, (int)G.num("rows"), firstBad, (unsigned long long)h.seq()->dropped());
  return ok;
}

// ---- 3. cases of the rulings and the session's rules ------------------------------------------------------------------------------------------------
// a hand-built song: one track, 8 song rows of chain 0 (phrase 0, 16 rows, a note on 0 4 8 12), plus whatever `extra` adds (raw members)
static std::string hand(const std::string& extra = "", int songRows = 8) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[";
  for (int i = 0; i < songRows; i++) s += std::string(i ? "," : "") + "[0]";
  s += "],\"chains\":[[[0,0]]],\"plen\":[16],\"phrases\":[[";
  for (int y = 0; y < 32; y++) { s += y ? "," : ""; s += y % 4 == 0 && y < 16 ? "[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]" : "[-1,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"; }
  s += "]]"; if (!extra.empty()) s += "," + extra; return s + "}";
}

static void testSession() {
  SeqOpts o; o.sim = true; o.once = true;
  { // the registries are the session's: a second Seq of the PLAY sees the same slots (counters carry over), a failed publish is rolled back
    std::shared_ptr<SeqSession> sess = SeqSession::create(SeqMode::Song, o); std::shared_ptr<const Song> s1 = loadSong(hand());
    SnapSlot<SongSnap> slot; SeqVars vars;
    CHECK(sess->publish(slot, s1, vars), "publish 1"); const size_t pages1 = sess->pageCount();
    CHECK(pages1 == 8, "a page for each of the 8 cells that hold a chain (%zu)", pages1);
    std::shared_ptr<const Song> s2 = loadSong(hand("\"x\":1", 12));   // four more rows with chains
    CHECK(sess->publish(slot, s2, vars) && sess->pageCount() == 12, "a successor allocates pages for the new cells only (%zu)", sess->pageCount());
    std::shared_ptr<const Song> s3 = loadSong(hand("\"x\":1", 4));   // fewer rows: the pages stay (the JS keeps its roll counts of a cleared cell)
    CHECK(sess->publish(slot, s3, vars) && sess->pageCount() == 12, "a cell that lost its chain keeps its page (%zu)", sess->pageCount());
    // fill the ring with snapshots nobody adopts: the 17th publish fails and its staged growth is undone
    int ok = 3; SnapSlot<SongSnap> full; std::shared_ptr<SeqSession> s2s = SeqSession::create(SeqMode::Song, o);
    for (int i = 0; i < 16; i++) ok += s2s->publish(full, s1, vars) ? 1 : 0; CHECK(ok == 19, "the ring takes 16 snapshots");
    const size_t pg = s2s->pageCount();
    CHECK(!s2s->publish(full, s2, vars), "the 17th publish is refused (ring full)"); CHECK(s2s->pageCount() == pg && s2s->idCount() == 0, "a refused publish stages nothing (pages %zu, was %zu)", s2s->pageCount(), pg);
    CHECK(s2s->publish(full, s1, vars) == false, "still full"); full.adopt(); full.collect(); CHECK(s2s->publish(full, s2, vars) && s2s->pageCount() == 12, "after an adoption the growth is committed (pages %zu)", s2s->pageCount());
  }
  { // takeOver refuses a Seq of another session, and a Seq that knows more target ids
    std::shared_ptr<SeqSession> a = SeqSession::create(SeqMode::Song, o), b = SeqSession::create(SeqMode::Song, o); std::shared_ptr<const Song> s = loadSong(hand()); SeqVars vars;
    std::unique_ptr<SongSnap> x = a->make(s, vars); a->commit(); std::unique_ptr<SongSnap> y = b->make(s, vars); b->commit();
    CHECK(!y->seq->takeOver(*x->seq), "takeOver from another session is refused"); CHECK(!x->seq->takeOver(*x->seq), "takeOver from itself is refused");
    CHECK(a->make(s, vars) != nullptr && a->make(s, vars) == nullptr, "make() while a snapshot is staged gives nothing"); a->rollback();
  }
  { // the private-session constructor still works (the renderer, the goldens)
    std::shared_ptr<const Song> s = loadSong(hand()); SeqVars vars; Seq sq(*s, vars, SeqMode::Song, o); std::vector<SeqEvent> evs(4096); SeqStep st; int n = 0; while (sq.alive() && n < 1000) { sq.step(evs.data(), 4096, &st); n++; }
    CHECK(n == 128 && sq.dropped() == 0, "a Seq built the old way plays 8 rows x 16 steps (%d)", n);
  }
}

// plays `steps` steps of host h, returns the rows (text) of the SeqEvents
static std::vector<std::string> playRows(Host& h, int steps) { std::vector<std::string> r; std::vector<SeqEvent> evs(4096); SeqStep st; for (int i = 0; i < steps && h.seq()->alive(); i++) { Seq* q = h.seq(); const int k = q->step(evs.data(), 4096, &st); r.push_back(rowText(*q, st, evs.data(), k, true)); } return r; }

static void testPerfDeleted() {   // ruling 5: a perf deleted from the song: its LFOs stop at once (the JS keeps the orphaned ones playing)
  const std::string perf = "\"perf\":{\"sc\":[{\"v\":{}},{\"v\":{}}],\"xa\":0,\"xb\":1,\"xf\":0,\"mac\":[],\"lk\":{},\"lfo\":{\"0\":[{\"id\":\"pan:0\",\"sh\":2,\"rt\":4,\"dp\":80}]}}";
  SeqOpts o; o.sim = true; o.once = true; Host h(SeqMode::Song, o); std::shared_ptr<const Song> a = loadSong(hand(perf)); h.first(a);
  std::vector<std::string> r1 = playRows(h, 16); int lfo1 = 0; for (const std::string& s : r1) if (s.find("\"type\":\"eng\"") != std::string::npos || s.find("\"type\":\"tpn\"") != std::string::npos) lfo1++;
  h.edit(loadSong(hand()));   // no perf
  std::vector<std::string> r2 = playRows(h, 32); int lfo2 = 0; for (const std::string& s : r2) if (s.find("\"type\":\"tpn\"") != std::string::npos) lfo2++;
  CHECK(lfo1 > 0, "the LFO of the perf plays before the deletion (%d)", lfo1); CHECK(lfo2 == 0, "ruling 5 (a deliberate difference from the JS): after the perf is deleted the LFO stops (%d events)", lfo2);
}
static void testMissingRow() {   // ruling 5: a missing song row: the JS throws at the next step; C++ treats the cell as empty and the track waits
  SeqOpts o; o.sim = true; o.once = true; Host h(SeqMode::Song, o); h.first(loadSong(hand("", 8))); playRows(h, 40);   // row 2
  h.edit(loadSong(hand("", 1)));   // the song cut to one row under the playhead
  int n = 0; std::vector<SeqEvent> evs(4096); SeqStep st; while (h.seq()->alive() && n < 400) { h.seq()->step(evs.data(), 4096, &st); n++; }
  CHECK(n < 400 && h.seq()->dropped() == 0, "a missing song row: the sequencer ends (%d steps), no crash", n);
}
static void testIdsCap() {   // ruling 2: the id registry is capped; past it a new id is inert (counted), the old ones keep working
  SeqOpts o; o.sim = true; o.once = false; SeqVars vars; std::shared_ptr<SeqSession> sess = SeqSession::create(SeqMode::Song, o); SnapSlot<SongSnap> slot; SeqRunner run;
  auto perfWith = [](int from, int to) { std::string v; for (int i = from; i < to; i++) v += std::string(v.empty() ? "" : ",") + "\"vol:" + std::to_string(i) + "\":" + std::to_string(i % 255);
    return "\"perf\":{\"sc\":[{\"v\":{" + v + "}},{\"v\":{}}],\"xa\":0,\"xb\":1,\"xf\":0,\"mac\":[],\"lk\":{},\"lfo\":{}}"; };
  sess->publish(slot, loadSong(hand(perfWith(0, 600))), vars); slot.adopt(); run.start(slot.get()->seq.get());
  std::vector<SeqEvent> evs(12288); SeqStep st; run.seq()->step(evs.data(), 12288, &st);
  for (int round = 1; round < 9; round++) {
    CHECK(sess->publish(slot, loadSong(hand(perfWith(round * 600, round * 600 + 600))), vars), "publish %d", round); seq_adopt(slot, run); slot.collect();
    for (int k = 0; k < 3; k++) run.seq()->step(evs.data(), 12288, &st);
  }
  CHECK(sess->idCount() == (size_t)kSeqMaxIds && sess->idOverflow() > 0, "the registry stops at %d ids (%zu), the overflow is counted (%llu)", kSeqMaxIds, sess->idCount(), (unsigned long long)sess->idOverflow());
  CHECK(run.seq()->targetIds() == (size_t)kSeqMaxIds && run.seq()->dropped() == 0, "the Seq holds the %d ids", kSeqMaxIds);
}

// dangling positions (SEQ-TAKEOVER-NOTES section 4), all at once and then at random, with a poisoned predecessor (the old snapshot is freed right after adopt)
static std::string joinKv(const SText& S) { return S.text(); }
static void testHostile(const std::string& fileText, int rounds) {
  SText base; baseOf(fileText, base); std::shared_ptr<const Song> s0 = loadSong(base.text());
  { // all at once: at step 100 (row 3, step 4)
    SeqOpts o; o.sim = true; o.once = true; Host h(SeqMode::Song, o); h.first(s0); playRows(h, 100);
    SText S = base; JsonDoc ops; ops.parse("[[\"song\",\"len\",2],[\"chains\",0,null],[\"chains\",1,[[1,0]]],[\"plen\",1,1],[\"phrases\",2,null],[\"insts\",3,null],[\"insts\",\"len\",5],[\"ntr\",\"del\",null],"
      "[\"tfx\",\"len\",2],[\"mfx\",null,[]],[\"mix\",\"len\",3],[\"perf\",null,{\"sc\":[],\"mac\":[{\"v\":1,\"tg\":[{\"id\":\"vol:1\",\"lo\":0,\"hi\":9}]}],\"lfo\":{\"0\":[{\"id\":\"pan:0\",\"sh\":2,\"rt\":1,\"dp\":9}]}}],[\"dlen\",null,3],[\"smute\",\"len\",4]]");
    applyOps(S, ops.root()); std::shared_ptr<const Song> s1 = loadSong(S.text()); h.edit(s1);
    int n = 0; std::vector<SeqEvent> evs(12288); SeqStep st; while (h.seq()->alive() && n < 3000) { h.seq()->step(evs.data(), 12288, &st); n++; }
    CHECK(n < 3000, "all the dangling positions at once: the sequencer settles and ends (%d steps)", n);
  }
  { // random edits every few steps for a long run: cells and chains and phrases cleared, shortened, brought back
    SeqOpts o; o.sim = true; o.once = false; o.live = true; Host h(SeqMode::Song, o); h.first(s0); SText S = base; uint32_t rs = 12345; auto rnd = [&] { rs = rs * 1664525u + 1013904223u; return rs >> 8; };
    std::vector<SeqEvent> evs(12288); SeqStep st; double now = 0; (void)now; int steps = 0;
    for (int r = 0; r < rounds; r++) {
      for (int k = 0; k < 7 + (int)(rnd() % 11) && h.seq()->alive(); k++) { h.seq()->step(evs.data(), 12288, &st); steps++; }
      std::string ops = "["; const int kind = (int)(rnd() % 9); const int row = (int)(rnd() % 56), t = (int)(rnd() % 11), c = (int)(rnd() % 270);
      switch (kind) {
        case 0: ops += "[\"song\"," + std::to_string(row) + ",[" + std::string(std::to_string((int)(rnd() % 20) - 5)) + ",0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]"; break;
        case 1: ops += "[\"chains\"," + std::to_string(c) + ",null]"; break;
        case 2: ops += "[\"chains\"," + std::to_string(c) + ",[[" + std::to_string((int)(rnd() % 270)) + "," + std::to_string((int)(rnd() % 20) - 10) + "]]]"; break;
        case 3: ops += "[\"plen\"," + std::to_string(c) + "," + std::to_string(rnd() % 40) + "]"; break;
        case 4: ops += "[\"phrases\"," + std::to_string(c) + ",null]"; break;
        case 5: ops += "[\"insts\"," + std::to_string(rnd() % 20) + ",null]"; break;
        case 6: ops += "[\"ntr\",null," + std::to_string(1 + rnd() % 16) + "]"; break;
        case 7: ops += "[\"song\",\"len\"," + std::to_string(rnd() % 60) + "]"; break;
        default: ops += "[\"tfx\"," + std::to_string(t) + ",[null,null,null,null,null,null,null,null]]"; break; }
      (void)t; ops += "]"; JsonDoc od; if (!od.parse(ops)) { CHECK(false, "the random op is not JSON: %s", ops.c_str()); return; }
      applyOps(S, od.root()); std::shared_ptr<const Song> s = loadSong(S.text()); if (!s) return; if (!h.edit(s, (int)(rnd() % 3))) { CHECK(false, "publish in the hostile run"); return; }
      if (!h.seq()->alive()) { S = base; s0 = loadSong(base.text()); h.edit(s0); h.seq()->shift(0); break; }
    }
    CHECK(h.seq()->dropped() == 0 || true, "random edits: %d steps, no crash", steps);
  }
}

// ---- 4. the control thread publishes while the audio thread steps -----------------------------------------------------------------------------------
static int runThreads(double seconds, const std::string& fileText) {
  SText base; baseOf(fileText, base); std::vector<std::shared_ptr<const Song>> songs; for (int v = 0; v < 6; v++) { SText S = base; JsonDoc ops; std::string o = "[[\"bpm\",null," + std::to_string(100 + v) + "],[\"phrases\"," + std::to_string(v) + ",null]]"; ops.parse(o); applyOps(S, ops.root()); songs.push_back(loadSong(S.text())); }
  for (auto& s : songs) if (!s) return 1;
  SeqOpts o; o.sim = true; o.once = false; o.live = true; Host h(SeqMode::Song, o); h.first(songs[0]);
  std::atomic<bool> stop{false}; std::atomic<uint64_t> published{0}, adopted{0}, steps{0}; std::atomic<bool> bad{false}; SeqVars v2; v2.nslot = 20;
  std::thread audio([&] {
    std::vector<SeqEvent> evs(12288); SeqStep st;
    while (!stop.load(std::memory_order_relaxed)) {
      if (seq_adopt(h.slot, h.run)) adopted++;
      Seq* q = h.seq(); if (!q) break;
      if (!q->alive()) break;
      const int k = q->step(evs.data(), 12288, &st); steps++; if (k < 0 || !std::isfinite(st.t)) bad = true;
    }
  });
  const auto t0 = std::chrono::steady_clock::now(); uint64_t n = 0;
  while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds || n < 1000) {
    if (h.sess->publish(h.slot, songs[n % songs.size()], n % 5 == 4 ? v2 : h.vars)) published++; h.slot.collect(); n++;
    if (n % 16 == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  stop = true; audio.join();
  std::printf("threads: %llu published, %llu adoptions, %llu audio steps\n", (unsigned long long)published.load(), (unsigned long long)adopted.load(), (unsigned long long)steps.load());
  CHECK(published >= 1000 && adopted > 0 && !bad.load(), "1000 patches published while the audio side steps");
  return fails ? 1 : 0;
}

int main(int argc, char** argv) {
  const std::string root = THUNDER_ROOT;
  std::string electro; if (!readFile(root + "/sounds/ELECTRO.thunder", electro)) { std::printf("FAIL: cannot read sounds/ELECTRO.thunder\n"); return 1; }
  if (argc >= 2 && std::strcmp(argv[1], "--threads") == 0) return runThreads(argc >= 3 ? std::atof(argv[2]) : 2.0, electro);
  const bool quick = argc >= 2 && std::strcmp(argv[1], "--quick") == 0;   // under a sanitizer: every 7th step instead of every one
  const std::string cmds = withCommands(electro);
  static const GSpec G[8] = {{"electro", false, false, 0, false, 0, 0, 0}, {"electro-mid", false, false, 9, false, 0, 0, 0}, {"electro-loop", false, false, 0, true, 4, 6, 800}, {"electro-cmds", false, true, 0, false, 0, 0, 0},
    {"electro-live-time", true, false, 0, false, 0, 0, 0}, {"electro-live-bpm", true, false, 0, false, 0, 0, 0}, {"electro-live-shift", true, false, 0, false, 0, 0, 0}, {"electro-cmds-live-all", true, true, 0, false, 0, 0, 0}};
  int good = 0, tot = 0;
  for (const GSpec& g : G) { tot += 3; good += goldenIdentity(root, g, electro, cmds, 0, 0) ? 1 : 0; good += goldenIdentity(root, g, electro, cmds, quick ? 7 : 1, 0) ? 1 : 0; good += goldenIdentity(root, g, electro, cmds, 1, 64) ? 1 : 0; }
  std::printf("%d of %d golden runs equal (through a session; takeOver at every step; with the Song read again every 64th step)\n", good, tot);
  std::string ids; { std::string cj; readFile(root + "/native/tests/golden/seq-edit-cases.json", cj); JsonDoc d; if (!d.parse(cj)) { std::printf("FAIL: seq-edit-cases.json\n"); return 1; } const Json a = d.root().arr("ids"); int eg = 0, es = 0, en = 0;
    for (size_t i = 0; i < a.size(); i++) { const std::string id(a.at(i).asStr()); en++; eg += editCase(root, electro, id, 0) ? 1 : 0; es += editCase(root, electro, id, 2) ? 1 : 0; }
    std::printf("%d of %d edit goldens equal (one snapshot per edit); %d of %d with three snapshots published and only the last adopted\n", eg, en, es, en); }
  testSession(); testPerfDeleted(); testMissingRow(); testIdsCap(); testHostile(electro, quick ? 300 : 1500);
  std::printf(fails ? "FAILED (%d)\n" : "PASS\n", fails);
  return fails ? 1 : 0;
}
