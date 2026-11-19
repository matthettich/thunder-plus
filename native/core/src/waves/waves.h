// U30: THE …WAVES ENGINE (waves/waves-worklet.js outside the shared module block): the graph (Graph), the voice pool (VoicePool), the engine (WavesEngine: patch -> voices -> buses), the
// waves-only modules, the worklet's mix-down (WavesProcessor) and the live runner (WavesLive, wavesl). THE JS IS THE SPECIFICATION, quirks included (docs/port/U30-NOTES.md lists them).
// The shared MODFX modules are the ones of native/core/src/modfx (U19 to U21), run through the same Def / Module / IO / Ctx; the waves-only ones (VCO ... Out, the 33 tfx effects, Expression,
// Pitch delay / reverb, Scope) are in wmods.cpp, with their port and parameter tables GENERATED from the JS (wtable.inc, tools/waves-gen.mjs).
//
// REAL-TIME RULES. A patch is PREPARED off the audio thread (WavesEngine::loadPatch, or WavesPatch::build for the live runner): every module object of every voice is made then. The audio thread's
// process() allocates nothing, takes no lock and does no I/O: the one exception that is kept from the JS is named in the notes (Expression's text change compiles on the audio thread).
// The JS makes a module's state at the module's first block and draws its random seeds then; here the object is made ahead and `arm()`ed (the seed drawn) at that same moment, so the
// order of the draws is the JS's.
#ifndef THUNDER_WAVES_WAVES_H
#define THUNDER_WAVES_WAVES_H
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "modfx/modfx.h"
#include "rt/json.h"

