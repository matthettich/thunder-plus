// U37 P2: LiveHost event records: sizes and layouts, `seq` numbering, the merge of the two rings by seq in poll, the critical ring (64) never pushed out by the ordinary one (1024), drops counted and visible as gaps, poll with a small buffer.
#include "live_common.h"
using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;

static_assert(sizeof(thunder_live_ev_head) == 16 && sizeof(thunder_live_ev_clock) == 48 && sizeof(thunder_live_ev_pos) == 32 && sizeof(thunder_live_ev_rows) == 24 && sizeof(thunder_live_ev_rowent) == 8, "layout");
static_assert(sizeof(thunder_live_ev_state) == 24 && sizeof(thunder_live_ev_song) == 24 && sizeof(thunder_live_ev_pads) == 24 && sizeof(thunder_live_ev_error) == 32 && sizeof(thunder_live_ev_late) == 40, "layout");

static bool critical(int t) { return t == THUNDER_LIVE_EV_STATE || t == THUNDER_LIVE_EV_HELD || t == THUNDER_LIVE_EV_SONG || t == THUNDER_LIVE_EV_PADS || t == THUNDER_LIVE_EV_ERROR; }
using Recs = std::vector<std::vector<uint8_t>>;

static void layoutsAndSeq() {
  Host H(opts()); PadSet ps; H.song(electro(), 7); H.loadPads(ps); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po);
  int64_t fr = 0; Recs all; for (int i = 0; i < 48000 * 12 / 4096; i++) { H.run(4096, 4096, fr); for (auto& r : H.poll()) all.push_back(r); }   // polled every ~85 ms
  uint32_t prev = 0; int n[16] = {0}; bool sizesOk = true, seqOk = true, alignOk = true; int64_t clockF = -1; bool clockF128 = true;
  for (auto& r : all) {
    thunder_live_ev_head h; std::memcpy(&h, r.data(), sizeof h); if (h.type >= 16) { sizesOk = false; continue; } n[h.type]++;
    if (h.bytes != r.size() || (h.bytes & 7)) alignOk = false;
    switch (h.type) {
      case THUNDER_LIVE_EV_CLOCK: sizesOk &= h.bytes == 48; { const auto c = evAs<thunder_live_ev_clock>(r); sizesOk &= c.sr == 48000 && c.voices >= 0; if (clockF >= 0 && h.f - clockF < 4000) clockF128 = false; clockF = h.f; clockF128 &= (h.f % Q) == 0; } break;
      case THUNDER_LIVE_EV_POS: sizesOk &= h.bytes == 32; break;
      case THUNDER_LIVE_EV_ROWS: { const auto rw = evAs<thunder_live_ev_rows>(r); int k = 0; for (int t = 0; t < 33; t++) k += (int)((rw.valid >> t) & 1); sizesOk &= h.bytes == 24 + 8 * k && k > 0; for (int t = 33; t < 64; t++) sizesOk &= ((rw.valid >> t) & 1) == 0; } break;
      case THUNDER_LIVE_EV_STATE: case THUNDER_LIVE_EV_SONG: case THUNDER_LIVE_EV_PADS: sizesOk &= h.bytes == 24; break;
      case THUNDER_LIVE_EV_ERROR: sizesOk &= h.bytes == 32; break;
      case THUNDER_LIVE_EV_LATE: sizesOk &= h.bytes == 40; break;
    }
    if (h.seq != prev + 1) seqOk = false; prev = h.seq;
  }
  CHECK(sizesOk && alignOk, "record sizes and layouts (%zu records)", all.size()); CHECK(seqOk, "seq counts from 1 with no gap when polled in time (last %u)", prev);
  CHECK(clockF128, "CLOCK records at a quantum's first frame, 10 a second");
  CHECK(n[THUNDER_LIVE_EV_CLOCK] >= 115 && n[THUNDER_LIVE_EV_CLOCK] <= 125, "CLOCK 10 a second over 12 s: %d", n[THUNDER_LIVE_EV_CLOCK]);
  CHECK(n[THUNDER_LIVE_EV_POS] >= 100 && n[THUNDER_LIVE_EV_POS] <= 365, "POS at most 30 a second (one per step that moved): %d over 12 s", n[THUNDER_LIVE_EV_POS]);
  CHECK(n[THUNDER_LIVE_EV_ROWS] > 100, "ROWS per tracker row: %d", n[THUNDER_LIVE_EV_ROWS]);
  CHECK(n[THUNDER_LIVE_EV_SONG] == 1 && n[THUNDER_LIVE_EV_PADS] == 1 && n[THUNDER_LIVE_EV_STATE] == 1, "one SONG, one PADS, one STATE(PLAYING): %d %d %d", n[THUNDER_LIVE_EV_SONG], n[THUNDER_LIVE_EV_PADS], n[THUNDER_LIVE_EV_STATE]);
  for (auto& r : all) if (evType(r) == THUNDER_LIVE_EV_SONG) CHECK(evAs<thunder_live_ev_song>(r).rev == 7, "SONG rev 7");
  for (auto& r : all) if (evType(r) == THUNDER_LIVE_EV_PADS) CHECK(evAs<thunder_live_ev_pads>(r).id >= 1, "PADS id %lld", (long long)evAs<thunder_live_ev_pads>(r).id);
  // POS: row, step, cr and the tempo x 10
  for (auto& r : all) if (evType(r) == THUNDER_LIVE_EV_POS) { const auto p = evAs<thunder_live_ev_pos>(r); CHECK(p.row >= 0 && p.step >= 0 && p.bpm10 > 0, "POS fields %d %d %d", p.row, p.step, p.bpm10); break; }
  const auto st = stats(H.h); CHECK(st.evt_dropped == 0 && st.evt_crit_dropped == 0, "nothing dropped when polled in time");
}

