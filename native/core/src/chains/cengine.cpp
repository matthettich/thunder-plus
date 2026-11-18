// U31: the …chains engine: ChainsEngine (chains/chains-worklet.js lines 5784 to 6077: the patch of one track's chain run as a graph, with the buses, sends, sources, bypass and the focus run list),
// ChainsProcessor's mix-down, ThunderChains' per-track wrapper (ChainsNode). The graph itself is U30's (waves/graph.h). docs/port/U31-NOTES.md lists what is decided differently and why.
#include <algorithm>
#include <cmath>
#include <cstring>

#include "chains/chains.h"
#include "modfx/bfutil.h"
#include "modfx/util.h"
#include "waves/graph.h"

namespace thunder {
namespace chains {

using modfx::clamp;
using modfx::i32;
using modfx::IO;
using waves::ModNode;
using waves::PatchImpl;
using waves::Slot;

// ---- Resample's takes: a ring from the audio thread to the control thread ---------------------------------------------------------------------------------
struct ChainsEngine::TakeQ {
  static constexpr unsigned N = 16;
  ResampleTake t[N]; std::atomic<unsigned> head{0}, tail{0};
  bool push(const ResampleTake& x) { const unsigned h = head.load(std::memory_order_relaxed), tl = tail.load(std::memory_order_acquire); if (h - tl >= N) return false; t[h % N] = x; head.store(h + 1, std::memory_order_release); return true; }
  bool pop(ResampleTake* o) { const unsigned tl = tail.load(std::memory_order_relaxed), h = head.load(std::memory_order_acquire); if (h == tl) return false; *o = t[tl % N]; tail.store(tl + 1, std::memory_order_release); return true; }
};

static bool srOk(double sr) { return sr == sr && sr >= 8000 && sr <= 384000; }
static PatchImpl* newPatch(double sr, Host* h) { PatchImpl* P = new PatchImpl(nullptr, sr, 1, h, &chainsTable()); P->chains = true; P->rawParams = true; return P; }
void chains_free_patch(PatchImpl* p) { delete p; }

ChainsEngine::ChainsEngine(double sampleRate, const EngineOptions& o, Host* host)
    : sr_(sampleRate), valid_(srOk(sampleRate)), host_(host), transport_(o.transport), quality_(o.quality), focus_(o.focus) {
  if (!host_) { ownHost_.reset(new Host()); host_ = ownHost_.get(); }
  busesL_.reset(new float[(size_t)kChannels * kBlock]()); busesR_.reset(new float[(size_t)kChannels * kBlock]());
  for (double& b : bsw_) b = -1;
  for (int16_t& t : tapMap_) t = -1;
  takes_.reset(new TakeQ());
  p_.reset(newPatch(valid_ ? sr_ : 48000, host_));
  bindCtx();
}
ChainsEngine::~ChainsEngine() { p_.reset(); }
void ChainsEngine::bindCtx() {
  CCtx& c = ctx_;
  c.sr = valid_ ? sr_ : 48000; c.key = &ChainsEngine::keyCb; c.keyUser = this; c.host = host_;
  c.busesL = busesL_.get(); c.busesR = busesR_.get(); c.bw = bw_; c.bwl = bwl_; c.nbw = &nbw_;
  for (int n = 0; n < kSends; n++) c.sendOut[n] = sendOut_ + (size_t)n * kBlock;
  c.zero = zero_; c.takeUser = this;
  c.onTake = [](void* user, const ResampleTake& t) { ChainsEngine* e = static_cast<ChainsEngine*>(user); if (!e->takes_->push(t)) resample_release(t); };
}
void ChainsEngine::dispose() { p_.reset(newPatch(valid_ ? sr_ : 48000, host_)); }

// ctx.key (SOURCE): 0 Self -> none; 1 to 8 a send; 9 and up a track's own sound (input 2, through tapMap)
const float* ChainsEngine::keyCb(void* user, double sck) {
  const ChainsEngine* e = static_cast<const ChainsEngine*>(user);
  double v = js_round(sck); if (v != v) v = 0;
  if (v <= 0) return nullptr;
  if (v > kSends + kChannels) return e->zero_;
  const int iv = (int)v;
  if (iv <= kSends) { const int n = iv - 1; if (e->sendInSet_) return e->sendInExt_[n] ? e->sendInExt_[n] : e->zero_; if (e->standalone_) return e->sendPrev_ + (size_t)n * kBlock; return e->zero_; }
  const int k = iv - kSends - 1; const int c = e->hasTapMap_ ? e->tapMap_[k] : k;
  return (c >= 0 && c < e->nTaps_ && e->taps_[c]) ? e->taps_[c] : e->zero_;
}

// ---- the run list and the bypass numbers (_active, _bnMap): functions of the graph and the parameters, worked out on the control thread, allocation-free once compile() sized the scratch ----
static int chainOutTrack(const ModNode& m) { const int k = waves::wparam(*m.wi, "track"); const double v = k >= 0 ? m.base[(size_t)k] : 0; return (int)clamp((double)(i32(v) - 1), 0, kChannels - 1); }
void ChainsEngine::prepareDerived(PatchImpl& P) const {
  const size_t n = P.mods.size();
  if (P.act.size() != n || P.actTel.size() != n || P.bn.size() != n || P.seen.size() != n || P.stk.size() < 2 * n + 2) return;
  // bypass numbers: a module's own _bn, else the lowest free one, in patch order
  { bool used[16] = {}; for (size_t i = 0; i < n; i++) { ModNode& m = *P.mods[i]; P.bn[i] = 0; if (!m.bypOk) continue; const int v = i32(m.bnP); if (v >= 1 && v <= 15 && !used[v]) { used[v] = true; P.bn[i] = v; } }
    int k = 1; for (size_t i = 0; i < n; i++) { ModNode& m = *P.mods[i]; if (!m.bypOk || P.bn[i]) continue; while (k <= 15 && used[k]) k++; if (k > 15) break; used[k] = true; P.bn[i] = k; } }
  P.hasAct = focus_ >= 0;
  for (int tel = 0; tel < 2; tel++) {
    std::vector<char>& act = tel ? P.actTel : P.act; std::fill(act.begin(), act.end(), (char)0);
    int* q = P.stk.data(); int nq = 0; int* w = P.stk.data() + n + 1;
    if (focus_ < 0) { std::fill(act.begin(), act.end(), (char)1); continue; }
    bool anyScout = false;
    for (auto& up : P.mods) { ModNode& m = *up; const std::string_view t = m.wi->type; if (t == "scout") anyScout = true; if (t == "chainOut" && chainOutTrack(m) == focus_) { act[(size_t)m.idx] = 1; q[nq++] = m.idx; } }
    // SENDS: a Sidechain Out runs in one node only: the node of the lowest track a Chain Out in its chain (the modules joined to it by cables, either way) goes to
    if (anyScout) {
      for (auto& up : P.mods) {
        ModNode& m = *up; if (std::string_view(m.wi->type) != "scout" || act[(size_t)m.idx]) continue;
        std::fill(P.seen.begin(), P.seen.end(), (char)0); int nw = 0; w[nw++] = m.idx; P.seen[(size_t)m.idx] = 1; int low = kChannels;
        while (nw) {
          const int cur = w[--nw]; const ModNode& x = *P.mods[(size_t)cur]; if (std::string_view(x.wi->type) == "chainOut") low = std::min(low, chainOutTrack(x));
          for (const waves::detail::CableRec& c : P.cables) {
            ModNode* a = P.find(c.a0); ModNode* b = P.find(c.b0); if (!a || !b) continue;
            ModNode* y = a->idx == cur ? b : b->idx == cur ? a : nullptr; if (!y || P.seen[(size_t)y->idx]) continue;
            P.seen[(size_t)y->idx] = 1; w[nw++] = y->idx;
          }
        }
        if (low == focus_) { act[(size_t)m.idx] = 1; q[nq++] = m.idx; }
      }
    }
    if (tel) for (auto& up : P.mods) { ModNode& m = *up; if ((std::string_view(m.wi->type) == "scin" || (m.wi->flags & waves::kWF_Probe)) && !act[(size_t)m.idx]) { act[(size_t)m.idx] = 1; q[nq++] = m.idx; } }
    while (nq) { const int k = q[--nq]; for (const waves::detail::GCable& c : P.mods[(size_t)k]->inc) if (c.src && !act[(size_t)c.src->idx]) { act[(size_t)c.src->idx] = 1; q[nq++] = c.src->idx; } }
  }
}

// ---- the JS's methods ---------------------------------------------------------------------------------------------------------------------------------------
bool ChainsEngine::addModule(std::string_view id, std::string_view type, Json o) {
  if (!valid_) return false;
  const WInfo* wi = cinfo(type); if (!wi) return true;
  if ((int)p_->mods.size() >= waves::kMaxModules) { stats_.refusedCalls++; return false; }
  if (!p_->addNode(id, type, o)) { stats_.refusedCalls++; return false; }
  p_->sync(true); prepareDerived(*p_); return true;
}
void ChainsEngine::removeModule(std::string_view id) { p_->removeNode(std::string(id)); p_->sync(true); prepareDerived(*p_); }
void ChainsEngine::connect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort, bool fb) {
  if ((int)p_->cables.size() >= waves::kMaxCables) { stats_.refusedCalls++; return; }
  waves::detail::CableRec c; c.a0 = aId; c.a1 = aPort; c.b0 = bId; c.b1 = bPort; p_->addCable(c, fb); p_->sync(true); prepareDerived(*p_);
}
void ChainsEngine::disconnect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort) {
  const std::string a0(aId), a1(aPort), b0(bId), b1(bPort);
  auto& cs = p_->cables; cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const waves::detail::CableRec& c) { return c.a0 == a0 && c.a1 == a1 && c.b0 == b0 && c.b1 == b1; }), cs.end());
  p_->compile(); p_->sync(true); prepareDerived(*p_);
}
void ChainsEngine::setParam(std::string_view id, std::string_view name, double v) {
  ModNode* m = nullptr; for (auto& up : p_->mods) if (up->id == id) { m = up.get(); break; }   // (a linear search: no std::string made, so this allocates nothing on the audio thread)
  if (!m) return;
  const int k = waves::wparam(*m->wi, name); if (k >= 0) { m->base[(size_t)k] = v; waves::detail::repin(*m); }
  if (name == "_byp") m->bypP = v;
  else if (name == "_bn") { m->bnP = v; prepareDerived(*p_); }
  else if (name == "track") prepareDerived(*p_);
}
void ChainsEngine::setText(std::string_view id, std::string_view text) {
  ModNode* m = p_->find(std::string(id)); if (!m || !(m->wi->flags & waves::kWF_Text)) return;
  m->text.resize(waves::kMaxText); const int n = waves::detail::toU16(text, m->text.data(), waves::kMaxText); m->text.resize((size_t)n); m->textLen = n;
}
void ChainsEngine::setTransport(const Transport& t) { const bool was = transport_.playing; transport_ = t; if (!was && transport_.playing) transport_.startN = (transport_.startN != transport_.startN || transport_.startN == 0 ? 0 : transport_.startN) + 1; }
void ChainsEngine::setTelemetry(bool on) { telemetryOn_ = on; }
void ChainsEngine::setFocus(int track) { focus_ = track; prepareDerived(*p_); }
void ChainsEngine::setTapMap(const int* tracks, int n) { for (int16_t& t : tapMap_) t = -1; for (int i = 0; i < n; i++) { const int t = tracks[i]; if (t >= 0 && t < kChannels) tapMap_[t] = (int16_t)i; } hasTapMap_ = true; }
void ChainsEngine::setBusMap(const int* tracks, int n) { busMap_.clear(); for (int i = 0; i < n; i++) if (tracks[i] >= 0 && tracks[i] < kChannels) busMap_.push_back(tracks[i]); }

