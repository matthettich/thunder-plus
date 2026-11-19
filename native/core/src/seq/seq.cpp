// THE SEQUENCER (U07). See seq.h. A port of makeSeq and the helpers it reads (core/seq-core.js lines 84-803, build 616): the
// comments name the JS they stand for, and the statements keep its order, because the order of the random draws, of the
// events and of every floating-point operation is what the goldens compare.
//
// PORTED AS IT IS (things in the JS that look like slips; changing them would change what songs play):
//   - Object.assign(T, fresh()) (JMP, wake, a hand-over) does not clear what fresh() does not list: the track's CUT / RES /
//     DEC / ATK / REL, FXS, SCA, its locks, the CND 1ST counts, the last condition's result, the last `eng` state and "was
//     muted". They live in Tr's second half here and are never reset.
//   - RND / RNL with no command to their left draw even when the row has no note.
//   - CND's NEI on track 0 with X = 0 looks at "track -1": never true.
//   - PRB with nothing to its left pushes CHA 00 (the row never plays) instead of rolling the note's own chance.
//   - A retrigger's level with RTV uses the gain after the first hit was scaled, divided by max(1e-6, level(0)).
//   - RET's count K is taken from `nat` (after HUMAN TIME moved the note), the loop's times too.
//   - `rate` of a KEY FOLLOW note reads S.tune by the pitch class of the unsnapped fractional note: with FIN the class is
//     not a whole number and the tuning is 0.
//   - The master column's SED / TCH are read before the tracks' rows only when its row has no RND, RNL or REP.
//   - lfoAt's random shape multiplies (cyc | 0) by 0x6D2B79F5 as doubles (not Math.imul): above 2^53 the product is rounded
//     before it is brought to 32 bits. Done the same way here.
// NOT THE SAME (and why): a target id that tgtOk refuses (a malformed lock id) is inert here, where the JS would still act on
// its first two parts or throw; a part with no `mix` array or instrument objects missing fields throws in the JS and plays
// the song's own here; Math.sin (the sine LFO) and Math.log (a banded pump parameter's own byte) are the C library's until
// U08 brings fdlibm's (their results are rounded to a byte or to 1/1000 before anything sees them).
#include "seq/seq.h"
#include "seq/session.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <set>

#include "rt/jsmath.h"
#include "rt/dbl.h"
#include "rt/num.h"

