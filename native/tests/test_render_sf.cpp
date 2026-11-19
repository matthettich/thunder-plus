// RENDER-SF: the SoundFont route of the offline song renderer (thunder_render_add_font / set_pad_sf / clear_pad_sf / remove_font, SongRenderer's SfPool).
// docs/port/RENDER-SF-NOTES.md. Fonts: native/tests/golden/sf_test.sf2 (15 KB, hostile arguments and sharing) and sounds/FluidR3Mono_GM.sf3 (real SF3: the organ and the piano, a few keys).
// Checks: (1) an sf note through the renderer equals an SfPool driven directly bit for bit (a tap on the mixer's per-track inputs against a second sequencer + SfPool);
// (2) the C API equals a direct SongRenderer drive and every host block size gives the same bits; (3) takeover both ways, note-off, stp; (4) two pads sharing one font;
// replace / remove; (5) silence cases and stats; (6) no allocation while reading; (7) hostile arguments (run under -DTHUNDER_SANITIZE=ON).
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>

#include "render/render.h"
#include "sf/sf.h"
#include "sf/sf_live.h"
#include "song/song.h"
#include "thunder/render.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
using namespace thunder;
static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

// ---- allocation counter: reads must allocate nothing --------------------------------------------------------------------------------------------------------------------------------------------------------
static bool g_count = false; static long g_allocs = 0;
void* operator new(std::size_t n) { if (g_count) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n) { if (g_count) g_allocs++; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (g_count) g_allocs++; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { if (g_count) g_allocs++; return std::malloc(n ? n : 1); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static std::vector<uint8_t> readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

// ---- songs ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
constexpr int kFxSTP = 56, kFxPBN = 16;
struct Row { int note = -1, inst = -1, fx = -1, fxv = -1; int c1 = -1, c2 = -1, c3 = -1; };   // c1..c3: the other note columns (a poly track plays all four as a chord)
constexpr int NOTE_OFF_V = 200;
// the tracker's note for MIDI key k: the page plays rate 1 at tracker note 48, and the sf player turns rate 1 into key 60 (+ the pad's trans)
static Row N(int midiKey, int inst, int fx = -1, int fxv = -1) { Row r; r.note = midiKey - 12; r.inst = inst; r.fx = fx; r.fxv = fxv; return r; }
// tracks[i] = the 8 rows of track i (row = 0.125 s at 120 bpm, lpb 4); insts[i] = the pad of instrument i
static std::string songJson(const std::vector<std::vector<Row>>& tracks, const std::vector<int>& insts, int ntr = 8) {
  std::string s = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":" + std::to_string(ntr) + ",\"song\":[[";
  for (size_t t = 0; t < tracks.size(); t++) { s += (t ? "," : "") + std::to_string(t); }
  s += "]],\"chains\":[";
  for (size_t t = 0; t < tracks.size(); t++) s += std::string(t ? "," : "") + "[[" + std::to_string(t) + ",0]]";
  s += "],\"plen\":[";
  for (size_t t = 0; t < tracks.size(); t++) s += std::string(t ? "," : "") + "8";
  s += "],\"insts\":[";
  for (size_t i = 0; i < insts.size(); i++) s += std::string(i ? "," : "") + "{\"pad\":" + std::to_string(insts[i]) + ",\"tsp\":0,\"slc\":0,\"sync\":0,\"mo\":0}";
  s += "],\"phrases\":[";
  for (size_t t = 0; t < tracks.size(); t++) {
    s += t ? ",[" : "[";
    for (int r = 0; r < 8; r++) {
      const Row w = r < (int)tracks[t].size() ? tracks[t][(size_t)r] : Row();
      char b[140]; std::snprintf(b, sizeof b, "%s[%d,%d,%d,%d,-1,%d,%d,%d,-1,-1,-1,-1]", r ? "," : "", w.note, w.c1, w.c2, w.c3, w.inst, w.fx, w.fx >= 0 ? w.fxv : -1); s += b;
    }
    s += "]";
  }
  return s + "]}";
}

// ---- the C API rig ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static std::vector<uint8_t> g_gm, g_small;
constexpr int64_t kFrames = 72000;
struct SfSet { int pad = 0, font = 0, bank = 0, prog = 0; bool params = false; thunder_render_sf_params P{}; std::vector<uint8_t> keys; };
struct Rig {
  std::string song; std::vector<const std::vector<uint8_t>*> fonts; std::vector<SfSet> sf; std::vector<std::pair<int, int>> routes; std::vector<std::pair<int, float>> dcPads;
};
static thunder_render* make(const Rig& g, int64_t frames = kFrames) {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = frames;
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)g.song.data(), g.song.size(), &o, err, sizeof err);
  if (!r) { std::printf("create failed: %s\n", err); g_fail++; return nullptr; }
  for (size_t i = 0; i < g.fonts.size(); i++) CHECK(thunder_render_add_font(r, g.fonts[i]->data(), g.fonts[i]->size()) == (int)i);
  for (const SfSet& s : g.sf) {
    const int e = thunder_render_set_pad_sf(r, s.pad, s.font, s.bank, s.prog, s.params ? &s.P : nullptr, s.keys.empty() ? nullptr : s.keys.data(), s.keys.size());
    CHECKF(e == 0, "set_pad_sf -> %d", e);
  }
  for (auto& pr : g.routes) CHECK(thunder_render_set_pad_route(r, pr.first, pr.second) == 0);
  for (auto& dc : g.dcPads) { std::vector<float> d(96000, dc.second); CHECK(thunder_render_set_pad(r, dc.first, d.data(), nullptr, 96000, 48000) == 0); }
  return r;
}
struct Out { std::vector<float> L, R; thunder_render_stats st; double peak = 0; };
static Out run(const Rig& g, int block = 4096, int64_t frames = kFrames) {
  Out o; thunder_render* r = make(g, frames); if (!r) return o;
  o.L.assign((size_t)frames, 0.f); o.R.assign((size_t)frames, 0.f);
  int64_t got = 0; while (got < frames) { const int64_t n = thunder_render_read(r, o.L.data() + got, o.R.data() + got, std::min<int64_t>(block, frames - got)); if (n <= 0) break; got += n; }
  CHECK(got == frames);
  std::memset(&o.st, 0, sizeof o.st); o.st.size = sizeof o.st; CHECK(thunder_render_stats_get(r, &o.st) == 0);
  for (int64_t i = 0; i < frames; i++) o.peak = std::max(o.peak, (double)std::max(std::fabs(o.L[(size_t)i]), std::fabs(o.R[(size_t)i])));
  thunder_render_free(r);
  return o;
}
static bool same(const Out& a, const Out& b) { return a.L.size() == b.L.size() && !a.L.empty() && !std::memcmp(a.L.data(), b.L.data(), a.L.size() * 4) && !std::memcmp(a.R.data(), b.R.data(), a.R.size() * 4); }
static double rms(const std::vector<float>& v, int64_t a, int64_t b) { double s = 0; for (int64_t i = a; i < b; i++) s += (double)v[(size_t)i] * v[(size_t)i]; return std::sqrt(s / (double)(b - a)); }
static double maxdiff(const Out& a, const Out& b, int64_t i0, int64_t i1) { double m = 0; for (int64_t i = i0; i < i1; i++) m = std::max(m, std::max(std::fabs((double)a.L[(size_t)i] - b.L[(size_t)i]), std::fabs((double)a.R[(size_t)i] - b.R[(size_t)i]))); return m; }