// several publishes before one quantum: ONE SONG record, for the newest rev
static void songCoalesce() {
  Host H(opts()); H.song(electro(), 1); H.song(electro(), 2); H.song(electro(), 3); int64_t fr = 0; H.run(Q, Q, fr);
  auto r = H.poll(); int n = 0; int64_t rev = 0; for (auto& x : r) if (evType(x) == THUNDER_LIVE_EV_SONG) { n++; rev = evAs<thunder_live_ev_song>(x).rev; } CHECK(n == 1 && rev == 3, "one SONG record for rev 3 (%d, %lld)", n, (long long)rev);
}

// a consumer that does not poll: the ordinary ring overflows, the critical one keeps every record; the gap in seq says what was lost; the newest of a kind is refused
static void overflow() {
  Host H(opts()); PadSet ps; H.song(electro(), 1); H.loadPads(ps); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po);
  int64_t fr = 0; H.run(48000 * 60, Q, fr);   // ROWS + POS + CLOCK for 60 s, never polled
  auto st = stats(H.h); CHECK(st.evt_dropped > 0 && st.evt_crit_dropped == 0, "the ordinary ring overflowed (%u dropped), the critical did not (%u)", st.evt_dropped, st.evt_crit_dropped);
  auto all = H.poll(); int ord = 0, crit = 0; uint32_t prev = 0; bool ascending = true; int gaps = 0; int critTypes = 0;
  for (auto& r : all) { const int t = evType(r); if (critical(t)) { crit++; if (t == THUNDER_LIVE_EV_STATE) critTypes |= 1; if (t == THUNDER_LIVE_EV_SONG) critTypes |= 2; if (t == THUNDER_LIVE_EV_PADS) critTypes |= 4; } else ord++; const uint32_t s = evSeq(r); if (s <= prev) ascending = false; if (s != prev + 1) gaps++; prev = s; }
  CHECK(crit == 3 && critTypes == 7, "the three critical records (PADS, SONG, STATE) all there: %d", crit); CHECK(ord == 1024, "the ordinary ring holds its 1024 (%d)", ord);
  CHECK(ascending, "merged in ascending seq (%d gaps inside: the refused records are the newest, so the kept ones are a prefix)", gaps);
  const uint32_t lastSeq = all.empty() ? 0 : evSeq(all.back()); { int64_t f2 = fr; H.run(48000, Q, f2); fr = f2; auto more = H.poll(); CHECK(!more.empty() && evSeq(more[0]) > lastSeq + 1, "the gap after the drop: seq %u follows %u", more.empty() ? 0 : evSeq(more[0]), lastSeq); }
  // the oldest are kept: the first ordinary seq is early (the CLOCK of quantum 0 or the first ROWS), the newest were refused
  CHECK(!all.empty() && evSeq(all[0]) <= 3, "the oldest kept (first seq %u)", all.empty() ? 0 : evSeq(all[0]));
  // the next CLOCK carries the total
  H.run(48000, Q, fr); thunder_live_ev_clock last{}; bool have = false; for (auto& r : H.poll()) if (evType(r) == THUNDER_LIVE_EV_CLOCK) { last = evAs<thunder_live_ev_clock>(r); have = true; }
  CHECK(have && last.evt_dropped >= st.evt_dropped, "CLOCK.evt_dropped %u >= %u", last.evt_dropped, st.evt_dropped);
}
static void critOverflow() {
  Host H(opts()); int64_t fr = 0; const std::string s = electro(); for (int i = 1; i <= 70; i++) { CHECK(H.song(s, i) == 0, "publish %d", i); H.run(Q, Q, fr); }   // 70 SONG records, never polled
  auto st = stats(H.h); CHECK(st.evt_crit_dropped == 7 && st.evt_dropped >= 7, "71 critical records (70 SONG and the PADS of create) into 64: 7 refused (%u, total %u)", st.evt_crit_dropped, st.evt_dropped);
  auto all = H.poll(); int n = 0; int64_t first = 0, last = 0; for (auto& r : all) if (evType(r) == THUNDER_LIVE_EV_SONG) { if (!n) first = evAs<thunder_live_ev_song>(r).rev; last = evAs<thunder_live_ev_song>(r).rev; n++; }
  CHECK(n == 63 && first == 1 && last == 63, "the first 63 SONG (and the PADS) are kept, the newest refused (%d: %lld .. %lld)", n, (long long)first, (long long)last);
  const uint32_t lastSeq = all.empty() ? 0 : evSeq(all.back());
  H.song(s, 71); H.run(Q, Q, fr); all = H.poll(); int sg = 0; for (auto& r : all) if (evType(r) == THUNDER_LIVE_EV_SONG) { sg++; CHECK(evSeq(r) >= lastSeq + 7, "the gap: seq %u after %u", evSeq(r), lastSeq); } CHECK(sg == 1, "a record fits again after the poll");
}

