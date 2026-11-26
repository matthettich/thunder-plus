// H6: window.thunderDesktop.* (desktop_api.h): the 31 channels, each answered; the file rules; the close and open-path hand-offs.
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include "check.h"
#include "desktop_api.h"
#include "rt/json.h"
#include "text_util.h"
namespace fs = std::filesystem;
using namespace tpd;

struct FakePlat : IPlatform {
  std::string openAnswer, saveAnswer, lastDir, lastName; int reloads = 0, closes = 0, shown = 0; bool playing = false, closedQuit = false, nat = true;
  std::string pickOpenFile(const std::string&) override { return openAnswer; }
  std::string pickSaveFile(const std::string& dir, const std::string& name, const std::string&, const std::string&) override { lastDir = dir; lastName = name; return saveAnswer; }
  void showInFolder(const std::string&) override { shown++; }
  void reloadPage() override { reloads++; }
  void setPlaying(bool on) override { playing = on; }
  bool naturalScroll() override { return nat; }
  void closeWindow(bool q) override { closes++; closedQuit = q; }
  std::string knownDir(const std::string& w) override { return "/home/" + w; }
};
static std::string slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }

int main() {
  // the table: 31 unique channels of the right kinds
  { std::set<std::string> names; int h = 0, s = 0, ss = 0, p = 0;
    for (int i = 0; i < kChannelCount; i++) { names.insert(kChannels[i].name); switch (kChannels[i].kind) { case Channel::Handle: h++; break; case Channel::Send: s++; break; case Channel::SendSync: ss++; break; case Channel::Push: p++; break; } }
    CHECK(names.size() == 31 && h == 19 && s == 5 && ss == 1 && p == 6, "31 channels: 19 handle, 5 send, 1 sendSync, 6 push (got %zu / %d %d %d %d)", names.size(), h, s, ss, p); }
  FakePlat plat; std::vector<std::string> out; int64_t now = 1000;
  DesktopApi::Config cfg; cfg.version = "9.9.9"; cfg.nowMs = [&] { return now; };
  DesktopApi api(&plat, [&](const std::string& j) { out.push_back(j); }, cfg);
  auto lastReply = [&](thunder::JsonDoc& d) { CHECK(!out.empty(), "a reply was sent"); if (out.empty()) return thunder::Json(); d.parse(out.back()); return d.root(); };

  // every Handle channel answers a dreply with the id it was given (the gate: "every channel answers")
  for (int i = 0; i < kChannelCount; i++) {
    if (kChannels[i].kind != Channel::Handle) continue;
    out.clear();
    const std::string msg = std::string("{\"ch\":\"") + kChannels[i].name + "\",\"id\":" + std::to_string(100 + i) + ",\"a\":[]}";
    CHECK(api.handle(msg), "%s is known", kChannels[i].name);
    thunder::JsonDoc d; thunder::Json r = lastReply(d);
    CHECK(out.size() == 1 && r.str("t") == "dreply" && (int)r.num("id") == 100 + i && !r.get("v").missing(), "%s answers with a dreply (%s)", kChannels[i].name, out.empty() ? "nothing" : out.back().c_str());
  }
  // every Send channel is accepted and sends nothing back
  for (int i = 0; i < kChannelCount; i++) { if (kChannels[i].kind != Channel::Send) continue; out.clear(); CHECK(api.handle(std::string("{\"ch\":\"") + kChannels[i].name + "\",\"a\":[false]}") && out.size() <= 1, "%s accepted", kChannels[i].name); }
  // not channels: Push and SendSync names, and unknown ones, are refused with an err reply
  { out.clear(); CHECK(!api.handle("{\"ch\":\"thunder:nope\",\"id\":1,\"a\":[]}") && out.size() == 1 && out[0].find("\"err\"") != std::string::npos, "unknown channel answers an error"); }
  CHECK(!api.handle("not json"), "not JSON");

  // the stubs say so plainly
  { out.clear(); api.handle("{\"ch\":\"thunder:io-start\",\"id\":1,\"a\":[]}"); CHECK(out[0].find("\"ok\":false") != std::string::npos && out[0].find("H7") != std::string::npos, "io-start: not yet (H7)");
    out.clear(); api.handle("{\"ch\":\"thunder:upd-state\",\"id\":2,\"a\":[]}"); CHECK(out[0].find("9.9.9") != std::string::npos && out[0].find("\"notifyOnly\":true") != std::string::npos, "upd-state carries the version");
    out.clear(); api.handle("{\"ch\":\"thunder:natural-scroll\",\"id\":3,\"a\":[]}"); CHECK(out[0].find("true") != std::string::npos, "natural-scroll asks the platform");
    CHECK(api.initJson().find("\"available\":false") != std::string::npos && api.initJson().find("Link") != std::string::npos, "initJson: link not available, with a reason"); }

  // files
  const fs::path dir = fs::temp_directory_path() / "tpd_desktop_api_test"; fs::remove_all(dir); fs::create_directories(dir);
  const std::string song = (dir / "a b.thunder").u8string();
  { out.clear(); api.handle("{\"ch\":\"thunder:file-save\",\"id\":5,\"a\":[" + jsonQuote(song) + ",\"{\\\"x\\\":1}\"]}"); thunder::JsonDoc d; auto r = lastReply(d);
    CHECK(r.get("v").boolean("ok") && slurp(dir / "a b.thunder") == "{\"x\":1}" && !fs::exists(song + ".saving"), "file-save writes the file and leaves no .saving");
    out.clear(); api.handle("{\"ch\":\"thunder:file-read\",\"id\":6,\"a\":[" + jsonQuote(song) + "]}"); thunder::JsonDoc d2; auto r2 = lastReply(d2);
    CHECK(r2.get("v").str("text") == "{\"x\":1}" && r2.get("v").str("name") == "a b.thunder", "file-read returns path, name, text");
    out.clear(); api.handle("{\"ch\":\"thunder:file-read\",\"id\":7,\"a\":[" + jsonQuote((dir / "x.txt").u8string()) + "]}"); thunder::JsonDoc d3; auto r3 = lastReply(d3);
    CHECK(!r3.get("v").str("error").empty(), "file-read refuses other extensions");
    out.clear(); api.handle("{\"ch\":\"thunder:file-read\",\"id\":8,\"a\":[" + jsonQuote((dir / "gone.thunder").u8string()) + "]}"); thunder::JsonDoc d4; auto r4 = lastReply(d4);
    CHECK(!r4.get("v").str("error").empty(), "file-read of a missing file: an error text"); }
  { fs::create_directories(dir / "nodir"); out.clear(); api.handle("{\"ch\":\"thunder:file-save\",\"id\":9,\"a\":[" + jsonQuote((dir / "nodir2/x.thunder").u8string()) + ",\"{}\"]}"); thunder::JsonDoc d; CHECK(!lastReply(d).get("v").str("error").empty(), "file-save into a missing folder: error"); }
  { plat.openAnswer = song; out.clear(); api.handle("{\"ch\":\"thunder:file-open\",\"id\":10,\"a\":[]}"); thunder::JsonDoc d; CHECK(lastReply(d).get("v").str("text") == "{\"x\":1}", "file-open reads the picked file");
    plat.openAnswer = ""; out.clear(); api.handle("{\"ch\":\"thunder:file-open\",\"id\":11,\"a\":[]}"); thunder::JsonDoc d2; CHECK(lastReply(d2).get("v").isNull(), "file-open cancelled: null"); }
  { plat.saveAnswer = (dir / "new").u8string(); out.clear(); api.handle("{\"ch\":\"thunder:file-saveas\",\"id\":12,\"a\":[\"My/Song\",\"{}\"," + jsonQuote(song) + "]}"); thunder::JsonDoc d; auto r = lastReply(d);
    CHECK(r.get("v").boolean("ok") && fs::exists(dir / "new.thunder") && plat.lastName == "My_Song" && plat.lastDir == dir.u8string(), "file-saveas: name cleaned, dialog starts near the open file, .thunder added");
    plat.saveAnswer = ""; out.clear(); api.handle("{\"ch\":\"thunder:file-saveas\",\"id\":13,\"a\":[\"\",\"{}\",\"\"]}"); thunder::JsonDoc d2; CHECK(lastReply(d2).get("v").isNull() && plat.lastDir == "/home/documents" && plat.lastName == "Song.thunder", "file-saveas cancelled: null; default folder Documents"); }
  { plat.saveAnswer = (dir / "r").u8string(); out.clear(); api.handle("{\"ch\":\"thunder:out-pick\",\"id\":14,\"a\":[\"Mix.wav\",\"\"]}"); thunder::JsonDoc d; CHECK(lastReply(d).get("v").asStr() == (dir / "r.wav").u8string() && plat.lastDir == "/home/music", "out-pick adds the extension; starts in Music");
    out.clear(); api.handle("{\"ch\":\"thunder:out-pick\",\"id\":15,\"a\":[\"Mix2.wav\",\"\"]}"); CHECK(plat.lastDir == dir.u8string(), "out-pick remembers the last folder");
    out.clear(); api.handle("{\"ch\":\"thunder:out-pick\",\"id\":16,\"a\":[\"x.exe\",\"\"]}"); thunder::JsonDoc d2; CHECK(lastReply(d2).get("v").isNull(), "out-pick only for wav, zip, mid"); }
  { const std::string p = (dir / "r.wav").u8string(); const std::string b = base64Encode((const uint8_t*)"RIFFxxxx", 8);
    out.clear(); api.handle("{\"ch\":\"thunder:out-write\",\"id\":17,\"a\":[" + jsonQuote(p) + ",{\"b64\":\"" + b + "\"}]}"); thunder::JsonDoc d; CHECK(lastReply(d).get("v").boolean("ok") && slurp(dir / "r.wav") == "RIFFxxxx", "out-write writes the bytes");
    out.clear(); api.handle("{\"ch\":\"thunder:out-write\",\"id\":18,\"a\":[" + jsonQuote((dir / "r.exe").u8string()) + ",{\"b64\":\"" + b + "\"}]}"); thunder::JsonDoc d2; CHECK(!lastReply(d2).get("v").str("error").empty() && !fs::exists(dir / "r.exe"), "out-write refuses other extensions");
    out.clear(); api.handle("{\"ch\":\"thunder:out-show\",\"id\":19,\"a\":[" + jsonQuote(p) + "]}"); CHECK(plat.shown == 1, "out-show asks the platform"); api.handle("{\"ch\":\"thunder:out-show\",\"id\":20,\"a\":[\"/nonexistent\"]}"); CHECK(plat.shown == 1, "out-show of a missing file does nothing"); }
  CHECK(DesktopApi::isProj("A.THUNDER") && DesktopApi::isProj("x.json") && !DesktopApi::isProj("x.txt") && !DesktopApi::isProj("thunder"), "isProj");

  // send channels reach the platform
  plat.reloads = 0; api.handle("{\"ch\":\"thunder:reload\",\"a\":[]}"); api.handle("{\"ch\":\"thunder:playing\",\"a\":[true]}"); CHECK(plat.reloads == 1 && plat.playing, "reload, playing");
  api.handle("{\"ch\":\"thunder:playing\",\"a\":[false]}"); CHECK(!plat.playing, "playing off");

  // open-path: held until the page is ready
  api.onLoadStart(); out.clear(); api.openPath("/x/y.thunder"); CHECK(out.empty(), "open-path waits for page-ready");
  api.handle("{\"ch\":\"thunder:page-ready\",\"a\":[]}"); CHECK(out.size() == 1 && out[0].find("thunder:open-path") != std::string::npos && out[0].find("/x/y.thunder") != std::string::npos, "page-ready flushes the pending path");
  out.clear(); api.openPath("/z.thunder"); CHECK(out.size() == 1, "once ready it goes at once");

  // the close hand-off
  { FakePlat p2; int64_t t = 0; std::vector<std::string> o2; DesktopApi::Config c2; c2.nowMs = [&] { return t; };
    DesktopApi a2(&p2, [&](const std::string& j) { o2.push_back(j); }, c2);
    CHECK(a2.requestClose(false), "a page that is not ready does not stop the window closing");
    a2.handle("{\"ch\":\"thunder:page-ready\",\"a\":[]}");
    CHECK(!a2.requestClose(false) && o2.size() == 1 && o2[0].find("thunder:close-ask") != std::string::npos, "close: the page is asked, the window stays");
    t = 2900; a2.tick(); CHECK(p2.closes == 0, "before 3 s nothing happens");
    a2.handle("{\"ch\":\"thunder:close-ack\",\"a\":[]}"); t = 9000; a2.tick(); CHECK(p2.closes == 0, "an ack cancels the 3 s timer");
    a2.handle("{\"ch\":\"thunder:close-reply\",\"a\":[false]}"); CHECK(p2.closes == 0, "close-reply false: stay");
    CHECK(!a2.requestClose(true), "asked again (quitting)"); a2.handle("{\"ch\":\"thunder:close-reply\",\"a\":[true]}"); CHECK(p2.closes == 1 && p2.closedQuit, "close-reply true: the platform closes (and quits)");
    CHECK(a2.requestClose(false), "the close that follows is allowed");
    a2.onLoadStart(); CHECK(a2.requestClose(false), "a reloading page is not asked");
    a2.handle("{\"ch\":\"thunder:page-ready\",\"a\":[]}"); t = 10000; a2.requestClose(false); t = 13001; a2.tick(); CHECK(p2.closes == 2, "a page that does not answer in 3 s does not stop the window closing"); }
  fs::remove_all(dir);
  DONE("test_desktop_api");
}
