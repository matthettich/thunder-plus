// U09: see mix.h. The specification is the JS: index.html (makePlayer's strip, applyMix, busSync, busPlan, audible, curve / trackGain /
// trimGain) and core/core-processor.js (mixEv, mixOut, mixFill). The JS model of the nodes' arithmetic this file equals bit for bit is
// native/tools/mix-dump.mjs (class Model).
#include "mix/mix.h"
#include "rt/num.h"
#include "rt/dbl.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "rt/jsmath.h"

namespace thunder {
namespace {

constexpr double kPi = 3.14159265358979323846;   // Math.PI
constexpr int kNT = kSongNT;
constexpr double kTcMix = 0.012;                 // applyMix, moApply: setTargetAtTime(v, now, 0.012)
constexpr double kTcSong = 0.004;                // mixEv: TVL / TVA / TPN / MOUT's time constant, MVOL's linear ramp

inline double clamp255(double v) { return std::max(0.0, std::min(255.0, v)); }   // Math.max(0, Math.min(255, v))

// ---- a JSON value as JavaScript reads it (for busPlan's `int(...)` and `+(...)`) -------------------------------------------------
double strToNumber(std::string_view s) {   // Number("...")
  size_t a = 0, b = s.size();
  auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
  while (a < b && ws(s[a])) a++;
  while (b > a && ws(s[b - 1])) b--;
  s = s.substr(a, b - a);
  if (s.empty()) return 0;
  if (s == "Infinity" || s == "+Infinity") return HUGE_VAL;
  if (s == "-Infinity") return -HUGE_VAL;
  if (s.size() > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X' || s[1] == 'b' || s[1] == 'B' || s[1] == 'o' || s[1] == 'O')) {
    const int base = (s[1] == 'x' || s[1] == 'X') ? 16 : (s[1] == 'b' || s[1] == 'B') ? 2 : 8; double v = 0;
    for (size_t i = 2; i < s.size(); i++) { const char c = s[i]; int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 99; if (d >= base) return std::nan(""); v = v * base + d; }
    return v;
  }
  size_t i = 0, digits = 0;
  if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++;
  while (i < s.size() && s[i] >= '0' && s[i] <= '9') { i++; digits++; }
  if (i < s.size() && s[i] == '.') { i++; while (i < s.size() && s[i] >= '0' && s[i] <= '9') { i++; digits++; } }
  if (!digits) return std::nan("");
  if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) { i++; if (i < s.size() && (s[i] == '+' || s[i] == '-')) i++; size_t e = 0; while (i < s.size() && s[i] >= '0' && s[i] <= '9') { i++; e++; } if (!e) return std::nan(""); }
  if (i != s.size()) return std::nan("");
  { double v = 0; parse_double(s, &v); return v; }
}
double toNumber(Json v) {   // +v
  switch (v.type()) {
    case JType::Null: return 0;
    case JType::Bool: return v.asBool() ? 1 : 0;
    case JType::Number: return v.asNum();
    case JType::String: return strToNumber(v.asStr());
    case JType::Array: if (v.size() == 0) return 0; if (v.size() == 1) { Json e = v.at(0); return e.isNull() ? 0 : e.isObj() ? std::nan("") : toNumber(e); } return std::nan("");
    default: return std::nan("");   // undefined, an object
  }
}
bool truthy(Json v) {
  switch (v.type()) { case JType::Missing: case JType::Null: return false; case JType::Bool: return v.asBool(); case JType::Number: { const double n = v.asNum(); return n == n && n != 0; }
    case JType::String: return !v.asStr().empty(); default: return true; }
}
// `m.params && m.params.<key>`
Json paramOf(Json m, std::string_view key) { Json p = m.get("params"); if (!truthy(p)) return p; return p.isObj() ? p.get(key) : Json(); }
// int(v, d): Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d
double intOf(Json v, double d) { const double n = toNumber(v); if (!std::isfinite(n) || v.isNull() || (v.isStr() && v.asStr().empty())) return d; return js_round(n); }
// trk(m): the module's track as 0..NT-1, or -1
int trk(Json m) { const double k = intOf(paramOf(m, "track"), 1); return k >= 1 && k <= kNT ? (int)k - 1 : -1; }
// a Map key (SameValueZero): numbers by value, strings by text, a number and its string apart
std::string keyOf(Json v, int* uniq) {
  char buf[40];
  switch (v.type()) {
    case JType::Missing: return "u";
    case JType::Null: return "z";
    case JType::Bool: return v.asBool() ? "b1" : "b0";
    case JType::Number: { double n = v.asNum(); if (n == 0) n = 0; std::snprintf(buf, sizeof buf, "n%.17g", n); return buf; }
    case JType::String: return "s" + std::string(v.asStr());
    default: std::snprintf(buf, sizeof buf, "o%d", (*uniq)++); return buf;   // an object is only ever equal to itself
  }
}
bool isType(Json m, const char* t) { Json ty = m.get("type"); return ty.isStr() && ty.asStr() == t; }
// `c.from[0]` for a truthy from (an array; a string's first character would be a key no module has, as is anything else)
Json first(Json v) { return v.isArr() ? v.at(0) : Json(); }

}  // namespace

