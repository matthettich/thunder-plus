// U37 P1 (ctest `live_pads_epoch`): the pads-and-chop snapshot with deferred free (render/pads_snap.h; prewire F2 + F3; docs/port/U37-P1-NOTES.md).
//   E1  a long sample sounds, its pad is replaced (published, adopted): the old buffer is NOT freed while the voice lives (it is still read: the output is the old pad's), IS freed after the voice ended and
//       collect() ran; an unchanged pad is shared (same pointer in both snapshots) and never freed; setPad with the same data changes nothing
//   E2  a voice BOOKED to start later (not sounding yet) keeps its buffer too, and plays it
//   E3  per pad: a long voice on pad 0 holds pad 0's old buffer and nothing else; pad 2's retired buffers go at once
//   E4  a SoundFont voice keeps its pad's SoundFont data (font + decoded PCM) alive the same way
//   E5  two real threads: the control thread replaces the same pad 1000 times (publish + collect) while the audio thread plays notes on it and on a shared pad; no sample ever comes from a freed buffer (a freed
//       buffer is poisoned with 1000.0 in the free hook; run under ASan / TSan for the real check), nothing leaks (every buffer is freed in the end)
//   E6  a chop change (hits edited) published and adopted between steps: the Seq's events from that step on equal those of a Seq built fresh with the new table; before it, those of the old table
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "render/pads_snap.h"
#include "render/players.h"
#include "sf/sf.h"
#include "sf/sf_live.h"
#include "song/song.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
using namespace thunder;
static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf("" __VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

// one track, one phrase of 16 rows with a note on rows 0 (48), 4 (49), 8 (50), 12 (51) on instrument 0 (as test_seq_chop's handSong)
static std::string handSong(const std::string& inst, int bpm = 120) {
  std::string s = "{\"v\":6,\"bpm\":" + std::to_string(bpm) + ",\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[16],\"insts\":[" + inst + "],\"phrases\":[[";
  for (int y = 0; y < 32; y++) { s += y ? "," : ""; const int n = y == 0 ? 48 : y == 4 ? 49 : y == 8 ? 50 : y == 12 ? 51 : -1; s += "[" + std::to_string(n) + ",-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"; }
  return s + "]]}";
}

// ---- the free log: every buffer the host frees, in order; the freed audio is poisoned (a read of it shows up as a huge sample)
struct FreeLog { std::vector<std::pair<int, uint64_t>> v; int count(int slot) const { int n = 0; for (auto& p : v) n += p.first == slot; return n; } bool has(uint64_t ep) const { for (auto& p : v) if (p.second == ep) return true; return false; } };
static void onFree(void* user, const PadBuf* b) {
  FreeLog* l = (FreeLog*)user; l->v.push_back({b->slot, b->epoch});
  if (b->mem) { float* m = b->mem; for (size_t i = 0; i < b->bytes() / sizeof(float); i++) m[i] = 1000.0f; }
  if (b->varMem) { size_t n = 0; for (int i = 0; i < b->nVars; i++) n += (size_t)b->vars[i].frames; for (size_t i = 0; i < n; i++) b->varMem[i] = 1000.0f; }
}

struct Rig {
  Song song; SeqVars vars; std::unique_ptr<Seq> seq; PadsHost* host = nullptr; std::unique_ptr<Players> P; FreeLog log;
  float store[kVoiceTracks][128]; float* ptr[kVoiceTracks]; int64_t frame = 0; bool ok = false;
  explicit Rig(const std::string& text = handSong("{\"pad\":0,\"slc\":0,\"sync\":0,\"mo\":0}")) {
    JsonError je; if (!song.load(text.data(), text.size(), &je)) { std::printf("song does not load\n"); return; }
    SeqOpts so; so.once = true; seq.reset(new Seq(song, vars, SeqMode::Song, so));
    host = PadsHost::create(); if (!host) return;
    host->setFreeHook(&onFree, &log);
    P.reset(new Players(nullptr, 48000, 64));   // no pads until the first snapshot is adopted (every note is then "no pad")
    for (int i = 0; i < kVoiceTracks; i++) ptr[i] = store[i];
    ok = true;
  }
  ~Rig() { P.reset(); seq.reset(); delete host; }
  // one 128-frame quantum, as the host runs it: beginQuantum first, then the voices
  bool quantum() {
    const bool adopted = host->beginQuantum(*P, seq.get());
    const double t0 = (double)frame / 48000; P->setTime(t0, frame);
    std::memset(store, 0, sizeof store); P->render(ptr, ptr, 128, t0, frame); frame += 128; return adopted;
  }
  void quanta(int n) { for (int i = 0; i < n; i++) quantum(); }
  double now() const { return (double)frame / 48000; }
  static SeqEvent note(int track, int pad, double t, bool poly) { SeqEvent e; e.type = SEV_NOTE; e.track = track; e.pad = pad; e.ii = 0; e.t = t; e.poly = poly; e.rate = 1; e.gain = 1; e.pan = 128; e.vel = 1; return e; }
  void go(int track, int pad, double t, bool poly = true) { P->dispatch(note(track, pad, t, poly), *seq, song); }
  double out(int track) const { return store[track][127]; }
  bool endVoices(int maxQ = 4000) { P->stopAll(now()); for (int i = 0; i < maxQ && P->voices() > 0; i++) quantum(); return P->voices() == 0; }
  int set(int pad, float v, int frames, double sr = 48000) { std::vector<float> d((size_t)frames, v); return host->setPad(pad, d.data(), nullptr, frames, sr); }
  void post() { CHECK(host->publish() == PadsHost::kOk, "publish"); }
};
static const double kPan = 1.41421356237309505;   // what a mono pad at gain 1, centre pan, adds to its track (measured: 0.1 -> 0.141421)
static bool near(double a, double b) { return std::fabs(a - b) < 1e-5; }

static void e1() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  const int SEC4 = 48000 * 4;
  CHECK(r.set(0, 0.1f, SEC4) == 0 && r.set(1, 0.3f, SEC4) == 0 && r.set(2, 0.5f, 4800) == 0, "setPad");
  r.post(); CHECK(r.quantum(), "the first snapshot is adopted");
  const float* p1 = r.host->current()->pads.buf[1].data; const uint64_t ep0 = r.host->current()->pads.buf[0].epoch, ep1 = r.host->current()->pads.buf[1].epoch;
  CHECK(p1 && ep0 > 0 && ep1 > 0 && r.host->current()->pads.buf[0].slot == 0, "the snapshot's descriptors carry epoch and slot");
  r.go(0, 0, r.now()); r.go(1, 1, r.now()); r.quanta(3);
  CHECK(near(r.out(0), 0.1 * kPan) && near(r.out(1), 0.3 * kPan), "both voices sound: %g %g", r.out(0), r.out(1));
  // the same data again: nothing changes (no new buffer)
  { const uint64_t made = r.host->made(); CHECK(r.set(1, 0.3f, SEC4) == 0 && r.host->made() == made && r.host->padBuf(1) && r.host->padBuf(1)->epoch == ep1, "setPad with the same data changes nothing"); }
  // replace pad 0 (a voice is on it), leave pad 1 alone
  CHECK(r.set(0, 0.2f, SEC4) == 0, "replace pad 0"); r.post(); CHECK(r.quantum(), "the new snapshot is adopted");
  CHECK(r.host->current()->pads.buf[1].data == p1 && r.host->current()->pads.buf[1].epoch == ep1, "the unchanged pad is SHARED between the two snapshots (same pointer)");
  CHECK(r.host->current()->pads.buf[0].epoch > ep0, "the replaced pad has a newer epoch");
  r.host->collect(); r.host->collect();
  CHECK(!r.log.has(ep0) && r.host->retired() == 1, "the old buffer is retired, NOT freed, while its voice lives (retired %zu, freed %zu)", r.host->retired(), r.log.v.size());
  r.quanta(5); r.host->collect();
  CHECK(near(r.out(0), 0.1 * kPan), "the voice still reads the OLD buffer after the replacement: %g", r.out(0));
  r.go(2, 0, r.now()); r.quanta(2);
  CHECK(near(r.out(2), 0.2 * kPan), "a new note takes the NEW buffer: %g", r.out(2));
  r.host->collect(); CHECK(!r.log.has(ep0) && r.log.count(1) == 0, "still held; pad 1 never freed");
  // the voices end: the old buffer goes, and only it
  CHECK(r.endVoices(), "voices ended"); r.quantum(); const size_t n = r.host->collect();
  CHECK(r.log.has(ep0) && n == 1 && r.host->retired() == 0, "freed after the voice ended and collect() ran (%zu freed)", n);
  CHECK(!r.log.has(ep1) && r.log.count(1) == 0 && r.host->current()->pads.buf[1].data == p1, "the shared pad is never freed");
}

