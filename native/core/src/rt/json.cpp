#include "json.h"

#include <clocale>
#include <cmath>
#include <cstdlib>
#include <charconv>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace thunder {

namespace {

struct Parser {
  const char* s; size_t n; size_t p = 0;
  bool build = false;
  // counts (dry pass) / write positions (build pass)
  uint32_t nn = 0, ns = 0, nc = 0;
  void* nodesV = nullptr; uint32_t* slots = nullptr; char* chars = nullptr;
  std::vector<uint32_t> scratch;   // child lists under construction (build pass)
  JsonError err; bool failed = false;
};

using NodeV = JsonDoc::Node;

bool fail(Parser& P, size_t at, const char* msg) { if (!P.failed) { P.failed = true; P.err.offset = at; P.err.msg = msg; } return false; }

inline void skipWs(Parser& P) { while (P.p < P.n) { char c = P.s[P.p]; if (c == ' ' || c == '\t' || c == '\n' || c == '\r') P.p++; else break; } }

inline void putChar(Parser& P, char c) { if (P.build) P.chars[P.nc] = c; P.nc++; }

int hexv(char c) { if (c >= '0' && c <= '9') return c - '0'; if (c >= 'a' && c <= 'f') return c - 'a' + 10; if (c >= 'A' && c <= 'F') return c - 'A' + 10; return -1; }

bool hex4(Parser& P, size_t at, unsigned* out) {
  if (at + 4 > P.n) return false;
  unsigned v = 0;
  for (int i = 0; i < 4; i++) { int h = hexv(P.s[at + i]); if (h < 0) return false; v = v * 16 + (unsigned)h; }
  *out = v; return true;
}

void putUtf8(Parser& P, unsigned cp) {   // also writes surrogates (WTF-8)
  if (cp < 0x80) putChar(P, (char)cp);
  else if (cp < 0x800) { putChar(P, (char)(0xC0 | (cp >> 6))); putChar(P, (char)(0x80 | (cp & 63))); }
  else if (cp < 0x10000) { putChar(P, (char)(0xE0 | (cp >> 12))); putChar(P, (char)(0x80 | ((cp >> 6) & 63))); putChar(P, (char)(0x80 | (cp & 63))); }
  else { putChar(P, (char)(0xF0 | (cp >> 18))); putChar(P, (char)(0x80 | ((cp >> 12) & 63))); putChar(P, (char)(0x80 | ((cp >> 6) & 63))); putChar(P, (char)(0x80 | (cp & 63))); }
}

// A string (the opening quote is at P.p). Decoded text goes to chars; *off / *len say where.
bool parseString(Parser& P, uint32_t* off, uint32_t* len) {
  P.p++;
  *off = P.nc;
  for (;;) {
    if (P.p >= P.n) return fail(P, P.p, "unterminated string");
    unsigned char c = (unsigned char)P.s[P.p];
    if (c == '"') { P.p++; break; }
    if (c < 0x20) return fail(P, P.p, "control character in string");
    if (c == '\\') {
      if (P.p + 1 >= P.n) return fail(P, P.p, "unterminated string");
      char e = P.s[P.p + 1];
      switch (e) {
        case '"': putChar(P, '"'); break; case '\\': putChar(P, '\\'); break; case '/': putChar(P, '/'); break;
        case 'b': putChar(P, '\b'); break; case 'f': putChar(P, '\f'); break; case 'n': putChar(P, '\n'); break;
        case 'r': putChar(P, '\r'); break; case 't': putChar(P, '\t'); break;
        case 'u': {
          unsigned u;
          if (!hex4(P, P.p + 2, &u)) return fail(P, P.p, "bad \\u escape");
          P.p += 6;
          if (u >= 0xD800 && u < 0xDC00 && P.p + 1 < P.n && P.s[P.p] == '\\' && P.s[P.p + 1] == 'u') {
            unsigned lo;
            if (hex4(P, P.p + 2, &lo) && lo >= 0xDC00 && lo < 0xE000) { u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00); P.p += 6; }
          }
          putUtf8(P, u);
          continue;
        }
        default: return fail(P, P.p, "bad escape");
      }
      P.p += 2; continue;
    }
    if (c < 0x80) { putChar(P, (char)c); P.p++; continue; }
    // UTF-8 sequence, strictly validated
    int extra; unsigned char lo = 0x80, hi = 0xBF;
    if (c >= 0xC2 && c <= 0xDF) extra = 1;
    else if (c >= 0xE0 && c <= 0xEF) { extra = 2; if (c == 0xE0) lo = 0xA0; else if (c == 0xED) hi = 0x9F; }
    else if (c >= 0xF0 && c <= 0xF4) { extra = 3; if (c == 0xF0) lo = 0x90; else if (c == 0xF4) hi = 0x8F; }
    else return fail(P, P.p, "invalid UTF-8");
    if (P.p + (size_t)extra >= P.n) return fail(P, P.p, "invalid UTF-8");
    for (int k = 1; k <= extra; k++) {
      unsigned char b = (unsigned char)P.s[P.p + (size_t)k];
      if (k == 1 ? (b < lo || b > hi) : (b < 0x80 || b > 0xBF)) return fail(P, P.p, "invalid UTF-8");
    }
    for (int k = 0; k <= extra; k++) putChar(P, P.s[P.p + (size_t)k]);
    P.p += (size_t)extra + 1;
  }
  *len = P.nc - *off;
  return true;
}

