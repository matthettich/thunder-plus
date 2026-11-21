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

// Everything a PLAY needs that is made on the control side. Heap object, created by play(), handed to the audio side in LiveCmd::p, returned through playBack_ and deleted by collect().
struct PlayPlan {
  std::shared_ptr<const Song> song;                  // mixer.load(*song) at the play (audio, at once); the Seq itself travels in the SongSnap published by the same call
  MasterChainConfig master;                          // master_chain_from_song(song.mst, song.limiter); used when F_MASTER
  FxPlan* fx[kSongNT + 1] = {nullptr};               // the song's track and master effects (FxHost::planFromSong); null: the track keeps what it has; committed at the play (audio), then returned
  bool hasMaster = false;
  PdcPlan pdc; PdcMode pdcMode = PdcMode::Auto;      // pdc_plan(bus, slots of `fx` as planned, sr, mode)
  int64_t t0 = -1; int spp = 0;
  ~PlayPlan();                                       // control: deletes any plan the audio side did not take
};

// A PDC plan or a master configuration published beside the song: one slot each (rt/snap.h SnapSlot<T>, plain adopt()), adopted at step 2.
struct LivePdcMsg { PdcPlan plan; PdcMode mode = PdcMode::Auto; };
struct LiveBusMsg { BusPlan plan; };                 // built from the song's chain patch by song_publish; published with it
struct LiveMasterMsg { MasterChainConfig cfg; };

// An event record waiting for poll(): fixed slots so that the audio side writes one without allocating. `bytes` of `data` are valid (thunder_live_ev_* of live.h).
struct LiveEvSlot { uint16_t bytes = 0; uint8_t data[THUNDER_LIVE_EV_MAX] = {0}; };
constexpr uint32_t kLiveEvRing = 1024;               // 330 KB; a full ring refuses the NEWEST record (stats.evt_dropped); the critical ring is 64 records

