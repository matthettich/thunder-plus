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

#include <new>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#define ALLOC_AUDIT 1
// live = allocations minus frees (a leak check without a sanitizer, as test_chains_host); g_failAt: the n-th allocation after g_failN was zeroed fails (null: operator new throws bad_alloc)
static long g_live = 0, g_failAt = 0, g_failN = 0;
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (!p) g_live++; return __libc_realloc(p, n); }
void free(void* p) { if (p) g_live--; __libc_free(p); } }
#endif
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
    { std::string deep; for (int i = 0; i < 70; i++) deep += '['; deep += '1'; for (int i = 0; i < 70; i++) deep += ']';
      std::string t2 = "{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"a\",\"params\":{\"d\":" + deep + "}}]}"; JsonDoc x, y; CHECK("parse deep", x.parse(std::string_view(t2)) && y.parse(std::string_view(t2)));
      CHECK("nested past the limit: Structural for two parses", chains_diff(x.root(), y.root()).kind == ChainsDiffKind::Structural);
      CHECK("nested past the limit: Structural for the same handle too (no identity shortcut)", chains_diff(x.root(), x.root()).kind == ChainsDiffKind::Structural); }
    JsonDoc e; CHECK("parse2", e.parse(t));
    CHECK("twin documents: an object-valued param is deep-equal, no op (departure from the page)", chains_diff(d.root(), e.root()).kind == ChainsDiffKind::Same); }
  { // a Send / Receive `lab` change is Structural (control-thread diff; ChainsLive::apply drops a lab op); any other param change stays Params
    auto mk = [](const char* ty, const char* lab, const char* gain) { return std::string("{\"format\":\"chains.patch\",\"modules\":[{\"id\":\"s1\",\"type\":\"") + ty + "\",\"params\":{\"lab\":" + lab + ",\"gain\":" + gain + "}}],\"cables\":[]}"; };
    for (const char* ty : {"send", "recv"}) {
      JsonDoc a1, a2, a3; std::string s1 = mk(ty, "1", "1"), s2 = mk(ty, "2", "1"), s3 = mk(ty, "1", "0.5");
      CHECK("send/recv lab parse", a1.parse(s1) && a2.parse(s2) && a3.parse(s3));
      CHECK("send/recv: lab change only -> Structural", chains_diff(a1.root(), a2.root()).kind == ChainsDiffKind::Structural);
      CHECK("send/recv: gain change only -> Params", chains_diff(a1.root(), a3.root()).kind == ChainsDiffKind::Params);
    }
    JsonDoc b1, b2; std::string t1 = mk("gain", "1", "1"), t2 = mk("gain", "2", "1");
    CHECK("other module: lab change stays Params", b1.parse(t1) && b2.parse(t2) && chains_diff(b1.root(), b2.root()).kind == ChainsDiffKind::Params);
  }
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


#ifdef ALLOC_AUDIT
  // ---- bad_alloc sweep: the n-th allocation of chains_diff fails (operator new throws std::bad_alloc), for every n until the call succeeds; every failing call must give back everything it made ----
  { try { throw std::bad_alloc(); } catch (const std::bad_alloc&) {}   // the first exception of a process makes the unwinder's caches (never freed): before the count
    static const char* names[] = {"electro/param-one", "electro/param-two-modules-order", "electro/identical", "electro/connect", "electro/text-changed", "electro/prev-missing", "syn/dup-ids-order-by-first-position",
      "syn/param-order-index-keys-first", "syn/cable-key-nested-array-parts", "syn/cable-key-float-formats", "syn/params-string-unicode", "deep/identical-object-and-array-params", "deep/duplicate-keys-in-next-last-wins-raw",
      "syn/module-null-in-both-throws", "syn/cable-without-from-in-both-throws", "syn/raw-text-split-surrogate-at-200", "syn/many-modules-one-param", "syn/many-params-two-change", "syn/many-cables-one-fb"};
    const long liveStart = g_live; size_t swept = 0, failedCalls = 0; bool leak = false;
    for (const char* want : names) {
      Json c; for (size_t ci = 0; ci < cases.size(); ci++) if (cases.at(ci).str("name") == want) c = cases.at(ci);
      if (c.missing()) { std::printf("FAIL sweep case %s not in the golden\n", want); g_fail++; continue; }
      JsonDoc pd, nd; Json prev, next;
      if (c.get("prevText").isStr() || c.get("nextText").isStr()) { if (c.get("prevText").isStr()) { pd.parse(c.str("prevText")); prev = pd.root(); } if (c.get("nextText").isStr()) { nd.parse(c.str("nextText")); next = nd.root(); } }
      else { prev = c.get("prev"); next = c.get("next"); }
      swept++;
      for (long n = 1; n < 2000000 && !leak; n++) {
        const long live0 = g_live; bool threw = false; ChainsDiffKind k = ChainsDiffKind::Same;
        g_failN = 0; g_failAt = n;
        try { ChainsDiff d = chains_diff(prev, next); k = d.kind; } catch (const std::bad_alloc&) { threw = true; }
        g_failAt = 0;
        if (g_live != live0) { std::printf("FAIL %s: allocation %ld failing leaves %+ld block(s) behind\n", want, n, g_live - live0); g_fail++; leak = true; }
        if (!threw) { CHECK("the sweep's last call gives the page's kind", std::string(kindName(k)) == std::string(c.str("expectKind"))); break; }
        failedCalls++;
      }
    }
    CHECK("the sweep ran over every case and failed some calls", swept == sizeof names / sizeof names[0] && failedCalls > 100);
    std::printf("chains_diff: bad_alloc sweep: %zu pairs, %zu failing calls, live allocations left: %+ld\n", swept, failedCalls, g_live - liveStart);
  }
#else
  std::printf("chains_diff: the bad_alloc sweep is skipped (needs glibc without a sanitizer)\n");
#endif
  std::printf("chains_diff: %zu pairs (Same %zu, Params %zu, Structural %zu of which %zu throw in the page; %zu raw-text), %d failure(s)\n", cases.size(), nSame, nParams, nStruct, nThrow, nRaw, g_fail);
  return g_fail ? 1 : 0;
}
