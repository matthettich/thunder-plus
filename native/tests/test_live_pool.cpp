// LIVE ROUTE B2: the live pool (core/src/engine/live_pool.h): mono bass / acid / mach notes into the mixer inputs.
//   (1) P6d: sample_at_or_after() is the sampler's start rule: a sampler note and a live note with the same ev.t start on the same sample (random times, and the hairline ones);
//   (2) L10(a) THE SEAM: the tap on the track inputs of a Players with live pads equals the LiveShell driven directly by the U26 rule (events as the test books them itself, rate / gain / pan as plain automation lists),
//       bit for bit, every track and quantum;
//   (3) L4: a note lands on its own sample (the first sound is at the note's sample, not lat later); an event inside the look-ahead is applied late and counted;
//   (4) the C API: set_pad_live / clear_pad_live (state, hostile arguments, the budget), the stats, and a whole song through thunder_render_read at host blocks 1 / 128 / 480 / 4096 (the same samples).
//   test_live_pool
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "engine/live_pool.h"
#include "render/players.h"
#include "render/render.h"
#include "rt/auto.h"
#include "rt/sampletime.h"
#include "song/song.h"
#include "thunder/render.h"

using namespace thunder;
static int g_fail = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

static const double SR = 48000;
static uint64_t rngS = 0x9e3779b97f4a7c15ull;
static double rnd() { rngS ^= rngS << 13; rngS ^= rngS >> 7; rngS ^= rngS << 17; return (double)(rngS >> 11) / 9007199254740992.0; }

static std::string songJson() {   // one empty phrase: the sequencer exists but plays nothing that matters
  return "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";
}

static SeqEvent note(double t, int track, int pad, double rate, double gain = 0.8, double pan = 128) {
  SeqEvent e; e.type = SEV_NOTE; e.t = t; e.track = track; e.pad = pad; e.rate = rate; e.gain = gain; e.pan = pan; return e;
}

// ---- (1) the start rule ----------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void testStartRule() {
  // the function's own definition: the first sample whose sample_time is at or after t, and the sample before is below it
  for (int i = 0; i < 20000; i++) {
    const double t = rnd() * 200.0;
    const int64_t s = sample_at_or_after(t, SR);
    CHECKF(sample_time(s, SR) >= t && (s == 0 || sample_time(s - 1, SR) < t), "t=%.17g s=%lld", t, (long long)s);
  }
  // a hair above a sample, a hair below, exactly on it, across a quantum edge
  const double hair[] = {60.00000000000129, 59.999999999999, 1.0 / 48000 * 127, 1.0 / 48000 * 128, 128.0 / 48000, 128.0 / 48000 + 1e-13, 0.0, -1.0};
  for (double t : hair) { const int64_t s = sample_at_or_after(t, SR); CHECK(sample_time(s, SR) >= t || t <= 0); }
  // against the sampler: a DC pad note at ev.t starts (the first nonzero output) on sample_at_or_after(ev.t)
  std::vector<float> dc(4000, 1.0f);
  for (int k = 0; k < 400; k++) {
    VoicePool vp(SR, 4); VoiceBuf b; b.data = dc.data(); b.len = (int)dc.size(); b.sr = SR; VoiceKit kit;
    const double t = (k < 40 ? (k * 128 + (k % 9)) / SR + (k % 2 ? 1e-12 : 0) : rnd() * 0.05);
    SeqEvent e = note(t, 0, 0, 1.0, 1.0, 128);
    const int64_t want = sample_at_or_after(t, SR);
    const int nq = (int)(want / 128) + 2; std::vector<float> L((size_t)nq * 128, 0), R((size_t)nq * 128, 0);
    vp.setTime(0); CHECK(vp.note(e, nullptr, b, kit, 120));
    for (int q = 0; q < nq; q++) { float* l[1] = {L.data() + q * 128}; float* r[1] = {R.data() + q * 128}; vp.setTime(q * 128 / SR); vp.render(l, r, 128, q * 128 / SR); }
    int64_t first = -1; for (size_t i = 0; i < L.size(); i++) if (L[i] != 0) { first = (int64_t)i; break; }
    CHECKF(first == want, "t=%.17g sampler starts %lld, rule %lld", t, (long long)first, (long long)want);
  }
}

