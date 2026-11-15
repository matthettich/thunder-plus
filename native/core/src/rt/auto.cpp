// See auto.h. Each method keeps the JS method's statements and arithmetic order (core/core-processor.js `class Auto`).
#include "auto.h"

#include <cstring>

#include "jsmath.h"

namespace thunder {

bool Auto::ins(const AutoEv& e) {
  if (n_ >= cap_) { dropped++; return false; }
  int i = n_;
  while (i > 0 && ev_[i - 1].t > e.t) i--;
  if (i < n_) std::memmove(ev_ + i + 1, ev_ + i, sizeof(AutoEv) * (size_t)(n_ - i));
  ev_[i] = e; n_++;
  return true;
}

void Auto::cancel(double t) {
  int w = 0;
  for (int q = 0; q < n_; q++) if (ev_[q].t < t) { if (w != q) ev_[w] = ev_[q]; w++; }
  n_ = w;
}

bool Auto::constFrom(double t, double* v) const {
  if (!n_) { *v = v0; return true; }
  const AutoEv& e = ev_[n_ - 1];
  if (e.t <= t && (e.k == 0 || e.k == 1)) { *v = e.v; return true; }
  return false;
}

bool Auto::constBlk(double t, double t1, double* out) const {
  double v = v0; int i = 0;
  for (; i < n_ && ev_[i].t <= t; i++) { const AutoEv& e = ev_[i]; if (e.k > 1) return false; v = e.v; }
  if (i < n_) { const AutoEv& e = ev_[i]; if (e.t <= t1 || e.k == 1) return false; }
  *out = v; return true;
}

void Auto::prune(double t) {
  if (!n_ || ev_[0].t > t) return;
  // A target in progress lives in scalars: hs (is there one), sf (from), sT (start), so (to), sc (time constant).
  double v = v0, sf = 0, sT = 0, so = 0, sc = 1, pt = 0; bool hs = false; int cur = -1, i = 0;
  for (; i < n_; i++) { const AutoEv& e = ev_[i];
    if (e.k == 1) { if (e.t > t) break; v = e.v; hs = false; pt = e.t; continue; }   // (as the JS: a ramp point does not end a curve in progress)
    if (e.t > t) break;
    if (e.k == 0) { v = e.v; hs = false; cur = -1; pt = e.t; }
    else if (e.k == 2) { sf = hs ? so + (sf - so) * js_exp(-(e.t - sT) / sc) : v; hs = true; sT = e.t; so = e.v; sc = e.tc; cur = -1; }
    else { if (e.t + e.d > t) { cur = i; } else { v = e.c(e.t + e.d); hs = false; cur = -1; pt = e.t + e.d; } } }
  if (i == 0) return;
  // A linear ramp still running (the next event a k 1 later than t) starts from the last point's TIME, not t: keep the
  // point where it was (FLX CHECK 2 in the JS). Otherwise the head is the value now, at t.
  double now = v; int h = 1;
  if (!(!hs && cur < 0 && i < n_ && ev_[i].k == 1)) {
    now = cur >= 0 ? ev_[cur].c(t) : hs ? so + (sf - so) * js_exp(-(t - sT) / sc) : v; pt = t; h = 1 + (hs ? 1 : 0) + (cur >= 0 ? 1 : 0); }
  AutoEv head[3]; int w = 0;
  head[w].k = 0; head[w].t = pt; head[w].v = now; w++;
  if (hs) { head[w].k = 2; head[w].t = t; head[w].v = so; head[w].tc = sc; w++; }
  if (cur >= 0) head[w++] = ev_[cur];
  const int rest = n_ - i;
  if (rest > 0 && h != i) std::memmove(ev_ + h, ev_ + i, sizeof(AutoEv) * (size_t)rest);   // h <= i + 2 <= storage (kSlack)
  for (int q = 0; q < h; q++) ev_[q] = head[q];
  n_ = h + rest;
  v0 = now;
}

double Auto::at(double t) const {
  double v = v0, pt = 0, sf = 0, sT = 0, so = 0, sc = 1; bool hs = false;
  for (int q = 0; q < n_; q++) { const AutoEv& e = ev_[q];
    if (e.k == 1) { if (t < e.t) { const double a = hs ? so + (sf - so) * js_exp(-(pt - sT) / sc) : v; return e.t > pt ? a + (e.v - a) * (t - pt) / (e.t - pt) : e.v; } v = e.v; hs = false; pt = e.t; continue; }
    if (t < e.t) return hs ? so + (sf - so) * js_exp(-(t - sT) / sc) : v;
    if (e.k == 0) { v = e.v; hs = false; pt = e.t; }
    else if (e.k == 2) { sf = hs ? so + (sf - so) * js_exp(-(e.t - sT) / sc) : v; hs = true; sT = e.t; so = e.v; sc = e.tc; pt = e.t; }
    else { if (t < e.t + e.d) return e.c(t); v = e.c(e.t + e.d); hs = false; pt = e.t + e.d; }
  }
  return hs ? so + (sf - so) * js_exp(-(t - sT) / sc) : v;
}

void Auto::fill(double* C, double t0, double dt, int N, int i0) const {
  for (int i = i0; i < N; i++) C[i] = at(t0 + i * dt);
}

}  // namespace thunder
