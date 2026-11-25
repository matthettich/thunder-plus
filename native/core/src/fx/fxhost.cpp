// See fxhost.h. Each block of code keeps the worklet's statements and arithmetic order (index.html TFX_SRC: set, p1, slotRun, slotNap, tcChunks).
#include "fx/fxhost.h"
#include <memory>

#include "fx/fxmod.h"
#include "fx/pumpfx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "rt/jsmath.h"

#ifdef THUNDER_HAVE_TFX
#include "tfx.h"
#else
struct Fx { float p[12]; float sr = 48000.f; virtual ~Fx() {} virtual void process(float*, float*, int) = 0; };
namespace { struct ModuleState { ModuleState(uint64_t*, uint32_t*, uint32_t*) {} }; }   // (no effects library: no statics to swap; the tempo-locked cores need none)
#endif

#ifdef THUNDER_HAVE_TFX
extern "C" void thunder_wasi_rand_use(uint64_t* state);
extern "C" void thunder_fx_state_use(uint32_t* stmlib_rng, uint32_t* daisy_myrand);
namespace {
// This host's copies of the effect sources' statics for as long as effect code runs on this thread (one tfx.wasm instance per node in the web app).
struct ModuleState {
  ModuleState(uint64_t* r, uint32_t* stm, uint32_t* daisy) { thunder_wasi_rand_use(r); thunder_fx_state_use(stm, daisy); }
  ~ModuleState() { thunder_wasi_rand_use(nullptr); thunder_fx_state_use(nullptr, nullptr); }
};
}
#endif
// Cloud Seed's delay buffers are fixed arrays sized for 96 kHz (third_party/prepare.sh: 131072 and 4800 samples; the web's own 32768 / 2400 are smaller still), and its taps are
// set in samples from the rate it is given: above 96 kHz an allpass or a late line is longer than its buffer and reads before it (index -7 at 192 kHz, -4822 at 384 kHz in
// ModulatedAllpass.h; U34a verification D8). The web app has no rule for it (its worklet only ever runs at the context's rate and reads outside the buffers at 96 kHz already).
// So the reverb is made for at most 96 kHz: above that it runs with its 96 kHz delays and filters (its time constants are shorter, by the ratio, than the rate asks for)
// and stays inside its buffers. At 44.1 and 48 kHz (and everything up to 96 kHz) nothing changes. The third-party source is not touched.
static constexpr float kCloudSeedMaxSr = 96000.f;

