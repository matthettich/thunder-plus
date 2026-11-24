// U37 P5 (ctest `live_capi`): the C API (thunder_live_*) as C++ code uses it.
//   1. BIT-EQUAL: ELECTRO with its effects, the master chain, PDC and the meters, played through thunder_live_* is bit for bit what LiveHost driven directly gives, and so (live_fx) what SongRenderer gives:
//      pads by pad_set and by pad_take (blocks from thunder_live_alloc), several call sizes and frame0s.
//   2. The numbers the shell needs: the engine offset recipe of live.h (MIDI records keep the engine frame), the caps.
//   3. A THREADED run through the API in the style of live_threads: a control thread, an audio thread and a consumer thread (poll, MIDI drain, stats), every call answered 0 or a code its comment lists.
// The dead handle is in test_live_capi_dead.cpp (it needs the wrapper's own source). The allocation guard over the audio-thread functions of the API is rt_harness pass 4. The pure-C test is test_live_c.c.
#include <atomic>
#include <chrono>
#include <thread>

#include "engine/layer_pe.h"
#include "fx/fxbank.h"
#include "live_common.h"
#include "master/chain.h"
#include "meter/meter.h"
#include "mix/pdc.h"

#if defined(THUNDER_SANITIZE_BUILD) || defined(__SANITIZE_ADDRESS__)
#define CAPI_SAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
#define CAPI_SAN 1
#endif
#endif
#ifndef CAPI_SAN
#define CAPI_SAN 0
#endif

using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;

namespace {
const uint32_t FX = THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER | THUNDER_LIVE_F_METERS;

struct Out { std::vector<float> l, r; int rc = 0; thunder_live_stats st; int nPads = 0; };

// ---- through the C API
Out viaApi(const std::string& text, const PadSet& ps, bool take, int cs, int64_t f0, int64_t N, uint32_t flags = FX) {
  Out o; thunder_live_opts op = opts(flags, 4096); char err[256] = {0};
  thunder_live* L = thunder_live_create(&op, err, sizeof err); if (!L) { std::printf("create: %s\n", err); o.rc = -1; return o; }
  int rc = thunder_live_song_publish(L, (const uint8_t*)text.data(), text.size(), nullptr, 0, 1, err, sizeof err); if (rc) { o.rc = -2; thunder_live_destroy(L); return o; }
  for (int p = 0; p < PadSet::PN; p++) {
    if (take) { float* b = thunder_live_alloc(PadSet::NS); if (!b) { o.rc = -3; break; } std::memcpy(b, ps.pad(p), sizeof(float) * PadSet::NS); rc = thunder_live_pad_take(L, p, b, 0, PadSet::NS, SR); if (rc) { thunder_live_free(b); o.rc = -4; break; } }
    else rc = thunder_live_pad_set(L, p, ps.pad(p), nullptr, PadSet::NS, SR);
    if (rc) { o.rc = -5; break; }
    rc = thunder_live_pad_kit(L, p, PadSet::choke(p), PadSet::syn(p) ? 1 : 0, PadSet::rel(p)); if (rc) { o.rc = -6; break; }
  }
  if (!o.rc) rc = thunder_live_pads_publish(L), o.rc = rc;
  if (!o.rc) o.rc = thunder_live_meters_want(L, 30);
  thunder_live_play_opts po = playOpts(f0); po.pdc_mode = 1;
  if (!o.rc) o.rc = thunder_live_play(L, &po);
  int64_t frame = f0, done = 0; std::vector<float> a((size_t)cs), b((size_t)cs);
  while (!o.rc && done < N) { const int c = (int)std::min<int64_t>(cs, N - done); o.rc = thunder_live_process(L, a.data(), b.data(), c, frame, 0); o.l.insert(o.l.end(), a.begin(), a.begin() + c); o.r.insert(o.r.end(), b.begin(), b.begin() + c); done += c; frame += c; }
  std::memset(&o.st, 0, sizeof o.st); o.st.size = sizeof o.st; thunder_live_stats_get(L, &o.st);
  thunder_live_destroy(L);
  return o;
}
// ---- the C++ class directly (what live_fx does)
Out direct(const std::string& text, const PadSet& ps, int cs, int64_t f0, int64_t N, uint32_t flags = FX) {
  Out o; Host H(opts(flags, 4096)); if (!H.h) { o.rc = -1; return o; }
  H.song(text); H.loadPads(ps); H->meters(30);
  thunder_live_play_opts po = playOpts(f0); po.pdc_mode = 1; if (H->play(po)) { o.rc = -2; return o; }
  int64_t frame = f0; o.rc = H.run(o.l, o.r, N, cs, frame); o.st = stats(H.h);
  return o;
}
// ---- SongRenderer, as render_api.cpp / live_fx set it up
Out reference(const Song& song, const RenderPads& rp, int64_t N) {
  Out o; SeqVars vars; SeqOpts so; so.once = false; so.live = false; so.sim = false; so.randomSeed = 0x2545F491u;
  SongRenderer R(song, vars, rp, SR, 512, so, 64);
  R.setEffects(true); R.setMasterChain(master_chain_from_song(song.mst, song.limiter));
  std::vector<PdcSlot> slots(1024); const int ns = R.fxBank() ? R.fxBank()->pdcSlots(slots.data(), (int)slots.size()) : 0;
  R.setPlan(pdc_plan(bus_plan(song.chainPatch, song.chainOn), slots.data(), ns, SR, PdcMode::Auto), PdcMode::Auto, 0);
  MeterBank bank(SR); R.setMeters(&bank);
  o.l.resize((size_t)N); o.r.resize((size_t)N);
  for (int64_t q = 0; q * Q < N; q++) { const int n = (int)std::min<int64_t>(Q, N - q * Q); R.block(o.l.data() + q * Q, o.r.data() + q * Q, n); MeterEvent e; while (bank.pop(&e)) {} }
  return o;
}
size_t diff(const std::vector<float>& a, const std::vector<float>& b, size_t* first) {
  size_t bad = 0; *first = (size_t)-1; const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) if (std::memcmp(&a[i], &b[i], 4) != 0) { if (*first == (size_t)-1) *first = i; bad++; }
  return bad + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}


