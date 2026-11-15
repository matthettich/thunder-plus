// U09: bus_plan / chains_feed / mix_audible (core/src/mix) against the JS's busPlan / chainsFeed / audible (core/seq-core.js and
// index.html, run by tools/mix-dump.mjs --busplan): hand-made patches (a bus, PRE sends, a send that keeps its own out, a track with
// its own Track In, odd ids and values, tracks out of range) and every song in sounds/. Exact.
//   test_busplan <dir>/busplan.json
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
  std::string js; if (!readFile(argv[1], &js)) { std::printf("busplan: cannot read %s\n", argv[1]); return 1; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("busplan: bad JSON\n"); return 1; }
  Json cases = doc.root(); int fails = 0;
  for (size_t i = 0; i < cases.size(); i++) {
    Json C = cases.at(i), W = C.get("want"); const std::string id(C.str("id"));
    bool chainOn[kSongNT] = {false}; uint32_t mute = 0, solo = 0; Json patch; Song song; std::string text;
    if (!C.get("file").missing()) {
      if (!readFile(std::string(THUNDER_ROOT) + "/" + std::string(C.str("file")), &text) || !song.load(text.data(), text.size(), nullptr)) { std::printf("%-28s cannot load the song  FAIL\n", id.c_str()); fails++; continue; }
      patch = song.chainPatch; for (int t = 0; t < kSongNT; t++) { chainOn[t] = C.boolean("allOn") || song.chainOn[t]; if (song.mix[t].mute) mute |= 1u << t; if (song.mix[t].solo) solo |= 1u << t; }
    } else { Json S = C.get("S"); patch = S.get("chainPatch"); for (int t = 0; t < kSongNT; t++) { chainOn[t] = S.arr("chainOn").at((size_t)t).asBool(); Json m = S.arr("mix").at((size_t)t); if (m.boolean("mute")) mute |= 1u << t; if (m.boolean("solo")) solo |= 1u << t; } }
    const BusPlan P = bus_plan(patch, chainOn);
    std::string why;
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
    if ((double)chains_feed(patch, chainOn) != W.num("chainsFeed")) why += " chainsFeed";
    if ((double)mix_audible(P, mute, solo) != W.num("audible")) why += " audible";
    int buses = 0, srcs = 0; for (int t = 0; t < kSongNT; t++) { if (P.feedLen[t]) buses++; if (P.targets[t]) srcs++; }
    std::printf("%-28s %d bus(es), %d source(s)%s%s\n", id.c_str(), buses, srcs, why.empty() ? "  ok" : ("  DIFFERS:" + why).c_str(), why.empty() ? "" : "  FAIL");
    if (!why.empty()) fails++;
  }
  std::printf("busplan: %zu cases, %d failed\n", cases.size(), fails);
  return fails ? 1 : 0;
}