static void e2() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  CHECK(r.set(3, 0.4f, 24000) == 0, "setPad 0.5 s"); r.post(); r.quantum();
  const uint64_t ep = r.host->current()->pads.buf[3].epoch;
  r.go(0, 3, r.now() + 0.3);   // booked: it starts 0.3 s from now
  CHECK(r.P->voices() == 1, "a booked voice is in the pool");
  CHECK(r.set(3, 0.8f, 24000) == 0, "replace"); r.post(); r.quantum(); r.host->collect();
  CHECK(!r.log.has(ep) && r.host->retired() == 1, "a booked (not yet sounding) voice keeps its buffer");
  r.quanta(10); r.host->collect(); CHECK(!r.log.has(ep) && near(r.out(0), 0), "still booked, silent: %g", r.out(0));
  int q = 0; while (r.now() < 0.35 && q++ < 1000) r.quantum();
  CHECK(near(r.out(0), 0.4 * kPan), "it then plays the OLD buffer: %g", r.out(0)); CHECK(!r.log.has(ep), "not freed while it plays");
  for (int i = 0; i < 300 && r.P->voices() > 0; i++) r.quantum();
  CHECK(r.P->voices() == 0, "the voice ended by itself"); r.quantum(); r.host->collect(); CHECK(r.log.has(ep) && r.host->retired() == 0, "freed once it ended");
}

