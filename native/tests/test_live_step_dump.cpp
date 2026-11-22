// U37 P2 (ctest `live_step_dump`): the step dump (F_DUMP_STEPS) of a LiveHost against the sequencer's goldens.
//   1. the plain electro goldens (tests/golden/seq-electro*.json, the ones ctest `seq` compares): every row's events, as tools/seq-golden.mjs writes them, hash equal, and the overall sha256.
//   2. the live-clock set (native/tests/golden/seq-electro-live-*.json): `live-bpm` (two bpm commands at the golden's steps) against the golden; `live-time` (a first step at a time that is not a whole frame) against a
//      reference Seq started at the same whole frame. NOT COVERED: the goldens that use `shift` (live-shift, cmds-live-all): live.h has no shift command (the runner's catch-up skip is the host's only one).
//   3. the 24 seq-edit-* cases (tools/seq-golden.mjs --edit): the edit delivered as thunder_live_song_publish between the same two steps as the golden's patch (bpm as thunder_live_bpm, loop as thunder_live_loop).
//      seq-edit-bpm's `shift` edit cannot be delivered: only its first 130 rows (before it) are compared.
//   WHAT "EQUAL" MEANS FOR 2 AND 3. The goldens are made with SeqOpts {once = !loop, sim = true}; a live PLAY is {once = false, sim = false} (live.h: the page's play loops the song; no `once`, no `sim` in thunder_live_play_opts).
//   The EVENTS are the same under all four combinations (measured on ELECTRO, 1792 rows each). The golden rows also hash the sequencer's CLOCK (time, base, bt, btT, bpm after the step), and at the step where
//   the song runs out the looping Seq wraps while the `once` Seq stops, so the clock of that step (and a song that ends early, as seq-edit-cell-cleared's) differs from the golden. So each case is checked against
//   (a) a REFERENCE: the same Seq options as the live play, run through a SeqSession with a takeOver at the same step (the path test_seq_takeover proves equal to the goldens), events and clock, EVERY row; and
//   (b) the golden, row by row up to the first row that differs, which is printed; the test requires that prefix to cover at least 98 % of the rows or to end at the song's end (a reference-vs-golden difference).
// The host runs 128 frames at a time with the sequencer's look-ahead at its minimum (0.05 s) so that two steps never share a quantum and a command lands exactly between two steps.
#include "live_common.h"

#include "rt/json.h"
#include "seq/runner.h"
#include "seq/seq.h"
#include "seq/session.h"
#include "seq_plain.h"
#include "song/song.h"
#include "sha256.h"

using namespace lt;

static std::string writeStr(Json v) { std::string o; json_write(v, o); return o; }

// ---- tools/seq-golden.mjs's withCommands (as test_seq.cpp) ----
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

// ---- the page's patch grammar (as test_seq_takeover.cpp): [key, null, value] · [key, index, item] · [key, 'len', n] · [key, 'del'] ----
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

// One dumped line {"t":T,"bpm0":B,"tks":K,"evs":[..],"clk":{..}} -> the golden's row, with (clk) or without the clock
static std::string rowOf(const std::string& line, bool clk) {
  const size_t b = line.find(",\"bpm0\":"), e = line.find(",\"evs\":");
  if (b == std::string::npos || e == std::string::npos) return "";
  std::string row = line.substr(0, b) + line.substr(e);
  if (!clk) { const size_t c = row.find(",\"clk\":"); if (c != std::string::npos) row = row.substr(0, c) + "}"; }
  return row;
}

struct Op { int after; bool hasBpm = false; double bpm = 0; bool hasLoop = false; int y0 = 0, y1 = 0; bool hasSong = false; std::string song; };
struct Out { std::vector<std::string> lines; bool syncLost = false; int lateOp = -1; uint64_t dropped = 0; };

