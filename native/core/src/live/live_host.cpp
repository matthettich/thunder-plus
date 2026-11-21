// THE LIVE HOST (U37 P2): see live_host.h (the declaration, one comment per method) and include/thunder/live.h (the contract). This file is the core of it: life, song, pads, transport, the quantum, the events and the
// step dump. NOT here (later units): effects and the master chain (P3), PDC (P3), MIDI (P4), meters beyond a stub, the C API functions (P5), SoundFont and live-engine pads (answered E_UNSUPPORTED, see below).
//
// BUILT WITH EXCEPTIONS (core/CMakeLists.txt, like render.cpp and pads_snap.cpp): the CONTROL side calls Song::load, Seq / SeqSession construction and std::vector code that throw std::bad_alloc, which is caught
// here and returned as E_MEMORY (live.h, OUT OF MEMORY). The AUDIO side (process() and everything it calls) throws nothing, allocates nothing, takes no lock, frees nothing and does no I/O.
//
// WHO TOUCHES WHAT. Every member is either CONTROL ONLY, AUDIO ONLY, or crosses through a named Spsc / SnapSlot / atomic with its memory order stated at the member (live_host.h, STATE). The audio side never frees what
// the control side allocated: song snapshots, pads snapshots, play plans and buffers travel back through rings and are freed by collect().
#include "live/live_host.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

#include "engine/layer_pe.h"
#include "live/dump_plain.h"
#include "render/render.h"
#include "rt/num.h"

