#include "song/song.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>

#include "rt/num.h"
#include "rt/dbl.h"

namespace thunder {

namespace {

// ---- JavaScript's number coercions, as index.html's normSong uses them ---------------------------------------------------
// +v for a JSON value: number as is, true 1, false 0, null 0, a numeric string by Number()'s rules; anything else NaN.
// (JS also turns [] into 0 and [5] into 5; arrays and objects give NaN here.)
const double kNaN = std::nan("");

double stringToNumber(std::string_view s) {
  size_t a = 0, b = s.size();
  auto ws = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; };
  while (a < b && ws(s[a])) a++;
  while (b > a && ws(s[b - 1])) b--;
  if (a == b) return 0;
  std::string t(s.substr(a, b - a));
  if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
    double v = 0; for (size_t i = 2; i < t.size(); i++) { char c = t[i]; int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; if (h < 0) return kNaN; v = v * 16 + h; }
    return v;
  }
  size_t i = 0; if (t[i] == '+' || t[i] == '-') i++;
  size_t d0 = i, digs = 0;
  while (i < t.size() && t[i] >= '0' && t[i] <= '9') { i++; digs++; }
  if (i < t.size() && t[i] == '.') { i++; while (i < t.size() && t[i] >= '0' && t[i] <= '9') { i++; digs++; } }
  if (!digs) return kNaN;
  (void)d0;
  if (i < t.size() && (t[i] == 'e' || t[i] == 'E')) { i++; if (i < t.size() && (t[i] == '+' || t[i] == '-')) i++; size_t e0 = i; while (i < t.size() && t[i] >= '0' && t[i] <= '9') i++; if (i == e0) return kNaN; }
  if (i != t.size()) return kNaN;
  { double v = 0; parse_double(t, &v); return v; }   // checked above; locale-free
}

double jsPlus(Json v) {
  switch (v.type()) {
    case JType::Number: return v.asNum();
    case JType::Bool: return v.asBool() ? 1 : 0;
    case JType::Null: return 0;
    case JType::String: return stringToNumber(v.asStr());
    default: return kNaN;
  }
}

// index.html: const int = (v, d) => Number.isFinite(+v) && v !== null && v !== '' ? Math.round(+v) : d
double jsInt(Json v, double d) {
  if (v.isNull() || v.missing()) return d;
  if (v.isStr() && v.asStr().empty()) return d;
  const double x = jsPlus(v);
  if (!std::isfinite(x)) return d;
  return std::floor(x + 0.5);   // Math.round
}
int clampI(double v, int lo, int hi) { return to_int_sat(v, lo, hi); }   // (NaN gives lo: the cast of NaN is undefined; N1 C3)
int cInt(Json v, int d, int lo, int hi) { return clampI(jsInt(v, d), lo, hi); }
bool truthy(Json v) {
  switch (v.type()) {
    case JType::Bool: return v.asBool();
    case JType::Number: { double x = v.asNum(); return x != 0 && x == x; }
    case JType::String: return !v.asStr().empty();
    case JType::Array: case JType::Object: return true;
    default: return false;
  }
}
int32_t toInt32(double x) {   // x | 0
  if (!std::isfinite(x)) return 0;
  double t = std::trunc(x); t = std::fmod(t, 4294967296.0); if (t < 0) t += 4294967296.0;
  return (int32_t)(uint32_t)t;
}

const double kLfoRates[] = {1.0 / 6, 0.25, 1.0 / 3, 0.5, 2.0 / 3, 0.75, 1, 4.0 / 3, 1.5, 2, 4, 8, 16, 32};
const double kEchoRates[] = {1.0 / 8, 1.0 / 6, 0.25, 1.0 / 3, 0.5, 2.0 / 3, 0.75, 1, 1.5, 2, 4};
const int kCcDef[4] = {74, 71, 1, 7};
constexpr int kNPad = 8, kMaxSlot = 64, kNPort = 4, kLfoShapes = 6, kTcol = 13, kScales = 15, kTicksDefault = 6, kTicIndex = 27, kNSlotDefault = 16;

template <size_t N> bool inRates(const double (&R)[N], double x) { for (double r : R) if (std::fabs(r - x) < 1e-9) return true; return false; }

