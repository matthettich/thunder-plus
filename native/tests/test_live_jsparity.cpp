// LIVE ROUTE gate L10 (b): the live pool against the JS. The sequences of tools/engine-u26-dump.mjs `seqRender` (the live rule of the real core; its committed Linux hashes, golden/engine_u26_golden.inc) are turned back into the
// SEQUENCER EVENTS that would make them (a note with its rate, a slide for a legato note or a ramp, an off), played through Players::dispatch on a live pad, and each node's engine output BEFORE gain / pan (LivePool::setTap)
// must hash to the JS's, bit for bit. A sequence is played only if the sequencer can say it: a rate event rides on the note at its sample (a note's rate, or its slide), a ramp only on a legato note, no bpm / wob lane events,
// no note-off in the sample of a note-on (the one expected difference: the pool takes the off back, D4 / C3; the JS's seqRender does not), vox: the syllable and `same` lanes the node's own counter gives. The rest are listed with the reason.
//   test_live_jsparity <refDir> (the cases.json of engine-u26-dump.mjs)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "engine/live_pool.h"
#include "engine/xeng.h"
#include "render/players.h"
#include "rt/json.h"
#include "rt/sampletime.h"
#include "sha256.h"
#include "song/song.h"
#include "golden/engine_u26_golden.inc"

using namespace thunder;
static int g_fail = 0;
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

#define XP_GETP(n, d) if (o.get(#n).isNum()) P.n = o.get(#n).asNum();
static void applyBass(BassP& P, Json o) { XP_BASS(XP_GETP) }
static void applyVox(VoxP& P, Json o) { XP_VOX(XP_GETP) if (o.get("txt").isStr()) P.txt = std::string(o.get("txt").asStr()); }
static void applyAcid(AcidP& P, Json o) { XP_ACID(XP_GETP) }
static void applyMach(MachP& P, Json o) { XP_MACH(XP_GETP) }

