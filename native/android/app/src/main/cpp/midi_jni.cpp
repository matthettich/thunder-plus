// U36 T9: the Android-only half of the MIDI out: AMidi ports (PortIo) and the JNI entry points of Kotlin's NativeMidi. Everything else is in midi_out.{h,cpp} (tested on Linux).
// The app WRITES to a device's INPUT ports (AMidiInputPort); Kotlin (MidiPorts.kt) lists devices, opens them (MidiManager.openDevice is asynchronous) and hands the open
// android.media.midi.MidiDevice here. UNVERIFIED on a device: all of it (no emulator, no phone in the build session); CI only compiles it.
#include <android/log.h>
#include <amidi/AMidi.h>
#include <jni.h>

#include <time.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <memory>
#include <string>
#include <vector>

#include "midi_in.h"
#include "midi_out.h"

#define LOG_TAG "ThunderMidi"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace {

// One AMidiDevice, shared by the ports opened on it; released when the last port is gone (on whichever thread drops the last reference: never under the PortTable lock).
struct AmidiDevice {
  AMidiDevice* dev = nullptr;
  ~AmidiDevice() { if (dev) AMidiDevice_release(dev); }
};

class AmidiPort : public tpa::PortIo {
 public:
  AmidiPort(std::shared_ptr<AmidiDevice> d, AMidiInputPort* p, bool ts) : dev_(std::move(d)), port_(p), ts_(ts) {}
  ~AmidiPort() override { if (port_) AMidiInputPort_close(port_); }   // before dev_ (the device) is released: members die in reverse order
  int send(const uint8_t* b, int len, int64_t at) override {
    // AMidiInputPort_sendWithTimestamp: the time is CLOCK_MONOTONIC ns, the same clock as tpa::monoNs (what the pump's at_ns is on). Plain send: now.
    // AMidi returns the bytes written, 0 when the port's buffer is full (NOT an error code), < 0 for an error. A write may be PARTIAL (0 < n < len, UNVERIFIED on a device): the
    // rest is then sent from byte n, never from byte 0 again (that would repeat bytes of a message already begun). A write of 0 bytes is retried a few times (0.2 ms apart: this is
    // the sender thread, the pump waits at most 0.6 ms); still not accepted after four: the message is LOST, counted in `short`, and reported to the core as a send error (< 0), not as
    // delivered: a lost note-off must show in the stats. (That marks the cable gone until the next set_devices: a full buffer after four tries means the device is stuck.)
    int off = 0, zero = 0;
    while (zero < 4) {
      const ssize_t n = (ts_ && at > 0) ? AMidiInputPort_sendWithTimestamp(port_, b + off, (size_t)(len - off), at) : AMidiInputPort_send(port_, b + off, (size_t)(len - off));
      if (n < 0) return -1;   // AMEDIA_ERROR_*: the device is gone
      if (n >= len - off) return 0;
      tpa::host().shortWrite.fetch_add(1, std::memory_order_relaxed);
      if (n > 0) { off += (int)n; continue; }   // progress: not a refusal
      zero++;
      timespec ts{0, 200 * 1000}; nanosleep(&ts, nullptr);
    }
    return -1;
  }
 private:
  std::shared_ptr<AmidiDevice> dev_;
  AMidiInputPort* port_;
  bool ts_;
};

std::string utf8(JNIEnv* env, jbyteArray a) {   // UTF-8 bytes from Kotlin (not GetStringUTFChars: modified UTF-8)
  std::string s;
  if (!a) return s;
  const jsize n = env->GetArrayLength(a);
  if (n <= 0) return s;
  s.resize((size_t)n);
  env->GetByteArrayRegion(a, 0, n, reinterpret_cast<jbyte*>(&s[0]));
  if (env->ExceptionCheck()) { env->ExceptionClear(); s.clear(); }
  return s;
}

