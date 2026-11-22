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
#if !defined(_WIN32)
#include <unistd.h>
#endif
#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__)
#define ALLOC_AUDIT 1
#endif
#ifdef ALLOC_AUDIT
static thread_local int g_on = 0; static thread_local unsigned long g_cnt = 0;
static long g_live = 0; static long g_failAt = 0, g_failN = 0;   // live = allocations minus frees (a leak check without a sanitizer); g_failAt: the n-th allocation fails (null: operator new throws bad_alloc)
extern "C" { void* __libc_malloc(size_t); void* __libc_calloc(size_t, size_t); void* __libc_realloc(void*, size_t); void __libc_free(void*);
void* malloc(size_t n) { if (g_on) g_cnt++; if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_malloc(n); }
void* calloc(size_t a, size_t b) { if (g_on) g_cnt++; if (g_failAt && ++g_failN == g_failAt) return nullptr; g_live++; return __libc_calloc(a, b); }
void* realloc(void* p, size_t n) { if (g_on) g_cnt++; if (!p) g_live++; return __libc_realloc(p, n); }
void free(void* p) { if (g_on && p) g_cnt++; if (p) g_live--; __libc_free(p); } }
#else
static int g_on = 0; static unsigned long g_cnt = 0;
#endif
#include "chains/chost.h"
#include "chains_song_common.h"
#include "rt/json.h"

