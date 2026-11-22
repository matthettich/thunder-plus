// U37 P1 (ctest `live_pads_epoch`): the pads-and-chop snapshot with deferred free (render/pads_snap.h; prewire F2 + F3; docs/port/U37-P1-NOTES.md).
//   E1  a long sample sounds, its pad is replaced (published, adopted): the old buffer is NOT freed while the voice lives (it is still read: the output is the old pad's), IS freed after the voice ended and
//       collect() ran; an unchanged pad is shared (same pointer in both snapshots) and never freed; setPad with the same data changes nothing
//   E2  a voice BOOKED to start later (not sounding yet) keeps its buffer too, and plays it
//   E3  per pad: a long voice on pad 0 holds pad 0's old buffer and nothing else; pad 2's retired buffers go at once
//   E4  a SoundFont voice keeps its pad's SoundFont data (font + decoded PCM) alive the same way
//   E5  two real threads: the control thread replaces the same pad 1000 times (publish + collect) while the audio thread plays notes on it and on a shared pad; no sample ever comes from a freed buffer (a freed
//       buffer is poisoned with 1000.0 in the free hook; run under ASan / TSan for the real check), nothing leaks (every buffer is freed in the end)
//   E0  no pads yet: a note is counted as no-pad (no null dereference); create() publishes an empty snapshot the first quantum adopts
//   E7  MUT variations (a variation voice keeps its pad's buffer; a variation change is a change) and a stereo pad
//   E8  the order inside beginQuantum, deterministically: a test seam runs publish + collect between its stages (adopt above the scan would free a buffer a voice is on)
//   E6  a chop change (hits edited) published and adopted between steps: the Seq's events from that step on equal those of a Seq built fresh with the new table; before it, those of the old table
#include <atomic>
#include <csignal>
#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif
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

// ---- the free log: every buffer the host frees, in order, tagged by its frame count (the tests give each pad its own length). A freed buffer is poisoned with 1000.0 and QUARANTINED (the hook takes the memory from
// the host, which then frees nothing): a voice that still reads it hears 1000.0, whatever the allocator would have done with the chunk.
struct FreeLog {
  std::vector<std::pair<int, uint64_t>> v; std::vector<float*> keep;
  int count(int frames) const { int n = 0; for (auto& p : v) n += p.first == frames; return n; }
  bool has(uint64_t ep) const { for (auto& p : v) if (p.second == ep) return true; return false; }
  ~FreeLog() { for (float* p : keep) delete[] p; }
};
static void onFree(void* user, PadBuf* b) {
  FreeLog* l = (FreeLog*)user; l->v.push_back({b->frames, b->epoch});
  if (b->mem) { const size_t n = b->bytes() / sizeof(float); for (size_t i = 0; i < n; i++) b->mem[i] = 1000.0f; l->keep.push_back(b->mem); b->mem = nullptr; }
  if (b->varMem) { size_t n = 0; for (int i = 0; i < b->nVars; i++) n += (size_t)b->vars[i].frames; for (size_t i = 0; i < n; i++) b->varMem[i] = 1000.0f; l->keep.push_back(b->varMem); b->varMem = nullptr; }
}

