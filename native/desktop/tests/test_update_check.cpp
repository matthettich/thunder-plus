// H8: the updater (update_check.h): which release is offered, and the thunderDesktop upd-* channels through DesktopApi with a fake platform.
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include "check.h"
#include "desktop_api.h"
#include "rt/json.h"
#include "update_check.h"
using namespace tpd;

static std::string rel(const char* tag, bool pre = false, bool draft = false, const char* url = nullptr, bool native = true, bool electron = true) {
  const std::string ver = std::string(tag).substr(1 < std::string(tag).size() ? 1 : 0), nat = std::string(kNativeAssetPrefix) + ver + "-" + kNativeAssetSuffix;
  std::string assets = "[";
  if (electron) assets += "{\"name\":\"Thunder-Plus-" + ver + "-portable.exe\"},{\"name\":\"Thunder-Plus-" + ver + ".dmg\"}";
  if (native) assets += std::string(electron ? "," : "") + "{\"name\":\"" + nat + "\"}";
  assets += "]";
  return std::string("{\"tag_name\":\"") + tag + "\",\"assets\":" + assets + ",\"prerelease\":" + (pre ? "true" : "false") + ",\"draft\":" + (draft ? "true" : "false") +
         ",\"html_url\":\"" + (url ? url : (std::string("https://github.com/matthettich/thunder-plus/releases/tag/") + tag).c_str()) + "\"}";
}
struct Plat : IPlatform {
  std::mutex m; std::string feed; bool ok = true, can = true; std::string asked, opened; std::vector<std::string> notes; int delayMs = 0;
  std::string pickOpenFile(const std::string&) override { return ""; }
  std::string pickSaveFile(const std::string&, const std::string&, const std::string&, const std::string&) override { return ""; }
  void showInFolder(const std::string&) override {} void reloadPage() override {} void setPlaying(bool) override {} bool naturalScroll() override { return false; }
  void closeWindow(bool) override {} std::string knownDir(const std::string&) override { return "/"; }
  bool canHttp() override { return can; }
  bool httpGet(const std::string& u, std::string* b) override { if (delayMs) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs)); std::lock_guard<std::mutex> g(m); asked = u; if (!ok) return false; *b = feed; return true; }
  void openUrl(const std::string& u) override { std::lock_guard<std::mutex> g(m); opened = u; }
  void notify(const std::string&, const std::string& t) override { std::lock_guard<std::mutex> g(m); notes.push_back(t); }
};
template <class F> static bool waitFor(F f, int ms = 3000) { for (int i = 0; i < ms / 10; i++) { if (f()) return true; std::this_thread::sleep_for(std::chrono::milliseconds(10)); } return f(); }