static void e3() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  CHECK(r.set(0, 0.1f, 48000 * 4) == 0 && r.set(2, 0.5f, 4800) == 0, "setPad"); r.post(); r.quantum();
  const uint64_t ep0 = r.host->current()->pads.buf[0].epoch;
  r.go(0, 0, r.now()); r.quanta(2);
  CHECK(r.set(0, 0.2f, 48000 * 4) == 0, "replace pad 0"); r.post(); r.quantum(); r.host->collect();
  for (int i = 0; i < 5; i++) { CHECK(r.set(2, (float)(0.6 + 0.01 * i), 4800) == 0, "replace pad 2"); r.post(); r.quantum(); r.host->collect(); }
  CHECK(!r.log.has(ep0), "pad 0's old buffer is held by its voice");
  CHECK(r.log.count(2) == 5 && r.log.count(0) == 0, "every retired buffer of pad 2 is freed at once, though they are NEWER than the one held (freed: pad 2 %d, pad 0 %d)", r.log.count(2), r.log.count(0));
  CHECK(r.host->retired() == 1, "only pad 0's old buffer waits (%zu)", r.host->retired());
}

static std::vector<uint8_t> readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }
static void e4() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  std::vector<uint8_t> gm = readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3");
  std::shared_ptr<sf::Font> font(new sf::Font());
  if (gm.empty() || sf::parse(gm.data(), gm.size(), *font) != sf::kParseOk) { CHECK(false, "font does not load"); return; }
  const sf::Preset* pr = sf::sf_preset(*font, 0, 19); uint8_t flags[128] = {}; flags[60] = 1; sf::DecodeOpts dop;
  auto prepare = [&]() { std::shared_ptr<sf::PresetPcm> pcm(new sf::PresetPcm()); return sf::prepare_preset(*font, pr, dop, *pcm, flags) == sf::kPrepOk ? pcm : nullptr; };
  auto pcm1 = prepare(), pcm2 = prepare(); if (!pr || !pcm1 || !pcm2) { CHECK(false, "preset does not prepare"); return; }
  sf::PadParams P; P.bank = 0; P.prog = 19;
  CHECK(r.host->setSf(0, font, pr, pcm1, P) == 0 && r.host->setRoute(0, PadRoute::SoundFont) == 0, "setSf");
  r.post(); r.quantum();
  const uint64_t ep = r.host->current()->pads.sf[0].epoch; CHECK(ep > 0 && r.host->current()->pads.sf[0].slot == kRenderPads, "the snapshot's SfPad carries epoch and slot");
  r.go(0, 0, r.now()); r.quanta(4); CHECK(r.P->sfPool().active() == 1, "an SF voice sounds");
  CHECK(r.host->setSf(0, font, pr, pcm2, P) == 0, "replace the SoundFont data"); pcm1.reset();   // (the host's buffer is now the only holder of the old PCM)
  r.post(); r.quantum(); r.host->collect(); r.host->collect();
  CHECK(!r.log.has(ep) && r.host->retired() == 1, "the old SoundFont data is retired, not freed, while its voice lives");
  r.quanta(40); r.host->collect(); CHECK(!r.log.has(ep), "still held");
  CHECK(r.endVoices(), "ended"); r.quantum(); r.host->collect(); CHECK(r.log.has(ep) && r.host->retired() == 0, "freed after the voice ended");
  r.P->stopAll(r.now());
}

