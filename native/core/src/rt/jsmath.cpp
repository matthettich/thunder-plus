// js_exp: fdlibm e_exp.c, js_pow: fdlibm e_pow.c (the algorithms V8's base/ieee754.cc uses for Math.exp and Math.pow).
/*
 * ====================================================
 * Copyright (C) 2004 by Sun Microsystems, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 */
#include "jsmath.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace thunder {

namespace {
inline uint32_t hi_word(double x) { uint64_t u; std::memcpy(&u, &x, 8); return (uint32_t)(u >> 32); }
inline uint32_t lo_word(double x) { uint64_t u; std::memcpy(&u, &x, 8); return (uint32_t)u; }
inline double from_words(uint32_t hi, uint32_t lo) { const uint64_t u = ((uint64_t)hi << 32) | lo; double d; std::memcpy(&d, &u, 8); return d; }

const double one = 1.0, halF[2] = {0.5, -0.5}, huge = 1.0e+300,
  twom1000 = 9.33263618503218878990e-302,     /* 2**-1000 */
  o_threshold = 7.09782712893383973096e+02,   /* 0x40862E42, 0xFEFA39EF */
  u_threshold = -7.45133219101941108420e+02,  /* 0xc0874910, 0xD52D3051 */
  ln2HI[2] = {6.93147180369123816490e-01, -6.93147180369123816490e-01},   /* 0x3fe62e42, 0xfee00000 */
  ln2LO[2] = {1.90821492927058770002e-10, -1.90821492927058770002e-10},   /* 0x3dea39ef, 0x35793c76 */
  invln2 = 1.44269504088896338700e+00,        /* 0x3ff71547, 0x652b82fe */
  P1 = 1.66666666666666019037e-01,            /* 0x3FC55555, 0x5555553E */
  P2 = -2.77777777770155933842e-03,           /* 0xBF66C16C, 0x16BEBD93 */
  P3 = 6.61375632143793436117e-05,            /* 0x3F11566A, 0xAF25DE2C */
  P4 = -1.65339022054652515390e-06,           /* 0xBEBBBD41, 0xC5D26BF1 */
  P5 = 4.13813679705723846039e-08,            /* 0x3E663769, 0x72BEA4D0 */
  two1023 = 8.988465674311579539e307;         /* 0x1p1023 */
}  // namespace

double js_exp(double x) {
  double y, hi = 0.0, lo = 0.0, c, t, twopk;
  int32_t k = 0, xsb;
  uint32_t hx = hi_word(x);
  xsb = (int32_t)((hx >> 31) & 1);   /* sign bit of x */
  hx &= 0x7fffffff;                  /* high word of |x| */

  /* filter out non-finite argument */
  if (hx >= 0x40862E42) {            /* if |x|>=709.78... */
    if (hx >= 0x7ff00000) {
      if (((hx & 0xfffff) | lo_word(x)) != 0) return x + x;   /* NaN */
      return (xsb == 0) ? x : 0.0;                           /* exp(+-inf)={inf,0} */
    }
    if (x > o_threshold) return huge * huge;                 /* overflow */
    if (x < u_threshold) return twom1000 * twom1000;         /* underflow */
  }

  /* argument reduction */
  if (hx > 0x3fd62e42) {             /* if  |x| > 0.5 ln2 */
    if (hx < 0x3FF0A2B2) {           /* and |x| < 1.5 ln2 */
      hi = x - ln2HI[xsb]; lo = ln2LO[xsb]; k = 1 - xsb - xsb;
    } else {
      k = (int32_t)(invln2 * x + halF[xsb]);
      t = k;
      hi = x - t * ln2HI[0];         /* t*ln2HI is exact here */
      lo = t * ln2LO[0];
    }
    x = hi - lo;
  } else if (hx < 0x3e300000) {      /* when |x|<2**-28 */
    if (huge + x > one) return one + x;   /* trigger inexact */
  } else {
    k = 0;
  }

  /* x is now in primary range */
  t = x * x;
  if (k >= -1021) twopk = from_words((uint32_t)(0x3ff00000 + (int32_t)((uint32_t)k << 20)), 0);
  else twopk = from_words((uint32_t)(0x3ff00000 + (int32_t)((uint32_t)(k + 1000) << 20)), 0);
  c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  if (k == 0) return one - ((x * c) / (c - 2.0) - x);
  y = one - ((lo - (x * c) / (2.0 - c)) - hi);
  if (k >= -1021) {
    if (k == 1024) return y * 2.0 * two1023;
    return y * twopk;
  }
  return y * twopk * twom1000;
}


