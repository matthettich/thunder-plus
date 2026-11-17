// U30: the …waves engine: Graph (compile: Kahn's sort, poly, tails, exits), the cable sum (_gather), the voice loop (_render), VoicePool, setOnly / _cull, loadPatch with its refusals, the
// worklet's mix-down. Line for line from waves/waves-worklet.js (lines 5416 to 5960); docs/port/U30-NOTES.md lists what is decided differently and why.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>

#include "modfx/util.h"
#include "modfx/bfutil.h"
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

// ---- text ---------------------------------------------------------------------------------------------------------------------------
static int toU16(std::string_view s, uint16_t* out, int maxUnits) {
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

// ---- the patch (Graph + the module states and buffers the engine keeps in _ios / _states) --------------------------------------------------
namespace {
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
};
int weightOf(const WInfo& w) {
  const std::string_view t = w.type;
  if (t == "plaits" || t == "elements") return 64;
  if (w.flags & kWF_Effect) return 32;
  static const char* const big[] = {"spectral", "stretch", "convolve", "dline", "bbd", "spring", "plate", "lverb", "ensemble", "reverse", "gdelay", "harm", "dimension", "autotune", "vowel", "rotary",
    "sdelay", "marbles", "tides", "pdelay", "preverb", "string", "additive", "reson", "flex", "sgate", "retrig"};
  for (const char* b : big) if (t == b) return 16;
  return 1;
}
// Hostile values: a module's parameter outside its declared range crashes some modules (the fuzz found it) where the JS computes with it. Held inside the range, a NaN taking the default; in range nothing changes.
void repin(ModNode& m) {
  m.bp.resize(m.base.size());
  for (int k = 0; k < m.wi->np; k++) { const modfx::ParamDef& d = m.wi->params[k]; const double v = m.base[(size_t)k]; m.bp[(size_t)k] = v != v ? d.def : v < d.mn ? d.mn : v > d.mx ? d.mx : v; }
}
inline bool isParamPort(const std::string& p) { return p.size() >= 2 && p[0] == 'p' && p[1] == ':'; }
bool truthyJ(Json v) {
  if (v.missing() || v.isNull()) return false;
  if (v.isBool()) return v.asBool();
  if (v.isNum()) { const double d = v.asNum(); return d != 0 && d == d; }
  if (v.isStr()) return !v.asStr().empty();
  return true;
}
// a port name of any JSON type, as text that no real port has when it is not a string
std::string portText(Json v) { if (v.isStr()) return std::string(v.asStr()); return std::string("\x01") + std::to_string((int)v.type()) + (v.isNum() ? std::string(v.numText()) : std::string()); }
}  // namespace

