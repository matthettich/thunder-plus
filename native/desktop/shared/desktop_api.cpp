#include "desktop_api.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <string_view>
#include <system_error>

#include "rt/json.h"
#include "text_util.h"

namespace fs = std::filesystem;

namespace tpd {

const Channel kChannels[kChannelCount] = {
  {"thunder:file-open", Channel::Handle}, {"thunder:file-read", Channel::Handle}, {"thunder:file-save", Channel::Handle}, {"thunder:file-saveas", Channel::Handle},
  {"thunder:out-pick", Channel::Handle}, {"thunder:out-write", Channel::Handle}, {"thunder:out-show", Channel::Handle},
  {"thunder:link-enable", Channel::Handle}, {"thunder:link-sync", Channel::Handle}, {"thunder:link-tempo", Channel::Handle}, {"thunder:link-start", Channel::Handle},
  {"thunder:link-stop", Channel::Handle}, {"thunder:link-now", Channel::Handle},
  {"thunder:io-start", Channel::Handle}, {"thunder:io-mic", Channel::Handle}, {"thunder:natural-scroll", Channel::Handle},
  {"thunder:upd-state", Channel::Handle}, {"thunder:upd-check", Channel::Handle}, {"thunder:upd-act", Channel::Handle},
  {"thunder:page-ready", Channel::Send}, {"thunder:close-ack", Channel::Send}, {"thunder:close-reply", Channel::Send}, {"thunder:playing", Channel::Send}, {"thunder:reload", Channel::Send},
  {"thunder:link-avail", Channel::SendSync},
  {"thunder:close-ask", Channel::Push}, {"thunder:open-path", Channel::Push}, {"thunder:link-state", Channel::Push}, {"thunder:link-time", Channel::Push}, {"thunder:upd", Channel::Push},
  {"thunder:io-port", Channel::Push}};
const Channel kExtraChannels[kExtraChannelCount] = {{"thunder:power", Channel::Push}};

namespace {
fs::path u8(const std::string& s) { return fs::u8path(s); }
std::string lowerExt(const std::string& p) { const size_t d = p.find_last_of('.'); std::string e = d == std::string::npos ? "" : p.substr(d + 1); for (auto& c : e) c = (char)std::tolower((unsigned char)c); return e; }
std::string err(const std::string& m) { return "{\"error\":" + jsonQuote(m) + "}"; }
std::string sanitizeName(std::string n) { for (auto& c : n) if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') c = '_'; return n; }
std::string parentDirIfProj(const std::string& near) {
  if (!DesktopApi::isProj(near)) return "";
  std::error_code ec; const fs::path d = u8(near).parent_path();
  return !d.empty() && fs::exists(d, ec) ? d.u8string() : "";
}
// A path as one comparable key: absolute, normalised, symlinks resolved as far as the path exists (lower-cased on Windows, where the file system is case-insensitive).
std::string pathKey(const std::string& p) {
  try {
    std::error_code ec; fs::path c = fs::weakly_canonical(u8(p), ec);
    std::string k = (ec ? u8(p).lexically_normal() : c).generic_u8string();
#ifdef _WIN32
    for (auto& ch : k) ch = (char)std::tolower((unsigned char)ch);
#endif
    return k;
  } catch (...) { return p; }
}
const size_t kAllowMax = 64;
std::string argStr(thunder::Json a, size_t i) { thunder::Json v = a.at(i); return v.isStr() ? std::string(v.asStr()) : std::string(); }
}  // namespace

bool DesktopApi::isProj(const std::string& p) { const std::string e = lowerExt(p); return e == "thunder" || e == "json"; }

std::string DesktopApi::readProj(const std::string& p) {
  try {
    if (!isProj(p)) return err("that isn\xe2\x80\x99t a .thunder or .json file");
    std::error_code ec; const bool there = fs::is_regular_file(u8(p), ec);
    if (!there) return err(fs::exists(u8(p), ec) ? "could not read it" : "it isn\xe2\x80\x99t there any more");
    const uintmax_t sz = fs::file_size(u8(p), ec);
    if (!ec && sz > kMaxProjBytes) return err("that file is too large (over 256 MiB)");
    std::ifstream in(u8(p), std::ios::binary);
    if (!in) return err("could not read it");
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return "{\"path\":" + jsonQuote(p) + ",\"name\":" + jsonQuote(u8(p).filename().u8string()) + ",\"text\":" + jsonQuote(text) + "}";
  } catch (const std::exception& e) { return err(std::string("could not read it: ") + e.what()); } catch (...) { return err("could not read it"); }
}

// Written next to the file first, then renamed over it, so a crash mid-save never leaves half a project.
static std::string writeProjImpl_(const std::string& p, const std::string& text) {
  if (!DesktopApi::isProj(p)) return err("not a project file");
  if (text.size() > kMaxProjBytes) return err("that project is too large to save (over 256 MiB)");
  const std::string tmp = p + ".saving";
  std::error_code ec;
  { std::ofstream o(u8(tmp), std::ios::binary | std::ios::trunc); if (!o) return err("could not write next to the file"); o.write(text.data(), (std::streamsize)text.size()); o.flush(); if (!o) { o.close(); fs::remove(u8(tmp), ec); return err("write failed"); } }
  fs::rename(u8(tmp), u8(p), ec);
  if (ec) { std::error_code e2; fs::remove(u8(tmp), e2); return err(ec.message()); }
  return "{\"ok\":true,\"path\":" + jsonQuote(p) + "}";
}

static std::string writeOutImpl_(const std::string& p, const std::string& bytes) {
  if (bytes.size() > kMaxOutBytes) return err("that file is too large (over 512 MiB)");
  const std::string e = lowerExt(p);
  if (e != "wav" && e != "zip" && e != "mid") return err("not a render file");
  std::ofstream o(u8(p), std::ios::binary | std::ios::trunc);
  if (!o) return err("could not write the file");
  o.write(bytes.data(), (std::streamsize)bytes.size()); o.flush();
  if (!o) return err("write failed");
  return "{\"ok\":true,\"path\":" + jsonQuote(p) + "}";
}

std::string DesktopApi::writeProj(const std::string& p, const std::string& text) {
  try { return writeProjImpl_(p, text); } catch (const std::exception& e) { return err(std::string("could not write: ") + e.what()); } catch (...) { return err("could not write"); }
}
std::string DesktopApi::writeOut(const std::string& p, const std::string& bytes) {
  try { return writeOutImpl_(p, bytes); } catch (const std::exception& e) { return err(std::string("could not write: ") + e.what()); } catch (...) { return err("could not write"); }
}

DesktopApi::DesktopApi(IPlatform* plat, Emit emit, Config cfg) : plat_(plat), emit_(std::move(emit)), cfg_(std::move(cfg)) {
  if (!cfg_.nowMs) cfg_.nowMs = [] { return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
  if (!cfg_.allowFile.empty()) {   // the paths picked in earlier runs (S-1: "Open recent" keeps working; nothing the page says can add to this list)
    try {
      std::ifstream in(u8(cfg_.allowFile), std::ios::binary); std::string line;
      while (in && std::getline(in, line)) { while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back(); if (!line.empty()) allowed_.push_back({pathKey(line), line}); }
      while (allowed_.size() > kAllowMax) allowed_.erase(allowed_.begin());
    } catch (...) {}
  }
  UpdateChecker::Hooks h;
  h.httpGet = [plat](const std::string& u, std::string* b) { return plat->httpGet(u, b); };
  h.openUrl = [plat](const std::string& u) { plat->openUrl(u); };
  h.notify = [plat](const std::string& t, const std::string& m) { plat->notify(t, m); };
  h.pushState = [this](const std::string& st) { push("thunder:upd", st); };
  upd_.reset(new UpdateChecker(cfg_.version, std::move(h)));
  if (cfg_.asyncIo) io_ = std::thread([this] { ioLoop(); });
}

DesktopApi::~DesktopApi() { upd_.reset(); stopIo(); }
void DesktopApi::stopIo() {
  { std::lock_guard<std::mutex> g(ioM_); ioStop_ = true; }
  ioCv_.notify_all();
  if (io_.joinable()) io_.join();
}

void DesktopApi::ioLoop() {
  for (;;) {
    std::function<void()> f;
    { std::unique_lock<std::mutex> lk(ioM_); ioCv_.wait(lk, [&] { return !ioQ_.empty() || ioStop_; }); if (ioQ_.empty()) return; f = std::move(ioQ_.front()); ioQ_.pop_front(); ioBusy_ = true; }
    try { f(); } catch (...) {}
    { std::lock_guard<std::mutex> g(ioM_); ioBusy_ = false; }
    ioIdle_.notify_all();
  }
}
void DesktopApi::runIo(std::function<void()> fn) {
  if (!cfg_.asyncIo || ioStop_) { try { fn(); } catch (...) {} return; }
  { std::lock_guard<std::mutex> g(ioM_); ioQ_.push_back(std::move(fn)); }
  ioCv_.notify_one();
}
void DesktopApi::waitIdle() { std::unique_lock<std::mutex> lk(ioM_); ioIdle_.wait(lk, [&] { return ioQ_.empty() && !ioBusy_; }); }

bool DesktopApi::pathAllowed(const std::string& p) const {
  if (p.empty()) return false;
  const std::string k = pathKey(p);
  std::lock_guard<std::mutex> g(allowM_);
  for (auto& a : allowed_) if (a.first == k) return true;
  for (auto& d : cfg_.allowDirs) { std::string dk = pathKey(d); if (!dk.empty() && dk.back() != '/') dk += '/'; if (k.size() > dk.size() && k.compare(0, dk.size(), dk) == 0) return true; }
  return false;
}
void DesktopApi::allowPath(const std::string& p) {
  if (p.empty() || p.find('\n') != std::string::npos || p.find('\r') != std::string::npos) return;
  const std::string k = pathKey(p);
  { std::lock_guard<std::mutex> g(allowM_);
    for (size_t i = 0; i < allowed_.size(); i++) if (allowed_[i].first == k) { allowed_.erase(allowed_.begin() + (long)i); break; }
    allowed_.push_back({k, p}); while (allowed_.size() > kAllowMax) allowed_.erase(allowed_.begin()); }
  saveAllowed();
}
void DesktopApi::saveAllowed() {
  if (cfg_.allowFile.empty()) return;
  std::string all; { std::lock_guard<std::mutex> g(allowM_); for (auto& a : allowed_) all += a.second + "\n"; }
  const std::string file = cfg_.allowFile;
  runIo([file, all] { try { const fs::path f = u8(file); std::error_code ec; fs::create_directories(f.parent_path(), ec); std::ofstream o(f, std::ios::binary | std::ios::trunc); o.write(all.data(), (std::streamsize)all.size()); } catch (...) {} });
}

void DesktopApi::reply(const std::string& id, const std::string& v) { if (id.empty()) return; emit_("{\"t\":\"dreply\",\"id\":" + id + ",\"v\":" + v + "}"); }
void DesktopApi::push(const char* ch, const std::string& v) { emit_(std::string("{\"t\":\"dpush\",\"ch\":\"") + ch + "\",\"v\":" + (v.empty() ? "null" : v) + "}"); }

std::string DesktopApi::initJson() const {
  return "{\"version\":" + jsonQuote(cfg_.version) + ",\"link\":{\"available\":false,\"why\":\"Ableton Link is not in the native desktop shell yet (H8)\"},"
         "\"io\":{\"available\":false}}";
}

bool DesktopApi::handle(const std::string& json) {
  try { return handleImpl(json); }
  catch (...) {   // R-2: nothing escapes to the shell's callback; the page's call still gets an answer
    try { thunder::JsonDoc d; if (d.parse(json) && d.root().isObj()) { thunder::Json idj = d.root().get("id"); if (idj.isNum()) reply(std::to_string((long long)idj.asNum()), err("the shell could not handle that call")); } } catch (...) {}
    return false;
  }
}

bool DesktopApi::handleImpl(const std::string& json) {
  thunder::JsonDoc d;
  if (!d.parse(json) || !d.root().isObj()) return false;
  const thunder::Json j = d.root();
  const std::string ch(j.str("ch"));
  thunder::Json idj = j.get("id");
  const std::string id = idj.isNum() ? std::to_string((long long)idj.asNum()) : "";
  const thunder::Json a = j.arr("a");
  const Channel* known = nullptr;
  for (int i = 0; i < kChannelCount; i++) if (ch == kChannels[i].name) known = &kChannels[i];
  if (!known || (known->kind != Channel::Handle && known->kind != Channel::Send)) { if (!id.empty()) emit_("{\"t\":\"dreply\",\"id\":" + id + ",\"err\":" + jsonQuote("unknown channel " + ch) + "}"); return false; }
  auto done = [&](const std::string& v) { if (!id.empty()) reply(id, v); return true; };

  if (known->kind == Channel::Send) {
    if (ch == "thunder:page-ready") { pageReady_ = true; if (!pendingPath_.empty()) { const std::string p = pendingPath_; pendingPath_.clear(); push("thunder:open-path", jsonQuote(p)); } }
    else if (ch == "thunder:close-ack") closeDeadline_ = 0;
    else if (ch == "thunder:close-reply") {
      closeDeadline_ = 0;
      if (!(a.at(0).isBool() && a.at(0).asBool())) { quitting_ = false; return true; }
      closeOk_ = true; const bool q = quitting_; plat_->closeWindow(q);
    }
    else if (ch == "thunder:playing") plat_->setPlaying(a.at(0).isBool() && a.at(0).asBool());
    else if (ch == "thunder:reload") plat_->reloadPage();
    return true;
  }
  // ---- handle ----
  const std::string notPicked = "not allowed: the shell only reads and writes files you chose in a dialog (Open, Save as) or opened with the app";
  auto saved = [this](const std::string& r) { if (r.compare(0, 6, "{\"ok\":") == 0 && cfg_.onSaved) cfg_.onSaved(); };
  if (ch == "thunder:file-open") { const std::string p = plat_->pickOpenFile(""); if (p.empty()) return done("null"); allowPath(p); runIo([this, id, p] { reply(id, readProj(p)); }); return true; }
  if (ch == "thunder:file-read") { const std::string p = argStr(a, 0); if (!pathAllowed(p)) return done(err(notPicked)); runIo([this, id, p] { reply(id, readProj(p)); }); return true; }
  if (ch == "thunder:file-save") {
    const std::string p = argStr(a, 0); thunder::Json t = a.at(1);
    if (!t.isStr()) return done(err("not a project file"));
    if (!pathAllowed(p)) return done(err(notPicked));
    runIo([this, id, p, text = std::string(t.asStr()), saved] { const std::string r = writeProj(p, text); saved(r); reply(id, r); });
    return true;
  }
  if (ch == "thunder:file-saveas") {
    std::string base = sanitizeName(a.at(0).isStr() && !a.at(0).asStr().empty() ? std::string(a.at(0).asStr()) : "Song.thunder");
    std::string dir = parentDirIfProj(argStr(a, 2)); if (dir.empty()) dir = plat_->knownDir("documents");
    std::string p = plat_->pickSaveFile(dir, base, "Thunder Plus project", "thunder");
    if (p.empty()) return done("null");
    if (!isProj(p)) p += ".thunder";
    thunder::Json t = a.at(1);
    if (!t.isStr()) return done(err("not a project file"));
    allowPath(p);   // the person chose it in the Save dialog
    runIo([this, id, p, text = std::string(t.asStr()), saved] { const std::string r = writeProj(p, text); saved(r); reply(id, r); });
    return true;
  }
  if (ch == "thunder:out-pick") {
    const std::string base = sanitizeName(a.at(0).isStr() && !a.at(0).asStr().empty() ? std::string(a.at(0).asStr()) : "Render.wav");
    std::string ext = lowerExt(base); if (ext.empty()) ext = "wav";
    if (ext != "wav" && ext != "zip" && ext != "mid") return done("null");
    std::error_code ec; std::string dir;
    if (!lastOut_.empty() && fs::exists(u8(lastOut_), ec)) dir = lastOut_; else { dir = parentDirIfProj(argStr(a, 1)); if (dir.empty()) dir = plat_->knownDir("music"); }
    std::string p = plat_->pickSaveFile(dir, base, ext == "wav" ? "WAV audio" : ext == "zip" ? "Zip archive" : "MIDI file", ext);
    if (p.empty()) return done("null");
    if (lowerExt(p) != ext) p += "." + ext;
    lastOut_ = u8(p).parent_path().u8string();
    allowPath(p);
    return done(jsonQuote(p));
  }
  if (ch == "thunder:out-write") {
    thunder::Json data = a.at(1);   // {"b64": "..."}: the page's ArrayBuffer as base64
    const std::string p = argStr(a, 0);
    if (!data.isObj()) return done(err("no data"));
    if (!pathAllowed(p)) return done(err(notPicked));
    const std::string_view b64v = data.str("b64");
    if (b64v.size() / 4 * 3 > kMaxOutBytes + 3) return done(err("that file is too large (over 512 MiB)"));
    runIo([this, id, p, b64 = std::string(b64v)] { std::string bytes; if (!base64Decode(b64, &bytes)) { reply(id, err("no data")); return; } reply(id, writeOut(p, bytes)); });
    return true;
  }
  if (ch == "thunder:out-show") { const std::string p = argStr(a, 0); std::error_code ec; if (!p.empty() && fs::exists(u8(p), ec)) plat_->showInFolder(p); return done("true"); }
  if (ch == "thunder:link-enable" || ch == "thunder:link-sync" || ch == "thunder:link-tempo" || ch == "thunder:link-stop") return done("false");   // NOT YET: Link off
  if (ch == "thunder:link-start" || ch == "thunder:link-now") return done("0");
  if (ch == "thunder:io-start") return done("{\"ok\":false,\"why\":\"the Electron I/O helper is replaced by thunderCore in the native shell (audio and MIDI devices: H7)\"}");
  if (ch == "thunder:io-mic") return done("{\"ok\":false,\"why\":\"audio input is H7 (not in the H6 shell skeleton)\"}");
  if (ch == "thunder:natural-scroll") return done(plat_->naturalScroll() ? "true" : "false");
  // H8: the updater is check-only (update_check.h): state / check / act answer for real; the page's Update button opens the release page
  if (ch == "thunder:upd-state") return done(upd_->stateJson());
  if (ch == "thunder:upd-check") { if (!plat_->canHttp()) return done("false"); upd_->check(true, [this, id](bool ok) { reply(id, ok ? "true" : "false"); }); return true; }
  if (ch == "thunder:upd-act") { upd_->act(); return done("true"); }
  return done("null");
}

bool DesktopApi::requestClose(bool quitting) {
  if (closeOk_ || !pageReady_) { closeOk_ = false; return true; }
  quitting_ = quitting;
  push("thunder:close-ask", "");
  closeDeadline_ = cfg_.nowMs() + cfg_.closeTimeoutMs;
  return false;
}

void DesktopApi::tick() {
  if (cfg_.autoUpdateCheck && plat_->canHttp()) {   // H8: Electron checks 8 s after it opens and every 4 hours
    const int64_t now = cfg_.nowMs();
    if (!nextAuto_) nextAuto_ = now + 8000;
    else if (now >= nextAuto_) { nextAuto_ = now + 4LL * 3600 * 1000; upd_->check(false, nullptr); }
  }
  if (closeDeadline_ && cfg_.nowMs() >= closeDeadline_) { closeDeadline_ = 0; closeOk_ = true; plat_->closeWindow(quitting_); }   // a page that does not answer in 3 s does not stop the window closing
}

void DesktopApi::onLoadStart() { pageReady_ = false; closeOk_ = false; closeDeadline_ = 0; }

void DesktopApi::openPath(const std::string& path) { allowPath(path); if (pageReady_) push("thunder:open-path", jsonQuote(path)); else pendingPath_ = path; }

}  // namespace tpd
