// U30: the …waves engine: Graph (compile: Kahn's sort, poly, tails, exits), the cable sum (_gather), the voice loop (_render), VoicePool, setOnly / _cull, loadPatch with its refusals, the
// worklet's mix-down. Line for line from waves/waves-worklet.js (lines 5416 to 5960); docs/port/U30-NOTES.md lists what is decided differently and why.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

#include "modfx/util.h"
#include "modfx/bfutil.h"
#include "waves/graph.h"
#include "waves/waves.h"

namespace thunder {
namespace waves {

using modfx::clamp;
using modfx::i32;
using modfx::IO;

// ---- small helpers ------------------------------------------------------------------------------------------------------------------
double waves_soft(double x) { return modfx::soft(x); }
void waves_fbadd(float* dst, const float* b, double g) {
  for (int i = 0; i < kBlock; i++) {
    double x = (double)b[i] * g;
    if (!(x > -1 && x < 1)) {
      if (x != x || x == INFINITY || x == -INFINITY) x = 0;
      else if (x < 0) x = -1 - js_tanh(-x - 1);
      else x = 1 + js_tanh(x - 1);
    }
    dst[i] = (float)((double)dst[i] + x);
  }
}
void waves_mixdown(const WavesEngine& e, float* L, float* R, int n) {
  for (int i = 0; i < n; i++) { L[i] = 0; R[i] = 0; }
  for (int k = 0; k < kBuses; k++) {
    const float* bl = e.busesL() + k * kBlock; const float* br = e.busesR() + k * kBlock;
    for (int i = 0; i < n; i++) { L[i] = (float)((double)L[i] + (double)bl[i]); R[i] = (float)((double)R[i] + (double)br[i]); }
  }
  for (int i = 0; i < n; i++) { L[i] = (float)waves_soft((double)L[i]); R[i] = (float)waves_soft((double)R[i]); }
}

// ---- VoicePool ----------------------------------------------------------------------------------------------------------------------
VoicePool::VoicePool(int m) : max(m < 0 ? 0 : m > kMaxVoices ? kMaxVoices : m) { rows.resize((size_t)max); }
int VoicePool::on(double pitch, double vel) {
  const int n = (int)rows.size();
  if (n == 0) return -1;   // (the JS throws on rows[undefined])
  int i = -1;
  for (int j = 0; j < n; j++) if (rows[(size_t)j].gate == 0 && rows[(size_t)j].pitch == pitch) { i = j; break; }
  if (i < 0) { int best = -1; for (int j = 0; j < n; j++) if (rows[(size_t)j].gate == 0 && (best < 0 || rows[(size_t)j].age < rows[(size_t)best].age)) best = j; i = best; }
  if (i < 0) { int a = 0; for (int j = 0; j < n; j++) if (rows[(size_t)j].age < rows[(size_t)a].age) a = j; i = a; }
  VoiceRow& r = rows[(size_t)i]; r.pitch = pitch; r.gate = 1; r.vel = vel; r.age = ++clock; return i;
}
void VoicePool::off(double pitch) { for (VoiceRow& r : rows) if (r.gate != 0 && r.pitch == pitch) { r.gate = 0; r.offAt = now; } }
void VoicePool::allOff() { for (VoiceRow& r : rows) if (r.gate != 0) { r.gate = 0; r.offAt = now; } }

// ---- the patch (Graph + the module states and buffers the engine keeps in _ios / _states): graph.h (U31: moved there so the …chains engine can run the same graph) ----
// ---- the engine ---------------------------------------------------------------------------------------------------------------------
static bool srOk(double sr) { return sr == sr && sr >= 8000 && sr <= 384000; }

WavesEngine::WavesEngine(double sampleRate, const EngineOptions& o, Host* host)
    : sr_(sampleRate), valid_(srOk(sampleRate)), host_(host), transport_(o.transport), quality_(o.quality), hosted_(o.hosted), voices_(o.maxVoices) {
  if (!host_) { ownHost_.reset(new Host()); host_ = ownHost_.get(); }
  if (o.hasSeed) host_->seedN = js_to_uint32(o.seed);
  std::memset(busesL_, 0, sizeof busesL_); std::memset(busesR_, 0, sizeof busesR_);
  for (float& k : knobs_) k = -1.f;
  p_.reset(new PatchImpl(this, valid_ ? sr_ : 48000, voices_.max, host_));
}
WavesEngine::~WavesEngine() { p_.reset(); }
void waves_free_patch(PatchImpl* p) { delete p; }
void WavesEngine::dispose() { p_.reset(new PatchImpl(this, valid_ ? sr_ : 48000, voices_.max, host_)); cullDirty_ = true; }

bool WavesEngine::addModule(std::string_view id, std::string_view type, Json o) {
  if (!valid_) return false;
  const WInfo* wi = winfo(type); if (!wi) return true;
  const int need = (wi->scope == 0 ? 1 : voices_.max);
  if (p_->instances + need > kMaxInstances || (int)p_->mods.size() >= kMaxModules) { stats_.refusedCalls++; return false; }
  if (!p_->addNode(id, type, o)) { stats_.refusedCalls++; return false; }
  p_->sync(true); cullDirty_ = true; return true;
}
void WavesEngine::removeModule(std::string_view id) { p_->removeNode(std::string(id)); p_->sync(true); cullDirty_ = true; }
void WavesEngine::connect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort, bool fb) {
  if ((int)p_->cables.size() >= kMaxCables) { stats_.refusedCalls++; return; }
  CableRec c; c.a0 = aId; c.a1 = aPort; c.b0 = bId; c.b1 = bPort; p_->addCable(c, fb); p_->sync(true); cullDirty_ = true;
}
void WavesEngine::disconnect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort) {
  const std::string a0(aId), a1(aPort), b0(bId), b1(bPort);
  auto& cs = p_->cables; cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const CableRec& c) { return c.a0 == a0 && c.a1 == a1 && c.b0 == b0 && c.b1 == b1; }), cs.end());
  p_->compile(); p_->sync(true); cullDirty_ = true;
}
void WavesEngine::setParam(std::string_view id, std::string_view name, double v) {
  ModNode* m = p_->find(std::string(id)); if (!m) return;
  const int k = wparam(*m->wi, name); if (k >= 0) { m->base[(size_t)k] = v; repin(*m); }
  if (name == "slot") cullDirty_ = true;
  if (name == "lab" && (m->wi->type == std::string_view("send") || m->wi->type == std::string_view("recv"))) { p_->compile(); p_->sync(true); cullDirty_ = true; }   // SEND / RECEIVE: another label, another cable set
}
void WavesEngine::setText(std::string_view id, std::string_view text) {
  ModNode* m = p_->find(std::string(id)); if (!m || !(m->wi->flags & kWF_Text)) return;
  m->text.resize(kMaxText); const int n = toU16(text, m->text.data(), kMaxText); m->text.resize((size_t)n); m->textLen = n;
  if (m->wi->type == std::string_view("send") || m->wi->type == std::string_view("recv")) { p_->compile(); p_->sync(true); cullDirty_ = true; }   // SEND / RECEIVE: the text is the label (lab 0)
}

