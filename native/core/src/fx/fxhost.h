// FxHost (U14): the C++ of the `ThunderFx` worklet (index.html TFX_SRC, `thunder-tfx`) for the 34 effects tfx.wasm holds (ids 0..33):
// one host per track and one for the master. It owns the slots (up to 8, FXSLOTS), runs them in order on a stereo pair, and does what the
// worklet does around them: ON / OFF with a 5 ms crossfade (a slot's own switch, or an FXO command holding it), the node's sleep (silence
// in, the tails gone: it outputs silence), a slot's nap (30 s of nothing in and out), a muted track's rest ({rest}), the 1e-18 anti-denormal
// dither, the clip / zero of samples past +-8 / +-64, the REVERB MIX blend of the three reverbs, per-slot level telemetry (FX BAR) and the
// dynamics levels (FX DYNAMICS), FXV (`pset`, the core's queue: a slot's new parameters at a time) and k-rate FXO / FLX / GAT / RTG lanes.
// THE JS IS THE SPECIFICATION, quirks included (list in docs of the unit; progress/u14.md).
//
// U15: the tempo-locked JS effects are slot kinds here too: Retrigger 93, Step gate 94, Flex 95 (fx/clkcores.h). They read the beat clock block `bp` that
// process() is handed (Mixer::beat(track), what the node's `bp` AudioParam carries) and the FLX / GAT / RTG lanes, run whether the slot is on or off (Flex
// is fed while off), and decide, with the node's sleep, when the host may sleep (`ck` of the worklet's p1).
//
// U18: the 25 pump effects are slot kinds here too, ids 200 + the pump type (kPumpT0): the worklet keeps them in the same chain, with the same sleep, FXO switches
// and levels, and the same node clock, so they are slots of this host and not a sibling (fxpump.cpp, pumpfx.h; the page's `pt`, `pv`, `bi`, `sck`, `cr`, `clk`
// of a tfxChain entry are FxEntry's pump fields). They call the `pump_*` entry points of pump.wasm directly (library thunder_pump).
//
// Slot kinds the JS has that are not here yet: Beat grains / scramble / Glitch (100..102), Pitch
// delay / reverb, Vintage sampler, Degrade, Resonator (U16), shared MODFX modules (U17). A chain entry of those kinds is
// left out of the host as the JS leaves out a slot whose engine is missing: the sound passes. `Slot` has the places they will use.
//
// REAL-TIME RULES. process(), psetAt(), lane(), reset(), setRest(), setLevels(), setTelemetry(), commit() allocate nothing and take no lock
// (the audio thread, between blocks). plan() / the song reader / delete plan run on the control thread: effect objects are made there
// (Fx::operator new is malloc) and handed over by commit(); what a commit retires is deleted with its FxPlan, on the control thread.
// EXCEPTION (U18, see fxpump.cpp): a pump slot that wakes from its rest, or whose plugin put out garbage, gets a FRESH plugin instance in the audio thread, as the
// JS does (pump_new allocates); audioRemakes() counts them. Nothing else of pump allocates in process(); pump's tables are shared by every host of the process.
#ifndef THUNDER_FX_FXHOST_H
#define THUNDER_FX_FXHOST_H
#include <cstdint>
#include <cstring>
#include <string_view>

#include "fx/clkcores.h"
#include "rt/auto.h"
#include "rt/json.h"

struct Fx;   // dsp/tfx/tfx.h