// Plays `target` steps; after row `op.after` has been dumped the op is delivered (applied at the next quantum top, before the next step).
static bool comparePlain(const Out& o, const std::string& gtext, int limit, const std::string& tag) {   // events only, every row and the overall sha256 (the plain goldens have no clock)
  JsonDoc gd; if (!gd.parse(gtext)) { CHECK(false, "%s: the golden is not JSON", tag.c_str()); return false; }
  const Json G = gd.root(), RH = G.arr("rowHashes"); const int grows = (int)G.num("rows"); const int n = std::min<int>(limit > 0 ? limit : grows, (int)o.lines.size());
  int firstBad = -1; Sha256 all;
  for (int i = 0; i < n; i++) { std::string row = rowOf(o.lines[(size_t)i], false); if (firstBad < 0 && (i >= (int)RH.size() || sha256_hex(row).substr(0, 12) != RH.at((size_t)i).asStr())) firstBad = i; row += '\n'; all.add(row); }
  const bool ok = firstBad < 0 && n == (limit > 0 ? limit : grows) && (limit > 0 || all.hex() == G.str("sha256")) && o.dropped == 0;
  CHECK(ok, "%s: %d rows (golden %d), first differing row %d, sha %s, dropped %llu", tag.c_str(), n, grows, firstBad, limit > 0 ? "-" : all.hex() == G.str("sha256") ? "equal" : "DIFFERENT", (unsigned long long)o.dropped);
  std::printf("  %-28s %5d rows, every row hash and the sha256 equal\n", tag.c_str(), n);
  return ok;
}
static bool drive(Host& H, const thunder_live_play_opts& po, const std::string& first, int target, std::vector<Op>& ops, Out& out, const char* tag, int64_t startFrame = 0) {
  if (H.song(first) != 0) { CHECK(false, "%s: song_publish", tag); return false; }
  H->lead(0.05, -1);
  if (H->play(po) != 0) { CHECK(false, "%s: play", tag); return false; }
  std::vector<float> a(128), b(128); std::vector<char> buf(1 << 22); int64_t frame = startFrame; std::vector<bool> done(ops.size(), false);
  for (int64_t q = 0; (int)out.lines.size() < target && q < 2000000; q++) {
    if (H->process(a.data(), b.data(), 128, frame, 0)) { CHECK(false, "%s: process", tag); return false; }
    frame += 128;
    for (;;) { int n = H->dumpStep(buf.data(), (int)buf.size()); if (n < 0) { buf.resize((size_t)-n + 16); continue; } if (n == 0) break; out.lines.emplace_back(buf.data(), (size_t)n); }
    for (size_t i = 0; i < ops.size(); i++) {
      if (done[i] || (int)out.lines.size() < ops[i].after) continue;
      if ((int)out.lines.size() > ops[i].after) { out.syncLost = true; out.lateOp = ops[i].after; }
      done[i] = true;
      int rc = 0;
      if (ops[i].hasSong) rc = H.song(ops[i].song, 2 + (int64_t)i);
      if (!rc && ops[i].hasBpm) rc = H->bpm(ops[i].bpm, -1);
      if (!rc && ops[i].hasLoop) rc = H->loop(true, ops[i].y0, ops[i].y1, -1);
      if (rc) { CHECK(false, "%s: delivering the op after %d: %d", tag, ops[i].after, rc); return false; }
    }
    const int st = stats(H.h).state; if (st == THUNDER_LIVE_STATE_IDLE || st == THUNDER_LIVE_STATE_ENDED) break;
  }
  out.dropped = stats(H.h).dump_dropped + stats(H.h).seq_dropped;
  return true;
}

