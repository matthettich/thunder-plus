#include "app_scheme.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace tpd {

namespace {
std::string lower(std::string s) { for (auto& c : s) c = (char)std::tolower((unsigned char)c); return s; }
int hexv(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }
fs::path u8(const std::string& s) { return fs::u8path(s); }
}  // namespace

std::string percentDecode(const std::string& s, bool* ok) {
  std::string o; o.reserve(s.size());
  if (ok) *ok = true;
  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] != '%') { o += s[i]; continue; }
    if (i + 2 >= s.size()) { if (ok) *ok = false; return o; }
    const int a = hexv(s[i + 1]), b = hexv(s[i + 2]);
    if (a < 0 || b < 0) { if (ok) *ok = false; return o; }
    o += (char)(a * 16 + b); i += 2;
  }
  return o;
}

std::string mimeOfPath(const std::string& path) {
  const size_t d = path.find_last_of('.');
  const std::string ext = d == std::string::npos ? "" : lower(path.substr(d + 1));
  static const std::pair<const char*, const char*> kMime[] = {
    {"html", "text/html; charset=utf-8"}, {"htm", "text/html; charset=utf-8"}, {"js", "text/javascript; charset=utf-8"}, {"mjs", "text/javascript; charset=utf-8"},
    {"css", "text/css; charset=utf-8"}, {"json", "application/json; charset=utf-8"}, {"thunder", "application/json; charset=utf-8"}, {"wasm", "application/wasm"},
    {"webmanifest", "application/manifest+json"}, {"png", "image/png"}, {"jpg", "image/jpeg"}, {"jpeg", "image/jpeg"}, {"svg", "image/svg+xml"}, {"ico", "image/x-icon"},
    {"woff", "font/woff"}, {"woff2", "font/woff2"}, {"ttf", "font/ttf"}, {"otf", "font/otf"}, {"wav", "audio/wav"}, {"mp3", "audio/mpeg"}, {"ogg", "audio/ogg"}, {"flac", "audio/flac"},
    {"txt", "text/plain; charset=utf-8"}, {"md", "text/plain; charset=utf-8"}, {"sf2", "application/octet-stream"}, {"bin", "application/octet-stream"}};
  for (auto& e : kMime) if (ext == e.first) return e.second;
  return "application/octet-stream";
}

std::string reportOnlyCsp() {
  return "default-src 'self'; script-src 'self' 'unsafe-inline' 'wasm-unsafe-eval' blob:; worker-src 'self' blob:; style-src 'self' 'unsafe-inline'; font-src 'self'; "
         "img-src 'self' data: blob:; media-src 'self' blob: data:; connect-src 'self' blob:; frame-src 'self'; object-src 'none'; base-uri 'none'; form-action 'none'";
}

AppResponse resolveAppUrl(const std::string& url, const std::string& rootDir) {
  AppResponse r;
  const std::string scheme = "app://";
  if (lower(url.substr(0, scheme.size())) != scheme) return r;
  std::string rest = url.substr(scheme.size());
  const size_t cut = rest.find_first_of("?#");
  if (cut != std::string::npos) rest.resize(cut);
  const size_t slash = rest.find('/');
  const std::string host = lower(slash == std::string::npos ? rest : rest.substr(0, slash));
  if (host != "thunder") return r;
  bool ok = true;
  std::string path = percentDecode(slash == std::string::npos ? std::string() : rest.substr(slash), &ok);
  if (!ok) return r;
  if (path.empty() || path.back() == '/') path += "index.html";
  for (char c : path) if (c == '\0' || c == '\\' || c == ':') return r;
  // segments: no "..", no "." (an empty segment from "//" is skipped)
  std::vector<std::string> segs; size_t i = 0;
  while (i <= path.size()) {
    size_t j = path.find('/', i); if (j == std::string::npos) j = path.size();
    const std::string s = path.substr(i, j - i);
    if (s == "..") return r;
    if (!s.empty() && s != ".") segs.push_back(s);
    i = j + 1;
  }
  if (segs.empty()) return r;
  std::error_code ec;
  fs::path root = fs::weakly_canonical(u8(rootDir), ec);
  if (ec) return r;
  fs::path f = root;
  for (auto& s : segs) f /= u8(s);
  if (!fs::is_regular_file(f, ec) || ec) return r;
  const fs::path canon = fs::weakly_canonical(f, ec);
  if (ec) return r;
  // the canonical file must be inside the canonical root (a symlink cannot lead out)
  auto rb = root.begin(), re = root.end(); auto cb = canon.begin(), ce = canon.end();
  for (; rb != re; ++rb, ++cb) if (cb == ce || *cb != *rb) return r;
  r.status = 200;
  r.file = canon.u8string();
  r.mime = mimeOfPath(r.file);
  r.headers.push_back({"Content-Type", r.mime});
  r.headers.push_back({"Access-Control-Allow-Origin", "*"});
  r.headers.push_back({"Cache-Control", "no-cache"});
  if (r.mime.compare(0, 9, "text/html") == 0) r.headers.push_back({"Content-Security-Policy-Report-Only", reportOnlyCsp()});
  return r;
}

bool readWholeFile(const std::string& path, std::string* out) {
  std::ifstream in(u8(path), std::ios::binary);
  if (!in) return false;
  out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return !in.bad();
}

}  // namespace tpd
