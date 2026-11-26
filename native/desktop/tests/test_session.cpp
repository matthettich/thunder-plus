// H6: the thunderCore bridge end to end on the host (bridge_session.h): the page's messages in, replies and events out, with the live engine behind a hand-driven output.
// The same sequence the page client sends: hello (ask), song.load (ask), kit.load, a put + buf.use + pads.commit, play, mix.set, stop; the refusals; engine.reset.
#include <atomic>
#include <cmath>
#include <filesystem>
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
  s.post("{\"t\":\"render.start\",\"id\":40}"); s.post("{\"t\":\"song.patch\",\"id\":41}"); s.post("{\"t\":\"mix.set\",\"tr\":1,\"k\":\"send\",\"v\":1}"); s.post("{\"t\":\"midi.cfg\",\"outs\":[\"\",\"\",\"\",\"\"],\"clockOut\":[false,false,false,false],\"gearDelay\":[0,0,0,0]}"); s.drain();
  CHECK(sink.find({"\"t\":\"reply\"", "\"id\":40", "\"err\":"}) != "" && sink.find({"\"t\":\"reply\"", "\"id\":41", "\"err\":"}) != "", "refused asks get an error reply");
  CHECK(sink.find({"\"where\":\"midi.cfg\"", "no MIDI"}) != "", "midi.cfg: err naming the missing MIDI");
  s.post("{\"t\":\"stop\"}"); s.drain();
  // engine.reset: down, up, and the page's state is gone
  const int ups = sink.count("\"up\":true");
  s.post("{\"t\":\"engine.reset\"}"); s.drain();
  CHECK(sink.count("\"up\":false") == 1 && sink.count("\"up\":true") == ups + 1 && host.active(), "engine.reset: engine up:false then up:true");
  s.post("{\"t\":\"song.load\",\"title\":\"x\",\"S\":" + S + ",\"vars\":null,\"rev\":8,\"id\":50}"); s.drain();
  CHECK(sink.find({"\"t\":\"reply\"", "\"id\":50", "\"pending\":true"}) != "", "after a reset the kit is gone: the song is pending again");
  // M4 parity: the caps, the meters lists, fx.values as an ask
  CHECK(sink.find({"\"id\":2", "\"meters2\":true", "\"fxValues\":true", "\"recoveryPut\":false"}) != "", "hello caps: meters2 and fxValues true, recoveryPut false without a recovery folder: %s", sink.find({"\"id\":2"}).c_str());
  s.post("{\"t\":\"meters.want\",\"rate\":10,\"fx\":[0,\"m\"],\"dyn\":[0]}"); s.post("{\"t\":\"meters.want\",\"rate\":10,\"fx\":[],\"dyn\":[]}"); s.post("{\"t\":\"meters.want\",\"rate\":10,\"fx\":[99]}"); s.drain();
  CHECK(sink.find({"\"where\":\"meters.want\"", "bad track"}) != "" && sink.count("\"where\":\"meters.want\"") == 1, "meters.want with lists reaches the core; a bad track is refused (%d errs)", sink.count("\"where\":\"meters.want\""));
  s.post("{\"t\":\"fx.values\",\"tr\":0,\"slot\":0,\"kind\":\"pump\",\"v\":[0.5,0.25],\"id\":60}"); s.post("{\"t\":\"fx.values\",\"tr\":0,\"slot\":0,\"kind\":\"pump\",\"v\":[],\"id\":61}"); s.post("{\"t\":\"fx.values\",\"tr\":0,\"slot\":9,\"kind\":\"mod\",\"v\":[1],\"id\":62}"); s.drain();
  CHECK(sink.find({"\"t\":\"reply\"", "\"id\":60", "\"ok\":false", "\"error\":\"rc "}) != "", "fx.values on an empty slot: the core refuses, the ask gets {ok:false, error}: %s", sink.find({"\"id\":60"}).c_str());
  CHECK(sink.find({"\"id\":61", "\"ok\":false", "1..512"}) != "" && sink.find({"\"id\":62", "\"ok\":false", "slot must be 0..7"}) != "", "fx.values refusals carry ok:false and error");
  // R-1: the limits, refused at once with an error reply for an ask
  CHECK(!s.post("{\"t\":\"song.load\",\"S\":\"" + std::string(tpd::kMaxSongBytes, 'x') + "\",\"id\":70}"), "a song.load over 16 MiB is refused by post()");
  CHECK(sink.find({"\"id\":70", "\"ok\":false", "16 MiB"}) != "" && sink.find({"\"where\":\"song.load\"", "refused"}) != "", "... with an err and an error reply");
  CHECK(!s.post("B:" + std::string(tpd::kMaxPutChars + 1, 'A')) && sink.find({"\"where\":\"put\"", "24 M"}) != "", "a put chunk over 24 M chars is refused");
  CHECK(s.post("{\"t\":\"bpm\",\"v\":120}"), "a normal message is accepted"); s.drain();
  // M4 recovery.* (a folder of its own): put, announce after hello, get, drop
  { namespace fs = std::filesystem; const fs::path rd = fs::temp_directory_path() / "tpd_session_recovery"; fs::remove_all(rd);
    tpd::RecoveryStore rs(rd.u8string()); Sink s2; tpd::BridgeSession b2(host, [&](const std::string& l) { s2.add(l); }, &rs);
    b2.post("{\"t\":\"recovery.get\",\"id\":1}"); b2.drain(); CHECK(s2.find({"\"id\":1", "\"ok\":false"}) != "", "no copy yet: recovery.get says so");
    b2.post("{\"t\":\"recovery.put\",\"project\":\"{\\\"app\\\":\\\"thunder-project\\\",\\\"v\\\":1,\\\"song\\\":{}}\"}"); b2.post("{\"t\":\"recovery.put\",\"project\":\"\"}"); b2.post("{\"t\":\"recovery.put\",\"project\":5}"); b2.drain();
    CHECK(rs.has() && fs::exists(rd / "recovery.put.thunder") && !fs::exists(rd / "recovery.put.thunder.tmp"), "recovery.put stored atomically; an empty or non-string project is ignored (the copy stays)");
    b2.post("{\"t\":\"hello\",\"v\":1,\"id\":2}"); b2.drain(); CHECK(s2.find({"\"t\":\"recovery\"", "\"time\":"}) != "", "the copy is announced after hello");
    b2.post("{\"t\":\"recovery.get\",\"id\":3}"); b2.drain(); CHECK(s2.find({"\"id\":3", "\"ok\":true", "\"source\":\"put\"", "thunder-project"}) != "", "recovery.get returns the put: %s", s2.find({"\"id\":3"}).c_str());
    b2.post("{\"t\":\"recovery.drop\",\"id\":4}"); b2.post("{\"t\":\"recovery.get\",\"id\":5}"); b2.drain(); CHECK(!rs.has() && s2.find({"\"id\":4", "\"ok\":true"}) != "" && s2.find({"\"id\":5", "\"ok\":false"}) != "", "recovery.drop deletes it");
    b2.shutdown(); fs::remove_all(rd); }
  // C-2: a restart that fails says so definitely: engine up:false with the error, and a fatal err
  { struct FailOut : tpd::AudioOut { bool open(Callback, void*, double*, std::string* e) override { if (e) *e = "device busy"; return false; } bool start(std::string*) override { return false; } void close() override {} };
    int made = 0; tpd::LiveHost h3([&]() -> std::unique_ptr<tpd::AudioOut> { if (made++ == 0) return std::unique_ptr<tpd::AudioOut>(new ManualOut(48000)); return std::unique_ptr<tpd::AudioOut>(new FailOut()); });
    Sink s3; tpd::BridgeSession b3(h3, [&](const std::string& l) { s3.add(l); });
    b3.post("{\"t\":\"hello\",\"v\":1,\"live\":true,\"id\":1}"); b3.drain(); b3.post("{\"t\":\"engine.reset\"}"); b3.drain();
    CHECK(s3.find({"\"t\":\"engine\"", "\"up\":false", "\"error\":\"the live engine did not restart"}) != "" && s3.find({"\"t\":\"err\"", "\"where\":\"engine.reset\"", "\"fatal\":true"}) != "" && s3.count("\"up\":true") == 0 && !h3.active(), "a failed engine.reset: engine up:false + error, a fatal err, never up:true");
    b3.shutdown(); h3.shutdown(); }
  run = false; dev.join();
  s.shutdown(); host.shutdown();
  DONE("test_desktop_session");
}
