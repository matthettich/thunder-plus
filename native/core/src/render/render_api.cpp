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
#include "rt/json.h"

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

struct thunder_render {
  Song song;                           // must outlive R
  JsonDoc varsDoc;                     // the vars JSON (SeqVars may keep handles into it)
  SeqVars vars;
  RenderPads pads;                     // must outlive R; the buffers are in `store`
  std::vector<float> store[kRenderPads];
  size_t padBytes = 0;
  std::unique_ptr<SongRenderer> R;
  // options
  double sr = 48000; bool useFrames = false; int64_t frames = 0, maxFrames = 0;
  int64_t skip = 0;                    // raw frames dropped at the start (the latency)
  // the stream
  float q[2 * kRenderQuantum];         // the last quantum: left, then right
  int qPos = kRenderQuantum;           // next unread frame of q (kRenderQuantum: empty)
  int64_t raw = 0, skipLeft = 0, outPos = 0;
  int64_t total = -1;                  // frames of output once known
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
    TR_COPY(reserved1); TR_COPY(frames); TR_COPY(max_frames); TR_COPY(vars_json); TR_COPY(vars_bytes);
#undef TR_COPY
  }
  if (o.reserved0 != 0 || o.reserved1 != 0) { setErr(err, cap, "opts.reserved must be 0"); return nullptr; }
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
  r->sr = sr; r->useFrames = o.use_frames != 0; r->frames = o.frames;
  r->maxFrames = o.max_frames ? o.max_frames : (int64_t)(sr * 1260);
  SeqOpts so; so.row = r->song.renderFrom; so.once = true;
  r->R.reset(new SongRenderer(r->song, r->vars, r->pads, sr, kRenderQuantum, so));
  r->R->pool().setPageTiming(o.page_timing != 0);
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
    if (frames == 0) { r->padBytes -= r->store[pad].size() * sizeof(float); std::vector<float>().swap(r->store[pad]); r->pads.buf[pad] = VoiceBuf(); return 0; }
    if (!l || !fin(sample_rate) || sample_rate < 1 || sample_rate > 1e6) return THUNDER_RENDER_E_ARG;
    if (frames > THUNDER_RENDER_MAX_PAD_FRAMES) return THUNDER_RENDER_E_LIMIT;
    const size_t ch = rr ? 2 : 1, n = (size_t)frames * ch, old = r->store[pad].size() * sizeof(float);
    // The budget bounds what the handle HOLDS, not only what it ends up with. The final state must fit (the old pad is replaced); then, if the old copy and
    // the new one fit TOGETHER, the new one is built first and swapped in (a failure leaves the pad as it was); if they do not (a big pad replaced at the
    // limit), the old copy is released first, so the pads never hold more than THUNDER_RENDER_MAX_PAD_BYTES, and an allocation failure then leaves the pad removed.
    if (n * sizeof(float) > THUNDER_RENDER_MAX_PAD_BYTES || r->padBytes - old + n * sizeof(float) > THUNDER_RENDER_MAX_PAD_BYTES) return THUNDER_RENDER_E_LIMIT;
    for (int64_t i = 0; i < frames; i++) if (!fin(l[i])) return THUNDER_RENDER_E_ARG;   // validated before anything is changed or released
    if (rr) for (int64_t i = 0; i < frames; i++) if (!fin(rr[i])) return THUNDER_RENDER_E_ARG;
    std::vector<float> tmp;
    if (r->padBytes + n * sizeof(float) > THUNDER_RENDER_MAX_PAD_BYTES) { r->pads.buf[pad] = VoiceBuf(); std::vector<float>().swap(r->store[pad]); r->padBytes -= old; }
    tmp.resize(n);
    std::memcpy(tmp.data(), l, (size_t)frames * sizeof(float));
    if (rr) std::memcpy(tmp.data() + frames, rr, (size_t)frames * sizeof(float));
    const size_t old2 = r->store[pad].size() * sizeof(float);
    r->store[pad].swap(tmp); r->padBytes = r->padBytes - old2 + n * sizeof(float);
    VoiceBuf& b = r->pads.buf[pad]; b.data = r->store[pad].data(); b.data2 = rr ? b.data + frames : nullptr; b.len = (int)frames; b.sr = sample_rate; b.noShape = false;
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
