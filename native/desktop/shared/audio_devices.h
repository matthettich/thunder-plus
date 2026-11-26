// H7: the audio device model of the desktop shell, with no OS call in it (tested on Linux: tests/test_audio_devices.cpp). The OS part (miniaudio's context, CoreAudio) is audio_miniaudio.cpp and only fills
// AudioEndpoint rows. What this file owns: merging a device's playback and capture entries into one record (the Electron helper's rule: io/README.md), the JSON of `dev.list`, the validation of `dev.open`,
// choosing the output when the wanted one is gone (fall back to the system default, say so), and the saved choice (audio.json).
// Ids (as the Electron helper's): "out:" / "in:" + the raw device id bytes in hex (CoreAudio: the device's UID string), so a choice survives a restart and a re-plug of the same hardware. "" = the system default.
#ifndef TPD_AUDIO_DEVICES_H
#define TPD_AUDIO_DEVICES_H

#include <memory>
#include <string>
#include <vector>

namespace tpd {

// One side of one device as the OS lists it.
struct AudioEndpoint {
  bool input = false;                 // true: a capture entry
  std::string raw;                    // the raw id bytes in hex (no prefix)
  std::string name, backend;
  int channels = 0;                   // the widest native channel count; 0 = not known (reported as 2)
  std::vector<int> rates;             // the native sample rates (an "any" entry, 0, is left out)
  bool isDefault = false;
};

// One device as the page sees it (the helper's `devices` record plus `ins` / `outs`, the names CORE-BRIDGE `dev.list` uses).
struct AudioDevice {
  std::string id;                     // inId, or outId when there is no capture side
  std::string inId, outId;            // "" when the device has no such side
  std::string name, backend;
  int inputs = 0, outputs = 0;        // 0 = no such side
  std::vector<int> rates;             // sorted, unique, both sides
  bool isDefaultIn = false, isDefaultOut = false;
};

// A capture and a playback entry become ONE record only when the name AND the raw id are equal; unsure stays separate (the helper's rule). Order: first appearance. A channel count of 0 is reported as 2.
std::vector<AudioDevice> mergeAudioEndpoints(const std::vector<AudioEndpoint>& eps);

// The array for `dev.list`'s reply.
std::string audioDevicesJson(const std::vector<AudioDevice>& devs);

// What `dev.open` asks for. out / in: an id from dev.list, "" = the system default (out) / no input (in). sr: 0 = the device's own, else 8000..384000. buf: 0 = leave the device as it is, else 16..8192 frames.
struct DevOpenRequest { std::string out, in; int sr = 0, buf = 0; bool hasIn = false; };
// Clamps and checks; the error is a readable text. `sr` and `buf` outside their ranges are refused, not clamped (a typo must not become another rate).
bool validateDevOpen(const DevOpenRequest& r, std::string* err);

// The output that will open: `wanted` when it is listed and has outputs; the system default when `wanted` is "" ; otherwise the default again with fellBack = true. id "" means "the default".
struct OutChoice { std::string id; bool fellBack = false; std::string name; };
OutChoice chooseOutput(const std::vector<AudioDevice>& devs, const std::string& wantedOutId);
// The same for the input: "" in = no input; a wanted id that is gone -> fellBack with id "" (no input; the page is told).
OutChoice chooseInput(const std::vector<AudioDevice>& devs, const std::string& wantedInId);

// What the page chose last, kept between runs (the page also keeps its own copy and sends dev.open after hello).
struct AudioSettings { std::string out, in; int sr = 0, buf = 0; bool operator==(const AudioSettings& o) const { return out == o.out && in == o.in && sr == o.sr && buf == o.buf; } };
class AudioSettingsStore {
 public:
  explicit AudioSettingsStore(std::string dir) : dir_(std::move(dir)) {}   // "" = nothing is kept
  AudioSettings load() const;                                               // defaults when there is no file or it is damaged
  bool save(const AudioSettings& s) const;                                  // atomic (temp file, then rename)
 private:
  std::string dir_;
};

// The OS side of the list (audio_miniaudio.cpp; a fake in the tests). list() may block for a moment (a driver call): never on the audio thread.
class AudioEnumerator {
 public:
  virtual ~AudioEnumerator() = default;
  virtual std::vector<AudioDevice> list() = 0;
};
// One "null (no device)" device with 2 in / 2 out: CI and headless runs.
std::shared_ptr<AudioEnumerator> makeNullEnumerator();

}  // namespace tpd
#endif
