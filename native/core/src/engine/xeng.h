// U28: the whole-voice engines of index.html's XENG table that only ever render (tone, modal, swarm, drift, piano, ep, organ, breath, strings, fmdrum, wind, phase, partials, skin, chip)
// and KeysUtil, line for line. THE JS IS THE SPECIFICATION, quirks included: every Math.* is a js_* function (fdlibm as V8 12.4 has them), every expression keeps the JS's order, the only
// float32 values are the ones the JS writes into a Float32Array (the render buffer), the oscillator banks and filters are doubles as in the JS.
// Layout: one source file per engine (xeng_<name>.cpp), xeng_util.{h,cpp} is KeysUtil (bank / rnd / finish) and the JS's shared tail (trim + fade), xeng_voice.cpp is renderX / renderVoice's XENG
// branch (the voice's engine params, hold = `hold || Math.min(V.len || k, m)`, the pad effects through U17's pad_* cores) and the JSON reader.
// NOT HERE (U29 / later): table, vector, scanned, resynth, bytebeat, sf, wavesl, the live engines (acid, mach), the wasm-backed ones. A voice of such an engine is refused (kEngUnsupported).
// Buffers: the caller hands in `out` of at least xeng_render_bound() floats; nothing here owns a pad buffer. Every engine renders without allocating EXCEPT strings, wind and drift (they
// allocate their delay lines: a Float32Array sized from the sample rate and the pitch in the JS) and a voice with pad effects (U17's pad_* cores return std::vector). Offline, control thread.
#ifndef THUNDER_ENGINE_XENG_H
#define THUNDER_ENGINE_XENG_H
#include <cstddef>
#include <cstdint>

#include "engine/engine.h"
#include "rt/json.h"