double toDouble(const char* t, size_t n, bool simpleInt) {
  if (simpleInt) {   // up to 15 digits: exact in a double
    size_t i = 0; bool neg = false; if (t[0] == '-') { neg = true; i = 1; }
    double v = 0; for (; i < n; i++) v = v * 10 + (t[i] - '0');
    return neg ? -v : v;
  }
  // N1 review C9: the library's strtod follows the process locale (a ',' decimal point would stop it at the '.'), and the old fix read
  // localeconv(), which is not thread-safe against a setlocale on another thread (the Android side may call one). std::from_chars is
  // locale-free and correctly rounded, as strtod is in the C locale: the same doubles (tested against strtod on 100,000 strings:
  // test_hostile). It does not accept a '+' before the number, which JSON never has. An out-of-range result: a value too big is
  // +-infinity and one too small is +-0, as strtod gives (a subnormal is in range, but see below).
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  {
    double v = 0; const std::from_chars_result r = std::from_chars(t, t + n, v);
    if (r.ec == std::errc() && r.ptr == t + n) return v;
    if (r.ec == std::errc::result_out_of_range) {
      // too big or too small? The decimal magnitude of the value: its integer digits (or minus the zeros after the point) plus the exponent.
      const bool neg = t[0] == '-'; size_t i = neg ? 1 : 0; long mag = 0, ex = 0; bool seen = false, tiny = false;
      for (; i < n && t[i] >= '0' && t[i] <= '9'; i++) { if (t[i] != '0') seen = true; if (seen) mag++; }
      if (i < n && t[i] == '.') for (i++; i < n && t[i] >= '0' && t[i] <= '9'; i++) { if (!seen && t[i] == '0') mag--; else seen = true; }
      if (i < n && (t[i] == 'e' || t[i] == 'E')) { i++; const bool en = i < n && t[i] == '-'; if (i < n && (t[i] == '-' || t[i] == '+')) i++; for (; i < n && ex < 100000; i++) ex = ex * 10 + (t[i] - '0'); if (en) ex = -ex; }
      tiny = mag + ex <= 0;
      if (!tiny) return neg ? -HUGE_VAL : HUGE_VAL;
      // too small: libstdc++ reports a SUBNORMAL result as out of range too (it keeps strtod's ERANGE), so these few numbers (below
      // 2.2e-308) go to the strtod path below: the one case that still reads the locale. Overflow, the normal range and every number
      // above 2.2e-308 are locale-free.
    }
  }
#endif
  // No floating-point from_chars in this C++ library: strtod, with the locale's decimal point put in for the '.'.
  char buf[64]; std::string big;
  char* b = buf;
  if (n >= sizeof buf) { big.assign(t, n); b = &big[0]; } else { std::memcpy(buf, t, n); buf[n] = 0; }
  const char dp = std::localeconv()->decimal_point[0];
  if (dp != '.') for (size_t i = 0; i < n; i++) if (b[i] == '.') b[i] = dp;
  if (b == &big[0]) big.push_back(0);
  return std::strtod(b, nullptr);
}

