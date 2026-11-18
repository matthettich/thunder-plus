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

bool decodeOgg(const uint8_t* p, size_t n, std::vector<float>& d, double& sr) {
  if (n == 0 || n > (size_t)INT_MAX) return false;
  int err = 0; stb_vorbis* v = stb_vorbis_open_memory(p, (int)n, &err, nullptr);
  if (!v) return false;
  const stb_vorbis_info info = stb_vorbis_get_info(v);
  if (info.channels < 1 || info.sample_rate == 0) { stb_vorbis_close(v); return false; }
  sr = (double)info.sample_rate;
  d.clear();
  float tmp[4096]; float* ch[1] = {tmp};
  for (;;) {
    const int got = stb_vorbis_get_samples_float(v, 1, ch, 4096);
    if (got <= 0) break;
    if (d.size() + (size_t)got > kMaxSampleFrames) { stb_vorbis_close(v); return false; }
    d.insert(d.end(), tmp, tmp + got);
  }
  stb_vorbis_close(v);
  return !d.empty();
}

}  // namespace

void decode_sample(const Font& f, uint32_t i, const DecodeOpts&, Pcm& o) {
  o = Pcm();
  if (i >= f.samples.size()) { fallback(o); return; }
  const SampleHdr& s = f.samples[i];
  if (s.type & 0x10) {   // SF3: an Ogg Vorbis stream at start..end (bytes); loops are frames from the sample's start
    const size_t a = std::min<size_t>(s.start, f.smplN), b = std::min<size_t>(s.end, f.smplN);   // smpl.slice(start, end)
    std::vector<float> d; double sr = 0;
    if (b <= a || !decodeOgg(f.smpl + a, b - a, d, sr)) { fallback(o); return; }
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

void prepare_preset(const Font& f, const Preset* pr, const DecodeOpts& opt, PresetPcm& out) {
  out.byIndex.clear(); out.byIndex.resize(f.samples.size()); out.bytes = 0;
  if (!pr) return;
  auto add = [&](uint32_t s) { if (out.byIndex[s].present) return; decode_sample(f, s, opt, out.byIndex[s]); out.bytes += out.byIndex[s].d.size() * sizeof(float); };
  for (uint32_t i = 0; i < pr->nz; i++) { const Zone& z = f.zones[pr->z0 + i]; add(z.s); const SampleHdr& s = f.samples[z.s]; if ((s.type & 6) && s.link < f.samples.size()) add(s.link); }
}

}  // namespace sf
}  // namespace thunder