// One step of the test dump (F_DUMP_STEPS): the head says how many raw events follow in the event ring. Raw SeqEvent copies (456 B each); the control side turns them into JSON in dumpStep().
struct LiveDumpHead { int64_t frame = 0; double t = 0, bpm0 = 0; int32_t tks = 0, n = 0; uint64_t validMask = 0; };

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
  // ~LiveHost(): the audio thread has stopped. If playing: stop + ONE quantum on this thread (midi_begin / midi_end after MidiSched::stop, render.cpp:101,:109) with the audio discarded, then free everything: PadsHost's
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
  // masterConfig(): master_chain_from_song(Json mst, limiter) (chain.h:36) into a LiveMasterMsg on masterSlot_; the audio side calls MasterChain::configure (chain.h:44) at step 2. NOT BUILT: configure is "allocation-free, states kept"
  // but SongRenderer only calls it before the first block (render.h:128), a latency change has no PDC / output-delay path, and the quantum buffer's latency report would jump. Returns E_UNSUPPORTED until P3 builds it.
  int masterConfig(const uint8_t* mst, size_t n, bool safety);

  // ======================================================================================================================================================================================================
  // CONTROL: MIDI, events, test
  // ======================================================================================================================================================================================================
  thunder_midi* midi() { return midi_; }             // owned; valid until ~LiveHost
  int midiLead(double seconds);                      // thunder_midi_set_lead (midi.h:88; its cap 0.5 s -> 1.2 s is P4's change, ruling D6)
  int meters(double rateHz);                         // LiveCmd::Meters: MeterBank::setRate (meter.h:44)
  void hostStats(int outLatencyFrames, int underruns) { outLat_.store(outLatencyFrames, std::memory_order_relaxed); under_.store(underruns, std::memory_order_relaxed); }   // ANY thread
  int poll(void* buf, int cap);                      // CONSUMER: pops evRing_ and copies whole records
  int dumpStep(char* json, int cap);                 // TEST: pops dumpHeads_ / dumpEvs_, writes the golden's JSON (tests/golden/seq-*.json format)
  bool statsGet(thunder_live_stats* out) const;      // ANY: relaxed loads of pub_[] + the control side's own counters + padsHost_->buffers() / retired() (pads_snap.h) + bytes held

  // ======================================================================================================================================================================================================
  // AUDIO
  // ======================================================================================================================================================================================================
  // process(): thunder_live_process. Validates; re-anchors if frame0 != expected (counts it); then like SongRenderer::block (render.cpp:83): while frames remain, render a quantum into q_ when qRead_ is at the end
  // (quantum()), copy what the call needs. Frames > 128 are cut into quanta; the samples do not depend on the call size.
  int process(float* l, float* r, int frames, int64_t frame0, int64_t wallNs);

 private:
  LiveHost() = default;

  // ---- the quantum (audio): the nine steps of live.h, in order -------------------------------------------------------------------------------------------------------------------------------------------------
  void quantum();                                    // template: SongRenderer::quantum (render.cpp:93-120)
  void applyCommands(int64_t qStart, int64_t qEnd);  // step 1: pop cmds_ / fxParams_ into pend_ (Spsc::pop, rt/spsc.h:22), apply every due command in ring order (see applyOne)
  void applyOne(const LiveCmd& c, int64_t qStart);   // the switch: Play -> applyPlay; Stop -> runner_.stop() (runner.h:41), players_->stopAll(at) (players.h:52), mix_->songLanesReset() (mix.h:133), mix_->clockStop(at) (mix.h:135),
                                                     // pendMidiStop_ = true (MidiSched::stop is called by midiBegin, midisched.h:92), the Seq is dropped (the next adopt() calls padsHost_->seqGone() before beginQuantum), state IDLE, STATE record; Hold -> runner_.hold() (runner.h:37) (if the held Seq is dropped: seqGone() as for Stop); Loop / Bpm / Perf -> the running Seq's setters
                                                     // (seq.h:146, 159, 170-172) if runner_.seq(); Lead -> lead_; StopTrack -> players_->stopTrack (players.h:47); MixSet -> mix_->set (mix.h:132); FxCommit -> fxBank_->host(t).commit(plan)
                                                     // (fxhost.h:134) and the plan to fxBack_; Meters -> meterBank_->setRate; SongAck -> an ack record
  void applyPlay(PlayPlan* p, int64_t t0Frame, int spp, int pdcMode, int64_t qStart);   // adopt songSlot_ (seq_adopt, session.h:100), runner_.start(songSlot_.get()->seq.get()) (runner.h:36), seq->setTime(t0) (seq.h:158),
                                                     // mix_->load(*p->song) (mix.h:100), songLanesReset, clockStop, commit every p->fx[t] (fxhost.h:134), mchain_->configure(p->master) (chain.h:44), pdc_apply (pdc.h:48),
                                                     // pendMidiStartSpp_ = spp (the Start itself is called by midiBegin, right after midi_begin: midisched.h:90), tempo seed from the Seq's bpm (seq.h:149 bpm(); fxBank_->setBpm fxbank.h:34, players_->live().tempo live_pool.h:99), STATE PLAYING; the PlayPlan goes to playBack_
  void adopt(int64_t qStart);                        // step 2: seq_adopt(songSlot_, runner_) (session.h:100); then, if the Seq was dropped, padsHost_->seqGone() (BEFORE beginQuantum); padsHost_->beginQuantum(*players_, runner_.seq()) every quantum (pads_snap.h); busSlot_ / pdcSlot_ / masterSlot_.adopt() (snap.h:29); post SONG / PADS records
  void midiBegin(double t0);                         // step 4: midi_begin(midi_, t0, 128 / sr, min(2, latency / sr), mix_->audibleMask()) (midi_host.h:18; render.cpp:101; < 0: count, panic once); THEN, in this order, the direct calls
                                                     // queued by step 1 in pendMidiStop_ / pendMidiStartSpp_: MidiSched::stop(t0) (midisched.h:92), MidiSched::start(spp, t0) (midisched.h:90). Same `now`, same figures, same point as the posted
                                                     // OP_STOP / OP_START of blockBegin (midi_api.cpp:83-90, apply :70-71) except for ring order against other posted commands and stop's `at`: P4's done-test is byte-equality with the
                                                     // posted path for the 18 ledger songs; the posted path is the fallback (live.h, MIDI)
  template <class F> SeqPump stepLoop(double t0, F&& onStep);   // step 5: runner_.pump(t0, lead_, evs_, kRenderEvents, step_, onStep) (runner.h:50); onStep: midi_sched(midi_).onStep (midisched.h:103), dump, then dispatch() each
  void dispatch(const SeqEvent& e);                  // SongRenderer::dispatch (render.cpp:28): mixer events (mix.h:131), the beat clock, FxBank::event (fxbank.h:30), Players::dispatch (players.h:44); chc / eng / tempo / MIDI events counted ignored
  void midiEnd(double t0);                           // step 6: midi_end(midi_, t0, seq time, seq bpm, alive) (midi_host.h:22; render.cpp:109)
  void songEnded(double t0);                         // step 7: once, players_->songOver(seq time) (players.h:54; render.cpp:111), STATE ENDED; arms endStopAt_ = t0 + 0.3
  void render(double t0);                            // step 8: players_->setTime / render (players.h:49-50), mix_->render (mix.h:147), meters_feed + advance (meter.h:61, 48)
  void post(const void* rec, int bytes);             // step 9: write one record into evRing_ (Spsc::push; a full ring: evtDropped_++)
  void afterBlock(int64_t qStart);                   // step 9: CLOCK (10/s), POS (30/s), ROWS, METERS (pop), LATE (4/s), publish pub_[]
  void reanchor(int64_t frame0);                     // process(): drop qRead_, restart the grid, count, ERROR record

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
  bool controlPlaying_ = false;                      // control's view: a play was issued and no stop
  FxPlan* stagedFx_[kSongNT + 1] = {nullptr};        // fxPlan() staged, fxCommit() moved to the ring
  std::unique_ptr<LiveFont> fonts_[THUNDER_LIVE_MAX_FONTS];
  int sfFont_[kRenderPads]; size_t sfBytes_[kRenderPads];   // which font a pad's preset came from; its PCM bytes (for fontRemove and the budget)
  size_t liveBytes_[kRenderPads];                    // LivePool::padBytes (live_pool.h:75) of each live pad
  size_t fontBytes_ = 0, liveBytesTotal_ = 0;        // the rest of the budget besides PadsHost's buffers
  uint32_t cmdBusy_ = 0;                             // stats.cmd_busy
  int64_t lastRev_ = 0;
  std::atomic<bool> noteSessionFull_{false};         // control writes (relaxed), audio reads once a quantum: post the SESSION_FULL record

  // ---- CONTROL -> AUDIO (single producer, single consumer; Spsc / SnapSlot orders: release on publish, acquire on adopt) ----
  Spsc<LiveCmd, kLiveCmdRing> cmds_;                 // control pushes, audio pops
  Spsc<LiveFxParam, kLiveFxParamRing> fxParams_;     // control pushes, audio pops
  SnapSlot<SongSnap> songSlot_;                      // control: SeqSession::publish; audio: seq_adopt; control: collect()
  SnapSlot<LiveBusMsg> busSlot_;                     // plain adopt()
  SnapSlot<LivePdcMsg> pdcSlot_;
  SnapSlot<LiveMasterMsg> masterSlot_;
  PadsHost* padsHost_ = nullptr;                     // PadsHost::publish / collect: control; beginQuantum: audio (pads_snap.h): its own atomics (board_) and SnapSlot
  // ---- AUDIO -> CONTROL ----
  Spsc<PlayPlan*, 8> playBack_;                      // spent PlayPlans (audio pushes, control pops in collect)
  Spsc<FxPlan*, 64> fxBack_;                         // committed FxPlans (their `retired_` objects go with them)
  // ---- AUDIO -> CONSUMER ----
  Spsc<LiveEvSlot, kLiveEvRing> evRing_;             // audio pushes records, the consumer thread pops (poll)
  Spsc<LiveEvSlot, 64> evCrit_;                      // the CRITICAL records (STATE, HELD, ERROR, SONG, PADS) only: own ring, so a flood of POS / METERS cannot drop one; poll merges the two rings by `seq`
  std::unique_ptr<SeqEvent[]> dumpEvs_; uint32_t dumpEvCap_ = 0;   // F_DUMP_STEPS: raw events, written by audio, read by control after it pops the matching head (the Spsc release / acquire orders them)
  std::unique_ptr<Spsc<LiveDumpHead, 1024>> dumpHeads_;
  // ---- published numbers (audio writes with relaxed stores once a quantum; any thread reads: each value valid, the set not one instant) ----
  std::atomic<uint64_t> pub_[64];                    // stats.* of the audio side, in thunder_live_stats order
  std::atomic<int32_t> state_{THUNDER_LIVE_STATE_IDLE};   // audio writes (release), control reads (acquire): the state after the last quantum
  std::atomic<int32_t> outLat_{0}, under_{0};        // hostStats() writes, the audio side reads relaxed (CLOCK records)

  // ---- AUDIO ONLY (never touched by control after create(); create() builds them before the audio thread exists) ----
  std::unique_ptr<Players> players_;                 // players.h:30
  std::unique_ptr<Mixer> mix_;                       // mix.h:94 (made in create() with the rate and 128)
  std::unique_ptr<FxBank> fxBank_;                   // F_EFFECTS (fxbank.h:21)
  std::unique_ptr<MasterChain> mchain_;              // F_MASTER (chain.h)
  std::unique_ptr<MeterBank> meterBank_;             // F_METERS (meter.h)
  thunder_midi* midi_ = nullptr;                     // F_MIDI: made by create(), a raw non-atomic pointer the audio side reads; it outlives every call (it is NEVER swapped: duty D-8 (e))
  SeqRunner runner_;                                 // runner.h
  std::unique_ptr<SeqEvent[]> evs_;                  // kRenderEvents (12288 x 456 B = 5.6 MB), allocated once
  std::unique_ptr<SeqStep> step_;
  std::unique_ptr<float[]> q_;                       // the last quantum: left then right (2 x 128)
  int qRead_ = kLiveQuantum;                         // how much of q_ the host has had (128: empty)
  int64_t qFrame_ = 0, nextFrame_ = 0, origin_ = 0;  // the frame the next quantum starts at; the frame the next process call must start at; the grid's anchor
  bool anchored_ = false, peReady_ = false;
  int64_t wallNs_ = 0;                               // process()'s wall_ns, copied into the next CLOCK record
  LiveCmd pend_[kLiveCmdWait]; int nPend_ = 0;       // timed commands waiting for their frame, ordered by at (equal at: ring order); a 65th is applied at once and counted (cmdLate_)
  double lead_ = 0.25;                               // the runner's look-ahead (render.h:173)
  int stateA_ = THUNDER_LIVE_STATE_IDLE;             // the audio side's own copy of state_
  bool songOverDone_ = false; double endStopAt_ = -1;   // C4's songOver once; the MIDI stop 0.3 s later
  bool midiBeginFailedOnce_ = false;
  bool pendMidiStop_ = false; int pendMidiStartSpp_ = -1;   // set by step 1, done by midiBegin right after midi_begin (stop first, then start)
  int thread_prepares_ = 0;                          // process(): pe_prepare() on this thread's first call; stats.thread_prepares
  PlayerStats lastPlayerStats_;                      // for the LATE record (changed since last post)
  int64_t nextClockAt_ = 0, nextPosAt_ = 0, nextLateAt_ = 0;   // frames: the next CLOCK / POS / LATE record is due at or after
  uint32_t evSeq_ = 0, evtDropped_ = 0, cmdLate_ = 0, reanchors_ = 0;
  uint64_t cmdsApplied_ = 0, quanta_ = 0, calls_ = 0, steps_ = 0, events_ = 0, dumpDropped_ = 0;
  int ackRevPending_ = 0; int64_t ackRev_ = 0; uint64_t lastPadsId_ = 0;
  RenderStats st_;                                   // the part of RenderStats the live host keeps (events ignored, steps, peakStepEvents)
};

}  // namespace thunder
#endif