// ---- the gain laws -----------------------------------------------------------------------------------------------------------------
double mix_curve(double v) { return js_pow(clamp255(v) / 255, 2); }
double mix_track_gain(double v) { v = clamp255(v); return v <= 224 ? js_pow(v / 224, 2) : js_pow(10, (v - 224) / (255 - 224) * 6 / 20); }
double mix_trim_gain(double v) {
  double r = js_round(v); if (!(r == r)) r = 0;                 // Math.round(+v || 0)
  v = clamp255(r);
  const double db = v >= 128 ? (v - 128) / 127 * 18 : (v - 128) / 128 * 18;
  return js_pow(10, db / 20);
}
double mix_pan(double v) { return std::max(-1.0, std::min(1.0, (v - 128) / 127)); }

// ---- busPlan -------------------------------------------------------------------------------------------------------------------------
BusPlan::BusPlan() { std::memset(feeds, 0, sizeof feeds); for (int t = 0; t < kSongNT; t++) { nFeeds[t] = 0; feedLen[t] = 0; feedMask[t] = 0; targets[t] = 0; } }

uint32_t chains_tracks(Json d) {
  uint32_t ids = 0; Json mods = d.arr("modules");
  for (size_t i = 0; i < mods.size(); i++) { Json m = mods.at(i); if (!m.isObj() || !isType(m, "chainOut")) continue; const int k = trk(m); if (k >= 0) ids |= 1u << k; }
  return ids;
}
uint32_t chains_feed(Json d, const bool* chainOn) { const uint32_t ct = chains_tracks(d); uint32_t m = 0; for (int t = 0; t < kNT; t++) if (chainOn && chainOn[t] && (ct >> t & 1u)) m |= 1u << t; return m; }

