// See fxhost.h. Each block of code keeps the worklet's statements and arithmetic order (index.html TFX_SRC: set, p1, slotRun, slotNap, tcChunks).
#include "fx/fxhost.h"

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
    case 19: f = make_warps(sr); break;     case 20: f = make_cloudseed(sr); break;
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
static Fx* make_clk(int type, double sr) {
  switch (type) { case 93: return new RetrigFx(sr); case 94: return new SgateFx(sr); case 95: return new FlexFx(sr); default: return nullptr; }
}
// c.p -> s.dp: the worklet's set(): {pat, mix, xf, wait} (Flex), {pat, m1, m2, div, len, depth, smooth, wait} (Step gate), {mode, div, decay, pitch, gate, mix} (Retrigger)
static void setClkDp(Fx* fx, int t, const double* q) {
  if (t == 95) static_cast<FlexFx*>(fx)->dp = FlexDp{q[0], q[1], q[2], q[3]};
  else if (t == 94) static_cast<SgateFx*>(fx)->dp = SgDp{q[0], q[1], q[2], q[3], q[4], q[5], q[6], q[7]};
  else static_cast<RetrigFx*>(fx)->dp = RtDp{q[0], q[1], q[2], q[3], q[4], q[5]};
}

static void copyKey(char* d, const char* s) { std::snprintf(d, kFxKey, "%s", s); }
static bool keyHas(const char* k, const char* needle) { return std::strstr(k, needle) != nullptr; }

FxPlan::~FxPlan() {
  for (int i = 0; i < kFxSlots; i++) { delete fx_[i]; delete retired_[i]; }
}

FxHost::FxHost(double sr) : sr_(sr) {
  for (int i = 0; i < 11; i++) lanes_[i] = new AutoN<96>(-1);
  std::memset(Lb_, 0, sizeof Lb_); std::memset(Rb_, 0, sizeof Rb_); std::memset(cL_, 0, sizeof cL_); std::memset(cR_, 0, sizeof cR_); std::memset(dL_, 0, sizeof dL_); std::memset(dR_, 0, sizeof dR_);
}
FxHost::~FxHost() {
  for (int i = 0; i < nSlots_; i++) delete slots_[i].fx;
  for (int i = 0; i < nDead_; i++) delete dead_[i];
  for (int i = 0; i < 11; i++) delete lanes_[i];
}
uint32_t FxHost::laneDropped() const { uint32_t n = 0; for (int i = 0; i < 11; i++) n += lanes_[i]->dropped; return n; }

// ---- control thread --------------------------------------------------------------------------------------------------------------------
FxPlan* FxHost::plan(const FxEntry* e, int n) {
  FxPlan* pl = new FxPlan; bool used[kFxSlots] = {false}; Fx* nf[kFxSlots] = {nullptr};
  for (int i = 0; i < n && pl->n_ < kFxSlots; i++) {
    if (e[i].t < 0 || (e[i].t > 33 && !fx_is_clocked(e[i].t))) continue;   // not an effect hosted here: left out (the sound passes)
    int j = -1; for (int k = 0; k < nCtl_; k++) if (!used[k] && std::strcmp(ctl_[k].key, e[i].key) == 0) { j = k; break; }
    const int w = pl->n_;
    if (j >= 0) { used[j] = true; pl->reuse_[w] = true; nf[w] = ctlFx_[j]; }
    else { Fx* f; if (fx_is_clocked(e[i].t)) f = make_clk(e[i].t, sr_); else { ModuleState ms(&rand_, &stmRng_, &daisySeed_); f = make_fx(e[i].t, (float)sr_); } if (!f) continue; pl->fx_[w] = f; nf[w] = f; }
    pl->e_[w] = e[i]; pl->n_++;
  }
  nCtl_ = pl->n_; for (int i = 0; i < nCtl_; i++) { ctl_[i] = pl->e_[i]; ctlFx_[i] = nf[i]; }
  return pl;
}

