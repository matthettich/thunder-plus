// U31: the …chains engine (core/src/chains) against chains/chains-worklet.js. See docs/port/U31-NOTES.md for what each section gates.
//  golden/chains_cases.json (tools/chains-dump.mjs): 429 module rows (tests/golden/chains-modules.json: one shared engine, the JS's order), the 4 processor / node rows, and the scenarios of this unit.
//  Every case that has no module built from a strict upstream library is bit-exact with its committed hash on every platform. A library case follows the -100 dB rule against the reference
//  (--ref: x86-64 Linux, where CI runs the web's own wasm through Node) and is named in golden/chains_effects.json when it is exact or beyond; without the reference: the committed RMS and length.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "chains/chains.h"
#include "fx/pumpfx.h"
#include "rt/jsmath.h"
#include "rt/json.h"

using namespace thunder;
using namespace thunder::chains;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

typedef std::vector<std::vector<float>> Rec;   // what a case records: arrays, hashed as one stream
static uint32_t fnvCanon(const Rec& r) {   // kind 'over': every NaN as the canonical 0x7fc00000 (the machine's NaN sign / payload differs between the JS's arithmetic and ours)
  uint32_t h = 0x811c9dc5u;
  for (const auto& a : r) for (float f : a) { uint32_t b; if (f != f) b = 0x7fc00000u; else std::memcpy(&b, &f, 4); for (int k = 0; k < 4; k++) { h ^= (b >> (8 * k)) & 255u; h *= 0x01000193u; } }
  return h;
}
static uint32_t fnvRec(const Rec& r) {
  uint32_t h = 0x811c9dc5u;
  for (const auto& a : r) { const unsigned char* u = (const unsigned char*)a.data(); for (size_t i = 0; i < a.size() * 4; i++) { h ^= u[i]; h *= 0x01000193u; } }
  return h;
}
static std::string hex(uint32_t h) { char b[16]; std::snprintf(b, sizeof b, "%08x", h); return b; }
static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static std::vector<float> flat(const Rec& r) { std::vector<float> f; for (const auto& a : r) f.insert(f.end(), a.begin(), a.end()); return f; }

// tests/helpers.js testSignal
static std::vector<float> signalOf(int n, uint32_t seed = 12345, double sr = 48000) {
  std::vector<float> out((size_t)n); uint32_t s = seed ? seed : 1;
  for (int i = 0; i < n; i++) {
    s ^= s << 13; s ^= s >> 17; s ^= s << 5; const double noise = ((double)s / 4294967296.0 - .5) * .1;
    out[(size_t)i] = (float)(.4 * js_sin(2 * 3.141592653589793 * 220 * i / sr) + .2 * js_sin(2 * 3.141592653589793 * 1337 * i / sr) + (i < (n >> 2) ? noise : 0));
  }
  return out;
}
// a JSON value as text (numbers as written)
static std::string ser(Json j) {
  if (j.isNull()) return "null";
  if (j.isBool()) return j.asBool() ? "true" : "false";
  if (j.isNum()) return std::string(j.numText());
  if (j.isStr()) { std::string s = "\""; for (char c : j.asStr()) { if (c == '"' || c == '\\') s += '\\'; s += c; } return s + "\""; }
  if (j.isArr()) { std::string s = "["; for (size_t i = 0; i < j.size(); i++) s += (i ? "," : "") + ser(j.at(i)); return s + "]"; }
  if (j.isObj()) { std::string s = "{"; for (size_t i = 0; i < j.size(); i++) s += (i ? "," : "") + std::string("\"") + std::string(j.keyAt(i)) + "\":" + ser(j.valAt(i)); return s + "}"; }
  return "null";
}
struct Doc { JsonDoc d; Json r; explicit Doc(const std::string& t) { if (!d.parse(t)) std::printf("bad test json: %s\n", t.c_str()); r = d.root(); } };

static EngineOptions opts(int focus, bool playing) { EngineOptions o; o.focus = focus; o.transport.tempo = 120; o.transport.lpb = 4; o.transport.ticks = 6; o.transport.playing = playing; return o; }
static void newHost(Host& h) { h.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u); }

