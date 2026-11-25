// U36 T9: see midi_out.h (threads, locking, shutdown). No Android includes: built on Linux by the ctest `android_midi_sink`.
#include "midi_out.h"

#include <time.h>
#include <sched.h>
#if defined(__ANDROID__)
#include <sys/resource.h>
#include <unistd.h>
#elif defined(__linux__)
#include <sys/syscall.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>

#include "midi/midi_host.h"
#include "seq/runner.h"
#include "seq/seq.h"
#include "song/song.h"

namespace tpa {
using namespace thunder;

int64_t monoNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

// ---- PortTable ----------------------------------------------------------------------------------------------------------------------------
void PortTable::put(uint64_t token, std::shared_ptr<PortIo> io, bool ts) {
  std::shared_ptr<PortIo> old;   // dies after the unlock
  {
    std::lock_guard<std::mutex> g(mu_);
    for (auto& s : v_) if (s.token == token) { old = std::move(s.e.io); s.e.io = std::move(io); s.e.ts = ts; return; }
    v_.push_back(Slot{token, Entry{std::move(io), ts}});
  }
}
void PortTable::remove(uint64_t token) {
  std::shared_ptr<PortIo> old;
  {
    std::lock_guard<std::mutex> g(mu_);
    for (size_t i = 0; i < v_.size(); i++) if (v_[i].token == token) { old = std::move(v_[i].e.io); v_.erase(v_.begin() + (long)i); break; }
  }
}
void PortTable::clear() {
  std::vector<Slot> old;
  { std::lock_guard<std::mutex> g(mu_); old.swap(v_); }
}
PortTable::Entry PortTable::find(uint64_t token) const {
  std::lock_guard<std::mutex> g(mu_);
  for (const auto& s : v_) if (s.token == token) return s.e;
  return Entry();
}
size_t PortTable::size() const { std::lock_guard<std::mutex> g(mu_); return v_.size(); }

// ---- the sink ---------------------------------------------------------------------------------------------------------------------------
struct SinkGlue {
  static int64_t now(void* u) { return static_cast<MidiHost*>(u)->nowFn_(); }
  static int send(void* u, uint64_t dev, const uint8_t* b, int len, int64_t at) { return static_cast<MidiHost*>(u)->sendTo(dev, b, len, at); }
  static int64_t clear(void* u, uint64_t dev) { return static_cast<MidiHost*>(u)->clearDev(dev); }
};

// ---- the debug song -------------------------------------------------------------------------------------------------------------------------
// Two bars of a C major arpeggio on a MIDI instrument (OUTPUT 1, port A, channel 1), 120 bpm, 4 lines a beat: 16 rows a phrase, two song rows that both play the phrase.
// Built as text for the core's own loader, so the song format is the app's. The MIDI clock rides on port A's clock box.
struct DebugSong {
  enum { ARMED = 0, RUNNING = 1, FINISHED = 2 };
  Song song; SeqVars vars; std::unique_ptr<Seq> seq; SeqRunner runner;
  std::vector<SeqEvent> evs; SeqStep st;
  std::atomic<int> state{ARMED};
  std::string text;

