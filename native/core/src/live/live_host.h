// THE LIVE HOST (U37 P0: the declaration only; P2 to P5 write the bodies, docs/port/U37-DESIGN-DRAFT.md section 9). One class that assembles what exists but nothing assembles: the sequencer's
// session and snapshots, the runner, the pads snapshot, the players, the mixer, the effects, the master chain, the meters and the MIDI scheduler, into the block loop of a real-time host.
// The C API (include/thunder/live.h) is a thin shell around it: every function of live.h is one method here, and live.h's comments are the contract these comments explain.
//
// TWO ROLES. CONTROL: one thread at a time (a shell's control thread; the web's message handlers). AUDIO: process() only. In the web both are the one rendering thread, never at once.
// REAL-TIME RULES: process() and everything it calls allocate nothing, take no lock, free nothing, do no I/O, throw nothing. A CONTROL method NEVER aborts on a failed allocation: it uses new (std::nothrow) and
// returns an error code (the places a build without exceptions cannot catch are listed in docs/port/U37-P0-NOTES.md, question 4). Everything the audio side must not free travels back to the control side through a
// ring and is freed by collect().
//
// MEMBERS BY OWNERSHIP (the section "STATE" below): each member says who writes it and who reads it, and for the shared ones the memory order. A member with no order named is touched by ONE role only.
#ifndef THUNDER_LIVE_LIVE_HOST_H
#define THUNDER_LIVE_LIVE_HOST_H
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

#include "thunder/live.h"
#include "thunder/midi.h"

#include "chains/chost.h"
#include "fx/fxbank.h"
#include "master/chain.h"
#include "meter/meter.h"
#include "midi/midi_host.h"
#include "mix/mix.h"
#include "mix/pdc.h"
#include "render/pads_snap.h"   // PR #276 (n1/u37-p1-pads): PadsHost, PadsSnap, PadBuf, PadVarIn
#include "render/players.h"
#include "rt/json.h"
#include "rt/snap.h"
#include "rt/spsc.h"
#include "seq/runner.h"
#include "seq/seq.h"
#include "seq/session.h"
#include "sf/sf.h"
#include "song/song.h"