namespace thunder {

constexpr int kFxSlots = 8;          // FXSLOTS
constexpr int kFxBlock = 128;        // the worklet's quantum (the wasm io buffers)
constexpr int kFxMaxCall = 4096;     // a call longer than this fails soft: silence (tcChunks)
constexpr int kFxQueue = 256;        // FXV messages waiting (the core's __q)
constexpr int kFxKey = 24;           // "slot:type"
inline bool fx_is_clocked(int id) { return id >= 93 && id <= 95; }   // Retrigger, Step gate, Flex (the tempo-locked JS cores)
// U18: pump effects. Kind = kPumpT0 + the pump type (pump_wasm.cpp's order: EQ 0, COMP, LIMIT, CALM, TONE, BAND, WIDE, HEAT, METER, TUBE, SNAP, GATE, ECHO, SPRING, VERB,
// GRAIN, VOX, TUNE, CRUSH, SWIRL, STUTTER, CHOP, TWANG, MASTER, STRIP 24). The page's own slot id for them is none (it keys them by `pt`).
constexpr int kPumpT0 = 200, kPumpTypes = 25;
constexpr int kPumpMaxParams = 280;   // EQ has 277 (pump-meta); pump_stage holds 512, so the JS would stage more: a slot with more values than this is refused
inline bool fx_is_pump(int id) { return id >= kPumpT0 && id < kPumpT0 + kPumpTypes; }
const char* fx_pump_key(int pt);       // the page's type name ("eq", "comp", ..., "strip"); the chain key is slot + ":p" + this
int fx_pump_bypass(int pt);            // the parameter index of the plugin's own bypass (the page's pumpIdx[...].bypass), -1: METER has none
bool fx_pump_creative(int pt);         // the entry's `cr`: transport + notes (the creative effects and MASTER)
bool fx_pump_clocked(int pt);          // the entry's `clk`: GATE, STUTTER, CHOP keep running on the clock while silent

// ---- the effect table (fxtable.inc, generated from TFX: ids 0..33 and 93..95) and the parameter mapping ------------------------------------------------------
int fx_type_of(std::string_view key);                 // the page's effect key ("eq", "reverb", "flex", ...) -> id 0..33 or 93..95, else -1
const char* fx_key_of(int id);                        // id -> key (nullptr for an id the table has not)
int fx_param_count(int id);
int fx_param_min(int id, int i); int fx_param_max(int id, int i); int fx_param_default(int id, int i);
// map(): the page's TFX[type].map(f, sr, bpm) (f: the controls in the table's order, as normTfx leaves them) into the 12 parameters; fxRestartN is tgate's 7th.
void fx_map(int id, const double* f, double sr, double bpm, double restartN, double* p12);
// normTfx's number rule for one control: int(v, d) clamped to min..max (v: the song's JSON value, Missing = the default)
double fx_norm_value(Json v, int id, int i);
const char* kFxKeyOf(int id, int i);                  // control i's key
// One chain entry as tfxChain makes it for a wasm effect.
struct FxEntry {
  char key[kFxKey] = {0};
  int t = -1, slot = -1;
  bool on = true;
  double p[12] = {0};          // the mapped parameters (doubles, as the JS has them; stored to float32 when set)
  double vals[12] = {0};       // the controls (for FXV)
  int nvals = 0;
  // pump effects (kind kPumpT0 + pt; p / vals are not used): the page's tfxChain entry {pt, pv, bi, sck, cr, clk}. Filled by the caller except `lat`.
  float pv[kPumpMaxParams];    // every parameter value in the plugin's own order (the page's f.pump[p.s] ?? p.def); only the first npv are meaningful
  int npv = 0;                 // pv.length (0: the instance keeps pump's defaults)
  int bi = -1;                 // the parameter index of the plugin's own bypass (-1: none, METER)
  int sck = 0;                 // COMP's External sidechain source: 0 none, 1..8 send n, 9 + t track t
  bool cr = false, clk = false;   // creative (gets the transport and its notes), keeps running on the clock
  double lat = 0;              // the latency LATENCY COMP counts (pumpLatencyOf), samples: set by plan(), read by slotLatency()
  FxEntry() { std::memset(pv, 0, sizeof pv); }
};

class FxHost;
// A new chain, made on the control thread (what `set(chain)` of the worklet is, with the effect objects made ahead).
class FxPlan {
 public:
  FxPlan() = default;
  FxPlan(const FxPlan&) = delete; FxPlan& operator=(const FxPlan&) = delete;
  ~FxPlan();
  int size() const { return n_; }
  const FxEntry& entry(int i) const { return e_[i]; }
 private:
  friend class FxHost;
  int n_ = 0;
  FxEntry e_[kFxSlots];
  Fx* fx_[kFxSlots] = {nullptr};      // new objects (null: reuse the live slot of the same key)
  bool reuse_[kFxSlots] = {false};
  Fx* retired_[kFxSlots] = {nullptr}; // filled by commit(): what the live chain no longer holds
  int nRetired_ = 0;
  bool committed_ = false;
};

struct FxDyn { int slot; char key[kFxKey]; float v[3]; };   // [reduction dB, level in dB, peak out dB]
struct PumpTelOut { int slot; char key[kFxKey]; int type; int n; float t[32]; };   // the {pump} post for METER and LIMIT while {tel} is on: the plugin's snapshot (pump_tel)
struct PumpLat { int slot; char key[kFxKey]; int lat; };                          // the {plat} post: one pump slot's latency in samples
struct PumpMidi { int slot; double at; int status, d1, d2; };                      // the {pmidi} message: [slot, at (s), status, d1, d2]
constexpr int kPumpMidi = 1024, kPumpQueue = 32;
struct PumpSlot;   // pumpfx.h

class FxHost {
 public:
  explicit FxHost(double sr);
  ~FxHost();
  FxHost(const FxHost&) = delete; FxHost& operator=(const FxHost&) = delete;