// loadPatch's reading of the doc: false = refused (nothing changed). U31: the JS's own reading, odd documents included (a document that is not an object is an empty patch; `modules` / `cables` that are falsy are empty,
// a string iterates its characters; a module whose id is not a string has a key of its own; a cable end is indexed as the JS does (an array, a string's characters, a number's undefined); a cable whose `from` module is
// not there is skipped before its `to` is looked at; the cable shapes at which the JS THROWS (null / number / missing ends, a cable that is not an object, a null module) refuse the patch here.
namespace {
struct JV { char k = 'u'; std::string s; double d = 0; };   // a JS value as far as ids and ports go: u undefined, n null, b bool, d number, s string, o object / array
JV jvOf(Json v) { JV r; if (v.isStr()) { r.k = 's'; r.s = std::string(v.asStr()); } else if (v.isNum()) { char b[40]; std::snprintf(b, sizeof b, "%.15g", v.asNum()); r.k = 'd'; r.s = b; r.d = v.asNum(); } else if (v.isBool()) { r.k = 'b'; r.s = v.asBool() ? "true" : "false"; } else if (v.isNull()) { r.k = 'n'; r.s = "null"; } else if (v.isArr() || v.isObj()) { r.k = 'o'; r.s = "[object Object]"; } else r.s = "undefined"; return r; }
// a Map key as the JS has it (SameValueZero): a number by its exact value (-0 is 0; 15 digits would join two numbers that differ), an object / array by identity (each one is its own: a cable's `[1]` is never the module's `[1]`)
thread_local uint64_t tlObjSeq = 0;
std::string keyOf(const JV& v) {
  if (v.k == 's') return v.s;
  if (v.k == 'd') { char b[48]; std::snprintf(b, sizeof b, "\x01n%.17g", v.d == 0 ? 0.0 : v.d); return b; }
  if (v.k == 'b') return "\x01" "b" + v.s;
  if (v.k == 'n') return "\x01null";
  if (v.k == 'o') return "\x01obj" + std::to_string(++tlObjSeq);
  return "\x01undef";
}
// a module `type` as the JS indexes MODULES with it: a string is itself, an array of one element is that element's text (String(["a"]) is "a"); anything else is no type. false: the type is a property every object has
// (constructor, toString, __proto__, ...): MODULES[type] is a function / object with no `params`, and the JS throws in addModule
bool typeOf(Json m, std::string& out, bool& throws, bool* wrapped = nullptr) {
  Json t = m.get("type"); if (wrapped) *wrapped = t.isArr(); while (t.isArr() && t.size() == 1) t = t.at(0);
  throws = false; out.clear(); if (!t.isStr()) return false; out = std::string(t.asStr());
  static const char* const kProto[] = {"constructor", "__proto__", "__defineGetter__", "__defineSetter__", "__lookupGetter__", "__lookupSetter__", "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable", "toString", "toLocaleString", "valueOf"};
  for (const char* n : kProto) if (out == n) { throws = true; return false; }
  return true;
}
std::string portOf(const JV& v) { if (v.k == 's') return v.s; return std::string("\x01") + v.k + v.s; }
// v[i] in JS: false when it throws (v null / undefined)
bool jvIdx(Json v, int i, JV& out) {
  if (v.isNull() || v.missing()) return false;
  if (v.isArr()) { out = jvOf(v.at((size_t)i)); return true; }
  if (v.isStr()) { std::string_view t = v.asStr(); out = JV(); if ((size_t)i < t.size() && (unsigned char)t[(size_t)i] < 0x80) { out.k = 's'; out.s = std::string(1, t[(size_t)i]); } else if ((size_t)i < t.size()) { out.k = 'o'; out.s = "?"; } else out.s = "undefined"; return true; }
  if (v.isObj()) { out = jvOf(v.get(std::to_string(i))); return true; }
  out = JV(); out.s = "undefined"; return true;   // a number, true / false: no such element
}
std::string idOfModule(Json m) { return m.isObj() ? keyOf(jvOf(m.get("id"))) : std::string("\x01undef"); }
bool truthyJs(Json v) { if (v.missing() || v.isNull()) return false; if (v.isBool()) return v.asBool(); if (v.isNum()) { const double d = v.asNum(); return d == d && d != 0; } if (v.isStr()) return !v.asStr().empty(); return true; }
}  // namespace
bool readPatchDoc(Json doc, PatchImpl& P, bool chains) {
  P.chains = chains;
  if (doc.isNull() || doc.missing()) return false;   // (the JS throws on `doc.modules`)
  const bool isObj = doc.isObj();
  Json mj = isObj ? doc.get("modules") : Json(), cj = isObj ? doc.get("cables") : Json();
  size_t nm = 0, nc = 0; const bool hasM = mj.isArr(), hasC = cj.isArr();
  if (truthyJs(mj)) { if (mj.isArr()) nm = mj.size(); else if (!mj.isStr()) return false; }   // a string: its characters are not modules; a number / true / object: not iterable
  if (truthyJs(cj)) { if (cj.isArr()) nc = cj.size(); else return false; }   // (a non-empty string: its first character is a cable that is not an object: the JS throws)
  if (nm > (size_t)kMaxModules || nc > (size_t)kMaxCables) return false;
  P.nMods = (int)nm; P.nCables = (int)nc;
  for (size_t i = 0; i < nm; i++) {
    Json m = mj.at(i);
    if (m.isNull()) return false;   // m.id of null
    if (!m.isObj()) continue;
    std::string tn; bool throws = false; const bool isT = typeOf(m, tn, throws);
    if (throws) return false;   // MODULES["constructor"] and the like: the JS throws
    if (!isT || !P.tab->info(tn)) continue;
    if (!P.addNode(idOfModule(m), tn, m)) return false;
  }
  for (size_t i = 0; i < nc; i++) {
    Json c = cj.at(i);
    if (!c.isObj()) return false;   // null / a number / a string / an array: c.from is undefined or c is null: the JS throws in connect
    Json f = c.get("from"), t = c.get("to");
    JV a0, b0, a1, b1;
    if (!jvIdx(f, 0, a0)) return false;
    CableRec r; r.a0 = keyOf(a0);
    if (!P.find(r.a0)) continue;   // Graph.connect: `!this.mods.has(a[0]) || !this.mods.has(b[0])`: returns before `to` is touched
    if (!jvIdx(t, 0, b0)) return false;
    r.b0 = keyOf(b0); if (!P.find(r.b0)) continue;
    jvIdx(f, 1, a1); jvIdx(t, 1, b1); r.a1 = portOf(a1); r.b1 = portOf(b1);
    P.addCable(r, c.get("fb").isBool() && c.get("fb").asBool());
  }
  // the older patches' added cables (stereoUpCables, trackInStereoCables): both read `d.modules` / `d.cables` only when they are arrays
  const bool st2 = isObj && truthyJs(doc.get("st2")), ti2 = isObj && truthyJs(doc.get("ti2"));
  std::unordered_set<std::string> wrapped; std::unordered_map<std::string, std::string> type;   // d.modules.map(m => [m && m.id, m && m.type]): the last of equal keys wins
  if (hasM && hasC && (!st2 || (chains && !ti2))) {
    for (size_t i = 0; i < nm; i++) {
      Json m = mj.at(i); std::string k, ty;
      if (m.isObj()) { k = idOfModule(m); bool th = false, wr = false; if (!typeOf(m, ty, th, &wr)) ty.clear(); else if (wr) wrapped.insert(k); }   // (a type given as ["x"] indexes MODULES like "x", but the JS's `=== 'trackIn'` and Set.has(type) see the array)
      else if (m.isNull() || m.isBool() || m.isNum() || m.isStr()) { const bool falsy = !truthyJs(m); k = falsy ? keyOf(jvOf(m)) : std::string("\x01undef"); }
      else k = "\x01undef";
      type[k] = ty;
    }
  }
  auto endOf = [](Json c, const char* which, JV& e0, JV& e1) { Json x = c.get(which); if (!truthyJs(x)) return false; jvIdx(x, 0, e0); jvIdx(x, 1, e1); return true; };
  auto jsStr = [](const JV& v) { return v.s; };
  auto isOut = [](const JV& v) { return v.k == 's' && v.s == "out"; }; auto isIn = [](const JV& v) { return v.k == 's' && v.s == "in"; };
  auto has = [&](const std::string& id, bool in, const char* port) { auto it = type.find(id); if (it == type.end()) return false; const WInfo* w = P.tab->info(it->second); if (!w) return false; return (in ? wport_in(*w, port) : wport_out(*w, port)) >= 0; };
  auto addCableOf = [&](const JV& a, const char* ap, const JV& b, const char* bp) { CableRec r; r.a0 = keyOf(a); r.a1 = ap; r.b0 = keyOf(b); r.b1 = bp; P.addCable(r, false); };
  // STEREO (stereoUpCables)
  if (!st2 && hasM && hasC) {
    auto isNew = [](const std::string& t) { return t == "vcf" || t == "vca" || t == "sccomp" || t == "blades" || t == "meta" || t == "phaser"; };
    std::vector<std::string> taken;
    for (size_t i = 0; i < nc; i++) { Json c = cj.at(i); JV e0, e1; if (c.isObj() && endOf(c, "to", e0, e1)) taken.push_back(jsStr(e0) + "." + jsStr(e1)); }
    struct Add { JV a, b; }; std::vector<Add> add;
    for (size_t i = 0; i < nc; i++) {
      Json c = cj.at(i); JV f0, f1, t0, t1; if (!c.isObj() || !endOf(c, "from", f0, f1) || !endOf(c, "to", t0, t1)) continue;
      if (!isOut(f1) || !isIn(t1)) continue;
      const std::string a = keyOf(f0), b = keyOf(t0); const auto ta = type.find(a), tb = type.find(b);
      if (!(ta != type.end() && !wrapped.count(a) && isNew(ta->second)) && !(tb != type.end() && !wrapped.count(b) && isNew(tb->second))) continue;
      const std::string tk = jsStr(t0) + ".inR";
      if (has(a, false, "outR") && has(b, true, "inR") && std::find(taken.begin(), taken.end(), tk) == taken.end()) { taken.push_back(tk); add.push_back({f0, t0}); }
    }
    for (const Add& x : add) addCableOf(x.a, "outR", x.b, "inR");
  }
  // TRACK IN STEREO (…chains only): a Track In whose Out R goes nowhere gets Out R -> In R of every module its Out feeds, where that In R is free
  if (chains && !ti2 && hasM && hasC) {
    auto hasIn = [&](const std::string& id) { auto it = type.find(id); if (it == type.end()) return false; const WInfo* w = P.tab->info(it->second); return w && wport_in(*w, "inR") >= 0; };
    std::vector<std::string> taken, usedR;
    for (size_t i = 0; i < nc; i++) {
      Json c = cj.at(i); JV e0, e1; if (c.isObj() && endOf(c, "to", e0, e1)) taken.push_back(jsStr(e0) + "." + jsStr(e1));
      JV f0, f1; if (c.isObj() && endOf(c, "from", f0, f1) && f1.k == 's' && f1.s == "outR") usedR.push_back(keyOf(f0));
    }
    struct Add { JV a, b; }; std::vector<Add> add;
    for (size_t i = 0; i < nc; i++) {
      Json c = cj.at(i); JV f0, f1, t0, t1; if (!c.isObj() || !endOf(c, "from", f0, f1) || !endOf(c, "to", t0, t1)) continue;
      if (!isOut(f1) || !isIn(t1)) continue;
      const std::string a = keyOf(f0); auto ta = type.find(a);
      if (ta == type.end() || ta->second != "trackIn" || wrapped.count(a) || std::find(usedR.begin(), usedR.end(), a) != usedR.end()) continue;
      if (f0.k == 'n') continue;   // (the JS's usedR holds null for every cable that is not an outR one, and this cable is not: a Track In whose id is null never gets the right side)
      const std::string tk = jsStr(t0) + ".inR";
      if (hasIn(keyOf(t0)) && std::find(taken.begin(), taken.end(), tk) == taken.end()) { taken.push_back(tk); add.push_back({f0, t0}); }
    }
    for (const Add& x : add) addCableOf(x.a, "outR", x.b, "inR");
  }
  Json tr = doc.get("transport");
  if (tr.isObj()) {
    P.hasT = true;
    auto num = [&](const char* k, double& d, bool& f) { Json v = tr.get(k); if (v.isNum()) { d = v.asNum(); f = true; } };
    num("tempo", P.tpart.tempo, P.tTempo); num("lpb", P.tpart.lpb, P.tLpb); num("ticks", P.tpart.ticks, P.tTicks); num("ppq", P.tpart.ppq, P.tPpq); num("startN", P.tpart.startN, P.tStart);
    Json pl = tr.get("playing"); if (pl.isBool()) { P.tpart.playing = pl.asBool(); P.tPlaying = true; }
  }
  return true;
}
PatchImpl* WavesEngine::preparePatch(Json doc) const {
  if (!valid_) return nullptr;
  std::unique_ptr<PatchImpl> P(new PatchImpl(const_cast<WavesEngine*>(this), sr_, voices_.max, host_));
  if (!readPatchDoc(doc, *P, false)) return nullptr;
  if (!P->sync(false)) return nullptr;
  return P.release();
}
PatchImpl* WavesEngine::swapPatch(PatchImpl* n) {
  PatchImpl* old = p_.release();
  if (old) old->letGo();                      // _reset(): the sequencers let their notes go (the old patch is freed by the control thread)
  p_.reset(n); p_->eng = this; cullDirty_ = true;
  if (p_->hasT) { if (p_->tTempo) transport_.tempo = p_->tpart.tempo; if (p_->tLpb) transport_.lpb = p_->tpart.lpb; if (p_->tTicks) transport_.ticks = p_->tpart.ticks;
    if (p_->tPpq) transport_.ppq = p_->tpart.ppq; if (p_->tStart) transport_.startN = p_->tpart.startN; if (p_->tPlaying) transport_.playing = p_->tpart.playing; }
  return old;
}
bool WavesEngine::loadPatch(Json doc) {
  PatchImpl* n = preparePatch(doc);
  if (!n) { stats_.refusedPatches++; return false; }
  waves_free_patch(swapPatch(n));
  return true;
}
int WavesEngine::patchModules() const { return p_->nMods; }
int WavesEngine::patchCables() const { return p_->nCables; }

