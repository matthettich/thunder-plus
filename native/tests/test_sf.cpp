// U32: the SoundFont player against the web app's own code (native/tools/sf-ref.mjs: index.html's SoundFont block and core/seq-core.js in a vm), fed the SAME decoded PCM.
//   test_sf export DIR     writes DIR/export.json and the PCM files the cases need (decoded here: stb_vorbis for the GM font's SF3 samples, int16 for the test font's SF2 samples)
//   test_sf compare DIR    reads DIR/ref_* (made by `node sf-ref.mjs --ref DIR`) and checks: the parse (canonical text of every zone), sfSrc, Math.log10, sfVoice (every zone of the GM font, bitwise), sfPick (every preset, key and
//                          a few velocities), the pad render (bit-exact, every case), the live note at the web copy's 128-frame blocks (bit-exact against core/seq-core.js) and at host blocks of 64, 17, 1, uneven and one block (equal to the 128 run bit for bit),
//                          the committed hashes (sf_golden.inc: the test font's on every platform, the GM font's on Linux x86-64)
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "rt/jsmath.h"
#include "rt/json.h"
#include "sf/sf.h"
#include "sf/sf_live.h"
#include "sf/sf_math.h"
#include "voice/voice.h"
#include "golden/sf_golden.inc"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif

using namespace thunder;
using namespace thunder::sf;

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool readFile(const std::string& p, std::string* out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); *out = s.str(); return true; }
static bool writeFile(const std::string& p, const void* d, size_t n) { std::ofstream f(p, std::ios::binary); if (!f) return false; f.write((const char*)d, (std::streamsize)n); return (bool)f; }
static uint64_t fnv(const void* d, size_t n) { const uint8_t* p = (const uint8_t*)d; uint64_t h = 0xcbf29ce484222325ull; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; } return h; }

struct FontBox { std::string key; std::vector<uint8_t> bytes; Font f; };
struct Rig {
  JsonDoc cdoc; std::string ctext; Json cases; std::map<std::string, FontBox> fonts;
  bool load() {
    if (!readFile(std::string(THUNDER_ROOT) + "/native/tests/golden/sf_cases.json", &ctext) || !cdoc.parse(ctext)) { std::printf("FAIL: sf_cases.json\n"); return false; }
    cases = cdoc.root(); Json F = cases.obj("fonts");
    for (size_t i = 0; i < F.size(); i++) {
      FontBox& fb = fonts[std::string(F.keyAt(i))]; fb.key = std::string(F.keyAt(i)); std::string b;
      if (!readFile(std::string(THUNDER_ROOT) + "/" + std::string(F.valAt(i).asStr()), &b)) { std::printf("FAIL: font %s\n", fb.key.c_str()); return false; }
      fb.bytes.assign(b.begin(), b.end());
      const ParseStatus st = parse(fb.bytes.data(), fb.bytes.size(), fb.f);
      if (st != kParseOk) { std::printf("FAIL: parse %s: %d\n", fb.key.c_str(), (int)st); return false; }
    }
    return true;
  }
};

