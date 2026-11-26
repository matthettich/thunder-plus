// H6: the app:// URL scheme of the desktop shells (the same job as Electron's protocol.handle('app') in electron/main.js and the Android shell's MimeAssets / WebViewAssetLoader).
// PLATFORM-NEUTRAL: no WebView2 / Cocoa type here; a Linux test runs it (native/desktop/tests/test_app_scheme.cpp).
//   app://thunder/<path>  ->  <bundle root>/<path>; "" and a trailing "/" mean index.html; the query and the fragment are ignored; the path is percent-decoded.
//   Refused (404): another host, a ".." segment (after decoding), a backslash, a colon, a NUL, a path that leaves the root through a symlink.
#ifndef TPD_APP_SCHEME_H
#define TPD_APP_SCHEME_H

#include <string>
#include <utility>
#include <vector>

namespace tpd {

struct AppResponse {
  int status = 404;            // 200 or 404 (405 for a method other than GET / HEAD is the shell's)
  std::string mime;            // Content-Type value
  std::string file;            // the file to read when status == 200 (native path, UTF-8)
  std::vector<std::pair<std::string, std::string>> headers;   // every response header (Content-Type included)
};

std::string percentDecode(const std::string& s, bool* ok);
std::string mimeOfPath(const std::string& path);   // by extension; .wasm is application/wasm (streaming compile), unknown: application/octet-stream
// The report-only CSP Electron sends on HTML (electron/main.js CSP): same text.
std::string reportOnlyCsp();
// `rootDir`: the bundle directory (UTF-8). The file's existence is checked here.
AppResponse resolveAppUrl(const std::string& url, const std::string& rootDir);
// true for app://thunder, app://thunder/..., ?... and #... only (the one origin the shells trust; a lookalike host such as app://thunder.evil or app://thunderx is not).
bool isAppOrigin(const std::string& url);
// Whole file into `out`. false when it cannot be read.
bool readWholeFile(const std::string& path, std::string* out);

}  // namespace tpd
#endif