// ---- edits through the API against the same edits on LiveHost (kills a wrapper that drops or inverts an argument)
struct EditCfg { int masterSafety = -1; int64_t fxAt = -2; /* -2: no effects edit; -1: NOW; else frames after the edit point */ };
Out editRun(bool api, const EditCfg& c, const std::string& text, const PadSet& ps) {
  Out o; const char* fxj = "[{\"type\":\"delay\",\"on\":true}]"; const size_t fxn = std::strlen(fxj);
  thunder_live* L = nullptr; Host* H = nullptr; std::unique_ptr<Host> hold; int64_t frame = 0; int rc = 0;
  thunder_live_opts op = opts(FX, 4096); char err[128];
  if (api) { L = thunder_live_create(&op, err, sizeof err); if (!L) { o.rc = -1; return o; } rc = thunder_live_song_publish(L, (const uint8_t*)text.data(), text.size(), nullptr, 0, 1, err, sizeof err); for (int p = 0; p < PadSet::PN && !rc; p++) { rc = thunder_live_pad_set(L, p, ps.pad(p), nullptr, PadSet::NS, SR); if (!rc) rc = thunder_live_pad_kit(L, p, PadSet::choke(p), PadSet::syn(p) ? 1 : 0, PadSet::rel(p)); } if (!rc) rc = thunder_live_pads_publish(L); thunder_live_play_opts po = playOpts(0); po.pdc_mode = 1; if (!rc) rc = thunder_live_play(L, &po); }
  else { hold.reset(new Host(op)); H = hold.get(); if (!H->h) { o.rc = -1; return o; } H->song(text); rc = H->loadPads(ps); thunder_live_play_opts po = playOpts(0); po.pdc_mode = 1; if (!rc) rc = (*H)->play(po); }
  if (rc) { o.rc = -2; if (L) thunder_live_destroy(L); return o; }
  auto run = [&](int64_t n) { std::vector<float> a(128), b(128); for (int64_t d = 0; d < n; d += 128) { const int r = api ? thunder_live_process(L, a.data(), b.data(), 128, frame, 0) : (*H)->process(a.data(), b.data(), 128, frame, 0); if (r) o.rc = r; o.l.insert(o.l.end(), a.begin(), a.end()); o.r.insert(o.r.end(), b.begin(), b.end()); frame += 128; } };
  run(48000);
  if (c.masterSafety >= 0) o.rc |= api ? thunder_live_master_config(L, nullptr, 0, c.masterSafety) : (*H)->masterConfig(nullptr, 0, c.masterSafety != 0);
  if (c.fxAt != -2) {
    o.rc |= api ? thunder_live_fx_plan(L, THUNDER_LIVE_MASTER, (const uint8_t*)fxj, fxn, 120) : (*H)->fxPlan(THUNDER_LIVE_MASTER, (const uint8_t*)fxj, fxn, 120);
    const int64_t at = c.fxAt < 0 ? THUNDER_LIVE_NOW : frame + c.fxAt;
    o.rc |= api ? thunder_live_fx_commit(L, THUNDER_LIVE_MASTER, at) : (*H)->fxCommit(THUNDER_LIVE_MASTER, at);
  }
  run(48000 * 3);
  if (L) thunder_live_destroy(L);
  return o;
}

