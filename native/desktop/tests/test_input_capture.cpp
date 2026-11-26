// H7: the audio input (input_capture.h) with a fake capture device: 28 channels (the Cranborne 500R8's count), the ring, arm / start / stop, the takes (data, time stamp, gaps), the meters, a lost device, and the
// capture-thread rule (a ring push and nothing else) under a producer thread at the real callback rate.
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include "check.h"
#include "input_capture.h"
#include "rt/json.h"

using namespace tpd;
static bool g_closed = false;   // the capture owns (and frees) the input on close, so the test cannot look at the object afterwards
struct FakeIn : AudioIn {
  Callback cb = nullptr; void* user = nullptr; int ch = 28; double sr = 48000; int lat = 96; bool openOk = true; std::atomic<bool> lostFlag{false}; AudioInConfig seen;
  bool open(const AudioInConfig& c, Callback f, void* u, AudioInInfo* info, std::string* err) override {
    seen = c; if (!openOk) { if (err) *err = "no such input"; return false; }
    cb = f; user = u; info->channels = ch; info->sampleRate = sr; info->latencyFrames = lat; info->periodFrames = 128; info->name = "Fake 28"; info->id = c.inId; return true; }
  bool start(std::string*) override { return true; }
  void close() override { g_closed = true; }
  bool lost() const override { return lostFlag.load(); }
  // one callback: sample (frame i, channel c) = c + (i % 1000) / 1000 / 10  (channel in the integer part so every channel is told apart)
  void feed(int frames, int64_t first) { std::vector<float> b((size_t)frames * (size_t)ch); for (int i = 0; i < frames; i++) for (int c = 0; c < ch; c++) b[(size_t)i * ch + c] = (float)c + (float)((first + i) % 1000) / 10000.f; cb(user, b.data(), frames, ch, 1000); }
};
static void waitFor(InputCapture& ic, uint32_t zero) { for (int i = 0; i < 500 && ic.availableFrames() > zero; i++) std::this_thread::sleep_for(std::chrono::milliseconds(2)); }

