#include "engine/engine_json.h"
#include "engine/layers.h"

#include <cmath>

namespace thunder {
namespace {
inline void num(Json j, std::string_view k, double& d) { Json v = j.get(k); if (v.isNum()) d = v.asNum(); }
}

void eng_layer_from_json(EngLayer& L, Json j) {
  Json t = j.get("type"); if (t.isStr()) { const std::string_view s = t.asStr(); L.type = eng_layer_type(s.data(), s.size()); }
  Json on = j.get("on"); if (on.isBool()) L.on = on.asBool();
  Json md = j.get("mode"); if (md.isStr()) L.adsr = md.asStr() == "ADSR";
  num(j, "a", L.a); num(j, "b", L.b); num(j, "level", L.level); num(j, "tune", L.tune); num(j, "fine", L.fine); num(j, "pAmt", L.pAmt); num(j, "pDec", L.pDec);
  num(j, "atk", L.atk); num(j, "hold", L.hold); num(j, "dec", L.dec); num(j, "sus", L.sus); num(j, "rel", L.rel);
  num(j, "lpCut", L.lpCut); num(j, "lpRes", L.lpRes); num(j, "lpEnv", L.lpEnv); num(j, "hpCut", L.hpCut); num(j, "hpRes", L.hpRes); num(j, "hpEnv", L.hpEnv);
  num(j, "fAtk", L.fAtk); num(j, "fDec", L.fDec); num(j, "delay", L.delay); num(j, "burst", L.burst); num(j, "spacing", L.spacing); num(j, "seed", L.seed);
  eng_layer_u23_from_json(L, j);
}

void eng_fx_from_json(EngFx& f, Json j) {
  const std::string_view t = j.str("type");
  f.kind = t == "degrade" ? EngFx::Kind::Degrade : t == "reson" ? EngFx::Kind::Reson : t == "pdelay" ? EngFx::Kind::PitchDelay : t == "preverb" ? EngFx::Kind::PitchReverb : EngFx::Kind::Other;
  f.on = j.boolean("on", true);
  num(j, "bits", f.bits); num(j, "rate", f.rate); num(j, "noise", f.noise); num(j, "color", f.color); num(j, "follow", f.follow); num(j, "dry", f.dry); num(j, "wet", f.wet);
  num(j, "note", f.note); num(j, "chord", f.chord); num(j, "decay", f.decay); num(j, "bright", f.bright); num(j, "mode", f.mode); num(j, "spread", f.spread); num(j, "mix", f.mix);
  num(j, "time", f.time); num(j, "fb", f.fb); num(j, "pitch", f.pitch); num(j, "pitch2", f.pitch2); num(j, "tone", f.tone); num(j, "size", f.size); num(j, "wow", f.wow);
}

bool eng_voice_from_json(EngVoice& V, Json v, Json layers, Json fx) {
  num(v, "level", V.level); num(v, "tune", V.tune); num(v, "drive", V.drive); num(v, "gate", V.gate); num(v, "len", V.len); num(v, "tail", V.tail);
  num(v, "gPAmt", V.gPAmt); num(v, "gPDec", V.gPDec); num(v, "gLpCut", V.gLpCut); num(v, "gLpRes", V.gLpRes); num(v, "gLpEnv", V.gLpEnv); num(v, "gFAtk", V.gFAtk); num(v, "gFDec", V.gFDec);
  Json ao = v.get("gAmpOn"); if (ao.isBool()) V.gAmpOn = ao.asBool(); else if (ao.isNum()) V.gAmpOn = ao.asNum() != 0;
  Json gm = v.get("gMode"); if (gm.isStr()) V.gAdsr = gm.asStr() == "ADSR";
  num(v, "gAtk", V.gAtk); num(v, "gHold", V.gHold); num(v, "gDec", V.gDec); num(v, "gSus", V.gSus); num(v, "gRel", V.gRel); num(v, "gAmpAmt", V.gAmpAmt);
  static const char* const kRate[3] = {"lfo1Rate", "lfo2Rate", "lfo3Rate"}, *const kShape[3] = {"lfo1Shape", "lfo2Shape", "lfo3Shape"}, *const kDepth[3] = {"lfo1Depth", "lfo2Depth", "lfo3Depth"}, *const kSync[3] = {"lfo1Sync", "lfo2Sync", "lfo3Sync"};
  static const char* const kA[3] = {"env1A", "env2A", "env3A"}, *const kD[3] = {"env1D", "env2D", "env3D"}, *const kS[3] = {"env1S", "env2S", "env3S"}, *const kR[3] = {"env1R", "env2R", "env3R"};
  for (int i = 0; i < 3; i++) { num(v, kRate[i], V.lfoRate[i]); num(v, kShape[i], V.lfoShape[i]); num(v, kDepth[i], V.lfoDepth[i]); num(v, kSync[i], V.lfoSync[i]);
    num(v, kA[i], V.envA[i]); num(v, kD[i], V.envD[i]); num(v, kS[i], V.envS[i]); num(v, kR[i], V.envR[i]); }
  num(v, "seqRate", V.seqRate); num(v, "seqDepth", V.seqDepth); num(v, "seqSync", V.seqSync);
  Json sq = v.arr("seq"); if (sq.isArr()) for (size_t i = 0; i < 32; i++) { Json e = sq.at(i); V.seq[i] = e.isNum() ? e.asNum() : 0; }
  static const char* const kDst[7] = {"r1dst", "r2dst", "r3dst", "r4dst", "r5dst", "r6dst", "r7dst"}, *const kAmt[7] = {"r1amt", "r2amt", "r3amt", "r4amt", "r5amt", "r6amt", "r7amt"};
  for (int i = 0; i < 7; i++) { num(v, kDst[i], V.rdst[i]); num(v, kAmt[i], V.ramt[i]); }
  if (layers.size() > (size_t)kEngLayers || fx.size() > (size_t)kEngFx) return false;
  for (size_t i = 0; i < layers.size(); i++) eng_layer_from_json(V.layers[i], layers.at(i));
  V.nFx = (int)fx.size();
  for (size_t i = 0; i < fx.size(); i++) eng_fx_from_json(V.fx[i], fx.at(i));
  return true;
}

}  // namespace thunder
