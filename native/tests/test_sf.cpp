// U32: the SoundFont player against the web app's own code (native/tools/sf-ref.mjs: index.html's SoundFont block and core/seq-core.js in a vm), fed the SAME decoded PCM.
//   test_sf export DIR     writes DIR/export.json and the PCM files the cases need (decoded here: stb_vorbis for the GM font's SF3 samples, int16 for the test font's SF2 samples)
//   test_sf compare DIR    reads DIR/ref_* (made by `node sf-ref.mjs --ref DIR`) and checks: the parse (canonical text of every zone), sfSrc, Math.log10, sfVoice (every zone of the GM font, bitwise), sfPick (every preset, key and
//                          a few velocities), the pad render (bit-exact, every case), the live note at the web copy's 128-frame blocks (bit-exact against core/seq-core.js) and at host blocks of 64, 17, 1, uneven and one block (equal to the 128 run bit for bit),
//                          the committed hashes (sf_golden.inc: the test font's on every platform, the GM font's on Linux x86-64)
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
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

// Peak live bytes allocated through operator new (counted, not RSS): the parse-memory tests reset it and read the peak. (stb_vorbis allocates with malloc and is not counted; parse() does not use it.)
static size_t g_liveNew = 0, g_peakNew = 0;
void* operator new(size_t n) { void* p = std::malloc(n + 16); if (!p) throw std::bad_alloc(); *(size_t*)p = n; g_liveNew += n; if (g_liveNew > g_peakNew) g_peakNew = g_liveNew; return (char*)p + 16; }
void* operator new[](size_t n) { return operator new(n); }
void operator delete(void* q) noexcept { if (!q) return; char* p = (char*)q - 16; g_liveNew -= *(size_t*)p; std::free(p); }
void operator delete[](void* q) noexcept { operator delete(q); }
void operator delete(void* q, size_t) noexcept { operator delete(q); }
void operator delete[](void* q, size_t) noexcept { operator delete(q); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return operator new(n); } catch (...) { return nullptr; } }   // (the sanitizer runtimes do not route these through operator new(size_t): all forms are replaced, or a pointer from one is freed by another)
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return operator new(n); } catch (...) { return nullptr; } }
void operator delete(void* q, const std::nothrow_t&) noexcept { operator delete(q); }
void operator delete[](void* q, const std::nothrow_t&) noexcept { operator delete(q); }

using namespace thunder;
using namespace thunder::sf;

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static bool readFile(const std::string& p, std::string* out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::ostringstream s; s << f.rdbuf(); *out = s.str(); return true; }
static bool writeFile(const std::string& p, const void* d, size_t n) { std::ofstream f(p, std::ios::binary); if (!f) return false; f.write((const char*)d, (std::streamsize)n); return (bool)f; }
static uint64_t fnv(const void* d, size_t n) { const uint8_t* p = (const uint8_t*)d; uint64_t h = 0xcbf29ce484222325ull; for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 0x100000001b3ull; } return h; }

// Node's Math.* is the reference only where Node itself matches the committed goldens (Linux x86-64): on macOS arm64 V8 fuses multiply-adds inside Math.log10 (the same reason the libm tests use a committed golden). Everywhere the native
// results must equal the COMMITTED hash of the Linux x86-64 Node (kSfMathGolden); the comparison with the local Node stays, and is fatal on the reference platform only.
#if defined(__linux__) && (defined(__x86_64__) || defined(_M_X64))
static const bool kNodeIsReference = true;
#else
static const bool kNodeIsReference = false;
#endif
static bool mathGolden(const char* id, const void* d, size_t doubles) {
  for (int i = 0; i < kSfMathGoldenN; i++) if (std::strcmp(kSfMathGolden[i].id, id) == 0) { const uint64_t h = fnv(d, doubles * 8); const bool ok = h == kSfMathGolden[i].hash && (int)doubles == kSfMathGolden[i].doubles;
      CHECK(ok, "%s: native hash %016llx, committed (Node 22, Linux x86-64) %016llx", id, (unsigned long long)h, kSfMathGolden[i].hash); std::printf("%s: %zu doubles, native hash %s the committed Node reference\n", id, doubles, ok ? "equals" : "DIFFERS from"); return ok; }
  CHECK(false, "no committed math golden for %s", id); return false; }

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
static void decodeInto(const Font& f, const std::set<uint32_t>& idx, PresetPcm& pcm, double ctxRate = 0, bool pageTail = false) {
  DecodeOpts dopt; dopt.ctxRate = ctxRate; dopt.pageTail = pageTail;
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
    if (kNodeIsReference) CHECK(bad == 0, "sf_log10 differs from Math.log10 in %zu of %zu inputs", bad, n); else if (bad) std::printf("Math.log10: %zu of %zu inputs differ from THIS platform's Node (expected off Linux x86-64: V8 fuses multiply-adds); the committed reference decides below\n", bad, n);
    std::printf("Math.log10: %zu inputs bit-identical\n", n - bad);
    std::vector<double> lb; { uint32_t q = 9001; auto nx = [&]() { q = q * 1664525u + 1013904223u; return q; }; for (int i = 0; i < 30000; i++) { uint32_t hi = nx() & 0x7fffffffu; const uint32_t lo = nx(); if ((hi >> 20) == 0x7ff) hi -= 0x00100000u; const uint64_t b = ((uint64_t)hi << 32) | lo; double x; std::memcpy(&x, &b, 8); lb.push_back(sf_log10(x)); } }
    mathGolden("log10b", lb.data(), lb.size()); }
  { std::vector<char> out; CHECK(refBin(dir, "src", &out), "no sfSrc reference"); std::vector<double> mine; uint32_t s = 777; auto rnd = [&]() { s = s * 1664525u + 1013904223u; return s; };
    for (int i = 0; i < 4000; i++) { const unsigned code = i < 2000 ? (rnd() & 0xffff) : (((rnd() & 0x3) << 10) | (rnd() & 0x3ff)); const double kv[4][2] = {{60, 100}, {0, 1}, {127, 127}, {37, 64}};
      for (auto& q : kv) for (int am = 0; am < 2; am++) mine.push_back(sf_src(code, q[0], q[1], am != 0)); }
    const bool srcSame = out.size() == mine.size() * 8 && std::memcmp(out.data(), mine.data(), out.size()) == 0; if (kNodeIsReference) CHECK(srcSame, "sfSrc differs from the JS (%zu values)", mine.size()); std::printf("sfSrc: %zu values %s the local Node\n", mine.size(), srcSame ? "bit-identical to" : "DIFFER from");
    mathGolden("src", mine.data(), mine.size()); }
  // ---- sfVoice, sfPick
  for (const char* fk : {"gm", "syn"}) {
    const Font& f = R.fonts[fk].f; std::vector<double> mine; const bool gm = std::string(fk) == "gm";
    std::vector<std::pair<double, double>> KV = {{60, 100}, {21, 1}, {127, 127}, {77, 64}}; if (!gm) { KV.push_back({0, 1}); KV.push_back({59, 91}); KV.push_back({100, 127}); }
    for (const Preset& p : f.presets) for (uint32_t zi = 0; zi < p.nz; zi++) for (auto& q : KV) voiceRecs(f, q.first, q.second, f.zones[p.z0 + zi], mine);
    CHECK(refBin(dir, std::string("voice_") + fk, &ref), "no sfVoice reference");
    const bool same = ref.size() == mine.size() * 8 && std::memcmp(ref.data(), mine.data(), ref.size()) == 0;
    if (!same && ref.size() == mine.size() * 8) { size_t i = 0; while (std::memcmp((const double*)ref.data() + i, &mine[i], 8) == 0) i++; std::printf("  first difference at record %zu field %zu: native %.17g js %.17g\n", i / 26, i % 26, mine[i], ((const double*)ref.data())[i]); }
    if (kNodeIsReference || !gm) { CHECK(same, "sfVoice (%s) differs from the JS", fk); }
    if (gm) mathGolden("voice_gm", mine.data(), mine.size());
    std::printf("sfVoice %s: %zu zone x (key, velocity) records, %zu numbers: %s\n", fk, mine.size() / 26, mine.size(), same ? "bit-identical" : "DIFFERS");
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
    std::string b; readFile(dir + "/" + std::string(s.str("file")), &b); DecodeOpts dopt2 = dopt; dopt2.pageTail = true; Pcm p, p2; decode_sample(fb.f, idx, dopt, p); decode_sample(fb.f, idx, dopt2, p2);
    const size_t nc = b.size() / 4; const float* c = (const float*)b.data(); const size_t m = std::min(nc, p.d.size()), m2 = std::min(nc, p2.d.size());
    std::printf("decode sample %4u (file %5g Hz): chrome %7zu frames | native %7zu frames, first %7zu differ %6.1f dB | pageTail %7zu frames, %6.1f dB\n", idx, s.num("hdrSr"), nc, p.d.size(), m, m ? dbDiff(p.d.data(), c, m) : 0.0, p2.d.size(), m2 ? dbDiff(p2.d.data(), c, m2) : 0.0);
  }
    Json Rn = R.cases.arr("renders"); Json RR = M.arr("renders"); double worst = -1e9, worstT = -1e9;
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
    // the same render with `pageTail` (Chrome's loss of the last frame of one-page streams reproduced): what is left is the resampler and rounding
    PresetPcm tail; decodeInto(fb.f, cs.idx, tail, ctxRate, true); size_t cutFrames = 0; for (uint32_t ix : cs.idx) cutFrames += pcm.byIndex[ix].d.size() - tail.byIndex[ix].d.size();
    std::vector<float> out2(sf_render_bound(fb.f, P, sr, hold)); const size_t n2 = sf_render(fb.f, tail, P, sr, hold, out2.data(), out2.size()); const double dbT = n2 == nc ? dbDiff(out2.data(), (const float*)b.data(), std::min(n2, nc)) : 0; worstT = std::max(worstT, dbT);
    if (n2 != nc) std::printf("render %-20s: with pageTail the length is %zu, chrome %zu\n", id.c_str(), n2, nc);
    std::printf("render %-20s: native vs the page's sfRender in Chrome: %7.1f dB; with pageTail (%zu frames fewer): %7.1f dB\n", id.c_str(), db, cutFrames, dbT);
  }
  std::printf("worst render difference %.1f dB (with pageTail: %.1f dB)\n", worst, worstT);
  return 0;
}