BusPlan bus_plan(Json d, const bool* chainOn) {
  BusPlan P;
  Json mods = d.arr("modules");
  const bool ok = d.isObj() && d.str("format") == "chains.patch" && d.get("format").isStr() && mods.isArr();
  if (!ok) return P;
  int uniq = 0;
  // by: id -> module, in the order ids first appear (a later module with the same id takes the first one's place)
  std::vector<std::string> order; std::map<std::string, Json> by;
  for (size_t i = 0; i < mods.size(); i++) { Json m = mods.at(i); if (!m.isObj()) continue; Json id = m.get("id"); if (id.missing() || id.isNull()) continue;
    const std::string k = keyOf(id, &uniq); if (!by.count(k)) order.push_back(k); by[k] = m; }
  std::map<std::string, std::vector<std::string>> nx;
  { Json cs = d.get("cables"); if (cs.isArr()) for (size_t i = 0; i < cs.size(); i++) { Json c = cs.at(i); if (!c.isObj() || !truthy(c.get("from")) || !truthy(c.get("to"))) continue;
      nx[keyOf(first(c.get("from")), &uniq)].push_back(keyOf(first(c.get("to")), &uniq)); } }
  const uint32_t cfeed = chains_feed(d, chainOn);
  uint32_t ownIn = 0;
  for (const std::string& k : order) { Json m = by[k]; if (isType(m, "trackIn")) { const int t = trk(m); if (t >= 0) ownIn |= 1u << t; } }
  std::vector<int> adj[kNT];
  auto reaches = [&](int a, int b) { uint32_t seen = 1u << a; std::vector<int> w{a}; while (!w.empty()) { const int x = w.back(); w.pop_back(); if (x == b) return true; for (int y : adj[x]) if (!(seen >> y & 1u)) { seen |= 1u << y; w.push_back(y); } } return false; };
  std::vector<int> feeds[kNT];
  for (const std::string& k : order) {
    Json m = by[k];
    if (!isType(m, "trackIn")) continue;
    const int t = trk(m);
    if (t < 0 || !(chainOn && chainOn[t])) continue;
    std::map<std::string, bool> seen; seen[k] = true; std::vector<std::string> w{k}; uint32_t outs = 0;
    while (!w.empty()) { const std::string id = w.back(); w.pop_back(); auto x = by.find(id); if (x != by.end() && isType(x->second, "chainOut")) { const int o = trk(x->second); if (o >= 0) outs |= 1u << o; }
      auto n = nx.find(id); if (n != nx.end()) for (const std::string& y : n->second) if (!seen.count(y)) { seen[y] = true; w.push_back(y); } }
    bool fed = false;
    for (int b = 0; b < kNT; b++) {   // [...outs].sort()
      if (!(outs >> b & 1u)) continue;
      if (b == t || (ownIn >> b & 1u) || !(cfeed >> b & 1u)) continue;   // its own out, or not a bus
      if (reaches(b, t)) { P.loop = true; continue; }
      adj[t].push_back(b); fed = true;
      feeds[b].push_back(t); P.targets[t] |= 1u << b;
    }
    if (fed && !(outs >> t & 1u)) P.pure |= 1u << t;
    { const double pre = toNumber(paramOf(m, "pre")); if (fed && pre == pre && pre != 0) P.pre |= 1u << t; }
  }
  for (int b = 0; b < kNT; b++) { std::sort(feeds[b].begin(), feeds[b].end()); P.feedLen[b] = (int)feeds[b].size(); P.nFeeds[b] = std::min(P.feedLen[b], kMixFeedMax);
    for (int j = 0; j < P.feedLen[b]; j++) { P.feedMask[b] |= 1u << feeds[b][(size_t)j]; if (j < kMixFeedMax) P.feeds[b][j] = (uint8_t)feeds[b][(size_t)j]; } }
  return P;
}

uint32_t mix_audible(const BusPlan& P, uint32_t mute, uint32_t solo) {
  uint32_t a = 0;
  for (int t = 0; t < kNT; t++) {
    const uint32_t bit = 1u << t; bool on;
    if (solo & bit) on = true; else if (mute & bit) on = false; else if (!solo) on = true;
    else on = (P.targets[t] & solo) != 0 || (P.feedMask[t] & solo) != 0;   // soloing a bus keeps its sources; soloing a source keeps its bus
    if (on) a |= bit;
  }
  return a;
}

// ---- the mixer -----------------------------------------------------------------------------------------------------------------------
Mixer::Mixer(double sampleRate, int maxBlock, double maxDelaySec) : sr_(sampleRate), dt_(1 / sampleRate), maxBlock_(maxBlock), capSec_(maxDelaySec > 0 ? maxDelaySec : 0) {
  S_.reset(new Strip[kMixTracks]);
  const size_t mb = (size_t)maxBlock;
  in_.reset(new float[kMixTracks * 2 * mb]()); post_.reset(new float[kMixTracks * 2 * mb]()); preb_.reset(new float[kMixTracks * 2 * mb]());
  c_.reset(new float[8 * mb]()); d_.reset(new double[mb]());
  for (int t = 0; t < kMixTracks; t++) { inPL_[t] = in_.get() + (size_t)(t * 2) * mb; inPR_[t] = in_.get() + (size_t)(t * 2 + 1) * mb; mono_[t] = false; order_[t] = t; }
  const int cap = (int)std::ceil(capSec_ * sampleRate);
  dly_.reset(new Dly[kMixTracks]); for (int t = 0; t < kMixTracks; t++) { dly_[t].post.init(cap, maxBlock); dly_[t].pre.init(cap, maxBlock); }
  fd_.reset(new float[(size_t)kMixTracks * 4 * mb]()); sc_.reset(new float[2 * mb]());
  for (int t = 0; t < kMixTracks; t++) { const float* p = post_.get() + (size_t)(t * 2) * mb; const float* e = preb_.get() + (size_t)(t * 2) * mb; fpL_[t] = p; fpR_[t] = p + mb; feL_[t] = e; feR_[t] = e + mb; }
  rd_.reset(new Rd[kMixTracks]); for (int t = 0; t < kMixTracks; t++) { rd_[t].ring.init(cap, maxBlock, 1); clkRd_[t] = nullptr; }
  clkLane_.reset(new float[mb]()); clkBus_.reset(new float[mb]()); clkOut_.reset(new float[(size_t)kMixTracks * mb]());
  reset();
}
Mixer::~Mixer() = default;

