// THE PADS-AND-CHOP SNAPSHOT WITH DEFERRED FREE (U37 P1; prewire-19nov F2 + F3; U37-DESIGN-DRAFT 3.4; the lead's rulings D5 and D5 refined at round 2 of PR #276).
//
// THE PROBLEM. A voice borrows a pad's samples (VoiceBuf::data, an SF voice's PCM) and the sequencer borrows the chop table (SeqOpts::chop); the C API's RenderPads keeps both for the renderer's life, which a live
// host cannot: the page replaces a pad, or edits its slice points, while notes sound.
//
// THE DESIGN, in one paragraph. The CONTROL side owns every PadBuf (one pad's audio and its variations, or one pad's prepared SoundFont) and counts references to it; only snapshots (and the builder's own "current
// pad" table) hold references, and a reference moves on the control thread only. A PadsSnap is built whole on the control side (a RenderPads, the exact payload Players and Seq read today, plus the references that keep
// its buffers alive), published through a SnapSlot, adopted by the audio thread at the top of a quantum, and handed back through the slot to be deleted on the control side. A PadBuf nobody refers to any more is
// RETIRED, not freed: voices that started on it may still read it. Every voice that takes a PadBuf's data records the buffer's EPOCH (unique, > 0). Once a quantum the audio side publishes, in the BOARD, one atomic
// per voice slot of the pools that can hold pad data (the sampler pool's capacity + the SoundFont pool's): the epoch of the buffer that slot's live or booked voice is on, 0 when it has none. collect() frees a
// retired buffer when no board entry equals its epoch. Exact: a long sample on pad A holds ITS buffer and nothing else (not later buffers of pad A, not any buffer of another pad).
//
// THE FREE RULE, exactly. Buffer b (epoch e) is freed by collect() iff  refs(b) == 0  AND  for no board entry k: board[k] == e.
//   Control-side cost of a collect(): one pass over the board (nBoard loads, nBoard <= kBoardMax = 512, 96 with the default pools) plus retired x nBoard compares. With R retired buffers: R * nBoard compares, no
//   allocation; R is bounded by the pad replacements made since the oldest voice on a retired buffer started.
//
// WHY IT IS SAFE. Facts: (F1) a voice only starts on a buffer named by the snapshot Players currently points at (the adopted one); that snapshot holds a reference, so the buffer has refs > 0 and is not retired.
// (F2) the audio thread returns a snapshot to the control side only inside slot.adopt() (the ring push, a release), and beginQuantum() runs the scan and the board stores BEFORE it. (F3) a buffer is retired only
// when the last snapshot naming it has been deleted by collect(), i.e. after that snapshot was popped from the ring (an acquire). (F4) a voice's entry holds from the scan that first sees it (it is in the alive list
// from note() on, booked or sounding) until the first scan after it ended; a voice reads the buffer only between those.
//   * Voice V started on b in quantum Q (after Q's scan, at the latest) while b's snapshot S was current. b can be retired only after S is returned, at the earliest by Q+1's adopt, which runs after Q+1's scan.
//     Q+1's scan sees V if V is alive at that scan and stores its epoch (release) before the ring push (release); the control side pops S (acquire) and then loads the board (acquire): it sees that store or a later
//     one. If V ended before the scan, its last read precedes the scan in program order, hence the push, hence the control side's free. So V never reads a freed buffer.
//   * A later board value is as safe as the one stored before the push: entries only change when voices start (on buffers that are NOT retired: F1) or end.
//   * A voice slot reused by a new voice between two scans: the entry shows the newest voice's epoch (or the same one). The older voice has ended (its slot was freed only when it ended), so it needs nothing; the
//     newer one is covered from its own first scan on, as above. Entries are compared by value, so no entry can "belong" to the wrong buffer: epochs are unique, never reused.
//   * A voice that ends: its entry goes to 0 at the next scan; until then the buffer is merely held longer.
//   * The board is read entry by entry while the audio thread writes: an entry is only ever 0 -> e (a voice starts on a non-retired buffer), e -> 0 (it ended) or e -> e' (slot reused): a torn view of two entries
//     cannot hide a voice, because a voice keeps its own entry nonzero from its first scan to its end.
//   * The board covers exactly the pools of the Players the host was created for (create() checks the capacities fit kBoardMax; beginQuantum() refuses another Players).
//   * Identical data: setPad with the same samples, rate, channels and variations as the pad already has is a no-op (the buffer stays shared, same pointer, same epoch, AND the pad's chop data is KEPT: unlike
//     the C API's thunder_render_set_pad, which clears it on every call; a CHANGED pad loses its chop data). A REPLACED pad with identical content is a new object with a new epoch. A retired buffer is never revived.
//   * epoch 0 means "not tracked" (SongRenderer's by-reference pads): the board entry is 0 and nothing is held.
// PRECONDITIONS (the host, P2): (1) beginQuantum() runs on the audio thread at the top of every quantum, after seq_adopt() and before any note of the quantum is dispatched or any Seq is stepped; (2) Players::pads_
// is only set through it (one Players per PadsHost); (3) THE SEQ: the host passes the Seq the runner uses, EVERY quantum (beginQuantum points it at the current table every call, so a Seq just made by SeqSession or
// adopted by seq_adopt gets the table before its first step, whatever its opts carried: create the session with opts.chop = nullptr anyway). A Seq the host holds but does not pass keeps a pointer into a snapshot that
// goes back to the control side and is freed: use after free. That is the host's bug; beginQuantum asserts (debug builds) that a snapshot is not adopted with a null `seq` after a Seq was seen, until the host says
// seqGone() (it dropped its Seq), and counts such adopts (orphanAdopts()).
// THE TEST SEAM. setSeam(fn, user): a function pointer, null in a product build; beginQuantum() calls it, if set, between its stages (0 entry, 1 after the scan, 2 after the board stores, 3 after the adopt, 4 after
// the re-point) so a test can run publish() + collect() at exactly those points. A function pointer rather than a compile-time define: the tests then run the very binary a product links (a define would make the
// core library the tests use differ from the product's), and the cost is five loads and predictable never-taken branches per 2.7 ms quantum.
//
// THREAD RULES. Control thread: everything except beginQuantum(), seqGone() and current(), one thread at a time (the one that calls thunder_post in the web / the shells' control thread). Audio thread: those. The
// audio side allocates nothing, takes no lock, frees nothing, throws nothing. All control-side allocations of the new code are `new (std::nothrow)` and return kNoMem; the places that use a std::vector (the snapshot's
// RenderPads copy: the chop table's vector, `var`) are in pads_snap.cpp, built WITH exceptions like render.cpp, catching std::bad_alloc (a failure there may leak the partial copy); a build without exceptions
// (wasm) aborts there on exhaustion (docs/port/U37-P1-NOTES.md, open question 1). Stage, then publish once: a pad published before its chop data is set plays unchopped (counted: seqChopSkipped) until the next publish.
#ifndef THUNDER_RENDER_PADS_SNAP_H
#define THUNDER_RENDER_PADS_SNAP_H
#include <atomic>
#include <cassert>
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
  uint64_t epoch = 0;          // unique, grows with every PadBuf made (> 0), never reused
  // audio
  float* mem = nullptr;        // L then R in one allocation
  int frames = 0; double rate = 0; bool stereo = false;
  struct Var { int ii = 0; char kind = 0; int num = 0; int frames = 0; double sr = 0; const float* data = nullptr; };
  Var* vars = nullptr; int nVars = 0; float* varMem = nullptr;
  // SoundFont: keeps the font, the preset's PCM alive; `pr` points into font->presets
  std::shared_ptr<const sf::Font> font; std::shared_ptr<const sf::PresetPcm> pcm; const sf::Preset* pr = nullptr;
  // control-side bookkeeping
  uint32_t refs = 0; PadBuf* nextRetired = nullptr;
  size_t held = 0;             // U37 P2: the bytes this buffer counts against the host's budget (samples + variations' samples + whatever setSf was told): added at creation, subtracted when it is freed
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
  // Control thread; nullptr: out of memory, or the pools of `pl` have more voice slots than kBoardMax. Publishes an EMPTY snapshot: the first beginQuantum() adopts it, so `pl` has pads (none) from then on.
  static PadsHost* create(const Players& pl);
  static constexpr int kBoardMax = 512;
  ~PadsHost();                                       // after the audio thread has stopped: frees everything, whatever the voices say
  PadsHost(const PadsHost&) = delete;
  PadsHost& operator=(const PadsHost&) = delete;

  // ---- CONTROL THREAD: stage changes, then publish() ----------------------------------------------------------------------------------------------
  // The pad's audio (mono l, or stereo l + r, `frames` each, copied) and, optionally, its variations (copied). A pad that changes loses its chop data (the C API's rule: thunder_render_set_pad) and, being a new
  // buffer, its old variations; frames == 0 removes the pad. The same data again changes nothing. Nothing changes on an error.
  int setPad(int pad, const float* l, const float* r, int frames, double rate, const PadVarIn* vars = nullptr, int nVars = 0);
  // U37 P2: as setPad (no variations), but TAKES OWNERSHIP of `mem` (`new (std::nothrow) float[]`, which freeBuf() frees with delete[]; thunder_live_alloc makes exactly such a block): frames floats mono, or 2 * frames
  // stereo (all of L, then all of R); no copy. frames 1 .. MAX_PAD_FRAMES (frames 0 is kArg: there is nothing to take). The samples are checked finite IN PLACE. No identical-data shortcut: the block is kept and the old
  // pad is replaced (retired when nobody refers to it). On ANY error (kArg, kNoMem) the block is still the CALLER's and nothing changed.
  int setPadTake(int pad, float* mem, bool stereo, int frames, double rate);
  int setKit(int pad, const VoiceKit& kit);
  int setRoute(int pad, PadRoute route);
  // A prepared SoundFont preset for the pad (font, a preset of it, its decoded PCM: kept alive by the buffer; the route is still setRoute's). setSfParams changes the pad's params only (no new buffer).
  // `heldBytes` (U37 P2): what the preset's decoded PCM (and the font, if the caller wants it counted) weigh, for bytesHeld(); 0 = not counted.
  int setSf(int pad, std::shared_ptr<const sf::Font> font, const sf::Preset* pr, std::shared_ptr<const sf::PresetPcm> pcm, const sf::PadParams& P, size_t heldBytes = 0);
  int clearSf(int pad);
  int setSfParams(int pad, const sf::PadParams& P);
  SeqChop& chop() { return stage_.chop; }            // the staged chop table: edit it with setLoop / setHits, then publish() (the audio side never sees this object)
  // Builds the snapshot from what is staged and hands it to the audio side. kFull: the ring is full (nothing published; collect() and retry). kNoMem: nothing published.
  int publish();
  // Frees the snapshots the audio side gave back and every retired buffer the rule above allows. Call it often (every post, every few ms). Returns the buffers freed.
  size_t collect();

  size_t buffers() const { return made_ - freed_; }  // PadBufs alive (current, in snapshots, or retired and waiting)
  size_t retired() const;                            // of those, waiting for their voices
  // U37 P2, the budget's figures (control thread): the bytes of every PadBuf alive (the builder's current ones, those in snapshots, and the RETIRED ones that voices may still read), and of those the retired ones.
  size_t bytesHeld() const { return bytes_; }
  size_t bytesRetired() const;
  int boardSlots() const { return nBoard_; }          // the voice slots the epoch board was sized for at create() (verify-276 R2-4: beginQuantum asserts it equals the pool's epochSlots())
  uint64_t made() const { return made_; }
  uint64_t freed() const { return freed_; }
  uint64_t lastEpoch() const { return nextEpoch_ - 1; }
  const PadBuf* padBuf(int pad, int kind = 0) const { return pad >= 0 && pad < kRenderPads && (kind == 0 || kind == 1) ? cur_[kind][pad] : nullptr; }   // the builder's current buffer (tests)
  // Called with each buffer just before it is freed (tests). Control thread. The hook may take ownership of `mem` / `varMem` (and set them to nullptr): a test keeps freed audio poisoned instead of returning it to the allocator.
  typedef void (*FreeHook)(void* user, PadBuf* b);
  void setFreeHook(FreeHook fn, void* user) { hook_ = fn; hookUser_ = user; }

  // ---- AUDIO THREAD ---------------------------------------------------------------------------------------------------------------------------------
  // Once per quantum, at its top, BEFORE the sequencer steps (and after seq_adopt, so that a Seq that has just been adopted gets the table too):
  //   1. publishes, per voice slot of `pl`, the epoch of its live or booked voice, or 0 (only the entries whose value changed are stored);
  //   2. adopts a newly published snapshot, if any: pl.setPads(its pads);
  //   3. points `seq` (the Seq the runner now uses; may be null) at the current snapshot's chop table (a pointer store, every call: harmless when unchanged).
  // U37 P2 (verify-276 R2-2): while `seq` is null and a Seq was seen since the last seqGone(), a pending snapshot is NOT adopted (it would return a chop table that the host's Seq still points at): it stays pending,
  // orphanAdopts() counts the quantum, and the adopt happens in the first quantum that passes a Seq or follows seqGone(). The scan and the board stores run as always.
  // Returns true when a new snapshot was adopted. No allocation, no lock, no free, no exception.
  bool beginQuantum(Players& pl, Seq* seq);
  void seqGone() { seqSeen_ = false; }               // audio thread: the host dropped its Seq (a stopped play): a snapshot may now be adopted with a null seq
  uint64_t orphanAdopts() const { return orphan_; }  // quanta in which an adopt was held back because a Seq was seen and none was passed (a host bug; verify-276 R2-2: nothing is adopted then)
  typedef void (*Seam)(void* user, int stage);
  void setSeam(Seam fn, void* user) { seam_ = fn; seamUser_ = user; }   // tests; before the first beginQuantum
  const PadsSnap* current() const { return slot_->get(); }   // audio thread

 private:
  PadsHost() = default;
  friend struct PadsSnap;
  void release(PadBuf* b);                           // control: one reference less; the last one retires the buffer
  void freeBuf(PadBuf* b);
  void addRef(PadBuf* b) { b->refs++; }
  PadBuf* newBuf();                                  // refs 1, epoch next
  void seam(int stage) { if (seam_) seam_(seamUser_, stage); }

  SnapSlot<PadsSnap>* slot_ = nullptr;
  const Players* owner_ = nullptr; int nBoard_ = 0;
  RenderPads stage_;                                 // control: kit, route, sf params, chop (buf / var unused)
  PadBuf* cur_[2][kRenderPads] = {};                 // control: the builder's reference per pad
  PadBuf* retired_ = nullptr;                        // control: refs == 0, waiting for their voices
  uint64_t nextEpoch_ = 1, nextId_ = 1, made_ = 0, freed_ = 0; size_t bytes_ = 0;   // bytes_: the sum of `held` of every PadBuf alive
  FreeHook hook_ = nullptr; void* hookUser_ = nullptr;
  Seam seam_ = nullptr; void* seamUser_ = nullptr;
  bool seqSeen_ = false; uint64_t orphan_ = 0;       // audio only (orphan_: quanta in which a pending snapshot was held back because the host passed no Seq after having had one)
  // The board and the audio-only scratch each start on their own cache line, apart from the control-written fields above and from each other (verify-276 F8).
  alignas(64) std::atomic<uint64_t> board_[kBoardMax];   // audio writes (release), control reads (acquire): the epoch each voice slot's voice is on, 0 none
  alignas(64) uint64_t scan_[kBoardMax];                 // audio only: this quantum's scan
  alignas(64) uint64_t last_[kBoardMax];                 // audio only: what board_ holds
};

}  // namespace thunder
#endif
