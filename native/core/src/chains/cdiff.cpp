// C2a: chains_diff, the page's chnDiff classified (see cdiff.h). THE CODE IS THE SPECIFICATION: index.html `function chnDiff`. Every JS quirk below is followed on purpose.
//
//   - `if (!next) return []`: next is falsy (missing, null, false, 0, "") -> Same. Any other next is read, whatever it is (a number: its .modules is undefined).
//   - the reload rule: prev falsy, prev.format !== 'chains.patch', prev.modules not an array, or (!prev.st2 !== !next.st2) or (!prev.ti2 !== !next.ti2) -> a `patch` op -> Structural.
//     JS truthiness: "0", [], {} are truthy; 0, "", null, false are not.
//   - `(x.modules || [])`: falsy -> empty; an array -> its elements; any other truthy value has no .map to call -> the page THROWS -> Structural. Same for `cables`.
//   - modules go into `new Map(list.map(m => [m.id, m]))`: m null throws. The key is compared as SameValueZero (1 and "1" differ, 0 and -0 are the same, an object or array id never
//     equals another). DUPLICATE ids: the Map keeps the position of the FIRST occurrence and the value of the LAST. All of chnDiff's loops walk that order and read that value,
//     so a duplicate id's first module is never looked at, and param ops come in the order of the first occurrences. (OMap below.)
//   - Same / Params needs the id sets equal (an id only in next is an `add`, only in prev a `remove`: Structural).
//   - `for (const k in b) if (b[k] !== a[k])` with a = q.params || {}, b = m.params || {}: JS for-in order on an object is the array-index keys ascending (canonical "0".."4294967294"), then the
//     rest in first-insertion order; a duplicate JSON key keeps its first position and its last value (JSON.parse). b may also be an array (keys "0".."n-1") or a string (a for-in over
//     its UTF-16 units); a number / boolean has no keys. a[k] reads an object member, an array element or "length", or a string's unit or "length". `!==` is strict: numbers by value (-0 === 0),
//     strings by content, objects and arrays by identity (the same Json handle). A name that only exists on Object.prototype (constructor, toString, __proto__ when a has no own member) is
//     a function there, never equal to a JSON value: absent. A differing value is a `param` op: Params when it is a finite number (Number.isFinite; 1e999 reads as Infinity) and the
//     module's id is a string, else Structural. (An id that is a number would have to be a string in ChainsParamOp: a decision, see C2A-NOTES.md.)
//   - text: ta / tb are the first 200 UTF-16 units of `text` when it is a string, else undefined; ta !== tb is a `text` op: Structural.
//   - cables: key = from[0] + '.' + from[1] + '>' + to[0] + '.' + to[1] with JS string conversion of each part (number -> Number::toString, null -> "null", undefined -> "undefined",
//     array -> join(","), object -> "[object Object]"; from / to may be a string (its units), an array, an object ("0" / "1" members), or a number / boolean (undefined parts)). A cable that is
//     not an object, or whose from / to is missing or null, THROWS. Duplicate keys: first position, last value. Same / Params needs the key sets equal and `fb === true` equal for each key.
// Order of evaluation does not matter for the class: every throw and every op other than a numeric param is Structural, so the walk stops at the first of them.
#include "chains/cdiff.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>
#include <unordered_map>

#include "rt/jsmath.h"

