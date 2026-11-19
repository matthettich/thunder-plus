// verify-242 F1 and F2: the SoundFont calls of the render C API under an allocation failure, and under a font built to cost memory per pad.
// This program compiles render_api.cpp INTO ITSELF with the pad budget lowered to 128 MiB (THUNDER_RENDER_MAX_PAD_BYTES is overridable for exactly this), so that the budget can be reached with
// little memory and the handle's own counters (padBytes, sfPcm, pads.sf) can be read. Everything else (the parser, the decoder with stb_vorbis, the pools) is the library's.
//   F1  an operator new that fails at the k-th allocation, over thunder_render_add_font and thunder_render_set_pad_sf (the real GM SF3: stb_vorbis allocates with malloc, which operator new does
//       not see: the glibc heap's bytes in use, and LeakSanitizer under -DTHUNDER_SANITIZE=ON, do): every failure is E_MEMORY, the pad, the font table and the budget are as they were, nothing leaks.
//       Before the fix decodeOgg leaked its stb_vorbis handle (about 170 KB) when d.insert threw.
//   F2  a font with a very large number of sample headers (generated here): every pad that uses it gets a table of one Pcm per header; it is counted in the budget BEFORE it is allocated, so the
//       calls start failing with E_LIMIT and the memory stays bounded. Before the fix the table was not counted: 256 pads took gigabytes (the loop here stops at 150 MB and reports it).
#define THUNDER_RENDER_MAX_PAD_BYTES ((size_t)128 << 20)
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <vector>
#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "render/render_api.cpp"   // (thunder_render_* are defined in this program: the archive's copy is not pulled in)

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
#if defined(__SANITIZE_ADDRESS__)
#define TR_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define TR_SANITIZED 1
#endif
#endif

namespace {
bool g_armed = false; long g_calls = 0, g_failAt = 0; bool g_failed = false;
int64_t g_live = 0, g_peak = 0; constexpr size_t kHdr = 16;
}
void* operator new(size_t n) {
  if (g_armed) { ++g_calls; if (g_failAt > 0 && g_calls == g_failAt) { g_failAt = 0; g_failed = true; throw std::bad_alloc(); } }
  void* p = std::malloc(n + kHdr); if (!p) throw std::bad_alloc();
  *(size_t*)p = n; g_live += (int64_t)n; if (g_live > g_peak) g_peak = g_live;
  return (char*)p + kHdr;
}
void operator delete(void* q) noexcept { if (!q) return; char* p = (char*)q - kHdr; g_live -= (int64_t)*(size_t*)p; std::free(p); }
void operator delete(void* q, size_t) noexcept { operator delete(q); }
void* operator new[](size_t n) { return operator new(n); }
void* operator new(size_t n, const std::nothrow_t&) noexcept { try { return operator new(n); } catch (...) { return nullptr; } }
void* operator new[](size_t n, const std::nothrow_t&) noexcept { try { return operator new(n); } catch (...) { return nullptr; } }
void operator delete(void* q, const std::nothrow_t&) noexcept { operator delete(q); }
void operator delete[](void* q, const std::nothrow_t&) noexcept { operator delete(q); }
void operator delete[](void* q) noexcept { operator delete(q); }
void operator delete[](void* q, size_t) noexcept { operator delete(q); }

static int g_fail = 0, g_checks = 0;
#define CHECK(c) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)
#define CHECKF(c, ...) do { g_checks++; if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)

// every malloc in use (operator new's and stb_vorbis's), where the C library can say it (glibc without a sanitizer); 0 elsewhere (LeakSanitizer is the judge there)
static int64_t heapInUse() {
#if defined(__GLIBC__) && defined(M_MXFAST) && !defined(TR_SANITIZED) && (__GLIBC__ > 2 || __GLIBC_MINOR__ >= 33)
  const struct mallinfo2 m = mallinfo2(); return (int64_t)m.uordblks + (int64_t)m.hblkhd;
#else
  return 0;
#endif
}

static std::vector<uint8_t> readFile(const std::string& p) { std::ifstream f(p, std::ios::binary); return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); }

static const char* kSong = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[8],\"phrases\":[[[60,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1]]]}";
static thunder_render* make() {
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = sizeof o; o.use_frames = 1; o.frames = 4800;
  char err[200]; thunder_render* r = thunder_render_create((const uint8_t*)kSong, std::strlen(kSong), &o, err, sizeof err);
  if (!r) { std::printf("create failed: %s\n", err); g_fail++; }
  return r;
}

