// See fxbank.h.
#include "fx/fxbank.h"

#include <algorithm>

#include "rt/jsmath.h"

namespace thunder {

FxBank::FxBank(double sr) : sr_(sr) {
  for (int t = 0; t <= kSongNT; t++) h_[t].reset(new FxHost(sr));
  for (int t = 0; t <= kSongNT; t++) for (int k = 0; k < kFxSlots; k++) ov_[t][k].mask = 0;
}
void FxBank::load(const Song& S, double bpm, double restartN) {
  bpm_ = bpm; restartN_ = restartN;
  for (int t = 0; t < kSongNT; t++) { FxPlan* p = h_[t]->planFromSong(S.tfx.at((size_t)t), bpm, restartN); if (p) { h_[t]->commit(p); delete p; } }
  { FxPlan* p = h_[kSongNT]->planFromSong(S.mfx, bpm, restartN); if (p) { h_[kSongNT]->commit(p); delete p; } }
  for (int t = 0; t <= kSongNT; t++) if (h_[t]->slotCount() > 0) flush(t, 0);
}
void FxBank::attach(Mixer& m, bool master) {
  mix_ = &m;
  for (int t = 0; t < kSongNT; t++) m.setFx(t, h_[t]->slotCount() > 0 ? &FxBank::hook : nullptr, this);
  if (master && h_[kSongNT]->slotCount() > 0) m.setMasterFx(&FxBank::hook, this);
}
void FxBank::hook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed*, int) {
  FxBank* b = (FxBank*)user; if (track < 0 || track > kSongNT) return;
  // the clock this node hears (clkBus through the node's own delay), valid during the mixer's render: the node's `bp` param, `frames` long
  b->h_[track]->process(L, R, frames, t0, b->mix_ ? b->mix_->beat(track) : nullptr, b->mix_ ? frames : 0);
}
void FxBank::flush(int track, double now) {   // fxcFlush: booked at max(t, now), no clock-delay shift
  int w = 0;
  for (int i = 0; i < nPend_; i++) { const Pend& e = pend_[i]; if (e.track == track) h_[track]->lane(e.p, e.v, std::max(e.t, now)); else pend_[w++] = e; }
  nPend_ = w;
}
void FxBank::reset(int track, double t, double now) {
  if (track < 0 || track > kSongNT) return;
  const double at = std::max(t, now); h_[track]->reset(at);
  int w = 0; for (int i = 0; i < nPend_; i++) if (pend_[i].track != track) pend_[w++] = pend_[i]; nPend_ = w;
}
bool FxBank::event(const SeqEvent& e, double now, const Mixer& m) {
  const int tr = e.track >= kSongNT ? kSongNT : e.track; if (tr < 0) return false;
  if (e.type == SEV_FXC) {
    if (e.p < 0 || e.p > 10) return false;
    FxHost& H = *h_[tr];
    if (H.slotCount() > 0) { H.lane(e.p, e.v, std::max(e.t, now) + m.clockDelaySec(tr)); return true; }
    if (nPend_ < kPend) pend_[nPend_++] = Pend{tr, e.p, e.v, e.t}; else pendDropped_++;   // FIRST PLAY: the node is not there yet
    return true;
  }
  if (e.type == SEV_FXP) {   // FXV: the slot's controls with this one moved, mapped, queued at its time (unshifted)
    FxHost& H = *h_[tr]; const FxEntry* c = nullptr; for (int i = 0; i < H.chainSize(); i++) if (H.chainEntry(i)->slot == e.slot) { c = H.chainEntry(i); break; }
    if (!c || e.slot < 0 || e.slot >= kFxSlots || e.pi < 0 || e.pi >= c->nvals) return false;
    Ov& o = ov_[tr][e.slot]; const int mn = fx_param_min(c->t, e.pi), mx = fx_param_max(c->t, e.pi);
    o.v[e.pi] = mn + js_round((double)(mx - mn) * e.v / 255); o.mask |= 1u << e.pi;
    double vals[12]; for (int i = 0; i < 12; i++) vals[i] = c->vals[i]; for (int i = 0; i < c->nvals; i++) if (o.mask >> i & 1u) vals[i] = o.v[i];
    double p[12]; fx_map(c->t, vals, sr_, bpm_, restartN_, p); H.psetAt(std::max(e.t, now), e.slot, p); return true;
  }
  return false;
}
int FxBank::pdcSlots(PdcSlot* out, int max) const {
  int n = 0;
  for (int t = 0; t < kSongNT; t++) for (int i = 0; i < h_[t]->slotCount() && n < max; i++) out[n++] = PdcSlot{t, h_[t]->slotNumber(i), h_[t]->slotOn(i), h_[t]->slotBypass(i), h_[t]->slotLatency(i)};
  return n;
}

}  // namespace thunder
