// U34a: the public C API of the offline song render (include/thunder/render.h): a pull model over SongRenderer. See the header for the contract.
// A failed allocation is a std::bad_alloc (the effects' malloc throws it too: dsp/fx_alloc.cpp); it is caught here and returned as THUNDER_RENDER_E_MEMORY. The files that
// make a handle are built WITH C++ exceptions (core/CMakeLists.txt) so that what a failed create has made is freed on the way out (RAII). No exception leaves an
// extern "C" function. Built WITHOUT exceptions (the wasm build) the try / catch below compile away (TR_TRY, TR_CATCH_*): an allocation failure is then not caught
// and the process aborts (render.h, MEMORY). test render_api_noexc compiles this file that way.
#include "thunder/render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

#include "render/render.h"
#include "render/players.h"
#include "engine/xeng.h"
#include "sf/sf.h"
#include "rt/json.h"
#include "rt/num.h"

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || (defined(_MSC_VER) && defined(_CPPUNWIND))
#define TR_TRY try
#define TR_CATCH_BADALLOC(...) catch (const std::bad_alloc&) { __VA_ARGS__ }
#define TR_CATCH_ALL(...) catch (...) { __VA_ARGS__ }
#else
#define TR_TRY
#define TR_CATCH_BADALLOC(...)
#define TR_CATCH_ALL(...)
#endif

using namespace thunder;

// A font handed in with add_font: the copy of the file's bytes (parse() keeps a VIEW of it), the parsed tables, and the counters prepare_preset keeps per font.
struct RenderFont {
  std::vector<uint8_t> bytes; sf::Font font; uint64_t used = 0, workUsed = 0; size_t held = 0;
};

struct thunder_render {
  Song song;                           // must outlive R
  JsonDoc varsDoc;                     // the vars JSON (SeqVars may keep handles into it)
  SeqVars vars;
  RenderPads pads;                     // must outlive R; the buffers are in `store`
  std::vector<float> store[kRenderPads];
  size_t padBytes = 0;                 // pads + variations + fonts (copy and tables) + prepared SoundFont PCM
  std::unique_ptr<RenderFont> fonts[THUNDER_RENDER_MAX_FONTS];
  std::unique_ptr<sf::PresetPcm> sfPcm[kRenderPads]; int sfFont[kRenderPads]; size_t sfBytes[kRenderPads];
  std::unique_ptr<SongRenderer> R;
  // options
  double sr = 48000; bool useFrames = false; int64_t frames = 0, maxFrames = 0;
  int64_t skip = 0;                    // raw frames dropped at the start (the latency)
  // the stream
  float q[2 * kRenderQuantum];         // the last quantum: left, then right
  int qPos = kRenderQuantum;           // next unread frame of q (kRenderQuantum: empty)
  int64_t raw = 0, skipLeft = 0, outPos = 0;
  int64_t total = -1;                  // frames of output once known
  int stem = 0;                        // opts.stem (0: the mix)
  int64_t est = -2;                    // length_estimate's cached answer (-2: not computed yet)
  bool started = false, ended = false, hitLimit = false, dead = false;
};

