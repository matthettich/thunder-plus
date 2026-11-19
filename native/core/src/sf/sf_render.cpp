// U32: sfRender (index.html): the pad's own render, one note at C-4 (+ Transpose), velocity 100, held for `hold` seconds and then released. Mono: a stereo pair is rendered from the one zone sfPick returns.
#include <cmath>

#include "rt/jsmath.h"
#include "sf/sf.h"
#include "sf/sf_math.h"

namespace thunder {
namespace sf {
namespace {

struct Setup { const Preset* pr; double key, h, rlen; Pick picks[4]; int np; size_t N; bool ok; };

Setup setup(const Font& f, const PadParams& P, double sr, double hold, const PresetPcm* pcm, VoiceParams* vo, double* rels) {
  Setup S{}; S.ok = false;
  if (!(sr > 0 && std::isfinite(sr))) return S;
  S.pr = sf_preset(f, js_round(P.bank), js_round(P.prog));
  S.key = jclamp(60 + js_round(P.trans), 0, 127);
  S.np = sf_pick(f, S.pr, S.key, 100, S.picks);
  S.h = jmax(0.05, orv(hold, 1.5));
  S.rlen = 0;
  for (int i = 0; i < S.np; i++) {
    sf_voice(f, *S.picks[i].z, S.key, 100, vo[i]);
    if (pcm && !pcm->get(S.picks[i].z->s)) return S;   // not decoded yet: the JS asks for the decode and skips the zone
    rels[i] = jmin(4, vo[i].v.rel * P.rel); S.rlen = jmax(S.rlen, rels[i]);
  }
  const double n = jmax(1, std::ceil(jmin(30, S.h + S.rlen + 0.05) * sr));
  if (!(n >= 1 && n <= 30.0 * sr + 1 && n < 4e9) || !std::isfinite(n)) return S;
  S.N = (size_t)n; S.ok = true;
  return S;
}

}  // namespace

size_t sf_render_bound(const Font& f, const PadParams& P, double sr, double hold) {
  VoiceParams vo[4]; double rels[4];
  const Setup S = setup(f, P, sr, hold, nullptr, vo, rels);
  return S.ok ? S.N : 0;
}

size_t sf_render(const Font& f, const PresetPcm& pcm, const PadParams& P, double sr, double hold, float* out, size_t cap) {
  VoiceParams vo[4]; double rels[4];
  const Setup S = setup(f, P, sr, hold, &pcm, vo, rels);
  if (!S.ok || S.N > cap) return 0;
  const size_t N = S.N; const double key = S.key, h = S.h;
  for (size_t i = 0; i < N; i++) out[i] = 0;
  for (int pi = 0; pi < S.np; pi++) {
    const Pcm& a = *pcm.get(S.picks[pi].z->s); const VoiceParams& v = vo[pi]; const double rel = rels[pi];
    const double ratio = js_pow(2, ((key - v.root) * v.scale / 100 + v.tune) / 12) * a.sr / sr;
    const double ls = a.ls, le = a.le; const bool loop = (v.modes == 1 || v.modes == 3) && ls >= 0;   // (ls >= 0: a loop that starts before the sample would read index -1; the JS reads undefined there)
    const float* d = a.d.data(); const double len = (double)a.d.size();
    const double lg = js_pow(10, -v.atten / 200), sus = js_pow(10, -v.v.sus / 200); const auto& E = v.v;
    double pos = 0;
    for (size_t i = 0; i < N; i++) {
      const double t = (double)i / sr; double e;
      const double ta = t - E.delay;
      if (ta < 0) e = 0; else if (ta < E.atk * P.atk) e = ta / (E.atk * P.atk); else if (ta < E.atk * P.atk + E.hold) e = 1;
      else { const double td = ta - E.atk * P.atk - E.hold; e = jmax(sus, js_pow(10, -(td / jmax(0.001, E.dec)) * 100 / 20)); }
      if (t > h) e *= js_exp(-(t - h) / jmax(0.003, rel / 6.9));
      const double j = std::floor(pos), fr = pos - j;
      if (!(j >= 0 && j + 1 < len)) break;   // (the JS: `j + 1 >= d.length`; a negative or NaN j would read undefined there)
      const size_t jj = (size_t)j;
      out[i] = (float)((double)out[i] + ((double)d[jj] + ((double)d[jj + 1] - (double)d[jj]) * fr) * e * lg);
      pos += ratio; if (loop && le > ls + 4 && pos >= le) pos -= (le - ls);
    }
  }
  double pk = 0; for (size_t i = 0; i < N; i++) pk = jmax(pk, std::fabs((double)out[i]));
  const double lv = js_pow(10, orv(P.level, 0) / 20) * (pk > 0 ? 0.8 / pk : 1);
  for (size_t i = 0; i < N; i++) out[i] = (float)((double)out[i] * lv);
  return N;
}

}  // namespace sf
}  // namespace thunder
