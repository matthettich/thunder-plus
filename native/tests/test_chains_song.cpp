// C1: ctest `chains_song`. Every case of golden/chains_song_cases.json (made by tools/chains-song-dump.mjs from the page's own chains worklet and mixer model; docs/port/C1G-NOTES.md) is rebuilt here and must give
// the golden's hash EXACTLY, quantum by quantum: through Mixer + ChainsHost (all cases), and through SongRenderer::setChains / setStem (the events go in through chainsHost()->event, as dispatch does). Before any audio the plan (heard tracks, nodes, bus maps, PRE sources, routed sources, event quanta) is compared with the case's `expect` block.
// C2b: a third level: the same cases through LiveHost (F_CHAINS; the input tap and the events go in through the test seams), at call sizes 1 / 128 / 333 / 480 / 4096 and frame0 0 / 37 / 2^31 (a sanitizer build: fewer), must give the golden's hash
// too; and with F_EFFECTS | F_MASTER the host equals SongRenderer with setChains + setEffects + setMasterChain. A stem case has no live equivalent (stems are offline) and is skipped there.
// usage: test_chains_song <chains_song_cases.json> [case-name-substring]
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "chains/chost.h"
#include "chains_song_common.h"
#include "live/live_host.h"
#include "master/chain.h"
#include "render/render.h"
#include "rt/json.h"
#include "song/song.h"

using namespace thunder;
static int g_fail = 0;
#define CHECK(msg, cond) do { if (!(cond)) { std::printf("FAIL %s (line %d)\n", msg, __LINE__); g_fail++; } } while (0)
static const double kSr = 48000;
#ifdef THUNDER_SANITIZE_BUILD
static const bool kSanitize = true;
#else
static const bool kSanitize = false;
#endif

struct Case {
  std::string name; Json song, patch, expect; int stem = -1, quanta = 400; std::vector<std::pair<int, bool>> inputs; uint32_t noteTracks = 0; std::vector<SeqEvent> events; std::string hash, qh; Json ctl;
  bool chainOn[kSongNT] = {}; struct M { double vol, trim, pan; bool mute, solo; } mix[kSongNT];
  int bpm = 120, lpb = 4, ticks = 6, master = 255, mout = 255;
};
static std::vector<int> ints(Json a) { std::vector<int> v; for (size_t i = 0; i < a.size(); i++) v.push_back((int)a.at(i).asNum()); return v; }
static std::string show(const std::vector<int>& v) { std::string s = "["; for (size_t i = 0; i < v.size(); i++) s += (i ? "," : "") + std::to_string(v[i]); return s + "]"; }
static Case readCase(Json c) {
  Case k; k.name = std::string(c.str("name")); k.song = c.get("song"); k.patch = k.song.get("chainPatch"); k.expect = c.get("expect"); k.quanta = (int)c.num("quanta", 400);
  k.stem = c.get("stem").isNum() ? (int)c.get("stem").asNum() : -1; k.hash = std::string(c.str("hash")); k.qh = std::string(c.str("qh")); k.ctl = c.get("controls");
  { Json nt = c.get("noteTracks"); for (size_t i = 0; i < nt.size(); i++) k.noteTracks |= 1u << (int)nt.at(i).asNum(); }
  Json in = c.get("inputs"); for (size_t i = 0; i < in.size(); i++) k.inputs.push_back({(int)in.at(i).num("t"), in.at(i).boolean("mono")});
  Json ev = c.get("events");
  for (size_t i = 0; i < ev.size(); i++) {
    Json e = ev.at(i); SeqEvent s; s.t = e.num("t");
    if (e.str("type") == "chc") { s.type = SEV_CHC; s.track = (int)e.num("track"); s.p = std::atoi(std::string(e.str("p")).c_str() + 1); s.v = e.num("v"); }   // "b1" -> 1
    else { s.type = SEV_TEMPO; s.v = e.num("bpm"); s.tks = (int)e.num("tks"); }
    k.events.push_back(s);
  }
  k.bpm = (int)k.song.num("bpm", 120); k.lpb = (int)k.song.num("lpb", 4); k.ticks = (int)k.song.num("ticks", 6); k.master = (int)k.song.num("master", 255); k.mout = (int)k.song.num("mout", 255);
  Json mx = k.song.get("mix"), co = k.song.get("chainOn");
  for (int t = 0; t < kSongNT; t++) { Json m = mx.at((size_t)t); k.mix[t] = {m.num("vol", 224), m.num("trim", 128), m.num("pan", 128), m.boolean("mute"), m.boolean("solo")}; k.chainOn[t] = co.at((size_t)t).asBool(); }
  return k;
}