Json get(Json o, const char* k) { return o.get(k); }

static double sxNum(Json v, double a, double b, double d, bool isInt) {
  double x = (v.missing() || v.isNull() || (v.isStr() && v.asStr().empty()) || (v.type() == JType::Bool && !v.asBool())) ? kNaN : jsPlus(v);
  if (!std::isfinite(x)) x = d;
  x = std::min(b, std::max(a, x));
  return isInt ? std::floor(x + 0.5) : x;
}
void readInst(Json o, int i, SongInst& I) {
  I = SongInst();
  I.pad = i % kNSlotDefault; I.kf = (i % kNSlotDefault) >= kNPad ? 1 : 0;
  for (int k = 0; k < 4; k++) I.cc[k] = kCcDef[k];
  if (!o.isObj()) return;   // freshInst(i)
  I.pad = cInt(get(o, "pad"), I.pad, 0, kMaxSlot - 1); I.tsp = cInt(get(o, "tsp"), 0, -48, 48); I.vol = cInt(get(o, "vol"), 255, 0, 255);
  I.pan = cInt(get(o, "pan"), 128, 0, 255); I.rev = truthy(get(o, "rev")) ? 1 : 0; I.ofs = cInt(get(o, "ofs"), 0, 0, 255);
  I.slc = cInt(get(o, "slc"), 0, 0, 64); I.smode = truthy(get(o, "smode")) ? 1 : 0; I.sync = cInt(get(o, "sync"), 0, 0, 3);
  I.beats = cInt(get(o, "beats"), 4, 1, 64); I.grain = cInt(get(o, "grain"), 1, 0, 4);
  { const int ln = cInt(get(o, "lines"), 0, -(1 << 30), 1 << 30); I.lines = ln > 0 ? clampI(ln, 1, 1024) : 0; }   // index.html:21677: `int(o.lines, 0) > 0 ? clamp(.., 1, 1024) : delete`
  { const SongSx x = song_sx_load(o, I.sync); I.sx = x.sx; I.og = x.og; I.ou = x.ou; I.orp = x.orp; I.ox = x.ox; I.opt = x.opt; }   // index.html:21678 sxLoad
  I.mo = cInt(get(o, "mo"), 0, 0, 2); I.mch = cInt(get(o, "mch"), 0, 0, 15);
  I.mpt = cInt(get(o, "mpt"), 0, 0, kNPort - 1); I.pbr = cInt(get(o, "pbr"), 2, 1, 48);
  const Json cc = get(o, "cc");
  for (int k = 0; k < 4; k++) I.cc[k] = cInt(cc.isArr() ? cc.at((size_t)k) : Json(), kCcDef[k], 0, 127);
  I.lcc = cInt(get(o, "lcc"), -1, -1, 127); I.lsh = cInt(get(o, "lsh"), 0, 0, kLfoShapes - 1);
  { const double x = jsPlus(get(o, "lrb")); I.lrb = inRates(kLfoRates, x) ? x : 4; }
  I.ldp = cInt(get(o, "ldp"), 64, 0, 127); I.lct = cInt(get(o, "lct"), 64, 0, 127); I.lrg = truthy(get(o, "lrg")) ? 1 : 0;
  I.mrng = cInt(get(o, "mrng"), 64, 0, 255); I.gtype = cInt(get(o, "gtype"), 0, 0, 12); I.gcen = cInt(get(o, "gcen"), 0, 0, 255);
  { const Json kf = get(o, "kf"); I.kf = (!kf.missing() && !kf.isNull()) ? (truthy(kf) ? 1 : 0) : ((I.mo | 0) || I.pad >= kNPad ? 1 : 0); }
  I.hut = cInt(get(o, "hut"), 0, 0, 50); I.huv = cInt(get(o, "huv"), 0, 0, 90);
  I.mlen = cInt(get(o, "mlen"), 0, 0, 255); I.eco = cInt(get(o, "eco"), 0, 0, 16);
  { const double x = jsPlus(get(o, "ecr")); I.ecr = inRates(kEchoRates, x) ? x : 0.5; }
  I.ecf = cInt(get(o, "ecf"), 30, 0, 100); I.ecp = cInt(get(o, "ecp"), 0, -12, 12);
}

}  // namespace
double song_js_plus(Json v) { return jsPlus(v); }

