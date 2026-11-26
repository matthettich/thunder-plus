#include "bridge_session.h"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "host_clock.h"
#include "rt/json.h"
#include "text_util.h"

namespace tpd {

namespace {
// The reply body to an fx.values ASK (PC6): {ok:true}; {ok:false, busy:true, error:'busy'} for E_BUSY (-8); {ok:false, error:'rc N (...)'} otherwise: the Android shell's fxValuesReply.
std::string fxValuesBody(int rc) {
  if (rc == 0) return "\"ok\":true";
  if (rc == -8) return "\"ok\":false,\"busy\":true,\"error\":\"busy\"";
  std::string m = "rc " + std::to_string(rc);
  if (rc == -1) m += " (refused: not a hosted slot of that kind, a wrong value count, or a value the core keeps in the song)"; else if (rc == -2) m += " (not allowed now)"; else if (rc == -9) m += " (not supported by this build)";
  return "\"ok\":false,\"error\":" + jsonQuote(m);
}
std::string num(double v) { char b[64]; std::snprintf(b, sizeof b, "%.3f", v); return b; }
std::string strList(const std::vector<std::string>& v) { std::string o = "["; for (size_t i = 0; i < v.size(); i++) { if (i) o += ","; o += jsonQuote(v[i]); } return o + "]"; }
}  // namespace

BridgeSession::BridgeSession(LiveHost& host, Emit emit, RecoveryStore* recovery, SessionIo io) : host_(host), io_(std::move(io)), emit_(std::move(emit)), rec_(recovery) { worker_ = std::thread([this] { worker(); }); }
BridgeSession::~BridgeSession() { if (abandoned_) { if (worker_.joinable()) worker_.detach(); } else shutdown(); }

bool BridgeSession::shutdown(int timeoutMs) {
  { std::lock_guard<std::mutex> g(wm_); watchStop_ = true; }
  wcv_.notify_all();
  if (watch_.joinable()) watch_.join();
  std::thread p;
  { std::lock_guard<std::mutex> g(pm_); closing_ = true; pumpStop_ = true; p = std::move(pump_); }   // L-1: no late hello can start a pump after this
  pcv_.notify_all();
  if (p.joinable()) p.join();
  { std::lock_guard<std::mutex> g(qm_); stop_ = true; }
  qcv_.notify_all();
  if (timeoutMs >= 0) {
    std::unique_lock<std::mutex> lk(qm_);
    if (!doneCv_.wait_for(lk, std::chrono::milliseconds(timeoutMs), [&] { return workerDone_; })) { abandoned_ = true; return false; }
  }
  if (worker_.joinable()) worker_.join();
  return true;
}

bool BridgeSession::post(const std::string& msg) {
  const char* why = nullptr;
  if (msg.size() >= 2 && msg[0] == 'B' && msg[1] == ':') { if (msg.size() - 2 > kMaxPutChars) why = "a put chunk over 24 M characters"; }
  else { const bool song = typeOf(msg) == "song.load"; if (msg.size() > (song ? kMaxSongBytes : kMaxOtherBytes)) why = song ? "a song.load over 16 MiB (maxSongBytes)" : "a message over 64 MiB"; }
  if (!why) {
    std::lock_guard<std::mutex> g(qm_);
    if (q_.size() >= kMaxQueueMsgs || qBytes_ + msg.size() > kMaxQueueBytes) why = "the shell's message queue is full";
    else { q_.push_back(msg); qBytes_ += msg.size(); }
  }
  if (why) {   // refused: told at once, never queued (an ask gets an error reply, so the page does not wait for it)
    const std::string t = (msg.size() >= 2 && msg[0] == 'B' && msg[1] == ':') ? std::string("put") : typeOf(msg);
    const std::string m = std::string("refused: ") + why;
    err(t.empty() ? "message" : t, m);
    if (t != "put") reply(idOfTail(msg), "\"ok\":false,\"error\":" + jsonQuote(m) + ",\"err\":" + jsonQuote(m));
    return false;
  }
  qcv_.notify_one();
  return true;
}

void BridgeSession::drain() { std::unique_lock<std::mutex> lk(qm_); idleCv_.wait(lk, [&] { return (q_.empty() && !busy_) || stop_; }); }

void BridgeSession::worker() {
  for (;;) {
    std::string m;
    { std::unique_lock<std::mutex> lk(qm_);
      qcv_.wait(lk, [&] { return !q_.empty() || stop_; });
      if (q_.empty() && stop_) { workerDone_ = true; doneCv_.notify_all(); return; }
      m = std::move(q_.front()); q_.pop_front(); qBytes_ -= m.size(); busy_ = true; }
    run(m);
    { std::lock_guard<std::mutex> g(qm_); busy_ = false; }
    idleCv_.notify_all();
  }
}

void BridgeSession::err(const std::string& where, const std::string& msg, bool fatal) { emit("{\"t\":\"err\",\"where\":" + jsonQuote(where) + ",\"msg\":" + jsonQuote(msg) + ",\"fatal\":" + (fatal ? "true" : "false") + "}"); }

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
    if (type == "dev.fallback") { devFallback(); return; }   // the hot-plug watcher's request (internal; harmless from a page: it acts only on a device that is really gone)
    if (type == "put.header") { const std::string e = puts_.header(raw); if (!e.empty()) err("put", e); return; }
    Translation tr = translate(raw);
    switch (tr.kind) {
      case Translation::kIgnored: break;
      case Translation::kRefused: err(tr.where, tr.msg); reply(tr.replyId, "\"err\":" + jsonQuote(tr.msg) + ",\"ok\":false,\"error\":" + jsonQuote(tr.msg)); break;
      case Translation::kCall: execute(tr.call, type); break;
    }
  } catch (const std::exception& e) { err(typeOf(raw), std::string("shell error: ") + e.what()); }
}