struct Rig {
  Song song; SeqVars vars; std::unique_ptr<Seq> seq; PadsHost* host = nullptr; std::unique_ptr<Players> P; FreeLog log;   // (log is declared before host's use and outlives it)
  float store[kVoiceTracks][128], storeR[kVoiceTracks][128]; float* ptr[kVoiceTracks]; float* ptrR[kVoiceTracks]; int64_t frame = 0; bool ok = false, stereoOut = false;
  explicit Rig(const std::string& text = handSong("{\"pad\":0,\"slc\":0,\"sync\":0,\"mo\":0}")) {
    JsonError je; if (!song.load(text.data(), text.size(), &je)) { std::printf("song does not load\n"); return; }
    SeqOpts so; so.once = true; seq.reset(new Seq(song, vars, SeqMode::Song, so));
    P.reset(new Players(nullptr, 48000, 64));   // no pads until the first snapshot is adopted: a note is then counted as no-pad (E0)
    host = PadsHost::create(*P); if (!host) return;
    host->setFreeHook(&onFree, &log);
    for (int i = 0; i < kVoiceTracks; i++) { ptr[i] = store[i]; ptrR[i] = storeR[i]; }
    ok = true;
  }
  ~Rig() { delete host; host = nullptr; P.reset(); seq.reset(); }
  // one 128-frame quantum, as the host runs it: beginQuantum first, then the voices
  bool quantum() {
    const bool adopted = host->beginQuantum(*P, seq.get());
    const double t0 = (double)frame / 48000; P->setTime(t0, frame);
    std::memset(store, 0, sizeof store); std::memset(storeR, 0, sizeof storeR); P->render(ptr, stereoOut ? ptrR : ptr, 128, t0, frame); frame += 128; return adopted;
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
  const int SEC4 = 192000, L1 = 190000;   // each pad has its own length: the free log tells them apart
  CHECK(r.set(0, 0.1f, SEC4) == 0 && r.set(1, 0.3f, L1) == 0 && r.set(2, 0.5f, 4800) == 0, "setPad");
  r.post(); CHECK(r.quantum(), "the first snapshot is adopted");
  const float* p1 = r.host->current()->pads.buf[1].data; const uint64_t ep0 = r.host->current()->pads.buf[0].epoch, ep1 = r.host->current()->pads.buf[1].epoch;
  CHECK(p1 && ep0 > 0 && ep1 > 0 && ep1 != ep0, "the snapshot's descriptors carry their epochs");
  r.go(0, 0, r.now()); r.go(1, 1, r.now()); r.quanta(3);
  CHECK(near(r.out(0), 0.1 * kPan) && near(r.out(1), 0.3 * kPan), "both voices sound: %g %g", r.out(0), r.out(1));
  // the same data again: nothing changes (no new buffer)
  { const uint64_t made = r.host->made(); CHECK(r.set(1, 0.3f, L1) == 0 && r.host->made() == made && r.host->padBuf(1) && r.host->padBuf(1)->epoch == ep1, "setPad with the same data changes nothing"); }
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
  r.host->collect(); CHECK(!r.log.has(ep0) && r.log.count(L1) == 0, "still held; pad 1 never freed");
  // the voices end: the old buffer goes, and only it
  CHECK(r.endVoices(), "voices ended"); r.quantum(); const size_t n = r.host->collect();
  CHECK(r.log.has(ep0) && n == 1 && r.host->retired() == 0, "freed after the voice ended and collect() ran (%zu freed)", n);
  CHECK(!r.log.has(ep1) && r.log.count(L1) == 0 && r.host->current()->pads.buf[1].data == p1, "the shared pad is never freed");
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
  CHECK(r.set(0, 0.1f, 192000) == 0 && r.set(2, 0.5f, 4800) == 0, "setPad"); r.post(); r.quantum();
  const uint64_t ep0 = r.host->current()->pads.buf[0].epoch;
  r.go(0, 0, r.now()); r.quanta(2);
  CHECK(r.set(0, 0.2f, 192000) == 0, "replace pad 0"); r.post(); r.quantum(); r.host->collect();
  for (int i = 0; i < 5; i++) { CHECK(r.set(2, (float)(0.6 + 0.01 * i), 4800) == 0, "replace pad 2"); r.post(); r.quantum(); r.host->collect(); }
  CHECK(!r.log.has(ep0), "pad 0's old buffer is held by its voice");
  CHECK(r.log.count(4800) == 5 && r.log.count(192000) == 0, "every retired buffer of pad 2 is freed at once, though they are NEWER than the one held (freed: pad 2 %d, pad 0 %d)", r.log.count(4800), r.log.count(192000));
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
  const uint64_t ep = r.host->current()->pads.sf[0].epoch; CHECK(ep > 0, "the snapshot's SfPad carries its epoch");
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


static void e0() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  r.go(0, 0, 0.0);
  CHECK(r.P->stats().notes == 1 && r.P->stats().notesNoPad == 1 && r.P->voices() == 0, "a note before any pads exist is counted as no-pad, nothing sounds (notes %d, noPad %d)", r.P->stats().notes, r.P->stats().notesNoPad);
  CHECK(r.host->current() == nullptr, "nothing adopted yet");
  CHECK(r.quantum() && r.host->current() != nullptr, "create() published an empty snapshot: the first quantum adopts it");
  r.go(0, 0, 0.1); CHECK(r.P->stats().notesNoPad == 2, "an empty pad: no-pad, counted");
}

static void e7() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  std::vector<float> a(48000, 0.1f), v1(48000, 0.7f), v2(48000, 0.9f), a2(48000, 0.2f), w1(48000, 0.3f);
  PadVarIn vin[2] = {{0, 'm', 1, v1.data(), 48000, 48000.0}, {0, 'g', 2, v2.data(), 48000, 48000.0}};
  CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, vin, 2) == 0, "setPad with two variations"); r.post(); r.quantum();
  const uint64_t ep = r.host->padBuf(0)->epoch;
  { // the identical-data test sees variations
    const uint64_t made = r.host->made();
    CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, vin, 2) == 0 && r.host->made() == made && r.host->padBuf(0)->epoch == ep, "the same pad and variations again: a no-op");
    std::vector<float> v1b = v1; v1b[1000] = 0.71f; PadVarIn c1[2] = {{0, 'm', 1, v1b.data(), 48000, 48000.0}, {0, 'g', 2, v2.data(), 48000, 48000.0}};
    CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, c1, 2) == 0 && r.host->padBuf(0)->epoch != ep, "one variation sample changed: a new buffer");
    CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, vin, 2) == 0 && r.host->padBuf(0)->epoch != ep, "and back: another new buffer");
    CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, vin, 1) == 0, "one variation fewer: accepted");
    CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, vin, 2) == 0, "and back");
    PadVarIn dup[2] = {vin[0], vin[0]}; CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, dup, 2) == PadsHost::kArg, "a duplicate variation is refused");
    r.post(); r.quantum(); r.host->collect(); }
  const uint64_t ep2 = r.host->current()->pads.var.empty() ? 0 : r.host->current()->pads.var[0].buf.epoch;
  CHECK(r.host->current()->pads.var.size() == 2 && ep2 == r.host->current()->pads.buf[0].epoch && ep2 > ep, "the snapshot's variations carry their pad's buffer epoch (%llu)", (unsigned long long)ep2);
  SeqEvent e = Rig::note(0, 0, r.now(), true); e.has |= SH_VK; e.vkKind = 'm'; e.vkNum = 1; r.P->dispatch(e, *r.seq, r.song);
  r.quanta(3); CHECK(near(r.out(0), 0.7 * kPan), "the MUT variation sounds: %g", r.out(0));
  CHECK(r.host->setPad(0, a2.data(), nullptr, 48000, 48000, nullptr, 0) == 0, "replace the pad (variations gone)"); r.post(); r.quantum(); r.host->collect(); r.host->collect();
  CHECK(!r.log.has(ep2), "a variation voice keeps its pad's buffer alive (its variation data is in it)");
  r.quanta(5); CHECK(near(r.out(0), 0.7 * kPan), "and keeps sounding the OLD variation: %g", r.out(0));
  CHECK(r.endVoices(), "ended"); r.quantum(); r.host->collect(); CHECK(r.log.has(ep2), "freed after it ended");
  // a stereo pad: L 0.2, R 0.6; its voice keeps the stereo buffer (both channels) through a replacement
  std::vector<float> L(48000, 0.2f), R(48000, 0.6f), L2(48000, 0.4f), R2(48000, 0.1f);
  CHECK(r.host->setPad(4, L.data(), R.data(), 48000, 48000) == 0, "stereo setPad"); r.post(); r.quantum(); r.stereoOut = true;
  const uint64_t eps = r.host->padBuf(4)->epoch; r.go(1, 4, r.now()); r.quanta(3);
  const double l0 = r.store[1][127], r0 = r.storeR[1][127]; CHECK(l0 > 0.05 && r0 > 0.05 && std::fabs(l0 - r0) > 0.01, "a stereo pad: two different channels (%g %g)", l0, r0);
  CHECK(r.host->setPad(4, L2.data(), R2.data(), 48000, 48000) == 0, "replace it"); r.post(); r.quantum(); r.host->collect(); r.host->collect(); CHECK(!r.log.has(eps), "held by its stereo voice");
  r.quanta(3); CHECK(near(r.store[1][127], l0) && near(r.storeR[1][127], r0), "both channels still read the old buffer (%g %g)", r.store[1][127], r.storeR[1][127]);
  CHECK(r.endVoices(), "ended"); r.quantum(); r.host->collect(); CHECK(r.log.has(eps), "freed after");
  // the remaining setters: a removal and a clear release their buffers
  CHECK(r.host->setPad(4, nullptr, nullptr, 0, 48000) == 0 && r.host->padBuf(4) == nullptr, "setPad(frames 0) removes");
  CHECK(r.host->setKit(4, VoiceKit()) == 0 && r.host->setRoute(4, PadRoute::Sampler) == 0 && r.host->setSfParams(4, sf::PadParams()) == 0 && r.host->clearSf(4) == 0, "the small setters");
  CHECK(r.host->setPad(0, a.data(), nullptr, 48000, 48000, vin, 5000) == PadsHost::kArg, "more variations than the C API allows: refused");
}

