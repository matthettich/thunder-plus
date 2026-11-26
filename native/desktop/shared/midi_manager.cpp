#include "midi_manager.h"

#include <chrono>
#include <cstdio>

#include "host_clock.h"
#include "rt/json.h"
#include "text_util.h"

namespace tpd {

int64_t MidiManager::nowFn() { return hostNowNs(); }

MidiManager::MidiManager(std::shared_ptr<MidiBackend> backend, Emit emit) : be_(std::move(backend)), emit_(std::move(emit)) {
  if (be_) be_->setChangeHandler([this] { { std::lock_guard<std::mutex> g(wakeMu_); wake_ = true; } wakeCv_.notify_all(); });
}

MidiManager::~MidiManager() {
  if (be_) be_->setChangeHandler(nullptr);
  { std::lock_guard<std::mutex> g(wakeMu_); stop_ = true; }
  wakeCv_.notify_all();
  if (scan_.joinable()) scan_.join();
  engineStopping();
  std::vector<std::shared_ptr<InPort>> ins;
  { std::lock_guard<std::mutex> g(mu_); for (auto& kv : in_) ins.push_back(kv.second); in_.clear(); }
  for (auto& p : ins) if (be_) be_->closeInput(p->e.key);
}

// ---- MidiService --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

bool MidiManager::engineAttach(const std::shared_ptr<tpa::LiveEngine>& eng, double sampleRate) {
  if (!be_ || !eng || !eng->handle()) return false;
  thunder_midi* core = thunder_live_midi(eng->handle());
  if (!core) return false;
  tpa::host().setNowFn(&MidiManager::nowFn);   // the shell's one clock: host_clock.h (CoreMIDI times convert from it by the mach timebase)
  const bool ok = tpa::host().engineStartExternal(core, sampleRate, nullptr, nullptr);   // no output timestamp source: the clock map falls back to the callback start + the latency floor
  if (!ok) return false;
  on_.store(true);
  lastLat_.store(-1);
  { std::lock_guard<std::mutex> g(engMu_); eng_ = eng; }
  return true;
}

void MidiManager::engineStarted() {
  std::shared_ptr<tpa::LiveEngine> eng;
  { std::lock_guard<std::mutex> g(engMu_); eng = eng_; }
  if (!eng) return;
  tpa::host().setControlRunner([eng](std::function<int()> fn) {
    const int rc = eng->call([fn](thunder_live*) { return fn(); }, 1500);
    return (rc == -1000 && eng->running()) ? THUNDER_LIVE_E_BUSY : rc;   // a timeout BEFORE fn started is BUSY (fn never runs on this thread; nothing changed: a retry is safe); -1001 passes through
  });
  readerRun_ = true;
  reader_ = std::thread([this] { readerMain(); });
  { std::lock_guard<std::mutex> g(wakeMu_); if (!scanStarted_) { scanStarted_ = true; stop_ = false; scan_ = std::thread([this] { scanMain(); }); } wake_ = true; }
  wakeCv_.notify_all();
}

void MidiManager::engineStopping() {
  readerRun_ = false;
  if (reader_.joinable()) reader_.join();
  std::lock_guard<std::mutex> g(engMu_);
  eng_.reset();
}

void MidiManager::engineDetached() {
  if (on_.exchange(false)) tpa::host().engineStop();   // the sender delivers the Stop (<= about 0.5 s), then thunder_midiout_free: BEFORE thunder_live_destroy (live.h thunder_live_midi)
}

void MidiManager::streamRemap() { if (on_.load()) tpa::host().remap(); }

void MidiManager::setOutLatencyNs(int64_t ns) {
  if (!on_.load() || ns <= 0 || ns == lastLat_.load()) return;
  const int rc = tpa::host().setOutLatencyNs(ns);
  if (rc == 0) lastLat_.store(ns);   // BUSY: tried again at the next tick
}

void MidiManager::songEnded() { if (on_.load()) tpa::host().setPlaying(false); }
void MidiManager::transport(bool playing) { if (on_.load()) tpa::host().setPlayingDirect(playing); }

// THE AUDIO THREAD's one MIDI call (LiveHost::audioCb, after render): the clock map. No lock, no allocation (midi_out.h: AUDIO).
void MidiManager::audioBlock(int frames, int64_t hostFrame0, int64_t engineOffset, int64_t wallNs) { tpa::host().audioBlockLive(frames, hostFrame0, engineOffset, wallNs); }

bool MidiManager::running() const { return on_.load(); }

// ---- the page ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

std::string MidiManager::applyCfg(const std::vector<std::string>& outs, const bool clockOut[4], const double gear[4]) {
  std::string bad;
  for (int i = 0; i < 4; i++) {
    const int r = tpa::host().assign(i, i < (int)outs.size() ? outs[(size_t)i] : std::string());
    if (r < 0 && bad.empty()) bad = std::string("port ") + (char)('A' + i) + ": assign refused (" + std::to_string(r) + ")";
    tpa::host().setClock(i, clockOut[i]);
    tpa::host().setGear(i, gear[i]);
  }
  publish();
  return bad;
}

std::string MidiManager::portsEvent() {
  MidiPortState ps[4];
  uint64_t late = 0, sent = 0;
  thunder::JsonDoc d;
  if (d.parse(tpa::host().portsJson())) {
    auto a = d.root().arr("ports");
    for (size_t i = 0; i < 4 && i < a.size(); i++) {
      auto p = a.at(i);
      ps[i].state = (int)p.num("state", 0); ps[i].id = std::string(p.str("id")); ps[i].name = std::string(p.str("name")); ps[i].gear = p.num("gear", 0); ps[i].clock = (int)p.num("clock", 0);
    }
    late = (uint64_t)d.root().num("late", 0); sent = (uint64_t)d.root().num("sent", 0);
  }
  std::vector<MidiEntry> outs, ins;
  { std::lock_guard<std::mutex> g(mu_); for (const auto& e : regOut_.entries()) if (outOpen_.count(e.token) && outOpen_[e.token]) outs.push_back(e); for (const auto& kv : in_) ins.push_back(kv.second->e); }
  return midiPortsEventJson(outs, ins, ps, late, sent);
}

void MidiManager::publish() { if (emit_) emit_(portsEvent()); }

uint64_t MidiManager::inDropped() const {
  uint64_t n = 0;
  std::lock_guard<std::mutex> g(const_cast<std::mutex&>(rxMu_));
  for (const auto& p : rx_) n += p->ring->dropped();
  return n;
}

// ---- ports ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void MidiManager::pushList() {   // mu_ held
  std::vector<tpa::DevInfo> devs;
  for (const auto& e : regOut_.entries()) {
    auto it = outOpen_.find(e.token);
    if (it == outOpen_.end() || !it->second) continue;
    tpa::DevInfo d; d.id = e.id; d.name = e.name; d.token = e.token; d.ts = be_->outputStamped();
    devs.push_back(std::move(d));
  }
  tpa::host().setDevices(devs);   // a control call through the engine's control thread when an engine runs (a few ms; the scan thread is not the page's)
}

