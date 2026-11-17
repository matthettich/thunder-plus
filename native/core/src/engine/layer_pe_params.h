// U25: the freshLayer fields of the layer types plaits (Macro osc: pEng .. pAux) and elements (Resonator: eModel .. eSpace). Included by engine.h (EngLayer::pe). The JS holds pStrike / pAux as booleans:
// here a double whose truthiness is the JS's (non-zero and not NaN).
#ifndef THUNDER_ENGINE_LAYER_PE_PARAMS_H
#define THUNDER_ENGINE_LAYER_PE_PARAMS_H
namespace thunder {
struct EngPE {
  double pEng = 8, pHarm = 0.5, pTim = 0.5, pMor = 0.5, pDecay = 0.5, pLpg = 0.5, pFm = 0, pTimEnv = 0, pMorEnv = 0, pStrike = 1, pAux = 0;
  double eModel = 0, eStrength = 0.8, eContour = 0, eBow = 0, eBlow = 0, eStrike = 0.8, eBowT = 0.5, eFlow = 0.5, eBlowT = 0.5, eMallet = 0.5, eStrikeT = 0.5, eGeo = 0.3, eBright = 0.5, eDamp = 0.6, ePos = 0.3, eSpace = 0.3;
};
}  // namespace thunder
#endif
