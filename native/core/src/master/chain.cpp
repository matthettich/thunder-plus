// U12: the master chain. See chain.h.
#include "master/chain.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

#include "rt/jsmath.h"

namespace thunder {

MasterChainConfig::MasterChainConfig() {}

MasterChainConfig master_chain_fresh(bool safety) {
  MasterChainConfig c; c.mstOn = false; c.eqOn = true; c.nBands = 6; c.safety = safety;
  const struct { EqType t; double f, g, q; bool on; int slope; } d[6] = {
    {EqType::LowCut, 28, 0, .71, true, 24}, {EqType::LowShelf, 110, 0, .7, true, 24}, {EqType::Bell, 350, 0, 1.2, true, 24}, {EqType::Bell, 2500, 0, 1.2, true, 24},
    {EqType::HighShelf, 9000, 0, .7, true, 24}, {EqType::HighCut, 19000, 0, .71, false, 12}};
  for (int i = 0; i < 6; i++) { EqBand& b = c.bands[i]; b.type = d[i].t; b.f = d[i].f; b.g = d[i].g; b.q = d[i].q; b.on = d[i].on; b.slope = d[i].slope; }
  c.set.hasSup = c.set.hasComp = c.set.hasLim = true;
  SupSet& s = c.set.sup; s.on = false; s.depth = .4; s.sharp = .5; s.sel = 4; s.atk = 8; s.rel = 90; s.lo = 150; s.hi = 12000; s.mix = 1; s.delta = false;
  CompSet& p = c.set.comp; p.on = true; p.thr = -14; p.ratio = 2; p.atk = 10; p.rel = 150; p.autoRel = true; p.knee = 6; p.makeup = 0; p.autoMakeup = true; p.mix = 1; p.hpf = 80;
  LimSet& l = c.set.lim; l.on = true; l.drive = 0; l.ceil = -1; l.rel = 120; l.look = 3; l.tp = true;
  return c;
}

namespace {
// `+v` finite? (normMst: Number.isFinite(+v)): numbers, null (0), booleans, numeric strings
bool plusNum(Json v, double* out) {
  if (v.isNum()) { *out = v.asNum(); return std::isfinite(*out); }
  if (v.isNull()) { *out = 0; return true; }
  if (v.isBool()) { *out = v.asBool() ? 1 : 0; return true; }
  if (v.isStr()) { std::string s(v.asStr()); char* e = nullptr; const double d = std::strtod(s.c_str(), &e); if (s.empty()) { *out = 0; return true; } if (e && *e == 0 && std::isfinite(d)) { *out = d; return true; } }
  return false;
}
double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
double orDefault(Json v, double dflt) { double x; if (!plusNum(v, &x) || x == 0) return dflt; return x; }   // (+b.f || dflt)
void num(Json src, const char* key, MOpt* dst, double lo, double hi) { double x; if (plusNum(src.get(key), &x)) *dst = clampd(x, lo, hi); }
void flag(Json src, const char* key, MBool* dst) { Json v = src.get(key); if (v.isBool()) *dst = v.asBool(); }
}  // namespace

MasterChainConfig master_chain_from_song(Json j, bool limiter) {
  MasterChainConfig c = master_chain_fresh(limiter);
  if (!j.isObj()) return c;
  c.mstOn = j.get("on").isBool() ? j.get("on").asBool() : (j.get("on").isNum() ? j.get("on").asNum() != 0 : (j.get("on").isStr() ? !j.get("on").asStr().empty() : !j.get("on").missing() && !j.get("on").isNull()));
  Json s = j.get("sup");
  if (s.isObj()) { SupSet& x = c.set.sup; x.on = s.get("on").isBool() ? s.get("on").asBool() : x.on;
    num(s, "depth", &x.depth, 0, 1); num(s, "sharp", &x.sharp, 0, 1); num(s, "sel", &x.sel, 0, 12); num(s, "atk", &x.atk, 1, 100); num(s, "rel", &x.rel, 10, 500); num(s, "lo", &x.lo, 20, 2000); num(s, "hi", &x.hi, 1000, 20000); num(s, "mix", &x.mix, 0, 1); flag(s, "delta", &x.delta); }
  s = j.get("comp");
  if (s.isObj()) { CompSet& x = c.set.comp; x.on = s.get("on").isBool() ? s.get("on").asBool() : x.on;
    num(s, "thr", &x.thr, -40, 0); num(s, "ratio", &x.ratio, 1, 20); num(s, "atk", &x.atk, 0.1, 100); num(s, "rel", &x.rel, 20, 1500); num(s, "knee", &x.knee, 0, 12); num(s, "makeup", &x.makeup, 0, 18); num(s, "mix", &x.mix, 0, 1); num(s, "hpf", &x.hpf, 0, 300); flag(s, "auto", &x.autoRel); flag(s, "automk", &x.autoMakeup); }
  s = j.get("lim");
  if (s.isObj()) { LimSet& x = c.set.lim; x.on = s.get("on").isBool() ? s.get("on").asBool() : x.on;
    num(s, "drive", &x.drive, 0, 18); num(s, "ceil", &x.ceil, -6, 0); num(s, "rel", &x.rel, 10, 1000); num(s, "look", &x.look, 1, 10); flag(s, "tp", &x.tp); }
  Json eq = j.get("eq");
  if (eq.isObj()) {
    c.eqOn = !(eq.get("on").isBool() && !eq.get("on").asBool());   // j.eq.on !== false
    Json bs = eq.get("bands");
    if (bs.isArr()) {
      c.nBands = 0;
      for (size_t i = 0; i < bs.size() && i < (size_t)kEqMaxBands; i++) {   // slice(0, 8), then the filter on a valid type
        Json b = bs.at(i); if (!b.isObj()) continue; const std::string_view t = b.str("type"); EqBand e;
        if (t == "lowcut") e.type = EqType::LowCut; else if (t == "lowshelf") e.type = EqType::LowShelf; else if (t == "bell") e.type = EqType::Bell;
        else if (t == "highshelf") e.type = EqType::HighShelf; else if (t == "highcut") e.type = EqType::HighCut; else continue;
        e.f = clampd(orDefault(b.get("f"), 1000), 20, 20000); e.g = clampd(orDefault(b.get("g"), 0), -18, 18); e.q = clampd(orDefault(b.get("q"), 1), 0.1, 18);
        e.on = !(b.get("on").isBool() && !b.get("on").asBool()); double sl; e.slope = plusNum(b.get("slope"), &sl) && (sl == 12 || sl == 24 || sl == 48) ? (int)sl : 24;
        c.bands[c.nBands++] = e;
      }
    }
  }
  return c;
}

MasterChain::MasterChain(double sampleRate, int) : sr_(sampleRate), mst_(new MasterProcessor(sampleRate, false)), safe_(new MasterProcessor(sampleRate, false)) {
  safe_->set(master_safe_limiter_set()); safe_->set(MasterSet());   // (SAFE_LIM: the limiter alone, on)
  MasterSet off; off.hasSup = off.hasComp = off.hasLim = true; mst_->set(off);   // nothing on until configure()
}

void MasterChain::configure(const MasterChainConfig& c) {
  mstOn_ = c.mstOn;
  eq_.configure(sr_, c.mstOn && c.eqOn, c.bands, c.nBands);
  wkOn_ = c.mstOn && (c.set.sup.on || c.set.comp.on || c.set.lim.on);
  MasterSet s = c.set; s.hasSup = s.hasComp = s.hasLim = true;
  if (!c.mstOn) { s.sup.on = s.comp.on = s.lim.on = false; }
  mst_->set(s);
  safeOn_ = c.safety && !(c.mstOn && c.set.lim.on && wkOn_);   // mLim = ch.wk && M.lim.on
  safe_->set(master_safe_limiter_set());
}

int MasterChain::latencyMastering() const { return wkOn_ ? mst_->latency() : 0; }
int MasterChain::latencySafety() const { return safeOn_ ? safe_->latencyLim() : 0; }
int MasterChain::latency() const { return latencyMastering() + latencySafety(); }

void MasterChain::process(float* L, float* R, int frames) {
  if (eq_.nodes() > 0) eq_.process(L, R, frames);
  if (wkOn_) mst_->process(L, R, frames);
  if (safeOn_) safe_->process(L, R, frames);
}

void MasterChain::hook(void* user, int, float* L, float* R, int frames, double, const MixFeed*, int) { static_cast<MasterChain*>(user)->process(L, R, frames); }

}  // namespace thunder
