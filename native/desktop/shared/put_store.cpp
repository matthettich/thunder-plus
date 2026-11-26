#include "put_store.h"

#include <chrono>
#include <cstring>

#include "rt/json.h"

namespace tpd {

PutStore::PutStore(int64_t maxBytes, int64_t staleMs, std::function<int64_t()> clockMs) : maxBytes_(maxBytes), staleMs_(staleMs), clock_(std::move(clockMs)) {
  if (!clock_) clock_ = [] { return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); };
}

std::string PutStore::header(const std::string& text) {
  thunder::JsonDoc d;
  if (!d.parse(text) || !d.root().isObj()) return "put.header is not JSON";
  sweep();
  const bool stale = cur_.valid;
  Hdr h; h.valid = true;
  thunder::Json j = d.root();
  h.pid = (int)j.num("pid", -1); h.id = (int)j.num("id", -1); h.c = (int)j.num("c", 0); h.o = (int64_t)j.num("o", 0); h.n = (int64_t)j.num("n", 0);
  h.last = j.boolean("last", false); h.ch = (int)j.num("ch", 1); h.sr = j.num("sr", -1);
  h.hasFrames = j.get("frames").isNum(); h.frames = (int64_t)j.num("frames", (double)h.n);
  cur_ = h;
  return stale ? "put.header while a chunk is still expected (the earlier header is dropped)" : "";
}

std::pair<std::string, int> PutStore::chunk(const uint8_t* bytes, size_t nb) {
  if (!cur_.valid) return {"an ArrayBuffer without a put.header", -1};
  const Hdr h = cur_; cur_ = Hdr();
  sweep();
  lastPid_ = h.pid;
  if (h.id < 0 || h.pid < 0) return {"put.header without id / pid", -1};
  auto fail = [&](const char* m) { dropPid(h.pid); return std::make_pair(std::string(m), -1); };
  if ((nb & 3) != 0) return fail("put chunk is not whole floats");
  auto it = parts_.find(h.pid);
  if (it == parts_.end()) {
    if (h.ch < 1 || h.ch > 2) return {"put: ch must be 1 or 2", -1};
    if (!(h.sr >= 1 && h.sr <= 1e6)) return {"put.header without a sample rate (sr)", -1};
    if (h.n <= 0 || h.n > 0x7fffffff / 4) return {"put.header n out of range", -1};
    if (h.frames != h.n) return {"put: frames and n disagree", -1};
    const int64_t need = h.n * 4 * h.ch;
    if (held_ + need > maxBytes_) return {"put: too much audio held", -1};
    Pending p; p.pid = h.pid; p.id = h.id; p.ch = h.ch; p.frames = (int)h.n; p.sr = h.sr; p.born = clock_();
    p.data.assign((size_t)h.ch, std::vector<uint8_t>((size_t)h.n * 4));
    p.got.assign((size_t)h.ch, std::vector<bool>((size_t)h.n, false)); p.gotN.assign((size_t)h.ch, 0);
    it = parts_.emplace(h.pid, std::move(p)).first; held_ += need;
  } else if (it->second.id != h.id) return fail("put: pid was started for another id");
  Pending& p = it->second;
  if (h.c < 0 || h.c >= p.ch) return fail("put: channel out of range");
  if (h.o < 0 || (uint64_t)h.o * 4 + nb > p.data[(size_t)h.c].size()) return fail("put: chunk past the end of the buffer");
  if (nb) std::memcpy(p.data[(size_t)h.c].data() + (size_t)h.o * 4, bytes, nb);
  for (size_t k = 0; k < nb / 4; k++) { auto b = p.got[(size_t)h.c][(size_t)h.o + k]; if (!b) { p.got[(size_t)h.c][(size_t)h.o + k] = true; p.gotN[(size_t)h.c]++; } }
  if (h.last) {
    for (int k = 0; k < p.ch; k++) if (p.gotN[(size_t)k] != p.frames) return fail("put: incomplete (a channel has missing floats)");
    Buf b; b.id = p.id; b.sr = p.sr; b.ch = p.ch; b.frames = p.frames; b.data = std::move(p.data);
    const int id = p.id;
    parts_.erase(it);
    free(id);   // a new put under the same id replaces the old
    done_[id] = std::move(b);
    return {"", id};
  }
  return {"", -1};
}

void PutStore::dropPid(int pid) { auto it = parts_.find(pid); if (it != parts_.end()) { held_ -= it->second.bytes(); parts_.erase(it); } }

int PutStore::sweep(bool force) {
  const int64_t now = clock_(); int n = 0;
  for (auto it = parts_.begin(); it != parts_.end();) {
    if (force || now - it->second.born > staleMs_) { held_ -= it->second.bytes(); it = parts_.erase(it); n++; } else ++it;
  }
  return n;
}

const PutStore::Buf* PutStore::take(int id) const { auto it = done_.find(id); return it == done_.end() ? nullptr : &it->second; }
void PutStore::free(int id) { auto it = done_.find(id); if (it != done_.end()) { held_ -= (int64_t)it->second.frames * 4 * it->second.ch; done_.erase(it); } }
void PutStore::clear() { cur_ = Hdr(); parts_.clear(); done_.clear(); held_ = 0; }

}  // namespace tpd
