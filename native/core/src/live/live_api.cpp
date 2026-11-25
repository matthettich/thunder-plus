// THE LIVE HOST'S C API (U37 P5): every function of include/thunder/live.h as a thin extern "C" wrapper over thunder::LiveHost (live_host.h). The contract is the header's; this file only
//   - turns a handle into a LiveHost (a NULL handle is E_ARG, a DEAD handle is E_STATE: see `dead` below),
//   - applies the ABI rule of the structs that cross the API (`size` first: a field the caller's struct does not reach keeps its default, a field past the end of ours must be zero),
//   - checks what LiveHost does not (a size above a limit, a pointer with a size, the flags a handle was made with),
//   - and keeps every exception inside: a std::bad_alloc is E_MEMORY (the call changed nothing: LiveHost's methods catch their own allocations and leave the host as it was), anything else
//     is E_INTERNAL and kills the handle (live.h, E_INTERNAL: only destroy, the getters and thunder_live_alloc / free work from then on).
// BUILT WITH EXCEPTIONS (core/CMakeLists.txt, like live_host.cpp and render_api.cpp). Built WITHOUT them (the wasm build) the try / catch below compile away (TL_TRY, TL_CATCH_*): an allocation failure
// in the parser is then not caught (live.h, OUT OF MEMORY).
// THREADS: nothing here takes a lock. thunder_live_process is the AUDIO function and goes straight to LiveHost::process after two loads; it allocates nothing, throws nothing (rt_harness pass 4 polices it).
#include "thunder/live.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#include "live/live_host.h"

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || (defined(_MSC_VER) && defined(_CPPUNWIND))
#define TL_TRY try
#define TL_CATCH_BADALLOC(...) catch (const std::bad_alloc&) { __VA_ARGS__ }
#define TL_CATCH_ALL(...) catch (...) { __VA_ARGS__ }
#else
#define TL_TRY
#define TL_CATCH_BADALLOC(...)
#define TL_CATCH_ALL(...)
#endif

using thunder::LiveHost;

struct thunder_live {
  LiveHost* h = nullptr;
  std::atomic<int> dead{0};   // 1: a CONTROL call met an exception that is not an allocation failure: every call but destroy, the getters, poll and alloc / free answers E_STATE
  uint32_t flags = 0;         // the create flags (dump_step needs F_DUMP_STEPS and must tell a bad argument from a missing part)
  int maxBlock = 128;         // the process call's limit (a dead handle still zeroes the outputs it was given)
};

namespace {

// The ABI rule for a struct the caller made: `size` is its first member. Copies what both have, leaves the rest zero (= default); a caller newer than this build must have zeroed what we do not know.
// Returns 0, or THUNDER_LIVE_E_ARG for a size below 4 or a nonzero byte past our struct. `dst` is zero-filled first and its `size` set to ours.
int abiCopyIn(const void* src, void* dst, size_t dstSize) {
  uint32_t sz = 0; std::memcpy(&sz, src, sizeof sz);
  std::memset(dst, 0, dstSize);
  if (sz < sizeof(uint32_t)) return THUNDER_LIVE_E_ARG;
  const size_t n = sz < dstSize ? (size_t)sz : dstSize;
  std::memcpy(dst, src, n);
  if (sz > dstSize) { const unsigned char* t = static_cast<const unsigned char*>(src); for (size_t i = dstSize; i < sz; i++) if (t[i]) return THUNDER_LIVE_E_ARG; }
  const uint32_t mine = (uint32_t)dstSize; std::memcpy(dst, &mine, sizeof mine);
  return THUNDER_LIVE_OK;
}

// One CONTROL call: handle checks, then `f(host)` with the exceptions kept in. (A lambda per call: it inlines to the same code a hand-written try block gives.)
template <class F> int ctl(thunder_live* L, F f) {
  if (!L) return THUNDER_LIVE_E_ARG;
  if (L->dead.load(std::memory_order_relaxed)) return THUNDER_LIVE_E_STATE;
  TL_TRY { return f(*L->h); }
  TL_CATCH_BADALLOC(return THUNDER_LIVE_E_MEMORY;)
  TL_CATCH_ALL(L->dead.store(1, std::memory_order_relaxed); return THUNDER_LIVE_E_INTERNAL;)
}

}  // namespace

