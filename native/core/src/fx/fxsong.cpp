// FX1: pump and MODFX effects as slots of the track and master effect hosts, read from a SONG. See fxmod.h.
//  - the song reader: tfxChain's pump branch (`pv`: every parameter in the plugin's order, f.pump[p.s] ?? p.def after pumpNorm1's pumpClamp; bi, cr, clk, sck, pst) and its MODFX branch
//    (`mp`: TFX[k].map(f): the controls' bytes converted by the table of fx/modfxtable.inc);
//  - the MODFX slot: the worklet's `modRun` (the module's input is the slot's L / R, "inR" linked, the transport of the node's clock, the Source's key; a block that went past +-64 or NaN
//    is not used and the module starts afresh; the output is clipped to +-8) around a modfx::Module.
// THE JS IS THE SPECIFICATION (index.html TFX_SRC, `set()` and `modRun`). REAL-TIME: the module and its spare are made on the control thread; modRun() allocates nothing.
#include "fx/fxmod.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

#include "rt/jsmath.h"
#include "song/song.h"

#ifdef THUNDER_HAVE_TFX
#include "tfx.h"
#else
struct Fx { float p[12]; float sr = 48000.f; virtual ~Fx() {} virtual void process(float*, float*, int) = 0; };
#endif

namespace thunder {

#include "fx/pumpparams.inc"
#include "fx/modfxtable.inc"
// the generated tables against the limits of fxhost.h (the arrays are `static const`, not constexpr: the per-effect parameter counts are checked by the generator's --check and at run time below)
static_assert(kModDefN <= kModMax, "modfxtable.inc holds more effects than kModMax");
static_assert(sizeof(kPumpParamN) / sizeof(kPumpParamN[0]) == (size_t)kPumpTypes, "kPumpParamN must have one entry per pump type");
static_assert(sizeof(kPumpParams) / sizeof(kPumpParams[0]) >= (size_t)kPumpTypes, "kPumpParams must have one entry per pump type");

// ---- small JS rules ---------------------------------------------------------------------------------------------------------------------
static bool jsNumber(Json v, double* out) {   // +v, when it is a number the page's int() / pumpClamp() would take (a number, a boolean, a numeric string)
  if (v.isNum()) { *out = v.asNum(); return true; }
  if (v.isBool()) { *out = v.asBool() ? 1 : 0; return true; }
  if (v.isNull()) { *out = 0; return true; }
  if (v.isStr()) { const std::string s(v.asStr()); if (s.empty()) return false; char* e = nullptr; const double d = std::strtod(s.c_str(), &e); if (e && *e == 0) { *out = d; return true; } }
  return false;
}
static double jsInt(Json v, double d) {   // int(v, d): Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d
  double x; if (v.isNull() || !jsNumber(v, &x) || !std::isfinite(x)) return d;
  return js_round(x);
}
static double u8(double v) { return v == 128 ? 0.5 : (v < 0 ? 0 : v > 255 ? 255 : v) / 255; }   // u8: 128 is exactly .5

// ---- tables -------------------------------------------------------------------------------------------------------------------------------
const char* fx_pump_param_name(int pt, int i) { return pt >= 0 && pt < kPumpTypes && i >= 0 && i < kPumpParamN[pt] ? kPumpParams[pt][i].s : nullptr; }
int fx_mod_index(std::string_view key) { for (int i = 0; i < kModDefN; i++) if (key == kModDefs[i].key) return kModT0 + i; return -1; }
const char* fx_mod_key(int id) { return id >= kModT0 && id < kModT0 + kModDefN ? kModDefs[id - kModT0].key : nullptr; }
int fx_mod_param_count(int id) { return id >= kModT0 && id < kModT0 + kModDefN ? kModDefs[id - kModT0].n : 0; }
bool fx_mod_bounds(int id, int pi, int* bmin, int* bmax) {
  if (id < kModT0 || id >= kModT0 + kModDefN || pi < 0 || pi >= kModDefs[id - kModT0].n) return false;
  const ModParam& q = kModDefs[id - kModT0].ps[pi]; *bmin = q.bmin; *bmax = q.bmax; return true;
}
void fx_mod_map(int id, const double* bytes, double* mp) {   // conv[p][1](f[q]): a choice and a whole number as they are, lin: min + (max - min) * u8(b), log: min * pow(max / min, u8(b))
  const ModDef& d = kModDefs[id - kModT0]; if (d.n > kModMaxParams) return;   // (a regenerated table with more controls than the buffers hold: nothing is written)
  for (int i = 0; i < d.n; i++) { const ModParam& q = d.ps[i]; const double b = bytes[i];
    mp[i] = q.kind == 'g' ? q.lo * js_pow(q.hi / q.lo, u8(b)) : q.kind == 'l' ? q.lo + (q.hi - q.lo) * u8(b) : b; }
}

// ---- the song reader ----------------------------------------------------------------------------------------------------------------------
bool fx_entry_pump(Json o, int slot, int pt, FxEntry* out, FxSlotText* txt, int* why) {
  FxEntry& c = *out; c = FxEntry();
  const char* type = fx_pump_key(pt); Json pump = o.obj("pump");
  const int np = kPumpParamN[pt]; c.npv = np < kPumpMaxParams ? np : kPumpMaxParams;
  for (int i = 0; i < c.npv; i++) {   // pumpNorm1: the symbol's value clamped (pumpClamp), a missing one at its start value; tfxChain: f.pump[p.s] ?? p.def
    const PumpParam& p = kPumpParams[pt][i]; double v = p.st; Json j = pump.get(p.s);
    if (!j.missing()) { double x; if (!jsNumber(j, &x) || x != x) v = p.def; else { v = x > p.mx ? p.mx : x < p.mn ? p.mn : x; if (p.flags & 6) v = js_round(v); } }
    c.pv[i] = (float)v;
  }
  c.on = !(o.get("on").isBool() && !o.get("on").asBool());
  c.t = kPumpT0 + pt; c.slot = slot; std::snprintf(c.key, kFxKey, "%d:p%s", slot, type);
  c.bi = fx_pump_bypass(pt); c.cr = fx_pump_creative(pt); c.clk = fx_pump_clocked(pt);
  const bool hasSck = !std::strcmp(type, "comp") || !std::strcmp(type, "vox"), hasNin = !std::strcmp(type, "vox") || !std::strcmp(type, "tune") || !std::strcmp(type, "twang") || !std::strcmp(type, "stutter") || !std::strcmp(type, "chop");
  if (hasSck) { double s = jsInt(o.get("sck"), 0); c.sck = (int)(s < 0 ? 0 : s > 8 + kSongNT ? 8 + kSongNT : s); }
  if (c.sck > 0) { if (why) *why = kRefSidechain; return false; }   // COMP's External sidechain / VOX's source: the track taps and sends are not wired yet (C3)
  if (hasNin && jsInt(o.get("nin"), 0) > 0) { if (why) *why = kRefNotesFrom; return false; }
  if (txt) {   // pst: master's analysis, tune's notes (tune's curve is the window's own: PUMP_STATE_UI); an empty text is not given (pumpNew: `if (s.pst[key])`)
    Json pst = o.obj("pst"); txt->n = 0;
    for (const char* k : {"analysis", "notes"}) { Json v = pst.get(k); if (!v.isStr() || v.asStr().empty() || txt->n >= 2) continue;
      const bool ok = (!std::strcmp(k, "analysis") && !std::strcmp(type, "master")) || (!std::strcmp(k, "notes") && !std::strcmp(type, "tune")); if (!ok) continue;
      txt->key[txt->n] = k; txt->val[txt->n] = std::string(v.asStr().substr(0, 2000000)); txt->n++; }
    if (txt->n) c.pst = txt;
  }
  return true;
}

bool fx_entry_mod(Json o, int slot, int id, double bpm, FxEntry* out, int* why) {
  FxEntry& c = *out; c = FxEntry();
  const ModDef& d = kModDefs[id - kModT0]; if (d.n > kModMaxParams) { if (why) *why = kRefUnknown; return false; }
  c.nmp = d.n;
  for (int i = 0; i < d.n; i++) { const ModParam& q = d.ps[i];   // normTfx: clamp(int(o[q], d), min, max)
    double b = jsInt(o.get(q.q), q.d); b = b < q.bmin ? q.bmin : b > q.bmax ? q.bmax : b; c.mb[i] = b;
    if (!std::strcmp(q.p, "sck") && b > 0) { if (why) *why = kRefSidechain; return false; } }
  fx_mod_map(id, c.mb, c.mp);
  c.on = !(o.get("on").isBool() && !o.get("on").asBool());
  c.t = id; c.slot = slot; c.bpm = bpm; std::snprintf(c.key, kFxKey, "%d:%s", slot, d.key);
  return true;
}

// ---- the MODFX slot -----------------------------------------------------------------------------------------------------------------------
struct ModFx : Fx {
  const modfx::Def* def = nullptr;
  std::unique_ptr<modfx::Module> mod, spare;   // spare: made with the slot (own seeds), taken when the output goes bad; what it replaces stays here, freed with the slot
  bool spareLive = false, dead = false;
  double p[32] = {0}; int np = 0;
  int src[32]; double pdef[32];               // module parameter j <- entry mp[src[j]] (-1: the module's default)
  int inL = -1, inR = -1, outL = -1, outR = -1;
  modfx::IO io; modfx::Ctx ctx;
  std::unique_ptr<float[]> buf; int nbuf = 0; float* zero = nullptr, *wL = nullptr, *wR = nullptr;   // wL / wR: the writable input buffers
  void process(float*, float*, int) override {}
  ModFx() { std::memset(src, 0xff, sizeof src); for (double& d : pdef) d = 0; }
};

Fx* fx_make_mod(const FxEntry& e, double sr, modfx::Seeds& seeds) {
  const ModDef& d = kModDefs[e.t - kModT0]; const modfx::Def* def = modfx::find(d.key);
  if (!def || def->np > 32 || def->nin > modfx::kMaxPorts || def->nout > modfx::kMaxPorts || def->nout == 0) return nullptr;   // (modRun reads io.out[0])
  std::unique_ptr<ModFx> m(new ModFx); m->def = def;
  m->mod = def->make(sr, seeds); if (!m->mod) return nullptr;
  { modfx::SeedCounter own(54321); m->spare = def->make(sr, own); m->spareLive = m->spare != nullptr; }   // its own seeds: the shared counter's order is the page's
  m->np = def->np;
  for (int j = 0; j < def->np; j++) { m->pdef[j] = def->params[j].def; for (int i = 0; i < d.n; i++) if (!std::strcmp(def->params[j].name, d.ps[i].p)) { m->src[j] = i; break; } }
  for (int k = 0; k < def->nin; k++) { if (!std::strcmp(def->in[k].name, "in")) m->inL = k; else if (!std::strcmp(def->in[k].name, "inR")) m->inR = k; }
  for (int k = 0; k < def->nout; k++) { if (!std::strcmp(def->out[k].name, "out")) m->outL = k; else if (!std::strcmp(def->out[k].name, "outR")) m->outR = k; }
  const int B = modfx::kBlock; m->nbuf = 3 + def->nout;   // zero, L in, R in, then one per output
  m->buf.reset(new float[(size_t)m->nbuf * B]());
  float* z = m->buf.get(); m->zero = z;
  for (int k = 0; k < def->nin; k++) { m->io.in[k] = z; m->io.linked[k] = false; }
  if (m->inL >= 0) { m->io.in[m->inL] = z + B; m->wL = z + B; }
  if (m->inR >= 0) { m->wR = z + 2 * B; m->io.in[m->inR] = z + 2 * B; m->io.linked[m->inR] = true; }   // linked: new Set(['inR'])
  for (int k = 0; k < def->nout; k++) m->io.out[k] = z + (3 + k) * B;
  m->io.p = m->p;
  m->ctx.sr = sr; m->ctx.block = B; m->ctx.full = true; m->ctx.tempo = 120; m->ctx.lpb = 4; m->ctx.ticks = 6; m->ctx.playing = false; m->ctx.startN = 0;
  m->ctx.key = nullptr; m->ctx.keyUser = nullptr; m->ctx.irs = nullptr;   // (modRun hands the host's sources)
  fx_mod_set(m.get(), e);
  m->sr = (float)sr;
  return m.release();
}
void fx_mod_set(Fx* f, const FxEntry& e) {   // `s.io.p[k] = typeof v === 'number' && v === v ? v : s.def.params[k][0]`
  ModFx* m = static_cast<ModFx*>(f);
  for (int j = 0; j < m->np; j++) { const int i = m->src[j]; const double v = i >= 0 && i < e.nmp ? e.mp[i] : std::nan(""); m->p[j] = v == v ? v : m->pdef[j]; }
}

static bool hotBad(const float* oL, const float* oR, int n) { for (int k = 0; k < n; k++) { const float a = oL[k], b = oR[k]; if (!(a > -64 && a < 64 && b > -64 && b < 64)) return true; } return false; }

// The worklet's modRun for one block of n <= 128 frames (Lb_ / Rb_ in, out).
void FxHost::modRun(Slot& s, int n) {
  ModFx* m = static_cast<ModFx*>(s.fx); if (!m || m->dead) return;
  const int B = modfx::kBlock;
  if (m->wL) std::memcpy(m->wL, Lb_, sizeof(float) * (size_t)n);   // io.in.in.set(L); io.in.inR.set(R)
  if (m->wR) std::memcpy(m->wR, Rb_, sizeof(float) * (size_t)n);
  modfx::Ctx& c = m->ctx; c.tempo = playing_ ? tempo_ : bpm_; c.playing = playing_; c.startN = (double)startN_; c.block = B; c.fail = false;
  struct Keys { static const float* f(void* u, double v) { FxHost* h = (FxHost*)u; const int k = (int)js_round(v); return h->srcKey(k); } };
  c.key = &Keys::f; c.keyUser = this;
  if (!m->mod->run(m->io, c) || c.fail) { m->dead = true; modDead_++; return; }   // crashed: bypassed from now on
  const float* oL = m->io.out[m->outL >= 0 ? m->outL : 0]; const float* oR = m->outR >= 0 ? m->io.out[m->outR] : oL;
  if (hotBad(oL, oR, n)) {   // went bad: start it afresh, pass this block. The fresh one is the spare made on the control thread; a second time the slot passes the sound for good
    if (m->spareLive) { std::swap(m->mod, m->spare); m->spareLive = false; modRemakes_++; } else { m->dead = true; modDead_++; }
    return; }
  std::memcpy(Lb_, oL, sizeof(float) * (size_t)n); std::memcpy(Rb_, oR, sizeof(float) * (size_t)n);
  for (int k = 0; k < n; k++) { const float a = Lb_[k], b = Rb_[k]; if (a > 8) Lb_[k] = 8; else if (a < -8) Lb_[k] = -8; if (b > 8) Rb_[k] = 8; else if (b < -8) Rb_[k] = -8; }   // hotClip
}

}  // namespace thunder
