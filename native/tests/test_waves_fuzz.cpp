// U30: the fuzz of the …waves engine: random hostile patches (cycles with and without fb, missing nodes and ports, duplicate ids, parameter cables onto anything, NaN / inf / huge parameters,
// every module type, unknown types, thousands of nodes, zero nodes), random events while playing (notes with absurd pitches, parameter / text / transport changes, connects, removes, a new patch, setOnly,
// frames 0..128 and beyond), at the three sample rates. A run is clean when nothing crashes (ASan + UBSan in the sanitizer build), every render ends inside the time limit and the buses stay finite where
// the patch is sane enough to say so (not checked: a hostile patch may legitimately make a NaN, as the JS does). Usage: test_waves_fuzz [runs] [seed] [per-run limit in seconds]
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "rt/json.h"
#include "waves/waves.h"

using namespace thunder;
using namespace thunder::waves;

static uint64_t rs = 88172645463325252ull;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static int ri(int n) { return (int)(rnd() % (uint32_t)n); }
static double rv() {
  switch (ri(14)) { case 0: return 0; case 1: return 1; case 2: return -1; case 3: return 1e308; case 4: return -1e308; case 5: return 1e-308; case 6: return 1e999; case 7: return -1e999; case 8: return 48000; case 9: return 0.5;
    case 10: return (double)ri(200) - 100; default: return (rnd() / 4294967296.0) * 4 - 2; }
}
static std::string num(double v) { if (v == 1e999) return "1e999"; if (v == -1e999) return "-1e999"; char b[64]; std::snprintf(b, sizeof b, "%.17g", v); return b; }
static const char* kPortJunk[] = {"in", "out", "outR", "inR", "pitch", "gate", "cv", "p:wave", "p:level", "p:slot", "p:tune", "p:nope", "p:", "toString", "constructor", "__proto__", "", "in1", "in16", "p:time", "p:gain", "p:cutoff"};
static std::string portName(const WInfo* w, bool in) {
  if (w && ri(4)) { const int n = in ? w->nin : w->nout; if (n > 0 && ri(3)) return in ? w->in[ri(n)].name : w->out[ri(n)].name; if (in && w->np > 0 && ri(2)) return std::string("p:") + w->params[ri(w->np)].name; }
  return kPortJunk[ri((int)(sizeof kPortJunk / sizeof *kPortJunk))];
}
static std::string textJunk() {
  static const char* t[] = {"a * k1", "sin(t*440*2*pi)", "((((((((((((((((((((((((((((((((((a))))))))))))))))))))))))))))))))", "a+++", "rnd * 2 - 1", "tanh(a*b)+c-d", "1/0", "mod(a,0)", "pow(-1, 0.5)", "log(0)", "", "\\u00e9\\ud83d\\ude00 a", "clip(a,-1,1)?b:c"};
  return t[ri(13)];
}
static std::string randomPatch(int level) {
  const int nm = level == 3 ? 900 + ri(200) : level == 2 ? 0 : 1 + ri(level == 1 ? 40 : 14);
  std::vector<std::string> ids; std::string s = "{\"modules\":[";
  for (int i = 0; i < nm; i++) {
    const WInfo& w = winfo_at(ri(winfo_count())); const bool bad = ri(30) == 0, simple = level == 3;
    std::string id = (ri(25) == 0 && !ids.empty()) ? ids[(size_t)ri((int)ids.size())] : "m" + std::to_string(i); ids.push_back(id);
    const char* type = bad ? (ri(2) ? "nonsense" : "constructor") : simple ? "atten" : w.type;
    s += std::string(i ? "," : "") + "{\"id\":\"" + id + "\",\"type\":\"" + type + "\",\"params\":{";
    bool f = true;
    for (int k = 0; k < w.np && !simple; k++) if (ri(3) == 0) { s += std::string(f ? "" : ",") + "\"" + w.params[k].name + "\":" + (ri(4) ? num(w.params[k].mn + (w.params[k].mx - w.params[k].mn) * (rnd() / 4294967296.0)) : num(rv())); f = false; }
    s += "}";
    if (w.flags & kWF_Text) s += ",\"text\":\"" + textJunk() + "\"";
    s += "}";
  }
  s += "],\"cables\":[";
  const int nc = level == 3 ? 1000 + ri(3000) : ri(nm * 3 + 3);
  for (int i = 0; i < nc && !ids.empty(); i++) {
    const std::string a = ri(40) == 0 ? "zzz" : ids[(size_t)ri((int)ids.size())], b = ri(40) == 0 ? "zzz" : ids[(size_t)ri((int)ids.size())];
    const WInfo* wa = nullptr; const WInfo* wb = nullptr; for (int k = 0; k < winfo_count(); k++) {}   // (ports are drawn from random modules' tables, mostly wrong ones: that is the point)
    wa = &winfo_at(ri(winfo_count())); wb = &winfo_at(ri(winfo_count()));
    s += std::string(i ? "," : "") + "{\"from\":[\"" + a + "\",\"" + portName(wa, false) + "\"],\"to\":[\"" + b + "\",\"" + portName(wb, true) + "\"]" + (ri(6) == 0 ? ",\"fb\":true" : "") + "}";
  }
  return s + "]" + (ri(5) == 0 ? ",\"transport\":{\"tempo\":" + num(rv()) + "}" : "") + "}";
}
int main(int argc, char** argv) {
  const int runs = argc > 1 ? std::atoi(argv[1]) : 200; if (argc > 2) rs ^= (uint64_t)std::atoll(argv[2]) * 0x9e3779b97f4a7c15ull; const double limit = argc > 3 ? std::atof(argv[3]) : 20;
  int clean = 0, slow = 0, refused = 0; double worst = 0; int worstRun = -1;
  for (int run = 0; run < runs; run++) {
    const auto t0 = std::chrono::steady_clock::now();
    const double srs[] = {44100, 48000, 96000, 48000, 8000, 384000, 5, 1e9, NAN}; const double sr = srs[ri(9) < 6 ? ri(3) : ri(9)];
    EngineOptions o; o.maxVoices = ri(10) == 0 ? ri(200) - 20 : 1 + ri(8); o.hasSeed = ri(2); o.seed = rv(); o.transport.tempo = ri(8) == 0 ? rv() : 120; o.transport.playing = ri(2);
    o.quality = ri(5) == 0 ? Quality::Draft : Quality::Full;
    Host host; if (ri(2)) host.setFixed(rnd() / 4294967296.0); else host.setRandom(ri(2) ? Host::Rnd::Xorshift : Host::Rnd::Mulberry, rnd());
    WavesEngine e(sr, o, &host);
    const int level = ri(40) == 0 ? 3 : ri(12) == 0 ? 2 : ri(3) == 0 ? 1 : 0;
    std::string pj = randomPatch(level); JsonDoc d; if (!d.parse(pj)) { std::printf("fuzz: generated bad json\n"); return 1; }
    if (!e.loadPatch(d.root())) refused++;
    e.setOnly(ri(4) == 0 ? ri(10) - 1 : 0);
    const int blocks = level == 3 ? 3 : 4 + ri(24);
    for (int b = 0; b < blocks; b++) {
      for (int k = ri(3); k > 0; k--) switch (ri(14)) {
        case 0: e.noteOn(rv() * 20, rv()); break; case 1: e.noteOff(rv() * 20); break; case 2: e.noteOn(ri(40) - 12, 1); break; case 3: e.allNotesOff(); break;
        case 4: { const WInfo& w = winfo_at(ri(winfo_count())); if (w.np) e.setParam("m" + std::to_string(ri(10)), w.params[ri(w.np)].name, ri(3) ? rv() : (double)NAN); break; }
        case 5: e.setText("m" + std::to_string(ri(10)), textJunk()); break;
        case 6: e.removeModule("m" + std::to_string(ri(10))); break;
        case 7: e.connect("m" + std::to_string(ri(10)), kPortJunk[ri(22)], "m" + std::to_string(ri(10)), kPortJunk[ri(22)], ri(2)); break;
        case 8: e.disconnect("m" + std::to_string(ri(10)), kPortJunk[ri(22)], "m" + std::to_string(ri(10)), kPortJunk[ri(22)]); break;
        case 9: { std::string p2 = randomPatch(ri(2)); JsonDoc d2; if (d2.parse(p2)) e.loadPatch(d2.root()); break; }
        case 10: e.addModule("m" + std::to_string(ri(10)), winfo_at(ri(winfo_count())).type); break;
        case 11: { Transport t; t.tempo = rv(); t.lpb = rv(); t.ticks = rv(); t.startN = ri(3); t.playing = ri(2); e.setTransport(t); break; }
        case 12: e.setOnly(ri(10) - 1); break; case 13: e.setQuality(ri(2) ? Quality::Draft : Quality::Full); break;
      }
      e.process(ri(10) == 0 ? ri(200) - 20 : ri(8) == 0 ? ri(129) : 128);
      float L[128], R[128]; waves_mixdown(e, L, R, 128);
    }
    e.dispose();
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (dt > worst) { worst = dt; worstRun = run; }
    if (dt > limit) { slow++; std::printf("fuzz: run %d took %.1f s (limit %.0f)\n", run, dt, limit); } else clean++;
  }
  std::printf("fuzz: %d runs, %d clean, %d over the time limit, %d patches refused, slowest run %d: %.2f s\n", runs, clean, slow, refused, worstRun, worst);
  return slow ? 1 : 0;
}