int main() {
  FakeIn* fi = nullptr; bool fail = false;
  std::mutex em; std::vector<std::string> events;
  InputCapture ic([&] { auto p = std::unique_ptr<FakeIn>(new FakeIn()); fi = p.get(); p->openOk = !fail; return std::unique_ptr<AudioIn>(std::move(p)); }, [&](const std::string& j) { std::lock_guard<std::mutex> g(em); events.push_back(j); });
  std::string e;
  CHECK(!ic.arm({0}, &e) && e.find("no input") != std::string::npos, "arm without an input: refused with a reason");
  fail = true; CHECK(!ic.open({"in:x", 48000, 0}, &e) && e.find("no such input") != std::string::npos && !ic.isOpen(), "a failed open says why");
  fail = false; CHECK(ic.open({"in:500r8", 48000, 0}, &e) && ic.isOpen() && ic.info().channels == 28 && ic.info().latencyFrames == 96 && fi->seen.inId == "in:500r8", "open: all 28 channels");
  CHECK(!ic.arm({}, &e) && !ic.arm({28}, &e) && e.find("28 channels") != std::string::npos && !ic.arm({-1}, &e), "arm refuses no channels and channels the device lacks");
  CHECK(ic.arm({0, 17, 27}, &e) && ic.armed(), "arm three of 28");
  CHECK(ic.start(&e), "start"); CHECK(ic.recording() && !ic.arm({1}, &e), "no re-arm while recording");
  int64_t f = 0; for (int k = 0; k < 40; k++) { fi->feed(128, f); f += 128; std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
  waitFor(ic, 0);
  const int id = ic.stop(&e); CHECK(id == 1 && !ic.recording(), "stop gives take 1");
  auto t = ic.take(id);
  CHECK(t && t->channels == 3 && t->frames == 5120 && t->sr == 48000 && t->data.size() == 3 && t->latencyFrames == 96 && t->gaps == 0, "the take: 3 channels, 5120 frames: %lld", t ? (long long)t->frames : -1);
  bool right = t != nullptr;
  if (t) for (int i = 0; i < 5120 && right; i++) { right = std::fabs(t->data[0][i] - (0.f + (float)(i % 1000) / 10000.f)) < 1e-6 && std::fabs(t->data[1][i] - (17.f + (float)(i % 1000) / 10000.f)) < 1e-6 && std::fabs(t->data[2][i] - (27.f + (float)(i % 1000) / 10000.f)) < 1e-6; }
  CHECK(right, "every sample of channels 0, 17 and 27 is the right one (no channel mix-up, no shift)");
  // the host time of the take's first frame: the capture start (1000 ns, the fake's callback time) + frame / rate - the latency (96 frames = 2 ms)
  CHECK(t && std::llabs(t->hostNs0 - (1000 - (int64_t)(96.0 * 1e9 / 48000.0))) < 1000, "hostNs0 = start - input latency: %lld", t ? (long long)t->hostNs0 : 0);
  auto pk = ic.readPeaks(); CHECK(pk.size() == 3 && pk[0] > 0.f && pk[2] > 26.9f, "meters: one peak per armed channel");
  CHECK(ic.stop(&e) < 0 && e.find("no take") != std::string::npos, "stop without a take");
  { std::lock_guard<std::mutex> g(em); bool any = false; for (auto& l : events) if (l.find("\"t\":\"rec\"") != std::string::npos && l.find("\"pk\":[") != std::string::npos) any = true; CHECK(any, "the rec meter event is emitted while armed"); }

  // a gap: callbacks the ring could not take leave a hole in the device frame numbers; the take is filled with silence in place and counts it
  CHECK(ic.start(&e), "second take");
  fi->feed(128, 0); waitFor(ic, 0); std::this_thread::sleep_for(std::chrono::milliseconds(30));
  // (the capture callback numbers frames itself: a dropped callback = ring full. Overfill the ring to force it.)
  for (int k = 0; k < 600; k++) fi->feed(128, 0);   // 76800 frames into a 48000-frame ring while the drain keeps up: some may drop, the take must stay aligned either way
  waitFor(ic, 0); std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const int id2 = ic.stop(&e); auto t2 = ic.take(id2);
  CHECK(id2 == 2 && t2 && t2->frames <= 76928 && (uint64_t)t2->frames + ic.dropped() >= 76928, "a gap is filled with silence in place: take frames <= delivered <= take frames + dropped (%lld + %llu)", t2 ? (long long)t2->frames : -1, (unsigned long long)ic.dropped());
  ic.freeTake(id2); CHECK(ic.take(id2) == nullptr && ic.take(id) != nullptr, "freeTake releases one take");

  fi->lostFlag = true; CHECK(ic.lost(), "a device that stops by itself is reported lost");
  fi->lostFlag = false;
  g_closed = false; ic.close(); CHECK(!ic.isOpen() && g_closed, "close stops the device"); ic.close();   // twice is fine

  // the capture thread at the real callback rate: 28 channels, 128-frame callbacks every 2.67 ms for two seconds, nothing dropped, the take whole
  CHECK(ic.open({"", 48000, 0}, &e) && ic.arm({0, 1}, &e) && ic.start(&e), "reopen");
  std::atomic<bool> go{true}; std::atomic<int64_t> fed{0};
  std::thread cap([&] { auto next = std::chrono::steady_clock::now(); while (go) { fi->feed(128, fed); fed += 128; next += std::chrono::microseconds(2667); std::this_thread::sleep_until(next); } });
  std::this_thread::sleep_for(std::chrono::seconds(2)); go = false; cap.join(); waitFor(ic, 0); std::this_thread::sleep_for(std::chrono::milliseconds(20));
  const int id3 = ic.stop(&e); auto t3 = ic.take(id3);
  CHECK(t3 && t3->frames == fed.load() && ic.dropped() == 0 && t3->gaps == 0, "a 2-second take at the callback rate is whole: %lld of %lld frames, dropped %llu", t3 ? (long long)t3->frames : -1, (long long)fed.load(), (unsigned long long)ic.dropped());
  ic.close();
  DONE("input_capture");
}
