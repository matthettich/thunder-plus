#include "update_check.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <thread>
#include <vector>

#include "rt/json.h"
#include "text_util.h"

namespace tpd {
namespace {
struct Ver { std::vector<long> n; bool suffix = false; bool ok = false; };
Ver parseVer(std::string s) {
  Ver v; if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
  size_t i = 0;
  while (i < s.size() && std::isdigit((unsigned char)s[i])) {
    long x = 0; while (i < s.size() && std::isdigit((unsigned char)s[i])) { x = x * 10 + (s[i] - '0'); if (x > 1000000000L) x = 1000000000L; i++; }
    v.n.push_back(x); v.ok = true;
    if (i < s.size() && s[i] == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1])) i++; else break;
  }
  v.suffix = i < s.size();   // "-native.12", "-rc1", anything left over
  return v;
}
}  // namespace

bool versionNewer(const std::string& a, const std::string& b) {
  const Ver A = parseVer(a), B = parseVer(b);
  for (size_t i = 0; i < std::max(A.n.size(), B.n.size()); i++) { const long x = i < A.n.size() ? A.n[i] : 0, y = i < B.n.size() ? B.n[i] : 0; if (x != y) return x > y; }
  return !A.suffix && B.suffix;
}

bool tagIsVersion(const std::string& t) {
  if (t.size() < 2 || t[0] != 'v' || !std::isdigit((unsigned char)t[1])) return false;
  for (size_t i = 1; i < t.size(); i++) {
    if (std::isdigit((unsigned char)t[i])) continue;
    if (t[i] == '.' && i + 1 < t.size() && std::isdigit((unsigned char)t[i + 1]) && std::isdigit((unsigned char)t[i - 1])) continue;
    return false;
  }
  return true;
}

ReleaseInfo newestRelease(const std::string& json, const std::string& assetSuffix) {
  ReleaseInfo best;
  thunder::JsonDoc d; if (!d.parse(json)) return best;
  const thunder::Json root = d.root();
  auto consider = [&](thunder::Json r) {
    if (!r.isObj() || r.boolean("draft") || r.boolean("prerelease")) return;
    const std::string tag(r.str("tag_name"));
    if (!tagIsVersion(tag)) return;
    const std::string ver = tag.substr(1);
    const std::string want = std::string(kNativeAssetPrefix) + ver + "-" + assetSuffix;   // a native desktop build for this platform must be attached
    bool has = false; const thunder::Json as = r.arr("assets");
    for (size_t i = 0; i < as.size(); i++) if (as.at(i).isObj() && as.at(i).str("name") == want) has = true;
    if (!has) return;
    if (best.found && !versionNewer(ver, best.version)) return;
    best.found = true; best.tag = tag; best.version = ver;
    const std::string url(r.str("html_url"));
    best.url = url.compare(0, std::char_traits<char>::length(kReleasesPrefix), kReleasesPrefix) == 0 ? url : std::string(kReleasesPage);
  };
  if (root.isArr()) for (size_t i = 0; i < root.size(); i++) consider(root.at(i)); else consider(root);
  return best;
}

struct UpdateChecker::Ctx {
  std::string version; Hooks h;
  std::mutex m; bool alive = true, busy = false; std::string avail, availUrl;
  std::string state() const { return "{\"version\":" + jsonQuote(version) + ",\"avail\":" + (avail.empty() ? "null" : jsonQuote(avail)) + ",\"ready\":false,\"notifyOnly\":true,\"dl\":null,\"file\":null}"; }
};

UpdateChecker::UpdateChecker(std::string v, Hooks h) : c_(std::make_shared<Ctx>()) { c_->version = std::move(v); c_->h = std::move(h); }
UpdateChecker::~UpdateChecker() { std::lock_guard<std::mutex> g(c_->m); c_->alive = false; }

std::string UpdateChecker::stateJson() const { std::lock_guard<std::mutex> g(c_->m); return c_->state(); }

void UpdateChecker::check(bool byHand, std::function<void(bool)> done) {
  { std::lock_guard<std::mutex> g(c_->m); if (c_->busy) { if (done) done(true); return; } c_->busy = true; }
  std::shared_ptr<Ctx> c = c_;
  std::thread([c, byHand, done = std::move(done)] {
    std::string body; bool ok = false; ReleaseInfo r;
    try { ok = c->h.httpGet && c->h.httpGet(kReleasesFeed, &body); if (ok) r = newestRelease(body); } catch (...) { ok = false; }
    const bool newer = ok && r.found && versionNewer(r.version, c->version);
    std::string push, title, msg;
    { std::lock_guard<std::mutex> g(c->m);
      c->busy = false;
      if (!c->alive) return;   // the app is closing: nothing to say, nobody to say it to
      if (newer && c->avail != r.version) { c->avail = r.version; c->availUrl = r.url; push = c->state(); }
      if (newer) c->availUrl = r.url;
      if (c->h.pushState && !push.empty()) c->h.pushState(push);
      if (done) done(ok);
    }
    if (byHand && c->h.notify) {   // like Electron's boxes; outside the lock (a message box blocks)
      if (!ok) c->h.notify("Thunder Plus", "Couldn\xe2\x80\x99t check for updates. You may be offline. The newest version is always on the download page.");
      else if (!newer) c->h.notify("Thunder Plus", "You have the newest Thunder Plus (" + c->version + ").");
      else c->h.notify("Thunder Plus update", "Thunder Plus " + r.version + " is out. You have " + c->version + ". Press Update at the top to open the download page.");
    }
  }).detach();
}

void UpdateChecker::act() {
  std::string url; { std::lock_guard<std::mutex> g(c_->m); url = c_->avail.empty() || c_->availUrl.empty() ? std::string(kReleasesPage) : c_->availUrl; }
  if (c_->h.openUrl) c_->h.openUrl(url);
}

}  // namespace tpd