static Rig organ(const std::string& song, std::vector<uint8_t> keys = {60}) {
  Rig g; g.song = song; g.fonts = {&g_gm};
  SfSet s; s.pad = 0; s.font = 0; s.bank = 0; s.prog = 19; s.keys = keys; g.sf.push_back(s); g.routes.push_back({0, THUNDER_RENDER_ROUTE_SF});
  return g;
}

// ---- (1) the seam: the renderer's SfPool against an SfPool driven directly -------------------------------------------------------------------------------------------------------------------------------------
struct Tap { std::vector<float> L[8], R[8]; };
static void tapFn(void* u, const float* const* inL, const float* const* inR, int n, int64_t) {
  Tap* t = (Tap*)u;
  for (int k = 0; k < 8; k++) { if (inL[k]) t->L[k].insert(t->L[k].end(), inL[k], inL[k] + n); else t->L[k].insert(t->L[k].end(), (size_t)n, 0.f);
                                if (inR[k]) t->R[k].insert(t->R[k].end(), inR[k], inR[k] + n); else t->R[k].insert(t->R[k].end(), (size_t)n, 0.f); }
}
struct Seam { int nOn = 0, nOff = 0, nStp = 0, nMod = 0, played = 0; bool equal = true; double peak = 0; };
static Seam seam(const std::string& songText, const std::vector<int>& sfPads, int prog, const std::vector<uint8_t>& keys, int quanta) {
  Seam out;
  Song song; JsonError je; CHECK(song.load(songText.data(), songText.size(), &je));
  sf::Font font; CHECK(sf::parse(g_gm.data(), g_gm.size(), font) == sf::kParseOk);
  const sf::Preset* pr = sf::sf_preset(font, 0, prog); uint8_t flags[128] = {}; for (uint8_t k : keys) flags[k] = 1;
  sf::PresetPcm pcm; sf::DecodeOpts dop; CHECK(sf::prepare_preset(font, pr, dop, pcm, flags) == sf::kPrepOk);
  sf::PadParams P; P.bank = 0; P.prog = prog;
  SeqVars vars; RenderPads pads; for (int p : sfPads) { pads.route[p] = PadRoute::SoundFont; pads.sf[p].font = &font; pads.sf[p].pr = pr; pads.sf[p].pcm = &pcm; pads.sf[p].P = P; }
  SeqOpts so; so.row = song.renderFrom; so.once = true;
  SongRenderer R(song, vars, pads, 48000, 128, so); Tap tap; R.setInputTap(tapFn, &tap);
  std::vector<float> a(128), b(128);
  for (int q = 0; q < quanta; q++) R.block(a.data(), b.data(), 128);
  out.played = R.stats().notesSfPlayed;
  // the oracle
  Seq seq(song, vars, SeqMode::Song, so); sf::SfPool pool(48000);
  std::vector<SeqEvent> evs((size_t)kRenderEvents); std::unique_ptr<SeqStep> step(new SeqStep);
  std::vector<float> oL[8], oR[8]; VoiceKit kit;
  for (int q = 0; q < quanta; q++) {
    const double t0 = (double)((int64_t)q * 128) / 48000.0; pool.setFrame((uint64_t)q * 128);
    int n = 0;
    while (seq.alive() && seq.time() < t0 + 0.25 && n++ < 3) {
      const int k = seq.step(evs.data(), kRenderEvents, step.get());
      for (int i = 0; i < k; i++) {
        const SeqEvent& e = evs[(size_t)i];
        if (e.type == SEV_STP) { pool.stp(e); out.nStp++; }
        else if (e.type == SEV_OFF && e.track >= 0) { pool.off(e); out.nOff++; }
        else if (e.type == SEV_MOD && e.track >= 0) { pool.mod(e, seq.arp(e)); out.nMod++; }
        else if (e.type == SEV_NOTE && e.track >= 0) { const int p = (int)e.pad; if (std::find(sfPads.begin(), sfPads.end(), p) != sfPads.end()) { sf::SfPad sp = pads.sf[p]; out.nOn++; pool.note(e, seq.arp(e), sp, kit); } }
      }
    }
    float bl[8][128] = {}, br[8][128] = {}; float* lp[kVoiceTracks] = {}; float* rp[kVoiceTracks] = {};
    for (int k = 0; k < 8; k++) { lp[k] = bl[k]; rp[k] = br[k]; }
    pool.render(lp, rp, 128, (uint64_t)q * 128);
    for (int k = 0; k < 8; k++) { oL[k].insert(oL[k].end(), bl[k], bl[k] + 128); oR[k].insert(oR[k].end(), br[k], br[k] + 128); }
  }
  for (int k = 0; k < 8; k++) {
    if (tap.L[k].size() != oL[k].size() || std::memcmp(tap.L[k].data(), oL[k].data(), oL[k].size() * 4) || std::memcmp(tap.R[k].data(), oR[k].data(), oR[k].size() * 4)) out.equal = false;
    for (float v : oL[k]) out.peak = std::max(out.peak, (double)std::fabs(v));
  }
  return out;
}

