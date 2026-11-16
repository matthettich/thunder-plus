// The engine foundation (U22): see engine.h. Line for line from index.html's ENGINE block (renderLayer, makeOsc, voiceMod, voiceFilter, voiceAmp, renderLayersOnly, renderVoice,
// mkVoice, freshLayer, the tables). Every expression keeps the JS's order of operations (contraction is off for this target).
#include "engine/engine.h"

#include <cmath>
#include <cstring>
#include <vector>

#include "fx/u17cores.h"
#include "rt/jsmath.h"

namespace thunder {

// ---- tables ---------------------------------------------------------------------------------------------------------------------------------------------
const double kRatios[24] = {0.25, 0.5, 0.75, 1, 1.25, 1.5, 1.75, 2, 2.5, 3, 3.5, 4, 4.5, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 16};
const double kMem[12] = {1, 1.594, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, 3.6, 3.652, 4.06, 4.154};
const double kBell[12] = {0.5, 1, 1.183, 1.506, 2, 2.514, 2.662, 3.011, 4.166, 5.433, 6.796, 8.215};
const double kBellA[12] = {0.9, 1, 0.7, 0.6, 0.55, 0.45, 0.4, 0.35, 0.28, 0.22, 0.16, 0.12};
const double kBar[6] = {1, 2.756, 5.404, 8.933, 13.344, 18.638};
const double kHat[6] = {205.3, 304.4, 369.6, 522.7, 540, 800};
const double kSyncBeats[9] = {0, 0.125, 0.25, 0.5, 1, 2, 4, 8, 16};
const EngChord kChords[14] = {
    {"Octave", 2, {0, 12}}, {"Fifth", 2, {0, 7}}, {"5th + oct", 3, {0, 7, 12}}, {"Major", 3, {0, 4, 7}}, {"Minor", 3, {0, 3, 7}},
    {"Sus2", 3, {0, 2, 7}}, {"Sus4", 3, {0, 5, 7}}, {"Dom 7", 4, {0, 4, 7, 10}}, {"Minor 7", 4, {0, 3, 7, 10}}, {"Major 7", 4, {0, 4, 7, 11}},
    {"Minor 9", 5, {0, 3, 7, 10, 14}}, {"Dim", 3, {0, 3, 6}}, {"Aug", 3, {0, 4, 8}}, {"4ths", 4, {0, 5, 10, 15}}};

namespace {
struct SinTable {
  float v[kSinTS + 1];
  SinTable() { for (int i = 0; i <= kSinTS; i++) v[i] = (float)js_sin((double)i / (double)kSinTS * 2 * 3.141592653589793); }   // Math.sin(i / TS * 2 * Math.PI)
};
const SinTable& sinTable() { static const SinTable t; return t; }

// Math.max / Math.min on two doubles: NaN wins, and +0 beats -0 (max) / -0 beats +0 (min)
inline double jmax(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double jmin(double a, double b) { if (a != a || b != b) return NAN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline double orv(double x, double d) { return (x != 0 && x == x) ? x : d; }   // x || d
inline float f32(double x) { return (float)x; }
}  // namespace

void eng_init() { (void)sinTable(); }
const float* eng_sin_table() { return sinTable().v; }

double eng_sinT(double p) {
  const float* SIN = sinTable().v;
  p -= std::floor(p);
  const double x = p * kSinTS; const int32_t i = js_to_int32(x);
  // SIN[i] and SIN[i + 1] past the table's end are undefined in the JS (NaN in the sum); i is 0..4096 for p in [0, 1], and 4096 only when p rounded up to exactly 1
  const double a = (i >= 0 && i <= kSinTS) ? (double)SIN[i] : NAN, b = (i + 1 >= 0 && i + 1 <= kSinTS) ? (double)SIN[i + 1] : NAN;
  return a + (b - a) * (x - i);
}

double eng_blep(double t, double dt) {
  if (t < dt) { t /= dt; return t + t - t * t - 1; }
  if (t > 1 - dt) { t = (t - 1) / dt; return t * t + t + t + 1; }
  return 0;
}

double eng_hash_rnd(double n, double k) {
  uint32_t x = (uint32_t)js_to_int32(n * 374761393.0 + k * 668265263.0);
  x = js_imul(x ^ (x >> 13), 1274126177u); x ^= x >> 16;
  return (double)x / 2147483648.0 - 1;
}

void EngRng::reseed(double seed) { uint32_t u = js_to_uint32(seed); s = u ? u : 1; }

uint32_t EngSeedCounter::next() {
  const double p = (double)state * 1103515245.0 + 12345.0;
  state = js_to_uint32(p);
  return state ? state : 1;   // (_seed = ...) || 1 : the layer gets 1 when the state is 0, the state stays 0
}

LType eng_layer_type(const char* name, size_t n) {
  static const struct { const char* s; LType t; } T[] = {{"sine", LType::Sine}, {"tri", LType::Tri}, {"saw", LType::Saw}, {"square", LType::Square}, {"pulse", LType::Pulse},
    {"white", LType::White}, {"pink", LType::Pink}, {"brown", LType::Brown}, {"blue", LType::Blue}, {"metal", LType::Metal}, {"lfsr", LType::Lfsr}, {"dust", LType::Dust}, {"grit", LType::Grit}};
  for (const auto& e : T) if (std::strlen(e.s) == n && std::memcmp(e.s, name, n) == 0) return e.t;
  return LType::Other;
}
bool eng_type_supported(LType t) { return t != LType::Other; }

EngFx::EngFx() {
  const double nan = NAN;
  bits = rate = noise = color = follow = dry = wet = nan; note = chord = decay = bright = mode = spread = mix = nan; time = fb = pitch = pitch2 = tone = size = wow = nan;
}

// ---- the oscillator (makeOsc's closures: s(inc) is the sample, blk(...) is a no-op for these types) ------------------------------------------------------
namespace {
struct Osc {
  LType t = LType::Other;
  EngRng R;
  // sine / tri / saw / square / pulse
  double ph = 0, pm = 0, ps = 0, ratio = 1, g = 1, w = 0;
  bool fold = false, sq = false;
  // white / pink / brown / blue
  int kind = 0; double H = 1, q = 0, c = 0, held = 0, b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, br = 0, pw = 0;
  // metal
  double fr[6] = {}, pp[6] = {};
  // lfsr
  double mult = 0, acc = 0, out = 1; bool shortLoop = false; int32_t reg = 0x7fff;
  // dust / grit
  double p = 0, cf = 0, k = 0, y = 0, cg = 0, accg = 1;

  void init(const EngLayer& L, double sr) {
    const double A = L.a, B = L.b; t = L.type; R.reseed(orv(L.seed, 7));   // rng(L.seed || 7)
    switch (t) {
      case LType::Sine: ph = B; g = 1 + A * A * 8; fold = A > 0.002; break;
      case LType::Tri: ph = B; g = 1 + A * A * 8; fold = A > 0.002; break;
      case LType::Saw: case LType::Square: ratio = 1 + A * 7; sq = (t == LType::Square); pm = B; ps = eng_frac(B * ratio); break;
      case LType::Pulse: w = 0.5 - A * 0.47; ph = B; break;
      case LType::White: case LType::Pink: case LType::Brown: case LType::Blue:
        kind = t == LType::White ? 0 : t == LType::Pink ? 1 : t == LType::Brown ? 2 : 3;
        H = 1 + js_round(A * A * 63 * sr / 48000); q = B > 0.01 ? js_pow(2, 16 - B * 14) / 2 : 0; c = 0; held = 0; break;
      case LType::Metal: {
        const double ex = 0.6 + A * 0.8; w = 0.5 - B * 0.4;
        for (int i = 0; i < 6; i++) { fr[i] = 205.3 * js_pow(kHat[i] / 205.3, ex) / kBase; pp[i] = i * 0.137; }
        break; }
      case LType::Lfsr: mult = js_pow(2, A * 8 - 1) * 16; shortLoop = B >= 0.5; reg = 0x7fff; acc = 0; out = 1; break;
      case LType::Dust: p = A * A * 0.25 * 48000 / sr; cf = 0.03 + B * 0.97; k = 1.5 / (0.2 + cf); y = 0; break;
      case LType::Grit: mult = js_pow(2, B * 6) * 4; cg = 1 - A * 0.995; accg = 1; held = 0; y = 0; break;
      default: break;
    }
  }

  double s(double inc) {
    switch (t) {
      case LType::Sine: { const double yy = eng_sinT(ph); ph += inc; return fold ? eng_sinT(yy * g * 0.25) : yy; }
      case LType::Tri: { const double yy = 1 - 4 * std::fabs(eng_frac(ph + 0.25) - 0.5); ph += inc; return fold ? eng_sinT(yy * g * 0.25) : yy; }
      case LType::Saw: case LType::Square: {
        const double dt = jmin(inc * ratio, 0.5); double yy;
        if (sq) yy = (ps < 0.5 ? 1 : -1) + eng_blep(ps, dt) - eng_blep(eng_frac(ps + 0.5), dt); else yy = 2 * ps - 1 - eng_blep(ps, dt);
        pm += inc; ps += inc * ratio; if (pm >= 1) { pm -= std::floor(pm); ps = pm * ratio; } if (ps >= 1) ps -= std::floor(ps);
        return yy * 0.8; }
      case LType::Pulse: {
        const double dt = jmin(inc, 0.5);
        const double yy = (ph < w ? 1 : -1) + eng_blep(ph, dt) - eng_blep(eng_frac(ph + 1 - w), dt) - (2 * w - 1);
        ph += inc; if (ph >= 1) ph -= std::floor(ph); return yy * 0.8; }
      case LType::White: case LType::Pink: case LType::Brown: case LType::Blue: {
        if (c <= 0) {
          c = H; const double wv = R.next() * 2 - 1; double yy;
          if (kind == 0) yy = wv;
          else if (kind == 1) {
            b0 = 0.99886 * b0 + wv * 0.0555179; b1 = 0.99332 * b1 + wv * 0.0750759; b2 = 0.969 * b2 + wv * 0.153852; b3 = 0.8665 * b3 + wv * 0.3104856;
            b4 = 0.55 * b4 + wv * 0.5329522; b5 = -0.7616 * b5 - wv * 0.016898; yy = (b0 + b1 + b2 + b3 + b4 + b5 + b6 + wv * 0.5362) * 0.11; b6 = wv * 0.115926;
          } else if (kind == 2) { br = (br + 0.02 * wv) / 1.02; yy = br * 3.5; }
          else { yy = (wv - pw) * 0.5; pw = wv; }
          if (q != 0) yy = js_round(yy * q) / q;
          held = yy;
        }
        c--; return held; }
      case LType::Metal: {
        double yy = 0;
        for (int i = 0; i < 6; i++) { double pv = pp[i] + inc * fr[i]; pv -= std::floor(pv); pp[i] = pv; yy += pv < w ? 1 : -1; }
        return (yy / 6 - (2 * w - 1)) * 0.9; }
      case LType::Lfsr: {
        acc += inc * mult; if (acc > 64) acc = 64;
        while (acc >= 1) {
          acc -= 1; const int32_t fb = (reg ^ (reg >> 1)) & 1; reg = (reg >> 1) | (fb << 14); if (shortLoop) reg = (reg & ~64) | (fb << 6); out = (reg & 1) ? 1 : -1;
        }
        return out * 0.7; }
      case LType::Dust: {
        const double imp = R.next() < p ? (R.next() * 2 - 1) : 0; y += (imp - y) * cf; return jmax(-1, jmin(1, y * k)); }
      case LType::Grit: {
        accg += inc * mult; if (accg >= 1) { accg -= std::floor(accg); held = R.next() * 2 - 1; } y += (held - y) * cg; return y; }
      default: return 0;
    }
  }
};

// ---- voiceMod ---------------------------------------------------------------------------------------------------------------------------------------------
inline void addMod(EngMod& m, int dst, double v) { if (dst == 1) m.pitch += v; else if (dst == 2) m.level += v; else if (dst == 3) m.lpcut += v; else if (dst == 4) m.hpcut += v; }
inline int dstOf(double d) { return js_to_int32(d); }   // V['r' + i + 'dst'] | 0
inline double syncRate(double sync, double bpm) {      // songBpm() / 60 / SYNC_BEATS[sy]  (an index past the table: undefined: NaN)
  const int32_t sy = js_to_int32(sync); return (sy >= 0 && sy <= 8) ? bpm / 60 / kSyncBeats[sy] : NAN;
}
}  // namespace

EngMod eng_voice_mod(const EngVoice& V, double t, double bpmIn) {
  const double bpm = orv(bpmIn, 120);
  EngMod m{0, 0, 0, 0};
  const double gate = jmax(V.gate, 1) / 1000;
  for (int i = 1; i <= 3; i++) {
    const int j = i - 1;
    const double sy = (double)js_to_int32(V.lfoSync[j]);
    const double rate = sy != 0 ? syncRate(sy, bpm) : orv(V.lfoRate[j], 0);
    const int32_t shape = js_to_int32(V.lfoShape[j]); const double dep = orv(V.lfoDepth[j], 0);
    const double tp0 = t * rate, n = std::floor(tp0); const double p = tp0 - n;
    const double s = shape == 1 ? (p < .5 ? 1 : -1) : shape == 2 ? 2 * p - 1 : shape == 3 ? 1 - 4 * std::fabs(p - .5)
      : shape == 4 ? eng_hash_rnd(n, i) : shape == 5 ? eng_hash_rnd(n, i) + (eng_hash_rnd(n + 1, i) - eng_hash_rnd(n, i)) * (0.5 - 0.5 * js_cos(3.141592653589793 * p))
      : js_sin(2 * 3.141592653589793 * p);
    addMod(m, dstOf(V.rdst[j]), s * dep * orv(V.ramt[j], 0));
  }
  for (int i = 1; i <= 3; i++) {
    const int j = i - 1;
    const double a = jmax(orv(V.envA[j], 1), 1) / 1000, d = jmax(orv(V.envD[j], 1), 1) / 1000, sus = orv(V.envS[j], 0), r = jmax(orv(V.envR[j], 1), 1) / 1000;
    double e;
    if (t < a) e = t / a;
    else if (t < gate) e = sus + (1 - sus) * js_exp(-6.9 * (t - a) / d);
    else { const double tg = sus + (1 - sus) * js_exp(-6.9 * jmax(gate - a, 0) / d); e = tg * js_exp(-6.9 * (t - gate) / r); }
    addMod(m, dstOf(V.rdst[3 + j]), e * orv(V.ramt[3 + j], 0));
  }
  {
    const double ss = (double)js_to_int32(V.seqSync), rate = ss != 0 ? syncRate(ss, bpm) : orv(V.seqRate, 4);
    const double idx = std::fmod(std::fmod(std::floor(t * rate), 32.0) + 32.0, 32.0);   // ((Math.floor(t * rate) % 32) + 32) % 32
    const double step = (idx >= 0 && idx < 32 && idx == std::floor(idx)) ? orv(V.seq[(int)idx], 0) : 0;   // steps[idx] || 0 (NaN: undefined)
    addMod(m, dstOf(V.rdst[6]), step * orv(V.seqDepth, 0) * orv(V.ramt[6], 0));
  }
  return m;
}

// ---- voice length -----------------------------------------------------------------------------------------------------------------------------------------
namespace {
inline double layerLen(const EngLayer& L, double gate) {
  const int32_t nb = js_to_int32(L.burst); const double sp = L.spacing / 1000;
  const double e = L.adsr ? jmax(gate / 1000, L.atk / 1000) + L.rel / 1000 : (L.atk + L.hold + L.dec) / 1000;
  return nb * sp + e;
}
double voiceLen(const EngVoice& V, double gate, double len) {
  double m = 0.01;
  for (int i = 0; i < V.nLayers; i++) { const EngLayer& L = V.layers[i]; if (L.on && L.level > 0) m = jmax(m, L.delay / 1000 + layerLen(L, gate)); }
  return jmin(m + 0.005, len);
}

// ---- renderLayer ----------------------------------------------------------------------------------------------------------------------------------------
void renderLayer(const EngLayer& L, const EngVoice& V, double bpm, double sr, float* out, size_t N) {
  const double d0 = js_round(L.delay / 1000 * sr); if (d0 >= (double)N) return;
  const double f0 = kBase * js_pow(2, (L.tune + V.tune + L.fine / 100) / 12);
  const double ptau = jmax(L.pDec, 1) / 4000, pA = L.pAmt;
  const double gPtau = jmax(orv(V.gPDec, 60), 1) / 4000, gPA = orv(V.gPAmt, 0);
  const double atk = jmax(L.atk, 0.05) / 1000, hold = L.hold / 1000, dec = jmax(L.dec, 1) / 1000, sus = L.sus, rel = jmax(L.rel, 1) / 1000, gate = V.gate / 1000;
  const bool adsr = L.adsr;
  const int32_t nb = js_to_int32(L.burst); const double sp = L.spacing / 1000, bd = jmax(jmin(dec, sp * 1.5), 0.003);
  auto lv = [&](double t) { return t < atk ? t / atk : sus + (1 - sus) * js_exp(-6.9 * (t - atk) / dec); };
  const double gLv = lv(gate);
  auto env1 = [&](double t) -> double {
    if (t < 0) return 0;
    if (adsr) return t < gate ? lv(t) : gLv * js_exp(-6.9 * (t - gate) / rel);
    if (t < atk) return t / atk;
    if (t < atk + hold) return 1;
    return js_exp(-6.9 * (t - atk - hold) / dec);
  };
  auto ampAt = [&](double t) {
    double m = env1(t - nb * sp);
    for (int32_t k = 0; k < nb; k++) { const double tk = t - k * sp; if (tk < 0) break; const double e = tk < 0.0004 ? tk / 0.0004 : js_exp(-6.9 * (tk - 0.0004) / bd); if (e > m) m = e; }
    return m;
  };
  const double fA = jmax(L.fAtk, 0.05) / 1000, fD = jmax(L.fDec, 1) / 1000;
  auto fenv = [&](double t) { return t < fA ? t / fA : js_exp(-6.9 * (t - fA) / fD); };
  bool modLp = false, modHp = false;
  for (int k = 0; k < 7; k++) { if (dstOf(V.rdst[k]) == 3 && orv(V.ramt[k], 0) != 0) modLp = true; if (dstOf(V.rdst[k]) == 4 && orv(V.ramt[k], 0) != 0) modHp = true; }
  const bool lpOn = L.lpCut < 19900 || L.lpEnv != 0 || modLp, hpOn = L.hpCut > 20.5 || L.hpEnv != 0 || modHp;
  const double nyq = sr * 0.45;
  double l1 = 0, l2 = 0, h1 = 0, h2 = 0, la1 = 0, la2 = 0, la3 = 0, lk = 2, ha1 = 0, ha2 = 0, ha3 = 0, hk = 2;
  const double tEnd = layerLen(L, V.gate) + 0.002;
  Osc osc; osc.init(L, sr);
  const double gain = L.level * L.level, M = (double)N - d0; const int BLK = 16;
  double ePrev = ampAt(0);
  for (double i = 0; i < M; i += BLK) {
    const double t = i / sr; if (t > tEnd) break;
    const double e0 = ePrev, e1 = ampAt((i + BLK) / sr); ePrev = e1;
    const EngMod mo = eng_voice_mod(V, t, bpm);
    const double inc = f0 * js_pow(2, (pA * js_exp(-t / ptau) + (gPA != 0 ? gPA * js_exp(-((d0 + i) / sr) / gPtau) : 0) + mo.pitch) / 12) / sr;
    if (lpOn || hpOn) {
      const double fe = fenv(t);
      if (hpOn) { const double fc = jmin(jmax(L.hpCut * js_pow(2, L.hpEnv * 8 * fe + mo.hpcut), 10), nyq), gg = js_tan(3.141592653589793 * fc / sr);
        hk = 2 - 2 * jmin(L.hpRes, 0.98); ha1 = 1 / (1 + gg * (gg + hk)); ha2 = gg * ha1; ha3 = gg * ha2; }
      if (lpOn) { const double fc = jmin(jmax(L.lpCut * js_pow(2, L.lpEnv * 8 * fe + mo.lpcut), 10), nyq), gg = js_tan(3.141592653589793 * fc / sr);
        lk = 2 - 2 * jmin(L.lpRes, 0.98); la1 = 1 / (1 + gg * (gg + lk)); la2 = gg * la1; la3 = gg * la2; }
    }
    const double lvl = jmax(0, 1 + mo.level);
    const double n = jmin(BLK, M - i), de = (e1 - e0) / BLK, o = d0 + i;
    for (int j = 0; (double)j < n; j++) {
      double x = osc.s(inc);
      if (hpOn) { const double v3 = x - h2, v1 = ha1 * h1 + ha2 * v3, v2 = h2 + ha2 * h1 + ha3 * v3; h1 = 2 * v1 - h1; h2 = 2 * v2 - h2; x = x - hk * v1 - v2; }
      if (lpOn) { const double v3 = x - l2, v1 = la1 * l1 + la2 * v3, v2 = l2 + la2 * l1 + la3 * v3; l1 = 2 * v1 - l1; l2 = 2 * v2 - l2; x = v2; }
      const double pos = o + j;   // out[pos] += ...: a position outside the typed array (a negative delay) is read as undefined and the write is dropped
      if (pos >= 0 && pos < (double)N) { const size_t idx = (size_t)pos; out[idx] = f32((double)out[idx] + x * (e0 + de * j) * gain * lvl); }
    }
  }
}

void voiceFilter(const EngVoice& V, double sr, float* out, size_t N) {
  const double nyq = sr * 0.45, fA = jmax(V.gFAtk, 0.05) / 1000, fD = jmax(V.gFDec, 1) / 1000; const size_t BLK = 16;
  auto fenv = [&](double t) { return t < fA ? t / fA : js_exp(-6.9 * (t - fA) / fD); };
  double l1 = 0, l2 = 0, lk = 2, la1 = 0, la2 = 0, la3 = 0;
  for (size_t i = 0; i < N; i += BLK) {
    const double fc = jmin(jmax(V.gLpCut * js_pow(2, V.gLpEnv * 8 * fenv((double)i / sr)), 10), nyq), g = js_tan(3.141592653589793 * fc / sr);
    lk = 2 - 2 * jmin(V.gLpRes, 0.98); la1 = 1 / (1 + g * (g + lk)); la2 = g * la1; la3 = g * la2;
    const size_t n = BLK < N - i ? BLK : N - i;
    for (size_t j = 0; j < n; j++) { const double x = out[i + j], v3 = x - l2, v1 = la1 * l1 + la2 * v3, v2 = l2 + la2 * l1 + la3 * v3; l1 = 2 * v1 - l1; l2 = 2 * v2 - l2; out[i + j] = f32(v2); }
  }
}

void voiceAmp(const EngVoice& V, double sr, float* out, size_t N) {
  const double atk = jmax(V.gAtk, 0.05) / 1000, hold = V.gHold / 1000, dec = jmax(V.gDec, 1) / 1000, sus = V.gSus, rel = jmax(V.gRel, 1) / 1000, gate = V.gate / 1000;
  const bool adsr = V.gAdsr;
  const double amt = V.gAmpAmt != V.gAmpAmt ? 1 : jmax(0, jmin(1, V.gAmpAmt));
  auto lv = [&](double t) { return t < atk ? t / atk : sus + (1 - sus) * js_exp(-6.9 * (t - atk) / dec); };
  const double gLv = lv(gate);
  auto env = [&](double t) -> double {
    if (t < 0) return 0;
    if (adsr) return t < gate ? lv(t) : gLv * js_exp(-6.9 * (t - gate) / rel);
    if (t < atk) return t / atk;
    if (t < atk + hold) return 1;
    return js_exp(-6.9 * (t - atk - hold) / dec);
  };
  for (size_t i = 0; i < N; i++) out[i] = f32((double)out[i] * (1 + (env((double)i / sr) - 1) * amt));
}

// the pitch effects' tail (applyFx: Math.round(sr * (...))), for the size bound only
double pitchTail(const EngFx& f, double sr) {
  const bool rv = f.kind == EngFx::Kind::PitchReverb;
  return js_round(sr * (rv ? 3 + f.size * 4 : jmin(8, f.time / 1000 * (1 + 6 * f.fb))));
}
inline bool fxReady(const EngFx& f) { return f.kind != EngFx::Kind::Other; }

struct Plan { double N, extN; };
// renderLayersOnly's lengths (V already has the hold applied)
Plan planOf(const EngVoice& V, const EngRenderOpts& o) {
  Plan p{0, 0};
  const double gRel = jmax(orv(V.gRel, 1), 1) / 1000, sr = o.sr;
  double N = jmax(1, std::ceil(voiceLen(V, V.gate, V.len) * sr));
  if (V.gAmpOn) N = jmin(std::ceil(jmax(voiceLen(V, V.gate, V.len), V.gate / 1000 + gRel) * sr), sr * (kSmpMax + 32));
  p.N = N;
  const double tailN = js_round(orv(V.tail, 0) * sr);
  p.extN = jmin(N + tailN, sr * (kSmpMax + 12));
  return p;
}
bool anyFxOn(const EngVoice& V) { for (int i = 0; i < V.nFx; i++) if (V.fx[i].on && fxReady(V.fx[i])) return true; return false; }
}  // namespace

namespace {
// renderLayersOnly: `if (hold) V = Object.assign({}, V, {gate: hold * 1000, len: hold})`: a copy on the stack (no allocation)
inline void applyHold(EngVoice& W, const EngRenderOpts& o) { if (o.hold != 0 && o.hold == o.hold) { W.gate = o.hold * 1000; W.len = o.hold; } }
}

size_t eng_layers_len(const EngVoice& V0, const EngRenderOpts& o) { EngVoice V = V0; applyHold(V, o); const Plan p = planOf(V, o); return (p.N >= 0 && p.N < 4e9) ? (size_t)p.N : 0; }

size_t eng_render_bound(const EngVoice& V0, const EngRenderOpts& o) {
  EngVoice V = V0; applyHold(V, o);
  const Plan p = planOf(V, o);
  if (!(p.N >= 1 && p.N < 4e9)) return 0;
  double len = p.N;
  if (anyFxOn(V)) {
    double ext = p.extN >= 0 ? std::floor(p.extN) : 0; len = ext > len ? ext : len; double cur = ext;
    for (int i = 0; i < V.nFx; i++) { const EngFx& f = V.fx[i]; if (!f.on || !fxReady(f)) continue;
      if (f.kind == EngFx::Kind::PitchDelay || f.kind == EngFx::Kind::PitchReverb) { const double t = pitchTail(f, o.sr); if (t >= 0 && t < 4e9) cur += t; if (cur > len) len = cur; } }
  }
  return len < 4e9 ? (size_t)len : 0;
}

int eng_render_voice(const EngVoice& V0, const EngRenderOpts& o, float* out, size_t cap, size_t* nOut) {
  const double sr = o.sr;
  EngVoice V = V0; applyHold(V, o);
  for (int i = 0; i < V.nLayers; i++) { const EngLayer& L = V.layers[i]; if (L.on && L.level > 0 && !eng_type_supported(L.type)) return kEngUnsupported; }
  const size_t bound = eng_render_bound(V0, o);
  if (bound == 0 || cap < bound) return kEngTooSmall;
  const Plan p = planOf(V, o);
  const size_t N = (size_t)p.N;
  const bool gFilt = V.gLpCut < 19900 || V.gLpEnv != 0;
  std::memset(out, 0, N * sizeof(float));
  for (int i = 0; i < V.nLayers; i++) { const EngLayer& L = V.layers[i]; if (L.on && L.level > 0) renderLayer(L, V, o.bpm, sr, out, N); }
  if (gFilt) voiceFilter(V, sr, out, N);
  if (V.gAmpOn) voiceAmp(V, sr, out, N);
  {
    const double g = js_pow(10, V.level / 20), dr = V.drive;
    if (dr > 0.001) { const double k = 1 + dr * dr * 24, nrm = 1 / js_tanh(k); for (size_t i = 0; i < N; i++) out[i] = f32(js_tanh((double)out[i] * k) * nrm * g); }
    else for (size_t i = 0; i < N; i++) out[i] = f32((double)out[i] * g);
  }
  size_t len = N;
  if (anyFxOn(V)) {
    const size_t ext = (size_t)(p.extN > 0 ? std::floor(p.extN) : 0);
    // ext.set(out.subarray(0, Math.min(N, ext.length))): the sound, zeros after it
    if (ext > N) std::memset(out + N, 0, (ext - N) * sizeof(float));
    len = ext;
    for (int i = 0; i < V.nFx; i++) {
      const EngFx& f = V.fx[i]; if (!f.on || !fxReady(f)) continue;   // (applyFx: a type that is not ready returns the sound as it is)
      std::vector<float> r;
      switch (f.kind) {
        case EngFx::Kind::Degrade: r = pad_degrade(PadDegrade{f.bits, f.rate, f.noise, f.color, f.follow, f.dry, f.wet}, out, len, sr); break;
        case EngFx::Kind::Reson: r = pad_reson(PadReson{f.note, f.chord, f.decay, f.bright, f.mode, f.spread, f.mix}, out, len, sr); break;
        case EngFx::Kind::PitchDelay: r = pad_pitchfx(false, PadPitchFx{f.mode, f.time, f.fb, f.pitch, f.pitch2, f.tone, f.size, f.wow, f.mix}, out, len, sr); break;
        case EngFx::Kind::PitchReverb: r = pad_pitchfx(true, PadPitchFx{f.mode, f.time, f.fb, f.pitch, f.pitch2, f.tone, f.size, f.wow, f.mix}, out, len, sr); break;
        default: break;
      }
      if (r.empty()) return kEngFxThrew;   // (a JS exception: RangeError in new Float32Array / set: renderVoice throws)
      if (r.size() > cap) return kEngTooSmall;
      std::memcpy(out, r.data(), r.size() * sizeof(float)); len = r.size();
    }
  }
  size_t end = len; while (end > 1 && std::fabs((double)out[end - 1]) < 1e-4) end--;
  const size_t rl = end > 1 ? end : 1;
  const double flr = js_round(sr * 0.002); const size_t fl = (double)rl < flr ? rl : (size_t)flr;
  for (size_t i = 0; i < fl; i++) out[rl - 1 - i] = f32((double)out[rl - 1 - i] * ((double)i / (double)fl));
  *nOut = rl;
  return kEngOk;
}

}  // namespace thunder
