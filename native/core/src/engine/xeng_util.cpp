// U28: KeysUtil.finish and the shared tail of the XENG engines (see xeng_util.h).
#include "engine/xeng_util.h"

namespace thunder {
namespace xe {

void level_drive(float* out, size_t N, double level, double drive, double scale) {
  const double lv = js_pow(10, orz(level) / 20) * scale, dr = orz(drive) > 0.001 ? 1 + drive * drive * 10 : 0, dn = dr != 0 ? 1 / js_tanh(dr) : 1;
  for (size_t i = 0; i < N; i++) { double y = (double)out[i] * lv; if (dr != 0) y = js_tanh(y * dr) * dn; out[i] = (float)y; }
}

size_t trim_fade(float* out, size_t N, double sr, double fade) {
  size_t end = N; while (end > 1 && jabs((double)out[end - 1]) < 1e-4) end--;
  const size_t rl = end > 1 ? end : 1;   // Math.max(end, 1)
  const double flr = js_round(sr * fade); const size_t fl = (double)rl < flr ? rl : (size_t)(flr > 0 ? flr : 0);
  for (size_t i = 0; i < fl; i++) out[rl - 1 - i] = (float)((double)out[rl - 1 - i] * ((double)i / (double)fl));
  return rl;
}

size_t finish(float* out, size_t N, double level, double drive, double sr, double tailFade) {
  const double pk = peak_of(out, N);
  level_drive(out, N, level, drive, pk > 0 ? 0.8 / pk : 1);
  return trim_fade(out, N, sr, tailFade);
}

}  // namespace xe
}  // namespace thunder
