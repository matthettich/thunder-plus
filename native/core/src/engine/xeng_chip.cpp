// U28: ChipCore (XENG chip): an 8-bit console voice run at frame rate. index.html ChipCore.render, line for line. No allocation.
#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;
namespace {
const int kIvlN[9] = {1, 2, 2, 3, 3, 4, 4, 3, 3};
const int kIvl[9][4] = {{0, 0, 0, 0}, {0, 12, 0, 0}, {0, 7, 0, 0}, {0, 4, 7, 0}, {0, 3, 7, 0}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 5, 7, 0}, {0, 3, 6, 0}};
}

size_t xeng_chip(const ChipP& p, double sr, double hold, float* out, size_t cap) {
  const double h = mx(0.02, orv(hold, 0.5)), FR = 60, fl = sr / FR;   // hold: the caller's `hold || Math.min(V.len || 1, 4)`; ChipCore adds its own `hold || 0.5`
  const double atkF = js_round(p.atk), decF = mx(0, js_round(p.dec)), relF = mx(0, js_round(p.rel)), sus = js_round(p.sus * 15) / 15;
  const double holdF = js_round(h * FR), len = mn(10, (holdF + relF + 1) / FR), Nd = mx(1, std::ceil(len * sr));
  if (!okN(Nd)) return 0;
  const size_t N = (size_t)Nd;
  if (!out) return N;
  if (N > cap) return 0;
  const double wv = mx(0, mn(2, js_round(p.wave))); const int ci = (int)mx(0, mn(8, js_round(p.chord))); const int ivlN = kIvlN[ci]; const int* ivl = kIvl[ci]; const double arpF = mx(1, js_round(p.arp));
  const double f0 = 65.406 * js_pow(2, p.oct + orz(p.tune) / 12); static const double duties[4] = {0.125, 0.25, 0.5, 0.75};
  double ph = 0, lastN = 1, nacc = 0, vol = 0, frame = -1, f = f0, dut = duties[1], inc = 0, ninc = 0; int32_t lfsr = 1;
  for (size_t i = 0; i < N; i++) {
    const double fr = std::floor((double)i / fl);
    if (fr != frame) {
      frame = fr;
      if (fr < holdF) { vol = atkF > 0 && fr < atkF ? fr / atkF : fr - atkF < decF ? 1 - (1 - sus) * (fr - atkF) / decF : sus; }
      else vol = relF != 0 ? mx(0, sus * (1 - (fr - holdF) / relF)) : 0;
      vol = js_round(vol * 15) / 15;
      const double stI = std::fmod(std::floor(fr / arpF), (double)ivlN);   // Math.floor(fr / arpF) % ivl.length (non-negative here)
      const double st = (double)ivl[(int)stI];
      const double vb = p.vib > 0 ? js_round(js_sin(6.2832 * p.vrate * fr / FR) * 4) / 4 * p.vib * 0.5 : 0;
      double ff = f0 * js_pow(2, (st + p.slide * 12 * mn(1, fr / mx(1, holdF)) + vb) / 12);
      if (p.quant >= 0.5) { const double per = mx(8, js_round(1789773 / (16 * ff) - 1)); ff = 1789773 / (16 * (per + 1)); }
      f = ff; inc = f / sr;
      { const double di = js_round(p.duty) + (p.sweep > 0 ? std::floor(fr / mx(1, js_round(8 / p.sweep))) : 0);
        dut = duties[(int)std::fmod(mx(0, mn(3, di)), 4.0)]; }
      ninc = js_pow(2, p.nfreq * 11) * 4 / sr;
    }
    double y;
    if (wv == 0) y = (std::fmod(ph, 1.0) < dut ? 1 : -1);
    else if (wv == 1) y = js_round((1 - jabs(2 * std::fmod(ph, 1.0) - 1)) * 15) / 7.5 - 1;
    else {
      nacc += ninc;
      while (nacc >= 1) { nacc -= 1; const int32_t fb = (lfsr & 1) ^ ((lfsr >> (p.nshort >= 0.5 ? 6 : 1)) & 1); lfsr = (lfsr >> 1) | (fb << 14); lastN = (lfsr & 1) ? 1 : -1; }
      y = lastN;
    }
    ph += inc;
    out[i] = (float)(y * vol * (wv == 1 ? 1 : 0.8));
  }
  return finish(out, N, p.level, p.drive, sr, 0.002);
}

}  // namespace thunder