bool WavesEngine::handleMessage(Json m) {
  if (!m.isObj()) { stats_.droppedEvents++; return false; }
  const std::string_view op = m.str("op");
  auto numOf = [&](const char* k, double def, bool* ok) { Json v = m.get(k); if (v.isNum()) return v.asNum(); if (v.missing() || v.isNull()) return def; *ok = false; return def; };
  if (op == "patch") return loadPatch(m.get("value"));
  if (op == "add") { Json id = m.get("id"), t = m.get("type"); if (!t.isStr()) return true; return addModule(jsKey(id), t.asStr(), m); }
  if (op == "remove") { Json id = m.get("id"); if (id.isStr()) removeModule(id.asStr()); return true; }
  if (op == "connect" || op == "disconnect") {
    Json a = m.get("a"), b = m.get("b"); if (!a.isArr() || !b.isArr()) { stats_.droppedEvents++; return false; }
    const std::string a0 = jsKey(a.at(0)), b0 = jsKey(b.at(0)), a1 = portText(a.at(1)), b1 = portText(b.at(1));
    if (op == "connect") connect(a0, a1, b0, b1, m.get("fb").isBool() && m.get("fb").asBool()); else disconnect(a0, a1, b0, b1);
    return true;
  }
  if (op == "param") { Json v = m.get("value"); if (m.get("id").isStr() && m.get("name").isStr() && v.isNum()) setParam(m.get("id").asStr(), m.get("name").asStr(), v.asNum()); return true; }
  if (op == "text") { if (m.get("id").isStr() && m.get("value").isStr()) setText(m.get("id").asStr(), m.get("value").asStr()); return true; }
  if (op == "params") {
    Json l = m.get("list"); if (!l.isArr()) { stats_.droppedEvents++; return false; }
    for (size_t i = 0; i < l.size(); i++) { Json e = l.at(i); if (e.isArr() && e.at(0).isStr() && e.at(1).isStr() && e.at(2).isNum()) setParam(e.at(0).asStr(), e.at(1).asStr(), e.at(2).asNum()); }
    return true;
  }
  if (op == "quality") { std::string_view q = m.str("value"); quality_ = q == "full" ? Quality::Full : q == "draft" ? Quality::Draft : Quality::Other; return true; }
  if (op == "transport") {
    Json v = m.get("value"); if (!v.isObj()) return true;
    auto num = [&](const char* k, double& d) { Json x = v.get(k); if (x.isNum()) d = x.asNum(); };
    num("tempo", transport_.tempo); num("lpb", transport_.lpb); num("ticks", transport_.ticks); num("ppq", transport_.ppq); num("startN", transport_.startN);
    Json pl = v.get("playing"); if (pl.isBool()) transport_.playing = pl.asBool();
    return true;
  }
  if (op == "telemetry") { telemetryOn_ = truthyJ(m.get("value")); return true; }
  if (op == "noteOn") { bool ok = true; const double p = numOf("pitch", NAN, &ok), v = numOf("vel", 1, &ok); if (!ok || m.get("pitch").missing()) { stats_.droppedEvents++; return false; } noteOn(p, v); return true; }
  if (op == "noteOff") { bool ok = true; const double p = numOf("pitch", NAN, &ok); if (!ok) { stats_.droppedEvents++; return false; } noteOff(p); return true; }
  if (op == "panic") { allNotesOff(); return true; }
  return true;
}

