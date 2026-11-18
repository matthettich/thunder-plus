// U32: the fuzz of the SoundFont player (run under ASan + UBSan with -DTHUNDER_SANITIZE=ON; a small count in ctest, a long run is `test_sf_fuzz ITER SEED`).
//   1. the SF2 / SF3 parser: the test font and the GM font, truncated, with bytes flipped, with 32-bit fields set to extremes (chunk sizes, counts, offsets), with blocks swapped; whatever parses is then used (pick, voice, preset
//      preparation, render, live note), so an inconsistent font that gets through the load checks is exercised, not just parsed.
//   2. stb_vorbis (the SF3 decoder): real Ogg streams of the GM font, corrupted (flips, zeroed spans, truncation, swapped blocks, the setup header attacked on purpose), decoded through decode_sample.
//   3. the voice: a font built directly with several extreme generator values, modulators and sample fields AT ONCE, played by the live pool (extreme rate, gain, pan, bend, vibrato, cut, release) and by the pad render.
// The test passes if nothing faults (the sanitizers fail the run) and every call returns; it also checks the invariants the live pool promises (it never writes outside the block: guard floats around every block).
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "sf/sf.h"
#include "sf/sf_live.h"
#include "voice/voice.h"

#ifndef THUNDER_ROOT
#error THUNDER_ROOT
#endif
using namespace thunder;
using namespace thunder::sf;

static int fails = 0;
#define CHECK(cond, ...) do { if (!(cond)) { fails++; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

struct Rng { uint64_t s; explicit Rng(uint64_t x) : s(x * 2654435761u + 88172645463325252ull) {} uint64_t next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return s; } uint32_t u(uint32_t n) { return n ? (uint32_t)(next() % n) : 0; } double f() { return (double)(next() >> 11) / 9007199254740992.0; } };
static bool readFile(const std::string& p, std::vector<uint8_t>* out) { std::ifstream f(p, std::ios::binary); if (!f) return false; out->assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()); return true; }
static const double kExtreme[] = {0, -0.0, 1, -1, 1e-300, -1e-300, 1e300, -1e300, 1e9, -1e9, 4294967296.0, 2147483648.0, -2147483649.0, 0.5, 127, 128, 255, 32767, -32768, 65535, 100000, std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN(), 1e-9, 12345.678};
static double xt(Rng& r) { return r.u(3) ? kExtreme[r.u((uint32_t)(sizeof kExtreme / sizeof kExtreme[0]))] : (r.f() - 0.5) * 2000; }

// What a parsed font is used for: everything the host does with it.
static void useFont(const Font& f, Rng& r, bool decode, uint64_t* checksum) {
  for (const Preset& p : f.presets) {
    for (int k = 0; k < 4; k++) {
      const int key = (int)r.u(128), vel = 1 + (int)r.u(127); Pick pk[4]; const int n = sf_pick(f, &p, key, vel, pk);
      for (int i = 0; i < n; i++) { VoiceParams vp; sf_voice(f, *pk[i].z, key, vel, vp); *checksum += (uint64_t)(std::isfinite(vp.scale) ? 1 : 2); }
    }
    if (f.presets.size() > 40 && &p != &f.presets[0] && &p != &f.presets.back()) continue;   // a big font: a few presets
    if (!decode) continue;
    PadParams P; P.bank = p.bank; P.prog = p.prog; P.trans = (double)((int)r.u(25) - 12);
    const size_t bound = sf_render_bound(f, P, 48000, 0.2);
    if (bound == 0 || bound > ((size_t)1 << 22)) continue;
    DecodeOpts o; o.pageTail = r.u(2) == 0; uint8_t keys[128]; for (int k = 0; k < 128; k++) keys[k] = (uint8_t)(r.u(3) == 0); PresetPcm pcm; prepare_preset(f, &p, o, pcm, r.u(2) ? keys : nullptr);
    std::vector<float> out(bound + 8, 12345.f); const size_t n = sf_render(f, pcm, P, 48000, 0.2, out.data(), bound);
    CHECK(n <= bound, "render wrote %zu of %zu", n, bound);
    for (size_t i = bound; i < bound + 8; i++) CHECK(out[i] == 12345.f, "render wrote past its buffer");
    SfPool pool(48000, 4); SfPad pad; pad.font = &f; pad.pr = &p; pad.pcm = &pcm; pad.P = P; VoiceKit kv; kv.syn = true;
    SeqEvent e; e.type = SEV_NOTE; e.t = 0; e.track = 0; e.rate = 1 + r.u(8); e.gain = 0.8; e.pan = 128; std::vector<double> arp; pool.setFrame(0); pool.note(e, arp.data(), pad, kv);
    std::vector<float> L(260, 0.f), R(260, 0.f); float* lp[kVoiceTracks] = {}; float* rp[kVoiceTracks] = {}; lp[0] = L.data() + 2; rp[0] = R.data() + 2; L[0] = L[1] = L[258] = L[259] = 777.f; R[0] = R[1] = R[258] = R[259] = 777.f;
    pool.render(lp, rp, 256, 0); CHECK(L[0] == 777.f && L[1] == 777.f && L[258] == 777.f && L[259] == 777.f && R[0] == 777.f && R[259] == 777.f, "live render wrote outside its block");
  }
}