namespace thunder {

// ---- the command ring ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// A command is one fixed-size record (no allocation to queue it, none to apply it). Control produces, audio consumes: Spsc<LiveCmd, kLiveCmdRing> (rt/spsc.h: release on push, acquire on pop; a full ring is E_BUSY).
// Applied in ring order at step 1 of a quantum, once due (see `at`); the waiting list keeps the timed ones.
enum class LiveCmdType : uint8_t {
  SongAck,      // an IDLE song_publish: post the SONG record for `a` (the rev) at the next quantum. i64a = rev
  Play,         // p = PlayPlan* (ownership goes to the audio side, comes back through playBack_); i64a = t0 frame (< 0: none); i0 = spp; i1 = pdc mode
  Stop,         // at = when
  Hold,         // at
  Loop,         // i0 = has, i1 = y0, i2 = y1
  Bpm,          // x = bpm
  Lead,         // x = seconds
  Perf,         // i0 = what (THUNDER_LIVE_PERF_*), i1 = index, x = v
  StopTrack,    // i0 = track; at = when (also the time of the voices' release)
  MixSet,       // i0 = track, i1 = key (MixKey as int), i2 = Send's index (always 0), x = value, y = ramp frames; at = when
  FxCommit,     // i0 = track; p = FxPlan* (ownership to the audio side; the plan comes back through fxBack_ after the commit)
  Meters,       // x = rate Hz (0: off)
  ChainsCommit, // p = ChainsPlan* (ownership goes to the audio side; the plan comes back through chBack_ after the commit); at = NOW always: it never waits in pend_
};
struct LiveCmd {
  LiveCmdType type = LiveCmdType::Stop;
  uint8_t reserved[3] = {0, 0, 0};
  int32_t i0 = 0;
  int64_t at = -1;        // a frame on the host's timeline; -1 (THUNDER_LIVE_NOW): next quantum
  int64_t i64a = 0;
  int32_t i1 = 0, i2 = 0;
  double x = 0, y = 0;
  void* p = nullptr;
};
static_assert(sizeof(void*) != 8 || sizeof(LiveCmd) == 56, "LiveCmd is 56 bytes on a 64-bit host (kept small: 256 of them are the ring)");
constexpr uint32_t kLiveCmdRing = THUNDER_LIVE_CMD_RING;       // 256: Spsc needs a power of two
constexpr int kLiveCmdWait = THUNDER_LIVE_CMD_WAIT;            // 64: timed commands waiting (audio only), ordered by `at`, equal `at` in ring order
constexpr uint32_t kLiveFxParamRing = THUNDER_LIVE_FXPARAM_RING;   // 64
constexpr int kLiveQuantum = kRenderQuantum;                   // 128

// fx_param does not fit a LiveCmd (12 doubles): its own ring, drained in step 1 in the same pass as the commands (a param with `at` goes into FxHost::psetAt, which keeps its own 256-entry queue by time).
struct LiveFxParam { int32_t track = 0, slot = 0; int64_t at = -1; double p[12] = {0}; };
/* PP: thunder_live_fx_values. One fixed-size record (copied, ~1.2 KB: a pump slot's value list is up to 280 floats): the pump values as float32 (what the plugin takes), the modular ones as doubles (what FxEntry::mp holds). */
constexpr uint32_t kLiveFxValuesRing = THUNDER_LIVE_FXVALUES_RING;   // 16
struct LiveFxValues { int32_t track = 0, slot = 0, kind = 0, n = 0; int64_t at = -1; float pv[kPumpMaxParams] = {0}; double mp[kModMaxParams] = {0}; };

// The chains host's parameter edits (C2 R8): one record per `param` op, in its own ring, drained at step 1 AFTER the commands (a param sent after a commit meets the new patch). id / name are NUL-terminated and fit
// (the control side judges: an id or a name that does not fit is not representable). Every node gets every record (ChainsHost::param).
struct LiveChainParam { char id[48]; char name[32]; double v; uint32_t gen; };   // gen: the control side's count of WHOLE-PATCH plans handed over when the record was pushed (V3a)
constexpr uint32_t kLiveChainRing = THUNDER_LIVE_CHAIN_RING;   // 256

// Everything a PLAY needs that is made on the control side. Heap object, created by play(), handed to the audio side in LiveCmd::p, returned through playBack_ and deleted by collect().
struct PlayPlan {
  std::shared_ptr<const Song> song;                  // mixer.load(*song) at the play (audio, at once); the Seq itself travels in the SongSnap published by the same call
  MasterChainConfig master;                          // master_chain_from_song(song.mst, song.limiter); used when F_MASTER
  FxPlan* fx[kSongNT + 1] = {nullptr};               // the song's track and master effects (FxHost::planFromSong); null: the track keeps what it has; committed at the play (audio), then returned
  bool hasMaster = false;
  ChainsPlan* chains = nullptr; ChainsHost* chainsHost = nullptr;   // C2: the play's chains plan (null: none needed), committed at the play (audio) or at its drop, returned with the PlayPlan; ~PlayPlan settles and deletes it (control; chainsHost outlives every plan)
  PdcPlan pdc; PdcMode pdcMode = PdcMode::Auto;      // pdc_plan(bus, slots of `fx` as planned, sr, mode)
  int64_t t0 = -1; int spp = 0;
  uint64_t cfgOrd = 0;                               // P3: the control side's ordinal of this play's master / PDC configuration; a pdc_set / master_config published later has a higher one and wins (one counter, `cfgOrd_`)
  const SeqSession* sess = nullptr;                  // P2: identity of the PLAY's session: applyPlay starts the runner only on a snapshot of THIS session (an adopt stalled by a full return ring leaves the old Seq current)
  ~PlayPlan();                                       // control: deletes any plan the audio side did not take; a chains plan is settled first (committed or abandoned: ChainsHost::settle) and deleted
};

// A PDC plan or a master configuration published beside the song: one slot each (rt/snap.h SnapSlot<T>, plain adopt()), adopted at step 2.
struct LivePdcMsg { PdcPlan plan; PdcMode mode = PdcMode::Auto; uint64_t ord = 0; };   // ord: see PlayPlan::cfgOrd
struct LiveBusMsg { BusPlan plan; };                 // built from the song's chain patch by song_publish; published with it
struct LiveMasterMsg { MasterChainConfig cfg; uint64_t ord = 0; };

// An event record waiting for poll(): fixed slots so that the audio side writes one without allocating. `bytes` of `data` are valid (thunder_live_ev_* of live.h).
struct LiveEvSlot { uint16_t bytes = 0; alignas(8) uint8_t data[THUNDER_LIVE_EV_MAX] = {0}; };   // (the record is 8-aligned in the slot)
constexpr uint32_t kLiveEvRing = 1024;               // 330 KB; a full ring refuses the NEWEST record (stats.evt_dropped); the critical ring is 64 records

// One step of the test dump (F_DUMP_STEPS): the head says how many raw events follow in the event ring. Raw SeqEvent copies (456 B each); the control side turns them into JSON in dumpStep().
struct LiveDumpHead { int64_t frame = 0; double t = 0, bpm0 = 0; int32_t tks = 0, n = 0; uint64_t validMask = 0 /* P2: the index of the step's first event in the circular event array */; double cTime = 0, cBase = 0, cBt = 0, cBtT = 0, cBpm = 0; /* P2: the Seq's clock after the step (the live-clock goldens hash it) */ };

// A font handed in with font_add: the bytes (sf::parse keeps a VIEW of them) and the parsed tables in one object, so that a shared_ptr<const sf::Font> that aliases `font` keeps the bytes alive too.
struct LiveFont { std::vector<uint8_t> bytes; sf::Font font; size_t held = 0; uint64_t used = 0, workUsed = 0; };

class LiveHost {
 public:
  // ======================================================================================================================================================================================================
  // CONTROL: life
  // ======================================================================================================================================================================================================
  // create(): thunder_live_create. Makes Players(&emptyPads, sr, maxVoices) (players.h:32) FIRST, setDropLate(true) (players.h:41, ruling D-4), then PadsHost::create(*players_) (pads_snap.h; it binds to that Players' voice pools and publishes an EMPTY snapshot, nullptr when the pools exceed kBoardMax), Mixer(sr, 128) (mix.h:94), the SongSnap slot,
  // the rings, the event buffers (SeqEvent[kRenderEvents], SeqStep), and by flag: thunder_midi_create(sr, err, errCap) (midi.h:79), FxBank(sr) + attach(mix, true) (fxbank.h:27), MasterChain(sr, 128) (chain.h:42) + Mixer::setMasterFx (mix.h:113,
  // chain.h:57), MeterBank(sr) (meter.h:40). LivePool::setIdleSkip stays on (live_pool.h:116). pe_prepare() (engine/layer_pe.h:20) is the audio thread's duty (process() does it on its first call).
  // nullptr, with `err`, on a bad option or no memory (nothing left allocated).
  static LiveHost* create(const thunder_live_opts& o, char* err, size_t errCap);
  // ~LiveHost(): the audio thread has stopped. No quantum is run (P4: the stop a shell owes is thunder_live_stop + one more process + a drain BEFORE destroy; a quantum here could not be seen). Frees everything: PadsHost's
  // destructor frees every buffer whatever the voices say (pads_snap.h), the SnapSlots free their snapshots (rt/snap.h:22), thunder_midi_free (midi.h).
  ~LiveHost();
  LiveHost(const LiveHost&) = delete;
  LiveHost& operator=(const LiveHost&) = delete;

  // collect(): thunder_live_collect. songSlot_.collect() (snap.h:24), padsHost_->collect() (pads_snap.h), pops playBack_ / fxBack_ and deletes them, empties the retired-song list of the midi handle through its own control
  // calls (a later thunder_midi_* control call does that: midi_api.cpp reap()). Returns the objects freed.
  int collect();

  // ======================================================================================================================================================================================================
  // CONTROL: song
  // ======================================================================================================================================================================================================
  // songPublish(): thunder_live_song_publish. collect(); Song::load(text, len, &err, /*pageS*/ true) (song.h:92) into a new Song (new (std::nothrow)); vars via Json parse + SeqVars::load (seq.h:53) into varsCur_;
  // bus_plan(song.chainPatch, song.chainOn) (mix.h:70) into a LiveBusMsg. When F_MIDI: thunder_midi_set_song(midi_, text, len) (midi.h:84). Then by control state:
  //   idle:    songCur_ / varsCur_ replaced; a SongAck command queued (E_BUSY if the ring is full: the publish itself is kept).
  //   playing: SeqSession::publish(songSlot_, songCur_, varsCur_) (session.h:51): false = ring full -> E_BUSY, songCur_ unchanged; then busSlot_.publish(). sess_->sessionFull() (session.h:63) sets the stats flag and the
  //            one-time SESSION_FULL record request (audio posts it: a control-side flag in `noteSessionFull_`, read by the audio side with relaxed order).
  int songPublish(const uint8_t* song, size_t n, const uint8_t* vars, size_t varsN, int64_t rev, char* err, size_t errCap);