namespace thunder {

namespace {
// stats.* of the audio side, in pub_[]: the audio thread stores each once a quantum (relaxed), any thread loads (relaxed)
enum Pub { P_FRAMES, P_ORIGIN, P_QUANTA, P_CALLS, P_STEPS, P_EVENTS, P_SEQDROP, P_SKIPS, P_CMDS, P_DUMPDROP, P_VOICES, P_PEAKV, P_NOTES, P_NPLAYED, P_NLATE, P_NNOPAD, P_NLIVE, P_NSF, P_NVARMISS,
  P_NSTRETCH, P_EVIGN, P_CMDLATE, P_EVTDROP, P_EVTCRIT, P_REANCH, P_PREP, P_LIVEFAIL, P_LIVELATE, P_SONGADOPT, P_COUNT };
constexpr double kEndStopSec = 0.3;          // the web's frame() rule: the MIDI stop 0.3 s after the song's end (live.h, THE STATE MACHINE)
constexpr double kPlayDelaySec = 0.08;       // t0 < 0: the next quantum's start + 0.08 s (live.h, thunder_live_play_opts::t0)
constexpr int kBudgetErr = THUNDER_LIVE_E_LIMIT;
inline bool finite(double x) { return std::isfinite(x); }
inline int64_t toFrame(double sec, double sr) { return (int64_t)std::llround(sec * sr); }
}  // namespace

PlayPlan::~PlayPlan() { for (int t = 0; t <= kSongNT; t++) { delete fx[t]; fx[t] = nullptr; } }

// =========================================================================================================================================================================================================
// CONTROL: life
// =========================================================================================================================================================================================================
LiveHost* LiveHost::create(const thunder_live_opts& o, char* err, size_t errCap) {
  auto fail = [&](const char* m) -> LiveHost* { if (err && errCap) std::snprintf(err, errCap, "%s", m); return nullptr; };
  if (err && errCap) err[0] = 0;
  if (o.reserved0 || o.reserved1 || o.reserved2) return fail("reserved option field is not zero");
  const double sr = o.sample_rate == 0 ? 48000.0 : o.sample_rate;
  if (!(sr >= 8000 && sr <= 384000)) return fail("sample_rate outside 8000 .. 384000");
  if (o.max_pad_bytes > ((uint64_t)1 << 31)) return fail("max_pad_bytes above 2^31");
  if (o.max_song_bytes > (uint64_t)THUNDER_LIVE_MAX_SONG_BYTES) return fail("max_song_bytes above 16 MiB");
  const int maxBlock = o.max_block == 0 ? kLiveQuantum : o.max_block;
  if (maxBlock < 1 || maxBlock > 1048576) return fail("max_block outside 1 .. 1048576");
  const int maxVoices = o.max_voices == 0 ? 64 : o.max_voices;
  if (maxVoices < 1 || maxVoices > 256) return fail("max_voices outside 1 .. 256");
  const int dumpSteps = o.dump_steps == 0 ? 256 : o.dump_steps;
  if (dumpSteps < 1 || dumpSteps > 4096) return fail("dump_steps outside 1 .. 4096");
  constexpr uint32_t kBuilt = THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_DUMP_STEPS | THUNDER_LIVE_F_PAGE_TIMING;
  if (o.flags & ~(kBuilt | THUNDER_LIVE_F_MIDI | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER)) return fail("unknown flag");
  // P2 does not make the MIDI scheduler (P4), the effects hosts or the master chain (P3): a host asked for them is refused here, not silently made without.
  if (o.flags & (THUNDER_LIVE_F_MIDI | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER)) return fail("F_MIDI / F_EFFECTS / F_MASTER are not built in this unit (U37 P3 / P4)");

  std::unique_ptr<LiveHost> h(new (std::nothrow) LiveHost());
  if (!h) return fail("out of memory");
  try {
    h->sr_ = sr; h->maxBlock_ = maxBlock; h->flags_ = o.flags;
    h->budget_ = o.max_pad_bytes ? o.max_pad_bytes : THUNDER_LIVE_DEFAULT_PAD_BYTES;
    h->maxSongBytes_ = o.max_song_bytes ? o.max_song_bytes : (uint64_t)THUNDER_LIVE_MAX_SONG_BYTES;
    h->players_.reset(new Players(nullptr, sr, maxVoices));        // no pads until the first snapshot: PadsHost::create publishes an EMPTY one, the first quantum adopts it
    h->players_->setDropLate(true);                                // ruling D-4: a note more than 0.15 s late is not played (stats.notes_late)
    if (o.flags & THUNDER_LIVE_F_PAGE_TIMING) h->players_->pool().setPageTiming(true);
    h->padsHost_ = PadsHost::create(*h->players_);                 // after Players: the board covers exactly its pools
    if (!h->padsHost_) return fail("out of memory (pads)");
    h->mix_.reset(new Mixer(sr, kLiveQuantum));
    h->evs_.reset(new (std::nothrow) SeqEvent[kRenderEvents]);
    h->step_.reset(new (std::nothrow) SeqStep());
    h->q_.reset(new (std::nothrow) float[(size_t)kLiveQuantum * 2]());
    h->padVars_.reset(new (std::nothrow) PadVars[kRenderPads]);
    if (!h->evs_ || !h->step_ || !h->q_ || !h->padVars_) return fail("out of memory");
    if (o.flags & THUNDER_LIVE_F_DUMP_STEPS) {
      h->dumpEvCap_ = (uint32_t)kRenderEvents + (uint32_t)dumpSteps * 64u;   // room for one worst-case step and `dumpSteps` ordinary ones
      h->dumpEvs_.reset(new (std::nothrow) SeqEvent[h->dumpEvCap_]);
      h->dumpHeads_.reset(new (std::nothrow) Spsc<LiveDumpHead, 1024>());
      if (!h->dumpEvs_ || !h->dumpHeads_) return fail("out of memory (step dump)");
    }
    for (int i = 0; i < 64; i++) h->pub_[i].store(0, std::memory_order_relaxed);
    h->refreshCtl();
  } catch (...) { return fail("out of memory"); }
  return h.release();
}

LiveHost::~LiveHost() {
  // The audio thread has stopped (live.h, thunder_live_destroy). A host left PLAYING needs no quantum here in P2: there is no MIDI scheduler whose note-offs would be owed (P4 adds that quantum).
  LiveCmd c; while (cmds_.pop(c)) if (c.type == LiveCmdType::Play) delete static_cast<PlayPlan*>(c.p);
  delete stalledPlay_; stalledPlay_ = nullptr;
  PlayPlan* pp; while (playBack_.pop(pp)) delete pp;
  delete padsHost_; padsHost_ = nullptr;   // frees every buffer, whatever the voices say; Players is destroyed after this body and does not dereference its pads
}

void LiveHost::refreshCtl() {
  ctlPadHeld_.store((uint64_t)padsHost_->bytesHeld() + stagedVarBytes_, std::memory_order_relaxed);
  ctlPadRetired_.store((uint64_t)padsHost_->bytesRetired(), std::memory_order_relaxed);
  ctlPadBuffers_.store((int32_t)padsHost_->buffers(), std::memory_order_relaxed);
  ctlPadRetiredN_.store((int32_t)padsHost_->retired(), std::memory_order_relaxed);
  ctlSessionBytes_.store(sess_ ? (uint64_t)sess_->bytes() : 0, std::memory_order_relaxed);
}

int LiveHost::collect() {
  int n = 0;
  n += (int)songSlot_.collect(); n += (int)busSlot_.collect(); n += (int)pdcSlot_.collect(); n += (int)masterSlot_.collect();
  n += (int)padsHost_->collect();
  PlayPlan* p; while (playBack_.pop(p)) { delete p; n++; playOut_--; }
  refreshCtl();
  return n;
}

// =========================================================================================================================================================================================================
// CONTROL: song
// =========================================================================================================================================================================================================
int LiveHost::songPublish(const uint8_t* song, size_t n, const uint8_t* vars, size_t varsN, int64_t rev, char* err, size_t errCap) {
  if (err && errCap) err[0] = 0;
  auto say = [&](const char* m, size_t off) { if (err && errCap) std::snprintf(err, errCap, "%s (byte %zu)", m ? m : "bad song", off); };
  if (!song || n == 0 || (varsN > 0 && !vars)) return THUNDER_LIVE_E_ARG;
  if (n > maxSongBytes_ || varsN > (size_t)THUNDER_LIVE_MAX_VARS_BYTES) return THUNDER_LIVE_E_LIMIT;   // judged BEFORE any parse allocates
  collect();
  std::shared_ptr<const Song> sp; SeqVars sv;
  try {
    Song* s = new (std::nothrow) Song();
    if (!s) return THUNDER_LIVE_E_MEMORY;
    std::unique_ptr<Song> own(s);
    JsonError je;
    if (!s->load(reinterpret_cast<const char*>(song), n, &je, /*pageS*/ true)) { say(je.msg, je.offset); return THUNDER_LIVE_E_SONG; }
    if (varsN > 0) {
      JsonDoc vd; JsonError ve;
      if (!vd.parse(reinterpret_cast<const char*>(vars), varsN, &ve) || !vd.root().isObj()) { say(ve.msg ? ve.msg : "vars is not an object", ve.offset); return THUNDER_LIVE_E_SONG; }
      sv.load(vd.root());
    }
    sp = std::shared_ptr<const Song>(own.release());
  } catch (...) { return THUNDER_LIVE_E_MEMORY; }

  if (controlPlaying_ && sess_) {
    // PLAYING: a SUCCESSOR snapshot of the play's session (SeqSession::publish, written out here only to set `rev` / `ord` between make() and the slot's publish). Rolled back when the ring is full.
    std::unique_ptr<SongSnap> snap;
    try { snap = sess_->make(sp, sv); } catch (...) { sess_->rollback(); return THUNDER_LIVE_E_MEMORY; }
    if (!snap) return THUNDER_LIVE_E_MEMORY;
    snap->rev = rev; snap->ord = songOrd_ + 1;
    LiveBusMsg* bm = new (std::nothrow) LiveBusMsg();
    if (!bm) { sess_->rollback(); return THUNDER_LIVE_E_MEMORY; }
    try { bm->plan = bus_plan(sp->chainPatch, sp->chainOn); } catch (...) { delete bm; sess_->rollback(); return THUNDER_LIVE_E_MEMORY; }
    if (!songSlot_.publish(snap.get())) { sess_->rollback(); delete bm; cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // the 17th waiting publish: nothing changed
    snap.release(); sess_->commit(); songOrd_++; songPublished_.store(songOrd_, std::memory_order_release);
    if (!busSlot_.publish(bm)) delete bm;   // (the plan only gives solo its chains-aware mask in this build: a full bus ring keeps the last plan)
    songCur_ = sp; varsCur_ = sv; songRev_ = rev;
    if (sess_->sessionFull() && !sessionFullSeen_) { sessionFullSeen_ = true; sessionFullFlag_.store(1, std::memory_order_relaxed); noteSessionFull_.store(true, std::memory_order_release); }
    refreshCtl();
    return THUNDER_LIVE_OK;
  }
  // IDLE: keep the Song for the next play; nothing is built for the audio side. The SONG record is posted at the next quantum (a SongAck command).
  songCur_ = sp; varsCur_ = sv; songRev_ = rev;
  LiveCmd c; c.type = LiveCmdType::SongAck; c.i64a = rev;
  if (!cmds_.push(c)) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // the publish itself is kept (live_host.h)
  return THUNDER_LIVE_OK;
}

// =========================================================================================================================================================================================================
// CONTROL: pads
// =========================================================================================================================================================================================================
// Error mapping of PadsHost's codes.
static int padErr(int rc) { return rc == PadsHost::kOk ? THUNDER_LIVE_OK : rc == PadsHost::kNoMem ? THUNDER_LIVE_E_MEMORY : rc == PadsHost::kFull ? THUNDER_LIVE_E_BUSY : THUNDER_LIVE_E_ARG; }

// The same samples, rate and channel count as the pad has now and no variations: PadsHost::setPad would change nothing (and allocate nothing), so the budget must not refuse it.
static bool samePad(const PadsHost& h, int pad, const float* l, const float* r, int frames, double rate) {
  const PadBuf* c = h.padBuf(pad);
  return c && c->frames == frames && c->rate == rate && c->stereo == (r != nullptr) && c->nVars == 0 && std::memcmp(c->L(), l, (size_t)frames * sizeof(float)) == 0 && (!r || std::memcmp(c->R(), r, (size_t)frames * sizeof(float)) == 0);
}

void LiveHost::dropStagedVars(int pad) {
  PadVars& pv = padVars_[(size_t)pad];
  for (const VarStage& v : pv.v) stagedVarBytes_ -= v.own.size() * sizeof(float);
  pv.v.clear(); pv.active = false;
}

int LiveHost::padSet(int pad, const float* l, const float* r, int64_t frames, double rate) {
  if (pad < 0 || pad >= kRenderPads || frames < 0) return THUNDER_LIVE_E_ARG;
  if (frames == 0) { dropStagedVars(pad); padsHost_->setPad(pad, nullptr, nullptr, 0, 1); padsDirty_ = true; refreshCtl(); return THUNDER_LIVE_OK; }   // removes the pad (its notes are silent)
  if (!l || !finite(rate) || rate < 1 || rate > 1e6) return THUNDER_LIVE_E_ARG;
  if (frames > THUNDER_LIVE_MAX_PAD_FRAMES) return THUNDER_LIVE_E_LIMIT;
  collect();
  const bool same = samePad(*padsHost_, pad, l, r, (int)frames, rate) && !padVars_[(size_t)pad].active;
  if (!same) {
    // THE BUDGET, judged before anything is allocated: what is held (current, in snapshots, retired and not yet freed, staged variations) plus what this call would add.
    const uint64_t add = (uint64_t)frames * (r ? 8u : 4u);
    if ((uint64_t)padsHost_->bytesHeld() + stagedVarBytes_ + add > budget_) return kBudgetErr;
  }
  int rc = padsHost_->setPad(pad, l, r, (int)frames, rate, nullptr, 0);
  if (rc == PadsHost::kOk && !same) { dropStagedVars(pad); padsDirty_ = true; }   // a pad whose audio changes loses its variations (staged ones too)
  refreshCtl();
  return padErr(rc);
}

int LiveHost::padTake(int pad, float* block, bool stereo, int64_t frames, double rate) {
  if (pad < 0 || pad >= kRenderPads || !block || frames < 1 || !finite(rate) || rate < 1 || rate > 1e6) return THUNDER_LIVE_E_ARG;
  if (frames > THUNDER_LIVE_MAX_PAD_FRAMES) return THUNDER_LIVE_E_LIMIT;
  collect();
  const uint64_t add = (uint64_t)frames * (stereo ? 8u : 4u);
  if ((uint64_t)padsHost_->bytesHeld() + stagedVarBytes_ + add > budget_) return kBudgetErr;   // before the pad is touched: the block stays the caller's
  const int rc = padsHost_->setPadTake(pad, block, stereo, (int)frames, rate);   // on any error the block is still the caller's
  if (rc == PadsHost::kOk) { dropStagedVars(pad); padsDirty_ = true; }
  refreshCtl();
  return padErr(rc);
}

int LiveHost::padKit(int pad, int choke, int syn, double rel) {
  if (pad < 0 || pad >= kRenderPads || choke < 0 || choke > 255 || (syn != 0 && syn != 1) || !finite(rel) || rel < 0 || rel > 60) return THUNDER_LIVE_E_ARG;
  VoiceKit k; k.choke = choke; k.syn = syn != 0; k.rel = rel;
  const int rc = padsHost_->setKit(pad, k); padsDirty_ = true; return padErr(rc);
}
int LiveHost::padRoute(int pad, int route) {
  if (pad < 0 || pad >= kRenderPads || route < 0 || route > 2) return THUNDER_LIVE_E_ARG;
  const int rc = padsHost_->setRoute(pad, static_cast<PadRoute>(route)); padsDirty_ = true; return padErr(rc);
}

int LiveHost::varSet(int ii, int kind, int num, int pad, const float* l, int64_t frames, double rate) {
  if (ii < 0 || ii > 63 || (kind != 'g' && kind != 'm') || num < 0 || num > 255 || pad < 0 || pad >= kRenderPads || frames < 0) return THUNDER_LIVE_E_ARG;
  const PadBuf* pb = padsHost_->padBuf(pad);
  if (!pb) return THUNDER_LIVE_E_ARG;                                   // the pad has no audio staged
  PadVars& pv = padVars_[(size_t)pad];
  try {
    if (!pv.active) {                                                   // seed the stage with the variations the pad has now (borrowed: the PadBuf stays alive until publish replaces it)
      pv.v.clear();
      for (int i = 0; i < pb->nVars; i++) { VarStage s; s.ii = pb->vars[i].ii; s.kind = pb->vars[i].kind; s.num = pb->vars[i].num; s.frames = pb->vars[i].frames; s.sr = pb->vars[i].sr; s.data = pb->vars[i].data; pv.v.push_back(std::move(s)); }
      pv.active = true;
    }
    size_t at = pv.v.size();
    for (size_t i = 0; i < pv.v.size(); i++) if (pv.v[i].ii == ii && pv.v[i].kind == kind && pv.v[i].num == num) { at = i; break; }
    if (frames == 0) {                                                  // removes
      if (at < pv.v.size()) { stagedVarBytes_ -= pv.v[at].own.size() * sizeof(float); pv.v.erase(pv.v.begin() + (std::ptrdiff_t)at); padsDirty_ = true; }
      refreshCtl(); return THUNDER_LIVE_OK;
    }
    if (!l || !finite(rate) || rate < 1 || rate > 1e6) return THUNDER_LIVE_E_ARG;
    if (frames > THUNDER_LIVE_MAX_PAD_FRAMES) return THUNDER_LIVE_E_LIMIT;
    size_t total = (size_t)pb->frames * (pb->stereo ? 2 : 1) * sizeof(float);
    for (size_t i = 0; i < pv.v.size(); i++) if (i != at) total += (size_t)pv.v[i].frames * sizeof(float);
    total += (size_t)frames * sizeof(float);
    if (total > (size_t)THUNDER_RENDER_MAX_PAD_BYTES || (pv.v.size() + (at == pv.v.size() ? 1 : 0)) > (size_t)THUNDER_RENDER_MAX_VARIATIONS) return THUNDER_LIVE_E_LIMIT;
    const size_t replaced = at < pv.v.size() ? pv.v[at].own.size() * sizeof(float) : 0;
    if ((uint64_t)padsHost_->bytesHeld() + stagedVarBytes_ - replaced + (uint64_t)frames * 4u > budget_) return kBudgetErr;   // the staged copy counts at once
    for (int64_t i = 0; i < frames; i++) if (!finite(l[i])) return THUNDER_LIVE_E_ARG;
    VarStage s; s.ii = ii; s.kind = (char)kind; s.num = num; s.frames = (int)frames; s.sr = rate; s.own.assign(l, l + frames); s.data = s.own.data();
    stagedVarBytes_ = stagedVarBytes_ - replaced + (size_t)frames * sizeof(float);
    if (at < pv.v.size()) pv.v[at] = std::move(s); else pv.v.push_back(std::move(s));
    for (VarStage& x : pv.v) if (!x.own.empty()) x.data = x.own.data();   // (a push_back may have moved the vectors' owners: the data pointers of moved vectors stay valid, refreshed anyway)
  } catch (...) { return THUNDER_LIVE_E_MEMORY; }
  padsDirty_ = true; refreshCtl();
  return THUNDER_LIVE_OK;
}

int LiveHost::padChop(int pad, double loopSecs) {
  if (pad < 0 || pad >= kRenderPads || !finite(loopSecs) || loopSecs < 0 || loopSecs > kSeqChopMaxSecs) return THUNDER_LIVE_E_ARG;
  if (!padsHost_->chop().setLoop(pad, loopSecs)) return THUNDER_LIVE_E_ARG;
  padsDirty_ = true; return THUNDER_LIVE_OK;
}
int LiveHost::padHits(int pad, int nSlices, const double* pts, int count) {
  if (pad < 0 || pad >= kRenderPads || nSlices < 2 || nSlices > kSeqChopMaxSlices || count < 0 || count > nSlices || (count > 0 && !pts)) return THUNDER_LIVE_E_ARG;
  for (int i = 0; i < count; i++) if (!finite(pts[i]) || pts[i] < 0 || pts[i] > kSeqChopMaxSecs) return THUNDER_LIVE_E_ARG;
  try { if (!padsHost_->chop().setHits(pad, nSlices, pts, count)) return THUNDER_LIVE_E_ARG; } catch (...) { return THUNDER_LIVE_E_MEMORY; }
  padsDirty_ = true; return THUNDER_LIVE_OK;
}

// SoundFonts and live-engine pads are NOT in P2 (the task's scope C): PadsHost::setSf and Players' SfPool / LivePool exist and are tested by their own units, but the host's font table, the budget's PCM accounting and the
// voice-JSON parse are not wired here. A host that never calls these behaves as the contract says.
int LiveHost::fontAdd(const uint8_t*, size_t) { return THUNDER_LIVE_E_UNSUPPORTED; }
int LiveHost::fontRemove(int) { return THUNDER_LIVE_E_UNSUPPORTED; }
int LiveHost::padSf(int, int, int, int, const thunder_render_sf_params*, const uint8_t*, size_t) { return THUNDER_LIVE_E_UNSUPPORTED; }
int LiveHost::padSfClear(int pad) { return pad < 0 || pad >= kRenderPads ? THUNDER_LIVE_E_ARG : 0; }   // (no preset can exist)
int LiveHost::padLive(int, int, const uint8_t*, size_t, const thunder_render_live_params*) { return THUNDER_LIVE_E_UNSUPPORTED; }
int LiveHost::padLiveClear(int pad) { return pad < 0 || pad >= kRenderPads ? THUNDER_LIVE_E_ARG : 0; }   // (no node can exist)

int LiveHost::padsPublish() {
  collect();
  if (!padsDirty_) return THUNDER_LIVE_OK;   // nothing staged: nothing published
  // Variations (ruling 6): one setPad per pad that has a staged list, from the pad's current samples and the staged variations.
  for (int p = 0; p < kRenderPads; p++) {
    PadVars& pv = padVars_[(size_t)p];
    if (!pv.active) continue;
    const PadBuf* pb = padsHost_->padBuf(p);
    if (!pb) { dropStagedVars(p); continue; }
    try {
      std::vector<PadVarIn> in; in.reserve(pv.v.size());
      for (const VarStage& s : pv.v) { PadVarIn x; x.ii = s.ii; x.kind = s.kind; x.num = s.num; x.mono = s.data; x.frames = s.frames; x.sr = s.sr; in.push_back(x); }
      const int rc = padsHost_->setPad(p, pb->L(), pb->R(), pb->frames, pb->rate, in.empty() ? nullptr : in.data(), (int)in.size());   // copies before it releases the old buffer
      if (rc == PadsHost::kNoMem) return THUNDER_LIVE_E_MEMORY;       // the staging is kept: retry
      dropStagedVars(p);
      if (rc != PadsHost::kOk) return THUNDER_LIVE_E_ARG;
    } catch (...) { return THUNDER_LIVE_E_MEMORY; }
  }
  const int rc = padsHost_->publish();
  if (rc == PadsHost::kOk) padsDirty_ = false;
  refreshCtl();
  return padErr(rc);   // kFull -> E_BUSY (collect and retry), kNoMem -> E_MEMORY (nothing published, the staging is kept)
}

// =========================================================================================================================================================================================================
// CONTROL: transport
// =========================================================================================================================================================================================================
int LiveHost::push(const LiveCmd& c) {
  if (!cmds_.push(c)) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // changed nothing, lost nothing: the caller owns the retry
  return THUNDER_LIVE_OK;
}

int LiveHost::play(const thunder_live_play_opts& o) {
  if (o.reserved0 != 0 || o.mode < THUNDER_LIVE_MODE_SONG || o.mode > THUNDER_LIVE_MODE_PHRASE || o.row < 0 || o.row > 255 || o.track < 0 || o.track >= THUNDER_LIVE_TRACKS || o.sect < -1 || o.phrase < -1
      || (o.has_loop && (o.loop_y0 < 0 || o.loop_y1 < o.loop_y0)) || o.spp < -1 || o.spp > 16383 || o.pdc_mode < 0 || o.pdc_mode > 2) return THUNDER_LIVE_E_ARG;
  if (!songCur_) return THUNDER_LIVE_E_STATE;                       // no song published
  collect();
  if (cmds_.space() == 0 || playOut_ >= 8) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // checked BEFORE anything is built: the session of a refused play is never made
  SeqOpts so;
  so.row = o.row; so.hasLoop = o.has_loop != 0; so.loopY0 = o.loop_y0; so.loopY1 = o.loop_y1;
  so.once = false; so.live = false; so.sim = false;                 // live.h / live_host.h: the page's play loops the song; an END command ends it
  so.hasOnly = o.has_only != 0; so.only = o.only; so.track = o.track; so.sect = o.sect; so.phrase = o.phrase;
  so.hasSeed = o.has_seed != 0; so.seed = o.seed; so.randomSeed = o.random_seed ? o.random_seed : 0x2545F491u;
  so.chop = nullptr;                                                // PadsHost::beginQuantum sets the table every quantum (pads_snap.h, precondition 3)
  const SeqMode mode = o.mode == THUNDER_LIVE_MODE_SECT ? SeqMode::Sect : o.mode == THUNDER_LIVE_MODE_PHRASE ? SeqMode::Phrase : SeqMode::Song;
  std::shared_ptr<SeqSession> sess; std::unique_ptr<SongSnap> snap; PlayPlan* plan = nullptr; LiveBusMsg* bm = nullptr;
  try {
    sess = SeqSession::create(mode, so);
    if (!sess) return THUNDER_LIVE_E_MEMORY;
    snap = sess->make(songCur_, varsCur_);                          // the first snapshot of a session is the starting Seq
    if (!snap) return THUNDER_LIVE_E_MEMORY;
    plan = new (std::nothrow) PlayPlan(); bm = new (std::nothrow) LiveBusMsg();
    if (!plan || !bm) { sess->rollback(); delete plan; delete bm; return THUNDER_LIVE_E_MEMORY; }
    bm->plan = bus_plan(songCur_->chainPatch, songCur_->chainOn);
  } catch (...) { if (sess) sess->rollback(); delete plan; delete bm; return THUNDER_LIVE_E_MEMORY; }
  plan->song = songCur_; plan->t0 = o.t0; plan->spp = o.spp; plan->pdcMode = o.pdc_mode == 0 ? PdcMode::Off : o.pdc_mode == 2 ? PdcMode::On : PdcMode::Auto; plan->sess = sess.get();
  snap->rev = songRev_; snap->ord = songOrd_ + 1;
  if (!songSlot_.publish(snap.get())) { sess->rollback(); delete plan; delete bm; cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // (the SeqSession built for it is discarded; a retry rebuilds it)
  snap.release(); sess->commit(); songOrd_++; songPublished_.store(songOrd_, std::memory_order_release);
  if (!busSlot_.publish(bm)) delete bm;
  LiveCmd c; c.type = LiveCmdType::Play; c.p = plan; c.i64a = o.t0; c.i0 = o.spp; c.i1 = o.pdc_mode;
  const bool pushed = cmds_.push(c); assert(pushed); (void)pushed;  // (space was checked above and only this thread pushes)
  playOut_++;
  sess_ = sess; controlPlaying_ = true; sessionFullSeen_ = false; sessionFullFlag_.store(0, std::memory_order_relaxed);
  refreshCtl();
  return THUNDER_LIVE_OK;
}

int LiveHost::stop(int64_t at) { LiveCmd c; c.type = LiveCmdType::Stop; c.at = at; const int rc = push(c); if (rc == 0) controlPlaying_ = false; return rc; }   // a later songPublish no longer reaches the sequencer
int LiveHost::hold(int64_t at) { LiveCmd c; c.type = LiveCmdType::Hold; c.at = at; return push(c); }
int LiveHost::loop(bool has, int y0, int y1, int64_t at) {
  if (y0 < 0 || y1 < 0 || y0 > y1) return THUNDER_LIVE_E_ARG;
  LiveCmd c; c.type = LiveCmdType::Loop; c.at = at; c.i0 = has ? 1 : 0; c.i1 = y0; c.i2 = y1; return push(c);
}
int LiveHost::bpm(double v, int64_t at) {
  if (!finite(v) || !(v > 0)) return THUNDER_LIVE_E_ARG;
  LiveCmd c; c.type = LiveCmdType::Bpm; c.at = at; c.x = v; return push(c);
}
int LiveHost::lead(double seconds, int64_t at) {
  if (!finite(seconds) || seconds < 0.05 || seconds > 2) return THUNDER_LIVE_E_ARG;
  LiveCmd c; c.type = LiveCmdType::Lead; c.at = at; c.x = seconds; return push(c);
}
int LiveHost::perf(int what, int index, double v, int64_t at) {
  if (what < THUNDER_LIVE_PERF_XF || what > THUNDER_LIVE_PERF_FILL || !finite(v)) return THUNDER_LIVE_E_ARG;
  if (what == THUNDER_LIVE_PERF_MACRO && (index < 0 || index >= kSeqMacros)) return THUNDER_LIVE_E_ARG;
  if (what == THUNDER_LIVE_PERF_FILL && v != 0 && v != 1) return THUNDER_LIVE_E_ARG;
  LiveCmd c; c.type = LiveCmdType::Perf; c.at = at; c.i0 = what; c.i1 = index; c.x = v; return push(c);
}
int LiveHost::stopTrack(int track, int64_t at) {
  if (track < 0 || track >= THUNDER_LIVE_TRACKS) return THUNDER_LIVE_E_ARG;
  LiveCmd c; c.type = LiveCmdType::StopTrack; c.at = at; c.i0 = track; return push(c);
}
int LiveHost::mixSet(int track, int key, double value, double ramp, int64_t at) {
  if (track < 0 || track > THUNDER_LIVE_MASTER || !finite(value) || value < 0 || value > 255 || !finite(ramp)) return THUNDER_LIVE_E_ARG;
  switch (key) {
    case THUNDER_LIVE_MIX_VOL: case THUNDER_LIVE_MIX_TRIM: case THUNDER_LIVE_MIX_PAN: break;
    case THUNDER_LIVE_MIX_MUTE: case THUNDER_LIVE_MIX_SOLO: if (track == THUNDER_LIVE_MASTER || (value != 0 && value != 1)) return THUNDER_LIVE_E_ARG; break;
    case THUNDER_LIVE_MIX_OUT: if (track != THUNDER_LIVE_MASTER) return THUNDER_LIVE_E_ARG; break;
    default: return THUNDER_LIVE_E_ARG;   // 5 (Send) is not a thing this mixer has
  }
  if (track == THUNDER_LIVE_MASTER && (key == THUNDER_LIVE_MIX_TRIM || key == THUNDER_LIVE_MIX_PAN)) return THUNDER_LIVE_E_ARG;   // the master has VOL and OUT only (Mixer::set refuses the rest)
  LiveCmd c; c.type = LiveCmdType::MixSet; c.at = at; c.i0 = track; c.i1 = key; c.x = value; c.y = ramp; return push(c);
}

// Not built in P2 (P3): the effects hosts, the master chain, PDC. Each answers as live.h says for a host made without the part (E_STATE) or for a part the build lacks (E_UNSUPPORTED).
int LiveHost::pdcSet(int mode) { if (mode < 0 || mode > 2) return THUNDER_LIVE_E_ARG; return THUNDER_LIVE_E_UNSUPPORTED; }
int LiveHost::fxPlan(int, const uint8_t*, size_t, double) { return THUNDER_LIVE_E_STATE; }
int LiveHost::fxCommit(int, int64_t) { return THUNDER_LIVE_E_STATE; }
int LiveHost::fxParam(int, int, const double*, int64_t) { return THUNDER_LIVE_E_STATE; }
int LiveHost::masterConfig(const uint8_t*, size_t, bool) { return THUNDER_LIVE_E_STATE; }
int LiveHost::midiLead(double) { return THUNDER_LIVE_E_STATE; }   // no F_MIDI in this unit
int LiveHost::meters(double rateHz) {
  if (!(flags_ & THUNDER_LIVE_F_METERS)) return THUNDER_LIVE_E_STATE;
  if (!finite(rateHz) || rateHz < 0 || rateHz > 30) return THUNDER_LIVE_E_ARG;
  return rateHz == 0 ? THUNDER_LIVE_OK : THUNDER_LIVE_E_UNSUPPORTED;   // a stub: F_METERS is honoured as OFF; the bank and the METERS records are P3
}

// =========================================================================================================================================================================================================
// CONTROL: events, stats, test
// =========================================================================================================================================================================================================
// poll(): the next whole records of the two rings in `seq` order (live.h, EVENTS). One consumer thread.
int LiveHost::poll(void* buf, int cap) {
  if (!buf || cap < 0) return THUNDER_LIVE_E_ARG;
  uint8_t* out = static_cast<uint8_t*>(buf); int w = 0;
  for (;;) {
    // peek both rings (consumer side: tail is ours; head acquires the producer's writes)
    const bool hc = evCrit_.tail.load(std::memory_order_relaxed) != evCrit_.head.load(std::memory_order_acquire);
    const bool hr = evRing_.tail.load(std::memory_order_relaxed) != evRing_.head.load(std::memory_order_acquire);
    if (!hc && !hr) break;
    const LiveEvSlot* sc = hc ? &evCrit_.buf[evCrit_.tail.load(std::memory_order_relaxed) & 63u] : nullptr;
    const LiveEvSlot* sr = hr ? &evRing_.buf[evRing_.tail.load(std::memory_order_relaxed) & (kLiveEvRing - 1)] : nullptr;
    uint32_t qc = 0, qr = 0;
    if (sc) std::memcpy(&qc, sc->data + offsetof(thunder_live_ev_head, seq), 4);
    if (sr) std::memcpy(&qr, sr->data + offsetof(thunder_live_ev_head, seq), 4);
    const bool takeCrit = sc && (!sr || (int32_t)(qc - qr) < 0);   // (a wrap-safe compare of two seq numbers)
    const LiveEvSlot* s = takeCrit ? sc : sr;
    if (w + (int)s->bytes > cap) { if (w == 0) return THUNDER_LIVE_E_ARG; break; }   // too small for the next record: it stays queued
    std::memcpy(out + w, s->data, s->bytes); w += s->bytes;
    if (takeCrit) { LiveEvSlot tmp; evCrit_.pop(tmp); } else { LiveEvSlot tmp; evRing_.pop(tmp); }
  }
  return w;
}

int LiveHost::dumpStep(char* json, int cap) {
  if (!dumpHeads_ || !json || cap < 0) return THUNDER_LIVE_E_STATE;
  Spsc<LiveDumpHead, 1024>& R = *dumpHeads_;
  const uint32_t t = R.tail.load(std::memory_order_relaxed);
  if (t == R.head.load(std::memory_order_acquire)) return 0;
  const LiveDumpHead& h = R.buf[t & 1023u];
  std::string o;
  try {
    o = "{\"t\":"; liveplain::num(o, h.t); o += ",\"bpm0\":"; liveplain::num(o, h.bpm0); o += ",\"tks\":"; liveplain::num(o, h.tks); o += ",\"evs\":[";
    const uint64_t start = h.validMask;   // (the head's `validMask` field carries the first event's index in the circular event array)
    for (int i = 0; i < h.n; i++) { if (i) o += ','; liveplain::event(o, dumpEvs_[(size_t)((start + (uint64_t)i) % dumpEvCap_)]); }
    o += "]}";
  } catch (...) { return THUNDER_LIVE_E_MEMORY; }
  if ((int)o.size() + 1 > cap) return -((int)o.size() + 1);   // the step stays
  std::memcpy(json, o.data(), o.size()); json[o.size()] = 0;
  const int len = (int)o.size();
  dumpTail_.store(h.validMask + (uint64_t)h.n, std::memory_order_release);   // the events are the audio side's to overwrite again
  LiveDumpHead tmp; R.pop(tmp);
  return len;
}

bool LiveHost::statsGet(thunder_live_stats* out) const {
  if (!out || out->size < sizeof(uint32_t)) return false;
  auto P = [&](int i) { return pub_[i].load(std::memory_order_relaxed); };
  thunder_live_stats s; std::memset(&s, 0, sizeof s);
  s.size = (uint32_t)sizeof s;
  s.state = state_.load(std::memory_order_acquire);
  s.frames = (int64_t)P(P_FRAMES); s.frame_origin = (int64_t)P(P_ORIGIN); s.quanta = P(P_QUANTA); s.calls = P(P_CALLS); s.steps = P(P_STEPS); s.events = P(P_EVENTS);
  s.seq_dropped = P(P_SEQDROP); s.runner_skips = P(P_SKIPS); s.cmds_applied = P(P_CMDS);
  s.pad_bytes_held = ctlPadHeld_.load(std::memory_order_relaxed); s.pad_bytes_retired = ctlPadRetired_.load(std::memory_order_relaxed); s.pad_budget = budget_;
  s.session_bytes = ctlSessionBytes_.load(std::memory_order_relaxed); s.dump_dropped = P(P_DUMPDROP);
  s.latency_frames = 0;   // no master chain, no PDC in this unit
  s.voices = (int32_t)P(P_VOICES); s.peak_voices = (int32_t)P(P_PEAKV);
  s.notes = (int32_t)P(P_NOTES); s.notes_played = (int32_t)P(P_NPLAYED); s.notes_late = (int32_t)P(P_NLATE); s.notes_no_pad = (int32_t)P(P_NNOPAD); s.notes_live = (int32_t)P(P_NLIVE); s.notes_sf = (int32_t)P(P_NSF);
  s.notes_var_missing = (int32_t)P(P_NVARMISS); s.notes_stretched = (int32_t)P(P_NSTRETCH); s.events_ignored = (int32_t)P(P_EVIGN);
  s.pad_buffers = ctlPadBuffers_.load(std::memory_order_relaxed); s.pad_retired = ctlPadRetiredN_.load(std::memory_order_relaxed);
  s.cmd_busy = cmdBusy_.load(std::memory_order_relaxed); s.cmd_late = (uint32_t)P(P_CMDLATE); s.evt_dropped = (uint32_t)P(P_EVTDROP); s.reanchors = (uint32_t)P(P_REANCH);
  { const uint64_t pub = songPublished_.load(std::memory_order_acquire), ad = P(P_SONGADOPT); s.song_pending = pub > ad ? (uint32_t)(pub - ad) : 0; }
  s.session_full = sessionFullFlag_.load(std::memory_order_relaxed);
  s.live_failed = (uint32_t)P(P_LIVEFAIL); s.live_late = (uint32_t)P(P_LIVELATE); s.thread_prepares = (uint32_t)P(P_PREP); s.evt_crit_dropped = (uint32_t)P(P_EVTCRIT);
  const uint32_t n = std::min<uint32_t>(out->size, (uint32_t)sizeof s);
  const uint32_t keep = out->size; std::memcpy(out, &s, n); out->size = keep;
  return true;
}

// =========================================================================================================================================================================================================
// AUDIO: process and the quantum
// =========================================================================================================================================================================================================
int LiveHost::process(float* l, float* r, int frames, int64_t frame0, int64_t wallNs) {
  // validation: the outputs are ALWAYS written (zeros on an error)
  auto zero = [&]() { if (frames > 0 && frames <= maxBlock_) { if (l) std::memset(l, 0, (size_t)frames * sizeof(float)); if (r && r != l) std::memset(r, 0, (size_t)frames * sizeof(float)); } };
  if (!l || !r || frames < 0 || frames > maxBlock_ || frame0 < 0 || (frames > 0 && l < r + frames && r < l + frames)) { zero(); return THUNDER_LIVE_E_ARG; }
  if (!pe_thread_ready()) { pe_prepare(); thread_prepares_++; }   // the one call that may allocate: this thread's Plaits / Elements workspace (live.h, create)
  calls_++; wallNs_ = wallNs;
  if (frames == 0) { publish(); return THUNDER_LIVE_OK; }        // the web's first handler: prepare only; no anchor, no quantum
  if (!anchored_) { anchored_ = true; origin_ = qFrame_ = frame0; qRead_ = kLiveQuantum; nextClockAt_ = nextPosAt_ = nextLateAt_ = frame0; }
  else if (frame0 != nextFrame_) reanchor(frame0);               // a call that does not continue the last one (live.h, TIME)
  nextFrame_ = frame0 + frames;
  int done = 0;
  while (done < frames) {
    if (qRead_ >= kLiveQuantum) { quantum(); qRead_ = 0; }
    const int n = std::min(frames - done, kLiveQuantum - qRead_);
    std::memcpy(l + done, q_.get() + qRead_, (size_t)n * sizeof(float));
    std::memcpy(r + done, q_.get() + kLiveQuantum + qRead_, (size_t)n * sizeof(float));
    done += n; qRead_ += n;
  }
  return THUNDER_LIVE_OK;
}

void LiveHost::reanchor(int64_t frame0) {
  const int64_t skipped = frame0 - nextFrame_;
  qRead_ = kLiveQuantum;                       // the buffered quantum is dropped
  qFrame_ = origin_ = frame0;                  // the grid restarts here
  reanchors_++;
  thunder_live_ev_error e; std::memset(&e, 0, sizeof e);
  e.h.type = THUNDER_LIVE_EV_ERROR; e.h.f = frame0; e.where = THUNDER_LIVE_WHERE_REANCHOR; e.code = 0; e.fatal = 0;
  e.reserved = (int32_t)std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, skipped));   // live.h gives the skipped-frames "value" no field of its own: carried in `reserved` (notes, departure 3)
  post(&e, sizeof e);
}

void LiveHost::quantum() {
  const int64_t qStart = qFrame_;
  const double t0 = (double)qStart / sr_;
  // 1. APPLY COMMANDS
  retryStalledPlay(qStart);
  applyCommands(qStart, qStart + kLiveQuantum);
  // 2. ADOPT: seq_adopt, then seqGone() if the Seq was dropped, then PadsHost::beginQuantum(players, runner's Seq) every quantum
  adopt(qStart);
  // 3. CLOCKS
  players_->setTime(t0, qStart); mix_->setTime(t0);
  // 4. MIDI BEGIN (P4)
  // 5. STEP
  const SeqPump pr = stepLoop(t0, qStart);
  // 6. MIDI END (P4)
  // 7. SONG OVER, and the ENDED timer
  songEnded(t0, qStart, pr);
  // 8. SOUND
  render(t0, qStart);
  // 9. AFTER THE BLOCK (the counters first: stats.frames is the frame the NEXT quantum starts at)
  qFrame_ += kLiveQuantum; quanta_++;
  afterBlock(qStart);
}

// Step 1. Pop the ring into the waiting list (ring order); apply, in ring order, what is due in this quantum [qStart, qEnd): `at` < 0, in the past, or inside the quantum. A waiting command waits for the quantum
// that CONTAINS its `at`. A 65th waiting command (list full) is applied at once instead and counted (stats.cmd_late). live_host.h says the list is "ordered by at"; it is kept in RING order instead, which is what
// live.h's step 1 says commands are applied in (a stop at 3000 sent before a mix_set at 2950 must not be reordered): notes, departure 2.
void LiveHost::applyCommands(int64_t qStart, int64_t qEnd) {
  // (a) the commands that were waiting (sent before everything in the ring now): apply the due ones in order, keep the rest in order
  int w = 0;
  for (int i = 0; i < nPend_; i++) {
    const LiveCmd p = pend_[i];
    if (p.at < 0 || p.at < qEnd) applyOne(p, qStart); else pend_[w++] = p;
  }
  nPend_ = w;
  // (b) the ring, in ring order: due now: applied; not due: waits; the list is full (64): applied at once instead and counted
  LiveCmd c;
  while (cmds_.pop(c)) {
    if (c.at < 0 || c.at < qEnd) { applyOne(c, qStart); continue; }
    if (nPend_ < kLiveCmdWait) { pend_[nPend_++] = c; continue; }
    cmdLate_++; applyOne(c, qStart);
  }
}

void LiveHost::dropSeq() {
  if (runner_.seq()) seqDropBase_ += runner_.seq()->dropped();
  runner_.stop();
}

void LiveHost::giveBack(PlayPlan* p) { const bool ok = playBack_.push(p); assert(ok); (void)ok; }   // room is guaranteed: play() allows 8 plans outstanding and the ring holds 8

void LiveHost::applyOne(const LiveCmd& c, int64_t qStart) {
  cmdsApplied_++;
  const int64_t atF = c.at < qStart ? qStart : c.at;   // never before this quantum's start
  const double at = (double)atF / sr_;
  switch (c.type) {
    case LiveCmdType::SongAck: postSong(c.i64a, qStart); ackValid_ = true; ackRev_ = c.i64a; break;
    case LiveCmdType::Play: applyPlay(static_cast<PlayPlan*>(c.p), qStart); break;
    case LiveCmdType::Stop: {
      if (stalledPlay_) { giveBack(stalledPlay_); stalledPlay_ = nullptr; }   // a play that never started is cancelled too
      const int prev = stateA_;
      dropSeq();
      players_->stopAll(at); mix_->songLanesReset(); mix_->clockStop(at);
      endStopAt_ = -1; stateA_ = THUNDER_LIVE_STATE_IDLE;
      if (prev != THUNDER_LIVE_STATE_IDLE) postState(qStart, THUNDER_LIVE_ST_STOPPED, 0);   // from IDLE: no record
      break; }
    case LiveCmdType::Hold: runner_.hold(); break;   // arms the flag only, whatever the state (also with no Seq)
    case LiveCmdType::Loop: if (runner_.seq()) runner_.seq()->setLoop(c.i0 != 0, c.i1, c.i2); break;
    case LiveCmdType::Bpm: if (runner_.seq()) runner_.seq()->setBpm(c.x); break;
    case LiveCmdType::Lead: lead_ = c.x; break;
    case LiveCmdType::Perf:
      if (runner_.seq()) { Seq* s = runner_.seq(); if (c.i0 == THUNDER_LIVE_PERF_XF) s->setXf(c.x); else if (c.i0 == THUNDER_LIVE_PERF_MACRO) s->setMac(c.i1, c.x); else s->setFill(c.x != 0); }
      break;
    case LiveCmdType::StopTrack: players_->stopTrack(c.i0, at); break;
    case LiveCmdType::MixSet: {
      MixSet m; m.tr = c.i0; m.k = static_cast<MixKey>(c.i1); m.i = c.i2; m.v = c.x; m.ramp = c.y; m.at = at;   // sample-accurate from `at` (a time before this quantum is its start)
      mix_->set(m);
      break; }
    case LiveCmdType::FxCommit: case LiveCmdType::Meters: break;   // P3
  }
}

// PLAY (live.h, thunder_live_play): adopt the play's snapshot (the runner starts on ITS Seq), setTime(t0), the mixer from the song at once (no ramp), the tempo seed, state PLAYING.
void LiveHost::applyPlay(PlayPlan* p, int64_t qStart) {
  if (stalledPlay_) { giveBack(stalledPlay_); stalledPlay_ = nullptr; }   // a newer play replaces one that never started
  if (!tryStartPlay(p, qStart)) stalledPlay_ = p;
}
void LiveHost::retryStalledPlay(int64_t qStart) { if (stalledPlay_ && tryStartPlay(stalledPlay_, qStart)) stalledPlay_ = nullptr; }

bool LiveHost::tryStartPlay(PlayPlan* p, int64_t qStart) {
  if (seq_adopt(songSlot_, runner_)) noteSongAdopted(qStart);
  const SongSnap* sn = songSlot_.get();
  if (!sn || sn->sess.get() != p->sess) return false;   // the adopt was held back (the return ring is full: the control side has not collected): try again next quantum; the old Seq is never started
  dropSeq();                                            // a Seq that was running is dropped (its voices are NOT cut)
  runner_.start(sn->seq.get());
  const double t0 = p->t0 >= 0 ? (double)p->t0 / sr_ : (double)(qStart + (int64_t)std::llround(kPlayDelaySec * sr_)) / sr_;
  runner_.seq()->setTime(t0);
  if (busSlot_.adopt()) mix_->setPlan(busSlot_.get()->plan);
  mix_->load(*sn->song); mix_->songLanesReset(); mix_->clockStop((double)qStart / sr_);
  players_->live().tempo((double)qStart / sr_, runner_.seq()->bpm());   // the tempo consumers are seeded: SEV_TEMPO is emitted only on a change (seq.cpp:995)
  endStopAt_ = -1; songOverDone_ = false; stateA_ = THUNDER_LIVE_STATE_PLAYING;
  postState(qStart, THUNDER_LIVE_ST_PLAYING, 0);
  giveBack(p);
  return true;
}

// ONE SONG record for the newest rev adopted in a quantum (live.h, song_publish); a play that adopts the snapshot an IDLE publish already acknowledged posts none.
void LiveHost::noteSongAdopted(int64_t qStart) {
  const SongSnap* sn = songSlot_.get();
  pub_[P_SONGADOPT].store(sn->ord, std::memory_order_relaxed);
  if (ackValid_ && ackRev_ == sn->rev) { ackValid_ = false; return; }
  ackValid_ = false; postSong(sn->rev, qStart);
}

// Step 2.
void LiveHost::adopt(int64_t qStart) {
  if (seq_adopt(songSlot_, runner_)) noteSongAdopted(qStart);   // the newest snapshot; the Seq that was running hands its state to it (takeOver)
  // seqGone() BEFORE beginQuantum when the Seq was dropped (stop, held, song ended and timed out): then a pads snapshot may be adopted with a null Seq (pads_snap.h)
  if (hadSeq_ && !runner_.seq()) { padsHost_->seqGone(); hadSeq_ = false; }
  const bool padsNew = padsHost_->beginQuantum(*players_, runner_.seq());   // EVERY quantum: the scan of the voices' epochs, the adopt, the chop table
  if (runner_.seq()) hadSeq_ = true;
  if (padsNew) { thunder_live_ev_pads e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_PADS; e.h.f = qStart; e.id = (int64_t)padsHost_->current()->id; post(&e, sizeof e); }
  if (busSlot_.adopt()) mix_->setPlan(busSlot_.get()->plan);   // never earlier than the song (it was published after it)
  if (noteSessionFull_.load(std::memory_order_acquire) && noteSessionFull_.exchange(false, std::memory_order_acq_rel)) {
    thunder_live_ev_error e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_ERROR; e.h.f = qStart; e.where = THUNDER_LIVE_WHERE_SESSION; post(&e, sizeof e);   // informational, once
  }
}

// SeqEvent routing: SongRenderer::dispatch (render.cpp:28) without the effects bank (P3): the mixer's events, the beat clock, then the Players.
void LiveHost::dispatch(const SeqEvent& e, const Seq& seq, const Song& song) {
  events_++;
  switch (e.type) {
    case SEV_TVL: case SEV_TVA: case SEV_TPN: case SEV_MVOL: case SEV_MOUT: mix_->event(e); return;
    case SEV_BEAT: mix_->event(e); return;
    case SEV_FXC: case SEV_FXP: return;   // (no effects bank in this unit)
    default: break;
  }
  if (!players_->dispatch(e, seq, song)) st_.eventsIgnored++;   // chc, eng, tempo, the MIDI events: no route (counted)
}

// Step 5: the runner (the catch-up skip, EXACT STOP, at most 3 steps), per step the dump copy, the ROWS / POS bookkeeping and then every event (render.cpp:106-108's order).
SeqPump LiveHost::stepLoop(double t0, int64_t qStart) {
  Seq* sq = runner_.seq();
  if (!sq) return SeqPump();
  const Song& song = *songSlot_.get()->song;
  SeqPump r = runner_.pump(t0, lead_, evs_.get(), kRenderEvents, step_.get(), [&](const SeqEvent* evs, int k, const SeqStep& st) {
    steps_++;
    if (dumpEvs_) dumpCopy(evs, k, st, qStart);
    notePos(st);
    for (int i = 0; i < k; i++) { dispatch(evs[i], *sq, song); players_->samplePeak(); }
  });
  if (r.held) {   // EXACT STOP: the runner dropped the Seq at a pattern edge
    seqDropBase_ += sq->dropped();
    stateA_ = THUNDER_LIVE_STATE_HELD;
    heldRecord(r.heldT, qStart);
  }
  return r;
}

// the played step's place for the ROWS record (every step) and the POS record (30 a second): row / step = the highest of the valid tracks (what SeqRunner::pump computes), cr that track's chain row
void LiveHost::notePos(const SeqStep& st) {
  uint8_t buf[sizeof(thunder_live_ev_rows) + 8 * kSeqTracks];
  thunder_live_ev_rows* rows = reinterpret_cast<thunder_live_ev_rows*>(buf);
  std::memset(rows, 0, sizeof *rows);
  int n = 0, row = -1, sp = -1, cr = 0;
  for (int t = 0; t < kSeqTracks; t++) {
    const SeqPos& p = st.pos[t]; if (!p.valid) continue;
    rows->valid |= (uint64_t)1 << t;
    thunder_live_ev_rowent ent; ent.row = (int16_t)p.row; ent.cr = (int16_t)p.cr; ent.step = (int16_t)p.step; ent.phrase = (int16_t)p.phrase;
    std::memcpy(buf + sizeof(thunder_live_ev_rows) + 8 * (size_t)n, &ent, 8); n++;
    if (p.row > row) { row = p.row; cr = p.cr; }
    if (p.step > sp) sp = p.step;
  }
  if (n == 0) return;
  rows->h.type = THUNDER_LIVE_EV_ROWS; rows->h.f = toFrame(st.t, sr_);
  post(rows, (int)(sizeof(thunder_live_ev_rows) + 8 * (size_t)n));
  posDirty_ = true; posRow_ = row; posStep_ = sp; posCr_ = cr; posF_ = rows->h.f;
}

void LiveHost::heldRecord(double heldT, int64_t) {
  uint8_t buf[sizeof(thunder_live_ev_rows) + 8 * kSeqTracks];
  thunder_live_ev_rows* rows = reinterpret_cast<thunder_live_ev_rows*>(buf);
  std::memset(rows, 0, sizeof *rows);
  const SeqPos* hp = runner_.heldPos(); int n = 0;
  for (int t = 0; t < kSeqTracks; t++) {
    const SeqPos& p = hp[t]; if (!p.valid) continue;
    rows->valid |= (uint64_t)1 << t;
    thunder_live_ev_rowent ent; ent.row = (int16_t)p.row; ent.cr = (int16_t)p.cr; ent.step = (int16_t)p.step; ent.phrase = (int16_t)p.phrase;
    std::memcpy(buf + sizeof(thunder_live_ev_rows) + 8 * (size_t)n, &ent, 8); n++;
  }
  rows->h.type = THUNDER_LIVE_EV_HELD; rows->h.f = toFrame(heldT, sr_);
  post(rows, (int)(sizeof(thunder_live_ev_rows) + 8 * (size_t)n));
}

// F_DUMP_STEPS: the events of the step into the circular event array (no allocation), its head into the head ring. A step that does not fit is refused and counted.
void LiveHost::dumpCopy(const SeqEvent* evs, int k, const SeqStep& st, int64_t qStart) {
  const uint64_t tail = dumpTail_.load(std::memory_order_acquire);
  if (dumpWr_ + (uint64_t)k - tail > dumpEvCap_ || dumpHeads_->head.load(std::memory_order_relaxed) - dumpHeads_->tail.load(std::memory_order_acquire) >= 1024u) { dumpDropped_++; return; }
  for (int i = 0; i < k; i++) dumpEvs_[(size_t)((dumpWr_ + (uint64_t)i) % dumpEvCap_)] = evs[i];
  LiveDumpHead h; h.frame = qStart; h.t = st.t; h.bpm0 = st.bpm0; h.tks = st.tks; h.n = k; h.validMask = dumpWr_;   // validMask: the index of the first event (live_host.h's field reused)
  const bool ok = dumpHeads_->push(h); assert(ok); (void)ok;
  dumpWr_ += (uint64_t)k;
}

// Step 7: the sequencer ran out (once per play): Players::songOver, state ENDED, the 0.3 s timer; and the timer's end: IDLE, STOPPED arg 1.
void LiveHost::songEnded(double t0, int64_t qStart, const SeqPump& pr) {
  if (pr.ended && !songOverDone_ && runner_.seq()) {
    songOverDone_ = true;
    const double endT = runner_.seq()->time();
    players_->songOver(endT);                      // C4: live voices still held let go at the song's end
    stateA_ = THUNDER_LIVE_STATE_ENDED; endStopAt_ = t0 + kEndStopSec;
    thunder_live_ev_state e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_STATE; e.h.f = toFrame(endT, sr_); e.state = THUNDER_LIVE_ST_ENDED; e.arg = 0;
    post(&e, sizeof e);
    return;                                        // (the timer is judged from the next quantum on)
  }
  if (stateA_ == THUNDER_LIVE_STATE_ENDED && endStopAt_ >= 0 && t0 >= endStopAt_) {
    dropSeq();                                     // no sequencer exists in IDLE (seqGone() follows at the next quantum's adopt)
    endStopAt_ = -1; stateA_ = THUNDER_LIVE_STATE_IDLE;
    postState(qStart, THUNDER_LIVE_ST_STOPPED, 1);   // the MIDI stop's place (P4)
  }
}

// Step 8: the voices into the tracks' inputs, then the mixer (render.cpp:112-117 without the tap and the meters).
void LiveHost::render(double t0, int64_t qStart) {
  players_->render(mix_->inputsL(), mix_->inputsR(), kLiveQuantum, t0, qStart);
  float* const qL = q_.get(); float* const qR = q_.get() + kLiveQuantum;
  mix_->render(qL, qR, kLiveQuantum, t0);
}

// Step 9: CLOCK (10 a second), POS (30 a second, while playing, when a step was played), LATE (4 a second, when a counter changed), the published numbers, the state.
void LiveHost::afterBlock(int64_t qStart) {
  if (qStart >= nextClockAt_) {
    thunder_live_ev_clock e; std::memset(&e, 0, sizeof e);
    e.h.type = THUNDER_LIVE_EV_CLOCK; e.h.f = qStart; e.wall_ns = wallNs_; e.sr = (int32_t)std::lround(sr_);
    e.out_lat = outLat_.load(std::memory_order_relaxed); e.under = (uint32_t)under_.load(std::memory_order_relaxed); e.evt_dropped = evtDropped_; e.late_cmds = cmdLate_; e.voices = players_->voices();
    post(&e, sizeof e); nextClockAt_ = qStart + (int64_t)std::llround(sr_ / 10);
  }
  if (posDirty_ && stateA_ == THUNDER_LIVE_STATE_PLAYING && qStart >= nextPosAt_) {
    thunder_live_ev_pos e; std::memset(&e, 0, sizeof e);
    e.h.type = THUNDER_LIVE_EV_POS; e.h.f = posF_; e.row = posRow_; e.step = posStep_; e.cr = posCr_; e.bpm10 = runner_.seq() ? (int32_t)std::lround(runner_.seq()->bpm() * 10) : 0;
    post(&e, sizeof e); nextPosAt_ = qStart + (int64_t)std::llround(sr_ / 30); posDirty_ = false;
  }
  const PlayerStats& ps = players_->stats();
  if (qStart >= nextLateAt_ && (lateLast_[0] != (uint32_t)ps.notesLate || lateLast_[1] != (uint32_t)runner_.skips())) {
    thunder_live_ev_late e; std::memset(&e, 0, sizeof e);
    e.h.type = THUNDER_LIVE_EV_LATE; e.h.f = qStart; e.notes_late = (uint32_t)ps.notesLate; e.runner_skips = (uint32_t)runner_.skips();
    post(&e, sizeof e); lateLast_[0] = (uint32_t)ps.notesLate; lateLast_[1] = (uint32_t)runner_.skips(); nextLateAt_ = qStart + (int64_t)std::llround(sr_ / 4);
  }
  publish();
  state_.store(stateA_, std::memory_order_release);
}

void LiveHost::publish() {
  const PlayerStats& ps = players_->stats();
  auto S = [&](int i, uint64_t v) { pub_[i].store(v, std::memory_order_relaxed); };
  S(P_FRAMES, (uint64_t)qFrame_); S(P_ORIGIN, (uint64_t)origin_); S(P_QUANTA, quanta_); S(P_CALLS, calls_); S(P_STEPS, steps_); S(P_EVENTS, events_);
  S(P_SEQDROP, std::max<uint64_t>(pub_[P_SEQDROP].load(std::memory_order_relaxed), seqDropBase_ + (runner_.seq() ? runner_.seq()->dropped() : 0)));
  S(P_SKIPS, runner_.skips()); S(P_CMDS, cmdsApplied_); S(P_DUMPDROP, dumpDropped_);
  S(P_VOICES, (uint64_t)players_->voices()); S(P_PEAKV, (uint64_t)ps.peakAll);
  S(P_NOTES, (uint64_t)ps.notes); S(P_NPLAYED, (uint64_t)ps.notesPlayed); S(P_NLATE, (uint64_t)ps.notesLate); S(P_NNOPAD, (uint64_t)ps.notesNoPad); S(P_NLIVE, (uint64_t)ps.notesLive); S(P_NSF, (uint64_t)ps.notesSf);
  S(P_NVARMISS, (uint64_t)ps.notesVarMissing); S(P_NSTRETCH, (uint64_t)ps.notesStretched); S(P_EVIGN, (uint64_t)st_.eventsIgnored);
  S(P_CMDLATE, cmdLate_); S(P_EVTDROP, evtDropped_); S(P_EVTCRIT, evtCritDropped_); S(P_REANCH, reanchors_); S(P_PREP, (uint64_t)thread_prepares_);
  const LiveStats ls = players_->live().stats(); S(P_LIVEFAIL, players_->live().failedNodes()); S(P_LIVELATE, ls.late);
}

// ---- records ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// post(): `rec` starts with a thunder_live_ev_head with `type` and `f` set; this fills `bytes` and `seq` (counted over ALL records, dropped ones too: a gap shows a loss) and pushes into the critical ring (STATE, HELD,
// SONG, PADS, ERROR) or the ordinary one. A full ring refuses the NEWEST record and counts it (live.h, EVENTS). Audio thread: a stack copy and a release store, nothing else.
void LiveHost::post(void* rec, int bytes) {
  thunder_live_ev_head* h = static_cast<thunder_live_ev_head*>(rec);
  h->bytes = (uint16_t)bytes; h->seq = ++evSeq_;
  const bool crit = h->type == THUNDER_LIVE_EV_STATE || h->type == THUNDER_LIVE_EV_HELD || h->type == THUNDER_LIVE_EV_SONG || h->type == THUNDER_LIVE_EV_PADS || h->type == THUNDER_LIVE_EV_ERROR;
  LiveEvSlot s; s.bytes = (uint16_t)bytes; std::memcpy(s.data, rec, (size_t)bytes);
  const bool ok = crit ? evCrit_.push(s) : evRing_.push(s);
  if (!ok) { evtDropped_++; if (crit) evtCritDropped_++; }
}
void LiveHost::postState(int64_t f, int state, int arg) {
  thunder_live_ev_state e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_STATE; e.h.f = f; e.state = state; e.arg = arg; post(&e, sizeof e);
}
void LiveHost::postSong(int64_t rev, int64_t f) {
  thunder_live_ev_song e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_SONG; e.h.f = f; e.rev = rev; post(&e, sizeof e);
}

}  // namespace thunder
