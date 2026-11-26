// H6: the PURE translation of the page's CORE-BRIDGE v1 messages (docs/port/CORE-BRIDGE.md sections 3 and 7) into calls on the live host. A C++ port of the Android shell's
// BridgeTranslator.kt (native/android/.../BridgeTranslator.kt), whose header comment is the shell's reading of the contract; the desktop shell follows it message for message so that both shells
// behave alike. DUPLICATION, noted in docs/port/H6-NOTES.md: ~150 lines of rules in Kotlin and here; when one changes the other follows (tests/test_translator.cpp mirrors LiveBridgeTest).
// No platform type, no thread, no I/O. Differences from the Android translator, all on purpose: dev.list / dev.open and midi.cfg answer "H7" (audio devices and MIDI are the next task), and
// the shell's `caps` say so.
#ifndef TPD_BRIDGE_TRANSLATOR_H
#define TPD_BRIDGE_TRANSLATOR_H

#include <cstdint>
#include <string>
#include <vector>

namespace tpd {

constexpr int64_t kNow = -1;   // THUNDER_LIVE_NOW as the translator uses it: absent or null `at`

enum class Op {
  SongLoad, KitLoad, Play, Stop, Hold, Bpm, Loop, Lead, Perf, MixSet, FxPlan, FxCommit, FxParam, MasterConfig, ChainEdit, MetersWant, PadsCommit, PadChop, PdcSet, EngineReset,
  BufUse, BufFree, Hello, MidiCfg, Lxp
};

// One flat record for every call: only the fields of its `op` are meaningful.
struct Call {
  Op op = Op::Hello;
  std::string raw;               // SongLoad / KitLoad: the whole message text; FxPlan: the slots JSON; MasterConfig: the mst JSON; ChainEdit: the ops JSON
  int64_t at = kNow;             // every timed command (Play: t0)
  int id = -1;                   // Hello: the ask's id
  bool live = false;             // Hello
  // Play
  int mode = 0, row = 0, track = 0, sect = -1, phrase = -1, hasLoop = 0, y0 = 0, y1 = 0, hasSeed = 0, seed = 0, pdc = 1, hasOnly = 0; uint64_t onlyBits = 0;
  // Bpm / Lead / Perf / MixSet value, MixSet ramp, Perf what / index, MixSet key, FxParam slot, FxPlan bpm, MetersWant hz, PadChop loopSecs
  double v = 0, ramp = -1, bpm = 120, hz = 0, loopSecs = 0;
  int key = 0, what = -1, index = 0, slot = -1, safety = 1, n = 0;   // MasterConfig safety, PadChop / PadsCommit n
  double p[12] = {0};            // FxParam
  std::vector<double> pts;       // PadChop slice points in frames
  // BufUse / BufFree / PadChop: id, pad, layer, kind ("pad" | "var"), ii, vk letter ('g' | 'm'), vk number, frames (-1 = a normal take)
  int bufId = -1, pad = 0, layer = 0, ii = 0, vk = 'g', vkNum = 0, frames = -1; std::string kind;
  // MidiCfg
  std::vector<std::string> outs; bool clockOut[4] = {false, false, false, false}; double gear[4] = {0, 0, 0, 0}; bool clockIn = false;
  // Lxp
  int st = 0;
};

struct Translation {
  enum Kind { kCall, kRefused, kIgnored } kind = kIgnored;
  Call call;                     // kCall
  std::string where, msg;        // kRefused: the `err {where, msg, fatal:false}` event; replyId >= 0: it was an ask, so an error reply carries it too
  int replyId = -1;
  std::string type;              // kIgnored
};

// The message's type without parsing the whole text (a song.load is a megabyte): the "t" member near the front, which JSON.stringify of {t: ..} writes first. "" when absent.
std::string typeOf(const std::string& raw);
// song.load / kit.load: the id and rev the page puts at the TAIL (key order matters: the shell never parses the song).
int idOfTail(const std::string& raw);
int64_t revOfTail(const std::string& raw);
// The engines the core's pad renderer has, for `hello` caps (the same list as the Android shell; LXR ones only when the module is registered).
std::vector<std::string> engineList(bool hasLxr);

Translation translate(const std::string& raw);

}  // namespace tpd
#endif
