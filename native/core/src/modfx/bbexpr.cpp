// U21: BBExpr (see bbexpr.h): chains-worklet.js lines 4453 to 4690.
#include "modfx/bbexpr.h"

#include <cmath>
#include <cstring>
#include <memory>

#include "rt/jsmath.h"

namespace thunder {
namespace modfx {
namespace {

const int kMaxLen = 512, kMaxNodes = 400, kMaxDepth = 40;
struct SinTab { int32_t s[256]; SinTab() { for (int i = 0; i < 256; i++) s[i] = (int32_t)js_round(127 * js_sin((double)i / 256 * 2 * 3.141592653589793)); } };   // Math.round(127 * Math.sin(i / 256 * 2 * Math.PI))
const SinTab& sinTab() { static const SinTab t; return t; }
inline int32_t wrap(uint32_t v) { return (int32_t)v; }
// the core is built without exceptions: the first failure is recorded (the JS throws it) and every parse step returns at once when `bad` is set
struct Err { bool bad = false; int pos = 0; std::string msg; };
inline void failTo(Err& e, int pos, const std::string& msg) { if (!e.bad) { e.bad = true; e.pos = pos; e.msg = "Formula: at character " + std::to_string(pos + 1) + ", " + msg; } }

struct Tok { enum K { Num, Id, Op, End } k; int32_t v; std::string s; int p; };
inline bool dig(char c) { return c >= '0' && c <= '9'; }
inline bool hexd(char c) { return dig(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
inline bool alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }

std::vector<Tok> tokens(const std::string& src, Err& E) {
#define fail(pos, msg) do { failTo(E, pos, msg); T.push_back({Tok::End, 0, "", (int)src.size()}); return T; } while (0)
  std::vector<Tok> T; const int n = (int)src.size(); int i = 0;
  while (i < n) {
    const char ch = src[(size_t)i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') { i++; continue; }
    if (dig(ch)) {
      const int p = i; int32_t v;
      if (ch == '0' && i + 1 < n && (src[(size_t)i + 1] == 'x' || src[(size_t)i + 1] == 'X')) {
        i += 2; const int q = i; while (i < n && hexd(src[(size_t)i])) i++;
        if (i == q) fail(p, "a hex number needs digits after 0x");
        if (i - q > 8) fail(p, "that number is too long (32 bits at most)");
        v = wrap((uint32_t)std::stoull(src.substr((size_t)q, (size_t)(i - q)), nullptr, 16));
      } else if (ch == '0' && i + 1 < n && (src[(size_t)i + 1] == 'b' || src[(size_t)i + 1] == 'B')) {
        i += 2; const int q = i; while (i < n && (src[(size_t)i] == '0' || src[(size_t)i] == '1')) i++;
        if (i == q) fail(p, "a binary number needs digits after 0b");
        if (i - q > 32) fail(p, "that number is too long (32 bits at most)");
        v = wrap((uint32_t)std::stoull(src.substr((size_t)q, (size_t)(i - q)), nullptr, 2));
      } else {
        while (i < n && dig(src[(size_t)i])) i++;
        const unsigned long long big = i - p > 10 ? 0 : std::stoull(src.substr((size_t)p, (size_t)(i - p)));
        if (i - p > 10 || big > 4294967295ull) fail(p, "that number is too big (32 bits at most)");
        v = wrap((uint32_t)big);
      }
      if (i < n && src[(size_t)i] == '.') fail(i, "only whole numbers work (the maths is 32-bit whole numbers, like the C original)");
      if (i < n && (alpha(src[(size_t)i]) || dig(src[(size_t)i]))) fail(i, "a number runs into a name (put an operator between them)");
      T.push_back({Tok::Num, v, "", p}); continue;
    }
    if (alpha(ch)) { const int p = i; while (i < n && (alpha(src[(size_t)i]) || dig(src[(size_t)i]))) i++; T.push_back({Tok::Id, 0, src.substr((size_t)p, (size_t)(i - p)), p}); continue; }
    const std::string three = src.substr((size_t)i, 3), two = src.substr((size_t)i, 2);
    if (three == ">>>" || three == "===" || three == "!==") { T.push_back({Tok::Op, 0, three == "===" ? "==" : three == "!==" ? "!=" : three, i}); i += 3; continue; }
    if (two == "<<" || two == ">>" || two == "<=" || two == ">=" || two == "==" || two == "!=" || two == "&&" || two == "||") { T.push_back({Tok::Op, 0, two, i}); i += 2; continue; }
    if (std::strchr("+-*/%&|^~!<>?:(),", ch) && ch != 0) { T.push_back({Tok::Op, 0, std::string(1, ch), i}); i++; continue; }
    if (ch == '=') fail(i, "'=' isn't allowed (a formula can't set anything; == compares)");
    if (ch == '"' || ch == '\'' || ch == '`' || ch == '[' || ch == ']' || ch == '{' || ch == '}') fail(i, "text, [ ] and { } aren't part of a formula");
    if (ch == ';') fail(i, "';' isn't allowed: a formula is one expression");
    if (ch == '.') fail(i, "'.' isn't allowed (whole numbers only, and no methods)");
    { const unsigned char u = (unsigned char)ch; fail(i, std::string("'") + (u > 126 || u < 32 ? "that character" : std::string(1, ch)) + "' isn't allowed here"); }
  }
  T.push_back({Tok::End, 0, "", n});
  return T;
#undef fail
}

// a tree node: o 0 constant (v), 1..4 t a b c, a unary or a function of one (x), a binary (x, y), 50 ?: (x = c, y = a, z = b)
struct Node { int o = 0; int32_t v = 0; int len = 2; int x = -1, y = -1, z = -1; };
inline int funcArgc(const std::string& nm) { return nm == "sin" || nm == "cos" || nm == "tri" || nm == "abs" || nm == "sqrt" || nm == "floor" || nm == "int" ? 1 : nm == "min" || nm == "max" ? 2 : nm == "rnd" || nm == "random" ? -1 : -2; }
inline int binOp(const std::string& s) {
  static const char* k[] = {"+", "-", "*", "/", "%", "<<", ">>", ">>>", "<", "<=", ">", ">=", "==", "!=", "&", "^", "|", "&&", "||"};
  for (int i = 0; i < 19; i++) if (s == k[i]) return 10 + i;
  return 0;
}
const char* const kLevels[10][4] = {{"||"}, {"&&"}, {"|"}, {"^"}, {"&"}, {"==", "!="}, {"<", "<=", ">", ">="}, {"<<", ">>", ">>>"}, {"+", "-"}, {"*", "/", "%"}};

struct Parser {
  const std::string& src; Err E; std::vector<Tok> T; size_t k = 0; int nodes = 0, depth = 0; std::vector<Node> arena;
  explicit Parser(const std::string& s) : src(s) { T = tokens(s, E); }
  void fail(int pos, const std::string& msg) { failTo(E, pos, msg); }
  const Tok& peek() const { return T[k]; }
  const Tok& next() { const Tok& t = T[k]; if (k + 1 < T.size()) k++; return t; }   // (the End token is never stepped over: the JS would read past it, which only happens after a failure)
  std::string what(const Tok& t) const { return t.k == Tok::End ? "the formula ended" : "found '" + (t.k == Tok::Num ? std::to_string(t.v) : t.s) + "'"; }
  void count(const Tok& t) { if (++nodes > kMaxNodes) fail(t.p, "the formula is too complicated (" + std::to_string(kMaxNodes) + " parts at most)"); }
  void enter(const Tok& t) { if (++depth > kMaxDepth + 1) fail(t.p, "it is nested too deep (" + std::to_string(kMaxDepth) + " levels at most)"); }
  int add(const Node& n) { arena.push_back(n); return (int)arena.size() - 1; }
  int cst(int32_t v) { Node n; n.o = 0; n.v = v; return add(n); }
  // mk(o, x, y): anything whose parts are all constants is worked out now (y < 0: a unary / a function of one)
  int mk(int o, int x, int y) {
    if (arena[(size_t)x].o == 0 && (y < 0 || arena[(size_t)y].o == 0)) return cst(BBProg::op(o, arena[(size_t)x].v, y < 0 ? 0 : arena[(size_t)y].v));
    Node n; n.o = o; n.x = x; if (y >= 0) { n.y = y; n.len = 3; } return add(n);
  }
  int expr() {   // ?: (right to left)
    const Tok& t0 = peek(); enter(t0); if (E.bad) return 0;
    const int c = level(0); if (E.bad) return 0;
    if (peek().k == Tok::Op && peek().s == "?") {
      next(); const int a = expr(); if (E.bad) return 0; const Tok& t = next();
      if (t.k != Tok::Op || t.s != ":") { fail(t.p, "expected ':' for the '?' but " + what(t)); return 0; }
      const int b = expr(); if (E.bad) return 0; count(t); depth--; if (E.bad) return 0;
      if (arena[(size_t)c].o == 0) return arena[(size_t)c].v ? a : b;
      Node n; n.o = 50; n.len = 4; n.x = c; n.y = a; n.z = b; return add(n);
    }
    depth--; return c;
  }
  bool inLevel(int i, const Tok& t) const { if (t.k != Tok::Op) return false; for (int j = 0; j < 4 && kLevels[i][j]; j++) if (t.s == kLevels[i][j]) return true; return false; }
  int level(int i) {
    if (i >= 10) return unary();
    int L = level(i + 1); if (E.bad) return 0;
    for (;;) {
      const Tok& t = peek(); if (!inLevel(i, t)) return L;
      next(); const int R = level(i + 1); if (E.bad) return 0; count(t); if (E.bad) return 0; L = mk(binOp(t.s), L, R);
    }
  }
  int unary() {
    const Tok& t = peek();
    if (t.k == Tok::Op && (t.s == "-" || t.s == "+" || t.s == "~" || t.s == "!")) {
      next(); enter(t); if (E.bad) return 0; const int a = unary(); if (E.bad) return 0; depth--; count(t); if (E.bad) return 0;
      return t.s == "+" ? a : mk(t.s == "-" ? 30 : t.s == "~" ? 31 : 32, a, -1);
    }
    return primary();
  }
  int primary() {
    const Tok& t = next(); count(t); if (E.bad) return 0;
    if (t.k == Tok::Num) return cst(t.v);
    if (t.k == Tok::Op && t.s == "(") {
      const int e = expr(); if (E.bad) return 0; const Tok& c = next();
      if (c.k != Tok::Op || c.s != ")") { fail(c.p, "expected ')' to close the '(' at character " + std::to_string(t.p + 1) + " but " + what(c)); return 0; }
      return e;
    }
    if (t.k == Tok::Id) {
      const std::string& nm = t.s; const int vi = nm == "t" ? 0 : nm == "a" ? 1 : nm == "b" ? 2 : nm == "c" ? 3 : -1;
      if (vi >= 0) { Node n; n.o = 1 + vi; return add(n); }
      const int want = funcArgc(nm);
      if (want == -2) { fail(t.p, "'" + nm + "' isn't known (use t, a, b, c, numbers, or sin cos tri abs min max sqrt rnd)"); return 0; }
      const Tok& o = next(); if (o.k != Tok::Op || o.s != "(") { fail(o.p, "expected '(' after " + nm + " but " + what(o)); return 0; }
      std::vector<int> args;
      if (!(peek().k == Tok::Op && peek().s == ")")) {
        for (;;) { args.push_back(expr()); if (E.bad) return 0; const Tok& s = peek(); if (s.k == Tok::Op && s.s == ",") { next(); continue; } break; }
      }
      const Tok& c = next();
      if (c.k != Tok::Op || c.s != ")") { fail(c.p, "expected ')' or ',' in " + nm + "( ) but " + what(c)); return 0; }
      if (want >= 0 && (int)args.size() != want) { fail(t.p, nm + " takes " + std::to_string(want) + (want == 1 ? " value" : " values") + ", not " + std::to_string(args.size())); return 0; }
      if (want < 0 && args.size() > 1) { fail(t.p, nm + " takes one value or none"); return 0; }
      if (nm == "floor" || nm == "int") return args[0];
      const int fo = nm == "sin" ? 40 : nm == "cos" ? 41 : nm == "tri" ? 42 : nm == "abs" ? 43 : nm == "sqrt" ? 44 : nm == "rnd" || nm == "random" ? 45 : nm == "min" ? 46 : 47;
      if (fo >= 46) return mk(fo, args[0], args[1]);
      int a0; if (args.empty()) { Node n; n.o = 1; a0 = add(n); } else a0 = args[0];   // args[0] || [1]
      return mk(fo, a0, -1);
    }
    if (t.k == Tok::End) { fail(t.p, src.find_first_not_of(" \t\n\r\v\f") != std::string::npos ? "the formula ended where a value was expected" : "the formula is empty"); return 0; }
    fail(t.p, "expected a number, t, a, b, c, a function or '(' but " + what(t));
    return 0;
  }
};

struct Emit {
  const std::vector<Node>& A; std::vector<int32_t> code; int sp = 0, top = 1;
  explicit Emit(const std::vector<Node>& a) : A(a) {}
  void push() { if (++sp > top) top = sp; }
  void emit(int ni) {
    const Node& n = A[(size_t)ni]; const int o = n.o;
    if (o == 0) { code.push_back(0); code.push_back(n.v); push(); return; }
    if (o <= 4) { code.push_back(o); push(); return; }
    if (o == 50) {
      emit(n.x); code.push_back(50); code.push_back(0); const size_t j1 = code.size() - 1; sp--;
      emit(n.y); code.push_back(51); code.push_back(0); const size_t j2 = code.size() - 1; sp--;
      code[j1] = (int32_t)code.size(); emit(n.z); code[j2] = (int32_t)code.size(); return;
    }
    if (n.len == 2) { emit(n.x); code.push_back(o); return; }
    if (A[(size_t)n.y].o == 0 && o <= 28) { emit(n.x); code.push_back(o + 50); code.push_back(A[(size_t)n.y].v); return; }
    emit(n.x); emit(n.y); code.push_back(o); sp--;
  }
};

}  // namespace

int32_t BBProg::toI32(double v) { return js_to_int32(v); }

int32_t BBProg::hash(int32_t x) {
  uint32_t h = js_imul((uint32_t)x ^ ((uint32_t)x >> 16), 0x45d9f3bu); h = js_imul(h ^ (h >> 16), 0x45d9f3bu); return (int32_t)((h ^ (h >> 16)) & 255u);
}

int32_t BBProg::op(int o, int32_t x, int32_t y) {
  const int32_t* S = sinTab().s; const uint32_t ux = (uint32_t)x, uy = (uint32_t)y;
  switch (o) {
    case 10: return wrap(ux + uy); case 11: return wrap(ux - uy); case 12: return wrap(js_imul(ux, uy));
    case 13: return y == 0 ? 0 : y == -1 ? wrap(0u - ux) : x / y;
    case 14: return y == 0 || y == -1 ? 0 : x % y;
    case 15: return wrap(ux << (uy & 31)); case 16: return x >> (uy & 31); case 17: return wrap(ux >> (uy & 31));
    case 18: return x < y ? 1 : 0; case 19: return x <= y ? 1 : 0; case 20: return x > y ? 1 : 0; case 21: return x >= y ? 1 : 0;
    case 22: return x == y ? 1 : 0; case 23: return x != y ? 1 : 0; case 24: return x & y; case 25: return x ^ y; case 26: return x | y;
    case 27: return x != 0 && y != 0 ? 1 : 0; case 28: return x != 0 || y != 0 ? 1 : 0;
    case 30: return wrap(0u - ux); case 31: return ~x; case 32: return x ? 0 : 1;
    case 40: return S[ux & 255]; case 41: return S[(ux + 64) & 255];
    case 42: { const int32_t q = (int32_t)((ux + 64) & 255) - 128; return (127 * (64 - (q < 0 ? -q : q))) >> 6; }
    case 43: return x < 0 ? wrap(0u - ux) : x;
    case 44: return (int32_t)std::sqrt(std::fabs((double)x));
    case 45: return hash(x);
    case 46: return x < y ? x : y; case 47: return x > y ? x : y;
  }
  return 0;
}

bool BBProg::compile(const std::string& src, BBProg* out, std::string* error, int* pos) {
  auto bad = [&](const Err& e) { if (error) *error = e.msg; if (pos) *pos = e.pos; return false; };
  if (src.size() > (size_t)kMaxLen) { Err e; failTo(e, kMaxLen, "the formula is too long (" + std::to_string(kMaxLen) + " characters at most)"); return bad(e); }
  Parser P(src); if (P.E.bad) return bad(P.E);
  const int root = P.expr(); if (P.E.bad) return bad(P.E);
  const Tok& e = P.peek();
  if (e.k != Tok::End) { failTo(P.E, e.p, e.k == Tok::Op && e.s == ")" ? "there's a ')' with no '(' before it" : e.k == Tok::Op && (e.s == "," || e.s == ":") ? "'" + e.s + "' isn't expected here" : "expected an operator but " + P.what(e)); return bad(P.E); }
  Emit E(P.arena); E.emit(root);
  out->code_ = E.code; out->st_.assign((size_t)E.top + 2, 0); out->nodes_ = P.nodes;
  return true;
}

// The bytecode loop, with the JS's typed-array behaviour kept for a malformed program (the compiler only makes well-formed ones, except for the 77 / 78 quirk below, which can leave the stack
// or the code short): a read of an index outside the Int32Array (or past the code) is `undefined`, which is NaN in every sum and compares false; a write outside it is ignored; a store wraps
// to 32 bits and NaN or infinity stores 0; a jump to `undefined` ends the program. Values are doubles here (an int32, or NaN for undefined).
namespace {
inline int32_t toI(double v) { return js_to_int32(v); }
inline bool nz(double v) { return v != v || v != 0; }   // `v !== 0` (undefined is not 0)
double opU(int o, double x, double y) {   // one operation as the JS evaluates `St[s] OP y` when either side may be undefined (NaN)
  if (x == x && y == y) return BBProg::op(o, (int32_t)x, (int32_t)y);
  switch (o) {
    case 10: case 11: case 13: case 14: return 0;                                   // NaN stores 0
    case 12: case 15: case 16: case 17: case 24: case 25: case 26: return BBProg::op(o, toI(x), toI(y));   // ToInt32(NaN) is 0
    case 18: case 19: case 20: case 21: return 0;                                    // comparisons with NaN
    case 22: return x != x && y != y ? 1 : 0;                                       // undefined === undefined
    case 23: return x != x && y != y ? 0 : 1;
    case 27: return nz(x) && nz(y) ? 1 : 0;
    case 28: return nz(x) || nz(y) ? 1 : 0;
    case 30: return 0; case 31: return -1; case 32: return 1;                       // -undefined stores 0; ~undefined is ~0; !undefined is 1
    case 40: return BBProg::op(40, 0, 0); case 41: return BBProg::op(41, -64, 0); case 42: return BBProg::op(42, -64, 0);   // (undefined + 64) & 255 is 0
    case 43: case 44: return 0; case 45: return BBProg::op(45, 0, 0);
  }
  return 0;
}
}  // namespace

void BBProg::exec(int32_t t, int32_t a, int32_t b, int32_t c) const {
  const int64_t N = (int64_t)code_.size(), NS = (int64_t)st_.size(); int32_t* St = st_.data();
  auto C = [&](int64_t k) -> double { return k >= 0 && k < N ? (double)code_[(size_t)k] : (double)NAN; };
  auto rd = [&](int64_t k) -> double { return k >= 0 && k < NS ? (double)St[k] : (double)NAN; };
  auto wr = [&](int64_t k, double v) { if (k >= 0 && k < NS) St[k] = toI(v); };
  int64_t s = -1, i = 0;
  while (i < N) {
    const double od = C(i++);
    if (od != od) { i = N; continue; }
    const int op = (int)od;
    switch (op) {
      case 0: ++s; wr(s, C(i)); i++; break;
      case 1: ++s; wr(s, t); break; case 2: ++s; wr(s, a); break; case 3: ++s; wr(s, b); break; case 4: ++s; wr(s, c); break;
      case 10: case 11: case 12: case 13: case 14: case 15: case 16: case 17: case 18: case 19: case 20: case 21: case 22: case 23: case 24: case 25: case 26: case 27: case 28:
      { const double y = rd(s); s--; wr(s, opU(op, rd(s), y)); break; }
      case 30: case 31: case 32: case 40: case 41: case 42: case 43: case 44: case 45: wr(s, opU(op, rd(s), 0)); break;
      case 46: { const double y = rd(s); s--; if (y < rd(s)) wr(s, y); break; }
      case 47: { const double y = rd(s); s--; if (y > rd(s)) wr(s, y); break; }
      case 50: { const double v = rd(s); s--; if (v == 0) { const double j = C(i); i = j != j ? N : (int64_t)j; } else i++; break; }
      case 51: { const double j = C(i); i = j != j ? N : (int64_t)j; break; }
      case 60: case 61: case 62: case 63: case 64: case 65: case 66: case 67: case 68: case 69: case 70: case 71: case 72: case 73: case 74: case 75: case 76:
      { const double x = rd(s), y = C(i); i++; wr(s, opU(op - 50, x, y)); break; }
      // 77 / 78 (`x && constant`, `x || constant`): the constant is read before the test (web build 633), so a short-circuit no longer leaves it to run as a step.
      case 77: { const double x = rd(s); const double y = C(i); i++; wr(s, nz(x) && nz(y) ? 1 : 0); break; }
      case 78: { const double x = rd(s); const double y = C(i); i++; wr(s, nz(x) || nz(y) ? 1 : 0); break; }
      default: i = N;
    }
  }
}

int BBProg::s8(int32_t t, int32_t a, int32_t b, int32_t c, int mode) const {
  exec(t, a, b, c); const int32_t v = st_[0];
  return mode == 1 ? (int)(int8_t)(v & 255) : mode == 2 ? (v < -128 ? -128 : v > 127 ? 127 : v) : (v & 255) - 128;
}

}  // namespace modfx
}  // namespace thunder
