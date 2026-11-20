/* U34a gate 4: a C (not C++) translation unit includes thunder/render.h; built with -std=c99 -Wall -Wextra -Werror -pedantic.
   It also runs the API once from C: a bad song is refused with a text, a good one renders. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "thunder/render.h"

int main(void) {
  char err[128];
  thunder_render_opts o;
  thunder_render* r;
  float l[256], rr[256];
  const char* bad = "nope";
  const char* song = "{\"v\":6,\"bpm\":120,\"lpb\":4,\"ntr\":8,\"song\":[[0]],\"chains\":[[[0,0]]],\"plen\":[16],\"phrases\":[[]]}";
  int64_t n;
  memset(&o, 0, sizeof o);
  o.size = (uint32_t)sizeof o;
  r = thunder_render_create((const uint8_t*)bad, strlen(bad), &o, err, sizeof err);
  if (r || !err[0]) { puts("FAIL: a bad song was not refused with a text"); return 1; }
  r = thunder_render_create((const uint8_t*)song, strlen(song), &o, err, sizeof err);
  if (!r) { printf("FAIL: create: %s\n", err); return 1; }
  n = thunder_render_read(r, l, rr, 256);
  if (n < 0 || n > 256 || thunder_render_position(r) != n || thunder_render_length(r) < -1) { puts("FAIL: read"); thunder_render_free(r); return 1; }
  if (thunder_render_set_pad(r, 0, l, NULL, 10, 48000.0) != THUNDER_RENDER_E_STATE) { puts("FAIL: set_pad after read"); thunder_render_free(r); return 1; }
  thunder_render_free(r);
  { /* U34b: a stem handle from C, its list and its stats fields */
    int32_t tr[4]; thunder_render_stats st;
    o.stem = 1;
    r = thunder_render_create((const uint8_t*)song, strlen(song), &o, err, sizeof err);
    if (!r) { printf("FAIL: stem create: %s\n", err); return 1; }
    memset(&st, 0, sizeof st); st.size = (uint32_t)sizeof st;
    if (thunder_render_stem_tracks(r, tr, 4) < 0 || thunder_render_stats_get(r, &st) != 0 || st.stem != 1 || st.heard_mask != 1u) { puts("FAIL: stem"); thunder_render_free(r); return 1; }
    thunder_render_free(r);
    o.stem = 33;
    if (thunder_render_create((const uint8_t*)song, strlen(song), &o, err, sizeof err) || !err[0]) { puts("FAIL: a bad stem was not refused"); return 1; }
  }
  puts("test_render_c: ok");
  return 0;
}