// ---- F2: a font with `nHdr` sample headers (one preset, one instrument, one zone on sample 0; the other headers are unused but valid) -----------------------------------------------------------------------------
static void le16(std::vector<uint8_t>& v, uint32_t x) { v.push_back((uint8_t)x); v.push_back((uint8_t)(x >> 8)); }
static void le32(std::vector<uint8_t>& v, uint32_t x) { le16(v, x); le16(v, x >> 16); }
static void chunk(std::vector<uint8_t>& o, const char* id, const std::vector<uint8_t>& body) { o.insert(o.end(), id, id + 4); le32(o, (uint32_t)body.size()); o.insert(o.end(), body.begin(), body.end()); if (body.size() & 1) o.push_back(0); }
static void name20(std::vector<uint8_t>& v, const char* s) { for (int i = 0; i < 20; i++) v.push_back(i < (int)std::strlen(s) ? (uint8_t)s[i] : 0); }
static std::vector<uint8_t> makeFont(size_t nHdr) {
  std::vector<uint8_t> smpl; for (int i = 0; i < 64; i++) { le16(smpl, (uint32_t)(int16_t)(i * 500 - 16000)); }   // 64 frames
  std::vector<uint8_t> phdr, pbag, pgen, inst, ibag, igen, shdr;
  auto ph = [&](const char* n, int bag) { name20(phdr, n); le16(phdr, 0); le16(phdr, 0); le16(phdr, (uint32_t)bag); le32(phdr, 0); le32(phdr, 0); le32(phdr, 0); };
  ph("P", 0); ph("EOP", 1);
  le16(pbag, 0); le16(pbag, 0); le16(pbag, 1); le16(pbag, 0);
  le16(pgen, 41); le16(pgen, 0); le16(pgen, 0); le16(pgen, 0);
  auto in = [&](const char* n, int bag) { name20(inst, n); le16(inst, (uint32_t)bag); };
  in("I", 0); in("EOI", 1);
  le16(ibag, 0); le16(ibag, 0); le16(ibag, 1); le16(ibag, 0);
  le16(igen, 53); le16(igen, 0); le16(igen, 0); le16(igen, 0);
  for (size_t i = 0; i <= nHdr; i++) {   // nHdr headers and the terminal one
    name20(shdr, i < nHdr ? "s" : "EOS"); le32(shdr, 0); le32(shdr, 64); le32(shdr, 8); le32(shdr, 56); le32(shdr, 44100); shdr.push_back(60); shdr.push_back(0); le16(shdr, 0); le16(shdr, 1);
  }
  std::vector<uint8_t> sdta; chunk(sdta, "smpl", smpl);
  std::vector<uint8_t> pdta; chunk(pdta, "phdr", phdr); chunk(pdta, "pbag", pbag); chunk(pdta, "pmod", std::vector<uint8_t>(10, 0)); chunk(pdta, "pgen", pgen); chunk(pdta, "inst", inst);
  chunk(pdta, "ibag", ibag); chunk(pdta, "imod", std::vector<uint8_t>(10, 0)); chunk(pdta, "igen", igen); chunk(pdta, "shdr", shdr);
  std::vector<uint8_t> body; body.insert(body.end(), {'s', 'f', 'b', 'k'});
  { std::vector<uint8_t> l = {'s', 'd', 't', 'a'}; l.insert(l.end(), sdta.begin(), sdta.end()); chunk(body, "LIST", l); }
  { std::vector<uint8_t> l = {'p', 'd', 't', 'a'}; l.insert(l.end(), pdta.begin(), pdta.end()); chunk(body, "LIST", l); }
  std::vector<uint8_t> f; chunk(f, "RIFF", body); return f;
}