extern "C" {

// ---- 1. LIFE -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

uint32_t thunder_live_abi(void) { return THUNDER_LIVE_ABI; }

uint32_t thunder_live_caps(void) {
  // What THIS build has. The live host does not build SoundFonts, live-engine pads or a live-engine swap yet (LiveHost::fontAdd / padSf / padLive answer E_UNSUPPORTED): CAP_SF, CAP_LIVE_SWAP and CAP_ENGINES stay 0.
  uint32_t c = THUNDER_LIVE_CAP_MASTER_SWAP | THUNDER_LIVE_CAP_TAKE | THUNDER_LIVE_CAP_CHAINS;   // LiveHost::masterConfig (P3), PadsHost::setPadTake (P2), ChainsHost live (C2: F_CHAINS, chain_edit)
#ifdef THUNDER_HAVE_TFX
  c |= THUNDER_LIVE_CAP_EFFECTS;
#endif
  return c;
}

thunder_live* thunder_live_create(const thunder_live_opts* opts, char* err, size_t err_cap) {
  if (err && err_cap) err[0] = 0;
  auto say = [&](const char* m) -> thunder_live* { if (err && err_cap) std::snprintf(err, err_cap, "%s", m); return nullptr; };
  thunder_live_opts o; std::memset(&o, 0, sizeof o); o.size = (uint32_t)sizeof o;
  if (opts && abiCopyIn(opts, &o, sizeof o) != 0) return say("opts: size is below 4 or a field this build does not know is not zero");
  thunder_live* L = new (std::nothrow) thunder_live();
  if (!L) return say("out of memory");
  TL_TRY {
    L->h = LiveHost::create(o, err, err_cap);   // it writes `err` on its own failures
  }
  TL_CATCH_ALL(L->h = nullptr; if (err && err_cap) std::snprintf(err, err_cap, "%s", "out of memory");)
  if (!L->h) { delete L; return nullptr; }
  L->flags = o.flags; L->maxBlock = o.max_block == 0 ? THUNDER_LIVE_QUANTUM : o.max_block;
  return L;
}

void thunder_live_destroy(thunder_live* L) {
  if (!L) return;
  delete L->h; L->h = nullptr;
  delete L;
}

int thunder_live_process(thunder_live* L, float* out_l, float* out_r, int frames, int64_t frame0, int64_t wall_ns) {
  if (!L) return THUNDER_LIVE_E_ARG;   // (nothing is known of the buffers' size: they are not written)
  if (L->dead.load(std::memory_order_relaxed)) {   // silence: the outputs are always written
    if (out_l && out_r && frames > 0 && frames <= L->maxBlock && !(out_l < out_r + frames && out_r < out_l + frames)) { std::memset(out_l, 0, (size_t)frames * sizeof(float)); std::memset(out_r, 0, (size_t)frames * sizeof(float)); }
    return THUNDER_LIVE_E_STATE;
  }
  return L->h->process(out_l, out_r, frames, frame0, wall_ns);
}

int thunder_live_stats_get(const thunder_live* L, thunder_live_stats* out) {
  if (!L || !out || out->size < sizeof(uint32_t)) return THUNDER_LIVE_E_ARG;
  return L->h->statsGet(out) ? THUNDER_LIVE_OK : THUNDER_LIVE_E_ARG;
}

int thunder_live_engine_offset(const thunder_live* L, int64_t* out) {
  if (!L || !out) return THUNDER_LIVE_E_ARG;
  *out = L->h->engineOffset();
  return THUNDER_LIVE_OK;
}

float* thunder_live_alloc(int64_t nfloats) {
  // `new float[]`, because the block is handed to PadsHost::setPadTake and freed there with delete[] (pads_snap.cpp freeBuf): U37-P0-NOTES.md, "P5 work list". Alignment: that of operator new[] (16 bytes on glibc, bionic 64-bit,
  // macOS, MSVC x64 and wasi-libc; nothing in the core needs more than alignof(float): live.h says so).
  if (nfloats <= 0 || nfloats > ((int64_t)1 << 30)) return nullptr;
  return new (std::nothrow) float[(size_t)nfloats];
}
void thunder_live_free(float* block) { delete[] block; }

// ---- 2. SONG -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

int thunder_live_song_publish(thunder_live* L, const uint8_t* song, size_t song_bytes, const uint8_t* vars, size_t vars_bytes, int64_t rev, char* err, size_t err_cap) {
  if (err && err_cap) err[0] = 0;
  return ctl(L, [&](LiveHost& h) { return h.songPublish(song, song_bytes, vars, vars_bytes, rev, err, err_cap); });
}

// ---- 3. PADS -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

int thunder_live_pad_set(thunder_live* L, int pad, const float* l, const float* r, int64_t frames, double rate) { return ctl(L, [&](LiveHost& h) { return h.padSet(pad, l, r, frames, rate); }); }
int thunder_live_pad_take(thunder_live* L, int pad, float* block, int stereo, int64_t frames, double rate) {
  return ctl(L, [&](LiveHost& h) { return (stereo != 0 && stereo != 1) ? THUNDER_LIVE_E_ARG : h.padTake(pad, block, stereo == 1, frames, rate); });
}
int thunder_live_pad_kit(thunder_live* L, int pad, int choke, int syn, double rel) { return ctl(L, [&](LiveHost& h) { return h.padKit(pad, choke, syn, rel); }); }
int thunder_live_pad_route(thunder_live* L, int pad, int route) { return ctl(L, [&](LiveHost& h) { return h.padRoute(pad, route); }); }
int thunder_live_var_set(thunder_live* L, int ii, int kind, int num, int pad, const float* l, int64_t frames, double rate) { return ctl(L, [&](LiveHost& h) { return h.varSet(ii, kind, num, pad, l, frames, rate); }); }
int thunder_live_pad_chop(thunder_live* L, int pad, double loop_secs) { return ctl(L, [&](LiveHost& h) { return h.padChop(pad, loop_secs); }); }
int thunder_live_pad_hits(thunder_live* L, int pad, int n_slices, const double* pts, int count) { return ctl(L, [&](LiveHost& h) { return h.padHits(pad, n_slices, pts, count); }); }
// SoundFonts and live-engine pads: not in the live host yet (LiveHost answers E_UNSUPPORTED to every one of them; thunder_live_caps has no CAP_SF / CAP_ENGINES / CAP_LIVE_SWAP). The two `_clear` calls answer 0 for a pad
// in range (there is nothing to clear), E_ARG for a bad pad.
int thunder_live_font_add(thunder_live* L, const uint8_t* bytes, size_t n) { return ctl(L, [&](LiveHost& h) { return h.fontAdd(bytes, n); }); }
int thunder_live_font_remove(thunder_live* L, int font_id) { return ctl(L, [&](LiveHost& h) { return h.fontRemove(font_id); }); }
int thunder_live_pad_sf(thunder_live* L, int pad, int font_id, int bank, int prog, const thunder_render_sf_params* params, const uint8_t* keys, size_t nkeys) {
  return ctl(L, [&](LiveHost& h) { return h.padSf(pad, font_id, bank, prog, params, keys, nkeys); });
}
int thunder_live_pad_sf_clear(thunder_live* L, int pad) { return ctl(L, [&](LiveHost& h) { return h.padSfClear(pad); }); }
int thunder_live_pad_live(thunder_live* L, int pad, int kind, const uint8_t* voice_json, size_t n, const thunder_render_live_params* params) { return ctl(L, [&](LiveHost& h) { return h.padLive(pad, kind, voice_json, n, params); }); }
int thunder_live_pad_live_clear(thunder_live* L, int pad) { return ctl(L, [&](LiveHost& h) { return h.padLiveClear(pad); }); }
int thunder_live_pads_publish(thunder_live* L) { return ctl(L, [&](LiveHost& h) { return h.padsPublish(); }); }
int thunder_live_collect(thunder_live* L) { return ctl(L, [&](LiveHost& h) { return h.collect(); }); }

// ---- 4. TRANSPORT ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

int thunder_live_play(thunder_live* L, const thunder_live_play_opts* o) {
  if (!L) return THUNDER_LIVE_E_ARG;
  if (!o) return THUNDER_LIVE_E_ARG;
  thunder_live_play_opts p;
  uint32_t callerSize = 0; std::memcpy(&callerSize, o, sizeof callerSize);
  if (abiCopyIn(o, &p, sizeof p) != 0) return THUNDER_LIVE_E_ARG;
  if (callerSize < offsetof(thunder_live_play_opts, t0) + sizeof(int64_t)) p.t0 = THUNDER_LIVE_NOW;   // live.h ABI: a struct too short to hold t0 gets the default, which is NOW, not frame 0
  return ctl(L, [&](LiveHost& h) { return h.play(p); });
}
int thunder_live_stop(thunder_live* L, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.stop(at); }); }
int thunder_live_hold(thunder_live* L, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.hold(at); }); }
int thunder_live_loop(thunder_live* L, int has, int y0, int y1, int64_t at) { return ctl(L, [&](LiveHost& h) { return (has != 0 && has != 1) ? THUNDER_LIVE_E_ARG : h.loop(has == 1, y0, y1, at); }); }
int thunder_live_bpm(thunder_live* L, double bpm, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.bpm(bpm, at); }); }
int thunder_live_lead(thunder_live* L, double seconds, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.lead(seconds, at); }); }
int thunder_live_perf(thunder_live* L, int what, int index, double v, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.perf(what, index, v, at); }); }
int thunder_live_stop_track(thunder_live* L, int track, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.stopTrack(track, at); }); }

