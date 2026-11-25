// N2 verify-n2 F1 / F10 / F3: the live engine against SHUTDOWN. Four control hammer threads (song publish, pad take + publish, play / stop / bpm / mix / loop / hold / fx plan / chain edit / meters) and
// status / stats readers reach the engine THROUGH tpa::Slot (engine_slot.h: the pattern every JNI entry point uses: a copy per call) while a render thread runs and the engine is torn down and
// made again (engine.reset: stop, two callbacks, take from the slot, destroy) and the stream is reopened on the same engine. Pass: no crash, no hang, calls on a dead engine answer -1000 / "{}".
// Adopted from the independent verifier's adversarial test (docs/port/reviews/verify-n2.md); run under TSan / ASan by hand (see N2-NOTES.md). Argument: seconds (default 6).
// Also checks the one state mapping (F3): the poller's STATE record values -> THUNDER_LIVE_STATE_*.
#include <atomic>
#include <shared_mutex>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "live_engine.h"
#include "pad_filler.h"
#include "engine_slot.h"
#include "pad_block.h"
#include "rt/json.h"
using namespace tpa;
static std::string readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static Slot<LiveEngine> gE;
static std::shared_ptr<LiveEngine> eng() { return gE.get(); }
struct Render {
  std::atomic<bool> stop{false}; std::thread th; std::shared_ptr<LiveEngine> e;
  void start(std::shared_ptr<LiveEngine> ee) { e = ee; stop = false; th = std::thread([this] { std::vector<float> o(2 * 480); int64_t wall = 1000000000LL; while (!stop.load()) { e->render(o.data(), 480, wall); wall += 10000000LL; std::this_thread::sleep_for(std::chrono::milliseconds(3)); } }); }
  void halt() { stop = true; if (th.joinable()) th.join(); }
};
int main(int argc, char** argv) {
  const int secs = argc > 1 ? atoi(argv[1]) : 6;
  if (stateOfRecord(THUNDER_LIVE_ST_PLAYING) != THUNDER_LIVE_STATE_PLAYING || stateOfRecord(THUNDER_LIVE_ST_STOPPED) != THUNDER_LIVE_STATE_IDLE || stateOfRecord(THUNDER_LIVE_ST_ENDED) != THUNDER_LIVE_STATE_ENDED) { printf("FAIL: state mapping\n"); return 1; }
  const std::string proj = readFile(std::string(THUNDER_ROOT) + "/sounds/ELECTRO.thunder");
  const std::string songT(extractMember(proj, "song"));
  auto mk = [&] { auto e = std::make_shared<LiveEngine>(); LiveEngine::Config c; c.sampleRate = 48000; c.maxBlock = 1024; std::string err; if (!e->create(c, &err)) { printf("create: %s\n", err.c_str()); exit(2); } e->startThreads(); return e; };
  gE.set(mk());
  Render R; R.start(eng());
  std::atomic<bool> done{false}; std::atomic<long> calls{0}, busy{0}, dead{0}; std::atomic<int> other_rc[16]; for (auto& a : other_rc) a = 0;
  auto hammer = [&](int seed) {
    std::mt19937 rng(seed);
    while (!done.load()) {
      auto e = eng(); if (!e) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }   // between take() and set(): no engine
      const int k = rng() % 16; int rc = 0;
      switch (k) {
        case 0: if (rng() % 8 == 0) rc = e->call([&](thunder_live* L) { char er[128]; return thunder_live_song_publish(L, (const uint8_t*)songT.data(), songT.size(), nullptr, 0, 1, er, sizeof er); }, 3000); break;
        case 1: case 2: { auto pad = (int)(rng() % 8); float* b = thunder_live_alloc(4800); if (!b) break; for (int i = 0; i < 4800; i++) b[i] = 0.3f * std::sin(i * 0.05f);
          // the block is owned by a holder the TASK shares: a task dropped at a timeout, or never run, frees it (verify-n2 F9); a taken block (pad_take == 0) belongs to the core
          auto blk = std::make_shared<PadBlock>(b);
          rc = e->call([=](thunder_live* L) { int x = thunder_live_pad_take(L, pad, blk->p, 0, 4800, 48000.0); if (x) return x; blk->owned = false; x = thunder_live_pad_kit(L, pad, 0, 0, 0.0); if (x) return x; return thunder_live_pads_publish(L); }, 2000); break; }
        case 3: rc = e->call([](thunder_live* L) { thunder_live_play_opts po; std::memset(&po, 0, sizeof po); po.size = sizeof po; po.t0 = THUNDER_LIVE_NOW; po.pdc_mode = 1; po.random_seed = 7; po.spp = -1; return thunder_live_play(L, &po); }, 2000); break;
        case 4: rc = e->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }, 2000); break;
        case 5: { const double bpm = 90 + (double)(rng() % 80); rc = e->call([=](thunder_live* L) { return thunder_live_bpm(L, bpm, THUNDER_LIVE_NOW); }, 2000); } break;
        case 6: { const int tr = (int)(rng() % 8); const double v = (double)(rng() % 255); rc = e->call([=](thunder_live* L) { return thunder_live_mix_set(L, tr, 0, v, -1.0, THUNDER_LIVE_NOW); }, 2000); } break;
        case 7: rc = e->call([](thunder_live* L) { const char* s = "[]"; return thunder_live_fx_plan(L, 0, (const uint8_t*)s, 2, 120.0); }, 2000); if (rc == 0) rc = e->call([](thunder_live* L) { return thunder_live_fx_commit(L, 0, THUNDER_LIVE_NOW); }, 2000); break;
        case 8: rc = e->call([](thunder_live* L) { const char* s = "[]"; return thunder_live_chain_edit(L, (const uint8_t*)s, 2, THUNDER_LIVE_NOW); }, 2000); break;
        case 9: { const double hz = (double)(rng() % 20); rc = e->call([=](thunder_live* L) { return thunder_live_meters_want(L, hz); }, 2000); } break;
        case 10: (void)e->statusJson(); (void)e->statsJson(); break;
        case 11: (void)e->statsJson(); (void)e->audioCalls(); (void)e->drainEvents(); break;
        case 12: (void)e->statusJson(); (void)e->engineOffset(); (void)e->chainsOn(); break;
        case 13: e->setHostStats((int)(rng() % 500), (int)(rng() % 3)); break;
        case 14: rc = e->call([](thunder_live* L) { return thunder_live_loop(L, 1, 0, 4, THUNDER_LIVE_NOW); }, 2000); break;
        case 15: rc = e->call([](thunder_live* L) { return thunder_live_hold(L, THUNDER_LIVE_NOW); }, 2000); break;
      }
      calls++; if (rc == -8) busy++; else if (rc == -1000) dead++; else if (rc) other_rc[k]++;
      std::this_thread::sleep_for(std::chrono::microseconds(200 + rng() % 2000));
    }
  };
  std::vector<std::thread> hs; for (int i = 0; i < 4; i++) hs.emplace_back(hammer, 100 + i);
  // reset / reopen cycles
  const auto t0 = std::chrono::steady_clock::now(); int resets = 0, reopens = 0; std::mt19937 rng(5);
  while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(secs)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(400 + rng() % 600));
    auto e = eng();
    if (rng() & 1) {   // stream reopen: stop, two callbacks, drop the stream, a new stream on the SAME engine
      e->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }, 500);
      const int64_t c0 = e->audioCalls(); for (int i = 0; i < 40 && e->audioCalls() < c0 + 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      R.halt(); R.start(e); reopens++;
    } else {           // engine.reset = teardownLocked then start
      e->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }, 500);
      const int64_t c0 = e->audioCalls(); for (int i = 0; i < 40 && e->audioCalls() < c0 + 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
      R.halt(); gE.take(); e->destroy(); R.e.reset();   // take(): no new reader; copies the hammers hold stay safe (call -> -1000, statsJson -> "{}")
      e.reset(); gE.set(mk()); R.start(eng()); resets++;
    }
  }
  done = true; for (auto& h : hs) h.join(); R.halt(); auto e = eng(); e->destroy();
  printf("resets %d reopens %d calls %ld busy %ld dead(-1000) %ld; other rc:", resets, reopens, calls.load(), busy.load(), dead.load());
  for (int i = 0; i < 16; i++) if (other_rc[i]) printf(" k%d:%d", i, other_rc[i].load());
  printf("\nOK\n"); return resets + reopens > 0 ? 0 : 1; }
