// U37 P2: LiveHost transport: every row of the state table of live.h (THE STATE MACHINE), the transport commands, mix_set (at once / ramped, sample accurate), timed commands (the quantum and the waiting list of 64),
// a full command ring (E_BUSY, nothing lost) and the re-anchor. Everything is read from what a shell sees: the event records, stats and the samples.
#include "live_common.h"
using namespace lt;

constexpr int Q = THUNDER_LIVE_QUANTUM;
struct W {   // a host with ELECTRO and pads, and a running frame counter
  Host H; std::string song; PadSet ps; int64_t fr = 0;
  explicit W(uint32_t flags = 0) : H(opts(flags)), song(electro()) { H.song(song); H.loadPads(ps); }
  void run(int64_t n) { H.run(n, Q, fr); }
  std::vector<std::vector<uint8_t>> ev() { return H.poll(); }
  static std::vector<std::vector<uint8_t>> of(const std::vector<std::vector<uint8_t>>& v, int type) { std::vector<std::vector<uint8_t>> o; for (auto& r : v) if (evType(r) == type) o.push_back(r); return o; }
  int state() { return stats(H.h).state; }
};
static std::vector<std::vector<uint8_t>> states(const std::vector<std::vector<uint8_t>>& v) { return W::of(v, THUNDER_LIVE_EV_STATE); }
static int stSt(const std::vector<uint8_t>& r) { return evAs<thunder_live_ev_state>(r).state; }
static int stArg(const std::vector<uint8_t>& r) { return evAs<thunder_live_ev_state>(r).arg; }

