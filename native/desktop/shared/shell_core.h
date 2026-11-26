// H6: what the two shells share beyond their windows: one object that owns the live host, the thunderCore session and the thunderDesktop API, routes the page's text messages, batches the shell -> page
// lines (events come from other threads; one script call per flush, on the UI thread), builds the bootstrap script, and judges the `--selftest` result. The shells (win/main_win.cpp, mac/main_mac.mm)
// only make a window and a web view, hand every page message to onPageMessage() and run what deliver() gives them.
#ifndef TPD_SHELL_CORE_H
#define TPD_SHELL_CORE_H

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "bridge_session.h"
#include "desktop_api.h"
#include "live_host.h"
#include "recovery_store.h"

namespace tpd {

class ShellCore {
 public:
  struct Options {
    bool nullAudio = false;                                   // --null-audio: no device (CI runners, headless)
    std::string version = "0.0.0";
    AudioOutFactory audio;                                    // the shell's real output (miniaudio); used unless nullAudio
    std::function<void()> requestFlush;                       // ANY thread: arrange for flush() to run on the UI thread soon (PostMessage / dispatch_async)
    std::function<void(const std::string& text)> deliver;     // UI thread: run window.__thunderRecv(<text as a JS string>) in the page
    bool selfTest = false;                                    // --selftest: only then is an "S:" message the selftest verdict (S-3); in a normal run it is dropped
    std::function<void(bool ok, const std::string& report)> onSelfTest;   // UI thread: the selftest verdict (called only when selfTest is set)
    std::string dataDir;                                      // the app's data folder (UTF-8): recovery copy and the list of picked file paths live under it; "" = neither is kept
  };
  ShellCore(IPlatform* plat, Options opt);
  ~ShellCore();
  // R-1 / S-3: a page message over this size is refused here (the largest legitimate one is a render's out-write).
  static constexpr size_t kMaxPageMessage = (size_t)720 << 20;
  // The page's text message (UI thread): "D:" thunderDesktop, "S:" the selftest result, "B:" and everything else thunderCore.
  void onPageMessage(const std::string& utf8);
  void flush();                                               // UI thread
  void tick() { api_.tick(); }                                // UI thread, ~100 ms
  DesktopApi& api() { return api_; }
  // The injected script: the embedded bootstrap with the constants of DesktopApi::initJson() in place of __TPD_INIT__.
  std::string bootstrapScript(const char* js, size_t len) const;
  static const char* selfTestCall();                          // the script that runs the page's selftest and posts "S:<json>" back through `post`
  void shutdown();                                            // join the threads, stop the engine (idempotent)

 private:
  void toPage(const std::string& lines);
  Options opt_;
  std::unique_ptr<LiveHost> host_;
  RecoveryStore recovery_;
  DesktopApi api_;
  std::unique_ptr<BridgeSession> session_;
  std::mutex m_; std::string buf_; bool flushPending_ = false;
};

// The selftest gate: `ok`, no failures, and every one of the 31 channels (plus core.hello and core.dev.list) answered. false: `report` says what is missing.
bool judgeSelfTest(const std::string& json, std::string* report);

}  // namespace tpd
#endif