namespace {
constexpr int64_t kMaxFrames = (int64_t)1 << 40;

void setErr(char* err, size_t cap, const char* msg) {
  if (!err || cap == 0) return;
  std::snprintf(err, cap, "%s", msg);
}

bool fin(double v) { return std::isfinite(v); }

// One more quantum from the song renderer into q, minus what the latency trim drops; then the end test (unknown length only).
void pump(thunder_render* r) {
  float* L = r->q; float* Rr = r->q + kRenderQuantum;
  r->R->block(L, Rr, kRenderQuantum);
  r->raw += kRenderQuantum;
  r->qPos = 0;
  if (r->skipLeft > 0) { const int64_t d = std::min<int64_t>(r->skipLeft, kRenderQuantum); r->qPos = (int)d; r->skipLeft -= d; }
  if (!r->useFrames && !r->ended) {
    const int64_t avail = std::max<int64_t>(0, r->raw - r->skip);   // the output so far, if nothing is cut
    const bool over = r->R->songOver() && r->R->voices() == 0;
    if (over || avail >= r->maxFrames) {
      // The output ends exactly at max_frames: the last quantum is cut to it (not rounded up to the quantum), and hit_limit is set whenever
      // something was cut: the song was still going (not over), or it ended inside this quantum beyond max_frames.
      r->ended = true; r->hitLimit = !over || avail > r->maxFrames; r->total = std::min(avail, r->maxFrames);
    }
  }
}

thunder_render* createImpl(const uint8_t* songBytes, size_t n, const thunder_render_opts* opts, char* err, size_t cap) {
  if (err && cap) err[0] = 0;
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = (uint32_t)sizeof o;
  if (opts) {
    if (opts->size < offsetof(thunder_render_opts, sample_rate)) { setErr(err, cap, "opts.size is too small"); return nullptr; }
#define TR_COPY(f) if (opts->size >= offsetof(thunder_render_opts, f) + sizeof(opts->f)) o.f = opts->f
    TR_COPY(reserved0); TR_COPY(sample_rate); TR_COPY(effects); TR_COPY(master_chain); TR_COPY(pdc_mode); TR_COPY(page_timing); TR_COPY(use_frames);
    TR_COPY(reserved1); TR_COPY(frames); TR_COPY(max_frames); TR_COPY(vars_json); TR_COPY(vars_bytes); TR_COPY(stem); TR_COPY(reserved2);
#undef TR_COPY
  }
  if (o.stem < 0 || o.stem > kSongNT) { setErr(err, cap, "opts.stem must be 0..32"); return nullptr; }
  if (o.reserved0 != 0 || o.reserved1 != 0 || o.reserved2 != 0) { setErr(err, cap, "opts.reserved must be 0"); return nullptr; }
  double sr = o.sample_rate == 0 ? 48000.0 : o.sample_rate;
  if (!fin(sr) || sr < 8000 || sr > 384000) { setErr(err, cap, "opts.sample_rate must be 8000..384000"); return nullptr; }
  if ((o.effects | 1) != 1 || (o.master_chain | 1) != 1 || (o.page_timing | 1) != 1 || (o.use_frames | 1) != 1) { setErr(err, cap, "opts flags must be 0 or 1"); return nullptr; }
  if (o.pdc_mode < 0 || o.pdc_mode > 2) { setErr(err, cap, "opts.pdc_mode must be 0..2"); return nullptr; }
  if (o.use_frames && (o.frames < 0 || o.frames > kMaxFrames)) { setErr(err, cap, "opts.frames must be 0..2^40"); return nullptr; }
  if (o.max_frames < 0 || o.max_frames > kMaxFrames) { setErr(err, cap, "opts.max_frames must be 0..2^40"); return nullptr; }
  if (!o.vars_json && o.vars_bytes) { setErr(err, cap, "opts.vars_json is null but vars_bytes is not 0"); return nullptr; }
  if (o.vars_bytes > THUNDER_RENDER_MAX_VARS_BYTES) { setErr(err, cap, "vars JSON is over the limit"); return nullptr; }
  if (!songBytes) { setErr(err, cap, "song is null"); return nullptr; }
  if (n > THUNDER_RENDER_MAX_SONG_BYTES) { setErr(err, cap, "song is over the size limit"); return nullptr; }

  std::unique_ptr<thunder_render> r(new thunder_render());
  for (int i = 0; i < kRenderPads; i++) { r->sfFont[i] = -1; r->sfBytes[i] = 0; }
  JsonError je;
  if (n == 0 || !r->song.load((const char*)songBytes, n, &je)) {
    if (n != 0 && je.msg && !std::strcmp(je.msg, "out of memory")) { setErr(err, cap, "out of memory"); return nullptr; }   // (the parser's own report: not a bad song)
    char b[200]; std::snprintf(b, sizeof b, "the song bytes are not a song (%s at byte %zu)", n == 0 ? "empty" : (je.msg ? je.msg : "unreadable"), n == 0 ? (size_t)0 : je.offset);
    setErr(err, cap, b); return nullptr;
  }
  if (o.vars_bytes) {
    if (!r->varsDoc.parse((const char*)o.vars_json, o.vars_bytes, &je)) { setErr(err, cap, je.msg && !std::strcmp(je.msg, "out of memory") ? "out of memory" : "vars_json is not a JSON object"); return nullptr; }
    if (!r->varsDoc.root().isObj()) { setErr(err, cap, "vars_json is not a JSON object"); return nullptr; }
    r->vars.load(r->varsDoc.root());
  }
  r->stem = o.stem;
  r->sr = sr; r->useFrames = o.use_frames != 0; r->frames = o.frames;
  r->maxFrames = o.max_frames ? o.max_frames : (int64_t)(sr * 1260);
  SeqOpts so; so.row = r->song.renderFrom; so.once = true;
  r->R.reset(new SongRenderer(r->song, r->vars, r->pads, sr, kRenderQuantum, so));
  r->R->pool().setPageTiming(o.page_timing != 0);
  if (o.stem > 0) r->R->setStem(o.stem - 1);   // first: a stem has no master effects and no master chain (the calls below leave the master alone)
  if (o.effects) r->R->setEffects(true);
  if (o.master_chain) r->R->setMasterChain(master_chain_from_song(r->song.mst, r->song.limiter));
  if (o.pdc_mode) {
    const PdcMode mode = o.pdc_mode == 2 ? PdcMode::On : PdcMode::Auto;
    std::vector<PdcSlot> slots(1024); int ns = r->R->fxBank() ? r->R->fxBank()->pdcSlots(slots.data(), (int)slots.size()) : 0;
    const PdcPlan plan = pdc_plan(bus_plan(r->song.chainPatch, r->song.chainOn), slots.data(), ns, sr, mode);
    r->R->setPlan(plan, mode, 0);
  }
  r->skip = std::max<int64_t>(0, r->R->latencyFrames());
  r->skipLeft = r->skip;
  if (r->useFrames) r->total = r->frames;
  return r.release();
}

bool padOk(int pad) { return pad >= 0 && pad < kRenderPads; }
}  // namespace