static PadParams padOf(Json p) {
  PadParams P; P.bank = p.num("bank", 0); P.prog = p.num("prog", 0); P.trans = p.num("trans", 0); P.velo = p.num("velo", 0.7); P.atk = p.num("atk", 1); P.rel = p.num("rel", 1); P.level = p.num("level", 0); P.len = p.num("len", 0);
  return P;
}
static void keyVel(const PadParams& P, double rate, double gain, double* key, double* vel) {
  const double lr = js_log2(jmax(1e-4, rate)); *key = jclamp(js_round(60 + 12 * lr + P.trans), 0, 127);
  *vel = jclamp(js_round(127 * ((1 - P.velo) + P.velo * std::sqrt(jclamp(gain, 0, 1)))), 1, 127);
}
// the samples a case needs: the picks' samples and each stereo pick's partner
static void neededFor(const Font& f, const Preset* pr, double key, double vel, std::set<uint32_t>& out) {
  Pick pk[4]; const int n = sf_pick(f, pr, key, vel, pk);
  for (int i = 0; i < n; i++) { out.insert(pk[i].z->s); const SampleHdr& s = f.samples[pk[i].z->s]; if (pk[i].stereo && s.link < f.samples.size()) out.insert(s.link); }
}
struct CaseSamples { std::set<uint32_t> idx; };
static void samplesOfRender(const Font& f, Json c, CaseSamples& o) {
  const PadParams P = padOf(c.obj("pad")); const Preset* pr = sf_preset(f, js_round(P.bank), js_round(P.prog));
  neededFor(f, pr, jclamp(60 + js_round(P.trans), 0, 127), 100, o.idx);
}
static void samplesOfLive(const Font& f, Json c, CaseSamples& o) {
  const PadParams P = padOf(c.obj("pad")); const Preset* pr = sf_preset(f, js_round(P.bank), js_round(P.prog)); Json E = c.arr("events");
  for (size_t i = 0; i < E.size(); i++) { Json e = E.at(i); if (e.str("type") != "note") continue; double k, v; keyVel(P, e.num("rate"), e.num("gain"), &k, &v); neededFor(f, pr, k, v, o.idx); }
}
static void decodeInto(const Font& f, const std::set<uint32_t>& idx, PresetPcm& pcm, double ctxRate = 0) {
  DecodeOpts dopt; dopt.ctxRate = ctxRate;
  pcm.byIndex.assign(f.samples.size(), Pcm()); pcm.bytes = 0;
  for (uint32_t i : idx) { decode_sample(f, i, dopt, pcm.byIndex[i]); pcm.bytes += pcm.byIndex[i].d.size() * 4; }
}

static int doExport(const std::string& dir) {
  Rig R; if (!R.load()) return 1;
  std::map<std::string, std::set<uint32_t>> need;
  Json Rn = R.cases.arr("renders"), Lv = R.cases.arr("lives");
  for (size_t i = 0; i < Rn.size(); i++) { CaseSamples cs; samplesOfRender(R.fonts[std::string(Rn.at(i).str("font"))].f, Rn.at(i), cs); need[std::string(Rn.at(i).str("font"))].insert(cs.idx.begin(), cs.idx.end()); }
  for (size_t i = 0; i < Lv.size(); i++) { CaseSamples cs; samplesOfLive(R.fonts[std::string(Lv.at(i).str("font"))].f, Lv.at(i), cs); need[std::string(Lv.at(i).str("font"))].insert(cs.idx.begin(), cs.idx.end()); }
  std::string js = "{\"samples\":[";
  bool first = true; char buf[400];
  for (auto& kv : need) {
    const Font& f = R.fonts[kv.first].f;
    for (uint32_t i : kv.second) {
      Pcm p; decode_sample(f, i, DecodeOpts(), p);
      char fn[80]; std::snprintf(fn, sizeof fn, "pcm_%s_%u.f32", kv.first.c_str(), i);
      if (!writeFile(dir + "/" + fn, p.d.data(), p.d.size() * 4)) { std::printf("FAIL: write %s\n", fn); return 1; }
      std::snprintf(buf, sizeof buf, "%s{\"font\":\"%s\",\"idx\":%u,\"sr\":%.17g,\"ls\":%.17g,\"le\":%.17g,\"k\":%.17g,\"n\":%zu,\"ok\":%d,\"file\":\"%s\"}", first ? "" : ",", kv.first.c_str(), i, p.sr, p.ls, p.le, p.k, p.d.size(), p.ok ? 1 : 0, fn);
      js += buf; first = false;
    }
  }
  js += "]}"; writeFile(dir + "/export.json", js.data(), js.size());
  std::printf("exported %zu samples\n", (size_t)std::count(js.begin(), js.end(), '{') - 1);
  return 0;
}

