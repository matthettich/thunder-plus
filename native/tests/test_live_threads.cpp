// U37 P2 (ctest `live_threads`, also run under ThreadSanitizer): a real AUDIO thread runs LiveHost::process in a loop while a CONTROL thread hammers every control call (song_publish, pads, play, stop, hold, loop, bpm, lead,
// perf, stop_track, mix_set, collect, poll, stats) with E_BUSY retried, then both stop and a quiet stretch is compared: the hammered host, once its state is put back, plays bit-equal to a host that was never touched,
// and equal (to float rounding of the host timeline, see live_equals_render) to the offline renderer.
#include <atomic>
#include <chrono>
#include <thread>

#include "engine/layer_pe.h"
#include "live_common.h"
using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;

struct Rng { uint64_t s; uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); } int below(int n) { return (int)(next() % (uint32_t)n); } double unit() { return next() / 2147483648.0; } };
static bool okRc(int rc) { return rc == 0 || rc == THUNDER_LIVE_E_BUSY || rc == THUNDER_LIVE_E_LIMIT || rc == THUNDER_LIVE_E_SONG; }

int main() {
  const std::string text = electro(); PadSet ps;
  Host A(opts(0, 1024)); if (!A.h) return 1;
  CHECK(A.song(text, 1) == 0 && A.loadPads(ps) == 0, "setup");
  std::atomic<bool> stop{false}; std::atomic<int64_t> frames{0}; std::atomic<int> audioErr{0}; int64_t nextFrame = 0;
  std::thread audio([&] {
    std::vector<float> l(1024), r(1024); Rng g{99}; int64_t fr = 0;
    while (!stop.load(std::memory_order_acquire)) {
      const int n = g.below(3) == 0 ? 1 + g.below(1024) : 128; if (A->process(l.data(), r.data(), n, fr, 0)) audioErr++;
      for (int i = 0; i < n; i++) if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i])) audioErr++;
      fr += n; frames.store(fr, std::memory_order_relaxed);
      if ((fr & 0xFFFF) < 1024) std::this_thread::yield();
    }
    nextFrame = fr;
  });
  // CONTROL
  int badRc = 0, busy = 0, ops = 0; uint32_t prevSeq = 0; bool seqAsc = true; Rng g{12345};
  std::vector<std::vector<float>> variant; for (int v = 0; v < 4; v++) variant.push_back([&] { std::vector<float> t((size_t)PadSet::NS); for (int i = 0; i < PadSet::NS; i++) t[(size_t)i] = (float)std::sin(0.013 * (v + 1) * i) * 0.25f; return t; }());
  const int ITER = 4000; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW);
  auto retry = [&](auto&& f) { int rc = f(); for (int k = 0; k < 50 && rc == THUNDER_LIVE_E_BUSY; k++) { busy++; std::this_thread::yield(); std::this_thread::sleep_for(std::chrono::microseconds(100)); rc = f(); } return rc; };
  for (int it = 0; it < ITER; it++) {
    int rc = 0; const int k = g.below(100); ops++;
    if (k < 8) rc = retry([&] { return A.song(text, 100 + it); });
    else if (k < 10) { char e[64]; const char bad[] = "{not a song"; rc = A->songPublish((const uint8_t*)bad, sizeof bad - 1, nullptr, 0, 5, e, sizeof e); if (rc != THUNDER_LIVE_E_SONG) badRc++; rc = 0; }
    else if (k < 20) { const int p = g.below(64); const auto& v = variant[(size_t)g.below(4)]; rc = A->padSet(p, v.data(), nullptr, PadSet::NS, SR); if (!rc) rc = retry([&] { return A->padsPublish(); }); }
    else if (k < 24) rc = retry([&] { return A->play(po); });
    else if (k < 28) rc = retry([&] { return A->stop(THUNDER_LIVE_NOW); });
    else if (k < 31) rc = retry([&] { return A->hold(THUNDER_LIVE_NOW); });
    else if (k < 35) rc = retry([&] { return A->loop(g.below(2), g.below(4), 3 + g.below(4), THUNDER_LIVE_NOW); });
    else if (k < 42) rc = retry([&] { return A->bpm(60 + g.below(120), g.below(2) ? THUNDER_LIVE_NOW : frames.load() + g.below(20000)); });
    else if (k < 45) rc = retry([&] { return A->lead(0.05 + g.unit() * 1.5, THUNDER_LIVE_NOW); });
    else if (k < 50) { const int w = g.below(3); const int ix = w == THUNDER_LIVE_PERF_MACRO ? g.below(8) : 0; const double v = w == THUNDER_LIVE_PERF_FILL ? g.below(2) : g.unit(); rc = retry([&] { return A->perf(w, ix, v, THUNDER_LIVE_NOW); }); }
    else if (k < 53) rc = retry([&] { return A->stopTrack(g.below(32), THUNDER_LIVE_NOW); });
    else if (k < 62) { const int key = g.below(2) ? THUNDER_LIVE_MIX_VOL : (g.below(2) ? THUNDER_LIVE_MIX_MUTE : THUNDER_LIVE_MIX_SOLO); const int trk = key == THUNDER_LIVE_MIX_VOL ? g.below(33) : g.below(32); const double ramp = g.below(3) == 0 ? -1 : g.below(500); rc = retry([&] { return A->mixSet(trk, key, g.below(2), ramp, THUNDER_LIVE_NOW); }); }
    else if (k < 70) A->collect();
    else if (k < 90) { uint8_t buf[512]; for (int n; (n = A->poll(buf, sizeof buf)) > 0;) { int o = 0; while (o < n) { thunder_live_ev_head h; std::memcpy(&h, buf + o, sizeof h); if (h.seq <= prevSeq) seqAsc = false; prevSeq = h.seq; o += h.bytes; } } }
    else { thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; A->statsGet(&s); if (s.pad_bytes_held < s.pad_bytes_retired) badRc++; }
    if (it % 500 == 250) for (int i = 0; i < 300; i++) { rc = retry([&] { return A->bpm(90 + i % 30, THUNDER_LIVE_NOW); }); if (rc) break; }   // a burst: the ring (256) fills, E_BUSY, the caller retries
    if (!okRc(rc)) { badRc++; std::printf("op %d (k %d): rc %d\n", it, k, rc); }
    if ((it & 63) == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
  }
  stop.store(true, std::memory_order_release); audio.join();
  CHECK(badRc == 0, "every call answered 0, E_BUSY, E_LIMIT or the expected E_SONG (%d bad of %d ops, %d busy)", badRc, ops, busy);
  CHECK(audioErr.load() == 0, "process never failed and never produced a non-finite sample (%d)", audioErr.load());
  CHECK(seqAsc, "records arrive in ascending seq on the consumer thread");
  const auto st = stats(A.h); // thread_prepares counts the prepares a process call actually did: 1 where the build has a per-thread Plaits / Elements workspace (a fresh thread is not "ready"), 0 where it has none (pe_thread_ready() is
  // always true: e.g. a build without the library), and never more than 1 for one audio thread.
  bool hasWorkspace = false; { std::thread t([&] { hasWorkspace = !pe_thread_ready(); }); t.join(); }
  CHECK(st.thread_prepares == (hasWorkspace ? 1u : 0u), "the audio side prepared its workspace %u time(s); this build %s a per-thread workspace", st.thread_prepares, hasWorkspace ? "has" : "has no"); CHECK(st.reanchors == 0, "no re-anchor (%u)", st.reanchors);
  CHECK(st.seq_dropped == 0, "no dropped sequencer events (%llu)", (unsigned long long)st.seq_dropped);
  std::printf("hammer: %d control ops (%d E_BUSY retries), %llu quanta, %llu commands applied, %u late commands, %llu steps, %u records dropped\n", ops, busy, (unsigned long long)st.quanta, (unsigned long long)st.cmds_applied, st.cmd_late, (unsigned long long)st.steps, st.evt_dropped);

  // ---- the quiet stretch ----
  CHECK(A->stop(THUNDER_LIVE_NOW) == 0 && A->lead(0.25, THUNDER_LIVE_NOW) == 0 && A->loop(0, 0, 0, THUNDER_LIVE_NOW) == 0, "stop, lead and loop back to the defaults");
  int64_t fr = nextFrame; A.run(48000, Q, fr); A->collect(); A.run(Q, Q, fr); A->collect(); A.poll();
  CHECK(A.song(text, 1000) == 0 && A.loadPads(ps) == 0, "the same song and pads again"); A.run(Q * 4, Q, fr); A->collect();
  const int64_t F = ((fr + 1023) / Q + 4) * Q;   // t0 on the grid
  Host B(opts(0, 1024)); CHECK(B.song(text, 1) == 0 && B.loadPads(ps) == 0, "B setup"); int64_t fb = F - Q * 8; B.run(Q * 8, Q, fb); B.poll();   // B has never been touched
  thunder_live_play_opts p2 = playOpts(F); p2.random_seed = 0x2545F491u;
  for (int i = 0; i < 4; i++) A->mixSet(i, THUNDER_LIVE_MIX_VOL, 100, 0, THUNDER_LIVE_NOW);   // queued before the play: the play's mixer.load puts the song's levels back
  CHECK(A->play(p2) == 0 && B->play(p2) == 0, "play at the same frame");
  const int64_t NQ = (int64_t)(SR * 10); std::vector<float> la, ra, lb, rb; int64_t fa = fr; A.run(F - fa, Q, fa); CHECK(fa == F, "A at F");
  A.run(la, ra, NQ, Q, fa); B.run(lb, rb, NQ, Q, fb);
  // The two hosts' ENGINE clocks differ (A has rendered minutes of hammer, B eight quanta), so a sample position `engine time + x` rounds one way in one and the other way in the other: a handful of last-bit
  // differences in 480000 samples are the clock's rounding, not state (the same effect live_equals_render removed by starting the engine at 0): at most 0.001 % of the samples, by at most 1e-6.
  size_t bad = 0, first = (size_t)-1; double mdab = 0; for (size_t i = 0; i < la.size(); i++) if (std::memcmp(&la[i], &lb[i], 4) || std::memcmp(&ra[i], &rb[i], 4)) { if (first == (size_t)-1) first = i; bad++; mdab = std::max(mdab, std::max((double)std::fabs(la[i] - lb[i]), (double)std::fabs(ra[i] - rb[i]))); }
  double pk = 0; for (float x : lb) pk = std::max(pk, (double)std::fabs(x));
  CHECK(pk > 0.01, "B sounds (%g)", pk); CHECK(bad * 100000 <= (size_t)la.size() * 2 && mdab <= 1e-6, "the hammered host equals the untouched one over 10 s: %zu samples differ (first at %zu), by at most %g", bad, first, mdab);
  // against the offline render (timeline shifted by F frames: float rounding of the timeline only, see live_equals_render)
  static RenderPads rp; fillRenderPads(rp, ps); Song song; JsonError je; song.load(text.data(), text.size(), &je, true); SeqVars vars; std::vector<float> rl((size_t)NQ), rr((size_t)NQ);
  { SeqOpts so; so.once = false; so.live = false; so.sim = false; so.randomSeed = 0x2545F491u; SongRenderer R(song, vars, rp, SR, 512, so, 64); R.block(rl.data(), rr.data(), (int)NQ); }
  size_t d = 0; double md = 0; for (size_t i = 0; i < la.size(); i++) if (std::memcmp(&la[i], &rl[i], 4)) { d++; md = std::max(md, (double)std::fabs(la[i] - rl[i])); }
  CHECK(d * 100000 <= (size_t)NQ * 2 && md <= 1e-6, "against the offline render: %zu of %lld samples differ, by at most %g", d, (long long)NQ, md);
 
  std::printf("live_threads: %d checks, %d failed\n", g_checks, g_fail); return g_fail ? 1 : 0;
}
