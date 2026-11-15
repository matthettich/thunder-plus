// U07: the sequencer against the JS's goldens (ctest `seq`).
//   For each case of tools/seq-golden.mjs (ELECTRO whole; from row 9, which runs CHASE; rows 4-6 looped; a command written
//   into every free third command column) the C++ sequencer runs sounds/ELECTRO.thunder and every row's events are written
//   as the tool writes them (seq_plain.h) and hashed: each of the golden's rowHashes and its overall sha256 must be equal,
//   exactly. Nothing is rounded beyond what the tool itself rounds (1e-6), and no golden is ever edited from here.
//   On a mismatch the first differing row is printed with the JS's row beside it when a dump is at hand
//   (node native/tools/seq-dump.mjs writes /tmp/seqdump-node/; SEQ_DUMP_DIR names another folder).
//   test_seq --case FILE.json DIR   one extra case (see seq-dump.mjs --case) against DIR/seq-<id>.json: ctest `seq_extra`.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rt/json.h"
#include "seq/seq.h"
#include "song/song.h"

#include "seq_plain.h"
#include "sha256.h"

using namespace thunder;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static bool readFile(const std::string& path, std::string& out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); out = s.str(); return true; }

// tools/seq-golden.mjs's withCommands: every phrase row with a note gets one command in its third command column (if free),
// cycling through CMDS. The song text is written again with the phrases changed; everything else as it stands.
static std::string withCommands(const std::string& text, int* count) {
  static const int CMDS[16][2] = {{0 /*ARP*/, 0x37}, {11 /*RET*/, 0x24}, {3 /*DEL*/, 0x03}, {43 /*PRB*/, 0x80}, {42 /*GEN*/, 0x41}, {2 /*CHA*/, 0x80}, {13 /*RND*/, 0x24}, {20 /*TSP*/, 0x05},
    {22 /*PIT*/, 0x0C}, {21 /*VOL*/, 0x90}, {26 /*PAN*/, 0x40}, {10 /*NTH*/, 0x02}, {8 /*KIL*/, 0x04}, {9 /*OFF*/, 0x06}, {27 /*TIC*/, 0x03}, {14 /*RNL*/, 0x13}};
  JsonDoc d; if (!d.parse(text)) return text;
  const Json root = d.root(); std::string o = "{"; int n = 0;
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
          const int* cv = CMDS[(p * 7 + y) % 16]; n++; o += '[';
          for (size_t k = 0; k < r.size(); k++) { if (k) o += ','; if (k == 10) o += std::to_string(cv[0]); else if (k == 11) o += std::to_string(cv[1]); else json_write(r.at(k), o); }
          o += ']'; }
        o += ']'; }
      o += ']'; }
    o += '}'; }
  o += '}'; *count = n; return o;
}

struct Case { std::string id; std::string songText; SeqMode mode = SeqMode::Song; SeqOpts opt; int max = 0; SeqVars vars; bool full = false; };
struct Stats { int rows = 0, maxEvs = 0, maxArp = 0; size_t rollPlaces = 0, countRows = 0, ids = 0; };

