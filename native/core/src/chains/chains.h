// U31: THE …CHAINS ENGINE (chains/chains-worklet.js outside the shared module block): ChainsEngine (the patch of one track's effect chain run as a graph, with the per-track buses, the
// sends and sources, bypass and the run list of a node), ChainsProcessor's mix-down (chains_mixdown), ThunderChains' per-track wrapper (ChainsNode) and the live runner (ChainsLive, clive.cpp).
// THE JS IS THE SPECIFICATION, quirks included (docs/port/U31-NOTES.md lists what is decided differently and why).
//
// REUSE. The graph is U30's: ChainsEngine runs a waves::PatchImpl (waves/graph.h: Graph.compile, _gather, the module slots) built from the …chains module table (ctable.inc, generated from the JS by
// tools/chains-gen.mjs). The modules are the MODFX ones (U19 to U21), the 33 track-effect wrappers and the control modules U30 ported (wmods.cpp), and the …chains-only ones of cmods.cpp.
//
// REAL-TIME RULES. A patch is PREPARED off the audio thread (loadPatch / preparePatch): every module object is made then (the effects, the pump instances, the buffers of Repeat and Resample, the FFT
// tables). process() allocates nothing, takes no lock and does no I/O. What the JS does on the audio thread that allocates is kept out of it: see the notes ("Spare pump instances", "Resample", "Expression").
#ifndef THUNDER_CHAINS_CHAINS_H
#define THUNDER_CHAINS_CHAINS_H
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "modfx/modfx.h"
#include "rt/json.h"
#include "waves/waves.h"

