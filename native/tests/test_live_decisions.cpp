// LIVE ROUTE gate L10(f): the pool's DECISIONS against the JS core's (tools/live-route-dump.mjs: core/seq-core.js ThunderCore.ev() with fake nodes; the committed golden/live_route_log.json holds the events of each case and,
// per node, the automation lists the core left on it). The same events go through Players::dispatch; each node's booked events are compared with the JS's lists:
//   gate (the note-ons and note-offs, at the sample of the JS's time), the trig count, leg / syl / same / acc at each surviving note-on, rate / g / pn exactly (the lists, not sampled), wob, and per node lastOn / sylN, voxG.
// Cases that need the sampler's or the SoundFont's voices, the pump synths or waves (takeover, choke, the poly cap across pools) are B5 / later steps and are skipped here (listed).
//   test_live_decisions <live_route_log.json>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "engine/live_pool.h"
#include "render/players.h"
#include "rt/json.h"
#include "rt/sampletime.h"
#include "song/song.h"

using namespace thunder;
static int g_fail = 0;
#define CHECKF(c, ...) do { if (!(c)) { std::printf("FAIL %s:%d: %s: ", __FILE__, __LINE__, #c); std::printf(__VA_ARGS__); std::printf("\n"); g_fail++; } } while (0)
static const double SR = 48000;

static std::string songJson() {
  return "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[4],\"phrases\":[[[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1],[-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1]]]}";
}

struct JEv { std::vector<double> arp; SeqEvent e; bool stopAll = false; double at = 0; int b = 0; };
static JEv convert(Json j) {
  JEv o; o.b = (int)j.num("b");
  if (j.str("op") == "stopAll") { o.stopAll = true; o.at = j.num("at"); return o; }
  Json ev = j.get("ev"); SeqEvent& e = o.e; const std::string_view ty = ev.str("type");
  e.t = ev.num("t"); e.track = (int)ev.num("track", 0);
  if (ty == "note") {
    e.type = SEV_NOTE; e.pad = ev.num("pad"); e.poly = ev.boolean("poly"); e.col = (int)ev.num("col"); e.rate = ev.num("rate"); e.gain = ev.num("gain"); e.pan = ev.num("pan"); e.acc = ev.boolean("acc");
  } else if (ty == "eng") {
    e.type = SEV_ENG; static const char* cn[5] = {"cut", "res", "dec", "atk", "rel"}, *on[7] = {"cut", "res", "env", "atk", "dec", "sus", "rel"};
    for (int k = 0; k < 5; k++) e.engC[k] = ev.get("c").get(cn[k]).isNum() ? ev.get("c").num(cn[k]) : std::nan("");   // (a key that is missing is not a number)
    for (int k = 0; k < 7; k++) e.engO[k] = ev.get("o").get(on[k]).isNum() ? ev.get("o").num(on[k]) : std::nan("");
  } else if (ty == "off") { e.type = SEV_OFF; } else if (ty == "mod") { e.type = SEV_MOD; } else if (ty == "stp") { e.type = SEV_STP; e.flag = ev.boolean("all"); }
  if (ev.get("slide").isObj()) { e.has |= SH_SLIDE; e.slideFrom = ev.get("slide").num("from"); e.slideDur = ev.get("slide").num("dur"); }
  if (ev.get("wob").isNum()) { e.has |= SH_WOB; e.wob = ev.num("wob"); }
  if (ev.get("cut").isNum()) { e.has |= SH_CUT; e.cut = ev.num("cut"); }
  if (ev.get("rel").isNum()) { e.has |= SH_REL; e.rel = ev.num("rel"); }
  if (ev.get("mul").isNum()) { e.has |= SH_MUL; e.mul = ev.num("mul"); }
  if (ty == "mod") { if (ev.get("gain").isNum()) { e.has |= SH_GAIN; e.gain = ev.num("gain"); } if (ev.get("pan").isNum()) { e.has |= SH_PAN; e.pan = ev.num("pan"); } }
  if (ev.get("cols").isArr()) { e.has |= SH_COLS; e.cols = 0; for (size_t i = 0; i < ev.get("cols").size(); i++) e.cols |= (uint8_t)(1u << (int)ev.get("cols").at(i).asNum()); }
  if (ev.get("arp").isArr()) { e.has |= SH_ARP; e.arpN = (int)ev.get("arp").size(); for (int i = 0; i < e.arpN; i++) { o.arp.push_back(ev.get("arp").at((size_t)i).at(0).asNum()); o.arp.push_back(ev.get("arp").at((size_t)i).at(1).asNum()); } }
  if (ev.get("bend").isObj()) { e.has |= SH_BEND; e.bendMul = ev.get("bend").num("mul"); e.bendT1 = ev.get("bend").num("t1"); }
  if (ev.get("vib").isObj()) { e.has |= SH_VIB; e.vibSpd = ev.get("vib").num("spd"); e.vibDep = ev.get("vib").num("dep"); e.vibT1 = ev.get("vib").num("t1"); }
  return o;
}

