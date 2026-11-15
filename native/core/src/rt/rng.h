// The sequencer's seeded random stream (U05): core/seq-core.js `rand` (mulberry32), bit for bit. A song with a SED command
// must fall the same way in the web copy and in every native app, so this is integer arithmetic only.
//   JS: let x = seeded.s += 0x6D2B79F5; x = Math.imul(x ^ x >>> 15, x | 1); x ^= x + Math.imul(x ^ x >>> 7, x | 61);
//       return ((x ^ x >>> 14) >>> 0) / 4294967296;
// Note: JS keeps `seeded.s` as a NUMBER that grows past 2^32 (+= without |0) while every use of it goes through ToInt32, so
// only its low 32 bits matter until it passes 2^53 (never: 2^53 / 0x6D2B79F5 is about 4.9 million draws short of... see
// rng_js_exact_limit below). uint32 wrap-around here gives the same bits.
#ifndef THUNDER_RT_RNG_H
#define THUNDER_RT_RNG_H
#include <cstdint>

namespace thunder {

struct Mulberry32 {
  uint32_t s = 0;
  explicit Mulberry32(uint32_t seed = 0) : s(seed) {}
  uint32_t next_u32() {
    uint32_t x = (s += 0x6D2B79F5u);
    x = (x ^ (x >> 15)) * (x | 1u);
    x ^= x + (x ^ (x >> 7)) * (x | 61u);
    return x ^ (x >> 14);
  }
  double next() { return (double)next_u32() / 4294967296.0; }   // [0, 1), as the JS
};

// SED nn -> the stream's seed: JS `fx.SED * 2654435761 >>> 0` (a double product, then ToUint32; exact for nn <= 255).
inline uint32_t sed_seed(uint32_t nn) { return (uint32_t)((uint64_t)nn * 2654435761ull); }

}  // namespace thunder
#endif
