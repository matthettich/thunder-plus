// M3: shared by test_live_chlvl.cpp and test_live_chlvl_alloc.cpp: a song whose chains patch has a pass-through node on tracks 0 and 1 (Track In n -> Chain Out n at level 0.5), the CHN flags on for both,
// and a test tap that writes a known signal into the mixer inputs of tracks 0, 1 and 5 (track 5 has no node).
#ifndef THUNDER_TESTS_CHLVL_COMMON_H
#define THUNDER_TESTS_CHLVL_COMMON_H
#include "live_common.h"
namespace lt {
inline std::string chlvlPatch() {
  std::string m, c;
  for (int n = 1; n <= 2; n++) {
    const std::string s = std::to_string(n);
    m += std::string(n > 1 ? "," : "") + "{\"id\":\"i" + s + "\",\"type\":\"trackIn\",\"params\":{\"track\":" + s + ",\"pre\":0}},{\"id\":\"o" + s + "\",\"type\":\"chainOut\",\"params\":{\"track\":" + s + ",\"level\":0.5}}";
    c += std::string(n > 1 ? "," : "") + "{\"from\":[\"i" + s + "\",\"out\"],\"to\":[\"o" + s + "\",\"in\"]},{\"from\":[\"i" + s + "\",\"outR\"],\"to\":[\"o" + s + "\",\"inR\"]}";
  }
  return "{\"format\":\"chains.patch\",\"version\":1,\"modules\":[" + m + "],\"cables\":[" + c + "]}";
}
inline std::string chlvlSong() {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"master\":255,\"mout\":255,\"ntr\":8,\"song\":[[-1,-1";
  for (int t = 2; t < kSongNT; t++) s += ",-1";
  s += "]],\"chains\":[[[0,0]]],\"plen\":[8],\"phrases\":[[";
  for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]";
  s += "]],\"mix\":[";
  for (int t = 0; t < kSongNT; t++) s += std::string(t ? "," : "") + "{\"vol\":224,\"trim\":128,\"pan\":128,\"mute\":false,\"solo\":false}";
  s += "],\"chainOn\":[true,true";
  for (int t = 2; t < kSongNT; t++) s += ",false";
  s += "],\"chainPatch\":" + chlvlPatch() + "}";
  return s;
}
// the input: a 0.05 sine with one sample of pulse per quantum, 0.8 on L and 0.3 on R (track 0), half of that on track 1, track 5 the same as track 0. `on` false: silence.
struct ChTap { bool on = true; };
inline void chTapFn(void* u, Mixer& m, int64_t q) {
  const bool on = static_cast<ChTap*>(u)->on;
  const int tr[3] = {0, 1, 5}; const float g[3] = {1.f, 0.5f, 1.f};
  for (int k = 0; k < 3; k++) for (int i = 0; i < 128; i++) {
    float l = 0, r = 0;
    if (on) { const double x = (double)(q + i); l = g[k] * (float)(0.05 * std::sin(0.0575 * x)); r = g[k] * (float)(0.05 * std::sin(0.0431 * x)); if (i == 17) { l = g[k] * 0.8f; r = g[k] * 0.3f; } }
    m.inputsL()[tr[k]][i] = l; m.inputsR()[tr[k]][i] = r;
  }
}
}  // namespace lt
#endif
