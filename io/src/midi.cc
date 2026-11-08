// thunder_io.node, phase B: MIDI out and in through libremidi (CoreMIDI, WinMM, ALSA where its headers were there at build
// time; the dummy backend otherwise). See docs/NATIVE-IO-HELPER-PLAN.md §3.1, §4 (Phase B).
//
// OUT: midiSend(port, bytes, atNs) queues a message for an OS-clock time (HostNowNs, the clock now() and the input's
// timestamps use). ONE sender thread, at the highest priority the OS gives a normal app, sends each message at its time:
// it sleeps until ~0.5 ms before, then spins the rest. midiClear(port) drops everything still queued for that port (STOP:
// nothing booked ahead plays after it), so the page needs no OUTBOX of its own on this path. Messages for the same time
// keep their order (a multimap inserts equal keys after the ones already there).
// IN: a port opened with midiOpenIn() calls back on libremidi's thread; each message is stamped with HostNowNs() there and
// queued; midiRead() hands over everything that came since the last call.
// TEST PORT: "test:loop" (in midiPorts() only when THUNDER_IO_TEST is set, but openable always) is an output whose messages,
// when the sender thread sends them, come back from midiRead() as if from an input "test:loop", stamped with the time they
// went out: the scheduling can be tested on a machine with no MIDI at all (io/test/midi.js).
//
// Plain N-API. libremidi needs C++ exceptions (binding.gyp turns them on); every call into it is inside a try here, so none
// ever reaches N-API: a throw becomes {ok:false, why} or is dropped.

#if defined(__linux__) && __has_include(<alsa/asoundlib.h>)
  #define LIBREMIDI_ALSA 1   // (loaded at run time with dlopen by libremidi: nothing extra is linked)
#endif
#if defined(__APPLE__)
  #define LIBREMIDI_COREMIDI 1
#endif
#if defined(_WIN32)
  #define LIBREMIDI_WINMM 1
#endif
#define LIBREMIDI_HEADER_ONLY 1
#include <libremidi/libremidi.hpp>

#include <node_api.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(__APPLE__)
  #include <pthread.h>
  #include <pthread/qos.h>
#elif defined(_WIN32)
  #include <windows.h>
  #include <timeapi.h>
#endif

uint64_t HostNowNs(void);   // thunder_io.cc

