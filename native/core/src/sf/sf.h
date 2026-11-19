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
constexpr size_t kMaxModPool = (size_t)1 << 19;          // modulator records of all zones together (FluidR3 GM: 26265)
constexpr size_t kMaxWork = 200000;                   // preset zone x instrument zone pairs looked at while building the zones (FluidR3 GM: 10027; this is 20x)
constexpr size_t kMaxSampleFrames = (size_t)1 << 25;     // frames one decoded sample may have (33 million: 12 minutes at 44.1 kHz); more is refused, as the JS's failed allocation / decode gives its 1-sample silence
// Parse memory: the tables parse() builds (zones + generators + modulators) are the part of a font that can be far larger than the file (a zone pair copies its generators and modulators: a 191 KB font at kMaxWork was 205 MB).
// They may hold at most kParseTableFloor + kParseTableFactor x (the font's own size) bytes (the tables' element sizes times counts; vector growth may transiently hold about 3x that), else kParseTooBig.
// FluidR3 GM: its tables are 2.6 MB for a 140 MB file (0.02x); the test font fits the floor.
constexpr size_t kParseTableFactor = 32;
constexpr size_t kParseTableFloor = (size_t)8 << 20;
constexpr uint64_t kWorkPerOpen = 32768;    // work units charged for opening an Ogg stream (stb_vorbis parses its setup header: 0.35 ms measured, about 29000 units of decode time), on top of its bytes and frames
constexpr int kMaxListDepth = 32;
constexpr uint32_t kMinStreamRate = 4000, kMaxStreamRate = 192000;   // an Ogg stream's own sample rate outside this, and a context rate (DecodeOpts::ctxRate) outside it, is refused (silent 1-frame fallback)
constexpr uint32_t kMinHdrRate = 400, kMaxHdrRate = 192000;           // a sample header's rate (SF2 spec: 400..50000) outside this is refused the same way; the page plays at hdr.sr / sr, so an absurd one is an absurd pitch                        // nested LIST chunks

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
  uint64_t work = 0;                                 // zone pairs looked at while building (a measure of the font's shape; the load limit is kMaxWork)
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
  std::vector<float> d; double sr = 48000, ls = 0, le = 0, k = 1; bool ok = false, present = false, tooBig = false, overWork = false;   // present: sfSample ran for it (SF.smp has the key); ok: it decoded (false: the JS's 1-sample silence fallback)
};
struct DecodeOpts {
  bool pageTail = false;    // reproduce Chrome's decodeAudioData losing the last frame (at most half a short block) of a one-audio-page Ogg stream (default off: libvorbis and stb_vorbis keep every frame; U32-NOTES.md "The missing frames")
  static DecodeOpts page(double ctxRate) { DecodeOpts o; o.ctxRate = ctxRate; o.pageTail = true; return o; }   // what the web page's decodeAudioData does at this context rate (resample, lose the last frame of one-page streams)
  uint64_t presetBytes = (uint64_t)256 << 20;   // budget of one prepare_preset call (decoded PCM bytes): over it the preset is refused (kPrepOverPresetBudget). The real font's whole-keyboard piano is 191 MB (208 MB at 48 kHz)
  uint64_t fontBytes = (uint64_t)512 << 20;     // budget of all presets prepared from one font at once: the host keeps a counter in `fontUsed` (prepare_preset adds, the host subtracts a PresetPcm's bytes when it drops it)
  uint64_t* fontUsed = nullptr;
  // WORK budgets (prepare_preset): every decode, kept or thrown away, is charged in work units = kWorkPerOpen per stream opened + the stream's input bytes + the frames the decoder produced (+ the frames a resampler writes). A preset that needs more is
  // refused (kPrepOverWorkBudget), whatever it keeps. The byte budgets above only count what is KEPT, so they cannot stop a font whose samples decode in full and are then discarded (a hostile granule, NaN, a failed resample).
  // Measured here (2 cores, Release): 80 to 90 million units are one second of a long stream's decode, and opening a stream costs 0.35 ms whatever its size (hence kWorkPerOpen), so the limits are about 3 s / 6 s of decode work. FluidR3 GM: the most any of its 197 presets needs (whole keyboard, resampled to 48 kHz) is about 119 million units (`test_sf gmall`), so the preset limit is 2.3x that and the font limit 4.7x.
  uint64_t presetWork = (uint64_t)1 << 28;      // work units of one prepare_preset call (268 million)
  uint64_t fontWork = (uint64_t)1 << 29;        // work units of all presets prepared from one font at once: the host keeps a counter in `fontWorkUsed` (prepare_preset adds a kept preset's PresetPcm::work, the host subtracts it when it drops it)
  uint64_t* fontWorkUsed = nullptr;
  size_t maxBytes = 0;                          // (decode_sample: refuse a sample that would be larger than this many bytes; 0 = no limit; prepare_preset sets it from the budgets)
  double ctxRate = 0;       // 0: keep each sample's own rate (default). >0: resample to this rate as the page's decodeAudioData does (sf_decode.cpp: SF3 only; see U32-NOTES.md "sample rate")
};
// The page's resampler as fitted to Chrome (see sf_decode.cpp); false if the result would exceed kMaxSampleFrames. Exposed for the tests.
bool sf_resample(std::vector<float>& d, double from, double to);
// sfSample(font, i): decode one sample (control thread; allocates). A sample the JS could not decode (bad Ogg, a failed allocation) is the fallback {d: 1 zero, sr: 48000, ls: 0, le: 0, k: 1}.
void decode_sample(const Font& f, uint32_t i, const DecodeOpts& o, Pcm& out);
// A preset's samples (sfPreparePreset): the zones' samples and each stereo sample's partner. `byIndex` is sized to the font's sample count; an entry that is not wanted has present == false.
struct PresetPcm {
  std::vector<Pcm> byIndex; uint64_t bytes = 0, work = 0;   // work: the decode work units charged for it (the host subtracts it from `fontWorkUsed` when it drops the PresetPcm)
  const Pcm* get(uint32_t i) const { return i < byIndex.size() && byIndex[i].present ? &byIndex[i] : nullptr; }   // the JS's SF.smp.get / D.smp[i]: undefined when not decoded
};
// `keys`: null = every zone of the preset (the whole keyboard); else 128 flags, and only the zones that cover a flagged key (any velocity) are decoded, with their stereo partners. A key whose zones are not
// decoded plays nothing (the live note refuses it, the render returns 0): the host lists the keys its song and its pad use (U32-NOTES.md "Memory"). Same samples, same bits, less RAM.
enum PrepareStatus { kPrepOk = 0, kPrepNoPreset = 1, kPrepOverPresetBudget = 2, kPrepOverFontBudget = 3, kPrepOverWorkBudget = 4 };
PrepareStatus prepare_preset(const Font& f, const Preset* pr, const DecodeOpts& o, PresetPcm& out, const uint8_t* keys = nullptr);   // control thread; allocates; the host calls it before the pad plays

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
