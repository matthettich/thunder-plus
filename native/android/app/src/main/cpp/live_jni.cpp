// N2 H1a: the Android glue of the LIVE host: miniaudio (AAudio) opens the output and calls tpa::LiveEngine::render; JNI entry points for Kotlin's LiveNative (LiveNative.kt).
// Beside the N0 glue (jni.cpp, its own device and its own thunder_core): the two are never started together (Kotlin stops the one before starting the other). Everything with a decision in it
// is in live_engine.cpp / pad_filler.cpp, which a Linux test runs; this file is the part only a phone can run (UNVERIFIED on a device: CI compiles it).
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <pthread.h>
#include <time.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "a5_xrun.h"
#include "engine_slot.h"
#include "live_engine.h"
#include "midi_in.h"
#include "midi_out.h"
#include "miniaudio.h"
#include "pad_block.h"
#include "pad_filler.h"
#include "songfit.h"
#include "rt/json.h"
#include "thunder/rtlog.h"
#ifdef THUNDER_HAVE_LXR
#include "thunder/lxr_seam.h"
#include "thunder/lxr_module.h"   // the optional drum module (native/lxr): registered once at start
#endif

#define LOG_TAG "ThunderLive"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

struct Live {
  tpa::Slot<tpa::LiveEngine> eng;                    // ANY thread reads it through get() (a copy for the whole call); only start / teardown (g_life) replace it. The MIDI control runner and the MIDI-in sink hold copies too.
  std::atomic<tpa::LiveEngine*> audioEng{nullptr};   // the audio callback's raw pointer: set before the stream starts, cleared after it stopped (the callback takes no lock and copies no shared_ptr)
  ma_device device;
  std::atomic<bool> deviceInit{false};
  void* aaLib = nullptr;
  int32_t (*aaXrun)(void*) = nullptr;
  int32_t (*aaBuf)(void*) = nullptr;
  int32_t (*aaTs)(void*, int32_t, int64_t*, int64_t*) = nullptr;   // H2: the clock map's AAudio pair (read by the audio callback only)
  int64_t (*aaWritten)(void*) = nullptr;
  std::atomic<bool> midiOn{false};     // H2: the MIDI host runs on this engine. Set before the stream starts, cleared after it stopped (the callback reads it plainly)
  std::atomic<int64_t> lastLatNs{-1};   // the latency floor the MIDI host has accepted (set by the poller's tick hook only on rc 0)
  std::atomic<int64_t> pendLatNs{-1};   // the floor nativeStatus wants applied: the poller thread does it (B1: no caller thread waits on the control thread), and retries while it answers BUSY (B5)
  int64_t rev = 0;
  std::vector<int> lastSlots;   // pads the previous project filled (to remove the ones a reload does not fill)
  // H1b: what the page last sent (song.load, kit.load): the texts SongFit and the kit entries read. Guarded by g_life.
  std::shared_ptr<std::string> songText, kitText;
  std::shared_ptr<thunder::JsonDoc> kitDoc;   // parses kitText (its Json refers into it: both live as long as this)
  double bpm = 120, lpb = 4;
};
Live g;
std::mutex g_dev;    // the ma_device fields other threads read (sample rate, the AAudio stream pointer, its state): held by start / teardown / reopen around init, start and uninit; the ANY-thread readers (status, stream state) only TRY it and skip a round while the stream is rebuilt
std::mutex g_life;   // start / shutdown / the state a load reads and writes: short holds ONLY. A load does NOT hold it across renderPads or the engine call (a shutdown must not wait 20 s behind a load): ANR fix 25 Nov
std::mutex g_loadM;  // the three loads (project / song / kit) run one at a time, as g_life made them before; shutdown never takes it
rtlog g_lrt;         // A5's clock-drift logger on the live callback
tpa::XrunGate g_xrun;   // the stream's xrun count since the A5 run began: fed to g_lrt by the status poll (verify-n2 F2)
std::atomic<double> g_load{0.0};   // CORE-BRIDGE 7 G9: the callback's time over its period, peak-held with decay (written by the audio thread, read by Kotlin)

std::mutex g_inMu;
std::shared_ptr<tpa::LiveEngine> g_inEng;   // the engine the MIDI-in sink pushes to (a copy under g_inMu: the reader thread never meets a freed one)

inline int64_t monoNs() { timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec; }

// THE AUDIO CALLBACK: the logger, then LiveEngine::render (flush-to-zero, thunder_live_process, interleave). No allocation, no lock, no JNI, no log.
void dataCallback(ma_device* dev, void* out, const void*, ma_uint32 frames) {
  const int64_t t0 = monoNs();
  Live* lv = static_cast<Live*>(dev->pUserData);
  tpa::LiveEngine* e = lv->audioEng.load(std::memory_order_acquire);
  if (!e) { std::memset(out, 0, sizeof(float) * 2 * (size_t)frames); return; }
  rtlog_begin(&g_lrt, t0);
  const int64_t hostF = e->hostFrameNow();
  e->render(static_cast<float*>(out), (int)frames, t0);
  if (lv->midiOn) tpa::host().audioBlockLive((int)frames, hostF, e->engineOffsetRT(), t0);   // H2: AFTER process, the clock map (engine frame = host frame - offset); the sender thread pumps
  const int64_t t1 = monoNs();
  rtlog_end(&g_lrt, t1, (int)frames);
  const double period = (double)frames / (double)dev->sampleRate, work = (double)(t1 - t0) * 1e-9, prev = g_load.load(std::memory_order_relaxed);
  const double now = work / period;
  g_load.store(now > prev ? now : prev * 0.95 + now * 0.05, std::memory_order_relaxed);   // no allocation, no lock
}

// miniaudio's notification callback. It runs on miniaudio's OWN thread (the job thread that re-routed the stream, or the thread that started / stopped it): atomics only, no lock, no JNI, no log.
// After a re-route (headphones, Bluetooth: miniaudio closed the stream and opened another by itself) AAudio's xrun counter belongs to the new stream and restarts at 0: tell the A5 gate.
void onDeviceNotification(const ma_device_notification* n) {
  if (n->type == ma_device_notification_type_rerouted) {
    g_xrun.newStream();
    if (g.midiOn.load(std::memory_order_relaxed)) tpa::host().remap();   // a new AAudio stream is a new timestamp pair: MidiHost::remap() is one atomic store (midi_out.h); host() is a function-local static, already built by the engine start
  }
}