int main() {
  // versions
  CHECK(versionNewer("1.2.10", "1.2.9") && !versionNewer("1.2.9", "1.2.10") && versionNewer("1.3.15", "1.3") && !versionNewer("1.3.0", "1.3") && !versionNewer("1.3", "1.3.0"), "dotted numbers compare numerically");
  CHECK(versionNewer("v2.0.0", "1.9.9") && !versionNewer("1.0.0", "1.0.0"), "a leading v is ignored; equal is not newer");
  CHECK(versionNewer("1.3.15", "0.0.0-native.12") && versionNewer("0.0.0", "0.0.0-native.12") && !versionNewer("0.0.0-native.12", "0.0.0"), "a dev suffix sorts below the plain number");
  // tags
  CHECK(tagIsVersion("v1.3.15") && tagIsVersion("v2") && tagIsVersion("v10.0.1.4"), "v<digits>(.<digits>)* is a version tag");
  CHECK(!tagIsVersion("android-beta-test") && !tagIsVersion("v") && !tagIsVersion("1.3.15") && !tagIsVersion("v1.4.0-rc1") && !tagIsVersion("vNext") && !tagIsVersion("v1..2") && !tagIsVersion("v1.") && !tagIsVersion("v1.2.x") && !tagIsVersion("") && !tagIsVersion("V1.2"), "everything else is not");
  // the feed
  { const std::string f = "[" + rel("android-beta-test", true) + "," + rel("v1.3.16", true) + "," + rel("v1.3.17", false, true) + "," + rel("nightly") + "," + rel("v1.3.15") + "," + rel("v1.3.9") + "]";
    ReleaseInfo r = newestRelease(f); CHECK(r.found && r.version == "1.3.15" && r.tag == "v1.3.15" && r.url.find("/releases/tag/v1.3.15") != std::string::npos, "the newest NON-pre-release, NON-draft v-tag wins (got %s)", r.version.c_str()); }
  { ReleaseInfo r = newestRelease("[" + rel("android-beta-test", true) + "," + rel("android-beta-test", false) + "]"); CHECK(!r.found, "the rolling android-beta-test release is never offered, pre-release flag or not"); }
  { ReleaseInfo r = newestRelease("[" + rel("v1.3.15", false, false, "https://evil.example/x") + "]"); CHECK(r.found && r.url == kReleasesPage, "a release URL outside the repo is replaced by the releases page"); }
  // NATIVE assets only: Electron releases are ignored until the first native release exists
  { const std::string f = "[" + rel("v1.3.15", false, false, nullptr, false, true) + "," + rel("v1.3.14", false, false, nullptr, false, true) + "]";
    CHECK(!newestRelease(f).found, "a list with only Electron assets offers nothing");
    const std::string g = "[" + rel("v1.3.15", false, false, nullptr, false, true) + "," + rel("v1.4.0", false, false, nullptr, true, false) + "]";
    ReleaseInfo r = newestRelease(g); CHECK(r.found && r.version == "1.4.0", "a release with a native asset of a higher version is offered (got %s)", r.version.c_str());
    ReleaseInfo o = newestRelease(g, "other-platform.zip"); CHECK(!o.found, "a native asset for another platform does not count");
    CHECK(!newestRelease("[{\"tag_name\":\"v2.0.0\",\"assets\":[{\"name\":\"Thunder-Plus-native-2.0.1-" + std::string(kNativeAssetSuffix) + "\"}]}]").found, "a native asset of another version than the tag does not count"); }
  CHECK(!newestRelease("").found && !newestRelease("not json").found && !newestRelease("[]").found && !newestRelease("{\"message\":\"rate limited\"}").found && !newestRelease("[1,null,\"x\"]").found, "garbage and empty feeds offer nothing");
  CHECK(newestRelease(rel("v9.0.0")).found, "a single release object is accepted");

  // the channels
  { Plat p; std::mutex m; std::vector<std::string> out;
    DesktopApi::Config c; c.version = "1.3.14"; DesktopApi api(&p, [&](const std::string& j) { std::lock_guard<std::mutex> g(m); out.push_back(j); }, c);
    auto has = [&](const char* s) { std::lock_guard<std::mutex> g(m); for (auto& o : out) if (o.find(s) != std::string::npos) return true; return false; };
    p.feed = "[" + rel("android-beta-test", true) + "," + rel("v1.3.15") + "]";
    api.handle("{\"ch\":\"thunder:upd-state\",\"id\":1,\"a\":[]}");
    CHECK(has("\"id\":1,\"v\":{\"version\":\"1.3.14\",\"avail\":null,\"ready\":false,\"notifyOnly\":true,\"dl\":null,\"file\":null}"), "state before any check: nothing available");
    api.handle("{\"ch\":\"thunder:upd-check\",\"id\":2,\"a\":[]}");
    CHECK(waitFor([&] { return has("\"id\":2"); }), "upd-check answers after the feed is read");
    CHECK(has("\"id\":2,\"v\":true") && has("\"t\":\"dpush\",\"ch\":\"thunder:upd\"") && has("\"avail\":\"1.3.15\""), "a newer release: reply true and the page is pushed the state with avail");
    CHECK(p.asked.find("api.github.com/repos/matthettich/thunder-plus/releases") != std::string::npos, "the GitHub releases feed of the repo is read");
    CHECK(waitFor([&] { std::lock_guard<std::mutex> g(p.m); return !p.notes.empty(); }) && p.notes[0].find("1.3.15") != std::string::npos, "a by-hand check says so in a message");
    api.handle("{\"ch\":\"thunder:upd-state\",\"id\":3,\"a\":[]}"); CHECK(has("\"id\":3,\"v\":{\"version\":\"1.3.14\",\"avail\":\"1.3.15\""), "state now carries avail");
    api.handle("{\"ch\":\"thunder:upd-act\",\"id\":4,\"a\":[]}");
    { std::lock_guard<std::mutex> g(p.m); CHECK(p.opened == "https://github.com/matthettich/thunder-plus/releases/tag/v1.3.15", "Update opens the release page of the new version (got %s)", p.opened.c_str()); }
    CHECK(has("\"id\":4,\"v\":true"), "upd-act answers true");
    api.waitIdle();
  }
  // up to date, offline, pre-release only
  { Plat p; std::mutex m; std::vector<std::string> out; DesktopApi::Config c; c.version = "1.3.15"; DesktopApi api(&p, [&](const std::string& j) { std::lock_guard<std::mutex> g(m); out.push_back(j); }, c);
    auto has = [&](const char* s) { std::lock_guard<std::mutex> g(m); for (auto& o : out) if (o.find(s) != std::string::npos) return true; return false; };
    p.feed = "[" + rel("v1.3.15") + "," + rel("android-beta-test", true) + "]";
    api.handle("{\"ch\":\"thunder:upd-check\",\"id\":1,\"a\":[]}"); CHECK(waitFor([&] { return has("\"id\":1,\"v\":true"); }), "up to date: true");
    CHECK(waitFor([&] { std::lock_guard<std::mutex> g(p.m); return !p.notes.empty(); }) && p.notes[0].find("newest") != std::string::npos && !has("thunder:upd\""), "up to date: 'you have the newest', no push");
    { std::lock_guard<std::mutex> g(p.m); p.ok = false; p.notes.clear(); }
    api.handle("{\"ch\":\"thunder:upd-check\",\"id\":2,\"a\":[]}"); CHECK(waitFor([&] { return has("\"id\":2,\"v\":false"); }), "offline: false");
    CHECK(waitFor([&] { std::lock_guard<std::mutex> g(p.m); return !p.notes.empty(); }) && p.notes[0].find("offline") != std::string::npos, "offline: the message says so");
    api.handle("{\"ch\":\"thunder:upd-act\",\"id\":3,\"a\":[]}"); { std::lock_guard<std::mutex> g(p.m); CHECK(p.opened == kReleasesPage, "with nothing known Update opens the releases page"); }
    // a platform with no network answers false at once (headless, selftest)
    Plat q; q.can = false; std::vector<std::string> o2; DesktopApi a2(&q, [&](const std::string& j) { o2.push_back(j); }, c); a2.handle("{\"ch\":\"thunder:upd-check\",\"id\":9,\"a\":[]}");
    CHECK(o2.size() == 1 && o2[0].find("\"id\":9,\"v\":false") != std::string::npos, "no network capability: an immediate false");
  }
  // an auto check: 8 s after the first tick, then every 4 h; never in a build that leaves it off
  { Plat p; std::mutex m; std::vector<std::string> out; int64_t now = 1000; DesktopApi::Config c; c.version = "1.0.0"; c.autoUpdateCheck = true; c.nowMs = [&] { return now; };
    DesktopApi api(&p, [&](const std::string& j) { std::lock_guard<std::mutex> g(m); out.push_back(j); }, c); p.feed = "[" + rel("v1.0.1") + "]";
    auto asked = [&] { std::lock_guard<std::mutex> g(p.m); return !p.asked.empty(); };
    api.tick(); now += 7000; api.tick(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); CHECK(!asked(), "no check before 8 s");
    now += 1500; api.tick(); CHECK(waitFor(asked), "the check runs after 8 s");
    { std::lock_guard<std::mutex> g(p.m); p.asked.clear(); } now += 3600 * 1000; api.tick(); std::this_thread::sleep_for(std::chrono::milliseconds(100)); CHECK(!asked(), "not again within the 4 hours");
    now += 3 * 3600 * 1000 + 10; api.tick(); CHECK(waitFor(asked), "again after 4 hours");
    { std::lock_guard<std::mutex> g(m); bool pushed = false; for (auto& o : out) if (o.find("\"thunder:upd\"") != std::string::npos && o.find("1.0.1") != std::string::npos) pushed = true; CHECK(pushed, "an automatic check pushes the new version without any message box"); }
    std::lock_guard<std::mutex> g(p.m); CHECK(p.notes.empty(), "an automatic check opens no message box"); }
  // closing while a check is in flight: no crash, nothing said
  { Plat p; p.delayMs = 300; p.feed = "[" + rel("v9.9.9") + "]"; std::atomic<int> got{0};
    { DesktopApi::Config c; c.version = "1.0.0"; DesktopApi api(&p, [&](const std::string&) { got++; }, c); api.handle("{\"ch\":\"thunder:upd-check\",\"id\":1,\"a\":[]}"); }
    std::this_thread::sleep_for(std::chrono::milliseconds(600)); CHECK(got == 0 && p.notes.empty(), "a check that ends after the app is gone says nothing"); }
  DONE("test_update_check");
}
