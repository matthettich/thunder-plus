// H6: the put store (a port of PutStore in BridgeTranslator.kt): chunks, stereo, errors, stale sweep, replacement, the budget.
#include <cstring>
#include "check.h"
#include "put_store.h"
using tpd::PutStore;
static std::string hdr(int pid, int id, int c, long o, long n, bool last, double sr = 48000, int ch = 1) {
  char b[256]; std::snprintf(b, sizeof b, "{\"t\":\"put.header\",\"pid\":%d,\"id\":%d,\"c\":%d,\"o\":%ld,\"n\":%ld,\"last\":%s,\"sr\":%.0f,\"ch\":%d,\"frames\":%ld}", pid, id, c, o, n, last ? "true" : "false", sr, ch, n); return b;
}
static std::vector<float> ramp(int n, float k) { std::vector<float> v((size_t)n); for (int i = 0; i < n; i++) v[(size_t)i] = k + (float)i; return v; }
int main() {
  { PutStore s;   // mono, two chunks
    auto a = ramp(100, 0), b = ramp(100, 100);
    CHECK(s.header(hdr(1, 5, 0, 0, 200, false)).empty(), "header");
    auto r1 = s.chunk((const uint8_t*)a.data(), 400); CHECK(r1.first.empty() && r1.second == -1, "first chunk: more to come");
    CHECK(s.header(hdr(1, 5, 0, 100, 200, true)).empty(), "header 2");
    auto r2 = s.chunk((const uint8_t*)b.data(), 400); CHECK(r2.first.empty() && r2.second == 5, "last chunk completes id 5: '%s'", r2.first.c_str());
    const PutStore::Buf* bf = s.take(5); CHECK(bf && bf->frames == 200 && bf->ch == 1 && bf->sr == 48000, "buffer shape");
    float f[200]; std::memcpy(f, bf->data[0].data(), 800); CHECK(f[0] == 0 && f[150] == 150 && f[199] == 199, "the floats are in order");
    CHECK(s.held() == 800 && s.inFlight() == 0, "held 800 bytes");
    s.free(5); CHECK(!s.take(5) && s.held() == 0, "free releases");
  }
  { PutStore s;   // stereo
    auto l = ramp(10, 0), r = ramp(10, 50);
    s.header(hdr(2, 7, 0, 0, 10, false, 44100, 2)); auto x = s.chunk((const uint8_t*)l.data(), 40); CHECK(x.second == -1 && x.first.empty(), "left channel");
    s.header(hdr(2, 7, 1, 0, 10, true, 44100, 2)); x = s.chunk((const uint8_t*)r.data(), 40); CHECK(x.second == 7, "right channel completes: '%s'", x.first.c_str());
    const PutStore::Buf* bf = s.take(7); float f; std::memcpy(&f, bf->data[1].data() + 4, 4); CHECK(bf->ch == 2 && f == 51, "stereo data");
  }
  { PutStore s;   // an incomplete transfer is an error, never silence
    auto l = ramp(10, 0);
    s.header(hdr(3, 8, 0, 0, 20, true)); auto x = s.chunk((const uint8_t*)l.data(), 40); CHECK(!x.first.empty() && x.second == -1 && !s.take(8), "incomplete: refused");
    CHECK(s.held() == 0, "its memory is released");
  }
  { PutStore s; auto l = ramp(10, 0);   // errors
    CHECK(s.chunk((const uint8_t*)l.data(), 40).first.find("without a put.header") != std::string::npos, "bytes without a header");
    CHECK(!s.header("{not json").empty(), "bad header JSON");
    s.header(hdr(4, 9, 0, 0, 10, true)); CHECK(!s.chunk((const uint8_t*)l.data(), 39).first.empty(), "not whole floats");
    s.header(hdr(5, 9, 0, 5, 10, true)); CHECK(!s.chunk((const uint8_t*)l.data(), 40).first.empty(), "past the end");
    s.header(hdr(6, 9, 0, 0, 10, true, 0)); CHECK(!s.chunk((const uint8_t*)l.data(), 40).first.empty(), "no sample rate");
    s.header(hdr(7, 9, 0, 0, 10, true, 48000, 3)); CHECK(!s.chunk((const uint8_t*)l.data(), 40).first.empty(), "ch 3");
    CHECK(!s.header(hdr(8, 1, 0, 0, 10, false)).empty() == false, "first header fine");
    CHECK(!s.header(hdr(8, 1, 0, 0, 10, false)).empty(), "a second header before the bytes replaces the first and says so");
  }
  { int64_t now = 0; PutStore s(1 << 20, 30000, [&] { return now; });   // stale sweep and budget
    auto l = ramp(10, 0);
    s.header(hdr(10, 1, 0, 0, 20, false)); s.chunk((const uint8_t*)l.data(), 40); CHECK(s.inFlight() == 1, "in flight");
    now = 31000; CHECK(s.sweep() == 1 && s.inFlight() == 0 && s.held() == 0, "stale transfer released");
    s.header(hdr(11, 2, 0, 0, 20, false)); s.chunk((const uint8_t*)l.data(), 40); CHECK(s.sweep(true) == 1, "force: pads.commit releases all");
    s.header(hdr(12, 3, 0, 0, 300000, false)); CHECK(!s.chunk((const uint8_t*)l.data(), 40).first.empty(), "over the budget");
  }
  { PutStore s; auto l = ramp(4, 0), m = ramp(4, 9);   // same id replaces
    s.header(hdr(20, 4, 0, 0, 4, true)); s.chunk((const uint8_t*)l.data(), 16);
    s.header(hdr(21, 4, 0, 0, 4, true)); s.chunk((const uint8_t*)m.data(), 16);
    float f; std::memcpy(&f, s.take(4)->data[0].data(), 4); CHECK(f == 9 && s.held() == 16, "a new put under the same id replaces the old");
  }
  DONE("test_desktop_put_store");
}
