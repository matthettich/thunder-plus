#include "bridge_session.h"

#include <array>
#include <chrono>
#include <cstdio>

#include "rt/json.h"
#include "text_util.h"

namespace tpd {

namespace {
std::string num(double v) { char b[64]; std::snprintf(b, sizeof b, "%.3f", v); return b; }
std::string strList(const std::vector<std::string>& v) { std::string o = "["; for (size_t i = 0; i < v.size(); i++) { if (i) o += ","; o += jsonQuote(v[i]); } return o + "]"; }
}  // namespace

BridgeSession::BridgeSession(LiveHost& host, Emit emit) : host_(host), emit_(std::move(emit)) { worker_ = std::thread([this] { worker(); }); }
BridgeSession::~BridgeSession() { shutdown(); }

void BridgeSession::shutdown() {
  { std::lock_guard<std::mutex> g(pm_); pumpStop_ = true; }
  pcv_.notify_all();
  if (pump_.joinable()) pump_.join();
  { std::lock_guard<std::mutex> g(qm_); stop_ = true; }
  qcv_.notify_all();
  if (worker_.joinable()) worker_.join();
}

void BridgeSession::post(const std::string& msg) { { std::lock_guard<std::mutex> g(qm_); q_.push_back(msg); } qcv_.notify_one(); }

void BridgeSession::drain() { std::unique_lock<std::mutex> lk(qm_); idleCv_.wait(lk, [&] { return (q_.empty() && !busy_) || stop_; }); }

void BridgeSession::worker() {
  for (;;) {
    std::string m;
    { std::unique_lock<std::mutex> lk(qm_);
      qcv_.wait(lk, [&] { return !q_.empty() || stop_; });
      if (q_.empty() && stop_) return;
      m = std::move(q_.front()); q_.pop_front(); busy_ = true; }
    run(m);
    { std::lock_guard<std::mutex> g(qm_); busy_ = false; }
    idleCv_.notify_all();
  }
}

void BridgeSession::err(const std::string& where, const std::string& msg) { emit("{\"t\":\"err\",\"where\":" + jsonQuote(where) + ",\"msg\":" + jsonQuote(msg) + ",\"fatal\":false}"); }

void BridgeSession::reply(int id, const std::string& fields) { if (id >= 0) emit("{\"t\":\"reply\",\"id\":" + std::to_string(id) + (fields.empty() ? "" : "," + fields) + "}"); }

void BridgeSession::check(const std::string& where, int rc) {
  if (rc == 0) return;
  std::string m = "rc " + std::to_string(rc);
  if (rc == -9) m += " (not supported by this build)"; else if (rc == -2) m += std::string(" (not allowed now: ") + (host_.active() ? "state" : "engine off") + ")";
  err(where, m);
}

void BridgeSession::run(const std::string& raw) {
  try {
    if (raw.size() >= 2 && raw[0] == 'B' && raw[1] == ':') {   // the bytes of the put chunk whose header came just before
      std::string bytes;
      if (!base64Decode(raw.substr(2), &bytes)) { err("put", "the put chunk is not base64"); emit("{\"t\":\"put.done\",\"pid\":" + std::to_string(puts_.lastPid()) + ",\"ok\":false,\"info\":\"not base64\"}"); return; }
      auto r = puts_.chunk((const uint8_t*)bytes.data(), bytes.size());
      const int pid = puts_.lastPid();
      if (!r.first.empty()) { err("put", r.first); emit("{\"t\":\"put.done\",\"pid\":" + std::to_string(pid) + ",\"ok\":false,\"info\":" + jsonQuote(r.first) + "}"); return; }
      if (r.second >= 0) { const PutStore::Buf* b = puts_.take(r.second); emit("{\"t\":\"put.done\",\"pid\":" + std::to_string(pid) + ",\"ok\":true,\"info\":\"" + std::to_string(b ? b->frames : 0) + " frames x " + std::to_string(b ? b->ch : 0) + "\"}"); }
      return;
    }
    const std::string type = typeOf(raw);
    if (type == "put.header") { const std::string e = puts_.header(raw); if (!e.empty()) err("put", e); return; }
    Translation tr = translate(raw);
    switch (tr.kind) {
      case Translation::kIgnored: break;
      case Translation::kRefused: err(tr.where, tr.msg); reply(tr.replyId, "\"err\":" + jsonQuote(tr.msg)); break;
      case Translation::kCall: execute(tr.call, type); break;
    }
  } catch (const std::exception& e) { err(typeOf(raw), std::string("shell error: ") + e.what()); }
}

void BridgeSession::execute(const Call& c, const std::string& type) {
  // The engine is down (engine.reset in progress): the page gets an error and, for an ask, an error reply
  if (!host_.active() && c.op != Op::Hello && c.op != Op::EngineReset && c.op != Op::BufFree && c.op != Op::Lxp && c.op != Op::MidiCfg) {
    const std::string msg = "the live engine is down; the page re-sends its state after {t:'engine',up:true}";
    err(type, msg);
    if (c.op == Op::SongLoad) reply(idOfTail(c.raw), "\"ok\":false,\"err\":" + jsonQuote(msg));
    return;
  }
  const int64_t at = c.at;
  switch (c.op) {
    case Op::Hello: hello(c); break;
    case Op::MidiCfg:
      err("midi.cfg", "MIDI is H7 (not in the H6 shell skeleton)");
      if (c.clockIn != clockFollow_.load()) { clockFollow_ = c.clockIn; if (host_.active()) { tpa::FitResult fr; std::string j; host_.fit(lastRev_, fitFlags(), &fr, &j); emit(j); } }
      break;
    case Op::SongLoad: {
      const int id = idOfTail(c.raw); lastRev_ = revOfTail(c.raw);
      const LoadResult r = host_.songLoad(c.raw);
      tpa::FitResult fr; std::string fitJson;
      const bool published = r.ok;
      if (published) host_.fit(lastRev_, fitFlags(), &fr, &fitJson);
      std::vector<std::string> reasons, warn;
      if (published) { for (auto& x : fr.reasons) reasons.push_back(x.text); warn = fr.warns; }
      else { reasons.push_back(r.err.empty() ? "the native engine refused the song (rc " + std::to_string(r.rc) + ")" : r.err); err("song.load", reasons[0]); }
      if (!fitJson.empty()) emit(fitJson);
      // pending: no kit.load has reached the shell yet, so this is NOT a verdict; the `fit` event after kit.load carries it
      reply(id, std::string("\"ok\":") + (published && fr.ok ? "true" : "false") + ",\"pending\":" + (published && fr.pending ? "true" : "false") + ",\"rev\":" + std::to_string((long long)lastRev_.load()) +
                    ",\"reasons\":" + strList(reasons) + ",\"warn\":" + strList(warn));
      break;
    }
    case Op::KitLoad: {
      const LoadResult r = host_.kitLoad(c.raw);
      if (!r.ok) { err("kit.load", r.err.empty() ? "rc " + std::to_string(r.rc) : r.err); return; }
      char b[320]; std::snprintf(b, sizeof b, "{\"t\":\"kit\",\"ok\":true,\"rc\":0,\"err\":\"\",\"pads\":%d,\"skipped\":%d,\"dry\":%d,\"renderMs\":%.0f,\"empty\":[%s]}", r.pads, r.skipped, r.dry, r.renderMs, r.empty.c_str());
      emit(b);
      tpa::FitResult fr; std::string j; host_.fit(lastRev_, fitFlags(), &fr, &j); emit(j);
      break;
    }
    case Op::PadsCommit: puts_.sweep(true); check("pads.commit", host_.padsPublish()); break;   // transfers still open at the commit never finished: released
    case Op::Lxp: { const bool on = c.st != 0; if (on != lxpActive_.load()) { lxpActive_ = on; if (host_.active()) { tpa::FitResult fr; std::string j; host_.fit(lastRev_, fitFlags(), &fr, &j); emit(j); } } break; }
    case Op::PadChop: check("pad.chop", host_.padChop(c.pad, c.loopSecs, c.n, c.pts)); break;
    case Op::PdcSet: { const int m = c.mode; check("pdc.set", host_.call([=](thunder_live* L) { return thunder_live_pdc_set(L, m); }, 10000)); break; }
    case Op::EngineReset: engineReset(); break;
    case Op::Play: check(type, host_.play(c.mode, c.row, c.track, c.sect, c.phrase, c.hasLoop, c.y0, c.y1, at, c.hasSeed, c.seed, c.pdc, c.onlyBits, c.hasOnly)); break;
    case Op::Stop: check(type, host_.call([=](thunder_live* L) { return thunder_live_stop(L, at); })); break;
    case Op::Hold: check(type, host_.call([=](thunder_live* L) { return thunder_live_hold(L, at); })); break;
    case Op::Bpm: { const double v = c.v; check(type, host_.call([=](thunder_live* L) { return thunder_live_bpm(L, v, at); })); break; }
    case Op::Loop: { const int h = c.hasLoop, a = c.y0, b = c.y1; check(type, host_.call([=](thunder_live* L) { return thunder_live_loop(L, h, a, b, at); })); break; }
    case Op::Lead: { const double v = c.v; check(type, host_.call([=](thunder_live* L) { return thunder_live_lead(L, v, at); })); break; }
    case Op::Perf: { const int w = c.what, i = c.index; const double v = c.v; check(type, host_.call([=](thunder_live* L) { return thunder_live_perf(L, w, i, v, at); })); break; }
    case Op::MixSet: { const int t = c.track, k = c.key; const double v = c.v, r = c.ramp; check(type, host_.call([=](thunder_live* L) { return thunder_live_mix_set(L, t, k, v, r, at); })); break; }
    case Op::FxPlan: {   // G5a: plan, then commit at NOW
      auto b = std::make_shared<std::string>(c.raw); const int t = c.track; const double bpm = c.bpm;
      const int rc = host_.call([=](thunder_live* L) { return thunder_live_fx_plan(L, t, b->empty() ? nullptr : (const uint8_t*)b->data(), b->size(), bpm); }, 10000);
      check(type, rc);
      if (rc == 0) check("fx.commit", host_.call([=](thunder_live* L) { return thunder_live_fx_commit(L, t, kNow); }));
      break;
    }
    case Op::FxCommit: { const int t = c.track; check(type, host_.call([=](thunder_live* L) { return thunder_live_fx_commit(L, t, at); })); break; }
    case Op::FxParam: { auto p = std::make_shared<std::array<double, 12>>(); for (int i = 0; i < 12; i++) (*p)[(size_t)i] = c.p[i]; const int t = c.track, s = c.slot; check(type, host_.call([=](thunder_live* L) { return thunder_live_fx_param(L, t, s, p->data(), at); })); break; }
    case Op::MasterConfig: { auto b = std::make_shared<std::string>(c.raw); const int sf = c.safety; check(type, host_.call([=](thunder_live* L) { return thunder_live_master_config(L, b->empty() ? nullptr : (const uint8_t*)b->data(), b->size(), sf); }, 10000)); break; }
    case Op::ChainEdit: {
#ifdef THUNDER_LIVE_F_CHAINS
      auto b = std::make_shared<std::string>(c.raw); check(type, host_.call([=](thunder_live* L) { return thunder_live_chain_edit(L, b->empty() ? nullptr : (const uint8_t*)b->data(), b->size(), at); }));
#else
      check(type, THUNDER_LIVE_E_UNSUPPORTED);
#endif
      break;
    }
    case Op::MetersWant: { const double hz = c.hz; check(type, host_.call([=](thunder_live* L) { return thunder_live_meters_want(L, hz); })); break; }
    case Op::BufFree: puts_.free(c.bufId); break;
    case Op::BufUse: {
      if (c.kind == "pad" && c.frames == 0) { check("buf.use", host_.padRemove(c.pad)); return; }   // the page's removal: no put, zero frames
      const PutStore::Buf* b = puts_.take(c.bufId);
      if (!b) { err("buf.use", "no put with id " + std::to_string(c.bufId)); return; }
      if (c.kind == "var") { check("buf.use", host_.varSet(c.ii, c.vk, c.vkNum, c.pad, b->data[0].data(), b->frames, b->sr)); return; }
      check("buf.use", host_.padTake(c.pad, b->data[0].data(), b->ch > 1 ? b->data[1].data() : nullptr, b->frames, b->sr));   // audible at the page's pads.commit
      break;
    }
  }
}

void BridgeSession::hello(const Call& h) {
  session_ = h.live;
  if (!h.live) {   // no N0 test core on the desktop: a hello without `live` is answered without starting anything, and the caps say there is no native audio
    reply(h.id, "\"v\":1,\"core\":\"h6-desktop\",\"host\":\"desktop\",\"caps\":{\"nativeAudio\":false,\"midi\":false,\"link\":false,\"plugins\":false,\"inputs\":false,\"render\":false,\"files\":false,\"engines\":[],\"fx\":[]}");
    return;
  }
  std::string e;
  if (host_.start(true, &e) < 0) { err("hello", "the live engine did not start: " + e); reply(h.id, "\"err\":" + jsonQuote("live engine did not start: " + e)); return; }
  flag_ = true;
  startPump();
  const uint32_t caps = host_.liveCaps();
  int outLat = 0; long long padBudget = (long long)128 << 20;
  { thunder::JsonDoc d; const std::string st = host_.statusJson(); if (d.parse(st)) outLat = (int)d.root().num("outLat", 0); }
  { thunder::JsonDoc d; const std::string st = host_.statsJson(); if (d.parse(st)) padBudget = (long long)d.root().num("padBudget", (double)padBudget); }
  const double sr = host_.sampleRate();
  char b[512];
  std::snprintf(b, sizeof b, "\"v\":1,\"core\":\"h6-live\",\"sampleRate\":%.0f,\"sr\":%.0f,\"outLat\":%d,\"host\":\"desktop\",\"maxPadBytes\":%lld,\"maxSongBytes\":%lld,\"live\":true,", sr, sr, outLat, padBudget, (long long)16 << 20);
  std::string body = b;
  body += std::string("\"caps\":{\"nativeAudio\":true,\"midi\":false,\"link\":false,\"plugins\":false,\"inputs\":false,\"render\":false,\"files\":false,\"live\":true,\"chains\":") + (host_.chainsOn() ? "true" : "false") +
          ",\"sf\":" + ((caps & 8) ? "true" : "false") + ",\"engines\":" + strList(engineList(LiveHost::hasLxr())) + ",\"fx\":[\"tfx\",\"pump\",\"modfx\",\"master\"],\"abi\":1,\"liveCaps\":" + std::to_string(caps) + "}";
  reply(h.id, body);
}

void BridgeSession::startPump() {
  std::lock_guard<std::mutex> g(pm_);
  if (pumpRun_) return;
  pumpRun_ = true; pumpStop_ = false;
  if (pump_.joinable()) pump_.join();
  pump_ = std::thread([this] { pump(); });
}

void BridgeSession::pump() {
  for (;;) {
    { std::unique_lock<std::mutex> lk(pm_); if (pcv_.wait_for(lk, std::chrono::milliseconds(20), [&] { return pumpStop_; })) return; }
    if (!host_.active()) continue;
    const std::string ev = host_.drainEvents();
    if (ev.empty()) continue;
    const std::string ld = num(host_.load());
    std::string out; size_t i = 0;
    while (i < ev.size()) {
      size_t j = ev.find('\n', i); if (j == std::string::npos) j = ev.size();
      std::string line = ev.substr(i, j - i); i = j + 1;
      if (line.empty()) continue;
      if (line.compare(0, 12, "{\"t\":\"clock\"") == 0 && line.back() == '}') { line.pop_back(); line += ",\"load\":" + ld + "}"; }   // CORE-BRIDGE 7 G9
      if (!out.empty()) out += '\n';
      out += line;
    }
    if (!out.empty()) emit(out);
  }
}

// CORE-BRIDGE 7 G13: stop, destroy, create. The page re-sends song, kit and pads when it sees {t:'engine', up:true}.
void BridgeSession::engineReset() {
  emit("{\"t\":\"engine\",\"up\":false}");
  const bool was = flag_.load();
  host_.shutdown();
  puts_.clear();
  std::string e;
  if (host_.start(true, &e) < 0) { err("engine.reset", "the live engine did not restart: " + e); return; }
  flag_ = was;
  emit("{\"t\":\"engine\",\"up\":true,\"sr\":" + std::to_string((long long)host_.sampleRate()) + "}");
}

}  // namespace tpd