// setOnly / _cull
void WavesEngine::doCull() {
  cullDirty_ = false; only_ = false; if (!onlyBus_) return;
  PatchImpl& G = *p_;
  std::vector<char>& keep = G.cullKeep; std::vector<int>& stack = G.cullStack; std::fill(keep.begin(), keep.end(), (char)0); stack.clear();
  for (auto& up : G.mods) {
    ModNode& m = *up;
    if (m.wi->flags & kWF_PlaysNotes) { keep[(size_t)m.idx] = 1; stack.push_back(m.idx); }
    if (std::string_view(m.wi->type) != "out") continue;
    for (const GCable& c : m.inc) { (void)c; }
    // `some(c => c.b[1] === 'p:slot')`: a cable into the parameter "slot" (an unknown name is a different port text, so only the real one counts)
    const int sp = wparam(*m.wi, "slot");
    for (const GCable& c : m.inc) if (c.isParam && c.dstParam == sp && sp >= 0) return;
    const double sl = m.base[(size_t)(sp < 0 ? 0 : sp)];
    if (clamp((double)i32(sl), 1, kBuses) == (double)onlyBus_) { if (!keep[(size_t)m.idx]) keep[(size_t)m.idx] = 1; stack.push_back(m.idx); }
  }
  while (!stack.empty()) { const int k = stack.back(); stack.pop_back(); for (const GCable& c : G.mods[(size_t)k]->inc) if (c.src && !keep[(size_t)c.src->idx]) { keep[(size_t)c.src->idx] = 1; stack.push_back(c.src->idx); } }
  bool exits = false, kept = false;
  for (auto& up : G.mods) { ModNode& m = *up; if (m.poly && !m.exits.empty()) { exits = true; if (keep[(size_t)m.idx]) kept = true; } }
  if (exits && !kept) return;
  for (auto& up : G.mods) up->inOnly = keep[(size_t)up->idx] != 0;
  only_ = true;
}
void WavesEngine::cullNow() { doCull(); }

