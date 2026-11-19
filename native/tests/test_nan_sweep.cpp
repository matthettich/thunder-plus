// U31 verification round 2 (defect D1): no crash and no out-of-range read on ANY double reaching ANY parameter of ANY module type, in the ...chains engine and in the waves engine.
// Raw parameters are not clamped (the JS does not clamp them either), and NaN / infinity cannot come from a song file (JSON has neither) but they CAN come from setParam / a live Param event.
// For every module type that has a case "mod/<type>/default" in the committed golden (the patch of that case is the template: it has an input and an output, so the module does work), and for every
// parameter of that type (and `_bn` / `_byp` in chains), a fresh engine is loaded, run for a few blocks, hit with setParam(NaN | +Inf | -Inf | 1e300 | -1e300 | 3e38 | 1e-300) on that one parameter, and run for 25
// more blocks. Then once more with the value on ALL parameters at once. Clean = no crash, and under -DTHUNDER_SANITIZE=ON no ASan / UBSan report (the sanitizer build is where this test earns its keep:
// an index of INT_MIN into a table is a plain segfault in Release only when it lands in unmapped memory). Output that is not finite is NOT a failure (the JS gives NaN too, for most of these).
// Usage: test_nan_sweep chains|waves <golden cases.json> [only-type]
#include <csignal>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "chains/chains.h"
#include "waves/waves.h"
#include "rt/json.h"

using namespace thunder;

static char g_cur[256] = "";
static void onCrash(int sig) { std::fprintf(stderr, "nan_sweep: signal %d while running: %s\n", sig, g_cur); std::_Exit(2); }

static const int kNV = 7;
static const double kVals[kNV] = {NAN, INFINITY, -INFINITY, 1e300, -1e300, 3e38, 1e-300};
static const char* kValNames[kNV] = {"NaN", "+Inf", "-Inf", "1e300", "-1e300", "3e38", "1e-300"};

static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

struct Stats { int runs = 0, nonfinite = 0, failedBlocks = 0, refused = 0; };

static const waves::WInfo* infoOf(bool chains, const std::string& type) {
  if (!chains) return waves::winfo(type);
  for (int i = 0; i < chains::cinfo_count(); i++) if (type == chains::cinfo_at(i).type) return &chains::cinfo_at(i);
  return nullptr;
}

// One run: the parameters `names` of module `id` are set to `val` after 5 blocks; 30 blocks in all.
static void runOne(bool chains, Json cs, const std::string& id, const std::vector<std::string>& names, double val, Stats& st) {
  st.runs++;
  float x[128]; for (int i = 0; i < 128; i++) x[i] = (float)(std::sin(i * .13) * .4 + ((i * 2654435761u) % 1000) / 5000.0 - .1);
  bool fin = true;
  if (chains) {
    chains::EngineOptions o; o.transport.playing = true; waves::Host h; chains::ChainsEngine e(48000, o, &h);
    if (!e.loadPatch(cs.get("patch"))) { st.refused++; return; }
    for (int b = 0; b < 30; b++) {
      if (b == 5) for (const std::string& n : names) e.setParam(id, n, val);
      e.setTrackInputs(x, x, 1); if (!e.process(128)) st.failedBlocks++;
      if (b >= 5) for (int k = 0; k < 128; k++) if (!std::isfinite(e.bus(1)[k]) || !std::isfinite(e.busR(1)[k])) fin = false;
    }
  } else {
    waves::EngineOptions o; o.maxVoices = 4; o.transport.tempo = 120; o.transport.playing = true; Json sd = cs.get("seed"); if (sd.isNum()) { o.hasSeed = true; o.seed = sd.asNum(); }
    waves::Host h; h.setRandom(waves::Host::Rnd::Xorshift, 0x9e3779b9u); waves::WavesEngine e(48000, o, &h);
    if (!e.loadPatch(cs.get("patch"))) { st.refused++; return; }
    const int bus = (int)cs.num("bus", 1);
    for (int b = 0; b < 30; b++) {
      if (b == 0) { e.noteOn(60, 1); e.noteOn(67, .7); }
      if (b == 5) for (const std::string& n : names) e.setParam(id, n, val);
      if (!e.process(128)) st.failedBlocks++;
      if (b >= 5) for (int k = 0; k < 128; k++) if (!std::isfinite(e.bus(bus)[k]) || !std::isfinite(e.busR(bus)[k])) fin = false;
    }
  }
  if (!fin) st.nonfinite++;
}

int main(int argc, char** argv) {
  if (argc < 3) { std::printf("usage: test_nan_sweep chains|waves <cases.json> [type]\n"); return 1; }
  std::signal(SIGSEGV, onCrash); std::signal(SIGABRT, onCrash); std::signal(SIGFPE, onCrash); 
#ifdef SIGBUS
  std::signal(SIGBUS, onCrash);
#endif
  
  const bool chains = std::string(argv[1]) == "chains"; const char* only = argc > 3 ? argv[3] : nullptr;
  const std::string txt = slurp(argv[2]); JsonDoc d; if (!d.parse(txt)) { std::printf("nan_sweep: cannot read %s\n", argv[2]); return 1; }
  Json cases = d.root(); Stats st; int types = 0, params = 0; std::vector<std::string> seen;
  for (size_t i = 0; i < cases.size(); i++) {
    Json c = cases.at(i); const std::string name(c.str("name")); if (name.compare(0, 4, "mod/") != 0 || name.size() < 12 || name.compare(name.size() - 8, 8, "/default") != 0) continue;
    const std::string type = name.substr(4, name.size() - 12); if (only && type != only) continue;
    const waves::WInfo* wi = infoOf(chains, type); if (!wi) continue; if (chains && !chains::chains_have_lib(type)) continue;
    std::string id; Json ms = c.get("patch").get("modules"); for (size_t k = 0; k < ms.size(); k++) if (ms.at(k).str("type") == type) id = std::string(ms.at(k).str("id"));
    if (id.empty()) continue; bool dup = false; for (const std::string& s : seen) if (s == type) dup = true; if (dup) continue; seen.push_back(type);
    types++;
    std::vector<std::string> all; for (int k = 0; k < wi->np; k++) all.push_back(wi->params[k].name);
    if (chains) { all.push_back("_bn"); all.push_back("_byp"); }
    for (int v = 0; v < kNV; v++) {
      for (const std::string& p : all) { std::snprintf(g_cur, sizeof g_cur, "%s %s.%s = %s", chains ? "chains" : "waves", type.c_str(), p.c_str(), kValNames[v]); params++; runOne(chains, c, id, {p}, kVals[v], st); }
      std::snprintf(g_cur, sizeof g_cur, "%s %s all parameters = %s", chains ? "chains" : "waves", type.c_str(), kValNames[v]); runOne(chains, c, id, all, kVals[v], st);
    }
  }
  std::printf("nan_sweep %s: %d module types, %d (parameter, value) pairs, %d runs in all (all-at-once included), %d refused, %d with non-finite output (allowed), %d failed blocks (a module threw in the JS sense)\n", argv[1], types, params, st.runs, st.refused, st.nonfinite, st.failedBlocks);
  if (!types) { std::printf("nan_sweep: no module types found (wrong cases file?)\n"); return 1; }
  return 0;
}