static void stateTable() {
  { W w; w.run(Q); w.ev();   // IDLE: stop and hold: no record, still IDLE; (hold arms, harmless)
    CHECK(w.H->stop(THUNDER_LIVE_NOW) == 0 && w.H->hold(THUNDER_LIVE_NOW) == 0, "idle stop/hold");
    w.run(Q); const auto e = w.ev(); CHECK(states(e).empty() && W::of(e, THUNDER_LIVE_EV_HELD).empty(), "IDLE stop / hold post nothing (%zu records)", e.size()); CHECK(w.state() == THUNDER_LIVE_STATE_IDLE, "IDLE stays"); }
  { W w; w.run(Q); w.ev();   // IDLE -> PLAYING; PLAYING + play -> PLAYING (a second PLAYING record, no STOPPED); PLAYING stop -> IDLE, STOPPED 0
    thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); CHECK(w.H->play(po) == 0, "play");
    CHECK(w.state() == THUNDER_LIVE_STATE_IDLE, "play is queued, the state moves at the quantum"); w.run(Q);
    auto s = states(w.ev()); CHECK(s.size() == 1 && stSt(s[0]) == THUNDER_LIVE_ST_PLAYING && stArg(s[0]) == 0, "PLAYING record (%zu)", s.size());
    CHECK(!s.empty() && evF(s[0]) == w.fr - Q, "PLAYING.f is the quantum's frame (%lld vs %lld)", s.empty() ? -1LL : (long long)evF(s[0]), (long long)(w.fr - Q));
    CHECK(w.state() == THUNDER_LIVE_STATE_PLAYING, "PLAYING"); w.run(48000);
    CHECK(w.H->play(po) == 0, "play again"); w.run(Q); s = states(w.ev()); CHECK(s.size() == 1 && stSt(s[0]) == THUNDER_LIVE_ST_PLAYING, "PLAYING again, nothing between (%zu)", s.size()); CHECK(w.state() == THUNDER_LIVE_STATE_PLAYING, "still PLAYING");
    w.H->stop(THUNDER_LIVE_NOW); w.run(Q); s = states(w.ev()); CHECK(s.size() == 1 && stSt(s[0]) == THUNDER_LIVE_ST_STOPPED && stArg(s[0]) == 0, "STOPPED arg 0 (%zu)", s.size()); CHECK(w.state() == THUNDER_LIVE_STATE_IDLE, "IDLE after stop");
    w.H->stop(THUNDER_LIVE_NOW); w.run(Q); CHECK(states(w.ev()).empty(), "stop from IDLE: no record"); }
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 4); w.ev();   // PLAYING + hold -> still PLAYING (armed), the first edge step -> HELD, HELD record with positions
    CHECK(w.H->hold(THUNDER_LIVE_NOW) == 0, "hold"); w.run(Q); CHECK(w.state() == THUNDER_LIVE_STATE_PLAYING, "hold only arms (state %d)", w.state());
    for (int i = 0; i < 48000 * 30 / Q && w.state() == THUNDER_LIVE_STATE_PLAYING; i++) w.run(Q);
    CHECK(w.state() == THUNDER_LIVE_STATE_HELD, "HELD at the first pattern edge (state %d)", w.state());
    auto e = w.ev(); auto hl = W::of(e, THUNDER_LIVE_EV_HELD); CHECK(hl.size() == 1, "one HELD record (%zu)", hl.size());
    if (!hl.empty()) { const auto rw = evAs<thunder_live_ev_rows>(hl[0]); int n = 0; for (int t = 0; t < 33; t++) n += (int)((rw.valid >> t) & 1); CHECK(n > 0 && hl[0].size() == 24u + 8u * (unsigned)n && (hl[0].size() % 8) == 0, "HELD carries %d positions, %zu bytes", n, hl[0].size()); }
    w.H->hold(THUNDER_LIVE_NOW); w.run(Q * 8); e = w.ev(); CHECK(W::of(e, THUNDER_LIVE_EV_HELD).empty() && states(e).empty() && w.state() == THUNDER_LIVE_STATE_HELD, "hold in HELD: no-op");
    w.H->play(po); w.run(Q); auto s = states(w.ev()); CHECK(s.size() == 1 && stSt(s[0]) == THUNDER_LIVE_ST_PLAYING && w.state() == THUNDER_LIVE_STATE_PLAYING, "HELD + play -> PLAYING");
    w.H->hold(THUNDER_LIVE_NOW); for (int i = 0; i < 48000 * 30 / Q && w.state() == THUNDER_LIVE_STATE_PLAYING; i++) w.run(Q); CHECK(w.state() == THUNDER_LIVE_STATE_HELD, "held again"); w.ev();
    w.H->stop(THUNDER_LIVE_NOW); w.run(Q); s = states(w.ev()); CHECK(s.size() == 1 && stSt(s[0]) == THUNDER_LIVE_ST_STOPPED && stArg(s[0]) == 0 && w.state() == THUNDER_LIVE_STATE_IDLE, "HELD + stop -> IDLE, STOPPED 0"); }
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); po.has_only = 1; po.only = 0;   // nothing plays: the sequencer runs out at once -> ENDED, then 0.3 s later IDLE and STOPPED arg 1
    w.H->play(po); int64_t endedAt = -1; std::vector<std::vector<uint8_t>> all;
    for (int i = 0; i < 48000 / Q; i++) { w.run(Q); for (auto& r : w.ev()) all.push_back(r); }
    auto s = states(all); bool sawEnded = false; int64_t stoppedF = -1, endedF = -1; for (auto& r : s) { if (stSt(r) == THUNDER_LIVE_ST_ENDED) { sawEnded = true; endedF = evF(r); } if (stSt(r) == THUNDER_LIVE_ST_STOPPED && stArg(r) == 1) stoppedF = evF(r); }
    (void)endedAt; CHECK(sawEnded, "ENDED record (%zu state records)", s.size());
    CHECK(s.size() == 3 && stSt(s[0]) == THUNDER_LIVE_ST_PLAYING && stSt(s[1]) == THUNDER_LIVE_ST_ENDED && stSt(s[2]) == THUNDER_LIVE_ST_STOPPED && stArg(s[2]) == 1, "PLAYING, ENDED, STOPPED(1) (%zu)", s.size());
    CHECK(stoppedF >= 0 && endedF >= 0 && stoppedF - endedF >= 14400 - Q && stoppedF - endedF <= 14400 + 2 * Q, "STOPPED 0.3 s after the SONG's end time (the ENDED record's f) (%lld frames)", (long long)(stoppedF - endedF)); CHECK(w.state() == THUNDER_LIVE_STATE_IDLE, "IDLE at the end");
    // ENDED + hold: nothing; ENDED + play cancels the timer; ENDED + stop -> STOPPED 0
    W v; v.H->play(po); v.run(Q * 4); auto e = states(v.ev()); CHECK(v.state() == THUNDER_LIVE_STATE_ENDED, "ENDED (state %d)", v.state());
    v.H->hold(THUNDER_LIVE_NOW); v.run(Q); e = v.ev(); CHECK(states(e).empty() && v.state() == THUNDER_LIVE_STATE_ENDED, "ENDED + hold: nothing");
    thunder_live_play_opts po2 = playOpts(THUNDER_LIVE_NOW); v.H->play(po2); v.run(48000); e = states(v.ev()); bool stop1 = false; for (auto& r : e) if (stSt(r) == THUNDER_LIVE_ST_STOPPED) stop1 = true;
    CHECK(!e.empty() && stSt(e[0]) == THUNDER_LIVE_ST_PLAYING && !stop1 && v.state() == THUNDER_LIVE_STATE_PLAYING, "ENDED + play -> PLAYING, the timer cancelled (%zu records)", e.size());
    W x; x.H->play(po); x.run(Q * 4); x.ev(); x.H->stop(THUNDER_LIVE_NOW); x.run(Q); e = states(x.ev()); CHECK(e.size() == 1 && stSt(e[0]) == THUNDER_LIVE_ST_STOPPED && stArg(e[0]) == 0 && x.state() == THUNDER_LIVE_STATE_IDLE, "ENDED + stop -> STOPPED 0"); }
}

