/* U37 P5 (ctest `live_c`): the C API of the live host, from a pure C99 translation unit (-std=c99 -Wall -Wextra -pedantic; /W4 under MSVC): every function of thunder/live.h, its success path and every
   error path that can be reached from C. Created hosts, a song and pads published, a play, the audio call, the event records, the MIDI records, the transport and the mixer, effects and master, meters, the dump
   ring, E_BUSY, the budget, the alloc / take pair, the NULL handle of every function. (The dead handle needs an exception: test_live_capi.cpp makes one.) No <unistd.h>, no VLA, nothing big on the stack. */
#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS 1   /* sprintf / fopen / strcpy: C4996 is an error under /WX (MSVC CI) */
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "thunder/live.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { g_checks++; if (!(c)) { printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); printf("" __VA_ARGS__); printf("\n"); g_fail++; } } while (0)

#if defined(THUNDER_SANITIZE_BUILD) || defined(__SANITIZE_ADDRESS__)
#define SANITIZED 1
#else
#define SANITIZED 0
#endif

#define MJ "{\"on\":true,\"comp\":{\"thr\":-25,\"ratio\":6}}"
#define SR 48000.0
#define Q THUNDER_LIVE_QUANTUM

static double nan_(void) { volatile double z = 0.0; return z / z; }
static double inf_(void) { volatile double z = 0.0; return 1.0 / z; }

static char* read_file(const char* rel, size_t* n) {
  char path[1024]; FILE* f; long sz; char* b;
  sprintf(path, "%s/%s", THUNDER_ROOT, rel);
  f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
  b = (char*)malloc((size_t)sz + 1); if (!b) { fclose(f); return NULL; }
  if (fread(b, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(b); return NULL; }
  fclose(f); b[sz] = 0; *n = (size_t)sz; return b;
}

static thunder_live_opts opts_of(unsigned flags, int max_block) { thunder_live_opts o; memset(&o, 0, sizeof o); o.size = (uint32_t)sizeof o; o.sample_rate = SR; o.max_block = max_block; o.flags = flags; return o; }
static thunder_live_play_opts play_of(int64_t t0) { thunder_live_play_opts p; memset(&p, 0, sizeof p); p.size = (uint32_t)sizeof p; p.t0 = t0; p.spp = -1; return p; }
static thunder_live* make(unsigned flags, int max_block) { thunder_live_opts o = opts_of(flags, max_block); char err[256] = {0}; thunder_live* L = thunder_live_create(&o, err, sizeof err); if (!L) printf("create failed: %s\n", err); return L; }

/* a made-up pad, as the other live tests make theirs */
#define PN 64
#define NS 24000
static void make_pad(float* x, int p) { int i; for (i = 0; i < NS; i++) x[i] = (float)sin(0.01 * (p % 7 + 1) * i + 0.7) * (1.0f - (float)i / NS) * 0.3f; }
static int load_pads(thunder_live* L, int n) {
  float* x = (float*)malloc(sizeof(float) * NS); int p, rc = 0;
  if (!x) return -99;
  for (p = 0; p < n && !rc; p++) {
    make_pad(x, p);
    rc = thunder_live_pad_set(L, p, x, NULL, NS, SR);
    if (!rc) rc = thunder_live_pad_kit(L, p, p == 3 ? 1 : 0, p >= 8 ? 1 : 0, p >= 8 ? 0.1 : 0.0);
  }
  free(x);
  if (!rc) rc = thunder_live_pads_publish(L);
  return rc;
}

/* process `n` frames in calls of `cs` (cs <= the host's max_block); the output goes to l / r (n floats each) or nowhere. Returns 0 or the first error. */
static int run(thunder_live* L, float* l, float* r, int64_t n, int cs, int64_t* frame) {
  float* a = (float*)malloc(sizeof(float) * (size_t)cs); float* b = (float*)malloc(sizeof(float) * (size_t)cs); int64_t done = 0; int rc = 0;
  if (!a || !b) { free(a); free(b); return -99; }
  while (done < n && !rc) {
    const int c = (int)(n - done < cs ? n - done : cs);
    rc = thunder_live_process(L, a, b, c, *frame, 0);
    if (!rc) { if (l) memcpy(l + done, a, sizeof(float) * (size_t)c); if (r) memcpy(r + done, b, sizeof(float) * (size_t)c); done += c; *frame += c; }
  }
  free(a); free(b); return rc;
}

/* the records waiting */
typedef struct evs { int n, by_type[16]; int states[64], state_args[64], nstates; int64_t last_seq_ok; uint32_t last_seq; int seq_ascending; int64_t song_rev, pads_id; int clock_out_lat, clock_sr, nclock; int reanchors, fatal; } evs;
static void drain_events(thunder_live* L, evs* e) {
  unsigned char buf[1024]; int n;
  while ((n = thunder_live_poll(L, buf, (int)sizeof buf)) > 0) {
    int o = 0;
    while (o < n) {
      thunder_live_ev_head h; memcpy(&h, buf + o, sizeof h);
      e->n++; if (h.type < 16) e->by_type[h.type]++;
      if (e->last_seq && (int32_t)(h.seq - e->last_seq) <= 0) e->seq_ascending = 0;
      e->last_seq = h.seq;
      if (h.type == THUNDER_LIVE_EV_STATE && e->nstates < 64) { thunder_live_ev_state s; memcpy(&s, buf + o, sizeof s); e->states[e->nstates] = s.state; e->state_args[e->nstates++] = s.arg; }
      if (h.type == THUNDER_LIVE_EV_SONG) { thunder_live_ev_song s; memcpy(&s, buf + o, sizeof s); e->song_rev = s.rev; }
      if (h.type == THUNDER_LIVE_EV_PADS) { thunder_live_ev_pads s; memcpy(&s, buf + o, sizeof s); e->pads_id = s.id; }
      if (h.type == THUNDER_LIVE_EV_CLOCK) { thunder_live_ev_clock s; memcpy(&s, buf + o, sizeof s); e->clock_out_lat = s.out_lat; e->clock_sr = s.sr; e->nclock++; }
      if (h.type == THUNDER_LIVE_EV_ERROR) { thunder_live_ev_error s; memcpy(&s, buf + o, sizeof s); if (s.where == THUNDER_LIVE_WHERE_REANCHOR) e->reanchors++; if (s.fatal) e->fatal++; }
      o += h.bytes;
    }
  }
}
static void evs_init(evs* e) { memset(e, 0, sizeof *e); e->seq_ascending = 1; }
static thunder_live_stats stats_of(thunder_live* L) { thunder_live_stats s; memset(&s, 0, sizeof s); s.size = (uint32_t)sizeof s; if (thunder_live_stats_get(L, &s)) s.size = 0; return s; }
static int finite_all(const float* x, size_t n) { size_t i; for (i = 0; i < n; i++) if (!(x[i] == x[i]) || x[i] > 1e6f || x[i] < -1e6f) return 0; return 1; }
static double peak_of(const float* x, size_t n) { size_t i; double p = 0; for (i = 0; i < n; i++) { double a = fabs((double)x[i]); if (a > p) p = a; } return p; }

/* ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ */
static void test_static(void) {
  CHECK(thunder_live_abi() == THUNDER_LIVE_ABI, "abi");
  { const uint32_t c = thunder_live_caps();
    CHECK(c & THUNDER_LIVE_CAP_MASTER_SWAP, "P5 sets CAP_MASTER_SWAP");
    CHECK(c & THUNDER_LIVE_CAP_TAKE, "pad_take is built");
    CHECK(!(c & (THUNDER_LIVE_CAP_SF | THUNDER_LIVE_CAP_LIVE_SWAP | THUNDER_LIVE_CAP_ENGINES)), "SoundFonts and live-engine pads are not in the live host: caps 0x%x", (unsigned)c); }
  /* alloc / free */
  { float* b = thunder_live_alloc(1000); size_t i;
    CHECK(b != NULL, "alloc 1000"); if (b) { for (i = 0; i < 1000; i++) b[i] = (float)i; thunder_live_free(b); }
    CHECK(thunder_live_alloc(0) == NULL && thunder_live_alloc(-5) == NULL, "alloc of 0 and of a negative count is NULL");
    CHECK(thunder_live_alloc(((int64_t)1 << 30) + 1) == NULL, "alloc above 2^30 floats is NULL");
    thunder_live_free(NULL); CHECK(1, "free(NULL)"); }
}

static void test_create(void) {
  char err[64]; thunder_live* L; thunder_live_opts o; unsigned char big[sizeof(thunder_live_opts) + 16];
  L = thunder_live_create(NULL, NULL, 0); CHECK(L != NULL, "opts NULL: all defaults, err NULL"); { thunder_live_stats s = stats_of(L); CHECK(s.size && s.state == THUNDER_LIVE_STATE_IDLE && s.pad_budget == THUNDER_LIVE_DEFAULT_PAD_BYTES, "defaults: IDLE, the 256 MiB budget"); } thunder_live_destroy(L);
  thunder_live_destroy(NULL); CHECK(1, "destroy(NULL)");
  o = opts_of(0, 0); o.sample_rate = 0; L = thunder_live_create(&o, err, sizeof err); CHECK(L != NULL, "sample_rate 0 is 48000"); thunder_live_destroy(L);
  o = opts_of(0, 0); o.sample_rate = 7999; strcpy(err, "x"); L = thunder_live_create(&o, err, sizeof err); CHECK(L == NULL && err[0] && strstr(err, "sample_rate"), "rate below 8000: NULL, err says '%s'", err);
  o = opts_of(0, 0); o.sample_rate = 384001; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "rate above 384000");
  o = opts_of(0, 0); o.sample_rate = nan_(); CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "rate NaN");
  o = opts_of(0, 0); o.sample_rate = inf_(); CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "rate inf");
  o = opts_of(0, -1); CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "max_block -1");
  o = opts_of(0, 1048577); CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "max_block above 2^20");
  o = opts_of(0, 0); o.max_voices = 257; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "max_voices 257");
  o = opts_of(0, 0); o.max_voices = -1; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "max_voices -1");
  o = opts_of(0, 0); o.max_pad_bytes = ((uint64_t)1 << 31) + 1; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "max_pad_bytes above 2^31");
  o = opts_of(0, 0); o.max_song_bytes = (uint64_t)THUNDER_LIVE_MAX_SONG_BYTES + 1; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "max_song_bytes above 16 MiB");
  o = opts_of(0, 0); o.dump_steps = -1; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "dump_steps -1");
  o = opts_of(0x8000u, 0); CHECK(thunder_live_create(&o, err, sizeof err) == NULL && strstr(err, "flag"), "an unknown flag: '%s'", err);
  o = opts_of(0, 0); o.reserved0 = 1; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "reserved0");
  o = opts_of(0, 0); o.reserved2 = 1; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "reserved2");
  /* err: truncated to err_cap, NUL-terminated; NULL with cap 0 is fine */
  o = opts_of(0, -1); { char e4[4]; memset(e4, 'z', sizeof e4); CHECK(thunder_live_create(&o, e4, sizeof e4) == NULL && e4[3] == 0 && strlen(e4) == 3, "err truncated to its capacity, NUL-terminated"); }
  CHECK(thunder_live_create(&o, NULL, 0) == NULL, "a bad option with err NULL");
  CHECK(thunder_live_create(&o, NULL, 100) == NULL, "err NULL with a capacity is not read");
  /* the ABI rule: size */
  o = opts_of(0, 0); o.size = 0; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "size 0");
  o = opts_of(0, 0); o.size = 3; CHECK(thunder_live_create(&o, err, sizeof err) == NULL, "size 3");
  o = opts_of(0, 0); o.size = 4; L = thunder_live_create(&o, err, sizeof err); CHECK(L != NULL, "size 4: every other field is its default"); thunder_live_destroy(L);
  o = opts_of(THUNDER_LIVE_F_METERS, 0); o.size = 12; L = thunder_live_create(&o, err, sizeof err); CHECK(L != NULL, "size 12 reaches the sample rate only"); { thunder_live_stats s = stats_of(L); CHECK(s.pad_budget == THUNDER_LIVE_DEFAULT_PAD_BYTES, "a field past the caller's size keeps its default"); } CHECK(thunder_live_meters_want(L, 10) == THUNDER_LIVE_E_STATE, "flags (past size 12) are not read: no F_METERS"); thunder_live_destroy(L);
  memset(big, 0, sizeof big); o = opts_of(0, 0); memcpy(big, &o, sizeof o); { uint32_t sz = (uint32_t)sizeof big; memcpy(big, &sz, 4); }
  L = thunder_live_create((const thunder_live_opts*)big, err, sizeof err); CHECK(L != NULL, "a newer caller whose extra fields are zero"); thunder_live_destroy(L);
  big[sizeof(thunder_live_opts) + 5] = 1; CHECK(thunder_live_create((const thunder_live_opts*)big, err, sizeof err) == NULL, "a newer caller with a field this build does not know set: NULL");
}

