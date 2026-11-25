// C2b (ctest `live_chains`): the chains host LIVE through LiveHost (docs/port/C2-DESIGN.md, "C2b: done when" 1, 2 and the API part of 4).
//  1. ELECTRO through LiveHost (F_CHAINS | F_EFFECTS | F_MASTER) equals SongRenderer with setChains + setEffects + setMasterChain BIT FOR BIT at call sizes 1 / 128 / 333 / 480 / 4096 and frame0 0 / 37 / 2^31 (the golden songs of
//     C1 are in `chains_song`, level 3); without F_CHAINS the output is today's and the flag adds the chain; through the C API the same.
//  2. Edits while the song plays, on a small song whose chain has an oscillator (module state a reset would show): (i) chain_edit of a parameter, a publish that changes the same parameter and a direct setParam at that quantum give the
//     same samples; (ii) a structural publish swaps the patch (state reset), the retired patch is freed by collect(); (iii) a CHN switch (bus plan only) keeps the node, its patch and its state (keep-patch); (iv) a second structural
//     publish before the audio side has seen the first is E_BUSY with NOTHING changed; (v) play / stop / play: transport and startN; HELD keeps playing, ENDED stops 0.3 s after the end; (vi) a refused patch and an over-budget plan:
//     the call succeeds, the counters move, the sound continues; chain_edit's errors; events through the sequencer (BYP, TPO); the mixer's bus plan is the commit's alone.
#include <algorithm>
#include <thread>

#include "chains/chost.h"
#include "live_common.h"
#include "master/chain.h"
#include "render/render.h"

using namespace lt;
constexpr int Q = THUNDER_LIVE_QUANTUM;
#ifdef THUNDER_SANITIZE_BUILD
static const bool kSan = true;
#else
static const bool kSan = false;
#endif
static const uint32_t FC = THUNDER_LIVE_F_CHAINS;

// ---- the small song: track 1 (a bus) plays a note and carries a chain: Track In 1 + an oscillator -> Stereo mixer -> Chain Out 2. Track 0 is a source with no sound (no pad): the oscillator is the whole sound and its phase is the state.
static std::string patchOf(double freq, int extra, bool hostile = false) {
  std::string m = "{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"osc1\",\"type\":\"osc\",\"params\":{\"wave\":1,\"freq\":" + std::to_string(freq) + ",\"fm\":0,\"level\":0.5}},"
    "{\"id\":\"mA\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"level\":1,\"l1\":1,\"p1\":0,\"l2\":1,\"p2\":0}},{\"id\":\"o2\",\"type\":\"chainOut\",\"params\":{\"track\":2,\"level\":1}}";
  for (int k = 1; k <= extra; k++) m += ",{\"id\":\"z" + std::to_string(k) + "\",\"type\":\"vca\",\"params\":{\"gain\":1}}";
  std::string c = hostile ? "null" : "{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in2\"]},{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},"
    "{\"from\":[\"mA\",\"out\"],\"to\":[\"o2\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"o2\",\"inR\"]}";
  return "{\"format\":\"chains.patch\",\"version\":1,\"modules\":[" + m + "],\"cables\":[" + c + "]}";
}
// row0: the phrase's first row on track 1 (a note, optionally BYP / TPO commands); on0: the CHN flag of track 0
static std::string songOf(const std::string& patch, bool on0 = true, int bpm = 120, const char* row0 = "[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]") {
  std::string s = "{\"v\":6,\"bpm\":" + std::to_string(bpm) + ",\"lpb\":4,\"ticks\":6,\"master\":255,\"mout\":255,\"ntr\":8,\"song\":[[-1,0";
  for (int t = 2; t < kSongNT; t++) s += ",-1";
  s += "]],\"chains\":[[[0,0]]],\"plen\":[8],\"phrases\":[[" + std::string(row0);
  for (int i = 1; i < 8; i++) s += ",[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]";
  s += "]],\"mix\":[";
  for (int t = 0; t < kSongNT; t++) s += std::string(t ? "," : "") + "{\"vol\":224,\"trim\":128,\"pan\":128,\"mute\":false,\"solo\":false}";
  s += "],\"chainOn\":[" + std::string(on0 ? "true" : "false") + ",true";
  for (int t = 2; t < kSongNT; t++) s += ",false";
  s += "],\"chainPatch\":" + patch + "}";
  return s;
}