// ---- N2 H2: MIDI IN. The app READS a device's OUTPUT ports (AMidiOutputPort). AMidiOutputPort_receive does not block, so ONE reader thread polls every open port (1 ms sleep when all are empty),
// feeds each port's MidiInParser and hands the messages to the sink the live engine installed (midi_in.h). The table is guarded by one mutex the reader holds for a pass; attach / detach (Kotlin's
// main thread) take it briefly, and a closed port is destroyed AFTER the unlock on the caller's thread (AMidiDevice_release needs an attached thread: the main thread is one).
struct InPort {
  uint64_t token = 0;
  std::shared_ptr<AmidiDevice> dev;
  AMidiOutputPort* port = nullptr;
  tpa::MidiInParser parser;
  ~InPort() { if (port) AMidiOutputPort_close(port); }   // before `dev`: members die in reverse order
};
struct InTable {
  std::mutex mu;
  std::condition_variable cv;        // the reader waits on it (no ports / stopped) and midiInStop waits for the reader's exit on it
  std::vector<std::unique_ptr<InPort>> v;
  bool run = false;                  // the reader should run: set by midiInStart (the live engine has a sink), cleared by midiInStop (verify B3: no 1 kHz polling with no engine)
  uint64_t gen = 0;                  // the running reader's generation: a reader whose generation is not the current one exits
  uint64_t exitedGen = 0;            // the last reader generation that has left inLoop
  std::thread th;
};
InTable& inTable() { static InTable* t = new InTable; return *t; }   // never destroyed: the reader thread outlives static destruction

struct InCtx { uint64_t token; int64_t ts; };
void inEmit(void* user, const tpa::MidiInMsg& m) { const InCtx* c = static_cast<const InCtx*>(user); if (tpa::MidiInSink s = tpa::midiInSink()) s(c->token, c->ts, m); }

void inLoop(uint64_t myGen) {
  InTable& t = inTable();
  uint8_t buf[1024];
  for (;;) {
    bool any = false;
    {
      std::unique_lock<std::mutex> lk(t.mu);
      t.cv.wait(lk, [&] { return !t.v.empty() || !t.run || t.gen != myGen; });
      if (!t.run || t.gen != myGen) { t.exitedGen = myGen; lk.unlock(); t.cv.notify_all(); return; }
      for (auto& p : t.v) {
        for (int k = 0; k < 64; k++) {
          int32_t op = 0; size_t n = 0; int64_t ts = 0;
          const ssize_t got = AMidiOutputPort_receive(p->port, &op, buf, sizeof buf, &n, &ts);
          if (got <= 0) break;   // 0: nothing waiting; < 0: the device is gone (Kotlin detaches it on the hot-plug callback)
          any = true;
          if (op == AMIDI_OPCODE_DATA) { InCtx c{p->token, ts}; p->parser.feed(buf, (int)n, &inEmit, &c); }
        }
      }
    }
    if (!any) { timespec ts{0, 1000 * 1000}; nanosleep(&ts, nullptr); }
  }
}

JavaVM* g_vm = nullptr;
}  // namespace

namespace tpa {
// B3: the reader runs only while a live engine has the MIDI-in sink. Start: a new generation. Stop: the reader leaves at its next pass (a pass is bounded: 64 non-blocking receives per port); joined with a
// bound of 200 ms, detached after that (its generation is stale, so it exits by itself and a later start cannot meet two readers). The ports stay attached: the next start reads them again.
void midiInStart() {
  InTable& t = inTable();
  std::thread old;
  { std::lock_guard<std::mutex> lk(t.mu);
    if (t.run) return;
    t.run = true; t.gen++; old = std::move(t.th);
    t.th = std::thread(inLoop, t.gen); }
  if (old.joinable()) old.detach();
  t.cv.notify_all();
}
void midiInStop() {
  InTable& t = inTable();
  std::thread th; uint64_t g;
  { std::unique_lock<std::mutex> lk(t.mu);
    if (!t.run) return;
    t.run = false; g = t.gen; th = std::move(t.th);
    t.cv.notify_all();
    t.cv.wait_for(lk, std::chrono::milliseconds(200), [&] { return t.exitedGen == g; });
    if (t.exitedGen != g) { lk.unlock(); if (th.joinable()) th.detach(); return; } }
  if (th.joinable()) th.join();
}
}  // namespace tpa

namespace {
// The sender thread is a std::thread: attached to the JVM for its life, because dropping the LAST reference to an AmidiPort (after a hot-unplug, while a send was in flight)
// runs AMidiDevice_release on it, and that calls JavaVM::GetEnv and aborts on an unattached thread.
void senderEnter() { if (g_vm) { JNIEnv* e = nullptr; g_vm->AttachCurrentThread(&e, nullptr); } }
void senderLeave() { if (g_vm) g_vm->DetachCurrentThread(); }

}  // namespace

extern "C" {

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
  g_vm = vm;
  tpa::MidiHost::setSenderThreadHooks(&senderEnter, &senderLeave);
  return JNI_VERSION_1_6;
}