// ---- canonical text of a parsed font (the same lines as sf-ref.mjs's parseText) ----------------------------------------------------------
static std::string parseText(const Font& f) {
  std::string o; char b[512];
  std::snprintf(b, sizeof b, "font %s|%d|%zu|%zu|%zu\n", f.name.c_str(), f.sf3 ? 1 : 0, f.samples.size(), f.presets.size(), f.smplN); o += b;
  for (const SampleHdr& s : f.samples) { std::snprintf(b, sizeof b, "s %s|%u|%u|%u|%u|%u|%d|%d|%d|%d\n", s.name, s.start, s.end, s.ls, s.le, s.sr, (int)s.root, (int)s.corr, (int)s.link, (int)s.type); o += b; }
  for (const Preset& p : f.presets) {
    std::snprintf(b, sizeof b, "p %s|%d|%d|%u\n", p.name, p.bank, p.prog, p.nz); o += b;
    for (uint32_t zi = 0; zi < p.nz; zi++) {
      const Zone& z = f.zones[p.z0 + zi]; std::snprintf(b, sizeof b, "z %d,%d,%d,%d,%u|", z.klo, z.khi, z.vlo, z.vhi, z.s); o += b;
      for (int i = 0; i < z.gN; i++) { const GenV& g = f.gens[z.gOff + (uint32_t)i]; std::snprintf(b, sizeof b, "%s%d=%d", i ? "," : "", (int)g.op, (int)g.v); o += b; }
      for (int w = 0; w < 2; w++) { o += "|"; const uint32_t off = w ? z.pmOff : z.imOff, n = w ? z.pmN : z.imN;
        for (uint32_t i = 0; i < n; i++) { const Mod& m = f.mods[off + i]; std::snprintf(b, sizeof b, "%s%u,%u,%d,%u,%u", i ? ";" : "", m.src, m.dst, (int)m.amt, m.as, m.tr); o += b; } }
      o += "\n";
    }
  }
  return o;
}

static std::string firstDiffLine(const std::string& a, const std::string& b) {
  size_t i = 0, line = 1; while (i < a.size() && i < b.size() && a[i] == b[i]) { if (a[i] == '\n') line++; i++; }
  auto ln = [&](const std::string& s) { size_t st = s.rfind('\n', i ? i - 1 : 0); st = st == std::string::npos ? 0 : st + 1; size_t en = s.find('\n', i); return s.substr(st, (en == std::string::npos ? s.size() : en) - st).substr(0, 300); };
  return "line " + std::to_string(line) + "\n  native: " + ln(a) + "\n  js:     " + ln(b);
}

static bool refBin(const std::string& dir, const std::string& id, std::vector<char>* out) {
  std::string s; if (!readFile(dir + "/ref_" + id + ".bin", &s)) return false; out->assign(s.begin(), s.end()); return true;
}

// the record list of sfVoice, in the order sf-ref.mjs writes it
static void voiceRecs(const Font& f, double key, double vel, const Zone& z, std::vector<double>& r) {
  VoiceParams o; sf_voice(f, z, key, vel, o);
  const double a[26] = {o.root, o.excl, (double)o.modes, o.scale, o.tune, o.atten, o.pan, o.fc, o.q, o.modEnvToFc, o.ofsStart, o.ofsEnd, o.ofsLs, o.ofsLe, o.v.delay, o.v.atk, o.v.hold, o.v.dec, o.v.sus, o.v.rel, o.m.delay, o.m.atk, o.m.hold, o.m.dec, o.m.sus, o.m.rel};
  r.insert(r.end(), a, a + 26);
}

