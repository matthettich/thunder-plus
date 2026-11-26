// H6: the page's messages to calls (bridge_translator.h), message for message as the Android LiveBridgeTest reads the contract.
#include <cmath>
#include "bridge_translator.h"
#include "check.h"
using namespace tpd;
static Translation T(const char* s) { return translate(s); }
static bool isCall(const Translation& t, Op op) { return t.kind == Translation::kCall && t.call.op == op; }
static bool refusedWith(const Translation& t, const char* part) { return t.kind == Translation::kRefused && t.msg.find(part) != std::string::npos; }
int main() {
  CHECK(typeOf("{\"t\":\"song.load\",\"S\":{}}") == "song.load" && typeOf("{ \"t\" : \"play\" }") == "play" && typeOf("{\"x\":1}") == "" && typeOf("not json") == "", "typeOf");
  CHECK(idOfTail("{\"t\":\"song.load\",\"S\":{\"id\":3},\"rev\":12,\"id\":9}") == 9 && revOfTail("{\"t\":\"song.load\",\"S\":{},\"rev\":12,\"id\":9}") == 12 && idOfTail("{\"t\":\"x\"}") == -1, "id and rev at the tail");
  { auto t = T("{\"t\":\"song.load\",\"title\":\"x\",\"S\":{\"bpm\":1},\"vars\":null,\"rev\":1,\"id\":2}"); CHECK(isCall(t, Op::SongLoad) && t.call.raw.size() > 10, "song.load goes whole"); }
  CHECK(isCall(T("{\"t\":\"kit.load\",\"kit\":{},\"rev\":1}"), Op::KitLoad), "kit.load goes whole");
  { auto t = T("{\"t\":\"hello\",\"v\":1,\"live\":true,\"app\":\"x\",\"id\":4}"); CHECK(isCall(t, Op::Hello) && t.call.live && t.call.id == 4, "hello live"); }
  CHECK(!T("{\"t\":\"hello\",\"v\":1,\"id\":4}").call.live, "hello without live");
  { auto t = T("{\"t\":\"play\",\"mode\":\"sect\",\"row\":8,\"loop\":{\"y0\":2,\"y1\":6},\"only\":[0,3,40],\"seed\":5,\"at\":96000}");
    const Call& c = t.call; CHECK(isCall(t, Op::Play) && c.mode == 1 && c.row == 8 && c.hasLoop && c.y0 == 2 && c.y1 == 6 && c.hasOnly && c.onlyBits == 9 && c.hasSeed && c.seed == 5 && c.at == 96000 && c.pdc == 1, "play fields"); }
  { auto t = T("{\"t\":\"play\"}"); CHECK(isCall(t, Op::Play) && t.call.mode == 0 && t.call.at == kNow && !t.call.hasLoop && !t.call.hasOnly && !t.call.hasSeed, "play defaults"); }
  CHECK(refusedWith(T("{\"t\":\"play\",\"mode\":\"weird\"}"), "unknown play mode") && refusedWith(T("{\"t\":\"play\",\"mode\":7}"), "out of range") && refusedWith(T("{\"t\":\"play\",\"pdc\":3}"), "pdc"), "play refusals");
  CHECK(isCall(T("{\"t\":\"stop\"}"), Op::Stop) && T("{\"t\":\"stop\",\"at\":null}").call.at == kNow && T("{\"t\":\"stop\",\"at\":480}").call.at == 480, "stop and its at");
  CHECK(T("{\"t\":\"bpm\",\"v\":140.5}").call.v == 140.5 && T("{\"t\":\"bpm\",\"bpm\":90}").call.v == 90 && refusedWith(T("{\"t\":\"bpm\"}"), "bpm without"), "bpm");
  { auto t = T("{\"t\":\"loop\",\"y0\":4,\"y1\":8}"); CHECK(isCall(t, Op::Loop) && t.call.hasLoop == 1 && t.call.y0 == 4, "loop with y0 has a loop"); CHECK(T("{\"t\":\"loop\",\"has\":false}").call.hasLoop == 0, "loop has:false"); }
  CHECK(T("{\"t\":\"lead\",\"s\":0.2}").call.v == 0.2, "lead");
  { auto t = T("{\"t\":\"perf\",\"what\":2,\"i\":3,\"v\":0.5}"); CHECK(isCall(t, Op::Perf) && t.call.what == 2 && t.call.index == 3 && t.call.v == 0.5, "perf"); }
  { auto t = T("{\"t\":\"mix.set\",\"tr\":\"m\",\"k\":\"vol\",\"v\":0.7,\"ramp\":480}"); CHECK(isCall(t, Op::MixSet) && t.call.track == 32 && t.call.key == 0 && t.call.v == 0.7 && t.call.ramp == 480, "mix.set master"); }
  CHECK(T("{\"t\":\"mix.set\",\"tr\":3,\"k\":\"mute\",\"v\":1}").call.key == 3 && T("{\"t\":\"mix.set\",\"tr\":3,\"k\":\"out\",\"v\":1}").call.key == 6 && T("{\"t\":\"mix.set\",\"tr\":3,\"k\":\"vol\",\"v\":1}").call.ramp == -1, "mix keys");
  CHECK(refusedWith(T("{\"t\":\"mix.set\",\"tr\":3,\"k\":\"send\",\"v\":1}"), "send") && refusedWith(T("{\"t\":\"mix.set\",\"tr\":40,\"k\":\"vol\",\"v\":1}"), "tr must") && refusedWith(T("{\"t\":\"mix.set\",\"tr\":3,\"k\":\"zzz\",\"v\":1}"), "unknown key"), "mix.set refusals");
  { auto t = T("{\"t\":\"fx.set\",\"tr\":1,\"slot\":2,\"p\":[1,2,3,4,5,6,7,8,9,10,11,12]}"); CHECK(isCall(t, Op::FxParam) && t.call.slot == 2 && t.call.p[11] == 12, "fx.set vector"); }
  CHECK(refusedWith(T("{\"t\":\"fx.set\",\"tr\":1,\"slot\":2,\"p\":3,\"v\":1}"), "12 values") && refusedWith(T("{\"t\":\"fx.set\",\"tr\":1,\"slot\":2,\"p\":[1,2]}"), "exactly 12"), "fx.set single-value form refused");
  { auto t = T("{\"t\":\"fx.plan\",\"tr\":\"m\",\"slots\":[{\"k\":\"eq\"},null],\"bpm\":128}"); CHECK(isCall(t, Op::FxPlan) && t.call.track == 32 && t.call.bpm == 128 && t.call.raw == "[{\"k\":\"eq\"},null]", "fx.plan: slots JSON passed on (%s)", t.call.raw.c_str()); }
  { auto t = T("{\"t\":\"master.config\",\"mst\":{\"a\":1},\"safety\":false}"); CHECK(isCall(t, Op::MasterConfig) && t.call.raw == "{\"a\":1}" && t.call.safety == 0, "master.config"); }
  CHECK(isCall(T("{\"t\":\"chain.edit\",\"ops\":[{\"op\":\"param\"}]}"), Op::ChainEdit) && refusedWith(T("{\"t\":\"chain.edit\"}"), "without ops"), "chain.edit");
  CHECK(T("{\"t\":\"meters.want\",\"rate\":100}").call.hz == 30 && T("{\"t\":\"meters.want\",\"rate\":-1}").call.hz == 0, "meters.want clamps");
  { auto t = T("{\"t\":\"pad.chop\",\"pad\":3,\"loopSecs\":2.5,\"slices\":[0,100,200,300]}"); CHECK(isCall(t, Op::PadChop) && t.call.pad == 3 && t.call.n == 4 && t.call.pts.size() == 4 && t.call.loopSecs == 2.5, "pad.chop"); }
  CHECK(refusedWith(T("{\"t\":\"pad.chop\",\"pad\":3,\"slices\":[0,-1]}"), "slice point") && refusedWith(T("{\"t\":\"pad.chop\",\"slices\":[]}"), "needs pad"), "pad.chop refusals");
  CHECK(T("{\"t\":\"pdc.set\",\"mode\":2}").call.mode == 2 && refusedWith(T("{\"t\":\"pdc.set\",\"mode\":5}"), "mode"), "pdc.set");
  CHECK(isCall(T("{\"t\":\"engine.reset\"}"), Op::EngineReset) && isCall(T("{\"t\":\"pads.commit\",\"n\":3}"), Op::PadsCommit) && isCall(T("{\"t\":\"lxp\",\"st\":2}"), Op::Lxp) && T("{\"t\":\"lxp\",\"st\":2}").call.st == 2, "small commands");
  { auto t = T("{\"t\":\"buf.use\",\"id\":3,\"pad\":9,\"layer\":1,\"kind\":\"var\",\"ii\":4,\"vk\":\"m12\"}"); CHECK(isCall(t, Op::BufUse) && t.call.kind == "var" && t.call.vk == 'm' && t.call.vkNum == 12 && t.call.ii == 4 && t.call.frames == -1, "buf.use var"); }
  { auto t = T("{\"t\":\"buf.use\",\"id\":-1,\"pad\":9,\"layer\":0,\"kind\":\"pad\",\"frames\":0}"); CHECK(isCall(t, Op::BufUse) && t.call.frames == 0 && t.call.kind == "pad", "buf.use pad removal"); }
  CHECK(refusedWith(T("{\"t\":\"buf.use\",\"id\":1,\"pad\":1,\"kind\":\"sf\"}"), "not an input") && refusedWith(T("{\"t\":\"buf.use\",\"id\":1,\"pad\":1,\"kind\":\"var\",\"ii\":1}"), "vk") && refusedWith(T("{\"t\":\"buf.use\",\"pad\":1}"), "needs id"), "buf.use refusals");
  CHECK(isCall(T("{\"t\":\"buf.free\",\"id\":3}"), Op::BufFree), "buf.free");
  { auto t = T("{\"t\":\"midi.cfg\",\"outs\":[\"a\",\"\",\"c\"],\"clockOut\":[true,0,1],\"gearDelay\":[500,-99,12.4],\"clockIn\":\"x\"}"); const Call& c = t.call;
    CHECK(isCall(t, Op::MidiCfg) && c.outs.size() == 4 && c.outs[0] == "a" && c.outs[3] == "" && c.clockOut[0] && !c.clockOut[1] && c.clockOut[2] && c.gear[0] == 200 && c.gear[1] == -50 && c.gear[2] == 12 && c.clockIn, "midi.cfg"); }
  // the messages the live host does not take: refused, and an ask carries its id so an error reply can follow
  { auto t = T("{\"t\":\"dev.list\",\"id\":11}"); CHECK(t.kind == Translation::kRefused && t.replyId == 11 && t.msg.find("H7") != std::string::npos, "dev.list: refused with H7 and the ask's id"); }
  for (const char* m : {"dev.open", "note.on", "note.off", "rec.arm", "rec.start", "rec.stop", "render.start", "render.cancel", "link.set", "song.patch", "kit.patch"}) {
    const std::string s = std::string("{\"t\":\"") + m + "\"}"; CHECK(translate(s).kind == Translation::kRefused, "%s refused", m);
  }
  CHECK(T("{\"t\":\"put.header\"}").kind == Translation::kIgnored && T("{\"t\":\"zzz\"}").kind == Translation::kIgnored && T("garbage").kind == Translation::kIgnored && T("{\"t\":\"play\" ").kind == Translation::kRefused, "ignored, and not JSON");
  CHECK(engineList(false).size() == 26 && engineList(true).size() == 30, "engine list");
  // M4: meters.want with the effect lists (a replacement state), fx.values (an ask), recovery.*
  { auto t = T("{\"t\":\"meters.want\",\"rate\":10}"); CHECK(t.kind == Translation::kCall && t.call.op == Op::MetersWant && t.call.hz == 10, "rate-only meters.want stays MetersWant");
    t = T("{\"t\":\"meters.want\",\"rate\":99,\"fx\":[3,\"m\"],\"dyn\":[5]}"); CHECK(t.kind == Translation::kCall && t.call.op == Op::MetersSet && t.call.hz == 30 && t.call.kinds == 3 && t.call.fxTracks == (((uint64_t)1 << 3) | ((uint64_t)1 << 32) | ((uint64_t)1 << 5)), "lists: union mask, both kinds, master as bit 32, rate clamped");
    t = T("{\"t\":\"meters.want\",\"rate\":5,\"fx\":[],\"dyn\":[]}"); CHECK(t.call.op == Op::MetersSet && t.call.kinds == 0 && t.call.fxTracks == 0, "all off is MetersSet(rate, 0, 0)");
    t = T("{\"t\":\"meters.want\",\"rate\":5,\"fx\":[40]}"); CHECK(t.kind == Translation::kRefused, "a bad track refuses");
    t = T("{\"t\":\"meters.want\",\"rate\":5,\"fx\":\"x\"}"); CHECK(t.kind == Translation::kRefused, "a non-array list refuses");
    t = T("{\"t\":\"meters.want\",\"rate\":5,\"chains\":[1,4],\"master\":true,\"fx\":[2]}"); CHECK(t.call.op == Op::MetersSet && t.call.kinds == (1 | 4 | 8) && t.call.chainsTracks == ((1u << 1) | (1u << 4)) && t.call.fxTracks == (1u << 2), "M3 / M2: chains list -> CHLVL with its own mask, master:true -> MASTERM");
    t = T("{\"t\":\"meters.want\",\"rate\":5,\"chains\":true,\"master\":false,\"trim\":1}"); CHECK(t.call.op == Op::MetersSet && t.call.kinds == 0 && t.call.chainsTracks == 0, "a boolean chains names no track; master:false is off; trim is ignored");
    CHECK(T("{\"t\":\"meters.want\",\"rate\":5,\"chains\":[32]}").kind == Translation::kRefused && T("{\"t\":\"meters.want\",\"rate\":5,\"chains\":[\"m\"]}").kind == Translation::kRefused && T("{\"t\":\"meters.want\",\"rate\":5,\"master\":1}").kind == Translation::kRefused && T("{\"t\":\"meters.want\",\"rate\":5,\"chains\":\"x\"}").kind == Translation::kRefused, "chains 32 / 'm', master not boolean, chains a string: refused"); }
  { auto t = T("{\"t\":\"fx.values\",\"tr\":\"m\",\"slot\":2,\"kind\":\"mod\",\"v\":[0.5,1],\"at\":480,\"id\":7}");
    CHECK(t.kind == Translation::kCall && t.call.op == Op::FxValues && t.call.track == 32 && t.call.slot == 2 && t.call.fxKind == 2 && t.call.vals.size() == 2 && t.call.at == 480 && t.call.id == 7, "fx.values: track, slot, kind, values, at, id");
    for (const char* bad : {"{\"t\":\"fx.values\",\"tr\":0,\"slot\":8,\"kind\":\"pump\",\"v\":[1],\"id\":1}", "{\"t\":\"fx.values\",\"tr\":0,\"slot\":0,\"kind\":\"x\",\"v\":[1],\"id\":1}", "{\"t\":\"fx.values\",\"tr\":0,\"slot\":0,\"kind\":\"pump\",\"v\":[],\"id\":1}",
                            "{\"t\":\"fx.values\",\"tr\":0,\"slot\":0,\"kind\":\"pump\",\"v\":[\"a\"],\"id\":1}", "{\"t\":\"fx.values\",\"tr\":33,\"slot\":0,\"kind\":\"pump\",\"v\":[1],\"id\":1}", "{\"t\":\"fx.values\",\"tr\":0,\"slot\":0,\"kind\":\"pump\",\"id\":1}"}) {
      auto r = T(bad); CHECK(r.kind == Translation::kRefused && r.replyId == 1, "fx.values refused with the ask's id: %s", bad); }
    std::string many = "{\"t\":\"fx.values\",\"tr\":0,\"slot\":0,\"kind\":\"pump\",\"v\":["; for (int i = 0; i < 513; i++) many += i ? ",0" : "0"; many += "],\"id\":2}"; CHECK(translate(many).kind == Translation::kRefused, "513 values: refused"); }
  CHECK(T("{\"t\":\"recovery.put\",\"project\":\"x\"}").call.op == Op::RecoveryPut && T("{\"t\":\"recovery.put\",\"project\":\"\"}").kind == Translation::kIgnored && T("{\"t\":\"recovery.put\",\"project\":3}").kind == Translation::kIgnored, "recovery.put: a non-empty string only");
  CHECK(T("{\"t\":\"recovery.get\",\"id\":4}").call.op == Op::RecoveryGet && T("{\"t\":\"recovery.drop\",\"id\":5}").call.id == 5, "recovery.get / drop carry the id");
  // L-5: a number outside the int range is clamped, never undefined behaviour
  { auto t = T("{\"t\":\"play\",\"mode\":\"song\",\"row\":1e30,\"track\":-1e30,\"at\":1e300}"); CHECK(t.kind == Translation::kCall && t.call.row == INT32_MAX && t.call.track == INT32_MIN && t.call.at == INT64_MAX, "huge numbers clamp");
    t = T("{\"t\":\"mix.set\",\"tr\":1e30,\"k\":\"vol\",\"v\":1}"); CHECK(t.kind == Translation::kRefused, "a track outside the range is refused, not wrapped"); }
  DONE("test_desktop_translator");
}
