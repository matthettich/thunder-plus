// H6b: the recovery copy of the desktop shells (CORE-BRIDGE recovery.put / recovery.get / recovery.drop and the `recovery` event after hello): the page hands the shell its project text every ~10 s while the song is
// unsaved; after a crash the next start announces it and the page asks "Restore unsaved changes?". The desktop twin of the Android RecoveryStore's PUT half (recovery.put.thunder + recovery.put.json). There is no
// swipe-away on a desktop, so there is no song.load copy. Atomic: temp files, then renames, the sidecar last (the sidecar is what says "there is a recovery"). Thread-safe (one mutex). Never throws.
#ifndef TPD_RECOVERY_STORE_H
#define TPD_RECOVERY_STORE_H

#include <mutex>
#include <string>

namespace tpd {

class RecoveryStore {
 public:
  explicit RecoveryStore(std::string dir) : dir_(std::move(dir)) {}
  bool enabled() const { return !dir_.empty(); }
  bool put(const std::string& text, int64_t nowMs);   // false: not stored (the older copy stays)
  bool has();
  // {"t":"recovery","title":"","time":<ms>,"file":null}, or "" when there is no complete copy
  std::string announceEvent();
  // the body of the reply to recovery.get: `"ok":true,"title":"","time":N,"file":null,"source":"put","text":"..."` or `"ok":false,"error":"there is no recovery copy"`
  std::string getBody();
  void drop();
 private:
  std::string dir_;
  std::mutex m_;
};

}  // namespace tpd
#endif
