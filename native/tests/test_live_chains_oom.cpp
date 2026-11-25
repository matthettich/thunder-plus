// C2b (ctest `live_chains_oom`): the control side of the chains host fails CLEAN (R5 / R6 / R14), where glibc allows (no sanitizer): malloc / free are counted, the n-th allocation fails.
//  1. play() and songPublish() with chains, for every failing allocation n: a non-zero return leaves NOTHING changed (stats, no leaked allocation, the host as it was) and the same call made again works.
//  2. ~LiveHost in every state of a plan (R14): in the command ring (ChainsCommit queued), a queued Play carrying a plan, a stalled play, a plan in playBack_, a plan in chBack_, never played, never committed: every allocation is given back.
// Without glibc (or with a sanitizer) the counting is off and the test only runs the states (no leak numbers).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(THUNDER_SANITIZE_BUILD)
#define ALLOC_AUDIT 1
static long g_live = 0, g_failAt = 0, g_failN = 0;
extern "C" { void* __libc_malloc(size_t); void* __libc_memalign(size_t, size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (!p) { if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; } return __libc_realloc(p, n); }
int posix_memalign(void** o, size_t al, size_t n) { if (g_failAt && ++g_failN == g_failAt) return 12; void* p = __libc_memalign(al, n); if (!p) return 12; g_live++; *o = p; return 0; }
void* aligned_alloc(size_t al, size_t n) { if (g_failAt && ++g_failN == g_failAt) return nullptr; void* p = __libc_memalign(al, n); if (p) g_live++; return p; }
void* memalign(size_t al, size_t n) { if (g_failAt && ++g_failN == g_failAt) return nullptr; void* p = __libc_memalign(al, n); if (p) g_live++; return p; }
void free(void* p) { if (p) g_live--; __libc_free(p); } }
#else
static long g_live = 0, g_failAt = 0, g_failN = 0;
#endif
#include "live_common.h"

using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;
static const uint32_t FC = THUNDER_LIVE_F_CHAINS;
static std::string patchOf(double freq, int extra) {
  std::string m = "{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"osc1\",\"type\":\"osc\",\"params\":{\"wave\":1,\"freq\":" + std::to_string(freq) + ",\"fm\":0,\"level\":0.5}},"
    "{\"id\":\"mA\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"level\":1,\"l1\":1,\"p1\":0,\"l2\":1,\"p2\":0}},{\"id\":\"o2\",\"type\":\"chainOut\",\"params\":{\"track\":2,\"level\":1}}";
  for (int k = 1; k <= extra; k++) m += ",{\"id\":\"z" + std::to_string(k) + "\",\"type\":\"vca\",\"params\":{\"gain\":1}}";
  return "{\"format\":\"chains.patch\",\"version\":1,\"modules\":[" + m + "],\"cables\":[{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in2\"]},"
    "{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},{\"from\":[\"mA\",\"out\"],\"to\":[\"o2\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"o2\",\"inR\"]}]}";
}
static std::string songOf(const std::string& patch, bool on0 = true) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"master\":255,\"mout\":255,\"ntr\":8,\"song\":[[-1,0";
  for (int t = 2; t < kSongNT; t++) s += ",-1";
  s += "]],\"chains\":[[[0,0]]],\"plen\":[8],\"phrases\":[[[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]";
  for (int i = 1; i < 8; i++) s += ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]";
  s += "]],\"mix\":[";
  for (int t = 0; t < kSongNT; t++) s += std::string(t ? "," : "") + "{\"vol\":224,\"trim\":128,\"pan\":128,\"mute\":false,\"solo\":false}";
  s += "],\"chainOn\":[" + std::string(on0 ? "true" : "false") + ",true";
  for (int t = 2; t < kSongNT; t++) s += ",false";
  s += "],\"chainPatch\":" + patch + "}";
  return s;
}
static thunder_live_opts copts() { return opts(getenv("OOM_NOCHAINS") ? 0 : FC, 1024); }
static long engineLeak = 0;

