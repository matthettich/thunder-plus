// THE LIVE HOST (U37 P2): see live_host.h (the declaration, one comment per method) and include/thunder/live.h (the contract). This file is the core of it: life, song, pads, transport, the quantum, the events and the
// step dump, and (P3) the track and master effects, the master chain, PDC and the meters. NOT here (later units): MIDI (P4), the C API functions (P5), SoundFont and live-engine pads (answered E_UNSUPPORTED, see below).
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

#include "chains/cdiff.h"
#include "engine/layer_pe.h"
#include "live/dump_plain.h"
#include "midi/midi_spp.h"
#include "modfx/modfx.h"
#include "render/render.h"
#include "rt/num.h"

namespace thunder {

static_assert(sizeof(thunder_live_ev_head) == 16 && sizeof(thunder_live_ev_clock) == 48 && sizeof(thunder_live_ev_pos) == 32 && sizeof(thunder_live_ev_rows) == 24 && sizeof(thunder_live_ev_rowent) == 8
              && sizeof(thunder_live_ev_state) == 24 && sizeof(thunder_live_ev_song) == 24 && sizeof(thunder_live_ev_pads) == 24 && sizeof(thunder_live_ev_error) == 32 && sizeof(thunder_live_ev_late) == 40
              && sizeof(thunder_live_ev_meters) == 304, "the record layouts of live.h (the same on wasm32 and 64-bit)");
/* M3 */ static_assert(sizeof(thunder_live_ev_chlvl) == 40 && sizeof(thunder_live_ev_chlvl) % 8 == 0, "the M3 record layout of live.h");
/* M1 */ static_assert(sizeof(thunder_live_ev_fxlvl) == 56 && sizeof(thunder_live_ev_fxdyn_ent) == 16 && sizeof(thunder_live_ev_fxdyn) == 152 && sizeof(thunder_live_ev_fxdyn) <= THUNDER_LIVE_EV_MAX && sizeof(thunder_live_ev_fxlvl) % 8 == 0 && sizeof(thunder_live_ev_fxdyn) % 8 == 0, "the M1 record layouts of live.h (METERS-DESIGN 4.1)");

namespace {
// stats.* of the audio side, in pub_[]: the audio thread stores each once a quantum (relaxed), any thread loads (relaxed)
enum Pub { P_FRAMES, P_ORIGIN, P_QUANTA, P_CALLS, P_STEPS, P_EVENTS, P_SEQDROP, P_SKIPS, P_CMDS, P_DUMPDROP, P_VOICES, P_PEAKV, P_NOTES, P_NPLAYED, P_NLATE, P_NNOPAD, P_NLIVE, P_NSF, P_NVARMISS,
  P_NSTRETCH, P_EVIGN, P_CMDLATE, P_EVTDROP, P_EVTCRIT, P_REANCH, P_PREP, P_LIVEFAIL, P_LIVELATE, P_SONGADOPT, P_CLEARED, P_NOSEQ, P_LATENCY, P_FXREMAKE, P_FXFAIL, P_MIDIFAIL, P_MIDIHANG,
  P_CHBLOCKS, P_CHFAILED, P_CHEVENTS, P_CHDROPPED, P_CHBAD, P_CHNONODE, P_CHMISMATCH, P_CHNODES, P_CHSTALE, P_COUNT };   // C2: the chains host's audio-side counters
constexpr double kEndStopSec = 0.3;          // the web's frame() rule: the MIDI stop 0.3 s after the song's end (live.h, THE STATE MACHINE)
constexpr int64_t kMaxHostFrame = (int64_t)1 << 62;   // frame0, t0 and `at` above this are E_ARG: engine + offset arithmetic cannot overflow
constexpr double kPlayDelaySec = 0.08;       // t0 < 0: the next quantum's start + 0.08 s (live.h, thunder_live_play_opts::t0)
constexpr int kBudgetErr = THUNDER_LIVE_E_LIMIT;
inline bool finite(double x) { return std::isfinite(x); }
inline int64_t toFrame(double sec, double sr) { return (int64_t)std::llround(sec * sr); }
}  // namespace

PlayPlan::~PlayPlan() {
  for (int t = 0; t <= kSongNT; t++) { delete fx[t]; fx[t] = nullptr; }
  if (chains) { if (chainsHost) chainsHost->settle(chains); delete chains; chains = nullptr; }   // C2: settled first (committed: its nodes become the control view; never committed: abandoned, the view stays), then deleted (what commit retired goes with it)
}

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
  constexpr uint32_t kBuilt = THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_DUMP_STEPS | THUNDER_LIVE_F_PAGE_TIMING | THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER | THUNDER_LIVE_F_MIDI | THUNDER_LIVE_F_CHAINS;
  if (o.flags & ~kBuilt) return fail("unknown flag");