static void e5() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  const int N = 1000, LEN = 4800;
  CHECK(r.set(1, 0.3f, 48000 * 2) == 0 && r.set(0, 0.1f, LEN) == 0, "setPad"); r.post();
  std::atomic<bool> stop{false}; std::atomic<int64_t> quanta{0}; std::atomic<int> bad{0}, notes{0};
  std::thread audio([&] {
    uint32_t rng = 7; int q = 0;
    while (!stop.load(std::memory_order_acquire)) {
      r.host->beginQuantum(*r.P, r.seq.get());
      const double t0 = (double)r.frame / 48000; r.P->setTime(t0, r.frame);
      if (q % 3 == 0) { rng = rng * 1664525u + 1013904223u; r.go((int)((rng >> 16) % 8), 0, t0 + (double)((rng >> 8) % 20) * 0.001); notes++; }
      if (q % 57 == 0) r.go(8 + (q / 57) % 4, 1, t0);   // a long sample on the shared pad
      std::memset(r.store, 0, sizeof r.store); r.P->render(r.ptr, r.ptr, 128, t0, r.frame); r.frame += 128;
      for (int t = 0; t < kVoiceTracks; t++) for (int i = 0; i < 128; i++) { const float x = r.store[t][i]; if (!(x >= 0 && x < 3.0f)) bad++; }
      quanta++; q++;
    }
  });
  for (int i = 0; i < N; i++) {
    CHECK(r.set(0, 0.1f + 0.004f * (float)(i % 50), LEN) == 0, "set %d", i);
    int tries = 0; int rc; while ((rc = r.host->publish()) == PadsHost::kFull && tries++ < 100000) { r.host->collect(); std::this_thread::yield(); }
    CHECK(rc == PadsHost::kOk, "publish %d", i); r.host->collect();
  }
  const int64_t q0 = quanta.load(); while (quanta.load() < q0 + 400) std::this_thread::yield();   // let the last voices play on the last snapshot
  stop.store(true, std::memory_order_release); audio.join();
  CHECK(bad.load() == 0, "%d samples out of range: a voice read a freed (poisoned) buffer", bad.load());
  CHECK(notes.load() > 100, "the audio thread played %d notes", notes.load());
  CHECK(r.endVoices(), "voices ended"); r.quanta(2); r.host->collect();
  CHECK(r.host->retired() == 0 && r.host->buffers() == 2, "no leak: every retired buffer was freed (%zu alive, %zu retired, made %llu, freed %llu)", r.host->buffers(), r.host->retired(), (unsigned long long)r.host->made(), (unsigned long long)r.host->freed());
  CHECK(r.host->made() >= (uint64_t)N, "made %llu", (unsigned long long)r.host->made());
  std::printf("  e5: %d replacements, %d notes, %lld quanta, made %llu freed %llu\n", N, notes.load(), (long long)quanta.load(), (unsigned long long)r.host->made(), (unsigned long long)r.host->freed());
  const uint64_t made = r.host->made(); FreeLog* lg = &r.log; r.P.reset(); delete r.host; r.host = nullptr;
  CHECK((uint64_t)lg->v.size() == made, "every buffer ever made was freed, at the latest at teardown (%zu of %llu)", lg->v.size(), (unsigned long long)made);
}

