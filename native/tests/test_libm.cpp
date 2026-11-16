// U15: js_cos / js_sin / js_tan / js_log (core/src/rt/fdlibm.cpp) against Node's Math.* on 400000 arguments (tools/libm-dump.mjs): every result bit for bit.
// Also prints how many results the C library's own functions get wrong against the same reference (the reason fdlibm is ported: glibc differs in the last bit).
//   test_libm <dir>     a missing reference is a failure
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

#include "rt/jsmath.h"

using namespace thunder;
static bool rd(const std::string& p, std::vector<double>* v) { std::ifstream f(p, std::ios::binary | std::ios::ate); if (!f) return false; const size_t n = (size_t)f.tellg() / 8; v->resize(n); f.seekg(0); f.read((char*)v->data(), (std::streamsize)(n * 8)); return true; }
static bool same(double a, double b) { return std::memcmp(&a, &b, 8) == 0 || (a != a && b != b); }
int main(int argc, char** argv) {
  if (argc < 2) { std::printf("libm: no reference directory: FAIL\n"); return 1; }
  std::vector<double> x, o; if (!rd(std::string(argv[1]) + "/x.f64", &x) || !rd(std::string(argv[1]) + "/o.f64", &o) || x.empty() || o.size() != x.size() * 4) { std::printf("libm: reference files missing: FAIL\n"); return 1; }
  const char* nm[4] = {"cos", "sin", "tan", "log"}; long bad[4] = {0}, libc[4] = {0};
  for (size_t i = 0; i < x.size(); i++) {
    const double v = x[i], a[4] = {js_cos(v), js_sin(v), js_tan(v), js_log(v)}, g[4] = {std::cos(v), std::sin(v), std::tan(v), std::log(v)};
    for (int k = 0; k < 4; k++) { if (!same(a[k], o[i * 4 + (size_t)k])) bad[k]++; if (!same(g[k], o[i * 4 + (size_t)k])) libc[k]++; }
  }
  int fails = 0;
  for (int k = 0; k < 4; k++) { std::printf("js_%s: %zu arguments, %ld differ from Math.%s (the C library's %s: %ld)  %s\n", nm[k], x.size(), bad[k], nm[k], nm[k], libc[k], bad[k] ? "FAIL" : "ok"); if (bad[k]) fails++; }
  return fails ? 1 : 0;
}
