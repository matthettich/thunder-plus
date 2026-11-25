// N2 H1a: see pad_filler.h.
#include "pad_filler.h"

#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/xeng.h"
#include "rt/json.h"

namespace tpa {

std::string_view extractMember(std::string_view s, std::string_view key) {
  size_t i = 0; const size_t n = s.size();
  auto ws = [&] { while (i < n && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; };
  // skips one JSON string starting at s[i] == '"'; returns its raw content range
  auto str = [&](size_t* b, size_t* e) -> bool {
    if (i >= n || s[i] != '"') return false;
    *b = ++i;
    while (i < n && s[i] != '"') { if (s[i] == '\\') i++; i++; }
    if (i >= n) return false;
    *e = i++; return true;
  };
  // skips one value (string, object, array or scalar) starting at s[i]; returns its range
  auto value = [&](size_t* b, size_t* e) -> bool {
    ws(); *b = i;
    if (i >= n) return false;
    if (s[i] == '"') { size_t x, y; if (!str(&x, &y)) return false; *e = i; return true; }
    if (s[i] == '{' || s[i] == '[') {
      int depth = 0;
      while (i < n) {
        const char c = s[i];
        if (c == '"') { size_t x, y; if (!str(&x, &y)) return false; continue; }
        if (c == '{' || c == '[') depth++;
        else if (c == '}' || c == ']') { depth--; if (depth == 0) { i++; *e = i; return true; } }
        i++;
      }
      return false;
    }
    while (i < n && s[i] != ',' && s[i] != '}' && s[i] != ']' && s[i] != ' ' && s[i] != '\n' && s[i] != '\r' && s[i] != '\t') i++;
    *e = i; return *e > *b;
  };
  ws();
  if (i >= n || s[i] != '{') return {};
  i++;
  std::string_view found;
  for (;;) {
    ws();
    if (i < n && s[i] == '}') break;
    size_t kb, ke; if (!str(&kb, &ke)) return {};
    ws(); if (i >= n || s[i] != ':') return {}; i++;
    size_t vb, ve; if (!value(&vb, &ve)) return {};
    if (s.substr(kb, ke - kb) == key) found = s.substr(vb, ve - vb);   // the last of equal keys, as the core's Json::get
    ws();
    if (i < n && s[i] == ',') { i++; continue; }
    break;
  }
  return found;
}

PadSet::~PadSet() { for (auto& p : pads) if (p.block) { thunder_live_free(p.block); p.block = nullptr; } }

double relOfVoice(thunder::Json V) {
  if (!V.isObj()) return 0.03;
  const std::string_view eng = V.str("engine");
  const bool layersV = eng.empty() || eng == "layers";
  const bool bass = eng == "bass", vox = eng == "vox", blip = eng == "blip", xeng = !layersV && !bass && !vox && !blip;
  thunder::Json own;
  if (bass) own = V.obj("bass"); else if (vox) own = V.obj("vox"); else if (xeng && eng != "sf") own = V.obj(eng);
  if (own.isObj() && own.get("rel").isNum()) {
    const double rel = own.num("rel");
    return eng == "chip" ? std::fmax(1, rel * 1000 / 60) / 1000 : std::fmax(1, rel) / 1000;
  }
  double r = 0;
  const thunder::Json L = V.arr("layers");
  for (size_t i = 0; i < L.size(); i++) { const thunder::Json l = L.at(i); if (l.boolean("on", false) && l.num("level") > 0 && l.str("mode") == "ADSR") r = std::fmax(r, l.num("rel")); }
  if (blip || xeng) return std::fmax(30, r) / 1000;
  if (V.boolean("gAmpOn") && V.str("gMode") == "ADSR") { const double g = V.num("gRel"); return std::fmax(1, g != 0 ? g : 1) / 1000; }
  return (r > 0 ? std::fmax(1, r) : 30) / 1000;
}

bool kitEntryOf(thunder::Json kit, size_t slot, int* choke, bool* syn, double* rel) {
  const thunder::Json voices = kit.arr("voices");
  const thunder::Json v = voices.at(slot);
  if (!v.isObj()) return false;
  const bool vtrk = voices.size() > 16;   // GUESS, as in renderPads
  const thunder::Json held = kit.arr("held");
  const bool s = (!vtrk && held.isArr() && held.at(slot).isBool()) ? held.at(slot).asBool() : slot >= 8;
  *choke = (int)v.num("choke"); *syn = s; *rel = s ? std::fmin(60.0, relOfVoice(v)) : 0;
  return true;
}

namespace {

int dryFx(thunder::Json fx) {   // pad effects the core does not host (Kind::Other) that are switched on
  int n = 0;
  for (size_t i = 0; i < fx.size(); i++) { thunder::EngFx f; thunder::eng_fx_from_json(f, fx.at(i)); if (f.kind == thunder::EngFx::Kind::Other && f.on) n++; }
  return n;
}

}  // namespace

bool renderPads(std::string_view kitJson, double bpm, double lpb, double sr, PadSet* out) {
  using namespace thunder;
  const auto t0 = std::chrono::steady_clock::now();
  JsonDoc doc; if (!doc.parse(kitJson)) return false;
  const Json kit = doc.root(); const Json voices = kit.arr("voices");
  if (!kit.isObj() || !voices.isArr()) return false;
  const bool vtrk = voices.size() > 16;   // GUESS: index.html:20438 sets vtrk for a loaded project with more than 16 voices
  const Json held = kit.arr("held");
  eng_init();
  auto isSyn = [&](size_t i) { if (!vtrk && held.isArr() && held.at(i).isBool()) return held.at(i).asBool(); return i >= 8; };   // index.html:4924, NPAD = 8
  auto xv = std::make_unique<XengVoice>(); auto ev = std::make_unique<EngVoice>();   // large: on the heap
  std::vector<float> buf;
  const size_t cnt = voices.size() < (size_t)THUNDER_LIVE_PADS ? voices.size() : (size_t)THUNDER_LIVE_PADS;
  for (size_t i = 0; i < cnt; i++) {
    const Json v = voices.at(i);
    if (!v.isObj()) continue;
    char tag[64]; std::snprintf(tag, sizeof tag, "pad %zu (%.*s)", i, (int)v.str("name").size(), v.str("name").data());
    const bool syn = isSyn(i);
    EngRenderOpts o; o.sr = sr; o.bpm = bpm > 0 ? bpm : 120; o.hold = syn ? v.num("len") : 0;
    size_t n = 0; int st = kEngUnsupported;
    *xv = XengVoice();
    if (xeng_voice_from_json(*xv, v, v.arr("fx"))) {
      const size_t bound = xeng_render_bound(*xv, o);
      if (bound) { buf.assign(bound, 0.f); st = xeng_render_voice(*xv, o, buf.data(), buf.size(), &n); }
    } else if (xv->kind == XKind::None) {
      *ev = EngVoice();
      if (eng_voice_from_json(*ev, v, v.arr("layers"), v.arr("fx"))) {
        double hold = o.hold; eng_env_lines_fit(*ev, &hold, o.bpm, lpb > 0 ? lpb : 4); o.hold = hold;
        const size_t bound = eng_render_bound(*ev, o);
        if (bound) { buf.assign(bound, 0.f); st = eng_render_voice(*ev, o, buf.data(), buf.size(), &n); }
      }
    }
    if (st != kEngOk || n == 0 || n > buf.size()) { out->skipped++; out->notes.push_back(std::string(tag) + ": not rendered by the core (status " + std::to_string(st) + ")"); continue; }
    bool finite = true; for (size_t k = 0; k < n; k++) if (!std::isfinite(buf[k])) { finite = false; break; }
    if (!finite) { out->skipped++; out->notes.push_back(std::string(tag) + ": render has a non-finite sample, left empty"); continue; }
    float* b = thunder_live_alloc((int64_t)n);
    if (!b) { out->skipped++; out->notes.push_back(std::string(tag) + ": out of memory"); continue; }
    std::memcpy(b, buf.data(), sizeof(float) * n);
    RenderedPad p; p.slot = (int)i; p.block = b; p.frames = (int64_t)n; p.choke = (int)v.num("choke"); p.syn = syn; p.rel = syn ? std::fmin(60.0, relOfVoice(v)) : 0;
    out->pads.push_back(p);
    if (dryFx(v.arr("fx")) > 0) { out->dry++; out->notes.push_back(std::string(tag) + ": rendered without a pad effect the core does not have"); }
  }
  out->ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
  return true;
}

}  // namespace tpa
