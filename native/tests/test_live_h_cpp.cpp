// U37 P0: a C++ translation unit that includes BOTH headers of the live host (the C API and the class it wraps) and checks that they agree: the constants the class takes from the API, the sizes it relies on, and
// that the C header is usable from C++17 without -fexceptions / -frtti. Compile-only in P0 (g++ -std=c++17 -fno-exceptions -fno-rtti -Wall -Wextra -fsyntax-only, with the include path of the pads snapshot of
// PR #276: live_host.h includes render/pads_snap.h). Not registered in CMake (P2 registers the real test beside it).
#include "thunder/live.h"
#include "live/live_host.h"

using namespace thunder;

static_assert(THUNDER_LIVE_QUANTUM == kRenderQuantum, "the quantum is the renderer's");
static_assert(THUNDER_LIVE_QUANTUM == kLiveQuantum, "");
static_assert(THUNDER_LIVE_TRACKS == kSongNT && THUNDER_LIVE_MASTER == kSongNT, "tracks 0..31, the master column is kSongNT");
static_assert(THUNDER_LIVE_PADS == kRenderPads, "pads 0..255");
static_assert(THUNDER_LIVE_METER_FLOATS == kMeterFloats, "the meters record carries MeterEvent::a");
static_assert(THUNDER_LIVE_CMD_RING == kLiveCmdRing && THUNDER_LIVE_CMD_WAIT == kLiveCmdWait && THUNDER_LIVE_FXPARAM_RING == kLiveFxParamRing, "ring sizes of the contract");
static_assert(sizeof(thunder_live_ev_meters) - sizeof(thunder_live_ev_head) >= sizeof(MeterEvent::a), "a METERS record holds a MeterEvent's floats");
static_assert(THUNDER_LIVE_MAX_FONTS == THUNDER_RENDER_MAX_FONTS, "");
static_assert(sizeof(LiveEvSlot) >= THUNDER_LIVE_EV_MAX, "an event slot holds the longest record");
static_assert(sizeof(void*) != 8 || sizeof(LiveCmd) == 56, "LiveCmd");
static_assert(sizeof(thunder_live_stats) == 320 && sizeof(thunder_live_opts) == 64 && sizeof(thunder_live_play_opts) == 88, "the sizes live.h documents");
static_assert(static_cast<int>(MixKey::Vol) == THUNDER_LIVE_MIX_VOL && static_cast<int>(MixKey::Trim) == THUNDER_LIVE_MIX_TRIM && static_cast<int>(MixKey::Pan) == THUNDER_LIVE_MIX_PAN &&
              static_cast<int>(MixKey::Mute) == THUNDER_LIVE_MIX_MUTE && static_cast<int>(MixKey::Solo) == THUNDER_LIVE_MIX_SOLO && static_cast<int>(MixKey::Out) == THUNDER_LIVE_MIX_OUT,
              "the C mixer keys are MixKey's values (Send is 5 and is refused)");
static_assert(static_cast<int>(SeqMode::Song) == THUNDER_LIVE_MODE_SONG && static_cast<int>(SeqMode::Sect) == THUNDER_LIVE_MODE_SECT && static_cast<int>(SeqMode::Phrase) == THUNDER_LIVE_MODE_PHRASE, "the C modes");
static_assert(static_cast<int>(PadRoute::Sampler) == THUNDER_RENDER_ROUTE_SAMPLER && static_cast<int>(PadRoute::Live) == THUNDER_RENDER_ROUTE_LIVE && static_cast<int>(PadRoute::SoundFont) == THUNDER_RENDER_ROUTE_SF, "the C routes");
static_assert(static_cast<int>(PdcMode::Off) == 0 && static_cast<int>(PdcMode::Auto) == 1 && static_cast<int>(PdcMode::On) == 2, "pdc_mode of the play options");

int main() { return 0; }   // compile and size checks only: P0 has no bodies to link