// H2: the AAudio timestamp pair for the MIDI clock map (spec 3.4). Called by audioBlockLive on the audio thread, at most every 20 ms: the only reader of getTimestamp.
// It does NOT take rerouteLock (lead's read 25 Nov, A): the reroute job holds that lock while it closes the stream, and AAudioStream_close waits for the running data callback, so a lock here would
// deadlock. It needs none: this runs INSIDE the callback of the stream it reads, and AAudio does not free a stream whose callback is running (close joins it); a stream that is being replaced answers an error.
bool liveTsRead(void*, tpa::TsPair* p) {
  void* stream = (void*)g.device.aaudio.pStreamPlayback;
  if (!stream || !g.aaTs || !g.aaWritten) return false;
  int64_t pos = 0, ns = 0;
  if (g.aaTs(stream, CLOCK_MONOTONIC, &pos, &ns) != 0) return false;
  p->pos = pos; p->ns = ns; p->written = g.aaWritten(stream);
  return true;
}

// H2: a message from a device's MIDI port: one bridge event. `f` = the host frame of that moment (the CLOCK events map it to wall time); note messages are kept when the queue must drop.
void midiInSink(uint64_t token, int64_t ns, const tpa::MidiInMsg& m) {
  std::shared_ptr<tpa::LiveEngine> e;
  { std::lock_guard<std::mutex> lk(g_inMu); e = g_inEng; }
  if (!e) return;
  const int64_t now = ns > 0 ? ns : monoNs();
  e->pushEvent(tpa::midiInJson(e->hostFrameAt(now), now, token, m), tpa::midiInIsNote(m));
}

std::string jsonEscape(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; } return o; }

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeCaps(JNIEnv*, jobject) { return (jint)thunder_live_caps(); }

// H2: the MIDI host runs on this engine (its start, MIDI out and MIDI in).
JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeMidiOn(JNIEnv*, jobject) { return (g.deviceInit && g.eng.get() && g.midiOn) ? JNI_TRUE : JNI_FALSE; }

// Opens the output, creates the live host at the stream's rate, starts the control and poller threads, then the stream. Returns the sample rate, or a negative code.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeStart(JNIEnv*, jobject, jboolean chains) {
  std::lock_guard<std::mutex> lk(g_life);
  if (g.deviceInit) return (jint)g.device.sampleRate;
#ifdef THUNDER_HAVE_LXR
  thunder_lxr_module_register();   // idempotent registry of one pointer (lxr_seam.h); pads of the LXR engines are rendered through it
#endif
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = 0;   // the device's native rate
  cfg.dataCallback = dataCallback;
  cfg.pUserData = &g;
  cfg.performanceProfile = ma_performance_profile_low_latency;
  cfg.aaudio.usage = ma_aaudio_usage_media;
  cfg.notificationCallback = onDeviceNotification;
  auto eng = std::make_shared<tpa::LiveEngine>();
  { std::lock_guard<std::mutex> dl(g_dev);
    if (ma_device_init(nullptr, &cfg, &g.device) != MA_SUCCESS) { LOGI("ma_device_init failed"); return -1; }
    g.deviceInit = true; }
  tpa::LiveEngine::Config ec; ec.sampleRate = (double)g.device.sampleRate; ec.maxBlock = 4096; ec.chains = chains == JNI_TRUE;
  std::string err;
  if (!eng->create(ec, &err)) { LOGI("live create failed: %s", err.c_str()); { std::lock_guard<std::mutex> dl(g_dev); ma_device_uninit(&g.device); g.deviceInit = false; } return -2; }
  // H2: the MIDI host on the live handle's thunder_midi (U37-P5-NOTES call order: create, thunder_midiout_create + devices / ports on the control thread, then the audio callback). Before the control
  // thread starts, this thread IS the control thread; afterwards every MIDI control call goes through the control thread's queue (the runner).
  g.midiOn = false;
  if (thunder_midi* cm = thunder_live_midi(eng->handle())) g.midiOn = tpa::host().engineStartExternal(cm, (double)g.device.sampleRate, &liveTsRead, nullptr);
  if (!g.midiOn) LOGI("MIDI host did not start (MIDI out and in are off)");
  eng->setTickHook([] {   // the poller thread: the clock map's latency floor (nativeStatus only posts it); a BUSY answer keeps it pending, so it is tried again at the next tick (B5)
    const int64_t v = g.pendLatNs.load();
    if (v < 0 || !g.midiOn) return;
    const int rc = tpa::host().setOutLatencyNs(v);
    if (rc == 0) g.lastLatNs = v;
    if (rc != THUNDER_LIVE_E_BUSY) { int64_t exp = v; g.pendLatNs.compare_exchange_strong(exp, -1); }
  });
  eng->setStateHook([](int st) { if (g.midiOn && st == THUNDER_LIVE_STATE_ENDED) tpa::host().setPlaying(false); });   // a song that ends by itself: the clock box goes quiet (the play and stop calls set it themselves)
  eng->startThreads();
  if (g.midiOn) {
    tpa::host().setControlRunner([eng](std::function<int()> fn) {
      const int rc = eng->call([fn](thunder_live*) { return fn(); }, 1500);
      return (rc == -1000 && eng->running()) ? THUNDER_LIVE_E_BUSY : rc;   // a timeout BEFORE fn started (-1000 with the thread up) is BUSY (fn never runs on this thread, F11; nothing changed, a retry is safe); -1001 (fn was already running: unknown, it may still apply) passes through as an error, NOT as BUSY (a retry could apply it twice); -1000 without a thread stays "no control thread"
    });   // a copy of the engine: the runner cannot meet a freed one
    { std::lock_guard<std::mutex> lk(g_inMu); g_inEng = eng; }
    tpa::setMidiInSink(&midiInSink);
    tpa::midiInStart();
  }
  { std::lock_guard<std::mutex> dl(g_dev);   // resolved before the stream runs: the status call reads the xrun count and buffer size through these (under g_dev)
    g.aaLib = dlopen("libaaudio.so", RTLD_NOW);
    if (g.aaLib) { g.aaXrun = (int32_t(*)(void*))dlsym(g.aaLib, "AAudioStream_getXRunCount"); g.aaBuf = (int32_t(*)(void*))dlsym(g.aaLib, "AAudioStream_getBufferSizeInFrames");
                   g.aaTs = (int32_t(*)(void*, int32_t, int64_t*, int64_t*))dlsym(g.aaLib, "AAudioStream_getTimestamp"); g.aaWritten = (int64_t(*)(void*))dlsym(g.aaLib, "AAudioStream_getFramesWritten"); } }
  rtlog_reset(&g_lrt, (int)g.device.sampleRate); g_xrun.restart();
  g.eng.set(eng);
  g.audioEng.store(eng.get(), std::memory_order_release);   // before the stream starts: the callback never sees a half-made engine
  int started;
  { std::lock_guard<std::mutex> dl(g_dev); started = ma_device_start(&g.device); }
  if (started != MA_SUCCESS) {
    g.audioEng.store(nullptr, std::memory_order_release);
    g.eng.take();
    tpa::setMidiInSink(nullptr); tpa::midiInStop(); { std::lock_guard<std::mutex> lk(g_inMu); g_inEng.reset(); }
    eng->stopThreads(); if (g.midiOn) { g.midiOn = false; tpa::host().engineStop(); }
    eng->destroy();
    { std::lock_guard<std::mutex> dl(g_dev); ma_device_uninit(&g.device); g.deviceInit = false;
      if (g.aaLib) { dlclose(g.aaLib); g.aaLib = nullptr; } g.aaXrun = nullptr; g.aaBuf = nullptr; }
    return -3;
  }
  LOGI("live engine started at %u Hz", g.device.sampleRate);
  return (jint)g.device.sampleRate;
}