void Mixer::reset() {
  for (int t = 0; t < kNT; t++) { vol_[t] = 224; trim_[t] = 128; pan_[t] = 128; }
  mute_ = solo_ = 0; master_ = 255; mout_ = 255; apply();
}
void Mixer::load(const Song& S) {
  for (int t = 0; t < kNT; t++) setTrack(t, S.mix[t].vol, S.mix[t].trim, S.mix[t].pan, S.mix[t].mute, S.mix[t].solo);
  setMaster(S.master, S.mout); apply();
}
void Mixer::setTrack(int t, double vol, double trim, double pan, bool mute, bool solo) {
  if (t < 0 || t >= kNT) return;
  vol_[t] = vol; trim_[t] = trim; pan_[t] = pan; const uint32_t b = 1u << t; mute_ = mute ? mute_ | b : mute_ & ~b; solo_ = solo ? solo_ | b : solo_ & ~b;
}
void Mixer::setMaster(double master, double mout) { master_ = master; mout_ = mout; }
void Mixer::apply() {
  audible_ = forced_ ? forcedMask_ : mix_audible(plan_, mute_, solo_);
  for (int t = 0; t < kMixTracks; t++) { Strip& s = S_[t]; const Want w = want(t); s.inp.reset(w.tg); s.g.reset(w.gv); s.pre.reset(w.prv); s.pan.reset(w.pv); s.last = w; }
  out_.reset(mix_curve(master_)); mo_.reset(mix_curve(mout_));
}
void Mixer::songLanesReset() { for (int t = 0; t < kMixTracks; t++) { S_[t].hasMg = S_[t].hasMp = false; S_[t].mg.reset(1); S_[t].mp.reset(0); } hasMm_ = hasMo_ = false; mm_.reset(0); mo2_.reset(0); }

void Mixer::setPlan(const BusPlan& plan) { plan_ = plan; rebuild(); }
void Mixer::setChain(int t, MixInsertFn fn, void* user) { if (t < 0 || t >= kNT) return; chn_[t].fn = fn; chn_[t].user = user; rebuild(); }
void Mixer::setFx(int t, MixInsertFn fn, void* user) { if (t < 0 || t >= kMixTracks) return; fx_[t].fn = fn; fx_[t].user = user; }
void Mixer::setMasterFx(MixInsertFn fn, void* user) { mfx_.fn = fn; mfx_.user = user; }

// busSync: the buses that exist, who leaves the master, who is heard PRE; and an order in which every bus comes after its sources
void Mixer::rebuild() {
  busOn_ = routed_ = preSrc_ = fdlSrc_ = 0;
  for (int b = 0; b < kNT; b++) { if (!plan_.nFeeds[b] || !chn_[b].fn) continue; busOn_ |= 1u << b;
    for (int j = 0; j < plan_.nFeeds[b]; j++) { const uint32_t s = 1u << plan_.feeds[b][j]; if (plan_.pure & s) routed_ |= s; if (plan_.pre & s) preSrc_ |= s; else fdlSrc_ |= s; } }
  uint64_t done = 0; int n = 0;
  for (int pass = 0; pass < kMixTracks && n < kMixTracks; pass++)
    for (int t = 0; t < kMixTracks; t++) { if (done >> t & 1u) continue; bool ready = true;
      if (t < kNT && (busOn_ >> t & 1u)) for (int j = 0; j < plan_.nFeeds[t]; j++) if (!(done >> plan_.feeds[t][j] & 1u)) { ready = false; break; }
      if (ready) { order_[n++] = t; done |= 1ull << t; } }
  for (int t = 0; t < kMixTracks && n < kMixTracks; t++) if (!(done >> t & 1u)) order_[n++] = t;   // (a loop: the plan never has one)
}

