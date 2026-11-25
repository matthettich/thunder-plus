// C2a: ctest `chains_diff`. Every pair of golden/chains_diff_cases.json (made by tools/chains-diff-dump.mjs from the page's own chnDiff) must be classified as the page's ops say (Same / Params / Structural;
// a throw is Structural) with the same param list in the same order; then bus_plan_equal field by field, the same-document case, and a mutation sweep over hostile shapes (never a crash; run it under the sanitizers).
// usage: test_chains_diff <chains_diff_cases.json>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "chains/cdiff.h"
#include "mix/mix.h"
#include "rt/json.h"

using namespace thunder;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)

static const char* kindName(ChainsDiffKind k) { return k == ChainsDiffKind::Same ? "Same" : k == ChainsDiffKind::Params ? "Params" : "Structural"; }
static bool readFile(const char* path, std::string& out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }

// ---- a JSON writer that replaces the Nth node (pre-order) by a literal: the hostile-shape sweep ----
static void write_with(Json v, int& counter, int target, const char* repl, std::string& out) {
  const int me = counter++;
  const bool hit = me == target;
  std::string tmp;
  std::string& o = hit ? tmp : out;
  if (v.isArr()) {
    o += '[';
    for (size_t i = 0; i < v.size(); i++) { if (i) o += ','; write_with(v.at(i), counter, target, repl, o); }
    o += ']';
  } else if (v.isObj()) {
    o += '{';
    for (size_t i = 0; i < v.size(); i++) {
      if (i) o += ',';
      std::string ks = "\""; ks += std::string(v.keyAt(i)); ks += "\"";   // keys here are plain ASCII
      o += ks; o += ':'; write_with(v.valAt(i), counter, target, repl, o);
    }
    o += '}';
  } else json_write(v, o);
  if (hit) out += repl;
}
static int count_nodes(Json v) { int n = 1; if (v.isArr()) for (size_t i = 0; i < v.size(); i++) n += count_nodes(v.at(i)); else if (v.isObj()) for (size_t i = 0; i < v.size(); i++) n += count_nodes(v.valAt(i)); return n; }

