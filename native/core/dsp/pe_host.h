// U25: the Plaits and Elements library of the web app (dsp/w_plaits.cc, dsp/w_elements.cc: pl_* / el_* exports of thunder-dsp.wasm) as plain C++ for the native core,
// with the SAME calls in the SAME order, built STRICT (-fno-fast-math -ffp-contract=off) from the pinned, patched upstream. No upstream header is visible here.
// A Session is one "wasm instance" worth of state: the web page keeps ONE thunder-dsp instance, so what the upstream code keeps in statics lives here instead (all of it
// zero at the start, as a fresh instance's memory is):
//   Plaits:   the Voice (its engines share one RAM block that pl_init does not clear), the patch, the modulations, the output blocks
//   Elements: the Part with its reverb buffer, the dither generator (w_elements.cc's `rng_state`, 1 at the start: it continues from one call to the next)
//   stmlib::Random (Plaits' noise engines, particles, Elements' exciters): one state, 0x21 at the start, reached through host_fx_state.h's per-thread pointer on host builds
// The engine makes one Session per rendered voice (eng_render_voice), as a fresh instantiation per rendered voice is what the reference does.
#ifndef THUNDER_DSP_PE_HOST_H
#define THUNDER_DSP_PE_HOST_H
namespace thunder_pe {
struct Session;
Session* session_new();            // zeroed; allocates (control thread only)
void session_free(Session*);
void session_reset(Session*);      // back to the state of session_new (a fresh instance), without allocating

// Plaits (pl_init .. pl_render of w_plaits.cc)
void pl_init(Session*);
void pl_set(Session*, int engine, float note, float harmonics, float timbre, float morph, float decay, float lpg_colour, float fm_amt, float timbre_amt, float morph_amt, int strike);
void pl_trigger(Session*, float v);
void pl_render(Session*, int n);                  // n a multiple of 12, at most 4096
const float* pl_out(const Session*);
const float* pl_aux(const Session*);

// Elements (el_init .. el_process of w_elements.cc)
void el_init(Session*);
void el_set(Session*, int model, float note, float strength, float contour, float bow, float blow, float strike, float bow_timbre, float flow, float blow_timbre, float mallet,
            float strike_timbre, float geometry, float brightness, float damping, float position, float space);
void el_gate(Session*, int g);
void el_process(Session*, int n);                 // n a multiple of 16, at most 4096
const float* el_main(const Session*);
const float* el_aux(const Session*);
}  // namespace thunder_pe
#endif
