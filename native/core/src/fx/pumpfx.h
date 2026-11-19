// PumpSlot (U18): the state of ONE pump effect in a slot of the track-effect host, the C++ of the worklet's `s` object for a pump slot (index.html TFX_SRC:
// pumpNew, pumpRun, pumpRest, restDelay; `s.pu` is the handle, `s.rd` the rest delay line). Private to core/src/fx. The logic that runs a slot is FxHost's
// (fxpump.cpp); this is the data, the instance's life (make, renew, free) and the one gate that keeps the control thread out of pump's shared tables while the
// audio thread is in them (see the REAL-TIME block of fxpump.cpp).
#ifndef THUNDER_FX_PUMPFX_H
#define THUNDER_FX_PUMPFX_H
#include "fx/fxhost.h"

namespace thunder {

constexpr int kPumpRestMax = 8192;   // pumpRest: a latency above this is never rested (the JS's `lat > 8192`)

struct PumpSlot {
  int h = -1;                 // s.pu: pump's handle, -1 none
  int pt = 0, bi = -1, npv = 0;   // s.pt, s.bi (the parameter that is the plugin's own bypass, -1: METER has none), pv.length
  bool byp = false, dirty = false, fxo = false, pch = false, pd = false, cr = false, clk = false;   // s.byp, s.dirty, s.fxo, s.pch, s.pd, s.cr, s.clk
  int sck = 0, rs = 0;        // s.sck (COMP's External sidechain source: 0 none, 1..8 a send, 9 + t track t), s.rs (0 runs, 1 rests, 2 wakes)
  double wm = 0, up = 0;      // s.wm (the warm-up samples left), s.up (the latency of the pump slots before this one)
  float pv[kPumpMaxParams];   // s.pv: every parameter value in the plugin's order (written into pump_values as float32)
  // s.rd: the rest delay line {n: latency, m: n + 128, L, R, w, ok, sig}. The buffers are made with the slot (the JS makes them when a rest begins).
  bool hasRd = false; int rn = 0, rm = 0, rw = 0, rok = 0, rsig = 0;
  float* rbl = nullptr; float* rbr = nullptr;

  // Control thread: stages pv (and the bypass) and calls pump_new. nullptr: pump refused (the JS leaves such a slot out of the chain: the sound passes).
  static PumpSlot* make(const FxEntry& e, double sr);
  // Control thread: the latency pumpLatencyOf reports for an entry (a temporary instance staged with pv, no bypass of its own), -1 when refused.
  static int probeLatency(const FxEntry& e, double sr);
  ~PumpSlot();                // control thread: pump_free
  // Audio thread, inside a PumpGate: a fresh instance with the slot's settings (the JS's pump_free + pumpNew). false: pump refused (h = -1).
  bool renew(double sr);
  PumpSlot(const PumpSlot&) = delete; PumpSlot& operator=(const PumpSlot&) = delete;
  PumpSlot() = default;
};

// The audio thread's entry into pump's shared tables (the handle table, pump_io, the stage): the whole of a block's pump work happens inside one. `ok` is false
// while the control thread is making or freeing an instance: the audio thread then leaves its pump slots alone for that block (they pass the sound) and does not wait.
struct PumpGate { explicit PumpGate(bool use); ~PumpGate(); bool ok, used; PumpGate(const PumpGate&) = delete; PumpGate& operator=(const PumpGate&) = delete; };

// U31: the …chains pump modules make and free their instances here (control thread, inside the gate)
int pump_make_staged(int pt, const float* pv, int npv, double sr);
void pump_free_ctl(int h);

// pump's C entry points (wasm/pump_wasm.cpp), thin wrappers so the rest compiles without the library (no pump: every call is a no-op and a refused creation).
namespace pump {
bool available();
int nparams(int type);
int create(int type, double sr);   // pump_new
void destroy(int h);               // pump_free
float* stage();                    // pump_stage
int tel_len(int type);
int latency(int h);
const float* tel(int h);
void midi(int h, int frame, int status, int d1, int d2);
void transport(int h, double bpm, double ppq, int playing);
void hold(int h, int kind, float a, float b, float c, float d);
float* values(int h);
void commit(int h);
float* io(int ch);
void process(int h, int n, int sc);
}

}  // namespace thunder
#endif