  // ======================================================================================================================================================================================================
  // CONTROL: pads (all of them stage into padsHost_; publish() is the only one the audio side sees)
  // ======================================================================================================================================================================================================
  // padSet(): PadsHost::setPad(pad, l, r, (int)frames, rate, nullptr, 0) (pads_snap.h). The budget is checked FIRST: heldBytes() + frames * (r ? 8 : 4) > budget_ -> E_LIMIT (before any allocation).
  int padSet(int pad, const float* l, const float* r, int64_t frames, double rate);
  // padTake(): NEEDS PadsHost::setPadTake(pad, float* mem, stereo, frames, rate) (NEW in P2, about 30 lines: PadBuf::mem is `new float[]` and freed with delete[] (pads_snap.cpp freeBuf), so a block from
  // thunder_live_alloc (= new (std::nothrow) float[]) is adopted as `mem` as it is; the finite check runs in place; no identical-data shortcut). Without it: E_UNSUPPORTED (THUNDER_LIVE_CAP_TAKE off).
  int padTake(int pad, float* block, bool stereo, int64_t frames, double rate);
  // padKit(): PadsHost::setKit (pads_snap.h); padRoute(): setRoute (pads_snap.h). No allocation.
  int padKit(int pad, int choke, int syn, double rel);
  int padRoute(int pad, int route);
  // varSet(): a MUT / GEN variation. PadsHost has no standalone variation call: its variations travel with setPad (pads_snap.h, PadVarIn). LiveHost keeps, per pad, the pad's CURRENT samples (read from
  // padsHost_->padBuf(pad)->L() / R(), pads_snap.h) and the list of its variations (their data stays in the PadBuf; only the list of PadVarIn is rebuilt), and calls setPad again with the full list: one pad copy per
  // call until P1's open question 3 (variations as their own refcounted object) is done. 4096 variations at most.
  int varSet(int ii, int kind, int num, int pad, const float* l, int64_t frames, double rate);
  // padChop() / padHits(): padsHost_->chop().setLoop(pad, secs) / setHits(pad, n, pts, count) (seq/chop.h:21; pads_snap.h).
  int padChop(int pad, double loopSecs);
  int padHits(int pad, int nSlices, const double* pts, int count);
  // fontAdd(): LiveFont (bytes copied), sf::parse(bytes, n, font, false) (render_api.cpp:415 is the model); budget: n + font.bytesUsed(). fontRemove(): E_ARG while a pad uses it.
  // padSf(): sf::sf_preset(font, bank, prog), sf::prepare_preset(font, pr, DecodeOpts, pcm, keys) (render_api.cpp:463 is the model; presetBytes limited to the room the budget leaves), then
  // PadsHost::setSf(pad, shared_ptr<const sf::Font> aliasing LiveFont::font, pr, shared_ptr<const sf::PresetPcm>, P) (pads_snap.h). padSfClear(): PadsHost::clearSf (pads_snap.h).
  int fontAdd(const uint8_t* bytes, size_t n);
  int fontRemove(int fontId);
  int padSf(int pad, int fontId, int bank, int prog, const thunder_render_sf_params* params, const uint8_t* keys, size_t nkeys);
  int padSfClear(int pad);
  // padLive(): LivePool::addPad(pad, LivePadConfig) (live_pool.h:71) for kinds 1..4 (engine_json parse of the voice JSON as render_api.cpp thunder_render_set_pad_live does). NO C++ SUPPORT for calling it while
  // the audio thread runs: Players owns its LivePool BY VALUE (players.h:61 live_). P2 builds the swappable pool (a LivePool* in Players and a second pool built beside, published with the pads snapshot); until then
  // the method answers E_STATE unless idle_ (control's view: no play issued since the last stop was acknowledged by a STOPPED record AND the audio thread is not running a quantum: a host that calls process
  // concurrently cannot be idle; see notes question 3).
  int padLive(int pad, int kind, const uint8_t* json, size_t n, const thunder_render_live_params* params);
  int padLiveClear(int pad);
  // padsPublish(): collect(); PadsHost::publish() (pads_snap.h): kFull -> E_BUSY, kNoMem -> E_MEMORY.
  int padsPublish();

