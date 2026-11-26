// H6: ShellCore (shell_core.h): the routing and batching both shells rely on, with the real-time null audio device; and the selftest verdict.
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "check.h"
#include "desktop_api.h"
#include "shell_core.h"
using namespace tpd;
struct Plat : IPlatform {
  std::string pickOpenFile(const std::string&) override { return ""; }
  std::string pickSaveFile(const std::string&, const std::string&, const std::string&, const std::string&) override { return ""; }
  void showInFolder(const std::string&) override {} void reloadPage() override {} void setPlaying(bool) override {} bool naturalScroll() override { return false; }
  void closeWindow(bool) override {} std::string knownDir(const std::string&) override { return "/"; }
};
int main() {
  Plat plat; std::mutex m; std::string got; std::atomic<int> flushes{0}, delivers{0}; ShellCore* cp = nullptr; std::atomic<bool> verdict{false}, verdictOk{false};
  ShellCore::Options o; o.nullAudio = true; o.version = "1.2.3";
  o.requestFlush = [&] { flushes++; };   // the shell would marshal to its UI thread; here the test calls flush() itself
  o.deliver = [&](const std::string& t) { delivers++; std::lock_guard<std::mutex> g(m); got += t + "\n"; };
  o.onSelfTest = [&](bool ok, const std::string&) { verdictOk = ok; verdict = true; };
  ShellCore core(&plat, o); cp = &core; (void)cp;
  auto has = [&](const char* s) { std::lock_guard<std::mutex> g(m); return got.find(s) != std::string::npos; };
  // the bootstrap constants
  { const char js[] = "// the __TPD_INIT__ word in a comment stays\nvar INIT = __TPD_INIT__;"; const std::string s = core.bootstrapScript(js, sizeof js - 1); CHECK(s.find("var INIT = {") != std::string::npos && s.find("var INIT = __TPD_INIT__") == std::string::npos && s.find("\"version\":\"1.2.3\"") != std::string::npos, "bootstrap: constants in place: %s", s.c_str()); }
  // D: reaches the API; its reply is batched until flush()
  core.onPageMessage("D:{\"ch\":\"thunder:upd-state\",\"id\":5,\"a\":[]}");
  CHECK(flushes == 1 && delivers == 0, "a reply asks for one flush and is not delivered until flush()");
  core.onPageMessage("D:{\"ch\":\"thunder:link-now\",\"id\":6,\"a\":[]}");
  CHECK(flushes == 1, "a second line before the flush does not ask again");
  core.flush(); CHECK(delivers == 1 && has("\"id\":5") && has("\"id\":6") && has("\n"), "both replies in ONE delivery, joined by a newline");
  core.flush(); CHECK(delivers == 1, "an empty flush delivers nothing");
  // thunderCore: hello with the null device
  core.onPageMessage("{\"t\":\"hello\",\"v\":1,\"live\":true,\"app\":\"x\",\"id\":9}");
  for (int i = 0; i < 100 && flushes < 2; i++) std::this_thread::sleep_for(std::chrono::milliseconds(20));
  core.flush();
  CHECK(has("\"t\":\"reply\"") && has("\"id\":9") && has("\"nativeAudio\":true") && has("\"sr\":48000"), "hello answered by the live engine on the null device");
  // the pump delivers events (clock) while the engine runs
  for (int i = 0; i < 100 && !has("\"t\":\"clock\""); i++) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); core.flush(); }
  CHECK(has("\"t\":\"clock\""), "clock events flow through the pump");
  // the selftest verdict
  { std::string good = "{\"ok\":true,\"answered\":["; for (int i = 0; i < kChannelCount; i++) good += std::string(i ? "," : "") + "\"" + kChannels[i].name + "\""; good += ",\"core.hello\",\"core.dev.list\"],\"failed\":[],\"pageErrors\":[]}";
    std::string rep; CHECK(judgeSelfTest(good, &rep), "a complete answer list passes: %s", rep.c_str());
    std::string bad = good; const size_t p = bad.find("\"thunder:io-mic\","); bad.erase(p, 17); CHECK(!judgeSelfTest(bad, &rep) && rep.find("thunder:io-mic") != std::string::npos, "a missing channel fails and is named: %s", rep.c_str());
    CHECK(!judgeSelfTest("{\"ok\":false,\"answered\":[],\"failed\":[\"x\"]}", &rep) && !judgeSelfTest("junk", &rep), "failures and junk fail");
    core.onPageMessage("S:" + good); CHECK(!verdict, "S-3: in a normal run an S: message from the page does nothing (it cannot end the app)");
    ShellCore::Options o2 = o; o2.selfTest = true; ShellCore c2(&plat, o2); c2.onPageMessage("S:" + good); CHECK(verdict && verdictOk, "with --selftest an S: message reaches onSelfTest");
    // R-1: a message over the page limit is refused, not queued
    std::string big(ShellCore::kMaxPageMessage + 1, 'x'); core.onPageMessage(big); core.flush(); CHECK(has("refused: a message over"), "a message over the limit is refused"); }
  core.shutdown();
  DONE("test_desktop_shell_core");
}
