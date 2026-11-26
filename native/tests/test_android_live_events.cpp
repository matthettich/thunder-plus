// M4: the shell's JSON for the effect meter records (native/android/app/src/main/cpp/live_events.cpp): FXLVL -> fx.lvl, FXDYN -> fx.dyn. No Android, no engine: hand-built records
// (the layout the core posts is checked by test_live_meters2; here the decode of the same structs).
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

#include "live_events.h"
#include "thunder/live.h"

static int g_fail = 0;
#define CHECK(c, ...) do { if (!(c)) { g_fail++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

int main() {
  using namespace tpa;
  std::string o;
  {   // FXLVL: linear peaks, 8 numbers, seq and f carried, an absent slot is 0
    thunder_live_ev_fxlvl v; std::memset(&v, 0, sizeof v);
    v.h.type = THUNDER_LIVE_EV_FXLVL; v.h.bytes = sizeof v; v.h.seq = 77; v.h.f = 123456; v.track = 3; v.slots = 0x5;
    v.lvl[0] = 0.5f; v.lvl[2] = 0.125f; v.lvl[5] = -1.f; v.lvl[6] = NAN; v.lvl[7] = INFINITY;
    CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o), "fxlvl decodes");
    CHECK(o == "{\"t\":\"fx.lvl\",\"seq\":77,\"f\":123456,\"tr\":3,\"a\":[0.5,0,0.125,0,0,0,0,0]}", "fx.lvl json: %s", o.c_str());
    CHECK(o.find('\n') == std::string::npos, "one line");
    v.track = 32; CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o) && o.find("\"tr\":32") != std::string::npos, "master's effects are tr 32");
    v.track = 33; CHECK(!eventToJson((const unsigned char*)&v, sizeof v, &o), "a track above 32 is not forwarded");
    v.track = -1; CHECK(!eventToJson((const unsigned char*)&v, sizeof v, &o), "a negative track is not forwarded");
    v.track = 1; CHECK(!eventToJson((const unsigned char*)&v, sizeof v - 8, &o), "a short record is refused");
    CHECK(!eventIsCritical(THUNDER_LIVE_EV_FXLVL) && !eventIsCritical(THUNDER_LIVE_EV_FXDYN), "meters may be dropped first");
  }
  {   // FXDYN: [slot, type string, red, in, out], dB as given (-180 = silence)
    thunder_live_ev_fxdyn v; std::memset(&v, 0, sizeof v);
    v.h.type = THUNDER_LIVE_EV_FXDYN; v.h.bytes = sizeof v; v.h.seq = 9; v.h.f = 48000; v.track = 2; v.n = 2;
    v.ent[0].slot = 1; v.ent[0].type = 7;   v.ent[0].red_db = 0.f;     v.ent[0].in_db = -12.3456f; v.ent[0].out_db = -180.f;
    v.ent[1].slot = 4; v.ent[1].type = 201; v.ent[1].red_db = -6.5f;   v.ent[1].in_db = -9.f;      v.ent[1].out_db = -3.0005f;
    CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o), "fxdyn decodes");
    const char* c7 = thunder_fx_type_name(7); const char* c201 = thunder_fx_type_name(201);
    CHECK(c7 && c201 && !std::strcmp(c7, "comp") && !std::strcmp(c201, "pcomp"), "type names: %s %s", c7 ? c7 : "(null)", c201 ? c201 : "(null)");
    CHECK(o == "{\"t\":\"fx.dyn\",\"seq\":9,\"f\":48000,\"tr\":2,\"d\":[[1,\"comp\",0.000,-12.346,-180.000],[4,\"pcomp\",-6.500,-9.000,-3.000]]}" ||
          o == "{\"t\":\"fx.dyn\",\"seq\":9,\"f\":48000,\"tr\":2,\"d\":[[1,\"comp\",0.000,-12.346,-180.000],[4,\"pcomp\",-6.500,-9.000,-3.001]]}", "fx.dyn json: %s", o.c_str());
    // an entry with a type the core does not name, or a slot out of range, is left out; n is clamped to 8; non-finite dB are 0 / -180
    v.n = 4; v.ent[2].slot = 3; v.ent[2].type = -5; v.ent[3].slot = 9; v.ent[3].type = 7;
    CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o) && o.find("[3,") == std::string::npos && o.find("[9,") == std::string::npos && o.find("[4,\"pcomp\"") != std::string::npos, "bad entries dropped: %s", o.c_str());
    v.n = 100; CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o), "n clamped");
    v.n = 1; v.ent[0].red_db = NAN; v.ent[0].in_db = INFINITY; v.ent[0].out_db = -INFINITY;
    CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o) && o.find("[[1,\"comp\",0.000,-180.000,-180.000]]") != std::string::npos, "non-finite dB: %s", o.c_str());
    v.n = 0; CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o) && o.find("\"d\":[]}") != std::string::npos, "an empty list is valid JSON: %s", o.c_str());
    v.n = 1; v.track = 40; CHECK(!eventToJson((const unsigned char*)&v, sizeof v, &o), "bad track");
    v.track = 0; CHECK(!eventToJson((const unsigned char*)&v, 100, &o), "a short record is refused");
    // the longest line: 8 pump / modular entries stay one JSON line well under the queue's needs
    v.n = 8; for (int i = 0; i < 8; i++) { v.ent[i].slot = (int16_t)i; v.ent[i].type = 201; v.ent[i].red_db = -99.999f; v.ent[i].in_db = -179.999f; v.ent[i].out_db = -179.999f; }
    CHECK(eventToJson((const unsigned char*)&v, sizeof v, &o) && o.size() > 300 && o.back() == '}' && o.find("[7,\"pcomp\"") != std::string::npos, "8 entries: %zu bytes", o.size());
  }
  std::printf(g_fail ? "FAILED: %d\n" : "ok\n", g_fail);
  return g_fail ? 1 : 0;
}