// applyMix's values for a track
Mixer::Want Mixer::want(int t) const {
  Want w; if (t >= kNT) return w;
  const bool on = audible_ >> t & 1u;
  w.gv = on ? mix_track_gain(vol_[t]) : 0; w.pv = mix_pan(pan_[t]); w.tg = mix_trim_gain(trim_[t]); w.prv = on ? 1 : 0;
  return w;
}
// N1 review A5. A lane holds kAutoUi (16) events and a full lane drops the NEWEST one, so more than 16 moves between two blocks (a
// fader sweep across a 4096-frame block) lost the last: a MUTE could be lost for good. Now the latest wins: a lane without room for
// this move's events first drops what is booked at or after `at` (cancel: those are being superseded, and the move's own events
// replace them); if the list is still full of earlier events it is cleared and starts from the new value. The only cost is a jump
// instead of a glide in a lane that was overflowing. Returns false only if even that failed (it cannot).
bool Mixer::move(Auto& A, double v, double at, double ramp) {
  if (A.size() + 2 > A.capacity()) A.cancel(at);
  const uint32_t d0 = A.dropped;
  if (ramp < 0) A.target(v, at, kTcMix);
  else if (ramp > 0) { A.hold(at); A.lin(v, at + ramp / sr_); }
  else { A.cancel(at); A.set(v, at); }
  bool ok = A.dropped == d0;
  if (!ok) { A.cancel(-INFINITY); A.v0 = v; ok = A.set(v, at); }   // still full of earlier events: latest wins outright
  note(A);
  return ok;
}
void Mixer::refresh(double at, double ramp) {
  audible_ = forced_ ? forcedMask_ : mix_audible(plan_, mute_, solo_);
  for (int t = 0; t < kNT; t++) { Strip& s = S_[t]; const Want w = want(t); Want& l = s.last;   // applyMix; only what moved gets an event (the same curve)
    if (w.gv != l.gv && move(s.g, w.gv, at, ramp)) l.gv = w.gv;      // `last` follows only what the lane accepted
    if (w.pv != l.pv && move(s.pan, w.pv, at, ramp)) l.pv = w.pv;
    if (w.tg != l.tg && move(s.inp, w.tg, at, ramp)) l.tg = w.tg;
    if (w.prv != l.prv && move(s.pre, w.prv, at, ramp)) l.prv = w.prv; }
}

bool Mixer::set(const MixSet& c) {
  const double at = std::max(c.at < 0 ? now_ : c.at, now_);
  if (c.tr == kNT) {
    if (c.k == MixKey::Vol) { master_ = c.v; if (c.ramp < 0) { out_.cancel(at); out_.set(mix_curve(c.v), at); note(out_); } else move(out_, mix_curve(c.v), at, c.ramp); return true; }   // rtRoute: rt.out.gain.value = curve(S.master)
    if (c.k == MixKey::Out) { mout_ = c.v; move(mo_, mix_curve(c.v), at, c.ramp); return true; }                                                                                    // moApply
    return false;
  }
  if (c.tr < 0 || c.tr > kNT) return false;
  const uint32_t b = 1u << c.tr;
  switch (c.k) {
    case MixKey::Vol: vol_[c.tr] = c.v; break;
    case MixKey::Trim: trim_[c.tr] = c.v; break;
    case MixKey::Pan: pan_[c.tr] = c.v; break;
    case MixKey::Mute: mute_ = c.v != 0 ? mute_ | b : mute_ & ~b; break;
    case MixKey::Solo: solo_ = c.v != 0 ? solo_ | b : solo_ & ~b; break;
    default: return false;   // Send: the web app has no send level (a send is a cable in the chains patch, at unity; PRE / POST is its Track In's switch)
  }
  refresh(at, c.ramp);
  return true;
}

