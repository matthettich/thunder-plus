// MODFX control / CV modules (chains-worklet.js 719 to 1036 and 1379 to 1603, 1873 to 2000): Envelope follower, Gate extract, Slew, Sample & hold, Quantizer, Math, Logic, Steps, Clock, Clock multiplier,
// Chance gate, Counter, Step switch, Burst, Gate delay, Flip-flop, Rectify, Scale / map, Comparator, Switch, Crossfade, Constant, Divider, Euclidean. Pitch follow / Pitch track and
// Scope are not here (see U19-NOTES.md). Each process() keeps the JS's statements and arithmetic order.
#include <vector>

#include "modfx/mods.h"
#include "modfx/util.h"

namespace thunder {
namespace modfx {
namespace {

#define PD(a) (int)(sizeof(a) / sizeof(a[0])), a
const double kDivBeats[10] = {.0625, .125, 1.0 / 6, .25, 1.0 / 3, .5, 1, 2, 4, 8};
inline double divBeats(double v) { return kDivBeats[(int)clamp((double)i32(v), 0, 9)]; }
inline uint32_t imulSeed(int32_t sd) { return (uint32_t)sd * 2654435761u; }   // Math.imul(sd, 2654435761) >>> 0

// ---- Envelope follower ---------------------------------------------------------------------------------------------------------------
const PortDef kEnvIn[] = {{"in", "audio"}}, kEnvOut[] = {{"out", "cv"}, {"gate", "gate"}};
const ParamDef kEnvP[] = {{"atk", .01, 0, 1, .001}, {"rel", .15, 0, 2, .001}, {"gain", 2, 0, 16, .01}, {"thr", .3, 0, 1, .001}, {"mode", 0, 0, 1, 1}};
class EnvFol : public Module {
 public:
  EnvFol(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double dt = 1 / c.sr, ka = 1 - js_exp(-dt / jmax(p[0], 1e-4)), kr = 1 - js_exp(-dt / jmax(p[1], 1e-4));
    const double gain = p[2], thr = p[3]; const bool sq = p[4] >= .5; const In X = inp(io, 0); const Out O = outp(io, 0), G = outp(io, 1);
    for (int i = 0; i < c.block; i++) {
      const double x = X[i] * gain, y = sq ? x * x : std::fabs(x);
      env_ += (y - env_) * (y > env_ ? ka : kr);
      O[i] = env_; G[i] = env_ > thr ? 1 : 0;
    }
  }
 private:
  double env_ = 0;
};

// ---- Gate extract --------------------------------------------------------------------------------------------------------------------
const PortDef kGxOut[] = {{"out", "gate"}, {"env", "cv"}};
const ParamDef kGxP[] = {{"thr", .1, 0, 1, .001}, {"hyst", .25, 0, 1, .01}, {"rel", .05, 0, 1, .001}};
class GateEx : public Module {
 public:
  GateEx(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double thr = io.p[0], hy = thr * (1 - io.p[1]), kr = 1 - js_exp(-1 / (c.sr * jmax(io.p[2], 1e-4))); const In X = inp(io, 0); const Out O = outp(io, 0), E = outp(io, 1);
    for (int i = 0; i < c.block; i++) {
      const double a = std::fabs(X[i]);
      if (on_) { if (a < hy) on_ = 0; } else if (a > thr) on_ = 1;
      env_ += ((on_ ? 1 : 0) - env_) * kr;
      O[i] = on_; E[i] = env_;
    }
  }
 private:
  int on_ = 0; double env_ = 0;
};

// ---- Slew ----------------------------------------------------------------------------------------------------------------------------
const PortDef kCvIn[] = {{"in", "cv"}}, kCvOut[] = {{"out", "cv"}};
const ParamDef kSlewP[] = {{"up", .05, 0, 1, .001}, {"dn", .05, 0, 1, .001}};
class Slew : public Module {
 public:
  Slew(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double ku = 1 - js_exp(-1 / (c.sr * jmax(io.p[0], 1e-4))), kd = 1 - js_exp(-1 / (c.sr * jmax(io.p[1], 1e-4))); const In X = inp(io, 0); const Out O = outp(io, 0);
    for (int i = 0; i < c.block; i++) { const double x = X[i]; y_ += (x - y_) * (x > y_ ? ku : kd); O[i] = y_; }
  }
 private:
  double y_ = 0;
};

// ---- Sample & hold -------------------------------------------------------------------------------------------------------------------
const PortDef kShIn[] = {{"in", "cv"}, {"trig", "gate"}}, kShOut[] = {{"out", "cv"}, {"noise", "cv"}};
const ParamDef kShP[] = {{"rate", 4, .05, 40, .01}, {"smooth", 0, 0, 1, .01}, {"depth", 1, 0, 1, .01}};
class SampleHold : public Module {
 public:
  SampleHold(double, Seeds& s) : R_(s.next()) {}
  void process(const IO& io, const Ctx& c) override {
    const bool trig = io.linked[1]; const double k = 1 - js_pow(io.p[1], .15) * .9995, dep = io.p[2]; const In X = inp(io, 0), T = inp(io, 1); const Out O = outp(io, 0), N = outp(io, 1);
    for (int i = 0; i < c.block; i++) {
      bool fire = false;
      if (trig) { const bool g = T[i] > .5; fire = g && !t_; t_ = g; }
      else { ph_ += io.p[0] / c.sr; if (ph_ >= 1) { ph_ -= 1; fire = true; } }
      if (fire) { held_ = X[i]; noise_ = R_() * 2 - 1; }
      y_ += (held_ - y_) * k; ny_ += (noise_ - ny_) * k;
      O[i] = y_ * dep; N[i] = ny_ * dep;
    }
  }
 private:
  Rng R_; double ph_ = 0, held_ = 0, noise_ = 0, y_ = 0, ny_ = 0; bool t_ = false;
};

// ---- Quantizer -----------------------------------------------------------------------------------------------------------------------
const int kScaleN[7] = {7, 7, 7, 5, 6, 12, 3};
const int kScales[7][12] = {{0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 3, 5, 7, 10}, {0, 2, 4, 6, 8, 10}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}, {0, 5, 7}};
const ParamDef kQuantP[] = {{"scale", 0, 0, 6, 1}, {"transpose", 0, -24, 24, 1}, {"depth", 1, 0, 1, .01}};
class Quant : public Module {
 public:
  Quant(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const int si = (int)clamp((double)i32(io.p[0]), 0, 6); const int* sc = kScales[si]; const int ns = kScaleN[si]; const double tr = io.p[1], dep = io.p[2]; const In X = inp(io, 0); const Out O = outp(io, 0);
    for (int i = 0; i < c.block; i++) {
      const double semi = js_round((X[i] - .5) * 96) + tr, oct = std::floor(semi / 12), pc = jmod(jmod(semi, 12) + 12, 12);
      double best = sc[0], bd = 99;
      for (int k = 0; k < ns; k++) { const double s = sc[k], d = std::fabs(s - pc); if (d < bd) { bd = d; best = s; } }
      O[i] = ((oct * 12 + best) / 96 + .5) * dep;
    }
  }
};

// ---- Math ----------------------------------------------------------------------------------------------------------------------------
const PortDef kMathIn[] = {{"a", "cv"}, {"b", "cv"}}, kMathOut[] = {{"sum", "cv"}, {"diff", "cv"}, {"mul", "cv"}, {"min", "cv"}, {"max", "cv"}};
const ParamDef kMathP[] = {{"ka", 0, -1, 1, .01}, {"kb", 0, -1, 1, .01}};
class MathM : public Module {
 public:
  MathM(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In A = inp(io, 0), B = inp(io, 1); const double ka = io.p[0], kb = io.p[1];
    for (int i = 0; i < c.block; i++) {
      const double a = A[i] + ka, b = B[i] + kb;
      outp(io, 0)[i] = a + b; outp(io, 1)[i] = a - b; outp(io, 2)[i] = a * b; outp(io, 3)[i] = a < b ? a : b; outp(io, 4)[i] = a > b ? a : b;
    }
  }
};

// ---- Logic ---------------------------------------------------------------------------------------------------------------------------
const PortDef kLogIn[] = {{"a", "gate"}, {"b", "gate"}}, kLogOut[] = {{"and", "gate"}, {"or", "gate"}, {"xor", "gate"}, {"not", "gate"}};
const ParamDef kLogP[] = {{"thr", .5, .05, .95, .01}};
class Logic : public Module {
 public:
  Logic(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In A = inp(io, 0), B = inp(io, 1); const double t = io.p[0];
    for (int i = 0; i < c.block; i++) {
      const bool a = A[i] >= t, b = B[i] >= t;
      outp(io, 0)[i] = a && b ? 1 : 0; outp(io, 1)[i] = a || b ? 1 : 0; outp(io, 2)[i] = a != b ? 1 : 0; outp(io, 3)[i] = a ? 0 : 1;
    }
  }
};

// ---- Steps ---------------------------------------------------------------------------------------------------------------------------
const PortDef kStepsIn[] = {{"clock", "gate"}, {"reset", "gate"}}, kStepsOut[] = {{"out", "cv"}, {"gate", "gate"}};
const ParamDef kStepsP[] = {{"div", 3, 0, 9, 1}, {"len", 8, 1, 8, 1}, {"glide", 0, 0, 1, .01}, {"depth", 1, 0, 1, .01}, {"s1", 0, -1, 1, .01}, {"s2", 0, -1, 1, .01}, {"s3", 0, -1, 1, .01},
  {"s4", 0, -1, 1, .01}, {"s5", 0, -1, 1, .01}, {"s6", 0, -1, 1, .01}, {"s7", 0, -1, 1, .01}, {"s8", 0, -1, 1, .01}};
class Steps : public Module {
 public:
  Steps(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const bool ext = io.linked[0]; const double hz = c.tempo / 60 / divBeats(p[0]); int32_t n = i32(p[1]); if (!n) n = 8;
    const double k = p[2] > 0 ? 1 - js_exp(-1 / (c.sr * p[2] * .25 / hz)) : 1;
    if (rs_(c)) { ph_ = 0; step_ = 0; }
    const In CK = inp(io, 0), RS = inp(io, 1); const Out O = outp(io, 0), G = outp(io, 1);
    for (int i = 0; i < c.block; i++) {
      const bool rs = RS[i] > .5; if (rs && !r_) { ph_ = 0; step_ = 0; } r_ = rs;
      if (ext) { const bool g = CK[i] > .5; if (g && !c_) { step_ = (step_ + 1) % n; ph_ = 0; } c_ = g; }
      else { ph_ += hz / c.sr; if (ph_ >= 1) { ph_ -= 1; step_ = (step_ + 1) % n; } }
      if (step_ >= n) step_ = 0;
      const int idx = 4 + step_; const double tgt = (idx < 12 ? p[idx] : kNaN) * p[3]; y_ += (tgt - y_) * k;
      O[i] = y_; G[i] = ext ? (CK[i] > .5 ? 1 : 0) : (ph_ < .5 ? 1 : 0);
    }
  }
 private:
  Restart rs_; double ph_ = 0, y_ = 0; int32_t step_ = 0; bool c_ = false, r_ = false;
};

// ---- Clock ---------------------------------------------------------------------------------------------------------------------------
const PortDef kClkIn[] = {{"reset", "gate"}}, kClkOut[] = {{"gate", "gate"}, {"ramp", "cv"}, {"bar", "cv"}};
const ParamDef kClkP[] = {{"div", 3, 0, 9, 1}, {"swing", 0, 0, .75, .01}, {"width", .5, .05, .95, .01}, {"bars", 1, 1, 16, 1}};
class Clock : public Module {
 public:
  Clock(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double beats = divBeats(p[0]), hz = c.tempo / 60 / beats, sw = p[1], w = p[2];
    const double barHz = c.tempo / 60 / (4 * jmax(1, (double)i32(p[3])));
    if (rs_(c)) { ph_ = 0; n_ = 0; bp_ = 0; }
    const In RS = inp(io, 0); const Out G = outp(io, 0), RA = outp(io, 1), BA = outp(io, 2);
    for (int i = 0; i < c.block; i++) {
      const bool rs = RS[i] > .5; if (rs && !r_) { ph_ = 0; n_ = 0; bp_ = 0; } r_ = rs;
      ph_ += hz / c.sr; if (ph_ >= 1) { ph_ -= 1; n_ ^= 1; }
      bp_ += barHz / c.sr; if (bp_ >= 1) bp_ -= 1;
      const double off = n_ ? sw * .5 : 0, q = ph_ - off;
      G[i] = q >= 0 && q < w * (1 - off) ? 1 : 0;
      RA[i] = ph_; BA[i] = bp_;
    }
  }
 private:
  Restart rs_; double ph_ = 0, bp_ = 0; int32_t n_ = 0; bool r_ = false;
};

// ---- Clock multiplier ----------------------------------------------------------------------------------------------------------------
const PortDef kCmIn[] = {{"clock", "gate"}, {"reset", "gate"}}, kGateOut[] = {{"out", "gate"}};
const ParamDef kCmP[] = {{"mul", 2, 1, 16, 1}, {"width", .5, .05, .95, .01}};
class ClockMul : public Module {
 public:
  ClockMul(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In C = inp(io, 0), Rs = inp(io, 1); const Out O = outp(io, 0); const double M = jmax(1, (double)i32(io.p[0])), w = io.p[1];
    for (int i = 0; i < c.block; i++) {
      const bool r = Rs[i] >= .5; if (r && !lr_) { seen_ = 0; per_ = 0; t_ = 0; } lr_ = r;
      const bool cc = C[i] >= .5;
      if (cc && !lc_) { if (seen_) per_ = t_; seen_ = 1; t_ = 0; }
      lc_ = cc;
      if (!seen_) O[i] = 0;
      else if (!(per_ > 0)) O[i] = cc ? 1 : 0;
      else { const double sub = per_ / M, k = std::floor(t_ / sub); O[i] = k < M && t_ - k * sub < sub * w ? 1 : 0; }
      t_++;
    }
  }
 private:
  bool lc_ = false, lr_ = false; double t_ = 0, per_ = 0; int seen_ = 0;
};

// ---- Chance gate ---------------------------------------------------------------------------------------------------------------------
const PortDef kChIn[] = {{"in", "gate"}, {"reset", "gate"}}, kChOut[] = {{"out", "gate"}, {"other", "gate"}};
const ParamDef kChP[] = {{"prob", .5, 0, 1, .01}, {"seed", 1, 0, 999, 1}};
class ChanceGate : public Module {
 public:
  ChanceGate(double, Seeds& sd) : seeds_(sd) {}
  void process(const IO& io, const Ctx& c) override {
    const In G = inp(io, 0), Rs = inp(io, 1); const double p = io.p[0]; const int32_t sd = i32(io.p[1]);
    if (rs_(c) || sd_ != (double)sd) { sd_ = sd; if (sd) { s_ = imulSeed(sd) ? imulSeed(sd) : 1; } else { if (!free_) { const uint32_t n = seeds_.next(); free_ = n ? n : 1; } s_ = free_; } }
    for (int i = 0; i < c.block; i++) {
      const bool r = Rs[i] >= .5; if (r && !lr_ && sd) s_ = imulSeed(sd) ? imulSeed(sd) : 1; lr_ = r;
      const bool g = G[i] >= .5;
      if (g && !lg_) { uint32_t s = s_; s ^= s << 13; s ^= s >> 17; s ^= s << 5; s_ = s; if (!sd) free_ = s; pass_ = (double)s / 4294967296.0 < p ? 1 : 0; }
      lg_ = g;
      outp(io, 0)[i] = g && pass_ ? 1 : 0; outp(io, 1)[i] = g && !pass_ ? 1 : 0;
    }
  }
 private:
  Seeds& seeds_; Restart rs_; uint32_t s_ = 0, free_ = 0; double sd_ = -1; bool lg_ = false, lr_ = false; int pass_ = 0;
};

// ---- Counter -------------------------------------------------------------------------------------------------------------------------
const PortDef kCtIn[] = {{"clock", "gate"}, {"reset", "gate"}}, kCtOut[] = {{"step", "cv"}, {"end", "gate"}};
const ParamDef kCtP[] = {{"steps", 8, 1, 64, 1}, {"mode", 0, 0, 2, 1}};
class Counter : public Module {
 public:
  Counter(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In C = inp(io, 0), Rs = inp(io, 1); const double N = jmax(1, (double)i32(io.p[0])); const int32_t mode = i32(io.p[1]); const double L = mode == 2 && N > 1 ? 2 * N - 2 : N;
    if (rs_(c)) c_ = -1;
    for (int i = 0; i < c.block; i++) {
      const bool r = Rs[i] >= .5; if (r && !lr_) c_ = -1; lr_ = r;
      const bool cc = C[i] >= .5; if (cc && !lc_) c_ = jmod(c_ + 1, L); lc_ = cc;
      const double k = c_ < 0 ? 0 : jmod(c_, L), pos = mode == 1 ? N - 1 - k : mode == 2 && k >= N ? L - k : k;
      outp(io, 0)[i] = N > 1 ? pos / (N - 1) : 0; outp(io, 1)[i] = c_ >= 0 && k == L - 1 && cc ? 1 : 0;
    }
  }
 private:
  Restart rs_; double c_ = -1; bool lc_ = false, lr_ = false;
};

// ---- Step switch ---------------------------------------------------------------------------------------------------------------------
const PortDef kSwIn[] = {{"i1", "audio"}, {"i2", "audio"}, {"i3", "audio"}, {"i4", "audio"}, {"clock", "gate"}, {"reset", "gate"}}, kAudOut[] = {{"out", "audio"}};
const ParamDef kSwP[] = {{"steps", 4, 2, 4, 1}, {"fade", .005, 0, .1, .001}};
class StepSwitch : public Module {
 public:
  StepSwitch(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In I1 = inp(io, 0), I2 = inp(io, 1), I3 = inp(io, 2), I4 = inp(io, 3), C = inp(io, 4), Rs = inp(io, 5); const Out O = outp(io, 0); const double N = clamp((double)i32(io.p[0]), 2, 4);
    const double step = io.p[1] > 0 ? 1 / jmax(1, io.p[1] * c.sr) : 1;
    if (rs_(c)) k_ = 0;
    if (k_ >= N) k_ = 0;
    double k = k_, w0 = w0_, w1 = w1_, w2 = w2_, w3 = w3_;
    for (int i = 0; i < c.block; i++) {
      const bool r = Rs[i] >= .5, cc = C[i] >= .5;
      if (r && !lr_) k = 0; else if (cc && !lc_) k = jmod(k + 1, N);
      lr_ = r; lc_ = cc;
      if (k == 0) { if (w0 < 1) { w0 += step; if (w0 > 1) w0 = 1; } } else if (w0 > 0) { w0 -= step; if (w0 < 0) w0 = 0; }
      if (k == 1) { if (w1 < 1) { w1 += step; if (w1 > 1) w1 = 1; } } else if (w1 > 0) { w1 -= step; if (w1 < 0) w1 = 0; }
      if (k == 2) { if (w2 < 1) { w2 += step; if (w2 > 1) w2 = 1; } } else if (w2 > 0) { w2 -= step; if (w2 < 0) w2 = 0; }
      if (k == 3) { if (w3 < 1) { w3 += step; if (w3 > 1) w3 = 1; } } else if (w3 > 0) { w3 -= step; if (w3 < 0) w3 = 0; }
      O[i] = w0 == 1 ? I1[i] : w1 == 1 ? I2[i] : w2 == 1 ? I3[i] : w3 == 1 ? I4[i] : I1[i] * w0 + I2[i] * w1 + I3[i] * w2 + I4[i] * w3;
    }
    k_ = k; w0_ = w0; w1_ = w1; w2_ = w2; w3_ = w3;
  }
 private:
  Restart rs_; double k_ = 0, w0_ = 1, w1_ = 0, w2_ = 0, w3_ = 0; bool lc_ = false, lr_ = false;
};

// ---- Burst ---------------------------------------------------------------------------------------------------------------------------
const PortDef kBuIn[] = {{"trig", "gate"}}, kBuOut[] = {{"out", "gate"}, {"level", "cv"}};
const ParamDef kBuP[] = {{"count", 4, 1, 32, 1}, {"time", .06, .005, 1, .001}, {"bend", 0, -1, 1, .01}, {"width", .5, .05, .95, .01}};
class Burst : public Module {
 public:
  Burst(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In T = inp(io, 0); const double N = jmax(1, (double)i32(io.p[0])), base = jmax(1, io.p[1] * c.sr), w = io.p[3];
    const double ratio = js_pow(2, -io.p[2] * 3 / jmax(1, N - 1));
    for (int i = 0; i < c.block; i++) {
      const bool g = T[i] >= .5;
      if (g && !lt_) { n_ = N; of_ = N; t_ = 0; len_ = base; }
      lt_ = g;
      if (n_ > 0) {
        outp(io, 0)[i] = t_ < len_ * w ? 1 : 0; outp(io, 1)[i] = of_ > 1 ? (n_ - 1) / (of_ - 1) : 1;
        if (++t_ >= len_) { t_ = 0; n_--; len_ = jmax(1, len_ * ratio); }
      } else { outp(io, 0)[i] = 0; outp(io, 1)[i] = 0; }
    }
  }
 private:
  double n_ = 0, t_ = 0, len_ = 0, of_ = 0; bool lt_ = false;
};

// ---- Gate delay ----------------------------------------------------------------------------------------------------------------------
const PortDef kGdIn[] = {{"in", "gate"}};
const ParamDef kGdP[] = {{"delay", .1, 0, 2, .001}, {"length", 0, 0, 2, .001}};
class GateDelay : public Module {
 public:
  explicit GateDelay(double sr, Seeds&) : b_(new uint8_t[(size_t)std::ceil(sr * 2) + 2]()), M_((int)std::ceil(sr * 2) + 2) {}
  void process(const IO& io, const Ctx& c) override {
    const In G = inp(io, 0); const Out O = outp(io, 0); const int M = M_; const int d = (int)clamp(js_round(io.p[0] * c.sr), 0, M - 1); const double L = js_round(io.p[1] * c.sr);
    for (int i = 0; i < c.block; i++) {
      b_[w_] = G[i] >= .5 ? 1 : 0;
      int r = w_ - d; if (r < 0) r += M; const int x = b_[r];
      if (++w_ >= M) w_ = 0;
      if (L > 0) { if (x && !lo_) hold_ = L; O[i] = hold_ > 0 ? 1 : 0; if (hold_ > 0) hold_--; }
      else { O[i] = x; hold_ = 0; }
      lo_ = x;
    }
  }
 private:
  std::unique_ptr<uint8_t[]> b_; int M_, w_ = 0, lo_ = 0; double hold_ = 0;
};

// ---- Flip-flop -----------------------------------------------------------------------------------------------------------------------
const PortDef kFfIn[] = {{"toggle", "gate"}, {"set", "gate"}, {"reset", "gate"}}, kFfOut[] = {{"q", "gate"}, {"nq", "gate"}};
class FlipFlop : public Module {
 public:
  FlipFlop(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In T = inp(io, 0), S = inp(io, 1), Rs = inp(io, 2);
    if (rs_(c)) q_ = 0;
    for (int i = 0; i < c.block; i++) {
      const bool t = T[i] >= .5, s = S[i] >= .5, r = Rs[i] >= .5;
      if (t && !lt_) q_ ^= 1;
      if (s && !ls_) q_ = 1;
      if (r && !lr_) q_ = 0;
      lt_ = t; ls_ = s; lr_ = r;
      outp(io, 0)[i] = q_; outp(io, 1)[i] = 1 - q_;
    }
  }
 private:
  Restart rs_; int q_ = 0; bool lt_ = false, ls_ = false, lr_ = false;
};

// ---- Rectify -------------------------------------------------------------------------------------------------------------------------
const PortDef kRcOut[] = {{"inv", "cv"}, {"abs", "cv"}, {"pos", "cv"}, {"neg", "cv"}};
class Rectify : public Module {
 public:
  Rectify(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In X = inp(io, 0);
    for (int i = 0; i < c.block; i++) { const double x = X[i]; outp(io, 0)[i] = -x; outp(io, 1)[i] = x < 0 ? -x : x; outp(io, 2)[i] = x > 0 ? x : 0; outp(io, 3)[i] = x < 0 ? -x : 0; }
  }
};

// ---- Scale / map ---------------------------------------------------------------------------------------------------------------------
const ParamDef kScP[] = {{"ilo", -1, -1, 1, .01}, {"ihi", 1, -1, 1, .01}, {"olo", 0, -1, 1, .01}, {"ohi", 1, -1, 1, .01}, {"clip", 1, 0, 1, 1}};
class ScaleMap : public Module {
 public:
  ScaleMap(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const In X = inp(io, 0); const Out O = outp(io, 0); const double d = p[1] - p[0], k = d > 1e-6 || d < -1e-6 ? (p[3] - p[2]) / d : 0, a = p[0], b = p[2];
    const double lo = p[2] < p[3] ? p[2] : p[3], hi = p[2] < p[3] ? p[3] : p[2]; const bool clip = p[4] >= .5;
    for (int i = 0; i < c.block; i++) { double y = b + (X[i] - a) * k; if (clip) { if (y < lo) y = lo; else if (y > hi) y = hi; } O[i] = y; }
  }
};

// ---- Comparator ----------------------------------------------------------------------------------------------------------------------
const PortDef kCoIn[] = {{"a", "cv"}, {"b", "cv"}}, kCoOut[] = {{"gt", "gate"}, {"lt", "gate"}};
const ParamDef kCoP[] = {{"thr", 0, -1, 1, .01}, {"hyst", .02, 0, .5, .01}};
class Comparator : public Module {
 public:
  Comparator(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In A = inp(io, 0), B = inp(io, 1); const double t = io.p[0], h = io.p[1] * .5; double s = s_;
    for (int i = 0; i < c.block; i++) { const double d = A[i] - B[i] - t; if (d > h) s = 1; else if (!(d >= -h) || !(h > 0)) s = 0; outp(io, 0)[i] = s; outp(io, 1)[i] = 1 - s; }
    s_ = s;
  }
 private:
  double s_ = 0;
};

// ---- Switch --------------------------------------------------------------------------------------------------------------------------
const PortDef kS2In[] = {{"a", "audio"}, {"b", "audio"}, {"sel", "gate"}};
const ParamDef kS2P[] = {{"pick", 0, 0, 1, 1}, {"fade", .005, 0, .1, .001}};
class Switch2 : public Module {
 public:
  Switch2(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In A = inp(io, 0), B = inp(io, 1), S = inp(io, 2); const Out O = outp(io, 0); const double base = io.p[0] >= .5 ? 1 : 0;
    const double step = io.p[1] > 0 ? 1 / jmax(1, io.p[1] * c.sr) : 1; double g = g_;
    if (g < 0) g = (S[0] >= .5 ? 1 - base : base);
    for (int i = 0; i < c.block; i++) {
      const double want = S[i] >= .5 ? 1 - base : base;
      if (g < want) { g += step; if (g > want) g = want; } else if (g > want) { g -= step; if (g < want) g = want; }
      O[i] = g <= 0 ? A[i] : g >= 1 ? B[i] : A[i] + (B[i] - A[i]) * g;
    }
    g_ = g;
  }
 private:
  double g_ = -1;
};

// ---- Crossfade -----------------------------------------------------------------------------------------------------------------------
const PortDef kXfIn[] = {{"a", "audio"}, {"b", "audio"}, {"mixin", "cv"}};
const ParamDef kXfP[] = {{"mix", .5, 0, 1, .01}, {"curve", 0, 0, 1, 1}};
class Crossfade : public Module {
 public:
  Crossfade(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In A = inp(io, 0), B = inp(io, 1), C = inp(io, 2); const Out O = outp(io, 0); const double m = io.p[0]; const bool eq = io.p[1] >= .5;
    for (int i = 0; i < c.block; i++) {
      double x = m + C[i]; if (!(x > 0)) x = 0; else if (x > 1) x = 1;
      if (eq) { const double t = x * 1.5707963267948966; O[i] = A[i] * js_cos(t) + B[i] * js_sin(t); }
      else O[i] = A[i] + (B[i] - A[i]) * x;
    }
  }
};

// ---- Constant ------------------------------------------------------------------------------------------------------------------------
const ParamDef kKP[] = {{"value", 0, -1, 1, .01}, {"fine", 0, -.01, .01, .0001}};
class Constant : public Module {
 public:
  Constant(double, Seeds&) {}
  void process(const IO& io, const Ctx&) override { outp(io, 0).fill(kBlock, io.p[0] + io.p[1]); }   // (fill(): the whole buffer, whatever ctx.block is)
};

// ---- Divider -------------------------------------------------------------------------------------------------------------------------
const PortDef kDvOut[] = {{"d2", "gate"}, {"d4", "gate"}, {"d8", "gate"}, {"n", "gate"}};
const ParamDef kDvP[] = {{"n", 3, 1, 32, 1}};
class Divider : public Module {
 public:
  Divider(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const In C = inp(io, 0), Rs = inp(io, 1); const double N = jmax(1, (double)i32(io.p[0]));
    for (int i = 0; i < c.block; i++) {
      const bool r = Rs[i] >= .5; if (r && !lr_) c_ = -1; lr_ = r;
      const bool cc = C[i] >= .5; if (cc && !lc_) c_++; lc_ = cc;
      const double k = c_;
      outp(io, 0)[i] = cc && jmod(k, 2) == 0 ? 1 : 0; outp(io, 1)[i] = cc && jmod(k, 4) == 0 ? 1 : 0; outp(io, 2)[i] = cc && jmod(k, 8) == 0 ? 1 : 0; outp(io, 3)[i] = cc && jmod(k, N) == 0 ? 1 : 0;
    }
  }
 private:
  double c_ = -1; bool lc_ = false, lr_ = false;
};

// ---- Euclidean -----------------------------------------------------------------------------------------------------------------------
const PortDef kEuOut[] = {{"gate", "gate"}, {"accent", "gate"}};
const ParamDef kEuP[] = {{"steps", 16, 1, 32, 1}, {"hits", 5, 0, 32, 1}, {"rot", 0, 0, 31, 1}, {"div", 3, 0, 9, 1}, {"len", .5, .05, .95, .01}};
inline bool eucHit(double k, double rot, double S, double H) { const double j = jmod(jmod(k - rot, S) + S, S); return H > 0 && jmod(j * H, S) < H; }
inline bool eucFirst(double k, double rot, double S, double H) { for (double j = 0; j < S; j++) if (eucHit(j, rot, S, H)) return j == jmod(jmod(k, S) + S, S); return false; }
class Euclid : public Module {
 public:
  Euclid(double, Seeds&) {}
  void process(const IO& io, const Ctx& c) override {
    const double* p = io.p; const double S = jmax(1, (double)i32(p[0])), H = clamp((double)i32(p[1]), 0, S), rot = i32(p[2]); const bool ck = io.linked[0];
    if (rs_(c)) { step_ = -1; ph_ = 1; }
    const double inc = (truthy(c.tempo) ? c.tempo : 120) / 60 / divBeats(p[3]) / c.sr;
    const In RS = inp(io, 1), CK = inp(io, 0);
    for (int i = 0; i < c.block; i++) {
      const bool r = RS[i] >= .5; if (r && !lr_) { step_ = -1; ph_ = 1; } lr_ = r;
      bool on;
      if (ck) { const bool cc = CK[i] >= .5; if (cc && !lc_) step_ = jmod(step_ + 1, S); lc_ = cc; on = cc; }
      else { ph_ += inc; if (ph_ >= 1) { ph_ -= std::floor(ph_); step_ = jmod(step_ + 1, S); } on = ph_ < p[4]; }
      const bool h = step_ >= 0 && on && eucHit(step_, rot, S, H);
      outp(io, 0)[i] = h ? 1 : 0; outp(io, 1)[i] = h && eucFirst(step_, rot, S, H) ? 1 : 0;
    }
  }
 private:
  Restart rs_; double step_ = -1, ph_ = 1; bool lc_ = false, lr_ = false;
};

template <class M> std::unique_ptr<Module> mk(double sr, Seeds& s) { return std::unique_ptr<Module>(new M(sr, s)); }
const PortDef kNone[] = {{"", ""}};
const Def kDefs[] = {
  {"envfol", PD(kEnvIn), PD(kEnvOut), PD(kEnvP), mk<EnvFol>},
  {"gateex", PD(kEnvIn), PD(kGxOut), PD(kGxP), mk<GateEx>},
  {"slew", PD(kCvIn), PD(kCvOut), PD(kSlewP), mk<Slew>},
  {"snh", PD(kShIn), PD(kShOut), PD(kShP), mk<SampleHold>},
  {"quant", PD(kCvIn), PD(kCvOut), PD(kQuantP), mk<Quant>},
  {"math", PD(kMathIn), PD(kMathOut), PD(kMathP), mk<MathM>},
  {"logic", PD(kLogIn), PD(kLogOut), PD(kLogP), mk<Logic>},
  {"steps", PD(kStepsIn), PD(kStepsOut), PD(kStepsP), mk<Steps>},
  {"clock", PD(kClkIn), PD(kClkOut), PD(kClkP), mk<Clock>},
  {"clkmul", PD(kCmIn), PD(kGateOut), PD(kCmP), mk<ClockMul>},
  {"chance", PD(kChIn), PD(kChOut), PD(kChP), mk<ChanceGate>},
  {"counter", PD(kCtIn), PD(kCtOut), PD(kCtP), mk<Counter>},
  {"stepsw", PD(kSwIn), PD(kAudOut), PD(kSwP), mk<StepSwitch>},
  {"burst", PD(kBuIn), PD(kBuOut), PD(kBuP), mk<Burst>},
  {"gatedly", PD(kGdIn), PD(kGateOut), PD(kGdP), mk<GateDelay>},
  {"flipflop", PD(kFfIn), PD(kFfOut), 0, nullptr, mk<FlipFlop>},
  {"rect", PD(kCvIn), PD(kRcOut), 0, nullptr, mk<Rectify>},
  {"scale", PD(kCvIn), PD(kCvOut), PD(kScP), mk<ScaleMap>},
  {"compare", PD(kCoIn), PD(kCoOut), PD(kCoP), mk<Comparator>},
  {"switch", PD(kS2In), PD(kAudOut), PD(kS2P), mk<Switch2>},
  {"xfade", PD(kXfIn), PD(kAudOut), PD(kXfP), mk<Crossfade>},
  {"const", 0, kNone, PD(kCvOut), PD(kKP), mk<Constant>},
  {"divider", PD(kCmIn), PD(kDvOut), PD(kDvP), mk<Divider>},
  {"euclid", PD(kCmIn), PD(kEuOut), PD(kEuP), mk<Euclid>},
};
}  // namespace

void addCvMods(std::vector<const Def*>& v) { for (const Def& d : kDefs) v.push_back(&d); }

}  // namespace modfx
}  // namespace thunder