extern "C" {

thunder_render* thunder_render_create(const uint8_t* song, size_t song_bytes, const thunder_render_opts* opts, char* err, size_t err_cap) {
  TR_TRY { return createImpl(song, song_bytes, opts, err, err_cap); }
  TR_CATCH_BADALLOC(setErr(err, err_cap, "out of memory"); return nullptr;)
  TR_CATCH_ALL(setErr(err, err_cap, "internal error"); return nullptr;)
}

int thunder_render_set_pad(thunder_render* r, int pad, const float* l, const float* rr, int64_t frames, double sample_rate) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad) || frames < 0) return THUNDER_RENDER_E_ARG;
  TR_TRY {
    // A pad whose buffer changes (set, replaced or removed) loses its variations, as the page's does (core/core-processor.js:337: 'buf' -> varDrop(pad)); they are
    // dropped only once the call has succeeded (a failed call changes nothing), and what they hold counts as freed by the replacement.
    const size_t varB = r->pads.varBytesOfPad(pad);
    if (frames == 0) { r->padBytes -= r->store[pad].size() * sizeof(float); std::vector<float>().swap(r->store[pad]); r->pads.buf[pad] = VoiceBuf(); r->padBytes -= r->pads.dropVarsOfPad(pad); r->pads.chop.setLoop(pad, 0); return 0; }
    if (!l || !fin(sample_rate) || sample_rate < 1 || sample_rate > 1e6) return THUNDER_RENDER_E_ARG;
    if (frames > THUNDER_RENDER_MAX_PAD_FRAMES) return THUNDER_RENDER_E_LIMIT;
    const size_t ch = rr ? 2 : 1, n = (size_t)frames * ch, old = r->store[pad].size() * sizeof(float);
    // The budget bounds what the handle HOLDS, not only what it ends up with. The final state must fit (the old pad and the pad's variations are replaced); then, if
    // what the handle holds now and the new copy fit TOGETHER, the new one is built first and swapped in (a failure leaves the pad as it was); if they do not (a big pad
    // replaced at the limit), the old copy and the variations are released first, so the pads never hold more than THUNDER_RENDER_MAX_PAD_BYTES, and an allocation
    // failure then leaves the pad removed.
    if (n * sizeof(float) > THUNDER_RENDER_MAX_PAD_BYTES || r->padBytes - old - varB + n * sizeof(float) > THUNDER_RENDER_MAX_PAD_BYTES) return THUNDER_RENDER_E_LIMIT;
    for (int64_t i = 0; i < frames; i++) if (!fin(l[i])) return THUNDER_RENDER_E_ARG;   // validated before anything is changed or released
    if (rr) for (int64_t i = 0; i < frames; i++) if (!fin(rr[i])) return THUNDER_RENDER_E_ARG;
    std::vector<float> tmp;
    if (r->padBytes + n * sizeof(float) > THUNDER_RENDER_MAX_PAD_BYTES) { r->pads.buf[pad] = VoiceBuf(); std::vector<float>().swap(r->store[pad]); r->padBytes -= old; r->padBytes -= r->pads.dropVarsOfPad(pad); }
    tmp.resize(n);
    std::memcpy(tmp.data(), l, (size_t)frames * sizeof(float));
    if (rr) std::memcpy(tmp.data() + frames, rr, (size_t)frames * sizeof(float));
    const size_t old2 = r->store[pad].size() * sizeof(float);
    r->store[pad].swap(tmp); r->padBytes = r->padBytes - old2 + n * sizeof(float);
    VoiceBuf& b = r->pads.buf[pad]; b.data = r->store[pad].data(); b.data2 = rr ? b.data + frames : nullptr; b.len = (int)frames; b.sr = sample_rate; b.noShape = false;
    r->padBytes -= r->pads.dropVarsOfPad(pad);   // (nothing left to drop when the release-first branch ran)
    r->pads.chop.setLoop(pad, 0);                // a changed pad loses its chop data too (its loop and hits were made from the old one)
    return 0;
  }
  TR_CATCH_BADALLOC(return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

int thunder_render_set_pad_kit(thunder_render* r, int pad, int choke, int syn, double rel) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad) || choke < 0 || choke > 255 || (syn != 0 && syn != 1) || !fin(rel) || rel < 0 || rel > 60) return THUNDER_RENDER_E_ARG;
  r->pads.kit[pad].choke = choke; r->pads.kit[pad].syn = syn != 0; r->pads.kit[pad].rel = rel;
  return 0;
}

int thunder_render_set_pad_route(thunder_render* r, int pad, int route) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad) || route < THUNDER_RENDER_ROUTE_SAMPLER || route > THUNDER_RENDER_ROUTE_SF) return THUNDER_RENDER_E_ARG;
  r->pads.route[pad] = route == THUNDER_RENDER_ROUTE_LIVE ? PadRoute::Live : route == THUNDER_RENDER_ROUTE_SF ? PadRoute::SoundFont : PadRoute::Sampler;
  return 0;
}

