// See clkcores.h. Each function keeps the JS's statements and arithmetic order (index.html: clkSpb, FlexCore, StepGateCore, RetrigCore).
#include "fx/clkcores.h"

#include <cmath>
#include <cstring>
#include <limits>

#include "rt/jsmath.h"

namespace thunder {
namespace {

const double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kPi = 3.141592653589793;   // Math.PI

// Math.max / Math.min: NaN if either is NaN, +0 over -0 for max and -0 over +0 for min
inline double js_max(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? b : a; return a > b ? a : b; }
inline double js_min(double a, double b) { if (a != a || b != b) return kNaN; if (a == 0 && b == 0) return std::signbit(a) ? a : b; return a < b ? a : b; }
inline double js_min3(double a, double b, double c) { return js_min(js_min(a, b), c); }
inline bool truthy(double x) { return x != 0 && x == x; }    // a Number in `if (x)`
inline int32_t i32(double x) { return js_to_int32(x); }      // `x | 0`
inline double mod(double a, double b) { return std::fmod(a, b); }   // JS `%` on Numbers
// A Float32Array read: an index outside the array is undefined in the JS, and every sum with it NaN.
inline double rdf(const float* b, int N, double i) { return i >= 0 && i < N ? (double)b[(int)i] : kNaN; }

#include "fx/flexspec.inc"

// The pattern table (FlexCore.SPEC()): made once, on the first Flex made (the control thread), never changed.
const FlexPat* flexTable() {
  static FlexPat* T = [] { FlexPat* t = new FlexPat[512]; flexSpecFill(t); return t; }();
  return T;
}

// FlexCore.K
const double kTup[5] = {0, 0, 1, 2, 3};
const double kSte[7] = {1.0 / 2, 1.0 / 3, 1.0 / 4, 1.0 / 6, 1.0 / 8, 1.0 / 12, 1.0 / 16};
const double kCp[4] = {0.5, 0.75, 1.5, 2};
const double kP13[16] = {0, 1, 2, 3, 4, 5, 2, 7, 8, 9, 10, 11, 8, 13, 6, 15};
enum { TX_crush = 0, TX_lp, TX_hp, TX_chop };
const int kTx4[4] = {TX_crush, TX_lp, TX_hp, TX_chop}, kTx3[3] = {TX_crush, TX_lp, TX_hp};
const int kMrr[3] = {W_mute, W_rep, W_rat}, kSkp[4] = {W_stuck, W_jump, W_drop, 0 /* 'prev' */};

inline double cs(double p, double C) { return std::floor(p / C + 1e-9) * C; }      // FlexCore.cs
inline double lr(const double* v, double t) { return v[0] + (v[1] - v[0]) * t; }   // FlexCore.lr
inline bool eon(const FlexPat& o, double N, double j) { return mod(mod(j + o.rot, N) * o.k, N) < o.k; }   // FlexCore.eon

// FlexCore.hh: a repeatable random number in [0, 1) from three integers.
// Math.imul((a | 0) * 73856093 ^ (b | 0) * 19349663 ^ (c | 0) * 83492791, 2654435761) >>> 0, then two more xor-shift / imul rounds: the products are
// exact doubles (the arguments are small integers) that `^` brings to int32, as i32() does.
double hh(double a, double b, double c) {
  const uint32_t x = (uint32_t)i32((double)i32(a) * 73856093) ^ (uint32_t)i32((double)i32(b) * 19349663) ^ (uint32_t)i32((double)i32(c) * 83492791);
  uint32_t u = x * 2654435761u;
  u ^= u >> 15; u = u * 2246822519u; u = u ^ (u >> 13);
  return (double)u / 4294967296.0;
}

}  // namespace

// ---- clkSpb ----------------------------------------------------------------------------------------------------------------------------
double clk_spb(ClkSpb& o, const float* bp, int bpn, int n) {
  if (!bp || bpn < 2 || n < 2) return o.spb;
  const double a = bp[0], b = bp[n - 1];
  if (a < 0 || b < 0) { o.cn = 0; return o.spb; }
  if (!(o.cn > 0)) { o.cu = 0; o.cl = a; }
  if (a < o.cl - 8) o.cu += 16;                                          // the clock wrapped (every 16 beats) since the last block...
  const double A = a + o.cu; if (b < a - 8) o.cu += 16;                  // ...or inside this one
  const double B = b + o.cu, s = (B - A) / (n - 1); o.cl = b;
  if (!(s > 1e-7 && s < 0.01)) { o.cn = 0; o.ps = 0; return o.spb; }
  if (!o.sm) { o.spb = 1 / s; o.sm = true; o.cn = 0; }
  else if (std::fabs(s * o.spb - 1) > 0.02) {
    if (truthy(o.ps) && std::fabs(s / o.ps - 1) < 0.005) { o.spb = 1 / s; o.cn = 0; } else { o.ps = s; o.cn = 0; return o.spb; }
  }
  o.ps = 0;
  if (!(o.cn > 0) || o.cn > 32768) { o.cn = 0; o.ca = A; }
  const double len = o.cn + n - 1; if (len >= 4096) o.spb = len / (B - o.ca);
  o.cn += n; return o.spb;
}

// ---- Flex ------------------------------------------------------------------------------------------------------------------------------
FlexCore::FlexCore(double sr) : sr_(sr), N_((int)std::ceil(sr * 13)), c_(sr), sp_(sr / 2), T_(flexTable()) {
  // the JS makes the two rings at the first block (Float32Array: zeros); here at once, in the constructor, so that process() allocates nothing
  bL_.reset(new float[(size_t)N_]()); bR_.reset(new float[(size_t)N_]());
}

// 17-255 (and FLY 257-511): sets pg_ (gain), plp_ / php_ (filter, 0-1; 1 / 0 = open), pcr_ / pbt_ (hold, bits; 1 / 0 = clean); returns the read position
double FlexCore::ext(int k, double p, double bar) {
  const FlexPat* op = k >= 0 && k < 512 && T_[k].f ? &T_[k] : nullptr;   // (this.T || SPEC())[k]: undefined outside the table
  double r = p, g = 1, lp = 1, hp = 0, cr = 1, bt = 0;
  if (op) {
    const FlexPat& o = *op;
    switch (o.f) {
      case PK_roll: if (p >= o.s) r = o.s + mod(p - o.s, o.L); break;
      case PK_stut: { const double c = cs(p, o.C); r = c + mod(p - c, o.L); } break;
      case PK_build: { const double* s = nullptr; for (int j = 0; j < o.nsg; j++) if (p >= o.sg[j][0]) s = o.sg[j]; if (s) r = s[0] + mod(p - s[0], s[1]); } break;
      case PK_rev: { const double C = o.C, c = cs(p, C), i = js_round(c / C), u = p - c;
        const bool on = o.mw == W_all || o.mw == W_half || (o.mw == W_alt && (i32(i) & 1)) || (o.mw == W_last && c >= 3 - 1e-9) || (o.mw == W_rnd && hh(i, k, 5) < 0.5);
        if (on) r = c - (o.mw == W_half ? u / 2 : u); } break;
      case PK_down: if (p >= o.a) { const double c = o.a + std::floor((p - o.a) / o.C + 1e-9) * o.C; r = c + (p - c) * o.s; } break;
      case PK_up: { const double C = o.C, c = cs(p, C), i = js_round(c / C);
        if (!o.mw || (o.mw == W_last && c >= 3 - 1e-9) || (o.mw == W_alt && (i32(i) & 1))) r = c - C * (o.s - 1) + (p - c) * o.s; } break;
      case PK_stop: { const double a = truthy(o.C) ? cs(p, o.C) : o.a; const double W = truthy(o.C) ? o.C : o.b - o.a;   // (o.C || o.b - o.a)
        if (p >= a) { const double u = js_min(p - a, W); r = a + u - u * u / (2 * W); if (u > W * 0.85) g = js_max(0, (W - u) / (W * 0.15)); } } break;
      case PK_start: { const double a = truthy(o.C) ? cs(p, o.C) : o.a; const double W = truthy(o.C) ? o.C : o.b - o.a;
        // FLX CHECK 2 (5 Nov, owner): speed 4t - 3t^2 (0 -> peak 1.33x -> 1x): it starts from the spin-up's own downbeat, never reads ahead (r <= p) and lands on the grid.
        if (p >= a && p < a + W) { const double u = p - a, t = u / W; r = a + W * t * t * (2 - t); g = js_min(1, u / (W * 0.2)); } } break;
      case PK_wow: r = p - o.d * (1 - js_cos(2 * kPi * p / o.T)) / 2 - (truthy(o.d2) ? o.d2 * (1 - js_cos(2 * kPi * p / o.T2)) / 2 : 0); break;
      case PK_scr: { const double C = o.C, c = cs(p, C), u = (p - c) / C; r = c - C * o.d + C * o.d * (1 - js_cos(2 * kPi * o.k * u)) / 2;
        if (o.gt == 1) g = js_sin(2 * kPi * o.k * u) >= 0 ? 1 : 0; else if (o.gt == 2) g = mod(p * 16, 1) < 0.5 ? 1 : 0; } break;
      case PK_gate: g = mod(p / o.g, 2) < 1 ? 1 : 0; break;
      case PK_pump: { const double u = mod(p, o.C) / o.C; g = js_min(1, u * 3); } break;
      case PK_swell: g = mod(p, o.C) / o.C; break;
      case PK_fade: g = 1 - p / 4; break;
      case PK_mask: g = (double)((i32(o.mn) >> ((15 - (int)js_min(15, std::floor(p * 4))) & 31)) & 1); break;
      case PK_filt: { const double t = truthy(o.a) ? js_max(0, (p - o.a) / (4 - o.a)) : p / 4;
        if (o.has_lp) lp = lr(o.lp, t);
        if (o.has_hp) hp = lr(o.hp, t);
        if (o.has_lpw) lp = o.lpw[1] + o.lpw[2] * js_cos(2 * kPi * p * o.lpw[0]);
        if (o.has_hpw) hp = js_max(0, o.hpw[1] - o.hpw[2] * js_cos(2 * kPi * p));
        if (truthy(o.bw)) { hp = 0.05 + 0.7 * t; lp = hp + 0.25; }
        if (truthy(o.lpb)) lp = 1 - 0.8 * mod(p, 1); } break;
      case PK_crush: { bool on = true;
        if (o.has_beats) { const double f = std::floor(p); on = f == o.beats[0] || f == o.beats[1]; }
        if (truthy(o.alt)) on = (i32(std::floor(p / o.alt)) & 1) != 0;
        if (truthy(o.a)) on = p >= o.a;
        if (on) { cr = o.has_hs ? o.hs[0] + (o.hs[1] - o.hs[0]) * p / 4 : (truthy(o.h) ? o.h : 1); bt = truthy(o.b) ? o.b : 0; if (truthy(o.lps)) lp = o.lps; } } break;
      case PK_echo: { const double c = cs(p, o.C), n = std::floor((p - c) / o.L + 1e-9); r = c + (p - c) - n * o.L; g = js_pow(o.k, n); } break;
      case PK_jug: { const double G = o.G, per = js_round(4 / G), s = js_min(per - 1, std::floor(p / G + 1e-9)), u = p - s * G;
        if (hh(o.seed, s, 11) < o.c) { const double src = s - 1 - std::floor(hh(o.seed, s, 23) * (per - 1)); const bool rv = hh(o.seed, s, 31) < o.rv;
          r = src * G + (rv ? G - u : u); } } break;
      case PK_swing: { const double C = o.C, c = cs(p, C), u = (p - c) / C, h = 0.5 + o.a;
        r = c + C * (u < h ? u * 0.5 / h : 0.5 + (u - h) * 0.5 / (1 - h)); } break;
      case PK_tup: { const double C = o.C, c = cs(p, C), u = (p - c) / C, j = js_min(o.q - 1, std::floor(u * o.q + 1e-9)), sj = o.q == 5 ? kTup[(int)j] : j;   // FLX CHECK 2: the downbeat doubles and 2-4 land late, as a quintuplet does
        r = c + C * (sj / 4 + (u - j / o.q)); } break;
      case PK_drunk: { const double s = std::floor(p * 4); r = p - hh((double)(i32(bar) & 3), s, 41) * 0.06; } break;
      case PK_lag: r = mod(p, 1) >= 0.5 ? p - 1.0 / 16 : p; break;
      case PK_chaos: { const double G = o.G, s = std::floor(p / G + 1e-9), c = s * G, u = p - c, key = truthy(o.nb) ? (i32(bar) & 3) + 1 : 0;
        if (hh(o.seed, s, key * 7 + 3) < o.c) { const double opn = std::floor(hh(o.seed, s, key * 7 + 4) * 7);
          if (opn == 0) r = p - G; else if (opn == 1) r = c - u; else if (opn == 2) r = c - G + u * 2; else if (opn == 3) r = c + u / 2;
          else if (opn == 4) r = c + mod(u, G / 4); else if (opn == 5) g = u < G / 2 ? 1 : 0; else r = c + u - u * u / (2 * G); } } break;
      // ===== FLY (5 Nov): k 257-511. Each reads only what has played (r <= p).
      case PK_proll: if (p >= o.a) { const double n = std::floor((p - o.a) / o.L + 1e-9), u = p - o.a - n * o.L;   // repeat n: speed <= n + 1 keeps it behind now
          if (n > 0) r = o.a + u * js_min(n + 1, js_max(0.25, js_pow(2, n * o.s / 12))); } break;
      case PK_euc: { const double N = o.N, step = 4 / N, s = js_min(N - 1, std::floor(p / step + 1e-9));
        if (!truthy(o.h)) g = eon(o, N, s) ? 1 : 0;
        else if (!eon(o, N, s)) { double j = s - 1; while (j >= 0 && !eon(o, N, j)) j--; if (j >= 0) { const double c0 = j * step; r = c0 + mod(p - c0, step); } } } break;
      case PK_prob: { const double G = o.G, s = std::floor(p / G + 1e-9), c0 = s * G, u = p - c0, B = (double)(i32(bar) & 255);
        if (hh(k, s, B * 13 + 1) < o.c) { int opn = o.op; if (opn == W_any) opn = kMrr[(int)std::floor(hh(k, s, B * 13 + 2) * 3)];
          if (opn == W_mute) g = 0; else if (opn == W_rep) r = p - G; else r = c0 + mod(u, G / (truthy(o.x) ? o.x : 2 + std::floor(hh(k, s, B * 13 + 3) * 3))); } } break;
      case PK_spitch: { const double G = o.G, s = std::floor(p / G + 1e-9), c0 = s * G, u = p - c0, B = truthy(o.nb) ? (double)(i32(bar) & 255) : 0;
        if (hh(k, s, B * 17 + 1) < o.c) { const double sp = o.S[(int)std::floor(hh(k, s, B * 17 + 2) * o.nS)]; r = sp > 1 ? c0 - G * (sp - 1) + u * sp : c0 + u * sp; } } break;
      case PK_frz: { double a, b; if (truthy(o.C)) { const double c = cs(p, o.C); a = c + o.h * o.C; b = c + o.C; } else { a = o.a; b = o.b; }
        if (p >= a && p < b && (o.pr >= 1 || hh(k, js_round(a * 64), (double)(i32(bar) & 255)) < o.pr)) { const double u = p - a, w = truthy(o.sw) ? o.w * (1 - o.sw * u / (b - a)) : o.w;
          r = a + std::floor(u / w + 1e-9) * w * o.rho + mod(u, w); } } break;
      case PK_skip: { const double G = o.G, s = std::floor(p / G + 1e-9), c0 = s * G, u = p - c0, B = truthy(o.nb) ? (double)(i32(bar) & 255) : 0;
        if (hh(k, s, B * 19 + 1) < o.d) { const int opn = o.only ? o.only : kSkp[(int)std::floor(hh(k, s, B * 19 + 2) * 4)];
          if (opn == W_stuck) r = c0 + mod(u, G / 8); else if (opn == W_jump) r = p - G * (1 + std::floor(hh(k, s, B * 19 + 3) * 3)) / 2;
          else if (opn == W_drop) g = u < G * 0.35 ? 0 : 1; else r = c0 - G / 4 + mod(u, G / 4); } } break;
      case PK_bnc: { const double a = truthy(o.C) ? cs(p, o.C) : o.a, b = truthy(o.C) ? a + o.C : 4;
        if (p >= a && p < b) { const double x = p - a, q = o.q, Lm = 1.0 / 128; double n, c;   // repeat n lasts L0 * q^n (q < 1 faster, > 1 slower)
          if (q > 1) { n = std::floor(js_log(1 + x * (q - 1) / o.L0) / js_log(q) + 1e-9); c = o.L0 * (js_pow(q, n) - 1) / (q - 1); }
          else { const double nm = js_max(0, std::ceil(js_log(Lm / o.L0) / js_log(q))), Sm = o.L0 * (1 - js_pow(q, nm)) / (1 - q);
            if (x < Sm) { n = std::floor(js_log(1 - x * (1 - q) / o.L0) / js_log(q) + 1e-9); c = o.L0 * (1 - js_pow(q, n)) / (1 - q); }
            else { const double m = std::floor((x - Sm) / Lm + 1e-9); n = nm + m; c = Sm + m * Lm; if (o.dk < 1) g = 0; } }   // a bounce stops; a roll buzzes on
          if (c > x) c = x;
          r = a + x - c; if (o.dk < 1 && truthy(g)) g = js_pow(o.dk, n); if (truthy(o.sw)) g = js_min(1, 0.25 * js_pow(1.2, n)); } } break;
      case PK_mut: { const double G = o.G, per = js_round(4 / G), s = js_min(per - 1, std::floor(p / G + 1e-9)), u = p - s * G; double key = 0;
        if (truthy(o.cyc)) { const double B = mod(bar, o.cyc); if (truthy(B) && hh(k, s, 97 + B) < o.mn) key = 100 + B; }
        else for (double B = (double)(i32(bar) & 3); B > 0; B--) if (hh(k, s, 97 + B) < o.mn) { key = 100 + B; break; }   // the changes add up over the four bars
        if (hh(k, s, key * 7 + 11) < o.c) { const double src = s - 1 - std::floor(hh(k, s, key * 7 + 23) * (per - 1)); const bool rv = hh(k, s, key * 7 + 31) < o.rv;
          r = src * G + (rv ? G - u : u); } } break;
      case PK_sted: { const double G = o.G, s = std::floor(p / G + 1e-9), c0 = s * G, u = p - c0, B = truthy(o.nb) ? (double)(i32(bar) & 255) : 0;
        if (hh(k, s, B * 23 + 1) < o.d) { const double Lr = G * kSte[(int)std::floor(hh(k, s, B * 23 + 2) * 7)], n = std::floor(u / Lr + 1e-9), v = u - n * Lr;
          r = c0 + v * (truthy(o.s) && n > 0 ? js_min(n + 1, js_max(0.25, js_pow(2, n * o.s / 12))) : 1);
          if (truthy(o.fd)) g = js_pow(o.fd, n);
          if (truthy(o.gt)) g = v < Lr / 2 ? 1 : 0; } } break;
      case PK_poly: if (p >= o.a) { const double x = p - o.a, n = std::floor(x / o.M + 1e-9), u = x - n * o.M; r = o.a + (truthy(o.pp) && (i32(n) & 1) ? o.M - u : u); } break;
      case PK_warp: { const double G = o.G, s = std::floor(p / G + 1e-9), c0 = s * G, u = p - c0, K = o.k;
        const bool up = o.dw == W_UP ? true : o.dw == W_DOWN ? false : o.dw == W_ZIGZAG ? !(i32(s) & 1) : hh(k, s, (double)(i32(bar) & 255) * 29 + 1) < 0.5;
        r = up ? c0 + (1 - K) * u + K * u * u / G : c0 - K * G / 4 + (1 + K) * u - K * u * u / G; } break;   // up: slow -> fast, lands on the grid; down: fast -> slow
      case PK_rroll: if (o.mw == W_rew) { const double a = truthy(o.C) ? cs(p, o.C) + o.h * o.C : o.a, b = truthy(o.C) ? cs(p, o.C) + o.C : 4;
            if (p >= a && p < b) { r = a - o.k * (p - a); g = js_min(1, (b - p) / ((b - a) * 0.1)); } }
          else if (p >= o.a) { const double L = o.L, n = std::floor((p - o.a) / L + 1e-9), u = p - o.a - n * L;
            if (n > 0) { const double sp = truthy(o.s) ? js_max(0.25, js_pow(2, n * o.s / 12)) : 1; r = o.mw == W_rev || (i32(n) & 1) ? o.a + L - u * sp : o.a + u;
              if (truthy(o.sw)) g = js_min(1, 0.2 + 0.8 * (p - o.a) / (4 - o.a)); } } break;
      case PK_grain: if (p >= o.a) { const double w = o.w, j = std::floor(p / w + 1e-9), u = p - j * w, B = (double)(i32(bar) & 255), off = w + hh(k, j, B * 31 + 1) * (o.W - w);
          r = hh(k, j, B * 31 + 2) < o.rv ? p - off + w - 2 * u : p - off; if (truthy(o.gap) && hh(k, j, B * 31 + 3) < o.gap) g = 0; } break;
      case PK_tex: { const double G = o.G, s = std::floor(p / G + 1e-9), u = p - s * G, B = truthy(o.nb) ? (double)(i32(bar) & 255) : 0;
        if (hh(k, s, B * 37 + 1) < o.d) { const double h2 = hh(k, s, B * 37 + 2), h3 = hh(k, s, B * 37 + 3);
          const int opn = o.set == W_all ? kTx4[(int)std::floor(h2 * 4)] : o.set == W_cf ? kTx3[(int)std::floor(h2 * 3)] : o.set == W_filt ? (h2 < 0.5 ? TX_lp : TX_hp) : o.set == W_crush ? TX_crush : TX_chop;
          if (opn == TX_crush) { cr = 3 + std::floor(h3 * 22); bt = h3 < 0.5 ? 4 + std::floor(h3 * 8) : 0; } else if (opn == TX_lp) lp = 0.2 + h3 * 0.35; else if (opn == TX_hp) hp = 0.5 + h3 * 0.3;
          else g = u < G * (0.25 + h3 * 0.4) ? 1 : 0; } } break;
      case PK_chaos2: { const double G = o.G, s = std::floor(p / G + 1e-9), c0 = s * G, u = p - c0, B = truthy(o.nb) ? (truthy(o.slow) ? (double)((i32(bar) >> 1) & 1) : (double)(i32(bar) & 255)) : 0;
        if (hh(k, s, B * 41 + 1) < o.d) { const double opn = std::floor(hh(k, s, B * 41 + 2) * 8), h3 = hh(k, s, B * 41 + 3);
          if (opn == 0) { const double sp = kCp[(int)std::floor(h3 * 4)]; r = sp > 1 ? c0 - G * (sp - 1) + u * sp : c0 + u * sp; }   // pitch
          else if (opn == 1) r = c0 + mod(u, G / 8);            // freeze
          else if (opn == 2) r = p - G / 2;                     // skip back
          else if (opn == 3) r = c0 - u;                        // the slice before, backwards
          else if (opn == 4) r = c0 + 0.5 * u + 0.5 * u * u / G;   // warp up
          else if (opn == 5) g = u < G / 2 ? 1 : 0;             // gate
          else if (opn == 6) { cr = 6 + std::floor(h3 * 16); bt = 5; }   // crush
          else r = c0 + mod(u, G / 3); } } break;               // ratchet
      default: break;
    }
  }
  pg_ = g; plp_ = lp; php_ = hp; pcr_ = cr; pbt_ = bt; return r;
}

double FlexCore::pat(int k, double p, double bar) {
  double r = p, g = 1, u;
  switch (k) {
    case 1: r = mod(p, 1); break;
    case 2: r = std::floor(p) + mod(p, 0.5); break;
    case 3: if (p >= 3) r = 3 + mod(p - 3, 0.25); break;
    case 4: if (p >= 3.5) r = 3.5 + mod(p - 3.5, 0.125); else if (p >= 3) r = 3 + mod(p - 3, 0.25); else if (p >= 2) r = 2 + mod(p - 2, 0.5); break;
    case 5: r = p / 2; break;
    case 6: r = p - p * p / 8; if (p > 3.5) g = (4 - p) * 2; break;
    case 7: r = std::floor(p) - mod(p, 1); break;
    case 8: r = std::floor(p * 2) / 2 - mod(p, 0.5); break;
    case 9: if (p >= 3) { u = p - 3; r = 3 - 2.5 * u * u; g = 1 - u * u; } break;
    // FLX CHECK 2 (5 Nov): depth 0.105 is the most that stays behind the playhead (0.1098), so the push and pull are smooth.
    case 10: { const double b = std::floor(p * 2) / 2; u = (p - b) * 2; r = b + 0.105 * (1 - js_cos(2 * kPi * u)); } break;
    case 11: g = mod(p, 0.25) < 0.125 ? 1 : 0; break;
    case 12: g = (double)((0xB6B6 >> ((15 - (int)std::floor(p * 4)) & 31)) & 1); break;
    case 13: { const double s = std::floor(p * 4); r = kP13[(int)s] * 0.25 + mod(p, 0.25); } break;
    case 14: if (p >= 2) r = p - 2; break;
    case 15: r = std::floor(p) - 1 + 2 * mod(p, 1); break;
    case 16: { const double s = std::floor(p * 4);
      uint32_t h = (uint32_t)((int32_t)((i32(bar) & 3) * 31 + (int32_t)(s * 17) + 7)) * 2654435761u;   // Math.imul(.., 2654435761) >>> 0
      // `h ^= h >>> 13` leaves h a SIGNED int32 in the JS, and `h % 11` then keeps its sign: a negative h gives a negative remainder, which is < 4, so the offset is 0
      const int32_t hs = (int32_t)(h ^ (h >> 13)); const int32_t hm = hs % 11;
      r = (s - (hm < 4 ? 0 : hm - 3)) * 0.25 + mod(p, 0.25); } break;
    default: break;
  }
  pg_ = g; return r;
}

// 4-point Hermite read (half speed, tape stops and scratches stay clean on cymbals); linear within 2 samples of now, where the Hermite's far point would still be unwritten.
double FlexCore::rd(const float* b, int w, double D) const {
  const int N = N_; double x = w - D; if (x < 0) x += N;
  const double i = std::floor(x), f = x - i, j = i + 1 >= N ? 0 : i + 1;
  if (D < 2) return rdf(b, N, i) + (rdf(b, N, j) - rdf(b, N, i)) * f;
  const double y0 = rdf(b, N, truthy(i) ? i - 1 : N - 1), y1 = rdf(b, N, i), y2 = rdf(b, N, j), y3 = rdf(b, N, j + 1 >= N ? 0 : j + 1);
  return ((((y3 - y0) * 0.5 + (y1 - y2) * 1.5) * f + (y0 - 2.5 * y1 + 2 * y2 - 0.5 * y3)) * f + (y2 - y0) * 0.5) * f + y1;
}

// the Flex filter's coefficients for cutoff f, and one sample through a filter stage
void FlexCore::svc(double* o, double f, double nyq) const { const double gg = js_tan(kPi * js_min(nyq, f) / sr_), kk = 1.1, a1 = 1 / (1 + gg * (gg + kk)); o[0] = a1; o[1] = gg * a1; o[2] = gg * gg * a1; }
double FlexCore::svf(double* fs, double x, int j, const double* c, bool hi) {
  const double v3 = x - fs[j + 1], v1 = c[0] * fs[j] + c[1] * v3, v2 = fs[j + 1] + c[1] * fs[j] + c[2] * v3; fs[j] = 2 * v1 - fs[j]; fs[j + 1] = 2 * v2 - fs[j + 1]; return hi ? x - 1.1 * v1 - v2 : v2;
}

void FlexCore::run(float* L, float* R, int n, const FlexDp* dp, const ClkTr& tr) {
  const int N = N_; float* bL = bL_.get(); float* bR = bR_.get();
  const float* bp = tr.bp; const double spb = clk_spb(c_, bp, tr.bpn, n); const bool live = bp && tr.bpn > 1;
  // WAITS FOR ITS COMMAND (owner, 3 Nov): STARTS ON FLX (dp.wait) plays nothing until an FLX sets a pattern or an FXO switches the slot on (tr.sw).
  const int k = !dp ? 0 : tr.flx >= 0 ? (int)js_min(511, js_round(tr.flx)) : truthy(dp->wait) && !(tr.sw > 0.5) ? 0 : i32(dp->pat);
  const double mix = dp ? dp->mix : 0, fsk = 1 - js_exp(-1 / (sr_ * 0.015)), nyq = sr_ * 0.45;
  double* fs = fs_;
  const double XF = js_max(16, js_round(sr_ * (dp ? dp->xf : 5) / 1000)), gk = 1 - js_exp(-1 / (sr_ * 0.002));
  // Lock our position to the clock: gently while close (the clock's own float32 steps stay out), at once when it jumps.
  const double q0 = live ? (double)bp[0] : -1;
  if (q0 < 0) ph_ = -1;
  else if (ph_ < 0) ph_ = q0;
  else { double e = q0 - ph_; e -= 16 * js_round(e / 16); if (std::fabs(e) > 0.004) ph_ = q0; else ph_ += e * 0.1; }
  if (std::fabs(spb / sp_ - 1) > 0.02) sp_ = spb;               // a real tempo change: follow at once (that jump crossfades)
  const double sk = 1 - js_exp(-1 / (sr_ * 0.1));
  for (int i = 0; i < n; i++) {
    const int w = w_; bL[w] = L[i]; bR[w] = R[i];
    sp_ += (spb - sp_) * sk;
    const double q = ph_; if (q >= 0) { ph_ += 1 / sp_; if (ph_ >= 16) ph_ -= 16; }
    if (!dp) { D_ = 0; w_ = w + 1 >= N ? 0 : w + 1; continue; }
    double D = 0, g = 1, lp = 1, hp = 0, cr = 1, bt = 0;
    if (k > 0 && q >= 0) { const double p = mod(q, 4), bar = std::floor(q / 4), r = k > 16 ? ext(k, p, bar) : pat(k, p, bar); g = pg_;
      if (k > 16) { lp = plp_; hp = php_; cr = pcr_; bt = pbt_; }
      D = js_max(0, js_min(N - 4, (p - r) * sp_)); }
    if (std::fabs(D - D_) > 8) { xo_ = D_; xc_ = XF; xn_ = XF; }
    D_ = D; g_ += (g - g_) * gk;
    double yl = rd(bL, w, D), yr = rd(bR, w, D);
    if (xc_ > 0) { const double f = xc_ / xn_; yl += (rd(bL, w, xo_) - yl) * f; yr += (rd(bR, w, xo_) - yr) * f; xc_--; }
    // FLEX 256: CRUSH (hold every cr samples, bt bits) and FILTER (12 dB low- and high-pass state-variable filters)
    if (cr > 1.01) { if (++hc_ >= cr) { hc_ -= cr; hl_ = yl; hr_ = yr; } yl = hl_; yr = hr_; } else { hc_ = 0; hl_ = yl; hr_ = yr; }
    if (bt > 0) { const double qz = js_pow(2, bt - 1); yl = js_round(yl * qz) / qz; yr = js_round(yr * qz) / qz; }
    slp_ += (lp - slp_) * fsk; shp_ += (hp - shp_) * fsk;
    if (slp_ < 0.995 || shp_ > 0.005) {
      if (!(fk_++ & 7)) { svc(fa_, 40 * js_pow(2, slp_ * 9), nyq); svc(fb_, 20 * js_pow(2, shp_ * 9), nyq); }
      if (slp_ < 0.995) { yl = svf(fs, yl, 0, fa_, false); yr = svf(fs, yr, 2, fa_, false); }
      if (shp_ > 0.005) { yl = svf(fs, yl, 4, fb_, true); yr = svf(fs, yr, 6, fb_, true); }
    } else if (truthy(fs[0]) || truthy(fs[1]) || truthy(fs[4])) for (int j = 0; j < 8; j++) fs[j] = 0;
    L[i] = (float)((double)L[i] + (yl * g_ - (double)L[i]) * mix); R[i] = (float)((double)R[i] + (yr * g_ - (double)R[i]) * mix);
    w_ = w + 1 >= N ? 0 : w + 1;
  }
}

// ---- Step gate -------------------------------------------------------------------------------------------------------------------------
namespace {
// StepGateCore.PATS (the built-in 16-step patterns, 'x' = open), as the masks StepGateCore.mask makes of them (step 1 = the top bit)
const char* const kSgPats[16] = {"", "xxxxxxxxxxxxxxxx", "x.x.x.x.x.x.x.x.", ".x.x.x.x.x.x.x.x", "x..x..x.x..x..x.", "xx.xx.x.xx.xx.x.", "x.xxx.x.x.xxx.xx", "xxx.xxx.xxx.x.x.",
  "x...x...x...x...", "..x...x...x...x.", "x.x..x.x.x..x.x.", "xxxx....xxxx....", "xxxxxxxx........", "x.xx.x.xx.xx.x.x", "xx..xx..xx..x.x.", "x..x.x..x..x.xx."};
int sgMask(const char* s) { int m = 0; for (int j = 0; j < 16; j++) if (s[j] == 'x') m |= 1 << (15 - j); return m; }
const double kSgSb[5] = {0.5, 0.25, 0.125, 1.0 / 6, 1.0 / 3};   // StepGateCore.SB
}  // namespace

void StepGateCore::process(float* L, float* R, int n, const SgDp& dp, const ClkTr& tr) {
  // tr.gat: -1 the slot's own (PATTERN, or waiting with STARTS ON GAT), 0 GAT 00 (off: the gate opens), 1-15 a pattern, 16 CUSTOM
  const double g = tr.gat >= 0 ? js_round(tr.gat) : -1; const float* bp = tr.bp;
  const bool wait = g == 0 || (g < 0 && truthy(dp.wait) && !(tr.sw > 0.5)), live = !wait && bp && tr.bpn > 1;
  const int pi = g > 0 ? (g >= 16 ? 0 : (int)g) : i32(dp.pat);
  // (the JS throws on a pattern past 15: normTfx keeps 0..15 and GAT 16.. is CUSTOM, so it does not arise; here such a pattern is an empty one)
  const int mask = pi == 0 ? ((i32(dp.m1) & 255) << 8) | (i32(dp.m2) & 255) : pi > 0 && pi < 16 ? sgMask(kSgPats[pi]) : 0;
  const int di = i32(dp.div);
  const double sb = di >= 0 && di < 5 ? kSgSb[di] : 0.25, len = dp.len, lo = 1 - dp.depth, tc = 1 - js_exp(-1 / (sr_ * js_max(0.0003, dp.smooth / 1000)));
  for (int i = 0; i < n; i++) {
    const double q = bp ? (live ? (double)bp[i] : (double)bp[0]) : -1; double tg = 1;
    if (live && q >= 0) { const double sp = q / sb, st = std::floor(sp + 1e-6), fr = sp - st, idx = mod(mod(st, 16) + 16, 16); tg = ((mask >> ((15 - (int)idx) & 31)) & 1) && fr < len ? 1 : lo; }
    g_ += (tg - g_) * tc; L[i] = (float)((double)L[i] * g_); R[i] = (float)((double)R[i] * g_);
  }
}

// ---- Retrigger -------------------------------------------------------------------------------------------------------------------------
RetrigCore::RetrigCore(double sr) : sr_(sr), N_((int)std::ceil(sr * 3)), c_(sr) { bL_.reset(new float[(size_t)N_]()); bR_.reset(new float[(size_t)N_]()); }

void RetrigCore::process(float* L, float* R, int n, const RtDp& dp, const ClkTr& tr) {
  static const double kDivs[8] = {1, 0.5, 0.25, 0.125, 0.0625, 1.0 / 3, 1.0 / 6, 1.0 / 12};   // RetrigCore.DIVS
  const int N = N_; float* bL = bL_.get(); float* bR = bR_.get();
  const double spb = clk_spb(c_, tr.bp, tr.bpn, n), o = tr.rtg;
  const bool eng = o > 0.5 ? true : o > -0.5 ? false : i32(dp.mode) == 1;
  const double di = o > 0.5 ? js_min(7, js_round(o) - 1) : js_min(7, (double)i32(dp.div));
  if (eng && (!on_ || di != div_)) { on_ = true; div_ = di; st_ = w_; el_ = 0; Ls_ = js_max(64, js_min(N - 64, js_round((di >= 0 && di < 8 ? kDivs[(int)di] : kNaN) * spb))); }
  if (!eng) on_ = false;
  const double tgt = on_ ? 1 : 0, gk = 1 - js_exp(-1 / (sr_ * 0.003)), fd = js_max(8, js_round(sr_ * 0.0015)), rs = js_pow(2, dp.pitch / 12), dec = 1 - dp.decay;
  const double Ls = Ls_, gl = js_max(0.05, dp.gate) * Ls, mix = dp.mix;
  for (int i = 0; i < n; i++) {
    const bool frozen = g_ > 1e-4 && el_ >= Ls;
    if (!frozen) { bL[w_] = L[i]; bR[w_] = R[i]; w_ = w_ + 1 >= N ? 0 : w_ + 1; }
    g_ += (tgt - g_) * gk;
    if (g_ < 1e-4 && !on_) { g_ = 0; continue; }
    const double rep = std::floor(el_ / Ls); double yl = L[i], yr = R[i];
    if (rep > 0) {
      const double pos = el_ - rep * Ls, rate = js_max(0.25, js_min(4, js_pow(rs, rep))), rp = mod(pos * rate, Ls);
      double x = st_ + rp; if (x >= N) x -= N; const double a = std::floor(x), f = x - a, b = a + 1 >= N ? 0 : a + 1;
      const double env = js_pow(dec, rep) * js_max(0, js_min3(1, pos / fd, (gl - pos) / fd));
      yl = (rdf(bL, N, a) + (rdf(bL, N, b) - rdf(bL, N, a)) * f) * env; yr = (rdf(bR, N, a) + (rdf(bR, N, b) - rdf(bR, N, a)) * f) * env;
    }
    el_++;
    const double m = g_ * mix; L[i] = (float)((double)L[i] + (yl - (double)L[i]) * m); R[i] = (float)((double)R[i] + (yr - (double)R[i]) * m);
  }
}

}  // namespace thunder