// ---- the live note: one case, any host block pattern ----------------------------------------------------------------------------------------
static void mkEvent(Json e, double sr, SeqEvent& o, std::vector<double>& arp) {
  o = SeqEvent(); const std::string_view ty = e.str("type");
  o.type = ty == "note" ? SEV_NOTE : ty == "off" ? SEV_OFF : ty == "mod" ? SEV_MOD : SEV_STP;
  o.t = e.num("f") / sr; o.track = (int)e.num("track", 0); o.pad = 0;
  if (ty == "note") { o.poly = e.boolean("poly"); o.col = (int)e.num("col"); o.rate = e.num("rate"); o.gain = e.num("gain"); o.pan = e.get("pan").isNum() ? e.num("pan") : 128; }
  if (ty == "stp") o.flag = e.boolean("all");
  if (ty == "off" && e.arr("cols").size()) { o.has |= SH_COLS; for (size_t i = 0; i < e.arr("cols").size(); i++) o.cols |= (uint8_t)(1u << (int)e.arr("cols").at(i).asNum()); }
  if (e.get("slide").isObj()) { o.has |= SH_SLIDE; o.slideFrom = e.obj("slide").num("from"); o.slideDur = e.obj("slide").num("dur"); }
  if (e.get("bend").isObj()) { o.has |= SH_BEND; o.bendMul = e.obj("bend").num("mul"); o.bendT1 = e.obj("bend").num("t1F") / sr; }
  if (e.get("vib").isObj()) { o.has |= SH_VIB; o.vibSpd = e.obj("vib").num("spd"); o.vibDep = e.obj("vib").num("dep"); o.vibT1 = e.obj("vib").num("t1F") / sr; }
  if (e.get("arp").isArr()) { o.has |= SH_ARP; Json A = e.arr("arp"); o.arpN = (int)A.size(); arp.clear(); for (size_t i = 0; i < A.size(); i++) { arp.push_back(A.at(i).at(0).asNum() / sr); arp.push_back(A.at(i).at(1).asNum()); } }
  if (e.get("cutF").isNum()) { o.has |= SH_CUT; o.cut = e.num("cutF") / sr; }
  if (e.get("relF").isNum()) { o.has |= SH_REL; o.rel = e.num("relF") / sr; }
  if (ty == "mod") { if (e.get("gain").isNum()) { o.has |= SH_GAIN; o.gain = e.num("gain"); } if (e.get("pan").isNum()) { o.has |= SH_PAN; o.pan = e.num("pan"); } if (e.get("mul").isNum()) { o.has |= SH_MUL; o.mul = e.num("mul"); } }
}

struct LiveRun { std::vector<float> out; int active = 0; uint32_t dropped = 0, adrop = 0; };
// pattern: host block sizes, repeated (0 entries: one block of everything)
static LiveRun runLive(Json c, const FontBox& fb, const std::vector<int>& pat) {
  const double sr = c.get("sr").isNum() ? c.num("sr") : 48000; const PadParams P = padOf(c.obj("pad")); const int K = (int)c.num("tracks", 1), blocks = (int)std::ceil(c.num("frames") / 128);
  const size_t F = (size_t)blocks * 128;
  CaseSamples cs; samplesOfLive(fb.f, c, cs); PresetPcm pcm; decodeInto(fb.f, cs.idx, pcm);
  SfPad pad; pad.font = &fb.f; pad.pr = sf_preset(fb.f, js_round(P.bank), js_round(P.prog)); pad.pcm = &pcm; pad.P = P;
  VoiceKit kv; kv.syn = !(c.obj("kit").get("syn").isBool() && !c.obj("kit").boolean("syn")); kv.choke = (int)c.obj("kit").num("choke", 0);
  SfPool pool(sr, 32);
  LiveRun r; r.out.assign((size_t)K * 2 * F, 0.f);
  std::vector<float*> L(kVoiceTracks, nullptr), Rr(kVoiceTracks, nullptr);
  for (int t = 0; t < K; t++) { L[(size_t)t] = r.out.data() + ((size_t)t * 2) * F; Rr[(size_t)t] = r.out.data() + ((size_t)t * 2 + 1) * F; }
  Json E = c.arr("events"); std::vector<size_t> order; for (size_t i = 0; i < E.size(); i++) order.push_back(i);
  size_t next = 0, pos = 0, pi = 0; std::vector<double> arp;
  while (pos < F) {
    size_t n = pat.empty() || pat[pi % pat.size()] == 0 ? F - pos : (size_t)pat[pi % pat.size()]; pi++; n = std::min(n, F - pos);
    pool.setFrame(pos);
    for (size_t i = 0; i < E.size(); i++) {
      Json e = E.at(i); if (e.num("f") >= (double)(pos + n) || e.num("f") < (double)pos) continue;
      if (e.str("type") == "stopAll") { pool.stopAll(e.num("f") / sr); continue; }
      SeqEvent ev; mkEvent(e, sr, ev, arp);
      switch (ev.type) { case SEV_NOTE: pool.note(ev, arp.data(), pad, kv); break; case SEV_OFF: pool.off(ev); break; case SEV_MOD: pool.mod(ev, arp.data()); break; default: pool.stp(ev); }
    }
    float* lp[kVoiceTracks]; float* rp[kVoiceTracks];
    for (int t = 0; t < kVoiceTracks; t++) { lp[t] = L[(size_t)t] ? L[(size_t)t] + pos : nullptr; rp[t] = Rr[(size_t)t] ? Rr[(size_t)t] + pos : nullptr; }
    pool.render(lp, rp, (int)n, pos);
    pos += n;
  }
  (void)next; (void)order;
  r.active = pool.active(); r.dropped = pool.dropped(); r.adrop = pool.autoDropped();
  return r;
}

