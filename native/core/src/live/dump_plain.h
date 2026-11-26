// TEST ONLY (F_DUMP_STEPS, thunder_live_dump_step): a sequencer event written as tools/seq-golden.mjs compares it (corePlain: keys sorted, numbers rounded to 1e-6, nested objects dropped, arrays of
// numbers kept). A COPY of native/tests/seq_plain.h (the writer the `seq` goldens test uses; the core must not include the tests' folder), without the `full` mode: the dump is the golden's row.
// Control side only (it allocates a std::string); never on the audio path.
#ifndef THUNDER_LIVE_DUMP_PLAIN_H
#define THUNDER_LIVE_DUMP_PLAIN_H
#include <cmath>
#include <string>

#include "rt/jsmath.h"
#include "seq/seq.h"

namespace thunder { namespace liveplain {
inline void num(std::string& o, double v) {   // coreNum, then JSON.stringify
  v = js_round(v * 1e6) / 1e6;
  if (!(v == v) || std::isinf(v)) { o += "null"; return; }
  char b[32]; o.append(b, (size_t)js_number_to_string(v, b));
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
inline void event(std::string& o, const SeqEvent& e) {
  W w(o); const char* ty = seq_ev_name(e.type);
  switch (e.type) {
    case SEV_BEAT: w.n("b", e.v); w.n("t", e.t); w.s("type", ty); break;
    case SEV_TVA: case SEV_TPN: case SEV_MOUT: case SEV_TVL: w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_FXP: if (e.has & SH_BAND) w.n("b", e.band); w.n("pi", e.pi); w.n("slot", e.slot); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_STP: w.b("all", e.flag); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); break;
    case SEV_FXC: w.s("p", e.p < 8 ? "sw" + std::to_string(e.p) : e.p == 8 ? "flx" : e.p == 9 ? "gat" : "rtg"); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_CHC: w.s("p", e.p > 15 ? "k" + std::to_string(e.p - 15) : "b" + std::to_string(e.p)); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_MVOL: w.n("t", e.t); w.s("type", ty); w.n("v", e.v); break;
    case SEV_ENG: w.n("t", e.t); w.n("track", e.track); w.s("type", ty); break;   // (c and o are objects: corePlain drops them)
    case SEV_MPG: case SEV_MPB: case SEV_MAT: w.n("ii", e.ii); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_MCC: w.n("ii", e.ii); w.n("k", e.k); w.n("ramp", e.ramp); w.n("t", e.t); w.n("track", e.track); w.s("type", ty); w.n("v", e.v); break;
    case SEV_OFF: if (e.has & SH_COLS) { w.key("cols"); o += '['; bool f = true; for (int c = 0; c < 4; c++) if ((e.cols >> c) & 1) { if (!f) o += ','; f = false; o += (char)('0' + c); } o += ']'; }
      w.n("t", e.t); w.n("track", e.track); w.s("type", ty); break;
    case SEV_NOTE:
      w.b("acc", e.acc); w.n("apn", e.apn);
      if (!(e.has & SH_ARP)) w.nul("arp"); else if (e.arpN == 0) { w.key("arp"); o += "[]"; }   // (an arpeggio is an array of pairs: dropped; an empty one is kept)
      if (!(e.has & SH_BEND)) w.nul("bend");
      w.n("col", e.col); w.opt("cut", (e.has & SH_CUT) != 0, e.cut); if (e.has & SH_CHOP) w.n("durSec", e.durSec); w.n("fdi", e.fdi); w.n("gain", e.gain); w.n("ii", e.ii); w.n("mn", e.mn); w.n("ofs", e.ofs); if (e.has & SH_CHOP) w.n("ofsSec", e.ofsSec); w.n("pad", e.pad); w.n("pan", e.pan);
      w.b("poly", e.poly); w.n("rate", e.rate); w.opt("rel", (e.has & SH_REL) != 0, e.rel); w.b("rev", e.rev); if (e.has & SH_RLO) w.n("rlo", e.rlo); w.opt("slc", (e.has & SH_SLC) != 0, e.slc);
      if (!(e.has & SH_SLIDE)) w.nul("slide");
      if (e.has & SH_ST) { w.key("st"); W b(o); b.n("beats", e.stBeats); b.n("bpm", e.stBpm); b.n("grain", e.stGrain); if (e.stSx) b.n("gs", e.stGs); b.n("mode", e.stMode); if (e.stSx) { b.n("pt", e.stPt); b.n("rp", e.stRp); b.n("sx", e.stSx); b.n("x", e.stX); } b.end(); }
      w.n("t", e.t); w.n("tick", e.tick); w.n("track", e.track); w.n("trm", e.trm);
      if (!(e.has & SH_TW)) w.nul("tw");
      w.s("type", ty); w.n("vc", e.vc); w.n("vel", e.vel);
      if (!(e.has & SH_VIB)) w.nul("vib");
      if (e.has & SH_VK) w.s("vk", std::string(1, e.vkKind) + std::to_string(e.vkNum)); else w.nul("vk");
      w.opt("wob", (e.has & SH_WOB) != 0, e.wob); break;
    case SEV_MOD:
      if ((e.has & SH_ARP) && e.arpN == 0) { w.key("arp"); o += "[]"; }
      if (e.has & SH_CUT) w.n("cut", e.cut);
      if (e.has & SH_GAIN) w.n("gain", e.gain);
      if (e.has & SH_MUL) w.n("mul", e.mul);
      if (e.has & SH_PAN) w.n("pan", e.pan);
      if (e.has & SH_REL) w.n("rel", e.rel);
      w.n("t", e.t); w.n("track", e.track); w.s("type", ty);
      if (e.has & SH_WOB) w.n("wob", e.wob);
      break;
    case SEV_TEMPO: w.n("bpm", e.v); w.n("t", e.t); w.n("tks", e.tks); w.s("type", ty); break;
    default: w.s("type", ty); break;
  }
  w.end();
}
}}  // namespace thunder::liveplain
#endif