using namespace thunder;
static int g_fail = 0; static char g_msg[80] = "";
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
// E: only Track In 1 feeds the mixer (bus map [0], where patchA's is [0, 1])
static std::string patchE() {
  const char* mods = "{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"mA\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"level\":1,\"l1\":0.8,\"p1\":-0.4}},{\"id\":\"o4\",\"type\":\"chainOut\",\"params\":{\"track\":4,\"level\":1}}";
  const char* cabs = "{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"mA\",\"out\"],\"to\":[\"o4\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"o4\",\"inR\"]}";
  return std::string(kHead) + "\"modules\":[" + mods + "],\"cables\":[" + cabs + "]}";
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
// the protocol (chost.h): commit on the audio side, then the control side settles the plan
static void adopt(ChainsHost& h, ChainsPlan* p, Mixer& m) { h.commit(p, m); h.settle(p); }
static ChainsPlan* planOf(ChainsHost& h, const Doc& p, uint32_t heard = 0xffffffffu, double bpm = 131, double lpb = 8, double ticks = 6) { ChainsSongTransport tp; tp.tempo = bpm; tp.lpb = lpb; tp.ticks = ticks; return h.plan(p.r, chainOn3, heard, tp); }

int main() {
  std::setvbuf(stdout, nullptr, _IONBF, 0);   // (a crash on a CI machine must show how far the test got)
  initChainOn(); const Sig sig = makeSig(80);

  // 1. the hook's feeds equal a direct ChainsNode run, bit for bit, in bus-map order (and the test can tell: swapped pairs differ)
  {
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pa)); CHECK("plan", p != nullptr); adopt(h, p.get(), *mix);
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
    std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); adopt(h, p1.get(), *mix);
    std::vector<float> L, R; for (int q = 0; q < 10; q++) hookRun(h, sig, q, L, R, q * 128 / kSr);
    std::unique_ptr<ChainsPlan> p2(planOf(h, pb)); CHECK("plan 2", p2 != nullptr && p2->refused() == 0);
    g_cnt = 0; g_on = 1; h.commit(p2.get(), *mix); g_on = 0; h.settle(p2.get());
    CHECK("commit allocates / frees nothing (the swap included)", g_cnt == 0);
    CHECK("commit returned the old patch, no node", p2->retiredPatches() == 1 && p2->retiredNodes() == 0 && p1->retiredPatches() == 0);
    Ref ref(131, 8, 6), wrong(131, 8, 6); ref.setup(pb, 131, 8, 6); wrong.setup(pb, 131, 8, 6, false);   // wrong: the patch's saved transport left in force (mutation: no re-apply)
    bool ok = true, ctl = false;
    for (int q = 10; q < 60; q++) { hookRun(h, sig, q, L, R, q * 128 / kSr); ref.run(sig, q); wrong.run(sig, q); ok = ok && same(L, ref.L) && same(R, ref.R); ctl = ctl || !same(wrong.L, ref.L); }
    CHECK("after the swap the node equals a direct node with the song's transport", ok);
    CHECK("the saved transport would sound different (the test pins it)", ctl);
    CHECK("the transport is the song's", h.node(3)->engine().transport().tempo == 131 && h.node(3)->engine().transport().lpb == 8 && h.node(3)->engine().transport().playing);
  }

  // 2b. a swap whose feeds change: the node's bus map follows the new plan (commit sets it on an existing node too)
  {
    Doc pa(patchA()), pe(patchE()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); adopt(h, p1.get(), *mix);
    CHECK("map [0, 1] first", h.node(3)->engine().busMap() == std::vector<int>({0, 1}));
    std::unique_ptr<ChainsPlan> p2(planOf(h, pe)); adopt(h, p2.get(), *mix);
    CHECK("after the swap the map is the new patch's [0], as the plan's", h.node(3)->engine().busMap() == std::vector<int>({0}) && p2->busMap(3) == std::vector<int>({0}) && mix->plan().nFeeds[3] == 1);
  }

  // 3. a hostile patch: refused, the playing patch stays; a fresh node for a hostile patch is no node (the track plays dry, no bus)
  {
    Doc pa(patchA()); Doc bad(std::string(kHead) + "\"modules\":[" + kMods + "],\"cables\":[null]}");
    ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); adopt(h, p1.get(), *mix);
    std::vector<float> L, R; Ref ref(131, 8, 6); ref.setup(pa, 131, 8, 6); bool ok = true;
    for (int q = 0; q < 10; q++) { hookRun(h, sig, q, L, R, q * 128 / kSr); ref.run(sig, q); }
    { waves::PatchImpl* x = ref.n.engine().preparePatch(bad.r); CHECK("direct: refused", x == nullptr); }   // (the reference is disturbed the same way, if preparing disturbs)
    std::unique_ptr<ChainsPlan> p2(planOf(h, bad)); CHECK("hostile: plan made", p2 != nullptr);
    CHECK("hostile: refused, counted, a no-op plan", p2 && p2->refused() == 1 && p2->noop() && p2->nodeMask() == (1u << 3));
    adopt(h, p2.get(), *mix); CHECK("hostile: nothing swapped", p2->retiredPatches() == 0);
    for (int q = 10; q < 30; q++) { hookRun(h, sig, q, L, R, q * 128 / kSr); ref.run(sig, q); const bool s = same(L, ref.L) && same(R, ref.R); if (!s && ok) std::printf("  first difference at quantum %d\n", q); ok = ok && s; }
    CHECK("hostile: the playing patch goes on, state kept", ok);
    {   // a refused document with ANOTHER bus plan (one feed) must not reach the mixer or the node's map
      std::string e = patchE(); e.insert(e.find("\"cables\":[") + 10, "null,"); Doc bad2(e); CHECK("bad2 parses", bad2.r.isObj());
      std::unique_ptr<ChainsPlan> pb2(planOf(h, bad2)); CHECK("hostile 2: refused", pb2 && pb2->refused() == 1 && pb2->noop()); adopt(h, pb2.get(), *mix);
      CHECK("hostile 2: the mixer's bus plan and the node's map are the playing patch's", mix->plan().nFeeds[3] == 2 && h.node(3)->engine().busMap() == std::vector<int>({0, 1})); }
    ChainsHost h2(kSr); auto mix2 = mkMixer(); std::unique_ptr<ChainsPlan> q1(planOf(h2, bad)); CHECK("fresh hostile: refused", q1 && q1->refused() == 1 && q1->nodeMask() == 0);
    adopt(h2, q1.get(), *mix2); CHECK("fresh hostile: no hook, no bus", mix2->busMask() == 0 && h2.node(3) == nullptr);
  }

  // 4. heard: in a stem only the heard tracks are hooked; the others get no node
  {
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pa, (1u << 0) | (1u << 1)));   // the bus track 3 is not heard
    CHECK("un-heard bus: no node", p && p->nodeMask() == 0); adopt(h, p.get(), *mix); CHECK("un-heard bus: no bus in the mixer", mix->busMask() == 0);
    ChainsHost h2(kSr); auto mix2 = mkMixer(); std::unique_ptr<ChainsPlan> p2(planOf(h2, pa, 1u << 3)); CHECK("heard bus: node", p2 && p2->nodeMask() == (1u << 3));
    // a track that stops being heard loses its node: the plan removes it and returns it
    adopt(h2, p2.get(), *mix2); std::unique_ptr<ChainsPlan> p3(planOf(h2, pa, 0)); adopt(h2, p3.get(), *mix2);
    CHECK("removed node returned", p3->retiredNodes() == 1 && h2.node(3) == nullptr && mix2->busMask() == 0);
  }

  // 5. events: the two due rules (in frames), the 4096-entry array, the counted drops, the bad ones
  {
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pa)); adopt(h, p.get(), *mix);
    SeqEvent e; e.type = SEV_CHC; e.track = 3; e.p = 1; e.v = 1; e.t = 0.4;
    int64_t differ = 0;
    for (int k = 1; k < 3000; k++) { const double t = (double)k * 128 / kSr; const int64_t a = h.tempoDue(t), b = h.chcDue(t); if (a != b) differ++;
      CHECK("chc rule: due = ceil(ceil(t * sr) / 128) * 128", b == (int64_t)std::ceil(std::ceil(t * kSr) / 128.0) * 128); CHECK("tempo rule: due = ceil(t / q) * 128, q = 128.0 / sr", a == (int64_t)std::ceil(t / (128.0 / kSr)) * 128); }
    CHECK("the two rules differ for some quantum starts (the k5 case of the golden pins one)", differ > 0);
    CHECK("one frame after a quantum start is the next quantum", h.chcDue((150 * 128 + 1) / kSr) == 151 * 128 && h.tempoDue((150 * 128 + 1) / kSr) == 151 * 128);
    CHECK("t <= 0 is due at frame 0", h.chcDue(-1) == 0 && h.tempoDue(0) == 0);
    int kept = 0; for (int i = 0; i < 4200; i++) { e.t = 1.0 + i * 0.0001; kept += h.event(e) ? 1 : 0; }
    CHECK("4096 events kept", kept == kChainsHostEvents && h.pendingEvents() == kChainsHostEvents && kChainsHostEvents >= 4096); CHECK("104 drops counted", h.stats().droppedEvents == 104);
    SeqEvent b = e; b.track = 99; CHECK("bad track refused", !h.event(b)); b = e; b.p = 16; CHECK("bad switch refused", !h.event(b)); b = e; b.v = NAN; CHECK("NaN value refused", !h.event(b)); b = e; b.t = NAN; CHECK("NaN time refused", !h.event(b));
    CHECK("bad events counted", h.stats().badEvents == 4); b = e; b.type = SEV_FXC; CHECK("another kind is not taken", !h.event(b));
  }
  {   // applied at their frame, in time order whatever the order they came in, to every node; a chc for a track with no node is counted when it arrives and not queued
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pa)); adopt(h, p.get(), *mix);
    SeqEvent t1; t1.type = SEV_TEMPO; t1.v = 90; t1.tks = 5; t1.t = 20 * 128 / kSr - 1e-9;      // quantum 20
    SeqEvent t2 = t1; t2.v = 100; t2.t = 10 * 128 / kSr - 1e-9;                                 // quantum 10, given LATER
    SeqEvent c1e; c1e.type = SEV_CHC; c1e.track = 3; c1e.p = 2; c1e.v = 1; c1e.t = 12 * 128 / kSr - 1e-9; SeqEvent c2 = c1e; c2.track = 5;
    h.event(t1); h.event(t2); h.event(c1e); CHECK("a chc for a track with no node is dropped, not queued", !h.event(c2) && h.pendingEvents() == 3);
    std::vector<float> L, R;
    for (int q = 0; q < 25; q++) {
      h.beginBlock((int64_t)q * 128); hookRun(h, sig, q, L, R, q * 128 / kSr); const waves::Transport& T = h.node(3)->engine().transport();
      if (q == 9) CHECK("tempo not yet applied at quantum 9", T.tempo == 131); if (q == 10) CHECK("the earlier event applied at quantum 10", T.tempo == 100 && T.ticks == 5);
      if (q == 11) CHECK("the chc is not yet applied at quantum 11", h.node(3)->engine().bypassSwitchState(2) == -1); if (q == 12) CHECK("the chc applied at quantum 12", h.node(3)->engine().bypassSwitchState(2) == 1);
      if (q == 19) CHECK("still 100 at quantum 19", T.tempo == 100); if (q == 20) CHECK("the later event at quantum 20", T.tempo == 90);
    }
    CHECK("three applied, the chc for the node-less track counted", h.stats().eventsApplied == 3 && h.stats().chcNoNode == 1 && h.pendingEvents() == 0);
    CHECK("the host's transport follows", h.transport().tempo == 90 && h.transport().ticks == 5);
    // two events at the same time: the later one given is applied last (a stable order)
    SeqEvent s1 = t1; s1.v = 91; s1.t = 40 * 128 / kSr - 1e-9; SeqEvent s2 = s1; s2.v = 101; h.event(s1); h.event(s2); h.beginBlock(40 * 128); CHECK("same frame: the one given last wins", h.node(3)->engine().transport().tempo == 101);
    // a node that goes away after its chc was queued: counted, not applied
    { ChainsHost h3(kSr); auto mix3 = mkMixer(); std::unique_ptr<ChainsPlan> p3(planOf(h3, pa)); adopt(h3, p3.get(), *mix3);
      SeqEvent g = c1e; g.t = 50 * 128 / kSr; const uint64_t ap0 = h3.stats().eventsApplied, nn0 = h3.stats().chcNoNode; CHECK("chc queued", h3.event(g)); std::unique_ptr<ChainsPlan> rm(planOf(h3, pa, 0)); adopt(h3, rm.get(), *mix3); h3.beginBlock(51 * 128);
      CHECK("the node went away: counted as no node, not as applied", h3.stats().chcNoNode == nn0 + 1 && h3.stats().eventsApplied == ap0); }
    // a block that starts later than the event's frame (a host that skipped some) applies everything due at or before it
    SeqEvent late = t1; late.v = 77; late.t = 30 * 128 / kSr; h.event(late); h.beginBlock(31 * 128); CHECK("an event due before the block's frame is applied", h.node(3)->engine().transport().tempo == 77);
  }

  // 6. a failing block is silence for that track and counted (an Auto pan whose division is NaN: the page's JS throws, the engine's catch clears the buses)
  {
    Doc pb(patchF()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p(planOf(h, pb)); adopt(h, p.get(), *mix);
    std::vector<float> L, R; hookRun(h, sig, 0, L, R, 0); bool loud = false; for (float v : L) loud = loud || v != 0; CHECK("sound before", loud);
    h.node(3)->engine().setParam("ap", "div", NAN);
    hookRun(h, sig, 1, L, R, 128 / kSr); bool silent = true; for (float v : L) silent = silent && v == 0; for (float v : R) silent = silent && v == 0;
    CHECK("a failed block is silence", silent); CHECK("and counted", h.stats().failedBlocks == 1);
  }

  // 7. ownership (chost.h, THE PROTOCOL): every sequence of plan / commit / settle / destroy
  {
    Doc pa(patchA()), pb(patchB());
    {   // 7a. an abandoned plan: the control view never saw it, the next plan makes a FRESH node (it must not try to swap a node that was freed)
      ChainsHost h(kSr); auto mix = mkMixer();
      { std::unique_ptr<ChainsPlan> w(planOf(h, pa)); h.settle(w.get()); }   // (statics made)
#ifdef ALLOC_AUDIT
      const long live0 = g_live;
#endif
      std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); CHECK("7a plan", p1 && p1->nodeMask() == (1u << 3));
      h.settle(p1.get()); p1.reset();
#ifdef ALLOC_AUDIT
      CHECK("7a the abandoned plan gave back everything it made", g_live == live0);
#endif
      CHECK("7a abandoned: no node", h.node(3) == nullptr && mix->busMask() == 0);
      std::unique_ptr<ChainsPlan> p2(planOf(h, pb)); CHECK("7a the next plan is a fresh node (one set of patch bytes, not a swap's two)", p2 && p2->nodeMask() == (1u << 3) && p2->bytes() == p2->patchBytes() + kChainsNodeOverhead);
      adopt(h, p2.get(), *mix); CHECK("7a node made, nothing retired", h.node(3) != nullptr && p2->retiredPatches() == 0 && p2->retiredNodes() == 0);
    }
    {   // 7b. one plan in flight: a second plan() is null and counted; after the first is settled planning goes on
      ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); ChainsPlan* second = planOf(h, pb);
      CHECK("7b a second plan is refused", second == nullptr && h.stats().busyRefused == 1);
      h.commit(p1.get(), *mix); CHECK("7b still in flight until settled", planOf(h, pb) == nullptr && h.stats().busyRefused == 2);
      h.settle(p1.get()); std::unique_ptr<ChainsPlan> p3(planOf(h, pb)); CHECK("7b after settle: a swap plan", p3 && p3->nodeMask() == (1u << 3)); adopt(h, p3.get(), *mix);
      CHECK("7b the swap retired the patch", p3->retiredPatches() == 1);
      h.settle(p3.get()); h.settle(p3.get());   // a second settle is nothing
      std::unique_ptr<ChainsPlan> p4(planOf(h, pa)); CHECK("7b settled twice: one plan can be made", p4 != nullptr); h.settle(p4.get());
    }
    {   // 7c. committed twice: the second is nothing (a fresh node is not adopted twice, a patch not swapped twice)
      ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); h.commit(p1.get(), *mix); chains::ChainsNode* n = h.node(3); h.commit(p1.get(), *mix); h.settle(p1.get());
      CHECK("7c fresh: same node, nothing retired", h.node(3) == n && n && p1->retiredNodes() == 0);
      std::unique_ptr<ChainsPlan> p2(planOf(h, pb)); h.commit(p2.get(), *mix); h.commit(p2.get(), *mix); h.settle(p2.get());
      CHECK("7c swap: one patch retired, same node", h.node(3) == n && p2->retiredPatches() == 1);
      std::unique_ptr<ChainsPlan> p3(planOf(h, pa, 0)); h.commit(p3.get(), *mix); h.commit(p3.get(), *mix); h.settle(p3.get());
      CHECK("7c removal: one node retired, none left", p3->retiredNodes() == 1 && h.node(3) == nullptr);
      p3.reset();   // frees the node
      std::unique_ptr<ChainsPlan> p4(planOf(h, pb)); CHECK("7c after a removal: a fresh node again", p4 && p4->nodeMask() == (1u << 3)); adopt(h, p4.get(), *mix); CHECK("7c node back", h.node(3) != nullptr);
    }
    {   // 7d. a stale swap plan whose node is gone: commit() does not touch the (null) node; the patch is freed with the plan
      ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); adopt(h, p1.get(), *mix);
      std::unique_ptr<ChainsPlan> p2(planOf(h, pb)); h.settle(p2.get());   // abandoned (never committed) ...
      std::unique_ptr<ChainsPlan> p3(planOf(h, pa, 0)); adopt(h, p3.get(), *mix); p3.reset();   // ... the node goes away ...
      h.commit(p2.get(), *mix);   // ... and the stale plan is committed late
      CHECK("7d no node, nothing swapped, no crash", h.node(3) == nullptr && p2->retiredPatches() == 0);
    }
    {   // 7e. destroyed without commit or settle by the host's destructor: nothing leaks (live-allocation count where the build has it)
#ifdef ALLOC_AUDIT
      const long live0 = g_live;
#endif
      { ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); adopt(h, p1.get(), *mix); std::unique_ptr<ChainsPlan> p2(planOf(h, pb)); h.commit(p2.get(), *mix); h.settle(p2.get()); }
