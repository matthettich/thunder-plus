// THE SEQUENCER (N1, unit U07): index.html's makeSeq (core/seq-core.js is the copy this was ported from, build 616), statement
// for statement, quirks included. It reads an immutable Song (song/song.h), walks every track on its own clock and gives one
// step's events at a time: the notes, the commands turned into automation for the player, the PERFORM scenes / macros / LFOs /
// parts, the engine targets. The goldens tests/golden/seq-electro*.json (made by the JS) are met exactly: ctest `seq`.
//
// REAL-TIME RULES. The constructor allocates and parses (control thread). step() allocates nothing, takes no lock and does no
// I/O: all per-track and per-song state lives in storage sized at construction. Where the JS grows without bound:
//   - the step's event list: the caller's buffer (cap events); more are counted in `dropped` (and the step's `dropped`);
//   - an arpeggio's [time, rate] pairs (at most 96 a row, as the JS): a pool of kSeqTracks x 96 owned by the sequencer,
//     valid until the next step();
//   - the chance rolls' "how often has this track rolled here" map (rvs): one counter per place a roll can happen
//     (every song cell with a chain x 16 chain rows x 32 steps; in sect / phrase mode 33 x 16 x 32);
//   - NTH's and CND 1ST's "passes through this row" maps: one counter per track for every phrase row that holds an NTH, CND
//     or REP command (the only rows that can count).
//   None of these can overflow for a song that fits the format; a place that has no counter is counted in `dropped` too.
//
// RANDOM NUMBERS. The JS draws from Math.random() until a SED command seeds the stream. Here there is no unseeded source:
// SeqOpts::randomSeed seeds a Mulberry32 that stands for Math.random (the golden tool's coreSeeded uses 0x2545F491).
#ifndef THUNDER_SEQ_SEQ_H
#define THUNDER_SEQ_SEQ_H
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "rt/json.h"
#include "rt/rng.h"
#include "song/song.h"

