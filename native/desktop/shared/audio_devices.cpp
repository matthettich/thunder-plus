#include "audio_devices.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

#include "rt/json.h"
#include "text_util.h"

namespace tpd {
namespace fs = std::filesystem;

std::vector<AudioDevice> mergeAudioEndpoints(const std::vector<AudioEndpoint>& eps) {
  std::vector<AudioDevice> out;
  std::vector<bool> used(eps.size(), false);
  auto fill = [](AudioDevice& d, const AudioEndpoint& e) {
    const int ch = e.channels > 0 ? e.channels : 2;
    if (e.input) { d.inId = "in:" + e.raw; d.inputs = ch; d.isDefaultIn = e.isDefault; } else { d.outId = "out:" + e.raw; d.outputs = ch; d.isDefaultOut = e.isDefault; }
    for (int r : e.rates) if (r > 0 && std::find(d.rates.begin(), d.rates.end(), r) == d.rates.end()) d.rates.push_back(r);
  };
  for (size_t i = 0; i < eps.size(); i++) {
    if (used[i]) continue;
    used[i] = true;
    AudioDevice d; d.name = eps[i].name; d.backend = eps[i].backend;
    fill(d, eps[i]);
    for (size_t j = i + 1; j < eps.size(); j++) {
      if (used[j] || eps[j].input == eps[i].input || eps[j].raw != eps[i].raw || eps[j].name != eps[i].name) continue;
      used[j] = true; fill(d, eps[j]); break;
    }
    std::sort(d.rates.begin(), d.rates.end());
    d.id = d.inId.empty() ? d.outId : d.inId;
    out.push_back(std::move(d));
  }
  return out;
}

std::string audioDevicesJson(const std::vector<AudioDevice>& devs) {
  std::string s = "[";
  for (size_t i = 0; i < devs.size(); i++) {
    const AudioDevice& d = devs[i];
    if (i) s += ",";
    s += "{\"id\":" + jsonQuote(d.id) + ",\"inId\":" + (d.inId.empty() ? "null" : jsonQuote(d.inId)) + ",\"outId\":" + (d.outId.empty() ? "null" : jsonQuote(d.outId)) + ",\"name\":" + jsonQuote(d.name) + ",\"backend\":" + jsonQuote(d.backend) +
         ",\"inputs\":" + std::to_string(d.inputs) + ",\"outputs\":" + std::to_string(d.outputs) + ",\"ins\":" + std::to_string(d.inputs) + ",\"outs\":" + std::to_string(d.outputs) + ",\"rates\":[";
    for (size_t k = 0; k < d.rates.size(); k++) { if (k) s += ","; s += std::to_string(d.rates[k]); }
    s += std::string("],\"isDefaultIn\":") + (d.isDefaultIn ? "true" : "false") + ",\"isDefaultOut\":" + (d.isDefaultOut ? "true" : "false") + "}";
  }
  return s + "]";
}

bool validateDevOpen(const DevOpenRequest& r, std::string* err) {
  auto bad = [&](const char* m) { if (err) *err = m; return false; };
  if (r.sr != 0 && (r.sr < 8000 || r.sr > 384000)) return bad("dev.open: sr must be 0 (the device's own) or 8000..384000");
  if (r.buf != 0 && (r.buf < 16 || r.buf > 8192)) return bad("dev.open: buf must be 0 (leave the device as it is) or 16..8192 frames");
  if (r.out.size() > 512 || r.in.size() > 512) return bad("dev.open: an id is too long");
  return true;
}

OutChoice chooseOutput(const std::vector<AudioDevice>& devs, const std::string& wanted) {
  OutChoice c;
  if (wanted.empty()) { for (const auto& d : devs) if (d.isDefaultOut && !d.outId.empty()) { c.name = d.name; break; } return c; }
  for (const auto& d : devs) if (d.outId == wanted && d.outputs > 0) { c.id = wanted; c.name = d.name; return c; }
  c.fellBack = true;
  for (const auto& d : devs) if (d.isDefaultOut && !d.outId.empty()) { c.name = d.name; break; }
  return c;
}

OutChoice chooseInput(const std::vector<AudioDevice>& devs, const std::string& wanted) {
  OutChoice c;
  if (wanted.empty()) return c;
  for (const auto& d : devs) if (d.inId == wanted && d.inputs > 0) { c.id = wanted; c.name = d.name; return c; }
  c.fellBack = true;
  return c;
}

AudioSettings AudioSettingsStore::load() const {
  AudioSettings s;
  if (dir_.empty()) return s;
  std::ifstream f((fs::u8path(dir_) / "audio.json"), std::ios::binary);
  if (!f) return s;
  std::string t((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  thunder::JsonDoc d;
  if (t.size() > 4096 || !d.parse(t)) return s;
  const auto r = d.root();
  s.out = std::string(r.str("out")); s.in = std::string(r.str("in"));
  const double sr = r.num("sr", 0), buf = r.num("buf", 0);
  s.sr = (sr == 0 || (sr >= 8000 && sr <= 384000)) ? (int)sr : 0;
  s.buf = (buf == 0 || (buf >= 16 && buf <= 8192)) ? (int)buf : 0;
  if (s.out.size() > 512) s.out.clear();
  if (s.in.size() > 512) s.in.clear();
  return s;
}

bool AudioSettingsStore::save(const AudioSettings& s) const {
  if (dir_.empty()) return false;
  std::error_code ec;
  fs::create_directories(fs::u8path(dir_), ec);
  const fs::path p = fs::u8path(dir_) / "audio.json", t = fs::u8path(dir_) / "audio.json.tmp";
  { std::ofstream o(t, std::ios::binary | std::ios::trunc); if (!o) return false;
    o << "{\"out\":" << jsonQuote(s.out) << ",\"in\":" << jsonQuote(s.in) << ",\"sr\":" << s.sr << ",\"buf\":" << s.buf << "}"; o.flush(); if (!o) return false; }
  fs::rename(t, p, ec);
  return !ec;
}

namespace {
class NullEnum : public AudioEnumerator {
 public:
  std::vector<AudioDevice> list() override {
    AudioDevice d; d.id = "in:6e756c6c"; d.inId = d.id; d.outId = "out:6e756c6c"; d.name = "null (no device)"; d.backend = "null"; d.inputs = 2; d.outputs = 2; d.rates = {44100, 48000}; d.isDefaultIn = d.isDefaultOut = true;
    return {d};
  }
};
}  // namespace
std::shared_ptr<AudioEnumerator> makeNullEnumerator() { return std::make_shared<NullEnum>(); }

}  // namespace tpd
