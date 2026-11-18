// U04: the LXR seam, in BOTH configurations (the core alone, THUNDER_WITH_LXR=OFF; and with the module, ON). Built from the same file; THUNDER_HAVE_LXR tells which.
//   OFF (nothing registered): there is no api; an LXR voice is refused (kEngUnsupported) and xeng_voice_from_json says false; every other engine renders exactly as before.
//   ON : the same refusal BEFORE the module registers and AFTER it unregisters; after thunder_lxr_module_register() the voice renders; a wrong abi / size is rejected.
// The test never touches the LXR sources: it uses only thunder/lxr_seam.h and the core.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include "engine/xeng.h"
#include "rt/json.h"
#include "thunder/lxr_seam.h"
#ifdef THUNDER_HAVE_LXR
#include "thunder/lxr_module.h"
#endif
using namespace thunder;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("  FAIL: " __VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static int renderLxr(const char* name, size_t* n, std::vector<float>* out) {
  XengVoice V; V.kind = xeng_kind(name, std::strlen(name)); V.level = 0; V.len = 0.5;
  std::memset(&V.lxr, 0, sizeof V.lxr); V.lxr.kind = 0; V.lxr.vel = 100; V.lxr.sr = 48000; V.lxr.bpm = 120; V.lxr.vlen = 0.5; V.lxr.hold = 0; V.lxr.level = 0;
  const ThunderLxrApi* a = thunder_lxr_api();
  if (a) { ThunderLxrDescribe d; std::memset(&d, 0, sizeof d); if (a->describe(0, &d) == 0) for (int i = 0; i < THUNDER_LXR_MAX_PARAMS; i++) if (d.present[i]) V.lxr.p[i] = d.def[i]; }
  EngRenderOpts o; o.sr = 48000;
  const size_t bound = xeng_render_bound(V, o);
  out->assign((bound ? bound : 1) + 1, 0.f);
  return xeng_render_voice(V, o, out->data(), out->size() - 1, n);
}
static int renderTone(double* peak) {
  XengVoice V; V.kind = xeng_kind("tone", 4); V.level = -3; V.len = 0.3; EngRenderOpts o; std::vector<float> b(xeng_render_bound(V, o) + 1); size_t n = 0;
  const int st = xeng_render_voice(V, o, b.data(), b.size() - 1, &n); double p = 0; for (size_t i = 0; i < n; i++) p = std::fmax(p, std::fabs(b[i])); *peak = p; return st;
}
int main() {
  size_t n = 0; std::vector<float> buf; double p0 = 0, p1 = 0;
  CHECK(xeng_kind("lxdrum", 6) == XKind::Lxr && xeng_kind("lxsnare", 7) == XKind::Lxr && xeng_kind("lxcym", 5) == XKind::Lxr && xeng_kind("lxhat", 5) == XKind::Lxr, "the four LXR engine names are XKind::Lxr");
  CHECK(thunder_lxr_api() == nullptr, "an api is registered before anything registered it");
  CHECK(renderLxr("lxdrum", &n, &buf) == kEngUnsupported, "an LXR voice must be refused (kEngUnsupported) with no module");
  CHECK(renderTone(&p0) == kEngOk && p0 > 0.01, "another engine must still render (peak %g)", p0);
  { JsonDoc d; const char* js = "{\"type\":\"lxdrum\",\"p1\":100}"; XengVoice V; d.parse(js); CHECK(!xeng_voice_from_json(V, d.root(), Json()), "xeng_voice_from_json must say false without a module"); }
  { ThunderLxrApi bad; std::memset(&bad, 0, sizeof bad); bad.abi = THUNDER_LXR_ABI + 1; bad.size = sizeof bad; CHECK(thunder_lxr_register(&bad) != 0 && thunder_lxr_api() == nullptr, "a wrong abi must be rejected");
    bad.abi = THUNDER_LXR_ABI; CHECK(thunder_lxr_register(&bad) != 0 && thunder_lxr_api() == nullptr, "a table without its functions must be rejected"); }
#ifdef THUNDER_HAVE_LXR
  std::printf("lxr_seam: ON configuration (module linked)\n");
  CHECK(thunder_lxr_module_register() == 0 && thunder_lxr_api() != nullptr, "the module did not register");
  const int st = renderLxr("lxdrum", &n, &buf); double pk = 0; for (size_t i = 0; i < n; i++) pk = std::fmax(pk, std::fabs(buf[i]));
  CHECK(st == kEngOk && n > 1000 && pk > 0.01, "a registered module must render a voice (status %d, n %zu, peak %g)", st, n, pk);
  for (const char* k : {"lxsnare", "lxcym", "lxhat"}) { CHECK(renderLxr(k, &n, &buf) == kEngOk, "%s must render with the module", k); }
  { ThunderLxrApi tooSmall = *thunder_lxr_api(); tooSmall.size = 8; thunder_lxr_unregister(); CHECK(thunder_lxr_register(&tooSmall) != 0, "a table that is smaller than the header says must be rejected"); }
  thunder_lxr_unregister();
  CHECK(renderLxr("lxdrum", &n, &buf) == kEngUnsupported, "after unregister the refusal must be back");
  CHECK(renderTone(&p1) == kEngOk && p1 == p0, "another engine changed with the module in the process (%g vs %g)", p1, p0);
#else
  std::printf("lxr_seam: OFF configuration (no module in this binary)\n");
  CHECK(renderTone(&p1) == kEngOk && p1 == p0, "repeat render of another engine differs (%g vs %g)", p1, p0);
#endif
  if (fails) { std::printf("lxr_seam: %d FAIL\n", fails); return 1; }
  std::printf("lxr_seam: PASS\n"); return 0;
}