#ifdef ALLOC_AUDIT
      CHECK("7e host and plans destroyed: no allocation left", g_live == live0);
#endif
    }
  }

  // 8. a hostile / refused plan changes nothing; the budget (D7)
  {
    Doc pa(patchA()), pb(patchB());
    ChainsHost probe(kSr); std::unique_ptr<ChainsPlan> pp(planOf(probe, pa)); const size_t pbytes = pp->patchBytes(), one = pp->bytes(); probe.settle(pp.get());
    CHECK("the patch has a cost from cmem.inc", pbytes > 1024 && one > pbytes && one == pbytes + kChainsNodeOverhead);
    CHECK("default budget 1.5 GiB", probe.budget() == (size_t)1536 << 20);
    { ChainsHost h(kSr, one + 100); CHECK("budget settable", h.budget() == one + 100); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); CHECK("one node fits", p1 && !p1->overBudget()); adopt(h, p1.get(), *mix); chains::ChainsNode* n = h.node(3);
      std::unique_ptr<ChainsPlan> p2(planOf(h, pa));   // a swap holds two patches at once: over (the same patch: one set fits, two do not)
      CHECK("the swap counts the second set: over budget, refused whole", p2 && p2->overBudget() && p2->noop() && p2->nodeMask() == (1u << 3) && h.stats().overBudget == 1);
      adopt(h, p2.get(), *mix); CHECK("the playing chains stay", h.node(3) == n && p2->retiredPatches() == 0);
      std::unique_ptr<ChainsPlan> p3(planOf(h, pa)); CHECK("a refused plan leaves nothing in flight", p3 != nullptr && h.stats().busyRefused == 0); h.settle(p3.get()); }
    { ChainsHost h(kSr, one - 1); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); CHECK("below one node: refused, no node", p1 && p1->overBudget() && p1->nodeMask() == 0 && h.stats().overBudget == 1); adopt(h, p1.get(), *mix); CHECK("no hook, no bus", h.node(3) == nullptr && mix->busMask() == 0); }
  }

  // 9. BYP 0Y on every track: 32 tracks x (15 switches + 15 restores) = 960 events for one row, all kept and applied
  {
    std::string mods, cabs; bool on32[kSongNT];
    for (int t = 0; t < kSongNT; t++) { on32[t] = true; const std::string n = std::to_string(t + 1), i = "i" + n, o = "o" + n;
      mods += std::string(t ? "," : "") + "{\"id\":\"" + i + "\",\"type\":\"trackIn\",\"params\":{\"track\":" + n + ",\"pre\":0}},{\"id\":\"" + o + "\",\"type\":\"chainOut\",\"params\":{\"track\":" + n + ",\"level\":1}}";
      cabs += std::string(t ? "," : "") + "{\"from\":[\"" + i + "\",\"out\"],\"to\":[\"" + o + "\",\"in\"]},{\"from\":[\"" + i + "\",\"outR\"],\"to\":[\"" + o + "\",\"inR\"]}"; }
    Doc pd(std::string(kHead) + "\"modules\":[" + mods + "],\"cables\":[" + cabs + "]}");
    ChainsHost h(kSr); auto mix = mkMixer(); ChainsSongTransport tp; std::unique_ptr<ChainsPlan> p(h.plan(pd.r, on32, 0xffffffffu, tp)); CHECK("32 tracks plan", p && p->nodes() == kSongNT); adopt(h, p.get(), *mix);
    int sent = 0; for (int t = 0; t < kSongNT; t++) for (int sw = 1; sw <= 15; sw++) { SeqEvent e; e.type = SEV_CHC; e.track = t; e.p = sw; e.v = 1; e.t = 10 * 128 / kSr - 1e-9; sent += h.event(e) ? 1 : 0; }
    for (int t = 0; t < kSongNT; t++) for (int sw = 1; sw <= 15; sw++) { SeqEvent e; e.type = SEV_CHC; e.track = t; e.p = sw; e.v = -1; e.t = 20 * 128 / kSr - 1e-9; sent += h.event(e) ? 1 : 0; }
    CHECK("960 events kept, none dropped", sent == 960 && h.stats().droppedEvents == 0 && h.pendingEvents() == 960);
    bool allOn = true, allRestored = true;
    h.beginBlock(10 * 128); for (int t = 0; t < kSongNT; t++) for (int sw = 1; sw <= 15; sw++) allOn = allOn && h.node(t)->engine().bypassSwitchState(sw) == 1;
    h.beginBlock(19 * 128); CHECK("nothing restored early", h.pendingEvents() == 480);
    h.beginBlock(20 * 128); for (int t = 0; t < kSongNT; t++) for (int sw = 1; sw <= 15; sw++) allRestored = allRestored && h.node(t)->engine().bypassSwitchState(sw) == -1;
    CHECK("every switch of every track applied", allOn); CHECK("every restore applied", allRestored && h.stats().eventsApplied == 960 && h.pendingEvents() == 0);
  }

  // 10. the song transport with no node, and a new plan for a changed song
  {
    Doc pa(patchA()); ChainsHost h(kSr); auto mix = mkMixer(); ChainsSongTransport s1; s1.tempo = 131; s1.lpb = 8; s1.ticks = 6;
    std::unique_ptr<ChainsPlan> p0(h.plan(pa.r, chainOn3, 0, s1)); adopt(h, p0.get(), *mix); CHECK("no node", h.node(3) == nullptr && h.transport().tempo == 131);
    SeqEvent t; t.type = SEV_TEMPO; t.v = 90; t.tks = 5; t.t = 0.5; CHECK("a tempo with no node is applied to the host's transport at once, not queued", h.event(t) && h.pendingEvents() == 0 && h.transport().tempo == 90 && h.transport().ticks == 5);
    std::unique_ptr<ChainsPlan> p1(h.plan(pa.r, chainOn3, 0xffffffffu, s1)); adopt(h, p1.get(), *mix);
    CHECK("a node made later starts from the current tempo (the song has not changed)", h.node(3) && h.node(3)->engine().transport().tempo == 90 && h.node(3)->engine().transport().ticks == 5 && h.node(3)->engine().transport().lpb == 8);
    ChainsSongTransport s2; s2.tempo = 100; s2.lpb = 4; s2.ticks = 6;
    std::unique_ptr<ChainsPlan> p2(h.plan(pa.r, chainOn3, 0xffffffffu, s2)); adopt(h, p2.get(), *mix);
    CHECK("a plan for a changed song takes its transport, nodes included", h.transport().tempo == 100 && h.transport().lpb == 4 && h.node(3)->engine().transport().tempo == 100 && h.node(3)->engine().transport().lpb == 4);
    std::unique_ptr<ChainsPlan> p3(h.plan(pa.r, chainOn3, 0xffffffffu, s2)); adopt(h, p3.get(), *mix);   // the same song again: the transport is kept
    SeqEvent t2 = t; t2.v = 70; t2.t = 0.001; h.event(t2); h.beginBlock(1 << 20); std::unique_ptr<ChainsPlan> p4(h.plan(pa.r, chainOn3, 0xffffffffu, s2)); adopt(h, p4.get(), *mix);
    CHECK("the same song again keeps the events' tempo", h.transport().tempo == 70 && h.node(3)->engine().transport().tempo == 70);
  }

