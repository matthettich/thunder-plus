// SeqSession (see session.h). Control thread only.
#include "seq/session.h"

#include <cstring>

namespace thunder {

SeqSession::SeqSession(SeqMode mode, const SeqOpts& opts) : mode_(mode), opts_(opts) {
  pageTab_.assign(mode == SeqMode::Song ? (size_t)kSongRows * kSeqTracks : (size_t)kSeqTracks, nullptr);
}
std::shared_ptr<SeqSession> SeqSession::create(SeqMode mode, const SeqOpts& opts) {
  std::shared_ptr<SeqSession> s(new SeqSession(mode, opts));
  if (mode != SeqMode::Song) { for (size_t t = 0; t < (size_t)kSeqTracks; t++) s->ensurePage(t); s->stagedPages_.clear(); }   // sect / phrase mode: a page a track, from the start
  return s;
}
SeqSession::~SeqSession() { for (uint32_t* p : pageTab_) delete[] p; }

uint32_t* SeqSession::ensurePage(size_t idx) {
  if (idx >= pageTab_.size()) return nullptr;
  if (!pageTab_[idx]) { pageTab_[idx] = new uint32_t[kSeqPageU32](); nPages_++; stagedPages_.push_back(idx); }
  return pageTab_[idx];
}
void SeqSession::copyPages(uint32_t** dst) const { std::memcpy(dst, pageTab_.data(), pageTab_.size() * sizeof(uint32_t*)); }

int SeqSession::slotFor(uint32_t key) {
  const auto it = slotOf_.find(key); if (it != slotOf_.end()) return it->second;
  const int slot = nSlots_;
  if ((size_t)(slot >> 8) >= (size_t)kSeqMaxChunks) return -1;
  if ((size_t)(slot >> 8) >= chunks_.size()) chunks_.emplace_back(new SeqCountChunk());   // value-initialised: zero
  slotOf_.emplace(key, slot); stagedKeys_.push_back(key); nSlots_++;
  return slot;
}
void SeqSession::copyChunks(SeqCountChunk** dst) const {
  for (size_t i = 0; i < (size_t)kSeqMaxChunks; i++) dst[i] = i < chunks_.size() ? chunks_[i].get() : nullptr;
}

int SeqSession::findId(std::string_view s) const { const auto it = idIndex_.find(std::string(s)); return it == idIndex_.end() ? -1 : it->second; }
int SeqSession::addId(std::string_view s) {
  if (ids_.size() >= (size_t)kSeqMaxIds) { idOverflow_++; return -1; }
  ids_.emplace_back(s); idIndex_.emplace(ids_.back(), (int)ids_.size() - 1); return (int)ids_.size() - 1;
}

void SeqSession::commit() {
  stagedPages_.clear(); stagedKeys_.clear(); committedChunks_ = chunks_.size(); committedSlots_ = nSlots_; committedIds_ = ids_.size();
  pending_ = false; anyCommitted_ = true;
}
void SeqSession::rollback() {
  for (size_t i : stagedPages_) { delete[] pageTab_[i]; pageTab_[i] = nullptr; nPages_--; }
  stagedPages_.clear();
  for (uint32_t k : stagedKeys_) slotOf_.erase(k);
  stagedKeys_.clear(); nSlots_ = committedSlots_; chunks_.resize(committedChunks_);
  while (ids_.size() > committedIds_) { idIndex_.erase(ids_.back()); ids_.pop_back(); }
  pending_ = false;
}

std::unique_ptr<SongSnap> SeqSession::make(std::shared_ptr<const Song> song, const SeqVars& vars) {
  if (pending_ || !song) return nullptr;
  pending_ = true;
  std::unique_ptr<SongSnap> s(new SongSnap());
  s->sess = shared_from_this(); s->song = std::move(song); s->vars = vars;
  s->seq.reset(new Seq(*this, *s->song, s->vars, anyCommitted_));
  return s;
}
bool SeqSession::publish(SnapSlot<SongSnap>& slot, std::shared_ptr<const Song> song, const SeqVars& vars) {
  std::unique_ptr<SongSnap> s = make(std::move(song), vars);
  if (!s) return false;
  if (slot.publish(s.get())) { s.release(); commit(); return true; }
  rollback(); return false;   // s is deleted here (its Seq first)
}

}  // namespace thunder
