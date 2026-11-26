// H6: the live host (live_host.h) with a hand-driven output: start, a real song and kit from sounds/ELECTRO.thunder, play, the audio callback runs, events come out, stop, shutdown, restart.
#include <cmath>
#include <fstream>
#include <sstream>
#include <thread>
#include "check.h"
#include "live_host.h"
#include "pad_filler.h"
#include "rt/json.h"
#include "test_audio.h"
static std::string slurp(const std::string& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main() {
  ManualOut* mo = nullptr;
  tpd::LiveHost host([&] { auto o = std::unique_ptr<ManualOut>(new ManualOut(48000)); mo = o.get(); return std::unique_ptr<tpd::AudioOut>(std::move(o)); });
  CHECK(!host.active() && host.sampleRate() == 0, "not started");
  CHECK(host.songLoad("{}").rc == -2, "a load without an engine says so");
  std::string err; const int sr = host.start(true, &err);
  CHECK(sr == 48000 && host.active() && mo && mo->started_, "start: %d %s", sr, err.c_str());
  CHECK(host.start(true, &err) == 48000, "a second start is a no-op");
  const std::string file = slurp(std::string(TPD_REPO) + "/sounds/ELECTRO.thunder");
  CHECK(file.size() > 1000, "fixture present");
  const std::string_view S = tpa::extractMember(file, "song"), K = tpa::extractMember(file, "kit");
  auto r = host.songLoad("{\"t\":\"song.load\",\"title\":\"x\",\"S\":" + std::string(S) + ",\"vars\":null,\"rev\":3,\"id\":1}");
  CHECK(r.ok, "song.load: rc %d %s", r.rc, r.err.c_str());
  tpa::FitResult fr; std::string fj;
  CHECK(host.fit(3, 0, &fr, &fj) && fr.pending && !fr.ok, "before a kit there is no verdict (pending)");
  auto k = host.kitLoad("{\"t\":\"kit.load\",\"kit\":" + std::string(K) + ",\"rev\":1}");
  CHECK(k.ok && k.pads > 0, "kit.load: rc %d, %d pads rendered, %d skipped", k.rc, k.pads, k.skipped);
  CHECK(host.fit(3, 0, &fr, &fj) && !fr.pending, "after the kit there is a verdict: %s", fj.substr(0, 160).c_str());
  CHECK(host.play(0, 0, 0, -1, -1, 0, 0, 0, -1, 0, 0, 1, 0, 0) == 0, "play");
  // drive the callback in 480-frame blocks the way a device does, a little faster than real time
  double peak = 0; bool finite = true; std::string events;
  for (int i = 0; i < 300; i++) {
    auto b = mo->tick(480);
    for (float f : b) { if (!std::isfinite(f)) finite = false; peak = std::fmax(peak, std::fabs(f)); }
    if (i % 20 == 19) { std::this_thread::sleep_for(std::chrono::milliseconds(20)); events += host.drainEvents(); }
  }
  CHECK(finite && peak > 0.001, "audio is finite and not silent (peak %.4f)", peak);
  CHECK(events.find("\"t\":\"clock\"") != std::string::npos, "the event list has clock events");
  CHECK(events.find("\"t\":\"state\"") != std::string::npos || events.find("\"t\":\"pos\"") != std::string::npos, "the event list has state / pos events");
  CHECK(host.load() >= 0, "load figure");
  CHECK(host.call([](thunder_live* L) { return thunder_live_stop(L, THUNDER_LIVE_NOW); }) == 0, "stop");
  for (int i = 0; i < 4; i++) mo->tick(480);
  host.shutdown();
  CHECK(!host.active(), "shutdown closes the output");
  CHECK(host.start(true, &err) == 48000 && host.active(), "start again after a shutdown");
  host.shutdown();
  DONE("test_desktop_live_host");
}
