// U18: the pump effects as slot kinds of the track-effect host (core/src/fx/fxpump.cpp, pumpfx.h) against the real ThunderFx worklet running a pump.wasm in Node
// (tools/pump-dump.mjs: every one of the 25 effects at its defaults and with a factory preset, ON / OFF / switching, the OFF rest and its three wakes, block sizes 1 to 1024,
// slots in series, two nodes side by side, COMP's External sidechain, notes, FXV, pump_hold, a runaway plugin, telemetry). Per case: the output against the JS sample by sample,
// gate -80 dB below the reference's peak, the figure printed (exact where it is); the posts the worklet makes ({lvl}, {dyn}, {pump}, {plat}); the case list is committed
// (golden/pump_cases.inc) and a case whose reference file is missing FAILS. Also: pumpLatencyOf of every type through the plan (the number LATENCY COMP counts), the pump table
// against the reference's meta, a second pass in reverse order that must give the same bits as the first (pump's statics are shared by every host of the process, so no case may
// leave state behind that changes another), the gate between a control thread that makes and frees instances and an audio thread that runs them, and the audio-thread
// allocation count of the rest wake.
//   test_pump <dir> [--pin] [--write-gate FILE] [case-substring]
// THE GATE (the lead's decision, 16 Nov, docs/port/U18-NOTES.md): the web app's pump.wasm is built with -ffast-math (reassociated float sums that no native build can reproduce) and is not
// changed; the native build of pump is STRICT. So against the wasm a case must be within kGateDb (-100 dB: max |difference| / the reference's peak) and the worst is printed; a case that
// is bit-exact today stays bit-exact (golden/pump_gate.inc, kPumpGateExact: a case that moves from exact to "within" FAILS); a case where a quantiser follows a nonlinear stage has a
// named rule in kRules (at most N samples above the line, all others within it). Separately, the native render itself is pinned: golden/pump_gate.inc holds the SHA-256 of the native strict
// render of every case (made on Linux x86-64 against the pin-built reference), checked here on every platform.
//   --pin: the reference is the pump.wasm built from the pin (parity-u18.sh's second run): the exact list is enforced and nothing is skipped.
//   without --pin: the reference is the SHIPPED pump/pump.wasm, older than the pin in MASTER (75 parameters against 77, latency 358 against 4454): the cases that depend on MASTER's
//   parameter list / latency are skipped by name with the reason printed (kSkipShipped); every other case is gated the same way except that the exact list is not enforced.
//   --no-skip: run the skipped cases anyway (how the reason for each skip was checked).
//   --write-gate FILE: writes golden/pump_gate.inc from this run (the exact list and the hashes; never hand-edited).
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "fx/fxhost.h"
#include "fx/pumpfx.h"
#include "rt/json.h"
#include "golden/pump_cases.inc"
#include "golden/pump_gate.inc"
#include "sha256.h"

using namespace thunder;

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }
static int fails = 0;
// JSON has no NaN / Infinity: pump-dump writes them as strings
static double numOf(Json v) { if (v.isStr()) { const std::string_view s = v.asStr(); return s == "Infinity" ? INFINITY : s == "-Infinity" ? -INFINITY : NAN; } return v.asNum(); }
static int laneIndex(std::string_view k) { if (k.size() == 3 && k.substr(0, 2) == "sw") return k[2] - '0'; if (k == "flx") return 8; if (k == "gat") return 9; if (k == "rtg") return 10; return -1; }

static void entriesOf(Json ch, std::vector<FxEntry>* out) {
  out->clear();
  for (size_t i = 0; i < ch.size(); i++) {
    Json c = ch.at(i); FxEntry e; std::snprintf(e.key, kFxKey, "%s", std::string(c.str("key")).c_str());
    e.t = kPumpT0 + (int)c.num("pt", -1); e.slot = (int)c.num("slot", -1); e.on = c.boolean("on", true); e.bi = (int)c.num("bi", -1); e.sck = (int)c.num("sck", 0); e.cr = c.boolean("cr"); e.clk = c.boolean("clk");
    Json pv = c.arr("pv"); e.npv = (int)std::min<size_t>(pv.size(), kPumpMaxParams); for (int k = 0; k < e.npv; k++) e.pv[k] = (float)pv.at((size_t)k).asNum();
    out->push_back(e);
  }
}
static void setChain(FxHost& H, Json ch) { std::vector<FxEntry> v; entriesOf(ch, &v); FxPlan* p = H.plan(v.data(), (int)v.size()); H.commit(p); delete p; }

