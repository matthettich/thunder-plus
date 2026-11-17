// U26 (PROVISIONAL INTERFACE, FOR THE LEAD'S REVIEW): the live shell (core/src/engine/live_shell.h) against the rendered path. THE CLAIM: the shell's output for a sequence of note / rate / bpm / wob events is the same, bit for bit,
//   (1) as the JS cores driven by the live rule (tools/engine-u26-dump.mjs seqRender: the committed Linux hashes of the "seq_*" cases, 122 sequences: bass, vox, acid, mach; single note, legato, retrigger, glide, slides,
//       syllable changes, `same`, rests, bpm / wob changes, events at every offset around the control-block and host-block edges), at every host block size 1 / 17 / 64 / 128 and an uneven pattern;
//   (2) as the rendered path itself: renderBass / renderVox (xeng_bass, xeng_vox) for one note and for the whole lyric: the same samples before the trim's fade;
//   (3) as the rule written once more here directly on the cores (random event sequences, random host blocks).
// Also the lane decoder (the page's gate / trig / leg lanes as note events) and the shell's limits (a full queue, a non-finite value, an event outside the call).
//   test_live_u26 <refDir> <engine_u26_voices.json>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "engine/bass_core.h"
#include "engine/engine.h"
#include "engine/live_shell.h"
#include "engine/vox_core.h"
#include "engine/xeng.h"
#include "engine/xeng_live.h"
#include "rt/json.h"
#include "sha256.h"
#include "golden/engine_u26_golden.inc"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
static const bool localMustMatch = true;
#else
static const bool localMustMatch = false;
#endif

#define XP_GETP(n, d) if (o.get(#n).isNum()) P.n = o.get(#n).asNum();
static void applyBass(BassP& P, Json o) { XP_BASS(XP_GETP) }
static void applyVox(VoxP& P, Json o) { XP_VOX(XP_GETP) if (o.get("txt").isStr()) P.txt = std::string(o.get("txt").asStr()); }
static void applyAcid(AcidP& P, Json o) { XP_ACID(XP_GETP) }
static void applyMach(MachP& P, Json o) { XP_MACH(XP_GETP) }

struct Seq { std::string name, kind; double sr; size_t N; Json p; std::vector<LiveEvent> ev; };   // ev: sorted by time (stable), only those inside N, and the End event at N

static LiveEvent evOf(Json e) {
  LiveEvent v; v.at = (uint32_t)e.num("t"); const std::string_view t = e.str("type");
  if (t == "on") { v.type = LiveEvType::NoteOn; v.leg = e.get("leg").asBool(); v.acc = e.get("acc").asBool(); v.same = e.get("same").asBool(); v.v = e.num("syl", 0); }
  else if (t == "off") v.type = LiveEvType::NoteOff;
  else if (t == "rate") { v.type = LiveEvType::RateSet; v.v = e.num("v"); }
  else if (t == "ramp") { v.type = LiveEvType::RateRamp; v.v = e.num("v"); v.dur = e.num("dur"); }
  else if (t == "bpm") { v.type = LiveEvType::Bpm; v.v = e.num("v"); }
  else { v.type = LiveEvType::Wob; v.v = e.num("v"); }
  return v;
}
static Seq seqOf(Json c) {
  Seq s; s.name = std::string(c.str("name")); s.kind = std::string(c.str("kind")); s.sr = c.num("sr"); s.N = (size_t)c.num("N"); s.p = c.obj("p");
  Json E = c.arr("events"); for (size_t i = 0; i < E.size(); i++) { LiveEvent v = evOf(E.at(i)); if (v.at < s.N && !(getenv("U26_DROP") && (v.type == LiveEvType::Wob || (v.type == LiveEvType::Bpm && getenv("U26_DROP")[0] == 'b')))) s.ev.push_back(v); }
  std::stable_sort(s.ev.begin(), s.ev.end(), [](const LiveEvent& a, const LiveEvent& b) { return a.at < b.at; });
  LiveEvent end; end.type = LiveEvType::End; end.at = (uint32_t)s.N; s.ev.push_back(end);
  return s;
}

