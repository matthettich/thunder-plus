/* U37 P0: a C (not C++) translation unit includes thunder/live.h; built with -std=c99 -Wall -Wextra -pedantic (see docs/port/U37-P0-NOTES.md). It takes the sizes of the structs that cross the API and checks
   them against the sizes live.h documents, at COMPILE time (a static assert), so that the contract cannot drift without this file failing. The same numbers hold on wasm32 and on 64-bit: every member is a
   fixed-width integer, float or double, with explicit padding; there is no pointer and no size_t in a struct. Not registered in CMake (P5 registers it with the implementation). */
#include <stdio.h>
#include <stddef.h>

#include "thunder/live.h"

/* _Static_assert is C11: under -std=c99 -pedantic it would warn, so the check is the C99 spelling of it (a negative array size) */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define LIVE_SA(name, cond) _Static_assert(cond, #name)
#else
#define LIVE_SA(name, cond) typedef char live_sa_##name[(cond) ? 1 : -1]
#endif

LIVE_SA(opts_size, sizeof(thunder_live_opts) == 64);
LIVE_SA(opts_rate_at8, offsetof(thunder_live_opts, sample_rate) == 8);
LIVE_SA(opts_max_pad_at16, offsetof(thunder_live_opts, max_pad_bytes) == 16);
LIVE_SA(play_opts_size, sizeof(thunder_live_play_opts) == 88);
LIVE_SA(play_opts_t0_at8, offsetof(thunder_live_play_opts, t0) == 8);
LIVE_SA(play_opts_only_at16, offsetof(thunder_live_play_opts, only) == 16);
LIVE_SA(stats_size, sizeof(thunder_live_stats) == 320);
LIVE_SA(stats_frames_at8, offsetof(thunder_live_stats, frames) == 8);
LIVE_SA(ev_head_size, sizeof(thunder_live_ev_head) == 16);
LIVE_SA(ev_head_f_at8, offsetof(thunder_live_ev_head, f) == 8);
LIVE_SA(ev_clock_size, sizeof(thunder_live_ev_clock) == 48);
LIVE_SA(ev_pos_size, sizeof(thunder_live_ev_pos) == 32);
LIVE_SA(ev_rowent_size, sizeof(thunder_live_ev_rowent) == 8);
LIVE_SA(ev_rows_size, sizeof(thunder_live_ev_rows) == 24);
LIVE_SA(ev_meters_size, sizeof(thunder_live_ev_meters) == 304);
LIVE_SA(ev_state_size, sizeof(thunder_live_ev_state) == 24);
LIVE_SA(ev_song_size, sizeof(thunder_live_ev_song) == 24);
LIVE_SA(ev_pads_size, sizeof(thunder_live_ev_pads) == 24);
LIVE_SA(ev_error_size, sizeof(thunder_live_ev_error) == 32);
LIVE_SA(ev_late_size, sizeof(thunder_live_ev_late) == 40);
LIVE_SA(ev_fxlvl_size, sizeof(thunder_live_ev_fxlvl) == 56);   /* M1 */
LIVE_SA(ev_fxdyn_size, sizeof(thunder_live_ev_fxdyn) == 152 && sizeof(thunder_live_ev_fxdyn_ent) == 16 && THUNDER_LIVE_EV_MAX >= sizeof(thunder_live_ev_fxdyn));   /* M1 */
LIVE_SA(ev_masterm_size, sizeof(thunder_live_ev_masterm) == 248 && THUNDER_LIVE_EV_MAX >= sizeof(thunder_live_ev_masterm));   /* M2 */
/* every record is a multiple of 8 and fits the poll slot; the longest (ROWS with 33 entries) is 24 + 33 * 8 = 288 */
LIVE_SA(ev_max_holds_meters, THUNDER_LIVE_EV_MAX >= sizeof(thunder_live_ev_meters));
LIVE_SA(ev_max_holds_rows, THUNDER_LIVE_EV_MAX >= sizeof(thunder_live_ev_rows) + 33 * sizeof(thunder_live_ev_rowent));
LIVE_SA(meter_floats, THUNDER_LIVE_METER_FLOATS == 70);
LIVE_SA(quantum, THUNDER_LIVE_QUANTUM == 128);
LIVE_SA(rings_pow2_cmd, (THUNDER_LIVE_CMD_RING & (THUNDER_LIVE_CMD_RING - 1)) == 0);
LIVE_SA(rings_pow2_fx, (THUNDER_LIVE_FXPARAM_RING & (THUNDER_LIVE_FXPARAM_RING - 1)) == 0);
LIVE_SA(err_codes_are_renders, THUNDER_LIVE_E_ARG == -1 && THUNDER_LIVE_E_LIMIT == -4 && THUNDER_LIVE_E_FONT == -7 && THUNDER_LIVE_E_BUSY == -8 && THUNDER_LIVE_E_UNSUPPORTED == -9);

int main(void) {
  /* only sizes are taken: no function of the API is called (none is implemented in P0) */
  printf("thunder_live_opts %u\nthunder_live_play_opts %u\nthunder_live_stats %u\n", (unsigned)sizeof(thunder_live_opts), (unsigned)sizeof(thunder_live_play_opts), (unsigned)sizeof(thunder_live_stats));
  printf("ev_head %u clock %u pos %u rowent %u rows %u meters %u state %u song %u pads %u error %u late %u\n", (unsigned)sizeof(thunder_live_ev_head), (unsigned)sizeof(thunder_live_ev_clock),
         (unsigned)sizeof(thunder_live_ev_pos), (unsigned)sizeof(thunder_live_ev_rowent), (unsigned)sizeof(thunder_live_ev_rows), (unsigned)sizeof(thunder_live_ev_meters), (unsigned)sizeof(thunder_live_ev_state),
         (unsigned)sizeof(thunder_live_ev_song), (unsigned)sizeof(thunder_live_ev_pads), (unsigned)sizeof(thunder_live_ev_error), (unsigned)sizeof(thunder_live_ev_late));
  puts("test_live_h: ok");
  return 0;
}