  // ======================================================================================================================================================================================================
  // CONTROL: transport
  // ======================================================================================================================================================================================================
  // play(): needs songCur_ (else E_STATE) and no uncommitted staged fx plan. collect(). sess_ = SeqSession::create(mode, SeqOpts{row, loop, once = false, live = false, sim = false, only, track, sect, phrase, seed,
  // randomSeed, chop = nullptr (beginQuantum sets it each quantum)}) (session.h:37); sess_->publish(songSlot_, songCur_, varsCur_) (session.h:51; the first snapshot of a session is the starting Seq); spp = o.spp >= 0 ? o.spp :
  // midi_spp_for(*song, vars, row) (midi_spp.h:11: STEPS A SEQ, allocates); PlayPlan: master_chain_from_song(song.mst, song.limiter) (chain.h:36); per track FxHost::planFromSong(song.tfx[t] / song.mfx, bpm, 0) (fxhost.h:130, with
  // F_EFFECTS); pdc_plan(bus_plan(...), PdcSlot[] built from the FxPlan entries (FxPlan::entry(i): lat, bi >= 0, on; NOT FxBank::pdcSlots, which reads the audio thread's chain_), sr, mode) (pdc.h:39). Queues LiveCmd::Play.
  // controlPlaying_ = true. E_BUSY after the session was made: sess_ is dropped, the snapshot deleted by publish's rule (session.h:51), nothing else changed.
  int play(const thunder_live_play_opts& o);
  void testSetEvSeq(uint32_t v) { evSeq_ = v; }                 // TEST ONLY (no audio thread running): starts the record numbering at `v` (the 2^32 wrap)
  void setTestSeam(void (*f)(void*), void* user) { testSeam_ = f; testSeamUser_ = user; }   // TEST ONLY: `f` runs inside play(), between the push of the Play command and the publish of its snapshot (verify-283 F9)
  void setTestFxSeam(void (*f)(void*), void* user) { testFxSeam_ = f; testFxSeamUser_ = user; }   // TEST ONLY (single thread; P3b): `f` runs on the audio side between a quantum's command loop and the drain of fxParams_ (the window where the control side pushes fx_plan + fx_commit and then fx_param)
  void setTestCommandSeam(void (*f)(void*), void* user) { testCmdSeam_ = f; testCmdSeamUser_ = user; }   // TEST ONLY (single thread): `f` runs on the audio side between a quantum's command loop and its parameter drain (C2 L1: the window where the control side pushes a commit and then records)
  void setTestAudioSeam(void (*f)(void*), void* user) { testAudioSeam_ = f; testAudioSeamUser_ = user; }   // TEST ONLY (single thread): `f` runs on the audio side right after a started play was given back to the control side (R1: the window between giveBack and the state's store)
  void setTestTap(void (*f)(void*, Mixer&, int64_t), void* user) { testTap_ = f; testTapUser_ = user; }   // TEST ONLY (single thread): `f` runs in step 8 right after the voices were rendered into the mixer's inputs, with the ENGINE frame of the quantum (SongRenderer::setInputTap's place): the chains goldens' input signals
  size_t chEditedForTest() const { return chEdited_.size(); }   // TEST ONLY (control thread): the (id, name) pairs chain_edit remembers
  ChainsHost* chainsHostForTest() { return chains_.get(); }   // TEST ONLY: the chains host (events are injected, nodes inspected, only while no audio thread runs)
  const Mixer& mixerForTest() const { return *mix_; }   // TEST ONLY: the mixer (its bus plan is the commit's alone with a chains host; read only while no audio thread runs)
  // stop(): queues LiveCmd::Stop; controlPlaying_ = false (a later songPublish no longer reaches the sequencer).
  int stop(int64_t at);
  int hold(int64_t at);                              // LiveCmd::Hold
  int loop(bool has, int y0, int y1, int64_t at);    // LiveCmd::Loop: Seq::setLoop (seq.h:146)
  int bpm(double bpm, int64_t at);                   // LiveCmd::Bpm: Seq::setBpm (seq.h:159)
  int lead(double seconds, int64_t at);              // LiveCmd::Lead: lead_ = seconds (the runner's `lead` argument; render.h:173 `lead`)
  int perf(int what, int index, double v, int64_t at);   // LiveCmd::Perf: Seq::setXf / setMac / setFill (seq.h:170-172)
  int stopTrack(int track, int64_t at);              // LiveCmd::StopTrack: Players::stopTrack (players.h:47)

  // ======================================================================================================================================================================================================
  // CONTROL: mixer, effects, master
  // ======================================================================================================================================================================================================
  // mixSet(): LiveCmd::MixSet -> Mixer::set(MixSet{tr, key, i, v, ramp, at / sr}) (mix.h:132).
  int mixSet(int track, int key, double value, double ramp, int64_t at);
  // pdcSet(): pdc_plan(busCur_, slots from the staged / committed plans' entries, sr, mode) (pdc.h:39) into a LivePdcMsg, published on pdcSlot_; the audio side applies it with pdc_apply(mix_, plan, PdcApply{mode, live = true,
  // playing, now}) (pdc.h:48) at step 2.
  int pdcSet(int mode);
  // fxPlan(): FxBank::host(t).planFromSong(Json slots, bpm, 0) (fxhost.h:130) with the slots JSON parsed into a JsonDoc kept until the commit; the FxPlan is staged in stagedFx_[t]. fxCommit(): LiveCmd::FxCommit with the plan.
  // NO C++ SUPPORT FOR THE WHOLE PATH: FxBank::load (fxbank.h:25) commits "now" (offline); the live path calls host(t).plan / commit directly (both public through FxBank::host, fxbank.h:21). FxHost::plan reuses objects of the
  // previous plan (fxhost.h:128), so every plan is committed in order (the E_STATE rule of live.h).
  int fxPlan(int track, const uint8_t* json, size_t n, double bpm);
  int fxCommit(int track, int64_t at);
  // fxParam(): pushes a LiveFxParam on fxParams_; the audio side calls FxHost::psetAt(at / sr, slot, p12) (fxhost.h:135).
  int fxParam(int track, int slot, const double* p12, int64_t at);
  // PP: fxValues(): judged against ppKind_ (the last planned chain), pushed on fxValues_; the audio side calls FxHost::psetPumpRt / psetModRt (fxhost.h) at the top of the quantum, after the commands (docs/port/PP-NOTES.md).
  int fxValues(int track, int slot, int kind, const double* v, int n, int64_t at);
  // masterConfig(): master_chain_from_song(Json mst, limiter) (chain.h:36) into a LiveMasterMsg on masterSlot_; the audio side calls MasterChain::configure (chain.h:44) at step 2. NOT BUILT: configure is "allocation-free, states kept"
  // but SongRenderer only calls it before the first block (render.h:128), a latency change has no PDC / output-delay path, and the quantum buffer's latency report would jump. Returns E_UNSUPPORTED until P3 builds it.
  int masterConfig(const uint8_t* mst, size_t n, bool safety);

  // ======================================================================================================================================================================================================
  // CONTROL: chains (C2; F_CHAINS)
  // ======================================================================================================================================================================================================
  // chainEdit(): thunder_live_chain_edit: the page's chnDiff `param` ops as records into chainParams_ (all or nothing); the audio side calls ChainsHost::param for each at step 1 (after the commands). chainsSong_ does NOT move.
  int chainEdit(const uint8_t* ops, size_t n, int64_t at);

  // ======================================================================================================================================================================================================
  // CONTROL: MIDI, events, test
  // ======================================================================================================================================================================================================
  thunder_midi* midi() { return midi_; }             // owned; valid until ~LiveHost
  int midiLead(double seconds);                      // thunder_midi_set_lead (0.005 .. 1.2: the cap went from 0.5 s to 1.2 s in P4, ruling D6); E_STATE without F_MIDI, E_ARG out of range, E_BUSY when the midi command ring is full
  int64_t midiDrain(thunder_midi_msg* out, int64_t cap);   // thunder_live_midi_drain: thunder_midi_drain (CONSUMER, one thread); E_STATE without F_MIDI
  int meters(double rateHz);                         // LiveCmd::Meters: MeterBank::setRate (meter.h:44)
  void hostStats(int outLatencyFrames, int underruns) { outLat_.store(outLatencyFrames, std::memory_order_relaxed); under_.store(underruns, std::memory_order_relaxed); }   // ANY thread
  int poll(void* buf, int cap);                      // CONSUMER: pops evRing_ and copies whole records
  int dumpStep(char* json, int cap);                 // TEST: pops dumpHeads_ / dumpEvs_, writes the golden's JSON (tests/golden/seq-*.json format)
  int64_t engineOffset() const { return offPub_.load(std::memory_order_relaxed); }   // ANY: hostFrame - engineFrame, ONE 64-bit atomic written by process() when it changes (first anchor, re-anchor); thunder_live_engine_offset
  bool statsGet(thunder_live_stats* out) const;      // ANY: relaxed loads of pub_[] + the control side's own counters + padsHost_->buffers() / retired() (pads_snap.h) + bytes held