// ---- (2) the seam ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void setP(BassCore& c, const BassP* b, const AcidP*, const MachP*) { c.set(*b); }
static void setP(AcidCore& c, const BassP*, const AcidP* a, const MachP*) { c.set(*a); }
static void setP(MachCore& c, const BassP*, const AcidP*, const MachP* m) { c.set(*m); }
template <class SH> static void refRun(const BassP* bp, const AcidP* ap, const MachP* mp, const std::vector<SeqEvent>& evs, int track, int pad, int64_t total, std::vector<float>& outL, std::vector<float>& outR) {
  SH sh(SR);
  setP(sh.core(), bp, ap, mp);
  static AutoN<4096> rate, g, pn; rate.reset(0); g.reset(1); pn.reset(0);   // (the rate param starts at 0 in the page: P5)
  std::vector<LiveEvent> le; const uint32_t lat = SH::latency();
  auto ev = [&](int64_t T, LiveEvType ty, bool leg, bool acc) { LiveEvent x; x.at = (uint32_t)T; x.type = ty; x.leg = leg; x.acc = acc; le.push_back(x); };
  for (const SeqEvent& e : evs) {
    if (e.track != track || (int)e.pad != pad) continue;
    const double t = e.t; const int64_t T = sample_at_or_after(t, SR); const bool leg = (e.has & SH_SLIDE) && e.slideDur > 0;
    rate.hold(t); if (leg) { rate.set(e.slideFrom, t); rate.lin(e.rate, t + e.slideDur); } else rate.set(e.rate, t);
    ev(T, LiveEvType::NoteOn, leg, e.acc);
    g.set(e.gain, t); pn.set(std::max(-1.0, std::min(1.0, (e.pan - 128) / 127)), t);
    if (e.has & SH_REL) ev(sample_at_or_after(e.rel, SR), LiveEvType::NoteOff, false, false);
  }
  std::stable_sort(le.begin(), le.end(), [](const LiveEvent& a, const LiveEvent& b) { return a.at < b.at; });
  const uint64_t n = (uint64_t)total + lat; std::vector<float> L(n), R(n);
  // events are offsets inside the one call: a call of n samples takes events with at < n
  sh.setRateFeed(&rate, SR);
  sh.process(L.data(), R.data(), (uint32_t)n, le.data(), (uint32_t)le.size());
  outL.assign(total, 0); outR.assign(total, 0);
  for (int64_t q0 = 0; q0 < total; q0 += 128) {
    const double dt = 1 / SR, tb0 = (double)q0 / SR;
    for (int i = 0; i < 128 && q0 + i < total; i++) {
      const double t = tb0 + i * dt, gg = g.at(t), p = pn.at(t);
      const double l = (double)L[q0 + i + lat] * gg, r = (double)R[q0 + i + lat] * gg; double a, b;
      if (p <= 0) { const double x = (p + 1) * 3.14159265358979323846 / 2; a = l + r * std::cos(x); b = r * std::sin(x); }
      else { const double x = p * 3.14159265358979323846 / 2; a = l * std::cos(x); b = r + l * std::sin(x); }
      outL[q0 + i] = (float)(double)(float)a; outR[q0 + i] = (float)(double)(float)b;
    }
  }
}