/* every int function with a NULL handle is E_ARG; the void and pointer ones do not crash */
static void test_null_handle(void) {
  float a[8], b[8]; char err[16] = "x"; thunder_live_stats st; thunder_live_play_opts po = play_of(0); thunder_midi_msg m[2]; unsigned char buf[600]; double p12[12] = {0}; char js[8];
  memset(&st, 0, sizeof st); st.size = sizeof st;
  CHECK(thunder_live_process(NULL, a, b, 8, 0, 0) == THUNDER_LIVE_E_ARG, "process");
  CHECK(thunder_live_stats_get(NULL, &st) == THUNDER_LIVE_E_ARG, "stats_get");
  { int64_t off = 5; CHECK(thunder_live_engine_offset(NULL, &off) == THUNDER_LIVE_E_ARG && off == 5, "engine_offset(NULL)"); }
  CHECK(thunder_live_song_publish(NULL, (const uint8_t*)"{}", 2, NULL, 0, 1, err, sizeof err) == THUNDER_LIVE_E_ARG && err[0] == 0, "song_publish (err cleared)");
  CHECK(thunder_live_pad_set(NULL, 0, a, NULL, 8, SR) == THUNDER_LIVE_E_ARG, "pad_set");
  CHECK(thunder_live_pad_take(NULL, 0, a, 0, 8, SR) == THUNDER_LIVE_E_ARG, "pad_take");
  CHECK(thunder_live_pad_kit(NULL, 0, 0, 0, 0) == THUNDER_LIVE_E_ARG, "pad_kit");
  CHECK(thunder_live_pad_route(NULL, 0, 0) == THUNDER_LIVE_E_ARG, "pad_route");
  CHECK(thunder_live_var_set(NULL, 0, 'g', 0, 0, a, 8, SR) == THUNDER_LIVE_E_ARG, "var_set");
  CHECK(thunder_live_pad_chop(NULL, 0, 1.0) == THUNDER_LIVE_E_ARG, "pad_chop");
  CHECK(thunder_live_pad_hits(NULL, 0, 4, NULL, 0) == THUNDER_LIVE_E_ARG, "pad_hits");
  CHECK(thunder_live_font_add(NULL, (const uint8_t*)"x", 1) == THUNDER_LIVE_E_ARG, "font_add");
  CHECK(thunder_live_font_remove(NULL, 0) == THUNDER_LIVE_E_ARG, "font_remove");
  CHECK(thunder_live_pad_sf(NULL, 0, 0, 0, 0, NULL, NULL, 0) == THUNDER_LIVE_E_ARG, "pad_sf");
  CHECK(thunder_live_pad_sf_clear(NULL, 0) == THUNDER_LIVE_E_ARG, "pad_sf_clear");
  CHECK(thunder_live_pad_live(NULL, 0, 1, NULL, 0, NULL) == THUNDER_LIVE_E_ARG, "pad_live");
  CHECK(thunder_live_pad_live_clear(NULL, 0) == THUNDER_LIVE_E_ARG, "pad_live_clear");
  CHECK(thunder_live_pads_publish(NULL) == THUNDER_LIVE_E_ARG, "pads_publish");
  CHECK(thunder_live_collect(NULL) == THUNDER_LIVE_E_ARG, "collect");
  CHECK(thunder_live_play(NULL, &po) == THUNDER_LIVE_E_ARG, "play");
  CHECK(thunder_live_stop(NULL, -1) == THUNDER_LIVE_E_ARG, "stop");
  CHECK(thunder_live_hold(NULL, -1) == THUNDER_LIVE_E_ARG, "hold");
  CHECK(thunder_live_loop(NULL, 0, 0, 0, -1) == THUNDER_LIVE_E_ARG, "loop");
  CHECK(thunder_live_bpm(NULL, 120, -1) == THUNDER_LIVE_E_ARG, "bpm");
  CHECK(thunder_live_lead(NULL, 0.25, -1) == THUNDER_LIVE_E_ARG, "lead");
  CHECK(thunder_live_perf(NULL, 0, 0, 0, -1) == THUNDER_LIVE_E_ARG, "perf");
  CHECK(thunder_live_stop_track(NULL, 0, -1) == THUNDER_LIVE_E_ARG, "stop_track");
  CHECK(thunder_live_mix_set(NULL, 0, 0, 0, 0, -1) == THUNDER_LIVE_E_ARG, "mix_set");
  CHECK(thunder_live_pdc_set(NULL, 0) == THUNDER_LIVE_E_ARG, "pdc_set");
  CHECK(thunder_live_fx_plan(NULL, 0, (const uint8_t*)"[]", 2, 120) == THUNDER_LIVE_E_ARG, "fx_plan");
  CHECK(thunder_live_fx_commit(NULL, 0, -1) == THUNDER_LIVE_E_ARG, "fx_commit");
  CHECK(thunder_live_fx_param(NULL, 0, 0, p12, -1) == THUNDER_LIVE_E_ARG, "fx_param");
  CHECK(thunder_live_master_config(NULL, NULL, 0, 0) == THUNDER_LIVE_E_ARG, "master_config");
  CHECK(thunder_live_midi(NULL) == NULL, "midi: NULL");
  CHECK(thunder_live_midi_drain(NULL, m, 2) == THUNDER_LIVE_E_ARG, "midi_drain");
  CHECK(thunder_live_midi_lead(NULL, 0.1) == THUNDER_LIVE_E_ARG, "midi_lead");
  thunder_live_host_stats(NULL, 1, 2); CHECK(1, "host_stats(NULL) is a no-op");
  CHECK(thunder_live_poll(NULL, buf, (int)sizeof buf) == THUNDER_LIVE_E_ARG, "poll");
  CHECK(thunder_live_meters_want(NULL, 10) == THUNDER_LIVE_E_ARG, "meters_want");
  CHECK(thunder_live_dump_step(NULL, js, (int)sizeof js) == THUNDER_LIVE_E_ARG, "dump_step");
  /* a NULL handle writes no output (nothing is known of the buffers) */
  a[0] = 5.f; b[0] = 6.f; thunder_live_process(NULL, a, b, 8, 0, 0); CHECK(a[0] == 5.f && b[0] == 6.f, "process(NULL) leaves the buffers alone");
}