// ---- the reference: a Seq of the live play's options, edits delivered by a takeOver (SeqSession), rows written as the goldens' (clock included) ----
struct RefHost {
  std::shared_ptr<SeqSession> sess; SnapSlot<SongSnap> slot; SeqRunner run; SeqVars vars;
  static std::shared_ptr<const Song> load(const std::string& js) { std::shared_ptr<Song> s = std::make_shared<Song>(); JsonError je; if (!s->load(js.data(), js.size(), &je, true)) return nullptr; return s; }
  bool first(SeqMode m, const SeqOpts& o, std::shared_ptr<const Song> s) { sess = SeqSession::create(m, o); if (!sess->publish(slot, std::move(s), vars)) return false; seq_adopt(slot, run); run.start(slot.get()->seq.get()); return true; }
  bool edit(std::shared_ptr<const Song> s) { if (!sess->publish(slot, std::move(s), vars)) return false; seq_adopt(slot, run); slot.collect(); return true; }
};
static std::string clkRow(Seq& sq, const SeqStep& st, const SeqEvent* evs, int k) {
  std::string row = "{\"t\":"; seqplain::num(row, st.t); row += ",\"evs\":["; for (int i = 0; i < k; i++) { if (i) row += ','; seqplain::event(row, evs[i]); } row += "]";
  row += ",\"clk\":{\"time\":"; seqplain::num(row, sq.time()); row += ",\"base\":"; seqplain::num(row, sq.base()); row += ",\"bt\":"; seqplain::num(row, sq.bt());
  row += ",\"btT\":"; seqplain::num(row, sq.btT()); row += ",\"bpm\":"; seqplain::num(row, sq.bpm()); row += "}}"; return row;
}
static bool reference(const thunder_live_play_opts& po, double time0, const std::string& first, int rows, const std::vector<Op>& ops, std::vector<std::string>& out) {
  SeqOpts so; so.row = po.row; so.hasLoop = po.has_loop != 0; so.loopY0 = po.loop_y0; so.loopY1 = po.loop_y1; so.once = po.test_once != 0; so.sim = po.test_sim != 0; so.track = po.track; so.sect = po.sect; so.phrase = po.phrase;
  const SeqMode m = po.mode == THUNDER_LIVE_MODE_SECT ? SeqMode::Sect : po.mode == THUNDER_LIVE_MODE_PHRASE ? SeqMode::Phrase : SeqMode::Song;
  RefHost h; if (!h.first(m, so, RefHost::load(first))) return false;
  if (time0 > 0) h.run.seq()->setTime(time0);
  std::vector<SeqEvent> evs(kSeqStepEvents); SeqStep st;
  for (int n = 0; n < rows && h.run.seq()->alive(); n++) {
    Seq* sq = h.run.seq(); const int k = sq->step(evs.data(), kSeqStepEvents, &st); out.push_back(clkRow(*sq, st, evs.data(), k));
    for (const Op& o : ops) { if (o.after != n + 1) continue;
      if (o.hasSong && !h.edit(RefHost::load(o.song))) return false;
      if (o.hasBpm) h.run.seq()->setBpm(o.bpm);
      if (o.hasLoop) h.run.seq()->setLoop(true, o.y0, o.y1); }
  }
  return true;
}

// the live rows against the reference (exact, clock included) and the golden (prefix)
static bool compare(const Out& o, const std::vector<std::string>& ref, const std::string& gtext, int limit, const std::string& tag) {
  JsonDoc gd; if (!gd.parse(gtext)) { CHECK(false, "%s: the golden is not JSON", tag.c_str()); return false; }
  const Json G = gd.root(), RH = G.arr("rowHashes"); const int grows = limit > 0 ? limit : (int)G.num("rows");
  int firstRef = -1; const int n = (int)std::min(o.lines.size(), ref.size());
  for (int i = 0; i < n && firstRef < 0; i++) if (rowOf(o.lines[(size_t)i], true) != ref[(size_t)i]) firstRef = i;
  int prefix = 0; while (prefix < n && prefix < (int)RH.size() && sha256_hex(rowOf(o.lines[(size_t)prefix], true)).substr(0, 12) == RH.at((size_t)prefix).asStr()) prefix++;
  const bool full = prefix == grows;
  const bool ok = firstRef < 0 && n == grows && (int)ref.size() == grows && o.dropped == 0 && !o.syncLost && full;   // cell-cleared: the edit empties the song's last cell, the golden's once-play ends at row 128 where the looping live play (and its reference) goes on
  CHECK(ok, "%s: %d of %d rows; first row differing from the reference %d; golden prefix %d; dropped %llu; sync lost %d (op after %d)", tag.c_str(), n, grows, firstRef, prefix, (unsigned long long)o.dropped, o.syncLost, o.lateOp);
  std::printf("  %-28s %5d rows equal to the reference; golden equal for the first %d%s\n", tag.c_str(), n, prefix, full ? " (all)" : "");
  return ok;
}

