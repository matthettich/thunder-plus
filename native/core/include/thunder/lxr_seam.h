/* THE LXR SEAM (U04): the only thing the core knows about the LXR drum-synth module.
 *
 * LXR is GPL-3.0-only code (lxr/, native/lxr/). The permissively licensed core must not contain it, include its headers or call its symbols, so that a build
 * made without the module (cmake -DTHUNDER_WITH_LXR=OFF) links, runs and plays everything but the four LXR engines. The core talks to the module through
 * this table of function pointers: the host application registers the module's table once at start-up (`thunder_lxr_register`; the module's own
 * `thunder_lxr_module_register()` does it). With nothing registered every LXR call in the core answers "unsupported" (kEngUnsupported): a song that uses an LXR
 * engine is refused cleanly, never rendered as silence. This header is plain C, has no LXR type, and is the same in both configurations.
 *
 * Copyright (C) 2026 the Thunder Plus authors. Licence: as the rest of native/core (permissive); it contains no LXR code. */
#ifndef THUNDER_LXR_SEAM_H
#define THUNDER_LXR_SEAM_H
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define THUNDER_LXR_ABI 1u
#define THUNDER_LXR_MAX_PARAMS 256   /* the firmware's sound parameters are one byte each (247 today); a pad's knob "p<n>" has n < this */
#define THUNDER_LXR_KINDS 4          /* 0 lxdrum, 1 lxsnare, 2 lxcym, 3 lxhat: the page's engine keys, in this order */

/* What a pad of one LXR engine holds when it is new (the page's XENG[k].d()): `present[n]` says the knob p<n> exists, `def[n]` its default; the rest are the pad's
   other keys. Values are the page's own numbers (the machine's 0-127 knobs). */
typedef struct ThunderLxrDescribe {
  int32_t nparams;                           /* the firmware's parameter count */
  double rate;                               /* the machine's sample rate (44140): the rate of the raw voice, before resampling to the app's */
  double len;                                /* the engine's default pad length in seconds (LxrCore.len) */
  uint8_t present[THUNDER_LXR_MAX_PARAMS];
  double def[THUNDER_LXR_MAX_PARAMS];
  double vdst, ldst, vel, open;              /* defaults of the pad's velocity target, LFO target, hit velocity and (hat only) open flag */
  int32_t hasOpen;                           /* 1 for the hat */
} ThunderLxrDescribe;

/* One pad render: the page's `LxrCore.render(k, P, sr, hold, V, bpm)`. P is the pad's parameters (normX: a number the page would not keep is replaced by its
   default by the caller), `tune` / `drive` / `level` the voice's, `vlen` the voice's V.len, `hold` the render's hold in seconds (0 = none), `bpm` songBpm(). */
typedef struct ThunderLxrRequest {
  int32_t kind;
  double p[THUNDER_LXR_MAX_PARAMS];          /* p<n> for each knob that exists (see ThunderLxrDescribe.present) */
  double vdst, ldst, vel, open;
  double tune, drive, level;
  double vlen, hold, bpm, sr;
} ThunderLxrRequest;

/* The live path's trigger: at `offset` frames into the block, firmware voice `voice` (0-2 drums, 3 snare, 4 cymbal, 5 closed hat, 6 open hat) is struck. PROVISIONAL. */
typedef struct ThunderLxrEvent { uint32_t offset; int32_t voice; int32_t vel; int32_t note; } ThunderLxrEvent;

enum { THUNDER_LXR_OK = 0, THUNDER_LXR_TOO_SMALL = -1, THUNDER_LXR_REFUSED = -2 };

typedef struct ThunderLxrApi {
  uint32_t abi;                              /* THUNDER_LXR_ABI */
  uint32_t size;                             /* sizeof(ThunderLxrApi) as the module was built */
  const char* version;                       /* the firmware commit and the module's build, for logs */
  int32_t (*describe)(int32_t kind, ThunderLxrDescribe* out);                                              /* 0, or REFUSED for a kind outside 0..3 */
  size_t (*render_bound)(const ThunderLxrRequest* r);                                                       /* floats the output buffer must hold (an upper bound); 0 = cannot be rendered */
  int32_t (*render_voice)(const ThunderLxrRequest* r, float* out, size_t cap, size_t* n);                   /* the finished pad sound (mono, at r->sr): OK, TOO_SMALL or REFUSED */
  /* ---- PROVISIONAL, the live path: a kit held by the module and rendered a block at a time at the machine's own rate (describe().rate); no allocation, no lock ---- */
  int32_t (*live_open)(const uint8_t* kit, size_t nkit, double bpm);                                       /* power-on state, then this kit (the firmware's parameter bytes, the page's resolved ones) */
  int32_t (*live_process)(int32_t voice, const ThunderLxrEvent* ev, size_t nev, float* out, size_t frames); /* render `frames` of voice 0..5; triggers act at the first block boundary at or after their offset */
} ThunderLxrApi;

/* Registers the module's table (call once at start-up, from the main thread). Returns 0, or -1 if the table's abi / size is not this header's (nothing is registered then). */
int thunder_lxr_register(const ThunderLxrApi* api);
void thunder_lxr_unregister(void);
/* The registered table, or NULL: a closed build always gets NULL. Safe to call from any thread (a plain pointer read; register before the audio starts). */
const ThunderLxrApi* thunder_lxr_api(void);

#ifdef __cplusplus
}
#endif
#endif