static void testSeam() {
  Song song; JsonError je; const std::string sj = songJson(); CHECK(song.load(sj.data(), sj.size(), &je));
  SeqVars vars; SeqOpts so; so.row = song.renderFrom; so.once = true; Seq seq(song, vars, SeqMode::Song, so);
  std::unique_ptr<RenderPads> pads(new RenderPads());
  const int padB = 0, padA = 1, padM = 2; pads->route[padB] = pads->route[padA] = pads->route[padM] = PadRoute::Live;
  Players pl(pads.get(), SR, 8);
  BassP bp; bp.shape = 0.3; bp.cut = 700; bp.glide = 20; AcidP ap; ap.cut = 500; ap.res = 0.7; MachP mp; mp.machine = 1; mp.lp = 2000;
  LivePadConfig cb; cb.kind = LiveKind::Bass; cb.bass = bp; cb.trackMask = 0b000011;   // tracks 0, 1 (each pad has its own tracks: a mono live note takes its track over, so pads sharing a track are the takeover test's)
  LivePadConfig ca; ca.kind = LiveKind::Acid; ca.acid = ap; ca.trackMask = 0b001100;   // tracks 2, 3
  LivePadConfig cm; cm.kind = LiveKind::Mach; cm.mach = mp; cm.trackMask = 0b110000;  // tracks 4, 5
  CHECK(pl.live().addPad(padB, cb)); CHECK(pl.live().addPad(padA, ca)); CHECK(pl.live().addPad(padM, cm));
  CHECK(!pl.live().addPad(3, LivePadConfig()));   // a pad with no kind

  // the notes: increasing in time on each (track, pad); each ends by its own `rel`, so the reference needs no cancel
  std::vector<SeqEvent> evs;
  const int tracks[3] = {0, 1, 3};
  for (int k = 0; k < 2; k++) for (int j = 0; j < 7; j++) {
    SeqEvent e = note(0.02 + j * 0.11 + k * 0.0031 + rnd() * 0.01, tracks[k], padB, 0.6 + 0.2 * j, 0.5 + 0.05 * j, 40 + 30 * j); e.t = std::round(e.t * 1e9) / 1e9;
    if (j % 3 == 1) { e.has |= SH_SLIDE; e.slideFrom = e.rate * 0.5; e.slideDur = 0.03; }
    if (j % 2 == 0) { e.has |= SH_REL; e.rel = e.t + 0.05 + 0.005 * j; }
    evs.push_back(e);
  }
  for (int k = 0; k < 2; k++) for (int j = 0; j < 6; j++) {
    SeqEvent e = note(0.03 + j * 0.13 + k * 0.002, 2 + k, padA, 0.5 + 0.25 * j, 0.7, 128 + 10 * j); e.acc = j % 2; if (j % 3 == 2) { e.has |= SH_SLIDE; e.slideFrom = 1; e.slideDur = 0.02; }
    if (j % 2) { e.has |= SH_REL; e.rel = e.t + 0.09; }
    evs.push_back(e);
  }
  for (int k = 0; k < 2; k++) for (int j = 0; j < 5; j++) {
    SeqEvent e = note(0.01 + j * 0.15 + k * 0.0007, 4 + k, padM, 0.8 + 0.1 * j, 0.6, 128 + 20 * j - 40);
    if (j % 2 == 0) { e.has |= SH_REL; e.rel = e.t + 0.1; }
    evs.push_back(e);
  }
  const int64_t total = (int64_t)(1.2 * SR) / 128 * 128;
  const int NTR = 8; std::vector<std::vector<float>> gotL(NTR, std::vector<float>(total, 0)), gotR(NTR, std::vector<float>(total, 0));
  std::vector<std::vector<float>> bufL(NTR, std::vector<float>(128)), bufR(NTR, std::vector<float>(128));
  float* inL[kVoiceTracks]; float* inR[kVoiceTracks]; for (int t = 0; t < kVoiceTracks; t++) { inL[t] = inR[t] = nullptr; } for (int t = 0; t < NTR; t++) { inL[t] = bufL[t].data(); inR[t] = bufR[t].data(); }
  std::vector<char> done(evs.size(), 0);
  for (int64_t q0 = 0; q0 < total; q0 += 128) {
    const double t0 = (double)q0 / SR; pl.setTime(t0, q0);
    for (size_t i = 0; i < evs.size(); i++) if (!done[i] && evs[i].t - 0.25 <= t0) { done[i] = 1; pl.dispatch(evs[i], seq, song); }
    for (int t = 0; t < NTR; t++) { std::fill(bufL[t].begin(), bufL[t].end(), 0.f); std::fill(bufR[t].begin(), bufR[t].end(), 0.f); }
    pl.render(inL, inR, 128, t0, q0);
    for (int t = 0; t < NTR; t++) { std::copy(bufL[t].begin(), bufL[t].end(), gotL[t].begin() + q0); std::copy(bufR[t].begin(), bufR[t].end(), gotR[t].begin() + q0); }
  }
  // the reference: per track, the sum of its pads' shells
  int sounded = 0;
  for (int t = 0; t < NTR; t++) {
    std::vector<float> eL(total, 0), eR(total, 0), L, R;
    const bool b = (cb.trackMask >> t & 1) != 0, a = (ca.trackMask >> t & 1) != 0;
    // the pool's nodes are in pad order: bass, acid, mach (the sum's order is the float order: pad 0, 1, 2)
    if (b) { refRun<LiveBass>(&bp, nullptr, nullptr, evs, t, padB, total, L, R); for (int64_t i = 0; i < total; i++) { eL[i] = (float)((double)eL[i] + (double)L[i]); eR[i] = (float)((double)eR[i] + (double)R[i]); } }
    if (a) { refRun<LiveAcid>(nullptr, &ap, nullptr, evs, t, padA, total, L, R); for (int64_t i = 0; i < total; i++) { eL[i] = (float)((double)eL[i] + (double)L[i]); eR[i] = (float)((double)eR[i] + (double)R[i]); } }
    { refRun<LiveMach>(nullptr, nullptr, &mp, evs, t, padM, total, L, R); for (int64_t i = 0; i < total; i++) { eL[i] = (float)((double)eL[i] + (double)L[i]); eR[i] = (float)((double)eR[i] + (double)R[i]); } }
    int64_t bad = -1; for (int64_t i = 0; i < total; i++) if (std::memcmp(&eL[i], &gotL[t][i], 4) || std::memcmp(&eR[i], &gotR[t][i], 4)) { bad = i; break; }
    CHECKF(bad < 0, "track %d differs from the shell driven directly at sample %lld (%g vs %g)", t, (long long)bad, bad >= 0 ? eL[bad] : 0.0, bad >= 0 ? gotL[t][bad] : 0.0);
    double e = 0; for (float x : gotL[t]) e += std::fabs(x); if (e > 1e-3) sounded++;
  }
  CHECKF(sounded >= 5, "only %d tracks sounded", sounded);
  const LiveStats& s = pl.live().stats(); CHECKF(s.late == 0 && s.queueDropped == 0 && s.noNode == 0, "late %u dropped %u noNode %u", s.late, s.queueDropped, s.noNode);
  std::printf("memory per node: bass %zu B, acid %zu B, mach %zu B (padBytes / nodes: %zu / 2, %zu / 2, %zu / 2)\n", pl.live().padBytes(padB) / 2, pl.live().padBytes(padA) / 2, pl.live().padBytes(padM) / 2, pl.live().padBytes(padB), pl.live().padBytes(padA), pl.live().padBytes(padM));
  std::printf("seam: %d tracks sounded, %u notes played, late %u\n", sounded, s.played, s.late);
}


