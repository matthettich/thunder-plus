// THE SONG'S LIFETIME UNDER A RUNNING SEQUENCER (SEQ-LIVE-DESIGN Part B, rulings 2-4; SEQ-TAKEOVER-BUILD-NOTES.md has the plan).
//
// One SeqSession per PLAY, made on the control thread. It owns three APPEND-ONLY registries that give a position its slot for the whole PLAY:
//   1. the roll PAGES (`rv`): one 2 KB page (16 chain rows x 32 steps of "how often has this track rolled here") for every song cell that held
//      a chain at any time of the PLAY (sect / phrase mode: one per track), never freed before the session ends;
//   2. the NTH / CND / REP rows: (phrase id, step) -> slot, slots never reused, counters in chunks of 256 slots;
//   3. the target ids (the PERFORM `ids_`): the strings, in the order they were first met, capped at kSeqMaxIds.
// Every Seq of the session is built against it (SeqSession::make), so a position has the same slot in every Seq of the PLAY: a successor
// carries `Tr::lk[]`, the `sent*` table and the counters over as they are. The counters are not copied at all: the pages and chunks are the
// session's, the successor already points at them. takeOver(old) is therefore valid from ANY earlier Seq of the session (no chaining).
//
// THREADS. The registries are touched by the control thread only. A Seq keeps its OWN copy of the page table and the chunk table (taken at its
// construction), so the audio thread never reads what the control thread writes. The pages and chunks themselves are written by the audio
// thread (counters) and never touched by the control thread, except to free the ones a failed publish staged (nobody else points at them yet).
// LIFETIME. SongSnap holds a shared_ptr to its session: the session is freed after the last snapshot (so after the audio side has returned its
// last Seq). The refcount moves on the control thread only (make, collect, the slot's destructor).
// COMMIT. make() stages the growth; SeqSession::publish commits it only when the slot took the snapshot, else rolls it back.
#ifndef THUNDER_SEQ_SESSION_H
#define THUNDER_SEQ_SESSION_H
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "rt/snap.h"
#include "seq/runner.h"
#include "seq/seq.h"

namespace thunder {

struct SongSnap;

class SeqSession : public std::enable_shared_from_this<SeqSession> {
 public:
  static std::shared_ptr<SeqSession> create(SeqMode mode, const SeqOpts& opts);   // control thread
  ~SeqSession();
  SeqSession(const SeqSession&) = delete;
  SeqSession& operator=(const SeqSession&) = delete;

  SeqMode mode() const { return mode_; }
  const SeqOpts& opts() const { return opts_; }

  // Builds the snapshot of a song (or a vars-only change: the same song pointer). The first one of a session is the starting Seq; the others are
  // successors (their state is written by takeOver). nullptr if a previous make() has not been committed or rolled back. Control thread.
  std::unique_ptr<SongSnap> make(std::shared_ptr<const Song> song, const SeqVars& vars);
  void commit();     // the staged registry growth stays
  void rollback();   // the staged growth is undone (pages freed, ids / slots forgotten)
  // make + slot.publish + commit (taken) or rollback (ring full: the snapshot is deleted). Control thread.
  bool publish(SnapSlot<SongSnap>& slot, std::shared_ptr<const Song> song, const SeqVars& vars);

  size_t pageCount() const { return nPages_; }        // roll pages allocated (committed + staged)
  size_t slotCount() const { return nSlots_; }        // NTH / CND / REP slots
  size_t chunkCount() const { return chunks_.size(); }
  size_t idCount() const { return ids_.size(); }
  uint64_t idOverflow() const { return idOverflow_; } // ids refused because the registry was full, ever (those targets are inert)

 private:
  friend class Seq;
  SeqSession(SeqMode mode, const SeqOpts& opts);
  // for Seq's constructor (control thread, between make() and commit / rollback)
  size_t pageTableSize() const { return pageTab_.size(); }
  void copyPages(uint32_t** dst) const;
  uint32_t* ensurePage(size_t idx);
  int slotFor(uint32_t key);                          // (pid * 32 + step) -> slot; -1 if the chunk table is full
  void copyChunks(SeqCountChunk** dst) const;
  int findId(std::string_view s) const;
  int addId(std::string_view s);                      // -1: full (counted)
  const std::vector<std::string>& ids() const { return ids_; }

  SeqMode mode_; SeqOpts opts_;
  std::vector<uint32_t*> pageTab_; size_t nPages_ = 0; std::vector<size_t> stagedPages_;
  std::unordered_map<uint32_t, int> slotOf_; int nSlots_ = 0; std::vector<uint32_t> stagedKeys_;
  std::vector<std::unique_ptr<SeqCountChunk>> chunks_; size_t committedChunks_ = 0; int committedSlots_ = 0;
  std::vector<std::string> ids_; std::unordered_map<std::string, int> idIndex_; size_t committedIds_ = 0;
  uint64_t idOverflow_ = 0;
  bool pending_ = false, anyCommitted_ = false;
};

// One object, one lifetime: a Seq can never outlive the Song or the vars it reads, nor the session whose pages it points into.
struct SongSnap {
  std::shared_ptr<SeqSession> sess;
  std::shared_ptr<const Song> song;
  SeqVars vars;
  std::unique_ptr<Seq> seq;       // declared last: destroyed first
};

// AUDIO THREAD: adopts what the control thread published; the Seq that was running hands its state to the newest one (takeOver) before it
// goes back to the control thread, and the runner is re-pointed. Returns true when the Seq changed. No allocation, no lock.
inline bool seq_adopt(SnapSlot<SongSnap>& slot, SeqRunner& runner) {
  const bool changed = slot.adopt([](SongSnap& prev, SongSnap& next) { next.seq->takeOver(*prev.seq); });
  if (changed) runner.rebind(slot.get()->seq.get());
  return changed;
}

}  // namespace thunder
#endif