static size_t firstDiffF(const float* a, const float* b, size_t n) { for (size_t i = 0; i < n; i++) if (std::memcmp(&a[i], &b[i], 4) != 0) return i; return n; }
static double dbDiff(const float* a, const float* b, size_t n) { double e = 0, p = 0; for (size_t i = 0; i < n; i++) { const double d = (double)a[i] - b[i]; e += d * d; p += (double)b[i] * b[i]; } return p > 0 ? 10 * std::log10(std::max(e / p, 1e-30)) : (e > 0 ? 0 : -300); }

static int doCompare(const std::string& dir) {
  Rig R; if (!R.load()) return 1;
  std::vector<char> ref;
  // ---- parse
  for (auto& kv : R.fonts) {
    std::string js; CHECK(readFile(dir + "/ref_parse_" + kv.first + ".txt", &js), "no reference parse of %s", kv.first.c_str());
    const std::string mine = parseText(kv.second.f);
    CHECK(mine == js, "parse of %s differs at %s", kv.first.c_str(), firstDiffLine(mine, js).c_str());
    std::printf("parse %s: %zu presets, %zu zones, %zu samples: %s\n", kv.first.c_str(), kv.second.f.presets.size(), kv.second.f.zones.size(), kv.second.f.samples.size(), mine == js ? "text identical to the JS parse" : "DIFFERS");
  }
  // ---- Math.log10, sfSrc
  { std::vector<char> in, out; CHECK(refBin(dir, "log10_in", &in) && refBin(dir, "log10", &out), "no log10 reference");
    const double* x = (const double*)in.data(); const double* y = (const double*)out.data(); const size_t n = in.size() / 8; size_t bad = 0;
    for (size_t i = 0; i < n; i++) { const double m = sf_log10(x[i]); if (std::memcmp(&m, &y[i], 8) != 0) { if (bad++ < 5) std::printf("  log10(%.17g): native %.17g js %.17g\n", x[i], m, y[i]); } }
    CHECK(bad == 0, "sf_log10 differs from Math.log10 in %zu of %zu inputs", bad, n); std::printf("Math.log10: %zu inputs bit-identical\n", n - bad); }
  { std::vector<char> out; CHECK(refBin(dir, "src", &out), "no sfSrc reference"); std::vector<double> mine; uint32_t s = 777; auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s; };
    for (int i = 0; i < 4000; i++) { const unsigned code = i < 2000 ? (rnd() & 0xffff) : (((rnd() & 0x3) << 10) | (rnd() & 0x3ff)); const double kv[4][2] = {{60, 100}, {0, 1}, {127, 127}, {37, 64}};
      for (auto& q : kv) for (int am = 0; am < 2; am++) mine.push_back(sf_src(code, q[0], q[1], am != 0)); }
    CHECK(out.size() == mine.size() * 8 && std::memcmp(out.data(), mine.data(), out.size()) == 0, "sfSrc differs from the JS (%zu values)", mine.size()); std::printf("sfSrc: %zu values bit-identical\n", mine.size()); }
  // ---- sfVoice, sfPick
  for (const char* fk : {"gm", "syn"}) {
    const Font& f = R.fonts[fk].f; std::vector<double> mine; const bool gm = std::string(fk) == "gm";
    std::vector<std::pair<double, double>> KV = {{60, 100}, {21, 1}, {127, 127}, {77, 64}}; if (!gm) { KV.push_back({0, 1}); KV.push_back({59, 91}); KV.push_back({100, 127}); }
    for (const Preset& p : f.presets) for (uint32_t zi = 0; zi < p.nz; zi++) for (auto& q : KV) voiceRecs(f, q.first, q.second, f.zones[p.z0 + zi], mine);
    CHECK(refBin(dir, std::string("voice_") + fk, &ref), "no sfVoice reference");
    const bool same = ref.size() == mine.size() * 8 && std::memcmp(ref.data(), mine.data(), ref.size()) == 0;
    if (!same && ref.size() == mine.size() * 8) { size_t i = 0; while (std::memcmp((const double*)ref.data() + i, &mine[i], 8) == 0) i++; std::printf("  first difference at record %zu field %zu: native %.17g js %.17g\n", i / 26, i % 26, mine[i], ((const double*)ref.data())[i]); }
    CHECK(same, "sfVoice (%s) differs from the JS", fk); std::printf("sfVoice %s: %zu zone x (key, velocity) records, %zu numbers: %s\n", fk, mine.size() / 26, mine.size(), same ? "bit-identical" : "DIFFERS");
    std::vector<int32_t> pk; const int vels4[4] = {1, 64, 100, 127}, vels5[5] = {1, 64, 90, 91, 127};
    for (const Preset& p : f.presets) for (int key = 0; key < 128; key++) for (int vi = 0; vi < (gm ? 4 : 5); vi++) { Pick r[4]; const int n = sf_pick(f, &p, key, gm ? vels4[vi] : vels5[vi], r);
      pk.push_back(n); for (int i = 0; i < n; i++) { pk.push_back((int32_t)(r[i].z - &f.zones[p.z0])); pk.push_back(r[i].stereo ? 1 : 0); } }
    CHECK(refBin(dir, std::string("pick_") + fk, &ref), "no sfPick reference");
    const bool ps = ref.size() == pk.size() * 4 && std::memcmp(ref.data(), pk.data(), ref.size()) == 0; CHECK(ps, "sfPick (%s) differs from the JS", fk);
    std::printf("sfPick %s: %zu presets x 128 keys x %d velocities: %s\n", fk, f.presets.size(), gm ? 4 : 5, ps ? "identical" : "DIFFERS");
  }
  // ---- the pad render
  std::map<std::string, std::vector<float>> mineOut;
  Json Rn = R.cases.arr("renders"); int exactR = 0;
  for (size_t i = 0; i < Rn.size(); i++) {
    Json c = Rn.at(i); const std::string id(c.str("id")); const FontBox& fb = R.fonts[std::string(c.str("font"))];
    CaseSamples cs; samplesOfRender(fb.f, c, cs); PresetPcm pcm; decodeInto(fb.f, cs.idx, pcm);
    const PadParams P = padOf(c.obj("pad")); const double sr = c.num("sr"), hold = c.num("hold");
    const size_t bound = sf_render_bound(fb.f, P, sr, hold); std::vector<float> out(bound); const size_t n = sf_render(fb.f, pcm, P, sr, hold, out.data(), out.size());
    CHECK(n == bound && n > 0, "%s: render length %zu bound %zu", id.c_str(), n, bound);
    CHECK(refBin(dir, "render_" + id, &ref), "%s: no reference", id.c_str());
    const bool same = ref.size() == n * 4 && std::memcmp(ref.data(), out.data(), ref.size()) == 0;
    if (same) exactR++; else { std::printf("%-26s DIFFERS (len %zu vs js %zu", id.c_str(), n, ref.size() / 4); if (ref.size() == n * 4) std::printf(", first at %zu, %.1f dB", firstDiffF(out.data(), (const float*)ref.data(), n), dbDiff(out.data(), (const float*)ref.data(), n)); std::printf(")\n"); fails++; }
    mineOut["render_" + id] = out;
  }
  std::printf("pad render: %d of %zu cases bit-identical to the web's sfRender (same decoded PCM)\n", exactR, Rn.size());
  // ---- the live note
  Json Lv = R.cases.arr("lives"); int exactL = 0, sameBlk = 0, blkRuns = 0;
  const std::vector<std::vector<int>> pats = {{128}, {64}, {17}, {1}, {1, 7, 128, 33, 100, 2, 127, 64, 5}, {0}};
  for (size_t i = 0; i < Lv.size(); i++) {
    Json c = Lv.at(i); const std::string id(c.str("id")); const FontBox& fb = R.fonts[std::string(c.str("font"))];
    CHECK(refBin(dir, "live_" + id, &ref), "%s: no reference", id.c_str());
    LiveRun base = runLive(c, fb, pats[0]);
    const bool same = ref.size() == base.out.size() * 4 && std::memcmp(ref.data(), base.out.data(), ref.size()) == 0;
    if (same) exactL++; else { std::printf("live %-22s DIFFERS from core/seq-core.js", id.c_str()); if (ref.size() == base.out.size() * 4) std::printf(" (first at %zu of %zu, %.1f dB)", firstDiffF(base.out.data(), (const float*)ref.data(), base.out.size()), base.out.size(), dbDiff(base.out.data(), (const float*)ref.data(), base.out.size())); std::printf("\n"); fails++; }
    CHECK(base.dropped == 0 && base.adrop == 0, "%s: dropped %u, automation events dropped %u", id.c_str(), base.dropped, base.adrop);
    double pk = 0; for (float v : base.out) pk = std::max(pk, (double)std::fabs(v)); CHECK(pk > 0.001 && std::isfinite(pk), "%s: silent or not finite (peak %g)", id.c_str(), pk);
    for (size_t pi = 1; pi < pats.size(); pi++) {
      LiveRun r = runLive(c, fb, pats[pi]); blkRuns++; const bool eq = r.out == base.out || (r.out.size() == base.out.size() && std::memcmp(r.out.data(), base.out.data(), base.out.size() * 4) == 0);
      if (eq) sameBlk++; else { fails++; std::printf("live %-22s host block pattern #%zu differs from the 128 run (first at %zu)\n", id.c_str(), pi, firstDiffF(r.out.data(), base.out.data(), base.out.size())); }
    }
    mineOut["live_" + id] = base.out;
  }
  std::printf("live note: %d of %zu cases bit-identical to core/seq-core.js at 128-frame blocks; %d of %d runs at host blocks of 64, 17, 1, uneven and one block equal the 128 run bit for bit\n", exactL, Lv.size(), sameBlk, blkRuns);
  // ---- committed hashes
  { int checked = 0;
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
    const bool gmToo = true;
#else
    const bool gmToo = false;
#endif
    for (int i = 0; i < kSfGoldenN; i++) { const std::string id = kSfGolden[i].id; const bool isGm = id.find("_gm_") != std::string::npos;
      if (isGm && !gmToo) continue;
      auto it = mineOut.find(id); CHECK(it != mineOut.end(), "golden %s has no case", id.c_str()); if (it == mineOut.end()) continue;
      const uint64_t h = fnv(it->second.data(), it->second.size() * 4); CHECK(h == kSfGolden[i].hash && (int)it->second.size() == kSfGolden[i].floats, "%s: native hash %016llx, committed %016llx", id.c_str(), (unsigned long long)h, kSfGolden[i].hash); checked++; }
    CHECK(kSfGoldenN == (int)(Rn.size() + Lv.size()), "golden has %d rows, cases %zu", kSfGoldenN, Rn.size() + Lv.size());
    std::printf("committed hashes: %d of %d checked (the GM font's only on Linux x86-64)\n", checked, kSfGoldenN); }
  std::printf(fails ? "FAIL: %d\n" : "OK\n", fails);
  return fails ? 1 : 0;
}


