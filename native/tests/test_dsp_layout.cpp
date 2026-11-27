// W-TFX (B2): the layout of Beads' 12-bit delay items, as prepare.sh leaves them on a host build. The upstream structs are two 12-bit bit-fields marked __attribute__((packed)),
// 3 bytes with GCC and clang on Linux / Mac / wasm; under the Microsoft ABI (clang-cl) they would be 4. prepare.sh packs the 3 bytes explicitly on every host build, so the Beads effect
// (media 1 "Vintage digital" and 2 "Clean tape") has the same memory and the same samples everywhere. This test holds that: size 3 on this compiler, and the explicit
// layout (slot 0 = bits 0..11, slot 1 = bits 12..23, little-endian, sign-extended) against a reference written here. Built only when the effects library is (the headers come from the prepared upstream tree).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "beads/dsp/delay_line.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL: " __VA_ARGS__); std::printf("\n"); } } while (0)

static int ref12(uint32_t word, int slot) { int v = int((word >> (slot ? 12 : 0)) & 0xfff); return v >= 0x800 ? v - 0x1000 : v; }

int main() {
  using namespace beads;
  std::printf("sizeof(Item<12-bit, 2>) = %zu, sizeof(Item<12-bit, 1>) = %zu, sizeof(Item<16-bit, 2>) = %zu\n",
              sizeof(Item<RESOLUTION_12_BIT, 2>), sizeof(Item<RESOLUTION_12_BIT, 1>), sizeof(Item<RESOLUTION_16_BIT, 2>));
  static_assert(sizeof(Item<RESOLUTION_12_BIT, 2>) == 3, "Beads 12-bit stereo item must be 3 bytes");
  static_assert(sizeof(Item<RESOLUTION_12_BIT, 1>) == 3, "Beads 12-bit mono item must be 3 bytes");
  static_assert(sizeof(Item<RESOLUTION_16_BIT, 2>) == 4, "Beads 16-bit stereo item must be 4 bytes");
  static_assert(alignof(Item<RESOLUTION_12_BIT, 2>) == 1, "items are byte-addressed");
  uint32_t sd = 12345;
  auto rnd = [&]() { sd = sd * 1664525u + 1013904223u; return (sd >> 8) / 16777216.0f; };
  const float edge[] = { 0.0f, 1.0f, -1.0f, 0.99997f, -0.99997f, 1.5f, -1.5f, 0.5f, -0.5f, 1.0f / 2048.0f, -1.0f / 2048.0f };
  std::vector<Item<RESOLUTION_12_BIT, 2>> st(256);
  std::memset(st.data(), 0, st.size() * 3);
  for (int k = 0; k < 4000; k++) {
    FloatFrame f; f.l = k < 121 ? edge[k % 11] : (rnd() - 0.5f) * 2.6f; f.r = k < 121 ? edge[(k / 11) % 11] : (rnd() - 0.5f) * 2.6f;
    const int idx = int(rnd() * 255.99f);
    st[idx].Store(0, f);
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&st[idx]);
    const uint32_t w = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16);
    const int el = stmlib::Clip16(int32_t(f.l * 32768.0f)) >> 4, er = stmlib::Clip16(int32_t(f.r * 32768.0f)) >> 4;
    CHECK(ref12(w, 0) == el && ref12(w, 1) == er, "stereo item %d: stored (%d %d), read back (%d %d)", idx, el, er, ref12(w, 0), ref12(w, 1));
    const FloatFrame v = st[idx].value(0);
    CHECK(v.l == float(el) / 2048.0f && v.r == float(er) / 2048.0f, "stereo value %d", idx);
  }
  std::vector<Item<RESOLUTION_12_BIT, 1>> mo(256);
  std::memset(mo.data(), 0, mo.size() * 3);
  for (int k = 0; k < 4000; k++) {
    FloatFrame f; f.l = (rnd() - 0.5f) * 2.6f; f.r = (rnd() - 0.5f) * 2.6f;
    const int idx = int(rnd() * 255.99f), odd = int(rnd() * 1.99f);
    mo[idx].Store(odd, f);
    const int lr = stmlib::Clip16(int32_t((f.l + f.r) * 16384.0f)) >> 4;
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&mo[idx]);
    const uint32_t w = uint32_t(b[0]) | (uint32_t(b[1]) << 8) | (uint32_t(b[2]) << 16);
    CHECK(ref12(w, odd ? 0 : 1) == lr, "mono item %d (%s): stored %d, read %d", idx, odd ? "odd" : "even", lr, ref12(w, odd ? 0 : 1));
    CHECK(mo[idx].value(odd).l == float(lr) / 2048.0f, "mono value %d", idx);
  }
  std::printf("%s\n", g_fail ? "dsp_layout: FAIL" : "dsp_layout: ok");
  return g_fail ? 1 : 0;
}