// ---- (2b) vox: the mono node (the syllable counter), polyVox (the JS's picks and syllables, from the decision-log golden), the audio of each node against the shell driven directly --------------------------------------
struct VoxNote { SeqEvent e; double syl; bool same; };
static void refRunVox(const VoxP& vp, const std::vector<VoxNote>& ns, int64_t total, std::vector<float>& outL, std::vector<float>& outR) {
  LiveVox sh(SR); sh.core().set(vp);
  static AutoN<4096> rate, g, pn; rate.reset(0); g.reset(1); pn.reset(0);   // (the rate param starts at 0 in the page: P5)
  std::vector<LiveEvent> le; const uint32_t lat = LiveVox::latency();
  for (const VoxNote& n : ns) {
    const SeqEvent& e = n.e; const int64_t T = sample_at_or_after(e.t, SR); const bool leg = (e.has & SH_SLIDE) && e.slideDur > 0;
    rate.hold(e.t); if (leg) { rate.set(e.slideFrom, e.t); rate.lin(e.rate, e.t + e.slideDur); } else rate.set(e.rate, e.t);
    LiveEvent x; x.at = (uint32_t)T; x.type = LiveEvType::NoteOn; x.leg = leg; x.same = n.same; x.v = n.syl; le.push_back(x);
    g.set(e.gain, e.t); pn.set(std::max(-1.0, std::min(1.0, (e.pan - 128) / 127)), e.t);
    if (e.has & SH_REL) { LiveEvent o; o.at = (uint32_t)sample_at_or_after(e.rel, SR); o.type = LiveEvType::NoteOff; le.push_back(o); }
  }
  std::stable_sort(le.begin(), le.end(), [](const LiveEvent& a, const LiveEvent& b) { return a.at < b.at; });
  const uint64_t n = (uint64_t)total + lat; std::vector<float> L(n), R(n);
  sh.setRateFeed(&rate, SR); sh.process(L.data(), R.data(), (uint32_t)n, le.data(), (uint32_t)le.size());
  outL.assign(total, 0); outR.assign(total, 0);
  for (int64_t q0 = 0; q0 < total; q0 += 128) {
    const double dt = 1 / SR, tb0 = (double)q0 / SR;
    for (int i = 0; i < 128 && q0 + i < total; i++) {
      const double t = tb0 + i * dt, gg = g.at(t), p = pn.at(t);
      const double l = (double)L[q0 + i + lat] * gg, r = (double)R[q0 + i + lat] * gg; double a, b;
      if (p <= 0) { const double x = (p + 1) * 3.14159265358979323846 / 2; a = l + r * std::cos(x); b = r * std::sin(x); }
      else { const double x = p * 3.14159265358979323846 / 2; a = l * std::cos(x); b = r + l * std::sin(x); }
      outL[q0 + i] = (float)(double)(float)a; outR[q0 + i] = (float)(double)(float)b;
    }
  }
}
static void testVox() {
  Song song; JsonError je; const std::string sj = songJson(); CHECK(song.load(sj.data(), sj.size(), &je));
  SeqVars vars; SeqOpts so; so.row = song.renderFrom; so.once = true; Seq seq(song, vars, SeqMode::Song, so);
  std::unique_ptr<RenderPads> pads(new RenderPads()); pads->route[0] = PadRoute::Live;
  Players pl(pads.get(), SR, 8);
  VoxP vp; vp.txt = "la di da ba ne"; vp.sylLen = 180;
  LivePadConfig c; c.kind = LiveKind::Vox; c.vox = vp; c.trackMask = 0b11; CHECK(pl.live().addPad(0, c));
  std::printf("memory per vox node: %zu B (padBytes %zu / 10 nodes)\n", pl.live().padBytes(0) / 10, pl.live().padBytes(0));
  std::vector<LiveTraceRec> trace; trace.reserve(4096); pl.live().setTrace(&trace);
  std::vector<SeqEvent> evs;
  // track 0: the mono node (slides and a release; the syllable counter counts the notes that are not slides)
  for (int j = 0; j < 6; j++) { SeqEvent e = note(0.05 + 0.1 * j, 0, 0, 0.7 + 0.15 * j, 0.7, 128 + 10 * j); if (j % 3 == 1) { e.has |= SH_SLIDE; e.slideFrom = e.rate * 0.6; e.slideDur = 0.03; } if (j % 2 == 0) { e.has |= SH_REL; e.rel = e.t + 0.07; } evs.push_back(e); }
  // track 1: polyVox, the first seven notes of the decision-log golden's vox_poly case (tools/live-route-dump.mjs): a chord of four within 4 ms, then three more
  struct PN { int b, o, col; double rate; bool slide; };
  const PN pn[7] = {{1, 0, 0, 1}, {1, 40, 1, 1.25}, {1, 80, 2, 1.5}, {1, 120, 3, 2}, {4, 0, 4, 0.5}, {6, 0, 0, 1.1, true}, {8, 0, 1, 1.3}};
  const size_t first = evs.size();
  for (int k = 0; k < 7; k++) { SeqEvent e = note((double)(pn[k].b * 128 + pn[k].o) / SR, 1, 0, pn[k].rate); e.poly = true; e.col = pn[k].col; if (pn[k].slide) { e.has |= SH_SLIDE; e.slideFrom = 1; e.slideDur = 0.02; } evs.push_back(e); }
  const int64_t total = (int64_t)(0.9 * SR) / 128 * 128;
  std::vector<std::vector<float>> gotL(2, std::vector<float>(total, 0)), gotR(2, std::vector<float>(total, 0)), bl(2, std::vector<float>(128)), br(2, std::vector<float>(128));
  float* inL[kVoiceTracks] = {}; float* inR[kVoiceTracks] = {}; for (int t = 0; t < 2; t++) { inL[t] = bl[t].data(); inR[t] = br[t].data(); }
  std::vector<char> done(evs.size(), 0);
  for (int64_t q0 = 0; q0 < total; q0 += 128) {
    const double t0 = (double)q0 / SR; pl.setTime(t0, q0);
    for (size_t i = 0; i < evs.size(); i++) if (!done[i] && evs[i].t - 0.25 <= t0) { done[i] = 1; pl.dispatch(evs[i], seq, song); }
    for (int t = 0; t < 2; t++) { std::fill(bl[t].begin(), bl[t].end(), 0.f); std::fill(br[t].begin(), br[t].end(), 0.f); }
    pl.render(inL, inR, 128, t0, q0);
    for (int t = 0; t < 2; t++) { std::copy(bl[t].begin(), bl[t].end(), gotL[t].begin() + q0); std::copy(br[t].begin(), br[t].end(), gotR[t].begin() + q0); }
  }
  // the picks and syllables of the poly chord, as the JS's log has them (nodes 1, 2, 3, 4, 1, 2, 3; syllables 0, 0, 0, 0, 1, 1, 2; the slide's `same` 1)
  std::vector<LiveTraceRec> pn1; for (const LiveTraceRec& r : trace) if (r.what == 'n' && r.track == 1) pn1.push_back(r);
  const int wantJ[7] = {1, 2, 3, 4, 1, 2, 3}; const double wantSyl[7] = {0, 0, 0, 0, 1, 1, 2}; const bool wantSame[7] = {0, 0, 0, 0, 0, 1, 0};
  CHECKF(pn1.size() == 7, "poly notes booked: %zu", pn1.size());
  for (size_t k = 0; k < pn1.size() && k < 7; k++) CHECKF(pn1[k].j == wantJ[k] && pn1[k].v == wantSyl[k] && pn1[k].same == wantSame[k], "poly note %zu: node %d syl %g same %d (want %d %g %d)", k, pn1[k].j, pn1[k].v, (int)pn1[k].same, wantJ[k], wantSyl[k], (int)wantSame[k]);
  // the audio: the mono node (track 0) with the syllable counter; each poly node from the notes the trace gave it
  { std::vector<VoxNote> ns; double sylN = 0; for (size_t i = 0; i < first; i++) { VoxNote n; n.e = evs[i]; const bool leg = (n.e.has & SH_SLIDE) && n.e.slideDur > 0; n.syl = leg ? std::max(0.0, sylN - 1) : sylN++; n.same = leg; ns.push_back(n); }
    std::vector<float> L, R; refRunVox(vp, ns, total, L, R);
    int64_t bad = -1; for (int64_t i = 0; i < total; i++) if (std::memcmp(&L[i], &gotL[0][i], 4) || std::memcmp(&R[i], &gotR[0][i], 4)) { bad = i; break; }
    CHECKF(bad < 0, "vox mono differs from the shell driven directly at %lld", (long long)bad);
    double e = 0; for (float x : gotL[0]) e += std::fabs(x); CHECK(e > 1e-3); }
  { std::vector<float> sumL(total, 0), sumR(total, 0);
    for (int j = 1; j <= 4; j++) {
      std::vector<VoxNote> ns; for (size_t k = 0; k < pn1.size(); k++) if (pn1[k].j == j) { VoxNote n; n.e = evs[first + k]; n.syl = pn1[k].v; n.same = pn1[k].same; ns.push_back(n); }
      std::vector<float> L, R; refRunVox(vp, ns, total, L, R);
      for (int64_t i = 0; i < total; i++) { sumL[i] = (float)((double)sumL[i] + (double)L[i]); sumR[i] = (float)((double)sumR[i] + (double)R[i]); }
    }
    // (the mono node of track 1 adds exact zeros first)
    int64_t bad = -1; for (int64_t i = 0; i < total; i++) if (std::memcmp(&sumL[i], &gotL[1][i], 4) || std::memcmp(&sumR[i], &gotR[1][i], 4)) { bad = i; break; }
    CHECKF(bad < 0, "polyVox differs from the four shells driven directly at %lld (%g %g; sum %g %g)", (long long)bad, bad>=0?(double)gotL[1][bad]:0.0, bad>=0?(double)gotR[1][bad]:0.0, bad>=0?(double)sumL[bad]:0.0, bad>=0?(double)sumR[bad]:0.0);
    double e = 0; for (float x : gotL[1]) e += std::fabs(x); CHECK(e > 1e-3); }
  // the overflow guard: five notes at one instant on one track with four nodes busy: the fifth is consumed, silent
  { std::vector<LiveTraceRec> t2; t2.reserve(64); pl.live().setTrace(&t2); const double base = 1.0;
    pl.setTime(base, (int64_t)(base * SR)); uint32_t before = pl.live().stats().played;
    for (int k = 0; k < 5; k++) { SeqEvent e = note(base, 1, 0, 1.0); e.poly = true; e.col = 10 + k; pl.dispatch(e, seq, song); }
    int ons = 0; for (const LiveTraceRec& r : t2) if (r.what == 'n') ons++;
    CHECKF(ons == 4 && pl.live().stats().played - before == 4, "five notes at one instant: %d booked, %u played (want 4)", ons, pl.live().stats().played - before); }
}

