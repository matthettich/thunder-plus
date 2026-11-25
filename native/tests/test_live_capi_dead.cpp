// U37 P5 (ctest `live_capi_dead`): the DEAD handle of live.h (ERRORS, NULL AND DEAD HANDLES). An exception that is not an allocation failure, out of a CONTROL call, makes the handle dead: that call answers E_INTERNAL and from then on
// every call but destroy, the getters, poll and alloc / free answers E_STATE, process writes silence. An allocation failure (std::bad_alloc) is E_MEMORY and the handle lives. The wrapper's own source is compiled into this
// program (as render_api_oom does) to reach the LiveHost behind a handle and to make a call throw: the test seam of LiveHost::play runs inside play(), outside its own try blocks.
#include <new>
#include <stdexcept>

#include "../core/src/live/live_api.cpp"
#include "live_common.h"

using namespace lt;

static int g_thrown = 0;
static void throwRuntime(void*) { g_thrown++; throw std::runtime_error("not an allocation failure"); }
static void throwBadAlloc(void*) { g_thrown++; throw std::bad_alloc(); }

int main() {
  const std::string text = electro(); PadSet ps;
  thunder_live_opts o = opts(THUNDER_LIVE_F_METERS, 512); char err[128];
  thunder_live* L = thunder_live_create(&o, err, sizeof err); if (!L) { std::printf("create: %s\n", err); return 1; }
  CHECK(thunder_live_song_publish(L, (const uint8_t*)text.data(), text.size(), nullptr, 0, 1, err, sizeof err) == 0, "song");
  for (int p = 0; p < 8; p++) thunder_live_pad_set(L, p, ps.pad(p), nullptr, PadSet::NS, SR);
  CHECK(thunder_live_pads_publish(L) == 0, "pads");
  std::vector<float> a(512, 1.f), b(512, 1.f); int64_t fr = 0;
  CHECK(thunder_live_process(L, a.data(), b.data(), 512, fr, 0) == 0, "process before"); fr += 512;
  thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW);
  // a bad_alloc out of a control call: E_MEMORY, the handle lives
  L->h->setTestSeam(throwBadAlloc, nullptr);
  CHECK(thunder_live_play(L, &po) == THUNDER_LIVE_E_MEMORY && g_thrown == 1, "bad_alloc: E_MEMORY");
  CHECK(thunder_live_bpm(L, 100, -1) == 0 && thunder_live_collect(L) >= 0, "the handle still works after an E_MEMORY");
  CHECK(thunder_live_process(L, a.data(), b.data(), 512, fr, 0) == 0, "process still works"); fr += 512;
  // any other exception: E_INTERNAL, then dead
  L->h->setTestSeam(throwRuntime, nullptr);
  CHECK(thunder_live_play(L, &po) == THUNDER_LIVE_E_INTERNAL && g_thrown == 2, "another exception: E_INTERNAL");
  L->h->setTestSeam(nullptr, nullptr);
  CHECK(thunder_live_bpm(L, 100, -1) == THUNDER_LIVE_E_STATE && thunder_live_stop(L, -1) == THUNDER_LIVE_E_STATE && thunder_live_play(L, &po) == THUNDER_LIVE_E_STATE, "dead: transport calls are E_STATE");
  CHECK(thunder_live_song_publish(L, (const uint8_t*)text.data(), text.size(), nullptr, 0, 1, err, sizeof err) == THUNDER_LIVE_E_STATE, "dead: song_publish");
  CHECK(thunder_live_pad_set(L, 0, ps.pad(0), nullptr, PadSet::NS, SR) == THUNDER_LIVE_E_STATE && thunder_live_pads_publish(L) == THUNDER_LIVE_E_STATE && thunder_live_collect(L) == THUNDER_LIVE_E_STATE, "dead: pads, collect");
  CHECK(thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, 1, 0, -1) == THUNDER_LIVE_E_STATE && thunder_live_meters_want(L, 10) == THUNDER_LIVE_E_STATE && thunder_live_font_add(L, nullptr, 0) == THUNDER_LIVE_E_STATE, "dead: mixer, meters, fonts");
  a.assign(512, 1.f); b.assign(512, 1.f);
  CHECK(thunder_live_process(L, a.data(), b.data(), 512, fr, 0) == THUNDER_LIVE_E_STATE && a[0] == 0.f && a[511] == 0.f && b[300] == 0.f, "dead: process writes silence and says E_STATE");
  a.assign(1024, 1.f); CHECK(thunder_live_process(L, a.data(), b.data(), 1024, fr, 0) == THUNDER_LIVE_E_STATE && a[0] == 1.f, "dead: frames above max_block: E_STATE, the outputs are not touched");
  // the getters, poll, alloc / free and destroy work
  thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s;
  CHECK(thunder_live_stats_get(L, &s) == 0 && s.calls >= 2, "dead: stats_get works");
  unsigned char buf[600]; CHECK(thunder_live_poll(L, buf, sizeof buf) >= 0, "dead: poll works");
  CHECK(thunder_live_midi(L) == nullptr, "dead: midi() answers (no F_MIDI: NULL)"); thunder_live_host_stats(L, 1, 2);
  float* blk = thunder_live_alloc(100); CHECK(blk != nullptr, "dead: alloc works"); thunder_live_free(blk);
  thunder_live_destroy(L);
  // dump_step of a dead handle
  std::printf("live_capi_dead: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