// mixEv
bool Mixer::event(const SeqEvent& e) {
  if (e.type == SEV_BEAT) return clk_.event(e, now_);
  const double at = std::max(e.t, now_);
  if (e.type == SEV_MVOL) { if (!hasMm_) { mm_.reset(0); hasMm_ = true; } mm_.hold(at); mm_.lin(mix_curve(e.v) - mix_curve(master_), at + kTcSong); note(mm_); return true; }
  if (e.type == SEV_MOUT) { if (!hasMo_) { mo2_.reset(0); hasMo_ = true; } mo2_.target(mix_curve(e.v) - mix_curve(mout_), at, kTcSong); note(mo2_); return true; }
  if (e.type != SEV_TVL && e.type != SEV_TVA && e.type != SEV_TPN) return false;
  const int t = e.track; if (t < 0 || t >= kNT) return false;
  Strip& s = S_[t];
  if (e.type == SEV_TPN) { if (!s.hasMp) { s.mp.reset(0); s.hasMp = true; } s.mp.target(mix_pan(e.v) - mix_pan(pan_[t]), at, kTcSong); note(s.mp); return true; }
  if (!s.hasMg) { s.mg.reset(1); s.hasMg = true; }
  if (e.type == SEV_TVL) s.mg.target(e.v / 255, at, kTcSong);
  else { const double fg = mix_track_gain(vol_[t]), g = mix_track_gain(e.v); s.mg.target(fg > 1e-6 ? g / fg : 0, at, kTcSong); }
  note(s.mg);
  return true;
}

uint32_t Mixer::autoDropped() const {
  uint32_t n = out_.dropped + mo_.dropped + mm_.dropped + mo2_.dropped + clk_.dropped();
  for (int t = 0; t < kMixTracks; t++) { const Strip& s = S_[t]; n += s.inp.dropped + s.g.dropped + s.pre.dropped + s.pan.dropped + s.mg.dropped + s.mp.dropped; }
  return n;
}

// mixFill: the lane's values for the block as float32 (an AudioParam's values, a worklet's output)
bool Mixer::lane(float* C, Auto& A, int N, double t0) {
  A.prune(t0); double v;
  if (A.constBlk(t0, t0 + N * dt_, &v)) { const float f = (float)v; for (int i = 0; i < N; i++) C[i] = f; return true; }
  double* D = d_.get(); A.fill(D, t0, dt_, N); for (int i = 0; i < N; i++) C[i] = (float)D[i];
  return false;
}

void Mixer::track(int t, int N, double t0) {
  Strip& s = S_[t]; const size_t mb = (size_t)maxBlock_;
  float *cI = c_.get(), *cG = cI + mb, *cP = cG + mb, *cPan = cP + mb, *cMg = cPan + mb, *cMp = cMg + mb;
  float *L = inPL_[t], *R = inPR_[t], *pL = post_.get() + (size_t)(t * 2) * mb, *pR = pL + mb, *eL = preb_.get() + (size_t)(t * 2) * mb, *eR = eL + mb;
  lane(cI, s.inp, N, t0); lane(cG, s.g, N, t0); lane(cP, s.pre, N, t0); lane(cPan, s.pan, N, t0);
  if (s.hasMg) lane(cMg, s.mg, N, t0);
  if (s.hasMp) { lane(cMp, s.mp, N, t0); for (int i = 0; i < N; i++) cPan[i] = cPan[i] + cMp[i]; }   // the pan param: its own value + the core's offset
  const bool hasChn = t < kNT && chn_[t].fn, isBus = t < kNT && (busOn_ >> t & 1u), mono = mono_[t] && !hasChn;
  if (!hasChn && !fx_[t].fn) {   // nothing in: nothing out (the same zeros the arithmetic gives)
    bool any = false; for (int i = 0; i < N && !any; i++) any = L[i] != 0 || (!mono_[t] && R[i] != 0);
    if (!any) { std::memset(pL, 0, sizeof(float) * (size_t)N); std::memset(pR, 0, sizeof(float) * (size_t)N); if (bit32(preSrc_, t)) { std::memset(eL, 0, sizeof(float) * (size_t)N); std::memset(eR, 0, sizeof(float) * (size_t)N); } return; }
  }
  // inp (TRIM)
  for (int i = 0; i < N; i++) L[i] = L[i] * cI[i];
  if (mono_[t]) std::memcpy(R, L, sizeof(float) * (size_t)N); else for (int i = 0; i < N; i++) R[i] = R[i] * cI[i];
  // chains (dry 0, wet 1: the 20 ms crossfade of a live CHN switch belongs to the chains host), with the bus's sources; then the track's effects
  if (hasChn) {
    MixFeed fd[kMixFeedMax]; int nf = 0;
    if (isBus) for (int j = 0; j < plan_.nFeeds[t]; j++) { const int q = plan_.feeds[t][j]; const bool pre = plan_.pre >> q & 1u; fd[nf++] = MixFeed{q, pre, pre ? feL_[q] : fpL_[q], pre ? feR_[q] : fpR_[q]}; }
    chn_[t].fn(chn_[t].user, t, L, R, N, t0, fd, nf);
  }
  if (fx_[t].fn) fx_[t].fn(fx_[t].user, t, L, R, N, t0, nullptr, 0);
  // pre (an explicit stereo pair: mute and solo still silence it)
  if (bit32(preSrc_, t)) for (int i = 0; i < N; i++) { eL[i] = L[i] * cP[i]; eR[i] = R[i] * cP[i]; }
  // g (VOL x audible), cg (the song's TVL / TVA)
  for (int i = 0; i < N; i++) { L[i] = L[i] * cG[i]; R[i] = R[i] * cG[i]; }
  if (s.hasMg) for (int i = 0; i < N; i++) { L[i] = L[i] * cMg[i]; R[i] = R[i] * cMg[i]; }
  // p: the StereoPannerNode. Mono in: x = (pan + 1) / 2, L = in cos(x pi/2), R = in sin(x pi/2). Stereo in: pan <= 0: x = pan + 1,
  // L = inL + inR cos(x pi/2), R = inR sin(x pi/2); pan > 0: x = pan, L = inL cos(x pi/2), R = inR + inL sin(x pi/2).
  double cs = 0, sn = 0; float lastPan = 2;
  for (int i = 0; i < N; i++) {
    const float pan = std::max(-1.0f, std::min(1.0f, cPan[i]));
    if (pan != lastPan) { const double a = mono ? ((double)pan * 0.5 + 0.5) * (kPi / 2) : pan <= 0 ? ((double)pan + 1) * (kPi / 2) : (double)pan * (kPi / 2); cs = std::cos(a); sn = std::sin(a); lastPan = pan; }
    const double l = L[i], r = R[i];
    if (mono) { pL[i] = (float)(l * cs); pR[i] = (float)(l * sn); }
    else if (pan <= 0) { pL[i] = (float)(l + r * cs); pR[i] = (float)(r * sn); }
    else { pL[i] = (float)(l * cs); pR[i] = (float)(r + l * sn); }
  }
}

