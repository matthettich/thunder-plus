// U34a test helpers: songs, made-up pads, and the DIRECT drive of SongRenderer that the C API is compared against.
#pragma once
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "render/render.h"
#include "song/song.h"
#include "thunder/render.h"

namespace u34 {
using namespace thunder;

inline std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::ostringstream o; o << f.rdbuf(); return o.str(); }
inline std::string row(int note) { char b[128]; std::snprintf(b, sizeof b, "[%d,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]", note); return b; }
// one phrase of 32 rows, every row a note: about 4 s at 120 BPM
inline std::string shortSong(int bpm = 120) {
  std::string s = "{\"v\":6,\"bpm\":" + std::to_string(bpm) + ",\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[32],\"phrases\":[[";
  for (int i = 0; i < 32; i++) s += (i ? "," : "") + row(60 + i % 5);
  return s + "]]}";
}
constexpr int kNS = 24000;
struct PadSet {
  std::vector<float> data;   // pad p: kNS samples
  int n = 64;
  PadSet() : data((size_t)64 * kNS) {
    for (int p = 0; p < 64; p++) for (int i = 0; i < kNS; i++) data[(size_t)p * kNS + i] = (float)std::sin(0.01 * (p % 7 + 1) * i) * (1.0f - (float)i / kNS) * 0.3f;
  }
  const float* pad(int p) const { return &data[(size_t)p * kNS]; }
  static bool syn(int p) { return p >= 8; }
  static double rel(int p) { return p >= 8 ? 0.1 : 0; }
  static int choke(int p) { return p == 3 ? 1 : 0; }
};
inline bool installPads(thunder_render* r, const PadSet& ps, int* nerr = nullptr) {
  bool ok = true;
  for (int p = 0; p < ps.n; p++) {
    // odd pads stereo (the right channel is the same samples reversed in the sense of a shift), even pads mono
    int rc = thunder_render_set_pad(r, p, ps.pad(p), (p & 1) ? ps.pad((p + 1) % ps.n) : nullptr, kNS, 48000);
    int rk = thunder_render_set_pad_kit(r, p, PadSet::choke(p), PadSet::syn(p), PadSet::rel(p));
    if (rc || rk) { ok = false; if (nerr) (*nerr)++; }
  }
  return ok;
}

struct Cfg { bool fx = false, master = false; int pdc = 0; bool pageTiming = false; };

// The direct drive: SongRenderer used as thunder-render did before the API, with the same setup the API does. If useFrames, N frames after the
// trim in one block call; else quanta until the song is over and the pool is empty (cap in raw frames).
inline std::vector<float> direct(const std::string& songJson, const PadSet& ps, const Cfg& c, bool useFrames, int64_t N, std::vector<float>* outR, int64_t cap = 48000LL * 600) {
  Song song; JsonError je; if (!song.load(songJson.data(), songJson.size(), &je)) { std::printf("direct: song does not load\n"); std::exit(2); }
  SeqVars vars; static RenderPads pads; pads = RenderPads();
  for (int p = 0; p < ps.n; p++) {
    pads.buf[p].data = ps.pad(p); pads.buf[p].data2 = (p & 1) ? ps.pad((p + 1) % ps.n) : nullptr; pads.buf[p].len = kNS; pads.buf[p].sr = 48000;
    pads.kit[p].choke = PadSet::choke(p); pads.kit[p].syn = PadSet::syn(p); pads.kit[p].rel = PadSet::rel(p);
  }
  SeqOpts o; o.row = song.renderFrom; o.once = true;
  SongRenderer R(song, vars, pads, 48000, 128, o);
  R.pool().setPageTiming(c.pageTiming);
  if (c.fx) R.setEffects(true);
  if (c.master) R.setMasterChain(master_chain_from_song(song.mst, song.limiter));
  if (c.pdc) { const PdcMode m = c.pdc == 2 ? PdcMode::On : PdcMode::Auto; std::vector<PdcSlot> sl(1024); int ns = R.fxBank() ? R.fxBank()->pdcSlots(sl.data(), 1024) : 0;
    R.setPlan(pdc_plan(bus_plan(song.chainPatch, song.chainOn), sl.data(), ns, 48000, m), m, 0); }
  const int64_t skip = R.latencyFrames();
  std::vector<float> L, Rt;
  if (useFrames) { std::vector<float> a((size_t)(skip + N)), b((size_t)(skip + N)); R.block(a.data(), b.data(), (int)(skip + N)); L.assign(a.begin() + skip, a.end()); Rt.assign(b.begin() + skip, b.end()); }
  else {
    float a[128], b[128]; std::vector<float> A, B;
    while (R.frame() < cap) { R.block(a, b, 128); A.insert(A.end(), a, a + 128); B.insert(B.end(), b, b + 128); if (R.songOver() && R.voices() == 0) break; }
    const size_t s = (size_t)std::min<int64_t>(skip, (int64_t)A.size()); L.assign(A.begin() + s, A.end()); Rt.assign(B.begin() + s, B.end());
  }
  *outR = Rt; return L;
}

inline thunder_render* makeApi(const std::string& songJson, const PadSet& ps, const Cfg& c, bool useFrames, int64_t N, char* err = nullptr, size_t ecap = 0) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o;
  o.effects = c.fx; o.master_chain = c.master; o.pdc_mode = c.pdc; o.page_timing = c.pageTiming; o.use_frames = useFrames; o.frames = N;
  thunder_render* r = thunder_render_create((const uint8_t*)songJson.data(), songJson.size(), &o, err, ecap);
  if (r && !installPads(r, ps)) { thunder_render_free(r); return nullptr; }
  return r;
}
}  // namespace u34
