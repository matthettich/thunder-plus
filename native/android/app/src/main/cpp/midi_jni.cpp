// U36 T9: the Android-only half of the MIDI out: AMidi ports (PortIo) and the JNI entry points of Kotlin's NativeMidi. Everything else is in midi_out.{h,cpp} (tested on Linux).
// The app WRITES to a device's INPUT ports (AMidiInputPort); Kotlin (MidiPorts.kt) lists devices, opens them (MidiManager.openDevice is asynchronous) and hands the open
// android.media.midi.MidiDevice here. UNVERIFIED on a device: all of it (no emulator, no phone in the build session); CI only compiles it.
#include <android/log.h>
#include <amidi/AMidi.h>
#include <jni.h>

#include <time.h>

#include <cstring>
#include <memory>
#include <string>
#include <vector>

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
    // AMidi returns the bytes written, 0 when the port's buffer is full (NOT an error code), < 0 for an error. A message is sent whole or not at all (UNVERIFIED), so 0 is
    // retried a few times (0.2 ms apart: this is the sender thread, the pump waits at most 0.6 ms); still not accepted: the message is LOST, counted in `short`, and reported to the
    // core as a send error (< 0), not as delivered: a lost note-off must show in the stats. (That marks the cable gone until the next set_devices: a full buffer after four tries means the device is stuck.)
    for (int tries = 0; tries < 4; tries++) {
      const ssize_t n = (ts_ && at > 0) ? AMidiInputPort_sendWithTimestamp(port_, b, (size_t)len, at) : AMidiInputPort_send(port_, b, (size_t)len);
      if (n < 0) return -1;   // AMEDIA_ERROR_*: the device is gone
      if (n >= len) return 0;
      tpa::host().shortWrite.fetch_add(1, std::memory_order_relaxed);
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

JavaVM* g_vm = nullptr;
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

JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeAssign(JNIEnv* env, jobject, jint port, jbyteArray id) { return tpa::host().assign(port, utf8(env, id)); }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeTest(JNIEnv*, jobject) { return tpa::host().debugTest(); }
JNIEXPORT jboolean JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativePoll(JNIEnv*, jobject) { return tpa::host().debugPoll() ? JNI_TRUE : JNI_FALSE; }
JNIEXPORT jint JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeSenderTid(JNIEnv*, jobject) { return tpa::host().senderTid(); }
JNIEXPORT jstring JNICALL Java_app_thunderplus_nativetest_NativeMidi_nativeStatus(JNIEnv* env, jobject) { return env->NewStringUTF(tpa::host().status().c_str()); }

}  // extern "C"