  // ======================================================================================================================================================================================================
  // AUDIO
  // ======================================================================================================================================================================================================
  // process(): thunder_live_process. Validates; re-anchors if frame0 != expected (counts it); then like SongRenderer::block (render.cpp:83): while frames remain, render a quantum into q_ when qRead_ is at the end
  // (quantum()), copy what the call needs. Frames > 128 are cut into quanta; the samples do not depend on the call size.
  int process(float* l, float* r, int frames, int64_t frame0, int64_t wallNs);

 private:
  LiveHost() = default;

  // ---- the quantum (audio): the nine steps of live.h, in order (P2: the bodies are in live_host.cpp; the signatures below are the ones the bodies needed, the contract text is the comment of each) -----------------------
  void quantum();                                    // template: SongRenderer::quantum (render.cpp:93-120)
  void applyCommands(int64_t qStart, int64_t qEnd);  // step 1: pop cmds_ into the waiting list / apply what is due, in ring order (see applyOne)
  void applyOne(const LiveCmd& c, int64_t qStart);   // the switch: Play -> applyPlay; Stop -> runner stop, players_->stopAll(at), mix_->songLanesReset(), clockStop(at), state IDLE, STATE record; Hold -> runner_.hold();
                                                     // Loop / Bpm / Perf -> the running Seq's setters; Lead -> lead_; StopTrack -> players_->stopTrack; MixSet -> mix_->set; SongAck -> a SONG record. (P3: FxCommit, Meters)
  void applyPlay(PlayPlan* p, int64_t qStart);       // adopt the play's snapshot (tryStartPlay), runner_.start, setTime(t0), mix_->load, tempo seed, STATE PLAYING; a play whose snapshot cannot be adopted yet is kept (stalledPlay_)
  bool tryStartPlay(PlayPlan* p, int64_t qStart);
  void retryStalledPlay(int64_t qStart);
  void noteSongAdopted(int64_t qStart);              // ONE SONG record for the newest rev adopted (none for the snapshot an IDLE publish already acknowledged)
  void adopt(int64_t qStart);                        // step 2: seq_adopt; seqGone() if the Seq was dropped; padsHost_->beginQuantum(*players_, runner_.seq()) every quantum; busSlot_; PADS record
  void dispatch(const SeqEvent& e, const Seq& seq, const Song& song);   // SongRenderer::dispatch (render.cpp:28) without the effects bank
  SeqPump stepLoop(double t0, int64_t qStart);       // step 5: runner_.pump(t0, lead_, evs_, kRenderEvents, step_, onStep); onStep: dump copy, ROWS bookkeeping, then dispatch() each event
  void notePos(const SeqStep& st);                   // a played step: the ROWS record and the POS bookkeeping
  void heldRecord(double heldT, int64_t qStart);     // EXACT STOP: the HELD record
  void dumpCopy(const SeqEvent* evs, int k, const SeqStep& st, const Seq& sq, int64_t qStart);   // F_DUMP_STEPS
  void songEnded(double t0, int64_t qStart, const SeqPump& pr);   // step 7: Players::songOver once, STATE ENDED, the 0.3 s timer -> IDLE, STOPPED arg 1
  void render(double t0, int64_t qStart);            // step 8: players_->render into the mixer's inputs, mix_->render
  void post(void* rec, int bytes);                   // step 9: stamp `bytes` and `seq`, push into evCrit_ (STATE, HELD, SONG, PADS, ERROR) or evRing_; a full ring refuses the newest and counts it
  void postState(int64_t f, int state, int arg);
  void postSong(int64_t rev, int64_t f);
  void afterBlock(int64_t qStart);                   // step 9: CLOCK (10/s), POS (30/s), LATE (4/s), publish pub_[], state_ (P3: METERS)
  void publish();                                    // pub_[] <- the audio side's counters (relaxed stores)
  void reanchor(int64_t frame0);                     // process(): the host timeline moves (off_ only: nothing in the engine does), count, ERROR record (live.h TIME)
  void dropSeq();                                    // the runner lets go of its Seq (stop, ENDED timeout, a new play); seqGone() follows in the next adopt()
  void giveBack(PlayPlan* p);                        // a PlayPlan back to the control side (playBack_)
  // ---- P3: effects, master chain, PDC, meters (audio) ----
  void commitFxLive(int track, FxPlan* pl, int64_t qStart);   // FxBank::commitLive + the master hook rule; the plan stays with its owner (PlayPlan / the FxCommit command, which returns it)
  void commitPlayFx(PlayPlan* p, int64_t qStart);             // every plan of a play, master first then the tracks (the order FxBank::load commits in); null entries are left alone
  void commitWaitingFx(int64_t qStart, int track = -1);       // the FxCommits waiting in pend_ (all tracks or one) are committed in list order and returned; marked done (p = nullptr), not removed
  void dropPlay(PlayPlan* p, int64_t qStart);                 // a play that never started: its plans are committed anyway (the control side planned them against each other: FxHost::plan's rule), then it goes back
  void setMasterHook();                                       // Mixer::setMasterFx: the hook is on while the master chain exists or the master effects host has slots (SongRenderer::setEffects / setMasterChain)
  static void masterHook(void* user, int track, float* L, float* R, int frames, double t0, const MixFeed* feeds, int nFeeds);   // SongRenderer::masterHook
  void applyPdc(int64_t qStart, bool nowGlide);               // pdc_apply(mix, pdcCur_, ...) at the mixer's time
  void applyFxValues(int64_t qStart, uint32_t n);                        // PP: step 1, right after applyFxParams: drain fxValues_ (at most the n records of the pre-loop snapshot) into FxHost::psetPumpRt / psetModRt
  void applyFxParams(int64_t qStart, uint32_t n);                        // step 1: drain fxParams_ into FxHost::psetAt (after the commands, so a param sent after a commit meets the new chain)
  void clearWaiting(int64_t qStart);                          // a stop / a started play: the waiting list is cleared; an FxCommit in it is committed first (its plan must not be lost)
  int64_t latencyFrames() const;                              // the output's lateness behind the song: master chain + PDC trim + master pump effects (SongRenderer::latencyFrames)
  void postMeters();                                          // step 9: the bank's events as METERS records
  // ---- C2: the chains host (audio) ----
  void commitChains(ChainsPlan* p);                                    // AUDIO: ChainsHost::commit + the whole-patch count (V3a)
  void applyChainParams(uint32_t n);                                    // step 1, after the commands (and after a stalled play has started): drain chainParams_ into ChainsHost::param
  // ---- P4: MIDI (audio) ----
  void midiOp(bool start, int spp);                           // step 1: queue a MIDI Start (play) or Stop (stop command) for step 4, in the order the commands were applied
  void midiBegin(double t0, int64_t qStart);                                  // step 4: the ENDED timer's stop, midi_begin, then the queued Starts / Stops DIRECTLY on the scheduler (same `now`, same figures, before the step loop)
  void midiEnd(double t0);                                    // step 6: midi_end(t0, the last Seq's time and tempo, alive)
  // ---- control helpers ----
  int push(const LiveCmd& c);                        // cmds_.push; E_BUSY + cmd_busy when full
  void refreshCtl();                                 // the control-side numbers statsGet reads
  void dropStagedVars(int pad);
  void noteFxPlanned(int track, const FxPlan* p);    // P3: the pump slots of the track's newest plan, for pdc_plan (control side)
  // C2 (control): what a play / a publish has to do for the chains, decided against chainsSong_ (R3) and made (R4..R6). chainsDecide() makes the plan (and the param records), changes NOTHING of the host's own state; chainsFinish()
  // moves chainsSong_ / chainsBus_ / chainsFeed_ forward and pushes the param records, only once the call can no longer fail; chainsAbandon() settles and deletes a plan that was not handed over.
  struct ChainsWork { ChainsPlan* plan = nullptr; bool hasPlan = false; uint32_t nodeMask = 0; std::vector<LiveChainParam> params; bool move = false; uint32_t feed = 0; BusPlan bus; std::shared_ptr<const Song> song; bool full = false; };
  int chainsDecide(const std::shared_ptr<const Song>& S, ChainsWork& w);   // 0, E_BUSY (a plan is in flight, or the edit ring has no room: counted), E_MEMORY; on an error `w` owns nothing
  void chainsAbandon(ChainsWork& w);
  void chainsFinish(ChainsWork& w, bool asCommand);   // asCommand: push the plan as a ChainsCommit (a publish); a play's plan rides in its PlayPlan