namespace thunder {

#ifdef THUNDER_HAVE_TFX
// tfx.cc's tfx_new switch (the table of make_*), called directly: its handle table holds 32 objects for the whole process, a song has up to 72 slots
static Fx* make_fx(int type, float sr) {
  Fx* f = nullptr;
  switch (type) {
    case 0: f = make_rings(sr); break;   case 1: f = make_clouds(sr); break;
    case 2: f = make_density(sr); break; case 3: f = make_totape(sr); break;
    case 4: f = make_chorus(sr); break;  case 5: f = make_crush(sr); break;
    case 6: f = make_eq(sr); break;      case 7: f = make_comp(sr); break;
    case 8: f = make_reverb(sr); break;  case 9: f = make_delay(sr); break;
    case 10: f = make_beads(sr); break;
    case 11: f = make_flanger(sr); break;   case 12: f = make_phaser(sr); break;
    case 13: f = make_tremolo(sr); break;   case 14: f = make_overdrive(sr); break;
    case 15: f = make_folder(sr); break;    case 16: f = make_autowah(sr); break;
    case 17: f = make_pitch(sr); break;     case 18: f = make_ladder(sr); break;
    case 19: f = make_warps(sr); break;     case 20: f = make_cloudseed(sr > kCloudSeedMaxSr ? kCloudSeedMaxSr : sr); break;
    case 21: f = make_derez2(sr); break;    case 22: f = make_purestdrive(sr); break;
    case 23: f = make_air3(sr); break;      case 24: f = make_verbity2(sr); break;
    case 25: f = make_comb(sr); break;      case 26: f = make_transient(sr); break;
    case 27: f = make_gate(sr); break;      case 28: f = make_ott(sr); break;
    case 29: f = make_shift(sr); break;     case 30: f = make_tovinyl4(sr); break;
    case 31: f = make_dubly(sr); break;     case 32: f = make_cabs(sr); break;
    case 33: f = make_tube2(sr); break;
  }
  if (f) f->sr = sr;
  return f;
}
#else
static Fx* make_fx(int, float) { return nullptr; }
#endif
Fx* fx_make_tfx(int id, float sr) { return id >= 0 && id <= 33 ? make_fx(id, sr) : nullptr; }

// U15: a tempo-locked core in a slot. It is an Fx so that the plan, commit and retirement of U14 own, keep and delete it as they do the wasm effects (reuse by key,
// retired by commit(), deleted on the control thread); process() is never called, the host runs the core itself (runClk). dp: the slot's mapped parameters,
// kept as doubles (the worklet's `s.dp`, an object of Numbers; the float32 `p` of a wasm effect is not used).
template <class C, class Dp> struct ClkFx : Fx {
  C core; Dp dp;
  explicit ClkFx(double sr) : core(sr) {}
  void process(float*, float*, int) override {}
};
using RetrigFx = ClkFx<RetrigCore, RtDp>;
using SgateFx = ClkFx<StepGateCore, SgDp>;
using FlexFx = ClkFx<FlexCore, FlexDp>;
using GrainsFx = ClkFx<GrainsCore, GrDp>;       // U16
using ScrambleFx = ClkFx<ScrambleCore, ScDp>;
using GlitchFx = ClkFx<GlitchCore, GlDp>;
using VSampFx = ClkFx<VSampCore, VsDp>;
using PitchFx = ClkFx<PitchFxCore, PfDp>;       // U17 (96 and 97)
using ResonFx = ClkFx<ResonCore, RsDp>;
using DegradeFx = ClkFx<DegradeCore, DgDp>;
static Fx* make_clk(int type, double sr) {
  switch (type) { case 93: return new RetrigFx(sr); case 94: return new SgateFx(sr); case 95: return new FlexFx(sr);
    case 100: return new GrainsFx(sr); case 101: return new ScrambleFx(sr); case 102: return new GlitchFx(sr); case 103: return new VSampFx(sr);
    case 96: case 97: return new PitchFx(sr); case 98: return new ResonFx(sr); case 99: return new DegradeFx(sr); default: return nullptr; }
}
// c.p -> s.dp: the worklet's set(): {pat, mix, xf, wait} (Flex), {pat, m1, m2, div, len, depth, smooth, wait} (Step gate), {mode, div, decay, pitch, gate, mix} (Retrigger)
static void setClkDp(Fx* fx, int t, const double* q) {
  if (t == 100) static_cast<GrainsFx*>(fx)->dp = GrDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10], q[11]};   // {div, density, size, pos, spray, pitch, rpitch, rev, shape, spread, freeze, mix}
  else if (t == 101) static_cast<ScrambleFx*>(fx)->dp = ScDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]};                      // {div, chance, rev, keep, vary, seed, xf, mix}
  else if (t == 102) static_cast<GlitchFx*>(fx)->dp = GlDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9], q[10]};     // {div, chance, stut, rev, pitch, tape, crush, gate, vary, seed, mix}
  else if (t == 103) static_cast<VSampFx*>(fx)->dp = VsDp{q[0], q[1], q[2], q[3], q[4], q[5]};                                      // {model, drive, filter, rate, noise, mix}
  else if (t == 96 || t == 97) static_cast<PitchFx*>(fx)->dp = PfDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7], q[8], q[9]};   // {kind, mode, time, fb, pitch, pitch2, tone, size, wow, mix}
  else if (t == 98) static_cast<ResonFx*>(fx)->dp = RsDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6]};                                  // {note, chord, mode, decay, bright, spread, mix}
  else if (t == 99) static_cast<DegradeFx*>(fx)->dp = DgDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6]};                                // {bits, rate, noise, color, follow, dry, wet}
  else if (t == 95) static_cast<FlexFx*>(fx)->dp = FlexDp{q[0], q[1], q[2], q[3]};
  else if (t == 94) static_cast<SgateFx*>(fx)->dp = SgDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]};
  else static_cast<RetrigFx*>(fx)->dp = RtDp{q[0], q[1], q[2], q[3], q[4], q[5]};
}

// U18: a pump effect in a slot. Like ClkFx it is an Fx so that plan, commit and retirement own, keep and delete it as they do the others; process() is never called (pumpRun is).
// The plugin instance and the slot's state are the PumpSlot; deleting it frees the plugin (pump_free) on the control thread.
struct PumpHold : Fx {
  PumpSlot* ps;
  explicit PumpHold(PumpSlot* p) : ps(p) {}
  ~PumpHold() override { delete ps; }
  void process(float*, float*, int) override {}
};
static Fx* make_pump(const FxEntry& e, double sr) { PumpSlot* p = PumpSlot::make(e, sr); if (!p) return nullptr; try { return new PumpHold(p); } catch (...) { delete p; throw; } }

static void copyKey(char* d, const char* s) { std::snprintf(d, kFxKey, "%s", s); }
static bool keyHas(const char* k, const char* needle) { return std::strstr(k, needle) != nullptr; }

FxPlan::~FxPlan() {
  for (int i = 0; i < kFxSlots; i++) { delete fx_[i]; delete retired_[i]; }
}