/* IDLE host: argument errors of every call that can be judged without a song, the states, the process contract */
static void test_idle_and_args(void) {
  thunder_live* L = make(0, 1024); thunder_live* Lall; float a[2048], b[2048]; float* x; int64_t fr = 0; double p12[12]; unsigned char buf[600]; char err[128]; int i;
  if (!L) { CHECK(0, "make"); return; }
  /* process */
  for (i = 0; i < 16; i++) { a[i] = 9.f; b[i] = 9.f; }
  CHECK(thunder_live_process(L, a, b, 16, 0, 0) == 0 && a[0] == 0.f && b[15] == 0.f, "an IDLE host is silent");
  CHECK(thunder_live_process(L, a, b, 0, 16, 0) == 0, "frames 0 is fine");
  CHECK(thunder_live_process(L, NULL, b, 16, 16, 0) == THUNDER_LIVE_E_ARG, "null out_l");
  CHECK(thunder_live_process(L, a, NULL, 16, 16, 0) == THUNDER_LIVE_E_ARG, "null out_r");
  for (i = 0; i < 16; i++) a[i] = 9.f;
  CHECK(thunder_live_process(L, a, b, 1025, 16, 0) == THUNDER_LIVE_E_ARG, "frames above max_block");
  CHECK(thunder_live_process(L, a, b, -1, 16, 0) == THUNDER_LIVE_E_ARG, "negative frames");
  for (i = 0; i < 16; i++) a[i] = 9.f;
  CHECK(thunder_live_process(L, a, b, 16, -1, 0) == THUNDER_LIVE_E_ARG && a[0] == 0.f && a[15] == 0.f, "frame0 < 0: E_ARG, the outputs zeroed");
  for (i = 0; i < 16; i++) a[i] = 9.f;
  CHECK(thunder_live_process(L, a, b, 16, ((int64_t)1 << 62) + 1, 0) == THUNDER_LIVE_E_ARG && a[3] == 0.f, "frame0 above 2^62: E_ARG, the outputs zeroed");
  CHECK(thunder_live_process(L, a, a + 8, 16, 16, 0) == THUNDER_LIVE_E_ARG, "overlapping buffers");
  CHECK(thunder_live_process(L, a, a, 16, 16, 0) == THUNDER_LIVE_E_ARG, "the same buffer twice");
  CHECK(thunder_live_process(L, a, a + 16, 16, 16, 0) == 0, "adjacent buffers do not overlap");
  /* stats */
  { thunder_live_stats s; memset(&s, 0, sizeof s);
    CHECK(thunder_live_stats_get(L, NULL) == THUNDER_LIVE_E_ARG, "stats_get(out NULL)");
    s.size = 0; CHECK(thunder_live_stats_get(L, &s) == THUNDER_LIVE_E_ARG, "stats size 0");
    s.size = 8; s.frames = 77; CHECK(thunder_live_stats_get(L, &s) == 0 && s.state == THUNDER_LIVE_STATE_IDLE && s.frames == 77 && s.size == 8, "a short struct: only its own fields are written");
    s = stats_of(L); CHECK(s.size == sizeof s && s.calls >= 2 && s.pad_budget == THUNDER_LIVE_DEFAULT_PAD_BYTES && s.thread_prepares <= 1, "stats: calls %llu, thread_prepares %u", (unsigned long long)s.calls, s.thread_prepares); }
  /* play with nothing published */
  { thunder_live_play_opts po = play_of(THUNDER_LIVE_NOW); CHECK(thunder_live_play(L, &po) == THUNDER_LIVE_E_STATE, "play with no song: E_STATE"); }
  CHECK(thunder_live_pdc_set(L, 1) == THUNDER_LIVE_E_STATE, "pdc_set with no song: E_STATE");
  /* the song */
  CHECK(thunder_live_song_publish(L, NULL, 10, NULL, 0, 1, err, sizeof err) == THUNDER_LIVE_E_ARG, "song NULL");
  CHECK(thunder_live_song_publish(L, (const uint8_t*)"{}", 0, NULL, 0, 1, err, sizeof err) == THUNDER_LIVE_E_ARG, "song empty");
  CHECK(thunder_live_song_publish(L, (const uint8_t*)"{}", 2, NULL, 5, 1, err, sizeof err) == THUNDER_LIVE_E_ARG, "vars_bytes without vars");
  strcpy(err, "stale"); CHECK(thunder_live_song_publish(L, (const uint8_t*)"{not a song", 11, NULL, 0, 1, err, sizeof err) == THUNDER_LIVE_E_SONG && err[0] && strcmp(err, "stale") != 0, "not JSON: E_SONG, err says '%s'", err);
  CHECK(thunder_live_song_publish(L, (const uint8_t*)"[1,2]", 5, NULL, 0, 1, NULL, 0) == THUNDER_LIVE_E_SONG, "a JSON array is not a song (err NULL)");
  { size_t n = 0; char* song = read_file("sounds/ELECTRO.thunder", &n);
    if (!song) CHECK(0, "cannot read ELECTRO");
    else {
      CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, (const uint8_t*)"[1]", 3, 1, err, sizeof err) == THUNDER_LIVE_E_SONG, "vars that are not an object");
      CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, (const uint8_t*)"{broken", 7, 1, err, sizeof err) == THUNDER_LIVE_E_SONG, "vars that are not JSON");
      { uint8_t* hv = (uint8_t*)malloc((size_t)THUNDER_LIVE_MAX_VARS_BYTES + 1);
        if (hv) { memset(hv, ' ', (size_t)THUNDER_LIVE_MAX_VARS_BYTES + 1); CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, hv, (size_t)THUNDER_LIVE_MAX_VARS_BYTES + 1, 1, err, sizeof err) == THUNDER_LIVE_E_LIMIT, "vars above 4 MiB"); free(hv); } }
      { thunder_live_opts o = opts_of(0, 0); thunder_live* S; o.max_song_bytes = 1000; S = thunder_live_create(&o, err, sizeof err);
        if (S) { CHECK(thunder_live_song_publish(S, (const uint8_t*)song, n, NULL, 0, 1, err, sizeof err) == THUNDER_LIVE_E_LIMIT, "a song above opts.max_song_bytes"); thunder_live_destroy(S); } }
      CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, (const uint8_t*)"{}", 2, 7, err, sizeof err) == 0, "ELECTRO with empty vars");
      CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, NULL, 0, 8, NULL, 0) == 0, "ELECTRO, no vars, err NULL");
      free(song);
    } }
  /* pads */
  x = (float*)malloc(sizeof(float) * 2000); for (i = 0; i < 2000; i++) x[i] = 0.1f;
  CHECK(thunder_live_pad_set(L, -1, x, NULL, 100, SR) == THUNDER_LIVE_E_ARG, "pad -1");
  CHECK(thunder_live_pad_set(L, 256, x, NULL, 100, SR) == THUNDER_LIVE_E_ARG, "pad 256");
  CHECK(thunder_live_pad_set(L, 0, x, NULL, -1, SR) == THUNDER_LIVE_E_ARG, "frames -1");
  CHECK(thunder_live_pad_set(L, 0, NULL, NULL, 100, SR) == THUNDER_LIVE_E_ARG, "l NULL with frames");
  CHECK(thunder_live_pad_set(L, 0, x, NULL, 100, 0.0) == THUNDER_LIVE_E_ARG, "rate 0");
  CHECK(thunder_live_pad_set(L, 0, x, NULL, 100, 1e6 + 1) == THUNDER_LIVE_E_ARG, "rate above 1e6");
  CHECK(thunder_live_pad_set(L, 0, x, NULL, 100, nan_()) == THUNDER_LIVE_E_ARG, "rate NaN");
  CHECK(thunder_live_pad_set(L, 0, x, NULL, (int64_t)THUNDER_LIVE_MAX_PAD_FRAMES + 1, SR) == THUNDER_LIVE_E_LIMIT, "more than 2^28 frames");
  x[50] = (float)nan_(); CHECK(thunder_live_pad_set(L, 0, x, NULL, 100, SR) == THUNDER_LIVE_E_ARG, "a NaN sample"); x[50] = (float)inf_(); CHECK(thunder_live_pad_set(L, 0, x, NULL, 100, SR) == THUNDER_LIVE_E_ARG, "an infinite sample"); x[50] = 0.1f;
  CHECK(thunder_live_pad_set(L, 0, x, NULL, 100, SR) == 0, "a mono pad");
  CHECK(thunder_live_pad_set(L, 1, x, x + 1000, 100, SR) == 0, "a stereo pad");
  CHECK(thunder_live_pad_set(L, 1, x, x + 1000, 100, SR) == 0, "the same again: nothing changes");
  CHECK(thunder_live_pad_set(L, 2, NULL, NULL, 0, SR) == 0, "frames 0 removes a pad (l NULL)");
  CHECK(thunder_live_pad_kit(L, -1, 0, 0, 0) == THUNDER_LIVE_E_ARG && thunder_live_pad_kit(L, 256, 0, 0, 0) == THUNDER_LIVE_E_ARG, "kit: pad");
  CHECK(thunder_live_pad_kit(L, 0, -1, 0, 0) == THUNDER_LIVE_E_ARG && thunder_live_pad_kit(L, 0, 256, 0, 0) == THUNDER_LIVE_E_ARG, "kit: choke");
  CHECK(thunder_live_pad_kit(L, 0, 0, 2, 0) == THUNDER_LIVE_E_ARG && thunder_live_pad_kit(L, 0, 0, -1, 0) == THUNDER_LIVE_E_ARG, "kit: syn");
  CHECK(thunder_live_pad_kit(L, 0, 0, 0, -0.5) == THUNDER_LIVE_E_ARG && thunder_live_pad_kit(L, 0, 0, 0, 60.5) == THUNDER_LIVE_E_ARG && thunder_live_pad_kit(L, 0, 0, 0, nan_()) == THUNDER_LIVE_E_ARG, "kit: rel");
  CHECK(thunder_live_pad_kit(L, 0, 255, 1, 60.0) == 0, "kit: the limits are accepted");
  CHECK(thunder_live_pad_route(L, -1, 0) == THUNDER_LIVE_E_ARG && thunder_live_pad_route(L, 256, 0) == THUNDER_LIVE_E_ARG && thunder_live_pad_route(L, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_pad_route(L, 0, 3) == THUNDER_LIVE_E_ARG, "route");
  CHECK(thunder_live_pad_route(L, 0, THUNDER_RENDER_ROUTE_SAMPLER) == 0 && thunder_live_pad_route(L, 0, THUNDER_RENDER_ROUTE_LIVE) == 0 && thunder_live_pad_route(L, 0, THUNDER_RENDER_ROUTE_SF) == 0, "route: the three routes");
  CHECK(thunder_live_pad_route(L, 0, THUNDER_RENDER_ROUTE_SAMPLER) == 0, "route back");
  /* variations */
  CHECK(thunder_live_var_set(L, -1, 'g', 0, 0, x, 50, SR) == THUNDER_LIVE_E_ARG && thunder_live_var_set(L, 64, 'g', 0, 0, x, 50, SR) == THUNDER_LIVE_E_ARG, "var: ii");
  CHECK(thunder_live_var_set(L, 0, 'x', 0, 0, x, 50, SR) == THUNDER_LIVE_E_ARG, "var: kind");
  CHECK(thunder_live_var_set(L, 0, 'g', -1, 0, x, 50, SR) == THUNDER_LIVE_E_ARG && thunder_live_var_set(L, 0, 'g', 256, 0, x, 50, SR) == THUNDER_LIVE_E_ARG, "var: num");
  CHECK(thunder_live_var_set(L, 0, 'g', 0, 99, x, 50, SR) == THUNDER_LIVE_E_ARG, "var: a pad with no audio staged");
  CHECK(thunder_live_var_set(L, 0, 'g', 0, 0, NULL, 50, SR) == THUNDER_LIVE_E_ARG && thunder_live_var_set(L, 0, 'g', 0, 0, x, 50, 0.0) == THUNDER_LIVE_E_ARG, "var: l NULL, rate 0");
  x[10] = (float)nan_(); CHECK(thunder_live_var_set(L, 0, 'g', 0, 0, x, 50, SR) == THUNDER_LIVE_E_ARG, "var: a NaN sample"); x[10] = 0.1f;
  CHECK(thunder_live_var_set(L, 0, 'g', 0, 0, x, 50, SR) == 0 && thunder_live_var_set(L, 1, 'm', 3, 0, x, 60, SR) == 0, "two variations");
  CHECK(thunder_live_var_set(L, 0, 'g', 0, 0, NULL, 0, SR) == 0, "frames 0 removes a variation");
  /* chop and hits */
  CHECK(thunder_live_pad_chop(L, -1, 1.0) == THUNDER_LIVE_E_ARG && thunder_live_pad_chop(L, 256, 1.0) == THUNDER_LIVE_E_ARG, "chop: pad");
  CHECK(thunder_live_pad_chop(L, 0, -1.0) == THUNDER_LIVE_E_ARG && thunder_live_pad_chop(L, 0, 1e6 + 1) == THUNDER_LIVE_E_ARG && thunder_live_pad_chop(L, 0, nan_()) == THUNDER_LIVE_E_ARG, "chop: seconds");
  CHECK(thunder_live_pad_chop(L, 0, 0.5) == 0 && thunder_live_pad_chop(L, 0, 0.0) == 0, "chop set and removed");
  { double pts[4] = {0.0, 0.25, 0.5, 0.75}, bad[2] = {0.1, 0.0}; bad[1] = nan_();
    CHECK(thunder_live_pad_hits(L, -1, 4, pts, 4) == THUNDER_LIVE_E_ARG && thunder_live_pad_hits(L, 0, 1, pts, 1) == THUNDER_LIVE_E_ARG && thunder_live_pad_hits(L, 0, 65, pts, 1) == THUNDER_LIVE_E_ARG, "hits: pad, slices");
    CHECK(thunder_live_pad_hits(L, 0, 4, pts, 5) == THUNDER_LIVE_E_ARG && thunder_live_pad_hits(L, 0, 4, pts, -1) == THUNDER_LIVE_E_ARG && thunder_live_pad_hits(L, 0, 4, NULL, 2) == THUNDER_LIVE_E_ARG, "hits: count, NULL points");
    CHECK(thunder_live_pad_hits(L, 0, 4, bad, 2) == THUNDER_LIVE_E_ARG, "hits: a NaN point");
    CHECK(thunder_live_pad_hits(L, 0, 4, pts, 4) == 0 && thunder_live_pad_hits(L, 0, 4, NULL, 0) == 0, "hits set and removed"); }
  /* the budget */
  { thunder_live_opts o = opts_of(0, 0); thunder_live* S; float* big = (float*)malloc(sizeof(float) * 300000); o.max_pad_bytes = (uint64_t)1 << 20; S = thunder_live_create(&o, err, sizeof err);
    if (S && big) { for (i = 0; i < 300000; i++) big[i] = 0.01f;
      CHECK(thunder_live_pad_set(S, 0, big, NULL, 300000, SR) == THUNDER_LIVE_E_LIMIT, "a mono pad of 1.2 MB in a 1 MiB budget: E_LIMIT");
      CHECK(thunder_live_pad_set(S, 0, big, NULL, 200000, SR) == 0, "800 KB fits");
      CHECK(thunder_live_pad_set(S, 1, big, NULL, 200000, SR) == THUNDER_LIVE_E_LIMIT, "the second does not");
      { thunder_live_stats s = stats_of(S); CHECK(s.pad_bytes_held >= 800000 && s.pad_budget == (uint64_t)1 << 20, "held %llu", (unsigned long long)s.pad_bytes_held); }
      CHECK(thunder_live_var_set(S, 0, 'g', 0, 0, big, 200000, SR) == THUNDER_LIVE_E_LIMIT, "a variation past the budget"); }
    free(big); thunder_live_destroy(S); }
  /* pad_take with a block from alloc */
  { float* blk = thunder_live_alloc(2 * 1000); thunder_live_stats s0, s1;
    CHECK(blk != NULL, "alloc");
    CHECK(thunder_live_pad_take(L, 5, blk, 2, 1000, SR) == THUNDER_LIVE_E_ARG && thunder_live_pad_take(L, 5, blk, -1, 1000, SR) == THUNDER_LIVE_E_ARG, "take: stereo not 0 / 1");
    CHECK(thunder_live_pad_take(L, 5, NULL, 0, 1000, SR) == THUNDER_LIVE_E_ARG, "take: block NULL");
    CHECK(thunder_live_pad_take(L, -1, blk, 0, 1000, SR) == THUNDER_LIVE_E_ARG && thunder_live_pad_take(L, 256, blk, 0, 1000, SR) == THUNDER_LIVE_E_ARG, "take: pad");
    CHECK(thunder_live_pad_take(L, 5, blk, 0, 0, SR) == THUNDER_LIVE_E_ARG && thunder_live_pad_take(L, 5, blk, 0, -3, SR) == THUNDER_LIVE_E_ARG, "take: frames");
    CHECK(thunder_live_pad_take(L, 5, blk, 0, 1000, 0.0) == THUNDER_LIVE_E_ARG && thunder_live_pad_take(L, 5, blk, 0, 1000, nan_()) == THUNDER_LIVE_E_ARG, "take: rate");
    CHECK(thunder_live_pad_take(L, 5, blk, 0, (int64_t)THUNDER_LIVE_MAX_PAD_FRAMES + 1, SR) == THUNDER_LIVE_E_LIMIT, "take: frames above the limit");
    for (i = 0; i < 2000; i++) blk[i] = 0.2f;
    blk[777] = (float)nan_(); CHECK(thunder_live_pad_take(L, 5, blk, 0, 1000, SR) == THUNDER_LIVE_E_ARG, "take: a NaN sample, in place"); blk[777] = 0.2f;
    s0 = stats_of(L);
    CHECK(thunder_live_pad_take(L, 5, blk, 1, 1000, SR) == 0, "take: a stereo block of 2 * 1000 floats is the host's now");
    s1 = stats_of(L); CHECK(s1.pad_bytes_held >= s0.pad_bytes_held + 8000, "take: the budget counts it (%llu -> %llu)", (unsigned long long)s0.pad_bytes_held, (unsigned long long)s1.pad_bytes_held);
    blk = thunder_live_alloc(500); for (i = 0; i < 500; i++) blk[i] = 0.1f;
    CHECK(thunder_live_pad_take(L, 6, blk, 0, 500, SR) == 0, "take: a mono block");
    blk = thunder_live_alloc(100); CHECK(blk != NULL && thunder_live_pad_take(L, 256, blk, 0, 100, SR) == THUNDER_LIVE_E_ARG, "take: on an error the block is still the caller's"); thunder_live_free(blk); }
  CHECK(thunder_live_pads_publish(L) == 0, "publish what is staged"); CHECK(thunder_live_pads_publish(L) == 0, "publish with nothing staged: 0");
  CHECK(thunder_live_collect(L) >= 0, "collect");
  free(x);
  /* unsupported parts: every answer is E_UNSUPPORTED whatever the arguments (the live host has no SoundFonts / live-engine pads yet) */
  CHECK(thunder_live_font_add(L, (const uint8_t*)"RIFF", 4) == THUNDER_LIVE_E_UNSUPPORTED && thunder_live_font_add(L, NULL, 0) == THUNDER_LIVE_E_UNSUPPORTED, "font_add");
  CHECK(thunder_live_font_remove(L, 0) == THUNDER_LIVE_E_UNSUPPORTED, "font_remove");
  CHECK(thunder_live_pad_sf(L, 0, 0, 0, 0, NULL, NULL, 0) == THUNDER_LIVE_E_UNSUPPORTED, "pad_sf");
  CHECK(thunder_live_pad_live(L, 0, THUNDER_RENDER_LIVE_BASS, (const uint8_t*)"{}", 2, NULL) == THUNDER_LIVE_E_UNSUPPORTED, "pad_live");
  CHECK(thunder_live_pad_sf_clear(L, 0) == 0 && thunder_live_pad_live_clear(L, 0) == 0, "the clears: 0 (nothing to clear)");
  CHECK(thunder_live_pad_sf_clear(L, -1) == THUNDER_LIVE_E_ARG && thunder_live_pad_sf_clear(L, 256) == THUNDER_LIVE_E_ARG && thunder_live_pad_live_clear(L, -1) == THUNDER_LIVE_E_ARG && thunder_live_pad_live_clear(L, 256) == THUNDER_LIVE_E_ARG, "the clears: a bad pad");
  /* transport arguments (an IDLE host: queued, applied and dropped at the next quantum) */
  CHECK(thunder_live_loop(L, 2, 0, 1, -1) == THUNDER_LIVE_E_ARG && thunder_live_loop(L, -1, 0, 1, -1) == THUNDER_LIVE_E_ARG, "loop: has");
  CHECK(thunder_live_loop(L, 1, 3, 2, -1) == THUNDER_LIVE_E_ARG && thunder_live_loop(L, 1, -1, 2, -1) == THUNDER_LIVE_E_ARG && thunder_live_loop(L, 1, 0, -1, -1) == THUNDER_LIVE_E_ARG, "loop: y0 > y1, negative");
  CHECK(thunder_live_loop(L, 1, 0, 3, -1) == 0 && thunder_live_loop(L, 0, 0, 0, -1) == 0, "loop");
  CHECK(thunder_live_bpm(L, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_bpm(L, -5, -1) == THUNDER_LIVE_E_ARG && thunder_live_bpm(L, nan_(), -1) == THUNDER_LIVE_E_ARG && thunder_live_bpm(L, inf_(), -1) == THUNDER_LIVE_E_ARG, "bpm");
  CHECK(thunder_live_bpm(L, 1e9, -1) == 0, "bpm: a finite positive value is clamped by the Seq, not refused");
  CHECK(thunder_live_lead(L, 0.04, -1) == THUNDER_LIVE_E_ARG && thunder_live_lead(L, 2.01, -1) == THUNDER_LIVE_E_ARG && thunder_live_lead(L, nan_(), -1) == THUNDER_LIVE_E_ARG, "lead");
  CHECK(thunder_live_lead(L, 0.05, -1) == 0 && thunder_live_lead(L, 2.0, -1) == 0 && thunder_live_lead(L, 0.25, -1) == 0, "lead: the limits");
  CHECK(thunder_live_perf(L, -1, 0, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_perf(L, 3, 0, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_perf(L, THUNDER_LIVE_PERF_XF, 0, nan_(), -1) == THUNDER_LIVE_E_ARG, "perf: what, NaN");
  CHECK(thunder_live_perf(L, THUNDER_LIVE_PERF_MACRO, 8, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_perf(L, THUNDER_LIVE_PERF_MACRO, -1, 0, -1) == THUNDER_LIVE_E_ARG, "perf: macro index");
  CHECK(thunder_live_perf(L, THUNDER_LIVE_PERF_FILL, 0, 0.5, -1) == THUNDER_LIVE_E_ARG, "perf: fill is 0 / 1");
  CHECK(thunder_live_perf(L, THUNDER_LIVE_PERF_XF, 0, 0.5, -1) == 0 && thunder_live_perf(L, THUNDER_LIVE_PERF_MACRO, 7, 0.5, -1) == 0 && thunder_live_perf(L, THUNDER_LIVE_PERF_FILL, 0, 1, -1) == 0, "perf");
  CHECK(thunder_live_stop_track(L, -1, -1) == THUNDER_LIVE_E_ARG && thunder_live_stop_track(L, 32, -1) == THUNDER_LIVE_E_ARG && thunder_live_stop_track(L, 31, -1) == 0, "stop_track");
  CHECK(thunder_live_stop(L, ((int64_t)1 << 62) + 1) == THUNDER_LIVE_E_ARG && thunder_live_hold(L, ((int64_t)1 << 62) + 1) == THUNDER_LIVE_E_ARG && thunder_live_bpm(L, 100, ((int64_t)1 << 62) + 1) == THUNDER_LIVE_E_ARG, "`at` above 2^62");
  CHECK(thunder_live_hold(L, -1) == 0 && thunder_live_stop(L, -1) == 0, "hold and stop on an IDLE host");
  /* the mixer */
  CHECK(thunder_live_mix_set(L, -1, THUNDER_LIVE_MIX_VOL, 100, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 33, THUNDER_LIVE_MIX_VOL, 100, 0, -1) == THUNDER_LIVE_E_ARG, "mix: track");
  CHECK(thunder_live_mix_set(L, 0, 5, 100, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 0, -1, 100, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 0, 7, 100, 0, -1) == THUNDER_LIVE_E_ARG, "mix: key 5 (Send) and out of range");
  CHECK(thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, -1, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, 256, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, nan_(), 0, -1) == THUNDER_LIVE_E_ARG, "mix: value 0 .. 255");
  CHECK(thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, 100, nan_(), -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, 100, inf_(), -1) == THUNDER_LIVE_E_ARG, "mix: ramp");
  CHECK(thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_MUTE, 2, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_SOLO, 0.5, 0, -1) == THUNDER_LIVE_E_ARG, "mix: mute / solo are 0 / 1");
  CHECK(thunder_live_mix_set(L, 32, THUNDER_LIVE_MIX_MUTE, 1, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 32, THUNDER_LIVE_MIX_SOLO, 1, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 32, THUNDER_LIVE_MIX_TRIM, 1, 0, -1) == THUNDER_LIVE_E_ARG && thunder_live_mix_set(L, 32, THUNDER_LIVE_MIX_PAN, 1, 0, -1) == THUNDER_LIVE_E_ARG, "mix: the master has VOL and OUT only");
  CHECK(thunder_live_mix_set(L, 3, THUNDER_LIVE_MIX_OUT, 1, 0, -1) == THUNDER_LIVE_E_ARG, "mix: OUT is the master's");
  CHECK(thunder_live_mix_set(L, 3, THUNDER_LIVE_MIX_VOL, 100, -1, -1) == 0 && thunder_live_mix_set(L, 3, THUNDER_LIVE_MIX_PAN, 128, 0, -1) == 0 && thunder_live_mix_set(L, 3, THUNDER_LIVE_MIX_MUTE, 1, 480, -1) == 0 && thunder_live_mix_set(L, 32, THUNDER_LIVE_MIX_OUT, 200, 0, -1) == 0 && thunder_live_mix_set(L, 32, THUNDER_LIVE_MIX_VOL, 255, 0, 1000) == 0, "mix_set");
  CHECK(thunder_live_pdc_set(L, 0) == 0 && thunder_live_pdc_set(L, 2) == 0, "pdc_set");
  CHECK(thunder_live_pdc_set(L, -1) == THUNDER_LIVE_E_ARG && thunder_live_pdc_set(L, 3) == THUNDER_LIVE_E_ARG, "pdc_set: mode");
  /* what a host without F_EFFECTS / F_MASTER / F_METERS / F_MIDI / F_DUMP_STEPS answers */
  for (i = 0; i < 12; i++) p12[i] = 0.5;
  CHECK(thunder_live_fx_plan(L, 0, (const uint8_t*)"[]", 2, 120) == THUNDER_LIVE_E_STATE && thunder_live_fx_commit(L, 0, -1) == THUNDER_LIVE_E_STATE && thunder_live_fx_param(L, 0, 0, p12, -1) == THUNDER_LIVE_E_STATE, "effects without F_EFFECTS: E_STATE");
  CHECK(thunder_live_master_config(L, NULL, 0, 0) == THUNDER_LIVE_E_STATE, "master_config without F_MASTER: E_STATE");
  CHECK(thunder_live_meters_want(L, 10) == THUNDER_LIVE_E_STATE, "meters_want without F_METERS: E_STATE");
  CHECK(thunder_live_midi(L) == NULL && thunder_live_midi_lead(L, 0.1) == THUNDER_LIVE_E_STATE, "MIDI without F_MIDI: NULL / E_STATE");
  { thunder_midi_msg m[2]; CHECK(thunder_live_midi_drain(L, m, 2) == THUNDER_LIVE_E_STATE, "midi_drain without F_MIDI"); }
  { char js[64]; CHECK(thunder_live_dump_step(L, js, (int)sizeof js) == THUNDER_LIVE_E_STATE, "dump_step without F_DUMP_STEPS"); }
  /* poll */
  { evs e; evs_init(&e); fr = 16 + 16; run(L, NULL, NULL, 4 * Q, Q, &fr); /* (fr: not the expected frame after the calls above: a re-anchor) */
    CHECK(thunder_live_poll(L, buf, -1) == THUNDER_LIVE_E_ARG && thunder_live_poll(L, NULL, 100) == THUNDER_LIVE_E_ARG, "poll: cap < 0, buf NULL");
    CHECK(thunder_live_poll(L, buf, 8) == THUNDER_LIVE_E_ARG, "poll: cap too small for the next record: E_ARG, the record stays");
    drain_events(L, &e); CHECK(e.n > 0, "the records are still there after the refusal (%d)", e.n); }
  /* a host with every part: the answers the part's functions give */
  Lall = make(THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER | THUNDER_LIVE_F_METERS | THUNDER_LIVE_F_MIDI | THUNDER_LIVE_F_DUMP_STEPS, 1024);
  if (Lall) {
    CHECK(thunder_live_fx_plan(Lall, -1, (const uint8_t*)"[]", 2, 120) == THUNDER_LIVE_E_ARG && thunder_live_fx_plan(Lall, 33, (const uint8_t*)"[]", 2, 120) == THUNDER_LIVE_E_ARG, "fx_plan: track");
    CHECK(thunder_live_fx_plan(Lall, 0, NULL, 2, 120) == THUNDER_LIVE_E_ARG && thunder_live_fx_plan(Lall, 0, (const uint8_t*)"[]", 0, 120) == THUNDER_LIVE_E_ARG, "fx_plan: json NULL / empty");
    CHECK(thunder_live_fx_plan(Lall, 0, (const uint8_t*)"[]", 2, 0.0) == THUNDER_LIVE_E_ARG && thunder_live_fx_plan(Lall, 0, (const uint8_t*)"[]", 2, nan_()) == THUNDER_LIVE_E_ARG, "fx_plan: bpm");
    CHECK(thunder_live_fx_plan(Lall, 0, (const uint8_t*)"{}", 2, 120) == THUNDER_LIVE_E_ARG && thunder_live_fx_plan(Lall, 0, (const uint8_t*)"[", 1, 120) == THUNDER_LIVE_E_ARG, "fx_plan: not an array, not JSON");
    CHECK(thunder_live_fx_plan(Lall, 0, (const uint8_t*)"[]", (size_t)THUNDER_LIVE_MAX_VARS_BYTES + 1, 120) == THUNDER_LIVE_E_LIMIT, "fx_plan: above 4 MiB");
    CHECK(thunder_live_fx_commit(Lall, 0, -1) == THUNDER_LIVE_E_STATE, "fx_commit: nothing staged");
    CHECK(thunder_live_fx_commit(Lall, -1, -1) == THUNDER_LIVE_E_ARG && thunder_live_fx_commit(Lall, 33, -1) == THUNDER_LIVE_E_ARG, "fx_commit: track");
    CHECK(thunder_live_fx_plan(Lall, 0, (const uint8_t*)"[{\"type\":\"delay\",\"on\":true}]", 28, 120) == 0, "fx_plan");
    CHECK(thunder_live_fx_plan(Lall, 0, (const uint8_t*)"[]", 2, 120) == THUNDER_LIVE_E_STATE, "a second fx_plan for the track before the commit: E_STATE");
    { thunder_live_play_opts po = play_of(THUNDER_LIVE_NOW); size_t n = 0; char* song = read_file("sounds/ELECTRO.thunder", &n); if (song) { thunder_live_song_publish(Lall, (const uint8_t*)song, n, NULL, 0, 1, err, sizeof err); CHECK(thunder_live_play(Lall, &po) == THUNDER_LIVE_E_STATE, "play with an effects plan staged: E_STATE"); free(song); } }
    CHECK(thunder_live_fx_commit(Lall, 0, -1) == 0, "fx_commit");
    CHECK(thunder_live_fx_param(Lall, -1, 0, p12, -1) == THUNDER_LIVE_E_ARG && thunder_live_fx_param(Lall, 33, 0, p12, -1) == THUNDER_LIVE_E_ARG && thunder_live_fx_param(Lall, 0, -1, p12, -1) == THUNDER_LIVE_E_ARG && thunder_live_fx_param(Lall, 0, 8, p12, -1) == THUNDER_LIVE_E_ARG, "fx_param: track, slot");
    CHECK(thunder_live_fx_param(Lall, 0, 0, NULL, -1) == THUNDER_LIVE_E_ARG, "fx_param: p12 NULL");
    p12[7] = nan_(); CHECK(thunder_live_fx_param(Lall, 0, 0, p12, -1) == THUNDER_LIVE_E_ARG, "fx_param: a NaN"); p12[7] = 0.5;
    CHECK(thunder_live_fx_param(Lall, 0, 0, p12, ((int64_t)1 << 62) + 1) == THUNDER_LIVE_E_ARG, "fx_param: at above 2^62");
    CHECK(thunder_live_fx_param(Lall, 0, 0, p12, -1) == 0, "fx_param");
    CHECK(thunder_live_master_config(Lall, (const uint8_t*)"{}", 2, 2) == THUNDER_LIVE_E_ARG && thunder_live_master_config(Lall, NULL, 5, 0) == THUNDER_LIVE_E_ARG, "master_config: safety, NULL with a size");
    CHECK(thunder_live_master_config(Lall, (const uint8_t*)"{", 1, 0) == THUNDER_LIVE_E_ARG, "master_config: not JSON");
    CHECK(thunder_live_master_config(Lall, (const uint8_t*)"{}", (size_t)THUNDER_LIVE_MAX_VARS_BYTES + 1, 0) == THUNDER_LIVE_E_LIMIT, "master_config: above 4 MiB");
    CHECK(thunder_live_master_config(Lall, (const uint8_t*)MJ, sizeof MJ - 1, 1) == 0 && thunder_live_master_config(Lall, NULL, 0, 0) == 0 && thunder_live_master_config(Lall, (const uint8_t*)"{}", 0, 1) == 0, "master_config");
    CHECK(thunder_live_meters_want(Lall, -1) == THUNDER_LIVE_E_ARG && thunder_live_meters_want(Lall, 31) == THUNDER_LIVE_E_ARG && thunder_live_meters_want(Lall, nan_()) == THUNDER_LIVE_E_ARG && thunder_live_meters_want(Lall, 30) == 0 && thunder_live_meters_want(Lall, 0) == 0, "meters_want");
    CHECK(thunder_live_midi(Lall) != NULL, "midi handle with F_MIDI");
    CHECK(thunder_live_midi_lead(Lall, 0.004) == THUNDER_LIVE_E_ARG && thunder_live_midi_lead(Lall, 1.21) == THUNDER_LIVE_E_ARG && thunder_live_midi_lead(Lall, nan_()) == THUNDER_LIVE_E_ARG, "midi_lead: 0.005 .. 1.2");
    CHECK(thunder_live_midi_lead(Lall, 0.005) == 0 && thunder_live_midi_lead(Lall, 1.2) == 0 && thunder_live_midi_lead(Lall, 0.06) == 0, "midi_lead: the limits (1.2 s since P4)");
    { thunder_midi_msg m[2]; CHECK(thunder_live_midi_drain(Lall, NULL, 2) == THUNDER_LIVE_E_ARG && thunder_live_midi_drain(Lall, m, -1) == THUNDER_LIVE_E_ARG, "midi_drain: out NULL, cap < 0"); CHECK(thunder_live_midi_drain(Lall, m, 0) == 0 && thunder_live_midi_drain(Lall, m, 2) == 0, "midi_drain: nothing yet"); }
    { char js[64]; CHECK(thunder_live_dump_step(Lall, NULL, 10) == THUNDER_LIVE_E_ARG && thunder_live_dump_step(Lall, js, -1) == THUNDER_LIVE_E_ARG, "dump_step: json NULL, cap < 0"); CHECK(thunder_live_dump_step(Lall, js, (int)sizeof js) == 0, "dump_step: no step waiting: 0"); }
    { thunder_live_play_opts po = play_of(THUNDER_LIVE_NOW); po.test_once = 1; CHECK(thunder_live_play(L, &po) == THUNDER_LIVE_E_ARG, "play: test_once without F_DUMP_STEPS"); }
    thunder_live_destroy(Lall);
  }
  thunder_live_destroy(L);
}

/* play options: the ABI rule and every field */
static void test_play_args(void) {
  thunder_live* L = make(0, 1024); size_t n = 0; char* song = read_file("sounds/ELECTRO.thunder", &n); thunder_live_play_opts p; unsigned char big[sizeof(thunder_live_play_opts) + 16]; char err[128];
  if (!L || !song) { CHECK(0, "setup"); free(song); thunder_live_destroy(L); return; }
  CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, NULL, 0, 1, err, sizeof err) == 0, "song");
  CHECK(thunder_live_play(L, NULL) == THUNDER_LIVE_E_ARG, "play(NULL options)");
  p = play_of(0); p.size = 0; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "size 0");
  p = play_of(0); p.size = 3; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "size 3");
  p = play_of(0); p.reserved0 = 1; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "reserved0");
  p = play_of(0); p.mode = 3; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "mode 3"); p.mode = -1; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "mode -1");
  p = play_of(0); p.row = -1; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "row -1"); p.row = 256; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "row 256");
  p = play_of(0); p.track = 32; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "track 32"); p.track = -1; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "track -1");
  p = play_of(0); p.sect = -2; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "sect -2"); p = play_of(0); p.phrase = -2; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "phrase -2");
  p = play_of(0); p.has_loop = 1; p.loop_y0 = 3; p.loop_y1 = 2; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "loop y0 > y1"); p.loop_y0 = -1; p.loop_y1 = 2; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "loop y0 < 0");
  p = play_of(0); p.spp = -2; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "spp -2"); p.spp = 16384; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "spp 16384");
  p = play_of(0); p.pdc_mode = 3; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "pdc_mode 3"); p.pdc_mode = -1; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "pdc_mode -1");
  p = play_of(((int64_t)1 << 62) + 1); CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "t0 above 2^62");
  p = play_of(0); p.test_sim = 1; CHECK(thunder_live_play(L, &p) == THUNDER_LIVE_E_ARG, "test_sim on a product host");
  /* sizes: a short struct leaves the rest default; a newer caller must have zeroed what this build does not know */
  p = play_of(THUNDER_LIVE_NOW); p.size = 16; p.mode = 7; /* past size 16: not read */ CHECK(thunder_live_play(L, &p) == 0, "size 16: fields past it are defaults (a mode past it is not read)");
  memset(big, 0, sizeof big); p = play_of(THUNDER_LIVE_NOW); memcpy(big, &p, sizeof p); { uint32_t sz = (uint32_t)sizeof big; memcpy(big, &sz, 4); }
  CHECK(thunder_live_play(L, (const thunder_live_play_opts*)big) == 0, "a newer caller, extra fields zero");
  big[sizeof(thunder_live_play_opts) + 3] = 9; CHECK(thunder_live_play(L, (const thunder_live_play_opts*)big) == THUNDER_LIVE_E_ARG, "a newer caller with an unknown field set");
  /* a struct too short to hold t0 gets NOW (the next quantum + 0.08 s), not frame 0: no catch-up skip */
  thunder_live_destroy(L); L = make(0, 1024);
  if (L) { int64_t fr = 0; thunder_live_stats s0, s1; CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, NULL, 0, 1, err, sizeof err) == 0 && load_pads(L, 8) == 0, "song again");
    run(L, NULL, NULL, 96000, 1024, &fr);
    p = play_of(0); p.size = 8; CHECK(thunder_live_play(L, &p) == 0, "play with size 8"); run(L, NULL, NULL, 48000, 1024, &fr); s0 = stats_of(L);
    CHECK(s0.state == THUNDER_LIVE_STATE_PLAYING && s0.runner_skips == 0, "size 8: t0 is NOW, the song starts after 2 s of idle without a catch-up skip (skips %llu)", (unsigned long long)s0.runner_skips);
    thunder_live_stop(L, -1); run(L, NULL, NULL, 4 * Q, Q, &fr);
    p = play_of(0); CHECK(thunder_live_play(L, &p) == 0, "play with t0 = 0 (a past frame)"); run(L, NULL, NULL, 48000, 1024, &fr); s1 = stats_of(L);
    CHECK(s1.runner_skips > s0.runner_skips, "t0 = 0 given: the runner catches up (skips %llu): the two defaults differ", (unsigned long long)s1.runner_skips); }
  /* E_BUSY of play: 8 plays outstanding, then busy (nothing ran: no quantum applied them) */
  { int ok = 0, busy = 0, k; for (k = 0; k < 12; k++) { p = play_of(THUNDER_LIVE_NOW); { int rc = thunder_live_play(L, &p); if (rc == 0) ok++; else if (rc == THUNDER_LIVE_E_BUSY) busy++; } }
    CHECK(ok <= 8 && busy >= 4, "plays queued with no quantum between: %d accepted, %d E_BUSY (the play ring holds 8)", ok, busy); }
  thunder_live_destroy(L); free(song);
}