static void mutate(std::vector<uint8_t>& b, Rng& r, size_t hot0) {   // hot0: where the interesting part starts (a font's pdta, an Ogg's headers)
  if (b.size() < 8) return;
  auto at = [&]() -> size_t { return r.u(3) ? std::min(b.size() - 1, hot0 + r.u((uint32_t)std::max<size_t>(1, b.size() - hot0))) : r.u((uint32_t)b.size()); };
  const int n = 1 + (int)r.u(3);
  for (int m = 0; m < n; m++) {
    switch (r.u(7)) {
      case 0: b.resize(1 + r.u((uint32_t)b.size())); break;                                                    // truncate
      case 1: for (int i = 0, k = 1 + (int)r.u(8); i < k; i++) b[at()] ^= (uint8_t)(1u << r.u(8)); break;       // flip bits
      case 2: { static const uint32_t X[] = {0, 1, 0xffffffffu, 0x7fffffffu, 0x80000000u, 0xfffffffeu, 0x10000u, 0x01000000u, 0x00ffffffu}; size_t o = at() & ~(size_t)3; if (o + 4 <= b.size()) { const uint32_t v = r.u(4) ? X[r.u(9)] : (uint32_t)r.next(); std::memcpy(&b[o], &v, 4); } break; }   // a 32-bit field
      case 3: { size_t o = at() & ~(size_t)1; if (o + 2 <= b.size()) { const uint16_t v = r.u(2) ? (uint16_t)(r.u(2) ? 0xffff : 0) : (uint16_t)r.next(); std::memcpy(&b[o], &v, 2); } break; }   // a 16-bit field
      case 4: { const size_t len = 1 + r.u(64), o = at(); for (size_t i = 0; i < len && o + i < b.size(); i++) b[o + i] = 0; break; }   // zero a span
      case 5: { const size_t len = 4 + r.u(48), o1 = at(), o2 = at(); if (o1 + len <= b.size() && o2 + len <= b.size()) for (size_t i = 0; i < len; i++) std::swap(b[o1 + i], b[o2 + i]); break; }   // swap two blocks
      default: { const size_t len = 1 + r.u(256), o = at(); for (size_t i = 0; i < len && o + i < b.size(); i++) b[o + i] = (uint8_t)r.next(); break; }   // noise
    }
  }
}

// 1. the parser
static void fuzzParse(const std::vector<uint8_t>& base, size_t hot0, int iters, Rng& r, bool decode, const char* what) {
  int ok = 0, bad = 0; uint64_t cs = 0;
  for (int it = 0; it < iters; it++) {
    std::vector<uint8_t> b = base; mutate(b, r, hot0);
    Font f; const ParseStatus st = parse(b.data(), b.size(), f, false);
    if (st != kParseOk) { bad++; continue; }
    ok++; useFont(f, r, decode, &cs);
  }
  std::printf("parser fuzz (%s): %d mutated files, %d refused, %d parsed and used\n", what, iters, bad, ok);
}

// 2. stb_vorbis, through decode_sample, on corrupted copies of real streams
static void fuzzOgg(const Font& gm, const std::vector<uint32_t>& idx, int iters, Rng& r) {
  int decoded = 0, failed = 0; size_t frames = 0;
  for (int it = 0; it < iters; it++) {
    const SampleHdr& s = gm.samples[idx[r.u((uint32_t)idx.size())]]; std::vector<uint8_t> b(gm.smpl + s.start, gm.smpl + s.end);
    const size_t hot = r.u(2) ? 0 : std::min<size_t>(b.size() / 2, 400); mutate(b, r, hot);
    if (r.u(4) == 0) for (int k = 0, n = 1 + (int)r.u(6); k < n; k++) b[r.u((uint32_t)std::min<size_t>(b.size(), 400))] = (uint8_t)r.next();   // the headers
    Font g; g.sf3 = true; g.smpl = b.data(); g.smplN = b.size(); SampleHdr h = s; h.start = 0; h.end = (uint32_t)b.size(); g.samples.push_back(h);
    Pcm p; DecodeOpts o; if (r.u(3) == 0) o.ctxRate = 48000; o.pageTail = r.u(2) == 0;
    decode_sample(g, 0, o, p); if (p.ok) { decoded++; frames += p.d.size(); } else failed++;
    for (float v : p.d) if (!std::isfinite(v)) { /* a corrupt stream may decode to garbage; infinities would be a defect in the decode path */ CHECK(false, "non-finite decoded sample"); break; }
  }
  std::printf("stb_vorbis fuzz: %d corrupted streams, %d decoded (%zu frames), %d refused\n", iters, decoded, frames, failed);
}