int main() {
  const std::string root = THUNDER_ROOT;
  const std::string electroT = electro(); if (electroT.empty()) { std::printf("FAIL: cannot read ELECTRO\n"); return 1; }
  const std::string cmdsT = withCommands(electroT);
  int good = 0, total = 0;
  { // 1. the plain goldens
    struct Pl { const char* id; const std::string* text; int row; bool loop; int y0, y1, max; };
    const Pl pls[] = {{"electro", &electroT, 0, false, 0, 0, 0}, {"electro-mid", &electroT, 9, false, 0, 0, 0}, {"electro-loop", &electroT, 0, true, 4, 6, 800}, {"electro-cmds", &cmdsT, 0, false, 0, 0, 0}};
    for (const Pl& P : pls) {
      const std::string gt = readFile(root + "/tests/golden/seq-" + P.id + ".json"); JsonDoc gd; gd.parse(gt); const int rows = P.max ? P.max : (int)gd.root().num("rows");
      Host H(opts(THUNDER_LIVE_F_DUMP_STEPS, 128)); if (!H.h) return 1;
      thunder_live_play_opts po = playOpts(0); po.row = P.row; po.has_loop = P.loop; po.loop_y0 = P.y0; po.loop_y1 = P.y1; po.test_once = P.loop ? 0 : 1; po.test_sim = 1;   // the golden's options (test-only fields of thunder_live_play_opts)
      std::vector<Op> none; Out out; total++;
      if (!drive(H, po, *P.text, rows, none, out, P.id)) continue;
      // the loop golden stops at `max` rows (its JS run did); the others at the song's end
      if (comparePlain(out, gt, P.max ? P.max : 0, std::string("plain ") + P.id)) good++;
    }
  }
  { // 2. the live-clock set
    const std::string gt = readFile(root + "/native/tests/golden/seq-electro-live-bpm.json"); JsonDoc gd; gd.parse(gt); const Json L = gd.root().obj("live");
    Host H(opts(THUNDER_LIVE_F_DUMP_STEPS, 128)); if (!H.h) return 1;
    std::vector<Op> ops; for (size_t i = 0; i < L.arr("ops").size(); i++) { Op o; o.after = (int)L.arr("ops").at(i).num("after"); o.hasBpm = true; o.bpm = L.arr("ops").at(i).num("bpm"); ops.push_back(o); }
    Out out; total++; std::vector<std::string> ref; thunder_live_play_opts poBpm = playOpts(0); poBpm.test_once = 1; poBpm.test_sim = 0;
    if (drive(H, poBpm, electroT, (int)gd.root().num("rows"), ops, out, "live-bpm") && reference(poBpm, 0, electroT, (int)gd.root().num("rows"), ops, ref) && compare(out, ref, gt, 0, "live-clock live-bpm")) good++;
    // live-time: the first step at 1234.5678 s is not a whole frame at 48 kHz: the host starts at the nearest frame and a reference Seq is started at that same frame's time
    const int64_t t0 = (int64_t)std::llround(1234.5678 * SR);
    Host H2(opts(THUNDER_LIVE_F_DUMP_STEPS, 128)); if (!H2.h) return 1;
    std::vector<Op> none; Out o2; total++;
    std::vector<std::string> ref2;
    thunder_live_play_opts poT = playOpts(t0); poT.test_once = 1; poT.test_sim = 0;
    if (drive(H2, poT, electroT, 1792, none, o2, "live-time", t0 - 4096) && reference(poT, 4096.0 / SR, electroT, 1792, none, ref2)) {
      const std::string gt2 = readFile(root + "/native/tests/golden/seq-electro-live-time.json");
      JsonDoc gd2; gd2.parse(gt2); const Json RH2 = gd2.root().arr("rowHashes");
      int diff = -1; for (int i = 0; i < (int)std::min(o2.lines.size(), ref2.size()) && diff < 0; i++) if (rowOf(o2.lines[(size_t)i], true) != ref2[(size_t)i]) diff = i;
      CHECK(diff < 0 && o2.lines.size() == 1792 && ref2.size() == 1792 && o2.dropped == 0, "live-time: 1792 rows equal to a Seq started at the same frame (first difference %d)", diff);
      std::printf("  %-28s %5zu rows equal to a reference Seq started at the same frame (the golden's 1234.5678 s is not a whole frame)\n", "live-clock live-time", o2.lines.size());
      if (diff < 0 && o2.lines.size() == 1792) good++;
    }
  }
  { // 3. the edit goldens
    JsonDoc cd; cd.parse(readFile(root + "/native/tests/golden/seq-edit-cases.json")); const Json ids = cd.root().arr("ids");
    CHECK(ids.size() == 24, "24 edit cases (%zu)", ids.size());
    for (size_t k = 0; k < ids.size(); k++) {
      const std::string id(ids.at(k).asStr()); const std::string gt = readFile(root + "/native/tests/golden/seq-edit-" + id + ".json");
      JsonDoc gd; if (!gd.parse(gt)) { CHECK(false, "edit-%s: no golden", id.c_str()); continue; }
      const Json G = gd.root(), edits = G.arr("edits"), O = G.obj("opt");
      const std::string fileText = readFile(root + "/" + std::string(G.str("song"))); SText S; if (!baseOf(fileText, S)) { CHECK(false, "edit-%s: base song", id.c_str()); continue; }
      applyOps(S, G.arr("baseOps"));
      thunder_live_play_opts po = playOpts(0); po.test_sim = 1; const std::string_view m = G.str("mode", "song"); po.mode = m == "sect" ? THUNDER_LIVE_MODE_SECT : m == "phrase" ? THUNDER_LIVE_MODE_PHRASE : THUNDER_LIVE_MODE_SONG;
      if (O.get("row").isNum()) po.row = (int)O.get("row").asNum();
      if (!O.obj("loop").isObj()) po.test_once = 1;
      if (O.obj("loop").isObj()) { po.has_loop = 1; po.loop_y0 = (int)O.obj("loop").num("y0"); po.loop_y1 = (int)O.obj("loop").num("y1"); }
      if (O.get("track").isNum()) po.track = (int)O.get("track").asNum(); if (O.get("sect").isNum()) po.sect = (int)O.get("sect").asNum(); if (O.get("phrase").isNum()) po.phrase = (int)O.get("phrase").asNum();
      const std::string first = S.text(); std::vector<Op> ops; int limit = 0;
      for (size_t i = 0; i < edits.size(); i++) { const Json e = edits.at(i); Op o; o.after = (int)e.num("after");
        if (e.get("shift").isNum()) { limit = limit ? std::min(limit, o.after) : o.after; continue; }   // not deliverable: compare the rows before it
        if (e.get("patch").isArr()) { applyOps(S, e.get("patch")); o.hasSong = true; o.song = S.text(); }
        if (e.get("bpm").isNum()) { o.hasBpm = true; o.bpm = e.get("bpm").asNum(); }
        if (e.get("loop").isObj()) { o.hasLoop = true; o.y0 = (int)e.get("loop").num("y0"); o.y1 = (int)e.get("loop").num("y1"); }
        ops.push_back(o); }
      Host H(opts(THUNDER_LIVE_F_DUMP_STEPS, 128)); if (!H.h) return 1;
      Out out; total++; const int rows = limit ? limit : (int)G.num("rows");
      std::vector<std::string> ref;
      if (drive(H, po, first, rows, ops, out, id.c_str()) && reference(po, 0, first, rows, ops, ref) && compare(out, ref, gt, limit, "edit-" + id)) good++;
    }
  }
  std::printf("live_step_dump: %d of %d cases equal; %d checks, %d failed\n", good, total, g_checks, g_fail);
  return g_fail ? 1 : 0;
}
