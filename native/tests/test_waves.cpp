// U30: the …waves engine (core/src/waves) against waves/waves-worklet.js. Grows with the unit; see docs/port/U30-NOTES.md for what each section gates.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "rt/json.h"
#include "waves/waves.h"

using namespace thunder;
using namespace thunder::waves;

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

static uint32_t fnv(const std::vector<const std::vector<float>*>& arrs) {
  uint32_t h = 0x811c9dc5u;
  for (auto* a : arrs) { const unsigned char* u = (const unsigned char*)a->data(); for (size_t i = 0; i < a->size() * 4; i++) { h ^= u[i]; h *= 0x01000193u; } }
  return h;
}
static std::string hex(uint32_t h) { char b[16]; std::snprintf(b, sizeof b, "%08x", h); return b; }

// ---- the processor rows of tests/golden/waves-processor.json ----
static std::string procPatch(double gain) {
  char g[64]; std::snprintf(g, sizeof g, "%.17g", gain);
  return std::string("{\"modules\":[{\"id\":\"h\",\"type\":\"hostIn\",\"params\":{\"transpose\":0}},{\"id\":\"v\",\"type\":\"vco\",\"params\":{\"wave\":1,\"tune\":0,\"fine\":0}},"
    "{\"id\":\"e\",\"type\":\"adsr\",\"params\":{\"a\":0.005,\"d\":0.2,\"s\":0.7,\"r\":0.3}},{\"id\":\"g\",\"type\":\"vca\",\"params\":{\"gain\":") + g +
    "}},{\"id\":\"o\",\"type\":\"out\",\"params\":{\"slot\":1,\"level\":1}}],\"cables\":[{\"from\":[\"h\",\"pitch\"],\"to\":[\"v\",\"pitch\"]},{\"from\":[\"h\",\"gate\"],\"to\":[\"e\",\"gate\"]},"
    "{\"from\":[\"v\",\"out\"],\"to\":[\"g\",\"in\"]},{\"from\":[\"e\",\"out\"],\"to\":[\"g\",\"cv\"]},{\"from\":[\"g\",\"out\"],\"to\":[\"o\",\"in\"]}]}";
}
static std::string driveProcessor(double gain) {
  EngineOptions o; o.maxVoices = 4; o.transport.tempo = 120; o.transport.playing = true;
  Host host; host.setRandom(Host::Rnd::Xorshift, 0x9e3779b9u);
  WavesEngine e(48000, o, &host);
  JsonDoc d; const std::string t = procPatch(gain); if (!d.parse(t)) { CHECK(false, "patch parse"); return ""; }
  CHECK(e.loadPatch(d.root()), "loadPatch");
  const int blocks = 160; std::vector<float> L((size_t)blocks * 128), R((size_t)blocks * 128);
  for (int b = 0; b < blocks; b++) {
    if (b == 0) { e.noteOn(60, 1); e.noteOn(64, 1); e.noteOn(67, 1); }
    CHECK(e.process(128), "process");
    waves_mixdown(e, &L[(size_t)b * 128], &R[(size_t)b * 128], 128);
  }
  return hex(fnv({&L, &R}));
}
int main() {
  const std::string q = driveProcessor(.05), h = driveProcessor(1);
  std::printf("processor quiet %s hot %s\n", q.c_str(), h.c_str());
  CHECK(q == "9f094f39", "quiet row %s", q.c_str());
  CHECK(h == "5d99c475", "hot row %s", h.c_str());
  std::printf(fails ? "test_waves: %d FAILED\n" : "test_waves: ok\n", fails);
  return fails ? 1 : 0;
}
