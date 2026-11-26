// H6: `window.thunderDesktop.*` for the native shells: the 31 IPC channels the Electron app has today (docs/port/BRIDGE-TODAY.md section 4; electron/main.js + preload.js), answered by one platform-neutral
// dispatcher. The platform (WebView2 / WKWebView shell) supplies the few things that need a window: the system dialogs, "show in folder", reload, the sleep guard, closing the window.
// Transport: page -> shell: text "D:" + {"ch":"thunder:...","id":N,"a":[args]}; shell -> page: {"t":"dreply","id":N,"v":value} (or "err":text) for a `handle` channel, {"t":"dpush","ch":...,"v":...} for main -> page.
// The page's bootstrap (js/desktop_bridge.js) builds window.thunderDesktop from the same table. REAL: files, dialogs, show-in-folder, reload, playing, natural-scroll, the close and open-path hand-offs.
// "NOT YET" (a clear reply, never silence): Link (H8 / later), the updater (H8), the Electron io helper (replaced by thunderCore + H7). docs/port/H6-NOTES.md lists each.
#ifndef TPD_DESKTOP_API_H
#define TPD_DESKTOP_API_H

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace tpd {

struct Channel {
  enum Kind { Handle, Send, SendSync, Push };   // Handle: ipcRenderer.invoke (a reply); Send: ipcRenderer.send; SendSync: answered by the bootstrap's constants; Push: main -> page
  const char* name; Kind kind;
};
// The 31 channels, in the order of BRIDGE-TODAY 4: 19 handle, 5 send, 1 sendSync, 6 push. kExtraChannels: what preload.js has beyond that count (the `power` push).
extern const Channel kChannels[];
constexpr int kChannelCount = 31;
// R-1: what one file call may carry (decided here, noted in H6-NOTES): a project file read or saved, and a render written (decoded bytes).
constexpr size_t kMaxProjBytes = (size_t)256 << 20;
constexpr size_t kMaxOutBytes = (size_t)512 << 20;
extern const Channel kExtraChannels[];
constexpr int kExtraChannelCount = 1;

class IPlatform {
 public:
  virtual ~IPlatform() = default;
  // The system Open dialog for a project (.thunder / .json), starting in startDir ("" = the platform's choice). UTF-8 path, "" when cancelled.
  virtual std::string pickOpenFile(const std::string& startDir) = 0;
  // The Save dialog: default folder + file name, a filter label and an extension (no dot). UTF-8 path, "" when cancelled.
  virtual std::string pickSaveFile(const std::string& dir, const std::string& name, const std::string& filterName, const std::string& ext) = 0;
  virtual void showInFolder(const std::string& path) = 0;
  virtual void reloadPage() = 0;
  virtual void setPlaying(bool on) = 0;        // the sleep / App Nap guard while a song plays
  virtual bool naturalScroll() = 0;
  virtual void closeWindow(bool quit) = 0;     // the page said yes (or did not answer): close for good
  virtual std::string knownDir(const std::string& which) = 0;   // "documents" | "music"
};

class DesktopApi {
 public:
  using Emit = std::function<void(const std::string& json)>;
  struct Config {
    std::string version = "0.0.0"; int closeTimeoutMs = 3000; std::function<int64_t()> nowMs;
    bool asyncIo = false;                      // T-1: file reads / writes and base64 decoding run on a worker thread and the reply is emitted from there (Emit must then be thread-safe); false: inline (tests)
    std::function<void()> onSaved;             // a project file was written (any thread): the recovery copy is older than the save
    std::vector<std::string> allowDirs;        // S-1: folders whose files the page may read / write without a dialog (the app's own songs folder, if there is one; none today)
    std::string allowFile;                     // S-1: where the list of picked paths is kept between runs (so "Open recent" works); "" = this run only
  };
  DesktopApi(IPlatform* plat, Emit emit, Config cfg);
  ~DesktopApi();

  // A page message after the "D:" prefix. UI thread (the dialogs). Returns false when the channel is unknown (an err reply was sent).
  bool handle(const std::string& json);

  // The window's close request (WM_CLOSE / windowShouldClose). true: close now. false: the page was asked (close-ask) and the window stays until it answers or 3 s pass (tick()).
  bool requestClose(bool quitting);
  void tick();                                  // the shell calls it every ~100 ms: the 3 s close timer
  void onLoadStart();                           // the page is (re)loading: it is not ready, a pending close answer is void
  void openPath(const std::string& path);       // a .thunder file opened from the desktop / a second launch: held until the page is ready
  void push(const char* channel, const std::string& valueJson);
  bool pageReady() const { return pageReady_; }
  // S-1: file-read / file-save / out-write touch only a path the person picked in a dialog (file-open, file-saveas, out-pick), a path handed over by openPath, a path kept from an earlier run, or a file in allowDirs.
  bool pathAllowed(const std::string& p) const;
  void allowPath(const std::string& p);
  void stopIo();                                // finish what is queued and join the IO worker (idempotent; the destructor does it too)
  void waitIdle();                              // tests: until the IO worker has finished everything queued
  // The constants the bootstrap defines before the page runs (the sendSync answers): {"link":{...},"version":...}
  std::string initJson() const;

  // File rules (electron/main.js readProj / writeProj / out-*), public for the tests.
  static bool isProj(const std::string& p);
  static std::string readProj(const std::string& p);                          // {"path","name","text"} | {"error"}
  static std::string writeProj(const std::string& p, const std::string& text);   // {"ok":true,"path"} | {"error"}; written to p + ".saving" then renamed
  static std::string writeOut(const std::string& p, const std::string& bytes);   // only .wav .zip .mid

 private:
  void reply(const std::string& id, const std::string& valueJson);
  void runIo(std::function<void()> fn);
  void ioLoop();
  void saveAllowed();
  bool handleImpl(const std::string& json);
  IPlatform* plat_; Emit emit_; Config cfg_;
  bool pageReady_ = false, closeOk_ = false, quitting_ = false;
  int64_t closeDeadline_ = 0;
  std::string pendingPath_, lastOut_;
  std::vector<std::pair<std::string, std::string>> allowed_;   // (normalised key, path as given), oldest first, at most 64
  mutable std::mutex allowM_;
  std::mutex ioM_; std::condition_variable ioCv_, ioIdle_; std::deque<std::function<void()>> ioQ_; bool ioBusy_ = false, ioStop_ = false; std::thread io_;
};

}  // namespace tpd
#endif