/* the engine offset (live.h): host frame - engine frame, one atomic */
static void test_offset(void) {
  thunder_live* L = make(0, 1024); float a[1024], b[1024]; int64_t off = -7, fr = 777; int i;
  if (!L) { CHECK(0, "make"); return; }
  CHECK(thunder_live_engine_offset(L, NULL) == THUNDER_LIVE_E_ARG, "engine_offset(out NULL)");
  CHECK(thunder_live_engine_offset(L, &off) == 0 && off == 0, "0 before the first call");
  thunder_live_process(L, a, b, 0, 5, 0); thunder_live_engine_offset(L, &off); CHECK(off == 0, "frames 0 anchors nothing");
  for (i = 0; i < 5; i++) { thunder_live_process(L, a, b, 1024, fr, 0); fr += 1024; }
  thunder_live_engine_offset(L, &off); CHECK(off == 777, "gapless: the first frame0 (%lld)", (long long)off);
  thunder_live_process(L, a, b, 1, fr, 0); fr += 1;   /* mid-quantum from here on */
  fr += 5000; thunder_live_process(L, a, b, 1, fr, 0); fr += 1;   /* a re-anchor in a call that completes no quantum */
  thunder_live_engine_offset(L, &off); CHECK(off == 777 + 5000, "a forward gap, in a 1-frame call: the new offset at once (%lld)", (long long)off);
  fr -= 3000; thunder_live_process(L, a, b, 1, fr, 0); thunder_live_engine_offset(L, &off); CHECK(off == 777 + 5000 - 3000, "a backward jump moves it back (%lld)", (long long)off);
  thunder_live_destroy(L);
}

