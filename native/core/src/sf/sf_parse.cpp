// U32: parseSF (index.html), line for line, with the validation the JS gets for free from its runtime (a DataView read past the file throws; an out-of-range slice is empty; numbers do not overflow).
#include <algorithm>
#include <cstring>

#include "sf/sf.h"

namespace thunder {
namespace sf {
namespace {

struct Chunk { uint64_t o = 0, n = 0; bool has = false; };
enum { C_phdr, C_pbag, C_pgen, C_pmod, C_inst, C_ibag, C_igen, C_imod, C_shdr, C_smpl, C_INAM, C_N };
const char* const kIds[C_N] = {"phdr", "pbag", "pgen", "pmod", "inst", "ibag", "igen", "imod", "shdr", "smpl", "INAM"};

struct Rd {
  const uint8_t* b; size_t n; bool bad = false;
  uint32_t u16(uint64_t o) { if (o + 2 > n) { bad = true; return 0; } return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8); }
  uint32_t u32(uint64_t o) { if (o + 4 > n) { bad = true; return 0; } return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) | ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24); }
  int16_t i16(uint64_t o) { return (int16_t)(uint16_t)u16(o); }
  uint8_t u8(uint64_t o) { if (o >= n) { bad = true; return 0; } return b[o]; }
};

// str(o, n): latin1 up to a NUL (or the end of the file: `u8[e]` is undefined there), trimmed of white space
void str(const uint8_t* b, size_t len, uint64_t o, uint64_t n, char* out21) {
  uint64_t e = o; while (e < o + n && e < len && b[e]) e++;
  uint64_t s = o; uint64_t t = e < len ? e : len; if (s > t) s = t;
  auto ws = [](uint8_t c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f' || c == 0xA0; };
  while (s < t && ws(b[s])) s++;
  while (t > s && ws(b[t - 1])) t--;
  size_t k = 0; for (uint64_t i = s; i < t && k < 20; i++) out21[k++] = (char)b[i];
  out21[k] = 0;
}

struct Gen { uint16_t op; int16_t v; uint8_t lo, hi; uint16_t u; };
struct Bag { uint16_t g, m; };
struct Zl { uint32_t g0, g1, m0, m1; uint16_t t; };           // a zone's generator / modulator ranges and its terminal generator's value
struct ZSet { uint32_t gg0 = 0, gg1 = 0, gm0 = 0, gm1 = 0; std::vector<Zl> out; };   // glob (generators [gg0, gg1)), gm (modulators), the zones

// slice(a, b) of an array of `len`: the JS clamps both ends and an end at or before the start is empty
inline void slice(size_t len, uint32_t a, uint64_t b, uint32_t& s, uint32_t& e) { s = (uint32_t)std::min<uint64_t>(a, len); e = (uint32_t)std::min<uint64_t>(b, len); if (e < s) e = s; }

// zones(bags, gens, mods, from, to, term)
void zonesOf(const std::vector<Bag>& bags, const std::vector<Gen>& gens, const std::vector<Mod>& mods, uint32_t from, uint32_t to, uint16_t term, ZSet& r) {
  bool haveGlob = false;
  for (uint64_t b = from; b < to && b < bags.size(); b++) {
    const uint64_t g1 = b + 1 < bags.size() ? bags[b + 1].g : gens.size(), m1 = b + 1 < bags.size() ? bags[b + 1].m : mods.size();
    uint32_t gs, ge, ms, me; slice(gens.size(), bags[b].g, g1, gs, ge); slice(mods.size(), bags[b].m, m1, ms, me);
    int tv = -1; for (uint32_t i = gs; i < ge; i++) if (gens[i].op == term) { tv = gens[i].u; break; }
    if (tv >= 0) r.out.push_back(Zl{gs, ge, ms, me, (uint16_t)tv});
    else if (b == from && !haveGlob) { r.gg0 = gs; r.gg1 = ge; r.gm0 = ms; r.gm1 = me; haveGlob = true; }
  }
}

bool same(const Mod& a, const Mod& b) { return a.src == b.src && a.dst == b.dst && a.as == b.as && a.tr == b.tr; }
void mergeInto(std::vector<Mod>& out, const Mod* over, size_t n) {   // merge(base, over): base is `out`
  for (size_t k = 0; k < n; k++) {
    size_t i = 0; for (; i < out.size(); i++) if (same(out[i], over[k])) break;
    if (i < out.size()) out[i] = over[k]; else out.push_back(over[k]);
  }
}

bool isAddr(int op) { return op == 0 || op == 1 || op == 2 || op == 3 || op == 4 || op == 12 || op == 45 || op == 50; }
bool isAbs(int op) { return op == 41 || op == 43 || op == 44 || op == 46 || op == 47 || op == 53 || op == 54 || op == 57 || op == 58; }

}  // namespace