struct Res { bool ran = false, exact = false, postOk = true; double db = -INFINITY, err = 0, ref = 0; long nDiff = 0, n = 0, first = -1; double firstGot = 0, firstWant = 0; int posts = 0; uint32_t remakes = 0, gated = 0; uint64_t hash = 0; long nOver = 0; std::string sha, inSha, why; };

// the gate (see the header): kGateDb below the reference's peak; kRules the named per-case exceptions; kSkipShipped the cases the shipped wasm cannot be the reference of
static const double kGateDb = -100.0;
struct Rule { const char* name; long maxOver; const char* why; };
static const Rule kRules[] = {
  // TUBE (oversampled: a halfband FIR whose sum order differs between the fast-math wasm and the strict native build, ~1e-7 relative) into CRUSH (std::round(x * q) / q): a handful of samples
  // land on the other side of a quantiser step, 14 samples per host at -68.4 dB (U18-NOTES.md, "x_two_hosts_same"). The count of samples above the -100 dB line is MEASURED at 20 (both hosts, L and R; GCC 13);
  // the limit is twice that, 40. A new case of this kind is listed here by name with its measured numbers; there is no wildcard.
  {"x_two_hosts_same", 40, "TUBE into CRUSH: a quantiser step flips in a few samples per host (measured 20 above -100 dB in all, limit 2x)"},
};
static const Rule* ruleOf(const char* nm) { for (const Rule& r : kRules) if (std::strcmp(r.name, nm) == 0) return &r; return nullptr; }
static bool inList(const char* const* l, int n, const char* nm) { for (int i = 0; i < n; i++) if (std::strcmp(l[i], nm) == 0) return true; return false; }
struct Skip { const char* name; const char* why; };
static const Skip kSkipShipped[] = {
  // the shipped pump/pump.wasm is an older pump than the pin the native build links: MASTER has 75 parameters there and 77 in the pin, and its latency is 358 against 4454. Each case below
  // fails against the shipped wasm for that reason only (verified when this list was made: the same case is within the gate against the pin-built wasm).
  {"p_master_def", "MASTER: 75 parameters / latency 358 in the shipped wasm, 77 / 4454 in the pin"},
  {"p_master_alt", "MASTER: 75 parameters / latency 358 in the shipped wasm, 77 / 4454 in the pin"},
};
static const Skip* skipOf(const char* nm) { for (const Skip& k : kSkipShipped) if (std::strcmp(k.name, nm) == 0) return &k; return nullptr; }

static bool near(double a, double b, double tol) { return a == b || std::fabs(a - b) <= tol + 1e-3 * std::fabs(b) || (std::isnan(a) && std::isnan(b)); }