/* E_BUSY: the command ring (256) */
static void test_busy(void) {
  thunder_live* L = make(0, 1024); int ok = 0, rc = 0, i; int64_t fr = 0; thunder_live_stats s;
  if (!L) { CHECK(0, "make"); return; }
  for (i = 0; i < 300; i++) { rc = thunder_live_bpm(L, 100 + i % 10, -1); if (rc == 0) ok++; else break; }
  CHECK(ok == THUNDER_LIVE_CMD_RING && rc == THUNDER_LIVE_E_BUSY, "the ring takes %d commands, then E_BUSY (%d)", ok, rc);
  CHECK(thunder_live_stop(L, -1) == THUNDER_LIVE_E_BUSY && thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, 1, 0, -1) == THUNDER_LIVE_E_BUSY, "every command call is E_BUSY, none changed");
  s = stats_of(L); CHECK(s.cmd_busy >= 3, "stats.cmd_busy %u", s.cmd_busy);
  CHECK(run(L, NULL, NULL, Q, Q, &fr) == 0, "one quantum empties the ring");
  CHECK(thunder_live_bpm(L, 90, -1) == 0, "the caller's retry goes through");
  s = stats_of(L); CHECK(s.cmds_applied >= 256 && s.cmds_no_seq >= 256, "applied %llu, dropped for lack of a sequencer %u", (unsigned long long)s.cmds_applied, s.cmds_no_seq);
  { double p12[12] = {0}; thunder_live* F = make(THUNDER_LIVE_F_EFFECTS, 1024); int okp = 0; rc = 0;
    if (F) { for (i = 0; i < 100; i++) { rc = thunder_live_fx_param(F, 0, 0, p12, -1); if (rc == 0) okp++; else break; } CHECK(okp == THUNDER_LIVE_FXPARAM_RING && rc == THUNDER_LIVE_E_BUSY, "the fx_param ring takes %d, then E_BUSY (%d)", okp, rc); thunder_live_destroy(F); } }
  thunder_live_destroy(L);
}