// ---- (3) run-ahead: an event inside the look-ahead is late ------------------------------------------------------------------------------------------------------------------------------------
static void testLate() {
  Song song; JsonError je; const std::string sj = songJson(); CHECK(song.load(sj.data(), sj.size(), &je));
  SeqVars vars; SeqOpts so; so.row = song.renderFrom; so.once = true; Seq seq(song, vars, SeqMode::Song, so);
  std::unique_ptr<RenderPads> pads(new RenderPads()); pads->route[0] = PadRoute::Live;
  Players pl(pads.get(), SR, 8); LivePadConfig c; c.kind = LiveKind::Bass; CHECK(pl.live().addPad(0, c));
  std::vector<float> bl(128), br(128); float* inL[kVoiceTracks] = {}; float* inR[kVoiceTracks] = {}; inL[0] = bl.data(); inR[0] = br.data();
  for (int q = 0; q < 6; q++) {
    const int64_t q0 = q * 128; pl.setTime((double)q0 / SR, q0);
    if (q == 3) { SeqEvent e = note((double)(q0 + 5) / SR, 0, 0, 1.0); pl.dispatch(e, seq, song); }   // 5 samples after the quantum's start: inside the shell's look-ahead (31)
    if (q == 4) { SeqEvent e = note((double)(q0 + 31 + 128 + 40) / SR, 0, 0, 1.0); pl.dispatch(e, seq, song); }   // beyond it: on time
    std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f); pl.render(inL, inR, 128, (double)q0 / SR, q0);
  }
  CHECKF(pl.live().stats().late == 1, "late %u (want 1)", pl.live().stats().late);
}