static BusPlan electroPlan(const char* patchText, JsonDoc& d) {
  if (!d.parse(patchText, std::strlen(patchText))) { std::printf("FAIL patch text\n"); g_fail++; }
  bool on[kSongNT] = {}; for (int t = 0; t < kSongNT; t++) on[t] = true;
  return bus_plan(d.root(), on);
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: test_chains_diff <chains_diff_cases.json>\n"); return 2; }
  std::string text;
  if (!readFile(argv[1], text)) { std::printf("cannot read %s\n", argv[1]); return 2; }
  JsonDoc doc; JsonError err;
  if (!doc.parse(text, &err)) { std::printf("golden does not parse at %zu\n", err.offset); return 2; }
  Json cases = doc.root().arr("cases");
  CHECK("at least 60 pairs", cases.size() >= 60);
  size_t nSame = 0, nParams = 0, nStruct = 0, nThrow = 0, nRaw = 0;
  for (size_t ci = 0; ci < cases.size(); ci++) {
    Json c = cases.at(ci); const std::string name(c.str("name"));
    JsonDoc pd, nd; Json prev, next;
    if (c.get("prevText").isStr() || c.get("nextText").isStr()) {
      nRaw++;
      if (c.get("prevText").isStr()) { std::string_view t = c.str("prevText"); if (!pd.parse(t)) { std::printf("FAIL %s: prevText does not parse\n", name.c_str()); g_fail++; continue; } prev = pd.root(); }
      if (c.get("nextText").isStr()) { std::string_view t = c.str("nextText"); if (!nd.parse(t)) { std::printf("FAIL %s: nextText does not parse\n", name.c_str()); g_fail++; continue; } next = nd.root(); }
    } else { prev = c.get("prev"); next = c.get("next"); }
    const ChainsDiff d = chains_diff(prev, next);
    const std::string want(c.str("expectKind"));
    if (want != kindName(d.kind)) { std::printf("FAIL %s: kind %s, the page says %s%s\n", name.c_str(), kindName(d.kind), want.c_str(), c.get("throws").isStr() ? " (it throws)" : ""); g_fail++; continue; }
    Json ep = c.arr("expectParams");
    if (d.params.size() != ep.size()) { std::printf("FAIL %s: %zu params, the page says %zu\n", name.c_str(), d.params.size(), ep.size()); g_fail++; continue; }
    for (size_t i = 0; i < ep.size(); i++) {
      Json e = ep.at(i);
      if (d.params[i].id != e.str("id") || d.params[i].name != e.str("name") || !(d.params[i].v == e.num("v", -12345.678))) { std::printf("FAIL %s: param %zu is %s.%s=%.17g, the page says %s.%s=%.17g\n", name.c_str(), i, d.params[i].id.c_str(), d.params[i].name.c_str(), d.params[i].v, std::string(e.str("id")).c_str(), std::string(e.str("name")).c_str(), e.num("v")); g_fail++; break; }
    }
    if (want == "Same") nSame++; else if (want == "Params") nParams++; else nStruct++;
    if (c.get("throws").isStr()) nThrow++;
    if (d.kind != ChainsDiffKind::Params && !d.params.empty()) { std::printf("FAIL %s: params listed for a %s\n", name.c_str(), kindName(d.kind)); g_fail++; }
  }
  CHECK("every kind is exercised", nSame >= 10 && nParams >= 10 && nStruct >= 10 && nThrow >= 5 && nRaw >= 5);

  // ---- the same document on both sides: JS identity makes every object equal to itself ----
  { std::string_view t = "{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"a\",\"params\":{\"x\":1,\"o\":{}}}],\"cables\":[{\"from\":[\"a\",\"o\"],\"to\":[\"a\",\"i\"]}]}"; JsonDoc d; CHECK("parse", d.parse(t));
    CHECK("same handle: Same", chains_diff(d.root(), d.root()).kind == ChainsDiffKind::Same);
    JsonDoc e; CHECK("parse2", e.parse(t));
    CHECK("twin documents: an object-valued param differs (!== is identity)", chains_diff(d.root(), e.root()).kind == ChainsDiffKind::Structural); }
  CHECK("missing, missing", chains_diff(Json(), Json()).kind == ChainsDiffKind::Same);

  // ---- bus_plan_equal ----
  { static const char* P1 = "{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"i2\",\"type\":\"trackIn\",\"params\":{\"track\":2,\"pre\":1}},{\"id\":\"o\",\"type\":\"chainOut\",\"params\":{\"track\":4}}],\"cables\":[{\"from\":[\"i1\",\"out\"],\"to\":[\"o\",\"in\"]},{\"from\":[\"i2\",\"out\"],\"to\":[\"o\",\"in\"]}]}";
    static const char* P2 = "{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"o\",\"type\":\"chainOut\",\"params\":{\"track\":4}}],\"cables\":[{\"from\":[\"i1\",\"out\"],\"to\":[\"o\",\"in\"]}]}";
    JsonDoc d1, d1b, d2; const BusPlan a = electroPlan(P1, d1), b = electroPlan(P1, d1b), c2 = electroPlan(P2, d2);
    CHECK("plan has a bus", a.feedLen[3] == 2 && a.pre != 0 && a.pure != 0);
    CHECK("default plans equal", bus_plan_equal(BusPlan(), BusPlan()));
    CHECK("same patch twice equal", bus_plan_equal(a, b) && bus_plan_equal(a, a));
    CHECK("different patch differs", !bus_plan_equal(a, c2) && !bus_plan_equal(c2, a));
    bool on[kSongNT] = {}; JsonDoc d3; d3.parse(std::string_view(P1));
    CHECK("chain switches off differs", !bus_plan_equal(a, bus_plan(d3.root(), on)));
#define DIFF1(field, expr) do { BusPlan m = a; { auto& F = m.field; expr; } CHECK("field " #field " differs", !bus_plan_equal(a, m) && !bus_plan_equal(m, a)); } while (0)
    DIFF1(feeds[3][0], F ^= 1);
    DIFF1(feeds[3][1], F ^= 1);
    DIFF1(nFeeds[3], F += 1);
    DIFF1(feedLen[3], F += 1);
    DIFF1(feedMask[3], F ^= 1u << 7);
    DIFF1(targets[0], F ^= 1u << 9);
    DIFF1(loopTo[2], F ^= 1u << 3);
    DIFF1(srcAll[3], F ^= 1u << 12);
    DIFF1(busAll[1], F ^= 1u << 5);
    DIFF1(pure, F ^= 1u << 0);
    DIFF1(pre, F ^= 1u << 1);
    DIFF1(loop, F = !F);
#undef DIFF1
    { BusPlan m = a; m.feeds[kSongNT - 1][0] = 0; CHECK("equal after a no-op write", bus_plan_equal(a, m)); }
    { BusPlan m = a; m.feeds[3][5] = 99; CHECK("a feeds entry beyond nFeeds is not part of the plan", bus_plan_equal(a, m)); }
    { BusPlan m = a; m.feeds[kSongNT - 1][kMixFeedMax - 1] = 7; CHECK("last slot of an empty list is not part of the plan", bus_plan_equal(a, m)); }
    for (int t = 0; t < kSongNT; t++) { BusPlan m = a; m.feedMask[t] ^= 1u << 20; CHECK("feedMask of every track compared", !bus_plan_equal(a, m)); BusPlan q = a; q.loopTo[t] ^= 1u << 20; CHECK("loopTo of every track compared", !bus_plan_equal(a, q)); BusPlan s = a; s.busAll[t] ^= 1; CHECK("busAll of every track compared", !bus_plan_equal(a, s)); }
  }

  // ---- hostile-shape sweep: replace every node of two real patches, on either side, by a hostile literal; nothing may crash or hang ----
  { static const char* lits[] = {"null", "5", "\"s\"", "[]", "{}", "true", "[[]]", "{\"0\":1}", "1e999", "[null,null]", "\"\"", "0", "[1,\"a\"]", "{\"id\":null}"};
    const char* base = "{\"format\":\"chains.patch\",\"version\":1,\"modules\":[{\"id\":\"tin1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0},\"text\":\"a*b\"},{\"id\":\"m\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"l1\":0.8}},{\"id\":\"o\",\"type\":\"chainOut\",\"params\":{\"track\":4,\"level\":1}}],\"cables\":[{\"from\":[\"tin1\",\"out\"],\"to\":[\"m\",\"in1\"],\"fb\":true},{\"from\":[\"m\",\"out\"],\"to\":[\"o\",\"in\"]}],\"st2\":1,\"ti2\":1}";
    JsonDoc b; CHECK("sweep base parses", b.parse(std::string_view(base)));
    const int n = count_nodes(b.root()); size_t runs = 0, structural = 0;
    for (int target = 0; target < n; target++) for (const char* lit : lits) {
      int counter = 0; std::string mutated; write_with(b.root(), counter, target, lit, mutated);
      JsonDoc m; if (!m.parse(std::string_view(mutated))) continue;
      ChainsDiff x = chains_diff(b.root(), m.root()), y = chains_diff(m.root(), b.root()), z = chains_diff(m.root(), m.root());
      runs += 3; structural += (x.kind == ChainsDiffKind::Structural) + (y.kind == ChainsDiffKind::Structural);
      if (x.kind != ChainsDiffKind::Params && !x.params.empty()) { std::printf("FAIL sweep: params without Params\n"); g_fail++; }
      (void)z;
    }
    CHECK("sweep ran", runs > 500 && structural > 100);
  }
  // wrong types at the top, and deep nesting (the parser limits depth; a deep array id / from must not recurse without bound)
  { std::string deep; for (int i = 0; i < kJsonMaxDepth - 4; i++) deep += '['; for (int i = 0; i < kJsonMaxDepth - 4; i++) deep += ']';
    std::string t = "{\"format\":\"chains.patch\",\"modules\":[{\"id\":" + deep + "}],\"cables\":[{\"from\":" + deep + ",\"to\":[\"a\",\"b\"]}]}";
    JsonDoc d; if (d.parse(std::string_view(t))) { ChainsDiff x = chains_diff(d.root(), d.root()); CHECK("deep nesting classified", x.kind == ChainsDiffKind::Same || x.kind == ChainsDiffKind::Structural); } }

  std::printf("chains_diff: %zu pairs (Same %zu, Params %zu, Structural %zu of which %zu throw in the page; %zu raw-text), %d failure(s)\n", cases.size(), nSame, nParams, nStruct, nThrow, nRaw, g_fail);
  return g_fail ? 1 : 0;
}