// ---- Math.pow ------------------------------------------------------------------------------------------------------
// fdlibm's e_pow.c (__ieee754_pow), which V8's ieee754::pow is a port of; the notice above covers it. Method (from the file):
//   1. Compute and return log2(x) in two pieces: log2(x) = w1 + w2, where w1 has 53-24 = 29 bit trailing zeros.
//   2. Perform y*log2(x) = n+y' by simulating multi-precision arithmetic, where |y'|<=0.5.
//   3. Return x**y = 2**n*exp(y'*log2)
// The special cases are IEEE's, except (+-1)**(+-inf), which is NaN here as in ECMAScript.
double js_pow(double x, double y) {
  static const double bp[] = {1.0, 1.5}, dp_h[] = {0.0, 5.84962487220764160156e-01}, dp_l[] = {0.0, 1.35003920212974897128e-08},
    zero = 0.0, one = 1.0, two = 2.0, two53 = 9007199254740992.0, huge = 1.0e300, tiny = 1.0e-300,
    L1 = 5.99999999999994648725e-01, L2 = 4.28571428578550184252e-01, L3 = 3.33333329818377432918e-01,
    L4 = 2.72728123808534006489e-01, L5 = 2.30660745775561754067e-01, L6 = 2.06975017800338417784e-01,
    P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
    P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08,
    lg2 = 6.93147180559945286227e-01, lg2_h = 6.93147182464599609375e-01, lg2_l = -1.90465429995776804525e-09,
    ovt = 8.0085662595372944372e-0017,   /* -(1024-log2(ovfl+.5ulp)) */
    cp = 9.61796693925975554329e-01,     /* 2/(3ln2) */
    cp_h = 9.61796700954437255859e-01, cp_l = -7.02846165095275826516e-09,
    ivln2 = 1.44269504088896338700e+00, ivln2_h = 1.44269502162933349609e+00, ivln2_l = 1.92596299112661746302e-08;
  const auto HI = [](double d) { return (int32_t)hi_word(d); };
  const auto LO = [](double d) { uint64_t u; std::memcpy(&u, &d, 8); return (uint32_t)u; };
  const auto setLo0 = [](double d) { uint64_t u; std::memcpy(&u, &d, 8); u &= 0xffffffff00000000ull; std::memcpy(&d, &u, 8); return d; };
  const auto setHi = [](double d, int32_t h) { uint64_t u; std::memcpy(&u, &d, 8); u = (u & 0xffffffffull) | ((uint64_t)(uint32_t)h << 32); std::memcpy(&d, &u, 8); return d; };

  double z, ax, z_h, z_l, p_h, p_l, y1, t1, t2, r, s, t, u, v, w;
  int32_t i, j, k, yisint, n, hx, hy, ix, iy;
  uint32_t lx, ly;
  hx = HI(x); lx = LO(x); hy = HI(y); ly = LO(y);
  ix = hx & 0x7fffffff; iy = hy & 0x7fffffff;

  if ((iy | ly) == 0) return one;   /* y==zero: x**0 = 1 */
  if (ix > 0x7ff00000 || ((ix == 0x7ff00000) && (lx != 0)) || iy > 0x7ff00000 || ((iy == 0x7ff00000) && (ly != 0))) return x + y;   /* +-NaN */

  /* determine if y is an odd int when x < 0: yisint = 0 not an integer, 1 odd, 2 even */
  yisint = 0;
  if (hx < 0) {
    if (iy >= 0x43400000) yisint = 2;
    else if (iy >= 0x3ff00000) {
      k = (iy >> 20) - 0x3ff;
      if (k > 20) { const uint32_t jj = ly >> (52 - k); if ((jj << (52 - k)) == ly) yisint = 2 - (int32_t)(jj & 1); }
      else if (ly == 0) { j = iy >> (20 - k); if ((j << (20 - k)) == iy) yisint = 2 - (j & 1); }
    }
  }

  /* special value of y */
  if (ly == 0) {
    if (iy == 0x7ff00000) {   /* y is +-inf */
      if (((ix - 0x3ff00000) | (int32_t)lx) == 0) return y - y;   /* (+-1)**+-inf is NaN */
      else if (ix >= 0x3ff00000) return (hy >= 0) ? y : zero;      /* (|x|>1)**+-inf = inf,0 */
      else return (hy < 0) ? -y : zero;                            /* (|x|<1)**-,+inf = inf,0 */
    }
    if (iy == 0x3ff00000) { if (hy < 0) return one / x; else return x; }   /* y is +-1 */
    if (hy == 0x40000000) return x * x;                                    /* y is 2 */
    if (hy == 0x3fe00000) { if (hx >= 0) return std::sqrt(x); }            /* y is 0.5, x >= +0 */
  }

  ax = std::fabs(x);
  /* special value of x */
  if (lx == 0) {
    if (ix == 0x7ff00000 || ix == 0 || ix == 0x3ff00000) {
      z = ax;                       /* x is +-0,+-inf,+-1 */
      if (hy < 0) z = one / z;      /* z = (1/|x|) */
      if (hx < 0) {
        if (((ix - 0x3ff00000) | yisint) == 0) z = (z - z) / (z - z);   /* (-1)**non-int is NaN */
        else if (yisint == 1) z = -z;                                   /* (x<0)**odd = -(|x|**odd) */
      }
      return z;
    }
  }

  n = (hx < 0) ? 0 : 1;   /* (fdlibm: (hx>>31)+1 with a signed shift) */

  if ((n | yisint) == 0) return (x - x) / (x - x);   /* (x<0)**(non-int) is NaN */

  s = one;                                  /* s (sign of result -ve**odd) = -1 else = 1 */
  if ((n | (yisint - 1)) == 0) s = -one;    /* (-ve)**(odd int) */

  if (iy > 0x41e00000) {       /* |y| > 2**31 */
    if (iy > 0x43f00000) {     /* |y| > 2**64, must o/uflow */
      if (ix <= 0x3fefffff) return (hy < 0) ? huge * huge : tiny * tiny;
      if (ix >= 0x3ff00000) return (hy > 0) ? huge * huge : tiny * tiny;
    }
    /* over/underflow if x is not close to one */
    if (ix < 0x3fefffff) return (hy < 0) ? s * huge * huge : s * tiny * tiny;
    if (ix > 0x3ff00000) return (hy > 0) ? s * huge * huge : s * tiny * tiny;
    /* now |1-x| is tiny <= 2**-20, suffice to compute log(x) by x-x^2/2+x^3/3-x^4/4 */
    t = ax - one;   /* t has 20 trailing zeros */
    w = (t * t) * (0.5 - t * (0.3333333333333333333333 - t * 0.25));
    u = ivln2_h * t;   /* ivln2_h has 21 sig. bits */
    v = t * ivln2_l - w * ivln2;
    t1 = setLo0(u + v);
    t2 = v - (t1 - u);
  } else {
    double ss, s2, s_h, s_l, t_h, t_l;
    n = 0;
    if (ix < 0x00100000) { ax *= two53; n -= 53; ix = HI(ax); }   /* subnormal */
    n += ((ix) >> 20) - 0x3ff;
    j = ix & 0x000fffff;
    /* determine interval */
    ix = j | 0x3ff00000;              /* normalize ix */
    if (j <= 0x3988E) k = 0;          /* |x|<sqrt(3/2) */
    else if (j < 0xBB67A) k = 1;      /* |x|<sqrt(3)   */
    else { k = 0; n += 1; ix -= 0x00100000; }
    ax = setHi(ax, ix);

    /* compute ss = s_h+s_l = (x-1)/(x+1) or (x-1.5)/(x+1.5) */
    u = ax - bp[k];
    v = one / (ax + bp[k]);
    ss = u * v;
    s_h = setLo0(ss);
    /* t_h=ax+bp[k] High */
    t_h = setHi(zero, ((ix >> 1) | 0x20000000) + 0x00080000 + (k << 18));
    t_l = ax - (t_h - bp[k]);
    s_l = v * ((u - s_h * t_h) - s_h * t_l);
    /* compute log(ax) */
    s2 = ss * ss;
    r = s2 * s2 * (L1 + s2 * (L2 + s2 * (L3 + s2 * (L4 + s2 * (L5 + s2 * L6)))));
    r += s_l * (s_h + ss);
    s2 = s_h * s_h;
    t_h = setLo0(3.0 + s2 + r);
    t_l = r - ((t_h - 3.0) - s2);
    /* u+v = ss*(1+...) */
    u = s_h * t_h;
    v = s_l * t_h + t_l * ss;
    /* 2/(3log2)*(ss+...) */
    p_h = setLo0(u + v);
    p_l = v - (p_h - u);
    z_h = cp_h * p_h;   /* cp_h+cp_l = 2/(3*log2) */
    z_l = cp_l * p_h + p_l * cp + dp_l[k];
    /* log2(ax) = (ss+..)*2/(3*log2) = n + dp_h + z_h + z_l */
    t = (double)n;
    t1 = setLo0(((z_h + z_l) + dp_h[k]) + t);
    t2 = z_l - (((t1 - t) - dp_h[k]) - z_h);
  }

  /* split up y into y1+y2 and compute (y1+y2)*(t1+t2) */
  y1 = setLo0(y);
  p_l = (y - y1) * t1 + y * t2;
  p_h = y1 * t1;
  z = p_l + p_h;
  j = HI(z); i = (int32_t)LO(z);
  if (j >= 0x40900000) {                               /* z >= 1024 */
    if (((j - 0x40900000) | i) != 0) return s * huge * huge;   /* z > 1024: overflow */
    else { if (p_l + ovt > z - p_h) return s * huge * huge; }
  } else if ((j & 0x7fffffff) >= 0x4090cc00) {         /* z <= -1075 */
    if ((((uint32_t)j - 0xc090cc00u) | (uint32_t)i) != 0) return s * tiny * tiny;   /* z < -1075: underflow */
    else { if (p_l <= z - p_h) return s * tiny * tiny; }
  }
  /* compute 2**(p_h+p_l) */
  i = j & 0x7fffffff;
  k = (i >> 20) - 0x3ff;
  n = 0;
  if (i > 0x3fe00000) {   /* if |z| > 0.5, set n = [z+0.5] */
    n = j + (0x00100000 >> (k + 1));
    k = ((n & 0x7fffffff) >> 20) - 0x3ff;   /* new k for n */
    t = setHi(zero, (n & ~(0x000fffff >> k)));
    n = ((n & 0x000fffff) | 0x00100000) >> (20 - k);
    if (j < 0) n = -n;
    p_h -= t;
  }
  t = setLo0(p_l + p_h);
  u = t * lg2_h;
  v = (p_l - (t - p_h)) * lg2 + t * lg2_l;
  z = u + v;
  w = v - (z - u);
  t = z * z;
  t1 = z - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
  r = (z * t1) / ((t1 - two) - (w + z * w));   /* V8's own grouping (base::Divide(z * t1, (t1 - two) - (w + z * w))); fdlibm has (z*t1)/(t1-two)-(w+z*w). Math.pow is what must be matched */
  z = one - (r - z);
  j = HI(z);
  j += (int32_t)((uint32_t)n << 20);
  if ((j >> 20) <= 0) z = std::scalbn(z, n);   /* subnormal output */
  else z = setHi(z, HI(z) + (int32_t)((uint32_t)n << 20));
  return s * z;
}

