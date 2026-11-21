// THE PADS-AND-CHOP SNAPSHOT WITH DEFERRED FREE (U37 P1; prewire-19nov F2 + F3; U37-DESIGN-DRAFT 3.4 and the lead's ruling D5).
//
// THE PROBLEM. A voice borrows a pad's samples (VoiceBuf::data, an SF voice's PCM) and the sequencer borrows the chop table (SeqOpts::chop); the C API's RenderPads keeps both for the renderer's life, which a live
// host cannot: the page replaces a pad, or edits its slice points, while notes sound.
//
// THE DESIGN, in one paragraph. The CONTROL side owns every PadBuf (one pad's audio and its variations, or one pad's prepared SoundFont) and counts references to it; only snapshots (and the builder's own "current pad"
// table) hold references, and a reference moves on the control thread only. A PadsSnap is built whole on the control side (a RenderPads, the exact payload Players and Seq read today, plus the references that keep its
// buffers alive), published through a SnapSlot, adopted by the audio thread at the top of a quantum, and handed back through the slot to be deleted on the control side. A PadBuf nobody refers to any more is RETIRED, not
// freed: voices that started on it may still read it. Every voice that takes a PadBuf's data records the buffer's EPOCH (its own number, a counter that only grows, > 0) and its SLOT (the sampler's audio of pad p: p; the
// SoundFont data of pad p: 256 + p). Once a quantum the audio side publishes, per slot, the smallest epoch over its live or booked voices (kEpochNone: none). collect() frees a retired buffer only when its epoch is below
// the published minimum of ITS slot. Per slot, so a long sample on pad A holds pad A's old buffers only; nothing of pad B.
//
// THE FREE RULE, exactly. Buffer b (slot s, epoch e) is freed by collect() iff  refs(b) == 0  AND  e < minLive[s]   (minLive[s] = kEpochNone = 2^64-1 when no voice of slot s is alive: every retired buffer of the slot goes).
//   * Why `<`: a voice that started on b recorded e itself, so while one lives minLive[s] <= e and the test fails. A voice that started on a NEWER buffer of the slot (epoch > e) does not hold b: e < its epoch: b goes.
//     A voice that started on an OLDER buffer (epoch < e) keeps b too: the minimum cannot tell the two apart. That is a bounded over-retention (b lives as long as the oldest voice of its slot), never a use after free.
//   * Why it is safe: a voice can only start on a buffer of the CURRENT snapshot, whose references keep refs > 0; the snapshot is returned through the ring only after the audio side published the minima of that same
//     beginQuantum() call (scan, THEN adopt, then the ring push, which is a release); every voice that started on b before it was retired was alive at that scan or ended before it. The control side reads the minima
//     after popping the snapshot (acquire), so it sees that scan or a later one; a later one is as safe (voices only start on buffers that are in the current snapshot).
//   * Identical data: setPad with the same samples, rate and variations as the pad already has is a no-op (the buffer stays shared, same pointer, same epoch). A REPLACED pad whose data happens to be identical is a
//     new object with a new, larger epoch; the old one is retired and freed by the rule above (while a voice sounds on it, the new one is not blocked by it: its epoch is larger, and `e < min` is the OLD one's test).
//     A retired buffer is never revived: refs can only grow from a snapshot being built, and the builder holds the only reference of a pad that no published snapshot names (refs 0 means it is in neither).
//   * epoch 0 / slot -1 mean "not tracked" (SongRenderer's by-reference pads): such voices are ignored by the scan.
//
// THREAD RULES. Control thread: everything except beginQuantum(), one thread at a time (the one that calls thunder_post in the web / the shells' control thread). Audio thread: beginQuantum() only (and reading the
// snapshot it adopted). The audio side allocates nothing, takes no lock, frees nothing, throws nothing. All control-side allocations of the new code are `new (std::nothrow)` and return kNoMem; the two places that
// use a std::vector (the snapshot's RenderPads copy: the chop table's vector, `var`) are in pads_snap.cpp, which is built WITH exceptions like render.cpp and catches std::bad_alloc; a build without exceptions
// (the wasm one) would abort there on exhaustion (see docs/port/U37-P1-NOTES.md, open question 1).
#ifndef THUNDER_RENDER_PADS_SNAP_H
#define THUNDER_RENDER_PADS_SNAP_H
#include <atomic>
#include <cstdint>
#include <memory>

#include "render/render.h"
#include "rt/snap.h"
#include "sf/sf.h"