// one shell run: `blocks` is the cycle of host block sizes, the first call starts `first` samples late (the stream is cut into calls differently), L / R have N + latency samples
template <class S> static void setCore(S&, const Seq&);
template <> void setCore<LiveBass>(LiveBass& s, const Seq& q) { BassP p; applyBass(p, q.p); s.core().set(p); }
template <> void setCore<LiveVox>(LiveVox& s, const Seq& q) { VoxP p; applyVox(p, q.p); CHECK(s.core().set(p), "%s: set", q.name.c_str()); }
template <> void setCore<LiveAcid>(LiveAcid& s, const Seq& q) { AcidP p; applyAcid(p, q.p); s.core().set(p); }
template <> void setCore<LiveMach>(LiveMach& s, const Seq& q) { MachP p; applyMach(p, q.p); s.core().set(p); }

template <class S> static void runShell(const Seq& q, const std::vector<uint32_t>& blocks, std::vector<float>& L, std::vector<float>& R, uint32_t* dropped = nullptr) {
  S* sh = new S(q.sr); setCore<S>(*sh, q);
  const size_t total = q.N + S::latency(); L.assign(total + 8, 12345.0f); R.assign(total + 8, 12345.0f);
  size_t pos = 0, ei = 0, bi = 0; std::vector<LiveEvent> tmp;
  while (pos < total) {
    size_t n = blocks[bi++ % blocks.size()]; if (n > total - pos) n = total - pos;
    tmp.clear(); while (ei < q.ev.size() && q.ev[ei].at < pos + n) { LiveEvent e = q.ev[ei++]; e.at -= (uint32_t)pos; tmp.push_back(e); }
    sh->process(L.data() + pos, R.data() + pos, (uint32_t)n, tmp.data(), (uint32_t)tmp.size()); pos += n;
  }
  CHECK(L[total] == 12345.0f && R[total + 7] == 12345.0f, "%s: the shell wrote past its output", q.name.c_str());
  if (dropped) *dropped = sh->dropped();
  CHECK(!sh->failed(), "%s: the shell failed", q.name.c_str());
  delete sh;
  L.resize(total); R.resize(total);
}
static void runKind(const Seq& q, const std::vector<uint32_t>& b, std::vector<float>& L, std::vector<float>& R) {
  if (q.kind == "bass") runShell<LiveBass>(q, b, L, R); else if (q.kind == "vox") runShell<LiveVox>(q, b, L, R); else if (q.kind == "acid") runShell<LiveAcid>(q, b, L, R); else runShell<LiveMach>(q, b, L, R);
}
static uint32_t latencyOf(const std::string& k) { return k == "bass" ? LiveBass::latency() : LiveVox::latency(); }

static std::string hashLR(const std::vector<float>& L, const std::vector<float>& R, size_t from, size_t n) { Sha256 h; h.add((const char*)(L.data() + from), n * 4); h.add((const char*)(R.data() + from), n * 4); return h.hex(); }

// ---- (3) the rule, written once more directly on the cores (the oracle of the random sequences)
template <class A> static void ruleDrive(const Seq& q, std::vector<float>& L, std::vector<float>& R, void (*setup)(typename A::Core&, const Seq&)) {
  typename A::Core core(q.sr); setup(core, q); const double BL = A::kBlk; L.assign(q.N + 8, 0.0f); R.assign(q.N + 8, 0.0f);
  LiveRateLane lane; size_t ei = 0; double s = 0; const double N = (double)q.N;
  while (s < N) {
    while (ei < q.ev.size() && (double)q.ev[ei].at <= s) { const LiveEvent& e = q.ev[ei++];
      switch (e.type) { case LiveEvType::NoteOn: A::on(core, e); break; case LiveEvType::NoteOff: A::off(core); break; case LiveEvType::RateSet: lane.set((double)e.at, e.v); break;
        case LiveEvType::RateRamp: lane.ramp((double)e.at, e.v, e.dur); break; case LiveEvType::Bpm: A::bpm(core, e.v); break; case LiveEvType::Wob: A::wob(core, e.v); break; case LiveEvType::End: break; } }
    double nn = 1e18; for (size_t j = ei; j < q.ev.size(); j++) { const LiveEvType t = q.ev[j].type; if (t == LiveEvType::NoteOn || t == LiveEvType::NoteOff || t == LiveEvType::End) { nn = (double)q.ev[j].at; break; } }
    double end = std::min(std::min(s + BL, nn), N);
    core.render(L.data(), R.data(), q.N, s, end, lane.at(s)); s = end;
  }
}
static void setupBass(BassCore& c, const Seq&) { BassP p; c.set(p); }
static void setupVox(VoxCore& c, const Seq&) { VoxP p; p.txt = "ba-na-na, la strengths"; c.set(p); }
static void setupAcid(AcidCore& c, const Seq&) { AcidP p; c.set(p); }
static void setupMach(MachCore& c, const Seq&) { MachP p; p.machine = 4; c.set(p); }

