// U37 P2: LiveHost pads: a pad replaced while a long sample sounds (the old buffer is retired, read until its voice ends, freed by collect: ASan sees any read of freed memory), the budget (E_LIMIT before anything is
// allocated or read; retired bytes count; a transient E_LIMIT clears after collect), pad_take (ownership, errors leave the block with the caller), the pads ring (16) and the PADS record.
#include <new>
#include "live_common.h"
using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;

static std::vector<float> tone(int n, double f, float amp = 0.3f) { std::vector<float> v((size_t)n); for (int i = 0; i < n; i++) v[(size_t)i] = (float)std::sin(f * i) * amp; return v; }

// the 64 pads of the ELECTRO set, but pads 0 .. 7 are 10 s long so a voice of one keeps reading its buffer for seconds
struct Long { std::vector<std::vector<float>> p; Long() { for (int i = 0; i < 64; i++) p.push_back(tone(i < 8 ? 480000 : PadSet::NS, 0.01 * (i % 7 + 1) + 0.0003 * i)); } };
static int setAll(Host& H, Long& L, bool kit = true) {
  for (int i = 0; i < 64; i++) { int rc = H->padSet(i, L.p[(size_t)i].data(), nullptr, (int64_t)L.p[(size_t)i].size(), SR); if (rc) return rc; if (kit) { rc = H->padKit(i, PadSet::choke(i), PadSet::syn(i) ? 1 : 0, PadSet::rel(i)); if (rc) return rc; } }
  return H->padsPublish();
}

static void replaceWhileSounding() {
  Host H(opts()); Long L; H.song(electro(), 1); CHECK(setAll(H, L) == 0, "pads"); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); int64_t fr = 0; H.run(48000 * 3, Q, fr);
  auto st = stats(H.h); CHECK(st.voices > 0 && st.pad_buffers == 64 && st.pad_retired == 0, "playing: %d voices, %d buffers, %d retired", st.voices, st.pad_buffers, st.pad_retired);
  const uint64_t held0 = st.pad_bytes_held; Long L2; for (auto& v : L2.p) for (auto& x : v) x = -x;   // new data on every pad
  CHECK(setAll(H, L2) == 0, "replace all"); H.run(Q * 2, Q, fr); H.h->collect(); st = stats(H.h);
  CHECK(st.pad_retired > 0 && st.pad_bytes_retired > 0, "old buffers of pads with sounding voices are still held: %d retired, %llu bytes", st.pad_retired, (unsigned long long)st.pad_bytes_retired);
  CHECK(st.pad_buffers == 64 + st.pad_retired && st.pad_bytes_held >= held0 + st.pad_bytes_retired - 1 && st.pad_bytes_held <= held0 + st.pad_bytes_retired + 1, "held = current + retired (%llu = %llu + %llu)", (unsigned long long)st.pad_bytes_held, (unsigned long long)held0, (unsigned long long)st.pad_bytes_retired);
  // the voices go on reading the old buffers (ASan: nothing freed under them); some seconds later they are gone and collect frees
  int prevRetired = st.pad_retired; bool shrank = false; for (int i = 0; i < 12 * 48000 / 4096 + 1; i++) { H.run(4096, 4096, fr); H.h->collect(); if (stats(H.h).pad_retired < prevRetired) shrank = true; prevRetired = stats(H.h).pad_retired; }
  CHECK(shrank, "retired buffers are freed one by one as their voices end");
  H->stop(THUNDER_LIVE_NOW); H.run(48000, Q, fr); H.h->collect(); H.run(Q, Q, fr); H.h->collect(); st = stats(H.h);
  CHECK(st.pad_retired == 0 && st.pad_buffers == 64 && st.pad_bytes_retired == 0, "after stop and collect: %d retired, %d buffers, %llu retired bytes", st.pad_retired, st.pad_buffers, (unsigned long long)st.pad_bytes_retired);
  CHECK(st.pad_bytes_held == held0, "held bytes back to the current set (%llu vs %llu)", (unsigned long long)st.pad_bytes_held, (unsigned long long)held0);
  // identical data: nothing changes (no new buffer)
  const int b0 = st.pad_buffers; CHECK(H->padSet(0, L2.p[0].data(), nullptr, (int64_t)L2.p[0].size(), SR) == 0 && H->padsPublish() == 0, "same data"); H.run(Q, Q, fr); H.h->collect(); CHECK(stats(H.h).pad_buffers == b0 && stats(H.h).pad_bytes_held == held0, "the same samples change nothing");
  // frames == 0 removes the pad
  CHECK(H->padSet(5, nullptr, nullptr, 0, SR) == 0 && H->padsPublish() == 0, "remove"); H.run(Q, Q, fr); H.h->collect(); H.run(Q, Q, fr); H.h->collect(); st = stats(H.h); CHECK(st.pad_buffers == 63 && st.pad_bytes_held == held0 - 480000 * 4, "pad 5 removed (%d buffers, %llu bytes)", st.pad_buffers, (unsigned long long)st.pad_bytes_held);
}

