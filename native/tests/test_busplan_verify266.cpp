// VERIFY-266: bus_plan / mix_audible against answers worked out BY HAND from the web's busPlan() and audible() at branch beta (index.html 21032 ff, 21975 ff),
// not from the PR's notes. Each case says how the web's answer was derived. Patches are built from (Track In track, [Chain Out tracks it reaches], pre) lists in
// module order; every Track In gets one cable to each Chain Out it reaches; each Chain Out module is made once (after the Track Ins).
//   test_busplan_verify266          (no arguments)
#include <cstdio>
#include <string>
#include <vector>

#include "mix/mix.h"
#include "rt/json.h"
#include "song/song.h"

using namespace thunder;

struct TI { int t; std::vector<int> outs; int pre = 0; };
static std::string patchOf(const std::vector<TI>& v) {
  std::string m, c; std::vector<int> cos;
  for (size_t i = 0; i < v.size(); i++) {
    m += std::string(m.empty() ? "" : ",") + "{\"id\":\"ti" + std::to_string(i) + "\",\"type\":\"trackIn\",\"params\":{\"track\":" + std::to_string(v[i].t) + (v[i].pre ? ",\"pre\":1" : "") + "}}";
    for (int o : v[i].outs) { bool have = false; for (int x : cos) have |= x == o; if (!have) cos.push_back(o);
      c += std::string(c.empty() ? "" : ",") + "{\"from\":[\"ti" + std::to_string(i) + "\",\"out\"],\"to\":[\"co" + std::to_string(o) + "\",\"in\"]}"; }
  }
  for (int o : cos) m += ",{\"id\":\"co" + std::to_string(o) + "\",\"type\":\"chainOut\",\"params\":{\"track\":" + std::to_string(o) + "}}";
  return "{\"format\":\"chains.patch\",\"modules\":[" + m + "],\"cables\":[" + c + "]}";
}
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)
static BusPlan plan(const std::vector<TI>& v, uint32_t onMask) {
  JsonDoc* doc = new JsonDoc(); const std::string s = patchOf(v);
  if (!doc->parse(s)) { std::printf("bad patch\n"); g_fail++; }
  bool on[kSongNT]; for (int t = 0; t < kSongNT; t++) on[t] = onMask >> t & 1u;
  const BusPlan P = bus_plan(doc->root(), on); delete doc; return P;   // (BusPlan holds no Json)
}
static uint32_t bit(int t1) { return 1u << (t1 - 1); }   // 1-based track -> mask bit
static int popc(uint32_t v) { int n = 0; for (; v; v &= v - 1) n++; return n; }
static uint32_t loopCount(const BusPlan& P) { uint32_t n = 0; for (int t = 0; t < kSongNT; t++) for (int b = 0; b < kSongNT; b++) n += P.loopTo[t] >> b & 1u; return n; }

