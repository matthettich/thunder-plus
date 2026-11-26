// H6: small text helpers shared by the shell library: base64 (the desktop `put` and file bytes cross the web view as text), and JSON string escaping for the replies and events the shell writes.
#ifndef TPD_TEXT_UTIL_H
#define TPD_TEXT_UTIL_H

#include <cstdint>
#include <string>

namespace tpd {

std::string base64Encode(const uint8_t* p, size_t n);
// Lenient: whitespace skipped, padding optional. false on a character that is not base64.
bool base64Decode(const std::string& s, std::string* out);
// A JSON string literal (quotes included) for UTF-8 text: ", \, control characters escaped; bytes >= 0x80 pass through.
std::string jsonQuote(const std::string& s);
inline std::string jsonQuote(const char* s) { return jsonQuote(std::string(s)); }

}  // namespace tpd
#endif