  // ============================================================================================================================================================================================
  // STATE
  // ============================================================================================================================================================================================
  // ---- constants (written in create(), read by both: no order needed) ----
  double sr_ = 48000; int maxBlock_ = 128; uint32_t flags_ = 0; uint64_t budget_ = 0, maxSongBytes_ = 0;

  // ---- CONTROL ONLY ----
  std::shared_ptr<const Song> songCur_;              // the last published Song; play() and songPublish read it
  JsonDoc varsDoc_; SeqVars varsCur_;                // the last published vars (SongSnap holds its own copy of SeqVars)
  BusPlan busCur_;                                   // bus_plan of songCur_ (pdcSet and play use it)
  std::shared_ptr<SeqSession> sess_;                 // THIS play's session; null while idle
  void (*testSeam_)(void*) = nullptr; void* testSeamUser_ = nullptr;   // TEST ONLY: called in play() between the two statements of F9
  bool controlPlaying_ = false;                      // control's view: a play was issued and no stop
  std::shared_ptr<const Song> chainsSong_;           // C2 CONTROL: the song whose chain patch and CHN flags the audio side has, or will have once what is in flight lands (moves forward only when a call returns OK)
  BusPlan chainsBus_; uint32_t chainsFeed_ = 0;      // C2 CONTROL: bus_plan(chainsSong_) and chains_feed(chainsSong_); a host that never planned: the default plan, 0
  uint32_t chFullSeq_ = 0;                           // C2 CONTROL (V3a): whole-patch plans handed over so far; every record is stamped with it AFTER a plan of the same call counted
  std::vector<std::pair<std::string, std::string>> chEdited_;   // C2 CONTROL (V2): the (module id, param name) pairs chain_edit sent since chainsSong_ last moved (at most 256; a 257th sets chEditAll_)
  bool chEditAll_ = false;
  bool chainsPlanned_ = false;                       // C2 CONTROL: a plan that makes nodes was ever handed over (R3 (a))
  FxPlan* stagedFx_[kSongNT + 1] = {nullptr};        // fxPlan() staged, fxCommit() moved to the ring
  int fxOut_ = 0;                                    // P3: FxCommit commands issued and their plans not yet collected (fxBack_ holds 64)
  /* PP CONTROL: the kind and value count of each slot of each track's (and the master's) LAST PLANNED chain, by the slot number: 0 none / tfx / core, THUNDER_LIVE_FXV_PUMP, THUNDER_LIVE_FXV_MOD. Written by ppNote (fxPlan, play), read by fxValues. */
  struct PpSlot { int16_t kind = 0, n = 0; };
  PpSlot ppKind_[kSongNT + 1][kFxSlots];
  void ppNote(int track, const FxPlan* p);
  LiveFxValues fxvScratch_;                          // PP AUDIO: the record being applied (a member: 1.2 KB stays off the audio stack)
  PdcSlot ctlPdc_[kSongNT][kFxSlots]; int ctlPdcN_[kSongNT] = {0};   // P3: the pump slots of each track's LAST PLANNED chain (what pdc_plan reads; FxPlan::entry, not the audio thread's chain_)
  void (*testCmdSeam_)(void*) = nullptr; void* testCmdSeamUser_ = nullptr;
  void (*testFxSeam_)(void*) = nullptr; void* testFxSeamUser_ = nullptr;   // TEST ONLY (P3b)
  void (*testAudioSeam_)(void*) = nullptr; void* testAudioSeamUser_ = nullptr;
  void (*testTap_)(void*, Mixer&, int64_t) = nullptr; void* testTapUser_ = nullptr;   // TEST ONLY: setTestTap
  std::unique_ptr<LiveFont> fonts_[THUNDER_LIVE_MAX_FONTS];
  int sfFont_[kRenderPads]; size_t sfBytes_[kRenderPads];   // which font a pad's preset came from; its PCM bytes (for fontRemove and the budget)
  size_t liveBytes_[kRenderPads];                    // LivePool::padBytes (live_pool.h:75) of each live pad
  size_t fontBytes_ = 0, liveBytesTotal_ = 0;        // the rest of the budget besides PadsHost's buffers
  // P2: the control side's numbers that statsGet (ANY thread) reads: atomics, relaxed (each a valid value, the set not one instant)
  std::atomic<uint32_t> cmdBusy_{0};                 // stats.cmd_busy
  std::atomic<uint64_t> ctlPadHeld_{0}, ctlPadRetired_{0}, ctlSessionBytes_{0}, songPublished_{0};
  std::atomic<int32_t> ctlPadBuffers_{0}, ctlPadRetiredN_{0};
  std::atomic<uint32_t> sessionFullFlag_{0};
  std::atomic<uint32_t> chPlans_{0}, chRefused_{0}, chOver_{0}, chBusy_{0};   // C2: stats.chains_plans / _refused / _over_budget / _busy (control writes, any thread reads)
  uint64_t songOrd_ = 0;                             // the ordinal of the last snapshot published (SongSnap::ord)
  bool sessionFullSeen_ = false;                     // control: the ERROR SESSION_FULL request was made for this play
  // P2: staged pad changes (control only). `padsDirty_`: something was staged since the last publish. Variations are staged here (ruling 6) and handed to PadsHost::setPad once per pad at padsPublish().
  uint64_t cfgOrd_ = 0;                              // P3: the ordinal of the last PDC / master configuration issued (play, pdc_set, master_config)
  bool padsDirty_ = false;
  struct VarStage { int ii = 0; char kind = 0; int num = 0, frames = 0; double sr = 0; std::vector<float> own; const float* data = nullptr; };   // `data` borrows the pad's current PadBuf (seeded) or points at `own`
  struct PadVars { bool active = false; std::vector<VarStage> v; };
  std::unique_ptr<PadVars[]> padVars_;               // kRenderPads
  size_t stagedVarBytes_ = 0;                        // bytes of the `own` copies: counted against the budget at once
  int64_t songRev_ = 0;                              // the rev of songCur_
  int playOut_ = 0;                                  // PlayPlans issued and not yet collected (play() allows 8: the ring that brings them back holds 8)
  std::atomic<bool> noteSessionFull_{false};         // control writes (relaxed), audio reads once a quantum: post the SESSION_FULL record