// poll with small buffers
static void smallBuffer() {
  Host H(opts()); int64_t fr = 0; H.song(electro(), 1); H.run(Q, Q, fr);   // a SONG record (24 bytes) waits
  uint8_t buf[512];
  CHECK(H->poll(nullptr, 512) == THUNDER_LIVE_E_ARG, "null buffer"); CHECK(H->poll(buf, -1) == THUNDER_LIVE_E_ARG, "negative cap");
  CHECK(H->poll(buf, 8) == THUNDER_LIVE_E_ARG && H->poll(buf, 0) == THUNDER_LIVE_E_ARG && H->poll(buf, 23) == THUNDER_LIVE_E_ARG, "cap too small for the next record: E_ARG, it stays queued");
  const int n = H->poll(buf, 24); CHECK(n == 24, "exactly one record fits (%d)", n); thunder_live_ev_head h; std::memcpy(&h, buf, sizeof h); CHECK(h.type == THUNDER_LIVE_EV_SONG && h.seq >= 1, "it is the record that stayed (type %d)", h.type);
  // many records, a cap that takes a few: whole records only, in order, nothing lost
  PadSet ps; H.loadPads(ps); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); H->play(po); H.run(48000 * 2, Q, fr);
  uint32_t prev = h.seq; int total = 0; bool ok = true, whole = true; for (int guard = 0; guard < 5000; guard++) {
    const int k = H->poll(buf, 330); if (k == 0) break; if (k < 0) { ok = false; break; } int o = 0; while (o < k) { thunder_live_ev_head x; std::memcpy(&x, buf + o, sizeof x); if (x.bytes < 16 || o + x.bytes > k) { whole = false; break; } if (x.seq <= prev) ok = false; prev = x.seq; o += x.bytes; total++; } if (!whole) break; }
  CHECK(ok && whole && total > 50, "polled in 330-byte bites: whole records, ascending (%d records)", total);
  CHECK(H->poll(buf, 512) == 0, "empty: 0");
}

int main() {
  layoutsAndSeq(); songCoalesce(); overflow(); critOverflow(); smallBuffer();
  std::printf("live_events: %d checks, %d failed\n", g_checks, g_fail); return g_fail ? 1 : 0;
}
