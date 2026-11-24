// U37 P3 (ctest `live_fx`): LiveHost with the track and master EFFECTS, the MASTER CHAIN, LATENCY COMP (PDC) and the METERS is BIT-EQUAL to SongRenderer with the same options, for any frame0 and for
// several host block sizes, on ELECTRO with all its effect slots (pump slots, MODFX slots, the master's Flex / Retrigger, mastering on, the safety limiter) and on variants of it (mastering off, no
// limiter, PDC off / on, the page's timing); plus the live EDITS (fx_plan + fx_commit at a quantum, fx_param, pdc_set, master_config, meters_want) against the same edit done on a SongRenderer between
// blocks, the refusals of each call, the plans' return, and the stats. The allocation guard is in rt_harness (LiveHost with effects).
#include "live_common.h"

#include <functional>

#include "fx/fxbank.h"
#include "master/chain.h"
#include "meter/meter.h"
#include "mix/pdc.h"
#include "render/render.h"

using namespace lt;

namespace {
struct Ref { std::vector<float> l, r; std::vector<MeterEvent> meters; int64_t latency = 0; int64_t qLatencyEnd = 0; };
typedef std::function<void(SongRenderer&)> RefOp;

// the reference: SongRenderer as render_api.cpp sets it up (effects, master chain from the song, PDC plan from the bank's slots), run in 128-frame steps; `ops` run between quanta (key: the quantum index before which they run)
Ref reference(const Song& song, const RenderPads& rp, int pdcMode, bool page, bool effects, bool master, int64_t N, const std::vector<std::pair<int64_t, RefOp>>& ops = {}, bool meters = true) {
  Ref out; SeqVars vars; SeqOpts so; so.once = false; so.live = false; so.sim = false; so.randomSeed = 0x2545F491u;
  SongRenderer R(song, vars, rp, SR, 512, so, 64);
  R.pool().setPageTiming(page); R.mixer().setPageClock(page);
  if (effects) R.setEffects(true);
  if (master) R.setMasterChain(master_chain_from_song(song.mst, song.limiter));
  if (effects && pdcMode) {
    const PdcMode mode = pdcMode == 2 ? PdcMode::On : PdcMode::Auto;
    std::vector<PdcSlot> slots(1024); const int ns = R.fxBank() ? R.fxBank()->pdcSlots(slots.data(), (int)slots.size()) : 0;
    R.setPlan(pdc_plan(bus_plan(song.chainPatch, song.chainOn), slots.data(), ns, SR, mode), mode, 0);
  }
  MeterBank bank(SR); if (meters) R.setMeters(&bank);
  out.l.resize((size_t)N); out.r.resize((size_t)N);
  for (int64_t q = 0; q * 128 < N; q++) {
    for (auto& op : ops) if (op.first == q) op.second(R);
    const int n = (int)std::min<int64_t>(128, N - q * 128);
    R.block(out.l.data() + q * 128, out.r.data() + q * 128, n);
    MeterEvent e; while (bank.pop(&e)) out.meters.push_back(e);
  }
  out.latency = R.latencyFrames();
  return out;
}

struct Run { std::vector<float> l, r; std::vector<std::vector<uint8_t>> recs; int rc = 0; thunder_live_stats st; };
// the live host: song, pads, play at t0 = f0; `steps` = (frame offset from f0, action) run between process calls (the offset is where the NEXT process call starts: a multiple of 128 for an exact quantum)
typedef std::function<void(Host&, int64_t f0)> LiveOp;
Run live(const std::string& text, const PadSet& ps, uint32_t flags, int pdcMode, int cs, int64_t f0, int64_t N, bool meters = true, const std::vector<std::pair<int64_t, LiveOp>>& ops = {}, const std::function<void(Host&)>& before = nullptr) {
  Run o; Host H(opts(flags, 4096)); if (!H.h) { o.rc = -1; return o; }
  H.song(text); H.loadPads(ps);
  if (before) before(H);
  if (meters) H->meters(30);
  thunder_live_play_opts po = playOpts(f0); po.pdc_mode = pdcMode; if (H->play(po)) { o.rc = -2; return o; }
  int64_t frame = f0, done = 0; size_t oi = 0; std::vector<std::pair<int64_t, LiveOp>> sorted = ops;
  while (done < N) {
    while (oi < sorted.size() && sorted[oi].first <= done) { sorted[oi].second(H, f0); oi++; }
    int64_t next = N; if (oi < sorted.size()) next = std::min(next, sorted[oi].first);
    const int64_t n = std::min<int64_t>(next - done, 8192);
    o.rc = H.run(o.l, o.r, n, cs, frame); if (o.rc) return o;
    done += n;
    for (auto& r : H.poll()) o.recs.push_back(r);
  }
  for (auto& r : H.poll()) o.recs.push_back(r);
  o.st = stats(H.h);
  return o;
}
size_t diff(const std::vector<float>& a, const std::vector<float>& b, size_t* first) {
  size_t bad = 0; *first = (size_t)-1; const size_t n = std::min(a.size(), b.size());
  for (size_t i = 0; i < n; i++) if (std::memcmp(&a[i], &b[i], 4) != 0) { if (*first == (size_t)-1) *first = i; bad++; }
  return bad + (a.size() > b.size() ? a.size() - b.size() : b.size() - a.size());
}
// the METERS records of a live run against the reference bank's events
void checkMeters(const Run& L, const Ref& R, int64_t f0, const char* what) {
  std::vector<std::vector<uint8_t>> m; for (auto& r : L.recs) if (evType(r) == THUNDER_LIVE_EV_METERS) m.push_back(r);
  CHECK(m.size() == R.meters.size() && !m.empty(), "%s: %zu METERS records, the reference bank made %zu", what, m.size(), R.meters.size());
  size_t bad = 0; bool order = true; uint32_t lastSeq = 0;
  for (size_t i = 0; i < std::min(m.size(), R.meters.size()); i++) {
    const thunder_live_ev_meters e = evAs<thunder_live_ev_meters>(m[i]); const MeterEvent& x = R.meters[i];
    uint32_t bs; std::memcpy(&bs, &e.reserved[0], 4);
    if (std::memcmp(e.a, x.a, sizeof e.a) != 0 || bs != x.seq || e.h.f != f0 + (int64_t)std::llround(x.at * SR)) bad++;
    if (evSeq(m[i]) <= lastSeq) order = false; lastSeq = evSeq(m[i]);
  }
  CHECK(bad == 0, "%s: %zu METERS records differ from the reference bank's (peak / rms arrays, count, end frame)", what, bad);
  CHECK(order, "%s: METERS records in the ring's seq order", what);
}
}  // namespace