class PatchImpl {
 public:
  PatchImpl(WavesEngine* e, double sr_, int nVoices_, Host* h) : eng(e), sr(sr_), nVoices(nVoices_), N((double)nVoices_), host(h) { seeds.host = h; seeds.armed = true; }
  ~PatchImpl() { destroyAll(); }
  WavesEngine* eng; double sr; int nVoices; double N; Host* host;
  WSeeds seeds;   // the engine's nextSeed(): a module keeps a reference to it (ChanceGate draws at its first block); armed = real draws, off while a module is being made
  std::vector<std::unique_ptr<ModNode>> mods; std::unordered_map<std::string, ModNode*> byId;
  std::vector<CableRec> cables; std::vector<ModNode*> order, tails; uint32_t ver = 0;
  int instances = 0, weight = 0; int nMods = 0, nCables = 0;
  bool hasT = false; Transport tpart; bool tTempo = false, tLpb = false, tTicks = false, tPpq = false, tStart = false, tPlaying = false;   // the doc's transport, applied at the swap
  std::vector<float> fbS = std::vector<float>(kBlock);
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
    const WInfo* wi = winfo(type); if (!wi) return true;   // unknown type: nothing, as the JS
    if (id.find('#') != std::string_view::npos || id.empty()) return false;
    const std::string sid(id);
    if (find(sid)) removeNode(sid);
    auto m = std::make_unique<ModNode>(); m->id = sid; m->wi = wi; m->def = wdef(type); m->weight = weightOf(*wi);
    m->base.resize((size_t)wi->np);
    const Json ps = o.obj("params");
    for (int k = 0; k < wi->np; k++) { Json v = ps.get(wi->params[k].name); m->base[(size_t)k] = v.isNum() ? v.asNum() : wi->params[k].def; }
    repin(*m);
    if (wi->flags & kWF_Text) { Json t = o.get("text"); if (t.isStr()) { m->text.resize(kMaxText); const int n = toU16(t.asStr(), m->text.data(), kMaxText); m->text.resize((size_t)n); m->textLen = n; } }
    if (wi->flags & kWF_Tail) { m->tailT = wparam(*wi, "time"); m->tailF = wparam(*wi, "fine"); m->tailD = wparam(*wi, "depth"); }
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
      if (fits && m.def) { seeds.armed = false; const uint32_t d0 = seeds.drawn; s.mod = m.def->make(sr, seeds); s.drawn = seeds.drawn - d0; seeds.armed = true; }
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

// ---- the engine ---------------------------------------------------------------------------------------------------------------------
static bool srOk(double sr) { return sr == sr && sr >= 8000 && sr <= 384000; }

WavesEngine::WavesEngine(double sampleRate, const EngineOptions& o, Host* host)
    : sr_(sampleRate), valid_(srOk(sampleRate)), host_(host), transport_(o.transport), quality_(o.quality), hosted_(o.hosted), voices_(o.maxVoices) {
  if (!host_) { ownHost_.reset(new Host()); host_ = ownHost_.get(); }
  if (o.hasSeed) host_->seedN = js_to_uint32(o.seed);
  std::memset(busesL_, 0, sizeof busesL_); std::memset(busesR_, 0, sizeof busesR_);
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
}
void WavesEngine::setText(std::string_view id, std::string_view text) {
  ModNode* m = p_->find(std::string(id)); if (!m || !(m->wi->flags & kWF_Text)) return;
  m->text.resize(kMaxText); const int n = toU16(text, m->text.data(), kMaxText); m->text.resize((size_t)n); m->textLen = n;
}

// loadPatch's reading of the doc: false = refused (nothing changed)
static bool readDoc(Json doc, PatchImpl& P, int nVoices, Host*) {
  (void)nVoices;
  if (!doc.isObj()) return false;
  Json mj = doc.get("modules"), cj = doc.get("cables");
  const bool hasM = !(mj.missing() || mj.isNull()), hasC = !(cj.missing() || cj.isNull());
  if ((hasM && !mj.isArr()) || (hasC && !cj.isArr())) return false;
  const size_t nm = hasM ? mj.size() : 0, nc = hasC ? cj.size() : 0;
  if (nm > (size_t)kMaxModules || nc > (size_t)kMaxCables) return false;
  P.nMods = (int)nm; P.nCables = (int)nc;
  uint32_t cm = 0;
  for (size_t i = 0; i < nm; i++) {
    Json m = mj.at(i);
    if (m.isNull()) return false;
    if (!m.isObj()) continue;
    Json idj = m.get("id"), tj = m.get("type");
    if (!tj.isStr() || !winfo(tj.asStr())) continue;
    if (!idj.isStr()) return false;
    if (P.find(std::string(idj.asStr()))) cm++;   // (a duplicate id: remove then add, two compiles)
    if (!P.addNode(idj.asStr(), tj.asStr(), m)) return false;
  }
  (void)cm;
  for (size_t i = 0; i < nc; i++) {
    Json c = cj.at(i);
    if (!c.isObj()) return false;
    Json f = c.get("from"), t = c.get("to");
    if (!f.isArr() || !t.isArr()) return false;
    CableRec r; Json a0 = f.at(0), b0 = t.at(0);
    r.a0 = a0.isStr() ? std::string(a0.asStr()) : std::string("\x01"); r.a1 = portText(f.at(1)); r.b0 = b0.isStr() ? std::string(b0.asStr()) : std::string("\x01"); r.b1 = portText(t.at(1));
    P.addCable(r, c.get("fb").isBool() && c.get("fb").asBool());
  }
  // STEREO: an older patch's right sides (stereoUpCables)
  if (!truthyJ(doc.get("st2")) && hasM && hasC) {
    std::unordered_map<std::string, std::string> type;
    for (size_t i = 0; i < nm; i++) { Json m = mj.at(i); if (m.isObj() && m.get("id").isStr()) type[std::string(m.get("id").asStr())] = m.get("type").isStr() ? std::string(m.get("type").asStr()) : std::string(); }
    auto isNew = [](const std::string& t) { return t == "vcf" || t == "vca" || t == "sccomp" || t == "blades" || t == "meta" || t == "phaser"; };
    auto has = [&](const std::string& id, bool in, const char* port) { auto it = type.find(id); if (it == type.end()) return false; const WInfo* w = winfo(it->second); if (!w) return false; return (in ? wport_in(*w, port) : wport_out(*w, port)) >= 0; };
    std::vector<std::string> taken;
    auto key = [](Json t) { std::string k; Json a = t.at(0), b = t.at(1); k += a.isStr() ? std::string(a.asStr()) : std::string("\x01"); k += "."; k += portText(b); return k; };
    for (size_t i = 0; i < nc; i++) { Json c = cj.at(i); if (c.isObj() && c.get("to").isArr()) taken.push_back(key(c.get("to"))); }
    std::vector<CableRec> add;
    for (size_t i = 0; i < nc; i++) {
      Json c = cj.at(i); Json f = c.get("from"), t = c.get("to");
      if (!f.at(1).isStr() || f.at(1).asStr() != "out" || !t.at(1).isStr() || t.at(1).asStr() != "in") continue;
      const std::string a = f.at(0).isStr() ? std::string(f.at(0).asStr()) : std::string("\x01"), b = t.at(0).isStr() ? std::string(t.at(0).asStr()) : std::string("\x01");
      const auto ta = type.find(a), tb = type.find(b);
      if (!(ta != type.end() && isNew(ta->second)) && !(tb != type.end() && isNew(tb->second))) continue;
      const std::string tk = b + ".inR";
      if (has(a, false, "outR") && has(b, true, "inR") && std::find(taken.begin(), taken.end(), tk) == taken.end()) { taken.push_back(tk); CableRec r; r.a0 = a; r.a1 = "outR"; r.b0 = b; r.b1 = "inR"; add.push_back(r); }
    }
    for (const CableRec& r : add) P.addCable(r, false);
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
  if (!readDoc(doc, *P, voices_.max, host_)) return nullptr;
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
  if (op == "add") { Json id = m.get("id"), t = m.get("type"); if (!t.isStr()) return true; if (!id.isStr()) { stats_.droppedEvents++; return false; } return addModule(id.asStr(), t.asStr(), m); }
  if (op == "remove") { Json id = m.get("id"); if (id.isStr()) removeModule(id.asStr()); return true; }
  if (op == "connect" || op == "disconnect") {
    Json a = m.get("a"), b = m.get("b"); if (!a.isArr() || !b.isArr()) { stats_.droppedEvents++; return false; }
    const std::string a0 = a.at(0).isStr() ? std::string(a.at(0).asStr()) : std::string("\x01"), b0 = b.at(0).isStr() ? std::string(b.at(0).asStr()) : std::string("\x01"), a1 = portText(a.at(1)), b1 = portText(b.at(1));
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
  ctx.tempo = transport_.tempo; ctx.lpb = transport_.lpb; ctx.ticks = transport_.ticks; ctx.playing = transport_.playing; ctx.startN = transport_.startN;
  ctx.eng = this; ctx.notes = voices_.rows.data(); ctx.nNotes = (int)voices_.rows.size(); ctx.host = host_; ctx.fail = false;
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