namespace thunder {

constexpr int kSeqMT = kSongNT;              // the master column's track number (MT)
constexpr int kSeqTracks = kSongNT + 1;      // tracks 0..31 and the master column
constexpr int kSeqArpMax = 96;               // [time, rate] pairs of one arpeggio (the JS's own limit)
constexpr double kSeqBpmMin = 1e-3, kSeqBpmMax = 1e6;   // setBpm's clamp (see there)
constexpr int kSeqMacros = 8;                // PERF_MACROS

enum class SeqMode { Song, Sect, Phrase };

// ---- the page's vars (index.html coreVars(): {vtrk, NSLOT, TFX: coreTfx()}) ------------------------------------------
struct SeqTfxParam { std::string k; double min = 0, max = 0; bool lfo = false; bool hasPi = false; int pi = 0; bool hasPb = false; std::vector<double> pb; };
struct SeqPumpParam { std::string s; double min = 0, max = 0, def = 0, skew = 0; int scale = 0 /* 0 linear, 1 'log', 2 'pow' */; int flags = 0; };
struct SeqTfxType { std::string type; bool pump = false; std::vector<SeqTfxParam> ps; bool hasPd = false; std::vector<SeqPumpParam> pd; };
struct SeqVars {
  bool vtrk = false;                         // a voice per track (pads follow the track, not the instrument)
  int nslot = 16;                            // NSLOT
  std::vector<SeqTfxType> tfx;               // TFX, as coreTfx() gives it
  void load(Json vars);                      // from {"vtrk":..,"NSLOT":..,"TFX":{..}} (control thread); missing members keep their values
  const SeqTfxType* type(std::string_view name) const;
};

// ---- makeSeq's options `o` --------------------------------------------------------------------------------------------
struct SeqAt { bool valid = false; int row = 0, cr = -1, phrase = -1, step = 0; };
struct SeqOpts {
  int row = 0;                               // song: the row PLAY starts on (below the top: CHASE)
  bool hasLoop = false; int loopY0 = 0, loopY1 = 0;   // loop {y0, y1}: song rows, chain rows (sect) or steps (phrase)
  bool once = false;                         // a render: stops at the end instead of wrapping
  bool live = false, sim = false;            // live play (wake, live transpose); sim: no audio (the golden runs)
  bool hasOnly = false; uint64_t only = 0;   // BUS PLAY: bit t = track t plays
  int track = 0, sect = -1, phrase = -1;     // sect / phrase mode
  bool hasSeed = false; uint32_t seed = 0;   // o.seed: this PLAY's chance seed (else drawn from the random stream)
  uint32_t randomSeed = 0x2545F491u;         // stands for Math.random (see above)
  int lx = 0;                                // lxp.st: the live transpose (used when live)
  bool hasAt = false; int atRef = 0; SeqAt at[kSeqTracks];   // o.at / o.atRef: each track where it was (a hand-over)
};

// N1 review B4: the most events one step() can write, so that no step drops one (a dropped OFF / STP hangs a live note). Worst case the
// song format allows, per track (33: the 32 tracks and the master column):
//   notes     4 columns x 64 hits (RET: the first and at most 63 more)                      = 256
//   one row   OFF / SKP 1, TVL TPN STP 3, FXV 1, PGM MPB MAT + 4 CC 7, CHC 15 x 2 = 30,
//             ENG 4, FXC 16 (FXSLOTS 8, a switch and a parameter each)                       =  62   (the master column has fewer)
//   per track 256 + 62 = 318; x 33 tracks = 10494; + BEAT 1 = 10495.
// The PERFORM events (PRT part snapshot, scenes, macros, locks, LFOs) are bounded by the song, not by the format (review C6): the
// rest of the 12288 is theirs (1793). Past that the events are still counted (Seq::dropped, RenderStats::seqDropped), never silent.
// 12288 x sizeof(SeqEvent) (416 B) = 5 MB, allocated once with the renderer.
constexpr int kSeqStepEvents = 12288;

// ---- events ----------------------------------------------------------------------------------------------------------
enum SeqEvType : uint8_t { SEV_BEAT = 0, SEV_TVA, SEV_TPN, SEV_MOUT, SEV_TVL, SEV_FXP, SEV_STP, SEV_FXC, SEV_CHC, SEV_MVOL, SEV_ENG, SEV_MPG, SEV_MPB,
  SEV_MAT, SEV_MCC, SEV_OFF, SEV_NOTE, SEV_MOD, SEV_TEMPO, SEV_COUNT };
const char* seq_ev_name(uint8_t type);      // the JS's `type` string

// which optional members an event carries (the JS's "key present and not null")
enum : uint32_t { SH_CUT = 1u << 0, SH_REL = 1u << 1, SH_SLIDE = 1u << 2, SH_TW = 1u << 3, SH_TWCUT = 1u << 4, SH_TWDEC = 1u << 5, SH_RLO = 1u << 6,
  SH_SLC = 1u << 7, SH_WOB = 1u << 8, SH_ARP = 1u << 9, SH_BEND = 1u << 10, SH_VIB = 1u << 11, SH_COLS = 1u << 12, SH_BAND = 1u << 13,
  SH_GAIN = 1u << 14, SH_PAN = 1u << 15, SH_MUL = 1u << 16, SH_VK = 1u << 17 };

// One event, flat. Which members mean something depends on `type` (the JS event's keys, in brackets):
//   beat   t, v [b]                               tempo  t, v [bpm], tks
//   tva / tpn / mout / tvl   t, track, v          mvol   t, v
//   fxp    t, track, slot, pi, v, band [b] (SH_BAND)
//   stp    t, track, flag [all]                   fxc    t, track, p (0-7: sw0-sw7, 8 flx, 9 gat, 10 rtg), v
//   chc    t, track, p (1-15: b1-b15), v          eng    t, track, engC[5] [c: cut res dec atk rel], engO[7] [o: cut res env atk dec sus rel]
//   mpg / mpb / mat   t, track, ii, v             mcc    t, track, ii, k, v, ramp
//   off    t, track, cols (SH_COLS: bit c = note column c)
//   note   every member from `poly` down          mod    t, track and gain / pan / wob / mul / arp / bend / vib / cut / rel when their SH_ bit is set
struct SeqEvent {
  uint8_t type = 0; bool flag = false, poly = false, rev = false, acc = false; uint8_t cols = 0; char vkKind = 0 /* 'g' GEN, 'm' MUT */;
  uint32_t has = 0;
  int32_t track = -1, slot = 0, pi = 0, p = 0, k = 0, tks = 0, col = 0, vkNum = 0, arpOff = 0, arpN = 0;
  double t = 0, v = 0, band = 0, ramp = 0, ii = 0;
  double pad = 0, rate = 0, gain = 0, pan = 0, ofs = 0, cut = 0, rel = 0, slideFrom = 0, slideDur = 0, twCut = 0, twRes = 0, twDec = 0, rlo = 0, slc = 0,
    fdi = 0, trm = 0, apn = 0, wob = 0, bendMul = 0, bendT1 = 0, vibSpd = 0, vibDep = 0, vibT1 = 0, mn = 0, vel = 0, tick = 0, vc = 0, mul = 0;
  double engC[5] = {0, 0, 0, 0, 0}, engO[7] = {0, 0, 0, 0, 0, 0, 0};
};

// Where a track was on this step (the JS's `pos`: the playhead, the piano's sounding notes).
struct SeqPos { bool valid = false; int row = 0, cr = 0, step = 0, sect = -1, phrase = -1; bool off = false; uint8_t offc = 0; int nsc = 0; int8_t scCol[4] = {0, 0, 0, 0}; double scNote[4] = {0, 0, 0, 0};
  bool hasPd = false; double pd = 0; };
struct SeqStep { double t = 0; int n = 0; int tks = 0; double line = 0; uint32_t dropped = 0; SeqPos pos[kSeqTracks]; };

class Seq {
 public:
  // Control thread. The Song and the vars must outlive the sequencer and must not change while it lives.
  Seq(const Song& song, const SeqVars& vars, SeqMode mode, const SeqOpts& opts);
  ~Seq();
  Seq(const Seq&) = delete;
  Seq& operator=(const Seq&) = delete;