bool ChainsEngine::handleMessage(Json m) {
  if (!m.isObj()) { stats_.droppedEvents++; return false; }
  const std::string_view op = m.str("op");
  if (op == "patch") return loadPatch(m.get("value"));
  if (op == "add") { Json id = m.get("id"), t = m.get("type"); if (!t.isStr()) return true; if (!id.isStr()) { stats_.droppedEvents++; return false; } return addModule(id.asStr(), t.asStr(), m); }
  if (op == "remove") { Json id = m.get("id"); if (id.isStr()) removeModule(id.asStr()); return true; }
  if (op == "connect" || op == "disconnect") {
    Json a = m.get("a"), b = m.get("b"); if (!a.isArr() || !b.isArr()) { stats_.droppedEvents++; return false; }
    const std::string a0 = a.at(0).isStr() ? std::string(a.at(0).asStr()) : std::string("\x01"), b0 = b.at(0).isStr() ? std::string(b.at(0).asStr()) : std::string("\x01"), a1 = waves::detail::portText(a.at(1)), b1 = waves::detail::portText(b.at(1));
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
    Transport t = transport_;
    auto num = [&](const char* k, double& d) { Json x = v.get(k); if (x.isNum()) d = x.asNum(); };
    num("tempo", t.tempo); num("lpb", t.lpb); num("ticks", t.ticks); num("ppq", t.ppq); num("startN", t.startN);
    Json pl = v.get("playing"); if (pl.isBool()) t.playing = pl.asBool();
    setTransport(t); return true;
  }
  if (op == "telemetry") { Json v = m.get("value"); telemetryOn_ = waves::detail::truthyJ(v) && !(v.isStr() && v.asStr() == "load"); return true; }   // true: meters and displays; 'load' and false: no run list change
  if (op == "tapmap") {
    Json v = m.get("value"); int t[kChannels * 4]; int n = 0;
    if (v.isArr()) for (size_t i = 0; i < v.size() && n < (int)(sizeof t / sizeof t[0]); i++) { Json e = v.at(i); t[n++] = e.isNum() && e.asNum() == std::floor(e.asNum()) && e.asNum() >= 0 && e.asNum() < kChannels ? (int)e.asNum() : -1; }
    // (channel i carries track t[i]: an entry that is not a track number keeps its channel and carries none)
    for (int16_t& x : tapMap_) x = -1;
    for (int i = 0; i < n; i++) if (t[i] >= 0) tapMap_[t[i]] = (int16_t)i;
    hasTapMap_ = true; return true;
  }
  if (op == "busmap") {
    Json v = m.get("value"); busMap_.clear();
    if (v.isArr()) for (size_t i = 0; i < v.size(); i++) { Json e = v.at(i); if (e.isNum() && e.asNum() >= 0 && e.asNum() < kChannels) busMap_.push_back((int)e.asNum()); }   // (a fraction is cut: the JS addresses by it)
    return true;
  }
  return true;
}