FxHost::FxHost(double sr) : sr_(sr), node_(sr) {
  for (int i = 0; i < 128; i++) tapIdx_[i] = -1;
  std::memset(zero_, 0, sizeof zero_); std::memset(rL_, 0, sizeof rL_); std::memset(rR_, 0, sizeof rR_);
  for (int i = 0; i < 11; i++) lanes_[i] = nullptr;
  try { for (int i = 0; i < 11; i++) lanes_[i] = new AutoN<96>(-1); }
  catch (...) { for (int i = 0; i < 11; i++) delete lanes_[i]; throw; }   // (U34a: a constructor that throws does not run the destructor: what it had made is freed here)
  std::memset(Lb_, 0, sizeof Lb_); std::memset(Rb_, 0, sizeof Rb_); std::memset(cL_, 0, sizeof cL_); std::memset(cR_, 0, sizeof cR_); std::memset(dL_, 0, sizeof dL_); std::memset(dR_, 0, sizeof dR_);
}
FxHost::~FxHost() {
  for (int i = 0; i < nSlots_; i++) delete slots_[i].fx;
  for (int i = 0; i < nDead_; i++) delete dead_[i];
  for (int i = 0; i < 11; i++) delete lanes_[i];
}
uint32_t FxHost::laneDropped() const { uint32_t n = 0; for (int i = 0; i < 11; i++) n += lanes_[i]->dropped; return n; }

// P3b: the random state of a plan that makes its first effect with statics: the first such plan of a host starts from the values a fresh wasm instance has (a render's own plan is
// as before); the k-th later one from a hash of k (splitmix64), so that nothing the audio thread owns is read.
void FxHost::seedPlan(FxPlan* pl) {
  const uint32_t k = planSeq_++; if (!k) return;
  uint64_t z = (uint64_t)k * 0x9E3779B97F4A7C15ull; z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; z ^= z >> 31;
  pl->rs_.rand = z; pl->rs_.stm = (uint32_t)(z >> 32); pl->rs_.daisy = (uint32_t)z;
}
// ---- control thread --------------------------------------------------------------------------------------------------------------------
FxPlan* FxHost::plan(const FxEntry* e, int n, modfx::Seeds* seeds) {
  std::unique_ptr<FxPlan> plp(new FxPlan); FxPlan* pl = plp.get();   // (U34a: an allocation failure part way frees the plan and the effects it has made)
  bool used[kFxSlots] = {false}; Fx* nf[kFxSlots] = {nullptr};
  modfx::SeedCounter ownSeeds; modfx::Seeds& sd = seeds ? *seeds : ownSeeds;   // (the JS's counter is one per audio thread: the caller shares it among the hosts of a render)
  for (int i = 0; i < n && pl->n_ < kFxSlots; i++) {
    const bool pmp = fx_is_pump(e[i].t), mdf = fx_is_mod(e[i].t) && fx_mod_key(e[i].t) != nullptr;
    if (e[i].t < 0 || (e[i].t > 33 && !fx_is_core(e[i].t) && !pmp && !mdf)) { refused_[kRefUnknown]++; continue; }   // not an effect hosted here: left out (the sound passes), counted
    int j = -1; for (int k = 0; k < nCtl_; k++) if (!used[k] && std::strcmp(ctl_[k].key, e[i].key) == 0) { j = k; break; }
    const int w = pl->n_;
    if (j >= 0) { used[j] = true; pl->reuse_[w] = true; nf[w] = ctlFx_[j]; }
    else { Fx* f; if (pmp) f = make_pump(e[i], sr_); else if (mdf) f = fx_make_mod(e[i], sr_, sd); else if (fx_is_core(e[i].t)) f = make_clk(e[i].t, sr_); else { if (!pl->rsUsed_) { pl->rsUsed_ = true; seedPlan(pl); } ModuleState ms(&pl->rs_.rand, &pl->rs_.stm, &pl->rs_.daisy); f = make_fx(e[i].t, (float)sr_); } if (!f) { refused_[kRefNoEngine]++; continue; } pl->fx_[w] = f; nf[w] = f; }
    pl->e_[w] = e[i]; pl->e_[w].pst = nullptr; pl->n_++;
    if (pmp) {   // LATENCY COMP counts pumpLatencyOf(slot): a temporary instance with the slot's values; the one measured for the same values before is kept
      FxEntry& c = pl->e_[w]; const int jj = j;
      if (jj >= 0 && ctl_[jj].npv == c.npv && ctl_[jj].bi == c.bi && std::memcmp(ctl_[jj].pv, c.pv, sizeof(float) * (size_t)c.npv) == 0) c.lat = ctl_[jj].lat;
      else { const int l = PumpSlot::probeLatency(c, sr_); c.lat = l < 0 ? 0 : l; }
    }
  }
  nCtl_ = pl->n_; for (int i = 0; i < nCtl_; i++) { ctl_[i] = pl->e_[i]; ctlFx_[i] = nf[i]; }
  return plp.release();
}

