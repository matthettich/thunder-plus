#include "desktop_api.h"

#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
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
std::string argStr(thunder::Json a, size_t i) { thunder::Json v = a.at(i); return v.isStr() ? std::string(v.asStr()) : std::string(); }
}  // namespace

bool DesktopApi::isProj(const std::string& p) { const std::string e = lowerExt(p); return e == "thunder" || e == "json"; }

std::string DesktopApi::readProj(const std::string& p) {
  if (!isProj(p)) return err("that isn\xe2\x80\x99t a .thunder or .json file");
  std::ifstream in(u8(p), std::ios::binary);
  if (!in) { std::error_code ec; return err(fs::exists(u8(p), ec) ? "could not read it" : "it isn\xe2\x80\x99t there any more"); }
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return "{\"path\":" + jsonQuote(p) + ",\"name\":" + jsonQuote(u8(p).filename().u8string()) + ",\"text\":" + jsonQuote(text) + "}";
}

// Written next to the file first, then renamed over it, so a crash mid-save never leaves half a project.
std::string DesktopApi::writeProj(const std::string& p, const std::string& text) {
  if (!isProj(p)) return err("not a project file");
  const std::string tmp = p + ".saving";
  std::error_code ec;
  { std::ofstream o(u8(tmp), std::ios::binary | std::ios::trunc); if (!o) return err("could not write next to the file"); o.write(text.data(), (std::streamsize)text.size()); o.flush(); if (!o) { o.close(); fs::remove(u8(tmp), ec); return err("write failed"); } }
  fs::rename(u8(tmp), u8(p), ec);
  if (ec) { std::error_code e2; fs::remove(u8(tmp), e2); return err(ec.message()); }
  return "{\"ok\":true,\"path\":" + jsonQuote(p) + "}";
}

std::string DesktopApi::writeOut(const std::string& p, const std::string& bytes) {
  const std::string e = lowerExt(p);
  if (e != "wav" && e != "zip" && e != "mid") return err("not a render file");
  std::ofstream o(u8(p), std::ios::binary | std::ios::trunc);
  if (!o) return err("could not write the file");
  o.write(bytes.data(), (std::streamsize)bytes.size()); o.flush();
  if (!o) return err("write failed");
  return "{\"ok\":true,\"path\":" + jsonQuote(p) + "}";
}

DesktopApi::DesktopApi(IPlatform* plat, Emit emit, Config cfg) : plat_(plat), emit_(std::move(emit)), cfg_(std::move(cfg)) {
  if (!cfg_.nowMs) cfg_.nowMs = [] { return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
}

void DesktopApi::reply(const std::string& id, const std::string& v) { emit_("{\"t\":\"dreply\",\"id\":" + id + ",\"v\":" + v + "}"); }
void DesktopApi::push(const char* ch, const std::string& v) { emit_(std::string("{\"t\":\"dpush\",\"ch\":\"") + ch + "\",\"v\":" + (v.empty() ? "null" : v) + "}"); }

std::string DesktopApi::initJson() const {
  return "{\"version\":" + jsonQuote(cfg_.version) + ",\"link\":{\"available\":false,\"why\":\"Ableton Link is not in the native desktop shell yet (H8)\"},"
         "\"io\":{\"available\":false}}";
}

bool DesktopApi::handle(const std::string& json) {
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
  if (ch == "thunder:file-open") { const std::string p = plat_->pickOpenFile(""); return done(p.empty() ? "null" : readProj(p)); }
  if (ch == "thunder:file-read") return done(readProj(argStr(a, 0)));
  if (ch == "thunder:file-save") { thunder::Json t = a.at(1); return done(t.isStr() ? writeProj(argStr(a, 0), std::string(t.asStr())) : err("not a project file")); }
  if (ch == "thunder:file-saveas") {
    std::string base = sanitizeName(a.at(0).isStr() && !a.at(0).asStr().empty() ? std::string(a.at(0).asStr()) : "Song.thunder");
    std::string dir = parentDirIfProj(argStr(a, 2)); if (dir.empty()) dir = plat_->knownDir("documents");
    std::string p = plat_->pickSaveFile(dir, base, "Thunder Plus project", "thunder");
    if (p.empty()) return done("null");
    if (!isProj(p)) p += ".thunder";
    thunder::Json t = a.at(1);
    return done(t.isStr() ? writeProj(p, std::string(t.asStr())) : err("not a project file"));
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
    return done(jsonQuote(p));
  }
  if (ch == "thunder:out-write") {
    thunder::Json data = a.at(1);   // {"b64": "..."}: the page's ArrayBuffer as base64
    std::string bytes;
    if (!data.isObj() || !base64Decode(std::string(data.str("b64")), &bytes)) return done(err("no data"));
    return done(writeOut(argStr(a, 0), bytes));
  }
  if (ch == "thunder:out-show") { const std::string p = argStr(a, 0); std::error_code ec; if (!p.empty() && fs::exists(u8(p), ec)) plat_->showInFolder(p); return done("true"); }
  if (ch == "thunder:link-enable" || ch == "thunder:link-sync" || ch == "thunder:link-tempo" || ch == "thunder:link-stop") return done("false");   // NOT YET (H8): Link off
  if (ch == "thunder:link-start" || ch == "thunder:link-now") return done("0");
  if (ch == "thunder:io-start") return done("{\"ok\":false,\"why\":\"the Electron I/O helper is replaced by thunderCore in the native shell (audio and MIDI devices: H7)\"}");
  if (ch == "thunder:io-mic") return done("{\"ok\":false,\"why\":\"audio input is H7 (not in the H6 shell skeleton)\"}");
  if (ch == "thunder:natural-scroll") return done(plat_->naturalScroll() ? "true" : "false");
  if (ch == "thunder:upd-state") return done("{\"version\":" + jsonQuote(cfg_.version) + ",\"avail\":null,\"ready\":false,\"notifyOnly\":true,\"dl\":null,\"file\":null}");
  if (ch == "thunder:upd-check") return done("false");   // NOT YET (H8): the updater
  if (ch == "thunder:upd-act") return done("null");
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
  if (closeDeadline_ && cfg_.nowMs() >= closeDeadline_) { closeDeadline_ = 0; closeOk_ = true; plat_->closeWindow(quitting_); }   // a page that does not answer in 3 s does not stop the window closing
}

void DesktopApi::onLoadStart() { pageReady_ = false; closeOk_ = false; closeDeadline_ = 0; }

void DesktopApi::openPath(const std::string& path) { if (pageReady_) push("thunder:open-path", jsonQuote(path)); else pendingPath_ = path; }

}  // namespace tpd