// The destroy order (live.h destroy, N0's nativeStop): stop the transport, let two callbacks run it, stop the stream (no more process), join the threads, destroy. g_life held.
// The slot is emptied FIRST: from then on no new JNI call gets the engine; a call already inside it holds its own copy, and destroy() below waits for / refuses it (live_engine.h).
// Also right after a failed reopen (the stream is gone, deviceInit false, the engine still there): the engine is destroyed all the same.
void teardownLocked() {
  std::shared_ptr<tpa::LiveEngine> eng = g.eng.take();
  if (!eng && !g.deviceInit) return;
  if (eng && g.deviceInit) {
    eng->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }, 500);
    const int64_t c0 = eng->audioCalls();
    for (int i = 0; i < 100 && eng->audioCalls() < c0 + 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  { std::lock_guard<std::mutex> dl(g_dev); if (g.deviceInit) { ma_device_uninit(&g.device); g.deviceInit = false; } }   // stops the callback
  g.audioEng.store(nullptr, std::memory_order_release);
  tpa::setMidiInSink(nullptr);
  tpa::midiInStop();   // B3: the reader leaves with the engine (joined, 200 ms bound); the next start restarts it
  { std::lock_guard<std::mutex> lk(g_inMu); g_inEng.reset(); }
  if (eng) eng->stopThreads();   // the control thread is the live handle's ONE control thread: it ends before this thread makes the MIDI control calls below
  if (g.midiOn) { g.midiOn = false; tpa::host().engineStop(); }   // the sender delivers the Stop (<= about 0.5 s), then thunder_midiout_free: BEFORE destroy (live.h thunder_live_midi)
  if (eng) eng->destroy();       // joins what is left, then thunder_live_destroy
  eng.reset();
  g.lastSlots.clear();
  g.lastLatNs = -1; g.pendLatNs = -1;
  g.songText.reset(); g.kitText.reset(); g.kitDoc.reset();   // the page re-sends song and kit after an engine reset: a verdict must never be judged with the old kit
  { std::lock_guard<std::mutex> dl(g_dev); if (g.aaLib) { dlclose(g.aaLib); g.aaLib = nullptr; } g.aaXrun = nullptr; g.aaBuf = nullptr; g.aaTs = nullptr; g.aaWritten = nullptr; }
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeShutdown(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lk(g_life);
  teardownLocked();
}

// H1c: the output changed under the host (a device was unplugged, a Bluetooth sink came or went, the stream died). The HOST stays: its sample rate, song and pads are kept. Only the stream is rebuilt, and at the
// ENGINE's rate (the new device may run at another rate: Android's mixer converts, miniaudio converts if it does not; no new create, no pad re-render, no song re-publish). The transport is stopped first
// and is NOT restarted (a defined state: stopped; the owner presses Play). process() takes the new frame0 count as a re-anchor (live.h TIME), so no seam is possible.
// Returns the stream's rate (the engine's), or -1 when no stream could be opened (the host is then ended too: the shell reports "engine down" and the page re-sends everything).
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeReopenStream(JNIEnv*, jobject) {
  std::lock_guard<std::mutex> lk(g_life);
  auto eng = g.eng.get();
  if (!g.deviceInit || !eng) return -2;
  eng->call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }, 500);
  const int64_t c0 = eng->audioCalls();
  for (int i = 0; i < 40 && eng->audioCalls() < c0 + 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(5));   // a dead stream makes no calls: the wait is 200 ms at most
  std::unique_lock<std::mutex> dl(g_dev);   // status / stream-state readers skip their round while the stream is rebuilt
  ma_device_uninit(&g.device);
  g.deviceInit = false;
  ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.sampleRate = (ma_uint32)eng->config().sampleRate;
  cfg.dataCallback = dataCallback;
  cfg.pUserData = &g;
  cfg.performanceProfile = ma_performance_profile_low_latency;
  cfg.aaudio.usage = ma_aaudio_usage_media;
  cfg.notificationCallback = onDeviceNotification;
  if (ma_device_init(nullptr, &cfg, &g.device) != MA_SUCCESS) { dl.unlock(); LOGI("reopen: ma_device_init failed: the host ends"); teardownLocked(); return -1; }
  g.deviceInit = true;
  if (ma_device_start(&g.device) != MA_SUCCESS) { dl.unlock(); LOGI("reopen: ma_device_start failed: the host ends"); teardownLocked(); return -1; }
  g_xrun.newStream();   // AAudio counts xruns per stream
  if (g.midiOn) tpa::host().remap();   // a new stream is a new AAudio timestamp pair: the clock map is published again at the next callback
  LOGI("stream reopened at the engine's %u Hz", g.device.sampleRate);
  return (jint)g.device.sampleRate;
}