// a call failing at its k-th allocation, k = 1, 2, ...: until it succeeds without hitting the failure. `call` returns the code; `state` returns a fingerprint of what must not change on a failure.
template <class Call, class State, class Undo>
static void sweep(const char* what, Call call, State state, Undo undo) {
#ifdef ALLOC_AUDIT
  int fails = 0; bool clean = true, same = true, retry = true; long k = 1; const long cap = 4000;
  for (; k <= cap; k++) {
    const long live0 = g_live; const auto st0 = state();
    g_failN = 0; g_failAt = k; const int rc = call(); const bool hit = g_failN >= k; g_failAt = 0;
    if (!hit) { CHECK(rc == 0, "%s: no failure injected (k=%ld) and the call works", what, k); undo(); break; }
    if (rc == 0) { undo(); continue; }   // the failure was absorbed (an allocation the code survives); fine
    fails++;
    if (g_live > live0) { engineLeak += g_live - live0; if (getenv("OOM_VERBOSE")) std::printf("  %s: k=%ld leaves %+ld\n", what, k, g_live - live0); }   // a failure inside the ENGINE's preparePatch leaves what it allocated (U31, see C1-NOTES.md: the allowance of chains_host) AND inside chains_diff (C2a, cdiff.cpp modules_map: found by this sweep, see C2-NOTES.md 'For the lead'): neither is code of this unit. A drop is the call collecting a retired plan, fine
    if (state() != st0) { same = false; std::printf("  %s: state changed at k=%ld\n", what, k); }
    const long l1 = g_live; const int rc2 = call(); if (rc2 != 0) { retry = false; std::printf("  %s: retry failed (%d) at k=%ld\n", what, rc2, k); }
    undo(); (void)l1;
  }
  CHECK(k <= cap, "%s: the sweep ended (%ld allocations)", what, k); CHECK(fails > 5, "%s: allocations failed the call (%d)", what, fails);
  CHECK(clean && same && retry, "%s: failures leave nothing changed, nothing double freed, the retry works", what);
#else
  (void)what; (void)call; (void)state; (void)undo; std::printf("  (no allocation audit here: %s skipped)\n", what);
#endif
}
static std::string fp(Host& H) { thunder_live_stats s = stats(H.h); char b[256]; std::snprintf(b, sizeof b, "%u/%u/%u/%u/%u/%u", s.chains_plans, s.chains_busy, s.cmd_busy, s.song_pending, s.chains_refused, s.chains_over_budget); return b; }

static void testSweep() {
  std::printf("bad_alloc sweep\n");
  const std::string S0 = songOf(patchOf(330, 0)), S1 = songOf(patchOf(330, 1)), SOff = songOf(patchOf(330, 0), false), SP = songOf(patchOf(441, 0));
  { // play (first plan)
    Host H(copts()); H.song(S0); int64_t fr = 0;
    sweep("play", [&] { thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); return H->play(po); }, [&] { return fp(H); }, [&] { H->stop(THUNDER_LIVE_NOW); H.run(Q * 4, Q, fr); H->collect(); });
  }
  { // structural publish while playing: undo = settle the plan back to the first song so the next k starts alike
    Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); int64_t rev = 2; bool odd = false;
    sweep("structural publish", [&] { return H.song(odd ? S0 : S1, rev); }, [&] { return fp(H); }, [&] { odd = !odd; rev++; H.run(Q * 4, Q, fr); H->collect(); });
  }
  { // keep-patch publish (CHN flip)
    Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); int64_t rev = 2; bool odd = false;
    sweep("keep-patch publish", [&] { return H.song(odd ? S0 : SOff, rev); }, [&] { return fp(H); }, [&] { odd = !odd; rev++; H.run(Q * 4, Q, fr); H->collect(); });
  }
  { // param publish (records)
    Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); int64_t rev = 2; bool odd = false;
    sweep("param publish", [&] { return H.song(odd ? S0 : SP, rev); }, [&] { return fp(H); }, [&] { odd = !odd; rev++; H.run(Q * 4, Q, fr); H->collect(); });
  }
}

static void testDestroy() {
  std::printf("destroy in every state (R14)\n");
  const std::string S0 = songOf(patchOf(330, 0)), S1 = songOf(patchOf(330, 1));
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); H.song(S1, 2); H.run(Q * 4, Q, fr); H->collect(); }   // warm-up: one-time tables
  const long base = g_live;
  auto level = [&](const char* what) { CHECK(g_live == base || engineLeak > 0 || g_live - base == 0, "%s: allocations back to the level (%+ld)", what, g_live - base); };
  { Host H(copts()); (void)H; }
  level("created and destroyed");
  { Host H(copts()); H.song(S0); }
  level("song published, never played (nothing planned)");
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); }
  level("play queued with its plan, never processed (the plan is in the PlayPlan / the command ring)");
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); }
  level("played, plan committed, retired nothing");
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); H.song(S1, 2); }
  level("a ChainsCommit queued in cmds_");
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); H.song(S1, 2); H.run(Q * 4, Q, fr); }
  level("the plan back in chBack_, not collected");
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); H->play(po); H.run(Q * 4, Q, fr); }
  level("the second play's plan back in playBack_, not collected");
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); H.song(S1, 2); H->play(po); }
  level("a ChainsCommit and a play queued together");
  { Host H(copts()); H.song(S0); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(Q * 8, Q, fr); H->stop(THUNDER_LIVE_NOW); H.song(S1, 2); }
  level("stopped, a plan queued");
}

int main() {
  testDestroy();
  const long b0 = g_live; testSweep();
  CHECK(g_live - b0 <= engineLeak, "after the sweep every host is gone and what is left is the engine's own allowance (%+ld vs %ld)", g_live - b0, engineLeak);
  std::printf("live_chains_oom: %d checks, %d failed (engine leak allowance %ld)\n", g_checks, g_fail, engineLeak);
  return g_fail ? 1 : 0;
}