  // api.step(): plays the next line of every track that is due, writes its events to evs[0..cap) and returns how many
  // (also out->n). Audio thread: no allocation, no lock, no I/O.
  int step(SeqEvent* evs, int cap, SeqStep* out);
  bool alive() const;                         // api.alive()
  void setLoop(bool has, int y0, int y1);   // api.setLoop; the rows are clamped to the mode's range (N1 review C2)
  int loopMax() const { return mode_ == SeqMode::Song ? 255 : mode_ == SeqMode::Sect ? 15 : 31; }   // the last row / chain row / step a loop can name
  double time() const { return time_; }      // api.time: when the next step is due
  double bpm() const { return bpm_; }
  // ---- the clock, as the JS api's writable members (SEQ-LIVE-DESIGN A1). Audio thread; no allocation, no lock. ----------------
  // setTime: `seq.time = t0` (core-processor play, :422). Before the first step() the base is unset and step() takes it from time_.
  //   A t that is not finite is ignored and counted (the JS would take it; every later time would be NaN): deliberate difference.
  // setBpm: `seq.bpm = v` (:354); heard from the next step(). A v that is not finite or not > 0 is ignored and counted (deliberate
  //   difference); one that is finite and positive is clamped to [kSeqBpmMin, kSeqBpmMax] so a step's times stay finite (counted too).
  // shift: `base += d; time += d; if (btT != null) btT += d` (:421, :778). With no base yet (never stepped) it moves time_ only.
  //   A d that is not finite, or whose result would not be finite, is ignored and counted.
  void setTime(double t);
  void setBpm(double v);
  void shift(double d);
  bool hasBase() const { return hasBase_; }  // the `rows` message's base / btT (:792): null before the first step
  double base() const { return base_; }
  bool hasBtT() const { return hasBtT_; }
  double btT() const { return btT_; }
  double bt() const { return bt_; }          // api.bt: the song position in beats at the last step
  uint64_t badSets() const { return badSets_; }   // setter calls ignored or clamped (setTime, setBpm, shift), ever
  int tks() const { return tks_; }
  const double* arp(const SeqEvent& e) const { return arpPool_.get() + 2 * (size_t)e.arpOff; }   // e.arpN pairs [time, rate multiplier]
  // PERFORM's live state (the panel's): the crossfader, a macro, the FILL switch. Heard from the next perfDiff / condition.
  void setXf(double v) { pfXf_ = v; }
  void setMac(int i, double v) { if (i >= 0 && i < (int)pfMac_.size()) { pfMac_[(size_t)i] = v; pfMacSet_[(size_t)i] = 1; } }
  void setFill(bool on) { pfFill_ = on; }

