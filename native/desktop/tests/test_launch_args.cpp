// H8: file hand-offs (launch_args.h), the Windows file-association entries (file_assoc.h).
#include <filesystem>
#include <fstream>
#include "check.h"
#include "file_assoc.h"
#include "launch_args.h"
namespace fs = std::filesystem;
using namespace tpd;
static void touch(const fs::path& p, const char* t = "{}") { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
int main() {
  const fs::path dir = fs::temp_directory_path() / "tpd_launch_args_test"; fs::remove_all(dir); fs::create_directories(dir);
  touch(dir / "a b.thunder"); touch(dir / "UP.THUNDER"); touch(dir / "n.json"); touch(dir / "x.wav"); fs::create_directories(dir / "dir.thunder");
  const std::string ab = (dir / "a b.thunder").u8string();
  // argv: the first existing .thunder (Electron's argPath)
  CHECK(projectFromArgs({"--selftest", ab}) == ab, "an existing .thunder argument is found");
  CHECK(projectFromArgs({(dir / "missing.thunder").u8string(), ab}) == ab, "a missing file is skipped, the next one is taken");
  CHECK(projectFromArgs({(dir / "n.json").u8string()}).empty(), "a .json argument is not a launch file (Electron: .thunder only)");
  CHECK(projectFromArgs({(dir / "x.wav").u8string()}).empty() && projectFromArgs({}).empty(), "other files and no arguments: none");
  CHECK(!projectFromArgs({(dir / "UP.THUNDER").u8string()}).empty(), "the extension is matched in any case");
  CHECK(projectFromArgs({(dir / "dir.thunder").u8string()}).empty(), "a folder named .thunder is not a file");
  CHECK(projectFromArgs({"a b.thunder"}, dir.u8string()) == (dir / "a b.thunder").lexically_normal().u8string(), "a relative path is resolved against the given folder");
  CHECK(projectFromArgs({"-evil.thunder"}).empty(), "an option-looking argument is skipped");
  // OS open events
  CHECK(openablePath(ab) == ab && !openablePath((dir / "n.json").u8string()).empty(), ".thunder and .json files open");
  CHECK(openablePath((dir / "x.wav").u8string()).empty() && openablePath("").empty() && openablePath((dir / "nope.thunder").u8string()).empty(), "other types, empty and missing paths do not");
  CHECK(openablePath(ab + "\n").empty() && openablePath(std::string("a.thunder\0b", 11)).empty(), "a newline or a NUL in a path is refused (the picked-paths list is line based)");
  // the second-instance hand-off
  { std::string p; const std::string m = handoffEncode(ab);
    CHECK(handoffDecode(m.data(), m.size(), &p) && p == ab, "a hand-off with a path decodes to it");
    const std::string m0 = handoffEncode(""); CHECK(handoffDecode(m0.data(), m0.size(), &p) && p.empty(), "a hand-off without a path means: bring the window forward");
    std::string withNul = m; withNul.push_back('\0'); CHECK(handoffDecode(withNul.data(), withNul.size(), &p) && p == ab, "a trailing terminator is tolerated");
    CHECK(!handoffDecode("hello", 5, &p) && !handoffDecode("TPD2\nx", 6, &p) && !handoffDecode(nullptr, 0, &p), "foreign data is not ours");
    const std::string bad = handoffEncode((dir / "x.wav").u8string()); CHECK(!handoffDecode(bad.data(), bad.size(), &p), "a hand-off naming a non-project file is refused");
    const std::string big = "TPD1\n" + std::string(kHandoffMax, 'a'); CHECK(!handoffDecode(big.data(), big.size(), &p), "an oversized hand-off is refused");
    const std::string inner = std::string("TPD1\n") + "a\0b.thunder"; CHECK(!handoffDecode(inner.data(), 5 + 12, &p), "a NUL inside the path is refused"); }
  // file association: per-user keys, the open command quotes the exe and %1
  { const auto e = fileAssocEntries("C:\\Program Files\\Thunder Plus\\Thunder Plus.exe");
    bool ext = false, cmd = false, icon = false, mime = false;
    for (auto& v : e) {
      if (v.key == "Software\\Classes\\.thunder" && v.name.empty() && v.value == kProgId) ext = true;
      if (v.key == "Software\\Classes\\.thunder" && v.name == "Content Type" && v.value == "application/x-thunder-plus") mime = true;
      if (v.key == std::string("Software\\Classes\\") + kProgId + "\\shell\\open\\command" && v.value == "\"C:\\Program Files\\Thunder Plus\\Thunder Plus.exe\" \"%1\"") cmd = true;
      if (v.key.size() > 12 && v.key.find("DefaultIcon") != std::string::npos && v.value == "\"C:\\Program Files\\Thunder Plus\\Thunder Plus.exe\",0") icon = true;
    }
    CHECK(ext && mime && cmd && icon, "extension, content type, open command and icon entries (%d %d %d %d)", ext, mime, cmd, icon);
    CHECK(fileAssocEntries("").empty() && fileAssocEntries("C:\\a\"b.exe").empty(), "no exe path, or a quote in it: no entries");
    const auto rm = fileAssocKeysToRemove(); CHECK(rm.size() == 7 && rm.back() == "Software\\Classes\\.thunder" && rm[0].find("command") != std::string::npos, "keys to remove: children before parents"); }
  fs::remove_all(dir);
  DONE("test_launch_args");
}