// E8: the order inside beginQuantum, made deterministic with the seam: at every stage of one quantum the control side runs collect()
struct SeamCtx { Rig* r; uint64_t ep; int calls = 0, freedAt = -1; };
static void seamFn(void* u, int stage) { SeamCtx* c = (SeamCtx*)u; c->calls++; c->r->host->collect(); if (c->freedAt < 0 && c->r->log.has(c->ep)) c->freedAt = stage; }
static void e8() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  CHECK(r.set(0, 0.1f, 192000) == 0, "setPad"); r.post(); r.quantum();
  const uint64_t ep0 = r.host->current()->pads.buf[0].epoch;
  r.go(0, 0, r.now());   // a note started AFTER the last scan: the board does not know it yet
  CHECK(r.set(0, 0.2f, 192000) == 0, "replace"); r.post();
  SeamCtx c; c.r = &r; c.ep = ep0; r.host->setSeam(&seamFn, &c);
  CHECK(r.quantum(), "the replacement is adopted in this quantum");
  r.host->setSeam(nullptr, nullptr);
  CHECK(c.calls == 5, "the seam ran at every stage (%d)", c.calls);
  CHECK(c.freedAt < 0 && !r.log.has(ep0), "collect() at every stage of the quantum did not free the buffer a voice started on (freed at stage %d)", c.freedAt);
  r.quanta(3); CHECK(near(r.out(0), 0.1 * kPan), "and the voice reads it: %g", r.out(0));
  r.host->collect(); CHECK(!r.log.has(ep0), "held"); CHECK(r.endVoices(), "ended"); r.quantum(); r.host->collect(); CHECK(r.log.has(ep0), "freed once it ended");
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
  std::unique_ptr<Players> P(new Players(nullptr, 48000, 8)); PadsHost* host = PadsHost::create(*P);
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
  // (b) a Seq made AFTER the adopt (as SeqSession::make does) carries a stale table in its opts and no snapshot changes at its first quantum: beginQuantum must still point it at the current table
  { SeqOpts ob; ob.once = true; ob.chop = &c1; Seq B(song, vars, SeqMode::Song, ob);
    const std::vector<Ev> lb = run(B, [&](int) { host->beginQuantum(*P, &B); });
    int badB = 0; if (lb.size() != f2.size()) badB = 1; else for (size_t i = 0; i < lb.size(); i++) if (!sameEv(lb[i].e, f2[i].e)) badB++;
    CHECK(badB == 0 && B.chopped() == 4, "a Seq made after the adopt is re-pointed at the current table at its first quantum (%d differ, %llu chopped)", badB, (unsigned long long)B.chopped()); }
  delete host; P.reset();
}


