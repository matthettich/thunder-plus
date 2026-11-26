// N2 H1b: see live_events.h.
#include "live_events.h"

#include <cmath>
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
    case THUNDER_LIVE_EV_FXLVL: { thunder_live_ev_fxlvl v; if (!take(r, bytes, &v)) return false;   // M4: {t:"fx.lvl", seq, f, tr, a:[8]} (METERS-DESIGN 4.4; the page's onFxLvl): linear peaks, 0 for a slot that did not report
      if (v.track < 0 || v.track > 32) return false;
      ap(out, "{\"t\":\"fx.lvl\",\"seq\":%u,\"f\":%lld,\"tr\":%d,\"a\":[", v.h.seq, (long long)v.h.f, v.track);
      for (int k = 0; k < 8; k++) { double x = (double)v.lvl[k]; if (!(x > 0.0) || x > 1e30) x = 0.0; ap(out, "%s%.5g", k ? "," : "", x); }
      out->append("]}"); return true; }
    case THUNDER_LIVE_EV_CHLVL: { thunder_live_ev_chlvl v; if (!take(r, bytes, &v)) return false;   // M3: {t:"ch.lvl", seq, f, tr, in:[l,r], out:[l,r]}: linear peaks of the track's chains node, in = what goes in (after TRIM), out = what comes out
      if (v.track < 0 || v.track > 31) return false;
      auto pk = [](float x) { return (std::isfinite(x) && x > 0.f && x < 1e30f) ? (double)x : 0.0; };
      ap(out, "{\"t\":\"ch.lvl\",\"seq\":%u,\"f\":%lld,\"tr\":%d,\"in\":[%.5g,%.5g],\"out\":[%.5g,%.5g]}", v.h.seq, (long long)v.h.f, v.track, pk(v.in[0]), pk(v.in[1]), pk(v.out[0]), pk(v.out[1])); return true; }
    case THUNDER_LIVE_EV_MASTERM: { thunder_live_ev_masterm v; if (!take(r, bytes, &v)) return false;   // M2: {t:"master.lvl", seq, f, inPk, outPk, tp, inLu, outLu, comp, compIn, lim, load, sup:[48] | null}: the page's mstMeter object (same field names); peaks linear, the rest dB as the master worklet gives them (loudness -180 = none)
      auto lin = [](float x) { return (std::isfinite(x) && x > 0.f && x < 1e30f) ? (double)x : 0.0; };
      auto db = [](float x, double dflt) { return (std::isfinite(x) && x > -1000.f && x < 1000.f) ? (double)x : dflt; };
      ap(out, "{\"t\":\"master.lvl\",\"seq\":%u,\"f\":%lld,\"inPk\":%.5g,\"outPk\":%.5g,\"tp\":%.5g,", v.h.seq, (long long)v.h.f, lin(v.in_pk), lin(v.out_pk), lin(v.tp));   // (ap's buffer is 256 bytes: two calls)
      ap(out, "\"inLu\":%.3f,\"outLu\":%.3f,\"comp\":%.3f,\"compIn\":%.3f,\"lim\":%.3f,\"load\":%.4g,\"sup\":", db(v.in_lu, -180.0), db(v.out_lu, -180.0), db(v.comp, 0.0), db(v.comp_in, -200.0), db(v.lim, 0.0), lin(v.load));
      if (v.flags & 1u) { out->push_back('['); for (int b = 0; b < 48; b++) ap(out, "%s%.3f", b ? "," : "", db(v.sup[b], 0.0)); out->push_back(']'); } else out->append("null");
      out->push_back('}'); return true; }
    case THUNDER_LIVE_EV_FXDYN: { thunder_live_ev_fxdyn v; if (!take(r, bytes, &v)) return false;   // M4: {t:"fx.dyn", seq, f, tr, d:[[slot, "type", red, in, out], ...]}: dB as the core gives them (-180 = silence); the type is the page's string
      if (v.track < 0 || v.track > 32) return false;
      ap(out, "{\"t\":\"fx.dyn\",\"seq\":%u,\"f\":%lld,\"tr\":%d,\"d\":[", v.h.seq, (long long)v.h.f, v.track);
      int n = v.n < 0 ? 0 : v.n > 8 ? 8 : v.n, first = 1;
      for (int i = 0; i < n; i++) {
        const thunder_live_ev_fxdyn_ent& e = v.ent[i];
        const char* nm = thunder_fx_type_name(e.type);
        if (e.slot < 0 || e.slot > 7 || !nm) continue;   // an entry the page could not match to a slot would be dropped by it anyway
        auto db = [](float x, double dflt) { return (std::isfinite(x) && x > -1000.f && x < 1000.f) ? (double)x : dflt; };
        ap(out, "%s[%d,\"%s\",%.3f,%.3f,%.3f]", first ? "" : ",", e.slot, nm, db(e.red_db, 0.0), db(e.in_db, -180.0), db(e.out_db, -180.0)); first = 0;
      }
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
  std::string o;   // one event per line: a JSON object never contains a raw newline (strings are escaped), so the shell splits on '\n' without parsing
  for (size_t i = 0; i < q_.size(); i++) { if (i) o += '\n'; o += q_[i].s; }
  q_.clear();
  return o;
}

}  // namespace tpa
