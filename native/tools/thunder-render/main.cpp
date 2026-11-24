// thunder-render (U09): renders a .thunder song offline with the C++ core's sequencer -> sampler voices -> mixer (core/src/render) and
// writes a float32 stereo WAV.
//   thunder-render <song.thunder> <padsDir> <out.wav> [--frames N] [--block N] [--page-timing]
//   --page-timing: the voices timed as the page player's (voice/voice.h setPageTiming): for render-compare.sh's reading of a difference only
// <padsDir>/index.json: {"sr": 48000, "frames": N?, "vars": {vtrk, NSLOT, TFX} (the page's coreVars()), "kit": [{choke, syn, rel} | null, ...]
// (the page's coreKitV()), "pads": [{"pad": n, "file": "pad-n.f32", "len": samples, "sr": 48000, "stereo": false}, ...]}; a pad file is
// raw float32 (stereo: the left channel's samples, then the right's). native/tools/song-ref.mjs writes such a folder from the web app.
// The length: --frames, else index.json's frames, else until the song is over and every voice has ended.
// After the last frame the render runs on (not written) until the voice pool is empty: a voice that never ends is an error (exit 3).
// U34a: this program is a client of include/thunder/render.h (thunder_render_create / set_pad / read / drain): it no longer touches SongRenderer.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "rt/json.h"
#include "thunder/render.h"   // U34a: the render goes through the public C API (the same path a host uses)

using namespace thunder;   // (the JSON reader for index.json only)

static bool readFile(const std::string& path, std::string* out) { std::ifstream f(path, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); *out = ss.str(); return true; }
static void put32(std::string& s, uint32_t v) { for (int i = 0; i < 4; i++) s += (char)(v >> (8 * i) & 255); }
static void put16(std::string& s, uint32_t v) { for (int i = 0; i < 2; i++) s += (char)(v >> (8 * i) & 255); }

static bool writeWav(const std::string& path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
  const uint32_t n = (uint32_t)L.size(), bytes = n * 8; std::string h;
  h += "RIFF"; put32(h, 36 + bytes); h += "WAVEfmt "; put32(h, 16); put16(h, 3 /* IEEE float */); put16(h, 2); put32(h, (uint32_t)sr); put32(h, (uint32_t)sr * 8); put16(h, 8); put16(h, 32); h += "data"; put32(h, bytes);
  std::vector<float> il((size_t)n * 2); for (uint32_t i = 0; i < n; i++) { il[(size_t)i * 2] = L[i]; il[(size_t)i * 2 + 1] = R[i]; }
  std::ofstream f(path, std::ios::binary); if (!f) return false;
  f.write(h.data(), (std::streamsize)h.size()); f.write((const char*)il.data(), (std::streamsize)bytes); return (bool)f;
}