// Runs a case; false on a mismatch (already printed).
static bool runCase(const Case& C, const std::string& goldenPath, const std::string& dumpPath, Stats* st) {
  std::string gtext; if (!readFile(goldenPath, gtext)) { CHECK(false, "%s: cannot read %s", C.id.c_str(), goldenPath.c_str()); return false; }
  JsonDoc gd; JsonError je; if (!gd.parse(gtext, &je)) { CHECK(false, "%s: the golden is not JSON", C.id.c_str()); return false; }
  const Json G = gd.root(), RH = G.arr("rowHashes"); const int grows = (int)G.num("rows");
  Song song; if (!song.load(C.songText.data(), C.songText.size(), &je)) { CHECK(false, "%s: the song does not load: %s", C.id.c_str(), je.msg ? je.msg : "?"); return false; }
  Seq sq(song, C.vars, C.mode, C.opt);
  const int CAP = 4096; std::vector<SeqEvent> evs((size_t)CAP); SeqStep step;
  std::vector<std::string> dump; { std::string dt; if (readFile(dumpPath, dt)) { std::istringstream is(dt); std::string l; while (std::getline(is, l)) dump.push_back(l); } }
  Sha256 all; int n = 0, firstBad = -1; std::string badMine;
  const int maxRows = C.max ? C.max : 20000;
  for (; sq.alive() && n < maxRows; n++) {
    const int k = sq.step(evs.data(), CAP, &step);
    std::string row = seqplain::row(step.t, evs.data(), k, C.full ? &sq : nullptr);
    const std::string h = sha256_hex(row).substr(0, 12);
    if (firstBad < 0 && (n >= (int)RH.size() || h != RH.at((size_t)n).asStr())) { firstBad = n; badMine = row; }
    row += '\n'; all.add(row);
  }
  st->rows = n; st->maxEvs = sq.maxEventsPerStep(); st->maxArp = sq.maxArpPairsPerStep(); st->rollPlaces = sq.rollPlaces(); st->countRows = sq.countRows(); st->ids = sq.targetIds();
  const std::string sha = all.hex();
  CHECK(sq.dropped() == 0, "%s: %llu events or counters did not fit", C.id.c_str(), (unsigned long long)sq.dropped());
  const bool ok = firstBad < 0 && n == grows && (int)RH.size() == grows && sha == G.str("sha256");
  if (!ok) {
    CHECK(false, "%s: rows %d (golden %d), sha256 %s (golden %s), first differing row %d", C.id.c_str(), n, grows, sha.c_str(), std::string(G.str("sha256")).c_str(), firstBad);
    if (firstBad >= 0) { std::printf("  C++ row %d:\n%s\n", firstBad, badMine.c_str());
      if (firstBad < (int)dump.size()) std::printf("  JS  row %d:\n%s\n", firstBad, dump[(size_t)firstBad].c_str()); else std::printf("  (no JS dump at %s: node native/tools/seq-dump.mjs writes it)\n", dumpPath.c_str()); }
  }
  return ok && sq.dropped() == 0;
}

static void optsFromJson(Json o, SeqOpts& q) {
  if (o.get("row").isNum()) q.row = (int)o.get("row").asNum();
  const Json l = o.obj("loop"); if (!l.missing()) { q.hasLoop = true; q.loopY0 = (int)l.num("y0"); q.loopY1 = (int)l.num("y1"); }
  q.once = o.get("once").isBool() ? o.get("once").asBool() : !q.hasLoop; q.sim = true; q.live = o.boolean("live");
  if (o.get("track").isNum()) q.track = (int)o.get("track").asNum();
  if (o.get("sect").isNum()) q.sect = (int)o.get("sect").asNum();
  if (o.get("phrase").isNum()) q.phrase = (int)o.get("phrase").asNum();
  if (o.get("seed").isNum()) { q.hasSeed = true; q.seed = js_to_uint32(o.get("seed").asNum()); }
  const Json at = o.arr("at"); if (!at.missing()) { q.hasAt = true; q.atRef = (int)o.num("atRef"); for (size_t i = 0; i < at.size() && i < (size_t)kSeqTracks; i++) { const Json a = at.at(i); if (!a.isObj()) continue;
      q.at[i].valid = true; q.at[i].row = (int)a.num("row"); q.at[i].cr = (int)a.num("cr", -1); q.at[i].phrase = (int)a.num("phrase", -1); q.at[i].step = (int)a.num("step"); } }
  const Json on = o.arr("only"); if (!on.missing()) { q.hasOnly = true; for (size_t i = 0; i < on.size(); i++) q.only |= 1ull << (int)on.at(i).asNum(); }
}

