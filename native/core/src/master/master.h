// THE MASTER PROCESSOR (N1, unit U11): master-worklet.js ('thunder-master'), the app's own mastering chain and the safety limiter,
// as one C++ class with fixed storage. Resonance suppressor -> compressor -> limiter, stereo-linked, plus the BS.1770 loudness meters.
// THE CODE IS THE SPECIFICATION: every stage is the worklet's arithmetic (doubles, float32 where the JS keeps a Float32Array), line for
// line; the reference is native/tools/master-dump.mjs (the worklet itself in a Node vm) and ctest `master`.
//
//   Math functions: Math.exp / pow / round are js_exp / js_pow / js_round (bit for bit); Math.sin / cos / tan / log2 / log10 are std::
//   (V8 uses its own fdlibm port, glibc may differ in the last bit of a double: it shows only as a rare float32 rounding flip, see the test).
//
// BLOCKS. The worklet works on Web Audio's 128-frame quanta, and the suppressor spreads its frame's work over them (one stage a block), the
// meter message counts quanta. process() therefore cuts whatever block it is given into pieces of at most 128 frames and runs each as one
// quantum: given blocks that are multiples of 128 (a render, an audio callback of 128 / 256 / 512) it is the worklet's own sequence, bit
// for bit; any other length only moves where the suppressor's stage boundaries fall (the sound is the same to rounding).
//
// REAL-TIME RULES: the constructor allocates (the look-ahead buffers are sized for maxLookMs); set(), process(), popMeter() allocate
// nothing, take no lock and do no I/O. One thread calls them all (the audio thread; the host reads meters from the queue it fills there).
#ifndef THUNDER_MASTER_MASTER_H
#define THUNDER_MASTER_MASTER_H
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace thunder {

// ---- the message {set:{sup, comp, lim}}: what the worklet's apply() takes (a missing key keeps the stage's value; a stage given without `on` is OFF) ----
using MOpt = std::optional<double>;
using MBool = std::optional<bool>;
struct SupSet  { bool on = false; MOpt depth, sharp, sel, atk, rel, lo, hi, mix; MBool delta; };
struct CompSet { bool on = false; MOpt thr, ratio, atk, rel, knee, makeup, mix, hpf; MBool autoRel, autoMakeup; };   // autoRel = JS `auto`, autoMakeup = `automk`
struct LimSet  { bool on = false; MOpt drive, ceil, rel, look; MBool tp; };
struct MasterSet {
  bool hasSup = false, hasComp = false, hasLim = false;
  SupSet sup; CompSet comp; LimSet lim;
};
// the safety limiter's own setting (index.html SAFE_LIM): the limiter alone, on, 0 dB drive, -1 dB ceiling, 120 ms release, 3 ms look-ahead, true peak
MasterSet master_safe_limiter_set();

// the worklet's meter message: {meter:{inPk, outPk, tp, inLu, outLu, load, comp, compIn, lim, sup}}
constexpr int kSupBands = 48;
struct MasterMeter {
  double inPk = 0, outPk = 0, tp = 0;     // linear peaks since the last message (tp: since resetPeak, the true-peak read-out holds)
  double inLu = 0, outLu = 0;             // short-term loudness (LUFS, -inf before the first 100 ms block)
  double load = 0;                        // the processor's share of real time (half-second average; wall clock, not reproducible)
  double comp = 0, compIn = -200;         // compressor gain reduction (dB, <= 0, the lowest since the last message), detector's loudest level (dB)
  double lim = 0;                         // limiter gain reduction (dB, <= 0)
  bool hasSup = false; float sup[kSupBands] = {};   // suppressor reduction per band (dB, 20 Hz..20 kHz log spaced), when it is on
};

class Suppressor; class Compressor; class Limiter; class Loudness;

class MasterProcessor {
 public:
  static constexpr int kQuantum = 128;
  static constexpr int kMeterQueue = 8;
  // maxLookMs: the longest limiter look-ahead the storage holds (the app's slider goes to 10 ms); a larger request is cut to it.
  explicit MasterProcessor(double sampleRate, bool meters = true, double maxLookMs = 50.0);   // allocates (control thread)
  ~MasterProcessor();
  MasterProcessor(const MasterProcessor&) = delete;
  MasterProcessor& operator=(const MasterProcessor&) = delete;

  void set(const MasterSet& s);          // apply(): a NaN / Infinity value is ignored (the stage keeps its own); lo / hi / ratio <= 0 too
  void resetPeak() { tpMax_ = 0; }       // {resetPeak:true}
  void dispose() { dead_ = true; }       // {dispose:true}: process() then returns false and writes silence
  // One block in place (frames of any size, see BLOCKS). Returns false once disposed.
  bool process(float* L, float* R, int frames);

  // samples the chain delays the sound by, with the stages that are on now (the page's mstLatency: suppressor 2560, limiter its look-ahead)
  int latency() const;
  int latencySup() const;
  int latencyLim() const;
  bool supOn() const { return on_sup_; }
  bool compOn() const { return on_comp_; }
  bool limOn() const { return on_lim_; }
  // the meter messages posted since the last pop (about 20 a second); oldest first. false: none.
  bool popMeter(MasterMeter* out);
  // M2: the meters (the BS.1770 loudness filters, the peaks, the reduction read-outs and the message queue) on or off, at run time, from the audio thread: a flag flip, no allocation. On an edge the running
  // peaks / reductions and the queue are cleared, and at `on` the two loudness windows start empty (lead's read: scalars and fixed arrays only), so the first message is fresh and LUFS reads none until 100 ms
  // have passed. The audio is bit-equal either way (the meters only read).
  void setMeters(bool on);
  bool metersOn() const { return meters_; }
  uint32_t meterDropped() const { return mdrop_; }   // messages lost because the host did not pop (the queue is kMeterQueue deep: the oldest goes)
  uint32_t silentBlocks() const { return sk_; }      // quanta the SILENCE SHORTCUT skipped (for tests)

 private:
  bool quantum(float* L, float* R, int n);
  double sr_; bool meters_;
  std::unique_ptr<Suppressor> sup_; std::unique_ptr<Compressor> comp_; std::unique_ptr<Limiter> lim_;
  std::unique_ptr<Loudness> inLu_, outLu_;
  bool on_sup_ = false, on_comp_ = false, on_lim_ = false, dead_ = false;
  double inPk_ = 0, outPk_ = 0, tpMax_ = 0, compGr_ = 0, nq_ = 0, ms_ = 0, fr_ = 0, load_ = 0;
  double zr_ = 0; uint32_t sk_ = 0;
  MasterMeter q_[kMeterQueue]; int qh_ = 0, qn_ = 0; uint32_t mdrop_ = 0;
};

}  // namespace thunder
#endif
