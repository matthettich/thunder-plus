// Smoke: make every tfx effect (0..33), run 8 blocks of a sine through it, check the output is finite. Not a parity test.
#include <cmath>
#include <cstdio>
extern "C" {
int tfx_new(int type, float sr);
void tfx_free(int h);
float* tfx_params(int h);
float* tfx_l();
float* tfx_r();
void tfx_process(int h, int n);
}
int main() {
  int bad = 0, made = 0;
  for (int type = 0; type <= 33; ++type) {
    int h = tfx_new(type, 48000.f);
    if (h < 0) { std::printf("tfx %d: not created\n", type); ++bad; continue; }
    ++made;
    float* p = tfx_params(h);
    for (int i = 0; i < 12; ++i) p[i] = 0.5f;
    if (type == 0) { p[0] = 0.f; p[1] = 1.f; p[2] = 60.f; }   // Rings: model and polyphony are integers (polyphony 0 divides by zero)
    for (int b = 0; b < 8; ++b) {
      for (int i = 0; i < 128; ++i) { float s = 0.25f * std::sin(0.05f * float(b * 128 + i)); tfx_l()[i] = s; tfx_r()[i] = s; }
      tfx_process(h, 128);
      for (int i = 0; i < 128; ++i)
        if (!std::isfinite(tfx_l()[i]) || !std::isfinite(tfx_r()[i])) { std::printf("tfx %d: non-finite output\n", type); ++bad; b = 8; break; }
    }
    tfx_free(h);
  }
  std::printf("dsp_smoke_tfx: %d effects ran, %d problems\n", made, bad);
  return (bad == 0 && made == 34) ? 0 : 1;
}