int thunder_render_set_pad_chop(thunder_render* r, int pad, double loop_secs) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad) || !fin(loop_secs) || loop_secs < 0 || loop_secs > kSeqChopMaxSecs) return THUNDER_RENDER_E_ARG;
  return r->pads.chop.setLoop(pad, loop_secs) ? 0 : THUNDER_RENDER_E_ARG;
}

int thunder_render_set_pad_hits(thunder_render* r, int pad, int n_slices, const double* pts, int count) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad) || n_slices < 2 || n_slices > kSeqChopMaxSlices || count < 0 || count > n_slices || (count > 0 && !pts)) return THUNDER_RENDER_E_ARG;
  TR_TRY { return r->pads.chop.setHits(pad, n_slices, pts, count) ? 0 : THUNDER_RENDER_E_ARG; }
  TR_CATCH_BADALLOC(return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

int thunder_render_set_variation(thunder_render* r, int ii, int kind, int num, int pad, const float* l, int64_t frames, double sample_rate) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (ii < 0 || ii >= THUNDER_RENDER_INSTS || (kind != 'g' && kind != 'm') || num < 0 || num > 255 || !padOk(pad) || frames < 0) return THUNDER_RENDER_E_ARG;
  TR_TRY {
    RenderVar* old = r->pads.findVar(ii, (char)kind, num, pad);
    const size_t oldBytes = old ? old->data.size() * sizeof(float) : 0;
    if (frames == 0) { if (old) { r->padBytes -= oldBytes; r->pads.setVar(ii, (char)kind, num, pad, nullptr, 0, 0); } return 0; }
    if (!l || !fin(sample_rate) || sample_rate < 1 || sample_rate > 1e6) return THUNDER_RENDER_E_ARG;
    if (frames > THUNDER_RENDER_MAX_PAD_FRAMES) return THUNDER_RENDER_E_LIMIT;
    const size_t bytes = (size_t)frames * sizeof(float);
    if (bytes > THUNDER_RENDER_MAX_PAD_BYTES || r->padBytes - oldBytes + bytes > THUNDER_RENDER_MAX_PAD_BYTES) return THUNDER_RENDER_E_LIMIT;
    if (!old && r->pads.var.size() >= (size_t)THUNDER_RENDER_MAX_VARIATIONS) return THUNDER_RENDER_E_LIMIT;
    for (int64_t i = 0; i < frames; i++) if (!fin(l[i])) return THUNDER_RENDER_E_ARG;   // validated before anything is changed
    // The same rule as set_pad: when what the handle holds now and the new copy fit together, the copy is built first (a failed allocation leaves the table as it was);
    // when they do not (a replacement at the limit), the old variation is released first and a failed allocation leaves it removed.
    const bool released = old && r->padBytes + bytes > THUNDER_RENDER_MAX_PAD_BYTES;
    if (released) { r->padBytes -= oldBytes; r->pads.setVar(ii, (char)kind, num, pad, nullptr, 0, 0); }   // (`old` is dangling from here)
    r->pads.setVar(ii, (char)kind, num, pad, l, (int)frames, sample_rate);
    r->padBytes = r->padBytes - (released ? 0 : oldBytes) + bytes;
    return 0;
  }
  TR_CATCH_BADALLOC(return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

int64_t thunder_render_length(thunder_render* r) { return r ? r->total : -1; }
int64_t thunder_render_position(thunder_render* r) { return r ? r->outPos : -1; }
int64_t thunder_render_length_estimate(thunder_render* r) {
  if (!r) return -1;
  if (r->total >= 0) return r->total;
  if (r->est != -2) return r->est;
  TR_TRY {
    // A second sequencer, stepped without any audio, to its end: the same song, vars and options as the renderer's own, and the sequencer's random
    // stream is seeded the same, so it ends where the renderer's does (the voices' tails after it are what only the render can tell).
    SeqOpts so; so.row = r->song.renderFrom; so.once = true;
    Seq seq(r->song, r->vars, SeqMode::Song, so);
    std::unique_ptr<SeqEvent[]> evs(new SeqEvent[kRenderEvents]); std::unique_ptr<SeqStep> st(new SeqStep);
    const double limit = (double)(r->maxFrames + r->skip) / r->sr;
    int64_t steps = 0, e = -1; bool cut = false;
    while (seq.alive()) {
      if (seq.time() >= limit) { cut = true; break; }
      if (++steps > 20000000) return r->est = -1;
      seq.step(evs.get(), kRenderEvents, st.get());
    }
    if (cut) e = r->maxFrames;
    else e = std::max<int64_t>(0, (int64_t)std::llround(seq.time() * r->sr) - r->skip);
    return r->est = e;
  }
  TR_CATCH_BADALLOC(return -1;)   // not cached: a later call may succeed; the handle is untouched
  TR_CATCH_ALL(return -1;)
}
int thunder_render_stem_tracks(thunder_render* r, int32_t* out, int cap) {
  if (!r || cap < 0 || (out && cap == 0)) return THUNDER_RENDER_E_ARG;
  if (r->dead) return THUNDER_RENDER_E_STATE;
  TR_TRY {
    // The notes: the same sequencer as the render's, stepped without audio (see length_estimate). A note counts when its track is 0..NT-1 and its pad is in the kit.
    SeqOpts so; so.row = r->song.renderFrom; so.once = true;
    Seq seq(r->song, r->vars, SeqMode::Song, so);
    std::unique_ptr<SeqEvent[]> evs(new SeqEvent[kRenderEvents]); std::unique_ptr<SeqStep> st(new SeqStep);
    const double limit = (double)(r->maxFrames + r->skip) / r->sr;
    uint32_t has = 0; int64_t steps = 0;
    while (seq.alive() && seq.time() < limit) {
      if (++steps > 20000000) return THUNDER_RENDER_E_LIMIT;
      const int k = seq.step(evs.get(), kRenderEvents, st.get());
      for (int i = 0; i < k; i++) {
        const SeqEvent& e = evs[i];
        if (e.type != SEV_NOTE || e.track < 0 || e.track >= kSongNT) continue;
        const int pad = to_int_sat(e.pad, -1, 1 << 20);
        if (pad < 0 || pad >= kRenderPads) continue;
        if (r->pads.buf[pad].data || r->pads.route[pad] != PadRoute::Sampler || r->pads.sf[pad].font) has |= 1u << e.track;
      }
    }
    uint32_t mute = 0, solo = 0;
    for (int t = 0; t < kSongNT; t++) { if (r->song.mix[t].mute) mute |= 1u << t; if (r->song.mix[t].solo) solo |= 1u << t; }
    const BusPlan& P = r->R->mixer().plan();   // the patch's plan, as the page's busPlan(): a bus is a track with sources (BP.feeds), a pure source plays only through its bus
    const uint32_t aud = mix_audible(P, mute, solo);
    uint32_t keep = aud & has;
    for (bool more = true; more;) { more = false;   // busStemPlan: until nothing is added (a bus that only a bus feeds is in too)
      for (int b = 0; b < kSongNT; b++) if (P.feedMask[b] && !(keep >> b & 1u) && (aud >> b & 1u) && (P.feedMask[b] & keep)) { keep |= 1u << b; more = true; } }
    uint32_t list = keep;
    for (int t = 0; t < kSongNT; t++) if ((keep >> t & 1u) && (P.pure >> t & 1u) && (P.targets[t] & keep)) list &= ~(1u << t);   // inside its bus's stem
    int n = 0;
    for (int t = 0; t < kSongNT; t++) if (list >> t & 1u) { if (out && n < cap) out[n] = t + 1; n++; }
    return n;
  }
  TR_CATCH_BADALLOC(return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(return THUNDER_RENDER_E_INTERNAL;)
}
int thunder_render_latency(thunder_render* r) { return r ? (int)std::min<int64_t>(r->skip, 0x7fffffff) : -1; }

int64_t thunder_render_read(thunder_render* r, float* l, float* rr, int64_t frames) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead) return THUNDER_RENDER_E_STATE;
  if (frames < 0) return THUNDER_RENDER_E_ARG;
  if (frames == 0) return 0;
  if (!l || !rr) return THUNDER_RENDER_E_ARG;
  { const uintptr_t a = (uintptr_t)l, b = (uintptr_t)rr, bytes = (uintptr_t)frames * sizeof(float);   // (frames is bounded by what the host could allocate; a wrap is caught below)
    if ((uintptr_t)frames > (UINTPTR_MAX / sizeof(float)) / 2) return THUNDER_RENDER_E_ARG;
    if (a < b + bytes && b < a + bytes) return THUNDER_RENDER_E_ARG; }
  TR_TRY {
    r->started = true;
    int64_t n = 0;
    while (n < frames) {
      int64_t want = frames - n;
      if (r->total >= 0) { const int64_t left = r->total - r->outPos; if (left <= 0) break; want = std::min(want, left); }
      if (r->qPos >= kRenderQuantum) { if (r->ended) break; pump(r); continue; }
      const int c = (int)std::min<int64_t>(want, kRenderQuantum - r->qPos);
      std::memcpy(l + n, r->q + r->qPos, (size_t)c * sizeof(float));
      std::memcpy(rr + n, r->q + kRenderQuantum + r->qPos, (size_t)c * sizeof(float));
      r->qPos += c; n += c; r->outPos += c;
    }
    return n;
  }
  TR_CATCH_BADALLOC(r->dead = true; return THUNDER_RENDER_E_MEMORY;)   // the handle is dead (render.h, ERRORS): the frames of this call are lost with it
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

int thunder_render_stats_get(thunder_render* r, thunder_render_stats* out) {
  if (!r || !out || out->size < sizeof(uint32_t) * 2) return THUNDER_RENDER_E_ARG;
  thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = (uint32_t)sizeof s;
  const RenderStats t = r->R->stats();
  s.song_over = r->R->songOver(); s.voices_active = r->R->voices(); s.hit_limit = r->hitLimit; s.frames_rendered = t.frames;
  s.steps = t.steps; s.events = t.events; s.notes = t.notes; s.notes_played = t.notesPlayed; s.notes_no_pad = t.notesNoPad; s.notes_midi_only = t.notesMidiOnly;
  s.notes_shaped_rev = t.notesShapedRev; s.mix_events = t.mixEvents; s.peak_voices = t.peakVoices;
  s.voices_refused = t.voiceDropped; s.voice_track_full = t.voiceOverflow; s.voice_auto_dropped = t.voiceAutoDropped; s.mix_auto_dropped = t.mixAutoDropped; s.seq_dropped = t.seqDropped;
  s.notes_live = t.notesLive; s.notes_sf = t.notesSf; s.notes_var = t.notesVar; s.notes_var_missing = t.notesVarMissing; s.notes_unshaped = t.notesUnshaped; s.events_ignored = t.eventsIgnored; s.notes_sf_played = t.notesSfPlayed;
  s.notes_stretched = t.notesStretched; s.seq_chopped = t.seqChopped; s.seq_chop_skipped = t.seqChopSkipped; s.seq_chop_no_hits = t.seqChopNoHits;
  s.sf_voices_refused = t.sfVoiceDropped; s.sf_voice_track_full = t.sfVoiceOverflow; s.sf_voice_auto_dropped = t.sfVoiceAutoDropped; s.peak_voices_all = t.peakVoicesAll;
  s.stem = r->stem; s.heard_mask = r->R->heardMask();
  s.notes_live_played = t.notesLivePlayed; s.live_no_node = t.liveNoNode; s.live_late = t.liveLate; s.live_queue_dropped = t.liveQueueDropped; s.live_forced_off = t.liveForcedOff; s.live_failed = t.liveFailed; s.live_refused = t.liveRefused;
  const size_t k = std::min<size_t>(out->size, sizeof s); const uint32_t keep = out->size;
  std::memcpy(out, &s, k); out->size = keep;
  return 0;
}

int64_t thunder_render_drain(thunder_render* r, int64_t max_frames) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead) return THUNDER_RENDER_E_STATE;
  if (max_frames < 0) return THUNDER_RENDER_E_ARG;
  if (r->total < 0 || r->outPos < r->total) return THUNDER_RENDER_E_STATE;
  r->started = true;   // the renderer runs on: the pads and the kit are fixed from here, as after the first read
  TR_TRY {
    float L[kRenderQuantum], Rr[kRenderQuantum]; int64_t run = 0;
    while (!(r->R->songOver() && r->R->voices() == 0) && run < max_frames) { r->R->block(L, Rr, kRenderQuantum); run += kRenderQuantum; r->raw += kRenderQuantum; }
    return (r->R->songOver() && r->R->voices() == 0) ? run : THUNDER_RENDER_E_LIMIT;
  }
  TR_CATCH_BADALLOC(r->dead = true; return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

void thunder_render_free(thunder_render* r) {
  TR_TRY { delete r; }
  TR_CATCH_ALL()
}

}  // extern "C"

// ---- SoundFont pads (RENDER-SF-NOTES.md) -------------------------------------------------------------------------------------------------------------------------------------------------------------------
namespace {
// Gives back what pad `pad`'s prepared preset holds (the PCM bytes, the font's counters) and clears it. Allocates nothing.
void dropPadSf(thunder_render* r, int pad) {
  if (!r->sfPcm[pad]) return;
  const int fi = r->sfFont[pad];
  if (fi >= 0 && r->fonts[fi]) { RenderFont& f = *r->fonts[fi]; f.used -= std::min<uint64_t>(f.used, r->sfPcm[pad]->bytes); f.workUsed -= std::min<uint64_t>(f.workUsed, r->sfPcm[pad]->work); }
  r->padBytes -= r->sfBytes[pad]; r->sfBytes[pad] = 0; r->sfFont[pad] = -1;
  r->pads.sf[pad] = sf::SfPad();   // (the renderer is not running: only before the first read)
  r->sfPcm[pad].reset();
}
}  // namespace

extern "C" {

int thunder_render_add_font(thunder_render* r, const uint8_t* bytes, size_t n) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!bytes || n == 0) return THUNDER_RENDER_E_ARG;
  TR_TRY {
    int id = -1; for (int i = 0; i < THUNDER_RENDER_MAX_FONTS; i++) if (!r->fonts[i]) { id = i; break; }
    if (id < 0 || n > THUNDER_RENDER_MAX_PAD_BYTES || r->padBytes + n > THUNDER_RENDER_MAX_PAD_BYTES) return THUNDER_RENDER_E_LIMIT;
    std::unique_ptr<RenderFont> f(new RenderFont());
    f->bytes.assign(bytes, bytes + n);
    const sf::ParseStatus st = sf::parse(f->bytes.data(), f->bytes.size(), f->font, false);
    if (st == sf::kParseTooBig) return THUNDER_RENDER_E_LIMIT;
    if (st != sf::kParseOk) return THUNDER_RENDER_E_FONT;
    f->held = n + (size_t)f->font.bytesUsed();
    if (r->padBytes + f->held > THUNDER_RENDER_MAX_PAD_BYTES) return THUNDER_RENDER_E_LIMIT;
    r->padBytes += f->held; r->fonts[id] = std::move(f);
    return id;
  }
  TR_CATCH_BADALLOC(return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

int thunder_render_remove_font(thunder_render* r, int font_id) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (font_id < 0 || font_id >= THUNDER_RENDER_MAX_FONTS || !r->fonts[font_id]) return THUNDER_RENDER_E_ARG;
  for (int p = 0; p < kRenderPads; p++) if (r->sfFont[p] == font_id) return THUNDER_RENDER_E_ARG;   // a pad still uses it
  r->padBytes -= r->fonts[font_id]->held; r->fonts[font_id].reset();
  return 0;
}

int thunder_render_set_pad_sf(thunder_render* r, int pad, int font_id, int bank, int prog, const thunder_render_sf_params* params, const uint8_t* keys, size_t nkeys) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad) || font_id < 0 || font_id >= THUNDER_RENDER_MAX_FONTS || !r->fonts[font_id] || bank < 0 || bank > 16383 || prog < 0 || prog > 127) return THUNDER_RENDER_E_ARG;
  if ((!keys && nkeys) || (keys && (nkeys == 0 || nkeys > THUNDER_RENDER_MAX_SF_KEYS))) return THUNDER_RENDER_E_ARG;
  sf::PadParams P; P.bank = bank; P.prog = prog;   // defaults: trans 0, velo 0.7, atk 1, rel 1, level 0, len 0
  if (params) {
    if (params->size < offsetof(thunder_render_sf_params, trans)) return THUNDER_RENDER_E_ARG;
    thunder_render_sf_params q; std::memset(&q, 0, sizeof q); q.size = (uint32_t)sizeof q; q.velo = P.velo; q.atk = P.atk; q.rel = P.rel;
    const size_t k = std::min<size_t>(params->size, sizeof q); std::memcpy(&q, params, k); q.size = (uint32_t)sizeof q;
    if (q.reserved0 != 0) return THUNDER_RENDER_E_ARG;
    if (!fin(q.trans) || q.trans < -128 || q.trans > 128 || !fin(q.velo) || q.velo < 0 || q.velo > 1 || !fin(q.atk) || q.atk < 0 || q.atk > 100 || !fin(q.rel) || q.rel < 0 || q.rel > 100 ||
        !fin(q.level) || q.level < -200 || q.level > 200 || !fin(q.len) || q.len < 0 || q.len > 3600) return THUNDER_RENDER_E_ARG;
    P.trans = q.trans; P.velo = q.velo; P.atk = q.atk; P.rel = q.rel; P.level = q.level; P.len = q.len;
  }
  uint8_t flags[128]; std::memset(flags, 0, sizeof flags);
  if (keys) for (size_t i = 0; i < nkeys; i++) { if (keys[i] > 127) return THUNDER_RENDER_E_ARG; flags[keys[i]] = 1; }
  TR_TRY {
    RenderFont& f = *r->fonts[font_id];
    const sf::Preset* pr = sf::sf_preset(f.font, bank, prog);
    if (!pr) return THUNDER_RENDER_E_ARG;   // a font with no preset at all
    // Built first (the old copy stays until the new one is in): the room is what the budget leaves with the old copy still held.
    std::unique_ptr<sf::PresetPcm> pcm(new sf::PresetPcm());
    sf::DecodeOpts o; const uint64_t room = THUNDER_RENDER_MAX_PAD_BYTES - std::min<size_t>(r->padBytes, THUNDER_RENDER_MAX_PAD_BYTES);
    o.presetBytes = std::min<uint64_t>(o.presetBytes, room);
    // the font's counters: when this pad already holds a preset of the same font, its share is still counted (the new copy is built beside it)
    uint64_t used = f.used, wused = f.workUsed; o.fontUsed = &used; o.fontWorkUsed = &wused;
    const sf::PrepareStatus st = sf::prepare_preset(f.font, pr, o, *pcm, keys ? flags : nullptr);
    if (st == sf::kPrepNoPreset) return THUNDER_RENDER_E_ARG;
    if (st != sf::kPrepOk) return THUNDER_RENDER_E_LIMIT;
    if (r->padBytes + (size_t)pcm->bytes + (size_t)pcm->tableBytes > THUNDER_RENDER_MAX_PAD_BYTES) return THUNDER_RENDER_E_LIMIT;   // (cannot happen: presetBytes was bounded by the room)
    // committed: nothing below can fail
    const size_t newBytes = (size_t)pcm->bytes + (size_t)pcm->tableBytes;   // the PCM and the per-sample-header table (verify-242 F2)
    dropPadSf(r, pad);
    r->fonts[font_id]->used += pcm->bytes; r->fonts[font_id]->workUsed += pcm->work;
    r->sfPcm[pad] = std::move(pcm); r->sfFont[pad] = font_id; r->sfBytes[pad] = newBytes; r->padBytes += newBytes;
    sf::SfPad sp; sp.font = &f.font; sp.pr = pr; sp.pcm = r->sfPcm[pad].get(); sp.P = P; r->pads.sf[pad] = sp;
    return 0;
  }
  TR_CATCH_BADALLOC(return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

int thunder_render_clear_pad_sf(thunder_render* r, int pad) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad)) return THUNDER_RENDER_E_ARG;
  dropPadSf(r, pad);
  return 0;
}

