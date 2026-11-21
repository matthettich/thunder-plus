// U09: bus_plan / chains_feed / mix_audible (core/src/mix) against the JS's busPlan / chainsFeed / audible (core/seq-core.js and
// index.html, run by tools/mix-dump.mjs --busplan): hand-made patches (a bus, PRE sends, a send that keeps its own out, a track with
// its own Track In, odd ids and values, tracks out of range) and every song in sounds/. Exact.
//   test_busplan <dir>/busplan.json [more.json ...]
// BUS INTO BUS (web build 651): the same cases from native/tests/golden/bus-into-bus-native.json (the web's own functions, cut out of index.html at build
// 651 and run in a vm by tools/make-bus-fixtures.mjs on branch beta; copied here, so no Node is needed): the new fields loopEdges, srcAll, busAll are compared when present.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "mix/mix.h"
#include "rt/json.h"
#include "song/song.h"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("busplan: no reference file (Node not found): not run\n"); return 0; }
  int fails = 0, total = 0;
  for (int ai = 1; ai < argc; ai++) {
  std::string js; if (!readFile(argv[ai], &js)) { std::printf("busplan: cannot read %s\n", argv[ai]); return 1; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("busplan: bad JSON in %s\n", argv[ai]); return 1; }
  Json cases = doc.root(); total += (int)cases.size();
  for (size_t i = 0; i < cases.size(); i++) {
    Json C = cases.at(i), W = C.get("want"); const std::string id(C.str("id"));
    bool chainOn[kSongNT] = {false}; uint32_t mute = 0, solo = 0; Json patch; Song song; std::string text;
    if (!C.get("file").missing()) {
      if (!readFile(std::string(THUNDER_ROOT) + "/" + std::string(C.str("file")), &text) || !song.load(text.data(), text.size(), nullptr)) { std::printf("%-28s cannot load the song  FAIL\n", id.c_str()); fails++; continue; }
      patch = song.chainPatch; for (int t = 0; t < kSongNT; t++) { chainOn[t] = C.boolean("allOn") || song.chainOn[t]; if (song.mix[t].mute) mute |= 1u << t; if (song.mix[t].solo) solo |= 1u << t; }
    } else { Json S = C.get("S"); patch = S.get("chainPatch"); for (int t = 0; t < kSongNT; t++) { chainOn[t] = S.arr("chainOn").at((size_t)t).asBool(); Json m = S.arr("mix").at((size_t)t); if (m.boolean("mute")) mute |= 1u << t; if (m.boolean("solo")) solo |= 1u << t; } }
    const BusPlan P = bus_plan(patch, chainOn);
    std::string why;
    if (id == "cross") {   // SUPERSEDED by bus into bus: the old web (this checkout's index.html) said "a track with a Track In is no bus"; now 1 -> 4 is kept and 4 -> 1 (the higher source) is left out
      if (P.feedMask[3] != 1u || P.targets[0] != 8u || P.targets[3] != 0u || P.feedMask[0] != 0u || P.pure != 1u || !P.loop || P.loopTo[3] != 1u) why += " (bus-into-bus loop rule)";
      std::printf("%-28s superseded by bus into bus (loop: the closing cable of the higher source is left out)%s\n", id.c_str(), why.empty() ? "  ok" : ("  FAIL" + why).c_str()); if (!why.empty()) fails++; continue; }
    for (int t = 0; t < kSongNT; t++) {
      if ((double)P.feedMask[t] != W.arr("feeds").at((size_t)t).asNum()) why += " feeds[" + std::to_string(t) + "]";
      if ((double)P.targets[t] != W.arr("targets").at((size_t)t).asNum()) why += " targets[" + std::to_string(t) + "]";
      Json L = W.get("lists").get(std::to_string(t));
      if ((int)L.size() != P.feedLen[t]) why += " list length[" + std::to_string(t) + "]";
      else for (int j = 0; j < P.nFeeds[t]; j++) if ((double)P.feeds[t][j] != L.at((size_t)j).asNum()) { why += " list[" + std::to_string(t) + "]"; break; }
    }
    if ((double)P.pure != W.num("pure")) why += " pure";
    if ((double)P.pre != W.num("pre")) why += " pre";
    if (P.loop != W.boolean("loop")) why += " loop";
    if (!W.get("srcAll").missing()) {   // bus into bus: the closures and the cables left out
      for (int t = 0; t < kSongNT; t++) {
        if ((double)P.srcAll[t] != W.arr("srcAll").at((size_t)t).asNum()) why += " srcAll[" + std::to_string(t) + "]";
        if ((double)P.busAll[t] != W.arr("busAll").at((size_t)t).asNum()) why += " busAll[" + std::to_string(t) + "]";
      }
      Json LE = W.arr("loopEdges"); int nl = 0; for (int t = 0; t < kSongNT; t++) for (int b = 0; b < kSongNT; b++) if (P.loopTo[t] >> b & 1u) nl++;
      if ((int)LE.size() != nl) why += " loopEdges (count)";
      for (size_t j = 0; j < LE.size(); j++) { const int a = (int)LE.at(j).at(0).asNum(), b = (int)LE.at(j).at(1).asNum(); if (a < 0 || a >= kSongNT || b < 0 || b >= kSongNT || !(P.loopTo[a] >> b & 1u)) why += " loopEdges"; }
    }
    if ((double)chains_feed(patch, chainOn) != W.num("chainsFeed")) why += " chainsFeed";
    { const uint32_t nm = W.get("srcAll").missing() ? 0xffffffffu : 0xffffu;   // the bus-into-bus fixture was made with 16 tracks
      if ((double)(mix_audible(P, mute, solo) & nm) != W.num("audible")) why += " audible"; }
    int buses = 0, srcs = 0; for (int t = 0; t < kSongNT; t++) { if (P.feedLen[t]) buses++; if (P.targets[t]) srcs++; }
    std::printf("%-28s %d bus(es), %d source(s)%s%s\n", id.c_str(), buses, srcs, why.empty() ? "  ok" : ("  DIFFERS:" + why).c_str(), why.empty() ? "" : "  FAIL");
    if (!why.empty()) fails++;
  }
  }
  std::printf("busplan: %d cases, %d failed\n", total, fails);
  return fails ? 1 : 0;
}
