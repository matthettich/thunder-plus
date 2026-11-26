// H6: `window.thunderDesktop.*` for the native shells: the 31 IPC channels the Electron app has today (docs/port/BRIDGE-TODAY.md section 4; electron/main.js + preload.js), answered by one platform-neutral
// dispatcher. The platform (WebView2 / WKWebView shell) supplies the few things that need a window: the system dialogs, "show in folder", reload, the sleep guard, closing the window.
// Transport: page -> shell: text "D:" + {"ch":"thunder:...","id":N,"a":[args]}; shell -> page: {"t":"dreply","id":N,"v":value} (or "err":text) for a `handle` channel, {"t":"dpush","ch":...,"v":...} for main -> page.
// The page's bootstrap (js/desktop_bridge.js) builds window.thunderDesktop from the same table. REAL: files, dialogs, show-in-folder, reload, playing, natural-scroll, the close and open-path hand-offs.
// "NOT YET" (a clear reply, never silence): Link (H8 / later), the updater (H8), the Electron io helper (replaced by thunderCore + H7). docs/port/H6-NOTES.md lists each.
#ifndef TPD_DESKTOP_API_H
#define TPD_DESKTOP_API_H

#include <cstdint>
#include <functional>
#include <string>

namespace tpd {

struct Channel {
  enum Kind { Handle, Send, SendSync, Push };   // Handle: ipcRenderer.invoke (a reply); Send: ipcRenderer.send; SendSync: answered by the bootstrap's constants; Push: main -> page
  const char* name; Kind kind;
};
// The 31 channels, in the order of BRIDGE-TODAY 4: 19 handle, 5 send, 1 sendSync, 6 push. kExtraChannels: what preload.js has beyond that count (the `power` push).
extern const Channel kChannels[];
constexpr int kChannelCount = 31;
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
  struct Config { std::string version = "0.0.0"; int closeTimeoutMs = 3000; std::function<int64_t()> nowMs; };
  DesktopApi(IPlatform* plat, Emit emit, Config cfg);

  // A page message after the "D:" prefix. UI thread (the dialogs). Returns false when the channel is unknown (an err reply was sent).
  bool handle(const std::string& json);

  // The window's close request (WM_CLOSE / windowShouldClose). true: close now. false: the page was asked (close-ask) and the window stays until it answers or 3 s pass (tick()).
  bool requestClose(bool quitting);
  void tick();                                  // the shell calls it every ~100 ms: the 3 s close timer
  void onLoadStart();                           // the page is (re)loading: it is not ready, a pending close answer is void
  void openPath(const std::string& path);       // a .thunder file opened from the desktop / a second launch: held until the page is ready
  void push(const char* channel, const std::string& valueJson);
  bool pageReady() const { return pageReady_; }
  // The constants the bootstrap defines before the page runs (the sendSync answers): {"link":{...},"version":...}
  std::string initJson() const;

  // File rules (electron/main.js readProj / writeProj / out-*), public for the tests.
  static bool isProj(const std::string& p);
  static std::string readProj(const std::string& p);                          // {"path","name","text"} | {"error"}
  static std::string writeProj(const std::string& p, const std::string& text);   // {"ok":true,"path"} | {"error"}; written to p + ".saving" then renamed
  static std::string writeOut(const std::string& p, const std::string& bytes);   // only .wav .zip .mid

 private:
  void reply(const std::string& id, const std::string& valueJson);
  IPlatform* plat_; Emit emit_; Config cfg_;
  bool pageReady_ = false, closeOk_ = false, quitting_ = false;
  int64_t closeDeadline_ = 0;
  std::string pendingPath_, lastOut_;
};

}  // namespace tpd
#endif
