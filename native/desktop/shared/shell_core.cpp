#include "shell_core.h"

#include "audio_out.h"
#include "rt/json.h"
#include "text_util.h"

namespace tpd {

ShellCore::ShellCore(IPlatform* plat, Options opt)
    : opt_(std::move(opt)), api_(plat, [this](const std::string& j) { toPage(j); }, [&] { DesktopApi::Config c; c.version = opt_.version; return c; }()) {
  AudioOutFactory f = opt_.nullAudio || !opt_.audio ? AudioOutFactory([] { return makeNullAudioOut(); }) : opt_.audio;
  host_.reset(new LiveHost(std::move(f)));
  session_.reset(new BridgeSession(*host_, [this](const std::string& j) { toPage(j); }));
}

ShellCore::~ShellCore() { shutdown(); }

void ShellCore::shutdown() {
  if (session_) session_->shutdown();
  if (host_) host_->shutdown();
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
  if (m.size() >= 2 && m[0] == 'D' && m[1] == ':') { api_.handle(m.substr(2)); return; }
  if (m.size() >= 2 && m[0] == 'S' && m[1] == ':') { std::string rep; const bool ok = judgeSelfTest(m.substr(2), &rep); if (opt_.onSelfTest) opt_.onSelfTest(ok, rep); return; }
  session_->post(m);
}

std::string ShellCore::bootstrapScript(const char* js, size_t len) const {
  std::string s(js, len);
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