namespace thunder {
namespace waves { class PatchImpl; struct ModTable; }
namespace chains {

using waves::Host;
using waves::Quality;
using waves::Transport;
using waves::WCtx;
using waves::WInfo;
using modfx::ParamDef;
using modfx::PortDef;
using waves::kWF_Destroy; using waves::kWF_Effect; using waves::kWF_PlaysNotes; using waves::kWF_Probe; using waves::kWF_Show; using waves::kWF_Tail; using waves::kWF_Text;

constexpr int kBlock = 128;          // BLOCK
constexpr int kChannels = 128;       // CHAN_COUNT: the tracks a patch can address (Track In, Chain Out, Sidechain In's Source)
constexpr int kSends = 8;            // NSEND
constexpr int kSckMax = kSends + kChannels;   // SCK_MAX
constexpr int kBypSwitches = 15;     // the tracker's BYP switches b1..b15
constexpr int kMaxFeeds = 32;        // ThunderChains: bus feeds from other tracks (inputs 4 and 5, 16 tracks each)
constexpr int kMaxTaps = kChannels;  // input 2: one channel per track a Sidechain In listens to

// The module tables of …chains (ports, parameters, flags: generated) and the factory of a type (the chains-only modules first, then the shared ones).
const WInfo* cinfo(std::string_view type);
int cinfo_count();
const WInfo& cinfo_at(int i);
const modfx::Def* cdef(std::string_view type);
bool chains_type_is_lib(std::string_view type);
bool chains_type_is_pump(std::string_view type);   // one of the 13 pump modules (they need the pump library, not the tfx one)
uint64_t chains_pump_swaps(); uint64_t chains_pump_bad(); uint64_t chains_pump_dry(); uint64_t chains_pump_overflow();   // overflow: dead handles that did not fit a module's four places (0 always; the test keeps it there)   // the pump modules' counters (process-wide): spares swapped in, blocks that went bad, bad blocks with no spare ready
double chains_module_bytes(std::string_view type, double sr);   // the heap a module of this type holds (cmem.inc: measured by tests/test_chains_mem.cpp, with a margin): the memory budget's unit
constexpr size_t kDefaultMemoryBudget = (size_t)1 << 30;   // 1 GiB per patch (a …chains patch holds one module object each; Resample 23 MiB, Convolve 66 MiB, Pgrain 57 MiB at 48 kHz, more at higher rates)
bool chains_have_lib(std::string_view type);        // this build can run it: the library the type needs is in the build (a closed build has none)
const waves::ModTable& chainsTable();
void resample_release(const struct ResampleTake& t);   // cmods2.cpp   // a module built from a strict upstream library (the tfx effects, the pump modules, Marbles / Tides): not bit-exact with the web's fast-math wasm

// A finished Resample take handed to the host (the JS's ctx.engine.onResample): the buffers belong to the module until releaseTake().
struct ResampleTake { char id[48]; const float* L; const float* R; int n; double sr; int bars; double tempo; int pad; int slices; void* owner; int which; };   // owner / which: the module and the buffer pair, for releaseTake

// What the …chains-only modules reach (the JS's ctx members): set by the engine for every block.
struct CCtx : WCtx {
  const float* inL = nullptr; const float* inR = nullptr; int inChans = 0;   // ctx._inL / _inR: kBlock floats per channel, inChans channels (null: none: Track In is silence)
  float* busesL = nullptr; float* busesR = nullptr; uint8_t* bw = nullptr; int* bwl = nullptr; int* nbw = nullptr;   // the buses and the list of the ones written this block (ctx.busL / busR)
  float* sendOut[kSends] = {};     // ctx.sendOut(n)
  const float* sendIn[kSends] = {}; const float* taps[kMaxTaps] = {}; int nTaps = 0; const int16_t* tapMap = nullptr;   // ctx.sendIn / ctx.tap
  const float* zero = nullptr;     // ctx._zero
  void* takeUser = nullptr; void (*onTake)(void* user, const ResampleTake& t) = nullptr; const char* curId = "";   // Resample: where a take goes
  float* busL(int k) const { if (!bw[k]) { bw[k] = 1; bwl[(*nbw)++] = k; } return busesL + (size_t)k * kBlock; }
  float* busR(int k) const { if (!bw[k]) { bw[k] = 1; bwl[(*nbw)++] = k; } return busesR + (size_t)k * kBlock; }
};

struct EngineOptions { Transport transport; Quality quality = Quality::Full; int focus = -1; };
struct Stats { uint64_t blocks = 0, failedBlocks = 0, refusedPatches = 0, refusedCalls = 0, droppedEvents = 0, droppedTakes = 0; };   // droppedTakes: a finished Resample take that found the 16-place take queue full (its buffers are marked free again in place: nothing is freed, nothing allocated)

class ChainsEngine {
 public:
  ChainsEngine(double sampleRate, const EngineOptions& o = EngineOptions(), Host* host = nullptr);   // host null: its own Host
  ~ChainsEngine();
  ChainsEngine(const ChainsEngine&) = delete; ChainsEngine& operator=(const ChainsEngine&) = delete;
  bool valid() const { return valid_; }
  // ---- the JS's methods (control thread; they allocate) ----
  // The memory budget (bytes; a PATCH is refused, with lastError() saying why, when its modules would need more: counted before anything is allocated). A failed allocation (std::bad_alloc) while a patch is
  // prepared is caught at that boundary too and refuses the patch (where the build has exceptions: GCC / Clang; the core is built -fno-exceptions, only this file has them). Never on the audio thread.
  void setMemoryBudget(size_t bytes) { memBudget_ = bytes ? bytes : kDefaultMemoryBudget; }
  size_t memoryBudget() const { return memBudget_; }
  const std::string& lastError() const { return lastError_; }
  bool loadPatch(Json doc);     // false: refused (the engine is left as it was)
  bool addModule(std::string_view id, std::string_view type, Json o = Json());   // o: {params, text}; unknown type: nothing (as the JS)
  void removeModule(std::string_view id);
  void connect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort, bool fb = false);
  void disconnect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort);
  void setParam(std::string_view id, std::string_view name, double v);   // (no allocation: the live runner calls it on the audio thread)
  void setText(std::string_view id, std::string_view text);
  void setTransport(const Transport& t);   // Object.assign, and startN + 1 when it starts playing
  void setTransportTempo(double t) { transport_.tempo = t; }
  void setQuality(Quality q) { quality_ = q; }
  void setTelemetry(bool on);
  void setFocus(int track);
  bool handleMessage(Json m);   // the protocol's ops: patch, add, remove, connect, disconnect, param, text, params, quality, transport, telemetry, tapmap, busmap
  void setTapMap(const int* tracks, int n);   // the host's 'tapmap': channel i of input 2 carries tracks[i]'s own sound (never called: channel k is track k)
  void setBusMap(const int* tracks, int n);   // ThunderChains' 'busmap': the other tracks this node takes in
  const std::vector<int>& busMap() const { return busMap_; }
  void dispose();
  // ---- audio thread ----
  void setTrackInputs(const float* L, const float* R, int channels) { trackL_ = L; trackR_ = R; trackChans_ = channels; }
  const float* sidechainBuffer(int side) const { return side ? scR_ : scL_; }   // (test access) the host's input 1 as setSidechain stored it, kBlock floats
  void setSidechain(const float* L, const float* R, int n);   // the host's input 1 (stereo); no module of the JS reads it, kept as the JS has it
  void setTaps(const float* const* taps, int n);               // input 2: the tracks' own sounds, one channel each (null: none)
  void endStandaloneBlock();   // ChainsProcessor's p1 after process(): the sends of this block become the next block's Sidechain In input (one block late, as the JS)
  void setSendIn(const float* const* sends, int n);            // input 3: the sends of every chain, one channel each (null entries: silence)
  double bypassSwitchState(int n) const { return n >= 1 && n <= kBypSwitches ? bsw_[n] : -1; }   // (diagnostics, tests)
  void setBypassSwitch(int n, double v) { if (n >= 1 && n <= kBypSwitches) bsw_[n] = v; }   // the tracker's BYP switches: -1 the patch's own, 0 in, 1 bypassed, 2 flipped
  bool process(int frames = kBlock);   // false: refused (frames outside 0..128) or a module failed (the buses are cleared, as the worklet's catch does)
  const float* bus(int slot) const { return busesL_.get() + (size_t)(slot - 1) * kBlock; }   // slot 1..128
  const float* busR(int slot) const { return busesR_.get() + (size_t)(slot - 1) * kBlock; }
  const float* sendOut(int n) const { return sendOut_ + (size_t)n * kBlock; }
  const int* writtenBuses(int* n) const { *n = nbw_; return bwl_; }   // the buses written this block, in the order they were first written (ChainsProcessor sums them in this order)
  // Resample's takes (control thread): the take the audio thread finished, and giving its buffers back
  bool popTake(ResampleTake* t);
  void releaseTake(const ResampleTake& t);
  // ---- inspection (tests) ----
  std::vector<std::string> order() const;
  std::vector<waves::Cable> cables() const;
  uint32_t graphVer() const;
  int patchModules() const; int patchCables() const;
  int instances() const;
  const Transport& transport() const { return transport_; }
  Quality quality() const { return quality_; }
  Host* host() { return host_; }
  double sampleRate() const { return sr_; }
  const Stats& stats() const { return stats_; }
  bool isActive(std::string_view id) const;                 // in the run list (focus)
  bool isBypassed(std::string_view id) const;               // io.byp
  int bypassNumber(std::string_view id) const;              // _bnMap(): 1..15 or 0
  double param(std::string_view id, std::string_view name) const;   // NaN if there is none
  float* outBuf(std::string_view id, std::string_view port);
  float* inBuf(std::string_view id, std::string_view port);
  // The spare pump instances (decision (e) of U18): the control thread keeps every pump module's spare current; the audio thread swaps it in when a module's output goes bad. Call regularly.
  void setCarryFade(bool on) { carryFade_ = on; }   // the live runner cuts blocks at events: a Bypass crossfade then goes on across the cuts (a 128-sample ramp whatever the cuts); off for the engine API, as the JS (its blocks are whole)
  void setInlineService(bool on) { inlineService_ = on; }   // true (the default, offline): process() services the spares itself at the start of each block (one thread, as the JS makes its new instance at the next block); the live runner turns it off and its control thread calls serviceSpares()
  void serviceSpares();
  uint64_t spareSwaps() const { return spareSwaps_.load(); }
  uint64_t audioRemakes() const { return audioRemakes_.load(); }   // pump_new on the audio thread: 0 here, always (a swap needs no allocation; with no spare left the module is silenced for the block)
  // ---- live: swap a whole prepared patch in (audio thread, at a block start) ----
  waves::PatchImpl* swapPatch(waves::PatchImpl* p);       // takes p; returns the old one (free it with chains_free_patch on the control thread)
  void serviceSparesOf(waves::PatchImpl* p);              // the control thread's service of the patch the audio thread runs now
  waves::PatchImpl* currentPatch() const { return curA_.load(std::memory_order_acquire); }   // the patch the audio thread runs (set whenever p_ changes: construction, dispose, swapPatch / loadPatch)
  waves::PatchImpl* preparePatch(Json doc) const;         // off the audio thread: what loadPatch builds, without touching this engine's patch
  friend class ChainsLive;
 private:
  struct TakeQ;   // finished Resample takes
  bool render(int frames);
  void refresh();
  static const float* keyCb(void* user, double sck);
  void bindCtx();
  void prepareDerived(waves::PatchImpl& P) const;   // the run list and the bypass numbers of a patch (control thread; allocation-free after compile)
  double sr_; bool valid_ = true; Host* host_; std::unique_ptr<Host> ownHost_;
  Transport transport_; Quality quality_; bool telemetryOn_ = false; int focus_ = -1;
  std::unique_ptr<waves::PatchImpl> p_;
  std::unique_ptr<float[]> busesL_, busesR_; uint8_t bw_[kChannels] = {}; int bwl_[kChannels] = {}; int nbw_ = 0;
  float sendPrev_[kSends * kBlock] = {}; bool standalone_ = false; const float* sendInExt_[kSends] = {}; bool sendInSet_ = false;
  float sendOut_[kSends * kBlock] = {}; float zero_[kBlock] = {}; float scL_[kBlock] = {}, scR_[kBlock] = {}; bool scOn_ = false;
  const float* trackL_ = nullptr; const float* trackR_ = nullptr; int trackChans_ = 0;
  const float* sendIn_[kSends] = {}; const float* taps_[kMaxTaps] = {}; int nTaps_ = 0; int16_t tapMap_[kChannels]; bool hasTapMap_ = false;
  std::vector<int> busMap_; double bsw_[kBypSwitches + 1];
  std::atomic<waves::PatchImpl*> curA_{nullptr};
  CCtx ctx_; Stats stats_; bool lastFailed_ = false; size_t memBudget_ = kDefaultMemoryBudget; mutable std::string lastError_;
  std::atomic<uint64_t> spareSwaps_{0}, audioRemakes_{0}; bool inlineService_ = true, carryFade_ = false;
  std::unique_ptr<TakeQ> takes_;
};
void chains_free_patch(waves::PatchImpl* p);   // deletes a patch preparePatch / swapPatch handed out (PatchImpl is private to the engines)