int main() {
  const std::string text = electro();
  if (text.empty()) { std::printf("FAIL: cannot read ELECTRO\n"); return 1; }
  PadSet ps; static RenderPads rp; fillRenderPads(rp, ps);
  auto loadSong = [&](const std::string& t, Song& s) { JsonError je; return s.load(t.data(), t.size(), &je, /*pageS*/ true); };
  Song song; if (!loadSong(text, song)) { std::printf("FAIL: ELECTRO does not load\n"); return 1; }
  const uint32_t FX = THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER | THUNDER_LIVE_F_METERS;
  const int64_t N = (int64_t)(SR * 14);

  // ---- 1. THE DONE-TEST: every effect slot, the master chain, PDC and the meters, bit for bit, whatever the call size and frame0 --------------------------------------------------------------------------
  struct Var { const char* name; std::string text; int pdc; bool page; };
  std::vector<Var> vars;
  vars.push_back({"ELECTRO (pdc auto)", text, 1, false});
  { std::string t = text; const size_t at = t.find("\"limiter\":true"); if (at != std::string::npos) t.replace(at, 14, "\"limiter\":false"); vars.push_back({"ELECTRO, safety limiter off, pdc ON", t, 2, false}); }
  vars.push_back({"ELECTRO, pdc OFF", text, 0, false});
  vars.push_back({"ELECTRO, the page's timing", text, 1, true});
  for (const Var& v : vars) {
    Song sv; if (!loadSong(v.text, sv)) { std::printf("FAIL: variant %s does not load\n", v.name); return 1; }
    const Ref ref = reference(sv, rp, v.pdc, v.page, true, true, N);
    double peak = 0; for (float x : ref.l) peak = std::max(peak, (double)std::fabs(x));
    CHECK(peak > 0.01, "%s: the reference is not silent (peak %g)", v.name, peak);
    CHECK(!ref.meters.empty(), "%s: the reference bank made events", v.name);
    struct Case { int cs; int64_t f0; };
    const Case cases[] = {{128, 0}, {480, 37}, {4096, (int64_t)1 << 31}, {333, 1000}};
    for (const Case& c : cases) {
      const Run L = live(v.text, ps, FX | (v.page ? THUNDER_LIVE_F_PAGE_TIMING : 0), v.pdc, c.cs, c.f0, N);
      CHECK(L.rc == 0, "%s: process rc %d", v.name, L.rc);
      size_t first; const size_t bad = diff(L.l, ref.l, &first), bad2 = diff(L.r, ref.r, &first);
      CHECK(bad == 0 && bad2 == 0, "%s, call size %d, frame0 %lld: %zu + %zu samples differ, first at %zu", v.name, c.cs, (long long)c.f0, bad, bad2, first);
      checkMeters(L, ref, c.f0, v.name);
      CHECK(L.st.latency_frames == (int32_t)ref.latency, "%s: stats.latency_frames %d, SongRenderer::latencyFrames %lld", v.name, L.st.latency_frames, (long long)ref.latency);
      CHECK(L.st.fx_failed_blocks == 0 && L.st.seq_dropped == 0 && L.st.evt_dropped == 0 && L.st.reanchors == 0, "%s: no failed effects block, no dropped event, no re-anchor", v.name);
      std::printf("%-40s call size %4d, frame0 %12lld: %s (latency %d frames, %zu METERS, %u pump remakes)\n", v.name, c.cs, (long long)c.f0, bad + bad2 ? "DIFFERENT" : "bit-equal", L.st.latency_frames, ref.meters.size(), L.st.fx_remakes);
    }
  }
  // the latency is not zero for ELECTRO (master chain + pump slots): the check above would be empty otherwise
  { const Ref r1 = reference(song, rp, 1, false, true, true, 4096); CHECK(r1.latency > 0, "ELECTRO has a latency to report (%lld)", (long long)r1.latency); }

  // ---- 2. EDITS WHILE PLAYING, each equal to the same edit made on a SongRenderer between two quanta -----------------------------------------------------------------------------------------------------------
  // a song with no track effects of its own at track 0..: the edit makes them. (ELECTRO's own track 0 has none.)
  const int64_t Q = 64 * 128;   // the edit at quantum 64 (8192 frames)
  const int64_t N2 = (int64_t)(SR * 6);
  const std::string slotsDelay = "[{\"type\":\"delay\",\"on\":true,\"time\":3,\"fb\":60,\"mix\":50}]", slotsTape = "[{\"type\":\"tape\",\"on\":true},{\"type\":\"reverb\",\"on\":true}]";
  auto planFor = [&](SongRenderer& R, int track, const std::string& js) -> FxPlan* { JsonDoc d; JsonError je; d.parse(js.data(), js.size(), &je); return R.fxBank()->host(track).planFromSong(d.root(), song.bpm, 0, nullptr); };
  std::vector<FxPlan*> keep;   // the renderer-side plans (the reference's hosts are gone when reference() returns; deleted at the end of the test)
  {   // fx_plan + fx_commit at a quantum (track 0, then the master), and fx_param after it, against SongRenderer + FxHost::commit / psetAt between blocks
    std::vector<std::pair<int64_t, RefOp>> rops;
    rops.push_back({64, [&](SongRenderer& R) { FxPlan* p = planFor(R, 0, slotsDelay); R.fxBank()->host(0).commit(p); R.mixer().setFx(0, &FxBank::hook, R.fxBank()); keep.push_back(p); }});
    rops.push_back({128, [&](SongRenderer& R) { double p12[12]; for (int i = 0; i < 12; i++) p12[i] = 0.1 * i; R.fxBank()->host(0).psetAt(128 * 128.0 / SR, 0, p12); }});
    rops.push_back({200, [&](SongRenderer& R) { FxPlan* p = planFor(R, 5, slotsTape); R.fxBank()->host(5).commit(p); R.mixer().setFx(5, &FxBank::hook, R.fxBank()); keep.push_back(p); }});
    const Ref ref = reference(song, rp, 0, false, true, true, N2, rops);   // (with ELECTRO's own slots on the other tracks; PDC off here so the edit's latency is not in the way)
    { const Ref unedited = reference(song, rp, 0, false, true, true, N2); size_t f1; const size_t nd = diff(unedited.l, ref.l, &f1);
      CHECK(nd > 1000, "the fx edit is audible (%zu samples differ from the unedited render)", nd); }
    for (int cs : {128, 4096}) {
      std::vector<std::pair<int64_t, LiveOp>> lops;
      lops.push_back({0, [&](Host& H, int64_t f0) {
        CHECK(H->fxPlan(0, (const uint8_t*)slotsDelay.data(), slotsDelay.size(), song.bpm) == 0, "fx_plan track 0");
        CHECK(H->fxCommit(0, f0 + 64 * 128) == 0, "fx_commit track 0 at quantum 64");
        double p12[12]; for (int i = 0; i < 12; i++) p12[i] = 0.1 * i; CHECK(H->fxParam(0, 0, p12, f0 + 128 * 128) == 0, "fx_param");
        CHECK(H->fxPlan(5, (const uint8_t*)slotsTape.data(), slotsTape.size(), song.bpm) == 0, "fx_plan track 5");
        CHECK(H->fxCommit(5, f0 + 200 * 128) == 0, "fx_commit track 5 at quantum 200");
      }});
      const Run L = live(text, ps, FX, 0, cs, 12345, N2, true, lops);
      size_t first; const size_t bad = diff(L.l, ref.l, &first), bad2 = diff(L.r, ref.r, &first);
      CHECK(bad == 0 && bad2 == 0, "fx_plan / fx_commit / fx_param, call size %d: %zu + %zu samples differ, first at %zu", cs, bad, bad2, first);
      std::printf("fx edits (plan, commit at a quantum, param, a second track), call size %4d: %s\n", cs, bad + bad2 ? "DIFFERENT" : "bit-equal");
    }
    // the plans came back: collect frees them (and a plan that was never committed does not leak: the host's destructor frees it)
    { Host H(opts(FX, 4096)); H.song(text); H.loadPads(ps); H->meters(0);
      CHECK(H->fxPlan(2, (const uint8_t*)slotsDelay.data(), slotsDelay.size(), 120) == 0, "fx_plan for the return test");
      CHECK(H->fxPlan(2, (const uint8_t*)slotsDelay.data(), slotsDelay.size(), 120) == THUNDER_LIVE_E_STATE, "a second fx_plan for a track whose plan is staged is E_STATE");
      thunder_live_play_opts po = playOpts(0); CHECK(H->play(po) == THUNDER_LIVE_E_STATE, "play with a staged plan is E_STATE");
      CHECK(H->fxCommit(3, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_STATE, "fx_commit with nothing staged is E_STATE");
      CHECK(H->fxCommit(2, THUNDER_LIVE_NOW) == 0, "fx_commit");
      CHECK(H.run(1024, 128, *std::make_unique<int64_t>(0)) == 0, "process");
      CHECK(H->collect() >= 1, "collect frees the committed plan");
      CHECK(H->fxPlan(2, (const uint8_t*)slotsTape.data(), slotsTape.size(), 120) == 0, "a new plan after the commit");   // (left staged: the destructor frees it)
    }
  }
  {   // P3 fix (verify-301 finding 1): effects plans of a track are committed IN THE ORDER THEY WERE PLANNED
    const int64_t N3 = (int64_t)SR * 2;
    {   // (a) a stale edit (planned and committed with a far-future `at` BEFORE the play) waits in the list; the play's own plans were made after it and win: equal to a host that never had the edit
      const Run clean = live(text, ps, FX, 0, 128, 0, N3, true);
      const Ref ref = reference(song, rp, 0, false, true, true, N3);
      const Run L = live(text, ps, FX, 0, 128, 0, N3, true, {}, [&](Host& H) {
        CHECK(H->fxPlan(0, (const uint8_t*)slotsDelay.data(), slotsDelay.size(), song.bpm) == 0, "stale: fx_plan");
        CHECK(H->fxCommit(0, (int64_t)1 << 40) == 0, "stale: fx_commit far in the future"); });
      size_t first; const size_t bad = diff(L.l, ref.l, &first), bad2 = diff(L.r, ref.r, &first), c1 = diff(L.l, clean.l, &first), c2 = diff(L.r, clean.r, &first);
      CHECK(bad == 0 && bad2 == 0 && c1 == 0 && c2 == 0, "a stale waiting fx_commit before the play: %zu + %zu samples differ from SongRenderer, %zu + %zu from a host without the edit (first at %zu)", bad, bad2, c1, c2, first);
      CHECK(L.st.cmds_cleared == 0, "the stale commit is committed, not counted as cleared (%u)", (unsigned)L.st.cmds_cleared);
      std::printf("stale waiting fx_commit, then a play: %s\n", bad + bad2 + c1 + c2 ? "DIFFERENT" : "bit-equal");
    }
    {   // (b) a commit with a later `at` (quantum 40), then a NOW commit of a later plan, same track: the NOW commit applies first and takes the earlier plan with it; the chain ends as the LATER plan's
      std::vector<std::pair<int64_t, RefOp>> rops;
      rops.push_back({0, [&](SongRenderer& R) {
        FxPlan* a = planFor(R, 0, slotsDelay); FxPlan* b = planFor(R, 0, slotsTape);
        R.fxBank()->host(0).commit(a); R.fxBank()->host(0).commit(b); R.mixer().setFx(0, &FxBank::hook, R.fxBank()); keep.push_back(a); keep.push_back(b); }});
      const Ref ref = reference(song, rp, 0, false, true, true, N3, rops);
      for (int cs : {128, 4096}) {
        std::vector<std::pair<int64_t, LiveOp>> lops;
        lops.push_back({0, [&](Host& H, int64_t) {
          CHECK(H->fxPlan(0, (const uint8_t*)slotsDelay.data(), slotsDelay.size(), song.bpm) == 0, "order: fx_plan A");
          CHECK(H->fxCommit(0, 40 * 128) == 0, "order: fx_commit A (quantum 40)");
          CHECK(H->fxPlan(0, (const uint8_t*)slotsTape.data(), slotsTape.size(), song.bpm) == 0, "order: fx_plan B");
          CHECK(H->fxCommit(0, THUNDER_LIVE_NOW) == 0, "order: fx_commit B (now)"); }});
        const Run L = live(text, ps, FX, 0, cs, 0, N3, true, lops);
        size_t first; const size_t bad = diff(L.l, ref.l, &first), bad2 = diff(L.r, ref.r, &first);
        CHECK(bad == 0 && bad2 == 0, "a commit at quantum 40 then a now commit (call size %d): %zu + %zu samples differ, first at %zu", cs, bad, bad2, first);
        std::printf("fx_commit at quantum 40 then a now fx_commit, call size %4d: %s\n", cs, bad + bad2 ? "DIFFERENT" : "bit-equal");
      }
    }
  }
  {   // refusals
    Host H(opts(0, 4096)); const std::string js = "[]";
    CHECK(H->fxPlan(0, (const uint8_t*)js.data(), js.size(), 120) == THUNDER_LIVE_E_STATE && H->fxCommit(0, -1) == THUNDER_LIVE_E_STATE && H->masterConfig(nullptr, 0, true) == THUNDER_LIVE_E_STATE, "without F_EFFECTS / F_MASTER: E_STATE");
    double p12[12] = {0}; CHECK(H->fxParam(0, 0, p12, -1) == THUNDER_LIVE_E_STATE && H->meters(30) == THUNDER_LIVE_E_STATE, "without F_EFFECTS / F_METERS: E_STATE");
    Host G(opts(FX, 4096)); const std::string bad = "{\"type\":1}", junk = "[[[";
    CHECK(G->fxPlan(33, (const uint8_t*)js.data(), js.size(), 120) == THUNDER_LIVE_E_ARG && G->fxPlan(-1, (const uint8_t*)js.data(), js.size(), 120) == THUNDER_LIVE_E_ARG, "fx_plan: track out of range");
    CHECK(G->fxPlan(0, (const uint8_t*)bad.data(), bad.size(), 120) == THUNDER_LIVE_E_ARG && G->fxPlan(0, (const uint8_t*)junk.data(), junk.size(), 120) == THUNDER_LIVE_E_ARG && G->fxPlan(0, nullptr, 0, 120) == THUNDER_LIVE_E_ARG, "fx_plan: not a slot list");
    CHECK(G->fxPlan(0, (const uint8_t*)js.data(), js.size(), 0) == THUNDER_LIVE_E_ARG && G->fxPlan(0, (const uint8_t*)js.data(), js.size(), NAN) == THUNDER_LIVE_E_ARG, "fx_plan: bpm");
    CHECK(G->fxCommit(33, -1) == THUNDER_LIVE_E_ARG, "fx_commit: track out of range");
    CHECK(G->fxParam(0, 8, p12, -1) == THUNDER_LIVE_E_ARG && G->fxParam(0, -1, p12, -1) == THUNDER_LIVE_E_ARG && G->fxParam(40, 0, p12, -1) == THUNDER_LIVE_E_ARG && G->fxParam(0, 0, nullptr, -1) == THUNDER_LIVE_E_ARG, "fx_param: slot / track / null");
    p12[3] = NAN; CHECK(G->fxParam(0, 0, p12, -1) == THUNDER_LIVE_E_ARG, "fx_param: NaN"); p12[3] = 0;
    CHECK(G->meters(31) == THUNDER_LIVE_E_ARG, "meters above 30 Hz");
    Host M(opts(THUNDER_LIVE_F_METERS, 4096)); CHECK(M->meters(-1) == THUNDER_LIVE_E_ARG && M->meters(31) == THUNDER_LIVE_E_ARG && M->meters(NAN) == THUNDER_LIVE_E_ARG && M->meters(30) == 0 && M->meters(0) == 0, "meters rate checks");
    CHECK(G->pdcSet(3) == THUNDER_LIVE_E_ARG && G->pdcSet(1) == THUNDER_LIVE_E_STATE, "pdc_set: bad mode, no song");
    Host F(opts(THUNDER_LIVE_F_MASTER, 4096)); const std::string notjson = "{{{"; CHECK(F->masterConfig((const uint8_t*)notjson.data(), notjson.size(), true) == THUNDER_LIVE_E_ARG, "master_config: not JSON");
    CHECK(F->masterConfig(nullptr, 0, true) == 0, "master_config: NULL is freshMst");
    // F_MIDI is still P4's
    thunder_live_opts o = opts(THUNDER_LIVE_F_MIDI); char err[128]; CHECK(LiveHost::create(o, err, sizeof err) == nullptr, "F_MIDI is refused until P4");
  }

  // ---- 3. PDC, master and meters edits against the same edit on the renderer ------------------------------------------------------------------------------------------------------------------------
  {   // pdc_set (mode Off -> Auto) at quantum 64: the delays glide 50 ms (pdc_apply live), equal to the renderer's pdc_apply(live) between blocks
    std::vector<std::pair<int64_t, RefOp>> rops;
    rops.push_back({64, [&](SongRenderer& R) {
      std::vector<PdcSlot> slots(1024); const int ns = R.fxBank()->pdcSlots(slots.data(), (int)slots.size());
      const PdcPlan pl = pdc_plan(bus_plan(song.chainPatch, song.chainOn), slots.data(), ns, SR, PdcMode::Auto);
      R.mixer().setTime(64 * 128 / SR); PdcApply a; a.mode = PdcMode::Auto; a.live = true; a.playing = true; a.now = false; pdc_apply(R.mixer(), pl, a); }});
    // the reference starts with PDC "off" (no plan applied: all zeros) which is what a play with pdc_mode 0 gives
    const Ref ref = reference(song, rp, 0, false, true, true, N2, rops);
    std::vector<std::pair<int64_t, LiveOp>> lops;
    lops.push_back({Q, [&](Host& H, int64_t) { CHECK(H->pdcSet(1) == 0, "pdc_set"); }});
    const Run L = live(text, ps, FX, 0, 128, 777, N2, true, lops);
    size_t first; const size_t bad = diff(L.l, ref.l, &first), bad2 = diff(L.r, ref.r, &first);
    CHECK(bad == 0 && bad2 == 0, "pdc_set at a quantum: %zu + %zu samples differ, first at %zu", bad, bad2, first);
    CHECK(L.st.latency_frames == (int32_t)ref.latency && ref.latency > 0, "the latency after pdc_set (%d vs the renderer's %lld with its own plan in force)", L.st.latency_frames, (long long)ref.latency);
    std::printf("pdc_set (off -> auto, glide) at a quantum: %s\n", bad + bad2 ? "DIFFERENT" : "bit-equal");
  }
  {   // master_config: the song's own config again (nothing changes), and a different one (comp threshold, limiter off)
    const std::string mstJs = "{\"on\":true,\"comp\":{\"on\":true,\"thr\":-30,\"ratio\":8},\"lim\":{\"on\":false}}";
    for (int kind = 0; kind < 2; kind++) {
      MasterChainConfig cfg2; { JsonDoc d; JsonError je; d.parse(mstJs.data(), mstJs.size(), &je); cfg2 = master_chain_from_song(d.root(), true); }
      std::vector<std::pair<int64_t, RefOp>> rops;
      if (kind == 1) rops.push_back({64, [&](SongRenderer& R) { R.masterChain()->configure(cfg2); }});
      else rops.push_back({64, [&](SongRenderer& R) { R.masterChain()->configure(master_chain_from_song(song.mst, song.limiter)); }});
      const Ref ref = reference(song, rp, 0, false, true, true, N2, rops);
      const Ref base = reference(song, rp, 0, false, true, true, N2);
      std::vector<std::pair<int64_t, LiveOp>> lops;
      if (kind == 1) lops.push_back({Q, [&](Host& H, int64_t) { CHECK(H->masterConfig((const uint8_t*)mstJs.data(), mstJs.size(), true) == 0, "master_config"); }});
      else lops.push_back({Q, [&](Host& H, int64_t) { // the song's own mst object, written out of the song text
        const size_t at = text.find("\"mst\":"); size_t b = at + 6, depth = 0, e = b; do { if (text[e] == '{') depth++; if (text[e] == '}') depth--; e++; } while (depth > 0 && e < text.size());
        CHECK(H->masterConfig((const uint8_t*)text.data() + b, e - b, true) == 0, "master_config (the song's own)"); }});
      const Run L = live(text, ps, FX, 0, 4096, 0, N2, true, lops);
      size_t first; const size_t bad = diff(L.l, ref.l, &first), bad2 = diff(L.r, ref.r, &first);
      CHECK(bad == 0 && bad2 == 0, "master_config (%s) at a quantum: %zu + %zu samples differ, first at %zu", kind ? "a different chain" : "the song's own again", bad, bad2, first);
      size_t f2; const size_t nd = diff(base.l, ref.l, &f2);
      if (kind == 1) CHECK(nd > 1000, "a different master chain is audible (%zu samples differ from the unchanged render)", nd);
      for (float x : L.l) if (!(x == x)) { CHECK(false, "NaN after master_config"); break; }
      std::printf("master_config (%s) at a quantum: %s\n", kind ? "a different chain" : "the song's own", bad + bad2 ? "DIFFERENT" : "bit-equal");
    }
  }
  {   // meters_want: off gives no METERS record, on gives them at the wanted rate, off again stops them
    Host H(opts(THUNDER_LIVE_F_METERS, 4096)); H.song(text); H.loadPads(ps);
    thunder_live_play_opts po = playOpts(0); H->play(po); int64_t frame = 0; std::vector<float> l, r;
    H.run(l, r, (int64_t)SR, 128, frame); int off1 = 0; for (auto& rec : H.poll()) if (evType(rec) == THUNDER_LIVE_EV_METERS) off1++;
    CHECK(off1 == 0, "no METERS records until switched on (%d)", off1);
    CHECK(H->meters(10) == 0, "meters 10 Hz"); H.run(l, r, (int64_t)SR * 2, 128, frame); int on1 = 0; for (auto& rec : H.poll()) if (evType(rec) == THUNDER_LIVE_EV_METERS) on1++;
    CHECK(on1 >= 19 && on1 <= 21, "about 10 METERS records a second for 2 s (%d)", on1);
    CHECK(H->meters(0) == 0, "meters off"); H.run(l, r, (int64_t)SR, 128, frame); int off2 = 0; for (auto& rec : H.poll()) if (evType(rec) == THUNDER_LIVE_EV_METERS) off2++;
    CHECK(off2 <= 1, "switched off again: no more records (%d)", off2);
  }
  {   // stop: LATENCY COMP AUTO lets go (latency_frames loses the PDC's T and keeps the master chain's), the mode ON keeps it; play again takes it back
    for (int mode = 1; mode <= 2; mode++) {
      Host H(opts(FX, 4096)); H.song(text); H.loadPads(ps); thunder_live_play_opts po = playOpts(0); po.pdc_mode = mode; H->play(po);
      int64_t frame = 0; std::vector<float> l, r; H.run(l, r, 4096, 128, frame); const int during = stats(H.h).latency_frames;
      H->stop(THUNDER_LIVE_NOW); H.run(l, r, 4096, 128, frame); const int after = stats(H.h).latency_frames;
      CHECK(during > 0, "mode %d: latency while playing (%d)", mode, during);
      if (mode == 1) CHECK(after <= during, "AUTO: after the stop the PDC's part is gone or nothing (%d -> %d)", during, after); else CHECK(after == during, "ON: the latency stays after the stop (%d -> %d)", during, after);
      H->play(po); H.run(l, r, 4096, 128, frame); CHECK(stats(H.h).latency_frames == during, "mode %d: the next play takes the latency back (%d)", mode, stats(H.h).latency_frames);
    }
  }
  {   // a cancelled play still commits its plans (the plans were made against each other): stop with a play stalled / a play replaced by a newer one, then a third play: all three go through without a crash and equal a plain play
    Host H(opts(FX, 4096)); H.song(text); H.loadPads(ps); thunder_live_play_opts po = playOpts(0); int64_t frame = 0; std::vector<float> l, r;
    CHECK(H->play(po) == 0 && H->play(po) == 0 && H->stop(THUNDER_LIVE_NOW) == 0 && H->play(po) == 0, "three plays and a stop queued");
    CHECK(H.run(l, r, 48000, 128, frame) == 0, "process"); H->collect();
    const Ref ref = reference(song, rp, 1, false, true, true, 48000);
    CHECK(stats(H.h).fx_failed_blocks == 0, "no failed effects block");
    size_t first; const size_t bad = diff(l, ref.l, &first), bad2 = diff(r, ref.r, &first);
    CHECK(bad == 0 && bad2 == 0, "plays cancelled by a stop / a newer play: the last play equals a plain play (%zu + %zu samples differ, first at %zu)", bad, bad2, first);
  }
  for (FxPlan* p : keep) delete p;
  std::printf("live_fx: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