// U37 P2, verify-276 R2-2: a Seq was seen and none is passed (the host forgot seqGone(), or still holds one): a pending snapshot is NOT adopted (its chop table is the one the Seq points at) and the quantum is counted;
// once seqGone() was called, or a Seq is passed, the adopt happens.
static void e9() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  CHECK(r.set(0, 0.1f, 4800) == 0, "setPad"); r.post(); CHECK(r.host->beginQuantum(*r.P, r.seq.get()), "adopted with a Seq (the Seq is now seen)");
  const uint64_t id1 = r.host->current()->id, orphans0 = r.host->orphanAdopts();
  CHECK(r.set(0, 0.2f, 4800) == 0, "replace"); r.post();
  for (int i = 0; i < 3; i++) CHECK(!r.host->beginQuantum(*r.P, nullptr), "quantum %d with no Seq after one was seen: not adopted", i);
  CHECK(r.host->current()->id == id1 && r.host->orphanAdopts() == orphans0 + 3, "the old snapshot stays current (id %llu), the three quanta are counted (%llu)", (unsigned long long)r.host->current()->id, (unsigned long long)(r.host->orphanAdopts() - orphans0));
  CHECK(r.host->beginQuantum(*r.P, r.seq.get()) && r.host->current()->id > id1, "passed a Seq again: the snapshot is adopted");
  // seqGone(): the host dropped its Seq on purpose: adopting with a null Seq is fine and is not counted
  CHECK(r.set(0, 0.3f, 4800) == 0, "replace again"); r.post(); r.host->seqGone(); const uint64_t o2 = r.host->orphanAdopts();
  CHECK(r.host->beginQuantum(*r.P, nullptr) && r.host->orphanAdopts() == o2, "after seqGone(): adopted with no Seq, not counted");
  // a host that never had a Seq adopts as always
  Rig q; if (!q.ok) { CHECK(false, "rig"); return; } CHECK(q.set(0, 0.1f, 4800) == 0, "setPad"); q.post(); CHECK(q.host->beginQuantum(*q.P, nullptr) && q.host->orphanAdopts() == 0, "no Seq ever seen: adopted, not counted");
  r.host->collect();
}

