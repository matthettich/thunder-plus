// H6: `put` (CORE-BRIDGE section 2) on the desktop shell: a port of the Android shell's PutStore (BridgeTranslator.kt). DUPLICATION, on purpose and noted in docs/port/H6-NOTES.md: the Kotlin one is
// 30 lines of logic; keep the two in step. The page sends, per chunk of at most 1 MB per channel, a header and then the bytes:
//   {t:'put.header', pid, id, c: channel (0 default), o: float offset in that channel, n: floats in that channel, last: true on the final chunk of the final channel, sr, ch, frames}
// (desktop: the bytes follow as a text message "B:<base64>"; the shell decodes them and calls chunk()). Assembled little-endian float32 channels; buf.use takes them out. No platform type.
#ifndef TPD_PUT_STORE_H
#define TPD_PUT_STORE_H

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tpd {

class PutStore {
 public:
  struct Buf { int id = -1; double sr = 0; int ch = 0; int frames = 0; std::vector<std::vector<uint8_t>> data; };
  explicit PutStore(int64_t maxBytes = (int64_t)256 << 20, int64_t staleMs = 30000, std::function<int64_t()> clockMs = nullptr);

  // A header message (JSON text). Returns an error text, or "". A second header before the bytes of the first REPLACES it: the old one is reported.
  std::string header(const std::string& text);
  // The bytes that follow a header. {error text or "", completed id or -1}. Any error drops the transfer's pid (its memory is released at once).
  std::pair<std::string, int> chunk(const uint8_t* bytes, size_t n);
  // Releases transfers that never completed: older than staleMs, or ALL of them (force: pads.commit says the puts are over). How many.
  int sweep(bool force = false);
  const Buf* take(int id) const;
  void free(int id);
  void clear();
  int64_t held() const { return held_; }
  int lastPid() const { return lastPid_; }
  int inFlight() const { return (int)parts_.size(); }

 private:
  struct Hdr { bool valid = false; int pid = -1, id = -1, c = 0; int64_t o = 0, n = 0; bool last = false; int ch = 1; double sr = 0; int64_t frames = 0; bool hasFrames = false; };
  struct Pending { int pid, id, ch, frames; double sr; std::vector<std::vector<uint8_t>> data; std::vector<std::vector<bool>> got; std::vector<int> gotN; int64_t born; int64_t bytes() const { return (int64_t)frames * 4 * ch; } };
  void dropPid(int pid);
  int64_t maxBytes_, staleMs_;
  std::function<int64_t()> clock_;
  Hdr cur_;
  std::map<int, Pending> parts_;
  std::map<int, Buf> done_;
  int64_t held_ = 0;
  int lastPid_ = -1;
};

}  // namespace tpd
#endif