// ---- patches --------------------------------------------------------------------------------------------------------------------------------------------------
PatchImpl* ChainsEngine::preparePatch(Json doc) const {
  if (!valid_) return nullptr;
  std::unique_ptr<PatchImpl> P(newPatch(sr_, host_));
  if (!waves::readPatchDoc(doc, *P, true)) return nullptr;
  if (!P->sync(false)) return nullptr;
  prepareDerived(*P);
  return P.release();
}
PatchImpl* ChainsEngine::swapPatch(PatchImpl* n) {
  PatchImpl* old = p_.release();
  if (old) old->letGo();
  p_.reset(n);
  if (p_->hasT) {   // setTransport(doc.transport): Object.assign of the keys it has, then startN + 1 when it starts playing
    Transport t = transport_;
    if (p_->tTempo) t.tempo = p_->tpart.tempo;
    if (p_->tLpb) t.lpb = p_->tpart.lpb;
    if (p_->tTicks) t.ticks = p_->tpart.ticks;
    if (p_->tPpq) t.ppq = p_->tpart.ppq;
    if (p_->tStart) t.startN = p_->tpart.startN;
    if (p_->tPlaying) t.playing = p_->tpart.playing;
    setTransport(t);
  }
  return old;
}
bool ChainsEngine::loadPatch(Json doc) {
  PatchImpl* n = preparePatch(doc);
  if (!n) { stats_.refusedPatches++; return false; }
  chains_free_patch(swapPatch(n));
  return true;
}
int ChainsEngine::patchModules() const { return p_->nMods; }
int ChainsEngine::patchCables() const { return p_->nCables; }

