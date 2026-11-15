// U03 smoke: every pump instrument type, through the same C entry points pump-synth.wasm exports, plays a note (MIDI 60) for 24 blocks of
// 256 frames at 48 kHz: no crash, finite output, and not all silence (FREEZE needs a source: it gets a short noise burst as its File).
#include <cmath>
#include <cstdio>
extern "C" {
int psyn_types();
int psyn_nparams(int type);
int psyn_new(int type, double sr);
void psyn_free(int h);
float* psyn_values(int h);
void psyn_commit(int h);
void psyn_midi(int h, int frame, int status, int d1, int d2);
float* psyn_out(int ch);
void psyn_process(int h, int n);
int psyn_latency(int h);
void psyn_tempo(int h, double bpm);
float* psyn_buf(int n);
void psyn_file(int h, int n, double sr);
}
int main() {
  int bad = 0;
  const int nt = psyn_types();
  if (nt != 13) { std::printf("psyn_types() = %d, expected 13\n", nt); ++bad; }
  for (int type = 0; type < nt; ++type) {
    int h = psyn_new(type, 48000.0);
    if (h < 0) { std::printf("psyn type %d: not created\n", type); ++bad; continue; }
    if (psyn_nparams(type) <= 0 || !psyn_values(h)) { std::printf("psyn type %d: no parameters\n", type); ++bad; }
    psyn_tempo(h, 120.0);
    if (type == 11) {   // FREEZE: a File source
      float* b = psyn_buf(48000);
      unsigned r = 1u;
      for (int i = 0; b && i < 48000; ++i) { r = r * 1664525u + 1013904223u; b[i] = 0.3f * ((float)((r >> 8) & 0xFFFF) / 32768.f - 1.f); }
      psyn_file(h, 48000, 48000.0);
    }
    psyn_midi(h, 0, 0x90, 60, 110);
    double energy = 0; bool finite = true;
    for (int blk = 0; blk < 24; ++blk) {
      if (blk == 16) psyn_midi(h, 0, 0x80, 60, 0);
      psyn_process(h, 256);
      for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 256; ++i) {
        float v = psyn_out(ch)[i];
        if (!std::isfinite(v)) finite = false;
        energy += (double)v * v;
      }
    }
    if (!finite) { std::printf("psyn type %d: non-finite output\n", type); ++bad; }
    if (!(energy > 1e-9)) { std::printf("psyn type %d: silent\n", type); ++bad; }
    std::printf("  type %2d: %d params, latency %d, energy %.4f\n", type, psyn_nparams(type), psyn_latency(h), energy);
    psyn_free(h);
    psyn_process(h, 256);
    if (psyn_values(h) != nullptr) { std::printf("psyn type %d: freed handle still answers\n", type); ++bad; }
  }
  std::printf("dsp_smoke_pump_synth: %d problems\n", bad);
  return bad ? 1 : 0;
}
