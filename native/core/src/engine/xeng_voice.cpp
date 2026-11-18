// U28: renderX / renderVoice's XENG branch for the engines of this unit, and the JSON reader of a voice that uses one: xParams (normX + tune / drive / level), `hold || Math.min(V.len || k, m)`, then padFx
// (a tail, then the pad effects in order: the four JS ones through U17's pad_* cores, every other type passes the sound through as the golden's Node does: U22's decision; then padFx's own trim and 2 ms fade,
// which is there only when some effect is on and ready: without one, padFx returns the engine's render as it is).
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <vector>

#include "engine/engine_json.h"
#include "engine/xeng_util.h"
#include "fx/u17cores.h"

namespace thunder {
using namespace xe;

namespace {
struct FieldRef { const char* name; size_t off; };
#define XP_REF(n, d) {#n, offsetof(XP_CUR, n)},
#define XP_CUR ToneP
const FieldRef kTone[] = { XP_TONE(XP_REF) };
#undef XP_CUR
#define XP_CUR ModalP
const FieldRef kModal[] = { XP_MODAL(XP_REF) };
#undef XP_CUR
#define XP_CUR SwarmP
const FieldRef kSwarm[] = { XP_SWARM(XP_REF) };
#undef XP_CUR
#define XP_CUR DriftP
const FieldRef kDrift[] = { XP_DRIFT(XP_REF) };
#undef XP_CUR
#define XP_CUR PianoP
const FieldRef kPiano[] = { XP_PIANO(XP_REF) };
#undef XP_CUR
#define XP_CUR EpP
const FieldRef kEp[] = { XP_EP(XP_REF) };
#undef XP_CUR
#define XP_CUR OrganP
const FieldRef kOrgan[] = { XP_ORGAN(XP_REF) };
#undef XP_CUR
#define XP_CUR BreathP
const FieldRef kBreath[] = { XP_BREATH(XP_REF) };
#undef XP_CUR
#define XP_CUR StringsP
const FieldRef kStrings[] = { XP_STRINGS(XP_REF) };
#undef XP_CUR
#define XP_CUR FmDrumP
const FieldRef kFmDrum[] = { XP_FMDRUM(XP_REF) };
#undef XP_CUR
#define XP_CUR WindP
const FieldRef kWind[] = { XP_WIND(XP_REF) };
#undef XP_CUR
#define XP_CUR PhaseP
const FieldRef kPhase[] = { XP_PHASE(XP_REF) };
#undef XP_CUR
#define XP_CUR PartialsP
const FieldRef kPartials[] = { XP_PARTIALS(XP_REF) };
#undef XP_CUR
#define XP_CUR SkinP
const FieldRef kSkin[] = { XP_SKIN(XP_REF) };
#undef XP_CUR
#define XP_CUR ChipP
const FieldRef kChip[] = { XP_CHIP(XP_REF) };
#undef XP_CUR
#define XP_CUR AcidP
const FieldRef kAcid[] = { XP_ACID(XP_REF) };
#undef XP_CUR
#define XP_CUR MachP
const FieldRef kMach[] = { XP_MACH(XP_REF) };
#undef XP_CUR
#define XP_CUR XBlipP
const FieldRef kBlip[] = { XP_BLIP(XP_REF) };
#undef XP_CUR
#define XP_CUR BassP
const FieldRef kBass[] = { XP_BASS(XP_REF) };
#undef XP_CUR
#define XP_CUR VoxP
const FieldRef kVox[] = { XP_VOX(XP_REF) };   // (the lyrics, a string, are read apart)
#undef XP_CUR
#define XP_CUR TableP
const FieldRef kTable[] = { XP_TABLE(XP_REF) };
#undef XP_CUR
#define XP_CUR VectorP
const FieldRef kVector[] = { XP_VECTOR(XP_REF) };
#undef XP_CUR
#define XP_CUR ScannedP
const FieldRef kScanned[] = { XP_SCANNED(XP_REF) };
#undef XP_CUR
#define XP_CUR ResynthP
const FieldRef kResynth[] = { XP_RESYNTH(XP_REF) };
#undef XP_CUR
#define XP_CUR BytebeatP
const FieldRef kBytebeat[] = { XP_BYTEBEAT(XP_REF) };
#undef XP_CUR

// normX: only a number that is finite overrides the default; the keys are the defaults' own; tune, drive and level are not taken from the object (xParams puts the voice's)
// (a Blip keeps its own `drive`: normBlip deletes only `vel`)
template <bool kSkipTDL = true, class P, size_t K> void fill(P& p, const FieldRef (&t)[K], Json o) {
  if (!o.isObj()) return;
  for (size_t i = 0; i < K; i++) {
    const std::string_view nm = t[i].name; if (kSkipTDL && (nm == "tune" || nm == "drive" || nm == "level")) continue;
    Json v = o.get(nm); if (v.isNum() && std::isfinite(v.asNum())) *reinterpret_cast<double*>(reinterpret_cast<char*>(&p) + t[i].off) = v.asNum();
  }
}

// String.prototype.slice(0, units) of a UTF-8 (WTF-8) string, counting UTF-16 code units: a pair that would be cut in two is dropped (a lone surrogate matches nothing in the lyrics' tokeniser)
std::string slice16(std::string_view s, size_t units) {
  size_t i = 0, u = 0; const unsigned char* p = (const unsigned char*)s.data();
  while (i < s.size()) {
    const unsigned c = p[i]; const size_t k = c < 0x80 ? 1 : (c >= 0xC2 && c < 0xE0) ? 2 : (c >= 0xE0 && c < 0xF0) ? 3 : (c >= 0xF0 && c < 0xF5) ? 4 : 1; const size_t w = k == 4 ? 2 : 1;
    if (i + k > s.size() || u + w > units) break;
    i += k; u += w;
  }
  return std::string(s.substr(0, i));
}

struct KindName { const char* name; XKind k; };
const KindName kNames[] = {{"tone", XKind::Tone}, {"modal", XKind::Modal}, {"swarm", XKind::Swarm}, {"drift", XKind::Drift}, {"piano", XKind::Piano}, {"ep", XKind::Ep}, {"organ", XKind::Organ}, {"breath", XKind::Breath},
  {"strings", XKind::Strings}, {"fmdrum", XKind::FmDrum}, {"wind", XKind::Wind}, {"phase", XKind::Phase}, {"partials", XKind::Partials}, {"skin", XKind::Skin}, {"chip", XKind::Chip},
  {"table", XKind::Table}, {"vector", XKind::Vector}, {"scanned", XKind::Scanned}, {"resynth", XKind::Resynth}, {"bytebeat", XKind::Bytebeat},
  // the other entries of XENG (and the wasm-backed ones): not this unit's
  {"acid", XKind::Acid}, {"mach", XKind::Mach}, {"blip", XKind::Blip}, {"bass", XKind::Bass}, {"vox", XKind::Vox}, {"sf", XKind::Other}, {"wavesl", XKind::Other},
  {"lxdrum", XKind::Lxr}, {"lxsnare", XKind::Lxr}, {"lxcym", XKind::Lxr}, {"lxhat", XKind::Lxr}, {"pmono", XKind::Other}, {"ppoly", XKind::Other}, {"pcoil", XKind::Other}, {"porbit", XKind::Other}};

inline double holdOf(double hold, double len, double k, double m) { return orv(hold, mn(orv(len, k), m)); }   // hold || Math.min(V.len || k, m)

size_t runEngine(const XengVoice& V, double sr, double hold, double bpm, float* out, size_t cap) {
  const double tu = orz(V.tune), dr = orz(V.drive), lv = orz(V.level);   // xParams: tune: V.tune || 0 ...
#define XP_VOICE(P) P.tune = tu; P.drive = dr; P.level = lv
  switch (V.kind) {
    case XKind::Tone: { ToneP P = V.tone; XP_VOICE(P); return xeng_tone(P, sr, out, cap); }
    case XKind::Modal: { ModalP P = V.modal; XP_VOICE(P); return xeng_modal(P, sr, orv(hold, orv(V.gate, 400) / 1000 * 2), out, cap); }   // renderX's last line: hold || (V.gate || 400) / 1000 * 2
    case XKind::Swarm: { SwarmP P = V.swarm; XP_VOICE(P); return xeng_swarm(P, sr, holdOf(hold, V.len, 3, 4), out, cap); }
    case XKind::Drift: { DriftP P = V.drift; XP_VOICE(P); return xeng_drift(P, sr, holdOf(hold, V.len, 3, 6), out, cap); }
    case XKind::Piano: { PianoP P = V.piano; XP_VOICE(P); return xeng_piano(P, sr, holdOf(hold, V.len, 3, 4), out, cap); }
    case XKind::Ep: { EpP P = V.ep; XP_VOICE(P); return xeng_ep(P, sr, holdOf(hold, V.len, 3, 4), out, cap); }
    case XKind::Organ: { OrganP P = V.organ; XP_VOICE(P); return xeng_organ(P, sr, holdOf(hold, V.len, 2, 3), out, cap); }
    case XKind::Breath: { BreathP P = V.breath; XP_VOICE(P); return xeng_breath(P, sr, holdOf(hold, V.len, 1, 4), out, cap); }
    case XKind::Strings: { StringsP P = V.strings; XP_VOICE(P); return xeng_strings(P, sr, holdOf(hold, V.len, 3, 4), out, cap); }
    case XKind::FmDrum: { FmDrumP P = V.fmdrum; XP_VOICE(P); return xeng_fmdrum(P, sr, out, cap); }
    case XKind::Wind: { WindP P = V.wind; XP_VOICE(P); return xeng_wind(P, sr, holdOf(hold, V.len, 2, 4), out, cap); }
    case XKind::Phase: { PhaseP P = V.phase; XP_VOICE(P); return xeng_phase(P, sr, holdOf(hold, V.len, 2, 4), out, cap); }
    case XKind::Partials: { PartialsP P = V.partials; XP_VOICE(P); return xeng_partials(P, sr, holdOf(hold, V.len, 3, 4), out, cap); }
    case XKind::Skin: { SkinP P = V.skin; XP_VOICE(P); return xeng_skin(P, sr, out, cap); }
    case XKind::Chip: { ChipP P = V.chip; XP_VOICE(P); return xeng_chip(P, sr, holdOf(hold, V.len, 1, 4), out, cap); }
    case XKind::Table: { TableP P = V.table; XP_VOICE(P); return xeng_table(P, sr, holdOf(hold, V.len, 2, 4), V.wt.present ? &V.wt : nullptr, out, cap); }   // V.wtData || null
    case XKind::Vector: { VectorP P = V.vector; XP_VOICE(P); return xeng_vector(P, sr, holdOf(hold, V.len, 3, 6), bpm, out, cap); }   // the song tempo for note-value wave steps
    case XKind::Scanned: { ScannedP P = V.scanned; XP_VOICE(P); return xeng_scanned(P, sr, holdOf(hold, V.len, 3, 6), out, cap); }
    case XKind::Resynth: { ResynthP P = V.resynth; XP_VOICE(P); return xeng_resynth(P, sr, holdOf(hold, V.len, 12, 12), V.rs.present ? &V.rs : nullptr, out, cap); }   // V.rsData || null
    case XKind::Bytebeat: { BytebeatP P = V.bytebeat; XP_VOICE(P); return xeng_bytebeat(P, sr, holdOf(hold, V.len, 2, 4), V.hasBb ? &V.bbExpr : nullptr, out, cap); }   // V.bbExpr: the formula text
    case XKind::Acid: { AcidP P = V.acid; XP_VOICE(P); return xeng_acid(P, sr, holdOf(hold, V.len, 1, 1.2), out, cap); }   // renderLiveCore(AcidCore, P, sr, hold || Math.min(V.len || 1, 1.2), false)
    case XKind::Mach: { MachP P = V.mach; XP_VOICE(P); return xeng_mach(P, sr, holdOf(hold, V.len, 2, 2), out, cap); }       // renderLiveCore(MachCore, P, sr, hold || Math.min(V.len || 2, 2))
    case XKind::Blip: return xeng_blip(V.blip, sr, V.tune, V.drive, V.level, out, cap);   // renderBlip(V, sr): no hold
    case XKind::Bass: { BassP P = V.bass; XP_VOICE(P); return xeng_bass(P, sr, hold, V.len, bpm, out, cap); }   // renderBass(V, sr, hold): `hold || V.len || 2`, c.bpm = songBpm(); a buffer of two renders (see xeng.h)
    case XKind::Vox: { VoxP P = V.vox; XP_VOICE(P); return xeng_vox(P, sr, out, cap); }
    case XKind::Lxr: {   // U04: LxrCore.render(k, P, sr, hold, V, songBpm()) through the seam; no module registered = refused (the JS has the engine: silence would be a lie)
      const ThunderLxrApi* api = thunder_lxr_api(); if (!api) return kRefuse;
      ThunderLxrRequest R = V.lxr; R.tune = tu; R.drive = dr; R.level = lv; R.vlen = V.len; R.hold = hold; R.bpm = bpm; R.sr = sr;
      if (!out) return api->render_bound(&R);
      size_t n = 0; const int st = api->render_voice(&R, out, cap, &n);
      return st == THUNDER_LXR_OK ? n : st == THUNDER_LXR_TOO_SMALL ? 0 : kRefuse;
    }   // renderVox(V, sr): no hold
    default: return 0;
  }
#undef XP_VOICE
}

// padFx's lengths (index.html padFx, and U22's engine.cpp for the pitch effects' tails)
inline bool fxReady(const EngFx& f) { return f.kind != EngFx::Kind::Other; }
// engFx: pad effects run on Blip and on every engine that is not live; Acid and Machine play live in the song, so they stay dry (their voice's fx are not applied)
bool anyFxOn(const XengVoice& V) {
  if (V.kind == XKind::Acid || V.kind == XKind::Mach || V.kind == XKind::Bass || V.kind == XKind::Vox) return false;   // (renderVoice returns the bass and voice renders before padFx)
  for (int i = 0; i < V.nFx; i++) { if (V.fx[i].on && fxReady(V.fx[i])) return true; }
  return false;
}
double pitchTail(const EngFx& f, double sr) { const bool rv = f.kind == EngFx::Kind::PitchReverb; return js_round(sr * (rv ? 3 + (f.sizeNull ? 0.0 : f.size) * 4 : mn(8, f.time / 1000 * (1 + 6 * f.fb)))); }
}  // namespace

XKind xeng_kind(const char* name, size_t n) {
  for (const KindName& k : kNames) if (std::strlen(k.name) == n && std::memcmp(k.name, name, n) == 0) return k.k;
  return XKind::None;
}

bool xeng_voice_from_json(XengVoice& V, Json v, Json fx) {
  const std::string_view en = v.str("engine"); V.kind = xeng_kind(en.data(), en.size());
  if (V.kind == XKind::None || V.kind == XKind::Other) return false;
  if (V.kind == XKind::Lxr) {   // U04: the pad's defaults come from the module (its table of knobs); with no module the voice is not understood (as before: refused)
    const ThunderLxrApi* api = thunder_lxr_api(); if (!api) return false;
    const int k = en == "lxdrum" ? 0 : en == "lxsnare" ? 1 : en == "lxcym" ? 2 : 3;
    ThunderLxrDescribe D; if (api->describe(k, &D) != THUNDER_LXR_OK) return false;
    V.lxr.kind = k; V.len = 0;   // a voice without `len` plays the engine's own (V.len || Vc.len)
    for (int n = 0; n < THUNDER_LXR_MAX_PARAMS; n++) V.lxr.p[n] = D.def[n];
    V.lxr.vdst = D.vdst; V.lxr.ldst = D.ldst; V.lxr.vel = D.vel; V.lxr.open = D.open;
    Json o = v.get(en);   // normX: a number that is finite replaces the default of a key the engine has
    char key[8];
    for (int n = 0; n < THUNDER_LXR_MAX_PARAMS; n++) if (D.present[n]) { std::snprintf(key, sizeof key, "p%d", n); Json x = o.get(key); if (x.isNum() && std::isfinite(x.asNum())) V.lxr.p[n] = x.asNum(); }
    auto nx = [&](const char* kk, double& d) { Json x = o.get(kk); if (x.isNum() && std::isfinite(x.asNum())) d = x.asNum(); };
    nx("vdst", V.lxr.vdst); nx("ldst", V.lxr.ldst); nx("vel", V.lxr.vel); if (D.hasOpen) nx("open", V.lxr.open);
  }
  auto num = [&](const char* k, double& d) { Json x = v.get(k); if (x.isNum()) d = x.asNum(); };
  num("level", V.level); num("tune", V.tune); num("drive", V.drive); num("len", V.len); num("tail", V.tail); num("gate", V.gate);
  if (fx.size() > (size_t)kEngFx) return false;
  V.nFx = (int)fx.size(); for (size_t i = 0; i < fx.size(); i++) eng_fx_from_json(V.fx[i], fx.at(i));
  Json o = v.get(en);
  switch (V.kind) {
    case XKind::Tone: fill(V.tone, kTone, o); break;       case XKind::Modal: fill(V.modal, kModal, o); break;     case XKind::Swarm: fill(V.swarm, kSwarm, o); break;
    case XKind::Drift: fill(V.drift, kDrift, o); break;    case XKind::Piano: fill(V.piano, kPiano, o); break;     case XKind::Ep: fill(V.ep, kEp, o); break;
    case XKind::Organ: fill(V.organ, kOrgan, o); break;    case XKind::Breath: fill(V.breath, kBreath, o); break;  case XKind::Strings: fill(V.strings, kStrings, o); break;
    case XKind::FmDrum: fill(V.fmdrum, kFmDrum, o); break; case XKind::Wind: fill(V.wind, kWind, o); break;       case XKind::Phase: fill(V.phase, kPhase, o); break;
    case XKind::Partials: fill(V.partials, kPartials, o); break; case XKind::Skin: fill(V.skin, kSkin, o); break; case XKind::Chip: fill(V.chip, kChip, o); break;
    case XKind::Table: fill(V.table, kTable, o); xeng_wt_from_json(V.wt, v.get("wtData")); break;
    case XKind::Bass: fill(V.bass, kBass, o); break;
    case XKind::Vox: fill(V.vox, kVox, o); { Json t = o.get("txt"); if (o.isObj() && t.isStr()) V.vox.txt = slice16(t.asStr(), 4000); } break;   // normVox: txt is `o.txt.slice(0, 4000)` when it is a string, else the default
    case XKind::Acid: fill(V.acid, kAcid, o); break;      case XKind::Mach: fill(V.mach, kMach, o); break;        case XKind::Blip: fill<false>(V.blip, kBlip, o); break;
    case XKind::Vector: fill(V.vector, kVector, o); break; case XKind::Scanned: fill(V.scanned, kScanned, o); break;
    case XKind::Resynth: fill(V.resynth, kResynth, o); xeng_rs_from_json(V.rs, v.get("rsData")); break;
    case XKind::Bytebeat: fill(V.bytebeat, kBytebeat, o); { Json b = v.get("bbExpr"); V.hasBb = b.isStr(); if (V.hasBb) V.bbExpr = std::string(b.asStr()); } break;
    default: break;
  }
  return true;
}

size_t xeng_render_bound(const XengVoice& V, const EngRenderOpts& o) {
  if (!(o.sr > 0 && o.sr <= 1e7)) return 0;
  const size_t N = runEngine(V, o.sr, o.hold, o.bpm, nullptr, 0);
  if (N == 0 || N == kRefuse) return 0;
  double len = (double)N;
  if (anyFxOn(V)) {
    const double tailN = js_round(orz(V.tail) * o.sr), extN = mn((double)N + tailN, o.sr * (kSmpMax + 12));
    const double ext = extN >= 0 ? std::floor(extN) : 0; len = ext > len ? ext : len; double cur = ext;
    for (int i = 0; i < V.nFx; i++) { const EngFx& f = V.fx[i]; if (!f.on || !fxReady(f)) continue;
      if (f.kind == EngFx::Kind::PitchDelay || f.kind == EngFx::Kind::PitchReverb) { const double t = pitchTail(f, o.sr); if (t >= 0 && t < 4e9) cur += t; if (cur > len) len = cur; } }
  }
  return len < 4e9 ? (size_t)len : 0;
}

int xeng_render_voice(const XengVoice& V, const EngRenderOpts& o, float* out, size_t cap, size_t* nOut) {
  if (V.kind == XKind::None || V.kind == XKind::Other) return kEngUnsupported;
  if (!(o.sr > 0 && o.sr <= 1e7)) return kEngUnsupported;
  const size_t bound = xeng_render_bound(V, o);
  if (bound == 0) return kEngUnsupported;   // not representable (a length the JS cannot allocate either): refused, never silence
  if (cap < bound) return kEngTooSmall;
  const double sr = o.sr;
  size_t len = runEngine(V, sr, o.hold, o.bpm, out, cap);
  if (len == kRefuse) return kEngUnsupported;
  if (len == 0) return kEngTooSmall;
  if (anyFxOn(V)) {
    const size_t N = len;
    const double tailN = js_round(orz(V.tail) * sr), extN = mn((double)N + tailN, sr * (kSmpMax + 12));
    const size_t ext = (size_t)(extN > 0 ? std::floor(extN) : 0);
    if (ext > N) std::memset(out + N, 0, (ext - N) * sizeof(float));
    len = ext;
    for (int i = 0; i < V.nFx; i++) {
      const EngFx& f = V.fx[i]; if (!f.on || !fxReady(f)) continue;
      std::vector<float> r;
      switch (f.kind) {
        case EngFx::Kind::Degrade: r = pad_degrade(PadDegrade{f.bits, f.rate, f.noise, f.color, f.follow, f.dry, f.wet}, out, len, sr); break;
        case EngFx::Kind::Reson: r = pad_reson(PadReson{f.note, f.chord, f.decay, f.bright, f.mode, f.spread, f.mix}, out, len, sr); break;
        case EngFx::Kind::PitchDelay: r = pad_pitchfx(false, PadPitchFx{f.mode, f.time, f.fb, f.pitch, f.pitch2, f.tone, f.size, f.wow, f.mix, f.sizeNull}, out, len, sr); break;
        case EngFx::Kind::PitchReverb: r = pad_pitchfx(true, PadPitchFx{f.mode, f.time, f.fb, f.pitch, f.pitch2, f.tone, f.size, f.wow, f.mix, f.sizeNull}, out, len, sr); break;
        default: break;
      }
      if (r.empty()) return kEngFxThrew;
      if (r.size() > cap) return kEngTooSmall;
      std::memcpy(out, r.data(), r.size() * sizeof(float)); len = r.size();
    }
    len = trim_fade(out, len, sr, 0.002);   // padFx's own tail: the last sample at or above 1e-4, a 2 ms fade into it
  }
  *nOut = len;
  return kEngOk;
}

}  // namespace thunder