static Res runCase(const std::string& dir, const char* name, std::vector<float>* outAll = nullptr) {
  Res R; std::string js, in, ref;
  if (!readFile(dir + "/" + name + ".json", &js) || !readFile(dir + "/" + name + ".in.f32", &in) || !readFile(dir + "/" + name + ".f32", &ref)) { R.why = "reference files missing"; return R; }
  JsonDoc doc; if (!doc.parse(js)) { R.why = "case json unreadable"; return R; } Json M = doc.root();
  const int n = (int)M.num("n"), nh = (int)M.num("hosts", 1), nt = (int)M.num("ntaps"), ns = (int)M.num("nsends");
  if ((size_t)n * 8 * (size_t)nh != ref.size() || (size_t)n * 4 * (size_t)(2 * nh + nt + ns) != in.size()) { R.why = "reference sizes differ from the case"; return R; }
  const float* I = (const float*)in.data(); const float* J = (const float*)ref.data();
  std::vector<float> aL[2], aR[2]; for (int h = 0; h < nh; h++) { aL[h].assign(I + (size_t)(2 * h) * n, I + (size_t)(2 * h + 1) * n); aR[h].assign(I + (size_t)(2 * h + 1) * n, I + (size_t)(2 * h + 2) * n); }
  const float* srcT[8]; const float* srcS[8];
  FxHost H0(48000.0), H1(48000.0); FxHost* H[2] = {&H0, &H1};
  setChain(H0, M.arr("chain")); if (nh > 1) setChain(H1, M.arr("chain2"));
  { std::vector<int> tm; Json t = M.arr("tapMap"); for (size_t i = 0; i < t.size(); i++) tm.push_back((int)t.at(i).asNum()); for (int h = 0; h < nh; h++) H[h]->setTapMap(tm.data(), (int)tm.size()); }
  Json ops = M.arr("ops"), q = M.arr("q"), msgs = M.arr("msgs"), posted = M.arr("posted"), sz = M.arr("sizes"); const std::string_view bpMode = M.str("bp"); const double bpm = M.num("bpm", 120);
  for (size_t i = 0; i < ops.size(); i++) { Json o = ops.at(i); const std::string_view k = o.at(0).asStr(); if (k == "reset") H0.reset(o.at(1).asNum()); else H0.lane(laneIndex(k), o.at(2).asNum(), o.at(1).asNum()); }
  for (size_t i = 0; i < q.size(); i++) { Json e = q.at(i); Json pm = e.at(1).get("pset"); Json pv = pm.arr("pv"); std::vector<float> v; for (size_t k = 0; k < pv.size(); k++) v.push_back((float)pv.at(k).asNum()); H0.psetPump(e.at(0).asNum(), (int)pm.num("slot"), v.data(), (int)v.size()); }
  size_t pk = 0; int f0 = 0, b = 0, si = 0; std::vector<float> bp((size_t)n + 1);
  while (f0 < n) {
    const int want = (int)sz.at((size_t)(si++ % (int)sz.size())).asNum(), m = std::min(want, n - f0); const double t0 = f0 / 48000.0;
    for (size_t i = 0; i < msgs.size(); i++) { Json e = msgs.at(i); if ((int)e.num("b") != b) continue; Json g = e.get("msg");
      if (g.isArr()) setChain(H0, g); else if (g.get("rest").isBool()) H0.setRest(g.boolean("rest"), t0); else if (g.get("lvl").isBool()) H0.setLevels(g.boolean("lvl")); else if (g.get("tel").isBool()) H0.setTelemetry(g.boolean("tel"));
      else if (g.get("pmidi").isArr()) { Json a = g.get("pmidi"); for (size_t k = 0; k < a.size(); k++) { Json x = a.at(k); H0.pmidi((int)x.at(0).asNum(), x.at(1).asNum(), (int)x.at(2).asNum(), (int)x.at(3).asNum(), (int)x.at(4).asNum()); } }
      else if (g.get("phold").isObj()) { Json x = g.get("phold"); H0.hold(std::string(x.str("key")).c_str(), (int)x.num("kind"), x.num("a"), x.num("b"), x.num("c"), x.num("d")); } }
    if (bpMode == "play") for (int i = 0; i < m; i++) bp[(size_t)i] = (float)std::fmod((double)(f0 + i) * bpm / 60 / 48000.0, 16.0); else for (int i = 0; i < m; i++) bp[(size_t)i] = -1.f;
    const int bpn = bpMode == "const" ? 1 : m;
    for (int t = 0; t < nt; t++) srcT[t] = I + (size_t)(2 * nh + t) * n + f0;
    for (int s = 0; s < ns; s++) srcS[s] = I + (size_t)(2 * nh + nt + s) * n + f0;
    for (int h = 0; h < nh; h++) { H[h]->setSources(srcT, nt, srcS, ns); H[h]->process(aL[h].data() + f0, aR[h].data() + f0, m, t0, bp.data(), bpn); }
    // the posts of this block (node 0): the worklet's {lvl}, {dyn}, {pump}, {plat}, in that order within a kind
    float lv[8]; FxDyn dy[8]; PumpTelOut pt[8]; PumpLat pl[8];
    const bool gotLvl = H0.popLevels(lv); const int nd = H0.popDyn(dy, 8), np = H0.popPumpTel(pt, 8), nl = H0.popLatencies(pl, 8);
    bool wantLvl = false, wantDyn = false, wantPump = false, wantPlat = false;
    for (;;) { if (pk >= posted.size() || (int)posted.at(pk).num("b") != b) break; Json e = posted.at(pk); pk++; if ((int)e.num("node") != 0) continue; Json pm = e.get("m");
      if (pm.get("lvl").isArr()) { wantLvl = true; R.posts++; if (!gotLvl) R.postOk = false; else for (int k = 0; k < 8; k++) if (!near(lv[k], numOf(pm.get("lvl").at((size_t)k)), 1e-4)) R.postOk = false; }
      else if (pm.get("dyn").isArr()) { wantDyn = true; R.posts++; Json d = pm.get("dyn"); if ((int)d.size() != nd) R.postOk = false; else for (int k = 0; k < nd; k++) { Json x = d.at((size_t)k); Json v = x.at(2); if ((int)x.at(0).asNum() != dy[k].slot) R.postOk = false; for (int c = 0; c < 3; c++) if (!near(dy[k].v[c], numOf(v.at((size_t)c)), 0.05)) R.postOk = false; } }
      else if (pm.get("pump").isArr()) { wantPump = true; R.posts++; Json d = pm.get("pump"); if ((int)d.size() != np) R.postOk = false; else for (int k = 0; k < np; k++) { Json x = d.at((size_t)k); if ((int)x.num("slot") != pt[k].slot || (int)x.num("type") != pt[k].type) R.postOk = false; Json t = x.arr("t"); for (int c = 0; c < pt[k].n && c < (int)t.size(); c++) if (!near(pt[k].t[c], numOf(t.at((size_t)c)), 0.05)) { R.postOk = false; if (R.why.empty()) { char w[96]; std::snprintf(w, sizeof w, "{pump} t[%d] got %g want %g", c, pt[k].t[c], numOf(t.at((size_t)c))); R.why = w; } } } }
      else if (pm.get("plat").isArr()) { wantPlat = true; R.posts++; Json d = pm.get("plat"); if ((int)d.size() != nl) R.postOk = false; else for (int k = 0; k < nl; k++) { Json x = d.at((size_t)k); if ((int)x.at(0).asNum() != pl[k].slot || (int)x.at(2).asNum() != pl[k].lat) R.postOk = false; } } }
    if ((gotLvl && !wantLvl) || (nd && !wantDyn) || (np && !wantPump) || (nl && !wantPlat)) R.postOk = false;
    f0 += m; b++;
  }
  if (pk != posted.size()) { for (; pk < posted.size(); pk++) if ((int)posted.at(pk).num("node") == 0) R.postOk = false; }
  R.ran = true; R.remakes = H0.audioRemakes(); R.gated = H0.pumpGated();
  double peak = 0, err = 0; bool exact = true; std::vector<float> all;
  for (int h = 0; h < nh; h++) { all.insert(all.end(), aL[h].begin(), aL[h].end()); all.insert(all.end(), aR[h].begin(), aR[h].end()); }
  for (size_t i = 0; i < all.size(); i++) { const double a = all[i], r = J[i]; peak = std::max(peak, std::fabs(r)); if ((float)a == (float)r || (std::isnan(a) && std::isnan(r))) continue; exact = false; if (R.first < 0) { R.first = (long)i; R.firstGot = a; R.firstWant = r; } R.nDiff++; err = std::max(err, std::fabs(a - r)); }
  { const double thr = peak * std::pow(10.0, kGateDb / 20.0); for (size_t i = 0; i < all.size(); i++) { const double a = all[i], r = J[i]; if ((float)a == (float)r || (std::isnan(a) && std::isnan(r))) continue; if (!(std::fabs(a - r) <= thr)) R.nOver++; } }
  { Sha256 h; h.add((const char*)all.data(), all.size() * 4); R.sha = h.hex(); Sha256 hi; hi.add(in.data(), in.size()); R.inSha = hi.hex(); }
  R.n = (long)all.size(); R.exact = exact; R.ref = peak; R.db = exact ? -INFINITY : 20 * std::log10(err / std::max(peak, 1e-30)); R.err = err; R.hash = fnv(all.data(), all.size());
  if (const char* od = std::getenv("PUMP_OUT")) { std::ofstream o(std::string(od) + "/" + name + ".cpp.f32", std::ios::binary); o.write((const char*)all.data(), (std::streamsize)(all.size() * 4)); }
  if (outAll) *outAll = all;
  return R;
}