/* THE RUN: ELECTRO and 64 pads through the C API; the records; the transport; stop; the shutdown the header asks for */
static void test_run(void) {
  const int secs = SANITIZED ? 4 : 12; const int64_t N = (int64_t)(SR * secs);
  thunder_live* L = make(THUNDER_LIVE_F_EFFECTS | THUNDER_LIVE_F_MASTER | THUNDER_LIVE_F_METERS, 1024); size_t n = 0; char* song = read_file("sounds/ELECTRO.thunder", &n); float *l, *r; int64_t fr = 0; evs e; thunder_live_stats s; char err[128]; thunder_live_play_opts po; int i;
  if (!L || !song) { CHECK(0, "setup"); free(song); thunder_live_destroy(L); return; }
  l = (float*)malloc(sizeof(float) * (size_t)N); r = (float*)malloc(sizeof(float) * (size_t)N); evs_init(&e);
  thunder_live_host_stats(L, 480, 3);
  CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, NULL, 0, 42, err, sizeof err) == 0, "song_publish (IDLE: kept for the play)");
  CHECK(load_pads(L, PN) == 0, "64 pads, kits, published");
  run(L, NULL, NULL, 4 * Q, Q, &fr); drain_events(L, &e);
  CHECK(e.by_type[THUNDER_LIVE_EV_SONG] == 1 && e.song_rev == 42, "a SONG record for rev 42 (%d, %lld)", e.by_type[THUNDER_LIVE_EV_SONG], (long long)e.song_rev);
  CHECK(e.by_type[THUNDER_LIVE_EV_PADS] >= 1 && e.pads_id >= 1, "a PADS record (id %lld)", (long long)e.pads_id);
  CHECK(e.by_type[THUNDER_LIVE_EV_CLOCK] >= 1 && e.clock_sr == 48000 && e.clock_out_lat == 480, "a CLOCK record with sr 48000 and the host's out_lat 480 (%d, %d)", e.clock_sr, e.clock_out_lat);
  s = stats_of(L); CHECK(s.state == THUNDER_LIVE_STATE_IDLE && s.quanta == 4 && s.frames == 4 * Q, "idle: %llu quanta, frames %lld", (unsigned long long)s.quanta, (long long)s.frames);
  po = play_of(THUNDER_LIVE_NOW); po.random_seed = 0x2545F491u;
  CHECK(thunder_live_play(L, &po) == 0, "play");
  CHECK(thunder_live_meters_want(L, 20) == 0, "meters on");
  CHECK(run(L, l, r, N / 2, 480, &fr) == 0, "process, first half in calls of 480");
  CHECK(thunder_live_bpm(L, 140, -1) == 0 && thunder_live_loop(L, 1, 0, 3, -1) == 0 && thunder_live_lead(L, 0.4, -1) == 0 && thunder_live_perf(L, THUNDER_LIVE_PERF_XF, 0, 0.3, -1) == 0, "bpm, loop, lead, perf while playing");
  CHECK(thunder_live_mix_set(L, 0, THUNDER_LIVE_MIX_VOL, 120, -1, fr + 1000) == 0 && thunder_live_mix_set(L, 32, THUNDER_LIVE_MIX_VOL, 220, 480, -1) == 0, "mix_set while playing");
  CHECK(thunder_live_stop_track(L, 2, -1) == 0, "stop_track");
  CHECK(thunder_live_fx_plan(L, 1, (const uint8_t*)"[{\"type\":\"delay\",\"on\":true}]", 28, 140) == 0 && thunder_live_fx_commit(L, 1, fr + 2000) == 0, "an effects plan staged and committed while playing");
  CHECK(thunder_live_pdc_set(L, 1) == 0 && thunder_live_master_config(L, (const uint8_t*)MJ, sizeof MJ - 1, 1) == 0, "pdc_set and master_config while playing");
  { float* x = (float*)malloc(sizeof(float) * NS); make_pad(x, 9); CHECK(thunder_live_pad_set(L, 9, x, NULL, NS, SR) == 0 && thunder_live_pads_publish(L) == 0, "a pad replaced and published while playing"); free(x); }
  CHECK(run(L, l + N / 2, r + N / 2, N - N / 2, 333, &fr) == 0, "process, second half in calls of 333");
  CHECK(finite_all(l, (size_t)N) && finite_all(r, (size_t)N), "every sample finite");
  CHECK(peak_of(l, (size_t)N) > 0.01 && peak_of(r, (size_t)N) > 0.01, "the song sounds (%g)", peak_of(l, (size_t)N));
  drain_events(L, &e);
  CHECK(e.seq_ascending, "records in ascending seq"); CHECK(e.by_type[THUNDER_LIVE_EV_POS] > 10 && e.by_type[THUNDER_LIVE_EV_ROWS] > 10, "POS %d and ROWS %d records while playing", e.by_type[THUNDER_LIVE_EV_POS], e.by_type[THUNDER_LIVE_EV_ROWS]);
  CHECK(e.by_type[THUNDER_LIVE_EV_METERS] > 5, "METERS records (%d)", e.by_type[THUNDER_LIVE_EV_METERS]);
  { int playing = 0; for (i = 0; i < e.nstates; i++) if (e.states[i] == THUNDER_LIVE_ST_PLAYING) playing++; CHECK(playing == 1, "one STATE PLAYING record (%d)", playing); }
  s = stats_of(L); CHECK(s.state == THUNDER_LIVE_STATE_PLAYING && s.steps > 20 && s.voices > 0 && s.seq_dropped == 0 && s.reanchors == 0, "playing: %llu steps, %d voices, dropped %llu, reanchors %u", (unsigned long long)s.steps, s.voices, (unsigned long long)s.seq_dropped, s.reanchors);
  CHECK(s.thread_prepares <= 1 && s.fx_failed_blocks == 0 && s.live_failed == 0, "thread_prepares %u, fx_failed_blocks %u", s.thread_prepares, s.fx_failed_blocks);
  CHECK(s.frames == fr, "stats.frames is the next frame (%lld vs %lld)", (long long)s.frames, (long long)fr);
  /* a song publish while playing (the successor snapshot) and a collect */
  CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, NULL, 0, 43, err, sizeof err) == 0, "song_publish while playing");
  run(L, NULL, NULL, 8 * Q, Q, &fr); { evs e2; evs_init(&e2); drain_events(L, &e2); CHECK(e2.by_type[THUNDER_LIVE_EV_SONG] == 1 && e2.song_rev == 43, "the SONG record for rev 43"); }
  CHECK(thunder_live_collect(L) >= 0, "collect");
  /* a re-anchor: a frame0 that does not continue */
  fr += 5000; run(L, NULL, NULL, 2 * Q, Q, &fr); { evs e3; evs_init(&e3); drain_events(L, &e3); s = stats_of(L); CHECK(s.reanchors == 1 && e3.reanchors == 1, "a re-anchor is counted (%u) and reported (%d), not an error", s.reanchors, e3.reanchors); }
  /* stop, one more process, the records, then destroy */
  CHECK(thunder_live_stop(L, -1) == 0, "stop");
  run(L, NULL, NULL, 2 * Q, Q, &fr); { evs e4; evs_init(&e4); drain_events(L, &e4); { int stopped = 0; for (i = 0; i < e4.nstates; i++) if (e4.states[i] == THUNDER_LIVE_ST_STOPPED && e4.state_args[i] == 0) stopped++; CHECK(stopped == 1, "STOPPED arg 0 (%d)", stopped); } }
  s = stats_of(L); CHECK(s.state == THUNDER_LIVE_STATE_IDLE, "IDLE after the stop");
  run(L, NULL, NULL, 50 * Q, Q, &fr); /* the effects tails and the voices ring out */
  CHECK(thunder_live_collect(L) >= 0, "collect after the stop");
  thunder_live_destroy(L); free(song); free(l); free(r);
}