// ChainsProcessor's mix-down: every bus written this block summed (float32 adds, in the order they were first written), then soft() on each side. L / R: n floats.
void chains_mixdown(const ChainsEngine& e, float* L, float* R, int n);

// ---- ThunderChains: one node per track (inp -> thunder-chains -> fx -> g) ----------------------------------------------------------------------------------------------------------------------
// The streams of one node's block, as the worklet's inputs / outputs were. A null pointer is an input that is not connected (silent). Lengths: n frames each.
struct NodeIn {
  const float* inL = nullptr; const float* inR = nullptr;                 // input 0: this track's sound (inR null: inL on both sides)
  const float* scL = nullptr; const float* scR = nullptr;                 // input 1: the sidechain bus
  const float* const* taps = nullptr; int nTaps = 0;                      // input 2: the tracks' own sounds
  const float* const* sends = nullptr; int nSends = 0;                    // input 3: the sends of every chain (a block late, from the host)
  const float* const* feeds = nullptr; int nFeeds = 0;                    // inputs 4 and 5: the other tracks this node takes in, a stereo pair each (feeds[2j], feeds[2j + 1]), in busMap's order
  const double* byp = nullptr;                                            // b1..b15 (15 values) or null
};
struct NodeOut { float* L = nullptr; float* R = nullptr; float* const* sends = nullptr; int nSends = 0; };   // L / R: this track's bus; sends: the Sidechain Outs
class ChainsNode {
 public:
  ChainsNode(double sr, int track, const EngineOptions& o = EngineOptions(), Host* host = nullptr);   // track 0..127; focus = track
  ChainsEngine& engine() { return eng_; }
  const ChainsEngine& engine() const { return eng_; }
  int track() const { return track_; }
  // The worklet's process(ins, outs, params): any n (cut into chunks of kBlock; more than 4096 frames fails soft: silence, counted)
  bool process(const NodeIn& in, NodeOut& out, int n);
  uint64_t failedLong() const { return failedLong_; }
 private:
  bool p1(const NodeIn& in, NodeOut& out, int n, int a, int len);
  ChainsEngine eng_; int track_;
  std::unique_ptr<float[]> tmpL_, tmpR_; int tw_[kMaxFeeds + 1]; int ntw_ = 0; uint64_t failedLong_ = 0;
};


