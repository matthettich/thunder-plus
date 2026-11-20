// U35: MidiSched against the web app's MIDI ledgers (tools/midi-ledger.mjs -> native/tests/golden/midi-ledger-*.json + *.song.json).
// Each case's song is played through Seq + MidiSched on the web harness's cadence; what the fake shell delivers is matched message by message
// (same port, same bytes, within one frame) against the ledger. Whatever does not match must be listed in native/tests/golden/midi-ledger-expect.json
// (case, B number, which messages, why); an unlisted difference fails, and so does a listed one that does not happen. No silent tolerance.
#include <cstdio>
#include <set>

#include "midi_drive.h"

using namespace mdrive;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); fails++; } } while (0)

struct Exp { double ms; int p; int len; uint8_t b[3]; std::string k; };
static std::string hex(const uint8_t* b, int n) { std::string s; char t[4]; for (int i = 0; i < n; i++) { std::snprintf(t, sizeof t, "%02x", b[i]); s += t; } return s; }
static bool scheduledKind(uint8_t k) { return k == MK_EV || k == MK_CLOCK || k == MK_LFO || k == MK_RAMP || k == MK_ECHO || k == MK_START || k == MK_JOIN; }

int main(int argc, char** argv) {
  const std::string root = THUNDER_ROOT; const bool verbose = argc > 1 && std::string(argv[1]) == "-v";
  std::string idx; if (!readFile(root + "/native/tests/golden/midi-ledger-index.json", idx)) { std::printf("FAIL: no index\n"); return 1; }
  JsonDoc id; id.parse(idx); const Json cases = id.root().arr("cases");
  std::string expText; readFile(root + "/native/tests/golden/midi-ledger-expect.json", expText); JsonDoc expDoc; CHECK(expDoc.parse(expText), "midi-ledger-expect.json does not parse");
  int ncase = 0;
  for (size_t ci = 0; ci < cases.size(); ci++) {
    const Json e = cases.at(ci); if (e.str("mode") != "native") continue;     // the -web ledgers are for a port without clear(): the native helper always has it
    const std::string name(e.str("case"));
    Case C; if (!loadCase(root, name, C)) { CHECK(false, "%s: cannot load", name.c_str()); continue; }
    std::string g; readFile(root + "/native/tests/golden/midi-ledger-" + name + ".json", g); JsonDoc gd; gd.parse(g);
    std::vector<Exp> ex; const Json M = gd.root().arr("msgs");
    for (size_t i = 0; i < M.size(); i++) { const Json m = M.at(i); Exp x; x.ms = m.num("ms"); x.p = (int)m.num("p"); x.k = std::string(m.str("k")); const Json b = m.arr("b"); x.len = (int)b.size(); for (int j = 0; j < 3; j++) x.b[j] = j < x.len ? (uint8_t)b.at((size_t)j).asNum() : 0; ex.push_back(x); }
    Drive D; if (!run(D, C)) { CHECK(false, "%s: song does not load", name.c_str()); continue; }
    struct Act { double ms; int p; int len; uint8_t b[3]; uint8_t kind; bool used; };
    std::vector<Act> act;
    for (const Out& o : D.kept) act.push_back({(double)o.frame * 1000.0 / D.sr + (scheduledKind(o.kind) ? C.outLat * 1000 : 0), o.cab, o.len, {o.b[0], o.b[1], o.b[2]}, o.kind, false});
    const double tol = 1000.0 / D.sr + 1e-6;
    // ---- the expected-difference rules of this case (midi-ledger-expect.json), applied to the web's stream ----
    std::vector<char> gone(ex.size(), 0); std::vector<char> addDone;
    struct Rule { std::string B, op, why; int count, count2; std::vector<int> bytes0; double ms, to, a, b; int p; std::string hexb; };
    std::vector<Rule> rules;
    { const Json R = expDoc.root().arr("rules"); for (size_t i = 0; i < R.size(); i++) { const Json r = R.at(i); if (r.str("case") != name) continue;
        Rule x; x.B = std::string(r.str("B")); x.op = std::string(r.str("op")); x.why = std::string(r.str("why")); x.count = (int)r.num("count", -1); x.count2 = (int)r.num("count2", -1); x.ms = r.num("ms"); x.to = r.num("to"); x.a = r.num("from"); x.b = r.num("until"); x.p = (int)r.num("p", -1); x.hexb = std::string(r.str("bytes"));
        const Json b0 = r.arr("bytes0"); for (size_t k = 0; k < b0.size(); k++) x.bytes0.push_back((int)std::strtol(std::string(b0.at(k).asStr()).c_str(), nullptr, 16));
        rules.push_back(x); } }
    std::vector<char> fixedAt(ex.size(), 0);
    int ruleNo = 0;
    for (const Rule& r : rules) {
      ruleNo++; int hit = 0;
      if (r.op == "gear") {                       // B3: the web sends the transport at the booked time; native adds the port's Gear delay to it
        for (size_t i = 0; i < ex.size(); i++) { if (gone[i] || (r.p >= 0 && ex[i].p != r.p)) continue; bool in = false; for (int b0 : r.bytes0) in = in || ex[i].b[0] == b0; if (!in) continue; ex[i].ms -= C.gear[(size_t)ex[i].p]; hit++; }
      } else if (r.op == "stop") {                // B3 + B8: the Stop is stamped like the last tick: stop time + FX latency - Gear delay
        for (size_t i = 0; i < ex.size(); i++) { if (gone[i] || fixedAt[i] || ex[i].k != "stop" || (r.p >= 0 && ex[i].p != r.p)) continue; ex[i].ms += C.fxLat * 1000 - C.gear[(size_t)ex[i].p]; hit++; }
      } else if (r.op == "drop" || r.op == "move") {
        for (size_t i = 0; i < ex.size(); i++) { if (gone[i] || ex[i].p != r.p || hex(ex[i].b, ex[i].len) != r.hexb || std::fabs(ex[i].ms - r.ms) > 0.01) continue; if (r.op == "drop") gone[i] = 1; else { ex[i].ms = r.to; fixedAt[i] = 1; } hit++; break; }
      } else if (r.op == "zone") {                // B2: ticks in [from, until] are checked by the rule below, not one by one
        for (size_t i = 0; i < ex.size(); i++) if (!gone[i] && ex[i].p == r.p && ex[i].b[0] == 0xF8 && ex[i].ms >= r.a && ex[i].ms <= r.b) { gone[i] = 1; hit++; }
        int na = 0; std::vector<double> tk;
        for (Act& x : act) if (x.p == r.p && x.b[0] == 0xF8 && x.ms >= r.a && x.ms <= r.b) { x.used = true; na++; tk.push_back(x.ms); }
        std::sort(tk.begin(), tk.end()); double minI = 1e9, maxI = 0; bool mono = true;
        for (size_t k = 1; k < tk.size(); k++) { const double d = tk[k] - tk[k - 1]; if (d <= 0) mono = false; minI = std::min(minI, d); maxI = std::max(maxI, d); }
        CHECK(na == (r.count2 < 0 ? hit : r.count2), "%s rule %d (%s zone): %d native ticks in the zone, the table says %d (web: %d)", name.c_str(), ruleNo, r.B.c_str(), na, r.count2 < 0 ? hit : r.count2, hit);
        CHECK(mono, "%s rule %d: the ticks in the zone are not monotonic", name.c_str(), ruleNo);
        CHECK(minI >= r.ms - 2e3 / D.sr && maxI <= r.to + 2e3 / D.sr, "%s rule %d: tick intervals %.4f..%.4f outside the two tempos' tick lengths %.4f..%.4f", name.c_str(), ruleNo, minI, maxI, r.ms, r.to);
      } else if (r.op == "add") {                 // native sends a message the web does not
        for (Act& x : act) if (!x.used && x.p == r.p && hex(x.b, x.len) == r.hexb && std::fabs(x.ms - r.ms) <= 0.01) { x.used = true; hit++; break; }
      } else CHECK(false, "%s: unknown rule op %s", name.c_str(), r.op.c_str());
      if (verbose) std::printf("   rule %d %s %s: %d\n", ruleNo, r.B.c_str(), r.op.c_str(), hit);
      CHECK(r.count < 0 ? hit > 0 : hit == r.count, "%s rule %d (%s %s): matched %d messages, the table says %d", name.c_str(), ruleNo, r.B.c_str(), r.op.c_str(), hit, r.count);
    }
    std::vector<int> missing; int matched = 0;
    for (size_t i = 0; i < ex.size(); i++) {
      if (gone[i]) continue;
      int best = -1; double bd = 1e18;
      for (size_t j = 0; j < act.size(); j++) { if (act[j].used || act[j].p != ex[i].p || act[j].len != ex[i].len || std::memcmp(act[j].b, ex[i].b, (size_t)ex[i].len)) continue; const double d = std::fabs(act[j].ms - ex[i].ms); if (d <= tol + 1e-3 && d < bd) { bd = d; best = (int)j; } }
      if (best < 0) missing.push_back((int)i); else { act[(size_t)best].used = true; matched++; }
    }
    size_t extra = 0; for (const Act& a : act) if (!a.used) extra++;
    std::printf("%-20s web %3zu native %3zu exact %3d, %zu rule(s); unexplained: missing %zu extra %zu\n", name.c_str(), ex.size(), act.size(), matched, rules.size(), missing.size(), extra);
    CHECK(missing.empty() && extra == 0, "%s: %zu web messages missing and %zu native messages extra beyond the listed differences (run with -v)", name.c_str(), missing.size(), extra);
    if (verbose) {
      for (int i : missing) std::printf("   MISSING  %10.4f ms  port %d  %s  [%s]\n", ex[(size_t)i].ms, ex[(size_t)i].p, hex(ex[(size_t)i].b, ex[(size_t)i].len).c_str(), ex[(size_t)i].k.c_str());
      for (const Act& a : act) if (!a.used) std::printf("   EXTRA    %10.4f ms  port %d  %s  kind %d\n", a.ms, a.p, hex(a.b, a.len).c_str(), a.kind);
    }
    ncase++;
  }
  std::printf("%d cases, %d failures\n", ncase, fails);
  return fails ? 1 : 0;
}
