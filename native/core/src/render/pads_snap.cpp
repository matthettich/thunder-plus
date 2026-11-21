// See pads_snap.h. Built WITH exceptions (core/CMakeLists.txt, like render.cpp): the std::vector members of RenderPads are copied here, on the control thread, and std::bad_alloc is caught.
#include "render/pads_snap.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

#include "render/players.h"
#include "thunder/render.h"   // the C API's limits (THUNDER_RENDER_MAX_*), applied here as there

namespace thunder {

static_assert(std::atomic<uint64_t>::is_always_lock_free, "the epoch board must be lock-free");

#if defined(__cpp_exceptions)
#define PS_TRY try
#define PS_CATCH_NOMEM catch (...)
#else
#define PS_TRY if (true)
#define PS_CATCH_NOMEM else
#endif

static bool finite(double x) { return std::isfinite(x); }

PadsSnap::~PadsSnap() {
  if (!owner) return;
  for (int k = 0; k < 2; k++) for (int p = 0; p < kRenderPads; p++) if (hold[k][p]) owner->release(hold[k][p]);
}

PadsHost* PadsHost::create(const Players& pl) {
  const int n = pl.epochSlots();
  if (n <= 0 || n > kBoardMax) return nullptr;   // the board covers exactly the pools of `pl`
  PadsHost* h = new (std::nothrow) PadsHost();
  if (!h) return nullptr;
  h->slot_ = new (std::nothrow) SnapSlot<PadsSnap>();
  if (!h->slot_) { delete h; return nullptr; }
  h->owner_ = &pl; h->nBoard_ = n;
  for (int i = 0; i < kBoardMax; i++) { h->board_[i].store(0, std::memory_order_relaxed); h->scan_[i] = h->last_[i] = 0; }
  if (h->publish() != kOk) { delete h; return nullptr; }   // an EMPTY snapshot: the first quantum adopts it, so Players has pads from then on
  return h;
}

PadsHost::~PadsHost() {
  delete slot_; slot_ = nullptr;   // the snapshots (pending, current, returned) go: their references are released into the retired list
  for (int k = 0; k < 2; k++) for (int p = 0; p < kRenderPads; p++) if (cur_[k][p]) { release(cur_[k][p]); cur_[k][p] = nullptr; }
  while (retired_) { PadBuf* b = retired_; retired_ = b->nextRetired; freeBuf(b); }   // the audio side has stopped: nothing can read them
}

PadBuf* PadsHost::newBuf() {
  PadBuf* b = new (std::nothrow) PadBuf();
  if (!b) return nullptr;
  b->epoch = nextEpoch_++; b->refs = 1; made_++;
  return b;
}

void PadsHost::release(PadBuf* b) {
  if (--b->refs == 0) { b->nextRetired = retired_; retired_ = b; }
}

void PadsHost::freeBuf(PadBuf* b) {
  if (hook_) hook_(hookUser_, b);   // (a test may take mem / varMem: the pointers are then null)
  delete[] b->mem; delete[] b->vars; delete[] b->varMem; delete b; freed_++;
}

size_t PadsHost::retired() const { size_t n = 0; for (const PadBuf* b = retired_; b; b = b->nextRetired) n++; return n; }

int PadsHost::setPad(int pad, const float* l, const float* r, int frames, double rate, const PadVarIn* vars, int nVars) {
  if (pad < 0 || pad >= kRenderPads || frames < 0 || frames > THUNDER_RENDER_MAX_PAD_FRAMES || nVars < 0 || nVars > THUNDER_RENDER_MAX_VARIATIONS) return kArg;
  if (frames == 0) {
    if (cur_[0][pad]) { release(cur_[0][pad]); cur_[0][pad] = nullptr; }
    stage_.chop.setLoop(pad, 0);
    return kOk;
  }
  if (!l || !finite(rate) || rate < 1 || rate > 1e6 || (nVars > 0 && !vars)) return kArg;
  for (int i = 0; i < frames; i++) if (!finite(l[i]) || (r && !finite(r[i]))) return kArg;
  size_t varFrames = 0;
  for (int i = 0; i < nVars; i++) {
    const PadVarIn& v = vars[i];
    if (!v.mono || v.frames <= 0 || v.frames > THUNDER_RENDER_MAX_PAD_FRAMES || !finite(v.sr) || v.sr < 1 || v.sr > 1e6) return kArg;
    for (int j = 0; j < v.frames; j++) if (!finite(v.mono[j])) return kArg;
    for (int j = 0; j < i; j++) if (vars[j].ii == v.ii && vars[j].kind == v.kind && vars[j].num == v.num) return kArg;
    varFrames += (size_t)v.frames;
  }
  if (((size_t)frames * (r ? 2 : 1) + varFrames) * sizeof(float) > THUNDER_RENDER_MAX_PAD_BYTES) return kArg;   // one pad and its variations together, as the C API's budget
  // identical to what the pad has: nothing changes (the buffer stays shared between snapshots)
  const PadBuf* c = cur_[0][pad];
  if (c && c->frames == frames && c->rate == rate && c->stereo == (r != nullptr) && c->nVars == nVars
      && std::memcmp(c->L(), l, (size_t)frames * sizeof(float)) == 0 && (!r || std::memcmp(c->R(), r, (size_t)frames * sizeof(float)) == 0)) {
    bool same = true;
    for (int i = 0; i < nVars && same; i++) {
      const PadVarIn& v = vars[i]; const PadBuf::Var* o = nullptr;
      for (int j = 0; j < c->nVars; j++) if (c->vars[j].ii == v.ii && c->vars[j].kind == v.kind && c->vars[j].num == v.num) o = &c->vars[j];
      same = o && o->frames == v.frames && o->sr == v.sr && std::memcmp(o->data, v.mono, (size_t)v.frames * sizeof(float)) == 0;
    }
    if (same) return kOk;
  }
  const size_t n = (size_t)frames * (r ? 2 : 1);
  float* mem = new (std::nothrow) float[n];
  float* vmem = varFrames ? new (std::nothrow) float[varFrames] : nullptr;
  PadBuf::Var* vv = nVars ? new (std::nothrow) PadBuf::Var[(size_t)nVars] : nullptr;
  PadBuf* b = (mem && (!varFrames || vmem) && (!nVars || vv)) ? newBuf() : nullptr;
  if (!b) { delete[] mem; delete[] vmem; delete[] vv; return kNoMem; }
  std::memcpy(mem, l, (size_t)frames * sizeof(float));
  if (r) std::memcpy(mem + frames, r, (size_t)frames * sizeof(float));
  b->mem = mem; b->frames = frames; b->rate = rate; b->stereo = r != nullptr;
  size_t off = 0;
  for (int i = 0; i < nVars; i++) {
    std::memcpy(vmem + off, vars[i].mono, (size_t)vars[i].frames * sizeof(float));
    PadBuf::Var& d = vv[i]; d.ii = vars[i].ii; d.kind = vars[i].kind; d.num = vars[i].num; d.frames = vars[i].frames; d.sr = vars[i].sr; d.data = vmem + off;
    off += (size_t)vars[i].frames;
  }
  b->vars = vv; b->nVars = nVars; b->varMem = vmem;
  if (cur_[0][pad]) release(cur_[0][pad]);
  cur_[0][pad] = b;
  stage_.chop.setLoop(pad, 0);   // a changed pad loses its chop data (the C API's rule)
  return kOk;
}

int PadsHost::setKit(int pad, const VoiceKit& kit) {
  if (pad < 0 || pad >= kRenderPads || kit.choke < 0 || kit.choke > 255 || !finite(kit.rel) || kit.rel < 0 || kit.rel > 60) return kArg;
  stage_.kit[pad] = kit; return kOk;
}
int PadsHost::setRoute(int pad, PadRoute route) {
  if (pad < 0 || pad >= kRenderPads) return kArg;
  stage_.route[pad] = route; return kOk;
}

int PadsHost::setSf(int pad, std::shared_ptr<const sf::Font> font, const sf::Preset* pr, std::shared_ptr<const sf::PresetPcm> pcm, const sf::PadParams& P) {
  if (pad < 0 || pad >= kRenderPads || !font || !pcm || !pr || pr < font->presets.data() || pr >= font->presets.data() + font->presets.size()) return kArg;
  PadBuf* b = newBuf();
  if (!b) return kNoMem;
  b->font = std::move(font); b->pcm = std::move(pcm); b->pr = pr;
  if (cur_[1][pad]) release(cur_[1][pad]);
  cur_[1][pad] = b; stage_.sf[pad].P = P;
  return kOk;
}
int PadsHost::clearSf(int pad) {
  if (pad < 0 || pad >= kRenderPads) return kArg;
  if (cur_[1][pad]) { release(cur_[1][pad]); cur_[1][pad] = nullptr; }
  return kOk;
}
int PadsHost::setSfParams(int pad, const sf::PadParams& P) {
  if (pad < 0 || pad >= kRenderPads) return kArg;
  stage_.sf[pad].P = P; return kOk;
}

int PadsHost::publish() {
  PadsSnap* s = new (std::nothrow) PadsSnap();
  if (!s) return kNoMem;
  s->owner = this; s->id = nextId_;
  PS_TRY {
    s->pads = stage_;   // kit, route, sf params, chop table: a private copy (the vector in the chop table is copied here)
    size_t nv = 0;
    for (int p = 0; p < kRenderPads; p++) if (cur_[0][p]) nv += (size_t)cur_[0][p]->nVars;
    s->pads.var.clear(); s->pads.var.reserve(nv);
    for (int p = 0; p < kRenderPads; p++) {
      if (PadBuf* b = cur_[0][p]) {
        addRef(b); s->hold[0][p] = b;
        VoiceBuf& d = s->pads.buf[p]; d = VoiceBuf();
        d.data = b->L(); d.data2 = b->R(); d.len = b->frames; d.sr = b->rate; d.epoch = b->epoch;
        for (int i = 0; i < b->nVars; i++) {
          const PadBuf::Var& v = b->vars[i]; RenderVar rv; rv.ii = v.ii; rv.kind = v.kind; rv.num = v.num; rv.pad = p;
          rv.buf.data = v.data; rv.buf.len = v.frames; rv.buf.sr = v.sr; rv.buf.epoch = b->epoch;   // no `data` of its own: the PadBuf owns it
          s->pads.var.push_back(std::move(rv));
        }
      } else s->pads.buf[p] = VoiceBuf();
      sf::SfPad& sp = s->pads.sf[p];
      if (PadBuf* b = cur_[1][p]) { addRef(b); s->hold[1][p] = b; sp.font = b->font.get(); sp.pr = b->pr; sp.pcm = b->pcm.get(); sp.epoch = b->epoch; }
      else { sp.font = nullptr; sp.pr = nullptr; sp.pcm = nullptr; sp.epoch = 0; }
    }
    std::sort(s->pads.var.begin(), s->pads.var.end(), [](const RenderVar& a, const RenderVar& b) { return RenderPads::varLess(a, b.ii, b.kind, b.num, b.pad); });
  } PS_CATCH_NOMEM { delete s; return kNoMem; }
  if (!slot_->publish(s)) { delete s; return kFull; }   // (the builder still holds its references: nothing retires)
  nextId_++;
  return kOk;
}

size_t PadsHost::collect() {
  slot_->collect();   // the snapshots the audio side returned are deleted here: the last reference of a replaced buffer retires it
  if (!retired_) return 0;
  uint64_t b[kBoardMax];   // one pass over the board, after the snapshots were popped (acquire); then retired x nBoard compares
  for (int i = 0; i < nBoard_; i++) b[i] = board_[i].load(std::memory_order_acquire);
  size_t n = 0;
  PadBuf** pp = &retired_;
  while (*pp) {
    PadBuf* x = *pp; bool used = false;
    for (int i = 0; i < nBoard_ && !used; i++) used = b[i] == x->epoch;
    if (!used) { *pp = x->nextRetired; freeBuf(x); n++; }
    else pp = &x->nextRetired;
  }
  return n;
}

bool PadsHost::beginQuantum(Players& pl, Seq* seq) {
  assert(&pl == owner_);
  if (&pl != owner_) return false;   // a Players whose pools the board does not cover: nothing is adopted, nothing is published
  seam(0);
  for (int i = 0; i < nBoard_; i++) scan_[i] = 0;
  pl.epochVoices(scan_);
  seam(1);
  for (int i = 0; i < nBoard_; i++) if (scan_[i] != last_[i]) { last_[i] = scan_[i]; board_[i].store(scan_[i], std::memory_order_release); }
  seam(2);
  const bool changed = slot_->adopt();   // (the old snapshot goes back to the control thread only now, AFTER the stores above)
  seam(3);
  const PadsSnap* s = slot_->get();
  if (changed) pl.setPads(&s->pads);
  assert(!(changed && !seq && seqSeen_));   // a Seq the host still holds was not passed: it would point into the snapshot just returned
  if (changed && !seq && seqSeen_) orphan_++;
  if (s && seq) { seq->setChop(&s->pads.chop); seqSeen_ = true; }
  seam(4);
  return changed;
}

}  // namespace thunder