bool parseNumber(Parser& P, uint32_t* off, uint32_t* len, double* val) {
  size_t st = P.p, i = P.p;
  if (i < P.n && P.s[i] == '-') i++;
  if (i >= P.n) return fail(P, i, "bad number");
  if (P.s[i] == '0') i++;
  else if (P.s[i] >= '1' && P.s[i] <= '9') { while (i < P.n && P.s[i] >= '0' && P.s[i] <= '9') i++; }
  else return fail(P, i, "bad number");
  bool simple = true;
  if (i < P.n && P.s[i] == '.') {
    simple = false; i++;
    if (i >= P.n || P.s[i] < '0' || P.s[i] > '9') return fail(P, i, "bad number");
    while (i < P.n && P.s[i] >= '0' && P.s[i] <= '9') i++;
  }
  if (i < P.n && (P.s[i] == 'e' || P.s[i] == 'E')) {
    simple = false; i++;
    if (i < P.n && (P.s[i] == '+' || P.s[i] == '-')) i++;
    if (i >= P.n || P.s[i] < '0' || P.s[i] > '9') return fail(P, i, "bad number");
    while (i < P.n && P.s[i] >= '0' && P.s[i] <= '9') i++;
  }
  size_t n = i - st;
  *off = P.nc; *len = (uint32_t)n;
  if (P.build) { std::memcpy(P.chars + P.nc, P.s + st, n); *val = toDouble(P.s + st, n, simple && n <= 15); }
  P.nc += (uint32_t)n;
  P.p = i;
  return true;
}

bool lit(Parser& P, const char* w) {
  size_t k = std::strlen(w);
  if (P.p + k > P.n || std::memcmp(P.s + P.p, w, k) != 0) return fail(P, P.p, "unexpected token");
  P.p += k; return true;
}

bool parseValue(Parser& P, int depth, uint32_t* idxOut) {
  skipWs(P);
  if (P.p >= P.n) return fail(P, P.p, "unexpected end of input");
  if (depth > kJsonMaxDepth) return fail(P, P.p, "nested too deeply");
  if (P.nn >= 0xFFFFFFF0u) return fail(P, P.p, "document too large");
  const uint32_t me = P.nn++;
  NodeV* N = P.build ? (NodeV*)P.nodesV + me : nullptr;
  if (N) { N->num = 0; N->a = 0; N->b = 0; N->type = 0; }
  *idxOut = me;
  const char c = P.s[P.p];
  if (c == '{' || c == '[') {
    const bool obj = c == '{'; const char close = obj ? '}' : ']';
    const size_t per = obj ? 3 : 1;
    P.p++;
    uint32_t count = 0;
    skipWs(P);
    if (P.p < P.n && P.s[P.p] == close) P.p++;
    else for (;;) {
      skipWs(P);
      if (obj) {
        if (P.p >= P.n || P.s[P.p] != '"') return fail(P, P.p, "expected a string key");
        uint32_t ko = 0, kl = 0; if (!parseString(P, &ko, &kl)) return false;
        skipWs(P);
        if (P.p >= P.n || P.s[P.p] != ':') return fail(P, P.p, "expected ':'");
        P.p++;
        uint32_t v = 0; if (!parseValue(P, depth + 1, &v)) return false;
        if (P.build) { P.scratch.push_back(ko); P.scratch.push_back(kl); P.scratch.push_back(v); }
      } else {
        uint32_t v = 0; if (!parseValue(P, depth + 1, &v)) return false;
        if (P.build) P.scratch.push_back(v);
      }
      count++;
      skipWs(P);
      if (P.p >= P.n) return fail(P, P.p, "unexpected end of input");
      if (P.s[P.p] == ',') { P.p++; continue; }
      if (P.s[P.p] == close) { P.p++; break; }
      return fail(P, P.p, obj ? "expected ',' or '}'" : "expected ',' or ']'");
    }
    const size_t tail = (size_t)count * per;
    if (P.ns + tail >= 0xFFFFFFF0u) return fail(P, P.p, "document too large");
    if (P.build) {   // children have closed and written their own slots: this container's entries are the scratch tail
      N->type = (uint8_t)(obj ? JType::Object : JType::Array); N->a = P.ns; N->b = count;
      const size_t at = P.scratch.size() - tail;
      for (size_t k = 0; k < tail; k++) P.slots[P.ns + k] = P.scratch[at + k];
      P.scratch.resize(at);
    }
    P.ns += (uint32_t)tail;
    return true;
  }
  if (c == '"') {
    uint32_t o = 0, l = 0; if (!parseString(P, &o, &l)) return false;
    if (N) { N->type = (uint8_t)JType::String; N->a = o; N->b = l; }
    return true;
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    uint32_t o = 0, l = 0; double v = 0; if (!parseNumber(P, &o, &l, &v)) return false;
    if (N) { N->type = (uint8_t)JType::Number; N->a = o; N->b = l; N->num = v; }
    return true;
  }
  if (c == 't') { if (!lit(P, "true")) return false; if (N) { N->type = (uint8_t)JType::Bool; N->num = 1; } return true; }
  if (c == 'f') { if (!lit(P, "false")) return false; if (N) { N->type = (uint8_t)JType::Bool; N->num = 0; } return true; }
  if (c == 'n') { if (!lit(P, "null")) return false; if (N) N->type = (uint8_t)JType::Null; return true; }
  return fail(P, P.p, "unexpected character");
}

