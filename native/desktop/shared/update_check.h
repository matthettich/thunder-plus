// H8: the updater, CHECK ONLY (no download, no install). The Electron app reads GitHub Releases (electron/updater.js: electron-updater, and `ghCheck` = the newest release of matthettich/thunder-plus); the
// native shells read the same feed and, when a newer version is out, fill the page's existing "update available" path: `thunder:upd-state` / the `thunder:upd` push carry `{version, avail, ready:false,
// notifyOnly:true, dl:null, file:null}`, the page shows its Update button and toast, and Update (`thunder:upd-act`) opens the release page in the browser (the Mac / portable rule of Electron minus the
// download: a silent install of an unsigned build is not offered).
// WHAT IS OFFERED: a release that is not a draft, not a pre-release, and whose tag is v<digits>(.<digits>)* (so the rolling `android-beta-test` pre-release, and any other tag, can never be offered); the
// highest version among those. Pure logic here (tested on Linux); the HTTPS GET, the browser and the message box come from IPlatform.
#ifndef TPD_UPDATE_CHECK_H
#define TPD_UPDATE_CHECK_H

#include <functional>
#include <memory>
#include <string>

namespace tpd {

constexpr const char* kReleasesFeed = "https://api.github.com/repos/matthettich/thunder-plus/releases?per_page=30";
constexpr const char* kReleasesPage = "https://github.com/matthettich/thunder-plus/releases/latest";
constexpr const char* kReleasesPrefix = "https://github.com/matthettich/thunder-plus/";   // the only places act() may open

// '1.2.10' newer than '1.2.9'; a leading v is ignored; "-suffix" (a dev build, `0.0.0-native.12`) sorts BELOW the same number without one.
bool versionNewer(const std::string& a, const std::string& b);
// v<digit>(.<digit>)*, nothing else (no suffix): "v1.3.15" yes; "android-beta-test", "v1.4.0-rc1", "1.3.15", "v" no.
bool tagIsVersion(const std::string& tag);

struct ReleaseInfo { bool found = false; std::string version, tag, url; };   // version without the v; url = the release page (https://github.com/matthettich/thunder-plus/...), or kReleasesPage
// The newest eligible release in the feed's JSON (an array of releases; one release object is accepted too).
ReleaseInfo newestRelease(const std::string& json);

class UpdateChecker {
 public:
  struct Hooks {
    // platform side (any thread; they outlive the checker): HTTPS GET of a URL, the browser, a message box for the person
    std::function<bool(const std::string& url, std::string* body)> httpGet;
    std::function<void(const std::string& url)> openUrl;
    std::function<void(const std::string& title, const std::string& message)> notify;
    // API side (called on the check's thread, never after the checker is destroyed): the state changed (the `thunder:upd` push)
    std::function<void(const std::string& stateJson)> pushState;
  };
  UpdateChecker(std::string currentVersion, Hooks hooks);
  ~UpdateChecker();    // does not wait for a check in flight (it ends by itself, and says nothing)
  std::string stateJson() const;
  // Starts a check on its own thread. byHand: the person asked (a message says "you have the newest" or "could not check"). done(ok): ok = the feed could be read (false = offline / unreadable). A check already running answers done(true) at once.
  void check(bool byHand, std::function<void(bool ok)> done);
  void act();          // open the release page in the browser (the newest one when one is known)
 private:
  struct Ctx;
  std::shared_ptr<Ctx> c_;
};

}  // namespace tpd
#endif