namespace thunder {
namespace waves {

using modfx::Def;
using modfx::Module;
using modfx::ParamDef;
using modfx::PortDef;

constexpr int kBlock = 128;          // BLOCK
constexpr int kBuses = 8;            // BUS_COUNT
// Limits the JS does not have (its memory is the limit there); a patch or call beyond one is REFUSED, never a crash (docs/port/U30-NOTES.md "Refusals"):
constexpr int kMaxVoices = 64;
constexpr int kMaxModules = 1024;
constexpr int kMaxCables = 16384;
constexpr int kMaxInstances = 4096;   // module objects made for a patch (a poly module counts one per voice)
constexpr int kMaxWeight = 4096;      // sum of the instances' weights (heavy modules count more): a stand-in for the memory budget
constexpr int kMaxText = 200;         // Expression's text (the JS cuts it at 200 UTF-16 units)

enum WFlags { kWF_Effect = 1, kWF_PlaysNotes = 2, kWF_Probe = 4, kWF_Text = 8, kWF_Tail = 16, kWF_Show = 32, kWF_Destroy = 64 };
struct WInfo {
  const char* type; int scope; int flags;     // scope: 0 mono, 1 poly, 2 root
  int nin; const PortDef* in; int nout; const PortDef* out; int np; const ParamDef* params;
};
const WInfo* winfo(std::string_view type);       // the table of one module type (generated from the JS), nullptr for an unknown type
int winfo_count();
const WInfo& winfo_at(int i);
bool waves_type_is_fx(std::string_view type);    // one of the 33 track-effect wrappers (they need the effects library; their output is not bit-exact off the reference build)
const Def* wdef(std::string_view type);          // the module factory: modfx's registry, else the waves-only ones
int wport_in(const WInfo& w, std::string_view name);
int wport_out(const WInfo& w, std::string_view name);
int wparam(const WInfo& w, std::string_view name);

// One row of the voice pool (the JS's {pitch, gate, vel, age, offAt} plus the `quiet` and `lane` the engine and the live node hang on it).
struct VoiceRow { double pitch = 0, gate = 0, vel = 0, age = 0, offAt = -1e12, quiet = 0; int lane = -1; };

class VoicePool {
 public:
  explicit VoicePool(int max);
  int on(double pitch, double vel);          // returns the row (the JS's on())
  void off(double pitch);
  void allOff();
  bool awake(int i, double hold) const { const VoiceRow& r = rows[(size_t)i]; return r.gate > 0 || now - r.offAt < hold; }
  int max;
  double clock = 0, now = 0;
  std::vector<VoiceRow> rows;
};

// What the JS keeps in module variables of the scope the script runs in (so it is shared by every engine of that scope, as there): the seed counter, Math.random, and the state of the
// tfx wasm INSTANCE (one per scope: wasi's rand(), stmlib's Random, DaisySP's myrand). A test makes one Host per "page".
class Host {
 public:
  enum class Rnd { Xorshift, Mulberry, Fixed };
  Host() = default;
  uint32_t nextSeed();                          // seedN = (seedN * 1103515245 + 12345) >>> 0
  double random();                              // Math.random
  void setRandom(Rnd kind, uint32_t seed);      // Xorshift: tests/helpers.js freezeClock (0x9e3779b9); Mulberry: the page's renders (index.html wavesPre / wavesLiveRender)
  void setFixed(double v) { rndKind = Rnd::Fixed; fixed = v; }   // Math.random() === v (a test's way to make a skipped module not shift the others' draws)
  double fixed = 0.25;
  uint32_t seedN = 12345;
  Rnd rndKind = Rnd::Xorshift; uint32_t rndS = 0x9e3779b9u;
  uint64_t wasiRand = 0; uint32_t stmRng = 0x21, daisySeed = 1;   // the effects' instance state
  // Is the strict effects library there (THUNDER_HAVE_TFX)? Is Plaits / Elements (THUNDER_ENGINE_PE)? Without them the modules bypass / are silent, as the JS's without its wasm.
  static bool haveEffects();
  static bool havePE();
};

// The part of the engine's ctx the waves-only modules read (the MODFX modules read only the Ctx base).
class WavesEngine;
struct WCtx : modfx::Ctx {
  WavesEngine* eng = nullptr;                   // ctx.engine (the Sequencer plays voices through it)
  const VoiceRow* notes = nullptr; int nNotes = 0; int voice = 0;
  float* busL[kBuses] = {}; float* busR[kBuses] = {};
  Host* host = nullptr;
  const uint16_t* text = nullptr; int textLen = -1;   // io.inst.text (UTF-16 units); textLen < 0: undefined
  bool draft = false;                           // ctx.quality === 'draft'
};
// The seeds a state is made with (the placeholder) and then armed with (the real counter).
class WSeeds : public modfx::Seeds {
 public:
  uint32_t next() override { drawn++; return host && armed ? host->nextSeed() : 1; }
  Host* host = nullptr; bool armed = false; uint32_t drawn = 0;
};
class WModule : public Module {   // a waves-only module (wmods.cpp)
 public:
  virtual void destroy(WavesEngine*) {}
};

struct Transport { double tempo = 120, lpb = 4, ticks = 6, ppq = 0, startN = 0; bool playing = false; };
enum class Quality { Full, Draft, Other };

struct EngineOptions {
  int maxVoices = 8; bool hasSeed = false; double seed = 0; Transport transport; Quality quality = Quality::Full;
  bool hosted = false;
};
struct Stats { uint64_t blocks = 0, failedBlocks = 0, refusedPatches = 0, refusedCalls = 0, droppedEvents = 0; };

struct Cable { std::string aId, aPort, bId, bPort; bool fb = false; };   // as stored (ports of any other JSON type are kept as an unmatchable text)

class PatchImpl;
class WavesEngine {
 public:
  WavesEngine(double sampleRate, const EngineOptions& o, Host* host = nullptr);   // host null: its own Host
  ~WavesEngine();
  WavesEngine(const WavesEngine&) = delete; WavesEngine& operator=(const WavesEngine&) = delete;
  bool valid() const { return valid_; }
  // ---- the JS's methods (control thread; allocate) ----
  bool loadPatch(Json doc);     // false: refused (the engine is left as it was). The JS's returned {modules, cables} counts are patchModules() / patchCables()
  bool addModule(std::string_view id, std::string_view type, Json o = Json());   // o: {params, text}; unknown type: nothing (as the JS)
  void removeModule(std::string_view id);
  void connect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort, bool fb = false);
  void disconnect(std::string_view aId, std::string_view aPort, std::string_view bId, std::string_view bPort);
  void setParam(std::string_view id, std::string_view name, double v);
  void setText(std::string_view id, std::string_view text);
  void setTransport(const Transport& t) { transport_ = t; }
  void setTransportTempo(double t) { transport_.tempo = t; }
  void setQuality(Quality q) { quality_ = q; }
  void setTelemetry(bool on) { telemetryOn_ = on; }
  void setOnly(int slot) { onlyBus_ = slot | 0; cullDirty_ = true; }
  bool handleMessage(Json m);   // the protocol's ops (patch, add, remove, connect, disconnect, param, text, params, quality, transport, telemetry, noteOn, noteOff, panic)
  void noteOn(double pitch, double vel = 1) { voiceOn(pitch, vel); }
  void noteOff(double pitch) { voices_.off(pitch); }
  void allNotesOff() { voices_.allOff(); }
  int voiceOn(double pitch, double vel) { return voices_.on(pitch, vel); }
  void dispose();
  // ---- audio thread ----
  bool process(int frames = kBlock);   // false: refused (frames outside 0..128) or a module failed (the buses are cleared, as the worklet's catch does)
  const float* bus(int slot) const { return busesL_ + (slot - 1) * kBlock; }    // slot 1..8
  const float* busR(int slot) const { return busesR_ + (slot - 1) * kBlock; }
  const float* busesL() const { return busesL_; }
  const float* busesR() const { return busesR_; }
  // ---- inspection (tests) ----
  VoicePool& voices() { return voices_; }
  const VoicePool& voices() const { return voices_; }
  Quality quality() const { return quality_; }
  const Transport& transport() const { return transport_; }
  std::vector<std::string> order() const;                  // graph.order (ids)
  std::vector<Cable> cables() const;                       // graph.cables
  bool isPoly(std::string_view id) const;
  uint32_t graphVer() const;
  int patchModules() const; int patchCables() const;
  std::vector<std::string> onlyIds() const;                // the ids `only` holds, sorted; has_only(): `only` is not null
  bool hasOnly() const { return only_; }
  void cullNow();                                          // _cull()
  int instances() const;                                   // module objects held
  Host* host() { return host_; }
  double sampleRate() const { return sr_; }
  int maxVoices() const { return voices_.max; }
  const Stats& stats() const { return stats_; }
  double param(std::string_view id, std::string_view name) const;   // NaN if there is none
  // test hooks: the buffers of a module (`E._bufs(m, v).out[port]`), and one gather
  float* outBuf(std::string_view id, int voice, std::string_view port);
  float* inBuf(std::string_view id, int voice, std::string_view port);
  void gatherNow(std::string_view id, int voice);
  // ---- live: swap a whole prepared patch in (audio thread, at a block start); returns the one it replaced (the control thread deletes it) ----
  PatchImpl* swapPatch(PatchImpl* p);          // takes p; returns the old one (free it with waves_free_patch on the control thread)
  PatchImpl* preparePatch(Json doc) const;     // the caller owns it (waves_free_patch)   // off the audio thread: what loadPatch builds, without touching this engine's patch (the engine's voices, seed counter and sample rate are read)
  friend class PatchImpl;
  friend class WavesLive;
 private:
  void render(int frames);
  void doCull();
  double sr_; bool valid_ = true; Host* host_; std::unique_ptr<Host> ownHost_;
  Transport transport_; Quality quality_; bool telemetryOn_ = false; bool hosted_;
  VoicePool voices_; std::unique_ptr<PatchImpl> p_;
  int onlyBus_ = 0; bool only_ = false; bool cullDirty_ = false;
  float busesL_[kBuses * kBlock]; float busesR_[kBuses * kBlock];
  WCtx ctx_; Stats stats_; bool lastFailed_ = false;
};

// The worklet's mix-down: every bus summed (float32 adds in bus order), then soft() on each side. L / R: n floats.
void waves_mixdown(const WavesEngine& e, float* L, float* R, int n);
double waves_soft(double x);   // soft = x => { a = |x|; a < .8 ? x : sign(x) * (.8 + .2 * tanh((a - .8) / .2)) }
// fbAdd (the feedback guard): dst[i] += guarded(b[i] * g) for the block
void waves_fbadd(float* dst, const float* b, double g);


// ---- the live runner (wavesl): the page's thunder-wlive, with sample-exact events ----------------------------------------------------------------------------------------------------------------
// WHAT THE PAGE DOES (waves/waves-worklet.js, WavesLiveProc): four lanes (r rate, g gate, t counter, l legato, v velocity) arrive as AudioParam automation; per 128-frame block the first change in a lane
// starts a note AT THE BLOCK'S START WHATEVER SAMPLE IT WAS ON (block-quantised, up to 127 samples late), a lane's pitch is moved once per block to the one of the lane's rate at the block's END, and the
// engine runs one 128-frame block per quantum. Here an event lands on its own sample (the block is cut there) and the output is delayed by 127 samples, as U26 (docs/port/U26-NOTES.md): a block is rendered
// only when no event not yet seen can cut it, so the output does not depend on the host's block size. The offline render of the same events (WavesLive::renderOffline) is the same loop with no queue.
struct LiveEvent {
  enum Kind : uint8_t { NoteOn, NoteOff, Pitch, Tempo, Level, Drive, Bus, Patch };
  Kind kind = NoteOn; uint8_t lane = 0; bool legato = false; int64_t at = 0;   // at: the engine sample the event lands on
  double a = 0, b = 0;   // NoteOn: a pitch (semitones from C4), b velocity 0..1; Pitch: a pitch; Tempo: a bpm; Level: a dB; Drive: a 0..1; Bus: a 1..8; Patch: a the mailbox slot preparePatch gave
};
constexpr int kLiveLanes = 4, kLiveLatency = 127, kLiveQueue = 4096, kLivePending = 4096, kLiveSlots = 8;
void waves_free_patch(PatchImpl* p);   // deletes a patch preparePatch / swapPatch handed out (PatchImpl is private to engine.cpp)
double waves_live_pitch(double tune, double rate);   // the page's pitch(r): 36 + tune + 12 log2 r - 60 (r floored at 1e-6)
struct LiveStats { uint64_t blocks = 0, cutBlocks = 0, lateEvents = 0, droppedEvents = 0, refusedPatches = 0, failedBlocks = 0; };
class WavesLive {
 public:
  WavesLive(double sampleRate, int maxVoices, Host* host = nullptr);
  ~WavesLive();
  WavesLive(const WavesLive&) = delete; WavesLive& operator=(const WavesLive&) = delete;
  bool valid() const { return eng_.valid(); }
  // control thread
  bool post(const LiveEvent& e);                                // false: the queue is full (counted)
  bool preparePatch(Json doc, int64_t at);                      // builds the patch off the audio thread and posts its swap at `at`; false: refused (the playing patch stays)
  void collectRetired();                                        // frees the patches the audio thread gave back (and any prepared one that was never applied)
  // audio thread: no allocation, no lock. n is any number of frames
  void process(float* L, float* R, int n);
  int latency() const { return kLiveLatency; }
  const LiveStats& stats() const { return st_; }
  int64_t enginePos() const { return pos_; }
  WavesEngine& engine() { return eng_; }
  // the same events rendered with no queue and no latency: `frames` samples, L / R resized. (control thread: allocates)
  static bool renderOffline(double sr, int maxVoices, Host* host, const std::vector<LiveEvent>& events, const std::vector<Json>& patches, int64_t frames, std::vector<float>& L, std::vector<float>& R);
 private:
  void drain();
  void apply(const LiveEvent& e);
  void segment(int64_t end);
  friend struct LiveOffline;
  WavesEngine eng_; double sr_;
  LiveEvent q_[kLiveQueue]; std::atomic<uint32_t> qh_{0}, qt_{0};
  LiveEvent pend_[kLivePending]; int npend_ = 0;
  std::atomic<PatchImpl*> slot_[kLiveSlots]; std::atomic<PatchImpl*> retired_[kLiveSlots];
  int lane_[kLiveLanes]; int64_t pos_ = 0, hostPos_ = 0; double level_ = 0, drive_ = 0; int bus_ = 1;
  float ringL_[512], ringR_[512]; LiveStats st_;
};

// ---- the web page's offline renders of a waves patch (index.html wavesPre / wavesLiveRender) ----
struct WavesPreArgs { double dur, pA, ptau; double tune, vtune, fine, gate /* V.gate ms */; int bus; double tempo; };
std::vector<float> waves_pre_render(Json doc, const WavesPreArgs& a, double* rate);                              // {data, rate: 48000}; empty on a refusal / no patch
struct WavesLiveRenderArgs { int bus; double tune, level, drive, vel; double hold; double bpm; double sr; };
std::vector<float> waves_live_render(Json doc, const WavesLiveRenderArgs& a);

}  // namespace waves
}  // namespace thunder
#endif