struct Rig {
  Host H; int64_t fr = 0; std::vector<float> l, r;
  explicit Rig(const thunder_live_opts& o) : H(o) {}
  int run(int64_t n) { return H.run(l, r, n, Q, fr); }
  int play(const thunder_live_play_opts* p = nullptr) { thunder_live_play_opts po = p ? *p : playOpts(THUNDER_LIVE_NOW); return H->play(po); }
  ChainsHost* ch() { return H->chainsHostForTest(); }
  thunder_live_stats st() { return stats(H.h); }
};
static thunder_live_opts copts(uint32_t flags = 0, uint64_t chainsBytes = 0) { thunder_live_opts o = opts(flags | FC, 4096); o.chains_bytes = chainsBytes; return o; }
static double peak(const std::vector<float>& v, size_t from = 0) { double p = 0; for (size_t i = from; i < v.size(); i++) p = std::max(p, (double)std::fabs(v[i])); return p; }
static size_t ndiff(const std::vector<float>& a, const std::vector<float>& b, size_t from = 0) {
  size_t bad = 0; if (a.size() != b.size()) return a.size() + b.size() + 1;
  for (size_t i = from; i < a.size(); i++) if (std::memcmp(&a[i], &b[i], 4) != 0) bad++;
  return bad;
}
static const char* kEditOsc = "[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":441}]";
static int edit(Rig& R, const char* j) { return R.H->chainEdit((const uint8_t*)j, std::strlen(j), THUNDER_LIVE_NOW); }
static const int64_t kEdit = 201 * Q;   // the edit point: a quantum boundary where 330 Hz is not at phase 0 (25728 * 330 / 48000 = 176.88 cycles)

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void testTransport() {
  std::printf("transport\n");
  { Rig a(copts()); CHECK(a.H.h != nullptr, "create with F_CHAINS"); if (!a.H.h) return;
    CHECK(a.ch() && !a.ch()->playing(), "R10: a live host starts not playing (create calls stop())");
    CHECK(a.H.song(songOf(patchOf(330, 0), true, 131)) == 0 && a.play() == 0, "song, play"); a.run(Q * 8);
    chains::ChainsNode* n = a.ch()->node(1); CHECK(n != nullptr, "the bus track has a node");
    if (n) { const waves::Transport t = n->engine().transport(); CHECK(t.playing && t.startN == 1 && t.tempo == 131 && t.lpb == 4 && t.ticks == 6, "play: {tempo 131 (the Seq's), playing, startN 1} (tempo %g startN %g playing %d)", t.tempo, t.startN, (int)t.playing); }
    CHECK(a.ch()->playing(), "the host is playing");
    // an event far in the future waits in the queue; a stop clears it and the nodes stop
    SeqEvent e; e.type = SEV_TEMPO; e.t = 100.0; e.v = 99; e.tks = 6; CHECK(a.ch()->event(e) && a.ch()->pendingEvents() == 1, "a look-ahead tempo event is queued");
    CHECK(a.H->stop(THUNDER_LIVE_NOW) == 0, "stop"); a.run(Q * 2);
    CHECK(a.ch()->pendingEvents() == 0, "R10/R11: a stop clears the queue (look-ahead events of a Seq that is gone are not applied)");
    CHECK(!a.ch()->playing() && n && !n->engine().transport().playing, "stop: not playing, applied to the node");
    CHECK(n && a.ch()->node(1) == n, "the node stays hooked while IDLE (its tails ring out)");
    CHECK(a.st().chains_nodes == 1, "stats.chains_nodes");
    e.t = 100.0; a.ch()->event(e);
    CHECK(a.play() == 0, "play again"); a.run(Q * 4);
    CHECK(a.ch()->pendingEvents() == 0, "a started play clears the queue");
    if (n) { const waves::Transport t = n->engine().transport(); CHECK(t.playing && t.startN == 2 && t.tempo == 131, "play / stop / play: startN 2 (startN %g)", t.startN); }
    // a play over a playing song goes through not playing, as the page's tStart does (tStop(true) first)
    CHECK(a.play() == 0, "play over a play"); a.run(Q * 4);
    if (n) CHECK(n->engine().transport().startN == 3 && n->engine().transport().playing, "play over a play: startN 3");
  }
  {   // HELD keeps playing until the stop; ENDED stops 0.3 s after the song's end (the page's tp.on: tStop at the end of the hold / 0.3 s after the end)
    thunder_live_opts o = copts(THUNDER_LIVE_F_DUMP_STEPS); Rig a(o); a.H.song(songOf(patchOf(330, 0))); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); po.test_once = 1; a.play(&po);
    bool ended = false, idle = false, playingWhileEnded = true; int k = 0;
    for (; k < 600 && !idle; k++) { a.run(Q * 8); const int s = a.st().state; if (s == THUNDER_LIVE_STATE_ENDED) { ended = true; playingWhileEnded = playingWhileEnded && a.ch()->playing(); } if (s == THUNDER_LIVE_STATE_IDLE && ended) idle = true; }
    CHECK(ended && idle, "the song ended and the 0.3 s timer ran (%d)", k); CHECK(playingWhileEnded, "ENDED: the chains still play"); CHECK(!a.ch()->playing(), "after the 0.3 s timer (IDLE): stop() as the page's tStop");
    Rig b(copts()); b.H.song(songOf(patchOf(330, 0))); b.play(); b.run(Q * 20); b.H->hold(THUNDER_LIVE_NOW); bool held = false; for (int i = 0; i < 400 && !held; i++) { b.run(Q * 8); held = b.st().state == THUNDER_LIVE_STATE_HELD; }
    CHECK(held, "HELD reached"); b.run(Q * 8); CHECK(b.ch()->playing(), "HELD: the chains keep playing until the stop");
    b.H->stop(THUNDER_LIVE_NOW); b.run(Q * 2); CHECK(!b.ch()->playing(), "the stop after HELD: not playing");
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void testEdits() {
  std::printf("edits while playing\n");
  const std::string S0 = songOf(patchOf(330, 0)), SF = songOf(patchOf(441, 0)), SX1 = songOf(patchOf(330, 1)), SX2 = songOf(patchOf(330, 2)), SOff = songOf(patchOf(330, 0), false);
  auto mk = [&](Rig& R, const std::string& text) { R.H.song(text); R.play(); R.run(kEdit); };
  Rig X(copts()), A(copts()), B(copts()), C(copts()), K(copts()), W(copts());
  mk(X, S0); mk(A, S0); mk(B, S0); mk(C, S0); mk(K, S0); mk(W, S0);
  CHECK(peak(X.l) > 0.1, "the oscillator through the chain sounds (peak %g)", peak(X.l));
  CHECK(X.st().chains_nodes == 1 && X.st().chains_plans == 1 && X.st().chains_blocks > 0, "one node, one plan (the play's)");
  // (i) three ways to the same parameter
  CHECK(edit(A, kEditOsc) == 0, "chain_edit");
  CHECK(B.H.song(SF, 2) == 0, "a publish that changes the same parameter");
  CHECK(C.ch()->node(1) != nullptr, "node"); C.ch()->param("osc1", "freq", 441);   // a direct setParam, between two quanta (no audio thread runs)
  const chains::ChainsNode* nodeB = B.ch()->node(1); const waves::PatchImpl* patchB = nodeB ? nodeB->engine().currentPatch() : nullptr;
  for (Rig* R : {&X, &A, &B, &C}) R->run(Q * 40);
  CHECK(ndiff(A.l, C.l) == 0 && ndiff(A.r, C.r) == 0, "(i) chain_edit equals a direct setParam at that quantum: %zu samples differ", ndiff(A.l, C.l));
  CHECK(ndiff(B.l, A.l) == 0 && ndiff(B.r, A.r) == 0, "(i) a publish of the same value gives the same samples (param records, no swap): %zu differ", ndiff(B.l, A.l));
  CHECK(ndiff(X.l, A.l, (size_t)kEdit) > 1000, "(i) the edit is audible");
  CHECK(B.ch()->node(1) == nodeB && nodeB && nodeB->engine().currentPatch() == patchB && B.st().chains_plans == 1, "(i) the publish made no plan and swapped nothing (plans %u)", B.st().chains_plans);
  // (ii) a structural publish swaps the patch: state reset, the retired patch freed by collect()
  CHECK(W.H.song(SX1, 2) == 0, "structural publish"); const waves::PatchImpl* p0 = W.ch()->node(1)->engine().currentPatch(); W.run(Q * 40);
  CHECK(W.ch()->node(1)->engine().currentPatch() != p0 && W.ch()->node(1)->engine().patchModules() == 5, "(ii) the node runs the new patch (5 modules)");
  CHECK(W.st().chains_plans == 2, "(ii) a second plan");
  CHECK(ndiff(X.l, W.l, (size_t)kEdit) > 1000, "(ii) the whole-patch swap RESET the oscillator's phase (the click the ruling accepts)");
  W.H->collect(); CHECK(W.H->collect() == 0, "(ii) everything came back with the first collect");
  // (iii) a CHN switch on another track: the bus plan changes, the patch does not: the node, its patch and its state stay
  const chains::ChainsNode* nodeK = K.ch()->node(1); const waves::PatchImpl* patchK = nodeK->engine().currentPatch(); CHECK(nodeK->engine().busMap() == std::vector<int>({0}), "bus map [0] before");
  CHECK(K.H.song(SOff, 2) == 0, "CHN switch publish"); K.run(Q * 40);
  CHECK(K.ch()->node(1) == nodeK && nodeK->engine().currentPatch() == patchK, "(iii) keep-patch: the node and its patch are the same objects (no retired patch)");
  CHECK(nodeK->engine().busMap().empty() && K.st().chains_plans == 2, "(iii) the node's bus map follows the bus plan (empty now), one more plan");
  CHECK(ndiff(X.l, K.l) == 0 && ndiff(X.r, K.r) == 0, "(iii) its output equals a run without the switch (track 0 is silent): the state was kept (%zu samples differ)", ndiff(X.l, K.l));
  CHECK(K.st().chains_feed_mismatch == 0 && X.st().chains_feed_mismatch == 0, "no feed mismatch");

  // R7 (M1 / M6 of the mutation table): (a) a play that is replaced or cancelled before it starts still COMMITS its plan: play(S1: a plan) + play + stop + play queued together, the node ends with S1's patch;
  // (b) the param records of a chain_edit made right after a structural publish meet the NEW patch (the commit is applied first, then the records): the same samples as a publish that carries the value
  {
    Rig R(copts()); R.H.song(S0); R.play(); R.run(kEdit); R.H->stop(THUNDER_LIVE_NOW); R.run(Q * 4);
    CHECK(R.H.song(SX1, 2) == 0, "song S1 while idle"); thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW);
    CHECK(R.H->play(po) == 0 && R.H->play(po) == 0 && R.H->stop(THUNDER_LIVE_NOW) == 0 && R.H->play(po) == 0, "play (with a plan), play, stop, play queued"); R.run(Q * 40); R.H->collect();
    CHECK(R.ch()->node(1) && R.ch()->node(1)->engine().patchModules() == 5, "(R7) the plan of a replaced / cancelled play was committed: the node runs S1's patch (%d modules)", R.ch()->node(1) ? R.ch()->node(1)->engine().patchModules() : -1);
    CHECK(R.H.song(SX1, 3) == 0 && R.st().chains_plans == 2, "the same song again makes no plan (the control view and the audio side agree; plans %u)", R.st().chains_plans);
  }
  {
    Rig P(copts()), E(copts()); P.H.song(S0); P.play(); P.run(kEdit); E.H.song(S0); E.play(); E.run(kEdit);
    const std::string SXF = songOf(patchOf(441, 1));
    CHECK(P.H.song(SXF, 2) == 0, "structural publish carrying freq 441");
    CHECK(E.H.song(SX1, 2) == 0 && edit(E, kEditOsc) == 0, "structural publish, then chain_edit freq 441 before the next quantum");
    P.run(Q * 40); E.run(Q * 40);
    CHECK(ndiff(P.l, E.l) == 0 && ndiff(P.r, E.r) == 0, "(R7) the records are applied AFTER the commit: %zu samples differ", ndiff(P.l, E.l));
  }
  // the mixer's bus plan is the commit's alone (R7): a refused plan with another bus plan must leave it
  {
    Rig R(copts()); R.H.song(S0); R.play(); R.run(kEdit); CHECK(R.H->mixerForTest().plan().nFeeds[1] == 1, "bus 1 has one feed");
    CHECK(R.H.song(songOf(patchOf(330, 0, true), false), 2) == 0, "a refused patch with another bus plan: the call succeeds"); R.run(Q * 4);
    CHECK(R.H->mixerForTest().plan().nFeeds[1] == 1 && R.ch()->node(1) && R.ch()->node(1)->engine().busMap() == std::vector<int>({0}), "R7: the bus plan message was adopted and NOT applied: the mixer keeps the plan the nodes' maps are for");
    CHECK(R.st().chains_refused == 1 && R.st().chains_plans == 1 && R.st().chains_feed_mismatch == 0, "(vi) counted: refused 1, plans 1");
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void testBusy() {
  std::printf("one plan in flight\n");
  const std::string S0 = songOf(patchOf(330, 0)), S1 = songOf(patchOf(330, 1)), S2 = songOf(patchOf(330, 2));
  Rig R(copts(THUNDER_LIVE_F_MIDI)); R.H.song(S0); R.play(); R.run(kEdit);
  const thunder_live_stats s0 = R.st();
  CHECK(R.H.song(S1, 2) == 0, "(iv) the first structural publish");
  const thunder_live_stats s1 = R.st();
  CHECK(R.H.song(S2, 3) == THUNDER_LIVE_E_BUSY, "(iv) the second one, before the audio side has seen the first: E_BUSY");
  const thunder_live_stats s2 = R.st();
  CHECK(s2.cmd_busy == s1.cmd_busy + 1 && s2.chains_busy == s1.chains_busy + 1 && s2.chains_plans == s1.chains_plans && s2.song_pending == s1.song_pending, "(iv) counted, and nothing else moved (busy %u/%u, plans %u, pending %u/%u)", s2.cmd_busy, s2.chains_busy, s2.chains_plans, s2.song_pending, s1.song_pending);
  R.run(Q * 4);
  CHECK(R.ch()->node(1)->engine().patchModules() == 5, "the first publish landed (5 modules), not the second's (6)");
  // the second publish's call changed nothing, chainsSong_ included: publishing the FIRST song again makes no plan
  CHECK(R.H.song(S1, 4) == 0 && R.st().chains_plans == 2, "(iv) chainsSong_ did not move on the failed call: the same song again makes no plan (plans %u)", R.st().chains_plans);
  R.run(Q * 4);
  CHECK(R.H.song(S2, 5) == 0, "after the audio side has run, the second publish is accepted (the first plan came back and was settled inside the call)"); R.run(Q * 4);
  CHECK(R.ch()->node(1)->engine().patchModules() == 6 && R.st().chains_plans == 3, "the second landed (6 modules), 3 plans");
  R.H->collect();
  // a second publish before the first was APPLIED, many times: always E_BUSY, never a leak or a stuck host
  CHECK(R.H.song(S0, 6) == 0, "back to S0"); for (int i = 0; i < 20; i++) CHECK(R.H.song(i % 2 ? S1 : S2, 7 + i) == THUNDER_LIVE_E_BUSY, "E_BUSY again"); R.run(Q * 2); CHECK(R.H.song(S1, 40) == 0, "and accepted after a quantum");
  CHECK(R.st().chains_busy == s2.chains_busy + 20, "20 more E_BUSY counted (%u)", R.st().chains_busy);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void testRefused() {
  std::printf("refused and over-budget plans\n");
  const std::string S0 = songOf(patchOf(330, 0)), SBad = songOf(patchOf(330, 0, true)), SFree = songOf(patchOf(330, 1)), SOff = songOf(patchOf(330, 0), false);
  {   // a refused patch while playing: the call succeeds, the chains play on, the next publish of the old song makes no plan (chainsSong_ did not move)
    Rig X(copts()), R(copts()); X.H.song(S0); X.play(); X.run(kEdit); R.H.song(S0); R.play(); R.run(kEdit);
    CHECK(R.H.song(SBad, 2) == 0, "(vi) a refused patch: E_OK"); X.run(Q * 40); R.run(Q * 40);
    CHECK(R.st().chains_refused == 1 && R.st().chains_over_budget == 0 && R.st().chains_plans == 1, "(vi) refused 1, plans 1");
    CHECK(ndiff(X.l, R.l) == 0, "(vi) the sound goes on as if nothing was published (%zu differ)", ndiff(X.l, R.l));
    CHECK(R.H.song(S0, 3) == 0 && R.st().chains_plans == 1, "(vi) the old song again: no plan (chainsSong_ did not move on the refused one)");
    CHECK(R.H.song(SFree, 4) == 0 && R.st().chains_plans == 2, "(vi) the next good structural publish is planned"); R.run(Q * 4); CHECK(R.ch()->node(1)->engine().patchModules() == 5, "and lands");
  }
  {   // a refused patch at the first play: dry (no node), PLAYING, then a good publish brings the chain in
    Rig R(copts()); R.H.song(SBad); CHECK(R.play() == 0, "(vi) play with a refused patch succeeds"); R.run(Q * 40);
    CHECK(R.st().state == THUNDER_LIVE_STATE_PLAYING && R.ch()->node(1) == nullptr && R.st().chains_nodes == 0 && R.st().chains_refused == 1 && R.st().chains_plans == 0, "(vi) playing dry: no node, refused 1");
    CHECK(peak(R.l) == 0, "dry: the oscillator is not there");
    CHECK(R.H.song(S0, 2) == 0 && R.st().chains_plans == 1, "a good publish makes the first plan"); R.run(Q * 40); CHECK(R.ch()->node(1) != nullptr && peak(R.l, (size_t)(Q * 40)) > 0.1, "and the chain sounds");
  }
  {   // the memory budget: one node fits, a swap (two patches at once) does not; a keep-patch plan (one patch) does
    ChainsHost probe(SR); JsonDoc d; d.parse(patchOf(330, 0)); bool on[kSongNT] = {}; on[0] = on[1] = true; ChainsSongTransport tp;
    ChainsPlan* pl = probe.plan(d.root(), on, 0xFFFFFFFFu, tp); const size_t pb = pl ? pl->patchBytes() : 0; if (pl) { probe.settle(pl); delete pl; }
    const uint64_t budget = kChainsNodeOverhead + pb + pb / 2; CHECK(pb > 1000, "the patch has a cost (%zu)", pb);
    Rig X(copts(0, budget)), R(copts(0, budget)); X.H.song(S0); X.play(); X.run(kEdit); R.H.song(S0); R.play(); R.run(kEdit);
    CHECK(R.st().chains_plans == 1 && R.st().chains_over_budget == 0 && R.ch()->node(1), "one node fits");
    CHECK(R.H.song(SFree, 2) == 0, "(vi) a swap over the budget: the call succeeds"); X.run(Q * 40); R.run(Q * 40);
    CHECK(R.st().chains_over_budget == 1 && R.st().chains_refused == 1 && R.st().chains_plans == 1, "(vi) over_budget 1, refused 1, plans 1 (%u %u %u)", R.st().chains_over_budget, R.st().chains_refused, R.st().chains_plans);
    CHECK(ndiff(X.l, R.l) == 0, "(vi) the chain plays on, state kept (%zu differ)", ndiff(X.l, R.l));
    CHECK(R.H.song(SOff, 3) == 0 && R.st().chains_plans == 2, "a keep-patch plan needs one patch: it fits"); R.run(Q * 4); CHECK(R.ch()->node(1)->engine().busMap().empty(), "and lands");
    Rig T(copts(0, kChainsNodeOverhead / 2)); T.H.song(S0); CHECK(T.play() == 0, "budget below one node: play succeeds"); T.run(Q * 8);
    CHECK(T.st().chains_over_budget == 1 && T.ch()->node(1) == nullptr && T.st().chains_nodes == 0, "dry, over_budget 1");
    CHECK(T.H->mixerForTest().plan().nFeeds[1] == 0, "R7 (tryStartPlay): a play whose plan was refused does not apply the song's bus plan either: the mixer's plan is the commit's alone (nFeeds[1] %d)", T.H->mixerForTest().plan().nFeeds[1]);
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void testChainEditErrors() {
  std::printf("chain_edit\n");
  const std::string S0 = songOf(patchOf(330, 0));
  { Host H(opts(0, 1024)); CHECK(H->chainEdit((const uint8_t*)"[]", 2, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_STATE, "no F_CHAINS: E_STATE"); }
  Rig R(copts()); CHECK(edit(R, "[]") == THUNDER_LIVE_E_STATE, "no song yet: E_STATE"); R.H.song(S0); CHECK(edit(R, "[]") == THUNDER_LIVE_E_STATE, "a song published but never planned (no play yet): E_STATE"); R.play(); R.run(kEdit); CHECK(edit(R, "[]") == 0, "an empty array is fine once a song with chains is planned");
  Rig X(copts()); X.H.song(S0); X.play(); X.run(kEdit);
  auto bad = [&](const char* j, int want, const char* what) { const int rc = edit(R, j); CHECK(rc == want, "%s: %d (want %d)", what, rc, want); };
  CHECK(R.H->chainEdit((const uint8_t*)"[]", 2, 5) == THUNDER_LIVE_E_ARG, "`at` other than NOW is E_ARG");
  bad("{\"op\":\"param\"}", THUNDER_LIVE_E_ARG, "not an array"); bad("not json", THUNDER_LIVE_E_ARG, "malformed"); bad("[1]", THUNDER_LIVE_E_ARG, "an element that is not an object");
  bad("[{\"op\":\"text\",\"id\":\"osc1\",\"value\":\"x\"}]", THUNDER_LIVE_E_ARG, "an op that is not param");
  bad("[{\"op\":\"param\",\"id\":\"nope\",\"name\":\"freq\",\"value\":1}]", THUNDER_LIVE_E_ARG, "an unknown module");
  bad("[{\"op\":\"param\",\"id\":\"i1\",\"name\":\"track\",\"value\":2}]", THUNDER_LIVE_E_ARG, "trackIn");
  bad("[{\"op\":\"param\",\"id\":\"o2\",\"name\":\"track\",\"value\":3}]", THUNDER_LIVE_E_ARG, "chainOut");
  bad("[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":\"x\"}]", THUNDER_LIVE_E_ARG, "a string value");
  bad("[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":1e999}]", THUNDER_LIVE_E_ARG, "an infinite value");
  bad("[{\"op\":\"param\",\"id\":\"osc1\",\"value\":1}]", THUNDER_LIVE_E_ARG, "no name");
  bad("[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"abcdefghijklmnopqrstuvwxyzabcdefghijkl\",\"value\":1}]", THUNDER_LIVE_E_LIMIT, "a name that does not fit a record");
  { std::string big = "["; while (big.size() < (1u << 20) + 8) big += "{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":1},"; big.back() = ']'; CHECK(R.H->chainEdit((const uint8_t*)big.data(), big.size(), THUNDER_LIVE_NOW) == THUNDER_LIVE_E_LIMIT, "n above 1 MiB: E_LIMIT"); }
  CHECK(R.H->chainEdit(nullptr, 3, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG, "NULL with n > 0: E_ARG");
  // all or nothing: a good op followed by a bad one changes nothing
  { const char* mixed = "[{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":500},{\"op\":\"param\",\"id\":\"nope\",\"name\":\"freq\",\"value\":1}]"; CHECK(edit(R, mixed) == THUNDER_LIVE_E_ARG, "mixed: E_ARG"); }
  R.run(Q * 40); X.run(Q * 40); CHECK(ndiff(R.l, X.l) == 0, "all or nothing: no refused edit moved anything (%zu differ)", ndiff(R.l, X.l));
  // the ring: 200 records fit, the next 200 do not (E_BUSY, nothing changed), after the audio side has run they do
  { std::string ops = "["; for (int i = 0; i < 200; i++) ops += std::string(i ? "," : "") + "{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":" + std::to_string(330 + i % 3) + "}"; ops += "]";
    CHECK(edit(R, ops.c_str()) == 0, "200 records"); const uint32_t busy0 = R.st().cmd_busy; CHECK(edit(R, ops.c_str()) == THUNDER_LIVE_E_BUSY && R.st().cmd_busy == busy0 + 1, "the next 200: E_BUSY, counted"); R.run(Q * 2); CHECK(edit(R, ops.c_str()) == 0, "after a quantum: accepted");
    std::string many = "["; for (int i = 0; i < 300; i++) many += std::string(i ? "," : "") + "{\"op\":\"param\",\"id\":\"osc1\",\"name\":\"freq\",\"value\":330}"; many += "]";
    CHECK(edit(R, many.c_str()) == THUNDER_LIVE_E_LIMIT, "more records than the ring can ever hold: E_LIMIT"); }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// events from the sequencer reach the nodes: BYP (command 38, value 0x11: switch 1 bypassed) and TPO (19, 90) on the chain's track
static void testSeqEvents() {
  std::printf("events through the sequencer\n");
  const char* row = "[60,-1,-1,-1,-1,0,38,17,19,90,-1,-1]";
  const std::string S = songOf(patchOf(330, 0), true, 120, row);
  int ign[2] = {0, 0};
  for (int on = 0; on < 2; on++) {
    Rig R(opts(on ? FC : 0, 1024)); R.H.song(S); R.play(); R.run(Q * 400);
    const thunder_live_stats s = R.st(); ign[on] = s.events_ignored;
    if (on) {
      chains::ChainsNode* n = R.ch()->node(1);
      CHECK(n != nullptr && s.chains_events >= 2 && s.chains_chc_no_node == 0 && s.chains_events_dropped == 0 && s.chains_events_bad == 0, "the chc and the tempo event were applied (%llu)", (unsigned long long)s.chains_events);
      CHECK(n && n->engine().transport().tempo == 90, "the node's tempo is the TPO's (%g)", n ? n->engine().transport().tempo : 0);
      CHECK(n && n->engine().bypassSwitchState(1) == 1, "BYP switch 1 is set (%g)", n ? n->engine().bypassSwitchState(1) : -9);
    }
  }
  CHECK(ign[1] == ign[0] - 1, "the chc is no longer counted as ignored; the tempo still is (the Players see it too): %d vs %d", ign[1], ign[0]);
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// ELECTRO: LiveHost == SongRenderer, with chains + effects + master; without F_CHAINS: today's output
struct Ref { std::vector<float> l, r; };
static Ref reference(const Song& song, const RenderPads& rp, int64_t N, bool chains) {
  Ref o; SeqVars vars; SeqOpts so; so.once = false; so.live = false; so.sim = false; so.randomSeed = 0x2545F491u;
  SongRenderer R(song, vars, rp, SR, 512, so, 64);
  if (chains) { const int rc = R.setChains(true); CHECK(rc == 0, "setChains %d", rc); }
  R.setEffects(true); R.setMasterChain(master_chain_from_song(song.mst, song.limiter));
  o.l.resize((size_t)N); o.r.resize((size_t)N);
  for (int64_t q = 0; q * Q < N; q++) { const int n = (int)std::min<int64_t>(Q, N - q * Q); R.block(o.l.data() + q * Q, o.r.data() + q * Q, n); }
  return o;
}
static void testElectro() {
  std::printf("ELECTRO\n");
  const std::string text = electro(); PadSet ps; static RenderPads rp; fillRenderPads(rp, ps);
  Song song; JsonError je; if (!song.load(text.data(), text.size(), &je, true)) { CHECK(false, "ELECTRO loads"); return; }
  const int64_t N = (int64_t)(SR * (kSan ? 2 : 8));
  const Ref withC = reference(song, rp, N, true), without = reference(song, rp, N, false);
  CHECK(peak(withC.l) > 0.01 && ndiff(withC.l, without.l) > 1000, "the chain is audible in the reference");
  struct Cs { int cs; int64_t f0; };
  std::vector<Cs> cases = {{1, 0}, {128, 0}, {333, 0}, {480, 0}, {4096, 0}, {1, 37}, {128, 37}, {333, 37}, {480, 37}, {4096, 37}, {1, (int64_t)1 << 31}, {128, (int64_t)1 << 31}, {333, (int64_t)1 << 31}, {480, (int64_t)1 << 31}, {4096, (int64_t)1 << 31}};
  if (kSan) cases = {{128, 0}, {333, 37}, {4096, (int64_t)1 << 31}};
  const uint32_t FX = THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER;
  for (const Cs& c : cases) {
    for (int chains = 0; chains < 2; chains++) {
      if (!chains && !((c.cs == 128 && c.f0 == 0) || (c.cs == 480 && c.f0 == 37))) continue;   // the flag off: two cases are enough, it is today's code
      Host H(chains ? copts(FX) : opts(FX, 4096)); if (!H.h) return; H.song(text); H.loadPads(ps);
      thunder_live_play_opts po = playOpts(c.f0); CHECK(H->play(po) == 0, "play");
      std::vector<float> l, r; int64_t fr = c.f0; CHECK(H.run(l, r, N, c.cs, fr) == 0, "process");
      const Ref& ref = chains ? withC : without; const size_t bad = ndiff(l, ref.l) + ndiff(r, ref.r);
      CHECK(bad == 0, "F_CHAINS %d, call size %d, frame0 %lld: %zu samples differ from SongRenderer", chains, c.cs, (long long)c.f0, bad);
      const thunder_live_stats s = stats(H.h);
      if (chains) CHECK(s.chains_nodes == 1 && s.chains_blocks >= (uint64_t)(N / Q) && s.chains_failed_blocks == 0 && s.chains_feed_mismatch == 0 && s.chains_plans == 1 && s.chains_events_dropped == 0 && s.chains_events_bad == 0, "ELECTRO counters (nodes %u blocks %llu)", s.chains_nodes, (unsigned long long)s.chains_blocks);
      else CHECK(s.chains_nodes == 0 && s.chains_blocks == 0 && s.chains_plans == 0, "without the flag: no chains counters");
      std::printf("  F_CHAINS %d, call size %4d, frame0 %10lld: %s\n", chains, c.cs, (long long)c.f0, bad ? "DIFFERENT" : "bit-equal");
    }
  }
}

// ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// the C API: the same, and the ABI rules of what C2 appended
static void testApi() {
  std::printf("C API\n");
  CHECK((thunder_live_caps() & THUNDER_LIVE_CAP_CHAINS) != 0, "caps: CAP_CHAINS");
  const std::string S0 = songOf(patchOf(330, 0)); char err[256] = {0};
  thunder_live_opts o = copts(); thunder_live* L = thunder_live_create(&o, err, sizeof err); CHECK(L != nullptr, "create with F_CHAINS: %s", err); if (!L) return;
  CHECK(thunder_live_song_publish(L, (const uint8_t*)S0.data(), S0.size(), nullptr, 0, 1, err, sizeof err) == 0, "song");
  thunder_live_play_opts po = playOpts(THUNDER_LIVE_NOW); CHECK(thunder_live_play(L, &po) == 0, "play");
  std::vector<float> a(1024), b(1024); int64_t fr = 0; for (int i = 0; i < 100; i++) { thunder_live_process(L, a.data(), b.data(), 1024, fr, 0); fr += 1024; }
  CHECK(thunder_live_chain_edit(L, (const uint8_t*)kEditOsc, std::strlen(kEditOsc), THUNDER_LIVE_NOW) == 0, "chain_edit through the API");
  CHECK(thunder_live_chain_edit(L, (const uint8_t*)"[]", 2, 7) == THUNDER_LIVE_E_ARG && thunder_live_chain_edit(L, nullptr, 0, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG && thunder_live_chain_edit(nullptr, (const uint8_t*)"[]", 2, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_ARG, "API: argument errors");
  thunder_live_stats st; std::memset(&st, 0xAB, sizeof st); st.size = sizeof st; CHECK(thunder_live_stats_get(L, &st) == 0 && st.chains_nodes == 1 && st.chains_plans == 1 && st.chains_blocks > 0, "stats: the chains fields (nodes %u plans %u)", st.chains_nodes, st.chains_plans);
  { unsigned char raw[sizeof(thunder_live_stats)]; std::memset(raw, 0xAB, sizeof raw); thunder_live_stats* old = (thunder_live_stats*)raw; old->size = 240;   // an older caller: its struct ends at cmds_no_seq: nothing after is written
    CHECK(thunder_live_stats_get(L, old) == 0 && old->size == 240 && raw[240] == 0xAB && raw[sizeof raw - 1] == 0xAB, "the size rule: a 240-byte stats struct gets 240 bytes"); }
  thunder_live_destroy(L);
  { thunder_live_opts n; std::memset(&n, 0, sizeof n); n.size = 56; n.sample_rate = SR; n.flags = FC; thunder_live* O = thunder_live_create(&n, err, sizeof err);   // an older caller's opts (56 bytes, no chains_bytes): the default budget
    CHECK(O != nullptr, "an opts struct of 56 bytes works: chains_bytes = 0 = the default"); thunder_live_destroy(O); }
  { thunder_live_opts n = copts(); n.chains_bytes = 1; thunder_live* O = thunder_live_create(&n, err, sizeof err); CHECK(O != nullptr, "chains_bytes 1 is a budget, not an error"); thunder_live_destroy(O); }
  { thunder_live_opts n = opts(0, 128); thunder_live* O = thunder_live_create(&n, err, sizeof err); CHECK(O && thunder_live_chain_edit(O, (const uint8_t*)"[]", 2, THUNDER_LIVE_NOW) == THUNDER_LIVE_E_STATE, "without F_CHAINS: chain_edit is E_STATE"); thunder_live_destroy(O); }
}

int main() {
  testTransport();
  testEdits();
  testBusy();
  testRefused();
  testChainEditErrors();
  testSeqEvents();
  testApi();
  testElectro();
  std::printf("live_chains: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
