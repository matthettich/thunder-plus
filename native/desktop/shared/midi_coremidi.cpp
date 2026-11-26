// H7: the Mac's MIDI, CoreMIDI behind MidiBackend (midi_backend.h). Plain C++ over CoreMIDI's C API (no Objective-C, no blocks). UNVERIFIED without hardware: CI compiles it; the owner's Mac and a MIDI device test it.
//   PORTS     the app's MIDI OUT ports are CoreMIDI destinations, its MIDI IN ports are sources; the page's ids are "n:" + the endpoint's display name (midi_registry.h), the key is kMIDIPropertyUniqueID.
//   OUT       one output port; a PortIo per destination. send() runs on tpa::MidiHost's sender thread (never real-time): MIDISend with the packet's time as a MIDITimeStamp = mach_absolute_time ticks (CoreMIDI holds
//             a packet stamped in the future and delivers it at that time; 0 = now). The shell's clock is host_clock.h (CLOCK_UPTIME_RAW = mach_absolute_time in ns), so the conversion is the mach timebase only.
//   IN        one input port, every source connected with its own refCon (a struct holding that source's ring). THE CoreMIDI READ PROC runs on a thread CoreMIDI owns: it walks the packet list and pushes each packet
//             into the source's lock-free ring (midi_rx.h) and does nothing else: no allocation, no lock, no system call, no log. A reader thread (midi_manager.cpp) does the rest.
//   HOT-PLUG  MIDIClientCreate's notify proc (kMIDIMsgSetupChanged / ObjectAdded / ObjectRemoved) sets the manager's flag. CoreMIDI delivers notifications on the run loop of the thread that made the client: the
//             shell makes the backend on the main thread; the manager also scans every 3 s whatever happens.
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#include <mach/mach_time.h>
#include <pthread.h>

#include <atomic>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>

#include "midi_backend.h"