// ---- mod rows: tests/chains.test.js renderModule, in its order, on ONE engine and ONE Host ----
constexpr int kModBlocks = 400;
struct ModRun {
  Host host; std::unique_ptr<ChainsEngine> e; std::vector<float> sig;
  ModRun() { newHost(host); e.reset(new ChainsEngine(48000, opts(0, true), &host)); sig = signalOf(128 * kModBlocks); }
  bool render(Json c, Rec* rec, bool probe) {
    if (!e->loadPatch(c.get("patch"))) return false;
    if (probe) e->setTelemetry(true);
    Json ports = c.get("ports"); const size_t np = probe ? 0 : ports.size();
    rec->assign(2 + np, std::vector<float>((size_t)128 * kModBlocks));
    for (int b = 0; b < kModBlocks; b++) {
      const float* s = &sig[(size_t)b * 128]; e->setTrackInputs(s, s, 1); e->process(128);
      std::memcpy(&(*rec)[0][(size_t)b * 128], e->bus(1), 512); std::memcpy(&(*rec)[1][(size_t)b * 128], e->busR(1), 512);
      for (size_t p = 0; p < np; p++) { const float* o = e->outBuf("b", ports.at(p).asStr()); if (o) std::memcpy(&(*rec)[2 + p][(size_t)b * 128], o, 512); }
    }
    if (probe) e->setTelemetry(false);
    return true;
  }
};
// ---- proc rows ----
static std::string hotPatch(double gain) {
  char g[64]; std::snprintf(g, sizeof g, "%.17g", gain);
  return std::string("{\"modules\":[{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"b\",\"type\":\"utility\",\"params\":{\"gain\":") + g + ",\"pan\":0,\"mono\":0,\"flip\":0,\"mute\":0}},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1,\"level\":1}}],"
    "\"cables\":[{\"from\":[\"a\",\"out\"],\"to\":[\"b\",\"in\"]},{\"from\":[\"a\",\"outR\"],\"to\":[\"b\",\"inR\"]},{\"from\":[\"b\",\"out\"],\"to\":[\"c\",\"in\"]},{\"from\":[\"b\",\"outR\"],\"to\":[\"c\",\"inR\"]}]}";
}
static Rec runProc(Json c) {
  const int blocks = (int)c.num("blocks"); const bool node = c.str("node") == "thunder-chains";
  Host host; newHost(host); const std::string pj = hotPatch(c.num("gain")); Doc d(pj);
  Rec r(2, std::vector<float>((size_t)blocks * 128)); const std::vector<float> sig = signalOf(128 * blocks);
  if (!node) {
    ChainsEngine e(48000, opts(-1, false), &host); CHECK(e.loadPatch(d.r), "proc: patch");
    for (int b = 0; b < blocks; b++) { const float* s = &sig[(size_t)b * 128]; e.setTrackInputs(s, s, 1); e.setSidechain(nullptr, nullptr, 128); e.process(128); e.endStandaloneBlock(); chains_mixdown(e, &r[0][(size_t)b * 128], &r[1][(size_t)b * 128], 128); }
  } else {
    ChainsNode n(48000, 0, opts(-1, false), &host); CHECK(n.engine().loadPatch(d.r), "proc: patch");
    for (int b = 0; b < blocks; b++) { const float* s = &sig[(size_t)b * 128]; NodeIn in; in.inL = s; in.inR = s; NodeOut out; out.L = &r[0][(size_t)b * 128]; out.R = &r[1][(size_t)b * 128]; n.process(in, out, 128); }
  }
  return r;
}
// ---- scn rows ----
static Rec runScn(Json c, bool* ok, Json patchOverride = Json()) {
  *ok = true; Host host; newHost(host);
  const int B = (int)c.num("blocks"), N = (int)c.num("blockLen", 128), focus = (int)c.num("focus", -1), chans = (int)c.num("chans", 2), ntaps = (int)c.num("taps"), nsends = (int)c.num("sends"), nfeeds = (int)c.num("feeds");
  const bool node = c.str("mode") == "node", standalone = c.boolean("standalone");
  Json ev = c.get("events"), tm = c.get("tapmap"), bmj = c.get("busmap");
  auto chan = [&](int j) { return signalOf(N * B, 12345 + 77 * j); };
  std::vector<std::vector<float>> chs, taps, sends, feeds; for (int j = 0; j < chans; j++) chs.push_back(chan(j)); for (int j = 0; j < ntaps; j++) taps.push_back(signalOf(N * B, 5000 + j));
  for (int k = 0; k < nsends; k++) sends.push_back(signalOf(N * B, 7000 + k)); for (int j = 0; j < nfeeds * 2; j++) feeds.push_back(signalOf(N * B, 9000 + j)); const std::vector<float> scS = signalOf(N * B, 4242);
  auto applyEv = [&](ChainsEngine& E, Json x, double* byp) {
    const std::string_view op = x.at(1).asStr();
    if (op == "bsw") { const int n = (int)x.at(2).asNum(); if (byp) byp[n - 1] = x.at(3).asNum(); else E.setBypassSwitch(n, x.at(3).asNum()); return; }
    if (op == "kn") { const int n = (int)x.at(2).asNum(); if (byp) byp[kBypSwitches + n - 1] = x.at(3).asNum(); else E.setBypassSwitch(kBypSwitches + n, x.at(3).asNum()); return; }   // patch knobs: k1..k8 (the node's params, or the engine's knobs[n])
    if (op == "text") { E.setText(x.at(2).asStr(), x.at(3).asStr()); return; }
    if (op == "param") { Json v = x.at(4); E.setParam(x.at(2).asStr(), x.at(3).asStr(), v.isStr() ? (double)NAN : v.asNum()); return; }   // JSON has no NaN: the "nan" rows carry the string "nan"
    if (op == "connect") { E.connect(x.at(2).asStr(), x.at(3).asStr(), x.at(4).asStr(), x.at(5).asStr(), x.at(6).isBool() && x.at(6).asBool()); return; }
    if (op == "disconnect") { E.disconnect(x.at(2).asStr(), x.at(3).asStr(), x.at(4).asStr(), x.at(5).asStr()); return; }
    if (op == "patch") { Doc p("{\"op\":\"patch\",\"value\":" + ser(x.at(2)) + "}"); E.handleMessage(p.r); return; }
    std::string m = "{\"op\":\"" + std::string(op) + "\"";
    if (op == "add") m += ",\"id\":" + ser(x.at(2)) + ",\"type\":" + ser(x.at(3)) + ",\"params\":" + ser(x.at(4));
    else if (op == "remove") m += ",\"id\":" + ser(x.at(2));
    else if (op == "params") m += ",\"list\":" + ser(x.at(2));
    else m += ",\"value\":" + ser(x.at(2));
    m += "}"; Doc p(m); E.handleMessage(p.r);
  };
  Rec r;
  if (!node) {
    ChainsEngine e(48000, opts(focus, true), &host); if (!e.loadPatch(patchOverride.missing() ? c.get("patch") : patchOverride)) { *ok = false; return r; }
    if (tm.isArr()) { Doc m("{\"op\":\"tapmap\",\"value\":" + ser(tm) + "}"); e.handleMessage(m.r); }
    r.assign(10, std::vector<float>((size_t)128 * B));
    std::vector<float> tl((size_t)128 * chans), tr((size_t)128 * chans); std::vector<const float*> tp((size_t)ntaps);
    for (int b = 0; b < B; b++) {
      for (size_t k = 0; k < ev.size(); k++) if ((int)ev.at(k).at(0).asNum() == b) applyEv(e, ev.at(k), nullptr);
      for (int j = 0; j < chans; j++) { std::memcpy(&tl[(size_t)j * 128], &chs[(size_t)j][(size_t)b * 128], 512); std::memcpy(&tr[(size_t)j * 128], &chs[(size_t)((j + 1) % chans)][(size_t)b * 128], 512); }
      e.setTrackInputs(tl.data(), tr.data(), chans);
      if (ntaps) { for (int j = 0; j < ntaps; j++) tp[(size_t)j] = &taps[(size_t)j][(size_t)b * 128]; e.setTaps(tp.data(), ntaps); } else e.setTaps(nullptr, 0);
      e.process(128); if (standalone) e.endStandaloneBlock();
      chains_mixdown(e, &r[0][(size_t)b * 128], &r[1][(size_t)b * 128], 128);
      for (int k = 0; k < 8; k++) std::memcpy(&r[(size_t)(2 + k)][(size_t)b * 128], e.sendOut(k), 512);
    }
  } else {
    ChainsNode n(48000, (int)c.num("track"), opts(-1, false), &host); if (!n.engine().loadPatch(c.get("patch"))) { *ok = false; return r; }
    if (bmj.isArr()) { Doc m("{\"op\":\"busmap\",\"value\":" + ser(bmj) + "}"); n.engine().handleMessage(m.r); }
    if (tm.isArr()) { Doc m("{\"op\":\"tapmap\",\"value\":" + ser(tm) + "}"); n.engine().handleMessage(m.r); }
    r.assign(10, std::vector<float>((size_t)N * B)); double byp[kBypSwitches + kKnobs]; for (double& v : byp) v = -1;
    std::vector<const float*> tp((size_t)ntaps), sp((size_t)nsends), fp((size_t)nfeeds * 2); std::vector<std::vector<float>> so(8, std::vector<float>((size_t)N)); std::vector<float*> sop(8); std::vector<float> oL((size_t)N), oR((size_t)N);
    for (int k = 0; k < 8; k++) sop[(size_t)k] = so[(size_t)k].data();
    for (int b = 0; b < B; b++) {
      for (size_t k = 0; k < ev.size(); k++) if ((int)ev.at(k).at(0).asNum() == b) applyEv(n.engine(), ev.at(k), byp);
      const size_t o = (size_t)b * N; NodeIn in; in.inL = &chs[0][o]; in.inR = &chs[0][o]; if (c.boolean("sc")) { in.scL = &scS[o]; in.scR = &scS[o]; }
      if (ntaps) { for (int j = 0; j < ntaps; j++) tp[(size_t)j] = &taps[(size_t)j][o]; in.taps = tp.data(); in.nTaps = ntaps; }
      if (nsends) { for (int j = 0; j < nsends; j++) sp[(size_t)j] = &sends[(size_t)j][o]; in.sends = sp.data(); in.nSends = nsends; }
      if (nfeeds) { for (int j = 0; j < nfeeds * 2; j++) fp[(size_t)j] = &feeds[(size_t)j][o]; in.feeds = fp.data(); in.nFeeds = nfeeds; }
      in.byp = byp; in.kn = byp + kBypSwitches; NodeOut out; out.L = oL.data(); out.R = oR.data(); out.sends = sop.data(); out.nSends = 8;
      n.process(in, out, N);
      std::memcpy(&r[0][o], oL.data(), (size_t)N * 4); std::memcpy(&r[1][o], oR.data(), (size_t)N * 4); for (int k = 0; k < 8; k++) std::memcpy(&r[(size_t)(2 + k)][o], so[(size_t)k].data(), (size_t)N * 4);
    }
  }
  return r;
}

