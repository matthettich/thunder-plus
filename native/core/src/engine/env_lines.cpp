// ENVELOPE TIMES IN LINES (web #259, docs/ENV-LINES-NOTES.md on beta): the host-side conversion of a voice's lines-mode envelopes to ms before a render. The JS is the specification (index.html at beta cdd9439:
// ENVL_*, linesMs, msLines, envLinesOf, envMs, voiceLines, envLinesFit, :5127-5155); every expression keeps the JS's operation order so the doubles are equal.
// Control thread, no allocation, no exception. The engines themselves stay in ms and do not change.
#include <algorithm>
#include <cmath>

#include "engine/engine.h"
#include "rt/jsmath.h"
#include <limits>

namespace thunder {
namespace {
// Math.min / Math.max: NaN in, NaN out (std::min / max would return the other argument)
inline double jmin(double a, double b) { return (a != a || b != b) ? std::numeric_limits<double>::quiet_NaN() : (b < a ? b : a); }
inline double jmax(double a, double b) { return (a != a || b != b) ? std::numeric_limits<double>::quiet_NaN() : (b > a ? b : a); }
constexpr double kMaxMs = 64000, kMaxLen = 300;
inline double orZero(double x) { return x != x ? 0 : x; }                                    // `+x || 0` (NaN -> 0; the JS's `|| 0` also maps -0, which is the same number here)
double linesMs(double n, double bpm, double lpb) { return jmin(kMaxMs, jmax(0.0, orZero(n)) / jmax(1.0, lpb) * 60 / jmax(1.0, bpm) * 1000); }
double msLines(double ms, double bpm, double lpb) { return jmax(0.0, js_round(orZero(ms) / 1000 * jmax(1.0, bpm) / 60 * jmax(1.0, lpb))); }
double linesOf(double ownL, double ms, double bpm, double lpb) { return ownL == ownL ? orZero(ownL) : msLines(ms, bpm, lpb); }   // envLinesOf: `o[k + 'L'] != null ? +o[k + 'L'] || 0 : msLines(o[k])`
double songBpmOf(double bpm) { return bpm == bpm && bpm != 0 ? bpm : 120; }                       // (S && S.bpm) || 120
double songLpbOf(double lpb) { return jmax(1.0, js_round(lpb == lpb && lpb != 0 ? lpb : 4)); }   // Math.max(1, Math.round(S.lpb || 4))
bool layerLines(const EngLayer& L) { return L.on && L.level > 0 && (L.aLines || L.fLines); }
bool voiceLinesOf(const EngVoice& V) {
  if (V.gAmpOn && V.gLines) return true;
  for (int i = 0; i < V.nLayers; i++) if (layerLines(V.layers[i])) return true;
  return false;
}
}  // namespace

double eng_layer_rel_ms(const EngLayer& L, double bpm, double lpb) {
  if (!L.aLines) return L.rel;
  const double b = songBpmOf(bpm), l = songLpbOf(lpb);
  return linesMs(linesOf(L.aL[3], L.rel, b, l), b, l);
}
double eng_voice_grel_ms(const EngVoice& V, double bpm, double lpb) {
  if (!V.gLines) return V.gRel;
  const double b = songBpmOf(bpm), l = songLpbOf(lpb);
  return linesMs(linesOf(V.gL[3], V.gRel, b, l), b, l);
}

bool eng_env_lines_fit(EngVoice& V, double* holdp, double bpmIn, double lpbIn) {
  if (!voiceLinesOf(V)) return false;
  const double hold = holdp ? *holdp : 0, bpm = songBpmOf(bpmIn), lpb = songLpbOf(lpbIn), bar = 240 / jmax(1.0, bpm);
  double need = 0;
  for (int i = 0; i < V.nLayers; i++) {
    EngLayer& L = V.layers[i];
    if (!L.on || !(L.level > 0)) continue;
    if (!L.aLines && !L.fLines) continue;
    // conv(): every stage of a lines-mode envelope from the SAVED values (the ms fields are only read here, for a stage with no `...L` key), then written
    if (L.aLines) {
      const double a0 = linesMs(linesOf(L.aL[0], L.atk, bpm, lpb), bpm, lpb), a1 = linesMs(linesOf(L.aL[1], L.hold, bpm, lpb), bpm, lpb),
                   a2 = linesMs(linesOf(L.aL[2], L.dec, bpm, lpb), bpm, lpb), a3 = linesMs(linesOf(L.aL[3], L.rel, bpm, lpb), bpm, lpb);
      L.atk = a0; L.hold = a1; L.dec = a2; L.rel = a3;
    }
    if (L.fLines) {
      const double f0 = linesMs(linesOf(L.fL[0], L.fAtk, bpm, lpb), bpm, lpb), f1 = linesMs(linesOf(L.fL[1], L.fDec, bpm, lpb), bpm, lpb);
      L.fAtk = f0; L.fDec = f1;
    }
    if (L.aLines) {   // the pad must last as long as this layer's stages (an ADSR in a held slot: attack, decay and a bar of sustain)
      const double base = orZero(L.delay) / 1000 + (double)js_to_int32(L.burst) * orZero(L.spacing) / 1000;
      const double t = L.adsr ? (hold > 0 ? (L.atk + L.dec) / 1000 + bar : jmax(V.gate / 1000, L.atk / 1000) + L.rel / 1000) : (L.atk + L.hold + L.dec) / 1000;
      need = jmax(need, base + t + 0.005);
    }
  }
  if (V.gAmpOn && V.gLines) {
    const double g0 = linesMs(linesOf(V.gL[0], V.gAtk, bpm, lpb), bpm, lpb), g1 = linesMs(linesOf(V.gL[1], V.gHold, bpm, lpb), bpm, lpb),
                 g2 = linesMs(linesOf(V.gL[2], V.gDec, bpm, lpb), bpm, lpb), g3 = linesMs(linesOf(V.gL[3], V.gRel, bpm, lpb), bpm, lpb);
    V.gAtk = g0; V.gHold = g1; V.gDec = g2; V.gRel = g3;
    need = jmax(need, (V.gAdsr ? (hold > 0 ? (V.gAtk + V.gDec) / 1000 + bar : jmax(V.gate / 1000, V.gAtk / 1000) + V.gRel / 1000) : (V.gAtk + V.gHold + V.gDec) / 1000) + 0.005);
  }
  V.len = jmin(kMaxLen, jmax(orZero(V.len), need));
  if (holdp) *holdp = (hold == hold && hold != 0) ? jmin(kMaxLen, jmax(hold, need)) : 0;
  return true;
}

}  // namespace thunder
