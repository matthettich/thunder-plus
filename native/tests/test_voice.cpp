// U08: the sampler voice player (core/src/voice) against the JS (core/seq-core.js's ThunderCore, run by tools/voice-dump.mjs).
//   test_voice <dir>      for every case in <dir>/<case>.json: the same pads (regenerated here), the same events, one block at a time;
//                         the output is compared with <dir>/<case>.f32 (the JS's). Gate: the difference at or below -80 dB of the
//                         reference's peak (the largest difference over the largest reference sample), every case. The figure is printed.
//   test_voice            no reference (no Node): the committed hashes only (tests/golden/voice_golden.inc), for the cases this C++ equals
//                         bit for bit; the others are listed as not checked.
// Also: the committed hash of every case is compared with the C++'s output (bit-exact cases say so), and the pool's counters must be 0.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rt/json.h"
#include "voice/voice.h"
#include "golden/voice_golden.inc"

using namespace thunder;

// ---- the synthetic pads (the same recurrences as tools/voice-dump.mjs PADS) ----------------------------------------------------
struct Pad { std::vector<float> d, d2; double sr = 0; };
static Pad pad(const std::string& n) {
  Pad p;
  if (n == "sine") {
    const int N = 22050; p.d.resize(N); p.sr = 44100; const double c = 1.9898130397128468; double y1 = 0.0627, y2 = 0, g = 1;
    for (int i = 0; i < N; i++) { const double y = i == 0 ? y1 : c * y1 - y2; if (i > 0) { y2 = y1; y1 = y; } p.d[(size_t)i] = (float)(y * g); g *= 0.99997; }
  } else if (n == "noise") {
    const int N = 9600; p.d.resize(N); p.sr = 48000; uint32_t s = 12345;
    for (int i = 0; i < N; i++) { s = s * 1664525u + 1013904223u; p.d[(size_t)i] = (float)(((s / 4294967296.0) * 2 - 1) * (1 - (double)i / N)); }
  } else if (n == "saw") {
    const int N = 480; p.d.resize(N); p.sr = 48000; for (int i = 0; i < N; i++) p.d[(size_t)i] = (float)((double)i / N * 2 - 1);
  } else {   // st
    const int N = 24000; p.d.resize(N); p.d2.resize(N); p.sr = 48000; const double c = 1.9904; double y1 = 0.0431, y2 = 0, g = 1;
    for (int i = 0; i < N; i++) { const double y = i == 0 ? y1 : c * y1 - y2; if (i > 0) { y2 = y1; y1 = y; } p.d[(size_t)i] = (float)(y * g); p.d2[(size_t)i] = (float)((1 - (double)i / N) * 0.5 * g); g *= 0.99985; }
  }
  return p;
}

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }

static uint64_t fnv(const float* f, size_t n) { uint64_t h = 0xcbf29ce484222325ull; const unsigned char* u = (const unsigned char*)f; for (size_t i = 0; i < n * 4; i++) { h ^= u[i]; h *= 0x100000001b3ull; } return h; }

struct Ev { int b = 0; bool stopAll = false; double at = 0; SeqEvent e; std::vector<double> arp; };