static uint32_t rs = 12345;
static uint32_t rnd() { rs = rs * 1664525u + 1013904223u; return rs >> 8; }

static Seq randomSeq(const char* kind, size_t N) {
  Seq s; s.name = std::string("random ") + kind; s.kind = kind; s.sr = 48000; s.N = N; double lastRate = 1; bool on = false;
  const int ne = 4 + (int)(rnd() % 14); uint32_t t = rnd() % 40;
  for (int i = 0; i < ne && t < N; i++) {
    LiveEvent e; e.at = t; const uint32_t k = rnd() % 10;
    if (k < 3 || !on) { e.type = LiveEvType::NoteOn; e.leg = on && rnd() % 2; e.acc = rnd() % 3 == 0; e.same = rnd() % 4 == 0; e.v = (double)(rnd() % 6) + ((rnd() % 5 == 0) ? 0.4 : 0); on = true; }
    else if (k < 5) { e.type = LiveEvType::NoteOff; on = false; }
    else if (k < 7) { e.type = LiveEvType::RateSet; e.v = lastRate = 0.25 + (rnd() % 400) / 100.0; }
    else if (k < 8) { e.type = LiveEvType::RateRamp; e.v = 0.2 + (rnd() % 300) / 100.0; e.dur = (double)(rnd() % 1500); lastRate = e.v; }
    else if (k < 9) { e.type = LiveEvType::Bpm; e.v = 20 + rnd() % 300; }
    else { e.type = LiveEvType::Wob; e.v = (rnd() % 4) * 0.5; }
    s.ev.push_back(e); t += (rnd() % 3 == 0) ? 0 : rnd() % 700;   // same-sample clusters are in
  }
  std::stable_sort(s.ev.begin(), s.ev.end(), [](const LiveEvent& a, const LiveEvent& b) { return a.at < b.at; });
  LiveEvent end; end.type = LiveEvType::End; end.at = (uint32_t)N; s.ev.push_back(end); (void)lastRate;
  return s;
}