int thunder_render_set_pad_live(thunder_render* r, int pad, int kind, const uint8_t* voice_json, size_t n, const thunder_render_live_params* params) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad) || !voice_json || n == 0 || n > THUNDER_RENDER_MAX_VARS_BYTES) return THUNDER_RENDER_E_ARG;
  if (kind != THUNDER_RENDER_LIVE_BASS && kind != THUNDER_RENDER_LIVE_VOX && kind != THUNDER_RENDER_LIVE_ACID && kind != THUNDER_RENDER_LIVE_MACH) return THUNDER_RENDER_E_ARG;   // (the pump synths and waves: later steps)
  uint64_t mask = 0;
  if (params) {
    if (params->size < 2 * sizeof(uint32_t)) return THUNDER_RENDER_E_ARG;
    thunder_render_live_params q; std::memset(&q, 0, sizeof q); q.size = (uint32_t)sizeof q;
    const size_t k = std::min<size_t>(params->size, sizeof q); std::memcpy(&q, params, k); q.size = (uint32_t)sizeof q;
    if (q.reserved0 != 0 || (q.track_mask >> 33) != 0) return THUNDER_RENDER_E_ARG;
    mask = q.track_mask;
  }
  TR_TRY {
    JsonDoc doc; JsonError je;
    if (!doc.parse((const char*)voice_json, n, &je)) return je.msg && !std::strcmp(je.msg, "out of memory") ? THUNDER_RENDER_E_MEMORY : THUNDER_RENDER_E_ARG;
    if (!doc.root().isObj()) return THUNDER_RENDER_E_ARG;
    std::unique_ptr<XengVoice> V(new XengVoice());
    if (!xeng_voice_from_json(*V, doc.root(), Json())) return THUNDER_RENDER_E_ARG;
    LivePadConfig cfg; cfg.trackMask = mask;
    if (kind == THUNDER_RENDER_LIVE_BASS) { if (V->kind != XKind::Bass) return THUNDER_RENDER_E_ARG; cfg.kind = LiveKind::Bass; cfg.bass = V->bass; cfg.bass.tune = V->tune; cfg.bass.drive = V->drive; cfg.bass.level = V->level; }   // (tune, drive and level come from the voice, as the page's node params do)
    else if (kind == THUNDER_RENDER_LIVE_VOX) { if (V->kind != XKind::Vox) return THUNDER_RENDER_E_ARG; cfg.kind = LiveKind::Vox; cfg.vox = V->vox; cfg.vox.tune = V->tune; cfg.vox.drive = V->drive; cfg.vox.level = V->level; }
    else if (kind == THUNDER_RENDER_LIVE_ACID) { if (V->kind != XKind::Acid) return THUNDER_RENDER_E_ARG; cfg.kind = LiveKind::Acid; cfg.acid = V->acid; cfg.acid.tune = V->tune; cfg.acid.drive = V->drive; cfg.acid.level = V->level; }
    else { if (V->kind != XKind::Mach) return THUNDER_RENDER_E_ARG; cfg.kind = LiveKind::Mach; cfg.mach = V->mach; cfg.mach.tune = V->tune; cfg.mach.drive = V->drive; cfg.mach.level = V->level; }
    LivePool& L = r->R->players().live();
    // the nodes' bytes count against the pad budget (the old pad's nodes are released only when the new ones are in: judged with both held)
    size_t each = 0; { int tracks = kVoiceTracks; if (mask) { tracks = 0; for (uint64_t m = mask; m; m &= m - 1) tracks++; } each = (size_t)tracks * (kind == THUNDER_RENDER_LIVE_BASS ? 58000 : kind == THUNDER_RENDER_LIVE_VOX ? 5 * 52000 : 39000); }
    if (r->padBytes + each > THUNDER_RENDER_MAX_PAD_BYTES) return THUNDER_RENDER_E_LIMIT;
    const size_t old = L.padBytes(pad);
    if (!L.addPad(pad, cfg)) return THUNDER_RENDER_E_ARG;
    r->padBytes = r->padBytes - old + L.padBytes(pad);
    return 0;
  }
  TR_CATCH_BADALLOC(return THUNDER_RENDER_E_MEMORY;)
  TR_CATCH_ALL(r->dead = true; return THUNDER_RENDER_E_INTERNAL;)
}

int thunder_render_clear_pad_live(thunder_render* r, int pad) {
  if (!r) return THUNDER_RENDER_E_ARG;
  if (r->dead || r->started) return THUNDER_RENDER_E_STATE;
  if (!padOk(pad)) return THUNDER_RENDER_E_ARG;
  LivePool& L = r->R->players().live();
  r->padBytes -= L.padBytes(pad); L.removePad(pad);
  return 0;
}

}  // extern "C"