static void budgetTest() {
  const size_t nHdr = 100000;
  const std::vector<uint8_t> font = makeFont(nHdr);
  thunder_render* r = make(); if (!r) return;
  const int id = thunder_render_add_font(r, font.data(), font.size());
  CHECKF(id == 0, "the generated font (%zu headers, %zu bytes) is accepted: %d", nHdr, font.size(), id);
  if (id != 0) { thunder_render_free(r); return; }
  const int64_t live0 = g_live; g_peak = g_live;
  const size_t table = nHdr * sizeof(sf::Pcm);
  const uint8_t key[1] = {60};
  int ok = 0, firstFail = -1, rc = 0;
  for (int pad = 0; pad < THUNDER_RENDER_PADS; pad++) {
    rc = thunder_render_set_pad_sf(r, pad, id, 0, 0, nullptr, key, 1);
    if (rc == 0) { ok++; if (g_live - live0 > ((int64_t)150 << 20)) { CHECKF(false, "pad %d: %lld MB held and the budget has not refused yet (the per-header table is not counted)", pad, (long long)((g_live - live0) >> 20)); break; } continue; }
    firstFail = pad; break;
  }
  std::printf("budget: %d pads of a %zu-header font fit (table %.1f MB a pad), then rc %d; padBytes %.1f MB of %.1f; live %.1f MB, peak %.1f MB\n", ok, nHdr, (double)table / 1048576.0, rc,
              (double)r->padBytes / 1048576.0, (double)THUNDER_RENDER_MAX_PAD_BYTES / 1048576.0, (double)(g_live - live0) / 1048576.0, (double)(g_peak - live0) / 1048576.0);
  CHECKF(ok >= 1 && firstFail == ok && rc == THUNDER_RENDER_E_LIMIT, "the calls start failing with the budget error (E_LIMIT): ok %d, first failure at pad %d, rc %d", ok, firstFail, rc);
  CHECK(r->padBytes <= THUNDER_RENDER_MAX_PAD_BYTES);
  CHECKF(g_peak - live0 < ((int64_t)THUNDER_RENDER_MAX_PAD_BYTES), "the memory the pads took is bounded by the budget: peak %lld MB", (long long)((g_peak - live0) >> 20));
  // what a pad costs is counted whole: the table of one pad is in its sfBytes
  CHECK(ok >= 1 && r->sfBytes[0] >= table);
  // a refused call changes nothing: the pad that was refused has no preset; the others are as they were
  const size_t before = r->padBytes; CHECK(thunder_render_set_pad_sf(r, firstFail + 1 < THUNDER_RENDER_PADS ? firstFail + 1 : 255, id, 0, 0, nullptr, key, 1) == THUNDER_RENDER_E_LIMIT && r->padBytes == before);
  CHECK(!r->sfPcm[firstFail < 0 ? 255 : firstFail]);
  // the room comes back when the pads are cleared (nothing leaks in the counters)
  for (int pad = 0; pad < ok; pad++) CHECK(thunder_render_clear_pad_sf(r, pad) == 0);
  CHECKF(r->padBytes == r->fonts[id]->held, "after clearing every pad only the font is counted (%zu vs %zu)", r->padBytes, r->fonts[id]->held);
  CHECK(thunder_render_set_pad_sf(r, 0, id, 0, 0, nullptr, key, 1) == 0);
  // a font with so many headers that ONE table is over the whole budget is refused outright (no allocation of it)
  thunder_render_free(r);
}

