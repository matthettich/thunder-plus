// U31: THE GRAPH OF A PATCH, shared by the …waves engine (engine.cpp) and the …chains engine (chains/cengine.cpp). This is U30's code unchanged in behaviour: the part of engine.cpp that was
// private (ModNode, Slot, PatchImpl: Graph.compile with Kahn's sort, the cable sum _gather, the module slots) moved here so a second engine can run the same graph. The one change is
// a parameter: PatchImpl reads its module types from a ModTable (wavesTable() unless the engine passes another), where it called winfo() / wdef() directly.
// (U30's gates run unchanged: test_waves, the 666 cases, the page renders, the mutations, the fuzz.) Internal: nothing outside core/src/waves and core/src/chains includes this.
#ifndef THUNDER_WAVES_GRAPH_H
#define THUNDER_WAVES_GRAPH_H
#include <algorithm>
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
  std::vector<uint16_t> text; int textLen = -1; std::vector<double> base, bp;   // base: the parameter values as set (the JS's inst.params); bp: the same held inside each parameter's range (what the modules get) int textLen = -1;
  bool poly = false; int idx = 0; bool inOnly = false;
  std::vector<GCable> inc; bool linked[modfx::kMaxPorts] = {}; std::vector<int> exits;
  std::vector<Slot> slots; bool slotsMade = false; bool slotsPoly = false; int weight = 1;
  int tailT = -1, tailF = -1, tailD = -1;
  // U31 (…chains): the bypass state and the patch's own bypass setting (_byp), the module's bypass number as set (_bn), the dry copy kept while bypassing
  bool bypassed = false; double bypP = 0, bnP = 0; std::vector<float> dry;
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
// Hostile values: a module's parameter outside its declared range crashes some modules (the fuzz found it) where the JS computes with it. Held inside the range, a NaN taking the default; in range nothing changes.
inline void repin(ModNode& m) {
  m.bp.resize(m.base.size());
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
  bool chains = false; std::vector<char> act, actTel; bool hasAct = false; std::vector<int> bn; std::vector<char> seen; std::vector<int> stk;   // act: the run list without telemetry, actTel: with it; seen / stk: scratch (sized by compile, so the audio thread allocates nothing)
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

  // ---- Graph ----
  void compile() {
    const size_t n = mods.size();
    for (size_t i = 0; i < n; i++) { mods[i]->idx = (int)i; mods[i]->inc.clear(); for (bool& b : mods[i]->linked) b = false; mods[i]->exits.clear(); }
    indeg.assign(n, 0); adj.resize(n); for (auto& a : adj) a.clear();
    for (const CableRec& c : cables) {
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
    if (chains) { act.assign(n, 1); actTel.assign(n, 1); bn.assign(n, 0); seen.assign(n, 0); stk.assign(2 * n + 2, 0); }
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
  // false: refused (a known type with an id the JS's own keys cannot hold)
  bool addNode(std::string_view id, std::string_view type, Json o) {
    const WInfo* wi = tab->info(type); if (!wi) return true;   // unknown type: nothing, as the JS
    if (id.find('#') != std::string_view::npos || id.empty()) return false;
    const std::string sid(id);
    if (find(sid)) removeNode(sid);
    auto m = std::make_unique<ModNode>(); m->id = sid; m->wi = wi; m->def = tab->def(type); m->weight = weightOf(*wi);
    m->base.resize((size_t)wi->np);
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
      if (fits && m.def) { seeds.armed = false; const uint32_t d0 = seeds.drawn; s.mod = m.def->make(sr, seeds); s.drawn = seeds.drawn - d0; seeds.armed = true; if (s.mod) s.mod->prepare(s.p); }
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
