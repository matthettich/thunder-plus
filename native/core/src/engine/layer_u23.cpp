// U23: the dispatcher of Osc's layer types click, wavetable, pd, chip, supersaw, pwm, sample and grains, and the layer's JSON fields.
#include "engine/layers.h"
#include "rt/jsmath.h"

namespace thunder {

bool LayerOsc::handles(LType t) {
  switch (t) { case LType::Click: case LType::Wavetable: case LType::Pd: case LType::Chip: case LType::Supersaw: case LType::Pwm: case LType::Sample: case LType::Grains: return true; default: return false; }
}

void LayerOsc::init(const EngLayer& L, double sr, EngRng& R) {
  const double A = L.a, B = L.b;
  switch (L.type) {
    case LType::Click: which = Click; click.init(A, B, sr); break;
    case LType::Wavetable: which = Wavetable; wt.init(L, sr, A, B, R); break;
    case LType::Pd: which = Pd; pd.init(A, B); break;
    case LType::Chip: which = Chip; chip.init(sr, A, B); break;
    case LType::Supersaw: which = Supersaw; ss.init(A, B, R); break;
    case LType::Pwm: which = Pwm; pwm.init(L, A, B); break;
    case LType::Sample:
      if (!L.u23.smp || !L.u23.smpLen) which = Silent;
      else if (js_to_int32(L.u23.vint) > 0) { which = Vint; vint.init(L, sr, A, B); }
      else { which = Sample; smp.init(L, A, B); }
      break;
    case LType::Grains: if (!L.u23.smp || !L.u23.smpLen) which = Silent; else { which = Grains; gr.init(L, sr, A, B); } break;
    default: which = Silent; break;
  }
}

double LayerOsc::s(double inc, EngRng& R) {
  switch (which) {
    case Click: return click.s();
    case Wavetable: return wt.s(inc, R);
    case Pd: return pd.s(inc);
    case Chip: return chip.s(inc);
    case Supersaw: return ss.s(inc);
    case Pwm: return pwm.s(inc);
    case Sample: return smp.s(inc);
    case Vint: return vint.s(inc);
    case Grains: return gr.s(inc, R);
    default: return 0;
  }
}

void eng_layer_u23_from_json(EngLayer& L, Json j) {
  EngU23& U = L.u23;
  auto num = [&](const char* k, double& d) { Json v = j.get(k); if (v.isNum()) d = v.asNum(); };
  num("wtBend", U.wtBend); num("wtUni", U.wtUni); num("wtSpread", U.wtSpread); num("wtSub", U.wtSub); num("wtNoise", U.wtNoise); num("vint", U.vint); num("vintF", U.vintF);
  num("lpMode", U.lpMode); num("lpS", U.lpS); num("lpE", U.lpE); num("lpX", U.lpX);
}

void EngSampleBank::attach(EngLayer& L, Json sample) {
  Json data = sample.arr("data"); if (!sample.isObj()) return;
  store.emplace_back(data.size());
  std::vector<float>& v = store.back(); for (size_t i = 0; i < data.size(); i++) v[i] = (float)data.at(i).asNum();
  L.u23.smp = v.data(); L.u23.smpLen = v.size(); L.u23.smpSr = sample.num("sr", 48000);
}
}  // namespace thunder