int main(int argc, char** argv) {
  if (argc < 4) { std::fprintf(stderr, "usage: thunder-render <song.thunder> <padsDir> <out.wav> [--frames N] [--block N] [--page-timing] [--fx] [--pdc] [--master] [--chains]\n"); return 2; }
  const std::string songPath = argv[1], dir = argv[2], outPath = argv[3]; int64_t frames = -1; int block = 128;
  bool pageTiming = false; for (int i = 4; i < argc; i++) if (!std::strcmp(argv[i], "--page-timing")) { pageTiming = true; for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1]; argc--; break; }
  bool fxOn = false; for (int i = 4; i < argc; i++) if (!std::strcmp(argv[i], "--fx")) { fxOn = true; for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1]; argc--; break; }
  bool chainsOn = false; for (int i = 4; i < argc; i++) if (!std::strcmp(argv[i], "--chains")) { chainsOn = true; for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1]; argc--; break; }
  bool pdc = false; for (int i = 4; i < argc; i++) if (!std::strcmp(argv[i], "--pdc")) { pdc = true; for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1]; argc--; break; }   // FX1: LATENCY COMP auto (the page's default): matters once an effect has latency (pump)
  bool master = false; for (int i = 4; i < argc; i++) if (!std::strcmp(argv[i], "--master")) { master = true; for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1]; argc--; break; }
  for (int i = 4; i + 1 < argc; i += 2) { if (!std::strcmp(argv[i], "--frames")) frames = std::atoll(argv[i + 1]); else if (!std::strcmp(argv[i], "--block")) block = std::atoi(argv[i + 1]); }
  if (block < 1 || block > 8192) { std::fprintf(stderr, "thunder-render: --block 1..8192\n"); return 2; }
  std::string text, idx;
  if (!readFile(songPath, &text)) { std::fprintf(stderr, "thunder-render: cannot read %s\n", songPath.c_str()); return 1; }
  if (!readFile(dir + "/index.json", &idx)) { std::fprintf(stderr, "thunder-render: cannot read %s/index.json\n", dir.c_str()); return 1; }
  JsonDoc doc; if (!doc.parse(idx)) { std::fprintf(stderr, "thunder-render: index.json is not JSON\n"); return 1; }
  Json I = doc.root(); const double sr = I.num("sr", 48000);
  if (frames < 0 && I.get("frames").isNum()) frames = (int64_t)I.num("frames");
  const std::string varsJson = json_write(I.get("vars"));   // the page's coreVars(), handed on as text
  thunder_render_opts o; std::memset(&o, 0, sizeof o); o.size = (uint32_t)sizeof o;
  o.sample_rate = sr; o.effects = fxOn; o.chains = chainsOn; o.pdc_mode = pdc ? 1 : 0; o.master_chain = master; o.page_timing = pageTiming;
  if (frames >= 0) { o.use_frames = 1; o.frames = frames; }
  if (I.get("vars").isObj()) { o.vars_json = (const uint8_t*)varsJson.data(); o.vars_bytes = varsJson.size(); }
  char err[256];
  thunder_render* R = thunder_render_create((const uint8_t*)text.data(), text.size(), &o, err, sizeof err);
  if (!R) { std::fprintf(stderr, "thunder-render: %s: %s\n", songPath.c_str(), err); return 1; }
  Json kit = I.arr("kit");
  for (size_t p = 0; p < kit.size() && p < (size_t)THUNDER_RENDER_PADS; p++) { Json k = kit.at(p); if (!k.isObj()) continue; thunder_render_set_pad_kit(R, (int)p, (int)k.num("choke"), k.boolean("syn") ? 1 : 0, k.num("rel")); }
  Json pl = I.arr("pads");
  for (size_t k = 0; k < pl.size(); k++) {
    Json P = pl.at(k); const int pad = (int)P.num("pad", -1); if (pad < 0 || pad >= THUNDER_RENDER_PADS) continue;
    std::string raw; if (!readFile(dir + "/" + std::string(P.str("file")), &raw)) { std::fprintf(stderr, "thunder-render: cannot read pad %d's file\n", pad); return 1; }
    std::vector<float> d(raw.size() / 4); std::memcpy(d.data(), raw.data(), d.size() * 4);
    const bool stereo = P.boolean("stereo"); const int64_t len = (int64_t)(d.size() / (stereo ? 2 : 1));
    const int rc = thunder_render_set_pad(R, pad, d.data(), stereo ? d.data() + len : nullptr, len, P.num("sr", sr));
    if (rc) { std::fprintf(stderr, "thunder-render: pad %d refused (code %d)\n", pad, rc); return 1; }
  }
  const int skip = thunder_render_latency(R);
  std::vector<float> L, Rt, bl((size_t)block), br((size_t)block);
  if (frames >= 0) { L.reserve((size_t)frames); Rt.reserve((size_t)frames); }
  for (;;) {
    const int64_t n = thunder_render_read(R, bl.data(), br.data(), block);
    if (n < 0) { std::fprintf(stderr, "thunder-render: read failed (code %lld)\n", (long long)n); return 1; }
    if (n == 0) break;
    L.insert(L.end(), bl.begin(), bl.begin() + n); Rt.insert(Rt.end(), br.begin(), br.begin() + n);
  }
  const int64_t written = (int64_t)L.size();
  thunder_render_stats s0; std::memset(&s0, 0, sizeof s0); s0.size = (uint32_t)sizeof s0; thunder_render_stats_get(R, &s0);
  const int atEnd = s0.voices_active; const bool overAtEnd = s0.song_over != 0;
  // the pool must empty once the song is over (U08's trap: a voice on a track with no output never ends; with the mixer every track has one)
  const int64_t extra = thunder_render_drain(R, (int64_t)(sr * 120)); const bool emptied = extra >= 0;
  thunder_render_stats s; std::memset(&s, 0, sizeof s); s.size = (uint32_t)sizeof s; thunder_render_stats_get(R, &s);
  double pk = 0; for (size_t i = 0; i < L.size(); i++) pk = std::max(pk, (double)std::max(std::fabs(L[i]), std::fabs(Rt[i])));
  thunder_render_free(R);
  if (!writeWav(outPath, L, Rt, (int)sr)) { std::fprintf(stderr, "thunder-render: cannot write %s\n", outPath.c_str()); return 1; }
  if (master) std::printf("thunder-render: master chain on, latency %d samples trimmed\n", skip);
  std::printf("thunder-render: %lld frames (%.2f s) at %d Hz, peak %.4f -> %s\n", (long long)written, written / sr, (int)sr, pk, outPath.c_str());
  std::printf("  steps %d, events %d, notes %d: played %d, no pad %d, MIDI-only %d, reversed+shaped (the page's) %d; mixer events %d\n", s.steps, s.events, s.notes, s.notes_played, s.notes_no_pad, s.notes_midi_only, s.notes_shaped_rev, s.mix_events);
  std::printf("  peak voices %d\n", s.peak_voices);
  std::printf("  at the last frame: song %s, %d voice(s) sounding; the pool emptied %.3f s later%s\n", overAtEnd ? "over" : "NOT over", atEnd, emptied ? extra / sr : 0.0, emptied ? "" : "  [NEVER: FAIL]");
  if (s.hit_limit) std::printf("  the render was cut at its length limit (1260 s)\n");
  const bool lost = s.voices_refused || s.voice_track_full || s.voice_auto_dropped || s.mix_auto_dropped || s.seq_dropped;
  if (lost) std::printf("  LOST: voices refused %u, track lists full %u, voice automation %u, mixer automation %u, sequencer %llu\n", s.voices_refused, s.voice_track_full, s.voice_auto_dropped, s.mix_auto_dropped, (unsigned long long)s.seq_dropped);
  if (s.chains_events_dropped || s.chains_events_bad || s.chains_chc_no_node || s.chains_failed_blocks || s.chains_feed_mismatch || s.chains_refused)   // (silent when all 0)
    std::fprintf(stderr, "thunder-render: chains: applied %llu, dropped %llu, bad %llu, chc without a node %llu, failed blocks %llu, feed mismatches %llu, refused %llu\n", (unsigned long long)s.chains_events_applied, (unsigned long long)s.chains_events_dropped, (unsigned long long)s.chains_events_bad, (unsigned long long)s.chains_chc_no_node, (unsigned long long)s.chains_failed_blocks, (unsigned long long)s.chains_feed_mismatch, (unsigned long long)s.chains_refused);
  if (!emptied || !s.song_over) return 3;
  return lost ? 4 : 0;
}