// ---- the live runner (ChainsLive): one track's node with sample-exact events -----------------------------------------------------------------------------------------------------------------------
// WHAT THE WORKLET DOES: one ThunderChains node runs a 128-frame block per quantum; a parameter, the tempo or a BYP switch changes at a block's start whatever sample it was meant for (the page's
// Thunder Core queue lands a tempo within 64 samples). Here an event lands on its own sample (the block is cut there) and the output is delayed by kChainsLatency = 127 samples, as U26 and U30 (a block
// is rendered only when no event not yet seen can cut it, so the output does not depend on the host's block size). The host's inputs go through a ring with the same delay. The offline render of the
// same events (ChainsLive::renderOffline) is the same loop with no queue and no latency.
struct ChainsEvent {
  enum Kind : uint8_t { Param, Tempo, Playing, Bypass, Patch };
  Kind kind = Param; int64_t at = 0;   // at: the engine sample the event lands on
  double a = 0, b = 0;                 // Param: a the value; Tempo: a bpm; Playing: a 1 or 0; Bypass: a the switch 1..15, b its value (-1 the patch's own, 0 in, 1 bypassed, 2 flipped); Patch: a the mailbox slot preparePatch gave
  char id[40] = {}; char name[24] = {};   // Param: the module id and the parameter name (a longer one is refused by post())
};
constexpr int kChainsLatency = 127, kChainsQueue = 1024, kChainsPending = 1024, kChainsSlots = 8, kChainsRing = 512, kChainsReserve = 256;   // kChainsReserve: places only an event that must not be lost may take
struct ChainsLiveConfig { int taps = 0; bool sc = false; bool sends = false; int feeds = 0; };   // which of the node's inputs the host feeds (fixed for the runner's life)
// A snapshot of the runner's counters (stats() copies them; each is written with a relaxed atomic add by whichever thread counts it, so reading from any thread is safe; the numbers of one snapshot are not one instant).
// droppedEvents: events lost (refused by post() / drain when nothing could give way, plus a Param refused for room); droppedParams: a pending Param of ANOTHER parameter removed to make room for an event that must be kept
// (a loss, unlike coalescedEvents: a value replaced by a later value of the same thing, or a Tempo / Bypass / Playing a later one of its kind supersedes).
struct ChainsLiveStats { uint64_t blocks = 0, cutBlocks = 0, lateEvents = 0, droppedEvents = 0, refusedPatches = 0, failedBlocks = 0, coalescedEvents = 0, deferredPatches = 0, droppedParams = 0; };
class ChainsLive {
 public:
  ChainsLive(double sampleRate, int track, const ChainsLiveConfig& cfg = ChainsLiveConfig(), Host* host = nullptr);
  ~ChainsLive();
  ChainsLive(const ChainsLive&) = delete; ChainsLive& operator=(const ChainsLive&) = delete;
  bool valid() const { return node_.engine().valid(); }
  ChainsNode& node() { return node_; }
  // CONTROL THREAD: post(), postParam(), preparePatch(), service() and loadPatch() are for ONE thread (the queue is single-producer: the host must call them from one thread, or serialise them with its own lock). A debug build
  // (no NDEBUG) aborts with a message when a second thread calls them. The audio thread calls only process().
  bool post(const ChainsEvent& e);                                    // false: the queue is full (counted), or a Param's id / name is too long
  void setMemoryBudget(size_t bytes) { node_.engine().setMemoryBudget(bytes); }   // per patch prepared by this runner (see ChainsEngine::setMemoryBudget)
  bool postParam(const char* id, const char* name, double v, int64_t at);
  bool loadPatch(Json doc) { return node_.engine().loadPatch(doc); }   // BEFORE the audio thread runs (control thread): a patch loaded straight into the engine; the engine tracks the current patch itself
  bool preparePatch(Json doc, int64_t at);                            // builds the patch off the audio thread and posts its swap at `at`; false: refused (the playing patch stays)
  // REQUIRED RATE: service() must run at least once per engine block of 128 frames (every 2.67 ms at 48 kHz; any faster is better) for as long as a pump module can go bad. A pump module holds ONE spare; a bad block uses it (silent for
  // that block), and the next spare is made only by service(): called every block, no block ever runs dry (measured: 400 blocks, 0 dry); every 3rd block, a third of the bad blocks found no spare (132 of 400 were silent);
  // never: the module is silent from its second bad block on. It also frees the patches the audio thread gave back: with fewer than kChainsSlots - 2 (6) patches in flight, preparePatch() works at any rate; past that it returns false (two places stay spare: one for the patch being handed over, one for the new patch's own swap).
  void service();                                                     // frees the patches the audio thread gave back and keeps the pump spares current (one control thread calls this at the rate above, and no other frees patches)
  // audio thread: no allocation, no lock. n: any number of frames. in.byp is ignored (BYP comes as events)
  void process(const NodeIn& in, NodeOut& out, int n);
  int latency() const { return kChainsLatency; }
  ChainsLiveStats stats() const;   // a snapshot (any thread)
  int64_t enginePos() const { return posPub_.load(std::memory_order_relaxed); }   // the engine sample the audio thread has rendered up to (a copy it publishes after each segment: any thread may read it)
  double bypassSwitch(int n) const   /* (diagnostics: only while process() is not running: the audio thread writes it) */ { return n >= 1 && n <= kBypSwitches ? byp_[n - 1] : -1; }   // the value the last Bypass event gave switch n (1..15; -1: the patch's own)
  // (while the audio thread hands a patch over it holds one out of both arrays for a moment: the count can be one short then, which is why preparePatch() keeps two places spare)
  int patchesInFlight() const { int n = 0; for (int i = 0; i < kChainsSlots; i++) { if (slot_[i].load()) n++; if (retired_[i].load()) n++; } return n; }   // prepared and not yet swapped in, plus swapped out and not yet collected by service()
  // the same events rendered with no queue and no latency (control thread: allocates). in[c]: the host's channels in the order of the node's inputs: Track In L, R, then sc L, R (cfg.sc), taps, sends (cfg.sends: 8), feeds (2 each); each `frames` long
  static bool renderOffline(double sr, int track, const ChainsLiveConfig& cfg, Host* host, const std::vector<ChainsEvent>& events, const std::vector<Json>& patches, const std::vector<std::vector<float>>& inputs,
                            int64_t frames, std::vector<std::vector<float>>& outputs /* L, R, sends 0..7 */, Json initialPatch = Json(), const std::vector<int>* busMap = nullptr, const std::vector<int>* tapMap = nullptr);
 private:
  void drain();
  void eraseAt(int j); int victim(const ChainsEvent& e) const; void dropPatch(const ChainsEvent& e);
  void apply(const ChainsEvent& e);
  void segment(int64_t end);
  int nIn() const { return 2 + (cfg_.sc ? 2 : 0) + cfg_.taps + (cfg_.sends ? 8 : 0) + 2 * cfg_.feeds; }
  ChainsNode node_; double sr_; ChainsLiveConfig cfg_;
  struct Ctr { std::atomic<uint64_t> blocks{0}, cutBlocks{0}, lateEvents{0}, droppedEvents{0}, refusedPatches{0}, failedBlocks{0}, coalescedEvents{0}, deferredPatches{0}, droppedParams{0}; } ctr_;
  void ctlCheck();   // debug builds: one control thread
  std::atomic<size_t> ctlThread_{0};
  ChainsEvent q_[kChainsQueue]; std::atomic<uint32_t> qh_{0}, qt_{0};
  ChainsEvent pend_[kChainsPending]; int npend_ = 0;
  std::atomic<waves::PatchImpl*> slot_[kChainsSlots]; std::atomic<waves::PatchImpl*> retired_[kChainsSlots]; 
  int64_t pos_ = 0, hostPos_ = 0; double byp_[kBypSwitches]; std::atomic<int64_t> posPub_{0};
  std::unique_ptr<float[]> inRing_, outRing_, seg_, oseg_;   // inRing_: nIn() x kChainsRing; outRing_: 10 x kChainsRing; seg_ / oseg_: the segment's contiguous copies (128 each)
  std::vector<const float*> ptrIn_; std::vector<float*> ptrOut_;
  friend struct ChainsLiveOffline;
};

}  // namespace chains
}  // namespace thunder
#endif