// ---- (4) the C API ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static std::string apiSong() {   // track 0: notes on rows 0, 2, 4, 6 of one phrase; inst 0 -> pad 0
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[8],\"insts\":[{\"pad\":0,\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}],\"phrases\":[[";
  for (int i = 0; i < 8; i++) { char b[120]; if (i % 2 == 0) std::snprintf(b, sizeof b, "%s[%d,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]", i ? "," : "", 48 + 3 * i); else std::snprintf(b, sizeof b, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += b; }
  return s + "]]}";
}
static std::vector<float> apiRender(int block, bool live, bool route, thunder_render_stats* st, const char* json = "{\"engine\":\"bass\",\"level\":0,\"tune\":0,\"drive\":0,\"bass\":{\"cut\":600}}") {
  const std::string sg = apiSong(); thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = 48000;
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &o, err, sizeof err); if (!r) { std::printf("create: %s\n", err); g_fail++; return {}; }
  if (route) CHECK(thunder_render_set_pad_route(r, 0, THUNDER_RENDER_ROUTE_LIVE) == 0);
  if (live) CHECK(thunder_render_set_pad_live(r, 0, THUNDER_RENDER_LIVE_BASS, (const uint8_t*)json, std::strlen(json), nullptr) == 0);
  std::vector<float> L(48000), R(48000); int64_t got = 0;
  while (got < 48000) { const int64_t n = thunder_render_read(r, L.data() + got, R.data() + got, std::min<int64_t>(block, 48000 - got)); if (n <= 0) break; got += n; }
  CHECK(got == 48000);
  if (st) { std::memset(st, 0, sizeof *st); st->size = sizeof *st; CHECK(thunder_render_stats_get(r, st) == 0); }
  CHECK(thunder_render_set_pad_live(r, 0, THUNDER_RENDER_LIVE_BASS, (const uint8_t*)json, std::strlen(json), nullptr) == THUNDER_RENDER_E_STATE);   // after the first read
  CHECK(thunder_render_clear_pad_live(r, 0) == THUNDER_RENDER_E_STATE);
  thunder_render_free(r); L.insert(L.end(), R.begin(), R.end()); return L;
}