  // ---- control thread ----
  // From entries (the JS `set(chain)`): objects are made for keys the chain after the previous plan does not hold. nullptr if there is no effects library.
  FxPlan* plan(const FxEntry* entries, int n);
  // From a song's slot list (S.tfx[t] / S.mfx: up to 8 objects or null): normTfx + tfxChain for the wasm effects; others are left out.
  FxPlan* planFromSong(Json slots, double bpm, double restartN);
  static bool entryFromSlot(Json slotObj, int slot, double sr, double bpm, double restartN, FxEntry* out);   // false: not a wasm effect

  // ---- audio thread (between blocks) ----
  void commit(FxPlan* p);                       // set(chain): slots in the plan's order, objects of equal key kept, idle = 0
  void psetAt(double at, int slot, const double* p12);   // the core's tcAt(proc, at, {pset:{slot, p}}): applied by the first block starting within 64 samples of `at`
  void lane(int k, double v, double at);        // k 0..7 sw0..sw7, 8 flx, 9 gat, 10 rtg: setValueAtTime (read once a block, at its start)
  void reset(double at);                        // fxreset: every lane cancelled from `at` and -1 there
  void setRest(bool on, double now);            // {rest}: from now + 0.1 s the node hears silence
  void setLevels(bool on) { lvl_ = on; lvlN_ = 0; std::memset(lvlP_, 0, sizeof lvlP_); }   // {lvl}
  void setTelemetry(bool on) { tel_ = on; }     // {tel}
  void kill();                                  // the `null` message: dead, the objects are retired by the next commit (or the destructor)
  // ---- pump effects (U18; audio thread, between blocks) ----
  void psetPump(double at, int slot, const float* pv, int n);   // FXV for a pump slot, the core's `{pset:{slot, pv}}` at a time (the slot's whole value list)
  void pmidi(int slot, double at, int status, int d1, int d2);  // {pmidi}: a note for a creative effect (NOTES FROM), `at` in seconds on the block clock; kept in time order
  void hold(const char* key, int kind, double a, double b, double c, double d);   // {phold}: pump_hold on the slot with this chain key (listen / audition / MASTER LEARN)
  // SOURCE (COMP's External sidechain, `sck`): this block's inputs 1 and 2 of the node, as pointers valid for the next process() call: taps[c] the sound of the
  // c-th tapped track (`setTapMap`), sends[i] send i + 1; each at least as long as the block. nullptr / 0: nothing connected (sck then hears silence).
  void setSources(const float* const* taps, int nTaps, const float* const* sends, int nSends) { taps_ = nTaps > 0 ? taps : nullptr; nTaps_ = nTaps; sends_ = nSends > 0 ? sends : nullptr; nSends_ = nSends; }
  void setTapMap(const int* tracks, int n);     // {tapmap}: tracks[i] is the track heard on tap channel i