// The stream as the watchdog sees it: {"calls": audio callbacks since create, "state": ma_device state (3 = started), "sr", "duck"}. "{}" when no host or while the stream is being rebuilt.
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeStreamState(JNIEnv* env, jobject) {
  auto eng = g.eng.get(); if (!eng) return env->NewStringUTF("{}");
  std::unique_lock<std::mutex> dl(g_dev, std::try_to_lock);
  if (!dl.owns_lock() || !g.deviceInit) return env->NewStringUTF("{}");
  char b[160];
  std::snprintf(b, sizeof b, "{\"calls\":%lld,\"state\":%d,\"sr\":%u,\"duck\":%.3f}", (long long)eng->audioCalls(), (int)ma_device_get_state(&g.device), g.device.sampleRate, (double)eng->duck());
  return env->NewStringUTF(b);
}

// The shell's output gain (audio-focus ducking): 0..1, ramped by the audio thread.
JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeSetDuck(JNIEnv*, jobject, jfloat gain) { auto eng = g.eng.get(); if (eng) eng->setDuck(gain); }

// A .thunder project's bytes (UTF-8): the song and the kit. The pads are rendered HERE (the caller's thread, not the control thread, not the audio thread), then ONE control call publishes the
// song and the pads. Returns a JSON object {ok, rc, err, pads, skipped, dry, renderMs, notes[]}.
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeLoadProject(JNIEnv* env, jobject, jbyteArray bytes) {
  std::lock_guard<std::mutex> ll(g_loadM);
  auto fail = [&](const char* why, int rc) { char b[300]; std::snprintf(b, sizeof b, "{\"ok\":false,\"rc\":%d,\"err\":\"%s\"}", rc, why); return env->NewStringUTF(b); };
  // g_life only to copy what the load needs; it is NOT held across renderPads or the engine call (a shutdown meanwhile returns at once, and the call comes back -1000 / -1001)
  std::shared_ptr<tpa::LiveEngine> eng; double sr = 0; int64_t rev = 0; std::vector<int> prevSlots;
  { std::lock_guard<std::mutex> lk(g_life); eng = g.eng.get(); if (eng) { sr = (double)g.device.sampleRate; rev = ++g.rev; prevSlots = g.lastSlots; } }
  if (!eng) return fail("engine not started", -2);
  if (!bytes) return fail("no bytes", -1);
  const jsize n = env->GetArrayLength(bytes);
  if (n <= 0) return fail("empty file", -1);
  // The task below may outlive this call if the wait times out (LiveEngine::call): everything it touches is owned by shared_ptrs it captures.
  auto textP = std::make_shared<std::string>((size_t)n, '\0');
  std::string& text = *textP;
  env->GetByteArrayRegion(bytes, 0, n, reinterpret_cast<jbyte*>(&text[0]));
  if (env->ExceptionCheck()) { env->ExceptionClear(); return fail("could not read the bytes", -1); }
  const std::string_view songT = tpa::extractMember(text, "song"), kitT = tpa::extractMember(text, "kit");
  if (songT.empty()) return fail("not a .thunder project: no song member", -3);
  double bpm = 120, lpb = 4;
  { thunder::JsonDoc sd; if (sd.parse(songT)) { bpm = sd.root().num("bpm", 120); lpb = sd.root().num("lpb", 4); } }
  auto psP = std::make_shared<tpa::PadSet>();
  tpa::PadSet& ps = *psP;
  if (!kitT.empty() && !tpa::renderPads(kitT, bpm, lpb, sr, &ps)) return fail("the kit is unreadable", -3);
  auto serrP = std::make_shared<std::array<char, 256>>(); serrP->fill(0);
  std::vector<int> slots; for (auto& p : ps.pads) slots.push_back(p.slot);
  auto goneP = std::make_shared<std::vector<int>>(); std::vector<int>& gone = *goneP; for (int s : prevSlots) { bool keep = false; for (int t : slots) if (t == s) keep = true; if (!keep) gone.push_back(s); }
  const int rc = eng->call([=](thunder_live* L) -> int {
    tpa::PadSet& ps = *psP; char* serr = serrP->data();
    const std::string_view songT = tpa::extractMember(*textP, "song");
    int r = thunder_live_song_publish(L, (const uint8_t*)songT.data(), songT.size(), nullptr, 0, rev, serr, serrP->size());
    if (r) return r;
    for (int s : *goneP) { r = thunder_live_pad_set(L, s, nullptr, nullptr, 0, sr); if (r) return r; }
    for (auto& p : ps.pads) {
      if (p.kitDone) continue;   // an E_BUSY retry repeats the lambda from the start: the song publish is idempotent, a pad whose take AND kit worked is already in
      if (p.block) {             // not taken yet (a taken pad has block == nullptr: its take must not be repeated, only its kit may be missing)
        r = thunder_live_pad_take(L, p.slot, p.block, 0, p.frames, sr); if (r) return r;
        p.block = nullptr;
      }
      r = thunder_live_pad_kit(L, p.slot, p.choke, p.syn ? 1 : 0, p.rel); if (r) return r;
      p.kitDone = true;
    }
    return thunder_live_pads_publish(L);
  }, 20000);
  { std::lock_guard<std::mutex> lk(g_life);   // write back only to the engine this load ran on (a shutdown / reset meanwhile: the results are dropped)
    if (g.eng.get() == eng) {
      if (rc == 0) g.lastSlots = slots;
      else { for (int t : slots) { bool have = false; for (int s : g.lastSlots) if (s == t) have = true; if (!have) g.lastSlots.push_back(t); } }   // a failed / timed-out load may have put some pads in: the next load removes the union
    } }
  std::string notes;
  for (size_t i = 0; i < ps.notes.size() && i < 8; i++) { if (i) notes += ","; notes += "\"" + jsonEscape(ps.notes[i]) + "\""; }
  char head[360];
  std::snprintf(head, sizeof head, "{\"ok\":%s,\"rc\":%d,\"err\":\"%s\",\"pads\":%zu,\"skipped\":%d,\"dry\":%d,\"renderMs\":%.0f,\"bpm\":%.1f,\"notes\":[", rc == 0 ? "true" : "false", rc, jsonEscape(serrP->data()).c_str(), ps.pads.size(), ps.skipped, ps.dry, ps.ms, bpm);
  return env->NewStringUTF((std::string(head) + notes + "]}").c_str());
}

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePlay(JNIEnv*, jobject, jint row) {
  auto eng = g.eng.get(); if (!eng) return -2;
  const uint32_t seed = (uint32_t)(monoNs() & 0x7fffffff) | 1u;
  return eng->call([=](thunder_live* L) {
    thunder_live_play_opts po; std::memset(&po, 0, sizeof po);
    po.size = (uint32_t)sizeof po; po.t0 = THUNDER_LIVE_NOW; po.mode = THUNDER_LIVE_MODE_SONG; po.row = row; po.pdc_mode = 1; po.random_seed = seed; po.spp = -1;
    int r = thunder_live_meters_want(L, 10.0); (void)r;
    if (g.midiOn) tpa::host().setPlayingDirect(true);   // before play: a port set after play() is in force for its Start (live.h), the transport flag first
    return thunder_live_play(L, &po);
  });
}

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeTransportStop(JNIEnv*, jobject, jlong at) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([=](thunder_live* L) { const int r = thunder_live_stop(L, at); if (r == 0 && g.midiOn) tpa::host().setPlayingDirect(false); return r; });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeBpm(JNIEnv*, jobject, jdouble bpm, jlong at) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([=](thunder_live* L) { return thunder_live_bpm(L, bpm, at); });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeLoop(JNIEnv*, jobject, jint has, jint y0, jint y1, jlong at) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([=](thunder_live* L) { return thunder_live_loop(L, has, y0, y1, at); });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeMixSet(JNIEnv*, jobject, jint track, jint key, jdouble value, jdouble ramp, jlong at) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([=](thunder_live* L) { return thunder_live_mix_set(L, track, key, value, ramp, at); });
}