bool FxHost::entryFromSlot(Json o, int slot, double sr, double bpm, double restartN, FxEntry* out) {
  if (!o.isObj()) return false;
  const int id = fx_type_of(o.str("type")); if (id < 0) return false;
  FxEntry& c = *out; c = FxEntry();
  c.nvals = fx_param_count(id); for (int i = 0; i < c.nvals; i++) c.vals[i] = fx_norm_value(o.get(kFxKeyOf(id, i)), id, i);
  Json on = o.get("on"); c.on = !(on.isBool() && !on.asBool());
  c.t = id; c.slot = slot; std::snprintf(c.key, kFxKey, "%d:%s", slot, fx_key_of(id));
  fx_map(id, c.vals, sr, bpm, restartN, c.p);
  return true;
}
FxPlan* FxHost::planFromSong(Json slots, double bpm, double restartN) {
  FxEntry e[kFxSlots]; int n = 0;
  for (int k = 0; k < kFxSlots; k++) if (entryFromSlot(slots.at((size_t)k), k, sr_, bpm, restartN, &e[n])) n++;
  return plan(e, n);
}

// ---- audio thread ----------------------------------------------------------------------------------------------------------------------
static const double kJsPi = 3.141592653589793;   // Math.PI (M_PI is not in MSVC's <cmath> without _USE_MATH_DEFINES)
static void rvMix(double m, double* dry, double* wet) {   // rvMix(m): [cos(a), sin(a)^2]
  const double a = (m < 0 ? 0 : m > 1 ? 1 : m) * kJsPi / 2, s = std::sin(a);
  *dry = std::cos(a); *wet = s * s;
}
void FxHost::applyEntry(Slot& s, const FxEntry& c) {
  copyKey(s.key, c.key); s.t = c.t; s.slot = c.slot; s.on = c.on;
  s.wi = c.t == 8 ? 4 : c.t == 20 ? 7 : c.t == 24 ? 3 : -1;   // REVERB MIX: Galactic, Space and Room run fully wet; the blend is rvMix
  s.nn = keyHas(c.key, "vinyl") || keyHas(c.key, "pcrush");
  double pp[12]; for (int i = 0; i < 12; i++) pp[i] = c.p[i];
  if (s.wi >= 0) { rvMix(c.p[s.wi], &s.rvDry, &s.rvWet); pp[s.wi] = 1; }
  if (s.fx && fx_is_clocked(c.t)) setClkDp(s.fx, c.t, pp);
  else if (s.fx) for (int i = 0; i < 12; i++) s.fx->p[i] = (float)pp[i];
}
void FxHost::commit(FxPlan* pl) {
  if (!pl || pl->committed_) return;
  Slot next[kFxSlots]; FxEntry nc[kFxSlots]; int nn = 0; bool used[kFxSlots] = {false};
  for (int i = 0; i < pl->n_; i++) {
    const FxEntry& c = pl->e_[i]; Slot s;
    if (pl->reuse_[i]) { int j = -1; for (int k = 0; k < nSlots_; k++) if (!used[k] && std::strcmp(slots_[k].key, c.key) == 0) { j = k; break; } if (j < 0) continue; used[j] = true; s = slots_[j]; }
    else { s.fx = pl->fx_[i]; pl->fx_[i] = nullptr; s.slot = c.slot; }
    applyEntry(s, c); nc[nn] = c; next[nn++] = s;
  }
  pl->nRetired_ = 0; for (int k = 0; k < nSlots_; k++) if (!used[k]) pl->retired_[pl->nRetired_++] = slots_[k].fx;
  for (int i = 0; i < nn; i++) { slots_[i] = next[i]; chain_[i] = nc[i]; }
  nSlots_ = nn; nChain_ = nn; idle_ = 0; pl->committed_ = true;
}
void FxHost::kill() { dead_flag_ = true; }