bool runPass(Parser& P) {
  uint32_t root = 0;
  if (!parseValue(P, 0, &root)) return false;
  skipWs(P);
  if (P.p != P.n) return fail(P, P.p, "unexpected data after the value");
  return true;
}

}  // namespace

bool JsonDoc::parse(const char* text, size_t len, JsonError* err) {
  arena_.reset(); nodes_ = nullptr; slots_ = nullptr; chars_ = nullptr; count_ = 0; bytes_ = 0;
  auto bad = [&](const JsonError& e) { if (err) *err = e; return false; };
  if (!text && len) { JsonError e; e.msg = "no text"; return bad(e); }
  if (len >= 0xFFFFFFF0u) { JsonError e; e.offset = 0; e.msg = "text too large"; return bad(e); }
  Parser dry; dry.s = text; dry.n = len; dry.build = false;
  if (!runPass(dry)) return bad(dry.err);
  const size_t nodeB = (size_t)dry.nn * sizeof(Node), slotB = (size_t)dry.ns * 4, charB = dry.nc;
  const size_t total = nodeB + ((slotB + 7) & ~(size_t)7) + charB;
  std::unique_ptr<uint64_t[]> mem(new (std::nothrow) uint64_t[(total + 7) / 8 + 1]);
  if (!mem) { JsonError e; e.msg = "out of memory"; return bad(e); }
  unsigned char* base = (unsigned char*)mem.get();
  Parser B; B.s = text; B.n = len; B.build = true;
  B.nodesV = base; B.slots = (uint32_t*)(base + nodeB); B.chars = (char*)(base + nodeB + ((slotB + 7) & ~(size_t)7));
  if (!runPass(B)) return bad(B.err);   // cannot differ from the counting pass; kept honest anyway
  arena_ = std::move(mem);
  nodes_ = (Node*)base; slots_ = B.slots; chars_ = B.chars; count_ = dry.nn; bytes_ = total;
  return true;
}