// ---- Number::toString ----------------------------------------------------------------------------------------------
int js_number_to_string(double v, char* buf) {
  if (!(v == v)) { std::memcpy(buf, "NaN", 4); return 3; }
  if (v == 0) { buf[0] = '0'; buf[1] = 0; return 1; }
  char* o = buf;
  if (v < 0) { *o++ = '-'; v = -v; }
  if (std::isinf(v)) { std::memcpy(o, "Infinity", 9); return (int)(o - buf) + 8; }
  // the shortest digits that round-trip, as d.ddddde[+-]xx (std::to_chars: C++17's shortest form)
  char sci[40]; const auto res = std::to_chars(sci, sci + sizeof sci, v, std::chars_format::scientific);
  char dig[24]; int k = 0, e = 0; const char* p = sci;
  for (; p < res.ptr && *p != 'e'; p++) if (*p != '.') dig[k++] = *p;
  if (p < res.ptr) { p++; bool neg = false; if (*p == '-') { neg = true; p++; } else if (*p == '+') p++; for (; p < res.ptr; p++) e = e * 10 + (*p - '0'); if (neg) e = -e; }
  const int n = e + 1;   // the decimal point sits after digit n (ECMAScript's n)
  if (k <= n && n <= 21) { std::memcpy(o, dig, (size_t)k); o += k; for (int i = k; i < n; i++) *o++ = '0'; }
  else if (0 < n && n <= 21) { std::memcpy(o, dig, (size_t)n); o += n; *o++ = '.'; std::memcpy(o, dig + n, (size_t)(k - n)); o += k - n; }
  else if (-6 < n && n <= 0) { *o++ = '0'; *o++ = '.'; for (int i = 0; i < -n; i++) *o++ = '0'; std::memcpy(o, dig, (size_t)k); o += k; }
  else { *o++ = dig[0]; if (k > 1) { *o++ = '.'; std::memcpy(o, dig + 1, (size_t)(k - 1)); o += k - 1; }
    *o++ = 'e'; int x = n - 1; *o++ = x < 0 ? '-' : '+'; if (x < 0) x = -x; char t[8]; int m = 0; do { t[m++] = (char)('0' + x % 10); x /= 10; } while (x); while (m) *o++ = t[--m]; }
  *o = 0; return (int)(o - buf);
}

}  // namespace thunder
