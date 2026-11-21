// THE C ABI OF thunder_render_opts AND thunder_render_stats: the byte offset of EVERY field, pinned. A host built against an older header reads the structs by these offsets, so a field is only ever APPENDED
// (after the last one), never inserted: the live-route review (verify-265 F1) found six stats fields inserted before stem / heard_mask. When a field is appended, add its row; an existing row never changes.
#include <cstddef>
#include <cstdio>

#include "thunder/render.h"

static int g_fail = 0;
#define OFF(S, f, n) do { if (offsetof(S, f) != (size_t)(n)) { std::printf("FAIL: offsetof(%s, %s) = %zu, pinned %d\n", #S, #f, (size_t)offsetof(S, f), (int)(n)); g_fail++; } } while (0)

int main() {
  OFF(thunder_render_opts, size, 0);
  OFF(thunder_render_opts, reserved0, 4);
  OFF(thunder_render_opts, sample_rate, 8);
  OFF(thunder_render_opts, effects, 16);
  OFF(thunder_render_opts, master_chain, 20);
  OFF(thunder_render_opts, pdc_mode, 24);
  OFF(thunder_render_opts, page_timing, 28);
  OFF(thunder_render_opts, use_frames, 32);
  OFF(thunder_render_opts, reserved1, 36);
  OFF(thunder_render_opts, frames, 40);
  OFF(thunder_render_opts, max_frames, 48);
  OFF(thunder_render_opts, vars_json, 56);
  OFF(thunder_render_opts, vars_bytes, 64);
  OFF(thunder_render_opts, stem, 72);
  OFF(thunder_render_opts, reserved2, 76);
  OFF(thunder_render_stats, size, 0);
  OFF(thunder_render_stats, song_over, 4);
  OFF(thunder_render_stats, voices_active, 8);
  OFF(thunder_render_stats, hit_limit, 12);
  OFF(thunder_render_stats, frames_rendered, 16);
  OFF(thunder_render_stats, steps, 24);
  OFF(thunder_render_stats, events, 28);
  OFF(thunder_render_stats, notes, 32);
  OFF(thunder_render_stats, notes_played, 36);
  OFF(thunder_render_stats, notes_no_pad, 40);
  OFF(thunder_render_stats, notes_midi_only, 44);
  OFF(thunder_render_stats, notes_shaped_rev, 48);
  OFF(thunder_render_stats, mix_events, 52);
  OFF(thunder_render_stats, peak_voices, 56);
  OFF(thunder_render_stats, voices_refused, 60);
  OFF(thunder_render_stats, voice_track_full, 64);
  OFF(thunder_render_stats, voice_auto_dropped, 68);
  OFF(thunder_render_stats, mix_auto_dropped, 72);
  OFF(thunder_render_stats, seq_dropped, 80);
  OFF(thunder_render_stats, notes_live, 88);
  OFF(thunder_render_stats, notes_sf, 92);
  OFF(thunder_render_stats, notes_var, 96);
  OFF(thunder_render_stats, notes_var_missing, 100);
  OFF(thunder_render_stats, notes_unshaped, 104);
  OFF(thunder_render_stats, events_ignored, 108);
  OFF(thunder_render_stats, notes_sf_played, 112);
  OFF(thunder_render_stats, sf_voices_refused, 116);
  OFF(thunder_render_stats, sf_voice_track_full, 120);
  OFF(thunder_render_stats, sf_voice_auto_dropped, 124);
  OFF(thunder_render_stats, peak_voices_all, 128);
  OFF(thunder_render_stats, notes_stretched, 132);
  OFF(thunder_render_stats, seq_chopped, 136);
  OFF(thunder_render_stats, seq_chop_skipped, 144);
  OFF(thunder_render_stats, seq_chop_no_hits, 152);
  OFF(thunder_render_stats, stem, 160);
  OFF(thunder_render_stats, heard_mask, 164);
  OFF(thunder_render_stats, notes_live_played, 168);
  OFF(thunder_render_stats, live_no_node, 172);
  OFF(thunder_render_stats, live_late, 176);
  OFF(thunder_render_stats, live_queue_dropped, 180);
  OFF(thunder_render_stats, live_forced_off, 184);
  OFF(thunder_render_stats, live_failed, 188);
  OFF(thunder_render_stats, live_refused, 192);
  if (sizeof(thunder_render_opts) < 80 || sizeof(thunder_render_stats) < 200) { std::printf("FAIL: a struct got smaller\n"); g_fail++; }   // (they only grow)
  if (g_fail) { std::printf("render_abi: %d FAILED\n", g_fail); return 1; }
  std::printf("render_abi: all offsets as pinned\n"); return 0;
}