namespace thunder {

namespace {

const double kInf = std::numeric_limits<double>::infinity();
const double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr int NT = kSongNT, MT = kSeqMT, NI = kNI, ROOT = 48, NOTE_OFF = 200, TICKS = 6, PROWS = kPhraseRows, NCOL = 4, VC = 4, IC = 5, FX0 = kFX0, RW = kRW,
  FXSLOTS = 8, NPAD = 8, PERF_MACROS = kSeqMacros;

// FXC, in index.html's order (the numbers a phrase stores)
enum Fxc { ARP = 0, ARC, CHA, DEL, GRV, GGR, HOP, INS, KIL, OFF, NTH, RET, REP, RND, RNL, PSL, PBN, PVB, SED, TPO, TSP, VOL, PIT, FIN, PLY, STA, PAN, TIC, WOB, FXO, FLX, GAT, RTG,
  CCA, CCB, CCC, CCD, PGM, BYP, CSL, DIR, MUT, GEN, PRB, FXS, FXV, SCA, SCG, CUT, RES, DEC, FDI, TRM, APN, TVL, TPN, STP, LPB, PDL, ONE, MPB, MAT, CND, NUD, RTV, SLC, XFD, MAC,
  LK1, LK2, LK3, LK4, PRT, END, JMP, FLY, ATK, REL, TCH, SKP, CHD, FXC_N };
static_assert(FXC_N == kFxcCount, "FXC has 81 commands");

inline bool isSigned(int c) { return c == PBN || c == TSP || c == PIT || c == FIN || c == NUD || c == RTV; }
inline int rstep(int c) {   // RSTEP[c] || 17
  switch (c) { case TIC: case DEL: case KIL: case OFF: case PSL: case RET: case PBN: case TSP: case PIT: case ARP: case HOP: case INS: case PGM: case CSL: case DIR: return 1; case FIN: return 4; default: return 17; }
}
inline int s8(int v) { return v > 127 ? v - 256 : v; }
inline int ci(int v, int a, int b) { return std::max(a, std::min(b, v)); }
// clamp = (v, a, b) => Math.max(a, Math.min(b, v)): NaN stays NaN
inline double cd(double v, double a, double b) { if (v != v) return v; return std::max(a, std::min(b, v)); }
const double BASS_DIV[13] = {16, 8, 4, 2, 4.0 / 3, 1, 2.0 / 3, 0.75, 0.5, 1.0 / 3, 0.25, 1.0 / 6, 0.125};
inline double wobOf(int v) { return v ? BASS_DIV[std::min(13, v) - 1] : 0; }
inline double curve(double v) { return js_pow(std::max(0.0, std::min(255.0, v)) / 255, 2); }   // (NaN: Math.max / Math.min give NaN; std::max(0, NaN) gives 0: a part's instrument without `vol`, see the top)
inline double decSec(double v) { return 0.008 * js_pow(4 / 0.008, cd(v, 0, 254) / 254); }
const double PLFO_RATES[9] = {1.0 / 8, 1.0 / 4, 1.0 / 2, 1, 2, 4, 8, 16, 32};

// SCALES[i][1]
const int8_t SCALE_N[15] = {0, 7, 7, 7, 7, 7, 7, 7, 7, 7, 5, 5, 6, 6, 8};
const int8_t SCALE_PC[15][8] = {{0}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 9, 11}, {0, 2, 3, 5, 7, 9, 10}, {0, 1, 3, 5, 7, 8, 10},
  {0, 2, 4, 6, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10}, {0, 1, 3, 5, 6, 8, 10}, {0, 2, 4, 7, 9}, {0, 3, 5, 7, 10}, {0, 3, 5, 6, 7, 10}, {0, 2, 4, 6, 8, 10}, {0, 2, 3, 5, 6, 8, 9, 11}};
inline bool scaleHas(int sc, double pc) { for (int i = 0; i < SCALE_N[sc]; i++) if ((double)SCALE_PC[sc][i] == pc) return true; return false; }
double scaleSnap(int scale, int sroot, double n) {
  if (scale < 1 || scale > 14) return n;
  const double pc = std::fmod(std::fmod(n - sroot, 12) + 12, 12); if (scaleHas(scale, pc)) return n;
  for (int d = 1; d < 7; d++) { if (scaleHas(scale, std::fmod(pc - d + 12, 12))) return n - d; if (scaleHas(scale, std::fmod(pc + d, 12))) return n + d; }
  return n;
}

// lfoAt(sh, ph, cyc)
double lfoAt(double sh, double ph, double cyc) {
  if (sh == 0) return std::sin(ph * 2 * 3.141592653589793);
  if (sh == 1) return ph < 0.5 ? ph * 4 - 1 : 3 - ph * 4;
  if (sh == 2) return ph * 2 - 1;
  if (sh == 3) return 1 - ph * 2;
  if (sh == 4) return ph < 0.5 ? 1 : -1;
  uint32_t x = js_to_uint32((double)js_to_int32(cyc) * (double)0x6D2B79F5 + (double)0x1234567);
  x = js_imul(x ^ (x >> 15), x | 1u); x ^= x + js_imul(x ^ (x >> 7), x | 61u);
  return ((double)(x ^ (x >> 14)) / 4294967296.0) * 2 - 1;
}
inline uint32_t hmix(uint32_t x) { x = js_imul(x ^ (x >> 16), 0x85EBCA6Bu); x = js_imul(x ^ (x >> 13), 0xC2B2AE35u); return x ^ (x >> 16); }

// ---- JSON as the JS reads it ------------------------------------------------------------------------------------------
double strNum(std::string_view s) {   // Number(string), for the forms a song can hold
  size_t a = 0, b = s.size(); while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\n' || s[a] == '\r')) a++; while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\n' || s[b - 1] == '\r')) b--;
  if (a == b) return 0;
  std::string t(s.substr(a, b - a));
  if (t == "Infinity" || t == "+Infinity") return kInf;
  if (t == "-Infinity") return -kInf;
  for (char ch : t) if (!((ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.' || ch == 'e' || ch == 'E')) return kNaN;
  double v; return parse_double(t, &v) ? v : kNaN;
}
double jnum(Json v) {   // +v
  switch (v.type()) { case JType::Number: return v.asNum(); case JType::Bool: return v.asBool() ? 1 : 0; case JType::Null: return 0; case JType::String: return strNum(v.asStr()); default: return kNaN; }
}
bool truthy(Json v) {
  switch (v.type()) { case JType::Missing: case JType::Null: return false; case JType::Bool: return v.asBool(); case JType::Number: { const double d = v.asNum(); return d == d && d != 0; } case JType::String: return !v.asStr().empty(); default: return true; }
}
inline bool nullish(Json v) { return v.missing() || v.isNull(); }
// int = (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d
double jint(Json v, double d) { if (v.isNull() || (v.isStr() && v.asStr().empty())) return d; const double x = jnum(v); return std::isfinite(x) ? js_round(x) : d; }
// an object's keys as `for in` / Object.keys give them when none looks like an array index: each key once, where it first stands, with its last value
template <class F> void eachKey(Json o, F f) {
  if (!o.isObj()) return;
  for (size_t i = 0; i < o.size(); i++) { const std::string_view k = o.keyAt(i); bool dup = false; for (size_t j = 0; j < i && !dup; j++) dup = o.keyAt(j) == k; if (!dup) f(k, o.get(k)); }
}
bool allDigits(std::string_view s) { if (s.empty()) return false; for (char ch : s) if (ch < '0' || ch > '9') return false; return true; }
std::string idKey(Json v) {   // a Map key (SameValueZero) as text
  if (v.isNum()) { char b[32]; js_number_to_string(v.asNum(), b); return std::string("n") + b; }
  if (v.isStr()) return "s" + std::string(v.asStr());
  return "o" + json_write(v);
}

enum Kind { K_VOL = 0, K_PAN, K_CUT, K_RES, K_DEC, K_TCH, K_ENV, K_ATK, K_SUS, K_REL, K_FX };
const char* const KIND_NAME[11] = {"vol", "pan", "cut", "res", "dec", "tch", "env", "atk", "sus", "rel", "fx"};
const int ENGK[7] = {K_CUT, K_RES, K_ENV, K_ATK, K_DEC, K_SUS, K_REL};   // ENGK = ['cut', 'res', 'env', 'atk', 'dec', 'sus', 'rel']
inline int engIdx(int kind) { for (int i = 0; i < 7; i++) if (ENGK[i] == kind) return i; return -1; }
inline int pnoIdx(int kind) { return kind == K_CUT ? 0 : kind == K_RES ? 1 : kind == K_DEC ? 2 : kind == K_TCH ? 3 : -1; }

// pumpNorm / pumpByte (a banded pump parameter's own byte)
double pumpNorm(const SeqPumpParam& p, double v) {
  if (!(p.max > p.min)) return 0;
  v = std::min(p.max, std::max(p.min, v));
  return p.scale == 1 ? std::log(v / p.min) / std::log(p.max / p.min) : p.scale == 2 ? js_pow((v - p.min) / (p.max - p.min), 1 / (p.skew > 0 ? p.skew : 1)) : (v - p.min) / (p.max - p.min);
}
double pumpByte(const SeqPumpParam& p, double v) {   // (coreTfx sends no `ch`, so never the 'ch' kind)
  const bool isBool = (p.flags & 4) != 0, isInt = (p.flags & 2) && p.max - p.min <= 255;
  return isBool ? (v >= 0.5 ? 1 : 0) : isInt ? js_round(v) : js_round(pumpNorm(p, v) * 255);
}

}  // namespace

const char* seq_ev_name(uint8_t type) {
  static const char* const N[SEV_COUNT] = {"beat", "tva", "tpn", "mout", "tvl", "fxp", "stp", "fxc", "chc", "mvol", "eng", "mpg", "mpb", "mat", "mcc", "off", "note", "mod", "tempo"};
  return type < SEV_COUNT ? N[type] : "?";
}

// ---- SeqVars ---------------------------------------------------------------------------------------------------------
void SeqVars::load(Json vars) {
  if (!vars.isObj()) return;
  if (!vars.get("vtrk").missing()) vtrk = truthy(vars.get("vtrk"));
  if (vars.get("NSLOT").isNum()) nslot = (int)vars.get("NSLOT").asNum();
  const Json T = vars.get("TFX"); if (!truthy(T) || !T.isObj()) return;
  tfx.clear();
  eachKey(T, [&](std::string_view k, Json d) {
    SeqTfxType ty; ty.type = std::string(k); ty.pump = truthy(d.get("pump"));
    const Json ps = d.arr("ps");
    for (size_t i = 0; i < ps.size(); i++) { const Json q = ps.at(i); SeqTfxParam P; P.k = std::string(q.get("k").asStr()); P.min = jnum(q.get("min")); P.max = jnum(q.get("max")); P.lfo = truthy(q.get("lfo"));
      if (q.get("pi").isNum()) { P.hasPi = true; P.pi = (int)q.get("pi").asNum(); }
      const Json pb = q.get("pb"); if (truthy(pb)) { P.hasPb = true; for (size_t j = 0; j < pb.size(); j++) P.pb.push_back(jnum(pb.at(j))); }
      ty.ps.push_back(std::move(P)); }
    const Json pd = d.get("pd");
    if (pd.isArr()) { ty.hasPd = true; for (size_t i = 0; i < pd.size(); i++) { const Json q = pd.at(i); SeqPumpParam P; P.s = std::string(q.get("s").asStr()); P.min = jnum(q.get("min")); P.max = jnum(q.get("max")); P.def = jnum(q.get("def"));
        P.skew = jnum(q.get("skew")); const std::string_view sc = q.get("scale").asStr(); P.scale = sc == "log" ? 1 : sc == "pow" ? 2 : 0; P.flags = q.get("flags").isNum() ? (int)q.get("flags").asNum() : 0; ty.pd.push_back(std::move(P)); } }
    tfx.push_back(std::move(ty));
  });
}
const SeqTfxType* SeqVars::type(std::string_view name) const { for (const SeqTfxType& t : tfx) if (t.type == name) return &t; return nullptr; }

// ---- a track's state (the JS's T) ------------------------------------------------------------------------------------
struct Seq::Tr {
  // what fresh() makes
  bool on = false, empty = false; int row = 0, cr = 0, step = 0, inst = 0; double rate = 1; int tsp = 0, next = -1;
  bool hasLast = false; int lastC = 0, lastV = 0;
  bool rnlHas[FXC_N]; int rnl[FXC_N]; int rnlNote = 0;
  int arcMode = 0, arcSpd = 1; double rel = 0; int g = 0; bool hasGrv = false; int grv[2] = {0, 0}; int dir = 0, pp = 1, cnt = 0; double r0 = 0; int wr = 0;
  int tch = -1 /* -1: null */, skp = 0, chd = 0; bool hasSeed = false; uint32_t seed = 0; int vn = -1;
  // what the commands and the sequencer add to T later (fresh() does not list these, so Object.assign(T, fresh()) keeps them)
  int cres = 0 /* 0 undefined, 1 true, 2 false */; int lk[4] = {-1, -1, -1, -1}; bool hasLk = false;
  bool hasFxs = false; int fxsS = 0, fxsP = 0; bool hasScl = false; int sclScale = 0, sclRoot = 0;
  int cut = -1, dec = -1, cAtk = -1, cRel = -1 /* -1: null / undefined */, res = 0; bool rsg = false;
  int eng = 0 /* 0 none yet, 1 '' (unknown), 2 engC / engO */; double engC[5], engO[7]; bool mut = false;
};
struct Seq::Fx {
  bool has[FXC_N]; int v[FXC_N];
  Fx() { std::memset(has, 0, sizeof has); std::memset(v, 0, sizeof v); }
  bool H(int c) const { return has[c]; }                 // fx.X != null
  bool T(int c) const { return has[c] && v[c] != 0; }    // fx.X (truthy)
  int operator[](int c) const { return v[c]; }
  int or0(int c) const { return has[c] ? v[c] : 0; }     // fx.X || 0
};
struct Seq::Ctx { SeqEvent* evs; int cap; int n; uint32_t dropped; SeqPos* pos; SeqEvent scratch; };

void Seq::fresh(Tr& T) {
  T.on = false; T.empty = false; T.row = 0; T.cr = 0; T.step = 0; T.inst = 0; T.rate = 1; T.tsp = 0; T.next = -1; T.hasLast = false; T.lastC = 0; T.lastV = 0;
  std::memset(T.rnlHas, 0, sizeof T.rnlHas); T.rnlNote = 0; T.arcMode = 0; T.arcSpd = 1; T.rel = 0; T.g = 0; T.hasGrv = false; T.dir = 0; T.pp = 1; T.cnt = 0; T.r0 = 0; T.wr = 0;
  T.tch = -1; T.skp = 0; T.chd = 0; T.hasSeed = false; T.seed = 0; T.vn = -1;
  trGen_[&T - tr_.get()]++;   // nth: new Map()
}

// ---- construction ----------------------------------------------------------------------------------------------------
void Seq::parseId(TgtId& g) const {   // tgtParse and tgtOk
  std::vector<std::string_view> a; { std::string_view s = g.s; size_t p0 = 0; for (;;) { const size_t q = s.find(':', p0); if (q == std::string_view::npos) { a.push_back(s.substr(p0)); break; } a.push_back(s.substr(p0, q - p0)); p0 = q + 1; } }
  g.kind = -1; for (int k = 0; k < 11; k++) if (a[0] == KIND_NAME[k]) g.kind = k;
  g.t = a.size() > 1 ? strNum(a[1]) : kNaN; g.sl = a.size() > 2 ? strNum(a[2]) : -1; g.p = a.size() > 3 ? strNum(a[3]) : -1; g.hasB = a.size() > 4; g.b = g.hasB ? strNum(a[4]) : 0;
  g.ok = g.kind >= 0 && (g.kind == K_FX ? (a.size() == 4 || a.size() == 5) : a.size() == 2); for (size_t i = 1; g.ok && i < a.size(); i++) g.ok = allDigits(a[i]);
  g.tch4 = g.s.compare(0, 4, "tch:") == 0;
}
double Seq::tgtOwnOf(const TgtId& g, bool* fxOk) const {   // tgtOwn (and tgtFx: *fxOk)
  *fxOk = false;
  if (g.kind == K_VOL) return g.t < NT ? S_.mix[(int)g.t].vol : S_.mout;
  if (g.kind == K_PAN) return g.t < NT ? S_.mix[(int)g.t].pan : 128;
  if (g.kind == K_FX) {
    const Json A = g.t >= MT ? S_.mfx : S_.tfx.at((size_t)g.t), f = A.isArr() && g.sl < 4294967295.0 ? A.at((size_t)g.sl) : Json();   // (g.t < MT here: a small index)
    if (!truthy(f)) return 0;
    const SeqTfxType* ty = f.get("type").isStr() ? V_.type(f.get("type").asStr()) : nullptr; if (!ty) return 0;
    if (!(g.p < (double)ty->ps.size())) return 0;
    const SeqTfxParam& q = ty->ps[(size_t)g.p];
    if ((ty->pump && !q.lfo) || (q.hasPb != g.hasB) || (q.hasPb && !(g.b >= 0 && g.b < (double)q.pb.size()))) return 0;
    *fxOk = true;
    if (q.hasPb) { const double pi = q.pb[(size_t)g.b]; if (!(pi >= 0 && pi < (double)ty->pd.size())) return 0; const SeqPumpParam& p = ty->pd[(size_t)pi];
      const Json pv = truthy(f.get("pump")) ? f.get("pump").get(p.s) : Json(); return cd(pumpByte(p, pv.missing() ? p.def : jnum(pv)), 0, 255); }
    const double span = q.max - q.min;
    return cd(js_round((jnum(f.get(q.k)) - q.min) / (span != 0 && span == span ? span : 1) * 255), 0, 255);
  }
  return g.kind == K_CUT || g.kind == K_DEC || g.kind == K_TCH ? 255 : 0;
}
// the ids registry (session.h): ids_ is the session's list in its order, so an index means the same in every Seq of the PLAY
void Seq::loadIds() {
  const std::vector<std::string>& v = sess_->ids(); ids_.reserve(v.size() + 16);
  for (const std::string& s : v) { TgtId g; g.s = s; parseId(g); if (g.ok) g.own = tgtOwnOf(g, &g.fxOk); ids_.push_back(std::move(g)); }
}
int Seq::internStr(std::string_view s, TgtId&& g) {   // the index of s; a new id is appended to the registry and to ids_; -1: the registry is full (counted)
  const int k = sess_->findId(s); if (k >= 0) return k;
  const int n = sess_->addId(s); if (n < 0) return -1;
  ids_.push_back(std::move(g)); return n;
}
int Seq::intern(Json id) {
  if (!id.isStr()) return -1;
  const std::string_view s = id.asStr();
  const int k = sess_->findId(s); if (k >= 0) return k;
  TgtId g; g.s = std::string(s); parseId(g); if (g.ok) g.own = tgtOwnOf(g, &g.fxOk);
  return internStr(s, std::move(g));
}
void Seq::buildPerf() {   // perfOf(S): S.perf, or freshPerf() (no scenes' values, eight macros at 0, no locks, no LFOs)
  loadIds();
  const Json P = S_.perf;
  for (int t = 0; t < kSeqTracks; t++) { lfoIsArr_[t] = false; for (int n = 0; n < 4; n++) lk_[t][n] = -1; }
  double xf = 0;
  if (truthy(P)) {
    xf = jnum(P.get("xf"));
    const Json sc = P.get("sc"), ja = sc.at(to_size_sat(jnum(P.get("xa")), 1u << 30)), jb = sc.at(to_size_sat(jnum(P.get("xb")), 1u << 30));
    const Json A = truthy(ja) ? ja.get("v") : Json(), B = truthy(jb) ? jb.get("v") : Json();
    const auto add = [&](std::string_view k, Json) { for (const SceneEnt& e : scene_) if (ids_[(size_t)e.id].s == k) return;
      TgtId g; g.s = std::string(k); parseId(g); if (!g.ok) return;
      int id = sess_->findId(k);
      if (id < 0) { g.own = tgtOwnOf(g, &g.fxOk); id = internStr(k, std::move(g)); if (id < 0) return; }
      const Json va = A.get(k), vb = B.get(k); scene_.push_back({id, !va.missing(), !vb.missing(), jnum(va), jnum(vb)}); };
    eachKey(A, add); eachKey(B, add);
    const Json mac = P.get("mac");
    for (size_t i = 0; i < mac.size(); i++) { const Json m = mac.at(i); Macro M; M.hasV = !nullish(m.get("v")); M.v = M.hasV ? jnum(m.get("v")) : kNaN;
      const Json tg = m.get("tg"); for (size_t j = 0; j < tg.size(); j++) { const Json g = tg.at(j); const int id = intern(g.get("id")); if (id < 0 || !ids_[(size_t)id].ok) continue; M.tg.push_back({id, jnum(g.get("lo")), jnum(g.get("hi"))}); }
      macros_.push_back(std::move(M)); }
    const Json lk = P.get("lk"), lfo = P.get("lfo");
    for (int t = 0; t < kSeqTracks; t++) { char key[8]; std::snprintf(key, sizeof key, "%d", t);
      const Json l = lk.get(key); if (truthy(l)) for (size_t n = 0; n < 4; n++) { const Json id = l.isArr() ? l.at(n) : Json(); if (truthy(id)) lk_[t][n] = intern(id); }
      const Json La = lfo.get(key); if (La.isArr()) { lfoIsArr_[t] = true;
        for (size_t i = 0; i < La.size(); i++) { const Json L = La.at(i); Lfo x; x.present = truthy(L); x.id = x.present ? intern(L.get("id")) : -1; x.ok = x.id >= 0 && ids_[(size_t)x.id].ok;
          x.sh = L.get("sh").isNum() ? L.get("sh").asNum() : kNaN; x.rt = L.get("rt").isNum() ? L.get("rt").asNum() : kNaN; x.dp = jnum(L.get("dp")); lfo_[t].push_back(x); } } }
  } else { for (int i = 0; i < PERF_MACROS; i++) { Macro M; M.v = 0; M.hasV = true; macros_.push_back(std::move(M)); } }
  pfXf_ = xf; pfMac_.assign(std::max<size_t>((size_t)PERF_MACROS, macros_.size()), 0); pfMacSet_.assign(pfMac_.size(), 0);
  for (int k = 0; k < 7; k++) for (int u = 0; u < NT; u++) { char key[16]; std::snprintf(key, sizeof key, "%s:%d", KIND_NAME[ENGK[k]], u); canon_[k][u] = -1; for (size_t i = 0; i < ids_.size(); i++) if (ids_[i].s == key) canon_[k][u] = (int)i; }
  const size_t N = ids_.size();
  sentHas_.assign(N, 0); wantHas_.assign(N, 0); sentVal_.assign(N, 0); wantVal_.assign(N, 0); sentOrd_.assign(N, 0); wantOrd_.assign(N, 0);
  for (int t = 0; t < kSeqTracks; t++) for (int j = 0; j < 4; j++) { pnoHas_[t][j] = false; pno_[t][j] = 0; }
}
// partEvs's effect half for one snapshot: `fx` is {"t:s": {type, k: value...}} (own: partSnap()'s, built from the song's effects)
void Seq::partEvsOf(Json mix, Json fx, bool own, std::vector<PEv>& out) const {
  if (own) { for (int t = 0; t < NT; t++) { out.push_back({SEV_TVA, t, 0, 0, cd(js_round((double)S_.mix[t].vol), 0, 255)}); out.push_back({SEV_TPN, t, 0, 0, cd(js_round((double)S_.mix[t].pan), 0, 255)}); } }
  else for (size_t t = 0; t < mix.size() && t < (size_t)NT; t++) { const Json m = mix.at(t); out.push_back({SEV_TVA, (int)t, 0, 0, cd(jint(m.at(0), 224), 0, 255)}); out.push_back({SEV_TPN, (int)t, 0, 0, cd(jint(m.at(1), 128), 0, 255)}); }
  const auto slotFx = [&](double t, double s, Json o) {
    const Json A = t >= MT ? S_.mfx : S_.tfx.at((size_t)t), f = A.isArr() && s >= 0 ? A.at((size_t)s) : Json(); if (!truthy(f) || !f.get("type").isStr()) return;
    if (!own && !(o.get("type").isStr() && o.get("type").asStr() == f.get("type").asStr())) return;
    const SeqTfxType* ty = V_.type(f.get("type").asStr()); if (!ty || (own && ty->pump)) return;
    for (size_t pi = 0; pi < ty->ps.size(); pi++) { const SeqTfxParam& q = ty->ps[pi]; const Json v = own ? f.get(q.k) : o.get(q.k); if (nullish(v)) continue; const double span = q.max - q.min;
      out.push_back({SEV_FXP, (int)t, (int)s, (int)pi, cd(js_round((jnum(v) - q.min) / (span != 0 && span == span ? span : 1) * 255), 0, 255)}); } };
  if (own) { for (int t = 0; t <= NT; t++) { const Json A = t >= NT ? S_.mfx : S_.tfx.at((size_t)t); for (size_t s = 0; s < A.size(); s++) slotFx(t, (double)s, Json()); } }
  else eachKey(fx, [&](std::string_view key, Json o) { const size_t c = key.find(':'); if (c == std::string_view::npos) return; const size_t c2 = key.find(':', c + 1);
    const double t = strNum(key.substr(0, c)), s = strNum(key.substr(c + 1, c2 == std::string_view::npos ? std::string_view::npos : c2 - c - 1)); if (!(t >= 0) || !(s >= 0) || t != std::floor(t) || s != std::floor(s) || t > 1e6 || s > 1e6) return; slotFx(t, s, o); });
}
void Seq::buildParts() {
  sInsts_.resize(NI);
  for (int i = 0; i < NI; i++) { const SongInst& I = S_.insts[i]; PInst& p = sInsts_[(size_t)i]; p.present = !S_.instMissing[i]; p.pad = I.pad; p.tsp = I.tsp; p.vol = I.vol; p.pan = I.pan; p.ofs = I.ofs; p.hut = I.hut; p.huv = I.huv; p.kf = I.kf != 0; p.rev = I.rev != 0; }
  partEvsOf(Json(), Json(), true, own_.evs);
  const Json P = S_.parts;
  for (size_t k = 0; k < P.size(); k++) { Part part; const Json p = P.isArr() ? P.at(k) : Json(); part.present = truthy(p);
    if (part.present) { partEvsOf(p.get("mix"), p.get("fx"), false, part.evs); const Json in = p.get("insts");
      for (size_t i = 0; i < in.size(); i++) { const Json I = in.at(i); PInst x; x.present = truthy(I); if (x.present) { x.pad = I.get("pad").missing() ? kNaN : jnum(I.get("pad")); x.tsp = I.get("tsp").missing() ? kNaN : jnum(I.get("tsp"));
          x.vol = I.get("vol").missing() ? kNaN : jnum(I.get("vol")); x.pan = I.get("pan").missing() ? kNaN : jnum(I.get("pan")); x.ofs = I.get("ofs").missing() ? kNaN : jnum(I.get("ofs"));
          x.hut = js_to_int32(jnum(I.get("hut"))); x.huv = js_to_int32(jnum(I.get("huv"))); x.kf = truthy(I.get("kf")); x.rev = truthy(I.get("rev")); }
        part.insts.push_back(x); } }
    parts_.push_back(std::move(part)); }
}
// busPlan(): which tracks are buses (feeds' keys) and what each track feeds (targets), then makeSeq's busT and hearT
void Seq::buildBus() {
  for (int t = 0; t < NT; t++) busT_[t] = false;
  std::map<int, std::vector<int>> targets; std::set<int> feeds;
  const Json d = S_.chainPatch;
  if (truthy(d) && d.get("format").isStr() && d.get("format").asStr() == "chains.patch" && d.get("modules").isArr()) {
    const Json mods = d.get("modules"), cables = d.get("cables");
    std::vector<std::string> order; std::map<std::string, Json> by;
    for (size_t i = 0; i < mods.size(); i++) { const Json m = mods.at(i); if (!truthy(m) || nullish(m.get("id"))) continue; const std::string k = idKey(m.get("id")); if (!by.count(k)) order.push_back(k); by[k] = m; }
    std::map<std::string, std::vector<std::string>> nx;
    for (size_t i = 0; i < cables.size(); i++) { const Json c = cables.at(i); if (!truthy(c) || !truthy(c.get("from")) || !truthy(c.get("to"))) continue; nx[idKey(c.get("from").at(0))].push_back(idKey(c.get("to").at(0))); }
    const auto trk = [](Json m) { const double k = jint(truthy(m.get("params")) ? m.get("params").get("track") : Json(), 1); return k >= 1 && k <= NT ? (int)k - 1 : -1; };
    std::set<int> chainsTracks; for (size_t i = 0; i < mods.size(); i++) { const Json m = mods.at(i); if (truthy(m) && m.get("type").isStr() && m.get("type").asStr() == "chainOut") { const int k = trk(m); if (k >= 0) chainsTracks.insert(k); } }
    const auto isType = [](Json m, const char* ty) { return m.get("type").isStr() && m.get("type").asStr() == ty; };
    std::set<int> ownIn; for (const std::string& k : order) if (isType(by[k], "trackIn")) { const int t = trk(by[k]); if (t >= 0) ownIn.insert(t); }
    std::map<int, std::vector<int>> adj;
    const auto reaches = [&](int a, int b) { std::set<int> seen{a}; std::vector<int> w{a}; while (!w.empty()) { const int x = w.back(); w.pop_back(); if (x == b) return true; for (int y : adj[x]) if (!seen.count(y)) { seen.insert(y); w.push_back(y); } } return false; };
    for (const std::string& k : order) { const Json m = by[k];
      if (!isType(m, "trackIn")) continue;
      const int t = trk(m); if (t < 0 || !S_.chainOn[t]) continue;
      std::set<std::string> seen{k}; std::vector<std::string> w{k}; std::set<int> outs;
      while (!w.empty()) { const std::string id = w.back(); w.pop_back(); const auto it = by.find(id); if (it != by.end() && isType(it->second, "chainOut")) { const int o = trk(it->second); if (o >= 0) outs.insert(o); }
        const auto nit = nx.find(id); if (nit != nx.end()) for (const std::string& y : nit->second) if (!seen.count(y)) { seen.insert(y); w.push_back(y); } }
      for (int b : outs) {   // (a std::set: ascending, as the JS sorts them)
        if (b == t || ownIn.count(b) || !(S_.chainOn[b] && chainsTracks.count(b))) continue;
        if (reaches(b, t)) continue;   // (P.loop)
        adj[t].push_back(b); feeds.insert(b); targets[t].push_back(b);
      }
    }
  }
  for (int b : feeds) busT_[b] = true;
  hasHear_ = false; for (int t = 0; t < kSeqTracks; t++) hearT_[t] = false;
  if (mode_ != SeqMode::Song || o_.hasOnly) { hasHear_ = true; std::vector<int> w;
    if (mode_ == SeqMode::Song) { for (int t = 0; t < kSeqTracks; t++) if ((o_.only >> t) & 1) { hearT_[t] = true; w.push_back(t); } } else if (o_.track >= 0 && o_.track < kSeqTracks) { hearT_[o_.track] = true; w.push_back(o_.track); }
    while (!w.empty()) { const int x = w.back(); w.pop_back(); for (int b : targets[x]) if (!hearT_[b]) { hearT_[b] = true; w.push_back(b); } } }
}
void Seq::buildCounters() {
  SeqSession& R = *sess_;
  // rvs: a page of counters for every place a chance roll can happen: this Seq takes the session's table, and the session gives a page to every cell that has a chain now
  pageN_ = R.pageTableSize(); pages_.reset(new uint32_t*[pageN_]); R.copyPages(pages_.get());
  if (mode_ == SeqMode::Song) for (int y = 0; y < kSongRows; y++) for (int t = 0; t < kSeqTracks; t++) if (S_.cell(y, t) >= 0) { uint32_t*& pg = pages_[(size_t)y * kSeqTracks + (size_t)t]; if (!pg) pg = R.ensurePage((size_t)y * kSeqTracks + (size_t)t); }
  rvN_ = 0; for (size_t i = 0; i < pageN_; i++) if (pages_[i]) rvN_ += kSeqPageU32;
  // nth / c1: a slot, from the session, for every phrase row with NTH, CND or REP (REP can repeat either); the counters are in the session's chunks
  rowSlot_.assign(S_.phraseData.size() / RW, -1);
  for (int pid = 0; pid < kNPH; pid++) { const int32_t off = S_.phraseOff[(size_t)pid]; if (off < 0) continue;
    for (int s = 0; s < PROWS; s++) { const size_t i = (size_t)off / RW + (size_t)s; const int16_t* r = S_.phraseData.data() + i * RW;
      for (int k = FX0; k < RW; k += 2) if (r[k] == NTH || r[k] == CND || r[k] == REP) { rowSlot_[i] = R.slotFor((uint32_t)(pid * PROWS + s)); break; } } }
  nthK_ = R.nSlots_;
  chunks_.reset(new SeqCountChunk*[kSeqMaxChunks]); R.copyChunks(chunks_.get());
  for (int t = 0; t < kSeqTracks; t++) trGen_[t] = 1;
  arpPool_.reset(new double[(size_t)kSeqTracks * kSeqArpMax * 2]());
}
uint32_t* Seq::nthSlot(int t, int pid, int step, bool c1) {
  const int32_t off = pid >= 0 && pid < kNPH ? S_.phraseOff[(size_t)pid] : -1; const int slot = off >= 0 && step >= 0 && step < PROWS ? rowSlot_[(size_t)off / RW + (size_t)step] : -1;
  SeqCountChunk* ch = slot >= 0 ? chunks_[(size_t)slot >> 8] : nullptr;
  if (!ch) { dropped_++; return nullptr; }
  const size_t i = (size_t)t * kSeqChunkSlots + ((size_t)slot & (kSeqChunkSlots - 1));
  if (c1) return &ch->c1[i];
  if (ch->gen[i] != trGen_[t]) { ch->gen[i] = trGen_[t]; ch->nth[i] = 0; }
  return &ch->nth[i];
}
// chaseChance(row) for every row, once: the song is immutable, so the seeds and track chances "the rows above would have left"
// are a table (a JMP on the audio thread then costs a copy, not a walk through the song).
void Seq::buildChase() {
  if (mode_ != SeqMode::Song) return;
  chase_.assign((size_t)(kSongRows + 1) * kSeqTracks, Chase{0, -1, 0, 0, 0}); chaseHasS_.assign(kSongRows + 1, 0); chaseS_.assign(kSongRows + 1, 0); chaseM_.assign(kSongRows + 1, 255);
  Chase cur[kSeqTracks]; for (Chase& c : cur) c = Chase{0, -1, 0, 0, 0}; bool hasS = false; uint32_t ss = 0; int mt = 255;
  for (int y = 0; y < kSongRows; y++) {
    for (int t = 0; t <= MT; t++) { const int16_t* ch = S_.chain(S_.cell(y, t)); if (!ch || (t == MT && mutedAt(MT, y))) continue;
      for (int cr = 0; cr < kChainRows; cr++) { const int pid = ch[cr * 2]; const int16_t* ph = S_.phrase(pid); if (!ph) continue; const int L = std::min(PROWS, lenOf(pid));
        for (int i = 0; i < L; i++) { const int16_t* r = ph + i * RW;
          for (int k = FX0; k < RW; k += 2) { const int c = r[k]; if (c != SED && c != TCH && c != SKP && c != CHD) continue; const int v = std::max(0, (int)r[k + 1]);
            if (c == SED) { if (t == MT) { hasS = v != 0; ss = sed_seed((uint32_t)v); } else { cur[t].hasSeed = v != 0; cur[t].seed = sed_seed((uint32_t)v); } }
            else if (c == TCH) { if (t == MT) mt = v; else cur[t].tch = (int16_t)(v >= 255 ? -1 : v); }
            else if (c == SKP) cur[t].skp = v ? 1 : 0;
            else cur[t].chd = v ? 1 : 0; } } } }
    for (int t = 0; t < kSeqTracks; t++) chase_[(size_t)(y + 1) * kSeqTracks + (size_t)t] = cur[t];
    chaseHasS_[(size_t)y + 1] = hasS; chaseS_[(size_t)y + 1] = ss; chaseM_[(size_t)y + 1] = (int16_t)mt;
  }
}
void Seq::chaseChance(int row) {
  const size_t y = (size_t)ci(row, 0, kSongRows);
  hasSseed_ = chaseHasS_[y] != 0; sseed_ = chaseS_[y]; mtch_ = chaseM_[y];
  for (int t = 0; t < kSeqTracks; t++) { const Chase& c = chase_[y * kSeqTracks + (size_t)t]; Tr& T = tr_[t]; T.hasSeed = c.hasSeed != 0; T.seed = c.seed; T.tch = c.tch; T.skp = c.skp; T.chd = c.chd; }
}

Seq::Seq(const Song& song, const SeqVars& vars, SeqMode mode, const SeqOpts& opts) : Seq(SeqSession::create(mode, opts), song, vars) {}
Seq::Seq(std::shared_ptr<SeqSession> own, const Song& song, const SeqVars& vars) : Seq(*own, song, vars, false) { ownSess_ = std::move(own); ownSess_->commit(); }
Seq::Seq(SeqSession& session, const Song& song, const SeqVars& vars, bool successor) : S_(song), V_(vars), mode_(session.mode()), o_(session.opts()), sess_(&session), successor_(successor), mrand_(session.opts().randomSeed) {
  once_ = o_.once; setLoop(o_.hasLoop, o_.loopY0, o_.loopY1);
  ntr_ = S_.ntr ? S_.ntr : 16;
  polyFrom_ = S_.nmono >= 1 ? std::min(S_.nmono, ntr_ - 1) : (ntr_ + 1) / 2;   // polyFrom(): Math.ceil(n / 2)
  NIs_ = ci(S_.ni ? S_.ni : 16, 16, NI);
  flxOld_ = truthy(S_.S.get("flxOld"));
  { const Json M = S_.S.get("smuteM"); hasSmuteM_ = M.isArr(); smuteM_.assign(kSongRows, 0); if (hasSmuteM_) for (size_t i = 0; i < M.size(); i++) { const Json v = M.at(i); if (v.isNum()) { const double y = v.asNum(); if (y >= 0 && y < kSongRows && y == std::floor(y)) smuteM_[(size_t)y] = 1; } } }
  for (int t = 0; t < kSeqTracks; t++) for (int k = 0; k < FXSLOTS; k++) { const Json A = t >= MT ? S_.mfx : S_.tfx.at((size_t)t), f = A.at((size_t)k); const std::string_view ty = truthy(f) ? f.get("type").asStr() : std::string_view();
    need_[t][k] = ty == "flex" ? 1 : ty == "sgate" ? 2 : ty == "retrig" ? 3 : 0; }
  tr_.reset(new Tr[kSeqTracks]);
  buildCounters();
  for (int t = 0; t < kSeqTracks; t++) fresh(tr_[t]);
  buildBus(); buildPerf(); buildParts(); buildChase();
  bpm_ = S_.bpm; tks_ = ci(S_.ticks ? S_.ticks : TICKS, 1, 32);
  PL_ = o_.live && !o_.sim;
  Tr* tr = tr_.get();
  wakeOk_ = mode_ == SeqMode::Song && (once_ || o_.live);
  if (successor_) return;   // a successor's run state comes from takeOver()
  if (mode_ == SeqMode::Song) {
    for (int t = 0; t <= MT; t++) { Tr& T = tr[t]; T.row = hasLoop_ ? ci(o_.row, loopY0_, loopY1_) : o_.row;
      if ((t < ntr_ || t == MT) && songCell(T.row, t) >= 0 && (!o_.hasOnly || ((o_.only >> t) & 1))) { T.on = true; settle(T, t); } }
  } else if (o_.track >= 0 && o_.track < kSeqTracks) { Tr& T = tr[o_.track]; T.on = true; if (hasLoop_) { if (mode_ == SeqMode::Sect) T.cr = loopY0_; else T.step = loopY0_; } settle(T, o_.track); }
  // EACH TRACK WHERE IT WAS (o.at / o.atRef)
  if (mode_ == SeqMode::Song && o_.hasAt && !hasLoop_ && !o_.hasOnly && o_.atRef >= 0 && o_.atRef < kSeqTracks) { const SeqAt& R = o_.at[o_.atRef];
    if (R.valid && R.row == o_.row) for (int t = 0; t <= MT; t++) { const SeqAt& q = o_.at[t]; Tr& T = tr[t];
      if (!q.valid || t == o_.atRef || !(q.row >= 0 && q.row < 256) || q.row == R.row || !(t < ntr_ || t == MT)) continue;
      fresh(T); T.row = q.row; const int cid = songCell(q.row, t); const int16_t* c = S_.chain(cid); if (!c) continue;
      int cr = 0; if (q.cr >= 0 && q.cr < kChainRows && c[q.cr * 2] == q.phrase && q.phrase >= 0) cr = q.cr; else for (int k = 0; k < kChainRows; k++) if (c[k * 2] == q.phrase && q.phrase >= 0) { cr = k; break; }
      const int pid = c[cr * 2]; const double line = 60 / bpm_ / plpbOf(pid), lineR = 60 / bpm_ / plpbOf(R.phrase), dt = q.step * line - R.step * lineR, d = js_round(dt / line);
      T.on = true; T.cr = cr;
      if (dt >= -1e-9 && d < lenOf(pid)) { T.step = T.g = T.cnt = (int)d; T.r0 = -d * line; } else if (dt < 0) T.rel = T.r0 = -dt;
      settle(T, t); } }
  if (mode_ == SeqMode::Song && o_.row > 0) chaseChance(o_.row);
}
Seq::~Seq() = default;

// takeOver: see seq.h. Audio thread: plain copies into storage this Seq sized at its construction; no allocation, no lock, no loop longer than the
// tables (33 tracks, 4 x 33 PERFORM numbers, the sent arrays: one entry per target id of the old Seq, at most kSeqMaxIds).
bool Seq::takeOver(const Seq& old) {
  if (&old == this || old.sess_ != sess_ || old.ids_.size() > ids_.size()) return false;
  for (int t = 0; t < kSeqTracks; t++) tr_[t] = old.tr_[t];
  time_ = old.time_; base_ = old.base_; bt_ = old.bt_; btT_ = old.btT_; hasBase_ = old.hasBase_; hasBtT_ = old.hasBtT_;
  bpm_ = old.bpm_; tks_ = old.tks_; hasGrv_ = old.hasGrv_; grv_[0] = old.grv_[0]; grv_[1] = old.grv_[1]; hasLpb_ = old.hasLpb_; lpb_ = old.lpb_;
  mrand_ = old.mrand_; seeded_ = old.seeded_; isSeeded_ = old.isSeeded_; hasSseed_ = old.hasSseed_; sseed_ = old.sseed_; hasPseed_ = old.hasPseed_; pseed_ = old.pseed_; mtch_ = old.mtch_;
  hasGscl_ = old.hasGscl_; gsclScale_ = old.gsclScale_; gsclRoot_ = old.gsclRoot_;
  hasLoop_ = old.hasLoop_; loopY0_ = old.loopY0_; loopY1_ = old.loopY1_;
  pfXf_ = old.pfXf_; pfFill_ = old.pfFill_; pfPart_ = old.pfPart_; perfFirst_ = old.perfFirst_;
  std::memcpy(pnoHas_, old.pnoHas_, sizeof pnoHas_); std::memcpy(pno_, old.pno_, sizeof pno_);
  std::memcpy(trGen_, old.trGen_, sizeof trGen_);
  badSets_ = old.badSets_; dropped_ = old.dropped_; maxEvs_ = old.maxEvs_; maxArp_ = old.maxArp_;
  // what the JS computes once, at PLAY (ruling 1): the bus plan, the idle-LFO set, the INS clamp stay the PLAY's
  std::memcpy(busT_, old.busT_, sizeof busT_); std::memcpy(hearT_, old.hearT_, sizeof hearT_); hasHear_ = old.hasHear_; NIs_ = old.NIs_;
  // the macros' resting values: the JS copied them at make (PF.mac), so an edit of `v` is not heard; the effective value in force is carried
  { const size_t nm = std::min(old.pfMac_.size(), pfMac_.size());
    for (size_t i = 0; i < nm; i++) {
      if (i < old.macros_.size()) { pfMac_[i] = old.pfMacSet_[i] ? old.pfMac_[i] : old.macros_[i].v; pfMacSet_[i] = 1; }
      else { pfMac_[i] = old.pfMac_[i]; pfMacSet_[i] = old.pfMacSet_[i]; } } }
  // the targets sent so far: an id means the same in both (the registry), and the ORDER is the Map's insertion order
  const size_t n = old.ids_.size();
  if (n) { std::memcpy(sentHas_.data(), old.sentHas_.data(), n); std::memcpy(sentVal_.data(), old.sentVal_.data(), n * sizeof(double)); std::memcpy(sentOrd_.data(), old.sentOrd_.data(), n * sizeof(int)); }   // (memcpy with a null pointer is undefined, even for 0 bytes)
  for (size_t i = n; i < sentHas_.size(); i++) sentHas_[i] = 0;
  sentN_ = old.sentN_; wantN_ = 0;
  return true;
}

bool Seq::alive() const { for (int t = 0; t < kSeqTracks; t++) if (tr_[t].on) return true; return false; }

// ---- makeSeq's helpers -----------------------------------------------------------------------------------------------
int Seq::sectId(const Tr& T, int t) const { return mode_ == SeqMode::Song ? (T.row < 256 ? songCell(T.row, t) : -1) : o_.sect; }
int Seq::lenOf(int id) const { if (!(id >= 0 && id < kNPH)) return 16; const int v = S_.plen[(size_t)id]; return ci(v ? v : 16, 1, PROWS); }
int Seq::emptyLen() const { return ci(S_.dlen ? S_.dlen : 16, 1, PROWS); }
int Seq::plpbOf(int id) const {   // api.lpb || plpbOf(S, id)
  if (hasLpb_ && lpb_) return lpb_;
  if (id >= 0 && id < kNPH && S_.plpb[(size_t)id]) return S_.plpb[(size_t)id];
  return S_.lpb ? S_.lpb : 4;
}
int Seq::offInSong(const Tr& T, int t) const { int k = 0; const int cid = sectId(T, t); for (int cr = T.cr - 1; cr >= 0; cr--) { const int p = chainPhrase(cid, cr); if (p >= 0) k += lenOf(p); } return k + T.step; }
bool Seq::mutedAt(int t, int y) const {
  if (mode_ != SeqMode::Song) return false;
  if (t < MT) return y >= 0 && y < 256 && t >= 0 ? ((S_.smute[(size_t)y] >> t) & 1) != 0 : false;
  return hasSmuteM_ && y >= 0 && y < kSongRows && smuteM_[(size_t)y];
}
const int16_t* Seq::rowOf(const Tr& T, int t, int pid) const {   // r0(T, pid)
  const int16_t* ph = S_.phrase(pid); if (!ph) return nullptr;
  if (t == MT && mutedAt(MT, T.row)) return nullptr;
  return T.step >= 0 && T.step < PROWS ? ph + T.step * RW : nullptr;
}
void Seq::settle(Tr& T, int t) {
  T.empty = false;
  if (mode_ == SeqMode::Phrase) return;
  const bool song = mode_ == SeqMode::Song;
  for (int n = 0; n < 4200; n++) {
    if (song && (sectId(T, t) < 0 || (hasLoop_ && (T.row > loopY1_ || T.row < loopY0_)))) {
      if (once_) { T.on = false; T.wr = std::min(T.row, 256) + 1; return; }
      if (hasLoop_) {
        if (T.row > loopY1_ || T.row < loopY0_) { T.row = loopY0_; T.cr = 0; }
        int f = T.row;
        while (songCell(f, t) < 0) { f++; if (f > loopY1_) f = loopY0_; if (f == T.row) { T.on = false; return; } }
        T.row = f; T.cr = 0;
      } else { T.on = false; T.wr = std::min(T.row, 256) + 1; return; }
    }
    const int cid = sectId(T, t); int ph = -1, ctsp = 0; const bool cRow = cid >= 0 && chainRow(cid, T.cr, &ph, &ctsp);
    if (cRow && ph >= 0) return;
    if (song && cid >= 0 && T.cr == 0) return;
    if (mode_ == SeqMode::Sect) {
      if (hasLoop_) { T.cr = loopY0_; continue; }
      if (T.cr > 0) { T.cr = 0; continue; }
      T.empty = true; return;
    }
    T.row++; T.cr = 0;
    if (hasLoop_ && T.row > loopY1_) T.row = loopY0_;
  }
  T.on = false;
}
void Seq::wake(double rel) {
  Tr* tr = tr_.get(); const Tr* L = nullptr; for (int t = 0; t <= MT; t++) if (tr[t].on && !tr[t].empty) { L = &tr[t]; break; }
  if (!L) return;
  const int y = L->row; if (y < 0 || y > 255) return;
  for (int t = 0; t <= MT; t++) {
    Tr& T = tr[t]; if (T.on || !(t < ntr_ || t == MT) || (o_.hasOnly && !((o_.only >> t) & 1))) continue;
    const int p = chainPhrase(songCell(y, t), 0);
    if (p < 0 || (hasLoop_ && (y < loopY0_ || y > loopY1_))) continue;
    if (!hasLoop_ && T.wr && y < T.wr) continue;
    const double line = 60 / bpm_ / plpbOf(p), st = std::max(0.0, std::ceil((rel - L->r0) / line - 1e-6));
    if (st >= lenOf(p)) continue;
    const bool hs = T.hasSeed; const uint32_t sd = T.seed; const int tch = T.tch, skp = T.skp, chd = T.chd;
    fresh(T); T.on = true; T.row = y; T.cr = 0; T.step = T.g = T.cnt = (int)st; T.r0 = L->r0; T.rel = L->r0 + st * line; T.hasSeed = hs; T.seed = sd; T.tch = tch; T.skp = skp; T.chd = chd;
  }
}
double Seq::rnd() { return isSeeded_ ? seeded_.next() : mrand_.next(); }   // rand()
int Seq::rint(int dn, int up) { return (int)std::floor(rnd() * (dn + up + 1)) - dn; }
double Seq::roll(Tr& T, int t, int salt) {
  const int key = (mode_ == SeqMode::Song ? T.row * 65536 : 0) + T.cr * 256 + T.step;
  if (T.vn < 0) {
    uint32_t* cnt = nullptr;
    if (T.cr >= 0 && T.cr < 16 && T.step >= 0 && T.step < 32) {
      uint32_t* pg = mode_ == SeqMode::Song ? (T.row >= 0 && T.row < kSongRows ? pages_[(size_t)T.row * kSeqTracks + (size_t)t] : nullptr) : pages_[(size_t)t];
      if (pg) cnt = &pg[T.cr * 32 + T.step];
    }
    if (cnt) { T.vn = (int)*cnt; (*cnt)++; } else { T.vn = 0; dropped_++; }
  }
  uint32_t sd;
  if (T.hasSeed) sd = T.seed; else if (hasSseed_) sd = sseed_;
  else { const Tr* f = nullptr; for (int u = 0; u < MT; u++) if (tr_[u].hasSeed) { f = &tr_[u]; break; }   // anySeed()
    if (f) sd = f->seed; else { if (!hasPseed_) { hasPseed_ = true; pseed_ = o_.hasSeed ? o_.seed : (uint32_t)std::floor(mathRandom() * 4294967296.0); } sd = pseed_; } }
  const uint32_t h = hmix(hmix(hmix(sd ^ js_imul((uint32_t)(t + 1), 0x9E3779B1u)) ^ (uint32_t)key) ^ js_imul((uint32_t)(T.vn * 31 + salt + 1), 0x27D4EB2Fu));
  return (double)h / 4294967296.0;
}
int Seq::arpOf(int v, const Tr& T, double at, double end, double tick, int* off) {
  const int X = v >> 4, Y = v & 15, spd = std::max(1, T.arcSpd), m = T.arcMode & 3;
  const int pat[3][4] = {{0, X, Y, 0}, {Y, X, 0, 0}, {0, X, Y, X}}; const int* pt = m == 1 ? pat[1] : m == 2 ? pat[2] : pat[0]; const int pn = m == 2 ? 4 : 3;
  const int cap = kSeqTracks * kSeqArpMax; *off = arpUsed_; int n = 0; double tt = at;
  for (int k = 0; tt < end - 1e-6 && k < 96; k++, tt += spd * tick) {
    const double rate = js_pow(2, (double)(m == 3 ? pt[(int)std::floor(rnd() * 3)] : pt[k % pn]) / 12);
    if (arpUsed_ < cap) { arpPool_[(size_t)arpUsed_ * 2] = tt; arpPool_[(size_t)arpUsed_ * 2 + 1] = rate; arpUsed_++; n++; } else dropped_++;
  }
  if (arpUsed_ > maxArp_) maxArp_ = arpUsed_;
  return n;
}
int Seq::dirNext(Tr& T, int y, int L) {
  if (T.dir == 1) return y > 0 ? y - 1 : L - 1;
  if (T.dir == 3) return (int)std::floor(rnd() * L);
  int n = y + T.pp; if (n >= L) { T.pp = -1; n = std::max(0, L - 2); } else if (n < 0) { T.pp = 1; n = std::min(L - 1, 1); }
  return n;
}
const Seq::PInst& Seq::instOf(int ii) const {   // IS[ii] || IS[0] || S.insts[ii] || S.insts[0], IS = instsNow()
  if (pfPart_ >= 0 && pfPart_ < (int)parts_.size() && parts_[(size_t)pfPart_].present) { const std::vector<PInst>& IS = parts_[(size_t)pfPart_].insts;
    if (ii >= 0 && ii < (int)IS.size() && IS[(size_t)ii].present) return IS[(size_t)ii];
    if (!IS.empty() && IS[0].present) return IS[0]; }
  { const PInst& s = sInsts_[(size_t)ci(ii, 0, NI - 1)]; if (s.present) return s; }   // S.insts[ii] || S.insts[0] (instrument 0 even when it is missing too: default values, where the JS would throw)
  return sInsts_[0];
}
// N1 review C2: settle()'s inner loop walks every row of the loop range, so a loop of 1e8 rows held one step() for 100 ms and 2e9
// for seconds (then signed overflow). The ends are clamped to what the mode can hold: 0..255 song rows, 0..15 chain rows, 0..31 steps.
// (Every loop a song or the page can make is inside these: the goldens and seq_extra's loops are unchanged.)
void Seq::setLoop(bool has, int y0, int y1) {
  const int mx = loopMax();
  hasLoop_ = has; loopY0_ = std::max(0, std::min(mx, y0)); loopY1_ = std::max(0, std::min(mx, y1));
}
SeqEvent* Seq::push(Ctx& c) {
  if (c.n < c.cap) { SeqEvent* e = &c.evs[c.n++]; *e = SeqEvent(); return e; }
  c.dropped++; dropped_++; c.scratch = SeqEvent(); return &c.scratch;
}
void Seq::sentSet(int id, double v) { if (!sentHas_[(size_t)id]) { sentHas_[(size_t)id] = 1; sentOrd_[(size_t)sentN_++] = id; } sentVal_[(size_t)id] = v; }
void Seq::sentDel(int id) { if (!sentHas_[(size_t)id]) return; sentHas_[(size_t)id] = 0; int w = 0; for (int i = 0; i < sentN_; i++) if (sentOrd_[(size_t)i] != id) sentOrd_[(size_t)w++] = sentOrd_[(size_t)i]; sentN_ = w; }
// tgtEvs(id, v, at, PF.pno): own = (v == null)
void Seq::tgtEvs(Ctx& c, int id, bool own, double v, double at) {
  if (id < 0) return;
  const TgtId& g = ids_[(size_t)id]; if (!g.ok) return;
  const int pi = pnoIdx(g.kind);
  if (pi >= 0) { if (g.t <= MT) { const int t = (int)g.t; pnoHas_[t][pi] = !own; pno_[t][pi] = v; } return; }
  if (g.kind == K_VOL) {
    if (g.t < NT) { SeqEvent* e = push(c); e->type = SEV_TVA; e->t = at; e->track = (int)g.t; e->v = own ? S_.mix[(int)g.t].vol : v; }
    else if (g.t == MT) { SeqEvent* e = push(c); e->type = SEV_MOUT; e->t = at; e->track = MT; e->v = own ? g.own : v; }
    return; }
  if (g.kind == K_PAN) { if (g.t < NT) { SeqEvent* e = push(c); e->type = SEV_TPN; e->t = at; e->track = (int)g.t; e->v = own ? S_.mix[(int)g.t].pan : v; } return; }
  if (g.kind == K_FX && g.fxOk) { SeqEvent* e = push(c); e->type = SEV_FXP; e->t = at; e->track = (int)std::min(g.t, 2147483647.0); e->slot = (int)std::min(g.sl, 2147483647.0); e->pi = (int)std::min(g.p, 2147483647.0);
    if (g.hasB) { e->has |= SH_BAND; e->band = g.b; } e->v = own ? g.own : v; }
}
// perfDiff(evs, at): perfResolve(PF), then what changed since the last one
void Seq::perfDiff(Ctx& c, double at) {
  for (int i = 0; i < wantN_; i++) wantHas_[(size_t)wantOrd_[(size_t)i]] = 0;
  wantN_ = 0;
  const auto want = [this](int id, double v) { if (!wantHas_[(size_t)id]) { wantHas_[(size_t)id] = 1; wantOrd_[(size_t)wantN_++] = id; } wantVal_[(size_t)id] = v; };
  const double x = cd(pfXf_, 0, 255) / 255;
  for (const SceneEnt& e : scene_) { const double own = ids_[(size_t)e.id].own, a = e.inA ? e.a : own, b = e.inB ? e.b : own; want(e.id, js_round(a + (b - a) * x)); }
  for (size_t i = 0; i < macros_.size(); i++) { const Macro& m = macros_[i]; const double v = cd(pfMacSet_[i] ? pfMac_[i] : m.v, 0, 255); for (const MacTg& g : m.tg) want(g.id, js_round(g.lo + (g.hi - g.lo) * v / 255)); }
  for (int i = 0; i < wantN_; i++) { const int id = wantOrd_[(size_t)i]; const double v = wantVal_[(size_t)id]; if (!(sentHas_[(size_t)id] && sentVal_[(size_t)id] == v)) { tgtEvs(c, id, false, v, at); sentSet(id, v); } }
  int w = 0; for (int i = 0; i < sentN_; i++) { const int id = sentOrd_[(size_t)i]; if (!wantHas_[(size_t)id]) { tgtEvs(c, id, true, 0, at); sentHas_[(size_t)id] = 0; } else sentOrd_[(size_t)w++] = id; }
  sentN_ = w;
}
void Seq::slotWake(Ctx& c, int t, int need, bool on, double sw, double at2) {
  if (!on) return;
  for (int k = 0; k < FXSLOTS; k++) if (need_[t][k] == need) { SeqEvent* e = push(c); e->type = SEV_FXC; e->t = at2; e->track = t; e->p = k; e->v = sw; }
}
// fxEvs(t, fx, at, line): FXO / FLX / FLY / GAT / RTG, the master column's VOL, BYP
void Seq::fxEvs(Ctx& c, int t, const Fx& fx, double at, double line) {
  const auto fxc = [&](double tt, int p, double v) { SeqEvent* e = push(c); e->type = SEV_FXC; e->t = tt; e->track = t; e->p = p; e->v = v; };
  if (fx.H(FXO)) { const int X = fx[FXO] >> 4, Y = fx[FXO] & 15;
    if (X == 0) for (int k = 0; k < FXSLOTS; k++) fxc(at, k, -1);
    else if (X <= FXSLOTS || X == 9) for (int k = X == 9 ? 0 : X - 1; k < (X == 9 ? FXSLOTS : X); k++) { fxc(at, k, Y ? 1 : 0); if (Y >= 2) fxc(at + Y * line, k, -1); } }
  if (fx.H(FLX)) { fxc(at, 8, flxOld_ ? std::min(16, fx[FLX]) : std::min(255, fx[FLX])); slotWake(c, t, 1, fx[FLX] > 0, -1, at); }
  if (fx.H(FLY)) { fxc(at, 8, fx[FLY] ? 256 + fx[FLY] : 0); slotWake(c, t, 1, fx[FLY] > 0, -1, at); }
  if (fx.H(GAT)) { fxc(at, 9, fx[GAT] == 0 ? 0 : fx[GAT] >= 16 ? 16 : fx[GAT]); slotWake(c, t, 2, fx[GAT] != 0, -1, at); }
  if (fx.H(RTG)) { const int X = fx[RTG] >> 4, Y = fx[RTG] & 15; fxc(at, 10, X ? std::min(8, X) : 0); slotWake(c, t, 3, true, X ? 1 : -1, at);
    if (X && Y) { fxc(at + Y * line, 10, -1); slotWake(c, t, 3, true, -1, at + Y * line); } }
  if (t == MT && fx.H(VOL)) { SeqEvent* e = push(c); e->type = SEV_MVOL; e->t = at; e->v = fx[VOL]; }
  if (fx.H(BYP) && t < MT) { const int X = fx[BYP] >> 4, Y = fx[BYP] & 15;
    for (int k = X ? X : 1; k <= (X ? X : 15); k++) { SeqEvent* e = push(c); e->type = SEV_CHC; e->t = at; e->track = t; e->p = k; e->v = Y >= 2 ? 2 : Y;
      if (Y >= 2) { SeqEvent* e2 = push(c); e2->type = SEV_CHC; e2->t = at + Y * line; e2->track = t; e2->p = k; e2->v = -1; } } }
}
// engEmit(u, t0, dur, ne, pts): the `eng` event of track u (pts: the LFO's points per ENGK kind, ptsHas: which kinds have them; null: one point)
void Seq::engEmit(Ctx& c, int u, double t0, double dur, int ne, const double (*pts)[4], unsigned ptsHas) {
  Tr& T = tr_[u];
  const double cc[5] = {(double)(T.cut >= 0 ? T.cut : -1), (double)(T.rsg ? T.res : -1), (double)(T.dec >= 0 ? T.dec : -1), (double)(T.cAtk >= 0 ? T.cAtk : -1), (double)(T.cRel >= 0 ? T.cRel : -1)};
  double o0[7];
  for (int k = 0; k < 7; k++) { const int kind = ENGK[k], id = canon_[k][u];
    const bool cmd = kind == K_CUT ? T.cut >= 0 : kind == K_RES ? T.rsg : kind == K_DEC ? T.dec >= 0 : kind == K_ATK ? T.cAtk >= 0 : kind == K_REL ? T.cRel >= 0 : false;
    o0[k] = cmd || !sentHas(id) ? 0 : sentVal_[(size_t)id] - (kind == K_CUT || kind == K_DEC ? 255 : 0); }
  const int np = pts ? ne : 1; double os[4][7];
  const auto same = [](double a, double b) { return a == b || (a != a && b != b); };   // the JS compares the numbers as text
  int lastState = T.eng; double lastC[5], lastO[7];
  if (lastState == 2) { std::memcpy(lastC, T.engC, sizeof lastC); std::memcpy(lastO, T.engO, sizeof lastO); }
  else { for (double& x : lastC) x = -1; for (double& x : lastO) x = 0; }
  bool changed = false;
  for (int p = 0; p < np && p < 4; p++) { for (int k = 0; k < 7; k++) os[p][k] = js_round(cd(o0[k] + (pts && ((ptsHas >> k) & 1) ? pts[k][p] : 0), -255, 255) * 1000) / 1000;
    bool eq = lastState != 1; for (int k = 0; k < 5 && eq; k++) eq = same(cc[k], lastC[k]); for (int k = 0; k < 7 && eq; k++) eq = same(os[p][k], lastO[k]);
    if (!eq) changed = true;
    lastState = 2; std::memcpy(lastC, cc, sizeof lastC); std::memcpy(lastO, os[p], sizeof lastO); }
  if (!changed) return;
  for (int p = 0; p < np && p < 4; p++) { SeqEvent* e = push(c); e->type = SEV_ENG; e->t = t0 + dur * p / np; e->track = u; std::memcpy(e->engC, cc, sizeof cc); std::memcpy(e->engO, os[p], sizeof os[p]); }
  T.eng = 2; std::memcpy(T.engC, lastC, sizeof lastC); std::memcpy(T.engO, lastO, sizeof lastO);
}
// lfoEvs(t, t0, dur): the TRACK LFOs of track t over [t0, t0 + dur), and the `eng` events of the tracks they (or the row) touch
void Seq::lfoEvs(Ctx& c, int t, double t0, double dur) {
  const int ne = std::min(4, tks_);   // lfoPts(false): LFO_PTS_LIVE = LFO_PTS_RENDER_FX = LFO_PTS_RENDER_PARAM = 4
  int epOrd[NT]; int epN = 0; bool epSeen[NT]; unsigned epHas[NT]; double epPts[NT][7][4];
  for (int u = 0; u < NT; u++) { epSeen[u] = false; epHas[u] = 0; }
  if (lfoIsArr_[t]) { const std::vector<Lfo>& La = lfo_[t]; const int n0 = (int)La.size();
    for (int i = 0; i < n0; i++) { const Lfo& L = La[(size_t)i]; if (!L.present || !L.ok) continue;
      bool dup = false; for (int j = 0; j < i; j++) if (La[(size_t)j].present && La[(size_t)j].id == L.id) { dup = true; break; } if (dup) continue;
      const TgtId& g = ids_[(size_t)L.id]; const bool fl = g.kind == K_CUT || g.kind == K_RES || g.kind == K_DEC || g.kind == K_TCH; const int n = fl ? 1 : ne;
      const double cv = sentHas(L.id) ? sentVal_[(size_t)L.id] : g.own;
      const auto sw = [&](double tt, double v) { for (int j = i; j < n0; j++) { const Lfo& M = La[(size_t)j]; if (!M.present || M.id != L.id) continue;
          const double rate = M.rt >= 0 && M.rt <= 8 && M.rt == std::floor(M.rt) ? PLFO_RATES[(int)M.rt] : 1, bt = (bt_ + (tt - t0) * bpm_ / 60) / rate, cyc = std::floor(bt); v += lfoAt(M.sh, bt - cyc, cyc) * M.dp / 2; } return v; };
      if (!(g.kind == K_ENV || g.kind == K_ATK || g.kind == K_SUS || g.kind == K_REL)) for (int k = 0; k < n; k++) { const double tt = t0 + dur * k / n; tgtEvs(c, L.id, false, cd(js_round(sw(tt, cv)), 0, 255), tt); }
      const int ek = engIdx(g.kind);
      if (ek >= 0 && g.t < NT) { const int u = (int)g.t; if (!epSeen[u]) { epSeen[u] = true; epOrd[epN++] = u; } epHas[u] |= 1u << ek; for (int k = 0; k < ne; k++) epPts[u][ek][k] = sw(t0 + dur * k / ne, 0); } } }
  bool own = false; if (t < NT && !epSeen[t]) { epSeen[t] = true; epOrd[epN++] = t; own = true; }
  for (int i = 0; i < epN; i++) { const int u = epOrd[i]; if (own && u == t) engEmit(c, u, t0, dur, ne, nullptr, 0); else engEmit(c, u, t0, dur, ne, epPts[u], epHas[u]); }
}

// ---- the clock's setters (SEQ-LIVE-DESIGN A1) ---------------------------------------------------------------------------------
void Seq::setTime(double t) { if (!std::isfinite(t)) { badSets_++; return; } time_ = t; }
void Seq::setBpm(double v) {
  if (!std::isfinite(v) || !(v > 0)) { badSets_++; return; }
  if (v < kSeqBpmMin) { v = kSeqBpmMin; badSets_++; } else if (v > kSeqBpmMax) { v = kSeqBpmMax; badSets_++; }
  bpm_ = v;
}
void Seq::shift(double d) {
  if (!std::isfinite(d)) { badSets_++; return; }
  const double nt = time_ + d, nbase = base_ + d, nbt = btT_ + d;
  if (!std::isfinite(nt) || (hasBase_ && !std::isfinite(nbase)) || (hasBtT_ && !std::isfinite(nbt))) { badSets_++; return; }
  time_ = nt; if (hasBase_) base_ = nbase; if (hasBtT_) btT_ = nbt;
}

// ---- api.step() ------------------------------------------------------------------------------------------------------
int Seq::step(SeqEvent* evs, int cap, SeqStep* out) {
  Ctx c; c.evs = evs; c.cap = cap < 0 ? 0 : cap; c.n = 0; c.dropped = 0; c.pos = out ? out->pos : nullptr;
  SeqPos posLocal[kSeqTracks]; SeqPos* pos = out ? out->pos : posLocal;
  Tr* tr = tr_.get(); arpUsed_ = 0;
  const bool song = mode_ == SeqMode::Song, phrase = mode_ == SeqMode::Phrase;
  if (!hasBase_) { hasBase_ = true; base_ = time_; }
  double rel0 = kInf; for (int t = 0; t < kSeqTracks; t++) if (tr[t].on && tr[t].rel < rel0) rel0 = tr[t].rel;
  if (rel0 == kInf) rel0 = time_ - base_;
  if (wakeOk_) wake(rel0);
  // END / JMP: looked for on the rows due now, before any of them plays
  if (song && !hasLoop_) { int jc = 0, jto = 0, jfrom = 0;   // jc: 1 END, 2 JMP
    for (int t = 0; t <= MT && !jc; t++) { const Tr& T = tr[t]; if (!T.on || T.empty || T.rel > rel0 + 1e-9) continue;
      const int16_t* r = rowOf(T, t, chainPhrase(sectId(T, t), T.cr)); if (!r) continue;
      for (int k = FX0; k + 1 < RW; k += 2) { if (r[k] == END) { jc = 1; break; } if (r[k] == JMP) { jc = 2; jto = ci(r[k + 1], 0, 255); jfrom = T.row; break; } } }
    if (jc) {
      if (jc == 1 || (once_ && jto <= jfrom)) { for (int t = 0; t < kSeqTracks; t++) { tr[t].on = false; tr[t].wr = 0; } }
      else { const int y = jto; for (int t = 0; t <= MT; t++) { Tr& T = tr[t]; fresh(T); T.row = y; T.rel = rel0; T.r0 = rel0;
          if ((t < ntr_ || t == MT) && songCell(y, t) >= 0 && (!o_.hasOnly || ((o_.only >> t) & 1))) { T.on = true; settle(T, t); } }
        chaseChance(y); } } }
  const double t0 = base_ + rel0, sw = (double)ci(S_.swing, 50, 75) / 100; const int swB = ci(S_.swingB ? S_.swingB : 2, 1, 7);
  for (int t = 0; t < kSeqTracks; t++) pos[t] = SeqPos();
  double nb = bpm_, pdl = 0; bool nHasGrv = hasGrv_; int ngrv[2] = {grv_[0], grv_[1]}; const int tks0 = tks_;
  // the song position in beats at this step
  if (!hasBtT_) { hasBtT_ = true; btT_ = t0; bt_ = 0; } else { bt_ += (t0 - btT_) * bpm_ / 60; btT_ = t0; }
  { SeqEvent* e = push(c); e->type = SEV_BEAT; e->t = t0; e->v = bt_; }
  if (perfFirst_) { perfFirst_ = false; perfDiff(c, t0); }
  const auto cmdV = [](const int16_t* r, int cmd, int* v) { bool has = false; if (r) for (int k = FX0; k < RW; k += 2) if (r[k] == cmd) { *v = std::max(0, (int)r[k + 1]); has = true; } return has; };
  // the phrase a track is on: -1 when it has none (phrase mode: o.phrase); *ctsp: the chain row's transpose
  const auto pidOf = [&](const Tr& T, int t, int* cidOut, int* ctsp) { *ctsp = 0; if (phrase) { *cidOut = -1; return o_.phrase; } const int cid = sectId(T, t); *cidOut = cid; int ph = -1, ts = 0;
    if (!T.empty && cid >= 0 && chainRow(cid, T.cr, &ph, &ts)) { *ctsp = ts; return ph; } return -1; };
  // GGR on a row due now grooves every track's step from this one
  for (int t = 0; t <= MT; t++) { const Tr& T = tr[t]; if (!T.on || T.rel > rel0 + 1e-9) continue; int cid, ctsp, g;
    if (cmdV(rowOf(T, t, pidOf(T, t, &cid, &ctsp)), GGR, &g)) { hasGrv_ = nHasGrv = g != 0; grv_[0] = ngrv[0] = (g >> 4) ? (g >> 4) : tks_; grv_[1] = ngrv[1] = (g & 15) ? (g & 15) : tks_; } }
  // the master column's SED and TCH count for every track on the same row: read them first
  { const Tr& T = tr[MT]; if (!phrase && T.on && !(T.rel > rel0 + 1e-9)) { int cid, ctsp, v; const int16_t* r = rowOf(T, MT, pidOf(T, MT, &cid, &ctsp));
      if (r && !cmdV(r, RND, &v) && !cmdV(r, RNL, &v) && !cmdV(r, REP, &v)) { int sd, tc; if (cmdV(r, SED, &sd)) { hasSseed_ = sd != 0; sseed_ = sed_seed((uint32_t)sd); } if (cmdV(r, TCH, &tc)) mtch_ = tc; } } }
  bool lfoRan[kSeqTracks]; for (bool& b : lfoRan) b = false;
  for (int t = 0; t <= MT; t++) {
    Tr& T = tr[t]; if (!T.on || T.rel > rel0 + 1e-9) continue;
    int cid, ctsp; const int pid = pidOf(T, t, &cid, &ctsp);
    const double line = 60 / bpm_ / plpbOf(pid);
    // TIC on this row changes the ticks a step from here on, and counts already for this row
    { const int16_t* r = rowOf(T, t, pid); if (r) { int tc = 0; bool has = false; for (int k = FX0; k < RW; k += 2) if (r[k] == TIC) { tc = std::max(0, (int)r[k + 1]); has = true; } if (has) tks_ = tc ? ci(tc, 1, 32) : ci(S_.ticks ? S_.ticks : TICKS, 1, 32); }
      int g; if (cmdV(r, GRV, &g)) { T.hasGrv = g != 0; T.grv[0] = (g >> 4) ? (g >> 4) : tks_; T.grv[1] = (g & 15) ? (g & 15) : tks_; } }
    const double tick = line / tks_; const bool hasGv = T.hasGrv || hasGrv_; const int* gv = T.hasGrv ? T.grv : grv_;
    const int pair = 2 * swB, slot = ((T.dir && !(hasLoop_ && phrase) ? T.cnt : T.step) + (song ? offInSong(T, t) : 0)) % pair; const bool offB = slot >= swB;
    const double dur = hasGv ? gv[T.g % 2] * tick : 2 * line * (offB ? 1 - sw : sw);
    SeqPos& P = pos[t]; P.valid = true; P.row = T.row; P.cr = T.cr; P.step = T.step; P.sect = cid; P.phrase = pid;
    const bool muted = mutedAt(t, T.row); const int ev0 = c.n;
    const int16_t* r = rowOf(T, t, pid);
    int hop = -1; T.vn = -1;
    if (r) {
      // the three FX columns left to right
      Cmd cmds[3]; int nc = 0, nrnd = 0;
      for (int k = FX0; k < RW; k += 2) {
        if (r[k] < 0 || r[k] >= FXC_N) continue;
        const int cm = r[k], v = std::max(0, (int)r[k + 1]);
        if (cm == PRB) { Cmd* tg = nc ? &cmds[nc - 1] : nullptr; if (tg && (tg->c == SED || tg->c == TCH || tg->c == SKP || tg->c == CHD)) continue;
          if (tg) { if (roll(T, t, 8 + k) * 255 >= v) nc--; } else if (roll(T, t, 8 + k) * 255 >= v) cmds[nc++] = Cmd{CHA, 0}; continue; }
        if (cm == SED) { if (t == MT) { hasSseed_ = v != 0; sseed_ = sed_seed((uint32_t)v); } else { T.hasSeed = v != 0; T.seed = sed_seed((uint32_t)v); } }
        if (cm == REP) { if (!T.hasLast) continue; T.lastV = (T.lastV + v) & 255; cmds[nc++] = Cmd{T.lastC, T.lastV}; continue; }
        if (cm == RND || cm == RNL) {
          const int dn = v >> 4, up = v & 15; Cmd* tg = nc ? &cmds[nc - 1] : nullptr;
          if (!tg) { if (cm == RNL) { T.rnlNote = ci(T.rnlNote + rint(dn, up), -48, 48); nrnd = T.rnlNote; } else nrnd = rint(dn, up); continue; }
          const int st = rstep(tg->c); const bool sg = isSigned(tg->c);
          const int base = cm == RNL && T.rnlHas[tg->c] ? T.rnl[tg->c] : sg ? s8(tg->v) : tg->v;
          int nv = base + rint(dn, up) * st; nv = sg ? ci(nv, -128, 127) : ci(nv, 0, 255);
          if (cm == RNL) { T.rnlHas[tg->c] = true; T.rnl[tg->c] = nv; }
          tg->v = nv & 255; continue;
        }
        T.hasLast = true; T.lastC = cm; T.lastV = v; cmds[nc++] = Cmd{cm, v};
      }
      Fx fx; for (int i = 0; i < nc; i++) { const Cmd& q = cmds[i]; if (q.c == FLX) fx.has[FLY] = false; else if (q.c == FLY) fx.has[FLX] = false; fx.has[q.c] = true; fx.v[q.c] = q.v; }
      if (fx.H(TPO) && fx[TPO] >= 40) nb = fx[TPO];
      if (fx.H(GRV)) { T.hasGrv = fx[GRV] != 0; T.grv[0] = (fx[GRV] >> 4) ? (fx[GRV] >> 4) : tks_; T.grv[1] = (fx[GRV] & 15) ? (fx[GRV] & 15) : tks_; }
      if (fx.H(GGR)) { nHasGrv = fx[GGR] != 0; ngrv[0] = (fx[GGR] >> 4) ? (fx[GGR] >> 4) : tks_; ngrv[1] = (fx[GGR] & 15) ? (fx[GGR] & 15) : tks_; }
      if (fx.H(SED)) { isSeeded_ = fx[SED] != 0; if (isSeeded_) seeded_ = Mulberry32(sed_seed((uint32_t)fx[SED])); if (t == MT) { hasSseed_ = fx[SED] != 0; sseed_ = sed_seed((uint32_t)fx[SED]); } else { T.hasSeed = fx[SED] != 0; T.seed = sed_seed((uint32_t)fx[SED]); } }
      if (fx.H(TCH)) { if (t == MT) mtch_ = fx[TCH]; else T.tch = fx[TCH] >= 255 ? -1 : fx[TCH]; }
      if (fx.H(SKP)) T.skp = fx[SKP] ? 1 : 0;
      if (fx.H(CHD)) T.chd = fx[CHD] ? 1 : 0;
      if (fx.H(TSP)) T.tsp = s8(fx[TSP]);
      if (fx.H(LPB)) { hasLpb_ = true; lpb_ = fx[LPB] ? ci(fx[LPB], 1, 16) : 0; }
      if (fx.T(PDL)) pdl = std::max(pdl, fx[PDL] * line);
      if (fx.H(TVL)) { SeqEvent* e = push(c); e->type = SEV_TVL; e->t = t0; e->track = t; e->v = fx[TVL]; }
      if (fx.H(TPN)) { SeqEvent* e = push(c); e->type = SEV_TPN; e->t = t0; e->track = t; e->v = fx[TPN]; }
      if (fx.H(STP)) { SeqEvent* e = push(c); e->type = SEV_STP; e->t = t0; e->track = t; e->flag = fx[STP] > 0; }
      if (fx.H(FXS)) { T.hasFxs = true; T.fxsS = std::max(1, fx[FXS] >> 4) - 1; T.fxsP = std::max(1, fx[FXS] & 15) - 1; }
      if (fx.H(SCA)) { T.hasScl = true; T.sclScale = std::min(14, fx[SCA] & 15); T.sclRoot = (fx[SCA] >> 4) % 12; }
      if (fx.H(SCG)) { hasGscl_ = true; gsclScale_ = std::min(14, fx[SCG] & 15); gsclRoot_ = (fx[SCG] >> 4) % 12; }
      if (fx.H(CUT)) T.cut = fx[CUT] >= 255 ? -1 : fx[CUT];
      if (fx.H(RES)) { T.res = fx[RES]; T.rsg = true; }
      if (fx.H(DEC)) T.dec = fx[DEC] >= 255 ? -1 : fx[DEC];
      if (fx.H(ATK)) T.cAtk = fx[ATK] >= 255 ? -1 : fx[ATK];
      if (fx.H(REL)) T.cRel = fx[REL] >= 255 ? -1 : fx[REL];
      if (fx.H(ARC)) { T.arcMode = fx[ARC] >> 4; T.arcSpd = (fx[ARC] & 15) ? (fx[ARC] & 15) : 1; }
      if (fx.H(DIR)) { T.dir = std::min(3, fx[DIR]); T.pp = T.dir == 1 ? -1 : 1; }
      if (fx.H(HOP)) hop = std::min(fx[HOP], PROWS - 1);
      const double nud = fx.H(NUD) ? s8(fx[NUD]) * tick : 0;
      const double at = std::max(t0 - line * 0.5, t0 + std::min(fx.or0(DEL) * tick, dur * 0.98) + nud), end = t0 + dur;
      // PERFORM commands: XFD, MAC, PRT, LK1-LK4
      if (fx.H(XFD)) { pfXf_ = fx[XFD]; perfDiff(c, at); }
      if (fx.H(MAC)) { const int X = fx[MAC] >> 4; if (X >= 1 && X <= PERF_MACROS) { pfMac_[(size_t)X - 1] = (fx[MAC] & 15) * 17; pfMacSet_[(size_t)X - 1] = 1; perfDiff(c, at); } }
      if (fx.H(PRT)) { const int k = fx[PRT] - 1; const bool has = k >= 0 && k < (int)parts_.size() && parts_[(size_t)k].present; pfPart_ = has ? k : -1;
        const std::vector<PEv>& pe = has ? parts_[(size_t)k].evs : own_.evs;
        for (const PEv& x : pe) { SeqEvent* e = push(c); e->type = x.type; e->t = at; e->track = x.track; e->slot = x.slot; e->pi = x.pi; e->v = x.v; }
        for (int i = 0; i < sentN_; i++) sentHas_[(size_t)sentOrd_[(size_t)i]] = 0;
        sentN_ = 0; perfDiff(c, at); }
      for (int n = 0; n < 4; n++) { const int id = lk_[t][n]; if (!fx.H(LK1 + n) || id < 0) continue; tgtEvs(c, id, false, fx[LK1 + n], at); T.hasLk = true; T.lk[n] = id; }
      bool play = true;
      if (fx.H(NTH)) { const int X = (fx[NTH] >> 4) ? (fx[NTH] >> 4) : 1, Y = (fx[NTH] & 15) ? (fx[NTH] & 15) : 1; uint32_t* cnt = nthSlot(t, pid, T.step, false); const uint32_t n = cnt ? (*cnt)++ : 0;
        play = (int)(n % (uint32_t)X) == (Y - 1) % X; T.cres = play ? 1 : 2; }
      // CHANCE: the note's own (CHA) times the track's (TCH, else a MOD on CHANCE) times the master column's TCH
      const bool polyT = isPolyT(t); bool hasN = false, hasKeep = false; bool keepC[NCOL] = {false, false, false, false};
      if (t < MT && !busT_[t]) for (int col = 0; col < (polyT ? NCOL : 1); col++) if (r[col] >= 0 && r[col] < 120) { hasN = true; break; }
      if (hasN && T.hasLk) for (int n = 0; n < 4; n++) { const int id = T.lk[n]; if (id < 0) continue; if (fx.H(LK1 + n) || !ids_[(size_t)id].tch4) continue; T.lk[n] = -1; tgtEvs(c, id, !sentHas(id), sentHas(id) ? sentVal_[(size_t)id] : 0, at); }
      { const bool po = hasN && pnoHas_[t][3]; const double tc = !hasN ? 255 : T.tch >= 0 ? T.tch : po ? pno_[t][3] : 255, pc = (fx.H(CHA) ? fx[CHA] : 255) * tc * (hasN ? mtch_ : 255);
        if (pc < 16581375) { const double p = pc / 16581375; bool ok;
          if (T.chd && polyT && hasN) { hasKeep = true; int nk = 0; for (int col = 0; col < NCOL; col++) if (r[col] >= 0 && r[col] < 120 && roll(T, t, 1 + col) < p) { keepC[col] = true; nk++; } ok = nk > 0; }
          else ok = roll(T, t, 0) < p;
          if (!ok) play = false;
          T.cres = ok ? 1 : 2; }
        else if (fx.H(CHA)) T.cres = 1; }
      // TRIG CONDITIONS
      if (fx.H(CND) && (fx[CND] & 15) == 8) { play = !play; T.cres = play ? 1 : 2; hasKeep = false; }
      else if (fx.H(CND)) { const int ty = std::min(7, fx[CND] & 15), X = fx[CND] >> 4, kind = ty >> 1; bool ok;
        if (kind == 0) ok = pfFill_;
        else if (kind == 1) ok = T.cres == 1;
        else if (kind == 2) { const int nbi = X ? std::min(MT, X - 1) : t - 1; ok = nbi >= 0 && tr[nbi].cres == 1; }
        else { uint32_t* cnt = nthSlot(t, pid, T.step, true); const uint32_t n = cnt ? (*cnt)++ : 0; ok = n == 0; }
        if (ty & 1) ok = !ok;
        if (kind == 3) T.cres = ok ? 1 : 2;
        if (!ok) play = false; }
      if (play) fxEvs(c, t, fx, at, line);
      if (play && fx.H(FXV)) { SeqEvent* e = push(c); e->type = SEV_FXP; e->t = at; e->track = t; e->slot = T.hasFxs ? T.fxsS : 0; e->pi = T.hasFxs ? T.fxsP : 0; e->v = fx[FXV]; }
      // MIDI: PGM and the CC slots go to the row's instrument (or the track's last), ahead of the row's notes
      if (play && t < MT) {
        const int mi = fx.H(INS) ? ci(fx[INS], 0, NIs_ - 1) : r[IC] >= 0 ? r[IC] : T.inst;
        if (fx.H(PGM)) { SeqEvent* e = push(c); e->type = SEV_MPG; e->t = at; e->track = t; e->ii = mi; e->v = std::min(127, fx[PGM]); }
        const double ramp = fx.H(CSL) ? (fx[CSL] ? fx[CSL] * tick : std::max(0.0, end - at)) : 0;
        if (fx.H(MPB)) { SeqEvent* e = push(c); e->type = SEV_MPB; e->t = at; e->track = t; e->ii = mi; e->v = fx[MPB]; }
        if (fx.H(MAT)) { SeqEvent* e = push(c); e->type = SEV_MAT; e->t = at; e->track = t; e->ii = mi; e->v = std::min(127, fx[MAT]); }
        for (int k = 0; k < 4; k++) if (fx.H(CCA + k)) { SeqEvent* e = push(c); e->type = SEV_MCC; e->t = at; e->track = t; e->ii = mi; e->k = k; e->v = std::min(127, fx[CCA + k]); e->ramp = ramp; }
      }
      const double pit = (fx.H(PIT) ? s8(fx[PIT]) : 0) + (fx.H(FIN) ? (double)s8(fx[FIN]) / 100 : 0);
      // poly tracks: all four note columns play, as a chord
      const bool poly = polyT; const int nns = poly ? NCOL : 1;
      const bool nbus = t < MT && !busT_[t]; bool offN = false; if (nbus) for (int i = 0; i < nns; i++) if (r[i] == NOTE_OFF) offN = true;
      int notes[NCOL], ncol[NCOL], nn = 0;
      if (nbus) for (int i = 0; i < nns; i++) if (r[i] >= 0 && r[i] < 120) { notes[nn] = r[i]; ncol[nn] = i; nn++; }
      const bool hasOffC = poly && offN; uint8_t offC = 0; if (hasOffC) for (int i = 0; i < nns; i++) if (r[i] == NOTE_OFF) offC |= (uint8_t)(1u << i);
      if (offN) { SeqEvent* e = push(c); e->type = SEV_OFF; e->t = at; e->track = t; if (hasOffC) { e->has |= SH_COLS; e->cols = offC; P.offc = offC; } else P.off = true; }
      if (hasKeep && play) { int w = 0; for (int k = 0; k < nn; k++) if (keepC[ncol[k]]) { notes[w] = notes[k]; ncol[w] = ncol[k]; w++; } nn = w; }   // CHD 01: the notes whose roll failed
      if (!play && hasN && T.skp && !offN) { SeqEvent* e = push(c); e->type = SEV_OFF; e->t = at; e->track = t; P.off = true; }   // SKP 01
      if (fx.H(ONE) && nn > 1) { const int k = (int)std::floor(rnd() * nn); notes[0] = notes[k]; ncol[0] = ncol[k]; nn = 1; }   // ONE: one note of the chord
      if (nn && play && T.hasLk) for (int n = 0; n < 4; n++) { const int id = T.lk[n]; if (id < 0 || fx.H(LK1 + n)) continue; T.lk[n] = -1; tgtEvs(c, id, !sentHas(id), sentHas(id) ? sentVal_[(size_t)id] : 0, at); }   // a lock lasts until the next note without it
      if (nn && play) {
        const int ii = fx.H(INS) ? ci(fx[INS], 0, NIs_ - 1) : r[IC] >= 0 ? std::min((int)r[IC], NI - 1) : T.inst; T.inst = ii;
        const PInst& I = instOf(ii);
        // PERFORM: the pad filter from a scene, macro, lock or LFO (pno) when no CUT / RES / DEC of the track's own is on
        const bool* ph = pnoHas_[t]; const double* po = pno_[t];
        const bool hasPc = T.cut >= 0 || (ph[0] && po[0] < 255), hasPdc = T.dec >= 0 || (ph[2] && po[2] < 255);
        const double pc = T.cut >= 0 ? T.cut : po[0], pdc = T.dec >= 0 ? T.dec : po[2], prs = T.cut >= 0 ? T.res : ph[1] ? po[1] : T.res;
        const int volv = fx.H(VOL) ? fx[VOL] : r[VC] >= 0 ? r[VC] : 255, vcv = fx.H(VOL) ? fx[VOL] : r[VC];
        const double gain = curve(volv) * curve(I.vol);
        bool hasArp = false, hasBend = false, hasVib = false; int arpOff = 0, arpN = 0; double bendMul = 1, vibSpd = 0, vibDep = 0;
        if (fx.T(ARP)) { hasArp = true; arpN = arpOf(fx[ARP], T, at, end, tick, &arpOff); }
        else if (fx.T(PBN)) { hasBend = true; bendMul = js_pow(2, (double)s8(fx[PBN]) / 12); }
        else if (fx.T(PVB)) { hasVib = true; vibSpd = fx[PVB] >> 4; vibDep = fx[PVB] & 15; }
        const int every = fx.or0(RET) & 15, fade = fx.or0(RET) >> 4;
        double firstRate = 0;
        // KEY FOLLOW instruments: live transpose and the song's scale, before PIT / FIN. HUMAN TIME / HUMAN VEL
        const bool kf = I.kf; const int lx = kf && o_.live ? o_.lx : 0;
        const int sScale = T.hasScl ? T.sclScale : hasGscl_ ? gsclScale_ : S_.scale, sRoot = T.hasScl ? T.sclRoot : hasGscl_ ? gsclRoot_ : S_.sroot;
        const auto nOf = [&](int x) { const double b = x + ctsp + I.tsp + T.tsp + nrnd + lx; return (kf ? scaleSnap(sScale, sRoot, b) : b) + pit; };
        const double hut = (double)ci(I.hut, 0, 50) / 1000, huv = (double)ci(I.huv, 0, 90) / 100;
        for (int q = 0; q < nn; q++) {
          const int n = notes[q];
          const double nat = hut != 0 ? std::max(t0 - hut, at + (rnd() * 2 - 1) * hut) : at, hg = huv != 0 ? 1 - rnd() * huv : 1;
          const double note = nOf(n), rel = note - ROOT, npc = std::fmod(std::fmod(rel, 12) + 12, 12);
          double ntc = npc == std::floor(npc) && npc >= 0 && npc < 12 ? S_.tune[(int)npc] : 0; if (!(ntc == ntc)) ntc = 0;
          const double rate = js_pow(2, rel / 12) * js_pow(2, ntc / 1200);
          if (!q) firstRate = rate;
          SeqEvent ev; ev.type = SEV_NOTE; ev.t = nat; ev.track = t; ev.poly = poly; ev.col = ncol[q]; ev.pad = V_.vtrk ? NPAD + t : I.pad; ev.rate = rate; ev.gain = gain * hg;
          ev.pan = fx.H(PAN) ? fx[PAN] : I.pan; ev.ofs = fx.H(STA) ? fx[STA] : I.ofs; ev.rev = fx.H(PLY) ? (fx[PLY] & 1) == 1 : I.rev;
          if (fx.H(KIL)) { ev.has |= SH_CUT; ev.cut = nat + fx[KIL] * tick; }
          if (fx.H(OFF)) { ev.has |= SH_REL; ev.rel = nat + fx[OFF] * tick; }
          if (fx.T(PSL) && !q) { ev.has |= SH_SLIDE; ev.slideFrom = T.rate; ev.slideDur = fx[PSL] * tick; }
          if (fx.H(GEN)) { ev.has |= SH_VK; ev.vkKind = 'g'; ev.vkNum = fx[GEN]; } else if (fx.H(MUT)) { ev.has |= SH_VK; ev.vkKind = 'm'; ev.vkNum = fx[MUT]; }
          if (hasPc || hasPdc) { ev.has |= SH_TW; if (hasPc) { ev.has |= SH_TWCUT; ev.twCut = pc; } ev.twRes = prs; if (hasPdc) { ev.has |= SH_TWDEC; ev.twDec = pdc; } }
          if (T.cRel >= 0) { ev.has |= SH_RLO; ev.rlo = decSec(T.cRel); }
          if (fx.H(SLC)) { ev.has |= SH_SLC; ev.slc = fx[SLC]; }
          ev.fdi = fx.H(FDI) ? fx[FDI] * tick : T.cAtk >= 0 ? decSec(T.cAtk) : 0; ev.trm = fx.or0(TRM); ev.apn = fx.or0(APN);
          if (fx.H(WOB)) { ev.has |= SH_WOB; ev.wob = wobOf(fx[WOB]); }
          ev.acc = vcv >= 0xC0;
          if (hasArp) { ev.has |= SH_ARP; ev.arpOff = arpOff; ev.arpN = arpN; }
          if (hasBend) { ev.has |= SH_BEND; ev.bendMul = bendMul; ev.bendT1 = end; }
          if (hasVib) { ev.has |= SH_VIB; ev.vibSpd = vibSpd; ev.vibDep = vibDep; ev.vibT1 = end; }
          ev.ii = ii; ev.mn = note + 12 + ntc / 100; ev.vel = hg * volv / 255; ev.tick = tick; ev.vc = vcv;
          // (applyChop and varWant: not in sim mode, and the core's copies do nothing)
          if (every) {
            // RTV: the retriggers' level curve, signed; without it RET's own fade
            const double K = std::max(1.0, std::ceil((end - 1e-6 - nat) / (every * tick)) - 1); const bool hasRv = fx.H(RTV); const int rv = hasRv ? s8(fx[RTV]) : 0;
            const auto lvl = [&](int k) { return !hasRv ? std::max(0.0, 1 - (double)(fade * k) / 16) : rv > 0 ? std::max(0.0, 1 - rv * (K - k) / 32) : std::max(0.0, 1 + (double)(rv * k) / 32); };
            if (hasRv) { const double g0 = lvl(0); ev.gain *= g0; ev.vel *= g0; }
            const double g0b = hasRv ? std::max(1e-6, lvl(0)) : 1;
            *push(c) = ev;
            int k = 1;
            for (double tt = nat + every * tick; tt < end - 1e-6 && k < 64; tt += every * tick, k++) {
              SeqEvent* e = push(c); *e = ev; e->t = tt; e->has &= ~(uint32_t)(SH_SLIDE | SH_ARP | SH_BEND | SH_VIB);
              if (ev.has & SH_CUT) e->cut = tt + (ev.cut - nat);
              if (ev.has & SH_REL) e->rel = tt + (ev.rel - nat);
              e->gain = ev.gain / g0b * lvl(k); e->vel = ev.vel / g0b * lvl(k); }
          } else *push(c) = ev;
        }
        T.rate = firstRate * (hasBend ? bendMul : 1);
        // SOUNDING NOTES: the pitches this row really starts, by note column
        P.nsc = 0; for (int i = 0; i < nns; i++) if (r[i] >= 0 && r[i] < 120) { P.scCol[P.nsc] = (int8_t)i; P.scNote[P.nsc] = js_round(nOf(r[i])); P.nsc++; }
        P.hasPd = true; P.pd = V_.vtrk ? NPAD + t : I.pad;
      } else if (!nn && !offN && play && t < MT) {
        // no note on this row: instrument commands change the note that is already playing
        SeqEvent m; m.type = SEV_MOD; m.t = at; m.track = t; bool any = false;
        if (fx.H(VOL)) { m.has |= SH_GAIN; m.gain = curve(fx[VOL]); any = true; }
        if (fx.H(PAN)) { m.has |= SH_PAN; m.pan = fx[PAN]; any = true; }
        if (fx.H(WOB)) { m.has |= SH_WOB; m.wob = wobOf(fx[WOB]); any = true; }
        if (fx.H(PIT) || fx.H(FIN)) { m.has |= SH_MUL; m.mul = js_pow(2, pit / 12); any = true; }
        if (fx.T(ARP)) { m.has |= SH_ARP; m.arpN = arpOf(fx[ARP], T, at, end, tick, &m.arpOff); any = true; }
        else if (fx.T(PBN)) { m.has |= SH_BEND; m.bendMul = js_pow(2, (double)s8(fx[PBN]) / 12); m.bendT1 = end; T.rate *= m.bendMul; any = true; }
        else if (fx.T(PVB)) { m.has |= SH_VIB; m.vibSpd = fx[PVB] >> 4; m.vibDep = fx[PVB] & 15; m.vibT1 = end; any = true; }
        if (fx.H(KIL)) { m.has |= SH_CUT; m.cut = at + fx[KIL] * tick; any = true; }
        if (fx.H(OFF)) { m.has |= SH_REL; m.rel = at + fx[OFF] * tick; any = true; }
        if (any) *push(c) = m;
      }
    }
    // TRACK LFO
    lfoEvs(c, t, t0, dur); lfoRan[t] = true;
    if (muted) { c.n = std::min(c.n, ev0); if (T.eng != 0) T.eng = 1; if (!T.mut && t < MT) { SeqEvent* e = push(c); e->type = SEV_OFF; e->t = t0; e->track = t; } P.nsc = 0; P.off = true; }
    T.mut = muted;
    const int L = (song && pid < 0) ? emptyLen() : lenOf(pid);
    // DIR: backwards, ping-pong or random steps; a pass still lasts L steps
    int dirGo = 0, dirRow = 0;   // dirGo: 0 the top (-1 in the JS), 1 the bottom (Infinity), 2 a random row (-2), 3 the row dirRow
    T.cnt++;
    if (hop >= 0) { T.step = L; T.next = hop; }
    else if (!T.dir || (hasLoop_ && phrase)) T.step++;
    else if (T.cnt >= L) { if (T.dir == 1) dirGo = 1; else if (T.dir == 3) dirGo = 2; else { dirGo = 3; dirRow = dirNext(T, T.step, L); } T.step = L; }
    else T.step = dirNext(T, T.step, L);
    if (phrase && hasLoop_ && T.next < 0 && (T.step > loopY1_ || T.step < loopY0_)) T.step = loopY0_;
    bool newRow = false;
    if (T.step >= L) {
      T.cnt = 0;
      T.step = T.next >= 0 ? T.next : 0; T.next = -1;
      if (phrase) { if (hasLoop_ && T.step >= loopY1_ + 1) T.step = loopY0_; }
      else { T.cr++;
        if (hasLoop_ && mode_ == SeqMode::Sect && (T.cr > loopY1_ || T.cr < loopY0_)) T.cr = loopY0_;
        settle(T, t); newRow = T.cr == 0; }
    }
    // the row just moved to: an empty numbered song cell counts as an empty pattern
    { const int np = phrase ? o_.phrase : T.on && !T.empty ? chainPhrase(sectId(T, t), T.cr) : -1, L2 = (song && np < 0) ? emptyLen() : lenOf(np);
      if (dirGo) T.step = dirGo == 1 ? L2 - 1 : dirGo == 2 ? (int)std::floor(rnd() * L2) : ci(dirRow, 0, L2 - 1);
      if (T.step >= L2) T.step = 0; }
    T.rel += dur; T.g++;
    if (newRow) T.r0 = T.rel;
  }
  // ONE WRAP FOR THE WHOLE SONG: every track has run off its blocks, so they all start again together
  if (song && !hasLoop_ && !once_) { bool any = false; int w = 0; double re = -kInf;
    for (int t = 0; t < kSeqTracks; t++) { const Tr& T = tr[t]; if (T.on) { any = true; break; } if (T.wr) { if (T.wr > w) w = T.wr; if (T.rel > re) re = T.rel; } }
    if (!any && w) { const auto ok = [&](int t) { return (t < ntr_ || t == MT) && (!o_.hasOnly || ((o_.only >> t) & 1)); };
      const auto has = [&](int y) { if (y < 0 || y >= kSongRows) return false; for (int t = 0; t <= MT; t++) if (ok(t) && S_.cell(y, t) >= 0) return true; return false; };
      int y = ci(w - 2, 0, 255); while (y > 0 && has(y - 1)) y--;
      for (int t = 0; t <= MT; t++) { Tr& T = tr[t]; T.on = false; T.empty = false; T.wr = 0; T.row = y; T.cr = 0; T.step = 0; T.next = -1; T.cnt = 0; T.g = 0; T.rel = re; T.r0 = re; if (ok(t) && S_.cell(y, t) >= 0) { T.on = true; settle(T, t); } } } }
  if (pdl != 0) for (int t = 0; t < kSeqTracks; t++) if (tr[t].on) tr[t].rel += pdl;   // PDL: the whole song waits
  double nx = kInf; for (int t = 0; t < kSeqTracks; t++) if (tr[t].on && tr[t].rel < nx) nx = tr[t].rel;
  // LFOs ALWAYS RUN: a track with no pattern on this song row still moves its LFOs, on the song's beat clock
  double nl = nx; if (nl == kInf) for (int t = 0; t <= MT; t++) if (lfoRan[t] && tr[t].rel < nl) nl = tr[t].rel;
  if (nl != kInf) { const double dI = base_ + nl - t0; int ldRow = -1; if (song) for (int t = 0; t < kSeqTracks; t++) if (pos[t].valid) { ldRow = pos[t].row; break; }
    for (int t = 0; t <= MT; t++) {
      if (lfoRan[t] || tr[t].on || !(t < ntr_ || t == MT)) continue;
      if (!lfoIsArr_[t] || lfo_[t].empty() || (hasHear_ && t < MT && !hearT_[t])) continue;
      if (ldRow >= 0 && t < MT && ldRow < 256 && ((S_.smute[(size_t)ldRow] >> t) & 1)) continue;
      lfoEvs(c, t, t0, dI); } }
  if (nb != bpm_ || tks_ != tks0) { SeqEvent* e = push(c); e->type = SEV_TEMPO; e->t = t0; e->v = nb; e->tks = tks_; }
  bpm_ = nb; hasGrv_ = nHasGrv; grv_[0] = ngrv[0]; grv_[1] = ngrv[1];
  time_ = base_ + (nx == kInf ? rel0 : nx);
  if (c.n > maxEvs_) maxEvs_ = c.n;
  if (out) { out->t = t0; out->n = c.n; out->tks = tks_; out->line = 60 / bpm_ / (S_.lpb ? S_.lpb : 4); out->dropped = c.dropped; }
  return c.n;
}

}  // namespace thunder