// pumpLatencyOf through the plan: the latency LATENCY COMP counts, for each type with its default and preset values; and the table against the meta the reference used
static void latencyTest(const std::string& dir, bool pin) {
  std::string js; if (!readFile(dir + "/lat.json", &js)) { std::printf("lat.json missing  FAIL\n"); fails++; return; }
  JsonDoc doc; if (!doc.parse(js)) { std::printf("lat.json unreadable  FAIL\n"); fails++; return; } Json A = doc.root();
  int bad = 0, nz = 0, info = 0, skippedLat = 0, skippedLatDiff = 0;
  for (size_t i = 0; i < A.size(); i++) { Json x = A.at(i); const int pt = (int)x.num("pt"), lat = (int)x.num("lat");
    FxEntry e; e.t = kPumpT0 + pt; e.slot = 0; e.on = true; e.bi = (int)x.num("bi"); std::snprintf(e.key, kFxKey, "0:p%s", std::string(x.str("type")).c_str());
    Json pv = x.arr("pv"); e.npv = (int)std::min<size_t>(pv.size(), kPumpMaxParams); for (int k = 0; k < e.npv; k++) e.pv[k] = (float)pv.at((size_t)k).asNum();
    FxHost H(48000.0); FxPlan* p = H.plan(&e, 1); H.commit(p); delete p;
    const bool same = H.slotCount() == 1 && (int)H.slotLatency(0) == lat && H.slotBypass(0) == (e.bi >= 0) && PumpSlot::probeLatency(e, 48000.0) == lat;
    if (lat > 0) nz++;
    if (!pin && std::string_view(x.str("type")) == "master") { skippedLat++; if (!same) skippedLatDiff++; continue; }   // shipped wasm's MASTER is older than the pin's (kSkipShipped)
    if (!same) { bad++; std::printf("  latency %-8s %-3s: host %d, reference %d  FAIL\n", std::string(x.str("type")).c_str(), std::string(x.str("set")).c_str(), H.slotCount() ? (int)H.slotLatency(0) : -1, lat); }
    if (std::string_view(fx_pump_key(pt) ? fx_pump_key(pt) : "") != x.str("type") || fx_pump_bypass(pt) != (int)x.num("bi")) { bad++; std::printf("  table: type %d is %s / bypass %d here, %s / %d in the reference's meta  FAIL\n", pt, fx_pump_key(pt), fx_pump_bypass(pt), std::string(x.str("type")).c_str(), (int)x.num("bi")); }
    if (pump::nparams(pt) != (int)x.num("nparams")) { info++; std::printf("  INFO  %s: pump (the pin) has %d parameters, the reference's pump.wasm %d (the pin is newer)\n", std::string(x.str("type")).c_str(), pump::nparams(pt), (int)x.num("nparams")); }
  }
  if (bad) fails++;
  std::printf("latency: %d type x set pairs through plan() + slotLatency() against pumpLatencyOf, %d with a latency above 0, %d differ  %s\n", (int)A.size(), nz, bad, bad ? "FAIL" : "ok");
  if (skippedLat) std::printf("latency: SKIPPED %d MASTER pairs (%d of them differ) against the shipped wasm: its MASTER is older than the pin's (75 against 77 parameters, latency 358 against 4454); gated against the pin-built wasm\n", skippedLat, skippedLatDiff);
}