// the inputs of a case: one stereo pair per listed track, `quanta` * 128 samples
struct Inputs { std::vector<std::vector<float>> L, R; };
static Inputs makeInputs(const Case& c) { Inputs in; for (auto& q : c.inputs) { in.L.emplace_back(); in.R.emplace_back(); c1::makeInput(q.first, c.quanta * 128, in.L.back(), in.R.back()); } return in; }

// ---- the plan check against `expect` (before any audio) ----
static bool checkPlan(const Case& c, uint32_t heard, uint32_t nodes, const BusPlan& bp, uint32_t routed, ChainsHost& host, const ChainsPlan* plan) {
  bool ok = true;
  std::vector<int> eh = ints(c.expect.get("heard")), en = ints(c.expect.get("nodes")), er = ints(c.expect.get("routed")), ep = ints(c.expect.get("pre")), eq = ints(c.expect.get("eventQuanta"));
  std::vector<int> mh, mn, mr, mp, mq;
  for (int t = 0; t < kSongNT; t++) { if (heard >> t & 1u) mh.push_back(t);   // heard: keepMask() (or a stem's own set)
     if (nodes >> t & 1u) mn.push_back(t); if (routed >> t & 1u) mr.push_back(t); }
  std::sort(en.begin(), en.end());
  auto cmp = [&](const char* what, const std::vector<int>& a, const std::vector<int>& b) { if (a != b) { std::printf("  %s: plan %s, expect %s\n", what, show(a).c_str(), show(b).c_str()); ok = false; } };
  cmp("heard", mh, eh); cmp("nodes", mn, en); cmp("routed", mr, er);
  for (int t : mn) { std::vector<int> bm = plan ? plan->busMap(t) : std::vector<int>(); Json e = c.expect.get("busMaps").get(std::to_string(t)); if (!bm.empty() || e.size()) cmp(("busMap " + std::to_string(t)).c_str(), bm, ints(e));
    for (int s : bm) if ((bp.pre >> s & 1u) && std::find(mp.begin(), mp.end(), s) == mp.end()) mp.push_back(s); }
  std::sort(mp.begin(), mp.end()); cmp("pre", mp, ep);
  for (const SeqEvent& e : c.events) {
    int64_t k = (e.type == SEV_CHC ? host.chcDue(e.t) : host.tempoDue(e.t)) / 128;
    if (e.type == SEV_TEMPO) { const double q = 128.0 / kSr, at = std::ceil(e.t / q) * q; if (!(at > 0 && at < c.quanta * 128.0 / kSr)) k = 0; }   // the page's "sent at once" rule (index.html:23229-23231)
    mq.push_back((int)k);
  }
  cmp("eventQuanta", mq, eq);
  if (!ok) std::printf("  (case %s: the plan differs from expect)\n", c.name.c_str());
  return ok;
}

// the tracks that have notes: the golden's own `noteTracks` (the page's notes.some(e => e.track === t)). The renderer's song plays a note on each of them that is not a bus (the sequencer plays no note on a bus)
static uint32_t noteTracksOf(const Case& c) { return c.noteTracks; }

struct Out { std::string hash, qh; };
static void hashQuantum(uint32_t& h, std::string& qh, const float* L, const float* R) {
  uint32_t hq = 0x811c9dc5u; hq = c1::fnv(hq, L, 128); hq = c1::fnv(hq, R, 128); char b[9]; c1::hex8(hq, b); qh += b;
  h = c1::fnv(h, L, 128); h = c1::fnv(h, R, 128);
}
static void report(const Case& c, const Out& o, const char* path) {
  char b[9];
  if (o.hash == c.hash) return;
  g_fail++; std::printf("FAIL %s [%s]: hash %s, golden %s\n", c.name.c_str(), path, o.hash.c_str(), c.hash.c_str());
  for (size_t q = 0; q * 8 + 8 <= c.qh.size() && q * 8 + 8 <= o.qh.size(); q++) if (c.qh.compare(q * 8, 8, o.qh, q * 8, 8) != 0) { std::printf("  first differing quantum %zu: %s, golden %s (frame %zu)\n", q, o.qh.substr(q * 8, 8).c_str(), c.qh.substr(q * 8, 8).c_str(), q * 128); break; }
  (void)b;
}

