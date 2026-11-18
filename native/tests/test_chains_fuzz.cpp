// U31: the fuzz of the …chains engine: random hostile patches (every …chains module type, several extreme fields at once: parameters at their limits or NaN / inf, Track In / Chain Out tracks far out of range,
// Send / Receive keys beyond the 8 + 128, bypass numbers duplicated or out of range, cables onto any port, cycles, bus into bus, thousands of nodes, unknown types, Expression text junk), random events while
// running (parameters, text, connects, removes, a new patch, the bypass switches, transport, taps / sends / track inputs of any length and with NaN / huge samples, a tap or bus map of any size, telemetry, focus,
// frames 0..128 and beyond, Resample takes taken or left) and the live runner with events posted at absurd times and patch swaps, at several sample rates. A run is clean when nothing crashes (ASan + UBSan in the
// sanitizer build) and ends inside the time limit. Usage: test_chains_fuzz [runs] [seed] [per-run limit in seconds]
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "chains/chains.h"
#include "rt/json.h"

using namespace thunder;
using namespace thunder::chains;
static uint64_t rs = 88172645463325252ull;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return (uint32_t)(rs >> 11); }
static int ri(int n) { return (int)(rnd() % (uint32_t)n); }
static double rv() {
  switch (ri(14)) { case 0: return 0; case 1: return 1; case 2: return -1; case 3: return 1e308; case 4: return -1e308; case 5: return 1e-308; case 6: return HUGE_VAL; case 7: return -HUGE_VAL; case 8: return 48000; case 9: return 0.5;
    case 10: return (double)ri(200) - 100; default: return (rnd() / 4294967296.0) * 4 - 2; }
}
static std::string num(double v) { if (v == HUGE_VAL) return "1e999"; if (v == -HUGE_VAL) return "-1e999"; char b[64]; std::snprintf(b, sizeof b, "%.17g", v); return b; }
static const char* kPortJunk[] = {"in", "out", "in2R", "in32", "inB", "inBR", "sck", "snd", "track", "outR", "inR", "pitch", "gate", "cv", "p:wave", "p:level", "p:slot", "p:tune", "p:nope", "p:", "toString", "constructor", "__proto__", "", "in1", "in16", "p:time", "p:gain", "p:cutoff"};
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
    const WInfo& w = cinfo_at(ri(cinfo_count())); const bool bad = ri(30) == 0, simple = level == 3;
    std::string id = (ri(25) == 0 && !ids.empty()) ? ids[(size_t)ri((int)ids.size())] : "m" + std::to_string(i); ids.push_back(id);
    const char* type = bad ? (ri(2) ? "nonsense" : "constructor") : simple ? "vca" : w.type;
    s += std::string(i ? "," : "") + "{\"id\":\"" + id + "\",\"type\":\"" + type + "\",\"params\":{";
    bool f = true;
    const bool extreme = ri(4) == 0;   // several extreme fields at once
    for (int k = 0; k < w.np && !simple; k++) if (extreme ? ri(2) == 0 : ri(3) == 0) { s += std::string(f ? "" : ",") + "\"" + w.params[k].name + "\":" + (extreme ? num(ri(3) ? (ri(2) ? w.params[k].mx : w.params[k].mn) : rv()) : ri(4) ? num(w.params[k].mn + (w.params[k].mx - w.params[k].mn) * (rnd() / 4294967296.0)) : num(rv())); f = false; }
    if (!simple && ri(5) == 0) { s += std::string(f ? "" : ",") + "\"_bn\":" + num(ri(3) ? (double)ri(20) : rv()); f = false; }
    if (!simple && ri(6) == 0) { s += std::string(f ? "" : ",") + "\"track\":" + num(rv() * 100); f = false; }
    s += "}";
    if (w.flags & kWF_Text) s += ",\"text\":\"" + textJunk() + "\"";
    s += "}";
  }
  s += "],\"cables\":[";
  const int nc = level == 3 ? 1000 + ri(3000) : ri(nm * 3 + 3);
  for (int i = 0; i < nc && !ids.empty(); i++) {
    const std::string a = ri(40) == 0 ? "zzz" : ids[(size_t)ri((int)ids.size())], b = ri(40) == 0 ? "zzz" : ids[(size_t)ri((int)ids.size())];
    const WInfo* wa = &cinfo_at(ri(cinfo_count())); const WInfo* wb = &cinfo_at(ri(cinfo_count()));
    s += std::string(i ? "," : "") + "{\"from\":[\"" + a + "\",\"" + portName(wa, false) + "\"],\"to\":[\"" + b + "\",\"" + portName(wb, true) + "\"]" + (ri(6) == 0 ? ",\"fb\":true" : "") + "}";
  }
  return s + "]" + (ri(5) == 0 ? ",\"transport\":{\"tempo\":" + num(rv()) + "}" : "") + "}";
}
static void fillBuf(std::vector<float>& v) { for (float& x : v) { const int k = ri(40); x = k == 0 ? NAN : k == 1 ? 1e30f : k == 2 ? -INFINITY : (float)(rnd() / 4294967296.0 - .5); } }
int main(int argc, char** argv) {
  const int runs = argc > 1 ? std::atoi(argv[1]) : 200; if (argc > 2) rs ^= (uint64_t)std::atoll(argv[2]) * 0x9e3779b97f4a7c15ull; const double limit = argc > 3 ? std::atof(argv[3]) : 20;
  int clean = 0, slow = 0, refused = 0; double worst = 0; int worstRun = -1;
  for (int run = 0; run < runs; run++) {
    const auto t0 = std::chrono::steady_clock::now();
    const double srs[] = {44100, 48000, 96000, 192000, 8000, 384000, 5, 1e9, NAN}; const double sr = srs[ri(9) < 6 ? ri(4) : ri(9)];
    EngineOptions o; o.focus = ri(3) == 0 ? ri(140) - 5 : 0; o.transport.tempo = ri(8) == 0 ? rv() : 120; o.transport.playing = ri(2);
    Host host; if (ri(2)) host.setFixed(rnd() / 4294967296.0); else host.setRandom(ri(2) ? Host::Rnd::Xorshift : Host::Rnd::Mulberry, rnd());
    const int level = ri(40) == 0 ? 3 : ri(12) == 0 ? 2 : ri(3) == 0 ? 1 : 0;
    std::string pj = randomPatch(level); JsonDoc d; if (!d.parse(pj)) { std::printf("fuzz: generated bad json\n"); return 1; }
    const int mode = ri(3);
    if (mode < 2) {
      ChainsEngine e(sr, o, &host); e.setInlineService(ri(2));
      if (!e.loadPatch(d.root())) refused++;
      if (ri(3) == 0) { std::vector<int> bm; for (int k = ri(40); k > 0; k--) bm.push_back(ri(3) ? ri(20) : ri(400) - 100); e.setBusMap(bm.data(), (int)bm.size()); }
      if (ri(3) == 0) { std::vector<int> tm; for (int k = ri(140); k > 0; k--) tm.push_back(ri(400) - 100); e.setTapMap(tm.data(), (int)tm.size()); }
      if (ri(4) == 0) e.setTelemetry(true);
      const int blocks = level == 3 ? 3 : 4 + ri(24);
      std::vector<float> a(128 * 8), b(128 * 8), t(128 * 140), s(128 * 9); fillBuf(a); fillBuf(b); fillBuf(t); fillBuf(s);
      for (int bl = 0; bl < blocks; bl++) {
        for (int k = ri(4); k > 0; k--) switch (ri(15)) {
          case 0: { const WInfo& w = cinfo_at(ri(cinfo_count())); if (w.np) e.setParam("m" + std::to_string(ri(10)), w.params[ri(w.np)].name, ri(3) ? rv() : (double)NAN); break; }
          case 1: e.setText("m" + std::to_string(ri(10)), textJunk()); break;
          case 2: e.removeModule("m" + std::to_string(ri(10))); break;
          case 3: e.connect("m" + std::to_string(ri(10)), kPortJunk[ri(25)], "m" + std::to_string(ri(10)), kPortJunk[ri(25)], ri(2)); break;
          case 4: e.disconnect("m" + std::to_string(ri(10)), kPortJunk[ri(25)], "m" + std::to_string(ri(10)), kPortJunk[ri(25)]); break;
          case 5: { std::string p2 = randomPatch(ri(2)); JsonDoc d2; if (d2.parse(p2)) e.loadPatch(d2.root()); break; }
          case 6: e.addModule("m" + std::to_string(ri(10)), cinfo_at(ri(cinfo_count())).type); break;
          case 7: { Transport tr; tr.tempo = rv(); tr.lpb = rv(); tr.ticks = rv(); tr.startN = ri(3); tr.playing = ri(2); e.setTransport(tr); break; }
          case 8: e.setBypassSwitch(ri(20) - 2, ri(3) ? (double)ri(4) - 1 : rv()); break;
          case 9: e.setFocus(ri(140) - 5); break;
          case 10: e.setQuality(ri(2) ? Quality::Draft : Quality::Full); break;
          case 11: e.setTelemetry(ri(2)); break;
          case 12: { ResampleTake tk; while (e.popTake(&tk)) { if (ri(4)) e.releaseTake(tk); else break; } break; }
          case 13: e.serviceSpares(); break;
          case 14: fillBuf(a); fillBuf(t); break;
        }
        const int ch = 1 + ri(8); e.setTrackInputs(a.data(), ri(4) ? b.data() : nullptr, ch); const float* tp[140]; const int nt = ri(140); for (int k = 0; k < nt; k++) tp[k] = ri(5) ? &t[(size_t)k * 128] : nullptr; e.setTaps(ri(6) ? tp : nullptr, nt);
        const float* sp[9]; const int ns = ri(10); for (int k = 0; k < ns; k++) sp[k] = ri(4) ? &s[(size_t)k * 128] : nullptr; e.setSendIn(ri(6) ? sp : nullptr, ns); e.setSidechain(ri(2) ? a.data() : nullptr, ri(2) ? b.data() : nullptr, ri(3) ? 128 : ri(300));
        e.process(ri(10) == 0 ? ri(200) - 20 : ri(8) == 0 ? ri(129) : 128); e.endStandaloneBlock();
        float L[128], R[128]; chains_mixdown(e, L, R, 128);
      }
      e.dispose();
    } else {
      ChainsLiveConfig cfg; cfg.taps = ri(5); cfg.sc = ri(2); cfg.sends = ri(2); cfg.feeds = ri(4);   // the host feeds what the configuration declares (the runner's contract): the pointer arrays below have that many entries
      ChainsLive live(sr, o.focus, cfg, &host);
      if (!live.node().engine().loadPatch(d.root())) refused++;
      for (int k = ri(3); k > 0; k--) { std::string p2 = randomPatch(ri(2)); JsonDoc d2; if (d2.parse(p2)) live.preparePatch(d2.root(), (int64_t)(ri(3) ? ri(3000) : rnd())); }
      std::vector<float> x(4096), y(4096), L(4096), R(4096), so(8 * 4096); fillBuf(x); fillBuf(y);
      const float* tps[4] = {x.data(), y.data(), x.data(), y.data()}; const float* snds[8]; for (int k = 0; k < 8; k++) snds[k] = ri(3) ? y.data() : nullptr; const float* fd[8] = {x.data(), y.data(), y.data(), x.data(), x.data(), x.data(), y.data(), y.data()};
      float* sop[8]; for (int k = 0; k < 8; k++) sop[k] = &so[(size_t)k * 4096];
      const int blocks = level == 3 ? 3 : 8 + ri(40);
      for (int bl = 0; bl < blocks; bl++) {
        const int64_t now = live.enginePos();
        for (int k = ri(4); k > 0; k--) switch (ri(5)) {
          case 0: { const WInfo& w = cinfo_at(ri(cinfo_count())); if (w.np) live.postParam(("m" + std::to_string(ri(10))).c_str(), w.params[ri(w.np)].name, rv(), now + (ri(4) ? ri(600) : (int64_t)rnd() - 5000)); break; }
          case 1: { ChainsEvent ev; ev.kind = ChainsEvent::Bypass; ev.at = now + ri(500) - 20; ev.a = ri(20) - 2; ev.b = rv(); live.post(ev); break; }
          case 2: { ChainsEvent ev; ev.kind = ChainsEvent::Tempo; ev.at = now + ri(500); ev.a = rv(); live.post(ev); break; }
          case 3: { std::string p2 = randomPatch(0); JsonDoc d2; if (d2.parse(p2)) live.preparePatch(d2.root(), now + ri(900)); break; }
          case 4: live.service(); break;
        }
        const int n = ri(6) == 0 ? ri(1100) - 50 : ri(4) == 0 ? 1 + ri(300) : 128; NodeIn ni; ni.inL = x.data(); ni.inR = ri(2) ? y.data() : nullptr; ni.scL = ri(2) ? x.data() : nullptr; ni.taps = tps; ni.nTaps = cfg.taps; ni.sends = snds; ni.nSends = cfg.sends ? 8 : 0; ni.feeds = fd; ni.nFeeds = 2 * cfg.feeds;
        NodeOut no; no.L = L.data(); no.R = ri(2) ? R.data() : nullptr; no.sends = sop; no.nSends = 8;
        live.process(ni, no, n < 0 ? 0 : n);
      }
    }
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (dt > worst) { worst = dt; worstRun = run; }
    if (dt > limit) { slow++; std::printf("fuzz: run %d took %.1f s (limit %.0f)\n", run, dt, limit); } else clean++;
  }
  std::printf("fuzz: %d runs, %d clean, %d over the time limit, %d patches refused, slowest run %d: %.2f s\n", runs, clean, slow, refused, worstRun, worst);
  return slow ? 1 : 0;
}
