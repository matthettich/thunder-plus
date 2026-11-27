#include "launch_args.h"

#include <cctype>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace tpd {
namespace {
std::string lowerExt(const std::string& p) { const size_t d = p.find_last_of('.'); std::string e = d == std::string::npos ? "" : p.substr(d + 1); for (auto& c : e) c = (char)std::tolower((unsigned char)c); return e; }
std::string absolutePath(const std::string& p, const std::string& cwd) {
  std::error_code ec;
  fs::path f = fs::u8path(p);
  if (f.is_relative()) f = (cwd.empty() ? fs::current_path(ec) : fs::u8path(cwd)) / f;
  return f.lexically_normal().u8string();
}
bool isFile(const std::string& p) { std::error_code ec; return fs::is_regular_file(fs::u8path(p), ec); }
}  // namespace

std::string projectFromArgs(const std::vector<std::string>& args, const std::string& cwd) {
  for (const std::string& a : args) {
    if (a.empty() || a[0] == '-' || lowerExt(a) != "thunder") continue;
    try { const std::string p = absolutePath(a, cwd); if (isFile(p)) return p; } catch (...) {}
  }
  return "";
}

std::string openablePath(const std::string& path, const std::string& cwd) {
  if (path.empty() || path.find('\0') != std::string::npos || path.find('\n') != std::string::npos || path.find('\r') != std::string::npos) return "";
  const std::string e = lowerExt(path);
  if (e != "thunder" && e != "json") return "";
  try { const std::string p = absolutePath(path, cwd); return isFile(p) ? p : ""; } catch (...) { return ""; }
}

std::string handoffEncode(const std::string& path) { return "TPD1\n" + path; }

bool handoffDecode(const char* data, size_t size, std::string* path) {
  if (!data || size < 5 || size > kHandoffMax || path == nullptr) return false;
  if (std::string(data, 5) != "TPD1\n") return false;
  std::string p(data + 5, size - 5);
  while (!p.empty() && p.back() == '\0') p.pop_back();   // WM_COPYDATA from a C string may carry the terminator
  if (p.find('\0') != std::string::npos) return false;
  if (p.empty()) { path->clear(); return true; }
  const std::string ok = openablePath(p);
  if (ok.empty()) return false;
  *path = ok; return true;
}

}  // namespace tpd