  std::unique_ptr<LiveHost> h(new (std::nothrow) LiveHost());
  if (!h) return fail("out of memory");
  try {
    h->sr_ = sr; h->maxBlock_ = maxBlock; h->flags_ = o.flags;
    h->budget_ = o.max_pad_bytes ? o.max_pad_bytes : THUNDER_LIVE_DEFAULT_PAD_BYTES;
    h->maxSongBytes_ = o.max_song_bytes ? o.max_song_bytes : (uint64_t)THUNDER_LIVE_MAX_SONG_BYTES;
    h->players_.reset(new Players(nullptr, sr, maxVoices));        // no pads until the first snapshot: PadsHost::create publishes an EMPTY one, the first quantum adopts it
    h->players_->setDropLate(true);                                // ruling D-4: a note more than 0.15 s late is not played (stats.notes_late)
    if (o.flags & THUNDER_LIVE_F_PAGE_TIMING) h->players_->pool().setPageTiming(true);
    // (set below, once the mixer exists: FX1: page timing also means the page player's beat clock, as render_api.cpp sets it)
    h->padsHost_ = PadsHost::create(*h->players_);                 // after Players: the board covers exactly its pools
    if (!h->padsHost_) return fail("out of memory (pads)");
    h->mix_.reset(new Mixer(sr, kLiveQuantum));
    if (o.flags & THUNDER_LIVE_F_PAGE_TIMING) h->mix_->setPageClock(true);
    if (o.flags & THUNDER_LIVE_F_CHAINS) {   // C2 R1: the chains host, live (its nodes never build a spare on the audio thread); a host that cannot be made fails the create. It starts NOT playing (R10).
      const uint64_t cb = o.chains_bytes > (uint64_t)SIZE_MAX ? (uint64_t)SIZE_MAX : o.chains_bytes;   // (0: the host's default 1.5 GiB)
      h->chains_.reset(new (std::nothrow) ChainsHost(sr, (size_t)cb, true));
      if (!h->chains_ || !h->chains_->valid()) return fail("out of memory (chains)");
      h->chains_->stop();
    }
    if (o.flags & THUNDER_LIVE_F_EFFECTS) h->fxBank_.reset(new FxBank(sr));          // (33 hosts; their hooks are set by the commits)
    if (o.flags & THUNDER_LIVE_F_MASTER) { h->mchain_.reset(new MasterChain(sr, kLiveQuantum)); h->setMasterHook(); }   // SongRenderer::setMasterChain: the hook is on from the start, the chain configured by the first play
    if (o.flags & THUNDER_LIVE_F_METERS) h->meterBank_.reset(new MeterBank(sr));
    if (o.flags & THUNDER_LIVE_F_MIDI) { char me[128] = {0}; h->midi_ = thunder_midi_create(sr, me, sizeof me); if (!h->midi_) return fail(me[0] ? me : "out of memory (midi)"); }   // the host OWNS it (live.h MIDI): freed in ~LiveHost
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
  // The audio thread has stopped (live.h, thunder_live_destroy). No MIDI quantum is run here: nothing could deliver its records (the handle is freed below, and the shell has freed its thunder_midiout first). The duty of a shell
  // that must not leave a hung note is thunder_live_stop, one more process call and a drain BEFORE destroy (verify-p4 finding 4: an earlier version ran one here, which nobody could ever see).
  // R14 (C2): every chains plan, wherever it is, is settled and deleted HERE, while chains_ lives (it is destroyed after this body, after the mixer: the mixer's hooks point into it). A plan in a queued / stalled / returned PlayPlan goes
  // with the PlayPlan (~PlayPlan settles it: never committed = abandoned; committed and returned = its nodes become the control view, and then the host frees them); one in a ChainsCommit command or in chBack_ is settled and deleted below.
  auto dropChains = [&](ChainsPlan* cp) { if (!cp) return; if (chains_) chains_->settle(cp); delete cp; };
  LiveCmd c; while (cmds_.pop(c)) { if (c.type == LiveCmdType::Play) delete static_cast<PlayPlan*>(c.p); else if (c.type == LiveCmdType::FxCommit) delete static_cast<FxPlan*>(c.p); else if (c.type == LiveCmdType::ChainsCommit) dropChains(static_cast<ChainsPlan*>(c.p)); }
  for (int i = 0; i < nPend_; i++) if (pend_[i].type == LiveCmdType::FxCommit) delete static_cast<FxPlan*>(pend_[i].p);
  delete stalledPlay_; stalledPlay_ = nullptr;
  for (int t = 0; t <= kSongNT; t++) { delete stagedFx_[t]; stagedFx_[t] = nullptr; }
  { FxPlan* fp; while (fxBack_.pop(fp)) delete fp; }
  PlayPlan* pp; while (playBack_.pop(pp)) delete pp;
  { ChainsPlan* cp; while (chBack_.pop(cp)) dropChains(cp); }
  delete padsHost_; padsHost_ = nullptr;   // frees every buffer, whatever the voices say; Players is destroyed after this body and does not dereference its pads
  if (midi_) { thunder_midi_free(midi_); midi_ = nullptr; }
}

void LiveHost::refreshCtl() {
  ctlPadHeld_.store((uint64_t)padsHost_->bytesHeld() + stagedVarBytes_, std::memory_order_relaxed);
  ctlPadRetired_.store((uint64_t)padsHost_->bytesRetired(), std::memory_order_relaxed);
  ctlPadBuffers_.store((int32_t)padsHost_->buffers(), std::memory_order_relaxed);
  ctlPadRetiredN_.store((int32_t)padsHost_->retired(), std::memory_order_relaxed);
  ctlSessionBytes_.store(sess_ ? (uint64_t)sess_->bytes() : 0, std::memory_order_relaxed);
  if (chains_) { const ChainsHostStats cs = chains_->ctlStats(); chRefused_.store((uint32_t)cs.refused, std::memory_order_relaxed); chOver_.store((uint32_t)cs.overBudget, std::memory_order_relaxed); chBusy_.store((uint32_t)cs.busyRefused, std::memory_order_relaxed); }   // (the host's own control-thread fields: ChainsHost::stats() is never called here)
}

int LiveHost::collect() {
  int n = 0;
  n += (int)songSlot_.collect(); n += (int)busSlot_.collect(); n += (int)pdcSlot_.collect(); n += (int)masterSlot_.collect();
  n += (int)padsHost_->collect();
  PlayPlan* p; while (playBack_.pop(p)) { delete p; n++; playOut_--; }
  { FxPlan* fp; while (fxBack_.pop(fp)) { delete fp; n++; fxOut_--; } }   // a committed plan: the effects it retired go with it, freed HERE (never on the audio thread)
  { ChainsPlan* cp; while (chBack_.pop(cp)) { if (chains_) chains_->settle(cp); delete cp; n++; } }   // C2 R7: settled only AFTER the pop (release on push, acquire on pop: the happens-before for committed_ and the retired nodes), then deleted: the swapped-out patches and removed nodes are freed here
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
  // P4: the scheduler's own copy of the song (its MIDI instruments: port, channel, CCs, echo, LFO), parsed there and adopted at the top of the next quantum's midi_begin. ORDER (verify-p4 finding 1): the post is the LAST thing that
  // can fail. Every host-side failure point (allocations, the command ring's room, the snapshot slot's room) is passed BEFORE it; what follows it (the pushes whose room was just checked, and this thread is their only producer)
  // cannot fail. A refusal by the scheduler (E_BUSY: six songs in flight or its ring full; E_MEMORY; E_SONG) undoes what the host made and returns with NOTHING changed on either side; a refusal by the host
  // never reaches the scheduler. So after a returned error the scheduler and the host hold the same song.
  auto postMidi = [&]() -> int {
    if (!midi_) return THUNDER_LIVE_OK;
    const int mr = thunder_midi_set_song(midi_, song, n);
    if (mr == THUNDER_MIDI_OK) return THUNDER_LIVE_OK;
    if (mr == THUNDER_MIDI_E_BUSY) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
    if (mr == THUNDER_MIDI_E_MEMORY) return THUNDER_LIVE_E_MEMORY;
    if (mr == THUNDER_MIDI_E_SONG) { say("the MIDI scheduler refused the song", 0); return THUNDER_LIVE_E_SONG; }
    return THUNDER_LIVE_E_INTERNAL;
  };

  // control's view of PLAYING follows the audio side (F6): a play not yet started / not yet collected (playOut_ > 0), or the published state PLAYING; HELD, ENDED and the timer's IDLE take the IDLE path (the song is kept for the next play)
  if (controlPlaying_ && sess_ && (playOut_ > 0 || state_.load(std::memory_order_acquire) == THUNDER_LIVE_STATE_PLAYING)) {
    // PLAYING: a SUCCESSOR snapshot of the play's session (SeqSession::publish, written out here only to set `rev` / `ord` between make() and the slot's publish). Rolled back when the ring is full.
    std::unique_ptr<SongSnap> snap;
    try { snap = sess_->make(sp, sv); } catch (...) { sess_->rollback(); return THUNDER_LIVE_E_MEMORY; }
    if (!snap) return THUNDER_LIVE_E_MEMORY;
    snap->rev = rev; snap->ord = songOrd_ + 1;
    LiveBusMsg* bm = new (std::nothrow) LiveBusMsg();
    if (!bm) { sess_->rollback(); return THUNDER_LIVE_E_MEMORY; }
    try { bm->plan = bus_plan(sp->chainPatch, sp->chainOn); } catch (...) { delete bm; sess_->rollback(); return THUNDER_LIVE_E_MEMORY; }
    if (songSlot_.room() == 0) { sess_->rollback(); delete bm; cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // the 17th waiting publish: nothing changed
    // C2 R3 / R6: THE CHAINS. The order of failure points: everything that allocates (above), then the chains plan (a plan in flight is E_BUSY: nothing changed), then the rings' room (the ChainsCommit's slot in cmds_; the edit ring's room was
    // checked with the decision), then postMidi(), then the pushes that cannot fail. Any failure after the plan was made ABANDONS it (settle uncommitted: the control view stays; delete frees what it made).
    ChainsWork cw;
    if (chains_) {
      const int cr = chainsDecide(sp, cw);
      if (cr != THUNDER_LIVE_OK) { sess_->rollback(); delete bm; refreshCtl(); return cr; }
      if (cw.plan && cmds_.space() == 0) { chainsAbandon(cw); sess_->rollback(); delete bm; cmdBusy_.fetch_add(1, std::memory_order_relaxed); refreshCtl(); return THUNDER_LIVE_E_BUSY; }
    }
    { const int mr = postMidi(); if (mr != THUNDER_LIVE_OK) { chainsAbandon(cw); sess_->rollback(); delete bm; refreshCtl(); return mr; } }   // the last thing that can fail
    { const bool pub = songSlot_.publish(snap.get()); assert(pub); (void)pub; }   // (room checked above; this thread is the only producer)
    snap.release(); sess_->commit(); songOrd_++; songPublished_.store(songOrd_, std::memory_order_release);
    if (chains_) chainsFinish(cw, true);   // R7: the ChainsCommit goes right AFTER the snapshot (at = NOW: it never waits); then the param records; chainsSong_ / chainsBus_ / chainsFeed_ move forward (the call is OK from here)
    if (!busSlot_.publish(bm)) delete bm;   // (the plan only gives solo its chains-aware mask in this build: a full bus ring keeps the last plan)
    songCur_ = sp; varsCur_ = sv; songRev_ = rev;
    if (sess_->sessionFull() && !sessionFullSeen_) { sessionFullSeen_ = true; sessionFullFlag_.store(1, std::memory_order_relaxed); noteSessionFull_.store(true, std::memory_order_release); }
    refreshCtl();
    return THUNDER_LIVE_OK;
  }
  // IDLE: keep the Song for the next play; nothing is built for the audio side. The SONG record is posted at the next quantum (a SongAck command). The ack is queued FIRST: on E_BUSY nothing has changed (F5).
  LiveCmd c; c.type = LiveCmdType::SongAck; c.i64a = rev;
  if (cmds_.space() == 0) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
  { const int mr = postMidi(); if (mr != THUNDER_LIVE_OK) return mr; }   // the last thing that can fail
  { const bool ok = cmds_.push(c); assert(ok); (void)ok; }
  songCur_ = sp; varsCur_ = sv; songRev_ = rev;
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
  if (c.type != LiveCmdType::Play && c.type != LiveCmdType::SongAck && c.at > kMaxHostFrame) return THUNDER_LIVE_E_ARG;   // live.h TIME: frames above 2^62
  if (!cmds_.push(c)) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // changed nothing, lost nothing: the caller owns the retry
  return THUNDER_LIVE_OK;
}

int LiveHost::play(const thunder_live_play_opts& o) {
  if (o.reserved0 != 0 || o.mode < THUNDER_LIVE_MODE_SONG || o.mode > THUNDER_LIVE_MODE_PHRASE || o.row < 0 || o.row > 255 || o.track < 0 || o.track >= THUNDER_LIVE_TRACKS || o.sect < -1 || o.phrase < -1
      || (o.has_loop && (o.loop_y0 < 0 || o.loop_y1 < o.loop_y0)) || o.spp < -1 || o.spp > 16383 || o.pdc_mode < 0 || o.pdc_mode > 2) return THUNDER_LIVE_E_ARG;
  if (o.t0 > kMaxHostFrame) return THUNDER_LIVE_E_ARG;
  // R2 (verify-283): test_once / test_sim are the goldens' options and belong to a test host: without F_DUMP_STEPS they are refused, so that a product can never loop-less play by a stray field
  if ((o.test_once != 0 || o.test_sim != 0) && !(flags_ & THUNDER_LIVE_F_DUMP_STEPS)) return THUNDER_LIVE_E_ARG;
  if (!songCur_) return THUNDER_LIVE_E_STATE;                       // no song published
  for (int t = 0; t <= kSongNT; t++) if (stagedFx_[t]) return THUNDER_LIVE_E_STATE;   // an effects plan staged by fx_plan and not committed (live.h: FxHost::plan reuses the objects of the previous plan, so plans commit in order)
  collect();
  const bool fx = (flags_ & THUNDER_LIVE_F_EFFECTS) != 0 && fxBank_;
  // (the play's 33 effects plans may have to be sent as plain commits if planning fails part way: room for them, and for their return, is checked BEFORE anything is made)
  if (cmds_.space() == 0 || (fx && (cmds_.space() < (uint32_t)kSongNT + 3 || fxOut_ + kSongNT + 1 > 64)) || playOut_ >= 8 || songSlot_.room() == 0) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // checked BEFORE anything is built: the session of a refused play is never made
  // P4: the MIDI Start's Song Position Pointer: the caller's, or (spp = -1) the one for `row` (midi_spp_for: it STEPS A SEQ, so it allocates: here, on the control side, before anything is made; 0 from the top, as the shells' sppFor)
  int spp = o.spp < 0 ? 0 : o.spp;
  if (midi_ && o.spp < 0 && o.row > 0) {
    int v = -1;
    try { v = midi_spp_for(*songCur_, varsCur_, o.row); } catch (...) { v = -1; }
    if (v < 0) return THUNDER_LIVE_E_MEMORY;
    spp = v;
  }
  SeqOpts so;
  so.row = o.row; so.hasLoop = o.has_loop != 0; so.loopY0 = o.loop_y0; so.loopY1 = o.loop_y1;
  so.once = o.test_once != 0; so.live = false; so.sim = o.test_sim != 0;   // live.h: the page's play loops the song (test_once / test_sim: TEST ONLY, the goldens' options)
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
  plan->song = songCur_; plan->t0 = o.t0; plan->spp = spp; plan->pdcMode = o.pdc_mode == 0 ? PdcMode::Off : o.pdc_mode == 2 ? PdcMode::On : PdcMode::Auto; plan->sess = sess.get();
  snap->rev = songRev_; snap->ord = songOrd_ + 1;
  if (mchain_) { plan->master = master_chain_from_song(songCur_->mst, songCur_->limiter); plan->hasMaster = true; }   // the song's own master chain (SongRenderer's caller: master_chain_from_song(song.mst, song.limiter))
  plan->cfgOrd = ++cfgOrd_;
  auto unwindFx = [&]() { for (int t = 0; t <= kSongNT; t++) if (plan->fx[t]) { LiveCmd fc; fc.type = LiveCmdType::FxCommit; fc.i0 = t; fc.p = plan->fx[t]; if (cmds_.push(fc)) { plan->fx[t] = nullptr; fxOut_++; } } };   // plans already made must reach their hosts (FxHost::plan's rule): sent as plain commits
  // C2 R3 / R6 / L2: THE CHAINS DECISION comes BEFORE the effects plans: a refusal here (E_BUSY: a plan in flight or no room in the edit ring; E_MEMORY) returns with nothing sent and nothing changed, while the effects plans, once made, MUST reach
  // their hosts (FxHost::plan's rule: unwindFx). A chains plan CAN be abandoned, so a failure of the effects planning below abandons it (chainsAbandon) beside unwindFx.
  ChainsWork cw;
  if (chains_) {
    const int cr = chainsDecide(songCur_, cw);
    if (cr != THUNDER_LIVE_OK) { sess->rollback(); delete plan; delete bm; refreshCtl(); return cr; }
  }
  // THE SONG'S EFFECTS, planned LAST (every plan made must be committed, in order: FxHost::plan's rule), master first and then the tracks 0..31 (FxBank::load's order: the MODFX modules draw their seeds from ONE counter).
  // A failure part way sends the plans already made as plain commits (they must reach their hosts), then the play is given up.
  if (fx) {
    bool bad = false;
    try {
      modfx::SeedCounter seeds;
      plan->fx[kSongNT] = fxBank_->host(kSongNT).planFromSong(songCur_->mfx, songCur_->bpm, 0, &seeds);
      ppNote(kSongNT, plan->fx[kSongNT]);   // PP
      for (int t = 0; t < kSongNT && !bad; t++) { plan->fx[t] = fxBank_->host(t).planFromSong(songCur_->tfx.at((size_t)t), songCur_->bpm, 0, &seeds); noteFxPlanned(t, plan->fx[t]); }
    } catch (...) { bad = true; }
    if (bad) { unwindFx(); chainsAbandon(cw); sess->rollback(); delete plan; delete bm; return THUNDER_LIVE_E_MEMORY; }
    try {   // PDC: from the plans just made (FxPlan::entry), not from the audio thread's chains
      PdcSlot all[kSongNT * kFxSlots]; int n = 0;
      for (int t = 0; t < kSongNT; t++) for (int i = 0; i < ctlPdcN_[t]; i++) all[n++] = ctlPdc_[t][i];
      plan->pdc = pdc_plan(chains_ && !cw.move ? chainsBus_ : bm->plan, all, n, sr_, plan->pdcMode);   // V-PDC: the bus plan the mixer WILL have: with a chains host it is the last commit's; a refused / over-budget plan (nothing moves) leaves it, so the PDC plan is made from that one
    } catch (...) { unwindFx(); chainsAbandon(cw); sess->rollback(); delete plan; delete bm; return THUNDER_LIVE_E_MEMORY; }   // (the plans are made: they go as commits, as above)
  }
  // C2 L2: the chains plan rides in the PlayPlan, attached only now (right before the Play command is pushed): the PlayPlan owns it from here (its destructor settles and deletes it on every path). The edit ring's room was checked with the decision; the Play command's slot at the top.
  if (chains_) { plan->chains = cw.plan; plan->chainsHost = chains_.get(); cw.plan = nullptr; }
  // ORDER (verify-283 F9): the Play command goes into the ring FIRST, the snapshot is published SECOND. A quantum that runs between the two finds a Play whose snapshot is not there yet and keeps it stalled
  // (tryStartPlay); the audio side adopts nothing else while a play is stalled (adopt()), so the running runner is never re-pointed at the unstarted Seq of a session whose Play has not been applied.
  // Room in both rings was checked above and only this thread fills them, so neither push can fail.
  LiveCmd c; c.type = LiveCmdType::Play; c.p = plan; c.i64a = o.t0; c.i0 = spp; c.i1 = o.pdc_mode;
  const bool pushed = cmds_.push(c); assert(pushed); (void)pushed;
  if (chains_) chainsFinish(cw, false);                              // C2: the param records, and chainsSong_ / chainsBus_ / chainsFeed_ forward: nothing below can fail
  std::unique_ptr<LiveBusMsg> bmOwn(bm);                            // owned until published: an exception between here and the publish (only the test seam can raise one) must not leak it (the PlayPlan is the ring's: ~LiveHost deletes un-popped commands)
  if (testSeam_) testSeam_(testSeamUser_);                         // TEST ONLY: a quantum can run here
  const bool published = songSlot_.publish(snap.get()); assert(published); (void)published;
  snap.release(); sess->commit(); songOrd_++; songPublished_.store(songOrd_, std::memory_order_release);
  if (busSlot_.publish(bmOwn.get())) bmOwn.release();   // (a full bus ring: bmOwn deletes it)
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

// P3: effects, master chain, PDC, meters (control side). The plans are made here (objects are made on the control thread: Fx::operator new is malloc), committed by the audio side at a quantum's top and come back through fxBack_.
void LiveHost::noteFxPlanned(int track, const FxPlan* p) {
  ppNote(track, p);   // PP (the master too: the next line leaves it out)
  if (track < 0 || track >= kSongNT) return;
  int n = 0;
  if (p) for (int i = 0; i < p->size(); i++) { const FxEntry& e = p->entry(i); const bool pump = fx_is_pump(e.t); ctlPdc_[track][n++] = PdcSlot{track, e.slot, e.on, pump && e.bi >= 0, pump ? e.lat : 0.0}; }   // FxHost::slotBypass / slotLatency, read from the entry
  ctlPdcN_[track] = n;
}
int LiveHost::pdcSet(int mode) {
  if (mode < 0 || mode > 2) return THUNDER_LIVE_E_ARG;
  if (!songCur_) return THUNDER_LIVE_E_STATE;
  collect();
  const PdcMode m = mode == 0 ? PdcMode::Off : mode == 2 ? PdcMode::On : PdcMode::Auto;
  LivePdcMsg* msg = new (std::nothrow) LivePdcMsg(); if (!msg) return THUNDER_LIVE_E_MEMORY;
  try {
    PdcSlot all[kSongNT * kFxSlots]; int n = 0;
    for (int t = 0; t < kSongNT; t++) for (int i = 0; i < ctlPdcN_[t]; i++) all[n++] = ctlPdc_[t][i];
    msg->plan = pdc_plan(bus_plan(songCur_->chainPatch, songCur_->chainOn), all, n, sr_, m);
  } catch (...) { delete msg; return THUNDER_LIVE_E_MEMORY; }
  msg->mode = m; msg->ord = ++cfgOrd_;
  if (!pdcSlot_.publish(msg)) { delete msg; cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
  return THUNDER_LIVE_OK;
}
int LiveHost::fxPlan(int track, const uint8_t* json, size_t n, double bpm) {
  if (!(flags_ & THUNDER_LIVE_F_EFFECTS) || !fxBank_) return THUNDER_LIVE_E_STATE;
  if (track < 0 || track > THUNDER_LIVE_MASTER || !json || n == 0 || !finite(bpm) || bpm <= 0) return THUNDER_LIVE_E_ARG;
  if (stagedFx_[track]) return THUNDER_LIVE_E_STATE;   // at most one staged plan per track
  collect();
  FxPlan* pl = nullptr;
  try {
    JsonDoc doc; JsonError je;
    if (!doc.parse(reinterpret_cast<const char*>(json), n, &je) || !doc.root().isArr()) return THUNDER_LIVE_E_ARG;
    pl = fxBank_->host(track).planFromSong(doc.root(), bpm, 0, nullptr);   // (the entries are copied: the document is not needed after)
  } catch (...) { return THUNDER_LIVE_E_MEMORY; }
  if (!pl) return THUNDER_LIVE_E_UNSUPPORTED;          // no effects library in this build
  stagedFx_[track] = pl; noteFxPlanned(track, pl);
  return THUNDER_LIVE_OK;
}
int LiveHost::fxCommit(int track, int64_t at) {
  if (!(flags_ & THUNDER_LIVE_F_EFFECTS) || !fxBank_) return THUNDER_LIVE_E_STATE;
  if (track < 0 || track > THUNDER_LIVE_MASTER) return THUNDER_LIVE_E_ARG;
  if (!stagedFx_[track]) return THUNDER_LIVE_E_STATE;
  collect();
  if (fxOut_ >= 64) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // the return ring holds 64: the plan stays staged
  LiveCmd c; c.type = LiveCmdType::FxCommit; c.at = at; c.i0 = track; c.p = stagedFx_[track];
  const int rc = push(c); if (rc) return rc;           // E_BUSY / E_ARG: the plan stays staged
  stagedFx_[track] = nullptr; fxOut_++;
  return THUNDER_LIVE_OK;
}
int LiveHost::fxParam(int track, int slot, const double* p12, int64_t at) {
  if (!(flags_ & THUNDER_LIVE_F_EFFECTS) || !fxBank_) return THUNDER_LIVE_E_STATE;
  if (track < 0 || track > THUNDER_LIVE_MASTER || slot < 0 || slot >= kFxSlots || !p12 || at > kMaxHostFrame) return THUNDER_LIVE_E_ARG;
  LiveFxParam fp; fp.track = track; fp.slot = slot; fp.at = at;
  for (int i = 0; i < 12; i++) { if (!finite(p12[i])) return THUNDER_LIVE_E_ARG; fp.p[i] = p12[i]; }
  if (!fxParams_.push(fp)) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
  return THUNDER_LIVE_OK;
}
// PP: thunder_live_fx_values (control side).
void LiveHost::ppNote(int track, const FxPlan* p) {
  if (track < 0 || track > kSongNT) return;
  for (int i = 0; i < kFxSlots; i++) ppKind_[track][i] = PpSlot();
  if (p) for (int i = 0; i < p->size(); i++) {
    const FxEntry& e = p->entry(i); if (e.slot < 0 || e.slot >= kFxSlots) continue;
    if (fx_is_pump(e.t)) { ppKind_[track][e.slot].kind = THUNDER_LIVE_FXV_PUMP; ppKind_[track][e.slot].n = (int16_t)e.npv;
      PpSlot& s = ppKind_[track][e.slot]; s.locked = fx_pump_live_locked(e.t - kPumpT0, e.pv, e.npv);
      for (int j = 0; j < e.npv && s.nfix < 4; j++) if (fx_pump_live_fixed(e.t - kPumpT0, j)) { s.fixI[s.nfix] = (int16_t)j; s.fixV[s.nfix] = e.pv[j]; s.nfix++; } }
    else if (fx_is_mod(e.t)) { ppKind_[track][e.slot].kind = THUNDER_LIVE_FXV_MOD; ppKind_[track][e.slot].n = (int16_t)e.nmp; }
  }
}
int LiveHost::fxValues(int track, int slot, int kind, const double* v, int n, int64_t at) {
  if (!(flags_ & THUNDER_LIVE_F_EFFECTS) || !fxBank_) return THUNDER_LIVE_E_STATE;
  if (track < 0 || track > THUNDER_LIVE_MASTER || slot < 0 || slot >= kFxSlots || !v || at > kMaxHostFrame) return THUNDER_LIVE_E_ARG;
  if (kind != THUNDER_LIVE_FXV_PUMP && kind != THUNDER_LIVE_FXV_MOD) return THUNDER_LIVE_E_ARG;
  const PpSlot& k = ppKind_[track][slot];
  if (k.kind != kind || n <= 0 || n != k.n) return THUNDER_LIVE_E_ARG;   // not a hosted slot of that kind in the last planned chain, or not the planned count
  if (k.locked) return THUNDER_LIVE_E_ARG;   // rides in the song (fx_pump_live_locked)
  for (int j = 0; j < k.nfix; j++) if ((float)v[k.fixI[j]] != k.fixV[j]) return THUNDER_LIVE_E_ARG;   // a value that rides in the song, changed (fx_pump_live_fixed)
  LiveFxValues r; r.track = track; r.slot = slot; r.kind = kind; r.n = n; r.at = at;
  for (int i = 0; i < n; i++) {
    if (!finite(v[i])) return THUNDER_LIVE_E_ARG;
    if (kind == THUNDER_LIVE_FXV_PUMP) { const float f = (float)v[i]; if (!std::isfinite(f)) return THUNDER_LIVE_E_ARG; r.pv[i] = f; } else r.mp[i] = v[i];
  }
  if (!fxValues_.push(r)) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
  return THUNDER_LIVE_OK;
}
int LiveHost::masterConfig(const uint8_t* mst, size_t n, bool safety) {
  if (!(flags_ & THUNDER_LIVE_F_MASTER) || !mchain_) return THUNDER_LIVE_E_STATE;
  collect();
  LiveMasterMsg* m = new (std::nothrow) LiveMasterMsg(); if (!m) return THUNDER_LIVE_E_MEMORY;
  try {
    if (!mst || n == 0) m->cfg = master_chain_fresh(safety);
    else { JsonDoc doc; JsonError je; if (!doc.parse(reinterpret_cast<const char*>(mst), n, &je)) { delete m; return THUNDER_LIVE_E_ARG; } m->cfg = master_chain_from_song(doc.root(), safety); }   // (a document that is not an object gives freshMst, as the page's normMst)
  } catch (...) { delete m; return THUNDER_LIVE_E_MEMORY; }
  m->ord = ++cfgOrd_;
  if (!masterSlot_.publish(m)) { delete m; cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
  return THUNDER_LIVE_OK;
}
// =========================================================================================================================================================================================================
// CONTROL: chains (C2)
// =========================================================================================================================================================================================================
// R3: what a play / a publish has to do, against chainsSong_ (the song whose patch and CHN flags the audio side has, or will have once what is in flight lands), chainsBus_ and chainsFeed_:
//   (a) no nodes ever planned, chains_feed(S) == 0, busSame ....... nothing            (b) Same, busSame ....... nothing
//   (c) Params, busSame ............................................. the param records (no plan)
//   (d) Same / Params, !busSame ..................................... a KEEP-PATCH plan (the nodes keep their patch and state; fresh ones load S's) + the param records
//   (e) Structural (or the first plan) ................................ a full plan (existing nodes: preparePatch + swap)
// busSame = bus_plan(S) equals chainsBus_ && chains_feed(S) == chainsFeed_ && the CHN flags equal. This call changes NOTHING of the host's own state: chainsFinish() moves chainsSong_ and the rest forward once the call can no longer fail.
// A param record that does not fit its fixed fields (id 47 / name 31 bytes + NUL), or a diff of more records than the ring holds, is a Structural change (a full plan carries every value).
// A plan the host refuses whole (the engine refused the patch, over budget) comes back noop: NOT an error: the call succeeds, nothing moves, nothing is sent (the song plays with the chains it had; the next call tries again).
// ONE plan in flight (R5): plan() returns null with busyRefused moved: E_BUSY, cmd_busy + 1, nothing changed. Null otherwise: E_MEMORY. On any non-zero return `w` owns nothing.
int LiveHost::chainsDecide(const std::shared_ptr<const Song>& S, ChainsWork& w) {
  try {
    const Song& s = *S;
    w.song = S; w.bus = bus_plan(s.chainPatch, s.chainOn); w.feed = chains_feed(s.chainPatch, s.chainOn);
    bool onSame = true; if (chainsPlanned_) for (int t = 0; t < kSongNT; t++) if ((chainsSong_ ? chainsSong_->chainOn[t] : false) != s.chainOn[t]) { onSame = false; break; }   // (no node was ever planned: the CHN flags matter only through chains_feed / the bus plan, compared below; so a song with no chains never makes a plan, L4b)
    // V4: a song whose bpm / lpb / ticks differ from the one the nodes were planned for is NOT "same" while nodes exist: a (keep-patch) plan carries the transport (newSong_), as the page's chainsTempo does on a tempo / TICKS / LPB edit
    const bool trSame = !chainsPlanned_ || (chainsSong_ && chainsSong_->bpm == s.bpm && chainsSong_->lpb == s.lpb && chainsSong_->ticks == s.ticks);
    const bool busSame = onSame && trSame && w.feed == chainsFeed_ && bus_plan_equal(w.bus, chainsBus_);
    ChainsDiff d = chains_diff(chainsSong_ ? chainsSong_->chainPatch : Json(), s.chainPatch);
    if (d.kind != ChainsDiffKind::Structural && chEditAll_) d.kind = ChainsDiffKind::Structural;   // V2: more than 256 distinct edited params since the last move: a full plan carries every value
    std::vector<LiveChainParam> recs;
    if (d.kind != ChainsDiffKind::Structural) {
      // V2: every (id, name) chain_edit sent since chainsSong_ last moved is put back to the NEW song's value unless the diff already holds it (the shell may publish a song that does not carry the edit: the audio side must end up with the song's values)
      if (!chEdited_.empty()) {
        const Json mods = s.chainPatch.arr("modules");
        for (const auto& pr : chEdited_) {
          bool have = false; for (const ChainsParamOp& op : d.params) if (op.id == pr.first && op.name == pr.second) { have = true; break; }
          if (have) continue;
          for (size_t k = 0; k < mods.size(); k++) {
            const Json m = mods.at(k); if (!m.isObj() || m.str("id") != pr.first) continue;
            const Json v = m.get("params").get(pr.second);
            if (v.isNum() && std::isfinite(v.asNum())) { ChainsParamOp op; op.id = pr.first; op.name = pr.second; op.v = v.asNum(); d.params.push_back(std::move(op)); }
            break;
          }
        }
        if (!d.params.empty()) d.kind = ChainsDiffKind::Params;
      }
      if (d.kind == ChainsDiffKind::Params) {
        if (d.params.size() > kLiveChainRing) d.kind = ChainsDiffKind::Structural;
        else for (const ChainsParamOp& op : d.params) {
          if (op.id.size() >= sizeof(LiveChainParam::id) || op.name.size() >= sizeof(LiveChainParam::name) || !std::isfinite(op.v)) { d.kind = ChainsDiffKind::Structural; break; }
          LiveChainParam r; std::memset(&r, 0, sizeof r); std::memcpy(r.id, op.id.data(), op.id.size()); std::memcpy(r.name, op.name.data(), op.name.size()); r.v = op.v; recs.push_back(r);
        }
      }
    }
    if (d.kind == ChainsDiffKind::Structural) recs.clear();
    w.move = true;
    if ((!chainsPlanned_ && w.feed == 0 && busSame) || (d.kind == ChainsDiffKind::Same && busSame)) return THUNDER_LIVE_OK;                        // (a), (b)
    if (recs.size() > chainParams_.space()) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); w.move = false; return THUNDER_LIVE_E_BUSY; }   // the edit ring has no room (nothing made yet)
    if (d.kind == ChainsDiffKind::Params && busSame) { w.params = std::move(recs); return THUNDER_LIVE_OK; }                                      // (c)
    ChainsSongTransport tp; tp.tempo = s.bpm; tp.lpb = s.lpb ? s.lpb : 4; tp.ticks = s.ticks ? s.ticks : 6;   // pass(): S.bpm, S.lpb || 4, S.ticks || 6
    const uint64_t busy0 = chains_->ctlStats().busyRefused;
    ChainsPlan* p = chains_->plan(s.chainPatch, s.chainOn, 0xFFFFFFFFu, tp, d.kind != ChainsDiffKind::Structural);   // (d) keep-patch / (e) full; every track is heard (R2)
    w.move = false;
    if (!p) {
      if (chains_->ctlStats().busyRefused != busy0) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }   // a plan is still in flight: the shell retries after the next callback
      return THUNDER_LIVE_E_MEMORY;
    }
    if (p->noop()) { delete p; return THUNDER_LIVE_OK; }   // refused whole (counted by the host): the call succeeds, nothing moves; a noop plan needs no settle
    w.plan = p; w.hasPlan = true; w.full = p->full(); w.nodeMask = p->nodeMask(); w.params = std::move(recs); w.move = true;
    return THUNDER_LIVE_OK;
  } catch (...) { chainsAbandon(w); return THUNDER_LIVE_E_MEMORY; }
}
void LiveHost::chainsAbandon(ChainsWork& w) {
  if (w.plan) { chains_->settle(w.plan); delete w.plan; w.plan = nullptr; }   // uncommitted: the control view stays; deleting frees what the plan made
  w.hasPlan = false; w.params.clear(); w.move = false;
}
// Cannot fail: the command ring's slot and the edit ring's room were checked, and this thread is the only producer of both.
void LiveHost::chainsFinish(ChainsWork& w, bool asCommand) {
  if (w.plan && asCommand) { LiveCmd c; c.type = LiveCmdType::ChainsCommit; c.at = THUNDER_LIVE_NOW; c.p = w.plan; const bool ok = cmds_.push(c); assert(ok); (void)ok; w.plan = nullptr; }
  if (w.hasPlan) { chPlans_.fetch_add(1, std::memory_order_relaxed); if (w.nodeMask) chainsPlanned_ = true; if (w.full) chFullSeq_++; }   // V3a: counted BEFORE the records below are stamped
  for (LiveChainParam r : w.params) { r.gen = chFullSeq_; const bool ok = chainParams_.push(r); assert(ok); (void)ok; }
  if (w.move) { chainsSong_ = w.song; chainsBus_ = w.bus; chainsFeed_ = w.feed; chEdited_.clear(); chEditAll_ = false; }   // V2: the edits are now part of what the song says (resent by the decision that moved)
  w.params.clear(); refreshCtl();
}

int LiveHost::chainEdit(const uint8_t* ops, size_t n, int64_t at) {
  if (!chains_ || !chainsSong_) return THUNDER_LIVE_E_STATE;
  if (at != THUNDER_LIVE_NOW) return THUNDER_LIVE_E_ARG;
  if (n > (size_t)1 << 20) return THUNDER_LIVE_E_LIMIT;
  if (!ops) return THUNDER_LIVE_E_ARG;   // (n == 0 is not a JSON array either)
  std::vector<LiveChainParam> recs;
  try {
    JsonDoc doc; JsonError je;
    if (!doc.parse(reinterpret_cast<const char*>(ops), n, &je) || !doc.root().isArr()) return THUNDER_LIVE_E_ARG;
    const Json arr = doc.root(), mods = chainsSong_->chainPatch.arr("modules");
    for (size_t i = 0; i < arr.size(); i++) {
      const Json op = arr.at(i);
      if (!op.isObj() || op.str("op") != "param" || !op.get("id").isStr() || !op.get("name").isStr() || !op.get("value").isNum() || !std::isfinite(op.get("value").asNum())) return THUNDER_LIVE_E_ARG;
      const std::string_view id = op.str("id"), name = op.str("name");
      if (id.size() >= sizeof(LiveChainParam::id) || name.size() >= sizeof(LiveChainParam::name)) return THUNDER_LIVE_E_LIMIT;   // not representable in a record
      bool found = false;   // the id must be a module of the playing patch, and not one whose parameters move the bus plan or the sources
      for (size_t k = 0; k < mods.size() && !found; k++) {
        const Json m = mods.at(k); if (!m.isObj() || m.str("id") != id) continue;
        const std::string_view ty = m.str("type");
        if (ty == "trackIn" || ty == "chainOut" || ty == "scin" || ty == "scout") return THUNDER_LIVE_E_ARG;
        found = true;
      }
      if (!found) return THUNDER_LIVE_E_ARG;
      LiveChainParam r; std::memset(&r, 0, sizeof r); std::memcpy(r.id, id.data(), id.size()); std::memcpy(r.name, name.data(), name.size()); r.v = op.get("value").asNum(); recs.push_back(r);
    }
  } catch (...) { return THUNDER_LIVE_E_MEMORY; }
  if (recs.size() > kLiveChainRing) return THUNDER_LIVE_E_LIMIT;   // more than the ring can ever hold
  if (recs.size() > chainParams_.space()) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
  try {   // V2: remembered (the room for the vector is made BEFORE anything is pushed: a failure here changes nothing)
    std::vector<std::pair<std::string, std::string>> add;
    for (const LiveChainParam& r : recs) {
      bool have = false; for (const auto& pr : chEdited_) if (pr.first == r.id && pr.second == r.name) { have = true; break; }
      for (const auto& pr : add) if (pr.first == r.id && pr.second == r.name) { have = true; break; }
      if (!have) add.emplace_back(std::string(r.id), std::string(r.name));
    }
    if (!chEditAll_ && chEdited_.size() + add.size() > 256) chEditAll_ = true;
    else if (!chEditAll_) { chEdited_.reserve(chEdited_.size() + add.size()); for (auto& pr : add) chEdited_.push_back(std::move(pr)); }
  } catch (...) { return THUNDER_LIVE_E_MEMORY; }
  for (LiveChainParam r : recs) { r.gen = chFullSeq_; const bool ok = chainParams_.push(r); assert(ok); (void)ok; }
  return THUNDER_LIVE_OK;
}
int LiveHost::midiLead(double seconds) {
  if (!midi_) return THUNDER_LIVE_E_STATE;
  if (!finite(seconds) || seconds < 0.005 || seconds > 1.2) return THUNDER_LIVE_E_ARG;   // (the scheduler's own range, midi.h: judged here too so that the answer is the live one)
  const int r = thunder_midi_set_lead(midi_, seconds);
  if (r == THUNDER_MIDI_E_BUSY) { cmdBusy_.fetch_add(1, std::memory_order_relaxed); return THUNDER_LIVE_E_BUSY; }
  return r == THUNDER_MIDI_OK ? THUNDER_LIVE_OK : r == THUNDER_MIDI_E_ARG ? THUNDER_LIVE_E_ARG : THUNDER_LIVE_E_INTERNAL;
}
int64_t LiveHost::midiDrain(thunder_midi_msg* out, int64_t cap) {
  if (!midi_) return THUNDER_LIVE_E_STATE;
  const int64_t r = thunder_midi_drain(midi_, out, cap);
  return r >= 0 ? r : r == THUNDER_MIDI_E_ARG ? THUNDER_LIVE_E_ARG : THUNDER_LIVE_E_INTERNAL;
}
int LiveHost::meters(double rateHz) {
  if (!(flags_ & THUNDER_LIVE_F_METERS) || !meterBank_) return THUNDER_LIVE_E_STATE;
  if (!finite(rateHz) || rateHz < 0 || rateHz > 30) return THUNDER_LIVE_E_ARG;
  LiveCmd c; c.type = LiveCmdType::Meters; c.x = rateHz; return push(c);   // applied at the top of the next quantum
}
/* M1 */ int LiveHost::metersSet(const thunder_live_meters_spec& sp) {   // CONTROL
  if (!(flags_ & THUNDER_LIVE_F_METERS) || !meterBank_) return THUNDER_LIVE_E_STATE;
  if (sp.chains_tracks >> 32) return THUNDER_LIVE_E_ARG;   // M3: bit t 0..31
  const uint32_t known = THUNDER_LIVE_MK_FXLVL | THUNDER_LIVE_MK_FXDYN | THUNDER_LIVE_MK_CHLVL | THUNDER_LIVE_MK_MASTERM;
  if ((sp.kinds & ~known) != 0 || (sp.fx_tracks >> (THUNDER_LIVE_MASTER + 1)) != 0) return THUNDER_LIVE_E_ARG;
  if (!finite(sp.rate_hz) || sp.rate_hz < 0 || sp.rate_hz > 30 || !finite(sp.chains_hz) || sp.chains_hz < 0 || sp.chains_hz > 30) return THUNDER_LIVE_E_ARG;
  LiveCmd c; c.type = LiveCmdType::MetersSet; c.x = sp.rate_hz; c.y = (sp.chains_hz > 0 && sp.chains_hz < 1) ? 1.0 : sp.chains_hz /* lead's read: never a rate so small that sr / hz overflows llround on the audio thread */; c.i0 = (int32_t)sp.kinds; c.i64a = (int64_t)sp.fx_tracks; c.i1 = (int32_t)(uint32_t)sp.chains_tracks;
  return push(c);   // applied at the top of the next quantum; a full ring is E_BUSY and nothing changed
}

// =========================================================================================================================================================================================================
// CONTROL: events, stats, test
// =========================================================================================================================================================================================================
// poll(): the next whole records of the two rings in `seq` order (live.h, EVENTS). One consumer thread.
int LiveHost::poll(void* buf, int cap) {
  if (!buf || cap < 0) return THUNDER_LIVE_E_ARG;
  uint8_t* out = static_cast<uint8_t*>(buf); int w = 0;
  for (;;) {
    // STRICTLY in seq order (verify-283 F2). The two ring heads are not one atomic view, so: load the critical head, the ordinary head, the critical head again, and accept the view only when the critical head did not
    // move. Any record posted before one we saw is then in the view: a critical record older than an ordinary one seen was published before it, so the second critical load (after the ordinary acquire) has it
    // (and equals the first, or we retry); an ordinary record older than a critical one seen was published before it, and the ordinary load came after the first critical acquire.
    uint32_t ch = 0, rh = 0; const uint32_t ct = evCrit_.tail.load(std::memory_order_relaxed), rt = evRing_.tail.load(std::memory_order_relaxed);
    for (int tries = 0; tries < 16; tries++) {
      ch = evCrit_.head.load(std::memory_order_acquire); rh = evRing_.head.load(std::memory_order_acquire);
      if (evCrit_.head.load(std::memory_order_acquire) == ch) break;
    }
    const bool hc = ct != ch, hr = rt != rh;
    if (!hc && !hr) break;
    const LiveEvSlot* sc = hc ? &evCrit_.buf[ct & 63u] : nullptr;
    const LiveEvSlot* sr = hr ? &evRing_.buf[rt & (kLiveEvRing - 1)] : nullptr;
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
    // P2 addition to the line live.h describes: the Seq's clock after the step, as the live-clock goldens hash it (tools/seq-golden.mjs "clk"); a reader that does not know it ignores the member
    o += "],\"clk\":{\"time\":"; liveplain::num(o, h.cTime); o += ",\"base\":"; liveplain::num(o, h.cBase); o += ",\"bt\":"; liveplain::num(o, h.cBt); o += ",\"btT\":"; liveplain::num(o, h.cBtT); o += ",\"bpm\":"; liveplain::num(o, h.cBpm); o += "}}";
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
  s.latency_frames = (int32_t)(int64_t)P(P_LATENCY); s.midi_begin_failed = (uint32_t)P(P_MIDIFAIL); s.midi_may_hang = (uint32_t)P(P_MIDIHANG); s.fx_remakes = (uint32_t)P(P_FXREMAKE); s.fx_failed_blocks = (uint32_t)P(P_FXFAIL);
  s.voices = (int32_t)P(P_VOICES); s.peak_voices = (int32_t)P(P_PEAKV);
  s.notes = (int32_t)P(P_NOTES); s.notes_played = (int32_t)P(P_NPLAYED); s.notes_late = (int32_t)P(P_NLATE); s.notes_no_pad = (int32_t)P(P_NNOPAD); s.notes_live = (int32_t)P(P_NLIVE); s.notes_sf = (int32_t)P(P_NSF);
  s.notes_var_missing = (int32_t)P(P_NVARMISS); s.notes_stretched = (int32_t)P(P_NSTRETCH); s.events_ignored = (int32_t)P(P_EVIGN);
  s.pad_buffers = ctlPadBuffers_.load(std::memory_order_relaxed); s.pad_retired = ctlPadRetiredN_.load(std::memory_order_relaxed);
  s.cmd_busy = cmdBusy_.load(std::memory_order_relaxed); s.cmd_late = (uint32_t)P(P_CMDLATE); s.evt_dropped = (uint32_t)P(P_EVTDROP); s.reanchors = (uint32_t)P(P_REANCH);
  { const uint64_t pub = songPublished_.load(std::memory_order_acquire), ad = P(P_SONGADOPT); s.song_pending = pub > ad ? (uint32_t)(pub - ad) : 0; }
  s.session_full = sessionFullFlag_.load(std::memory_order_relaxed);
  s.live_failed = (uint32_t)P(P_LIVEFAIL); s.live_late = (uint32_t)P(P_LIVELATE); s.thread_prepares = (uint32_t)P(P_PREP); s.evt_crit_dropped = (uint32_t)P(P_EVTCRIT); s.cmds_cleared = (uint32_t)P(P_CLEARED); s.cmds_no_seq = (uint32_t)P(P_NOSEQ);
  s.chains_blocks = P(P_CHBLOCKS); s.chains_failed_blocks = P(P_CHFAILED); s.chains_events = P(P_CHEVENTS); s.chains_events_dropped = P(P_CHDROPPED); s.chains_events_bad = P(P_CHBAD); s.chains_chc_no_node = P(P_CHNONODE); s.chains_feed_mismatch = P(P_CHMISMATCH);
  s.chains_nodes = (uint32_t)P(P_CHNODES); s.chains_plans = chPlans_.load(std::memory_order_relaxed); s.chains_refused = chRefused_.load(std::memory_order_relaxed); s.chains_over_budget = chOver_.load(std::memory_order_relaxed); s.chains_busy = chBusy_.load(std::memory_order_relaxed); s.chains_params_stale = (uint32_t)P(P_CHSTALE);
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
  if (!l || !r || frames < 0 || frames > maxBlock_ || frame0 < 0 || frame0 > kMaxHostFrame || (frames > 0 && l < r + frames && r < l + frames)) { zero(); return THUNDER_LIVE_E_ARG; }
  if (!pe_thread_ready()) { pe_prepare(); thread_prepares_++; }   // the one call that may allocate: this thread's Plaits / Elements workspace (live.h, create)
  calls_++; wallNs_ = wallNs;
  if (frames == 0) { publish(); return THUNDER_LIVE_OK; }        // the web's first handler: prepare only; no anchor, no quantum
  if (!anchored_) { anchored_ = true; off_ = origin_ = frame0; offPub_.store(off_, std::memory_order_relaxed); qFrame_ = 0; qRead_ = kLiveQuantum; nextClockAt_ = nextPosAt_ = nextLateAt_ = 0; }   // engine frame 0 is the first call's frame0
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

// A call that does not continue the last one (forward gap, backward jump, a restarted counter): NOTHING inside the engine moves. The engine frame of the next sample to be handed out is
// qFrame_ - (samples still buffered); the host's frame0 now names it (off_), so the song continues sample for sample with no step skipped or repeated, no silence, no burst.
void LiveHost::reanchor(int64_t frame0) {
  const int64_t skipped = frame0 - nextFrame_;
  const int64_t eNext = qFrame_ - (kLiveQuantum - qRead_);   // (qRead_ == kLiveQuantum: nothing buffered: the next quantum's start)
  off_ = frame0 - eNext; origin_ = frame0;
  offPub_.store(off_, std::memory_order_relaxed);
  reanchors_++;
  thunder_live_ev_error e; std::memset(&e, 0, sizeof e);
  e.h.type = THUNDER_LIVE_EV_ERROR; e.h.f = eNext; e.where = THUNDER_LIVE_WHERE_REANCHOR; e.code = 0; e.fatal = 0;   // (post() adds off_: the record's f is frame0)
  e.value = (int32_t)std::max<int64_t>(INT32_MIN, std::min<int64_t>(INT32_MAX, skipped));
  if (reanchorRecAt_ != (int64_t)quanta_) { reanchorRecAt_ = (int64_t)quanta_; post(&e, sizeof e); }   // at most one record a quantum (a jittery counter re-anchors on every call); every one is counted
}

void LiveHost::quantum() {
  const int64_t qStart = qFrame_;
  const double t0 = (double)qStart / sr_;
  // 1. APPLY COMMANDS
  const uint32_t nChainParams = chainParams_.avail();   // C2 L1: the snapshot of the edit ring BEFORE the command loop
  const uint32_t nFxValues = fxValues_.avail();         // PP: the same snapshot for thunder_live_fx_values
  const uint32_t nFxParams = fxParams_.avail();         // P3b: the same snapshot for the effects parameters (a record pushed after a commit that the loop did not see waits for the next quantum)
  retryStalledPlay(qStart);
  if (clearPend_) { clearPend_ = false; clearWaiting(qStart); }   // (a stalled play that started now)
  applyCommands(qStart, qStart + kLiveQuantum);
  if (testFxSeam_) testFxSeam_(testFxSeamUser_);   // TEST ONLY (null in a product): between the command loop and the effects drain
  applyFxParams(qStart, nFxParams);
  applyFxValues(qStart, nFxValues);                      // PP
  if (testCmdSeam_) testCmdSeam_(testCmdSeamUser_);   // TEST ONLY (null in a product)
  if (!stalledPlay_) applyChainParams(nChainParams);   // after the commands: a param sent after a commit meets the new patch. NOT while a play waits for its snapshot: its plan is not committed yet (a param applied now would be lost under the swap)
  // 2. ADOPT: seq_adopt, then seqGone() if the Seq was dropped, then PadsHost::beginQuantum(players, runner's Seq) every quantum
  adopt(qStart);
  // 3. CLOCKS
  players_->setTime(t0, qStart); mix_->setTime(t0);
  // 4. MIDI BEGIN: midi_begin(t0 ...), then the Starts and Stops queued by step 1 and the ENDED timer, DIRECTLY on the scheduler (before the step loop: the Start never comes after the first step of the Seq).
  midiBegin(t0, qStart);
  // 5. STEP
  const SeqPump pr = stepLoop(t0, qStart);
  // 6. MIDI END: book the outbox, the clock, the LFOs. Engine frames only: the shell maps them to its clock with the CLOCK records.
  midiEnd(t0);
  // 7. SONG OVER, and the ENDED timer
  songEnded(t0, qStart, pr);
  // 8. SOUND. HOOK POINT for the next units (U37 P3 effects bank, C2 chains host, the master chain): call them from render() right after players_->render and before/around mix_->render, with the ENGINE frame
  //    qStart (time t0 = qStart / sr_), never a host frame: the engine's timeline is gapless (a re-anchor changes off_ only), which is what the chains host and the effects bank rely on.
  render(t0, qStart);
  // 9. AFTER THE BLOCK (the counters first: stats.frames is the frame the NEXT quantum starts at)
  qFrame_ += kLiveQuantum; quanta_++;
  afterBlock(qStart);
}

// Step 1. Pop the ring into the waiting list (ring order); apply, in ring order, what is due in this quantum [qStart, qEnd): `at` < 0, in the past, or inside the quantum. A waiting command waits for the quantum
// that CONTAINS its `at`. A 65th waiting command (list full) is applied at once instead and counted (stats.cmd_late). live_host.h says the list is "ordered by at"; it is kept in RING order instead, which is what
// live.h's step 1 says commands are applied in (a stop at 3000 sent before a mix_set at 2950 must not be reordered): notes, departure 2.
void LiveHost::applyCommands(int64_t qStart, int64_t qEnd) {
  bool lateErr = false;
  // (a) the commands that were waiting (sent before everything in the ring now): apply the due ones in order, keep the rest in order. A stop or a play that starts CLEARS what is still waiting (clearPend_).
  int w = 0;
  for (int i = 0; i < nPend_; i++) {
    const LiveCmd p = pend_[i];
    if (p.type == LiveCmdType::FxCommit && !p.p) continue;   // already committed (and returned) by commitWaitingFx: done, not kept, not counted
    if (p.at < 0 || p.at < qEnd + off_) {
      if (p.type == LiveCmdType::FxCommit) pend_[i].p = nullptr;   // this one is being applied (from the copy `p`): commitWaitingFx, called by applyOne, must not take it a second time
      applyOne(p, qStart);
      if (clearPend_) { clearPend_ = false; for (int k = i + 1; k < nPend_; k++) pend_[w++] = pend_[k]; nPend_ = w; clearWaiting(qStart); w = 0; break; }   // what was kept and what was not yet looked at: cleared (an FxCommit among them is committed first)
    } else { if (w != i && p.type == LiveCmdType::FxCommit) pend_[i].p = nullptr; pend_[w++] = p; }   // moved down: the old slot must not stay a second live copy (commitWaitingFx walks the whole list while this loop runs)
  }
  nPend_ = w;
  // (b) the ring, in ring order: due now: applied; not due: waits; the list is full (64): applied at once instead and counted
  LiveCmd c;
  while (cmds_.pop(c)) {
    if (c.at < 0 || c.at < qEnd + off_) {   // `at` is a HOST frame, judged against the mapping in force now (a re-anchor in between: a gap's commands are due)
      applyOne(c, qStart);
      if (clearPend_) { clearPend_ = false; clearWaiting(qStart); }
      continue;
    }
    if (nPend_ < kLiveCmdWait) { pend_[nPend_++] = c; continue; }
    cmdLate_++; if (!lateErr) { lateErr = true; thunder_live_ev_error e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_ERROR; e.h.f = qStart; e.where = THUNDER_LIVE_WHERE_CMD; post(&e, sizeof e); }   // at most one record a quantum
    applyOne(c, qStart);
    if (clearPend_) { clearPend_ = false; clearWaiting(qStart); }
  }
  if (ackPend_) { ackPend_ = false; postSong(ackPendRev_, qStart); ackValid_ = true; ackRev_ = ackPendRev_; }
}

void LiveHost::dropSeq() {
  if (runner_.seq()) seqDropBase_ += runner_.seq()->dropped();
  runner_.stop();
}

void LiveHost::giveBack(PlayPlan* p) { const bool ok = playBack_.push(p); assert(ok); (void)ok; }   // room is guaranteed: play() allows 8 plans outstanding and the ring holds 8

void LiveHost::applyOne(const LiveCmd& c, int64_t qStart) {
  cmdsApplied_++;
  const int64_t atF = c.at < 0 || c.at - off_ < qStart ? qStart : c.at - off_;   // host -> ENGINE frame; never before this quantum's start
  const double at = (double)atF / sr_;
  switch (c.type) {
    case LiveCmdType::SongAck: ackPend_ = true; ackPendRev_ = c.i64a; break;   // one SONG record for the newest of the quantum's acks (applyCommands)
    case LiveCmdType::Play: applyPlay(static_cast<PlayPlan*>(c.p), qStart); break;
    case LiveCmdType::Stop: {
      if (stalledPlay_) { dropPlay(stalledPlay_, qStart); stalledPlay_ = nullptr; }   // a play that never started is cancelled too
      const int prev = stateA_;
      dropSeq();
      players_->stopAll(at); mix_->songLanesReset(); mix_->clockStop(at);
      if (chains_) chains_->stop();                                            // the page's tStop: chainsTempo with playing false; the events waiting for their quantum go
      midiOp(false, 0);                                                        // the MIDI stop, from every state (from IDLE it is repeated, harmless): done right after midi_begin of this quantum
      endStopAt_ = -1; stateA_ = THUNDER_LIVE_STATE_IDLE; clearPend_ = true;   // a stop clears the waiting list (live.h TIME)
      if (prev != THUNDER_LIVE_STATE_IDLE) applyPdc(qStart, true);             // LATENCY COMP AUTO is in force only while a song plays: the delays glide back to 0 (the mode On keeps them)
      if (prev != THUNDER_LIVE_STATE_IDLE) postState(qStart, THUNDER_LIVE_ST_STOPPED, 0);   // from IDLE: no record
      break; }
    case LiveCmdType::Hold: runner_.hold(); break;   // arms the flag only, whatever the state (also with no Seq)
    case LiveCmdType::Loop: if (runner_.seq()) runner_.seq()->setLoop(c.i0 != 0, c.i1, c.i2); else cmdsNoSeq_++; break;
    case LiveCmdType::Bpm: if (runner_.seq()) runner_.seq()->setBpm(c.x); else cmdsNoSeq_++; break;
    case LiveCmdType::Lead: lead_ = c.x; break;
    case LiveCmdType::Perf:
      if (runner_.seq()) { Seq* s = runner_.seq(); if (c.i0 == THUNDER_LIVE_PERF_XF) s->setXf(c.x); else if (c.i0 == THUNDER_LIVE_PERF_MACRO) s->setMac(c.i1, c.x); else s->setFill(c.x != 0); } else cmdsNoSeq_++;
      break;
    case LiveCmdType::StopTrack: players_->stopTrack(c.i0, at); break;
    case LiveCmdType::MixSet: {
      MixSet m; m.tr = c.i0; m.k = static_cast<MixKey>(c.i1); m.i = c.i2; m.v = c.x; m.ramp = c.y; m.at = at;   // sample-accurate from `at` (a time before this quantum is its start)
      mix_->set(m);
      break; }
    case LiveCmdType::FxCommit: {   // the plan comes back through fxBack_ (its room is guaranteed: fxCommit() allows 64 outstanding and the ring holds 64)
      commitWaitingFx(qStart, c.i0);   // an FxCommit of this track that still waits in pend_ was planned BEFORE this one (FxHost::plan reuses the previous plan's objects): it is committed first
      FxPlan* pl = static_cast<FxPlan*>(c.p);
      commitFxLive(c.i0, pl, qStart);
      const bool ok = fxBack_.push(pl); assert(ok); (void)ok;
      break; }
    case LiveCmdType::ChainsCommit: {   // R7: commit at the top of the quantum; the plan comes back through chBack_ (ONE plan is in flight: room is guaranteed)
      ChainsPlan* cp = static_cast<ChainsPlan*>(c.p);
      commitChains(cp);
      const bool ok = chBack_.push(cp); assert(ok); (void)ok;
      break; }
    case LiveCmdType::Meters:
      if (meterBank_) { if (c.x > 0) { meterBank_->setRate(c.x); metersOn_ = true; } else metersOn_ = false; }
      break;
    case LiveCmdType::MetersSet: applyMetersSet(c); break;   // M1
  }
}

// PLAY (live.h, thunder_live_play): adopt the play's snapshot (the runner starts on ITS Seq), setTime(t0), the mixer from the song at once (no ramp), the tempo seed, state PLAYING.
void LiveHost::applyPlay(PlayPlan* p, int64_t qStart) {
  if (stalledPlay_) { dropPlay(stalledPlay_, qStart); stalledPlay_ = nullptr; }   // a newer play replaces one that never started
  if (!tryStartPlay(p, qStart)) stalledPlay_ = p;
}
void LiveHost::retryStalledPlay(int64_t qStart) { if (stalledPlay_ && tryStartPlay(stalledPlay_, qStart)) stalledPlay_ = nullptr; }

bool LiveHost::tryStartPlay(PlayPlan* p, int64_t qStart) {
  const uint64_t oldDropped = runner_.seq() ? runner_.seq()->dropped() : 0;   // read BEFORE the adopt rebinds the runner (F11)
  if (seq_adopt(songSlot_, runner_)) noteSongAdopted(qStart);
  const SongSnap* sn = songSlot_.get();
  if (!sn || sn->sess.get() != p->sess) return false;   // the adopt was held back (the return ring is full: the control side has not collected): try again next quantum; the old Seq is never started
  seqDropBase_ += oldDropped; runner_.stop();           // the Seq that was running is dropped (its voices are NOT cut); its dropped events stay counted
  runner_.start(sn->seq.get());
  const double t0 = p->t0 >= 0 ? (double)(p->t0 - off_) / sr_ : (double)(qStart + (int64_t)std::llround(kPlayDelaySec * sr_)) / sr_;
  runner_.seq()->setTime(t0);
  if (busSlot_.adopt() && !chains_) mix_->setPlan(busSlot_.get()->plan);   // WITH a chains host the bus plan is set by ChainsHost::commit alone (R7: a plan newer than the nodes' bus maps would hand a node the wrong feeds); the message is adopted so that it is freed
  commitChains(p->chains);                                                 // R7: the play's chains plan (null: none needed), BEFORE mix_->load (load's audible mask reads the plan)
  mix_->load(*sn->song); mix_->songLanesReset(); mix_->clockStop((double)qStart / sr_);
  players_->live().tempo((double)qStart / sr_, runner_.seq()->bpm());   // the tempo consumers are seeded: SEV_TEMPO is emitted only on a change (seq.cpp:995)
  if (chains_) chains_->play(runner_.seq()->bpm(), p->song->lpb ? p->song->lpb : 4, p->song->ticks ? p->song->ticks : 6);                     // the page's chainsTempo at the end of tStart: {tempo, playing true}; the queue of the Seq that is gone is cleared
  const bool fromIdle = stateA_ == THUNDER_LIVE_STATE_IDLE;
  playQ_ = qStart;
  midiOp(true, p->spp);                                 // the MIDI Start: done right after midi_begin of this quantum, before the first step (D-1)
  endStopAt_ = -1; songOverDone_ = false; stateA_ = THUNDER_LIVE_STATE_PLAYING; clearPend_ = true;   // a play that starts clears the waiting list
  postState(qStart, THUNDER_LIVE_ST_PLAYING, 0);
  // P3: the song's effects, the master chain and the PDC plan (SongRenderer's setEffects / setMasterChain / setPlan, in the order a render calls them). The tempo the effects were mapped for is the song's (FxBank::load(S, S.bpm)).
  if (fxBank_) { fxBank_->setBpm(p->song->bpm); commitWaitingFx(qStart); commitPlayFx(p, qStart); }   // a waiting FxCommit was planned before the play's own plans: it goes first
  if (mchain_ && p->hasMaster && p->cfgOrd >= masterOrd_) { mchain_->configure(p->master); masterOrd_ = p->cfgOrd; }
  if ((flags_ & THUNDER_LIVE_F_EFFECTS) && p->cfgOrd >= pdcOrd_) { pdcCur_ = p->pdc; pdcMode_ = p->pdcMode; pdcHave_ = true; pdcOrd_ = p->cfgOrd; applyPdc(qStart, !fromIdle); }   // from IDLE the delays are set AT ONCE as a render sets them (nothing of this play sounds yet: the first note is >= 80 ms away); over a running song they glide 10 ms
  // R1 (verify-283): the published state is stored BEFORE the play goes back. The control side decides "playing" from `playOut_ > 0 || state_ == PLAYING`, and a collect() that pops this plan lowers playOut_ at once:
  // with the store after the quantum (afterBlock) a song_publish between the two took the IDLE path and the running song missed its edit.
  state_.store(THUNDER_LIVE_STATE_PLAYING, std::memory_order_release);
  giveBack(p);
  if (testAudioSeam_) testAudioSeam_(testAudioSeamUser_);   // TEST ONLY: where the window was
  return true;
}

// P3 ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The master hook is on while the master chain exists or the master effects host has slots (SongRenderer::setMasterChain / setEffects); it runs the master effects, then the chain (SongRenderer::masterHook).
void LiveHost::masterHook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds) {
  LiveHost* h = static_cast<LiveHost*>(user);   // MASTER VOL -> master effects -> [mastering -> safety limiter] -> OUT
  // The master node hears the beat clock like a track node (page: setMfx calls clockTo(mfx)); without it master Flex / Step gate / Retrigger see a stopped clock.
  if (h->fxBank_ && h->fxBank_->host(kSongNT).slotCount() > 0) h->fxBank_->host(kSongNT).process(L, R, frames, t0, h->mix_->beat(kSongNT), frames);
  if (h->mchain_) MasterChain::hook(h->mchain_.get(), track, L, R, frames, t0, feeds, nFeeds);
}
void LiveHost::setMasterHook() { mix_->setMasterFx((mchain_ || (fxBank_ && fxBank_->host(kSongNT).slotCount() > 0)) ? &LiveHost::masterHook : nullptr, this); }
void LiveHost::commitFxLive(int track, FxPlan* pl, int64_t qStart) {
  if (!fxBank_ || !pl || track < 0 || track > kSongNT) return;
  fxBank_->commitLive(track, pl, *mix_, (double)qStart / sr_);
  if (track == kSongNT) setMasterHook();
}
void LiveHost::commitPlayFx(PlayPlan* p, int64_t qStart) {
  if (p->fx[kSongNT]) commitFxLive(kSongNT, p->fx[kSongNT], qStart);   // the master first, then the tracks 0..31 (FxBank::load)
  for (int t = 0; t < kSongNT; t++) if (p->fx[t]) commitFxLive(t, p->fx[t], qStart);
}
void LiveHost::dropPlay(PlayPlan* p, int64_t qStart) { commitWaitingFx(qStart); commitPlayFx(p, qStart); commitChains(p->chains); giveBack(p); }   // C2 R7: every plan handed to the audio side is committed (the control side's chainsSong_ is what the audio side ends with)
// Commits, in list order, the FxCommits that wait in pend_ (all tracks, or only `track`) and returns their plans through fxBack_. COMMITTED IN PLANNING ORDER: FxHost::plan (control) reuses the objects of the previous plan of the track,
// so a later plan must never be committed before an earlier one. Entries are marked done (p = nullptr, type kept) and NOT removed: applyCommands iterates pend_ by index while calling applyOne (which may land here), and a
// compaction here would corrupt its `w` / `i` loop; that loop and clearWaiting skip an FxCommit with a null p, and the loop drops it at its next compaction.
void LiveHost::commitWaitingFx(int64_t qStart, int track) {
  for (int i = 0; i < nPend_; i++) {
    if (pend_[i].type != LiveCmdType::FxCommit || !pend_[i].p || (track >= 0 && pend_[i].i0 != track)) continue;
    FxPlan* pl = static_cast<FxPlan*>(pend_[i].p); pend_[i].p = nullptr;
    commitFxLive(pend_[i].i0, pl, qStart);
    const bool ok = fxBack_.push(pl); assert(ok); (void)ok;
  }
}
void LiveHost::applyPdc(int64_t qStart, bool glide) {
  if (!pdcHave_) return;
  mix_->setTime((double)qStart / sr_);   // SongRenderer::setPlan: the delays are set at the block's start
  PdcApply a; a.mode = pdcMode_; a.live = glide; a.playing = stateA_ != THUNDER_LIVE_STATE_IDLE; a.now = glide && stateA_ == THUNDER_LIVE_STATE_PLAYING && qStart == playQ_;
  pdc_apply(*mix_, pdcCur_, a);
}
// At most `n` records: the ones that were in the ring BEFORE this quantum's command loop began (C2 L1). The producer pushes a ChainsCommit (or a Play with a plan) BEFORE the records that follow it, so every record seen by the snapshot
// has its commit already visible to the command loop that runs after the snapshot; a record pushed later (with a commit the loop did not see) waits for the next quantum, where it meets the new patch.
void LiveHost::commitChains(ChainsPlan* p) { if (!chains_) return; if (p && !p->noop() && p->full()) chFullApplied_++; chains_->commit(p, *mix_); }
void LiveHost::applyChainParams(uint32_t n) {
  LiveChainParam r;
  while (n-- && chainParams_.pop(r)) {
    if (r.gen < chFullApplied_) { chStale_++; continue; }   // V3a: pushed before a whole-patch plan that has been committed since: the swap carries every value of the song, an older record would override the newer patch
    assert(r.gen == chFullApplied_ || stalledPlay_);        // (a record of a plan not yet committed cannot be here outside a stalled play: L1's snapshot; in a release build it is applied)
    if (chains_) chains_->param(r.id, r.name, r.v);
  }   // (NUL-terminated by the control side; every node gets it: ChainsHost::param)
}
void LiveHost::applyFxParams(int64_t qStart, uint32_t n) {   // at most the `n` records that were in the ring before the command loop (P3b, as applyChainParams)
  LiveFxParam fp;
  while (n-- && fxParams_.pop(fp)) {
    if (!fxBank_) continue;
    const double now = (double)qStart / sr_, at = fp.at < 0 || fp.at - off_ < qStart ? now : (double)(fp.at - off_) / sr_;
    fxBank_->host(fp.track).psetAt(at, fp.slot, fp.p);   // FXV's queue (FxHost::psetAt): applied by the first block that starts within 64 samples of `at`
  }
}
void LiveHost::applyFxValues(int64_t qStart, uint32_t n) {   // PP: at most the `n` records that were in the ring before the command loop (as applyFxParams)
  LiveFxValues& r = fxvScratch_;
  while (n-- && fxValues_.pop(r)) {
    if (!fxBank_ || r.track < 0 || r.track > kSongNT) continue;
    const double now = (double)qStart / sr_, at = r.at < 0 || r.at - off_ < qStart ? now : (double)(r.at - off_) / sr_;
    FxHost& H = fxBank_->host(r.track);
    if (r.kind == THUNDER_LIVE_FXV_PUMP) H.psetPumpRt(at, r.slot, r.pv, r.n); else H.psetModRt(at, r.slot, r.mp, r.n);   // the host's queues; applied by the first block that starts within 64 samples of `at`
  }
}
void LiveHost::clearWaiting(int64_t qStart) {
  for (int i = 0; i < nPend_; i++) {
    if (pend_[i].type == LiveCmdType::FxCommit && !pend_[i].p) continue;   // done (commitWaitingFx)
    if (pend_[i].type == LiveCmdType::FxCommit) { FxPlan* pl = static_cast<FxPlan*>(pend_[i].p); commitFxLive(pend_[i].i0, pl, qStart); const bool ok = fxBack_.push(pl); assert(ok); (void)ok; }   // the plan was made against the chain before it: it is committed, not dropped
    else cmdsCleared_++;
  }
  nPend_ = 0;
}
int64_t LiveHost::latencyFrames() const {   // SongRenderer::latencyFrames: the master chain, the PDC's T (in force: the mode On, or Auto while a song plays), the master's pump effects
  const bool inForce = pdcHave_ && (pdcMode_ == PdcMode::On || (pdcMode_ == PdcMode::Auto && stateA_ != THUNDER_LIVE_STATE_IDLE));
  return (mchain_ ? mchain_->latency() : 0) + (inForce ? pdc_trim_frames(pdcCur_, sr_) : 0) + (fxBank_ ? fxBank_->masterPumpLatency() : 0);
}
void LiveHost::postMeters() {
  MeterEvent me;
  while (meterBank_ && meterBank_->pop(&me)) {
    thunder_live_ev_meters e; std::memset(&e, 0, sizeof e);
    e.h.type = THUNDER_LIVE_EV_METERS; e.h.f = toFrame(me.at, sr_);   // the frame at the end of the window (post() maps it to the host's)
    std::memcpy(e.a, me.a, sizeof e.a);
    std::memcpy(&e.reserved[0], &me.seq, 4);                           // the bank's own count (the head's seq is the ring's order: poll merges by it)
    post(&e, sizeof e);
  }
}

/* M1: AUDIO, step 1 (the top of a quantum). The replacement state of a MetersSet command. METERS: as LiveCmd::Meters. The watched effects hosts get setLevels / setTelemetry true and every other host false, but a host whose
   state does not change is left alone (setLevels restarts the 12-block window). A host that becomes watched has the post it held from an earlier watching dropped, so the first record is a fresh one. No allocation, no lock. */
void LiveHost::applyMetersSet(const LiveCmd& c) {
  if (meterBank_) { if (c.x > 0) { meterBank_->setRate(c.x); metersOn_ = true; } else metersOn_ = false; }
  const uint32_t kinds = (uint32_t)c.i0; const uint64_t tr = (uint64_t)c.i64a;
  const uint64_t wl = fxBank_ && (kinds & THUNDER_LIVE_MK_FXLVL) ? tr : 0, wd = fxBank_ && (kinds & THUNDER_LIVE_MK_FXDYN) ? tr : 0;
  if (fxBank_) {
    for (int t = 0; t <= kSongNT; t++) {
      const uint64_t b = (uint64_t)1 << t; FxHost& h = fxBank_->host(t);
      if ((wl & b) != (fxLvlWatch_ & b)) { h.setLevels((wl & b) != 0); if (wl & b) { float drop[8]; h.popLevels(drop); } }
      if ((wd & b) != (fxDynWatch_ & b)) { h.setTelemetry((wd & b) != 0); if (wd & b) { FxDyn drop[kFxSlots]; h.popDyn(drop, kFxSlots); } }
    }
  }
  fxLvlWatch_ = wl; fxDynWatch_ = wd;
  /* M3: the chains peaks. Watched = the CHLVL kind AND the host has a chains host. setPeakWatch zeroes the running peaks of a track whose watching changes (an identical re-send changes nothing, so the pacing and
     the running peaks go on); a changed set restarts the pacing, so the first record comes at the next quantum. chains_hz 0 = 16 a second. */
  if (chains_) {
    const uint32_t wc = (kinds & THUNDER_LIVE_MK_CHLVL) ? (uint32_t)c.i1 : 0u;
    if (wc != chWatch_) { chains_->setPeakWatch(wc); nextChAt_ = 0; }
    chWatch_ = wc;
  }
  chHz_ = c.y > 0 ? c.y : 16.0;
}

/* M1: AUDIO, step 9 after postMeters. For each WATCHED host (and only those: nothing is popped, nothing computed, for the others) FxHost::popLevels / popDyn hand over what process() posted this quantum (every 12th
   block); a record is built on the stack from the copy and posted on the big ring. No new maths. A full ring drops the newest record and counts it (post). qEnd: the engine frame at the end of the quantum. */
void LiveHost::postFxMeters(int64_t qEnd) {
  if (!fxBank_ || (fxLvlWatch_ | fxDynWatch_) == 0) return;
  for (int t = 0; t <= kSongNT; t++) {
    const uint64_t b = (uint64_t)1 << t;
    if (!((fxLvlWatch_ | fxDynWatch_) & b)) continue;
    FxHost& h = fxBank_->host(t);
    if (fxLvlWatch_ & b) {
      float lv[8];
      if (h.popLevels(lv)) {
        thunder_live_ev_fxlvl e; std::memset(&e, 0, sizeof e);
        e.h.type = THUNDER_LIVE_EV_FXLVL; e.h.f = qEnd; e.track = t;
        for (int i = 0, n = h.chainSize(); i < n; i++) { const FxEntry* en = h.chainEntry(i); if (en && en->slot >= 0 && en->slot < 8) e.slots |= 1u << en->slot; }
        for (int k = 0; k < 8; k++) e.lvl[k] = (e.slots >> k) & 1u ? lv[k] : 0.f;
        post(&e, sizeof e);
      }
    }
    if (fxDynWatch_ & b) {
      FxDyn d[kFxSlots];
      const int n = h.popDyn(d, kFxSlots);
      if (n > 0) {
        thunder_live_ev_fxdyn e; std::memset(&e, 0, sizeof e);
        e.h.type = THUNDER_LIVE_EV_FXDYN; e.h.f = qEnd; e.track = (int16_t)t; e.n = (int16_t)n;
        for (int i = 0; i < n; i++) {
          int type = -1; for (int k = 0, m = h.chainSize(); k < m; k++) { const FxEntry* en = h.chainEntry(k); if (en && en->slot == d[i].slot) { type = en->t; break; } }
          thunder_live_ev_fxdyn_ent& x = e.ent[i]; x.slot = (int16_t)d[i].slot; x.type = (int16_t)type; x.red_db = d[i].v[0]; x.in_db = d[i].v[1]; x.out_db = d[i].v[2];
        }
        post(&e, sizeof e);
      }
    }
  }
}

/* M3: AUDIO, step 9 after postFxMeters. Only while a track is watched (chWatch_ != 0): at most once per 1 / chains_hz seconds (paced in whole quanta against the engine frame), for each watched track that HAS a chains node
   the running peaks the hook kept (ChainsHost::popPeaks, which also resets them) go into a 40-byte record built on the stack and posted on the big ring. A watched track with no node posts nothing; nothing is popped,
   nothing is computed (the hook's scan is on only for the watched bits) for the others. A full ring drops the newest and counts it (post). qEnd: the engine frame at the end of the quantum. */
void LiveHost::postChainsMeters(int64_t qEnd) {
  if (!chains_ || chWatch_ == 0 || qEnd < nextChAt_) return;
  nextChAt_ = qEnd + (int64_t)std::llround(sr_ / chHz_);
  for (int t = 0; t < kSongNT; t++) {
    if (!((chWatch_ >> t) & 1u)) continue;
    float pk[4];
    if (!chains_->popPeaks(t, pk)) continue;
    thunder_live_ev_chlvl e; std::memset(&e, 0, sizeof e);
    e.h.type = THUNDER_LIVE_EV_CHLVL; e.h.f = qEnd; e.track = t;
    e.in[0] = pk[0]; e.in[1] = pk[1]; e.out[0] = pk[2]; e.out[1] = pk[3];
    post(&e, sizeof e);
  }
}

// ONE SONG record for the newest rev adopted in a quantum (live.h, song_publish); a play that adopts the snapshot an IDLE publish already acknowledged posts none.
void LiveHost::noteSongAdopted(int64_t qStart) {
  const SongSnap* sn = songSlot_.get();
  pub_[P_SONGADOPT].store(sn->ord, std::memory_order_relaxed);
  ackPend_ = false;   // an ack of this quantum (an IDLE publish before the play) and the play's adoption are one record: the adoption's
  if (ackValid_ && ackRev_ == sn->rev) { ackValid_ = false; return; }
  ackValid_ = false; postSong(sn->rev, qStart);
}

// Step 2.
void LiveHost::adopt(int64_t qStart) {
  if (!stalledPlay_ && seq_adopt(songSlot_, runner_)) noteSongAdopted(qStart);   // the newest snapshot; the Seq that was running hands its state to it (takeOver). Not while a play is stalled: only tryStartPlay adopts then (F9)
  // seqGone() BEFORE beginQuantum when the Seq was dropped (stop, held, song ended and timed out): then a pads snapshot may be adopted with a null Seq (pads_snap.h)
  if (hadSeq_ && !runner_.seq()) { padsHost_->seqGone(); hadSeq_ = false; }
  const bool padsNew = padsHost_->beginQuantum(*players_, runner_.seq());   // EVERY quantum: the scan of the voices' epochs, the adopt, the chop table
  if (runner_.seq()) hadSeq_ = true;
  if (padsNew) { thunder_live_ev_pads e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_PADS; e.h.f = qStart; e.id = (int64_t)padsHost_->current()->id; post(&e, sizeof e); }
  if (busSlot_.adopt() && !chains_) mix_->setPlan(busSlot_.get()->plan);   // never earlier than the song (it was published after it). With a chains host: adopted (freed) and NOT applied: ChainsHost::commit sets the plan (R7)
  if (pdcSlot_.adopt()) { const LivePdcMsg* m = pdcSlot_.get(); if (m->ord > pdcOrd_) { pdcCur_ = m->plan; pdcMode_ = m->mode; pdcHave_ = true; pdcOrd_ = m->ord; applyPdc(qStart, true); } }   // a pdc_set: the delays glide 50 ms (a play's own plan, of a later ordinal, wins)
  if (masterSlot_.adopt()) { const LiveMasterMsg* m = masterSlot_.get(); if (mchain_ && m->ord > masterOrd_) { mchain_->configure(m->cfg); masterOrd_ = m->ord; } }   // a master_config: the processors keep their states
  if (noteSessionFull_.load(std::memory_order_acquire) && noteSessionFull_.exchange(false, std::memory_order_acq_rel)) {
    thunder_live_ev_error e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_ERROR; e.h.f = qStart; e.where = THUNDER_LIVE_WHERE_SESSION; post(&e, sizeof e);   // informational, once
  }
}

// SeqEvent routing: SongRenderer::dispatch (render.cpp:28) without chains: the mixer's events, the beat clock, the effects bank's, then the Players.
void LiveHost::dispatch(const SeqEvent& e, const Seq& seq, const Song& song) {
  events_++;
  switch (e.type) {
    case SEV_TVL: case SEV_TVA: case SEV_TPN: case SEV_MVOL: case SEV_MOUT: mix_->event(e); return;
    case SEV_BEAT: mix_->event(e); return;
    case SEV_FXC: case SEV_FXP: if (fxBank_) fxBank_->event(e, mix_->now(), *mix_); return;   // FXO / FLX / GAT / RTG lanes and FXV (SongRenderer::dispatch)
    case SEV_CHC: if (chains_) { chains_->event(e); return; } break;   // the tracker's BYP: with no chains host it is counted as ignored below, as before
    case SEV_TEMPO: if (chains_) chains_->event(e); break;               // the chains' transport; the Players still see it (the live bass nodes follow the tempo)
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
    if (dumpEvs_) dumpCopy(evs, k, st, *sq, qStart);
    notePos(st);
    if (midiOk_) midi_sched(midi_).onStep(evs, k, st, *sq, t0);   // the WHOLE step first, then its events are dispatched (render.cpp quantum(): the order midi_drive.h pins)
    for (int i = 0; i < k; i++) { dispatch(evs[i], *sq, song); players_->samplePeak(); }
  });
  midiSeqT_ = sq->time(); midiBpm_ = sq->bpm();   // midi_end's figures (step 6); kept for the quanta in which the runner has no Seq (HELD, IDLE)
  if (r.held) {   // EXACT STOP: the runner dropped the Seq at a pattern edge
    seqDropBase_ += sq->dropped();
    stateA_ = THUNDER_LIVE_STATE_HELD;
    heldRecord(r.heldT, qStart);
  }
  return r;
}

// the played step's place for the ROWS record (every step) and the POS record (30 a second): row / step = the highest of the valid tracks (what SeqRunner::pump computes), cr that track's chain row
void LiveHost::notePos(const SeqStep& st) {
  alignas(8) uint8_t buf[sizeof(thunder_live_ev_rows) + 8 * kSeqTracks];
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
  alignas(8) uint8_t buf[sizeof(thunder_live_ev_rows) + 8 * kSeqTracks];
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
void LiveHost::dumpCopy(const SeqEvent* evs, int k, const SeqStep& st, const Seq& sq, int64_t qStart) {
  const uint64_t tail = dumpTail_.load(std::memory_order_acquire);
  if (dumpWr_ + (uint64_t)k - tail > dumpEvCap_ || dumpHeads_->head.load(std::memory_order_relaxed) - dumpHeads_->tail.load(std::memory_order_acquire) >= 1024u) { dumpDropped_++; return; }
  for (int i = 0; i < k; i++) dumpEvs_[(size_t)((dumpWr_ + (uint64_t)i) % dumpEvCap_)] = evs[i];
  LiveDumpHead h; h.frame = qStart; h.t = st.t; h.bpm0 = st.bpm0; h.tks = st.tks; h.n = k; h.validMask = dumpWr_;   // validMask: the index of the first event
  h.cTime = sq.time(); h.cBase = sq.base(); h.cBt = sq.bt(); h.cBtT = sq.btT(); h.cBpm = sq.bpm();
  const bool ok = dumpHeads_->push(h); assert(ok); (void)ok;
  dumpWr_ += (uint64_t)k;
}

// ---- P4: MIDI -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// The scheduler's transport is driven DIRECTLY from the audio side (live.h MIDI; docs/port/U37-P4-NOTES.md): the Start / Stop queued by step 1 (play, stop) and the ENDED timer's Stop are issued right after midi_begin
// returns, before the step loop, with the quantum's `now`: the point at which a posted thunder_midi_start / _stop would have run (midi_api.cpp blockBegin), in the order the commands were applied.
void LiveHost::midiOp(bool start, int spp) {
  if (!midi_) return;
  if (nMidiOps_ == kMidiOps) { std::memmove(midiOps_, midiOps_ + 1, sizeof(MidiOp) * (size_t)(kMidiOps - 1)); nMidiOps_--; }
  midiOps_[nMidiOps_].start = start; midiOps_[nMidiOps_].spp = spp; nMidiOps_++;
}
void LiveHost::midiBegin(double t0, int64_t qStart) {
  midiOk_ = false;
  if (!midi_) return;
  // the web's frame() rule: the MIDI stop 0.3 s after the song's end (the state machine of live.h: STATE ENDED -> IDLE is posted by songEnded, step 7, from the same test)
  if (stateA_ == THUNDER_LIVE_STATE_ENDED && endStopAt_ >= 0 && t0 >= endStopAt_) midiOp(false, 0);
  const int rc = midi_begin(midi_, t0, std::min(1.0, (double)kLiveQuantum / sr_), std::min(2.0, std::max(0.0, (double)latencyFrames() / sr_)), mix_->audibleMask());
  if (rc < 0) {   // cannot happen with finite arguments (midi_host.h): counted; the first one panics the gear (the scheduler's own PANIC, not the control-ring call: this is the audio thread) and says so once
    nMidiOps_ = 0; midiBeginFailed_++;
    if (!midiBeginFailedOnce_) {
      midiBeginFailedOnce_ = true; midi_sched(midi_).panic(t0);
      thunder_live_ev_error e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_ERROR; e.h.f = qStart; e.where = THUNDER_LIVE_WHERE_MIDI; e.code = rc; e.fatal = 0;
      post(&e, sizeof e);
    }
    assert(false && "midi_begin failed");
    return;
  }
  midiOk_ = true;
  MidiSched& ms = midi_sched(midi_);
  for (int i = 0; i < nMidiOps_; i++) { if (midiOps_[i].start) { ms.start(midiOps_[i].spp, t0); } else { ms.stop(t0); } }
  nMidiOps_ = 0;
}
void LiveHost::midiEnd(double t0) {
  if (!midi_ || !midiOk_) return;
  const Seq* sq = runner_.seq();
  if (!midi_end(midi_, t0, midiSeqT_, midiBpm_, sq != nullptr && sq->alive())) midiBeginFailed_++;
}

// Step 7: the sequencer ran out (once per play): Players::songOver, state ENDED, the 0.3 s timer; and the timer's end: IDLE, STOPPED arg 1.
void LiveHost::songEnded(double t0, int64_t qStart, const SeqPump& pr) {
  if (pr.ended && !songOverDone_ && runner_.seq()) {
    songOverDone_ = true;
    const double endT = runner_.seq()->time();
    players_->songOver(endT);                      // C4: live voices still held let go at the song's end
    stateA_ = THUNDER_LIVE_STATE_ENDED; endStopAt_ = std::max(t0, endT) + kEndStopSec;   // from the SONG's end (engine time), not from the quantum that found it
    thunder_live_ev_state e; std::memset(&e, 0, sizeof e); e.h.type = THUNDER_LIVE_EV_STATE; e.h.f = toFrame(endT, sr_); e.state = THUNDER_LIVE_ST_ENDED; e.arg = 0;
    post(&e, sizeof e);
    return;                                        // (the timer is judged from the next quantum on)
  }
  if (stateA_ == THUNDER_LIVE_STATE_ENDED && endStopAt_ >= 0 && t0 >= endStopAt_) {
    dropSeq();                                     // no sequencer exists in IDLE (seqGone() follows at the next quantum's adopt)
    endStopAt_ = -1; stateA_ = THUNDER_LIVE_STATE_IDLE;
    if (chains_) chains_->stop();                    // the page's frame() calls tStop 0.3 s after the song's end: chainsTempo with playing false (HELD keeps playing until the stop command, as the page's tp.on does)
    applyPdc(qStart, true);                          // the song is over: LATENCY COMP AUTO lets go (as a stop)
    postState(qStart, THUNDER_LIVE_ST_STOPPED, 1);   // (the MIDI stop was issued at step 4 of this quantum, midiBegin: the same test)
  }
}

// Step 8: the voices into the tracks' inputs, then the mixer and the meters (render.cpp:112-117 without the tap and the chains).
void LiveHost::render(double t0, int64_t qStart) {
  if (chains_) chains_->beginBlock(qStart);   // C2 R11, FIRST: the ENGINE frame (a multiple of 128: both due rules are exact): the chc / tempo events due at or before this quantum reach the nodes before any node runs
  players_->render(mix_->inputsL(), mix_->inputsR(), kLiveQuantum, t0, qStart);
  if (testTap_) testTap_(testTapUser_, *mix_, qStart);   // TEST ONLY (null in a product: one pointer test a quantum)
  float* const qL = q_.get(); float* const qR = q_.get() + kLiveQuantum;
  mix_->render(qL, qR, kLiveQuantum, t0);   // (the track effects, the master effects and the master chain run inside, through the hooks)
  if (metersOn_ && meterBank_) { meters_feed(*meterBank_, *mix_, qL, qR, kLiveQuantum); meterBank_->advance(kLiveQuantum, t0 + (double)kLiveQuantum / sr_); }   // render.cpp: after the mixer
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
  uint32_t mLate[3] = {0, 0, 0};
  if (midi_) { const MidiStats& ms = midi_sched(midi_).stats(); mLate[0] = (uint32_t)ms.lateNotes; mLate[1] = (uint32_t)ms.lateTicks; mLate[2] = (uint32_t)ms.mayHang; }
  const bool mChanged = mLate[0] != midiLateLast_[0] || mLate[1] != midiLateLast_[1] || mLate[2] != midiLateLast_[2];
  if (qStart >= nextLateAt_ && (lateLast_[0] != (uint32_t)ps.notesLate || lateLast_[1] != (uint32_t)runner_.skips() || mChanged)) {
    thunder_live_ev_late e; std::memset(&e, 0, sizeof e);
    e.h.type = THUNDER_LIVE_EV_LATE; e.h.f = qStart; e.notes_late = (uint32_t)ps.notesLate; e.runner_skips = (uint32_t)runner_.skips();
    e.midi_late_notes = mLate[0]; e.midi_late_ticks = mLate[1]; e.midi_may_hang = mLate[2];
    post(&e, sizeof e); lateLast_[0] = (uint32_t)ps.notesLate; lateLast_[1] = (uint32_t)runner_.skips(); nextLateAt_ = qStart + (int64_t)std::llround(sr_ / 4);
    midiLateLast_[0] = mLate[0]; midiLateLast_[1] = mLate[1]; midiLateLast_[2] = mLate[2];
  }
  postMeters();
  postFxMeters(qStart + kLiveQuantum);   // M1
  postChainsMeters(qStart + kLiveQuantum);   // M3
  publish();
  state_.store(stateA_, std::memory_order_release);
}

void LiveHost::publish() {
  const PlayerStats& ps = players_->stats();
  auto S = [&](int i, uint64_t v) { pub_[i].store(v, std::memory_order_relaxed); };
  S(P_FRAMES, (uint64_t)(qFrame_ + off_)); S(P_ORIGIN, (uint64_t)origin_); S(P_QUANTA, quanta_); S(P_CALLS, calls_); S(P_STEPS, steps_); S(P_EVENTS, events_);
  S(P_SEQDROP, std::max<uint64_t>(pub_[P_SEQDROP].load(std::memory_order_relaxed), seqDropBase_ + (runner_.seq() ? runner_.seq()->dropped() : 0)));
  S(P_SKIPS, runner_.skips()); S(P_CMDS, cmdsApplied_); S(P_DUMPDROP, dumpDropped_);
  S(P_VOICES, (uint64_t)players_->voices()); S(P_PEAKV, (uint64_t)ps.peakAll);
  S(P_NOTES, (uint64_t)ps.notes); S(P_NPLAYED, (uint64_t)ps.notesPlayed); S(P_NLATE, (uint64_t)ps.notesLate); S(P_NNOPAD, (uint64_t)ps.notesNoPad); S(P_NLIVE, (uint64_t)ps.notesLive); S(P_NSF, (uint64_t)ps.notesSf);
  S(P_NVARMISS, (uint64_t)ps.notesVarMissing); S(P_NSTRETCH, (uint64_t)ps.notesStretched); S(P_EVIGN, (uint64_t)st_.eventsIgnored);
  S(P_CMDLATE, cmdLate_); S(P_CLEARED, cmdsCleared_); S(P_NOSEQ, cmdsNoSeq_); S(P_EVTDROP, evtDropped_); S(P_EVTCRIT, evtCritDropped_); S(P_REANCH, reanchors_); S(P_PREP, (uint64_t)thread_prepares_);
  S(P_LATENCY, (uint64_t)(int64_t)latencyFrames());
  S(P_MIDIFAIL, midiBeginFailed_); S(P_MIDIHANG, midi_ ? midi_sched(midi_).stats().mayHang : 0);
  if (fxBank_) { uint64_t rm = 0, fb = 0; for (int t = 0; t <= kSongNT; t++) { rm += fxBank_->host(t).audioRemakes(); fb += fxBank_->host(t).failedBlock(); } S(P_FXREMAKE, rm); S(P_FXFAIL, fb); }
  if (chains_) { const ChainsHostStats cs = chains_->audioStats(); S(P_CHSTALE, chStale_); S(P_CHBLOCKS, cs.blocks); S(P_CHFAILED, cs.failedBlocks); S(P_CHEVENTS, cs.eventsApplied); S(P_CHDROPPED, cs.droppedEvents); S(P_CHBAD, cs.badEvents); S(P_CHNONODE, cs.chcNoNode); S(P_CHMISMATCH, cs.feedMismatch); S(P_CHNODES, (uint64_t)chains_->audioNodes()); }   // (the audio side's own fields: stats() is never called here)
  const LiveStats ls = players_->live().stats(); S(P_LIVEFAIL, players_->live().failedNodes()); S(P_LIVELATE, ls.late);
}

// ---- records ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// post(): `rec` starts with a thunder_live_ev_head with `type` and `f` set; this fills `bytes` and `seq` (counted over ALL records, dropped ones too: a gap shows a loss) and pushes into the critical ring (STATE, HELD,
// SONG, PADS, ERROR) or the ordinary one. A full ring refuses the NEWEST record and counts it (live.h, EVENTS). Audio thread: a stack copy and a release store, nothing else.
void LiveHost::post(void* rec, int bytes) {
  thunder_live_ev_head* h = static_cast<thunder_live_ev_head*>(rec);
  h->bytes = (uint16_t)bytes; h->seq = ++evSeq_; h->f += off_;   // the one place an ENGINE frame becomes a HOST frame
  const bool crit = h->type == THUNDER_LIVE_EV_STATE || h->type == THUNDER_LIVE_EV_HELD || h->type == THUNDER_LIVE_EV_SONG || h->type == THUNDER_LIVE_EV_PADS
                    || (h->type == THUNDER_LIVE_EV_ERROR && static_cast<const thunder_live_ev_error*>(rec)->fatal != 0);   // non-fatal ERRORs share the ordinary ring (F3)
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