  uint64_t dropped() const { return dropped_; }   // events (or counters) that did not fit, ever: the tests assert 0
  // the largest counts seen (for choosing capacities from a measurement)
  int maxEventsPerStep() const { return maxEvs_; }
  int maxArpPairsPerStep() const { return maxArp_; }
  size_t rollPlaces() const { return rvN_; }
  size_t countRows() const { return (size_t)nthK_; }
  size_t targetIds() const { return ids_.size(); }

 private:
  struct Tr;
  struct Cmd { int c; int v; };
  struct Fx;
  struct Ctx;
  struct TgtId { std::string s; int kind = -1; double t = 0, sl = -1, p = -1; bool hasB = false; double b = 0; bool ok = false, tch4 = false, fxOk = false; double own = 0; };
  struct SceneEnt { int id; bool inA, inB; double a, b; };
  struct MacTg { int id; double lo, hi; };
  struct Macro { double v; bool hasV; std::vector<MacTg> tg; };
  struct Lfo { bool present; int id; bool ok; double sh, rt, dp; };
  struct PEv { uint8_t type; int track, slot, pi; double v; };
  struct PInst { bool present = false; double pad = 0, tsp = 0, vol = 0, pan = 0, ofs = 0; int hut = 0, huv = 0; bool kf = false, rev = false; };
  struct Part { bool present = false; std::vector<PEv> evs; std::vector<PInst> insts; };
  struct Chase { uint32_t seed; int16_t tch; uint8_t hasSeed, skp, chd; };

  // construction (control thread)
  int intern(Json id);
  void parseId(TgtId& g) const;
  void buildPerf();
  void buildParts();
  void buildBus();
  void buildCounters();
  void buildChase();
  double tgtOwnOf(const TgtId& g, bool* fxOk) const;
  void partEvsOf(Json mix, Json fx, bool own, std::vector<PEv>& out) const;

  // helpers of makeSeq
  void fresh(Tr& T);
  int sectId(const Tr& T, int t) const;
  int songCell(int row, int t) const { return row >= 0 && row < kSongRows ? S_.cell(row, t) : -1; }
  int chainPhrase(int cid, int cr) const { const int16_t* c = S_.chain(cid); return c && cr >= 0 && cr < kChainRows ? c[cr * 2] : -1; }
  bool chainRow(int cid, int cr, int* ph, int* tsp) const { const int16_t* c = S_.chain(cid); if (!c || cr < 0 || cr >= kChainRows) return false; *ph = c[cr * 2]; *tsp = c[cr * 2 + 1]; return true; }
  int lenOf(int id) const;
  int emptyLen() const;
  int plpbOf(int id) const;
  int offInSong(const Tr& T, int t) const;
  void settle(Tr& T, int t);
  void wake(double rel);
  void chaseChance(int row);
  bool mutedAt(int t, int y) const;
  const int16_t* rowOf(const Tr& T, int t, int pid) const;
  double rnd();
  double mathRandom() { return mrand_.next(); }
  int rint(int dn, int up);
  double roll(Tr& T, int t, int salt);
  int arpOf(int v, const Tr& T, double at, double end, double tick, int* off);
  int dirNext(Tr& T, int y, int L);
  bool isPolyT(int t) const { return t >= polyFrom_ && t < kSongNT; }
  SeqEvent* push(Ctx& c);
  void tgtEvs(Ctx& c, int id, bool own, double v, double at);
  void perfDiff(Ctx& c, double at);
  void fxEvs(Ctx& c, int t, const Fx& fx, double at, double line);
  void slotWake(Ctx& c, int t, int need, bool on, double sw, double at2);
  void engEmit(Ctx& c, int u, double t0, double dur, int ne, const double (*pts)[4], unsigned ptsHas);
  void lfoEvs(Ctx& c, int t, double t0, double dur);
  const PInst& instOf(int ii) const;
  bool sentHas(int id) const { return id >= 0 && sentHas_[(size_t)id]; }
  void sentSet(int id, double v);
  void sentDel(int id);
  uint32_t* nthSlot(int t, int pid, int step, bool c1);

