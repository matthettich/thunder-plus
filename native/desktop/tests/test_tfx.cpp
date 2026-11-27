// W-TFX: the shell's library, as built here, hosts the track effects: THUNDER_HAVE_TFX is set, the live caps word says so, and all 34 effects of the core's host run (finite, not silent)
// through the effects library the build linked. THUNDER_EXPECT_TFX=1 (set by CMake when the effects library is a prebuilt clang-cl one, i.e. the Windows shell) turns "TFX: no" into a failure.
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "check.h"
#include "fx/fxhost.h"
#include "thunder/live.h"
int main() {
  const char* ex = std::getenv("THUNDER_EXPECT_TFX");
#ifndef THUNDER_HAVE_TFX
  std::printf("TFX: no\n");
  CHECK(!(ex && ex[0] == '1'), "THUNDER_EXPECT_TFX is set but this build has no effects library");
  DONE("tfx");
#else
  std::printf("TFX: yes\n");
  CHECK((thunder_live_caps() & THUNDER_LIVE_CAP_EFFECTS) != 0, "caps: effects");
  using namespace thunder;
  int ok = 0;
  for (int id = 0; id < 34; id++) {
    FxHost H(48000.0); FxEntry e; e.t = id; e.slot = 0; e.on = true; std::snprintf(e.key, kFxKey, "0:%s", fx_key_of(id)); e.nvals = fx_param_count(id);
    for (int k = 0; k < e.nvals; k++) e.vals[k] = fx_param_default(id, k);
    fx_map(id, e.vals, 48000.0, 120.0, 0.0, e.p);
    FxPlan* p = H.plan(&e, 1); H.commit(p); delete p;
    double sum = 0; bool fin = true; uint32_t sd = 99;
    for (int b = 0; b < 400; b++) {
      float L[128], R[128];
      for (int i = 0; i < 128; i++) { sd = sd * 1664525u + 1013904223u; L[i] = (float)(sd >> 8) / 16777216.f - 0.5f; sd = sd * 1664525u + 1013904223u; R[i] = (float)(sd >> 8) / 16777216.f - 0.5f; }
      H.process(L, R, 128, b * 128 / 48000.0);
      for (int i = 0; i < 128; i++) { if (!std::isfinite(L[i]) || !std::isfinite(R[i])) fin = false; sum += std::fabs(L[i]) + std::fabs(R[i]); }
    }
    CHECK(fin && sum > 0, "effect %d (%s): finite %d, sum %g", id, fx_key_of(id), (int)fin, sum);
    if (fin && sum > 0) ok++;
  }
  std::printf("tfx: %d of 34 effects ran\n", ok);
  DONE("tfx");
#endif
}
