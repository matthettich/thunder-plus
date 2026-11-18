// U32: THE SOUNDFONT / GM PLAYER. The web app's SoundFont code in C++: index.html's `parseSF`, `sfSrc`, `sfVoice`, `sfPick`, `sfPreset`, `sfSample` (SF2 PCM and SF3 Ogg Vorbis), `sfPreparePreset`, the pad render `sfRender`, and
// the core's live note (core/core-processor.js `sfNote`, `sfRun`: sf_live.h). THE JS IS THE SPECIFICATION, quirks included (docs/port/U32-NOTES.md lists every place it departs from the SoundFont spec and every place this port
// is stricter than the JS because the JS would read outside an array or throw). Every Math.* is a js_* function (fdlibm as V8 12.4 has them; sf_math.cpp adds Math.log10), Math.max / min keep NaN, Math.round is js_round.
//
// THREADS. Everything in this header is CONTROL THREAD (load / preparation): parse, decode (stb_vorbis, pinned in third_party/PINS.md), the pad render. Nothing here is called on the audio thread. The live voices
// (sf_live.h) only READ a Font and a PresetPcm, and allocate nothing after construction.
//
// MEMORY. parse() keeps the font's bytes as a VIEW (the host keeps them alive: an Android host maps the asset, nothing is copied) unless asked to copy. The zones are sparse (about 110 bytes a zone). The decoded PCM is
// per PRESET (`PresetPcm`): float32 frames of the samples one preset's zones use (and their stereo partners), decoded off the audio thread before the preset plays ("prepared before play"); drop it when the pad changes.
#ifndef THUNDER_SF_SF_H
#define THUNDER_SF_SF_H
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace thunder {
namespace sf {

constexpr int kGenN = 64;   // generator numbers kept: the player only ever reads 0..58, a generator above 63 is dropped at load (it could never be read)

// ---- limits (a hostile file stops at these: parse() fails, nothing is partly built) --------------------------------------------------------------------------------------------------------------------------------
constexpr size_t kMaxFontBytes = (size_t)1 << 31;        // the whole file
constexpr size_t kMaxZones = (size_t)1 << 18;            // zones of all presets together (FluidR3 GM: 9455)
constexpr size_t kMaxModPool = (size_t)1 << 22;          // modulator records of all zones together (FluidR3 GM: 26265)
constexpr size_t kMaxWork = (size_t)1 << 27;             // preset zone x instrument zone pairs looked at while building the zones
constexpr size_t kMaxSampleFrames = (size_t)1 << 25;     // frames one decoded sample may have (33 million: 12 minutes at 44.1 kHz); more is refused, as the JS's failed allocation / decode gives its 1-sample silence
constexpr int kMaxListDepth = 32;                        // nested LIST chunks

struct SampleHdr {          // shdr
  char name[21];
  uint32_t start, end, ls, le, sr;   // SF2: frames; SF3: start / end are BYTES of the Ogg stream, ls / le are frames from the sample's start. sr is the header's (`|| 44100`)
  uint8_t root; int8_t corr; uint16_t link, type;
};
struct Mod { uint16_t src, dst; int16_t amt; uint16_t as, tr; };
struct GenV { uint8_t op; int32_t v; };

struct Zone {               // one instrument zone of a preset zone, as parseSF merges them: {klo, khi, vlo, vhi, s, g, im, pm}
  int klo, khi, vlo, vhi;
  uint32_t s;               // sample index (always < samples.size())
  uint32_t gOff; uint16_t gN;      // sparse generators (op, value) in Font::gens, ascending op: the JS object `g`
  uint32_t imOff, pmOff, imN, pmN;   // modulators in Font::mods: the instrument's (defaults merged in) then the preset's
};
struct Preset { char name[21]; int bank, prog; uint32_t z0, nz; };   // zones[z0 .. z0 + nz)

struct Font {
  std::string name; bool sf3 = false;
  const uint8_t* smpl = nullptr; size_t smplN = 0;   // the sdta / smpl chunk (a view; clamped to the file as `u8.subarray` clamps)
  std::vector<uint8_t> own;                          // only if parse() was asked to copy
  std::vector<SampleHdr> samples;
  std::vector<Preset> presets;                       // sorted by bank, then program (a stable sort, as the JS's)
  std::vector<Zone> zones;
  std::vector<GenV> gens;
  std::vector<Mod> mods;
  double bytesUsed() const;                          // memory this object holds (not the view)
};

enum ParseStatus { kParseOk = 0, kParseNotSf = 1, kParseIncomplete = 2, kParseTruncated = 3, kParseTooBig = 4 };
// parseSF(buf): the JS throws 'That isn't a SoundFont' (not RIFF sfbk) or 'looks incomplete or damaged' (a required chunk missing); a DataView read past the file (a chunk longer than the file) throws a RangeError:
// kParseTruncated. kParseTooBig: a limit above (the JS would run out of memory or time). `copy`: keep our own copy of the bytes.
ParseStatus parse(const uint8_t* data, size_t n, Font& out, bool copy = false);

// ---- zones, modulators ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
double sf_src(unsigned code, double key, double vel, bool amt);   // sfSrc
struct VoiceParams {        // what sfVoice returns
  uint32_t s; double root, excl; int modes; double scale, tune, atten, pan, fc, q, modEnvToFc;
  double ofsStart, ofsEnd, ofsLs, ofsLe;
  struct { double delay, atk, hold, dec, sus, rel; } v;   // volume envelope (seconds; sus in centibels, 0..1440)
  struct { double delay, atk, hold, dec, sus, rel; } m;   // modulation envelope (sus 0..1)
};
void sf_voice(const Font& f, const Zone& z, double key, double vel, VoiceParams& out);   // sfVoice
struct Pick { const Zone* z; bool stereo; };
int sf_pick(const Font& f, const Preset* pr, double key, double vel, Pick out[4]);       // sfPick (at most 4); returns the count
const Preset* sf_preset(const Font& f, double bank, double prog);                         // sfPreset (null only for a font with no presets)

// ---- decoded samples ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
struct Pcm {                // sfSample's {d, sr, ls, le, k}: `ls` / `le` in frames of `d` (doubles: the JS keeps them as numbers)
  std::vector<float> d; double sr = 48000, ls = 0, le = 0, k = 1; bool ok = false, present = false;   // present: sfSample ran for it (SF.smp has the key); ok: it decoded (false: the JS's 1-sample silence fallback)
};
struct DecodeOpts {
  bool pageTail = false;    // reproduce Chrome's decodeAudioData losing the last frame (at most half a short block) of a one-audio-page Ogg stream (default off: libvorbis and stb_vorbis keep every frame; U32-NOTES.md "The missing frames")
  double ctxRate = 0;       // 0: keep each sample's own rate (default). >0: resample to this rate as the page's decodeAudioData does (sf_decode.cpp: SF3 only; see U32-NOTES.md "sample rate")
};
// sfSample(font, i): decode one sample (control thread; allocates). A sample the JS could not decode (bad Ogg, a failed allocation) is the fallback {d: 1 zero, sr: 48000, ls: 0, le: 0, k: 1}.
void decode_sample(const Font& f, uint32_t i, const DecodeOpts& o, Pcm& out);
// A preset's samples (sfPreparePreset): the zones' samples and each stereo sample's partner. `byIndex` is sized to the font's sample count; an entry that is not wanted has present == false.
struct PresetPcm {
  std::vector<Pcm> byIndex; uint64_t bytes = 0;
  const Pcm* get(uint32_t i) const { return i < byIndex.size() && byIndex[i].present ? &byIndex[i] : nullptr; }   // the JS's SF.smp.get / D.smp[i]: undefined when not decoded
};
// `keys`: null = every zone of the preset (the whole keyboard); else 128 flags, and only the zones that cover a flagged key (any velocity) are decoded, with their stereo partners. A key whose zones are not
// decoded plays nothing (the live note refuses it, the render returns 0): the host lists the keys its song and its pad use (U32-NOTES.md "Memory"). Same samples, same bits, less RAM.
void prepare_preset(const Font& f, const Preset* pr, const DecodeOpts& o, PresetPcm& out, const uint8_t* keys = nullptr);   // control thread; allocates; the host calls it before the pad plays

// ---- the pad's own render (sfRender: C-4, velocity 100, held for `hold`, then released; mono) -----------------------------------------------------------------------------------------------------------------
struct PadParams { double bank = 0, prog = 0, trans = 0, velo = 0.7, atk = 1, rel = 1; double level = 0; double len = 0; };   // xParams(V) of an `sf` voice (+ V.level, V.len)
// Length of the render before `out` is filled (an upper bound is the same number: the JS makes exactly this many floats), 0 if it cannot be made (no preset, a non-finite rate / hold).
size_t sf_render_bound(const Font& f, const PadParams& P, double sr, double hold);
// sfRender(V, sr, hold): `hold` is renderX's `hold || Math.min(V.len || 2, 3)` already. Returns N and writes N floats, or 0 (and writes nothing) if `cap` is too small, `pcm` lacks the preset's samples
// (the JS returns a 1-sample silence and asks for a decode: here 0 means "prepare first") or the inputs are not finite.
size_t sf_render(const Font& f, const PresetPcm& pcm, const PadParams& P, double sr, double hold, float* out, size_t cap);

}  // namespace sf
}  // namespace thunder
#endif