namespace thunder {

class Players;
class PadsHost;

// One variation (MUT / GEN) of a pad as the host hands it in: mono samples, copied.
struct PadVarIn { int ii = 0; char kind = 0; int num = 0; const float* mono = nullptr; int frames = 0; double sr = 0; };

// One pad's borrowed data. Immutable once a snapshot names it. Control thread only touches refs / retired link.
struct PadBuf {
  uint64_t epoch = 0;          // unique, grows with every PadBuf made (> 0)
  int slot = 0;                // 0..255: pad p's audio; 256 + p: pad p's SoundFont data
  // audio (slot < 256)
  float* mem = nullptr;        // L then R in one allocation
  int frames = 0; double rate = 0; bool stereo = false;
  struct Var { int ii = 0; char kind = 0; int num = 0; int frames = 0; double sr = 0; const float* data = nullptr; };
  Var* vars = nullptr; int nVars = 0; float* varMem = nullptr;
  // SoundFont (slot >= 256): keeps the font, the preset's PCM alive; `pr` points into font->presets
  std::shared_ptr<const sf::Font> font; std::shared_ptr<const sf::PresetPcm> pcm; const sf::Preset* pr = nullptr;
  // control-side bookkeeping
  uint32_t refs = 0; PadBuf* nextRetired = nullptr;
  const float* L() const { return mem; }
  const float* R() const { return stereo ? mem + frames : nullptr; }
  size_t bytes() const { return ((size_t)frames * (stereo ? 2 : 1)) * sizeof(float); }
};

// The immutable snapshot: the payload Players and the Seq read (the same struct the renderer takes by reference), and the references that keep its buffers alive.
struct PadsSnap {
  uint64_t id = 0;                                   // the publish counter (informational)
  RenderPads pads;                                   // buf / kit / route / sf / chop / var, all pointing into the PadBufs held below (var entries carry no data of their own)
  PadBuf* hold[2][kRenderPads] = {};                 // [0] audio, [1] SoundFont, per pad: one reference each
  PadsHost* owner = nullptr;
  ~PadsSnap();                                       // control thread (SnapSlot deletes it on collect / destruction): drops the references
};

class PadsHost {
 public:
  enum Err { kOk = 0, kArg = -1, kNoMem = -2, kFull = -3 };
  static PadsHost* create();                         // control thread; nullptr: out of memory
  ~PadsHost();                                       // after the audio thread has stopped: frees everything, whatever the voices say
  PadsHost(const PadsHost&) = delete;
  PadsHost& operator=(const PadsHost&) = delete;

  // ---- CONTROL THREAD: stage changes, then publish() ----------------------------------------------------------------------------------------------
  // The pad's audio (mono l, or stereo l + r, `frames` each, copied) and, optionally, its variations (copied). A pad that changes loses its chop data (the C API's rule: thunder_render_set_pad) and, being a new
  // buffer, its old variations; frames == 0 removes the pad. The same data again changes nothing. Nothing changes on an error.
  int setPad(int pad, const float* l, const float* r, int frames, double rate, const PadVarIn* vars = nullptr, int nVars = 0);
  int setKit(int pad, const VoiceKit& kit);
  int setRoute(int pad, PadRoute route);
  // A prepared SoundFont preset for the pad (font, a preset of it, its decoded PCM: kept alive by the buffer; the route is still setRoute's). setSfParams changes the pad's params only (no new buffer).
  int setSf(int pad, std::shared_ptr<const sf::Font> font, const sf::Preset* pr, std::shared_ptr<const sf::PresetPcm> pcm, const sf::PadParams& P);
  int clearSf(int pad);
  int setSfParams(int pad, const sf::PadParams& P);
  SeqChop& chop() { return stage_.chop; }            // the staged chop table: edit it with setLoop / setHits, then publish() (the audio side never sees this object)
  // Builds the snapshot from what is staged and hands it to the audio side. kFull: the ring is full (nothing published; collect() and retry). kNoMem: nothing published.
  int publish();
  // Frees the snapshots the audio side gave back and every retired buffer the rule above allows. Call it often (every post, every few ms). Returns the buffers freed.
  size_t collect();

  size_t buffers() const { return made_ - freed_; }  // PadBufs alive (current, in snapshots, or retired and waiting)
  size_t retired() const;                            // of those, waiting for their voices
  uint64_t made() const { return made_; }
  uint64_t freed() const { return freed_; }
  uint64_t lastEpoch() const { return nextEpoch_ - 1; }
  const PadBuf* padBuf(int pad, int kind = 0) const { return pad >= 0 && pad < kRenderPads && (kind == 0 || kind == 1) ? cur_[kind][pad] : nullptr; }   // the builder's current buffer (tests)
  // Called with each buffer just before it is freed (tests). Control thread.
  typedef void (*FreeHook)(void* user, const PadBuf* b);
  void setFreeHook(FreeHook fn, void* user) { hook_ = fn; hookUser_ = user; }

  // ---- AUDIO THREAD ---------------------------------------------------------------------------------------------------------------------------------
  // Once per quantum, at its top, BEFORE the sequencer steps (and after seq_adopt, so that a Seq that has just been adopted gets the table too):
  //   1. publishes the per-slot minimum epoch of the live and booked voices of `pl` (only the slots whose value changed are stored);
  //   2. adopts a newly published snapshot, if any: pl.setPads(its pads);
  //   3. points `seq` (the Seq the runner now uses; may be null) at the current snapshot's chop table (a pointer store, every call: harmless when unchanged).
  // Returns true when a new snapshot was adopted. No allocation, no lock, no free, no exception.
  bool beginQuantum(Players& pl, Seq* seq);
  const PadsSnap* current() const { return slot_->get(); }   // audio thread

 private:
  PadsHost() = default;
  friend struct PadsSnap;
  void release(PadBuf* b);                           // control: one reference less; the last one retires the buffer
  void freeBuf(PadBuf* b);
  void addRef(PadBuf* b) { b->refs++; }
  PadBuf* newBuf(int slot);                          // refs 1, epoch next

  SnapSlot<PadsSnap>* slot_ = nullptr;
  RenderPads stage_;                                 // control: kit, route, sf params, chop (buf / var unused)
  PadBuf* cur_[2][kRenderPads] = {};                 // control: the builder's reference per pad
  PadBuf* retired_ = nullptr;                        // control: refs == 0, waiting for their voices
  uint64_t nextEpoch_ = 1, nextId_ = 1, made_ = 0, freed_ = 0;
  FreeHook hook_ = nullptr; void* hookUser_ = nullptr;
  std::atomic<uint64_t> board_[kEpochSlots];         // audio writes (release), control reads (acquire): min live epoch per slot
  uint64_t mins_[kEpochSlots], last_[kEpochSlots];   // audio only: this quantum's scan; what board_ holds
};

}  // namespace thunder
#endif