// ---- unit checks that need no Node: a font built here (RIFF by hand) for the places the JS comparison cannot reach (extreme values, ties, tiny loops), and the Chrome page's own loop numbers for the SF3 rescale
struct Riff {
  std::vector<uint8_t> b;
  void u16(uint16_t v) { b.push_back((uint8_t)v); b.push_back((uint8_t)(v >> 8)); } void u32(uint32_t v) { u16((uint16_t)v); u16((uint16_t)(v >> 16)); }
  void tag(const char* t) { for (int i = 0; i < 4; i++) b.push_back((uint8_t)t[i]); } void name(const char* n) { for (int i = 0; i < 20; i++) b.push_back(i < (int)std::strlen(n) ? (uint8_t)n[i] : 0); }
  static std::vector<uint8_t> chunk(const char* t, const std::vector<uint8_t>& d) { Riff r; r.tag(t); r.u32((uint32_t)d.size()); r.b.insert(r.b.end(), d.begin(), d.end()); if (d.size() & 1) r.b.push_back(0); return r.b; }
};
struct MiniPreset { int bank, prog; };
static std::vector<uint8_t> miniSf2(const std::vector<MiniPreset>& pr, int gen29, uint32_t ls, uint32_t le, int mode, uint32_t endFrame = 100) {
  Riff smpl; for (int i = 0; i < 100; i++) smpl.u16((uint16_t)(int16_t)(i * 300 - 15000));
  Riff shdr; shdr.name("S"); shdr.u32(0); shdr.u32(endFrame); shdr.u32(ls); shdr.u32(le); shdr.u32(44100); shdr.b.push_back(60); shdr.b.push_back(0); shdr.u16(0); shdr.u16(1);
  shdr.name("EOS"); for (int i = 0; i < 5; i++) shdr.u32(0); shdr.u16(0); shdr.u16(0); shdr.u16(0);
  Riff inst; inst.name("I"); inst.u16(0); inst.name("EOI"); inst.u16(1);
  Riff ibag; ibag.u16(0); ibag.u16(0); ibag.u16(gen29 ? 3 : 2); ibag.u16(0);
  Riff igen; if (gen29) { igen.u16(29); igen.u16((uint16_t)gen29); } igen.u16(54); igen.u16((uint16_t)mode); igen.u16(53); igen.u16(0); igen.u16(0); igen.u16(0);
  Riff imod; for (int i = 0; i < 5; i++) imod.u16(0);
  Riff phdr, pbag, pgen, pmod; int k = 0;
  for (const MiniPreset& m : pr) { char nm[20]; std::snprintf(nm, 20, "P%d", k); phdr.name(nm); phdr.u16((uint16_t)m.prog); phdr.u16((uint16_t)m.bank); phdr.u16((uint16_t)k); phdr.u32(0); phdr.u32(0); phdr.u32(0); pbag.u16((uint16_t)k); pbag.u16(0); pgen.u16(41); pgen.u16(0); k++; }
  phdr.name("EOP"); phdr.u16(0); phdr.u16(0); phdr.u16((uint16_t)k); phdr.u32(0); phdr.u32(0); phdr.u32(0); pbag.u16((uint16_t)k); pbag.u16(0); pgen.u16(0); pgen.u16(0); for (int i = 0; i < 5; i++) pmod.u16(0);
  Riff sdta; sdta.tag("sdta"); { auto c = Riff::chunk("smpl", smpl.b); sdta.b.insert(sdta.b.end(), c.begin(), c.end()); }
  Riff pdta; pdta.tag("pdta"); for (auto& pr2 : std::vector<std::pair<const char*, const Riff*>>{{"phdr", &phdr}, {"pbag", &pbag}, {"pmod", &pmod}, {"pgen", &pgen}, {"inst", &inst}, {"ibag", &ibag}, {"imod", &imod}, {"igen", &igen}, {"shdr", &shdr}}) { auto c = Riff::chunk(pr2.first, pr2.second->b); pdta.b.insert(pdta.b.end(), c.begin(), c.end()); }
  Riff all; all.tag("sfbk"); { auto a = Riff::chunk("LIST", sdta.b); all.b.insert(all.b.end(), a.begin(), a.end()); auto c = Riff::chunk("LIST", pdta.b); all.b.insert(all.b.end(), c.begin(), c.end()); }
  return Riff::chunk("RIFF", all.b);
}