// ---- the page's lanes (gate / trig / leg per sample) as note events
static void lanesFromEvents(const Seq& q, size_t n, std::vector<double>& gate, std::vector<double>& trig, std::vector<double>& leg) {
  gate.assign(n, 0); trig.assign(n, 0); leg.assign(n, 0); double g = 0, t = 0, l = 0, cnt = 0; size_t ei = 0;
  for (size_t s = 0; s < n; s++) {
    while (ei < q.ev.size() && q.ev[ei].at <= s) { const LiveEvent& e = q.ev[ei++]; if (e.type == LiveEvType::NoteOn) { g = 1; t = ++cnt; l = e.leg ? 1 : 0; } else if (e.type == LiveEvType::NoteOff) g = 0; }
    gate[s] = g; trig[s] = t; leg[s] = l;
  }
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("FAIL: usage: test_live_u26 <refDir> <engine_u26_voices.json>\n"); return 1; }
  const std::string refDir = argv[1]; std::string rj, cj;
  if (!readFile(refDir + "/ref.json", &rj) || !readFile(refDir + "/cases.json", &cj)) { std::printf("FAIL: the reference (ref.json, cases.json) is missing\n"); return 1; }
  JsonDoc rdoc, cdoc; if (!rdoc.parse(rj) || !cdoc.parse(cj)) { std::printf("FAIL: a reference file does not parse\n"); return 1; }
  eng_init();
  std::map<std::string, const U26CaseGolden*> cg; for (int i = 0; i < kU26CaseGoldenN; i++) cg[kU26CaseGolden[i].name] = &kU26CaseGolden[i];
  std::map<std::string, std::string> cref; { Json RR = rdoc.root().arr("cases"); for (size_t i = 0; i < RR.size(); i++) cref[std::string(RR.at(i).str("name"))] = std::string(RR.at(i).str("sha256")); }
  static const std::vector<std::vector<uint32_t>> kPatterns = {{128}, {64}, {17}, {1}, {5, 127, 128, 129, 31, 33, 1, 2, 300}};
  // ---- (1) the sequences of the JS reference, at every host block pattern
  {
    Json C = cdoc.root().arr("cases"); int ran = 0, exact = 0, same = 0, localDiff = 0; std::map<std::string, std::pair<int, int>> per;
    for (size_t ci = 0; ci < C.size(); ci++) {
      Json c = C.at(ci); if (!c.get("live").asBool()) continue;
      const Seq q = seqOf(c); const auto g = cg.find(q.name); ran++; per[q.kind].second++;
      if (g == cg.end()) { CHECK(false, "%s: no committed hash", q.name.c_str()); continue; }
      const uint32_t lat = latencyOf(q.kind); std::vector<float> L0, R0; bool allSame = true, hashOk = false, localOk = false;
      for (size_t pi = 0; pi < kPatterns.size(); pi++) {
        std::vector<float> L, R; runKind(q, kPatterns[pi], L, R);
        if (pi == 0) { L0 = L; R0 = R; if (const char* d = getenv("U26_DUMP")) { std::ofstream f(std::string(d) + "/" + q.name + ".f32", std::ios::binary); f.write((const char*)(L.data() + lat), (long)(q.N * 4)); }   // debugging
 const std::string hex = hashLR(L, R, lat, q.N); hashOk = hex == g->second->sha256 && (long)q.N == g->second->len; const auto r = cref.find(q.name); localOk = r != cref.end() && r->second == hex;
          for (uint32_t i = 0; i < lat; i++) if (L[i] != 0 || R[i] != 0) { CHECK(false, "%s: the first %u samples (the latency) must be silent", q.name.c_str(), lat); break; } }
        else if (std::memcmp(L.data(), L0.data(), L.size() * 4) != 0 || std::memcmp(R.data(), R0.data(), R.size() * 4) != 0) { allSame = false; std::printf("%-30s output differs between host block patterns 0 and %zu\n", q.name.c_str(), pi); }
      }
      if (hashOk) { exact++; per[q.kind].first++; } if (allSame) same++; if (!localOk) localDiff++;
      if (!(hashOk && allSame && (localOk || !localMustMatch))) { fails++; std::printf("%-30s %s%s%s\n", q.name.c_str(), hashOk ? "bit-exact (committed hash)" : "DIFFERS from the committed hash", allSame ? "" : "  [host block sizes differ]", localOk ? "" : "  [the local Node reference differs]"); }
    }
    std::string tl; for (const auto& t : per) tl += (tl.empty() ? "" : " ") + t.first + ":" + std::to_string(t.second.first) + "/" + std::to_string(t.second.second);
    std::printf("live u26: %d of %d sequences equal the JS reference's hash at host blocks 128 / 64 / 17 / 1 / uneven (%s); %d identical at every block size; local Node differs for %d\n", exact, ran, tl.c_str(), same, localDiff);
    if (exact != ran) fails++;
    CHECK(per["bass"].second >= 60 && per["vox"].second >= 25 && per["acid"].second >= 10 && per["mach"].second >= 10, "too few live sequences (%d %d %d %d)", per["bass"].second, per["vox"].second, per["acid"].second, per["mach"].second);
  }
  // ---- (2) the rendered path: renderBass for one note, renderVox for the whole lyric
  {
    int checked = 0;
    for (double sr : {48000.0, 22050.0}) for (double hold : {0.0, 0.3, 1.1}) for (int sound = 0; sound < 4; sound++) {
      BassP p; if (sound == 1) { p.cut = 90; p.wRate = 7; p.wCut = 3; p.wShape = 2; } if (sound == 2) { p.atk = 40; p.glide = 100; p.rel = 300; p.parts = 24; } if (sound == 3) { p.det = 20; p.comb = 0.5; p.vowel = 2.5; p.wVowel = 0.4; p.wCut = 2; p.rel = 900; }
      const double bpm = sound == 1 ? 133 : 120;
      const size_t two = xeng_bass(p, sr, hold, 0, bpm, nullptr, 0); if (two == (size_t)-1 || two == 0) { CHECK(false, "xeng_bass refused a plain voice"); continue; }
      std::vector<float> out(two + 8, 12345.0f); const size_t rl = xeng_bass(p, sr, hold, 0, bpm, out.data(), two); CHECK(rl > 1 && rl <= two / 2, "xeng_bass length");
      const size_t N = two / 2; const double h = std::max(0.05, hold ? hold : 2.0), Nd = std::min(std::ceil((h + p.rel / 1000 * 1.6 + 0.02) * sr), sr * 32), hN = std::min(Nd, std::round(h * sr));
      Seq q; q.name = "rendered bass"; q.kind = "bass"; q.sr = sr; q.N = N;
      { LiveEvent e; e.type = LiveEvType::NoteOn; e.at = 0; q.ev.push_back(e); e = LiveEvent(); e.type = LiveEvType::NoteOff; e.at = (uint32_t)hN; if (e.at < N) q.ev.push_back(e); e = LiveEvent(); e.type = LiveEvType::End; e.at = (uint32_t)N; q.ev.push_back(e); }
      LiveBass* sh = new LiveBass(sr); sh->core().set(p); sh->core().bpm = bpm;
      std::vector<float> L(N + LiveBass::latency()), R(N + LiveBass::latency()); size_t pos = 0, ei = 0; const uint32_t blk = sound % 2 ? 128 : 37; std::vector<LiveEvent> tmp;
      while (pos < L.size()) { size_t n = std::min<size_t>(blk, L.size() - pos); tmp.clear(); while (ei < q.ev.size() && q.ev[ei].at < pos + n) { LiveEvent e = q.ev[ei++]; e.at -= (uint32_t)pos; tmp.push_back(e); }
        sh->process(L.data() + pos, R.data() + pos, (uint32_t)n, tmp.data(), (uint32_t)tmp.size()); pos += n; }
      delete sh;
      const size_t fl = (size_t)std::min<double>((double)rl, std::round(sr * 0.002)); size_t bad = 0;
      for (size_t i = 0; i + fl < rl; i++) { const float m = (float)(((double)L[i + LiveBass::latency()] + (double)R[i + LiveBass::latency()]) * 0.5); if (std::memcmp(&m, &out[i], 4) != 0) bad++; }
      for (size_t i = rl; i < N; i++) { const float m = (float)(((double)L[i + LiveBass::latency()] + (double)R[i + LiveBass::latency()]) * 0.5); if (!(std::fabs(m) < 1e-4f)) bad++; }
      CHECK(bad == 0, "bass sound %d sr %g hold %g: the live shell differs from renderBass in %zu samples", sound, sr, hold, bad); checked++;
    }
    for (double sr : {48000.0, 22050.0}) for (int sound = 0; sound < 4; sound++) {
      VoxP p; p.txt = sound == 0 ? "la" : sound == 1 ? "ba-na-na, la" : sound == 2 ? "strengths, hello world." : "[B AY] [S IY]"; if (sound == 2) { p.sylLen = 120; p.glide = 90; p.choir = 3; } if (sound == 3) { p.rel = 400; p.robot = 0.2; }
      const size_t cap = xeng_vox(p, sr, nullptr, 0); if (cap == (size_t)-1 || cap == 0) { CHECK(false, "xeng_vox refused a plain voice"); continue; }
      std::vector<float> out(cap + 8, 12345.0f); const size_t rl = xeng_vox(p, sr, out.data(), cap); CHECK(rl >= 1 && rl <= cap, "xeng_vox length");
      // the schedule of renderVox: found by running its loop on a probe core (the busy() coda decides when the next syllable starts)
      VoxCore probe(sr); probe.set(p); std::vector<float> sc(cap + 8, 0.0f); const double capD = sr * 30, hold = std::max(20.0, std::round(probe.p().sylLen / 1000 * sr)); double pos = 0; Seq q; q.name = "rendered vox"; q.kind = "vox"; q.sr = sr;
      for (double k = 0; k < std::max(1.0, (double)probe.nSyl()) && pos < capD - sr; k++) {
        LiveEvent e; e.type = LiveEvType::NoteOn; e.at = (uint32_t)pos; e.leg = k > 0; e.v = k; q.ev.push_back(e); probe.on(k > 0, k, false);
        probe.render(sc.data(), nullptr, cap, pos, std::min(capD, pos + hold), 1); pos = std::min(capD, pos + hold);
        e = LiveEvent(); e.type = LiveEvType::NoteOff; e.at = (uint32_t)pos; q.ev.push_back(e); probe.off(); double g = 0;
        while (probe.busy() && g < sr && pos < capD) { const double en = std::min(capD, pos + 64); probe.render(sc.data(), nullptr, cap, pos, en, 1); pos = en; g += 64; }
      }
      const double tail = std::min(capD, pos + std::round((probe.p().rel / 1000 * 1.6 + 0.03) * sr)); q.N = (size_t)tail;
      LiveEvent e; e.type = LiveEvType::End; e.at = (uint32_t)tail; q.ev.push_back(e);
      LiveVox* sh = new LiveVox(sr); sh->core().set(p);
      std::vector<float> L(q.N + LiveVox::latency()), R(q.N + LiveVox::latency()); size_t ps = 0, ei = 0; std::vector<LiveEvent> tmp; const uint32_t blk = sound % 2 ? 64 : 100;
      while (ps < L.size()) { size_t n = std::min<size_t>(blk, L.size() - ps); tmp.clear(); while (ei < q.ev.size() && q.ev[ei].at < ps + n) { LiveEvent x = q.ev[ei++]; x.at -= (uint32_t)ps; tmp.push_back(x); }
        sh->process(L.data() + ps, R.data() + ps, (uint32_t)n, tmp.data(), (uint32_t)tmp.size()); ps += n; }
      delete sh;
      const size_t fl = (size_t)std::min<double>((double)rl, std::round(sr * 0.002)); size_t bad = 0;
      for (size_t i = 0; i + fl < rl; i++) if (std::memcmp(&L[i + LiveVox::latency()], &out[i], 4) != 0) bad++;
      for (size_t i = rl; i < q.N; i++) if (!(std::fabs(L[i + LiveVox::latency()]) < 1e-4f)) bad++;
      CHECK(bad == 0, "vox sound %d sr %g: the live shell differs from renderVox in %zu samples (%zu syllables)", sound, sr, bad, probe.nSyl()); checked++;
    }
    std::printf("live u26: %d rendered-path comparisons (renderBass one note, renderVox the whole lyric, two sample rates) equal the live shell before the final fade\n", checked);
  }
  // ---- (3) random sequences against the rule written directly on the cores, at random host blocks
  {
    int ran = 0; const char* kinds[4] = {"bass", "vox", "acid", "mach"};
    for (int rep = 0; rep < 12; rep++) for (const char* kind : kinds) {
      Seq q = randomSeq(kind, 2000 + rnd() % 3000); std::vector<float> L0, R0; const std::string k = kind;
      if (k == "bass") ruleDrive<BassLive>(q, L0, R0, setupBass); else if (k == "vox") ruleDrive<VoxLive>(q, L0, R0, setupVox); else if (k == "acid") ruleDrive<AcidLive>(q, L0, R0, setupAcid); else ruleDrive<MachLive>(q, L0, R0, setupMach);
      for (int pat = 0; pat < 4; pat++) {
        std::vector<uint32_t> blocks; for (int j = 0; j < 8; j++) blocks.push_back(pat == 0 ? 128 : pat == 1 ? 1 + rnd() % 5 : 1 + rnd() % 300); if (pat == 3) blocks = {1};
        const uint32_t lat = latencyOf(q.kind);
        // the shell needs the same parameters as the oracle: bass / acid defaults, mach machine 4, vox lyrics
        std::vector<float> L, R;
        {
          const size_t total = q.N + lat; L.assign(total, 0.0f); R.assign(total, 0.0f); size_t pos = 0, ei = 0, bi = 0; std::vector<LiveEvent> tmp;
          auto drive = [&](auto* sh) { while (pos < total) { size_t n = std::min<size_t>(blocks[bi++ % blocks.size()], total - pos); tmp.clear(); while (ei < q.ev.size() && q.ev[ei].at < pos + n) { LiveEvent e = q.ev[ei++]; e.at -= (uint32_t)pos; tmp.push_back(e); }
              sh->process(L.data() + pos, R.data() + pos, (uint32_t)n, tmp.data(), (uint32_t)tmp.size()); pos += n; } CHECK(!sh->failed() && sh->dropped() == 0, "random sequence: failed or dropped events"); delete sh; };
          if (k == "bass") { auto* s = new LiveBass(q.sr); BassP p; s->core().set(p); drive(s); }
          else if (k == "vox") { auto* s = new LiveVox(q.sr); VoxP p; p.txt = "ba-na-na, la strengths"; s->core().set(p); drive(s); }
          else if (k == "acid") { auto* s = new LiveAcid(q.sr); AcidP p; s->core().set(p); drive(s); }
          else { auto* s = new LiveMach(q.sr); MachP p; p.machine = 4; s->core().set(p); drive(s); }
        }
        size_t bad = 0; for (size_t i = 0; i < q.N; i++) if (std::memcmp(&L[i + lat], &L0[i], 4) != 0 || std::memcmp(&R[i + lat], &R0[i], 4) != 0) bad++;
        CHECK(bad == 0, "random %s sequence %d pattern %d: %zu samples differ from the rule driven directly on the core", kind, rep, pat, bad); ran++;
      }
    }
    std::printf("live u26: %d random event sequences x host block patterns equal the rule driven directly on the cores\n", ran);
  }
  // ---- the lane decoder: the page's gate / trig / leg lanes as note events (BassProc.process, VoxProc.process, XProc.process: the same three lines)
  {
    { PageLaneDecoder d; bool leg = false;
      CHECK(d.step(0, 0, 0, &leg) == PageLaneDecoder::kNone, "idle");
      CHECK(d.step(0, 1, 0, &leg) == PageLaneDecoder::kOn && !leg, "gate rises, the counter unchanged: a fresh note (on(false))");
      CHECK(d.step(0, 1, 1, &leg) == PageLaneDecoder::kNone, "nothing changes");
      CHECK(d.step(1, 1, 1, &leg) == PageLaneDecoder::kOn && leg, "the counter moves with the gate high: legato (the leg lane >= 0.5)");
      CHECK(d.step(2, 1, 0.4, &leg) == PageLaneDecoder::kOn && !leg, "the counter moves, leg < 0.5: a retrigger");
      CHECK(d.step(2, 0, 0, &leg) == PageLaneDecoder::kOff, "gate falls: off");
      CHECK(d.step(3, 0, 0, &leg) == PageLaneDecoder::kNone, "the counter moves with the gate low: no event, the counter is remembered");
      CHECK(d.step(3, 1, 0, &leg) == PageLaneDecoder::kOn && !leg, "gate rises after that: on(false)");
      CHECK(d.step(4, 0, 0, &leg) == PageLaneDecoder::kOff, "the counter moves and the gate falls in the same sample: off");
      CHECK(d.step(5, 1, 1, &leg) == PageLaneDecoder::kOn && leg, "counter and gate rise together: on(leg >= 0.5)"); }
    // lanes built the way the page schedules them, decoded: the notes come back at their samples, in order (a note-off and a note-on in the same sample reach the page as the new note only: see the notes)
    int ran = 0; Json C = cdoc.root().arr("cases");
    for (size_t ci = 0; ci < C.size(); ci++) { Json c = C.at(ci); if (!c.get("live").asBool() || c.str("kind") != "bass" || c.str("name").find("_init_") == std::string_view::npos) continue;
      const Seq q = seqOf(c); std::vector<double> g, t, l; lanesFromEvents(q, q.N, g, t, l); PageLaneDecoder d; std::vector<std::pair<size_t, int>> got, want;
      for (size_t s = 0; s < q.N; s++) { bool leg; const auto o = d.step(t[s], g[s], l[s], &leg); if (o == PageLaneDecoder::kOn) got.push_back({s, leg ? 2 : 1}); else if (o == PageLaneDecoder::kOff) got.push_back({s, 0}); }
      for (size_t i = 0; i < q.ev.size(); i++) { const LiveEvent& e = q.ev[i]; const bool nextOnSame = i + 1 < q.ev.size() && q.ev[i + 1].at == e.at && q.ev[i + 1].type == LiveEvType::NoteOn;
        if (e.type == LiveEvType::NoteOn) want.push_back({e.at, e.leg ? 2 : 1}); else if (e.type == LiveEvType::NoteOff && !nextOnSame) want.push_back({e.at, 0}); }
      // (the page passes the leg lane's value whatever the gate was: a note whose leg lane is 1 is on(true) even from silence; the core decides what that means)
      if (got != want) { for (size_t i = 0; i < std::max(got.size(), want.size()); i++) std::printf("  %zu: got %s want %s\n", i, i < got.size() ? (std::to_string(got[i].first) + "/" + std::to_string(got[i].second)).c_str() : "-", i < want.size() ? (std::to_string(want[i].first) + "/" + std::to_string(want[i].second)).c_str() : "-"); }
      CHECK(got == want, "%s: the decoded lanes differ from the events (%zu vs %zu notes)", q.name.c_str(), got.size(), want.size()); ran++; }
    CHECK(ran >= 5, "decoder: too few sequences"); std::printf("live u26: lane decoder: %d sequences' lanes decode to their notes\n", ran);
  }
  // ---- limits
  {
    LiveBass* sh = new LiveBass(48000); std::vector<float> L(300), R(300); std::vector<LiveEvent> ev;
    for (int i = 0; i < 400; i++) { LiveEvent e; e.at = 10; e.type = LiveEvType::RateSet; e.v = 1; ev.push_back(e); }
    sh->process(L.data(), R.data(), 128, ev.data(), (uint32_t)ev.size()); CHECK(sh->dropped() == 400 - LiveBass::kPend, "a full queue drops what does not fit (%u)", sh->dropped());
    LiveEvent bad; bad.type = LiveEvType::RateSet; bad.v = NAN; bad.at = 0; LiveEvent late; late.type = LiveEvType::NoteOn; late.at = 500;
    const uint32_t d0 = sh->dropped(); sh->process(L.data(), R.data(), 128, &bad, 1); sh->process(L.data(), R.data(), 128, &late, 1);
    CHECK(sh->rejected() == 1 && sh->dropped() == d0 + 1, "a non-finite value is rejected, an event outside the call is dropped");
    sh->process(L.data(), R.data(), 300, nullptr, 0); for (int i = 0; i < 300; i++) if (!std::isfinite(L[i]) || !std::isfinite(R[i])) { CHECK(false, "the shell wrote a non-finite sample"); break; }
    LiveEvent on; on.type = LiveEvType::NoteOn; on.at = 0; for (int rep = 0; rep < 8; rep++) { sh->process(L.data(), R.data(), 128, &on, 1); } CHECK(!sh->failed(), "retriggers");
    delete sh;
    {   // the AudioParam ranges: rate 0..256, bpm 10..999, wob 0..64 (a value beyond is the same as the bound; a negative rate is 0)
      struct Pair { LiveEvType t; double a, b; };
      for (const Pair& pr : {Pair{LiveEvType::RateSet, 1000, 256}, Pair{LiveEvType::RateSet, -3, 0}, Pair{LiveEvType::RateRamp, 1e5, 256}, Pair{LiveEvType::Bpm, 5, 10}, Pair{LiveEvType::Bpm, 5000, 999}, Pair{LiveEvType::Wob, 100, 64}, Pair{LiveEvType::Wob, -1, 0}}) {
        std::vector<float> A(600), B(600), A2(600), B2(600); BassP p; p.wCut = 3; p.det = 10;
        for (int w = 0; w < 2; w++) { LiveBass* s2 = new LiveBass(48000); s2->core().set(p); LiveEvent e[3]; e[0].type = LiveEvType::NoteOn; e[1].type = pr.t; e[1].at = 40; e[1].v = w ? pr.b : pr.a; e[1].dur = 200; e[2].type = LiveEvType::Bpm; e[2].at = 300; e[2].v = 120;
          if (pr.t == LiveEvType::Bpm) e[2].type = LiveEvType::Wob, e[2].v = 0;
          s2->process(w ? B.data() : A.data(), w ? B2.data() : A2.data(), 600, e, 3); delete s2; }
        CHECK(std::memcmp(A.data(), B.data(), 600 * 4) == 0 && std::memcmp(A2.data(), B2.data(), 600 * 4) == 0, "an event value beyond the AudioParam's range (%g) must act as the bound (%g)", pr.a, pr.b);
      }
    }
    LiveVox* v = new LiveVox(48000); VoxP p; p.txt = "la na"; v->core().set(p); LiveEvent n; n.type = LiveEvType::NoteOn; n.v = 0.5 + 1e9; n.at = 0; std::vector<float> a(200), b(200); v->process(a.data(), b.data(), 64, &n, 1); v->process(a.data(), b.data(), 64, nullptr, 0);
    CHECK(!v->failed(), "a syllable number is rounded like Math.round (a fractional lane value is a whole syllable)"); delete v;
    LiveVox* w = new LiveVox(48000); w->core().set(p); LiveEvent nn; nn.type = LiveEvType::NoteOn; nn.v = 1e300; nn.at = 0; w->process(a.data(), b.data(), 64, &nn, 1); w->process(a.data(), b.data(), 64, nullptr, 0); CHECK(!w->failed(), "a huge syllable number wraps"); delete w;
  }
  std::printf("live u26: %d failed\n", fails);
  if (!fails) std::printf("PASS test_live_u26\n");
  return fails ? 1 : 0;
}
