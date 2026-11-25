// N2 H2: see midi_in.h.
#include "midi_in.h"

#include <atomic>
#include <cstdio>

namespace tpa {

namespace {
std::atomic<MidiInSink> g_sink{nullptr};
// data bytes after a status byte: channel messages by their high nibble; -1 = not forwarded
int dataBytes(uint8_t st) {
  if (st >= 0x80 && st < 0xF0) { const uint8_t ty = st & 0xF0; return (ty == 0xC0 || ty == 0xD0) ? 1 : 2; }
  switch (st) { case 0xF2: return 2; default: return -1; }
}
}  // namespace

void MidiInParser::feed(const uint8_t* bytes, int n, Fn fn, void* user) {
  for (int i = 0; i < n; i++) {
    const uint8_t c = bytes[i];
    if (c >= 0xF8) {   // System Real-Time: at once, anywhere (it does not touch the message being collected)
      if (c == 0xF8 || c == 0xFA || c == 0xFB || c == 0xFC) { MidiInMsg m; m.b[0] = c; m.b[1] = m.b[2] = 0; m.len = 1; fn(user, m); }
      else otherSkipped++;   // FE active sensing, FF reset, F9 / FD undefined
      continue;
    }
    if (c >= 0x80) {       // a status byte
      inSysex_ = false; skipping_ = false; have_ = 0; need_ = 0;   // ANY status byte ends the open message (verify-n2 F8: a truncated message followed by F7 / F0 must not turn the next data bytes into a phantom one)
      if (c == 0xF0) { inSysex_ = true; status_ = 0; sysexSkipped++; continue; }
      if (c == 0xF7) { status_ = 0; continue; }    // end of a SysEx (or stray)
      const int d = dataBytes(c);
      if (d < 0) { status_ = 0; skipping_ = true; otherSkipped++; continue; }   // F1, F3, F4, F5, F6: dropped with their data
      cur_ = c; need_ = d;
      if (c < 0xF0) status_ = c; else status_ = 0;   // only channel messages keep a running status (F2 does not)
      continue;
    }
    // a data byte
    if (inSysex_ || skipping_) continue;
    if (need_ == 0) {   // no message open: running status starts a new one
      if (!status_) { strayData++; continue; }
      cur_ = status_; need_ = dataBytes(status_); have_ = 0;
    }
    data_[have_++] = c;
    if (have_ == need_) {
      MidiInMsg m; m.b[0] = cur_; m.b[1] = data_[0]; m.b[2] = need_ == 2 ? data_[1] : 0; m.len = (uint8_t)(1 + need_);
      fn(user, m);
      need_ = 0; have_ = 0;
    }
  }
}

bool midiInIsNote(const MidiInMsg& m) { const uint8_t ty = m.b[0] & 0xF0; return ty == 0x80 || ty == 0x90; }

std::string midiInJson(int64_t f, int64_t wallNs, uint64_t port, const MidiInMsg& m) {
  char b[160];
  const int n = std::snprintf(b, sizeof b, "{\"t\":\"midi.in\",\"f\":%lld,\"wallNs\":%lld,\"port\":%llu,\"bytes\":[%u", (long long)f, (long long)wallNs, (unsigned long long)port, (unsigned)m.b[0]);
  std::string s(b, (size_t)n);
  for (int i = 1; i < m.len; i++) { char t[8]; std::snprintf(t, sizeof t, ",%u", (unsigned)m.b[i]); s += t; }
  return s + "]}";
}

void setMidiInSink(MidiInSink s) { g_sink.store(s, std::memory_order_release); }
MidiInSink midiInSink() { return g_sink.load(std::memory_order_acquire); }

}  // namespace tpa
