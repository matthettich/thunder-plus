// H7: Settings > Audio on the bridge (bridge_session.h): dev.list, dev.open (a rebuild with another output; the same config is a no-op), the saved choice, the fall-back when a device will not open, a device that
// disappears while playing (hot-plug: the output falls back to the default, the page is told in order), the input and its takes (rec.arm / start / stop / take), the A5 logger. All against hand-driven fakes.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <thread>
#include "bridge_session.h"
#include "check.h"
#include "rt/json.h"
#include "test_audio.h"
#include "text_util.h"

using namespace tpd;
struct Sink {
  std::mutex m; std::vector<std::string> lines;
  void add(const std::string& block) { std::lock_guard<std::mutex> g(m); size_t i = 0; while (i < block.size()) { size_t j = block.find('\n', i); if (j == std::string::npos) j = block.size(); if (j > i) lines.push_back(block.substr(i, j - i)); i = j + 1; } }
  std::vector<std::string> all() { std::lock_guard<std::mutex> g(m); return lines; }
  std::string find(std::initializer_list<const char*> parts, size_t from = 0) { auto v = all(); for (size_t i = from; i < v.size(); i++) { bool ok = true; for (auto p : parts) if (v[i].find(p) == std::string::npos) ok = false; if (ok) return v[i]; } return ""; }
  size_t index(std::initializer_list<const char*> parts, size_t from = 0) { auto v = all(); for (size_t i = from; i < v.size(); i++) { bool ok = true; for (auto p : parts) if (v[i].find(p) == std::string::npos) ok = false; if (ok) return i; } return (size_t)-1; }
  size_t size() { std::lock_guard<std::mutex> g(m); return lines.size(); }
};
struct FakeEnum : AudioEnumerator {
  std::mutex m; std::vector<AudioDevice> devs;
  std::vector<AudioDevice> list() override { std::lock_guard<std::mutex> g(m); return devs; }
  static AudioDevice dev(const char* raw, const char* name, int ins, int outs, bool defOut = false) { AudioDevice d; d.name = name; d.backend = "Fake"; if (ins) { d.inId = std::string("in:") + raw; d.inputs = ins; } if (outs) { d.outId = std::string("out:") + raw; d.outputs = outs; d.isDefaultOut = defOut; } d.id = ins ? d.inId : d.outId; d.rates = {44100, 48000, 96000}; return d; }
};
struct FakeIn : AudioIn {
  Callback cb = nullptr; void* user = nullptr; std::atomic<bool> lostFlag{false};
  bool open(const AudioInConfig& c, Callback f, void* u, AudioInInfo* info, std::string*) override { cb = f; user = u; info->channels = 28; info->sampleRate = c.sampleRate ? c.sampleRate : 48000; info->latencyFrames = 64; info->periodFrames = 128; info->name = "500R8"; info->id = c.inId; return true; }
  bool start(std::string*) override { return true; }
  void close() override {}
  bool lost() const override { return lostFlag.load(); }
};

