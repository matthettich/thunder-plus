// U26: VoxCore (index.html "voice engine") and renderVox, line for line. See vox_core.h. The tables (phonemes, letter-to-sound rules, dictionary, Unicode case mappings) are in vox_tables.inc, generated from the page.
#include "engine/vox_core.h"

#include <cmath>
#include <cstring>
#include <string_view>

#include "engine/xeng_util.h"

namespace thunder {
using namespace xe;

namespace {
#include "engine/vox_tables.inc"

struct Ph { char t; double f[3]; bool hasG; double g[3]; double av, nf, nb, fa, bf, bb, ba, d; };   // a field the JS leaves undefined is NaN
struct PhTab { Ph p[64]; int n; int AX, SH, ZH; };
const PhTab& phTab() {
  static const PhTab T = [] { PhTab t{}; t.n = kVoxPhN; for (int i = 0; i < kVoxPhN; i++) { const std::string_view nm = kVoxPhDefs[i].name; if (nm == "AX") t.AX = i; else if (nm == "SH") t.SH = i; else if (nm == "ZH") t.ZH = i; }
    for (int i = 0; i < kVoxPhN; i++) { const VoxPhDef& d = kVoxPhDefs[i]; Ph& p = t.p[i]; p.t = d.t; for (int k = 0; k < 3; k++) { p.f[k] = d.f[k]; p.g[k] = d.g[k]; } p.hasG = d.hasG;
      double* F[8] = {&p.av, &p.nf, &p.nb, &p.fa, &p.bf, &p.bb, &p.ba, &p.d}; const double V[8] = {d.av, d.nf, d.nb, d.fa, d.bf, d.bb, d.ba, d.d};
      for (int k = 0; k < 8; k++) *F[k] = (d.has >> k) & 1 ? V[k] : (double)NAN; }
    return t; }();
  return T;
}
int phId(std::string_view s) { for (int i = 0; i < kVoxPhN; i++) if (s == kVoxPhDefs[i].name) return i; return -1; }   // PH[x] (-1: undefined)
int idOf(const char* name) { return phId(name); }
constexpr int kDash = -2;   // the '-' of a forced syllable break
inline bool isV(int x) { return x >= 0 && phTab().p[x].t == 'V'; }
inline int rankOf(int x) { if (x < 0) return 0; const char t = phTab().p[x].t; return t == 'N' || t == 'L' ? 3 : t == 'Z' ? 2 : t == 'F' ? 1 : 0; }   // ({N:3, L:3, Z:2, F:1}[t] || 0)

// ---- the text: UTF-8 (WTF-8) to code points, String.prototype.toLowerCase / toUpperCase for the ASCII letters they can make, JS whitespace ------------------------------------------------------------
void decode(const std::string& s, std::vector<uint32_t>& out) {
  const unsigned char* p = (const unsigned char*)s.data(); const size_t n = s.size(); size_t i = 0;
  while (i < n) {
    const unsigned c = p[i]; uint32_t cp; size_t k;
    if (c < 0x80) { cp = c; k = 1; }
    else if (c >= 0xC2 && c < 0xE0 && i + 1 < n && (p[i + 1] & 0xC0) == 0x80) { cp = ((c & 0x1F) << 6) | (p[i + 1] & 0x3F); k = 2; }
    else if (c >= 0xE0 && c < 0xF0 && i + 2 < n && (p[i + 1] & 0xC0) == 0x80 && (p[i + 2] & 0xC0) == 0x80) { cp = ((c & 0x0F) << 12) | ((p[i + 1] & 0x3F) << 6) | (p[i + 2] & 0x3F); k = 3; }
    else if (c >= 0xF0 && c < 0xF5 && i + 3 < n && (p[i + 1] & 0xC0) == 0x80 && (p[i + 2] & 0xC0) == 0x80 && (p[i + 3] & 0xC0) == 0x80) { cp = ((c & 0x07) << 18) | ((p[i + 1] & 0x3F) << 12) | ((p[i + 2] & 0x3F) << 6) | (p[i + 3] & 0x3F); k = 4; }
    else { cp = 0xFFFD; k = 1; }
    out.push_back(cp); i += k;
  }
}
bool isSpace(uint32_t c) { if (c < 128) return c == 32 || (c >= 9 && c <= 13); for (int i = 0; i < kVoxSpaceN; i++) if (kVoxSpace[i] == c) return true; return false; }
void lowerCp(uint32_t c, std::vector<uint32_t>& out) {
  if (c < 128) { out.push_back(c >= 'A' && c <= 'Z' ? c + 32 : c); return; }
  for (int i = 0; i < kVoxLowerN; i++) if (kVoxLower[i].cp == c) { for (int k = 0; k < kVoxLower[i].n; k++) out.push_back(kVoxLower[i].s[k]); return; }
  out.push_back(c);
}
void upperCp(uint32_t c, std::string& cur) {   // appends to a phoneme token: ASCII as is, any other character as a byte no phoneme has
  if (c < 128) { cur.push_back((char)(c >= 'a' && c <= 'z' ? c - 32 : c)); return; }
  for (int i = 0; i < kVoxUpperN; i++) if (kVoxUpper[i].cp == c) { for (int k = 0; k < kVoxUpper[i].n; k++) { const unsigned u = kVoxUpper[i].s[k]; cur.push_back(u < 128 ? (char)u : '\x01'); } return; }
  cur.push_back('\x01');
}
inline bool wordCh(uint32_t c) { return (c >= 'a' && c <= 'z') || c == '\''; }
inline bool punctCh(uint32_t c) { return c == '.' || c == ',' || c == ';' || c == ':' || c == '!' || c == '?' || c == '_'; }

// ---- letter-to-sound (VoxCore.rules / ctxL / ctxR) ---------------------------------------------------------------------------------------------------------------------------------------------
inline bool cls(const std::string& w, long j, const char* set) { return j >= 0 && j < (long)w.size() && std::strchr(set, w[(size_t)j]) != nullptr && w[(size_t)j] != 0; }   // set.indexOf(w[j]) >= 0 (an undefined character is not in the set)
inline bool sub2(const std::string& w, long j, const char* two) { return j >= 0 && (size_t)j + 2 <= w.size() && w[(size_t)j] == two[0] && w[(size_t)j + 1] == two[1]; }   // w.substr(j, 2) === two
bool ctxL(const std::string& w, long j, const char* pat) {   // pattern matched right to left, ending at w[j]
  static const char* VOW = "AEIOU"; static const char* CON = "BCDFGHJKLMNPQRSTVWXYZ";
  for (long k = (long)std::strlen(pat) - 1; k >= 0; k--) {
    const char s = pat[k];
    if (s == '#') { if (j < 0 || !cls(w, j, VOW)) return false; while (j >= 0 && cls(w, j, VOW)) j--; }
    else if (s == ':') { while (j >= 0 && cls(w, j, CON)) j--; }
    else if (s == '^') { if (j < 0 || !cls(w, j, CON)) return false; j--; }
    else if (s == '.') { if (j < 0 || !cls(w, j, "BDVGJLMNRWZ")) return false; j--; }
    else if (s == '+') { if (j < 0 || !cls(w, j, "EIY")) return false; j--; }
    else if (s == '&') { if (j >= 1 && (sub2(w, j - 1, "CH") || sub2(w, j - 1, "SH"))) j -= 2; else if (j >= 0 && cls(w, j, "SCGZXJ")) j--; else return false; }
    else if (s == '@') { if (j >= 1 && (sub2(w, j - 1, "TH") || sub2(w, j - 1, "CH") || sub2(w, j - 1, "SH"))) j -= 2; else if (j >= 0 && cls(w, j, "TSRDLZNJ")) j--; else return false; }
    else if (s == '_') { if (j >= 0 && w[(size_t)j] != ' ') return false; j--; }
    else { if (j < 0 || w[(size_t)j] != s) return false; j--; }
  }
  return true;
}
bool ctxR(const std::string& w, long j, const char* pat) {   // pattern matched left to right from w[j]
  static const char* VOW = "AEIOU"; static const char* CON = "BCDFGHJKLMNPQRSTVWXYZ"; const long n = (long)w.size();
  for (size_t k = 0; pat[k]; k++) {
    const char s = pat[k];
    if (s == '#') { if (j >= n || !cls(w, j, VOW)) return false; while (j < n && cls(w, j, VOW)) j++; }
    else if (s == ':') { while (j < n && cls(w, j, CON)) j++; }
    else if (s == '^') { if (j >= n || !cls(w, j, CON)) return false; j++; }
    else if (s == '.') { if (j >= n || !cls(w, j, "BDVGJLMNRWZ")) return false; j++; }
    else if (s == '+') { if (j >= n || !cls(w, j, "EIY")) return false; j++; }
    else if (s == '%') {
      static const char* const SUF[6] = {"ING", "ELY", "ER", "ES", "ED", "E"}; const char* suf = nullptr;
      for (const char* x : SUF) {   // w.substr(j, x.length) === x && (w[j + x.length] === ' ' || x === 'ER' || x === 'ING' || x === 'ELY')
        const size_t L = std::strlen(x); if (j < 0 || (size_t)j + L > w.size() || w.compare((size_t)j, L, x) != 0) continue;
        if (((size_t)j + L < w.size() && w[(size_t)j + L] == ' ') || !std::strcmp(x, "ER") || !std::strcmp(x, "ING") || !std::strcmp(x, "ELY")) { suf = x; break; } }
      if (!suf) return false;
      j += (long)std::strlen(suf);
    }
    else if (s == '&') { if (sub2(w, j, "CH") || sub2(w, j, "SH")) j += 2; else if (j < n && cls(w, j, "SCGZXJ")) j++; else return false; }
    else if (s == '@') { if (sub2(w, j, "TH") || sub2(w, j, "CH") || sub2(w, j, "SH")) j += 2; else if (j < n && cls(w, j, "TSRDLZNJ")) j++; else return false; }
    else if (s == '_') { if (j < n && w[(size_t)j] != ' ') return false; j++; }
    else { if (j >= n || w[(size_t)j] != s) return false; j++; }
  }
  return true;
}
// appends the phonemes of a space separated name list (rules map WH to W)
void pushNames(const char* names, std::vector<int>& out, bool rulesMap) {
  const char* p = names;
  while (*p) { while (*p == ' ') p++; if (!*p) break; const char* q = p; while (*q && *q != ' ') q++; std::string_view nm(p, (size_t)(q - p)); if (rulesMap && nm == "WH") nm = "W"; out.push_back(phId(nm)); p = q; }
}
void rulesOf(const std::string& word, std::vector<int>& out) {
  std::string w = " "; for (char c : word) w.push_back((char)(c >= 'a' && c <= 'z' ? c - 32 : c)); w.push_back(' ');
  long i = 1;
  while (i < (long)w.size() - 1) {
    const char c = w[(size_t)i]; bool listed = false, hit = false;
    for (int r = 0; r < kVoxRuleN; r++) {
      const VoxRuleDef& d = kVoxRules[r]; if (d.letter != c) continue; listed = true;
      const size_t ml = std::strlen(d.m);
      if (w.compare((size_t)i, ml, d.m) != 0 || (size_t)i + ml > w.size()) continue;   // w.substr(i, m.length) !== m
      if (!ctxL(w, i - 1, d.l) || !ctxR(w, i + (long)ml, d.r)) continue;
      pushNames(d.p, out, true); i += (long)ml; hit = true; break;
    }
    if (!listed) { i++; continue; }
    if (!hit) i++;
  }
}
const VoxDictDef* dictFind(const std::string& w) { for (int i = 0; i < kVoxDictN; i++) if (w == kVoxDict[i].w) return &kVoxDict[i]; return nullptr; }

// ---- VoxCore.syllabify ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
void syllabify(const std::vector<int>& ph, VoxLyrics& L) {
  auto add = [&](size_t a, size_t v, size_t e) { VoxSyl s; s.rest = false; s.nu = ph[v]; s.onB = (uint32_t)L.pool.size(); for (size_t i = a; i < v; i++) L.pool.push_back((int16_t)ph[i]); s.onN = (uint32_t)(v - a);
    s.coB = (uint32_t)L.pool.size(); for (size_t i = v + 1; i < e; i++) L.pool.push_back((int16_t)ph[i]); s.coN = (uint32_t)(e > v + 1 ? e - v - 1 : 0); L.syl.push_back(s); };
  std::vector<size_t> V; for (size_t i = 0; i < ph.size(); i++) if (isV(ph[i])) V.push_back(i);
  if (V.empty()) { size_t bi = 0; for (size_t i = 0; i < ph.size(); i++) if (rankOf(ph[i]) > rankOf(ph[bi])) bi = i; add(0, bi, ph.size()); return; }   // no vowel: the most sonorous sound is the nucleus
  size_t start = 0;
  for (size_t k = 0; k < V.size(); k++) {
    const size_t v = V[k]; size_t end;
    if (k + 1 >= V.size()) end = ph.size();
    else { const size_t next = V[k + 1], cs = next - v - 1; end = v + 1 + (cs <= 1 ? 0 : 1); if (cs >= 1 && ph[v + 1] == idOf("NG")) end = end > v + 2 ? end : v + 2; }   // one consonant goes to the next syllable; of more, the first stays (NG never starts one)
    add(start, v, end); start = end;
  }
}
}  // namespace

bool VoxLyrics::parse(const std::string& utf8) {
  std::vector<uint32_t> cps, low; decode(utf8, cps); low.reserve(cps.size() + 4); for (uint32_t c : cps) lowerCp(c, low);
  VoxLyrics R; const size_t n = low.size(); size_t i = 0;
  while (i < n) {
    const uint32_t c = low[i];
    if (c == '[') {   // \[[^\]]*\]
      size_t j = i + 1; while (j < n && low[j] != ']') j++;
      if (j >= n) { i++; continue; }
      std::vector<int> ph; std::string cur;
      auto flush = [&]() { if (cur.empty()) return; const int id = phId(cur); if (id >= 0) ph.push_back(id); else if (cur == "-") ph.push_back(kDash); cur.clear(); };   // .filter(x => P[x] || x === '-')
      for (size_t k = i + 1; k < j; k++) { const uint32_t u = low[k]; if (isSpace(u) || u == ',') { flush(); continue; } if (u >= '0' && u <= '9') continue; upperCp(u, cur); }
      flush();
      i = j + 1;
      std::vector<int> g; bool any = false;
      auto emit = [&]() { if (!g.empty()) syllabify(g, R); g.clear(); (void)any; };
      for (int x : ph) { if (x == kDash) emit(); else g.push_back(x); }
      emit();
    } else if (wordCh(c)) {   // [a-z']+(?:-[a-z']+)*
      size_t j = i; while (j < n && wordCh(low[j])) j++;
      while (j + 1 < n && low[j] == '-' && wordCh(low[j + 1])) { j++; while (j < n && wordCh(low[j])) j++; }
      std::vector<int> ph; size_t a = i;
      for (size_t k = i; k <= j; k++) {
        if (k < j && low[k] != '-') continue;
        std::string part; for (size_t m = a; m < k; m++) part.push_back((char)low[m]);   // tk.split('-')
        a = k + 1;
        if (!ph.empty()) ph.push_back(kDash);
        size_t s0 = 0, s1 = part.size(); while (s0 < s1 && part[s0] == '\'') s0++; while (s1 > s0 && part[s1 - 1] == '\'') s1--;   // .replace(/^'+|'+$/g, '')
        const std::string w = part.substr(s0, s1 - s0);
        if (!w.empty()) {
          if (w == "constructor") return false;   // D["constructor"] is Object: `ph.push(...Object)` throws a TypeError
          const VoxDictDef* d = dictFind(w);
          if (d) pushNames(d->p, ph, false); else rulesOf(w, ph);
        }
      }
      i = j;
      std::vector<int> g;
      auto emit = [&]() { if (!g.empty()) syllabify(g, R); g.clear(); };
      for (int x : ph) { if (x == kDash) emit(); else g.push_back(x); }
      emit();
    } else if (punctCh(c)) {   // a rest
      VoxSyl s; s.rest = true; s.nu = -1; s.onB = s.coB = (uint32_t)R.pool.size(); s.onN = s.coN = 0; R.syl.push_back(s); i++;
    } else i++;
  }
  size_t m = 2, mc = 0; for (const VoxSyl& s : R.syl) if (!s.rest) { const size_t q = 3 * (s.onN > s.coN ? s.onN : s.coN) + 2; if (q > m) m = q; if (s.coN > mc) mc = s.coN; }
  R.maxSegs = m; R.maxCo = mc;
  syl = std::move(R.syl); pool = std::move(R.pool); maxSegs = R.maxSegs; maxCo = R.maxCo;
  return true;
}

std::string vox_syllables_text(const std::string& utf8, bool* threw) {
  VoxLyrics L; *threw = !L.parse(utf8); std::string out;
  if (*threw) return out;
  for (const VoxSyl& s : L.syl) {
    if (!out.empty()) out += ";";
    if (s.rest) { out += "R"; continue; }
    for (uint32_t i = 0; i < s.onN; i++) { if (i) out += " "; const int id = L.pool[s.onB + i]; out += id >= 0 ? kVoxPhDefs[id].name : "?"; }
    out += "|"; out += s.nu >= 0 ? kVoxPhDefs[s.nu].name : "?"; out += "|";
    for (uint32_t i = 0; i < s.coN; i++) { if (i) out += " "; const int id = L.pool[s.coB + i]; out += id >= 0 ? kVoxPhDefs[id].name : "?"; }
  }
  return out;
}

// ================================================================ VoxCore ====================================================================================================================
VoxCore::VoxCore(double sr) : sr_(sr) {
  phTab();   // the phoneme table is built here, not on the audio thread
  const double oq = 0.6;
  for (int i = 0; i <= 2048; i++) { const double x = i / 2048.0; glot_[i] = (float)(x < oq ? (2 * x / oq - 3 * (x / oq) * (x / oq)) : 0); }
  double m = 0; for (int i = 0; i <= 2048; i++) m += (double)glot_[i]; m /= 2049;
  double pk = 0; for (int i = 0; i <= 2048; i++) { glot_[i] = (float)((double)glot_[i] - m); pk = mx(pk, jabs((double)glot_[i])); }
  for (int i = 0; i <= 2048; i++) glot_[i] = (float)((double)glot_[i] / pk);
  k_ = Ctl{500, 1500, 2500, 0, 0, 0, 4000, 3000, 0}; tg_ = k_;
  std::memset(r_, 0, sizeof r_);
  for (int j = 0; j < 8; j++) { cph_[j] = std::fmod(j * 0.618, 1.0); cdp_[j] = std::fmod(j * 0.371, 1.0); }
  set(VoxP());
}

bool VoxCore::set(const VoxP& o) {
#define XP_CP(n, d) p_.n = o.n;
  XP_VOX(XP_CP)
#undef XP_CP
  if (p_.txt != o.txt) p_.txt = o.txt;
  if (!txtSet_ || p_.txt != txt_) {
    txt_ = p_.txt; txtSet_ = true;   // (the JS stores the new text before parsing it: a parse that throws leaves the old syllables)
    VoxLyrics L; if (!L.parse(p_.txt)) return false;
    lyr_ = std::move(L); if (segs_.size() < lyr_.maxSegs + 1) segs_.resize(lyr_.maxSegs + 1, Seg{-1, kEnd, 0}); /* (never shrunk or cleared: the segments being played are the JS's `this.segs`, which set() does not touch) */ if (curCo_.size() < lyr_.maxCo + 1) curCo_.resize(lyr_.maxCo + 1);   // (the playing syllable keeps its own copy of its closing phonemes: the JS's `cur` object outlives a new lyric)
  }
  return true;
}

double VoxCore::ms(double x) const { return mx(1, js_round(x / 1000 * sr_ / mx(0.3, p_.cons))); }

void VoxCore::plan(const int16_t* list, uint32_t n, bool coda) {
  const PhTab& T = phTab();
  for (uint32_t idx = 0; idx < n; idx++) {
    const int x = list[idx]; if (x < 0) continue;   // `if (!P) return`
    const Ph& P = T.p[x]; const double d = orv(P.d, 60);
    if (P.t == 'P' || P.t == 'B' || P.t == 'C' || P.t == 'J') {
      const bool first = !coda && idx == 0;
      segs_[nsegs_++] = Seg{x, kClo, ms(first ? 25 : (P.t == 'P' || P.t == 'C' ? 55 : 45))};
      segs_[nsegs_++] = Seg{x, kBurst, ms(P.t == 'P' ? 12 : 8)};
      if (P.t == 'C' || P.t == 'J') segs_[nsegs_++] = Seg{x, kFric, ms(P.t == 'C' ? 95 : 75)};
      else if (P.t == 'P') segs_[nsegs_++] = Seg{x, kAsp, ms(coda ? 20 : 35)};
    } else segs_[nsegs_++] = Seg{x, P.t == 'F' || P.t == 'Z' ? kFric : P.t == 'H' ? kAsp : kSon, ms(coda && P.t == 'N' ? d * 1.35 : d)};
  }
}

bool VoxCore::on(bool leg, double n, bool same) {
  (void)leg;
  go_ = env_ < 0.01;
  if (same && hasCur_ && hold_) return true;
  const size_t len = lyr_.syl.size(); if (!len) return true;
  const double L = (double)len, q = std::fmod(std::fmod(n, L) + L, L);   // ((n % S.length) + S.length) % S.length
  if (!(q == q) || q != std::floor(q) || q < 0 || q >= L) return false;   // S[NaN] or S[1.5] is undefined: `s.rest` throws a TypeError
  const size_t si = (size_t)q; const VoxSyl& s = lyr_.syl[si];
  if (s.rest) { hasCur_ = false; nsegs_ = 0; si_ = 0; hold_ = false; coda_ = false; envT_ = 0; return true; }
  hasCur_ = true; curNu_ = s.nu; curCoN_ = s.coN; for (uint32_t q2 = 0; q2 < s.coN && q2 < curCo_.size(); q2++) curCo_[q2] = lyr_.pool[s.coB + q2]; nsegs_ = 0; plan(lyr_.pool.data() + s.onB, s.onN, false); segs_[nsegs_++] = Seg{s.nu, kHold, -1};
  si_ = 0; st_ = 0; hold_ = false; coda_ = false; envT_ = 1; vt_ = 0; enter();
  return true;
}

void VoxCore::off() {
  if (!hasCur_) { envT_ = 0; return; }
  const PhTab& T = phTab();
  nsegs_ = 0;
  if (curNu_ >= 0 && T.p[curNu_].hasG) segs_[nsegs_++] = Seg{curNu_, kGlide, ms(70)};
  plan(curCo_.data(), curCoN_, true); segs_[nsegs_++] = Seg{-1, kEnd, 0};
  si_ = 0; st_ = 0; hold_ = false; coda_ = true; enter();
}

// The targets for the segment being entered.
void VoxCore::enter() {
  if (si_ >= nsegs_) return;   // `if (!g) return`
  const Seg& g = segs_[si_]; const PhTab& T = phTab(); const int AX = T.AX, SH = T.SH, ZH = T.ZH;
  const Ph& P = g.ph >= 0 ? T.p[g.ph] : T.p[AX]; Ctl& t = tg_;
  // the vowel this consonant leads into (or out of): aspiration takes its shape
  const Ph* vow = hasCur_ ? (curNu_ >= 0 ? &T.p[curNu_] : nullptr) : &T.p[AX]; if (!vow || vow->t != 'V') vow = &T.p[AX];
  const double* f = g.k == kAsp || P.t == 'H' ? (g.k == kAsp && coda_ ? T.p[AX].f : vow->f) : g.k == kGlide ? P.g : P.f;
  t.f1 = f[0]; t.f2 = f[1]; t.f3 = f[2];
  t.av = 0; t.an = 0; t.ah = 0; t.nf = orv(P.nf, 3000); t.nb = orv(P.nb, 3000); t.nz = g.k == kSon && P.t == 'N' ? 1 : 0;
  switch (g.k) {
    case kHold: case kGlide: case kSon: t.av = P.av != P.av ? 1 : P.av; break;
    case kClo: t.av = P.t == 'B' || P.t == 'J' ? 0.12 : 0; break;
    case kBurst: t.an = P.ba == P.ba ? P.ba : P.t == 'P' || P.t == 'C' ? 0.7 : 0.45; t.av = P.t == 'B' || P.t == 'J' ? 0.25 : 0; t.nf = orv(P.bf, 2000); t.nb = orv(P.bb, 2500); break;
    case kFric: { const Ph& F = P.t == 'C' ? T.p[SH] : P.t == 'J' ? T.p[ZH] : P; t.an = orv(F.fa, 0.4); t.av = orv(F.av, 0); t.nf = orv(F.nf, 4000); t.nb = orv(F.nb, 3000); break; }
    case kAsp: t.ah = 0.26; break;
    case kEnd: envT_ = 0; t.av = k_.av; break;
  }
  if (go_) { k_.f1 = t.f1; k_.f2 = t.f2; k_.f3 = t.f3; k_.nf = t.nf; k_.nb = t.nb; go_ = false; }
}

double VoxCore::reso(int j, double x, double F, double B) {   // two-pole resonator j (Klatt): y = A*x + B*y1 + C*y2
  const double T = 1 / sr_, c = -js_exp(-2 * kPi * B * T), bb = 2 * js_exp(-kPi * B * T) * js_cos(2 * kPi * mn(F, sr_ * 0.45) * T), aa = 1 - bb - c;
  const double y = aa * x + bb * r_[j * 2] + c * r_[j * 2 + 1]; r_[j * 2 + 1] = r_[j * 2]; r_[j * 2] = y; return y;
}
double VoxCore::src1(double ph, double inc, double rob) const {   // one period of the source at phase ph: the glottal pulse <-> a band-limited saw (Robot)
  const double x = ph * 2048; const int32_t xi = i32(x);
  auto G = [&](int64_t i) { return (i >= 0 && i < 2049) ? (double)glot_[i] : (double)NAN; };
  const double gl = G(xi) + (G((int64_t)xi + 1) - G(xi)) * (x - xi);
  double saw = 2 * ph - 1; { double t = ph; if (t < inc) { t /= inc; saw -= t + t - t * t - 1; } else if (t > 1 - inc) { t = (t - 1) / inc; saw -= t * t + t + t + 1; } }
  return gl * (1 - rob) + saw * rob * 0.8;
}

void VoxCore::render(float* L, float* R, size_t len, double a, double b, double rate) { for (double i = a; i < b; i += kBlk) block(L, R, len, i, mn(b, i + kBlk), rate); }

void VoxCore::block(float* L, float* R, size_t len, double a, double b, double rate) {
  static const double CDET[8] = {0, -1, 1, -0.55, 0.6, -0.8, 0.3, 0.9};
  const VoxP& p = p_; const double sr = sr_, n = b - a, dt = n / sr;
  auto put = [&](double i, double y) { if (i >= 0 && i < (double)len && i == std::floor(i)) { L[(size_t)i] = (float)y; if (R) R[(size_t)i] = (float)y; } };
  // move through the plan
  const Seg* g = si_ < nsegs_ ? &segs_[si_] : nullptr;
  while (g && g->d >= 0 && st_ >= g->d && (double)si_ < (double)nsegs_ - 1) { si_++; st_ = 0; g = &segs_[si_]; enter(); }
  if (g && g->k == kHold) hold_ = true;
  st_ += n;
  const double tfm = g && (g->k == kHold || g->k == kGlide) ? 24 : g && g->k == kSon ? 16 : 10;
  const double kf = 1 - js_exp(-dt * 1000 / (tfm / mx(0.3, p.cons))), ka = 1 - js_exp(-dt * 1000 / 4); Ctl& K = k_; const Ctl& T = tg_;
  K.f1 += (T.f1 - K.f1) * kf; K.f2 += (T.f2 - K.f2) * kf; K.f3 += (T.f3 - K.f3) * kf;
  // frication fades in over ~8 ms and out over ~12 ms; P T K bursts stay sharp
  const double kn1 = 1 - js_exp(-dt * 1000 / 8), kn2 = 1 - js_exp(-dt * 1000 / 12);
  K.av += (T.av - K.av) * ka; K.an += (T.an - K.an) * (g && g->k == kBurst ? ka : T.an > K.an ? kn1 : kn2); K.ah += (T.ah - K.ah) * ka; K.nf += (T.nf - K.nf) * ka; K.nb += (T.nb - K.nb) * ka; K.nz += (T.nz - K.nz) * ka;
  // amplitude envelope
  const double er = envT_ > env_ ? 1 - js_exp(-dt * 1000 / mx(0.5, p.atk)) : 1 - js_exp(-dt * 1000 * 4.6 / mx(1, p.rel));
  env_ += (envT_ - env_) * er; if (envT_ == 0 && env_ < 1e-4) env_ = 0;
  // pitch: C4 at C-4, with glide and vibrato (which fades in after Vibrato delay)
  const double target = 261.626 * js_pow(2, p.oct + p.tune / 12) * rate, lt = js_log2(mx(20, target));
  if (go_ || lf_ <= 0) lf_ = lt;
  if (p.glide > 0.5) lf_ += (lt - lf_) * (1 - js_exp(-dt * 1000 / p.glide)); else lf_ = lt;
  vt_ += dt; const double vd = mn(1, mx(0, (vt_ * 1000 - p.vibDelay) / 300));
  const double hs = mx(0, p.hiss) * 2, cl = p.clar;
  const double vc = mx(0, mn(1, p.voice)), fs = js_pow(2, p.form * 0.35), bwm = (0.6 + p.bw * 0.9) * (1.3 - cl * 0.6), rob = p.robot, br = p.breath, lv = js_pow(10, orz(p.level) / 20) * 0.32;
  const double kb = 1 - js_exp(-2 * kPi * (1200 * js_pow(10, p.bright)) / sr), dr = orz(p.drive) * 6, dn = truthy(dr) ? 1 / js_tanh(dr) : 1;
  const double ru = mx(0, mn(1, orz(p.rough))), nchD = mx(1, mn(8, js_round(orv(p.choir, 1)))), sp = mx(0, p.spread) * 30, cg = 1 / std::sqrt(nchD);
  const int nch = (int)nchD;   // 1 to 8 (a NaN count is not reachable: choir || 1)
  const double F1 = K.f1 * fs, F2 = K.f2 * fs, F3 = K.f3 * fs, F4 = 3350 * fs, F5 = 3850 * fs, B1 = 60 * bwm, B2 = 90 * bwm, B3 = 150 * bwm;
  const double nfz = K.nf * std::sqrt(fs), nbz = K.nb, nas = K.nz, knl = 1 - js_exp(-2 * kPi * 650 * std::sqrt(fs) / sr);
  if (env_ == 0 && envT_ == 0) { for (double i = a; i < b; i++) { put(i, 0); } return; }
  for (double i = a; i < b; i++) {
    vph_ += p.vibRate / sr; if (vph_ >= 1) vph_ -= 1;
    const double f0 = js_pow(2, lf_ + vd * p.vib * 0.06 * js_sin(2 * kPi * vph_)) * (1 + jt_);
    const double inc = f0 / sr; ph_ += inc;
    if (ph_ >= 1) { ph_ -= 1;
      if (ru > 0) { const uint32_t rn = (uint32_t)rnd_ * 1664525u + 1013904223u; rnd_ = (double)rn; const double u = rnd_ / 4294967296.0;
        jt_ = (u - 0.5) * ru * 0.045; shm_ = 1 - ru * 0.35 * std::fmod(u * 7.13, 1.0); odd_ ^= 1; } else { jt_ = 0; shm_ = 1; } }
    // the source: a smooth glottal pulse <-> a buzzy saw (Robot), tilted by Bright
    double src = src1(ph_, inc, rob) * shm_ * (odd_ && ru > 0.3 ? 1 - (ru - 0.3) * 0.9 : 1) * (1 + ru * 0.6);
    for (int j = 1; j < nch; j++) {
      cdp_[j] += (0.11 + j * 0.037) / sr; if (cdp_[j] >= 1) cdp_[j] -= 1;
      const double det = js_pow(2, (CDET[j] + 0.35 * js_sin(2 * kPi * cdp_[j])) * sp / 1200), ci = inc * det;
      cph_[j] += ci; if (cph_[j] >= 1) cph_[j] -= 1;
      src += src1(cph_[j], ci, rob);
    }
    if (nch > 1) src *= cg;
    lp_ += (src - lp_) * kb; src = lp_;
    { const uint32_t rn = (uint32_t)rnd_ * 1664525u + 1013904223u; rnd_ = (double)rn; }
    const double nz = rnd_ / 2147483648.0 - 1;
    const double open = ph_ < 0.6 ? 1 : 0.35;   // breath is louder while the glottis is open
    an1_ += (nz - an1_) * 0.45; const double anz = an1_ * 1.3;
    const double voiced = src * K.av * vc + (anz * K.ah * hs + nz * br * K.av * (0.5 + (1 - vc)) * open) * 0.6;   // Voice 0 = a whisper
    double y = reso(0, voiced, F1, B1); y = reso(1, y, F2, B2); y = reso(2, y, F3, B3); y = reso(3, y, F4, 250); y = reso(4, y, F5, 300);
    // M, N, NG: the mouth is closed and the sound comes out of the nose
    nl_ += (y - nl_) * knl; nl2_ += (nl_ - nl2_) * knl; if (nas > 0.001) y += (nl2_ * 1.6 - y) * nas;
    // frication: noise through its own resonance, in parallel
    const double fr = reso(5, nz * K.an * hs, nfz, nbz) * 1.6;
    const double pres = y - yp_; yp_ = y;
    double out = y * 0.5 + pres * cl * 0.9 + fr;
    const double o = out - dcx_ + 0.995 * dc_; dcx_ = out; dc_ = o; out = o * env_ * lv;
    if (truthy(dr)) out = js_tanh(out * dr) * dn;
    put(i, out);
  }
}

// ================================================================ renderVox ==================================================================================================================
// The pad's sound: the whole lyric sung at the pad's pitch, one syllable every Pad step. `const cap = sr * 30, L = new Float32Array(cap)`: a typed array's length is the truncated number (NaN gives 0), a negative one throws:
// refused, as the empty result is. out holds L (R is not read by anything: not kept).
size_t xeng_vox(const VoxP& P, double sr, float* out, size_t cap) {
  if (!(sr > 0 && sr <= 1e7)) return kRefuse;
  const double capD = sr * 30, tC = capD == capD ? std::trunc(capD) : 0;
  if (!(tC >= 1 && tC <= 16777216.0)) return kRefuse;
  VoxCore c(sr);   // (allocates: the lyrics; offline only)
  if (!c.set(P)) return kRefuse;   // `c.set(...)` threw
  const VoxP& p = c.p(); const double n = mx(1, (double)c.nSyl()), hold = mx(20, js_round(p.sylLen / 1000 * sr));
  const size_t len = (size_t)tC;
  if (!out) return len;
  if (cap < len) return 0;
  std::memset(out, 0, len * sizeof(float));
  double pos = 0;
  for (double k = 0; k < n && pos < capD - sr; k++) {
    if (!c.on(k > 0, k, false)) return kRefuse;
    c.render(out, nullptr, len, pos, mn(capD, pos + hold), 1); pos = mn(capD, pos + hold);
    c.off(); double g = 0;
    while (c.busy() && g < sr && pos < capD) { const double e = mn(capD, pos + 64); c.render(out, nullptr, len, pos, e, 1); pos = e; g += 64; }
  }
  const double tail = mn(capD, pos + js_round((p.rel / 1000 * 1.6 + 0.03) * sr)); c.render(out, nullptr, len, pos, tail, 1); pos = tail;
  // const out = L.slice(0, Math.max(1, pos)); trim; fade
  if (pos != pos) return kRefuse;   // a NaN release: `L.slice(0, NaN)` is empty and the JS returns an empty sound (refused, as the header says); the cast below would be UB
  const double sl = mx(1, pos); const size_t olen = sl >= (double)len ? len : (size_t)std::trunc(sl);   // slice(0, end): the end is truncated and clamped to the length
  return trim_fade(out, olen, sr, 0.002);
}

}  // namespace thunder
