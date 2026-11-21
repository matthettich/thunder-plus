// A sequencer event as tools/seq-golden.mjs compares it (U07): core/seq-core.js's corePlain (keys sorted, numbers rounded to
// 1e-6, nested objects dropped, arrays of numbers kept) written as JSON.stringify writes it.
// With a sequencer given (`full`), what corePlain drops is written too (the eng event's c and o, tw, slide, bend, vib, arp), as
// seq-dump.mjs's "full" cases write them.
#ifndef THUNDER_TESTS_SEQ_PLAIN_H
#define THUNDER_TESTS_SEQ_PLAIN_H
#include <cmath>
#include <string>

#include "rt/jsmath.h"
#include "seq/seq.h"

namespace seqplain {
inline void num(std::string& o, double v) {   // coreNum, then JSON.stringify
  v = thunder::js_round(v * 1e6) / 1e6;
  if (!(v == v) || std::isinf(v)) { o += "null"; return; }
  char b[32]; o.append(b, (size_t)thunder::js_number_to_string(v, b));
}
struct W {
  std::string& o; bool first = true;
  explicit W(std::string& out) : o(out) { o += '{'; }
  void key(const char* k) { if (!first) o += ','; first = false; o += '"'; o += k; o += "\":"; }
  void n(const char* k, double v) { key(k); num(o, v); }
  void b(const char* k, bool v) { key(k); o += v ? "true" : "false"; }
  void s(const char* k, const std::string& v) { key(k); o += '"'; o += v; o += '"'; }
  void nul(const char* k) { key(k); o += "null"; }
  void opt(const char* k, bool has, double v) { if (has) n(k, v); else nul(k); }
  void end() { o += '}'; }
};
inline void arpFull(std::string& o, const thunder::SeqEvent& e, const thunder::Seq* sq) { const double* a = sq->arp(e); o += '['; for (int i = 0; i < e.arpN; i++) { if (i) o += ','; o += '['; num(o, a[i * 2]); o += ','; num(o, a[i * 2 + 1]); o += ']'; } o += ']'; }
inline void event(std::string& o, const thunder::SeqEvent& e, const thunder::Seq* sq = nullptr) {
  using namespace thunder;
  const bool full = sq != nullptr;
  const auto bend = [&](W& w) { if (e.has & SH_BEND) { if (full) { w.key("bend"); W b(o); b.n("mul", e.bendMul); b.n("t1", e.bendT1); b.end(); } } };
  const auto vib = [&](W& w) { if (e.has & SH_VIB) { if (full) { w.key("vib"); W b(o); b.n("dep", e.vibDep); b.n("spd", e.vibSpd); b.n("t1", e.vibT1); b.end(); } } };
  W w(o); const char* ty = seq_ev_name(e.type);
  switch (e.type) {
    case SEV_BEAT: w.n("b", e.v); w.n("t", e.t); w.s("type", ty); break;
    case SEV_TVA: case SEV_TPN: case SEV_MOUT: case SEV_TVL: w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_FXP: if (e.has & SH_BAND) w.n("b", e.band); w.n("pi", e.pi); w.n("slot", e.slot); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_STP: w.b("all", e.flag); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); break;
    case SEV_FXC: w.s("p", e.p < 8 ? "sw" + std::to_string(e.p) : e.p == 8 ? "flx" : e.p == 9 ? "gat" : "rtg"); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_CHC: w.s("p", "b" + std::to_string(e.p)); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_MVOL: w.n("t", e.t); w.s("type", ty); w.n("v", e.v); break;
    case SEV_ENG:   // (c and o are objects: corePlain drops them)
      if (full) { w.key("c"); W b(o); b.n("atk", e.engC[3]); b.n("cut", e.engC[0]); b.n("dec", e.engC[2]); b.n("rel", e.engC[4]); b.n("res", e.engC[1]); b.end();
        w.key("o"); W q(o); q.n("atk", e.engO[3]); q.n("cut", e.engO[0]); q.n("dec", e.engO[4]); q.n("env", e.engO[2]); q.n("rel", e.engO[6]); q.n("res", e.engO[1]); q.n("sus", e.engO[5]); q.end(); }
      w.n("t", e.t); w.n("track", e.track); w.s("type", ty); break;
    case SEV_MPG: case SEV_MPB: case SEV_MAT: w.n("ii", e.ii); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_MCC: w.n("ii", e.ii); w.n("k", e.k); w.n("ramp", e.ramp); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_OFF: if (e.has & SH_COLS) { w.key("cols"); o += '['; bool f = true; for (int c = 0; c < 4; c++) if ((e.cols >> c) & 1) { if (!f) o += ','; f = false; o += (char)('0' + c); } o += ']'; }
      w.n("t", e.t); w.n("track", e.track); w.s("type", ty); break;
    case SEV_NOTE:
      w.b("acc", e.acc); w.n("apn", e.apn);
      if (!(e.has & SH_ARP)) w.nul("arp"); else if (full) { w.key("arp"); arpFull(o, e, sq); } else if (e.arpN == 0) { w.key("arp"); o += "[]"; }   // (an arpeggio is an array of pairs: dropped; an empty one is kept)
      if (!(e.has & SH_BEND)) w.nul("bend"); else bend(w);
      w.n("col", e.col); w.opt("cut", (e.has & SH_CUT) != 0, e.cut); if (e.has & SH_CHOP) w.n("durSec", e.durSec); w.n("fdi", e.fdi); w.n("gain", e.gain); w.n("ii", e.ii); w.n("mn", e.mn); w.n("ofs", e.ofs); if (e.has & SH_CHOP) w.n("ofsSec", e.ofsSec); w.n("pad", e.pad); w.n("pan", e.pan);
      w.b("poly", e.poly); w.n("rate", e.rate); w.opt("rel", (e.has & SH_REL) != 0, e.rel); w.b("rev", e.rev); if (e.has & SH_RLO) w.n("rlo", e.rlo); w.opt("slc", (e.has & SH_SLC) != 0, e.slc);
      if (!(e.has & SH_SLIDE)) w.nul("slide"); else if (full) { w.key("slide"); W b(o); b.n("dur", e.slideDur); b.n("from", e.slideFrom); b.end(); }
      if (e.has & SH_ST) { w.key("st"); W b(o); b.n("beats", e.stBeats); b.n("bpm", e.stBpm); b.n("grain", e.stGrain); if (e.stSx) b.n("gs", e.stGs); b.n("mode", e.stMode); if (e.stSx) { b.n("pt", e.stPt); b.n("rp", e.stRp); b.n("sx", e.stSx); b.n("x", e.stX); } b.end(); }   // (the JS's ev.st; corePlain drops it, the chop goldens' chopPlain keeps it)
      w.n("t", e.t); w.n("tick", e.tick); w.n("track", e.track); w.n("trm", e.trm);
      if (!(e.has & SH_TW)) w.nul("tw"); else if (full) { w.key("tw"); W b(o); b.opt("cut", (e.has & SH_TWCUT) != 0, e.twCut); b.opt("dec", (e.has & SH_TWDEC) != 0, e.twDec); b.n("res", e.twRes); b.end(); }
      w.s("type", ty); w.n("vc", e.vc); w.n("vel", e.vel);
      if (!(e.has & SH_VIB)) w.nul("vib"); else vib(w);
      if (e.has & SH_VK) w.s("vk", std::string(1, e.vkKind) + std::to_string(e.vkNum)); else w.nul("vk");
      w.opt("wob", (e.has & SH_WOB) != 0, e.wob); break;
    case SEV_MOD:
      if ((e.has & SH_ARP) && full) { w.key("arp"); arpFull(o, e, sq); } else if ((e.has & SH_ARP) && e.arpN == 0) { w.key("arp"); o += "[]"; }
      bend(w);
      if (e.has & SH_CUT) w.n("cut", e.cut);
      if (e.has & SH_GAIN) w.n("gain", e.gain);
      if (e.has & SH_MUL) w.n("mul", e.mul);
      if (e.has & SH_PAN) w.n("pan", e.pan);
      if (e.has & SH_REL) w.n("rel", e.rel);
      w.n("t", e.t); w.n("track", e.track); w.s("type", ty); vib(w);
      if (e.has & SH_WOB) w.n("wob", e.wob);
      break;
    case SEV_TEMPO: w.n("bpm", e.v); w.n("t", e.t); w.n("tks", e.tks); w.s("type", ty); break;
    default: w.s("type", ty); break;
  }
  w.end();
}
// one step as the golden tool's row: {"t":..,"evs":[..]}
inline std::string row(double t, const thunder::SeqEvent* evs, int n, const thunder::Seq* full = nullptr) {
  std::string o = "{\"t\":"; num(o, t); o += ",\"evs\":["; for (int i = 0; i < n; i++) { if (i) o += ','; event(o, evs[i], full); } o += "]}"; return o;
}
}  // namespace seqplain
#endif