static void budget() {
  const int N = 100000; const uint64_t B = (uint64_t)N * 4;   // one pad = 400000 bytes
  { thunder_live_opts o = opts(); o.max_pad_bytes = B * 2 + B / 2; Host H(o); int64_t fr = 0; auto a = tone(N, 0.02), b = tone(N, 0.03), c = tone(N, 0.04);
    CHECK(stats(H.h).pad_budget == B * 2 + B / 2, "budget as made");
    // E_LIMIT is decided before anything is read: a 2^27-frame pad from a pointer to 4 floats must be refused unread (ASan would flag any read; so would a crash)
    float tiny[4] = {0, 0, 0, 0}; CHECK(H->padSet(0, tiny, nullptr, (int64_t)1 << 27, SR) == THUNDER_LIVE_E_LIMIT, "huge pad: E_LIMIT, unread");
    CHECK(H->padSet(0, a.data(), nullptr, N, SR) == 0 && stats(H.h).pad_bytes_held == B, "a pad: %llu bytes", (unsigned long long)stats(H.h).pad_bytes_held);
    CHECK(H->padSet(1, tiny, nullptr, (int64_t)N * 3, SR) == THUNDER_LIVE_E_LIMIT, "1200000 bytes more would pass 1000000: E_LIMIT before the read"); CHECK(stats(H.h).pad_bytes_held == B, "nothing changed");
    CHECK(H->padSet(1, b.data(), nullptr, N, SR) == 0 && stats(H.h).pad_bytes_held == 2 * B, "a second pad"); CHECK(H->padSet(2, c.data(), nullptr, N, SR) == THUNDER_LIVE_E_LIMIT, "a third: E_LIMIT (the budget is 2.5 pads)");
    CHECK(H->padsPublish() == 0, "publish"); H.run(Q, Q, fr);
    // a replacement needs room for the new buffer BESIDE the old one: E_LIMIT, and the old pad stays current
    CHECK(H->padSet(0, c.data(), nullptr, N, SR) == THUNDER_LIVE_E_LIMIT && stats(H.h).pad_bytes_held == 2 * B, "replace: E_LIMIT, nothing changed");
    // remove one, publish, adopt, collect: the room is back
    CHECK(H->padSet(1, nullptr, nullptr, 0, SR) == 0 && H->padsPublish() == 0, "remove pad 1"); H.run(Q, Q, fr);
    CHECK(stats(H.h).pad_bytes_held == 2 * B, "the removed pad's bytes still count until collect (%llu held)", (unsigned long long)stats(H.h).pad_bytes_held);
    H->collect(); CHECK(stats(H.h).pad_bytes_held == B && stats(H.h).pad_bytes_retired == 0, "collect freed it (%llu)", (unsigned long long)stats(H.h).pad_bytes_held);
    CHECK(H->padSet(0, c.data(), nullptr, N, SR) == 0, "the replacement now fits"); }
  // the transient E_LIMIT: a replaced pad's bytes count until its voices are gone
  { thunder_live_opts o = opts(); o.max_pad_bytes = (uint64_t)(48000 * 10 * 4) * 2 + 400000; Host H(o); int64_t fr = 0; H.song(electro(), 1);
    std::vector<std::vector<float>> p; for (int i = 0; i < 64; i++) p.push_back(tone(i < 1 ? 480000 : 1000, 0.01 + 0.0001 * i)); int rc = 0;
    for (int i = 0; i < 64 && !rc; i++) rc = H->padSet(i, p[(size_t)i].data(), nullptr, (int64_t)p[(size_t)i].size(), SR); CHECK(rc == 0 && H->padsPublish() == 0, "set");
    thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); H.run(48000, Q, fr);
    auto q = tone(480000, 0.05), q2 = tone(480000, 0.06);
    const int rc1 = H->padSet(0, q.data(), nullptr, 480000, SR); CHECK(rc1 == 0, "first replacement fits (%d)", rc1); H->padsPublish(); H.run(Q * 2, Q, fr);
    const int rc2 = H->padSet(0, q2.data(), nullptr, 480000, SR);
    const auto st = stats(H.h); CHECK(rc2 == THUNDER_LIVE_E_LIMIT || st.pad_retired == 0, "second replacement: E_LIMIT while the old buffer is held by a voice (%d, %d retired, %llu bytes)", rc2, st.pad_retired, (unsigned long long)st.pad_bytes_retired);
    if (rc2 == THUNDER_LIVE_E_LIMIT) { H->stop(THUNDER_LIVE_NOW); H.run(48000, Q, fr); H->collect(); H.run(Q, Q, fr); H->collect(); CHECK(stats(H.h).pad_retired == 0, "voices gone, collected"); CHECK(H->padSet(0, q2.data(), nullptr, 480000, SR) == 0, "the retry succeeds"); } }
}