void BridgeSession::execute(const Call& c, const std::string& type) {
  // The engine is down (engine.reset in progress): the page gets an error and, for an ask, an error reply
  if (!host_.active() && c.op != Op::RecoveryPut && c.op != Op::RecoveryGet && c.op != Op::RecoveryDrop && c.op != Op::Hello && c.op != Op::EngineReset && c.op != Op::BufFree && c.op != Op::Lxp && c.op != Op::MidiCfg && c.op != Op::DevList && c.op != Op::DevOpen && c.op != Op::RtReset && c.op != Op::RtSnapshot &&
      c.op != Op::RecArm && c.op != Op::RecStart && c.op != Op::RecStop && c.op != Op::Take && c.op != Op::TakeFree) {
    const std::string msg = "the live engine is down; the page re-sends its state after {t:'engine',up:true}";
    err(type, msg);
    if (c.op == Op::SongLoad) reply(idOfTail(c.raw), "\"ok\":false,\"err\":" + jsonQuote(msg));
    if (c.op == Op::FxValues) reply(c.id, "\"ok\":false,\"error\":\"engine down\"");
    return;
  }
  const int64_t at = c.at;
  switch (c.op) {
    case Op::Hello: hello(c); break;
    case Op::MidiCfg:
      if (io_.midi) { const std::string bad = io_.midi->applyCfg(c.outs, c.clockOut, c.gear); if (!bad.empty()) err("midi.cfg", bad); }
      else err("midi.cfg", "this shell has no MIDI (hello.caps.midi is false)");
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
    case Op::Stop: check(type, host_.stop(at)); break;   // H7: also the MIDI clock boxes' transport flag
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
    case Op::MetersSet: {   // M4: a REPLACEMENT state (rate, which records, whose effects); E_BUSY is reported, the page re-sends its state on its next change
      thunder_live_meters_spec sp; std::memset(&sp, 0, sizeof sp);
      sp.size = sizeof sp; sp.kinds = (uint32_t)c.kinds; sp.rate_hz = c.hz; sp.fx_tracks = c.fxTracks; sp.chains_hz = 0; sp.chains_tracks = c.chainsTracks;   // chains_hz 0 = the core's 16 a second
      check(type, host_.call([=](thunder_live* L) { return thunder_live_meters_set(L, &sp); }));
      break;
    }
    case Op::FxValues: {   // M4 (PP): the whole value list of a pump or modular slot, at once. An ask (id) is answered either way; without an id a refusal is an err event
      auto v = std::make_shared<std::vector<double>>(c.vals); const int t = c.track, s = c.slot, k = c.fxKind;
      const int rc = host_.call([=](thunder_live* L) { return thunder_live_fx_values(L, t, s, k, v->data(), (int)v->size(), at); });
      if (c.id >= 0) reply(c.id, fxValuesBody(rc)); else check(type, rc);
      break;
    }
    case Op::DevList: devList(c.id); break;
    case Op::DevOpen: devOpen(c); break;
    case Op::RecArm: case Op::RecStart: case Op::RecStop: recOp(c); break;
    case Op::Take: takeFetch(c); break;
    case Op::TakeFree: if (io_.input) io_.input->freeTake(c.bufId); break;
    case Op::RtReset: host_.rtReset(); break;
    case Op::RtSnapshot: reply(c.id, "\"ok\":true," + host_.rtSnapshotJson()); break;
    case Op::RecoveryPut: if (rec_) rec_->put(c.raw, (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()); break;   // no reply
    case Op::RecoveryGet: reply(c.id, rec_ ? rec_->getBody() : std::string("\"ok\":false,\"error\":\"there is no recovery copy\"")); break;
    case Op::RecoveryDrop: if (rec_) rec_->drop(); reply(c.id, "\"ok\":true"); break;
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
    announce();
    return;
  }
  std::string e;
  if (!host_.active()) {   // H7: the saved choice (audio.json) is the first output; the page's own dev.open follows after hello and rebuilds only when it differs
    if (io_.settings) { const AudioSettings sv = io_.settings->load(); AudioConfig c; c.outId = sv.out; c.sampleRate = sv.sr; c.bufferFrames = sv.buf; host_.setAudioConfig(c); wantedOut_ = sv.out; wantedIn_ = sv.in; }
    else wantedOut_ = host_.audioConfig().outId;
  }
  if (host_.start(true, &e) < 0) { err("hello", "the live engine did not start: " + e); reply(h.id, "\"err\":" + jsonQuote("live engine did not start: " + e)); return; }
  flag_ = true;
  startPump();
  if (io_.input && !io_.input->isOpen() && !wantedIn_.empty()) { AudioInConfig ic; ic.inId = wantedIn_; ic.sampleRate = (int)host_.sampleRate(); std::string ie; if (io_.input->open(ic, &ie)) inCfg_ = ic; }
  const uint32_t caps = host_.liveCaps();
  int outLat = 0; long long padBudget = (long long)128 << 20;
  { thunder::JsonDoc d; const std::string st = host_.statusJson(); if (d.parse(st)) outLat = (int)d.root().num("outLat", 0); }
  { thunder::JsonDoc d; const std::string st = host_.statsJson(); if (d.parse(st)) padBudget = (long long)d.root().num("padBudget", (double)padBudget); }
  const double sr = host_.sampleRate();
  char b[512];
  std::snprintf(b, sizeof b, "\"v\":1,\"core\":\"h6-live\",\"sampleRate\":%.0f,\"sr\":%.0f,\"outLat\":%d,\"host\":\"desktop\",\"maxPadBytes\":%lld,\"maxSongBytes\":%lld,\"live\":true,", sr, sr, outLat, padBudget, (long long)16 << 20);
  std::string body = b;
  body += std::string("\"caps\":{\"nativeAudio\":true,\"midi\":") + ((io_.midi && io_.midi->running()) ? "true" : "false") + ",\"link\":false,\"plugins\":false,\"inputs\":" + (io_.input ? "true" : "false") + ",\"devices\":true,\"rt\":true,\"render\":false,\"files\":false,\"live\":true,\"meters2\":" + ((caps & 0x400) ? "true" : "false") + ",\"fxValues\":" + ((caps & 0x800) ? "true" : "false") +
          ",\"recoveryPut\":" + (rec_ && rec_->enabled() ? "true" : "false") + ",\"chains\":" + (host_.chainsOn() ? "true" : "false") +
          ",\"sf\":" + ((caps & 8) ? "true" : "false") + ",\"engines\":" + strList(engineList(LiveHost::hasLxr())) + ",\"fx\":[\"tfx\",\"pump\",\"modfx\",\"master\"],\"abi\":1,\"liveCaps\":" + std::to_string(caps) + "}";
  body += "," + audioBody();
  reply(h.id, body);
  announce();
  if (io_.midi) io_.midi->publish();   // the page learns the MIDI ports right after the hello (the Android shell does the same)
  devEvents(true);
  if (io_.devices && io_.devWatchMs > 0) { std::lock_guard<std::mutex> g(wm_); if (!watch_.joinable() && !watchStop_) watch_ = std::thread([this] { devWatch(); }); }
}

// after hello: a recovery copy from last time is announced once (the page asks "Restore unsaved changes?")
void BridgeSession::announce() { if (!rec_) return; const std::string e = rec_->announceEvent(); if (!e.empty()) emit(e); }

void BridgeSession::startPump() {
  std::lock_guard<std::mutex> g(pm_);
  if (pumpRun_ || closing_) return;
  pumpRun_ = true; pumpStop_ = false;
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
void BridgeSession::engineReset() { std::string e; rebuild(host_.audioConfig(), &e); }

// H7: the same with another output (dev.open, a lost device): the audio configuration is set between the destroy and the create.
bool BridgeSession::rebuild(const AudioConfig& cfg, std::string* errOut) {
  emit("{\"t\":\"engine\",\"up\":false}");
  const bool was = flag_.load();
  host_.shutdown();
  puts_.clear();
  host_.setAudioConfig(cfg);
  std::string e;
  if (host_.start(true, &e) < 0) {   // C-2: a definite answer: the engine is down for good until the next reset / hello; the page falls back to its own engine and says so
    const std::string m = "the live engine did not restart: " + e;
    emit("{\"t\":\"engine\",\"up\":false,\"error\":" + jsonQuote(m) + "}");
    err("engine.reset", m, true);
    if (errOut) *errOut = m;
    return false;
  }
  flag_ = was;
  emit("{\"t\":\"engine\",\"up\":true,\"sr\":" + std::to_string((long long)host_.sampleRate()) + "}");
  return true;
}

// ---- H7: audio devices, the input, takes ------------------------------------------------------------------------------------------------------------------------------------------------------------

// The fields every dev reply carries: what is open now.
std::string BridgeSession::audioBody() {
  const LiveHost::AudioInfo a = host_.audioInfo();
  const double sr = a.sampleRate;
  char b[160];
  std::snprintf(b, sizeof b, "\"sr\":%.0f,\"buf\":%d,\"latOut\":%d", sr, a.periodFrames, a.latencyFrames);
  std::string o = std::string("\"audio\":{\"open\":") + (a.open ? "true" : "false") + ",\"name\":" + jsonQuote(a.name) + ",\"id\":" + jsonQuote(a.id) + "," + (a.open ? b : "\"sr\":0,\"buf\":0,\"latOut\":0") + "}";
  if (io_.input && io_.input->isOpen()) {
    const AudioInInfo i = io_.input->info();
    o += ",\"input\":{\"open\":true,\"name\":" + jsonQuote(i.name) + ",\"id\":" + jsonQuote(i.id) + ",\"channels\":" + std::to_string(i.channels) + ",\"sr\":" + std::to_string((long long)i.sampleRate) + ",\"buf\":" + std::to_string(i.periodFrames) + ",\"latIn\":" + std::to_string(i.latencyFrames) + "}";
  } else o += ",\"input\":{\"open\":false}";
  return o;
}

void BridgeSession::devList(int id) {
  const auto devs = io_.devices ? io_.devices->list() : makeNullEnumerator()->list();
  const AudioConfig c = host_.audioConfig();
  reply(id, "\"ok\":true,\"devices\":" + audioDevicesJson(devs) + ",\"out\":" + jsonQuote(wantedOut_.empty() ? c.outId : wantedOut_) + ",\"in\":" + jsonQuote(wantedIn_) + "," + audioBody());
}

// An open input that goes away (or is replaced) while a take runs: the take is ended and kept (the page gets a `rec.done` event with the partial take), then the input closes.
void BridgeSession::closeInputKeepTake(const char* why) {
  if (!io_.input) return;
  if (io_.input->recording()) {
    std::string e; const int tid = io_.input->stop(&e);
    if (tid >= 0) { auto t = io_.input->take(tid); if (t) emit("{\"t\":\"rec.done\",\"take\":" + std::to_string(tid) + ",\"partial\":true,\"why\":" + jsonQuote(why) + ",\"frames\":" + std::to_string((long long)t->frames) + ",\"sr\":" + std::to_string((long long)t->sr) + ",\"nch\":" + std::to_string(t->channels) + "}"); }
  }
  io_.input->close();
}

void BridgeSession::devOpen(const Call& c) {
  auto fail = [&](const std::string& m) { err("dev.open", m); reply(c.id, "\"ok\":false,\"error\":" + jsonQuote(m) + ",\"err\":" + jsonQuote(m)); };
  DevOpenRequest r; r.out = c.devOut; r.in = c.devIn; r.hasIn = c.hasIn; r.sr = c.devSr; r.buf = c.devBuf;
  std::string e;
  if (!validateDevOpen(r, &e)) { fail(e); return; }
  AudioConfig want; want.outId = r.out; want.sampleRate = r.sr; want.bufferFrames = r.buf;
  bool rebuilt = false;
  if (!(host_.active() && host_.audioConfig() == want)) {
    if (!rebuild(want, &e)) { reply(c.id, "\"ok\":false,\"error\":" + jsonQuote(e) + ",\"err\":" + jsonQuote(e)); return; }
    rebuilt = true;
  }
  wantedOut_ = r.out;
  const LiveHost::OpenNote note = host_.openNote();
  std::string inErr;
  if (io_.input && c.hasIn) {
    wantedIn_ = r.in;
    if (r.in.empty()) { closeInputKeepTake("the input was closed"); inCfg_ = AudioInConfig(); }
    else {
      AudioInConfig ic; ic.inId = r.in; ic.sampleRate = (int)host_.sampleRate(); ic.bufferFrames = r.buf;
      const bool same = io_.input->isOpen() && inCfg_.inId == ic.inId && inCfg_.sampleRate == ic.sampleRate && inCfg_.bufferFrames == ic.bufferFrames && !io_.input->lost();
      if (!same) { closeInputKeepTake("the input changed"); if (io_.input->open(ic, &inErr)) inCfg_ = ic; else { inCfg_ = AudioInConfig(); err("dev.open", "the input did not open: " + inErr); } }
    }
  }   // an input is only opened when the page names one
  if (io_.settings) { AudioSettings sv; sv.out = r.out; sv.in = wantedIn_; sv.sr = r.sr; sv.buf = r.buf; io_.settings->save(sv); }
  std::string body = std::string("\"ok\":true,\"rebuilt\":") + (rebuilt ? "true" : "false") + ",\"fellBack\":" + (note.fellBack ? "true" : "false") + ",\"requested\":" + jsonQuote(note.requested) + "," + audioBody();
  if (!inErr.empty()) body += ",\"inError\":" + jsonQuote(inErr);
  reply(c.id, body);
}

void BridgeSession::recOp(const Call& c) {
  auto fail = [&](const std::string& m) { reply(c.id, "\"ok\":false,\"error\":" + jsonQuote(m) + ",\"err\":" + jsonQuote(m)); };
  if (!io_.input) { fail("this shell has no audio input (hello.caps.inputs is false)"); return; }
  std::string e;
  if (c.op == Op::RecArm) {
    if (!io_.input->arm(c.ins, &e)) { fail(e); return; }
    const AudioInInfo i = io_.input->info();
    reply(c.id, "\"ok\":true,\"nch\":" + std::to_string(c.ins.size()) + ",\"inputs\":" + std::to_string(i.channels) + ",\"sr\":" + std::to_string((long long)i.sampleRate) + ",\"latIn\":" + std::to_string(i.latencyFrames));
  } else if (c.op == Op::RecStart) {
    if (!io_.input->start(&e)) { fail(e); return; }
    reply(c.id, "\"ok\":true,\"hostNs\":" + std::to_string((long long)hostNowNs()));
  } else {
    const int tid = io_.input->stop(&e);
    if (tid < 0) { fail(e); return; }
    auto t = io_.input->take(tid);
    reply(c.id, "\"ok\":true,\"take\":" + std::to_string(tid) + ",\"frames\":" + std::to_string((long long)(t ? t->frames : 0)) + ",\"sr\":" + std::to_string((long long)(t ? t->sr : 0)) + ",\"nch\":" + std::to_string(t ? t->channels : 0) +
                 ",\"hostNs0\":" + std::to_string((long long)(t ? t->hostNs0 : 0)) + ",\"latencyFrames\":" + std::to_string(t ? t->latencyFrames : 0) + ",\"gaps\":" + std::to_string((long long)(t ? t->gaps : 0)) + ",\"full\":" + ((t && t->full) ? "true" : "false") + ",\"dropped\":" + std::to_string((unsigned long long)io_.input->dropped()));
  }
}

// `take {take, ch?, off?, n?}`: chunks of base64 float32 as `take.chunk` events {rid, c, off, b}, then the reply {ok, sr, nch, frames, hostNs0, latencyFrames}. At most 64 MiB per call.
void BridgeSession::takeFetch(const Call& c) {
  auto fail = [&](const std::string& m) { reply(c.id, "\"ok\":false,\"error\":" + jsonQuote(m) + ",\"err\":" + jsonQuote(m)); };
  if (!io_.input) { fail("this shell has no audio input"); return; }
  auto t = io_.input->take(c.bufId);
  if (!t) { fail("no take with id " + std::to_string(c.bufId)); return; }
  int c0 = 0, c1 = t->channels;
  c0 = c.takeCh >= 0 ? c.takeCh : 0; c1 = c.takeCh >= 0 ? c.takeCh + 1 : t->channels;
  if (c0 >= t->channels) { fail("take: the take has " + std::to_string(t->channels) + " channels"); return; }
  const int64_t off = c.takeOff < 0 ? 0 : c.takeOff, total = t->frames;
  if (off > total) { fail("take: off is past the end"); return; }
  const int64_t n = (c.takeN < 0 || off + c.takeN > total) ? total - off : c.takeN;
  if ((int64_t)(c1 - c0) * n * 4 > ((int64_t)64 << 20)) { fail("take: at most 64 MiB per call: ask one channel (ch) or a range (off, n)"); return; }
  const int64_t piece = 262144;
  for (int ch = c0; ch < c1; ch++) {
    const std::vector<float>& d = t->data[(size_t)ch];
    for (int64_t o = 0; o < n; o += piece) {
      const int64_t m = std::min<int64_t>(piece, n - o);
      std::string b64 = base64Encode(reinterpret_cast<const uint8_t*>(d.data() + off + o), (size_t)m * 4);
      emit("{\"t\":\"take.chunk\",\"rid\":" + std::to_string(c.id) + ",\"c\":" + std::to_string(ch - c0) + ",\"off\":" + std::to_string((long long)o) + ",\"b\":\"" + b64 + "\"}");
    }
  }
  reply(c.id, "\"ok\":true,\"sr\":" + std::to_string((long long)t->sr) + ",\"nch\":" + std::to_string(c1 - c0) + ",\"frames\":" + std::to_string((long long)n) + ",\"hostNs0\":" + std::to_string((long long)t->hostNs0) + ",\"latencyFrames\":" + std::to_string(t->latencyFrames));
}

// After hello: what the saved choice could not give. A device that is gone is said once; the default plays and the page decides whether to forget the choice.
void BridgeSession::devEvents(bool initial) {
  (void)initial;
  const LiveHost::OpenNote n = host_.openNote();
  if (n.fellBack) emit("{\"t\":\"dev\",\"kind\":\"lost\",\"which\":\"out\",\"id\":" + jsonQuote(n.requested) + ",\"fellBack\":true,\"name\":" + jsonQuote(n.actualName) + "}");
  if (io_.input && !wantedIn_.empty() && !io_.input->isOpen()) emit("{\"t\":\"dev\",\"kind\":\"lost\",\"which\":\"in\",\"id\":" + jsonQuote(wantedIn_) + "}");
}

// Hot-plug, the page's way of life: the watcher (its own thread) lists the devices, tells the page when the list changed and asks the worker (a `dev.fallback` message) to act when the open output or input is gone.
void BridgeSession::devWatch() {
  bool first = true; bool wantedBackSent = false;
  for (;;) {
    { std::unique_lock<std::mutex> lk(wm_); if (wcv_.wait_for(lk, std::chrono::milliseconds(io_.devWatchMs), [&] { return watchStop_; })) return; }
    if (!io_.devices) continue;
    const auto devs = io_.devices->list();
    const std::string js = audioDevicesJson(devs);
    if (js != lastDevList_) { if (!first) emit("{\"t\":\"dev\",\"kind\":\"changed\",\"devices\":" + js + "}"); lastDevList_ = js; }
    first = false;
    const LiveHost::AudioInfo a = host_.audioInfo();
    bool gone = false;
    auto hasOut = [&](const std::string& id) { for (const auto& d : devs) if (d.outId == id && d.outputs > 0) return true; return false; };
    auto hasIn = [&](const std::string& id) { for (const auto& d : devs) if (d.inId == id && d.inputs > 0) return true; return false; };
    if (flag_.load() && a.open && (a.lost || (!a.id.empty() && !hasOut(a.id)))) gone = true;
    if (io_.input && io_.input->isOpen() && (io_.input->lost() || (!inCfg_.inId.empty() && !hasIn(inCfg_.inId)))) gone = true;
    if (gone) { std::string m = "{\"t\":\"dev.fallback\"}"; post(m); }
    // the wanted output came back while another plays: said once, never switched (a switch would stop the music)
    const bool backNow = a.open && !wantedOut_.empty() && a.id != wantedOut_ && hasOut(wantedOut_);
    if (backNow && !wantedBackSent) { emit("{\"t\":\"dev\",\"kind\":\"changed\",\"wantedBack\":\"out\",\"id\":" + jsonQuote(wantedOut_) + "}"); wantedBackSent = true; }
    if (!backNow) wantedBackSent = false;
  }
}

// On the worker: act on a lost output or input. The output falls back to the system default (the page is told: `dev` lost, `device` stream stopped, then the engine events of the rebuild, then `dev` reopened);
// the input is closed (a running take ends and is kept).
void BridgeSession::devFallback() {
  const auto devs = io_.devices ? io_.devices->list() : makeNullEnumerator()->list();
  auto hasOut = [&](const std::string& id) { for (const auto& d : devs) if (d.outId == id && d.outputs > 0) return true; return false; };
  auto hasIn = [&](const std::string& id) { for (const auto& d : devs) if (d.inId == id && d.inputs > 0) return true; return false; };
  if (io_.input && io_.input->isOpen() && (io_.input->lost() || (!inCfg_.inId.empty() && !hasIn(inCfg_.inId)))) {
    const std::string id = inCfg_.inId;
    closeInputKeepTake("the input device went away");
    emit("{\"t\":\"dev\",\"kind\":\"lost\",\"which\":\"in\",\"id\":" + jsonQuote(id) + "}");
  }
  const LiveHost::AudioInfo a = host_.audioInfo();
  if (flag_.load() && a.open && (a.lost || (!a.id.empty() && !hasOut(a.id)))) {
    emit("{\"t\":\"dev\",\"kind\":\"lost\",\"which\":\"out\",\"id\":" + jsonQuote(a.id) + ",\"name\":" + jsonQuote(a.name) + "}");
    AudioConfig c = host_.audioConfig(); c.outId.clear(); c.bufferFrames = 0;   // the system default; the old rate is asked again (converted if the new device runs at another)
    emit("{\"t\":\"device\",\"reason\":\"stream\",\"stopped\":true}");
    std::string e;
    if (rebuild(c, &e)) { const LiveHost::AudioInfo b = host_.audioInfo(); emit("{\"t\":\"dev\",\"kind\":\"reopened\",\"which\":\"out\",\"name\":" + jsonQuote(b.name) + ",\"sr\":" + std::to_string((long long)b.sampleRate) + ",\"buf\":" + std::to_string(b.periodFrames) + ",\"fellBack\":true}"); }
  }
}

}  // namespace tpd