  // L, R in place; n frames starting at t0 seconds. n <= 128 is one quantum; a longer call is cut into 128s (tcChunks; lanes read once at t0);
  // above kFxMaxCall the output is silence and failedBlock() counts. Returns false when dead.
  // bp: the beat clock the node hears (params.bp: the song position in beats, -1 stopped), n floats, or bpn = 1 when the param is constant over the block;
  // nullptr: the param at its default, a constant 0. Cut with the block (tcChunks hands each chunk its own part of an a-rate param).
  bool process(float* L, float* R, int n, double t0, const float* bp = nullptr, int bpn = 0);

  // ---- reading back ----
  bool popLevels(float out[8]);                 // the {lvl} post (every 12th live block): true once per post
  int popDyn(FxDyn* out, int max);              // the {dyn} post: entries (0 when none was posted since)
  int popPumpTel(PumpTelOut* out, int max);     // the {pump} post (METER, LIMIT: the loudness readouts), 0 when none was posted since
  int popLatencies(PumpLat* out, int max);      // the {plat} post (the pump slots' latencies, when they changed; every half second at most): count, 0 when none
  uint32_t audioRemakes() const { return remakes_; }   // fresh plugin instances made in the audio thread (rest wake, runaway output): the one allocation of pump's slots
  uint32_t pumpGated() const { return gated_; }        // blocks in which the pump slots were left alone because the control thread was making an instance
  uint32_t failedBlock() const { return failedBlock_; }
  uint32_t queueDropped() const { return qDropped_; }
  uint32_t laneDropped() const;
  int slotCount() const { return nSlots_; }
  const char* slotKey(int i) const { return slots_[i].key; }
  int slotType(int i) const { return slots_[i].t; }
  int slotNumber(int i) const { return slots_[i].slot; }
  bool slotOn(int i) const { return slots_[i].on; }
  double slotLatency(int i) const { return fx_is_pump(slots_[i].t) ? chain_[i].lat : 0; }   // samples (PdcSlot::latency): pumpLatencyOf, taken when the plan was made
  bool slotBypass(int i) const { return fx_is_pump(slots_[i].t) && chain_[i].bi >= 0; }       // PdcSlot::bypass: the effect has a bypass of its own (every pump effect but METER)
  bool asleep() const { return asleep_; }       // the last block was the node's sleep (silence out)
  int idleFrames() const { return idle_; }
  bool slotNapping(int i) const { return slots_[i].zz; }
  double sampleRate() const { return sr_; }
  const FxEntry* chainEntry(int i) const { return i >= 0 && i < nChain_ ? &chain_[i] : nullptr; }
  int chainSize() const { return nChain_; }

 private:
  struct Slot {
    char key[kFxKey] = {0};
    Fx* fx = nullptr;
    PumpSlot* pu = nullptr;               // U18: the pump instance and its state when t is a pump kind (owned by `fx`, a PumpHold)
    int t = -1, slot = -1, wi = -1;       // wi: the REVERB MIX parameter index (8: 4, 20: 7, 24: 3), else -1
    bool on = true, gset = false, zz = false, nn = false;
    double g = 0, zq = 0, rvDry = 1, rvWet = 0;
    float lip = 0, lop = 0;
  };
  struct Q { double at; int slot; double p[12]; };
  void applyEntry(Slot& s, const FxEntry& c, bool fresh = false);   // fresh: a slot made by this commit (the JS's new slot: not `dirty`)
  void applyPset(int slot, const double* p12);
  void applyPumpPset(int slot, const float* pv, int n);
  // U18 (fxpump.cpp): the worklet's pumpRun, pumpRest, restDelay and the latency bookkeeping (`s.up`, {plat})
  void pumpRun(Slot& s, int n);
  bool pumpRest(Slot& s, int n, bool tg, bool pqOn);
  void restDelay(PumpSlot& u, int n);
  void pumpReport();                               // the half-second report: s.up again, {plat}
  void pumpUp();                                   // s.up of every pump slot (set()'s accumulation of the latencies before it)
  void pumpTelSlot(Slot& s, int* nDyn);            // one pump slot's part of the {tel} post
  void pumpClock(const float* bp, int bpn, int n); // the node's transport (T.playing, T.tempo, this.ppq) from the beat clock
  const float* srcKey(int sck) const;              // mctx.key(sck): the sound a pump slot's External sidechain listens to (nullptr: none asked)
  bool p1(float* L, float* R, int n, double t0, const float* bp, int bpn);
  void slotRun(Slot& s, int n);
  void runClk(Slot& s, bool feed, int n);        // a tempo-locked core: process() (or feed()) on Lb_ / Rb_ with tr_
  int clockedNeed() const;   // the worklet's `ck`: what the tempo-locked slots ask of the node's sleep (0..3)
  void slotNap(Slot& s, int n);
  float pk(int n) const;
  void finalizeTel();