#ifdef ALLOC_AUDIT
  // 11. bad_alloc: the n-th allocation of plan() fails, for every n: null (or a plan), nothing leaked, the host as it was, the next plan works
  std::printf("chains_host: bad_alloc sweep (every failing allocation of plan())\n"); std::fflush(stdout);   // (stdout's buffer is made here, not inside the count)
  { try { throw std::bad_alloc(); } catch (const std::bad_alloc&) {}   // the first exception of a process makes the unwinder's caches (never freed): before the count, not inside it
    Doc pw(patchA()); ChainsHost hw(kSr); for (long k = 1; k <= 3; k++) { g_failN = 0; g_failAt = k; ChainsPlan* w = planOf(hw, pw); g_failAt = 0; if (w) { hw.settle(w); delete w; } } }
  const long liveAll = g_live;
  {
    Doc pa(patchA()), pb(patchB()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> w(planOf(h, pa)); adopt(h, w.get(), *mix); w.reset();
    { std::unique_ptr<ChainsPlan> w2(planOf(h, pb)); adopt(h, w2.get(), *mix); }   // (first calls and statics made)
    chains::ChainsNode* n = h.node(3); int nulls = 0, plans = 0; bool leak = false, view = true;
    for (long k = 1; k <= 400; k++) {
      const long live0 = g_live; g_failN = 0; g_failAt = k; ChainsPlan* p = planOf(h, pa); g_failAt = 0;
      if (!p) nulls++; else { plans++; h.settle(p); delete p; }
      if (p && g_live != live0) { static int shown = 0; if (shown++ < 12) { char b[96]; const int m = std::snprintf(b, sizeof b, "  k=%ld: plan kept, refused=%d, live delta %+ld\n", k, p ? 1 : 0, g_live - live0); if (m > 0 && write(2, b, (size_t)m)) {} } }   // (diagnostic)
      if (!p && g_live != live0) { leak = true; std::printf("  leak at k=%ld: %ld\n", k, g_live - live0); break; }
      std::unique_ptr<ChainsPlan> q(planOf(h, pa, 0xffffffffu)); view = view && q && q->nodeMask() == (1u << 3); h.settle(q.get());   // the host is usable and its view is unchanged
      if (!p && q) { /* (counted above) */ }
    }
    CHECK("bad_alloc in plan(): no leak on any failing allocation", !leak); CHECK("some allocations failed the plan (null)", nulls > 0); CHECK("the host's view is unchanged and the next plan works", view && h.node(3) == n);
    std::snprintf(g_msg, sizeof g_msg, "  %d null plans, %d plans\n", nulls, plans);
  }
  if (g_live != liveAll) { char b[80]; const int m = std::snprintf(b, sizeof b, "  live allocations after the sweep: %+ld\n", g_live - liveAll); if (m > 0 && write(2, b, (size_t)m)) {} }
  CHECK("the sweep, the host and the plans destroyed: every allocation given back", g_live == liveAll); std::printf("%s", g_msg);
  {   // the host's own allocation (the event array) failing: a host that is not valid() refuses plans and events, counted, and gives everything back
    Doc pa(patchA()); const long live0 = g_live; int bad = 0; bool clean = true;
    for (long k = 1; k <= 4; k++) {
      g_failN = 0; g_failAt = k; std::unique_ptr<ChainsHost> hp(new (std::nothrow) ChainsHost(kSr)); g_failAt = 0;
      if (hp && !hp->valid()) { bad++; SeqEvent e; e.type = SEV_TEMPO; e.v = 100; e.t = 1; clean = clean && planOf(*hp, pa) == nullptr && hp->stats().refused == 1 && !hp->event(e) && hp->stats().droppedEvents == 1; hp->beginBlock(0); }
    }
    CHECK("a host whose event array failed refuses plans and events cleanly", bad > 0 && clean);
  }
  CHECK("... and the failing hosts gave everything back", g_live == liveAll);

  // 12. the allocation audit: hook, commit and the event apply (the plan, with everything that allocates, is made outside)
  {
    Doc pa(patchA()), pb(patchB()); ChainsHost h(kSr); auto mix = mkMixer(); std::unique_ptr<ChainsPlan> p1(planOf(h, pa)); adopt(h, p1.get(), *mix);
    std::unique_ptr<ChainsPlan> p2(planOf(h, pb)), p3;
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
      h.beginBlock((int64_t)q * 128);
      ChainsHost::hook(&h, 3, L.data(), R.data(), 128, q * 128 / kSr, fd, 2);
      g_on = 0; total += g_cnt;
      if (q == 20) { h.settle(p2.get()); p3.reset(planOf(h, pa)); }
      if (q == 40) h.settle(p3.get());
    }
    CHECK("hook, commit (two swaps), beginBlock and the event apply: 0 allocator calls", total == 0);
    if (total) std::printf("  allocator calls: %lu\n", total);
    CHECK("the audit ran the events", h.stats().eventsApplied == 6);
  }
#else
  std::printf("chains_host: the allocation audit and the bad_alloc sweep are skipped (need glibc without a sanitizer)\n");
#endif
  std::printf(g_fail ? "chains_host FAILED (%d)\n" : "chains_host OK\n", g_fail);
  return g_fail ? 1 : 0;
}