// ---- latency compensation (U10) ---------------------------------------------------------------------------------------------------
void Mixer::tap(const DelayRing& r, DelayParam& p, const float*& L, const float*& R, float* oL, float* oR, int N, double t0) {
  double v;
  if (p.constant(t0, t0 + N * dt_, &v)) { const double D = v * sr_; if (!(D > 1e-9)) return; r.read(D, oL, oR, N); }
  else { double* d = d_.get(); p.lane.fill(d, t0, dt_, N); for (int i = 0; i < N; i++) d[i] = d[i] * sr_; r.readVar(d, oL, oR, N); }
  L = oL; R = oR;
}
void Mixer::setDelays(const double* d, const double* f, const DelaySet& s) {
  for (int t = 0; t < kMixTracks; t++) { Dly& D = dly_[t];
    D.dl.set(d[t], now_, s.glide, s.ramp, s.idle, capSec_); D.fdl.set(f[t], now_, s.glide, s.ramp, s.idle, capSec_); D.fdlP.set(f[t], now_, s.glide, s.ramp, s.idle, capSec_); }
}
uint32_t Mixer::pdcClamped() const { uint32_t n = 0; for (int t = 0; t < kMixTracks; t++) n += dly_[t].dl.clamped + dly_[t].fdl.clamped + dly_[t].fdlP.clamped; return n; }
double Mixer::delayNow(int t, int point) const { if (t < 0 || t >= kMixTracks) return 0; const Dly& D = dly_[t]; const double v = point == 0 ? D.dl.pv : point == 1 ? D.fdl.pv : D.fdlP.pv; return v == v ? v : 0; }