  const Song& S_;
  const SeqVars& V_;
  SeqMode mode_;
  SeqOpts o_;
  bool once_ = false, hasLoop_ = false; int loopY0_ = 0, loopY1_ = 0;
  bool wakeOk_ = false, PL_ = false;
  int ntr_ = 16, polyFrom_ = 8, NIs_ = 16;
  bool flxOld_ = false, hasSmuteM_ = false;
  std::vector<uint8_t> smuteM_;
  uint8_t need_[kSeqTracks][8];              // FX_NEEDS per effect slot: 1 flex, 2 sgate, 3 retrig
  bool busT_[kSongNT];
  bool hasHear_ = false; bool hearT_[kSeqTracks];

  std::unique_ptr<Tr[]> tr_;
  // api
  double time_ = 0, bpm_ = 120, base_ = 0, bt_ = 0, btT_ = 0; bool hasBase_ = false, hasBtT_ = false;
  int tks_ = 6; bool hasGrv_ = false; int grv_[2] = {0, 0}; bool hasLpb_ = false; int lpb_ = 0;
  // random
  Mulberry32 mrand_, seeded_; bool isSeeded_ = false;
  bool hasSseed_ = false, hasPseed_ = false; uint32_t sseed_ = 0, pseed_ = 0; int mtch_ = 255;
  bool hasGscl_ = false; int gsclScale_ = 0, gsclRoot_ = 0;
  // PERFORM
  std::vector<TgtId> ids_;
  std::vector<SceneEnt> scene_;
  std::vector<Macro> macros_;
  int lk_[kSeqTracks][4];
  std::vector<Lfo> lfo_[kSeqTracks]; bool lfoIsArr_[kSeqTracks];
  int canon_[7][kSongNT];
  double pfXf_ = 0; std::vector<double> pfMac_; std::vector<uint8_t> pfMacSet_; bool pfFill_ = false; int pfPart_ = -1; bool perfFirst_ = true;
  bool pnoHas_[kSeqTracks][4]; double pno_[kSeqTracks][4];
  std::vector<uint8_t> sentHas_, wantHas_; std::vector<double> sentVal_, wantVal_; std::vector<int> sentOrd_, wantOrd_; int sentN_ = 0, wantN_ = 0;
  std::vector<Part> parts_; Part own_;
  std::vector<PInst> sInsts_;
  // counters
  std::vector<int32_t> cellSlot_; std::unique_ptr<uint32_t[]> rv_; size_t rvN_ = 0;
  std::vector<int32_t> rowSlot_; int nthK_ = 0; std::unique_ptr<uint32_t[]> nthCnt_, nthGen_, c1Cnt_; uint32_t trGen_[kSeqTracks];
  std::vector<Chase> chase_; std::vector<uint8_t> chaseHasS_; std::vector<uint32_t> chaseS_; std::vector<int16_t> chaseM_;
  std::unique_ptr<double[]> arpPool_; int arpUsed_ = 0;
  uint64_t badSets_ = 0;
  uint64_t dropped_ = 0; int maxEvs_ = 0, maxArp_ = 0;
};

}  // namespace thunder
#endif