void ChainsEngine::serviceSparesOf(PatchImpl* p) { if (!p) return; for (auto& m : p->mods) for (Slot& s : m->slots) if (s.mod) s.mod->service(); }
void ChainsEngine::serviceSpares() { serviceSparesOf(p_.get()); }

// ---- the inputs the host sets (setTrackInputs is inline) --------------------------------------------------------------------------------------------------------
void ChainsEngine::setSidechain(const float* L, const float* R, int n) {
  n = std::min(kBlock, n ? n : kBlock); if (n < 0) n = 0;
  std::memset(scL_, 0, sizeof scL_); std::memset(scR_, 0, sizeof scR_);
  if (L) std::memcpy(scL_, L, sizeof(float) * (size_t)n);
  if (R) std::memcpy(scR_, R, sizeof(float) * (size_t)n); else if (L) std::memcpy(scR_, L, sizeof(float) * (size_t)n);
  scOn_ = L != nullptr;
}
void ChainsEngine::setTaps(const float* const* taps, int n) { nTaps_ = 0; if (!taps) return; n = std::min(n, kMaxTaps); for (int i = 0; i < n; i++) taps_[i] = taps[i]; nTaps_ = n; }
void ChainsEngine::setSendIn(const float* const* sends, int n) { for (int k = 0; k < kSends; k++) sendInExt_[k] = (sends && k < n) ? sends[k] : nullptr; sendInSet_ = true; }
void ChainsEngine::endStandaloneBlock() { std::memcpy(sendPrev_, sendOut_, sizeof sendPrev_); standalone_ = true; }