// ---- (7) hostile arguments ---------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
static void hostile() {
  const std::string song = songJson({{N(60, 0)}}, {0});
  Rig base; base.song = song; thunder_render* r = make(base); if (!r) return;
  const uint8_t k60[1] = {60}; const double nan = std::nan(""), inf = INFINITY;
  // null handle
  CHECK(thunder_render_add_font(nullptr, g_small.data(), g_small.size()) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_remove_font(nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(nullptr, 0, 0, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_clear_pad_sf(nullptr, 0) == THUNDER_RENDER_E_ARG);
  // add_font arguments
  CHECK(thunder_render_add_font(r, nullptr, 100) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_add_font(r, g_small.data(), 0) == THUNDER_RENDER_E_ARG);
  { std::vector<uint8_t> junk(2000); uint32_t x = 12345; for (auto& b : junk) { x = x * 1664525u + 1013904223u; b = (uint8_t)(x >> 24); } CHECK(thunder_render_add_font(r, junk.data(), junk.size()) == THUNDER_RENDER_E_FONT); }
  { const uint8_t hdr[12] = {'R', 'I', 'F', 'F', 4, 0, 0, 0, 's', 'f', 'b', 'k'}; CHECK(thunder_render_add_font(r, hdr, 12) == THUNDER_RENDER_E_FONT); }
  CHECK(thunder_render_add_font(r, g_small.data(), 1) == THUNDER_RENDER_E_FONT);
  // truncated fonts, every prefix length sampled: never a crash, never a slot used up by a failure
  int accepted = 0;
  for (size_t n = 2; n < g_small.size(); n += 61) { const int id = thunder_render_add_font(r, g_small.data(), n); if (id >= 0) { accepted++; CHECK(thunder_render_remove_font(r, id) == 0); } else CHECKF(id == THUNDER_RENDER_E_FONT || id == THUNDER_RENDER_E_LIMIT, "truncated %zu -> %d", n, id); }
  { const int id = thunder_render_add_font(r, g_small.data(), g_small.size() - 1); if (id >= 0) { CHECK(thunder_render_remove_font(r, id) == 0); } }
  // mutated fonts: whatever is accepted is used (preparation + a short render) without a fault
  { uint32_t x = 777; auto rnd = [&]() { x = x * 1664525u + 1013904223u; return x >> 8; };
    for (int it = 0; it < 150; it++) {
      std::vector<uint8_t> f = g_small; const int flips = 1 + (int)(rnd() % 6); for (int j = 0; j < flips; j++) f[rnd() % f.size()] = (uint8_t)rnd();
      if (rnd() % 4 == 0) { const size_t at = rnd() % (f.size() - 4); const uint32_t v = rnd() % 3 == 0 ? 0xffffffffu : rnd(); std::memcpy(&f[at], &v, 4); }
      const int id = thunder_render_add_font(r, f.data(), f.size());
      if (id >= 0) { if (thunder_render_set_pad_sf(r, 0, id, 0, (int)(rnd() % 3), nullptr, nullptr, 0) == 0) { CHECK(thunder_render_clear_pad_sf(r, 0) == 0); } CHECK(thunder_render_remove_font(r, id) == 0); }
      else CHECK(id == THUNDER_RENDER_E_FONT || id == THUNDER_RENDER_E_LIMIT || id == THUNDER_RENDER_E_MEMORY);
    } }
  const int fid = thunder_render_add_font(r, g_small.data(), g_small.size()); CHECKF(fid == 0, "the first valid font after every failure is id 0 (%d)", fid);
  // set_pad_sf arguments
  CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, nullptr, nullptr, 0) == 0);
  CHECK(thunder_render_set_pad_sf(r, -1, fid, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 256, fid, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 0, -1, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 0, 16, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 0, 5, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);   // no such font
  CHECK(thunder_render_set_pad_sf(r, 0, fid, -1, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 0, fid, 16384, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, -1, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 128, nullptr, nullptr, 0) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, nullptr, k60, 0) == THUNDER_RENDER_E_ARG);               // a list with no key
  CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, nullptr, nullptr, 3) == THUNDER_RENDER_E_ARG);           // keys null, count not
  { std::vector<uint8_t> big(THUNDER_RENDER_MAX_SF_KEYS + 1, 60); CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, nullptr, big.data(), big.size()) == THUNDER_RENDER_E_ARG);
    big.resize(THUNDER_RENDER_MAX_SF_KEYS); CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, nullptr, big.data(), big.size()) == 0); }
  { const uint8_t bad[2] = {60, 128}; CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, nullptr, bad, 2) == THUNDER_RENDER_E_ARG); }
  { thunder_render_sf_params p; std::memset(&p, 0, sizeof p); p.size = sizeof p; p.velo = 0.7; p.atk = 1; p.rel = 1;
    auto bad = [&](thunder_render_sf_params q) { return thunder_render_set_pad_sf(r, 0, fid, 0, 0, &q, nullptr, 0) == THUNDER_RENDER_E_ARG; };
    CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, &p, nullptr, 0) == 0);
    thunder_render_sf_params q = p; q.trans = nan; CHECK(bad(q)); q = p; q.trans = inf; CHECK(bad(q)); q = p; q.trans = -129; CHECK(bad(q)); q = p; q.trans = 129; CHECK(bad(q));
    q = p; q.velo = -0.1; CHECK(bad(q)); q = p; q.velo = 1.1; CHECK(bad(q)); q = p; q.velo = nan; CHECK(bad(q));
    q = p; q.atk = -1; CHECK(bad(q)); q = p; q.atk = 101; CHECK(bad(q)); q = p; q.atk = nan; CHECK(bad(q));
    q = p; q.rel = -1; CHECK(bad(q)); q = p; q.rel = inf; CHECK(bad(q));
    q = p; q.level = 201; CHECK(bad(q)); q = p; q.level = -inf; CHECK(bad(q));
    q = p; q.len = -1; CHECK(bad(q)); q = p; q.len = 3601; CHECK(bad(q)); q = p; q.len = nan; CHECK(bad(q));
    q = p; q.reserved0 = 1; CHECK(bad(q)); q = p; q.size = 3; CHECK(bad(q));
    q = p; q.size = (uint32_t)offsetof(thunder_render_sf_params, trans); CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, &q, nullptr, 0) == 0);   // an older header: the fields it lacks are defaults
    q = p; q.trans = 128; q.level = -200; q.len = 3600; q.atk = 100; q.rel = 0; q.velo = 1; CHECK(thunder_render_set_pad_sf(r, 0, fid, 0, 0, &q, nullptr, 0) == 0); }   // the limits themselves
  // clear / remove
  CHECK(thunder_render_clear_pad_sf(r, -1) == THUNDER_RENDER_E_ARG); CHECK(thunder_render_clear_pad_sf(r, 256) == THUNDER_RENDER_E_ARG); CHECK(thunder_render_clear_pad_sf(r, 7) == 0);
  CHECK(thunder_render_remove_font(r, fid) == THUNDER_RENDER_E_ARG);    // pad 0 still uses it
  CHECK(thunder_render_remove_font(r, -1) == THUNDER_RENDER_E_ARG); CHECK(thunder_render_remove_font(r, 16) == THUNDER_RENDER_E_ARG); CHECK(thunder_render_remove_font(r, 9) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_clear_pad_sf(r, 0) == 0); CHECK(thunder_render_remove_font(r, fid) == 0); CHECK(thunder_render_remove_font(r, fid) == THUNDER_RENDER_E_ARG);
  CHECK(thunder_render_add_font(r, g_small.data(), g_small.size()) == fid);   // the slot is free again
  // all sixteen ids, then none
  { int last = 0; for (int i = 1; i < THUNDER_RENDER_MAX_FONTS; i++) last = thunder_render_add_font(r, g_small.data(), g_small.size()); CHECK(last == THUNDER_RENDER_MAX_FONTS - 1);
    CHECK(thunder_render_add_font(r, g_small.data(), g_small.size()) == THUNDER_RENDER_E_LIMIT);
    for (int i = 0; i < THUNDER_RENDER_MAX_FONTS; i++) CHECK(thunder_render_remove_font(r, i) == 0); }
  thunder_render_free(r);

  // the shared 512 MiB budget: a big pad leaves no room for a big font or a big preset, nothing changes on refusal, and the room comes back
  { Rig b2; b2.song = song; thunder_render* q = make(b2); if (q) {
      const int64_t bigFrames = 90000000; std::vector<float> big((size_t)bigFrames, 0.1f);
      CHECK(thunder_render_set_pad(q, 5, big.data(), nullptr, bigFrames, 48000) == 0);   // 360 MB
      const int gm = thunder_render_add_font(q, g_gm.data(), g_gm.size()); CHECKF(gm == 0, "the GM font fits beside a 360 MB pad (%d)", gm);
      CHECK(thunder_render_set_pad_sf(q, 0, gm, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_LIMIT);   // the whole-keyboard piano: 191 MB, the room is about 150 MB
      CHECK(thunder_render_set_pad_sf(q, 0, gm, 0, 0, nullptr, k60, 1) == 0);                          // one key fits
      CHECK(thunder_render_set_pad(q, 5, nullptr, nullptr, 0, 48000) == 0);                                // the big pad goes: the room is back
      CHECK(thunder_render_set_pad_sf(q, 1, gm, 0, 0, nullptr, nullptr, 0) == 0);                          // the whole piano now fits
      CHECK(thunder_render_set_pad(q, 5, big.data(), nullptr, 60000000, 48000) == 0);                      // 240 MB (the piano 191 + the font 25 + 240 = 456 MB: fits)
      CHECK(thunder_render_set_pad_sf(q, 1, gm, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_LIMIT);   // replacing it needs both copies at once: 456 + 191 MB is over the 512 MiB budget
      CHECK(thunder_render_clear_pad_sf(q, 1) == 0);
      CHECK(thunder_render_set_pad_sf(q, 1, gm, 0, 0, nullptr, nullptr, 0) == 0);                          // after clearing it fits again
      CHECK(thunder_render_add_font(q, g_gm.data(), g_gm.size()) == 1);
      thunder_render_free(q); }
    { Rig b3; b3.song = song; thunder_render* q = make(b3); if (q) {   // a pad that leaves too little for the font itself
        const int64_t bigFrames = 130000000; std::vector<float> big((size_t)bigFrames, 0.f);
        CHECK(thunder_render_set_pad(q, 5, big.data(), nullptr, bigFrames, 48000) == 0);   // 520 MB
        CHECK(thunder_render_add_font(q, g_gm.data(), g_gm.size()) == THUNDER_RENDER_E_LIMIT);
        CHECK(thunder_render_add_font(q, g_small.data(), g_small.size()) == 0);
        thunder_render_free(q); } } }

  // E_STATE after the first read (and a drain): every function, valid arguments
  { Rig b4; b4.song = song; b4.fonts = {&g_small}; thunder_render* q = make(b4); if (q) {
      CHECK(thunder_render_set_pad_sf(q, 0, 0, 0, 0, nullptr, nullptr, 0) == 0);
      float l[64], rr[64]; CHECK(thunder_render_read(q, l, rr, 64) == 64);
      CHECK(thunder_render_add_font(q, g_small.data(), g_small.size()) == THUNDER_RENDER_E_STATE);
      CHECK(thunder_render_set_pad_sf(q, 0, 0, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_STATE);
      CHECK(thunder_render_set_pad_sf(q, 3, 0, 0, 0, nullptr, nullptr, 0) == THUNDER_RENDER_E_STATE);
      CHECK(thunder_render_clear_pad_sf(q, 0) == THUNDER_RENDER_E_STATE);
      CHECK(thunder_render_remove_font(q, 0) == THUNDER_RENDER_E_STATE);
      thunder_render_free(q); } }
}

int main() {
  g_gm = readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3"); g_small = readFile(std::string(THUNDER_ROOT) + "/native/tests/golden/sf_test.sf2");
  if (g_gm.empty() || g_small.empty()) { std::printf("FAIL: fonts not found\n"); return 1; }

  // a held organ note (rows 0..), a second note on another key, a note-off, a bend
  const std::vector<std::vector<Row>> held = {{N(60, 0), {}, {}, {}, N(64, 0), {}, {NOTE_OFF_V, -1}, {}}};
  const std::string heldSong = songJson(held, {0});

  // (1) the seam
  { const Seam s = seam(heldSong, {0}, 19, {60, 64}, 560);
    std::printf("seam: notes %d off %d stp %d played %d peak %.4f\n", s.nOn, s.nOff, s.nStp, s.played, s.peak);
    CHECK(s.nOn == 2 && s.nOff == 1); CHECK(s.played == 2); CHECK(s.peak > 0.01); CHECK(s.equal); }
  { std::vector<std::vector<Row>> t = {{N(60, 0, kFxPBN, 24), {}, {}, {}, N(64, 0), {}, {}, {}}, {N(67, 0), {}, {}, {NOTE_OFF_V, -1}, {}, {}, {}, {}}};   // a bend; two tracks
    const Seam s = seam(songJson(t, {0}), {0}, 19, {60, 64, 67}, 560); CHECK(s.nOn == 3 && s.played == 3 && s.peak > 0.01 && s.equal); }
  { std::vector<std::vector<Row>> t = {{N(60, 0), {}, {}, {}, N(64, 0), {}, {-1, -1, kFxSTP, 1}, {}}};   // STP
    const Seam s = seam(songJson(t, {0}), {0}, 19, {60, 64}, 560); std::printf("seam stp: stp %d\n", s.nStp); CHECK(s.nStp >= 1 && s.equal && s.peak > 0.01); }

  // (2) the C API against a direct SongRenderer, and every block size
  { Rig g = organ(heldSong, {60, 64}); const Out ref = run(g, (int)kFrames);
    CHECKF(ref.peak > 0.01, "the organ plays: peak %g", ref.peak); CHECK(ref.st.notes == 2 && ref.st.notes_sf == 2 && ref.st.notes_sf_played == 2 && ref.st.notes_played == 0 && ref.st.notes_live == 0);
    for (int bs : {1, 64, 128, 333, 4096}) { const Out o = run(g, bs); CHECKF(same(ref, o), "block %d differs", bs); CHECK(o.st.notes_sf_played == 2); }
    // the direct drive
    Song song; JsonError je; CHECK(song.load(g.song.data(), g.song.size(), &je));
    sf::Font font; CHECK(sf::parse(g_gm.data(), g_gm.size(), font) == sf::kParseOk); const sf::Preset* pr = sf::sf_preset(font, 0, 19);
    uint8_t flags[128] = {}; flags[60] = flags[64] = 1; sf::PresetPcm pcm; sf::DecodeOpts dop; CHECK(sf::prepare_preset(font, pr, dop, pcm, flags) == sf::kPrepOk);
    SeqVars vars; RenderPads pads; pads.route[0] = PadRoute::SoundFont; pads.sf[0].font = &font; pads.sf[0].pr = pr; pads.sf[0].pcm = &pcm; pads.sf[0].P.bank = 0; pads.sf[0].P.prog = 19;
    SeqOpts so; so.row = song.renderFrom; so.once = true; SongRenderer R(song, vars, pads, 48000, 128, so);
    std::vector<float> L((size_t)kFrames), Rr((size_t)kFrames); R.block(L.data(), Rr.data(), (int)kFrames);
    CHECK(!std::memcmp(L.data(), ref.L.data(), L.size() * 4) && !std::memcmp(Rr.data(), ref.R.data(), Rr.size() * 4));
    CHECK(R.stats().notesSfPlayed == 2);
    // reads allocate nothing
    thunder_render* r = make(g); if (r) { std::vector<float> a(1000), b(1000); thunder_render_read(r, a.data(), b.data(), 1000); g_allocs = 0; g_count = true;
      int64_t n = 0; for (int i = 0; i < 60; i++) n += thunder_render_read(r, a.data(), b.data(), 1000); g_count = false; CHECK(n == 60000); CHECKF(g_allocs == 0, "%ld allocations while reading", g_allocs); thunder_render_free(r); } }

  // (3) the track takeover, both ways. pad 0 = the organ (SF), pad 2 = a DC sampler pad; inst 0 -> pad 0, inst 1 -> pad 2
  { Rig base = organ("", {60}); base.dcPads = {{2, 0.2f}};
    auto mk = [&](std::vector<Row> rows) { Rig g = base; g.song = songJson({rows}, {0, 2}); return g; };
    const Out sfThenSamp = run(mk({N(60, 0), {}, N(60, 1), {}, {}, {}, {}, {}})), sampOnly = run(mk({{}, {}, N(60, 1), {}, {}, {}, {}, {}}));
    const Out sampThenSf = run(mk({N(60, 1), {}, N(60, 0), {}, {}, {}, {}, {}})), sfOnly = run(mk({{}, {}, N(60, 0), {}, {}, {}, {}, {}}));
    const Out sfAlone = run(mk({N(60, 0), {}, {}, {}, {}, {}, {}, {}}));
    CHECKF(sampOnly.peak > 0.05 && sfOnly.peak > 0.01 && sfAlone.peak > 0.01, "peaks %g %g %g", sampOnly.peak, sfOnly.peak, sfAlone.peak);
    // the sampler takes over the organ: after the cut has died away the two renders are the same (without the takeover the organ would still sound)
    CHECKF(maxdiff(sfThenSamp, sampOnly, 48000, 72000) < 1e-6, "sf then sampler: %g", maxdiff(sfThenSamp, sampOnly, 48000, 72000));
    CHECK(maxdiff(sfThenSamp, sampOnly, 0, 12000) > 1e-3);   // (before the takeover the organ sounded)
    // the organ takes over the sampler note
    CHECKF(maxdiff(sampThenSf, sfOnly, 48000, 72000) < 1e-6, "sampler then sf: %g", maxdiff(sampThenSf, sfOnly, 48000, 72000));
    CHECK(maxdiff(sampThenSf, sfOnly, 0, 12000) > 1e-3);
    CHECK(sfThenSamp.st.notes_played == 1 && sfThenSamp.st.notes_sf_played == 1);
    // the sampler note on a SF-route pad's neighbour does not need the route: pad 2 stays SAMPLER
    CHECK(sampThenSf.st.notes_played == 1 && sampThenSf.st.notes_sf_played == 1); }

  // note-off and stp
  { const Rig held1 = organ(songJson({{N(60, 0), {}, {}, {}, {}, {}, {}, {}}}, {0}));
    const Rig off1 = organ(songJson({{N(60, 0), {}, {NOTE_OFF_V, -1}, {}, {}, {}, {}, {}}}, {0}));
    const Rig stp1 = organ(songJson({{N(60, 0), {}, {-1, -1, kFxSTP, 1}, {}, {}, {}, {}, {}}}, {0}));
    const Out h = run(held1), o = run(off1), s = run(stp1);
    const double rh = rms(h.L, 48000, 70000), ro = rms(o.L, 48000, 70000), rs = rms(s.L, 48000, 70000);
    std::printf("held %.5f off %.5f stp %.5f\n", rh, ro, rs);
    CHECK(rh > 0.005); CHECK(ro < rh * 0.1); CHECK(rs < rh * 0.01);
    CHECK(maxdiff(h, o, 0, 12000) == 0.0); CHECK(maxdiff(h, o, 20000, 48000) > 1e-3); }

  // (4) two pads sharing one font = two copies of the font, bit for bit; replace and remove
  { const std::vector<std::vector<Row>> two = {{N(60, 0), {}, {}, {}, {}, {}, {}, {}}, {N(64, 1), {}, {}, {}, {}, {}, {}, {}}};
    Rig sharedR; sharedR.song = songJson(two, {0, 1}); sharedR.fonts = {&g_gm};
    SfSet a; a.pad = 0; a.font = 0; a.prog = 19; a.keys = {60}; SfSet b; b.pad = 1; b.font = 0; b.prog = 33; b.keys = {64};
    sharedR.sf = {a, b}; sharedR.routes = {{0, 2}, {1, 2}};
    Rig sep = sharedR; sep.fonts = {&g_gm, &g_gm}; sep.sf[1].font = 1;
    const Out o1 = run(sharedR), o2 = run(sep); CHECK(o1.peak > 0.01); CHECK(same(o1, o2)); CHECK(o1.st.notes_sf_played == 2);
    // each pad alone: the sum of two tracks differs from either (both sound)
    Rig only0 = sharedR; only0.sf = {a}; only0.routes = {{0, 2}, {1, 2}}; const Out p0 = run(only0); CHECK(p0.st.notes_sf_played == 1 && p0.st.notes_sf == 2);
    CHECK(!same(p0, o1));
    // replace (set twice) = set once with the last values; remove (clear) = never set
    thunder_render* r = make(sharedR); thunder_render* r2 = nullptr; (void)r2;
    if (r) { SfSet c = a; c.prog = 0; c.keys = {60};
      CHECK(thunder_render_set_pad_sf(r, 0, 0, 0, 0, nullptr, c.keys.data(), 1) == 0);
      std::vector<float> L((size_t)kFrames), R((size_t)kFrames); thunder_render_read(r, L.data(), R.data(), kFrames); thunder_render_free(r);
      Rig once = sharedR; once.sf[0] = c; const Out oo = run(once); CHECK(!std::memcmp(L.data(), oo.L.data(), L.size() * 4)); CHECK(!same(oo, o1)); }
    r = make(sharedR); if (r) { CHECK(thunder_render_clear_pad_sf(r, 0) == 0); std::vector<float> L((size_t)kFrames), R((size_t)kFrames); thunder_render_read(r, L.data(), R.data(), kFrames);
      thunder_render_stats st; std::memset(&st, 0, sizeof st); st.size = sizeof st; thunder_render_stats_get(r, &st); thunder_render_free(r);
      Rig never = sharedR; never.sf = {b}; const Out nn = run(never); CHECK(!std::memcmp(L.data(), nn.L.data(), L.size() * 4)); CHECK(st.notes_sf == 2 && st.notes_sf_played == 1); }
    // the key list narrows what is decoded: a note on a key that was not listed is silent, listed it plays
    { Rig k = organ(heldSong, {64}); const Out ko = run(k); CHECK(ko.st.notes == 2 && ko.st.notes_sf == 2 && ko.st.notes_sf_played == 1);
      Rig k2 = organ(heldSong, {60}); const Out k2o = run(k2); CHECK(k2o.st.notes_sf_played == 1 && !same(ko, k2o));
      Rig all = organ(heldSong, {}); const Out ao = run(all); CHECK(ao.st.notes_sf_played == 2); Rig both = organ(heldSong, {60, 64}); CHECK(same(ao, run(both))); } }

  // (5) silence: SF route with no sf, route Sampler with an sf prepared, no zone (a font preset with no zone for the key)
  { Rig g; g.song = heldSong; g.routes = {{0, 2}}; const Out o = run(g);
    CHECK(o.peak == 0.0 && o.st.notes_sf == 2 && o.st.notes_sf_played == 0 && o.st.notes == 2 && o.st.notes_played == 0);
    Rig h = organ(heldSong, {60, 64}); h.routes.clear(); h.dcPads = {{0, 0.2f}}; const Out o2 = run(h);   // the SF is prepared but the pad is a SAMPLER pad: the DC buffer plays
    CHECK(o2.st.notes_sf == 0 && o2.st.notes_sf_played == 0 && o2.st.notes_played == 2 && o2.peak > 0.05); }

  // (8) verify-242 F3: a POLY track's cap of four is shared by the sampler's and the SoundFont's voices, oldest first (core-processor.js :442-443 and :582-583: one list per track). Track 4 of 8 is a poly track.
  // pad 0 = the organ (SF route, keys 60 64 67 71 72), pad 2 = a DC sampler pad; inst 0 -> pad 0, inst 1 -> pad 2. Once the released voice has died away (0.006 s tc; compared from frame 42000 on) the render equals the
  // render of the song WITHOUT the notes the cap released: without the shared cap the oldest voice of the other pool rings on (a held organ note: about full level; the DC pad: 0.2).
  { Rig base = organ("", {60, 64, 67, 71, 72}); base.dcPads = {{2, 0.2f}};
    auto mk = [&](std::vector<Row> rows) { Rig g = base; g.song = songJson({{}, {}, {}, {}, rows}, {0, 2}); return g; };
    const Row O = N(60, 0), O2 = N(64, 0), O3 = N(67, 0), O4 = N(71, 0), P = N(60, 1), P2 = N(62, 1);
    // (a) four SF notes, then a sampler note: the fifth. The oldest organ note (row 0) is released.
    const Out a = run(mk({O, O2, O3, O4, P, {}, {}, {}})), aWithout = run(mk({{}, O2, O3, O4, P, {}, {}, {}}));
    CHECKF(a.peak > 0.01 && a.st.notes_sf_played == 4 && a.st.notes_played == 1, "poly (a): peak %g played %d %d", a.peak, a.st.notes_sf_played, a.st.notes_played);
    CHECKF(maxdiff(a, aWithout, 42000, 72000) < 1e-6, "poly: four SF notes then a sampler note: the oldest SF note rings on (diff %g)", maxdiff(a, aWithout, 42000, 72000));
    CHECK(maxdiff(a, aWithout, 0, 12000) > 1e-4);   // (before the fifth note row 0's note sounds)
    // (b) the reverse: four sampler notes, then an SF note: the oldest sampler note is released
    const Out b = run(mk({P, P2, P, P2, O, {}, {}, {}})), bWithout = run(mk({{}, P2, P, P2, O, {}, {}, {}}));
    CHECKF(b.peak > 0.05 && b.st.notes_played == 4 && b.st.notes_sf_played == 1, "poly (b): %g %d %d", b.peak, b.st.notes_played, b.st.notes_sf_played);
    CHECKF(maxdiff(b, bWithout, 42000, 72000) < 1e-6, "poly: four sampler notes then an SF note: the oldest sampler note rings on (diff %g)", maxdiff(b, bWithout, 42000, 72000));
    // (c) mixed ages: SF, sampler, SF, sampler, then a sampler note (releases row 0, an SF note), then an SF note (releases row 1, a sampler note)
    const Out c = run(mk({O, P, O3, P2, P, O4, {}, {}})), cWithout = run(mk({{}, {}, O3, P2, P, O4, {}, {}}));
    CHECKF(maxdiff(c, cWithout, 46000, 72000) < 1e-6, "poly: mixed ages (diff %g)", maxdiff(c, cWithout, 46000, 72000));
    // (d) one pool alone is as before: five SF notes (the JS's list and ours are the same list)
    const Out d = run(mk({O, O2, O3, O4, N(72, 0), {}, {}, {}})), dWithout = run(mk({{}, O2, O3, O4, N(72, 0), {}, {}, {}}));
    CHECKF(maxdiff(d, dWithout, 42000, 72000) < 1e-6, "poly: five SF notes (diff %g)", maxdiff(d, dWithout, 42000, 72000));
    // (e) a MONO track (track 0) is unchanged: the second note cuts the first, in either pool
    { Rig g = base; g.song = songJson({{O, O2, P, O3, {}, {}, {}, {}}}, {0, 2}); const Out m = run(g); CHECK(m.st.notes_sf_played == 3 && m.st.notes_played == 1); } }

  // (9) verify-242 F4: three rules the first tests did not pin (each fails when its line in render.cpp's dispatch is removed)
  { Rig base = organ("", {60, 72}); base.dcPads = {{2, 0.2f}}; base.routes.push_back({4, THUNDER_RENDER_ROUTE_SF});   // pad 4: an SF-route pad with NO prepared preset
    auto mk = [&](std::vector<Row> rows, const std::vector<int>& insts = {0, 2, 4}) { Rig g = base; g.song = songJson({rows}, insts); return g; };
    // (a) a note with no zone for its key takes NOTHING over: the organ (prog 19) has no zone above key 96, so the sampler note on the track goes on sounding, bit for bit as if the SF note were not there
    const Out held = run(mk({N(60, 1), {}, {}, {}, {}, {}, {}, {}})), noZone = run(mk({N(60, 1), {}, N(100, 0), {}, {}, {}, {}, {}}));
    CHECKF(held.peak > 0.05 && same(held, noZone), "no zone: the note takes the track over (diff %g)", maxdiff(held, noZone, 0, kFrames));
    CHECK(noZone.st.notes_sf == 1 && noZone.st.notes_sf_played == 0);
    const Out zone = run(mk({N(60, 1), {}, N(72, 0), {}, {}, {}, {}, {}}));   // (control: a key it covers does take over, and sounds)
    CHECK(!same(held, zone) && zone.st.notes_sf_played == 1);
    // (b) an SF-route pad with nothing prepared (silent) still cuts the track's previous note: the sampler note is gone after the cut (without the takeover it would ring on: 0.2)
    const Out cut = run(mk({N(60, 1), {}, N(60, 2), {}, {}, {}, {}, {}}));
    CHECKF(cut.st.notes_sf == 1 && cut.st.notes_sf_played == 0 && cut.peak > 0.05 && rms(cut.L, 48000, 72000) == 0.0 && rms(held.L, 48000, 72000) > 0.01, "unprepared SF pad: takeover (tail rms %g)", rms(cut.L, 48000, 72000));
    // (c) SEV_MOD reaches the SoundFont voices: a KIL (cut after 1 tick) on a row with no note ends the held organ note; without it the note rings on
    const Out ringing = run(mk({N(60, 0), {}, {}, {}, {}, {}, {}, {}})), killed = run(mk({N(60, 0), {-1, 0, 8, 1}, {}, {}, {}, {}, {}, {}}));
    CHECKF(rms(ringing.L, 30000, 72000) > 0.005 && rms(killed.L, 30000, 72000) < rms(ringing.L, 30000, 72000) * 0.01 && ringing.st.events > 0, "mod: ringing %g killed %g", rms(ringing.L, 30000, 72000), rms(killed.L, 30000, 72000));
    // a volume command on the held note: the SF voice's gain follows (VOL, no note)
    const Out vol = run(mk({N(60, 0), {-1, 0, 21, 40}, {}, {}, {}, {}, {}, {}}));
    CHECKF(!same(ringing, vol) && rms(vol.L, 30000, 72000) < rms(ringing.L, 30000, 72000) * 0.9, "mod: vol %g vs %g", rms(vol.L, 30000, 72000), rms(ringing.L, 30000, 72000)); }

  // (10) verify-242 F6: the SoundFont pool's counters and the combined peak are in the stats (new fields at the end of the struct)
  { // 32 poly tracks' worth: ntr 24 gives 12 poly tracks (4 notes each = 48 notes a row against 32 voices): the second row's notes find the pool full
    std::vector<std::vector<Row>> tr((size_t)24); for (int t = 12; t < 24; t++) { Row c = N(60, 0); c.c1 = 64 - 12; c.c2 = 67 - 12; c.c3 = 72 - 12; tr[(size_t)t] = {c, c, {}, {}, {}, {}, {}, {}}; }
    Rig g = organ(songJson(tr, {0}, 24), {60, 64, 67, 72}); const Out o = run(g, 4096, 24000);
    std::printf("stats: notes_sf %d played %d sf_refused %u track_full %u auto %u peak %d peak_all %d\n", o.st.notes_sf, o.st.notes_sf_played, o.st.sf_voices_refused, o.st.sf_voice_track_full, o.st.sf_voice_auto_dropped, o.st.peak_voices, o.st.peak_voices_all);
    CHECK(o.st.notes_sf > o.st.notes_sf_played);
    CHECKF(o.st.sf_voices_refused > 0 && o.st.sf_voices_refused == (uint32_t)(o.st.notes_sf - o.st.notes_sf_played), "sf_voices_refused %u (lost notes %d)", o.st.sf_voices_refused, o.st.notes_sf - o.st.notes_sf_played);
    CHECK(o.st.peak_voices == 0 && o.st.peak_voices_all == 32 && o.st.voices_refused == 0);   // the sampler's own figures stay the sampler's
    // the same numbers from a direct SongRenderer
    Song song; JsonError je; CHECK(song.load(g.song.data(), g.song.size(), &je));
    sf::Font font; CHECK(sf::parse(g_gm.data(), g_gm.size(), font) == sf::kParseOk); const sf::Preset* pr = sf::sf_preset(font, 0, 19);
    uint8_t flags[128] = {}; flags[60] = flags[64] = flags[67] = flags[72] = 1; sf::PresetPcm pcm; sf::DecodeOpts dop; CHECK(sf::prepare_preset(font, pr, dop, pcm, flags) == sf::kPrepOk);
    SeqVars vars; RenderPads pads; pads.route[0] = PadRoute::SoundFont; pads.sf[0].font = &font; pads.sf[0].pr = pr; pads.sf[0].pcm = &pcm; pads.sf[0].P.prog = 19;
    SeqOpts so; so.row = song.renderFrom; so.once = true; SongRenderer R(song, vars, pads, 48000, 128, so);
    std::vector<float> L(24000), Rr(24000); R.block(L.data(), Rr.data(), 24000); const RenderStats rs = R.stats();
    CHECK(rs.sfVoiceDropped == R.sfPool().dropped() && rs.sfVoiceOverflow == R.sfPool().tOverflow() && rs.sfVoiceAutoDropped == R.sfPool().autoDropped() && rs.sfVoiceDropped == o.st.sf_voices_refused && rs.peakVoicesAll == 32);
    // the struct rule: a caller built against the old header (size up to notes_sf_played) gets only its fields and nothing is written past its size
    thunder_render* r = make(g, 24000); if (r) { std::vector<float> a(24000), b(24000); thunder_render_read(r, a.data(), b.data(), 24000);
      struct { thunder_render_stats s; unsigned char guard[16]; } buf; std::memset(&buf, 0xAB, sizeof buf);
      buf.s.size = (uint32_t)offsetof(thunder_render_stats, sf_voices_refused); CHECK(thunder_render_stats_get(r, &buf.s) == 0);
      CHECK(buf.s.size == offsetof(thunder_render_stats, sf_voices_refused) && buf.s.notes_sf_played == o.st.notes_sf_played);
      const unsigned char* tail = (const unsigned char*)&buf.s + offsetof(thunder_render_stats, sf_voices_refused); bool untouched = true; for (size_t i = 0; i < sizeof buf - offsetof(thunder_render_stats, sf_voices_refused); i++) untouched &= tail[i] == 0xAB; CHECK(untouched);
      thunder_render_free(r); } }

  hostile();
  std::printf(g_fail ? "FAILED (%d of %d checks)\n" : "render_sf ok (%d checks)\n", g_fail ? g_fail : g_checks, g_checks);
  return g_fail ? 1 : 0;
}