// 3. the voice: a font made directly, with extreme values together
static void fuzzVoice(int iters, Rng& r) {
  int played = 0; size_t blocks = 0;
  for (int it = 0; it < iters; it++) {
    Font f; f.sf3 = false; std::vector<uint8_t> smpl(2 * 400); for (auto& v : smpl) v = (uint8_t)r.next(); f.smpl = smpl.data(); f.smplN = smpl.size();
    const int ns = 1 + (int)r.u(2);
    for (int i = 0; i < ns; i++) { SampleHdr h; std::memset(&h, 0, sizeof h); const uint32_t E[] = {0, 1, 2, 399, 400, 401, 0x7fffffffu, 0xffffffffu, 0x80000000u};
      h.start = r.u(2) ? r.u(500) : E[r.u(9)]; h.end = r.u(2) ? r.u(500) : E[r.u(9)]; h.ls = r.u(2) ? r.u(500) : E[r.u(9)]; h.le = r.u(2) ? r.u(500) : E[r.u(9)]; h.sr = r.u(4) ? 44100 : E[r.u(9)] ? E[r.u(9)] : 44100;
      h.root = (uint8_t)(r.u(3) ? 60 : r.u(256)); h.corr = (int8_t)(r.u(3) ? 0 : (int)r.u(256) - 128); h.type = (uint16_t)(r.u(3) ? 1 : r.u(0x20)); h.link = (uint16_t)r.u(4); f.samples.push_back(h); }
    const int nz = 1 + (int)r.u(5); Preset pr; std::memset(&pr, 0, sizeof pr); pr.z0 = 0; pr.nz = (uint32_t)nz;
    for (int z = 0; z < nz; z++) {
      Zone zn; std::memset(&zn, 0, sizeof zn); zn.klo = (int)r.u(3) ? 0 : (int)r.u(128); zn.khi = r.u(3) ? 127 : (int)r.u(128); zn.vlo = 0; zn.vhi = 127; zn.s = r.u((uint32_t)ns);
      zn.gOff = (uint32_t)f.gens.size(); int last = -1;
      for (int op = 0; op < kGenN; op++) { if (!r.u(3)) continue; if (op <= last) continue; last = op; GenV g; g.op = (uint8_t)op; const double x = xt(r); g.v = r.u(2) ? (int32_t)((int)r.u(65536) - 32768) : (std::isfinite(x) ? (int32_t)std::max(-2147483648.0, std::min(2147483647.0, x)) : 0); f.gens.push_back(g); zn.gN++; }
      zn.imOff = (uint32_t)f.mods.size(); const int nm = (int)r.u(5); for (int m = 0; m < nm; m++) { Mod md; md.src = (uint16_t)r.next(); md.dst = (uint16_t)(r.u(2) ? r.u(64) : r.next()); md.amt = (int16_t)r.next(); md.as = (uint16_t)r.next(); md.tr = (uint16_t)r.next(); f.mods.push_back(md); }
      zn.imN = (uint32_t)nm; zn.pmOff = (uint32_t)f.mods.size(); zn.pmN = 0; f.zones.push_back(zn);
    }
    f.presets.push_back(pr);
    PadParams P; P.trans = xt(r); P.velo = xt(r); P.atk = xt(r); P.rel = xt(r); P.level = xt(r); P.len = xt(r); const double sr = r.u(4) ? 48000 : std::fabs(xt(r)), hold = r.u(2) ? 0.1 : xt(r);
    const size_t bound = sf_render_bound(f, P, sr, hold); DecodeOpts o; PresetPcm pcm; prepare_preset(f, &f.presets[0], o, pcm);
    if (bound > 0 && bound <= ((size_t)1 << 21)) { std::vector<float> out(bound + 4, 9.f); sf_render(f, pcm, P, sr, hold, out.data(), bound); for (size_t i = bound; i < bound + 4; i++) CHECK(out[i] == 9.f, "render past its end"); }
    SfPool pool(r.u(8) ? 48000 : std::max(8000.0, std::min(192000.0, std::fabs(xt(r)))), 4); SfPad pad; pad.font = &f; pad.pr = &f.presets[0]; pad.pcm = &pcm; pad.P = P; VoiceKit kv; kv.syn = true; kv.choke = (int)r.u(3);
    std::vector<double> arp = {0.01, 1.0, 0.03, 2.0};
    for (int e = 0; e < 4; e++) {
      SeqEvent ev; ev.type = (uint8_t)(e == 0 || r.u(2) ? SEV_NOTE : r.u(3) ? SEV_OFF : SEV_MOD); ev.t = r.u(2) ? 0.001 * r.u(50) : xt(r); ev.track = (int)r.u(kVoiceTracks); ev.poly = r.u(2); ev.col = (int)r.u(4);
      ev.rate = xt(r); ev.gain = xt(r); ev.pan = r.u(2) ? 128 : xt(r);
      if (r.u(2)) { ev.has |= SH_SLIDE; ev.slideFrom = xt(r); ev.slideDur = xt(r); } if (r.u(2)) { ev.has |= SH_BEND; ev.bendMul = xt(r); ev.bendT1 = xt(r); } if (r.u(2)) { ev.has |= SH_VIB; ev.vibSpd = xt(r); ev.vibDep = xt(r); ev.vibT1 = xt(r); }
      if (r.u(2)) { ev.has |= SH_CUT; ev.cut = xt(r); } if (r.u(2)) { ev.has |= SH_REL; ev.rel = xt(r); } if (r.u(3) == 0) { ev.has |= SH_ARP; ev.arpN = 2; }
      if (ev.type == SEV_MOD) { if (r.u(2)) { ev.has |= SH_GAIN; ev.gain = xt(r); } if (r.u(2)) { ev.has |= SH_PAN; ev.pan = xt(r); } if (r.u(2)) { ev.has |= SH_MUL; ev.mul = xt(r); } }
      pool.setFrame(0); if (ev.type == SEV_NOTE) played += pool.note(ev, arp.data(), pad, kv) ? 1 : 0; else if (ev.type == SEV_OFF) pool.off(ev); else pool.mod(ev, arp.data());
      if (r.u(6) == 0) { SeqEvent t2 = ev; t2.type = SEV_STP; pool.stp(t2); }
    }
    uint64_t frame = 0; const int pats[] = {128, 17, 1, 64, 300};
    for (int b = 0; b < 6; b++) { const int n = pats[r.u(5)]; std::vector<float> L((size_t)n + 4, 5.f), R((size_t)n + 4, 5.f); float* lp[kVoiceTracks] = {}; float* rp[kVoiceTracks] = {};
      for (int t = 0; t < kVoiceTracks; t++) if (r.u(4)) { lp[t] = L.data() + 2; rp[t] = r.u(5) ? R.data() + 2 : nullptr; }
      pool.render(lp, rp, n, frame); CHECK(L[0] == 5.f && L[1] == 5.f && L[(size_t)n + 2] == 5.f && L[(size_t)n + 3] == 5.f && R[(size_t)n + 2] == 5.f, "live render wrote outside its block"); frame += (uint64_t)n; blocks++;
      if (r.u(9) == 0) pool.stopAll((double)frame / 48000.0); }
  }
  std::printf("voice fuzz: %d fonts with extreme fields, %d notes started, %zu blocks rendered\n", iters, played, blocks);
}

