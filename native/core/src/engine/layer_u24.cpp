// U24: the dispatch of this unit's layer types (what engine.cpp's Osc calls). See layer_u24.h.
#include "engine/layer_u24.h"

#include <cstring>

#include "rt/jsmath.h"

namespace thunder {

bool u24_add_ok(const EngLayer& L);   // layer_add.cpp

namespace {
struct Name { const char* s; LType t; };
const Name kNames[] = {{"fm2", LType::Fm2}, {"fmfb", LType::Fmfb}, {"fmmetal", LType::Fmmetal}, {"fmcross", LType::Fmcross}, {"fm4", LType::Fm4}, {"fm6", LType::Fm6}, {"genesis", LType::Genesis},
  {"sb", LType::Sb}, {"arcade", LType::Arcade}, {"addsaw", LType::Addsaw}, {"addodd", LType::Addodd}, {"membrane", LType::Membrane}, {"bell", LType::Bell}, {"bar", LType::Bar}, {"chord", LType::Chord},
  {"formant", LType::Formant}, {"karplus", LType::Karplus}, {"vosim", LType::Vosim}};
inline bool isFm(LType t) { return t >= LType::Fm2 && t <= LType::Fmcross; }
inline bool isFmOp(LType t) { return t >= LType::Fm4 && t <= LType::Arcade; }
inline bool isAdd(LType t) { return t >= LType::Addsaw && t <= LType::Chord; }
}  // namespace

bool u24_is(LType t) { return t >= LType::Fm2 && t <= LType::Vosim; }
LType u24_layer_type(const char* name, size_t n) {
  for (const Name& e : kNames) if (std::strlen(e.s) == n && std::memcmp(e.s, name, n) == 0) return e.t;
  return LType::Other;
}
bool u24_layer_ok(const EngLayer& L) {
  if (isFmOp(L.type)) { const int na = L.type == LType::Fm6 ? 5 : 8; const double i = js_round(L.a * (na - 1)); return i == i; }   // algs[NaN] throws
  return u24_add_ok(L);
}

void U24Osc::init(const EngLayer& L, double sr, EngRng&) {
  t = L.type; const double A = L.a, B = L.b;
  if (isFm(t)) fm.init((int)t - (int)LType::Fm2, A, B);
  else if (isFmOp(t)) fo.init(t, A, B);
  else if (isAdd(t)) add.init(t, A, B);
  else if (t == LType::Karplus) ks.init(sr, A, B);
  else if (t == LType::Formant) fmt.init(A, B);
  else if (t == LType::Vosim) vs.init(A, B);
}
void U24Osc::blk(double tm, double env, double inc) {
  if (isFm(t)) fm.blk(env);
  else if (isFmOp(t)) fo.blk(env);
  else if (isAdd(t)) add.blk(tm, env, inc);
}
double U24Osc::s(double inc, EngRng& R) {
  if (isFm(t)) return fm.s(inc);
  if (isFmOp(t)) return fo.s(inc);
  if (isAdd(t)) return add.s(inc);
  if (t == LType::Karplus) return ks.s(inc, R);
  if (t == LType::Formant) return fmt.s(inc);
  return vs.s(inc);
}

}  // namespace thunder