// ---- level 1: Mixer + ChainsHost, driven as SongRenderer drives them ----
static bool runMixer(const Case& c, Out* out, bool heardAll = false) {   // heardAll (C2b R2): every track is heard, as a live host has it; the golden's plan checks are for the offline rule and are skipped
  Mixer mix(kSr, 128);
  const BusPlan bp = bus_plan(c.patch, c.chainOn);
  mix.setPlan(bp);
  for (int t = 0; t < kSongNT; t++) mix.setTrack(t, c.mix[t].vol, c.mix[t].trim, c.mix[t].pan, c.mix[t].mute, c.mix[t].solo);
  mix.setMaster(c.master, c.mout); mix.apply();
  for (auto& q : c.inputs) if (q.second) mix.setMono(q.first, true);
  if (c.stem >= 0) {   // SongRenderer::setStem's rule: the track, and a bus's audible sources through any depth
    const uint32_t aud = (uint32_t)mix.audibleMask(); uint32_t m = 1u << c.stem;
    for (bool more = true; more;) { more = false; for (int b = 0; b < kSongNT; b++) if ((m >> b & 1u) && (bp.feedMask[b] & aud & ~m)) { m |= bp.feedMask[b] & aud; more = true; } }
    mix.setHeard(m);
  }
  const uint32_t heard = heardAll ? 0xFFFFFFFFu : c.stem >= 0 ? (uint32_t)mix.audibleMask() : ChainsHost::keepMask(bp, (uint32_t)mix.audibleMask(), noteTracksOf(c));
  ChainsHost host(kSr); ChainsSongTransport tp; tp.tempo = c.bpm; tp.lpb = c.lpb; tp.ticks = c.ticks;
  std::unique_ptr<ChainsPlan> plan(host.plan(c.patch, c.chainOn, heard, tp));
  if (!plan) { std::printf("FAIL %s: plan() returned null\n", c.name.c_str()); g_fail++; return false; }
  host.commit(plan.get(), mix); host.settle(plan.get());
  if (c.name[0] == 'a' || c.name[0] == 'b') std::printf("  memory (D7): %d node(s), patch %.2f MB + node %.2f MB per track, %.2f MB in all (budget %.0f MB)\n", plan->nodes(), plan->patchBytes() / 1048576.0, kChainsNodeOverhead / 1048576.0, plan->bytes() / 1048576.0, host.budget() / 1048576.0);
  const bool pok = heardAll || checkPlan(c, heard, plan->nodeMask(), mix.plan(), mix.routedMask(), host, plan.get());
  if (!pok) { g_fail++; std::printf("FAIL %s: plan differs from expect\n", c.name.c_str()); return false; }
  for (const SeqEvent& e : c.events) host.event(e);
  const Inputs in = makeInputs(c);
  std::vector<float> oL(128), oR(128); uint32_t h = 0x811c9dc5u; std::string qh;
  for (int b = 0; b < c.quanta; b++) {
    const double t0 = (double)b * 128 / kSr; mix.setTime(t0); host.beginBlock((int64_t)b * 128);
    for (size_t k = 0; k < c.inputs.size(); k++) { const int t = c.inputs[k].first; std::memcpy(mix.inputsL()[t], in.L[k].data() + (size_t)b * 128, 128 * sizeof(float)); if (!c.inputs[k].second) std::memcpy(mix.inputsR()[t], in.R[k].data() + (size_t)b * 128, 128 * sizeof(float)); }
    mix.render(oL.data(), oR.data(), 128, t0);
    hashQuantum(h, qh, oL.data(), oR.data());
  }
  char b[9]; c1::hex8(h, b); out->hash = b; out->qh = qh;
  const ChainsHostStats st = host.stats();
  if (st.failedBlocks || st.droppedEvents || st.badEvents || st.feedMismatch) { std::printf("FAIL %s: host counters failed %llu dropped %llu bad %llu mismatch %llu\n", c.name.c_str(), (unsigned long long)st.failedBlocks, (unsigned long long)st.droppedEvents, (unsigned long long)st.badEvents, (unsigned long long)st.feedMismatch); g_fail++; }
  return true;
}

