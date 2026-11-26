#include "bridge_translator.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>

#include "rt/json.h"

namespace tpd {

using thunder::Json;

namespace {

const std::map<std::string, int>& mixKeys() { static const std::map<std::string, int> m = {{"vol", 0}, {"trim", 1}, {"pan", 2}, {"mute", 3}, {"solo", 4}, {"out", 6}}; return m; }

const std::map<std::string, std::string>& unsupported() {
  static const std::string kNotes = "notes from the host are not in the live API (live.h NOT IN THIS API)";
  static const std::string kDev = "audio devices and Settings > Audio are H7 (not in the H6 shell skeleton)";
  static const std::map<std::string, std::string> m = {
    {"song.patch", "the live host takes the whole song: send song.load"}, {"kit.patch", "the live host takes the whole kit: send kit.load"},
    {"note.on", kNotes}, {"note.off", kNotes}, {"dev.list", kDev}, {"dev.open", kDev},
    {"rec.arm", "recording is not on the live host"}, {"rec.start", "recording is not on the live host"}, {"rec.stop", "recording is not on the live host"},
    {"render.start", "renders do not go through the live host"}, {"render.cancel", "renders do not go through the live host"}, {"link.set", "Link is not on the live host"}};
  return m;
}

int64_t atOf(Json j) { Json a = j.get("at"); return a.isNum() ? (int64_t)a.asNum() : kNow; }
int ival(Json j, const char* k, int def) { Json v = j.get(k); return v.isNum() ? (int)v.asNum() : def; }
bool has(Json j, const char* k) { Json v = j.get(k); return !v.missing() && !v.isNull(); }
// a number under the first of `keys` that is one
bool numOf(Json j, double* out, const char* a, const char* b = nullptr) {
  for (const char* k : {a, b}) { if (!k) continue; Json v = j.get(k); if (v.isNum() && !std::isnan(v.asNum())) { *out = v.asNum(); return true; } }
  return false;
}
// the page's track: 0..32 or "m" (the master, 32)
bool trackOf(Json v, int* out) {
  if (v.isNum()) { const int t = (int)v.asNum(); if (t < 0 || t > 32) return false; *out = t; return true; }
  if (v.isStr() && v.asStr() == "m") { *out = 32; return true; }
  return false;
}
Translation refused(const std::string& where, const std::string& msg, int id) { Translation t; t.kind = Translation::kRefused; t.where = where; t.msg = msg; t.replyId = id; return t; }
Translation callOf(Call c) { Translation t; t.kind = Translation::kCall; t.call = std::move(c); return t; }
std::string text(Json v) { return thunder::json_write(v); }

}  // namespace

std::string typeOf(const std::string& raw) {
  const std::string head = raw.substr(0, 256);
  size_t p = 0;
  while ((p = head.find("\"t\"", p)) != std::string::npos) {
    size_t i = p + 3;
    while (i < head.size() && (head[i] == ' ' || head[i] == '\t' || head[i] == '\n' || head[i] == '\r')) i++;
    if (i < head.size() && head[i] == ':') {
      i++;
      while (i < head.size() && (head[i] == ' ' || head[i] == '\t' || head[i] == '\n' || head[i] == '\r')) i++;
      if (i < head.size() && head[i] == '"') { const size_t e = head.find('"', i + 1); if (e != std::string::npos) return head.substr(i + 1, e - i - 1); }
    }
    p += 3;
  }
  return "";
}

static bool tailNumber(const std::string& raw, const char* key, double* out) {
  const std::string tail = raw.size() > 200 ? raw.substr(raw.size() - 200) : raw;
  const std::string k = std::string("\"") + key + "\"";
  size_t at = std::string::npos, p = 0;
  while ((p = tail.find(k, p)) != std::string::npos) {
    size_t i = p + k.size();
    while (i < tail.size() && tail[i] == ' ') i++;
    if (i < tail.size() && tail[i] == ':') {
      i++; while (i < tail.size() && tail[i] == ' ') i++;
      const char* s = tail.c_str() + i; char* e = nullptr; const double v = std::strtod(s, &e);
      if (e != s) { at = i; *out = v; }
    }
    p += k.size();
  }
  return at != std::string::npos;
}
int idOfTail(const std::string& raw) { double v; return tailNumber(raw, "id", &v) && v >= 0 ? (int)v : -1; }
int64_t revOfTail(const std::string& raw) { double v; return tailNumber(raw, "rev", &v) ? (int64_t)v : 0; }

std::vector<std::string> engineList(bool hasLxr) {
  std::vector<std::string> v = {"layers", "tone", "modal", "swarm", "drift", "piano", "ep", "organ", "breath", "strings", "fmdrum", "wind", "phase", "partials", "skin", "chip", "table", "vector",
                                "scanned", "resynth", "bytebeat", "acid", "mach", "blip", "bass", "vox"};
  if (hasLxr) for (const char* s : {"lxdrum", "lxsnare", "lxcym", "lxhat"}) v.push_back(s);
  return v;
}

Translation translate(const std::string& raw) {
  const std::string t = typeOf(raw);
  Translation ign; ign.kind = Translation::kIgnored; ign.type = t;
  if (t.empty()) return ign;
  if (t == "song.load") { Call c; c.op = Op::SongLoad; c.raw = raw; return callOf(std::move(c)); }
  if (t == "kit.load") { Call c; c.op = Op::KitLoad; c.raw = raw; return callOf(std::move(c)); }
  thunder::JsonDoc doc;
  if (!doc.parse(raw) || !doc.root().isObj()) return refused(t, "the message is not JSON", -1);
  const Json j = doc.root();
  const int id = ival(j, "id", -1);
  auto un = unsupported().find(t);
  if (un != unsupported().end()) return refused(t, un->second, id);
  auto refuse = [&](const std::string& m) { return refused(t, m, id); };
  Call c;
  if (t == "hello") { c.op = Op::Hello; c.id = id; c.live = j.boolean("live", false); return callOf(std::move(c)); }
  if (t == "play") {
    c.op = Op::Play;
    Json m = j.get("mode");
    if (m.isNum()) c.mode = (int)m.asNum();
    else if (m.isStr()) { auto s = m.asStr(); if (s == "sect") c.mode = 1; else if (s == "phrase") c.mode = 2; else if (s == "song") c.mode = 0; else return refuse("unknown play mode " + std::string(s)); }
    else if (m.missing() || m.isNull()) c.mode = 0;
    else return refuse("unknown play mode");
    if (c.mode < 0 || c.mode > 2) return refuse("play mode out of range");
    Json loop = j.obj("loop");
    Json only = j.arr("only");
    if (!only.missing()) { c.hasOnly = 1; for (size_t i = 0; i < only.size(); i++) { Json e = only.at(i); const int tr = e.isNum() ? (int)e.asNum() : -1; if (tr >= 0 && tr <= 31) c.onlyBits |= (uint64_t)1 << tr; } }
    c.pdc = ival(j, "pdc", 1);
    if (c.pdc < 0 || c.pdc > 2) return refuse("pdc must be 0, 1 or 2");
    c.row = ival(j, "row", 0); c.track = ival(j, "track", 0); c.sect = ival(j, "sect", -1); c.phrase = ival(j, "phrase", -1);
    if (!loop.missing()) { c.hasLoop = 1; c.y0 = ival(loop, "y0", 0); c.y1 = ival(loop, "y1", 0); }
    c.at = atOf(j);
    if (has(j, "seed")) { c.hasSeed = 1; c.seed = (int)(int64_t)j.num("seed", 0); }
    return callOf(std::move(c));
  }
  if (t == "lxp") { c.op = Op::Lxp; c.st = ival(j, "st", 0); return callOf(std::move(c)); }
  if (t == "stop") { c.op = Op::Stop; c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "hold") { c.op = Op::Hold; c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "loop") { c.op = Op::Loop; c.y0 = ival(j, "y0", 0); c.y1 = ival(j, "y1", 0); c.hasLoop = j.boolean("has", !j.get("y0").missing()) ? 1 : 0; c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "bpm") { c.op = Op::Bpm; if (!numOf(j, &c.v, "bpm", "v")) return refuse("bpm without a number"); c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "lead") { c.op = Op::Lead; if (!numOf(j, &c.v, "s", "v")) return refuse("lead without a number"); c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "perf") { c.op = Op::Perf; c.what = ival(j, "what", -1); c.index = ival(j, "i", 0); if (!numOf(j, &c.v, "v")) return refuse("perf without v"); c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "mix.set") {
    c.op = Op::MixSet;
    if (!trackOf(j.get("tr"), &c.track)) return refuse("mix.set: tr must be 0..31 or 'm'");
    const std::string k(j.str("k"));
    if (k == "send") return refuse("mix.set send has no live call (the page folds sends into the song text)");
    auto mk = mixKeys().find(k);
    if (mk == mixKeys().end()) return refuse("mix.set: unknown key '" + k + "'");
    c.key = mk->second;
    if (!numOf(j, &c.v, "v")) return refuse("mix.set without v");
    double r; c.ramp = numOf(j, &r, "ramp") ? r : -1.0;
    c.at = atOf(j);
    return callOf(std::move(c));
  }
  if (t == "fx.plan") {
    c.op = Op::FxPlan;
    if (!trackOf(j.get("tr"), &c.track)) return refuse("fx.plan: bad tr");
    Json s = j.get("slots");
    if (s.isArr() || s.isObj()) c.raw = text(s); else if (s.isStr()) c.raw = std::string(s.asStr()); else return refuse("fx.plan without slots");
    double b; c.bpm = numOf(j, &b, "bpm") ? b : 120.0;
    return callOf(std::move(c));
  }
  if (t == "fx.set" || t == "fx.param") {
    c.op = Op::FxParam;
    if (!trackOf(j.get("tr"), &c.track)) return refuse(t + ": bad tr");
    Json p = j.arr("p");
    if (p.missing()) return refuse(t + " needs p: the slot's 12 values (a change of effect goes by fx.plan)");
    if (p.size() != 12) return refuse(t + " needs exactly 12 values");
    for (size_t i = 0; i < 12; i++) { Json e = p.at(i); const double d = e.isNum() ? e.asNum() : NAN; if (std::isnan(d) || std::isinf(d)) return refuse(t + ": a value is not a number"); c.p[i] = d; }
    c.slot = ival(j, "slot", -1); c.at = atOf(j);
    return callOf(std::move(c));
  }
  if (t == "midi.cfg") {
    c.op = Op::MidiCfg;
    Json outs = j.arr("outs"), clk = j.arr("clockOut"), gear = j.arr("gearDelay");
    if (!j.get("outs").missing() && outs.missing()) return refuse("midi.cfg: outs must be an array of four ids");
    for (size_t i = 0; i < 4; i++) {
      Json o = outs.at(i); c.outs.push_back(o.isStr() ? std::string(o.asStr()) : std::string());
      Json k = clk.at(i); c.clockOut[i] = (k.isBool() && k.asBool()) || (k.isNum() && (int)k.asNum() != 0);
      Json g = gear.at(i); double d = g.isNum() ? g.asNum() : 0.0; d = d < -50 ? -50 : d > 200 ? 200 : d; c.gear[i] = std::round(d);
    }
    Json ci = j.get("clockIn"); c.clockIn = (ci.isBool() && ci.asBool()) || (ci.isStr() && !ci.asStr().empty());
    return callOf(std::move(c));
  }
  if (t == "pads.commit") { c.op = Op::PadsCommit; c.n = ival(j, "n", 0); return callOf(std::move(c)); }
  if (t == "pad.chop") {
    c.op = Op::PadChop;
    if (j.get("pad").missing()) return refuse("pad.chop needs pad");
    Json s = j.arr("slices");
    for (size_t i = 0; i < s.size(); i++) { Json e = s.at(i); const double d = e.isNum() ? e.asNum() : NAN; if (std::isnan(d) || d < 0) return refuse("pad.chop: a slice point is not a number"); c.pts.push_back(d); }
    int n = ival(j, "n", (int)c.pts.size()); n = n < 2 ? 2 : n > 64 ? 64 : n;
    c.n = n; c.pad = ival(j, "pad", 0); double ls; c.loopSecs = numOf(j, &ls, "loopSecs") ? ls : 0.0;
    return callOf(std::move(c));
  }
  if (t == "pdc.set") { c.op = Op::PdcSet; const int m = ival(j, "mode", -1); if (m < 0 || m > 2) return refuse("pdc.set mode must be 0, 1 or 2"); c.mode = m; return callOf(std::move(c)); }
  if (t == "engine.reset") { c.op = Op::EngineReset; return callOf(std::move(c)); }
  if (t == "fx.commit") { c.op = Op::FxCommit; if (!trackOf(j.get("tr"), &c.track)) return refuse("fx.commit: bad tr"); c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "master.config") {
    c.op = Op::MasterConfig;
    Json m = j.get("mst");
    if (!m.isObj() && !m.isArr()) return refuse("master.config without mst");
    c.raw = text(m); c.safety = j.boolean("safety", true) ? 1 : 0;
    return callOf(std::move(c));
  }
  if (t == "chain.edit") { c.op = Op::ChainEdit; Json a = j.arr("ops"); if (a.missing()) return refuse("chain.edit without ops"); c.raw = text(a); c.at = atOf(j); return callOf(std::move(c)); }
  if (t == "meters.want") { c.op = Op::MetersWant; double r; if (!numOf(j, &r, "rate")) return refuse("meters.want without rate"); c.hz = r < 0 ? 0.0 : r > 30 ? 30.0 : r; return callOf(std::move(c)); }
  if (t == "buf.use") {
    c.op = Op::BufUse;
    const std::string kind = j.get("kind").isStr() ? std::string(j.str("kind")) : "pad";
    if (kind != "pad" && kind != "var") return refuse("buf.use kind '" + kind + "' is not an input of the live host");
    if (j.get("id").missing() || j.get("pad").missing()) return refuse("buf.use needs id and pad");
    const std::string vk(j.str("vk"));
    bool vkOk = vk.size() >= 2 && vk.size() <= 4 && (vk[0] == 'g' || vk[0] == 'm');
    for (size_t i = 1; i < vk.size() && vkOk; i++) if (vk[i] < '0' || vk[i] > '9') vkOk = false;
    if (kind == "var" && (j.get("ii").missing() || !vkOk)) return refuse("buf.use var needs ii and vk like 'm3' or 'g1'");
    c.kind = kind; c.bufId = ival(j, "id", -1); c.pad = ival(j, "pad", 0); c.layer = ival(j, "layer", 0); c.ii = ival(j, "ii", 0); c.frames = ival(j, "frames", -1);
    c.vk = vkOk ? vk[0] : 'g'; c.vkNum = vkOk ? std::atoi(vk.c_str() + 1) : 0;
    return callOf(std::move(c));
  }
  if (t == "buf.free") { c.op = Op::BufFree; c.bufId = ival(j, "id", -1); return callOf(std::move(c)); }
  ign.type = t;   // open, close, put.header, info (the N0 path's), and any type the page sends that the shell does not know
  return ign;
}

}  // namespace tpd