int main(int argc, char** argv) {
  const int iters = argc > 1 ? std::atoi(argv[1]) : 400; const uint64_t seed = argc > 2 ? (uint64_t)std::atoll(argv[2]) : 1; Rng r(seed);
  std::vector<uint8_t> syn, gm; const std::string root = THUNDER_ROOT;
  CHECK(readFile(root + "/native/tests/golden/sf_test.sf2", &syn), "no test font"); CHECK(readFile(root + "/sounds/FluidR3Mono_GM.sf3", &gm), "no GM font");
  if (syn.empty() || gm.empty()) return 1;
  fuzzParse(syn, 0, iters * 4, r, true, "test font, SF2");
  fuzzParse(syn, 0, iters, r, true, "test font, SF2, again");
  { // the GM font: the pdta lives near the end; mutate there mostly; decoding only for the small mutated copies
    Font f; CHECK(parse(gm.data(), gm.size(), f) == kParseOk, "GM font parses"); size_t hot = gm.size() > 600000 ? gm.size() - 600000 : 0; fuzzParse(gm, hot, std::max(8, iters / 8), r, false, "GM font, SF3");
    std::vector<uint32_t> idx; for (uint32_t i : {183u, 948u, 906u, 859u, 1075u, 1043u, 0u, 730u}) if (i < f.samples.size()) idx.push_back(i);
    fuzzOgg(f, idx, iters * 10, r);
  }
  fuzzVoice(iters, r);
  std::printf(fails ? "FAIL: %d\n" : "OK\n", fails);
  return fails ? 1 : 0;
}