namespace tpd {
namespace {

std::string cfToUtf8(CFStringRef s) {
  if (!s) return "";
  const CFIndex n = CFStringGetMaximumSizeForEncoding(CFStringGetLength(s), kCFStringEncodingUTF8) + 1;
  std::vector<char> b((size_t)n);
  if (!CFStringGetCString(s, b.data(), n, kCFStringEncodingUTF8)) return "";
  return std::string(b.data());
}

std::string endpointName(MIDIEndpointRef e) {
  CFStringRef s = nullptr;
  std::string out;
  if (MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s) == noErr && s) { out = cfToUtf8(s); CFRelease(s); }
  if (out.empty()) { s = nullptr; if (MIDIObjectGetStringProperty(e, kMIDIPropertyName, &s) == noErr && s) { out = cfToUtf8(s); CFRelease(s); } }
  return out;
}

bool endpointOffline(MIDIEndpointRef e) { SInt32 v = 0; return MIDIObjectGetIntegerProperty(e, kMIDIPropertyOffline, &v) == noErr && v != 0; }
int64_t uniqueId(MIDIEndpointRef e) { SInt32 v = 0; MIDIObjectGetIntegerProperty(e, kMIDIPropertyUniqueID, &v); return (int64_t)v; }

mach_timebase_info_data_t timebase() { static mach_timebase_info_data_t t = [] { mach_timebase_info_data_t x; mach_timebase_info(&x); return x; }(); return t; }
uint64_t nsToTicks(int64_t ns) { const auto t = timebase(); return (uint64_t)((unsigned __int128)(uint64_t)ns * t.denom / t.numer); }

// One connected source: its ring stays alive as long as this struct does. A disconnected source's struct is parked (never freed while the backend lives): a callback already in flight cannot touch freed memory.
struct SrcCtx { std::shared_ptr<MidiRxRing> ring; };

class CoreOut : public tpa::PortIo {
 public:
  CoreOut(MIDIPortRef port, MIDIEndpointRef dest) : port_(port), dest_(dest) {}
  int send(const uint8_t* b, int len, int64_t at_ns) override {   // the sender thread
    if (len <= 0) return 0;
    const MIDITimeStamp ts = at_ns > 0 ? (MIDITimeStamp)nsToTicks(at_ns) : 0;
    Byte small[512];
    std::vector<Byte> big;
    Byte* buf = small; size_t cap = sizeof small;
    if ((size_t)len + 64 > cap) { big.resize((size_t)len + 64); buf = big.data(); cap = big.size(); }
    MIDIPacketList* pl = reinterpret_cast<MIDIPacketList*>(buf);
    MIDIPacket* p = MIDIPacketListInit(pl);
    p = MIDIPacketListAdd(pl, cap, p, ts, (ByteCount)len, b);
    if (!p) return -1;
    return MIDISend(port_, dest_, pl) == noErr ? 0 : -1;   // an error: the endpoint is gone
  }
 private:
  MIDIPortRef port_; MIDIEndpointRef dest_;
};

class CoreMidi : public MidiBackend {
 public:
  CoreMidi() {
    if (MIDIClientCreate(CFSTR("Thunder Plus"), &CoreMidi::notify, this, &client_) != noErr) { client_ = 0; return; }
    if (MIDIOutputPortCreate(client_, CFSTR("Thunder Plus out"), &outPort_) != noErr) outPort_ = 0;
    if (MIDIInputPortCreate(client_, CFSTR("Thunder Plus in"), &CoreMidi::read, this, &inPort_) != noErr) inPort_ = 0;
  }
  ~CoreMidi() override {
    { std::lock_guard<std::mutex> g(m_); onChange_ = nullptr; }
    for (auto& kv : conn_) MIDIPortDisconnectSource(inPort_, kv.second.src);
    if (inPort_) MIDIPortDispose(inPort_);
    if (outPort_) MIDIPortDispose(outPort_);
    if (client_) MIDIClientDispose(client_);
    // the parked contexts are freed here, after the ports are gone
  }
  bool ok() const { return client_ != 0; }
  std::vector<MidiPortScan> listOutputs() override {
    std::vector<MidiPortScan> v;
    if (!client_) return v;
    const ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; i++) { MIDIEndpointRef e = MIDIGetDestination(i); if (!e || endpointOffline(e)) continue; v.push_back({uniqueId(e), endpointName(e)}); }
    return v;
  }
  std::vector<MidiPortScan> listInputs() override {
    std::vector<MidiPortScan> v;
    if (!client_) return v;
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; i++) { MIDIEndpointRef e = MIDIGetSource(i); if (!e || endpointOffline(e)) continue; v.push_back({uniqueId(e), endpointName(e)}); }
    return v;
  }
  std::shared_ptr<tpa::PortIo> openOutput(int64_t key) override {
    if (!outPort_) return nullptr;
    MIDIObjectRef ref = 0; MIDIObjectType type = kMIDIObjectType_Other;
    if (MIDIObjectFindByUniqueID((MIDIUniqueID)key, &ref, &type) != noErr || (type != kMIDIObjectType_Destination && type != kMIDIObjectType_ExternalDestination)) return nullptr;
    return std::make_shared<CoreOut>(outPort_, (MIDIEndpointRef)ref);
  }
  bool openInput(int64_t key, std::shared_ptr<MidiRxRing> ring) override {
    if (!inPort_) return false;
    MIDIObjectRef ref = 0; MIDIObjectType type = kMIDIObjectType_Other;
    if (MIDIObjectFindByUniqueID((MIDIUniqueID)key, &ref, &type) != noErr || (type != kMIDIObjectType_Source && type != kMIDIObjectType_ExternalSource)) return false;
    auto ctx = std::make_shared<SrcCtx>(); ctx->ring = std::move(ring);
    if (MIDIPortConnectSource(inPort_, (MIDIEndpointRef)ref, ctx.get()) != noErr) return false;
    std::lock_guard<std::mutex> g(m_);
    conn_[key] = {(MIDIEndpointRef)ref, ctx};
    return true;
  }
  void closeInput(int64_t key) override {
    Conn c;
    { std::lock_guard<std::mutex> g(m_); auto it = conn_.find(key); if (it == conn_.end()) return; c = it->second; conn_.erase(it); parked_.push_back(c.ctx); }
    MIDIPortDisconnectSource(inPort_, c.src);
  }
  void setChangeHandler(std::function<void()> f) override { std::lock_guard<std::mutex> g(m_); onChange_ = std::move(f); }
  int64_t ticksToNs(int64_t ticks) const override { const auto t = timebase(); return (int64_t)((unsigned __int128)(uint64_t)ticks * t.numer / t.denom); }
 private:
  struct Conn { MIDIEndpointRef src = 0; std::shared_ptr<SrcCtx> ctx; };
  // THE CoreMIDI READ PROC (a thread CoreMIDI owns): packets into the source's ring. Nothing else.
  static void read(const MIDIPacketList* pl, void*, void* srcRef) {
    SrcCtx* c = static_cast<SrcCtx*>(srcRef);
    if (!c) return;
    const MIDIPacket* p = &pl->packet[0];
    for (UInt32 i = 0; i < pl->numPackets; i++) { c->ring->push((int64_t)p->timeStamp, p->data, p->length); p = MIDIPacketNext(p); }
  }
  static void notify(const MIDINotification* m, void* ref) {
    if (m->messageID != kMIDIMsgSetupChanged && m->messageID != kMIDIMsgObjectAdded && m->messageID != kMIDIMsgObjectRemoved) return;
    CoreMidi* s = static_cast<CoreMidi*>(ref);
    std::function<void()> f; { std::lock_guard<std::mutex> g(s->m_); f = s->onChange_; }
    if (f) f();
  }
  MIDIClientRef client_ = 0; MIDIPortRef outPort_ = 0, inPort_ = 0;
  std::mutex m_;
  std::map<int64_t, Conn> conn_;
  std::vector<std::shared_ptr<SrcCtx>> parked_;
  std::function<void()> onChange_;
};

}  // namespace

// The MIDI sender thread (midi_out.cpp) asks for this when it starts: the highest user QoS, so macOS wakes it on time (it sleeps to each message's send time).
static void senderEnter() { pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0); }

std::shared_ptr<MidiBackend> makeCoreMidiBackend() {
  tpa::MidiHost::setSenderThreadHooks(&senderEnter, nullptr);
  auto b = std::make_shared<CoreMidi>();
  return b->ok() ? std::static_pointer_cast<MidiBackend>(b) : nullptr;
}

}  // namespace tpd
#endif