// ---- 5. MIXER ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

int thunder_live_mix_set(thunder_live* L, int track, int key, double value, double ramp, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.mixSet(track, key, value, ramp, at); }); }
int thunder_live_pdc_set(thunder_live* L, int mode) { return ctl(L, [&](LiveHost& h) { return h.pdcSet(mode); }); }

// ---- 6. EFFECTS AND MASTER ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------
// A JSON text above 4 MiB (THUNDER_LIVE_MAX_VARS_BYTES, the vars' limit) is E_LIMIT before it is parsed: a slot list or a master chain object is a few KB (live.h).

int thunder_live_fx_plan(thunder_live* L, int track, const uint8_t* slots_json, size_t n, double bpm) {
  return ctl(L, [&](LiveHost& h) { return n > (size_t)THUNDER_LIVE_MAX_VARS_BYTES ? THUNDER_LIVE_E_LIMIT : h.fxPlan(track, slots_json, n, bpm); });
}
int thunder_live_fx_commit(thunder_live* L, int track, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.fxCommit(track, at); }); }
int thunder_live_fx_param(thunder_live* L, int track, int slot, const double* p12, int64_t at) { return ctl(L, [&](LiveHost& h) { return h.fxParam(track, slot, p12, at); }); }
int thunder_live_master_config(thunder_live* L, const uint8_t* mst_json, size_t n, int safety) {
  return ctl(L, [&](LiveHost& h) {
    if (n > (size_t)THUNDER_LIVE_MAX_VARS_BYTES) return (int)THUNDER_LIVE_E_LIMIT;
    if (n > 0 && !mst_json) return (int)THUNDER_LIVE_E_ARG;
    if (safety != 0 && safety != 1) return (int)THUNDER_LIVE_E_ARG;
    return h.masterConfig(mst_json, n, safety == 1);
  });
}

