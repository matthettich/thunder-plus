// M2: shared by test_live_masterm.cpp and test_live_masterm_alloc.cpp: a song with the mastering chain on (compressor + limiter, optionally the suppressor; or off), no safety limiter, the OUT fader at unity, and a test tap that
// writes a loud two-tone signal into the mixer inputs of tracks 0 and 1 (a quiet stretch and a silent stretch on request).
#ifndef THUNDER_TESTS_MASTERM_COMMON_H
#define THUNDER_TESTS_MASTERM_COMMON_H
#include "live_common.h"
namespace lt {
inline std::string mstJson(bool mstOn, bool sup) {   // eq off: the reference processor in the tests is the MasterProcessor alone
  return std::string("\"mst\":{\"on\":") + (mstOn ? "true" : "false") + ",\"eq\":{\"on\":false},\"sup\":{\"on\":" + (sup ? "true" : "false") + ",\"depth\":0.6},\"comp\":{\"on\":true,\"thr\":-20,\"ratio\":4,\"atk\":5,\"rel\":120},\"lim\":{\"on\":true,\"ceil\":-1,\"look\":3}}";
}
inline std::string mastSong(bool mstOn, bool sup) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"master\":255,\"mout\":255,\"ntr\":8,\"limiter\":false,\"song\":[[-1,-1";
  for (int t = 2; t < kSongNT; t++) s += ",-1";
  s += "]],\"chains\":[[[0,0]]],\"plen\":[8],\"phrases\":[[";
  for (int i = 0; i < 8; i++) s += std::string(i ? "," : "") + "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]";
  s += "]],\"mix\":[";
  for (int t = 0; t < kSongNT; t++) s += std::string(t ? "," : "") + "{\"vol\":224,\"trim\":128,\"pan\":128,\"mute\":false,\"solo\":false}";
  s += "]," + mstJson(mstOn, sup) + "}";
  return s;
}
// mode 0: loud (0.9 and 0.7 sines with a 1.4 pulse every 4096 samples), 1: quiet (x 0.02), 2: silence
struct MTap { int mode = 0; };
inline void mTapFn(void* u, Mixer& m, int64_t q) {
  const int mode = static_cast<MTap*>(u)->mode;
  for (int k = 0; k < 2; k++) for (int i = 0; i < 128; i++) {
    float l = 0, r = 0;
    if (mode != 2) {
      const double x = (double)(q + i), g = mode == 1 ? 0.02 : 1.0;
      l = (float)(g * (k ? 0.5 : 0.9) * std::sin((k ? 0.0431 : 0.0575) * x)); r = (float)(g * (k ? 0.7 : 0.4) * std::sin((k ? 0.0777 : 0.0431) * x));
      if (((q + i) & 4095) == 17) { l += (float)(g * 0.5); r += (float)(g * 0.5); }
    }
    m.inputsL()[k][i] = l; m.inputsR()[k][i] = r;
  }
}
}  // namespace lt
#endif
