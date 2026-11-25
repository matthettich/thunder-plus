// N2 H1b: the live host's event records (live.h section 8) as the bridge's JSON events (CORE-BRIDGE section 4), and a bounded queue the shell drains. No Android header.
//   CLOCK  -> {t:"clock", f, wallNs, sr, outLat, under, voices, dropped}      (CORE-BRIDGE 4: clock {f, wallNs, sr, outLat, under})
//   POS    -> {t:"pos", f, row, step, cr, bpm}                                 (CORE-BRIDGE 4: pos {f, row, pat, step}: the record has no `pat`; `cr` is the chain row, bpm the sequencer's)
//   ROWS / HELD -> {t:"rows"|"held", f, evs:[{tr,row,cr,step,phrase}]}         (rows {f, evs}; held is an ADDITION: the exact-stop report)
//   METERS -> {t:"meters", seq, at, a:[70 floats]}                              (CORE-BRIDGE 6.8, as fixed by U13)
//   SONG   -> {t:"song", rev, f}                                                (CORE-BRIDGE 6.3: the audio thread reads snapshot `rev` from frame f)
//   ERROR  -> {t:"err", where, msg, code, fatal, value, f}                      (CORE-BRIDGE 4: err {where, msg, fatal})
//   ADDITIONS the page may ignore (unknown types are ignored: CORE-BRIDGE 2): STATE -> {t:"state", state:"playing"|"stopped"|"ended", arg, f}; PADS -> {t:"pads", id, f}; LATE -> {t:"late", ...}; fit -> songfit.h.
// `f` is a HOST frame (live.h TIME); the page maps it to its clock with the CLOCK events.
#ifndef TPA_LIVE_EVENTS_H
#define TPA_LIVE_EVENTS_H

#include <deque>
#include <mutex>
#include <string>

namespace tpa {

// One record (`bytes` long, starting at its thunder_live_ev_head) as JSON; false for a record this version does not forward (an unknown type, a short record).
bool eventToJson(const unsigned char* rec, int bytes, std::string* out);

class EventQueue {
 public:
  explicit EventQueue(size_t cap = 512) : cap_(cap) {}
  // Pushes one JSON object. A full queue drops the oldest NON-critical entry (clock, pos, rows, meters, late); a critical one (state, held, song, pads, err) is dropped only when the queue holds nothing else.
  void push(std::string json, bool critical);
  // Everything queued as JSON objects joined by newlines ("a\nb\nc"), or "" when empty. Counts what was dropped.
  std::string drain();
  size_t dropped() const { std::lock_guard<std::mutex> g(m_); return dropped_; }
 private:
  struct E { std::string s; bool crit; };
  mutable std::mutex m_;
  std::deque<E> q_;
  size_t cap_, dropped_ = 0;
};

bool eventIsCritical(int type);

}  // namespace tpa
#endif