  // ---- CONTROL -> AUDIO (single producer, single consumer; Spsc / SnapSlot orders: release on publish, acquire on adopt) ----
  Spsc<LiveCmd, kLiveCmdRing> cmds_;                 // control pushes, audio pops
  Spsc<LiveFxParam, kLiveFxParamRing> fxParams_;     // control pushes, audio pops
  Spsc<LiveFxValues, kLiveFxValuesRing> fxValues_;    // PP: control pushes, audio pops
  SnapSlot<SongSnap> songSlot_;                      // control: SeqSession::publish; audio: seq_adopt; control: collect()
  SnapSlot<LiveBusMsg> busSlot_;                     // plain adopt()
  SnapSlot<LivePdcMsg> pdcSlot_;
  SnapSlot<LiveMasterMsg> masterSlot_;
  Spsc<LiveChainParam, kLiveChainRing> chainParams_; // control pushes, audio pops (applyChainParams)
  uint32_t chFullApplied_ = 0;                       // C2 AUDIO (V3a): whole-patch plans committed so far (ChainsCommit, tryStartPlay, dropPlay); a record with gen below it is stale
  uint64_t chStale_ = 0;                             // C2 AUDIO: stale records dropped
  PlayPlan* stalledPlay_ = nullptr;                  // AUDIO ONLY: a play whose snapshot could not be adopted yet (return ring full): retried at every quantum top
  PadsHost* padsHost_ = nullptr;                     // PadsHost::publish / collect: control; beginQuantum: audio (pads_snap.h): its own atomics (board_) and SnapSlot
  // ---- AUDIO -> CONTROL ----
  Spsc<PlayPlan*, 8> playBack_;                      // spent PlayPlans (audio pushes, control pops in collect)
  Spsc<FxPlan*, 64> fxBack_;                         // committed FxPlans (their `retired_` objects go with them)
  Spsc<ChainsPlan*, 4> chBack_;                      // committed ChainsPlans of a ChainsCommit command (a play's plan comes back inside its PlayPlan): ONE plan is in flight, so room is guaranteed. Pushed after the commit (release), popped by collect() before the settle (acquire)
  // ---- AUDIO -> CONSUMER ----
  Spsc<LiveEvSlot, kLiveEvRing> evRing_;             // audio pushes records, the consumer thread pops (poll)
  Spsc<LiveEvSlot, 64> evCrit_;                      // the CRITICAL records (STATE, HELD, ERROR, SONG, PADS) only: own ring, so a flood of POS / METERS cannot drop one; poll merges the two rings by `seq`
  std::unique_ptr<SeqEvent[]> dumpEvs_; uint32_t dumpEvCap_ = 0;   // F_DUMP_STEPS: raw events, written by audio, read by control after it pops the matching head (the Spsc release / acquire orders them)
  std::unique_ptr<Spsc<LiveDumpHead, 1024>> dumpHeads_;
  uint64_t dumpWr_ = 0; std::atomic<uint64_t> dumpTail_{0};   // F_DUMP_STEPS: events written (audio) / consumed (control, release): the event ring is circular
  // ---- published numbers (audio writes with relaxed stores once a quantum; any thread reads: each value valid, the set not one instant) ----
  std::atomic<uint64_t> pub_[64];                    // stats.* of the audio side, in thunder_live_stats order
  std::atomic<int32_t> state_{THUNDER_LIVE_STATE_IDLE};   // audio writes (release), control reads (acquire): the state after the last quantum
  std::atomic<int32_t> outLat_{0}, under_{0};        // hostStats() writes, the audio side reads relaxed (CLOCK records)