static void take() {
  Host H(opts()); PadSet ps; H.loadPads(ps); H.song(electro(), 1); int64_t fr = 0; H.run(Q, Q, fr);
  const int N = PadSet::NS; float* blk = new (std::nothrow) float[(size_t)N]; for (int i = 0; i < N; i++) blk[i] = ps.pad(3)[i];
  const int b0 = stats(H.h).pad_buffers; const uint64_t h0 = stats(H.h).pad_bytes_held;
  CHECK(H->padTake(3, blk, false, N, SR) == 0, "take (the same samples as pad 3 has: no identical-data shortcut)"); H->padsPublish(); H.run(Q, Q, fr); H->collect(); H.run(Q, Q, fr); H->collect();
  CHECK(stats(H.h).pad_buffers == b0 && stats(H.h).pad_bytes_held == h0, "the old pad was retired and freed, the block is the pad now (%d buffers, %llu bytes)", stats(H.h).pad_buffers, (unsigned long long)stats(H.h).pad_bytes_held);
  // errors leave the block with the caller
  float* bad = new float[(size_t)N]; for (int i = 0; i < N; i++) bad[i] = 0.1f; bad[777] = NAN;
  CHECK(H->padTake(4, bad, false, N, SR) == THUNDER_LIVE_E_ARG, "NaN: E_ARG, the block stays the caller's"); bad[777] = 0.2f; CHECK(H->padTake(4, bad, false, N, SR) == 0, "the same block, repaired, is taken now");
  float* z = new float[16]; CHECK(H->padTake(-1, z, false, 16, SR) == THUNDER_LIVE_E_ARG && H->padTake(256, z, false, 16, SR) == THUNDER_LIVE_E_ARG && H->padTake(1, nullptr, false, 16, SR) == THUNDER_LIVE_E_ARG && H->padTake(1, z, false, 0, SR) == THUNDER_LIVE_E_ARG && H->padTake(1, z, false, 16, NAN) == THUNDER_LIVE_E_ARG && H->padTake(1, z, false, 16, 0) == THUNDER_LIVE_E_ARG, "take args"); delete[] z;
  { thunder_live_opts o = opts(); o.max_pad_bytes = 1000; Host G(o); float* big = new float[1000]; for (int i = 0; i < 1000; i++) big[i] = 0.1f; CHECK(G->padTake(0, big, false, 1000, SR) == THUNDER_LIVE_E_LIMIT, "over budget: E_LIMIT, the block is the caller's"); delete[] big; }
  // stereo: all of L, then all of R
  float* st2 = new float[2 * 1000]; for (int i = 0; i < 1000; i++) { st2[i] = 0.1f; st2[1000 + i] = 0.2f; } CHECK(H->padTake(6, st2, true, 1000, SR) == 0, "stereo take"); H->padsPublish(); H.run(Q, Q, fr);
  // a taken pad sounds as a copied one: two hosts, the same song, one set, one taken: bit-equal output
  Host A(opts()), B(opts()); A.song(electro(), 1); B.song(electro(), 1);
  for (int p = 0; p < PadSet::PN; p++) { A->padSet(p, ps.pad(p), nullptr, N, SR); A->padKit(p, PadSet::choke(p), PadSet::syn(p) ? 1 : 0, PadSet::rel(p));
    float* m = new (std::nothrow) float[(size_t)N]; std::memcpy(m, ps.pad(p), (size_t)N * 4); if (B->padTake(p, m, false, N, SR)) delete[] m; B->padKit(p, PadSet::choke(p), PadSet::syn(p) ? 1 : 0, PadSet::rel(p)); }
  A->padsPublish(); B->padsPublish(); thunder_live_play_opts po = playOpts(0); A->play(po); B->play(po); std::vector<float> la, ra, lb, rb; int64_t fa = 0, fb = 0; A.run(la, ra, 48000 * 4, 512, fa); B.run(lb, rb, 48000 * 4, 512, fb);
  CHECK(la == lb && ra == rb, "a taken pad and a copied pad: bit-equal output"); double pk = 0; for (float x : la) pk = std::max(pk, (double)std::fabs(x)); CHECK(pk > 0.05, "and it sounds (%g)", pk);
}

