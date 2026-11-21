/* Verifier of #268 (U36 PR A), gate: a C (not C++) translation unit includes thunder/midiport.h; built with -std=c99 -Wall -Wextra -Werror -pedantic.
   Calls every function with NULLs / bad arguments as the header documents, and pins the sizes of the structs a shell compiles against. */
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "thunder/midi.h"
#include "thunder/midiport.h"

static void cpyS(char* d, const char* src) { memcpy(d, src, strlen(src) + 1); }   /* (MSVC warns on strcpy: C4996) */

/* the ABI the header promises (LP64 and any ABI where uint64_t aligns to 8): a change here is a break for every shell */
typedef char sz_dev[sizeof(thunder_midiout_dev) == 208 ? 1 : -1];
typedef char sz_stats[sizeof(thunder_midiout_stats) == 96 ? 1 : -1];
typedef char off_token[offsetof(thunder_midiout_dev, token) == 200 ? 1 : -1];
typedef char off_sent[offsetof(thunder_midiout_stats, sent) == 8 ? 1 : -1];
typedef char off_hang[offsetof(thunder_midiout_stats, may_hang) == 80 ? 1 : -1];

static int64_t nowFn(void* u) { (void)u; return 1000000000; }
static int sendFn(void* u, uint64_t d, const uint8_t* b, int n, int64_t at) { (void)u; (void)d; (void)b; (void)n; (void)at; return 0; }

#define CHECK(c, m) do { if (!(c)) { printf("FAIL: %s\n", m); return 1; } } while (0)

int main(void) {
  thunder_midiout_sink sk;
  thunder_midiout_dev dv[2];
  thunder_midiout_port po;
  thunder_midiout_stats st;
  thunder_midiout* o;
  thunder_midi* h;
  int64_t nx = 5;
  memset(&sk, 0, sizeof sk); sk.now_ns = nowFn; sk.send = sendFn;
  CHECK(thunder_midiout_create(NULL, 48000, &sk) == NULL, "create(NULL core)");
  h = thunder_midi_create(48000, NULL, 0); CHECK(h != NULL, "midi create");
  CHECK(thunder_midiout_create(h, 48000, NULL) == NULL, "create(NULL sink)");
  CHECK(thunder_midiout_create(h, 7999, &sk) == NULL && thunder_midiout_create(h, 384001, &sk) == NULL, "create(rate out of range)");
  sk.send = NULL; CHECK(thunder_midiout_create(h, 48000, &sk) == NULL, "create(no send)"); sk.send = sendFn;
  sk.now_ns = NULL; CHECK(thunder_midiout_create(h, 48000, &sk) == NULL, "create(no now_ns)"); sk.now_ns = nowFn;
  o = thunder_midiout_create(h, 48000, &sk); CHECK(o != NULL, "create");
  thunder_midiout_free(NULL);
  CHECK(thunder_midiout_set_devices(NULL, NULL, 0) == THUNDER_MIDIOUT_E_ARG, "set_devices(NULL o)");
  CHECK(thunder_midiout_set_devices(o, NULL, 1) == THUNDER_MIDIOUT_E_ARG, "set_devices(NULL list, n 1)");
  CHECK(thunder_midiout_set_devices(o, NULL, -1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_devices(o, dv, 33) == THUNDER_MIDIOUT_E_ARG, "set_devices(n out of range)");
  CHECK(thunder_midiout_set_devices(o, NULL, 0) == THUNDER_MIDIOUT_OK, "set_devices(NULL, 0) is the empty list");
  memset(dv, 0, sizeof dv); cpyS(dv[0].id, "n:A"); cpyS(dv[0].name, "A"); dv[0].token = 1; cpyS(dv[1].id, "n:B"); cpyS(dv[1].name, "B"); dv[1].token = 2;
  CHECK(thunder_midiout_set_devices(o, dv, 2) == THUNDER_MIDIOUT_OK, "set_devices(2)");
  CHECK(thunder_midiout_assign(NULL, 0, "n:A") == THUNDER_MIDIOUT_E_ARG && thunder_midiout_assign(o, 0, NULL) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_assign(o, 4, "n:A") == THUNDER_MIDIOUT_E_ARG && thunder_midiout_assign(o, -1, "") == THUNDER_MIDIOUT_E_ARG, "assign bad args");
  CHECK(thunder_midiout_assign(o, 0, "n:A") == THUNDER_MIDIOUT_OK, "assign");
  CHECK(thunder_midiout_set_clock(NULL, 0, 1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_clock(o, 4, 1) == THUNDER_MIDIOUT_E_ARG, "set_clock bad args");
  CHECK(thunder_midiout_set_gear(NULL, 0, 1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_gear(o, 4, 1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_gear(o, 0, (double)NAN) == THUNDER_MIDIOUT_E_ARG, "set_gear bad args");
  CHECK(thunder_midiout_set_playing(NULL, 1) == THUNDER_MIDIOUT_E_ARG, "set_playing(NULL)");
  CHECK(thunder_midiout_set_out_latency(NULL, 0) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_out_latency(o, -1) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_set_out_latency(o, 2000000001LL) == THUNDER_MIDIOUT_E_ARG, "set_out_latency bad args");
  CHECK(thunder_midiout_set_out_latency(o, 2000000000LL) == THUNDER_MIDIOUT_OK, "set_out_latency(2 s)");
  CHECK(thunder_midiout_get_port(NULL, 0, &po) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_get_port(o, 0, NULL) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_get_port(o, 4, &po) == THUNDER_MIDIOUT_E_ARG, "get_port bad args");
  CHECK(thunder_midiout_get_port(o, 0, &po) == THUNDER_MIDIOUT_OK && po.state == THUNDER_MIDIOUT_PORT_OK && po.dev == 0, "get_port");
  thunder_midiout_clock_map(NULL, 0, 0);
  thunder_midiout_clock_map(o, 0, 1000000000);
  CHECK(thunder_midiout_pump(NULL, &nx) == THUNDER_MIDIOUT_E_ARG && nx == 5, "pump(NULL) leaves *next_ns alone");
  CHECK(thunder_midiout_pump(o, NULL) >= 0, "pump(o, NULL next_ns)");
  CHECK(thunder_midiout_pump(o, &nx) >= 0 && nx == INT64_MAX, "pump: empty queue sleeps until woken");
  memset(&st, 0, sizeof st);
  CHECK(thunder_midiout_stats_get(NULL, &st) == THUNDER_MIDIOUT_E_ARG && thunder_midiout_stats_get(o, NULL) == THUNDER_MIDIOUT_E_ARG, "stats_get NULLs");
  st.size = 4; CHECK(thunder_midiout_stats_get(o, &st) == THUNDER_MIDIOUT_E_ARG, "stats_get(size 4)");
  st.size = (uint32_t)sizeof st; CHECK(thunder_midiout_stats_get(o, &st) == THUNDER_MIDIOUT_OK && st.size == sizeof st && st.sent == 0 && st.may_hang == 0, "stats_get");
  thunder_midiout_free(o); thunder_midi_free(h);
  puts("midiport_c: ok");
  return 0;
}
