// H6: the thunderCore bridge end to end on the host (bridge_session.h): the page's messages in, replies and events out, with the live engine behind a hand-driven output.
// The same sequence the page client sends: hello (ask), song.load (ask), kit.load, a put + buf.use + pads.commit, play, mix.set, stop; the refusals; engine.reset.
#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include "bridge_session.h"
#include "pad_filler.h"
#include "check.h"
#include "rt/json.h"
#include "test_audio.h"
#include "text_util.h"
static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

struct Sink {
  std::mutex m; std::vector<std::string> lines;
  void add(const std::string& block) { std::lock_guard<std::mutex> g(m); size_t i = 0; while (i < block.size()) { size_t j = block.find('\n', i); if (j == std::string::npos) j = block.size(); if (j > i) lines.push_back(block.substr(i, j - i)); i = j + 1; } }
  std::vector<std::string> all() { std::lock_guard<std::mutex> g(m); return lines; }
  // the first line containing every part, from index `from`
  std::string find(std::initializer_list<const char*> parts, size_t from = 0) { auto v = all(); for (size_t i = from; i < v.size(); i++) { bool ok = true; for (auto p : parts) if (v[i].find(p) == std::string::npos) ok = false; if (ok) return v[i]; } return ""; }
  int count(const char* part) { int n = 0; for (auto& l : all()) if (l.find(part) != std::string::npos) n++; return n; }
};