// ---- level 2: the whole renderer (no events) ----
struct Tap { const Case* c; const Inputs* in; };
static void tapFn(void* user, const float* const* inL, const float* const* inR, int frames, int64_t frame0) {
  Tap* tp = (Tap*)user; const size_t q = (size_t)(frame0 / 128); (void)frames;
  for (size_t k = 0; k < tp->c->inputs.size(); k++) { const int t = tp->c->inputs[k].first; std::memcpy(const_cast<float*>(inL[t]), tp->in->L[k].data() + q * 128, 128 * sizeof(float)); if (!tp->c->inputs[k].second) std::memcpy(const_cast<float*>(inR[t]), tp->in->R[k].data() + q * 128, 128 * sizeof(float)); }
}
static std::string songText(const Case& c, const std::string& songRow = "[0]", const char* row0 = "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]", const char* row0b = nullptr) {   // row0b: a second chain (index 1) of one phrase with that row   // a silent song (no pad is handed in) that carries the case's mixer, CHN flags and patch
  std::string s = "{\"v\":6,\"bpm\":" + std::to_string(c.bpm) + ",\"lpb\":" + std::to_string(c.lpb) + ",\"ticks\":" + std::to_string(c.ticks) + ",\"master\":" + std::to_string(c.master) + ",\"mout\":" + std::to_string(c.mout) +
    ",\"ntr\":8,\"song\":[" + std::string(songRow) + "],\"chains\":[[[0,0]]" + std::string(row0b ? ",[[1,0]]" : "") + "],\"plen\":[8" + std::string(row0b ? ",8" : "") + "],\"phrases\":[[";
  for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + (i == 0 ? row0 : "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]");
  s += "]"; if (row0b) { s += ",["; for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + (i == 0 ? row0b : "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]"); s += "]"; }
  s += "],\"mix\":[";
  for (int t = 0; t < kSongNT; t++) { char b[160]; std::snprintf(b, sizeof b, "%s{\"vol\":%g,\"trim\":%g,\"pan\":%g,\"mute\":%s,\"solo\":%s}", t ? "," : "", c.mix[t].vol, c.mix[t].trim, c.mix[t].pan, c.mix[t].mute ? "true" : "false", c.mix[t].solo ? "true" : "false"); s += b; }
  s += "],\"chainOn\":["; for (int t = 0; t < kSongNT; t++) s += std::string(t ? "," : "") + (c.chainOn[t] ? "true" : "false");
  s += "],\"chainPatch\":" + json_write(c.patch) + "}";
  return s;
}
// chainsOn: 0 never, 1 on, 2 on and then off again (before the first block). The song plays a note on every non-bus input track (a pad is not handed in: silent), so noteTracksOf() in the renderer finds them.
static bool runRenderer(const Case& c, Out* out, int chainsOn = 1, bool inject = true, bool fxMaster = false) {
  const BusPlan bp = bus_plan(c.patch, c.chainOn); uint32_t nt = noteTracksOf(c); for (int t = 0; t < kSongNT; t++) if (bp.feedMask[t]) nt &= ~(1u << t);
  std::string row = "["; for (int t = 0; t < kSongNT; t++) row += std::string(t ? "," : "") + ((nt >> t & 1u) ? "0" : "-1"); row += "]";
  const std::string text = songText(c, row, "[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"); Song song; JsonError je;
  if (!song.load(text.data(), text.size(), &je)) { std::printf("FAIL %s: the test song did not load (%s)\n", c.name.c_str(), je.msg ? je.msg : "?"); g_fail++; return false; }
  SeqVars vars; RenderPads pads; SeqOpts so; so.row = song.renderFrom; so.once = true;
  SongRenderer r(song, vars, pads, kSr, 128, so);
  if (c.stem >= 0) r.setStem(c.stem);
  if (chainsOn) { const int rc = r.setChains(true); if (rc) { std::printf("FAIL %s: setChains returned %d\n", c.name.c_str(), rc); g_fail++; return false; } }
  if (fxMaster) { r.setEffects(true); r.setMasterChain(master_chain_from_song(song.mst, song.limiter)); }   // (C2b: the live host's F_EFFECTS | F_MASTER)
  if (chainsOn == 2) { r.setChains(false); CHECK("setChains(false): the mixer has no bus and no hook again", r.mixer().busMask() == 0 && r.chainsHost() == nullptr); }
  if (chainsOn == 1) { CHECK("setStem is refused once chains are on", !r.setStem(0)); }
  if (chainsOn == 1 && inject) for (const SeqEvent& e : c.events) r.chainsHost()->event(e);   // what dispatch does with the sequencer's events
  for (auto& q : c.inputs) if (q.second) r.mixer().setMono(q.first, true);
  const Inputs in = makeInputs(c); Tap tap{&c, &in}; r.setInputTap(&tapFn, &tap);
  std::vector<float> oL(128), oR(128); uint32_t h = 0x811c9dc5u; std::string qh;
  for (int b = 0; b < c.quanta; b++) { r.block(oL.data(), oR.data(), 128); hashQuantum(h, qh, oL.data(), oR.data()); }
  char b[9]; c1::hex8(h, b); out->hash = b; out->qh = qh;
  if (chainsOn == 1) {
    if (r.chainsHost() == nullptr) { std::printf("FAIL %s: no chains host\n", c.name.c_str()); g_fail++; }
    else { const ChainsHostStats st = r.chainsHost()->stats(); if (st.failedBlocks || st.droppedEvents || st.badEvents || st.feedMismatch) { std::printf("FAIL %s: renderer host counters\n", c.name.c_str()); g_fail++; } }
  }
  return true;
}