// U37 P2: setPadTake (ownership, the finite check in place, errors leave the block with the caller, no identical-data shortcut) and the byte counter (bytesHeld includes retired buffers; bytesRetired)
static void e10() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  const int N = 4800; CHECK(r.host->bytesHeld() == 0 && r.host->bytesRetired() == 0, "empty: no bytes");
  float* blk = new float[(size_t)N]; for (int i = 0; i < N; i++) blk[i] = 0.25f;
  CHECK(r.host->setPadTake(0, blk, false, N, 48000) == 0, "take a mono block"); CHECK(r.host->bytesHeld() == (size_t)N * 4 && r.host->buffers() == 1, "the block is counted: %zu bytes", r.host->bytesHeld());
  r.post(); r.quantum(); r.go(0, 0, r.now()); r.quanta(2); CHECK(near(r.out(0), 0.25 * kPan), "the taken block sounds: %g", r.out(0));
  // the same data again: taking does not shortcut, the old buffer is retired (a voice reads it) and both count
  float* blk2 = new float[(size_t)N]; for (int i = 0; i < N; i++) blk2[i] = 0.25f;
  CHECK(r.host->setPadTake(0, blk2, false, N, 48000) == 0 && r.host->buffers() == 2 && r.host->bytesHeld() == (size_t)N * 8, "a take of identical data still makes a new buffer (%zu buffers, %zu bytes)", r.host->buffers(), r.host->bytesHeld());
  r.post(); r.quantum(); r.host->collect(); CHECK(r.host->retired() == 1 && r.host->bytesRetired() == (size_t)N * 4 && r.host->bytesHeld() == (size_t)N * 8, "retired buffers are counted until freed (%zu retired bytes, %zu held)", r.host->bytesRetired(), r.host->bytesHeld());
  CHECK(r.endVoices(), "voice ended"); r.quantum(); r.host->collect(); CHECK(r.host->retired() == 0 && r.host->bytesRetired() == 0 && r.host->bytesHeld() == (size_t)N * 4, "freed: %zu bytes held", r.host->bytesHeld());
  // errors: the block stays the caller's and nothing is staged
  float* bad = new float[(size_t)N]; for (int i = 0; i < N; i++) bad[i] = 0.1f; bad[4000] = INFINITY; const size_t before = r.host->bytesHeld(), made = r.host->made();
  CHECK(r.host->setPadTake(1, bad, false, N, 48000) != 0 && r.host->bytesHeld() == before && r.host->made() == made, "a non-finite sample: refused, nothing counted");
  bad[4000] = 0.1f; CHECK(r.host->setPadTake(1, bad, false, N, 48000) == 0 && r.host->bytesHeld() == before + (size_t)N * 4, "the repaired block is taken");
  float* z = new float[16]; CHECK(r.host->setPadTake(-1, z, false, 16, 48000) != 0 && r.host->setPadTake(kRenderPads, z, false, 16, 48000) != 0 && r.host->setPadTake(2, nullptr, false, 16, 48000) != 0 && r.host->setPadTake(2, z, false, 0, 48000) != 0 && r.host->setPadTake(2, z, false, 16, 0) != 0, "bad arguments refused"); delete[] z;
  // stereo: all of L then all of R, 8 bytes a frame
  float* st = new float[(size_t)N * 2]; for (int i = 0; i < N; i++) { st[i] = 0.1f; st[N + i] = 0.2f; } const size_t b2 = r.host->bytesHeld();
  CHECK(r.host->setPadTake(2, st, true, N, 48000) == 0 && r.host->bytesHeld() == b2 + (size_t)N * 8, "stereo: %zu bytes more", r.host->bytesHeld() - b2);
  // removing a pad retires its bytes; a pad with no voice is freed by the next collect after the adopt
  CHECK(r.host->setPad(2, nullptr, nullptr, 0, 48000) == 0, "remove pad 2"); r.post(); r.quantum(); r.host->collect(); r.host->collect(); CHECK(r.host->bytesHeld() == before + (size_t)N * 4, "back to two pads' bytes (%zu)", r.host->bytesHeld());
}