int main() {
  ManualOut* mo = nullptr;
  tpd::LiveHost host([&] { auto o = std::unique_ptr<ManualOut>(new ManualOut(48000)); mo = o.get(); return std::unique_ptr<tpd::AudioOut>(std::move(o)); });
  Sink sink;
  tpd::BridgeSession s(host, [&](const std::string& l) { sink.add(l); });
  // a device thread: ticks the output while the test runs
  std::atomic<bool> run{true}; std::atomic<double> peak{0};
  std::thread dev([&] { while (run) { if (mo && host.active()) { auto b = mo->tick(480); double p = 0; for (float f : b) p = std::fmax(p, std::fabs(f)); if (p > peak) peak = p; } std::this_thread::sleep_for(std::chrono::milliseconds(5)); } });

  // before hello: nothing is on, a command is answered with an error, never a crash
  s.post("{\"t\":\"bpm\",\"v\":100}"); s.drain();
  CHECK(sink.count("\"t\":\"err\"") == 1 && sink.find({"engine is down"}) != "", "a command before hello: err event");
  s.post("{\"t\":\"hello\",\"v\":1,\"app\":\"x\",\"id\":1}"); s.drain();
  { auto l = sink.find({"\"t\":\"reply\"", "\"id\":1"}); CHECK(l.find("\"nativeAudio\":false") != std::string::npos && !host.active(), "hello without live: answered, nothing started (%s)", l.c_str()); }
  s.post("{\"t\":\"hello\",\"v\":1,\"live\":true,\"app\":\"x\",\"id\":2}"); s.drain();
  { auto l = sink.find({"\"t\":\"reply\"", "\"id\":2"}); thunder::JsonDoc d; CHECK(d.parse(l), "hello reply is JSON: %s", l.c_str());
    thunder::Json r = d.root(); thunder::Json caps = r.obj("caps");
    CHECK(host.active() && r.num("v") == 1 && r.num("sr") == 48000 && r.str("host") == "desktop" && caps.boolean("nativeAudio") && !caps.boolean("midi") && caps.arr("engines").size() >= 26 && r.boolean("live"), "hello live: v, sr, host, caps (%s)", l.substr(0, 200).c_str()); }
  const std::string file = slurp(std::string(TPD_REPO) + "/sounds/ELECTRO.thunder");
  const std::string S(tpa::extractMember(file, "song")), K(tpa::extractMember(file, "kit"));
  s.post("{\"t\":\"song.load\",\"title\":\"x\",\"S\":" + S + ",\"vars\":null,\"rev\":7,\"id\":3}"); s.drain();
  { auto l = sink.find({"\"t\":\"reply\"", "\"id\":3"}); thunder::JsonDoc d; d.parse(l); CHECK(d.root().get("pending").asBool() && !d.root().get("ok").asBool() && (int)d.root().num("rev") == 7, "song.load before a kit: pending, rev 7 (%s)", l.substr(0, 200).c_str());
    CHECK(sink.find({"\"t\":\"fit\""}) != "", "a fit event follows"); }
  s.post("{\"t\":\"kit.load\",\"kit\":" + K + ",\"rev\":1}"); s.drain();
  { auto l = sink.find({"\"t\":\"kit\"", "\"ok\":true"}); CHECK(l != "" && l.find("\"pads\":") != std::string::npos && l.find("\"empty\":[") != std::string::npos, "kit event with pads and empty: %s", l.substr(0, 160).c_str());
    CHECK(sink.count("\"t\":\"fit\"") == 2, "a second fit event follows the kit"); }
  // a put and its use: a mono pad of 4800 frames into slot 200 (an empty slot of the kit), in two chunks
  { std::vector<float> x(4800); for (size_t i = 0; i < x.size(); i++) x[i] = 0.5f * std::sin((float)i * 0.05f);
    for (int part = 0; part < 2; part++) {
      s.post("{\"t\":\"put.header\",\"pid\":1,\"id\":0,\"c\":0,\"o\":" + std::to_string(part * 2400) + ",\"n\":4800,\"last\":" + (part ? "true" : "false") + ",\"sr\":48000,\"ch\":1,\"frames\":4800}");
      s.post("B:" + tpd::base64Encode((const uint8_t*)x.data() + part * 2400 * 4, 2400 * 4));
    }
    s.drain();
    CHECK(sink.find({"\"t\":\"put.done\"", "\"ok\":true"}) != "", "put.done ok");
    s.post("{\"t\":\"buf.use\",\"id\":0,\"pad\":200,\"layer\":0,\"kind\":\"pad\"}"); s.post("{\"t\":\"buf.free\",\"id\":0}"); s.post("{\"t\":\"pads.commit\",\"n\":1}"); s.drain();
    CHECK(sink.find({"\"where\":\"buf.use\""}) == "" && sink.find({"\"where\":\"pads.commit\""}) == "", "buf.use and pads.commit raised no error: %s", sink.find({"\"t\":\"err\""}, 1).c_str());
    s.post("{\"t\":\"buf.use\",\"id\":9,\"pad\":201,\"layer\":0,\"kind\":\"pad\"}"); s.drain(); CHECK(sink.find({"no put with id 9"}) != "", "buf.use without a put: err");
    // a broken put: bytes with no header, and an incomplete transfer
    s.post("B:AAAA"); s.drain(); CHECK(sink.find({"without a put.header"}) != "", "bytes without a header: err");
  }
  s.post("{\"t\":\"mix.set\",\"tr\":0,\"k\":\"vol\",\"v\":0.8,\"ramp\":480}"); s.post("{\"t\":\"meters.want\",\"rate\":30}"); s.post("{\"t\":\"bpm\",\"v\":120}"); s.drain();
  s.post("{\"t\":\"play\",\"mode\":\"song\",\"row\":0}"); s.drain();
  std::this_thread::sleep_for(std::chrono::milliseconds(1200));
  CHECK(sink.count("\"t\":\"clock\"") > 0 && sink.find({"\"t\":\"clock\"", "\"load\":"}) != "", "clock events carry the load figure");
  CHECK(sink.count("\"t\":\"meters\"") > 0, "meters events arrive (%d)", sink.count("\"t\":\"meters\""));
  CHECK(sink.find({"\"t\":\"state\""}) != "" || sink.find({"\"t\":\"pos\""}) != "", "state / pos events");
  CHECK(peak > 0.001, "the song made sound (peak %.4f)", (double)peak);
  CHECK(sink.find({"\"where\":\"play\""}) == "", "play raised no error");
  // refusals: an ask carries an error reply
  s.post("{\"t\":\"dev.list\",\"id\":40}"); s.post("{\"t\":\"song.patch\",\"id\":41}"); s.post("{\"t\":\"mix.set\",\"tr\":1,\"k\":\"send\",\"v\":1}"); s.post("{\"t\":\"midi.cfg\",\"outs\":[\"\",\"\",\"\",\"\"],\"clockOut\":[false,false,false,false],\"gearDelay\":[0,0,0,0]}"); s.drain();
  CHECK(sink.find({"\"t\":\"reply\"", "\"id\":40", "\"err\":"}) != "" && sink.find({"\"t\":\"reply\"", "\"id\":41", "\"err\":"}) != "", "refused asks get an error reply");
  CHECK(sink.find({"\"where\":\"midi.cfg\"", "H7"}) != "", "midi.cfg: err naming H7");
  s.post("{\"t\":\"stop\"}"); s.drain();
  // engine.reset: down, up, and the page's state is gone
  const int ups = sink.count("\"up\":true");
  s.post("{\"t\":\"engine.reset\"}"); s.drain();
  CHECK(sink.count("\"up\":false") == 1 && sink.count("\"up\":true") == ups + 1 && host.active(), "engine.reset: engine up:false then up:true");
  s.post("{\"t\":\"song.load\",\"title\":\"x\",\"S\":" + S + ",\"vars\":null,\"rev\":8,\"id\":50}"); s.drain();
  CHECK(sink.find({"\"t\":\"reply\"", "\"id\":50", "\"pending\":true"}) != "", "after a reset the kit is gone: the song is pending again");
  run = false; dev.join();
  s.shutdown(); host.shutdown();
  DONE("test_desktop_session");
}