// ---- level 3 (C2b): LiveHost. The case's silent song (the renderer's), the golden's input signals through the test tap, the golden's events through the host's own event() right after the play started (what the renderer test does
// after setChains), the output taken in calls of `cs` frames from frame0 `f0`: the golden's hash, exactly. flags: F_CHAINS alone (the golden is the mixer's output) or + F_EFFECTS | F_MASTER (then the reference is the renderer with both).
struct LiveTapCtx { const Case* c; const Inputs* in; bool mono = false; };
static void liveTap(void* user, Mixer& mix, int64_t qStart) {
  LiveTapCtx* x = (LiveTapCtx*)user;
  if (!x->mono) { x->mono = true; for (auto& q : x->c->inputs) if (q.second) mix.setMono(q.first, true); }
  Tap t{x->c, x->in}; tapFn(&t, mix.inputsL(), mix.inputsR(), 128, qStart);
}
struct LiveSeamCtx { LiveHost* h; const Case* c; };
static void liveInject(void* user) { LiveSeamCtx* x = (LiveSeamCtx*)user; if (x->h->chainsHostForTest()) for (const SeqEvent& e : x->c->events) x->h->chainsHostForTest()->event(e); }
static bool runLive(const Case& c, Out* out, int cs, int64_t f0, uint32_t flags, const char* what, thunder_live_stats* stOut = nullptr) {
  const BusPlan bp = bus_plan(c.patch, c.chainOn); uint32_t nt = noteTracksOf(c); for (int t = 0; t < kSongNT; t++) if (bp.feedMask[t]) nt &= ~(1u << t);
  std::string row = "["; for (int t = 0; t < kSongNT; t++) row += std::string(t ? "," : "") + ((nt >> t & 1u) ? "0" : "-1"); row += "]";
  const std::string text = songText(c, row, "[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]");
  thunder_live_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.sample_rate = kSr; o.max_block = 4096; o.flags = flags | THUNDER_LIVE_F_CHAINS;
  char err[256] = {0}; std::unique_ptr<LiveHost> H(LiveHost::create(o, err, sizeof err));
  if (!H) { std::printf("FAIL %s [%s]: create: %s\n", c.name.c_str(), what, err); g_fail++; return false; }
  if (H->songPublish((const uint8_t*)text.data(), text.size(), nullptr, 0, 1, err, sizeof err)) { std::printf("FAIL %s [%s]: song_publish: %s\n", c.name.c_str(), what, err); g_fail++; return false; }
  const Inputs in = makeInputs(c); LiveTapCtx tc{&c, &in}; LiveSeamCtx sc{H.get(), &c};
  H->setTestTap(&liveTap, &tc); H->setTestAudioSeam(&liveInject, &sc);
  thunder_live_play_opts po; std::memset(&po, 0, sizeof po); po.size = sizeof po; po.t0 = f0; po.spp = -1;
  if (H->play(po)) { std::printf("FAIL %s [%s]: play\n", c.name.c_str(), what); g_fail++; return false; }
  const int64_t N = (int64_t)c.quanta * 128; std::vector<float> L, R; L.reserve((size_t)N); R.reserve((size_t)N); std::vector<float> a((size_t)cs), b((size_t)cs);
  int64_t frame = f0, done = 0;
  while (done < N) { const int n = (int)std::min<int64_t>(cs, N - done); if (H->process(a.data(), b.data(), n, frame, 0)) { std::printf("FAIL %s [%s]: process\n", c.name.c_str(), what); g_fail++; return false; } L.insert(L.end(), a.begin(), a.begin() + n); R.insert(R.end(), b.begin(), b.begin() + n); done += n; frame += n; }
  uint32_t h = 0x811c9dc5u; std::string qh; for (int q = 0; q < c.quanta; q++) hashQuantum(h, qh, L.data() + (size_t)q * 128, R.data() + (size_t)q * 128);
  char hb[9]; c1::hex8(h, hb); out->hash = hb; out->qh = qh;
  thunder_live_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; H->statsGet(&st); if (stOut) *stOut = st;
  if (st.chains_failed_blocks || st.chains_events_dropped || st.chains_events_bad || st.chains_feed_mismatch || st.reanchors) { std::printf("FAIL %s [%s]: live counters failed %llu dropped %llu bad %llu mismatch %llu reanchors %u\n", c.name.c_str(), what, (unsigned long long)st.chains_failed_blocks, (unsigned long long)st.chains_events_dropped, (unsigned long long)st.chains_events_bad, (unsigned long long)st.chains_feed_mismatch, st.reanchors); g_fail++; }
  return true;
}