// ---- _render --------------------------------------------------------------------------------------------------------------------------------------------------
bool ChainsEngine::render(int frames) {
  PatchImpl& G = *p_; CCtx& ctx = ctx_;
  ctx.sr = sr_; ctx.block = frames; ctx.full = quality_ == Quality::Full; ctx.draft = quality_ == Quality::Draft;
  ctx.tempo = transport_.tempo != transport_.tempo ? 120 : transport_.tempo < -1e5 ? -1e5 : transport_.tempo > 1e5 ? 1e5 : transport_.tempo;   // hostile tempo held to +-1e5 BPM (as the waves engine does)
  ctx.lpb = transport_.lpb; ctx.ticks = transport_.ticks; ctx.playing = transport_.playing; ctx.startN = transport_.startN; ctx.fail = false;
  ctx.inL = trackL_; ctx.inR = trackR_; ctx.inChans = trackChans_;
  const char* act = nullptr; if (focus_ >= 0) act = (telemetryOn_ ? G.actTel : G.act).data();
  for (ModNode* mp : G.order) {
    ModNode& m = *mp; if (act && !act[(size_t)m.idx]) continue;
    Slot& s = m.slots[0];
    G.gather(m, s, -1); G.arm(s);
    if (!s.mod) continue;
    IO io; for (int k = 0; k < m.wi->nin; k++) { io.in[k] = s.in[k]; io.linked[k] = m.linked[k]; } for (int k = 0; k < m.wi->nout; k++) io.out[k] = s.out[k]; io.p = s.p;
    ctx.text = m.text.data(); ctx.textLen = m.textLen; ctx.voice = 0; ctx.curId = m.id.c_str();
    if (m.bypOk) {   // BYPASS: the patch's own setting, or the tracker's switch for this module's number
      const int n = G.bn[(size_t)m.idx]; const double sw = n ? bsw_[n] : -1; const bool own = m.bypP >= .5;
      const bool want = sw < -.5 ? own : sw >= 1.5 ? !own : sw >= .5;
      if (want || m.bypassed) {
        const bool was = m.bypassed; float* dL = m.dry.data(); float* dR = dL + kBlock;
        const float* inL = s.in[m.pIn]; const float* inR = (m.pInR >= 0 && m.linked[m.pInR]) ? s.in[m.pInR] : inL;
        std::memcpy(dL, inL, sizeof(float) * kBlock); std::memcpy(dR, inR, sizeof(float) * kBlock);
        if (want && was) { std::memset(s.in[m.pIn], 0, sizeof(float) * kBlock); if (m.pInR >= 0) std::memset(s.in[m.pInR], 0, sizeof(float) * kBlock); }   // settled in bypass: it runs on silence
        s.mod->run(io, ctx);
        if (ctx.fail) { stats_.failedBlocks++; std::memset(busesL_.get(), 0, sizeof(float) * kChannels * kBlock); std::memset(busesR_.get(), 0, sizeof(float) * kChannels * kBlock); return false; }
        float* oL = s.out[m.pOut]; float* oR = m.pOutR >= 0 ? s.out[m.pOutR] : nullptr;
        if (want != was) for (int i = 0; i < kBlock; i++) { const double g = want ? (double)(i + 1) / kBlock : 1 - (double)(i + 1) / kBlock; oL[i] = (float)((double)oL[i] + ((double)dL[i] - (double)oL[i]) * g); if (oR) oR[i] = (float)((double)oR[i] + ((double)dR[i] - (double)oR[i]) * g); }
        else { std::memcpy(oL, dL, sizeof(float) * kBlock); if (oR) std::memcpy(oR, dR, sizeof(float) * kBlock); }
        m.bypassed = want; continue;
      }
    }
    s.mod->run(io, ctx);
    if (ctx.fail) { stats_.failedBlocks++; std::memset(busesL_.get(), 0, sizeof(float) * kChannels * kBlock); std::memset(busesR_.get(), 0, sizeof(float) * kChannels * kBlock); return false; }
  }
  return true;
}
bool ChainsEngine::process(int frames) {
  if (!valid_ || frames < 0 || frames > kBlock) { stats_.refusedCalls++; return false; }
  for (int j = 0; j < nbw_; j++) { const int k = bwl_[j]; std::memset(busesL_.get() + (size_t)k * kBlock, 0, sizeof(float) * kBlock); std::memset(busesR_.get() + (size_t)k * kBlock, 0, sizeof(float) * kBlock); bw_[k] = 0; }
  nbw_ = 0; std::memset(sendOut_, 0, sizeof sendOut_);
  if (inlineService_) serviceSparesOf(p_.get());
  const bool ok = render(frames); stats_.blocks++; lastFailed_ = !ok;
  return ok;
}
bool ChainsEngine::popTake(ResampleTake* t) { return takes_->pop(t); }
void ChainsEngine::releaseTake(const ResampleTake& t) { resample_release(t); }

