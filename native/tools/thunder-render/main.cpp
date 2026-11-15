// thunder-render (U09): renders a .thunder song offline with the C++ core's sequencer -> sampler voices -> mixer (core/src/render) and
// writes a float32 stereo WAV.
//   thunder-render <song.thunder> <padsDir> <out.wav> [--frames N] [--block N] [--page-timing]
//   --page-timing: the voices timed as the page player's (voice/voice.h setPageTiming): for render-compare.sh's reading of a difference only
// <padsDir>/index.json: {"sr": 48000, "frames": N?, "vars": {vtrk, NSLOT, TFX} (the page's coreVars()), "kit": [{choke, syn, rel} | null, ...]
// (the page's coreKitV()), "pads": [{"pad": n, "file": "pad-n.f32", "len": samples, "sr": 48000, "stereo": false}, ...]}; a pad file is
// raw float32 (stereo: the left channel's samples, then the right's). native/tools/song-ref.mjs writes such a folder from the web app.
// The length: --frames, else index.json's frames, else until the song is over and every voice has ended.
// After the last frame the render runs on (not written) until the voice pool is empty: a voice that never ends is an error (exit 3).
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "render/render.h"
#include "rt/json.h"

using namespace thunder;

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
  if (argc < 4) { std::fprintf(stderr, "usage: thunder-render <song.thunder> <padsDir> <out.wav> [--frames N] [--block N]\n"); return 2; }
  const std::string songPath = argv[1], dir = argv[2], outPath = argv[3]; int64_t frames = -1; int block = 128;
  bool pageTiming = false; for (int i = 4; i < argc; i++) if (!std::strcmp(argv[i], "--page-timing")) { pageTiming = true; for (int j = i; j + 1 < argc; j++) argv[j] = argv[j + 1]; argc--; break; }
  for (int i = 4; i + 1 < argc; i += 2) { if (!std::strcmp(argv[i], "--frames")) frames = std::atoll(argv[i + 1]); else if (!std::strcmp(argv[i], "--block")) block = std::atoi(argv[i + 1]); }
  if (block < 1 || block > 8192) { std::fprintf(stderr, "thunder-render: --block 1..8192\n"); return 2; }
  std::string text, idx;
  if (!readFile(songPath, &text)) { std::fprintf(stderr, "thunder-render: cannot read %s\n", songPath.c_str()); return 1; }
  if (!readFile(dir + "/index.json", &idx)) { std::fprintf(stderr, "thunder-render: cannot read %s/index.json\n", dir.c_str()); return 1; }
  Song song; JsonError err;
  if (!song.load(text.data(), text.size(), &err)) { std::fprintf(stderr, "thunder-render: %s is not a song\n", songPath.c_str()); return 1; }
  JsonDoc doc; if (!doc.parse(idx)) { std::fprintf(stderr, "thunder-render: index.json is not JSON\n"); return 1; }
  Json I = doc.root(); const double sr = I.num("sr", 48000);
  if (frames < 0 && I.get("frames").isNum()) frames = (int64_t)I.num("frames");
  SeqVars vars; vars.load(I.get("vars"));
  static RenderPads pads; std::vector<std::vector<float>> store;
  Json kit = I.arr("kit");
  for (size_t p = 0; p < kit.size() && p < (size_t)kRenderPads; p++) { Json k = kit.at(p); if (!k.isObj()) continue; pads.kit[p].choke = (int)k.num("choke"); pads.kit[p].syn = k.boolean("syn"); pads.kit[p].rel = k.num("rel"); }
  Json pl = I.arr("pads"); store.reserve(pl.size());
  for (size_t k = 0; k < pl.size(); k++) {
    Json P = pl.at(k); const int pad = (int)P.num("pad", -1); if (pad < 0 || pad >= kRenderPads) continue;
    std::string raw; if (!readFile(dir + "/" + std::string(P.str("file")), &raw)) { std::fprintf(stderr, "thunder-render: cannot read pad %d's file\n", pad); return 1; }
    store.emplace_back(raw.size() / 4); std::memcpy(store.back().data(), raw.data(), store.back().size() * 4);
    const bool stereo = P.boolean("stereo"); const int len = (int)(store.back().size() / (stereo ? 2 : 1));
    VoiceBuf& b = pads.buf[pad]; b.data = store.back().data(); b.data2 = stereo ? store.back().data() + len : nullptr; b.len = len; b.sr = P.num("sr", sr);
  }
  SeqOpts o; o.row = song.renderFrom; o.once = true;
  SongRenderer R(song, vars, pads, sr, block, o);
  R.pool().setPageTiming(pageTiming);
  std::vector<float> L, Rt, bl((size_t)block), br((size_t)block);
  if (frames >= 0) { L.reserve((size_t)frames); Rt.reserve((size_t)frames); }
  const int64_t cap = (int64_t)(sr * 1260);   // (renderOut's own limit is 1200 s of song)
  while (frames >= 0 ? R.frame() < frames : (!(R.songOver() && R.voices() == 0) && R.frame() < cap)) {
    const int n = frames >= 0 ? (int)std::min<int64_t>(block, frames - R.frame()) : block;
    R.block(bl.data(), br.data(), n); L.insert(L.end(), bl.begin(), bl.begin() + n); Rt.insert(Rt.end(), br.begin(), br.begin() + n);
  }
  const int64_t written = R.frame(); const int atEnd = R.voices(); const bool overAtEnd = R.songOver();
  // the pool must empty once the song is over (U08's trap: a voice on a track with no output never ends; with the mixer every track has one)
  int64_t extra = 0; while (!(R.songOver() && R.voices() == 0) && extra < (int64_t)(sr * 120)) { R.block(bl.data(), br.data(), block); extra += block; }
  const RenderStats s = R.stats(); double pk = 0; for (size_t i = 0; i < L.size(); i++) pk = std::max(pk, (double)std::max(std::fabs(L[i]), std::fabs(Rt[i])));
  if (!writeWav(outPath, L, Rt, (int)sr)) { std::fprintf(stderr, "thunder-render: cannot write %s\n", outPath.c_str()); return 1; }
  std::printf("thunder-render: %lld frames (%.2f s) at %d Hz, peak %.4f -> %s\n", (long long)written, written / sr, (int)sr, pk, outPath.c_str());
  std::printf("  steps %d, events %d (largest step %d of %d), notes %d: played %d, no pad %d, MIDI-only %d, reversed+shaped (the page's) %d; mixer events %d\n", s.steps, s.events, s.peakStepEvents, kRenderEvents, s.notes, s.notesPlayed, s.notesNoPad, s.notesMidiOnly, s.notesShapedRev, s.mixEvents);
  std::printf("  peak voices %d of %d, longest voice lane %d, longest mixer lane %d (of %d / %d)\n", s.peakVoices, R.pool().capacity(), s.peakVoiceLane, s.peakMixLane, Mixer::kAutoUi, Mixer::kAutoSong);
  std::printf("  at the last frame: song %s, %d voice(s) sounding; the pool emptied %.3f s later%s\n", overAtEnd ? "over" : "NOT over", atEnd, extra / sr, R.voices() == 0 && R.songOver() ? "" : "  [NEVER: FAIL]");
  const bool lost = s.voiceDropped || s.voiceOverflow || s.voiceAutoDropped || s.mixAutoDropped || s.seqDropped;
  if (lost) std::printf("  LOST: voices refused %u, track lists full %u, voice automation %u, mixer automation %u, sequencer %llu\n", s.voiceDropped, s.voiceOverflow, s.voiceAutoDropped, s.mixAutoDropped, (unsigned long long)s.seqDropped);
  if (!(R.voices() == 0 && R.songOver())) return 3;
  return lost ? 4 : 0;
}