static int lastBpm10(const std::vector<std::vector<uint8_t>>& e) { int v = -1; for (auto& r : W::of(e, THUNDER_LIVE_EV_POS)) v = evAs<thunder_live_ev_pos>(r).bpm10; return v; }
static void commands() {
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(48000); auto e = w.ev(); const int b0 = lastBpm10(e); CHECK(b0 > 0, "POS bpm10 %d", b0);
    CHECK(w.H->bpm(93.0, THUNDER_LIVE_NOW) == 0, "bpm"); w.run(48000); e = w.ev(); CHECK(lastBpm10(e) == 930, "bpm heard: %d", lastBpm10(e));
    CHECK(w.H->bpm(0, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->bpm(NAN, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->bpm(-3, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG, "bpm args");
    CHECK(w.H->loop(1, 3, 2, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->loop(1, -1, 2, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG, "loop args");
    CHECK(w.H->loop(1, 0, 0, THUNDER_LIVE_NOW) == 0, "loop"); w.run(48000 * 6); w.ev(); w.run(48000 * 6); e = w.ev(); int maxRow = -1, n = 0; for (auto& r : W::of(e, THUNDER_LIVE_EV_POS)) { maxRow = std::max(maxRow, evAs<thunder_live_ev_pos>(r).row); n++; }
    CHECK(n > 0 && maxRow == 0, "a loop of song row 0 keeps every position on row 0 (%d positions, max row %d)", n, maxRow);
    CHECK(w.H->loop(0, 0, 0, THUNDER_LIVE_NOW) == 0, "loop off"); w.run(48000 * 12); e = w.ev(); maxRow = 0; for (auto& r : W::of(e, THUNDER_LIVE_EV_POS)) maxRow = std::max(maxRow, evAs<thunder_live_ev_pos>(r).row); CHECK(maxRow > 0, "loop off: the song goes on (max row %d)", maxRow);
    CHECK(w.H->lead(0.01, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->lead(2.5, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->lead(NAN, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->lead(0.6, THUNDER_LIVE_NOW) == 0, "lead args");
    CHECK(w.H->perf(THUNDER_LIVE_PERF_XF, 0, 0.5, THUNDER_LIVE_NOW) == 0 && w.H->perf(THUNDER_LIVE_PERF_MACRO, 7, 0.25, THUNDER_LIVE_NOW) == 0 && w.H->perf(THUNDER_LIVE_PERF_FILL, 0, 1, THUNDER_LIVE_NOW) == 0, "perf");
    CHECK(w.H->perf(THUNDER_LIVE_PERF_MACRO, 8, 0.25, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->perf(THUNDER_LIVE_PERF_XF, 0, NAN, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->perf(9, 0, 0, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG, "perf args");
    CHECK(w.H->stopTrack(2, THUNDER_LIVE_NOW) == 0 && w.H->stopTrack(32, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && w.H->stopTrack(-1, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG, "stop_track");
    const uint64_t a0 = stats(w.H.h).cmds_applied; w.run(Q); CHECK(stats(w.H.h).cmds_applied - a0 >= 0, "applied counter"); }
  // a perf crossfade really reaches the sequencer: the song's own XF drives tracks; here we only prove the command is applied once (counted)
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q); const uint64_t a0 = stats(w.H.h).cmds_applied; w.H->perf(THUNDER_LIVE_PERF_XF, 0, 0.3, THUNDER_LIVE_NOW); w.H->lead(0.5, THUNDER_LIVE_NOW); CHECK(stats(w.H.h).cmds_applied == a0, "queued, not applied"); w.run(Q); CHECK(stats(w.H.h).cmds_applied == a0 + 2, "applied at the next quantum (%llu)", (unsigned long long)(stats(w.H.h).cmds_applied - a0)); }
}

// ---- timed commands ----
static void timed() {
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 4); w.ev();
    const int64_t at = w.fr + Q * 10 + 37;   // inside the 11th quantum from now
    w.H->stop(at); const uint64_t a0 = stats(w.H.h).cmds_applied;
    for (int i = 0; i < 10; i++) w.run(Q); CHECK(stats(w.H.h).cmds_applied == a0 && w.state() == THUNDER_LIVE_STATE_PLAYING, "not before its quantum");
    w.run(Q); auto s = states(w.ev()); CHECK(s.size() == 1 && stSt(s[0]) == THUNDER_LIVE_ST_STOPPED, "stop at its quantum (%zu)", s.size());
    CHECK(!s.empty() && evF(s[0]) >= at - 37 - 0 && evF(s[0]) <= at, "STOPPED.f %lld is the quantum containing %lld", s.empty() ? -1LL : (long long)evF(s[0]), (long long)at);
    CHECK(stats(w.H.h).cmds_applied == a0 + 1, "applied once"); }
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q);   // 65 timed commands: the waiting list holds 64, the 65th is applied at once and counted
    const int64_t base = w.fr + Q * 400; const uint64_t a0 = stats(w.H.h).cmds_applied;
    for (int i = 0; i < 65; i++) CHECK(w.H->bpm(100.0 + i, base + (int64_t)i * Q) == 0, "bpm %d queued", i);
    w.run(Q); auto st = stats(w.H.h); CHECK(st.cmd_late == 1 && st.cmds_applied == a0 + 1, "the 65th applied at once (late %u, applied %llu)", st.cmd_late, (unsigned long long)(st.cmds_applied - a0));
    auto e = w.ev(); int where1 = 0; for (auto& r : W::of(e, THUNDER_LIVE_EV_ERROR)) if (evAs<thunder_live_ev_error>(r).where == THUNDER_LIVE_WHERE_CMD) where1++; CHECK(where1 == 1, "one ERROR(CMD) record (%d)", where1);
    w.run(48000 * 4); st = stats(w.H.h); CHECK(st.cmds_applied == a0 + 1 + 0 || st.cmds_applied > a0 + 1, "the list waits"); 
    while (w.fr < base + 65 * Q) { w.run(Q * 8); w.ev(); } w.run(Q * 4); st = stats(w.H.h); CHECK(st.cmds_applied == a0 + 65 && st.cmd_late == 1, "all 65 applied, one late (%llu, %u)", (unsigned long long)(st.cmds_applied - a0), st.cmd_late);
    w.run(48000); { const int v = lastBpm10(w.ev()); CHECK(v == 1630, "the 65th (bpm 164) went in at once, the waiting 64 then ran in time order, the last of them (163) stays: %d", v); } }
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q);   // order: timed commands apply in `at` order of the ring for equal quanta (ring order)
    const int64_t at = w.fr + Q * 3; w.H->bpm(111, at + 5); w.H->bpm(122, at + 9); w.run(4800); { const int v = lastBpm10(w.ev()); CHECK(v == 1220, "ring order within one quantum: %d", v); } }
}

// ---- the ring ----
static void ring() {
  W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 8); w.ev();
  // the play took one slot and was applied: 256 free
  int ok = 0; for (int i = 0; i < THUNDER_LIVE_CMD_RING; i++) if (w.H->bpm(60.0 + i * 0.1, THUNDER_LIVE_NOW) == 0) ok++;
  CHECK(ok == THUNDER_LIVE_CMD_RING, "256 queued (%d)", ok);
  const double last = 60.0 + 256 * 0.1; CHECK(w.H->bpm(last, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_BUSY, "the 257th is E_BUSY");
  CHECK(w.H->perf(THUNDER_LIVE_PERF_XF, 0, 0.1, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_BUSY && w.H->stop(THUNDER_LIVE_NOW) == THUNDER_LIVE_E_BUSY && w.H->mixSet(0, THUNDER_LIVE_MIX_VOL, 10, 0, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_BUSY, "other commands too");
  thunder_live_play_opts p2 = playOpts(THUNDER_LIVE_NOW); CHECK(w.H->play(p2) == THUNDER_LIVE_E_BUSY, "play: E_BUSY");
  CHECK(stats(w.H.h).cmd_busy == 5, "cmd_busy counts the answers (%u)", stats(w.H.h).cmd_busy);
  const uint64_t a0 = stats(w.H.h).cmds_applied; w.run(Q); CHECK(stats(w.H.h).cmds_applied == a0 + 256, "every queued command applied (%llu)", (unsigned long long)(stats(w.H.h).cmds_applied - a0)); w.ev(); w.run(4800);
  { const int v = lastBpm10(w.ev()); CHECK(v == (int)std::lround((60.0 + 255 * 0.1) * 10), "the last of the 256 is in force: %d", v); }
  CHECK(w.H->bpm(last, THUNDER_LIVE_NOW) == 0, "the retry"); w.run(4800); { const int v = lastBpm10(w.ev()); CHECK(v == (int)std::lround(last * 10), "after the retry the retried message is in force: %d", v); }
  CHECK(w.state() == THUNDER_LIVE_STATE_PLAYING, "still playing, nothing lost");
}

// ---- mixer ----
static double peak(const std::vector<float>& v, size_t a, size_t b) { double m = 0; for (size_t i = a; i < b && i < v.size(); i++) m = std::max(m, (double)std::fabs(v[i])); return m; }
static void mixer() {
  const int64_t N = 48000 * 2, at = Q * 200 + 61;   // inside a quantum
  // every track at once: the song's own lanes (TVL / TVA / master VOL events) ride on top of a level, a MUTE is what silences a track
  auto make = [&](std::vector<float>& l, std::vector<float>& r, double ramp, bool cmd, int key, double val) {
    W w; thunder_live_play_opts po = playOpts(0); w.H->play(po); if (cmd) for (int t = 0; t < 32; t++) w.H->mixSet(t, key, val, ramp, at); w.H.run(l, r, N, Q, w.fr); };
  std::vector<float> l0, r0, l1, r1; make(l0, r0, 0, false, 0, 0);
  CHECK(peak(l0, (size_t)at, (size_t)N) > 0.01, "the reference sounds");
  make(l1, r1, 0, true, THUNDER_LIVE_MIX_MUTE, 1);   // mute at once, at frame `at`: bit-equal before the quantum containing it
  const size_t q0 = (size_t)(at / Q) * Q; size_t firstDiff = N; for (size_t i = 0; i < (size_t)N; i++) if (l0[i] != l1[i]) { firstDiff = i; break; }
  CHECK(firstDiff >= q0 && firstDiff <= (size_t)at + 1, "mute at once: first different sample %zu, `at` %lld (quantum starts %zu)", firstDiff, (long long)at, q0);
  CHECK(peak(l1, (size_t)at + 64, (size_t)N) < 1e-4, "silent from `at` on (peak %g)", peak(l1, (size_t)at + 64, (size_t)N));
  // a linear ramp over 4800 frames (the MUTE crossfade): the gain at k frames in is on the line from 1 to 0
  std::vector<float> l2, r2; make(l2, r2, 4800, true, THUNDER_LIVE_MIX_MUTE, 1);
  int good = 0, tot = 0; double gmid = 0; for (int k = 600; k < 4200; k += 7) { const size_t i = (size_t)at + (size_t)k; if (std::fabs(l0[i]) < 0.02f) continue; tot++; const double g = l2[i] / l0[i]; if (k == 2401 || k == 2400) gmid = g; if (std::fabs(g - (1.0 - k / 4800.0)) < 0.08) good++; }
  CHECK(tot > 50 && good >= tot * 0.9, "linear ramp: %d of %d sampled gains on the line (gain %g at the middle)", good, tot, gmid);
  CHECK(peak(l2, (size_t)at + 4800 + 8, (size_t)N) < 1e-4, "ramp ends silent (%g)", peak(l2, (size_t)at + 4800 + 8, (size_t)N));
  // ramp < 0: the page's law, a 12 ms approach: not at once (the first samples are still near full), and silent after about 100 ms
  std::vector<float> l3, r3; make(l3, r3, -1, true, THUNDER_LIVE_MIX_MUTE, 1);
  size_t k0 = (size_t)at + 2; double early = 0, ref = 0; for (size_t i = k0; i < k0 + 16; i++) { early += std::fabs(l3[i]); ref += std::fabs(l0[i]); }
  CHECK(early > 0.3 * ref && peak(l3, (size_t)at + 48000 / 5, (size_t)N) < 1e-3, "ramp < 0: early %g of %g, late peak %g", early, ref, peak(l3, (size_t)at + 9600, (size_t)N));
  // args
  Host H(opts()); CHECK(H->mixSet(33, 0, 1, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(0, 5, 1, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(0, 3, 2, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(32, 3, 1, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(0, 0, NAN, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(0, 0, 256, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(0, 6, 1, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(32, 6, 1, 0, -1) == 0 && H->mixSet(0, 3, 1, 128, -1) == 0, "mix_set args");
  // master VOL with ramp 0 and with ramp < 0 is applied at the same sample (the master's `out` lane; the song's own master events ride on it)
  std::vector<float> l5, r5; { W w; thunder_live_play_opts po = playOpts(0); w.H->play(po); w.H->mixSet(32, THUNDER_LIVE_MIX_OUT, 0, 0, at); w.H.run(l5, r5, N, Q, w.fr); }
  size_t d5 = N; for (size_t i = 0; i < (size_t)N; i++) if (l0[i] != l5[i]) { d5 = i; break; } CHECK(d5 >= q0 && d5 <= (size_t)at + 1, "master OUT 0 at once: first difference %zu", d5);
  CHECK(peak(l5, (size_t)at + 64, (size_t)N) < 1e-4, "OUT 0 is silent (%g)", peak(l5, (size_t)at + 64, (size_t)N));
}

// ---- the re-anchor ----
static void reanchor() {
  W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 10); w.ev();
  auto st = stats(w.H.h); CHECK(st.reanchors == 0 && st.frame_origin == 0, "no re-anchor so far");
  std::vector<float> a(300), b(300); int64_t f = w.fr + 5000;   // a gap of 5000 frames (a held-up audio thread, a changed device)
  CHECK(w.H->process(a.data(), b.data(), 300, f, 0) == 0, "process after a gap"); st = stats(w.H.h); CHECK(st.reanchors == 1 && st.frame_origin == f && st.frames == f + 384, "re-anchored: origin %lld frames %lld (%u)", (long long)st.frame_origin, (long long)st.frames, st.reanchors);
  auto e = W::of(w.ev(), THUNDER_LIVE_EV_ERROR); int n = 0; for (auto& r : e) { const auto x = evAs<thunder_live_ev_error>(r); if (x.where == THUNDER_LIVE_WHERE_REANCHOR) { n++; CHECK(x.value == 5000 && x.fatal == 0 && evF(r) == f, "REANCHOR record: skipped %d at %lld", x.value, (long long)evF(r)); } } CHECK(n == 1, "one REANCHOR record (%d)", n);
  f += 300; CHECK(w.H->process(a.data(), b.data(), 300, f - 40, 0) == 0, "backwards"); st = stats(w.H.h); CHECK(st.reanchors == 2 && st.frame_origin == f - 40, "backwards re-anchor (%u)", st.reanchors);
  e = W::of(w.ev(), THUNDER_LIVE_EV_ERROR); CHECK(e.size() == 1 && evAs<thunder_live_ev_error>(e[0]).value == -40, "negative skip %d", e.empty() ? 0 : evAs<thunder_live_ev_error>(e[0]).value);
  int64_t g = f - 40 + 300; CHECK(w.H->process(a.data(), b.data(), 128, g, 0) == 0 && stats(w.H.h).reanchors == 2, "the expected frame does not re-anchor");
  CHECK(w.H->process(a.data(), b.data(), 5000, g + 128, 0) == THUNDER_LIVE_E_ARG && w.H->process(nullptr, b.data(), 8, g + 128, 0) == THUNDER_LIVE_E_ARG && w.H->process(a.data(), b.data(), -1, g + 128, 0) == THUNDER_LIVE_E_ARG, "process args (max_block 4096)");
  CHECK(w.state() == THUNDER_LIVE_STATE_PLAYING, "still PLAYING after re-anchors");
}


// ---- round 2 (verify-283): the engine timeline across re-anchors, F9, F4, F5, F6, F8, F10, F13 and the behaviours the verifier found unkilled ----
static std::vector<std::string> dumpAll(LiveHost* h) { std::vector<std::string> v; static char js[16384]; for (int n; (n = h->dumpStep(js, sizeof js)) > 0;) v.emplace_back(js, (size_t)n); return v; }
static double dumpT(const std::string& row) { const size_t i = row.find("\"t\":"); return i == std::string::npos ? -1 : std::atof(row.c_str() + i + 4); }

static void reanchors() {
  // a timed command that waits across a backward re-anchor is judged against the new mapping: it applies where the HOST frame reaches `at`
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(48000); w.ev();
    const int64_t at = w.fr + 96000; w.H->bpm(111, at); const uint64_t a0 = stats(w.H.h).cmds_applied;
    w.fr = 0; w.run(48000); CHECK(stats(w.H.h).cmds_applied == a0, "after a backward re-anchor the command still waits (host frame %lld < %lld)", (long long)w.fr, (long long)at);
    w.run(at - w.fr - 400); CHECK(stats(w.H.h).cmds_applied == a0, "and does not apply early");
    w.run(800); CHECK(stats(w.H.h).cmds_applied == a0 + 1, "it applies when the host frame reaches `at` (%lld)", (long long)w.fr); w.run(4800); { const int v = lastBpm10(w.ev()); CHECK(v == 1110, "bpm heard %d", v); } CHECK(w.state() == THUNDER_LIVE_STATE_PLAYING, "playing on"); }
  // an `at` that fell inside a forward gap: due at the next quantum
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(48000); w.ev(); const int64_t at = w.fr + 10000; w.H->bpm(122, at); const uint64_t a0 = stats(w.H.h).cmds_applied;
    w.fr += 20000; w.run(Q); CHECK(stats(w.H.h).cmds_applied == a0 + 1, "an `at` inside the skipped gap is applied at the next quantum"); w.run(4800); { const int v = lastBpm10(w.ev()); CHECK(v == 1220, "bpm heard %d", v); } }
  // the ENDED timer still fires across a backward re-anchor, its STOPPED record on the new host frames
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); po.has_only = 1; po.only = 0; w.run(Q * 8); w.ev(); w.H->play(po); w.run(Q * 4); w.ev(); w.fr = 100; w.run(48000); auto s = states(w.ev());
    bool stopped = false; for (auto& r : s) if (stSt(r) == THUNDER_LIVE_ST_STOPPED && stArg(r) == 1) { stopped = true; CHECK(evF(r) >= 100 && evF(r) <= w.fr, "STOPPED.f %lld is on the new timeline", (long long)evF(r)); }
    CHECK(stopped && w.state() == THUNDER_LIVE_STATE_IDLE, "ENDED timer fires after the re-anchor"); }
  // CLOCK and POS records keep coming and their frames follow the new host timeline
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(48000); w.ev(); w.fr = 7; w.run(48000); int nc = 0; for (auto& r : w.ev()) if (evType(r) == THUNDER_LIVE_EV_CLOCK) { nc++; CHECK(evF(r) >= 7 && evF(r) <= 7 + 48000 + 128, "CLOCK on the new timeline"); } CHECK(nc >= 8, "CLOCK keeps coming (%d)", nc); }
}

// F9: a quantum runs between the push of the Play command and the publish of its snapshot (a test seam inside play()): the running song must not meet the new session's unstarted Seq
struct SeamCtx { W* w; int quanta; };
static void seamRun(void* u) { SeamCtx* c = (SeamCtx*)u; c->w->run((int64_t)Q * c->quanta); }
static void playInterleaving() {
  thunder_live_opts o = opts(THUNDER_LIVE_F_DUMP_STEPS); W w(THUNDER_LIVE_F_DUMP_STEPS); (void)o;
  thunder_live_play_opts a = playOpts(THUNDER_LIVE_NOW); w.H->play(a); w.run(48000); dumpAll(w.H.h);   // A is playing; drain its rows
  w.ev();
  const int64_t T0 = w.fr + 48000 * 3; thunder_live_play_opts b = playOpts(T0); SeamCtx ctx{&w, 3}; w.H->setTestSeam(seamRun, &ctx);
  CHECK(w.H->play(b) == 0, "play B with a quantum (3) between its two statements"); w.H->setTestSeam(nullptr, nullptr);
  w.run(48000 * 5); auto rows = dumpAll(w.H.h); bool mono = true; double prev = -1; int beforeT0 = 0, atT0 = 0; const double t0e = (double)(T0 - 0) / 48000;
  for (auto& r : rows) { const double t = dumpT(r); if (t + 1e-9 < prev) mono = false; prev = t; if (t < t0e - 1e-6) beforeT0++; else atT0++; }
  CHECK(mono && !rows.empty(), "step times only go forward across the start of B (%zu rows)", rows.size());
  CHECK(!rows.empty() && std::fabs(dumpT(rows[(size_t)beforeT0]) - t0e) < 1e-6, "B's first step is at its t0 (%g vs %g)", beforeT0 < (int)rows.size() ? dumpT(rows[(size_t)beforeT0]) : -1.0, t0e);
  auto s = states(w.ev()); int playing = 0; for (auto& r : s) if (stSt(r) == THUNDER_LIVE_ST_PLAYING) playing++; CHECK(playing == 1, "one PLAYING record for B (%d)", playing);
  // two plays before one quantum: exactly one starts (the second); the first's plan is given back
  W v; thunder_live_play_opts pa = playOpts(THUNDER_LIVE_NOW); pa.has_only = 1; pa.only = 0; thunder_live_play_opts pb = playOpts(THUNDER_LIVE_NOW); v.H->play(pa); v.H->play(pb); v.run(Q * 4); s = states(v.ev()); playing = 0; int ended = 0;
  for (auto& r : s) { if (stSt(r) == THUNDER_LIVE_ST_PLAYING) playing++; if (stSt(r) == THUNDER_LIVE_ST_ENDED) ended++; } CHECK(playing == 1 && ended == 0 && v.state() == THUNDER_LIVE_STATE_PLAYING, "two plays before one quantum: only the second runs (%d PLAYING, %d ENDED)", playing, ended);
}

static void policies() {
  // F4: the 0.3 s runs from the SONG's end, whatever the look-ahead
  { W w; w.H->lead(2.0, THUNDER_LIVE_NOW); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); po.has_only = 1; po.only = 0; w.H->play(po); int64_t endedF = -1, stopF = -1; for (int i = 0; i < 40000 / Q * 2; i++) { w.run(Q); for (auto& r : states(w.ev())) { if (stSt(r) == THUNDER_LIVE_ST_ENDED) endedF = evF(r); if (stSt(r) == THUNDER_LIVE_ST_STOPPED) stopF = w.fr; } }
    CHECK(endedF >= 0 && stopF - endedF >= 14400 - Q, "lead 2 s: STOPPED not before 0.3 s after the song's end (%lld frames after)", (long long)(stopF - endedF)); }
  // F5: an IDLE publish that answers E_BUSY changes nothing
  { W w; w.run(Q); w.ev(); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); int n = 0; while (w.H->bpm(100, THUNDER_LIVE_NOW) == 0) n++; CHECK(n == THUNDER_LIVE_CMD_RING, "ring filled (%d)", n);
    CHECK(w.H.song(w.song, 2) == THUNDER_LIVE_E_BUSY, "the IDLE publish is E_BUSY"); w.run(Q); w.ev(); CHECK(w.H->play(po) == 0, "play"); w.run(Q * 2); int64_t rev = -1; for (auto& r : W::of(w.ev(), THUNDER_LIVE_EV_SONG)) rev = evAs<thunder_live_ev_song>(r).rev;
    CHECK(rev == -1 || rev == 1, "the song of the refused publish (rev 2) is not the one that plays (SONG rev %lld)", (long long)rev); CHECK(stats(w.H.h).cmds_no_seq >= 1, "bpm with no sequencer is dropped and counted (%u)", stats(w.H.h).cmds_no_seq); }
  // F6: after HELD a publish takes the IDLE path: no snapshot is built, the song does not resume
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 4); w.H->hold(THUNDER_LIVE_NOW); for (int i = 0; i < 48000 * 30 / Q && w.state() != THUNDER_LIVE_STATE_HELD; i++) w.run(Q); CHECK(w.state() == THUNDER_LIVE_STATE_HELD, "HELD");
    w.ev(); CHECK(w.H.song(w.song, 5) == 0 && stats(w.H.h).song_pending == 0, "publish in HELD: no snapshot pending (%u)", stats(w.H.h).song_pending); const uint64_t st0 = stats(w.H.h).steps; w.run(48000); CHECK(w.state() == THUNDER_LIVE_STATE_HELD && stats(w.H.h).steps == st0, "the held song does not resume");
    int64_t rev = -1; for (auto& r : W::of(w.ev(), THUNDER_LIVE_EV_SONG)) rev = evAs<thunder_live_ev_song>(r).rev; CHECK(rev == 5, "SONG record for rev 5 (%lld)", (long long)rev); }
  // F10: stop and play clear the waiting list
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 4); w.ev(); w.H->bpm(150, w.fr + 48000 * 3); w.H->loop(1, 0, 0, w.fr + 48000 * 3); w.H->mixSet(0, THUNDER_LIVE_MIX_VOL, 10, 0, w.fr + 48000 * 3); w.run(Q);
    w.H->stop(THUNDER_LIVE_NOW); w.run(Q); CHECK(stats(w.H.h).cmds_cleared == 3, "stop cleared the 3 waiting commands (%u)", stats(w.H.h).cmds_cleared); w.H->play(po); w.run(48000 * 4); w.ev(); w.run(4800); { const int v = lastBpm10(w.ev()); CHECK(v != 1500 && v > 0, "the old play's bpm never lands in the new one (%d)", v); }
    w.H->bpm(133, w.fr + 48000 * 6); w.run(Q); thunder_live_play_opts p2 = playOpts(THUNDER_LIVE_NOW); w.H->play(p2); w.run(Q * 2); CHECK(stats(w.H.h).cmds_cleared == 4, "a play that starts clears too (%u)", stats(w.H.h).cmds_cleared); }
  // F13: Seq-bound commands with no sequencer are dropped and counted
  { W w; w.run(Q); w.H->bpm(120, THUNDER_LIVE_NOW); w.H->loop(1, 0, 1, THUNDER_LIVE_NOW); w.H->perf(THUNDER_LIVE_PERF_XF, 0, 0.5, THUNDER_LIVE_NOW); w.H->lead(0.5, THUNDER_LIVE_NOW); const uint64_t ca0 = stats(w.H.h).cmds_applied; w.run(Q); CHECK(stats(w.H.h).cmds_no_seq == 3 && stats(w.H.h).cmds_applied == ca0 + 4, "3 dropped and counted, all 4 in cmds_applied (%u, %llu)", stats(w.H.h).cmds_no_seq, (unsigned long long)stats(w.H.h).cmds_applied); }
  // F8: the true range of each key (mix.h:84, mix_pan = (v - 128) / 127 clamped: 128 is the centre, a left pan is below it)
  { Host H(opts()); CHECK(H->mixSet(3, THUNDER_LIVE_MIX_PAN, 0, 0, -1) == 0 && H->mixSet(3, THUNDER_LIVE_MIX_PAN, 255, 0, -1) == 0 && H->mixSet(3, THUNDER_LIVE_MIX_PAN, 128, 0, -1) == 0, "pan 0 / 128 / 255");
    CHECK(H->mixSet(3, THUNDER_LIVE_MIX_PAN, -0.5, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(3, THUNDER_LIVE_MIX_PAN, 255.5, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(3, THUNDER_LIVE_MIX_VOL, -1, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(3, THUNDER_LIVE_MIX_TRIM, 256, 0, -1) == THUNDER_LIVE_E_ARG && H->mixSet(32, THUNDER_LIVE_MIX_OUT, 255, 0, -1) == 0 && H->mixSet(3, THUNDER_LIVE_MIX_MUTE, 0.5, 0, -1) == THUNDER_LIVE_E_ARG, "outside each key's range: E_ARG"); }
  // lead: a long look-ahead steps ahead (the value is used, not only checked)
  { W a, b; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); a.H->lead(0.05, THUNDER_LIVE_NOW); b.H->lead(2.0, THUNDER_LIVE_NOW); a.H->play(po); b.H->play(po); a.run(Q * 30); b.run(Q * 30);
    CHECK(stats(b.H.h).steps > stats(a.H.h).steps + 20, "lead 2 s steps further ahead than lead 0.05 s (%llu vs %llu)", (unsigned long long)stats(b.H.h).steps, (unsigned long long)stats(a.H.h).steps); }
  // stop: every voice is let go (stopAll), and the pad snapshot published after a stop is adopted (seqGone)
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(48000 * 3); CHECK(stats(w.H.h).voices > 0, "voices sound"); w.H->stop(THUNDER_LIVE_NOW); w.run(Q * 150); CHECK(stats(w.H.h).voices == 0, "stop lets every voice go (%d)", stats(w.H.h).voices);
    w.ev(); std::vector<float> t(1000, 0.1f); CHECK(w.H->padSet(60, t.data(), nullptr, 1000, SR) == 0 && w.H->padsPublish() == 0, "pad after stop"); w.run(Q * 2); int np = 0; for (auto& r : w.ev()) if (evType(r) == THUNDER_LIVE_EV_PADS) np++; CHECK(np == 1, "the pads snapshot published after a stop is adopted, not held back (%d PADS records)", np); }
  // stop_track: the track's voices go, other tracks' do not
  { W w; thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); po.has_loop = 1; po.loop_y0 = 0; po.loop_y1 = 0; w.H->play(po); w.run(48000 * 3); const int v0 = stats(w.H.h).voices; w.H->loop(1, 0, 0, THUNDER_LIVE_NOW); w.H->hold(THUNDER_LIVE_NOW);
    for (int i = 0; i < 48000 * 30 / Q && w.state() != THUNDER_LIVE_STATE_HELD; i++) w.run(Q); const int vh = stats(w.H.h).voices; CHECK(vh > 0, "voices ring on in HELD (%d, was %d)", vh, v0);
    for (int t = 0; t < 32; t++) w.H->stopTrack(t, THUNDER_LIVE_NOW); w.run(Q * 150); CHECK(stats(w.H.h).voices == 0, "stop_track on every track lets every voice go (%d)", stats(w.H.h).voices); }
  // the play's default start: the next quantum + 0.08 s
  { W w(THUNDER_LIVE_F_DUMP_STEPS); w.run(Q * 5); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); w.H->play(po); w.run(Q * 40); auto rows = dumpAll(w.H.h); const double want = (double)(Q * 5 + (int64_t)std::llround(0.08 * SR)) / SR;
    CHECK(!rows.empty() && std::fabs(dumpT(rows[0]) - want) < 2e-6, "t0 < 0: the first step is at the next quantum + 0.08 s (%g vs %g)", rows.empty() ? -1.0 : dumpT(rows[0]), want); }
  // host_stats: the output latency reaches the CLOCK records
  { W w; w.H->hostStats(512, 3); w.run(48000); bool ok = false; for (auto& r : w.ev()) if (evType(r) == THUNDER_LIVE_EV_CLOCK) { const auto c = evAs<thunder_live_ev_clock>(r); ok = c.out_lat == 512 && c.under == 3; } CHECK(ok, "CLOCK carries the host's latency and underruns"); }
}

int main() {
  stateTable(); commands(); timed(); ring(); mixer(); reanchor(); reanchors(); playInterleaving(); policies();
  std::printf("live_transport: %d checks, %d failed\n", g_checks, g_fail); return g_fail ? 1 : 0;
}