// A font of nPz preset zones x nIz instrument zones whose key ranges never meet (no zone results): only the pair walk costs anything.
static std::vector<uint8_t> bigSf2(uint32_t nPz, uint32_t nIz) {
  Riff smpl; for (int i = 0; i < 100; i++) smpl.u16(0);
  Riff shdr; shdr.name("S"); shdr.u32(0); shdr.u32(100); shdr.u32(10); shdr.u32(20); shdr.u32(44100); shdr.b.push_back(60); shdr.b.push_back(0); shdr.u16(0); shdr.u16(1);
  shdr.name("EOS"); for (int i = 0; i < 5; i++) shdr.u32(0); shdr.u16(0); shdr.u16(0); shdr.u16(0);
  Riff inst; inst.name("I"); inst.u16(0); inst.name("EOI"); inst.u16((uint16_t)std::min<uint32_t>(nIz, 65535));
  Riff ibag, igen, imod; for (uint32_t i = 0; i < nIz; i++) { ibag.u16((uint16_t)(2 * i)); ibag.u16(0); igen.u16(43); igen.u16(64 | (127 << 8)); igen.u16(53); igen.u16(0); } ibag.u16((uint16_t)(2 * nIz)); ibag.u16(0); igen.u16(0); igen.u16(0); for (int i = 0; i < 5; i++) imod.u16(0);
  Riff phdr, pbag, pgen, pmod; phdr.name("P"); phdr.u16(0); phdr.u16(0); phdr.u16(0); phdr.u32(0); phdr.u32(0); phdr.u32(0); phdr.name("EOP"); phdr.u16(0); phdr.u16(0); phdr.u16((uint16_t)std::min<uint32_t>(nPz, 65535)); phdr.u32(0); phdr.u32(0); phdr.u32(0);
  for (uint32_t i = 0; i < nPz; i++) { pbag.u16((uint16_t)(2 * i)); pbag.u16(0); pgen.u16(43); pgen.u16(0 | (63 << 8)); pgen.u16(41); pgen.u16(0); } pbag.u16((uint16_t)(2 * nPz)); pbag.u16(0); pgen.u16(0); pgen.u16(0); for (int i = 0; i < 5; i++) pmod.u16(0);
  Riff sdta; sdta.tag("sdta"); { auto c = Riff::chunk("smpl", smpl.b); sdta.b.insert(sdta.b.end(), c.begin(), c.end()); }
  Riff pdta; pdta.tag("pdta"); for (auto& pr2 : std::vector<std::pair<const char*, const Riff*>>{{"phdr", &phdr}, {"pbag", &pbag}, {"pmod", &pmod}, {"pgen", &pgen}, {"inst", &inst}, {"ibag", &ibag}, {"imod", &imod}, {"igen", &igen}, {"shdr", &shdr}}) { auto c = Riff::chunk(pr2.first, pr2.second->b); pdta.b.insert(pdta.b.end(), c.begin(), c.end()); }
  Riff all; all.tag("sfbk"); { auto a = Riff::chunk("LIST", sdta.b); all.b.insert(all.b.end(), a.begin(), a.end()); auto c = Riff::chunk("LIST", pdta.b); all.b.insert(all.b.end(), c.begin(), c.end()); }
  return Riff::chunk("RIFF", all.b);
}
static double nowSec() { return (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1e6; }
// ---- U32 follow-up (docs/port/U32-NOTES.md "Follow-up: decode work budget"): hostile fonts built from the GM font's own Ogg streams, and the counted peak of parse()
static std::vector<uint8_t> riffFont(const std::vector<uint8_t>& smpl, const Riff& phdr, const Riff& pbag, const Riff& pgen, const Riff& pmod, const Riff& inst, const Riff& ibag, const Riff& igen, const Riff& imod, const Riff& shdr) {
  Riff sdta; sdta.tag("sdta"); { auto c = Riff::chunk("smpl", smpl); sdta.b.insert(sdta.b.end(), c.begin(), c.end()); }
  Riff pdta; pdta.tag("pdta"); for (auto& pr2 : std::vector<std::pair<const char*, const Riff*>>{{"phdr", &phdr}, {"pbag", &pbag}, {"pmod", &pmod}, {"pgen", &pgen}, {"inst", &inst}, {"ibag", &ibag}, {"imod", &imod}, {"igen", &igen}, {"shdr", &shdr}}) { auto c = Riff::chunk(pr2.first, pr2.second->b); pdta.b.insert(pdta.b.end(), c.begin(), c.end()); }
  Riff all; all.tag("sfbk"); { auto a = Riff::chunk("LIST", sdta.b); all.b.insert(all.b.end(), a.begin(), a.end()); auto c = Riff::chunk("LIST", pdta.b); all.b.insert(all.b.end(), c.begin(), c.end()); }
  return Riff::chunk("RIFF", all.b);
}
// An SF3 font with ONE preset whose one instrument has nHdr zones, zone i playing sample header i; header i names byte range ranges[i % ranges.size()] of `smpl` (so headers alias streams as often as the caller wants).
static std::vector<uint8_t> aliasSf3(const std::vector<uint8_t>& smpl, const std::vector<std::pair<uint32_t, uint32_t>>& ranges, uint32_t nHdr) {
  Riff shdr; for (uint32_t i = 0; i < nHdr; i++) { const auto& r = ranges[i % ranges.size()]; shdr.name("S"); shdr.u32(r.first); shdr.u32(r.second); shdr.u32(0); shdr.u32(100); shdr.u32(44100); shdr.b.push_back(60); shdr.b.push_back(0); shdr.u16(0); shdr.u16(0x11); }
  shdr.name("EOS"); for (int i = 0; i < 5; i++) shdr.u32(0); shdr.u16(0); shdr.u16(0); shdr.u16(0);
  Riff inst; inst.name("I"); inst.u16(0); inst.name("EOI"); inst.u16((uint16_t)nHdr);
  Riff ibag, igen, imod; for (uint32_t i = 0; i < nHdr; i++) { ibag.u16((uint16_t)(2 * i)); ibag.u16(0); igen.u16(43); igen.u16(0 | (127 << 8)); igen.u16(53); igen.u16((uint16_t)i); } ibag.u16((uint16_t)(2 * nHdr)); ibag.u16(0); igen.u16(0); igen.u16(0); for (int i = 0; i < 5; i++) imod.u16(0);
  Riff phdr, pbag, pgen, pmod; phdr.name("P"); phdr.u16(0); phdr.u16(0); phdr.u16(0); phdr.u32(0); phdr.u32(0); phdr.u32(0); phdr.name("EOP"); phdr.u16(0); phdr.u16(0); phdr.u16(1); phdr.u32(0); phdr.u32(0); phdr.u32(0);
  pbag.u16(0); pbag.u16(0); pbag.u16(1); pbag.u16(0); pgen.u16(41); pgen.u16(0); pgen.u16(0); pgen.u16(0); for (int i = 0; i < 5; i++) pmod.u16(0);
  return riffFont(smpl, phdr, pbag, pgen, pmod, inst, ibag, igen, imod, shdr);
}
// A SF2 font of nPz preset zones x nIz instrument zones that all overlap, each zone with about nGen generators (parse memory: every pair makes a zone holding its own copy of the merged generators)
static std::vector<uint8_t> genSf2(uint32_t nPz, uint32_t nIz, int nGen) {
  std::vector<int> ops; for (int o = 5; o <= 58; o++) { const int bad[] = {12, 41, 43, 44, 45, 46, 47, 50, 53, 54, 57, 58}; if (std::find(std::begin(bad), std::end(bad), o) == std::end(bad)) ops.push_back(o); }   // 42 plain ops
  std::vector<int> iops = ops; for (int o : {0, 1, 2, 3, 4, 12, 45, 50}) iops.push_back(o);
  Riff smpl; for (int i = 0; i < 100; i++) smpl.u16(0);
  Riff shdr; shdr.name("S"); shdr.u32(0); shdr.u32(100); shdr.u32(10); shdr.u32(20); shdr.u32(44100); shdr.b.push_back(60); shdr.b.push_back(0); shdr.u16(0); shdr.u16(1);
  shdr.name("EOS"); for (int i = 0; i < 5; i++) shdr.u32(0); shdr.u16(0); shdr.u16(0); shdr.u16(0);
  Riff inst; inst.name("I"); inst.u16(0); inst.name("EOI"); inst.u16((uint16_t)nIz);
  Riff ibag, igen, imod; const int ni = std::min<int>(nGen, (int)iops.size()), np = std::min<int>(nGen, (int)ops.size()); uint32_t gi = 0;
  for (uint32_t i = 0; i < nIz; i++) { ibag.u16((uint16_t)gi); ibag.u16(0); for (int k = 0; k < ni; k++) { igen.u16((uint16_t)iops[k]); igen.u16((uint16_t)(k + 1)); } igen.u16(53); igen.u16(0); gi += (uint32_t)ni + 1; } ibag.u16((uint16_t)gi); ibag.u16(0); igen.u16(0); igen.u16(0); for (int i = 0; i < 5; i++) imod.u16(0);
  Riff phdr, pbag, pgen, pmod; phdr.name("P"); phdr.u16(0); phdr.u16(0); phdr.u16(0); phdr.u32(0); phdr.u32(0); phdr.u32(0); phdr.name("EOP"); phdr.u16(0); phdr.u16(0); phdr.u16((uint16_t)nPz); phdr.u32(0); phdr.u32(0); phdr.u32(0);
  gi = 0; for (uint32_t i = 0; i < nPz; i++) { pbag.u16((uint16_t)gi); pbag.u16(0); for (int k = 0; k < np; k++) { pgen.u16((uint16_t)ops[k]); pgen.u16((uint16_t)(k + 1)); } pgen.u16(41); pgen.u16(0); gi += (uint32_t)np + 1; } pbag.u16((uint16_t)gi); pbag.u16(0); pgen.u16(0); pgen.u16(0); for (int i = 0; i < 5; i++) pmod.u16(0);
  return riffFont(std::vector<uint8_t>(smpl.b), phdr, pbag, pgen, pmod, inst, ibag, igen, imod, shdr);
}
// the granule position of an Ogg stream's LAST page set to 0 (stb_vorbis and libvorbis do not check the page CRC, and this tool does not fix it: a decoder that did would refuse the stream even earlier)
static bool zeroLastGranule(std::vector<uint8_t>& t) { size_t last = (size_t)-1; for (size_t pos = 0; pos + 27 <= t.size() && std::memcmp(&t[pos], "OggS", 4) == 0;) { last = pos; size_t segs = t[pos + 26], body = 0; if (pos + 27 + segs > t.size()) break; for (size_t q = 0; q < segs; q++) body += t[pos + 27 + q]; pos += 27 + segs + body; }
  if (last == (size_t)-1) return false; std::memset(&t[last + 6], 0, 8); return true; }

static int doUnit() {
  // A. presets of equal bank / program keep the file's order (the JS's sort is stable)
  { std::vector<MiniPreset> v(40, MiniPreset{0, 0}); auto b = miniSf2(v, 0, 0, 0, 0); Font f; CHECK(parse(b.data(), b.size(), f) == kParseOk, "mini font parses");
    bool inOrder = f.presets.size() == 40; for (size_t i = 0; i < f.presets.size() && inOrder; i++) { char nm[20]; std::snprintf(nm, 20, "P%zu", i); inOrder = std::strcmp(f.presets[i].name, nm) == 0; }
    CHECK(inOrder, "equal presets were reordered"); std::printf("unit: 40 presets with the same bank and program keep their order: %s\n", inOrder ? "yes" : "NO"); }
  // B. sfPreset: exact, then the same program in bank 0 / 128, then any preset of that bank, then the first
  { auto b = miniSf2({{0, 5}, {128, 0}, {128, 3}}, 0, 0, 0, 0); Font f; CHECK(parse(b.data(), b.size(), f) == kParseOk, "mini font parses");
    const Preset* a = sf_preset(f, 128, 9); const Preset* c = sf_preset(f, 129, 3); const Preset* d = sf_preset(f, 0, 9); const Preset* e = sf_preset(f, 128, 3); const Preset* g = sf_preset(f, 77, 5);
    CHECK(a && a->bank == 128 && a->prog == 0, "bank 128, program 9: expected the first preset of bank 128"); CHECK(c && c->bank == 128 && c->prog == 3, "bank 129, program 3: expected 128/3");
    CHECK(d && d->bank == 0 && d->prog == 5, "bank 0, program 9: expected 0/5"); CHECK(e && e->bank == 128 && e->prog == 3, "exact 128/3"); CHECK(g && g->bank == 0 && g->prog == 5, "bank 77, program 5: expected 0/5");
    std::printf("unit: sfPreset fallbacks (5 lookups) checked\n"); }
  // C. the modulation envelope's sustain is clamped to 1000 permille, whatever the font says
  { auto b = miniSf2({{0, 0}}, 1500, 0, 0, 0); Font f; CHECK(parse(b.data(), b.size(), f) == kParseOk, "mini font parses"); VoiceParams vp; sf_voice(f, f.zones[0], 60, 100, vp);
    CHECK(vp.m.sus == 1.0, "mod-env sustain 1500 -> %g (expected 1)", vp.m.sus); std::printf("unit: modulation-envelope sustain 1500 is clamped to %g\n", vp.m.sus); }
  // D. a five-frame loop still loops (the render's `le > ls + 4`): the note sustains long after the 100-frame sample has ended
  { auto b = miniSf2({{0, 0}}, 0, 50, 55, 1); Font f; CHECK(parse(b.data(), b.size(), f) == kParseOk, "mini font parses"); PresetPcm pcm; prepare_preset(f, sf_preset(f, 0, 0), DecodeOpts(), pcm);
    PadParams P; const size_t N = sf_render_bound(f, P, 44100, 1.0); std::vector<float> out(N); const size_t n = sf_render(f, pcm, P, 44100, 1.0, out.data(), out.size());
    double late = 0; for (size_t i = 22050; i < 30000 && i < n; i++) late = std::max(late, (double)std::fabs(out[i]));
    CHECK(n == N && late > 0.01, "a loop of 5 frames did not sustain (late peak %g)", late); std::printf("unit: a 5-frame loop sustains (peak at 0.5 s: %.3f)\n", late); }
  // E. SF3 loop points rescaled to the context rate: the numbers the page's own sfSample gave in Chrome (native/tools/sf-chrome.mjs, context 48 kHz)
  { std::string gmPath = std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3"; std::string gb; if (!readFile(gmPath, &gb)) { CHECK(false, "no GM font"); } else {
      Font f; CHECK(parse((const uint8_t*)gb.data(), gb.size(), f) == kParseOk, "GM font parses");
      const struct { uint32_t idx; double ls, le; } W[] = {{116, 261983, 350263}, {140, 552408, 644779}, {1043, 139222, 140323}, {948, 9, 13555}, {906, 9, 22457}, {730, 57210, 103476}, {183, 256, 2202}, {859, 4763, 5036}, {213, 54818, 167720}, {1075, 67684, 68073}};
      int good = 0; DecodeOpts o; o.ctxRate = 48000;
      for (auto& w : W) { Pcm p; decode_sample(f, w.idx, o, p); const bool ok = p.ls == w.ls && p.le == w.le && p.sr == 48000; if (!ok) std::printf("  sample %u: loop %g..%g, the page: %g..%g\n", w.idx, p.ls, p.le, w.ls, w.le); CHECK(ok, "sample %u loop points differ from the page's", w.idx); good += ok; }
      std::printf("unit: %d of %zu SF3 samples have the page's loop points at a 48 kHz context\n", good, sizeof W / sizeof W[0]); } }
  // G. frame counts: the default keeps every frame up to the stream's last granule position (what libvorbis returns: measured with libsndfile / libvorbis for these six streams); `pageTail` reproduces Chrome's decodeAudioData
  { std::string gb; if (readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3", &gb)) { Font f; CHECK(parse((const uint8_t*)gb.data(), gb.size(), f) == kParseOk, "GM font parses");
      const struct { uint32_t idx; size_t granule, chrome; } W[] = {{183, 2297, 2176}, {948, 12462, 12352}, {906, 20640, 20512}, {859, 4633, 4505}, {1075, 62568, 62568}, {0, 48, 1}};
      int good = 0; for (auto& w : W) { Pcm a, c; DecodeOpts o; decode_sample(f, w.idx, o, a); o.pageTail = true; decode_sample(f, w.idx, o, c); const bool ok = a.d.size() == w.granule && c.d.size() == w.chrome && (w.idx != 0 || (!c.ok && !a.ok == false));
        if (!ok) std::printf("  sample %u: default %zu (granule %zu), pageTail %zu (Chrome %zu)\n", w.idx, a.d.size(), w.granule, c.d.size(), w.chrome); CHECK(ok, "frame counts of sample %u", w.idx); good += ok; }
      std::printf("unit: %d of 6 streams have the libvorbis frame count by default and Chrome's with pageTail\n", good); } }
  // H. hostile fonts: memory, budgets, sample rates, parse time (each of these once cost 1.5 GB, an unbounded resample or 8 s of CPU)
  { // H1: a header that claims 2^25 frames in a 200-byte sample chunk decodes to the frames that are there; a header that overshoots by one frame likewise
    auto b = miniSf2({{0, 0}}, 0, 50, 55, 1, 1u << 25); Font f; CHECK(parse(b.data(), b.size(), f) == kParseOk, "claim font parses"); PresetPcm pcm; const PrepareStatus st = prepare_preset(f, sf_preset(f, 0, 0), DecodeOpts(), pcm);
    CHECK(st == kPrepOk && pcm.bytes == 400 && pcm.byIndex[0].d.size() == 100, "a sample claiming 2^25 frames: %llu bytes", (unsigned long long)pcm.bytes);
    auto b2 = miniSf2({{0, 0}}, 0, 50, 55, 1, 101); Font f2; parse(b2.data(), b2.size(), f2); PresetPcm p2; prepare_preset(f2, sf_preset(f2, 0, 0), DecodeOpts(), p2);
    const double lastWant = (double)(int16_t)(99 * 300 - 15000) / 32768; CHECK(p2.byIndex[0].d.size() == 100 && p2.byIndex[0].d[99] == (float)lastWant && p2.byIndex[0].d[0] == (float)((double)(int16_t)-15000 / 32768), "the last frame of the smpl chunk is read, none past it (%zu frames)", p2.byIndex[0].d.size());
    std::printf("unit: a sample header claiming 2^25 frames in a 200-byte chunk costs %llu bytes; the last frame is read, none past it\n", (unsigned long long)pcm.bytes); }
  { // H2: budgets: the preset's, then the font's (a host counter)
    auto b = miniSf2({{0, 0}}, 0, 50, 55, 1); Font f; parse(b.data(), b.size(), f); const Preset* pr = sf_preset(f, 0, 0); DecodeOpts o; o.presetBytes = 200; PresetPcm p1; const PrepareStatus s1 = prepare_preset(f, pr, o, p1);
    std::vector<float> tmp(sf_render_bound(f, PadParams(), 44100, 1.0)); PadParams P;
    CHECK(s1 == kPrepOverPresetBudget && p1.bytes == 0 && !p1.get(0) && sf_render(f, p1, P, 44100, 1.0, tmp.data(), tmp.size()) == 0, "preset budget: status %d bytes %llu", (int)s1, (unsigned long long)p1.bytes);
    uint64_t used = 700; DecodeOpts o2; o2.fontBytes = 1000; o2.fontUsed = &used; PresetPcm p2; const PrepareStatus s2 = prepare_preset(f, pr, o2, p2); CHECK(s2 == kPrepOverFontBudget && used == 700 && p2.bytes == 0, "font budget: status %d used %llu", (int)s2, (unsigned long long)used);
    used = 0; PresetPcm p3; const PrepareStatus s3 = prepare_preset(f, pr, o2, p3); CHECK(s3 == kPrepOk && used == 400 && p3.bytes == 400, "font counter: status %d used %llu", (int)s3, (unsigned long long)used);
    std::printf("unit: preset budget refuses (status %d, nothing kept, render 0); font budget refuses at 700+400 > 1000 (status %d) and counts a success (%llu)\n", (int)s1, (int)s2, (unsigned long long)used); }
  { std::string gb; if (readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3", &gb)) { Font f; parse((const uint8_t*)gb.data(), gb.size(), f);
      // H3: an SF3 stream is bounded by the budget at decode, resampled or not (sample 116: 321867 frames = 1.29 MB at its own rate, 1.40 MB at 48 kHz)
      Pcm a, b2, c, d2; DecodeOpts o; o.maxBytes = 100000; decode_sample(f, 116, o, a); o.maxBytes = 1300000; o.ctxRate = 48000; decode_sample(f, 116, o, b2); o.maxBytes = 2000000; decode_sample(f, 116, o, c); o.ctxRate = 0; o.maxBytes = 1300000; decode_sample(f, 116, o, d2);
      CHECK(a.tooBig && !a.ok && a.d.size() == 1 && b2.tooBig && !b2.ok && c.ok && c.d.size() == 350331 && d2.ok && d2.d.size() == 321867, "SF3 budget: %d %d / %d %zu / %d", a.tooBig, b2.tooBig, c.ok, c.d.size(), d2.ok);
      std::printf("unit: an SF3 sample over its byte budget is refused before and after resampling (1.29 MB native, 1.40 MB at 48 kHz)\n");
      // H4: the stream's own sample rate outside 4000..192000 Hz is refused (a silent frame), whatever the context rate
      const SampleHdr& h0 = f.samples[183]; std::vector<uint8_t> raw(f.smpl + h0.start, f.smpl + h0.end); size_t j = 0; for (; j + 7 < raw.size(); j++) if (raw[j] == 1 && std::memcmp(&raw[j + 1], "vorbis", 6) == 0) break;
      int refused = 0, accepted = 0; const uint32_t R[] = {0, 1, 7, 3999, 4000, 44100, 192000, 192001, 0x7fffffffu, 0xffffffffu};
      for (uint32_t rate : R) { std::vector<uint8_t> t = raw; std::memcpy(&t[j + 12], &rate, 4); Font g; g.sf3 = true; g.smpl = t.data(); g.smplN = t.size(); SampleHdr h = h0; h.start = 0; h.end = (uint32_t)t.size(); g.samples.push_back(h);
        for (double ctx : {0.0, 48000.0}) { Pcm p; DecodeOpts oo; oo.ctxRate = ctx; const double t0 = nowSec(); decode_sample(g, 0, oo, p); const double dt = nowSec() - t0; const bool sane = rate >= 4000 && rate <= 192000;
          CHECK(p.ok == sane && (sane || (p.d.size() == 1 && p.sr == 48000)) && dt < 5.0, "stream rate %u ctx %g: ok %d frames %zu sr %g (%.2f s)", rate, ctx, p.ok, p.d.size(), p.sr, dt); (sane ? accepted : refused)++; } }
      std::printf("unit: stream rates 0, 1, 7, 3999, 192001, 2^31-1, 2^32-1 Hz refused, 4000, 44100, 192000 accepted (%d, %d decodes)\n", refused, accepted);
      // H4b: the last page's granule decides the length, shortening only (libvorbis: granules 0 / 1 / 100 give 0 / 1 / 100 frames on this stream; a granule above the decoded 2304 changes nothing)
      { std::vector<uint8_t> t = raw; size_t last = 0; for (size_t q = 0; q + 27 < t.size(); q++) if (std::memcmp(&t[q], "OggS", 4) == 0) last = q;
        const struct { int64_t g; size_t want; } GW[] = {{1, 1}, {100, 100}, {2296, 2296}, {2297, 2297}, {2298, 2298}, {5000, 2304}, {(int64_t)1 << 40, 2304}, {0, 0}}; int okc = 0;
        for (auto& gw : GW) { std::vector<uint8_t> u = t; std::memcpy(&u[last + 6], &gw.g, 8); Font g; g.sf3 = true; g.smpl = u.data(); g.smplN = u.size(); SampleHdr h = h0; h.start = 0; h.end = (uint32_t)u.size(); g.samples.push_back(h); Pcm p; decode_sample(g, 0, DecodeOpts(), p);
          const bool ok = gw.want == 0 ? !p.ok : (p.ok && p.d.size() == gw.want); CHECK(ok, "last granule %lld: %zu frames (want %zu)", (long long)gw.g, p.d.size(), gw.want); okc += ok; }
        std::printf("unit: patched last-page granules 0, 1, 100, 2296, 2297, 2298, 5000, 2^40 on sample 183: %d of 8 give the libvorbis frame count\n", okc); }
      // G2: every SF3 sample of the font has exactly its last page's granule position as its frame count (the Vorbis rule, libvorbis's count), except streams whose decode ends before it
      size_t checked = 0, equal = 0, shorter = 0, bad = 0; const uint32_t named[] = {234, 235, 704, 183, 859, 948, 8, 9, 10}; const size_t namedGr[] = {37067, 33493, 60416, 2297, 4633, 12462, 79504, 79504, 107778}; int namedOk = 0;
      for (uint32_t i = 0; i < f.samples.size(); i++) { const SampleHdr& h = f.samples[i]; if (!(h.type & 0x10) || h.end <= h.start || h.end > f.smplN) continue; const uint8_t* p = f.smpl + h.start; const size_t n = h.end - h.start; int64_t last = -1;
        for (size_t pos = 0; pos + 27 <= n && std::memcmp(p + pos, "OggS", 4) == 0;) { const size_t segs = p[pos + 26]; if (pos + 27 + segs > n) break; size_t body = 0; for (size_t q = 0; q < segs; q++) body += p[pos + 27 + q]; int64_t g; std::memcpy(&g, p + pos + 6, 8); last = g; pos += 27 + segs + body; }
        Pcm pc; decode_sample(f, i, DecodeOpts(), pc); if (last <= 0) { bad++; continue; } checked++;
        if (pc.ok && pc.d.size() == (size_t)last) equal++; else if (pc.ok && pc.d.size() < (size_t)last) shorter++; else { bad++; std::printf("  sample %u: granule %lld, frames %zu (ok %d)\n", i, (long long)last, pc.d.size(), pc.ok); }
        for (size_t q = 0; q < sizeof named / sizeof named[0]; q++) if (named[q] == i && pc.d.size() == namedGr[q]) namedOk++; }
      CHECK(bad == 0 && equal + shorter == checked && namedOk == 9, "granule check: %zu checked, %zu equal, %zu shorter, %zu bad, %d of 9 named samples at their granule", checked, equal, shorter, bad, namedOk);
      std::printf("unit: all %zu SF3 samples with a granule: %zu have exactly their last page's granule as frame count, %zu end before it; samples 234 235 704 183 859 948 8 9 10 at the libvorbis counts: %d of 9\n", checked, equal, shorter, namedOk); } }
  { // H5: a font whose zone pairs run into the millions fails fast (the limit is 20x the GM font's 10027 pairs)
    auto big = bigSf2(30000, 30000); Font f; const double t0 = nowSec(); const ParseStatus st = parse(big.data(), big.size(), f); const double dt = nowSec() - t0;
    CHECK(st == kParseTooBig && dt < 1.0, "30000 x 30000 zones: status %d in %.3f s (%zu bytes)", (int)st, dt, big.size());
    auto ok = bigSf2(400, 400); Font f2; CHECK(parse(ok.data(), ok.size(), f2) == kParseOk && f2.work == 160000, "400 x 400 zones parses (work %llu)", (unsigned long long)f2.work);
    std::printf("unit: 30000 x 30000 zones refused in %.0f ms (%zu-byte file); 400 x 400 (160000 pairs, 16x the GM font) parses\n", dt * 1000, big.size()); }
  { // H6: absurd header rates and context rates are refused (a silent frame), sane ones decode
    int ok = 0, n = 0; const double CR[] = {0, 4000, 44100, 192000};
    for (uint32_t hr : {0u, 1u, 399u, 400u, 44100u, 192000u, 192001u, 0xffffffffu}) { auto b = miniSf2({{0, 0}}, 0, 50, 55, 1); Font f; parse(b.data(), b.size(), f); if (f.samples.empty()) continue; f.samples[0].sr = hr; Pcm p; decode_sample(f, 0, DecodeOpts(), p);
      const bool sane = hr >= 400 && hr <= 192000; n++; CHECK(p.ok == sane && (sane || (p.d.size() == 1 && p.sr == 48000)), "header rate %u: ok %d", hr, p.ok); ok += p.ok == sane; }
    { auto b = miniSf2({{0, 0}}, 0, 50, 55, 1); Font f; parse(b.data(), b.size(), f); for (double c : {-1.0, 1.0, 3999.0, 192001.0, 1e300, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) { Pcm p; DecodeOpts o; o.ctxRate = c; decode_sample(f, 0, o, p); n++; CHECK(!p.ok && p.d.size() == 1, "context rate %g refused", c); ok += !p.ok; }
      for (double c : CR) { Pcm p; DecodeOpts o; o.ctxRate = c; decode_sample(f, 0, o, p); n++; CHECK(p.ok && p.d.size() == 100, "context rate %g accepted", c); ok += p.ok; } }
    std::printf("unit: absurd header rates (0, 1, 399, 192001, 2^32-1) and context rates (negative, 1, 3999, 192001, 1e300, inf, NaN) refused, sane ones accepted: %d of %d\n", ok, n); }
  { // H7: parity mode is OFF by default (the libvorbis-correct decode is the default; Chrome's frame loss and its resampler are opt-in)
    DecodeOpts d; const DecodeOpts pg = DecodeOpts::page(48000);
    CHECK(d.pageTail == false && d.ctxRate == 0 && pg.pageTail == true && pg.ctxRate == 48000, "defaults: pageTail %d ctxRate %g; page(48000): %d %g", d.pageTail, d.ctxRate, pg.pageTail, pg.ctxRate);
    std::printf("unit: DecodeOpts default is pageTail off and ctxRate 0 (own rates); only DecodeOpts::page(rate) turns parity on\n"); }
  { // H8: a loop end beyond the decoded data drops the loop (the page's guard `le <= length`): the note ends with its data; the same loop inside the data sustains
    double tail[2] = {0, 0}; size_t len[2] = {0, 0}; const uint32_t LE[2] = {60, 150};
    for (int c = 0; c < 2; c++) { auto b = miniSf2({{0, 0}}, 0, 10, LE[c], 1); Font f; parse(b.data(), b.size(), f); PresetPcm pcm; prepare_preset(f, sf_preset(f, 0, 0), DecodeOpts(), pcm); PadParams P; const size_t cap = sf_render_bound(f, P, 44100, 1.0); std::vector<float> o(cap + 1);
      len[c] = sf_render(f, pcm, P, 44100, 1.0, o.data(), cap); for (size_t k = len[c] / 2; k < len[c]; k++) tail[c] += (double)o[k] * o[k]; }
    CHECK(len[0] > 1000 && len[1] == len[0] && tail[0] > 1e-6 && tail[1] == 0, "loop inside the data sustains (tail energy %g), loop end beyond it does not (%g), %zu frames", tail[0], tail[1], len[0]);
    std::printf("unit: loop end inside the data: second half of the note has energy %.3g; loop end beyond the data (150 > 100 frames): %.3g (the note ends with its data)\n", tail[0], tail[1]); }
  // R. the resampler against what Chrome 153 made of the same input (native/tests/golden/sf_resampler_chrome.json, native/tools/sf-resampler-ref.mjs)
  { std::string gj; if (!readFile(std::string(THUNDER_ROOT) + "/native/tests/golden/sf_resampler_chrome.json", &gj)) CHECK(false, "no resampler reference"); else { JsonDoc d; CHECK(d.parse(gj), "resampler reference parses"); Json C = d.root().arr("cases"); const size_t N = (size_t)d.root().num("n"); uint32_t seed0 = (uint32_t)d.root().num("seed");
      for (size_t i = 0; i < C.size(); i++) { Json c = C.at(i); std::vector<float> x(N); uint32_t s = seed0; for (size_t k = 0; k < N; k++) { s = s * 1664525u + 1013904223u; x[k] = (float)((double)((int)((s >> 16) & 0x3fff) - 0x2000) / 32768); }
        CHECK(sf_resample(x, c.num("from"), c.num("to")), "resample"); Json O = c.arr("out"); const size_t m = std::min(x.size(), O.size()); double e = 0, pw = 0; for (size_t k = 0; k < m; k++) { const double r = O.at(k).asNum(); e += ((double)x[k] - r) * ((double)x[k] - r); pw += r * r; }
        const double db = 10 * std::log10(e / pw + 1e-30); CHECK(x.size() == O.size() && db < -85, "%g -> %g Hz: %zu vs Chrome's %zu frames, %.1f dB", c.num("from"), c.num("to"), x.size(), O.size(), db); std::printf("unit: resampler %g -> %g Hz vs Chrome: %zu frames (Chrome %zu), %.1f dB\n", c.num("from"), c.num("to"), x.size(), O.size(), db); } } }
  // F. prepare_preset with a key list: only the zones that cover a listed key are decoded, with the same bits as the full preparation
  { auto b = miniSf2({{0, 0}}, 0, 50, 55, 1); Font f; CHECK(parse(b.data(), b.size(), f) == kParseOk, "mini font parses"); uint8_t none[128] = {0}, c4[128] = {0}; c4[60] = 1; PresetPcm all, one, zero;
    prepare_preset(f, sf_preset(f, 0, 0), DecodeOpts(), all); prepare_preset(f, sf_preset(f, 0, 0), DecodeOpts(), one, c4); prepare_preset(f, sf_preset(f, 0, 0), DecodeOpts(), zero, none);
    CHECK(one.bytes == all.bytes && one.bytes > 0 && zero.bytes == 0, "key list: bytes %llu / %llu / %llu", (unsigned long long)one.bytes, (unsigned long long)all.bytes, (unsigned long long)zero.bytes);
    PadParams P; std::vector<float> o1(sf_render_bound(f, P, 44100, 1.0)), o2(o1.size()); const size_t n1 = sf_render(f, all, P, 44100, 1.0, o1.data(), o1.size()), n2 = sf_render(f, one, P, 44100, 1.0, o2.data(), o2.size()), n3 = sf_render(f, zero, P, 44100, 1.0, o2.data(), o2.size());
    CHECK(n1 == n2 && std::memcmp(o1.data(), o2.data(), n1 * 4) == 0 && n3 == 0, "a key-list preparation changed the render (or the empty list still rendered)"); std::printf("unit: a key list decodes only what its keys play (empty list: nothing, render refused), same bits\n"); }

  { // H9: a font of headers that alias ONE stream whose last granule is 0 (decoded in full, then thrown away, once per header) is prepared at once: refused up front from the page header (no work charged), whatever the number of headers.
    // Before the fix: 30000 headers x a 2 KB stream = 17.7 s, 2000 headers x a 1.29 MB stream = 13.5 s (verify-226-round2.md D1).
    std::string gb; if (readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3", &gb)) { Font g; parse((const uint8_t*)gb.data(), gb.size(), g);
      const struct { uint32_t sample, n; } HC[] = {{183, 30000}, {116, 2000}};
      for (auto& hc : HC) { const SampleHdr& h = g.samples[hc.sample]; std::vector<uint8_t> t(g.smpl + h.start, g.smpl + h.end); CHECK(zeroLastGranule(t), "no Ogg page"); auto fb = aliasSf3(t, {{0, (uint32_t)t.size()}}, hc.n);
        Font F; CHECK(parse(fb.data(), fb.size(), F) == kParseOk && F.samples.size() == hc.n && F.zones.size() == hc.n, "alias font parses (%zu zones)", F.zones.size());
        PresetPcm pc; const double t0 = nowSec(); const PrepareStatus st = prepare_preset(F, sf_preset(F, 0, 0), DecodeOpts(), pc); const double dt = nowSec() - t0; size_t silent = 0; for (auto& q : pc.byIndex) silent += q.present && !q.ok && q.d.size() == 1;
        CHECK(st == kPrepOk && dt < 5.0 && silent == hc.n && pc.work == 0, "%u headers on a %zu-byte stream with granule 0: status %d, %zu silent, work %llu, %.3f s", hc.n, t.size(), (int)st, silent, (unsigned long long)pc.work, dt);
        std::printf("unit: %u sample headers (%zu-byte font) aliasing one %zu-byte stream with last granule 0: prepared in %.3f s, work charged %llu (before the fix this took seconds, not milliseconds)\n", hc.n, fb.size(), t.size(), dt, (unsigned long long)pc.work); }
      // the same with distinct streams: 8 MB of the font's own streams, each with granule 0: no decoding at all
      { std::vector<uint8_t> smpl; std::vector<std::pair<uint32_t, uint32_t>> R; for (uint32_t i = 0; i < g.samples.size() && smpl.size() < ((size_t)8 << 20); i++) { const SampleHdr& h = g.samples[i]; if (!(h.type & 0x10) || h.end <= h.start || h.end - h.start < 20000 || h.end > g.smplN) continue;
          std::vector<uint8_t> t(g.smpl + h.start, g.smpl + h.end); zeroLastGranule(t); R.push_back({(uint32_t)smpl.size(), (uint32_t)(smpl.size() + t.size())}); smpl.insert(smpl.end(), t.begin(), t.end()); }
        auto fb = aliasSf3(smpl, R, (uint32_t)R.size()); Font F; parse(fb.data(), fb.size(), F); PresetPcm pc; const double t0 = nowSec(); const PrepareStatus st = prepare_preset(F, sf_preset(F, 0, 0), DecodeOpts(), pc); const double dt = nowSec() - t0;
        CHECK(st == kPrepOk && pc.work == 0 && dt < 5.0, "%zu distinct granule-0 streams: status %d work %llu %.3f s", R.size(), (int)st, (unsigned long long)pc.work, dt);
        std::printf("unit: %zu distinct granule-0 streams (%.1f MB): work charged %llu, %.3f s\n", R.size(), (double)smpl.size() / 1048576.0, (unsigned long long)pc.work, dt); } } }
  { // H10: a stream is decoded ONCE however many headers name it (cache by byte range: offset AND length), the copies equal a stand-alone decode, and the work charged is that of one decode
    std::string gb; if (readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3", &gb)) { Font g; parse((const uint8_t*)gb.data(), gb.size(), g);
      const SampleHdr& h = g.samples[183]; std::vector<uint8_t> t(g.smpl + h.start, g.smpl + h.end); auto fb = aliasSf3(t, {{0, (uint32_t)t.size()}}, 12000); Font F; parse(fb.data(), fb.size(), F);
      Pcm one; decode_sample(F, 0, DecodeOpts(), one); PresetPcm pc; const double t0 = nowSec(); const PrepareStatus st = prepare_preset(F, sf_preset(F, 0, 0), DecodeOpts(), pc); const double dt = nowSec() - t0; size_t same = 0; for (auto& q : pc.byIndex) same += q.ok && q.d == one.d && q.sr == one.sr;
      CHECK(st == kPrepOk && same == 12000 && pc.work == (uint64_t)t.size() + one.d.size() + kWorkPerOpen, "12000 aliases of one good stream: status %d, %zu equal to a stand-alone decode, work %llu (one decode: %zu)", (int)st, same, (unsigned long long)pc.work, t.size() + one.d.size() + (size_t)kWorkPerOpen);
      std::printf("unit: 12000 headers aliasing one good stream: decoded once (work %llu = %zu bytes + %zu frames + %zu per open), all 12000 equal a stand-alone decode, %.3f s\n", (unsigned long long)pc.work, t.size(), one.d.size(), (size_t)kWorkPerOpen, dt);
      // ranges that share an offset but differ in length (a truncated stream) or follow each other are different streams: each equals its own stand-alone decode
      const SampleHdr& h2 = g.samples[859]; std::vector<uint8_t> t2(g.smpl + h2.start, g.smpl + h2.end); std::vector<uint8_t> sm = t; sm.insert(sm.end(), t2.begin(), t2.end()); const uint32_t n1 = (uint32_t)t.size(), n2 = (uint32_t)t2.size();
      const std::vector<std::pair<uint32_t, uint32_t>> R = {{0, n1}, {n1, n1 + n2}, {0, n1 / 2}, {0, n1 - 37}, {n1, n1 + n2 / 2}, {0, n1 + n2}};
      for (double ctx : {0.0, 48000.0}) { auto fb2 = aliasSf3(sm, R, 60); Font F2; parse(fb2.data(), fb2.size(), F2); DecodeOpts o; o.ctxRate = ctx; PresetPcm p2; CHECK(prepare_preset(F2, sf_preset(F2, 0, 0), o, p2) == kPrepOk, "prepare"); size_t eq = 0, okc = 0;
        for (uint32_t i = 0; i < 60; i++) { Pcm a; decode_sample(F2, i, o, a); const Pcm& b = p2.byIndex[i]; okc += a.ok; eq += a.ok == b.ok && a.d == b.d && a.sr == b.sr && a.ls == b.ls && a.le == b.le && a.k == b.k && b.present; }
        CHECK(eq == 60, "ranges sharing an offset: %zu of 60 equal their stand-alone decode (ctx %g)", eq, ctx); std::printf("unit: 6 ranges (same offset, different lengths; adjacent) x 10 headers, ctx %g: %zu of 60 equal their own stand-alone decode (%zu decode ok)\n", ctx, eq, okc); } } }
  { // H11: the WORK budget: every decode is charged (kept or not); over the preset's or the font's budget the preset is refused with nothing kept; a refusal reports what it spent; a kept preset adds its work to the host's counter
    std::string gb; if (readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3", &gb)) { Font g; parse((const uint8_t*)gb.data(), gb.size(), g);
      std::vector<uint8_t> smpl; std::vector<std::pair<uint32_t, uint32_t>> R; uint64_t want = 0; for (uint32_t i = 0; i < 40; i++) { const SampleHdr& h = g.samples[i]; if (!(h.type & 0x10) || h.end <= h.start) continue; Pcm p; decode_sample(g, i, DecodeOpts(), p); if (!p.ok) continue;
        R.push_back({(uint32_t)smpl.size(), (uint32_t)(smpl.size() + (h.end - h.start))}); smpl.insert(smpl.end(), g.smpl + h.start, g.smpl + h.end); want += (h.end - h.start) + p.d.size() + kWorkPerOpen; }
      auto fb = aliasSf3(smpl, R, (uint32_t)R.size()); Font F; parse(fb.data(), fb.size(), F); const Preset* pr = sf_preset(F, 0, 0);
      PresetPcm a; uint64_t used = 0; DecodeOpts o; o.fontWorkUsed = &used; const PrepareStatus s0 = prepare_preset(F, pr, o, a); CHECK(s0 == kPrepOk && a.work == want && used == want, "work of %zu distinct streams: status %d, %llu (expected bytes + frames = %llu), counter %llu", R.size(), (int)s0, (unsigned long long)a.work, (unsigned long long)want, (unsigned long long)used);
      PresetPcm b; DecodeOpts o1; o1.presetWork = want - 1; const PrepareStatus s1 = prepare_preset(F, pr, o1, b); size_t kept = 0; for (auto& q : b.byIndex) kept += q.present; CHECK(s1 == kPrepOverWorkBudget && kept == 0 && b.bytes == 0 && b.work <= want - 1 && b.work > want / 2, "preset work budget: status %d, kept %zu, spent %llu", (int)s1, kept, (unsigned long long)b.work);
      PresetPcm c; DecodeOpts o2; o2.presetWork = want; const PrepareStatus s2 = prepare_preset(F, pr, o2, c); CHECK(s2 == kPrepOk && c.work == want, "a budget equal to the work suffices: %d", (int)s2);
      PresetPcm d; uint64_t u2 = 1000; DecodeOpts o3; o3.fontWork = want + 999; o3.fontWorkUsed = &u2; const PrepareStatus s3 = prepare_preset(F, pr, o3, d); CHECK(s3 == kPrepOverWorkBudget && u2 == 1000, "font work budget: status %d counter %llu", (int)s3, (unsigned long long)u2);
      o3.fontWork = want + 1000; PresetPcm d2; const PrepareStatus s4 = prepare_preset(F, pr, o3, d2); CHECK(s4 == kPrepOk && u2 == 1000 + want, "font work budget exactly met: %d counter %llu", (int)s4, (unsigned long long)u2);
      // a decode the BYTE budget throws away is charged what it spent: sample 116 (1.29 MB stream, 321867 frames) over a 100 kB budget
      { const SampleHdr& h = g.samples[116]; std::vector<uint8_t> t(g.smpl + h.start, g.smpl + h.end); auto f3 = aliasSf3(t, {{0, (uint32_t)t.size()}}, 1); Font F3; parse(f3.data(), f3.size(), F3); PresetPcm e; DecodeOpts o4; o4.presetBytes = 100000;
        const PrepareStatus s5 = prepare_preset(F3, sf_preset(F3, 0, 0), o4, e); CHECK(s5 == kPrepOverPresetBudget && e.work >= t.size() + kWorkPerOpen + 20000, "a discarded decode is charged: status %d work %llu (stream %zu bytes)", (int)s5, (unsigned long long)e.work, t.size()); }
      std::printf("unit: work budget: %zu streams cost %llu units (per open + bytes + frames); one unit less refuses (spent %llu, nothing kept); font counter refuses and counts exactly; a decode thrown away for the byte budget is charged\n", R.size(), (unsigned long long)want, (unsigned long long)b.work); } }
  { // H12: parse memory is bounded by the font's size (counted allocation, not RSS): 400 x 500 zone pairs with ~50 generators each (a 191 KB file; 200000 pairs, 205 MB RSS before) is refused early; 20000 pairs of the same shape parse
    auto big = genSf2(400, 500, 50); const size_t bound = kParseTableFloor + kParseTableFactor * big.size(); Font f; const size_t base = g_liveNew; g_peakNew = g_liveNew; const double t0 = nowSec(); const ParseStatus st = parse(big.data(), big.size(), f); const double dt = nowSec() - t0; const size_t peak = g_peakNew - base;
    CHECK(st == kParseTooBig && peak <= 3 * bound, "hostile 400 x 500 x 50-generator font (%zu bytes): status %d, peak allocation %zu bytes (limit 3 x %zu)", big.size(), (int)st, peak, bound);
    std::printf("unit: %zu-byte font, 200000 zone pairs of 50 generators: status %d (too big), counted peak %.1f MB (table limit %.1f MB = %zu MB + %zu x the file), %.0f ms\n", big.size(), (int)st, (double)peak / 1048576.0, (double)bound / 1048576.0, kParseTableFloor >> 20, kParseTableFactor, dt * 1000);
    auto okf = genSf2(400, 50, 50); Font f2; g_peakNew = g_liveNew; const size_t base2 = g_liveNew; const ParseStatus st2 = parse(okf.data(), okf.size(), f2); const size_t peak2 = g_peakNew - base2;
    CHECK(st2 == kParseOk && f2.zones.size() == 20000 && peak2 <= 3 * (kParseTableFloor + kParseTableFactor * okf.size()), "20000-pair font: status %d zones %zu peak %zu", (int)st2, f2.zones.size(), peak2);
    std::printf("unit: the same shape with 20000 pairs (%zu bytes) parses: %zu zones, %zu generator records, counted peak %.1f MB\n", okf.size(), f2.zones.size(), f2.gens.size(), (double)peak2 / 1048576.0); }
  std::printf(fails ? "FAIL: %d\n" : "OK\n", fails);
  return fails ? 1 : 0;
}

// ---- RAM and time of the decoded font (the numbers in U32-NOTES.md): `test_sf ram`
static int doRam() {
  std::string gb; if (!readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3", &gb)) { std::printf("FAIL: no GM font\\n"); return 1; }
  Font f; if (parse((const uint8_t*)gb.data(), gb.size(), f) != kParseOk) return 1;
  std::printf("GM font: %zu presets, %zu zones, %zu samples, zone pairs looked at while parsing (Font::work): %llu\n", f.presets.size(), f.zones.size(), f.samples.size(), (unsigned long long)f.work);
  struct { const char* name; int bank, prog; } P[] = {{"Grand Piano", 0, 0}, {"Fingered Bass", 0, 33}, {"Standard drum kit", 128, 0}, {"Strings", 0, 48}};
  auto now = []() { return (double)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() / 1e6; };
  for (auto& p : P) {
    const Preset* pr = sf_preset(f, p.bank, p.prog); uint8_t all[128], oct[128], three[128]; std::memset(oct, 0, 128); std::memset(three, 0, 128); std::memset(all, 1, 128); for (int k = 60; k < 72; k++) oct[k] = 1; for (int k = 48; k < 84; k++) three[k] = 1;
    for (int mode = 0; mode < 4; mode++) {
      PresetPcm pc; DecodeOpts o; if (mode == 3) o.ctxRate = 48000; const double t0 = now(); prepare_preset(f, pr, o, pc, mode == 1 ? oct : mode == 2 ? three : nullptr); const double t1 = now(); size_t ns = 0; for (auto& q : pc.byIndex) ns += q.present ? 1 : 0;
      std::printf("%-18s %-28s %4zu samples %8.1f MB  decode %.2f s\\n", p.name, mode == 0 ? "all keys, own rates" : mode == 1 ? "one octave (C4..B4)" : mode == 2 ? "three octaves (C3..B5)" : "all keys, resampled to 48 kHz", ns, (double)pc.bytes / 1048576.0, t1 - t0);
    }
  }
  { size_t tot = 0, totR = 0; double t0 = now(); for (uint32_t i = 0; i < f.samples.size(); i++) { Pcm p; decode_sample(f, i, DecodeOpts(), p); tot += p.d.size() * 4; } const double t1 = now();
    std::printf("whole font, own rates: %.1f MB, decode %.2f s\\n", (double)tot / 1048576.0, t1 - t0); (void)totR; }
  return 0;
}

// every preset of the GM font prepared (whole keyboard) at its own rates and at 48 kHz: one line each (bytes, work, FNV of every kept sample), for before / after comparisons: `test_sf gmall`
static int doGmAll() {
  std::string gb; if (!readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3", &gb)) { std::printf("FAIL: no GM font\n"); return 1; }
  Font f; if (parse((const uint8_t*)gb.data(), gb.size(), f) != kParseOk) return 1; uint64_t maxWork = 0, all = 0; double tt = 0;
  for (double ctx : {0.0, 48000.0}) for (const Preset& p : f.presets) { DecodeOpts o; o.ctxRate = ctx; PresetPcm pc; const double t0 = nowSec(); const PrepareStatus st = prepare_preset(f, &p, o, pc); tt += nowSec() - t0; uint64_t h = 0xcbf29ce484222325ull; size_t ns = 0;
    for (size_t i = 0; i < pc.byIndex.size(); i++) { const Pcm& q = pc.byIndex[i]; if (!q.present) continue; ns++; h ^= fnv(q.d.data(), q.d.size() * 4) + (uint64_t)i + (uint64_t)q.ok; h *= 0x100000001b3ull; h ^= fnv(&q.sr, 8) ^ fnv(&q.ls, 8) ^ fnv(&q.le, 8) ^ fnv(&q.k, 8); h *= 0x100000001b3ull; }
    std::printf("ctx %g bank %d prog %d status %d samples %zu bytes %llu hash %016llx WORK %llu\n", ctx, p.bank, p.prog, (int)st, ns, (unsigned long long)pc.bytes, (unsigned long long)h, (unsigned long long)pc.work); maxWork = std::max<uint64_t>(maxWork, pc.work); all += pc.work; }
  std::printf("presets %zu x 2: max work of one preparation %llu, total %llu, total time %.2f s\n", f.presets.size(), (unsigned long long)maxWork, (unsigned long long)all, tt); return 0;
}

int main(int argc, char** argv) {
  if (argc < 2 || (argc < 3 && std::strcmp(argv[1], "unit") && std::strcmp(argv[1], "ram") && std::strcmp(argv[1], "gmall"))) { std::printf("usage: test_sf export|compare|chrome DIR | unit\n"); return 2; }
  if (!std::strcmp(argv[1], "export")) return doExport(argv[2]);
  if (!std::strcmp(argv[1], "compare")) return doCompare(argv[2]);
  if (!std::strcmp(argv[1], "unit")) return doUnit();
  if (!std::strcmp(argv[1], "ram")) return doRam();
  if (!std::strcmp(argv[1], "gmall")) return doGmAll();
  if (!std::strcmp(argv[1], "chrome")) return doChrome(argv[2], argc > 3 ? std::atof(argv[3]) : 0);
  return 2;
}
