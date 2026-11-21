// Shared by the U37 P2 live-host tests (test_live_*.cpp): the check macro, files, the ELECTRO song and its pads, a LiveHost made and driven the way a shell would.
#ifndef THUNDER_TESTS_LIVE_COMMON_H
#define THUNDER_TESTS_LIVE_COMMON_H
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "live/live_host.h"
#include "render/render.h"
#include "thunder/live.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf("" __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

namespace lt {
using namespace thunder;
constexpr double SR = 48000;

inline std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::ostringstream s; s << f.rdbuf(); return s.str(); }
inline std::string electro() { return readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder"); }

// deterministic made-up pads for ELECTRO (rt_harness's): PN pads of NS frames, kit entries as there
struct PadSet {
  static constexpr int PN = 64, NS = 24000;
  std::vector<float> d;
  PadSet() : d((size_t)PN * NS) {
    for (int p = 0; p < PN; p++) { float* x = &d[(size_t)p * NS]; for (int i = 0; i < NS; i++) x[i] = (float)std::sin(0.01 * (p % 7 + 1) * i) * (1.0f - (float)i / NS) * 0.3f; }
  }
  const float* pad(int p) const { return &d[(size_t)p * NS]; }
  static bool syn(int p) { return p >= 8; }
  static double rel(int p) { return p >= 8 ? 0.1 : 0; }
  static int choke(int p) { return p == 3 ? 1 : 0; }
};
// the same pads as a RenderPads for SongRenderer
inline void fillRenderPads(RenderPads& rp, const PadSet& ps) {
  for (int p = 0; p < PadSet::PN; p++) { rp.buf[p].data = ps.pad(p); rp.buf[p].len = PadSet::NS; rp.buf[p].sr = SR; rp.kit[p].syn = PadSet::syn(p); rp.kit[p].rel = PadSet::rel(p); rp.kit[p].choke = PadSet::choke(p); }
}

inline thunder_live_opts opts(uint32_t flags = 0, int maxBlock = 4096) { thunder_live_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.sample_rate = SR; o.max_block = maxBlock; o.flags = flags; return o; }
inline thunder_live_play_opts playOpts(int64_t t0) { thunder_live_play_opts p; std::memset(&p, 0, sizeof p); p.size = sizeof p; p.t0 = t0; p.spp = -1; return p; }

struct Host {
  LiveHost* h = nullptr; char err[256] = {0};
  explicit Host(const thunder_live_opts& o) { h = LiveHost::create(o, err, sizeof err); if (!h) std::printf("create failed: %s\n", err); }
  ~Host() { delete h; }
  LiveHost* operator->() { return h; }
  int song(const std::string& t, int64_t rev = 1, const std::string& vars = "") { char e[256]; return h->songPublish((const uint8_t*)t.data(), t.size(), vars.empty() ? nullptr : (const uint8_t*)vars.data(), vars.size(), rev, e, sizeof e); }
  int loadPads(const PadSet& ps, int n = PadSet::PN) {
    for (int p = 0; p < n; p++) { int rc = h->padSet(p, ps.pad(p), nullptr, PadSet::NS, SR); if (rc) return rc; rc = h->padKit(p, PadSet::choke(p), PadSet::syn(p) ? 1 : 0, PadSet::rel(p)); if (rc) return rc; }
    return h->padsPublish();
  }
  // process `n` frames in calls of `cs`, from `frame`; appends to l / r
  int run(std::vector<float>& l, std::vector<float>& r, int64_t n, int cs, int64_t& frame) {
    std::vector<float> a((size_t)cs), b((size_t)cs); int64_t done = 0;
    while (done < n) { const int c = (int)std::min<int64_t>(cs, n - done); const int rc = h->process(a.data(), b.data(), c, frame, 0); if (rc) return rc; l.insert(l.end(), a.begin(), a.begin() + c); r.insert(r.end(), b.begin(), b.begin() + c); done += c; frame += c; }
    return 0;
  }
  int run(int64_t n, int cs, int64_t& frame) { std::vector<float> l, r; return run(l, r, n, cs, frame); }
  // all records waiting
  std::vector<std::vector<uint8_t>> poll() {
    std::vector<std::vector<uint8_t>> out; uint8_t buf[1024];
    for (;;) { const int n = h->poll(buf, sizeof buf); if (n <= 0) break; int o = 0; while (o < n) { thunder_live_ev_head hd; std::memcpy(&hd, buf + o, sizeof hd); out.emplace_back(buf + o, buf + o + hd.bytes); o += hd.bytes; } }
    return out;
  }
};
inline int evType(const std::vector<uint8_t>& r) { thunder_live_ev_head h; std::memcpy(&h, r.data(), sizeof h); return h.type; }
inline uint32_t evSeq(const std::vector<uint8_t>& r) { thunder_live_ev_head h; std::memcpy(&h, r.data(), sizeof h); return h.seq; }
inline int64_t evF(const std::vector<uint8_t>& r) { thunder_live_ev_head h; std::memcpy(&h, r.data(), sizeof h); return h.f; }
template <class T> inline T evAs(const std::vector<uint8_t>& r) { T t; std::memcpy(&t, r.data(), sizeof t); return t; }
inline thunder_live_stats stats(LiveHost* h) { thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; h->statsGet(&s); return s; }
}  // namespace lt
#endif