bool MidiManager::rescan() {
  if (!be_) return false;
  const auto outs = be_->listOutputs(), ins = be_->listInputs();
  std::vector<std::shared_ptr<InPort>> closing;
  bool changed = false;
  {
    std::lock_guard<std::mutex> g(mu_);
    const MidiDiff dout = regOut_.update(outs), din = regIn_.update(ins);
    if (!dout.removed.empty()) {   // the core stops listing them BEFORE their ports close: a send in flight meets an unknown token (< 0), never a freed port
      for (const auto& e : dout.removed) outOpen_.erase(e.token);
      pushList();
      for (const auto& e : dout.removed) tpa::host().detach(e.token);
      changed = true;
    }
    for (const auto& e : dout.added) {
      auto io = be_->openOutput(e.key);
      if (io) { tpa::host().attach(e.token, io, be_->outputStamped()); outOpen_[e.token] = true; }
      changed = true;
    }
    if (!dout.added.empty() || !dout.changed.empty()) { pushList(); changed = true; }
    for (const auto& e : din.removed) { auto it = in_.find(e.token); if (it != in_.end()) { closing.push_back(it->second); in_.erase(it); } changed = true; }
    for (const auto& e : din.added) {
      auto p = std::make_shared<InPort>(); p->e = e; p->ring = std::make_shared<MidiRxRing>(); p->ring->init(1 << 16);
      if (be_->openInput(e.key, p->ring)) in_[e.token] = p;
      changed = true;
    }
    for (const auto& e : din.changed) { auto it = in_.find(e.token); if (it != in_.end()) it->second->e = e; changed = true; }
    { std::lock_guard<std::mutex> rg(rxMu_); rx_.clear(); for (const auto& kv : in_) rx_.push_back(kv.second); }
  }
  for (auto& p : closing) be_->closeInput(p->e.key);   // outside mu_: closeInput may wait for a callback in flight
  return changed;
}

void MidiManager::scanMain() {
  for (;;) {
    { std::unique_lock<std::mutex> lk(wakeMu_);
      wakeCv_.wait_for(lk, std::chrono::seconds(3), [&] { return wake_ || stop_; });
      if (stop_) return;
      wake_ = false; }
    rescan();
    publish();   // on every change and every 3 s: the counters (late, sent) move too
  }
}

// THE READER THREAD: rings -> parser -> `midi.in` events. Not real-time: it allocates the JSON and takes the event queue's mutex; the OS's callback threads never do.
void MidiManager::readerMain() {
  uint8_t buf[MidiRxRing::kMaxRecord];
  while (readerRun_.load()) {
    bool any = false;
    std::shared_ptr<tpa::LiveEngine> eng;
    { std::lock_guard<std::mutex> g(engMu_); eng = eng_; }
    std::vector<std::shared_ptr<InPort>> ports;
    { std::lock_guard<std::mutex> g(rxMu_); ports = rx_; }
    for (auto& p : ports) {
      for (int guard = 0; guard < 256; guard++) {
        int64_t ts = 0;
        const int n = p->ring->pop(&ts, buf, sizeof buf);
        if (n < 0) break;
        any = true;
        if (!eng) continue;
        const int64_t ns = ts > 0 ? be_->ticksToNs(ts) : 0;
        struct Ctx { tpa::LiveEngine* e; uint64_t token; int64_t ns; MidiManager* m; } ctx{eng.get(), p->e.token, ns > 0 ? ns : hostNowNs(), this};
        p->parser.feed(buf, n, [](void* u, const tpa::MidiInMsg& m) {
          Ctx* c = static_cast<Ctx*>(u);
          c->e->pushEvent(tpa::midiInJson(c->e->hostFrameAt(c->ns), c->ns, c->token, m), tpa::midiInIsNote(m));
          c->m->inMsgs_.fetch_add(1, std::memory_order_relaxed);
        }, &ctx);
      }
    }
    if (!any) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

}  // namespace tpd