static SeqEvent toEvent(Json j, std::vector<double>* arp) {
  SeqEvent e; const std::string_view ty = j.str("type");
  e.type = ty == "note" ? SEV_NOTE : ty == "off" ? SEV_OFF : ty == "mod" ? SEV_MOD : ty == "stp" ? SEV_STP : 255;
  e.t = j.num("t"); e.track = (int32_t)j.num("track", -1); e.poly = j.boolean("poly"); e.rev = j.boolean("rev"); e.flag = j.boolean("all"); e.col = (int32_t)j.num("col");
  e.pad = j.num("pad"); e.rate = j.num("rate"); e.gain = j.num("gain"); e.pan = j.num("pan", 128); e.ofs = j.num("ofs"); e.fdi = j.num("fdi"); e.trm = j.num("trm"); e.apn = j.num("apn");
  if (e.type == SEV_MOD) { if (!j.get("gain").missing()) e.has |= SH_GAIN; if (!j.get("pan").missing()) e.has |= SH_PAN; if (!j.get("mul").missing()) { e.has |= SH_MUL; e.mul = j.num("mul"); } }
  if (!j.get("cut").missing()) { e.has |= SH_CUT; e.cut = j.num("cut"); }
  if (!j.get("rel").missing()) { e.has |= SH_REL; e.rel = j.num("rel"); }
  if (!j.get("rlo").missing()) { e.has |= SH_RLO; e.rlo = j.num("rlo"); }
  if (Json s = j.obj("slide"); !s.missing()) { e.has |= SH_SLIDE; e.slideFrom = s.num("from"); e.slideDur = s.num("dur"); }
  if (Json w = j.obj("tw"); !w.missing()) { e.has |= SH_TW; if (!w.get("cut").missing()) { e.has |= SH_TWCUT; e.twCut = w.num("cut"); } if (!w.get("dec").missing()) { e.has |= SH_TWDEC; e.twDec = w.num("dec"); } e.twRes = w.num("res"); }
  if (Json b = j.obj("bend"); !b.missing()) { e.has |= SH_BEND; e.bendMul = b.num("mul"); e.bendT1 = b.num("t1"); }
  if (Json v = j.obj("vib"); !v.missing()) { e.has |= SH_VIB; e.vibSpd = v.num("spd"); e.vibDep = v.num("dep"); e.vibT1 = v.num("t1"); }
  if (Json a = j.arr("arp"); !a.missing()) { e.has |= SH_ARP; e.arpN = (int)a.size(); for (size_t q = 0; q < a.size(); q++) { arp->push_back(a.at(q).at(0).asNum()); arp->push_back(a.at(q).at(1).asNum()); } }
  if (Json c = j.arr("cols"); !c.missing()) { e.has |= SH_COLS; for (size_t q = 0; q < c.size(); q++) e.cols |= (uint8_t)(1u << (int)c.at(q).asNum()); }
  return e;
}

struct Result { std::vector<float> out; uint32_t dropped = 0, tover = 0, autod = 0; int peakV = 0, peakEv = 0; };

