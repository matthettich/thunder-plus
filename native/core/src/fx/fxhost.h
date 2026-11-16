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
// U16: Beat grains 100, Beat scramble 101, Glitch sequencer 102 and the Vintage sampler 103 (fx/idmcores.h) too. The first three are tempo-locked like Flex but keep
// running while the clock is stopped (the worklet's `ck` 3) and have no feed; the Vintage sampler ignores the clock, is never napped and asks nothing of the sleep.
//
// Slot kinds the JS has that are not here yet: Pitch delay / reverb, Degrade, Resonator (U17), shared MODFX modules (U17), pump effects (U18). A chain entry of those kinds is
// left out of the host as the JS leaves out a slot whose engine is missing: the sound passes. `Slot` has the places they will use.
//
// REAL-TIME RULES. process(), psetAt(), lane(), reset(), setRest(), setLevels(), setTelemetry(), commit() allocate nothing and take no lock
// (the audio thread, between blocks). plan() / the song reader / delete plan run on the control thread: effect objects are made there
// (Fx::operator new is malloc) and handed over by commit(); what a commit retires is deleted with its FxPlan, on the control thread.
#ifndef THUNDER_FX_FXHOST_H
#define THUNDER_FX_FXHOST_H
#include <cstdint>
#include <cstring>
#include <string_view>

#include "fx/clkcores.h"
#include "fx/idmcores.h"
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
inline bool fx_is_idm(int id) { return id >= 100 && id <= 103; }    // U16: Beat grains, Beat scramble, Glitch sequencer, Vintage sampler (the other JS cores; 100..102 are tempo-locked, 103 is not)
inline bool fx_is_core(int id) { return fx_is_clocked(id) || fx_is_idm(id); }   // a slot kind that is a JS core run by the host itself (a ClkFx), not a tfx effect

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
// Latency in samples of an effect type as the page counts it for LATENCY COMP: 0 for all 34 (only pump effects report one)
inline int fx_latency(int /*id*/) { return 0; }

// One chain entry as tfxChain makes it for a wasm effect.
struct FxEntry {
  char key[kFxKey] = {0};
  int t = -1, slot = -1;
  bool on = true;
  double p[12] = {0};          // the mapped parameters (doubles, as the JS has them; stored to float32 when set)
  double vals[12] = {0};       // the controls (for FXV)
  int nvals = 0;
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

  // L, R in place; n frames starting at t0 seconds. n <= 128 is one quantum; a longer call is cut into 128s (tcChunks; lanes read once at t0);
  // above kFxMaxCall the output is silence and failedBlock() counts. Returns false when dead.
  // bp: the beat clock the node hears (params.bp: the song position in beats, -1 stopped), n floats, or bpn = 1 when the param is constant over the block;
  // nullptr: the param at its default, a constant 0. Cut with the block (tcChunks hands each chunk its own part of an a-rate param).
  bool process(float* L, float* R, int n, double t0, const float* bp = nullptr, int bpn = 0);

  // ---- reading back ----
  bool popLevels(float out[8]);                 // the {lvl} post (every 12th live block): true once per post
  int popDyn(FxDyn* out, int max);              // the {dyn} post: entries (0 when none was posted since)
  uint32_t failedBlock() const { return failedBlock_; }
  uint32_t queueDropped() const { return qDropped_; }
  uint32_t laneDropped() const;
  int slotCount() const { return nSlots_; }
  const char* slotKey(int i) const { return slots_[i].key; }
  int slotType(int i) const { return slots_[i].t; }
  int slotNumber(int i) const { return slots_[i].slot; }
  bool slotOn(int i) const { return slots_[i].on; }
  double slotLatency(int i) const { return (double)fx_latency(slots_[i].t); }   // samples (PdcSlot::latency)
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
    int t = -1, slot = -1, wi = -1;       // wi: the REVERB MIX parameter index (8: 4, 20: 7, 24: 3), else -1
    bool on = true, gset = false, zz = false, nn = false;
    double g = 0, zq = 0, rvDry = 1, rvWet = 0;
    float lip = 0, lop = 0;
  };
  struct Q { double at; int slot; double p[12]; };
  void applyEntry(Slot& s, const FxEntry& c);
  void applyPset(int slot, const double* p12);
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
  float Lb_[kFxBlock + 2], Rb_[kFxBlock + 2], cL_[kFxBlock + 2], cR_[kFxBlock + 2], dL_[kFxBlock + 2], dR_[kFxBlock + 2];
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
};

}  // namespace thunder
#endif
