// U32: sfSample / sfPreparePreset (index.html). SF2: 16-bit PCM frames. SF3: an Ogg Vorbis stream per sample, decoded with the pinned stb_vorbis (third_party/PINS.md) from memory (no file I/O): channel 0 only, as the page's
// `getChannelData(0)`. Control thread only: it allocates, and stb_vorbis allocates with malloc.
#include <algorithm>
#include <climits>
#include <cmath>
#include <cstring>

#include "rt/jsmath.h"
#include "sf/sf.h"
#include "sf/stb_api.h"

namespace thunder {
namespace sf {
namespace {

void fallback(Pcm& o) { o.d.assign(1, 0.0f); o.sr = 48000; o.ls = 0; o.le = 0; o.k = 1; o.ok = false; o.present = true; }

// Chrome's decodeAudioData (FFmpeg's Ogg handling) loses frames at the end of a stream whose audio is all on ONE page (the last, with the end-of-stream granule): the last frame, at most half the short block size.
// Measured on all 1186 samples of FluidR3Mono_GM.sf3 in Chrome 153 (U32-NOTES.md "The missing frames"): 1182 follow this rule exactly. libvorbis and stb_vorbis return every frame up to the granule position, so this is
// OFF by default and only for reproducing the page (`DecodeOpts.pageTail`). Returns the frames to drop (0 if the stream has more than one audio page or cannot be read).
size_t pageTailLoss(const uint8_t* p, size_t n, size_t lastFrame) {
  size_t pos = 0; int audioPages = 0, bs0 = 0;
  while (pos + 27 <= n && std::memcmp(p + pos, "OggS", 4) == 0) {
    const size_t segs = p[pos + 26]; if (pos + 27 + segs > n) break;
    size_t body = 0; for (size_t i = 0; i < segs; i++) body += p[pos + 27 + i];
    int64_t g = 0; std::memcpy(&g, p + pos + 6, 8);   // (little-endian hosts; Android, iOS, Mac, Windows all are)
    if (g > 0) audioPages++;
    const size_t b0 = pos + 27 + segs;
    if (bs0 == 0 && body >= 29 && b0 + 29 <= n && p[b0] == 1 && std::memcmp(p + b0 + 1, "vorbis", 6) == 0) bs0 = 1 << (p[b0 + 28] & 15);
    if (b0 + body > n) break;
    pos = b0 + body;
  }
  if (audioPages != 1 || bs0 <= 0) return 0;
  return std::min<size_t>(lastFrame, (size_t)bs0 / 2);
}

bool decodeOgg(const uint8_t* p, size_t n, std::vector<float>& d, double& sr, bool pageTail) {
  if (n == 0 || n > (size_t)INT_MAX) return false;
  int err = 0; stb_vorbis* v = stb_vorbis_open_memory(p, (int)n, &err, nullptr);
  if (!v) return false;
  const stb_vorbis_info info = stb_vorbis_get_info(v);
  if (info.channels < 1 || info.sample_rate == 0) { stb_vorbis_close(v); return false; }
  sr = (double)info.sample_rate;
  d.clear(); size_t lastFrame = 0;
  for (;;) {   // frame by frame (what get_samples_float does underneath): the size of the last frame is what pageTail needs
    int ch = 0; float** o = nullptr; const int got = stb_vorbis_get_frame_float(v, &ch, &o);
    if (got <= 0 || ch < 1) break;
    if (d.size() + (size_t)got > kMaxSampleFrames) { stb_vorbis_close(v); return false; }
    d.insert(d.end(), o[0], o[0] + got); lastFrame = (size_t)got;
  }
  stb_vorbis_close(v);
  if (pageTail && !d.empty()) { const size_t cut = pageTailLoss(p, n, lastFrame); d.resize(d.size() - std::min(cut, d.size())); }
  // A corrupt stream can decode to infinities or NaN (a hostile codebook): the 1-sample silence instead, so nothing non-finite ever reaches a voice's filter state (U32-NOTES.md "Choices": the JS has no such check).
  for (float x : d) if (!std::isfinite(x)) return false;
  return !d.empty();
}

// Band-limited resampling to the context rate, as decodeAudioData does for a stream at another rate: a Kaiser-windowed sinc (beta 9, 16 zero crossings each side, window from a 4096-point table), in double,
// cutoff at the lower Nyquist. Chrome's own resampler is not specified bit for bit, so this reproduces it to within its filter error, not exactly (U32-NOTES.md "sample rate"). Cost: about 32 multiply-adds per output frame.
double bessel0(double x) { double s = 1, t = 1; for (int k = 1; k < 60; k++) { t *= (x / (2 * k)) * (x / (2 * k)); s += t; if (t < 1e-18 * s) break; } return s; }
void resampleTo(std::vector<float>& d, double from, double to) {
  if (d.empty() || !(from > 0) || !(to > 0) || from == to) return;
  const double r = to / from, fc = std::min(1.0, r), W = 16.0; const size_t n = d.size();
  const size_t m = (size_t)std::floor((double)n * r); if (m == 0 || m > kMaxSampleFrames) return;
  constexpr int TN = 4096; static double tab[TN + 2]; static bool tabOk = false;   // (a function-local table: filled once, control thread only)
  if (!tabOk) { const double b9 = bessel0(9.0); for (int i = 0; i <= TN + 1; i++) { const double u = std::min(1.0, (double)i / TN); tab[i] = bessel0(9.0 * std::sqrt(std::max(0.0, 1 - u * u))) / b9; } tabOk = true; }
  std::vector<float> o(m); const double half = W / fc, inv = 1.0 / half;
  for (size_t i = 0; i < m; i++) {
    const double t = (double)i / r; const long a = (long)std::ceil(t - half), z = (long)std::floor(t + half); double acc = 0;
    const double tf = std::floor(t); const double sp = fc == 1.0 ? (((long)tf & 1) ? -1.0 : 1.0) * std::sin(M_PI * (t - tf)) : 0;   // sin(pi t) from the fraction only: a large t would lose the digits
    for (long j = std::max(0L, a); j <= z && j < (long)n; j++) {
      const double x = t - (double)j, u = std::fabs(x) * inv; if (u >= 1) continue;
      const double q = u * TN; const int qi = (int)q; const double w = tab[qi] + (tab[qi + 1] - tab[qi]) * (q - qi);
      double sx; if (x == 0) sx = 1; else if (fc == 1.0) sx = ((j & 1) ? -sp : sp) / (M_PI * x); else sx = std::sin(M_PI * fc * x) / (M_PI * fc * x);
      acc += (double)d[(size_t)j] * fc * sx * w;
    }
    o[i] = (float)acc;
  }
  d.swap(o);
}

}  // namespace

void decode_sample(const Font& f, uint32_t i, const DecodeOpts& opt, Pcm& o) {
  o = Pcm();
  if (i >= f.samples.size()) { fallback(o); return; }
  const SampleHdr& s = f.samples[i];
  if (s.type & 0x10) {   // SF3: an Ogg Vorbis stream at start..end (bytes); loops are frames from the sample's start
    const size_t a = std::min<size_t>(s.start, f.smplN), b = std::min<size_t>(s.end, f.smplN);   // smpl.slice(start, end)
    std::vector<float> d; double sr = 0;
    if (b <= a || !decodeOgg(f.smpl + a, b - a, d, sr, opt.pageTail)) { fallback(o); return; }
    if (opt.ctxRate > 0 && opt.ctxRate != sr) { resampleTo(d, sr, opt.ctxRate); sr = opt.ctxRate; }
    const double k = sr / (double)s.sr;
    o.d = std::move(d); o.sr = sr; o.ls = js_round((double)s.ls * k); o.le = js_round((double)s.le * k); o.k = k; o.ok = true; o.present = true;
    return;
  }
  // SF2: 16-bit PCM frames; loops are absolute frames
  const int64_t n = std::max<int64_t>(0, (int64_t)s.end - (int64_t)s.start);
  if ((size_t)n > kMaxSampleFrames) { fallback(o); return; }
  o.d.assign((size_t)n, 0.0f);
  for (int64_t j = 0; j < n && (int64_t)(s.start + (uint64_t)j) * 2 + 1 < (int64_t)f.smplN; j++) {
    const size_t at = (size_t)(s.start + (uint64_t)j) * 2; const int16_t v = (int16_t)((uint16_t)f.smpl[at] | ((uint16_t)f.smpl[at + 1] << 8));
    o.d[(size_t)j] = (float)((double)v / 32768);
  }
  o.sr = (double)s.sr; o.ls = (double)s.ls - (double)s.start; o.le = (double)s.le - (double)s.start; o.k = 1; o.ok = true; o.present = true;
}

void prepare_preset(const Font& f, const Preset* pr, const DecodeOpts& opt, PresetPcm& out, const uint8_t* keys) {
  out.byIndex.clear(); out.byIndex.resize(f.samples.size()); out.bytes = 0;
  if (!pr) return;
  auto add = [&](uint32_t s) { if (out.byIndex[s].present) return; decode_sample(f, s, opt, out.byIndex[s]); out.bytes += out.byIndex[s].d.size() * sizeof(float); };
  for (uint32_t i = 0; i < pr->nz; i++) { const Zone& z = f.zones[pr->z0 + i];
    if (keys) { bool any = false; for (int k = std::max(0, z.klo); k <= std::min(127, z.khi) && !any; k++) any = keys[k] != 0; if (!any) continue; }
    add(z.s); const SampleHdr& s = f.samples[z.s]; if ((s.type & 6) && s.link < f.samples.size()) add(s.link); }
}

}  // namespace sf
}  // namespace thunder
