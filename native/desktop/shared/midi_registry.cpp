#include "midi_registry.h"

#include <algorithm>
#include <cstdio>
#include <set>

#include "text_util.h"

namespace tpd {

namespace {
int utf8Len(const std::string& s) { return (int)s.size(); }
std::string cleanName(const std::string& n) {
  std::string t;
  for (unsigned char c : n) t += c < 0x20 ? ' ' : (char)c;
  size_t a = 0, b = t.size();
  while (a < b && (t[a] == ' ' || t[a] == '\t')) a++;
  while (b > a && (t[b - 1] == ' ' || t[b - 1] == '\t')) b--;
  t = t.substr(a, b - a);
  return t.empty() ? "MIDI device" : t;
}
}  // namespace

std::string midiClampUtf8(const std::string& s, int maxBytes) {
  if (maxBytes <= 0) return "";
  size_t i = 0; int bytes = 0;
  while (i < s.size()) {
    const unsigned char c = (unsigned char)s[i];
    const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 1;   // a stray continuation byte counts as one
    if (bytes + n > maxBytes || i + (size_t)n > s.size()) break;
    bytes += n; i += (size_t)n;
  }
  return s.substr(0, i);
}

std::vector<MidiRow> midiBuildIds(const std::vector<MidiPortScan>& scanIn) {
  std::vector<MidiPortScan> scan = scanIn;
  std::stable_sort(scan.begin(), scan.end(), [](const MidiPortScan& a, const MidiPortScan& b) { return a.key < b.key; });
  std::vector<MidiRow> out;
  std::map<std::string, int> seen;
  std::set<std::string> used;   // the FINAL ids: the core refuses the whole list for a repeated id
  for (const auto& p : scan) {
    const std::string shown0 = cleanName(p.name);
    int count = (seen.count(shown0) ? seen[shown0] : 0) + 1;
    std::string id, shown;
    for (;;) {   // a clash after the suffix or after shortening takes the next number
      const std::string suffix = count > 1 ? " #" + std::to_string(count) : "";
      const int room = kMidiMaxIdBytes - 2 - utf8Len(suffix);
      const std::string cut = utf8Len(shown0) <= room ? shown0 : midiClampUtf8(shown0, room);
      shown = cut + suffix;
      id = "n:" + shown;
      if (used.insert(id).second) break;
      count++;
    }
    seen[shown0] = count;
    out.push_back({p.key, id, shown});
  }
  return out;
}

MidiDiff MidiRegistry::update(const std::vector<MidiPortScan>& scan) {
  const auto rows = midiBuildIds(scan);
  std::vector<MidiEntry> next;
  std::map<int64_t, bool> nu;
  for (const auto& r : rows) {
    auto it = tokens_.find(r.key);
    if (it == tokens_.end()) it = tokens_.emplace(r.key, next_++).first;
    next.push_back({it->second, r.key, r.id, r.name});
    nu[r.key] = true;
  }
  std::map<int64_t, MidiEntry> old;
  for (const auto& e : current_) old[e.key] = e;
  MidiDiff d;
  for (const auto& e : current_) if (!nu.count(e.key)) { d.removed.push_back(e); tokens_.erase(e.key); }
  for (const auto& e : next) {
    auto o = old.find(e.key);
    if (o == old.end()) d.added.push_back(e);
    else if (o->second.id != e.id || o->second.name != e.name) d.changed.push_back(e);
    else d.kept.push_back(e);
  }
  current_ = next;
  return d;
}

std::string midiPortsEventJson(const std::vector<MidiEntry>& outs, const std::vector<MidiEntry>& ins, const MidiPortState ports[4], uint64_t late, uint64_t sent) {
  std::string s = "{\"t\":\"midi.ports\",\"devs\":[";
  for (size_t i = 0; i < outs.size(); i++) { if (i) s += ","; s += "{\"id\":" + jsonQuote(outs[i].id) + ",\"name\":" + jsonQuote(outs[i].name) + "}"; }
  s += "],\"ins\":[";
  for (size_t i = 0; i < ins.size(); i++) { if (i) s += ","; s += "{\"id\":" + jsonQuote(ins[i].id) + ",\"name\":" + jsonQuote(ins[i].name) + ",\"port\":" + std::to_string((unsigned long long)ins[i].token) + "}"; }
  s += "],\"ports\":[";
  for (int i = 0; i < 4; i++) {
    if (i) s += ",";
    char g[48]; std::snprintf(g, sizeof g, "%.0f", ports[i].gear);
    s += "{\"state\":" + std::to_string(ports[i].state) + ",\"id\":" + jsonQuote(ports[i].id) + ",\"name\":" + jsonQuote(ports[i].name) + ",\"gear\":" + g + ",\"clock\":" + std::to_string(ports[i].clock) + "}";
  }
  s += "],\"late\":" + std::to_string((unsigned long long)late) + ",\"sent\":" + std::to_string((unsigned long long)sent) + "}";
  return s;
}

}  // namespace tpd