bool FxHost::entryFromSlot(Json o, int slot, double sr, double bpm, double restartN, FxEntry* out, FxSlotText* txt, int* why) {
  if (why) *why = -1;
  if (!o.isObj()) return false;
  const std::string_view type = o.str("type");
  const int id = fx_type_of(type);
  if (id < 0) {   // FX1: a pump effect ("p" + the plugin's type) or a MODFX module
    if (type.size() > 1 && type[0] == 'p') for (int pt = 0; pt < kPumpTypes; pt++) if (type.substr(1) == fx_pump_key(pt)) return fx_entry_pump(o, slot, pt, out, txt, why);
    const int m = fx_mod_index(type); if (m >= 0) return fx_entry_mod(o, slot, m, bpm, out, why);
    if (why) *why = kRefUnknown;
    return false;
  }
  FxEntry& c = *out; c = FxEntry();
  c.nvals = fx_param_count(id); for (int i = 0; i < c.nvals; i++) c.vals[i] = fx_norm_value(o.get(kFxKeyOf(id, i)), id, i);
  Json on = o.get("on"); c.on = !(on.isBool() && !on.asBool());
  c.t = id; c.slot = slot; std::snprintf(c.key, kFxKey, "%d:%s", slot, fx_key_of(id));
  fx_map(id, c.vals, sr, bpm, restartN, c.p);
  return true;
}
FxPlan* FxHost::planFromSong(Json slots, double bpm, double restartN, modfx::Seeds* seeds) {
  FxEntry e[kFxSlots]; FxSlotText txt[kFxSlots]; int n = 0;
  for (int k = 0; k < kFxSlots; k++) {
    int why = -1; Json o = slots.at((size_t)k);
    if (entryFromSlot(o, k, sr_, bpm, restartN, &e[n], &txt[n], &why)) n++;
    else if (why >= 0 && why < kRefN) refused_[why]++;   // a slot the song holds and this host does not play: counted (an empty slot is not)
  }
  return plan(e, n, seeds);
}

// ---- audio thread ----------------------------------------------------------------------------------------------------------------------
static const double kJsPi = 3.141592653589793;   // Math.PI (M_PI is not in MSVC's <cmath> without _USE_MATH_DEFINES)
static void rvMix(double m, double* dry, double* wet) {   // rvMix(m): [cos(a), sin(a)^2]
  const double a = (m < 0 ? 0 : m > 1 ? 1 : m) * kJsPi / 2, s = std::sin(a);
  *dry = std::cos(a); *wet = s * s;
}
void FxHost::applyEntry(Slot& s, const FxEntry& c, bool fresh) {
  copyKey(s.key, c.key); s.t = c.t; s.slot = c.slot; s.on = c.on;
  if (s.pu) {   // a pump slot (set(): `s.pv = c.pv; s.dirty = true`, a new slot has just been staged and is not dirty)
    PumpSlot& u = *s.pu; s.nn = keyHas(c.key, "pcrush");   // (slotNap: "vinyl|pcrush" always run: CRUSH speaks up on its own after a pause)
    if (!fresh) {
      if (u.hasRd && !u.pch) { if (u.npv != c.npv) u.pch = true; else for (int j = 0; j < u.npv; j++) if (u.pv[j] != c.pv[j]) { u.pch = true; break; } }   // values moved while resting: wake
      u.npv = c.npv < kPumpMaxParams ? c.npv : kPumpMaxParams; std::memcpy(u.pv, c.pv, sizeof(float) * (size_t)u.npv); u.dirty = true;
    }
    u.sck = c.sck; u.cr = c.cr; u.clk = c.clk; return;
  }
  if (fx_is_mod(c.t)) { if (s.fx) fx_mod_set(s.fx, c); if (c.bpm > 0) bpm_ = c.bpm; s.wi = -1; s.nn = false; return; }   // a MODFX slot: its module values (and `if (c.bpm) this.bpm = c.bpm`)
  s.wi = c.t == 8 ? 4 : c.t == 20 ? 7 : c.t == 24 ? 3 : -1;   // REVERB MIX: Galactic, Space and Room run fully wet; the blend is rvMix
  s.nn = keyHas(c.key, "vinyl") || keyHas(c.key, "pcrush");
  double pp[12]; for (int i = 0; i < 12; i++) pp[i] = c.p[i];
  if (s.wi >= 0) { rvMix(c.p[s.wi], &s.rvDry, &s.rvWet); pp[s.wi] = 1; }
  if (s.fx && fx_is_core(c.t)) setClkDp(s.fx, c.t, pp);
  else if (s.fx) for (int i = 0; i < 12; i++) s.fx->p[i] = (float)pp[i];
}
void FxHost::commit(FxPlan* pl) {
  if (!pl || pl->committed_) return;
  Slot next[kFxSlots]; FxEntry nc[kFxSlots]; int nn = 0; bool used[kFxSlots] = {false};
  for (int i = 0; i < pl->n_; i++) {
    const FxEntry& c = pl->e_[i]; Slot s;
    if (pl->reuse_[i]) { int j = -1; for (int k = 0; k < nSlots_; k++) if (!used[k] && std::strcmp(slots_[k].key, c.key) == 0) { j = k; break; } if (j < 0) continue; used[j] = true; s = slots_[j]; }
    else { s.fx = pl->fx_[i]; pl->fx_[i] = nullptr; s.slot = c.slot; if (fx_is_pump(c.t)) s.pu = static_cast<PumpHold*>(s.fx)->ps; }
    applyEntry(s, c, !pl->reuse_[i]); nc[nn] = c; next[nn++] = s;
  }
  pl->nRetired_ = 0; for (int k = 0; k < nSlots_; k++) if (!used[k]) pl->retired_[pl->nRetired_++] = slots_[k].fx;
  for (int i = 0; i < nn; i++) { slots_[i] = next[i]; chain_[i] = nc[i]; }
  nSlots_ = nn; nChain_ = nn; idle_ = 0; pl->committed_ = true;
  if (pl->rsUsed_) { rand_ = pl->rs_.rand; stmRng_ = pl->rs_.stm; daisySeed_ = pl->rs_.daisy; }   // P3b: the audio side continues from the plan's state (a plan that made no such effect leaves its own)
  nPump_ = 0; for (int i = 0; i < nn; i++) if (slots_[i].pu) nPump_++;
  if (nPump_) { pumpSeen_ = true; PumpGate g(true); pumpOk_ = g.ok; pumpUp(); pumpOk_ = false; }   // set(): s.up of every pump slot at once
}
void FxHost::kill() { dead_flag_ = true; }