// ---- 6b. CHAINS (C2) ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

int thunder_live_chain_edit(thunder_live* L, const uint8_t* ops_json, size_t n, int64_t at) {
  return ctl(L, [&](LiveHost& h) { return h.chainEdit(ops_json, n, at); });   // (LiveHost judges, in the header's order: E_STATE, `at`, the size, NULL, the ops)
}

// ---- 7. MIDI -----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

thunder_midi* thunder_live_midi(thunder_live* L) { return L ? L->h->midi() : nullptr; }
int64_t thunder_live_midi_drain(thunder_live* L, thunder_midi_msg* out, int64_t cap) {
  if (!L) return THUNDER_LIVE_E_ARG;
  if (L->dead.load(std::memory_order_relaxed)) return THUNDER_LIVE_E_STATE;
  if (!L->h->midi()) return THUNDER_LIVE_E_STATE;
  if (!out || cap < 0) return THUNDER_LIVE_E_ARG;
  return L->h->midiDrain(out, cap);   // (CONSUMER: no allocation, no exception)
}
int thunder_live_midi_lead(thunder_live* L, double seconds) { return ctl(L, [&](LiveHost& h) { return h.midiLead(seconds); }); }

// ---- 8. EVENTS TO THE HOST ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------

void thunder_live_host_stats(thunder_live* L, int out_latency_frames, int underruns) { if (L) L->h->hostStats(out_latency_frames, underruns); }
int thunder_live_poll(thunder_live* L, void* buf, int cap) {
  if (!L || !buf || cap < 0) return THUNDER_LIVE_E_ARG;
  return L->h->poll(buf, cap);   // (CONSUMER: allocates nothing; it still works on a dead handle, so that the records posted before the failure can be read)
}
int thunder_live_meters_want(thunder_live* L, double rate_hz) { return ctl(L, [&](LiveHost& h) { return h.meters(rate_hz); }); }

// ---- 9. TEST ONLY ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------

int thunder_live_dump_step(thunder_live* L, char* json, int cap) {
  if (!L) return THUNDER_LIVE_E_ARG;
  if (L->dead.load(std::memory_order_relaxed) || !(L->flags & THUNDER_LIVE_F_DUMP_STEPS)) return THUNDER_LIVE_E_STATE;
  if (!json || cap < 0) return THUNDER_LIVE_E_ARG;
  return ctl(L, [&](LiveHost& h) { return h.dumpStep(json, cap); });
}

}  // extern "C"