int main() {
  namespace fs = std::filesystem;
  const std::string dir = (fs::temp_directory_path() / "tpd-audio-session-test").string(); fs::remove_all(dir);
  ConfigCtl ctl; ConfigOut* out = nullptr; std::mutex om;
  LiveHost host([&] { auto o = std::unique_ptr<ConfigOut>(new ConfigOut(&ctl)); { std::lock_guard<std::mutex> g(om); out = o.get(); } return std::unique_ptr<AudioOut>(std::move(o)); });
  auto en = std::make_shared<FakeEnum>();
  en->devs = {FakeEnum::dev("spk", "Speakers", 0, 2, true), FakeEnum::dev("500r8", "500R8", 28, 2), FakeEnum::dev("hp", "Headphones", 0, 2)};
  FakeIn* fin = nullptr;
  InputCapture input([&] { auto p = std::unique_ptr<FakeIn>(new FakeIn()); fin = p.get(); return std::unique_ptr<AudioIn>(std::move(p)); });
  AudioSettingsStore store(dir);
  Sink sink;
  SessionIo io; io.devices = en; io.settings = &store; io.input = &input; io.devWatchMs = 40;
  std::atomic<bool> run{true};
  std::thread dev([&] { while (run) { { std::lock_guard<std::mutex> g(om); if (out && host.active()) out->tick(256); } std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });
  {
    BridgeSession s(host, [&](const std::string& l) { sink.add(l); }, nullptr, io);
    s.post("{\"t\":\"hello\",\"v\":1,\"live\":true,\"app\":\"x\",\"id\":1}"); s.drain();
    { auto l = sink.find({"\"t\":\"reply\"", "\"id\":1"}); thunder::JsonDoc d; CHECK(d.parse(l), "hello reply: %s", l.c_str());
      auto caps = d.root().obj("caps"); CHECK(caps.boolean("inputs") && !caps.boolean("midi") && caps.boolean("devices") && caps.boolean("rt") && d.root().obj("audio").boolean("open") && d.root().obj("audio").str("name") == "Default Out", "caps: inputs yes (a fake input), midi no (none given); the audio body says what is open"); }
    CHECK(sink.find({"\"kind\":\"lost\""}).empty(), "no saved choice: no lost-device event");

    s.post("{\"t\":\"dev.list\",\"id\":2}"); s.drain();
    { auto l = sink.find({"\"t\":\"reply\"", "\"id\":2"}); thunder::JsonDoc d; CHECK(d.parse(l) && d.root().boolean("ok") && d.root().arr("devices").size() == 3 && d.root().arr("devices").at(1).num("ins") == 28 && d.root().arr("devices").at(1).str("inId") == "in:500r8", "dev.list: three devices, the interface has 28 ins: %s", l.c_str()); }

    // dev.open: another output, rate and buffer -> a rebuild in the order the page needs (engine down, engine up with the new rate, then the reply)
    const size_t mark = sink.size();
    s.post("{\"t\":\"dev.open\",\"id\":3,\"out\":\"out:500r8\",\"sr\":96000,\"buf\":128,\"in\":\"in:500r8\"}"); s.drain();
    { const size_t down = sink.index({"\"t\":\"engine\"", "\"up\":false"}, mark), up = sink.index({"\"t\":\"engine\"", "\"up\":true", "\"sr\":96000"}, mark), rep = sink.index({"\"t\":\"reply\"", "\"id\":3"}, mark);
      CHECK(down != (size_t)-1 && up != (size_t)-1 && rep != (size_t)-1 && down < up && up < rep, "events in order: engine down, engine up at 96000, the reply (%zu %zu %zu)", down, up, rep);
      thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":3"})); auto r = d.root(); auto a = r.obj("audio"), in = r.obj("input");
      CHECK(r.boolean("ok") && r.boolean("rebuilt") && !r.boolean("fellBack") && a.num("sr") == 96000 && a.num("buf") == 128 && a.num("latOut") == 128 + 64 && a.str("id") == "out:500r8" && in.boolean("open") && in.num("channels") == 28 && in.num("latIn") == 64 && in.num("sr") == 96000, "dev.open reply: sr, buffer, latency, the 28-channel input at the engine's rate"); }
    { std::lock_guard<std::mutex> g(ctl.m); const auto& o = ctl.opened.back(); CHECK(o.outId == "out:500r8" && o.sampleRate == 96000 && o.bufferFrames == 128, "the output was opened with the page's choice"); }
    CHECK(host.active() && host.sampleRate() == 96000, "the engine runs at 96000");
    { AudioSettings sv = store.load(); CHECK(sv.out == "out:500r8" && sv.in == "in:500r8" && sv.sr == 96000 && sv.buf == 128, "the choice is saved"); }
    size_t opens; { std::lock_guard<std::mutex> g(ctl.m); opens = ctl.opened.size(); }
    s.post("{\"t\":\"dev.open\",\"id\":4,\"out\":\"out:500r8\",\"sr\":96000,\"buf\":128,\"in\":\"in:500r8\"}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":4"})); size_t now; { std::lock_guard<std::mutex> g(ctl.m); now = ctl.opened.size(); } CHECK(d.root().boolean("ok") && !d.root().boolean("rebuilt") && now == opens, "the same dev.open again changes nothing (no rebuild)"); }
    s.post("{\"t\":\"dev.open\",\"id\":5,\"sr\":3}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":5"})); CHECK(!d.root().boolean("ok") && d.root().str("error").find("sr") != std::string::npos && host.sampleRate() == 96000, "a bad rate: an error reply, the engine untouched"); }
    s.post("{\"t\":\"dev.open\",\"id\":6,\"out\":7}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":6"})); CHECK(!d.root().boolean("ok"), "an id that is not a string: refused"); }

    // the input and its takes through the bridge
    s.post("{\"t\":\"rec.arm\",\"id\":10,\"ins\":[0,5,27]}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":10"})); CHECK(d.root().boolean("ok") && d.root().num("nch") == 3 && d.root().num("inputs") == 28, "rec.arm: 3 of 28"); }
    s.post("{\"t\":\"rec.arm\",\"id\":11,\"ins\":[28]}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":11"})); CHECK(!d.root().boolean("ok") && d.root().str("error").find("28 channels") != std::string::npos, "rec.arm: a channel the device lacks"); }
    s.post("{\"t\":\"rec.start\",\"id\":12}"); s.drain();
    { std::vector<float> b(128 * 28, 0.25f); for (int k = 0; k < 20; k++) { fin->cb(fin->user, b.data(), 128, 28, 5000); std::this_thread::sleep_for(std::chrono::milliseconds(2)); } }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    s.post("{\"t\":\"rec.stop\",\"id\":13}"); s.drain();
    int take = -1; long long frames = 0;
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":13"})); take = (int)d.root().num("take", -1); frames = (long long)d.root().num("frames"); CHECK(d.root().boolean("ok") && take >= 1 && frames == 2560 && d.root().num("nch") == 3 && d.root().num("sr") == 96000, "rec.stop: a take of 2560 frames, 3 channels"); }
    s.post("{\"t\":\"take\",\"id\":14,\"take\":" + std::to_string(take) + ",\"ch\":1,\"off\":100,\"n\":300}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":14"})); CHECK(d.root().boolean("ok") && d.root().num("nch") == 1 && d.root().num("frames") == 300, "take: one channel, a range");
      const std::string c = sink.find({"\"t\":\"take.chunk\"", "\"rid\":14"}); thunder::JsonDoc cd; cd.parse(c); std::string raw; base64Decode(std::string(cd.root().str("b")), &raw);
      float v0 = 0; if (raw.size() >= 4) std::memcpy(&v0, raw.data(), 4); CHECK(raw.size() == 1200 && v0 == 0.25f, "the chunk is 300 float32 samples of the right value (%zu bytes, %f)", raw.size(), (double)v0); }
    s.post("{\"t\":\"take\",\"id\":15,\"take\":99}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":15"})); CHECK(!d.root().boolean("ok"), "an unknown take"); }
    s.post("{\"t\":\"take.free\",\"take\":" + std::to_string(take) + "}"); s.drain();
    s.post("{\"t\":\"take\",\"id\":16,\"take\":" + std::to_string(take) + "}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":16"})); CHECK(!d.root().boolean("ok"), "a freed take is gone"); }
    // a take running when the input disappears is ended and kept
    s.post("{\"t\":\"rec.arm\",\"id\":17,\"ins\":[0]}"); s.post("{\"t\":\"rec.start\",\"id\":18}"); s.drain();
    { std::vector<float> b(128 * 28, 0.1f); for (int k = 0; k < 5; k++) fin->cb(fin->user, b.data(), 128, 28, 7000); }
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    { std::lock_guard<std::mutex> g(en->m); en->devs.erase(en->devs.begin() + 1); en->devs.insert(en->devs.begin() + 1, FakeEnum::dev("500r8", "500R8", 0, 2)); }   // the capture side vanishes (the output stays)
    for (int i = 0; i < 100 && sink.find({"\"rec.done\""}).empty(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    s.drain();
    CHECK(!sink.find({"\"t\":\"rec.done\"", "\"partial\":true"}).empty() && !sink.find({"\"kind\":\"lost\"", "\"which\":\"in\""}).empty() && !input.isOpen(), "the input went away mid-take: rec.done (partial) and dev lost (in), the input is closed");

    // A5 logger
    s.post("{\"t\":\"rt.reset\"}"); std::this_thread::sleep_for(std::chrono::milliseconds(300)); s.post("{\"t\":\"rt.snapshot\",\"id\":20}"); s.drain();
    { thunder::JsonDoc d; d.parse(sink.find({"\"t\":\"reply\"", "\"id\":20"})); CHECK(d.root().boolean("ok") && d.root().num("sr") == 96000 && d.root().num("calls") > 5, "rt.snapshot answers with the logger's fields: calls %f", d.root().num("calls")); }

    // HOT-PLUG: the open output (500R8) disappears while the engine runs: the page is told in order and the default plays
    const size_t m2 = sink.size();
    { std::lock_guard<std::mutex> g(en->m); en->devs.erase(en->devs.begin() + 1); }
    for (int i = 0; i < 150 && sink.find({"\"kind\":\"reopened\""}, m2).empty(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    s.drain();
    { const size_t lost = sink.index({"\"kind\":\"lost\"", "\"which\":\"out\""}, m2), dv = sink.index({"\"t\":\"device\"", "\"stopped\":true"}, m2), down = sink.index({"\"t\":\"engine\"", "\"up\":false"}, m2), up = sink.index({"\"t\":\"engine\"", "\"up\":true"}, m2), re = sink.index({"\"kind\":\"reopened\""}, m2);
      CHECK(lost != (size_t)-1 && dv != (size_t)-1 && down != (size_t)-1 && up != (size_t)-1 && re != (size_t)-1 && lost < dv && dv < down && down < up && up < re, "unplugging the output: dev lost, device stopped, engine down, engine up, dev reopened, in that order (%zu %zu %zu %zu %zu)", lost, dv, down, up, re);
      CHECK(host.active() && host.audioConfig().outId.empty() && host.audioInfo().name == "Default Out", "the default output plays now"); }
    { AudioSettings sv = store.load(); CHECK(sv.out == "out:500r8", "the page's choice is remembered, not overwritten by the fall-back"); }
    // it comes back: said once, never switched
    const size_t m3 = sink.size();
    { std::lock_guard<std::mutex> g(en->m); en->devs.insert(en->devs.begin() + 1, FakeEnum::dev("500r8", "500R8", 28, 2)); }
    for (int i = 0; i < 100 && sink.find({"\"wantedBack\""}, m3).empty(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!sink.find({"\"wantedBack\":\"out\""}, m3).empty() && host.audioConfig().outId.empty(), "the wanted output is back: the page is told, nothing is switched under the music");
    CHECK(!sink.find({"\"kind\":\"changed\"", "\"devices\":["}, m3).empty(), "a changed device list is pushed");

    // the OS says the stream stopped by itself (default device removed): the same fall-back
    const size_t m4 = sink.size(); ctl.lost = true;
    for (int i = 0; i < 150 && sink.find({"\"kind\":\"reopened\""}, m4).empty(); i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(!sink.find({"\"kind\":\"reopened\""}, m4).empty() && !ctl.lost.load(), "a stream that stopped by itself is reopened");
    s.shutdown();
  }
  // a new run: the saved choice is the first output; a device that is gone falls back at hello and the page is told
  host.shutdown(); { std::lock_guard<std::mutex> g(ctl.m); ctl.opened.clear(); ctl.missing = {"out:500r8"}; }
  { AudioSettings sv; sv.out = "out:500r8"; sv.sr = 48000; store.save(sv); }
  { Sink s2; BridgeSession s(host, [&](const std::string& l) { s2.add(l); }, nullptr, io);
    s.post("{\"t\":\"hello\",\"v\":1,\"live\":true,\"app\":\"x\",\"id\":1}"); s.drain();
    { std::lock_guard<std::mutex> g(ctl.m); CHECK(ctl.opened.size() == 2 && ctl.opened[0].outId == "out:500r8" && ctl.opened[1].outId.empty(), "hello: the saved device is tried first, then the default"); }
    CHECK(host.active() && !s2.find({"\"kind\":\"lost\"", "\"which\":\"out\"", "\"fellBack\":true", "out:500r8"}).empty(), "the page is told the saved output was not there");
    s.shutdown(); }
  run = false; dev.join(); host.shutdown();
  fs::remove_all(dir);
  DONE("audio_session");
}