void FxHost::psetAt(double at, int slot, const double* p12) {
  if (nq_ >= kFxQueue) { qDropped_++; return; }
  int i = nq_; while (i > 0 && q_[i - 1].at > at) { q_[i] = q_[i - 1]; i--; }
  q_[i].at = at; q_[i].slot = slot; q_[i].n = 12; for (int k = 0; k < 12; k++) q_[i].p[k] = p12[k]; nq_++;
}
void FxHost::psetMod(double at, int slot, const double* mp, int n) {
  if (nq_ >= kFxQueue) { qDropped_++; return; }
  if (n > kModMaxParams) n = kModMaxParams;
  int i = nq_; while (i > 0 && q_[i - 1].at > at) { q_[i] = q_[i - 1]; i--; }
  q_[i].at = at; q_[i].slot = slot; q_[i].n = n; for (int k = 0; k < n; k++) q_[i].p[k] = mp[k]; nq_++;
}
void FxHost::applyPset(int slot, const double* p12, int n) {   // onmessage {pset}: the chain's entry for the slot gets p (or mp: a MODFX slot), then set(chain) again
  for (int i = 0; i < nChain_; i++) if (chain_[i].slot == slot) { if (fx_is_mod(chain_[i].t)) { for (int k = 0; k < n && k < chain_[i].nmp; k++) chain_[i].mp[k] = p12[k]; } else for (int k = 0; k < 12 && k < n; k++) chain_[i].p[k] = p12[k]; }
  for (int i = 0; i < nSlots_; i++) applyEntry(slots_[i], chain_[i]);
  pumpUp(); idle_ = 0;
}
void FxHost::lane(int k, double v, double at) { if (k >= 0 && k < 11) lanes_[k]->set(v, at); }
void FxHost::reset(double at) { for (int k = 0; k < 11; k++) { lanes_[k]->cancel(at); lanes_[k]->set(-1, at); } }
void FxHost::setRest(bool on, double now) { restAt_ = on ? now + 0.1 : -1; }

bool FxHost::popLevels(float out[8]) { if (!lvlReady_) return false; for (int i = 0; i < 8; i++) out[i] = lvlOut_[i]; lvlReady_ = false; return true; }
int FxHost::popDyn(FxDyn* out, int max) { const int n = std::min(nDynOut_, max); for (int i = 0; i < n; i++) out[i] = dynOut_[i]; nDynOut_ = 0; return n; }

float FxHost::pk(int n) const {   // pk(n): the peak of this.L / this.R over n frames, at most 8
  float p = 0; for (int k = 0; k < n; k++) { const float a = Lb_[k] < 0 ? -Lb_[k] : Lb_[k], b = Rb_[k] < 0 ? -Rb_[k] : Rb_[k]; if (a > p) p = a; if (b > p) p = b; }
  return p < 8 ? p : 8;
}

