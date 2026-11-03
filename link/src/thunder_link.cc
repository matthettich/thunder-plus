// thunder_link.node: Ableton Link for the Thunder Plus desktop app (electron/main.js loads it, optionally).
// GPL-2.0-or-later, as the Link SDK it is built with (see link/LICENSE and link/NOTICE.md).
//
// Plain N-API (node_api.h), so one build loads in any Electron / Node with N-API 8: no rebuild per Electron version.
// Everything here runs on the main process's JS thread, so it uses Link's app-thread session state (captureAppSessionState):
// Thunder Plus has no audio callback in this process. No callbacks into JS either: electron/link-session.js polls snapshot()
// every 20 ms while Link is on (what it sends the page anyway), which keeps this file free of thread-safe functions.
#define NAPI_VERSION 8
#include <node_api.h>
#include <ableton/Link.hpp>
#include <chrono>
#include <cmath>
#include <memory>

namespace {
using micros = std::chrono::microseconds;
std::unique_ptr<ableton::Link> gLink;   // made on first use (a Link that isn't enabled sends nothing on the network)

ableton::Link& L(){ if (!gLink) gLink.reset(new ableton::Link(120.0)); return *gLink; }
void cleanup(void*){ if (gLink){ gLink->enable(false); gLink.reset(); } }   // joins Link's threads before the process goes

double num(napi_env env, napi_value v, double d){ double x; return napi_get_value_double(env, v, &x) == napi_ok && std::isfinite(x) ? x : d; }
bool flag(napi_env env, napi_value v){ bool b = false; napi_valuetype t; if (napi_typeof(env, v, &t) == napi_ok && t == napi_boolean) napi_get_value_bool(env, v, &b); return b; }
napi_value mkNum(napi_env env, double x){ napi_value v; napi_create_double(env, x, &v); return v; }
napi_value mkBool(napi_env env, bool b){ napi_value v; napi_get_boolean(env, b, &v); return v; }
void put(napi_env env, napi_value o, const char* k, napi_value v){ napi_set_named_property(env, o, k, v); }
struct Args { napi_value v[4]; size_t n = 4; };
Args args(napi_env env, napi_callback_info info){ Args a; napi_get_cb_info(env, info, &a.n, a.v, nullptr, nullptr); for (size_t i = a.n; i < 4; i++) napi_get_undefined(env, &a.v[i]); return a; }

// enable(on): join / leave the session
napi_value Enable(napi_env env, napi_callback_info info){ auto a = args(env, info); L().enable(flag(env, a.v[0])); return mkBool(env, L().isEnabled()); }
// startStopSync(on): share start / stop with the peers that have it on too
napi_value StartStopSync(napi_env env, napi_callback_info info){ auto a = args(env, info); L().enableStartStopSync(flag(env, a.v[0])); return mkBool(env, L().isStartStopSyncEnabled()); }
// now(): Link's clock, µs
napi_value Now(napi_env env, napi_callback_info){ return mkNum(env, double(L().clock().micros().count())); }
// snapshot(quantum) → {enabled, sync, peers, tempo, beat, phase, playing, playAt, now}: beat and phase at `now` (Link's clock, µs)
napi_value Snapshot(napi_env env, napi_callback_info info){
  auto a = args(env, info); const double q = num(env, a.v[0], 4);
  auto& l = L(); const auto s = l.captureAppSessionState(); const auto t = l.clock().micros();
  napi_value o; napi_create_object(env, &o);
  put(env, o, "enabled", mkBool(env, l.isEnabled())); put(env, o, "sync", mkBool(env, l.isStartStopSyncEnabled()));
  put(env, o, "peers", mkNum(env, double(l.numPeers()))); put(env, o, "tempo", mkNum(env, s.tempo()));
  put(env, o, "beat", mkNum(env, s.beatAtTime(t, q))); put(env, o, "phase", mkNum(env, s.phaseAtTime(t, q)));
  put(env, o, "playing", mkBool(env, s.isPlaying())); put(env, o, "playAt", mkNum(env, double(s.timeForIsPlaying().count())));
  put(env, o, "now", mkNum(env, double(t.count())));
  return o;
}
// setTempo(bpm): from now (any peer may change it; Link is symmetric)
napi_value SetTempo(napi_env env, napi_callback_info info){
  auto a = args(env, info); const double bpm = num(env, a.v[0], 0); if (!(bpm >= 20 && bpm <= 999)) return mkBool(env, false);
  auto& l = L(); auto s = l.captureAppSessionState(); s.setTempo(bpm, l.clock().micros()); l.commitAppSessionState(s); return mkBool(env, true);
}
/* start(atUs, quantum, sync): we start at Link time atUs (the page picked it: the next bar line when there are peers, else
   now plus its lead). Alone, Link's beat 0 is moved there (requestBeatAtTime), so a peer joining later lines its bars up with
   our song; with peers the timeline is never moved (that would jolt them): the page already waited for their bar. sync:
   isPlaying goes true at that moment, so peers with start / stop sync start on the same bar. */
napi_value Start(napi_env env, napi_callback_info info){
  auto a = args(env, info); const double at = num(env, a.v[0], 0), q = num(env, a.v[1], 4); const bool play = flag(env, a.v[2]);
  auto& l = L(); auto s = l.captureAppSessionState(); const micros t(static_cast<long long>(at));
  if (l.numPeers() == 0) s.requestBeatAtTime(0, t, q);
  if (play) s.setIsPlaying(true, t);
  l.commitAppSessionState(s); return mkBool(env, true);
}
// stop(): isPlaying false now (only heard by peers with start / stop sync on)
napi_value Stop(napi_env env, napi_callback_info){
  auto& l = L(); auto s = l.captureAppSessionState(); s.setIsPlaying(false, l.clock().micros()); l.commitAppSessionState(s); return mkBool(env, true);
}
// timeAtBeat(beat, quantum) → µs
napi_value TimeAtBeat(napi_env env, napi_callback_info info){
  auto a = args(env, info); const auto s = L().captureAppSessionState(); return mkNum(env, double(s.timeAtBeat(num(env, a.v[0], 0), num(env, a.v[1], 4)).count()));
}

napi_value Init(napi_env env, napi_value exports){
  napi_add_env_cleanup_hook(env, cleanup, nullptr);
  const napi_property_descriptor P[] = {
    {"enable", nullptr, Enable, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
    {"startStopSync", nullptr, StartStopSync, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
    {"now", nullptr, Now, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
    {"snapshot", nullptr, Snapshot, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
    {"setTempo", nullptr, SetTempo, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
    {"start", nullptr, Start, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
    {"stop", nullptr, Stop, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
    {"timeAtBeat", nullptr, TimeAtBeat, nullptr, nullptr, nullptr, napi_enumerable, nullptr},
  };
  napi_define_properties(env, exports, sizeof(P) / sizeof(P[0]), P);
  return exports;
}
}  // namespace

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
