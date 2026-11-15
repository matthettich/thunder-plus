// A small JSON reader and writer (U06). Our own code, no exceptions, no RTTI, no third-party parts.
//   - JsonDoc::parse() turns UTF-8 JSON text into an immutable value tree held in ONE arena owned by the document (a single
//     allocation, sized by a counting pass). Parsing allocates and is for the control thread only; reading the tree
//     (Json's getters, json_write into a reserved string) allocates nothing.
//   - Numbers keep their source text, so writing a number back gives the same characters. Strings are stored decoded (UTF-8;
//     a lone surrogate as the three bytes WTF-8 gives it, and written back as \udXXX like JSON.stringify does).
//   - Object members stay in source order and duplicate keys are kept as they come; get() answers with the LAST of them (what
//     JSON.parse leaves in the object).
//   - Errors carry the byte offset; nothing here crashes on bad input (nesting is limited to kJsonMaxDepth).
#ifndef THUNDER_RT_JSON_H
#define THUNDER_RT_JSON_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace thunder {

enum class JType : uint8_t { Missing = 0, Null, Bool, Number, String, Array, Object };

struct JsonError { size_t offset = 0; const char* msg = nullptr; };

constexpr int kJsonMaxDepth = 256;

class JsonDoc;

// A handle to one value of a document (a document pointer and a node index). Cheap to copy. A missing value (a key that is not
// there, an index out of range) is a Json of type Missing; asking it for anything gives the default.
class Json {
 public:
  Json() = default;
  JType type() const;
  bool missing() const { return type() == JType::Missing; }
  bool isNull() const { return type() == JType::Null; }
  bool isBool() const { return type() == JType::Bool; }
  bool isNum() const { return type() == JType::Number; }
  bool isStr() const { return type() == JType::String; }
  bool isArr() const { return type() == JType::Array; }
  bool isObj() const { return type() == JType::Object; }

  // Values of this node (the default when the node is of another type).
  bool asBool(bool def = false) const;
  double asNum(double def = 0) const;
  std::string_view asStr(std::string_view def = {}) const;
  std::string_view numText() const;   // a number's source text ("" for anything else)

  size_t size() const;                // elements of an array, members of an object, else 0
  Json at(size_t i) const;            // array element (Missing when out of range or not an array)
  Json get(std::string_view key) const;   // object member by key (the last of equal keys); Missing when absent or not an object
  std::string_view keyAt(size_t i) const; // object member i's key
  Json valAt(size_t i) const;         // object member i's value

  // Typed getters on an object member, with defaults.
  double num(std::string_view key, double def = 0) const { Json v = get(key); return v.isNum() ? v.asNum() : def; }
  bool boolean(std::string_view key, bool def = false) const { Json v = get(key); return v.isBool() ? v.asBool() : def; }
  std::string_view str(std::string_view key, std::string_view def = {}) const { Json v = get(key); return v.isStr() ? v.asStr() : def; }
  Json arr(std::string_view key) const { Json v = get(key); return v.isArr() ? v : Json(); }   // Missing unless an array
  Json obj(std::string_view key) const { Json v = get(key); return v.isObj() ? v : Json(); }   // Missing unless an object

  bool operator==(const Json& o) const { return d_ == o.d_ && i_ == o.i_; }
 private:
  friend class JsonDoc;
  Json(const JsonDoc* d, uint32_t i) : d_(d), i_(i) {}
  const JsonDoc* d_ = nullptr;
  uint32_t i_ = 0;
};

class JsonDoc {
 public:
  JsonDoc() = default;
  JsonDoc(const JsonDoc&) = delete;
  JsonDoc& operator=(const JsonDoc&) = delete;

  // Parses one JSON value (surrounding whitespace allowed, nothing else after it). false: *err has the byte offset and a
  // message, and the document is empty.
  bool parse(const char* text, size_t len, JsonError* err = nullptr);
  bool parse(std::string_view t, JsonError* err = nullptr) { return parse(t.data(), t.size(), err); }
  Json root() const { return count_ ? Json(this, 0) : Json(); }
  size_t arenaBytes() const { return bytes_; }

 private:
  friend class Json;
 public:
  struct Node { double num; uint32_t a, b; uint8_t type; };   // internal: number/string: a = char offset, b = length; array/object: a = first slot, b = count
 private:
  std::unique_ptr<uint64_t[]> arena_;
  Node* nodes_ = nullptr;
  uint32_t* slots_ = nullptr;   // array: one node index per element; object: three per member (key offset, key length, node index)
  char* chars_ = nullptr;
  uint32_t count_ = 0;
  size_t bytes_ = 0;
};

// Serialises v as JSON.stringify would (compact, members in order). Appends to out.
void json_write(Json v, std::string& out);
inline std::string json_write(Json v) { std::string s; json_write(v, s); return s; }

}  // namespace thunder
#endif