JType Json::type() const { return d_ && i_ < d_->count_ ? (JType)d_->nodes_[i_].type : JType::Missing; }
bool Json::asBool(bool def) const { return type() == JType::Bool ? d_->nodes_[i_].num != 0 : def; }
double Json::asNum(double def) const { return type() == JType::Number ? d_->nodes_[i_].num : def; }
std::string_view Json::asStr(std::string_view def) const {
  if (type() != JType::String) return def;
  const JsonDoc::Node& n = d_->nodes_[i_]; return std::string_view(d_->chars_ + n.a, n.b);
}
std::string_view Json::numText() const {
  if (type() != JType::Number) return {};
  const JsonDoc::Node& n = d_->nodes_[i_]; return std::string_view(d_->chars_ + n.a, n.b);
}
size_t Json::size() const {
  JType t = type(); if (t != JType::Array && t != JType::Object) return 0;
  return d_->nodes_[i_].b;
}
Json Json::at(size_t i) const {
  if (type() != JType::Array || i >= d_->nodes_[i_].b) return Json();
  return Json(d_, d_->slots_[d_->nodes_[i_].a + i]);
}
std::string_view Json::keyAt(size_t i) const {
  if (type() != JType::Object || i >= d_->nodes_[i_].b) return {};
  const uint32_t* s = d_->slots_ + d_->nodes_[i_].a + i * 3; return std::string_view(d_->chars_ + s[0], s[1]);
}
Json Json::valAt(size_t i) const {
  if (type() != JType::Object || i >= d_->nodes_[i_].b) return Json();
  return Json(d_, d_->slots_[d_->nodes_[i_].a + i * 3 + 2]);
}
Json Json::get(std::string_view key) const {
  if (type() != JType::Object) return Json();
  const JsonDoc::Node& n = d_->nodes_[i_];
  for (size_t k = n.b; k-- > 0;) {   // the last of equal keys wins, as in JSON.parse
    const uint32_t* s = d_->slots_ + n.a + k * 3;
    if (s[1] == key.size() && std::memcmp(d_->chars_ + s[0], key.data(), key.size()) == 0) return Json(d_, s[2]);
  }
  return Json();
}

// ---- writer ---------------------------------------------------------------------------------------------------------
static void writeString(std::string_view v, std::string& out) {
  static const char* hx = "0123456789abcdef";
  out.push_back('"');
  const size_t n = v.size();
  for (size_t i = 0; i < n; i++) {
    const unsigned char c = (unsigned char)v[i];
    switch (c) {
      case '"': out += "\\\""; break; case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break; case '\f': out += "\\f"; break; case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break; case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) { out += "\\u00"; out.push_back(hx[c >> 4]); out.push_back(hx[c & 15]); }
        else if (c == 0xED && i + 2 < n && (unsigned char)v[i + 1] >= 0xA0) {   // a lone surrogate (WTF-8): \udXXX
          const unsigned u = 0xD000u | (((unsigned)(unsigned char)v[i + 1] & 63u) << 6) | ((unsigned)(unsigned char)v[i + 2] & 63u);
          out += "\\u"; out.push_back(hx[(u >> 12) & 15]); out.push_back(hx[(u >> 8) & 15]); out.push_back(hx[(u >> 4) & 15]); out.push_back(hx[u & 15]);
          i += 2;
        } else out.push_back((char)c);
    }
  }
  out.push_back('"');
}

void json_write(Json v, std::string& out) {
  switch (v.type()) {
    case JType::Missing: case JType::Null: out += "null"; break;   // a Missing value is undefined in JS; as an array element JSON.stringify gives null
    case JType::Bool: out += v.asBool() ? "true" : "false"; break;
    case JType::Number: { std::string_view t = v.numText(); out.append(t.data(), t.size()); } break;
    case JType::String: writeString(v.asStr(), out); break;
    case JType::Array: {
      out.push_back('[');
      const size_t n = v.size();
      for (size_t i = 0; i < n; i++) { if (i) out.push_back(','); json_write(v.at(i), out); }
      out.push_back(']');
    } break;
    case JType::Object: {
      out.push_back('{');
      const size_t n = v.size();
      for (size_t i = 0; i < n; i++) { if (i) out.push_back(','); writeString(v.keyAt(i), out); out.push_back(':'); json_write(v.valAt(i), out); }
      out.push_back('}');
    } break;
  }
}

}  // namespace thunder