  double sr_;
  Slot slots_[kFxSlots]; int nSlots_ = 0;
  FxEntry chain_[kFxSlots]; int nChain_ = 0;       // `this.chain`
  FxEntry ctl_[kFxSlots]; int nCtl_ = 0;           // the chain after the last plan() (control thread's view, for object reuse)
  Fx* ctlFx_[kFxSlots] = {nullptr};
  FxPlan* lastPlan_ = nullptr;
  Fx* dead_[kFxSlots * 2] = {nullptr}; int nDead_ = 0;
  AutoN<96>* lanes_[11];
  Q q_[kFxQueue]; int nq_ = 0; uint32_t qDropped_ = 0;
  float Lb_[kFxBlock + 2], Rb_[kFxBlock + 2], cL_[kFxBlock + 2], cR_[kFxBlock + 2], dL_[kFxBlock + 2], dR_[kFxBlock + 2], rL_[kFxBlock + 2], rR_[kFxBlock + 2];
  double restAt_ = -1;
  int idle_ = 0; bool dead_flag_ = false, asleep_ = false;
  ClkTr tr_;                                      // the worklet's `tr` (bp, flx, gat, rtg; sw per slot)
  float swv_[11] = {-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1};
  bool lvl_ = false, tel_ = false; int lvlN_ = 0, telN_ = 0; float lvlP_[8] = {0};
  bool lvlReady_ = false; float lvlOut_[8] = {0};
  int nDynOut_ = 0; FxDyn dynOut_[kFxSlots];
  uint64_t rand_ = 0;   // wasi-libc's rand() state of this host's "module" (see dsp/wasi_rand.cpp)
  // the other statics of this host's "module" (dsp/host_fx_state.h): stmlib::Random's state (Beads, Clouds) and DaisySP myrand()'s seed
  // (PitchShifter), with the values a fresh wasm instance starts from
  uint32_t stmRng_ = 0x21, daisySeed_ = 1;
  uint32_t failedBlock_ = 0;
  bool bsFail_ = false;
  // ---- pump (U18): the node-level state the worklet keeps beside the slots ----
  ClkSpb node_;                              // `this` as clkSpb's object: spb, cn, cu, cl, sm, ps, ca of the node's own clock slope (the modules' / pump's transport)
  bool wasOn_ = false, playing_ = false, pumpSeen_ = false, pumpOk_ = false; int nPump_ = 0;   // pumpSeen_: a pump slot was ever in this host (the worklet's `globalThis.__pumpX` exists)
  double tempo_ = 120, ppq_ = 0, fr_ = 0, t0_ = 0;   // T.tempo, this.ppq, this.fr (frames since the last half-second report), currentTime of this call
  PumpMidi pq_[kPumpMidi]; int pqn_ = 0; uint32_t pqDropped_ = 0;   // this.pq
  struct Qp { double at; int slot; int n; float pv[kPumpMaxParams]; };
  Qp qp_[kPumpQueue]; int nqp_ = 0; uint32_t qpDropped_ = 0;       // FXV for pump slots (the core's queue, as q_)
  const float* const* taps_ = nullptr; const float* const* sends_ = nullptr; int nTaps_ = 0, nSends_ = 0, srcOff_ = 0;
  short tapIdx_[128]; float zero_[kFxBlock + 2];                    // this.tapIdx, this.zero
  PumpLat latLast_[kFxSlots]; int nLatLast_ = -1; bool latReady_ = false;   // this.platS (the last list posted), the {plat} post
  PumpTelOut ptelOut_[kFxSlots], ptelTmp_[kFxSlots]; int nPtelOut_ = 0, nPtelTmp_ = 0;
  uint32_t remakes_ = 0, gated_ = 0;
};

}  // namespace thunder
#endif