// The worklet's `s.js.process(L, R, n, s.dp, tr)` / `s.js.feed(L, R, n, tr)`: the core works on the block in place, nothing is guarded after it (no clip, no
// zero: the guards belong to tfx_process, the wasm effects' entry).
void FxHost::runClk(Slot& s, bool feed, int n) {
  switch (s.t) {
    case 95: { FlexFx* f = static_cast<FlexFx*>(s.fx); if (feed) f->core.feed(Lb_, Rb_, n, tr_); else f->core.process(Lb_, Rb_, n, f->dp, tr_); break; }
    case 94: { SgateFx* f = static_cast<SgateFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp, tr_); break; }
    case 100: { GrainsFx* f = static_cast<GrainsFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp, tr_); break; }
    case 101: { ScrambleFx* f = static_cast<ScrambleFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp, tr_); break; }
    case 102: { GlitchFx* f = static_cast<GlitchFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp, tr_); break; }
    case 96: case 97: { PitchFx* f = static_cast<PitchFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp); break; }
    case 98: { ResonFx* f = static_cast<ResonFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp); break; }
    case 99: { DegradeFx* f = static_cast<DegradeFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp); break; }
    case 103: { VSampFx* f = static_cast<VSampFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp); break; }
    default: { RetrigFx* f = static_cast<RetrigFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp, tr_); break; }
  }
}
void FxHost::slotRun(Slot& s, int n) {
  if (!s.fx) return;
  if (s.pu) { pumpRun(s, n); return; }
  if (fx_is_mod(s.t)) { modRun(s, n); return; }
  if (fx_is_core(s.t)) { runClk(s, false, n); return; }   // (a core slot runs its JS core)
  std::memcpy(cL_, Lb_, sizeof(float) * (size_t)n); std::memcpy(cR_, Rb_, sizeof(float) * (size_t)n);
  s.fx->process(Lb_, Rb_, n);
  for (int i = 0; i < n; i++) {   // tfx_process: keep a broken effect from poisoning the whole track
    const float l = Lb_[i], r = Rb_[i];
    if (!(l > -8.f && l < 8.f)) Lb_[i] = 0.f;
    if (!(r > -8.f && r < 8.f)) Rb_[i] = 0.f;
  }
  // (the worklet's "state went bad" test, |x| > 1e4, cannot fire after that guard)
  if (s.wi >= 0) { const double dg = s.rvDry, wg = s.rvWet; for (int k = 0; k < n; k++) { Lb_[k] = (float)((double)cL_[k] * dg + (double)Lb_[k] * wg); Rb_[k] = (float)((double)cR_[k] * dg + (double)Rb_[k] * wg); } }   // REVERB MIX
}
void FxHost::slotNap(Slot& s, int n) {
  if (s.nn) { slotRun(s, n); return; }
  float ip = 0;
  for (int k = 0; k < n; k++) { const float a = Lb_[k] < 0 ? -Lb_[k] : Lb_[k], b = Rb_[k] < 0 ? -Rb_[k] : Rb_[k]; if (a > ip) ip = a; if (b > ip) ip = b; }
  if (s.zz) { if (ip < 1e-9) { for (int k = 0; k < n; k++) { Lb_[k] = 0; Rb_[k] = 0; } return; } s.zz = false; s.zq = 0; }
  slotRun(s, n);
  if (ip < 1e-9) { float op = 0; for (int k = 0; k < n; k++) { const float a = Lb_[k] < 0 ? -Lb_[k] : Lb_[k], b = Rb_[k] < 0 ? -Rb_[k] : Rb_[k]; if (a > op) op = a; if (b > op) op = b; }
    if (op < 1e-9) { s.zq = s.zq + n; if (s.zq >= sr_ * 30) s.zz = true; } else s.zq = 0; }
}

bool FxHost::process(float* L, float* R, int n, double t0, const float* bp, int bpn) {
  if (dead_flag_) return false;
  if (n <= 0) return true;
  if (n > kFxMaxCall) { std::memset(L, 0, sizeof(float) * (size_t)n); std::memset(R, 0, sizeof(float) * (size_t)n); failedBlock_++; return true; }   // tcChunks: a block longer than 4096 fails soft
#ifdef THUNDER_HAVE_TFX
  ModuleState ms(&rand_, &stmRng_, &daisySeed_);
#endif
  for (int k = 0; k < 11; k++) { lanes_[k]->prune(t0); swv_[k] = (float)lanes_[k]->at(t0); }   // the k-rate params, read once: the value at the block's start
  static const float kBpDefault = 0;   // the `bp` AudioParam with nothing connected: its default value, constant over the block
  if (!bp || bpn < 1) { bp = &kBpDefault; bpn = 1; }
  if (n <= kFxBlock) { srcOff_ = 0; return p1(L, R, n, t0, bp, bpn); }
  struct Off { int& o; ~Off() { o = 0; } } off{srcOff_};   // the sidechain sources are cut with the block, as the inputs of tcChunks
  for (int a = 0; a < n; a += kFxBlock) if ((srcOff_ = a, !p1(L + a, R + a, std::min(kFxBlock, n - a), t0, bpn > 1 ? bp + a : bp, bpn > 1 ? std::min(kFxBlock, n - a) : 1))) return false;   // (tcChunks: an a-rate param is cut with the block, a constant one is handed whole)
  return true;
}

// The worklet's `ck`: what its tempo-locked slots ask of the node's sleep, by whether each is ON now (its own switch, or an FXO holding it; or still fading out):
// 0 nothing clocked is on (sleep after 1 s of silence); 1 only switched-off Flex (it keeps a ring of the input for when it comes on): sleep once the silence is
// longer than the 13 s ring, clock running or not; 2 Flex / Step gate on: as 1, and only while the clock is stopped; 3 Retrigger, Grains, Scramble or Glitch on: never (they keep running while stopped).
int FxHost::clockedNeed() const {
  int ck = 0;
  for (int si = 0; si < nSlots_; si++) {
    const Slot& s = slots_[si]; if (!fx_is_clocked(s.t) && !(s.t >= 100 && s.t <= 102) && !(s.pu && s.pu->clk)) continue;   // (the Vintage sampler, 103, asks nothing: the worklet's `c` is 0 for it; a clocked pump effect: GATE, STUTTER, CHOP)
    const int c = s.t == 93 || (s.t >= 100 && s.t <= 102) || s.pu ? 3 : 2;
    const float sv = s.slot >= 0 && s.slot < 8 ? swv_[s.slot] : -1.f;
    const bool live = (sv < -0.5f ? s.on : sv > 0.5f) || (s.pu ? !s.pu->byp : s.g > 0);
    const int w = live ? c : s.t == 95 ? 1 : 0; if (w > ck) ck = w;
  }
  return ck;
}