struct Rng { uint64_t s; uint32_t next() { s = s * 6364136223846793005ull + 1442695040888963407ull; return (uint32_t)(s >> 33); } int below(int n) { return (int)(next() % (uint32_t)n); } double unit() { return next() / 2147483648.0; } };
bool okRc(int rc) { return rc == 0 || rc == THUNDER_LIVE_E_BUSY || rc == THUNDER_LIVE_E_LIMIT || rc == THUNDER_LIVE_E_SONG || rc == THUNDER_LIVE_E_STATE; }
}  // namespace

int main() {
  const std::string text = electro(); if (text.empty()) { std::printf("FAIL: cannot read ELECTRO\n"); return 1; }
  PadSet ps; static RenderPads rp; fillRenderPads(rp, ps);
  Song song; { JsonError je; if (!song.load(text.data(), text.size(), &je, true)) { std::printf("FAIL: ELECTRO does not load\n"); return 1; } }

  // ---- 2. caps
  { const uint32_t c = thunder_live_caps(); CHECK((c & THUNDER_LIVE_CAP_MASTER_SWAP) && (c & THUNDER_LIVE_CAP_TAKE), "caps: master swap and take");
#ifdef THUNDER_HAVE_TFX
    CHECK(c & THUNDER_LIVE_CAP_EFFECTS, "caps: effects (the core is built with tfx)");
#endif
    CHECK(thunder_live_abi() == THUNDER_LIVE_ABI, "abi"); }
  // alloc: aligned as operator new[] is; the pointer is usable for 2^20 floats
  { float* b = thunder_live_alloc(1 << 20); CHECK(b && ((uintptr_t)b % alignof(float)) == 0, "alloc 2^20 floats"); if (b) { b[0] = 1; b[(1 << 20) - 1] = 2; thunder_live_free(b); }
    std::printf("thunder_live_alloc: 16-byte aligned: %s\n", [] { float* p = thunder_live_alloc(10); const bool a = p && ((uintptr_t)p % 16) == 0; thunder_live_free(p); return a ? "yes" : "no"; }()); }

  // ---- 1. BIT-EQUAL
  const int64_t N = (int64_t)(SR * (CAPI_SAN ? 5 : 14));
  if (CAPI_SAN) std::printf("live_capi: sanitizer build, the reduced matrix\n");
  const Out ref = reference(song, rp, N);
  { double pk = 0; for (float x : ref.l) pk = std::max(pk, (double)std::fabs(x)); CHECK(pk > 0.01, "the reference sounds (%g)", pk); }
  struct Case { int cs; int64_t f0; bool take; };
  std::vector<Case> cases = {{128, 0, false}, {333, 0, true}};
  if (!CAPI_SAN) { cases.push_back({480, 37, false}); cases.push_back({4096, (int64_t)1 << 31, true}); cases.push_back({1, 0, false}); }
  else cases.push_back({4096, (int64_t)1 << 31, false});
  for (const Case& c : cases) {
    const int64_t NN = c.cs == 1 ? std::min<int64_t>(N, (int64_t)(SR * 2)) : N;   // call size 1: 2 s (one quantum call per frame is the slow path)
    const Out A = viaApi(text, ps, c.take, c.cs, c.f0, NN);
    CHECK(A.rc == 0, "C API run: rc %d", A.rc);
    size_t first = 0; size_t b1 = diff(A.l, ref.l, &first), b2 = diff(A.r, ref.r, &first);
    if (NN != N) { b1 = b2 = 0; for (int64_t i = 0; i < NN; i++) { if (std::memcmp(&A.l[(size_t)i], &ref.l[(size_t)i], 4)) b1++; if (std::memcmp(&A.r[(size_t)i], &ref.r[(size_t)i], 4)) b2++; } }
    CHECK(b1 == 0 && b2 == 0, "C API (call size %d, frame0 %lld, %s) against SongRenderer: %zu + %zu samples differ", c.cs, (long long)c.f0, c.take ? "pad_take" : "pad_set", b1, b2);
    const Out D = direct(text, ps, c.cs, c.f0, NN);
    size_t f2; CHECK(D.rc == 0 && diff(A.l, D.l, &f2) == 0 && diff(A.r, D.r, &f2) == 0, "C API (call size %d, frame0 %lld, %s) against LiveHost driven directly: bit-equal", c.cs, (long long)c.f0, c.take ? "pad_take" : "pad_set");
    CHECK(A.st.seq_dropped == 0 && A.st.fx_failed_blocks == 0 && A.st.reanchors == 0 && A.st.evt_dropped == D.st.evt_dropped, "no dropped sequencer events, no failed effects block, no re-anchor, the same records refused as LiveHost (nobody polls in this run)");
    CHECK(A.st.quanta == D.st.quanta && A.st.steps == D.st.steps && A.st.events == D.st.events && A.st.latency_frames == D.st.latency_frames, "the same counters (%llu quanta, %llu steps, latency %d)", (unsigned long long)A.st.quanta, (unsigned long long)A.st.steps, A.st.latency_frames);
    std::printf("C API: call size %4d, frame0 %12lld, %-8s: %s (%llu quanta, latency %d)\n", c.cs, (long long)c.f0, c.take ? "pad_take" : "pad_set", b1 + b2 ? "DIFFERENT" : "bit-equal to LiveHost and SongRenderer", (unsigned long long)A.st.quanta, A.st.latency_frames);
  }

  // ---- 1b. EDITS: master_config's `safety` and fx_commit's `at` reach the host as given
  { EditCfg s1; s1.masterSafety = 1; EditCfg s0; s0.masterSafety = 0;
    const Out a1 = editRun(true, s1, text, ps), d1 = editRun(false, s1, text, ps), a0 = editRun(true, s0, text, ps), d0 = editRun(false, s0, text, ps); size_t f;
    CHECK(a1.rc == 0 && d1.rc == 0 && a0.rc == 0 && d0.rc == 0, "master_config runs (%d %d %d %d)", a1.rc, d1.rc, a0.rc, d0.rc);
    CHECK(diff(a1.l, d1.l, &f) == 0 && diff(a1.r, d1.r, &f) == 0, "master_config(safety 1) through the API equals masterConfig(true)");
    CHECK(diff(a0.l, d0.l, &f) == 0 && diff(a0.r, d0.r, &f) == 0, "master_config(safety 0) through the API equals masterConfig(false)");
    CHECK(diff(d1.l, d0.l, &f) != 0, "the safety limiter makes a difference in this run (else the two checks above could not tell an inverted flag)"); }
  { EditCfg at; at.fxAt = 30000; EditCfg now; now.fxAt = -1;
    const Out aa = editRun(true, at, text, ps), da = editRun(false, at, text, ps), an = editRun(true, now, text, ps), dn = editRun(false, now, text, ps); size_t f;
    CHECK(aa.rc == 0 && da.rc == 0 && an.rc == 0 && dn.rc == 0, "fx_commit runs (%d %d %d %d)", aa.rc, da.rc, an.rc, dn.rc);
    CHECK(diff(aa.l, da.l, &f) == 0 && diff(aa.r, da.r, &f) == 0, "fx_commit(at 30000 frames on) through the API equals fxCommit(at)");
    CHECK(diff(an.l, dn.l, &f) == 0 && diff(an.r, dn.r, &f) == 0, "fx_commit(NOW) through the API equals fxCommit(NOW)");
    CHECK(diff(da.l, dn.l, &f) != 0, "the commit time makes a difference in this run (else `at` could be dropped unseen)"); }

  // ---- 1c. THE ENGINE OFFSET read from another thread while process runs: never torn, never stale for a call that has returned
  { thunder_live_opts op = opts(0, 128); thunder_live* L = thunder_live_create(&op, nullptr, 0);
    const int64_t val[4] = {0, 1000, 6000, 3000};   // after: nothing, the first call (frame0 1000), a forward gap of 5000, a jump back of 3000
    std::atomic<int> phase{0}; std::atomic<bool> done{false}; std::atomic<int> bad{0}; std::atomic<long> reads{0};
    std::thread audio([&] {
      std::vector<float> l(128), r(128); int64_t fr = 1000; const int N1 = CAPI_SAN ? 3000 : 30000;
      thunder_live_process(L, l.data(), r.data(), 128, fr, 0); fr += 128; phase.store(1, std::memory_order_release);
      for (int i = 0; i < N1; i++) { thunder_live_process(L, l.data(), r.data(), 128, fr, 0); fr += 128; }
      thunder_live_process(L, l.data(), r.data(), 1, fr, 0); fr += 1;                                   // a call that completes no quantum ...
      fr += 5000; thunder_live_process(L, l.data(), r.data(), 1, fr, 0); fr += 1; phase.store(2, std::memory_order_release);   // ... and a re-anchor inside one
      for (int i = 0; i < N1 / 4; i++) { thunder_live_process(L, l.data(), r.data(), 128, fr, 0); fr += 128; }
      fr -= 3000; thunder_live_process(L, l.data(), r.data(), 1, fr, 0); phase.store(3, std::memory_order_release);
      for (int i = 0; i < N1 / 4; i++) { thunder_live_process(L, l.data(), r.data(), 1, fr + 1 + i, 0); }
      done.store(true, std::memory_order_release);
    });
    std::thread reader([&] {
      while (!done.load(std::memory_order_acquire)) {
        const int p1 = phase.load(std::memory_order_acquire); int64_t v = -999; thunder_live_engine_offset(L, &v); const int p2 = phase.load(std::memory_order_acquire); reads++;
        bool ok = false; for (int q = p1; q <= std::min(3, p2 + 1); q++) if (v == val[q]) ok = true;
        if (!ok) bad++;
      }
    });
    audio.join(); reader.join();
    CHECK(bad.load() == 0 && reads.load() > 1000, "engine offset: %ld reads against a running process, %d torn or stale", reads.load(), bad.load());
    int64_t v = 0; thunder_live_engine_offset(L, &v); CHECK(v == 3000, "the last offset (%lld)", (long long)v);
    thunder_live_destroy(L); }

  // ---- 2b. the engine offset of live.h (MIDI records keep the engine frame): offset = stats.frames - 128 * stats.quanta, in a gapless run and across a re-anchor
  { thunder_live_opts op = opts(0, 1024); thunder_live* L = thunder_live_create(&op, nullptr, 0); std::vector<float> a(1024), b(1024);
    int64_t fr = 777; for (int i = 0; i < 10; i++) { thunder_live_process(L, a.data(), b.data(), 1024, fr, 0); fr += 1024; }
    thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; thunder_live_stats_get(L, &s);
    CHECK(s.frames - Q * (int64_t)s.quanta == 777, "gapless: OFFSET = first frame0 (%lld)", (long long)(s.frames - Q * (int64_t)s.quanta));
    fr += 5000; for (int i = 0; i < 4; i++) { thunder_live_process(L, a.data(), b.data(), 1024, fr, 0); fr += 1024; }
    thunder_live_stats_get(L, &s);
    CHECK(s.reanchors == 1 && s.frames - Q * (int64_t)s.quanta == 777 + 5000, "after a forward gap of 5000: OFFSET = 777 + 5000 (%lld)", (long long)(s.frames - Q * (int64_t)s.quanta));
    thunder_live_destroy(L); }

  // ---- 3. THE THREADED RUN through the API: control, audio, consumer
  {
    thunder_live_opts op = opts(THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER | THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_MIDI, 1024); char err[128];
    thunder_live* L = thunder_live_create(&op, err, sizeof err); if (!L) { std::printf("FAIL: create %s\n", err); return 1; }
    std::string mtext = text; for (size_t at = 0; (at = mtext.find("\"mo\":0", at)) != std::string::npos; at += 6) mtext.replace(at, 6, "\"mo\":2");
    CHECK(thunder_midi_set_port(thunder_live_midi(L), 0, 1, 1, 0, 0) == 0, "midi port");
    CHECK(thunder_live_song_publish(L, (const uint8_t*)mtext.data(), mtext.size(), nullptr, 0, 1, err, sizeof err) == 0, "song");
    for (int p = 0; p < PadSet::PN; p++) { thunder_live_pad_set(L, p, ps.pad(p), nullptr, PadSet::NS, SR); thunder_live_pad_kit(L, p, PadSet::choke(p), PadSet::syn(p), PadSet::rel(p)); }
    CHECK(thunder_live_pads_publish(L) == 0, "pads");
    std::atomic<bool> stop{false}, ctlDone{false}; std::atomic<int64_t> frames{0}; std::atomic<int> audioErr{0}, consErr{0}; std::atomic<uint64_t> midiRecs{0}, consRecs{0}; int64_t nextFrame = 0;
    std::thread audio([&] {
      std::vector<float> l(1024), r(1024); Rng g{99}; int64_t fr = 0;
      while (!stop.load(std::memory_order_acquire)) {
        const int n = g.below(3) == 0 ? 1 + g.below(1024) : 128;
        if (thunder_live_process(L, l.data(), r.data(), n, fr, fr * 20833) != 0) audioErr++;
        for (int i = 0; i < n; i++) if (!std::isfinite(l[(size_t)i]) || !std::isfinite(r[(size_t)i])) audioErr++;
        fr += n; frames.store(fr, std::memory_order_relaxed);
        if ((fr & 0xFFFF) < 1024) std::this_thread::yield();
      }
      nextFrame = fr;
    });
    std::thread consumer([&] {   // the ONE consumer thread: records, the MIDI records, the stats
      uint32_t prev = 0; unsigned char buf[1024]; std::vector<thunder_midi_msg> m(256);
      while (!ctlDone.load(std::memory_order_acquire)) {
        for (int n; (n = thunder_live_poll(L, buf, sizeof buf)) > 0;) { int o = 0; while (o < n) { thunder_live_ev_head h; std::memcpy(&h, buf + o, sizeof h); if (prev && (int32_t)(h.seq - prev) <= 0) consErr++; prev = h.seq; o += h.bytes; consRecs++; } }
        for (int64_t k; (k = thunder_live_midi_drain(L, m.data(), (int64_t)m.size())) > 0;) midiRecs += (uint64_t)k;
        thunder_live_stats s; std::memset(&s, 0, sizeof s); s.size = sizeof s; if (thunder_live_stats_get(L, &s) != 0 || s.pad_bytes_held < s.pad_bytes_retired) consErr++;
        thunder_live_host_stats(L, 480, 0);
        std::this_thread::sleep_for(std::chrono::microseconds(300));
      }
    });
    // CONTROL (this thread)
    int badRc = 0, busy = 0, ops = 0; Rng g{12345}; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); po.random_seed = 0x2545F491u;
    std::vector<std::vector<float>> variant; for (int v = 0; v < 4; v++) { std::vector<float> t((size_t)PadSet::NS); for (int i = 0; i < PadSet::NS; i++) t[(size_t)i] = (float)std::sin(0.013 * (v + 1) * i) * 0.25f; variant.push_back(std::move(t)); }
    auto retry = [&](auto&& f) { int rc = f(); for (int k = 0; k < 50 && rc == THUNDER_LIVE_E_BUSY; k++) { busy++; std::this_thread::yield(); std::this_thread::sleep_for(std::chrono::microseconds(100)); rc = f(); } return rc; };
    const int ITER = CAPI_SAN ? 700 : 4000;
    static const char* fxj[] = {"[{\"type\":\"delay\",\"on\":true}]", "[{\"type\":\"tape\"},{\"type\":\"reverb\"}]", "[]"};
    static const char* mj[] = {"{\"on\":true,\"comp\":{\"thr\":-25,\"ratio\":6}}", "{\"on\":false}"};
    for (int it = 0; it < ITER; it++) {
      int rc = 0; const int k = g.below(100); ops++;
      if (k < 6) rc = retry([&] { return thunder_live_song_publish(L, (const uint8_t*)mtext.data(), mtext.size(), nullptr, 0, 100 + it, err, sizeof err); });
      else if (k < 8) { const char bad[] = "{not a song"; rc = thunder_live_song_publish(L, (const uint8_t*)bad, sizeof bad - 1, nullptr, 0, 5, err, sizeof err); if (rc != THUNDER_LIVE_E_SONG) badRc++; rc = 0; }
      else if (k < 15) { const int p = g.below(64); const auto& v = variant[(size_t)g.below(4)]; rc = thunder_live_pad_set(L, p, v.data(), nullptr, PadSet::NS, SR); if (!rc) rc = retry([&] { return thunder_live_pads_publish(L); }); }
      else if (k < 19) { float* b = thunder_live_alloc(PadSet::NS); if (b) { for (int i = 0; i < PadSet::NS; i++) b[i] = 0.2f * (float)std::sin(0.02 * i); rc = thunder_live_pad_take(L, g.below(64), b, 0, PadSet::NS, SR); if (rc) thunder_live_free(b); else rc = retry([&] { return thunder_live_pads_publish(L); }); } }
      else if (k < 23) rc = retry([&] { return thunder_live_play(L, &po); });
      else if (k < 27) rc = retry([&] { return thunder_live_stop(L, THUNDER_LIVE_NOW); });
      else if (k < 30) rc = retry([&] { return thunder_live_hold(L, THUNDER_LIVE_NOW); });
      else if (k < 34) rc = retry([&] { return thunder_live_loop(L, g.below(2), g.below(4), 3 + g.below(4), THUNDER_LIVE_NOW); });
      else if (k < 40) rc = retry([&] { return thunder_live_bpm(L, 60 + g.below(120), g.below(2) ? THUNDER_LIVE_NOW : frames.load() + g.below(20000)); });
      else if (k < 43) rc = retry([&] { return thunder_live_lead(L, 0.05 + g.unit() * 1.5, THUNDER_LIVE_NOW); });
      else if (k < 47) { const int w = g.below(3); const int ix = w == THUNDER_LIVE_PERF_MACRO ? g.below(8) : 0; const double v = w == THUNDER_LIVE_PERF_FILL ? g.below(2) : g.unit(); rc = retry([&] { return thunder_live_perf(L, w, ix, v, THUNDER_LIVE_NOW); }); }
      else if (k < 50) rc = retry([&] { return thunder_live_stop_track(L, g.below(32), THUNDER_LIVE_NOW); });
      else if (k < 58) { const int key = g.below(2) ? THUNDER_LIVE_MIX_VOL : (g.below(2) ? THUNDER_LIVE_MIX_MUTE : THUNDER_LIVE_MIX_SOLO); const int trk = key == THUNDER_LIVE_MIX_VOL ? g.below(33) : g.below(32); const double ramp = g.below(3) == 0 ? -1 : g.below(500); rc = retry([&] { return thunder_live_mix_set(L, trk, key, g.below(2), ramp, THUNDER_LIVE_NOW); }); }
      else if (k < 62) { const int t = g.below(33); const char* js = fxj[g.below(3)]; rc = thunder_live_fx_plan(L, t, (const uint8_t*)js, std::strlen(js), 120); if (rc == 0) rc = retry([&] { return thunder_live_fx_commit(L, t, THUNDER_LIVE_NOW); }); }
      else if (k < 66) rc = retry([&] { return thunder_live_pdc_set(L, g.below(3)); });
      else if (k < 69) { const char* q = mj[g.below(2)]; rc = retry([&] { return thunder_live_master_config(L, (const uint8_t*)q, std::strlen(q), g.below(2)); }); }
      else if (k < 72) rc = retry([&] { return thunder_live_meters_want(L, g.below(3) ? 30.0 : 0.0); });
      else if (k < 75) rc = retry([&] { return thunder_live_midi_lead(L, 0.01 + 1.19 * g.unit()); });
      else rc = thunder_live_collect(L) >= 0 ? 0 : THUNDER_LIVE_E_ARG;
      if (it % 500 == 250) for (int i = 0; i < 300; i++) { rc = retry([&] { return thunder_live_bpm(L, 90 + i % 30, THUNDER_LIVE_NOW); }); if (rc) break; }
      if (!okRc(rc)) { badRc++; std::printf("op %d (k %d): rc %d\n", it, k, rc); }
      if ((it & 63) == 0) std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    ctlDone.store(true, std::memory_order_release); consumer.join();
    // the header's shutdown: stop, one more process call, the records, then destroy (the audio thread keeps running until the stop has been applied)
    retry([&] { return thunder_live_stop(L, THUNDER_LIVE_NOW); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    stop.store(true, std::memory_order_release); audio.join();
    { std::vector<float> l(256), r(256); int64_t fr = nextFrame; for (int i = 0; i < 4; i++) { if (thunder_live_process(L, l.data(), r.data(), 256, fr, 0)) audioErr++; fr += 256; } }
    thunder_live_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; thunder_live_stats_get(L, &st);
    thunder_midi_msg m[64]; uint64_t tail = 0; for (int64_t k; (k = thunder_live_midi_drain(L, m, 64)) > 0;) tail += (uint64_t)k;
    CHECK(badRc == 0, "every call answered 0, E_BUSY, E_LIMIT, E_STATE or the expected E_SONG (%d bad of %d ops, %d busy)", badRc, ops, busy);
    CHECK(audioErr.load() == 0, "process never failed and never produced a non-finite sample (%d)", audioErr.load());
    CHECK(consErr.load() == 0, "the consumer saw ascending seq numbers and sane stats (%d)", consErr.load());
    bool hasWorkspace = false; { std::thread t([&] { hasWorkspace = !pe_thread_ready(); }); t.join(); }
    CHECK(st.thread_prepares == (hasWorkspace ? 1u : 0u), "the audio side prepared its workspace %u time(s)", st.thread_prepares);
    CHECK(st.reanchors == 0 && st.seq_dropped == 0, "no re-anchor (%u), no dropped sequencer events (%llu)", st.reanchors, (unsigned long long)st.seq_dropped);
    CHECK(st.midi_begin_failed == 0, "no midi_begin failure");
    std::printf("threaded: %d control ops (%d E_BUSY retries), %llu quanta, %llu commands applied, %llu records consumed, %llu MIDI records (+%llu after the stop), %u late commands\n", ops, busy, (unsigned long long)st.quanta, (unsigned long long)st.cmds_applied, (unsigned long long)consRecs.load(), (unsigned long long)midiRecs.load(), (unsigned long long)tail, st.cmd_late);
    CHECK(consRecs.load() > 100, "the consumer read records (%llu)", (unsigned long long)consRecs.load());
    thunder_live_destroy(L);
  }
  std::printf("live_capi: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