  // ---- AUDIO ONLY (never touched by control after create(); create() builds them before the audio thread exists) ----
  std::unique_ptr<Players> players_;                 // players.h:30
  std::unique_ptr<ChainsHost> chains_;               // F_CHAINS: made by create() (control, before the audio thread); AUDIO: commit / beginBlock / event / param / play / stop / hook; CONTROL: plan / settle / ctlStats. DECLARED BEFORE mix_: destroyed after the mixer, whose hooks point into it (R14); every plan is settled and deleted in ~LiveHost's body, before either goes
  std::unique_ptr<Mixer> mix_;                       // mix.h:94 (made in create() with the rate and 128)
  std::unique_ptr<FxBank> fxBank_;                   // F_EFFECTS (fxbank.h:21)
  std::unique_ptr<MasterChain> mchain_;              // F_MASTER (chain.h)
  std::unique_ptr<MeterBank> meterBank_;             // F_METERS (meter.h)
  bool metersOn_ = false;                            // AUDIO: thunder_live_meters_want switched the bank on (it is fed and advanced only then)
  PdcPlan pdcCur_; PdcMode pdcMode_ = PdcMode::Auto; bool pdcHave_ = false;   // AUDIO: the PDC plan in force (the play's or pdc_set's); re-applied with playing = false at a stop
  int64_t playQ_ = -1;                               // AUDIO: the engine frame of the quantum a play started in (applyPdc: the 10 ms glide applies in that quantum only)
  uint64_t pdcOrd_ = 0, masterOrd_ = 0;              // AUDIO: the ordinals of the PDC / master configuration in force (a published message older than a play's own is ignored)
  thunder_midi* midi_ = nullptr;                     // F_MIDI: made by create(), a raw non-atomic pointer the audio side reads; it outlives every call (it is NEVER swapped: duty D-8 (e))
  SeqRunner runner_;                                 // runner.h
  std::unique_ptr<SeqEvent[]> evs_;                  // kRenderEvents (12288 x 456 B = 5.6 MB), allocated once
  std::unique_ptr<SeqStep> step_;
  std::unique_ptr<float[]> q_;                       // the last quantum: left then right (2 x 128)
  int qRead_ = kLiveQuantum;                         // how much of q_ the host has had (128: empty)
  // THE ENGINE'S OWN TIMELINE (U37 P2 round 2): qFrame_ counts ENGINE frames from 0 (the frames the engine has rendered, gapless); every time the engine sees (Seq, players, mixer, bookings, timers) is engine frame / sr.
  // The HOST's frame of an engine frame is engine + off_ (set at the first call, CHANGED by a re-anchor: nothing inside the engine moves). Conversions: process() (off_), applyCommands / applyOne / tryStartPlay (`at`, t0:
  // host -> engine), post() (every record's `f`: engine -> host), publish() (stats.frames).
  int64_t qFrame_ = 0;                               // the ENGINE frame the next quantum starts at
  int64_t off_ = 0;                                  // hostFrame = engineFrame + off_
  std::atomic<int64_t> offPub_{0};                   // AUDIO writes (relaxed, only when off_ changes), any thread reads (relaxed): off_ as ONE value (a torn read of two counters is what stats.frames - 128 * stats.quanta gave)
  int64_t nextFrame_ = 0, origin_ = 0;               // HOST frames: where the next process call must start; the frame0 of the first call / last re-anchor (stats.frame_origin)
  bool anchored_ = false, peReady_ = false;
  int64_t wallNs_ = 0;                               // process()'s wall_ns, copied into the next CLOCK record
  LiveCmd pend_[kLiveCmdWait]; int nPend_ = 0;       // timed commands waiting for their frame, in RING order (live.h TIME: commands apply in the order sent; `at` is a HOST frame); a stop or a started play clears the list (clearPend_); a 65th is applied at once and counted (cmdLate_)
  double lead_ = 0.25;                               // the runner's look-ahead (render.h:173)
  int stateA_ = THUNDER_LIVE_STATE_IDLE;             // the audio side's own copy of state_
  bool clearPend_ = false; uint32_t cmdsCleared_ = 0, cmdsNoSeq_ = 0; int64_t reanchorRecAt_ = -1;   // AUDIO: a stop / a started play asks applyCommands to clear pend_; counters; the quantum of the last REANCHOR record
  bool songOverDone_ = false; double endStopAt_ = -1;   // C4's songOver once; the MIDI stop 0.3 s later
  bool hadSeq_ = false;                              // AUDIO: a Seq was passed to beginQuantum and none has been reported gone since (-> seqGone() when the runner drops it)
  bool ackPend_ = false; int64_t ackPendRev_ = 0;     // AUDIO: SongAcks of this quantum: one SONG record, the newest rev
  bool ackValid_ = false; int64_t ackRev_ = 0;       // AUDIO: a SongAck posted this rev; a play that adopts the snapshot of that rev does not post a second SONG
  bool posDirty_ = false; int32_t posRow_ = 0, posStep_ = 0, posCr_ = 0; int64_t posF_ = 0;   // AUDIO: the last played step's place, for the POS record
  uint32_t lateLast_[4] = {0, 0, 0, 0};              // AUDIO: the counters of the last LATE record
  uint64_t seqDropBase_ = 0, adoptedOrd_ = 0;        // AUDIO
  // P4 (AUDIO ONLY): the MIDI scheduler's transport. step 1 queues, step 4 (midiBegin) applies right after midi_begin returns, before the step loop.
  struct MidiOp { bool start; int spp; };
  static constexpr int kMidiOps = 16;
  MidiOp midiOps_[kMidiOps]; int nMidiOps_ = 0;       // in the order the commands were applied; a 17th pushes the oldest out (counted nowhere: 16 plays and stops in ONE quantum cannot come from a real shell)
  bool midiOk_ = false;                               // midi_begin of THIS quantum succeeded (midiEnd and onStep run only then)
  bool midiBeginFailedOnce_ = false; uint32_t midiBeginFailed_ = 0;
  double midiSeqT_ = 0, midiBpm_ = 120;               // the last Seq's next step time and tempo (midi_end's arguments once the runner has no Seq: IDLE, HELD)
  uint32_t midiLateLast_[3] = {0, 0, 0};              // the counters of the last LATE record (late notes, late ticks, may_hang)
  int thread_prepares_ = 0;                          // process(): pe_prepare() on this thread's first call; stats.thread_prepares
  PlayerStats lastPlayerStats_;                      // for the LATE record (changed since last post)
  int64_t nextClockAt_ = 0, nextPosAt_ = 0, nextLateAt_ = 0;   // ENGINE frames: the next CLOCK / POS / LATE record is due at or after
  uint32_t evSeq_ = 0, evtDropped_ = 0, evtCritDropped_ = 0, cmdLate_ = 0, reanchors_ = 0;
  uint64_t cmdsApplied_ = 0, quanta_ = 0, calls_ = 0, steps_ = 0, events_ = 0, dumpDropped_ = 0;
  uint64_t lastPadsId_ = 0;
  RenderStats st_;                                   // the part of RenderStats the live host keeps (eventsIgnored)
};

}  // namespace thunder
#endif