// The poller's latest state as JSON. Also hands AAudio's xrun count and buffer size to the host (the poller thread passes them to thunder_live_host_stats every 100 ms).
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeStatus(JNIEnv* env, jobject) {
  auto eng = g.eng.get(); if (!eng) return env->NewStringUTF("{}");
  int64_t newLatNs = -1;
  { std::unique_lock<std::mutex> dl(g_dev, std::try_to_lock);   // the stream pointer and the two AAudio calls on it: start / teardown / reopen change them only under g_dev; a busy lock skips this round
    if (dl.owns_lock() && g.deviceInit) {
      // miniaudio re-routes an AAudio stream BY ITSELF (headphones, Bluetooth): its job closes and reopens pStreamPlayback under rerouteLock on its own thread. Every read of the pointer and every AAudio call on it is inside this lock (taken after g_dev; ma_device_uninit, which destroys it, runs under g_dev too).
      // ma_mutex is a pthread mutex on Android: TRY-lock (this call can come from the main thread, and a reroute holds the lock for as long as the reopen takes): a busy lock skips this round, like g_dev above.
      if (pthread_mutex_trylock((pthread_mutex_t*)&g.device.aaudio.rerouteLock) == 0) {
        void* stream = (void*)g.device.aaudio.pStreamPlayback;
        if (stream) {
          const int x = g.aaXrun ? g.aaXrun(stream) : 0, b = g.aaBuf ? g.aaBuf(stream) : 0;
          eng->setHostStats(b > 0 ? b : 0, x > 0 ? x : 0);   // the buffer size stands for the output latency (GUESS: an upper bound; H1c reads the AAudio timestamp as N0's probe does)
          // F2: the xruns AAudio counted since the A5 run began reach the gate's logger (N0 does the same in jni.cpp): "0 underruns" is then a measurement
          rtlog_host_underruns(&g_lrt, g_xrun.onStream(x));
          if (g.midiOn && b > 0 && g.device.sampleRate > 0) {   // the clock map's floor: one output buffer (an upper bound, as the host stats use it); only when it changed
            const int64_t ns = (int64_t)b * 1000000000LL / (int64_t)g.device.sampleRate;
            if (ns != g.lastLatNs.load()) newLatNs = ns; else g.pendLatNs.store(-1);   // back at the accepted value: drop a stale pending one;   // handed to the poller below: setOutLatencyNs goes through the control thread (up to 1.5 s): never on this (maybe main) thread
          }
        }
        ma_mutex_unlock(&g.device.aaudio.rerouteLock);
      }
    } }
  if (newLatNs >= 0) g.pendLatNs.store(newLatNs);   // B1: posted, not called
  return env->NewStringUTF(eng->statusJson().c_str());
}
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeStats(JNIEnv* env, jobject) {
  auto eng = g.eng.get(); if (!eng) return env->NewStringUTF("{}");
  return env->NewStringUTF(eng->statsJson().c_str());
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeRtReset(JNIEnv*, jobject) { std::lock_guard<std::mutex> dl(g_dev); if (g.deviceInit) { rtlog_reset(&g_lrt, (int)g.device.sampleRate); g_xrun.beginRun(); } }
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeRtSnapshot(JNIEnv* env, jobject) {
  char b[400];
  rtlog_snapshot sn = rtlog_read(&g_lrt);
  const int sr = g_lrt.sample_rate;
  std::snprintf(b, sizeof b,
    "{\"pending\":0,\"sr\":%d,\"frames\":%lld,\"calls\":%lld,\"late\":%lld,\"under\":%lld,\"audioS\":%.6f,\"wallS\":%.6f,\"ratioPpm\":%.2f,\"maxGapMs\":%.3f,\"maxWorkMs\":%.3f,\"pass\":%d,\"outLatMs\":0}",
    sr, (long long)sn.frames, (long long)sn.calls, (long long)sn.late, (long long)sn.underruns, sn.audio_s, sn.wall_s, sn.ratio_ppm, sn.max_gap_ms, sn.max_work_ms, sn.pass);
  return env->NewStringUTF(b);
}


// ============================== H1b: the CORE-BRIDGE v1 messages (docs/port/N2-PLAN.md section 2) ================================================================================
// Every call below runs on the caller's thread and hands its CONTROL work to the control thread (LiveEngine::call). Big messages arrive as the page's raw JSON text (UTF-8 bytes) and the members are cut
// out with extractMember, so the song is parsed once, by the core.
namespace {
std::shared_ptr<std::vector<uint8_t>> bytesOf(JNIEnv* env, jbyteArray a) {
  auto v = std::make_shared<std::vector<uint8_t>>();
  if (!a) return v;
  const jsize n = env->GetArrayLength(a);
  if (n <= 0) return v;
  v->resize((size_t)n);
  env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(v->data()));
  if (env->ExceptionCheck()) { env->ExceptionClear(); v->clear(); }
  return v;
}
long long numOf(std::string_view s, long long def) { if (s.empty()) return def; return std::strtoll(std::string(s).c_str(), nullptr, 10); }
jstring jstr(JNIEnv* env, const std::string& s) { return env->NewStringUTF(s.c_str()); }
std::string loadJson(bool ok, int rc, const std::string& err, const char* extra = "") {
  char b[400]; std::snprintf(b, sizeof b, "{\"ok\":%s,\"rc\":%d,\"err\":\"%s\"%s}", ok ? "true" : "false", rc, jsonEscape(err).c_str(), extra); return b;
}
}  // namespace