// Opens the listed input ports of an open MidiDevice and puts them in the token table. tokens[i] / ports[i] / ts[i] describe one port (ports = the PortInfo port numbers of the
// device's INPUT ports). Returns the tokens that were opened. Does NOT change the device list the core sees: Kotlin calls nativeSetDevices after this.
JNIEXPORT jlongArray JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeAttach(JNIEnv* env, jobject, jobject midiDevice, jlongArray tokens, jintArray ports, jbooleanArray ts) {
  std::vector<jlong> okTokens;
  auto result = [&]() { jlongArray r = env->NewLongArray((jsize)okTokens.size()); if (r && !okTokens.empty()) env->SetLongArrayRegion(r, 0, (jsize)okTokens.size(), okTokens.data()); return r; };
  if (!midiDevice || !tokens || !ports || !ts) return result();
  const jsize n = env->GetArrayLength(tokens);
  if (env->GetArrayLength(ports) != n || env->GetArrayLength(ts) != n || n <= 0) return result();
  std::vector<jlong> tk((size_t)n); std::vector<jint> pn((size_t)n); std::vector<jboolean> tsv((size_t)n);
  env->GetLongArrayRegion(tokens, 0, n, tk.data()); env->GetIntArrayRegion(ports, 0, n, pn.data()); env->GetBooleanArrayRegion(ts, 0, n, tsv.data());
  auto d = std::make_shared<AmidiDevice>();
  if (AMidiDevice_fromJava(env, midiDevice, &d->dev) != AMEDIA_OK) { d->dev = nullptr; LOGI("AMidiDevice_fromJava failed"); return result(); }
  const int32_t nIn = (int32_t)AMidiDevice_getNumInputPorts(d->dev);
  for (jsize i = 0; i < n; i++) {
    if (pn[(size_t)i] < 0 || pn[(size_t)i] >= nIn) { LOGI("port %d of %d: no such input port", (int)pn[(size_t)i], (int)nIn); continue; }
    AMidiInputPort* p = nullptr;
    if (AMidiInputPort_open(d->dev, pn[(size_t)i], &p) != AMEDIA_OK || !p) { LOGI("AMidiInputPort_open(%d) failed", (int)pn[(size_t)i]); continue; }
    tpa::host().attach((uint64_t)tk[(size_t)i], std::make_shared<AmidiPort>(d, p, tsv[(size_t)i] != JNI_FALSE), tsv[(size_t)i] != JNI_FALSE);
    okTokens.push_back(tk[(size_t)i]);
  }
  return result();   // the tokens that opened (a count could not say WHICH). `d` lives on in the ports; if none opened it is released here
}