// _render
void WavesEngine::render(int frames) {
  if (cullDirty_) doCull();
  PatchImpl& G = *p_; WCtx& ctx = ctx_;
  ctx.sr = sr_; ctx.block = frames; ctx.full = quality_ == Quality::Full; ctx.draft = quality_ == Quality::Draft;
  ctx.tempo = transport_.tempo != transport_.tempo ? 120 : transport_.tempo < -1e5 ? -1e5 : transport_.tempo > 1e5 ? 1e5 : transport_.tempo;   /* hostile tempo (NaN, inf, huge) held to +-1e5 BPM: a beat clock that overflows breaks the step gate's index */ ctx.lpb = transport_.lpb; ctx.ticks = transport_.ticks; ctx.playing = transport_.playing; ctx.startN = transport_.startN;
  ctx.knobs = knobs_; ctx.eng = this; ctx.notes = voices_.rows.data(); ctx.nNotes = (int)voices_.rows.size(); ctx.host = host_; ctx.fail = false;
  for (int k = 0; k < kBuses; k++) { ctx.busL[k] = busesL_ + k * kBlock; ctx.busR[k] = busesR_ + k * kBlock; }
  const bool meter = telemetryOn_, only = only_;
  voices_.now += frames;
  double tailS = 0;
  for (ModNode* t : G.tails) { const double tt = (t->base[(size_t)t->tailT] + t->base[(size_t)t->tailF] + t->base[(size_t)t->tailD]) * .001; if (tt > tailS) tailS = tt; }
  const double hold = sr_ * 12, quietFor = sr_ * (0.25 + (tailS > 0 ? (tailS < 4 ? tailS : 4) : 0));
  auto run = [&](ModNode& m, Slot& s, int v) -> bool {
    G.gather(m, s, v); G.arm(s);
    if (s.mod) {
      IO io; for (int k = 0; k < m.wi->nin; k++) { io.in[k] = s.in[k]; io.linked[k] = m.linked[k]; } for (int k = 0; k < m.wi->nout; k++) io.out[k] = s.out[k]; io.p = s.p;
      ctx.text = m.text.data(); ctx.textLen = m.textLen; ctx.voice = v < 0 ? 0 : v;
      s.mod->run(io, ctx);
      if (ctx.fail) return false;
    }
    return true;
  };
  bool ok = true;
  for (int v = 0; v < voices_.max && ok; v++) {
    ctx.voice = v;
    VoiceRow& row = voices_.rows[(size_t)v]; const bool awake = voices_.awake(v, hold);
    double peak = 0;
    for (ModNode* mp : G.order) {
      ModNode& m = *mp; if (!m.poly || (only && !m.inOnly)) continue;
      Slot& s = m.slots[(size_t)v];
      if (!awake) { if (!s.asleep) { for (int k = 0; k < m.wi->nout; k++) std::memset(s.out[k], 0, sizeof(float) * kBlock); s.asleep = true; } continue; }
      s.asleep = false;
      if (!run(m, s, v)) { ok = false; break; }
      if (!row.gate && !m.exits.empty()) for (int k : m.exits) { const float* b = s.out[k]; for (int i = 0; i < frames; i += 8) { const double a = b[i] < 0 ? -(double)b[i] : (double)b[i]; if (a > peak) peak = a; } }
    }
    if (!ok) break;
    if (awake && !row.gate) { if (peak < 1e-5) { row.quiet = row.quiet + frames; if (row.quiet >= quietFor) row.offAt = voices_.now - hold; } else row.quiet = 0; }
    else row.quiet = 0;
  }
  if (ok) {
    ctx.voice = 0;
    for (ModNode* mp : G.order) {
      ModNode& m = *mp; if (m.poly || (only && !m.inOnly)) continue;
      if ((m.wi->flags & kWF_Probe) && !meter) continue;
      if (!run(m, m.slots[0], -1)) { ok = false; break; }
    }
  }
  if (!ok) { stats_.failedBlocks++; std::memset(busesL_, 0, sizeof busesL_); std::memset(busesR_, 0, sizeof busesR_); lastFailed_ = true; } else lastFailed_ = false;
}
bool WavesEngine::process(int frames) {
  if (!valid_ || frames < 0 || frames > kBlock) { stats_.refusedCalls++; return false; }
  std::memset(busesL_, 0, sizeof busesL_); std::memset(busesR_, 0, sizeof busesR_);
  render(frames); stats_.blocks++;
  return !lastFailed_;
}

