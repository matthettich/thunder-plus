// H6: the app:// scheme mapping (app_scheme.h): bundle files, index.html defaults, query / fragment, percent-decoding, traversal and symlink escapes, hosts, MIME types, headers.
#include <filesystem>
#include <fstream>
#include "app_scheme.h"
#include "check.h"
namespace fs = std::filesystem;
static void put(const fs::path& p, const std::string& t) { fs::create_directories(p.parent_path()); std::ofstream(p, std::ios::binary) << t; }
int main() {
  const fs::path base = fs::temp_directory_path() / "tpd_app_scheme_test";
  fs::remove_all(base);
  const fs::path root = base / "web", outside = base / "secret.txt";
  put(root / "index.html", "<html></html>"); put(root / "a b.js", "1"); put(root / "core/seq-core.wasm", "x"); put(root / "sub/index.html", "s"); put(root / "fonts/x.woff2", "f");
  put(outside, "secret");
  const std::string R = root.u8string();
  auto r = [&](const std::string& u) { return tpd::resolveAppUrl(u, R); };
  CHECK(r("app://thunder/index.html").status == 200 && r("app://thunder/index.html").mime.find("text/html") == 0, "index.html");
  CHECK(r("app://thunder/").status == 200 && r("app://thunder").status == 200, "root is index.html");
  CHECK(r("app://thunder/sub/").status == 200, "a folder is its index.html");
  CHECK(r("app://thunder/index.html?x=1#y").status == 200, "query and fragment ignored");
  CHECK(r("app://THUNDER/index.html").status == 200 && r("APP://thunder/index.html").status == 200, "scheme and host are case-insensitive");
  CHECK(r("app://thunder/a%20b.js").status == 200 && r("app://thunder/a%20b.js").mime.find("text/javascript") == 0, "percent-decoded");
  CHECK(r("app://thunder/core/seq-core.wasm").mime == "application/wasm", "wasm mime (streaming compile)");
  CHECK(r("app://thunder/fonts/x.woff2").mime == "font/woff2", "woff2");
  CHECK(r("app://other/index.html").status == 404, "another host");
  CHECK(r("http://thunder/index.html").status == 404 && r("").status == 404, "another scheme");
  CHECK(r("app://thunder/nope.js").status == 404, "missing");
  CHECK(r("app://thunder/../secret.txt").status == 404, "..");
  CHECK(r("app://thunder/%2e%2e/secret.txt").status == 404, "encoded ..");
  CHECK(r("app://thunder/sub/%2E%2E/%2e%2e/secret.txt").status == 404, "encoded .. in a deeper path");
  CHECK(r("app://thunder/..%2fsecret.txt").status == 404, "encoded slash after ..");
  CHECK(r("app://thunder/a%5Cb").status == 404 && r("app://thunder/a\\b").status == 404, "backslash");
  CHECK(r("app://thunder/C:/Windows/win.ini").status == 404, "drive letter");
  CHECK(r("app://thunder/a%00.js").status == 404 && r("app://thunder/%zz").status == 404 && r("app://thunder/%4").status == 404, "NUL and bad escapes");
  CHECK(r("app://thunder/sub").status == 404, "a folder without a slash is not a file");
  { auto x = r("app://thunder/index.html"); bool csp = false, cors = false; for (auto& h : x.headers) { if (h.first == "Content-Security-Policy-Report-Only") csp = true; if (h.first == "Access-Control-Allow-Origin") cors = true; } CHECK(csp && !cors, "html: report-only CSP, and no cross-origin header (the page is same-origin)"); }
  { auto x = r("app://thunder/a%20b.js"); bool csp = false; for (auto& h : x.headers) if (h.first == "Content-Security-Policy-Report-Only") csp = true; CHECK(!csp, "no CSP on a script"); }
  std::string body; CHECK(tpd::readWholeFile(r("app://thunder/index.html").file, &body) && body == "<html></html>", "readWholeFile");
  // a symlink inside the root that leads out
  std::error_code ec; fs::create_symlink(outside, root / "link.txt", ec);
  if (!ec) CHECK(r("app://thunder/link.txt").status == 404, "a symlink out of the root is refused"); else std::printf("(symlinks not available here: that check skipped)\n");
  bool ok = true; CHECK(tpd::percentDecode("a%2Fb%20c", &ok) == "a/b c" && ok, "percentDecode");
  CHECK(tpd::isAppOrigin("app://thunder/") && tpd::isAppOrigin("app://thunder") && tpd::isAppOrigin("APP://thunder/index.html?seqtest=1") && tpd::isAppOrigin("app://thunder#x"), "isAppOrigin: the app");
  CHECK(!tpd::isAppOrigin("app://thunder.evil/") && !tpd::isAppOrigin("app://thunderx/") && !tpd::isAppOrigin("data:text/html,x") && !tpd::isAppOrigin("blob:app://thunder/x") && !tpd::isAppOrigin("https://app://thunder/") && !tpd::isAppOrigin(""), "isAppOrigin: nothing else");
  CHECK(r("app://thunder/%ff%fe.js").status == 404 && r("app://thunder/%c3%28").status == 404, "R-2: bytes that are not UTF-8 are a 404, never a throw");
  fs::remove_all(base);
  DONE("test_desktop_app_scheme");
}