// ---- the library cases (as test_waves) ----
static double rmsOf(const std::vector<float>& f) { double a = 0; for (float v : f) a += (double)v * v; return f.empty() ? 0 : std::sqrt(a / (double)f.size()); }
static double dbOf(const std::vector<float>& f, const std::string& ref, bool* ok) {
  *ok = false; if (ref.size() != f.size() * 4) return 0;
  const float* rf = (const float*)ref.data(); double mx = 0, pk = 0;
  for (size_t k = 0; k < f.size(); k++) { mx = std::fmax(mx, std::fabs((double)rf[k] - f[k])); pk = std::fmax(pk, std::fabs((double)rf[k])); }
  *ok = true; return 20 * std::log10(mx / (pk > 0 ? pk : 1) + 1e-30);
}
static double refRms(const std::string& ref, size_t n) { if (ref.size() != n * 4) return -1; const float* rf = (const float*)ref.data(); double a = 0; for (size_t k = 0; k < n; k++) a += (double)rf[k] * rf[k]; return n ? std::sqrt(a / (double)n) : 0; }
static bool finiteFlat(const std::vector<float>& f) { for (float v : f) if (!std::isfinite(v)) return false; return true; }

static void runGolden(const std::string& golden, const std::string& effectsFile, const std::string& refDir, const std::string& filter, const std::string& dumpDir, const std::string& writeEffects) {
  std::string txt; if (!readFile(golden, &txt)) { CHECK(false, "cannot read %s", golden.c_str()); return; }
  JsonDoc d; if (!d.parse(txt)) { CHECK(false, "golden parse"); return; }
  JsonDoc nd; std::string ntxt; Json eff; if (!effectsFile.empty() && readFile(effectsFile, &ntxt) && nd.parse(ntxt)) eff = nd.root();
  if (writeEffects.empty() && filter.empty() && !eff.isObj()) CHECK(false, "the effect-case record (chains_effects.json) is missing");
  Json all = d.root(); int ran = 0, bad = 0, skipped = 0, nExact = 0, nWithin = 0, nBeyond = 0, nStruct = 0, nPlain = 0, nScn = 0, nProc = 0, nLeftOut = 0, nMal = 0; double worst = -999; std::string worstName, out = "{", leftOut;
  ModRun mr, mrOver;   // mrOver: the rows of group 'over' (parameters outside their range) have an engine of their own
  // the mod rows share one engine, in the JS's order: a skipped row (a library missing here) must still make the JS's draws, so the order is kept by running every row that has no missing library
  for (size_t i = 0; i < all.size(); i++) {
    Json c = all.at(i); const std::string name(c.str("name")), kind(c.str("kind")); if (!filter.empty() && name.find(filter) == std::string::npos) continue;
    std::string fn = name; for (char& ch : fn) if (ch == '/' || ch == '@') ch = '_';
    Rec rec; bool lib = false, lacks = false, probe = c.boolean("probe");
    if (kind == "mod") {
      const std::string type(c.str("type")); lib = chains_type_is_lib(type); lacks = !chains_have_lib(type);
      CHECK(lib == c.boolean("lib"), "%s: the library flag of the type disagrees with the JS (native %d)", name.c_str(), (int)lib);
      const bool over = c.str("group") == "over"; if (!(over ? mrOver : mr).render(c, &rec, probe)) { CHECK(false, "%s: patch refused", name.c_str()); bad++; continue; }
      if (lacks && writeEffects.empty()) { skipped++; continue; }   // (rendered all the same: the JS's draws from the shared seed counter and Math.random must happen in its order)
    } else if (kind == "mal") {   // a malformed document (JSON text in the row): the JS plays it (outcome "plays": same output) or throws on it (outcome "refuses": here refused, nothing changed)
      JsonDoc pd; if (!pd.parse(c.str("patchText"))) { CHECK(false, "%s: the row's document does not parse", name.c_str()); bad++; continue; }
      if (c.str("outcome") == "refuses") { Host mh; ChainsEngine me(48000, EngineOptions(), &mh); const bool ld = me.loadPatch(pd.root()); if (ld) { bad++; CHECK(false, "%s: the JS throws on this document, native loaded it", name.c_str()); } else { ran++; nMal++; } continue; }
      JsonDoc cfg; cfg.parse("{\"mode\":\"engine\",\"blocks\":20,\"chans\":3,\"standalone\":true,\"focus\":-1}"); bool ok = true; rec = runScn(cfg.root(), &ok, pd.root());
      if (!ok) { CHECK(false, "%s: the JS plays this document, native refused it", name.c_str()); bad++; continue; }
    } else if (kind == "proc") { rec = runProc(c); }
    else { bool ok = true; rec = runScn(c, &ok); if (!ok) { CHECK(false, "%s: patch refused", name.c_str()); bad++; continue; } }
    ran++;
    if (!dumpDir.empty()) { std::ofstream f(dumpDir + "/" + fn + ".f32", std::ios::binary); for (const auto& a : rec) f.write((const char*)a.data(), (std::streamsize)(a.size() * 4)); }
    if (c.get("deviates").isStr()) {   // a decided difference from the JS (named in the notes): the output is silence
      bool silent = true; for (const auto& a : rec) for (float v : a) if (v != 0) silent = false; CHECK(silent, "%s: %s", name.c_str(), std::string(c.str("deviates")).c_str()); nLeftOut++; continue;
    }
    const std::string want(c.str("hash")); std::string h = hex(c.str("group") == "over" ? fnvCanon(rec) : fnvRec(rec));   // ("nan" rows: the JS output is finite, so plain FNV)
    if (probe) {   // the Scope: its show() list (the last 515 floats of the JS's stream) is not ported; the audio ([L, R]) is compared on its own
      Rec lr(rec.begin(), rec.begin() + 2); h = hex(fnvRec(lr)); const bool same = h == c.str("hashLR"); if (!same) { bad++; CHECK(false, "%s: audio not bit-exact (%s != %s)", name.c_str(), h.c_str(), std::string(c.str("hashLR")).c_str()); } else { nLeftOut++; leftOut += (leftOut.empty() ? "" : ", ") + name; } continue;
    }
    const bool exact = h == want;
    if (!lib) { if (exact) { if (kind == "mod") nPlain++; else if (kind == "proc") nProc++; else nScn++; } else { bad++; CHECK(false, "%s: not bit-exact (%s != %s)", name.c_str(), h.c_str(), want.c_str()); } continue; }
    const std::vector<float> fl = flat(rec); std::string ref; const bool haveRef = !refDir.empty() && readFile(refDir + "/" + fn + ".f32", &ref); bool okdb = false; const double db = haveRef ? dbOf(fl, ref, &okdb) : 0;
    const double rms = rmsOf(fl);
    if (!writeEffects.empty()) {
      if (!okdb) { CHECK(false, "%s: --write-effects needs --ref", name.c_str()); continue; }
      const bool beyond = !exact && db > -100; char b[420];
      std::snprintf(b, sizeof b, "%s\"%s\":{\"rms\":%.9g,\"len\":%zu,%s}", out.size() > 1 ? "," : "", name.c_str(), refRms(ref, fl.size()), fl.size(), exact ? "\"exact\":true" : beyond ? ("\"beyond\":" + std::to_string(db)).c_str() : "\"exact\":false");
      out += b; continue;
    }
    Json rec2 = eff.get(name);
    if (!rec2.isObj()) { bad++; CHECK(false, "%s: a library case with no entry in chains_effects.json", name.c_str()); continue; }
    const bool wasExact = rec2.get("exact").isBool() && rec2.get("exact").asBool(), beyondRec = rec2.get("beyond").isNum();
    if (!finiteFlat(fl)) { bad++; CHECK(false, "%s: output is not finite", name.c_str()); continue; }
    if ((size_t)rec2.num("len") != fl.size()) { bad++; CHECK(false, "%s: length %zu, wanted %d", name.c_str(), fl.size(), (int)rec2.num("len")); continue; }
    const double rr = rec2.num("rms"), dr = rr > 1e-9 ? 20 * std::log10((rms + 1e-30) / rr) : (rms < 1e-9 ? 0 : 999);
    const std::string type(c.str("type")); const bool tolCaseM = type == "marbles" || type == "tides";
    if (okdb) {
      if (db > worst) { worst = db; worstName = name; }
      if (exact) { nExact++; continue; }
      if (wasExact) { bad++; CHECK(false, "%s: was bit-exact with the web's wasm, now %.1f dB", name.c_str(), db); continue; }
      if (db <= -100) { nWithin++; continue; }
      if (tolCaseM) { bad++; CHECK(false, "%s: %.1f dB is above the -100 dB line for Marbles / Tides", name.c_str(), db); continue; }
      if (!beyondRec) { bad++; CHECK(false, "%s: %.1f dB is beyond -100 dB and not listed by name", name.c_str(), db); continue; }
      nBeyond++; if (db > rec2.num("beyond") + 3) { bad++; CHECK(false, "%s: %.1f dB is more than 3 dB worse than the %.1f dB recorded for it", name.c_str(), db, rec2.num("beyond")); }   // the recorded value is a bound, not a label
      if (std::fabs(dr) > 1) { bad++; CHECK(false, "%s: RMS %.2f dB off the reference's (listed beyond -100 dB: structural check)", name.c_str(), dr); }
    } else {
      nStruct++; if (std::fabs(dr) > 1) { bad++; CHECK(false, "%s: RMS %.2f dB off the reference's (no reference here: structural check)", name.c_str(), dr); }
    }
  }
  if (!writeEffects.empty()) { out += "}\n"; std::ofstream f(writeEffects, std::ios::binary); f << out; std::printf("wrote %s\n", writeEffects.c_str()); return; }
  std::printf("malformed documents (rows 'malx/'): %d documents the JS throws on were refused; the rest the JS plays are in the scenario count below\n", nMal);
  std::printf("golden cases: %d run, %d skipped (a library is missing in this build); %d module rows without a library bit-exact, %d scenarios and %d processor rows bit-exact; library rows: %d exact, %d within -100 dB, %d beyond (left out by name, structural check), %d structural only (no reference here); worst %.1f dB %s; %d left out by name or decided otherwise%s%s; %d wrong\n",
    ran, skipped, nPlain, nScn, nProc, nExact, nWithin, nBeyond, nStruct, worst, worstName.c_str(), nLeftOut, leftOut.empty() ? "" : " (Scope rows: audio exact, show() list not ported: ", leftOut.empty() ? "" : (leftOut + ")").c_str(), bad);
}

