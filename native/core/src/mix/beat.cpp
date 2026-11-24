// U10: see beat.h. The lines of core/core-processor.js: ev() 'beat', the stop / play resets, process1's clock fill.
#include "mix/beat.h"

#include <algorithm>
#include <cmath>

namespace thunder {

bool BeatClock::event(const SeqEvent& e, double now) {
  if (e.type != SEV_BEAT) return false;
  const double at = std::max(e.t, now), b = e.v;
  if (page_) {   // the page player's play('beat'): offset.setValueAtTime(beat - base) at the first, then linearRampToValueAtTime, and a set at the wrap
    if (!on_) { on_ = true; base_ = std::floor(b / 16) * 16; pins(0, b - base_, at); }
    else { const double v = b - base_; pins(1, v, at); if (v >= 16) { base_ += 16; pins(0, v - 16, at); } }
    if (pn_ > peak_) peak_ = pn_;
    return true;
  }
  if (!on_) { on_ = true; base_ = std::floor(b / 16) * 16; A_.set(b - base_ + 1, at); }
  else { const double v = b - base_; A_.lin(v + 1, at); if (v >= 16) { base_ += 16; A_.set(v - 16 + 1, at); } }
  if (A_.size() > peak_) peak_ = A_.size();
  return true;
}

void BeatClock::pins(int k, double v, double t) {
  if (pn_ >= kPEv) { pdrop_++; return; }
  int i = pn_; while (i > 0 && pev_[i - 1].t > t) i--;
  for (int j = pn_; j > i; j--) pev_[j] = pev_[j - 1];
  pev_[i].t = t; pev_[i].v = v; pev_[i].k = k; pn_++;
}
void BeatClock::pcancel(double t) { int w = 0; for (int q = 0; q < pn_; q++) if (pev_[q].t < t) pev_[w++] = pev_[q]; pn_ = w; }
void BeatClock::stop(double at) {
  on_ = false;
  if (page_) { pcancel(at); pins(0, -1, at); return; }   // clkReset: cancelScheduledValues(at), setValueAtTime(-1, at)
  A_.cancel(at); A_.set(0, at);
}

// The page player's clock for one block, the values Chrome's AudioParamTimeline gives a ConstantSource's `offset` (the float32 arithmetic below was read off
// Chrome's own output, frame by frame, on ELECTRO: tools/songref-dbg.mjs; 2964 of 2964 blocks bit-equal). A chunk is the frames of the block inside one
// ramp (cut at the block's start and at the frame an event falls on); within it, with k = 1 / (t2 - t1), vd = v2 - v1 (float32), inc1 = k * vd / sr:
//   frames 0..3: v1 + ((cs / sr - t1) + i / sr) * k * vd, each step rounded to float32; then value[j] = value[j - 4] + 4 * inc1 (float32), for whole groups of four;
//   the frames after the last whole group: v1 + float32((frame / sr - t1) * k) * vd.
void BeatClock::prender(float* C, int N, double t0) {
  const double sr = sr_; const int64_t fs = (int64_t)std::llround(t0 * sr);
  int drop = 0; while (drop + 1 < pn_ && pev_[drop + 1].t <= (double)fs / sr) drop++;   // older events cannot matter again
  if (drop > 0) { for (int q = drop; q < pn_; q++) pev_[q - drop] = pev_[q]; pn_ -= drop; }
  int64_t f0 = fs; const int64_t end = fs + N;
  while (f0 < end) {
    const double ts = (double)f0 / sr; int i = -1; while (i + 1 < pn_ && pev_[i + 1].t <= ts) i++;
    const PEv* e = i >= 0 ? &pev_[i] : nullptr; const PEv* nn = i + 1 < pn_ ? &pev_[i + 1] : nullptr;
    int64_t fe = nn ? std::min<int64_t>(end, (int64_t)std::ceil(nn->t * sr - 1e-9)) : end; if (fe <= f0) fe = f0 + 1;
    float* o = C + (f0 - fs); const int n = (int)(fe - f0);
    if (!nn || nn->k != 1 || !e) { const float v = e ? (float)e->v : -1.0f; for (int j = 0; j < n; j++) o[j] = v; f0 = fe; continue; }
    const float k = (float)(1.0 / (nn->t - e->t)), v1 = (float)e->v, vd = (float)nn->v - v1;
    const float kvd = k * vd, inc1 = (float)((double)kvd / sr), inc4 = 4 * inc1, sinv = (float)(1.0 / sr);
    const float a = (float)((double)f0 / sr - e->t); const int nv = n - n % 4;
    for (int j = 0; j < n; j++) {
      if (j >= nv) { const float x = (float)(((double)(f0 + j) / sr - e->t) * (double)k); o[j] = (float)((double)v1 + (double)x * (double)vd); }
      else if (j < 4) { const float b = (float)((double)j * (double)sinv), c = a + b, d = c * k, ee = d * vd; o[j] = v1 + ee; }
      else o[j] = o[j - 4] + inc4;
    }
    f0 = fe;
  }
}

void BeatClock::render(float* C, int N, double t0, double dt, double* scratch) {
  if (page_) { prender(C, N, t0); return; }
  A_.prune(t0);
  if (A_.size() == 0 && A_.v0 == (page_ ? -1 : 0)) { for (int i = 0; i < N; i++) C[i] = page_ ? -1.0f : 0.0f; return; }
  A_.fill(scratch, t0, dt, N);
  for (int i = 0; i < N; i++) C[i] = (float)scratch[i];
}

}  // namespace thunder