double Font::bytesUsed() const {
  return (double)(samples.capacity() * sizeof(SampleHdr) + presets.capacity() * sizeof(Preset) + zones.capacity() * sizeof(Zone) + gens.capacity() * sizeof(GenV) + mods.capacity() * sizeof(Mod) + own.capacity());
}

ParseStatus parse(const uint8_t* data, size_t n, Font& F, bool copy) {
  F = Font();
  if (n > kMaxFontBytes) return kParseTooBig;
  if (copy) { F.own.assign(data, data + n); data = F.own.data(); }
  Rd rd{data, n};
  auto id4 = [&](uint64_t o) { return std::string((const char*)data + o, 4); };
  if (n < 12 || id4(0) != "RIFF" || id4(8) != "sfbk") return kParseNotSf;
  Chunk ch[C_N];
  // walk(o, end): a LIST is entered (its 4-byte type skipped), every other chunk is recorded; a later chunk of the same id replaces an earlier one
  struct Walker { const uint8_t* b; Chunk* ch; bool tooDeep = false;
    void walk(uint64_t o, uint64_t end, int depth) {
      while (o + 8 <= end) {
        const std::string k((const char*)b + o, 4);
        const uint64_t sz = (uint64_t)b[o + 4] | ((uint64_t)b[o + 5] << 8) | ((uint64_t)b[o + 6] << 16) | ((uint64_t)b[o + 7] << 24);
        if (k == "LIST") { if (depth >= kMaxListDepth) { tooDeep = true; return; } walk(o + 12, std::min(end, o + 8 + sz), depth + 1); if (tooDeep) return; }
        else for (int i = 0; i < C_N; i++) if (k == kIds[i]) { ch[i].o = o + 8; ch[i].n = sz; ch[i].has = true; }
        o += 8 + sz + (sz & 1);
      } } } W{data, ch};
  W.walk(12, std::min<uint64_t>(n, 8 + (uint64_t)rd.u32(4)), 0);
  if (W.tooDeep) return kParseTooBig;
  for (int i : {C_phdr, C_pbag, C_pgen, C_inst, C_ibag, C_igen, C_shdr, C_smpl}) if (!ch[i].has) return kParseIncomplete;

  // rec(k, sz, f): every whole record the chunk's length claims; a record read past the file throws in the JS
  auto records = [&](int k, size_t sz, auto&& f) {
    const Chunk& c = ch[k];
    if (!c.has) return;
    for (uint64_t o = c.o; o + sz <= c.o + c.n; o += sz) { if (o + sz > n) { rd.bad = true; return; } f(o); }
  };
  struct Ph { char name[21]; int prog, bank; uint32_t bag; };
  std::vector<Ph> phdr; std::vector<Bag> pbag, ibag; std::vector<Gen> pgen, igen; std::vector<Mod> pmod, imod; struct In { uint32_t bag; }; std::vector<In> inst;
  records(C_phdr, 38, [&](uint64_t o) { Ph p; str(data, n, o, 20, p.name); p.prog = (int)rd.u16(o + 20); p.bank = (int)rd.u16(o + 22); p.bag = rd.u16(o + 24); phdr.push_back(p); });
  auto bagf = [&](std::vector<Bag>& v) { return [&v, &rd](uint64_t o) { v.push_back(Bag{(uint16_t)rd.u16(o), (uint16_t)rd.u16(o + 2)}); }; };
  records(C_pbag, 4, bagf(pbag)); records(C_ibag, 4, bagf(ibag));
  auto genf = [&](std::vector<Gen>& v) { return [&v, &rd](uint64_t o) { v.push_back(Gen{(uint16_t)rd.u16(o), rd.i16(o + 2), rd.u8(o + 2), rd.u8(o + 3), (uint16_t)rd.u16(o + 2)}); }; };
  records(C_pgen, 4, genf(pgen)); records(C_igen, 4, genf(igen));
  auto modf = [&](std::vector<Mod>& v) { return [&v, &rd](uint64_t o) { v.push_back(Mod{(uint16_t)rd.u16(o), (uint16_t)rd.u16(o + 2), rd.i16(o + 4), (uint16_t)rd.u16(o + 6), (uint16_t)rd.u16(o + 8)}); }; };
  records(C_pmod, 10, modf(pmod)); records(C_imod, 10, modf(imod));
  records(C_inst, 22, [&](uint64_t o) { inst.push_back(In{rd.u16(o + 20)}); });   // (the instrument's name is never read)
  records(C_shdr, 46, [&](uint64_t o) { SampleHdr s; str(data, n, o, 20, s.name); s.start = rd.u32(o + 20); s.end = rd.u32(o + 24); s.ls = rd.u32(o + 28); s.le = rd.u32(o + 32);
    s.sr = rd.u32(o + 36) ? rd.u32(o + 36) : 44100; s.root = rd.u8(o + 40); s.corr = (int8_t)rd.u8(o + 41); s.link = (uint16_t)rd.u16(o + 42); s.type = (uint16_t)rd.u16(o + 44); F.samples.push_back(s); });
  if (rd.bad) return kParseTruncated;
  if (!F.samples.empty()) F.samples.pop_back();   // the terminal EOS record
  { const uint64_t o = std::min<uint64_t>(ch[C_smpl].o, n), e = std::min<uint64_t>(ch[C_smpl].o + ch[C_smpl].n, n); F.smpl = data + o; F.smplN = (size_t)(e - o); }
  { char nm[21] = "SoundFont"; if (ch[C_INAM].has) str(data, n, ch[C_INAM].o, ch[C_INAM].n, nm); F.name = nm; }
  for (const SampleHdr& s : F.samples) if (s.type & 0x10) F.sf3 = true;

  // the zones of every instrument, then of every preset with its instrument's zones under it
  const std::vector<Mod> defmods = {Mod{0x0502, 48, 960, 0, 0}, Mod{0x0102, 8, -2400, 0x0D02, 0}};
  std::vector<ZSet> instZones; if (inst.size() > 1) instZones.resize(inst.size() - 1);
  for (size_t i = 0; i + 1 < inst.size(); i++) zonesOf(ibag, igen, imod, inst[i].bag, inst[i + 1].bag, 53, instZones[i]);
  struct TmpP { Preset p; };
  std::vector<Preset> presets;
  size_t work = 0;
  for (size_t p = 0; p + 1 < phdr.size(); p++) {
    ZSet pz; zonesOf(pbag, pgen, pmod, phdr[p].bag, phdr[p + 1].bag, 41, pz);
    Preset P; std::memcpy(P.name, phdr[p].name, 21); P.bank = phdr[p].bank; P.prog = phdr[p].prog; P.z0 = (uint32_t)F.zones.size(); P.nz = 0;
    for (const Zl& z : pz.out) {
      if (z.t >= instZones.size()) continue;
      const ZSet& iz = instZones[z.t];
      int pk0 = 0, pk1 = 127, pv0 = 0, pv1 = 127; int32_t padd[kGenN]; uint64_t pset = 0;
      auto preGen = [&](const Gen& g) {
        if (g.op == 43) { pk0 = g.lo; pk1 = g.hi; } else if (g.op == 44) { pv0 = g.lo; pv1 = g.hi; }
        else if (!isAbs(g.op) && !isAddr(g.op) && g.op < kGenN) { padd[g.op] = g.v; pset |= 1ull << g.op; } };
      for (uint32_t i = pz.gg0; i < pz.gg1; i++) preGen(pgen[i]);
      for (uint32_t i = z.g0; i < z.g1; i++) preGen(pgen[i]);
      std::vector<Mod> pm(pmod.begin() + pz.gm0, pmod.begin() + pz.gm1); mergeInto(pm, pmod.data() + z.m0, z.m1 - z.m0);
      for (const Zl& lz : iz.out) {
        if (++work > kMaxWork) return kParseTooBig;
        int k0 = 0, k1 = 127, v0 = 0, v1 = 127; int32_t gv[kGenN]; uint64_t gset = 0;
        auto inGen = [&](const Gen& x) {
          if (x.op == 43) { k0 = x.lo; k1 = x.hi; } else if (x.op == 44) { v0 = x.lo; v1 = x.hi; }
          else if (x.op < kGenN) { gv[x.op] = x.op == 53 ? (int32_t)x.u : (int32_t)x.v; gset |= 1ull << x.op; } };
        for (uint32_t i = iz.gg0; i < iz.gg1; i++) inGen(igen[i]);
        for (uint32_t i = lz.g0; i < lz.g1; i++) inGen(igen[i]);
        for (int op = 0; op < kGenN; op++) if (pset >> op & 1) { gv[op] = ((gset >> op & 1) ? gv[op] : 0) + padd[op]; gset |= 1ull << op; }   // g[op] = (g[op] || 0) + padd[op]
        const int klo = std::max(k0, pk0), khi = std::min(k1, pk1), vlo = std::max(v0, pv0), vhi = std::min(v1, pv1);
        if (klo > khi || vlo > vhi || lz.t >= F.samples.size()) continue;
        if (F.zones.size() >= kMaxZones) return kParseTooBig;
        std::vector<Mod> im = defmods; mergeInto(im, imod.data() + iz.gm0, iz.gm1 - iz.gm0); mergeInto(im, imod.data() + lz.m0, lz.m1 - lz.m0);
        if (F.mods.size() + im.size() + pm.size() > kMaxModPool) return kParseTooBig;
        { int ng = 0; for (int op = 0; op < kGenN; op++) ng += (int)(gset >> op & 1);   // the tables' own bytes after this zone: bounded by the font's size (kParseTableFactor), not only by counts
          const uint64_t tb = ((uint64_t)F.zones.size() + 1) * sizeof(Zone) + ((uint64_t)F.gens.size() + (uint64_t)ng) * sizeof(GenV) + ((uint64_t)F.mods.size() + im.size() + pm.size()) * sizeof(Mod);
          if (tb > (uint64_t)kParseTableFloor + (uint64_t)kParseTableFactor * n) return kParseTooBig; }
        Zone Z; Z.klo = klo; Z.khi = khi; Z.vlo = vlo; Z.vhi = vhi; Z.s = lz.t;
        Z.gOff = (uint32_t)F.gens.size(); Z.gN = 0; for (int op = 0; op < kGenN; op++) if (gset >> op & 1) { F.gens.push_back(GenV{(uint8_t)op, gv[op]}); Z.gN++; }
        Z.imOff = (uint32_t)F.mods.size(); Z.imN = (uint32_t)im.size(); F.mods.insert(F.mods.end(), im.begin(), im.end());
        Z.pmOff = (uint32_t)F.mods.size(); Z.pmN = (uint32_t)pm.size(); F.mods.insert(F.mods.end(), pm.begin(), pm.end());
        F.zones.push_back(Z); P.nz++;
      }
    }
    presets.push_back(P);
  }
  F.work = work;
  // presets.sort((a, b) => a.bank - b.bank || a.prog - b.prog): stable
  std::stable_sort(presets.begin(), presets.end(), [](const Preset& a, const Preset& b) { return a.bank != b.bank ? a.bank < b.bank : a.prog < b.prog; });
  F.presets = std::move(presets);
  F.zones.shrink_to_fit(); F.gens.shrink_to_fit(); F.mods.shrink_to_fit();
  return kParseOk;
}

}  // namespace sf
}  // namespace thunder