// documents at which the JS THROWS in loadPatch (a cable that is null or a number, a null `from` / `to` that is reached, a null module, a `cables` that is not iterable): the engine refuses and is left as it was; the documents the JS
// plays (the 'mal/' scenarios of the golden file) load, see runGolden. A cable whose `from` module is missing is skipped BEFORE `to` is read, so a null `to` after it is no error (as the JS).
static void testRefusals() {
  const char* m = "{\"id\":\"a\",\"type\":\"trackIn\",\"params\":{\"track\":1}},{\"id\":\"v\",\"type\":\"vca\"},{\"id\":\"c\",\"type\":\"chainOut\",\"params\":{\"track\":1}}";
  const char* good = "{\"from\":[\"a\",\"out\"],\"to\":[\"v\",\"in\"]},{\"from\":[\"v\",\"out\"],\"to\":[\"c\",\"in\"]}";
  struct T { std::string doc; bool loads; const char* what; };
  const std::vector<T> ts = {
    {std::string("{\"modules\":[") + m + "],\"cables\":[" + good + "]}", true, "plain"},
    {std::string("{\"modules\":[") + m + "],\"cables\":[" + good + ",{\"from\":[\"a\",\"out\"],\"to\":null}]}", false, "to: null (a is a module)"},
    {std::string("{\"modules\":[") + m + "],\"cables\":[" + good + ",{\"from\":[\"zz\",\"out\"],\"to\":null}]}", true, "to: null after a missing from"},
    {std::string("{\"modules\":[") + m + "],\"cables\":[" + good + ",null]}", false, "cable null"},
    {std::string("{\"modules\":[") + m + "],\"cables\":[" + good + ",7]}", false, "cable number"},
    {std::string("{\"modules\":[") + m + "],\"cables\":[" + good + ",{\"to\":[\"v\",\"in\"]}]}", false, "from missing"},
    {std::string("{\"modules\":[") + m + ",null],\"cables\":[" + good + "]}", false, "module null"},
    {std::string("{\"modules\":[") + m + "],\"cables\":{}}", false, "cables an object"},
    {std::string("{\"modules\":5,\"cables\":[]}"), false, "modules a number"},
    {std::string("{\"modules\":\"abc\",\"cables\":[]}"), true, "modules a string (characters are not modules)"},
    {std::string("5"), true, "a document that is a number: an empty patch"},
    {std::string("null"), false, "document null"},
  };
  for (const T& t : ts) {
    JsonDoc d; if (!d.parse(t.doc)) { CHECK(false, "test doc does not parse: %s", t.what); continue; }
    Host h; ChainsEngine e(48000, EngineOptions(), &h); const bool ok = e.loadPatch(d.root());
    CHECK(ok == t.loads, "%s: loadPatch %s, the JS %s", t.what, ok ? "loaded" : "refused", t.loads ? "plays it" : "throws");
  }
}
int main(int argc, char** argv) {
  std::string golden, effectsFile, refDir, filter, dumpDir, writeEffects;
  for (int i = 1; i < argc; i++) { const std::string a = argv[i]; if (a == "--ref" && i + 1 < argc) refDir = argv[++i]; else if (a == "--effects" && i + 1 < argc) effectsFile = argv[++i]; else if (a == "--write-effects" && i + 1 < argc) writeEffects = argv[++i];
    else if (a == "--filter" && i + 1 < argc) filter = argv[++i]; else if (a == "--dump" && i + 1 < argc) dumpDir = argv[++i]; else golden = a; }
  if (golden.empty()) CHECK(false, "no golden file given"); else runGolden(golden, effectsFile, refDir, filter, dumpDir, writeEffects);
  testRefusals();
  std::printf(fails ? "test_chains: %d FAILED\n" : "test_chains: ok\n", fails);
  return fails ? 1 : 0;
}