// song.load {S, vars, rev}: the whole song text to thunder_live_song_publish (ruling D3: no patches). Remembers the song's bpm and lpb for the pad render of the next kit.load.
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeSongLoad(JNIEnv* env, jobject, jbyteArray msg) {
  std::lock_guard<std::mutex> ll(g_loadM);
  std::shared_ptr<tpa::LiveEngine> eng; long long nextRev = 0;
  { std::lock_guard<std::mutex> lk(g_life); eng = g.eng.get(); if (eng) nextRev = ++g.rev; }
  if (!eng) return jstr(env, loadJson(false, -2, "engine not started"));
  auto m = bytesOf(env, msg);
  const std::string_view mv((const char*)m->data(), m->size());
  std::string_view S = tpa::extractMember(mv, "S"), V = tpa::extractMember(mv, "vars");
  if (S.empty()) return jstr(env, loadJson(false, -3, "song.load without S"));
  if (V == "null") V = std::string_view();
  const long long rev = numOf(tpa::extractMember(mv, "rev"), nextRev);
  auto text = std::make_shared<std::string>(S);
  auto vars = std::make_shared<std::string>(V);
  auto serr = std::make_shared<std::array<char, 256>>(); serr->fill(0);
  const int rc = eng->call([=](thunder_live* L) { return thunder_live_song_publish(L, (const uint8_t*)text->data(), text->size(), vars->empty() ? nullptr : (const uint8_t*)vars->data(), vars->size(), rev, serr->data(), serr->size()); }, 20000);
  if (rc == 0) {
    double bpm = 120, lpb = 4;
    thunder::JsonDoc sd; if (sd.parse(*text)) { bpm = sd.root().num("bpm", 120); lpb = sd.root().num("lpb", 4); }
    std::lock_guard<std::mutex> lk(g_life);
    if (g.eng.get() == eng) { g.songText = text; g.bpm = bpm; g.lpb = lpb; }   // only to the engine the song went to
  }
  return jstr(env, loadJson(rc == 0, rc, serr->data()));
}

// kit.load {kit, rev}: the core renders every pad it can (PadFiller) and publishes them with the kit entries; a pad it cannot render stays empty for the page to `put`. The answer says which.
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeKitLoad(JNIEnv* env, jobject, jbyteArray msg) {
  std::lock_guard<std::mutex> ll(g_loadM);
  std::shared_ptr<tpa::LiveEngine> eng; double bpm = 120, lpb = 4, sr = 0; std::vector<int> prevSlots;
  { std::lock_guard<std::mutex> lk(g_life); eng = g.eng.get(); if (eng) { bpm = g.bpm; lpb = g.lpb; sr = (double)g.device.sampleRate; prevSlots = g.lastSlots; } }
  if (!eng) return jstr(env, loadJson(false, -2, "engine not started"));
  auto m = bytesOf(env, msg);
  const std::string_view kitT = tpa::extractMember(std::string_view((const char*)m->data(), m->size()), "kit");
  if (kitT.empty()) return jstr(env, loadJson(false, -3, "kit.load without kit"));
  auto kitText = std::make_shared<std::string>(kitT);
  auto kitDoc = std::make_shared<thunder::JsonDoc>();
  if (!kitDoc->parse(*kitText)) {
    std::lock_guard<std::mutex> lk(g_life);
    if (g.eng.get() == eng) { g.kitDoc.reset(); g.kitText.reset(); }
    return jstr(env, loadJson(false, -3, "the kit is unreadable"));
  }
  auto psP = std::make_shared<tpa::PadSet>();
  tpa::renderPads(*kitText, bpm, lpb, sr, psP.get());
  std::vector<int> slots; for (auto& p : psP->pads) slots.push_back(p.slot);
  auto goneP = std::make_shared<std::vector<int>>(); for (int s : prevSlots) { bool keep = false; for (int t : slots) if (t == s) keep = true; if (!keep) goneP->push_back(s); }
  const int rc = eng->call([=](thunder_live* L) -> int {
    int r;
    for (int s : *goneP) { r = thunder_live_pad_set(L, s, nullptr, nullptr, 0, sr); if (r) return r; }
    for (auto& p : psP->pads) {
      if (p.kitDone) continue;   // an E_BUSY retry repeats the lambda from the start: a pad whose take AND kit worked is in
      if (p.block) {             // a taken pad (block == nullptr) is never taken again; only its kit may be missing
        r = thunder_live_pad_take(L, p.slot, p.block, 0, p.frames, sr); if (r) return r;
        p.block = nullptr;
      }
      r = thunder_live_pad_kit(L, p.slot, p.choke, p.syn ? 1 : 0, p.rel); if (r) return r;
      p.kitDone = true;
    }
    return thunder_live_pads_publish(L);
  }, 20000);
  { std::lock_guard<std::mutex> lk(g_life);   // write back only to the engine this load ran on (a shutdown / reset meanwhile: the results are dropped)
    if (g.eng.get() == eng) {
      g.kitText = kitText; g.kitDoc = kitDoc;
      if (rc == 0) g.lastSlots = slots;
      else { for (int t : slots) { bool have = false; for (int s : g.lastSlots) if (s == t) have = true; if (!have) g.lastSlots.push_back(t); } }   // a failed / timed-out load (-1000, -1001) may have put some pads in: the next load removes the union
    } }
  std::string empty; for (size_t i = 0; i < kitDoc->root().arr("voices").size() && i < 256; i++) { bool have = false; for (int s : slots) if (s == (int)i) have = true; if (!have && kitDoc->root().arr("voices").at(i).isObj()) { if (!empty.empty()) empty += ","; empty += std::to_string(i); } }
  char extra[200]; std::snprintf(extra, sizeof extra, ",\"pads\":%zu,\"skipped\":%d,\"dry\":%d,\"renderMs\":%.0f,\"empty\":[%s]", psP->pads.size(), psP->skipped, psP->dry, psP->ms, empty.c_str());
  return jstr(env, loadJson(rc == 0, rc, "", extra));
}