// N2 H2: opens the listed OUTPUT ports of an open MidiDevice (PortInfo TYPE_OUTPUT port numbers) for reading, under the given tokens. Returns the tokens that opened.
JNIEXPORT jlongArray JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeAttachIn(JNIEnv* env, jobject, jobject midiDevice, jlongArray tokens, jintArray ports) {
  std::vector<jlong> okTokens;
  auto result = [&]() { jlongArray r = env->NewLongArray((jsize)okTokens.size()); if (r && !okTokens.empty()) env->SetLongArrayRegion(r, 0, (jsize)okTokens.size(), okTokens.data()); return r; };
  if (!midiDevice || !tokens || !ports) return result();
  const jsize n = env->GetArrayLength(tokens);
  if (env->GetArrayLength(ports) != n || n <= 0) return result();
  std::vector<jlong> tk((size_t)n); std::vector<jint> pn((size_t)n);
  env->GetLongArrayRegion(tokens, 0, n, tk.data()); env->GetIntArrayRegion(ports, 0, n, pn.data());
  auto d = std::make_shared<AmidiDevice>();
  if (AMidiDevice_fromJava(env, midiDevice, &d->dev) != AMEDIA_OK) { d->dev = nullptr; LOGI("AMidiDevice_fromJava (in) failed"); return result(); }
  const int32_t nOut = (int32_t)AMidiDevice_getNumOutputPorts(d->dev);
  std::vector<std::unique_ptr<InPort>> add;
  for (jsize i = 0; i < n; i++) {
    if (pn[(size_t)i] < 0 || pn[(size_t)i] >= nOut) { LOGI("in port %d of %d: no such output port", (int)pn[(size_t)i], (int)nOut); continue; }
    AMidiOutputPort* p = nullptr;
    if (AMidiOutputPort_open(d->dev, pn[(size_t)i], &p) != AMEDIA_OK || !p) { LOGI("AMidiOutputPort_open(%d) failed", (int)pn[(size_t)i]); continue; }
    std::unique_ptr<InPort> ip(new InPort); ip->token = (uint64_t)tk[(size_t)i]; ip->dev = d; ip->port = p;
    add.push_back(std::move(ip)); okTokens.push_back(tk[(size_t)i]);
  }
  InTable& t = inTable();
  {
    std::lock_guard<std::mutex> lk(t.mu);
    for (auto& ip : add) t.v.push_back(std::move(ip));
  }
  t.cv.notify_all();
  return result();
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeDetachIn(JNIEnv* env, jobject, jlongArray tokens) {
  if (!tokens) return;
  const jsize n = env->GetArrayLength(tokens);
  if (n <= 0) return;
  std::vector<jlong> tk((size_t)n);
  env->GetLongArrayRegion(tokens, 0, n, tk.data());
  std::vector<std::unique_ptr<InPort>> gone;
  {
    InTable& t = inTable();
    std::lock_guard<std::mutex> lk(t.mu);
    for (size_t i = 0; i < t.v.size();) {
      bool hit = false; for (jlong x : tk) if ((uint64_t)x == t.v[i]->token) hit = true;
      if (hit) { gone.push_back(std::move(t.v[i])); t.v.erase(t.v.begin() + (long)i); } else i++;
    }
  }
  gone.clear();   // closed here, outside the lock
}

JNIEXPORT void JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeDetach(JNIEnv* env, jobject, jlongArray tokens) {
  if (!tokens) return;
  const jsize n = env->GetArrayLength(tokens);
  if (n <= 0) return;
  std::vector<jlong> tk((size_t)n);
  env->GetLongArrayRegion(tokens, 0, n, tk.data());
  for (jlong t : tk) tpa::host().detach((uint64_t)t);
}

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeSetDevices(JNIEnv* env, jobject, jobjectArray ids, jobjectArray names, jlongArray tokens, jbooleanArray ts) {
  std::vector<tpa::DevInfo> v;
  const jsize n = ids ? env->GetArrayLength(ids) : 0;
  if (n > 0) {
    if (!names || !tokens || !ts || env->GetArrayLength(names) != n || env->GetArrayLength(tokens) != n || env->GetArrayLength(ts) != n) return THUNDER_MIDIOUT_E_ARG;
    std::vector<jlong> tk((size_t)n); std::vector<jboolean> tsv((size_t)n);
    env->GetLongArrayRegion(tokens, 0, n, tk.data()); env->GetBooleanArrayRegion(ts, 0, n, tsv.data());
    for (jsize i = 0; i < n; i++) {
      jbyteArray a = (jbyteArray)env->GetObjectArrayElement(ids, i), b = (jbyteArray)env->GetObjectArrayElement(names, i);
      tpa::DevInfo d; d.id = utf8(env, a); d.name = utf8(env, b); d.token = (uint64_t)tk[(size_t)i]; d.ts = tsv[(size_t)i] != JNI_FALSE;
      if (a) env->DeleteLocalRef(a);
      if (b) env->DeleteLocalRef(b);
      v.push_back(std::move(d));
    }
  }
  return tpa::host().setDevices(v);
}

// N2 H2: the four letters' clock boxes and Gear delays (midi.cfg), and the ports as JSON for the `midi.ports` event.
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeSetClock(JNIEnv*, jobject, jint port, jboolean on) { return tpa::host().setClock(port, on != JNI_FALSE); }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeSetGear(JNIEnv*, jobject, jint port, jdouble ms) { return tpa::host().setGear(port, ms); }
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativePortsJson(JNIEnv* env, jobject) { return env->NewStringUTF(tpa::toJniUtf8(tpa::host().portsJson()).c_str()); }

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeAssign(JNIEnv* env, jobject, jint port, jbyteArray id) { return tpa::host().assign(port, utf8(env, id)); }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeTest(JNIEnv*, jobject) { return tpa::host().debugTest(); }
JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativePoll(JNIEnv*, jobject) { return tpa::host().debugPoll() ? JNI_TRUE : JNI_FALSE; }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeSenderTid(JNIEnv*, jobject) { return tpa::host().senderTid(); }
// NewStringUTF wants MODIFIED UTF-8: a device name with an emoji (4 bytes) would abort under CheckJNI.
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeStatus(JNIEnv* env, jobject) { return env->NewStringUTF(tpa::toJniUtf8(tpa::host().status()).c_str()); }

}  // extern "C"
