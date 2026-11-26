#include "text_util.h"

#include <cstdio>

namespace tpd {

static const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* p, size_t n) {
  std::string o; o.reserve((n + 2) / 3 * 4);
  size_t i = 0;
  for (; i + 2 < n; i += 3) {
    const uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8 | p[i + 2];
    o += kB64[v >> 18 & 63]; o += kB64[v >> 12 & 63]; o += kB64[v >> 6 & 63]; o += kB64[v & 63];
  }
  if (i + 1 == n) { const uint32_t v = (uint32_t)p[i] << 16; o += kB64[v >> 18 & 63]; o += kB64[v >> 12 & 63]; o += "=="; }
  else if (i + 2 == n) { const uint32_t v = (uint32_t)p[i] << 16 | (uint32_t)p[i + 1] << 8; o += kB64[v >> 18 & 63]; o += kB64[v >> 12 & 63]; o += kB64[v >> 6 & 63]; o += '='; }
  return o;
}

bool base64Decode(const std::string& s, std::string* out) {
  out->clear(); out->reserve(s.size() / 4 * 3);
  uint32_t acc = 0; int bits = 0;
  for (char c : s) {
    int v;
    if (c >= 'A' && c <= 'Z') v = c - 'A'; else if (c >= 'a' && c <= 'z') v = c - 'a' + 26; else if (c >= '0' && c <= '9') v = c - '0' + 52;
    else if (c == '+') v = 62; else if (c == '/') v = 63; else if (c == '=' || c == ' ' || c == '\n' || c == '\r' || c == '\t') continue; else return false;
    acc = acc << 6 | (uint32_t)v; bits += 6;
    if (bits >= 8) { bits -= 8; out->push_back((char)(acc >> bits & 0xff)); }
  }
  return true;
}

std::string jsonQuote(const std::string& s) {
  std::string o; o.reserve(s.size() + 2); o += '"';
  for (unsigned char c : s) {
    switch (c) {
      case '"': o += "\\\""; break; case '\\': o += "\\\\"; break; case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
      default: if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; } else o += (char)c;
    }
  }
  o += '"'; return o;
}

}  // namespace tpd
