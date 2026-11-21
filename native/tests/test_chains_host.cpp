// C1: ctest `chains_host`. ChainsHost against a ChainsNode driven directly: the hook's feeds equal a direct run bit for bit (in bus-map order), the transport is the song's and survives a patch swap, commit returns what it
// replaced and frees nothing on the audio side, a refused (hostile) patch leaves the playing one, events are counted, sorted and applied at their quantum, a failing block is silence and counted. Where the platform allows
// (glibc, no sanitizer) malloc / realloc / free / operator new are COUNTED while hook(), commit() and the event apply run, in the style of test_chains_alloc: any call fails (else: the audit is skipped, the rest runs).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#define ALLOC_AUDIT 1
#endif
#ifdef ALLOC_AUDIT
static thread_local int g_on = 0; static thread_local unsigned long g_cnt = 0;
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_on) g_cnt++; return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_on) g_cnt++; return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (g_on) g_cnt++; return __libc_realloc(p, n); }
void free(void* p) { if (g_on && p) g_cnt++; __libc_free(p); } }
#else
static int g_on = 0; static unsigned long g_cnt = 0;
#endif
#include "chains/chost.h"
#include "chains_song_common.h"
#include "rt/json.h"

using namespace thunder;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)
static const double kSr = 48000;

// tracks (0-based) 0 and 1 feed bus track 3 through a Stereo mixer with different levels and pans (so a swapped pair changes the sound); the saved transport says 120 / 4 / 6
static const char* kHead = "{\"format\":\"chains.patch\",\"version\":1,\"transport\":{\"tempo\":120,\"lpb\":4,\"ticks\":6,\"swing\":0},\"st2\":1,\"ti2\":1,";
static const char* kMods = "{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"i2\",\"type\":\"trackIn\",\"params\":{\"track\":2,\"pre\":0}},"
  "{\"id\":\"mA\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"level\":1,\"l1\":0.8,\"p1\":-0.4,\"l2\":0.6,\"p2\":0.5}},{\"id\":\"o4\",\"type\":\"chainOut\",\"params\":{\"track\":4,\"level\":1}}";
static const char* kCabs = "{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i2\",\"out\"],\"to\":[\"mA\",\"in2\"]},{\"from\":[\"i2\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},"
  "{\"from\":[\"mA\",\"out\"],\"to\":[\"o4\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"o4\",\"inR\"]}";
static std::string patchA() { return std::string(kHead) + "\"modules\":[" + kMods + "],\"cables\":[" + kCabs + "]}"; }
// B: a tempo-synced LFO on a VCA between the mixer and the Chain Out (the golden's lfoPatch)
static std::string patchB() {
  std::string cabs = "{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i2\",\"out\"],\"to\":[\"mA\",\"in2\"]},{\"from\":[\"i2\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},"
    "{\"from\":[\"mA\",\"out\"],\"to\":[\"v1\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"v1\",\"inR\"]},{\"from\":[\"v1\",\"out\"],\"to\":[\"o4\",\"in\"]},{\"from\":[\"v1\",\"outR\"],\"to\":[\"o4\",\"inR\"]},{\"from\":[\"l1\",\"out\"],\"to\":[\"v1\",\"cv\"]}";
  return std::string(kHead) + "\"modules\":[" + kMods + ",{\"id\":\"l1\",\"type\":\"lfo\",\"params\":{\"rate\":8,\"sync\":1,\"shape\":3,\"depth\":0.8}},{\"id\":\"v1\",\"type\":\"vca\",\"params\":{\"gain\":1}}],\"cables\":[" + cabs + "]}";
}
// F: an Auto pan (sync on) between the mixer and the Chain Out: its `div` parameter NaN makes the engine's block fail (the JS throws, the worklet's catch clears the buses)
static std::string patchF() {
  std::string cabs = "{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i2\",\"out\"],\"to\":[\"mA\",\"in2\"]},{\"from\":[\"i2\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},"
    "{\"from\":[\"mA\",\"out\"],\"to\":[\"ap\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"ap\",\"inR\"]},{\"from\":[\"ap\",\"out\"],\"to\":[\"o4\",\"in\"]},{\"from\":[\"ap\",\"outR\"],\"to\":[\"o4\",\"inR\"]}";
  return std::string(kHead) + "\"modules\":[" + kMods + ",{\"id\":\"ap\",\"type\":\"autopan\",\"params\":{\"sync\":1,\"div\":2}}],\"cables\":[" + cabs + "]}";
}
struct Doc { JsonDoc d; Json r; explicit Doc(const std::string& t) { if (!d.parse(t)) std::printf("bad json: %s\n", t.c_str()); r = d.root(); } };
static bool same(const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0; }