// SongFit over the last song and kit: the bridge's `fit` event (songfit.h). Chains count as available only when the host was made with F_CHAINS.
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeFit(JNIEnv* env, jobject, jlong rev, jint flags) {   // flags: bit 0 the page's live transpose is on (lxp), bit 1 it follows an external MIDI clock
  std::lock_guard<std::mutex> lk(g_life);
  auto eng = g.eng.get(); if (!eng || !g.songText) return jstr(env, "{\"t\":\"fit\",\"ok\":false,\"reasons\":[{\"code\":\"none\",\"text\":\"no song loaded\"}]}");
  uint32_t caps = thunder_live_caps();
#ifdef THUNDER_LIVE_F_CHAINS
  if (!eng->chainsOn()) caps &= ~(uint32_t)THUNDER_LIVE_CAP_CHAINS;
#endif
  bool lxr = false;
#ifdef THUNDER_HAVE_LXR
  lxr = thunder_lxr_api() != nullptr;
#endif
  tpa::FitContext ctx; ctx.lxpActive = (flags & 1) != 0; ctx.clockIn = (flags & 2) != 0;
  return jstr(env, tpa::songFit(*g.songText, g.kitText ? std::string_view(*g.kitText) : std::string_view(), caps, 0, lxr, ctx).json(rev));
}

// put + buf.use kind 'pad': `l` (and `r` for a stereo pad) are little-endian float32 as the page sent them. The block comes from thunder_live_alloc, all of L then all of R (pad_take's layout).
// The kit entry (choke, syn, rel) is the one of the last kit.load for that slot (0, 0, 0 without one). The caller publishes with nativePadsPublish.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePadTake(JNIEnv* env, jobject, jint pad, jbyteArray l, jbyteArray r, jint frames, jdouble rate) {
  std::lock_guard<std::mutex> lk(g_life);
  auto eng = g.eng.get(); if (!eng) return -2;
  if (frames <= 0 || pad < 0 || pad >= THUNDER_LIVE_PADS || !l) return -1;
  const jsize nl = env->GetArrayLength(l), nr = r ? env->GetArrayLength(r) : 0;
  const int ch = r ? 2 : 1;
  if ((jlong)nl < (jlong)frames * 4 || (r && (jlong)nr < (jlong)frames * 4)) return -1;
  float* b = thunder_live_alloc((int64_t)frames * ch);
  if (!b) return THUNDER_LIVE_E_MEMORY;
  auto blk = std::make_shared<tpa::PadBlock>(b);   // freed by the holder on every failure, including a task dropped at the timeout (verify-n2 F9)
  env->GetByteArrayRegion(l, 0, (jsize)frames * 4, reinterpret_cast<jbyte*>(b));
  if (r) env->GetByteArrayRegion(r, 0, (jsize)frames * 4, reinterpret_cast<jbyte*>(b + frames));
  if (env->ExceptionCheck()) { env->ExceptionClear(); return -1; }
  int choke = 0; bool syn = false; double rel = 0;
  if (g.kitDoc) tpa::kitEntryOf(g.kitDoc->root(), (size_t)pad, &choke, &syn, &rel);
  const int rc = eng->call([=](thunder_live* L) {
    if (blk->owned) {   // an E_BUSY retry repeats the lambda from the start: once the core has the block (owned == false) pad_take must NOT be called again, only pad_kit
      int x = thunder_live_pad_take(L, pad, blk->p, r ? 1 : 0, frames, rate);   // E_BUSY: not taken, the retry repeats it
      if (x) return x;
      blk->owned = false;   // the core has the block
    }
    return thunder_live_pad_kit(L, pad, choke, syn ? 1 : 0, rel);
  }, 10000);
  return rc;
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePadsPublish(JNIEnv*, jobject) {
  auto eng = g.eng.get(); if (!eng) return -2;
  return eng->call([](thunder_live* L) { return thunder_live_pads_publish(L); }, 10000);
}
// A MUT / GEN variation (buf.use kind 'var'): mono float32 in `l`.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeVarSet(JNIEnv* env, jobject, jint ii, jint kind, jint num, jint pad, jbyteArray l, jint frames, jdouble rate) {
  auto eng = g.eng.get(); if (!eng || !l || frames <= 0) return -2;
  if ((jlong)env->GetArrayLength(l) < (jlong)frames * 4) return -1;
  auto v = std::make_shared<std::vector<float>>((size_t)frames);
  env->GetByteArrayRegion(l, 0, (jsize)frames * 4, reinterpret_cast<jbyte*>(v->data()));
  return eng->call([=](thunder_live* L) { return thunder_live_var_set(L, ii, kind, num, pad, v->data(), frames, rate); }, 10000);
}