struct L { double t, v; int k; };
static std::vector<L> jsList(Json a) { std::vector<L> r; for (size_t i = 0; i < a.size(); i++) { Json x = a.at(i); r.push_back({x.at(1).asNum(), x.at(2).asNum(), (int)x.at(0).asNum()}); } return r; }
static bool autoEq(const Auto& a, const std::vector<L>& l) {
  if ((size_t)a.size() != l.size()) return false;
  for (int i = 0; i < a.size(); i++) if (a.event(i).k != l[(size_t)i].k || a.event(i).t != l[(size_t)i].t || !(a.event(i).v == l[(size_t)i].v)) return false;
  return true;
}
// the value of a JS param at the sample T: the last of its sets at or before it
static double valueAt(const std::vector<L>& l, int64_t T, double def) { double v = def; for (const L& x : l) if (sample_at_or_after(x.t, SR) <= T) v = x.v; return v; }

int main(int argc, char** argv) {
  if (argc < 2) { std::printf("usage: test_live_decisions live_route_log.json\n"); return 2; }
  std::ifstream f(argv[1], std::ios::binary); std::stringstream ss; ss << f.rdbuf(); const std::string text = ss.str();
  JsonDoc doc; JsonError je; if (!doc.parse(text, &je)) { std::printf("log does not parse\n"); return 2; }
  Song song; const std::string sj = songJson(); if (!song.load(sj.data(), sj.size(), &je)) return 2;
  SeqVars vars; SeqOpts so; so.row = song.renderFrom; so.once = true; Seq seq(song, vars, SeqMode::Song, so);
  // the cases of this stage; the rest are listed
  const std::set<std::string> run = {"bass_mono", "bass_kill_back", "bass_same_sample", "acid_mach", "vox_mono", "vox_poly", "vox_idle_gap", "mod_stp", "no_node", "late", "stop_all", "takeover", "choke_cross", "poly_cap", "eng_basic", "eng_late_bind", "eng_vox_poly", "eng_stop_all", "eng_partial"};
  std::vector<float> dc((size_t)(SR * 3), 0.25f);   // the sampler pads' buffer (pads 5 and 7): voices that stay for the case
  int ran = 0, skipped = 0, compared = 0;
  Json root = doc.root();
  for (size_t ci = 0; ci < root.size(); ci++) {
    Json C = root.at(ci); const std::string id(C.str("id"));
    if (!run.count(id)) { skipped++; continue; }
    std::unique_ptr<RenderPads> pads(new RenderPads());
    for (int p : {0, 1, 2, 3, 4, 6, 8}) pads->route[p] = PadRoute::Live;
    for (int p : {5, 7}) { pads->buf[p].data = dc.data(); pads->buf[p].len = (int)dc.size(); pads->buf[p].sr = SR; }
    pads->kit[6].choke = 1; pads->kit[7].choke = 1;   // the dump's table: pad 6 a bass with choke 1, pad 7 a sampler with choke 1
    const bool viaPlayers = id == "takeover" || id == "choke_cross" || id == "poly_cap";   // the cases that need the sampler's voices: the whole path, takeover and cap included
    Players pl(pads.get(), SR, 8);
    uint64_t mask = 0; for (size_t i = 0; i < C.get("tracks").size(); i++) mask |= (uint64_t)1 << (int)C.get("tracks").at(i).asNum();
    if (!mask) mask = (uint64_t)1 << 31;   // no node for the case's tracks
    LivePadConfig b, v, a, m; b.kind = LiveKind::Bass; v.kind = LiveKind::Vox; a.kind = LiveKind::Acid; m.kind = LiveKind::Mach; b.trackMask = v.trackMask = a.trackMask = m.trackMask = mask;
    CHECKF(pl.live().addPad(0, b) && pl.live().addPad(1, v) && pl.live().addPad(2, a) && pl.live().addPad(3, m) && pl.live().addPad(6, b), "%s: pads", id.c_str());
    Json evs = C.get("events"); std::vector<JEv> list; for (size_t i = 0; i < evs.size(); i++) list.push_back(convert(evs.at(i)));
    for (JEv& j : list) {
      pl.setTime((double)j.b * 128 / SR, (int64_t)j.b * 128);
      if (j.stopAll) { pl.stopAll(j.at); continue; }
      // (straight into the live pool: an arpeggio's pairs are the case's, not the sequencer's table; the takeover of the other pools is B5's and is checked there)
      if (viaPlayers) { pl.dispatch(j.e, seq, song); continue; }
      const SeqEvent& e = j.e; const double* arp = j.arp.empty() ? nullptr : j.arp.data();
      if (e.type == SEV_NOTE) pl.live().note(e, (int)e.pad, 120, arp); else if (e.type == SEV_OFF) pl.live().off(e); else if (e.type == SEV_MOD) pl.live().mod(e, arp); else if (e.type == SEV_STP) pl.live().stp(e); else if (e.type == SEV_ENG) pl.live().eng(e);
    }
    ran++;
    Json nodes = C.get("nodes");
    for (size_t ni = 0; ni < nodes.size(); ni++) {
      const std::string key(nodes.keyAt(ni)); Json N = nodes.valAt(ni);
      int track = 0, pad = 0, j = 0; char kind[16] = {}; { const int n = std::sscanf(key.c_str(), "%d:%d:%15[a-z]:%d", &track, &pad, kind, &j); if (n < 3) continue; if (n < 4) j = 0; }
      if (pad > 3 && pad != 6) continue;   // pump synths, waves: later
      if (id == "stop_all" && pad == 1) continue;   // (its vox notes are taken over by a pump-synth note: B5)
      LiveDebugNode d; if (!pl.live().debugNode(track, pad, j, &d)) { CHECKF(false, "%s: no node %s", id.c_str(), key.c_str()); continue; }
      compared++;
      // gate: note-ons and note-offs at the JS's samples
      std::vector<std::pair<int64_t, int>> got, want; int ons = 0;
      for (int i = 0; i < d.ntl; i++) { if (d.tl[i].type == LiveEvType::NoteOn) { got.push_back({d.tl[i].T, 1}); ons++; } else if (d.tl[i].type == LiveEvType::NoteOff) got.push_back({d.tl[i].T, 0}); }
      for (const L& x : jsList(N.get("gate"))) want.push_back({sample_at_or_after(x.t, SR), (int)x.v});
      CHECKF(got == want, "%s %s: gate differs (pool %zu events, JS %zu)", id.c_str(), key.c_str(), got.size(), want.size());
      CHECKF((size_t)ons == N.get("trig").size(), "%s %s: %d note-ons, JS trig %zu", id.c_str(), key.c_str(), ons, N.get("trig").size());
      // leg, syl, same, acc at each note-on
      const std::vector<L> leg = jsList(N.get("leg")), syl = jsList(N.get("syl")), same = jsList(N.get("same")), acc = jsList(N.get("acc"));
      for (int i = 0; i < d.ntl; i++) if (d.tl[i].type == LiveEvType::NoteOn) {
        const int64_t T = d.tl[i].T;
        CHECKF((valueAt(leg, T, 0) != 0) == d.tl[i].leg, "%s %s: leg at %lld", id.c_str(), key.c_str(), (long long)T);
        if (pad == 1) { CHECKF(valueAt(syl, T, 0) == d.tl[i].v, "%s %s: syl at %lld: pool %g JS %g", id.c_str(), key.c_str(), (long long)T, d.tl[i].v, valueAt(syl, T, 0)); CHECKF((valueAt(same, T, 0) != 0) == d.tl[i].same, "%s %s: same at %lld", id.c_str(), key.c_str(), (long long)T); }
        if (pad == 2 || pad == 3) CHECKF((valueAt(acc, T, 0) != 0) == d.tl[i].acc, "%s %s: acc at %lld", id.c_str(), key.c_str(), (long long)T);
      }
      { const std::vector<L> jl = jsList(N.get("rate")); if (!autoEq(*d.rate, jl)) for (int i = 0; i < d.rate->size() && (size_t)i < jl.size(); i++) if (d.rate->event(i).k != jl[(size_t)i].k || d.rate->event(i).t != jl[(size_t)i].t || d.rate->event(i).v != jl[(size_t)i].v) { std::printf("  rate[%d]: pool k%d t %.17g v %.17g, JS k%d t %.17g v %.17g\n", i, d.rate->event(i).k, d.rate->event(i).t, d.rate->event(i).v, jl[(size_t)i].k, jl[(size_t)i].t, jl[(size_t)i].v); break; } }
      CHECKF(autoEq(*d.rate, jsList(N.get("rate"))), "%s %s: the rate lists differ (pool %d events, JS %zu)", id.c_str(), key.c_str(), d.rate->size(), N.get("rate").size());
      CHECKF(autoEq(*d.g, jsList(N.get("g"))), "%s %s: the level lists differ", id.c_str(), key.c_str());
      CHECKF(autoEq(*d.pn, jsList(N.get("pn"))), "%s %s: the pan lists differ", id.c_str(), key.c_str());
      { static const char* lane[12] = {"cCut", "cRes", "cDec", "cAtk", "cRel", "oCut", "oRes", "oEnv", "oAtk", "oDec", "oSus", "oRel"};   // the engine targets: the twelve lanes as the JS left them
        for (int k = 0; k < 12; k++) { const std::vector<L> jl = jsList(N.get(lane[k])); if (d.eng[k]) CHECKF(autoEq(*d.eng[k], jl), "%s %s: the %s lane differs (pool %d events, JS %zu)", id.c_str(), key.c_str(), lane[k], d.eng[k]->size(), jl.size()); else CHECKF(jl.empty(), "%s %s: the pool has no %s lane, the JS has %zu events", id.c_str(), key.c_str(), lane[k], jl.size()); } }
      std::vector<std::pair<int64_t, double>> wg, ww; for (int i = 0; i < d.ntl; i++) if (d.tl[i].type == LiveEvType::Wob) wg.push_back({d.tl[i].T, d.tl[i].v});
      for (const L& x : jsList(N.get("wob"))) ww.push_back({sample_at_or_after(x.t, SR), x.v});
      CHECKF(wg == ww, "%s %s: wob differs", id.c_str(), key.c_str());
      Json LN = C.get("lnodes").get(key);
      if (LN.isObj()) { CHECKF(d.lastOn == LN.num("lastOn"), "%s %s: lastOn pool %.17g JS %.17g", id.c_str(), key.c_str(), d.lastOn, LN.num("lastOn")); if (pad == 1 && j == 0) CHECKF(d.sylN == LN.num("sylN"), "%s %s: sylN pool %g JS %g", id.c_str(), key.c_str(), d.sylN, LN.num("sylN")); }
    }
    // voxG
    Json VG = C.get("voxG");
    for (size_t gi = 0; gi < VG.size(); gi++) { int tr = 0, pd = 0; std::sscanf(std::string(VG.keyAt(gi)).c_str(), "%d:%d", &tr, &pd); double s, lt, lk; Json g = VG.valAt(gi);
      CHECKF(pl.live().debugVoxG(tr, pd, &s, &lt, &lk) && s == g.num("sylN") && lt == g.num("lastT") && lk == g.num("lastK"), "%s: voxG %d:%d differs", id.c_str(), tr, pd); }
    // the case with no node: nothing is booked, every note is counted
    if (id == "no_node") CHECKF(pl.live().stats().noNode == 2, "no_node: counted %u", pl.live().stats().noNode);
  }
  std::printf("decision log: %d cases compared (%d nodes), %d skipped (pump synths, waves: later steps)\n", ran, compared, skipped);
  if (g_fail) { std::printf("live_decisions: %d FAILED\n", g_fail); return 1; }
  std::printf("live_decisions: all checks passed\n"); return 0;
}