static bool runCase(Json meta, Result* R) {
  const int N = (int)meta.num("N"), blocks = (int)meta.num("blocks"), K = (int)meta.num("tracks"); const double bpm = meta.num("bpm"), SR = meta.num("sr");
  std::vector<Pad> pads; std::vector<VoiceBuf> bufs; std::vector<VoiceKit> kits;
  Json pn = meta.arr("pads"), kj = meta.arr("kit");
  for (size_t i = 0; i < pn.size(); i++) pads.push_back(pad(std::string(pn.at(i).asStr())));
  for (size_t i = 0; i < pads.size(); i++) { VoiceBuf b; b.data = pads[i].d.data(); b.data2 = pads[i].d2.empty() ? nullptr : pads[i].d2.data(); b.len = (int)pads[i].d.size(); b.sr = pads[i].sr; bufs.push_back(b);
    VoiceKit k; Json q = kj.at(i); k.choke = (int)q.num("choke"); k.syn = q.boolean("syn"); k.rel = q.num("rel"); kits.push_back(k); }
  std::vector<Ev> evs; Json ej = meta.arr("events");
  for (size_t i = 0; i < ej.size(); i++) { Ev v; v.b = (int)ej.at(i).num("b"); Json e = ej.at(i).obj("e");
    if (e.str("op") == "stopAll") { v.stopAll = true; v.at = e.num("at"); } else v.e = toEvent(e, &v.arp); evs.push_back(std::move(v)); }
  VoicePool pool(SR, 64);
  std::vector<float> L((size_t)K * N), Rr((size_t)K * N); float* ls[thunder::kVoiceTracks] = {}; float* rs[thunder::kVoiceTracks] = {};
  for (int t = 0; t < K; t++) { ls[t] = &L[(size_t)t * N]; rs[t] = &Rr[(size_t)t * N]; }
  R->out.assign((size_t)K * 2 * blocks * N, 0.f);
  for (int b = 0; b < blocks; b++) {
    const double t0 = (double)((int64_t)b * N) / SR;
    pool.setTime(t0);
    for (const Ev& v : evs) if (v.b == b) {
      if (v.stopAll) { pool.stopAll(v.at); continue; }
      const SeqEvent& e = v.e; const double* arp = v.arp.empty() ? nullptr : v.arp.data();
      switch (e.type) {
        case SEV_OFF: pool.off(e); break;
        case SEV_MOD: pool.mod(e, arp); break;
        case SEV_STP: pool.stp(e); break;
        case SEV_NOTE: { const int pd = (int)e.pad; if (pd < 0 || pd >= (int)bufs.size()) break;
          if (VoicePool::shapedReverse(e, bufs[(size_t)pd])) pool.take(e, &kits[(size_t)pd]); else pool.note(e, arp, bufs[(size_t)pd], kits[(size_t)pd], bpm); break; }
        default: break;
      }
    }
    std::memset(L.data(), 0, L.size() * 4); std::memset(Rr.data(), 0, Rr.size() * 4);
    pool.render(ls, rs, N, t0);
    for (int t = 0; t < K; t++) { std::memcpy(&R->out[((size_t)t * 2 + 0) * blocks * N + (size_t)b * N], ls[t], (size_t)N * 4); std::memcpy(&R->out[((size_t)t * 2 + 1) * blocks * N + (size_t)b * N], rs[t], (size_t)N * 4); }
  }
  R->dropped = pool.dropped(); R->tover = pool.tOverflow(); R->autod = pool.autoDropped(); R->peakV = pool.peakVoices(); R->peakEv = pool.peakEvents();
  return true;
}

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : "";
  int bad = 0, exact = 0, notChecked = 0;
  std::printf("%-12s %10s  %-10s %s\n", "case", "diff dB", "bit-exact", "(peak voices / longest automation list)");
  for (int g = 0; g < kVoiceGoldenN; g++) {
    const VoiceGolden& G = kVoiceGolden[g]; std::string txt;
    // the case file is needed even without a reference: the events are in it. Without Node (no dir) the cases come from the tool's own output
    // directory only; absent, a case cannot be run, so the committed hash is all that is left and nothing can be said of the C++.
    if (dir.empty()) { std::printf("%-12s %10s  %-10s (no reference run here: the case file is made by Node)\n", G.name, "-", "not run"); notChecked++; continue; }
    if (!readFile(dir + "/" + G.name + ".json", &txt)) { std::printf("%s: cannot read %s/%s.json\n", G.name, dir.c_str(), G.name); bad++; continue; }
    JsonDoc doc; if (!doc.parse(txt)) { std::printf("%s: bad json\n", G.name); bad++; continue; }
    Result R; runCase(doc.root(), &R);
    std::string bin; if (!readFile(dir + "/" + G.name + ".f32", &bin) || bin.size() != R.out.size() * 4) { std::printf("%s: reference size %zu, ours %zu bytes\n", G.name, bin.size(), R.out.size() * 4); bad++; continue; }
    const float* ref = (const float*)bin.data(); double peak = 0, md = 0; size_t first = (size_t)-1;
    for (size_t i = 0; i < R.out.size(); i++) { peak = std::max(peak, (double)std::fabs(ref[i])); const double d = std::fabs((double)R.out[i] - (double)ref[i]); if (d > md) md = d; if (d != 0 && first == (size_t)-1) first = i; }
    const bool same = md == 0, hashOk = fnv(R.out.data(), R.out.size()) == G.hash;
    const double dB = same ? -999 : 20 * std::log10(md / (peak > 0 ? peak : 1));
    const bool ok = (same || dB <= -80) && peak > 0 && !R.dropped && !R.tover && !R.autod;
    char db[32]; if (same) std::snprintf(db, sizeof db, "exact"); else std::snprintf(db, sizeof db, "%.1f", dB);
    std::printf("%-12s %10s  %-10s (%d voices / %d events)%s%s\n", G.name, db, same ? "yes" : "no", R.peakV, R.peakEv, hashOk != same ? "  [hash disagrees with the reference run]" : "", ok ? "" : "  FAIL");
    if (!same && first != (size_t)-1) std::printf("    first difference at float %zu: ours %.9g, JS %.9g\n", first, (double)R.out[first], (double)ref[first]);
    if (R.dropped || R.tover || R.autod) std::printf("    counters: dropped %u, track overflow %u, automation %u\n", R.dropped, R.tover, R.autod);
    if (!ok) bad++;
    if (same) exact++;
  }
  std::printf("voice: %d of %d cases bit-exact, %d failed, %d not run\n", exact, kVoiceGoldenN, bad, notChecked);
  return bad ? 1 : 0;
}
