// N2 H1b: see live_events.h.
#include "live_events.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include "thunder/live.h"

namespace tpa {

namespace {
void ap(std::string* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void ap(std::string* o, const char* fmt, ...) {
  char b[256]; va_list a; va_start(a, fmt); int n = std::vsnprintf(b, sizeof b, fmt, a); va_end(a);
  if (n > 0) o->append(b, (size_t)(n < (int)sizeof b ? n : (int)sizeof b - 1));
}
const char* whereName(int w) {
  switch (w) { case THUNDER_LIVE_WHERE_CMD: return "cmd"; case THUNDER_LIVE_WHERE_SESSION: return "session"; case THUNDER_LIVE_WHERE_REANCHOR: return "reanchor";
    case THUNDER_LIVE_WHERE_MIDI: return "midi"; case THUNDER_LIVE_WHERE_VOICES: return "voices"; case THUNDER_LIVE_WHERE_FATAL: return "fatal"; default: return "unknown"; }
}
template <class T> bool take(const unsigned char* r, int bytes, T* out) { if (bytes < (int)sizeof(T)) return false; std::memcpy(out, r, sizeof(T)); return true; }
}  // namespace

bool eventIsCritical(int type) {
  return type == THUNDER_LIVE_EV_STATE || type == THUNDER_LIVE_EV_HELD || type == THUNDER_LIVE_EV_SONG || type == THUNDER_LIVE_EV_PADS || type == THUNDER_LIVE_EV_ERROR;
}

bool eventToJson(const unsigned char* r, int bytes, std::string* out) {
  thunder_live_ev_head h;
  if (!take(r, bytes, &h) || h.bytes > bytes) return false;
  out->clear();
  switch (h.type) {
    case THUNDER_LIVE_EV_CLOCK: { thunder_live_ev_clock c; if (!take(r, bytes, &c)) return false;
      ap(out, "{\"t\":\"clock\",\"f\":%lld,\"wallNs\":%lld,\"sr\":%d,\"outLat\":%d,\"under\":%u,\"voices\":%d,\"dropped\":%u}", (long long)c.h.f, (long long)c.wall_ns, c.sr, c.out_lat, c.under, c.voices, c.evt_dropped); return true; }
    case THUNDER_LIVE_EV_POS: { thunder_live_ev_pos p; if (!take(r, bytes, &p)) return false;
      ap(out, "{\"t\":\"pos\",\"f\":%lld,\"row\":%d,\"step\":%d,\"cr\":%d,\"bpm\":%.1f}", (long long)p.h.f, p.row, p.step, p.cr, p.bpm10 / 10.0); return true; }
    case THUNDER_LIVE_EV_ROWS: case THUNDER_LIVE_EV_HELD: {
      thunder_live_ev_rows w; if (!take(r, bytes, &w)) return false;
      ap(out, "{\"t\":\"%s\",\"f\":%lld,\"evs\":[", h.type == THUNDER_LIVE_EV_ROWS ? "rows" : "held", (long long)w.h.f);
      int off = (int)sizeof w, first = 1;
      for (int t = 0; t < 33; t++) {
        if (!((w.valid >> t) & 1u)) continue;
        thunder_live_ev_rowent e; if (off + (int)sizeof e > bytes) return false;
        std::memcpy(&e, r + off, sizeof e); off += (int)sizeof e;
        ap(out, "%s{\"tr\":%d,\"row\":%d,\"cr\":%d,\"step\":%d,\"phrase\":%d}", first ? "" : ",", t, e.row, e.cr, e.step, e.phrase); first = 0;
      }
      out->append("]}"); return true; }
    case THUNDER_LIVE_EV_METERS: { thunder_live_ev_meters m; if (!take(r, bytes, &m)) return false;
      uint32_t cnt; std::memcpy(&cnt, &m.reserved[0], sizeof cnt);
      ap(out, "{\"t\":\"meters\",\"seq\":%u,\"at\":%lld,\"a\":[", cnt, (long long)m.h.f);
      for (int i = 0; i < THUNDER_LIVE_METER_FLOATS; i++) ap(out, "%s%.5g", i ? "," : "", (double)m.a[i]);
      out->append("]}"); return true; }
    case THUNDER_LIVE_EV_STATE: { thunder_live_ev_state s; if (!take(r, bytes, &s)) return false;
      const char* n = s.state == THUNDER_LIVE_ST_PLAYING ? "playing" : s.state == THUNDER_LIVE_ST_STOPPED ? "stopped" : s.state == THUNDER_LIVE_ST_ENDED ? "ended" : "unknown";
      ap(out, "{\"t\":\"state\",\"state\":\"%s\",\"arg\":%d,\"f\":%lld}", n, s.arg, (long long)s.h.f); return true; }
    case THUNDER_LIVE_EV_SONG: { thunder_live_ev_song s; if (!take(r, bytes, &s)) return false; ap(out, "{\"t\":\"song\",\"rev\":%lld,\"f\":%lld}", (long long)s.rev, (long long)s.h.f); return true; }
    case THUNDER_LIVE_EV_PADS: { thunder_live_ev_pads s; if (!take(r, bytes, &s)) return false; ap(out, "{\"t\":\"pads\",\"id\":%lld,\"f\":%lld}", (long long)s.id, (long long)s.h.f); return true; }
    case THUNDER_LIVE_EV_ERROR: { thunder_live_ev_error e; if (!take(r, bytes, &e)) return false;
      ap(out, "{\"t\":\"err\",\"where\":\"%s\",\"msg\":\"%s code %d value %d\",\"code\":%d,\"fatal\":%s,\"value\":%d,\"f\":%lld}", whereName(e.where), whereName(e.where), e.code, e.value, e.code, e.fatal ? "true" : "false", e.value, (long long)e.h.f); return true; }
    case THUNDER_LIVE_EV_LATE: { thunder_live_ev_late l; if (!take(r, bytes, &l)) return false;
      ap(out, "{\"t\":\"late\",\"notesLate\":%u,\"midiLateNotes\":%u,\"midiLateTicks\":%u,\"runnerSkips\":%u,\"fxRemakes\":%u,\"f\":%lld}", l.notes_late, l.midi_late_notes, l.midi_late_ticks, l.runner_skips, l.fx_remakes, (long long)l.h.f); return true; }
    default: return false;
  }
}

void EventQueue::push(std::string json, bool critical) {
  std::lock_guard<std::mutex> g(m_);
  if (q_.size() >= cap_) {
    size_t i = 0; while (i < q_.size() && q_[i].crit) i++;
    if (i < q_.size()) { q_.erase(q_.begin() + (long)i); dropped_++; }
    else { dropped_++; return; }
  }
  q_.push_back({std::move(json), critical});
}

std::string EventQueue::drain() {
  std::lock_guard<std::mutex> g(m_);
  if (q_.empty()) return std::string();
  std::string o = "[";
  for (size_t i = 0; i < q_.size(); i++) { if (i) o += ','; o += q_[i].s; }
  o += ']'; q_.clear();
  return o;
}

}  // namespace tpa
