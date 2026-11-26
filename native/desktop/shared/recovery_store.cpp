#include "recovery_store.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

#include "rt/json.h"
#include "text_util.h"

namespace fs = std::filesystem;

namespace tpd {

namespace {
const char* kText = "recovery.put.thunder";
const char* kSide = "recovery.put.json";
bool writeFile(const fs::path& p, const std::string& s) {
  std::ofstream o(p, std::ios::binary | std::ios::trunc);
  if (!o) return false;
  o.write(s.data(), (std::streamsize)s.size()); o.flush();
  return (bool)o;
}
// the time from the sidecar; -1 when it is not a complete copy
int64_t readMeta(const fs::path& dir) {
  std::error_code ec;
  if (!fs::is_regular_file(dir / kText, ec) || !fs::is_regular_file(dir / kSide, ec)) return -1;
  std::ifstream in(dir / kSide, std::ios::binary);
  if (!in) return -1;
  std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  thunder::JsonDoc d;
  if (!d.parse(s) || !d.root().isObj()) return -1;
  return (int64_t)d.root().num("time", 0);
}
}  // namespace

bool RecoveryStore::put(const std::string& text, int64_t nowMs) {
  if (!enabled() || text.empty()) return false;
  std::lock_guard<std::mutex> g(m_);
  try {
    const fs::path dir = fs::u8path(dir_);
    std::error_code ec; fs::create_directories(dir, ec);
    if (!fs::is_directory(dir, ec)) return false;
    const fs::path t = dir / "recovery.put.thunder.tmp", m = dir / "recovery.put.json.tmp";
    if (!writeFile(t, text) || !writeFile(m, "{\"title\":\"\",\"time\":" + std::to_string((long long)nowMs) + "}")) { fs::remove(t, ec); fs::remove(m, ec); return false; }
    fs::rename(t, dir / kText, ec);
    if (ec) { fs::remove(t, ec); fs::remove(m, ec); return false; }
    fs::rename(m, dir / kSide, ec);
    if (ec) { fs::remove(m, ec); return false; }
    return true;
  } catch (...) { return false; }
}

bool RecoveryStore::has() { if (!enabled()) return false; std::lock_guard<std::mutex> g(m_); try { return readMeta(fs::u8path(dir_)) >= 0; } catch (...) { return false; } }

std::string RecoveryStore::announceEvent() {
  if (!enabled()) return "";
  std::lock_guard<std::mutex> g(m_);
  try { const int64_t t = readMeta(fs::u8path(dir_)); return t < 0 ? "" : "{\"t\":\"recovery\",\"title\":\"\",\"time\":" + std::to_string((long long)t) + ",\"file\":null}"; } catch (...) { return ""; }
}

std::string RecoveryStore::getBody() {
  const std::string none = "\"ok\":false,\"error\":\"there is no recovery copy\"";
  if (!enabled()) return none;
  std::lock_guard<std::mutex> g(m_);
  try {
    const fs::path dir = fs::u8path(dir_);
    const int64_t t = readMeta(dir);
    if (t < 0) return none;
    std::ifstream in(dir / kText, std::ios::binary);
    if (!in) return none;
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return "\"ok\":true,\"title\":\"\",\"time\":" + std::to_string((long long)t) + ",\"file\":null,\"source\":\"put\",\"text\":" + jsonQuote(text);
  } catch (...) { return none; }
}

void RecoveryStore::drop() {
  if (!enabled()) return;
  std::lock_guard<std::mutex> g(m_);
  try { const fs::path dir = fs::u8path(dir_); std::error_code ec; fs::remove(dir / kSide, ec); fs::remove(dir / kText, ec); } catch (...) {}
}

}  // namespace tpd