struct Ev { double t; std::string type; double c[5], o[7]; double v = 0, dur = 0; bool leg = false, acc = false, same = false; double syl = 0; };
struct Tapped { std::vector<float> L, R; int64_t want = 0; };
static void tapFn(void* u, int, int, int j, const float* L, const float* R, int frames, int64_t q0) {
  if (j != 0) return;   // (vox: the poly nodes are silent here)
  Tapped& T = *(Tapped*)u; for (int i = 0; i < frames; i++) if (q0 + i < (int64_t)T.L.size()) { T.L[(size_t)(q0 + i)] = L[i]; T.R[(size_t)(q0 + i)] = R[i]; }
}

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: test_live_jsparity refDir\n"); return 2; }
  std::ifstream f(std::string(argv[1]) + "/cases.json", std::ios::binary); std::stringstream ss; ss << f.rdbuf(); const std::string text = ss.str();
  JsonDoc doc; JsonError je; if (!doc.parse(text, &je)) { std::printf("cases.json does not parse\n"); return 2; }
  std::map<std::string, const U26CaseGolden*> cg; for (int i = 0; i < kU26CaseGoldenN; i++) cg[kU26CaseGolden[i].name] = &kU26CaseGolden[i];
  const std::string sj = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";
  Song song; if (!song.load(sj.data(), sj.size(), &je)) return 2;
  SeqVars vars; SeqOpts so; so.row = song.renderFrom; so.once = true; Seq seq(song, vars, SeqMode::Song, so);
  Json C = doc.root().arr("cases"); int ran = 0, same = 0; std::map<std::string, int> skip, perKind, okKind;
  for (size_t ci = 0; ci < C.size(); ci++) {
    Json c = C.at(ci); if (!c.get("live").asBool()) continue;
    const std::string name(c.str("name")), kind(c.str("kind")); const double sr = c.num("sr"); const int64_t N = (int64_t)c.num("N");
    std::vector<Ev> evs; Json E = c.arr("events");
    for (size_t i = 0; i < E.size(); i++) { Json e = E.at(i); Ev v; v.t = e.num("t"); v.type = std::string(e.str("type")); v.v = e.num("v", 0); v.dur = e.num("dur", 0); v.leg = e.get("leg").asBool(); v.acc = e.get("acc").asBool(); v.same = e.get("same").asBool(); v.syl = e.num("syl", 0); for (int k = 0; k < 5; k++) v.c[k] = e.get("c").isArr() && e.get("c").at((size_t)k).isNum() ? e.get("c").at((size_t)k).asNum() : std::nan(""); for (int k = 0; k < 7; k++) v.o[k] = e.get("o").isArr() && e.get("o").at((size_t)k).isNum() ? e.get("o").at((size_t)k).asNum() : std::nan(""); if (v.t < (double)N) evs.push_back(v); }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t; });
    // the translation; `why` names what the sequencer cannot say
    std::vector<SeqEvent> out; std::string why; double v0 = 1, t0 = 0, v1 = 1, t1 = 0;
    auto at = [&](double t) { return t >= t1 ? v1 : v0 + (v1 - v0) * ((t - t0) / (t1 - t0)); };
    for (size_t i = 0; i < evs.size() && why.empty();) {
      size_t j = i; while (j < evs.size() && evs[j].t == evs[i].t) j++;
      int nOn = 0, nOff = 0; const Ev* rateEv = nullptr; const Ev* ramp = nullptr; const Ev* on = nullptr; bool laneAfterOn = false;
      for (size_t k = i; k < j; k++) { const Ev& e = evs[k];
        if (e.type == "on") { nOn++; if (!on) on = &e; } else if (e.type == "off") nOff++;
        else if (e.type == "rate") { rateEv = &e; if (on) laneAfterOn = true; } else if (e.type == "ramp") { ramp = &e; if (on) laneAfterOn = true; } else if (e.type == "eng") {} else why = "a " + e.type + " event"; }
      if (!why.empty()) break;
      if (nOn && nOff) { why = "a note-off in the sample of a note-on (D4: expected difference)"; break; }
      if (laneAfterOn) { why = "a rate event after the note in its sample"; break; }
      if ((rateEv || ramp) && nOn != 1) { why = "a rate event without exactly one note"; break; }
      if (ramp && !on->leg) { why = "a ramp on a note that is not legato"; break; }
      if (kind == "vox" && nOn && on->same != on->leg) { why = "vox: `same` is not the slide"; break; }
      for (size_t k = i; k < j; k++) { const Ev& g = evs[k];   // the group's events in their order (an eng before or after the note keeps its place)
        if (g.type == "on" && &g == on) {
          SeqEvent e; e.type = SEV_NOTE; e.t = sample_time((int64_t)evs[i].t, sr); e.track = 0; e.pad = 0; e.gain = 1; e.pan = 128; e.poly = false; e.acc = on->acc;
          double from = rateEv ? rateEv->v : at(evs[i].t);   // the lane's value when the note starts
          if (ramp) { e.rate = ramp->v; e.has |= SH_SLIDE; e.slideFrom = from; e.slideDur = ramp->dur / sr; }
          else { e.rate = rateEv ? rateEv->v : from; if (on->leg) { e.has |= SH_SLIDE; e.slideFrom = e.rate; e.slideDur = 0.001; } }
          out.push_back(e);
        } else if (g.type == "off") { SeqEvent e; e.type = SEV_OFF; e.t = sample_time((int64_t)evs[i].t, sr); e.track = 0; out.push_back(e); }
        else if (g.type == "eng") { SeqEvent e; e.type = SEV_ENG; e.t = sample_time((int64_t)evs[i].t, sr); e.track = 0; for (int q = 0; q < 5; q++) e.engC[q] = g.c[q]; for (int q = 0; q < 7; q++) e.engO[q] = g.o[q]; out.push_back(e); } }
      if (!nOn && !nOff && (rateEv || ramp)) { why = "a rate event with no note"; break; }
      for (size_t k = i; k < j; k++) { const Ev& e = evs[k]; if (e.type == "rate") { v0 = v1 = e.v; t0 = t1 = e.t; } else if (e.type == "ramp") { const double cur = at(e.t); v0 = cur; t0 = e.t; v1 = e.v; t1 = e.t + e.dur; } }
      i = j;
    }
    perKind[kind]++;
    if (!why.empty()) { skip[why]++; continue; }
    const auto g = cg.find(name); if (g == cg.end()) { CHECKF(false, "%s: no committed hash", name.c_str()); continue; }
    if (std::getenv("ENGDBG")) std::fprintf(stderr, "CASE %s\n", name.c_str());
    // play it
    std::unique_ptr<RenderPads> pads(new RenderPads()); pads->route[0] = PadRoute::Live; Players pl(pads.get(), sr, 8);
    LivePadConfig cfg; cfg.trackMask = 1;
    Json p = c.obj("p");
    if (kind == "bass") { cfg.kind = LiveKind::Bass; applyBass(cfg.bass, p); } else if (kind == "vox") { cfg.kind = LiveKind::Vox; applyVox(cfg.vox, p); } else if (kind == "acid") { cfg.kind = LiveKind::Acid; applyAcid(cfg.acid, p); } else { cfg.kind = LiveKind::Mach; applyMach(cfg.mach, p); }
    if (!pl.live().addPad(0, cfg)) { CHECKF(false, "%s: addPad", name.c_str()); continue; }
    const int64_t total = (N + 127) / 128 * 128; Tapped T; T.L.assign((size_t)total, 0.f); T.R.assign((size_t)total, 0.f); pl.live().setTap(tapFn, &T); pl.live().bookEnd(N);   // (seqRender's last control block ends at N)
    std::vector<LiveTraceRec> trace; trace.reserve(1024); pl.live().setTrace(&trace);
    std::vector<float> bl(128), br(128); float* inL[kVoiceTracks] = {}; float* inR[kVoiceTracks] = {}; inL[0] = bl.data(); inR[0] = br.data();
    std::vector<char> done(out.size(), 0);
    for (int64_t q0 = 0; q0 < total; q0 += 128) {
      const double t0q = (double)q0 / sr; pl.setTime(t0q, q0);
      for (size_t i = 0; i < out.size(); i++) if (!done[i] && out[i].t - 0.25 <= t0q) { done[i] = 1; pl.dispatch(out[i], seq, song); }
      std::fill(bl.begin(), bl.end(), 0.f); std::fill(br.begin(), br.end(), 0.f); pl.render(inL, inR, 128, t0q, q0);
    }
    if (const char* dn = std::getenv("JSP_TRACE")) if (name == dn) { for (const LiveTraceRec& r : trace) std::printf("  trace %c t %.9g T %lld v %g leg %d\n", r.what, r.t, (long long)r.T, r.v, (int)r.leg); for (const SeqEvent& e : out) std::printf("  ev type %d t %.9g rate %g slide %d from %g dur %g\n", (int)e.type, e.t, e.rate, (int)((e.has & SH_SLIDE) != 0), e.slideFrom, e.slideDur); for (const Ev& e : evs) std::printf("  js %s t %g v %g leg %d\n", e.type.c_str(), e.t, e.v, (int)e.leg); }
    // vox: the syllable lane the node's counter gave must be the sequence's
    if (kind == "vox") { size_t k = 0; bool okSyl = true; for (const Ev& e : evs) if (e.type == "on") { size_t m = 0; for (const LiveTraceRec& r : trace) if (r.what == 'n') { if (m++ == k) { if (r.v != std::round(e.syl)) okSyl = false; break; } } k++; }
      if (!okSyl) { skip["vox: the syllable numbers are not the node's counter's"]++; continue; } }
    Sha256 h; h.add((const char*)T.L.data(), (size_t)N * 4); h.add((const char*)T.R.data(), (size_t)N * 4);
    if (const char* rd = std::getenv("JSP_REF")) if (name.rfind("seq_eng_", 0) == 0) { std::ifstream rf(std::string(rd) + "/c" + std::to_string(ci) + ".f32", std::ios::binary); std::vector<float> ref((size_t)N * 2); rf.read((char*)ref.data(), (std::streamsize)ref.size() * 4);
      for (int64_t i = 0; i < N; i++) if (std::memcmp(&ref[(size_t)i], &T.L[(size_t)i], 4)) { std::printf("  %s: first L diff at %lld: JS %.9g pool %.9g\n", name.c_str(), (long long)i, ref[(size_t)i], T.L[(size_t)i]); break; }
      if (std::getenv("JSP_DUMP")) { for (int64_t i = 2646; i < 2662; i++) std::printf("   %lld JS %.9g pool %.9g\n", (long long)i, ref[(size_t)i], T.L[(size_t)i]); int64_t cnt = 0, mx = 0; double md = 0; for (int64_t i = 0; i < N; i++) if (std::memcmp(&ref[(size_t)i], &T.L[(size_t)i], 4)) { cnt++; md = std::fmax(md, std::fabs((double)ref[(size_t)i] - T.L[(size_t)i])); mx = i; } std::printf("  ndiff %lld maxdiff %g last %lld\n", (long long)cnt, md, (long long)mx); } }
    ran++; perKind[kind + "/ran"]++;
    const bool ok = h.hex() == g->second->sha256 && N == g->second->len;
    if (ok) { same++; okKind[kind]++; } else { std::printf("%-34s the pool's node output differs from seqRender's hash\n", name.c_str()); g_fail++; }
  }
  std::string sk; for (const auto& s : skip) sk += "\n    " + std::to_string(s.second) + " x " + s.first;
  std::printf("live jsparity: %d of %d played sequences equal seqRender's hash bit for bit (bass %d/%d, vox %d/%d, acid %d/%d, mach %d/%d); not played (the sequencer cannot say them):%s\n", same, ran,
              okKind["bass"], perKind["bass/ran"], okKind["vox"], perKind["vox/ran"], okKind["acid"], perKind["acid/ran"], okKind["mach"], perKind["mach/ran"], sk.c_str());
  CHECKF(ran >= 40, "too few sequences could be played (%d)", ran);
  if (g_fail) { std::printf("live_jsparity: %d FAILED\n", g_fail); return 1; }
  std::printf("live_jsparity: all checks passed\n"); return 0;
}