// U37 P2, verify-276 R2-4: the epoch board is sized for the pool's slots at create(); beginQuantum asserts the pool still has that many (a pool that grew would overflow the board)
static void e11() {
  Rig r; if (!r.ok) { CHECK(false, "rig"); return; }
  CHECK(r.host->boardSlots() == r.P->epochSlots() && r.host->boardSlots() > 0, "the board has %d slots, the pool %d", r.host->boardSlots(), r.P->epochSlots());
  Players small(nullptr, 48000, 16); PadsHost* h2 = PadsHost::create(small); CHECK(h2 && h2->boardSlots() == small.epochSlots() && h2->boardSlots() < r.host->boardSlots(), "a smaller pool, a smaller board (%d)", h2 ? h2->boardSlots() : -1);
#ifndef NDEBUG
  // assertions are on: a pool of another size passed to beginQuantum must abort (a child process)
#if defined(__unix__) || defined(__APPLE__)
  if (h2) { const pid_t pid = fork(); if (pid == 0) { std::freopen("/dev/null", "w", stderr); h2->beginQuantum(*r.P, nullptr); _exit(0); } int stt = 0; waitpid(pid, &stt, 0); CHECK(WIFSIGNALED(stt) && WTERMSIG(stt) == SIGABRT, "beginQuantum with a pool of another size aborts (status %d)", stt); }
#endif
#else
  std::printf("  (R2-4: NDEBUG build: the assert itself is compiled out; the sizing is checked above)\n");
#endif
  small.setPads(nullptr); delete h2;
}

int main() {
  e0(); e1(); e2(); e3(); e4(); e5(); e6(); e7(); e8(); e9(); e10(); e11();
  std::printf("live_pads_epoch: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
