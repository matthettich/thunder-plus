// Smoke: a Marbles (0) and a Tides 2 (1) module run 8 blocks; outputs must be finite (mods_process returns 1).
#include <cmath>
#include <cstdio>
extern "C" {
int mods_new(int type, float sr, unsigned seed);
void mods_free(int h);
float* mods_params(int h);
float* mods_in();
float* mods_out();
int mods_process(int h, int n);
}
int main() {
  int bad = 0;
  for (int type = 0; type < 2; ++type) {
    int h = mods_new(type, 48000.f, 1234u);
    if (h < 0) { std::printf("mods %d: not created\n", type); ++bad; continue; }
    float* p = mods_params(h);
    for (int i = 0; i < 32; ++i) p[i] = 0.5f;
    for (int b = 0; b < 8; ++b) {
      for (int i = 0; i < 128; ++i) mods_in()[i] = ((b * 128 + i) % 6000) < 100 ? 1.f : 0.f;   // a gate on input 0
      if (!mods_process(h, 128)) { std::printf("mods %d: bad output\n", type); ++bad; break; }
    }
    mods_free(h);
  }
  std::printf("dsp_smoke_mods: %d problems\n", bad);
  return bad ? 1 : 0;
}
