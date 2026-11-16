// U23: the sample layer in a vintage mode (vintData, vintOsc): the sample is re-sampled at the machine's rate and bit depth (a two-pole low pass under the new Nyquist, sample and hold, quantise),
// then played back by changing the read clock with no interpolation, then the machine's output filter (Colour moves it).
// The re-sampled copy is a std::vector made here (the JS makes a Float32Array and caches it on the sample; the copy is a pure function of the sample, so no cache is needed): this layer allocates.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {
using l23::mn; using l23::mx;

namespace {
struct Vint { double rate, bits, flt; };
const Vint kVint[6] = {{0, 0, 0}, {26040, 12, 0}, {40000, 12, 14000}, {31250, 12, 9000}, {22050, 8, 7000}, {10000, 8, 4000}};   // VINT (the names are the UI's)
}  // namespace

void VintOsc::init(const EngLayer& L, double sr, double A, double B) {
  const EngU23& U = L.u23; const int m = (int)mn(5, (double)js_to_int32(U.vint));   // vint | 0 > 0 here
  const double rate = kVint[m].rate, bits = kVint[m].bits, flt = kVint[m].flt;
  const float* src = U.smp; const size_t slen = U.smpLen; const double ssr = U.smpSr;
  // vintData
  const double kk = ssr / rate, nn = mx(1, std::floor((double)slen / kk)), q = js_pow(2, bits - 1);
  if (!(nn < 2e8)) { dv.clear(); pos = 0; hasFc = false; return; }   // (the JS: new Float32Array(Infinity) throws; a sample rate of 0 or a negative one gets here: silent)
  const size_t n = (size_t)nn; dv.assign(n, 0.0f);
  const double c = 1 - js_exp(-2 * l23::kPi * mn(rate * 0.45, ssr * 0.45) / ssr); double y1v = 0, y2v = 0; double j = 0;
  for (size_t i = 0; i < slen && j < nn; i++) {
    y1v += ((double)src[i] - y1v) * c; y2v += (y1v - y2v) * c;
    if ((double)i >= j * kk) { dv[(size_t)j] = (float)(js_round(mx(-1, mn(1, y2v)) * q) / q); j++; }
  }
  // vintOsc
  rev = B >= 0.5; k = rate / kBase;
  const double fc = flt != 0 ? mn(sr * 0.45, flt * js_pow(2, (U.vintF - 0.5) * 2)) : 0;
  hasFc = fc != 0 && fc == fc; g = hasFc ? 1 - js_exp(-2 * l23::kPi * fc / sr) : 1;
  pos = A * (double)dv.size(); y1 = 0; y2 = 0;
}
double VintOsc::s(double inc) {
  const double len = (double)dv.size();
  if (pos >= len) return 0;
  const int32_t i = js_to_int32(pos); const double x = rev ? l23::at(dv.data(), dv.size(), len - 1 - i) : l23::at(dv.data(), dv.size(), i); pos += inc * k;
  if (!hasFc) return x;
  y1 += (x - y1) * g; y2 += (y1 - y2) * g; return y2;
}
}  // namespace thunder