namespace thunder {
namespace {

// ---- JS values read out of a Json ---------------------------------------------------------------------------------------------------
struct JV {
  enum K : uint8_t { Undef, Null, Bool, Num, Str, Ref } k = Undef;
  bool b = false; double d = 0; std::string s; Json ref;   // Ref: an array or object, compared by identity
};
JV jv_of(Json j) {
  JV v;
  switch (j.type()) {
    case JType::Null: v.k = JV::Null; break;
    case JType::Bool: v.k = JV::Bool; v.b = j.asBool(); break;
    case JType::Number: v.k = JV::Num; v.d = j.asNum(); break;
    case JType::String: v.k = JV::Str; v.s = std::string(j.asStr()); break;
    case JType::Array: case JType::Object: v.k = JV::Ref; v.ref = j; break;
    default: break;
  }
  return v;
}
bool truthy(Json j) {
  switch (j.type()) {
    case JType::Bool: return j.asBool();
    case JType::Number: return j.asNum() != 0;   // JSON has no NaN
    case JType::String: return !j.asStr().empty();
    case JType::Array: case JType::Object: return true;
    default: return false;   // Missing, Null
  }
}
bool strict_eq(const JV& a, const JV& b) {
  if (a.k != b.k) return false;
  switch (a.k) {
    case JV::Undef: case JV::Null: return true;
    case JV::Bool: return a.b == b.b;
    case JV::Num: return a.d == b.d;
    case JV::Str: return a.s == b.s;
    default: return a.ref == b.ref;
  }
}

// ---- UTF-16 views of the stored strings (UTF-8 with lone surrogates as 3 bytes) ------------------------------------------------------
void to_u16(std::string_view s, std::vector<uint16_t>& o, size_t limit) {
  size_t i = 0; const size_t n = s.size();
  while (i < n && o.size() < limit) {
    const uint32_t c = (uint8_t)s[i];
    if (c < 0x80) { o.push_back((uint16_t)c); i++; }
    else if ((c >> 5) == 6 && i + 1 < n) { o.push_back((uint16_t)(((c & 31u) << 6) | ((uint8_t)s[i + 1] & 63u))); i += 2; }
    else if ((c >> 4) == 14 && i + 2 < n) { o.push_back((uint16_t)(((c & 15u) << 12) | (((uint8_t)s[i + 1] & 63u) << 6) | ((uint8_t)s[i + 2] & 63u))); i += 3; }
    else if ((c >> 3) == 30 && i + 3 < n) {
      uint32_t cp = ((c & 7u) << 18) | (((uint8_t)s[i + 1] & 63u) << 12) | (((uint8_t)s[i + 2] & 63u) << 6) | ((uint8_t)s[i + 3] & 63u); i += 4;
      if (cp >= 0x10000) { cp -= 0x10000; o.push_back((uint16_t)(0xD800 + (cp >> 10))); if (o.size() < limit) o.push_back((uint16_t)(0xDC00 + (cp & 1023u))); }
      else o.push_back((uint16_t)cp);
    } else { o.push_back(0xFFFD); i++; }
  }
}
std::string unit_str(uint16_t u) {   // one UTF-16 unit as the stored encoding (a surrogate alone: 3 bytes)
  std::string r;
  if (u < 0x80) r += (char)u;
  else if (u < 0x800) { r += (char)(0xC0 | (u >> 6)); r += (char)(0x80 | (u & 63)); }
  else { r += (char)(0xE0 | (u >> 12)); r += (char)(0x80 | ((u >> 6) & 63)); r += (char)(0x80 | (u & 63)); }
  return r;
}
// a canonical array index: "0", or digits without a leading zero, at most 4294967294
bool canon_index(std::string_view k, uint32_t& out) {
  if (k.empty() || k.size() > 10) return false;
  if (k[0] == '0') { if (k.size() != 1) return false; out = 0; return true; }
  uint64_t v = 0;
  for (char ch : k) { if (ch < '0' || ch > '9') return false; v = v * 10 + (uint64_t)(ch - '0'); }
  if (v > 4294967294ull) return false;
  out = (uint32_t)v; return true;
}

// ---- a JS value used as `x[key]` / `for (k in x)` (an object, an array, a string; anything else has neither) ----------------------------
struct PView {
  Json j; JType t = JType::Missing;
  std::vector<uint16_t> u;                         // a string's units
  std::vector<std::string_view> order;             // an object's for-in order (unique keys)
  std::unordered_map<std::string_view, uint32_t> last;   // an object's key -> member index of the last of equal keys
  void init(Json x) {
    j = x; t = x.type();
    if (t == JType::String) to_u16(x.asStr(), u, (size_t)-1);
    else if (t == JType::Object) {
      std::vector<std::string_view> first;
      const size_t n = x.size();
      last.reserve(n);
      for (size_t i = 0; i < n; i++) { std::string_view k = x.keyAt(i); auto r = last.emplace(k, (uint32_t)i); if (r.second) first.push_back(k); else r.first->second = (uint32_t)i; }
      std::vector<std::pair<uint32_t, std::string_view>> idx;
      for (std::string_view k : first) { uint32_t v; if (canon_index(k, v)) idx.push_back({v, k}); }
      std::stable_sort(idx.begin(), idx.end(), [](const std::pair<uint32_t, std::string_view>& a, const std::pair<uint32_t, std::string_view>& b) { return a.first < b.first; });
      order.reserve(first.size());
      for (auto& p : idx) order.push_back(p.second);
      for (std::string_view k : first) { uint32_t v; if (!canon_index(k, v)) order.push_back(k); }
    } else if (t != JType::Array) t = JType::Missing;
  }
  size_t keyCount() const { return t == JType::Object ? order.size() : t == JType::Array ? j.size() : t == JType::String ? u.size() : 0; }
  std::string keyAt(size_t i) const { return t == JType::Object ? std::string(order[i]) : std::to_string(i); }
  JV get(std::string_view key) const {
    JV v; uint32_t ix;
    if (t == JType::Object) { auto it = last.find(key); if (it != last.end()) v = jv_of(j.valAt(it->second)); }
    else if (t == JType::Array) {
      if (canon_index(key, ix)) { if (ix < j.size()) v = jv_of(j.at(ix)); }
      else if (key == "length") { v.k = JV::Num; v.d = (double)j.size(); }
    } else if (t == JType::String) {
      if (canon_index(key, ix)) { if (ix < u.size()) { v.k = JV::Str; v.s = unit_str(u[ix]); } }
      else if (key == "length") { v.k = JV::Num; v.d = (double)u.size(); }
    }
    return v;
  }
};

// x[i] for a small index i on any value (a cable's from / to)
JV elem(Json x, uint32_t i) {
  switch (x.type()) {
    case JType::Array: return i < x.size() ? jv_of(x.at(i)) : JV();
    case JType::Object: { char b[12]; int n = 0; uint32_t v = i; char r[12]; do { r[n++] = (char)('0' + v % 10); v /= 10; } while (v); for (int q = 0; q < n; q++) b[q] = r[n - 1 - q]; return jv_of(x.get(std::string_view(b, (size_t)n))); }
    case JType::String: { std::vector<uint16_t> u; to_u16(x.asStr(), u, (size_t)i + 1); JV v; if (i < u.size()) { v.k = JV::Str; v.s = unit_str(u[i]); } return v; }
    default: return JV();
  }
}
void to_js_string(const JV& v, std::string& out, int depth);
void json_to_js_string(Json j, std::string& out, int depth) { to_js_string(jv_of(j), out, depth); }
void to_js_string(const JV& v, std::string& out, int depth) {
  switch (v.k) {
    case JV::Undef: out += "undefined"; break;
    case JV::Null: out += "null"; break;
    case JV::Bool: out += v.b ? "true" : "false"; break;
    case JV::Num: { char b[40]; js_number_to_string(v.d, b); out += b; break; }
    case JV::Str: out += v.s; break;
    default:
      if (v.ref.isArr()) {   // Array.prototype.toString: join(","), null as ""
        if (depth > kJsonMaxDepth + 2) break;
        const size_t n = v.ref.size();
        for (size_t i = 0; i < n; i++) { if (i) out += ','; Json e = v.ref.at(i); if (!e.isNull()) json_to_js_string(e, out, depth + 1); }
      } else out += "[object Object]";
  }
}

// ---- a JS Map keyed by a string: first position, last value --------------------------------------------------------------------------
struct OMap {
  std::vector<std::string> keys; std::vector<Json> vals; std::unordered_map<std::string, size_t> idx;
  void put(std::string&& k, Json v) {
    auto it = idx.find(k);
    if (it == idx.end()) { idx.emplace(k, keys.size()); keys.push_back(std::move(k)); vals.push_back(v); }
    else vals[it->second] = v;
  }
  const Json* find(const std::string& k) const { auto it = idx.find(k); return it == idx.end() ? nullptr : &vals[it->second]; }
};

// the SameValueZero identity of a module id as a string key: 's' + text for a string (the only kind that can leave as a ParamOp id)
std::string id_key(Json id, uint64_t& uniq) {
  switch (id.type()) {
    case JType::String: return "s" + std::string(id.asStr());
    case JType::Number: { char b[40]; js_number_to_string(id.asNum(), b); return std::string("d") + b; }
    case JType::Bool: return id.asBool() ? "b1" : "b0";
    case JType::Null: return "n";
    case JType::Missing: return "u";
    default: return "o" + std::to_string(uniq++);   // an array / object: only equal to itself, and prev and next never share one
  }
}
// `list.map(...)` over `(x.modules || [])`: false = the page throws
bool list_of(Json v, Json& out) {
  if (!truthy(v)) { out = Json(); return true; }
  if (!v.isArr()) return false;
  out = v; return true;
}
bool modules_map(Json arr, OMap& m, uint64_t& uniq) {
  const size_t n = arr.size();
  m.vals.reserve(n);
  for (size_t i = 0; i < n; i++) {
    Json e = arr.at(i);
    if (e.isNull()) return false;                       // m.id of null
    m.put(id_key(e.get("id"), uniq), e);                // a non-object has no id: undefined
  }
  return true;
}
bool cables_map(Json arr, OMap& m) {
  const size_t n = arr.size();
  for (size_t i = 0; i < n; i++) {
    Json c = arr.at(i);
    if (!c.isObj()) return false;                       // c.from of null / a primitive / an array is undefined; undefined[0] throws
    Json f = c.get("from"), t = c.get("to");
    if (f.missing() || f.isNull() || t.missing() || t.isNull()) return false;
    std::string k;
    to_js_string(elem(f, 0), k, 0); k += '.'; to_js_string(elem(f, 1), k, 0); k += '>';
    to_js_string(elem(t, 0), k, 0); k += '.'; to_js_string(elem(t, 1), k, 0);
    m.put(std::move(k), c);
  }
  return true;
}
bool fb_true(Json c) { Json f = c.get("fb"); return f.isBool() && f.asBool(); }

ChainsDiff structural() { ChainsDiff d; d.kind = ChainsDiffKind::Structural; return d; }
// q.params || {}
Json params_of(Json m) { Json p = m.get("params"); return truthy(p) ? p : Json(); }
// typeof q.text === 'string' ? q.text.slice(0, 200) : undefined, compared with !==
bool text_same(Json q, Json m) {
  Json a = q.get("text"), b = m.get("text");
  if (!a.isStr() || !b.isStr()) return !a.isStr() && !b.isStr();
  if (a.asStr() == b.asStr()) return true;
  std::vector<uint16_t> x, y; to_u16(a.asStr(), x, 200); to_u16(b.asStr(), y, 200);
  return x == y;
}

}  // namespace

ChainsDiff chains_diff(Json prev, Json next) {
  ChainsDiff r;
  if (!truthy(next)) return r;
  Json pmods = prev.get("modules");
  if (!truthy(prev) || !(prev.get("format").isStr() && prev.get("format").asStr() == "chains.patch") || !pmods.isArr()
      || truthy(prev.get("st2")) != truthy(next.get("st2")) || truthy(prev.get("ti2")) != truthy(next.get("ti2"))) return structural();

  Json nmods;
  if (!list_of(next.get("modules"), nmods)) return structural();
  uint64_t uniq = 0;
  OMap pm, nm;
  if (!modules_map(pmods, pm, uniq) || !modules_map(nmods, nm, uniq)) return structural();
  if (pm.keys.size() != nm.keys.size()) return structural();
  for (const std::string& k : nm.keys) if (!pm.find(k)) return structural();     // an add (and, sizes being equal, a remove)

  for (size_t i = 0; i < nm.keys.size(); i++) {
    Json m = nm.vals[i], q = *pm.find(nm.keys[i]);
    PView A, B; A.init(params_of(q)); B.init(params_of(m));
    const size_t nk = B.keyCount();
    for (size_t j = 0; j < nk; j++) {
      const std::string k = B.keyAt(j);
      const JV bv = B.get(k);
      if (strict_eq(bv, A.get(k))) continue;
      if (bv.k != JV::Num || !std::isfinite(bv.d) || nm.keys[i][0] != 's') return structural();   // not a finite number, or an id that is not a string
      ChainsParamOp op; op.id = nm.keys[i].substr(1); op.name = k; op.v = bv.d; r.params.push_back(std::move(op));
    }
    if (!text_same(q, m)) return structural();
  }

  Json pcs, ncs;
  if (!list_of(prev.get("cables"), pcs) || !list_of(next.get("cables"), ncs)) return structural();
  OMap pc, nc;
  if (!cables_map(pcs, pc) || !cables_map(ncs, nc)) return structural();
  if (pc.keys.size() != nc.keys.size()) return structural();
  for (size_t i = 0; i < nc.keys.size(); i++) {
    const Json* q = pc.find(nc.keys[i]);
    if (!q || fb_true(*q) != fb_true(nc.vals[i])) return structural();
  }
  r.kind = r.params.empty() ? ChainsDiffKind::Same : ChainsDiffKind::Params;
  return r;
}

bool bus_plan_equal(const BusPlan& a, const BusPlan& b) {
  if (a.pure != b.pure || a.pre != b.pre || a.loop != b.loop) return false;
  for (int t = 0; t < kSongNT; t++) {
    if (a.nFeeds[t] != b.nFeeds[t] || a.feedLen[t] != b.feedLen[t]) return false;
    if (a.feedMask[t] != b.feedMask[t] || a.targets[t] != b.targets[t] || a.loopTo[t] != b.loopTo[t]) return false;
    if (a.srcAll[t] != b.srcAll[t] || a.busAll[t] != b.busAll[t]) return false;
    const int n = std::min(std::max(a.nFeeds[t], 0), kMixFeedMax);
    for (int i = 0; i < n; i++) if (a.feeds[t][i] != b.feeds[t][i]) return false;
  }
  return true;
}

}  // namespace thunder