struct Sig { std::vector<float> L[3], R[3]; };   // tracks 3 (the bus's own sound), 0 and 1 (feeds): `q` quanta of a generated signal
static Sig makeSig(int quanta) { Sig s; const int tr[3] = {3, 0, 1}; for (int k = 0; k < 3; k++) c1::makeInput(tr[k], quanta * 128, s.L[k], s.R[k]); return s; }
static bool chainOn3[kSongNT];
static void initChainOn() { for (int t = 0; t < kSongNT; t++) chainOn3[t] = t == 0 || t == 1 || t == 3; }

// the direct reference: what the page's node gets (G5 / G6): the song's transport over the patch's saved one, the bus map, the pairs in map order
struct Ref {
  chains::ChainsNode n; std::vector<float> L, R;
  Ref(double bpm, double lpb, double ticks) : n(kSr, 3, [&] { chains::EngineOptions o; o.transport.tempo = bpm; o.transport.lpb = lpb; o.transport.ticks = ticks; return o; }()) {}
  void setup(const Doc& p, double bpm, double lpb, double ticks, bool applyTransport = true) {
    n.engine().loadPatch(p.r); const int bm[2] = {0, 1}; n.engine().setBusMap(bm, 2);
    if (applyTransport) { waves::Transport t = n.engine().transport(); t.tempo = bpm; t.lpb = lpb; t.ticks = ticks; t.playing = true; n.engine().setTransport(t); }
  }
  void run(const Sig& s, int q, bool swapFeeds = false) {
    L.assign(s.L[0].begin() + q * 128, s.L[0].begin() + q * 128 + 128); R.assign(s.R[0].begin() + q * 128, s.R[0].begin() + q * 128 + 128);
    const float* f[4] = {s.L[1].data() + q * 128, s.R[1].data() + q * 128, s.L[2].data() + q * 128, s.R[2].data() + q * 128};
    const float* g[4] = {f[2], f[3], f[0], f[1]};
    chains::NodeIn in; in.inL = L.data(); in.inR = R.data(); in.feeds = swapFeeds ? g : f; in.nFeeds = 2;
    chains::NodeOut out; out.L = L.data(); out.R = R.data(); n.process(in, out, 128);
  }
};
static void hookRun(ChainsHost& h, const Sig& s, int q, std::vector<float>& L, std::vector<float>& R, double t0) {
  L.assign(s.L[0].begin() + q * 128, s.L[0].begin() + q * 128 + 128); R.assign(s.R[0].begin() + q * 128, s.R[0].begin() + q * 128 + 128);
  MixFeed fd[2] = {{0, false, s.L[1].data() + q * 128, s.R[1].data() + q * 128}, {1, false, s.L[2].data() + q * 128, s.R[2].data() + q * 128}};
  ChainsHost::hook(&h, 3, L.data(), R.data(), 128, t0, fd, 2);
}
static std::unique_ptr<Mixer> mkMixer() { return std::unique_ptr<Mixer>(new Mixer(kSr, 128)); }
static ChainsPlan* planOf(ChainsHost& h, const Doc& p, uint32_t heard = 0xffffffffu, double bpm = 131, double lpb = 8, double ticks = 6) { ChainsSongTransport tp; tp.tempo = bpm; tp.lpb = lpb; tp.ticks = ticks; return h.plan(p.r, chainOn3, heard, tp); }