static void ringAndRecord() {
  Host H(opts()); int64_t fr = 0; PadSet ps; H.loadPads(ps, 4); H.run(Q, Q, fr); auto r = H.poll(); int np = 0; int64_t id0 = 0; for (auto& x : r) if (evType(x) == THUNDER_LIVE_EV_PADS) { np++; id0 = std::max(id0, evAs<thunder_live_ev_pads>(x).id); }
  CHECK(np == 1 && id0 == 2, "one PADS record for the newest snapshot adopted in the quantum (the empty one of create is covered): %d, id %lld", np, (long long)id0);
  CHECK(H->padsPublish() == 0, "nothing staged: 0"); H.run(Q, Q, fr); CHECK(H.poll().empty(), "no record for a publish that changed nothing");
  // the ring: 16 snapshots may wait; the 17th is E_BUSY with the staging kept
  int ok = 0, busy = 0; for (int i = 0; i < 20; i++) { H->padKit(0, i % 200, 0, 0); const int rc = H->padsPublish(); if (rc == 0) ok++; else if (rc == THUNDER_LIVE_E_BUSY) busy++; }
  CHECK(ok == 16 && busy == 4, "16 wait, then E_BUSY (%d, %d)", ok, busy); H.run(Q, Q, fr); H->collect(); CHECK(H->padsPublish() == 0, "after the audio side adopted: the kept staging publishes"); H.run(Q, Q, fr);
  r = H.poll(); int last = -1; for (auto& x : r) if (evType(x) == THUNDER_LIVE_EV_PADS) last = (int)evAs<thunder_live_ev_pads>(x).id; CHECK(last > 0, "a PADS record for the newest (%d)", last);
  CHECK(H->padSet(-1, nullptr, nullptr, 0, SR) == THUNDER_LIVE_E_ARG && H->padSet(256, nullptr, nullptr, 0, SR) == THUNDER_LIVE_E_ARG && H->padKit(0, 256, 0, 0) == THUNDER_LIVE_E_ARG && H->padKit(0, 0, 2, 0) == THUNDER_LIVE_E_ARG && H->padKit(0, 0, 0, -1) == THUNDER_LIVE_E_ARG && H->padRoute(0, 3) == THUNDER_LIVE_E_ARG, "pad args");
  auto t = tone(100, 0.1); t[50] = INFINITY; CHECK(H->padSet(0, t.data(), nullptr, 100, SR) == THUNDER_LIVE_E_ARG && H->padSet(0, t.data(), nullptr, 100, 0) == THUNDER_LIVE_E_ARG && H->padSet(0, nullptr, nullptr, 100, SR) == THUNDER_LIVE_E_ARG, "pad_set: non-finite, rate, null");
}

int main() {
  replaceWhileSounding(); budget(); take(); ringAndRecord();
  std::printf("live_pads: %d checks, %d failed\n", g_checks, g_fail); return g_fail ? 1 : 0;
}