int main(int argc, char** argv) {
  const std::string root = THUNDER_ROOT;
  const char* dd = std::getenv("SEQ_DUMP_DIR"); const std::string dumpDir = dd ? dd : "/tmp/seqdump-node";
  if (argc >= 4 && std::strcmp(argv[1], "--case") == 0) {   // an extra case made by seq-dump.mjs --case
    std::string ct; if (!readFile(argv[2], ct)) { std::printf("FAIL: cannot read %s\n", argv[2]); return 1; }
    JsonDoc cd; if (!cd.parse(ct)) { std::printf("FAIL: the case is not JSON\n"); return 1; }
    const Json J = cd.root(); Case C; C.id = std::string(J.str("id"));
    if (J.get("song").isObj()) C.songText = json_write(J.get("song")); else if (!readFile(J.str("file").substr(0, 1) == "/" ? std::string(J.str("file")) : root + "/" + std::string(J.str("file")), C.songText)) { std::printf("FAIL: cannot read the case's song\n"); return 1; }
    if (J.str("edit") == "cmds") { int n = 0; C.songText = withCommands(C.songText, &n); }
    const std::string_view m = J.str("mode", "song"); C.mode = m == "sect" ? SeqMode::Sect : m == "phrase" ? SeqMode::Phrase : SeqMode::Song;
    optsFromJson(J.get("opt"), C.opt); if (J.get("opt").missing()) { C.opt.once = true; C.opt.sim = true; }
    C.max = (int)J.num("max"); C.vars.load(J.get("vars")); C.full = J.boolean("full");
    Stats st; const bool ok = runCase(C, std::string(argv[3]) + "/seq-" + C.id + ".json", std::string(argv[3]) + "/seq-" + C.id + ".jsonl", &st);
    std::printf("%s %s: %d rows, at most %d events a step, %zu target ids\n", ok ? "ok  " : "FAIL", C.id.c_str(), st.rows, st.maxEvs, st.ids);
    return ok && !fails ? 0 : 1;
  }
  std::string electro; if (!readFile(root + "/sounds/ELECTRO.thunder", electro)) { std::printf("FAIL: cannot read sounds/ELECTRO.thunder\n"); return 1; }
  int ncmd = 0; const std::string cmds = withCommands(electro, &ncmd);
  CHECK(ncmd == 1066, "withCommands wrote %d commands (the JS tool: 1066)", ncmd);
  std::vector<Case> cases(4);
  cases[0].id = "electro"; cases[0].songText = electro; cases[0].opt.once = true;
  cases[1].id = "electro-mid"; cases[1].songText = electro; cases[1].opt.row = 9; cases[1].opt.once = true;
  cases[2].id = "electro-loop"; cases[2].songText = electro; cases[2].opt.hasLoop = true; cases[2].opt.loopY0 = 4; cases[2].opt.loopY1 = 6; cases[2].opt.once = false; cases[2].max = 800;
  cases[3].id = "electro-cmds"; cases[3].songText = cmds; cases[3].opt.once = true;
  int good = 0;
  for (Case& C : cases) { C.opt.sim = true;   // op:'test': {row, loop, once:!loop, sim:true}, Math.random seeded with 0x2545F491 (SeqOpts' default)
    Stats st; const bool ok = runCase(C, root + "/tests/golden/seq-" + C.id + ".json", dumpDir + "/seq-" + C.id + ".jsonl", &st); if (ok) good++;
    std::printf("%s %s: %d rows; largest: %d events a step, %d arp pairs a step; %zu roll counters, %zu NTH / CND rows, %zu target ids\n", ok ? "ok  " : "FAIL", C.id.c_str(), st.rows, st.maxEvs, st.maxArp, st.rollPlaces, st.countRows, st.ids); }
  std::printf("%d of 4 sequencer goldens equal (every row hash and the overall sha256)\n", good);
  std::printf(fails ? "FAILED (%d)\n" : "PASS\n", fails);
  return fails ? 1 : 0;
}
