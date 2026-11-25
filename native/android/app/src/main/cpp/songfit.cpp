// N2 H1b: SongFit. See songfit.h.
#include "songfit.h"

#include <cstdio>

#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/xeng.h"
#include "rt/json.h"
#include "thunder/live.h"
#ifndef THUNDER_LIVE_CAP_CHAINS
#define THUNDER_LIVE_CAP_CHAINS 0x200u   /* live.h of n1/c2-live (#311); a header without it never reports the bit, so a chains song falls back until #311 is merged */
#endif

namespace tpa {

namespace {
void add(FitResult& r, const char* code, std::string text) { r.ok = false; r.reasons.push_back({code, std::move(text)}); }
std::string esc(const std::string& s) { std::string o; for (char c : s) { if (c == '"' || c == '\\') { o += '\\'; o += c; } else if ((unsigned char)c < 0x20) o += ' '; else o += c; } return o; }
bool truthy(thunder::Json j) { return j.isBool() ? j.asBool() : j.isNum() ? j.asNum() != 0 : false; }
bool isLxrEngine(std::string_view e) { return e == "lxdrum" || e == "lxsnare" || e == "lxcym" || e == "lxhat"; }
}  // namespace

std::string FitResult::json(int64_t rev) const {
  std::string s = "{\"t\":\"fit\",\"rev\":" + std::to_string((long long)rev) + ",\"ok\":" + (ok ? "true" : "false") + ",\"reasons\":[";
  for (size_t i = 0; i < reasons.size(); i++) { if (i) s += ","; s += "{\"code\":\"" + esc(reasons[i].code) + "\",\"text\":\"" + esc(reasons[i].text) + "\"}"; }
  return s + "]}";
}

FitResult songFit(std::string_view songJson, std::string_view kitJson, uint32_t caps, uint64_t maxSongBytes, bool lxrRegistered) {
  using namespace thunder;
  FitResult r;
  const uint64_t lim = maxSongBytes ? maxSongBytes : (uint64_t)THUNDER_LIVE_MAX_SONG_BYTES;
  if (songJson.size() > lim) add(r, "too-big", "the song is larger than the new engine's limit");
  if (!(caps & THUNDER_LIVE_CAP_EFFECTS)) add(r, "no-effects", "this build of the new engine has no effects");
  JsonDoc sd;
  if (!sd.parse(songJson) || !sd.root().isObj()) { add(r, "unreadable", "the song could not be read"); return r; }
  const Json S = sd.root();
  // chains: a chain switched on for some track, and a patch with modules
  const Json mods = S.obj("chainPatch").arr("modules");
  bool chainOn = false; { const Json co = S.arr("chainOn"); for (size_t i = 0; i < co.size(); i++) if (truthy(co.at(i))) chainOn = true; }
  if (chainOn && mods.size() > 0 && !(caps & THUNDER_LIVE_CAP_CHAINS)) add(r, "no-chains", "the song uses chains and this engine has none");
  for (size_t i = 0; i < mods.size(); i++) { const std::string_view t = mods.at(i).str("type"); if (t == "scin" || t == "scout") { add(r, "sidechain", "the song uses a sidechain in its chains"); break; } }
  // instruments: stretch
  { const Json ins = S.arr("insts"); for (size_t i = 0; i < ins.size(); i++) { const Json v = ins.at(i); const int sy = (int)v.num("sync"); if (sy == 2 || sy == 3 || truthy(v.get("sx"))) { add(r, "stretch", "an instrument uses time-stretch (instrument " + std::to_string(i) + ")"); break; } } }
  // kit voices
  if (!kitJson.empty()) {
    JsonDoc kd;
    if (kd.parse(kitJson)) {
      const Json voices = kd.root().arr("voices");
      eng_init();
      auto xv = std::make_unique<XengVoice>(); auto ev = std::make_unique<EngVoice>();
      int dryPads = 0;
      for (size_t i = 0; i < voices.size() && i < (size_t)THUNDER_LIVE_PADS; i++) {
        const Json v = voices.at(i);
        if (!v.isObj()) continue;
        const std::string_view eng = v.str("engine");
        char tag[80]; std::snprintf(tag, sizeof tag, "pad %zu", i);
        if (eng == "sf") { add(r, "font", std::string(tag) + " uses a SoundFont"); continue; }
        if (isLxrEngine(eng) && !lxrRegistered) { add(r, "no-lxr", std::string(tag) + " uses the drum module, which this build does not have"); continue; }
        EngRenderOpts o;
        *xv = XengVoice();
        bool renders = false;
        if (xeng_voice_from_json(*xv, v, v.arr("fx"))) renders = xeng_render_bound(*xv, o) > 0;
        else if (xv->kind == XKind::None) { *ev = EngVoice(); renders = eng_voice_from_json(*ev, v, v.arr("layers"), v.arr("fx")) && eng_render_bound(*ev, o) > 0; }
        if (!renders) { add(r, "engine", std::string(tag) + " (" + std::string(eng.empty() ? "layers" : eng) + ") is an instrument the new engine cannot render"); continue; }
        const Json fx = v.arr("fx");
        for (size_t k = 0; k < fx.size(); k++) { EngFx f; eng_fx_from_json(f, fx.at(k)); if (f.kind == EngFx::Kind::Other && f.on) { dryPads++; if (dryPads <= 8) add(r, "pad-effect", std::string(tag) + " has a pad effect the new engine does not have"); else r.ok = false; break; } }
      }
    }
  }
  return r;
}

}  // namespace tpa