// the dispatch cases: a song whose bus track (track 3 of case a/electro) plays a row with BYP (command 38, value 0x11: switch 1 bypassed) and TPO (command 19, value 90): both reach the chains host through
// SongRenderer::dispatch, the node's tempo becomes 90, and the chc is not counted as ignored (the tempo still is: the Players see it too)
static void testSeqEvents(const Case& c) {
  const std::string text = songText(c, "[1,1,1,0]", "[60,-1,-1,-1,-1,0,38,17,19,90,-1,-1]", "[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"); Song song; JsonError je;
  if (!song.load(text.data(), text.size(), &je)) { std::printf("FAIL: the event song did not load (%s)\n", je.msg ? je.msg : "?"); g_fail++; return; }
  SeqVars vars; RenderPads pads; SeqOpts so; so.row = song.renderFrom; so.once = true;
  int ign0 = 0;
  for (int on = 0; on < 2; on++) {
    SongRenderer r(song, vars, pads, kSr, 128, so); r.setChains(on != 0);
    std::vector<float> L(128), R(128); for (int b = 0; b < 400; b++) r.block(L.data(), R.data(), 128);
    if (on) {
      const ChainsHostStats st = r.chainsHost()->stats(); const bool node = r.chainsHost()->node(3) != nullptr;
      CHECK("dispatch: chc and tempo reached the host and were applied", node && st.eventsApplied >= 2 && st.chcNoNode == 0 && st.droppedEvents == 0 && st.badEvents == 0);
      CHECK("dispatch: the node's tempo is the TPO's", node && r.chainsHost()->node(3)->engine().transport().tempo == 90);
      CHECK("dispatch: the one chc is no longer counted ignored; the tempo still is (the Players see it too)", r.stats().eventsIgnored == ign0 - 1);
    } else { ign0 = r.stats().eventsIgnored; CHECK("chains off: no host, chc and tempo are ignored as before", r.chainsHost() == nullptr && ign0 >= 2); }
  }
}