int main() {
  const uint32_t all = 0xffffffffu;
  // L3 forward: Track In 1 -> Chain Out 2, Track In 2 -> Chain Out 3, Track In 3 -> Chain Out 1 (a three-node loop).
  // Web: cand = (0,1),(1,2),(2,0) already sorted by (t,b). (0,1): reaches(1,0) no -> add. (1,2): reaches(2,1) no -> add. (2,0): reaches(0,2): 0->1->2 yes -> loopEdges [2,0].
  // fed = ti1, ti2 (pure: outs lack their own track); ti3 not fed. feeds {1:[0], 2:[1]}, targets {0:[1], 1:[2]}. srcAll {1:[0], 2:[0,1]}, busAll {0:[1,2], 1:[2]}.
  for (int rev = 0; rev < 2; rev++) {
    std::vector<TI> v = {{1, {2}}, {2, {3}}, {3, {1}}}; if (rev) v = {{3, {1}}, {2, {3}}, {1, {2}}};   // the same patch, modules made in the other order: the sort makes the answer the same
    const BusPlan P = plan(v, all);
    CHECK("L3: feeds 2<-1, 3<-2, nothing into 1", P.feedMask[1] == bit(1) && P.feedMask[2] == bit(2) && P.feedMask[0] == 0);
    CHECK("L3: targets 1->2, 2->3, 3 none", P.targets[0] == bit(2) && P.targets[1] == bit(3) && P.targets[2] == 0);
    CHECK("L3: pure = 1, 2; loop; the cable left out is 3 -> 1 (the highest source), and only it", P.pure == (bit(1) | bit(2)) && P.loop && P.loopTo[2] == bit(1) && loopCount(P) == 1);
    CHECK("L3: srcAll / busAll", P.srcAll[2] == (bit(1) | bit(2)) && P.srcAll[1] == bit(1) && P.srcAll[0] == 0 && P.busAll[0] == (bit(2) | bit(3)) && P.busAll[1] == bit(3) && P.busAll[2] == 0);
  }
  // L3 rotated: Track In 1 -> Chain Out 3, Track In 3 -> Chain Out 2, Track In 2 -> Chain Out 1. cand sorted: (0,2),(1,0),(2,1).
  // (0,2) add. (1,0): reaches(0,1)? 0->2 only: no -> add. (2,1): reaches(1,2): 1->0->2 yes -> left out [2,1]. So 2 -> 1 is added, 3 -> 2 is the one dropped.
  { const BusPlan P = plan({{1, {3}}, {3, {2}}, {2, {1}}}, all);
    CHECK("L3 rotated: feeds 3<-1, 1<-2; 2 gets nothing", P.feedMask[2] == bit(1) && P.feedMask[0] == bit(2) && P.feedMask[1] == 0);
    CHECK("L3 rotated: the dropped cable is 3 -> 2", P.loop && P.loopTo[2] == bit(2) && loopCount(P) == 1 && P.pure == (bit(1) | bit(2)));
    CHECK("L3 rotated: srcAll[3] = {1, 2}, busAll[2] = {1, 3}", P.srcAll[2] == (bit(1) | bit(2)) && P.busAll[1] == (bit(1) | bit(3))); }
  // Two-node loop, both module orders (4 <-> 7): cand (3,6),(6,3) -> (3,6) added, (6,3): reaches(3,6) yes -> dropped [6,3].
  for (int rev = 0; rev < 2; rev++) { const BusPlan P = plan(rev ? std::vector<TI>{{7, {4}}, {4, {7}}} : std::vector<TI>{{4, {7}}, {7, {4}}}, all);
    CHECK("L2: 4 feeds 7; 7 -> 4 dropped; 4 is pure, 7 is not fed", P.feedMask[6] == bit(4) && P.feedMask[3] == 0 && P.loopTo[6] == bit(4) && loopCount(P) == 1 && P.pure == bit(4)); }
  // A track that is a source AND a bus, with its own Track In reaching its own Chain Out as well (a send that is also a bus): Track In 1 -> Chain Out 2; Track In 2 -> Chain Out 2 and Chain Out 3.
  // ownRet = {1} (ti2 reaches co2). ti1: b=1 is in ownRet -> skipped (no cand). ti2: b=1 is t -> skip; b=2 -> cand (1,2). So 2 feeds 3 only; ti2 is fed but NOT pure (its outs hold 2).
  { const BusPlan P = plan({{1, {2}}, {2, {2, 3}}}, all);
    CHECK("send+bus: 3 is a bus fed by 2 only; track 1 feeds nothing (2's out is its own)", P.feedMask[2] == bit(2) && P.feedMask[1] == 0 && P.targets[0] == 0 && P.targets[1] == bit(3));
    CHECK("send+bus: 2 is not pure; no loop", P.pure == 0 && !P.loop); }
  // A bus whose own Track In does not reach its own Chain Out (the owner's middle bus): Track In 1 -> Chain Out 2; Track In 2 -> Chain Out 3. ownRet = {}: both edges kept; 2 is source and bus, pure.
  { const BusPlan P = plan({{1, {2}}, {2, {3}}}, all);
    CHECK("middle bus: 1 -> 2 -> 3, both pure", P.feedMask[1] == bit(1) && P.feedMask[2] == bit(2) && P.pure == (bit(1) | bit(2)) && !P.loop); }
  // chainOn off in the middle: the same chain with CHN off on track 2: chainsFeed(2) false -> (0,1) is not a candidate; ti2 is skipped for chainOn. No edges at all.
  { const BusPlan P = plan({{1, {2}}, {2, {3}}}, all & ~bit(2));
    CHECK("chainOn off on the middle track: nothing is fed", P.pure == 0 && !P.loop && P.feedMask[1] == 0 && P.feedMask[2] == 0 && P.targets[0] == 0 && P.targets[1] == 0); }
  // CHN off on the LAST bus: (1,2) is kept, (2,3) is not (chainsFeed(3) false).
  { const BusPlan P = plan({{1, {2}}, {2, {3}}}, all & ~bit(3));
    CHECK("chainOn off on the last bus: only 1 -> 2", P.feedMask[1] == bit(1) && P.feedMask[2] == 0 && P.pure == bit(1)); }
  // tracks >= 16 (the web's NT is 32): 21 -> 26 -> 31, then 31 -> 21 closes it. cand (20,25),(25,30),(30,20): the last is dropped.
  { const BusPlan P = plan({{21, {26}}, {26, {31}}, {31, {21}}}, all);
    CHECK("tracks >= 16: 26 <- 21, 31 <- 26, 31 -> 21 left out", P.feedMask[25] == bit(21) && P.feedMask[30] == bit(26) && P.feedMask[20] == 0 && P.loopTo[30] == bit(21) && loopCount(P) == 1);
    CHECK("tracks >= 16: closures", P.srcAll[30] == (bit(21) | bit(26)) && P.busAll[20] == (bit(26) | bit(31)));
    // audible (web: solo wins, then mute, then none soloed = heard, else busAll/srcAll soloed)
    CHECK("tracks >= 16: solo 31 keeps 21 and 26", mix_audible(P, 0, bit(31)) == (bit(21) | bit(26) | bit(31)));
    CHECK("tracks >= 16: solo 21 keeps 26 and 31", mix_audible(P, 0, bit(21)) == (bit(21) | bit(26) | bit(31)));
    CHECK("tracks >= 16: nothing soloed: everyone", mix_audible(P, 0, 0) == all); }
  // more than kMixFeedMax feeds: 17 tracks, each with TWO Track In modules into bus 31 -> feeds list of 34 (duplicates kept, sorted), the mixer wires the first 32 (busSync src.slice(0, 32)).
  { std::vector<TI> v; for (int t = 1; t <= 17; t++) { v.push_back({t, {31}}); v.push_back({t, {31}}); }
    const BusPlan P = plan(v, all);
    CHECK("34 feeds: feedLen 34, nFeeds 32, mask has 17 sources", P.feedLen[30] == 34 && P.nFeeds[30] == kMixFeedMax && popc(P.feedMask[30]) == 17);
    CHECK("34 feeds: the first 32 are the sorted list [0,0,1,1,...] (the last two, 16,16, are cut)", P.feeds[30][0] == 0 && P.feeds[30][1] == 0 && P.feeds[30][2] == 1 && P.feeds[30][31] == 15);
    CHECK("34 feeds: every one of the 17 sources is pure", P.pure == ((1u << 17) - 1)); }
  // pre: only for a FED Track In. Track In 1 pre -> Chain Out 2 (fed: pre); Track In 3 pre -> Chain Out 3 only (its own out: never fed: no pre).
  { const BusPlan P = plan({{1, {2}, 1}, {3, {3}, 1}}, all);
    CHECK("pre needs fed: 1 yes, 3 no", P.pre == bit(1) && P.pure == bit(1)); }
  // audible on a chain 1 -> 2 -> 3 -> 4 (web audible(): solo, mute, none; else any of busAll[t] / srcAll[t] soloed)
  { const BusPlan P = plan({{1, {2}}, {2, {3}}, {3, {4}}}, all); const uint32_t chain = bit(1) | bit(2) | bit(3) | bit(4);
    CHECK("chain: solo 4 keeps all", mix_audible(P, 0, bit(4)) == chain);
    CHECK("chain: solo 1 keeps all", mix_audible(P, 0, bit(1)) == chain);
    CHECK("chain: solo 2 keeps 1 (its source) and 3, 4 (buses downstream)", mix_audible(P, 0, bit(2)) == chain);
    CHECK("chain: solo 2, mute 3 -> 3 off; 4 is still heard (srcAll[4] holds the solo)", mix_audible(P, bit(3), bit(2)) == (bit(1) | bit(2) | bit(4)));
    CHECK("chain: solo 4, mute 2 -> 2 off, 1 and 3 on", mix_audible(P, bit(2), bit(4)) == (bit(1) | bit(3) | bit(4)));
    CHECK("chain: solo 2 and mute 2 -> solo wins", mix_audible(P, bit(2), bit(2)) == chain);
    CHECK("chain: an unrelated track 5 is not heard while something is soloed", (mix_audible(P, 0, bit(1)) & bit(5)) == 0 && (mix_audible(P, 0, 0) & bit(5)) != 0); }
  std::printf("%s (%d failures)\n", g_fail ? "FAILED" : "ok", g_fail);
  return g_fail ? 1 : 0;
}