// ---- inspection -----------------------------------------------------------------------------------------------------------------------------------------------
std::vector<std::string> ChainsEngine::order() const { std::vector<std::string> r; for (ModNode* m : p_->order) r.push_back(m->id); return r; }
std::vector<waves::Cable> ChainsEngine::cables() const { std::vector<waves::Cable> r; for (const auto& c : p_->cables) r.push_back(waves::Cable{c.a0, c.a1, c.b0, c.b1, c.fb}); return r; }
uint32_t ChainsEngine::graphVer() const { return p_->ver; }
int ChainsEngine::instances() const { return p_->instances; }
bool ChainsEngine::isActive(std::string_view id) const { ModNode* m = p_->find(std::string(id)); if (!m) return false; if (focus_ < 0) return true; return (telemetryOn_ ? p_->actTel : p_->act)[(size_t)m->idx] != 0; }
bool ChainsEngine::isBypassed(std::string_view id) const { ModNode* m = p_->find(std::string(id)); return m && m->bypassed; }
int ChainsEngine::bypassNumber(std::string_view id) const { ModNode* m = p_->find(std::string(id)); return m ? p_->bn[(size_t)m->idx] : 0; }
double ChainsEngine::param(std::string_view id, std::string_view name) const {
  ModNode* m = p_->find(std::string(id)); if (!m) return NAN; const int k = waves::wparam(*m->wi, name); return k < 0 ? NAN : m->base[(size_t)k];
}
float* ChainsEngine::outBuf(std::string_view id, std::string_view port) { ModNode* m = p_->find(std::string(id)); if (!m || m->slots.empty()) return nullptr; const int k = waves::wport_out(*m->wi, port); return k < 0 ? nullptr : m->slots[0].out[k]; }
float* ChainsEngine::inBuf(std::string_view id, std::string_view port) { ModNode* m = p_->find(std::string(id)); if (!m || m->slots.empty()) return nullptr; const int k = waves::wport_in(*m->wi, port); return k < 0 ? nullptr : m->slots[0].in[k]; }

// ---- ChainsProcessor's mix-down -------------------------------------------------------------------------------------------------------------------------------
void chains_mixdown(const ChainsEngine& e, float* L, float* R, int n) {
  for (int i = 0; i < n; i++) { L[i] = 0; R[i] = 0; }
  int nb = 0; const int* l = e.writtenBuses(&nb);
  for (int j = 0; j < nb; j++) {
    const float* bl = e.bus(l[j] + 1); const float* br = e.busR(l[j] + 1);
    for (int i = 0; i < n; i++) { L[i] = (float)((double)L[i] + (double)bl[i]); R[i] = (float)((double)R[i] + (double)br[i]); }
  }
  for (int i = 0; i < n; i++) { L[i] = (float)modfx::soft((double)L[i]); R[i] = (float)modfx::soft((double)R[i]); }
}