// ---- the page's own decode and render in Chrome (native/tools/sf-chrome.mjs DIR): the decoder difference, measured
static int doChrome(const std::string& dir, double ctxRate) {
  Rig R; if (!R.load()) return 1;
  std::string meta; if (!readFile(dir + "/chrome_samples.json", &meta)) { std::printf("FAIL: no chrome_samples.json\n"); return 1; }
  JsonDoc md; if (!md.parse(meta)) { std::printf("FAIL: chrome_samples.json\n"); return 1; }
  Json M = md.root(); const FontBox& fb = R.fonts["gm"]; DecodeOpts dopt; dopt.ctxRate = ctxRate;
  Json S = M.arr("samples");
  for (size_t i = 0; i < S.size(); i++) {
    Json s = S.at(i); const uint32_t idx = (uint32_t)s.num("idx");
    std::string b; readFile(dir + "/" + std::string(s.str("file")), &b); Pcm p; decode_sample(fb.f, idx, dopt, p);
    const size_t nc = b.size() / 4; const float* c = (const float*)b.data();
    const size_t m = std::min(nc, p.d.size());
    std::printf("decode sample %4u (file %g Hz): chrome %zu frames, native %zu frames @%g Hz; first %zu frames differ by %.1f dB (without the last 64: %.1f dB)\n", idx, s.num("hdrSr"), nc, p.d.size(), p.sr, m, m ? dbDiff(p.d.data(), c, m) : 0.0, m > 64 ? dbDiff(p.d.data(), c, m - 64) : 0.0);
  }
  Json Rn = R.cases.arr("renders"); Json RR = M.arr("renders"); double worst = -1e9;
  for (size_t i = 0; i < RR.size(); i++) {
    Json r = RR.at(i); const std::string id(r.str("id")); Json c; bool found = false;
    for (size_t j = 0; j < Rn.size(); j++) if (std::string(Rn.at(j).str("id")) == id) { c = Rn.at(j); found = true; }
    if (!found) continue;
    CaseSamples cs; samplesOfRender(fb.f, c, cs); PresetPcm pcm; decodeInto(fb.f, cs.idx, pcm, ctxRate);
    const PadParams P = padOf(c.obj("pad")); const double sr = c.num("sr"), hold = c.num("hold");
    std::vector<float> out(sf_render_bound(fb.f, P, sr, hold)); const size_t n = sf_render(fb.f, pcm, P, sr, hold, out.data(), out.size());
    std::string b; readFile(dir + "/" + std::string(r.str("file")), &b); const size_t nc = b.size() / 4;
    if (nc != n) std::printf("render %-20s: length native %zu, chrome %zu\n", id.c_str(), n, nc);
    const double db = dbDiff(out.data(), (const float*)b.data(), std::min(n, nc)); worst = std::max(worst, db);
    std::printf("render %-20s: native vs the page's sfRender in Chrome: %.1f dB\n", id.c_str(), db);
  }
  std::printf("worst render difference %.1f dB\n", worst);
  return 0;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: test_sf export|compare DIR\n"); return 2; }
  if (!std::strcmp(argv[1], "export")) return doExport(argv[2]);
  if (!std::strcmp(argv[1], "compare")) return doCompare(argv[2]);
  if (!std::strcmp(argv[1], "chrome")) return doChrome(argv[2], argc > 3 ? std::atof(argv[3]) : 0);
  return 2;
}