/* HOLD (EXACT STOP), the END of a song, and the dump ring */
static void test_hold_end_dump(void) {
  thunder_live* L = make(THUNDER_LIVE_F_DUMP_STEPS, 1024); size_t n = 0; char* song = read_file("sounds/ELECTRO.thunder", &n); int64_t fr = 0; evs e; thunder_live_stats s; char err[128]; thunder_live_play_opts po; int i; int got = 0; char js[16384];
  if (!L || !song) { CHECK(0, "setup"); free(song); thunder_live_destroy(L); return; }
  thunder_live_song_publish(L, (const uint8_t*)song, n, NULL, 0, 1, err, sizeof err); load_pads(L, 16);
  po = play_of(THUNDER_LIVE_NOW); po.random_seed = 0x2545F491u; CHECK(thunder_live_play(L, &po) == 0, "play");
  CHECK(thunder_live_hold(L, -1) == 0, "hold");
  evs_init(&e);
  for (i = 0; i < 40 && stats_of(L).state != THUNDER_LIVE_STATE_HELD; i++) { run(L, NULL, NULL, 48000, 1024, &fr); { char* dummy = js; int len; while ((len = thunder_live_dump_step(L, dummy, (int)sizeof js)) > 0) { got++; CHECK(js[0] == '{' && js[len] == 0 && strstr(js, "\"evs\""), "a dump line"); } CHECK(len == 0, "dump_step returns 0 when none waits (%d)", len); } }
  drain_events(L, &e); s = stats_of(L);
  CHECK(s.state == THUNDER_LIVE_STATE_HELD && e.by_type[THUNDER_LIVE_EV_HELD] == 1, "EXACT STOP: HELD (state %d, %d HELD records)", s.state, e.by_type[THUNDER_LIVE_EV_HELD]);
  CHECK(got > 0, "dump steps delivered (%d)", got);
  { int cap_small = thunder_live_dump_step(L, js, 4); CHECK(cap_small >= 0, "a dump step is not stuck (%d)", cap_small); }
  CHECK(thunder_live_stop(L, -1) == 0, "stop after HELD"); run(L, NULL, NULL, 4 * Q, Q, &fr); s = stats_of(L); CHECK(s.state == THUNDER_LIVE_STATE_IDLE, "IDLE");
  /* a song that ends at once (no track plays: has_only with an empty mask): ENDED, then STOPPED arg 1 0.3 s later */
  po = play_of(THUNDER_LIVE_NOW); po.random_seed = 0x2545F491u; po.has_only = 1; po.only = 0; po.test_once = 1;
  evs_init(&e); CHECK(thunder_live_play(L, &po) == 0, "play (once, track 0 only)");
  for (i = 0; i < 90 && stats_of(L).state != THUNDER_LIVE_STATE_ENDED; i++) run(L, NULL, NULL, 48000, 1024, &fr);
  for (i = 0; i < 4; i++) run(L, NULL, NULL, 48000, 1024, &fr);
  drain_events(L, &e); s = stats_of(L);
  { int ended = 0, stopped1 = 0; for (i = 0; i < e.nstates; i++) { if (e.states[i] == THUNDER_LIVE_ST_ENDED) ended++; if (e.states[i] == THUNDER_LIVE_ST_STOPPED && e.state_args[i] == 1) stopped1++; }
    CHECK(ended == 1 && stopped1 == 1 && s.state == THUNDER_LIVE_STATE_IDLE, "the song ends: ENDED %d, STOPPED arg 1 %d, then IDLE (%d)", ended, stopped1, s.state); }
  thunder_live_destroy(L); free(song);
}