// ---- ThunderChains: one node per track ------------------------------------------------------------------------------------------------------------------------
static EngineOptions withFocus(EngineOptions o, int track) { o.focus = track; return o; }
ChainsNode::ChainsNode(double sr, int track, const EngineOptions& o, Host* host)
    : eng_(sr, withFocus(o, std::max(0, std::min(kChannels - 1, track))), host), track_(std::max(0, std::min(kChannels - 1, track))) {
  tmpL_.reset(new float[(size_t)kChannels * kBlock]()); tmpR_.reset(new float[(size_t)kChannels * kBlock]());
}
bool ChainsNode::process(const NodeIn& in, NodeOut& out, int n) {
  if (n > 4096) {   // tcChunks: fails soft, silence
    if (out.L) std::memset(out.L, 0, sizeof(float) * (size_t)n); if (out.R) std::memset(out.R, 0, sizeof(float) * (size_t)n);
    for (int k = 0; k < out.nSends; k++) if (out.sends && out.sends[k]) std::memset(out.sends[k], 0, sizeof(float) * (size_t)n);
    failedLong_++; return true;
  }
  if (n <= kBlock) return p1(in, out, n, 0, n);
  bool ok = true;
  for (int a = 0; a < n && ok; a += kBlock) ok = p1(in, out, n, a, std::min(kBlock, n - a));
  return ok;
}
bool ChainsNode::p1(const NodeIn& in, NodeOut& out, int /*n*/, int a, int len) {
  ChainsEngine& e = eng_;
  auto off = [&](const float* p) -> const float* { return p ? p + a : nullptr; };
  if (in.byp) for (int k = 1; k <= kBypSwitches; k++) e.setBypassSwitch(k, in.byp[k - 1]);
  // this track's input into the engine's per-track buffers (the slots filled last block are cleared first)
  for (int j = 0; j < ntw_; j++) { std::memset(tmpL_.get() + (size_t)tw_[j] * kBlock, 0, sizeof(float) * kBlock); std::memset(tmpR_.get() + (size_t)tw_[j] * kBlock, 0, sizeof(float) * kBlock); }
  ntw_ = 0;
  if (in.inL) { std::memcpy(tmpL_.get() + (size_t)track_ * kBlock, in.inL + a, sizeof(float) * (size_t)len); std::memcpy(tmpR_.get() + (size_t)track_ * kBlock, (in.inR ? in.inR : in.inL) + a, sizeof(float) * (size_t)len); tw_[ntw_++] = track_; }
  // BUS FEEDS: the other tracks this node takes in, as stereo pairs, in busMap's order
  const std::vector<int>& bm = e.busMap();
  for (int j = 0; j < (int)bm.size() && j < kMaxFeeds; j++) {
    const int t = bm[(size_t)j]; if (t == track_ || 2 * j >= in.nFeeds * 2 || !in.feeds || !in.feeds[2 * j]) continue;
    if (j >= in.nFeeds) continue;
    std::memcpy(tmpL_.get() + (size_t)t * kBlock, in.feeds[2 * j] + a, sizeof(float) * (size_t)len);
    std::memcpy(tmpR_.get() + (size_t)t * kBlock, (in.feeds[2 * j + 1] ? in.feeds[2 * j + 1] : in.feeds[2 * j]) + a, sizeof(float) * (size_t)len);
    bool seen = false; for (int q = 0; q < ntw_; q++) if (tw_[q] == t) seen = true; if (!seen) tw_[ntw_++] = t;
  }
  e.setTrackInputs(tmpL_.get(), tmpR_.get(), kChannels);
  e.setSidechain(off(in.scL), off(in.scR ? in.scR : in.scL), len);
  const float* tp[kMaxTaps]; int nt = 0; if (in.taps && in.nTaps > 0) { nt = std::min(in.nTaps, kMaxTaps); for (int k = 0; k < nt; k++) tp[k] = off(in.taps[k]); }
  e.setTaps(nt ? tp : nullptr, nt);
  const float* sp[kSends]; for (int k = 0; k < kSends; k++) sp[k] = (in.sends && k < in.nSends) ? off(in.sends[k]) : nullptr;
  e.setSendIn(sp, kSends);
  e.process(len);   // (a failed block clears the buses: the output below is silence)
  const float* bl = e.bus(track_ + 1); const float* br = e.busR(track_ + 1);
  if (out.L) std::memcpy(out.L + a, bl, sizeof(float) * (size_t)len);
  if (out.R && out.R != out.L) std::memcpy(out.R + a, br, sizeof(float) * (size_t)len);
  if (out.sends) for (int k = 0; k < out.nSends && k < kSends; k++) if (out.sends[k]) std::memcpy(out.sends[k] + a, e.sendOut(k), sizeof(float) * (size_t)len);
  return true;
}

}  // namespace chains
}  // namespace thunder