// index.html sxLoad (:23642): I.sx = Math.trunc(+o.sx) kept only for a (sync, sx) pair the picker has (SY_PICK: only OLDSCHOOL = sync 3, sx 1); with sx 1 the OLDSCHOOL values are clamped
// (n(v, a, b, d, int): missing / null / '' / false = the default, a non-finite number = the default, clamp, then Math.round for the integer ones), else they are deleted (here: defaults).
SongSx song_sx_load(Json o, int sync) {
  SongSx r;
  const double t = o.isObj() ? std::trunc(jsPlus(get(o, "sx"))) : kNaN;
  if (!(t > 0) || !(t == 1 && sync == 3)) return r;
  r.sx = 1; r.og = sxNum(get(o, "og"), 0.5, 250, 12, false); r.ou = (int)sxNum(get(o, "ou"), 0, 6, 0, true); r.orp = (int)sxNum(get(o, "orp"), 0, 3, 0, true);
  r.ox = (int)sxNum(get(o, "ox"), 0, 50, 0, true); r.opt = (int)sxNum(get(o, "opt"), -24, 24, 0, true);
  return r;
}


int Song::phrasesPresent() const { int n = 0; for (int i = 0; i < kNPH; i++) if (phraseOff[(size_t)i] >= 0) n++; return n; }
int Song::phrasesWithContent() const {
  int n = 0;
  for (int i = 0; i < kNPH; i++) {
    const int16_t* p = phrase(i); if (!p) continue;
    for (int k = 0; k < kPhraseRows * kRW; k++) if (p[k] >= 0) { n++; break; }
  }
  return n;
}