/* MIDI: the host's own thunder_midi, the records of the scheduler through thunder_live_midi_drain */
static void test_midi(void) {
  thunder_live* L = make(THUNDER_LIVE_F_MIDI, 1024); size_t n = 0; char* song = read_file("sounds/ELECTRO.thunder", &n); int64_t fr = 0; thunder_live_play_opts po; char err[128]; thunder_midi_msg m[256]; int64_t got, total = 0, notes = 0, last = -1; int ordered = 1, i;
  if (!L || !song) { CHECK(0, "setup"); free(song); thunder_live_destroy(L); return; }
  { char* at = song; while ((at = strstr(at, "\"mo\":0")) != NULL) { at[5] = '2'; at += 6; } }   /* every instrument: OUTPUT 2 (pad + MIDI) */
  CHECK(thunder_live_midi(L) != NULL, "the host's thunder_midi");
  CHECK(thunder_midi_set_port(thunder_live_midi(L), 0, 1, 1, 0, 0) == 0, "the shell's control thread sets a port on it");
  CHECK(thunder_live_song_publish(L, (const uint8_t*)song, n, NULL, 0, 1, err, sizeof err) == 0 && load_pads(L, 16) == 0, "song and pads");
  po = play_of(THUNDER_LIVE_NOW); po.random_seed = 0x2545F491u; CHECK(thunder_live_play(L, &po) == 0, "play");
  for (i = 0; i < (SANITIZED ? 100 : 400); i++) { run(L, NULL, NULL, 1024, 1024, &fr); while ((got = thunder_live_midi_drain(L, m, 256)) > 0) { int64_t k; total += got; for (k = 0; k < got; k++) { if (m[k].b[0] == 0x90 && m[k].len == 3) notes++; last = m[k].frame; } } CHECK(got == 0, "drain: %lld", (long long)got); }
  CHECK(total > 100 && notes > 10, "MIDI records %lld, note-ons %lld", (long long)total, (long long)notes); (void)ordered; (void)last;
  { thunder_live_stats s = stats_of(L); CHECK(s.midi_begin_failed == 0 && s.midi_may_hang == 0, "no midi_begin failure, no may_hang"); }
  /* the duty of the header: stop, one more process, drain, then destroy */
  CHECK(thunder_live_stop(L, -1) == 0, "stop"); run(L, NULL, NULL, 2 * Q, Q, &fr);
  { int64_t clr = 0, k; got = thunder_live_midi_drain(L, m, 256); for (k = 0; k < got; k++) if (m[k].flags & THUNDER_MIDI_F_CLEAR) clr++; CHECK(got > 0 && clr > 0, "the stop books its CLEAR record (%lld records, %lld CLEAR)", (long long)got, (long long)clr); }
  thunder_live_destroy(L); free(song);
}

/* Reduced where it must be: nothing in this file is heavy, the run is 4 s under a sanitizer. */
int main(void) {
  test_static(); test_create(); test_null_handle(); test_idle_and_args(); test_play_args(); test_offset(); test_busy(); test_run(); test_hold_end_dump(); test_midi();
  printf("live_c: %d checks, %d failed\n", g_checks, g_fail);
  return g_fail ? 1 : 0;
}