// G1: a BUS track that plays its own notes while its source has none is kept (the page's keepT: every audible track with notes, buses included): it gets its node and its chain sounds (an oscillator in the
// bus's patch, no input at all). Without the fix (buses left out of the note tracks) the bus gets no node and is silent.
static void testBusOwnNotes() {
  Case c; for (int t = 0; t < kSongNT; t++) { c.mix[t] = {224, 128, 128, false, false}; c.chainOn[t] = t < 2; }
  const std::string patch = "{\"format\":\"chains.patch\",\"version\":1,\"modules\":[{\"id\":\"i1\",\"type\":\"trackIn\",\"params\":{\"track\":1,\"pre\":0}},{\"id\":\"osc1\",\"type\":\"osc\",\"params\":{\"wave\":1,\"freq\":330,\"fm\":0,\"level\":0.5}},"
    "{\"id\":\"mA\",\"type\":\"smixer\",\"params\":{\"chans\":2,\"level\":1,\"l1\":1,\"p1\":0,\"l2\":1,\"p2\":0}},{\"id\":\"o2\",\"type\":\"chainOut\",\"params\":{\"track\":2,\"level\":1}}],\"cables\":["
    "{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1\"]},{\"from\":[\"osc1\",\"out\"],\"to\":[\"mA\",\"in1R\"]},{\"from\":[\"i1\",\"out\"],\"to\":[\"mA\",\"in2\"]},{\"from\":[\"i1\",\"outR\"],\"to\":[\"mA\",\"in2R\"]},"
    "{\"from\":[\"mA\",\"out\"],\"to\":[\"o2\",\"in\"]},{\"from\":[\"mA\",\"outR\"],\"to\":[\"o2\",\"inR\"]}]}";
  JsonDoc pd; if (!pd.parse(patch)) { CHECK("G1 patch parses", false); return; } c.patch = pd.root();
  const std::string text = songText(c, "[-1,0]", "[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]"); Song song; JsonError je;   // track 0 (the source): no notes; track 1 (the bus): notes
  if (!song.load(text.data(), text.size(), &je)) { CHECK("G1 song loads", false); return; }
  SeqVars vars; RenderPads pads; SeqOpts so; so.row = song.renderFrom; so.once = true;
  float peak[2] = {0, 0};
  for (int on = 0; on < 2; on++) {
    SongRenderer r(song, vars, pads, kSr, 128, so); CHECK("G1 setChains", r.setChains(on != 0) == 0);
    std::vector<float> L(128), R(128); for (int b = 0; b < 40; b++) { r.block(L.data(), R.data(), 128); for (float v : L) peak[on] = std::max(peak[on], std::fabs(v)); }
    if (on) CHECK("G1 the bus track with notes has a node", r.chainsHost() && r.chainsHost()->node(1) != nullptr);
  }
  CHECK("G1 the bus's own chain sounds (the oscillator), and without chains there is silence", peak[1] > 0.01f && peak[0] == 0.f);
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: test_chains_song <chains_song_cases.json> [name]\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary); std::ostringstream ss; ss << f.rdbuf(); const std::string text = ss.str();
  JsonDoc doc; if (!doc.parse(text)) { std::printf("FAIL: the golden file does not parse\n"); return 1; }
  Json cs = doc.root().get("cases"); const char* only = argc > 2 ? argv[2] : nullptr;
  int n = 0, nExact = 0, nRend = 0, nRendExact = 0, nLive = 0, nLiveExact = 0, nLiveFx = 0, nLiveFxExact = 0;
  for (size_t i = 0; i < cs.size(); i++) {
    Case c = readCase(cs.at(i)); if (only && c.name.find(only) == std::string::npos) continue;
    n++; const int before = g_fail; Out o;
    if (runMixer(c, &o)) report(c, o, "Mixer+ChainsHost");
    if (g_fail == before) nExact++;
    // a hash that equals one of the golden's negative controls would mean the case does not pin what it says
    for (size_t k = 0; k < c.ctl.size(); k++) if (c.ctl.valAt(k).asStr() == o.hash) { std::printf("FAIL %s: equals the negative control %s\n", c.name.c_str(), std::string(c.ctl.keyAt(k)).c_str()); g_fail++; }
    { const int b2 = g_fail; nRend++; Out r; if (runRenderer(c, &r)) report(c, r, "SongRenderer"); if (g_fail == b2) nRendExact++; }
    if (c.stem < 0) {   // C2b: LiveHost (F_CHAINS): the golden's hash at every call size and frame0 of the matrix (call size and frame0 must not matter)
      static const int kSizes[] = {1, 128, 333, 480, 4096}; static const int64_t kF0[] = {0, 37, (int64_t)1 << 31};
      // R2: a live host hears every track (mute and solo are the mixer's); the offline renderer keeps only the audible tracks that have notes (and the buses they feed). Where the two sets differ (a muted bus, a bus that plays no note:
      // e/mute-bus, l/chain-out-no-notes) the live output is the Mixer + ChainsHost output with every track heard; elsewhere it is the golden's.
      Out liveRef; bool sameSet = true; { Json en = c.expect.get("nodes"); uint32_t gm = 0; for (size_t i = 0; i < en.size(); i++) gm |= 1u << (int)en.at(i).asNum();
        const uint32_t want = chains_feed(c.patch, c.chainOn) & (c.patch.isObj() && c.patch.str("format") == "chains.patch" ? 0xFFFFFFFFu : 0u); sameSet = gm == want; }
      if (sameSet) liveRef = o; else { const int bb = g_fail; runMixer(c, &liveRef, true); if (g_fail != bb) std::printf("  (the heard-all reference of %s failed)\n", c.name.c_str()); std::printf("  %s: the live heard set (every track) differs from the golden's offline set (R2): compared with Mixer+ChainsHost, every track heard\n", c.name.c_str()); }
      for (int cs : kSizes) for (int64_t f0 : kF0) {
        if (kSanitize && !((cs == 128 && f0 == 0) || (cs == 333 && f0 == 37) || (cs == 4096 && f0 == ((int64_t)1 << 31)))) continue;   // the reduced matrix of a sanitizer build
        const int b3 = g_fail; Out l; char what[64]; std::snprintf(what, sizeof what, "LiveHost cs %d f0 %lld", cs, (long long)f0); nLive++;
        if (runLive(c, &l, cs, f0, 0, what)) { if (l.hash != liveRef.hash) { g_fail++; std::printf("FAIL %s [%s]: hash %s, expected %s%s\n", c.name.c_str(), what, l.hash.c_str(), liveRef.hash.c_str(), sameSet ? " (the golden)" : " (Mixer+ChainsHost, every track heard)"); } } if (g_fail == b3) nLiveExact++;
      }
      // F_EFFECTS | F_MASTER: the renderer with both
      if (sameSet) { Out r2, l2; const int b4 = g_fail; runRenderer(c, &r2, 1, true, true); runLive(c, &l2, 480, 37, THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER, "LiveHost fx+master");
        if (r2.hash != l2.hash) { g_fail++; std::printf("FAIL %s [LiveHost fx+master]: hash %s, SongRenderer (chains + effects + master) %s\n", c.name.c_str(), l2.hash.c_str(), r2.hash.c_str()); }
        nLiveFx++; if (g_fail == b4) nLiveFxExact++; }
    }
    std::printf("%-28s %s\n", c.name.c_str(), g_fail == before ? "exact" : "FAILED");
  }
  for (size_t i = 0; i < cs.size() && !only; i++) { Case c = readCase(cs.at(i)); if (c.name == "a/electro") {
    testSeqEvents(c); testBusOwnNotes();
    Out never, on, off; runRenderer(c, &never, 0); runRenderer(c, &on, 1); runRenderer(c, &off, 2);   // setChains(false) after true: the render is the chains-off one again
    CHECK("setChains(false) after true: the render equals one that never had chains", off.hash == never.hash && on.hash != never.hash);
    break; } }
  std::printf("chains_song: %d cases, %d exact through Mixer+ChainsHost; %d through SongRenderer, %d exact; %d LiveHost runs, %d exact; %d LiveHost fx+master runs equal SongRenderer: %d\n", n, nExact, nRend, nRendExact, nLive, nLiveExact, nLiveFx, nLiveFxExact);
  if (n == 0) { std::printf("FAIL: no case ran\n"); return 1; }
  std::printf(g_fail ? "FAILED (%d)\n" : "OK\n", g_fail);
  return g_fail ? 1 : 0;
}
