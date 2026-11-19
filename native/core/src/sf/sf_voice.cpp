// U32: sfSrc, sfVoice, sfZonesFor / sfPick, sfPreset (index.html), line for line.
#include <cmath>

#include "rt/jsmath.h"
#include "sf/sf.h"
#include "sf/sf_math.h"

namespace thunder {
namespace sf {
namespace {

// SF_DEF: the generators' defaults the JS reads when a zone does not set them (`SF_DEF[op] || 0`)
double defOf(int op) {
  switch (op) {
    case 8: return 13500;
    case 25: case 26: case 27: case 28: case 30: case 33: case 34: case 35: case 36: case 38: return -12000;
    case 46: case 47: return -1;
    case 56: return 100;
    case 58: return -1;
    default: return 0;
  }
}
inline double tcOf(double v) { return js_pow(2, v / 1200); }   // tc = v => Math.pow(2, v / 1200)

}  // namespace

double sf_src(unsigned code, double key, double vel, bool amt) {
  const unsigned idx = code & 127, cc = code & 128, neg = code & 256, bip = code & 512, typ = code >> 10;
  double x;
  if (cc) x = 0; else if (idx == 0) return amt ? 1 : 0; else if (idx == 2) x = vel / 128; else if (idx == 3) x = key / 128; else x = 0;
  if (neg) x = 1 - x;
  if (bip) x = x * 2 - 1;
  auto cv = [&](double u) -> double {
    if (typ == 1) return u >= 1 ? 1 : u <= 0 ? 0 : jmin(1, -20.0 / 96 * sf_log10((1 - u) * (1 - u)));
    if (typ == 2) return u <= 0 ? 0 : u >= 1 ? 1 : 1 - jmin(1, -20.0 / 96 * sf_log10(u * u));
    if (typ == 3) return u >= 0.5 ? 1 : 0;
    return u;
  };
  return bip ? jsign(x) * cv(std::fabs(x)) : cv(x);
}

void sf_voice(const Font& f, const Zone& z, double key, double vel, VoiceParams& o) {
  auto G = [&](int op) -> double { const GenV* g = f.gens.data() + z.gOff; for (int i = 0; i < z.gN; i++) if (g[i].op == op) return (double)g[i].v; return defOf(op); };
  double add[kGenN]; bool has[kGenN]; for (int i = 0; i < kGenN; i++) { add[i] = 0; has[i] = false; }
  // `if (v)` skips 0, -0 and NaN
  auto accum = [&](uint32_t off, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
      const Mod& m = f.mods[off + i];
      if (m.dst & 0x8000) continue;
      if (m.dst == 48 && ((m.src & 127) == 2) && !(m.src & 128)) continue;
      const double v = (double)m.amt * sf_src(m.src, key, vel, false) * sf_src(m.as, key, vel, true);
      if (v != 0 && v == v && m.dst < kGenN) { add[m.dst] = (has[m.dst] ? add[m.dst] : 0) + v; has[m.dst] = true; }
    }
  };
  accum(z.imOff, z.imN); accum(z.pmOff, z.pmN);
  auto g = [&](int op) -> double { return G(op) + (has[op] ? add[op] : 0); };
  const SampleHdr& s = f.samples[z.s];
  const double kd = 60 - key, root = G(58) >= 0 ? G(58) : (double)s.root;
  o.s = z.s; o.root = root; o.excl = G(57); o.modes = (int)(js_to_int32(G(54)) & 3); o.scale = G(56);
  o.tune = g(51) + (g(52) + (double)s.corr) / 100;
  o.atten = jmax(0, g(48)); o.pan = jmax(-500, jmin(500, g(17))); o.fc = g(8); o.q = jmax(0, g(9)); o.modEnvToFc = g(11);
  o.ofsStart = G(0) + G(4) * 32768; o.ofsEnd = G(1) + G(12) * 32768; o.ofsLs = G(2) + G(45) * 32768; o.ofsLe = G(3) + G(50) * 32768;
  o.v.delay = tcOf(g(33)); o.v.atk = tcOf(g(34)); o.v.hold = tcOf(g(35) + g(39) * kd); o.v.dec = tcOf(g(36) + g(40) * kd);
  o.v.sus = jmax(0, jmin(1440, g(37))); o.v.rel = tcOf(g(38));
  o.m.delay = tcOf(g(25)); o.m.atk = tcOf(g(26)); o.m.hold = tcOf(g(27) + g(31) * kd); o.m.dec = tcOf(g(28) + g(32) * kd);
  o.m.sus = jmax(0, jmin(1000, g(29))) / 1000; o.m.rel = tcOf(g(30));
}

int sf_pick(const Font& f, const Preset* pr, double key, double vel, Pick out[4]) {
  if (!pr) return 0;
  const Zone* Z = f.zones.data() + pr->z0; const uint32_t nz = pr->nz;
  auto match = [&](const Zone& z) { return key >= z.klo && key <= z.khi && vel >= z.vlo && vel <= z.vhi; };
  uint32_t used[16]; int nu = 0; int n = 0;
  auto isUsed = [&](uint32_t s) { for (int i = 0; i < nu; i++) if (used[i] == s) return true; return false; };
  for (uint32_t i = 0; i < nz; i++) {
    const Zone& z = Z[i]; if (!match(z)) continue;
    if (isUsed(z.s)) continue;
    const SampleHdr& s = f.samples[z.s];
    const Zone* partner = nullptr;
    if ((s.type & 6) && s.link < f.samples.size()) for (uint32_t j = 0; j < nz; j++) if (match(Z[j]) && Z[j].s == s.link) { partner = &Z[j]; break; }
    if (partner) { used[nu++] = z.s; used[nu++] = s.link; out[n].z = (s.type & 4) ? &z : partner; out[n].stereo = true; n++; }
    else { used[nu++] = z.s; out[n].z = &z; out[n].stereo = false; n++; }
    if (n >= 4) break;
  }
  return n;
}

const Preset* sf_preset(const Font& f, double bank, double prog) {
  const auto& P = f.presets;
  for (const Preset& p : P) if (p.bank == bank && p.prog == prog) return &p;
  const double b0 = bank >= 128 ? 128 : 0;
  for (const Preset& p : P) if (p.bank == b0 && p.prog == prog) return &p;
  for (const Preset& p : P) if (p.bank == b0) return &p;
  return P.empty() ? nullptr : &P[0];
}

}  // namespace sf
}  // namespace thunder
