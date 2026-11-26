// H7: the audio device model (audio_devices.h): merging, the dev.list JSON, dev.open validation, the output / input choice with the fall-back, the saved settings.
#include <filesystem>
#include "audio_devices.h"
#include "check.h"
#include "rt/json.h"

using namespace tpd;
static AudioEndpoint ep(bool in, const char* raw, const char* name, int ch, std::vector<int> rates, bool def = false) { AudioEndpoint e; e.input = in; e.raw = raw; e.name = name; e.backend = "Core Audio"; e.channels = ch; e.rates = rates; e.isDefault = def; return e; }

int main() {
  // merge: the same name AND raw id becomes one record; different raw ids stay apart; 0 channels = 2
  auto devs = mergeAudioEndpoints({ep(false, "aa", "MacBook Speakers", 2, {44100, 48000}, true), ep(false, "bb", "500R8", 2, {44100, 48000, 96000}), ep(true, "bb", "500R8", 28, {48000, 96000}), ep(true, "cc", "MacBook Mic", 0, {48000}, true),
                                    ep(true, "dd", "Same name", 2, {48000}), ep(false, "ee", "Same name", 2, {48000})});
  CHECK(devs.size() == 5, "five records: %zu", devs.size());
  CHECK(devs[0].name == "MacBook Speakers" && devs[0].outId == "out:aa" && devs[0].inId.empty() && devs[0].isDefaultOut && devs[0].outputs == 2 && devs[0].id == "out:aa", "speakers: out only");
  CHECK(devs[1].name == "500R8" && devs[1].outId == "out:bb" && devs[1].inId == "in:bb" && devs[1].inputs == 28 && devs[1].outputs == 2 && devs[1].id == "in:bb", "the interface is ONE record, 28 inputs");
  CHECK((devs[1].rates == std::vector<int>{44100, 48000, 96000}), "rates merged, sorted, unique");
  CHECK(devs[2].inputs == 2 && devs[2].isDefaultIn, "an unknown channel count is 2, the default input is marked");
  CHECK(devs[3].inId == "in:dd" && devs[4].outId == "out:ee" && devs[3].outId.empty(), "same name, different raw id: two records");

  thunder::JsonDoc d;
  CHECK(d.parse(audioDevicesJson(devs)) && d.root().isArr() && d.root().size() == 5, "the list is a JSON array");
  { auto r = d.root().at(1); CHECK(r.str("inId") == "in:bb" && r.num("ins") == 28 && r.num("outs") == 2 && r.num("inputs") == 28 && r.arr("rates").size() == 3, "record fields");
    auto s0 = d.root().at(0); CHECK(s0.get("inId").isNull() && s0.boolean("isDefaultOut"), "no capture side is null"); }

  std::string e; DevOpenRequest r;
  CHECK(validateDevOpen(r, &e), "the defaults are valid");
  r.sr = 48000; r.buf = 128; CHECK(validateDevOpen(r, &e), "48000 / 128 is valid");
  r.sr = 7; CHECK(!validateDevOpen(r, &e) && e.find("sr") != std::string::npos, "a bad rate is refused, not clamped");
  r.sr = 0; r.buf = 99999; CHECK(!validateDevOpen(r, &e), "a bad buffer is refused");
  r.buf = 0; r.out = std::string(600, 'x'); CHECK(!validateDevOpen(r, &e), "a long id is refused");

  auto c = chooseOutput(devs, "");
  CHECK(c.id.empty() && !c.fellBack && c.name == "MacBook Speakers", "the default output: id empty, named");
  c = chooseOutput(devs, "out:bb"); CHECK(c.id == "out:bb" && !c.fellBack && c.name == "500R8", "a listed output");
  c = chooseOutput(devs, "out:zz"); CHECK(c.id.empty() && c.fellBack && c.name == "MacBook Speakers", "an unlisted output falls back to the default and says so");
  c = chooseOutput(devs, "out:cc"); CHECK(c.fellBack, "an input-only device is not an output");
  c = chooseInput(devs, "in:bb"); CHECK(c.id == "in:bb" && !c.fellBack, "a listed input");
  c = chooseInput(devs, "in:gone"); CHECK(c.id.empty() && c.fellBack, "a gone input: no input, reported");
  c = chooseInput(devs, ""); CHECK(c.id.empty() && !c.fellBack, "no input wanted");

  namespace fs = std::filesystem;
  const std::string dir = (fs::temp_directory_path() / "tpd-audio-settings-test").string();
  fs::remove_all(dir);
  AudioSettingsStore st(dir);
  CHECK(st.load() == AudioSettings(), "no file: defaults");
  AudioSettings a; a.out = "out:bb"; a.in = "in:bb"; a.sr = 96000; a.buf = 128;
  CHECK(st.save(a) && st.load() == a, "saved and read back");
  { FILE* f = std::fopen((dir + "/audio.json").c_str(), "wb"); std::fputs("{\"out\":\"out:x\",\"sr\":7,\"buf\":3}", f); std::fclose(f); }
  { AudioSettings b = st.load(); CHECK(b.out == "out:x" && b.sr == 0 && b.buf == 0, "a bad rate / buffer in the file is dropped, the id kept"); }
  { FILE* f = std::fopen((dir + "/audio.json").c_str(), "wb"); std::fputs("not json", f); std::fclose(f); CHECK(st.load() == AudioSettings(), "a damaged file: defaults"); }
  CHECK(!AudioSettingsStore("").save(a) && AudioSettingsStore("").load() == AudioSettings(), "no folder: nothing is kept");
  fs::remove_all(dir);
  auto ne = makeNullEnumerator()->list(); CHECK(ne.size() == 1 && ne[0].isDefaultOut && ne[0].inputs == 2, "the null enumerator");
  DONE("audio_devices");
}