int main() {
  initChainOn(); const Sig sig = makeSig(80);

  // 1. the hook's feeds equal a direct ChainsNode run, bit for bit, in bus-map order (and the test can tell: swapped pairs differ)
  {
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pa)); CHECK("plan", p != nullptr); h.commit(p.get(), *mix);
    CHECK("nodes: only the bus track has a node", p->nodeMask() == (1u << 3));
    CHECK("bus map", p->busMap(3) == std::vector<int>({0, 1}));
    CHECK("mixer: bus exists, sources routed (pure)", (mix->busMask() >> 3 & 1u) && mix->routedMask() != 0);
    Ref ref(131, 8, 6), alt(131, 8, 6); ref.setup(pa, 131, 8, 6); alt.setup(pa, 131, 8, 6);
    bool allSame = true, anySwapDiffers = false; std::vector<float> L, R;
    for (int q = 0; q < 80; q++) { hookRun(h, sig, q, L, R, q * 128 / kSr); ref.run(sig, q); alt.run(sig, q, true); allSame = allSame && same(L, ref.L) && same(R, ref.R); anySwapDiffers = anySwapDiffers || !same(alt.L, ref.L); }
    CHECK("hook == direct ChainsNode, 80 quanta", allSame);
    CHECK("the test discriminates the feed pair order", anySwapDiffers);
    CHECK("no failed block, no mismatch", h.stats().failedBlocks == 0 && h.stats().feedMismatch == 0 && h.stats().blocks == 80);
  }

  // 2. commit: a second plan for the same track swaps the prepared patch in, returns the old one in the plan, and the song's transport (131 / 8) wins over the saved one (120 / 4) after the swap
  {
    Doc pa(patchA()), pb(patchB()); ChainsHost h(kSr); auto mix = mkMixer();
    std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); h.commit(p1.get(), *mix);
    std::vector<float> L, R; for (int q = 0; q < 10; q++) hookRun(h, sig, q, L, R, q * 128 / kSr);
    std::unique_ptr<ChainsPlan> p2(planOf(h, pb)); CHECK("plan 2", p2 != nullptr && p2->refused() == 0);
    g_cnt = 0; g_on = 1; h.commit(p2.get(), *mix); g_on = 0;
    CHECK("commit allocates / frees nothing (the swap included)", g_cnt == 0);
    CHECK("commit returned the old patch, no node", p2->retiredPatches() == 1 && p2->retiredNodes() == 0 && p1->retiredPatches() == 0);
    Ref ref(131, 8, 6), wrong(131, 8, 6); ref.setup(pb, 131, 8, 6); wrong.setup(pb, 131, 8, 6, false);   // wrong: the patch's saved transport left in force (mutation: no re-apply)
    bool ok = true, ctl = false;
    for (int q = 10; q < 60; q++) { hookRun(h, sig, q, L, R, q * 128 / kSr); ref.run(sig, q); wrong.run(sig, q); ok = ok && same(L, ref.L) && same(R, ref.R); ctl = ctl || !same(wrong.L, ref.L); }
    CHECK("after the swap the node equals a direct node with the song's transport", ok);
    CHECK("the saved transport would sound different (the test pins it)", ctl);
    CHECK("the transport is the song's", h.node(3)->engine().transport().tempo == 131 && h.node(3)->engine().transport().lpb == 8 && h.node(3)->engine().transport().playing);
  }

  // 3. a hostile patch: refused, the playing patch stays; a fresh node for a hostile patch is no node (the track plays dry, no bus)
  {
    Doc pa(patchA()); Doc bad(std::string(kHead) + "\"modules\":[" + kMods + "],\"cables\":[null]}");
    ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); h.commit(p1.get(), *mix);
    std::vector<float> L, R; Ref ref(131, 8, 6); ref.setup(pa, 131, 8, 6); bool ok = true;
    for (int q = 0; q < 10; q++) { hookRun(h, sig, q, L, R, q * 128 / kSr); ref.run(sig, q); }
    { waves::PatchImpl* x = ref.n.engine().preparePatch(bad.r); CHECK("direct: refused", x == nullptr); }   // (the reference is disturbed the same way, if preparing disturbs)
    std::unique_ptr<ChainsPlan> p2(planOf(h, bad)); CHECK("hostile: plan made", p2 != nullptr);
    CHECK("hostile: refused, counted, a no-op plan", p2 && p2->refused() == 1 && p2->noop() && p2->nodeMask() == (1u << 3));
    h.commit(p2.get(), *mix); CHECK("hostile: nothing swapped", p2->retiredPatches() == 0);
    for (int q = 10; q < 30; q++) { hookRun(h, sig, q, L, R, q * 128 / kSr); ref.run(sig, q); const bool s = same(L, ref.L) && same(R, ref.R); if (!s && ok) std::printf("  first difference at quantum %d\n", q); ok = ok && s; }
    CHECK("hostile: the playing patch goes on, state kept", ok);
    ChainsHost h2(kSr); auto mix2 = mkMixer(); std::unique_ptr<ChainsPlan> q1(planOf(h2, bad)); CHECK("fresh hostile: refused", q1 && q1->refused() == 1 && q1->nodeMask() == 0);
    h2.commit(q1.get(), *mix2); CHECK("fresh hostile: no hook, no bus", mix2->busMask() == 0 && h2.node(3) == nullptr);
  }

  // 4. heard: in a stem only the heard tracks are hooked; the others get no node
  {
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pa, (1u << 0) | (1u << 1)));   // the bus track 3 is not heard
    CHECK("un-heard bus: no node", p && p->nodeMask() == 0); h.commit(p.get(), *mix); CHECK("un-heard bus: no bus in the mixer", mix->busMask() == 0);
    ChainsHost h2(kSr); auto mix2 = mkMixer(); std::unique_ptr<ChainsPlan> p2(planOf(h2, pa, 1u << 3)); CHECK("heard bus: node", p2 && p2->nodeMask() == (1u << 3));
    // a track that stops being heard loses its node: the plan removes it and returns it
    h2.commit(p2.get(), *mix2); std::unique_ptr<ChainsPlan> p3(planOf(h2, pa, 0)); h2.commit(p3.get(), *mix2);
    CHECK("removed node returned", p3->retiredNodes() == 1 && h2.node(3) == nullptr && mix2->busMask() == 0);
  }

  // 5. events: the two quantum rules, the sorted array, the counted drops, the bad ones
  {
    ChainsHost h(kSr);
    SeqEvent e; e.type = SEV_CHC; e.track = 3; e.p = 1; e.v = 1; e.t = 0.4;
    int64_t kk = -1, differ = 0;
    for (int k = 1; k < 3000; k++) { const double t = (double)k * 128 / kSr; const int64_t a = h.tempoQuantum(t), b = h.chcQuantum(t); if (a != b && kk < 0) kk = k; if (a != b) differ++;
      CHECK("chc rule = ceil(ceil(t * sr) / 128)", b == (int64_t)std::ceil(std::ceil(t * kSr) / 128.0)); CHECK("tempo rule = ceil(t / q), q = 128.0 / sr", a == (int64_t)std::ceil(t / (128.0 / kSr))); }
    CHECK("the two rules differ for some quantum starts (the k5 case of the golden pins one)", differ > 0);
    CHECK("one frame after a quantum start is the next quantum", h.chcQuantum(151 * 128.0 / kSr - 1.0 / kSr) == 151 && h.chcQuantum((150 * 128 + 1) / kSr) == 151 && h.tempoQuantum((150 * 128 + 1) / kSr) == 151);
    CHECK("t <= 0 is quantum 0", h.chcQuantum(-1) == 0 && h.tempoQuantum(0) == 0);
    int kept = 0; for (int i = 0; i < 300; i++) { e.t = 1.0 + i * 0.001; kept += h.event(e) ? 1 : 0; }
    CHECK("256 events kept", kept == kChainsHostEvents && h.pendingEvents() == kChainsHostEvents); CHECK("44 drops counted", h.stats().droppedEvents == 44);
    SeqEvent b = e; b.track = 99; CHECK("bad track refused", !h.event(b)); b = e; b.p = 16; CHECK("bad switch refused", !h.event(b)); b = e; b.v = NAN; CHECK("NaN value refused", !h.event(b)); b = e; b.t = NAN; CHECK("NaN time refused", !h.event(b));
    CHECK("bad events counted", h.stats().badEvents == 4); b = e; b.type = SEV_FXC; CHECK("another kind is not taken", !h.event(b));
  }
  {   // applied at their quantum, in time order whatever the order they came in, to every node; a chc for a track with no node is counted
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pa)); h.commit(p.get(), *mix);
    SeqEvent t1; t1.type = SEV_TEMPO; t1.v = 90; t1.tks = 5; t1.t = 20 * 128 / kSr - 1e-9;      // quantum 20
    SeqEvent t2 = t1; t2.v = 100; t2.t = 10 * 128 / kSr - 1e-9;                                 // quantum 10, given LATER
    SeqEvent c1e; c1e.type = SEV_CHC; c1e.track = 3; c1e.p = 2; c1e.v = 1; c1e.t = 12 * 128 / kSr - 1e-9; SeqEvent c2 = c1e; c2.track = 5; c2.t = 12 * 128 / kSr - 1e-9;
    h.event(t1); h.event(t2); h.event(c1e); h.event(c2);
    std::vector<float> L, R;
    for (int q = 0; q < 25; q++) {
      hookRun(h, sig, q, L, R, q * 128 / kSr); const waves::Transport& T = h.node(3)->engine().transport();
      if (q == 9) CHECK("tempo not yet applied at quantum 9", T.tempo == 131); if (q == 10) CHECK("the earlier event applied at quantum 10", T.tempo == 100 && T.ticks == 5);
      if (q == 19) CHECK("still 100 at quantum 19", T.tempo == 100); if (q == 20) CHECK("the later event at quantum 20", T.tempo == 90);
    }
    CHECK("all four applied, the chc for the node-less track counted", h.stats().eventsApplied == 4 && h.stats().chcNoNode == 1 && h.pendingEvents() == 0);
  }

  // 6. a failing block is silence for that track and counted (an Auto pan whose division is NaN: the page's JS throws, the engine's catch clears the buses)
  {
    Doc pb(patchF()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pb)); h.commit(p.get(), *mix);
    std::vector<float> L, R; hookRun(h, sig, 0, L, R, 0); bool loud = false; for (float v : L) loud = loud || v != 0; CHECK("sound before", loud);
    h.node(3)->engine().setParam("ap", "div", NAN);
    hookRun(h, sig, 1, L, R, 128 / kSr); bool silent = true; for (float v : L) silent = silent && v == 0; for (float v : R) silent = silent && v == 0;
    CHECK("a failed block is silence", silent); CHECK("and counted", h.stats().failedBlocks == 1);
  }

  // 7. the allocation audit: hook, commit and the event apply (the plan, with everything that allocates, is made outside)