void FxHost::psetAt(double at, int slot, const double* p12) {
  if (nq_ >= kFxQueue) { qDropped_++; return; }
  int i = nq_; while (i > 0 && q_[i - 1].at > at) { q_[i] = q_[i - 1]; i--; }
  q_[i].at = at; q_[i].slot = slot; for (int k = 0; k < 12; k++) q_[i].p[k] = p12[k]; nq_++;
}
void FxHost::applyPset(int slot, const double* p12) {   // onmessage {pset}: the chain's entry for the slot gets p, then set(chain) again
  for (int i = 0; i < nChain_; i++) if (chain_[i].slot == slot) for (int k = 0; k < 12; k++) chain_[i].p[k] = p12[k];
  for (int i = 0; i < nSlots_; i++) applyEntry(slots_[i], chain_[i]);
  idle_ = 0;
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
    default: { RetrigFx* f = static_cast<RetrigFx*>(s.fx); f->core.process(Lb_, Rb_, n, f->dp, tr_); break; }
  }
}
void FxHost::slotRun(Slot& s, int n) {
  if (!s.fx) return;
  if (fx_is_clocked(s.t)) { runClk(s, false, n); return; }
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
  if (n <= kFxBlock) return p1(L, R, n, t0, bp, bpn);
  for (int a = 0; a < n; a += kFxBlock) if (!p1(L + a, R + a, std::min(kFxBlock, n - a), t0, bpn > 1 ? bp + a : bp, bpn > 1 ? std::min(kFxBlock, n - a) : 1)) return false;   // (tcChunks: an a-rate param is cut with the block, a constant one is handed whole)
  return true;
}

// The worklet's `ck`: what its tempo-locked slots ask of the node's sleep, by whether each is ON now (its own switch, or an FXO holding it; or still fading out):
// 0 nothing clocked is on (sleep after 1 s of silence); 1 only switched-off Flex (it keeps a ring of the input for when it comes on): sleep once the silence is
// longer than the 13 s ring, clock running or not; 2 Flex / Step gate on: as 1, and only while the clock is stopped; 3 Retrigger on: never (it keeps running while stopped).
int FxHost::clockedNeed(const float* bp, int bpn) const {
  (void)bp; (void)bpn;
  int ck = 0;
  for (int si = 0; si < nSlots_; si++) {
    const Slot& s = slots_[si]; if (!fx_is_clocked(s.t)) continue;
    const int c = s.t == 93 ? 3 : 2;
    const float sv = s.slot >= 0 && s.slot < 8 ? swv_[s.slot] : -1.f;
    const bool live = (sv < -0.5f ? s.on : sv > 0.5f) || s.g > 0;
    const int w = live ? c : s.t == 95 ? 1 : 0; if (w > ck) ck = w;
  }
  return ck;
}