// ---- inspection ----
std::vector<std::string> WavesEngine::order() const { std::vector<std::string> r; for (ModNode* m : p_->order) r.push_back(m->id); return r; }
std::vector<Cable> WavesEngine::cables() const { std::vector<Cable> r; for (const CableRec& c : p_->cables) r.push_back(Cable{c.a0, c.a1, c.b0, c.b1, c.fb}); return r; }
bool WavesEngine::isPoly(std::string_view id) const { ModNode* m = p_->find(std::string(id)); return m && m->poly; }
uint32_t WavesEngine::graphVer() const { return p_->ver; }
std::vector<std::string> WavesEngine::onlyIds() const { std::vector<std::string> r; if (only_) for (auto& m : p_->mods) if (m->inOnly) r.push_back(m->id); std::sort(r.begin(), r.end()); return r; }
int WavesEngine::instances() const { return p_->instances; }
double WavesEngine::param(std::string_view id, std::string_view name) const {
  ModNode* m = p_->find(std::string(id)); if (!m) return NAN; const int k = wparam(*m->wi, name); return k < 0 ? NAN : m->base[(size_t)k];
}
float* WavesEngine::outBuf(std::string_view id, int voice, std::string_view port) {
  ModNode* m = p_->find(std::string(id)); if (!m) return nullptr; const int k = wport_out(*m->wi, port); const size_t v = (size_t)(voice < 0 ? 0 : voice); if (k < 0 || v >= m->slots.size()) return nullptr; return m->slots[v].out[k];
}
float* WavesEngine::inBuf(std::string_view id, int voice, std::string_view port) {
  ModNode* m = p_->find(std::string(id)); if (!m) return nullptr; const int k = wport_in(*m->wi, port); const size_t v = (size_t)(voice < 0 ? 0 : voice); if (k < 0 || v >= m->slots.size()) return nullptr; return m->slots[v].in[k];
}
void WavesEngine::gatherNow(std::string_view id, int voice) {
  ModNode* m = p_->find(std::string(id)); if (!m) return; const size_t v = (size_t)(voice < 0 ? 0 : voice); if (v >= m->slots.size()) return; p_->gather(*m, m->slots[v], voice);
}

}  // namespace waves
}  // namespace thunder
