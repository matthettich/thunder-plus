// N2 H1b: SongFit. See songfit.h.
#include "songfit.h"

#include <cstdio>

#include "chains/chains.h"
#include "engine/engine.h"
#include "engine/engine_json.h"
#include "engine/xeng.h"
#include "pad_filler.h"
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
bool isPumpModule(std::string_view t) { static const char* const k[] = {"ptube", "psnap", "pgate", "pecho", "pspring", "pverb", "pgrain", "pvox", "ptune", "pcrush", "pswirl", "pstutter", "ptwang"}; for (const char* n : k) if (t == n) return true; return false; }
bool isLxrEngine(std::string_view e) { return e == "lxdrum" || e == "lxsnare" || e == "lxcym" || e == "lxhat"; }
}  // namespace

std::string FitResult::json(int64_t rev) const {
  std::string s = "{\"t\":\"fit\",\"rev\":" + std::to_string((long long)rev) + ",\"ok\":" + (ok ? "true" : "false") + (pending ? ",\"pending\":true" : "") + ",\"reasons\":[";
  for (size_t i = 0; i < reasons.size(); i++) { if (i) s += ","; s += "{\"code\":\"" + esc(reasons[i].code) + "\",\"text\":\"" + esc(reasons[i].text) + "\"}"; }
  s += "],\"reasonTexts\":[";
  for (size_t i = 0; i < reasons.size(); i++) { if (i) s += ","; s += "\"" + esc(reasons[i].text) + "\""; }
  s += "],\"warn\":[";
  for (size_t i = 0; i < warns.size(); i++) { if (i) s += ","; s += "\"" + esc(warns[i]) + "\""; }
  return s + "]}";
}

FitResult songFit(std::string_view songJson, std::string_view kitJson, uint32_t caps, uint64_t maxSongBytes, bool lxrRegistered, const FitContext& ctx) {
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
  { bool sc = false, rs = false, pm = false; std::string unknown;
    for (size_t i = 0; i < mods.size(); i++) {
      const std::string_view t = mods.at(i).str("type");
      if (t == "scin" || t == "scout") sc = true;
      else if (t == "resample") rs = true;
      else if (isPumpModule(t)) pm = !kPumpInChainServiced || pm;
      else if (unknown.empty() && !t.empty() && !chains::cdef(t)) unknown = std::string(t);
    }
    if (sc) add(r, "sidechain", "the song uses a sidechain in its chains");
    if (rs) add(r, "resample", "the song's chains use a Resample module");
    if (pm) add(r, "pump-in-chain", "the song's chains use a pump effect module, which the new engine's chains cannot run yet");
    if (!unknown.empty()) add(r, "module", "the song's chains use a module this build does not have (" + unknown + ")");
  }
  // instruments: stretch; KEY FOLLOW with a live transpose on
  { const Json ins = S.arr("insts"); bool stretch = false, kf = false;
    for (size_t i = 0; i < ins.size(); i++) { const Json v = ins.at(i); const int sy = (int)v.num("sync"); if (!stretch && (sy == 2 || sy == 3 || truthy(v.get("sx")))) { add(r, "stretch", "an instrument uses time-stretch (instrument " + std::to_string(i) + ")"); stretch = true; } if (truthy(v.get("kf"))) kf = true; }
    if (ctx.lxpActive && kf) add(r, "lxp", "live transpose (LXP) is on and the song has key-follow instruments"); }
  if (ctx.clockIn) add(r, "clock-follow", "the page follows an external MIDI clock, which the new engine does not");
  // kit voices. No kit: no verdict (pending) however clean the song looks (verify-n2 F6); the shell asks again after kit.load.
  if (kitJson.empty()) { r.pending = true; r.ok = false; r.reasons.push_back({"no-kit", "the kit has not been loaded yet, so the pads cannot be judged"}); return r; }
  {
    JsonDoc kd;
    if (kd.parse(kitJson)) {
      const Json voices = kd.root().arr("voices");
      eng_init();
      auto xv = std::make_unique<XengVoice>(); auto ev = std::make_unique<EngVoice>();
      int dryPads = 0, twins = 0;
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
        if (eng == "bass" || eng == "vox" || eng == "acid" || eng == "mach") twins++;
        // a pad effect counts only where the page applies it (engFx: padFxApplies) and only when the core build does not host it (Kind::Other: today every type the library does not have; after the pad-effects unit, #314,
        // its types are Kind::Lib and only a type nobody knows stays Other): the check needs no change when #314 lands.
        const Json fx = padFxApplies(eng) ? v.arr("fx") : Json();
        for (size_t k = 0; k < fx.size(); k++) { EngFx f; eng_fx_from_json(f, fx.at(k)); if (f.kind == EngFx::Kind::Other && f.on) { dryPads++; if (dryPads <= 8) add(r, "pad-effect", std::string(tag) + " has a pad effect the new engine does not have"); else r.ok = false; break; } }
      }
      if (twins > 0) r.warns.push_back(std::to_string(twins) + (twins == 1 ? " pad plays" : " pads play") + " its rendered twin (the live engines are not in the new engine yet)");
    } else add(r, "unreadable", "the kit could not be read");
  }
  return r;
}

}  // namespace tpa
