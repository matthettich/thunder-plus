// U20: FFTR, the real FFT that Spectral, Extreme stretch and Convolution reverb share (chains-worklet.js, `class FFTR`, and the shared FFTR_CACHE), line for line.
// It packs n real samples into an n/2-point complex FFT (radix 2, decimation in time) and splits the result. Every value in the transform is a double (the job's zr / zi are Float64Arrays); the
// arrays it reads and writes (x, re, im) are whatever the caller has: a Float32Array rounds on a store, a Float64Array does not, so the functions are templates over the element type and a
// store goes through (T): the arithmetic is the JS's, in the JS's order (the core is built with floating-point contraction off).
// THE CACHE (FFTR_CACHE): FFTR::get(n) makes a size's tables once and every module that wants that size shares them. The tables never change after they are made, so sharing is safe from any
// thread; making one allocates and takes a lock, so it happens in a module's constructor (the JS does it in create(), "made now, not on the audio thread later") and a module keeps the pointers
// it needs: nothing calls get() in process(). The JS's FFTR also owns one job for the convenience forward() / inverse(); here the caller passes its own Job (a job is scratch that fwdStart /
// invStart overwrite completely, so the result does not depend on whose it is) and no two threads ever share one.
#ifndef THUNDER_MODFX_FFTR_H
#define THUNDER_MODFX_FFTR_H
#include <cstdint>
#include <vector>

#include "modfx/util.h"

namespace thunder {
namespace modfx {

class FFTR {
 public:
  // FFTR.job(maxN): the work of one transform in flight (any size up to maxN)
  struct Job {
    explicit Job(int maxN) { const int m = maxN >> 1 > 2 ? maxN >> 1 : 2; zr.init(m); zi.init(m); }
    Arr<double> zr, zi; int p = 0; bool inv = false;
  };
  static const FFTR* get(int n);   // makes the size's tables on first use (allocates, locks): call it outside the audio path
  static const float* hann(int n); // hannWin(n) as the Float32Array, one per size, shared (xstHann; the others make theirs once)

  int n = 0, m = 0, passes = 0;

  template <class X> void fwdStart(Job& J, const X* x) const {
    double* zr = J.zr.get(); double* zi = J.zi.get();
    for (int i = 0, j = 0; i < m; i++, j += 2) { const int r = rev_[(size_t)i]; zr[r] = (double)x[j]; zi[r] = (double)x[j + 1]; }
    J.p = 0; J.inv = false;
  }
  // One radix-2 pass of the complex FFT; returns how many are left.
  int pass(Job& J) const {
    double* zr = J.zr.get(); double* zi = J.zi.get(); const int p = J.p;
    if (p >= passes) return 0;
    const int half = 1 << p, size = half << 1; const double sg = J.inv ? 1 : -1;
    if (half == 1) {
      for (int a = 0; a < m; a += 2) { const int b = a + 1; const double ar = zr[a], ai = zi[a], br = zr[b], bi = zi[b]; zr[a] = ar + br; zi[a] = ai + bi; zr[b] = ar - br; zi[b] = ai - bi; }
    } else if (half == 2) {
      for (int i = 0; i < m; i += 4) {
        int a = i, b = i + 2; double tr = zr[b], ti = zi[b];
        zr[b] = zr[a] - tr; zi[b] = zi[a] - ti; zr[a] += tr; zi[a] += ti;
        a = i + 1; b = i + 3; tr = -sg * zi[b]; ti = sg * zr[b];   // twiddle -+i
        zr[b] = zr[a] - tr; zi[b] = zi[a] - ti; zr[a] += tr; zi[a] += ti;
      }
    } else {
      const double* C = c_.data(); const double* S = s_.data();
      for (int i = 0; i < m; i += size) {
        for (int j = 0, a = i, b = i + half; j < half; j++, a++, b++) {
          const double wr = C[half + j], wi = sg * S[half + j], xr = zr[b], xi = zi[b], tr = xr * wr - xi * wi, ti = xr * wi + xi * wr;
          zr[b] = zr[a] - tr; zi[b] = zi[a] - ti; zr[a] += tr; zi[a] += ti;
        }
      }
    }
    J.p = p + 1; return passes - J.p;
  }
  template <class R, class I> void fwdEnd(Job& J, R* re, I* im, int o = 0, int oi = -1) const {
    if (oi < 0) oi = o;
    const double* zr = J.zr.get(); const double* zi = J.zi.get(); const double* SC = sc_.data(); const double* SS = ss_.data();
    re[o] = (R)(zr[0] + zi[0]); im[oi] = (I)0.0; re[o + m] = (R)(zr[0] - zi[0]); im[oi + m] = (I)0.0;
    for (int k = 1, q = m - 1; k <= q; k++, q--) {
      // bins k and m - k together, from Z[k] and Z[m - k]
      const double a = zr[k], b = zi[k], c = zr[q], d = zi[q];
      const double er = (a + c) * .5, ei = (b - d) * .5, fr = (b + d) * .5, fi = (c - a) * .5;
      const double wr = SC[k], wi = -SS[k];
      re[o + k] = (R)(er + wr * fr - wi * fi); im[oi + k] = (I)(ei + wr * fi + wi * fr);
      if (q != k) {
        // for m - k: Fe and Fo are the conjugates, and the twiddle is -conj(W^k)
        const double vr = -wr, vi = wi;
        re[o + q] = (R)(er + vr * fr + vi * fi); im[oi + q] = (I)(-ei - vr * fi + vi * fr);
      }
    }
  }
  template <class R, class I> void invStart(Job& J, const R* re, const I* im, int o = 0, int oi = -1) const {
    if (oi < 0) oi = o;
    double* zr = J.zr.get(); double* zi = J.zi.get(); const double* SC = sc_.data(); const double* SS = ss_.data();
    for (int k = 0; k < m; k++) {
      const double xr = (double)re[o + k], xi = (double)im[oi + k], yr = (double)re[o + m - k], yi = (double)im[oi + m - k];
      const double er = (xr + yr) * .5, ei = (xi - yi) * .5, dr = (xr - yr) * .5, di = (xi + yi) * .5;
      const double c = SC[k], s = SS[k], fr = c * dr - s * di, fi = c * di + s * dr;
      const int r = rev_[(size_t)k]; zr[r] = er - fi; zi[r] = ei + fr;
    }
    J.p = 0; J.inv = true;
  }
  template <class X> void invEnd(Job& J, X* x, double g = 1) const {
    const double* zr = J.zr.get(); const double* zi = J.zi.get(); const double s = g / m;
    for (int i = 0, j = 0; i < m; i++, j += 2) { x[j] = (X)(zr[i] * s); x[j + 1] = (X)(zi[i] * s); }
  }
  template <class X, class R, class I> void forward(Job& J, const X* x, R* re, I* im, int o = 0, int oi = -1) const { fwdStart(J, x); while (pass(J)) {} fwdEnd(J, re, im, o, oi); }
  template <class R, class I, class X> void inverse(Job& J, const R* re, const I* im, X* x, int o = 0, int oi = -1, double g = 1) const { invStart(J, re, im, o, oi); while (pass(J)) {} invEnd(J, x, g); }

 private:
  friend struct FFTRMaker;
  explicit FFTR(int n_);
  std::vector<int32_t> rev_;
  std::vector<double> c_, s_, sc_, ss_;
};

}  // namespace modfx
}  // namespace thunder
#endif