namespace thunder {

// ---- the engines' parameters: the JS's defaults() objects (tune, drive and level come from the voice, see normX / xParams) ------------------------------------------------------------
#define XP_TONE(X) X(wave,0) X(pitch,160) X(bend,14) X(bendT,45) X(tdec,320) X(tLev,0.85) X(spectra,0) X(nType,1) X(nFreq,2400) X(nRes,0.25) X(nDec,160) X(nSweep,0) X(nLev,0.35) X(click,0.3) X(dist,0) X(tune,0) X(drive,0) X(level,0)
#define XP_MODAL(X) X(mat,0) X(oct,2) X(modes,16) X(bright,0.6) X(dec,1.2) X(dtilt,0.5) X(pos,0.28) X(inharm,0) X(exc,0) X(hard,0.55) X(bow,0.6) X(noise,0) X(width,0.3) X(tune,0) X(drive,0) X(level,0)
#define XP_SWARM(X) X(oct,2) X(voices,16) X(wave,0) X(spread,0.35) X(drift,0.3) X(rate,0.4) X(cohes,0.3) X(scatter,0) X(ivl,0) X(bloom,0) X(shimmer,0) X(sub,0) X(cut,4000) X(res,0.15) X(fenv,0.2) X(fdec,600) X(key,0.5) X(atk,30) X(dec,800) X(sus,0.8) X(rel,600) X(tune,0) X(drive,0) X(level,0)
#define XP_DRIFT(X) X(oct,2) X(aSaw,0.8) X(aPulse,0) X(bSaw,0) X(bPulse,0.6) X(bSemi,0) X(detune,7) X(pw,0.5) X(pwm,0.35) X(pwmRate,0.7) X(sub,0) X(noise,0) X(ring,0) X(voices,2) X(spread,0.3) X(drift,0.35) \
  X(hp,40) X(hpRes,0) X(cut,1800) X(res,0.15) X(fenv,0.25) X(fatk,400) X(fdec,1500) X(fsus,0.4) X(key,0.5) X(atk,600) X(dec,1500) X(sus,0.85) X(rel,1800) X(vib,0) X(vibRate,5) X(vibDelay,600) \
  X(wow,0.35) X(wowRate,0.55) X(flutter,0.25) X(chorus,0.35) X(sat,0.3) X(age,0.3) X(hiss,0.15) X(tune,0) X(drive,0) X(level,0)
#define XP_PIANO(X) X(oct,2) X(hard,0.55) X(bright,0.55) X(dec,1) X(strings,3) X(det,0.35) X(inharm,0.4) X(pos,0.12) X(thump,0.35) X(body,0.4) X(rel,180) X(pedal,0) X(tune,0) X(drive,0) X(level,0)
#define XP_EP(X) X(oct,2) X(hard,0.5) X(bark,0.45) X(bell,0.4) X(dec,1) X(tone,0.6) X(trem,0) X(tremRate,4.5) X(rel,120) X(tune,0) X(drive,0) X(level,0)
#define XP_ORGAN(X) X(oct,2) X(d1,8) X(d2,8) X(d3,8) X(d4,0) X(d5,0) X(d6,0) X(d7,0) X(d8,0) X(d9,0) X(perc,0) X(percLv,0.5) X(percDec,250) X(click,0.3) X(pluck,0) X(pluckDec,300) X(rotary,0) X(rotDepth,0.5) X(bright,0.7) X(atk,3) X(rel,40) X(tune,0) X(drive,0) X(level,0)
#define XP_BREATH(X) X(color,0.5) X(grain,0) X(grate,40) X(bands,4) X(freq,3000) X(spread,0.3) X(metal,0.5) X(q,0.5) X(body,0.6) X(key,0) X(hp,200) X(lp,14000) X(fenv,0) X(fdec,200) X(sweep,0) X(wind,0) X(atk,1) X(dec,200) X(sus,0) X(rel,80) X(tune,0) X(drive,0) X(level,0)
#define XP_STRINGS(X) X(oct,2) X(voices,1) X(chord,0) X(strum,25) X(down,0) X(det,0.3) X(pick,0.6) X(pos,0.28) X(decay,1.6) X(damp,0.4) X(body,0.5) X(size,0.5) X(vib,0) X(vrate,5) X(rel,120) X(tune,0) X(drive,0) X(level,0)
#define XP_FMDRUM(X) X(pitch,110) X(bend,18) X(bendT,60) X(alg,0) X(r1,1) X(i1,0.4) X(idec1,120) X(r2,3.5) X(i2,0.3) X(idec2,60) X(fb,0) X(noise,0) X(ndec,120) X(ncut,6000) X(click,0.2) X(tone,1) X(dec,400) X(curve,0.5) X(tune,0) X(drive,0) X(level,0)
#define XP_WIND(X) X(model,0) X(oct,2) X(breath,0.6) X(noise,0.15) X(stiff,0.5) X(pos,0.3) X(damp,0.5) X(body,0.3) X(vib,0.2) X(vrate,5.5) X(growl,0) X(atk,60) X(rel,120) X(tune,0) X(drive,0) X(level,0)
#define XP_PHASE(X) X(oct,2) X(wave,0) X(wave2,1) X(mix,0) X(det,0.1) X(ring,0) X(dcw,0.7) X(datk,2) X(ddec,500) X(dsus,0.3) X(drel,300) X(key,0.3) X(noise,0) X(atk,3) X(dec,800) X(sus,0.7) X(rel,250) X(tune,0) X(drive,0) X(level,0)
#define XP_PARTIALS(X) X(oct,2) X(count,64) X(tilt,0.5) X(comb,0) X(fmt,1200) X(fwidth,0.4) X(famt,0) X(stretch,0) X(tilt2,0.5) X(comb2,0) X(morph,0) X(mtime,1500) X(decay,3) X(dtilt,0.5) X(bright,0.5) X(batk,10) X(bdec,800) X(drift,0.1) X(drate,0.5) X(atk,10) X(sus,1) X(rel,400) X(tune,0) X(drive,0) X(level,0)
#define XP_SKIN(X) X(pitch,110) X(size,0.5) X(tension,0.3) X(tenT,80) X(pos,0.3) X(hard,0.5) X(dec,600) X(damp,0.5) X(muffle,0.2) X(shell,0.4) X(shellF,0.5) X(noise,0.1) X(tune,0) X(drive,0) X(level,0)
#define XP_CHIP(X) X(oct,3) X(wave,0) X(duty,1) X(sweep,0) X(chord,0) X(arp,1) X(slide,0) X(vib,0) X(vrate,6) X(quant,1) X(atk,0) X(dec,12) X(sus,0.6) X(rel,4) X(nfreq,0.7) X(nshort,0) X(tune,0) X(drive,0) X(level,0)

#define XP_FIELD(n, d) double n = d;
struct ToneP { XP_TONE(XP_FIELD) };      struct ModalP { XP_MODAL(XP_FIELD) };      struct SwarmP { XP_SWARM(XP_FIELD) };   struct DriftP { XP_DRIFT(XP_FIELD) };
struct PianoP { XP_PIANO(XP_FIELD) };    struct EpP { XP_EP(XP_FIELD) };            struct OrganP { XP_ORGAN(XP_FIELD) };   struct BreathP { XP_BREATH(XP_FIELD) };
struct StringsP { XP_STRINGS(XP_FIELD) }; struct FmDrumP { XP_FMDRUM(XP_FIELD) };   struct WindP { XP_WIND(XP_FIELD) };     struct PhaseP { XP_PHASE(XP_FIELD) };
struct PartialsP { XP_PARTIALS(XP_FIELD) }; struct SkinP { XP_SKIN(XP_FIELD) };     struct ChipP { XP_CHIP(XP_FIELD) };

enum class XKind : uint8_t { None, Tone, Modal, Swarm, Drift, Piano, Ep, Organ, Breath, Strings, FmDrum, Wind, Phase, Partials, Skin, Chip, Other };
XKind xeng_kind(const char* name, size_t n);   // the engine's key in XENG; None for "layers" (and the engines of U22 / U23 / U24: bass, vox, blip are not XENG), Other for an XENG engine this unit does not port

// ---- one engine's render: `X.render(P, sr, hold)` with P = xParams(V). `out` is the caller's buffer of `cap` floats; the finished sound (trimmed, faded) goes in, its length is returned. ----
// With out == nullptr the call only returns the length N of the render before the trim (the upper bound), 0 if it is not representable. 0 is also returned if `cap` is too small.
size_t xeng_tone(const ToneP& p, double sr, float* out, size_t cap);
size_t xeng_modal(const ModalP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_swarm(const SwarmP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_drift(const DriftP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_piano(const PianoP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_ep(const EpP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_organ(const OrganP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_breath(const BreathP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_strings(const StringsP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_fmdrum(const FmDrumP& p, double sr, float* out, size_t cap);
size_t xeng_wind(const WindP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_phase(const PhaseP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_partials(const PartialsP& p, double sr, double hold, float* out, size_t cap);
size_t xeng_skin(const SkinP& p, double sr, float* out, size_t cap);
size_t xeng_chip(const ChipP& p, double sr, double hold, float* out, size_t cap);

// ---- a voice with one of these engines: mkVoice's fields that matter (level, tune, drive, len, tail, the pad effects) and the engine's own parameter object (v[engine]) ----------------
struct XengVoice {
  XKind kind = XKind::None;
  ToneP tone; ModalP modal; SwarmP swarm; DriftP drift; PianoP piano; EpP ep; OrganP organ; BreathP breath; StringsP strings; FmDrumP fmdrum; WindP wind; PhaseP phase; PartialsP partials; SkinP skin; ChipP chip;
  double level = 0, tune = 0, drive = 0, len = 4, tail = 2, gate = 200;
  EngFx fx[kEngFx]; int nFx = 0;
};
// Fills V from a song voice's JSON: `v` is the voice object (engine, level, tune, drive, len, tail, and the engine's parameter object under the engine's name), `fx` the pad effects (array).
// Returns false if the engine is not of this unit (V.kind says which: None = not an XENG voice, Other = an XENG engine not ported here) or there are more effects than kEngFx.
bool xeng_voice_from_json(XengVoice& V, Json v, Json fx);
// renderVoice(V, sr, null, hold) for such a voice (renderX, then padFx): into `out`, its length in *n. Status as the layer engine's (kEngOk, kEngTooSmall, kEngUnsupported, kEngFxThrew).
size_t xeng_render_bound(const XengVoice& V, const EngRenderOpts& o);   // floats the caller's buffer must hold (an upper bound), 0 if the voice cannot be rendered
int xeng_render_voice(const XengVoice& V, const EngRenderOpts& o, float* out, size_t cap, size_t* n);

}  // namespace thunder
#endif
