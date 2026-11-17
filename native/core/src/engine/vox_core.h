// U26: VoxCore of index.html (the singing robot: lyrics -> phonemes -> syllables -> a Klatt-style formant voice), the core the live voice node and the pad render share. Explicit state: render and on / off never
// allocate; set() allocates when the LYRICS CHANGE (it parses them: control thread only). THE JS IS THE SPECIFICATION (see bass_core.h for the conventions; the L / R buffers are Float32Array, the glottal table too).
// The text -> syllable part (VoxCore.parse / rules / syllabify, the dictionary and the letter-to-sound rules) is generated into vox_tables.inc by tools/u26-tables.mjs.
// on(leg, n, same) takes the syllable number as a double (the live node passes Math.round(syl lane)); where the JS would throw (`S[NaN].rest`, the lyric word "constructor": D["constructor"] is Object's) on() / set() return false.
// render(L, R, len, a, b, rate): control blocks of BLK = 16 samples anchored at a; L is written, R too unless it is nullptr. THE LIVE INTERFACE (live_shell.h) IS PROVISIONAL.
#ifndef THUNDER_ENGINE_VOX_CORE_H
#define THUNDER_ENGINE_VOX_CORE_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "engine/xeng.h"

namespace thunder {

// The syllables of a lyric (VoxCore.parse): phoneme ids index the phoneme table (-1: a name the table does not have, as `PH[x]` undefined)
struct VoxSyl { bool rest; int nu; uint32_t onB, onN, coB, coN; };   // on / co: runs in the pool
struct VoxLyrics {
  std::vector<VoxSyl> syl; std::vector<int16_t> pool; size_t maxSegs = 2, maxCo = 0;
  bool parse(const std::string& utf8);   // false: the JS would throw (the word "constructor"); the object is left as it was
};
// One syllable's phoneme runs as std::string names, for the tests (the JS's `{rest, on, nu, co}` objects): "R" for a rest, else "on1 on2 | nu | co1 co2"
std::string vox_syllables_text(const std::string& utf8, bool* threw);

class VoxCore {
 public:
  static constexpr double kBlk = 16;
  explicit VoxCore(double sr);
  bool set(const VoxP& o);               // false: the JS's set() threw (the new lyrics could not be parsed; the parameters are kept, the old syllables too, as in the JS)
  bool on(bool leg, double n, bool same);
  void off();
  bool busy() const { return coda_ && (double)si_ < (double)nsegs_ - 1; }
  void render(float* L, float* R, size_t len, double a, double b, double rate);
  size_t nSyl() const { return lyr_.syl.size(); }
  const VoxP& p() const { return p_; }
  // a core that is not producing sound (envelope 0 and no target): the shell may skip it
 private:
  enum Kind : uint8_t { kHold, kGlide, kClo, kBurst, kFric, kAsp, kSon, kEnd };
  struct Seg { int ph; Kind k; double d; };
  struct Ctl { double f1, f2, f3, av, an, ah, nf, nb, nz; };
  void block(float* L, float* R, size_t len, double a, double b, double rate);
  void enter();
  double ms(double x) const;
  void plan(const int16_t* list, uint32_t n, bool coda);   // appends to segs_
  double reso(int j, double x, double F, double B);
  double src1(double ph, double inc, double rob) const;
  double sr_; VoxP p_; VoxLyrics lyr_; std::string txt_; bool txtSet_ = false;
  float glot_[2049];
  std::vector<Seg> segs_; size_t nsegs_ = 0; size_t si_ = 0; double st_ = 0; bool hold_ = false, coda_ = false; bool hasCur_ = false; int curNu_ = -1; uint32_t curCoN_ = 0; std::vector<int16_t> curCo_;   // the playing syllable (its nucleus and a copy of its closing phonemes)
  double env_ = 0, envT_ = 0, ph_ = 0, lf_ = 0, vt_ = 0; bool go_ = true;
  Ctl k_, tg_; double r_[16]; double lp_ = 0, dc_ = 0, dcx_ = 0;
  double rnd_ = 12345, vph_ = 0, an1_ = 0, yp_ = 0, nl_ = 0, nl2_ = 0, jt_ = 0, shm_ = 1; int odd_ = 0; double cph_[8], cdp_[8];
};

}  // namespace thunder
#endif