bool FxHost::p1(float* oL, float* oR, int n, double t0, const float* bp, int bpn) {
  if (nq_ > 0) { const double due = t0 + 64 / sr_; while (nq_ > 0 && q_[0].at <= due) { Q q = q_[0]; for (int i = 1; i < nq_; i++) q_[i - 1] = q_[i]; nq_--; applyPset(q.slot, q.p); } }   // THUNDER CORE: FXV at its time
  float* L = Lb_; float* R = Rb_;
  const bool rest = restAt_ > 0 && t0 >= restAt_;
  float ip = 0; bool bad = false;
  if (!rest) for (int k = 0; k < n; k++) { const float a = oL[k], b = oR[k], v = a < 0 ? -a : a, w = b < 0 ? -b : b; if (v > ip) ip = v; if (w > ip) ip = w; if (!(v < 8 && w < 8)) bad = true; }
  const float* swv = swv_;
  // the sleep. ck: see clockedNeed; a stopped clock is bp[0] < 0 or bp[last] < 0
  const int ck = clockedNeed(bp, bpn);
  if ((rest && t0 - restAt_ >= 10) || (ip < 1e-7 && (double)idle_ >= sr_ && (ck == 0 || (ck < 3 && (double)idle_ >= sr_ * 14 && (ck == 1 || !(bp[0] >= 0 && bp[bpn - 1] >= 0)))))) { std::memset(oL, 0, sizeof(float) * (size_t)n); std::memset(oR, 0, sizeof(float) * (size_t)n); asleep_ = true; return true; }
  asleep_ = false;
  if (!rest) { std::memcpy(L, oL, sizeof(float) * (size_t)n); std::memcpy(R, oR, sizeof(float) * (size_t)n); } else { std::memset(L, 0, sizeof(float) * (size_t)(n + 1)); std::memset(R, 0, sizeof(float) * (size_t)(n + 1)); }
  // A NaN or infinity reaching the effects hangs the tape delay for good: a loud but real sample (8 to 64) is clipped to +-8, the rest zeroed
  if (bad) for (int k = 0; k < n; k++) { const float a = L[k], b = R[k]; if (!(a > -8 && a < 8)) L[k] = a > -64 && a < 64 ? (a < 0 ? -8.f : 8.f) : 0.f; if (!(b > -8 && b < 8)) R[k] = b > -64 && b < 64 ? (b < 0 ? -8.f : 8.f) : 0.f; }
  for (int k = 0; k < n; k += 2) { L[k] = (float)((double)L[k] + 1e-18); L[k + 1] = (float)((double)L[k + 1] - 1e-18); R[k] = (float)((double)R[k] + 1e-18); R[k + 1] = (float)((double)R[k + 1] - 1e-18); }   // anti-denormal
  tr_.bp = bp; tr_.bpn = bpn; tr_.flx = (double)swv_[8]; tr_.gat = (double)swv_[9]; tr_.rtg = (double)swv_[10];   // tr: the clock and the FLX / GAT / RTG params
  for (int si = 0; si < nSlots_; si++) {
    Slot& s = slots_[si];
    const float sv = s.slot >= 0 && s.slot < 8 ? swv[s.slot] : -1.f;
    const int tg = (sv < -0.5f ? s.on : sv > 0.5f) ? 1 : 0;
    if (!s.gset) { s.g = tg; s.gset = true; }
    tr_.sw = (double)sv;
    if (!tg && s.g <= 0) { if (s.t == 95) runClk(s, true, n); continue; }   // (a switched-off Flex is always fed: switching it on mid-bar already has the bar to play from)
    const bool fade = s.g != (double)tg;
    if (fade) { std::memcpy(dL_, L, sizeof(float) * (size_t)n); std::memcpy(dR_, R, sizeof(float) * (size_t)n); }
    const bool lvm = tel_ && (s.t == 7 || s.t == 26 || s.t == 28);
    if (lvm) { const float a = pk(n); if (!(s.lip > a)) s.lip = a; }
    if (fade) { s.zz = false; s.zq = 0; }   // a forced run must not leave the slot asleep
    if (fade || fx_is_clocked(s.t)) slotRun(s, n); else slotNap(s, n);   // (the tempo-locked cores keep their own time and rings: they always run)
    if (lvm) { const float a = pk(n); if (!(s.lop > a)) s.lop = a; }
    if (fade) { const double d = 1 / (sr_ * 0.005); double g = s.g;
      for (int k = 0; k < n; k++) { g = tg ? std::min(1.0, g + d) : std::max(0.0, g - d); L[k] = (float)((double)dL_[k] + ((double)L[k] - (double)dL_[k]) * g); R[k] = (float)((double)dR_[k] + ((double)R[k] - (double)dR_[k]) * g); }
      s.g = g; }
    if (lvl_ && s.slot >= 0 && s.slot < 8) { const float a = pk(n); if (a > lvlP_[s.slot]) lvlP_[s.slot] = a; }   // FX BAR: each slot's level out
  }
  if (lvl_ && ++lvlN_ >= 12) { lvlN_ = 0; std::memcpy(lvlOut_, lvlP_, sizeof lvlP_); lvlReady_ = true; std::memset(lvlP_, 0, sizeof lvlP_); }
  if (tel_ && ++telN_ >= 12) { telN_ = 0; int no = 0;
    for (int si = 0; si < nSlots_; si++) { Slot& s = slots_[si];
      if (s.t == 7 || s.t == 26 || s.t == 28) {
        FxDyn& d = dynOut_[no++]; d.slot = s.slot; copyKey(d.key, s.key); const double a = (double)s.lip, b = (double)s.lop;
        d.v[0] = 0; d.v[1] = (float)(a > 1e-9 ? 20 * std::log10(a) : -180); d.v[2] = (float)(b > 1e-9 ? 20 * std::log10(b) : -180); s.lip = 0; s.lop = 0; } }
    if (no) nDynOut_ = no; }
  std::memcpy(oL, L, sizeof(float) * (size_t)n); std::memcpy(oR, R, sizeof(float) * (size_t)n);
  float op = 0; for (int k = 0; k < n; k++) { const float v = L[k] < 0 ? -L[k] : L[k], w = R[k] < 0 ? -R[k] : R[k]; if (v > op) op = v; if (w > op) op = w; }
  idle_ = ip < 1e-7 && op < 1e-6 ? idle_ + n : 0;
  return true;
}

}  // namespace thunder
