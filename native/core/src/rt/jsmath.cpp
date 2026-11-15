// js_exp: fdlibm e_exp.c (the algorithm V8's base/ieee754.cc uses for Math.exp).
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

}  // namespace thunder