// ---- (4b) stems (U34b): a stem of the track that plays a live bass pad holds the bass; another track's stem does not -----------------------------------------------------------------------------
static std::string stemSong() {   // track 0: notes on inst 0 (pad 0, live bass); track 1: notes on inst 1 (pad 1, no buffer: silent)
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0,1]],\"chains\":[[[0,0]],[[1,0]]],\"plen\":[8,8],\"insts\":[{\"pad\":0,\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0},{\"pad\":1,\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}],\"phrases\":[";
  for (int p = 0; p < 2; p++) { s += p ? ",[" : "["; for (int i = 0; i < 8; i++) { char b[120]; if (i % 2 == 0) std::snprintf(b, sizeof b, "%s[%d,-1,-1,-1,-1,%d,-1,-1,-1,-1,-1,-1]", i ? "," : "", 48 + 3 * i, p); else std::snprintf(b, sizeof b, ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += b; } s += "]"; }
  return s + "]}";
}
static double stemEnergy(int stem) {
  const std::string sg = stemSong(); thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = 48000; o.stem = stem;
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &o, err, sizeof err); if (!r) { std::printf("create: %s\n", err); g_fail++; return -1; }
  const char* json = "{\"engine\":\"bass\",\"level\":0,\"tune\":0,\"drive\":0,\"bass\":{\"cut\":600}}";
  CHECK(thunder_render_set_pad_route(r, 0, THUNDER_RENDER_ROUTE_LIVE) == 0);
  thunder_render_live_params lp; std::memset(&lp, 0, sizeof lp); lp.size = sizeof lp; lp.track_mask = 0b11;
  CHECK(thunder_render_set_pad_live(r, 0, THUNDER_RENDER_LIVE_BASS, (const uint8_t*)json, std::strlen(json), &lp) == 0);
  std::vector<float> L(48000), R(48000); int64_t got = 0;
  while (got < 48000) { const int64_t n = thunder_render_read(r, L.data() + got, R.data() + got, std::min<int64_t>(4096, 48000 - got)); if (n <= 0) break; got += n; }
  double e = 0; for (int64_t i = 0; i < got; i++) e += std::fabs(L[i]) + std::fabs(R[i]);
  thunder_render_free(r); return e;
}
static void testStem() {
  const double mix = stemEnergy(0), s0 = stemEnergy(1), s1 = stemEnergy(2);
  CHECKF(mix > 1.0 && s0 > 1.0, "the mix and the stem of the live bass track hold the bass (%g, %g)", mix, s0);
  CHECKF(s1 == 0, "another track's stem holds no bass (%g)", s1);
}