// The gate: an audio thread runs two pump slots while another thread makes and frees pump instances (the shared handle table is what they would race on). It must not crash, hang
// or produce non-finite sound; blocks the control thread was in are counted (the slots then pass the sound). Run under ThreadSanitizer this is the check of the gate itself.
static void gateTest() {
  FxHost H(48000.0); FxEntry e[2];
  for (int i = 0; i < 2; i++) { e[i].t = kPumpT0 + (i ? 2 : 1); e[i].slot = i; e[i].on = true; e[i].bi = fx_pump_bypass(e[i].t - kPumpT0); std::snprintf(e[i].key, kFxKey, "%d:p%s", i, fx_pump_key(e[i].t - kPumpT0)); }
  FxPlan* p = H.plan(e, 2); H.commit(p); delete p;
  std::atomic<bool> stop{false}; std::atomic<int> made{0};
  std::thread ctl([&] { FxEntry c; c.t = kPumpT0 + 0; c.slot = 0; c.bi = fx_pump_bypass(0); c.on = true; std::snprintf(c.key, kFxKey, "0:peq");
    FxHost other(48000.0);
    while (!stop.load()) { FxPlan* q = other.plan(&c, 1); other.commit(q); delete q; PumpSlot::probeLatency(c, 48000.0); made++; } });
  std::vector<float> L(128), R(128); bool finite = true; int blocks = 0; double t = 0;
  while (made.load() < 60 && blocks < 200000) { for (int i = 0; i < 128; i++) { L[(size_t)i] = R[(size_t)i] = 0.3f * (float)std::sin(0.05 * (blocks * 128 + i)); } H.process(L.data(), R.data(), 128, t, nullptr, 0); t += 128 / 48000.0; blocks++;
    for (int i = 0; i < 128; i++) if (!std::isfinite(L[(size_t)i]) || !std::isfinite(R[(size_t)i])) finite = false; }
  stop = true; ctl.join();
  const bool ok = finite && made.load() >= 60;
  std::printf("gate: %d audio blocks while another thread made and freed %d instances; %u blocks left the pump slots alone; sound finite: %s  %s\n", blocks, made.load(), H.pumpGated(), finite ? "yes" : "NO", ok ? "ok" : "FAIL");
  if (!ok) fails++;
}