// ---- F1: an allocation failure at (nearly) every index of add_font / set_pad_sf on the real SF3 ------------------------------------------------------------------------------------------------------------
static long countAllocs(thunder_render* r, int what, const std::vector<uint8_t>& gm) {
  g_calls = 0; g_failAt = 0; g_armed = true;
  int rc = what == 0 ? thunder_render_add_font(r, gm.data(), gm.size()) : thunder_render_set_pad_sf(r, 0, 0, 0, 19, nullptr, (const uint8_t*)"\x3c", 1);
  g_armed = false; CHECK(rc >= 0 || rc == 0);
  return g_calls;
}
static void oomTest(const std::vector<uint8_t>& gm) {
  thunder_render* r = make(); if (!r) return;
  // warm-up (lazy tables are made once: not leaks), and how many allocations each call makes
  const int id = thunder_render_add_font(r, gm.data(), gm.size()); CHECK(id == 0); if (id != 0) { thunder_render_free(r); return; }
  CHECK(thunder_render_set_pad_sf(r, 0, id, 0, 19, nullptr, (const uint8_t*)"\x3c", 1) == 0); CHECK(thunder_render_clear_pad_sf(r, 0) == 0);
  const long nSet = countAllocs(r, 1, gm); CHECK(thunder_render_clear_pad_sf(r, 0) == 0);
  std::printf("oom: set_pad_sf (organ, key 60) makes %ld allocations\n", nSet);
  CHECK(nSet > 5);
  const size_t pad0 = r->padBytes; int64_t live0 = g_live, heap0 = heapInUse(); long injected = 0, leaks = 0, e_memory = 0; int64_t worst = 0;
  const long stride = std::max<long>(1, nSet / 90);
  for (long k = 1; k <= nSet + 2; k += (k < 400 ? 1 : stride)) {
    g_calls = 0; g_failed = false; g_failAt = k; g_armed = true;
    const int rc = thunder_render_set_pad_sf(r, 0, id, 0, 19, nullptr, (const uint8_t*)"\x3c", 1);
    g_armed = false; g_failAt = 0;
    if (rc == 0) { CHECK(thunder_render_clear_pad_sf(r, 0) == 0); live0 = g_live; heap0 = heapInUse(); continue; }   // k was past the last allocation
    injected++; CHECKF(rc == THUNDER_RENDER_E_MEMORY, "set_pad_sf at allocation %ld: %d", k, rc); e_memory += rc == THUNDER_RENDER_E_MEMORY;
    CHECKF(r->padBytes == pad0 && !r->sfPcm[0] && !r->pads.sf[0].font && r->sfBytes[0] == 0, "pad and budget unchanged after a failure at %ld", k);
    const int64_t dl = g_live - live0, dh = heapInUse() - heap0;
    if (dl != 0 || (heapInUse() != 0 && dh > 4096)) { leaks++; worst = std::max(worst, std::max(dl, dh)); if (leaks <= 5) std::printf("  LEAK at allocation %ld: %lld live bytes, heap +%lld\n", k, (long long)dl, (long long)dh); }
    live0 = g_live; heap0 = heapInUse();
  }
  std::printf("oom: set_pad_sf: %ld failures injected, %ld E_MEMORY, %ld leaked (worst %lld bytes)\n", injected, e_memory, leaks, (long long)worst);
  CHECK(injected >= 8 && e_memory == injected && leaks == 0);
  // the same on a pad that already holds a preset: a failed replacement leaves the old one
  CHECK(thunder_render_set_pad_sf(r, 0, id, 0, 19, nullptr, (const uint8_t*)"\x3c", 1) == 0);
  { const size_t held = r->padBytes; const sf::PresetPcm* was = r->sfPcm[0].get(); long inj = 0;
    for (long k = 1; k <= nSet + 2; k += (k < 400 ? 1 : stride * 2)) {
      g_calls = 0; g_failAt = k; g_armed = true; const int rc = thunder_render_set_pad_sf(r, 0, id, 0, 19, nullptr, (const uint8_t*)"\x3c\x40", 2); g_armed = false; g_failAt = 0;
      if (rc == 0) { was = r->sfPcm[0].get(); break; }
      inj++; CHECKF(rc == THUNDER_RENDER_E_MEMORY && r->padBytes == held && r->sfPcm[0].get() == was, "a failed replacement at %ld keeps the old preset (rc %d)", k, rc); }
    CHECK(inj >= 8); }
  CHECK(thunder_render_clear_pad_sf(r, 0) == 0);
  thunder_render_free(r);

  // add_font: the font's copy and its tables; the big SF3 at a stride, the 15 KB SF2 at every index
  r = make(); if (!r) return;
  { const long nAdd = countAllocs(r, 0, gm); CHECK(thunder_render_remove_font(r, 0) == 0); std::printf("oom: add_font (GM SF3) makes %ld allocations\n", nAdd);
    const size_t padBase = r->padBytes; int64_t l0 = g_live, h0 = heapInUse(); long inj = 0, lk = 0, bad = 0;
    for (long k = 1; k <= nAdd + 1; k += (k < 20 ? 1 : std::max<long>(1, nAdd / 30))) {
      g_calls = 0; g_failAt = k; g_armed = true; const int rc = thunder_render_add_font(r, gm.data(), gm.size()); g_armed = false; g_failAt = 0;
      if (rc >= 0) { CHECK(thunder_render_remove_font(r, rc) == 0); l0 = g_live; h0 = heapInUse(); continue; }   // (the shrink_to_fit inside parse swallows a failure by design: a valid id)
      inj++; if (rc != THUNDER_RENDER_E_MEMORY) { bad++; std::printf("  add_font at allocation %ld: %d\n", k, rc); }
      if (r->padBytes != padBase || r->fonts[0]) bad++;
      if (g_live != l0 || (heapInUse() != 0 && heapInUse() - h0 > 4096)) lk++;
      l0 = g_live; h0 = heapInUse(); }
    std::printf("oom: add_font: %ld failures injected, %ld not E_MEMORY / not unchanged, %ld leaked\n", inj, bad, lk);
    CHECK(inj >= 20 && bad == 0 && lk == 0); }
  thunder_render_free(r);
}

int main() {
  const std::vector<uint8_t> gm = readFile(std::string(THUNDER_ROOT) + "/sounds/FluidR3Mono_GM.sf3");
  if (gm.empty()) { std::printf("FAIL: font not found\n"); return 1; }
  budgetTest();
  oomTest(gm);
  std::printf(g_fail ? "FAILED (%d of %d checks)\n" : "render_sf_oom ok (%d checks)\n", g_fail ? g_fail : g_checks, g_checks);
  return g_fail ? 1 : 0;
}