  static std::string makeText() {
    std::string s = "{\"app\":\"thunder-song\",\"v\":6,\"bpm\":120,\"lpb\":4,\"ticks\":6,\"swing\":50,\"dlen\":16,\"plen\":[16],\"song\":[";
    for (int r = 0; r < 2; r++) { s += r ? ",[" : "["; s += "0"; for (int t = 1; t < 33; t++) s += ",-1"; s += "]"; }
    s += "],\"chains\":[[[0,0]";
    for (int k = 1; k < 16; k++) s += ",[-1,0]";
    s += "]],\"phrases\":[[";
    const int notes[4] = {48, 52, 55, 60};   // C-4 E-4 G-4 C-5 (tracker note 48 = MIDI 60)
    for (int row = 0; row < 16; row++) {
      if (row) s += ",";
      char b[96];
      if (row % 4 == 0) std::snprintf(b, sizeof b, "[%d,-1,-1,-1,255,0,-1,-1,-1,-1,-1,-1]", notes[row / 4]);
      else if (row % 4 == 2) std::snprintf(b, sizeof b, "[200,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]");   // OFF
      else std::snprintf(b, sizeof b, "[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]");
      s += b;
    }
    s += "]],\"insts\":[{\"mo\":1,\"mch\":0,\"mpt\":0,\"pbr\":2}]}";
    return s;
  }
};

// ---- MidiHost: control -----------------------------------------------------------------------------------------------------------------------
static void (*g_enter)() = nullptr;
static void (*g_leave)() = nullptr;
void MidiHost::setSenderThreadHooks(void (*enter)(), void (*leave)()) { g_enter = enter; g_leave = leave; }

MidiHost::MidiHost() : nowFn_(&monoNs) { std::memset(&sink_, 0, sizeof sink_); }
MidiHost::~MidiHost() { engineStop(); }

static void copyClamped(char* dst, const std::string& s) {   // at most 95 bytes, never inside a UTF-8 sequence
  size_t n = std::min(s.size(), (size_t)(THUNDER_MIDIOUT_ID_BYTES - 1));
  while (n > 0 && n < s.size() && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
  std::memcpy(dst, s.data(), n); dst[n] = 0;
}

int MidiHost::applyDevicesLocked() {
  if (!o_) return 0;
  thunder_midiout_dev d[THUNDER_MIDIOUT_MAX_DEVS];
  const int n = (int)std::min(devs_.size(), (size_t)THUNDER_MIDIOUT_MAX_DEVS);
  for (int i = 0; i < n; i++) {
    std::memset(&d[i], 0, sizeof d[i]);
    copyClamped(d[i].id, devs_[(size_t)i].id); copyClamped(d[i].name, devs_[(size_t)i].name);
    d[i].flags = devs_[(size_t)i].ts ? THUNDER_MIDIOUT_DEV_TS : 0; d[i].token = devs_[(size_t)i].token;
  }
  int r = THUNDER_MIDIOUT_OK;
  for (int tries = 0; tries < 8; tries++) {   // E_BUSY: the scheduler's command ring was full; the audio thread empties it every block
    r = thunder_midiout_set_devices(o_, d, n);
    if (r != THUNDER_MIDIOUT_E_BUSY) break;
    timespec ts{0, 3 * 1000 * 1000}; nanosleep(&ts, nullptr);
  }
  return r;
}

int MidiHost::applyAssignLocked(int port) {
  if (!o_) return 0;
  int r = THUNDER_MIDIOUT_OK;
  for (int tries = 0; tries < 8; tries++) {
    r = thunder_midiout_assign(o_, port, assign_[port].c_str());
    if (r != THUNDER_MIDIOUT_E_BUSY) break;
    timespec ts{0, 3 * 1000 * 1000}; nanosleep(&ts, nullptr);
  }
  return r;
}

bool MidiHost::engineStart(double sr, TsFn tsFn, void* tsUser) {
  std::lock_guard<std::mutex> g(ctl_);
  if (h_) return true;
  char err[128];
  thunder_midi* core = thunder_midi_create(sr, err, sizeof err);
  if (!core) return false;
  if (!startLocked(core, false, sr, tsFn, tsUser)) { thunder_midi_free(core); return false; }
  return true;
}

bool MidiHost::engineStartExternal(thunder_midi* core, double sr, TsFn tsFn, void* tsUser) {
  std::lock_guard<std::mutex> g(ctl_);
  if (h_) return true;
  if (!core) return false;
  return startLocked(core, true, sr, tsFn, tsUser);
}

bool MidiHost::startLocked(thunder_midi* core, bool external, double sr, TsFn tsFn, void* tsUser) {
  sink_.user = this; sink_.now_ns = &SinkGlue::now; sink_.send = &SinkGlue::send; sink_.clear = &SinkGlue::clear;   // clearDev: drops the held messages of a port without timestamps, flushes a stamped port's
  sink_.caps = THUNDER_MIDIOUT_CAP_TS;   // per SINK, fixed at create: a port that cannot stamp is held by sendTo until its time (see there)
  o_ = thunder_midiout_create(core, sr, &sink_);
  if (!o_) return false;
  h_ = core; external_ = external;
  sr_ = sr; tsFn_ = tsFn; tsUser_ = tsUser;
  frames_ = 0; lastMapNs_ = 0; mapped_ = false; mapReady_.store(false, std::memory_order_release); remap_.store(false, std::memory_order_relaxed); nHeld_ = 0; heldCount_.store(0, std::memory_order_relaxed);
  if (!held_) held_.reset(new (std::nothrow) Held[kHeld]);
  if (!held_) { thunder_midiout_free(o_); o_ = nullptr; h_ = nullptr; external_ = false; return false; }
  applyDevicesLocked();
  for (int p = 0; p < 4; p++) { applyAssignLocked(p); thunder_midiout_set_clock(o_, p, clock_[p] ? 1 : 0); thunder_midiout_set_gear(o_, p, gear_[p]); }
  if (outLatNs_.load(std::memory_order_relaxed) > 0) thunder_midiout_set_out_latency(o_, outLatNs_.load(std::memory_order_relaxed));
  stop_.store(false, std::memory_order_release);
  sender_ = std::thread(&MidiHost::senderMain, this);
  running_.store(true, std::memory_order_release);
  return true;
}

void MidiHost::freeDebugLocked(bool force) {
  if (!dbgOwned_) return;
  if (force || dbgOwned_->state.load(std::memory_order_acquire) == DebugSong::FINISHED) {
    if (force) dbg_.store(nullptr, std::memory_order_release);   // only after the audio callback has stopped
    delete dbgOwned_; dbgOwned_ = nullptr;
  }
}

void MidiHost::engineStop() {
  std::lock_guard<std::mutex> g(ctl_);
  if (!h_) return;
  running_.store(false, std::memory_order_release);
  { std::lock_guard<std::mutex> rg(runMu_); runner_ = nullptr; }
  // A song still playing: the audio callback has stopped, so THIS thread takes its role for one last block: Stop (notes off, CLEAR, clock Stop) goes through the real queue, and the
  // sender gets up to 200 ms to deliver it before it is stopped (a synth must not be left holding a note).
  const bool debugLive = dbgOwned_ && dbgOwned_->state.load(std::memory_order_acquire) != DebugSong::FINISHED;
  if (mapReady_.load(std::memory_order_acquire) && (debugLive || external_)) {
    if (!external_) {   // the live host made its own Stop when the shell stopped the transport (live.h: stop posts thunder_midi_stop); only the debug song needs this last block
      const double now = (double)frames_ / sr_ + 0.01;
      thunder::midi_begin(h_, now, 0.01, 0.0, ~(uint64_t)0);
      thunder::midi_sched(h_).stop(now);
      thunder::midi_end(h_, now, now, 120, false);
    }
    thunder_midiout_set_playing(o_, 0);
    // Quiet = the core's counters unchanged for `need` ms AND the sender holds nothing. The pump hands a message to the sink up to 20 ms before its time and the Stop is stamped 10 ms
    // ahead plus the output latency, so a window of 25 ms + the latency is what it takes for the Stop to have been handed over; a message held for a port without timestamps counts as
    // sent when it is held, so heldCount_ must be 0 as well (the sender sends them when due, and flushes the rest when it is told to stop).
    const int64_t latMs = outLatNs_.load(std::memory_order_relaxed) / 1000000;
    const int need = 25 + (int)(latMs > 300 ? 300 : latMs);
    uint64_t last = ~(uint64_t)0; int still = 0;
    for (int i = 0; i < 200 + need && still < need; i++) {
      timespec ts{0, 1000000}; nanosleep(&ts, nullptr);
      thunder_midiout_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; thunder_midiout_stats_get(o_, &st);
      still = (st.sent + st.dropped_gone == last && heldCount_.load(std::memory_order_acquire) == 0) ? still + 1 : 0; last = st.sent + st.dropped_gone;
    }
  }
  stop_.store(true, std::memory_order_release);
  if (sender_.joinable()) sender_.join();   // the caller stopped the audio callback first: nobody calls clock_map or process any more
  thunder_midiout_free(o_); o_ = nullptr;
  if (!external_) thunder_midi_free(h_);   // the live host's thunder_midi is freed by thunder_live_destroy
  h_ = nullptr; external_ = false;
  freeDebugLocked(true);
  nHeld_ = 0; heldCount_.store(0, std::memory_order_relaxed);
  if (held_) for (int i = 0; i < kHeld; i++) held_[i].io.reset();
}

void MidiHost::setControlRunner(std::function<int(std::function<int()>)> r) { std::lock_guard<std::mutex> g(runMu_); runner_ = std::move(r); }

int MidiHost::viaControl(std::function<int()> fn) {
  std::function<int(std::function<int()>)> r;
  { std::lock_guard<std::mutex> g(runMu_); r = runner_; }
  if (!r) return fn();
  const int rc = r(fn);
  // -1000: the runner says there is NO control thread (the engine is gone): nothing to race with, so fn runs here. A TIMEOUT or a busy control thread is the runner's own code (the shell's runner answers
  // THUNDER_LIVE_E_BUSY): fn is NOT run on the caller's thread beside a thunder_live_* control call, which is exactly what the runner exists to prevent (verify-n2 F11).
  return rc == -1000 ? fn() : rc;
}

int MidiHost::setDevices(const std::vector<DevInfo>& devs) { return viaControl([this, devs] { return setDevicesImpl(devs); }); }
int MidiHost::assign(int port, const std::string& id) { if (port < 0 || port > 3) return THUNDER_MIDIOUT_E_ARG; return viaControl([this, port, id] { return assignImpl(port, id); }); }

int MidiHost::setClock(int port, bool on) {
  if (port < 0 || port > 3) return THUNDER_MIDIOUT_E_ARG;
  return viaControl([this, port, on] { std::lock_guard<std::mutex> g(ctl_); clock_[port] = on; return o_ ? thunder_midiout_set_clock(o_, port, on ? 1 : 0) : 0; });
}
int MidiHost::setGear(int port, double ms) {
  if (port < 0 || port > 3) return THUNDER_MIDIOUT_E_ARG;
  return viaControl([this, port, ms] { std::lock_guard<std::mutex> g(ctl_); gear_[port] = ms; return o_ ? thunder_midiout_set_gear(o_, port, ms) : 0; });
}
int MidiHost::setPlaying(bool on) { return viaControl([this, on] { return setPlayingDirect(on); }); }
int MidiHost::setPlayingDirect(bool on) { std::lock_guard<std::mutex> g(ctl_); return o_ ? thunder_midiout_set_playing(o_, on ? 1 : 0) : 0; }

std::string MidiHost::portsJson() {
  std::lock_guard<std::mutex> g(ctl_);
  std::string s = "{\"ports\":[";
  char b[512];
  for (int i = 0; i < 4; i++) {
    thunder_midiout_port p; std::memset(&p, 0, sizeof p);
    if (o_) thunder_midiout_get_port(o_, i, &p);
    auto esc = [](const char* in) { std::string o; for (; *in; in++) { if (*in == '"' || *in == '\\') { o += '\\'; o += *in; } else if ((unsigned char)*in < 0x20) o += ' '; else o += *in; } return o; };
    std::snprintf(b, sizeof b, "%s{\"state\":%d,\"id\":\"%s\",\"name\":\"%s\",\"gear\":%.0f,\"clock\":%d}", i ? "," : "", o_ ? p.state : 0, esc(p.id).c_str(), esc(p.name).c_str(), o_ ? p.gear_ms : gear_[i], o_ ? p.clock : (clock_[i] ? 1 : 0));
    s += b;
  }
  thunder_midiout_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st;
  if (o_) thunder_midiout_stats_get(o_, &st);
  std::snprintf(b, sizeof b, "],\"late\":%llu,\"sent\":%llu,\"gone\":%llu,\"qfull\":%llu,\"hang\":%llu}", (unsigned long long)st.late, (unsigned long long)st.sent, (unsigned long long)st.dropped_gone, (unsigned long long)st.queue_full, (unsigned long long)st.may_hang);
  return s + b;
}

int MidiHost::setDevicesImpl(const std::vector<DevInfo>& devs) {
  std::lock_guard<std::mutex> g(ctl_);
  std::vector<DevInfo> old; old.swap(devs_); devs_ = devs;
  const int r = applyDevicesLocked();
  if (r == THUNDER_MIDIOUT_E_ARG) devs_.swap(old);   // refused, nothing changed in the core: keep the old list as the saved one
  return r;
}

int MidiHost::assignImpl(int port, const std::string& id) {
  std::lock_guard<std::mutex> g(ctl_);
  assign_[port] = id;
  return applyAssignLocked(port);
}

int MidiHost::setOutLatencyNs(int64_t ns) {
  const int64_t v = ns < 0 ? 0 : ns > 2000000000LL ? 2000000000LL : ns;
  return viaControl([this, v] { std::lock_guard<std::mutex> g(ctl_); outLatNs_.store(v, std::memory_order_relaxed); if (o_) thunder_midiout_set_out_latency(o_, v); return 0; });
}

int MidiHost::debugTest() {
  std::lock_guard<std::mutex> g(ctl_);
  if (!o_ || !h_) return -1;
  if (!mapReady_.load(std::memory_order_acquire)) return -5;   // the pump drops whatever it drains before the first clock map
  freeDebugLocked(false);
  if (dbgOwned_) return -3;
  thunder_midiout_port p; if (thunder_midiout_get_port(o_, 0, &p) != THUNDER_MIDIOUT_OK || p.state != THUNDER_MIDIOUT_PORT_OK) return -2;
  DebugSong* d = new (std::nothrow) DebugSong;
  if (!d) return -4;
  d->text = DebugSong::makeText();
  thunder::JsonError je;
  if (!d->song.load(d->text.data(), d->text.size(), &je)) { delete d; return -4; }
  thunder::SeqOpts o; o.live = true; o.once = true; o.row = 0;   // once: the song stops at its end instead of wrapping
  d->seq.reset(new (std::nothrow) thunder::Seq(d->song, d->vars, thunder::SeqMode::Song, o));
  d->evs.resize(256);
  if (!d->seq || thunder_midi_set_song(h_, reinterpret_cast<const uint8_t*>(d->text.data()), d->text.size()) != THUNDER_MIDI_OK) { delete d; return -4; }
  thunder_midiout_set_clock(o_, 0, 1);
  thunder_midiout_set_gear(o_, 0, 0);
  thunder_midiout_set_playing(o_, 1);
  thunder_midi_start(h_, 0);   // posted: the audio thread runs it at the top of the next block, before the first step
  const uint8_t on[3] = {0x90, 72, 100}, off[3] = {0x80, 72, 0};   // a live ping (the raw path) before the song: C6 for a quarter second
  thunder_midi_raw(h_, 0, on, 3, 0.0);
  thunder_midi_raw(h_, 0, off, 3, 0.25);
  dbgOwned_ = d;
  dbg_.store(d, std::memory_order_release);
  return 0;
}

bool MidiHost::debugPoll() {
  std::lock_guard<std::mutex> g(ctl_);
  if (!dbgOwned_) return false;
  if (dbgOwned_->state.load(std::memory_order_acquire) == DebugSong::FINISHED) {
    if (o_) thunder_midiout_set_playing(o_, 0);
    freeDebugLocked(false);
    return false;
  }
  return true;
}

std::string MidiHost::status() {
  std::lock_guard<std::mutex> g(ctl_);
  std::string s;
  char b[320];
  if (!o_) { s = "MIDI out: engine not running (tap MIDI out test to start it)"; }
  else {
    s = "MIDI out ports:";
    for (int i = 0; i < 4; i++) {
      thunder_midiout_port p; thunder_midiout_get_port(o_, i, &p);
      std::snprintf(b, sizeof b, " %c=%s", 'A' + i, p.state == THUNDER_MIDIOUT_PORT_OK ? p.id : p.state == THUNDER_MIDIOUT_PORT_MISSING ? "(not connected)" : "-");
      s += b;
    }
    thunder_midiout_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; thunder_midiout_stats_get(o_, &st);
    thunder_midi_stats cs; std::memset(&cs, 0, sizeof cs); cs.size = sizeof cs; thunder_midi_stats_get(h_, &cs);
    std::snprintf(b, sizeof b, "\nstats: sent %llu late %llu gone %llu nomap %llu cleared %llu qfull %llu maxq %llu err %llu lateTicks %llu hang %llu",
                  (unsigned long long)st.sent, (unsigned long long)st.late, (unsigned long long)st.dropped_gone, (unsigned long long)st.dropped_nomap, (unsigned long long)st.dropped_cleared,
                  (unsigned long long)st.queue_full, (unsigned long long)st.max_queue, (unsigned long long)st.send_errors, (unsigned long long)st.dropped_late_ticks, (unsigned long long)st.may_hang);
    s += b;
    std::snprintf(b, sizeof b, "\ncore: ticks %llu starts %llu stops %llu ringFull %llu lateNotes %llu | map exact %llu fallback %llu odd %llu offset %lld frames | held %llu defFull %llu defFail %llu short %llu early>3B %llu heldCleared %llu | sender prio %s",
                  (unsigned long long)cs.ticks, (unsigned long long)cs.starts, (unsigned long long)cs.stops, (unsigned long long)cs.ring_full, (unsigned long long)cs.late_notes,
                  (unsigned long long)mapExact.load(), (unsigned long long)mapFallback.load(), (unsigned long long)mapOdd.load(), (long long)lastOffsetFrames.load(),
                  (unsigned long long)deferred.load(), (unsigned long long)deferFull.load(), (unsigned long long)deferFail.load(), (unsigned long long)shortWrite.load(), (unsigned long long)earlyLong.load(), (unsigned long long)heldCleared.load(),
                  prio_.load() == 0 ? "ok" : prio_.load() == 1 ? "refused" : "not tried");
    s += b;
  }
  s += "\ndevices:";
  if (devs_.empty()) s += " none";
  for (const auto& d : devs_) { std::snprintf(b, sizeof b, "\n  %s%s", d.id.c_str(), d.ts ? "  [timestamps]" : ""); s += b; }
  return s;
}

// ---- MidiHost: sender ----------------------------------------------------------------------------------------------------------------------
// SENDER thread only. The pump hands a message to the sink up to 20 ms early (CAP_TS is per sink). A port that cannot stamp (Bluetooth, virtual: not measured) must not get it
// early: it is held here (fixed array, sender-owned, no lock) and sent when its time comes. ORDER: held messages are in arrival order, which the pump makes time order. Before sendTo
// sends or holds anything for such a port it first sends every held message that is due (flushDeferred), so a message that is sent at once can never overtake an earlier held one:
// the held ones still waiting are all later than `at`, because `at` is not before any earlier message's time. A CLEAR (clearDev) drops the not-yet-due held messages of its device,
// the same as the core drops its own queue; what the core replays after a clear (the messages before the marker) comes back through sendTo and is held again.
int MidiHost::sendTo(uint64_t dev, const uint8_t* b, int len, int64_t at) {
  PortTable::Entry e = table_.find(dev);
  if (!e.io) { sendUnknown.fetch_add(1, std::memory_order_relaxed); return -1; }
  if (e.ts) { const int r = e.io->send(b, len, at); return r < 0 ? -1 : 0; }
  const int64_t now = nowFn_();
  flushDeferred(now);   // due held messages go first (F7)
  if (at <= now + 50000 || len < 1) { const int r = e.io->send(b, len, 0); return r < 0 ? -1 : 0; }
  if (len > 3) { earlyLong.fetch_add(1, std::memory_order_relaxed); const int r = e.io->send(b, len, 0); return r < 0 ? -1 : 0; }   // SysEx: not held (Held::b is 3 bytes)
  if (nHeld_ >= kHeld) { deferFull.fetch_add(1, std::memory_order_relaxed); const int r = e.io->send(b, len, 0); return r < 0 ? -1 : 0; }
  Held& h = held_[nHeld_++];
  h.io = std::move(e.io); h.dev = dev; h.len = (uint8_t)len; std::memcpy(h.b, b, (size_t)len); h.at = at;
  heldCount_.store(nHeld_, std::memory_order_release);
  deferred.fetch_add(1, std::memory_order_relaxed);
  return 0;
}

// sink.clear: the core calls it (from the pump, so on this thread) when a CLEAR meets a message of this device handed over for later, and wants to know what was recalled (midiport.h).
// A STAMPED port: nothing is recalled (no AMidi flush: it could discard a note-off the core will not replay), so INT64_MAX: the core replays nothing and releases the note-ons that
// still play. A port WITHOUT stamps: the held messages that are not yet due are dropped (recalled) and the ones due are sent now; the return value is the time up to which nothing was
// recalled: now + 50 us, the same rule as sendTo / flushDeferred (a message at or before it has been sent or is sent here). So the core replays exactly what was held and dropped,
// never what was sent, whatever the 50 us rule or flushDeferred did before this call, and it releases the note-ons that were sent after its marker.
int64_t MidiHost::clearDev(uint64_t dev) {
  PortTable::Entry e = table_.find(dev);
  if (e.io && e.ts) return INT64_MAX;
  const int64_t R = nowFn_() + 50000;
  int w = 0;
  for (int i = 0; i < nHeld_; i++) {
    Held& h = held_[i];
    if (h.dev == dev) {
      if (h.at <= R) { if (h.io->send(h.b, h.len, 0) < 0) deferFail.fetch_add(1, std::memory_order_relaxed); }
      else heldCleared.fetch_add(1, std::memory_order_relaxed);
      h.io.reset();
      continue;
    }
    if (w != i) held_[w] = std::move(h);
    w++;
  }
  nHeld_ = w;
  heldCount_.store(w, std::memory_order_release);
  return R;
}

void MidiHost::flushAllHeld() {
  for (int i = 0; i < nHeld_; i++) { Held& h = held_[i]; if (h.io->send(h.b, h.len, 0) < 0) deferFail.fetch_add(1, std::memory_order_relaxed); h.io.reset(); }
  nHeld_ = 0;
  heldCount_.store(0, std::memory_order_release);
}

int64_t MidiHost::flushDeferred(int64_t now) {
  int64_t next = INT64_MAX;
  // in time order (stable): the held list is in arrival order, which the pump makes time order
  int w = 0;
  for (int i = 0; i < nHeld_; i++) {
    Held& h = held_[i];
    if (h.at <= now + 50000) {
      if (h.io->send(h.b, h.len, 0) < 0) deferFail.fetch_add(1, std::memory_order_relaxed);
      h.io.reset();
    } else {
      if (w != i) { held_[w] = std::move(h); }
      if (held_[w].at < next) next = held_[w].at;
      w++;
    }
  }
  nHeld_ = w;
  heldCount_.store(w, std::memory_order_release);
  return next;
}

void MidiHost::senderMain() {
#if defined(__ANDROID__)
  tid_.store((int)gettid(), std::memory_order_release);
  prio_.store(setpriority(PRIO_PROCESS, (id_t)gettid(), -19) == 0 ? 0 : 1, std::memory_order_release);   // ANDROID_PRIORITY_URGENT_AUDIO; Kotlin also asks Process.setThreadPriority(tid, URGENT_AUDIO)
#elif defined(__linux__)
  tid_.store((int)syscall(SYS_gettid), std::memory_order_release);
#endif
  if (g_enter) g_enter();
  const int64_t kMaxSleep = 2000000, kEarly = 300000;
  while (!stop_.load(std::memory_order_acquire)) {
    int64_t next = INT64_MAX;
    thunder_midiout_pump(o_, &next);
    int64_t now = nowFn_();
    const int64_t nd = flushDeferred(now);
    now = nowFn_();
    int64_t until = now + kMaxSleep;
    if (next < until) until = next;
    if (nd != INT64_MAX && nd - kEarly < until) until = nd - kEarly;
    if (until - now > 20000) {
      timespec ts; ts.tv_sec = (time_t)(until / 1000000000LL); ts.tv_nsec = (long)(until % 1000000000LL);
      clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, nullptr);
    } else sched_yield();
  }
  // told to stop: hand over whatever the core has due, then send every held message at once, in order (a Stop or a note-off must not be lost with the array)
  { int64_t next = INT64_MAX; thunder_midiout_pump(o_, &next); }
  flushAllHeld();
  if (g_leave) g_leave();
}

// ---- MidiHost: audio --------------------------------------------------------------------------------------------------------------------------
void MidiHost::audioBlock(int frames, int64_t t0) {
  if (!o_ || !h_ || frames <= 0) return;   // set before the callback starts, cleared after it stopped
  const int64_t F = frames_;
  const double now = (double)F / sr_, bs = (double)frames / sr_;
  const int64_t mono = t0 > 0 ? t0 : nowFn_();   // the callback's start (spec 3.4)
  using namespace thunder;
  DebugSong* d = dbg_.load(std::memory_order_acquire);
  midi_begin(h_, now, bs > 1 ? 1 : bs, 0.0, ~(uint64_t)0);
  if (d && d->state.load(std::memory_order_relaxed) != DebugSong::FINISHED) {
    if (d->state.load(std::memory_order_relaxed) == DebugSong::ARMED) { d->seq->setTime(now + 0.5); d->runner.start(d->seq.get()); d->state.store(DebugSong::RUNNING, std::memory_order_relaxed); }
    Seq* seq = d->seq.get();
    d->runner.pump(now, 0.25, d->evs.data(), (int)d->evs.size(), &d->st, [&](const SeqEvent* e, int n, const SeqStep& step) { midi_sched(h_).onStep(e, n, step, *seq, now); });
    midi_end(h_, now, seq->time(), seq->bpm(), seq->alive());
    if (!seq->alive() && now > seq->time() + 0.3) {   // the song is over: Stop (clock Stop, notes off, CLEAR), as the web's frame() does
      d->runner.stop(); midi_sched(h_).stop(now);
      dbg_.store(nullptr, std::memory_order_release);
      d->state.store(DebugSong::FINISHED, std::memory_order_release);   // the last touch: control may free it now
    }
  } else {
    midi_end(h_, now, now, 120, false);
  }
  if (!mapped_ || mono - lastMapNs_ >= 20000000LL) publishMap(F, F, mono);
  frames_ = F + frames;
}

// The clock map: core (ENGINE) frame `coreFrame` is AUDIBLE at host time host0. `hostFrame` is the same block start on the host's own count (the one AAudio's frames written counts against); in
// the debug path the two are equal. At most every 20 ms (AAudioStream_getTimestamp is not free), always at the first callback. AUDIO thread.
void MidiHost::publishMap(int64_t coreFrame, int64_t hostFrame, int64_t mono) {
  int64_t frame0 = coreFrame, host0 = mono + outLatNs_.load(std::memory_order_relaxed); bool exact = false;   // fallback: the block starts to be heard one output latency (the floor) after the callback
  TsPair p;
  if (tsFn_ && tsFn_(tsUser_, &p)) {
    const int64_t off = p.written - hostFrame;   // 0 in steady state if AAudio's frames written excludes the block being filled (UNVERIFIED on the device: reported in status)
    if (p.ns > mono - 2000000000LL && p.ns < mono + 200000000LL) { frame0 = p.pos - off - (hostFrame - coreFrame); host0 = p.ns; exact = true; lastOffsetFrames.store(off, std::memory_order_relaxed); }
    else mapOdd.fetch_add(1, std::memory_order_relaxed);
  }
  (exact ? mapExact : mapFallback).fetch_add(1, std::memory_order_relaxed);
  thunder_midiout_clock_map(o_, frame0, host0);
  mapped_ = true; lastMapNs_ = mono; mapReady_.store(true, std::memory_order_release);
}

void MidiHost::audioBlockLive(int frames, int64_t hostFrame0, int64_t offset, int64_t t0) {
  if (!o_ || !h_ || frames <= 0) return;
  const int64_t mono = t0 > 0 ? t0 : nowFn_();
  const bool force = remap_.exchange(false, std::memory_order_acq_rel);
  if (force || !mapped_ || mono - lastMapNs_ >= 20000000LL) publishMap(hostFrame0 - offset, hostFrame0, mono);
}

std::string toJniUtf8(const std::string& s) {
  std::string o; o.reserve(s.size() + 8);
  auto put3 = [&](unsigned c) { o += (char)(0xE0 | (c >> 12)); o += (char)(0x80 | ((c >> 6) & 0x3F)); o += (char)(0x80 | (c & 0x3F)); };
  const size_t n = s.size();
  for (size_t i = 0; i < n;) {
    const unsigned char c = (unsigned char)s[i];
    unsigned cp = 0; size_t len = 0;
    if (c < 0x80) { if (c == 0) { o += (char)0xC0; o += (char)0x80; } else o += (char)c; i++; continue; }
    else if (c >= 0xC2 && c <= 0xDF) { len = 2; cp = c & 0x1F; }
    else if (c >= 0xE0 && c <= 0xEF) { len = 3; cp = c & 0x0F; }
    else if (c >= 0xF0 && c <= 0xF4) { len = 4; cp = c & 0x07; }
    bool ok = len != 0 && i + len <= n;
    for (size_t k = 1; ok && k < len; k++) { const unsigned char d = (unsigned char)s[i + k]; if ((d & 0xC0) != 0x80) ok = false; else cp = (cp << 6) | (d & 0x3F); }
    if (ok && ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && (cp < 0x10000 || cp > 0x10FFFF)) || (cp >= 0xD800 && cp <= 0xDFFF))) ok = false;
    if (!ok) { o += '?'; i++; continue; }
    if (len == 4) { const unsigned v = cp - 0x10000; put3(0xD800 | (v >> 10)); put3(0xDC00 | (v & 0x3FF)); }
    else o.append(s, i, len);
    i += len;
  }
  return o;
}

MidiHost& host() { static MidiHost h; return h; }

}  // namespace tpa
