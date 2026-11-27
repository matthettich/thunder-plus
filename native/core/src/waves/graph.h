// U31: THE GRAPH OF A PATCH, shared by the …waves engine (engine.cpp) and the …chains engine (chains/cengine.cpp). This is U30's code unchanged in behaviour: the part of engine.cpp that was
// private (ModNode, Slot, PatchImpl: Graph.compile with Kahn's sort, the cable sum _gather, the module slots) moved here so a second engine can run the same graph. The one change is
// a parameter: PatchImpl reads its module types from a ModTable (wavesTable() unless the engine passes another), where it called winfo() / wdef() directly.
// (U30's gates run unchanged: test_waves, the 666 cases, the page renders, the mutations, the fuzz.) Internal: nothing outside core/src/waves and core/src/chains includes this.
#ifndef THUNDER_WAVES_GRAPH_H
#define THUNDER_WAVES_GRAPH_H
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "modfx/bfutil.h"
#include "modfx/util.h"
#include "waves/waves.h"

namespace thunder {
namespace waves {

using modfx::clamp;
using modfx::i32;
using modfx::IO;

// Where a patch finds its module types: the table (ports, parameters, flags) and the factory of a type, nullptr for an unknown one.
struct ModTable { const WInfo* (*info)(std::string_view); const Def* (*def)(std::string_view); };
const ModTable& wavesTable();   // winfo / wdef (wmods.cpp)

// ---- the patch (Graph + the module states and buffers the engine keeps in _ios / _states) --------------------------------------------------
namespace detail {
struct ModNode;
struct GCable { ModNode* src = nullptr; int srcPort = -1; bool srcAudio = false; int dstIn = -1; int dstParam = -1; bool isParam = false; bool fb = false; };
struct CableRec { std::string a0, a1, b0, b1; bool fb = false; };

struct Slot {
  std::unique_ptr<Module> mod; bool armed = false; uint32_t drawn = 0;
  std::vector<float> buf; float* in[modfx::kMaxPorts] = {}; float* out[modfx::kMaxPorts] = {};
  bool asleep = false;
  std::vector<float> mb; std::vector<char> inMo;   // the modulation buffers (one per parameter, kept) and the set in use
  int moVer = -1; std::vector<double> pm; const double* p = nullptr;
};
struct ModNode {
  std::string id; const WInfo* wi = nullptr; const Def* def = nullptr;
  std::vector<uint16_t> text; int textLen = -1; bool rawP = false; std::vector<double> base, bp;   // base: the parameter values as set (the JS's inst.params); bp: the same held inside each parameter's range (what the modules get) int textLen = -1;
  bool poly = false; int idx = 0; bool inOnly = false;
  std::vector<GCable> inc; bool linked[modfx::kMaxPorts] = {}; std::vector<int> exits;
  std::vector<Slot> slots; bool slotsMade = false; bool slotsPoly = false; int weight = 1;
  int tailT = -1, tailF = -1, tailD = -1;
  // U31 (…chains): the bypass state and the patch's own bypass setting (_byp), the module's bypass number as set (_bn), the dry copy kept while bypassing
  bool bypassed = false; int fade = 0; bool fadeTo = false; double bypP = 0, bnP = 0;   // fade / fadeTo: a bypass crossfade in progress across cut blocks (live runner): samples done of the 128, and where it goes
  std::vector<float> dry;
  bool bypOk = false; int pIn = -1, pInR = -1, pOut = -1, pOutR = -1;   // bypOk: In and Out are audio ports (the JS's bypOk); the port numbers the bypass reads
};
inline int weightOf(const WInfo& w) {
  const std::string_view t = w.type;
  if (t == "plaits" || t == "elements") return 64;
  if (w.flags & kWF_Effect) return 32;
  static const char* const big[] = {"spectral", "stretch", "convolve", "dline", "bbd", "spring", "plate", "lverb", "ensemble", "reverse", "gdelay", "harm", "dimension", "autotune", "vowel", "rotary",
    "sdelay", "marbles", "tides", "pdelay", "preverb", "string", "additive", "reson", "flex", "sgate", "retrig"};
  for (const char* b : big) if (t == b) return 16;
  return 1;
}
// Parameters: the JS uses a value as set (io.p = io.base), outside its range too, so the engines here do (rawP, the default). The held-in-range copy below (a NaN taking the default) is for a host that asks for it (rawParams false).
// Modules whose value goes straight into a third-party C++ library: the library effects (Rings, Clouds, Beads, ...: the tfx set), Marbles and Tides. For a value outside the declared range the JS's wasm traps or reads out of its
// tables (Tides 2: a float -> int conversion of the shape, then the index shape * 1025; Rings: the model and note index its tables), which in native C++ is undefined behaviour (found by the ASan / UBSan fuzz at raw
// parameters). For these the value is held inside its declared range (a NaN takes the default), as U30 held every module's. Inside the range nothing changes; outside it the JS's result is a trap (silence) or garbage
// that depends on the wasm memory layout, which no native build can reproduce, and the golden "over" rows do not include these types.
inline bool heldInRange(std::string_view type) { return type == "marbles" || type == "tides" || waves_type_is_fx(type); }
inline void repin(ModNode& m) {
  m.bp.resize(m.base.size());
  if (m.rawP) { for (int k = 0; k < m.wi->np; k++) m.bp[(size_t)k] = m.base[(size_t)k]; return; }   // U31: the value as set, as the JS (io.p = io.base): no range, a NaN stays a NaN
  for (int k = 0; k < m.wi->np; k++) { const modfx::ParamDef& d = m.wi->params[k]; const double v = m.base[(size_t)k]; m.bp[(size_t)k] = v != v ? d.def : v < d.mn ? d.mn : v > d.mx ? d.mx : v; }
}
inline bool isParamPort(const std::string& p) { return p.size() >= 2 && p[0] == 'p' && p[1] == ':'; }
inline bool truthyJ(Json v) {
  if (v.missing() || v.isNull()) return false;
  if (v.isBool()) return v.asBool();
  if (v.isNum()) { const double d = v.asNum(); return d != 0 && d == d; }
  if (v.isStr()) return !v.asStr().empty();
  return true;
}
// a port name of any JSON type, as text that no real port has when it is not a string
// U31: a module id / cable end as the JS's Map holds it: a string is itself; anything else (a number, true / false, null, a missing id, an array / object) gets a key no string id can equal (the JS's Map keys are not
// coerced: 5 and "5" differ), so a patch with such ids loads and plays as it does in the JS. (Two ids that are both objects share one key here: they could never be the target of a cable in the JS either.)
inline std::string jsKey(Json v) {
  if (v.isStr()) return std::string(v.asStr());
  if (v.isNum()) { char b[40]; std::snprintf(b, sizeof b, "\x01n%.15g", v.asNum()); return b; }
  if (v.isBool()) return v.asBool() ? std::string("\x01" "btrue") : std::string("\x01" "bfalse");
  if (v.isNull()) return std::string("\x01null");
  if (v.missing()) return std::string("\x01undef");
  return std::string("\x01obj");
}
inline std::string portText(Json v) { if (v.isStr()) return std::string(v.asStr()); return std::string("\x01") + std::to_string((int)v.type()) + (v.isNum() ? std::string(v.numText()) : std::string()); }
// text
inline int toU16(std::string_view s, uint16_t* out, int maxUnits) {
  int n = 0; size_t i = 0;
  while (i < s.size() && n < maxUnits) {
    const unsigned char c = (unsigned char)s[i]; uint32_t cp; int len;
    if (c < 0x80) { cp = c; len = 1; }
    else if ((c >> 5) == 6 && i + 1 < s.size()) { cp = ((c & 31u) << 6) | ((unsigned char)s[i + 1] & 63u); len = 2; }
    else if ((c >> 4) == 14 && i + 2 < s.size()) { cp = ((c & 15u) << 12) | (((unsigned char)s[i + 1] & 63u) << 6) | ((unsigned char)s[i + 2] & 63u); len = 3; }
    else if ((c >> 3) == 30 && i + 3 < s.size()) { cp = ((c & 7u) << 18) | (((unsigned char)s[i + 1] & 63u) << 12) | (((unsigned char)s[i + 2] & 63u) << 6) | ((unsigned char)s[i + 3] & 63u); len = 4; }
    else { cp = 0xFFFD; len = 1; }
    i += (size_t)len;
    if (cp >= 0x10000) { cp -= 0x10000; out[n++] = (uint16_t)(0xD800 + (cp >> 10)); if (n < maxUnits) out[n++] = (uint16_t)(0xDC00 + (cp & 1023)); }
    else out[n++] = (uint16_t)cp;
  }
  return n;
}


// ---- Send / Receive (15 Nov, web build 633 on): an invisible cable, resolved in the compile step (srResolve of chains/chains-worklet.js and waves/waves-worklet.js, the same code in both) ----------------
// A Send takes In / In R; every Receive with the same label outputs what its Send(s) get. Nothing runs in either module: compile() replaces each cable out of a Receive by the cables into the matching Send,
// so the engine sees ordinary cables. Label: lab 1-8 is A-H; 0 is the module's own text (trimmed, not case sensitive: JS trim() / toLowerCase(), ASCII, Latin-1, Greek and Cyrillic letters here).
inline bool srWs(uint16_t c) { return (c >= 9 && c <= 13) || c == 32 || c == 0xA0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 || c == 0xFEFF; }
inline uint16_t srLower(uint16_t c) {
  if (c >= 'A' && c <= 'Z') return (uint16_t)(c + 32);
  if (c < 0xC0) return c;
  if ((c >= 0xC0 && c <= 0xDE && c != 0xD7)) return (uint16_t)(c + 32);
  if (c >= 0x100 && c <= 0x137) return (c & 1) ? c : (uint16_t)(c + 1);
  if (c >= 0x139 && c <= 0x148) return (c & 1) ? (uint16_t)(c + 1) : c;
  if (c >= 0x14A && c <= 0x177) return (c & 1) ? c : (uint16_t)(c + 1);
  if (c == 0x178) return 0xFF;
  if (c >= 0x179 && c <= 0x17E) return (c & 1) ? (uint16_t)(c + 1) : c;
  if (c >= 0x391 && c <= 0x3A9 && c != 0x3A2) return (uint16_t)(c + 32);
  if (c >= 0x410 && c <= 0x42F) return (uint16_t)(c + 32);
  if (c >= 0x400 && c <= 0x40F) return (uint16_t)(c + 80);
  return c;
}

}  // namespace detail
using namespace detail;

class PatchImpl {
 public:
  PatchImpl(WavesEngine* e, double sr_, int nVoices_, Host* h, const ModTable* t = nullptr) : eng(e), sr(sr_), nVoices(nVoices_), N((double)nVoices_), host(h), tab(t ? t : &wavesTable()) { seeds.host = h; seeds.armed = true; }
  ~PatchImpl() { destroyAll(); }
  WavesEngine* eng; double sr; int nVoices; double N; Host* host; const ModTable* tab;   // tab: where the module types come from (…waves: wavesTable(); …chains: chainsTable())
  WSeeds seeds;   // the engine's nextSeed(): a module keeps a reference to it (ChanceGate draws at its first block); armed = real draws, off while a module is being made
  std::vector<std::unique_ptr<ModNode>> mods; std::unordered_map<std::string, ModNode*> byId;
  std::vector<CableRec> cables; std::vector<ModNode*> order, tails; uint32_t ver = 0;
  int instances = 0, weight = 0; int nMods = 0, nCables = 0;
  bool hasT = false; Transport tpart; bool tTempo = false, tLpb = false, tTicks = false, tPpq = false, tStart = false, tPlaying = false;   // the doc's transport, applied at the swap
  std::vector<float> fbS = std::vector<float>(kBlock);
  // U31 (…chains): per-patch data the …chains engine computes on the control thread: the run list (focus) and the bypass numbers, by module index; chains: this patch is a …chains one
  // rawParams: a parameter is used as set (the JS: io.p = io.base), in …waves as in …chains
  // chains: uaS / uaL: the cables as undirected adjacency by module index (CSR), cmp / cmpLow: scratch of the run list's connected components; built / sized by compile() so the audio thread's prepareDerived (setParam of `track` / `_bn`) is index-only and allocation-free
  std::vector<int> uaS, uaL, cmp, cmpLow;
  bool chains = false, rawParams = true; std::vector<char> act, actTel; bool hasAct = false; std::vector<int> bn; std::vector<char> seen; std::vector<int> stk;   // act: the run list without telemetry, actTel: with it; seen / stk: scratch (sized by compile, so the audio thread allocates nothing)
  // scratch of compile()
  std::vector<int> indeg, q; std::vector<std::vector<int>> adj; std::vector<char> inOrder;
  std::vector<char> cullKeep; std::vector<int> cullStack;   // doCull's scratch, sized by compile() (the audio thread allocates nothing)