bool Song::load(const char* text, size_t len, JsonError* err, bool pageS) {
  if (!doc.parse(text, len, err)) return false;
  root = doc.root();
  auto bad = [&](const char* m) { if (err) { err->offset = 0; err->msg = m; } return false; };
  // Which shape: a message {"t":..,"rev":N,"S":{..}}, a bare S, or a file {app:'thunder-project', song:{..}, kit:{..}} (alone or as S).
  if (!root.isObj()) return bad("not an object");
  Json cur = root, fileObj;
  rev = to_i64_sat(root.num("rev", 0), -(1LL << 53), 1LL << 53);   // (N1 C3: a JS safe integer at most; NaN and huge values no longer reach the cast)
  double bld = root.num("build", 0);
  { Json s = cur.get("S"); if (s.isObj()) cur = s; }
  if (cur.get("song").isObj()) { fileObj = cur; cur = cur.get("song"); isFile = true; if (fileObj.get("build").isNum()) bld = fileObj.num("build", 0); }
  else if (cur.get("build").isNum() && bld == 0) bld = cur.num("build", 0);
  S = cur; kit = isFile ? fileObj.get("kit") : root.get("kit");
  build = to_int_sat(bld, 0, 1 << 30); newerBuild = build > kKnownBuild;

  v = cInt(S.get("v"), 1, -0x7fffffff, 0x7fffffff);
  const bool v1 = !(jsInt(S.get("v"), 1) >= 2);
  const bool old1 = !(jsInt(S.get("v"), 1) >= 3);
  legacyFx = v1;

  bpm = cInt(S.get("bpm"), 120, 40, 300);
  swing = cInt(S.get("swing"), 50, 50, 75); swingB = cInt(S.get("swingB"), 2, 1, 7);
  limiter = !(S.get("limiter").isBool() && !S.get("limiter").asBool());
  renderFrom = cInt(S.get("renderFrom"), 0, 0, 255); master = cInt(S.get("master"), 255, 0, 255); mout = cInt(S.get("mout"), 255, 0, 255);

  // the arrangement
  song.assign((size_t)kSongRows * kSongCols, -1);
  { const Json J = S.get("song");
    if (J.isArr()) for (size_t i = 0; i < J.size() && i < (size_t)kSongRows; i++) { const Json r = J.at(i); if (!r.isArr()) continue;
      for (int t = 0; t <= kSongMT; t++) song[i * kSongCols + (size_t)t] = (int16_t)cInt(r.at((size_t)t), -1, -1, kNPH - 1); } }   // (rows of 33 numbers: NT = 32)

  chainOff.assign(kNPH, -1); chainData.clear();
  { const Json J = S.get("chains");
    if (J.isArr()) for (size_t i = 0; i < J.size() && i < (size_t)kNPH; i++) { const Json c = J.at(i); if (!c.isArr()) continue;
      chainOff[i] = (int32_t)chainData.size();
      for (int k = 0; k < kChainRows; k++) { const Json r = c.at((size_t)k).isArr() ? c.at((size_t)k) : Json();
        chainData.push_back((int16_t)cInt(r.at(0), -1, -1, kNPH - 1)); chainData.push_back((int16_t)cInt(r.at(1), 0, -128, 127)); } } }

  lpb = cInt(S.get("lpb"), 4, 1, 16); ticks = cInt(S.get("ticks"), kTicksDefault, 1, 32); dlen = cInt(S.get("dlen"), 16, 1, kPhraseRows);
  plen.assign(kNPH, (int16_t)dlen); plpb.assign(kNPH, 0);
  { const Json J = S.get("plen"); if (J.isArr()) for (size_t i = 0; i < J.size() && i < (size_t)kNPH; i++) plen[i] = (int16_t)cInt(J.at(i), dlen, 1, kPhraseRows); }
  { const Json J = S.get("plpb"); if (J.isArr()) for (size_t i = 0; i < J.size() && i < (size_t)kNPH; i++) plpb[i] = (int16_t)cInt(J.at(i), 0, 0, 16); }

  phraseOff.assign(kNPH, -1); phraseData.clear();
  { const Json J = S.get("phrases");
    if (J.isArr()) for (size_t i = 0; i < J.size() && i < (size_t)kNPH; i++) { const Json p = J.at(i); if (!p.isArr()) continue;
      phraseOff[i] = (int32_t)phraseData.size();
      for (int k = 0; k < kPhraseRows; k++) {
        const Json r = p.at((size_t)k).isArr() ? p.at((size_t)k) : Json();
        int16_t o[kRW];
        if (old1) { int q[9]; for (int m = 0; m < 9; m++) q[m] = cInt(r.at((size_t)m), -1, -1, 255);
          o[0] = (int16_t)q[0]; o[1] = o[2] = o[3] = -1; for (int m = 1; m < 9; m++) o[3 + m] = (int16_t)q[m]; }
        else for (int m = 0; m < kRW; m++) o[m] = (int16_t)cInt(r.at((size_t)m), -1, -1, 255);
        for (int m = kFX0; m < kRW; m += 2) {   // fxIdx for a current song (v >= 2): a command past FXC is dropped
          int c = o[m], val = o[m + 1];
          if (c >= 0 && c >= kFxcCount) { c = -1; val = -1; }
          o[m] = (int16_t)c; o[m + 1] = (int16_t)(c < 0 ? -1 : val);
        }
        for (int m = 0; m < kRW; m++) phraseData.push_back(o[m]);
      } } }
  // phrases with nothing in them take the project's default length
  for (int i = 0; i < kNPH; i++) {
    const int16_t* p = phrase(i); bool empty = true;
    if (p) for (int k = 0; k < kPhraseRows * kRW; k++) if (p[k] >= 0) { empty = false; break; }
    if (empty && !pageS) plen[(size_t)i] = (int16_t)dlen;   // normSong's, at LOAD only; pageS (the page's own S after an edit): the length the user gave an empty phrase stays (edit-plen)
  }

  // instruments, mixer
  { const Json J = S.get("insts"); for (int i = 0; i < kNI; i++) { readInst(J.isArr() ? J.at((size_t)i) : Json(), i, insts[i]); instMissing[i] = pageS && J.isArr() && !J.at((size_t)i).isObj(); } }
  { const Json J = S.get("mix");
    for (int i = 0; i < kSongNT; i++) { SongMix m; const Json o = J.isArr() ? J.at((size_t)i) : Json();
      if (o.isObj()) { m.vol = cInt(o.get("vol"), 224, 0, 255); m.trim = cInt(o.get("trim"), 128, 0, 255); m.pan = cInt(o.get("pan"), 128, 0, 255);
        m.mute = truthy(o.get("mute")); m.solo = truthy(o.get("solo")); m.col = cInt(o.get("col"), 0, 0, kTcol - 1); }
      mix[i] = m; } }
  { const Json J = S.get("chainOn"); for (int t = 0; t < kSongNT; t++) chainOn[t] = J.isArr() && truthy(J.at((size_t)t)); }
  mstPump = truthy(S.get("mstPump"));
  smute.assign(kSongRows, 0);
  { const Json J = S.get("smute"); if (J.isArr()) for (size_t i = 0; i < J.size() && i < (size_t)kSongRows; i++) smute[i] = toInt32(jsInt(J.at(i), 0)); }

  ntr = cInt(S.get("ntr"), 16, 8, kSongNT);   // the page caps at 24 with a voice per track (its vtrk); that arrives with the page's vars
  { // normSplit: nmono is kept only when it differs from the half rule
    nmono = 0; hmono = -1;
    auto num = [](Json x, bool* ok) { *ok = false; if (x.isNull() || x.missing()) return 0.0; if (x.isStr() && x.asStr().empty()) return 0.0; double d = jsPlus(x); if (!std::isfinite(d)) return 0.0; *ok = true; return std::floor(d + 0.5); };
    bool okm, okh; const double m = num(S.get("nmono"), &okm), h = num(S.get("hmono"), &okh);
    if (okm) { const int c = clampI(m, 1, ntr - 1); if (c != (ntr + 1) / 2) nmono = c; }
    if (okh) hmono = clampI(h, 0, kSongNT - ntr);
  }
  { const Json J = S.get("tune"); for (int k = 0; k < 12; k++) { double x = J.isArr() ? jsPlus(J.at((size_t)k)) : kNaN; if (!std::isfinite(x)) x = 0; tune[k] = x < -100 ? -100 : x > 100 ? 100 : x; } }
  ni = cInt(S.get("ni"), 16, 16, kNI);

  // songs from before v6 played every row for SWING NOTE's number of lines: convert as normSong does
  const int swF = jsInt(S.get("v"), 1) < 6 ? clampI(swingB ? swingB : 2, 1, 7) : 1;   // swingB is 1..7 here, so `|| 2` never applies
  if (swF > 1) {
    const double lp = (double)lpb / swF; const bool integral = lp == std::floor(lp);
    if (integral) lpb = (int)lp; else bpm = clampI(std::floor((double)bpm / swF + 0.5), 40, 300);
    const int div = integral ? swF : 1;
    for (int i = 0; i < kNPH; i++) if (plpb[(size_t)i]) plpb[(size_t)i] = (int16_t)clampI(std::floor((double)plpb[(size_t)i] / div + 0.5), 1, 16);
    ticks = clampI((double)(ticks ? ticks : kTicksDefault) * swF, 1, 32);
    for (int i = 0; i < kNPH; i++) { int16_t* p = phraseOff[(size_t)i] >= 0 ? phraseData.data() + phraseOff[(size_t)i] : nullptr; if (!p) continue;
      for (int k = 0; k < kPhraseRows; k++) for (int m = kFX0; m < kRW; m += 2) { int16_t* r = p + k * kRW; if (r[m] == kTicIndex && r[m + 1] > 0) r[m + 1] = (int16_t)clampI((double)r[m + 1] * swF, 1, 32); } }
  }
  scale = cInt(S.get("scale"), 0, 0, kScales - 1); sroot = cInt(S.get("sroot"), 0, 0, 11);

  tfx = S.get("tfx"); mfx = S.get("mfx"); mst = S.get("mst"); perf = S.get("perf"); parts = S.get("parts"); presets = S.get("presets"); irs = S.get("irs");
  { const Json cp = S.get("chainPatch"); chainPatch = Json();
    if (cp.isObj()) { const std::string_view f = cp.str("format"); if (f == "chains.patch" || f == "waves.patch") chainPatch = cp; } }
  return true;
}

}  // namespace thunder
