#include "shell_core.h"

#include <chrono>
#include <cstdio>

#include "audio_out.h"
#ifndef TPD_NO_MIDI
#include "midi_manager.h"
#endif
#include "rt/json.h"
#include "text_util.h"

namespace tpd {

namespace {
std::string under(const std::string& dir, const char* leaf) { if (dir.empty()) return ""; const char last = dir.back(); return dir + ((last == '/' || last == '\\') ? "" : "/") + leaf; }
}

ShellCore::ShellCore(IPlatform* plat, Options opt)
    : opt_(std::move(opt)), recovery_(under(opt_.dataDir, "recovery")),
      api_(plat, [this](const std::string& j) { toPage(j); }, [&] {
        DesktopApi::Config c; c.version = opt_.version; c.asyncIo = true;   // T-1: the disk work is off the UI thread
        c.allowFile = under(opt_.dataDir, "allowed-paths.txt");
        c.onSaved = [this] { recovery_.drop(); };   // a saved project makes the last recovery copy older than the file
        return c; }()) {
  AudioOutFactory f = opt_.nullAudio || !opt_.audio ? AudioOutFactory([] { return makeNullAudioOut(); }) : opt_.audio;
  host_.reset(new LiveHost(std::move(f)));
  SessionIo io;
  const bool real = !opt_.nullAudio && opt_.audio;   // the null output has no device list, no input and no MIDI
  io.devices = real && opt_.devices ? opt_.devices : makeNullEnumerator();
  settings_.reset(new AudioSettingsStore(under(opt_.dataDir, "audio")));
  io.settings = settings_.get();
  io.devWatchMs = real ? opt_.devWatchMs : 0;
  if (real && opt_.input) { input_.reset(new InputCapture(opt_.input, [this](const std::string& j) { toPage(j); })); io.input = input_.get(); }
#ifndef TPD_NO_MIDI
  if (real && opt_.midi) {
    std::shared_ptr<MidiBackend> be = opt_.midi();
    if (be) { midi_ = std::make_shared<MidiManager>(std::move(be), [this](const std::string& j) { toPage(j); }); host_->setMidi(midi_.get()); io.midi = midi_.get(); }
  }
#endif
  session_.reset(new BridgeSession(*host_, [this](const std::string& j) { toPage(j); }, &recovery_, io));
  if (!opt_.a5LogPath.empty()) {
    a5_ = std::thread([this] {
      for (;;) {
        { std::unique_lock<std::mutex> lk(a5m_); if (a5cv_.wait_for(lk, std::chrono::seconds(10), [&] { return a5stop_; })) return; }
        if (!host_->active()) continue;
        std::FILE* f = std::fopen(opt_.a5LogPath.c_str(), "ab");
        if (f) { const std::string l = "{" + host_->rtSnapshotJson() + "}\n"; std::fwrite(l.data(), 1, l.size(), f); std::fclose(f); }
      }
    });
  }
}

ShellCore::~ShellCore() { shutdown(); }

bool ShellCore::shutdown(int timeoutMs) {
  api_.stopIo();   // the IO worker emits through toPage: it ends before anything it uses
  if (session_ && !session_->shutdown(timeoutMs)) return false;   // the worker is inside a call on the host: the host must not be torn down under it
  { std::lock_guard<std::mutex> g(a5m_); a5stop_ = true; }
  a5cv_.notify_all();
  if (a5_.joinable()) a5_.join();
  if (input_) input_->close();
  if (host_) host_->shutdown();
  return true;
}

void ShellCore::toPage(const std::string& lines) {
  bool request = false;
  { std::lock_guard<std::mutex> g(m_); if (!buf_.empty()) buf_ += '\n'; buf_ += lines; if (!flushPending_) { flushPending_ = true; request = true; } }
  if (request && opt_.requestFlush) opt_.requestFlush();
}

void ShellCore::flush() {
  std::string out;
  { std::lock_guard<std::mutex> g(m_); out.swap(buf_); flushPending_ = false; }
  if (!out.empty() && opt_.deliver) opt_.deliver(out);
}

void ShellCore::onPageMessage(const std::string& m) {
  try {
    if (m.size() > kMaxPageMessage) { toPage("{\"t\":\"err\",\"where\":\"message\",\"msg\":\"refused: a message over 720 MiB\",\"fatal\":false}"); return; }
    if (m.size() >= 2 && m[0] == 'D' && m[1] == ':') { api_.handle(m.substr(2)); return; }
    if (m.size() >= 2 && m[0] == 'S' && m[1] == ':') {   // S-3: the selftest verdict only counts under --selftest; in a normal run a page cannot end the process with it
      if (!opt_.selfTest) return;
      std::string rep; const bool ok = judgeSelfTest(m.substr(2), &rep); if (opt_.onSelfTest) opt_.onSelfTest(ok, rep); return;
    }
    session_->post(m);
  } catch (...) {}   // R-2: nothing escapes to the web view's callback
}

std::string ShellCore::bootstrapScript(const char* js, size_t len) const {
  std::string s(js, len);
  // S-2: the bridge exists in the main frame of the app's own origin only: not in a frame, not on another origin (document-start scripts run in every frame)
  s = "if(window.top===window&&location.protocol==='app:'&&location.hostname==='thunder'){\n" + s + "\n}";
  const std::string key = "var INIT = __TPD_INIT__;";   // the one place the placeholder is used
  const size_t p = s.find(key);
  if (p != std::string::npos) s.replace(p, key.size(), "var INIT = " + api_.initJson() + ";");
  return s;
}

const char* ShellCore::selfTestCall() {
  return "(function(){var post=function(s){if(window.chrome&&window.chrome.webview)window.chrome.webview.postMessage(s);else window.webkit.messageHandlers.thunder.postMessage(s);};"
         "try{window.__thunderSelfTest().then(function(r){post('S:'+JSON.stringify(r));},function(e){post('S:'+JSON.stringify({ok:false,failed:['selftest threw: '+e],answered:[]}));});}"
         "catch(e){post('S:'+JSON.stringify({ok:false,failed:['selftest threw: '+e],answered:[]}));}})();";
}

bool judgeSelfTest(const std::string& json, std::string* report) {
  thunder::JsonDoc d; std::string rep;
  if (!d.parse(json) || !d.root().isObj()) { if (report) *report = "the selftest result is not JSON"; return false; }
  const thunder::Json r = d.root(), ans = r.arr("answered"), fail = r.arr("failed");
  bool ok = r.boolean("ok");
  for (size_t i = 0; i < fail.size(); i++) { rep += "FAILED: " + std::string(fail.at(i).asStr()) + "\n"; ok = false; }
  int missing = 0;
  auto answered = [&](const std::string& n) { for (size_t i = 0; i < ans.size(); i++) if (ans.at(i).asStr() == n) return true; return false; };
  for (int i = 0; i < kChannelCount; i++) if (!answered(kChannels[i].name)) { rep += std::string("NOT ANSWERED: ") + kChannels[i].name + "\n"; missing++; }
  for (const char* n : {"core.hello", "core.dev.list"}) if (!answered(n)) { rep += std::string("NOT ANSWERED: ") + n + "\n"; missing++; }
  if (missing) ok = false;
  rep += "answered " + std::to_string(ans.size()) + " channel checks; page errors: " + std::to_string(r.arr("pageErrors").size()) + "\n";
  for (size_t i = 0; i < r.arr("pageErrors").size(); i++) rep += "  page error: " + std::string(r.arr("pageErrors").at(i).asStr()) + "\n";
  const thunder::Json h = r.obj("hello");
  if (!h.missing()) rep += "hello: v " + std::to_string((int)h.num("v")) + ", sr " + std::to_string((int)h.num("sr")) + ", host " + std::string(h.str("host")) + ", nativeAudio " + (h.boolean("nativeAudio") ? "yes" : "no") + (h.str("err").empty() ? "" : ", err " + std::string(h.str("err"))) + "\n";
  const thunder::Json pg = r.obj("page");
  if (!pg.missing()) rep += "page: readyState " + std::string(pg.str("state")) + ", title '" + std::string(pg.str("title")) + "', body children " + std::to_string((int)pg.num("bodyChildren")) + "\n";
  if (report) *report = rep;
  return ok;
}

}  // namespace tpd
