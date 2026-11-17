// FFTR's tables and the shared cache (see fftr.h). The constructor is the JS's `constructor(n)`, line for line.
#include "modfx/fftr.h"

#include <map>
#include <memory>
#include <mutex>

namespace thunder {
namespace modfx {

FFTR::FFTR(int n_) {
  const int mm = n_ >> 1; int bits = 0; while ((1 << bits) < mm) bits++;
  n = n_; m = mm; passes = bits;
  rev_.assign((size_t)mm, 0);
  for (int i = 0; i < mm; i++) { int r = 0; for (int b = 0; b < bits; b++) r |= ((i >> b) & 1) << (bits - 1 - b); rev_[(size_t)i] = r; }
  // the complex FFT's twiddles, e^(-2 pi i j / size) for each pass, laid end to end (pass with half-size h at offset h)
  const size_t cs = (size_t)(mm > 2 ? mm : 2);
  c_.assign(cs, 0.0); s_.assign(cs, 0.0);
  for (int h = 1; h < mm; h <<= 1) for (int j = 0; j < h; j++) { c_[(size_t)(h + j)] = js_cos(kPi * j / h); s_[(size_t)(h + j)] = js_sin(kPi * j / h); }
  sc_.assign((size_t)mm + 1, 0.0); ss_.assign((size_t)mm + 1, 0.0);   // the split's, e^(-i pi k / m)
  for (int k = 0; k <= mm; k++) { sc_[(size_t)k] = js_cos(kPi * k / mm); ss_[(size_t)k] = js_sin(kPi * k / mm); }
}

namespace {
std::mutex& mu() { static std::mutex m; return m; }
std::map<int, std::unique_ptr<FFTR>>& cache() { static std::map<int, std::unique_ptr<FFTR>> c; return c; }
std::map<int, std::unique_ptr<float[]>>& hanns() { static std::map<int, std::unique_ptr<float[]>> c; return c; }
}  // namespace

// (a private constructor: the one place that makes one)
struct FFTRMaker { static FFTR* make(int n) { return new FFTR(n); } };
const FFTR* FFTR::get(int n) {
  std::lock_guard<std::mutex> lk(mu());
  auto& c = cache(); auto it = c.find(n);
  if (it == c.end()) it = c.emplace(n, std::unique_ptr<FFTR>(FFTRMaker::make(n))).first;
  return it->second.get();
}

const float* FFTR::hann(int n) {
  std::lock_guard<std::mutex> lk(mu());
  auto& c = hanns(); auto it = c.find(n);
  if (it == c.end()) {
    std::unique_ptr<float[]> w(new float[(size_t)n]);
    const double twoPi = 2 * kPi;
    for (int i = 0; i < n; i++) w[(size_t)i] = (float)(.5 - .5 * js_cos(twoPi * i / n));   // hannWin: .5 - .5 * Math.cos(TWO_PI * i / n)
    it = c.emplace(n, std::move(w)).first;
  }
  return it->second.get();
}

}  // namespace modfx
}  // namespace thunder
