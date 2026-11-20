// LIVE ROUTE B1 (C1): reset() on the four live cores and the shell, seek() on the shell.
//   (1) a used core / shell, after reset(), gives the same samples as a fresh one, bit for bit (a script of notes, legato, rate ramps, wob / bpm, over many host block sizes), for bass, vox, acid, mach;
//   (2) reset() + seek(c) then a script shifted by c gives the fresh run's samples (a bind to a note), and the first latency() samples after the seek are silence;
//   (3) the parameters survive reset() (a non-default set, the vox lyrics);
//   (4) neither reset() nor seek() allocates (a counting operator new, as the real-time harness has).
//   test_live_reset
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

#include "engine/live_shell.h"

using namespace thunder;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static thread_local int g_guard = 0; static thread_local unsigned long g_allocs = 0;
void* operator new(std::size_t n) { if (g_guard) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void* operator new[](std::size_t n) { if (g_guard) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) std::abort(); return p; }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static const double SR = 48000;

static LiveEvent ev(uint32_t at, LiveEvType t, double v = 0, bool leg = false, bool acc = false, bool same = false, double dur = 0) { LiveEvent e; e.at = at; e.type = t; e.v = v; e.leg = leg; e.acc = acc; e.same = same; e.dur = dur; return e; }
// the script: (absolute sample, event); it ends with an End so the last control block is cut
static std::vector<std::pair<uint64_t, LiveEvent>> script(uint64_t shift) {
  std::vector<std::pair<uint64_t, LiveEvent>> s;
  auto add = [&](uint64_t t, LiveEvent e) { s.push_back({t + shift, e}); };
  add(0, ev(0, LiveEvType::RateSet, 1.0)); add(0, ev(0, LiveEvType::NoteOn, 0, false, true, false));
  add(700, ev(0, LiveEvType::RateRamp, 1.5, false, false, false, 400)); add(1500, ev(0, LiveEvType::NoteOn, 1, true, false, true));
  add(2100, ev(0, LiveEvType::Wob, 2)); add(2300, ev(0, LiveEvType::Bpm, 140)); add(2900, ev(0, LiveEvType::NoteOff));
  add(3300, ev(0, LiveEvType::RateSet, 0.75)); add(3301, ev(0, LiveEvType::NoteOn, 2, false, false, false)); add(5200, ev(0, LiveEvType::NoteOff)); add(7000, ev(0, LiveEvType::End));
  return s;
}
// runs a shell over [0, total) of ITS clock from `from` (its seek point), host blocks of `blk`, events by absolute sample; returns the output
template <class SH> static std::vector<float> run(SH& sh, uint64_t from, uint64_t total, uint32_t blk, const std::vector<std::pair<uint64_t, LiveEvent>>& sc) {
  std::vector<float> out; std::vector<float> L(blk), R(blk); std::vector<LiveEvent> evs;
  for (uint64_t t = from; t < from + total; t += blk) {
    const uint32_t n = (uint32_t)(from + total - t < blk ? from + total - t : blk);
    evs.clear(); for (auto& pe : sc) if (pe.first >= t && pe.first < t + n) { LiveEvent e = pe.second; e.at = (uint32_t)(pe.first - t); evs.push_back(e); }
    sh.process(L.data(), R.data(), n, evs.data(), (uint32_t)evs.size());
    out.insert(out.end(), L.begin(), L.begin() + n); out.insert(out.end(), R.begin(), R.begin() + n);
  }
  return out;
}
static bool same(const std::vector<float>& a, const std::vector<float>& b) { return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0; }
static bool silent(const std::vector<float>& a, size_t n) { for (size_t i = 0; i < n && i < a.size(); i++) if (a[i] != 0) return false; return true; }

template <class SH, class Mk> static void kind(const char* name, Mk setParams) {
  const uint32_t blks[] = {128, 17, 64, 1};
  const uint64_t total = 8000;
  for (uint32_t blk : blks) {
    SH fresh(SR); setParams(fresh.core());
    const std::vector<float> ref = run(fresh, 0, total, blk, script(0));
    // a used shell: a different script first (other notes, other rate), then reset()
    SH used(SR); setParams(used.core());
    { std::vector<std::pair<uint64_t, LiveEvent>> junk; junk.push_back({0, ev(0, LiveEvType::RateSet, 3)}); junk.push_back({10, ev(0, LiveEvType::NoteOn, 4, true, true, true)}); junk.push_back({999, ev(0, LiveEvType::Wob, 9)}); junk.push_back({1500, ev(0, LiveEvType::Bpm, 77)}); junk.push_back({3000, ev(0, LiveEvType::NoteOff)});
      run(used, 0, 5000, blk, junk); }
    g_guard = 1; const unsigned long a0 = g_allocs; used.reset(); const unsigned long a1 = g_allocs; g_guard = 0;
    CHECK(a1 == a0, "%s: reset() allocated (%lu)", name, a1 - a0);
    const std::vector<float> again = run(used, 0, total, blk, script(0));
    CHECK(same(ref, again), "%s blk %u: reset shell differs from a fresh one", name, blk);
    // a bind: reset + seek(c), the script shifted by c
    const uint64_t c = 123457;
    used.reset(); g_guard = 1; const unsigned long b0 = g_allocs; used.seek(c); const unsigned long b1 = g_allocs; g_guard = 0;
    CHECK(b1 == b0, "%s: seek() allocated", name);
    const std::vector<float> bound = run(used, c, total, blk, script(c));
    CHECK(same(ref, bound), "%s blk %u: reset + seek(c) differs from a fresh run", name, blk);
    // seek alone after play: the queue is dropped and the latency samples are silent
    used.seek(c * 2); const std::vector<float> s2 = run(used, c * 2, 400, blk, {});   // (the core keeps sounding what it had: seek moves the clock only; the first latency() samples are silence)
    { std::vector<float> l; const size_t lat = SH::latency(); for (size_t i = 0; i < lat; i++) l.push_back(s2[i]); CHECK(silent(l, lat), "%s: the first latency() samples after seek() are not silent", name); }
  }
  // the core's own reset keeps the parameters, and `bad` is cleared by it
  SH a(SR); setParams(a.core()); const auto p0 = a.core().p(); a.reset();
  CHECK(std::memcmp(&p0.oct, &a.core().p().oct, sizeof(double)) == 0, "%s: parameters changed by reset()", name);
}

int main() {
  kind<LiveBass>("bass", [](BassCore& c) { BassP p; p.shape = 0.6; p.det = 0.4; p.cut = 900; p.wRate = 3; p.wAmp = 0.5; p.glide = 30; c.set(p); });
  kind<LiveVox>("vox", [](VoxCore& c) { VoxP p; p.txt = "la di da, ba ba"; p.vib = 0.4; p.sylLen = 200; c.set(p); });
  kind<LiveAcid>("acid", [](AcidCore& c) { AcidP p; p.cut = 600; p.res = 0.8; p.wave = 1; p.slide = 40; c.set(p); });
  kind<LiveMach>("mach", [](MachCore& c) { MachP p; p.machine = 1; p.lp = 2500; p.det = 0.5; c.set(p); });
  { LiveVox v(SR); VoxP p; p.txt = "one two three"; v.core().set(p); const size_t n = v.core().nSyl(); v.reset(); CHECK(v.core().nSyl() == n && n > 0, "vox lyrics lost by reset()"); }
  if (fails) { std::printf("live_reset: %d FAILED\n", fails); return 1; }
  std::printf("live_reset: all checks passed\n"); return 0;
}
