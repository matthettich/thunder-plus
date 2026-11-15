// Smoke: Plaits, Rings, Clouds, Elements and two Airwindows/DaisySP effects from thunder-dsp render a few blocks, finite output.
#include <cmath>
#include <cstdio>
extern "C" {
void pl_init(); void pl_set(int, float, float, float, float, float, float, float, float, float, int);
void pl_trigger(float); void pl_render(int); float* pl_out();
void rx_init(); void rx_set(int, int, float, float, float, float, float); void rx_process(int, int); float* rx_in(); float* rx_out();
void cl_init(int, int); void cl_set(float, float, float, float, float, float, float, float, float, int);
void cl_process(int); float* cl_in(); float* cl_l(); float* cl_r();
void el_init(); void el_set(int, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float, float);
void el_gate(int); void el_process(int); float* el_main();
void aw_density(int, float, float, float, float, float, int); void aw_totape(int, float, float, float, float, float, float, float, int);
void ds_chorus(int, float, float, float, float, float, int); float* fx_in(); float* fx_l(); float* fx_r();
}
static int bad = 0;
static void chk(const char* name, const float* x, int n) {
  for (int i = 0; i < n; ++i) if (!std::isfinite(x[i])) { std::printf("%s: non-finite output\n", name); ++bad; return; }
}
int main() {
  pl_init(); pl_set(0, 60.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f, 0.f, 0); pl_trigger(1.f);
  for (int b = 0; b < 4; ++b) { pl_render(128); chk("plaits", pl_out(), 128); }
  rx_init(); rx_set(0, 1, 60.f, 0.5f, 0.5f, 0.5f, 0.5f);
  for (int b = 0; b < 4; ++b) { for (int i = 0; i < 128; ++i) rx_in()[i] = (i == 0 && b == 0) ? 1.f : 0.f; rx_process(128, 1); chk("rings", rx_out(), 128); }
  cl_init(0, 0); cl_set(0.5f, 0.5f, 0.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.f, 0.f, 0);
  for (int b = 0; b < 8; ++b) { for (int i = 0; i < 128; ++i) cl_in()[i] = 0.3f * std::sin(0.05f * float(b * 128 + i)); cl_process(128); chk("clouds", cl_l(), 128); chk("clouds", cl_r(), 128); }
  el_init(); el_set(0, 60.f, 0.5f, 0.5f, 0.f, 0.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f); el_gate(1);
  for (int b = 0; b < 4; ++b) { el_process(128); chk("elements", el_main(), 128); }
  for (int i = 0; i < 128; ++i) fx_in()[i] = 0.3f * std::sin(0.05f * float(i));
  aw_density(1, 48000.f, 0.5f, 0.5f, 0.5f, 0.5f, 128); chk("density", fx_l(), 128);
  aw_totape(1, 48000.f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 128); chk("totape", fx_l(), 128);
  ds_chorus(1, 48000.f, 0.5f, 0.5f, 5.f, 0.2f, 128); chk("chorus", fx_l(), 128); chk("chorus", fx_r(), 128);
  std::printf("dsp_smoke_w: %d problems\n", bad);
  return bad ? 1 : 0;
}