  ModNode* find(const std::string& id) const { auto it = byId.find(id); return it == byId.end() ? nullptr : it->second; }
  void destroySlot(ModNode& m, Slot& s) {
    if (s.armed && s.mod && (m.wi->flags & kWF_PlaysNotes)) static_cast<WModule*>(s.mod.get())->destroy(eng);
    s.armed = false;
  }
  void letGo() { for (auto& m : mods) for (Slot& s : m->slots) destroySlot(*m, s); }   // _reset() on the audio thread (a live swap): the destroy hooks only, nothing is freed here
  void destroyNodeSlots(ModNode& m) { for (Slot& s : m.slots) destroySlot(m, s); instances -= (int)m.slots.size(); weight -= (int)m.slots.size() * m.weight; m.slots.clear(); m.slotsMade = false; }
  void destroyAll() { for (auto& m : mods) destroyNodeSlots(*m); }

  // ---- Send / Receive: the cables the engine runs (srResolve). The key of a module's label: "A".."H" for lab 1-8, else "t:" + its trimmed, lower-cased text ("" none). ----
  bool isSr(const ModNode& m, bool recv) const { const std::string_view t = m.wi->type; return recv ? t == "recv" : t == "send"; }
  std::u16string srKey(const ModNode& m) const {
    const int k = wparam(*m.wi, "lab"); const double lv = k >= 0 && (size_t)k < m.base.size() ? m.base[(size_t)k] : 0;
    const double r = lv == lv ? std::floor(lv + 0.5) : 0;   // Math.round(inst.params.lab) || 0
    if (r >= 1 && r <= 8) return std::u16string(1, (char16_t)('A' + (int)r - 1));
    if (m.textLen < 0) return std::u16string();
    int a = 0, b = m.textLen; while (a < b && srWs(m.text[(size_t)a])) a++; while (b > a && srWs(m.text[(size_t)b - 1])) b--;
    if (a == b) return std::u16string();
    std::u16string t = u"t:"; for (int i = a; i < b; i++) t.push_back((char16_t)srLower(m.text[(size_t)i])); return t;
  }
  std::vector<CableRec> srResolve() const {
    struct E { std::vector<const CableRec*> in, inR; };
    struct Src { std::string a0, a1; bool fb; };
    std::unordered_map<std::u16string, std::vector<int>> into; std::unordered_map<std::string, int> per; std::vector<E> ents;   // label -> its Sends (index into ents), each with its own In / In R cables
    for (const CableRec& c : cables) {
      ModNode* m = find(c.b0); if (!m || !isSr(*m, false)) continue;
      const std::u16string k = srKey(*m); if (k.empty()) continue; auto& l = into[k];
      auto pe = per.find(m->id); if (pe == per.end()) { pe = per.emplace(m->id, (int)ents.size()).first; ents.emplace_back(); l.push_back(pe->second); }
      if (c.b1 == "in") ents[(size_t)pe->second].in.push_back(&c); else if (c.b1 == "inR") ents[(size_t)pe->second].inR.push_back(&c);
    }
    // What a Receive's port carries: the sources cabled into its label's Sends (In R falling back to In, Send by Send); a source that is itself a Receive is followed through, a loop of Receives cut.
    // Memoised per label and port; a result worked out while a loop was cut is not kept.
    std::unordered_map<std::u16string, std::vector<Src>> memo; int cuts = 0;
    struct F { const PatchImpl* P; std::unordered_map<std::u16string, std::vector<int>>* into; std::vector<E>* ents; std::unordered_map<std::u16string, std::vector<Src>>* memo; int* cuts;
      std::vector<Src> feeds(const std::u16string& lab, const std::string& port, std::vector<std::u16string>& seen) {
        std::u16string key = lab; key.push_back(u'.'); for (char ch : port) key.push_back((char16_t)(unsigned char)ch);
        auto hit = memo->find(key); if (hit != memo->end()) return hit->second;
        for (const std::u16string& s : seen) if (s == key) { (*cuts)++; return {}; }
        seen.push_back(key); const int before = *cuts; std::vector<Src> got; std::unordered_map<std::string, size_t> gi;
        const auto put = [&](const std::string& a0, const std::string& a1, bool fb) { const std::string k = a0 + "." + a1; auto o = gi.find(k); if (o == gi.end()) { gi[k] = got.size(); got.push_back({a0, a1, fb}); } else if (got[o->second].fb && !fb) got[o->second].fb = false; };
        auto il = into->find(lab);
        if (il != into->end()) for (int ei : il->second) {
          const E& e = (*ents)[(size_t)ei]; const std::vector<const CableRec*>& L = port == "outR" ? (e.inR.empty() ? e.in : e.inR) : port == "out" ? e.in : std::vector<const CableRec*>();
          for (const CableRec* s : L) {
            ModNode* sm = P->find(s->a0);
            if (sm && P->isSr(*sm, true)) { for (const Src& f : feeds(P->srKey(*sm), s->a1, seen)) put(f.a0, f.a1, f.fb || s->fb); }
            else put(s->a0, s->a1, s->fb);
          }
        }
        seen.pop_back(); if (*cuts == before) (*memo)[key] = got; return got;
      } };
    F fx{this, &into, &ents, &memo, &cuts};
    std::vector<CableRec> eff; std::vector<CableRec> virt; std::unordered_map<std::string, size_t> vseen;
    for (const CableRec& c : cables) {
      ModNode* sm = find(c.a0); ModNode* dm = find(c.b0);
      if (dm && isSr(*dm, false)) continue;
      if (sm && isSr(*sm, true)) {
        std::vector<std::u16string> seen;
        for (const Src& f : fx.feeds(srKey(*sm), c.a1, seen)) { const std::string k = f.a0 + "." + f.a1 + ">" + c.b0 + "." + c.b1; const bool fb = c.fb || f.fb; auto o = vseen.find(k);
          if (o == vseen.end()) { CableRec v; v.a0 = f.a0; v.a1 = f.a1; v.b0 = c.b0; v.b1 = c.b1; v.fb = fb; vseen[k] = virt.size(); virt.push_back(v); } else if (virt[o->second].fb && !fb) virt[o->second].fb = false; }
        continue;
      }
      eff.push_back(c);
    }
    std::unordered_map<std::string, std::vector<std::string>> adj; for (const CableRec& c : eff) if (!c.fb) adj[c.a0].push_back(c.b0);
    const auto reach = [&](const std::string& a, const std::string& b) {
      if (a == b) return true; std::vector<std::string> seen{a}, w{a};
      while (!w.empty()) { const std::string x = w.back(); w.pop_back(); auto it = adj.find(x); if (it == adj.end()) continue;
        for (const std::string& y : it->second) { if (y == b) return true; if (std::find(seen.begin(), seen.end(), y) == seen.end()) { seen.push_back(y); w.push_back(y); } } }
      return false; };
    for (CableRec& v : virt) {
      if (!v.fb && reach(v.b0, v.a0)) v.fb = true;
      if (!v.fb) adj[v.a0].push_back(v.b0);
      eff.push_back(v);
    }
    return eff;
  }
  // ---- Graph ----
  void compile() {
    const size_t n = mods.size();
    for (size_t i = 0; i < n; i++) { mods[i]->idx = (int)i; mods[i]->inc.clear(); for (bool& b : mods[i]->linked) b = false; mods[i]->exits.clear(); }
    indeg.assign(n, 0); adj.resize(n); for (auto& a : adj) a.clear();
    bool anySr = false; for (const auto& m : mods) if (isSr(*m, false) || isSr(*m, true)) { anySr = true; break; }
    std::vector<CableRec> resolved; if (anySr) resolved = srResolve();   // SEND / RECEIVE: the cables as the engine runs them (the drawn ones when the patch has no pair)
    for (const CableRec& c : anySr ? resolved : cables) {
      ModNode* a = find(c.a0); ModNode* b = find(c.b0); if (!a || !b) continue;
      GCable g; g.src = a; g.fb = c.fb; g.srcPort = wport_out(*a->wi, c.a1); g.srcAudio = g.srcPort >= 0 && std::string_view(a->wi->out[g.srcPort].kind) == "audio";
      if (isParamPort(c.b1)) { g.isParam = true; g.dstParam = wparam(*b->wi, std::string_view(c.b1).substr(2)); }
      else { g.dstIn = wport_in(*b->wi, c.b1); if (g.dstIn >= 0) b->linked[g.dstIn] = true; }
      b->inc.push_back(g);
      if (c.fb) continue;
      adj[(size_t)a->idx].push_back(b->idx); indeg[(size_t)b->idx]++;
    }
    q.clear(); for (size_t i = 0; i < n; i++) if (indeg[i] == 0) q.push_back((int)i);
    order.clear(); inOrder.assign(n, 0);
    for (size_t h = 0; h < q.size(); h++) {
      const int k = q[h]; order.push_back(mods[(size_t)k].get()); inOrder[(size_t)k] = 1;
      for (int nx : adj[(size_t)k]) if (--indeg[(size_t)nx] == 0) q.push_back(nx);
    }
    for (size_t i = 0; i < n; i++) if (!inOrder[i]) order.push_back(mods[i].get());
    if (chains) { act.assign(n, 1); actTel.assign(n, 1); bn.assign(n, 0); seen.assign(n, 0); stk.assign(2 * n + 2, 0);
      cmp.assign(n, -1); cmpLow.assign(n + 1, 0); uaS.assign(n + 1, 0); uaL.clear();
      for (const CableRec& c : cables) { ModNode* x = find(c.a0); ModNode* y = find(c.b0); if (!x || !y) continue; uaS[(size_t)x->idx + 1]++; uaS[(size_t)y->idx + 1]++; }
      for (size_t i = 0; i < n; i++) uaS[i + 1] += uaS[i];
      uaL.assign((size_t)uaS[n], 0); { std::vector<int>& fill = q; fill.assign(uaS.begin(), uaS.begin() + (long)n);   // (q is rebuilt below; used here as the fill cursor)
        for (const CableRec& c : cables) { ModNode* x = find(c.a0); ModNode* y = find(c.b0); if (!x || !y) continue; uaL[(size_t)fill[(size_t)x->idx]++] = y->idx; uaL[(size_t)fill[(size_t)y->idx]++] = x->idx; } }
    }
    ver++; cullKeep.assign(n, 0); cullStack.clear(); cullStack.reserve(2 * n + 2);
    for (ModNode* m : order) {
      const int s = m->wi->scope;
      if (s == 2) m->poly = true; else if (s == 0) m->poly = false;
      else { bool any = false; for (const GCable& c : m->inc) if (!c.fb && c.src && c.src->poly) { any = true; break; } m->poly = any; }
    }
    tails.clear(); for (auto& m : mods) if (m->poly && (m->wi->flags & kWF_Tail)) tails.push_back(m.get());
    for (const CableRec& c : cables) {
      ModNode* a = find(c.a0); ModNode* b = find(c.b0); if (!a || !b || !a->poly || b->poly || isParamPort(c.b1)) continue;
      const int po = wport_out(*a->wi, c.a1); if (po < 0 || std::string_view(a->wi->out[po].kind) != "audio") continue;
      a->exits.push_back(po);
    }
  }
  bool addCable(const CableRec& c, bool fb) {   // Graph.connect: returns true when it compiled
    if (!find(c.a0) || !find(c.b0)) return false;
    for (CableRec& o : cables) if (o.a0 == c.a0 && o.a1 == c.a1 && o.b0 == c.b0 && o.b1 == c.b1) { if (o.fb != fb) { o.fb = fb; compile(); return true; } return false; }
    CableRec r = c; r.fb = fb; cables.push_back(r); compile(); return true;
  }
  void dropCables(const std::string& id) { cables.erase(std::remove_if(cables.begin(), cables.end(), [&](const CableRec& c) { return c.a0 == id || c.b0 == id; }), cables.end()); }
  void removeNode(const std::string& id) {
    ModNode* m = find(id); if (m) destroyNodeSlots(*m);
    if (m) { byId.erase(id); mods.erase(std::remove_if(mods.begin(), mods.end(), [&](const std::unique_ptr<ModNode>& p) { return p.get() == m; }), mods.end()); }
    dropCables(id); compile();
  }
  // false: never (an id is any key; U31)
  bool addNode(std::string_view id, std::string_view type, Json o) {
    const WInfo* wi = tab->info(type); if (!wi) return true;   // unknown type: nothing, as the JS
    (void)0;   // (U31: the JS's Map takes any id: '' and '#' too)
    const std::string sid(id);
    if (find(sid)) removeNode(sid);
    auto m = std::make_unique<ModNode>(); m->id = sid; m->wi = wi; m->def = tab->def(type); m->weight = weightOf(*wi);
    m->base.resize((size_t)wi->np); m->rawP = rawParams && !heldInRange(wi->type);
    const Json ps = o.obj("params");
    for (int k = 0; k < wi->np; k++) { Json v = ps.get(wi->params[k].name); m->base[(size_t)k] = v.isNum() ? v.asNum() : wi->params[k].def; }
    repin(*m);
    if (wi->flags & kWF_Text) { Json t = o.get("text"); if (t.isStr()) { m->text.resize(kMaxText); const int n = toU16(t.asStr(), m->text.data(), kMaxText); m->text.resize((size_t)n); m->textLen = n; } }
    if (wi->flags & kWF_Tail) { m->tailT = wparam(*wi, "time"); m->tailF = wparam(*wi, "fine"); m->tailD = wparam(*wi, "depth"); }
    if (chains) {   // …chains: _byp / _bn are kept beside the declared parameters; an older patch's Sidechain In (src / snd) becomes a Source (sckFrom)
      { const int pi = wport_in(*wi, "in"), po = wport_out(*wi, "out"); m->pIn = pi; m->pOut = po; m->pInR = wport_in(*wi, "inR"); m->pOutR = wport_out(*wi, "outR");
        m->bypOk = pi >= 0 && po >= 0 && std::string_view(wi->in[pi].kind) == "audio" && std::string_view(wi->out[po].kind) == "audio"; if (m->bypOk) m->dry.assign(2 * kBlock, 0.f); }
      m->bypP = ps.get("_byp").isNum() ? ps.get("_byp").asNum() : 0; m->bnP = ps.get("_bn").isNum() ? ps.get("_bn").asNum() : 0;
      const int sp = wparam(*wi, "sck");
      if (sp >= 0 && !ps.get("sck").isNum()) {
        double v = 0;
        if (ps.isObj() && std::string_view(wi->type) == "scin") {
          const double sn = js_round(ps.get("snd").isNum() ? ps.get("snd").asNum() : 0), k = js_round(ps.get("src").isNum() ? ps.get("src").asNum() : 0);
          const double sn2 = sn != sn ? 0 : sn, k2 = k != k ? 0 : k;
          v = sn2 > 0 ? (sn2 < 8 ? sn2 : 8) : k2 > 0 ? (8 + k2 < 136 ? 8 + k2 : 136) : 0;
        }
        m->base[(size_t)sp] = v; repin(*m);
      }
    }
    ModNode* raw = m.get(); mods.push_back(std::move(m)); byId[sid] = raw; compile();
    return true;
  }
  // ---- slots (the objects of _ios / _states) ----
  bool buildSlots(ModNode& m, bool soft) {
    const int cnt = m.poly ? nVoices : 1; const int w = cnt * m.weight;
    const bool fits = instances + cnt <= kMaxInstances && weight + w <= kMaxWeight;
    if (!fits && !soft) return false;
    m.slots.clear(); m.slots.resize((size_t)cnt); m.slotsPoly = m.poly; m.slotsMade = true;
    const WInfo& wi = *m.wi; const int nIn = wi.nin, nOut = wi.nout;
    for (Slot& s : m.slots) {
      s.buf.assign((size_t)(nIn + nOut) * kBlock, 0.f);
      for (int k = 0; k < nIn; k++) s.in[k] = s.buf.data() + (size_t)k * kBlock;
      for (int k = 0; k < nOut; k++) s.out[k] = s.buf.data() + (size_t)(nIn + k) * kBlock;
      s.inMo.assign((size_t)wi.np, 0); s.pm.assign((size_t)wi.np, 0.0); s.p = m.bp.data();
      if (fits && m.def) { seeds.armed = false; const uint32_t d0 = seeds.drawn; s.mod = m.def->make(sr, seeds); s.drawn = seeds.drawn - d0; seeds.armed = true; if (s.mod) { s.mod->prepare(s.p); if (wi.flags & kWF_Text) s.mod->prepareText(m.text.data(), m.textLen); } }
    }
    if (fits) { instances += cnt; weight += w; } else { m.slots.shrink_to_fit(); instances += 0; }
    // instances / weight are counted for the slots that hold an object only (a soft one holds buffers)
    if (!fits) { m.weight = m.weight; }
    return true;
  }
  void ensureMb(ModNode& m) {
    for (const GCable& c : m.inc) if (c.isParam && c.dstParam >= 0) for (Slot& s : m.slots) if (s.mb.empty()) s.mb.assign((size_t)m.wi->np * kBlock, 0.f);
  }
  // after any change (_sync): a module whose poly changed loses its states; a new one gets them
  bool sync(bool soft) {
    for (auto& up : mods) {
      ModNode& m = *up;
      if (m.slotsMade && m.slotsPoly != m.poly) destroyNodeSlots(m);
      if (!m.slotsMade) { if (!buildSlots(m, soft)) return false; }
      ensureMb(m);
    }
    return true;
  }
  // ---- the cable sum ----
  void gather(ModNode& m, Slot& s, int v) {
    const WInfo& wi = *m.wi;
    for (int k = 0; k < wi.nin; k++) std::memset(s.in[k], 0, sizeof(float) * kBlock);
    if (s.moVer != (int)cables.size()) { std::fill(s.inMo.begin(), s.inMo.end(), 0); s.moVer = (int)cables.size(); }
    else for (int k = 0; k < wi.np; k++) if (s.inMo[(size_t)k]) std::memset(s.mb.data() + (size_t)k * kBlock, 0, sizeof(float) * kBlock);
    bool mods_ = false;
    for (const GCable& c : m.inc) {
      ModNode* src = c.src; if (!src) continue;
      float* dst;
      if (c.isParam) { if (c.dstParam < 0 || s.mb.empty()) continue; mods_ = true; s.inMo[(size_t)c.dstParam] = 1; dst = s.mb.data() + (size_t)c.dstParam * kBlock; }
      else { if (c.dstIn < 0) continue; dst = s.in[c.dstIn]; }
      auto outOf = [&](int vv) -> const float* { if (c.srcPort < 0) return nullptr; const size_t k = (size_t)(vv < 0 ? 0 : vv); if (k >= src->slots.size()) return nullptr; return src->slots[k].out[c.srcPort]; };
      if (c.fb) {
        if (v >= 0 && src->poly) { const float* b = outOf(v); if (b) waves_fbadd(dst, b, 1); }
        else if (v >= 0) { const float* b = outOf(-1); if (b) waves_fbadd(dst, b, c.srcAudio ? 1 / N : 1); }
        else if (src->poly) {
          float* sc = fbS.data(); std::memset(sc, 0, sizeof(float) * kBlock);
          for (int vv = 0; vv < N; vv++) { const float* b = outOf(vv); if (b) for (int i = 0; i < kBlock; i++) sc[i] = (float)((double)sc[i] + (double)b[i]); }
          waves_fbadd(dst, sc, 1);
        } else { const float* b = outOf(-1); if (b) waves_fbadd(dst, b, 1); }
      } else if (v >= 0 && src->poly) { const float* b = outOf(v); if (b) for (int i = 0; i < kBlock; i++) dst[i] = (float)((double)dst[i] + (double)b[i]); }
      else if (v >= 0) { const float* b = outOf(-1); const double g = c.srcAudio ? 1 / N : 1; if (b) for (int i = 0; i < kBlock; i++) dst[i] = (float)((double)dst[i] + (double)b[i] * g); }
      else if (src->poly) { for (int vv = 0; vv < N; vv++) { const float* b = outOf(vv); if (b) for (int i = 0; i < kBlock; i++) dst[i] = (float)((double)dst[i] + (double)b[i]); } }
      else { const float* b = outOf(-1); if (b) for (int i = 0; i < kBlock; i++) dst[i] = (float)((double)dst[i] + (double)b[i]); }
    }
    if (mods_) {
      for (int k = 0; k < wi.np; k++) s.pm[(size_t)k] = m.bp[(size_t)k];
      for (int k = 0; k < wi.np; k++) {
        if (!s.inMo[(size_t)k]) continue;
        const modfx::ParamDef& pd = wi.params[k];
        double r = m.bp[(size_t)k] + (double)s.mb[(size_t)k * kBlock] * (pd.mx - pd.mn);
        r = clamp(r, pd.mn, pd.mx); if (pd.st >= 1) r = js_round((r - pd.mn) / pd.st) * pd.st + pd.mn;
        s.pm[(size_t)k] = r;
      }
      s.p = s.pm.data();
    } else s.p = m.bp.data();
  }
  void arm(Slot& s) {
    if (s.armed) return; s.armed = true;
    if (!s.mod) return;
    const uint32_t d0 = seeds.drawn; s.mod->arm(seeds);
    if (seeds.drawn == d0 && host) for (uint32_t k = 0; k < s.drawn; k++) host->nextSeed();
  }
};

// loadPatch's reading of a document into a prepared patch (engine.cpp): false = refused. chains: also the …chains older-patch cables (Track In's right side)
bool readPatchDoc(Json doc, PatchImpl& P, bool chains);

}  // namespace waves
}  // namespace thunder
#endif