struct Ev { int step; SeqEvent e; };
static bool sameEv(const SeqEvent& a, const SeqEvent& b) {
  return a.type == b.type && a.track == b.track && a.t == b.t && a.pad == b.pad && a.rate == b.rate && a.ofsSec == b.ofsSec && a.durSec == b.durSec && a.cut == b.cut && a.has == b.has && a.gain == b.gain && a.mn == b.mn;
}
// steps a Seq to its end; `hook(step)` runs before every step (the host's top of a quantum)
template <class H> static std::vector<Ev> run(Seq& sq, H hook) {
  std::vector<Ev> out; std::vector<SeqEvent> evs(4096); SeqStep st;
  for (int s = 0; s < 64 && sq.alive(); s++) { hook(s); const int k = sq.step(evs.data(), 4096, &st); for (int i = 0; i < k; i++) out.push_back({s, evs[(size_t)i]}); }
  return out;
}
static void e6() {
  const std::string text = handSong("{\"pad\":0,\"slc\":4,\"smode\":1}");
  Song song; JsonError je; if (!song.load(text.data(), text.size(), &je)) { CHECK(false, "song"); return; }
  const double h1[4] = {0, 0.5, 1.0, 1.5}, h2[4] = {0, 0.1, 0.25, 0.3};
  SeqChop c1, c2; c1.setLoop(0, 2.0); c1.setHits(0, 4, h1, 4); c2.setLoop(0, 2.0); c2.setHits(0, 4, h2, 4);
  SeqVars vars;
  auto fresh = [&](const SeqChop* c) { SeqOpts o; o.once = true; o.chop = c; Seq sq(song, vars, SeqMode::Song, o); return run(sq, [](int) {}); };
  const std::vector<Ev> f1 = fresh(&c1), f2 = fresh(&c2);
  int K = -1, nn = 0; for (const Ev& e : f1) if (e.e.type == SEV_NOTE && ++nn == 3) K = e.step;   // the step of the third note: the edit lands just before it
  CHECK(K > 0 && f1.size() == f2.size(), "the fresh runs: K %d, %zu / %zu events", K, f1.size(), f2.size());
  int differ = 0; for (size_t i = 0; i < f1.size() && i < f2.size(); i++) if (!sameEv(f1[i].e, f2[i].e)) differ++;
  CHECK(differ >= 2, "the two tables make different notes (%d differ): the test is not vacuous", differ);
  // the live path: a host, a Players, a Seq made with NO table (as a session's opts would carry none)
  PadsHost* host = PadsHost::create(); std::unique_ptr<Players> P(new Players(nullptr, 48000, 8));
  std::vector<float> pad(96000, 0.1f);
  CHECK(host->setPad(0, pad.data(), nullptr, 96000, 48000) == 0, "setPad");
  host->chop().setLoop(0, 2.0); host->chop().setHits(0, 4, h1, 4); CHECK(host->publish() == 0, "publish 1");
  SeqOpts o; o.once = true; o.chop = nullptr; Seq A(song, vars, SeqMode::Song, o);
  const std::vector<Ev> live = run(A, [&](int s) {
    if (s == K) { host->chop().setHits(0, 4, h2, 4); CHECK(host->publish() == 0, "publish 2"); }   // control side: edit + publish (no new pad buffer)
    host->beginQuantum(*P, &A);                                                                      // audio side: top of the quantum, before the step
    host->collect();
  });
  CHECK(live.size() == f1.size(), "event counts %zu / %zu", live.size(), f1.size());
  int bad = 0, nBefore = 0, nAfter = 0;
  for (size_t i = 0; i < live.size() && i < f1.size(); i++) {
    const bool after = live[i].step >= K; const Ev& want = after ? f2[i] : f1[i];
    if (live[i].step != want.step || !sameEv(live[i].e, want.e)) bad++;
    if (live[i].e.type == SEV_NOTE) (after ? nAfter : nBefore)++;
  }
  CHECK(bad == 0 && nBefore == 2 && nAfter == 2, "events before step %d equal the old table's, from it on a fresh Seq with the new table's (%d differ; notes %d + %d)", K, bad, nBefore, nAfter);
  CHECK(A.chopped() == 4 && A.chopSkipped() == 0, "all four notes were chopped (%llu, skipped %llu)", (unsigned long long)A.chopped(), (unsigned long long)A.chopSkipped());
  P.reset(); delete host;
}

int main() {
  e1(); e2(); e3(); e4(); e5(); e6();
  std::printf("live_pads_epoch: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
