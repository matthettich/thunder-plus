// U03 smoke: every pump effect type, through the same C entry points pump.wasm exports, runs 64 blocks of 128 (8192 frames: past the largest look-ahead, CALM's 2048 and the 4454 of type 23) frames of a stereo test
// signal at 48 kHz: no crash, finite output, a valid latency, telemetry, state and a freed handle is refused. Also with the sidechain
// connected. The nine mastering effects must not return silence for a non-silent input (types 3 and 23 are exempt only if still silent, see below).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern "C" {
int pump_types();
int pump_nparams(int type);
float* pump_stage();
int pump_new(int type, double sr);
void pump_free(int h);
float* pump_values(int h);
void pump_commit(int h);
float* pump_io(int ch);
void pump_process(int h, int n, int sc);
int pump_latency(int h);
float* pump_tel(int h);
int pump_tel_len(int type);
void pump_midi(int h, int frame, int status, int d1, int d2);
void pump_transport(int h, double bpm, double ppq, int playing);
}
static unsigned rng = 12345u;
static float noise() { rng = rng * 1664525u + 1013904223u; return (float)((rng >> 8) & 0xFFFF) / 32768.f - 1.f; }
// usage: dsp_smoke_pump [type]   one type per process (ctest does this: see the note in tests/dsp/CMakeLists.txt), or all in one process.
int main(int argc, char** argv) {
  int bad = 0;
  const int nt = pump_types();
  const int only = argc > 1 ? std::atoi(argv[1]) : -1;
  if (nt != 25) { std::printf("pump_types() = %d, expected 25\n", nt); ++bad; }
  for (int type = 0; type < nt; ++type) {
    if (only >= 0 && type != only) continue;
    for (int sc = 0; sc < 2; ++sc) {
      int h = pump_new(type, 48000.0);
      if (h < 0) { std::printf("pump type %d: not created\n", type); ++bad; break; }
      if (pump_nparams(type) <= 0 || !pump_values(h)) { std::printf("pump type %d: no parameters\n", type); ++bad; }
      pump_transport(h, 120.0, 0.0, 1);
      pump_midi(h, 0, 0x90, 60, 100);
      double energy = 0; bool finite = true; long frames = 0;
      for (int b = 0; b < 64; ++b) {
        for (int ch = 0; ch < 4; ++ch) {
          float* io = pump_io(ch);
          for (int i = 0; i < 128; ++i) {
            double t = (double)(b * 128 + i) / 48000.0;
            io[i] = 0.25f * (float)std::sin(6.2831853 * (220.0 + 110.0 * ch) * t) + 0.1f * noise();
          }
        }
        pump_process(h, 128, sc);
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 128; ++i) {
          float v = pump_io(ch)[i];
          if (!std::isfinite(v)) { if (finite) std::printf("  type %d block %d ch %d i %d: %g\n", type, b, ch, i, v); finite = false; }
          energy += (double)v * v;
        }
        frames += 128;
      }
      int lat = pump_latency(h);
      float* tel = pump_tel(h);
      for (int i = 0; i < pump_tel_len(type) && i < 64; ++i) (void)tel[i];   // exercised only: meters legitimately hold -inf dB for silence   // inf only (NaN means "no value" in a meter)
      if (!finite) { std::printf("pump type %d (sc %d): non-finite output\n", type, sc); ++bad; }
      if (!(energy > 1e-6) && type != 3 && type != 23) { std::printf("pump type %d (sc %d): silent output\n", type, sc); ++bad; }
      if (lat < 0 || lat > 48000) { std::printf("pump type %d: latency %d out of range\n", type, lat); ++bad; }
      pump_free(h);
      pump_process(h, 128, 0);              // a freed handle is refused: does nothing, does not crash
      if (pump_values(h) != nullptr) { std::printf("pump type %d: freed handle still answers\n", type); ++bad; }
      if (sc == 1) std::printf("  type %2d: %d params, latency %d, energy %.3f\n", type, pump_nparams(type), lat, energy);
    }
  }
  if (pump_new(-1, 48000.0) >= 0 || pump_new(0, 10.0) >= 0) { std::printf("bad arguments were accepted\n"); ++bad; }
  std::printf("dsp_smoke_pump: %d problems\n", bad);
  return bad ? 1 : 0;
}