bool FxHost::p1(float* oL, float* oR, int n, double t0, const float* bp, int bpn) {
  // U18: this block's pump work happens inside the gate (fxpump.cpp, REAL-TIME RULES); while the control thread is making an instance the pump slots pass the sound
  PumpGate gate(nPump_ > 0); pumpOk_ = nPump_ > 0 && gate.ok; if (nPump_ > 0 && !gate.ok) gated_++;
  t0_ = t0; nPtelTmp_ = 0;
  if (nqp_ > 0) { const double due = t0 + 64 / sr_; while (nqp_ > 0 && qp_[0].at <= due) { Qp q = qp_[0]; for (int i = 1; i < nqp_; i++) qp_[i - 1] = qp_[i]; nqp_--; applyPumpPset(q.slot, q.pv, q.n); } }   // FXV of a pump slot at its time
  if (nq_ > 0) { const double due = t0 + 64 / sr_; while (nq_ > 0 && q_[0].at <= due) { Q q = q_[0]; for (int i = 1; i < nq_; i++) q_[i - 1] = q_[i]; nq_--; applyPset(q.slot, q.p, q.n); } }   // THUNDER CORE: FXV at its time
  float* L = Lb_; float* R = Rb_;
  const bool rest = restAt_ > 0 && t0 >= restAt_;
  float ip = 0; bool bad = false;
  if (!rest) for (int k = 0; k < n; k++) { const float a = oL[k], b = oR[k], v = a < 0 ? -a : a, w = b < 0 ? -b : b; if (v > ip) ip = v; if (w > ip) ip = w; if (!(v < 8 && w < 8)) bad = true; }
  const float* swv = swv_;
  if (pqn_) { const double old = t0 - 1; int k = 0; while (k < pqn_ && pq_[k].at < old) k++; if (k) { for (int i = k; i < pqn_; i++) pq_[i - k] = pq_[i]; pqn_ -= k; } if (pqn_) idle_ = 0; }   // CREATIVE: notes due wake it (stale ones are dropped)
  // the sleep. ck: see clockedNeed; a stopped clock is bp[0] < 0 or bp[last] < 0
  const int ck = clockedNeed();
  if ((rest && t0 - restAt_ >= 10) || (ip < 1e-7 && (double)idle_ >= sr_ && (ck == 0 || (ck < 3 && (double)idle_ >= sr_ * 14 && (ck == 1 || !(bp[0] >= 0 && bp[bpn - 1] >= 0)))))) { std::memset(oL, 0, sizeof(float) * (size_t)n); std::memset(oR, 0, sizeof(float) * (size_t)n); asleep_ = true; fr_ += n; if (fr_ >= sr_ / 2) fr_ = 0; return true; }
  asleep_ = false;
  if (!rest) { std::memcpy(L, oL, sizeof(float) * (size_t)n); std::memcpy(R, oR, sizeof(float) * (size_t)n); } else { std::memset(L, 0, sizeof(float) * (size_t)(n + 1)); std::memset(R, 0, sizeof(float) * (size_t)(n + 1)); }
  // A NaN or infinity reaching the effects hangs the tape delay for good: a loud but real sample (8 to 64) is clipped to +-8, the rest zeroed
  if (bad) for (int k = 0; k < n; k++) { const float a = L[k], b = R[k]; if (!(a > -8 && a < 8)) L[k] = a > -64 && a < 64 ? (a < 0 ? -8.f : 8.f) : 0.f; if (!(b > -8 && b < 8)) R[k] = b > -64 && b < 64 ? (b < 0 ? -8.f : 8.f) : 0.f; }
  for (int k = 0; k < n; k += 2) { L[k] = (float)((double)L[k] + 1e-18); L[k + 1] = (float)((double)L[k + 1] - 1e-18); R[k] = (float)((double)R[k] + 1e-18); R[k + 1] = (float)((double)R[k + 1] - 1e-18); }   // anti-denormal
  tr_.bp = bp; tr_.bpn = bpn; tr_.flx = (double)swv_[8]; tr_.gat = (double)swv_[9]; tr_.rtg = (double)swv_[10];   // tr: the clock and the FLX / GAT / RTG params
  pumpClock(bp, bpn, n);   // the modules' / pump's transport: playing while the beat clock runs, at its tempo, and the song position of this block's start
  const bool pqOn = pqn_ > 0;   // notes on their way to a pump effect: none of them naps
  for (int si = 0; si < nSlots_; si++) {
    Slot& s = slots_[si];
    const float sv = s.slot >= 0 && s.slot < 8 ? swv[s.slot] : -1.f;
    const int tg = (sv < -0.5f ? s.on : sv > 0.5f) ? 1 : 0;
    if (s.pu) {   // PUMP: on / off is the plugin's own bypass (latency-matched, crossfaded), so the timing never jumps; METER just stops
      if (!pumpOk_) continue;
      PumpSlot& u = *s.pu; if (sv >= -0.5f) u.fxo = true;
      if (!pumpRest(s, n, tg != 0, pqOn)) {
        if (u.byp != !tg) { u.byp = !tg; u.dirty = true; s.zq = 0; s.zz = false; }
        if (!(u.bi < 0 && u.byp)) { if (u.clk || pqOn) slotRun(s, n); else slotNap(s, n); }
      }
      if (lvl_ && tg && s.slot >= 0 && s.slot < 8) { const float a = pk(n); if (a > lvlP_[s.slot]) lvlP_[s.slot] = a; }
      continue;
    }
    if (!s.gset) { s.g = tg; s.gset = true; }
    tr_.sw = (double)sv;
    if (!tg && s.g <= 0) { if (s.t == 95) runClk(s, true, n); continue; }   // (a switched-off Flex is always fed: switching it on mid-bar already has the bar to play from)
    const bool fade = s.g != (double)tg;
    if (fade) { std::memcpy(dL_, L, sizeof(float) * (size_t)n); std::memcpy(dR_, R, sizeof(float) * (size_t)n); }
    const bool lvm = tel_ && (s.t == 7 || s.t == 26 || s.t == 28);
    if (lvm) { const float a = pk(n); if (!(s.lip > a)) s.lip = a; }
    if (fade) { s.zz = false; s.zq = 0; }   // a forced run must not leave the slot asleep
    if (fade || (fx_is_core(s.t) && !(s.t >= 96 && s.t <= 98))) slotRun(s, n); else slotNap(s, n);   // (the JS cores keep their own time and rings, and the Vintage sampler and Degrade make their own noise: they always run; Pitch delay, Pitch reverb and the Resonator nap like the rest)
    if (lvm) { const float a = pk(n); if (!(s.lop > a)) s.lop = a; }
    if (fade) { const double d = 1 / (sr_ * 0.005); double g = s.g;
      for (int k = 0; k < n; k++) { g = tg ? std::min(1.0, g + d) : std::max(0.0, g - d); L[k] = (float)((double)dL_[k] + ((double)L[k] - (double)dL_[k]) * g); R[k] = (float)((double)dR_[k] + ((double)R[k] - (double)dR_[k]) * g); }
      s.g = g; }
    if (lvl_ && s.slot >= 0 && s.slot < 8) { const float a = pk(n); if (a > lvlP_[s.slot]) lvlP_[s.slot] = a; }   // FX BAR: each slot's level out
  }
  if (lvl_ && ++lvlN_ >= 12) { lvlN_ = 0; std::memcpy(lvlOut_, lvlP_, sizeof lvlP_); lvlReady_ = true; std::memset(lvlP_, 0, sizeof lvlP_); }
  if (tel_ && ++telN_ >= 12) { telN_ = 0; int no = 0;
    for (int si = 0; si < nSlots_; si++) { Slot& s = slots_[si];
      if (s.pu) { pumpTelSlot(s, &no); continue; }   // PUMP: the dynamics display (COMP, LIMIT, BAND), METER / LIMIT loudness
      if (s.t == 7 || s.t == 26 || s.t == 28) {
        FxDyn& d = dynOut_[no++]; d.slot = s.slot; copyKey(d.key, s.key); const double a = (double)s.lip, b = (double)s.lop;
        d.v[0] = 0; d.v[1] = (float)(a > 1e-9 ? 20 * std::log10(a) : -180); d.v[2] = (float)(b > 1e-9 ? 20 * std::log10(b) : -180); s.lip = 0; s.lop = 0; } }
    if (no) nDynOut_ = no;
    if (nPtelTmp_) { for (int i = 0; i < nPtelTmp_; i++) ptelOut_[i] = ptelTmp_[i]; nPtelOut_ = nPtelTmp_; } }
  std::memcpy(oL, L, sizeof(float) * (size_t)n); std::memcpy(oR, R, sizeof(float) * (size_t)n);
  float op = 0; for (int k = 0; k < n; k++) { const float v = L[k] < 0 ? -L[k] : L[k], w = R[k] < 0 ? -R[k] : R[k]; if (v > op) op = v; if (w > op) op = w; }
  idle_ = ip < 1e-7 && op < 1e-6 ? idle_ + n : 0;
  fr_ += n; if (fr_ >= sr_ / 2) { fr_ = 0; pumpReport(); }   // the worklet's half-second report: each pump slot's `up` again and its latency ({plat})
  return true;
}

}  // namespace thunder