// C4: a render of unknown length ends: the sequencer's end releases the live voice that is still held (a bass note with no release), so voices() reaches 0 (the page cuts at the end instead)
static void testSongOver() {
  const std::string sg = apiSong(); thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o;   // use_frames 0: the length is the song's
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &o, err, sizeof err); if (!r) { std::printf("create: %s\n", err); g_fail++; return; }
  const char* json = "{\"engine\":\"bass\",\"level\":0,\"tune\":0,\"drive\":0,\"bass\":{\"cut\":600}}";
  CHECK(thunder_render_set_pad_route(r, 0, THUNDER_RENDER_ROUTE_LIVE) == 0); CHECK(thunder_render_set_pad_live(r, 0, THUNDER_RENDER_LIVE_BASS, (const uint8_t*)json, std::strlen(json), nullptr) == 0);
  std::vector<float> L(4096), R(4096); int64_t got = 0; const int64_t cap = 48000LL * 40;
  for (;;) { const int64_t n = thunder_render_read(r, L.data(), R.data(), 4096); if (n <= 0) break; got += n; if (got > cap) break; }
  thunder_render_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; CHECK(thunder_render_stats_get(r, &st) == 0);
  CHECKF(got > 0 && got <= cap, "the render did not end (%lld frames)", (long long)got);
  CHECKF(st.voices_active == 0 && st.song_over, "voices_active %d song_over %d", (int)st.voices_active, (int)st.song_over);
  std::printf("unknown length: ended after %.2f s\n", (double)got / 48000);
  thunder_render_free(r);
}
static void testApi() {
  thunder_render_stats s0, s1; const std::vector<float> none = apiRender(4096, false, true, &s0), a = apiRender(4096, true, true, &s1);
  CHECKF(s0.notes_live == 4 && s0.notes_live_played == 0, "notes_live %d played %d (no live params: silent, counted)", s0.notes_live, s0.notes_live_played);
  double e0 = 0, e1 = 0; for (float x : none) e0 += std::fabs(x); for (float x : a) e1 += std::fabs(x);
  CHECK(e0 == 0); CHECKF(e1 > 1.0, "the live pad is silent (%g)", e1);
  CHECKF(s1.notes_live == 4 && s1.notes_live_played == 4 && s1.live_no_node == 0 && s1.live_late == 0 && s1.live_failed == 0, "stats %d %d %u %u %u", s1.notes_live, s1.notes_live_played, s1.live_no_node, s1.live_late, s1.live_failed);
  for (int blk : {1, 128, 480}) { const std::vector<float> b = apiRender(blk, true, true, nullptr); CHECKF(b.size() == a.size() && !std::memcmp(a.data(), b.data(), a.size() * 4), "host block %d changes the output", blk); }
  // a live pad whose route is not Live plays as the sampler does (no buffer: silent), the live params unused
  { thunder_render_stats s2; const std::vector<float> c = apiRender(4096, true, false, &s2); double e = 0; for (float x : c) e += std::fabs(x); CHECK(e == 0 && s2.notes_live == 0); }
  // hostile arguments
  const std::string sg = apiSong(); thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = 4800;
  thunder_render* r = thunder_render_create((const uint8_t*)sg.data(), sg.size(), &o, nullptr, 0); CHECK(r);
  const char* ok = "{\"engine\":\"bass\",\"bass\":{}}"; const size_t okn = std::strlen(ok);
  CHECK(thunder_render_set_pad_live(nullptr, 0, 1, (const uint8_t*)ok, okn, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, -1, 1, (const uint8_t*)ok, okn, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 256, 1, (const uint8_t*)ok, okn, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 0, 0, (const uint8_t*)ok, okn, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 0, 2, (const uint8_t*)ok, okn, nullptr) == THUNDER_RENDER_E_ARG);   // a bass voice is not a vox
  CHECK(thunder_render_set_pad_live(r, 0, 99, (const uint8_t*)ok, okn, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 0, 1, nullptr, okn, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)ok, 0, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)"{not json", 9, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)"[1]", 3, nullptr) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_live(r, 0, 3, (const uint8_t*)ok, okn, nullptr) == THUNDER_RENDER_E_ARG);   // an engine that is not the kind's
  thunder_render_live_params p; std::memset(&p, 0, sizeof p); p.size = sizeof p; p.track_mask = (uint64_t)1 << 40;
  CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)ok, okn, &p) == THUNDER_RENDER_E_ARG);
  p.track_mask = 1; p.reserved0 = 1; CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)ok, okn, &p) == THUNDER_RENDER_E_ARG);
  p.reserved0 = 0; p.size = 4; CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)ok, okn, &p) == THUNDER_RENDER_E_ARG);
  p.size = sizeof p; CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)ok, okn, &p) == 0);   // one track
  CHECK(thunder_render_set_pad_live(r, 0, 1, (const uint8_t*)ok, okn, nullptr) == 0);                // replaced by all 33
  CHECK(thunder_render_clear_pad_live(r, 0) == 0); CHECK(thunder_render_clear_pad_live(r, 0) == 0); CHECK(thunder_render_clear_pad_live(r, 300) == THUNDER_RENDER_E_ARG);
  thunder_render_free(r);
}

int main() {
  testStartRule();
  testSeam();
  testVox();
  testLate();
  testStem();
  testSongOver();
  testApi();
  if (g_fail) { std::printf("live_pool: %d FAILED\n", g_fail); return 1; }
  std::printf("live_pool: all checks passed\n"); return 0;
}