#ifdef ALLOC_AUDIT
  {
    Doc pa(patchA()), pb(patchB()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); h.commit(p1.get(), *mix);
    std::unique_ptr<ChainsPlan> p2(planOf(h, pb)), p3(planOf(h, pa));
    SeqEvent ev[6]; for (int i = 0; i < 6; i++) { ev[i].type = i % 2 ? SEV_CHC : SEV_TEMPO; ev[i].track = 3; ev[i].p = 1 + i; ev[i].v = i % 2 ? 1 : 80 + i; ev[i].tks = 6; ev[i].t = (5 + 7 * i) * 128 / kSr; }
    std::vector<float> L(128), R(128); const float* f[4] = {sig.L[1].data(), sig.R[1].data(), sig.L[2].data(), sig.R[2].data()};
    MixFeed fd[2] = {{0, false, f[0], f[1]}, {1, false, f[2], f[3]}};
    unsigned long total = 0;
    for (int q = 0; q < 60; q++) {
      for (int i = 0; i < 128; i++) { L[(size_t)i] = sig.L[0][(size_t)(q % 80) * 128 + (size_t)i]; R[(size_t)i] = sig.R[0][(size_t)(q % 80) * 128 + (size_t)i]; }
      g_cnt = 0; g_on = 1;
      if (q == 0) for (int i = 0; i < 6; i++) h.event(ev[i]);
      if (q == 20) h.commit(p2.get(), *mix);
      if (q == 40) h.commit(p3.get(), *mix);
      ChainsHost::hook(&h, 3, L.data(), R.data(), 128, q * 128 / kSr, fd, 2);
      g_on = 0; total += g_cnt;
    }
    CHECK("hook, commit (two swaps) and the event apply: 0 allocator calls", total == 0);
    if (total) std::printf("  allocator calls: %lu\n", total);
    CHECK("the audit ran the events", h.stats().eventsApplied == 6);
  }
#else
  std::printf("chains_host: the allocation audit is skipped (needs glibc without a sanitizer)\n");
#endif
  std::printf(g_fail ? "chains_host FAILED (%d)\n" : "chains_host OK\n", g_fail);
  return g_fail ? 1 : 0;
}