int main(int argc, char** argv) {
#ifndef THUNDER_HAVE_PUMP
  (void)argc; (void)argv;
  std::printf("pump: not run on this platform: no pump library (it is not built here), %d cases not run\n", kPumpCaseN);
  return 77;   // ctest: SKIP_RETURN_CODE 77 (shown as not run, never as passed)
#endif
  if (argc < 2) { std::printf("pump: no reference directory (Node not found): %d cases FAIL\n", kPumpCaseN); return 1; }
  const std::string dir = argv[1];
  { std::string w; if (readFile(dir + "/which.txt", &w)) std::printf("reference: pump.wasm and meta as named in %s/which.txt:\n%s", dir.c_str(), w.c_str()); }
  bool pin = false, noSkip = false; const char* writeGate = nullptr; const char* sub = nullptr;
  for (int i = 2; i < argc; i++) { if (!std::strcmp(argv[i], "--pin")) pin = true; else if (!std::strcmp(argv[i], "--no-skip")) noSkip = true; else if (!std::strcmp(argv[i], "--write-gate") && i + 1 < argc) writeGate = argv[++i]; else sub = argv[i]; }
  const bool all = !sub;
  std::printf("pump: reference = %s; gate %.0f dB below each reference's peak%s\n", pin ? "the pump.wasm built from the pin" : "the SHIPPED pump/pump.wasm (MASTER older than the pin's)", kGateDb, pin ? ", the exact list enforced" : "");
  int exact = 0, ran = 0, within = 0, ruled = 0, skipped = 0, hashBad = 0, regress = 0; double worst = -INFINITY; const char* worstName = ""; std::map<std::string, uint64_t> hashes; std::map<std::string, Res> results; int failedCases = 0;
  if (all) latencyTest(dir, pin);
  for (int i = 0; i < kPumpCaseN; i++) {
    const char* nm = kPumpCases[i]; if (sub && !std::strstr(nm, sub)) continue;
    if (!pin && !noSkip) if (const Skip* k = skipOf(nm)) { std::printf("%-20s SKIPPED (shipped wasm): %s\n", nm, k->why); skipped++; continue; }
    const Res R = runCase(dir, nm);
    if (!R.ran) { std::printf("%-20s could not be run: %s  FAIL\n", nm, R.why.c_str()); fails++; failedCases++; continue; }
    ran++; hashes[nm] = R.hash; results[nm] = R;
    const Rule* rule = ruleOf(nm); const bool wasExact = inList(kPumpGateExact, kPumpGateExactN, nm);
    bool within100 = R.exact || R.db <= kGateDb; if (rule) within100 = R.nOver <= rule->maxOver;
    bool ok = within100 && R.postOk; const char* extra = "";
    if (pin && wasExact && !R.exact) { ok = false; regress++; extra = "  (was bit-exact: the exact list is enforced)"; }
    // the native strict render pinned: the committed SHA-256 (Linux x86-64) on every platform
    const char* want = nullptr; const char* wantIn = nullptr; for (int g = 0; g < kPumpGateHashN; g++) if (!std::strcmp(kPumpGateHash[g].name, nm)) { want = kPumpGateHash[g].sha256; wantIn = kPumpGateHash[g].in; }
    bool hashOk = true; if (!writeGate) { hashOk = want && R.sha == want; if (!hashOk) { ok = false; hashBad++; extra = want && wantIn && R.inSha != wantIn ? "  NATIVE HASH DIFFERS (the reference input differs from the committed one: not this platform's render)" : "  NATIVE HASH DIFFERS from the committed one (golden/pump_gate.inc)"; } }
    char db[40]; if (R.exact) std::snprintf(db, sizeof db, "exact"); else if (rule) std::snprintf(db, sizeof db, "%.1f dB (rule)", R.db); else std::snprintf(db, sizeof db, "%.1f dB", R.db);
    if (R.exact) exact++; else if (rule) ruled++; else if (R.db <= kGateDb) within++; if (!R.exact && !rule && R.db > worst) { worst = R.db; worstName = nm; }
    std::printf("%-20s %-16s posts %-3d remakes %-3u%s%s%s%s\n", nm, db, R.posts, R.remakes, R.gated ? " (GATED)" : "", R.postOk ? "" : " POSTS DIFFER", extra, ok ? "" : "  FAIL");
    if (rule) std::printf("    rule: %ld samples of %ld above %.0f dB of the peak (at most %ld allowed; all others within): %s\n", R.nOver, R.n, kGateDb, rule->maxOver, rule->why);
    if (!ok) { fails++; failedCases++; std::printf("    where: %ld of %ld samples differ (%ld above the line); first at %ld: got %.9g want %.9g; largest difference %.3g against a peak of %.3g%s%s\n", R.nDiff, R.n, R.nOver, R.first, R.firstGot, R.firstWant, R.err, R.ref, R.why.empty() ? "" : "; ", R.why.c_str()); }
  }
  if (writeGate) {   // the gate golden from this run: never hand-edited
    std::ofstream o(writeGate); o << "// GENERATED by test_pump --pin --write-gate (GATE_WRITE=<file> tools/parity-u18.sh): the cases that are bit-exact against the pin-built pump.wasm today (kPumpGateExact: a case that\n"
      "// leaves the list is a regression and fails) and the SHA-256 of the native STRICT render of every case (made on Linux x86-64, checked on every platform), with the SHA-256 of the reference's input.\n";
    int ne = 0; for (auto& kv : results) if (kv.second.exact) ne++;
    o << "static const char* const kPumpGateExact[] = {\n"; for (auto& kv : results) if (kv.second.exact) o << "  \"" << kv.first << "\",\n"; o << "};\nstatic const int kPumpGateExactN = " << ne << ";\n";
    o << "struct PumpGateHash { const char* name; const char* sha256; const char* in; };\nstatic const PumpGateHash kPumpGateHash[] = {\n";
    for (auto& kv : results) o << "  {\"" << kv.first << "\", \"" << kv.second.sha << "\", \"" << kv.second.inSha << "\"},\n";
    o << "};\nstatic const int kPumpGateHashN = " << results.size() << ";\n"; std::printf("wrote %s: %d exact, %zu hashes\n", writeGate, ne, results.size());
  }
  // the second pass, in reverse order: the same bits as the first (the library's statics are shared by every host and every case of the process)
  if (all) { int moved = 0, n2 = 0;
    for (int i = kPumpCaseN - 1; i >= 0; i--) { if (!pin && skipOf(kPumpCases[i])) continue; const Res R = runCase(dir, kPumpCases[i]); if (!R.ran) continue; n2++; if (hashes.count(kPumpCases[i]) && hashes[kPumpCases[i]] != R.hash) { moved++; std::printf("  second pass: %s gives other bits than in the first pass  FAIL\n", kPumpCases[i]); } }
    if (moved) fails++;
    std::printf("order: %d cases run again in reverse order, %d gave other bits than the first time  %s\n", n2, moved, moved ? "FAIL" : "ok");
    gateTest(); }
  std::printf("pump: %d cases run, %d bit-exact, %d within %.0f dB, %d by a named rule (%d), %d skipped; worst of the rest %s (%s); %d exact-list regressions, %d native-hash differences, %d failed\n", ran, exact, within, kGateDb, ruled, (int)(sizeof kRules / sizeof kRules[0]), skipped,
    std::isinf(worst) ? "exact" : (std::to_string(worst) + " dB").c_str(), worstName, regress, hashBad, failedCases);
  return fails ? 1 : 0;
}