void Mixer::setClockDelays(const double* c, const DelaySet& s) { for (int t = 0; t < kMixTracks; t++) rd_[t].cd.set(c[t], now_, s.glide, s.ramp, s.idle, capSec_); }
double Mixer::clockDelaySec(int t) const { if (t < 0 || t >= kMixTracks) return 0; const double v = rd_[t].cd.pv; return v == v ? v : 0; }
// process1's clock fill, then clkBus (the lane + the ConstantSource's -1, float32 adds) and each effects node's delayed copy
void Mixer::clockBlock(int N, double t0) {
  const size_t mb = (size_t)maxBlock_; float* lane = clkLane_.get(); float* bus = clkBus_.get();
  clk_.render(lane, N, t0, dt_, d_.get());
  for (int i = 0; i < N; i++) bus[i] = lane[i] + -1.0f;
  for (int t = 0; t < kMixTracks; t++) {
    if (!(t < kNT ? fx_[t].fn != nullptr : mfx_.fn != nullptr)) { clkRd_[t] = nullptr; continue; }
    Rd& R = rd_[t]; R.ring.push(bus, nullptr, N); clkRd_[t] = bus;
    double v; if (R.cd.constant(t0, t0 + N * dt_, &v)) { const double D = v * sr_; if (!(D > 1e-9)) continue; R.ring.read(D, clkOut_.get() + (size_t)t * mb, nullptr, N); }
    else { double* d = d_.get(); R.cd.lane.fill(d, t0, dt_, N); for (int i = 0; i < N; i++) d[i] = d[i] * sr_; R.ring.readVar(d, clkOut_.get() + (size_t)t * mb, nullptr, N); }
    clkRd_[t] = clkOut_.get() + (size_t)t * mb;
  }
}

void Mixer::render(float* outL, float* outR, int N, double t0) {
  if (N > maxBlock_) N = maxBlock_;
  const size_t mb = (size_t)maxBlock_;
  clockBlock(N, t0);
  for (int k = 0; k < kMixTracks; k++) {
    const int t = order_[k]; track(t, N, t0);
    // the delay points' histories take this block (always: a delay that starts later reads what played before it), and the feeds a bus will hear
    Dly& D = dly_[t]; const float *pL = post_.get() + (size_t)(t * 2) * mb, *pR = pL + mb, *eL = preb_.get() + (size_t)(t * 2) * mb, *eR = eL + mb;
    D.post.push(pL, pR, N); fpL_[t] = pL; fpR_[t] = pR; feL_[t] = eL; feR_[t] = eR;
    if (bit32(preSrc_, t)) { D.pre.push(eL, eR, N); tap(D.pre, D.fdlP, feL_[t], feR_[t], fd_.get() + (size_t)(t * 4 + 2) * mb, fd_.get() + (size_t)(t * 4 + 3) * mb, N, t0); }
    if (bit32(fdlSrc_, t)) tap(D.post, D.fdl, fpL_[t], fpR_[t], fd_.get() + (size_t)(t * 4) * mb, fd_.get() + (size_t)(t * 4 + 1) * mb, N, t0);
  }
  // the master sum (dl -> out), in track order
  for (int i = 0; i < N; i++) { outL[i] = 0; outR[i] = 0; }
  for (int t = 0; t < kMixTracks; t++) { if (t < kNT && (routed_ >> t & 1u)) continue; const float *pL = post_.get() + (size_t)(t * 2) * mb, *pR = pL + mb;
    tap(dly_[t].post, dly_[t].dl, pL, pR, sc_.get(), sc_.get() + mb, N, t0);
    for (int i = 0; i < N; i++) { outL[i] = outL[i] + pL[i]; outR[i] = outR[i] + pR[i]; } }
  // out (MASTER VOL + the song's MVOL), [master effects, mastering, limiter], mo (OUT + the song's MOUT)
  float *cA = c_.get(), *cB = cA + mb;
  lane(cA, out_, N, t0); if (hasMm_) { lane(cB, mm_, N, t0); for (int i = 0; i < N; i++) cA[i] = cA[i] + cB[i]; }
  for (int i = 0; i < N; i++) { outL[i] = outL[i] * cA[i]; outR[i] = outR[i] * cA[i]; }
  if (mfx_.fn) mfx_.fn(mfx_.user, kNT, outL, outR, N, t0, nullptr, 0);
  lane(cA, mo_, N, t0); if (hasMo_) { lane(cB, mo2_, N, t0); for (int i = 0; i < N; i++) cA[i] = cA[i] + cB[i]; }
  for (int i = 0; i < N; i++) { outL[i] = outL[i] * cA[i]; outR[i] = outR[i] * cA[i]; }
  std::memset(in_.get(), 0, sizeof(float) * (size_t)kMixTracks * 2 * mb);
}

}  // namespace thunder
