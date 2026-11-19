// U32: sfSample / sfPreparePreset (index.html). SF2: 16-bit PCM frames. SF3: an Ogg Vorbis stream per sample, decoded with the pinned stb_vorbis (third_party/PINS.md) from memory (no file I/O): channel 0 only, as the page's
// `getChannelData(0)`. Control thread only: it allocates, and stb_vorbis allocates with malloc.
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include "rt/jsmath.h"
#include "sf/sf.h"
#include "sf/stb_api.h"

namespace thunder {
namespace sf {
namespace {

void fallback(Pcm& o) { o.d.assign(1, 0.0f); o.sr = 48000; o.ls = 0; o.le = 0; o.k = 1; o.ok = false; o.present = true; }

// What the Ogg page headers say (nothing is decoded here): the number of pages with a granule position above 0, the short block size from the identification header, and the LAST page's granule position
// (-1 if there is none or it is the "unset" value). Bounded walk; a truncated or malformed page ends it.
struct OggScan { int audioPages = 0; int bs0 = 0; int64_t lastGranule = -1; };
OggScan oggScan(const uint8_t* p, size_t n) {
  OggScan r; size_t pos = 0;
  while (pos + 27 <= n && std::memcmp(p + pos, "OggS", 4) == 0) {
    const size_t segs = p[pos + 26]; if (pos + 27 + segs > n) break;
    size_t body = 0; for (size_t i = 0; i < segs; i++) body += p[pos + 27 + i];
    int64_t g = 0; std::memcpy(&g, p + pos + 6, 8);   // (little-endian hosts; Android, iOS, Mac, Windows all are)
    if (g > 0) r.audioPages++;
    r.lastGranule = g < 0 ? -1 : g;
    const size_t b0 = pos + 27 + segs;
    if (r.bs0 == 0 && body >= 29 && b0 + 29 <= n && p[b0] == 1 && std::memcmp(p + b0 + 1, "vorbis", 6) == 0) r.bs0 = 1 << (p[b0 + 28] & 15);
    if (b0 + body > n) break;
    pos = b0 + body;
  }
  return r;
}

// Chrome's decodeAudioData (FFmpeg's Ogg handling) loses frames at the end of a stream whose audio is all on ONE page (the last, with the end-of-stream granule): the last frame, at most half the short block size.
// Measured on all 1186 samples of FluidR3Mono_GM.sf3 in Chrome 153 (U32-NOTES.md "The missing frames"): 1165 follow this rule exactly and 17 tiny streams fail to decode in Chrome (which is the same rule: the whole stream
// is dropped), 4 do not (234, 235, 704, 253). libvorbis returns exactly the last granule position. This is OFF by default and only for reproducing the page (`DecodeOpts.pageTail`).
size_t pageTailLoss(const OggScan& sc, size_t lastFrame) {
  if (sc.audioPages != 1 || sc.bs0 <= 0) return 0;
  return std::min<size_t>(lastFrame, (size_t)sc.bs0 / 2);
}

// kDecodeOk, or why not. The frames of a stream are bounded by what its BYTES can hold (a Vorbis audio packet is at least a byte and decodes to at most 4096 frames), by kMaxSampleFrames and by `maxFrames` (the budget).
// `work` (null: unlimited) is what prepare_preset has left to spend: the stream's bytes (and kWorkPerOpen) are charged before it is opened and every decoded frame as it is decoded; over it: kDecodeWork.
enum DecodeRes { kDecodeOk, kDecodeBad, kDecodeTooBig, kDecodeWork };
bool charge(uint64_t* work, uint64_t units) { if (!work) return true; if (units > *work) return false; *work -= units; return true; }
// Closes the stb_vorbis handle on every way out, including an exception (d.insert may throw bad_alloc: the handle, about 170 KB of decoder setup, used to leak: verify-242 F1).
struct VorbisGuard { stb_vorbis* v; ~VorbisGuard() { if (v) stb_vorbis_close(v); } VorbisGuard(const VorbisGuard&) = delete; VorbisGuard& operator=(const VorbisGuard&) = delete; };
DecodeRes decodeOgg(const uint8_t* p, size_t n, std::vector<float>& d, double& sr, bool pageTail, size_t maxFrames, uint64_t* work) {
  if (n == 0 || n > (size_t)INT_MAX) return kDecodeBad;
  // The LAST PAGE's granule position is the stream's length and is read from the page headers, BEFORE any decoding: 0 means the stream is empty (below, a trim to 0 frames is kDecodeBad after a full decode: a hostile stream
  // made the control thread decode megabytes for nothing). A granule above what the stream decodes to is harmless (it only shortens), so only the zero one is refused up front.
  const OggScan sc = oggScan(p, n);
  if (sc.lastGranule == 0) return kDecodeBad;
  if (!charge(work, (uint64_t)n + kWorkPerOpen)) return kDecodeWork;
  int err = 0; stb_vorbis* v = stb_vorbis_open_memory(p, (int)n, &err, nullptr);
  if (!v) return kDecodeBad;
  VorbisGuard guard{v};
  const stb_vorbis_info info = stb_vorbis_get_info(v);
  if (info.channels < 1 || info.sample_rate < kMinStreamRate || info.sample_rate > kMaxStreamRate) { return kDecodeBad; }   // 4000..192000 Hz: anything else is a hostile header
  sr = (double)info.sample_rate;
  const size_t byBytes = n * 4096 + 8192; size_t cap = std::min(kMaxSampleFrames, byBytes); const bool budgeted = maxFrames > 0 && maxFrames < cap; if (budgeted) cap = maxFrames;
  d.clear(); size_t lastFrame = 0;
  for (;;) {   // frame by frame (what get_samples_float does underneath): the size of the last frame is what pageTail needs
    int ch = 0; float** o = nullptr; const int got = stb_vorbis_get_frame_float(v, &ch, &o);
    if (got <= 0 || ch < 1) break;
    if (d.size() + (size_t)got > cap) { return budgeted ? kDecodeTooBig : kDecodeBad; }
    if (!charge(work, (uint64_t)got)) { return kDecodeWork; }
    d.insert(d.end(), o[0], o[0] + got); lastFrame = (size_t)got;
  }
  stb_vorbis_close(v); guard.v = nullptr;
  // The stream's length is its LAST PAGE's granule position (the Vorbis I rule; libvorbis does exactly this): stb_vorbis only trims the last block, so a granule that cuts more than that is applied here, shortening only
  // (a granule above the decoded length, or none, leaves the length as decoded).
  if (sc.lastGranule >= 0 && (uint64_t)sc.lastGranule < (uint64_t)d.size()) { d.resize((size_t)sc.lastGranule); lastFrame = std::min(lastFrame, d.size()); }
  if (pageTail && !d.empty()) { const size_t cut = pageTailLoss(sc, lastFrame); d.resize(d.size() - std::min(cut, d.size())); }
  // A corrupt stream can decode to infinities or NaN (a hostile codebook): the 1-sample silence instead, so nothing non-finite ever reaches a voice's filter state (U32-NOTES.md "Choices": the JS has no such check).
  for (float x : d) if (!std::isfinite(x)) return kDecodeBad;
  return d.empty() ? kDecodeBad : kDecodeOk;
}

// Resampling to the context rate as Chrome's decodeAudioData does it (Blink's SincResampler, measured, not copied: the constants were found by fitting Chrome's own output, to -136 dB on most of a long sample):
// a 32-tap Blackman (alpha 0.16) windowed sinc, 32 kernel sub-sample offsets (+1) with linear interpolation between two neighbours, cutoff 0.9 of the lower Nyquist, no normalisation, output length floor(n * ratio),
// input before the first and after the last frame is zero. Linear-phase, so no delay. Control thread only (it allocates), about 64 multiply-adds per output frame.
constexpr double kPi = 3.14159265358979323846;   // not M_PI: MSVC does not define it without _USE_MATH_DEFINES
bool resampleTo(std::vector<float>& d, double from, double to, size_t maxFrames, uint64_t* work = nullptr, bool* overWork = nullptr) {
  if (d.empty() || !(from > 0) || !(to > 0) || from == to) return true;
  const double r = to / from, sc = (r < 1 ? r : 1.0) * 0.9; const size_t n = d.size();
  const double mf = std::floor((double)n * r); if (mf < 1 || mf > (double)kMaxSampleFrames || (maxFrames > 0 && mf > (double)maxFrames)) return false;
  const size_t m = (size_t)mf;
  if (!charge(work, (uint64_t)m)) { if (overWork) *overWork = true; return false; }
  constexpr int KS = 32, KO = 32; std::vector<double> K((size_t)(KO + 1) * KS);
  for (int off = 0; off <= KO; off++) { const double so = (double)off / KO;
    for (int i = 0; i < KS; i++) { const double x = (i - so) / KS, w = 0.42 - 0.5 * std::cos(2 * kPi * x) + 0.08 * std::cos(4 * kPi * x), ps = kPi * (i - KS / 2 - so);
      K[(size_t)off * KS + (size_t)i] = w * (ps == 0 ? sc : std::sin(sc * ps) / ps); } }
  std::vector<float> o(m);
  for (size_t i = 0; i < m; i++) {
    const double t = (double)i / r; const long n0 = (long)std::floor(t); const double q = (t - (double)n0) * KO; const int lo = (int)q; const double g = q - lo; double a = 0, b2 = 0;
    const double* k0 = &K[(size_t)lo * KS]; const double* k1 = k0 + KS;
    for (int k = 0; k < KS; k++) { const long j = n0 + k - KS / 2; if (j < 0 || j >= (long)n) continue; const double v = (double)d[(size_t)j]; a += v * k0[k]; b2 += v * k1[k]; }
    o[i] = (float)((1 - g) * a + g * b2);
  }
  d.swap(o); return true;
}

}  // namespace

bool sf_resample(std::vector<float>& d, double from, double to) { return resampleTo(d, from, to, 0); }

namespace {
// Streams already decoded in this prepare_preset, by their byte range within the font (offset, length: the sample headers may alias one stream any number of times): the result is the same whatever header names it, so a stream
// is decoded ONCE, success or failure. A success is remembered as the index of the first sample that holds it (its PCM is copied, which the byte budgets already bound); a refusal for the budget is not remembered (it ends the preset).
struct StreamCache { struct E { bool ok; uint32_t idx; }; std::map<std::pair<size_t, size_t>, E> m; const std::vector<Pcm>* by = nullptr; };

void decodeSample(const Font& f, uint32_t i, const DecodeOpts& opt, Pcm& o, uint64_t* work, StreamCache* cache) {
  o = Pcm();
  if (i >= f.samples.size()) { fallback(o); return; }
  const SampleHdr& s = f.samples[i];
  if (s.sr < kMinHdrRate || s.sr > kMaxHdrRate) { fallback(o); return; }   // an absurd header rate
  if (opt.ctxRate != 0 && !(opt.ctxRate >= kMinStreamRate && opt.ctxRate <= kMaxStreamRate)) { fallback(o); return; }   // an absurd (or NaN) context rate
  const size_t maxFrames = opt.maxBytes / sizeof(float);   // 0: no budget
  if (s.type & 0x10) {   // SF3: an Ogg Vorbis stream at start..end (bytes); loops are frames from the sample's start
    const size_t a = std::min<size_t>(s.start, f.smplN), b = std::min<size_t>(s.end, f.smplN);   // smpl.slice(start, end)
    std::vector<float> d; double sr = 0; DecodeRes r = kDecodeBad; const std::pair<size_t, size_t> key(a, b - (b > a ? a : b));
    bool hit = false;
    if (b > a && cache) { const auto it = cache->m.find(key);
      if (it != cache->m.end()) { hit = true;
        if (!it->second.ok) r = kDecodeBad;
        else { const Pcm& src = (*cache->by)[it->second.idx]; if (maxFrames > 0 && src.d.size() > maxFrames) r = kDecodeTooBig; else { d = src.d; sr = src.sr; r = kDecodeOk; } } } }
    if (!hit && b > a) {
      r = decodeOgg(f.smpl + a, b - a, d, sr, opt.pageTail, maxFrames, work);
      if (r == kDecodeOk && opt.ctxRate > 0 && opt.ctxRate != sr) { bool ow = false; if (resampleTo(d, sr, opt.ctxRate, maxFrames, work, &ow)) sr = opt.ctxRate; else r = ow ? kDecodeWork : maxFrames ? kDecodeTooBig : kDecodeBad; }
      if (cache && (r == kDecodeOk || r == kDecodeBad)) cache->m[key] = StreamCache::E{r == kDecodeOk, i};   // (the caller keeps `o` at index i)
    }
    if (r != kDecodeOk) { fallback(o); o.tooBig = r == kDecodeTooBig; o.overWork = r == kDecodeWork; return; }
    const double k = sr / (double)s.sr;
    o.d = std::move(d); o.sr = sr; o.ls = js_round((double)s.ls * k); o.le = js_round((double)s.le * k); o.k = k; o.ok = true; o.present = true;
    return;
  }
  // SF2: 16-bit PCM frames; loops are absolute frames. The sample is clamped to the frames the smpl chunk really holds (the JS would read zeros past it; a header may claim 2^25 frames in a tiny file).
  const uint64_t have = (uint64_t)s.start * 2 < (uint64_t)f.smplN ? ((uint64_t)f.smplN - (uint64_t)s.start * 2) / 2 : 0;
  const uint64_t claimed = s.end > s.start ? (uint64_t)s.end - s.start : 0;
  const uint64_t n = std::min(claimed, have);
  if (n > kMaxSampleFrames) { fallback(o); return; }
  if (maxFrames > 0 && n > maxFrames) { fallback(o); o.tooBig = true; return; }
  if (!charge(work, n)) { fallback(o); o.overWork = true; return; }
  o.d.assign((size_t)n, 0.0f);
  for (uint64_t j = 0; j < n; j++) {
    const size_t at = (size_t)(s.start + j) * 2; const int16_t v = (int16_t)((uint16_t)f.smpl[at] | ((uint16_t)f.smpl[at + 1] << 8));
    o.d[(size_t)j] = (float)((double)v / 32768);
  }
  o.sr = (double)s.sr; o.ls = (double)s.ls - (double)s.start; o.le = (double)s.le - (double)s.start; o.k = 1; o.ok = true; o.present = true;
}
}  // namespace

void decode_sample(const Font& f, uint32_t i, const DecodeOpts& opt, Pcm& o) { decodeSample(f, i, opt, o, nullptr, nullptr); }

PrepareStatus prepare_preset(const Font& f, const Preset* pr, const DecodeOpts& opt, PresetPcm& out, const uint8_t* keys) {
  std::vector<Pcm>().swap(out.byIndex); out.bytes = 0; out.work = 0; out.tableBytes = 0;
  if (!pr) return kPrepNoPreset;
  // The per-sample-header table (one Pcm each, about 70 bytes: a hostile font may have millions of headers, and every pad that uses it gets its own table) is part of the preset's budget and is counted BEFORE it is allocated.
  const uint64_t tableBytes = (uint64_t)f.samples.size() * sizeof(Pcm);
  if (tableBytes > opt.presetBytes) return kPrepOverPresetBudget;
  out.byIndex.resize(f.samples.size()); out.tableBytes = tableBytes;
  const uint64_t fontUsed = opt.fontUsed ? *opt.fontUsed : 0;
  PrepareStatus st = kPrepOk;
  const uint64_t fontWorkUsed = opt.fontWorkUsed ? *opt.fontWorkUsed : 0;
  uint64_t workSpent = 0;   // work units charged so far by this call (kept or not)
  StreamCache cache; cache.by = &out.byIndex;
  // the work left: the preset's budget and the font's, whichever is smaller
  auto workRoom = [&]() -> uint64_t { const uint64_t a = opt.presetWork > workSpent ? opt.presetWork - workSpent : 0; const uint64_t used = fontWorkUsed + workSpent; const uint64_t b = opt.fontWork > used ? opt.fontWork - used : 0; return std::min(a, b); };
  // the room left: the preset's budget and the font's, whichever is smaller
  auto room = [&]() -> uint64_t { const uint64_t pb = out.bytes + out.tableBytes; const uint64_t a = opt.presetBytes > pb ? opt.presetBytes - pb : 0; const uint64_t used = fontUsed + out.bytes; const uint64_t b = opt.fontBytes > used ? opt.fontBytes - used : 0; return std::min(a, b); };
  auto binding = [&]() -> PrepareStatus {   // which budget is the smaller one right now
    const uint64_t pb = out.bytes + out.tableBytes, a = opt.presetBytes > pb ? opt.presetBytes - pb : 0, used = fontUsed + out.bytes, b = opt.fontBytes > used ? opt.fontBytes - used : 0;
    return b < a ? kPrepOverFontBudget : kPrepOverPresetBudget; };
  auto add = [&](uint32_t s) {
    if (st != kPrepOk || out.byIndex[s].present) return;
    const uint64_t r = room(); if (r < sizeof(float)) { st = binding(); return; }
    DecodeOpts o2 = opt; o2.maxBytes = (size_t)std::min<uint64_t>(r, (uint64_t)1 << 40);
    uint64_t wl = workRoom(); const uint64_t wl0 = wl;
    decodeSample(f, s, o2, out.byIndex[s], &wl, &cache);
    workSpent += wl0 - wl;   // (also what a refused or failed decode spent)
    if (out.byIndex[s].overWork) { st = kPrepOverWorkBudget; return; }
    if (out.byIndex[s].tooBig) { st = binding(); return; }
    out.bytes += out.byIndex[s].d.size() * sizeof(float);
  };
  for (uint32_t i = 0; i < pr->nz && st == kPrepOk; i++) { const Zone& z = f.zones[pr->z0 + i];
    if (keys) { bool any = false; for (int k = std::max(0, z.klo); k <= std::min(127, z.khi) && !any; k++) any = keys[k] != 0; if (!any) continue; }
    add(z.s); const SampleHdr& s = f.samples[z.s]; if ((s.type & 6) && s.link < f.samples.size()) add(s.link); }
  if (st != kPrepOk) { std::vector<Pcm>().swap(out.byIndex); out.bytes = 0; out.tableBytes = 0; out.work = workSpent; return st; }   // (after a refusal `work` is what the call SPENT, informational: the host's counter is not touched)   // refused: nothing is kept, the pad plays nothing
  out.work = workSpent;
  if (opt.fontUsed) *opt.fontUsed += out.bytes;
  if (opt.fontWorkUsed) *opt.fontWorkUsed += out.work;
  return kPrepOk;
}

}  // namespace sf
}  // namespace thunder
