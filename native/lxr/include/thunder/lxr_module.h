/* The LXR module (U04), its C++ face: GPL-3.0-only (see native/lxr/LICENSES.md). Only code that is itself part of the optional LXR build may include this header: the core
 * never does (it talks through include/thunder/lxr_seam.h). The firmware is global state (one voice set per process): every call here is for ONE caller at a time; a call that finds
 * another running is refused (THUNDER_LXR_REFUSED), it never waits.
 *
 * Copyright (C) 2026 the Thunder Plus authors. SPDX-License-Identifier: GPL-3.0-only, with the additional permission and terms in lxr/NOTICE.md. */
#ifndef THUNDER_LXR_MODULE_H
#define THUNDER_LXR_MODULE_H
#include <cstddef>
#include <cstdint>

#include "thunder/lxr_seam.h"

namespace thunder {
namespace lxr {

int NumParams();                 // the firmware's parameter count (247)
double NativeRate();             // 44140
int BlockFrames();               // the firmware's block (64): what live_process renders in
constexpr size_t kMaxFloats = size_t(1) << 25;   // a render longer than this is refused (the sample rate would have to be absurd)

// ---- the pad render: lxr/lxr-engine.js LxrCore.render, step for step (power-on state, the init kit, the pad's values, 20 settling blocks, the hit, the voice's blocks until it dies
// away or the hold is over, windowed-sinc resampling to r.sr, Level / Drive, the trim and the 3 ms fade). Offline, control thread: nothing allocates. ----
size_t RenderBound(const ThunderLxrRequest& r);                                   // floats `out` must hold; 0 = cannot be rendered
int RenderPad(const ThunderLxrRequest& r, float* out, size_t cap, size_t* n,     // THUNDER_LXR_OK / TOO_SMALL / REFUSED
              float* raw = nullptr, size_t rawCap = 0, size_t* rawN = nullptr);   // raw: the voice's samples at NativeRate() before resampling (for the gate)
size_t PadKitBytes(const ThunderLxrRequest& r, uint8_t* out, size_t cap);         // the firmware parameter bytes the render loads (NumParams() of them), for the gate; 0 if refused
bool Describe(int kind, ThunderLxrDescribe* d);
int KnobCount(int kind);                                                          // how many "p<n>" knobs the engine has (for the tests)

// ---- PROVISIONAL live path: a kit and a block render with triggers at sample offsets ----
int LiveOpen(const uint8_t* kit, size_t n, double bpm);
int LiveProcess(int voice, const ThunderLxrEvent* ev, size_t nev, float* out, size_t frames);

}  // namespace lxr
}  // namespace thunder

extern "C" {
const ThunderLxrApi* thunder_lxr_module_api(void);   // the table (static storage)
int thunder_lxr_module_register(void);               // thunder_lxr_register(thunder_lxr_module_api())
}
#endif
