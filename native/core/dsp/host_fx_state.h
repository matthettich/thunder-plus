// Host builds only (U14): state that is per wasm instance in the web app and would be per process natively.
// The web app gives every ThunderFx node (a track's effect chain, the master's) its own instance of tfx.wasm, so every static in the effect
// sources exists once per node there. Natively the effects of all hosts live in one process: a static that an effect WRITES while it runs
// would be shared by all of them, and an effect would then depend on what every other host did before (found as e_pshift_alt at -1.5 dB and
// Beads / Clouds drawing from one generator). The two such statics of the tfx sources are reached through these per-thread pointers
// (patched in by third_party/prepare.sh); FxHost points them at its own copies around its work (thunder_fx_state_use, wasi_rand.cpp):
//   stmlib::Random::rng_state_      (Beads, Clouds; 0x21 at start)          -> stmlib_rng_cur()
//   daisysp::myrand()'s seed        (PitchShifter's spray; 1 at start)      -> daisy_myrand()
// With no pointer set the old process-wide static is used, so code outside an effect host (the synth engines, mods) is unchanged.
// (The third is libc rand(): wasi_rand.h.)
#pragma once
#include <stdint.h>
namespace thunder_host {
inline uint32_t*& stmlib_rng_cur() { static thread_local uint32_t* p = nullptr; return p; }
inline uint32_t*& daisy_myrand_cur() { static thread_local uint32_t* p = nullptr; return p; }
inline uint32_t& daisy_myrand() { static uint32_t process_wide = 1; uint32_t* p = daisy_myrand_cur(); return p ? *p : process_wide; }
}  // namespace thunder_host