namespace {

// ---------------------------------------------------------------------------------------------------------------------
// Ports

struct PortList { std::vector<libremidi::output_port> outs; std::vector<libremidi::input_port> ins; };

// A port's id: its display name, made unique with " #2", " #3"… when two share one (the page keeps it per port letter)
template <class P> std::vector<std::string> Ids(const std::vector<P>& ps) {
  std::vector<std::string> out; std::map<std::string, int> seen;
  for (auto& p : ps) { std::string n = p.display_name.empty() ? p.port_name : p.display_name; int k = ++seen[n];
    out.push_back(k == 1 ? n : n + " #" + std::to_string(k)); }
  return out;
}
PortList Scan() {
  PortList L;
  try { libremidi::observer obs; L.outs = obs.get_output_ports(); L.ins = obs.get_input_ports(); } catch (...) { L = PortList{}; }
  return L;
}
bool TestPorts() { const char* e = getenv("THUNDER_IO_TEST"); return e && *e && strcmp(e, "0") != 0; }
const char* kLoop = "test:loop";

// ---------------------------------------------------------------------------------------------------------------------
// Inbound (MIDI in, and the test port's loop)

struct InMsg { int port; std::vector<uint8_t> bytes; uint64_t ns; };
// (every shared object lives on the heap and is never freed: at process exit the sender thread may still run while C++
// destroys globals, which aborted Node; the OS frees it all anyway)
std::mutex& g_inMx = *new std::mutex;
std::vector<InMsg>& g_inQ = *new std::vector<InMsg>;
void PushIn(int port, const uint8_t* b, size_t n, uint64_t ns) {
  std::lock_guard<std::mutex> l(g_inMx);
  if (g_inQ.size() > 20000) return;   // nobody is reading: drop rather than grow without end
  g_inQ.push_back({port, std::vector<uint8_t>(b, b + n), ns});
}

// ---------------------------------------------------------------------------------------------------------------------
// Outbound: the ports and the sender thread

struct Out { std::unique_ptr<libremidi::midi_out> dev; bool loop = false; int loopIn = 0; };
struct Msg { int port; uint64_t seq; std::vector<uint8_t> bytes; };

std::mutex& g_mx = *new std::mutex;    // guards everything below
std::condition_variable& g_cv = *new std::condition_variable;
std::map<int, Out>& g_outs = *new std::map<int, Out>;
std::map<int, std::unique_ptr<libremidi::midi_in>>& g_ins = *new std::map<int, std::unique_ptr<libremidi::midi_in>>;
std::multimap<uint64_t, Msg>& g_q = *new std::multimap<uint64_t, Msg>;   // at (ns) -> message
uint64_t g_seq = 0;
int g_next = 1;
std::thread* g_thread = nullptr;
bool g_run = false;
std::atomic<uint64_t> g_late{0};       // messages sent more than 2 ms after their time (the page can show it)

void Priority() {
#if defined(__APPLE__)
  pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
  timeBeginPeriod(1);
  SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

void SendNow(int port, const std::vector<uint8_t>& b, uint64_t at) {
  // (g_mx held) the device sends straight away; the test port loops back into midiRead()
  auto it = g_outs.find(port); if (it == g_outs.end()) return;
  uint64_t now = HostNowNs(); if (now > at + 2000000) g_late++;
  if (it->second.loop) { PushIn(it->second.loopIn ? it->second.loopIn : port, b.data(), b.size(), now); return; }
  if (it->second.dev) { try { it->second.dev->send_message(b.data(), b.size()); } catch (...) {} }
}

void Loop() {
  Priority();
  std::unique_lock<std::mutex> l(g_mx);
  while (g_run) {
    if (g_q.empty()) { g_cv.wait(l); continue; }
    uint64_t at = g_q.begin()->first, now = HostNowNs();
    if (at > now + 600000) {   // more than 0.6 ms away: sleep until 0.5 ms before (a new earlier message wakes it)
      g_cv.wait_for(l, std::chrono::nanoseconds(at - now - 500000)); continue;
    }
    if (at > now) {            // the last half millisecond: spin, without the lock (a clear may come in meanwhile)
      l.unlock(); while (HostNowNs() < at) std::this_thread::yield(); l.lock(); continue;
    }
    while (!g_q.empty() && g_q.begin()->first <= HostNowNs()) {   // everything due, in order
      auto n = g_q.extract(g_q.begin()); SendNow(n.mapped().port, n.mapped().bytes, n.key());
    }
  }
}
void StartThread() {   // (g_mx held)
  if (g_run) return;
  g_run = true; g_thread = new std::thread(Loop);
}
void StopAll(void*) {   // the environment is going away: stop the thread, close every port
  { std::lock_guard<std::mutex> l(g_mx); g_run = false; g_q.clear(); }
  g_cv.notify_all();
  if (g_thread) { if (g_thread->joinable()) g_thread->join(); delete g_thread; g_thread = nullptr; }
  std::lock_guard<std::mutex> l(g_mx);
  try { g_outs.clear(); g_ins.clear(); } catch (...) {}
}

// ---------------------------------------------------------------------------------------------------------------------
// N-API helpers

napi_value Str(napi_env env, const std::string& s) { napi_value v; napi_create_string_utf8(env, s.c_str(), s.size(), &v); return v; }
napi_value Num(napi_env env, double d) { napi_value v; napi_create_double(env, d, &v); return v; }
napi_value Undef(napi_env env) { napi_value v; napi_get_undefined(env, &v); return v; }
napi_value Res(napi_env env, bool ok, const std::string& why, int handle) {
  napi_value o, b; napi_create_object(env, &o); napi_get_boolean(env, ok, &b); napi_set_named_property(env, o, "ok", b);
  if (ok) napi_set_named_property(env, o, "port", Num(env, handle)); else napi_set_named_property(env, o, "why", Str(env, why));
  return o;
}
bool Args(napi_env env, napi_callback_info info, size_t want, napi_value* argv) {
  size_t argc = want; napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr); return argc >= want;
}
std::string GetStr(napi_env env, napi_value v) {
  size_t n = 0; if (napi_get_value_string_utf8(env, v, nullptr, 0, &n) != napi_ok) return "";
  std::string s(n, '\0'); napi_get_value_string_utf8(env, v, s.data(), n + 1, &n); return s;
}
int GetInt(napi_env env, napi_value v) { int32_t i = 0; napi_get_value_int32(env, v, &i); return i; }
// bytes: a Uint8Array or an array of numbers
bool GetBytes(napi_env env, napi_value v, std::vector<uint8_t>& out) {
  bool ta = false; napi_is_typedarray(env, v, &ta);
  if (ta) { napi_typedarray_type t; size_t n; void* d; napi_value ab; size_t off;
    napi_get_typedarray_info(env, v, &t, &n, &d, &ab, &off); if (t != napi_uint8_array) return false;
    out.assign((uint8_t*)d, (uint8_t*)d + n); return true; }
  bool arr = false; napi_is_array(env, v, &arr); if (!arr) return false;
  uint32_t n = 0; napi_get_array_length(env, v, &n); out.resize(n);
  for (uint32_t i = 0; i < n; i++) { napi_value e; napi_get_element(env, v, i, &e); out[i] = (uint8_t)(GetInt(env, e) & 0xFF); }
  return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// The functions

// midiPorts() -> {outs:[{id, name}], ins:[{id, name}]}
napi_value MidiPorts(napi_env env, napi_callback_info) {
  PortList L = Scan(); auto oi = Ids(L.outs), ii = Ids(L.ins);
  napi_value r, outs, ins; napi_create_object(env, &r); napi_create_array(env, &outs); napi_create_array(env, &ins);
  auto add = [&](napi_value a, uint32_t i, const std::string& id) { napi_value o; napi_create_object(env, &o);
    napi_set_named_property(env, o, "id", Str(env, id)); napi_set_named_property(env, o, "name", Str(env, id)); napi_set_element(env, a, i, o); };
  uint32_t k = 0; for (auto& id : oi) add(outs, k++, id);
  if (TestPorts()) add(outs, k++, kLoop);
  k = 0; for (auto& id : ii) add(ins, k++, id);
  if (TestPorts()) add(ins, k++, kLoop);
  napi_set_named_property(env, r, "outs", outs); napi_set_named_property(env, r, "ins", ins);
  return r;
}

// midiOpenOut(id) -> {ok, port} | {ok:false, why}
napi_value MidiOpenOut(napi_env env, napi_callback_info info) {
  napi_value a[1]; if (!Args(env, info, 1, a)) return Res(env, false, "no port id", 0);
  std::string id = GetStr(env, a[0]);
  Out o;
  if (id == kLoop) o.loop = true;
  else {
    PortList L = Scan(); auto ids = Ids(L.outs); size_t i = 0; for (; i < ids.size() && ids[i] != id; i++) {}
    if (i == ids.size()) return Res(env, false, "no MIDI output called " + id, 0);
    try {
      auto dev = std::make_unique<libremidi::midi_out>(libremidi::output_configuration{});
      if (auto err = dev->open_port(L.outs[i]); err != stdx::error{}) return Res(env, false, "couldn't open " + id, 0);
      o.dev = std::move(dev);
    } catch (...) { return Res(env, false, "couldn't open " + id, 0); }
  }
  std::lock_guard<std::mutex> l(g_mx);
  int h = g_next++; g_outs[h] = std::move(o); StartThread();
  return Res(env, true, "", h);
}

// midiOpenIn(id) -> {ok, port} | {ok:false, why}. "test:loop": the port the test output's messages come back from
napi_value MidiOpenIn(napi_env env, napi_callback_info info) {
  napi_value a[1]; if (!Args(env, info, 1, a)) return Res(env, false, "no port id", 0);
  std::string id = GetStr(env, a[0]);
  std::lock_guard<std::mutex> l(g_mx);
  int h = g_next++;
  if (id == kLoop) { for (auto& [k, o] : g_outs) if (o.loop) o.loopIn = h; g_ins[h] = nullptr; return Res(env, true, "", h); }
  PortList L = Scan(); auto ids = Ids(L.ins); size_t i = 0; for (; i < ids.size() && ids[i] != id; i++) {}
  if (i == ids.size()) return Res(env, false, "no MIDI input called " + id, 0);
  libremidi::input_configuration cfg;
  cfg.on_message = [h](const libremidi::message& m) { if (!m.bytes.empty()) PushIn(h, m.bytes.data(), m.bytes.size(), HostNowNs()); };
  cfg.ignore_sysex = false; cfg.ignore_timing = false; cfg.ignore_sensing = true;   // clock in needs the timing messages
  try {
    auto dev = std::make_unique<libremidi::midi_in>(cfg);
    if (auto err = dev->open_port(L.ins[i]); err != stdx::error{}) return Res(env, false, "couldn't open " + id, 0);
    g_ins[h] = std::move(dev);
  } catch (...) { return Res(env, false, "couldn't open " + id, 0); }
  return Res(env, true, "", h);
}

// midiClose(port): an output or an input (anything queued for an output is dropped)
napi_value MidiClose(napi_env env, napi_callback_info info) {
  napi_value a[1]; if (!Args(env, info, 1, a)) return Undef(env);
  int h = GetInt(env, a[0]); std::unique_ptr<libremidi::midi_in> in;
  { std::lock_guard<std::mutex> l(g_mx);
    for (auto it = g_q.begin(); it != g_q.end();) it = it->second.port == h ? g_q.erase(it) : std::next(it);
    try { g_outs.erase(h); } catch (...) {}
    auto i = g_ins.find(h); if (i != g_ins.end()) { in = std::move(i->second); g_ins.erase(i); } }
  try { in.reset(); } catch (...) {}   // (closed outside the lock: its callback may be waiting on g_inMx, never on g_mx)
  return Undef(env);
}

// midiSend(port, bytes, atNs): at OS time atNs (0 or past: now, after anything already due). Returns false for an unknown port or empty bytes
napi_value MidiSend(napi_env env, napi_callback_info info) {
  napi_value a[3]; size_t argc = 3; napi_get_cb_info(env, info, &argc, a, nullptr, nullptr);
  napi_value f; napi_get_boolean(env, false, &f); if (argc < 2) return f;
  int h = GetInt(env, a[0]); std::vector<uint8_t> b; if (!GetBytes(env, a[1], b) || b.empty()) return f;
  double at = 0; if (argc >= 3) napi_get_value_double(env, a[2], &at);
  { std::lock_guard<std::mutex> l(g_mx);
    if (!g_outs.count(h)) return f;
    uint64_t now = HostNowNs(), t = at > (double)now ? (uint64_t)at : now;   // 0 or past: now, in the order sent
    bool first = g_q.empty() || t < g_q.begin()->first;
    g_q.emplace(t, Msg{h, ++g_seq, std::move(b)});
    if (first) g_cv.notify_all(); }
  napi_value t; napi_get_boolean(env, true, &t); return t;
}

// midiClear(port): drops everything still queued for it (port 0: every port). Returns how many were dropped
napi_value MidiClear(napi_env env, napi_callback_info info) {
  napi_value a[1]; size_t argc = 1; napi_get_cb_info(env, info, &argc, a, nullptr, nullptr);
  int h = argc ? GetInt(env, a[0]) : 0; double n = 0;
  { std::lock_guard<std::mutex> l(g_mx);
    for (auto it = g_q.begin(); it != g_q.end();) if (!h || it->second.port == h) { it = g_q.erase(it); n++; } else ++it; }
  return Num(env, n);
}

// midiRead() -> [{port, bytes:[…], ns}] (everything since the last call) | null when nothing came
napi_value MidiRead(napi_env env, napi_callback_info) {
  std::vector<InMsg> q; { std::lock_guard<std::mutex> l(g_inMx); q.swap(g_inQ); }
  if (q.empty()) { napi_value n; napi_get_null(env, &n); return n; }
  napi_value arr; napi_create_array_with_length(env, q.size(), &arr);
  for (uint32_t i = 0; i < q.size(); i++) {
    napi_value o, b; napi_create_object(env, &o); napi_create_array_with_length(env, q[i].bytes.size(), &b);
    for (uint32_t j = 0; j < q[i].bytes.size(); j++) napi_set_element(env, b, j, Num(env, q[i].bytes[j]));
    napi_set_named_property(env, o, "port", Num(env, q[i].port)); napi_set_named_property(env, o, "bytes", b);
    napi_set_named_property(env, o, "ns", Num(env, (double)q[i].ns)); napi_set_element(env, arr, i, o);
  }
  return arr;
}

// midiStats() -> {queued, late}: how many wait, how many went out more than 2 ms late (since loading)
napi_value MidiStats(napi_env env, napi_callback_info) {
  napi_value o; napi_create_object(env, &o); size_t n; { std::lock_guard<std::mutex> l(g_mx); n = g_q.size(); }
  napi_set_named_property(env, o, "queued", Num(env, (double)n)); napi_set_named_property(env, o, "late", Num(env, (double)g_late.load()));
  return o;
}

void Fn(napi_env env, napi_value exports, const char* name, napi_callback cb) {
  napi_value fn; napi_create_function(env, name, NAPI_AUTO_LENGTH, cb, nullptr, &fn); napi_set_named_property(env, exports, name, fn);
}

}  // namespace

// Called from thunder_io.cc's Init
void MidiExport(napi_env env, napi_value exports) {
  Fn(env, exports, "midiPorts", MidiPorts);
  Fn(env, exports, "midiOpenOut", MidiOpenOut);
  Fn(env, exports, "midiOpenIn", MidiOpenIn);
  Fn(env, exports, "midiClose", MidiClose);
  Fn(env, exports, "midiSend", MidiSend);
  Fn(env, exports, "midiClear", MidiClear);
  Fn(env, exports, "midiRead", MidiRead);
  Fn(env, exports, "midiStats", MidiStats);
  napi_add_env_cleanup_hook(env, StopAll, nullptr);
}