// play {mode,row,at?,...}: every field of thunder_live_play_opts the page can send. t0 < 0: the next quantum + 0.08 s.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePlayEx(JNIEnv*, jobject, jint mode, jint row, jint track, jint sect, jint phrase, jint hasLoop, jint y0, jint y1, jlong t0, jint hasSeed, jint seed, jint pdc, jlong onlyBits, jint hasOnly) {
  auto eng = g.eng.get(); if (!eng) return -2;
  const uint32_t rs = (uint32_t)(monoNs() & 0x7fffffff) | 1u;
  return eng->call([=](thunder_live* L) {
    thunder_live_play_opts po; std::memset(&po, 0, sizeof po);
    po.size = (uint32_t)sizeof po; po.t0 = t0 < 0 ? THUNDER_LIVE_NOW : t0; po.mode = mode; po.row = row; po.track = track; po.sect = sect; po.phrase = phrase;
    po.has_loop = hasLoop; po.loop_y0 = y0; po.loop_y1 = y1; po.has_seed = hasSeed; po.seed = (uint32_t)seed; po.random_seed = rs; po.spp = -1; po.pdc_mode = pdc;
    po.only = (uint64_t)onlyBits; po.has_only = hasOnly;
    if (g.midiOn) tpa::host().setPlayingDirect(true);
    return thunder_live_play(L, &po);
  });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeHold(JNIEnv*, jobject, jlong at) { auto eng = g.eng.get(); if (!eng) return -2; return eng->call([=](thunder_live* L) { return thunder_live_hold(L, at); }); }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeLead(JNIEnv*, jobject, jdouble s, jlong at) { auto eng = g.eng.get(); if (!eng) return -2; return eng->call([=](thunder_live* L) { return thunder_live_lead(L, s, at); }); }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePerf(JNIEnv*, jobject, jint what, jint index, jdouble v, jlong at) { auto eng = g.eng.get(); if (!eng) return -2; return eng->call([=](thunder_live* L) { return thunder_live_perf(L, what, index, v, at); }); }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeMetersWant(JNIEnv*, jobject, jdouble hz) { auto eng = g.eng.get(); if (!eng) return -2; return eng->call([=](thunder_live* L) { return thunder_live_meters_want(L, hz); }); }

// fx.plan / fx.commit / fx.param / master.config (live.h section 5): the page's own JSON as bytes, passed through.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeFxPlan(JNIEnv* env, jobject, jint track, jbyteArray slots, jdouble bpm) {
  auto eng = g.eng.get(); if (!eng) return -2;
  auto b = bytesOf(env, slots);
  return eng->call([=](thunder_live* L) { return thunder_live_fx_plan(L, track, b->empty() ? nullptr : b->data(), b->size(), bpm); }, 10000);
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeFxCommit(JNIEnv*, jobject, jint track, jlong at) { auto eng = g.eng.get(); if (!eng) return -2; return eng->call([=](thunder_live* L) { return thunder_live_fx_commit(L, track, at); }); }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeFxParam(JNIEnv* env, jobject, jint track, jint slot, jdoubleArray p, jlong at) {
  auto eng = g.eng.get(); if (!eng || !p || env->GetArrayLength(p) < 12) return -1;
  auto v = std::make_shared<std::array<double, 12>>(); env->GetDoubleArrayRegion(p, 0, 12, v->data());
  return eng->call([=](thunder_live* L) { return thunder_live_fx_param(L, track, slot, v->data(), at); });
}
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeMasterConfig(JNIEnv* env, jobject, jbyteArray mst, jint safety) {
  auto eng = g.eng.get(); if (!eng) return -2;
  auto b = bytesOf(env, mst);
  return eng->call([=](thunder_live* L) { return thunder_live_master_config(L, b->empty() ? nullptr : b->data(), b->size(), safety); }, 10000);
}
// chain.edit {ops, at?}: needs the chains host (n1/c2-live, #311); without it E_UNSUPPORTED (-9).
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeChainEdit(JNIEnv* env, jobject, jbyteArray ops, jlong at) {
  auto eng = g.eng.get(); if (!eng) return -2;
#ifdef THUNDER_LIVE_F_CHAINS
  auto b = bytesOf(env, ops);
  return eng->call([=](thunder_live* L) { return thunder_live_chain_edit(L, b->empty() ? nullptr : b->data(), b->size(), at); });
#else
  (void)env; (void)ops; (void)at; return THUNDER_LIVE_E_UNSUPPORTED;
#endif
}
// Whether the optional drum module is linked AND registered in this build: the page's engine list must not claim lx* engines the core cannot render (verify-n2 F13).
JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeHasLxr(JNIEnv*, jobject) {
#ifdef THUNDER_HAVE_LXR
  return thunder_lxr_api() != nullptr ? JNI_TRUE : JNI_FALSE;
#else
  return JNI_FALSE;
#endif
}
JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeChainsOn(JNIEnv*, jobject) { auto eng = g.eng.get(); return eng && eng->chainsOn() ? JNI_TRUE : JNI_FALSE; }
// The poller's events since the last call, one JSON object per line ("" when none).
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeDrainEvents(JNIEnv* env, jobject) { auto eng = g.eng.get(); if (!eng) return jstr(env, ""); return jstr(env, eng->drainEvents()); }
JNIEXPORT jlong JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeEngineOffset(JNIEnv*, jobject) { auto eng = g.eng.get(); return (jlong)(eng ? eng->engineOffset() : 0); }


JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePdcSet(JNIEnv*, jobject, jint mode) { auto eng = g.eng.get(); if (!eng) return -2; return eng->call([=](thunder_live* L) { return thunder_live_pdc_set(L, mode); }, 10000); }
// pad.chop {pad, loopSecs, slices}: the chop data of a pad (live.h pad_chop / pad_hits). `pts` are SECONDS (render.h CHOP DATA); the page sends frames, the shell divides by the engine's rate.
// loopSecs <= 0 removes the loop; nSlices < 2 or an empty `pts` removes the hits.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_LiveNative_nativePadChop(JNIEnv* env, jobject, jint pad, jdouble loopSecs, jint nSlices, jdoubleArray pts) {
  auto eng = g.eng.get(); if (!eng) return -2;
  auto v = std::make_shared<std::vector<double>>();
  if (pts) { const jsize n = env->GetArrayLength(pts); v->resize((size_t)n); if (n > 0) env->GetDoubleArrayRegion(pts, 0, n, v->data()); }
  const double sr = (double)g.device.sampleRate;
  for (auto& x : *v) x /= sr;
  return eng->call([=](thunder_live* L) {
    int r = thunder_live_pad_chop(L, pad, loopSecs > 0 ? loopSecs : 0.0); if (r) return r;
    const int cnt = (int)v->size();
    if (cnt == 0 || nSlices < 2) return thunder_live_pad_hits(L, pad, 2, nullptr, 0);
    return thunder_live_pad_hits(L, pad, nSlices > 64 ? 64 : nSlices, v->data(), cnt > nSlices ? nSlices : cnt);
  }, 10000);
}
JNIEXPORT jdouble JNICALL Java_app_thunderplus_nativetest_LiveNative_nativeLoad(JNIEnv*, jobject) { return (jdouble)g_load.load(std::memory_order_relaxed); }

}  // extern "C"
