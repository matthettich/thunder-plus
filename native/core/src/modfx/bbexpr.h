// U21: BBExpr of chains-worklet.js (and index.html's ENGINE block): the safe compiler for one-line bytebeat formulas: text -> tokens -> tree (constants folded) -> a short bytecode run on a 32-bit
// integer stack. Port of `class BBExpr` (compile, tokens, op, hash, the exec loop and s8) line for line; the Int32Array stores are int32 wrap-arounds, x / 0 and x % 0 store 0.
// The error text and position are the JS's (compile() returns false and fills `error` / `pos`; the JS throws). U29's bytebeat engine (BytebeatCore, whose formula text arrives per pad) can use the same.
#ifndef THUNDER_MODFX_BBEXPR_H
#define THUNDER_MODFX_BBEXPR_H
#include <cstdint>
#include <string>
#include <vector>

namespace thunder {
namespace modfx {

class BBProg {
 public:
  // compile(src): false on a bad formula (error = the JS's message, pos = its 0-based character)
  static bool compile(const std::string& src, BBProg* out, std::string* error = nullptr, int* pos = nullptr);
  // run(t, a, b, c): the 32-bit value (the JS's run: the arguments are `| 0`)
  int32_t run(double t, double a, double b, double c) const { exec((int32_t)toI32(t), (int32_t)toI32(a), (int32_t)toI32(b), (int32_t)toI32(c)); return st_[0]; }
  // s8(t, a, b, c, mode): -128..127 (mode 0: the low byte as 0..255 less 128; 1: the low byte signed; 2: the value clipped)
  int s8(int32_t t, int32_t a, int32_t b, int32_t c, int mode) const;
  int nodes() const { return nodes_; }
  int size() const { return (int)code_.size(); }
  static int32_t hash(int32_t x);
  static int32_t op(int o, int32_t x, int32_t y);
 private:
  static int32_t toI32(double v);
  void exec(int32_t t, int32_t a, int32_t b, int32_t c) const;
  std::vector<int32_t> code_; mutable std::vector<int32_t> st_; int nodes_ = 0;
};

}  // namespace modfx
}  // namespace thunder
#endif
